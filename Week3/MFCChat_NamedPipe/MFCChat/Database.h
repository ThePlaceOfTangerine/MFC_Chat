#pragma once

#include "sqlite3.h"
#include <string>
#include <vector>

struct MessageRecord
{
	std::string senderUsername;
	std::string content;
	std::string sentAt;
};

struct ConversationSummary
{
	std::string username;
	std::string lastMessage;
	std::string lastSentAt;
};

class Database
{

public:
	Database();
	~Database();

	bool Open(const char* path);
	void Close();

	bool Initialize();

	bool CreateUser(const char* username, const char* password, const char* displayName);

	bool AuthenticateUser(const char* username, const char* password, int& userId, std::string& displayName);

	bool GetAllUsers(const char* currentUsername, std::vector<std::string>& usernames);

	bool SaveMessage(const char* senderUsername, const char* receiverUsername, const char* content);
	bool LoadMessages(const char* senderUsername, const char* receiverUsername, std::vector <MessageRecord>& messages);

	bool GetConversationSummaries(const char* currentUsername, std::vector<ConversationSummary>& conversations);

private:
	sqlite3* m_db;

};

