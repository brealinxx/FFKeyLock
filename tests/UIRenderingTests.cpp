#include "../FFKeyLock/AppState.h"
#include "../FFKeyLock/GameProtection.h"
#include "../FFKeyLock/ThemeManager.h"
#include "../FFKeyLock/UI/Main/MainContentView.h"
#include "../FFKeyLock/UI/Menu/MenuBar.h"
#include "../FFKeyLock/UI/Menu/AppMenus.h"
#include "../FFKeyLock/UI/Menu/PopupMenu.h"
#include "../FFKeyLock/UI/Profiles/ProfileEditor.h"
#include "../FFKeyLock/UI/Rendering/Surface.h"
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <numeric>
using namespace FFKeyLock;
namespace
{
int checks = 0;
void Check(bool okay, const char* reason) { ++checks; if (!okay) throw std::runtime_error(reason); }
// Keep incidental hover out of screenshot comparisons. Nested drag tests
// return here; restore the user's location when the harness finishes.
class TestCursor
{
public:
    explicit TestCursor(POINT neutral) : neutral_(neutral)
    { GetCursorPos(&saved_); SetCursorPos(neutral.x, neutral.y); }
    ~TestCursor()
    {
        POINT current{}; GetCursorPos(&current);
        if (current.x == neutral_.x && current.y == neutral_.y) SetCursorPos(saved_.x, saved_.y);
    }
private:
    POINT saved_{}, neutral_{};
};
// Model button state in this thread and keep the real cursor coordinates in
// sync: native tracking reads the cursor again on release. No button input is
// injected globally; restore the cursor unless the user moved it independently.
class ThreadMousePress
{
public:
    explicit ThreadMousePress(POINT start)
    {
        GetCursorPos(&savedCursor_); Move(start);
        GetKeyboardState(saved_); held_ = true;
        hook_ = SetWindowsHookExW(WH_CALLWNDPROC, Observe, nullptr, GetCurrentThreadId());
        if (!hook_) { held_ = false; SetCursorPos(savedCursor_.x, savedCursor_.y); throw std::runtime_error("Cannot observe test-thread mouse messages"); }
        Update();
    }
    ~ThreadMousePress()
    {
        UnhookWindowsHookEx(hook_); held_ = false; SetKeyboardState(saved_);
        POINT current{}; GetCursorPos(&current);
        if (current.x == expectedCursor_.x && current.y == expectedCursor_.y) SetCursorPos(savedCursor_.x, savedCursor_.y);
    }
    static void Move(POINT point) { expectedCursor_ = point; SetCursorPos(point.x, point.y); }
private:
    BYTE saved_[256]{};
    POINT savedCursor_{};
    static inline POINT expectedCursor_{};
    HHOOK hook_ = nullptr;
    static inline bool held_ = false;
    static void Update()
    {
        BYTE keys[256]{}; GetKeyboardState(keys);
        keys[VK_LBUTTON] = (keys[VK_LBUTTON] & 0x7f) | (held_ ? 0x80 : 0);
        SetKeyboardState(keys);
    }
    static LRESULT CALLBACK Observe(int code, WPARAM w, LPARAM l)
    {
        if (code >= 0)
        {
            const auto message = reinterpret_cast<CWPSTRUCT*>(l)->message;
            if (message == WM_LBUTTONUP) { held_ = false; Update(); }
            else if (held_) Update();
        }
        return CallNextHookEx(nullptr, code, w, l);
    }
};
struct Frame
{
    int width = 0, height = 0;
    std::vector<DWORD> pixels;
    COLORREF Pixel(int x, int y) const
    { const DWORD c = pixels.at(y * width + x); return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }
};
Frame Capture(HWND hwnd, bool includeFrame = false)
{
    RECT r{}; if (includeFrame) { GetWindowRect(hwnd, &r); OffsetRect(&r, -r.left, -r.top); } else GetClientRect(hwnd, &r); Frame frame{static_cast<int>(r.right), static_cast<int>(r.bottom)};
    HDC source = includeFrame ? GetWindowDC(hwnd) : GetDC(hwnd), dc = CreateCompatibleDC(source);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = r.right;
    info.bmiHeader.biHeight = -r.bottom; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* data = nullptr; HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    const auto old = SelectObject(dc, bitmap); BitBlt(dc, 0, 0, r.right, r.bottom, source, 0, 0, SRCCOPY); GdiFlush();
    frame.pixels.assign(static_cast<DWORD*>(data), static_cast<DWORD*>(data) + r.right * r.bottom);
    for (auto& pixel : frame.pixels) pixel &= 0xffffff;
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(hwnd, source); return frame;
}
void Save(const Frame& frame, const std::filesystem::path& path)
{
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(frame.pixels.size() * sizeof(DWORD));
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = frame.width; info.biHeight = -frame.height; info.biPlanes = 1; info.biBitCount = 32;
    std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<char*>(&header), sizeof(header));
    file.write(reinterpret_cast<char*>(&info), sizeof(info)); file.write(reinterpret_cast<const char*>(frame.pixels.data()), frame.pixels.size() * sizeof(DWORD));
}
HWND DropdownBar(HWND list)
{
    const HWND child = FindWindowExW(list, nullptr, L"SCROLLBAR", nullptr);
    return child && IsWindowVisible(child) ? child : list;
}
bool DropdownGeometry(HWND list, SCROLLBARINFO& geometry)
{
    const HWND bar = DropdownBar(list);
    return GetScrollBarInfo(bar, bar == list ? OBJID_VSCROLL : OBJID_CLIENT, &geometry) != FALSE;
}
void RepaintDropdownFrame(HWND list)
{
    SendMessageW(list, WM_NCPAINT, 1, 0);
    const HWND bar = DropdownBar(list);
    if (bar != list) RedrawWindow(bar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}
struct Harness { MainContentView view; UI::MenuBar menu; HWND window = nullptr; int popupChecks = 0; bool popupDark = false; bool replaceMenu = false; HWND dragBar = nullptr; int dragStep = 0; LPARAM dragPoint = 0; bool dragThemed = true; bool dragCaptured = false; bool dragContrast = true; HWND dropdownDrag = nullptr; int dropdownStep = 0; int dropdownTicks = 2; LPARAM dropdownPoint = 0; bool dropdownThemed = true; bool dropdownCaptured = false; HMENU arrowMenu = nullptr; std::vector<int> arrowGroups; bool arrowHighlighted = false; std::filesystem::path directory; };
void Pump(Harness& state)
{
    // Mouse enter/leave messages after moving children arrive asynchronously.
    // Compare settled frames, not two different native hover states.
    for (int frame = 0; frame < 3; ++frame)
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) throw std::runtime_error("UI test window closed");
            if (!state.menu.Translate(msg) && !IsDialogMessageW(state.window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) throw std::runtime_error("UI verification stopped with Escape");
        GdiFlush(); DwmFlush();
        if (frame < 2) MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
LRESULT CALLBACK HostProc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    auto* state = reinterpret_cast<Harness*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_CREATE)
    {
        state = static_cast<Harness*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        state->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->menu.Create(hwnd); state->view.Create(hwnd); state->menu.SetMenu(UI::CreateAppMenu()); return 0;
    }
    if (!state) return DefWindowProcW(hwnd, message, w, l);
    switch (message)
    {
    case WM_SIZE:
    {
        const int bar = MulDiv(36, GetDpiForWindow(hwnd), 96);
        SetWindowPos(state->menu.Window(), nullptr, 0, 0, LOWORD(l), bar, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->view.Window(), nullptr, 0, bar, LOWORD(l), std::max(1, static_cast<int>(HIWORD(l)) - bar), SWP_NOZORDER | SWP_NOACTIVATE); return 0;
    }
    case WM_TIMER:
    {
        if (w == 3 && state->dropdownDrag)
        {
            HWND list = state->dropdownDrag;
            const auto before = Capture(list, true);
            const HWND capture = GetCapture();
            state->dropdownCaptured |= capture == list || IsChild(list, capture);
            RepaintDropdownFrame(list);
            const auto after = Capture(list, true);
            state->dropdownThemed &= before.pixels == after.pixels;
            if (before.pixels != after.pixels)
            { Save(before, state->directory / L"dropdown-drag-native.bmp"); Save(after, state->directory / L"dropdown-drag-themed.bmp"); }
            POINT point{static_cast<short>(LOWORD(state->dropdownPoint)), static_cast<short>(HIWORD(state->dropdownPoint))};
            ClientToScreen(list, &point);
            ThreadMousePress::Move(point);
            if (capture) ScreenToClient(capture, &point);
            const LPARAM location = MAKELPARAM(point.x, point.y);
            ++state->dropdownStep;
            if (state->dropdownStep == 1)
                PostMessageW(capture ? capture : list, WM_MOUSEMOVE, MK_LBUTTON, location);
            if (state->dropdownStep == state->dropdownTicks)
            { PostMessageW(capture ? capture : list, WM_LBUTTONUP, 0, location); KillTimer(hwnd, 3); }
            return 0;
        }
        if (w == 2 && state->dragBar)
        {
            const auto frame = Capture(state->dragBar);
            state->dragCaptured |= GetCapture() == state->dragBar;
            SCROLLBARINFO geometry{sizeof(geometry)}; GetScrollBarInfo(state->dragBar, OBJID_CLIENT, &geometry);
            state->dragContrast &= frame.Pixel(frame.width / 2, (geometry.xyThumbTop + geometry.xyThumbBottom) / 2) ==
                ThemeManager::ScrollThumbColor(false, true);
            RedrawWindow(state->dragBar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            const auto themed = Capture(state->dragBar);
            state->dragThemed &= frame.pixels == themed.pixels;
            if (frame.pixels != themed.pixels)
            {
                Save(frame, state->directory / L"drag-transient.bmp");
                Save(themed, state->directory / L"drag-repaint.bmp");
            }
            POINT cursor{static_cast<short>(LOWORD(state->dragPoint)), static_cast<short>(HIWORD(state->dragPoint))};
            ClientToScreen(state->dragBar, &cursor); ThreadMousePress::Move(cursor);
            if (state->dragStep++ == 0) PostMessageW(state->dragBar, WM_MOUSEMOVE, MK_LBUTTON, state->dragPoint);
            else
            {
                PostMessageW(state->dragBar, WM_LBUTTONUP, 0, state->dragPoint);
                KillTimer(hwnd, 2);
            }
            return 0;
        }
        // TrackPopupMenu's native modal loop dispatches this timer. Inspect only
        // menu windows owned by this test process, then close through EndMenu.
        HWND popup = nullptr;
        while ((popup = FindWindowExW(nullptr, popup, L"#32768", nullptr)) != nullptr)
        {
            DWORD pid = 0; GetWindowThreadProcessId(popup, &pid);
            if (pid != GetCurrentProcessId()) continue;
            const auto frame = Capture(popup);
            if (frame.width > 20 && frame.height > 20)
            {
                const auto color = frame.Pixel(frame.width / 2, frame.height - 8);
                state->popupDark = GetRValue(color) < 100 && GetGValue(color) < 100 && GetBValue(color) < 100;
                ++state->popupChecks; Save(frame, state->directory / L"popup-menu.bmp");
                if (state->arrowMenu)
                {
                    Save(frame, state->directory / ((ThemeManager::IsDark() ? std::wstring(L"submenu-dark") : std::wstring(L"submenu-light")) + (state->arrowGroups.empty() ? L".bmp" : L"-highlight.bmp")));
                    for (int row = 0; row < GetMenuItemCount(state->arrowMenu); ++row)
                    {
                        RECT r{}; GetMenuItemRect(hwnd, state->arrowMenu, row, &r);
                        MapWindowPoints(nullptr, popup, reinterpret_cast<POINT*>(&r), 2);
                        const int center = (r.top + r.bottom) / 2;
                        const auto background = frame.Pixel(r.left + 2, center);
                        if (row == 1 && state->arrowGroups.size() >= 3)
                            state->arrowHighlighted = background == ThemeManager::MenuHoverColor();
                        const int dpi = GetDpiForWindow(popup);
                        int groups = 0; bool previous = false;
                        for (int x = std::max(0L, r.right - MulDiv(40, dpi, 96)); x < std::min(frame.width - 2, static_cast<int>(r.right) - 2); ++x)
                        {
                            bool ink = false;
                            for (int y = center - MulDiv(8, dpi, 96); y <= center + MulDiv(8, dpi, 96); ++y)
                                ink |= frame.Pixel(x, y) != background;
                            if (ink && !previous) ++groups;
                            previous = ink;
                        }
                        state->arrowGroups.push_back(groups);
                    }
                }
            }
        }
        if (state->arrowMenu && state->arrowGroups.size() == 3)
        {
            // Drive the native menu loop, not just the menu item's highlight
            // flag (which does not change the loop's current selection).
            PostMessageW(hwnd, WM_KEYDOWN, VK_DOWN, 0);
            PostMessageW(hwnd, WM_KEYDOWN, VK_DOWN, 0);
            return 0;
        }
        KillTimer(hwnd, 1);
        if (state->replaceMenu) state->menu.SetMenu(UI::CreateAppMenu());
        else EndMenu();
        return 0;
    }
    case WM_MEASUREITEM: return UI::PopupMenuTheme::Measure(*reinterpret_cast<MEASUREITEMSTRUCT*>(l));
    case WM_DRAWITEM: return UI::PopupMenuTheme::Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l));
    case WM_MENUCHAR: return UI::PopupMenuTheme::MenuChar(w, reinterpret_cast<HMENU>(l));
    case WM_PAINT: case WM_PRINTCLIENT: case WM_ERASEBKGND: return UI::PaintSurface(hwnd, message, w);
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
void EqualToFullRepaint(Harness& state, const std::filesystem::path& directory, const std::wstring& name)
{
    Pump(state); const auto incremental = Capture(state.window);
    UI::InvalidateSurface(state.window, true); Pump(state); const auto full = Capture(state.window);
    if (incremental.pixels != full.pixels)
    {
        Save(incremental, directory / (name + L"-incremental.bmp")); Save(full, directory / (name + L"-full.bmp"));
        const auto count = std::inner_product(incremental.pixels.begin(), incremental.pixels.end(), full.pixels.begin(), size_t{0}, std::plus<>(), std::not_equal_to<>());
        std::cerr << "Pixel differences: " << count << '\n';
        Check(false, "Incremental paint differs from full repaint (see frame artifacts)");
    }
    Check(true, "Paint equality");
}
void CheckMenuArrows(Harness& state)
{
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < 3; ++i)
    {
        HMENU submenu = CreatePopupMenu(); AppendMenuW(submenu, MF_STRING, 9000 + i, L"Child command");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), L"Submenu");
    }
    state.arrowMenu = menu; state.arrowGroups.clear(); state.arrowHighlighted = false;
    RECT r{}; GetWindowRect(state.window, &r);
    SetTimer(state.window, 1, 300, nullptr); UI::ShowPopupMenu(state.window, menu, {r.left + 60, r.top + 100});
    state.arrowMenu = nullptr; DestroyMenu(menu);
    std::cout << "Submenu arrow groups:"; for (int groups : state.arrowGroups) std::cout << ' ' << groups; std::cout << '\n';
    Check(state.arrowHighlighted, "Submenu highlight was actually rendered");
    Check(state.arrowGroups.size() == 6 && std::all_of(state.arrowGroups.begin(), state.arrowGroups.end(), [](int groups) { return groups == 1; }),
        "Each submenu row has exactly one arrow");
}
void CheckScrollbarTransient(Harness& state, HWND bar, const std::wstring& name)
{
    Pump(state); RECT r{}; GetClientRect(bar, &r);
    for (int step = 0; step < 5; ++step)
    {
        // Intentionally capture before pumping WM_PAINT: a deferred themed
        // repaint must not conceal a synchronous native white/hover frame.
        if (step < 2) SendMessageW(bar, WM_MOUSEMOVE, 0, MAKELPARAM(r.right / 2, step ? r.bottom / 2 : 8));
        else if (step == 2) SendMessageW(bar, WM_MOUSELEAVE, 0, 0);
        else if (step == 3) SetScrollPos(bar, SB_CTL, GetScrollPos(bar, SB_CTL) + 1, TRUE);
        else { SCROLLINFO info{sizeof(info), SIF_POS}; GetScrollInfo(bar, SB_CTL, &info); ++info.nPos; SetScrollInfo(bar, SB_CTL, &info, TRUE); }
        const auto immediate = Capture(bar);
        RedrawWindow(bar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); const auto repainted = Capture(bar);
        if (immediate.pixels != repainted.pixels)
        { Save(immediate, state.directory / (name + L"-transient.bmp")); Save(repainted, state.directory / (name + L"-repaint.bmp")); }
        Check(immediate.pixels == repainted.pixels, "Scrollbar is themed immediately during hover/position update");
    }
}

void CheckPanel(HWND panel)
{
    const auto frame = Capture(panel);
    for (auto point : {POINT{0, 0}, POINT{frame.width - 1, 0}, POINT{0, frame.height - 1}, POINT{frame.width - 1, frame.height - 1}})
        Check(frame.Pixel(point.x, point.y) == ThemeManager::WindowColor(), "Panel corner belongs to the page background");
    Check(frame.Pixel(frame.width / 2, 0) == ThemeManager::PanelBorderColor(), "Panel has a subtle top border");
    Check(frame.Pixel(frame.width / 2, frame.height - 3) == ThemeManager::SurfaceColor(), "Panel bottom padding has the surface color");
    const auto inner = UI::PanelInterior(panel);
    Check(frame.Pixel(frame.width - 3, inner.bottom - 1) == ThemeManager::SurfaceColor(), "Scrollbar bottom gutter has no white corner");
}
void CheckScrollbarInteraction(Harness& state, HWND bar)
{
    HWND panel = GetParent(bar);
    SendMessageW(panel, WM_VSCROLL, SB_TOP, reinterpret_cast<LPARAM>(bar));
    SetFocus(nullptr);
    SendMessageW(bar, WM_MOUSELEAVE, 0, 0); Pump(state);
    SCROLLBARINFO geometry{sizeof(geometry)};
    Check(GetScrollBarInfo(bar, OBJID_CLIENT, &geometry) && geometry.xyThumbBottom > geometry.xyThumbTop,
        "Native scrollbar exposes nonempty thumb geometry");
    RECT r{}; GetClientRect(bar, &r);
    const int x = r.right / 2, y = (geometry.xyThumbTop + geometry.xyThumbBottom) / 2;
    const auto normal = Capture(bar);
    Check(normal.Pixel(x, y) == ThemeManager::ScrollThumbColor(false, false), "Scrollbar idle thumb uses the neutral theme color");
    Check(normal.Pixel(0, y) == ThemeManager::SurfaceColor(), "Scrollbar track matches its panel");
    SendMessageW(bar, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
    const auto hot = Capture(bar);
    Check(hot.Pixel(x, y) == ThemeManager::ScrollThumbColor(true, false) && hot.Pixel(x, y) != normal.Pixel(x, y), "Scrollbar hover increases contrast immediately");
    // Focus changes can reset this thread's simulated mouse-button state.
    // Complete activation/focus before setting it for the native press.
    SetActiveWindow(state.window); SetFocus(bar); Pump(state);
    Check(GetFocus() == bar, "Scrollbar is focused before simulating its native mouse press");
    state.dragBar = bar; state.dragStep = 0; state.dragThemed = true; state.dragCaptured = false; state.dragContrast = true;
    state.dragPoint = MAKELPARAM(x, std::min(static_cast<int>(r.bottom) - geometry.dxyLineButton - 2, y + 70));
    SetTimer(state.window, 2, 80, nullptr);
    {
        POINT cursor{x, y}; ClientToScreen(bar, &cursor);
        ThreadMousePress buttonState(cursor);
        SendMessageW(bar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        const auto deadline = GetTickCount64() + 2000;
        while (state.dragStep < 2 && GetTickCount64() < deadline) Pump(state);
        Pump(state);
    }
    KillTimer(state.window, 2); state.dragBar = nullptr;
    // Cursor restoration leaves a delayed native TME_LEAVE notification.
    // Settle that known test hover before comparing unrelated scroll frames.
    TRACKMOUSEEVENT leave{sizeof(leave), TME_CANCEL | TME_LEAVE, bar, 0}; TrackMouseEvent(&leave);
    SendMessageW(bar, WM_MOUSELEAVE, 0, 0);
    if (!state.dragCaptured || state.dragStep != 2)
        std::cerr << "Scrollbar capture: samples=" << state.dragStep << " captured=" << state.dragCaptured << " focus=" << GetFocus() << " capture=" << GetCapture() << '\n';
    Check(state.dragCaptured && state.dragStep == 2, "Native thumb drag captured and released the pointer");
    Check(GetScrollPos(bar, SB_CTL) > 0, "Native thumb drag actually scrolls the content");
    Check(state.dragThemed, "Scrollbar keeps themed pixels inside the native drag loop");
    Check(state.dragContrast, "Captured thumb uses the higher contrast pressed color");
    Check(GetCapture() != bar, "Scrollbar releases capture after dragging");
    SetFocus(bar);
    SendMessageW(bar, WM_KEYDOWN, VK_HOME, 0); SendMessageW(bar, WM_KEYUP, VK_HOME, 0);
    Check(GetScrollPos(bar, SB_CTL) == 0, "Home scrolls to the top");
    SendMessageW(bar, WM_KEYDOWN, VK_NEXT, 0); SendMessageW(bar, WM_KEYUP, VK_NEXT, 0);
    Check(GetScrollPos(bar, SB_CTL) > 0, "Page Down scrolls through the native control");
    SendMessageW(bar, WM_KEYDOWN, VK_END, 0); SendMessageW(bar, WM_KEYUP, VK_END, 0);
    SCROLLINFO range{sizeof(range), SIF_ALL}; GetScrollInfo(bar, SB_CTL, &range);
    Check(range.nPos == range.nMax - static_cast<int>(range.nPage) + 1, "End scrolls to the bottom");
    SetFocus(nullptr);
    SendMessageW(panel, WM_VSCROLL, SB_TOP, reinterpret_cast<LPARAM>(bar));
    Pump(state); CheckPanel(panel);
}

void CheckDropdownScrolling(Harness& state, HWND combo)
{
    SetActiveWindow(state.window); SetFocus(combo);
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0); Pump(state);
    COMBOBOXINFO keyboard{sizeof(keyboard)}; GetComboBoxInfo(combo, &keyboard);
    SendMessageW(combo, WM_KEYDOWN, VK_END, 0); SendMessageW(combo, WM_KEYUP, VK_END, 0);
    const auto keyboardTop = SendMessageW(keyboard.hwndList, LB_GETTOPINDEX, 0, 0);
    Check(keyboardTop > 0 && GetScrollPos(DropdownBar(keyboard.hwndList), SB_CTL) == keyboardTop,
        "Dropdown keyboard navigation synchronizes the themed scrollbar");
    const auto keyFrame = Capture(keyboard.hwndList, true); RepaintDropdownFrame(keyboard.hwndList);
    Check(keyFrame.pixels == Capture(keyboard.hwndList, true).pixels, "Dropdown keyboard scrolling is immediately themed");
    SendMessageW(combo, WM_KEYDOWN, VK_ESCAPE, 0); SendMessageW(combo, WM_KEYUP, VK_ESCAPE, 0); Pump(state);
    Check(!IsWindowVisible(keyboard.hwndList) && GetCapture() != keyboard.hwndList,
        "Escape closes the keyboard-scrolled dropdown and releases capture");
    for (int action = 0; action < 3; ++action)
    {
        SetActiveWindow(state.window); SetFocus(combo); SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0); Pump(state);
        COMBOBOXINFO info{sizeof(info)}; GetComboBoxInfo(combo, &info);
        HWND list = info.hwndList;
        SendMessageW(list, LB_SETTOPINDEX, 0, 0); Pump(state);
        SCROLLBARINFO geometry{sizeof(geometry)};
        Check(DropdownGeometry(list, geometry) && geometry.xyThumbBottom > geometry.xyThumbTop,
            "Dropdown exposes accessible scrollbar geometry");
        const POINT start{(geometry.rcScrollBar.left + geometry.rcScrollBar.right) / 2,
            action == 1 ? geometry.rcScrollBar.bottom - geometry.dxyLineButton / 2 : geometry.rcScrollBar.top +
            (action == 2 ? geometry.xyThumbBottom + 12 : (geometry.xyThumbTop + geometry.xyThumbBottom) / 2)};
        POINT move{start.x, start.y + (action == 0 ? 60 : 0)}; ScreenToClient(list, &move);
        state.dropdownDrag = list; state.dropdownStep = 0; state.dropdownThemed = true; state.dropdownCaptured = false;
        state.dropdownTicks = action == 1 ? 12 : 2;
        state.dropdownPoint = MAKELPARAM(move.x, move.y);
        SetTimer(state.window, 3, 80, nullptr);
        {
            ThreadMousePress buttonState(start);
            POINT click = start; ScreenToClient(list, &click);
            // An open ComboLBox has capture: real gutter clicks arrive as
            // client mouse messages and must be forwarded to its scrollbar.
            SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(click.x, click.y));
            const auto deadline = GetTickCount64() + 3000;
            while (state.dropdownStep < state.dropdownTicks && GetTickCount64() < deadline) Pump(state);
        }
        Pump(state);
        KillTimer(state.window, 3); state.dropdownDrag = nullptr;
        Check(state.dropdownCaptured && state.dropdownStep == state.dropdownTicks, "Dropdown scrollbar retains native capture while held");
        const auto top = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
        Check(top > 0, "Dropdown thumb, held arrow and page track actually scroll items");
        Check(state.dropdownThemed, "Dropdown stays themed throughout scrollbar tracking");
        Check(IsWindowVisible(list) && GetCapture() == list, "Scrollbar release returns capture to the open dropdown");
        const auto frame = Capture(list, true);
        Save(frame, state.directory / ((ThemeManager::IsDark() ? std::wstring(L"dropdown-dark-") : std::wstring(L"dropdown-light-")) + std::to_wstring(action) + L".bmp"));
        SendMessageW(list, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        const auto wheel = Capture(list, true); RepaintDropdownFrame(list);
        Check(wheel.pixels == Capture(list, true).pixels, "Dropdown wheel scrolling has no native scrollbar frame");
        // Select a visible item after using the scrollbar. Native list input
        // must still dismiss the popup and update the closed combo field.
        const auto selected = SendMessageW(list, LB_GETTOPINDEX, 0, 0) + 1;
        const int row = static_cast<int>(SendMessageW(list, LB_GETITEMHEIGHT, 0, 0));
        SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, row + row / 2));
        SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(12, row + row / 2)); Pump(state);
        Check(SendMessageW(combo, CB_GETCURSEL, 0, 0) == selected && !SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0),
            "Mouse selection after scrolling commits the item and closes the dropdown");
        Check(GetCapture() != list && !IsChild(list, GetCapture()), "Closing the dropdown releases its capture");
    }
}

}
int wmain(int argc, wchar_t** argv)
{
    Harness state;
    try
    {
        Check(argc == 2, "Supply an isolated artifact directory");
        const auto directory = std::filesystem::absolute(argv[1]) / (L"ui-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory); state.directory = directory;
        std::cout << "UI artifacts: " << directory.string() << std::endl;
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES}; InitCommonControlsEx(&common);
        g_hInst = GetModuleHandleW(nullptr); g_notificationsEnabled = g_overlayNotificationsEnabled = false;
        g_portableMode = true; g_configPath = (directory / L"unused.ini").wstring();
        for (int i = 0; i < 80; ++i)
        {
            const auto name = L"sample-game-" + std::to_wstring(i) + L".exe";
            auto profile = NewGameProfile(); ApplyCatPreset(profile); profile.inputGuardEnabled = false;
            g_gameExeNames.push_back(name); g_gameProfiles[name] = profile;
        }
        g_themePreference = ThemePreference::Dark; ThemeManager::Initialize(GetDpiForSystem());
        WNDCLASSW wc{}; wc.lpfnWndProc = HostProc; wc.hInstance = g_hInst; wc.lpszClassName = L"FFKeyLockRenderingVerification";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int dpi = GetDpiForSystem(); auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
        const int width = std::min(scale(1060), static_cast<int>(work.right - work.left) - 40);
        const int height = std::min(scale(780), static_cast<int>(work.bottom - work.top) - 40);
        HWND host = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"FFKeyLock UI verification — Esc stops",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left + 20, work.top + 20, width, height, nullptr, nullptr, g_hInst, &state);
        Check(host != nullptr, "Create test window"); ThemeManager::ApplyDarkTitleBar(host);
        ShowWindow(host, SW_SHOWNOACTIVATE); SetWindowPos(host, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RECT hostBounds{}; GetWindowRect(host, &hostBounds);
        TestCursor cursor({(hostBounds.left + hostBounds.right) / 2, hostBounds.top + 8});
        UI::InvalidateSurface(host, true); Pump(state);
        HWND editor = FindWindowExW(state.view.Window(), nullptr, L"FFKeyLockProfileEditor", nullptr);
        HWND library = GetDlgItem(state.view.Window(), 4103);
        HWND list = FindWindowExW(library, nullptr, L"LISTBOX", nullptr);
        HWND content = FindWindowExW(FindWindowExW(editor, nullptr, L"FFKeyLockScrollViewport", nullptr), nullptr, L"FFKeyLockScrollContent", nullptr);
        Check(editor && library && list && content, "Find live editor/library controls");
        Check(GetMenu(host) == nullptr, "No system non-client menu background");
        Save(Capture(host), directory / L"dark-start.bmp");
        EqualToFullRepaint(state, directory, L"initial");
        CheckMenuArrows(state);
        CheckScrollbarInteraction(state, FindWindowExW(editor, nullptr, L"SCROLLBAR", nullptr));
        CheckScrollbarInteraction(state, FindWindowExW(library, nullptr, L"SCROLLBAR", nullptr));
        CheckScrollbarTransient(state, FindWindowExW(editor, nullptr, L"SCROLLBAR", nullptr), L"editor-dark");
        CheckScrollbarTransient(state, FindWindowExW(library, nullptr, L"SCROLLBAR", nullptr), L"library-dark");
        for (int i = 0; i < 40; ++i)
        {
            SendMessageW(editor, WM_VSCROLL, i < 20 ? SB_LINEDOWN : SB_LINEUP, 0);
            SendMessageW(library, WM_VSCROLL, i < 20 ? SB_LINEDOWN : SB_LINEUP, 0);
            EqualToFullRepaint(state, directory, L"scroll-" + std::to_wstring(i));
        }
        for (int i = 0; i < 80; ++i)
        {
            const WPARAM delta = MAKEWPARAM(0, static_cast<WORD>(i < 40 ? -30 : 30));
            SendMessageW(editor, WM_MOUSEWHEEL, delta, 0); SendMessageW(library, WM_MOUSEWHEEL, delta, 0);
        }
        EqualToFullRepaint(state, directory, L"rapid-fractional-wheel");
        SendMessageW(GetDlgItem(content, 2115), BM_CLICK, 0, 0);
        SendMessageW(editor, WM_VSCROLL, SB_BOTTOM, 0); EqualToFullRepaint(state, directory, L"expanded-bottom");
        SendMessageW(GetDlgItem(content, 2115), BM_CLICK, 0, 0); EqualToFullRepaint(state, directory, L"collapsed-bottom");
        const HWND combo = GetDlgItem(content, 2117);
        CheckDropdownScrolling(state, combo);
        SetActiveWindow(state.window); SetFocus(combo); SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0); Pump(state);
        COMBOBOXINFO info{sizeof(info)}; GetComboBoxInfo(combo, &info);
        Check(IsWindowVisible(info.hwndList), "Additional keys dropdown opens");
        auto dropdown = Capture(info.hwndList); Save(Capture(info.hwndList, true), directory / L"dropdown.bmp");
        Check(dropdown.Pixel(dropdown.width - 4, dropdown.height - 4) == ThemeManager::SurfaceColor(), "Dropdown background follows dark theme");
        Check(dropdown.height < scale(300), "Long dropdown stays compact");
        SCROLLBARINFO barInfo{sizeof(barInfo)}; DropdownGeometry(info.hwndList, barInfo);
        RECT bounds{}; GetWindowRect(info.hwndList, &bounds);
        const HWND dropdownBar = DropdownBar(info.hwndList);
        SendMessageW(dropdownBar, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2));
        const auto hoverFrame = Capture(info.hwndList, true);
        RepaintDropdownFrame(info.hwndList);
        Check(hoverFrame.pixels == Capture(info.hwndList, true).pixels, "Dropdown scrollbar hover has no native transient frame");
        const auto framed = Capture(info.hwndList, true);
        Check(framed.Pixel(barInfo.rcScrollBar.left - bounds.left + 1,
            barInfo.rcScrollBar.top - bounds.top + barInfo.dxyLineButton + 2) == ThemeManager::SurfaceColor(), "Dropdown scrollbar gutter is themed");
        SendMessageW(info.hwndList, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0); Pump(state);
        const auto scrolledDropdown = Capture(info.hwndList, true);
        Check(scrolledDropdown.Pixel(barInfo.rcScrollBar.left - bounds.left + 1,
            barInfo.rcScrollBar.top - bounds.top + barInfo.dxyLineButton + 2) == ThemeManager::SurfaceColor(), "Dropdown scrollbar remains themed after scrolling");
        Save(scrolledDropdown, directory / L"dropdown-scrolled.bmp");
        SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
        EqualToFullRepaint(state, directory, L"dropdown-dismissed");
        SendMessageW(GetDlgItem(content, 3000 + VK_F2), BM_CLICK, 0, 0);
        Check(ProfileEditor::IsDirty(editor), "Native checkbox creates a draft");
        for (auto theme : {ThemePreference::Light, ThemePreference::Dark})
        {
            g_themePreference = theme; ThemeManager::Initialize(dpi); state.view.Refresh(true); state.menu.SetMenu(UI::CreateAppMenu()); ThemeManager::ApplyDarkTitleBar(host);
            EqualToFullRepaint(state, directory, theme == ThemePreference::Dark ? L"dark-switch" : L"light-switch");
            Check(ProfileEditor::IsDirty(editor), "Theme switching keeps the draft");
            CheckMenuArrows(state);
            CheckDropdownScrolling(state, combo);
            CheckScrollbarInteraction(state, FindWindowExW(editor, nullptr, L"SCROLLBAR", nullptr));
            CheckScrollbarInteraction(state, FindWindowExW(library, nullptr, L"SCROLLBAR", nullptr));
            CheckScrollbarTransient(state, FindWindowExW(editor, nullptr, L"SCROLLBAR", nullptr), L"editor-theme-switch");
            CheckScrollbarTransient(state, FindWindowExW(library, nullptr, L"SCROLLBAR", nullptr), L"library-theme-switch");
            Save(Capture(host), directory / (theme == ThemePreference::Dark ? L"dark-scrolled.bmp" : L"light-scrolled.bmp"));
        }
        SetWindowTextW(GetDlgItem(state.view.Window(), 4102), L"sample-game-79"); EqualToFullRepaint(state, directory, L"filtered-list");
        const auto emptyArea = Capture(list);
        Check(emptyArea.Pixel(5, emptyArea.height - 5) == ThemeManager::SurfaceColor(), "List blank area has the theme background");
        SetWindowTextW(GetDlgItem(state.view.Window(), 4102), L"no-matching-game"); EqualToFullRepaint(state, directory, L"empty-list");
        CheckPanel(library);
        const auto disabledBar = Capture(FindWindowExW(library, nullptr, L"SCROLLBAR", nullptr));
        Check(disabledBar.Pixel(disabledBar.width / 2, disabledBar.height / 2) == ThemeManager::SurfaceColor(),
            "Empty library has a themed track without a stale thumb");
        SetWindowTextW(GetDlgItem(state.view.Window(), 4102), L"");
        g_language = UiLanguage::English; ThemeManager::Initialize(dpi); state.view.Refresh(true); state.menu.SetMenu(UI::CreateAppMenu());
        SetWindowPos(host, nullptr, 0, 0, scale(850), scale(600), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        EqualToFullRepaint(state, directory, L"narrow"); CheckPanel(editor); CheckPanel(library); Save(Capture(host), directory / L"narrow.bmp");
        SetWindowPos(host, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        EqualToFullRepaint(state, directory, L"restored-size");
        SetTimer(host, 1, 250, nullptr); SendMessageW(GetDlgItem(state.menu.Window(), 1), BM_CLICK, 0, 0); Pump(state);
        Check(state.popupChecks > 0 && state.popupDark, "Native popup menu uses the dark palette");
        EqualToFullRepaint(state, directory, L"menu-dismissed");
        state.replaceMenu = true; SetTimer(host, 1, 250, nullptr);
        SendMessageW(GetDlgItem(state.menu.Window(), 1), BM_CLICK, 0, 0); Pump(state);
        EqualToFullRepaint(state, directory, L"menu-refresh-while-open");
        SetFocus(GetDlgItem(state.view.Window(), 4102));
        MSG key{}; key.message = WM_KEYDOWN; key.wParam = VK_F10;
        Check(state.menu.Translate(key) && GetFocus() == GetDlgItem(state.menu.Window(), 1), "F10 focuses the menu bar");
        SendMessageW(GetFocus(), WM_KEYDOWN, VK_RIGHT, 0);
        Check(GetFocus() == GetDlgItem(state.menu.Window(), 2), "Menu arrow navigation");
        SendMessageW(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
        Check(GetFocus() == GetDlgItem(state.view.Window(), 4102), "Escape restores menu entry focus");
        key.message = WM_SYSKEYDOWN; key.wParam = VK_MENU; state.menu.Translate(key);
        key.message = WM_SYSKEYUP; state.menu.Translate(key);
        Check(GetFocus() == GetDlgItem(state.menu.Window(), 1), "Alt alone focuses the menu bar");
        DestroyWindow(host); state.window = nullptr; ThemeManager::Shutdown();
        std::cout << "PASS: " << checks << " visible UI checks; artifacts: " << directory.string() << '\n'; return 0;
    }
    catch (const std::exception& error)
    {
        if (IsWindow(state.window)) DestroyWindow(state.window);
        ThemeManager::Shutdown(); std::cerr << error.what() << '\n'; return 1;
    }
}
