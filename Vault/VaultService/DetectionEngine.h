#pragma once

// ============================================================================
// Vault Service - Detection Engine
// ============================================================================
// Detects installed games and applications via multiple sources:
// 1. Steam library folders (libraryfolders.vdf + appmanifest_*.acf)
// 2. Windows Registry Uninstall keys (HKLM + WOW6432Node)
// 3. Start Menu shortcuts (.lnk resolution via IShellLink)
// 4. Running processes (CreateToolhelp32Snapshot)
//
// Extracts icons via SHGetFileInfo/ExtractIconEx and caches as PNG.
// Deduplicates by resolved executable path.
// Applies exclusion list filtering.
// ============================================================================

#ifndef VAULT_DETECTION_ENGINE_H
#define VAULT_DETECTION_ENGINE_H

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "../Shared/Types.h"
#include "../Shared/IPCProtocol.h"

namespace Vault {

class DetectionEngine {
public:
    DetectionEngine();
    ~DetectionEngine();

    // Initialize - sets up COM for IShellLink, creates cache directories
    bool Initialize();

    // Run full detection scan
    // Returns vector of detected AppEntry objects
    std::vector<IPC::AppEntry> ScanAll();

    // Set/update the exclusion list
    void SetExclusionList(const std::vector<std::wstring>& exclusions);

    // Get current exclusion list
    std::vector<std::wstring> GetExclusionList() const;

    // Check if a path should be excluded
    bool IsExcluded(const std::wstring& path) const;

    // Extract icon for an executable and cache as PNG
    // Returns the cached PNG path on success, empty string on failure
    std::wstring ExtractAndCacheIcon(const std::wstring& exePath);

private:
    std::unordered_set<std::wstring> m_exclusions;
    std::unordered_map<std::wstring, std::wstring> m_iconCache; // exe path -> cached png path
    std::wstring m_iconCacheDir;
    std::wstring m_localAppData;

    // Detection source methods
    std::vector<IPC::AppEntry> ScanSteamGames();
    std::vector<IPC::AppEntry> ScanRegistryUninstall();
    std::vector<IPC::AppEntry> ScanStartMenuShortcuts();
    std::vector<IPC::AppEntry> ScanRunningProcesses();

    // Helper methods
    std::vector<IPC::AppEntry> MergeAndDeduplicate(
        const std::vector<std::vector<IPC::AppEntry>>& sources);
    
    std::wstring ResolveShortcutTarget(const std::wstring& lnkPath);
    std::string ComputePathHash(const std::wstring& path);
    std::wstring GetExecutableName(const std::wstring& path);
    std::wstring GetDisplayName(const std::wstring& path, const std::wstring& registryName);
    bool IsGameHeuristic(const std::wstring& path);
    
    // VDF/ACF parsing helpers for Steam
    std::vector<std::wstring> FindSteamLibraryFolders();
    std::vector<IPC::AppEntry> ParseSteamAppManifest(const std::wstring& manifestPath);

    // Icon conversion
    bool ConvertHICONtoPNG(HICON hIcon, const std::wstring& outputPath);
};

} // namespace Vault

#endif // VAULT_DETECTION_ENGINE_H
