#include "pch.h"
#include "TcpChatClient.h"
#include "ChatProtocol.h"

#include <cstdio>
#include <thread>
#include <cstring>
#include <new>
#include <string>
#include <cstdint>
#include "json.hpp"

using json = nlohmann::json;

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr uint32_t MAX_FRAME_SIZE = 1024 * 1024;
}

TcpChatClient::TcpChatClient()
    : m_socket(INVALID_SOCKET),
    m_running(false),
    m_notifyWindow(nullptr),
    m_connected(false),
    m_wsaStarted(false)
{
}

TcpChatClient::~TcpChatClient()
{
    Disconnect();
}

bool TcpChatClient::Connect(const std::string& ip, unsigned short port)
{
    Disconnect();

    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    m_wsaStarted = true;

    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (socketHandle == INVALID_SOCKET)
    {
        WSACleanup();
        m_wsaStarted = false;
        return false;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr) != 1)
    {
        closesocket(socketHandle);
        WSACleanup();
        m_wsaStarted = false;
        return false;
    }

    if (connect(
        socketHandle,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)) == SOCKET_ERROR)
    {
        closesocket(socketHandle);
        WSACleanup();
        m_wsaStarted = false;
        return false;
    }

    m_socket = socketHandle;

    return true;
}

bool TcpChatClient::Login(
    const std::string& username,
    const std::string& password)
{
    if (m_socket == INVALID_SOCKET ||
        username.empty() ||
        username.size() > 32 ||
        password.empty() ||
        password.size() > 1024)
    {
        SetLastErrorMessage("Invalid username or password.");
        return false;
    }

    json request = {
        {"action", "LOGIN"},
        {"username", username},
        {"password", password}
    };

    if (!SendFrame(request.dump()))
    {
        SetLastErrorMessage("Cannot send login request.");
        return false;
    }

    std::string payload;

    if (!RecvFrame(payload))
    {
        SetLastErrorMessage("Cannot receive login response.");
        return false;
    }

    try
    {
        json response = json::parse(payload);

        if (response.value("action", std::string()) != "LOGIN_RESULT")
        {
            SetLastErrorMessage("Invalid login response.");
            return false;
        }

        if (!response.value("success", false))
        {
            SetLastErrorMessage(
                response.value(
                    "message",
                    std::string("Invalid username or password.")));

            return false;
        }
    }
    catch (const json::exception&)
    {
        SetLastErrorMessage("Invalid JSON login response.");
        return false;
    }

    m_connected = true;
    m_running = true;

    try
    {
        m_receiveThread =
            std::thread(&TcpChatClient::ReceiveLoop, this);
    }
    catch (...)
    {
        m_running = false;
        m_connected = false;
        SetLastErrorMessage("Cannot start receive thread.");
        return false;
    }

    SetLastErrorMessage("");
    return true;
}

bool TcpChatClient::Register(
    const std::string& username,
    const std::string& password,
    const std::string& displayName)
{
    if (m_socket == INVALID_SOCKET ||
        username.empty() ||
        username.size() > 32 ||
        password.empty() ||
        password.size() > 1024 ||
        displayName.empty() ||
        displayName.size() > 128)
    {
        SetLastErrorMessage("Invalid registration information.");
        return false;
    }

    json request = {
        {"action", "REGISTER"},
        {"username", username},
        {"password", password},
        {"displayName", displayName}
    };

    if (!SendFrame(request.dump()))
    {
        SetLastErrorMessage("Cannot send registration request.");
        return false;
    }

    std::string payload;

    if (!RecvFrame(payload))
    {
        SetLastErrorMessage("Cannot receive registration response.");
        return false;
    }

    try
    {
        json response = json::parse(payload);

        if (response.value("action", std::string()) != "REGISTER_RESULT")
        {
            SetLastErrorMessage("Invalid registration response.");
            return false;
        }

        if (!response.value("success", false))
        {
            SetLastErrorMessage(
                response.value(
                    "message",
                    std::string("Registration failed.")));

            return false;
        }
    }
    catch (const json::exception&)
    {
        SetLastErrorMessage("Invalid JSON registration response.");
        return false;
    }

    SetLastErrorMessage("");
    return true;
}

bool TcpChatClient::SendMessageTo(
    const std::string& recipient,
    const std::string& message)
{
    if (!IsConnected() ||
        recipient.empty() ||
        message.empty())
    {
        return false;
    }

    json request;
    request["action"] = "SEND_MESSAGE";
    request["recipient"] = recipient;
    request["content"] = message;

    return SendFrame(request.dump());
}

bool TcpChatClient::IsConnected() const
{
    return m_connected.load();
}

void TcpChatClient::SetNotifyWindow(HWND hwnd)
{
    m_notifyWindow = hwnd;
}

bool TcpChatClient::SendAll(const char* data, int length)
{
    int totalSent = 0;

    while (totalSent < length)
    {
        int sent = send(
            m_socket,
            data + totalSent,
            length - totalSent,
            0);

        if (sent == SOCKET_ERROR || sent == 0)
            return false;

        totalSent += sent;
    }

    return true;
}

bool TcpChatClient::RecvExact(char* data, int length)
{
    int totalReceived = 0;

    while (totalReceived < length)
    {
        int received = recv(
            m_socket,
            data + totalReceived,
            length - totalReceived,
            0);

        if (received == SOCKET_ERROR || received == 0)
            return false;

        totalReceived += received;
    }

    return true;
}

bool TcpChatClient::SendFrame(const std::string& payload)
{
    if (m_socket == INVALID_SOCKET ||
        payload.empty() ||
        payload.size() > MAX_FRAME_SIZE)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_sendMutex);

    uint32_t networkLength =
        htonl(static_cast<uint32_t>(payload.size()));

    if (!SendAll(
        reinterpret_cast<const char*>(&networkLength),
        sizeof(networkLength)))
    {
        m_connected = false;
        m_running = false;
        return false;
    }

    if (!SendAll(
        payload.data(),
        static_cast<int>(payload.size())))
    {
        m_connected = false;
        m_running = false;
        return false;
    }

    return true;
}

bool TcpChatClient::RecvFrame(std::string& payload)
{
    uint32_t networkLength = 0;

    if (!RecvExact(
        reinterpret_cast<char*>(&networkLength),
        sizeof(networkLength)))
    {
        return false;
    }

    uint32_t payloadLength = ntohl(networkLength);

    if (payloadLength == 0 || payloadLength > MAX_FRAME_SIZE)
        return false;

    payload.resize(payloadLength);

    if (!RecvExact(&payload[0], static_cast<int>(payloadLength)))
    {
        payload.clear();
        return false;
    }

    return true;
}

void TcpChatClient::ReceiveLoop()
{
    while (m_running.load())
    {
        std::string payload;

        if (!RecvFrame(payload))
            break;

        try
        {
            json message = json::parse(payload);

            std::string action =
                message.value("action", std::string());

            if (action != "MESSAGE")
                continue;

            std::string sender =
                message.value("sender", std::string());

            std::string content =
                message.value("content", std::string());

            if (sender.empty() || content.empty())
                continue;

            TcpMessage* incoming =
                new (std::nothrow) TcpMessage;

            if (incoming == nullptr)
                continue;

            incoming->sender = std::move(sender);
            incoming->content = std::move(content);

            HWND hwnd = m_notifyWindow;

            if (hwnd == nullptr ||
                !::IsWindow(hwnd) ||
                !::PostMessage(
                    hwnd,
                    WM_APP_TCP_MESSAGE,
                    0,
                    reinterpret_cast<LPARAM>(incoming)))
            {
                delete incoming;
            }
        }
        catch (const json::exception&)
        {
            OutputDebugStringA(
                "[TcpChatClient] Invalid JSON received.\n");
        }
    }

    m_running = false;
    m_connected = false;
}

void TcpChatClient::Disconnect()
{
    m_connected = false;
    m_running = false;

    {
        std::lock_guard<std::mutex> lock(m_sendMutex);

        if (m_socket != INVALID_SOCKET)
            shutdown(m_socket, SD_BOTH);
    }

    if (m_receiveThread.joinable())
    {
        if (m_receiveThread.get_id() != std::this_thread::get_id())
            m_receiveThread.join();
        else
            m_receiveThread.detach();
    }

    {
        std::lock_guard<std::mutex> lock(m_sendMutex);

        if (m_socket != INVALID_SOCKET)
        {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    if (m_wsaStarted)
    {
        WSACleanup();
        m_wsaStarted = false;
    }
}

void TcpChatClient::SendEchoProbeAsync(const std::string& message)
{
    try
    {
        std::thread(
            &TcpChatClient::EchoProbeWorker,
            message
        ).detach();
    }
    catch (...)
    {
        OutputDebugStringA(
            "[EchoProbe] Failed to create worker thread.\n"
        );
    }
}

void TcpChatClient::EchoProbeWorker(std::string message)
{
    if (message.empty() || message.size() > 1024 * 1024)
    {
        OutputDebugStringA(
            "[EchoProbe] Invalid message size.\n"
        );
        return;
    }

    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        OutputDebugStringA(
            "[EchoProbe] WSAStartup failed.\n"
        );
        return;
    }

    SOCKET echoSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (echoSocket == INVALID_SOCKET)
    {
        OutputDebugStringA(
            "[EchoProbe] socket() failed.\n"
        );
        WSACleanup();
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9999);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (connect(
        echoSocket,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) == SOCKET_ERROR)
    {
        OutputDebugStringA(
            "[EchoProbe] Cannot connect to port 9999.\n"
        );
        closesocket(echoSocket);
        WSACleanup();
        return;
    }

    size_t totalSent = 0;

    while (totalSent < message.size())
    {
        int sent = send(
            echoSocket,
            message.data() + totalSent,
            static_cast<int>(message.size() - totalSent),
            0
        );

        if (sent == SOCKET_ERROR || sent == 0)
        {
            OutputDebugStringA(
                "[EchoProbe] send() failed.\n"
            );
            closesocket(echoSocket);
            WSACleanup();
            return;
        }

        totalSent += static_cast<size_t>(sent);
    }

    std::string response(message.size(), '\0');
    size_t totalReceived = 0;

    while (totalReceived < response.size())
    {
        int received = recv(
            echoSocket,
            &response[totalReceived],
            static_cast<int>(response.size() - totalReceived),
            0
        );

        if (received == SOCKET_ERROR || received == 0)
        {
            OutputDebugStringA(
                "[EchoProbe] recv() failed or connection closed.\n"
            );
            closesocket(echoSocket);
            WSACleanup();
            return;
        }

        totalReceived += static_cast<size_t>(received);
    }

    if (response == message)
    {
        OutputDebugStringA(
            "[EchoProbe] SUCCESS: response matches sent message.\n"
        );
    }
    else
    {
        OutputDebugStringA(
            "[EchoProbe] Response differs from sent message.\n"
        );
    }

    OutputDebugStringA("[EchoProbe] Response: ");
    OutputDebugStringA(response.c_str());
    OutputDebugStringA("\n");

    shutdown(echoSocket, SD_BOTH);
    closesocket(echoSocket);
    WSACleanup();
}

void TcpChatClient::SetLastErrorMessage(const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_lastErrorMessage = message;
}

std::string TcpChatClient::GetLastErrorMessage() const
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastErrorMessage;
}