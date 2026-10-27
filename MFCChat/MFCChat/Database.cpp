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
        OutputDebugStringA(
            m_db ? sqlite3_errmsg(m_db) : "Cannot open database");

        if (m_db != nullptr)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }

        return false;
    }

    sqlite3_busy_timeout(m_db, 5000);

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
	const char* sql = R"(
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

            CHECK (user1_id < user2_id),
            UNIQUE (user1_id, user2_id),

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
    )";

    char* errMessage = nullptr;

    int result = sqlite3_exec(
        m_db,
        sql,
        nullptr,
        nullptr,
        &errMessage
    );

    if (result != SQLITE_OK)
    {
        std::cout << "Db init failed: " << errMessage << std::endl;
        sqlite3_free(errMessage);
        return false;
    }

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
        SELECT sender.username, messages.content, messages.sent_at
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
            COALESCE(last_message.id, 0) AS last_id
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

        conversations.push_back(item);
    }

    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}
