#include "ThemeManager.h"

#include "GameProtection.h"
#include "Localization.h"
#include "Resource.h"

#include <dwmapi.h>
#include <strsafe.h>
#include <uxtheme.h>

#include <algorithm>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")

namespace FFKeyLock
{
namespace
{
constexpr wchar_t kButtonSubclassName[] = L"FFKeyLock.ThemeButtonSubclass";

UINT g_dpi = USER_DEFAULT_SCREEN_DPI;
HFONT g_uiFont = nullptr;
HFONT g_titleFont = nullptr;
HBRUSH g_windowBrush = nullptr;
HBRUSH g_surfaceBrush = nullptr;
HBRUSH g_cardBrush = nullptr;

COLORREF g_windowColor = RGB(32, 32, 32);
COLORREF g_surfaceColor = RGB(43, 43, 43);
COLORREF g_cardColor = RGB(43, 43, 43);
COLORREF g_buttonColor = RGB(58, 58, 58);
COLORREF g_buttonHotColor = RGB(72, 72, 72);
COLORREF g_buttonPressedColor = RGB(85, 85, 85);
COLORREF g_textColor = RGB(230, 230, 230);
COLORREF g_mutedTextColor = RGB(184, 184, 184);
COLORREF g_disabledTextColor = RGB(122, 122, 122);
COLORREF g_borderColor = RGB(82, 82, 82);
COLORREF g_accentColor = RGB(86, 156, 214);
COLORREF g_selectedColor = RGB(52, 80, 112);
bool g_dark = true;

bool SystemUsesDarkTheme()
{
    HKEY key = nullptr;
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(key);
    }
    return value == 0;
}

bool ResolveDarkTheme()
{
    if (g_themePreference == ThemePreference::Dark)
    {
        return true;
    }
    if (g_themePreference == ThemePreference::Light)
    {
        return false;
    }
    return SystemUsesDarkTheme();
}

HFONT CreateThemeFont(int pointSize, int weight)
{
    LOGFONTW font{};
    font.lfHeight = -MulDiv(pointSize, static_cast<int>(g_dpi), 72);
    font.lfWeight = weight;
    font.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(font.lfFaceName, std::size(font.lfFaceName), IsEnglish() ? L"Segoe UI" : L"Microsoft YaHei UI");
    return CreateFontIndirectW(&font);
}

void DeleteThemeResources()
{
    if (g_uiFont)
    {
        DeleteObject(g_uiFont);
        g_uiFont = nullptr;
    }
    if (g_titleFont)
    {
        DeleteObject(g_titleFont);
        g_titleFont = nullptr;
    }
    if (g_windowBrush)
    {
        DeleteObject(g_windowBrush);
        g_windowBrush = nullptr;
    }
    if (g_surfaceBrush)
    {
        DeleteObject(g_surfaceBrush);
        g_surfaceBrush = nullptr;
    }
    if (g_cardBrush)
    {
        DeleteObject(g_cardBrush);
        g_cardBrush = nullptr;
    }
}

void RebuildResources()
{
    DeleteThemeResources();
    g_dark = ResolveDarkTheme();
    if (g_dark)
    {
        g_windowColor = RGB(17, 24, 39);
        g_surfaceColor = RGB(31, 41, 55);
        g_cardColor = RGB(31, 41, 55);
        g_buttonColor = RGB(31, 41, 55);
        g_buttonHotColor = RGB(38, 54, 73);
        g_buttonPressedColor = RGB(43, 65, 88);
        g_textColor = RGB(243, 244, 246);
        g_mutedTextColor = RGB(156, 163, 175);
        g_disabledTextColor = RGB(107, 114, 128);
        g_borderColor = RGB(55, 65, 81);
        g_accentColor = RGB(34, 197, 94);
        g_selectedColor = RGB(22, 101, 52);
    }
    else
    {
        g_windowColor = RGB(248, 250, 252);
        g_surfaceColor = RGB(255, 255, 255);
        g_cardColor = RGB(255, 255, 255);
        g_buttonColor = RGB(245, 247, 250);
        g_buttonHotColor = RGB(235, 241, 249);
        g_buttonPressedColor = RGB(222, 233, 246);
        g_textColor = RGB(30, 41, 59);
        g_mutedTextColor = RGB(71, 85, 105);
        g_disabledTextColor = RGB(148, 163, 184);
        g_borderColor = RGB(203, 213, 225);
        g_accentColor = RGB(37, 99, 235);
        g_selectedColor = RGB(219, 234, 254);
    }

    g_uiFont = CreateThemeFont(9, FW_NORMAL);
    g_titleFont = CreateThemeFont(16, FW_SEMIBOLD);
    g_windowBrush = CreateSolidBrush(g_windowColor);
    g_surfaceBrush = CreateSolidBrush(g_surfaceColor);
    g_cardBrush = CreateSolidBrush(g_cardColor);
}

void SetBoolWindowAttribute(HWND hwnd, DWORD attribute, BOOL value)
{
    DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
}

LRESULT CALLBACK ButtonSubclassProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    UNREFERENCED_PARAMETER(refData);

    switch (message)
    {
    case WM_MOUSEMOVE:
        if (!GetPropW(button, L"FFKeyLock.ButtonHot"))
        {
            SetPropW(button, L"FFKeyLock.ButtonHot", reinterpret_cast<HANDLE>(1));
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, button, 0 };
            TrackMouseEvent(&track);
            InvalidateRect(button, nullptr, TRUE);
        }
        break;

    case WM_MOUSELEAVE:
        RemovePropW(button, L"FFKeyLock.ButtonHot");
        InvalidateRect(button, nullptr, TRUE);
        break;

    case WM_NCDESTROY:
        RemovePropW(button, L"FFKeyLock.ButtonHot");
        RemovePropW(button, kButtonSubclassName);
        RemoveWindowSubclass(button, ButtonSubclassProc, subclassId);
        break;
    }

    return DefSubclassProc(button, message, wParam, lParam);
}

void ApplyControlTheme(HWND hwnd)
{
    wchar_t className[64]{};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));

    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    if (_wcsicmp(className, L"Button") == 0)
    {
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, style | BS_OWNERDRAW);
        if (!GetPropW(hwnd, kButtonSubclassName))
        {
            SetPropW(hwnd, kButtonSubclassName, reinterpret_cast<HANDLE>(1));
            SetWindowSubclass(hwnd, ButtonSubclassProc, 1, 0);
        }
        SetWindowTheme(hwnd, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
    else if (_wcsicmp(className, L"Edit") == 0 || _wcsicmp(className, L"ListBox") == 0 || _wcsicmp(className, L"ComboBox") == 0)
    {
        SetWindowTheme(hwnd, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
    else if (_wcsicmp(className, L"SysListView32") == 0 || _wcsicmp(className, L"SysTreeView32") == 0 || _wcsicmp(className, L"SysHeader32") == 0)
    {
        SetWindowTheme(hwnd, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }

    InvalidateRect(hwnd, nullptr, TRUE);
}

BOOL CALLBACK ApplyThemeToChild(HWND child, LPARAM)
{
    ApplyControlTheme(child);
    return TRUE;
}

void DrawRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border, int radius)
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
}

void ThemeManager::Initialize(UINT dpi)
{
    g_dpi = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
    RebuildResources();
}

void ThemeManager::Shutdown()
{
    DeleteThemeResources();
}

void ThemeManager::SetDpi(UINT dpi)
{
    g_dpi = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
    RebuildResources();
}

int ThemeManager::Scale(int value)
{
    return MulDiv(value, g_dpi, USER_DEFAULT_SCREEN_DPI);
}

void ThemeManager::ApplyTheme(HWND root)
{
    if (!g_uiFont)
    {
        RebuildResources();
    }

    if (!root)
    {
        return;
    }

    ApplyDarkTitleBar(root);
    SendMessageW(root, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    EnumChildWindows(root, ApplyThemeToChild, 0);
    InvalidateRect(root, nullptr, TRUE);
    RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void ThemeManager::ApplyDarkTitleBar(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }

    BOOL dark = g_dark ? TRUE : FALSE;
    SetBoolWindowAttribute(hwnd, 20, dark);
    SetBoolWindowAttribute(hwnd, 19, dark);
}

void ThemeManager::HandleSettingChange(HWND root)
{
    if (g_themePreference != ThemePreference::System)
    {
        return;
    }

    RebuildResources();
    ApplyTheme(root);
}

HBRUSH ThemeManager::HandleCtlColor(HWND, HDC hdc, HWND)
{
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, g_surfaceColor);
    SetTextColor(hdc, g_textColor);
    return g_surfaceBrush ? g_surfaceBrush : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
}

void ThemeManager::DrawButton(const DRAWITEMSTRUCT& item)
{
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = GetPropW(item.hwndItem, L"FFKeyLock.ButtonHot") != nullptr;
    const int controlId = GetDlgCtrlID(item.hwndItem);

    COLORREF fill = g_buttonColor;
    if (disabled)
    {
        fill = g_dark ? RGB(45, 45, 45) : RGB(241, 245, 249);
    }
    else if (pressed)
    {
        fill = g_buttonPressedColor;
    }
    else if (hot)
    {
        fill = g_buttonHotColor;
    }

    RECT rect = item.rcItem;
    const bool cardButton =
        controlId == IDC_PROTECTION_BUTTON ||
        controlId == IDC_AUTO_DETECT_BUTTON ||
        controlId == IDC_STARTUP_BUTTON ||
        controlId == IDC_WINDOWS_KEY_BUTTON;

    DrawRoundRect(item.hDC, rect, fill, hot && !disabled ? g_accentColor : g_borderColor, Scale(cardButton ? 12 : 8));

    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? g_disabledTextColor : g_textColor);
    HGDIOBJ oldFont = SelectObject(item.hDC, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));
    if (pressed)
    {
        OffsetRect(&rect, 1, 1);
    }

    if (cardButton)
    {
        const wchar_t* icon = L"";
        const wchar_t* title = text;
        const wchar_t* subtitle = L"";
        bool on = false;
        switch (controlId)
        {
        case IDC_PROTECTION_BUTTON:
            icon = L"盾";
            title = g_protectionEnabled ? Text(L"关闭保护模式", L"Disable protection") : Text(L"开启保护模式", L"Enable protection");
            subtitle = g_protectionEnabled ? Text(L"暂停所有保护", L"Pause every protection") : Text(L"启用所有保护功能", L"Enable every protection");
            on = g_protectionEnabled;
            break;
        case IDC_AUTO_DETECT_BUTTON:
            icon = L"检";
            title = g_autoDetectEnabled ? Text(L"禁用自动检测", L"Disable auto detect") : Text(L"启用自动检测", L"Enable auto detect");
            subtitle = g_autoDetectEnabled ? Text(L"停止进程监控", L"Stop process monitoring") : Text(L"检测游戏自动开启保护", L"Detect games automatically");
            on = g_autoDetectEnabled;
            break;
        case IDC_STARTUP_BUTTON:
            icon = L"启";
            title = IsStartupEnabled() ? Text(L"关闭开机启动", L"Disable startup") : Text(L"开启开机启动", L"Enable startup");
            subtitle = Text(L"系统启动时自动运行", L"Run when Windows starts");
            on = IsStartupEnabled();
            break;
        case IDC_WINDOWS_KEY_BUTTON:
            icon = L"Win";
            title = g_windowsKeyGuardEnabled ? Text(L"启用 Win 键", L"Enable Win key") : Text(L"禁用 Win 键", L"Disable Win key");
            subtitle = Text(L"屏蔽 Win 键功能", L"Block Windows key");
            on = g_windowsKeyGuardEnabled;
            break;
        }

        RECT iconRect{ rect.left + Scale(20), rect.top + Scale(18), rect.left + Scale(54), rect.top + Scale(52) };
        DrawRoundRect(item.hDC, iconRect, on ? RGB(22, 101, 52) : RGB(55, 65, 81), on ? g_accentColor : g_borderColor, Scale(17));
        SetTextColor(item.hDC, on ? g_accentColor : g_mutedTextColor);
        DrawTextW(item.hDC, icon, -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT titleRect{ rect.left + Scale(68), rect.top + Scale(20), rect.right - Scale(16), rect.top + Scale(48) };
        SetTextColor(item.hDC, disabled ? g_disabledTextColor : g_textColor);
        DrawTextW(item.hDC, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT subtitleRect{ rect.left + Scale(68), rect.top + Scale(48), rect.right - Scale(16), rect.bottom - Scale(18) };
        SetTextColor(item.hDC, disabled ? g_disabledTextColor : g_mutedTextColor);
        DrawTextW(item.hDC, subtitle, -1, &subtitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        const wchar_t* label = text;
        switch (controlId)
        {
        case IDC_BROWSE_PROTECTED_BUTTON:
            label = Text(L"浏览列表", L"Browse list");
            break;
        case IDC_ADD_GAME_BUTTON:
            label = Text(L"添加程序", L"Add program");
            break;
        case IDC_ADD_FILE_BUTTON:
            label = Text(L"浏览程序", L"Browse program");
            break;
        case IDC_DELETE_GAME_BUTTON:
            label = Text(L"删除程序", L"Delete program");
            break;
        case IDC_SWITCH_EN_BUTTON:
            label = Text(L"切换为英文", L"English");
            break;
        case IDC_SWITCH_CN_BUTTON:
            label = Text(L"切换为中文", L"Chinese");
            break;
        }
        DrawTextW(item.hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(item.hDC, oldFont);
}

void ThemeManager::MeasureMenuItem(MEASUREITEMSTRUCT& item)
{
    const auto* text = reinterpret_cast<const std::wstring*>(item.itemData);
    HDC screen = GetDC(nullptr);
    HGDIOBJ oldFont = SelectObject(screen, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));
    SIZE size{};
    if (text)
    {
        GetTextExtentPoint32W(screen, text->c_str(), static_cast<int>(text->size()), &size);
    }
    SelectObject(screen, oldFont);
    ReleaseDC(nullptr, screen);

    item.itemWidth = std::max<UINT>(Scale(64), static_cast<UINT>(size.cx + Scale(34)));
    item.itemHeight = std::max<UINT>(Scale(28), static_cast<UINT>(size.cy + Scale(12)));
}

void ThemeManager::DrawMenuItem(const DRAWITEMSTRUCT& item)
{
    const auto* text = reinterpret_cast<const std::wstring*>(item.itemData);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const bool checked = (item.itemState & ODS_CHECKED) != 0;

    COLORREF fill = selected ? g_buttonHotColor : g_windowColor;
    if ((item.itemState & ODS_NOACCEL) == 0 && item.rcItem.top > 0)
    {
        fill = selected ? g_buttonHotColor : g_surfaceColor;
    }

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);

    RECT textRect = item.rcItem;
    textRect.left += Scale(24);
    textRect.right -= Scale(10);

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? g_disabledTextColor : g_textColor);
    HGDIOBJ oldFont = SelectObject(item.hDC, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));
    if (text)
    {
        DrawTextW(item.hDC, text->c_str(), -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }

    if (checked)
    {
        RECT checkRect = item.rcItem;
        checkRect.left += Scale(7);
        checkRect.right = checkRect.left + Scale(12);
        SetTextColor(item.hDC, g_accentColor);
        DrawTextW(item.hDC, L"\u2713", -1, &checkRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }

    SelectObject(item.hDC, oldFont);
}

void ThemeManager::DrawListBoxItem(const DRAWITEMSTRUCT& item, const std::vector<std::wstring>& items)
{
    if (item.itemID == static_cast<UINT>(-1))
    {
        return;
    }

    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    HBRUSH brush = CreateSolidBrush(selected ? g_selectedColor : g_surfaceColor);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);

    if (item.itemID < items.size())
    {
        RECT textRect = item.rcItem;
        textRect.left += Scale(12);
        textRect.right -= Scale(12);
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, g_textColor);
        HGDIOBJ oldFont = SelectObject(item.hDC, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(item.hDC, items[item.itemID].c_str(), -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(item.hDC, oldFont);
    }

    HPEN pen = CreatePen(PS_SOLID, 1, g_borderColor);
    HGDIOBJ oldPen = SelectObject(item.hDC, pen);
    MoveToEx(item.hDC, item.rcItem.left + Scale(10), item.rcItem.bottom - 1, nullptr);
    LineTo(item.hDC, item.rcItem.right - Scale(10), item.rcItem.bottom - 1);
    SelectObject(item.hDC, oldPen);
    DeleteObject(pen);

    if (item.itemState & ODS_FOCUS)
    {
        RECT focusRect = item.rcItem;
        InflateRect(&focusRect, -2, -2);
        DrawFocusRect(item.hDC, &focusRect);
    }
}

HFONT ThemeManager::UiFont()
{
    return g_uiFont;
}

HFONT ThemeManager::TitleFont()
{
    return g_titleFont;
}

HBRUSH ThemeManager::WindowBrush()
{
    return g_windowBrush;
}

HBRUSH ThemeManager::SurfaceBrush()
{
    return g_surfaceBrush;
}

COLORREF ThemeManager::WindowColor()
{
    return g_windowColor;
}

COLORREF ThemeManager::SurfaceColor()
{
    return g_surfaceColor;
}

COLORREF ThemeManager::TextColor()
{
    return g_textColor;
}

COLORREF ThemeManager::MutedTextColor()
{
    return g_mutedTextColor;
}

COLORREF ThemeManager::BorderColor()
{
    return g_borderColor;
}

bool ThemeManager::IsDark()
{
    return g_dark;
}
}
