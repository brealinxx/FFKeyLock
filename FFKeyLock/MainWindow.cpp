#include "UI/Rendering/NativeControls.h"
#include "MainWindow.h"
#include "AppState.h"
#include "Config.h"
#include "GameProtection.h"
#include "InputLanguage.h"
#include "Localization.h"
#include "Logger.h"
#include "OverlayNotificationManager.h"
#include "Resource.h"
#include "StringUtils.h"
#include "ThemeManager.h"
#include "TrayIcon.h"
#include "Version.h"
#include "WindowsKeyGuard.h"
#include "UI/Main/MainContentView.h"
#include "UI/Menu/MenuBar.h"
#include "UI/Menu/AppMenus.h"
#include "UI/Menu/PopupMenu.h"
#include "UI/Rendering/Surface.h"
#include "UI/Windows/RunningProgramPicker.h"
#include "UI/Profiles/ProfileEditor.h"
#include "UI/ProtectedPrograms/ProtectedProgramCommands.h"
#include <algorithm>
#include <commdlg.h>
#include <filesystem>
#include <shellapi.h>
#include <windowsx.h>

#pragma comment(lib, "Comdlg32.lib")
namespace FFKeyLock
{
namespace
{
MainContentView g_view;
UI::MenuBar g_menuBar;
bool g_quitting = false;
bool g_destroying = false;
bool g_dirtyView = false;

bool External(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return false;
    const auto name = GetExeNameFromPath(GetProgramPath(hwnd));
    return !name.empty() && name != L"explorer.exe" && name != L"shellexperiencehost.exe" && name != L"startmenuexperiencehost.exe";
}
void BuildMenus() { g_menuBar.SetMenu(UI::CreateAppMenu()); }

std::wstring ChooseFile(bool save, bool exe)
{
    wchar_t path[32768]{};
    if (save) wcscpy_s(path,L"FFKeyLock-profiles.ini");
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = g_hWnd;
    dialog.lpstrFile = path; dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrFilter = exe ? L"Programs (*.exe)\0*.exe\0\0" : L"FFKeyLock profiles (*.ini)\0*.ini\0\0";
    dialog.lpstrDefExt = exe ? L"exe" : L"ini";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) return path;
    return L"";
}

void RefreshTheme()
{
    ThemeManager::Initialize(GetDpiForWindow(g_hWnd)); ThemeManager::ApplyDarkTitleBar(g_hWnd);
    g_view.Refresh(true); BuildMenus(); InvalidateRect(g_hWnd,nullptr,TRUE);
}
void Quit()
{
    if (!g_view.ConfirmDiscard()) return;
    g_quitting = true; DestroyWindow(g_hWnd);
}
void AddPath(const std::wstring& path)
{
    if (path.empty()) return;
    AddGameExeName(path); DetectForegroundGame(); g_view.Refresh();
    const std::wstring identity = ToLower(std::filesystem::path(path).lexically_normal().wstring());
    if (g_gameProfiles.contains(identity)) g_view.Select(identity);
}

void Command(UINT id)
{
    bool save = false;
    switch (id)
    {
    case IDM_SHOW_WINDOW: case IDM_SHOW_SETTINGS: ShowMainWindow(); return;
    case IDM_EXIT: Quit(); return;
    case IDM_PAUSE_30: PauseProtection(30000); return;
    case IDM_PAUSE_300: PauseProtection(300000); return;
    case IDM_PAUSE_MANUAL: PauseProtection(); return;
    case IDM_RESUME: ResumeProtection(); return;
    case IDM_PROTECTION: g_protectionEnabled = !g_protectionEnabled; save = true; break;
    case IDM_AUTO_DETECT: g_autoDetectEnabled = !g_autoDetectEnabled; save = true; break;
    case IDM_WINDOWS_KEY_GUARD: ToggleWindowsKeyGuard(); BuildMenus(); return;
    case IDM_WINKEY_SCOPE_PROTECTED: g_windowsKeyGuardScope = WindowsKeyGuardScope::ProtectedForeground; save = true; break;
    case IDM_WINKEY_SCOPE_ALWAYS: g_windowsKeyGuardScope = WindowsKeyGuardScope::Always; save = true; break;
    case IDM_STARTUP: SetStartupEnabled(!IsStartupEnabled()); BuildMenus(); return;
    case IDM_SWITCH_ENGLISH: SwitchToEnglish(GetCommandTargetWindow()); UpdateMainWindow(); return;
    case IDM_SWITCH_CHINESE: SwitchToChinese(GetCommandTargetWindow()); UpdateMainWindow(); return;
    case IDM_COPY_GAME_NAME: ProtectedProgramCommands::CopyNameToClipboard(g_hWnd, GameDisplayName(g_view.SelectedName())); return;
    case IDM_RETRY_HOOK: DisableWindowsKeyGuard(); ApplyWindowsKeyGuard(); UpdateMainWindow(); return;
    case IDM_ADD_GAME_FILE: if (g_view.ConfirmDiscard()) AddPath(ChooseFile(false,true)); return;
    case IDM_RUNNING_PROGRAMS: if (g_view.ConfirmDiscard()) AddPath(UI::ChooseRunningProgram(g_hWnd)); return;
    case IDM_ADD_CURRENT_GAME: if (g_view.ConfirmDiscard()) AddPath(GetProgramPath(GetCommandTargetWindow())); return;
    case IDM_COPY_PROFILE: g_view.CopyProfile(); return;
    case IDM_PASTE_PROFILE: g_view.PasteProfile(); return;
    case IDM_OPEN_GAME_FOLDER:
    {
        const auto name = g_view.SelectedName(); const auto it = g_gameExePaths.find(name);
        ProtectedProgramCommands::OpenProgramFolder(g_hWnd,name,it == g_gameExePaths.end() ? L"" : it->second); return;
    }
    case IDM_DELETE_SELECTED_GAME:
    {
        if (!g_view.ConfirmDiscard()) return;
        const auto name = g_view.SelectedName(); if (name.empty()) return;
        if (name == g_activeGameExeName) LeaveGameProtection();
        g_gameExeNames.erase(std::remove(g_gameExeNames.begin(),g_gameExeNames.end(),name),g_gameExeNames.end());
        g_gameExePaths.erase(name); g_gameProfiles.erase(name); save = true; break;
    }
    case IDM_EXPORT_PROFILES: case IDM_IMPORT_PROFILES:
    {
        if (!g_view.ConfirmDiscard()) return;
        const auto path = ChooseFile(id == IDM_EXPORT_PROFILES,false); if (path.empty()) return;
        if (id == IDM_IMPORT_PROFILES && MessageBoxW(g_hWnd,Text(L"导入会合并游戏库，并替换身份相同的游戏配置。是否继续？",L"Import merges games and replaces profiles with the same identity. Continue?"),L"FFKeyLock",MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        StopKeyboardTest(); LeaveGameProtection();
        const bool ok = id == IDM_EXPORT_PROFILES ? ExportProfiles(path) : ImportProfiles(path);
        MessageBoxW(g_hWnd,ok ? Text(L"配置库操作完成。",L"Profile library operation completed.") : Text(L"操作失败。请检查文件格式、版本和目录权限；现有配置库未被替换。",L"Operation failed. Check file format, version and permissions. The existing library was not replaced."),L"FFKeyLock",MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        g_view.Refresh(); g_view.Select(g_view.SelectedName()); DetectForegroundGame(); return;
    }
    case IDM_EMERGENCY_BACK: case IDM_EMERGENCY_END: case IDM_EMERGENCY_HOME:
    {
        if (!g_view.ConfirmDiscard()) return;
        const UINT key = id == IDM_EMERGENCY_BACK ? VK_BACK : id == IDM_EMERGENCY_END ? VK_END : VK_HOME;
        for (const auto& [name,profile] : g_gameProfiles)
            if (std::find(profile.blockedKeys.begin(),profile.blockedKeys.end(),key) != profile.blockedKeys.end())
            { MessageBoxW(g_hWnd,Text(L"这个按键已被某个游戏锁定，请先在该游戏配置中放行。",L"A game blocks this key. Allow it in that profile first."),L"FFKeyLock",MB_OK | MB_ICONWARNING); return; }
        g_emergencyKey = key; g_view.Select(g_view.SelectedName()); save = true; break;
    }
    case IDM_NOTIFICATIONS: g_notificationsEnabled = !g_notificationsEnabled; save = true; break;
    case IDM_OVERLAY_NOTIFICATIONS: g_overlayNotificationsEnabled = !g_overlayNotificationsEnabled; save = true; break;
    case IDM_MUTE_NOTIFICATIONS: g_notificationsEnabled = false; g_overlayNotificationsEnabled = false; save = true; break;
    case IDM_TEST_NOTIFICATION: ShowTrayNotification(L"FFKeyLock",Text(L"游戏保护通知测试",L"Game protection notification test"),true); return;
    case IDM_THEME_SYSTEM: case IDM_THEME_LIGHT: case IDM_THEME_DARK:
        g_themePreference = id == IDM_THEME_SYSTEM ? ThemePreference::System : id == IDM_THEME_LIGHT ? ThemePreference::Light : ThemePreference::Dark;
        SaveConfig(); RefreshTheme(); return;
    case IDM_LANGUAGE_CHINESE: case IDM_LANGUAGE_ENGLISH:
        g_language = id == IDM_LANGUAGE_CHINESE ? UiLanguage::Chinese : UiLanguage::English;
        SaveConfig(); RefreshTheme(); return;
    case IDM_OPEN_CONFIG_DIR: ShellExecuteW(g_hWnd,L"open",std::filesystem::path(g_configPath).parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL); return;
    case IDM_OPEN_LOG_DIR: ShellExecuteW(g_hWnd,L"open",GetLogDirectory().c_str(),nullptr,nullptr,SW_SHOWNORMAL); return;
    case IDM_RESET_CONFIG:
        if (MessageBoxW(g_hWnd,Text(L"重置并清空游戏配置库？",L"Reset settings and clear all game profiles?"),L"FFKeyLock",MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        LeaveGameProtection(); g_gameExeNames.clear(); g_gameExePaths.clear(); g_gameProfiles.clear();
        g_protectionEnabled = true; g_autoDetectEnabled = true; g_windowsKeyGuardEnabled = false;
        g_windowsKeyGuardScope = WindowsKeyGuardScope::ProtectedForeground; g_notificationsEnabled = true; g_overlayNotificationsEnabled = true;
        g_emergencyKey = VK_BACK; g_protectionPaused = false; g_pauseUntil = 0; KillTimer(g_hWnd,TIMER_PAUSE); save = true; break;
    case IDM_CLEAR_LOCAL_DATA:
        if (MessageBoxW(g_hWnd,g_portableMode ? Text(L"删除当前便携配置文件并退出？备份文件会保留。", L"Delete the current portable configuration and exit? Backup files are kept.") : Text(L"删除配置、日志、开机启动项及通知快捷方式，然后退出？",L"Delete configuration, logs, startup entry and notification shortcut, then exit?"),L"FFKeyLock",MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        LeaveGameProtection(); DisableWindowsKeyGuard();
        if (!ClearLocalDataAndRegistry()) MessageBoxW(g_hWnd,Text(L"部分文件无法删除，请退出后检查。",L"Some files could not be deleted. Check after exit."),L"FFKeyLock",MB_OK | MB_ICONWARNING);
        g_quitting = true; DestroyWindow(g_hWnd); return;
    case IDM_HELP_USAGE:
        MessageBoxW(g_hWnd,Text(L"1. 选择游戏文件或运行中的游戏窗口。\n2. 点击防误触预设，放行游戏需要的键并保存。\n3. 返回游戏自动应用，切到桌面自动解除。\n\n锁键和输入法可以分别开关。聊天不会解除锁键。\n紧急解除快捷键可在设置中选择，或使用托盘暂停。\n测试配置仅影响本软件窗口；具体游戏请实测。",L"1. Choose the game's executable or running window.\n2. Apply the accidental press preset, allow required keys, and save.\n3. Return to the game to apply; switch away to release.\n\nKey blocking and input language work independently. Chat does not release blocked keys.\nChoose an emergency shortcut in Settings, or pause from the tray.\nThe key test affects only this window; verify behavior in your game."),L"FFKeyLock",MB_OK); return;
    case IDM_CHECK_UPDATES: ShellExecuteW(g_hWnd,L"open",L"https://github.com/brealinxx/FFKeyLock/releases/latest",nullptr,nullptr,SW_SHOWNORMAL); return;
    case IDM_ABOUT:
        MessageBoxW(g_hWnd,(std::wstring(L"FFKeyLock ") + FFKEYLOCK_VERSION_TEXT_W + Text(L"\n原生 Win32 游戏防误触助手\nAssembly by brealin",L"\nNative Win32 game key protection\nAssembly by brealin")).c_str(),L"FFKeyLock",MB_OK); return;
    }
    if (save) SaveConfig();
    DetectForegroundGame(); RefreshKeyboardPolicy(); UpdateMainWindow(); BuildMenus();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    if (message == g_taskbarCreatedMessage && g_taskbarCreatedMessage) { AddTrayIcon(); return 0; }
    switch (message)
    {
    case WM_CREATE:
        g_hWnd = hwnd; ThemeManager::Initialize(GetDpiForWindow(hwnd)); ThemeManager::ApplyDarkTitleBar(hwnd);
        g_menuBar.Create(hwnd); g_view.Create(hwnd); AddTrayIcon(); ApplyWindowsKeyGuard(); BuildMenus();
        SetTimer(hwnd,TIMER_GAME_DETECT,InitializeForegroundDetection() ? DETECT_FALLBACK_INTERVAL_MS : DETECT_RECOVERY_INTERVAL_MS,nullptr);
        DetectForegroundGame(); return 0;
    case WM_GAME_CHAT_KEY:
        if (static_cast<UINT>(l >> 1) == g_gameSession) HandleGameChatKey(static_cast<UINT>(w),(l & 1) != 0);
        return 0;
    case WM_EMERGENCY_UNLOCK: StopKeyboardTest(); PauseProtection(); return 0;
    case WM_FOREGROUND_CHANGED:
        if (GetForegroundWindow() != hwnd) StopKeyboardTest();
        DetectForegroundGame(); return 0;
    case WM_TIMER:
        if (w == TIMER_GAME_DETECT) DetectForegroundGame();
        else if (w == TIMER_CHAT_TIMEOUT) ResumeGameProtectionAfterChatTimeout();
        else if (w == TIMER_PAUSE && g_pauseUntil && GetTickCount64() >= g_pauseUntil) ResumeProtection();
        return 0;
    case WM_SIZE:
        { const int bar = MulDiv(36, GetDpiForWindow(hwnd), 96);
          if (g_menuBar.Window()) SetWindowPos(g_menuBar.Window(),nullptr,0,0,LOWORD(l),bar,SWP_NOACTIVATE | SWP_NOZORDER);
          if (g_view.Window()) SetWindowPos(g_view.Window(),nullptr,0,bar,LOWORD(l),std::max(0,static_cast<int>(HIWORD(l))-bar),SWP_NOACTIVATE | SWP_NOZORDER); }
        return 0;
    case WM_SHOWWINDOW: if (w && g_dirtyView) { g_dirtyView = false; g_view.Refresh(); } return 0;
    case WM_DPICHANGED:
    {
        const auto* r = reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER | SWP_NOACTIVATE);
        RefreshTheme(); return 0;
    }
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: RefreshTheme(); return 0;
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(l); const UINT dpi = GetDpiForWindow(hwnd);
        info->ptMinTrackSize = {MulDiv(850,dpi,96),MulDiv(600,dpi,96)}; return 0;
    }
    case WM_COMMAND: Command(LOWORD(w)); return 0;
    case WM_TRAYICON:
        if (LOWORD(l) == WM_CONTEXTMENU || LOWORD(l) == WM_RBUTTONUP) ShowTrayMenu();
        else if (LOWORD(l) == WM_LBUTTONDBLCLK || LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT) ShowMainWindow();
        return 0;
    case WM_PAINT: case WM_PRINTCLIENT: case WM_ERASEBKGND: return UI::PaintSurface(hwnd, message, w);
    case WM_DRAWITEM: return UI::PopupMenuTheme::Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l));
    case WM_MEASUREITEM: return UI::PopupMenuTheme::Measure(*reinterpret_cast<MEASUREITEMSTRUCT*>(l));
    case WM_MENUCHAR: return UI::PopupMenuTheme::MenuChar(w, reinterpret_cast<HMENU>(l));
    case WM_CLOSE:
        if (g_quitting) DestroyWindow(hwnd);
        else { StopKeyboardTest(); ShowWindow(hwnd,SW_HIDE); }
        return 0;
    case WM_DESTROY:
        g_destroying = true; KillTimer(hwnd,TIMER_GAME_DETECT); KillTimer(hwnd,TIMER_CHAT_TIMEOUT); KillTimer(hwnd,TIMER_PAUSE);
        ShutdownForegroundDetection(); StopKeyboardTest(); LeaveGameProtection(); DisableWindowsKeyGuard();
        OverlayNotificationManager::Shutdown(); RemoveTrayIcon(); ThemeManager::Shutdown(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}

bool TranslateMainMessage(MSG& message) { return g_menuBar.Translate(message); }

void UpdateMainWindow()
{
    if (g_destroying) return;
    RefreshKeyboardPolicy();
    if (g_hWnd && IsWindowVisible(g_hWnd) && !IsIconic(g_hWnd)) g_view.Refresh();
    else g_dirtyView = true;
}
void ShowMainWindow()
{
    ShowWindow(g_hWnd,SW_RESTORE); SetForegroundWindow(g_hWnd); g_view.Refresh();
}
void RememberExternalForegroundWindow(HWND hwnd)
{
    if (External(hwnd)) g_lastExternalForegroundWindow = GetAncestor(hwnd,GA_ROOT);
}
HWND GetCommandTargetWindow()
{
    if (External(g_menuTargetWindow)) return g_menuTargetWindow;
    if (External(GetForegroundWindow())) return GetForegroundWindow();
    return External(g_lastExternalForegroundWindow) ? g_lastExternalForegroundWindow : nullptr;
}
void ShowTrayMenu()
{
    RememberExternalForegroundWindow(GetForegroundWindow()); g_menuTargetWindow = GetCommandTargetWindow();
    HMENU menu = UI::CreateTrayMenu();
    POINT pt{}; GetCursorPos(&pt); SetForegroundWindow(g_hWnd);
    const UINT command = UI::ShowPopupMenu(g_hWnd, menu, pt);
    DestroyMenu(menu); PostMessageW(g_hWnd,WM_NULL,0,0);
    if (command) Command(command);
    g_menuTargetWindow = nullptr;
}
ATOM RegisterMainWindowClass(HINSTANCE instance)
{
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc; wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance,MAKEINTRESOURCEW(IDI_FFKEYLOCK)); wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName = kAppName;
    return RegisterClassExW(&wc);
}
}
