// ============================================================================
// Vault UI - Main Entry Point
// ============================================================================
// Creates a window with WebView2 hosting the HTML/CSS/JS frontend.
// Communicates with VaultService via named pipe IPC.
// ============================================================================

#include <windows.h>
#include <iostream>
#include <commctrl.h>
#include "WebViewHost.h"
#include "IPCClient.h"
#include "../Shared/Types.h"

#pragma comment(lib, "comctl32.lib")

using namespace Vault;

static IPCClient* g_ipcClient = nullptr;
static WebViewHost* g_webviewHost = nullptr;
static HWND g_mainWindow = nullptr;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icex);

    // Register window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = UI_WINDOW_CLASS;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        std::cerr << "Failed to register window class: " << GetLastError() << std::endl;
        return 1;
    }

    // Create main window
    g_mainWindow = CreateWindowExW(
        0,
        UI_WINDOW_CLASS,
        L"Vault",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 800,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_mainWindow) {
        std::cerr << "Failed to create window: " << GetLastError() << std::endl;
        return 1;
    }

    // Initialize IPC client and connect to service
    g_ipcClient = new IPCClient();
    
    // Try to connect - if service isn't running, we'll still show UI
    // and display appropriate error message
    if (!g_ipcClient->Connect()) {
        std::cout << "[VaultUI] Could not connect to VaultService - showing offline UI" << std::endl;
        // Note: UI should show "Service not running" indicator
    }

    // Set up message callback for IPC messages
    g_ipcClient->SetMessageCallback([](const std::string& jsonMessage) {
        // Forward IPC messages to WebView2
        if (g_webviewHost) {
            g_webviewHost->PostMessageToJS(jsonMessage);
        }
    });

    // Initialize WebView2 host
    g_webviewHost = new WebViewHost();
    g_webviewHost->SetIPCClient(g_ipcClient);
    
    if (!g_webviewHost->Initialize(g_mainWindow)) {
        std::cerr << "Failed to initialize WebView2" << std::endl;
        delete g_webviewHost;
        delete g_ipcClient;
        return 1;
    }

    // Load the UI HTML
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(hInstance, exePath, _countof(exePath));
    std::wstring exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of(L"\\");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    
    std::wstring htmlPath = exeDir + L"\\ui\\vault_ui_v2.html";
    if (!g_webviewHost->LoadUI(htmlPath)) {
        std::cerr << "Failed to load UI from: " << htmlPath << std::endl;
    }

    // Show window
    ShowWindow(g_mainWindow, nCmdShow);
    UpdateWindow(g_mainWindow);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    delete g_webviewHost;
    delete g_ipcClient;

    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            // Resize WebView2 to fill window
            if (g_webviewHost && g_webviewHost->GetHWND()) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                MoveWindow(g_webviewHost->GetHWND(), 0, 0, rc.right, rc.bottom, TRUE);
            }
            return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
