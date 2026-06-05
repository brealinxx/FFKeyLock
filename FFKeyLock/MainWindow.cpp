#include "MainWindow.h"

#include "AppState.h"
#include "Config.h"
#include "GameProtection.h"
#include "InputLanguage.h"
#include "Localization.h"
#include "OverlayNotificationManager.h"
#include "Resource.h"
#include "ThemeManager.h"
#include "TrayIcon.h"
#include "WindowsKeyGuard.h"

#include <commdlg.h>
#include <shellapi.h>
#include <strsafe.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace FFKeyLock
{
void DestroyTrayMenu();

namespace
{
constexpr wchar_t kProtectedBrowserClass[] = L"FFKeyLockProtectedBrowser";
constexpr wchar_t kTrayMenuClass[] = L"FFKeyLockTrayMenu";
HWND g_protectedBrowserWindow = nullptr;
HWND g_protectedBrowserList = nullptr;
HWND g_trayMenuWindow = nullptr;
std::vector<std::unique_ptr<std::wstring>> g_menuTextStore;

struct TrayMenuItem
{
    UINT id;
    std::wstring text;
    bool checked;
    bool enabled;
    bool separator;
};

std::vector<TrayMenuItem> g_trayMenuItems;
int g_trayMenuHover = -1;

int Scale(int value)
{
    return ThemeManager::Scale(value);
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
        g_configText,
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
        Scale(y),
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

COLORREF AccentColor()
{
    return RGB(34, 197, 94);
}

COLORREF WarningColor()
{
    return RGB(234, 179, 8);
}

COLORREF DangerColor()
{
    return RGB(239, 68, 68);
}

COLORREF HoverColor()
{
    return RGB(38, 54, 73);
}

void FillRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border, int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

struct CardControl
{
    static void Draw(HDC hdc, const RECT& rect)
    {
        FillRoundRect(hdc, rect, ThemeManager::SurfaceColor(), ThemeManager::BorderColor(), Scale(12));
    }
};

struct ToggleSwitchControl
{
    static void Draw(HDC hdc, const RECT& rect, bool on)
    {
        const COLORREF track = on ? AccentColor() : RGB(75, 85, 99);
        FillRoundRect(hdc, rect, track, track, Scale(18));

        const int knob = (rect.bottom - rect.top) - Scale(8);
        RECT knobRect{
            on ? rect.right - Scale(4) - knob : rect.left + Scale(4),
            rect.top + Scale(4),
            on ? rect.right - Scale(4) : rect.left + Scale(4) + knob,
            rect.bottom - Scale(4),
        };
        FillRoundRect(hdc, knobRect, RGB(243, 244, 246), RGB(243, 244, 246), knob);
    }
};

HFONT CreatePaintFont(int pointSize, int weight)
{
    LOGFONTW font{};
    font.lfHeight = -MulDiv(pointSize, GetDpiForWindow(g_hWnd), 72);
    font.lfWeight = weight;
    font.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(font.lfFaceName, std::size(font.lfFaceName), IsEnglish() ? L"Segoe UI" : L"Microsoft YaHei UI");
    return CreateFontIndirectW(&font);
}

void DrawTextBlock(HDC hdc, const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HGDIOBJ oldFont = SelectObject(hdc, font ? font : ThemeManager::UiFont());
    DrawTextW(hdc, text.c_str(), -1, &rect, format);
    SelectObject(hdc, oldFont);
}

RECT ScaledRect(int left, int top, int right, int bottom)
{
    return RECT{ Scale(left), Scale(top), Scale(right), Scale(bottom) };
}

void DrawToggleGlyph(HDC hdc, RECT rect, bool on)
{
    ToggleSwitchControl::Draw(hdc, rect, on);
}

std::wstring* StoreMenuText(const std::wstring& text)
{
    g_menuTextStore.push_back(std::make_unique<std::wstring>(text));
    return g_menuTextStore.back().get();
}

void ApplyMenuTheme(HMENU menu)
{
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = ThemeManager::SurfaceBrush();
    SetMenuInfo(menu, &info);
}

void AppendThemedMenuItem(HMENU menu, UINT id, const std::wstring& text, bool checked = false, bool enabled = true)
{
    AppendMenuW(
        menu,
        MF_OWNERDRAW | (checked ? MF_CHECKED : MF_UNCHECKED) | (enabled ? MF_ENABLED : MF_DISABLED),
        id,
        reinterpret_cast<LPCWSTR>(StoreMenuText(text)));
}

void AppendThemedMenuPopup(HMENU menu, HMENU popup, const std::wstring& text)
{
    ApplyMenuTheme(popup);
    AppendMenuW(menu, MF_OWNERDRAW | MF_POPUP, reinterpret_cast<UINT_PTR>(popup), reinterpret_cast<LPCWSTR>(StoreMenuText(text)));
}

HMENU CreateAppMenu()
{
    g_menuTextStore.clear();
    HMENU menuBar = CreateMenu();
    HMENU settingsMenu = CreatePopupMenu();
    HMENU languageMenu = CreatePopupMenu();
    HMENU themeMenu = CreatePopupMenu();
    HMENU notificationMenu = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();

    ApplyMenuTheme(menuBar);
    ApplyMenuTheme(settingsMenu);
    ApplyMenuTheme(languageMenu);
    ApplyMenuTheme(themeMenu);
    ApplyMenuTheme(notificationMenu);
    ApplyMenuTheme(helpMenu);

    AppendThemedMenuItem(languageMenu, IDM_LANGUAGE_CHINESE, Text(L"中文", L"Chinese"), !IsEnglish());
    AppendThemedMenuItem(languageMenu, IDM_LANGUAGE_ENGLISH, Text(L"English", L"English"), IsEnglish());

    AppendThemedMenuItem(themeMenu, IDM_THEME_LIGHT, Text(L"浅色", L"Light"), g_themePreference == ThemePreference::Light);
    AppendThemedMenuItem(themeMenu, IDM_THEME_DARK, Text(L"深色", L"Dark"), g_themePreference == ThemePreference::Dark);
    AppendThemedMenuItem(themeMenu, IDM_THEME_SYSTEM, Text(L"跟随系统", L"System"), g_themePreference == ThemePreference::System);

    AppendThemedMenuPopup(settingsMenu, languageMenu, Text(L"语言", L"Language"));
    AppendThemedMenuPopup(settingsMenu, themeMenu, Text(L"主题", L"Theme"));
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, nullptr);
    AppendThemedMenuItem(settingsMenu, IDM_WINDOWS_KEY_GUARD,
        g_windowsKeyGuardEnabled ? Text(L"Win 键：禁用", L"Windows key: Disabled") : Text(L"Win 键：开启", L"Windows key: Enabled"), g_windowsKeyGuardEnabled);
    AppendThemedMenuItem(settingsMenu, IDM_SWITCH_ENGLISH, Text(L"切换输入法到英文", L"Switch input to English"));
    AppendThemedMenuItem(settingsMenu, IDM_SWITCH_CHINESE, Text(L"切换输入法到中文", L"Switch input to Chinese"));

    AppendThemedMenuItem(notificationMenu, IDM_NOTIFICATIONS,
        g_notificationsEnabled ? Text(L"系统通知：开启", L"System notifications: On") : Text(L"系统通知：关闭", L"System notifications: Off"), g_notificationsEnabled);
    AppendThemedMenuItem(notificationMenu, IDM_OVERLAY_NOTIFICATIONS,
        g_overlayNotificationsEnabled ? Text(L"Overlay 通知：开启", L"Overlay notifications: On") : Text(L"Overlay 通知：关闭", L"Overlay notifications: Off"), g_overlayNotificationsEnabled);

    AppendThemedMenuItem(helpMenu, IDM_ABOUT, Text(L"关于 FFKeyLock", L"About FFKeyLock"));

    AppendThemedMenuPopup(menuBar, settingsMenu, Text(L"设置", L"Settings"));
    AppendThemedMenuPopup(menuBar, notificationMenu, Text(L"通知", L"Notifications"));
    AppendThemedMenuPopup(menuBar, helpMenu, Text(L"帮助", L"Help"));
    return menuBar;
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
    g_titleText = CreateControl(L"STATIC", L"", SS_LEFT, 0, 0, 0, 0, IDC_STATIC);
    g_statusText = CreateControl(L"STATIC", L"", SS_LEFT, 0, 0, 0, 0, IDC_STATUS_TEXT);

    g_protectionButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 32, 302, 210, 88, IDC_PROTECTION_BUTTON);
    g_autoDetectButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 258, 302, 210, 88, IDC_AUTO_DETECT_BUTTON);
    g_startupButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 484, 302, 210, 88, IDC_STARTUP_BUTTON);
    g_windowsKeyButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 710, 302, 210, 88, IDC_WINDOWS_KEY_BUTTON);

    g_switchEnglishButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 690, 636, 128, 34, IDC_SWITCH_EN_BUTTON);
    g_switchChineseButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 830, 636, 128, 34, IDC_SWITCH_CN_BUTTON);

    g_gameListLabel = CreateControl(L"STATIC", L"", SS_LEFT, 0, 0, 0, 0, IDC_STATIC);
    g_gameListHelpButton = CreateControl(L"BUTTON", L"?", BS_PUSHBUTTON, 812, 438, 40, 34, IDC_GAME_LIST_HELP);
    g_gameList = CreateWindowExW(
        0,
        L"LISTBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_CLIPSIBLINGS,
        Scale(32),
        Scale(486),
        Scale(590),
        Scale(178),
        g_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_GAME_LIST)),
        g_hInst,
        nullptr);
    if (g_gameList)
    {
        SetDefaultFont(g_gameList);
    }

    g_browseProtectedButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 352, 438, 132, 34, IDC_BROWSE_PROTECTED_BUTTON);
    g_addCurrentButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 492, 438, 132, 34, IDC_ADD_GAME_BUTTON);
    g_addFileButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 632, 438, 132, 34, IDC_ADD_FILE_BUTTON);
    g_deleteGameButton = CreateControl(L"BUTTON", L"", BS_PUSHBUTTON, 772, 438, 132, 34, IDC_DELETE_GAME_BUTTON);
    g_configText = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | ES_READONLY | ES_AUTOHSCROLL,
        0,
        0,
        0,
        0,
        g_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONFIG_TEXT)),
        g_hInst,
        nullptr);
    if (g_configText)
    {
        SetDefaultFont(g_configText);
        SendMessageW(g_configText, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(Scale(8), Scale(8)));
    }
    UpdateMainWindow();
}

void ResizeMainControls(int width, int height)
{
    const int margin = Scale(32);
    const int gap = Scale(16);
    const int contentWidth = std::max(Scale(720), width - margin * 2);
    const int quickTop = Scale(302);
    const int quickWidth = std::max(Scale(156), (contentWidth - gap * 3) / 4);
    const int quickHeight = Scale(88);
    const int listTop = Scale(486);
    const int footerTop = std::max(Scale(628), height - Scale(64));
    const int listHeight = std::max(Scale(112), footerTop - listTop - Scale(18));
    const int toolTop = Scale(438);
    const int toolWidth = std::max(Scale(104), std::min(Scale(132), (width - margin * 2 - Scale(404)) / 4));
    const int toolStart = std::max(margin + Scale(300), width - margin - Scale(40) - gap - toolWidth * 4 - gap * 3);

    if (g_titleText)
    {
        MoveWindow(g_titleText, 0, 0, 0, 0, FALSE);
    }
    if (g_statusText)
    {
        MoveWindow(g_statusText, 0, 0, 0, 0, FALSE);
    }
    if (g_protectionButton)
    {
        MoveWindow(g_protectionButton, margin, quickTop, quickWidth, quickHeight, TRUE);
    }
    if (g_autoDetectButton)
    {
        MoveWindow(g_autoDetectButton, margin + (quickWidth + gap), quickTop, quickWidth, quickHeight, TRUE);
    }
    if (g_startupButton)
    {
        MoveWindow(g_startupButton, margin + (quickWidth + gap) * 2, quickTop, quickWidth, quickHeight, TRUE);
    }
    if (g_windowsKeyButton)
    {
        MoveWindow(g_windowsKeyButton, margin + (quickWidth + gap) * 3, quickTop, quickWidth, quickHeight, TRUE);
    }
    if (g_switchEnglishButton)
    {
        MoveWindow(g_switchEnglishButton, width - margin - Scale(268), footerTop + Scale(8), Scale(128), Scale(34), TRUE);
    }
    if (g_switchChineseButton)
    {
        MoveWindow(g_switchChineseButton, width - margin - Scale(128), footerTop + Scale(8), Scale(128), Scale(34), TRUE);
    }
    if (g_gameListLabel)
    {
        MoveWindow(g_gameListLabel, 0, 0, 0, 0, FALSE);
    }
    if (g_gameList)
    {
        MoveWindow(g_gameList, margin, listTop, contentWidth, listHeight, TRUE);
    }
    if (g_gameListHelpButton)
    {
        MoveWindow(g_gameListHelpButton, width - margin - Scale(40), toolTop, Scale(40), Scale(34), TRUE);
    }
    if (g_browseProtectedButton)
    {
        MoveWindow(g_browseProtectedButton, toolStart, toolTop, toolWidth, Scale(34), TRUE);
    }
    if (g_addCurrentButton)
    {
        MoveWindow(g_addCurrentButton, toolStart + (toolWidth + gap), toolTop, toolWidth, Scale(34), TRUE);
    }
    if (g_addFileButton)
    {
        MoveWindow(g_addFileButton, toolStart + (toolWidth + gap) * 2, toolTop, toolWidth, Scale(34), TRUE);
    }
    if (g_deleteGameButton)
    {
        MoveWindow(g_deleteGameButton, toolStart + (toolWidth + gap) * 3, toolTop, toolWidth, Scale(34), TRUE);
    }
    if (g_configText)
    {
        MoveWindow(g_configText, 0, 0, 0, 0, FALSE);
    }

    InvalidateRect(g_hWnd, nullptr, TRUE);
}

void ApplyTheme()
{
    ThemeManager::Initialize(GetDpiForWindow(g_hWnd));
    ThemeManager::ApplyTheme(g_hWnd);
    ApplyFonts();
    if (g_configText)
    {
        SendMessageW(g_configText, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(Scale(8), Scale(8)));
    }
    if (g_protectedBrowserWindow)
    {
        ThemeManager::ApplyTheme(g_protectedBrowserWindow);
    }
    if (g_hWnd)
    {
        HMENU oldMenu = GetMenu(g_hWnd);
        SetMenu(g_hWnd, CreateAppMenu());
        if (oldMenu)
        {
            DestroyMenu(oldMenu);
        }
        DrawMenuBar(g_hWnd);
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

void AppendMenuItem(HMENU menu, UINT id, const std::wstring& text, bool checked = false, bool enabled = true)
{
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID | MIIM_DATA | MIIM_FTYPE | MIIM_STATE;
    item.fType = MFT_OWNERDRAW;
    item.wID = id;
    item.dwItemData = reinterpret_cast<ULONG_PTR>(StoreMenuText(text));
    item.fState = (checked ? MFS_CHECKED : MFS_UNCHECKED) | (enabled ? MFS_ENABLED : MFS_DISABLED);
    InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &item);
}

void AppendMenuSeparator(HMENU menu)
{
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
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

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }
    ApplyMenuTheme(menu);

    AppendMenuItem(menu, IDM_COPY_GAME_NAME, Text(L"复制名称", L"Copy name"));
    AppendMenuItem(menu, IDM_OPEN_GAME_FOLDER, Text(L"跳转到该程序文件夹", L"Open program folder"));
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0, g_hWnd, nullptr);
    DestroyMenu(menu);
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

    HFONT brandFont = CreatePaintFont(18, FW_SEMIBOLD);
    HFONT titleFont = CreatePaintFont(17, FW_SEMIBOLD);
    HFONT sectionFont = CreatePaintFont(13, FW_SEMIBOLD);
    HFONT valueFont = CreatePaintFont(24, FW_BOLD);
    HFONT bodyFont = CreatePaintFont(10, FW_NORMAL);
    HFONT smallFont = CreatePaintFont(9, FW_NORMAL);

    RECT brandIcon = ScaledRect(32, 24, 62, 54);
    FillRoundRect(hdc, brandIcon, AccentColor(), AccentColor(), Scale(10));
    DrawTextBlock(hdc, L"F", brandIcon, RGB(17, 24, 39), titleFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(hdc, L"FFKeyLock", ScaledRect(74, 21, 260, 58), ThemeManager::TextColor(), brandFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(hdc, L"概览", ScaledRect(32, 80, 160, 116), ThemeManager::TextColor(), titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(hdc, L"快速查看当前状态和常用操作", ScaledRect(32, 112, 340, 136), ThemeManager::MutedTextColor(), bodyFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT toggleText{ client.right - Scale(210), Scale(36), client.right - Scale(92), Scale(64) };
    DrawTextBlock(hdc, L"保护模式", toggleText, ThemeManager::TextColor(), bodyFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawToggleGlyph(hdc, RECT{ client.right - Scale(76), Scale(34), client.right - Scale(32), Scale(58) }, g_protectionEnabled);

    const int margin = Scale(32);
    const int gap = Scale(16);
    const int contentWidth = std::max(Scale(720), static_cast<int>(client.right) - margin * 2);
    const int statusWidth = std::max(Scale(156), (contentWidth - gap * 3) / 4);
    const int statusTop = Scale(142);
    struct StatusCard
    {
        const wchar_t* title;
        std::wstring value;
        const wchar_t* subtitle;
        COLORREF color;
    };
    std::array<StatusCard, 4> statuses{ {
        { L"保护状态", g_protectionEnabled ? L"已开启" : L"关闭", L"正在保护您的按键和系统", g_protectionEnabled ? AccentColor() : DangerColor() },
        { L"自动检测", g_autoDetectEnabled ? L"开启" : L"关闭", L"监控游戏进程中", g_autoDetectEnabled ? AccentColor() : DangerColor() },
        { L"Win 键", g_windowsKeyGuardEnabled ? L"已禁用" : L"未禁用", L"屏蔽 Win 键功能", g_windowsKeyGuardEnabled ? AccentColor() : WarningColor() },
        { L"受保护程序", std::to_wstring(g_gameExeNames.size()), L"已添加的程序数量", AccentColor() },
    } };

    for (size_t i = 0; i < statuses.size(); ++i)
    {
        RECT card{ margin + static_cast<int>(i) * (statusWidth + gap), statusTop, margin + static_cast<int>(i) * (statusWidth + gap) + statusWidth, statusTop + Scale(106) };
        CardControl::Draw(hdc, card);
        DrawTextBlock(hdc, statuses[i].title, RECT{ card.left + Scale(22), card.top + Scale(18), card.right - Scale(18), card.top + Scale(42) }, ThemeManager::TextColor(), bodyFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextBlock(hdc, statuses[i].value, RECT{ card.left + Scale(22), card.top + Scale(44), card.right - Scale(18), card.top + Scale(80) }, statuses[i].color, valueFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextBlock(hdc, statuses[i].subtitle, RECT{ card.left + Scale(22), card.top + Scale(80), card.right - Scale(18), card.bottom - Scale(12) }, ThemeManager::MutedTextColor(), smallFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    DrawTextBlock(hdc, L"快速操作", ScaledRect(32, 270, 180, 296), ThemeManager::TextColor(), sectionFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT listCard{ Scale(32), Scale(416), client.right - Scale(32), std::max(Scale(608), static_cast<int>(client.bottom) - Scale(82)) };
    CardControl::Draw(hdc, listCard);
    DrawTextBlock(hdc, L"受保护的程序", RECT{ listCard.left + Scale(20), listCard.top + Scale(18), listCard.left + Scale(220), listCard.top + Scale(48) }, ThemeManager::TextColor(), sectionFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(hdc, (std::to_wstring(g_gameExeNames.size()) + L" 个"), RECT{ listCard.left + Scale(210), listCard.top + Scale(18), listCard.left + Scale(280), listCard.top + Scale(48) }, AccentColor(), sectionFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT footer{ Scale(32), std::max(Scale(628), static_cast<int>(client.bottom) - Scale(64)), client.right - Scale(32), client.bottom - Scale(18) };
    FillRoundRect(hdc, footer, ThemeManager::SurfaceColor(), ThemeManager::BorderColor(), Scale(10));
    DrawTextBlock(hdc, L"配置文件:", RECT{ footer.left + Scale(18), footer.top, footer.left + Scale(96), footer.bottom }, ThemeManager::MutedTextColor(), smallFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(hdc, g_configPath, RECT{ footer.left + Scale(96), footer.top, footer.right - Scale(304), footer.bottom }, ThemeManager::TextColor(), smallFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);

    DeleteObject(brandFont);
    DeleteObject(titleFont);
    DeleteObject(sectionFont);
    DeleteObject(valueFont);
    DeleteObject(bodyFont);
    DeleteObject(smallFont);
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
        SetMenu(hWnd, CreateAppMenu());
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
            measureItem->itemHeight = Scale(36);
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
        minMaxInfo->ptMinTrackSize.x = Scale(900);
        minMaxInfo->ptMinTrackSize.y = Scale(620);
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
        DestroyTrayMenu();
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

int TrayMenuItemHeight(const TrayMenuItem& item)
{
    return item.separator ? Scale(10) : Scale(34);
}

int HitTestTrayMenuItem(int y)
{
    int cursor = Scale(10);
    for (size_t i = 0; i < g_trayMenuItems.size(); ++i)
    {
        const int height = TrayMenuItemHeight(g_trayMenuItems[i]);
        if (!g_trayMenuItems[i].separator && y >= cursor && y < cursor + height)
        {
            return static_cast<int>(i);
        }
        cursor += height;
    }
    return -1;
}

void DestroyTrayMenu()
{
    if (g_trayMenuWindow)
    {
        HWND window = g_trayMenuWindow;
        g_trayMenuWindow = nullptr;
        DestroyWindow(window);
    }
}

void PaintTrayMenu(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRoundRect(hdc, client, ThemeManager::SurfaceColor(), ThemeManager::BorderColor(), Scale(12));

    HFONT itemFont = CreatePaintFont(10, FW_NORMAL);
    HFONT statusFont = CreatePaintFont(10, FW_SEMIBOLD);
    int y = Scale(10);
    for (size_t i = 0; i < g_trayMenuItems.size(); ++i)
    {
        const TrayMenuItem& item = g_trayMenuItems[i];
        const int itemHeight = TrayMenuItemHeight(item);
        if (item.separator)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, ThemeManager::BorderColor());
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            MoveToEx(hdc, Scale(14), y + Scale(5), nullptr);
            LineTo(hdc, client.right - Scale(14), y + Scale(5));
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            y += itemHeight;
            continue;
        }

        RECT row{ Scale(8), y, client.right - Scale(8), y + itemHeight };
        if (static_cast<int>(i) == g_trayMenuHover && item.enabled)
        {
            FillRoundRect(hdc, row, HoverColor(), HoverColor(), Scale(8));
        }
        if (item.checked)
        {
            RECT mark{ row.left + Scale(12), row.top + Scale(10), row.left + Scale(22), row.top + Scale(20) };
            FillRoundRect(hdc, mark, AccentColor(), AccentColor(), Scale(5));
        }

        RECT textRect{ row.left + Scale(34), row.top, row.right - Scale(14), row.bottom };
        DrawTextBlock(hdc, item.text, textRect, item.enabled ? ThemeManager::TextColor() : ThemeManager::MutedTextColor(),
            item.id == 0 ? statusFont : itemFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += itemHeight;
    }

    DeleteObject(itemFont);
    DeleteObject(statusFont);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK TrayMenuProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        PaintTrayMenu(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
    {
        const int hit = HitTestTrayMenuItem(GET_Y_LPARAM(lParam));
        if (hit != g_trayMenuHover)
        {
            g_trayMenuHover = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_trayMenuHover = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
    {
        const int hit = HitTestTrayMenuItem(GET_Y_LPARAM(lParam));
        if (hit >= 0 && hit < static_cast<int>(g_trayMenuItems.size()))
        {
            const TrayMenuItem item = g_trayMenuItems[hit];
            if (item.enabled && item.id != 0)
            {
                DestroyTrayMenu();
                SendMessageW(g_hWnd, WM_COMMAND, MAKEWPARAM(item.id, 0), 0);
                return 0;
            }
        }
        DestroyTrayMenu();
        return 0;
    }
    case WM_RBUTTONUP:
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        DestroyTrayMenu();
        return 0;
    case WM_NCDESTROY:
        if (g_trayMenuWindow == hwnd)
        {
            g_trayMenuWindow = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterTrayMenuClass()
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = TrayMenuProc;
    wcex.hInstance = g_hInst;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = ThemeManager::SurfaceBrush();
    wcex.lpszClassName = kTrayMenuClass;
    RegisterClassExW(&wcex);
    registered = true;
}

void AddTrayMenuItem(UINT id, const std::wstring& text, bool checked = false, bool enabled = true)
{
    g_trayMenuItems.push_back({ id, text, checked, enabled, false });
}

void AddTrayMenuSeparator()
{
    g_trayMenuItems.push_back({ 0, L"", false, false, true });
}

void ShowTrayMenu()
{
    g_menuTargetWindow = GetForegroundWindow();
    RememberExternalForegroundWindow(g_menuTargetWindow);
    POINT point{};
    GetCursorPos(&point);

    DestroyTrayMenu();
    RegisterTrayMenuClass();
    g_trayMenuItems.clear();
    AddTrayMenuItem(IDM_SHOW_WINDOW, Text(L"显示主窗口", L"Show main window"));
    AddTrayMenuSeparator();
    AddTrayMenuItem(0, g_inGameProtection ? Text(L"状态：游戏保护中", L"Status: Protecting game") : Text(L"状态：普通", L"Status: Normal"), false, false);
    AddTrayMenuItem(IDM_PROTECTION, g_protectionEnabled ? Text(L"保护模式：开启", L"Protection: On") : Text(L"保护模式：关闭", L"Protection: Off"), g_protectionEnabled);
    AddTrayMenuItem(IDM_AUTO_DETECT, g_autoDetectEnabled ? Text(L"自动检测：开启", L"Auto detect: On") : Text(L"自动检测：关闭", L"Auto detect: Off"), g_autoDetectEnabled);
    AddTrayMenuItem(IDM_WINDOWS_KEY_GUARD, g_windowsKeyGuardEnabled ? Text(L"Win 键：禁用", L"Windows key: Disabled") : Text(L"Win 键：启用", L"Windows key: Enabled"), g_windowsKeyGuardEnabled);
    AddTrayMenuSeparator();
    AddTrayMenuItem(IDM_SWITCH_ENGLISH, Text(L"切换输入法到英文", L"Switch input to English"));
    AddTrayMenuItem(IDM_SWITCH_CHINESE, Text(L"切换输入法到中文", L"Switch input to Chinese"));
    AddTrayMenuSeparator();
    AddTrayMenuItem(IDM_ADD_CURRENT_GAME, Text(L"添加受保护程序", L"Add protected program"));
    AddTrayMenuItem(IDM_ADD_GAME_FILE, Text(L"浏览添加受保护程序...", L"Browse to add protected program..."));
    AddTrayMenuSeparator();
    const bool startupEnabled = IsStartupEnabled();
    AddTrayMenuItem(IDM_STARTUP, startupEnabled ? Text(L"开机启动：开启", L"Startup: On") : Text(L"开机启动：关闭", L"Startup: Off"), startupEnabled);
    AddTrayMenuItem(IDM_NOTIFICATIONS, g_notificationsEnabled ? Text(L"系统通知：开启", L"System notifications: On") : Text(L"系统通知：关闭", L"System notifications: Off"), g_notificationsEnabled);
    AddTrayMenuItem(IDM_OVERLAY_NOTIFICATIONS, g_overlayNotificationsEnabled ? Text(L"Overlay 通知：开启", L"Overlay notifications: On") : Text(L"Overlay 通知：关闭", L"Overlay notifications: Off"), g_overlayNotificationsEnabled);
    AddTrayMenuSeparator();
    AddTrayMenuItem(IDM_EXIT, Text(L"退出", L"Exit"));

    int menuHeight = Scale(20);
    for (const auto& item : g_trayMenuItems)
    {
        menuHeight += TrayMenuItemHeight(item);
    }
    const int menuWidth = Scale(292);
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{ sizeof(monitorInfo) };
    GetMonitorInfoW(monitor, &monitorInfo);
    int x = point.x;
    int y = point.y - menuHeight;
    if (x + menuWidth > monitorInfo.rcWork.right)
    {
        x = monitorInfo.rcWork.right - menuWidth - Scale(6);
    }
    if (y < monitorInfo.rcWork.top)
    {
        y = point.y + Scale(8);
    }

    g_trayMenuHover = -1;
    g_trayMenuWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kTrayMenuClass,
        L"",
        WS_POPUP,
        x,
        y,
        menuWidth,
        menuHeight,
        g_hWnd,
        nullptr,
        g_hInst,
        nullptr);
    if (g_trayMenuWindow)
    {
        SetWindowPos(g_trayMenuWindow, HWND_TOPMOST, x, y, menuWidth, menuHeight, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetFocus(g_trayMenuWindow);
    }
}

void ShowLegacyTrayMenu()
{
    g_menuTargetWindow = GetForegroundWindow();
    RememberExternalForegroundWindow(g_menuTargetWindow);

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }

    AppendMenuItem(menu, IDM_SHOW_WINDOW, Text(L"显示主窗口", L"Show main window"));
    AppendMenuSeparator(menu);

    AppendMenuItem(menu, 0, g_inGameProtection ? Text(L"当前状态：游戏保护中", L"Status: Protecting game") : Text(L"当前状态：普通", L"Status: Normal"), false, false);
    AppendMenuItem(menu, IDM_PROTECTION, g_protectionEnabled ? Text(L"保护模式：开启", L"Protection: On") : Text(L"保护模式：关闭", L"Protection: Off"), g_protectionEnabled);
    AppendMenuItem(menu, IDM_AUTO_DETECT, g_autoDetectEnabled ? Text(L"自动检测：开启", L"Auto detect: On") : Text(L"自动检测：关闭", L"Auto detect: Off"), g_autoDetectEnabled);
    AppendMenuItem(menu, IDM_WINDOWS_KEY_GUARD, g_windowsKeyGuardEnabled ? Text(L"Win 键：禁用", L"Windows key: Disabled") : Text(L"Win 键：开启", L"Windows key: Enabled"), g_windowsKeyGuardEnabled);
    AppendMenuSeparator(menu);

    AppendMenuItem(menu, IDM_SWITCH_ENGLISH, Text(L"切换输入法到英文", L"Switch input to English"));
    AppendMenuItem(menu, IDM_SWITCH_CHINESE, Text(L"切换输入法到中文", L"Switch input to Chinese"));
    AppendMenuSeparator(menu);

    AppendMenuItem(menu, IDM_ADD_CURRENT_GAME, Text(L"添加受保护程序", L"Add protected program"));
    AppendMenuItem(menu, IDM_ADD_GAME_FILE, Text(L"浏览添加受保护程序...", L"Browse to add protected program..."));
    AppendMenuSeparator(menu);

    const bool startupEnabled = IsStartupEnabled();
    AppendMenuItem(menu, IDM_STARTUP, startupEnabled ? Text(L"开机启动：开启", L"Startup: On") : Text(L"开机启动：关闭", L"Startup: Off"), startupEnabled);
    AppendMenuItem(menu, IDM_NOTIFICATIONS, g_notificationsEnabled ? Text(L"系统通知：开启", L"System notifications: On") : Text(L"系统通知：关闭", L"System notifications: Off"), g_notificationsEnabled);
    AppendMenuItem(menu, IDM_OVERLAY_NOTIFICATIONS, g_overlayNotificationsEnabled ? Text(L"Overlay 通知：开启", L"Overlay notifications: On") : Text(L"Overlay 通知：关闭", L"Overlay notifications: Off"), g_overlayNotificationsEnabled);
    AppendMenuSeparator(menu);

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
