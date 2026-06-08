#include "MainWindow.h"

#include "AppState.h"
#include "Config.h"
#include "GameProtection.h"
#include "InputLanguage.h"
#include "Localization.h"
#include "OverlayNotificationManager.h"
#include "Resource.h"
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
HWND g_protectedBrowserWindow = nullptr;
HWND g_protectedBrowserList = nullptr;
HWND g_menuBar = nullptr;
std::vector<RECT> g_menuBarItems;

int Scale(int value)
{
    return ThemeManager::Scale(value);
}

int MenuBarHeight()
{
    return Scale(30);
}

void SetDefaultFont(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::UiFont() ? ThemeManager::UiFont() : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

void ApplyFonts()
{
    HWND controls[] = {
        g_statusText,
        g_protectionButton,
        g_autoDetectButton,
        g_startupButton,
        g_windowsKeyButton,
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
    HWND control = CreateWindowExW(
        0,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        Scale(x),
        Scale(y) + MenuBarHeight(),
        Scale(width),
        Scale(height),
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

void RefreshProtectedBrowserList();

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
    InvalidateRect(g_menuBar, nullptr, FALSE);
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
    HBRUSH background = CreateSolidBrush(ThemeManager::MenuBarColor());
    FillRect(hdc, &client, background);
    DeleteObject(background);

    HPEN linePen = CreatePen(PS_SOLID, 1, ThemeManager::MenuBorderColor());
    HGDIOBJ oldPen = SelectObject(hdc, linePen);
    MoveToEx(hdc, client.left, client.bottom - 1, nullptr);
    LineTo(hdc, client.right, client.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(linePen);

    const wchar_t* labels[] = {
        Text(L"设置", L"Settings"),
        Text(L"通知", L"Notifications"),
        Text(L"帮助", L"Help"),
    };

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, ThemeManager::TextColor());
    HGDIOBJ oldFont = SelectObject(hdc, ThemeManager::UiFont() ? ThemeManager::UiFont() : GetStockObject(DEFAULT_GUI_FONT));
    g_menuBarItems.clear();
    int x = Scale(8);
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);
    for (const auto* label : labels)
    {
        SIZE size{};
        GetTextExtentPoint32W(hdc, label, static_cast<int>(wcslen(label)), &size);
        RECT rect{ x, Scale(3), x + size.cx + Scale(24), client.bottom - Scale(3) };
        g_menuBarItems.push_back(rect);
        if (PtInRect(&rect, cursor))
        {
            HBRUSH hover = CreateSolidBrush(ThemeManager::MenuBarHoverColor());
            FillRect(hdc, &rect, hover);
            DeleteObject(hover);
        }
        RECT textRect = rect;
        textRect.left += Scale(12);
        textRect.right -= Scale(12);
        DrawTextW(hdc, label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        x = rect.right + Scale(2);
    }
    SelectObject(hdc, oldFont);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK MenuBarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        PaintMenuBar(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSELEAVE:
        InvalidateRect(hwnd, nullptr, FALSE);
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
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        MenuBarHeight(),
        parent,
        nullptr,
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
    g_titleText = CreateControl(L"STATIC", L"FFKeyLock", SS_LEFT, 24, 20, 180, 28, IDC_STATIC);
    if (g_titleText)
    {
        SendMessageW(g_titleText, WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::TitleFont() ? ThemeManager::TitleFont() : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    g_statusText = CreateControl(L"STATIC", L"", SS_LEFT, 24, 56, 600, 24, IDC_STATUS_TEXT);

    g_protectionButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 24, 96, 148, 36, IDC_PROTECTION_BUTTON);
    g_autoDetectButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 184, 96, 148, 36, IDC_AUTO_DETECT_BUTTON);
    g_startupButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 344, 96, 148, 36, IDC_STARTUP_BUTTON);
    g_windowsKeyButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 504, 96, 120, 36, IDC_WINDOWS_KEY_BUTTON);

    g_switchEnglishButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 24, 144, 148, 34, IDC_SWITCH_EN_BUTTON);
    g_switchChineseButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 184, 144, 148, 34, IDC_SWITCH_CN_BUTTON);

    g_gameListLabel = CreateControl(L"STATIC", L"", SS_LEFT, 24, 204, 126, 24, IDC_STATIC);
    g_gameListHelpButton = CreateControl(L"BUTTON", L"?", BS_PUSHBUTTON, 148, 201, 24, 24, IDC_GAME_LIST_HELP);
    g_gameList = CreateWindowExW(
        0,
        L"LISTBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        Scale(24),
        Scale(236) + MenuBarHeight(),
        Scale(430),
        Scale(150),
        g_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_GAME_LIST)),
        g_hInst,
        nullptr);
    if (g_gameList)
    {
        SetDefaultFont(g_gameList);
    }

    g_browseProtectedButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 470, 198, 118, 32, IDC_BROWSE_PROTECTED_BUTTON);
    g_addCurrentButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 470, 236, 118, 32, IDC_ADD_GAME_BUTTON);
    g_addFileButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 470, 278, 118, 32, IDC_ADD_FILE_BUTTON);
    g_deleteGameButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 470, 320, 118, 32, IDC_DELETE_GAME_BUTTON);
    UpdateMainWindow();
}

void ResizeMainControls(int width, int height)
{
    const int topOffset = MenuBarHeight();
    const int margin = Scale(24);
    const int gap = Scale(16);
    const int rightButtonWidth = Scale(118);
    const int rightButtonX = std::max(Scale(470), width - margin - rightButtonWidth);
    const int listX = margin;
    const int listY = Scale(236);
    const int listWidth = std::max(Scale(260), rightButtonX - gap - listX);
    const int listHeight = std::max(Scale(132), height - topOffset - Scale(260));
    const int labelY = Scale(204);
    const int buttonHeight = Scale(32);

    if (g_titleText)
    {
        MoveWindow(g_titleText, margin, topOffset + Scale(20), Scale(180), Scale(28), TRUE);
    }
    if (g_statusText)
    {
        MoveWindow(g_statusText, margin, topOffset + Scale(56), std::max(Scale(260), width - margin * 2), Scale(24), TRUE);
    }
    if (g_protectionButton)
    {
        MoveWindow(g_protectionButton, margin, topOffset + Scale(96), Scale(148), Scale(36), TRUE);
    }
    if (g_autoDetectButton)
    {
        MoveWindow(g_autoDetectButton, margin + Scale(160), topOffset + Scale(96), Scale(148), Scale(36), TRUE);
    }
    if (g_startupButton)
    {
        MoveWindow(g_startupButton, margin + Scale(320), topOffset + Scale(96), Scale(148), Scale(36), TRUE);
    }
    if (g_windowsKeyButton)
    {
        MoveWindow(g_windowsKeyButton, margin + Scale(480), topOffset + Scale(96), Scale(120), Scale(36), TRUE);
    }
    if (g_switchEnglishButton)
    {
        MoveWindow(g_switchEnglishButton, margin, topOffset + Scale(144), Scale(148), Scale(34), TRUE);
    }
    if (g_switchChineseButton)
    {
        MoveWindow(g_switchChineseButton, margin + Scale(160), topOffset + Scale(144), Scale(148), Scale(34), TRUE);
    }
    if (g_gameListLabel)
    {
        MoveWindow(g_gameListLabel, listX, topOffset + labelY, Scale(122), Scale(24), TRUE);
    }
    if (g_gameList)
    {
        MoveWindow(g_gameList, listX, topOffset + listY, listWidth, listHeight, TRUE);
    }
    if (g_gameListHelpButton)
    {
        MoveWindow(g_gameListHelpButton, listX + Scale(124), topOffset + labelY - Scale(3), Scale(24), Scale(24), TRUE);
    }
    if (g_browseProtectedButton)
    {
        MoveWindow(g_browseProtectedButton, rightButtonX, topOffset + labelY - Scale(6), rightButtonWidth, buttonHeight, TRUE);
    }
    if (g_addCurrentButton)
    {
        MoveWindow(g_addCurrentButton, rightButtonX, topOffset + listY, rightButtonWidth, buttonHeight, TRUE);
    }
    if (g_addFileButton)
    {
        MoveWindow(g_addFileButton, rightButtonX, topOffset + listY + Scale(42), rightButtonWidth, buttonHeight, TRUE);
    }
    if (g_deleteGameButton)
    {
        MoveWindow(g_deleteGameButton, rightButtonX, topOffset + listY + Scale(84), rightButtonWidth, buttonHeight, TRUE);
    }
    if (g_menuBar)
    {
        MoveWindow(g_menuBar, 0, 0, width, topOffset, TRUE);
    }

    InvalidateRect(g_hWnd, nullptr, TRUE);
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
        InvalidateRect(g_menuBar, nullptr, TRUE);
    }
    if (g_gameList)
    {
        InvalidateRect(g_gameList, nullptr, TRUE);
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
    const int nextSelection = std::min(selected, static_cast<int>(g_gameExeNames.size()) - 1);
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
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
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
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ThemeManager::TextColor());
        return reinterpret_cast<LRESULT>(ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : GetStockObject(BLACK_BRUSH));
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
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
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
    FillRect(hdc, &client, ThemeManager::WindowBrush());

    auto drawPanel = [&](RECT rect)
    {
        HBRUSH brush = CreateSolidBrush(ThemeManager::SurfaceColor());
        HPEN pen = CreatePen(PS_SOLID, 1, ThemeManager::BorderColor());
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    };

    RECT controlsCard{ Scale(16), Scale(88), client.right - Scale(16), Scale(188) };
    RECT listCard{ Scale(16), Scale(192), client.right - Scale(16), client.bottom - Scale(16) };
    OffsetRect(&controlsCard, 0, MenuBarHeight());
    OffsetRect(&listCard, 0, MenuBarHeight());
    drawPanel(controlsCard);
    drawPanel(listCard);
    EndPaint(hWnd, &paint);
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
        ThemeManager::Initialize(GetDpiForWindow(hWnd));
        ThemeManager::ApplyDarkTitleBar(hWnd);
        CreateMenuBar(hWnd);
        AddTrayIcon();
        CreateMainControls();
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
        return 0;

    case WM_DPICHANGED:
    {
        ThemeManager::SetDpi(HIWORD(wParam));
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
        return 0;
    }

    case WM_ERASEBKGND:
    {
        RECT client{};
        GetClientRect(hWnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        return 1;
    }

    case WM_PAINT:
        PaintMainWindow(hWnd);
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ThemeManager::MutedTextColor());
        if (reinterpret_cast<HWND>(lParam) == g_titleText)
        {
            SetTextColor(hdc, ThemeManager::TextColor());
            return reinterpret_cast<LRESULT>(ThemeManager::WindowBrush() ? ThemeManager::WindowBrush() : GetStockObject(BLACK_BRUSH));
        }
        return reinterpret_cast<LRESULT>(ThemeManager::SurfaceBrush() ? ThemeManager::SurfaceBrush() : GetStockObject(BLACK_BRUSH));
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
        minMaxInfo->ptMinTrackSize.x = Scale(640);
        minMaxInfo->ptMinTrackSize.y = Scale(500);
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
        InvalidateRect(g_menuBar, nullptr, TRUE);
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

    POINT roundedPoint{};
    GetCursorPos(&roundedPoint);
    SetForegroundWindow(g_hWnd);
    RoundedMenu::ShowAndDispatch(g_hWnd, roundedPoint, BuildTrayMenu(), true);
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
