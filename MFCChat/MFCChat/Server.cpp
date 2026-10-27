#include "pch.h"
#include "Server.h"
#include "Database.h"
#include "json.hpp"

#include <iostream>
#include <thread>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>

using json = nlohmann::json;

#pragma comment(lib, "Ws2_32.lib")

static const char* DATABASE_PATH =
"D:\\Work\\CMC\\MFCChat\\chat.db";

std::vector<std::shared_ptr<ClientConnection>> clients;
std::mutex clientsMutex;
std::mutex consoleMutex;

bool SendAll(SOCKET s, const char* data, int length)
{
    int sent = 0;

    while (sent < length)
    {
        int result = send(s, data + sent, length - sent, 0);

        if (result == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEINTR)
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
            if (WSAGetLastError() == WSAEINTR)
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
    if (!client ||
        client->socket == INVALID_SOCKET ||
        message.empty() ||
        message.size() > MAX_MESSAGE_SIZE)
    {
        return false;
    }

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

    return SendAll(
        client->socket,
        message.data(),
        static_cast<int>(message.size()));
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

    if (length == 0 || length > MAX_MESSAGE_SIZE)
        return false;

    std::string buffer(length, '\0');

    if (!RecvExact(s, &buffer[0], static_cast<int>(length)))
        return false;

    message = std::move(buffer);
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

static bool IsValidPassword(const std::string& password)
{
    return !password.empty() && password.size() <= 1024;
}

static bool IsValidDisplayName(const std::string& displayName)
{
    return !displayName.empty() && displayName.size() <= 128;
}

static bool IsUsernameAlreadyConnected(
    const std::string& username,
    const std::shared_ptr<ClientConnection>& current)
{
    std::lock_guard<std::mutex> lock(clientsMutex);

    for (const auto& other : clients)
    {
        if (other != current &&
            other->username == username)
        {
            return true;
        }
    }

    return false;
}

static bool TryLogin(
    const std::shared_ptr<ClientConnection>& client,
    const std::string& username,
    const std::string& password,
    std::string& errorMessage)
{
    if (!IsValidUsername(username))
    {
        errorMessage = "Invalid username";
        return false;
    }

    if (!IsValidPassword(password))
    {
        errorMessage = "Invalid password";
        return false;
    }

    if (!client->username.empty())
    {
        errorMessage = "Already logged in";
        return false;
    }

    Database db;

    if (!db.Open(DATABASE_PATH))
    {
        errorMessage = "Cannot open database";
        return false;
    }

    if (!db.Initialize())
    {
        db.Close();
        errorMessage = "Cannot initialize database";
        return false;
    }

    int userId = 0;
    std::string displayName;

    bool authenticated = db .AuthenticateUser(
        username.c_str(),
        password.c_str(),
        userId,
        displayName);

    db.Close();

    if (!authenticated)
    {
        errorMessage = "Invalid username or password";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex);

        for (const auto& other : clients)
        {
            if (other != client &&
                other->username == username)
            {
                errorMessage = "User already connected";
                return false;
            }
        }

        client->username = username;
    }

    errorMessage = "Login successful";
    return true;
}

static bool TryRegister(
    const std::string& username,
    const std::string& password,
    const std::string& displayName,
    std::string& errorMessage)
{
    if (!IsValidUsername(username))
    {
        errorMessage = "Invalid username";
        return false;
    }

    if (!IsValidPassword(password))
    {
        errorMessage = "Invalid password";
        return false;
    }

    if (!IsValidDisplayName(displayName))
    {
        errorMessage = "Invalid display name";
        return false;
    }

    Database db;

    if (!db.Open(DATABASE_PATH))
    {
        errorMessage = "Cannot open database";
        return false;
    }

    if (!db.Initialize())
    {
        db.Close();
        errorMessage = "Cannot initialize database";
        return false;
    }

    bool registered = db.CreateUser(
        username.c_str(),
        password.c_str(),
        displayName.c_str());

    db.Close();

    if (!registered)
    {
        errorMessage = "Registration failed; username may already exist";
        return false;
    }

    errorMessage = "Registration successful";
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

        const std::string action =
            request["action"].get<std::string>();

        if (action == "REGISTER")
        {
            if (!request.contains("username") ||
                !request["username"].is_string() ||
                !request.contains("password") ||
                !request["password"].is_string() ||
                !request.contains("displayName") ||
                !request["displayName"].is_string())
            {
                json response = {
                    {"action", "REGISTER_RESULT"},
                    {"success", false},
                    {"message", "Missing registration fields"}
                };

                if (!SendFrame(client, response.dump()))
                    break;

                continue;
            }

            std::string username =
                request["username"].get<std::string>();

            std::string password =
                request["password"].get<std::string>();

            std::string displayName =
                request["displayName"].get<std::string>();

            std::string message;

            bool success = TryRegister(
                username,
                password,
                displayName,
                message);

            json response = {
                {"action", "REGISTER_RESULT"},
                {"success", success},
                {"message", message}
            };

            if (!SendFrame(client, response.dump()))
                break;

            continue;
        }

        if (action == "LOGIN")
        {
            if (!request.contains("username") ||
                !request["username"].is_string() ||
                !request.contains("password") ||
                !request["password"].is_string())
            {
                json response = {
                    {"action", "LOGIN_RESULT"},
                    {"success", false},
                    {"message", "Missing username or password"}
                };

                if (!SendFrame(client, response.dump()))
                    break;

                continue;
            }

            std::string username =
                request["username"].get<std::string>();

            std::string password =
                request["password"].get<std::string>();

            std::string message;

            bool success = TryLogin(
                client,
                username,
                password,
                message);

            json response = {
                {"action", "LOGIN_RESULT"},
                {"success", success},
                {"message", message}
            };

            if (!SendFrame(client, response.dump()))
                break;

            if (!success)
                continue;

            {
                std::lock_guard<std::mutex> lock(consoleMutex);
                std::cout << username << " logged in.\n";
            }

            continue;
        }

        if (action == "SEND_MESSAGE")
        {
            if (client->username.empty())
            {
                json response = {
                    {"action", "SEND_MESSAGE_RESULT"},
                    {"success", false},
                    {"message", "Login required"}
                };

                if (!SendFrame(client, response.dump()))
                    break;

                continue;
            }

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

            if (recipient.empty() ||
                content.empty() ||
                content.size() > MAX_MESSAGE_SIZE)
            {
                continue;
            }

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
                json response = {
                    {"action", "SEND_MESSAGE_RESULT"},
                    {"success", false},
                    {"message", "Recipient is offline"}
                };

                if (!SendFrame(client, response.dump()))
                    break;

                continue;
            }

            json outgoing = {
                {"action", "MESSAGE"},
                {"sender", client->username},
                {"content", content}
            };

            if (!SendFrame(target, outgoing.dump()))
            {
                std::lock_guard<std::mutex> lock(consoleMutex);
                std::cout << "Failed to deliver message to "
                    << recipient << ".\n";
            }

            continue;
        }

        {
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cout << "Unknown action: " << action << '\n';
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex);

        clients.erase(
            std::remove(clients.begin(), clients.end(), client),
            clients.end());
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
        std::cerr << "WSAStartup failed: " << result << '\n';
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

        try
        {
            std::thread(HandleClient, client).detach();
        }
        catch (...)
        {
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.erase(
                    std::remove(clients.begin(), clients.end(), client),
                    clients.end());
            }

            closesocket(clientSocket);
        }
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}