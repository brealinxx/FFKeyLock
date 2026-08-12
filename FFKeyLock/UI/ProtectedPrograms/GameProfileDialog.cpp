#include "GameProfileDialog.h"

#include "../../Localization.h"
#include "../../ThemeManager.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace FFKeyLock::GameProfileDialog
{
namespace
{
constexpr wchar_t kClassName[] = L"FFKeyLockGameProfileDialog";
constexpr int IDC_PROFILE_LANGUAGE = 2101;
constexpr int IDC_PROFILE_KEYS = 2102;
constexpr int IDC_PROFILE_MODE = 2103;
constexpr int IDC_PROFILE_TIMEOUT = 2104;
constexpr int IDC_PROFILE_WINKEY = 2105;
constexpr int IDC_PROFILE_NOTIFICATIONS = 2106;
constexpr int IDC_PROFILE_SAVE = 2107;
constexpr int IDC_PROFILE_CANCEL = 2108;

struct DialogState
{
    std::wstring exeName;
    GameProfile profile;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool accepted = false;
    HWND window = nullptr;
    HWND language = nullptr;
    HWND keys = nullptr;
    HWND mode = nullptr;
    HWND timeout = nullptr;
    HWND winKey = nullptr;
    HWND notifications = nullptr;
};

int Scale(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
}

void SetFont(HWND control)
{
    SendMessageW(control, WM_SETFONT,
        reinterpret_cast<WPARAM>(ThemeManager::UiFont() ? ThemeManager::UiFont() : GetStockObject(DEFAULT_GUI_FONT)),
        TRUE);
}

HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int width, int height)
{
    HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, width, height, parent, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    SetFont(control);
    return control;
}

HWND CreateEdit(HWND parent, int id, const std::wstring& text, int x, int y, int width, int height)
{
    HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    SetFont(control);
    return control;
}

HWND CreateCombo(HWND parent, int id, int x, int y, int width, int height)
{
    HWND control = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    SetFont(control);
    return control;
}

HWND CreateButton(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height, bool primary)
{
    HWND control = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | (primary ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    SetFont(control);
    return control;
}

void AddComboItem(HWND combo, const wchar_t* text)
{
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

std::wstring WindowText(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(std::max(0, length)) + 1, L'\0');
    GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
    text.resize(wcslen(text.c_str()));
    return text;
}

std::wstring ChatKeyText(UINT key)
{
    switch (key)
    {
    case VK_RETURN:
        return L"Enter";
    case 'T':
        return L"T";
    case 'Y':
        return L"Y";
    case VK_OEM_2:
        return L"/";
    default:
        return std::to_wstring(key);
    }
}

std::wstring JoinChatKeys(const std::vector<UINT>& keys)
{
    std::wstring text;
    for (UINT key : keys)
    {
        if (!text.empty())
        {
            text += L", ";
        }
        text += ChatKeyText(key);
    }
    return text;
}

std::wstring LowerTrim(std::wstring value)
{
    while (!value.empty() && iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && iswspace(value.back())) value.pop_back();
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

bool ParseChatKeys(std::wstring text, std::vector<UINT>& keys)
{
    std::replace(text.begin(), text.end(), L'，', L',');
    std::replace(text.begin(), text.end(), L';', L',');
    std::replace(text.begin(), text.end(), L'；', L',');
    keys.clear();
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find(L',', start);
        const std::wstring token = LowerTrim(text.substr(start, end == std::wstring::npos ? end : end - start));
        UINT key = 0;
        if (token == L"enter" || token == L"return" || token == L"回车") key = VK_RETURN;
        else if (token == L"t") key = 'T';
        else if (token == L"y") key = 'Y';
        else if (token == L"/" || token == L"slash") key = VK_OEM_2;
        else if (!token.empty()) return false;
        if (key && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return !keys.empty();
}

bool ReadProfile(DialogState& state)
{
    GameProfile profile = state.profile;
    profile.targetLanguage = SendMessageW(state.language, CB_GETCURSEL, 0, 0) == 1
        ? ProtectedInputLanguage::Chinese
        : ProtectedInputLanguage::English;
    profile.chatMode = SendMessageW(state.mode, CB_GETCURSEL, 0, 0) == 1
        ? ChatActivationMode::Hold
        : ChatActivationMode::Toggle;
    profile.lockWindowsKey = SendMessageW(state.winKey, CB_GETCURSEL, 0, 0) == 1;
    profile.showNotifications = SendMessageW(state.notifications, CB_GETCURSEL, 0, 0) == 1;

    if (!ParseChatKeys(WindowText(state.keys), profile.chatKeys))
    {
        MessageBoxW(state.window,
            Text(L"聊天按键只支持 Enter、T、Y 和 /，请至少填写一个，并使用逗号分隔。",
                L"Chat keys support Enter, T, Y and / only. Enter at least one key separated by commas."),
            L"FFKeyLock", MB_OK | MB_ICONWARNING);
        SetFocus(state.keys);
        return false;
    }

    const int timeoutSeconds = _wtoi(WindowText(state.timeout).c_str());
    if (timeoutSeconds < 1 || timeoutSeconds > 300)
    {
        MessageBoxW(state.window,
            Text(L"恢复时间必须在 1 到 300 秒之间。", L"Restore time must be between 1 and 300 seconds."),
            L"FFKeyLock", MB_OK | MB_ICONWARNING);
        SetFocus(state.timeout);
        return false;
    }
    profile.restoreTimeoutMs = static_cast<UINT>(timeoutSeconds * 1000);
    state.profile = std::move(profile);
    return true;
}

void CreateControls(HWND hwnd, DialogState& state)
{
    const UINT dpi = state.dpi;
    const int left = Scale(24, dpi);
    const int labelWidth = Scale(132, dpi);
    const int fieldLeft = left + labelWidth;
    const int fieldWidth = Scale(310, dpi);
    const int rowHeight = Scale(30, dpi);
    const int gap = Scale(14, dpi);
    int y = Scale(22, dpi);

    CreateLabel(hwnd, Text(L"程序", L"Program"), left, y, labelWidth, rowHeight);
    CreateLabel(hwnd, state.exeName.c_str(), fieldLeft, y, fieldWidth, rowHeight);
    y += rowHeight + gap;

    CreateLabel(hwnd, Text(L"目标输入法", L"Target language"), left, y, labelWidth, rowHeight);
    state.language = CreateCombo(hwnd, IDC_PROFILE_LANGUAGE, fieldLeft, y, fieldWidth, Scale(180, dpi));
    AddComboItem(state.language, Text(L"英文", L"English"));
    AddComboItem(state.language, Text(L"中文", L"Chinese"));
    SendMessageW(state.language, CB_SETCURSEL,
        state.profile.targetLanguage == ProtectedInputLanguage::Chinese ? 1 : 0, 0);
    y += rowHeight + gap;

    CreateLabel(hwnd, Text(L"聊天按键", L"Chat keys"), left, y, labelWidth, rowHeight);
    state.keys = CreateEdit(hwnd, IDC_PROFILE_KEYS, JoinChatKeys(state.profile.chatKeys), fieldLeft, y, fieldWidth, rowHeight);
    y += rowHeight + Scale(4, dpi);
    CreateLabel(hwnd, Text(L"可填写 Enter, T, Y, /，多个按键用逗号分隔", L"Enter, T, Y, / — separate multiple keys with commas"),
        fieldLeft, y, fieldWidth, Scale(22, dpi));
    y += Scale(22, dpi) + gap;

    CreateLabel(hwnd, Text(L"聊天模式", L"Chat mode"), left, y, labelWidth, rowHeight);
    state.mode = CreateCombo(hwnd, IDC_PROFILE_MODE, fieldLeft, y, fieldWidth, Scale(150, dpi));
    AddComboItem(state.mode, Text(L"切换：按聊天键进入，Enter/Esc 恢复", L"Toggle: chat key opens, Enter/Esc restores"));
    AddComboItem(state.mode, Text(L"按住：松开聊天键后恢复", L"Hold: restore when the key is released"));
    SendMessageW(state.mode, CB_SETCURSEL, state.profile.chatMode == ChatActivationMode::Hold ? 1 : 0, 0);
    y += rowHeight + gap;

    CreateLabel(hwnd, Text(L"恢复时间（秒）", L"Restore time (sec)"), left, y, labelWidth, rowHeight);
    state.timeout = CreateEdit(hwnd, IDC_PROFILE_TIMEOUT,
        std::to_wstring(std::max(1U, state.profile.restoreTimeoutMs / 1000)), fieldLeft, y, fieldWidth, rowHeight);
    y += rowHeight + gap;

    CreateLabel(hwnd, Text(L"锁定 Win 键", L"Lock Windows key"), left, y, labelWidth, rowHeight);
    state.winKey = CreateCombo(hwnd, IDC_PROFILE_WINKEY, fieldLeft, y, fieldWidth, Scale(120, dpi));
    AddComboItem(state.winKey, Text(L"关闭", L"Disabled"));
    AddComboItem(state.winKey, Text(L"开启", L"Enabled"));
    SendMessageW(state.winKey, CB_SETCURSEL, state.profile.lockWindowsKey ? 1 : 0, 0);
    y += rowHeight + gap;

    CreateLabel(hwnd, Text(L"显示状态通知", L"Show notifications"), left, y, labelWidth, rowHeight);
    state.notifications = CreateCombo(hwnd, IDC_PROFILE_NOTIFICATIONS, fieldLeft, y, fieldWidth, Scale(120, dpi));
    AddComboItem(state.notifications, Text(L"关闭", L"Disabled"));
    AddComboItem(state.notifications, Text(L"开启", L"Enabled"));
    SendMessageW(state.notifications, CB_SETCURSEL, state.profile.showNotifications ? 1 : 0, 0);

    const int buttonY = y + rowHeight + Scale(22, dpi);
    const int buttonWidth = Scale(104, dpi);
    const int buttonHeight = Scale(34, dpi);
    CreateButton(hwnd, IDC_PROFILE_CANCEL, Text(L"取消", L"Cancel"),
        fieldLeft + fieldWidth - buttonWidth * 2 - Scale(10, dpi), buttonY, buttonWidth, buttonHeight, false);
    CreateButton(hwnd, IDC_PROFILE_SAVE, Text(L"保存", L"Save"),
        fieldLeft + fieldWidth - buttonWidth, buttonY, buttonWidth, buttonHeight, true);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message)
    {
    case WM_CREATE:
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<DialogState*>(create->lpCreateParams);
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        ThemeManager::ApplyDarkTitleBar(hwnd);
        CreateControls(hwnd, *state);
        ThemeManager::ApplyTheme(hwnd);
        return 0;
    }

    case WM_COMMAND:
        if (state && LOWORD(wParam) == IDC_PROFILE_SAVE && HIWORD(wParam) == BN_CLICKED)
        {
            if (ReadProfile(*state))
            {
                state->accepted = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDC_PROFILE_CANCEL && HIWORD(wParam) == BN_CLICKED)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DRAWITEM:
    {
        const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_BUTTON)
        {
            ThemeManager::DrawButton(*item);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(ThemeManager::HandleCtlColor(
            hwnd, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

    case WM_ERASEBKGND:
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, ThemeManager::WindowBrush());
        return TRUE;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (state) state->window = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterClass(HINSTANCE instance)
{
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    registered = RegisterClassExW(&windowClass) != 0;
}
}

bool Show(HWND owner, const std::wstring& exeName, const GameProfile& current, GameProfile& result)
{
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    RegisterClass(instance);
    DialogState state{};
    state.exeName = exeName;
    state.profile = current;

    const UINT dpi = GetDpiForWindow(owner);
    state.dpi = dpi;
    RECT windowRect{ 0, 0, Scale(510, dpi), Scale(452, dpi) };
    AdjustWindowRectExForDpi(&windowRect, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClassName,
        Text(L"游戏独立配置", L"Game profile"),
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        x, y, width, height,
        owner, nullptr, instance, &state);
    if (!window)
    {
        return false;
    }

    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    MSG message{};
    bool sawQuit = false;
    while (state.window && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (message.message == WM_QUIT)
    {
        sawQuit = true;
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (sawQuit)
    {
        PostQuitMessage(static_cast<int>(message.wParam));
    }
    if (state.accepted)
    {
        result = std::move(state.profile);
    }
    return state.accepted;
}
}
