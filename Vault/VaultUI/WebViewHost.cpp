// ============================================================================
// Vault UI - WebView2 Host Implementation
// ============================================================================
// NOTE: This file requires the WebView2 SDK to compile.
// Install via NuGet: Install-Package Microsoft.Web.WebView2
// Or download from: https://developer.microsoft.com/en-us/microsoft-edge/webview2/
// ============================================================================

#include "WebViewHost.h"
#include <iostream>
#include <sstream>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace Vault {

WebViewHost::WebViewHost()
    : m_hwnd(nullptr)
    , m_ipcClient(nullptr) {
}

WebViewHost::~WebViewHost() {
}

bool WebViewHost::Initialize(HWND parentWindow) {
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::cerr << "[WebViewHost] COM initialization failed: " << std::hex << hr << std::endl;
        return false;
    }

    // Create WebView2 environment
    // Note: In production, specify a custom user data folder
    hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) {
                    std::cerr << "[WebViewHost] Failed to create environment: " << std::hex << result << std::endl;
                    return result;
                }

                m_webviewEnvironment = env;

                // Create controller
                m_webviewEnvironment->CreateCoreWebView2Controller(m_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result)) {
                                std::cerr << "[WebViewHost] Failed to create controller: " << std::hex << result << std::endl;
                                return result;
                            }

                            m_webviewController = controller;

                            // Get the WebView2 HWND
                            m_webviewController->get_CoreWebView2(&m_webview);

                            HWND webviewHwnd = nullptr;
                            m_webviewController->get_ParentWindow(&webviewHwnd);
                            m_hwnd = webviewHwnd;

                            // Set up message handler
                            m_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        wil::unique_cotaskmem_string message;
                                        args->TryGetWebMessageAsString(&message);
                                        
                                        if (message && m_messageCallback) {
                                            std::string msg(message.get());
                                            m_messageCallback(msg);
                                        }
                                        
                                        return S_OK;
                                    }).Get(),
                                &m_messageReceivedToken);

                            // Register native bridge
                            RegisterNativeBridge();

                            return S_OK;
                        }).Get());

                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        std::cerr << "[WebViewHost] CreateCoreWebView2EnvironmentWithOptions failed: " << std::hex << hr << std::endl;
        return false;
    }

    // Process messages until environment is created
    MSG msg;
    while (!m_webviewEnvironment) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }

    return true;
}

bool WebViewHost::LoadUI(const std::wstring& htmlPath) {
    if (!m_webview) {
        return false;
    }

    // Convert file path to file:// URL
    std::wstring url = L"file:///" + htmlPath;
    
    // Replace backslashes with forward slashes
    for (wchar_t& c : url) {
        if (c == L'\\') c = L'/';
    }

    HRESULT hr = m_webview->Navigate(url.c_str());
    if (FAILED(hr)) {
        std::cerr << "[WebViewHost] Navigate failed: " << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

HWND WebViewHost::GetHWND() const {
    return m_hwnd;
}

void WebViewHost::SetWebMessageCallback(WebMessageCallback callback) {
    m_messageCallback = callback;
}

bool WebViewHost::PostMessageToJS(const std::string& message) {
    if (!m_webview) {
        return false;
    }

    // Convert to wide string
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    std::wstring wideMsg(size_needed > 0 ? size_needed : 0, 0);
    if (size_needed > 0) {
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, &wideMsg[0], size_needed);
    }

    HRESULT hr = m_webview->PostWebMessageAsString(wideMsg.c_str());
    return SUCCEEDED(hr);
}

bool WebViewHost::ExecuteScript(const std::string& script) {
    if (!m_webview) {
        return false;
    }

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, script.c_str(), -1, nullptr, 0);
    std::wstring wideScript(size_needed > 0 ? size_needed : 0, 0);
    if (size_needed > 0) {
        MultiByteToWideChar(CP_UTF8, 0, script.c_str(), -1, &wideScript[0], size_needed);
    }

    HRESULT hr = m_webview->ExecuteScript(wideScript.c_str(), nullptr);
    return SUCCEEDED(hr);
}

void WebViewHost::SetIPCClient(IPCClient* ipcClient) {
    m_ipcClient = ipcClient;
}

void WebViewHost::RegisterNativeBridge() {
    // Add host object that JavaScript can call
    // The actual methods are exposed via window.chrome.webview.postMessage
    // and responses come back via WebMessageReceived event
    
    // For more advanced scenarios, use AddHostObjectToScript to expose
    // C++ objects directly to JavaScript
}

std::string WebViewHost::JS_GetAppList() {
    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return "{\"error\":\"Not connected to service\"}";
    }

    std::ostringstream oss;
    oss << "{\"type\":" << static_cast<int>(IPC::MessageType::RequestAppList) << "}";
    
    // Send request - response comes via callback
    m_ipcClient->SendMessage(oss.str());
    
    return ""; // Response handled asynchronously
}

std::string WebViewHost::JS_ToggleLock(const std::string& appId, bool locked) {
    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return "{\"error\":\"Not connected to service\"}";
    }

    std::ostringstream oss;
    oss << "{\"type\":" << static_cast<int>(IPC::MessageType::ToggleLock)
        << ",\"payload\":{\"appId\":\"" << appId 
        << "\",\"locked\":" << (locked ? "true" : "false") << "}}";
    
    m_ipcClient->SendMessage(oss.str());
    return "";
}

std::string WebViewHost::JS_AuthAttempt(const std::string& appId, const std::string& method, const std::string& payload) {
    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return "{\"error\":\"Not connected to service\"}";
    }

    std::ostringstream oss;
    oss << "{\"type\":" << static_cast<int>(IPC::MessageType::AuthAttempt)
        << ",\"payload\":{\"appId\":\"" << appId 
        << "\",\"method\":\"" << method 
        << "\",\"payload\":\"" << payload << "\"}}";
    
    m_ipcClient->SendMessage(oss.str());
    return "";
}

std::string WebViewHost::JS_GetAuthMethods() {
    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return "{\"error\":\"Not connected to service\"}";
    }

    std::ostringstream oss;
    oss << "{\"type\":" << static_cast<int>(IPC::MessageType::GetAuthMethods) << "}";
    
    m_ipcClient->SendMessage(oss.str());
    return "";
}

std::string WebViewHost::JS_GetStats(const std::string& appId) {
    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return "{\"error\":\"Not connected to service\"}";
    }

    std::ostringstream oss;
    oss << "{\"type\":" << static_cast<int>(IPC::MessageType::RequestStats)
        << ",\"payload\":{\"appId\":\"" << appId << "\"}}";
    
    m_ipcClient->SendMessage(oss.str());
    return "";
}

std::string WebViewHost::JS_SetPIN(const std::string& pin) {
    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return "{\"error\":\"Not connected to service\"}";
    }

    std::ostringstream oss;
    oss << "{\"type\":" << static_cast<int>(IPC::MessageType::SetPIN)
        << ",\"payload\":{\"pin\":\"" << pin << "\"}}";
    
    m_ipcClient->SendMessage(oss.str());
    return "";
}

std::string WebViewHost::JS_SetPassword(const std::string& password) {
    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return "{\"error\":\"Not connected to service\"}";
    }

    std::ostringstream oss;
    oss << "{\"type\":" << static_cast<int>(IPC::MessageType::SetPassword)
        << ",\"payload\":{\"password\":\"" << password << "\"}}";
    
    m_ipcClient->SendMessage(oss.str());
    return "";
}

std::string WebViewHost::JS_GetExclusionList() {
    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return "{\"error\":\"Not connected to service\"}";
    }

    std::ostringstream oss;
    oss << "{\"type\":" << static_cast<int>(IPC::MessageType::GetExclusionList) << "}";
    
    m_ipcClient->SendMessage(oss.str());
    return "";
}

} // namespace Vault
