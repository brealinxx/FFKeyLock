#include "ScrollView.h"
#include "../Rendering/Surface.h"
#include "../../AppState.h"
#include "../../Platform/DpiUtils.h"
#include <algorithm>
#include <windowsx.h>
namespace FFKeyLock::UI
{
void ScrollView::Create(HWND viewport)
{
    viewport_ = viewport; dpi_ = GetDpiForWindow(viewport);
    WNDCLASSW wc{}; wc.lpfnWndProc = ContentProc; wc.hInstance = g_hInst;
    wc.lpszClassName = L"FFKeyLockScrollContent"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    content_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, viewport, nullptr, g_hInst, nullptr);
    bar_ = CreateWindowExW(0, L"SCROLLBAR", L"", WS_CHILD | WS_VISIBLE | SBS_VERT,
        0, 0, 0, 0, viewport, nullptr, g_hInst, nullptr);
}
int ScrollView::ContentWidth()
{
    const UINT dpi = GetDpiForWindow(viewport_);
    if (dpi != dpi_) { position_ = MulDiv(position_, dpi, dpi_); dpi_ = dpi; }
    RECT r{}; GetClientRect(viewport_, &r);
    return std::max(1, static_cast<int>(r.right) - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_));
}
void ScrollView::SetExtent(int height)
{
    height_ = height;
    RECT r{}; GetClientRect(viewport_, &r);
    const int width = ContentWidth();
    position_ = std::clamp(position_, 0, std::max(0, height_ - static_cast<int>(r.bottom)));
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    info.nMax = std::max(0, height_ - 1); info.nPage = r.bottom; info.nPos = position_;
    SetScrollInfo(bar_, SB_CTL, &info, FALSE);
    SetWindowPos(bar_, nullptr, width, 0, r.right - width, r.bottom, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
    SetWindowPos(content_, nullptr, 0, -position_, width, std::max(height_, static_cast<int>(r.bottom)),
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_NOREDRAW);
    InvalidateSurface(viewport_);
}
void ScrollView::ScrollTo(int position)
{
    RECT r{}; GetClientRect(viewport_, &r);
    position = std::clamp(position, 0, std::max(0, height_ - static_cast<int>(r.bottom)));
    if (position_ == position) return;
    position_ = position;
    SetScrollPos(bar_, SB_CTL, position_, FALSE);
    // Do not blit a partially clipped child window or compose a second backing
    // surface. Invalidate the destination, exposed viewport AND descendants.
    SetWindowPos(content_, nullptr, 0, -position_, 0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_NOREDRAW);
    InvalidateSurface(viewport_);
}
void ScrollView::Reset() { remainder_ = 0; ScrollTo(0); }
void ScrollView::OnWheel(WPARAM value)
{
    remainder_ += GET_WHEEL_DELTA_WPARAM(value);
    const int steps = remainder_ / WHEEL_DELTA; remainder_ %= WHEEL_DELTA;
    UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    RECT r{}; GetClientRect(viewport_, &r);
    ScrollTo(position_ - steps * (lines == WHEEL_PAGESCROLL ? r.bottom :
        DpiUtils::Scale(20, dpi_) * static_cast<int>(std::min(lines, 100u))));
}
void ScrollView::OnScroll(WPARAM value)
{
    SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(bar_, SB_CTL, &info);
    int next = position_;
    switch (LOWORD(value))
    {
    case SB_LINEUP: next -= DpiUtils::Scale(32, dpi_); break;
    case SB_LINEDOWN: next += DpiUtils::Scale(32, dpi_); break;
    case SB_PAGEUP: next -= info.nPage; break;
    case SB_PAGEDOWN: next += info.nPage; break;
    case SB_THUMBPOSITION: case SB_THUMBTRACK: next = info.nTrackPos; break;
    case SB_TOP: next = 0; break;
    case SB_BOTTOM: next = height_; break;
    }
    ScrollTo(next);
}
void ScrollView::EnsureVisible(HWND child)
{
    RECT r{}, c{}; GetWindowRect(child, &r); MapWindowPoints(nullptr, viewport_, reinterpret_cast<POINT*>(&r), 2);
    GetClientRect(viewport_, &c);
    if (r.top < 0) ScrollTo(position_ + r.top - DpiUtils::Scale(8, dpi_));
    else if (r.bottom > c.bottom) ScrollTo(position_ + r.bottom - c.bottom + DpiUtils::Scale(8, dpi_));
}
void ScrollView::TrackFocus(HWND child) { SetWindowSubclass(child, FocusProc, 1, reinterpret_cast<DWORD_PTR>(this)); }
LRESULT CALLBACK ScrollView::FocusProc(HWND child, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    auto& self = *reinterpret_cast<ScrollView*>(data);
    if (message == WM_SETFOCUS) self.EnsureVisible(child);
    if (message == WM_MOUSEWHEEL)
    {
        wchar_t type[32]{}; GetClassNameW(child, type, 32);
        if (_wcsicmp(type, L"ComboBox") != 0 || !SendMessageW(child, CB_GETDROPPEDSTATE, 0, 0))
        { self.OnWheel(w); return 0; }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(child, FocusProc, id);
    return DefSubclassProc(child, message, w, l);
}
LRESULT CALLBACK ScrollView::ContentProc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    switch (message)
    {
    case WM_COMMAND: case WM_NOTIFY: case WM_DRAWITEM: case WM_MEASUREITEM:
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
    case WM_MOUSEWHEEL: return SendMessageW(GetParent(hwnd), message, w, l);
    case WM_PAINT: case WM_PRINTCLIENT: case WM_ERASEBKGND: return PaintSurface(hwnd, message, w);
    }
    return DefWindowProcW(hwnd, message, w, l);
}
}
