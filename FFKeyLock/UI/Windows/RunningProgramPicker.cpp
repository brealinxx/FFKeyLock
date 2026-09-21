#include "RunningProgramPicker.h"
#include "../Library/GameLibraryView.h"
#include "../Rendering/NativeControls.h"
#include "../Rendering/Surface.h"
#include "../../AppState.h"
#include "../../ThemeManager.h"
#include "../../GameProtection.h"
#include "../../Localization.h"
#include "../../StringUtils.h"
#include <algorithm>
namespace FFKeyLock::UI
{
struct RunningProgram { std::wstring path, title; };
struct Picker { HWND window = nullptr, list = nullptr; UI::GameLibraryView library; std::vector<RunningProgram> programs; std::wstring result; HFONT font = nullptr; UINT dpi = 0; };
void PopulatePicker(Picker& picker)
{
    picker.programs.clear();
    EnumWindows([](HWND hwnd, LPARAM data)->BOOL {
        auto& p = *reinterpret_cast<Picker*>(data);
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd,GW_OWNER)) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid); if (!pid || pid == GetCurrentProcessId()) return TRUE;
        const std::wstring path = GetProgramPath(hwnd);
        const auto name = GetExeNameFromPath(path);
        if (name.empty() || name == L"explorer.exe" || name == L"shellexperiencehost.exe" || name == L"startmenuexperiencehost.exe") return TRUE;
        if (std::any_of(p.programs.begin(),p.programs.end(),[&](const auto& item) { return _wcsicmp(item.path.c_str(),path.c_str()) == 0; })) return TRUE;
        wchar_t title[512]{}; GetWindowTextW(hwnd,title,512);
        if (!title[0]) return TRUE;
        p.programs.push_back({path,title}); return TRUE;
    },reinterpret_cast<LPARAM>(&picker));
    std::sort(picker.programs.begin(),picker.programs.end(),[](const auto& a,const auto& b) { return a.title < b.title; });
    std::vector<UI::LibraryItem> rows;
    for (const auto& item : picker.programs) rows.push_back({item.title, item.path, item.path});
    picker.library.SetItems(std::move(rows));
    if (!picker.programs.empty()) picker.library.Select(0);
}
void LayoutPicker(HWND hwnd, Picker& picker)
{
    RECT r{}; GetClientRect(hwnd,&r); const UINT dpi = GetDpiForWindow(hwnd);
    auto s = [&](int n) { return MulDiv(n,dpi,96); };
    SetWindowPos(picker.list,nullptr,s(16),s(16),r.right-s(32),r.bottom-s(82),SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd,100),nullptr,s(16),r.bottom-s(52),s(110),s(34),SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd,IDOK),nullptr,r.right-s(142),r.bottom-s(52),s(126),s(34),SWP_NOZORDER);
    if (picker.font && picker.dpi == dpi) return;
    picker.dpi = dpi;
    HFONT font = CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    EnumChildWindows(hwnd,[](HWND child,LPARAM data)->BOOL { SendMessageW(child,WM_SETFONT,data,TRUE); return TRUE; },reinterpret_cast<LPARAM>(font));
    if (picker.font) DeleteObject(picker.font); picker.font = font;
}
LRESULT CALLBACK PickerProc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    auto* picker = reinterpret_cast<Picker*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message == WM_CREATE)
    {
        picker = static_cast<Picker*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); picker->window = hwnd;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(picker));
        picker->list = picker->library.Create(hwnd, 10);
        CreateWindowW(L"BUTTON",Text(L"刷新",L"Refresh"),WS_CHILD | WS_VISIBLE | WS_TABSTOP,0,0,0,0,hwnd,reinterpret_cast<HMENU>(100),g_hInst,nullptr);
        CreateWindowW(L"BUTTON",Text(L"添加游戏",L"Add game"),WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDOK),g_hInst,nullptr);
        PopulatePicker(*picker); UI::ApplyTheme(hwnd); ThemeManager::ApplyDarkTitleBar(hwnd); LayoutPicker(hwnd,*picker); return 0;
    }
    if (!picker) return DefWindowProcW(hwnd,message,w,l);
    switch (message) {
    case WM_GETMINMAXINFO:
    {
        auto* size = reinterpret_cast<MINMAXINFO*>(l); const UINT dpi = GetDpiForWindow(hwnd);
        size->ptMinTrackSize = {MulDiv(500, dpi, 96), MulDiv(300, dpi, 96)}; return 0;
    }
    case WM_SIZE: LayoutPicker(hwnd,*picker); return 0;
    case WM_DPICHANGED: { const auto* r = reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER); picker->library.RefreshTheme(); picker->dpi = 0; LayoutPicker(hwnd,*picker); return 0; }
    case WM_COMMAND:
        if (LOWORD(w) == 100) { PopulatePicker(*picker); return 0; }
        if (LOWORD(w) == IDOK || (LOWORD(w) == 10 && HIWORD(w) == LBN_DBLCLK))
        {
            const auto selected = picker->library.Selected();
            if (selected >= 0 && selected < static_cast<LRESULT>(picker->programs.size())) { picker->result = picker->programs[selected].path; DestroyWindow(hwnd); }
        }
        if (LOWORD(w) == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    case WM_DRAWITEM: UI::DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(UI::HandleCtlColor(hwnd,reinterpret_cast<HDC>(w),reinterpret_cast<HWND>(l)));
    case WM_PAINT: case WM_PRINTCLIENT: case WM_ERASEBKGND: return UI::PaintSurface(hwnd, message, w);
    case WM_DESTROY: picker->window = nullptr; return 0;
    case WM_NCDESTROY: if (picker->font) DeleteObject(picker->font); picker->font = nullptr; break;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
std::wstring ChooseRunningProgram(HWND owner)
{
    WNDCLASSW wc{}; wc.hInstance = g_hInst; wc.lpfnWndProc = PickerProc; wc.lpszClassName = L"FFKeyLockRunningPrograms";
    wc.hCursor = LoadCursorW(nullptr,IDC_ARROW); RegisterClassW(&wc);
    Picker picker; RECT bounds{}; GetWindowRect(owner,&bounds);
    const UINT dpi = GetDpiForWindow(owner);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,Text(L"选择运行中的游戏（选择游戏窗口，而非启动器）",L"Choose the game window, not its launcher"),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,bounds.left+24,bounds.top+24,MulDiv(780,dpi,96),MulDiv(480,dpi,96),owner,nullptr,g_hInst,&picker);
    if (!hwnd) return L"";
    EnableWindow(owner,FALSE); ShowWindow(hwnd,SW_SHOW); SetFocus(picker.library.List());
    MSG msg{};
    while (picker.window && GetMessageW(&msg,nullptr,0,0)>0) if (!IsDialogMessageW(hwnd,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    EnableWindow(owner,TRUE); SetForegroundWindow(owner);
    if (msg.message == WM_QUIT) PostQuitMessage(static_cast<int>(msg.wParam));
    return picker.result;
}

}
