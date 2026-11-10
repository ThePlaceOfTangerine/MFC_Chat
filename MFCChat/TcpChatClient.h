#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <vector>

class TcpChatClient
{
public:
    TcpChatClient();
    ~TcpChatClient();

    bool Connect(
        const std::string& ip,
        unsigned short port
    );

    bool Login(
        const std::string& username,
        const std::string& password
    );

    bool Register(
        const std::string& username,
        const std::string& password,
        const std::string& displayName
    );

    bool SendMessageTo(
        const std::string& recipient,
        const std::string& message
    );

    bool SendGroupMessage(
        int64_t groupId,
        const std::string& message
    );

    bool CreateGroup(
        const std::string& groupName,
        const std::vector<std::string>& members
    );

    bool RequestUsers();
    bool RequestGroups();

    bool RequestGroupHistory(int64_t groupId);

    bool MarkMessageRead(
        int64_t messageId,
        bool isGroup
    );

    void Disconnect();

    bool IsConnected() const;

    void SetNotifyWindow(HWND hwnd);

    std::string GetLastErrorMessage() const;

    void SendEchoProbeAsync(const std::string& message);

    bool RequestConversations();

    bool RequestHistory(const std::string& otherUsername);

    // Gửi nguyên một gói JSON tới server (dùng cho upload/tải tệp/avatar).
    // An toàn khi gọi từ nhiều thread.
    bool SendRaw(const std::string& jsonPayload);

private:
    SOCKET m_socket;

    std::thread m_receiveThread;

    std::mutex m_sendMutex;
    mutable std::mutex m_errorMutex;
    std::mutex m_receiveMutex;

    std::atomic<bool> m_running;
    std::atomic<bool> m_connected;

    HWND m_notifyWindow;

    bool m_wsaStarted;

    std::string m_lastErrorMessage;

    bool SendAll(
        const char* data,
        int length
    );

    bool RecvExact(
        char* data,
        int length
    );

    bool SendFrame(
        const std::string& payload
    );

    bool RecvFrame(
        std::string& payload
    );

    bool SendRequestAndWait(
        const std::string& request,
        const std::string& expectedAction
    );

    void SetLastErrorMessage(
        const std::string& message
    );

    void ReceiveLoop();

    bool SendDeliveryAck(
        int64_t messageId,
        bool isGroup
    );
};