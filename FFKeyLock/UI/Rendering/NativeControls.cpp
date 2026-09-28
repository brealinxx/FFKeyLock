#include "NativeControls.h"
#include "Surface.h"
#include "../../ThemeManager.h"
#include "../../Platform/GdiUtils.h"
#include <algorithm>
#include <uxtheme.h>
#pragma comment(lib, "UxTheme.lib")
namespace FFKeyLock::UI
{
namespace
{
constexpr wchar_t kDropdownScroll[] = L"FFKeyLock.DropdownScroll";
LRESULT CALLBACK ScrollBarSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void PaintControlFrame(HWND hwnd, bool losingFocus = false);

// Keep the native combo list for selection/keyboard/accessibility, and give
// its scrolling gutter a real child SCROLLBAR using the shared theme adapter.
// The combo is created without WS_VSCROLL so Windows cannot draw a second,
// system-colored non-client scrollbar during its tracking loop.
struct DropdownScroll
{
    HWND list = nullptr, bar = nullptr, combo = nullptr;
    bool closing = false, syncing = false;
};
void SyncDropdownScroll(DropdownScroll& state)
{
    if (state.syncing || !state.bar) return;
    state.syncing = true;
    RECT client{}; GetClientRect(state.list, &client);
    const int count = std::max(0, static_cast<int>(SendMessageW(state.list, LB_GETCOUNT, 0, 0)));
    const int row = std::max(1, static_cast<int>(SendMessageW(state.list, LB_GETITEMHEIGHT, 0, 0)));
    SCROLLINFO range{sizeof(range), SIF_RANGE | SIF_PAGE | SIF_POS};
    range.nMax = std::max(0, count - 1); range.nPage = std::max(1L, client.bottom / row);
    range.nPos = static_cast<int>(SendMessageW(state.list, LB_GETTOPINDEX, 0, 0));
    const int width = MulDiv(ThemeManager::ScrollBarWidth, GetDpiForWindow(state.list), 96);
    RECT before{}; GetWindowRect(state.bar, &before); MapWindowPoints(nullptr, state.list, reinterpret_cast<POINT*>(&before), 2);
    RECT target{std::max(0L, client.right - width), 0, client.right, client.bottom};
    if (!EqualRect(&before, &target))
        SetWindowPos(state.bar, HWND_TOP, target.left, target.top, target.right - target.left, target.bottom,
            SWP_NOACTIVATE | SWP_NOCOPYBITS);
    const bool show = count > static_cast<int>(range.nPage);
    if (((GetWindowLongPtrW(state.bar, GWL_STYLE) & WS_VISIBLE) != 0) != show)
        ShowWindow(state.bar, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    SCROLLINFO old{sizeof(old), SIF_RANGE | SIF_PAGE | SIF_POS}; GetScrollInfo(state.bar, SB_CTL, &old);
    if (range.nMax != old.nMax || range.nPage != old.nPage || range.nPos != old.nPos)
        SetScrollInfo(state.bar, SB_CTL, &range, TRUE);
    state.syncing = false;
}
LRESULT CALLBACK DropdownBarProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    auto& state = *reinterpret_cast<DropdownScroll*>(data);
    if (message == WM_CANCELMODE) state.closing = true;
    const auto result = DefSubclassProc(hwnd, message, w, l);
    if (!IsWindow(hwnd)) return result;
    if (message == WM_LBUTTONUP || message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
    {
        PaintControlFrame(state.list);
    }
    // Restore only after the native tracking loop returns, never from
    // WM_CAPTURECHANGED (which would cancel its final position notification).
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) && !state.closing &&
        IsWindowVisible(state.list) && !GetCapture() && GetActiveWindow() == GetAncestor(state.combo, GA_ROOT) &&
        SendMessageW(state.combo, CB_GETDROPPEDSTATE, 0, 0)) SetCapture(state.list);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, DropdownBarProc, id);
    return result;
}
LRESULT CALLBACK DropdownListProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    auto& state = *reinterpret_cast<DropdownScroll*>(data);
    if (message == WM_SHOWWINDOW || message == WM_WINDOWPOSCHANGING)
    {
        const UINT flags = message == WM_SHOWWINDOW ? (w ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) :
            reinterpret_cast<WINDOWPOS*>(l)->flags;
        if (flags & SWP_SHOWWINDOW) state.closing = false;
        if (flags & SWP_HIDEWINDOW)
        {
            state.closing = true;
            if (GetCapture() == state.bar || GetCapture() == hwnd) ReleaseCapture();
        }
    }
    if (message == WM_CTLCOLORSCROLLBAR && reinterpret_cast<HWND>(l) == state.bar)
        return reinterpret_cast<LRESULT>(HandleCtlColor(hwnd, reinterpret_cast<HDC>(w), state.bar));
    if (message == WM_CAPTURECHANGED && reinterpret_cast<HWND>(l) == state.bar) return 0;
    if (message == WM_VSCROLL && reinterpret_cast<HWND>(l) == state.bar)
    {
        SCROLLINFO range{sizeof(range), SIF_ALL}; GetScrollInfo(state.bar, SB_CTL, &range);
        int next = static_cast<int>(SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0));
        switch (LOWORD(w))
        {
        case SB_LINEUP: --next; break; case SB_LINEDOWN: ++next; break;
        case SB_PAGEUP: next -= range.nPage; break; case SB_PAGEDOWN: next += range.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: next = range.nTrackPos; break;
        case SB_TOP: next = range.nMin; break; case SB_BOTTOM: next = range.nMax; break;
        default: PaintControlFrame(hwnd); return 0;
        }
        next = std::clamp(next, range.nMin, std::max(range.nMin, range.nMax - static_cast<int>(range.nPage) + 1));
        DefSubclassProc(hwnd, LB_SETTOPINDEX, next, 0);
        SyncDropdownScroll(state);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        return 0;
    }
    if (message == WM_MOUSEMOVE && GetCapture() == hwnd)
    {
        POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
        ClientToScreen(hwnd, &point);
        RECT bar{}; GetWindowRect(state.bar, &bar);
        if (IsWindowVisible(state.bar) && PtInRect(&bar, point))
        {
            ScreenToClient(state.bar, &point);
            return SendMessageW(state.bar, message, w, MAKELPARAM(point.x, point.y));
        }
        if (GetPropW(state.bar, L"FFKeyLock.ScrollHot")) SendMessageW(state.bar, WM_MOUSELEAVE, 0, 0);
    }
    if (message == WM_LBUTTONDOWN || (message == WM_NCLBUTTONDOWN && w == HTVSCROLL))
    {
        POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
        if (message == WM_LBUTTONDOWN) ClientToScreen(hwnd, &point);
        RECT bar{}; GetWindowRect(state.bar, &bar);
        if (IsWindowVisible(state.bar) && PtInRect(&bar, point))
        {
            SyncDropdownScroll(state);
            RedrawWindow(state.bar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            state.closing = false;
            ScreenToClient(state.bar, &point);
            SendMessageW(state.bar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
            return 0;
        }
    }
    if (message == WM_NCDESTROY)
    {
        RemovePropW(hwnd, kDropdownScroll);
        RemoveWindowSubclass(hwnd, DropdownListProc, id);
        const auto result = DefSubclassProc(hwnd, message, w, l);
        delete &state; return result;
    }
    const auto result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_SIZE || message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW ||
        message == WM_MOUSEWHEEL || message == WM_KEYDOWN || message == WM_MOUSEMOVE ||
        message == LB_SETTOPINDEX || message == LB_SETCURSEL || message == LB_RESETCONTENT ||
        message == LB_ADDSTRING || message == LB_INSERTSTRING || message == LB_DELETESTRING || message == LB_SETITEMHEIGHT)
        SyncDropdownScroll(state);
    return result;
}
void AdaptDropdownScroll(HWND combo, HWND list)
{
    if (GetPropW(list, kDropdownScroll)) return;
    auto* state = new DropdownScroll{list, nullptr, combo};
    state->bar = CreateWindowExW(WS_EX_NOACTIVATE, L"SCROLLBAR", L"", WS_CHILD | WS_VISIBLE | SBS_VERT,
        0, 0, 0, 0, list, nullptr, reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(list, GWLP_HINSTANCE)), nullptr);
    if (!state->bar) { delete state; return; }
    SetWindowLongPtrW(list, GWL_STYLE, GetWindowLongPtrW(list, GWL_STYLE) | WS_CLIPCHILDREN);
    SetWindowTheme(state->bar, L"", L"");
    SetWindowSubclass(state->bar, ScrollBarSubclassProc, 1, 0);
    SetWindowSubclass(state->bar, DropdownBarProc, 1, reinterpret_cast<DWORD_PTR>(state));
    SetPropW(list, kDropdownScroll, state);
    SetWindowSubclass(list, DropdownListProc, 1, reinterpret_cast<DWORD_PTR>(state));
    SyncDropdownScroll(*state);
}
void PaintButton(HWND hwnd, HDC dc)
{
    RECT r{}; GetClientRect(hwnd, &r);
    const LRESULT state = SendMessageW(hwnd, BM_GETSTATE, 0, 0);
    NMCUSTOMDRAW check{}; check.hdr.hwndFrom = hwnd; check.dwDrawStage = CDDS_PREPAINT; check.hdc = dc; check.rc = r;
    if (GetPropW(hwnd, L"FFKeyLock.ButtonHot")) check.uItemState |= CDIS_HOT;
    if (state & BST_PUSHED) check.uItemState |= CDIS_SELECTED;
    LRESULT result = 0;
    if (DrawCheckBox(check, result)) return;
    DRAWITEMSTRUCT button{}; button.CtlType = ODT_BUTTON; button.hwndItem = hwnd; button.hDC = dc; button.rcItem = r;
    if (!IsWindowEnabled(hwnd)) button.itemState |= ODS_DISABLED;
    if (state & BST_PUSHED) button.itemState |= ODS_SELECTED;
    if (GetFocus() == hwnd) button.itemState |= ODS_FOCUS;
    const auto ui = SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0);
    if (ui & UISF_HIDEFOCUS) button.itemState |= ODS_NOFOCUSRECT;
    if (ui & UISF_HIDEACCEL) button.itemState |= ODS_NOACCEL;
    DrawButton(button);
}
LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_PAINT)
    { PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); PaintButton(hwnd, dc); EndPaint(hwnd, &ps); return 0; }
    if (message == WM_PRINTCLIENT) { PaintButton(hwnd, reinterpret_cast<HDC>(w)); return 0; }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_MOUSEMOVE && !GetPropW(hwnd, L"FFKeyLock.ButtonHot"))
    {
        SetPropW(hwnd, L"FFKeyLock.ButtonHot", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&track); InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE) { RemovePropW(hwnd, L"FFKeyLock.ButtonHot"); InvalidateRect(hwnd, nullptr, FALSE); }
    if (message == WM_NCDESTROY) { RemovePropW(hwnd, L"FFKeyLock.ButtonHot"); RemoveWindowSubclass(hwnd, ButtonSubclassProc, id); }
    const LRESULT result = DefSubclassProc(hwnd, message, w, l);
    if (message == BM_SETCHECK || message == BM_SETSTATE || message == WM_ENABLE || message == WM_SETTEXT ||
        message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_UPDATEUISTATE)
        InvalidateRect(hwnd, nullptr, FALSE);
    return result;
}
void PaintLabel(HWND hwnd, HDC target)
{
    RECT r{}; GetClientRect(hwnd, &r); GdiUtils::BufferedPaint buffer(target, r); HDC dc = buffer.Dc();
    FillRect(dc, &r, BackgroundBrush(hwnd));
    std::wstring text(GetWindowTextLengthW(hwnd) + 1, L'\0'); GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetPropW(hwnd, L"FFKeyLock.MutedText") ? ThemeManager::MutedTextColor() : ThemeManager::TextColor());
    GdiUtils::SelectObjectScope font(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
    const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    UINT flags = DT_NOPREFIX | ((style & SS_TYPEMASK) == SS_CENTER ? DT_CENTER : DT_LEFT);
    if ((style & SS_ELLIPSISMASK) == SS_PATHELLIPSIS) flags |= DT_SINGLELINE | DT_PATH_ELLIPSIS;
    else if (style & SS_ENDELLIPSIS) flags |= DT_SINGLELINE | DT_END_ELLIPSIS;
    else flags |= DT_WORDBREAK;
    DrawTextW(dc, text.c_str(), -1, &r, flags); buffer.Present();
}
LRESULT CALLBACK LabelSubclassProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_PAINT)
    { PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); PaintLabel(hwnd, dc); EndPaint(hwnd, &ps); return 0; }
    if (message == WM_PRINTCLIENT) { PaintLabel(hwnd, reinterpret_cast<HDC>(w)); return 0; }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, LabelSubclassProc, id);
    const LRESULT result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_SETTEXT || message == WM_ENABLE) InvalidateRect(hwnd, nullptr, FALSE);
    return result;
}
// Native edit and combo-list windows retain their input implementation. Paint
// their non-client border; dropdown scrolling uses the themed child below.
void PaintControlFrame(HWND hwnd, bool losingFocus)
{
    HDC target = GetWindowDC(hwnd); if (!target) return;
    RECT window{}, client{}; GetWindowRect(hwnd, &window); GetClientRect(hwnd, &client);
    POINT origin{}; ClientToScreen(hwnd, &origin);
    OffsetRect(&client, origin.x - window.left, origin.y - window.top);
    RECT outer{0, 0, window.right - window.left, window.bottom - window.top};
    ExcludeClipRect(target, client.left, client.top, client.right, client.bottom);
    GdiUtils::BufferedPaint buffer(target, outer); HDC dc = buffer.Dc();
    FillRect(dc, &outer, BackgroundBrush(GetParent(hwnd)));
    const bool edit = GetPropW(hwnd, L"FFKeyLock.RoundedEdit") != nullptr;
    GdiUtils::FillRoundRect(dc, outer, ThemeManager::SurfaceColor(),
        !losingFocus && GetFocus() == hwnd ? ThemeManager::AccentColor() : ThemeManager::BorderColor(),
        edit ? MulDiv(ThemeManager::ControlRadius * 2, GetDpiForWindow(hwnd), 96) : 0);
    buffer.Present(); ReleaseDC(hwnd, target);
}
LRESULT CALLBACK FrameSubclassProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_NCCALCSIZE && GetPropW(hwnd, L"FFKeyLock.RoundedEdit"))
    {
        const LRESULT result = DefSubclassProc(hwnd, message, w, l);
        RECT& client = w ? reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0] : *reinterpret_cast<RECT*>(l);
        const int inset = MulDiv(4, GetDpiForWindow(hwnd), 96);
        InflateRect(&client, -std::min(inset, static_cast<int>(client.right - client.left) / 2),
            -std::min(inset, static_cast<int>(client.bottom - client.top) / 2));
        return result;
    }
    if (message == WM_NCPAINT) { PaintControlFrame(hwnd); return 0; }
    if (message == WM_NCMOUSEMOVE && w == HTVSCROLL && GetCapture() != hwnd) return 0;
    if (message == WM_NCDESTROY) { RemovePropW(hwnd, L"FFKeyLock.RoundedEdit"); RemoveWindowSubclass(hwnd, FrameSubclassProc, id); }
    const LRESULT result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_PAINT || message == WM_VSCROLL || message == WM_NCLBUTTONDOWN || message == WM_NCMOUSEMOVE ||
        message == WM_MOUSEWHEEL || message == WM_SETFOCUS || message == WM_KILLFOCUS ||
        message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW || message == WM_MOUSEMOVE ||
        message == WM_KEYDOWN || message == LB_SETTOPINDEX || message == LB_SETCURSEL)
        PaintControlFrame(hwnd, message == WM_KILLFOCUS);
    return result;
}
// Keep the native combo box for keyboard, popup and accessibility behavior;
// paint the closed field explicitly because themed arrows can stay white.
void PaintCombo(HWND hwnd, HDC target)
{
    RECT r{}; GetClientRect(hwnd, &r);
    GdiUtils::BufferedPaint buffer(target, r);
    HDC dc = buffer.Dc();
    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    const bool focused = GetFocus() == hwnd;
    const int dpi = GetDpiForWindow(hwnd);
    auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
    FillRect(dc, &r, BackgroundBrush(hwnd));
    GdiUtils::FillRoundRect(dc, r, ThemeManager::SurfaceColor(), focused ? ThemeManager::AccentColor() : ThemeManager::BorderColor(), scale(ThemeManager::ControlRadius * 2));
    wchar_t text[256]{}; GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
    RECT label = r; label.left += scale(10); label.right -= scale(28);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, enabled ? ThemeManager::TextColor() : ThemeManager::DisabledTextColor());
    GdiUtils::SelectObjectScope font(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
    DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    const int x = r.right - scale(15), y = (r.bottom + r.top) / 2;
    POINT arrow[] = {{x - scale(4), y - scale(2)}, {x, y + scale(2)}, {x + scale(4), y - scale(2)}};
    HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(1)), enabled ? ThemeManager::TextColor() : ThemeManager::DisabledTextColor());
    { GdiUtils::SelectObjectScope selected(dc, pen); Polyline(dc, arrow, 3); }
    DeleteObject(pen);
    if (focused && !(SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
    { InflateRect(&r, -scale(3), -scale(3)); DrawFocusRect(dc, &r); }
    buffer.Present();
}
LRESULT CALLBACK ComboSubclassProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); PaintCombo(hwnd, dc); EndPaint(hwnd, &ps); return 0;
    }
    if (message == WM_PRINTCLIENT) { PaintCombo(hwnd, reinterpret_cast<HDC>(w)); return 0; }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ComboSubclassProc, id);
    const LRESULT result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
        message == CB_SETCURSEL || message == CB_SHOWDROPDOWN || message == WM_UPDATEUISTATE)
        InvalidateRect(hwnd, nullptr, FALSE);
    // COMBOBOX updates its popup scrollbar directly after showing/selection;
    // that path does not send the list a WM_NCPAINT notification.
    if (message == CB_SHOWDROPDOWN || message == WM_LBUTTONDOWN || message == WM_KEYDOWN ||
        message == WM_SYSKEYDOWN || message == WM_CHAR || message == WM_MOUSEWHEEL || message == CB_SETCURSEL)
    {
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(hwnd, &info) && IsWindowVisible(info.hwndList))
        {
            if (auto* scroll = static_cast<DropdownScroll*>(GetPropW(info.hwndList, kDropdownScroll))) SyncDropdownScroll(*scroll);
            PaintControlFrame(info.hwndList);
        }
    }
    return result;
}

// A standard SCROLLBAR still owns hit testing, dragging, repeat, keyboard
// input and accessibility. Only its pixels are supplied by the app theme.
void PaintScrollBar(HWND hwnd, HDC target)
{
    RECT r{}; GetClientRect(hwnd, &r);
    GdiUtils::BufferedPaint buffer(target, r); HDC dc = buffer.Dc();
    // Let the native renderer calculate its hit-test/accessibility geometry in
    // the back buffer before replacing its pixels with the theme.
    SetPropW(hwnd, L"FFKeyLock.ScrollMeasure", reinterpret_cast<HANDLE>(1));
    DefSubclassProc(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    RemovePropW(hwnd, L"FFKeyLock.ScrollMeasure");
    FillRect(dc, &r, GetPropW(GetParent(hwnd), kDropdownScroll) ? ThemeManager::SurfaceBrush() : BackgroundBrush(hwnd));
    SCROLLBARINFO info{sizeof(info)};
    if (GetScrollBarInfo(hwnd, OBJID_CLIENT, &info))
    {
        SCROLLINFO range{sizeof(range), SIF_RANGE | SIF_PAGE}; GetScrollInfo(hwnd, SB_CTL, &range);
        const bool enabled = IsWindowEnabled(hwnd) && range.nMax - range.nMin + 1 > static_cast<int>(range.nPage);
        const int dpi = GetDpiForWindow(hwnd);
        auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
        const int inset = scale(4), arrow = info.dxyLineButton;
        if (enabled && info.xyThumbBottom > info.xyThumbTop)
        {
            RECT thumb{inset, info.xyThumbTop, r.right - inset, info.xyThumbBottom};
            const COLORREF color = ThemeManager::ScrollThumbColor(GetPropW(hwnd, L"FFKeyLock.ScrollHot") != nullptr, GetCapture() == hwnd);
            GdiUtils::FillRoundRect(dc, thumb, color, color, thumb.right - thumb.left);
        }
        HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(1)), enabled ? ThemeManager::MutedTextColor() : ThemeManager::DisabledTextColor());
        {
            GdiUtils::SelectObjectScope selected(dc, pen);
            const int x = r.right / 2, top = arrow / 2, bottom = r.bottom - arrow / 2;
            POINT up[] = {{x - scale(3), top + scale(2)}, {x, top - scale(1)}, {x + scale(3), top + scale(2)}};
            POINT down[] = {{x - scale(3), bottom - scale(2)}, {x, bottom + scale(1)}, {x + scale(3), bottom - scale(2)}};
            Polyline(dc, up, 3); Polyline(dc, down, 3);
        }
        DeleteObject(pen);
    }
    if (GetFocus() == hwnd && !(SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
    { InflateRect(&r, -1, -1); DrawFocusRect(dc, &r); }
    buffer.Present();
}
int ScrollBarPart(HWND hwnd, POINT point)
{
    RECT client{}; GetClientRect(hwnd, &client);
    if (!PtInRect(&client, point)) return -1;
    SCROLLBARINFO geometry{sizeof(geometry)};
    if (!GetScrollBarInfo(hwnd, OBJID_CLIENT, &geometry)) return -1;
    if (point.y < geometry.dxyLineButton) return SB_LINEUP;
    if (point.y >= client.bottom - geometry.dxyLineButton) return SB_LINEDOWN;
    if (point.y < geometry.xyThumbTop) return SB_PAGEUP;
    if (point.y >= geometry.xyThumbBottom) return SB_PAGEDOWN;
    return -1;
}
constexpr UINT_PTR kScrollRepeat = 0x4646;
LRESULT CALLBACK ScrollBarSubclassProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
{
    // Classic arrow buttons paint directly without WM_CTLCOLORSCROLLBAR.
    // Handle their press/repeat (and page track) here; the native control still
    // owns thumb dragging, keyboard input, range and accessibility geometry.
    if (message == WM_LBUTTONDOWN)
    {
        POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
        const int part = ScrollBarPart(hwnd, point);
        if (part >= 0 && IsWindowEnabled(hwnd))
        {
            if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_TABSTOP) SetFocus(hwnd);
            SetPropW(hwnd, L"FFKeyLock.ScrollPress", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(part + 1)));
            SetCapture(hwnd);
            SetTimer(hwnd, kScrollRepeat, 350, nullptr);
            SendMessageW(GetParent(hwnd), WM_VSCROLL, part, reinterpret_cast<LPARAM>(hwnd));
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            return 0;
        }
    }
    if (message == WM_TIMER && w == kScrollRepeat)
    {
        const auto press = reinterpret_cast<INT_PTR>(GetPropW(hwnd, L"FFKeyLock.ScrollPress"));
        POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd, &point);
        if (press && GetCapture() == hwnd && ScrollBarPart(hwnd, point) == press - 1)
            SendMessageW(GetParent(hwnd), WM_VSCROLL, press - 1, reinterpret_cast<LPARAM>(hwnd));
        SetTimer(hwnd, kScrollRepeat, 60, nullptr);
        return 0;
    }
    if (message == WM_LBUTTONUP || message == WM_CANCELMODE || message == WM_CAPTURECHANGED || message == WM_NCDESTROY)
    {
        if (RemovePropW(hwnd, L"FFKeyLock.ScrollPress"))
        {
            KillTimer(hwnd, kScrollRepeat);
            if (GetCapture() == hwnd) ReleaseCapture();
            SendMessageW(GetParent(hwnd), WM_VSCROLL, SB_ENDSCROLL, reinterpret_cast<LPARAM>(hwnd));
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            if (message == WM_LBUTTONUP) return 0;
        }
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); PaintScrollBar(hwnd, dc); EndPaint(hwnd, &ps); return 0;
    }
    if (message == WM_PRINTCLIENT) { PaintScrollBar(hwnd, reinterpret_cast<HDC>(w)); return 0; }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_MOUSEWHEEL) return SendMessageW(GetParent(hwnd), message, w, l);
    // The native class paints hover directly to a window DC, bypassing WM_PAINT.
    // Keep native drag/capture semantics; ordinary hover belongs to our theme.
    if (message == WM_MOUSEMOVE)
    {
        if (!GetPropW(hwnd, L"FFKeyLock.ScrollHot"))
        {
            SetPropW(hwnd, L"FFKeyLock.ScrollHot", reinterpret_cast<HANDLE>(1));
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&track);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        if (GetCapture() != hwnd || GetPropW(hwnd, L"FFKeyLock.ScrollPress")) return 0;
    }
    if (message == WM_MOUSELEAVE)
    {
        RemovePropW(hwnd, L"FFKeyLock.ScrollHot");
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); return 0;
    }
    if (message == SBM_SETSCROLLINFO || message == SBM_SETPOS || message == SBM_SETRANGEREDRAW)
    {
        const LRESULT result = message == SBM_SETSCROLLINFO ? DefSubclassProc(hwnd, message, FALSE, l) :
            message == SBM_SETPOS ? DefSubclassProc(hwnd, message, w, FALSE) : DefSubclassProc(hwnd, SBM_SETRANGE, w, l);
        // Never ask SetScrollInfo/SetScrollPos to draw a system-colored frame,
        // and do not leave an old thumb visible until an idle WM_PAINT.
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        return result;
    }
    if (message == WM_NCDESTROY) { RemovePropW(hwnd, L"FFKeyLock.ScrollHot"); RemoveWindowSubclass(hwnd, ScrollBarSubclassProc, id); }
    const LRESULT result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_ENABLE || message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN ||
        message == WM_LBUTTONUP || message == WM_CAPTURECHANGED || message == WM_KEYDOWN ||
        message == WM_KEYUP || message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_UPDATEUISTATE)
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    return result;
}

void ApplyControlTheme(HWND hwnd)
{
    wchar_t type[64]{}; GetClassNameW(hwnd, type, 64);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::UiFont()), FALSE);
    // Do not ask undocumented DarkMode_* classes to partially theme a control.
    // Each adapter below owns all its client pixels; native state/input stay intact.
    if (_wcsicmp(type, L"Button") == 0)
    {
        SetWindowTheme(hwnd, L"", L"");
        SetWindowSubclass(hwnd, ButtonSubclassProc, 1, 0);
    }
    else if (_wcsicmp(type, L"Static") == 0)
        SetWindowSubclass(hwnd, LabelSubclassProc, 1, 0);
    else if (_wcsicmp(type, L"ScrollBar") == 0)
    {
        SetWindowTheme(hwnd, L"", L""); SetWindowSubclass(hwnd, ScrollBarSubclassProc, 1, 0);
    }
    else if (_wcsicmp(type, L"ComboBox") == 0)
    {
        SetWindowTheme(hwnd, L"", L""); SetWindowSubclass(hwnd, ComboSubclassProc, 1, 0);
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(hwnd, &info))
        {
            SetWindowTheme(info.hwndList, L"", L"");
            SetWindowSubclass(info.hwndList, FrameSubclassProc, 1, 0);
            AdaptDropdownScroll(hwnd, info.hwndList);
            RedrawWindow(info.hwndList, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
        }
        SendMessageW(hwnd, CB_SETMINVISIBLE, 8, 0);
        SendMessageW(hwnd, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), MulDiv(26, GetDpiForWindow(hwnd), 96));
        SendMessageW(hwnd, CB_SETITEMHEIGHT, 0, MulDiv(26, GetDpiForWindow(hwnd), 96));
    }
    else if (_wcsicmp(type, L"Edit") == 0)
    {
        SetWindowTheme(hwnd, L"", L"");
        // Create edits without WS_BORDER: the native class caches that flag
        // and can paint a second square frame even after removing the style.
        SetPropW(hwnd, L"FFKeyLock.RoundedEdit", reinterpret_cast<HANDLE>(1));
        SetWindowSubclass(hwnd, FrameSubclassProc, 1, 0);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
    }
    else if (_wcsicmp(type, L"ListBox") == 0) SetWindowTheme(hwnd, L"", L"");
    InvalidateRect(hwnd, nullptr, FALSE);
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

HBRUSH HandleCtlColor(HWND, HDC hdc, HWND control)
{
    wchar_t type[32]{};
    GetClassNameW(control, type, 32);
    DWORD_PTR adapter = 0;
    if (_wcsicmp(type, L"ScrollBar") == 0 && GetWindowSubclass(control, ScrollBarSubclassProc, 1, &adapter) &&
        !GetPropW(control, L"FFKeyLock.ScrollMeasure"))
    {
        // Classic SCROLLBAR also draws directly inside its native tracking
        // loop. Supply the themed frame before that draw and exclude its
        // system pixels from this DC, just as we do for native menu arrows.
        // Clear only the explicit clip from the previous use of the DC; the
        // window's system visibility/child clipping remains in force.
        SelectClipRgn(hdc, nullptr);
        SendMessageW(control, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(hdc), PRF_CLIENT);
        RECT client{}; GetClientRect(control, &client);
        ExcludeClipRect(hdc, client.left, client.top, client.right, client.bottom);
    }
    const bool label = _wcsicmp(type, L"Static") == 0 || _wcsicmp(type, L"Button") == 0 || _wcsicmp(type, L"ListBox") == 0 || _wcsicmp(type, L"ScrollBar") == 0;
    SetBkMode(hdc, OPAQUE);
    const bool dropdownBar = _wcsicmp(type, L"ScrollBar") == 0 && GetPropW(GetParent(control), kDropdownScroll);
    SetBkColor(hdc, label && !dropdownBar ? BackgroundColor(control) : ThemeManager::SurfaceColor());
    SetTextColor(hdc, !IsWindowEnabled(control) ? ThemeManager::DisabledTextColor() : GetPropW(control, L"FFKeyLock.MutedText") ? ThemeManager::MutedTextColor() : ThemeManager::TextColor());
    return label && !dropdownBar ? BackgroundBrush(control) : ThemeManager::SurfaceBrush();
}

bool DrawCheckBox(const NMCUSTOMDRAW& item, LRESULT& result)
{
    wchar_t name[32]{}; GetClassNameW(item.hdr.hwndFrom, name, 32);
    if (_wcsicmp(name, L"Button") != 0) return false;
    const LONG_PTR style = GetWindowLongPtrW(item.hdr.hwndFrom, GWL_STYLE);
    const LONG_PTR type = style & BS_TYPEMASK;
    if (type != BS_AUTOCHECKBOX && type != BS_CHECKBOX) return false;
    result = CDRF_DODEFAULT;
    if (item.dwDrawStage != CDDS_PREPAINT) return true;
    const HWND hwnd = item.hdr.hwndFrom;
    const bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    const bool hot = (item.uItemState & CDIS_HOT) != 0;
    const bool pressed = (item.uItemState & CDIS_SELECTED) != 0;
    const bool key = (style & BS_PUSHLIKE) != 0;
    auto scale = [hwnd](int n) { return MulDiv(n, GetDpiForWindow(hwnd), 96); };
    RECT r = item.rc;
    GdiUtils::BufferedPaint buffer(item.hdc, r); HDC dc = buffer.Dc();
    FillRect(dc, &r, BackgroundBrush(hwnd));
    SetBkMode(dc, TRANSPARENT);
    const COLORREF ink = !enabled ? ThemeManager::DisabledTextColor() : checked && key && ThemeManager::HighContrast() ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ThemeManager::TextColor();
    SetTextColor(dc, ink);
    RECT label = r;
    if (key)
    {
        const COLORREF fill = checked ? ThemeManager::SelectionColor() : pressed ? ThemeManager::ButtonPressedColor() : hot ? ThemeManager::ButtonHotColor() : ThemeManager::ButtonColor();
        GdiUtils::FillRoundRect(dc, r, fill, checked || hot ? ThemeManager::AccentColor() : ThemeManager::BorderColor(), scale(ThemeManager::ControlRadius * 2));
        // An underline distinguishes selected keys without relying on color alone.
        if (checked)
        {
            RECT mark{r.left + scale(5), r.bottom - scale(4), r.right - scale(5), r.bottom - scale(2)};
            HBRUSH brush = CreateSolidBrush(ThemeManager::AccentColor()); FillRect(dc, &mark, brush); DeleteObject(brush);
        }
        InflateRect(&label, -scale(2), 0);
    }
    else
    {
        const int size = scale(16), top = (r.top + r.bottom - size) / 2;
        RECT box{r.left + scale(1), top, r.left + scale(1) + size, top + size};
        GdiUtils::FillRoundRect(dc, box, checked ? ThemeManager::AccentColor() : ThemeManager::SurfaceColor(),
            !enabled ? ThemeManager::DisabledTextColor() : hot ? ThemeManager::AccentColor() : ThemeManager::BorderColor(), scale(3));
        if (checked)
        {
            POINT tick[] = {{box.left + scale(3), top + scale(8)}, {box.left + scale(7), top + scale(12)}, {box.left + scale(13), top + scale(4)}};
            HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(2)), ThemeManager::HighContrast() ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ThemeManager::IsDark() ? RGB(18, 26, 36) : RGB(255, 255, 255));
            { GdiUtils::SelectObjectScope selected(dc, pen); Polyline(dc, tick, 3); }
            DeleteObject(pen);
        }
        label.left = box.right + scale(8);
    }
    wchar_t text[256]{}; GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
    GdiUtils::SelectObjectScope font(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
    DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | (key ? DT_CENTER : DT_LEFT));
    if (GetFocus() == hwnd && !(SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
    { InflateRect(&r, -scale(2), -scale(2)); DrawFocusRect(dc, &r); }
    buffer.Present(); result = CDRF_SKIPDEFAULT; return true;
}

void DrawButton(const DRAWITEMSTRUCT& item)
{
    GdiUtils::BufferedPaint buffer(item.hDC, item.rcItem);
    HDC hdc = buffer.Dc();
    auto scale = [&](int value) { return MulDiv(value, GetDpiForWindow(item.hwndItem), 96); };
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = GetPropW(item.hwndItem, L"FFKeyLock.ButtonHot") != nullptr;

    COLORREF fill = ThemeManager::ButtonColor();
    if (disabled)
    {
        fill = ThemeManager::HighContrast() ? GetSysColor(COLOR_BTNFACE) : ThemeManager::ButtonColor();
    }
    else if (pressed)
    {
        fill = ThemeManager::ButtonPressedColor();
    }
    else if (hot)
    {
        fill = ThemeManager::ButtonHotColor();
    }

    RECT rect = item.rcItem;
    HBRUSH background = CreateSolidBrush(BackgroundColor(item.hwndItem));
    FillRect(hdc, &rect, background);
    DeleteObject(background);
    const bool flat = GetPropW(item.hwndItem, L"FFKeyLock.FlatButton") != nullptr;
    if (flat && !hot && !pressed) fill = BackgroundColor(item.hwndItem);
    DrawRoundRect(hdc, rect, fill, flat ? fill : hot && !disabled ? ThemeManager::AccentColor() : ThemeManager::BorderColor(), scale(ThemeManager::ControlRadius * 2));

    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, disabled ? ThemeManager::DisabledTextColor() : ThemeManager::TextColor());
    const auto font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = SelectObject(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    if (pressed)
    {
        OffsetRect(&rect, 1, 1);
    }
    DrawTextW(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | ((item.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
    SelectObject(hdc, oldFont);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT))
    { InflateRect(&rect, -scale(3), -scale(3)); DrawFocusRect(hdc, &rect); }
    buffer.Present();
}


void ApplyTheme(HWND root)
{
    if (!root) return;
    EnumChildWindows(root, ApplyThemeToChild, 0);
    RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}
}
