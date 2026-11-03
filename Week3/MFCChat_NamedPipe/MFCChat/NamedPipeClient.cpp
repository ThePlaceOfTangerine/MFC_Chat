
#include "pch.h"
#include "NamedPipeClient.h"

#include <vector>
#include <string>
#include <cstring>

NamedPipeClient::NamedPipeClient() = default;

NamedPipeClient::~NamedPipeClient()
{
    Stop();
}

bool NamedPipeClient::Start(HWND notifyWindow)
{
    if (notifyWindow == nullptr || m_running)
        return false;

    m_notifyWindow = notifyWindow;
    m_connected = false;
    m_running = true;

    try
    {
        m_connectionThread = std::thread(
            &NamedPipeClient::ConnectionLoop,
            this
        );
    }
    catch (...)
    {
        m_running = false;
        m_notifyWindow = nullptr;
        OutputDebugStringW(
            L"[PIPE CLIENT] Failed to create connection thread\n"
        );
        return false;
    }

    return true;
}

bool NamedPipeClient::IsConnected() const
{
    return m_connected.load();
}

void NamedPipeClient::ConnectionLoop()
{
    OutputDebugStringW(
        L"[PIPE CLIENT] ConnectionLoop ENTER\n"
    );

    HANDLE writePipe = INVALID_HANDLE_VALUE;
    HANDLE readPipe = INVALID_HANDLE_VALUE;

    while (m_running)
    {
        writePipe = CreateFileW(
            NamedPipe::CLIENT_TO_SERVER,
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (writePipe != INVALID_HANDLE_VALUE)
            break;

        DWORD error = GetLastError();

        if (error == ERROR_PIPE_BUSY)
        {
            WaitNamedPipeW(
                NamedPipe::CLIENT_TO_SERVER,
                500
            );
        }
        else if (error != ERROR_FILE_NOT_FOUND)
        {
            wchar_t log[160]{};

            swprintf_s(
                log,
                L"[PIPE CLIENT] Open ClientToServer failed: %lu\n",
                error
            );

            OutputDebugStringW(log);
        }

        if (m_running)
            Sleep(200);
    }

    if (!m_running ||
        writePipe == INVALID_HANDLE_VALUE)
    {
        if (writePipe != INVALID_HANDLE_VALUE)
            CloseHandle(writePipe);

        OutputDebugStringW(
            L"[PIPE CLIENT] Stopped before write pipe connected\n"
        );

        return;
    }

    OutputDebugStringW(
        L"[PIPE CLIENT] Connected to ClientToServer\n"
    );

    while (m_running)
    {
        readPipe = CreateFileW(
            NamedPipe::SERVER_TO_CLIENT,
            GENERIC_READ,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (readPipe != INVALID_HANDLE_VALUE)
            break;

        DWORD error = GetLastError();

        if (error == ERROR_PIPE_BUSY)
        {
            WaitNamedPipeW(
                NamedPipe::SERVER_TO_CLIENT,
                500
            );
        }
        else if (error != ERROR_FILE_NOT_FOUND)
        {
            wchar_t log[160]{};

            swprintf_s(
                log,
                L"[PIPE CLIENT] Open ServerToClient failed: %lu\n",
                error
            );

            OutputDebugStringW(log);
        }

        if (m_running)
            Sleep(200);
    }

    if (!m_running ||
        readPipe == INVALID_HANDLE_VALUE)
    {
        if (readPipe != INVALID_HANDLE_VALUE)
            CloseHandle(readPipe);

        if (writePipe != INVALID_HANDLE_VALUE)
            CloseHandle(writePipe);

        OutputDebugStringW(
            L"[PIPE CLIENT] Stopped before read pipe connected\n"
        );

        return;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;

    if (!m_running)
    {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return;
    }

    m_writePipe = writePipe;
    m_readPipe = readPipe;
    m_connected = true;

    OutputDebugStringW(
        L"[PIPE CLIENT] Connected to ServerToClient\n"
    );

    OutputDebugStringW(
        L"[PIPE CLIENT] Chat connected\n"
    );

    try
    {
        m_readThread = std::thread(
            &NamedPipeClient::ReadLoop,
            this,
            readPipe
        );
    }
    catch (...)
    {
        OutputDebugStringW(
            L"[PIPE CLIENT] Failed to create read thread\n"
        );

        m_connected = false;
        m_running = false;
    }
}

bool NamedPipeClient::Send(
    const std::wstring & sender,
    const std::wstring & message)
{
    if (!m_running ||
        !m_connected ||
        sender.empty() ||
        message.empty())
    {
        OutputDebugStringW(
            L"[PIPE CLIENT] Send rejected: not connected or empty message\n"
        );

        return false;
    }

    std::wstring payload = sender + L"|" + message;

    if (payload.size() * sizeof(wchar_t) >
        NamedPipe::MAX_MESSAGE_BYTES)
    {
        OutputDebugStringW(
            L"[PIPE CLIENT] Send rejected: message too large\n"
        );

        return false;
    }

    return WriteMessage(m_writePipe, payload);
}

bool NamedPipeClient::WriteMessage(
    HANDLE pipe,
    const std::wstring & payload)
{
    if (pipe == INVALID_HANDLE_VALUE || payload.empty())
        return false;

    DWORD bytesToWrite = static_cast<DWORD>(
        payload.size() * sizeof(wchar_t)
        );

    DWORD bytesWritten = 0;

    BOOL success = WriteFile(
        pipe,
        payload.data(),
        bytesToWrite,
        &bytesWritten,
        nullptr
    );

    if (!success)
    {
        DWORD error = GetLastError();

        wchar_t log[160]{};

        swprintf_s(
            log,
            L"[PIPE CLIENT] WriteFile failed: %lu\n",
            error
        );

        OutputDebugStringW(log);

        m_connected = false;
        return false;
    }

    if (bytesWritten != bytesToWrite)
    {
        OutputDebugStringW(
            L"[PIPE CLIENT] Incomplete WriteFile\n"
        );

        m_connected = false;
        return false;
    }

    OutputDebugStringW(
        L"[PIPE CLIENT] WriteFile succeeded\n"
    );

    return true;
}

void NamedPipeClient::ReadLoop(HANDLE pipe)
{
    std::vector<char> buffer(
        NamedPipe::BUFFER_SIZE
    );

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
                nullptr
            );

            if (bytesRead > 0)
            {
                if (bytes.size() + bytesRead >
                    NamedPipe::MAX_MESSAGE_BYTES)
                {
                    OutputDebugStringW(
                        L"[PIPE CLIENT] Incoming message too large\n"
                    );

                    m_connected = false;
                    break;
                }

                bytes.append(
                    buffer.data(),
                    bytesRead
                );
            }

            if (!success)
            {
                DWORD error = GetLastError();

                if (error == ERROR_MORE_DATA)
                    continue;

                if (m_running)
                {
                    wchar_t log[160]{};

                    swprintf_s(
                        log,
                        L"[PIPE CLIENT] ReadFile failed: %lu\n",
                        error
                    );

                    OutputDebugStringW(log);
                }

                m_connected = false;
                break;
            }

            messageComplete = true;
        }

        if (!m_running || !m_connected)
            break;

        if (bytes.empty() ||
            bytes.size() % sizeof(wchar_t) != 0)
        {
            OutputDebugStringW(
                L"[PIPE CLIENT] Invalid incoming message size\n"
            );

            continue;
        }

        std::wstring payload(
            bytes.size() / sizeof(wchar_t),
            L'\0'
        );

        memcpy(
            payload.data(),
            bytes.data(),
            bytes.size()
        );

        if (!NamedPipe::PostPayload(
            m_notifyWindow,
            std::move(payload)))
        {
            OutputDebugStringW(
                L"[PIPE CLIENT] Failed to post message to UI\n"
            );
        }
    }

    m_connected = false;

    OutputDebugStringW(
        L"[PIPE CLIENT] ReadLoop exited\n"
    );
}

void NamedPipeClient::Stop()
{
    OutputDebugStringW(
        L"[PIPE CLIENT] Stop called\n"
    );

    m_running = false;
    m_connected = false;

    if (m_connectionThread.joinable())
    {
        CancelSynchronousIo(
            m_connectionThread.native_handle()
        );

        if (m_connectionThread.get_id() !=
            std::this_thread::get_id())
        {
            m_connectionThread.join();
        }
    }

    if (m_readThread.joinable())
    {
        CancelSynchronousIo(
            m_readThread.native_handle()
        );

        if (m_readThread.get_id() !=
            std::this_thread::get_id())
        {
            m_readThread.join();
        }
    }

    HANDLE writePipe = m_writePipe;
    HANDLE readPipe = m_readPipe;

    m_writePipe = INVALID_HANDLE_VALUE;
    m_readPipe = INVALID_HANDLE_VALUE;

    if (writePipe != INVALID_HANDLE_VALUE)
        CloseHandle(writePipe);

    if (readPipe != INVALID_HANDLE_VALUE)
        CloseHandle(readPipe);

    m_notifyWindow = nullptr;

    OutputDebugStringW(
        L"[PIPE CLIENT] Stop completed\n"
    );
}
