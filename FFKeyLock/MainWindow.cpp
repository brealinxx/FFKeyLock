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

struct UiButton
{
    int id = 0;
    RECT rect{};
    std::wstring text;
    bool hot = false;
    bool pressed = false;
    bool enabled = true;
};

struct ContentLayout
{
    RECT icon{};
    RECT title{};
    RECT subtitle{};
    RECT statusCard{};
    RECT statusTitle{};
    RECT primaryStatus{};
    RECT detectedStatus{};
    RECT inputStatus{};
    RECT winKeyStatus{};
    RECT featureCard{};
    RECT programsCard{};
    RECT programLabel{};
    RECT listRect{};
    int contentHeight = 0;
};

ContentLayout g_contentLayout{};
std::vector<UiButton> g_contentButtons;
int g_selectedGameIndex = -1;
int g_gameListTopIndex = 0;

int Scale(int value)
{
    return ThemeManager::Scale(value);
}

int MenuBarHeight()
{
    return Scale(30);
}

void InvalidateWindow(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }

    InvalidateRect(hwnd, nullptr, TRUE);
}

void InvalidateWindowAndChildren(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }

    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
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

void RefreshProtectedBrowserList();
int LayoutContentControls(HWND panel, int width, int height, int scrollY);
void ClampGameListSelection();
int HitTestGameListItem(POINT point, int scrollY);

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
    InvalidateWindow(g_menuBar);
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
        InvalidateWindow(hwnd);
        return 0;

    case WM_MOUSELEAVE:
        InvalidateWindow(hwnd);
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
    ClampGameListSelection();
    InvalidateWindow(g_contentPanel);
    RefreshProtectedBrowserList();
}

std::wstring GetSelectedGameName()
{
    ClampGameListSelection();
    if (g_selectedGameIndex < 0 || g_selectedGameIndex >= static_cast<int>(g_gameExeNames.size()))
    {
        return L"";
    }

    return g_gameExeNames[g_selectedGameIndex];
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

RECT VisualRect(const RECT& logicalRect, int scrollY)
{
    RECT rect = logicalRect;
    OffsetRect(&rect, 0, -scrollY);
    return rect;
}

void DrawPanelText(HDC hdc, const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    GdiUtils::SelectObjectScope fontScope(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(hdc, text.c_str(), -1, &rect, format);
}

UiButton* FindContentButton(int id)
{
    auto it = std::find_if(g_contentButtons.begin(), g_contentButtons.end(), [id](const UiButton& button) {
        return button.id == id;
    });
    return it == g_contentButtons.end() ? nullptr : &(*it);
}

void AddContentButton(int id, const RECT& rect, const std::wstring& text, bool enabled = true)
{
    UiButton button{};
    button.id = id;
    button.rect = rect;
    button.text = text;
    button.enabled = enabled;
    if (const UiButton* oldButton = FindContentButton(id))
    {
        button.hot = oldButton->hot;
        button.pressed = oldButton->pressed;
    }
    g_contentButtons.push_back(std::move(button));
}

void DrawPanelButton(HDC hdc, const UiButton& button, int scrollY)
{
    RECT rect = VisualRect(button.rect, scrollY);
    COLORREF fill = ThemeManager::ButtonColor();
    if (!button.enabled)
    {
        fill = ThemeManager::SurfaceColor();
    }
    else if (button.pressed)
    {
        fill = ThemeManager::ButtonPressedColor();
    }
    else if (button.hot)
    {
        fill = ThemeManager::ButtonHotColor();
    }

    GdiUtils::FillRoundRect(hdc, rect, fill, ThemeManager::BorderColor(), Scale(7));
    DrawPanelText(hdc, button.text, rect, button.enabled ? ThemeManager::TextColor() : ThemeManager::DisabledTextColor(),
        ThemeManager::UiFont(), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawCard(HDC hdc, const RECT& logicalRect, int scrollY, const std::wstring& title)
{
    RECT rect = VisualRect(logicalRect, scrollY);
    GdiUtils::FillRoundRect(hdc, rect, ThemeManager::SurfaceColor(), ThemeManager::BorderColor(), Scale(8));
    RECT titleRect = rect;
    titleRect.left += Scale(16);
    titleRect.right -= Scale(16);
    titleRect.top += Scale(10);
    titleRect.bottom = titleRect.top + Scale(24);
    DrawPanelText(hdc, title, titleRect, ThemeManager::TextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawFeatureRow(HDC hdc, int scrollY, int labelX, int stateX, int y, const std::wstring& label, const std::wstring& value)
{
    RECT labelRect{ labelX, y, labelX + Scale(150), y + Scale(24) };
    RECT stateRect{ stateX, y, stateX + Scale(76), y + Scale(24) };
    DrawPanelText(hdc, label, VisualRect(labelRect, scrollY), ThemeManager::TextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawPanelText(hdc, value, VisualRect(stateRect, scrollY), ThemeManager::MutedTextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

int GameListVisibleRows()
{
    const int itemHeight = Scale(28);
    const int listHeight = g_contentLayout.listRect.bottom - g_contentLayout.listRect.top;
    return itemHeight > 0 ? std::max(1, listHeight / itemHeight) : 1;
}

void ClampGameListSelection()
{
    const int itemCount = static_cast<int>(g_gameExeNames.size());
    if (itemCount <= 0)
    {
        g_selectedGameIndex = -1;
        g_gameListTopIndex = 0;
        return;
    }

    g_selectedGameIndex = std::clamp(g_selectedGameIndex, -1, itemCount - 1);
    const int visibleRows = GameListVisibleRows();
    const int maxTop = std::max(0, itemCount - visibleRows);
    g_gameListTopIndex = std::clamp(g_gameListTopIndex, 0, maxTop);
    if (g_selectedGameIndex >= 0)
    {
        if (g_selectedGameIndex < g_gameListTopIndex)
        {
            g_gameListTopIndex = g_selectedGameIndex;
        }
        else if (g_selectedGameIndex >= g_gameListTopIndex + visibleRows)
        {
            g_gameListTopIndex = std::min(maxTop, g_selectedGameIndex - visibleRows + 1);
        }
    }
}

void DrawProgramList(HDC hdc, int scrollY)
{
    ClampGameListSelection();

    RECT listRect = VisualRect(g_contentLayout.listRect, scrollY);
    GdiUtils::FillRoundRect(hdc, listRect, ThemeManager::WindowColor(), ThemeManager::BorderColor(), Scale(6));

    RECT clipRect = listRect;
    InflateRect(&clipRect, -Scale(1), -Scale(1));
    HRGN previousClip = CreateRectRgn(0, 0, 0, 0);
    const int previousClipType = GetClipRgn(hdc, previousClip);
    HRGN listClip = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right, clipRect.bottom);
    SelectClipRgn(hdc, listClip);
    DeleteObject(listClip);

    const int itemHeight = Scale(28);
    const int itemCount = static_cast<int>(g_gameExeNames.size());
    const int visibleRows = GameListVisibleRows();
    if (itemCount == 0)
    {
        RECT emptyRect = clipRect;
        emptyRect.left += Scale(12);
        DrawPanelText(hdc, Text(L"尚未添加受保护程序", L"No protected programs yet"), emptyRect,
            ThemeManager::MutedTextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        for (int row = 0; row < visibleRows; ++row)
        {
            const int index = g_gameListTopIndex + row;
            if (index >= itemCount)
            {
                break;
            }

            RECT itemRect{
                clipRect.left,
                clipRect.top + row * itemHeight,
                clipRect.right,
                clipRect.top + (row + 1) * itemHeight
            };
            if (index == g_selectedGameIndex)
            {
                HBRUSH selectedBrush = CreateSolidBrush(ThemeManager::ButtonHotColor());
                FillRect(hdc, &itemRect, selectedBrush);
                DeleteObject(selectedBrush);
            }

            RECT textRect = itemRect;
            textRect.left += Scale(10);
            textRect.right -= Scale(12);
            DrawPanelText(hdc, g_gameExeNames[index], textRect, ThemeManager::TextColor(), ThemeManager::UiFont(),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            GdiUtils::DrawSeparator(hdc, itemRect.left + Scale(8), itemRect.right - Scale(8), itemRect.bottom - 1, ThemeManager::BorderColor());
        }
    }

    if (itemCount > visibleRows)
    {
        RECT track{ listRect.right - Scale(8), listRect.top + Scale(4), listRect.right - Scale(4), listRect.bottom - Scale(4) };
        HBRUSH trackBrush = CreateSolidBrush(ThemeManager::SurfaceColor());
        FillRect(hdc, &track, trackBrush);
        DeleteObject(trackBrush);

        const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
        const int thumbHeight = std::max(Scale(18), MulDiv(trackHeight, visibleRows, itemCount));
        const int maxTop = std::max(1, itemCount - visibleRows);
        const int thumbTop = track.top + MulDiv(trackHeight - thumbHeight, g_gameListTopIndex, maxTop);
        RECT thumb{ track.left, thumbTop, track.right, thumbTop + thumbHeight };
        HBRUSH thumbBrush = CreateSolidBrush(ThemeManager::MutedTextColor());
        FillRect(hdc, &thumb, thumbBrush);
        DeleteObject(thumbBrush);
    }

    if (previousClipType == 1)
    {
        SelectClipRgn(hdc, previousClip);
    }
    else
    {
        SelectClipRgn(hdc, nullptr);
    }
    DeleteObject(previousClip);
}

void PaintContentPanel(HWND panel, HDC hdc, const RECT& client, int scrollY)
{
    UNREFERENCED_PARAMETER(panel);
    HRGN clipRegion = CreateRectRgn(client.left, client.top, client.right, client.bottom);
    SelectClipRgn(hdc, clipRegion);
    DeleteObject(clipRegion);

    HICON icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_FFKEYLOCK));
    RECT iconRect = VisualRect(g_contentLayout.icon, scrollY);
    if (icon)
    {
        DrawIconEx(hdc, iconRect.left, iconRect.top, icon, iconRect.right - iconRect.left, iconRect.bottom - iconRect.top, 0, nullptr, DI_NORMAL);
    }
    DrawPanelText(hdc, L"FFKeyLock", VisualRect(g_contentLayout.title, scrollY), ThemeManager::TextColor(), ThemeManager::TitleFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawPanelText(hdc, Text(L"游戏防误触助手", L"Game mis-touch protection assistant"), VisualRect(g_contentLayout.subtitle, scrollY),
        ThemeManager::MutedTextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DrawCard(hdc, g_contentLayout.statusCard, scrollY, Text(L"当前状态", L"Current status"));
    const bool canShowDetectedGame = g_protectionEnabled && g_autoDetectEnabled && !g_currentDetectedGameName.empty();
    const std::wstring detectedGame = canShowDetectedGame ? g_currentDetectedGameName : Text(L"无", L"None");
    const std::wstring inputLanguage = CurrentInputLanguageText();
    DrawPanelText(hdc, BuildPrimaryStatusText(), VisualRect(g_contentLayout.primaryStatus, scrollY), ThemeManager::AccentColor(), ThemeManager::TitleFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawPanelText(hdc, StatusLine(L"当前检测到：", L"Detected: ", detectedGame), VisualRect(g_contentLayout.detectedStatus, scrollY), ThemeManager::TextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawPanelText(hdc, StatusLine(L"输入法状态：", L"Input language: ", inputLanguage), VisualRect(g_contentLayout.inputStatus, scrollY), ThemeManager::TextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawPanelText(hdc, StatusLine(L"Win 键锁定：", L"Win key lock: ", EnabledText(g_windowsKeyGuardEnabled)), VisualRect(g_contentLayout.winKeyStatus, scrollY), ThemeManager::TextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DrawCard(hdc, g_contentLayout.featureCard, scrollY, Text(L"功能开关", L"Feature switches"));
    const int margin = Scale(24);
    const int contentWidth = g_contentLayout.featureCard.right - g_contentLayout.featureCard.left;
    const int groupInnerLeft = margin + Scale(20);
    const int switchStateX = margin + Scale(160);
    const int secondColX = margin + std::max(Scale(430), contentWidth / 2 + Scale(16));
    const int secondStateX = secondColX + Scale(132);
    DrawFeatureRow(hdc, scrollY, groupInnerLeft, switchStateX, g_contentLayout.featureCard.top + Scale(34), Text(L"保护模式", L"Protection mode"), EnabledText(g_protectionEnabled));
    DrawFeatureRow(hdc, scrollY, groupInnerLeft, switchStateX, g_contentLayout.featureCard.top + Scale(78), Text(L"自动检测", L"Auto detect"), EnabledText(g_autoDetectEnabled));
    DrawFeatureRow(hdc, scrollY, secondColX, secondStateX, g_contentLayout.featureCard.top + Scale(34), Text(L"开机启动", L"Startup"), EnabledText(IsStartupEnabled()));
    DrawFeatureRow(hdc, scrollY, secondColX, secondStateX, g_contentLayout.featureCard.top + Scale(78), Text(L"Win 键锁定", L"Win key lock"), EnabledText(g_windowsKeyGuardEnabled));

    DrawCard(hdc, g_contentLayout.programsCard, scrollY, Text(L"受保护程序", L"Protected programs"));
    DrawPanelText(hdc, Text(L"程序列表", L"Program list"), VisualRect(g_contentLayout.programLabel, scrollY), ThemeManager::TextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DrawProgramList(hdc, scrollY);

    for (const UiButton& button : g_contentButtons)
    {
        DrawPanelButton(hdc, button, scrollY);
    }
    SelectClipRgn(hdc, nullptr);
}

int HitTestContentPanelButton(HWND, POINT point, int scrollY)
{
    POINT logicalPoint{ point.x, point.y + scrollY };
    for (const UiButton& button : g_contentButtons)
    {
        if (button.enabled && PtInRect(&button.rect, logicalPoint))
        {
            return button.id;
        }
    }
    return 0;
}

void SetContentButtonState(HWND, int id, bool hot, bool pressed)
{
    if (UiButton* button = FindContentButton(id))
    {
        button->hot = hot;
        button->pressed = pressed;
    }
}

int HitTestGameListItem(POINT point, int scrollY)
{
    POINT logicalPoint{ point.x, point.y + scrollY };
    if (!PtInRect(&g_contentLayout.listRect, logicalPoint))
    {
        return -1;
    }

    const int itemHeight = Scale(28);
    if (itemHeight <= 0)
    {
        return -1;
    }

    const int row = (logicalPoint.y - g_contentLayout.listRect.top) / itemHeight;
    const int index = g_gameListTopIndex + row;
    return index >= 0 && index < static_cast<int>(g_gameExeNames.size()) ? index : -1;
}

void ContentPanelClicked(HWND panel, POINT point, int scrollY)
{
    const int index = HitTestGameListItem(point, scrollY);
    if (index >= 0)
    {
        g_selectedGameIndex = index;
        ClampGameListSelection();
        InvalidateWindow(panel);
    }
}

bool ContentPanelWheel(HWND, POINT point, int scrollY, int delta)
{
    POINT logicalPoint{ point.x, point.y + scrollY };
    if (!PtInRect(&g_contentLayout.listRect, logicalPoint))
    {
        return false;
    }

    const int itemCount = static_cast<int>(g_gameExeNames.size());
    const int visibleRows = GameListVisibleRows();
    const int maxTop = std::max(0, itemCount - visibleRows);
    if (maxTop <= 0)
    {
        return false;
    }

    const int lines = std::max(1, std::abs(delta) / WHEEL_DELTA);
    if (delta > 0)
    {
        g_gameListTopIndex -= lines;
    }
    else
    {
        g_gameListTopIndex += lines;
    }
    g_gameListTopIndex = std::clamp(g_gameListTopIndex, 0, maxTop);
    return true;
}

void CreateMainControls()
{
    UpdateMainWindow();
}

int LayoutContentControls(HWND panel, int width, int height, int scrollY)
{
    UNREFERENCED_PARAMETER(panel);
    {
        g_scrollY = std::max(0, scrollY);
        const int viewportHeight = std::max(0, height);
        const int margin = Scale(24);
        const int groupSpacing = Scale(18);
        const int buttonHeight = Scale(32);
        const int buttonGap = Scale(10);
        const int switchButtonWidth = Scale(104);
        const int actionButtonWidth = Scale(130);
        const int contentWidth = std::max(Scale(320), width - margin * 2);
        const bool twoColumnFeatures = contentWidth >= Scale(680);
        const bool wideProgramActions = contentWidth >= Scale(620);
        const int headerY = Scale(18);
        const int headerHeight = Scale(80);
        const int statusY = headerY + headerHeight + groupSpacing;
        const int statusHeight = Scale(158);
        const int featureY = statusY + statusHeight + groupSpacing;
        const int featureHeight = twoColumnFeatures ? Scale(126) : Scale(220);
        const int programsY = featureY + featureHeight + groupSpacing;
        const int programsHeight = std::max(wideProgramActions ? Scale(250) : Scale(360), viewportHeight - programsY - margin);
        g_contentHeight = programsY + programsHeight + margin;
        const int maxScroll = std::max(0, g_contentHeight - viewportHeight);
        g_scrollY = std::clamp(g_scrollY, 0, maxScroll);

        const int groupInnerLeft = margin + Scale(20);
        const int groupRight = margin + contentWidth;
        const int switchButtonX = margin + Scale(254);
        const int secondColX = margin + std::max(Scale(430), contentWidth / 2 + Scale(16));
        const int secondButtonX = groupRight - Scale(20) - switchButtonWidth;
        const int listX = groupInnerLeft;
        const int listY = programsY + Scale(52);
        const int actionButtonX = wideProgramActions ? groupRight - Scale(20) - actionButtonWidth : listX;
        const int listWidth = wideProgramActions
            ? std::max(Scale(260), actionButtonX - Scale(20) - listX)
            : std::max(Scale(260), groupRight - Scale(20) - listX);
        const int listHeight = wideProgramActions ? std::max(Scale(128), programsHeight - Scale(74)) : Scale(160);
        const int actionButtonY = wideProgramActions ? listY : listY + listHeight + Scale(14);
        const int narrowActionWidth = std::max(Scale(120), (listWidth - buttonGap) / 2);

        g_contentLayout.icon = RECT{ margin, headerY + Scale(10), margin + Scale(48), headerY + Scale(58) };
        g_contentLayout.title = RECT{ margin + Scale(64), headerY + Scale(8), groupRight, headerY + Scale(38) };
        g_contentLayout.subtitle = RECT{ margin + Scale(64), headerY + Scale(42), groupRight, headerY + Scale(64) };
        g_contentLayout.statusCard = RECT{ margin, statusY, margin + contentWidth, statusY + statusHeight };
        g_contentLayout.primaryStatus = RECT{ groupInnerLeft, statusY + Scale(30), groupInnerLeft + Scale(320), statusY + Scale(60) };
        g_contentLayout.detectedStatus = RECT{ groupInnerLeft, statusY + Scale(68), groupRight - Scale(20), statusY + Scale(92) };
        g_contentLayout.inputStatus = RECT{ groupInnerLeft, statusY + Scale(96), groupRight - Scale(20), statusY + Scale(120) };
        g_contentLayout.winKeyStatus = RECT{ groupInnerLeft, statusY + Scale(124), groupRight - Scale(20), statusY + Scale(148) };
        g_contentLayout.featureCard = RECT{ margin, featureY, margin + contentWidth, featureY + featureHeight };
        g_contentLayout.programsCard = RECT{ margin, programsY, margin + contentWidth, programsY + programsHeight };
        g_contentLayout.programLabel = RECT{ listX, programsY + Scale(28), listX + Scale(110), programsY + Scale(52) };
        g_contentLayout.listRect = RECT{ listX, listY, listX + listWidth, listY + listHeight };
        g_contentLayout.contentHeight = g_contentHeight;

        g_contentButtons.clear();
        AddContentButton(IDC_PROTECTION_BUTTON, RECT{ switchButtonX, featureY + Scale(28), switchButtonX + switchButtonWidth, featureY + Scale(28) + buttonHeight }, SwitchActionText(g_protectionEnabled));
        AddContentButton(IDC_AUTO_DETECT_BUTTON, RECT{ switchButtonX, featureY + Scale(72), switchButtonX + switchButtonWidth, featureY + Scale(72) + buttonHeight }, SwitchActionText(g_autoDetectEnabled));
        if (twoColumnFeatures)
        {
            AddContentButton(IDC_STARTUP_BUTTON, RECT{ secondButtonX, featureY + Scale(28), secondButtonX + switchButtonWidth, featureY + Scale(28) + buttonHeight }, SwitchActionText(IsStartupEnabled()));
            AddContentButton(IDC_WINDOWS_KEY_BUTTON, RECT{ secondButtonX, featureY + Scale(72), secondButtonX + switchButtonWidth, featureY + Scale(72) + buttonHeight }, SwitchActionText(g_windowsKeyGuardEnabled));
        }
        else
        {
            AddContentButton(IDC_STARTUP_BUTTON, RECT{ switchButtonX, featureY + Scale(116), switchButtonX + switchButtonWidth, featureY + Scale(116) + buttonHeight }, SwitchActionText(IsStartupEnabled()));
            AddContentButton(IDC_WINDOWS_KEY_BUTTON, RECT{ switchButtonX, featureY + Scale(160), switchButtonX + switchButtonWidth, featureY + Scale(160) + buttonHeight }, SwitchActionText(g_windowsKeyGuardEnabled));
        }
        AddContentButton(IDC_GAME_LIST_HELP, RECT{ listX + Scale(110), programsY + Scale(24), listX + Scale(138), programsY + Scale(52) }, L"?");
        AddContentButton(IDC_ADD_GAME_BUTTON, RECT{ actionButtonX, listY, actionButtonX + actionButtonWidth, listY + buttonHeight }, Text(L"添加程序", L"Add program"));
        AddContentButton(IDC_BROWSE_PROTECTED_BUTTON, RECT{ actionButtonX, listY + Scale(42), actionButtonX + actionButtonWidth, listY + Scale(42) + buttonHeight }, Text(L"浏览运行中程序", L"Browse running"));
        AddContentButton(IDC_ADD_FILE_BUTTON, RECT{ actionButtonX, listY + Scale(84), actionButtonX + actionButtonWidth, listY + Scale(84) + buttonHeight }, Text(L"从文件选择", L"Choose file"));
        AddContentButton(IDC_DELETE_GAME_BUTTON, RECT{ actionButtonX, listY + Scale(126), actionButtonX + actionButtonWidth, listY + Scale(126) + buttonHeight }, Text(L"删除选中", L"Delete selected"));

        ClampGameListSelection();
        return g_contentHeight;
    }
}

void ResizeMainControls(int width, int height)
{
    const int menuHeight = MenuBarHeight();
    if (g_menuBar)
    {
        SetWindowPos(g_menuBar, HWND_TOP, 0, 0, width, menuHeight, SWP_NOACTIVATE);
        InvalidateWindow(g_menuBar);
    }
    if (g_contentPanel)
    {
        SetWindowPos(g_contentPanel, nullptr, 0, menuHeight, width, std::max(0, height - menuHeight), SWP_NOZORDER | SWP_NOACTIVATE);
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
        InvalidateWindowAndChildren(g_hWnd);
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
    ClampGameListSelection();
    const int selected = g_selectedGameIndex;
    if (selected < 0 || selected >= static_cast<int>(g_gameExeNames.size()))
    {
        MessageBoxW(g_hWnd,
            Text(L"请选择一个受保护程序。", L"Select an item in the protected program list first."),
            L"FFKeyLock",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring exeName = g_gameExeNames[selected];
    g_gameExeNames.erase(g_gameExeNames.begin() + selected);
    g_gameExePaths.erase(exeName);
    g_selectedGameIndex = std::min(selected, static_cast<int>(g_gameExeNames.size()) - 1);
    ClampGameListSelection();
    SaveConfig();
    RefreshGameList();
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
        InvalidateWindowAndChildren(hwnd);
        return 0;
    }

    case WM_SIZE:
        InvalidateWindow(hwnd);
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
    POINT point{};
    if (lParam == -1)
    {
        if (g_selectedGameIndex < 0)
        {
            return;
        }
        RECT selectedRect = VisualRect(g_contentLayout.listRect, g_scrollY);
        if (g_selectedGameIndex >= 0)
        {
            const int row = g_selectedGameIndex - g_gameListTopIndex;
            selectedRect.top += row * Scale(28);
            selectedRect.bottom = selectedRect.top + Scale(28);
        }
        point.x = selectedRect.left + Scale(16);
        point.y = selectedRect.top + Scale(14);
        ClientToScreen(g_contentPanel, &point);
    }
    else
    {
        point.x = GET_X_LPARAM(lParam);
        point.y = GET_Y_LPARAM(lParam);
        POINT clientPoint = point;
        ScreenToClient(g_contentPanel, &clientPoint);
        const int index = HitTestGameListItem(clientPoint, g_scrollY);
        if (index < 0)
        {
            return;
        }
        else
        {
            g_selectedGameIndex = index;
            ClampGameListSelection();
            InvalidateWindow(g_contentPanel);
        }
    }

    if (GetSelectedGameName().empty())
    {
        return;
    }

    RoundedMenu::ShowAndDispatch(g_hWnd, point, BuildGameListContextMenu());
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
        ContentPanel::SetPaintCallback(g_contentPanel, PaintContentPanel);
        ContentPanel::SetButtonHitTestCallback(g_contentPanel, HitTestContentPanelButton);
        ContentPanel::SetButtonStateCallback(g_contentPanel, SetContentButtonState);
        ContentPanel::SetMouseClickCallback(g_contentPanel, ContentPanelClicked);
        ContentPanel::SetMouseWheelCallback(g_contentPanel, ContentPanelWheel);
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
        InvalidateRect(hWnd, nullptr, TRUE);
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
        InvalidateWindowAndChildren(hWnd);
        return 0;
    }

    case WM_DISPLAYCHANGE:
    {
        ThemeManager::SetDpi(GetDpiForWindow(hWnd));
        ApplyFonts();
        RECT client{};
        GetClientRect(hWnd, &client);
        ResizeMainControls(client.right - client.left, client.bottom - client.top);
        InvalidateWindowAndChildren(hWnd);
        return 0;
    }

    case WM_MOVE:
        InvalidateWindow(hWnd);
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
        SetTextColor(hdc, ThemeManager::TextColor());
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
        return FALSE;
    }

    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wParam) == g_contentPanel)
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
        InvalidateWindow(g_menuBar);
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
    if (g_contentPanel)
    {
        ContentPanel::Relayout(g_contentPanel);
    }
    InvalidateWindowAndChildren(g_hWnd);
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
    wcex.style = 0;
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


