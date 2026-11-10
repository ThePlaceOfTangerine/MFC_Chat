#include "Server.h"
#include "Database.h"
#include "json.hpp"
#include "Base64.h"

#include <iostream>
#include <thread>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>

#include <winsvc.h>

using json = nlohmann::json;

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Advapi32.lib")

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


// ===================== Lưu trữ tệp & avatar =====================

static const int64_t MAX_IMAGE_BYTES = 10LL * 1024 * 1024;     // 10 MB
static const int64_t MAX_VIDEO_BYTES = 100LL * 1024 * 1024;    // 100 MB
static const int64_t MAX_AVATAR_BYTES = 256LL * 1024;          // 256 KB
static const int64_t MAX_THUMB_BYTES = 128LL * 1024;           // 128 KB
static const size_t DOWNLOAD_CHUNK_BYTES = 192 * 1024;         // 192 KB / gói

// Tin nhắn có tệp đính kèm lưu nội dung dạng: "@@FILE@@" + JSON mô tả tệp.
// Client thường gửi text bắt đầu bằng tiền tố này sẽ bị từ chối.
static const char ATTACHMENT_PREFIX[] = "@@FILE@@";

static std::string DataRoot()
{
    std::string path = DATABASE_PATH;
    size_t pos = path.find_last_of("\\/");

    std::string directory =
        (pos == std::string::npos) ? std::string(".") : path.substr(0, pos);

    return directory + "\\data";
}

static std::string AttachmentDir() { return DataRoot() + "\\attachments"; }
static std::string AvatarDir() { return DataRoot() + "\\avatars"; }
static std::string TempDir() { return DataRoot() + "\\tmp"; }
static std::string ThumbDir() { return DataRoot() + "\\thumbs"; }

static std::string ThumbnailPath(int64_t attachmentId)
{
    return ThumbDir() + "\\" + std::to_string(attachmentId) + ".jpg";
}

static bool EnsureDirectory(const std::string& directory)
{
    if (CreateDirectoryA(directory.c_str(), nullptr))
        return true;

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool EnsureStorageDirectories()
{
    return EnsureDirectory(DataRoot()) &&
        EnsureDirectory(AttachmentDir()) &&
        EnsureDirectory(AvatarDir()) &&
        EnsureDirectory(ThumbDir()) &&
        EnsureDirectory(TempDir());
}

// Lấy phần tên file (bỏ đường dẫn) và thay ký tự không hợp lệ.
static std::string SanitizeFileName(const std::string& input)
{
    size_t pos = input.find_last_of("\\/");

    std::string name =
        (pos == std::string::npos) ? input : input.substr(pos + 1);

    std::string result;

    for (unsigned char c : name)
    {
        if (c < 32 ||
            c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '|' || c == '?' || c == '*')
        {
            result.push_back('_');
        }
        else
        {
            result.push_back(static_cast<char>(c));
        }
    }

    while (!result.empty() &&
        (result.back() == ' ' || result.back() == '.'))
    {
        result.pop_back();
    }

    return result;
}

// Trả về đuôi file viết thường, gồm cả dấu chấm (vd ".jpg"), hoặc rỗng.
static std::string FileExtensionLower(const std::string& fileName)
{
    size_t pos = fileName.find_last_of('.');

    if (pos == std::string::npos || pos + 1 >= fileName.size())
        return std::string();

    std::string ext = fileName.substr(pos);

    for (char& c : ext)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }

    return ext;
}

// "image" / "video" nếu là loại được phép, ngược lại rỗng.
static std::string ClassifyKind(const std::string& extension)
{
    static const char* imageExts[] =
    { ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp" };

    static const char* videoExts[] =
    { ".mp4", ".webm", ".mov", ".avi", ".mkv", ".wmv" };

    for (const char* ext : imageExts)
    {
        if (extension == ext)
            return "image";
    }

    for (const char* ext : videoExts)
    {
        if (extension == ext)
            return "video";
    }

    return std::string();
}

// Đọc field kiểu an toàn: sai kiểu hoặc thiếu thì trả về giá trị mặc định
// (json::value() sẽ ném exception nếu field có nhưng sai kiểu).
static std::string GetString(const json& request, const char* key)
{
    if (request.contains(key) && request[key].is_string())
        return request[key].get<std::string>();

    return std::string();
}

static int64_t GetInt64(
    const json& request, const char* key, int64_t defaultValue)
{
    if (request.contains(key) && request[key].is_number_integer())
        return request[key].get<int64_t>();

    return defaultValue;
}

static bool ReadWholeFile(
    const std::string& path, std::string& output, size_t maxBytes)
{
    output.clear();

    FILE* file = nullptr;

    if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr)
        return false;

    std::vector<char> buffer(64 * 1024);
    size_t total = 0;

    while (true)
    {
        size_t count = fread(buffer.data(), 1, buffer.size(), file);

        if (count == 0)
            break;

        total += count;

        if (total > maxBytes)
        {
            fclose(file);
            output.clear();
            return false;
        }

        output.append(buffer.data(), count);
    }

    fclose(file);
    return true;
}

static bool WriteWholeFile(const std::string& path, const std::string& data)
{
    FILE* file = nullptr;

    if (fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr)
        return false;

    const bool ok =
        fwrite(data.data(), 1, data.size(), file) == data.size();

    fclose(file);
    return ok;
}

static bool IsJpegData(const std::string& data)
{
    return data.size() > 4 &&
        static_cast<unsigned char>(data[0]) == 0xFF &&
        static_cast<unsigned char>(data[1]) == 0xD8 &&
        static_cast<unsigned char>(data[2]) == 0xFF;
}

static bool FileExists(const std::string& path)
{
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool IsPngData(const std::string& data)
{
    static const unsigned char signature[8] =
    { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    return data.size() > sizeof(signature) &&
        memcmp(data.data(), signature, sizeof(signature)) == 0;
}

static std::string BuildAttachmentContent(const AttachmentInfo& info)
{
    json meta = {
        {"id", info.id},
        {"kind", info.kind},
        {"name", info.fileName},
        {"size", info.size}
    };

    // Có ảnh thu nhỏ thì client sẽ tự tải về để hiển thị ngay trong khung chat.
    if (info.kind == "image" && FileExists(ThumbnailPath(info.id)))
        meta["thumb"] = true;

    return std::string(ATTACHMENT_PREFIX) + meta.dump();
}

static bool LooksLikeAttachmentContent(const std::string& content)
{
    return content.compare(0, sizeof(ATTACHMENT_PREFIX) - 1, ATTACHMENT_PREFIX) == 0;
}

// Hủy phiên upload hiện tại: đóng file và xóa file tạm.
static void AbortUpload(const std::shared_ptr<ClientConnection>& client)
{
    UploadSession& upload = client->upload;

    if (upload.file != nullptr)
    {
        fclose(upload.file);
        upload.file = nullptr;
    }

    if (!upload.tempPath.empty())
        DeleteFileA(upload.tempPath.c_str());

    upload = UploadSession();
}

static void SendUploadFailure(
    const std::shared_ptr<ClientConnection>& client,
    const std::string& uploadId,
    const std::string& message)
{
    SendFrame(client, json{
        {"action", "UPLOAD_RESULT"},
        {"success", false},
        {"uploadId", uploadId},
        {"message", message}
        }.dump());
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

    const bool hasAttachment =
        request.contains("attachmentId") &&
        request["attachmentId"].is_number_integer();

    if (!request.contains("recipient") ||
        !request["recipient"].is_string() ||
        (!hasAttachment &&
            (!request.contains("content") ||
                !request["content"].is_string())))
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

    // Tin có tệp: nội dung do server tự tạo từ thông tin tệp đã upload.
    std::string content = hasAttachment
        ? std::string()
        : request["content"].get<std::string>();

    if (!IsValidUsername(recipient) ||
        (!hasAttachment && content.empty()) ||
        content.size() > MAX_MESSAGE_SIZE ||
        LooksLikeAttachmentContent(content))
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

    if (hasAttachment)
    {
        AttachmentInfo attachment;

        if (!db.BindAttachmentToUser(
            request["attachmentId"].get<int64_t>(),
            client->username,
            recipient,
            attachment))
        {
            db.Close();

            SendFrame(client, json{
                {"action", "SEND_MESSAGE_RESULT"},
                {"success", false},
                {"message", "Attachment not available"}
                }.dump());

            return;
        }

        content = BuildAttachmentContent(attachment);
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
    const bool hasAttachment =
        request.contains("attachmentId") &&
        request["attachmentId"].is_number_integer();

    if (!IsAuthenticated(client) ||
        !request.contains("groupId") ||
        !request["groupId"].is_number_integer() ||
        (!hasAttachment &&
            (!request.contains("content") ||
                !request["content"].is_string())))
    {
        SendFrame(client, json{
            {"action", "SEND_GROUP_MESSAGE_RESULT"},
            {"success", false},
            {"message", "Invalid group message"}
            }.dump());

        return;
    }

    int64_t groupId = request["groupId"].get<int64_t>();

    std::string content = hasAttachment
        ? std::string()
        : request["content"].get<std::string>();

    if (groupId <= 0 ||
        (!hasAttachment && content.empty()) ||
        content.size() > MAX_MESSAGE_SIZE ||
        LooksLikeAttachmentContent(content))
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

    if (hasAttachment)
    {
        AttachmentInfo attachment;

        if (!db.BindAttachmentToGroup(
            request["attachmentId"].get<int64_t>(),
            client->username,
            groupId,
            attachment))
        {
            db.Close();

            SendFrame(client, json{
                {"action", "SEND_GROUP_MESSAGE_RESULT"},
                {"success", false},
                {"message", "Attachment not available"}
                }.dump());

            return;
        }

        content = BuildAttachmentContent(attachment);
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
            {"lastSentAt", item.lastSentAt},
            {"avatarVersion", item.avatarVersion}
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
            {"messageId", item.messageId},
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

// ===================== Upload / tải tệp / avatar =====================

static void HandleUploadBegin(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    const std::string uploadId = GetString(request, "uploadId");

    if (!IsAuthenticated(client))
    {
        SendUploadFailure(client, uploadId, "Login required");
        return;
    }

    // Bỏ phiên cũ nếu client bắt đầu phiên mới.
    AbortUpload(client);

    const std::string fileName =
        SanitizeFileName(GetString(request, "fileName"));

    const int64_t size = GetInt64(request, "size", 0);

    if (uploadId.empty() || uploadId.size() > 64 ||
        fileName.empty() || fileName.size() > 150)
    {
        SendUploadFailure(client, uploadId, "Invalid upload request");
        return;
    }

    const std::string kind = ClassifyKind(FileExtensionLower(fileName));

    if (kind.empty())
    {
        SendUploadFailure(client, uploadId, "Unsupported file type");
        return;
    }

    const int64_t limit =
        (kind == "image") ? MAX_IMAGE_BYTES : MAX_VIDEO_BYTES;

    if (size <= 0 || size > limit)
    {
        SendUploadFailure(client, uploadId, "File size is not allowed");
        return;
    }

    static std::atomic<uint64_t> counter{ 0 };

    const std::string tempPath =
        TempDir() + "\\" + std::to_string(GetTickCount64()) + "_" +
        std::to_string(++counter) + ".part";

    FILE* file = nullptr;

    if (fopen_s(&file, tempPath.c_str(), "wb") != 0 || file == nullptr)
    {
        SendUploadFailure(client, uploadId, "Cannot create temporary file");
        return;
    }

    UploadSession& upload = client->upload;

    upload.active = true;
    upload.file = file;
    upload.uploadId = uploadId;
    upload.tempPath = tempPath;
    upload.fileName = fileName;
    upload.kind = kind;
    upload.declaredSize = size;
    upload.received = 0;

    SendFrame(client, json{
        {"action", "UPLOAD_READY"},
        {"success", true},
        {"uploadId", uploadId}
        }.dump());
}

static void HandleUploadChunk(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    UploadSession& upload = client->upload;

    const std::string uploadId = GetString(request, "uploadId");

    // Gói không thuộc phiên hiện tại (vd đã bị hủy): bỏ qua.
    if (!IsAuthenticated(client) ||
        !upload.active ||
        upload.uploadId != uploadId)
    {
        return;
    }

    const int64_t offset = GetInt64(request, "offset", -1);

    std::string raw;

    if (offset != upload.received ||
        !b64::Decode(GetString(request, "data"), raw) ||
        raw.empty() ||
        upload.received + static_cast<int64_t>(raw.size())
            > upload.declaredSize)
    {
        AbortUpload(client);
        SendUploadFailure(client, uploadId, "Invalid upload data");
        return;
    }

    if (fwrite(raw.data(), 1, raw.size(), upload.file) != raw.size())
    {
        AbortUpload(client);
        SendUploadFailure(client, uploadId, "Cannot write file");
        return;
    }

    upload.received += static_cast<int64_t>(raw.size());
}

static void HandleUploadEnd(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    UploadSession& upload = client->upload;

    const std::string uploadId = GetString(request, "uploadId");

    if (!IsAuthenticated(client) ||
        !upload.active ||
        upload.uploadId != uploadId)
    {
        SendUploadFailure(client, uploadId, "No active upload");
        return;
    }

    if (upload.received != upload.declaredSize)
    {
        AbortUpload(client);
        SendUploadFailure(client, uploadId, "Incomplete upload");
        return;
    }

    fclose(upload.file);
    upload.file = nullptr;

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
    {
        AbortUpload(client);
        SendUploadFailure(client, uploadId, "Database unavailable");
        return;
    }

    int64_t attachmentId = 0;

    bool ok = db.CreateAttachment(
        client->username,
        upload.kind,
        upload.fileName,
        upload.declaredSize,
        attachmentId);

    const std::string storedName =
        std::to_string(attachmentId) + FileExtensionLower(upload.fileName);

    const std::string finalPath = AttachmentDir() + "\\" + storedName;

    if (ok && !MoveFileExA(
        upload.tempPath.c_str(), finalPath.c_str(),
        MOVEFILE_REPLACE_EXISTING))
    {
        db.DeleteAttachment(attachmentId);
        ok = false;
    }

    if (ok && !db.SetAttachmentStoredName(attachmentId, storedName))
    {
        DeleteFileA(finalPath.c_str());
        db.DeleteAttachment(attachmentId);
        ok = false;
    }

    db.Close();

    if (!ok)
    {
        AbortUpload(client);
        SendUploadFailure(client, uploadId, "Cannot save file");
        return;
    }

    json result = {
        {"action", "UPLOAD_RESULT"},
        {"success", true},
        {"uploadId", uploadId},
        {"attachmentId", attachmentId},
        {"kind", upload.kind},
        {"fileName", upload.fileName},
        {"size", upload.declaredSize}
    };

    // File tạm đã được di chuyển, không cần xóa nữa.
    upload.tempPath.clear();
    upload = UploadSession();

    SendFrame(client, result.dump());
}

static void SendAttachmentFailure(
    const std::shared_ptr<ClientConnection>& client,
    int64_t attachmentId,
    const std::string& message)
{
    SendFrame(client, json{
        {"action", "ATTACHMENT_CHUNK"},
        {"success", false},
        {"attachmentId", attachmentId},
        {"message", message}
        }.dump());
}

static void HandleGetAttachment(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    const int64_t attachmentId = GetInt64(request, "attachmentId", 0);
    const int64_t offset = GetInt64(request, "offset", 0);

    if (!IsAuthenticated(client) || attachmentId <= 0 || offset < 0)
    {
        SendAttachmentFailure(client, attachmentId, "Invalid request");
        return;
    }

    AttachmentInfo info;

    {
        Database db;

        if (!db.Open(DATABASE_PATH) || !db.Initialize())
        {
            SendAttachmentFailure(client, attachmentId, "Database unavailable");
            return;
        }

        const bool allowed =
            db.GetAttachmentForUser(attachmentId, client->username, info);

        db.Close();

        if (!allowed)
        {
            SendAttachmentFailure(client, attachmentId, "Attachment not found");
            return;
        }
    }

    const std::string path = AttachmentDir() + "\\" + info.storedName;

    FILE* file = nullptr;

    if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr)
    {
        SendAttachmentFailure(client, attachmentId, "File is missing");
        return;
    }

    _fseeki64(file, 0, SEEK_END);
    const int64_t total = _ftelli64(file);

    if (total < 0 || offset > total)
    {
        fclose(file);
        SendAttachmentFailure(client, attachmentId, "Invalid offset");
        return;
    }

    _fseeki64(file, offset, SEEK_SET);

    std::vector<unsigned char> buffer(DOWNLOAD_CHUNK_BYTES);
    const size_t count = fread(buffer.data(), 1, buffer.size(), file);

    fclose(file);

    SendFrame(client, json{
        {"action", "ATTACHMENT_CHUNK"},
        {"success", true},
        {"attachmentId", attachmentId},
        {"offset", offset},
        {"size", total},
        {"eof", offset + static_cast<int64_t>(count) >= total},
        {"data", b64::Encode(buffer.data(), count)}
        }.dump());
}

static void HandleSetThumbnail(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    const int64_t attachmentId = GetInt64(request, "attachmentId", 0);

    auto reply = [&client, attachmentId](bool success, const std::string& message)
    {
        SendFrame(client, json{
            {"action", "SET_THUMBNAIL_RESULT"},
            {"success", success},
            {"attachmentId", attachmentId},
            {"message", message}
            }.dump());
    };

    if (!IsAuthenticated(client) || attachmentId <= 0)
    {
        reply(false, "Invalid request");
        return;
    }

    std::string jpeg;

    if (!b64::Decode(GetString(request, "data"), jpeg) ||
        jpeg.empty() ||
        jpeg.size() > static_cast<size_t>(MAX_THUMB_BYTES) ||
        !IsJpegData(jpeg))
    {
        reply(false, "Invalid thumbnail");
        return;
    }

    {
        Database db;

        if (!db.Open(DATABASE_PATH) || !db.Initialize())
        {
            reply(false, "Database unavailable");
            return;
        }

        AttachmentInfo info;

        // Chỉ người upload mới được đặt ảnh thu nhỏ, và chỉ cho ảnh.
        const bool allowed =
            db.GetOwnedAttachment(attachmentId, client->username, info) &&
            info.kind == "image";

        db.Close();

        if (!allowed)
        {
            reply(false, "Attachment not found");
            return;
        }
    }

    const std::string path = ThumbnailPath(attachmentId);
    const std::string tempPath = path + ".tmp";

    if (!WriteWholeFile(tempPath, jpeg) ||
        !MoveFileExA(
            tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileA(tempPath.c_str());
        reply(false, "Cannot save thumbnail");
        return;
    }

    reply(true, "");
}

static void HandleGetThumbnail(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    const int64_t attachmentId = GetInt64(request, "attachmentId", 0);

    auto fail = [&client, attachmentId](const std::string& message)
    {
        SendFrame(client, json{
            {"action", "THUMBNAIL_RESULT"},
            {"success", false},
            {"attachmentId", attachmentId},
            {"message", message}
            }.dump());
    };

    if (!IsAuthenticated(client) || attachmentId <= 0)
    {
        fail("Invalid request");
        return;
    }

    AttachmentInfo info;

    {
        Database db;

        if (!db.Open(DATABASE_PATH) || !db.Initialize())
        {
            fail("Database unavailable");
            return;
        }

        // Cùng quy tắc quyền như tải tệp gốc.
        const bool allowed =
            db.GetAttachmentForUser(attachmentId, client->username, info) &&
            info.kind == "image";

        db.Close();

        if (!allowed)
        {
            fail("Attachment not found");
            return;
        }
    }

    std::string jpeg;

    if (!ReadWholeFile(
        ThumbnailPath(attachmentId), jpeg,
        static_cast<size_t>(MAX_THUMB_BYTES)))
    {
        fail("No thumbnail");
        return;
    }

    SendFrame(client, json{
        {"action", "THUMBNAIL_RESULT"},
        {"success", true},
        {"attachmentId", attachmentId},
        {"data", b64::Encode(jpeg)}
        }.dump());
}

static void HandleSetAvatar(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    auto fail = [&client](const std::string& message)
    {
        SendFrame(client, json{
            {"action", "SET_AVATAR_RESULT"},
            {"success", false},
            {"message", message}
            }.dump());
    };

    if (!IsAuthenticated(client))
    {
        fail("Login required");
        return;
    }

    std::string png;

    if (!b64::Decode(GetString(request, "data"), png) ||
        png.empty() ||
        png.size() > static_cast<size_t>(MAX_AVATAR_BYTES) ||
        !IsPngData(png))
    {
        fail("Invalid avatar image");
        return;
    }

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
    {
        fail("Database unavailable");
        return;
    }

    int64_t userId = 0;

    if (!db.GetUserId(client->username, userId))
    {
        db.Close();
        fail("User not found");
        return;
    }

    const std::string path =
        AvatarDir() + "\\" + std::to_string(userId) + ".png";

    const std::string tempPath = path + ".tmp";

    if (!WriteWholeFile(tempPath, png) ||
        !MoveFileExA(
            tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileA(tempPath.c_str());
        db.Close();
        fail("Cannot save avatar");
        return;
    }

    int version = 0;
    const bool bumped = db.BumpAvatarVersion(client->username, version);

    db.Close();

    if (!bumped)
    {
        fail("Cannot update avatar");
        return;
    }

    SendFrame(client, json{
        {"action", "SET_AVATAR_RESULT"},
        {"success", true},
        {"version", version}
        }.dump());
}

static void HandleGetAvatar(
    const std::shared_ptr<ClientConnection>& client,
    const json& request)
{
    const std::string username = GetString(request, "username");

    auto fail = [&client, &username](const std::string& message)
    {
        SendFrame(client, json{
            {"action", "AVATAR_RESULT"},
            {"success", false},
            {"username", username},
            {"message", message}
            }.dump());
    };

    if (!IsAuthenticated(client) || !IsValidUsername(username))
    {
        fail("Invalid request");
        return;
    }

    Database db;

    if (!db.Open(DATABASE_PATH) || !db.Initialize())
    {
        fail("Database unavailable");
        return;
    }

    int64_t userId = 0;
    int version = 0;

    const bool found =
        db.GetUserId(username, userId) &&
        db.GetAvatarVersion(username, version);

    db.Close();

    if (!found)
    {
        fail("User not found");
        return;
    }

    std::string encoded;

    if (version > 0)
    {
        std::string png;

        const std::string path =
            AvatarDir() + "\\" + std::to_string(userId) + ".png";

        if (ReadWholeFile(path, png, static_cast<size_t>(MAX_AVATAR_BYTES)))
            encoded = b64::Encode(png);
    }

    SendFrame(client, json{
        {"action", "AVATAR_RESULT"},
        {"success", true},
        {"username", username},
        {"version", encoded.empty() ? 0 : version},
        {"data", encoded}
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

        if (action == "UPLOAD_BEGIN")
        {
            HandleUploadBegin(client, request);
            continue;
        }

        if (action == "UPLOAD_CHUNK")
        {
            HandleUploadChunk(client, request);
            continue;
        }

        if (action == "UPLOAD_END")
        {
            HandleUploadEnd(client, request);
            continue;
        }

        if (action == "GET_ATTACHMENT")
        {
            HandleGetAttachment(client, request);
            continue;
        }

        if (action == "SET_THUMBNAIL")
        {
            HandleSetThumbnail(client, request);
            continue;
        }

        if (action == "GET_THUMBNAIL")
        {
            HandleGetThumbnail(client, request);
            continue;
        }

        if (action == "SET_AVATAR")
        {
            HandleSetAvatar(client, request);
            continue;
        }

        if (action == "GET_AVATAR")
        {
            HandleGetAvatar(client, request);
            continue;
        }

        SendFrame(client, json{
            {"action", "ERROR"},
            {"message", "Unknown action"}
            }.dump());
    }

    client->closing = true;
    client->authenticated = false;

    // Xóa file tạm nếu client ngắt kết nối giữa chừng khi đang upload.
    AbortUpload(client);

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

// ===================== Chạy như dịch vụ Windows =====================
//
//   Server.exe --install [--db "đường\dẫn\chat.db"]   cài dịch vụ (cần quyền Administrator)
//   Server.exe --uninstall                            gỡ dịch vụ
//   Server.exe --start / --stop                       bật / tắt dịch vụ
//   Server.exe --console [--db "..."]                 chạy ở chế độ console (gỡ lỗi)
//
// Chạy Server.exe không tham số: nếu do Service Manager khởi động thì chạy như
// dịch vụ, nếu mở từ console / Visual Studio thì tự chuyển sang chế độ console.

static const char SERVICE_NAME[] = "MFCChatServer";
static const char SERVICE_DISPLAY_NAME[] = "MFC Chat Server";
static const char SERVICE_DESCRIPTION_TEXT[] =
"TCP server for the MFC Chat application "
"(register, login, chat, file transfer).";

static std::atomic<bool> g_stopRequested{ false };
static std::atomic<SOCKET> g_listenSocket{ INVALID_SOCKET };
static std::ofstream g_logFile;
static std::string g_databasePathStorage;

static SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
static SERVICE_STATUS g_serviceStatus = {};

// Yêu cầu dừng: đóng socket lắng nghe để accept() thoát ra khỏi vòng lặp.
static void RequestStop()
{
    g_stopRequested = true;

    const SOCKET listener = g_listenSocket.exchange(INVALID_SOCKET);

    if (listener != INVALID_SOCKET)
    {
        shutdown(listener, SD_BOTH);
        closesocket(listener);
    }
}

static BOOL WINAPI ConsoleControlHandler(DWORD type)
{
    if (type == CTRL_C_EVENT ||
        type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT)
    {
        RequestStop();
        return TRUE;
    }

    return FALSE;
}

static std::string ExecutableDirectory()
{
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);

    const std::string full(path, length);
    const size_t pos = full.find_last_of("\\/");

    return (pos == std::string::npos) ? std::string(".") : full.substr(0, pos);
}

static bool DirectoryExists(const std::string& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());

    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Máy phát triển có sẵn thư mục D:\Work\CMC\MFCChat thì dùng DB ở đó
// (giữ dữ liệu cũ); ngược lại để chat.db cạnh Server.exe.
static std::string DefaultDatabasePath()
{
    const std::string developmentFolder = "D:\\Work\\CMC\\MFCChat";

    if (DirectoryExists(developmentFolder))
        return developmentFolder + "\\chat.db";

    return ExecutableDirectory() + "\\chat.db";
}

// Dịch vụ không có cửa sổ console: ghi log ra data\server.log.
static void RedirectOutputToLog()
{
    EnsureDirectory(DataRoot());

    g_logFile.open(DataRoot() + "\\server.log", std::ios::app);

    if (g_logFile.is_open())
    {
        std::cout.rdbuf(g_logFile.rdbuf());
        std::cerr.rdbuf(g_logFile.rdbuf());

        std::cout << std::unitbuf;
        std::cerr << std::unitbuf;
    }
}

// Phần chính của server (dùng chung cho chế độ console và chế độ dịch vụ).
// onReady được gọi khi server đã bắt đầu lắng nghe.
static int RunServer(const std::function<void()>& onReady)
{
    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    if (!EnsureStorageDirectories())
    {
        std::cerr << "Cannot create data directories under: "
            << DataRoot() << "\n";
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

    g_listenSocket = listenSocket;

    // Có thể có yêu cầu dừng đến ngay trong lúc khởi động.
    if (g_stopRequested)
        RequestStop();

    std::cout << "MFCChat TCP server listening on port "
        << SERVER_PORT << "\n";
    std::cout << "Database: " << DATABASE_PATH << "\n";

    if (onReady)
        onReady();

    while (!g_stopRequested)
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
            if (g_stopRequested)
                break;

            std::cerr << "accept() failed: "
                << WSAGetLastError() << "\n";

            Sleep(50);   // tránh vòng lặp chiếm CPU khi lỗi lặp lại
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

    // Dừng: đóng listener và ngắt các client để thread của chúng tự thoát.
    RequestStop();

    {
        std::lock_guard<std::mutex> lock(clientsMutex);

        for (const auto& client : clients)
        {
            if (client && client->socket != INVALID_SOCKET)
                shutdown(client->socket, SD_BOTH);
        }
    }

    Sleep(500);
    WSACleanup();

    std::cout << "Server stopped.\n";
    return 0;
}

static void ReportServiceStatus(
    DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0)
{
    static DWORD checkPoint = 1;

    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwCurrentState = state;
    g_serviceStatus.dwWin32ExitCode = exitCode;
    g_serviceStatus.dwWaitHint = waitHint;

    g_serviceStatus.dwControlsAccepted =
        (state == SERVICE_START_PENDING)
        ? 0
        : (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);

    g_serviceStatus.dwCheckPoint =
        (state == SERVICE_RUNNING || state == SERVICE_STOPPED)
        ? 0
        : checkPoint++;

    SetServiceStatus(g_statusHandle, &g_serviceStatus);
}

static void WINAPI ServiceControlHandler(DWORD control)
{
    if (control == SERVICE_CONTROL_STOP ||
        control == SERVICE_CONTROL_SHUTDOWN)
    {
        ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        RequestStop();
    }
}

static void WINAPI ServiceMain(DWORD, LPSTR*)
{
    g_statusHandle =
        RegisterServiceCtrlHandlerA(SERVICE_NAME, ServiceControlHandler);

    if (g_statusHandle == nullptr)
        return;

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 5000);

    RedirectOutputToLog();

    std::cout << "Service starting...\n";

    const int exitCode = RunServer(
        []() { ReportServiceStatus(SERVICE_RUNNING); });

    ReportServiceStatus(
        SERVICE_STOPPED,
        exitCode == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR);
}

static int InstallService(const std::string& databasePath)
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    std::string commandLine =
        std::string("\"") + exePath + "\" --service";

    if (!databasePath.empty())
        commandLine += " --db \"" + databasePath + "\"";

    SC_HANDLE manager =
        OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);

    if (manager == nullptr)
    {
        std::cerr << "OpenSCManager failed (" << GetLastError() << "). "
            << "Run this command as Administrator.\n";
        return 1;
    }

    SC_HANDLE service = CreateServiceA(
        manager,
        SERVICE_NAME,
        SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,          // tự chạy khi Windows khởi động
        SERVICE_ERROR_NORMAL,
        commandLine.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (service == nullptr)
    {
        const DWORD error = GetLastError();

        if (error == ERROR_SERVICE_EXISTS)
            std::cerr << "Service is already installed.\n";
        else
            std::cerr << "CreateService failed (" << error << ").\n";

        CloseServiceHandle(manager);
        return 1;
    }

    SERVICE_DESCRIPTIONA description = {};
    description.lpDescription = const_cast<char*>(SERVICE_DESCRIPTION_TEXT);
    ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &description);

    // Tự khởi động lại nếu tiến trình bị lỗi.
    SC_ACTION actions[3] = {
        { SC_ACTION_RESTART, 5000 },
        { SC_ACTION_RESTART, 5000 },
        { SC_ACTION_NONE, 0 }
    };

    SERVICE_FAILURE_ACTIONSA failure = {};
    failure.dwResetPeriod = 24 * 60 * 60;
    failure.cActions = 3;
    failure.lpsaActions = actions;
    ChangeServiceConfig2A(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

    std::cout << "Service '" << SERVICE_NAME << "' installed.\n"
        << "Start it with:  Server.exe --start   (or: net start "
        << SERVICE_NAME << ")\n";

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return 0;
}

static int UninstallService()
{
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);

    if (manager == nullptr)
    {
        std::cerr << "OpenSCManager failed (" << GetLastError() << "). "
            << "Run this command as Administrator.\n";
        return 1;
    }

    SC_HANDLE service = OpenServiceA(
        manager, SERVICE_NAME,
        DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);

    if (service == nullptr)
    {
        std::cerr << "Service is not installed (" << GetLastError() << ").\n";
        CloseServiceHandle(manager);
        return 1;
    }

    SERVICE_STATUS status = {};
    ControlService(service, SERVICE_CONTROL_STOP, &status);   // thử dừng trước

    const bool deleted = DeleteService(service) != FALSE;

    if (deleted)
        std::cout << "Service '" << SERVICE_NAME << "' removed.\n";
    else
        std::cerr << "DeleteService failed (" << GetLastError() << ").\n";

    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    return deleted ? 0 : 1;
}

static int StartInstalledService()
{
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);

    if (manager == nullptr)
    {
        std::cerr << "OpenSCManager failed (" << GetLastError() << ").\n";
        return 1;
    }

    SC_HANDLE service = OpenServiceA(manager, SERVICE_NAME, SERVICE_START);

    bool ok = false;

    if (service == nullptr)
    {
        std::cerr << "Service is not installed (" << GetLastError() << ").\n";
    }
    else if (!StartServiceA(service, 0, nullptr))
    {
        std::cerr << "StartService failed (" << GetLastError() << ").\n";
    }
    else
    {
        std::cout << "Service started.\n";
        ok = true;
    }

    if (service != nullptr)
        CloseServiceHandle(service);

    CloseServiceHandle(manager);
    return ok ? 0 : 1;
}

static int StopInstalledService()
{
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);

    if (manager == nullptr)
    {
        std::cerr << "OpenSCManager failed (" << GetLastError() << ").\n";
        return 1;
    }

    SC_HANDLE service = OpenServiceA(manager, SERVICE_NAME, SERVICE_STOP);

    bool ok = false;

    if (service == nullptr)
    {
        std::cerr << "Service is not installed (" << GetLastError() << ").\n";
    }
    else
    {
        SERVICE_STATUS status = {};

        if (ControlService(service, SERVICE_CONTROL_STOP, &status))
        {
            std::cout << "Stop requested.\n";
            ok = true;
        }
        else
        {
            std::cerr << "ControlService failed ("
                << GetLastError() << ").\n";
        }
    }

    if (service != nullptr)
        CloseServiceHandle(service);

    CloseServiceHandle(manager);
    return ok ? 0 : 1;
}

static void PrintUsage()
{
    std::cout
        << "MFC Chat Server\n\n"
        << "  Server.exe --install [--db <path>]  install as a Windows service (Administrator)\n"
        << "  Server.exe --uninstall              remove the service\n"
        << "  Server.exe --start | --stop         start / stop the installed service\n"
        << "  Server.exe --console [--db <path>]  run in console mode (debugging)\n\n"
        << "Without arguments the program runs as a service when started by the\n"
        << "Service Control Manager, otherwise in console mode.\n";
}

static int RunConsole()
{
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
    return RunServer(nullptr);
}

int main(int argc, char** argv)
{
    std::string command;
    std::string databasePath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--db" && i + 1 < argc)
            databasePath = argv[++i];
        else if (arg == "-h" || arg == "/?")
            command = "--help";
        else if (arg.compare(0, 2, "--") == 0)
            command = arg;
    }

    g_databasePathStorage =
        databasePath.empty() ? DefaultDatabasePath() : databasePath;

    DATABASE_PATH = g_databasePathStorage.c_str();

    if (command == "--help")
    {
        PrintUsage();
        return 0;
    }

    if (command == "--install")
        return InstallService(databasePath);

    if (command == "--uninstall")
        return UninstallService();

    if (command == "--start")
        return StartInstalledService();

    if (command == "--stop")
        return StopInstalledService();

    if (command == "--console")
        return RunConsole();

    // Không tham số (hoặc --service): thử chạy dưới Service Control Manager.
    SERVICE_TABLE_ENTRYA table[] =
    {
        { const_cast<char*>(SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };

    if (StartServiceCtrlDispatcherA(table))
        return 0;

    // Không do SCM khởi động (mở từ console hoặc Visual Studio): chạy console.
    if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
        return RunConsole();

    std::cerr << "StartServiceCtrlDispatcher failed ("
        << GetLastError() << ").\n";

    return 1;
}