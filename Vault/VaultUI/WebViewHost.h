#pragma once

// ============================================================================
// Vault UI - WebView2 Host
// ============================================================================
// Creates and manages a WebView2 control to host the HTML/CSS/JS frontend.
// Exposes C++ functions to JavaScript via AddHostObjectToScript for:
// - Getting app list from service
// - Toggling lock state
// - Authentication (PIN, password, Windows Hello)
// - Getting session stats
// ============================================================================

#ifndef VAULT_WEBVIEW_HOST_H
#define VAULT_WEBVIEW_HOST_H

#include <windows.h>
#include <string>
#include <functional>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"
#include "IPCClient.h"

namespace Vault {

class WebViewHost {
public:
    WebViewHost();
    ~WebViewHost();

    // Initialize WebView2 runtime and create controller
    bool Initialize(HWND parentWindow);

    // Navigate to the UI HTML file
    bool LoadUI(const std::wstring& htmlPath);

    // Get the underlying HWND for the WebView2 control
    HWND GetHWND() const;

    // Set callback for web messages
    using WebMessageCallback = std::function<void(const std::string& message)>;
    void SetWebMessageCallback(WebMessageCallback callback);

    // Send a message to JavaScript
    bool PostMessageToJS(const std::string& message);

    // Execute JavaScript code
    bool ExecuteScript(const std::string& script);

    // Set reference to IPC client for bridging
    void SetIPCClient(IPCClient* ipcClient);

private:
    wil::com_ptr<ICoreWebView2Environment> m_webviewEnvironment;
    wil::com_ptr<ICoreWebView2Controller> m_webviewController;
    wil::com_ptr<ICoreWebView2> m_webview;
    HWND m_hwnd;
    WebMessageCallback m_messageCallback;
    IPCClient* m_ipcClient;

    // Event handlers
    EventRegistrationToken m_messageReceivedToken;

    // Native bridge methods exposed to JS
    void RegisterNativeBridge();
    
    // Bridge method implementations
    std::string JS_GetAppList();
    std::string JS_ToggleLock(const std::string& appId, bool locked);
    std::string JS_AuthAttempt(const std::string& appId, const std::string& method, const std::string& payload);
    std::string JS_GetAuthMethods();
    std::string JS_GetStats(const std::string& appId);
    std::string JS_SetPIN(const std::string& pin);
    std::string JS_SetPassword(const std::string& password);
    std::string JS_GetExclusionList();
};

} // namespace Vault

#endif // VAULT_WEBVIEW_HOST_H
