#include "GameProtection.h"

#include "AppState.h"
#include "Config.h"
#include "InputLanguage.h"
#include "Localization.h"
#include "MainWindow.h"
#include "StringUtils.h"
#include "TrayIcon.h"

#include <algorithm>
#include <filesystem>

namespace FFKeyLock
{
namespace
{
std::wstring GetProcessExeName(HWND hwnd)
{
    if (!hwnd)
    {
        return L"";
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (!processId)
    {
        return L"";
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
    {
        return L"";
    }

    std::wstring path(MAX_PATH, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &size))
    {
        CloseHandle(process);
        return L"";
    }

    CloseHandle(process);
    path.resize(size);
    return GetExeNameFromPath(path);
}

std::wstring GetForegroundProcessExeName(HWND* foregroundWindow = nullptr)
{
    HWND hwnd = GetForegroundWindow();
    if (foregroundWindow)
    {
        *foregroundWindow = hwnd;
    }

    return GetProcessExeName(hwnd);
}

bool IsGameExe(const std::wstring& exeName)
{
    return std::find(g_gameExeNames.begin(), g_gameExeNames.end(), ToLower(exeName)) != g_gameExeNames.end();
}

void EnterGameProtection(HWND foregroundWindow)
{
    if (g_inGameProtection)
    {
        return;
    }

    DWORD threadId = foregroundWindow ? GetWindowThreadProcessId(foregroundWindow, nullptr) : 0;
    g_savedLayout = threadId ? GetKeyboardLayout(threadId) : GetKeyboardLayout(0);
    g_savedWindow = foregroundWindow;
    g_inGameProtection = true;
    SwitchToEnglish(foregroundWindow);
    ShowTrayNotification(L"FFKeyLock", Text(L"已进入游戏保护，输入法已切换为英文。", L"Game protection is active. Input language switched to English."));
    UpdateMainWindow();
}
}

void LeaveGameProtection()
{
    if (!g_inGameProtection)
    {
        return;
    }

    RestoreSavedLayout();
    g_inGameProtection = false;
    ShowTrayNotification(L"FFKeyLock", Text(L"已离开游戏窗口，输入法状态已恢复。", L"Left the game window. Input language has been restored."));
    UpdateMainWindow();
}

void DetectForegroundGame()
{
    HWND foregroundWindow = nullptr;
    const std::wstring exeName = GetForegroundProcessExeName(&foregroundWindow);
    RememberExternalForegroundWindow(foregroundWindow);

    if (!g_protectionEnabled || !g_autoDetectEnabled)
    {
        LeaveGameProtection();
        return;
    }

    if (!exeName.empty() && IsGameExe(exeName))
    {
        EnterGameProtection(foregroundWindow);
    }
    else
    {
        LeaveGameProtection();
    }
}

bool IsStartupEnabled()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStartupRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    wchar_t value[MAX_PATH * 2]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    const LSTATUS result = RegQueryValueExW(key, kAppName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ && value[0] != L'\0';
}

void SetStartupEnabled(bool enabled)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kStartupRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        ShowTrayNotification(L"FFKeyLock", Text(L"开机启动设置失败。", L"Failed to update startup setting."));
        return;
    }

    if (enabled)
    {
        const std::wstring command = L"\"" + GetCurrentExePath() + L"\"";
        RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, kAppName);
    }

    RegCloseKey(key);
}

bool AddGameExeName(std::wstring exeName)
{
    exeName = Trim(exeName);
    if (exeName.empty())
    {
        ShowTrayNotification(L"FFKeyLock", Text(L"请输入或选择有效的游戏程序。", L"Enter or select a valid game executable."));
        return false;
    }

    if (exeName.find(L'\\') != std::wstring::npos || exeName.find(L'/') != std::wstring::npos)
    {
        exeName = GetExeNameFromPath(exeName);
    }
    else
    {
        exeName = ToLower(exeName);
    }

    if (exeName.find(L'.') == std::wstring::npos)
    {
        exeName += L".exe";
    }

    if (exeName == ToLower(std::filesystem::path(GetCurrentExePath()).filename().wstring()))
    {
        ShowTrayNotification(L"FFKeyLock", Text(L"不能把 FFKeyLock 自己添加为游戏。", L"FFKeyLock cannot be added as a game."));
        return false;
    }

    if (IsGameExe(exeName))
    {
        ShowTrayNotification(L"FFKeyLock", (std::wstring(Text(L"游戏列表中已存在：", L"Already in game list: ")) + exeName).c_str());
        return false;
    }

    g_gameExeNames.push_back(exeName);
    SaveConfig();
    ShowTrayNotification(L"FFKeyLock", (std::wstring(Text(L"已添加游戏程序：", L"Added game executable: ")) + exeName).c_str());
    UpdateMainWindow();
    return true;
}

void AddProgramAsGame(HWND targetWindow)
{
    const std::wstring exeName = GetProcessExeName(targetWindow);
    if (exeName.empty())
    {
        ShowTrayNotification(L"FFKeyLock", Text(L"没有可添加的前台程序。", L"No foreground program can be added."));
        return;
    }

    AddGameExeName(exeName);
}
}
