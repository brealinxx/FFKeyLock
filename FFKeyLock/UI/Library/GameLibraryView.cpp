#include "GameLibraryView.h"
#include "../Rendering/NativeControls.h"
#include "../Rendering/Surface.h"
#include "../../AppState.h"
#include "../../ThemeManager.h"
#include "../../Platform/DpiUtils.h"
#include "../../Platform/GdiUtils.h"
#include "../../Resource.h"
#include <algorithm>
#include <unordered_set>
#include <shellapi.h>
#include <windowsx.h>
namespace FFKeyLock::UI
{
HWND GameLibraryView::Create(HWND parent, int id)
{
    id_ = id;
    WNDCLASSW wc{}; wc.hInstance = g_hInst; wc.lpfnWndProc = Proc;
    wc.lpszClassName = L"FFKeyLockGameLibrary"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    return CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, this);
}
void GameLibraryView::SetItems(std::vector<LibraryItem> items)
{
    const int top = static_cast<int>(SendMessageW(list_, LB_GETTOPINDEX, 0, 0));
    std::unordered_set<std::wstring> paths;
    for (const auto& item : items)
    {
        paths.insert(item.iconPath);
        if (icons_.contains(item.iconPath)) continue;
        SHFILEINFOW info{};
        if (item.iconPath.rfind(L"\\\\", 0) != 0)
            SHGetFileInfoW(item.iconPath.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_ICON |
                (GetFileAttributesW(item.iconPath.c_str()) == INVALID_FILE_ATTRIBUTES ? SHGFI_USEFILEATTRIBUTES : 0));
        icons_[item.iconPath] = info.hIcon ? info.hIcon : CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    }
    // Retain icons while filtering; bound the cache to the maximum library size.
    if (icons_.size() > 4096)
        for (auto it = icons_.begin(); it != icons_.end();)
            if (!paths.contains(it->first)) { if (it->second) DestroyIcon(it->second); it = icons_.erase(it); } else ++it;
    updating_ = true;
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0); items_ = std::move(items);
    for (const auto& item : items_)
    {
        // Native listbox strings are also its accessible item names.
        const auto label = item.title + L" — " + item.detail;
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(list_, LB_SETTOPINDEX, std::max(0, top), 0);
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    updating_ = false; SyncScroll(); InvalidateSurface(window_);
}
void GameLibraryView::Select(int index)
{
    SendMessageW(list_, LB_SETCURSEL, index, 0); SyncScroll(); InvalidateSurface(list_);
}
int GameLibraryView::Selected() const { return static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0)); }
void GameLibraryView::RefreshTheme()
{
    ApplyTheme(window_); rowHeight_ = DpiUtils::ScaleForWindow(window_, 56);
    SendMessageW(list_, LB_SETITEMHEIGHT, 0, rowHeight_); Layout(); InvalidateSurface(window_);
}
void GameLibraryView::Layout()
{
    if (!list_) return;
    const RECT r = PanelInterior(window_);
    const int gutter = DpiUtils::ScaleForWindow(window_, ThemeManager::ScrollBarWidth);
    const int width = std::max(1, static_cast<int>(r.right - r.left) - gutter);
    SetWindowPos(list_, nullptr, r.left, r.top, width, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);
    SetWindowPos(bar_, nullptr, r.left + width, r.top, std::max(0L, r.right - r.left - width), r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    SyncScroll(); InvalidateSurface(window_);
}
void GameLibraryView::SyncScroll()
{
    if (updating_ || !bar_) return;
    RECT r{}; GetClientRect(list_, &r);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    info.nMax = std::max(0, static_cast<int>(items_.size()) - 1);
    info.nPage = std::max(1, static_cast<int>(r.bottom) / rowHeight_);
    info.nPos = static_cast<int>(SendMessageW(list_, LB_GETTOPINDEX, 0, 0));
    SetScrollInfo(bar_, SB_CTL, &info, TRUE);
}
void GameLibraryView::ScrollTo(int row)
{
    SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(bar_, SB_CTL, &info);
    row = std::clamp(row, 0, std::max(0, info.nMax - static_cast<int>(info.nPage) + 1));
    SendMessageW(list_, LB_SETTOPINDEX, row, 0); SyncScroll(); InvalidateSurface(list_);
}
void GameLibraryView::Draw(const DRAWITEMSTRUCT& item)
{
    GdiUtils::BufferedPaint buffer(item.hDC, item.rcItem); HDC dc = buffer.Dc();
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    HBRUSH brush = CreateSolidBrush(selected ? ThemeManager::SelectionColor() : BackgroundColor(list_));
    FillRect(dc, &item.rcItem, brush); DeleteObject(brush);
    if (item.itemID < items_.size())
    {
        auto s = [this](int n) { return DpiUtils::ScaleForWindow(window_, n); };
        const auto& data = items_[item.itemID];
        const int iconSize = s(28);
        const auto icon = icons_.find(data.iconPath);
        if (icon != icons_.end() && icon->second)
            DrawIconEx(dc, item.rcItem.left + s(8), item.rcItem.top + s(14), icon->second, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        RECT title = item.rcItem; title.left += s(44); title.right -= s(8); title.top += s(6); title.bottom = title.top + s(22);
        SetBkMode(dc, TRANSPARENT);
        const COLORREF text = selected && ThemeManager::HighContrast() ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ThemeManager::TextColor();
        SetTextColor(dc, text);
        GdiUtils::SelectObjectScope font(dc, reinterpret_cast<HFONT>(SendMessageW(list_, WM_GETFONT, 0, 0)));
        DrawTextW(dc, data.title.c_str(), -1, &title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        title.top += s(22); title.bottom += s(22);
        SetTextColor(dc, selected ? text : ThemeManager::MutedTextColor());
        DrawTextW(dc, data.detail.c_str(), -1, &title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT))
    { RECT r = item.rcItem; InflateRect(&r, -2, -2); DrawFocusRect(dc, &r); }
    buffer.Present();
}
LRESULT CALLBACK GameLibraryView::ListProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    auto& self = *reinterpret_cast<GameLibraryView*>(data);
    if (message == WM_MOUSEWHEEL) return SendMessageW(self.window_, message, w, l);
    if (message == WM_CONTEXTMENU) return SendMessageW(GetParent(self.window_), message, reinterpret_cast<WPARAM>(self.window_), l);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ListProc, id);
    const LRESULT result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_KEYDOWN || message == WM_LBUTTONDOWN || message == WM_SIZE || message == LB_SETCURSEL)
    { self.SyncScroll(); InvalidateRect(hwnd, nullptr, FALSE); }
    return result;
}
LRESULT CALLBACK GameLibraryView::Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    auto* self = reinterpret_cast<GameLibraryView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_CREATE)
    {
        self = static_cast<GameLibraryView*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->window_ = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        MarkPanel(hwnd);
        self->list_ = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1), g_hInst, nullptr);
        self->bar_ = CreateWindowExW(0, L"SCROLLBAR", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | SBS_VERT,
            0, 0, 0, 0, hwnd, nullptr, g_hInst, nullptr);
        SetWindowSubclass(self->list_, ListProc, 1, reinterpret_cast<DWORD_PTR>(self)); self->RefreshTheme(); return 0;
    }
    if (!self) return DefWindowProcW(hwnd, message, w, l);
    switch (message)
    {
    case WM_SIZE: self->Layout(); return 0;
    case WM_SETFOCUS: SetFocus(self->list_); return 0;
    case WM_DRAWITEM: self->Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
    case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight = DpiUtils::ScaleForWindow(hwnd, 56); return TRUE;
    case WM_COMMAND:
        if (!self->updating_) SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(self->id_, HIWORD(w)), reinterpret_cast<LPARAM>(hwnd));
        return 0;
    case WM_MOUSEWHEEL:
    {
        self->wheel_ += GET_WHEEL_DELTA_WPARAM(w); const int steps = self->wheel_ / WHEEL_DELTA; self->wheel_ %= WHEEL_DELTA;
        UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        RECT r{}; GetClientRect(self->list_, &r);
        const int distance = lines == WHEEL_PAGESCROLL ? std::max(1, static_cast<int>(r.bottom) / self->rowHeight_) : static_cast<int>(std::min(lines, 100u));
        self->ScrollTo(static_cast<int>(SendMessageW(self->list_, LB_GETTOPINDEX, 0, 0)) - steps * distance); return 0;
    }
    case WM_VSCROLL:
    {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(self->bar_, SB_CTL, &info); int next = info.nPos;
        switch (LOWORD(w)) {
        case SB_LINEUP: --next; break; case SB_LINEDOWN: ++next; break;
        case SB_PAGEUP: next -= info.nPage; break; case SB_PAGEDOWN: next += info.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: next = info.nTrackPos; break;
        case SB_TOP: next = 0; break; case SB_BOTTOM: next = info.nMax; break;
        }
        self->ScrollTo(next); return 0;
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLORSCROLLBAR:
        return reinterpret_cast<LRESULT>(HandleCtlColor(hwnd, reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l)));
    case WM_PAINT: case WM_PRINTCLIENT: case WM_ERASEBKGND: return PaintSurface(hwnd, message, w);
    case WM_DESTROY:
        for (const auto& [path, icon] : self->icons_) if (icon) DestroyIcon(icon);
        self->icons_.clear(); self->window_ = self->list_ = self->bar_ = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
}
