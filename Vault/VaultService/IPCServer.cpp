// ============================================================================
// Vault Service - IPC Server Implementation
// ============================================================================

#include "IPCServer.h"
#include <iostream>
#include <sstream>

namespace Vault {

IPCServer::IPCServer()
    : m_pipeHandle(INVALID_HANDLE_VALUE)
    , m_running(false)
    , m_pipeName(PIPE_NAME) {
    
    // Resize handlers vector to accommodate all message types
    m_handlers.resize(256, nullptr);
}

IPCServer::~IPCServer() {
    Stop();
}

bool IPCServer::Initialize() {
    // Named pipe is created in ServerLoop when accepting connections
    return true;
}

void IPCServer::Start() {
    if (m_running) return;
    
    m_running = true;
    m_serverThread = std::thread(&IPCServer::ServerLoop, this);
}

void IPCServer::Stop() {
    if (!m_running) return;
    
    m_running = false;
    
    // Close the main pipe handle to break out of ConnectNamedPipe
    if (m_pipeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipeHandle);
        m_pipeHandle = INVALID_HANDLE_VALUE;
    }
    
    // Close all client handles
    for (HANDLE hClient : m_clientHandles) {
        if (hClient != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(hClient);
            CloseHandle(hClient);
        }
    }
    m_clientHandles.clear();
    
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

bool IPCServer::IsRunning() const {
    return m_running;
}

void IPCServer::SetHandler(IPC::MessageType type, MessageHandler handler) {
    size_t index = static_cast<size_t>(type);
    if (index < m_handlers.size()) {
        m_handlers[index] = handler;
    }
}

bool IPCServer::SendMessage(const std::string& jsonMessage) {
    // Send to all connected clients
    for (HANDLE hClient : m_clientHandles) {
        if (hClient != INVALID_HANDLE_VALUE) {
            WriteMessage(hClient, jsonMessage);
        }
    }
    return true;
}

void IPCServer::Broadcast(const std::string& message) {
    SendMessage(message);
}

std::wstring IPCServer::ToWide(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size_needed > 0 ? size_needed - 1 : 0, 0);
    if (size_needed > 0) {
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size_needed);
    }
    return result;
}

void IPCServer::ServerLoop() {
    std::cout << "[IPCServer] Starting server loop..." << std::endl;
    
    while (m_running) {
        // Create a new pipe instance for each connection
        m_pipeHandle = CreateNamedPipeW(
            m_pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096,  // Output buffer size
            4096,  // Input buffer size
            0,     // Default timeout
            nullptr);

        if (m_pipeHandle == INVALID_HANDLE_VALUE) {
            std::cerr << "[IPCServer] Failed to create named pipe: " << GetLastError() << std::endl;
            break;
        }

        // Wait for a client to connect
        BOOL connected = ConnectNamedPipe(m_pipeHandle, nullptr);
        
        if (!m_running) {
            // Stop was called during ConnectNamedPipe
            CloseHandle(m_pipeHandle);
            m_pipeHandle = INVALID_HANDLE_VALUE;
            break;
        }

        if (connected || GetLastError() == ERROR_PIPE_CONNECTED) {
            std::cout << "[IPCServer] Client connected" << std::endl;
            
            // Store client handle and start handling in a new thread
            HANDLE hClient = m_pipeHandle;
            m_clientHandles.push_back(hClient);
            
            // Create a new pipe instance for the next connection
            m_pipeHandle = INVALID_HANDLE_VALUE;
            
            // Handle client in a separate thread
            std::thread(&IPCServer::HandleClient, this, hClient).detach();
        } else {
            std::cerr << "[IPCServer] ConnectNamedPipe failed: " << GetLastError() << std::endl;
            CloseHandle(m_pipeHandle);
            m_pipeHandle = INVALID_HANDLE_VALUE;
        }
    }
    
    std::cout << "[IPCServer] Server loop stopped" << std::endl;
}

void IPCServer::HandleClient(HANDLE hPipe) {
    std::string message;
    
    while (m_running && ReadMessage(hPipe, message)) {
        ProcessMessage(hPipe, message);
        message.clear();
    }
    
    // Clean up client handle
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    
    // Remove from client list
    for (auto it = m_clientHandles.begin(); it != m_clientHandles.end(); ++it) {
        if (*it == hPipe) {
            m_clientHandles.erase(it);
            break;
        }
    }
    
    std::cout << "[IPCServer] Client disconnected" << std::endl;
}

bool IPCServer::ReadMessage(HANDLE hPipe, std::string& outMessage) {
    // Read length prefix (4 bytes, little-endian uint32)
    uint32_t length = 0;
    DWORD bytesRead = 0;
    
    if (!ReadFile(hPipe, &length, sizeof(length), &bytesRead, nullptr) ||
        bytesRead != sizeof(length)) {
        return false;
    }
    
    // Validate length
    if (length == 0 || length > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    // Read JSON payload
    std::vector<char> buffer(length);
    if (!ReadFile(hPipe, buffer.data(), length, &bytesRead, nullptr) ||
        bytesRead != length) {
        return false;
    }
    
    outMessage.assign(buffer.data(), length);
    return true;
}

bool IPCServer::WriteMessage(HANDLE hPipe, const std::string& message) {
    if (message.empty() || message.size() > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    // Write length prefix
    uint32_t length = static_cast<uint32_t>(message.size());
    DWORD bytesWritten = 0;
    
    if (!WriteFile(hPipe, &length, sizeof(length), &bytesWritten, nullptr) ||
        bytesWritten != sizeof(length)) {
        return false;
    }
    
    // Write JSON payload
    if (!WriteFile(hPipe, message.c_str(), length, &bytesWritten, nullptr) ||
        bytesWritten != length) {
        return false;
    }
    
    FlushFileBuffers(hPipe);
    return true;
}

void IPCServer::ProcessMessage(HANDLE hPipe, const std::string& message) {
    // Simple JSON parsing to extract "type" field
    // In production, use a proper JSON library like rapidjson or nlohmann/json
    
    std::string messageType;
    std::string payload;
    
    // Find "type" field
    size_t typePos = message.find("\"type\"");
    if (typePos != std::string::npos) {
        size_t colonPos = message.find(':', typePos);
        if (colonPos != std::string::npos) {
            // Extract the value (assumes numeric type)
            size_t valueStart = colonPos + 1;
            while (valueStart < message.size() && 
                   (message[valueStart] == ' ' || message[valueStart] == '\t')) {
                valueStart++;
            }
            
            size_t valueEnd = valueStart;
            while (valueEnd < message.size() && 
                   message[valueEnd] >= '0' && message[valueEnd] <= '9') {
                valueEnd++;
            }
            
            if (valueEnd > valueStart) {
                messageType = message.substr(valueStart, valueEnd - valueStart);
            }
        }
    }
    
    // Extract payload (everything after "payload":)
    size_t payloadPos = message.find("\"payload\"");
    if (payloadPos != std::string::npos) {
        size_t colonPos = message.find(':', payloadPos);
        if (colonPos != std::string::npos) {
            payload = message.substr(colonPos + 1);
            // Trim leading whitespace
            size_t start = payload.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) {
                payload = payload.substr(start);
            }
        }
    }
    
    // Dispatch to handler
    int typeValue = std::stoi(messageType);
    size_t handlerIndex = static_cast<size_t>(typeValue);
    
    std::string response;
    if (handlerIndex < m_handlers.size() && m_handlers[handlerIndex]) {
        response = m_handlers[handlerIndex](payload);
    } else {
        response = "{\"error\":\"Unknown message type\"}";
    }
    
    // Send response if any
    if (!response.empty()) {
        WriteMessage(hPipe, response);
    }
}

} // namespace Vault
