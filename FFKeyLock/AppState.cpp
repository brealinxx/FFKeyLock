#include "AppState.h"

namespace FFKeyLock
{
HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
UINT g_taskbarCreatedMessage = 0;
bool g_protectionEnabled = true;
bool g_autoDetectEnabled = true;
bool g_windowsKeyGuardEnabled = false;
WindowsKeyGuardScope g_windowsKeyGuardScope = WindowsKeyGuardScope::ProtectedForeground;
bool g_notificationsEnabled = true;
bool g_overlayNotificationsEnabled = true;
bool g_inGameProtection = false;
bool g_chatInputSuspended = false;
ULONGLONG g_chatInputSuspendUntil = 0;
UINT g_chatActiveKey = 0;
std::wstring g_currentDetectedGameName;
std::wstring g_activeGameExeName;
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
std::unordered_map<std::wstring, GameProfile> g_gameProfiles;
bool g_protectionPaused = false;
ULONGLONG g_pauseUntil = 0;
UINT g_emergencyKey = VK_BACK;
std::wstring g_configError;
UINT g_gameSession = 0;
bool g_portableMode = false;
}
