#include "pch.h"
#include "NamedPipeServer.h"

#include <vector>
#include <cstring>

NamedPipeServer::NamedPipeServer() = default;

NamedPipeServer::~NamedPipeServer()
{
    Stop();
}

bool NamedPipeServer::Start(HWND notifyWindow)
{
    if (notifyWindow == nullptr || m_running)
        return false;

    m_notifyWindow = notifyWindow;
    m_running = true;

    try
    {
        m_connectionThread =
            std::thread(
                &NamedPipeServer::ConnectionLoop,
                this);
    }
    catch (...)
    {
        m_running = false;
        return false;
    }

    return true;
}

bool NamedPipeServer::IsConnected() const
{
    return m_connected.load();
}

void NamedPipeServer::ConnectionLoop()
{
    HANDLE writePipe = CreateNamedPipeW(
        NamedPipe::SERVER_TO_CLIENT,
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        NamedPipe::BUFFER_SIZE,
        NamedPipe::BUFFER_SIZE,
        0,
        nullptr
    );

    if (writePipe == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        wchar_t log[128]{};
        swprintf_s(log, L"[PIPE SERVER] Create write pipe failed: %lu\n", error);
        OutputDebugStringW(log);
        m_running = false;
        return;
    }

    HANDLE readPipe = CreateNamedPipeW(
        NamedPipe::CLIENT_TO_SERVER,
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        NamedPipe::BUFFER_SIZE,
        NamedPipe::BUFFER_SIZE,
        0,
        nullptr
    );

    if (readPipe == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        wchar_t log[128]{};
        swprintf_s(log, L"[PIPE SERVER] Create read pipe failed: %lu\n", error);
        OutputDebugStringW(log);
        CloseHandle(writePipe);
        m_running = false;
        return;
    }

    m_writePipe = writePipe;
    m_readPipe = readPipe;

    OutputDebugStringW(
        L"[PIPE SERVER] Waiting for client write connection\n"
    );

    BOOL connected = ConnectNamedPipe(writePipe, nullptr);

    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        DWORD error = GetLastError();
        wchar_t log[128]{};
        swprintf_s(log, L"[PIPE SERVER] Connect write pipe failed: %lu\n", error);
        OutputDebugStringW(log);
        CloseHandle(writePipe);
        CloseHandle(readPipe);
        m_writePipe = INVALID_HANDLE_VALUE;
        m_readPipe = INVALID_HANDLE_VALUE;
        m_running = false;
        return;
    }

    if (!m_running)
        return;

    OutputDebugStringW(
        L"[PIPE SERVER] Client connected to ServerToClient\n"
    );

    OutputDebugStringW(
        L"[PIPE SERVER] Waiting for client read connection\n"
    );

    connected = ConnectNamedPipe(readPipe, nullptr);

    if (!connected)
    {
        DWORD error = GetLastError();

        if (error == ERROR_PIPE_CONNECTED)
        {
            connected = TRUE;
        }
        else if (error == ERROR_NO_DATA)
        {
            OutputDebugStringW(
                L"[PIPE SERVER] Client disconnected before read pipe connected\n"
            );

            DisconnectNamedPipe(readPipe);
            m_connected = false;
            m_running = false;

            CloseHandle(writePipe);
            CloseHandle(readPipe);

            m_writePipe = INVALID_HANDLE_VALUE;
            m_readPipe = INVALID_HANDLE_VALUE;

            return;
        }
        else
        {
            wchar_t log[128]{};

            swprintf_s(
                log,
                L"[PIPE SERVER] Connect read pipe failed: %lu\n",
                error
            );

            OutputDebugStringW(log);

            m_connected = false;
            m_running = false;

            CloseHandle(writePipe);
            CloseHandle(readPipe);

            m_writePipe = INVALID_HANDLE_VALUE;
            m_readPipe = INVALID_HANDLE_VALUE;

            return;
        }
    }

    if (!m_running)
        return;

    OutputDebugStringW(
        L"[PIPE SERVER] Client connected to ClientToServer\n"
    );

    m_connected = true;

    OutputDebugStringW(
        L"[PIPE SERVER] Chat connected\n"
    );

    try
    {
        m_readThread = std::thread(
            &NamedPipeServer::ReadLoop,
            this,
            readPipe
        );
    }
    catch (...)
    {
        m_connected = false;
        m_running = false;
    }
}

bool NamedPipeServer::Send(
    const std::wstring & sender,
    const std::wstring & message)
{
    if (!m_running ||
        !m_connected ||
        sender.empty() ||
        message.empty())
    {
        return false;
    }

    std::wstring payload =
        sender + L"|" + message;

    if (payload.size() * sizeof(wchar_t) >
        NamedPipe::MAX_MESSAGE_BYTES)
    {
        return false;
    }

    return WriteMessage(
        m_writePipe,
        payload);
}

bool NamedPipeServer::WriteMessage(
    HANDLE pipe,
    const std::wstring& payload)
{
    if (pipe == INVALID_HANDLE_VALUE || payload.empty())
        return false;

    DWORD bytesToWrite = static_cast<DWORD>(
        payload.size() * sizeof(wchar_t));

    DWORD bytesWritten = 0;

    BOOL success = WriteFile(
        pipe,
        payload.data(),
        bytesToWrite,
        &bytesWritten,
        nullptr);

    if (!success)
    {
        DWORD error = GetLastError();

        wchar_t log[128]{};
        swprintf_s(
            log,
            L"[PIPE SERVER] WriteFile failed: %lu\n",
            error);
        OutputDebugStringW(log);

        m_connected = false;
        return false;
    }

    if (bytesWritten != bytesToWrite)
    {
        OutputDebugStringW(
            L"[PIPE SERVER] Incomplete WriteFile\n");

        m_connected = false;
        return false;
    }

    return true;
}

void NamedPipeServer::ReadLoop(HANDLE pipe)
{
    std::vector<char> buffer(
        NamedPipe::BUFFER_SIZE);

    while (m_running && m_connected)
    {
        std::string bytes;
        bool messageComplete = false;

        while (m_running && !messageComplete)
        {
            DWORD bytesRead = 0;

            BOOL success = ReadFile(
                pipe,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesRead,
                nullptr);

            if (bytesRead > 0)
            {
                if (bytes.size() + bytesRead >
                    NamedPipe::MAX_MESSAGE_BYTES)
                {
                    m_connected = false;
                    break;
                }

                bytes.append(
                    buffer.data(),
                    bytesRead);
            }

            if (!success)
            {
                DWORD error = GetLastError();

                if (error == ERROR_MORE_DATA)
                    continue;

                m_connected = false;
                break;
            }

            messageComplete = true;
        }

        if (!m_connected)
            break;

        if (bytes.empty() ||
            bytes.size() % sizeof(wchar_t) != 0)
        {
            continue;
        }

        std::wstring payload(
            bytes.size() / sizeof(wchar_t),
            L'\0');

        memcpy(
            payload.data(),
            bytes.data(),
            bytes.size());

        NamedPipe::PostPayload(
            m_notifyWindow,
            std::move(payload));
    }

    m_connected = false;

    OutputDebugStringW(
        L"[PIPE SERVER] ReadLoop exited\n");
}

void NamedPipeServer::Stop()
{
    m_running = false;
    m_connected = false;

    HANDLE writePipe = m_writePipe;
    HANDLE readPipe = m_readPipe;

    m_writePipe = INVALID_HANDLE_VALUE;
    m_readPipe = INVALID_HANDLE_VALUE;

    if (m_connectionThread.joinable())
    {
        CancelSynchronousIo(
            m_connectionThread.native_handle());
    }

    if (m_readThread.joinable())
    {
        CancelSynchronousIo(
            m_readThread.native_handle());
    }

    if (writePipe != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(writePipe);
        CloseHandle(writePipe);
    }

    if (readPipe != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(readPipe);
        CloseHandle(readPipe);
    }

    if (m_connectionThread.joinable())
        m_connectionThread.join();

    if (m_readThread.joinable())
        m_readThread.join();

    m_notifyWindow = nullptr;
}