#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

constexpr unsigned short SERVER_PORT = 5000;
constexpr uint32_t MAX_MESSAGE_SIZE = 1024 * 1024;

struct ClientConnection
{
    SOCKET socket = INVALID_SOCKET;

    std::mutex sendMutex;

    std::string username;

    std::atomic<bool> authenticated{ false };
    std::atomic<bool> closing{ false };
};

extern std::vector<std::shared_ptr<ClientConnection>> clients;

extern std::mutex clientsMutex;
extern std::mutex consoleMutex;

bool SendAll(
    SOCKET socketHandle,
    const char* data,
    int length
);

bool RecvExact(
    SOCKET socketHandle,
    char* buffer,
    int length
);

bool SendFrame(
    const std::shared_ptr<ClientConnection>& client,
    const std::string& message
);

bool RecvFrame(
    SOCKET socketHandle,
    std::string& message
);

bool IsValidUsername(
    const std::string& username
);

void HandleClient(
    const std::shared_ptr<ClientConnection>& client
);