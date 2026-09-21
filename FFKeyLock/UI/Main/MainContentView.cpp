#include "../Rendering/NativeControls.h"
#include "../Rendering/Surface.h"
#include "MainContentView.h"
#include "../Menu/PopupMenu.h"
#include "../../Config.h"
#include "../../GameProtection.h"
#include "../../Localization.h"
#include "../../MainWindow.h"
#include "../../Resource.h"
#include "../../StringUtils.h"
#include "../../ThemeManager.h"
#include "../../WindowsKeyGuard.h"
#include "../../Platform/GdiUtils.h"
#include "../Profiles/ProfileEditor.h"
#include <algorithm>
#include <windowsx.h>
#include <shellapi.h>
#include <unordered_set>

namespace FFKeyLock
{
namespace
{
enum { Status = 4100, Detail, Search, List, Title, Path, Save, Undo, Dirty, Add, Running, Delete, Copy, Paste, Pause, Browse, ListLabel };
int Scale(HWND hwnd, int n) { return MulDiv(n, GetDpiForWindow(hwnd), 96); }
std::wstring TextOf(HWND hwnd)
{
    std::wstring value(GetWindowTextLengthW(hwnd) + 1, L'\0');
    GetWindowTextW(hwnd, value.data(), static_cast<int>(value.size())); value.resize(wcslen(value.c_str())); return value;
}
void SetText(HWND parent, int id, const std::wstring& value)
{
    HWND child = GetDlgItem(parent, id);
    if (TextOf(child) != value) SetWindowTextW(child, value.c_str());
}
HWND Make(HWND parent, const wchar_t* type, int id, DWORD style, const wchar_t* value = L"")
{
    return CreateWindowExW(0, type, value, WS_CHILD | WS_VISIBLE | style, 0,0,0,0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, nullptr);
}
std::wstring Summary(const GameProfile& profile)
{
    if (!profile.enabled) return Text(L"未启用", L"Disabled");
    auto count = profile.keyGuardEnabled ? profile.blockedKeys.size() : 0;
    if (profile.keyGuardEnabled && EffectiveProfileWindowsKey(profile))
    {
        for (UINT key : {UINT(VK_LWIN), UINT(VK_RWIN)})
            if (std::find(profile.blockedKeys.begin(), profile.blockedKeys.end(), key) == profile.blockedKeys.end()) ++count;
    }
    std::wstring summary = std::to_wstring(count) + Text(L" 键", L" keys");
    if (profile.inputGuardEnabled) summary += profile.targetLanguage == ProtectedInputLanguage::Chinese ? L" · 中文" : L" · EN";
    return summary;
}
}

HWND MainContentView::Create(HWND parent)
{
    WNDCLASSW wc{}; wc.hInstance = g_hInst; wc.lpfnWndProc = Proc; wc.lpszClassName = L"FFKeyLockWorkspace";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0,0,0,0,parent,nullptr,g_hInst,this);
    return window_;
}
void MainContentView::CreateControls()
{
    for (int id : {Status, Detail, Title, Path, Dirty, ListLabel}) Make(window_, L"STATIC", id, SS_LEFT);
    for (int id : {Detail, Path}) SetPropW(GetDlgItem(window_, id), L"FFKeyLock.MutedText", reinterpret_cast<HANDLE>(1));
    SetWindowLongPtrW(GetDlgItem(window_, Path), GWL_STYLE, WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS);
    search_ = Make(window_, L"EDIT", Search, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL);
    list_ = library_.Create(window_, List);
    for (int id : {Save, Undo, Add, Running, Delete, Copy, Paste, Pause, Browse}) Make(window_, L"BUTTON", id, WS_TABSTOP | BS_PUSHBUTTON);
    editor_ = ProfileEditor::CreateEditor(window_);
    Refresh(true);
}

void MainContentView::Layout()
{
    if (!window_) return;
    RECT r{}; GetClientRect(window_, &r);
    auto s = [&](int n) { return Scale(window_, n); };
    const int pad = s(16), gap = s(12), left = s(260), x = pad + left + gap * 2;
    const int right = std::max(1, static_cast<int>(r.right) - x - pad);
    const int button = s(32), half = (left - gap) / 2;
    HDWP batch = BeginDeferWindowPos(18);
    auto moveWindow = [&](HWND child, int px, int py, int w, int h) {
        const UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS;
        if (batch) batch = DeferWindowPos(batch, child, nullptr, px, py, std::max(1, w), std::max(1, h), flags);
        else SetWindowPos(child, nullptr, px, py, std::max(1, w), std::max(1, h), flags);
    };
    auto move = [&](int id, int px, int py, int w, int h) { moveWindow(GetDlgItem(window_, id), px, py, w, h); };
    move(Status, pad,pad,r.right - pad*2 - s(140),s(28));
    move(Pause,r.right-pad-s(120),pad,s(120),button);
    move(Detail,pad,pad+s(36),r.right-pad*2,s(40));
    const int top = s(104), bottom = static_cast<int>(r.bottom) - pad;
    move(ListLabel,pad,top,left,s(24));
    move(Search,pad,top+s(32),left,button);
    move(Add,pad,top+s(72),half,button);
    move(Running,pad+half+gap,top+s(72),half,button);
    move(List,pad,top+s(116),left,std::max(s(80),bottom-top-s(200)));
    move(Copy,pad,bottom-s(72),half,button);
    move(Paste,pad+half+gap,bottom-s(72),half,button);
    move(Delete,pad,bottom-button,half,button);
    move(Browse,pad+half+gap,bottom-button,half,button);
    move(Title,x,top,right,s(24)); move(Path,x,top+s(28),right,s(24));
    moveWindow(editor_,x,top+s(60),right,std::max(s(80),bottom-top-s(140)));
    move(Dirty,x,bottom-s(68),right,s(24));
    move(Undo,x+right-s(220),bottom-button,s(104),button);
    move(Save,x+right-s(104),bottom-button,s(104),button);
    if (batch) EndDeferWindowPos(batch);
    InvalidateRect(window_, nullptr, TRUE);
}

void MainContentView::RefreshList()
{
    const std::wstring search = ToLower(TextOf(search_));
    std::wstring signature = search + (IsEnglish() ? L"en" : L"zh") + std::to_wstring(GetDpiForWindow(window_));
    for (const auto& name : g_gameExeNames) signature += L"|" + name + Summary(GetGameProfileForExe(name)) + (g_gameExePaths.contains(name) ? g_gameExePaths.at(name) : L"");
    if (signature == signature_) return;
    signature_ = signature;
    refreshing_ = true;
    visible_.clear(); std::vector<UI::LibraryItem> items;
    int selectedIndex = -1;
    for (const auto& name : g_gameExeNames)
    {
        if (!search.empty() && ToLower(name).find(search) == std::wstring::npos) continue;
        if (name == selected_) selectedIndex = static_cast<int>(items.size());
        visible_.push_back(name);
        const auto found = g_gameExePaths.find(name);
        items.push_back({GameDisplayName(name), Summary(GetGameProfileForExe(name)), found == g_gameExePaths.end() ? name : found->second});
    }
    library_.SetItems(std::move(items)); library_.Select(selectedIndex);
    refreshing_ = false;
}

void MainContentView::Refresh(bool theme)
{
    if (!window_ || !list_) return;
    if (theme)
    {
        UI::ApplyTheme(window_); ProfileEditor::RefreshEditorTheme(editor_);
        library_.RefreshTheme();
        SendMessageW(GetDlgItem(window_, Status), WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::TitleFont()), TRUE);
        SendMessageW(GetDlgItem(window_, Title), WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::TitleFont()), TRUE);
        SendMessageW(GetDlgItem(window_, ListLabel), WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::TitleFont()), TRUE);
        SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(Text(L"搜索游戏或路径", L"Search games or paths")));
        SetText(window_,ListLabel,Text(L"游戏库",L"Games")); SetText(window_,Add,Text(L"选择文件",L"Choose file"));
        SetText(window_,Running,Text(L"运行中程序",L"Running apps")); SetText(window_,Delete,Text(L"移除游戏",L"Remove"));
        SetText(window_,Copy,Text(L"复制配置",L"Copy profile")); SetText(window_,Paste,Text(L"粘贴配置",L"Paste profile"));
        SetText(window_,Save,Text(L"保存配置",L"Save profile")); SetText(window_,Undo,Text(L"撤销修改",L"Revert"));
        SetText(window_,Browse,Text(L"打开目录",L"Open folder"));
        UpdateSelectionText(); Layout();
    }
    const std::wstring status = g_protectionPaused ? Text(L"已暂停保护", L"Protection paused") :
        !g_protectionEnabled ? Text(L"保护已关闭", L"Protection off") :
        g_inGameProtection ? GameDisplayName(g_activeGameExeName) + Text(L" · 保护中", L" · Protected") : Text(L"待机 · 等待游戏进入前台", L"Ready · waiting for a game");
    SetText(window_, Status, status);
    std::wstring detail;
    if (!g_configError.empty()) detail = g_configError;
    else if (!KeyboardGuardHealthy()) detail = Text(L"键盘拦截不可用，请在设置中重试。", L"Keyboard hook unavailable. Retry from Settings.");
    else if (g_protectionPaused) detail = g_pauseUntil ? Text(L"计时结束后自动恢复；也可以立即恢复。", L"Protection resumes when the timer ends, or resume now.") : Text(L"手动恢复前保持暂停，自动检测不会覆盖。", L"Paused until you resume; auto detection will not override this.");
    else if (!g_autoDetectEnabled) detail = Text(L"自动检测已关闭。请在设置中开启。", L"Auto detection is off. Enable it in Settings.");
    else if (g_inGameProtection) detail = Summary(GetGameProfileForExe(g_activeGameExeName)) + (g_chatInputSuspended ? Text(L" · 聊天中，锁键继续生效",L" · Chatting; key blocking remains active") : Text(L" · 切出游戏自动解除",L" · Released when you leave the game"));
    else if (!g_currentDetectedGameName.empty()) detail = Text(L"此游戏的自动应用已关闭。", L"Automatic application is disabled for this game.");
    else detail = Text(L"所选游戏用于编辑；返回该游戏后自动应用已保存配置。", L"Selection is for editing. Saved settings apply when you return to that game.");
    detail += L"  Ctrl + Alt + " + ProfileEditor::KeyName(g_emergencyKey) + Text(L"：紧急解除", L": emergency unlock");
    SetText(window_,Detail,detail); SetText(window_,Pause,g_protectionPaused ? Text(L"恢复保护",L"Resume") : Text(L"暂停保护",L"Pause"));
    RefreshList();
    if (!selected_.empty() && !g_gameProfiles.contains(selected_)) { selected_.clear(); LoadSelection(); }
    if (selected_.empty() && !visible_.empty()) Select(visible_.front());
    UpdateDirty();
}

void MainContentView::UpdateSelectionText()
{
    const bool available = !selected_.empty() && g_gameProfiles.contains(selected_);
    SetText(window_, Title, available ? GameDisplayName(selected_) : Text(L"添加一个游戏，开始配置", L"Add a game to get started"));
    const auto found = g_gameExePaths.find(selected_);
    SetText(window_, Path, !available ? L"" : found == g_gameExePaths.end() ?
        Text(L"按程序名匹配 · 重新选择文件可改为路径匹配", L"Name matching · choose its executable to use path matching") : found->second);
}

void MainContentView::LoadSelection()
{
    const bool available = !selected_.empty() && g_gameProfiles.contains(selected_);
    EnableWindow(editor_, available);
    UpdateSelectionText();
    ProfileEditor::LoadEditor(editor_, available ? selected_ : L"", available ? GetGameProfileForExe(selected_) : NewGameProfile());
    UpdateDirty();
}

void MainContentView::Select(const std::wstring& identity)
{
    selected_ = identity; refreshing_ = true;
    const auto found = std::find(visible_.begin(), visible_.end(), identity);
    library_.Select(found == visible_.end() ? -1 : static_cast<int>(found - visible_.begin()));
    refreshing_ = false; LoadSelection();
}
void MainContentView::UpdateDirty()
{
    const bool valid = !selected_.empty();
    const bool dirty = ProfileEditor::IsDirty(editor_);
    SetText(window_,Dirty,!g_configError.empty() ? g_configError : !valid ? L"" : dirty ? Text(L"有未保存修改",L"Unsaved changes") : Text(L"已保存 · 游戏在前台时生效",L"Saved · applies while the game is in front"));
    EnableWindow(GetDlgItem(window_,Save),valid && (dirty || !g_configError.empty())); EnableWindow(GetDlgItem(window_,Undo),valid && dirty);
    for (int id : {Delete,Copy,Browse}) EnableWindow(GetDlgItem(window_,id),valid);
    EnableWindow(GetDlgItem(window_,Paste),valid && copied_.has_value());
}
bool MainContentView::SavePending()
{
    if (selected_.empty()) return true;
    GameProfile profile;
    if (!ProfileEditor::ReadEditor(editor_,profile)) return false;
    SetGameProfileForExe(selected_,profile);
    if (!g_configError.empty()) { UpdateDirty(); return false; }
    ProfileEditor::LoadEditor(editor_,selected_,profile); Refresh(); return true;
}
bool MainContentView::ConfirmDiscard()
{
    if (!ProfileEditor::IsDirty(editor_)) return true;
    const int answer = MessageBoxW(window_,Text(L"保存当前游戏的修改吗？\n选择“否”放弃修改，选择“取消”继续编辑。", L"Save changes to this game?\nNo discards them; Cancel keeps editing."),L"FFKeyLock",MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false;
    if (answer == IDYES) return SavePending();
    LoadSelection(); return true;
}
void MainContentView::CopyProfile()
{
    GameProfile profile;
    if (!selected_.empty() && ProfileEditor::ReadEditor(editor_,profile)) { copied_ = profile; UpdateDirty(); }
}
void MainContentView::PasteProfile()
{
    if (!selected_.empty() && copied_ && ConfirmDiscard())
    {
        ProfileEditor::LoadEditor(editor_,selected_,*copied_);
        // Mark as a draft using the same editor notification path.
        SendMessageW(editor_,WM_COMMAND,MAKEWPARAM(2100,BN_CLICKED),0);
        UpdateDirty();
    }
}

LRESULT CALLBACK MainContentView::Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    auto* self = reinterpret_cast<MainContentView*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message == WM_CREATE)
    {
        self = static_cast<MainContentView*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->window_ = hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); self->CreateControls(); self->LoadSelection(); return 0;
    }
    if (!self) return DefWindowProcW(hwnd,message,w,l);
    switch (message)
    {
    case WM_SIZE: self->Layout(); return 0;
    case ProfileEditor::WM_PROFILE_DIRTY: self->UpdateDirty(); return 0;
    case WM_COMMAND:
    {
        const int id = LOWORD(w);
        if (id == List && !self->refreshing_)
        {
            if (HIWORD(w) == LBN_SELCHANGE)
            {
                const int index = self->library_.Selected();
                if (index >= 0 && index < static_cast<int>(self->visible_.size()) && self->selected_ != self->visible_[index])
                {
                    // Capture the identity before a save can refresh the list.
                    const auto identity = self->visible_[index];
                    if (self->ConfirmDiscard()) self->Select(identity);
                    else
                    {
                        const auto found = std::find(self->visible_.begin(), self->visible_.end(), self->selected_);
                        self->library_.Select(found == self->visible_.end() ? -1 : static_cast<int>(found - self->visible_.begin()));
                    }
                }
            }
            if (HIWORD(w) == LBN_DBLCLK) SetFocus(self->editor_);
            return 0;
        }
        if (id == Search && HIWORD(w) == EN_CHANGE) { self->RefreshList(); return 0; }
        if (HIWORD(w) != BN_CLICKED) return 0;
        switch (id) {
        case Save: self->SavePending(); return 0; case Undo: self->LoadSelection(); return 0;
        case Copy: self->CopyProfile(); return 0; case Paste: self->PasteProfile(); return 0;
        case Pause: if (g_protectionPaused) ResumeProtection(); else PauseProtection(); return 0;
        }
        const int command = id == Add ? IDM_ADD_GAME_FILE : id == Running ? IDM_RUNNING_PROGRAMS : id == Delete ? IDM_DELETE_SELECTED_GAME : id == Browse ? IDM_OPEN_GAME_FOLDER : 0;
        if (command) SendMessageW(GetParent(hwnd),WM_COMMAND,command,0);
        return 0;
    }
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(w) == self->list_)
        {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (pt.x == -1 && pt.y == -1)
            {
                RECT row{}; const HWND list = self->library_.List();
                if (SendMessageW(list, LB_GETITEMRECT, self->library_.Selected(), reinterpret_cast<LPARAM>(&row)) == LB_ERR)
                    GetClientRect(list, &row);
                pt = {row.left + 8, row.top}; ClientToScreen(list, &pt);
            }
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu,MF_STRING,IDM_COPY_PROFILE,Text(L"复制配置",L"Copy profile"));
            AppendMenuW(menu,MF_STRING,IDM_COPY_GAME_NAME,Text(L"复制程序名",L"Copy executable name"));
            AppendMenuW(menu,MF_STRING | (self->copied_ ? 0 : MF_GRAYED),IDM_PASTE_PROFILE,Text(L"粘贴配置",L"Paste profile"));
            AppendMenuW(menu,MF_STRING,IDM_OPEN_GAME_FOLDER,Text(L"打开目录",L"Open folder"));
            AppendMenuW(menu,MF_STRING,IDM_DELETE_SELECTED_GAME,Text(L"移除游戏",L"Remove game"));
            const UINT command = UI::ShowPopupMenu(GetParent(hwnd), menu, pt); DestroyMenu(menu);
            if (command) PostMessageW(GetParent(hwnd), WM_COMMAND, command, 0);
        }
        return 0;
    case WM_DRAWITEM: UI::DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(UI::HandleCtlColor(hwnd,reinterpret_cast<HDC>(w),reinterpret_cast<HWND>(l)));
    case WM_PAINT: case WM_PRINTCLIENT: case WM_ERASEBKGND: return UI::PaintSurface(hwnd, message, w);
    case WM_DESTROY:
        self->window_ = nullptr; return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}
