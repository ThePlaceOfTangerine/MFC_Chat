#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

#define WM_APP_TCP_MESSAGE (WM_APP + 102)
#define WM_APP_TCP_RESPONSE (WM_APP + 103)

struct TcpResponse
{
    std::string action;
    std::string payload;
};

struct TcpMessage
{
    int64_t messageId = 0;
    int64_t groupId = 0;

    std::string sender;
    std::string content;
    std::string sentAt;

    bool isGroup = false;
    bool offline = false;
};