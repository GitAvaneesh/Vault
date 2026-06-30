// ============================================================================
// Vault UI - IPC Client Implementation
// ============================================================================

#include "IPCClient.h"
#include <iostream>
#include <sstream>

namespace Vault {

IPCClient::IPCClient()
    : m_pipeHandle(INVALID_HANDLE_VALUE)
    , m_connected(false)
    , m_pipeName(PIPE_NAME) {
}

IPCClient::~IPCClient() {
    Disconnect();
}

bool IPCClient::Connect() {
    if (m_connected) {
        return true;
    }

    // Try to connect to the named pipe
    m_pipeHandle = CreateFileW(
        m_pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        0, nullptr);

    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        std::cerr << "[IPCClient] Failed to connect to service: " << GetLastError() << std::endl;
        return false;
    }

    // Set pipe to message mode
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(m_pipeHandle, &mode, nullptr, nullptr)) {
        CloseHandle(m_pipeHandle);
        m_pipeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    m_connected = true;
    std::cout << "[IPCClient] Connected to VaultService" << std::endl;

    // Start read thread
    m_readThread = std::thread(&IPCClient::ReadLoop, this);

    return true;
}

void IPCClient::Disconnect() {
    m_connected = false;

    if (m_pipeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipeHandle);
        m_pipeHandle = INVALID_HANDLE_VALUE;
    }

    if (m_readThread.joinable()) {
        m_readThread.join();
    }

    std::cout << "[IPCClient] Disconnected from VaultService" << std::endl;
}

bool IPCClient::IsConnected() const {
    return m_connected && m_pipeHandle != INVALID_HANDLE_VALUE;
}

void IPCClient::SetMessageCallback(MessageCallback callback) {
    m_messageCallback = callback;
}

bool IPCClient::SendMessage(const std::string& jsonMessage) {
    if (!IsConnected()) {
        return false;
    }

    return WriteMessage(jsonMessage);
}

std::string IPCClient::SendRequest(const std::string& jsonMessage, int timeoutMs) {
    // For request/response, we'd need to track requestId and wait for matching response
    // Simplified implementation just sends and returns empty
    if (!SendMessage(jsonMessage)) {
        return "";
    }

    // In a full implementation, we would:
    // 1. Parse the requestId from our request
    // 2. Wait on a condition variable for the matching response
    // 3. Return the response or timeout
    
    return ""; // Placeholder - responses come via callback
}

void IPCClient::ReadLoop() {
    while (m_connected) {
        std::string message;
        
        if (ReadMessage(message)) {
            // Dispatch to callback
            if (m_messageCallback) {
                m_messageCallback(message);
            }
        } else {
            // Read failed - check if disconnected
            if (GetLastError() == ERROR_BROKEN_PIPE || 
                GetLastError() == ERROR_NO_DATA) {
                std::cout << "[IPCClient] Pipe broken, attempting reconnect..." << std::endl;
                m_connected = false;
                
                // Try to reconnect
                TryReconnect();
            }
        }
    }
}

void IPCClient::TryReconnect() {
    // Attempt to reconnect with exponential backoff
    int delayMs = 1000;
    const int maxDelayMs = 30000;

    while (!m_connected && delayMs <= maxDelayMs) {
        Sleep(delayMs);
        
        if (Connect()) {
            std::cout << "[IPCClient] Reconnected successfully" << std::endl;
            return;
        }
        
        delayMs *= 2;
    }

    std::cerr << "[IPCClient] Failed to reconnect after multiple attempts" << std::endl;
}

bool IPCClient::ReadMessage(std::string& outMessage) {
    // Read length prefix (4 bytes, little-endian uint32)
    uint32_t length = 0;
    DWORD bytesRead = 0;

    if (!ReadFile(m_pipeHandle, &length, sizeof(length), &bytesRead, nullptr) ||
        bytesRead != sizeof(length)) {
        return false;
    }

    // Validate length
    if (length == 0 || length > MAX_MESSAGE_SIZE) {
        return false;
    }

    // Read JSON payload
    std::vector<char> buffer(length);
    if (!ReadFile(m_pipeHandle, buffer.data(), length, &bytesRead, nullptr) ||
        bytesRead != length) {
        return false;
    }

    outMessage.assign(buffer.data(), length);
    return true;
}

bool IPCClient::WriteMessage(const std::string& message) {
    if (message.empty() || message.size() > MAX_MESSAGE_SIZE) {
        return false;
    }

    // Write length prefix
    uint32_t length = static_cast<uint32_t>(message.size());
    DWORD bytesWritten = 0;

    if (!WriteFile(m_pipeHandle, &length, sizeof(length), &bytesWritten, nullptr) ||
        bytesWritten != sizeof(length)) {
        return false;
    }

    // Write JSON payload
    if (!WriteFile(m_pipeHandle, message.c_str(), length, &bytesWritten, nullptr) ||
        bytesWritten != length) {
        return false;
    }

    FlushFileBuffers(m_pipeHandle);
    return true;
}

} // namespace Vault
