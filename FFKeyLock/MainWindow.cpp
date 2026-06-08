#include "MainWindow.h"

#include "AppState.h"
#include "Config.h"
#include "GameProtection.h"
#include "InputLanguage.h"
#include "Localization.h"
#include "OverlayNotificationManager.h"
#include "Platform/GdiUtils.h"
#include "Resource.h"
#include "UI/Controls/ContentPanel.h"
#include "UI/Menu/RoundedMenu.h"
#include "ThemeManager.h"
#include "TrayIcon.h"
#include "WindowsKeyGuard.h"

#include <commdlg.h>
#include <shellapi.h>
#include <strsafe.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace FFKeyLock
{
namespace
{
constexpr wchar_t kProtectedBrowserClass[] = L"FFKeyLockProtectedBrowser";
constexpr wchar_t kMenuBarClass[] = L"FFKeyLockMenuBar";
constexpr wchar_t kCardPanelClass[] = L"FFKeyLockCardPanel";
HWND g_protectedBrowserWindow = nullptr;
HWND g_protectedBrowserList = nullptr;
HWND g_menuBar = nullptr;
HWND g_contentPanel = nullptr;
HWND g_statusGroup = nullptr;
HWND g_featureGroup = nullptr;
HWND g_inputGroup = nullptr;
HWND g_programsGroup = nullptr;
std::vector<RECT> g_menuBarItems;
int g_scrollY = 0;
int g_contentHeight = 0;

int Scale(int value)
{
    return ThemeManager::Scale(value);
}

int MenuBarHeight()
{
    return Scale(30);
}

void RedrawFully(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }

    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void InvalidateFully(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }

    InvalidateRect(hwnd, nullptr, TRUE);
}

void InvalidateChildBounds(HWND child)
{
    if (!child || !GetParent(child))
    {
        return;
    }

    RECT rect{};
    if (!GetWindowRect(child, &rect))
    {
        return;
    }

    POINT topLeft{ rect.left, rect.top };
    POINT bottomRight{ rect.right, rect.bottom };
    HWND parent = GetParent(child);
    ScreenToClient(parent, &topLeft);
    ScreenToClient(parent, &bottomRight);
    RECT clientRect{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
    InvalidateRect(parent, &clientRect, TRUE);
}

void MoveChildWindow(HWND child, int x, int y, int width, int height)
{
    if (!child)
    {
        return;
    }

    InvalidateChildBounds(child);
    SetWindowPos(child, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateChildBounds(child);
}

void SetDefaultFont(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::UiFont() ? ThemeManager::UiFont() : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

void ApplyFonts()
{
    HWND controls[] = {
        g_appIcon,
        g_subtitleText,
        g_statusText,
        g_statusDetectedText,
        g_statusInputText,
        g_statusWinKeyText,
        g_protectionLabel,
        g_protectionStateText,
        g_protectionButton,
        g_autoDetectLabel,
        g_autoDetectStateText,
        g_autoDetectButton,
        g_startupLabel,
        g_startupStateText,
        g_startupButton,
        g_windowsKeyLabel,
        g_windowsKeyStateText,
        g_windowsKeyButton,
        g_inputLanguageText,
        g_switchEnglishButton,
        g_switchChineseButton,
        g_gameListLabel,
        g_gameListHelpButton,
        g_addCurrentButton,
        g_addFileButton,
        g_deleteGameButton,
        g_browseProtectedButton,
        g_gameList,
    };

    if (g_titleText)
    {
        SendMessageW(g_titleText, WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::TitleFont() ? ThemeManager::TitleFont() : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    for (HWND control : controls)
    {
        if (control)
        {
            SetDefaultFont(control);
        }
    }
}

HWND CreateControl(const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id)
{
    HWND parent = g_contentPanel ? g_contentPanel : g_hWnd;
    HWND control = CreateWindowExW(
        0,
        className,
        text,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
        Scale(x),
        Scale(y),
        Scale(width),
        Scale(height),
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_hInst,
        nullptr);

    if (control)
    {
        SetDefaultFont(control);
    }
    return control;
}

void MoveCardPanel(HWND panel, int x, int y, int width, int height)
{
    if (!panel)
    {
        return;
    }

    InvalidateChildBounds(panel);
    SetWindowPos(panel, HWND_BOTTOM, x, y, width, height, SWP_NOACTIVATE);
    InvalidateChildBounds(panel);
}

void RefreshProtectedBrowserList();
int LayoutContentControls(HWND panel, int width, int height, int scrollY);
HBRUSH ContentPanelCtlColorStatic(HWND panel, HDC hdc, HWND control);

RoundedMenuItem MenuItem(UINT id, const std::wstring& text, const std::wstring& shortcut = L"", bool checked = false, bool enabled = true)
{
    RoundedMenuItem item{};
    item.id = id;
    item.text = text;
    item.shortcut = shortcut;
    item.checked = checked;
    item.enabled = enabled;
    return item;
}

RoundedMenuItem MenuTitle(const std::wstring& text)
{
    RoundedMenuItem item{};
    item.text = text;
    item.title = true;
    item.enabled = false;
    return item;
}

RoundedMenuItem MenuSeparator()
{
    RoundedMenuItem item{};
    item.separator = true;
    item.enabled = false;
    return item;
}

RoundedMenuItem MenuSubmenu(const std::wstring& text, std::vector<RoundedMenuItem> submenu)
{
    RoundedMenuItem item{};
    item.text = text;
    item.submenu = std::move(submenu);
    return item;
}

std::vector<RoundedMenuItem> BuildThemeMenu()
{
    return {
        MenuItem(IDM_THEME_LIGHT, Text(L"浅色", L"Light"), L"", g_themePreference == ThemePreference::Light),
        MenuItem(IDM_THEME_DARK, Text(L"深色", L"Dark"), L"", g_themePreference == ThemePreference::Dark),
        MenuItem(IDM_THEME_SYSTEM, Text(L"跟随系统", L"System"), L"", g_themePreference == ThemePreference::System),
    };
}

std::vector<RoundedMenuItem> BuildSettingsMenu()
{
    return {
        MenuSubmenu(Text(L"主题", L"Theme"), BuildThemeMenu()),
        MenuItem(IDM_OPEN_CONFIG_DIR, Text(L"打开配置目录", L"Open config directory")),
        MenuItem(IDM_OPEN_LOG_DIR, Text(L"打开日志目录", L"Open log directory")),
        MenuItem(IDM_STARTUP, Text(L"开机启动", L"Startup"), L"", IsStartupEnabled()),
        MenuSeparator(),
        MenuItem(IDM_RESET_CONFIG, Text(L"重置配置", L"Reset config")),
        MenuSeparator(),
        MenuItem(IDM_EXIT, Text(L"退出", L"Exit"), L"Alt+F4"),
    };
}

std::vector<RoundedMenuItem> BuildNotificationsMenu()
{
    return {
        MenuItem(IDM_TEST_NOTIFICATION, Text(L"显示测试通知", L"Show test notification")),
        MenuItem(IDM_OVERLAY_NOTIFICATIONS, Text(L"开启 Overlay 通知", L"Enable Overlay notifications"), L"", g_overlayNotificationsEnabled),
        MenuItem(IDM_NOTIFICATIONS, Text(L"开启系统 Toast 通知", L"Enable system Toast notifications"), L"", g_notificationsEnabled),
        MenuItem(IDM_MUTE_NOTIFICATIONS, Text(L"静音通知", L"Mute notifications"), L"", !g_notificationsEnabled && !g_overlayNotificationsEnabled),
    };
}

std::vector<RoundedMenuItem> BuildHelpMenu()
{
    return {
        MenuItem(IDM_HELP_USAGE, Text(L"使用说明", L"Usage")),
        MenuItem(IDM_CHECK_UPDATES, Text(L"检查更新", L"Check updates")),
        MenuItem(IDM_GITHUB_PROJECT, Text(L"GitHub 项目", L"GitHub project")),
        MenuSeparator(),
        MenuItem(IDM_ABOUT, Text(L"关于 FFKeyLock", L"About FFKeyLock")),
    };
}

std::vector<RoundedMenuItem> BuildTrayMenu()
{
    return {
        MenuTitle(L"FFKeyLock"),
        MenuItem(IDM_PROTECTION, Text(L"启用保护模式", L"Enable protection mode"), L"", g_protectionEnabled),
        MenuItem(IDM_AUTO_DETECT, g_autoDetectEnabled ? Text(L"自动检测：开启", L"Auto detect: On") : Text(L"自动检测：关闭", L"Auto detect: Off"), L"", g_autoDetectEnabled),
        MenuItem(IDM_WINDOWS_KEY_GUARD, g_windowsKeyGuardEnabled ? Text(L"Win 键：禁用", L"Win key: Disabled") : Text(L"Win 键：开启", L"Win key: Enabled"), L"", g_windowsKeyGuardEnabled),
        MenuSeparator(),
        MenuItem(IDM_SHOW_SETTINGS, Text(L"设置...", L"Settings...")),
        MenuItem(IDM_SHOW_WINDOW, Text(L"显示主窗口", L"Show main window")),
        MenuItem(IDM_EXIT, Text(L"退出", L"Exit")),
    };
}

std::vector<RoundedMenuItem> BuildGameListContextMenu()
{
    return {
        MenuItem(IDM_COPY_GAME_NAME, Text(L"复制名称", L"Copy name"), L"Ctrl+C"),
        MenuItem(IDM_OPEN_GAME_FOLDER, Text(L"跳转到该程序文件夹", L"Open program folder")),
    };
}

void OpenFolderPath(const std::wstring& path)
{
    if (!path.empty())
    {
        ShellExecuteW(g_hWnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void OpenConfigDirectory()
{
    std::filesystem::path path(g_configPath);
    OpenFolderPath(path.has_parent_path() ? path.parent_path().wstring() : std::filesystem::current_path().wstring());
}

void OpenLogDirectory()
{
    OpenConfigDirectory();
}

void ShowMenuBarPopup(int index)
{
    if (!g_menuBar || index < 0 || index >= static_cast<int>(g_menuBarItems.size()))
    {
        return;
    }

    std::vector<RoundedMenuItem> items;
    if (index == 0)
    {
        items = BuildSettingsMenu();
        items.insert(items.begin(), MenuSeparator());
        items.insert(items.begin(), MenuItem(IDM_SWITCH_ENGLISH, Text(L"切换为英文", L"Switch to English")));
        items.insert(items.begin(), MenuItem(IDM_SWITCH_CHINESE, Text(L"切换为中文", L"Switch to Chinese")));
    }
    else if (index == 1)
    {
        items = BuildNotificationsMenu();
    }
    else
    {
        items = BuildHelpMenu();
    }

    POINT anchor{ g_menuBarItems[index].left, g_menuBarItems[index].bottom };
    ClientToScreen(g_menuBar, &anchor);
    RoundedMenu::ShowAndDispatch(g_hWnd, anchor, items);
    InvalidateFully(g_menuBar);
}

int HitTestMenuBarItem(POINT point)
{
    for (size_t i = 0; i < g_menuBarItems.size(); ++i)
    {
        if (PtInRect(&g_menuBarItems[i], point))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void PaintMenuBar(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    GdiUtils::BufferedPaint buffer(hdc, client);
    HDC drawDc = buffer.Dc();
    HBRUSH background = CreateSolidBrush(ThemeManager::MenuBarColor());
    FillRect(drawDc, &client, background);
    DeleteObject(background);

    HPEN linePen = CreatePen(PS_SOLID, 1, ThemeManager::MenuBorderColor());
    HGDIOBJ oldPen = SelectObject(drawDc, linePen);
    MoveToEx(drawDc, client.left, client.bottom - 1, nullptr);
    LineTo(drawDc, client.right, client.bottom - 1);
    SelectObject(drawDc, oldPen);
    DeleteObject(linePen);

    const wchar_t* labels[] = {
        Text(L"设置", L"Settings"),
        Text(L"通知", L"Notifications"),
        Text(L"帮助", L"Help"),
    };

    SetBkMode(drawDc, TRANSPARENT);
    SetTextColor(drawDc, ThemeManager::TextColor());
    HGDIOBJ oldFont = SelectObject(drawDc, ThemeManager::UiFont() ? ThemeManager::UiFont() : GetStockObject(DEFAULT_GUI_FONT));
    g_menuBarItems.clear();
    int x = Scale(8);
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);
    for (const auto* label : labels)
    {
        SIZE size{};
        GetTextExtentPoint32W(drawDc, label, static_cast<int>(wcslen(label)), &size);
        RECT rect{ x, Scale(3), x + size.cx + Scale(24), client.bottom - Scale(3) };
        g_menuBarItems.push_back(rect);
        if (PtInRect(&rect, cursor))
        {
            HBRUSH hover = CreateSolidBrush(ThemeManager::MenuBarHoverColor());
            FillRect(drawDc, &rect, hover);
            DeleteObject(hover);
        }
        RECT textRect = rect;
        textRect.left += Scale(12);
        textRect.right -= Scale(12);
        DrawTextW(drawDc, label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        x = rect.right + Scale(2);
    }
    SelectObject(drawDc, oldFont);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK MenuBarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        PaintMenuBar(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_MOUSEMOVE:
        InvalidateFully(hwnd);
        return 0;

    case WM_MOUSELEAVE:
        InvalidateFully(hwnd);
        return 0;

    case WM_LBUTTONUP:
    {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ShowMenuBarPopup(HitTestMenuBarItem(point));
        return 0;
    }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnsureMenuBarClass()
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MenuBarProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kMenuBarClass;
    RegisterClassExW(&wc);
    registered = true;
}

void CreateMenuBar(HWND parent)
{
    EnsureMenuBarClass();
    g_menuBar = CreateWindowExW(
        0,
        kMenuBarClass,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0,
        0,
        0,
        MenuBarHeight(),
        parent,
        nullptr,
        g_hInst,
        nullptr);
}

void PaintCardPanel(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);

    GdiUtils::BufferedPaint buffer(hdc, client);
    HDC drawDc = buffer.Dc();
    HBRUSH background = CreateSolidBrush(ThemeManager::WindowColor());
    FillRect(drawDc, &client, background);
    DeleteObject(background);

    GdiUtils::FillRoundRect(drawDc, client, ThemeManager::SurfaceColor(), ThemeManager::BorderColor(), Scale(8));

    wchar_t title[128]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    RECT titleRect = client;
    titleRect.left += Scale(16);
    titleRect.right -= Scale(16);
    titleRect.top += Scale(10);
    titleRect.bottom = titleRect.top + Scale(24);

    SetBkMode(drawDc, TRANSPARENT);
    SetTextColor(drawDc, ThemeManager::TextColor());
    GdiUtils::SelectObjectScope fontScope(drawDc, ThemeManager::UiFont() ? ThemeManager::UiFont() : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(drawDc, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK CardPanelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        PaintCardPanel(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_SETTEXT:
    {
        const LRESULT result = DefWindowProcW(hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, TRUE);
        return result;
    }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnsureCardPanelClass()
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CardPanelProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kCardPanelClass;
    RegisterClassExW(&wc);
    registered = true;
}

HWND CreateCardPanel(const wchar_t* text, int id)
{
    EnsureCardPanelClass();
    HWND parent = g_contentPanel ? g_contentPanel : g_hWnd;
    return CreateWindowExW(
        0,
        kCardPanelClass,
        text,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0,
        0,
        10,
        10,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_hInst,
        nullptr);
}

std::wstring BuildStatusText()
{
    std::wstring status = Text(L"当前状态：", L"Status: ");
    if (g_chatInputSuspended)
    {
        status += Text(L"聊天输入中", L"Chat input");
    }
    else
    {
        status += g_inGameProtection ? Text(L"游戏保护中", L"Protecting game") : Text(L"普通", L"Normal");
    }
    status += Text(L"    保护模式：", L"    Protection: ");
    status += g_protectionEnabled ? Text(L"开启", L"On") : Text(L"关闭", L"Off");
    status += Text(L"    自动检测：", L"    Auto detect: ");
    status += g_autoDetectEnabled ? Text(L"开启", L"On") : Text(L"关闭", L"Off");
    status += Text(L"    Win 键：", L"    Windows key: ");
    status += g_windowsKeyGuardEnabled ? Text(L"禁用", L"Disabled") : Text(L"开启", L"Enabled");
    return status;
}

std::wstring BuildPrimaryStatusText()
{
    if (g_chatInputSuspended)
    {
        return Text(L"[CHAT] 聊天输入中", L"[CHAT] Chat input");
    }
    return g_inGameProtection ? Text(L"[ON] 保护中", L"[ON] Protecting") : Text(L"[IDLE] 待机中", L"[IDLE] Standing by");
}

std::wstring EnabledText(bool enabled)
{
    return enabled ? Text(L"已开启", L"On") : Text(L"未开启", L"Off");
}

std::wstring SwitchActionText(bool enabled)
{
    return enabled ? Text(L"关闭", L"Turn off") : Text(L"开启", L"Turn on");
}

std::wstring CurrentInputLanguageText()
{
    HWND targetWindow = GetCommandTargetWindow();
    if (!targetWindow)
    {
        targetWindow = GetForegroundWindow();
    }

    DWORD threadId = targetWindow ? GetWindowThreadProcessId(targetWindow, nullptr) : 0;
    HKL layout = threadId ? GetKeyboardLayout(threadId) : GetKeyboardLayout(0);
    const LANGID language = LOWORD(reinterpret_cast<UINT_PTR>(layout));
    const WORD primaryLanguage = PRIMARYLANGID(language);
    if (primaryLanguage == LANG_CHINESE)
    {
        return Text(L"中文", L"Chinese");
    }
    if (primaryLanguage == LANG_ENGLISH)
    {
        return Text(L"英文", L"English");
    }
    return Text(L"未知", L"Unknown");
}

std::wstring StatusLine(const wchar_t* chineseLabel, const wchar_t* englishLabel, const std::wstring& value)
{
    return std::wstring(Text(chineseLabel, englishLabel)) + value;
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

    RefreshProtectedBrowserList();
}

std::wstring GetSelectedGameName()
{
    if (!g_gameList)
    {
        return L"";
    }

    const int selected = static_cast<int>(SendMessageW(g_gameList, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR || selected < 0 || selected >= static_cast<int>(g_gameExeNames.size()))
    {
        return L"";
    }

    return g_gameExeNames[selected];
}

std::wstring GetSelectedBrowserGameName()
{
    if (!g_protectedBrowserList)
    {
        return L"";
    }

    const int selected = static_cast<int>(SendMessageW(g_protectedBrowserList, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR || selected < 0 || selected >= static_cast<int>(g_gameExeNames.size()))
    {
        return L"";
    }

    return g_gameExeNames[selected];
}

void CreateMainControls()
{
    g_appIcon = CreateControl(L"STATIC", L"", SS_ICON | SS_CENTERIMAGE, 20, 18, 48, 48, IDC_APP_ICON);
    if (g_appIcon)
    {
        SendMessageW(g_appIcon, STM_SETICON, reinterpret_cast<WPARAM>(LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_FFKEYLOCK))), 0);
    }

    g_titleText = CreateControl(L"STATIC", L"FFKeyLock", SS_LEFT, 80, 18, 220, 30, IDC_STATIC);
    if (g_titleText)
    {
        SendMessageW(g_titleText, WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::TitleFont() ? ThemeManager::TitleFont() : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    g_subtitleText = CreateControl(L"STATIC", Text(L"游戏防误触助手", L"Game mis-touch protection assistant"), SS_LEFT, 80, 48, 260, 22, IDC_SUBTITLE_TEXT);

    g_statusGroup = CreateCardPanel(Text(L"当前状态", L"Current status"), IDC_STATUS_GROUP);
    g_statusText = CreateControl(L"STATIC", L"", SS_LEFT, 40, 112, 240, 30, IDC_STATUS_TEXT);
    if (g_statusText)
    {
        SendMessageW(g_statusText, WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::TitleFont() ? ThemeManager::TitleFont() : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    g_statusDetectedText = CreateControl(L"STATIC", L"", SS_LEFT, 40, 146, 520, 22, IDC_STATUS_DETECTED_TEXT);
    g_statusInputText = CreateControl(L"STATIC", L"", SS_LEFT, 40, 172, 520, 22, IDC_STATUS_INPUT_TEXT);
    g_statusWinKeyText = CreateControl(L"STATIC", L"", SS_LEFT, 40, 198, 520, 22, IDC_STATUS_WINKEY_TEXT);

    g_featureGroup = CreateCardPanel(Text(L"功能开关", L"Feature switches"), IDC_FEATURE_GROUP);
    g_protectionLabel = CreateControl(L"STATIC", Text(L"保护模式", L"Protection mode"), SS_LEFT, 40, 266, 160, 22, IDC_PROTECTION_LABEL);
    g_protectionStateText = CreateControl(L"STATIC", L"", SS_LEFT, 220, 266, 100, 22, IDC_PROTECTION_STATE_TEXT);
    g_protectionButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 340, 260, 90, 32, IDC_PROTECTION_BUTTON);
    g_autoDetectLabel = CreateControl(L"STATIC", Text(L"自动检测", L"Auto detect"), SS_LEFT, 40, 306, 160, 22, IDC_AUTO_DETECT_LABEL);
    g_autoDetectStateText = CreateControl(L"STATIC", L"", SS_LEFT, 220, 306, 100, 22, IDC_AUTO_DETECT_STATE_TEXT);
    g_autoDetectButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 340, 300, 90, 32, IDC_AUTO_DETECT_BUTTON);
    g_startupLabel = CreateControl(L"STATIC", Text(L"开机启动", L"Startup"), SS_LEFT, 470, 266, 160, 22, IDC_STARTUP_LABEL);
    g_startupStateText = CreateControl(L"STATIC", L"", SS_LEFT, 640, 266, 100, 22, IDC_STARTUP_STATE_TEXT);
    g_startupButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 740, 260, 90, 32, IDC_STARTUP_BUTTON);
    g_windowsKeyLabel = CreateControl(L"STATIC", Text(L"Win 键锁定", L"Win key lock"), SS_LEFT, 470, 306, 160, 22, IDC_WINDOWS_KEY_LABEL);
    g_windowsKeyStateText = CreateControl(L"STATIC", L"", SS_LEFT, 640, 306, 100, 22, IDC_WINDOWS_KEY_STATE_TEXT);
    g_windowsKeyButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 740, 300, 90, 32, IDC_WINDOWS_KEY_BUTTON);

    g_programsGroup = CreateCardPanel(Text(L"受保护程序", L"Protected programs"), IDC_PROGRAMS_GROUP);
    g_gameListLabel = CreateControl(L"STATIC", Text(L"程序列表", L"Program list"), SS_LEFT, 40, 486, 126, 24, IDC_STATIC);
    g_gameListHelpButton = CreateControl(L"BUTTON", L"?", BS_PUSHBUTTON, 150, 482, 28, 28, IDC_GAME_LIST_HELP);
    g_gameList = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        Scale(40),
        Scale(514),
        Scale(630),
        Scale(70),
        g_contentPanel ? g_contentPanel : g_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_GAME_LIST)),
        g_hInst,
        nullptr);
    if (g_gameList)
    {
        SetDefaultFont(g_gameList);
    }

    g_addCurrentButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 700, 514, 130, 32, IDC_ADD_GAME_BUTTON);
    g_browseProtectedButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 700, 552, 130, 32, IDC_BROWSE_PROTECTED_BUTTON);
    g_addFileButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 700, 590, 130, 32, IDC_ADD_FILE_BUTTON);
    g_deleteGameButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 700, 628, 130, 32, IDC_DELETE_GAME_BUTTON);
    UpdateMainWindow();
}

int LayoutContentControls(HWND panel, int width, int height, int scrollY)
{
    UNREFERENCED_PARAMETER(panel);

    g_scrollY = std::max(0, scrollY);
    const int viewportHeight = std::max(0, height);
    const int margin = Scale(24);
    const int groupSpacing = Scale(18);
    const int buttonHeight = Scale(32);
    const int switchButtonWidth = Scale(104);
    const int actionButtonWidth = Scale(130);
    const int contentWidth = std::max(Scale(860), width - margin * 2 - GetSystemMetrics(SM_CXVSCROLL));
    const int headerY = Scale(18);
    const int headerHeight = Scale(80);
    const int statusY = headerY + headerHeight + groupSpacing;
    const int statusHeight = Scale(158);
    const int featureY = statusY + statusHeight + groupSpacing;
    const int featureHeight = Scale(126);
    const int programsY = featureY + featureHeight + groupSpacing;
    const int programsHeight = std::max(Scale(250), viewportHeight - programsY - margin);
    g_contentHeight = programsY + programsHeight + margin;
    const int maxScroll = std::max(0, g_contentHeight - viewportHeight);
    g_scrollY = std::clamp(g_scrollY, 0, maxScroll);

    const int groupInnerLeft = margin + Scale(20);
    const int groupRight = margin + contentWidth;
    const int switchStateX = margin + Scale(160);
    const int switchButtonX = margin + Scale(254);
    const int secondColX = margin + std::max(Scale(430), contentWidth / 2 + Scale(16));
    const int secondStateX = secondColX + Scale(132);
    const int secondButtonX = groupRight - Scale(20) - switchButtonWidth;
    const int listX = groupInnerLeft;
    const int listY = programsY + Scale(52);
    const int actionButtonX = groupRight - Scale(20) - actionButtonWidth;
    const int listWidth = std::max(Scale(300), actionButtonX - Scale(20) - listX);
    const int listHeight = std::max(Scale(128), programsHeight - Scale(74));
    auto y = [&](int logicalY) { return logicalY - g_scrollY; };

    if (g_titleText)
    {
        MoveChildWindow(g_titleText, margin + Scale(64), y(headerY + Scale(8)), Scale(260), Scale(30));
    }
    if (g_subtitleText)
    {
        MoveChildWindow(g_subtitleText, margin + Scale(64), y(headerY + Scale(42)), Scale(320), Scale(22));
    }
    if (g_appIcon)
    {
        MoveChildWindow(g_appIcon, margin, y(headerY + Scale(10)), Scale(48), Scale(48));
    }
    if (g_statusGroup)
    {
        MoveCardPanel(g_statusGroup, margin, y(statusY), contentWidth, statusHeight);
    }
    if (g_statusText)
    {
        MoveChildWindow(g_statusText, groupInnerLeft, y(statusY + Scale(30)), Scale(320), Scale(30));
    }
    if (g_statusDetectedText)
    {
        MoveChildWindow(g_statusDetectedText, groupInnerLeft, y(statusY + Scale(68)), contentWidth - Scale(40), Scale(24));
    }
    if (g_statusInputText)
    {
        MoveChildWindow(g_statusInputText, groupInnerLeft, y(statusY + Scale(96)), contentWidth - Scale(40), Scale(24));
    }
    if (g_statusWinKeyText)
    {
        MoveChildWindow(g_statusWinKeyText, groupInnerLeft, y(statusY + Scale(124)), contentWidth - Scale(40), Scale(24));
    }
    if (g_featureGroup)
    {
        MoveCardPanel(g_featureGroup, margin, y(featureY), contentWidth, featureHeight);
    }
    if (g_protectionLabel)
    {
        MoveChildWindow(g_protectionLabel, groupInnerLeft, y(featureY + Scale(34)), Scale(150), Scale(22));
    }
    if (g_protectionStateText)
    {
        MoveChildWindow(g_protectionStateText, switchStateX, y(featureY + Scale(34)), Scale(76), Scale(22));
    }
    if (g_protectionButton)
    {
        MoveChildWindow(g_protectionButton, switchButtonX, y(featureY + Scale(28)), switchButtonWidth, buttonHeight);
    }
    if (g_autoDetectLabel)
    {
        MoveChildWindow(g_autoDetectLabel, groupInnerLeft, y(featureY + Scale(78)), Scale(150), Scale(22));
    }
    if (g_autoDetectStateText)
    {
        MoveChildWindow(g_autoDetectStateText, switchStateX, y(featureY + Scale(78)), Scale(76), Scale(22));
    }
    if (g_autoDetectButton)
    {
        MoveChildWindow(g_autoDetectButton, switchButtonX, y(featureY + Scale(72)), switchButtonWidth, buttonHeight);
    }
    if (g_startupLabel)
    {
        MoveChildWindow(g_startupLabel, secondColX, y(featureY + Scale(34)), Scale(150), Scale(22));
    }
    if (g_startupStateText)
    {
        MoveChildWindow(g_startupStateText, secondStateX, y(featureY + Scale(34)), Scale(76), Scale(22));
    }
    if (g_startupButton)
    {
        MoveChildWindow(g_startupButton, secondButtonX, y(featureY + Scale(28)), switchButtonWidth, buttonHeight);
    }
    if (g_windowsKeyLabel)
    {
        MoveChildWindow(g_windowsKeyLabel, secondColX, y(featureY + Scale(78)), Scale(150), Scale(22));
    }
    if (g_windowsKeyStateText)
    {
        MoveChildWindow(g_windowsKeyStateText, secondStateX, y(featureY + Scale(78)), Scale(76), Scale(22));
    }
    if (g_windowsKeyButton)
    {
        MoveChildWindow(g_windowsKeyButton, secondButtonX, y(featureY + Scale(72)), switchButtonWidth, buttonHeight);
    }
    if (g_programsGroup)
    {
        MoveCardPanel(g_programsGroup, margin, y(programsY), contentWidth, programsHeight);
    }
    if (g_gameListLabel)
    {
        MoveChildWindow(g_gameListLabel, listX, y(programsY + Scale(28)), Scale(110), Scale(24));
    }
    if (g_gameListHelpButton)
    {
        MoveChildWindow(g_gameListHelpButton, listX + Scale(110), y(programsY + Scale(24)), Scale(28), Scale(28));
    }
    if (g_gameList)
    {
        MoveChildWindow(g_gameList, listX, y(listY), listWidth, listHeight);
    }
    if (g_addCurrentButton)
    {
        MoveChildWindow(g_addCurrentButton, actionButtonX, y(listY), actionButtonWidth, buttonHeight);
    }
    if (g_browseProtectedButton)
    {
        MoveChildWindow(g_browseProtectedButton, actionButtonX, y(listY + Scale(42)), actionButtonWidth, buttonHeight);
    }
    if (g_addFileButton)
    {
        MoveChildWindow(g_addFileButton, actionButtonX, y(listY + Scale(84)), actionButtonWidth, buttonHeight);
    }
    if (g_deleteGameButton)
    {
        MoveChildWindow(g_deleteGameButton, actionButtonX, y(listY + Scale(126)), actionButtonWidth, buttonHeight);
    }
    return g_contentHeight;
}

void ResizeMainControls(int width, int height)
{
    const int menuHeight = MenuBarHeight();
    if (g_menuBar)
    {
        SetWindowPos(g_menuBar, HWND_TOP, 0, 0, width, menuHeight, SWP_NOACTIVATE);
        InvalidateFully(g_menuBar);
    }
    if (g_contentPanel)
    {
        SetWindowPos(g_contentPanel, nullptr, 0, menuHeight, width, std::max(0, height - menuHeight), SWP_NOZORDER | SWP_NOACTIVATE);
        ContentPanel::Relayout(g_contentPanel);
    }
}

void ApplyTheme()
{
    ThemeManager::Initialize(GetDpiForWindow(g_hWnd));
    ThemeManager::ApplyTheme(g_hWnd);
    ApplyFonts();
    if (g_protectedBrowserWindow)
    {
        ThemeManager::ApplyTheme(g_protectedBrowserWindow);
    }
    if (g_hWnd)
    {
        RedrawFully(g_hWnd);
    }
    if (g_gameList)
    {
        InvalidateFully(g_gameList);
    }
}

bool IsOwnWindow(HWND hwnd)
{
    return hwnd && g_hWnd && GetAncestor(hwnd, GA_ROOT) == g_hWnd;
}

bool IsControlInsidePanel(HWND control, HWND panel)
{
    if (!control || !panel)
    {
        return false;
    }

    RECT controlRect{};
    RECT panelRect{};
    if (!GetWindowRect(control, &controlRect) || !GetWindowRect(panel, &panelRect))
    {
        return false;
    }

    POINT center{
        controlRect.left + (controlRect.right - controlRect.left) / 2,
        controlRect.top + (controlRect.bottom - controlRect.top) / 2
    };
    return PtInRect(&panelRect, center) != FALSE;
}

bool StaticUsesSurfaceBackground(HWND control)
{
    return IsControlInsidePanel(control, g_statusGroup)
        || IsControlInsidePanel(control, g_featureGroup)
        || IsControlInsidePanel(control, g_programsGroup);
}

HBRUSH ContentPanelCtlColorStatic(HWND, HDC hdc, HWND control)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, control == g_statusText ? ThemeManager::AccentColor() : ThemeManager::TextColor());
    if (StaticUsesSurfaceBackground(control))
    {
        return ThemeManager::SurfaceBrush() ? ThemeManager::SurfaceBrush() : reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    }
    return ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
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
    ofn.lpstrTitle = Text(L"选择受保护程序", L"Select protected executable");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
    {
        AddGameExeName(path);
    }
}

void DeleteSelectedGame()
{
    if (!g_gameList)
    {
        return;
    }

    const int selected = static_cast<int>(SendMessageW(g_gameList, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR || selected < 0 || selected >= static_cast<int>(g_gameExeNames.size()))
    {
        MessageBoxW(g_hWnd,
            Text(L"请先在受保护的程序列表中选择一项。", L"Select an item in the protected program list first."),
            L"FFKeyLock",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring exeName = g_gameExeNames[selected];
    g_gameExeNames.erase(g_gameExeNames.begin() + selected);
    g_gameExePaths.erase(exeName);
    SaveConfig();
    RefreshGameList();
    const int lastIndex = static_cast<int>(g_gameExeNames.size()) - 1;
    const int nextSelection = (std::min)(selected, lastIndex);
    if (nextSelection >= 0)
    {
        SendMessageW(g_gameList, LB_SETCURSEL, nextSelection, 0);
    }
}

void CopyTextToClipboard(const std::wstring& text)
{
    if (text.empty() || !OpenClipboard(g_hWnd))
    {
        return;
    }

    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory)
    {
        void* data = GlobalLock(memory);
        if (data)
        {
            CopyMemory(data, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory)
    {
        GlobalFree(memory);
    }
    CloseClipboard();
}

void OpenGameFolder(const std::wstring& exeName)
{
    const auto path = g_gameExePaths.find(exeName);
    if (exeName.empty() || path == g_gameExePaths.end() || path->second.empty())
    {
        MessageBoxW(g_hWnd,
            Text(L"这个条目只保存了程序名称，没有保存文件路径。请通过“浏览添加受保护程序...”重新添加一次即可记录路径。",
                L"This item only has an executable name, not a saved file path. Add it again with Browse to record its path."),
            L"FFKeyLock",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring argument = L"/select,\"" + path->second + L"\"";
    ShellExecuteW(g_hWnd, L"open", L"explorer.exe", argument.c_str(), nullptr, SW_SHOWNORMAL);
}

void OpenSelectedGameFolder()
{
    OpenGameFolder(GetSelectedGameName());
}

void RefreshProtectedBrowserList()
{
    if (!g_protectedBrowserList)
    {
        return;
    }

    SendMessageW(g_protectedBrowserList, LB_RESETCONTENT, 0, 0);
    for (const auto& game : g_gameExeNames)
    {
        std::wstring item = game;
        const auto path = g_gameExePaths.find(game);
        if (path != g_gameExePaths.end() && !path->second.empty())
        {
            item += L"    ";
            item += path->second;
        }
        SendMessageW(g_protectedBrowserList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
}

void PaintWindowBackground(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    GdiUtils::BufferedPaint buffer(hdc, client);
    FillRect(buffer.Dc(), &client, ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK ProtectedBrowserProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        CreateWindowExW(0, L"STATIC", Text(L"受保护程序", L"Protected programs"), WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 14, 180, 24,
            hwnd, nullptr, g_hInst, nullptr);
        g_protectedBrowserList = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | LBS_NOTIFY,
            16,
            48,
            560,
            260,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROTECTED_BROWSER_LIST)),
            g_hInst,
            nullptr);
        HWND copyButton = CreateWindowExW(0, L"BUTTON", Text(L"复制名称", L"Copy name"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            316, 14, 120, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROTECTED_BROWSER_COPY)), g_hInst, nullptr);
        HWND openButton = CreateWindowExW(0, L"BUTTON", Text(L"打开文件夹", L"Open folder"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            456, 14, 120, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROTECTED_BROWSER_OPEN)), g_hInst, nullptr);

        SetDefaultFont(g_protectedBrowserList);
        SetDefaultFont(copyButton);
        SetDefaultFont(openButton);
        RefreshProtectedBrowserList();
        ThemeManager::ApplyTheme(hwnd);
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
        PaintWindowBackground(hwnd);
        return 0;

    case WM_DPICHANGED:
    {
        ThemeManager::SetDpi(HIWORD(wParam));
        ApplyFonts();
        const auto* suggestedRect = reinterpret_cast<RECT*>(lParam);
        if (suggestedRect)
        {
            SetWindowPos(hwnd, nullptr, suggestedRect->left, suggestedRect->top,
                suggestedRect->right - suggestedRect->left,
                suggestedRect->bottom - suggestedRect->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ThemeManager::ApplyTheme(hwnd);
        RedrawFully(hwnd);
        return 0;
    }

    case WM_SIZE:
        RedrawFully(hwnd);
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ThemeManager::TextColor());
        return reinterpret_cast<LRESULT>(ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : GetStockObject(WHITE_BRUSH));
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(ThemeManager::HandleCtlColor(hwnd, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

    case WM_DRAWITEM:
    {
        const auto* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (drawItem && drawItem->CtlType == ODT_BUTTON)
        {
            ThemeManager::DrawButton(*drawItem);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_PROTECTED_BROWSER_COPY:
            CopyTextToClipboard(GetSelectedBrowserGameName());
            return 0;

        case IDC_PROTECTED_BROWSER_OPEN:
            OpenGameFolder(GetSelectedBrowserGameName());
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_protectedBrowserWindow == hwnd)
        {
            g_protectedBrowserWindow = nullptr;
            g_protectedBrowserList = nullptr;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnsureProtectedBrowserClass()
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = ProtectedBrowserProc;
    wcex.hInstance = g_hInst;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = kProtectedBrowserClass;
    registered = RegisterClassExW(&wcex) != 0;
}

void ShowProtectedProgramsBrowser()
{
    EnsureProtectedBrowserClass();
    if (g_protectedBrowserWindow)
    {
        RefreshProtectedBrowserList();
        ShowWindow(g_protectedBrowserWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_protectedBrowserWindow);
        return;
    }

    g_protectedBrowserWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kProtectedBrowserClass,
        Text(L"浏览受保护程序", L"Browse protected programs"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        610,
        370,
        g_hWnd,
        nullptr,
        g_hInst,
        nullptr);

    if (g_protectedBrowserWindow)
    {
        ShowWindow(g_protectedBrowserWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_protectedBrowserWindow);
    }
}

void ShowProtectedListHelp()
{
    MessageBoxW(g_hWnd,
        Text(
            L"受保护的程序列表用于保存需要 FFKeyLock 接管输入法保护的程序。\n\n当自动检测开启，前台窗口属于列表中的 exe 时，FFKeyLock 会切换到英文输入法；离开后会恢复之前的输入法状态。",
            L"The protected program list stores executables that FFKeyLock should guard.\n\nWhen auto detection is enabled and the foreground window belongs to one of these executables, FFKeyLock switches input to English and restores the previous input state after you leave it."),
        Text(L"受保护的程序列表", L"Protected program list"),
        MB_OK | MB_ICONINFORMATION);
}

void ShowGameListContextMenu(LPARAM lParam)
{
    if (!g_gameList)
    {
        return;
    }

    POINT point{};
    if (lParam == -1)
    {
        const int selected = static_cast<int>(SendMessageW(g_gameList, LB_GETCURSEL, 0, 0));
        RECT itemRect{};
        if (selected != LB_ERR && SendMessageW(g_gameList, LB_GETITEMRECT, selected, reinterpret_cast<LPARAM>(&itemRect)) != LB_ERR)
        {
            point.x = itemRect.left + 16;
            point.y = itemRect.top + 12;
            ClientToScreen(g_gameList, &point);
        }
        else
        {
            GetCursorPos(&point);
        }
    }
    else
    {
        point.x = GET_X_LPARAM(lParam);
        point.y = GET_Y_LPARAM(lParam);
        POINT clientPoint = point;
        ScreenToClient(g_gameList, &clientPoint);
        const DWORD hit = static_cast<DWORD>(SendMessageW(g_gameList, LB_ITEMFROMPOINT, 0, MAKELPARAM(clientPoint.x, clientPoint.y)));
        const int index = LOWORD(hit);
        const bool outside = HIWORD(hit) != 0;
        if (!outside && index >= 0 && index < static_cast<int>(g_gameExeNames.size()))
        {
            SendMessageW(g_gameList, LB_SETCURSEL, index, 0);
        }
    }

    if (GetSelectedGameName().empty())
    {
        return;
    }

    RoundedMenu::ShowAndDispatch(g_hWnd, point, BuildGameListContextMenu());
}

void DrawGameListItem(const DRAWITEMSTRUCT* item)
{
    if (!item || item->CtlID != IDC_GAME_LIST || item->itemID == static_cast<UINT>(-1))
    {
        return;
    }

    ThemeManager::DrawListBoxItem(*item, g_gameExeNames);
}

void PaintMainWindow(HWND hWnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hWnd, &paint);
    RECT client{};
    GetClientRect(hWnd, &client);
    GdiUtils::BufferedPaint buffer(hdc, client);
    HDC drawDc = buffer.Dc();
    FillRect(drawDc, &client, ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    EndPaint(hWnd, &paint);
}

LRESULT EraseMainWindowBackground(HWND hWnd, HDC hdc)
{
    RECT client{};
    GetClientRect(hWnd, &client);
    FillRect(hdc, &client, ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    return TRUE;
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
        SetWindowLongPtrW(hWnd, GWL_STYLE, (GetWindowLongPtrW(hWnd, GWL_STYLE) & ~WS_VSCROLL) | WS_CLIPCHILDREN);
        ThemeManager::Initialize(GetDpiForWindow(hWnd));
        ThemeManager::ApplyDarkTitleBar(hWnd);
        CreateMenuBar(hWnd);
        g_contentPanel = ContentPanel::Create(hWnd, g_hInst);
        ContentPanel::SetLayoutCallback(g_contentPanel, LayoutContentControls);
        ContentPanel::SetStaticCtlColorCallback(g_contentPanel, ContentPanelCtlColorStatic);
        AddTrayIcon();
        CreateMainControls();
        {
            RECT client{};
            GetClientRect(hWnd, &client);
            ResizeMainControls(client.right - client.left, client.bottom - client.top);
        }
        ThemeManager::ApplyTheme(hWnd);
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
        RedrawFully(hWnd);
        return 0;

    case WM_MOUSEWHEEL:
        if (g_contentPanel)
        {
            SendMessageW(g_contentPanel, message, wParam, lParam);
        }
        return 0;

    case WM_DPICHANGED:
    {
        ThemeManager::SetDpi(HIWORD(wParam));
        g_scrollY = 0;
        ContentPanel::SetScrollY(g_contentPanel, 0);
        ApplyFonts();
        const auto* suggestedRect = reinterpret_cast<RECT*>(lParam);
        if (suggestedRect)
        {
            SetWindowPos(hWnd, nullptr, suggestedRect->left, suggestedRect->top,
                suggestedRect->right - suggestedRect->left,
                suggestedRect->bottom - suggestedRect->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }

        RECT client{};
        GetClientRect(hWnd, &client);
        ResizeMainControls(client.right - client.left, client.bottom - client.top);
        ThemeManager::ApplyTheme(hWnd);
        RedrawFully(hWnd);
        return 0;
    }

    case WM_DISPLAYCHANGE:
    {
        ThemeManager::SetDpi(GetDpiForWindow(hWnd));
        ApplyFonts();
        RECT client{};
        GetClientRect(hWnd, &client);
        ResizeMainControls(client.right - client.left, client.bottom - client.top);
        RedrawFully(hWnd);
        return 0;
    }

    case WM_MOVE:
        RedrawFully(hWnd);
        return 0;

    case WM_ERASEBKGND:
        return EraseMainWindowBackground(hWnd, reinterpret_cast<HDC>(wParam));

    case WM_PAINT:
        PaintMainWindow(hWnd);
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ThemeManager::MutedTextColor());
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == g_titleText)
        {
            SetTextColor(hdc, ThemeManager::TextColor());
            return reinterpret_cast<LRESULT>(ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : GetStockObject(WHITE_BRUSH));
        }
        if (control == g_subtitleText || control == g_appIcon)
        {
            return reinterpret_cast<LRESULT>(ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : GetStockObject(WHITE_BRUSH));
        }
        if (control == g_statusText)
        {
            SetTextColor(hdc, ThemeManager::AccentColor());
        }
        if (StaticUsesSurfaceBackground(control))
        {
            return reinterpret_cast<LRESULT>(ThemeManager::SurfaceBrush() ? ThemeManager::SurfaceBrush() : GetStockObject(WHITE_BRUSH));
        }
        return reinterpret_cast<LRESULT>(ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : GetStockObject(WHITE_BRUSH));
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    {
        return reinterpret_cast<LRESULT>(ThemeManager::HandleCtlColor(hWnd, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
    }

    case WM_SETTINGCHANGE:
        ThemeManager::HandleSettingChange(hWnd);
        break;

    case WM_MEASUREITEM:
    {
        auto* measureItem = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measureItem && measureItem->CtlType == ODT_MENU)
        {
            ThemeManager::MeasureMenuItem(*measureItem);
            return TRUE;
        }
        if (measureItem && measureItem->CtlID == IDC_GAME_LIST)
        {
            measureItem->itemHeight = Scale(28);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        const auto* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (drawItem && drawItem->CtlType == ODT_MENU)
        {
            ThemeManager::DrawMenuItem(*drawItem);
            return TRUE;
        }
        if (drawItem && drawItem->CtlType == ODT_BUTTON)
        {
            ThemeManager::DrawButton(*drawItem);
            return TRUE;
        }
        DrawGameListItem(drawItem);
        return TRUE;
    }

    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wParam) == g_gameList)
        {
            ShowGameListContextMenu(lParam);
            return 0;
        }
        break;

    case WM_GETMINMAXINFO:
    {
        auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
        minMaxInfo->ptMinTrackSize.x = Scale(760);
        minMaxInfo->ptMinTrackSize.y = Scale(520);
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
        case IDM_OPEN_CONFIG_DIR:
            OpenConfigDirectory();
            return 0;

        case IDM_OPEN_LOG_DIR:
            OpenLogDirectory();
            return 0;

        case IDM_RESET_CONFIG:
            if (MessageBoxW(hWnd,
                Text(L"确定要重置 FFKeyLock 配置吗？", L"Reset FFKeyLock configuration?"),
                Text(L"重置配置", L"Reset config"),
                MB_YESNO | MB_ICONWARNING) == IDYES)
            {
                g_protectionEnabled = true;
                g_autoDetectEnabled = true;
                g_windowsKeyGuardEnabled = false;
                g_notificationsEnabled = true;
                g_overlayNotificationsEnabled = true;
                g_gameExeNames.clear();
                g_gameExePaths.clear();
                LeaveGameProtection();
                SaveConfig();
                UpdateMainWindow();
            }
            return 0;

        case IDM_TEST_NOTIFICATION:
            ShowTrayNotification(L"FFKeyLock", Text(L"这是一条测试通知。", L"This is a test notification."), true);
            return 0;

        case IDM_MUTE_NOTIFICATIONS:
            g_notificationsEnabled = false;
            g_overlayNotificationsEnabled = false;
            SaveConfig();
            UpdateMainWindow();
            return 0;

        case IDM_HELP_USAGE:
            MessageBoxW(hWnd,
                Text(L"启用保护模式和自动检测后，当前前台窗口属于受保护程序列表时，FFKeyLock 会切换并锁定英文输入法。", L"When protection and auto detect are enabled, FFKeyLock switches protected foreground programs to English input."),
                Text(L"使用说明", L"Usage"),
                MB_OK | MB_ICONINFORMATION);
            return 0;

        case IDM_CHECK_UPDATES:
            ShowTrayNotification(L"FFKeyLock", Text(L"当前版本暂无在线更新检查。", L"Online update checks are not available in this build."));
            return 0;

        case IDM_GITHUB_PROJECT:
            ShellExecuteW(hWnd, L"open", L"https://github.com/", nullptr, nullptr, SW_SHOWNORMAL);
            return 0;

        case IDM_SHOW_SETTINGS:
            ShowMainWindow();
            return 0;

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
            ApplyWindowsKeyGuard();
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
            UpdateMainWindow();
            return 0;

        case IDM_SWITCH_CHINESE:
        case IDC_SWITCH_CN_BUTTON:
            SwitchToChinese(GetCommandTargetWindow());
            UpdateMainWindow();
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

        case IDM_DELETE_SELECTED_GAME:
        case IDC_DELETE_GAME_BUTTON:
            DeleteSelectedGame();
            UpdateMainWindow();
            return 0;

        case IDM_COPY_GAME_NAME:
            CopyTextToClipboard(GetSelectedGameName());
            return 0;

        case IDM_OPEN_GAME_FOLDER:
            OpenSelectedGameFolder();
            return 0;

        case IDM_GAME_LIST_HELP:
        case IDC_GAME_LIST_HELP:
            ShowProtectedListHelp();
            return 0;

        case IDM_BROWSE_PROTECTED:
        case IDC_BROWSE_PROTECTED_BUTTON:
            ShowProtectedProgramsBrowser();
            return 0;

        case IDM_STARTUP:
        case IDC_STARTUP_BUTTON:
            SetStartupEnabled(!IsStartupEnabled());
            UpdateMainWindow();
            return 0;

        case IDM_NOTIFICATIONS:
            g_notificationsEnabled = !g_notificationsEnabled;
            SaveConfig();
            UpdateMainWindow();
            return 0;

        case IDM_OVERLAY_NOTIFICATIONS:
            g_overlayNotificationsEnabled = !g_overlayNotificationsEnabled;
            SaveConfig();
            UpdateMainWindow();
            return 0;

        case IDM_WINDOWS_KEY_GUARD:
        case IDC_WINDOWS_KEY_BUTTON:
            ToggleWindowsKeyGuard();
            return 0;

        case IDM_LANGUAGE_CHINESE:
            g_language = UiLanguage::Chinese;
            SaveConfig();
            ApplyTheme();
            UpdateMainWindow();
            return 0;

        case IDM_LANGUAGE_ENGLISH:
            g_language = UiLanguage::English;
            SaveConfig();
            ApplyTheme();
            UpdateMainWindow();
            return 0;

        case IDM_THEME_LIGHT:
            g_themePreference = ThemePreference::Light;
            SaveConfig();
            ApplyTheme();
            UpdateMainWindow();
            return 0;

        case IDM_THEME_DARK:
            g_themePreference = ThemePreference::Dark;
            SaveConfig();
            ApplyTheme();
            UpdateMainWindow();
            return 0;

        case IDM_THEME_SYSTEM:
            g_themePreference = ThemePreference::System;
            SaveConfig();
            ApplyTheme();
            UpdateMainWindow();
            return 0;

        case IDM_ABOUT:
            MessageBoxW(hWnd,
                Text(L"FFKeyLock\n\n轻量级 Win32 游戏输入法保护工具。\n\nVersion 0.1\nAssembly by brealin", L"FFKeyLock\n\nLightweight Win32 game input-language protection utility.\n\nVersion 0.1\nAssembly by brealin"),
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
        if (g_protectedBrowserWindow)
        {
            DestroyWindow(g_protectedBrowserWindow);
        }
        LeaveGameProtection();
        OverlayNotificationManager::Shutdown();
        DisableWindowsKeyGuard();
        RemoveTrayIcon();
        ThemeManager::Shutdown();
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
        InvalidateFully(g_menuBar);
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
        SetWindowTextW(g_gameListLabel, Text(L"受保护的程序列表", L"Protected program list"));
    }
    if (g_gameListHelpButton)
    {
        SetWindowTextW(g_gameListHelpButton, L"?");
    }
    if (g_addCurrentButton)
    {
        SetWindowTextW(g_addCurrentButton, Text(L"添加受保护程序", L"Add protected"));
    }
    if (g_addFileButton)
    {
        SetWindowTextW(g_addFileButton, Text(L"浏览添加...", L"Browse add..."));
    }
    if (g_deleteGameButton)
    {
        SetWindowTextW(g_deleteGameButton, Text(L"删除选中", L"Delete selected"));
    }
    if (g_browseProtectedButton)
    {
        SetWindowTextW(g_browseProtectedButton, Text(L"浏览列表", L"Browse list"));
    }
    if (g_subtitleText)
    {
        SetWindowTextW(g_subtitleText, Text(L"游戏防误触助手", L"Game mis-touch protection assistant"));
    }
    if (g_statusGroup)
    {
        SetWindowTextW(g_statusGroup, Text(L"当前状态", L"Current status"));
    }
    if (g_featureGroup)
    {
        SetWindowTextW(g_featureGroup, Text(L"功能开关", L"Feature switches"));
    }
    if (g_inputGroup)
    {
        SetWindowTextW(g_inputGroup, Text(L"输入法", L"Input language"));
    }
    if (g_programsGroup)
    {
        SetWindowTextW(g_programsGroup, Text(L"受保护程序", L"Protected programs"));
    }
    if (g_statusText)
    {
        SetWindowTextW(g_statusText, BuildPrimaryStatusText().c_str());
    }
    const bool canShowDetectedGame = g_protectionEnabled && g_autoDetectEnabled && !g_currentDetectedGameName.empty();
    const std::wstring detectedGame = canShowDetectedGame ? g_currentDetectedGameName : Text(L"无", L"None");
    const std::wstring inputLanguage = CurrentInputLanguageText();
    if (g_statusDetectedText)
    {
        SetWindowTextW(g_statusDetectedText, StatusLine(L"当前检测到：", L"Detected: ", detectedGame).c_str());
    }
    if (g_statusInputText)
    {
        SetWindowTextW(g_statusInputText, StatusLine(L"输入法状态：", L"Input language: ", inputLanguage).c_str());
    }
    if (g_statusWinKeyText)
    {
        SetWindowTextW(g_statusWinKeyText, StatusLine(L"Win 键锁定：", L"Win key lock: ", EnabledText(g_windowsKeyGuardEnabled)).c_str());
    }
    if (g_protectionLabel)
    {
        SetWindowTextW(g_protectionLabel, Text(L"保护模式", L"Protection mode"));
    }
    if (g_protectionStateText)
    {
        SetWindowTextW(g_protectionStateText, EnabledText(g_protectionEnabled).c_str());
    }
    if (g_protectionButton)
    {
        SetWindowTextW(g_protectionButton, SwitchActionText(g_protectionEnabled).c_str());
    }
    if (g_autoDetectLabel)
    {
        SetWindowTextW(g_autoDetectLabel, Text(L"自动检测", L"Auto detect"));
    }
    if (g_autoDetectStateText)
    {
        SetWindowTextW(g_autoDetectStateText, EnabledText(g_autoDetectEnabled).c_str());
    }
    if (g_autoDetectButton)
    {
        SetWindowTextW(g_autoDetectButton, SwitchActionText(g_autoDetectEnabled).c_str());
    }
    const bool startupEnabled = IsStartupEnabled();
    if (g_startupLabel)
    {
        SetWindowTextW(g_startupLabel, Text(L"开机启动", L"Startup"));
    }
    if (g_startupStateText)
    {
        SetWindowTextW(g_startupStateText, EnabledText(startupEnabled).c_str());
    }
    if (g_startupButton)
    {
        SetWindowTextW(g_startupButton, SwitchActionText(startupEnabled).c_str());
    }
    if (g_windowsKeyLabel)
    {
        SetWindowTextW(g_windowsKeyLabel, Text(L"Win 键锁定", L"Win key lock"));
    }
    if (g_windowsKeyStateText)
    {
        SetWindowTextW(g_windowsKeyStateText, EnabledText(g_windowsKeyGuardEnabled).c_str());
    }
    if (g_windowsKeyButton)
    {
        SetWindowTextW(g_windowsKeyButton, SwitchActionText(g_windowsKeyGuardEnabled).c_str());
    }
    if (g_inputLanguageText)
    {
        SetWindowTextW(g_inputLanguageText, StatusLine(L"当前输入法：", L"Current input language: ", inputLanguage).c_str());
    }
    if (g_switchChineseButton)
    {
        SetWindowTextW(g_switchChineseButton, Text(L"切换为中文", L"Switch to Chinese"));
    }
    if (g_switchEnglishButton)
    {
        SetWindowTextW(g_switchEnglishButton, Text(L"切换为英文", L"Switch to English"));
    }
    if (g_gameListLabel)
    {
        SetWindowTextW(g_gameListLabel, Text(L"程序列表", L"Program list"));
    }
    if (g_addCurrentButton)
    {
        SetWindowTextW(g_addCurrentButton, Text(L"添加程序", L"Add program"));
    }
    if (g_browseProtectedButton)
    {
        SetWindowTextW(g_browseProtectedButton, Text(L"浏览运行中程序", L"Browse running"));
    }
    if (g_addFileButton)
    {
        SetWindowTextW(g_addFileButton, Text(L"从文件选择", L"Choose file"));
    }
    if (g_deleteGameButton)
    {
        SetWindowTextW(g_deleteGameButton, Text(L"删除选中", L"Delete selected"));
    }
    RefreshGameList();
    RedrawFully(g_hWnd);
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

    POINT roundedPoint{};
    GetCursorPos(&roundedPoint);
    SetForegroundWindow(g_hWnd);
    RoundedMenu::ShowAndDispatch(g_hWnd, roundedPoint, BuildTrayMenu(), true);
}

ATOM RegisterMainWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_FFKEYLOCK));
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = kAppName;
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_FFKEYLOCK));
    return RegisterClassExW(&wcex);
}
}
