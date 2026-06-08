#include "ToggleSwitch.h"

#include "../../Platform/DpiUtils.h"
#include "../../Platform/GdiUtils.h"
#include "../../ThemeManager.h"

#include <windowsx.h>

namespace FFKeyLock
{
namespace ToggleSwitch
{
namespace
{
constexpr wchar_t kCheckedProp[] = L"FFKeyLockToggleChecked";
constexpr wchar_t kHotProp[] = L"FFKeyLockToggleHot";

bool IsHot(HWND hwnd)
{
    return GetPropW(hwnd, kHotProp) != nullptr;
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

void RedrawToggle(HWND hwnd)
{
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT paint{};
    HDC hdc = BeginPaint(hwnd, &paint);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    GdiUtils::BufferedPaint buffer(hdc, rect);
    HDC drawDc = buffer.Dc();
    const UINT dpi = DpiUtils::GetWindowDpi(hwnd);
    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    const bool checked = IsChecked(hwnd);

    const COLORREF track = checked
        ? ThemeManager::AccentColor()
        : (IsHot(hwnd) ? ThemeManager::ButtonHotColor() : ThemeManager::ButtonColor());
    const COLORREF border = checked ? ThemeManager::AccentColor() : ThemeManager::BorderColor();
    HBRUSH background = CreateSolidBrush(ThemeManager::WindowColor());
    FillRect(drawDc, &rect, background);
    DeleteObject(background);

    GdiUtils::FillRoundRect(drawDc, rect, enabled ? track : ThemeManager::SurfaceColor(), border, rect.bottom - rect.top);

    const int margin = DpiUtils::Scale(3, dpi);
    const int knobSize = (rect.bottom - rect.top) - margin * 2;
    RECT knob{};
    knob.top = rect.top + margin;
    knob.bottom = knob.top + knobSize;
    if (checked)
    {
        knob.right = rect.right - margin;
        knob.left = knob.right - knobSize;
    }
    else
    {
        knob.left = rect.left + margin;
        knob.right = knob.left + knobSize;
    }

    const COLORREF knobColor = enabled ? RGB(255, 255, 255) : ThemeManager::BorderColor();
    GdiUtils::FillRoundRect(drawDc, knob, knobColor, knobColor, knobSize);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);

    switch (message)
    {
    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_MOUSEMOVE:
        if (!IsHot(hwnd))
        {
            SetHot(hwnd, true);
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&track);
            RedrawToggle(hwnd);
        }
        return 0;

    case WM_MOUSELEAVE:
        SetHot(hwnd, false);
        RedrawToggle(hwnd);
        return 0;

    case WM_LBUTTONUP:
    {
        RECT rect{};
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        GetClientRect(hwnd, &rect);
        if (PtInRect(&rect, point))
        {
            SetChecked(hwnd, !IsChecked(hwnd));
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), StateChangedNotification), reinterpret_cast<LPARAM>(hwnd));
        }
        return 0;
    }

    case WM_ENABLE:
        RedrawToggle(hwnd);
        return 0;

    case WM_NCDESTROY:
        RemovePropW(hwnd, kCheckedProp);
        RemovePropW(hwnd, kHotProp);
        return 0;
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
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = ClassName;
    RegisterClassExW(&wc);
    registered = true;
}

HWND Create(HWND parent, int id, bool checked, int x, int y, int width, int height)
{
    Register(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)));
    HWND hwnd = CreateWindowExW(
        0,
        ClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
        nullptr);
    SetChecked(hwnd, checked);
    return hwnd;
}

bool IsChecked(HWND hwnd)
{
    return GetPropW(hwnd, kCheckedProp) != nullptr;
}

void SetChecked(HWND hwnd, bool checked)
{
    if (checked)
    {
        SetPropW(hwnd, kCheckedProp, reinterpret_cast<HANDLE>(1));
    }
    else
    {
        RemovePropW(hwnd, kCheckedProp);
    }
    RedrawToggle(hwnd);
}
}
}
