#pragma once

#include "framework.h"

#include <string>
#include <vector>

namespace FFKeyLock
{
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT_PTR TIMER_GAME_DETECT = 1;
constexpr UINT DETECT_INTERVAL_MS = 1000;
constexpr wchar_t kAppName[] = L"FFKeyLock";
constexpr wchar_t kConfigSection[] = L"Settings";
constexpr wchar_t kGamesKey[] = L"Games";
constexpr wchar_t kProtectionKey[] = L"ProtectionEnabled";
constexpr wchar_t kAutoDetectKey[] = L"AutoDetectEnabled";
constexpr wchar_t kWindowsKeyGuardKey[] = L"WindowsKeyGuardEnabled";
constexpr wchar_t kLanguageKey[] = L"Language";
constexpr wchar_t kStartupRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

enum class UiLanguage
{
    Chinese,
    English
};

extern HINSTANCE g_hInst;
extern HWND g_hWnd;
extern HWND g_statusText;
extern HWND g_protectionButton;
extern HWND g_autoDetectButton;
extern HWND g_startupButton;
extern HWND g_windowsKeyButton;
extern HWND g_switchEnglishButton;
extern HWND g_switchChineseButton;
extern HWND g_gameListLabel;
extern HWND g_addCurrentButton;
extern HWND g_addFileButton;
extern HWND g_gameList;
extern HWND g_configText;
extern UINT g_taskbarCreatedMessage;
extern bool g_protectionEnabled;
extern bool g_autoDetectEnabled;
extern bool g_windowsKeyGuardEnabled;
extern bool g_inGameProtection;
extern HKL g_savedLayout;
extern HWND g_savedWindow;
extern HWND g_menuTargetWindow;
extern HWND g_lastExternalForegroundWindow;
extern HICON g_trayIcon;
extern UiLanguage g_language;
extern std::wstring g_configPath;
extern std::vector<std::wstring> g_gameExeNames;
}
