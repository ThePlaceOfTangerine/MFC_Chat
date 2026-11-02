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
#include <atomic>
#include <cstdint>

using json = nlohmann::json;

#pragma comment(lib, "Ws2_32.lib")

static const char* DATABASE_PATH =
"D:\\Work\\CMC\\MFCChat\\chat.db";

std::vector<std::shared_ptr<ClientConnection>> clients;
std::mutex clientsMutex;
std::mutex consoleMutex;

bool SendAll(SOCKET socketHandle, const char* data, int length)
{
    int sent = 0;

    while (sent < length)
    {
        int result = send(
            socketHandle,
            data + sent,
            length - sent,
            0
        );

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

bool RecvExact(SOCKET socketHandle, char* buffer, int length)
{
    int received = 0;

    while (received < length)
    {
        int result = recv(
            socketHandle,
            buffer + received,
            length - received,
            0
        );

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

bool RecvFrame(SOCKET socketHandle, std::string& message)
{
    uint32_t networkLength = 0;

    if (!RecvExact(
        socketHandle,
        reinterpret_cast<char*>(&networkLength),
        sizeof(networkLength)))
    {
        return false;
    }

    uint32_t length = ntohl(networkLength);

    if (length == 0 || length > MAX_MESSAGE_SIZE)
        return false;

    std::string buffer(length, '\0');

    if (!RecvExact(
        socketHandle,
        &buffer[0],
        static_cast<int>(length)))
    {
        return false;
    }

    message = std::move(buffer);
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


static bool IsAuthenticated(
    const std::shared_ptr<ClientConnection>& client)
{
    return client &&
        client->authenticated.load() &&
        !client->username.empty();
}

static void SendPendingMessages(
    const std::shared_ptr<ClientConnection>& client)
{
    if (!client || !IsAuthenticated(client))
        return;

    Database db;

    if (!db.Open(DATABASE_PATH))
        return;

    if (!db.Initialize())
    {
        db.Close();
        return;
    }

    std::vector<PendingMessage> directMessages;
    std::vector<PendingMessage> groupMessages;

    bool directSuccess =
        db.GetPendingMessages(client->username, directMessages);

    bool groupSuccess =
        db.GetPendingGroupMessages(client->username, groupMessages);

    db.Close();

    if (!directSuccess || !groupSuccess)
        return;

    for (const auto& item : directMessages)
    {
        json message = {
            {"action", "MESSAGE"},
            {"messageId", item.messageId},
            {"sender", item.senderUsername},
            {"recipient", item.recipientUsername},
            {"content", item.content},
            {"sentAt", item.sentAt},
            {"offline", true}
        };

        if (!SendFrame(client, message.dump()))
            return;
    }

    for (const auto& item : groupMessages)
    {
        json message = {
            {"action", "GROUP_MESSAGE"},
            {"messageId", item.messageId},
            {"groupId", item.groupId},
            {"sender", item.senderUsername},
            {"recipient", item.recipientUsername},
            {"content", item.content},
            {"sentAt", item.sentAt},
            {"offline", true}
        };

        if (!SendFrame(client, message.dump()))
            return;
    }
}

static std::shared_ptr<ClientConnection> FindOnlineUser(
    const std::string& username)
{
    std::lock_guard<std::mutex> lock(clientsMutex);

    for (const auto& client : clients)
    {
        if (client &&
            client->authenticated.load() &&
            !client->closing.load() &&
            client->username == username)
        {
            return client;
        }
    }

    return nullptr;
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
        errorMessage = "Cannot initialize database";
        return false;
    }

    bool success = db.CreateUser(
        username.c_str(),
        password.c_str(),
        displayName.c_str()
    );

    db.Close();

    if (!success)
    {
        errorMessage =
            "Registration failed; username may already exist";
        return false;
    }

    errorMessage = "Registration successful";
    return true;
}

static bool TryLogin(
    const std::shared_ptr<ClientConnection>& client,
    const std::string& username,
    const std::string& password,
    std::string& errorMessage)
{
    if (!client || !IsValidUsername(username))
    {
        errorMessage = "Invalid username";
        return false;
    }

    if (!IsValidPassword(password))
    {
        errorMessage = "Invalid password";
        return false;
    }

    if (client->authenticated.load())
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
        errorMessage = "Cannot initialize database";
        return false;
    }

    int userId = 0;
    std::string displayName;

    bool valid = db.AuthenticateUser(
        username.c_str(),
        password.c_str(),
        userId,
        displayName
    );

    db.Close();

    if (!valid)
    {
        errorMessage = "Invalid username or password";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex);

        for (const auto& other : clients)
        {
            if (other != client &&
                other &&
                other->authenticated.load() &&
                other->username == username)
            {
                errorMessage = "User already connected";
                return false;
            }
        }

        client->username = username;
        client->authenticated = true;
    }

    errorMessage = "Login successful";
    return true;
}

static void HandleSendMessage(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    if (!IsAuthenticated(client))
    {
        SendFrame(client, json{
            {"action", "SEND_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Login required"}
            }.dump());

        return;
    }

    if (!request.contains("recipient") ||
        !request["recipient"].is_string() ||
        !request.contains("content") ||
        !request["content"].is_string())
    {
        SendFrame(client, json{
            {"action", "SEND_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Missing recipient or content"}
            }.dump());

        return;
    }

    std::string recipient =
        request["recipient"].get<std::string>();

    std::string content =
        request["content"].get<std::string>();

    if (!IsValidUsername(recipient) ||
        content.empty() ||
        content.size() > MAX_MESSAGE_SIZE)
    {
        SendFrame(client, json{
            {"action", "SEND_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Invalid message"}
            }.dump());

        return;
    }

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
    {
        SendFrame(client, json{
            {"action", "SEND_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Database unavailable"}
            }.dump());

        return;
    }

    int64_t messageId = 0;

    bool saved = db.SaveDirectMessage(
        client->username,
        recipient,
        content,
        messageId
    );

    db.Close();

    if (!saved)
    {
        SendFrame(client, json{
            {"action", "SEND_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Cannot save message"}
            }.dump());

        return;
    }

    // Báo cho người gửi biết server đã lưu tin nhắn.
    SendFrame(client, json{
        {"action", "SEND_MESSAGE_RESULT"},
        {"success", true},
        {"messageId", messageId},
        {"message", "Message saved"}
        }.dump());

    // Nếu người nhận đang online, gửi tin nhắn đến client đó.
    // Server chưa đánh dấu delivered cho đến khi nhận ACK_DELIVERED.
    std::shared_ptr<ClientConnection> target =
        FindOnlineUser(recipient);

    if (target)
    {
        json incoming = {
            {"action", "MESSAGE"},
            {"messageId", messageId},
            {"sender", client->username},
            {"recipient", recipient},
            {"content", content},
            {"offline", false}
        };

        if (!SendFrame(target, incoming.dump()))
        {
            // Tin nhắn vẫn nằm trong DB và sẽ được gửi lại khi đăng nhập.
            std::lock_guard<std::mutex> lock(consoleMutex);
            std::cout << "Delivery pending for " << recipient << "\n";
        }
    }
}

static void HandleAckDelivered(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    if (!IsAuthenticated(client) ||
        !request.contains("messageId") ||
        !request["messageId"].is_number_integer())
    {
        return;
    }

    int64_t messageId = request["messageId"].get<int64_t>();

    bool isGroup = request.value("isGroup", false);

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
        return;

    bool success = isGroup
        ? db.MarkGroupMessageDelivered(messageId, client->username)
        : db.MarkMessageDelivered(messageId, client->username);

    db.Close();

    if (success)
    {
        SendFrame(client, json{
            {"action", "DELIVERED_ACK"},
            {"messageId", messageId},
            {"isGroup", isGroup},
            {"success", true}
            }.dump());
    }
}

static void HandleAckRead(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    if (!IsAuthenticated(client) ||
        !request.contains("messageId") ||
        !request["messageId"].is_number_integer())
    {
        return;
    }

    int64_t messageId = request["messageId"].get<int64_t>();
    bool isGroup = request.value("isGroup", false);

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
        return;

    bool success = isGroup
        ? db.MarkGroupMessageRead(messageId, client->username)
        : db.MarkMessageRead(messageId, client->username);

    db.Close();

    if (success)
    {
        SendFrame(client, json{
            {"action", "READ_ACK"},
            {"messageId", messageId},
            {"isGroup", isGroup},
            {"success", true}
            }.dump());
    }
}

static void HandleCreateGroup(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    if (!IsAuthenticated(client) ||
        !request.contains("groupName") ||
        !request["groupName"].is_string() ||
        !request.contains("members") ||
        !request["members"].is_array())
    {
        SendFrame(client, json{
            {"action", "CREATE_GROUP_RESULT"},
            {"success", false},
            {"message", "Invalid group request"}
            }.dump());

        return;
    }

    std::string groupName = request["groupName"].get<std::string>();

    std::vector<std::string> members;

    for (const auto& item : request["members"])
    {
        if (item.is_string())
            members.push_back(item.get<std::string>());
    }

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
    {
        SendFrame(client, json{
            {"action", "CREATE_GROUP_RESULT"},
            {"success", false},
            {"message", "Database unavailable"}
            }.dump());

        return;
    }

    int64_t groupId = 0;

    bool success = db.CreateGroup(
        groupName,
        client->username,
        members,
        groupId
    );

    db.Close();

    SendFrame(client, json{
        {"action", "CREATE_GROUP_RESULT"},
        {"success", success},
        {"groupId", groupId},
        {"groupName", groupName},
        {"message", success ? "Group created" : "Cannot create group"}
        }.dump());
}

static void HandleSendGroupMessage(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    if (!IsAuthenticated(client) ||
        !request.contains("groupId") ||
        !request["groupId"].is_number_integer() ||
        !request.contains("content") ||
        !request["content"].is_string())
    {
        SendFrame(client, json{
            {"action", "SEND_GROUP_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Invalid group message"}
            }.dump());

        return;
    }

    int64_t groupId = request["groupId"].get<int64_t>();
    std::string content = request["content"].get<std::string>();

    if (groupId <= 0 ||
        content.empty() ||
        content.size() > MAX_MESSAGE_SIZE)
    {
        SendFrame(client, json{
            {"action", "SEND_GROUP_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Invalid group message"}
            }.dump());

        return;
    }

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
    {
        SendFrame(client, json{
            {"action", "SEND_GROUP_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Database unavailable"}
            }.dump());

        return;
    }

    if (!db.IsGroupMember(groupId, client->username))
    {
        db.Close();

        SendFrame(client, json{
            {"action", "SEND_GROUP_MESSAGE_RESULT"},
            {"success", false},
            {"message", "You are not a member of this group"}
            }.dump());

        return;
    }

    int64_t messageId = 0;

    bool saved = db.SaveGroupMessage(
        groupId,
        client->username,
        content,
        messageId
    );

    std::vector<std::string> members;

    if (saved)
        db.GetGroupMembers(groupId, members);

    db.Close();

    if (!saved)
    {
        SendFrame(client, json{
            {"action", "SEND_GROUP_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Cannot save group message"}
            }.dump());

        return;
    }

    SendFrame(client, json{
        {"action", "SEND_GROUP_MESSAGE_RESULT"},
        {"success", true},
        {"messageId", messageId},
        {"groupId", groupId},
        {"message", "Group message saved"}
        }.dump());

    for (const std::string& member : members)
    {
        if (member == client->username)
            continue;

        std::shared_ptr<ClientConnection> target = FindOnlineUser(member);

        if (!target)
            continue;

        json incoming = {
            {"action", "GROUP_MESSAGE"},
            {"messageId", messageId},
            {"groupId", groupId},
            {"sender", client->username},
            {"recipient", member},
            {"content", content},
            {"offline", false}
        };

        SendFrame(target, incoming.dump());
    }
}

static void HandleGetUsers(
    const std::shared_ptr<ClientConnection>& client)
{
    if (!IsAuthenticated(client))
        return;

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
        return;

    std::vector<std::string> users;
    bool success = db.GetAllUsers(client->username.c_str(), users);

    db.Close();

    json response = {
        {"action", "USERS_RESULT"},
        {"success", success},
        {"users", users}
    };

    SendFrame(client, response.dump());
}

static void HandleGetGroups(
    const std::shared_ptr<ClientConnection>& client)
{
    if (!IsAuthenticated(client))
        return;

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
        return;

    std::vector<GroupSummary> groups;
    bool success = db.GetUserGroups(client->username, groups);

    db.Close();

    json items = json::array();

    for (const auto& group : groups)
    {
        items.push_back({
            {"groupId", group.groupId},
            {"groupName", group.groupName},
            {"lastMessage", group.lastMessage},
            {"lastSentAt", group.lastSentAt}
            });
    }

    SendFrame(client, json{
        {"action", "GROUPS_RESULT"},
        {"success", success},
        {"groups", items}
        }.dump());
}

static void HandleGetGroupHistory(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    if (!IsAuthenticated(client) ||
        !request.contains("groupId") ||
        !request["groupId"].is_number_integer())
    {
        return;
    }

    int64_t groupId = request["groupId"].get<int64_t>();

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
        return;

    std::vector<MessageRecord> messages;

    bool success = db.LoadGroupMessages(
        groupId,
        client->username,
        messages
    );

    db.Close();

    json items = json::array();

    for (const auto& message : messages)
    {
        items.push_back({
            {"messageId", message.messageId},
            {"sender", message.senderUsername},
            {"content", message.content},
            {"sentAt", message.sentAt}
            });
    }

    SendFrame(client, json{
        {"action", "GROUP_HISTORY_RESULT"},
        {"success", success},
        {"groupId", groupId},
        {"messages", items}
        }.dump());
}

static void HandleGetConversations(
    const std::shared_ptr<ClientConnection>& client)
{
    if (!IsAuthenticated(client))
        return;

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
    {
        SendFrame(client, json{
            {"action", "CONVERSATIONS_RESULT"},
            {"success", false},
            {"message", "Database unavailable"},
            {"conversations", json::array()}
            }.dump());

        return;
    }

    std::vector<ConversationSummary> conversations;

    bool success = db.GetConversationSummaries(
        client->username.c_str(),
        conversations
    );

    db.Close();

    json items = json::array();

    for (const auto& item : conversations)
    {
        items.push_back({
            {"username", item.username},
            {"lastMessage", item.lastMessage},
            {"lastSentAt", item.lastSentAt}
            });
    }

    SendFrame(client, json{
        {"action", "CONVERSATIONS_RESULT"},
        {"success", success},
        {"conversations", items}
        }.dump());
}

static void HandleGetHistory(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    if (!IsAuthenticated(client) ||
        !request.contains("otherUsername") ||
        !request["otherUsername"].is_string())
    {
        return;
    }

    std::string otherUsername =
        request["otherUsername"].get<std::string>();

    if (!IsValidUsername(otherUsername))
        return;

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
    {
        SendFrame(client, json{
            {"action", "HISTORY_RESULT"},
            {"success", false},
            {"otherUsername", otherUsername},
            {"messages", json::array()}
            }.dump());

        return;
    }

    std::vector<MessageRecord> messages;

    bool success = db.LoadMessages(
        client->username.c_str(),
        otherUsername.c_str(),
        messages
    );

    db.Close();

    json items = json::array();

    for (const auto& item : messages)
    {
        items.push_back({
            {"sender", item.senderUsername},
            {"content", item.content},
            {"sentAt", item.sentAt}
            });
    }

    SendFrame(client, json{
        {"action", "HISTORY_RESULT"},
        {"success", success},
        {"otherUsername", otherUsername},
        {"messages", items}
        }.dump());
}

void HandleClient(const std::shared_ptr<ClientConnection>& client)
{
    if (!client)
        return;

    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "Client connected.\n";
    }

    std::string payload;

    while (!client->closing.load() &&
        RecvFrame(client->socket, payload))
    {
        json request;

        try
        {
            request = json::parse(payload);
        }
        catch (const json::exception&)
        {
            SendFrame(client, json{
                {"action", "ERROR"},
                {"message", "Invalid JSON"}
                }.dump());

            continue;
        }

        if (!request.is_object() ||
            !request.contains("action") ||
            !request["action"].is_string())
        {
            SendFrame(client, json{
                {"action", "ERROR"},
                {"message", "Invalid request"}
                }.dump());

            continue;
        }

        std::string action = request["action"].get<std::string>();

        if (action == "REGISTER")
        {
            std::string username =
                request.value("username", std::string());

            std::string password =
                request.value("password", std::string());

            std::string displayName =
                request.value("displayName", std::string());

            std::string message;

            bool success = TryRegister(
                username,
                password,
                displayName,
                message
            );

            SendFrame(client, json{
                {"action", "REGISTER_RESULT"},
                {"success", success},
                {"message", message}
                }.dump());

            continue;
        }

        if (action == "LOGIN")
        {
            std::string username =
                request.value("username", std::string());

            std::string password =
                request.value("password", std::string());

            std::string message;

            bool success = TryLogin(
                client,
                username,
                password,
                message
            );

            SendFrame(client, json{
                {"action", "LOGIN_RESULT"},
                {"success", success},
                {"message", message},
                {"username", success ? username : std::string()}
                }.dump());

            if (success)
            {
                {
                    std::lock_guard<std::mutex> lock(consoleMutex);
                    std::cout << username << " logged in.\n";
                }

                SendPendingMessages(client);
            }

            continue;
        }

        if (action == "SEND_MESSAGE")
        {
            HandleSendMessage(client, request);
            continue;
        }

        if (action == "ACK_DELIVERED")
        {
            HandleAckDelivered(client, request);
            continue;
        }

        if (action == "ACK_READ")
        {
            HandleAckRead(client, request);
            continue;
        }

        if (action == "CREATE_GROUP")
        {
            HandleCreateGroup(client, request);
            continue;
        }

        if (action == "SEND_GROUP_MESSAGE")
        {
            HandleSendGroupMessage(client, request);
            continue;
        }

        if (action == "GET_CONVERSATIONS")
        {
            HandleGetConversations(client);
            continue;
        }

        if (action == "GET_HISTORY")
        {
            HandleGetHistory(client, request);
            continue;
        }

        if (action == "GET_USERS")
        {
            HandleGetUsers(client);
            continue;
        }

        if (action == "GET_GROUPS")
        {
            HandleGetGroups(client);
            continue;
        }

        if (action == "GET_GROUP_HISTORY")
        {
            HandleGetGroupHistory(client, request);
            continue;
        }

        SendFrame(client, json{
            {"action", "ERROR"},
            {"message", "Unknown action"}
            }.dump());
    }

    client->closing = true;
    client->authenticated = false;

    {
        std::lock_guard<std::mutex> lock(clientsMutex);

        clients.erase(
            std::remove(clients.begin(), clients.end(), client),
            clients.end()
        );
    }

    if (client->socket != INVALID_SOCKET)
    {
        shutdown(client->socket, SD_BOTH);
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
    }

    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        std::cout << "Client disconnected.\n";
    }
}

int main()
{
    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    Database db;

    if (!db.Open(DATABASE_PATH))
    {
        std::cerr << "Cannot open database: "
            << DATABASE_PATH << "\n";

        WSACleanup();
        return 1;
    }

    if (!db.Initialize())
    {
        std::cerr << "Cannot initialize database.\n";

        db.Close();
        WSACleanup();
        return 1;
    }

    db.Close();

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listenSocket == INVALID_SOCKET)
    {
        std::cerr << "Cannot create socket.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(SERVER_PORT);

    if (bind(
        listenSocket,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) == SOCKET_ERROR)
    {
        std::cerr << "bind() failed: " << WSAGetLastError() << "\n";

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "listen() failed.\n";

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "MFCChat TCP server listening on port "
        << SERVER_PORT << "\n";

    while (true)
    {
        sockaddr_in clientAddress{};
        int clientAddressLength = sizeof(clientAddress);

        SOCKET clientSocket = accept(
            listenSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientAddressLength
        );

        if (clientSocket == INVALID_SOCKET)
        {
            std::cerr << "accept() failed: "
                << WSAGetLastError() << "\n";

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