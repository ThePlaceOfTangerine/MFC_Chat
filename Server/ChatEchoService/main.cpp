#include "EchoServer.h"

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr wchar_t SERVICE_NAME[] = L"ChatEchoService";

    SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
    SERVICE_STATUS g_status{};

    HANDLE g_stopEvent = nullptr;
    HANDLE g_startupEvent = nullptr;
    HANDLE g_workerThread = nullptr;

    EchoServerContext g_context;

    void ReportStatus(
        DWORD state,
        DWORD win32ExitCode = NO_ERROR,
        DWORD serviceExitCode = 0)
    {
        g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        g_status.dwCurrentState = state;
        g_status.dwWin32ExitCode = win32ExitCode;
        g_status.dwServiceSpecificExitCode = serviceExitCode;
        g_status.dwWaitHint = 5000;

        if (state == SERVICE_START_PENDING ||
            state == SERVICE_STOP_PENDING)
        {
            g_status.dwControlsAccepted = 0;
            ++g_status.dwCheckPoint;
        }
        else
        {
            g_status.dwCheckPoint = 0;
            g_status.dwControlsAccepted =
                (state == SERVICE_RUNNING)
                ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
                : 0;
        }

        if (g_statusHandle != nullptr)
        {
            SetServiceStatus(g_statusHandle, &g_status);
        }
    }

    void WINAPI ServiceControlHandler(DWORD control)
    {
        switch (control)
        {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            if (g_status.dwCurrentState == SERVICE_RUNNING ||
                g_status.dwCurrentState == SERVICE_START_PENDING)
            {
                ReportStatus(SERVICE_STOP_PENDING);
                SetEvent(g_stopEvent);
            }
            break;

        default:
            break;
        }
    }

    void CleanupHandles()
    {
        if (g_workerThread != nullptr)
        {
            CloseHandle(g_workerThread);
            g_workerThread = nullptr;
        }

        if (g_startupEvent != nullptr)
        {
            CloseHandle(g_startupEvent);
            g_startupEvent = nullptr;
        }

        if (g_stopEvent != nullptr)
        {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
    }

    void WINAPI ServiceMain(DWORD, LPWSTR*)
    {
        g_statusHandle = RegisterServiceCtrlHandlerW(
            SERVICE_NAME,
            ServiceControlHandler);

        if (g_statusHandle == nullptr)
            return;

        ReportStatus(SERVICE_START_PENDING);

        g_stopEvent = CreateEventW(
            nullptr, TRUE, FALSE, nullptr);

        g_startupEvent = CreateEventW(
            nullptr, TRUE, FALSE, nullptr);

        if (g_stopEvent == nullptr ||
            g_startupEvent == nullptr)
        {
            DWORD error = GetLastError();
            ReportStatus(SERVICE_STOPPED, error);
            CleanupHandles();
            return;
        }

        g_context.stopEvent = g_stopEvent;
        g_context.startupEvent = g_startupEvent;
        g_context.startupSucceeded = 0;
        g_context.startupError = 0;

        g_workerThread = CreateThread(
            nullptr,
            0,
            EchoServerThread,
            &g_context,
            0,
            nullptr);

        if (g_workerThread == nullptr)
        {
            DWORD error = GetLastError();
            ReportStatus(SERVICE_STOPPED, error);
            CleanupHandles();
            return;
        }

        WaitForSingleObject(g_startupEvent, INFINITE);

        if (InterlockedCompareExchange(
            &g_context.startupSucceeded, 0, 0) == 0)
        {
            DWORD error = g_context.startupError;

            WaitForSingleObject(g_workerThread, INFINITE);

            ReportStatus(
                SERVICE_STOPPED,
                ERROR_SERVICE_SPECIFIC_ERROR,
                error);

            CleanupHandles();
            return;
        }

        if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0)
        {
            ReportStatus(SERVICE_STOP_PENDING);
        }
        else
        {
            ReportStatus(SERVICE_RUNNING);
        }

        WaitForSingleObject(g_workerThread, INFINITE);

        ReportStatus(SERVICE_STOPPED);
        CleanupHandles();
    }
}

int wmain()
{
    SERVICE_TABLE_ENTRYW serviceTable[] =
    {
        {
            const_cast<LPWSTR>(SERVICE_NAME),
            ServiceMain
        },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(serviceTable))
        return static_cast<int>(GetLastError());

    return 0;
}