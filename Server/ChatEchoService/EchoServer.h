#pragma once

#include <Windows.h>

struct EchoServerContext
{
    HANDLE stopEvent;
    HANDLE startupEvent;
    volatile LONG startupSucceeded;
    DWORD startupError;
};

DWORD WINAPI EchoServerThread(LPVOID lpParam);