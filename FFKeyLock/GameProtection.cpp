#include "GameProtection.h"

#include "AppState.h"
#include "Config.h"
#include "InputLanguage.h"
#include "Localization.h"
#include "MainWindow.h"
#include "OverlayNotificationManager.h"
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

std::wstring GetProcessExePath(HWND hwnd)
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
    return path;
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
    g_chatInputSuspended = false;
    g_chatInputSuspendUntil = 0;
    SwitchToEnglish(foregroundWindow);
    ShowTrayNotification(L"FFKeyLock", Text(L"已进入保护模式，输入法已锁定为英文。", L"Protection is active. Input language is locked to English."), true);
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
    g_chatInputSuspended = false;
    g_chatInputSuspendUntil = 0;
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
        ResumeGameProtectionAfterChatTimeout();
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
    std::wstring exePath;
    if (exeName.empty())
    {
        ShowTrayNotification(L"FFKeyLock", Text(L"请输入或选择有效的受保护程序。", L"Enter or select a valid protected executable."));
        return false;
    }

    if (exeName.find(L'\\') != std::wstring::npos || exeName.find(L'/') != std::wstring::npos)
    {
        exePath = exeName;
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
        ShowTrayNotification(L"FFKeyLock", Text(L"不能把 FFKeyLock 自己添加为受保护程序。", L"FFKeyLock cannot be added as a protected program."));
        return false;
    }

    if (IsGameExe(exeName))
    {
        if (!exePath.empty())
        {
            g_gameExePaths[exeName] = exePath;
            SaveConfig();
        }
        ShowTrayNotification(L"FFKeyLock", (std::wstring(Text(L"保护列表中已存在：", L"Already in protected list: ")) + exeName).c_str());
        return false;
    }

    g_gameExeNames.push_back(exeName);
    if (!exePath.empty())
    {
        g_gameExePaths[exeName] = exePath;
    }
    SaveConfig();
    ShowTrayNotification(L"FFKeyLock", (std::wstring(Text(L"已添加受保护程序：", L"Added protected executable: ")) + exeName).c_str());
    UpdateMainWindow();
    return true;
}

void AddProgramAsGame(HWND targetWindow)
{
    const std::wstring exePath = GetProcessExePath(targetWindow);
    if (exePath.empty())
    {
        ShowTrayNotification(L"FFKeyLock", Text(L"没有可添加的前台程序。", L"No foreground program can be added."));
        return;
    }

    AddGameExeName(exePath);
}

void ToggleGameChatInputMode()
{
    if (!g_inGameProtection)
    {
        return;
    }

    HWND foregroundWindow = nullptr;
    const std::wstring exeName = GetForegroundProcessExeName(&foregroundWindow);
    if (exeName.empty() || !IsGameExe(exeName))
    {
        return;
    }

    HWND targetWindow = IsWindow(g_savedWindow) ? g_savedWindow : foregroundWindow;
    if (!g_chatInputSuspended)
    {
        g_chatInputSuspended = true;
        g_chatInputSuspendUntil = GetTickCount() + 12000;
        ApplySavedLayout(targetWindow);
        OverlayNotificationManager::ShowInfo(
            Text(L"输入法已恢复", L"Input restored"),
            Text(L"聊天输入中", L"Chat input"));
    }
    else
    {
        g_chatInputSuspended = false;
        g_chatInputSuspendUntil = 0;
        SwitchToEnglish(targetWindow);
        OverlayNotificationManager::ShowSuccess(
            Text(L"保护已恢复", L"Protection restored"),
            Text(L"输入法已锁定英文", L"English locked"));
    }
    UpdateMainWindow();
}

void ResumeGameProtectionAfterChatTimeout()
{
    if (!g_inGameProtection || !g_chatInputSuspended || g_chatInputSuspendUntil == 0)
    {
        return;
    }

    if (static_cast<LONG>(GetTickCount() - g_chatInputSuspendUntil) < 0)
    {
        return;
    }

    g_chatInputSuspended = false;
    g_chatInputSuspendUntil = 0;
    SwitchToEnglish(IsWindow(g_savedWindow) ? g_savedWindow : GetForegroundWindow());
    UpdateMainWindow();
}
}
