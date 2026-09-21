#pragma once

#include "framework.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace FFKeyLock
{
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_GAME_CHAT_KEY = WM_APP + 2;
constexpr UINT WM_FOREGROUND_CHANGED = WM_APP + 3;
constexpr UINT WM_EMERGENCY_UNLOCK = WM_APP + 4;
constexpr UINT WM_GUARD_FAILED = WM_APP + 5;
constexpr UINT_PTR TIMER_PAUSE = 3;
constexpr UINT_PTR TIMER_GAME_DETECT = 1;
constexpr UINT_PTR TIMER_CHAT_TIMEOUT = 2;
constexpr UINT DETECT_FALLBACK_INTERVAL_MS = 15000;
constexpr UINT DETECT_RECOVERY_INTERVAL_MS = 1000;
constexpr wchar_t kAppName[] = L"FFKeyLock";
constexpr wchar_t kConfigSection[] = L"Settings";
constexpr wchar_t kGamesKey[] = L"Games";
constexpr wchar_t kGamePathsKey[] = L"GamePaths";
constexpr wchar_t kGameProfilesKey[] = L"GameProfiles";
constexpr wchar_t kProtectionKey[] = L"ProtectionEnabled";
constexpr wchar_t kAutoDetectKey[] = L"AutoDetectEnabled";
constexpr wchar_t kWindowsKeyGuardKey[] = L"WindowsKeyGuardEnabled";
constexpr wchar_t kWindowsKeyGuardScopeKey[] = L"WindowsKeyGuardScope";
constexpr wchar_t kNotificationsKey[] = L"NotificationsEnabled";
constexpr wchar_t kOverlayNotificationsKey[] = L"OverlayNotificationsEnabled";
constexpr wchar_t kLanguageKey[] = L"Language";
constexpr wchar_t kThemeKey[] = L"Theme";
constexpr wchar_t kStartupRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

enum class UiLanguage
{
    Chinese,
    English
};

enum class ThemePreference
{
    Light,
    Dark,
    System
};

enum class ProtectedInputLanguage
{
    English,
    Chinese
};

enum class ChatActivationMode
{
    Toggle,
    Hold
};

enum class WindowsKeyGuardScope
{
    ProtectedForeground,
    Always
};

struct GameProfile
{
    bool enabled = true;
    bool inputGuardEnabled = true;
    bool keyGuardEnabled = true;
    bool inheritWindowsKey = false;
    std::vector<UINT> blockedKeys;
    ProtectedInputLanguage targetLanguage = ProtectedInputLanguage::English;
    std::vector<UINT> chatKeys{ VK_RETURN };
    ChatActivationMode chatMode = ChatActivationMode::Toggle;
    UINT restoreTimeoutMs = 12000;
    bool lockWindowsKey = false;
    bool showNotifications = true;
};

extern HINSTANCE g_hInst;
extern HWND g_hWnd;
extern UINT g_taskbarCreatedMessage;
extern bool g_protectionEnabled;
extern bool g_autoDetectEnabled;
extern bool g_windowsKeyGuardEnabled;
extern WindowsKeyGuardScope g_windowsKeyGuardScope;
extern bool g_notificationsEnabled;
extern bool g_overlayNotificationsEnabled;
extern bool g_inGameProtection;
extern bool g_chatInputSuspended;
extern ULONGLONG g_chatInputSuspendUntil;
extern UINT g_chatActiveKey;
extern std::wstring g_currentDetectedGameName;
extern std::wstring g_activeGameExeName;
extern HKL g_savedLayout;
extern HWND g_savedWindow;
extern HWND g_menuTargetWindow;
extern HWND g_lastExternalForegroundWindow;
extern HICON g_trayIcon;
extern UiLanguage g_language;
extern ThemePreference g_themePreference;
extern std::wstring g_configPath;
extern std::vector<std::wstring> g_gameExeNames;
extern std::unordered_map<std::wstring, std::wstring> g_gameExePaths;
extern std::unordered_map<std::wstring, GameProfile> g_gameProfiles;
extern bool g_protectionPaused;
extern ULONGLONG g_pauseUntil;
extern UINT g_emergencyKey;
extern std::wstring g_configError;
extern UINT g_gameSession;
extern bool g_portableMode;
}
