#pragma once

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>

constexpr unsigned short SERVER_PORT = 5000;
constexpr uint32_t MAX_MESSAGE_SIZE = 1024 * 1024;

struct ClientConnection
{
    SOCKET socket = INVALID_SOCKET;
    std::mutex sendMutex;
    std::string username;
};

extern std::vector<std::shared_ptr<ClientConnection>> clients;
extern std::mutex clientsMutex;
extern std::mutex consoleMutex;

bool SendAll(SOCKET s, const char* data, int length);

bool RecvExact(SOCKET s, char* buffer, int length);

bool SendFrame(
    const std::shared_ptr<ClientConnection>& client,
    const std::string& message
);

bool RecvFrame(SOCKET s, std::string& message);

bool ReadLine(
    const std::string& data,
    size_t& position,
    std::string& result
);

bool IsValidUsername(const std::string& username);

void HandleClient(
    const std::shared_ptr<ClientConnection>& client
);