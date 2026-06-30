// ============================================================================
// Vault Service - Main Entry Point
// ============================================================================
// Windows Service that runs in the background, enforcing app locks.
// Handles SCM commands (start, stop, pause, continue) and manages all
// service components: DetectionEngine, ProcessWatcher, SuspendEngine,
// ConfigStore, and IPCServer.
// ============================================================================

#include <windows.h>
#include <iostream>
#include <memory>
#include "SuspendEngine.h"
#include "DetectionEngine.h"
#include "ConfigStore.h"
#include "ProcessWatcher.h"
#include "IPCServer.h"
#include "../Shared/Types.h"
#include "../Shared/IPCProtocol.h"

using namespace Vault;

// Global service state
static SERVICE_STATUS g_serviceStatus = {0};
static SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
static bool g_running = false;

// Service components
static std::unique_ptr<SuspendEngine> g_suspendEngine;
static std::unique_ptr<DetectionEngine> g_detectionEngine;
static std::unique_ptr<ConfigStore> g_configStore;
static std::unique_ptr<ProcessWatcher> g_processWatcher;
static std::unique_ptr<IPCServer> g_ipcServer;

// Service control handler
void WINAPI ServiceCtrlHandler(DWORD ctrlCode) {
    switch (ctrlCode) {
        case SERVICE_CONTROL_STOP:
            g_running = false;
            g_serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_statusHandle, &g_serviceStatus);
            break;
            
        case SERVICE_CONTROL_PAUSE:
            if (g_processWatcher) g_processWatcher->Stop();
            g_serviceStatus.dwCurrentState = SERVICE_PAUSED;
            SetServiceStatus(g_statusHandle, &g_serviceStatus);
            break;
            
        case SERVICE_CONTROL_CONTINUE:
            if (g_processWatcher) g_processWatcher->Start();
            g_serviceStatus.dwCurrentState = SERVICE_RUNNING;
            SetServiceStatus(g_statusHandle, &g_serviceStatus);
            break;
            
        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(g_statusHandle, &g_serviceStatus);
            break;
    }
}

// Main service entry point
void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
    g_statusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_statusHandle) {
        return;
    }

    // Initialize service status
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwServiceSpecificExitCode = 0;
    g_serviceStatus.dwWin32ExitCode = NO_ERROR;
    g_serviceStatus.dwCheckPoint = 0;
    g_serviceStatus.dwWaitHint = 3000;
    g_serviceStatus.dwControlsAccepted = 
        SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE;

    // Report starting
    g_serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_statusHandle, &g_serviceStatus);

    std::cout << "[VaultService] Starting..." << std::endl;

    // Initialize components
    g_configStore = std::make_unique<ConfigStore>();
    if (!g_configStore->Initialize()) {
        std::cerr << "[VaultService] Failed to initialize ConfigStore" << std::endl;
        g_serviceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_statusHandle, &g_serviceStatus);
        return;
    }

    g_suspendEngine = std::make_unique<SuspendEngine>();
    if (!g_suspendEngine->Initialize()) {
        std::cerr << "[VaultService] Failed to initialize SuspendEngine" << std::endl;
        g_serviceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_statusHandle, &g_serviceStatus);
        return;
    }

    g_detectionEngine = std::make_unique<DetectionEngine>();
    if (!g_detectionEngine->Initialize()) {
        std::cerr << "[VaultService] Failed to initialize DetectionEngine" << std::endl;
        g_serviceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_statusHandle, &g_serviceStatus);
        return;
    }

    // Set up exclusion list from config
    auto exclusions = g_configStore->GetExclusionList();
    if (exclusions.empty()) {
        // Use defaults
        exclusions = std::vector<std::wstring>(DEFAULT_EXCLUSIONS.begin(), DEFAULT_EXCLUSIONS.end());
        g_configStore->SetExclusionList(exclusions);
    }
    g_detectionEngine->SetExclusionList(exclusions);

    // Initialize process watcher
    g_processWatcher = std::make_unique<ProcessWatcher>();
    g_processWatcher->Initialize(g_suspendEngine.get(), g_configStore.get());
    
    // Set up callbacks for IPC notifications
    g_processWatcher->SetOnLockedAppLaunched(
        [](const std::string& appId, DWORD pid, const std::wstring& path) {
            if (g_ipcServer) {
                std::ostringstream oss;
                oss << "{\"type\":" << static_cast<int>(IPC::MessageType::LockedAppLaunched)
                    << ",\"payload\":{\"appId\":\"" << appId 
                    << "\",\"pid\":" << pid 
                    << ",\"path\":\"" << path.c_str() << "\"}}";
                g_ipcServer->Broadcast(oss.str());
            }
        });

    g_processWatcher->SetOnProcessTerminated(
        [](const std::string& appId, DWORD pid) {
            if (g_ipcServer) {
                std::ostringstream oss;
                oss << "{\"type\":" << static_cast<int>(IPC::MessageType::ProcessTerminated)
                    << ",\"payload\":{\"appId\":\"" << appId << "\",\"pid\":" << pid << "}}";
                g_ipcServer->Broadcast(oss.str());
            }
        });

    // Initialize IPC server and set up message handlers
    g_ipcServer = std::make_unique<IPCServer>();
    if (!g_ipcServer->Initialize()) {
        std::cerr << "[VaultService] Failed to initialize IPCServer" << std::endl;
        g_serviceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_statusHandle, &g_serviceStatus);
        return;
    }

    // Set up IPC message handlers
    g_ipcServer->SetHandler(IPC::MessageType::RequestAppList,
        [](const std::string& payload) -> std::string {
            if (!g_detectionEngine) return "{\"error\":\"Not initialized\"}";
            
            auto apps = g_detectionEngine->ScanAll();
            
            // Apply lock states
            for (auto& app : apps) {
                app.locked = g_configStore->IsAppLocked(app.id);
            }
            
            // Build JSON response
            std::ostringstream oss;
            oss << "{\"type\":" << static_cast<int>(IPC::MessageType::AppList)
                << ",\"payload\":{\"apps\":[";
            
            bool first = true;
            for (const auto& app : apps) {
                if (!first) oss << ",";
                first = false;
                
                oss << "{\"id\":\"" << app.id << "\""
                    << ",\"name\":\"" << app.name.c_str() << "\""
                    << ",\"path\":\"" << app.path.c_str() << "\""
                    << ",\"iconPath\":\"" << app.iconPath.c_str() << "\""
                    << ",\"locked\":" << (app.locked ? "true" : "false")
                    << ",\"isGame\":" << (app.isGame ? "true" : "false")
                    << ",\"lastSeen\":" << app.lastSeen << "}";
            }
            
            oss << "]}}";
            return oss.str();
        });

    g_ipcServer->SetHandler(IPC::MessageType::ToggleLock,
        [](const std::string& payload) -> std::string {
            // Parse {"appId": "...", "locked": true/false}
            size_t idStart = payload.find("\"appId\"");
            if (idStart == std::string::npos) return "{\"error\":\"Invalid payload\"}";
            
            size_t quote1 = payload.find('"', idStart + 7);
            size_t quote2 = payload.find('"', quote1 + 1);
            if (quote1 == std::string::npos || quote2 == std::string::npos) {
                return "{\"error\":\"Invalid appId\"}";
            }
            
            std::string appId = payload.substr(quote1 + 1, quote2 - quote1 - 1);
            bool locked = payload.find("\"locked\":true") != std::string::npos ||
                         payload.find("\"locked\": true") != std::string::npos;
            
            g_configStore->SetAppLocked(appId, locked);
            
            std::ostringstream oss;
            oss << "{\"type\":" << static_cast<int>(IPC::MessageType::AuthResult)
                << ",\"payload\":{\"success\":true,\"appId\":\"" << appId << "\"}}";
            return oss.str();
        });

    g_ipcServer->SetHandler(IPC::MessageType::AuthAttempt,
        [](const std::string& payload) -> std::string {
            // Parse auth attempt and verify credentials
            // For PIN: {"appId":"...","method":"pin","payload":"1234"}
            // For Password: {"appId":"...","method":"password","payload":"secret"}
            // For FaceID/TouchID: would need WinRT integration
            
            size_t methodStart = payload.find("\"method\"");
            size_t payloadStart = payload.find("\"payload\"");
            size_t appIdStart = payload.find("\"appId\"");
            
            if (methodStart == std::string::npos || payloadStart == std::string::npos) {
                return "{\"type\":102,\"payload\":{\"success\":false,\"reason\":\"Invalid request\"}}";
            }
            
            // Extract method
            size_t mQuote1 = payload.find('"', methodStart + 8);
            size_t mQuote2 = payload.find('"', mQuote1 + 1);
            std::string method = payload.substr(mQuote1 + 1, mQuote2 - mQuote1 - 1);
            
            // Extract credential payload
            size_t pQuote1 = payload.find('"', payloadStart + 9);
            size_t pQuote2 = payload.find('"', pQuote1 + 1);
            std::wstring credential;
            if (pQuote1 != std::string::npos && pQuote2 != std::string::npos) {
                std::string credStr = payload.substr(pQuote1 + 1, pQuote2 - pQuote1 - 1);
                credential = std::wstring(credStr.begin(), credStr.end());
            }
            
            bool success = false;
            std::string reason = "Authentication failed";
            
            if (method == "pin") {
                if (g_configStore->VerifyPIN(credential)) {
                    success = true;
                    reason = "PIN verified";
                } else {
                    reason = "Invalid PIN";
                }
            } else if (method == "password") {
                if (g_configStore->VerifyPassword(credential)) {
                    success = true;
                    reason = "Password verified";
                } else {
                    reason = "Invalid password";
                }
            } else if (method == "faceId" || method == "touchId") {
                // These require WinRT UserConsentVerifier integration
                // For now, report as unavailable
                reason = "Windows Hello not integrated in this build";
            }
            
            // Log result
            std::string appId;
            if (appIdStart != std::string::npos) {
                size_t aQuote1 = payload.find('"', appIdStart + 7);
                size_t aQuote2 = payload.find('"', aQuote1 + 1);
                if (aQuote1 != std::string::npos && aQuote2 != std::string::npos) {
                    appId = payload.substr(aQuote1 + 1, aQuote2 - aQuote1 - 1);
                    
                    if (!success) {
                        g_configStore->LogAuthFailure(appId, method, 
                                                       static_cast<uint64_t>(std::time(nullptr)));
                    } else {
                        g_configStore->LogSessionStart(appId, 
                                                        static_cast<uint64_t>(std::time(nullptr)));
                        
                        // Resume the suspended process
                        // (PID would need to be tracked - simplified here)
                    }
                }
            }
            
            std::ostringstream oss;
            oss << "{\"type\":" << static_cast<int>(IPC::MessageType::AuthResult)
                << ",\"payload\":{\"success\":" << (success ? "true" : "false")
                << ",\"reason\":\"" << reason << "\""
                << ",\"appId\":\"" << appId << "\"}}";
            return oss.str();
        });

    g_ipcServer->SetHandler(IPC::MessageType::GetAuthMethods,
        [](const std::string& payload) -> std::string {
            std::ostringstream oss;
            oss << "{\"type\":" << static_cast<int>(IPC::MessageType::AuthMethods)
                << ",\"payload\":{"
                << "\"methods\":[";
            
            bool first = true;
            if (g_configStore->HasPIN()) {
                oss << "\"pin\"";
                first = false;
            }
            if (g_configStore->HasPassword()) {
                if (!first) oss << ",";
                oss << "\"password\"";
                first = false;
            }
            // FaceID/TouchID would require WinRT check
            oss << "],"
                << "\"hasPin\":" << (g_configStore->HasPIN() ? "true" : "false")
                << ",\"hasPassword\":" << (g_configStore->HasPassword() ? "true" : "false")
                << ",\"faceIdAvailable\":false"
                << ",\"touchIdAvailable\":false"
                << "}}";
            return oss.str();
        });

    g_ipcServer->SetHandler(IPC::MessageType::SetPIN,
        [](const std::string& payload) -> std::string {
            // Parse {"pin":"1234"}
            size_t pinStart = payload.find("\"pin\"");
            if (pinStart == std::string::npos) {
                return "{\"error\":\"Invalid payload\"}";
            }
            
            size_t quote1 = payload.find('"', pinStart + 5);
            size_t quote2 = payload.find('"', quote1 + 1);
            if (quote1 == std::string::npos || quote2 == std::string::npos) {
                return "{\"error\":\"Invalid PIN\"}";
            }
            
            std::string pinStr = payload.substr(quote1 + 1, quote2 - quote1 - 1);
            std::wstring pin(pinStr.begin(), pinStr.end());
            
            bool success = g_configStore->SetPIN(pin);
            
            std::ostringstream oss;
            oss << "{\"type\":" << static_cast<int>(IPC::MessageType::SettingsChanged)
                << ",\"payload\":{\"success\":" << (success ? "true" : "false") << "}}";
            return oss.str();
        });

    g_ipcServer->SetHandler(IPC::MessageType::GetExclusionList,
        [](const std::string& payload) -> std::string {
            auto exclusions = g_configStore->GetExclusionList();
            
            std::ostringstream oss;
            oss << "{\"type\":" << static_cast<int>(IPC::MessageType::ExclusionList)
                << ",\"payload\":{\"exclusions\":[";
            
            bool first = true;
            for (const auto& excl : exclusions) {
                if (!first) oss << ",";
                first = false;
                oss << "\"" << excl.c_str() << "\"";
            }
            
            oss << "]}}";
            return oss.str();
        });

    // Report running
    g_running = true;
    g_serviceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_statusHandle, &g_serviceStatus);

    std::cout << "[VaultService] Service started successfully" << std::endl;

    // Start components
    g_processWatcher->Start();
    g_ipcServer->Start();

    // Main service loop - wait until stopped
    while (g_running && g_serviceStatus.dwCurrentState == SERVICE_RUNNING) {
        Sleep(1000);
    }

    // Cleanup
    std::cout << "[VaultService] Stopping..." << std::endl;
    
    if (g_ipcServer) g_ipcServer->Stop();
    if (g_processWatcher) g_processWatcher->Stop();

    g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_statusHandle, &g_serviceStatus);
    
    std::cout << "[VaultService] Service stopped" << std::endl;
}

// Install the service
bool InstallService() {
    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) {
        std::cerr << "Failed to open SCM: " << GetLastError() << std::endl;
        return false;
    }

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, _countof(path));

    SC_HANDLE hService = CreateServiceW(
        hSCM,
        SERVICE_NAME,
        SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        path,
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!hService) {
        std::cerr << "Failed to create service: " << GetLastError() << std::endl;
        CloseServiceHandle(hSCM);
        return false;
    }

    std::cout << "Service installed successfully" << std::endl;
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return true;
}

// Uninstall the service
bool UninstallService() {
    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        std::cerr << "Failed to open SCM: " << GetLastError() << std::endl;
        return false;
    }

    SC_HANDLE hService = OpenServiceW(hSCM, SERVICE_NAME, DELETE);
    if (!hService) {
        std::cerr << "Failed to open service: " << GetLastError() << std::endl;
        CloseServiceHandle(hSCM);
        return false;
    }

    if (!DeleteService(hService)) {
        std::cerr << "Failed to delete service: " << GetLastError() << std::endl;
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return false;
    }

    std::cout << "Service uninstalled successfully" << std::endl;
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    // Check for command-line arguments
    if (argc > 1) {
        std::wstring cmd(argv[1]);
        
        if (cmd == L"install") {
            return InstallService() ? 0 : 1;
        } else if (cmd == L"uninstall") {
            return UninstallService() ? 0 : 1;
        } else if (cmd == L"run") {
            // Run in console mode for debugging
            SERVICE_TABLE_ENTRYW dispatchTable[] = {
                {(LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain},
                {nullptr, nullptr}
            };
            return StartServiceCtrlDispatcherW(dispatchTable) ? 0 : 1;
        }
    }

    // Default: run as service
    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        {(LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain},
        {nullptr, nullptr}
    };

    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        std::cerr << "Failed to start service dispatcher: " << GetLastError() << std::endl;
        return 1;
    }

    return 0;
}
