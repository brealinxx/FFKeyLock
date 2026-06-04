#include "MainWindow.h"

#include "AppState.h"
#include "Config.h"
#include "GameProtection.h"
#include "InputLanguage.h"
#include "Localization.h"
#include "Resource.h"
#include "TrayIcon.h"
#include "WindowsKeyGuard.h"

#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <string>

namespace FFKeyLock
{
namespace
{
void SetDefaultFont(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

HWND CreateControl(const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id)
{
    HWND control = CreateWindowExW(
        0,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        g_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_hInst,
        nullptr);

    if (control)
    {
        SetDefaultFont(control);
    }
    return control;
}

HMENU CreateAppMenu()
{
    HMENU menuBar = CreateMenu();
    HMENU settingsMenu = CreatePopupMenu();
    HMENU languageMenu = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();

    AppendMenuW(languageMenu, MF_STRING | (!IsEnglish() ? MF_CHECKED : MF_UNCHECKED), IDM_LANGUAGE_CHINESE, Text(L"中文", L"Chinese"));
    AppendMenuW(languageMenu, MF_STRING | (IsEnglish() ? MF_CHECKED : MF_UNCHECKED), IDM_LANGUAGE_ENGLISH, Text(L"English", L"English"));

    AppendMenuW(settingsMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(languageMenu), Text(L"语言", L"Language"));
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu, MF_STRING | (g_windowsKeyGuardEnabled ? MF_CHECKED : MF_UNCHECKED), IDM_WINDOWS_KEY_GUARD,
        g_windowsKeyGuardEnabled ? Text(L"Win 键：禁用", L"Windows key: Disabled") : Text(L"Win 键：开启", L"Windows key: Enabled"));
    AppendMenuW(settingsMenu, MF_STRING, IDM_SWITCH_ENGLISH, Text(L"切换输入法到英文", L"Switch input to English"));
    AppendMenuW(settingsMenu, MF_STRING, IDM_SWITCH_CHINESE, Text(L"切换输入法到中文", L"Switch input to Chinese"));

    AppendMenuW(helpMenu, MF_STRING, IDM_ABOUT, Text(L"关于 FFKeyLock", L"About FFKeyLock"));

    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(settingsMenu), Text(L"设置", L"Settings"));
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), Text(L"帮助", L"Help"));
    return menuBar;
}

std::wstring BuildStatusText()
{
    std::wstring status = Text(L"当前状态：", L"Status: ");
    status += g_inGameProtection ? Text(L"游戏保护中", L"Protecting game") : Text(L"普通", L"Normal");
    status += Text(L"    保护模式：", L"    Protection: ");
    status += g_protectionEnabled ? Text(L"开启", L"On") : Text(L"关闭", L"Off");
    status += Text(L"    自动检测：", L"    Auto detect: ");
    status += g_autoDetectEnabled ? Text(L"开启", L"On") : Text(L"关闭", L"Off");
    status += Text(L"    Win 键：", L"    Windows key: ");
    status += g_windowsKeyGuardEnabled ? Text(L"禁用", L"Disabled") : Text(L"开启", L"Enabled");
    return status;
}

void RefreshGameList()
{
    if (!g_gameList)
    {
        return;
    }

    SendMessageW(g_gameList, LB_RESETCONTENT, 0, 0);
    for (const auto& game : g_gameExeNames)
    {
        SendMessageW(g_gameList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(game.c_str()));
    }
}

void CreateMainControls()
{
    CreateControl(L"STATIC", L"FFKeyLock", SS_LEFT, 20, 18, 180, 24, IDC_STATIC);
    g_statusText = CreateControl(L"STATIC", L"", SS_LEFT, 20, 48, 600, 22, IDC_STATUS_TEXT);

    g_protectionButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 20, 84, 140, 32, IDC_PROTECTION_BUTTON);
    g_autoDetectButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 174, 84, 140, 32, IDC_AUTO_DETECT_BUTTON);
    g_startupButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 328, 84, 140, 32, IDC_STARTUP_BUTTON);
    g_windowsKeyButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 482, 84, 120, 32, IDC_WINDOWS_KEY_BUTTON);

    g_switchEnglishButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 20, 132, 140, 32, IDC_SWITCH_EN_BUTTON);
    g_switchChineseButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 174, 132, 140, 32, IDC_SWITCH_CN_BUTTON);

    g_gameListLabel = CreateControl(L"STATIC", L"", SS_LEFT, 20, 190, 180, 20, IDC_STATIC);
    g_gameList = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        20,
        216,
        430,
        150,
        g_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_GAME_LIST)),
        g_hInst,
        nullptr);
    if (g_gameList)
    {
        SetDefaultFont(g_gameList);
    }

    g_addCurrentButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 470, 216, 110, 30, IDC_ADD_GAME_BUTTON);
    g_addFileButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 470, 258, 110, 30, IDC_ADD_FILE_BUTTON);
    g_configText = CreateControl(L"STATIC", L"", SS_LEFT, 20, 386, 560, 40, IDC_CONFIG_TEXT);
    UpdateMainWindow();
}

void ResizeMainControls(int width, int height)
{
    const int margin = 20;
    const int rightButtonWidth = 110;
    const int rightButtonX = std::max(470, width - margin - rightButtonWidth);
    const int listWidth = std::max(260, rightButtonX - margin - 20);
    const int listHeight = std::max(120, height - 330);
    const int configY = 216 + listHeight + 20;

    if (g_statusText)
    {
        MoveWindow(g_statusText, 20, 48, std::max(260, width - 40), 22, TRUE);
    }
    if (g_gameList)
    {
        MoveWindow(g_gameList, 20, 216, listWidth, listHeight, TRUE);
    }
    if (g_addCurrentButton)
    {
        MoveWindow(g_addCurrentButton, rightButtonX, 216, rightButtonWidth, 30, TRUE);
    }
    if (g_addFileButton)
    {
        MoveWindow(g_addFileButton, rightButtonX, 258, rightButtonWidth, 30, TRUE);
    }
    if (g_configText)
    {
        MoveWindow(g_configText, 20, configY, std::max(260, width - 40), 40, TRUE);
    }
}

bool IsOwnWindow(HWND hwnd)
{
    return hwnd && g_hWnd && GetAncestor(hwnd, GA_ROOT) == g_hWnd;
}

void AddGameFromFileDialog()
{
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = IsEnglish()
        ? L"Executable files (*.exe)\0*.exe\0All files (*.*)\0*.*\0"
        : L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = static_cast<DWORD>(std::size(path));
    ofn.lpstrTitle = Text(L"选择游戏程序", L"Select game executable");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
    {
        AddGameExeName(path);
    }
}

void AppendMenuItem(HMENU menu, UINT id, const std::wstring& text, bool checked = false, bool enabled = true)
{
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
    item.wID = id;
    item.dwTypeData = const_cast<LPWSTR>(text.c_str());
    item.fState = (checked ? MFS_CHECKED : MFS_UNCHECKED) | (enabled ? MFS_ENABLED : MFS_DISABLED);
    InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &item);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == g_taskbarCreatedMessage)
    {
        AddTrayIcon();
        return 0;
    }

    switch (message)
    {
    case WM_CREATE:
        g_hWnd = hWnd;
        SetMenu(hWnd, CreateAppMenu());
        AddTrayIcon();
        CreateMainControls();
        SetTimer(hWnd, TIMER_GAME_DETECT, DETECT_INTERVAL_MS, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_GAME_DETECT)
        {
            DetectForegroundGame();
        }
        return 0;

    case WM_SIZE:
        ResizeMainControls(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_GETMINMAXINFO:
    {
        auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
        minMaxInfo->ptMinTrackSize.x = 640;
        minMaxInfo->ptMinTrackSize.y = 500;
        return 0;
    }

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP)
        {
            ShowTrayMenu();
        }
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
        {
            ShowMainWindow();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_SHOW_WINDOW:
            ShowMainWindow();
            return 0;

        case IDM_PROTECTION:
        case IDC_PROTECTION_BUTTON:
            g_protectionEnabled = !g_protectionEnabled;
            if (!g_protectionEnabled)
            {
                LeaveGameProtection();
            }
            SaveConfig();
            UpdateMainWindow();
            return 0;

        case IDM_AUTO_DETECT:
        case IDC_AUTO_DETECT_BUTTON:
            g_autoDetectEnabled = !g_autoDetectEnabled;
            if (!g_autoDetectEnabled)
            {
                LeaveGameProtection();
            }
            SaveConfig();
            UpdateMainWindow();
            return 0;

        case IDM_SWITCH_ENGLISH:
        case IDC_SWITCH_EN_BUTTON:
            SwitchToEnglish(GetCommandTargetWindow());
            return 0;

        case IDM_SWITCH_CHINESE:
        case IDC_SWITCH_CN_BUTTON:
            SwitchToChinese(GetCommandTargetWindow());
            return 0;

        case IDM_ADD_CURRENT_GAME:
        case IDC_ADD_GAME_BUTTON:
            AddProgramAsGame(GetCommandTargetWindow());
            UpdateMainWindow();
            return 0;

        case IDM_ADD_GAME_FILE:
        case IDC_ADD_FILE_BUTTON:
            AddGameFromFileDialog();
            return 0;

        case IDM_STARTUP:
        case IDC_STARTUP_BUTTON:
            SetStartupEnabled(!IsStartupEnabled());
            UpdateMainWindow();
            return 0;

        case IDM_WINDOWS_KEY_GUARD:
        case IDC_WINDOWS_KEY_BUTTON:
            ToggleWindowsKeyGuard();
            return 0;

        case IDM_LANGUAGE_CHINESE:
            g_language = UiLanguage::Chinese;
            SaveConfig();
            UpdateMainWindow();
            return 0;

        case IDM_LANGUAGE_ENGLISH:
            g_language = UiLanguage::English;
            SaveConfig();
            UpdateMainWindow();
            return 0;

        case IDM_ABOUT:
            MessageBoxW(hWnd,
                Text(L"FFKeyLock\n\n轻量级 Win32 游戏输入法保护工具。\n\nVersion 1.0",
                    L"FFKeyLock\n\nLightweight Win32 game input-language protection utility.\n\nVersion 1.0"),
                Text(L"关于 FFKeyLock", L"About FFKeyLock"),
                MB_OK | MB_ICONINFORMATION);
            return 0;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        ShowTrayNotification(L"FFKeyLock", Text(L"窗口已隐藏，程序仍在托盘运行。", L"The window is hidden. FFKeyLock is still running in the tray."));
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_GAME_DETECT);
        LeaveGameProtection();
        DisableWindowsKeyGuard();
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}
}

void UpdateMainWindow()
{
    if (g_hWnd)
    {
        SetWindowTextW(g_hWnd, kAppName);
        HMENU oldMenu = GetMenu(g_hWnd);
        SetMenu(g_hWnd, CreateAppMenu());
        if (oldMenu)
        {
            DestroyMenu(oldMenu);
        }
        DrawMenuBar(g_hWnd);
    }
    if (g_statusText)
    {
        SetWindowTextW(g_statusText, BuildStatusText().c_str());
    }
    if (g_protectionButton)
    {
        SetWindowTextW(g_protectionButton, g_protectionEnabled ? Text(L"关闭保护模式", L"Disable protection") : Text(L"开启保护模式", L"Enable protection"));
    }
    if (g_autoDetectButton)
    {
        SetWindowTextW(g_autoDetectButton, g_autoDetectEnabled ? Text(L"关闭自动检测", L"Disable auto detect") : Text(L"开启自动检测", L"Enable auto detect"));
    }
    if (g_startupButton)
    {
        SetWindowTextW(g_startupButton, IsStartupEnabled() ? Text(L"关闭开机启动", L"Disable startup") : Text(L"开启开机启动", L"Enable startup"));
    }
    if (g_windowsKeyButton)
    {
        SetWindowTextW(g_windowsKeyButton, g_windowsKeyGuardEnabled ? Text(L"开启 Win 键", L"Enable Win key") : Text(L"禁用 Win 键", L"Disable Win key"));
    }
    if (g_switchEnglishButton)
    {
        SetWindowTextW(g_switchEnglishButton, Text(L"切换为英文", L"Switch to English"));
    }
    if (g_switchChineseButton)
    {
        SetWindowTextW(g_switchChineseButton, Text(L"切换为中文", L"Switch to Chinese"));
    }
    if (g_gameListLabel)
    {
        SetWindowTextW(g_gameListLabel, Text(L"游戏程序列表", L"Game executable list"));
    }
    if (g_addCurrentButton)
    {
        SetWindowTextW(g_addCurrentButton, Text(L"添加当前", L"Add current"));
    }
    if (g_addFileButton)
    {
        SetWindowTextW(g_addFileButton, Text(L"浏览添加...", L"Browse..."));
    }
    if (g_configText)
    {
        SetWindowTextW(g_configText, (std::wstring(Text(L"配置文件：", L"Config file: ")) + g_configPath).c_str());
    }
    RefreshGameList();
}

void ShowMainWindow()
{
    ShowWindow(g_hWnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_hWnd);
}

void RememberExternalForegroundWindow(HWND hwnd)
{
    if (hwnd && !IsOwnWindow(hwnd))
    {
        g_lastExternalForegroundWindow = hwnd;
    }
}

HWND GetCommandTargetWindow()
{
    if (IsWindow(g_menuTargetWindow) && !IsOwnWindow(g_menuTargetWindow))
    {
        return g_menuTargetWindow;
    }

    if (IsWindow(g_lastExternalForegroundWindow))
    {
        return g_lastExternalForegroundWindow;
    }

    HWND foregroundWindow = GetForegroundWindow();
    return IsOwnWindow(foregroundWindow) ? nullptr : foregroundWindow;
}

void ShowTrayMenu()
{
    g_menuTargetWindow = GetForegroundWindow();
    RememberExternalForegroundWindow(g_menuTargetWindow);

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }

    AppendMenuItem(menu, IDM_SHOW_WINDOW, Text(L"显示主窗口", L"Show main window"));
    AppendMenuItem(menu, IDM_PROTECTION, g_protectionEnabled ? Text(L"保护模式：开启", L"Protection: On") : Text(L"保护模式：关闭", L"Protection: Off"), g_protectionEnabled);
    AppendMenuItem(menu, IDM_AUTO_DETECT, g_autoDetectEnabled ? Text(L"自动检测游戏：开启", L"Auto detect games: On") : Text(L"自动检测游戏：关闭", L"Auto detect games: Off"), g_autoDetectEnabled);
    AppendMenuItem(menu, IDM_WINDOWS_KEY_GUARD, g_windowsKeyGuardEnabled ? Text(L"Win 键：禁用", L"Windows key: Disabled") : Text(L"Win 键：开启", L"Windows key: Enabled"), g_windowsKeyGuardEnabled);
    AppendMenuItem(menu, 0, g_inGameProtection ? Text(L"当前状态：游戏保护中", L"Status: Protecting game") : Text(L"当前状态：普通", L"Status: Normal"), false, false);
    AppendMenuItem(menu, IDM_SWITCH_ENGLISH, Text(L"切换输入法到英文", L"Switch input to English"));
    AppendMenuItem(menu, IDM_SWITCH_CHINESE, Text(L"切换输入法到中文", L"Switch input to Chinese"));
    AppendMenuItem(menu, IDM_ADD_CURRENT_GAME, Text(L"添加当前程序为游戏", L"Add current program as game"));
    AppendMenuItem(menu, IDM_ADD_GAME_FILE, Text(L"浏览添加游戏...", L"Browse to add game..."));
    const bool startupEnabled = IsStartupEnabled();
    AppendMenuItem(menu, IDM_STARTUP, startupEnabled ? Text(L"开机启动：开启", L"Startup: On") : Text(L"开机启动：关闭", L"Startup: Off"), startupEnabled);
    AppendMenuItem(menu, IDM_EXIT, Text(L"退出", L"Exit"));

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(g_hWnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, g_hWnd, nullptr);
    DestroyMenu(menu);
}

ATOM RegisterMainWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_FFKEYLOCK));
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wcex.lpszClassName = kAppName;
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_FFKEYLOCK));
    return RegisterClassExW(&wcex);
}
}
