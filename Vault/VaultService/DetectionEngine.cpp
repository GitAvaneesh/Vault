// ============================================================================
// Vault Service - Detection Engine Implementation
// ============================================================================

#include "DetectionEngine.h"
#include <shlobj.h>
#include <shellapi.h>
#include <atlbase.h>
#include <atlcomcli.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <cmath>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;

namespace Vault {

// Simple VDF parser for Steam libraryfolders.vdf
static std::vector<std::wstring> ParseLibraryFoldersVDF(const std::wstring& path) {
    std::vector<std::wstring> folders;
    std::wifstream file(path);
    if (!file.is_open()) return folders;

    std::wstring line;
    bool in_paths_section = false;
    
    while (std::getline(file, line)) {
        // Look for "path" entries like: "1"		"C:\\SteamLibrary"
        size_t quote1 = line.find(L'"');
        if (quote1 == std::wstring::npos) continue;
        
        size_t quote2 = line.find(L'"', quote1 + 1);
        if (quote2 == std::wstring::npos) continue;
        
        size_t tab1 = line.find(L'\t', quote2);
        if (tab1 == std::wstring::npos) continue;
        
        size_t quote3 = line.find(L'"', tab1);
        if (quote3 == std::wstring::npos) continue;
        
        size_t quote4 = line.find(L'"', quote3 + 1);
        if (quote4 == std::wstring::npos) continue;
        
        std::wstring folderPath = line.substr(quote3 + 1, quote4 - quote3 - 1);
        // Replace escaped backslashes
        size_t pos = 0;
        while ((pos = folderPath.find(L"\\\\", pos)) != std::wstring::npos) {
            folderPath.replace(pos, 2, L"\\");
            pos += 1;
        }
        
        if (!folderPath.empty() && fs::exists(folderPath)) {
            folders.push_back(folderPath);
        }
    }
    
    return folders;
}

// Parse a single appmanifest_*.acf file
static IPC::AppEntry ParseAppManifest(const std::wstring& manifestPath, const std::wstring& steamPath) {
    IPC::AppEntry entry = {};
    entry.locked = false;
    entry.isGame = true;
    entry.lastSeen = static_cast<uint64_t>(std::time(nullptr));
    
    std::wifstream file(manifestPath);
    if (!file.is_open()) return entry;
    
    std::wstring line;
    std::wstring appId, name, installDir, exeName;
    
    while (std::getline(file, line)) {
        if (line.find(L"\t\"appid\"") != std::wstring::npos) {
            size_t start = line.find(L'"', line.find(L"\t\"appid\""));
            if (start != std::wstring::npos) {
                size_t end = line.find(L'"', start + 1);
                if (end != std::wstring::npos) {
                    appId = line.substr(start + 1, end - start - 1);
                }
            }
        }
        else if (line.find(L"\t\"name\"") != std::wstring::npos) {
            size_t start = line.find(L'"', line.find(L"\t\"name\""));
            if (start != std::wstring::npos) {
                size_t end = line.find(L'"', start + 1);
                if (end != std::wstring::npos) {
                    name = line.substr(start + 1, end - start - 1);
                }
            }
        }
        else if (line.find(L"\t\"installdir\"") != std::wstring::npos) {
            size_t start = line.find(L'"', line.find(L"\t\"installdir\""));
            if (start != std::wstring::npos) {
                size_t end = line.find(L'"', start + 1);
                if (end != std::wstring::npos) {
                    installDir = line.substr(start + 1, end - start - 1);
                }
            }
        }
        else if (line.find(L"\t\"Launcher\"") != std::wstring::npos || 
                 line.find(L"\t\"exe\"") != std::wstring::npos) {
            size_t start = line.find(L'"', line.find(L"\t\"Launcher\""));
            if (start == std::wstring::npos) {
                start = line.find(L'"', line.find(L"\t\"exe\""));
            }
            if (start != std::wstring::npos) {
                size_t end = line.find(L'"', start + 1);
                if (end != std::wstring::npos) {
                    exeName = line.substr(start + 1, end - start - 1);
                }
            }
        }
    }
    
    if (appId.empty() || installDir.empty()) {
        return entry; // Invalid manifest
    }
    
    // Construct the game directory and try to find the executable
    std::wstring gameDir = steamPath + L"\\steamapps\\common\\" + installDir;
    std::wstring exePath;
    
    if (!exeName.empty()) {
        exePath = gameDir + L"\\" + exeName;
        if (!fs::exists(exePath)) {
            // Try without the extension specified
            exePath = gameDir + L"\\" + exeName + L".exe";
        }
    }
    
    // If we still don't have an exe, look for the first .exe in the directory
    if (exePath.empty() || !fs::exists(exePath)) {
        if (fs::exists(gameDir)) {
            for (const auto& entry : fs::directory_iterator(gameDir)) {
                if (entry.path().extension() == L".exe") {
                    exePath = entry.path().wstring();
                    break;
                }
            }
        }
    }
    
    if (exePath.empty() || !fs::exists(exePath)) {
        return entry; // No valid executable found
    }
    
    entry.id = appId; // Use Steam AppID as identifier
    entry.name = name.empty() ? installDir : name;
    entry.path = exePath;
    entry.iconPath = L""; // Will be populated by icon extraction
    entry.locked = false;
    entry.isGame = true;
    entry.lastSeen = static_cast<uint64_t>(std::time(nullptr));
    
    return entry;
}

DetectionEngine::DetectionEngine() {
    // Initialize exclusions with defaults
    for (const auto& excl : DEFAULT_EXCLUSIONS) {
        m_exclusions.insert(excl);
    }
}

DetectionEngine::~DetectionEngine() {
}

bool DetectionEngine::Initialize() {
    // Initialize COM for IShellLink
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    // Get LocalAppData path
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        m_localAppData = path;
    } else {
        m_localAppData = fs::temp_directory_path().wstring();
    }

    // Create cache directories
    m_iconCacheDir = m_localAppData + L"\\Vault\\icons";
    
    try {
        fs::create_directories(m_iconCacheDir);
    } catch (...) {
        return false;
    }

    return true;
}

void DetectionEngine::SetExclusionList(const std::vector<std::wstring>& exclusions) {
    m_exclusions.clear();
    for (const auto& excl : exclusions) {
        m_exclusions.insert(excl);
    }
}

std::vector<std::wstring> DetectionEngine::GetExclusionList() const {
    return std::vector<std::wstring>(m_exclusions.begin(), m_exclusions.end());
}

bool DetectionEngine::IsExcluded(const std::wstring& path) const {
    // Get just the filename
    std::wstring filename = fs::path(path).filename().wstring();
    
    // Check direct filename match
    if (m_exclusions.count(filename) > 0) {
        return true;
    }
    
    // Check if in excluded system directories
    std::wstring parentDir = fs::path(path).parent_path().wstring();
    for (const auto& sysDir : EXCLUDED_SYSTEM_DIRS) {
        if (_wcsicmp(parentDir.c_str(), sysDir.c_str()) == 0) {
            return true;
        }
    }
    
    return false;
}

std::vector<IPC::AppEntry> DetectionEngine::ScanSteamGames() {
    std::vector<IPC::AppEntry> entries;
    
    // Find Steam installation
    HKEY hKey;
    wchar_t steamPath[MAX_PATH] = {0};
    DWORD size = sizeof(steamPath);
    
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey);
    }
    
    if (result == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"SteamPath", nullptr, nullptr, (LPBYTE)steamPath, &size);
        RegCloseKey(hKey);
    }
    
    if (wcslen(steamPath) == 0) {
        return entries; // Steam not installed
    }
    
    // Remove any trailing nulls or extra characters
    std::wstring steamRoot(steamPath);
    size_t nullPos = steamRoot.find(L'\0');
    if (nullPos != std::wstring::npos) {
        steamRoot = steamRoot.substr(0, nullPos);
    }
    
    // Parse libraryfolders.vdf
    std::wstring vdfPath = steamRoot + L"\\libraryfolders.vdf";
    std::vector<std::wstring> libraryFolders = ParseLibraryFoldersVDF(vdfPath);
    
    // Always add the main Steam folder
    if (std::find(libraryFolders.begin(), libraryFolders.end(), steamRoot) == libraryFolders.end()) {
        libraryFolders.insert(libraryFolders.begin(), steamRoot);
    }
    
    // Scan each library folder for app manifests
    for (const auto& folder : libraryFolders) {
        std::wstring manifestsDir = folder + L"\\steamapps";
        if (!fs::exists(manifestsDir)) continue;
        
        for (const auto& entry : fs::directory_iterator(manifestsDir)) {
            std::wstring filename = entry.path().filename().wstring();
            if (filename.find(L"appmanifest_") == 0 && filename.ends_with(L".acf")) {
                auto appEntry = ParseAppManifest(entry.path().wstring(), folder);
                if (!appEntry.path.empty() && !IsExcluded(appEntry.path)) {
                    entries.push_back(appEntry);
                }
            }
        }
    }
    
    return entries;
}

std::vector<IPC::AppEntry> DetectionEngine::ScanRegistryUninstall() {
    std::vector<IPC::AppEntry> entries;
    
    const wchar_t* uninstallKeys[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };
    
    for (const auto* keyPath : uninstallKeys) {
        HKEY hKey;
        LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKey);
        if (result != ERROR_SUCCESS) continue;
        
        DWORD index = 0;
        wchar_t subKeyName[256];
        DWORD subKeySize = _countof(subKeyName);
        
        while ((result = RegEnumKeyExW(hKey, index, subKeyName, &subKeySize, 
                                        nullptr, nullptr, nullptr, nullptr)) == ERROR_SUCCESS) {
            HKEY hSubKey;
            if (RegOpenKeyExW(hKey, subKeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                wchar_t displayName[512] = {0};
                wchar_t displayIcon[512] = {0};
                wchar_t installLocation[512] = {0};
                DWORD size;
                
                size = sizeof(displayName);
                RegQueryValueExW(hSubKey, L"DisplayName", nullptr, nullptr, (LPBYTE)displayName, &size);
                
                size = sizeof(displayIcon);
                RegQueryValueExW(hSubKey, L"DisplayIcon", nullptr, nullptr, (LPBYTE)displayIcon, &size);
                
                size = sizeof(installLocation);
                RegQueryValueExW(hSubKey, L"InstallLocation", nullptr, nullptr, (LPBYTE)installLocation, &size);
                
                RegCloseKey(hSubKey);
                
                if (wcslen(displayName) > 0) {
                    IPC::AppEntry entry = {};
                    entry.name = displayName;
                    
                    // Try to get executable from DisplayIcon or InstallLocation
                    std::wstring exePath;
                    if (wcslen(displayIcon) > 0) {
                        exePath = displayIcon;
                        // DisplayIcon might have ",0" suffix for icon index
                        size_t commaPos = exePath.find(L',');
                        if (commaPos != std::wstring::npos) {
                            exePath = exePath.substr(0, commaPos);
                        }
                    }
                    
                    if (exePath.empty() && wcslen(installLocation) > 0) {
                        // Look for exe in install location
                        std::wstring installDir(installLocation);
                        if (fs::exists(installDir)) {
                            for (const auto& f : fs::directory_iterator(installDir)) {
                                if (f.path().extension() == L".exe") {
                                    exePath = f.path().wstring();
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (!exePath.empty() && fs::exists(exePath) && !IsExcluded(exePath)) {
                        entry.path = exePath;
                        entry.id = ComputePathHash(exePath);
                        entry.iconPath = L"";
                        entry.locked = false;
                        entry.isGame = IsGameHeuristic(exePath);
                        entry.lastSeen = static_cast<uint64_t>(std::time(nullptr));
                        entries.push_back(entry);
                    }
                }
            }
            
            subKeySize = _countof(subKeyName);
            index++;
        }
        
        RegCloseKey(hKey);
    }
    
    return entries;
}

std::vector<IPC::AppEntry> DetectionEngine::ScanStartMenuShortcuts() {
    std::vector<IPC::AppEntry> entries;
    
    const wchar_t* startMenuPaths[] = {
        nullptr, // CSIDL_STARTMENU (current user)
        nullptr  // CSIDL_COMMON_STARTMENU (all users)
    };
    
    int csidls[] = {CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU};
    
    for (size_t i = 0; i < 2; i++) {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, csidls[i], nullptr, 0, path))) {
            std::wstring startMenuDir(path);
            
            // Recursively search for .lnk files
            try {
                for (const auto& entry : fs::recursive_directory_iterator(startMenuDir)) {
                    if (entry.path().extension() == L".lnk") {
                        std::wstring target = ResolveShortcutTarget(entry.path().wstring());
                        if (!target.empty() && fs::exists(target) && !IsExcluded(target)) {
                            IPC::AppEntry appEntry = {};
                            appEntry.path = target;
                            appEntry.id = ComputePathHash(target);
                            appEntry.name = entry.path().stem().wstring();
                            appEntry.iconPath = L"";
                            appEntry.locked = false;
                            appEntry.isGame = IsGameHeuristic(target);
                            appEntry.lastSeen = static_cast<uint64_t>(std::time(nullptr));
                            entries.push_back(appEntry);
                        }
                    }
                }
            } catch (...) {
                // Ignore directory access errors
            }
        }
    }
    
    return entries;
}

std::vector<IPC::AppEntry> DetectionEngine::ScanRunningProcesses() {
    std::vector<IPC::AppEntry> entries;
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return entries;
    }
    
    PROCESSENTRY32W pe32 = {0};
    pe32.dwSize = sizeof(pe32);
    
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            std::wstring exeName = pe32.szExeFile;
            
            // Skip if excluded by name
            if (m_exclusions.count(exeName) > 0) {
                continue;
            }
            
            // Get full path from process handle
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
            if (hProcess) {
                wchar_t path[MAX_PATH];
                DWORD size = _countof(path);
                if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                    std::wstring fullPath(path);
                    if (!IsExcluded(fullPath) && fs::exists(fullPath)) {
                        IPC::AppEntry entry = {};
                        entry.path = fullPath;
                        entry.id = ComputePathHash(fullPath);
                        entry.name = exeName;
                        entry.iconPath = L"";
                        entry.locked = false;
                        entry.isGame = IsGameHeuristic(fullPath);
                        entry.lastSeen = static_cast<uint64_t>(std::time(nullptr));
                        entries.push_back(entry);
                    }
                }
                CloseHandle(hProcess);
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
    return entries;
}

std::vector<IPC::AppEntry> DetectionEngine::ScanAll() {
    std::vector<std::vector<IPC::AppEntry>> sources;
    
    sources.push_back(ScanSteamGames());
    sources.push_back(ScanRegistryUninstall());
    sources.push_back(ScanStartMenuShortcuts());
    sources.push_back(ScanRunningProcesses());
    
    return MergeAndDeduplicate(sources);
}

std::vector<IPC::AppEntry> DetectionEngine::MergeAndDeduplicate(
    const std::vector<std::vector<IPC::AppEntry>>& sources) {
    
    std::unordered_map<std::wstring, IPC::AppEntry> uniqueApps;
    
    for (const auto& source : sources) {
        for (const auto& entry : source) {
            if (entry.path.empty()) continue;
            
            std::wstring normalizedPath = fs::weakly_canonical(entry.path).wstring();
            
            auto it = uniqueApps.find(normalizedPath);
            if (it == uniqueApps.end()) {
                uniqueApps[normalizedPath] = entry;
            } else {
                // Prefer entries with more complete information
                if (entry.name.length() > it->second.name.length()) {
                    it->second.name = entry.name;
                }
                if (!entry.iconPath.empty()) {
                    it->second.iconPath = entry.iconPath;
                }
                // Games take priority over apps
                if (entry.isGame) {
                    it->second.isGame = true;
                }
            }
        }
    }
    
    std::vector<IPC::AppEntry> result;
    result.reserve(uniqueApps.size());
    
    for (auto& pair : uniqueApps) {
        // Extract icons for all entries
        if (pair.second.iconPath.empty()) {
            pair.second.iconPath = ExtractAndCacheIcon(pair.second.path);
        }
        result.push_back(pair.second);
    }
    
    return result;
}

std::wstring DetectionEngine::ResolveShortcutTarget(const std::wstring& lnkPath) {
    CComPtr<IShellLinkW> pShellLink;
    HRESULT hr = pShellLink.CoCreateInstance(CLSID_ShellLink);
    if (FAILED(hr)) return L"";
    
    CComPtr<IPersistFile> pPersistFile;
    hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
    if (FAILED(hr)) return L"";
    
    hr = pPersistFile->Load(lnkPath.c_str(), STGM_READ);
    if (FAILED(hr)) return L"";
    
    wchar_t targetPath[MAX_PATH];
    hr = pShellLink->GetPath(targetPath, _countof(targetPath), nullptr, SLGP_UNCPRIORITY);
    if (FAILED(hr)) return L"";
    
    return targetPath;
}

std::string DetectionEngine::ComputePathHash(const std::wstring& path) {
    // Simple hash for use as ID - not cryptographically secure
    // In production, use SHA256 via BCrypt
    std::string narrowPath(path.begin(), path.end());
    
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a offset basis
    for (char c : narrowPath) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL; // FNV-1a prime
    }
    
    return std::to_string(hash);
}

std::wstring DetectionEngine::GetExecutableName(const std::wstring& path) {
    return fs::path(path).stem().wstring();
}

bool DetectionEngine::IsGameHeuristic(const std::wstring& path) {
    // Common game-related path patterns
    const wchar_t* gameIndicators[] = {
        L"steam", L"epic games", L"gog", L"origin", L"ubisoft",
        L"battle.net", L"xbox", L"microsoft games", L"games"
    };
    
    std::wstring lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    
    for (const auto* indicator : gameIndicators) {
        if (lowerPath.find(indicator) != std::wstring::npos) {
            return true;
        }
    }
    
    return false;
}

std::wstring DetectionEngine::ExtractAndCacheIcon(const std::wstring& exePath) {
    // Check cache first
    auto it = m_iconCache.find(exePath);
    if (it != m_iconCache.end() && fs::exists(it->second)) {
        return it->second;
    }
    
    // Extract icon using ExtractIconEx
    HICON hIconLarge = nullptr;
    HICON hIconSmall = nullptr;
    
    UINT count = ExtractIconExW(exePath.c_str(), 0, &hIconLarge, &hIconSmall, 1);
    
    if (count == 0 || !hIconLarge) {
        // Try SHGetFileInfo as fallback
        SHFILEINFOW sfi = {0};
        HIMAGELIST himl = (HIMAGELIST)SHGetFileInfoW(
            exePath.c_str(), 0, &sfi, sizeof(sfi),
            SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
        
        if (himl && sfi.hIcon) {
            hIconLarge = sfi.hIcon;
        } else {
            return L"";
        }
    }
    
    // Generate cache filename based on path hash
    std::string hash = ComputePathHash(exePath);
    std::wstring cachedPath = m_iconCacheDir + L"\\" + 
                              std::wstring(hash.begin(), hash.end()) + L".png";
    
    // Convert and save as PNG
    if (ConvertHICONtoPNG(hIconLarge, cachedPath)) {
        DestroyIcon(hIconLarge);
        if (hIconSmall) DestroyIcon(hIconSmall);
        
        m_iconCache[exePath] = cachedPath;
        return cachedPath;
    }
    
    DestroyIcon(hIconLarge);
    if (hIconSmall) DestroyIcon(hIconSmall);
    
    return L"";
}

// Minimal PNG writer - writes a basic 32-bit RGBA PNG
// For production, consider using libpng or WIC
bool DetectionEngine::ConvertHICONtoPNG(HICON hIcon, const std::wstring& outputPath) {
    // Get icon info
    ICONINFO iconInfo = {0};
    if (!GetIconInfo(hIcon, &iconInfo)) {
        return false;
    }
    
    BITMAP bmpInfo = {0};
    if (iconInfo.hbmColor && GetObject(iconInfo.hbmColor, sizeof(bmpInfo), &bmpInfo)) {
        int width = bmpInfo.bmWidth;
        int height = bmpInfo.bmHeight;
        
        // Create DIB section to get bitmap bits
        HDC hdc = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdc);
        
        BITMAPINFOHEADER biHeader = {0};
        biHeader.biSize = sizeof(BITMAPINFOHEADER);
        biHeader.biWidth = width;
        biHeader.biHeight = height;
        biHeader.biPlanes = 1;
        biHeader.biBitCount = 32;
        biHeader.biCompression = BI_RGB;
        
        BITMAPINFO bi = {0};
        bi.bmiHeader = biHeader;
        
        void* bits = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        
        if (hBitmap) {
            SelectObject(hdcMem, hBitmap);
            DrawIconEx(hdcMem, 0, 0, hIcon, width, height, 0, nullptr, DI_NORMAL);
            
            // Write minimal PNG file
            // Note: This is a simplified implementation
            // For production use, integrate libpng or Windows Imaging Component (WIC)
            
            DeleteObject(hBitmap);
        }
        
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdc);
    }
    
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    
    // TODO: Implement proper PNG encoding using WIC or libpng
    // For now, return false to indicate icon caching needs proper implementation
    return false;
}

} // namespace Vault
