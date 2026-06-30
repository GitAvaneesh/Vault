#pragma once

// ============================================================================
// Vault Service - Configuration Store
// ============================================================================
// Stores configuration in an encrypted JSON file using Windows DPAPI.
// Manages:
// - Locked state per application
// - PIN/password credentials (salted PBKDF2 hash, never plaintext)
// - Exclusion list
// - Session logs
// ============================================================================

#ifndef VAULT_CONFIG_STORE_H
#define VAULT_CONFIG_STORE_H

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "../Shared/Types.h"
#include "../Shared/IPCProtocol.h"

namespace Vault {

class ConfigStore {
public:
    ConfigStore();
    ~ConfigStore();

    // Initialize - creates/loads config file, sets up DPAPI
    bool Initialize();

    // Application lock state
    bool IsAppLocked(const std::string& appId) const;
    void SetAppLocked(const std::string& appId, bool locked);
    std::unordered_map<std::string, bool> GetAllLockStates() const;

    // PIN management
    bool HasPIN() const;
    bool SetPIN(const std::wstring& pin); // 4 digits
    bool VerifyPIN(const std::wstring& pin) const;
    void ClearPIN();

    // Password management
    bool HasPassword() const;
    bool SetPassword(const std::wstring& password);
    bool VerifyPassword(const std::wstring& password) const;
    void ClearPassword();

    // Exclusion list
    std::vector<std::wstring> GetExclusionList() const;
    void SetExclusionList(const std::vector<std::wstring>& exclusions);

    // Session logging
    void LogSessionStart(const std::string& appId, uint64_t timestamp);
    void LogSessionEnd(const std::string& appId, uint64_t timestamp);
    void LogAuthFailure(const std::string& appId, const std::string& method, uint64_t timestamp);
    IPC::SessionStats GetSessionStats(const std::string& appId) const;

    // Auth method availability
    bool IsFaceIDAvailable() const;
    bool IsTouchIDAvailable() const;

private:
    std::wstring m_configPath;
    mutable std::mutex m_mutex;

    // In-memory state (loaded from encrypted file)
    struct ConfigData {
        std::unordered_map<std::string, bool> lockStates;
        
        // Credentials - stored as salt+hash, never plaintext
        std::vector<unsigned char> pinSalt;
        std::vector<unsigned char> pinHash;
        bool hasPin;
        
        std::vector<unsigned char> passwordSalt;
        std::vector<unsigned char> passwordHash;
        bool hasPassword;
        
        std::vector<std::wstring> exclusions;
        
        // Session logs - stored as JSON array
        struct SessionLog {
            std::string appId;
            uint64_t startTime;
            uint64_t endTime; // 0 if still active
        };
        std::vector<SessionLog> sessionLogs;
        
        struct AuthFailure {
            std::string appId;
            std::string method;
            uint64_t timestamp;
        };
        std::vector<AuthFailure> authFailures;
    } m_data;

    // File I/O with DPAPI encryption
    bool LoadConfig();
    bool SaveConfig();
    
    // DPAPI helpers
    std::vector<unsigned char> EncryptData(const std::vector<unsigned char>& plaintext);
    std::vector<unsigned char> DecryptData(const std::vector<unsigned char>& ciphertext);
    
    // PBKDF2 hashing via BCrypt
    std::vector<unsigned char> GenerateSalt(size_t size = PBKDF2_SALT_SIZE);
    std::vector<unsigned char> HashCredential(const std::wstring& credential, 
                                               const std::vector<unsigned char>& salt);
    
    // Helper to get config directory
    std::wstring GetConfigDirectory();
};

} // namespace Vault

#endif // VAULT_CONFIG_STORE_H
