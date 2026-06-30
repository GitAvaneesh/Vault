#pragma once

// ============================================================================
// Vault Service - Process Watcher
// ============================================================================
// Polls for new processes and checks if they match locked applications.
// When a locked app is detected:
// 1. Immediately suspend it via SuspendEngine
// 2. Notify UI via IPC to show auth overlay
// 3. Wait for auth result (resume or terminate)
//
// NOTE: This uses polling via CreateToolhelp32Snapshot at ~300ms intervals.
// A kernel driver using PsSetCreateProcessNotifyRoutine would provide instant
// notification but requires a signed driver and is out of scope for v1.
// ============================================================================

#ifndef VAULT_PROCESS_WATCHER_H
#define VAULT_PROCESS_WATCHER_H

#include <windows.h>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <functional>
#include "SuspendEngine.h"
#include "ConfigStore.h"
#include "../Shared/Types.h"

namespace Vault {

class ProcessWatcher {
public:
    ProcessWatcher();
    ~ProcessWatcher();

    // Initialize with references to dependencies
    void Initialize(SuspendEngine* suspendEngine, ConfigStore* configStore);

    // Start the watcher thread
    void Start();

    // Stop the watcher thread
    void Stop();

    // Check if running
    bool IsRunning() const;

    // Callback types for events
    using OnLockedAppLaunched = std::function<void(const std::string& appId, DWORD pid, const std::wstring& path)>;
    using OnProcessResumed = std::function<void(const std::string& appId, DWORD pid)>;
    using OnProcessTerminated = std::function<void(const std::string& appId, DWORD pid)>;

    // Set event callbacks
    void SetOnLockedAppLaunched(OnLockedAppLaunched callback);
    void SetOnProcessResumed(OnProcessResumed callback);
    void SetOnProcessTerminated(OnProcessTerminated callback);

private:
    SuspendEngine* m_suspendEngine;
    ConfigStore* m_configStore;
    
    std::thread m_watchThread;
    std::atomic<bool> m_running;
    int m_pollIntervalMs;

    // Track suspended processes: pid -> (appId, startTime)
    struct SuspendedProcessInfo {
        std::string appId;
        std::wstring path;
        uint64_t suspendTime;
        bool awaitingAuth;
    };
    std::unordered_map<DWORD, SuspendedProcessInfo> m_suspendedProcesses;

    // Previous snapshot for change detection
    std::unordered_map<DWORD, std::wstring> m_previousSnapshot;

    // Callbacks
    OnLockedAppLaunched m_onLockedAppLaunched;
    OnProcessResumed m_onProcessResumed;
    OnProcessTerminated m_onProcessTerminated;

    // Main watcher loop
    void WatchLoop();

    // Take a process snapshot and compare
    void CheckForNewProcesses();

    // Handle a newly detected process
    void HandleNewProcess(DWORD pid, const std::wstring& path);

    // Check for exited processes
    void CheckForExitedProcesses();

    // Helper to get app ID from path
    std::string GetAppIdFromPath(const std::wstring& path);
};

} // namespace Vault

#endif // VAULT_PROCESS_WATCHER_H
