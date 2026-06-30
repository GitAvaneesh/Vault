#pragma once

// ============================================================================
// Vault UI - IPC Client (Named Pipe)
// ============================================================================
// Implements the client side of the VaultIPC named pipe.
// Connects to the VaultService and sends/receives messages.
// Thread-safe, handles reconnection if service restarts.
// ============================================================================

#ifndef VAULT_IPC_CLIENT_H
#define VAULT_IPC_CLIENT_H

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include "../Shared/Types.h"
#include "../Shared/IPCProtocol.h"

namespace Vault {

class IPCClient {
public:
    IPCClient();
    ~IPCClient();

    // Connect to the service
    bool Connect();

    // Disconnect from the service
    void Disconnect();

    // Check if connected
    bool IsConnected() const;

    // Send a message and optionally wait for response
    bool SendMessage(const std::string& jsonMessage);
    std::string SendRequest(const std::string& jsonMessage, int timeoutMs = 5000);

    // Set callback for incoming messages
    using MessageCallback = std::function<void(const std::string& jsonMessage)>;
    void SetMessageCallback(MessageCallback callback);

private:
    HANDLE m_pipeHandle;
    std::thread m_readThread;
    std::atomic<bool> m_connected;
    std::wstring m_pipeName;
    MessageCallback m_messageCallback;

    // Read loop for incoming messages
    void ReadLoop();

    // Read a complete message from pipe
    bool ReadMessage(std::string& outMessage);

    // Write a message to pipe
    bool WriteMessage(const std::string& message);

    // Auto-reconnect logic
    void TryReconnect();
};

} // namespace Vault

#endif // VAULT_IPC_CLIENT_H
