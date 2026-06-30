#pragma once

// ============================================================================
// Vault Common Types
// ============================================================================

#ifndef VAULT_TYPES_H
#define VAULT_TYPES_H

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace Vault {

// High-resolution clock alias
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using SystemClock = std::chrono::system_clock;

// Default exclusion list - also stored in config for user editing
inline const std::vector<std::wstring> DEFAULT_EXCLUSIONS = {
    L"node.exe",
    L"python.exe",
    L"python3.exe",
    L"pythonw.exe",
    L"cmd.exe",
    L"powershell.exe",
    L"pwsh.exe",
    L"explorer.exe",
    L"dwm.exe",
    L"svchost.exe",
    L"vault.exe",
    L"vaultservice.exe"
};

// System directories that are excluded by default
inline const std::vector<std::wstring> EXCLUDED_SYSTEM_DIRS = {
    L"C:\\Windows\\System32",
    L"C:\\Windows\\SysWOW64",
    L"C:\\Windows\\System"
};

// Configuration paths
inline const wchar_t CONFIG_DIR[] = L"Vault";
inline const wchar_t CONFIG_FILENAME[] = L"config.json";
inline const wchar_t ICONS_SUBDIR[] = L"icons";
inline const wchar_t LOGS_SUBDIR[] = L"logs";

// Named pipe name
inline const wchar_t PIPE_NAME[] = L"\\\\.\\pipe\\VaultIPC";

// Service names
inline const wchar_t SERVICE_NAME[] = L"VaultService";
inline const wchar_t SERVICE_DISPLAY_NAME[] = L"Vault App Locker Service";

// Window class names
inline const wchar_t UI_WINDOW_CLASS[] = L"VaultUIWindow";

// Message constants
constexpr uint32_t IPC_MAGIC = 0x5641554C; // "VAUL" in ASCII
constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB max message
constexpr int POLL_INTERVAL_MS = 300; // Process polling interval
constexpr int ICON_CACHE_DAYS = 30; // Days before icon cache is considered stale

// Auth constants
constexpr size_t PIN_MIN_LENGTH = 4;
constexpr size_t PIN_MAX_LENGTH = 4;
constexpr size_t PASSWORD_MIN_LENGTH = 1;
constexpr size_t PBKDF2_SALT_SIZE = 16; // bytes
constexpr int PBKDF2_ITERATIONS = 100000;

} // namespace Vault

#endif // VAULT_TYPES_H
