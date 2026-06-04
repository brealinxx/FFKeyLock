#include "AppState.h"

namespace FFKeyLock
{
HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
HWND g_statusText = nullptr;
HWND g_protectionButton = nullptr;
HWND g_autoDetectButton = nullptr;
HWND g_startupButton = nullptr;
HWND g_windowsKeyButton = nullptr;
HWND g_switchEnglishButton = nullptr;
HWND g_switchChineseButton = nullptr;
HWND g_gameListLabel = nullptr;
HWND g_addCurrentButton = nullptr;
HWND g_addFileButton = nullptr;
HWND g_gameList = nullptr;
HWND g_configText = nullptr;
UINT g_taskbarCreatedMessage = 0;
bool g_protectionEnabled = true;
bool g_autoDetectEnabled = true;
bool g_windowsKeyGuardEnabled = false;
bool g_inGameProtection = false;
HKL g_savedLayout = nullptr;
HWND g_savedWindow = nullptr;
HWND g_menuTargetWindow = nullptr;
HWND g_lastExternalForegroundWindow = nullptr;
HICON g_trayIcon = nullptr;
UiLanguage g_language = UiLanguage::Chinese;
std::wstring g_configPath;
std::vector<std::wstring> g_gameExeNames;
}
