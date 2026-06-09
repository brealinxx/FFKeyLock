#pragma once

#include "framework.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace FFKeyLock
{
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT_PTR TIMER_GAME_DETECT = 1;
constexpr UINT DETECT_INTERVAL_MS = 1000;
constexpr wchar_t kAppName[] = L"FFKeyLock";
constexpr wchar_t kConfigSection[] = L"Settings";
constexpr wchar_t kGamesKey[] = L"Games";
constexpr wchar_t kGamePathsKey[] = L"GamePaths";
constexpr wchar_t kProtectionKey[] = L"ProtectionEnabled";
constexpr wchar_t kAutoDetectKey[] = L"AutoDetectEnabled";
constexpr wchar_t kWindowsKeyGuardKey[] = L"WindowsKeyGuardEnabled";
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

extern HINSTANCE g_hInst;
extern HWND g_hWnd;
extern HWND g_titleText;
extern HWND g_subtitleText;
extern HWND g_appIcon;
extern HWND g_statusText;
extern HWND g_statusDetectedText;
extern HWND g_statusInputText;
extern HWND g_statusWinKeyText;
extern HWND g_protectionLabel;
extern HWND g_protectionStateText;
extern HWND g_protectionButton;
extern HWND g_autoDetectLabel;
extern HWND g_autoDetectStateText;
extern HWND g_autoDetectButton;
extern HWND g_startupLabel;
extern HWND g_startupStateText;
extern HWND g_startupButton;
extern HWND g_windowsKeyLabel;
extern HWND g_windowsKeyStateText;
extern HWND g_windowsKeyButton;
extern HWND g_inputLanguageText;
extern HWND g_switchEnglishButton;
extern HWND g_switchChineseButton;
extern HWND g_gameListLabel;
extern HWND g_gameListHelpButton;
extern HWND g_addCurrentButton;
extern HWND g_addFileButton;
extern HWND g_deleteGameButton;
extern HWND g_browseProtectedButton;
extern HWND g_configText;
extern UINT g_taskbarCreatedMessage;
extern bool g_protectionEnabled;
extern bool g_autoDetectEnabled;
extern bool g_windowsKeyGuardEnabled;
extern bool g_notificationsEnabled;
extern bool g_overlayNotificationsEnabled;
extern bool g_inGameProtection;
extern bool g_chatInputSuspended;
extern DWORD g_chatInputSuspendUntil;
extern std::wstring g_currentDetectedGameName;
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
}
