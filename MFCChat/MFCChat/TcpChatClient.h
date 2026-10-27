#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>

class TcpChatClient
{
public:
    TcpChatClient();
    ~TcpChatClient();

    bool Connect(const std::string& ip, unsigned short port);

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

    void Disconnect();
    bool IsConnected() const;
    void SetNotifyWindow(HWND hwnd);

    std::string GetLastErrorMessage() const;

    void SendEchoProbeAsync(const std::string& message);

private:
    SOCKET m_socket;
    std::thread m_receiveThread;
    std::mutex m_sendMutex;
    mutable std::mutex m_errorMutex;

    std::atomic<bool> m_running;
    HWND m_notifyWindow;

    std::atomic<bool> m_connected;
    bool m_wsaStarted;
    std::string m_lastErrorMessage;

    bool SendAll(const char* data, int length);
    bool RecvExact(char* data, int length);
    bool SendFrame(const std::string& payload);
    bool RecvFrame(std::string& payload);

    void SetLastErrorMessage(const std::string& message);

    void ReceiveLoop();

    static void EchoProbeWorker(std::string message);
};