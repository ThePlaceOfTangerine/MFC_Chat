#pragma once

#include <Windows.h>
#include <string>

#define WM_APP_TCP_MESSAGE (WM_APP + 102)

struct TcpMessage
{
    std::string sender;
    std::string content;
};
