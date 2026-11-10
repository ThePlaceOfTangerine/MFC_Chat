#pragma once

#include "sqlite3.h"

#include <cstdint>
#include <string>
#include <vector>

struct MessageRecord
{
    std::string senderUsername;
    std::string content;
    std::string sentAt;

    int64_t messageId = 0;
    bool isGroupMessage = false;
    int64_t groupId = 0;
};

struct ConversationSummary
{
    std::string username;
    std::string lastMessage;
    std::string lastSentAt;
    int avatarVersion = 0;
};

// Tệp đính kèm (ảnh/video) đã được upload lên server.
struct AttachmentInfo
{
    int64_t id = 0;
    std::string kind;        // "image" | "video"
    std::string fileName;    // tên hiển thị
    int64_t size = 0;
    std::string storedName;  // tên file trên đĩa server
};

struct GroupSummary
{
    int64_t groupId = 0;
    std::string groupName;
    std::string lastMessage;
    std::string lastSentAt;
};

struct PendingMessage
{
    int64_t messageId = 0;
    int64_t groupId = 0;

    std::string senderUsername;
    std::string recipientUsername;
    std::string content;
    std::string sentAt;
};

class Database
{
public:
    Database();
    ~Database();

    bool Open(const char* path);
    void Close();

    bool Initialize();

    bool CreateUser(
        const char* username,
        const char* password,
        const char* displayName
    );

    bool AuthenticateUser(
        const char* username,
        const char* password,
        int& userId,
        std::string& displayName
    );

    bool GetAllUsers(
        const char* currentUsername,
        std::vector<std::string>& usernames
    );

    bool SaveMessage(
        const char* senderUsername,
        const char* receiverUsername,
        const char* content
    );

    bool LoadMessages(
        const char* username1,
        const char* username2,
        std::vector<MessageRecord>& messages
    );

    bool GetConversationSummaries(
        const char* currentUsername,
        std::vector<ConversationSummary>& conversations
    );

    bool SaveDirectMessage(
        const std::string& senderUsername,
        const std::string& receiverUsername,
        const std::string& content,
        int64_t& messageId
    );

    bool GetPendingMessages(
        const std::string& username,
        std::vector<PendingMessage>& messages
    );

    bool MarkMessageDelivered(
        int64_t messageId,
        const std::string& recipientUsername
    );

    bool MarkMessageRead(
        int64_t messageId,
        const std::string& recipientUsername
    );

    bool CreateGroup(
        const std::string& groupName,
        const std::string& creatorUsername,
        const std::vector<std::string>& memberUsernames,
        int64_t& groupId
    );

    bool IsGroupMember(
        int64_t groupId,
        const std::string& username
    );

    bool SaveGroupMessage(
        int64_t groupId,
        const std::string& senderUsername,
        const std::string& content,
        int64_t& messageId
    );

    bool GetGroupMembers(
        int64_t groupId,
        std::vector<std::string>& usernames
    );

    bool GetUserGroups(
        const std::string& username,
        std::vector<GroupSummary>& groups
    );

    bool MarkGroupMessageDelivered(
        int64_t messageId,
        const std::string& recipientUsername
    );

    bool MarkGroupMessageRead(
        int64_t messageId,
        const std::string& recipientUsername
    );

    bool GetPendingGroupMessages(
        const std::string& username,
        std::vector<PendingMessage>& messages
    );

    bool LoadGroupMessages(
        int64_t groupId,
        const std::string& username,
        std::vector<MessageRecord>& messages
    );

    // ---- Avatar ----
    bool GetUserId(
        const std::string& username,
        int64_t& userId
    );

    bool GetAvatarVersion(
        const std::string& username,
        int& version
    );

    bool BumpAvatarVersion(
        const std::string& username,
        int& newVersion
    );

    // ---- Tệp đính kèm ----
    bool CreateAttachment(
        const std::string& uploaderUsername,
        const std::string& kind,
        const std::string& fileName,
        int64_t size,
        int64_t& attachmentId
    );

    bool SetAttachmentStoredName(
        int64_t attachmentId,
        const std::string& storedName
    );

    bool DeleteAttachment(int64_t attachmentId);

    // Gắn tệp (chưa dùng) vào tin nhắn 1-1. Chỉ người upload mới gắn được.
    bool BindAttachmentToUser(
        int64_t attachmentId,
        const std::string& senderUsername,
        const std::string& recipientUsername,
        AttachmentInfo& info
    );

    // Gắn tệp (chưa dùng) vào tin nhắn nhóm. Người gửi phải là thành viên.
    bool BindAttachmentToGroup(
        int64_t attachmentId,
        const std::string& senderUsername,
        int64_t groupId,
        AttachmentInfo& info
    );

    // Chỉ trả về tệp nếu chính người dùng này là người upload.
    bool GetOwnedAttachment(
        int64_t attachmentId,
        const std::string& username,
        AttachmentInfo& info
    );

    // Chỉ trả về tệp nếu người dùng có quyền xem
    // (người upload, người nhận, hoặc thành viên nhóm).
    bool GetAttachmentForUser(
        int64_t attachmentId,
        const std::string& username,
        AttachmentInfo& info
    );

private:
    bool MigrateSchema();

    sqlite3* m_db;
};