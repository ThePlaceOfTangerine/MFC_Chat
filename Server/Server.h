#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

constexpr unsigned short SERVER_PORT = 5000;
constexpr uint32_t MAX_MESSAGE_SIZE = 1024 * 1024;

// Phiên upload đang diễn ra của một client (mỗi client tối đa một phiên).
// Chỉ được truy cập từ thread xử lý client đó nên không cần mutex.
struct UploadSession
{
    bool active = false;
    FILE* file = nullptr;

    std::string uploadId;
    std::string tempPath;
    std::string fileName;
    std::string kind;

    int64_t declaredSize = 0;
    int64_t received = 0;
};

struct ClientConnection
{
    SOCKET socket = INVALID_SOCKET;

    std::mutex sendMutex;

    std::string username;

    UploadSession upload;

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