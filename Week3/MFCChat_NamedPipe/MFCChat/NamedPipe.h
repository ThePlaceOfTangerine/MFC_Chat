#pragma once

#include <Windows.h>
#include <string>
#include <new>
#include <utility>

constexpr UINT WM_APP_PIPE_MESSAGE = WM_APP + 101;

namespace NamedPipe
{
    inline constexpr wchar_t SERVER_TO_CLIENT[] =
        L"\\\\.\\pipe\\MFCChat_ServerToClient";

    inline constexpr wchar_t CLIENT_TO_SERVER[] =
        L"\\\\.\\pipe\\MFCChat_ClientToServer";

    inline constexpr DWORD BUFFER_SIZE = 4096;
    inline constexpr DWORD MAX_MESSAGE_BYTES = 4096;

    inline bool PostPayload(
        HWND hwnd,
        std::wstring&& payload)
    {
        if (hwnd == nullptr)
            return false;

        auto* data = new (std::nothrow)
            std::wstring(std::move(payload));

        if (data == nullptr)
            return false;

        if (!PostMessageW(
            hwnd,
            WM_APP_PIPE_MESSAGE,
            0,
            reinterpret_cast<LPARAM>(data)))
        {
            delete data;
            return false;
        }

        return true;
    }
}