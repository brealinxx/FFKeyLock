#include "../Rendering/NativeControls.h"
#include "ProfileEditor.h"
#include "../Controls/ScrollView.h"
#include "../Rendering/Surface.h"
#include "../../GameProtection.h"
#include "../../Localization.h"
#include "../../ThemeManager.h"
#include "../../WindowsKeyGuard.h"
#include "../../Platform/GdiUtils.h"
#include <algorithm>
#include <array>
#include <windowsx.h>

namespace FFKeyLock::ProfileEditor
{
namespace
{
constexpr wchar_t kEditorClass[] = L"FFKeyLockProfileEditor";
enum { Auto = 2100, Input, Keys, InheritWin, LockWin, Notify, Language, Mode, Timeout,
    ChatEnter, ChatT, ChatY, ChatSlash, Cat, Clear, Advanced, Test, OtherKey, AddKey, TestStatus };
constexpr int KeyBase = 3000;
struct State
{
    GameProfile original;
    std::wstring identity;
    bool loading = false;
    bool dirty = false;
    UI::ScrollView viewport;
    bool layingOut = false;
    std::vector<HWND> controls;
    std::vector<std::vector<UINT>> rows;
};
State* Get(HWND hwnd) { return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }
int S(HWND hwnd, int value) { return MulDiv(value, GetDpiForWindow(hwnd), 96); }
HWND Item(HWND hwnd, int id) { return GetDlgItem(Get(hwnd)->viewport.Content(), id); }
bool Checked(HWND hwnd, int id) { return SendMessageW(Item(hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void Check(HWND hwnd, int id, bool value) { SendMessageW(Item(hwnd, id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
void Label(HWND hwnd, int id, const wchar_t* cn, const wchar_t* en) { SetWindowTextW(Item(hwnd, id), Text(cn, en)); }

HWND Control(HWND hwnd, const wchar_t* type, int id, DWORD style, const wchar_t* text = L"")
{
    HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, Get(hwnd)->viewport.Content(),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(ThemeManager::UiFont()), FALSE);
    Get(hwnd)->controls.push_back(child);
    return child;
}
void MarkDirty(HWND hwnd)
{
    auto& state = *Get(hwnd);
    if (state.loading) return;
    state.dirty = true;
    SendMessageW(GetParent(hwnd), WM_PROFILE_DIRTY, 0, 0);
}

void SetOptions(HWND hwnd)
{
    Label(hwnd, Auto, L"自动应用这个游戏的配置", L"Automatically apply this game profile");
    Label(hwnd, Input, L"输入法保护", L"Input language protection");
    Label(hwnd, Keys, L"键位锁定", L"Key blocking");
    Label(hwnd, InheritWin, L"Win 键跟随全局默认", L"Use default Windows key setting");
    Label(hwnd, LockWin, L"锁定左右 Win 键", L"Block both Windows keys");
    Label(hwnd, Notify, L"配置生效时提示", L"Notify when profile is applied");
    Label(hwnd, Cat, L"防误触预设", L"Accidental press preset");
    Label(hwnd, Clear, L"清空锁键", L"Clear keys");
    Label(hwnd, Advanced, L"展开输入法与聊天设置", L"Show input language and chat settings");
    Label(hwnd, Test, L"测试配置（仅本窗口）", L"Test keys (this window only)");
    Label(hwnd, AddKey, L"切换此键", L"Toggle key");
    Label(hwnd, 2200, L"键盘区域 · 按下的按钮表示已锁定；再次点击即可放行", L"Keyboard · pressed buttons are blocked; click again to allow");
    Label(hwnd, 2201, L"目标输入法", L"Target language");
    Label(hwnd, 2202, L"聊天模式", L"Chat mode");
    Label(hwnd, 2203, L"恢复时间（1–300 秒）", L"Restore timeout (1–300 seconds)");
    Label(hwnd, 2204, L"聊天按键", L"Chat keys");
    Label(hwnd, 2205, L"额外按键（含 F13–F24、媒体键）", L"Additional keys (F13–F24, media keys)");
    Label(hwnd, 2206, L"Ctrl、Alt 和紧急解除键保留；聊天时仍锁定所选按键。", L"Ctrl, Alt and the emergency key stay available. Blocking continues during chat.");
    const auto oldLanguage = SendMessageW(Item(hwnd, Language), CB_GETCURSEL, 0, 0);
    const auto oldMode = SendMessageW(Item(hwnd, Mode), CB_GETCURSEL, 0, 0);
    SendMessageW(Item(hwnd, Language), CB_RESETCONTENT, 0, 0);
    SendMessageW(Item(hwnd, Language), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Text(L"英文", L"English")));
    SendMessageW(Item(hwnd, Language), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Text(L"中文", L"Chinese")));
    SendMessageW(Item(hwnd, Language), CB_SETCURSEL, std::max<LRESULT>(0, oldLanguage), 0);
    SendMessageW(Item(hwnd, Mode), CB_RESETCONTENT, 0, 0);
    SendMessageW(Item(hwnd, Mode), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Text(L"切换：Enter / Esc 恢复", L"Toggle: Enter / Esc restores")));
    SendMessageW(Item(hwnd, Mode), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Text(L"按住：松开后恢复", L"Hold: release to restore")));
    SendMessageW(Item(hwnd, Mode), CB_SETCURSEL, std::max<LRESULT>(0, oldMode), 0);
}

void Layout(HWND hwnd)
{
    auto& state = *Get(hwnd);
    if (state.layingOut || !state.viewport.Content()) return;
    state.layingOut = true;
    const int width = state.viewport.ContentWidth();
    const int pad = S(hwnd, 12), gap = S(hwnd, 8), row = S(hwnd, 32);
    const int inner = std::max(1, width - pad * 2);
    int y = pad;
    HDWP batch = BeginDeferWindowPos(static_cast<int>(state.controls.size()));
    HWND previous = HWND_TOP;
    auto place = [&](int id, int x, int py, int w, int h, bool visible = true) {
        HWND control = Item(hwnd, id);
        const UINT flags = SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW |
            (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
        if (batch) batch = DeferWindowPos(batch, control, previous, x, py, std::max(1, w), h, flags);
        else SetWindowPos(control, previous, x, py, std::max(1, w), h, flags);
        previous = control;
    };
    place(Auto, pad, y, inner, row); y += row + gap;
    place(Keys, pad, y, (inner - gap) / 2, row);
    place(Input, pad + (inner + gap) / 2, y, (inner - gap) / 2, row); y += row + gap;
    place(Cat, pad, y, (inner - gap) / 2, row);
    place(Clear, pad + (inner + gap) / 2, y, (inner - gap) / 2, row); y += row + S(hwnd, 16);
    place(Test, pad, y, inner, row); y += row;
    const bool testing = Checked(hwnd, Test);
    place(TestStatus, pad, y, inner, S(hwnd, 40), testing);
    y += testing ? S(hwnd, 48) : gap;
    place(2200, pad, y, inner, S(hwnd, 40)); y += S(hwnd, 44);
    // Keep short rows on a twelve-column grid. Give long key labels and
    // modifiers explicit spans instead of stretching every key in a short row.
    const int keyGap = S(hwnd, 4), keyHeight = S(hwnd, 32);
    for (const auto& keys : state.rows)
    {
        const int count = static_cast<int>(keys.size());
        int units = 0;
        auto weight = [](UINT key) { return key == VK_SPACE ? 4 :
            (key == VK_TAB || key == VK_BACK || key == VK_CAPITAL || key == VK_RETURN ||
             key == VK_LSHIFT || key == VK_RSHIFT || key == VK_SNAPSHOT || key == VK_SCROLL || key == VK_PAUSE ||
             key == VK_LWIN || key == VK_RWIN || key == VK_HOME || key == VK_PRIOR || key == VK_NEXT) ? 2 : 1; };
        for (UINT key : keys) units += weight(key);
        const int columns = std::max(12, units);
        const int usable = inner - keyGap * (columns - 1);
        int offset = 0;
        for (int index = 0; index < count; ++index)
        {
            const int span = weight(keys[index]);
            const int x = pad + MulDiv(usable, offset, columns) + keyGap * offset;
            const int end = pad + MulDiv(usable, offset + span, columns) + keyGap * (offset + span - 1);
            place(KeyBase + keys[index], x, y, end - x, keyHeight);
            offset += span;
        }
        y += keyHeight + keyGap;
    }
    y += S(hwnd, 12);
    place(2205, pad, y, inner, row); y += row;
    place(OtherKey, pad, y, inner - S(hwnd, 112) - gap, S(hwnd, 240));
    place(AddKey, width - pad - S(hwnd, 112), y, S(hwnd, 112), row); y += row + gap;
    place(2206, pad, y, inner, S(hwnd, 44)); y += S(hwnd, 52);
    place(InheritWin, pad, y, inner, row); y += row;
    place(LockWin, pad, y, inner, row); y += row;
    EnableWindow(Item(hwnd, LockWin), !Checked(hwnd, InheritWin));
    place(Notify, pad, y, inner, row); y += row + gap;
    place(Advanced, pad, y, inner, row); y += row + gap;
    const bool expanded = Checked(hwnd, Advanced);
    for (auto [label, field] : { std::pair{2201, Language}, {2202, Mode}, {2203, Timeout} })
    {
        place(label, pad, y, inner, S(hwnd, 24), expanded);
        place(field, pad, y + S(hwnd, 24), inner, field == Timeout ? row : S(hwnd, 150), expanded);
        if (expanded) y += S(hwnd, 24) + row + gap;
    }
    place(2204, pad, y, inner, row, expanded); if (expanded) y += row;
    int x = pad;
    for (int id : {ChatEnter, ChatT, ChatY, ChatSlash})
    {
        place(id, x, y, (inner - gap * 3) / 4, row, expanded); x += (inner + gap) / 4;
    }
    if (expanded) y += row + gap;
    if (batch) EndDeferWindowPos(batch);
    state.viewport.SetExtent(y + pad);
    state.layingOut = false;
}


bool Reserved(UINT key)
{
    return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL || key == VK_MENU ||
        key == VK_LMENU || key == VK_RMENU || key == g_emergencyKey;
}

void UpdateExtraKeys(HWND hwnd)
{
    auto& state = *Get(hwnd);
    const LRESULT selected = SendMessageW(Item(hwnd, OtherKey), CB_GETCURSEL, 0, 0);
    SendMessageW(Item(hwnd, OtherKey), CB_RESETCONTENT, 0, 0);
    std::vector<UINT> extras = { VK_OEM_3, VK_OEM_4, VK_OEM_6, VK_OEM_5, VK_OEM_1, VK_OEM_7, VK_OEM_2,
        VK_NUMLOCK, VK_DIVIDE, VK_MULTIPLY, VK_SUBTRACT, VK_ADD, VK_DECIMAL, VK_APPS,
        VK_VOLUME_MUTE, VK_VOLUME_DOWN, VK_VOLUME_UP, VK_MEDIA_NEXT_TRACK, VK_MEDIA_PREV_TRACK, VK_MEDIA_STOP, VK_MEDIA_PLAY_PAUSE };
    for (UINT key = VK_NUMPAD0; key <= VK_NUMPAD9; ++key) extras.push_back(key);
    for (UINT key = VK_F13; key <= VK_F24; ++key) extras.push_back(key);
    for (UINT key : state.original.blockedKeys)
        if (std::find(extras.begin(), extras.end(), key) == extras.end()) extras.push_back(key);
    for (UINT key : extras)
    {
        if (Reserved(key)) continue;
        bool inGrid = false;
        for (const auto& row : state.rows) if (std::find(row.begin(), row.end(), key) != row.end()) inGrid = true;
        if (inGrid) continue;
        const bool locked = std::find(state.original.blockedKeys.begin(), state.original.blockedKeys.end(), key) != state.original.blockedKeys.end();
        const std::wstring label = (locked ? L"[✓] " : L"[ ] ") + KeyName(key);
        const LRESULT index = SendMessageW(Item(hwnd, OtherKey), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(Item(hwnd, OtherKey), CB_SETITEMDATA, index, key);
    }
    SendMessageW(Item(hwnd, OtherKey), CB_SETCURSEL, std::max<LRESULT>(0, selected), 0);
}

void CreateControls(HWND hwnd)
{
    auto& state = *Get(hwnd);
    for (int id : {Auto, Input, Keys, InheritWin, LockWin, Notify, Advanced, Test, ChatEnter, ChatT, ChatY, ChatSlash})
        Control(hwnd, L"BUTTON", id, WS_TABSTOP | BS_AUTOCHECKBOX);
    for (int id : {Cat, Clear, AddKey}) Control(hwnd, L"BUTTON", id, WS_TABSTOP | BS_PUSHBUTTON);
    for (int id : {Language, Mode, OtherKey}) Control(hwnd, L"COMBOBOX", id, WS_TABSTOP | CBS_DROPDOWNLIST);
    Control(hwnd, L"EDIT", Timeout, WS_TABSTOP | ES_NUMBER);
    for (int id = 2200; id <= 2206; ++id) Control(hwnd, L"STATIC", id, 0);
    Control(hwnd, L"STATIC", TestStatus, 0);
    for (int id : {2200, 2206}) SetPropW(Item(hwnd, id), L"FFKeyLock.MutedText", reinterpret_cast<HANDLE>(1));
    state.rows = {
        {VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12},
        {VK_SNAPSHOT,VK_SCROLL,VK_PAUSE,VK_ESCAPE,VK_INSERT,VK_DELETE},
        {'1','2','3','4','5','6','7','8','9','0',VK_OEM_MINUS,VK_OEM_PLUS},
        {VK_TAB,'Q','W','E','R','T','Y','U','I','O','P',VK_BACK},
        {VK_CAPITAL,'A','S','D','F','G','H','J','K','L',VK_RETURN},
        {VK_LSHIFT,'Z','X','C','V','B','N','M',VK_OEM_COMMA,VK_OEM_PERIOD,VK_RSHIFT},
        {VK_LWIN,VK_SPACE,VK_RWIN,VK_HOME,VK_END,VK_PRIOR,VK_NEXT,VK_LEFT,VK_UP,VK_DOWN,VK_RIGHT}
    };
    for (const auto& row : state.rows) for (UINT key : row)
    {
        HWND control = Control(hwnd, L"BUTTON", KeyBase + key, WS_TABSTOP | BS_AUTOCHECKBOX | BS_PUSHLIKE, KeyName(key).c_str());
        EnableWindow(control, !Reserved(key));
    }
    SetWindowTextW(Item(hwnd, ChatEnter), L"Enter"); SetWindowTextW(Item(hwnd, ChatT), L"T");
    SetWindowTextW(Item(hwnd, ChatY), L"Y"); SetWindowTextW(Item(hwnd, ChatSlash), L"/");
    for (HWND control : state.controls) state.viewport.TrackFocus(control);
    SetOptions(hwnd);
    UI::ApplyTheme(hwnd);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    auto* state = Get(hwnd);
    switch (message)
    {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new State));
        Get(hwnd)->viewport.Create(hwnd);
        CreateControls(hwnd); return 0;
    case WM_SIZE: if (state) Layout(hwnd); return 0;
    case WM_SETFOCUS:
        if (state) SetFocus(Item(hwnd, Auto));
        return 0;
    case WM_MOUSEWHEEL: if (state) state->viewport.OnWheel(w); return 0;
    case WM_VSCROLL: if (state) state->viewport.OnScroll(w); return 0;
    case WM_COMMAND:
        if (!state || state->loading) return 0;
        if (LOWORD(w) == Advanced) { Layout(hwnd); return 0; }
        if (LOWORD(w) == Test)
        {
            if (Checked(hwnd, Test))
            {
                if (g_protectionPaused || !g_protectionEnabled || !KeyboardGuardHealthy())
                {
                    Check(hwnd, Test, false);
                    MessageBoxW(hwnd, Text(L"请先恢复总保护；若键盘拦截不可用，请在设置中重新连接。", L"Resume protection first. If the keyboard hook is unavailable, reconnect it in Settings."), L"FFKeyLock", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                GameProfile profile;
                if (!ReadEditor(hwnd, profile)) { Check(hwnd, Test, false); return 0; }
                StartKeyboardTest(hwnd, profile);
                SetWindowTextW(Item(hwnd, TestStatus), Text(L"测试中：按键后显示结果；只拦截本窗口。切换窗口即停止测试。", L"Testing: press a key. Only this window is affected; switching away ends the test."));
            }
            else { StopKeyboardTest(); SetWindowTextW(Item(hwnd, TestStatus), L""); }
            Layout(hwnd); return 0;
        }
        if (LOWORD(w) == Cat || LOWORD(w) == Clear)
        {
            StopKeyboardTest();
            GameProfile profile = state->original;
            if (LOWORD(w) == Cat) ApplyCatPreset(profile); else profile.blockedKeys.clear();
            state->original.blockedKeys = profile.blockedKeys;
            for (const auto& row : state->rows) for (UINT key : row)
                Check(hwnd, KeyBase + key, std::find(profile.blockedKeys.begin(), profile.blockedKeys.end(), key) != profile.blockedKeys.end());
            if (LOWORD(w) == Cat) Check(hwnd, Keys, true);
            else { Check(hwnd, InheritWin, false); Check(hwnd, LockWin, false); Layout(hwnd); }
            UpdateExtraKeys(hwnd); MarkDirty(hwnd); return 0;
        }
        if (LOWORD(w) == AddKey)
        {
            StopKeyboardTest();
            const auto index = SendMessageW(Item(hwnd, OtherKey), CB_GETCURSEL, 0, 0);
            if (index != CB_ERR)
            {
                UINT key = static_cast<UINT>(SendMessageW(Item(hwnd, OtherKey), CB_GETITEMDATA, index, 0));
                auto& keys = state->original.blockedKeys;
                auto found = std::find(keys.begin(), keys.end(), key);
                if (found == keys.end()) keys.push_back(key); else keys.erase(found);
                UpdateExtraKeys(hwnd); MarkDirty(hwnd);
            }
            return 0;
        }
        if (LOWORD(w) == OtherKey) return 0;
        if (HIWORD(w) == BN_CLICKED || HIWORD(w) == CBN_SELCHANGE || HIWORD(w) == EN_CHANGE)
        {
            if (Checked(hwnd, Test)) { Check(hwnd, Test, false); StopKeyboardTest(); Layout(hwnd); }
            if (LOWORD(w) == InheritWin) Layout(hwnd);
            MarkDirty(hwnd);
        }
        return 0;
    case WM_NOTIFY:
    {
        const auto* header = reinterpret_cast<NMHDR*>(l);
        if (header->code == NM_CUSTOMDRAW)
        {
            LRESULT result = 0;
            if (UI::DrawCheckBox(*reinterpret_cast<NMCUSTOMDRAW*>(l), result)) return result;
        }
        break;
    }
    case WM_TEST_KEY:
        if (state)
        {
            if (!w) { Check(hwnd, Test, false); SetWindowTextW(Item(hwnd, TestStatus), L""); Layout(hwnd); return 0; }
            const auto text = KeyName(static_cast<UINT>(w)) + (l ? Text(L" · 已拦截", L" · Blocked") : Text(L" · 已放行", L" · Allowed"));
            SetWindowTextW(Item(hwnd, TestStatus), text.c_str());
        }
        return 0;
    case WM_DRAWITEM:
        UI::DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: case WM_CTLCOLORSCROLLBAR:
        return reinterpret_cast<LRESULT>(UI::HandleCtlColor(hwnd, reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l)));
    case WM_PAINT: case WM_PRINTCLIENT: case WM_ERASEBKGND: return UI::PaintSurface(hwnd, message, w);
    case WM_DESTROY: StopKeyboardTest(); return 0;
    case WM_NCDESTROY: delete state; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
}

std::wstring KeyName(UINT key)
{
    if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) return std::wstring(1, static_cast<wchar_t>(key));
    switch (key) {
    case VK_SNAPSHOT: return L"PrtSc"; case VK_SCROLL: return L"ScrLk"; case VK_PAUSE: return L"Pause";
    case VK_ESCAPE: return L"Esc"; case VK_BACK: return L"Back"; case VK_RETURN: return L"Enter";
    case VK_LWIN: return L"LWin"; case VK_RWIN: return L"RWin"; case VK_SPACE: return L"Space";
    case VK_LSHIFT: case VK_RSHIFT: return L"Shift"; case VK_CAPITAL: return L"Caps"; case VK_TAB: return L"Tab";
    case VK_INSERT: return L"Ins"; case VK_DELETE: return L"Del"; case VK_HOME: return L"Home"; case VK_END: return L"End";
    case VK_PRIOR: return L"PgUp"; case VK_NEXT: return L"PgDn"; case VK_LEFT: return L"←"; case VK_RIGHT: return L"→";
    case VK_UP: return L"↑"; case VK_DOWN: return L"↓";
    case VK_VOLUME_MUTE: return Text(L"静音", L"Mute"); case VK_VOLUME_DOWN: return Text(L"音量减", L"Volume down");
    case VK_VOLUME_UP: return Text(L"音量加", L"Volume up"); case VK_MEDIA_PLAY_PAUSE: return Text(L"播放 / 暂停", L"Play / pause");
    case VK_MEDIA_NEXT_TRACK: return Text(L"下一首", L"Next track"); case VK_MEDIA_PREV_TRACK: return Text(L"上一首", L"Previous track");
    case VK_MEDIA_STOP: return Text(L"停止播放", L"Media stop");
    }
    wchar_t name[64]{};
    const UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64)) return name;
    return L"VK " + std::to_wstring(key);
}

HWND CreateEditor(HWND parent)
{
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = g_hInst; wc.lpszClassName = kEditorClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    return CreateWindowExW(WS_EX_CONTROLPARENT, kEditorClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, nullptr, g_hInst, nullptr);
}

void LoadEditor(HWND hwnd, const std::wstring& identity, const GameProfile& profile)
{
    StopKeyboardTest();
    auto& state = *Get(hwnd); state.loading = true; state.original = profile; state.identity = identity;
    Check(hwnd, Auto, profile.enabled); Check(hwnd, Input, profile.inputGuardEnabled); Check(hwnd, Keys, profile.keyGuardEnabled);
    Check(hwnd, InheritWin, profile.inheritWindowsKey); Check(hwnd, LockWin, profile.lockWindowsKey); Check(hwnd, Notify, profile.showNotifications);
    Check(hwnd, Test, false); SetWindowTextW(Item(hwnd, TestStatus), L"");
    SendMessageW(Item(hwnd, Language), CB_SETCURSEL, profile.targetLanguage == ProtectedInputLanguage::Chinese ? 1 : 0, 0);
    SendMessageW(Item(hwnd, Mode), CB_SETCURSEL, profile.chatMode == ChatActivationMode::Hold ? 1 : 0, 0);
    SetWindowTextW(Item(hwnd, Timeout), std::to_wstring(profile.restoreTimeoutMs / 1000).c_str());
    for (auto [id, key] : {std::pair{ChatEnter, UINT(VK_RETURN)}, {ChatT, UINT('T')}, {ChatY, UINT('Y')}, {ChatSlash, UINT(VK_OEM_2)}})
        Check(hwnd, id, std::find(profile.chatKeys.begin(), profile.chatKeys.end(), key) != profile.chatKeys.end());
    for (const auto& row : state.rows) for (UINT key : row)
    {
        Check(hwnd, KeyBase + key, std::find(profile.blockedKeys.begin(), profile.blockedKeys.end(), key) != profile.blockedKeys.end());
        EnableWindow(Item(hwnd, KeyBase + key), !Reserved(key));
    }
    UpdateExtraKeys(hwnd); state.loading = false; state.dirty = false; state.viewport.Reset(); Layout(hwnd);
}

bool ReadEditor(HWND hwnd, GameProfile& profile)
{
    const auto& state = *Get(hwnd);
    profile = state.original;
    profile.enabled = Checked(hwnd, Auto); profile.inputGuardEnabled = Checked(hwnd, Input); profile.keyGuardEnabled = Checked(hwnd, Keys);
    profile.inheritWindowsKey = Checked(hwnd, InheritWin); profile.lockWindowsKey = Checked(hwnd, LockWin); profile.showNotifications = Checked(hwnd, Notify);
    profile.targetLanguage = SendMessageW(Item(hwnd, Language), CB_GETCURSEL, 0, 0) == 1 ? ProtectedInputLanguage::Chinese : ProtectedInputLanguage::English;
    profile.chatMode = SendMessageW(Item(hwnd, Mode), CB_GETCURSEL, 0, 0) == 1 ? ChatActivationMode::Hold : ChatActivationMode::Toggle;
    wchar_t number[32]{}; GetWindowTextW(Item(hwnd, Timeout), number, 32);
    wchar_t* end = nullptr; const long seconds = wcstol(number, &end, 10);
    if (end == number || *end || seconds < 1 || seconds > 300)
    {
        MessageBoxW(hwnd, Text(L"恢复时间请输入 1–300 秒。", L"Enter a timeout between 1 and 300 seconds."), L"FFKeyLock", MB_OK | MB_ICONWARNING); return false;
    }
    profile.restoreTimeoutMs = static_cast<UINT>(seconds * 1000);
    profile.chatKeys.clear();
    for (auto [id, key] : {std::pair{ChatEnter, UINT(VK_RETURN)}, {ChatT, UINT('T')}, {ChatY, UINT('Y')}, {ChatSlash, UINT(VK_OEM_2)}})
        if (Checked(hwnd, id)) profile.chatKeys.push_back(key);
    if (profile.chatKeys.empty())
    {
        if (profile.inputGuardEnabled) { MessageBoxW(hwnd, Text(L"请至少选择一个聊天按键。", L"Select at least one chat key."), L"FFKeyLock", MB_OK | MB_ICONWARNING); return false; }
        profile.chatKeys.push_back(VK_RETURN);
    }
    for (const auto& row : state.rows) for (UINT key : row)
    {
        auto& keys = profile.blockedKeys;
        keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
        if (Checked(hwnd, KeyBase + key)) keys.push_back(key);
    }
    if (profile.inputGuardEnabled && profile.keyGuardEnabled)
        for (UINT key : profile.chatKeys) if (std::find(profile.blockedKeys.begin(), profile.blockedKeys.end(), key) != profile.blockedKeys.end())
        { MessageBoxW(hwnd, Text(L"聊天按键与锁键冲突，请放行聊天按键或关闭输入法保护。", L"A chat key is blocked. Allow it or turn off input language protection."), L"FFKeyLock", MB_OK | MB_ICONWARNING); return false; }
    NormalizeGameProfile(profile);
    return true;
}
bool IsDirty(HWND hwnd) { return hwnd && Get(hwnd) && Get(hwnd)->dirty; }
void RefreshEditorTheme(HWND hwnd)
{
    auto* state = Get(hwnd); if (!state) return;
    state->loading = true; SetOptions(hwnd); UpdateExtraKeys(hwnd); UI::ApplyTheme(hwnd); state->loading = false; Layout(hwnd);
}
}
