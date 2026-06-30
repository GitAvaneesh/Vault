// ============================================================================
// Vault Service - Process Watcher Implementation
// ============================================================================

#include "ProcessWatcher.h"
#include <tlhelp32.h>
#include <iostream>
#include <chrono>
#include <algorithm>

namespace Vault {

ProcessWatcher::ProcessWatcher()
    : m_suspendEngine(nullptr)
    , m_configStore(nullptr)
    , m_running(false)
    , m_pollIntervalMs(POLL_INTERVAL_MS) {
}

ProcessWatcher::~ProcessWatcher() {
    Stop();
}

void ProcessWatcher::Initialize(SuspendEngine* suspendEngine, ConfigStore* configStore) {
    m_suspendEngine = suspendEngine;
    m_configStore = configStore;
}

void ProcessWatcher::Start() {
    if (m_running) return;
    
    m_running = true;
    m_watchThread = std::thread(&ProcessWatcher::WatchLoop, this);
}

void ProcessWatcher::Stop() {
    if (!m_running) return;
    
    m_running = false;
    if (m_watchThread.joinable()) {
        m_watchThread.join();
    }
}

bool ProcessWatcher::IsRunning() const {
    return m_running;
}

void ProcessWatcher::SetOnLockedAppLaunched(OnLockedAppLaunched callback) {
    m_onLockedAppLaunched = callback;
}

void ProcessWatcher::SetOnProcessResumed(OnProcessResumed callback) {
    m_onProcessResumed = callback;
}

void ProcessWatcher::SetOnProcessTerminated(OnProcessTerminated callback) {
    m_onProcessTerminated = callback;
}

std::string ProcessWatcher::GetAppIdFromPath(const std::wstring& path) {
    // Simple FNV-1a hash for app ID
    std::string narrow(path.begin(), path.end());
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (char c : narrow) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return std::to_string(hash);
}

void ProcessWatcher::WatchLoop() {
    std::cout << "[ProcessWatcher] Starting watch loop..." << std::endl;
    
    while (m_running) {
        auto startTime = std::chrono::steady_clock::now();
        
        CheckForNewProcesses();
        CheckForExitedProcesses();
        
        // Sleep for remaining interval time
        auto endTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        
        int sleepTime = std::max(0, m_pollIntervalMs - static_cast<int>(elapsed));
        if (sleepTime > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
        }
    }
    
    std::cout << "[ProcessWatcher] Watch loop stopped" << std::endl;
}

void ProcessWatcher::CheckForNewProcesses() {
    if (!m_suspendEngine || !m_configStore) return;
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    
    PROCESSENTRY32W pe32 = {0};
    pe32.dwSize = sizeof(pe32);
    
    std::unordered_map<DWORD, std::wstring> currentSnapshot;
    
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            // Get full process path
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
            if (hProcess) {
                wchar_t path[MAX_PATH];
                DWORD size = _countof(path);
                if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                    currentSnapshot[pe32.th32ProcessID] = path;
                }
                CloseHandle(hProcess);
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
    
    // Find new processes not in previous snapshot
    for (const auto& [pid, path] : currentSnapshot) {
        if (m_previousSnapshot.find(pid) == m_previousSnapshot.end()) {
            // New process detected
            HandleNewProcess(pid, path);
        }
    }
    
    m_previousSnapshot = currentSnapshot;
}

void ProcessWatcher::HandleNewProcess(DWORD pid, const std::wstring& path) {
    if (!m_suspendEngine || !m_configStore) return;
    
    std::string appId = GetAppIdFromPath(path);
    
    // Check if this app is locked
    if (!m_configStore->IsAppLocked(appId)) {
        return; // Not a locked app
    }
    
    std::cout << "[ProcessWatcher] Locked app launched: " << appId 
              << " (PID: " << pid << ", Path: " << path << ")" << std::endl;
    
    // Suspend the process immediately
    if (m_suspendEngine->SuspendProcess(pid)) {
        SuspendedProcessInfo info;
        info.appId = appId;
        info.path = path;
        info.suspendTime = static_cast<uint64_t>(std::time(nullptr));
        info.awaitingAuth = true;
        m_suspendedProcesses[pid] = info;
        
        // Notify UI to show auth overlay
        if (m_onLockedAppLaunched) {
            m_onLockedAppLaunched(appId, pid, path);
        }
    } else {
        std::cerr << "[ProcessWatcher] Failed to suspend process " << pid << std::endl;
    }
}

void ProcessWatcher::CheckForExitedProcesses() {
    // Check our suspended processes for any that have exited
    std::vector<DWORD> exitedPids;
    
    for (auto& [pid, info] : m_suspendedProcesses) {
        HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (hProcess) {
            DWORD result = WaitForSingleObject(hProcess, 0);
            CloseHandle(hProcess);
            
            if (result == WAIT_OBJECT_0) {
                // Process has exited
                exitedPids.push_back(pid);
            }
        } else {
            // Can't open process - likely already exited
            exitedPids.push_back(pid);
        }
    }
    
    // Clean up exited processes
    for (DWORD pid : exitedPids) {
        auto it = m_suspendedProcesses.find(pid);
        if (it != m_suspendedProcesses.end()) {
            std::cout << "[ProcessWatcher] Suspended process exited: " << it->second.appId 
                      << " (PID: " << pid << ")" << std::endl;
            
            // Log session end
            if (m_configStore) {
                m_configStore->LogSessionEnd(it->second.appId, 
                                              static_cast<uint64_t>(std::time(nullptr)));
            }
            
            // Notify UI
            if (m_onProcessTerminated) {
                m_onProcessTerminated(it->second.appId, pid);
            }
            
            m_suspendedProcesses.erase(it);
        }
    }
}

} // namespace Vault
