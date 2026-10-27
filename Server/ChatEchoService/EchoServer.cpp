#include <winsock2.h>
#include <ws2tcpip.h>

#include "EchoServer.h"

#include <Windows.h>
#include <ShlObj.h>

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Shell32.lib")

using json = nlohmann::json;

namespace
{
    constexpr unsigned short ECHO_PORT = 9999;
    constexpr unsigned int MAX_FRAME_SIZE = 1024 * 1024;
    constexpr size_t MAX_MESSAGE_SIZE = 64 * 1024;

    const char* DATABASE_PATH =
        "D:\\Work\\CMC\\MFCChat\\chat.db";

    struct ClientState
    {
        SOCKET socket = INVALID_SOCKET;
        std::vector<char> receiveBuffer;
        std::vector<char> sendBuffer;
        size_t sent = 0;
    };

    bool SendAll(SOCKET socket, const char* data, int length)
    {
        int sent = 0;

        while (sent < length)
        {
            int result = send(
                socket,
                data + sent,
                length - sent,
                0);

            if (result == SOCKET_ERROR || result == 0)
                return false;

            sent += result;
        }

        return true;
    }

    bool SendFrame(SOCKET socket, const std::string& payload)
    {
        if (payload.size() > MAX_FRAME_SIZE)
            return false;

        uint32_t networkLength =
            htonl(static_cast<uint32_t>(payload.size()));

        if (!SendAll(
            socket,
            reinterpret_cast<const char*>(&networkLength),
            sizeof(networkLength)))
        {
            return false;
        }

        return SendAll(
            socket,
            payload.data(),
            static_cast<int>(payload.size()));
    }

    bool ExecuteSql(sqlite3* db, const char* sql)
    {
        char* errorMessage = nullptr;

        int result = sqlite3_exec(
            db,
            sql,
            nullptr,
            nullptr,
            &errorMessage);

        if (result != SQLITE_OK)
        {
            const char* message = errorMessage != nullptr
                ? errorMessage
                : sqlite3_errmsg(db);

            OutputDebugStringA("[ChatEchoService][SQLite] ");
            OutputDebugStringA(message);
            OutputDebugStringA("\n");
        }

        if (errorMessage != nullptr)
            sqlite3_free(errorMessage);

        return result == SQLITE_OK;
    }

    bool InitializeDatabase(sqlite3* db)
    {
        const char* sql = R"SQL(
        PRAGMA foreign_keys = ON;

        CREATE INDEX IF NOT EXISTS
            idx_messages_conversation
        ON messages(conversation_id, sent_at);
    )SQL";

        return ExecuteSql(db, sql);
    }

    bool FindUserId(
        sqlite3* db,
        const std::string& username,
        sqlite3_int64& userId)
    {
        sqlite3_stmt* statement = nullptr;

        const char* sql =
            "SELECT id FROM users WHERE username = ?;";

        if (sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr) != SQLITE_OK)
        {
            return false;
        }

        sqlite3_bind_text(
            statement,
            1,
            username.c_str(),
            -1,
            SQLITE_TRANSIENT);

        int result = sqlite3_step(statement);

        bool found = result == SQLITE_ROW;

        if (found)
            userId = sqlite3_column_int64(statement, 0);

        sqlite3_finalize(statement);

        return found;
    }

    json HandleStoreMessage(
        sqlite3* db,
        const json& request)
    {
        std::string requestId;

        if (request.contains("request_id")
            && request["request_id"].is_string())
        {
            requestId = request["request_id"].get<std::string>();
        }

        auto makeResponse =
            [&](bool success,
                const std::string& error,
                sqlite3_int64 messageId = 0)
        {
            json response = {
                {"action", "STORE_MESSAGE_RESULT"},
                {"request_id", requestId},
                {"success", success}
            };

            if (success)
                response["message_id"] = messageId;
            else
                response["error"] = error;

            return response;
        };

        if (!request.contains("action")
            || !request["action"].is_string()
            || request["action"] != "STORE_MESSAGE")
        {
            return makeResponse(false, "INVALID_ACTION");
        }

        if (!request.contains("sender")
            || !request["sender"].is_string()
            || !request.contains("receiver")
            || !request["receiver"].is_string()
            || !request.contains("content")
            || !request["content"].is_string())
        {
            return makeResponse(false, "INVALID_FIELDS");
        }

        const std::string sender =
            request["sender"].get<std::string>();

        const std::string receiver =
            request["receiver"].get<std::string>();

        const std::string content =
            request["content"].get<std::string>();

        if (sender.empty() || receiver.empty())
            return makeResponse(false, "EMPTY_USERNAME");

        if (sender == receiver)
            return makeResponse(false, "SELF_MESSAGE_NOT_ALLOWED");

        if (content.empty())
            return makeResponse(false, "EMPTY_MESSAGE");

        if (content.size() > MAX_MESSAGE_SIZE)
            return makeResponse(false, "MESSAGE_TOO_LARGE");

        sqlite3_int64 senderId = 0;
        sqlite3_int64 receiverId = 0;

        if (!FindUserId(db, sender, senderId))
            return makeResponse(false, "SENDER_NOT_FOUND");

        if (!FindUserId(db, receiver, receiverId))
            return makeResponse(false, "RECEIVER_NOT_FOUND");

        sqlite3_int64 user1Id = (std::min)(senderId, receiverId);
        sqlite3_int64 user2Id = (std::max)(senderId, receiverId);

        if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr)
            != SQLITE_OK)
        {
            return makeResponse(false, "DATABASE_TRANSACTION_FAILED");
        }

        auto rollback = [&]()
        {
            sqlite3_exec(
                db,
                "ROLLBACK;",
                nullptr,
                nullptr,
                nullptr);
        };

        sqlite3_stmt* statement = nullptr;

        const char* createConversationSql = R"SQL(
        INSERT OR IGNORE INTO conversations
            (user1_id, user2_id)
        VALUES (?, ?);
    )SQL";

        if (sqlite3_prepare_v2(
            db,
            createConversationSql,
            -1,
            &statement,
            nullptr) != SQLITE_OK)
        {
            rollback();
            return makeResponse(false, "CONVERSATION_PREPARE_FAILED");
        }

        sqlite3_bind_int64(statement, 1, user1Id);
        sqlite3_bind_int64(statement, 2, user2Id);

        int result = sqlite3_step(statement);
        sqlite3_finalize(statement);
        statement = nullptr;

        if (result != SQLITE_DONE)
        {
            rollback();
            return makeResponse(false, "CONVERSATION_CREATE_FAILED");
        }

        const char* findConversationSql = R"SQL(
        SELECT id
        FROM conversations
        WHERE user1_id = ? AND user2_id = ?;
    )SQL";

        if (sqlite3_prepare_v2(
            db,
            findConversationSql,
            -1,
            &statement,
            nullptr) != SQLITE_OK)
        {
            rollback();
            return makeResponse(false, "CONVERSATION_LOOKUP_FAILED");
        }

        sqlite3_bind_int64(statement, 1, user1Id);
        sqlite3_bind_int64(statement, 2, user2Id);

        result = sqlite3_step(statement);

        sqlite3_int64 conversationId = 0;

        if (result == SQLITE_ROW)
            conversationId = sqlite3_column_int64(statement, 0);

        sqlite3_finalize(statement);
        statement = nullptr;

        if (result != SQLITE_ROW)
        {
            rollback();
            return makeResponse(false, "CONVERSATION_NOT_FOUND");
        }

        const char* insertMessageSql = R"SQL(
        INSERT INTO messages
            (conversation_id, sender_id, content, is_read)
        VALUES (?, ?, ?, 0);
    )SQL";

        if (sqlite3_prepare_v2(
            db,
            insertMessageSql,
            -1,
            &statement,
            nullptr) != SQLITE_OK)
        {
            rollback();
            return makeResponse(false, "MESSAGE_PREPARE_FAILED");
        }

        sqlite3_bind_int64(statement, 1, conversationId);
        sqlite3_bind_int64(statement, 2, senderId);

        sqlite3_bind_text(
            statement,
            3,
            content.c_str(),
            static_cast<int>(content.size()),
            SQLITE_TRANSIENT);

        result = sqlite3_step(statement);
        sqlite3_finalize(statement);

        if (result != SQLITE_DONE)
        {
            rollback();
            return makeResponse(false, "MESSAGE_INSERT_FAILED");
        }

        sqlite3_int64 messageId = sqlite3_last_insert_rowid(db);

        if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr)
            != SQLITE_OK)
        {
            rollback();
            return makeResponse(false, "DATABASE_COMMIT_FAILED");
        }

        return makeResponse(true, "", messageId);
    }

    bool ProcessFrame(
        sqlite3* db,
        SOCKET socket,
        const std::string& payload)
    {
        json response;

        try
        {
            json request = json::parse(payload);

            response = HandleStoreMessage(db, request);
        }
        catch (const std::exception&)
        {
            response = {
                {"action", "ERROR"},
                {"success", false},
                {"error", "INVALID_JSON"}
            };
        }

        return SendFrame(socket, response.dump());
    }

    bool EnsureDatabaseDirectory()
    {
        int result = SHCreateDirectoryExA(
            nullptr,
            "C:\\ProgramData\\MFCChat",
            nullptr);

        return result == ERROR_SUCCESS
            || result == ERROR_ALREADY_EXISTS
            || result == ERROR_FILE_EXISTS;
    }

    bool InitializeSocketLibrary()
    {
        WSADATA data;

        return WSAStartup(
            MAKEWORD(2, 2),
            &data) == 0;
    }

    void CloseClient(ClientState& client)
    {
        if (client.socket != INVALID_SOCKET)
        {
            closesocket(client.socket);
            client.socket = INVALID_SOCKET;
        }
    }

    void CloseAllClients(
        std::vector<ClientState>& clients)
    {
        for (auto& client : clients)
            CloseClient(client);

        clients.clear();
    }

    bool ProcessReceiveBuffer(
        sqlite3* db,
        ClientState& client)
    {
        while (client.receiveBuffer.size() >= sizeof(uint32_t))
        {
            uint32_t networkLength = 0;

            memcpy(
                &networkLength,
                client.receiveBuffer.data(),
                sizeof(networkLength));

            uint32_t payloadLength = ntohl(networkLength);

            if (payloadLength == 0
                || payloadLength > MAX_FRAME_SIZE)
            {
                return false;
            }

            size_t frameSize =
                sizeof(uint32_t) + payloadLength;

            if (client.receiveBuffer.size() < frameSize)
                break;

            std::string payload(
                client.receiveBuffer.data() + sizeof(uint32_t),
                payloadLength);

            client.receiveBuffer.erase(
                client.receiveBuffer.begin(),
                client.receiveBuffer.begin() + frameSize);

            json response;

            try
            {
                json request = json::parse(payload);

                response = HandleStoreMessage(db, request);
            }
            catch (const std::exception&)
            {
                response = {
                    {"action", "ERROR"},
                    {"success", false},
                    {"error", "INVALID_JSON"}
                };
            }

            std::string responsePayload = response.dump();

            if (responsePayload.size() > MAX_FRAME_SIZE)
                return false;

            uint32_t responseLength =
                htonl(static_cast<uint32_t>(
                    responsePayload.size()));

            const char* header =
                reinterpret_cast<const char*>(&responseLength);

            client.sendBuffer.insert(
                client.sendBuffer.end(),
                header,
                header + sizeof(responseLength));

            client.sendBuffer.insert(
                client.sendBuffer.end(),
                responsePayload.begin(),
                responsePayload.end());
        }

        return true;
    }

    DWORD RunServer(EchoServerContext* context)
    {
        if (!EnsureDatabaseDirectory())
        {
            InterlockedExchange(&context->startupSucceeded, 0);
            context->startupError = GetLastError();
            SetEvent(context->startupEvent);
            return 1;
        }

        sqlite3* rawDatabase = nullptr;

        if (sqlite3_open(DATABASE_PATH, &rawDatabase) != SQLITE_OK)
        {
            if (rawDatabase != nullptr)
                sqlite3_close(rawDatabase);

            InterlockedExchange(&context->startupSucceeded, 0);
            context->startupError = ERROR_OPEN_FAILED;
            SetEvent(context->startupEvent);
            return 1;
        }

        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(
            rawDatabase,
            sqlite3_close);

        if (!InitializeDatabase(database.get()))
        {
            InterlockedExchange(&context->startupSucceeded, 0);
            context->startupError = ERROR_DATABASE_FAILURE;
            SetEvent(context->startupEvent);
            return 1;
        }

        if (!InitializeSocketLibrary())
        {
            InterlockedExchange(&context->startupSucceeded, 0);
            context->startupError = WSAGetLastError();
            SetEvent(context->startupEvent);
            return 1;
        }

        SOCKET listenSocket = socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP);

        if (listenSocket == INVALID_SOCKET)
        {
            InterlockedExchange(&context->startupSucceeded, 0);
            context->startupError = WSAGetLastError();
            WSACleanup();
            SetEvent(context->startupEvent);
            return 1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(ECHO_PORT);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(
            listenSocket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR)
        {
            InterlockedExchange(&context->startupSucceeded, 0);
            context->startupError = WSAGetLastError();

            closesocket(listenSocket);
            WSACleanup();

            SetEvent(context->startupEvent);
            return 1;
        }

        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
        {
            InterlockedExchange(&context->startupSucceeded, 0);
            context->startupError = WSAGetLastError();

            closesocket(listenSocket);
            WSACleanup();

            SetEvent(context->startupEvent);
            return 1;
        }

        u_long nonBlocking = 1;

        if (ioctlsocket(
            listenSocket,
            FIONBIO,
            &nonBlocking) == SOCKET_ERROR)
        {
            InterlockedExchange(&context->startupSucceeded, 0);
            context->startupError = WSAGetLastError();

            closesocket(listenSocket);
            WSACleanup();

            SetEvent(context->startupEvent);
            return 1;
        }

        InterlockedExchange(&context->startupSucceeded, 1);
        context->startupError = ERROR_SUCCESS;

        SetEvent(context->startupEvent);

        std::vector<ClientState> clients;

        while (WaitForSingleObject(context->stopEvent, 0)
            == WAIT_TIMEOUT)
        {
            fd_set readSet;
            fd_set writeSet;

            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);

            FD_SET(listenSocket, &readSet);

            for (const auto& client : clients)
            {
                FD_SET(client.socket, &readSet);

                if (client.sent < client.sendBuffer.size())
                    FD_SET(client.socket, &writeSet);
            }

            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 250000;

            int selected = select(
                0,
                &readSet,
                &writeSet,
                nullptr,
                &timeout);

            if (selected == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAEINTR)
                    continue;

                break;
            }

            if (FD_ISSET(listenSocket, &readSet))
            {
                SOCKET clientSocket = accept(
                    listenSocket,
                    nullptr,
                    nullptr);

                if (clientSocket != INVALID_SOCKET)
                {
                    u_long clientNonBlocking = 1;

                    if (ioctlsocket(
                        clientSocket,
                        FIONBIO,
                        &clientNonBlocking) == 0)
                    {
                        ClientState client;
                        client.socket = clientSocket;

                        clients.push_back(
                            std::move(client));
                    }
                    else
                    {
                        closesocket(clientSocket);
                    }
                }
            }

            for (size_t i = 0; i < clients.size();)
            {
                ClientState& client = clients[i];
                bool removeClient = false;

                if (FD_ISSET(client.socket, &readSet))
                {
                    char buffer[8192];

                    int received = recv(
                        client.socket,
                        buffer,
                        sizeof(buffer),
                        0);

                    if (received == 0)
                    {
                        removeClient = true;
                    }
                    else if (received == SOCKET_ERROR)
                    {
                        int error = WSAGetLastError();

                        if (error != WSAEWOULDBLOCK
                            && error != WSAEINTR)
                        {
                            removeClient = true;
                        }
                    }
                    else
                    {
                        client.receiveBuffer.insert(
                            client.receiveBuffer.end(),
                            buffer,
                            buffer + received);

                        if (client.receiveBuffer.size()
            > MAX_FRAME_SIZE + sizeof(uint32_t))
                        {
                            removeClient = true;
                        }
                        else if (!ProcessReceiveBuffer(
                            database.get(),
                            client))
                        {
                            removeClient = true;
                        }
                    }
                }

                if (!removeClient
                    && FD_ISSET(client.socket, &writeSet))
                {
                    size_t remaining =
                        client.sendBuffer.size() - client.sent;

                    int sent = send(
                        client.socket,
                        client.sendBuffer.data() + client.sent,
                        static_cast<int>(remaining),
                        0);

                    if (sent == 0)
                    {
                        removeClient = true;
                    }
                    else if (sent == SOCKET_ERROR)
                    {
                        int error = WSAGetLastError();

                        if (error != WSAEWOULDBLOCK
                            && error != WSAEINTR)
                        {
                            removeClient = true;
                        }
                    }
                    else
                    {
                        client.sent += static_cast<size_t>(sent);

                        if (client.sent == client.sendBuffer.size())
                        {
                            client.sendBuffer.clear();
                            client.sent = 0;
                        }
                    }
                }

                if (removeClient)
                {
                    CloseClient(client);

                    clients.erase(
                        clients.begin() + i);
                }
                else
                {
                    ++i;
                }
            }
        }

        CloseAllClients(clients);

        closesocket(listenSocket);
        WSACleanup();

        return 0;
    }
}

DWORD WINAPI EchoServerThread(LPVOID lpParam)
{
    EchoServerContext* context =
        static_cast<EchoServerContext*>(lpParam);

    if (context == nullptr)
        return 1;

    return RunServer(context);
}