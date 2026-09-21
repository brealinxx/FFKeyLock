#include "MenuBar.h"
#include "PopupMenu.h"
#include "../Rendering/NativeControls.h"
#include "../Rendering/Surface.h"
#include "../../AppState.h"
#include "../../Platform/DpiUtils.h"
#include "../../ThemeManager.h"
#include <algorithm>
#include <string>
#include <cwctype>
#include <windowsx.h>
namespace FFKeyLock::UI
{
thread_local MenuBar* MenuBar::tracked_ = nullptr;
HWND MenuBar::Create(HWND parent)
{
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = g_hInst;
    wc.lpszClassName = L"FFKeyLockMenuBar"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    return CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, nullptr, g_hInst, this);
}
void MenuBar::SetMenu(HMENU menu)
{
    // A theme/language refresh can arrive inside TrackPopupMenu's modal loop.
    // Keep its live tree valid until PopupMenuTheme has restored item metadata.
    if (tracking_)
    {
        if (pendingMenu_) DestroyMenu(pendingMenu_);
        pendingMenu_ = menu; next_ = -1; EndMenu(); return;
    }
    if (menu_) DestroyMenu(menu_); menu_ = menu;
    const int count = GetMenuItemCount(menu_);
    while (static_cast<int>(buttons_.size()) > count) { DestroyWindow(buttons_.back()); buttons_.pop_back(); }
    for (int index = 0; index < count; ++index)
    {
        wchar_t text[96]{}; GetMenuStringW(menu_, index, text, 96, MF_BYPOSITION);
        if (index >= static_cast<int>(buttons_.size()))
        {
            HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(index + 1)), g_hInst, nullptr);
            SetPropW(button, L"FFKeyLock.FlatButton", reinterpret_cast<HANDLE>(1));
            SetWindowSubclass(button, ButtonProc, 1, reinterpret_cast<DWORD_PTR>(this)); buttons_.push_back(button);
        }
        else SetWindowTextW(buttons_[index], text);
    }
    RefreshTheme();
}
void MenuBar::RefreshTheme() { ApplyTheme(window_); Layout(); InvalidateSurface(window_); }
void MenuBar::Layout()
{
    int x = DpiUtils::ScaleForWindow(window_, 8);
    for (HWND button : buttons_)
    {
        wchar_t text[96]{}; GetWindowTextW(button, text, 96);
        HDC dc = GetDC(button); auto old = SelectObject(dc, ThemeManager::UiFont());
        RECT r{}; DrawTextW(dc, text, -1, &r, DT_CALCRECT | DT_SINGLELINE); SelectObject(dc, old); ReleaseDC(button, dc);
        const int width = r.right + DpiUtils::ScaleForWindow(window_, 24);
        SetWindowPos(button, nullptr, x, DpiUtils::ScaleForWindow(window_, 3), width, DpiUtils::ScaleForWindow(window_, 30), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        x += width + DpiUtils::ScaleForWindow(window_, 4);
    }
}
void MenuBar::Open(int index)
{
    if (tracking_ || index < 0 || index >= static_cast<int>(buttons_.size())) return;
    const HWND focus = GetFocus();
    if (std::find(buttons_.begin(), buttons_.end(), focus) == buttons_.end()) previousFocus_ = focus;
    tracking_ = true; tracked_ = this;
    HHOOK hook = SetWindowsHookExW(WH_MSGFILTER, Filter, nullptr, GetCurrentThreadId());
    UINT command = 0;
    do
    {
        active_ = index; next_ = -1; activePopup_ = GetSubMenu(menu_, index); selectedMenu_ = activePopup_;
        selectedFlags_ = 0; SetFocus(buttons_[index]); SendMessageW(buttons_[index], BM_SETSTATE, TRUE, 0);
        RECT r{}; GetWindowRect(buttons_[index], &r);
        command = ShowPopupMenu(window_, activePopup_, {r.left, r.bottom});
        SendMessageW(buttons_[index], BM_SETSTATE, FALSE, 0); index = next_;
    } while (!command && index >= 0 && !pendingMenu_);
    if (hook) UnhookWindowsHookEx(hook); tracked_ = nullptr; tracking_ = false; active_ = -1; activePopup_ = nullptr;
    if (pendingMenu_) { HMENU menu = pendingMenu_; pendingMenu_ = nullptr; SetMenu(menu); }
    if (IsWindow(previousFocus_)) SetFocus(previousFocus_);
    if (command) PostMessageW(GetParent(window_), WM_COMMAND, command, 0);
}
void MenuBar::FocusMenu()
{
    const HWND focus = GetFocus();
    if (std::find(buttons_.begin(), buttons_.end(), focus) != buttons_.end())
    { if (IsWindow(previousFocus_)) SetFocus(previousFocus_); }
    else { previousFocus_ = focus; if (!buttons_.empty()) SetFocus(buttons_.front()); }
}
bool MenuBar::Translate(MSG& message)
{
    if (message.message == WM_SYSKEYDOWN && message.wParam == VK_MENU)
    { altPressed_ = true; return true; }
    if (message.message == WM_SYSKEYUP && message.wParam == VK_MENU)
    { if (altPressed_) FocusMenu(); altPressed_ = false; return true; }
    if (message.message == WM_SYSKEYDOWN && message.wParam != VK_MENU) altPressed_ = false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_F10 && !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000))
    { FocusMenu(); return true; }
    if (message.message != WM_SYSCHAR) return false;
    altPressed_ = false;
    for (int i = 0; i < static_cast<int>(buttons_.size()); ++i)
    {
        wchar_t text[96]{}; GetWindowTextW(buttons_[i], text, 96);
        for (int n = 0; text[n] && text[n + 1]; ++n)
            if (text[n] == L'&' && towupper(text[n + 1]) == towupper(static_cast<wchar_t>(message.wParam)))
            { Open(i); return true; }
    }
    return false;
}
LRESULT CALLBACK MenuBar::Filter(int code, WPARAM w, LPARAM l)
{
    if (code == MSGF_MENU && tracked_)
    {
        auto& self = *tracked_; const auto& message = *reinterpret_cast<MSG*>(l);
        int next = -1;
        if (message.message == WM_KEYDOWN && self.selectedMenu_ == self.activePopup_)
        {
            if (message.wParam == VK_LEFT) next = (self.active_ + static_cast<int>(self.buttons_.size()) - 1) % self.buttons_.size();
            if (message.wParam == VK_RIGHT && !(self.selectedFlags_ & MF_POPUP)) next = (self.active_ + 1) % self.buttons_.size();
        }
        if (message.message == WM_MOUSEMOVE)
        {
            POINT point{}; GetCursorPos(&point);
            for (int i = 0; i < static_cast<int>(self.buttons_.size()); ++i)
            { RECT r{}; GetWindowRect(self.buttons_[i], &r); if (PtInRect(&r, point) && i != self.active_) next = i; }
        }
        if (next >= 0) { self.next_ = next; EndMenu(); return TRUE; }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
LRESULT CALLBACK MenuBar::ButtonProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    auto& self = *reinterpret_cast<MenuBar*>(data);
    if (message == WM_GETDLGCODE) return DefSubclassProc(hwnd, message, w, l) | DLGC_WANTARROWS;
    if (message == WM_KEYDOWN)
    {
        const int index = GetDlgCtrlID(hwnd) - 1;
        if (w == VK_DOWN || w == VK_RETURN) { self.Open(index); return 0; }
        if (w == VK_LEFT || w == VK_RIGHT)
        { SetFocus(self.buttons_[(index + (w == VK_LEFT ? static_cast<int>(self.buttons_.size()) - 1 : 1)) % self.buttons_.size()]); return 0; }
        if (w == VK_ESCAPE && IsWindow(self.previousFocus_)) { SetFocus(self.previousFocus_); return 0; }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ButtonProc, id);
    return DefSubclassProc(hwnd, message, w, l);
}
LRESULT CALLBACK MenuBar::Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    auto* self = reinterpret_cast<MenuBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_CREATE)
    { self = static_cast<MenuBar*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window_ = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); return 0; }
    if (!self) return DefWindowProcW(hwnd, message, w, l);
    switch (message)
    {
    case WM_SIZE: self->Layout(); return 0;
    case WM_COMMAND: if (HIWORD(w) == BN_CLICKED) self->Open(LOWORD(w) - 1); return 0;
    case WM_MEASUREITEM: return PopupMenuTheme::Measure(*reinterpret_cast<MEASUREITEMSTRUCT*>(l));
    case WM_DRAWITEM:
        if (!PopupMenuTheme::Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l))) DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
    case WM_MENUCHAR: return PopupMenuTheme::MenuChar(w, reinterpret_cast<HMENU>(l));
    case WM_MENUSELECT: self->selectedMenu_ = reinterpret_cast<HMENU>(l); self->selectedFlags_ = HIWORD(w); return 0;
    case WM_PAINT: case WM_PRINTCLIENT: case WM_ERASEBKGND: return PaintSurface(hwnd, message, w);
    case WM_CTLCOLORBTN: return reinterpret_cast<LRESULT>(HandleCtlColor(hwnd, reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l)));
    case WM_DESTROY: if (self->pendingMenu_) DestroyMenu(self->pendingMenu_); self->pendingMenu_ = nullptr; if (self->menu_) DestroyMenu(self->menu_); self->menu_ = nullptr; self->buttons_.clear(); self->window_ = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
}
