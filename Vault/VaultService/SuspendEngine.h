#pragma once

// ============================================================================
// Vault Service - Suspend Engine
// ============================================================================
// Wraps NtSuspendProcess and NtResumeProcess from ntdll.dll.
// These are undocumented APIs, so we dynamically resolve them via GetProcAddress.
// 
// NOTE: A kernel-mode implementation using PsSetCreateProcessNotifyRoutine would
// provide instant process suspension at creation time, but requires a signed driver.
// This user-mode implementation polls CreateToolhelp32Snapshot at ~300ms intervals.
// ============================================================================

#ifndef VAULT_SUSPEND_ENGINE_H
#define VAULT_SUSPEND_ENGINE_H

#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <unordered_map>
#include <mutex>

namespace Vault {

// Function pointer types for undocumented ntdll functions
using NtSuspendProcessFn = NTSTATUS (NTAPI*)(HANDLE ProcessHandle);
using NtResumeProcessFn = NTSTATUS (NTAPI*)(HANDLE ProcessHandle);

class SuspendEngine {
public:
    SuspendEngine();
    ~SuspendEngine();

    // Initialize - loads ntdll and resolves function pointers
    // Returns true on success, false if ntdll functions not available
    bool Initialize();

    // Suspend a process by PID
    // Returns true on success, false on failure (check GetLastError)
    bool SuspendProcess(DWORD pid);

    // Resume a suspended process by PID
    // Returns true on success, false on failure
    bool ResumeProcess(DWORD pid);

    // Check if a process is currently suspended
    bool IsSuspended(DWORD pid) const;

    // Get the count of currently suspended processes
    size_t GetSuspendedCount() const;

    // Cleanup - resume all suspended processes before shutdown
    void ResumeAll();

private:
    HMODULE m_hNtdll;
    NtSuspendProcessFn m_pNtSuspendProcess;
    NtResumeProcessFn m_pNtResumeProcess;

    std::unordered_map<DWORD, bool> m_suspendedProcesses; // pid -> isSuspended
    mutable std::mutex m_mutex;

    // Helper to open process with required access
    HANDLE OpenProcessWithAccess(DWORD pid, DWORD access) const;
};

} // namespace Vault

#endif // VAULT_SUSPEND_ENGINE_H
