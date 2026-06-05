#include "ThemeManager.h"

#include "Localization.h"

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
        g_windowColor = RGB(32, 32, 32);
        g_surfaceColor = RGB(43, 43, 43);
        g_cardColor = RGB(43, 43, 43);
        g_buttonColor = RGB(58, 58, 58);
        g_buttonHotColor = RGB(72, 72, 72);
        g_buttonPressedColor = RGB(85, 85, 85);
        g_textColor = RGB(230, 230, 230);
        g_mutedTextColor = RGB(184, 184, 184);
        g_disabledTextColor = RGB(122, 122, 122);
        g_borderColor = RGB(82, 82, 82);
        g_accentColor = RGB(86, 156, 214);
        g_selectedColor = RGB(52, 80, 112);
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
    DrawRoundRect(item.hDC, rect, fill, hot && !disabled ? g_accentColor : g_borderColor, Scale(7));

    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? g_disabledTextColor : g_textColor);
    HGDIOBJ oldFont = SelectObject(item.hDC, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));
    if (pressed)
    {
        OffsetRect(&rect, 1, 1);
    }
    DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
