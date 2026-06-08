#include "ThemeManager.h"

#include "Localization.h"
#include "Platform/GdiUtils.h"

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
constexpr wchar_t kOwnerDrawGroupBoxName[] = L"FFKeyLock.OwnerDrawGroupBox";

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
COLORREF g_menuBarColor = RGB(45, 45, 48);
COLORREF g_menuBarHoverColor = RGB(55, 55, 58);
COLORREF g_menuBackgroundColor = RGB(45, 45, 48);
COLORREF g_menuHoverColor = RGB(55, 55, 58);
COLORREF g_menuPressedColor = RGB(63, 63, 70);
COLORREF g_menuBorderColor = RGB(69, 69, 69);
COLORREF g_menuSeparatorColor = RGB(58, 58, 58);
COLORREF g_menuIconColor = RGB(218, 218, 218);
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
        g_menuBarColor = RGB(45, 45, 48);
        g_menuBarHoverColor = RGB(55, 55, 58);
        g_menuBackgroundColor = RGB(45, 45, 48);
        g_menuHoverColor = RGB(55, 55, 58);
        g_menuPressedColor = RGB(63, 63, 70);
        g_menuBorderColor = RGB(69, 69, 69);
        g_menuSeparatorColor = RGB(58, 58, 58);
        g_menuIconColor = RGB(218, 218, 218);
    }
    else
    {
        g_windowColor = RGB(245, 245, 245);
        g_surfaceColor = RGB(255, 255, 255);
        g_cardColor = RGB(255, 255, 255);
        g_buttonColor = RGB(248, 248, 248);
        g_buttonHotColor = RGB(229, 241, 251);
        g_buttonPressedColor = RGB(204, 228, 247);
        g_textColor = RGB(30, 30, 30);
        g_mutedTextColor = RGB(93, 93, 93);
        g_disabledTextColor = RGB(150, 150, 150);
        g_borderColor = RGB(204, 204, 204);
        g_accentColor = RGB(0, 122, 204);
        g_selectedColor = RGB(204, 228, 247);
        g_menuBarColor = RGB(243, 243, 243);
        g_menuBarHoverColor = RGB(229, 241, 251);
        g_menuBackgroundColor = RGB(255, 255, 255);
        g_menuHoverColor = RGB(229, 241, 251);
        g_menuPressedColor = RGB(204, 228, 247);
        g_menuBorderColor = RGB(204, 204, 204);
        g_menuSeparatorColor = RGB(229, 229, 229);
        g_menuIconColor = RGB(80, 80, 80);
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
        RemovePropW(button, kOwnerDrawGroupBoxName);
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
        if ((style & BS_GROUPBOX) == BS_GROUPBOX)
        {
            SetPropW(hwnd, kOwnerDrawGroupBoxName, reinterpret_cast<HANDLE>(1));
            SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
            if (!GetPropW(hwnd, kButtonSubclassName))
            {
                SetPropW(hwnd, kButtonSubclassName, reinterpret_cast<HANDLE>(1));
                SetWindowSubclass(hwnd, ButtonSubclassProc, 1, 0);
            }
            SetWindowTheme(hwnd, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
            InvalidateRect(hwnd, nullptr, TRUE);
            return;
        }
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
    return g_surfaceBrush ? g_surfaceBrush : reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
}

void ThemeManager::DrawButton(const DRAWITEMSTRUCT& item)
{
    GdiUtils::BufferedPaint buffer(item.hDC, item.rcItem);
    HDC hdc = buffer.Dc();
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = GetPropW(item.hwndItem, L"FFKeyLock.ButtonHot") != nullptr;

    if (GetPropW(item.hwndItem, kOwnerDrawGroupBoxName) != nullptr)
    {
        RECT rect = item.rcItem;
        HBRUSH background = CreateSolidBrush(g_windowColor);
        FillRect(hdc, &rect, background);
        DeleteObject(background);

        wchar_t text[256]{};
        GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_textColor);
        GdiUtils::SelectObjectScope fontScope(hdc, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));

        RECT textRect = rect;
        textRect.left += Scale(10);
        textRect.right -= Scale(10);
        textRect.bottom = textRect.top + Scale(22);
        SIZE textSize{};
        GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &textSize);

        RECT borderRect = rect;
        borderRect.top += Scale(9);
        HPEN pen = CreatePen(PS_SOLID, 1, g_borderColor);
        GdiUtils::SelectObjectScope penScope(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, borderRect.left, borderRect.top, borderRect.right, borderRect.bottom, Scale(8), Scale(8));
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);

        RECT textBackground = textRect;
        textBackground.right = textBackground.left + textSize.cx + Scale(8);
        FillRect(hdc, &textBackground, background = CreateSolidBrush(g_windowColor));
        DeleteObject(background);
        DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

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
    HBRUSH background = CreateSolidBrush(g_windowColor);
    FillRect(hdc, &rect, background);
    DeleteObject(background);
    DrawRoundRect(hdc, rect, fill, hot && !disabled ? g_accentColor : g_borderColor, Scale(7));

    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, disabled ? g_disabledTextColor : g_textColor);
    HGDIOBJ oldFont = SelectObject(hdc, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));
    if (pressed)
    {
        OffsetRect(&rect, 1, 1);
    }
    DrawTextW(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
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

    GdiUtils::BufferedPaint buffer(item.hDC, item.rcItem);
    HDC hdc = buffer.Dc();
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill = selected ? g_selectedColor : g_surfaceColor;
    const COLORREF text = selected ? (g_dark ? RGB(255, 255, 255) : g_textColor) : g_textColor;
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(hdc, &item.rcItem, brush);
    DeleteObject(brush);

    if (item.itemID < items.size())
    {
        RECT textRect = item.rcItem;
        textRect.left += Scale(8);
        textRect.right -= Scale(8);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text);
        HGDIOBJ oldFont = SelectObject(hdc, g_uiFont ? g_uiFont : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(hdc, items[item.itemID].c_str(), -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(hdc, oldFont);
    }

    if (item.itemState & ODS_FOCUS)
    {
        RECT focusRect = item.rcItem;
        InflateRect(&focusRect, -1, -1);
        DrawFocusRect(hdc, &focusRect);
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

COLORREF ThemeManager::DisabledTextColor()
{
    return g_disabledTextColor;
}

COLORREF ThemeManager::BorderColor()
{
    return g_borderColor;
}

COLORREF ThemeManager::AccentColor()
{
    return g_accentColor;
}

COLORREF ThemeManager::ButtonColor()
{
    return g_buttonColor;
}

COLORREF ThemeManager::ButtonHotColor()
{
    return g_buttonHotColor;
}

COLORREF ThemeManager::ButtonPressedColor()
{
    return g_buttonPressedColor;
}

COLORREF ThemeManager::MenuBarColor()
{
    return g_menuBarColor;
}

COLORREF ThemeManager::MenuBarHoverColor()
{
    return g_menuBarHoverColor;
}

COLORREF ThemeManager::MenuBackgroundColor()
{
    return g_menuBackgroundColor;
}

COLORREF ThemeManager::MenuHoverColor()
{
    return g_menuHoverColor;
}

COLORREF ThemeManager::MenuPressedColor()
{
    return g_menuPressedColor;
}

COLORREF ThemeManager::MenuBorderColor()
{
    return g_menuBorderColor;
}

COLORREF ThemeManager::MenuSeparatorColor()
{
    return g_menuSeparatorColor;
}

COLORREF ThemeManager::MenuIconColor()
{
    return g_menuIconColor;
}

bool ThemeManager::IsDark()
{
    return g_dark;
}
}
