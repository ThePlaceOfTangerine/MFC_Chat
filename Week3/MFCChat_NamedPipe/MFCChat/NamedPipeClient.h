#pragma once

#include "NamedPipe.h"

#include <atomic>
#include <string>
#include <thread>

class NamedPipeClient
{
public:
    NamedPipeClient();
    ~NamedPipeClient();

    NamedPipeClient(const NamedPipeClient&) = delete;
    NamedPipeClient& operator=(const NamedPipeClient&) = delete;

    bool Start(HWND notifyWindow);

    bool Send(
        const std::wstring& sender,
        const std::wstring& message);

    void Stop();

    bool IsConnected() const;

private:
    void ConnectionLoop();
    void ReadLoop(HANDLE pipe);

    bool WriteMessage(
        HANDLE pipe,
        const std::wstring& payload);

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_connected{ false };

    HWND m_notifyWindow{ nullptr };

    HANDLE m_writePipe{ INVALID_HANDLE_VALUE };
    HANDLE m_readPipe{ INVALID_HANDLE_VALUE };

    std::thread m_connectionThread;
    std::thread m_readThread;
};