#pragma once

#include "AppState.h"
#include "framework.h"

#include <string>
#include <vector>

namespace FFKeyLock
{
class ThemeManager
{
public:
    static void Initialize(UINT dpi);
    static void Shutdown();
    static void SetDpi(UINT dpi);
    static int Scale(int value);
    static void ApplyTheme(HWND root);
    static void ApplyDarkTitleBar(HWND hwnd);
    static void HandleSettingChange(HWND root);
    static HBRUSH HandleCtlColor(HWND hwnd, HDC hdc, HWND control);
    static void DrawButton(const DRAWITEMSTRUCT& item);
    static void MeasureMenuItem(MEASUREITEMSTRUCT& item);
    static void DrawMenuItem(const DRAWITEMSTRUCT& item);
    static void DrawListBoxItem(const DRAWITEMSTRUCT& item, const std::vector<std::wstring>& items);
    static HFONT UiFont();
    static HFONT TitleFont();
    static HBRUSH WindowBrush();
    static HBRUSH SurfaceBrush();
    static COLORREF WindowColor();
    static COLORREF SurfaceColor();
    static COLORREF TextColor();
    static COLORREF MutedTextColor();
    static COLORREF DisabledTextColor();
    static COLORREF BorderColor();
    static COLORREF AccentColor();
    static COLORREF ButtonColor();
    static COLORREF ButtonHotColor();
    static COLORREF ButtonPressedColor();
    static COLORREF MenuBarColor();
    static COLORREF MenuBarHoverColor();
    static COLORREF MenuBackgroundColor();
    static COLORREF MenuHoverColor();
    static COLORREF MenuPressedColor();
    static COLORREF MenuBorderColor();
    static COLORREF MenuSeparatorColor();
    static COLORREF MenuIconColor();
    static bool IsDark();
};
}
