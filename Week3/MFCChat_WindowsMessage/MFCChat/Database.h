#pragma once

#include "sqlite3.h"
#include <string>
#include <vector>

using namespace std;

struct MessageRecord
{
	string senderUsername;
	string content;
	string sentAt;
};

struct ConversationSummary
{
	string username;
	string lastMessage;
	string lastSentAt;
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

	bool AuthenticateUser(const char* username, const char* password, int& userId, string& displayName);

	bool GetAllUsers(const char* currentUsername, vector<string>& usernames);

	bool SaveMessage(const char* senderUsername, const char* receiverUsername, const char* content);
	bool LoadMessages(const char* senderUsername, const char* receiverUsername, vector <MessageRecord>& messages);

	bool GetConversationSummaries(const char* currentUsername, vector<ConversationSummary>& conversations);

private:
	sqlite3* m_db;

};

