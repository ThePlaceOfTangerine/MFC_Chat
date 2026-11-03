#include "Database.h"
#include <iostream>
#include <algorithm>

using namespace std;

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
		cout << "Cannot open db: " << sqlite3_errmsg(m_db) << endl;
		return false;
	}

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
        cout << "Db init failed: " << errMessage << endl;
        sqlite3_free(errMessage);
        return false;
    }

    return true;
}

bool Database::CreateUser(const char* username, const char* password, const char* displayName)
{
    const char* sql = "INSERT INTO users (username, password, display_name) VALUES (?, ?, ?);";

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
        cout << "Prepare failed: " << sqlite3_errmsg(m_db) << endl;
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
        password,
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

    cout << "SQLITE_OK = " << SQLITE_OK << endl;
    cout << "SQLITE_ROW = " << SQLITE_ROW << endl;
    cout << "SQLITE_DONE = " << SQLITE_DONE << endl;

    result = sqlite3_step(stmt);
    cout << "sqlite3_step result = " << result << endl;
    cout << "SQLITE_DONE = " << SQLITE_DONE << endl;

    if (result == SQLITE_CONSTRAINT)
    {
        cout << "Username already exists." << endl;

        sqlite3_finalize(stmt);

        return false;
    }


    if (result != SQLITE_DONE)
    {
        cout << "Insert failed: " << sqlite3_errmsg(m_db) << endl;
        return false;
    }

    sqlite3_finalize(stmt);

    return true;
}

bool Database::AuthenticateUser(const char* username, const char* password, int& userId, string& displayName)
{
    const char* sql = "SELECT id, display_name FROM users WHERE username = ? AND password = ?;";

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
        cout << "Prepare failed: " << sqlite3_errmsg(m_db) << endl;
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
        password,
        -1,
        SQLITE_TRANSIENT
    );

    result = sqlite3_step(stmt);

    if (result == SQLITE_ROW)
    {
        userId = sqlite3_column_int(stmt, 0);

        const unsigned char* name = sqlite3_column_text(stmt, 1);

        displayName = name ? reinterpret_cast<const char*>(name) : "";

        sqlite3_finalize(stmt);

        return true;
    }

    if (result == SQLITE_DONE)
    {
        cout << "Invalid username or password." << endl;

        sqlite3_finalize(stmt);

        return false;
    }

    cout << "Authentication failed: " << sqlite3_errmsg(m_db) << endl;

    sqlite3_finalize(stmt);

    return false;
}

bool Database::SaveMessage(const char* senderUsername, const char* receiverUsername, const char* content)
{
    if (m_db == nullptr ||
        senderUsername == nullptr ||
        receiverUsername == nullptr ||
        content == nullptr)
    {
        return false;
    }

    int senderId = 0;
    int receiverId = 0;

    const char* userSql = "SELECT id FROM users WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, userSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, senderUsername, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        senderId = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (senderId == 0)
        return false;

    if (sqlite3_prepare_v2(m_db, userSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, receiverUsername, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        receiverId = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (receiverId == 0 || senderId == receiverId)
        return false;

    int user1Id = min(senderId, receiverId);
    int user2Id = max(senderId, receiverId);

    const char* conversationSql = "INSERT OR IGNORE INTO conversations (user1_id, user2_id) VALUES (?, ?);";

    if (sqlite3_prepare_v2(m_db, conversationSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int(stmt, 1, user1Id);
    sqlite3_bind_int(stmt, 2, user2Id);

    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (result != SQLITE_DONE)
        return false;

    const char* findConversationSql = "SELECT id FROM conversations WHERE user1_id = ? AND user2_id = ?;";

    if (sqlite3_prepare_v2(m_db, findConversationSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int(stmt, 1, user1Id);
    sqlite3_bind_int(stmt, 2, user2Id);

    int conversationId = 0;

    if (sqlite3_step(stmt) == SQLITE_ROW)
        conversationId = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (conversationId == 0)
        return false;

    const char* messageSql = "INSERT INTO messages (conversation_id, sender_id, content) VALUES (?, ?, ?);";

    if (sqlite3_prepare_v2(m_db, messageSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_int(stmt, 1, conversationId);
    sqlite3_bind_int(stmt, 2, senderId);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);

    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

bool Database::LoadMessages(const char* username1, const char* username2, vector<MessageRecord>& messages)
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

bool Database::GetAllUsers(const char* currentUsername, vector<string>& usernames)
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


bool Database::GetConversationSummaries(const char* currentUsername, vector<ConversationSummary>& conversations)
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
