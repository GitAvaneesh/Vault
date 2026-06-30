#pragma once

// ============================================================================
// Vault IPC Protocol Specification
// ============================================================================
// Transport: Named pipe \\.\pipe\VaultIPC
// Message format: Length-prefixed JSON
//   - 4-byte little-endian uint32_t length prefix (does not include the 4 bytes themselves)
//   - Followed by UTF-8 JSON payload of exactly 'length' bytes
// All messages have a "type" field. Request/response pairs share a "requestId" field.
// ============================================================================

#ifndef VAULT_IPC_PROTOCOL_H
#define VAULT_IPC_PROTOCOL_H

#include <cstdint>
#include <string>

namespace Vault {
namespace IPC {

// Message types (JSON "type" field values)
enum class MessageType : uint8_t {
    // UI -> Service
    RequestAppList = 1,       // No payload, requests current app list
    ToggleLock = 2,           // Payload: {"appId": string, "locked": bool}
    AuthAttempt = 3,          // Payload: {"appId": string, "method": string, "payload": any}
    RequestStats = 4,         // Payload: {"appId": string}
    SetPIN = 5,               // Payload: {"pin": string (4 digits)}
    SetPassword = 6,          // Payload: {"password": string}
    ClearPIN = 7,             // No payload
    ClearPassword = 8,        // No payload
    GetExclusionList = 9,     // No payload
    SetExclusionList = 10,    // Payload: {"exclusions": [string, ...]}
    GetAuthMethods = 11,      // No payload, returns available auth methods
    CancelAuth = 12,          // Payload: {"appId": string} - user cancelled auth overlay
    
    // Service -> UI
    AppList = 101,            // Payload: {"apps": [AppEntry, ...]}
    AuthResult = 102,         // Payload: {"success": bool, "reason": string, "appId": string}
    StatsResult = 103,        // Payload: {"appId": string, "stats": SessionStats}
    ExclusionList = 104,      // Payload: {"exclusions": [string, ...]}
    AuthMethods = 105,        // Payload: {"methods": [string, ...], "available": {...}}
    LockedAppLaunched = 106,  // Payload: {"appId": string, "pid": number, "path": string}
    ProcessResumed = 107,     // Payload: {"appId": string, "pid": number}
    ProcessTerminated = 108,  // Payload: {"appId": string, "pid": number}
    SettingsChanged = 109,    // Payload: {} - notify UI to refresh settings
    
    // Error
    Error = 255               // Payload: {"code": number, "message": string, "requestId": number}
};

// Authentication methods
enum class AuthMethod : uint8_t {
    None = 0,
    FaceID = 1,
    TouchID = 2,
    PIN = 3,
    Password = 4
};

inline std::string AuthMethodToString(AuthMethod method) {
    switch (method) {
        case AuthMethod::FaceID: return "faceId";
        case AuthMethod::TouchID: return "touchId";
        case AuthMethod::PIN: return "pin";
        case AuthMethod::Password: return "password";
        default: return "none";
    }
}

inline AuthMethod StringToAuthMethod(const std::string& str) {
    if (str == "faceId") return AuthMethod::FaceID;
    if (str == "touchId") return AuthMethod::TouchID;
    if (str == "pin") return AuthMethod::PIN;
    if (str == "password") return AuthMethod::Password;
    return AuthMethod::None;
}

// Application entry structure
struct AppEntry {
    std::string id;           // Unique identifier (SHA256 hash of full path)
    std::string name;         // Display name (from registry/manifest or filename)
    std::string path;         // Full path to executable
    std::string iconPath;     // Path to cached PNG icon (or empty if not extracted)
    bool locked;              // Current lock state
    bool isGame;              // Heuristic: true if from Steam or common game paths
    uint64_t lastSeen;        // Unix timestamp of last detection
};

// Session statistics per app
struct SessionStats {
    uint64_t totalSessions;       // Total unlock sessions
    uint64_t totalDurationSec;    // Cumulative duration unlocked (seconds)
    uint64_t lastLoginTime;       // Unix timestamp of last successful auth
    uint64_t lastLogoutTime;      // Unix timestamp of last process exit
    uint64_t failedAttempts;      // Total failed auth attempts
    struct FailedAttempt {
        uint64_t timestamp;
        std::string method;
    };
    std::vector<FailedAttempt> recentFailures; // Last 10 failures
};

// Auth availability info
struct AuthAvailability {
    bool faceIdAvailable;
    bool touchIdAvailable;
    bool pinSet;
    bool passwordSet;
    std::string consentVerifierStatus; // "available", "device_busy", "not_configured", etc.
};

// Helper to serialize/deserialize JSON is in the implementation files
// using a lightweight JSON library or Windows.Data.Json (WinRT)

} // namespace IPC
} // namespace Vault

#endif // VAULT_IPC_PROTOCOL_H
