#include "pch.h"
#include "TcpChatClient.h"
#include "ChatProtocol.h"
#include "json.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

using json = nlohmann::json;

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr uint32_t MAX_FRAME_SIZE = 1024 * 1024;
}

TcpChatClient::TcpChatClient()
    : m_socket(INVALID_SOCKET),
    m_running(false),
    m_connected(false),
    m_notifyWindow(nullptr),
    m_wsaStarted(false)
{
}

TcpChatClient::~TcpChatClient()
{
    Disconnect();
}

bool TcpChatClient::Connect(
    const std::string& ip,
    unsigned short port)
{
    Disconnect();

    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        SetLastErrorMessage("WSAStartup failed.");
        return false;
    }

    m_wsaStarted = true;

    SOCKET socketHandle = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (socketHandle == INVALID_SOCKET)
    {
        SetLastErrorMessage("Cannot create socket.");

        WSACleanup();
        m_wsaStarted = false;

        return false;
    }

    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    if (inet_pton(
        AF_INET,
        ip.c_str(),
        &serverAddress.sin_addr) != 1)
    {
        SetLastErrorMessage("Invalid server IP address.");

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
        SetLastErrorMessage("Cannot connect to server.");

        closesocket(socketHandle);
        WSACleanup();
        m_wsaStarted = false;

        return false;
    }

    m_socket = socketHandle;

    SetLastErrorMessage("");

    return true;
}

bool TcpChatClient::SendAll(
    const char* data,
    int length)
{
    int totalSent = 0;

    while (totalSent < length)
    {
        int sent = send(
            m_socket,
            data + totalSent,
            length - totalSent,
            0
        );

        if (sent == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEINTR)
                continue;

            return false;
        }

        if (sent == 0)
            return false;

        totalSent += sent;
    }

    return true;
}

bool TcpChatClient::RecvExact(
    char* data,
    int length)
{
    int totalReceived = 0;

    while (totalReceived < length)
    {
        int received = recv(
            m_socket,
            data + totalReceived,
            length - totalReceived,
            0
        );

        if (received == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEINTR)
                continue;

            return false;
        }

        if (received == 0)
            return false;

        totalReceived += received;
    }

    return true;
}

bool TcpChatClient::SendFrame(
    const std::string& payload)
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
        return false;
    }

    return SendAll(
        payload.data(),
        static_cast<int>(payload.size()));
}

bool TcpChatClient::RecvFrame(
    std::string& payload)
{
    if (m_socket == INVALID_SOCKET)
        return false;

    uint32_t networkLength = 0;

    if (!RecvExact(
        reinterpret_cast<char*>(&networkLength),
        sizeof(networkLength)))
    {
        return false;
    }

    uint32_t length = ntohl(networkLength);

    if (length == 0 || length > MAX_FRAME_SIZE)
        return false;

    std::string buffer(length, '\0');

    if (!RecvExact(&buffer[0], static_cast<int>(length)))
        return false;

    payload = std::move(buffer);

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
                    std::string("Login failed.")));

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
        SetLastErrorMessage("Cannot send empty message.");
        return false;
    }

    json request = {
        {"action", "SEND_MESSAGE"},
        {"recipient", recipient},
        {"content", message}
    };

    bool success = SendFrame(request.dump());

    if (!success)
        SetLastErrorMessage("Failed to send message.");

    return success;
}

bool TcpChatClient::SendGroupMessage(
    int64_t groupId,
    const std::string& message)
{
    if (!IsConnected() || groupId <= 0 || message.empty())
        return false;

    json request = {
        {"action", "SEND_GROUP_MESSAGE"},
        {"groupId", groupId},
        {"content", message}
    };

    return SendFrame(request.dump());
}

bool TcpChatClient::CreateGroup(
    const std::string& groupName,
    const std::vector<std::string>& members)
{
    if (!IsConnected() ||
        groupName.empty() ||
        groupName.size() > 128)
    {
        return false;
    }

    json request = {
        {"action", "CREATE_GROUP"},
        {"groupName", groupName},
        {"members", members}
    };

    return SendFrame(request.dump());
}

bool TcpChatClient::RequestUsers()
{
    if (!IsConnected())
        return false;

    return SendFrame(json{
        {"action", "GET_USERS"}
        }.dump());
}

bool TcpChatClient::RequestGroups()
{
    if (!IsConnected())
        return false;

    return SendFrame(json{
        {"action", "GET_GROUPS"}
        }.dump());
}

bool TcpChatClient::RequestGroupHistory(int64_t groupId)
{
    if (!IsConnected() || groupId <= 0)
        return false;

    return SendFrame(json{
        {"action", "GET_GROUP_HISTORY"},
        {"groupId", groupId}
        }.dump());
}

bool TcpChatClient::MarkMessageRead(
    int64_t messageId,
    bool isGroup)
{
    if (!IsConnected() || messageId <= 0)
        return false;

    return SendFrame(json{
        {"action", "ACK_READ"},
        {"messageId", messageId},
        {"isGroup", isGroup}
        }.dump());
}

bool TcpChatClient::SendDeliveryAck(
    int64_t messageId,
    bool isGroup)
{
    if (!IsConnected() || messageId <= 0)
        return false;

    return SendFrame(json{
        {"action", "ACK_DELIVERED"},
        {"messageId", messageId},
        {"isGroup", isGroup}
        }.dump());
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
            json response = json::parse(payload);

            std::string action =
                response.value("action", std::string());

            if (action == "MESSAGE" ||
                action == "GROUP_MESSAGE")
            {
                TcpMessage* incoming = new TcpMessage();

                incoming->messageId =
                    response.value("messageId", int64_t(0));

                incoming->groupId =
                    response.value("groupId", int64_t(0));

                incoming->sender =
                    response.value("sender", std::string());

                incoming->content =
                    response.value("content", std::string());

                incoming->sentAt =
                    response.value("sentAt", std::string());

                incoming->isGroup = (action == "GROUP_MESSAGE");

                incoming->offline =
                    response.value("offline", false);

                // Xác nhận đã nhận dữ liệu từ server.
                // Trạng thái này chưa có nghĩa là người dùng đã đọc.
                SendDeliveryAck(
                    incoming->messageId,
                    incoming->isGroup
                );

                if (m_notifyWindow != nullptr &&
                    IsWindow(m_notifyWindow))
                {
                    if (!PostMessage(
                        m_notifyWindow,
                        WM_APP_TCP_MESSAGE,
                        0,
                        reinterpret_cast<LPARAM>(incoming)))
                    {
                        delete incoming;
                    }
                }
                else
                {
                    delete incoming;
                }

                continue;
            }

            TcpResponse* incomingResponse = new TcpResponse();

            incomingResponse->action = action;
            incomingResponse->payload = payload;

            if (m_notifyWindow != nullptr &&
                IsWindow(m_notifyWindow))
            {
                if (!PostMessage(
                    m_notifyWindow,
                    WM_APP_TCP_RESPONSE,
                    0,
                    reinterpret_cast<LPARAM>(incomingResponse)))
                {
                    delete incomingResponse;
                }
            }
            else
            {
                delete incomingResponse;
            }
        }
        catch (const json::exception&)
        {
            SetLastErrorMessage("Invalid JSON received from server.");
        }
    }

    m_connected = false;
    m_running = false;
}

void TcpChatClient::Disconnect()
{
    m_running = false;
    m_connected = false;

    if (m_socket != INVALID_SOCKET)
    {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    if (m_receiveThread.joinable())
    {
        if (m_receiveThread.get_id() != std::this_thread::get_id())
            m_receiveThread.join();
    }

    if (m_wsaStarted)
    {
        WSACleanup();
        m_wsaStarted = false;
    }
}

bool TcpChatClient::IsConnected() const
{
    return m_connected.load();
}

void TcpChatClient::SetNotifyWindow(HWND hwnd)
{
    m_notifyWindow = hwnd;
}

void TcpChatClient::SetLastErrorMessage(
    const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_lastErrorMessage = message;
}

std::string TcpChatClient::GetLastErrorMessage() const
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastErrorMessage;
}

void TcpChatClient::SendEchoProbeAsync(
    const std::string& message)
{
    UNREFERENCED_PARAMETER(message);
}

bool TcpChatClient::RequestConversations()
{
    if (!IsConnected())
        return false;

    return SendFrame(json{
        {"action", "GET_CONVERSATIONS"}
        }.dump());
}

bool TcpChatClient::RequestHistory(
    const std::string& otherUsername)
{
    if (!IsConnected() || otherUsername.empty())
        return false;

    return SendFrame(json{
        {"action", "GET_HISTORY"},
        {"otherUsername", otherUsername}
        }.dump());
}