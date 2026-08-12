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
struct ForegroundProcessCache
{
    HWND window = nullptr;
    DWORD processId = 0;
    std::wstring exeName;
};

ForegroundProcessCache g_foregroundProcessCache;
HWINEVENTHOOK g_foregroundEventHook = nullptr;

GameProfile DefaultGameProfile()
{
    GameProfile profile{};
    profile.lockWindowsKey = g_windowsKeyGuardEnabled;
    profile.showNotifications = g_notificationsEnabled || g_overlayNotificationsEnabled;
    return profile;
}

void NormalizeProfile(GameProfile& profile)
{
    profile.restoreTimeoutMs = std::clamp(profile.restoreTimeoutMs, 1000U, 300000U);
    std::vector<UINT> keys;
    for (UINT key : profile.chatKeys)
    {
        if (key > 0 && key < 256 && std::find(keys.begin(), keys.end(), key) == keys.end())
        {
            keys.push_back(key);
        }
    }
    profile.chatKeys = keys.empty() ? std::vector<UINT>{ VK_RETURN } : std::move(keys);
}

const GameProfile& ActiveGameProfile()
{
    const auto profile = g_gameProfiles.find(g_activeGameExeName);
    if (profile != g_gameProfiles.end())
    {
        return profile->second;
    }
    static GameProfile fallback;
    fallback = DefaultGameProfile();
    return fallback;
}

bool IsConfiguredChatKey(const GameProfile& profile, UINT virtualKey)
{
    return std::find(profile.chatKeys.begin(), profile.chatKeys.end(), virtualKey) != profile.chatKeys.end();
}

void ApplyTargetLanguage(const GameProfile& profile, HWND targetWindow)
{
    if (profile.targetLanguage == ProtectedInputLanguage::Chinese)
    {
        SwitchToChinese(targetWindow);
    }
    else
    {
        SwitchToEnglish(targetWindow);
    }
}

void StopChatTimeoutTimer()
{
    if (g_hWnd)
    {
        KillTimer(g_hWnd, TIMER_CHAT_TIMEOUT);
    }
}

void ShowChatOverlay(bool restored)
{
    const GameProfile& profile = ActiveGameProfile();
    if (!profile.showNotifications || !g_overlayNotificationsEnabled)
    {
        return;
    }

    if (restored)
    {
        OverlayNotificationManager::ShowSuccess(
            Text(L"保护已恢复", L"Protection restored"),
            profile.targetLanguage == ProtectedInputLanguage::Chinese
                ? Text(L"输入法已锁定中文", L"Chinese locked")
                : Text(L"输入法已锁定英文", L"English locked"));
    }
    else
    {
        OverlayNotificationManager::ShowInfo(
            Text(L"输入法已恢复", L"Input restored"),
            Text(L"聊天输入中", L"Chat input"));
    }
}

void EndChatInput(bool showNotification)
{
    if (!g_chatInputSuspended)
    {
        return;
    }

    g_chatInputSuspended = false;
    g_chatInputSuspendUntil = 0;
    g_chatActiveKey = 0;
    StopChatTimeoutTimer();
    ApplyTargetLanguage(ActiveGameProfile(), IsWindow(g_savedWindow) ? g_savedWindow : GetForegroundWindow());
    if (showNotification)
    {
        ShowChatOverlay(true);
    }
    UpdateMainWindow();
}

void BeginChatInput(UINT virtualKey)
{
    if (g_chatInputSuspended)
    {
        return;
    }

    const GameProfile& profile = ActiveGameProfile();
    g_chatInputSuspended = true;
    g_chatActiveKey = virtualKey;
    g_chatInputSuspendUntil = GetTickCount64() + profile.restoreTimeoutMs;
    ApplySavedLayout(IsWindow(g_savedWindow) ? g_savedWindow : GetForegroundWindow());
    if (g_hWnd)
    {
        SetTimer(g_hWnd, TIMER_CHAT_TIMEOUT, profile.restoreTimeoutMs, nullptr);
    }
    ShowChatOverlay(false);
    UpdateMainWindow();
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

    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path.data(), &size);
    CloseHandle(process);
    if (!queried)
    {
        return L"";
    }
    path.resize(size);
    return path;
}

std::wstring GetProcessExeName(HWND hwnd)
{
    const std::wstring path = GetProcessExePath(hwnd);
    return path.empty() ? L"" : GetExeNameFromPath(path);
}

std::wstring GetForegroundProcessExeName(HWND* foregroundWindow = nullptr)
{
    HWND hwnd = GetForegroundWindow();
    if (foregroundWindow)
    {
        *foregroundWindow = hwnd;
    }

    DWORD processId = 0;
    if (hwnd)
    {
        GetWindowThreadProcessId(hwnd, &processId);
    }
    if (hwnd == g_foregroundProcessCache.window && processId != 0 &&
        processId == g_foregroundProcessCache.processId)
    {
        return g_foregroundProcessCache.exeName;
    }

    g_foregroundProcessCache.window = hwnd;
    g_foregroundProcessCache.processId = processId;
    g_foregroundProcessCache.exeName = GetProcessExeName(hwnd);
    return g_foregroundProcessCache.exeName;
}

bool IsGameExe(const std::wstring& exeName)
{
    return std::find(g_gameExeNames.begin(), g_gameExeNames.end(), ToLower(exeName)) != g_gameExeNames.end();
}

void EnterGameProtection(const std::wstring& exeName, HWND foregroundWindow)
{
    const bool switchingProtectedGame = g_inGameProtection && g_activeGameExeName != exeName;
    if (g_inGameProtection && !switchingProtectedGame)
    {
        if (foregroundWindow != g_savedWindow)
        {
            g_savedWindow = foregroundWindow;
            if (!g_chatInputSuspended)
            {
                ApplyTargetLanguage(ActiveGameProfile(), foregroundWindow);
            }
        }
        return;
    }

    if (!g_inGameProtection)
    {
        const DWORD threadId = foregroundWindow ? GetWindowThreadProcessId(foregroundWindow, nullptr) : 0;
        g_savedLayout = threadId ? GetKeyboardLayout(threadId) : GetKeyboardLayout(0);
        g_inGameProtection = true;
    }
    else
    {
        g_chatInputSuspended = false;
        g_chatInputSuspendUntil = 0;
        g_chatActiveKey = 0;
        StopChatTimeoutTimer();
    }

    g_activeGameExeName = exeName;
    g_savedWindow = foregroundWindow;
    const GameProfile& profile = ActiveGameProfile();
    ApplyTargetLanguage(profile, foregroundWindow);
    if (profile.showNotifications)
    {
        ShowTrayNotification(
            L"FFKeyLock",
            profile.targetLanguage == ProtectedInputLanguage::Chinese
                ? Text(L"已进入保护模式，输入法已锁定为中文。", L"Protection is active. Input language is locked to Chinese.")
                : Text(L"已进入保护模式，输入法已锁定为英文。", L"Protection is active. Input language is locked to English."),
            true);
    }
    UpdateMainWindow();
}

void CALLBACK ForegroundEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG objectId, LONG childId, DWORD, DWORD)
{
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd && objectId == OBJID_WINDOW && childId == CHILDID_SELF && g_hWnd)
    {
        PostMessageW(g_hWnd, WM_FOREGROUND_CHANGED, reinterpret_cast<WPARAM>(hwnd), 0);
    }
}
}

GameProfile GetGameProfileForExe(const std::wstring& exeName)
{
    const auto profile = g_gameProfiles.find(ToLower(exeName));
    return profile == g_gameProfiles.end() ? DefaultGameProfile() : profile->second;
}

void SetGameProfileForExe(const std::wstring& exeName, GameProfile profile)
{
    const std::wstring normalizedName = ToLower(Trim(exeName));
    if (normalizedName.empty())
    {
        return;
    }
    NormalizeProfile(profile);
    g_gameProfiles[normalizedName] = std::move(profile);
    SaveConfig();
    if (g_inGameProtection && g_activeGameExeName == normalizedName && !g_chatInputSuspended)
    {
        ApplyTargetLanguage(ActiveGameProfile(), IsWindow(g_savedWindow) ? g_savedWindow : GetForegroundWindow());
    }
    UpdateMainWindow();
}

bool InitializeForegroundDetection()
{
    if (g_foregroundEventHook)
    {
        return true;
    }
    g_foregroundEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr,
        ForegroundEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT);
    return g_foregroundEventHook != nullptr;
}

void ShutdownForegroundDetection()
{
    if (g_foregroundEventHook)
    {
        UnhookWinEvent(g_foregroundEventHook);
        g_foregroundEventHook = nullptr;
    }
}

void LeaveGameProtection()
{
    if (!g_inGameProtection)
    {
        return;
    }

    const GameProfile profile = ActiveGameProfile();
    StopChatTimeoutTimer();
    RestoreSavedLayout();
    g_inGameProtection = false;
    g_chatInputSuspended = false;
    g_chatInputSuspendUntil = 0;
    g_chatActiveKey = 0;
    g_activeGameExeName.clear();
    if (profile.showNotifications)
    {
        ShowTrayNotification(
            L"FFKeyLock",
            Text(L"已离开游戏窗口，输入法状态已恢复。", L"Left the game window. Input language has been restored."));
    }
    UpdateMainWindow();
}

void DetectForegroundGame()
{
    const std::wstring previousDetectedGame = g_currentDetectedGameName;
    const bool wasInGameProtection = g_inGameProtection;
    HWND foregroundWindow = nullptr;
    const std::wstring exeName = GetForegroundProcessExeName(&foregroundWindow);
    RememberExternalForegroundWindow(foregroundWindow);
    g_currentDetectedGameName = (!exeName.empty() && IsGameExe(exeName)) ? exeName : L"";

    if (!g_protectionEnabled || !g_autoDetectEnabled)
    {
        g_currentDetectedGameName.clear();
        LeaveGameProtection();
        if (!wasInGameProtection && previousDetectedGame != g_currentDetectedGameName)
        {
            UpdateMainWindow();
        }
        return;
    }

    if (!g_currentDetectedGameName.empty())
    {
        ResumeGameProtectionAfterChatTimeout();
        EnterGameProtection(g_currentDetectedGameName, foregroundWindow);
    }
    else
    {
        LeaveGameProtection();
    }

    if (wasInGameProtection == g_inGameProtection && previousDetectedGame != g_currentDetectedGameName)
    {
        UpdateMainWindow();
    }
}

bool IsGameChatControlKey(UINT virtualKey)
{
    if (!g_protectionEnabled || !g_inGameProtection || virtualKey >= 256)
    {
        return false;
    }
    const GameProfile& profile = ActiveGameProfile();
    return IsConfiguredChatKey(profile, virtualKey) ||
        (g_chatInputSuspended && (virtualKey == VK_RETURN || virtualKey == VK_ESCAPE));
}

void HandleGameChatKey(UINT virtualKey, bool keyDown)
{
    if (!g_protectionEnabled || !g_inGameProtection)
    {
        return;
    }

    HWND foregroundWindow = nullptr;
    const std::wstring exeName = GetForegroundProcessExeName(&foregroundWindow);
    if (exeName.empty() || exeName != g_activeGameExeName || !IsGameExe(exeName))
    {
        return;
    }

    const GameProfile& profile = ActiveGameProfile();
    if (!g_chatInputSuspended)
    {
        if (keyDown && IsConfiguredChatKey(profile, virtualKey))
        {
            BeginChatInput(virtualKey);
        }
        return;
    }

    if (keyDown && (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN))
    {
        EndChatInput(true);
        return;
    }
    if (profile.chatMode == ChatActivationMode::Hold && !keyDown && virtualKey == g_chatActiveKey)
    {
        EndChatInput(true);
    }
}

bool ShouldBlockWindowsKeyForActiveGame()
{
    return g_protectionEnabled && g_inGameProtection && ActiveGameProfile().lockWindowsKey;
}

void ResumeGameProtectionAfterChatTimeout()
{
    if (!g_inGameProtection || !g_chatInputSuspended || g_chatInputSuspendUntil == 0)
    {
        StopChatTimeoutTimer();
        return;
    }
    if (GetTickCount64() < g_chatInputSuspendUntil)
    {
        return;
    }
    EndChatInput(true);
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
    g_gameProfiles[exeName] = DefaultGameProfile();
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
}
