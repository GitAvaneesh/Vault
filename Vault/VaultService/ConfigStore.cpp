// ============================================================================
// Vault Service - Configuration Store Implementation
// ============================================================================

#include "ConfigStore.h"
#include <bcrypt.h>
#include <dpapi.h>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace Vault {

ConfigStore::ConfigStore() : m_data{} {
    m_data.hasPin = false;
    m_data.hasPassword = false;
}

ConfigStore::~ConfigStore() {
}

std::wstring ConfigStore::GetConfigDirectory() {
    wchar_t path[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path);
    if (SUCCEEDED(hr)) {
        return std::wstring(path) + L"\\Vault";
    }
    return fs::temp_directory_path().wstring() + L"\\Vault";
}

bool ConfigStore::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Get config directory and create if needed
    std::wstring configDir = GetConfigDirectory();
    try {
        fs::create_directories(configDir);
    } catch (...) {
        return false;
    }
    
    m_configPath = configDir + L"\\config.dat"; // Encrypted binary file
    
    // Load existing config or create new
    if (!LoadConfig()) {
        // First run - save defaults
        SaveConfig();
    }
    
    return true;
}

std::vector<unsigned char> ConfigStore::GenerateSalt(size_t size) {
    std::vector<unsigned char> salt(size);
    
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, 
                            CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        CryptGenRandom(hProv, static_cast<DWORD>(size), salt.data());
        CryptReleaseContext(hProv, 0);
    } else {
        // Fallback to C++ random (less secure but functional)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (size_t i = 0; i < size; ++i) {
            salt[i] = static_cast<unsigned char>(dis(gen));
        }
    }
    
    return salt;
}

std::vector<unsigned char> ConfigStore::HashCredential(
    const std::wstring& credential, 
    const std::vector<unsigned char>& salt) {
    
    std::vector<unsigned char> hash(32); // 256-bit output
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD dataLen = 0;
    
    // Open PBKDF2 algorithm
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_PBKDF2_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return hash;
    }
    
    // Convert credential to bytes
    std::string narrow(credential.begin(), credential.end());
    std::vector<unsigned char> credentialBytes(narrow.begin(), narrow.end());
    
    // Set iteration count
    DWORD iterations = PBKDF2_ITERATIONS;
    status = BCryptSetProperty(hAlg, BCRYPT_ITERATION_COUNT, 
                               reinterpret_cast<UCHAR*>(&iterations), 
                               sizeof(iterations), 0);
    
    if (BCRYPT_SUCCESS(status)) {
        // Create hash object
        status = BCryptCreateHash(hAlg, &hHash, nullptr, 0,
                                  salt.data(), static_cast<ULONG>(salt.size()), 0);
        
        if (BCRYPT_SUCCESS(status)) {
            // Hash the credential
            status = BCryptHashData(hHash, credentialBytes.data(), 
                                    static_cast<ULONG>(credentialBytes.size()), 0);
            
            if (BCRYPT_SUCCESS(status)) {
                // Get the hash value
                status = BCryptFinishHash(hHash, hash.data(), 
                                          static_cast<ULONG>(hash.size()), 0);
            }
            
            BCryptDestroyHash(hHash);
        }
    }
    
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return hash;
}

std::vector<unsigned char> ConfigStore::EncryptData(const std::vector<unsigned char>& plaintext) {
    DATA_BLOB input = {static_cast<DWORD>(plaintext.size()), 
                       const_cast<BYTE*>(plaintext.data())};
    DATA_BLOB output = {0, nullptr};
    
    if (CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, 
                         CRYPT_PROTECT_UI_FORBIDDEN, &output)) {
        std::vector<unsigned char> result(output.pbData, output.pbData + output.cbData);
        LocalFree(output.pbData);
        return result;
    }
    
    return {};
}

std::vector<unsigned char> ConfigStore::DecryptData(const std::vector<unsigned char>& ciphertext) {
    DATA_BLOB input = {static_cast<DWORD>(ciphertext.size()), 
                       const_cast<BYTE*>(ciphertext.data())};
    DATA_BLOB output = {0, nullptr};
    
    if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 
                           CRYPT_PROTECT_UI_FORBIDDEN, &output)) {
        std::vector<unsigned char> result(output.pbData, output.pbData + output.cbData);
        LocalFree(output.pbData);
        return result;
    }
    
    return {};
}

bool ConfigStore::LoadConfig() {
    if (!fs::exists(m_configPath)) {
        return false;
    }
    
    try {
        // Read encrypted file
        std::ifstream file(m_configPath, std::ios::binary);
        if (!file.is_open()) return false;
        
        std::vector<unsigned char> encrypted(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        file.close();
        
        // Decrypt
        auto decrypted = DecryptData(encrypted);
        if (decrypted.empty()) return false;
        
        // Parse JSON (simplified manual parsing)
        std::string json(decrypted.begin(), decrypted.end());
        
        // Parse lock states
        size_t pos = json.find("\"lockStates\"");
        if (pos != std::string::npos) {
            // Simple extraction - in production use a proper JSON library
            size_t braceStart = json.find('{', pos);
            if (braceStart != std::string::npos) {
                int braceCount = 1;
                size_t braceEnd = braceStart + 1;
                while (braceCount > 0 && braceEnd < json.size()) {
                    if (json[braceEnd] == '{') braceCount++;
                    if (json[braceEnd] == '}') braceCount--;
                    braceEnd++;
                }
                // Parse key-value pairs from this section
                // (simplified - production code should use rapidjson or similar)
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigStore::SaveConfig() {
    try {
        // Build JSON (simplified - production should use rapidjson/nlohmann)
        std::ostringstream oss;
        oss << "{\"hasPin\":" << (m_data.hasPin ? "true" : "false")
            << ",\"hasPassword\":" << (m_data.hasPassword ? "true" : "false")
            << ",\"lockStates\":{}";
        oss << "}";
        
        std::string json = oss.str();
        std::vector<unsigned char> plaintext(json.begin(), json.end());
        
        // Encrypt
        auto encrypted = EncryptData(plaintext);
        if (encrypted.empty()) return false;
        
        // Write to file
        std::ofstream file(m_configPath, std::ios::binary);
        if (!file.is_open()) return false;
        
        file.write(reinterpret_cast<const char*>(encrypted.data()), 
                   static_cast<std::streamsize>(encrypted.size()));
        file.close();
        
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigStore::IsAppLocked(const std::string& appId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_data.lockStates.find(appId);
    return it != m_data.lockStates.end() && it->second;
}

void ConfigStore::SetAppLocked(const std::string& appId, bool locked) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.lockStates[appId] = locked;
    SaveConfig();
}

std::unordered_map<std::string, bool> ConfigStore::GetAllLockStates() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.lockStates;
}

bool ConfigStore::HasPIN() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.hasPin;
}

bool ConfigStore::SetPIN(const std::wstring& pin) {
    if (pin.length() != 4) return false;
    
    // Validate all digits
    for (wchar_t c : pin) {
        if (c < L'0' || c > L'9') return false;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_data.pinSalt = GenerateSalt(PBKDF2_SALT_SIZE);
    m_data.pinHash = HashCredential(pin, m_data.pinSalt);
    m_data.hasPin = !m_data.pinHash.empty();
    
    return SaveConfig();
}

bool ConfigStore::VerifyPIN(const std::wstring& pin) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_data.hasPin || m_data.pinSalt.empty() || m_data.pinHash.empty()) {
        return false;
    }
    
    auto computedHash = HashCredential(pin, m_data.pinSalt);
    return computedHash == m_data.pinHash;
}

void ConfigStore::ClearPIN() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.pinSalt.clear();
    m_data.pinHash.clear();
    m_data.hasPin = false;
    SaveConfig();
}

bool ConfigStore::HasPassword() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.hasPassword;
}

bool ConfigStore::SetPassword(const std::wstring& password) {
    if (password.empty()) return false;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_data.passwordSalt = GenerateSalt(PBKDF2_SALT_SIZE);
    m_data.passwordHash = HashCredential(password, m_data.passwordSalt);
    m_data.hasPassword = !m_data.passwordHash.empty();
    
    return SaveConfig();
}

bool ConfigStore::VerifyPassword(const std::wstring& password) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_data.hasPassword || m_data.passwordSalt.empty() || m_data.passwordHash.empty()) {
        return false;
    }
    
    auto computedHash = HashCredential(password, m_data.passwordSalt);
    return computedHash == m_data.passwordHash;
}

void ConfigStore::ClearPassword() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.passwordSalt.clear();
    m_data.passwordHash.clear();
    m_data.hasPassword = false;
    SaveConfig();
}

std::vector<std::wstring> ConfigStore::GetExclusionList() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.exclusions;
}

void ConfigStore::SetExclusionList(const std::vector<std::wstring>& exclusions) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.exclusions = exclusions;
    SaveConfig();
}

void ConfigStore::LogSessionStart(const std::string& appId, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.sessionLogs.push_back({appId, timestamp, 0});
    SaveConfig();
}

void ConfigStore::LogSessionEnd(const std::string& appId, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Find the most recent unclosed session for this app
    for (auto it = m_data.sessionLogs.rbegin(); it != m_data.sessionLogs.rend(); ++it) {
        if (it->appId == appId && it->endTime == 0) {
            it->endTime = timestamp;
            break;
        }
    }
    
    SaveConfig();
}

void ConfigStore::LogAuthFailure(const std::string& appId, const std::string& method, 
                                  uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.authFailures.push_back({appId, method, timestamp});
    SaveConfig();
}

IPC::SessionStats ConfigStore::GetSessionStats(const std::string& appId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    IPC::SessionStats stats = {};
    stats.totalSessions = 0;
    stats.totalDurationSec = 0;
    stats.lastLoginTime = 0;
    stats.lastLogoutTime = 0;
    stats.failedAttempts = 0;
    
    // Count sessions and calculate duration
    for (const auto& log : m_data.sessionLogs) {
        if (log.appId == appId) {
            stats.totalSessions++;
            if (log.endTime > 0 && log.startTime > 0) {
                stats.totalDurationSec += (log.endTime - log.startTime);
            }
            if (log.startTime > stats.lastLoginTime) {
                stats.lastLoginTime = log.startTime;
            }
            if (log.endTime > stats.lastLogoutTime) {
                stats.lastLogoutTime = log.endTime;
            }
        }
    }
    
    // Count failures
    for (const auto& failure : m_data.authFailures) {
        if (failure.appId == appId) {
            stats.failedAttempts++;
            if (stats.recentFailures.size() < 10) {
                stats.recentFailures.push_back({failure.timestamp, failure.method});
            }
        }
    }
    
    return stats;
}

bool ConfigStore::IsFaceIDAvailable() const {
    // This would require checking Windows Hello capabilities
    // For now, return false - actual implementation needs WinRT interop
    return false;
}

bool ConfigStore::IsTouchIDAvailable() const {
    // Same as FaceID - requires WinRT UserConsentVerifier
    return false;
}

} // namespace Vault
