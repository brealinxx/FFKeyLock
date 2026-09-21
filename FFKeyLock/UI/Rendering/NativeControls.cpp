#include "NativeControls.h"
#include "../../ThemeManager.h"
#include "../../Platform/GdiUtils.h"
#include <algorithm>
#include <uxtheme.h>
#pragma comment(lib, "UxTheme.lib")
namespace FFKeyLock::UI
{
namespace
{
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
    FillRect(dc, &r, ThemeManager::WindowBrush());
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
// their non-client border and (for long dropdowns) native scrollbar as well.
void PaintControlFrame(HWND hwnd)
{
    HDC target = GetWindowDC(hwnd); if (!target) return;
    RECT window{}, client{}; GetWindowRect(hwnd, &window); GetClientRect(hwnd, &client);
    POINT origin{}; ClientToScreen(hwnd, &origin);
    OffsetRect(&client, origin.x - window.left, origin.y - window.top);
    RECT outer{0, 0, window.right - window.left, window.bottom - window.top};
    ExcludeClipRect(target, client.left, client.top, client.right, client.bottom);
    GdiUtils::BufferedPaint buffer(target, outer); HDC dc = buffer.Dc();
    FillRect(dc, &outer, ThemeManager::SurfaceBrush());
    HBRUSH border = CreateSolidBrush(GetFocus() == hwnd ? ThemeManager::AccentColor() : ThemeManager::BorderColor());
    FrameRect(dc, &outer, border); DeleteObject(border);
    SCROLLBARINFO info{sizeof(info)};
    if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL) && GetScrollBarInfo(hwnd, OBJID_VSCROLL, &info) &&
        !(info.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)))
    {
        RECT bar = info.rcScrollBar; OffsetRect(&bar, -window.left, -window.top);
        FillRect(dc, &bar, ThemeManager::WindowBrush());
        auto scale = [hwnd](int n) { return MulDiv(n, GetDpiForWindow(hwnd), 96); };
        if (info.xyThumbBottom > info.xyThumbTop)
        {
            RECT thumb{bar.left + scale(4), bar.top + info.xyThumbTop, bar.right - scale(4), bar.top + info.xyThumbBottom};
            GdiUtils::FillRoundRect(dc, thumb, ThemeManager::MutedTextColor(), ThemeManager::MutedTextColor(), scale(5));
        }
        HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(1)), ThemeManager::MutedTextColor());
        {
            GdiUtils::SelectObjectScope selected(dc, pen);
            const int x = (bar.left + bar.right) / 2, top = bar.top + info.dxyLineButton / 2, bottom = bar.bottom - info.dxyLineButton / 2;
            POINT up[] = {{x - scale(3), top + scale(2)}, {x, top - scale(1)}, {x + scale(3), top + scale(2)}};
            POINT down[] = {{x - scale(3), bottom - scale(2)}, {x, bottom + scale(1)}, {x + scale(3), bottom - scale(2)}};
            Polyline(dc, up, 3); Polyline(dc, down, 3);
        }
        DeleteObject(pen);
    }
    buffer.Present(); ReleaseDC(hwnd, target);
}
LRESULT CALLBACK FrameSubclassProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_NCPAINT) { PaintControlFrame(hwnd); return 0; }
    if (message == WM_NCMOUSEMOVE && w == HTVSCROLL && GetCapture() != hwnd) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, FrameSubclassProc, id);
    const LRESULT result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_PAINT || message == WM_VSCROLL || message == WM_NCLBUTTONDOWN || message == WM_NCMOUSEMOVE ||
        message == WM_MOUSEWHEEL || message == WM_SETFOCUS || message == WM_KILLFOCUS ||
        message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW || message == WM_MOUSEMOVE ||
        message == WM_KEYDOWN || message == LB_SETTOPINDEX || message == LB_SETCURSEL)
        PaintControlFrame(hwnd);
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
    FillRect(dc, &r, ThemeManager::WindowBrush());
    GdiUtils::FillRoundRect(dc, r, ThemeManager::SurfaceColor(), focused ? ThemeManager::AccentColor() : ThemeManager::BorderColor(), scale(6));
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
        if (GetComboBoxInfo(hwnd, &info) && IsWindowVisible(info.hwndList)) PaintControlFrame(info.hwndList);
    }
    return result;
}

// A standard SCROLLBAR still owns hit testing, dragging, repeat, keyboard
// input and accessibility. Only its pixels are supplied by the app theme.
void PaintScrollBar(HWND hwnd, HDC target)
{
    RECT r{}; GetClientRect(hwnd, &r);
    GdiUtils::BufferedPaint buffer(target, r); HDC dc = buffer.Dc();
    FillRect(dc, &r, ThemeManager::WindowBrush());
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
            const COLORREF color = GetCapture() == hwnd ? ThemeManager::AccentColor() : ThemeManager::MutedTextColor();
            GdiUtils::FillRoundRect(dc, thumb, color, color, scale(5));
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
    buffer.Present();
}
LRESULT CALLBACK ScrollBarSubclassProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); PaintScrollBar(hwnd, dc); EndPaint(hwnd, &ps); return 0;
    }
    if (message == WM_PRINTCLIENT) { PaintScrollBar(hwnd, reinterpret_cast<HDC>(w)); return 0; }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_MOUSEWHEEL) return SendMessageW(GetParent(hwnd), message, w, l);
    // The native class paints hover directly to a window DC, bypassing WM_PAINT.
    // Our appearance has no separate hover state. Keep its drag/capture path,
    // but do not start the native hover renderer on ordinary pointer movement.
    if ((message == WM_MOUSEMOVE && GetCapture() != hwnd) || message == WM_MOUSELEAVE) return 0;
    if (message == SBM_SETSCROLLINFO || message == SBM_SETPOS || message == SBM_SETRANGEREDRAW)
    {
        const LRESULT result = message == SBM_SETSCROLLINFO ? DefSubclassProc(hwnd, message, FALSE, l) :
            message == SBM_SETPOS ? DefSubclassProc(hwnd, message, w, FALSE) : DefSubclassProc(hwnd, SBM_SETRANGE, w, l);
        // Never ask SetScrollInfo/SetScrollPos to draw a system-colored frame,
        // and do not leave an old thumb visible until an idle WM_PAINT.
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        return result;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ScrollBarSubclassProc, id);
    const LRESULT result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_ENABLE || message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN ||
        message == WM_LBUTTONUP || message == WM_CAPTURECHANGED)
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
            RedrawWindow(info.hwndList, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
        }
        SendMessageW(hwnd, CB_SETMINVISIBLE, 8, 0);
        SendMessageW(hwnd, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), MulDiv(26, GetDpiForWindow(hwnd), 96));
        SendMessageW(hwnd, CB_SETITEMHEIGHT, 0, MulDiv(26, GetDpiForWindow(hwnd), 96));
    }
    else if (_wcsicmp(type, L"Edit") == 0)
    {
        SetWindowTheme(hwnd, L"", L""); SetWindowSubclass(hwnd, FrameSubclassProc, 1, 0);
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
    const bool label = _wcsicmp(type, L"Static") == 0 || _wcsicmp(type, L"Button") == 0 || _wcsicmp(type, L"ListBox") == 0;
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, label ? ThemeManager::WindowColor() : ThemeManager::SurfaceColor());
    SetTextColor(hdc, !IsWindowEnabled(control) ? ThemeManager::DisabledTextColor() : GetPropW(control, L"FFKeyLock.MutedText") ? ThemeManager::MutedTextColor() : ThemeManager::TextColor());
    return label ? ThemeManager::WindowBrush() : ThemeManager::SurfaceBrush();
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
    FillRect(dc, &r, ThemeManager::WindowBrush());
    SetBkMode(dc, TRANSPARENT);
    const COLORREF ink = !enabled ? ThemeManager::DisabledTextColor() : checked && key && ThemeManager::HighContrast() ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ThemeManager::TextColor();
    SetTextColor(dc, ink);
    RECT label = r;
    if (key)
    {
        const COLORREF fill = checked ? ThemeManager::SelectionColor() : pressed ? ThemeManager::ButtonPressedColor() : hot ? ThemeManager::ButtonHotColor() : ThemeManager::ButtonColor();
        GdiUtils::FillRoundRect(dc, r, fill, checked || hot ? ThemeManager::AccentColor() : ThemeManager::BorderColor(), scale(5));
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
    HBRUSH background = CreateSolidBrush(ThemeManager::WindowColor());
    FillRect(hdc, &rect, background);
    DeleteObject(background);
    const bool flat = GetPropW(item.hwndItem, L"FFKeyLock.FlatButton") != nullptr;
    if (flat && !hot && !pressed) fill = ThemeManager::WindowColor();
    DrawRoundRect(hdc, rect, fill, flat ? fill : hot && !disabled ? ThemeManager::AccentColor() : ThemeManager::BorderColor(), scale(7));

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
