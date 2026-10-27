#include "Server.h"

#include <iostream>
#include <thread>
#include <algorithm>
#include <string>
#include "json.hpp"

using json = nlohmann::json;

#pragma comment(lib, "Ws2_32.lib")

std::vector<std::shared_ptr<ClientConnection>> clients;
std::mutex clientsMutex;
std::mutex consoleMutex;

bool SendAll(SOCKET s, const char* data, int length)
{
    int sent = 0;

    while (sent < length)
    {
        int result = send(
            s,
            data + sent,
            length - sent,
            0);

        if (result == SOCKET_ERROR)
        {
            int error = WSAGetLastError();

            if (error == WSAEINTR)
                continue;

            return false;
        }

        if (result == 0)
            return false;

        sent += result;
    }

    return true;
}

bool RecvExact(SOCKET s, char* buffer, int length)
{
    int received = 0;

    while (received < length)
    {
        int result = recv(
            s,
            buffer + received,
            length - received,
            0);

        if (result == SOCKET_ERROR)
        {
            int error = WSAGetLastError();

            if (error == WSAEINTR)
                continue;

            return false;
        }

        if (result == 0)
            return false;

        received += result;
    }

    return true;
}   

bool SendFrame(
    const std::shared_ptr<ClientConnection>& client,
    const std::string& message)
{
    if (message.size() > MAX_MESSAGE_SIZE)
        return false;

    uint32_t networkLength =
        htonl(static_cast<uint32_t>(message.size()));

    std::lock_guard<std::mutex> lock(client->sendMutex);

    if (!SendAll(
        client->socket,
        reinterpret_cast<const char*>(&networkLength),
        sizeof(networkLength)))
    {
        return false;
    }

    if (message.empty())
        return true;

    return SendAll(
        client->socket,
        message.data(),
        static_cast<int>(message.size())
    );
}

bool RecvFrame(SOCKET s, std::string& message)
{
    uint32_t networkLength = 0;

    if (!RecvExact(
        s,
        reinterpret_cast<char*>(&networkLength),
        sizeof(networkLength)))
    {
        return false;
    }

    uint32_t length = ntohl(networkLength);

    if (length > MAX_MESSAGE_SIZE)
        return false;

    std::vector<char> buffer(length);

    if (length > 0 &&
        !RecvExact(s, buffer.data(), static_cast<int>(length)))
    {
        return false;
    }

    message.assign(buffer.begin(), buffer.end());
    return true;
}

bool ReadLine(
    const std::string& data,
    size_t& position,
    std::string& result)
{
    size_t end = data.find('\n', position);

    if (end == std::string::npos)
        return false;

    result = data.substr(position, end - position);
    position = end + 1;

    return true;
}

bool IsValidUsername(const std::string& username)
{
    if (username.empty() || username.size() > 32)
        return false;

    for (unsigned char c : username)
    {
        if (c <= 32 || c == 127)
            return false;
    }

    return true;
}

void HandleClient(const std::shared_ptr<ClientConnection>& client)
{
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "Client connected.\n";
    }

    std::string payload;

    while (RecvFrame(client->socket, payload))
    {
        json request;

        try
        {
            request = json::parse(payload);
        }
        catch (const json::exception&)
        {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cout << "Invalid JSON received.\n";
            break;
        }

        if (!request.is_object() ||
            !request.contains("action") ||
            !request["action"].is_string())
        {
            break;
        }

        std::string action = request["action"].get<std::string>();

        // =========================
        // LOGIN
        // =========================
        if (action == "LOGIN")
        {
            if (!request.contains("username") ||
                !request["username"].is_string())
            {
                break;
            }

            std::string username =
                request["username"].get<std::string>();

            if (!IsValidUsername(username))
                break;

            // Không cho đăng nhập lại trên cùng kết nối.
            if (!client->username.empty())
                break;

            bool accepted = true;

            {
                std::lock_guard<std::mutex> lock(clientsMutex);

                for (const auto& other : clients)
                {
                    if (other != client &&
                        other->username == username)
                    {
                        accepted = false;
                        break;
                    }
                }

                if (accepted)
                    client->username = username;
            }

            json response;
            response["action"] = "LOGIN_RESULT";
            response["success"] = accepted;
            response["message"] = accepted
                ? "Login successful"
                : "Username already connected";

            if (!SendFrame(client, response.dump()))
                break;

            if (!accepted)
                break;

            {
                std::lock_guard<std::mutex> lock(consoleMutex);
                std::cout << username << " logged in.\n";
            }

            continue;
        }

        // =========================
        // SEND_MESSAGE
        // =========================
        if (action == "SEND_MESSAGE")
        {
            // Chỉ tài khoản đã đăng nhập mới được gửi tin.
            if (client->username.empty())
                break;

            if (!request.contains("recipient") ||
                !request["recipient"].is_string() ||
                !request.contains("content") ||
                !request["content"].is_string())
            {
                continue;
            }

            std::string recipient =
                request["recipient"].get<std::string>();

            std::string content =
                request["content"].get<std::string>();

            if (recipient.empty() || content.empty())
                continue;

            std::shared_ptr<ClientConnection> target;

            {
                std::lock_guard<std::mutex> lock(clientsMutex);

                for (const auto& other : clients)
                {
                    if (other != client &&
                        other->username == recipient)
                    {
                        target = other;
                        break;
                    }
                }
            }

            if (!target)
            {
                json response;
                response["action"] = "SEND_MESSAGE_RESULT";
                response["success"] = false;
                response["message"] = "Recipient is offline";

                if (!SendFrame(client, response.dump()))
                    break;

                continue;
            }

            json outgoing;
            outgoing["action"] = "MESSAGE";
            outgoing["sender"] = client->username;
            outgoing["content"] = content;

            if (!SendFrame(target, outgoing.dump()))
            {
                std::lock_guard<std::mutex> lock(consoleMutex);
                std::cout << "Warning: failed to send message to "
                    << recipient << ".\n";
            }

            continue;
        }

        // Không xử lý những action không được hỗ trợ.
        {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cout << "Unknown action received: "
                << action << '\n';
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex);

        clients.erase(
            std::remove(clients.begin(), clients.end(), client),
            clients.end()
        );
    }

    SOCKET socketToClose = client->socket;
    client->socket = INVALID_SOCKET;

    if (socketToClose != INVALID_SOCKET)
    {
        shutdown(socketToClose, SD_BOTH);
        closesocket(socketToClose);
    }

    {
        std::lock_guard<std::mutex> lock(consoleMutex);

        if (!client->username.empty())
            std::cout << client->username << " logged out.\n";

        std::cout << "Client disconnected.\n";
    }
}

int main()
{
    WSADATA wsaData{};

    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (result != 0)
    {
        std::cerr << "WSAStartup failed: "
            << result << '\n';
        return 1;
    }

    SOCKET listenSocket =
        socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listenSocket == INVALID_SOCKET)
    {
        std::cerr << "socket failed: "
            << WSAGetLastError() << '\n';
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(
        listenSocket,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)) == SOCKET_ERROR)
    {
        std::cerr << "bind failed: "
            << WSAGetLastError() << '\n';

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "listen failed: "
            << WSAGetLastError() << '\n';

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "TCP Chat Server started on port "
        << SERVER_PORT << '\n';
    std::cout << "Waiting for clients...\n";

    while (true)
    {
        SOCKET clientSocket =
            accept(listenSocket, nullptr, nullptr);

        if (clientSocket == INVALID_SOCKET)
        {
            std::cerr << "accept failed: "
                << WSAGetLastError() << '\n';
            continue;
        }

        auto client = std::make_shared<ClientConnection>();
        client->socket = clientSocket;

        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients.push_back(client);
        }

        std::thread(HandleClient, client).detach();
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
