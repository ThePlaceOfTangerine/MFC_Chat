#include "Database.h"
#include <Windows.h>
#include <iostream>
#include <algorithm>
#include "PasswordHash.h"

Database::Database() : m_db(nullptr)
{
}

Database::~Database()
{
    Close();
}

bool Database::Open(const char* path)
{
    int result = sqlite3_open(path, &m_db);

    if (result != SQLITE_OK)
    {
        const char* openError =
            m_db ? sqlite3_errmsg(m_db) : "Cannot open database";

        OutputDebugStringA(openError);
        std::cerr << "[Database::Open] path=" << (path ? path : "(null)")
            << " code=" << result
            << " error=" << openError << std::endl;

        if (m_db != nullptr)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }

        return false;
    }

    sqlite3_busy_timeout(m_db, 5000);

    // SQLite mặc định tắt foreign key; bật để các FOREIGN KEY có tác dụng.
    sqlite3_exec(m_db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    // WAL cho phép đọc/ghi đồng thời tốt hơn khi nhiều thread mở DB.
    sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);

    return true;
}

void Database::Close()
{
    if (m_db != nullptr)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool Database::Initialize()
{
    if (m_db == nullptr)
        return false;

    const char* schemaSql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL UNIQUE,
            password TEXT NOT NULL,
            display_name TEXT NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS conversations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user1_id INTEGER NOT NULL,
            user2_id INTEGER NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

            FOREIGN KEY (user1_id) REFERENCES users(id),
            FOREIGN KEY (user2_id) REFERENCES users(id)
        );

        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id INTEGER NOT NULL,
            sender_id INTEGER NOT NULL,
            content TEXT NOT NULL,
            sent_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            is_read INTEGER NOT NULL DEFAULT 0,

            FOREIGN KEY (conversation_id) REFERENCES conversations(id),
            FOREIGN KEY (sender_id) REFERENCES users(id)
        );

        CREATE INDEX IF NOT EXISTS idx_messages_conversation
        ON messages(conversation_id, sent_at);

        CREATE TABLE IF NOT EXISTS message_receipts (
            message_id INTEGER NOT NULL,
            recipient_id INTEGER NOT NULL,
            delivered_at DATETIME,
            read_at DATETIME,

            PRIMARY KEY (message_id, recipient_id),
            FOREIGN KEY (message_id) REFERENCES messages(id),
            FOREIGN KEY (recipient_id) REFERENCES users(id)
        );

        CREATE INDEX IF NOT EXISTS idx_receipts_pending
        ON message_receipts(recipient_id, delivered_at);

        CREATE TABLE IF NOT EXISTS chat_groups (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            group_name TEXT NOT NULL,
            created_by INTEGER NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

            FOREIGN KEY (created_by) REFERENCES users(id)
        );

        CREATE TABLE IF NOT EXISTS group_members (
            group_id INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,

            PRIMARY KEY (group_id, user_id),
            FOREIGN KEY (group_id) REFERENCES chat_groups(id),
            FOREIGN KEY (user_id) REFERENCES users(id)
        );

        CREATE INDEX IF NOT EXISTS idx_group_members_user
        ON group_members(user_id);

        CREATE TABLE IF NOT EXISTS group_messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            group_id INTEGER NOT NULL,
            sender_id INTEGER NOT NULL,
            content TEXT NOT NULL,
            sent_at DATETIME DEFAULT CURRENT_TIMESTAMP,

            FOREIGN KEY (group_id) REFERENCES chat_groups(id),
            FOREIGN KEY (sender_id) REFERENCES users(id)
        );

        CREATE INDEX IF NOT EXISTS idx_group_messages_group
        ON group_messages(group_id, id);

        CREATE TABLE IF NOT EXISTS group_message_receipts (
            message_id INTEGER NOT NULL,
            recipient_id INTEGER NOT NULL,
            delivered_at DATETIME,
            read_at DATETIME,

            PRIMARY KEY (message_id, recipient_id),
            FOREIGN KEY (message_id) REFERENCES group_messages(id),
            FOREIGN KEY (recipient_id) REFERENCES users(id)
        );

        CREATE INDEX IF NOT EXISTS idx_group_receipts_pending
        ON group_message_receipts(recipient_id, delivered_at);

        CREATE TABLE IF NOT EXISTS attachments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            uploader_id INTEGER NOT NULL,
            kind TEXT NOT NULL,
            file_name TEXT NOT NULL,
            size INTEGER NOT NULL,
            stored_name TEXT NOT NULL DEFAULT '',
            recipient_id INTEGER,
            group_id INTEGER,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

            FOREIGN KEY (uploader_id) REFERENCES users(id)
        );
    )";

    char* errMessage = nullptr;

    int result = sqlite3_exec(
        m_db,
        schemaSql,
        nullptr,
        nullptr,
        &errMessage
    );

    if (result != SQLITE_OK)
    {
        std::cout << "Db init failed: "
            << (errMessage ? errMessage : "unknown error")
            << std::endl;

        sqlite3_free(errMessage);
        return false;
    }

    return MigrateSchema();
}

// Migration chạy một lần cho mỗi phiên bản, theo dõi bằng PRAGMA user_version.
//   version 1: gộp các dòng conversations bị trùng (do thiếu UNIQUE)
//              rồi tạo UNIQUE INDEX cho (user1_id, user2_id).
//   version 2: thêm cột users.avatar_version (bảng attachments được tạo
//              trong Initialize()).
bool Database::MigrateSchema()
{
    if (m_db == nullptr)
        return false;

    const int latestVersion = 2;

    auto readVersion = [this]() -> int
    {
        int v = 0;
        sqlite3_stmt* s = nullptr;

        if (sqlite3_prepare_v2(
            m_db, "PRAGMA user_version;", -1, &s, nullptr) == SQLITE_OK)
        {
            if (sqlite3_step(s) == SQLITE_ROW)
                v = sqlite3_column_int(s, 0);
        }

        sqlite3_finalize(s);
        return v;
    };

    auto columnExists = [this](const char* table, const char* column) -> bool
    {
        std::string sql = std::string("PRAGMA table_info(") + table + ");";
        sqlite3_stmt* s = nullptr;
        bool found = false;

        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(s) == SQLITE_ROW)
            {
                const unsigned char* name = sqlite3_column_text(s, 1);

                if (name != nullptr &&
                    std::string(reinterpret_cast<const char*>(name)) == column)
                {
                    found = true;
                    break;
                }
            }
        }

        sqlite3_finalize(s);
        return found;
    };

    // Đường nhanh: đã ở phiên bản mới nhất thì không cần khóa ghi.
    if (readVersion() >= latestVersion)
        return true;

    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr)
        != SQLITE_OK)
    {
        std::cout << "Db migration: cannot lock: "
            << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    // Đọc lại trong transaction: thread khác có thể vừa migrate xong.
    const int version = readVersion();

    if (version >= latestVersion)
    {
        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
        return true;
    }

    const char* migration1Sql = R"(
        UPDATE messages
        SET conversation_id = (
            SELECT MIN(c2.id)
            FROM conversations c1
            JOIN conversations c2
                ON c2.user1_id = c1.user1_id
               AND c2.user2_id = c1.user2_id
            WHERE c1.id = messages.conversation_id
        )
        WHERE conversation_id IN (
            SELECT id FROM conversations
            WHERE id NOT IN (
                SELECT MIN(id) FROM conversations
                GROUP BY user1_id, user2_id
            )
        );

        DELETE FROM conversations
        WHERE id NOT IN (
            SELECT MIN(id) FROM conversations
            GROUP BY user1_id, user2_id
        );

        CREATE UNIQUE INDEX IF NOT EXISTS idx_conversations_pair
        ON conversations(user1_id, user2_id);
    )";

    char* errMessage = nullptr;
    int result = SQLITE_OK;

    if (version < 1)
    {
        result = sqlite3_exec(
            m_db, migration1Sql, nullptr, nullptr, &errMessage);
    }

    if (result == SQLITE_OK && version < 2)
    {
        if (!columnExists("users", "avatar_version"))
        {
            result = sqlite3_exec(
                m_db,
                "ALTER TABLE users "
                "ADD COLUMN avatar_version INTEGER NOT NULL DEFAULT 0;",
                nullptr, nullptr, &errMessage);
        }
    }

    if (result == SQLITE_OK)
    {
        result = sqlite3_exec(
            m_db, "PRAGMA user_version = 2;", nullptr, nullptr, &errMessage);
    }

    if (result != SQLITE_OK)
    {
        std::cout << "Db migration failed: "
            << (errMessage ? errMessage : "unknown error")
            << std::endl;

        sqlite3_free(errMessage);
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr)
        != SQLITE_OK)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    // Best-effort: username không phân biệt hoa/thường.
    // Nếu DB cũ đã có username trùng kiểu "Alice"/"alice" thì bước này bị bỏ qua.
    sqlite3_exec(
        m_db,
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_users_username_nocase "
        "ON users(username COLLATE NOCASE);",
        nullptr, nullptr, nullptr);

    return true;
}

bool Database::CreateUser(
    const char* username,
    const char* password,
    const char* displayName)
{
    if (m_db == nullptr ||
        username == nullptr ||
        password == nullptr ||
        displayName == nullptr ||
        username[0] == '\0' ||
        password[0] == '\0' ||
        displayName[0] == '\0')
    {
        return false;
    }

    // Hash mật khẩu trước khi lưu vào SQLite.
    std::string passwordHash;

    if (!HashPassword(password, passwordHash))
    {
        OutputDebugStringA("[CreateUser] Password hashing failed.\n");
        return false;
    }

    const char* sql =
        "INSERT INTO users (username, password, display_name) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;

    int result = sqlite3_prepare_v2(
        m_db,
        sql,
        -1,
        &stmt,
        nullptr
    );

    if (result != SQLITE_OK)
    {
        OutputDebugStringA(
            "[CreateUser] Prepare failed.\n"
        );
        return false;
    }

    sqlite3_bind_text(
        stmt,
        1,
        username,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt,
        2,
        passwordHash.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt,
        3,
        displayName,
        -1,
        SQLITE_TRANSIENT
    );

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
    {
        OutputDebugStringA(
            sqlite3_errmsg(m_db)
        );

        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);

    OutputDebugStringA(
        "[CreateUser] User created successfully.\n"
    );

    return true;
}

bool Database::AuthenticateUser(
    const char* username,
    const char* password,
    int& userId,
    std::string& displayName)
{
    if (m_db == nullptr ||
        username == nullptr ||
        password == nullptr)
    {
        return false;
    }

    userId = 0;
    displayName.clear();

    const char* sql =
        "SELECT id, password, display_name "
        "FROM users "
        "WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;

    int result = sqlite3_prepare_v2(
        m_db,
        sql,
        -1,
        &stmt,
        nullptr
    );

    if (result != SQLITE_OK)
    {
        OutputDebugStringA(
            "[AuthenticateUser] Prepare failed.\n"
        );
        return false;
    }

    sqlite3_bind_text(
        stmt,
        1,
        username,
        -1,
        SQLITE_TRANSIENT
    );

    result = sqlite3_step(stmt);

    if (result != SQLITE_ROW)
    {
        if (result != SQLITE_DONE)
        {
            OutputDebugStringA(
                "[AuthenticateUser] Query failed.\n"
            );
        }

        sqlite3_finalize(stmt);
        return false;
    }

    int foundUserId = sqlite3_column_int(stmt, 0);

    const unsigned char* storedPassword =
        sqlite3_column_text(stmt, 1);

    const unsigned char* storedDisplayName =
        sqlite3_column_text(stmt, 2);

    std::string storedPasswordString =
        storedPassword
        ? reinterpret_cast<const char*>(storedPassword)
        : "";

    std::string foundDisplayName =
        storedDisplayName
        ? reinterpret_cast<const char*>(storedDisplayName)
        : "";

    sqlite3_finalize(stmt);
    stmt = nullptr;

    // Kiểm tra mật khẩu.
    bool authenticated = false;

    if (IsPasswordHash(storedPasswordString))
    {
        // Tài khoản đã lưu mật khẩu dạng hash.
        authenticated = VerifyPassword(
            password,
            storedPasswordString
        );
    }
    else
    {
        // Tài khoản cũ: mật khẩu vẫn lưu dạng văn bản.
        authenticated = (storedPasswordString == password);

        if (authenticated)
        {
            // Đăng nhập thành công: nâng cấp sang hash.
            std::string upgradedHash;

            if (HashPassword(password, upgradedHash))
            {
                const char* updateSql =
                    "UPDATE users "
                    "SET password = ? "
                    "WHERE id = ? AND password = ?;";

                sqlite3_stmt* updateStmt = nullptr;

                int updateResult = sqlite3_prepare_v2(
                    m_db,
                    updateSql,
                    -1,
                    &updateStmt,
                    nullptr
                );

                if (updateResult == SQLITE_OK)
                {
                    sqlite3_bind_text(
                        updateStmt,
                        1,
                        upgradedHash.c_str(),
                        -1,
                        SQLITE_TRANSIENT
                    );

                    sqlite3_bind_int(
                        updateStmt,
                        2,
                        foundUserId
                    );

                    sqlite3_bind_text(
                        updateStmt,
                        3,
                        storedPasswordString.c_str(),
                        -1,
                        SQLITE_TRANSIENT
                    );

                    updateResult = sqlite3_step(updateStmt);

                    if (updateResult != SQLITE_DONE)
                    {
                        OutputDebugStringA(
                            "[AuthenticateUser] Password upgrade failed.\n"
                        );
                    }
                }

                if (updateStmt != nullptr)
                {
                    sqlite3_finalize(updateStmt);
                }
            }
        }
    }

    if (!authenticated)
    {
        OutputDebugStringA(
            "[AuthenticateUser] Invalid username or password.\n"
        );
        return false;
    }

    userId = foundUserId;
    displayName = foundDisplayName;

    OutputDebugStringA(
        "[AuthenticateUser] Login successful.\n"
    );

    return true;
}

bool Database::SaveMessage(
    const char* senderUsername,
    const char* receiverUsername,
    const char* content)
{
    auto logError = [this](const char* step, int result)
    {
        char buffer[1024];

        sprintf_s(
            buffer,
            "[SaveMessage] %s failed. Code=%d, Error=%s\n",
            step,
            result,
            m_db ? sqlite3_errmsg(m_db) : "Database is null");

        OutputDebugStringA(buffer);
    };

    if (m_db == nullptr ||
        senderUsername == nullptr ||
        receiverUsername == nullptr ||
        content == nullptr)
    {
        OutputDebugStringA("[SaveMessage] Invalid input or database.\n");
        return false;
    }

    OutputDebugStringA("[SaveMessage] Begin\n");

    int senderId = 0;
    int receiverId = 0;
    int result = SQLITE_OK;

    const char* userSql =
        "SELECT id FROM users WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;

    result = sqlite3_prepare_v2(
        m_db, userSql, -1, &stmt, nullptr);

    if (result != SQLITE_OK)
    {
        logError("Prepare sender query", result);
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, senderUsername, -1, SQLITE_TRANSIENT);

    result = sqlite3_step(stmt);

    if (result == SQLITE_ROW)
        senderId = sqlite3_column_int(stmt, 0);
    else if (result != SQLITE_DONE)
        logError("Query sender", result);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (senderId == 0)
    {
        OutputDebugStringA("[SaveMessage] Sender not found.\n");
        return false;
    }

    result = sqlite3_prepare_v2(
        m_db, userSql, -1, &stmt, nullptr);

    if (result != SQLITE_OK)
    {
        logError("Prepare receiver query", result);
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, receiverUsername, -1, SQLITE_TRANSIENT);

    result = sqlite3_step(stmt);

    if (result == SQLITE_ROW)
        receiverId = sqlite3_column_int(stmt, 0);
    else if (result != SQLITE_DONE)
        logError("Query receiver", result);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (receiverId == 0)
    {
        OutputDebugStringA("[SaveMessage] Receiver not found.\n");
        return false;
    }

    if (senderId == receiverId)
    {
        OutputDebugStringA("[SaveMessage] Same sender and receiver.\n");
        return false;
    }

    int user1Id = min(senderId, receiverId);
    int user2Id = max(senderId, receiverId);

    const char* conversationSql =
        "INSERT OR IGNORE INTO conversations (user1_id, user2_id) "
        "VALUES (?, ?);";

    result = sqlite3_prepare_v2(
        m_db, conversationSql, -1, &stmt, nullptr);

    if (result != SQLITE_OK)
    {
        logError("Prepare conversation insert", result);
        return false;
    }

    sqlite3_bind_int(stmt, 1, user1Id);
    sqlite3_bind_int(stmt, 2, user2Id);

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
        logError("Insert conversation", result);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (result != SQLITE_DONE)
        return false;

    const char* findConversationSql =
        "SELECT id FROM conversations "
        "WHERE user1_id = ? AND user2_id = ?;";

    result = sqlite3_prepare_v2(
        m_db, findConversationSql, -1, &stmt, nullptr);

    if (result != SQLITE_OK)
    {
        logError("Prepare conversation query", result);
        return false;
    }

    sqlite3_bind_int(stmt, 1, user1Id);
    sqlite3_bind_int(stmt, 2, user2Id);

    int conversationId = 0;

    result = sqlite3_step(stmt);

    if (result == SQLITE_ROW)
        conversationId = sqlite3_column_int(stmt, 0);
    else if (result != SQLITE_DONE)
        logError("Query conversation", result);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (conversationId == 0)
    {
        OutputDebugStringA("[SaveMessage] Conversation not found.\n");
        return false;
    }

    const char* messageSql =
        "INSERT INTO messages (conversation_id, sender_id, content) "
        "VALUES (?, ?, ?);";

    result = sqlite3_prepare_v2(
        m_db, messageSql, -1, &stmt, nullptr);

    if (result != SQLITE_OK)
    {
        logError("Prepare message insert", result);
        return false;
    }

    sqlite3_bind_int(stmt, 1, conversationId);
    sqlite3_bind_int(stmt, 2, senderId);
    sqlite3_bind_text(
        stmt, 3, content, -1, SQLITE_TRANSIENT);

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
        logError("Insert message", result);

    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE)
        return false;

    OutputDebugStringA("[SaveMessage] Success\n");
    return true;
}

bool Database::LoadMessages(const char* username1, const char* username2, std::vector<MessageRecord>& messages)
{
    messages.clear();

    if (m_db == nullptr ||
        username1 == nullptr ||
        username2 == nullptr)
    {
        return false;
    }

    const char* sql = R"(
        SELECT sender.username, messages.content, messages.sent_at, messages.id
        FROM messages
        JOIN conversations
            ON messages.conversation_id = conversations.id
        JOIN users AS sender
            ON messages.sender_id = sender.id
        JOIN users AS u1
            ON conversations.user1_id = u1.id
        JOIN users AS u2
            ON conversations.user2_id = u2.id
        WHERE
            (u1.username = ? AND u2.username = ?)
            OR
            (u1.username = ? AND u2.username = ?)
        ORDER BY messages.id ASC;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username1, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username2, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, username2, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, username1, -1, SQLITE_TRANSIENT);

    int result;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        MessageRecord message;

        const unsigned char* sender = sqlite3_column_text(stmt, 0);
        const unsigned char* content = sqlite3_column_text(stmt, 1);
        const unsigned char* sentAt = sqlite3_column_text(stmt, 2);

        message.messageId = sqlite3_column_int64(stmt, 3);

        message.senderUsername = sender ? reinterpret_cast<const char*>(sender) : "";

        message.content = content ? reinterpret_cast<const char*>(content) : "";

        message.sentAt = sentAt ? reinterpret_cast<const char*>(sentAt) : "";

        messages.push_back(message);
    }

    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

bool Database::GetAllUsers(const char* currentUsername, std::vector<std::string>& usernames)
{
    usernames.clear();

    if (m_db == nullptr || currentUsername == nullptr)
        return false;

    const char* sql =
        "SELECT username FROM users "
        "WHERE username != ? "
        "ORDER BY username COLLATE NOCASE;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, currentUsername, -1, SQLITE_TRANSIENT);

    int result;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const unsigned char* username = sqlite3_column_text(stmt, 0);

        if (username != nullptr)
        {
            usernames.push_back(reinterpret_cast<const char*>(username));
        }
    }

    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}


bool Database::GetConversationSummaries(const char* currentUsername, std::vector<ConversationSummary>& conversations)
{
    conversations.clear();

    if (m_db == nullptr || currentUsername == nullptr)
        return false;

    const char* sql = R"(
        SELECT
            other.username,
            COALESCE(last_message.content, ''),
            COALESCE(last_message.sent_at, ''),
            COALESCE(last_message.id, 0) AS last_id,
            other.avatar_version
        FROM users AS current_user
        JOIN users AS other
            ON other.id != current_user.id
        LEFT JOIN conversations AS c
            ON (
                c.user1_id = current_user.id
                AND c.user2_id = other.id
            )
            OR (
                c.user2_id = current_user.id
                AND c.user1_id = other.id
            )
        LEFT JOIN messages AS last_message
            ON last_message.id = (
                SELECT m.id
                FROM messages AS m
                WHERE m.conversation_id = c.id
                ORDER BY m.id DESC
                LIMIT 1
            )
        WHERE current_user.username = ?
        ORDER BY last_id DESC, other.username COLLATE NOCASE ASC;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, currentUsername, -1, SQLITE_TRANSIENT);

    int result;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        ConversationSummary item;

        const unsigned char* username = sqlite3_column_text(stmt, 0);
        const unsigned char* content = sqlite3_column_text(stmt, 1);
        const unsigned char* sentAt = sqlite3_column_text(stmt, 2);

        item.username = username ? reinterpret_cast<const char*>(username) : "";

        item.lastMessage = content ? reinterpret_cast<const char*>(content) : "";

        item.lastSentAt = sentAt ? reinterpret_cast<const char*>(sentAt) : "";

        item.avatarVersion = sqlite3_column_int(stmt, 4);

        conversations.push_back(item);
    }

    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

bool Database::SaveDirectMessage(
    const std::string& senderUsername,
    const std::string& receiverUsername,
    const std::string& content,
    int64_t& messageId)
{
    messageId = 0;

    if (m_db == nullptr ||
        senderUsername.empty() ||
        receiverUsername.empty() ||
        senderUsername == receiverUsername ||
        content.empty())
    {
        return false;
    }

    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr)
        != SQLITE_OK)
    {
        return false;
    }

    bool success = false;
    sqlite3_stmt* stmt = nullptr;
    int senderId = 0;
    int receiverId = 0;
    int conversationId = 0;

    const char* findUserSql =
        "SELECT id FROM users WHERE username = ?;";

    const char* insertConversationSql = nullptr;
    const char* findConversationSql = nullptr;
    const char* insertMessageSql = nullptr;
    const char* insertReceiptSql = nullptr;

    if (sqlite3_prepare_v2(
        m_db, findUserSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        goto cleanup;
    }

    sqlite3_bind_text(
        stmt, 1, senderUsername.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW)
        goto cleanup;

    senderId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, findUserSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        goto cleanup;
    }

    sqlite3_bind_text(
        stmt, 1, receiverUsername.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW)
        goto cleanup;

    receiverId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (senderId < receiverId)
    {
        insertConversationSql =
            "INSERT OR IGNORE INTO conversations "
            "(user1_id, user2_id) VALUES (?, ?);";

        if (sqlite3_prepare_v2(
            m_db, insertConversationSql, -1, &stmt, nullptr)
            != SQLITE_OK)
        {
            goto cleanup;
        }

        sqlite3_bind_int(stmt, 1, senderId);
        sqlite3_bind_int(stmt, 2, receiverId);
    }
    else
    {
        insertConversationSql =
            "INSERT OR IGNORE INTO conversations "
            "(user1_id, user2_id) VALUES (?, ?);";

        if (sqlite3_prepare_v2(
            m_db, insertConversationSql, -1, &stmt, nullptr)
            != SQLITE_OK)
        {
            goto cleanup;
        }

        sqlite3_bind_int(stmt, 1, receiverId);
        sqlite3_bind_int(stmt, 2, senderId);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE)
        goto cleanup;

    sqlite3_finalize(stmt);
    stmt = nullptr;

    findConversationSql =
        "SELECT id FROM conversations "
        "WHERE user1_id = ? AND user2_id = ?;";

    if (sqlite3_prepare_v2(
        m_db, findConversationSql, -1, &stmt, nullptr)
        != SQLITE_OK)
    {
        goto cleanup;
    }

    sqlite3_bind_int(stmt, 1, (senderId < receiverId) ? senderId : receiverId);
    sqlite3_bind_int(stmt, 2, (senderId < receiverId) ? receiverId : senderId);

    if (sqlite3_step(stmt) != SQLITE_ROW)
        goto cleanup;

    conversationId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = nullptr;

    insertMessageSql =
        "INSERT INTO messages "
        "(conversation_id, sender_id, content) "
        "VALUES (?, ?, ?);";

    if (sqlite3_prepare_v2(
        m_db, insertMessageSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        goto cleanup;
    }

    sqlite3_bind_int(stmt, 1, conversationId);
    sqlite3_bind_int(stmt, 2, senderId);
    sqlite3_bind_text(
        stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        goto cleanup;

    messageId = sqlite3_last_insert_rowid(m_db);
    sqlite3_finalize(stmt);
    stmt = nullptr;

    insertReceiptSql =
        "INSERT INTO message_receipts(message_id, recipient_id) "
        "VALUES (?, ?);";

    if (sqlite3_prepare_v2(
        m_db, insertReceiptSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        goto cleanup;
    }

    sqlite3_bind_int64(stmt, 1, messageId);
    sqlite3_bind_int(stmt, 2, receiverId);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        goto cleanup;

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr)
        != SQLITE_OK)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    success = true;

cleanup:
    if (stmt != nullptr)
        sqlite3_finalize(stmt);

    if (!success)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        messageId = 0;
    }

    return success;
}

bool Database::GetPendingMessages(
    const std::string& username,
    std::vector<PendingMessage>& messages)
{
    messages.clear();

    if (m_db == nullptr || username.empty())
        return false;

    const char* sql = R"(
        SELECT
            m.id,
            sender.username,
            receiver.username,
            m.content,
            m.sent_at
        FROM message_receipts r
        JOIN messages m ON m.id = r.message_id
        JOIN users receiver ON receiver.id = r.recipient_id
        JOIN users sender ON sender.id = m.sender_id
        WHERE receiver.username = ?
          AND r.delivered_at IS NULL
        ORDER BY m.id ASC;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    int result = SQLITE_OK;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        PendingMessage message;

        message.messageId = sqlite3_column_int64(stmt, 0);

        const unsigned char* sender = sqlite3_column_text(stmt, 1);
        const unsigned char* receiver = sqlite3_column_text(stmt, 2);
        const unsigned char* content = sqlite3_column_text(stmt, 3);
        const unsigned char* sentAt = sqlite3_column_text(stmt, 4);

        message.senderUsername = sender
            ? reinterpret_cast<const char*>(sender) : "";

        message.recipientUsername = receiver
            ? reinterpret_cast<const char*>(receiver) : "";

        message.content = content
            ? reinterpret_cast<const char*>(content) : "";

        message.sentAt = sentAt
            ? reinterpret_cast<const char*>(sentAt) : "";

        messages.push_back(message);
    }

    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

bool Database::MarkMessageDelivered(
    int64_t messageId,
    const std::string& recipientUsername)
{
    if (m_db == nullptr || messageId <= 0 || recipientUsername.empty())
        return false;

    const char* sql = R"(
        UPDATE message_receipts
        SET delivered_at = COALESCE(delivered_at, CURRENT_TIMESTAMP)
        WHERE message_id = ?
          AND recipient_id = (
              SELECT id FROM users WHERE username = ?
          );
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, messageId);

    sqlite3_bind_text(
        stmt, 2, recipientUsername.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE
        && sqlite3_changes(m_db) > 0;

    sqlite3_finalize(stmt);
    return success;
}

bool Database::MarkMessageRead(
    int64_t messageId,
    const std::string& recipientUsername)
{
    if (m_db == nullptr || messageId <= 0 || recipientUsername.empty())
        return false;

    const char* sql = R"(
        UPDATE message_receipts
        SET delivered_at = COALESCE(delivered_at, CURRENT_TIMESTAMP),
            read_at = CURRENT_TIMESTAMP
        WHERE message_id = ?
          AND recipient_id = (
              SELECT id FROM users WHERE username = ?
          );
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, messageId);

    sqlite3_bind_text(
        stmt, 2, recipientUsername.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE
        && sqlite3_changes(m_db) > 0;

    sqlite3_finalize(stmt);
    return success;
}

bool Database::CreateGroup(
    const std::string& groupName,
    const std::string& creatorUsername,
    const std::vector<std::string>& memberUsernames,
    int64_t& groupId)
{
    groupId = 0;

    if (m_db == nullptr ||
        groupName.empty() ||
        groupName.size() > 128 ||
        creatorUsername.empty())
    {
        return false;
    }

    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr)
        != SQLITE_OK)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    bool success = false;
    int creatorId = 0;

    const char* findUserSql =
        "SELECT id FROM users WHERE username = ?;";

    if (sqlite3_prepare_v2(
        m_db, findUserSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        goto group_cleanup;
    }

    sqlite3_bind_text(
        stmt, 1, creatorUsername.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW)
        goto group_cleanup;

    creatorId = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "INSERT INTO chat_groups(group_name, created_by) VALUES (?, ?);",
        -1,
        &stmt,
        nullptr) != SQLITE_OK)
    {
        goto group_cleanup;
    }

    sqlite3_bind_text(
        stmt, 1, groupName.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_int(stmt, 2, creatorId);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        goto group_cleanup;

    groupId = sqlite3_last_insert_rowid(m_db);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "INSERT OR IGNORE INTO group_members(group_id, user_id) "
        "VALUES (?, ?);",
        -1,
        &stmt,
        nullptr) != SQLITE_OK)
    {
        goto group_cleanup;
    }

    sqlite3_bind_int64(stmt, 1, groupId);
    sqlite3_bind_int(stmt, 2, creatorId);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        goto group_cleanup;

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    for (const std::string& member : memberUsernames)
    {
        if (member.empty())
            continue;

        sqlite3_stmt* findStmt = nullptr;

        if (sqlite3_prepare_v2(
            m_db, findUserSql, -1, &findStmt, nullptr) != SQLITE_OK)
        {
            goto group_cleanup;
        }

        sqlite3_bind_text(
            findStmt, 1, member.c_str(), -1, SQLITE_TRANSIENT);

        int result = sqlite3_step(findStmt);

        if (result == SQLITE_ROW)
        {
            int userId = sqlite3_column_int(findStmt, 0);

            sqlite3_finalize(findStmt);
            findStmt = nullptr;

            sqlite3_bind_int64(stmt, 1, groupId);
            sqlite3_bind_int(stmt, 2, userId);

            if (sqlite3_step(stmt) != SQLITE_DONE)
                goto group_cleanup;

            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
        else
        {
            sqlite3_finalize(findStmt);
            findStmt = nullptr;

            if (result != SQLITE_DONE)
                goto group_cleanup;
        }
    }

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr)
        != SQLITE_OK)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        groupId = 0;
        return false;
    }

    success = true;

group_cleanup:
    if (stmt != nullptr)
        sqlite3_finalize(stmt);

    if (!success)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        groupId = 0;
    }

    return success;
}

bool Database::IsGroupMember(
    int64_t groupId,
    const std::string& username)
{
    if (m_db == nullptr || groupId <= 0 || username.empty())
        return false;

    const char* sql = R"(
        SELECT 1
        FROM group_members gm
        JOIN users u ON u.id = gm.user_id
        WHERE gm.group_id = ? AND u.username = ?
        LIMIT 1;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, groupId);

    sqlite3_bind_text(
        stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);

    bool found = sqlite3_step(stmt) == SQLITE_ROW;

    sqlite3_finalize(stmt);
    return found;
}

bool Database::GetGroupMembers(
    int64_t groupId,
    std::vector<std::string>& usernames)
{
    usernames.clear();

    if (m_db == nullptr || groupId <= 0)
        return false;

    const char* sql = R"(
        SELECT u.username
        FROM group_members gm
        JOIN users u ON u.id = gm.user_id
        WHERE gm.group_id = ?
        ORDER BY u.username;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, groupId);

    int result = SQLITE_OK;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const unsigned char* value = sqlite3_column_text(stmt, 0);

        usernames.push_back(
            value ? reinterpret_cast<const char*>(value) : "");
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool Database::SaveGroupMessage(
    int64_t groupId,
    const std::string& senderUsername,
    const std::string& content,
    int64_t& messageId)
{
    messageId = 0;

    if (m_db == nullptr ||
        groupId <= 0 ||
        senderUsername.empty() ||
        content.empty())
    {
        return false;
    }

    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr)
        != SQLITE_OK)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    bool success = false;
    int senderId = 0;

    const char* findUserSql =
        "SELECT id FROM users WHERE username = ?;";

    const char* receiptsSql = R"(
        INSERT INTO group_message_receipts(message_id, recipient_id)
        SELECT ?, user_id
        FROM group_members
        WHERE group_id = ? AND user_id != ?;
    )";

    if (sqlite3_prepare_v2(
        m_db, findUserSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        goto group_message_cleanup;
    }

    sqlite3_bind_text(
        stmt, 1, senderUsername.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW)
        goto group_message_cleanup;

    senderId = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "SELECT 1 FROM group_members WHERE group_id = ? AND user_id = ?;",
        -1,
        &stmt,
        nullptr) != SQLITE_OK)
    {
        goto group_message_cleanup;
    }

    sqlite3_bind_int64(stmt, 1, groupId);
    sqlite3_bind_int(stmt, 2, senderId);

    if (sqlite3_step(stmt) != SQLITE_ROW)
        goto group_message_cleanup;

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "INSERT INTO group_messages(group_id, sender_id, content) "
        "VALUES (?, ?, ?);",
        -1,
        &stmt,
        nullptr) != SQLITE_OK)
    {
        goto group_message_cleanup;
    }

    sqlite3_bind_int64(stmt, 1, groupId);
    sqlite3_bind_int(stmt, 2, senderId);

    sqlite3_bind_text(
        stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        goto group_message_cleanup;

    messageId = sqlite3_last_insert_rowid(m_db);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, receiptsSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        goto group_message_cleanup;
    }

    sqlite3_bind_int64(stmt, 1, messageId);
    sqlite3_bind_int64(stmt, 2, groupId);
    sqlite3_bind_int(stmt, 3, senderId);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        goto group_message_cleanup;

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr)
        != SQLITE_OK)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        messageId = 0;
        return false;
    }

    success = true;

group_message_cleanup:
    if (stmt != nullptr)
        sqlite3_finalize(stmt);

    if (!success)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        messageId = 0;
    }

    return success;
}

bool Database::GetUserGroups(
    const std::string& username,
    std::vector<GroupSummary>& groups)
{
    groups.clear();

    if (m_db == nullptr || username.empty())
        return false;

    const char* sql = R"(
        SELECT
            g.id,
            g.group_name,
            COALESCE((
                SELECT gm.content
                FROM group_messages gm
                WHERE gm.group_id = g.id
                ORDER BY gm.id DESC
                LIMIT 1
            ), ''),
            COALESCE((
                SELECT gm.sent_at
                FROM group_messages gm
                WHERE gm.group_id = g.id
                ORDER BY gm.id DESC
                LIMIT 1
            ), '')
        FROM chat_groups g
        JOIN group_members mine ON mine.group_id = g.id
        JOIN users u ON u.id = mine.user_id
        WHERE u.username = ?
        ORDER BY g.id DESC;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    int result = SQLITE_OK;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        GroupSummary item;

        item.groupId = sqlite3_column_int64(stmt, 0);

        const unsigned char* name = sqlite3_column_text(stmt, 1);
        const unsigned char* lastMessage = sqlite3_column_text(stmt, 2);
        const unsigned char* lastSentAt = sqlite3_column_text(stmt, 3);

        item.groupName = name
            ? reinterpret_cast<const char*>(name) : "";

        item.lastMessage = lastMessage
            ? reinterpret_cast<const char*>(lastMessage) : "";

        item.lastSentAt = lastSentAt
            ? reinterpret_cast<const char*>(lastSentAt) : "";

        groups.push_back(item);
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool Database::MarkGroupMessageDelivered(
    int64_t messageId,
    const std::string& recipientUsername)
{
    if (m_db == nullptr || messageId <= 0 || recipientUsername.empty())
        return false;

    const char* sql = R"(
        UPDATE group_message_receipts
        SET delivered_at = COALESCE(delivered_at, CURRENT_TIMESTAMP)
        WHERE message_id = ?
          AND recipient_id = (
              SELECT id FROM users WHERE username = ?
          );
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, messageId);

    sqlite3_bind_text(
        stmt, 2, recipientUsername.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE
        && sqlite3_changes(m_db) > 0;

    sqlite3_finalize(stmt);
    return success;
}

bool Database::MarkGroupMessageRead(
    int64_t messageId,
    const std::string& recipientUsername)
{
    if (m_db == nullptr || messageId <= 0 || recipientUsername.empty())
        return false;

    const char* sql = R"(
        UPDATE group_message_receipts
        SET delivered_at = COALESCE(delivered_at, CURRENT_TIMESTAMP),
            read_at = CURRENT_TIMESTAMP
        WHERE message_id = ?
          AND recipient_id = (
              SELECT id FROM users WHERE username = ?
          );
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, messageId);

    sqlite3_bind_text(
        stmt, 2, recipientUsername.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE
        && sqlite3_changes(m_db) > 0;

    sqlite3_finalize(stmt);
    return success;
}

bool Database::GetPendingGroupMessages(
    const std::string& username,
    std::vector<PendingMessage>& messages)
{
    messages.clear();

    if (m_db == nullptr || username.empty())
        return false;

    const char* sql = R"(
        SELECT
            gm.group_id,
            gm.id,
            sender.username,
            recipient.username,
            gm.content,
            gm.sent_at
        FROM group_message_receipts r
        JOIN group_messages gm ON gm.id = r.message_id
        JOIN users recipient ON recipient.id = r.recipient_id
        JOIN users sender ON sender.id = gm.sender_id
        WHERE recipient.username = ?
          AND r.delivered_at IS NULL
        ORDER BY gm.id ASC;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(
        stmt,
        1,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    int result = SQLITE_OK;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        PendingMessage message;

        message.groupId = sqlite3_column_int64(stmt, 0);
        message.messageId = sqlite3_column_int64(stmt, 1);

        const unsigned char* sender =
            sqlite3_column_text(stmt, 2);

        const unsigned char* recipient =
            sqlite3_column_text(stmt, 3);

        const unsigned char* content =
            sqlite3_column_text(stmt, 4);

        const unsigned char* sentAt =
            sqlite3_column_text(stmt, 5);

        message.senderUsername = sender
            ? reinterpret_cast<const char*>(sender) : "";

        message.recipientUsername = recipient
            ? reinterpret_cast<const char*>(recipient) : "";

        message.content = content
            ? reinterpret_cast<const char*>(content) : "";

        message.sentAt = sentAt
            ? reinterpret_cast<const char*>(sentAt) : "";

        messages.push_back(message);
    }

    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

bool Database::LoadGroupMessages(
    int64_t groupId,
    const std::string& username,
    std::vector<MessageRecord>& messages)
{
    messages.clear();

    if (m_db == nullptr ||
        groupId <= 0 ||
        !IsGroupMember(groupId, username))
    {
        return false;
    }

    const char* sql = R"(
        SELECT gm.id, u.username, gm.content, gm.sent_at
        FROM group_messages gm
        JOIN users u ON u.id = gm.sender_id
        WHERE gm.group_id = ?
        ORDER BY gm.id ASC;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, groupId);

    int result = SQLITE_OK;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        MessageRecord message;

        message.messageId = sqlite3_column_int64(stmt, 0);

        const unsigned char* sender = sqlite3_column_text(stmt, 1);
        const unsigned char* content = sqlite3_column_text(stmt, 2);
        const unsigned char* sentAt = sqlite3_column_text(stmt, 3);

        message.senderUsername = sender
            ? reinterpret_cast<const char*>(sender) : "";

        message.content = content
            ? reinterpret_cast<const char*>(content) : "";

        message.sentAt = sentAt
            ? reinterpret_cast<const char*>(sentAt) : "";

        message.isGroupMessage = true;
        message.groupId = groupId;

        messages.push_back(message);
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

// ================= Avatar & tệp đính kèm =================

namespace
{
    std::string ColumnString(sqlite3_stmt* stmt, int column)
    {
        const unsigned char* text = sqlite3_column_text(stmt, column);
        return text ? reinterpret_cast<const char*>(text) : std::string();
    }

    void ReadAttachmentRow(sqlite3_stmt* stmt, AttachmentInfo& info)
    {
        info.id = sqlite3_column_int64(stmt, 0);
        info.kind = ColumnString(stmt, 1);
        info.fileName = ColumnString(stmt, 2);
        info.size = sqlite3_column_int64(stmt, 3);
        info.storedName = ColumnString(stmt, 4);
    }

    bool LoadAttachmentById(
        sqlite3* db,
        int64_t attachmentId,
        AttachmentInfo& info)
    {
        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(
            db,
            "SELECT id, kind, file_name, size, stored_name "
            "FROM attachments WHERE id = ?;",
            -1, &stmt, nullptr) != SQLITE_OK)
        {
            return false;
        }

        sqlite3_bind_int64(stmt, 1, attachmentId);

        const bool found = sqlite3_step(stmt) == SQLITE_ROW;

        if (found)
            ReadAttachmentRow(stmt, info);

        sqlite3_finalize(stmt);
        return found;
    }
}

bool Database::GetUserId(const std::string& username, int64_t& userId)
{
    userId = 0;

    if (m_db == nullptr || username.empty())
        return false;

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "SELECT id FROM users WHERE username = ?;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    const bool found = sqlite3_step(stmt) == SQLITE_ROW;

    if (found)
        userId = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);
    return found;
}

bool Database::GetAvatarVersion(const std::string& username, int& version)
{
    version = 0;

    if (m_db == nullptr || username.empty())
        return false;

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "SELECT avatar_version FROM users WHERE username = ?;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    const bool found = sqlite3_step(stmt) == SQLITE_ROW;

    if (found)
        version = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return found;
}

bool Database::BumpAvatarVersion(const std::string& username, int& newVersion)
{
    newVersion = 0;

    if (m_db == nullptr || username.empty())
        return false;

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "UPDATE users SET avatar_version = avatar_version + 1 "
        "WHERE username = ?;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    const bool updated =
        sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(m_db) > 0;

    sqlite3_finalize(stmt);

    return updated && GetAvatarVersion(username, newVersion);
}

bool Database::CreateAttachment(
    const std::string& uploaderUsername,
    const std::string& kind,
    const std::string& fileName,
    int64_t size,
    int64_t& attachmentId)
{
    attachmentId = 0;

    if (m_db == nullptr ||
        uploaderUsername.empty() ||
        kind.empty() ||
        fileName.empty() ||
        size <= 0)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "INSERT INTO attachments (uploader_id, kind, file_name, size) "
        "VALUES ((SELECT id FROM users WHERE username = ?), ?, ?, ?);",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uploaderUsername.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, fileName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, size);

    const bool inserted = sqlite3_step(stmt) == SQLITE_DONE;

    sqlite3_finalize(stmt);

    if (!inserted)
        return false;

    attachmentId = sqlite3_last_insert_rowid(m_db);
    return true;
}

bool Database::SetAttachmentStoredName(
    int64_t attachmentId,
    const std::string& storedName)
{
    if (m_db == nullptr || attachmentId <= 0 || storedName.empty())
        return false;

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "UPDATE attachments SET stored_name = ? WHERE id = ?;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, storedName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, attachmentId);

    const bool updated =
        sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(m_db) > 0;

    sqlite3_finalize(stmt);
    return updated;
}

bool Database::DeleteAttachment(int64_t attachmentId)
{
    if (m_db == nullptr || attachmentId <= 0)
        return false;

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "DELETE FROM attachments WHERE id = ?;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, attachmentId);

    const bool deleted = sqlite3_step(stmt) == SQLITE_DONE;

    sqlite3_finalize(stmt);
    return deleted;
}

bool Database::BindAttachmentToUser(
    int64_t attachmentId,
    const std::string& senderUsername,
    const std::string& recipientUsername,
    AttachmentInfo& info)
{
    if (m_db == nullptr ||
        attachmentId <= 0 ||
        senderUsername.empty() ||
        recipientUsername.empty() ||
        senderUsername == recipientUsername)
    {
        return false;
    }

    int64_t senderId = 0;
    int64_t recipientId = 0;

    if (!GetUserId(senderUsername, senderId) ||
        !GetUserId(recipientUsername, recipientId))
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;

    // Chỉ gắn được tệp của chính mình và chưa gắn vào tin nhắn nào.
    if (sqlite3_prepare_v2(
        m_db,
        "UPDATE attachments SET recipient_id = ? "
        "WHERE id = ? AND uploader_id = ? AND stored_name != '' "
        "AND recipient_id IS NULL AND group_id IS NULL;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, recipientId);
    sqlite3_bind_int64(stmt, 2, attachmentId);
    sqlite3_bind_int64(stmt, 3, senderId);

    const bool bound =
        sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(m_db) == 1;

    sqlite3_finalize(stmt);

    return bound && LoadAttachmentById(m_db, attachmentId, info);
}

bool Database::BindAttachmentToGroup(
    int64_t attachmentId,
    const std::string& senderUsername,
    int64_t groupId,
    AttachmentInfo& info)
{
    if (m_db == nullptr ||
        attachmentId <= 0 ||
        groupId <= 0 ||
        senderUsername.empty() ||
        !IsGroupMember(groupId, senderUsername))
    {
        return false;
    }

    int64_t senderId = 0;

    if (!GetUserId(senderUsername, senderId))
        return false;

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
        m_db,
        "UPDATE attachments SET group_id = ? "
        "WHERE id = ? AND uploader_id = ? AND stored_name != '' "
        "AND recipient_id IS NULL AND group_id IS NULL;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, groupId);
    sqlite3_bind_int64(stmt, 2, attachmentId);
    sqlite3_bind_int64(stmt, 3, senderId);

    const bool bound =
        sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(m_db) == 1;

    sqlite3_finalize(stmt);

    return bound && LoadAttachmentById(m_db, attachmentId, info);
}

bool Database::GetAttachmentForUser(
    int64_t attachmentId,
    const std::string& username,
    AttachmentInfo& info)
{
    if (m_db == nullptr || attachmentId <= 0 || username.empty())
        return false;

    const char* sql = R"(
        SELECT a.id, a.kind, a.file_name, a.size, a.stored_name
        FROM attachments a
        JOIN users u ON u.username = ?
        WHERE a.id = ?
          AND a.stored_name != ''
          AND (
              a.uploader_id = u.id
              OR a.recipient_id = u.id
              OR EXISTS (
                  SELECT 1 FROM group_members gm
                  WHERE gm.group_id = a.group_id AND gm.user_id = u.id
              )
          );
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, attachmentId);

    const bool found = sqlite3_step(stmt) == SQLITE_ROW;

    if (found)
        ReadAttachmentRow(stmt, info);

    sqlite3_finalize(stmt);
    return found;
}

bool Database::GetOwnedAttachment(
    int64_t attachmentId,
    const std::string& username,
    AttachmentInfo& info)
{
    if (m_db == nullptr || attachmentId <= 0 || username.empty())
        return false;

    const char* sql = R"(
        SELECT a.id, a.kind, a.file_name, a.size, a.stored_name
        FROM attachments a
        JOIN users u ON u.username = ?
        WHERE a.id = ? AND a.uploader_id = u.id AND a.stored_name != '';
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, attachmentId);

    const bool found = sqlite3_step(stmt) == SQLITE_ROW;

    if (found)
        ReadAttachmentRow(stmt, info);

    sqlite3_finalize(stmt);
    return found;
}