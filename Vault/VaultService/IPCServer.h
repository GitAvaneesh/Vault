#pragma once

// ============================================================================
// Vault Service - IPC Server (Named Pipe)
// ============================================================================
// Implements the server side of the VaultIPC named pipe.
// Handles messages from the UI client and dispatches to appropriate handlers.
// Thread-safe, supports multiple concurrent connections via connection threads.
// ============================================================================

#ifndef VAULT_IPC_SERVER_H
#define VAULT_IPC_SERVER_H

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include "../Shared/Types.h"
#include "../Shared/IPCProtocol.h"

namespace Vault {

class IPCServer {
public:
    IPCServer();
    ~IPCServer();

    // Initialize - creates the named pipe
    bool Initialize();

    // Start listening for connections (blocks until Stop() is called)
    void Start();

    // Stop listening and close all connections
    void Stop();

    // Check if running
    bool IsRunning() const;

    // Send a message to connected clients
    bool SendMessage(const std::string& jsonMessage);

    // Callback types for handling incoming messages
    using MessageHandler = std::function<std::string(const std::string& jsonPayload)>;

    // Set handlers for each message type
    void SetHandler(IPC::MessageType type, MessageHandler handler);

    // Broadcast a message to all connected clients
    void Broadcast(const std::string& message);

private:
    HANDLE m_pipeHandle;
    std::thread m_serverThread;
    std::atomic<bool> m_running;
    std::wstring m_pipeName;

    std::vector<MessageHandler> m_handlers; // Indexed by MessageType enum value
    std::vector<HANDLE> m_clientHandles;

    // Server loop - accepts connections
    void ServerLoop();

    // Handle a single client connection
    void HandleClient(HANDLE hPipe);

    // Read a complete message from pipe
    bool ReadMessage(HANDLE hPipe, std::string& outMessage);

    // Write a message to pipe
    bool WriteMessage(HANDLE hPipe, const std::string& message);

    // Process an incoming message and send response
    void ProcessMessage(HANDLE hPipe, const std::string& message);

    // Helper to convert string to wide string
    static std::wstring ToWide(const std::string& str);
};

} // namespace Vault

#endif // VAULT_IPC_SERVER_H
