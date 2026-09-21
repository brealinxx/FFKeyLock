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
struct Harness { MainContentView view; UI::MenuBar menu; HWND window = nullptr; int popupChecks = 0; bool popupDark = false; bool replaceMenu = false; HMENU arrowMenu = nullptr; std::vector<int> arrowGroups; bool arrowHighlighted = false; std::filesystem::path directory; };
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

}
int wmain(int argc, wchar_t** argv)
{
    Harness state;
    try
    {
        Check(argc == 2, "Supply an isolated artifact directory");
        const auto directory = std::filesystem::absolute(argv[1]) / (L"ui-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory); state.directory = directory;
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
        UI::InvalidateSurface(host, true); Pump(state);
        HWND editor = FindWindowExW(state.view.Window(), nullptr, L"FFKeyLockProfileEditor", nullptr);
        HWND library = GetDlgItem(state.view.Window(), 4103);
        HWND list = FindWindowExW(library, nullptr, L"LISTBOX", nullptr);
        HWND content = FindWindowExW(editor, nullptr, L"FFKeyLockScrollContent", nullptr);
        Check(editor && library && list && content, "Find live editor/library controls");
        Check(GetMenu(host) == nullptr, "No system non-client menu background");
        Save(Capture(host), directory / L"dark-start.bmp");
        EqualToFullRepaint(state, directory, L"initial");
        CheckMenuArrows(state);
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
        SetFocus(combo); SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0); Pump(state);
        COMBOBOXINFO info{sizeof(info)}; GetComboBoxInfo(combo, &info);
        Check(IsWindowVisible(info.hwndList), "Additional keys dropdown opens");
        auto dropdown = Capture(info.hwndList); Save(Capture(info.hwndList, true), directory / L"dropdown.bmp");
        Check(dropdown.Pixel(dropdown.width - 4, dropdown.height - 4) == ThemeManager::SurfaceColor(), "Dropdown background follows dark theme");
        Check(dropdown.height < scale(300), "Long dropdown stays compact");
        SCROLLBARINFO barInfo{sizeof(barInfo)}; GetScrollBarInfo(info.hwndList, OBJID_VSCROLL, &barInfo);
        RECT bounds{}; GetWindowRect(info.hwndList, &bounds);
        SendMessageW(info.hwndList, WM_NCMOUSEMOVE, HTVSCROLL,
            MAKELPARAM(barInfo.rcScrollBar.left + 2, barInfo.rcScrollBar.top + 2));
        const auto hoverFrame = Capture(info.hwndList, true);
        SendMessageW(info.hwndList, WM_NCPAINT, 1, 0);
        Check(hoverFrame.pixels == Capture(info.hwndList, true).pixels, "Dropdown scrollbar hover has no native transient frame");
        const auto framed = Capture(info.hwndList, true);
        Check(framed.Pixel(barInfo.rcScrollBar.left - bounds.left + 1,
            barInfo.rcScrollBar.top - bounds.top + barInfo.dxyLineButton + 2) == ThemeManager::WindowColor(), "Dropdown scrollbar gutter is themed");
        SendMessageW(info.hwndList, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0); Pump(state);
        const auto scrolledDropdown = Capture(info.hwndList, true);
        Check(scrolledDropdown.Pixel(barInfo.rcScrollBar.left - bounds.left + 1,
            barInfo.rcScrollBar.top - bounds.top + barInfo.dxyLineButton + 2) == ThemeManager::WindowColor(), "Dropdown scrollbar remains themed after scrolling");
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
            CheckScrollbarTransient(state, FindWindowExW(editor, nullptr, L"SCROLLBAR", nullptr), L"editor-theme-switch");
            CheckScrollbarTransient(state, FindWindowExW(library, nullptr, L"SCROLLBAR", nullptr), L"library-theme-switch");
            Save(Capture(host), directory / (theme == ThemePreference::Dark ? L"dark-scrolled.bmp" : L"light-scrolled.bmp"));
        }
        SetWindowTextW(GetDlgItem(state.view.Window(), 4102), L"sample-game-79"); EqualToFullRepaint(state, directory, L"filtered-list");
        const auto emptyArea = Capture(list);
        Check(emptyArea.Pixel(5, emptyArea.height - 5) == ThemeManager::WindowColor(), "List blank area has the theme background");
        SetWindowTextW(GetDlgItem(state.view.Window(), 4102), L"no-matching-game"); EqualToFullRepaint(state, directory, L"empty-list");
        SetWindowTextW(GetDlgItem(state.view.Window(), 4102), L"");
        g_language = UiLanguage::English; ThemeManager::Initialize(dpi); state.view.Refresh(true); state.menu.SetMenu(UI::CreateAppMenu());
        SetWindowPos(host, nullptr, 0, 0, scale(850), scale(600), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        EqualToFullRepaint(state, directory, L"narrow"); Save(Capture(host), directory / L"narrow.bmp");
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
