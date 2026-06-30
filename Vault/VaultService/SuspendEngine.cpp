// ============================================================================
// Vault Service - Suspend Engine Implementation
// ============================================================================

#include "SuspendEngine.h"
#include <iostream>

namespace Vault {

SuspendEngine::SuspendEngine()
    : m_hNtdll(nullptr)
    , m_pNtSuspendProcess(nullptr)
    , m_pNtResumeProcess(nullptr) {
}

SuspendEngine::~SuspendEngine() {
    ResumeAll();
    if (m_hNtdll) {
        FreeLibrary(m_hNtdll);
    }
}

bool SuspendEngine::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_hNtdll) {
        return true; // Already initialized
    }

    m_hNtdll = LoadLibraryW(L"ntdll.dll");
    if (!m_hNtdll) {
        std::cerr << "[SuspendEngine] Failed to load ntdll.dll: " << GetLastError() << std::endl;
        return false;
    }

    // Resolve NtSuspendProcess
    m_pNtSuspendProcess = reinterpret_cast<NtSuspendProcessFn>(
        GetProcAddress(m_hNtdll, "NtSuspendProcess"));
    if (!m_pNtSuspendProcess) {
        std::cerr << "[SuspendEngine] Failed to resolve NtSuspendProcess" << std::endl;
        FreeLibrary(m_hNtdll);
        m_hNtdll = nullptr;
        return false;
    }

    // Resolve NtResumeProcess
    m_pNtResumeProcess = reinterpret_cast<NtResumeProcessFn>(
        GetProcAddress(m_hNtdll, "NtResumeProcess"));
    if (!m_pNtResumeProcess) {
        std::cerr << "[SuspendEngine] Failed to resolve NtResumeProcess" << std::endl;
        FreeLibrary(m_hNtdll);
        m_hNtdll = nullptr;
        return false;
    }

    return true;
}

HANDLE SuspendEngine::OpenProcessWithAccess(DWORD pid, DWORD access) const {
    HANDLE hProcess = OpenProcess(access, FALSE, pid);
    if (!hProcess) {
        // Try with reduced access if full access fails
        // Some system processes may require PROCESS_SUSPEND_RESUME only
        hProcess = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    }
    return hProcess;
}

bool SuspendEngine::SuspendProcess(DWORD pid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_pNtSuspendProcess) {
        return false; // Not initialized
    }

    // Check if already suspended
    auto it = m_suspendedProcesses.find(pid);
    if (it != m_suspendedProcesses.end() && it->second) {
        return true; // Already suspended
    }

    HANDLE hProcess = OpenProcessWithAccess(pid, PROCESS_SUSPEND_RESUME);
    if (!hProcess) {
        std::cerr << "[SuspendEngine] Failed to open process " << pid 
                  << " for suspension: " << GetLastError() << std::endl;
        return false;
    }

    NTSTATUS status = m_pNtSuspendProcess(hProcess);
    CloseHandle(hProcess);

    if (status == 0) { // STATUS_SUCCESS
        m_suspendedProcesses[pid] = true;
        return true;
    } else {
        std::cerr << "[SuspendEngine] NtSuspendProcess failed for PID " << pid 
                  << " with status: 0x" << std::hex << status << std::dec << std::endl;
        return false;
    }
}

bool SuspendEngine::ResumeProcess(DWORD pid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_pNtResumeProcess) {
        return false; // Not initialized
    }

    // Check if tracked as suspended
    auto it = m_suspendedProcesses.find(pid);
    if (it == m_suspendedProcesses.end() || !it->second) {
        return true; // Not suspended (or not tracked), consider success
    }

    HANDLE hProcess = OpenProcessWithAccess(pid, PROCESS_SUSPEND_RESUME);
    if (!hProcess) {
        std::cerr << "[SuspendEngine] Failed to open process " << pid 
                  << " for resumption: " << GetLastError() << std::endl;
        // Remove from tracking since we can't access it
        m_suspendedProcesses.erase(it);
        return false;
    }

    NTSTATUS status = m_pNtResumeProcess(hProcess);
    CloseHandle(hProcess);

    if (status == 0) { // STATUS_SUCCESS
        it->second = false;
        // Optionally remove from map entirely to keep it clean
        m_suspendedProcesses.erase(it);
        return true;
    } else {
        std::cerr << "[SuspendEngine] NtResumeProcess failed for PID " << pid 
                  << " with status: 0x" << std::hex << status << std::dec << std::endl;
        return false;
    }
}

bool SuspendEngine::IsSuspended(DWORD pid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_suspendedProcesses.find(pid);
    return it != m_suspendedProcesses.end() && it->second;
}

size_t SuspendEngine::GetSuspendedCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& pair : m_suspendedProcesses) {
        if (pair.second) {
            ++count;
        }
    }
    return count;
}

void SuspendEngine::ResumeAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_pNtResumeProcess) {
        return; // Not initialized
    }

    for (auto& pair : m_suspendedProcesses) {
        if (!pair.second) continue; // Not suspended
        
        DWORD pid = pair.first;
        HANDLE hProcess = OpenProcessWithAccess(pid, PROCESS_SUSPEND_RESUME);
        if (hProcess) {
            m_pNtResumeProcess(hProcess);
            CloseHandle(hProcess);
        }
    }
    
    m_suspendedProcesses.clear();
}

} // namespace Vault
