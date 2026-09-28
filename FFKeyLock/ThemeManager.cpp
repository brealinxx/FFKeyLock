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
bool g_highContrast = false;

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
    HIGHCONTRASTW contrast{sizeof(contrast)};
    g_highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON);
    if (g_dark)
    {
        g_windowColor = RGB(24, 26, 30);
        g_surfaceColor = RGB(30, 33, 38);
        g_cardColor = RGB(27, 29, 31);
        g_buttonColor = RGB(38, 42, 48);
        g_buttonHotColor = RGB(43, 47, 49);
        g_buttonPressedColor = RGB(30, 33, 35);
        g_textColor = RGB(242, 244, 244);
        g_mutedTextColor = RGB(166, 171, 173);
        g_disabledTextColor = RGB(105, 110, 112);
        g_borderColor = RGB(66, 71, 80);
        g_accentColor = RGB(114, 174, 250);
        g_selectedColor = RGB(38, 58, 82);
        g_menuBarColor = RGB(22, 24, 25);
        g_menuBarHoverColor = RGB(36, 39, 41);
        g_menuBackgroundColor = RGB(27, 29, 31);
        g_menuHoverColor = RGB(40, 44, 46);
        g_menuPressedColor = RGB(32, 35, 37);
        g_menuBorderColor = RGB(52, 56, 58);
        g_menuSeparatorColor = RGB(45, 48, 50);
        g_menuIconColor = RGB(229, 231, 231);
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

    if (g_highContrast)
    {
        g_windowColor = g_surfaceColor = g_cardColor = GetSysColor(COLOR_WINDOW);
        g_textColor = g_mutedTextColor = GetSysColor(COLOR_WINDOWTEXT);
        g_borderColor = GetSysColor(COLOR_WINDOWTEXT);
        g_disabledTextColor = GetSysColor(COLOR_GRAYTEXT);
        g_accentColor = g_selectedColor = GetSysColor(COLOR_HIGHLIGHT);
        g_menuBarColor = g_menuBackgroundColor = GetSysColor(COLOR_MENU);
        g_menuBarHoverColor = g_menuHoverColor = g_menuPressedColor = GetSysColor(COLOR_HIGHLIGHT);
        g_menuBorderColor = g_menuSeparatorColor = GetSysColor(COLOR_WINDOWTEXT);
        g_menuIconColor = GetSysColor(COLOR_MENUTEXT);
        g_buttonColor = g_buttonHotColor = g_buttonPressedColor = GetSysColor(COLOR_BTNFACE);
    }
    g_uiFont = CreateThemeFont(10, FW_NORMAL);
    g_titleFont = CreateThemeFont(13, FW_SEMIBOLD);
    g_windowBrush = CreateSolidBrush(g_windowColor);
    g_surfaceBrush = CreateSolidBrush(g_surfaceColor);
    g_cardBrush = CreateSolidBrush(g_cardColor);
}

void SetBoolWindowAttribute(HWND hwnd, DWORD attribute, BOOL value)
{
    DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
}

void SetColorWindowAttribute(HWND hwnd, DWORD attribute, COLORREF value)
{
    DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
}


}

COLORREF ThemeManager::SelectionColor() { return g_selectedColor; }

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

void ThemeManager::ApplyDarkTitleBar(HWND hwnd)
{
    if (!hwnd || (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD))
    {
        return;
    }

    BOOL dark = g_dark && !g_highContrast ? TRUE : FALSE;
    SetBoolWindowAttribute(hwnd, 20, dark);
    SetBoolWindowAttribute(hwnd, 19, dark);

    const COLORREF captionColor = g_windowColor;
    const COLORREF textColor = g_textColor;
    SetColorWindowAttribute(hwnd, 35, captionColor);
    SetColorWindowAttribute(hwnd, 36, textColor);
    SetColorWindowAttribute(hwnd, 34, g_borderColor);

    SetWindowPos(
        hwnd,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
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

COLORREF ThemeManager::PanelBorderColor()
{
    return g_highContrast ? g_borderColor : g_dark ? RGB(49, 54, 62) : RGB(218, 221, 225);
}

COLORREF ThemeManager::ScrollThumbColor(bool hot, bool pressed)
{
    if (g_highContrast) return pressed || hot ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOWTEXT);
    if (g_dark) return pressed ? RGB(157, 164, 174) : hot ? RGB(119, 127, 138) : RGB(82, 90, 102);
    return pressed ? RGB(96, 104, 116) : hot ? RGB(128, 136, 148) : RGB(174, 181, 191);
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

bool ThemeManager::HighContrast() { return g_highContrast; }
}
