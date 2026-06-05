#include "AppState.h"

namespace FFKeyLock
{
HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
HWND g_titleText = nullptr;
HWND g_statusText = nullptr;
HWND g_protectionButton = nullptr;
HWND g_autoDetectButton = nullptr;
HWND g_startupButton = nullptr;
HWND g_windowsKeyButton = nullptr;
HWND g_switchEnglishButton = nullptr;
HWND g_switchChineseButton = nullptr;
HWND g_gameListLabel = nullptr;
HWND g_gameListHelpButton = nullptr;
HWND g_addCurrentButton = nullptr;
HWND g_addFileButton = nullptr;
HWND g_deleteGameButton = nullptr;
HWND g_browseProtectedButton = nullptr;
HWND g_gameList = nullptr;
HWND g_configText = nullptr;
UINT g_taskbarCreatedMessage = 0;
bool g_protectionEnabled = true;
bool g_autoDetectEnabled = true;
bool g_windowsKeyGuardEnabled = false;
bool g_notificationsEnabled = true;
bool g_overlayNotificationsEnabled = true;
bool g_inGameProtection = false;
bool g_chatInputSuspended = false;
DWORD g_chatInputSuspendUntil = 0;
HKL g_savedLayout = nullptr;
HWND g_savedWindow = nullptr;
HWND g_menuTargetWindow = nullptr;
HWND g_lastExternalForegroundWindow = nullptr;
HICON g_trayIcon = nullptr;
UiLanguage g_language = UiLanguage::Chinese;
ThemePreference g_themePreference = ThemePreference::System;
std::wstring g_configPath;
std::vector<std::wstring> g_gameExeNames;
std::unordered_map<std::wstring, std::wstring> g_gameExePaths;
}
