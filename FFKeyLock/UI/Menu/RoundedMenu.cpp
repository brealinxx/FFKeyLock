#include "RoundedMenu.h"

#include "../../Platform/DpiUtils.h"
#include "../../Platform/GdiUtils.h"
#include "../../ThemeManager.h"

#include <dwmapi.h>
#include <strsafe.h>
#include <windowsx.h>

#include <algorithm>
#include <memory>

#pragma comment(lib, "Dwmapi.lib")

namespace FFKeyLock
{
namespace
{
constexpr wchar_t kRoundedMenuClass[] = L"FFKeyLockRoundedMenu";

int ScaleForDpi(int value, UINT dpi)
{
    return DpiUtils::Scale(value, dpi);
}

HFONT CreateMenuFont(UINT dpi)
{
    LOGFONTW font{};
    font.lfHeight = -ScaleForDpi(12, dpi);
    font.lfWeight = FW_NORMAL;
    font.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(font.lfFaceName, std::size(font.lfFaceName), L"Segoe UI");
    return CreateFontIndirectW(&font);
}

struct MenuState
{
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    HFONT font = nullptr;
    std::vector<RoundedMenuItem> items;
    std::vector<RECT> itemRects;
    int hotIndex = -1;
    int pressedIndex = -1;
    int openSubmenuIndex = -1;
    UINT result = 0;
    bool done = false;
    int width = 0;
    int height = 0;

    int Radius() const { return ScaleForDpi(8, dpi); }
    int PaddingY() const { return ScaleForDpi(4, dpi); }
    int ItemHeight() const { return ScaleForDpi(28, dpi); }
    int SeparatorHeight() const { return ScaleForDpi(7, dpi); }
    int IconWidth() const { return ScaleForDpi(22, dpi); }
    int TextPadding() const { return ScaleForDpi(6, dpi); }
    int RightPadding() const { return ScaleForDpi(8, dpi); }
};

std::vector<MenuState*> g_activeMenus;

bool IsInteractive(const RoundedMenuItem& item)
{
    return !item.separator && !item.title && item.enabled;
}

bool IsRoundedMenuWindow(HWND hwnd)
{
    if (!hwnd)
    {
        return false;
    }

    wchar_t className[64]{};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    return wcscmp(className, kRoundedMenuClass) == 0;
}

void CloseMenu(MenuState* state, UINT result)
{
    if (!state || state->done)
    {
        return;
    }
    state->result = result;
    state->done = true;
    if (state->hwnd)
    {
        DestroyWindow(state->hwnd);
    }
}

void CloseAllMenus(UINT result)
{
    auto menus = g_activeMenus;
    for (MenuState* menu : menus)
    {
        CloseMenu(menu, result);
    }
}

void RegisterActiveMenu(MenuState* state)
{
    g_activeMenus.push_back(state);
}

void UnregisterActiveMenu(MenuState* state)
{
    g_activeMenus.erase(std::remove(g_activeMenus.begin(), g_activeMenus.end(), state), g_activeMenus.end());
}

void MeasureMenu(MenuState& state)
{
    HDC hdc = GetDC(nullptr);
    HGDIOBJ oldFont = SelectObject(hdc, state.font ? state.font : GetStockObject(DEFAULT_GUI_FONT));
    int textWidth = 0;
    int shortcutWidth = 0;
    bool hasShortcut = false;
    bool hasSubmenu = false;
    state.height = state.PaddingY() * 2;
    state.itemRects.clear();

    for (const auto& item : state.items)
    {
        SIZE textSize{};
        SIZE shortcutSize{};
        if (!item.separator)
        {
            GetTextExtentPoint32W(hdc, item.text.c_str(), static_cast<int>(item.text.size()), &textSize);
            GetTextExtentPoint32W(hdc, item.shortcut.c_str(), static_cast<int>(item.shortcut.size()), &shortcutSize);
            textWidth = std::max(textWidth, static_cast<int>(textSize.cx));
            shortcutWidth = std::max(shortcutWidth, static_cast<int>(shortcutSize.cx));
            hasShortcut = hasShortcut || !item.shortcut.empty();
            hasSubmenu = hasSubmenu || !item.submenu.empty();
        }
        state.height += item.separator ? state.SeparatorHeight() : state.ItemHeight();
    }

    SelectObject(hdc, oldFont);
    ReleaseDC(nullptr, hdc);

    const int arrowWidth = hasSubmenu ? ScaleForDpi(14, state.dpi) : 0;
    const int shortcutGap = hasShortcut ? ScaleForDpi(14, state.dpi) : 0;
    state.width = std::max(
        ScaleForDpi(118, state.dpi),
        state.IconWidth() + state.TextPadding() + textWidth + shortcutGap + shortcutWidth + arrowWidth + state.RightPadding());

    int y = state.PaddingY();
    for (const auto& item : state.items)
    {
        const int h = item.separator ? state.SeparatorHeight() : state.ItemHeight();
        state.itemRects.push_back({ 0, y, state.width, y + h });
        y += h;
    }
}

void ApplyRoundedRegion(HWND hwnd, const MenuState& state)
{
    HRGN region = CreateRoundRectRgn(0, 0, state.width + 1, state.height + 1, state.Radius() * 2, state.Radius() * 2);
    SetWindowRgn(hwnd, region, TRUE);

    const DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
}

void DrawCheck(HDC hdc, RECT rect, UINT dpi)
{
    HPEN pen = CreatePen(PS_SOLID, ScaleForDpi(2, dpi), ThemeManager::AccentColor());
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    POINT points[] = {
        { rect.left + ScaleForDpi(6, dpi), rect.top + ScaleForDpi(15, dpi) },
        { rect.left + ScaleForDpi(10, dpi), rect.top + ScaleForDpi(19, dpi) },
        { rect.left + ScaleForDpi(18, dpi), rect.top + ScaleForDpi(10, dpi) },
    };
    Polyline(hdc, points, 3);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DrawArrow(HDC hdc, RECT rect, UINT dpi, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, ScaleForDpi(2, dpi), color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    const int cx = rect.right - ScaleForDpi(10, dpi);
    const int cy = rect.top + (rect.bottom - rect.top) / 2;
    POINT points[] = {
        { cx - ScaleForDpi(3, dpi), cy - ScaleForDpi(4, dpi) },
        { cx + ScaleForDpi(1, dpi), cy },
        { cx - ScaleForDpi(3, dpi), cy + ScaleForDpi(4, dpi) },
    };
    Polyline(hdc, points, 3);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void PaintMenu(MenuState& state, HDC target)
{
    RECT client{ 0, 0, state.width, state.height };
    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, state.width, state.height);
    HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);

    HBRUSH background = CreateSolidBrush(ThemeManager::MenuBackgroundColor());
    FillRect(hdc, &client, background);
    DeleteObject(background);

    HGDIOBJ oldFont = SelectObject(hdc, state.font ? state.font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);

    for (size_t index = 0; index < state.items.size(); ++index)
    {
        const auto& item = state.items[index];
        RECT rect = state.itemRects[index];

        if (item.separator)
        {
            RECT line = rect;
            line.left += ScaleForDpi(8, state.dpi);
            line.right -= ScaleForDpi(8, state.dpi);
            line.top += (line.bottom - line.top) / 2;
            line.bottom = line.top + 1;
            HBRUSH separator = CreateSolidBrush(ThemeManager::MenuSeparatorColor());
            FillRect(hdc, &line, separator);
            DeleteObject(separator);
            continue;
        }

        const bool hot = static_cast<int>(index) == state.hotIndex && IsInteractive(item);
        const bool pressed = static_cast<int>(index) == state.pressedIndex && IsInteractive(item);
        if (hot || pressed)
        {
            RECT hover = rect;
            InflateRect(&hover, -ScaleForDpi(4, state.dpi), -ScaleForDpi(2, state.dpi));
            const COLORREF fill = pressed ? ThemeManager::MenuPressedColor() : ThemeManager::MenuHoverColor();
            HBRUSH hoverBrush = CreateSolidBrush(fill);
            HPEN hoverPen = CreatePen(PS_SOLID, 1, fill);
            HGDIOBJ oldBrush = SelectObject(hdc, hoverBrush);
            HGDIOBJ oldPen = SelectObject(hdc, hoverPen);
            RoundRect(hdc, hover.left, hover.top, hover.right, hover.bottom, ScaleForDpi(6, state.dpi), ScaleForDpi(6, state.dpi));
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(hoverBrush);
            DeleteObject(hoverPen);
        }

        RECT iconRect = rect;
        iconRect.left += ScaleForDpi(3, state.dpi);
        iconRect.right = iconRect.left + state.IconWidth();
        if (item.checked)
        {
            DrawCheck(hdc, iconRect, state.dpi);
        }

        const COLORREF primary = item.enabled ? ThemeManager::TextColor() : ThemeManager::DisabledTextColor();
        const COLORREF secondary = item.enabled ? ThemeManager::MutedTextColor() : ThemeManager::DisabledTextColor();
        RECT textRect = rect;
        textRect.left += state.IconWidth() + state.TextPadding();
        textRect.right -= state.RightPadding();
        if (!item.submenu.empty())
        {
            textRect.right -= ScaleForDpi(18, state.dpi);
        }
        if (!item.shortcut.empty())
        {
            textRect.right -= ScaleForDpi(52, state.dpi);
        }
        SetTextColor(hdc, item.title ? ThemeManager::MutedTextColor() : primary);
        DrawTextW(hdc, item.text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (!item.shortcut.empty())
        {
            RECT shortcutRect = rect;
            shortcutRect.left = state.width - ScaleForDpi(62, state.dpi);
            shortcutRect.right = state.width - state.RightPadding() - (item.submenu.empty() ? 0 : ScaleForDpi(14, state.dpi));
            SetTextColor(hdc, secondary);
            DrawTextW(hdc, item.shortcut.c_str(), -1, &shortcutRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        if (!item.submenu.empty())
        {
            DrawArrow(hdc, rect, state.dpi, item.enabled ? ThemeManager::MenuIconColor() : ThemeManager::DisabledTextColor());
        }
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, ThemeManager::MenuBorderColor());
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, 0, 0, state.width, state.height, state.Radius() * 2, state.Radius() * 2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    BitBlt(target, 0, 0, state.width, state.height, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(hdc);
}

int HitTest(MenuState& state, POINT point)
{
    for (size_t index = 0; index < state.itemRects.size(); ++index)
    {
        if (PtInRect(&state.itemRects[index], point))
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void OpenSubmenu(MenuState* state, int index);

void ActivateItem(MenuState* state, int index)
{
    if (!state || index < 0 || index >= static_cast<int>(state->items.size()))
    {
        return;
    }

    const auto& item = state->items[index];
    if (!IsInteractive(item))
    {
        return;
    }

    if (!item.submenu.empty())
    {
        OpenSubmenu(state, index);
        return;
    }

    CloseAllMenus(item.id);
}

void OpenSubmenu(MenuState* state, int index)
{
    if (!state || index < 0 || index >= static_cast<int>(state->items.size()) || state->openSubmenuIndex == index)
    {
        return;
    }

    const auto& item = state->items[index];
    if (!IsInteractive(item) || item.submenu.empty())
    {
        state->openSubmenuIndex = -1;
        return;
    }

    state->openSubmenuIndex = index;
    RECT rect = state->itemRects[index];
    POINT anchor{ rect.right - ScaleForDpi(3, state->dpi), rect.top - ScaleForDpi(4, state->dpi) };
    ClientToScreen(state->hwnd, &anchor);
    const UINT command = RoundedMenu::Show(state->owner, anchor, item.submenu);
    state->openSubmenuIndex = -1;
    if (command)
    {
        CloseAllMenus(command);
    }
}

void MoveHot(MenuState* state, int delta)
{
    if (!state || state->items.empty())
    {
        return;
    }

    int index = state->hotIndex;
    for (size_t i = 0; i < state->items.size(); ++i)
    {
        index += delta;
        if (index < 0)
        {
            index = static_cast<int>(state->items.size()) - 1;
        }
        else if (index >= static_cast<int>(state->items.size()))
        {
            index = 0;
        }

        if (IsInteractive(state->items[index]))
        {
            state->hotIndex = index;
            InvalidateRect(state->hwnd, nullptr, FALSE);
            return;
        }
    }
}

LRESULT CALLBACK RoundedMenuProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<MenuState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_NCCREATE:
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(hwnd, &paint);
        if (state)
        {
            PaintMenu(*state, hdc);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (state)
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int hit = HitTest(*state, point);
            const int nextHot = hit >= 0 && IsInteractive(state->items[hit]) ? hit : -1;
            if (state->hotIndex != nextHot)
            {
                state->hotIndex = nextHot;
                InvalidateRect(hwnd, nullptr, FALSE);
                if (nextHot >= 0 && !state->items[nextHot].submenu.empty())
                {
                    OpenSubmenu(state, nextHot);
                }
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (state)
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int hit = HitTest(*state, point);
            state->pressedIndex = hit >= 0 && IsInteractive(state->items[hit]) ? hit : -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        SetCapture(hwnd);
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        if (state)
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int hit = HitTest(*state, point);
            state->pressedIndex = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            if (hit >= 0)
            {
                ActivateItem(state, hit);
            }
            else
            {
                CloseAllMenus(0);
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (state)
        {
            if (wParam == VK_ESCAPE)
            {
                CloseAllMenus(0);
                return 0;
            }
            if (wParam == VK_DOWN)
            {
                MoveHot(state, 1);
                return 0;
            }
            if (wParam == VK_UP)
            {
                MoveHot(state, -1);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_RIGHT)
            {
                ActivateItem(state, state->hotIndex);
                return 0;
            }
        }
        break;

    case WM_KILLFOCUS:
        if (state && !IsRoundedMenuWindow(reinterpret_cast<HWND>(wParam)))
        {
            CloseAllMenus(0);
        }
        return 0;

    case WM_CANCELMODE:
        if (state)
        {
            CloseAllMenus(0);
        }
        return 0;

    case WM_DESTROY:
        if (state)
        {
            state->hwnd = nullptr;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnsureClass(HINSTANCE instance)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_DROPSHADOW;
    wc.lpfnWndProc = RoundedMenuProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kRoundedMenuClass;
    RegisterClassExW(&wc);
    registered = true;
}

POINT FitToWorkArea(POINT anchor, const MenuState& state, bool alignBottom)
{
    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{ sizeof(info) };
    GetMonitorInfoW(monitor, &info);

    POINT point = anchor;
    if (alignBottom)
    {
        point.y -= state.height;
    }

    if (point.x + state.width > info.rcWork.right)
    {
        point.x = info.rcWork.right - state.width - ScaleForDpi(4, state.dpi);
    }
    if (point.y + state.height > info.rcWork.bottom)
    {
        point.y = info.rcWork.bottom - state.height - ScaleForDpi(4, state.dpi);
    }
    point.x = std::max(point.x, info.rcWork.left + ScaleForDpi(4, state.dpi));
    point.y = std::max(point.y, info.rcWork.top + ScaleForDpi(4, state.dpi));
    return point;
}
}

UINT RoundedMenu::Show(HWND owner, POINT anchor, const std::vector<RoundedMenuItem>& items, bool alignBottom)
{
    if (!owner || items.empty())
    {
        return 0;
    }

    auto state = std::make_unique<MenuState>();
    state->owner = owner;
    state->dpi = GetDpiForWindow(owner);
    state->font = CreateMenuFont(state->dpi);
    state->items = items;
    MeasureMenu(*state);

    for (size_t i = 0; i < state->items.size(); ++i)
    {
        if (IsInteractive(state->items[i]))
        {
            state->hotIndex = static_cast<int>(i);
            break;
        }
    }

    EnsureClass(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE)));
    POINT point = FitToWorkArea(anchor, *state, alignBottom);
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kRoundedMenuClass,
        L"",
        WS_POPUP,
        point.x,
        point.y,
        state->width,
        state->height,
        owner,
        nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE)),
        state.get());

    if (!hwnd)
    {
        DeleteObject(state->font);
        return 0;
    }

    state->hwnd = hwnd;
    RegisterActiveMenu(state.get());
    ApplyRoundedRegion(hwnd, *state);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg{};
    while (!state->done && GetMessageW(&msg, nullptr, 0, 0))
    {
        const bool pointerDismiss =
            msg.message == WM_LBUTTONDOWN ||
            msg.message == WM_RBUTTONDOWN ||
            msg.message == WM_MBUTTONDOWN ||
            msg.message == WM_NCLBUTTONDOWN ||
            msg.message == WM_NCRBUTTONDOWN ||
            msg.message == WM_NCMBUTTONDOWN ||
            msg.message == WM_MOUSEWHEEL ||
            msg.message == WM_MOUSEHWHEEL;
        if (pointerDismiss)
        {
            if (state->hwnd && msg.hwnd != state->hwnd && !IsChild(state->hwnd, msg.hwnd) && !IsRoundedMenuWindow(msg.hwnd))
            {
                CloseAllMenus(0);
            }
        }
        if (msg.message == WM_ACTIVATEAPP && !msg.wParam)
        {
            CloseAllMenus(0);
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    const UINT result = state->result;
    UnregisterActiveMenu(state.get());
    if (state->font)
    {
        DeleteObject(state->font);
    }
    return result;
}

void RoundedMenu::ShowAndDispatch(HWND owner, POINT anchor, const std::vector<RoundedMenuItem>& items, bool alignBottom)
{
    const UINT command = Show(owner, anchor, items, alignBottom);
    if (command)
    {
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
}
}
