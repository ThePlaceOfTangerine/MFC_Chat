
#include "AutoRun.h"

#include <Windows.h>
#include <cwchar>

#pragma comment(lib, "Advapi32.lib")

static constexpr wchar_t AUTORUN_KEY[] =
L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static constexpr wchar_t AUTORUN_NAME[] = L"MFCChat";

bool EnableAutoRun()
{
    HKEY hKey = nullptr;

    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        AUTORUN_KEY,
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS)
        return false;

    wchar_t exePath[MAX_PATH] = {};

    DWORD length = GetModuleFileNameW(
        nullptr,
        exePath,
        MAX_PATH
    );

    if (length == 0 || length >= MAX_PATH)
    {
        RegCloseKey(hKey);
        return false;
    }

    wchar_t command[MAX_PATH + 3] = {};

    if (swprintf_s(
        command,
        _countof(command),
        L"\"%s\"",
        exePath
    ) < 0)
    {
        RegCloseKey(hKey);
        return false;
    }

    result = RegSetValueExW(
        hKey,
        AUTORUN_NAME,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command),
        static_cast<DWORD>(
            (wcslen(command) + 1) * sizeof(wchar_t)));

    RegCloseKey(hKey);

    return result == ERROR_SUCCESS;
}

bool DisableAutoRun()
{
    HKEY hKey = nullptr;

    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        AUTORUN_KEY,
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS)
        return false;

    result = RegDeleteValueW(
        hKey,
        AUTORUN_NAME
    );

    RegCloseKey(hKey);

    return result == ERROR_SUCCESS ||
        result == ERROR_FILE_NOT_FOUND;
}

bool IsAutoRunEnabled()
{
    HKEY hKey = nullptr;

    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        AUTORUN_KEY,
        0,
        KEY_QUERY_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS)
        return false;

    result = RegQueryValueExW(
        hKey,
        AUTORUN_NAME,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    );

    RegCloseKey(hKey);

    return result == ERROR_SUCCESS;
}
