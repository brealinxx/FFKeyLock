#include "RoundedButton.h"

#include "../../Platform/DpiUtils.h"
#include "../../Platform/GdiUtils.h"
#include "../../ThemeManager.h"

#include <strsafe.h>
#include <windowsx.h>

namespace FFKeyLock
{
namespace RoundedButton
{
namespace
{
constexpr wchar_t kHotProp[] = L"FFKeyLockRoundedButtonHot";
constexpr wchar_t kPressedProp[] = L"FFKeyLockRoundedButtonPressed";

bool IsHot(HWND hwnd)
{
    return GetPropW(hwnd, kHotProp) != nullptr;
}

bool IsPressed(HWND hwnd)
{
    return GetPropW(hwnd, kPressedProp) != nullptr;
}

void SetHot(HWND hwnd, bool hot)
{
    if (hot)
    {
        SetPropW(hwnd, kHotProp, reinterpret_cast<HANDLE>(1));
    }
    else
    {
        RemovePropW(hwnd, kHotProp);
    }
}

void SetPressed(HWND hwnd, bool pressed)
{
    if (pressed)
    {
        SetPropW(hwnd, kPressedProp, reinterpret_cast<HANDLE>(1));
    }
    else
    {
        RemovePropW(hwnd, kPressedProp);
    }
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const UINT dpi = DpiUtils::GetWindowDpi(hwnd);
    const bool enabled = IsWindowEnabled(hwnd) != FALSE;

    COLORREF fill = ThemeManager::ButtonColor();
    if (!enabled)
    {
        fill = ThemeManager::SurfaceColor();
    }
    else if (IsPressed(hwnd))
    {
        fill = ThemeManager::ButtonPressedColor();
    }
    else if (IsHot(hwnd))
    {
        fill = ThemeManager::ButtonHotColor();
    }

    GdiUtils::FillRoundRect(hdc, rect, fill, ThemeManager::BorderColor(), DpiUtils::Scale(7, dpi));

    wchar_t text[256]{};
    GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, enabled ? ThemeManager::TextColor() : ThemeManager::DisabledTextColor());
    GdiUtils::SelectObjectScope fontScope(hdc, ThemeManager::UiFont() ? ThemeManager::UiFont() : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        if (!IsHot(hwnd))
        {
            SetHot(hwnd, true);
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSELEAVE:
        SetHot(hwnd, false);
        SetPressed(hwnd, false);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        SetPressed(hwnd, true);
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP:
    {
        const bool wasPressed = IsPressed(hwnd);
        SetPressed(hwnd, false);
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        if (wasPressed)
        {
            RECT rect{};
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            GetClientRect(hwnd, &rect);
            if (PtInRect(&rect, point))
            {
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
            }
        }
        return 0;
    }

    case WM_ENABLE:
    case WM_SETTEXT:
        InvalidateRect(hwnd, nullptr, TRUE);
        break;

    case WM_NCDESTROY:
        RemovePropW(hwnd, kHotProp);
        RemovePropW(hwnd, kPressedProp);
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

void Register(HINSTANCE instance)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = ClassName;
    RegisterClassExW(&wc);
    registered = true;
}

HWND Create(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height)
{
    Register(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)));
    return CreateWindowExW(
        0,
        ClassName,
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
        nullptr);
}
}
}
