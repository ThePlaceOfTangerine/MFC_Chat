#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <set>
#include <mutex>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

static wchar_t SERVICE_NAME[] = L"EchoService";
static const int PORT = 9999;

static SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
static SERVICE_STATUS        g_status = {};
static HANDLE                g_stopEvent = nullptr;

static SOCKET                g_listen = INVALID_SOCKET;
static std::set<SOCKET>      g_clients;
static std::mutex            g_mutex;
static LONG                  g_activeThreads = 0;   // số thread client đang chạy

// ---------- báo trạng thái cho SCM ----------
static void SetStatus(DWORD state, DWORD waitHint = 0)
{
    static DWORD checkPoint = 1;

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwWaitHint = waitHint;
    g_status.dwControlsAccepted =
        (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING)
        ? 0 : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_status.dwCheckPoint =
        (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkPoint++;

    ::SetServiceStatus(g_statusHandle, &g_status);
}

// ---------- xử lý echo ----------
static DWORD WINAPI ClientThread(LPVOID param)
{
    SOCKET s = (SOCKET)param;
    char buf[4096];

    while (true)
    {
        int n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;                       // 0 = client đóng, <0 = lỗi/bị đóng khi stop

        int sent = 0;
        while (sent < n)
        {
            int m = ::send(s, buf + sent, n - sent, 0);
            if (m <= 0) goto done;
            sent += m;
        }
    }
done:
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_clients.erase(s))                 
            ::closesocket(s);
    }
    ::InterlockedDecrement(&g_activeThreads);
    return 0;
}

// ---------- thread accept ----------
static DWORD WINAPI AcceptThread(LPVOID)
{
    while (::WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0)
    {
        SOCKET c = ::accept(g_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET)
            break;                               // listen socket đóng khi stop

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_clients.insert(c);
        }
        ::InterlockedIncrement(&g_activeThreads);

        HANDLE h = ::CreateThread(nullptr, 0, ClientThread, (LPVOID)c, 0, nullptr);
        if (h) ::CloseHandle(h);
        else
        {
            ::InterlockedDecrement(&g_activeThreads);
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_clients.erase(c)) ::closesocket(c);
        }
    }
    return 0;
}

// ---------- dừng Server----------
static void StopServer(HANDLE hAcceptThread)
{
    ::SetEvent(g_stopEvent);

    // Đóng listen socket -> accept() trả về lỗi ngay
    if (g_listen != INVALID_SOCKET)
    {
        ::closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }

    // Đóng mọi client -> recv() đang chặn trả về ngay
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (SOCKET c : g_clients)
        {
            ::shutdown(c, SD_BOTH);
            ::closesocket(c);
        }
        g_clients.clear();
    }

    // Chờ thread accept thoát
    if (hAcceptThread)
        ::WaitForSingleObject(hAcceptThread, 5000);

    // Chờ các thread client thoát
    for (int i = 0; i < 50 && g_activeThreads > 0; i++)
    {
        SetStatus(SERVICE_STOP_PENDING, 2000);
        ::Sleep(100);
    }
}

// ---------- callback nhận lệnh từ SCM ----------
static DWORD WINAPI ServiceCtrlHandler(DWORD ctrl, DWORD, LPVOID, LPVOID)
{
    switch (ctrl)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        SetStatus(SERVICE_STOP_PENDING, 10000);
        ::SetEvent(g_stopEvent);                 // tín hiệu Stop
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

// ---------- ServiceMain ----------
static void WINAPI ServiceMain(DWORD, LPWSTR*)
{
    g_statusHandle = ::RegisterServiceCtrlHandlerExW(SERVICE_NAME, ServiceCtrlHandler, nullptr);
    if (!g_statusHandle) return;

    SetStatus(SERVICE_START_PENDING, 5000);

    g_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

    WSADATA wsa;
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { SetStatus(SERVICE_STOPPED); return; }

    g_listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    BOOL reuse = TRUE;
    ::setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    if (::bind(g_listen, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
        ::listen(g_listen, SOMAXCONN) == SOCKET_ERROR)
    {
        ::closesocket(g_listen);
        ::WSACleanup();
        ::CloseHandle(g_stopEvent);
        SetStatus(SERVICE_STOPPED);
        return;
    }

    HANDLE hAccept = ::CreateThread(nullptr, 0, AcceptThread, nullptr, 0, nullptr);

    SetStatus(SERVICE_RUNNING);

    //Đợi tới khi có lệnh Stop
    ::WaitForSingleObject(g_stopEvent, INFINITE);

    StopServer(hAccept);
    if (hAccept) ::CloseHandle(hAccept);

    ::WSACleanup();
    ::CloseHandle(g_stopEvent);
    SetStatus(SERVICE_STOPPED);
}

int wmain()
{
    SERVICE_TABLE_ENTRYW table[] = {
        { SERVICE_NAME, ServiceMain },
        { nullptr, nullptr }
    };

    if (!::StartServiceCtrlDispatcherW(table))
        return (int)::GetLastError();            // 1063 nếu chạy trực tiếp, không qua SCM
    return 0;
}