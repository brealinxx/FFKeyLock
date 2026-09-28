#include "WindowsKeyGuard.h"
#include "Config.h"
#include "GameProtection.h"
#include "KeyPolicy.h"
#include "MainWindow.h"
#include "UI/Profiles/ProfileEditor.h"
#include <atomic>
#include <memory>

namespace FFKeyLock
{
namespace
{
constexpr UINT kUpdatePolicy = WM_APP + 70;
struct Policy
{
    HWND owner = nullptr;
    HWND foreground = nullptr;
    std::bitset<256> blocked;
    std::bitset<256> chat;
    bool alwaysWin = false;
    bool paused = false;
    UINT emergency = VK_BACK;
    HWND test = nullptr;
    UINT session = 0;
};
HANDLE g_thread = nullptr;
DWORD g_threadId = 0;
HANDLE g_ready = nullptr;
std::atomic<bool> g_healthy{false};
// Everything below is owned by the input thread, including press disposition.
Policy g_policy;
HHOOK g_hook = nullptr;
KeyPressTracker g_presses;
std::bitset<256> g_chatTracked;
bool g_emergencyLatched = false;
bool g_emergencyPending = false;
// These two are only accessed by the UI thread when publishing a policy.
HWND g_testEditor = nullptr;
GameProfile g_testProfile;

LRESULT CALLBACK KeyboardProc(int code, WPARAM message, LPARAM data)
{
    if (code != HC_ACTION) return CallNextHookEx(g_hook, code, message, data);
    const auto& input = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    if ((input.flags & LLKHF_INJECTED) || input.vkCode >= 256)
        return CallNextHookEx(g_hook, code, message, data);
    const UINT key = input.vkCode;
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    const bool first = down && !g_presses.IsHeld(key);
    const bool foreground = g_policy.foreground && GetForegroundWindow() == g_policy.foreground;
    const bool ctrl = g_presses.IsHeld(VK_LCONTROL) || g_presses.IsHeld(VK_RCONTROL);
    const bool alt = g_presses.IsHeld(VK_LMENU) || g_presses.IsHeld(VK_RMENU);
    if (first && key == g_policy.emergency && ctrl && alt && !g_policy.paused && (foreground || g_policy.alwaysWin))
    {
        g_policy.paused = true;
        g_policy.blocked.reset();
        g_policy.alwaysWin = false;
        g_emergencyLatched = true;
        g_emergencyPending = true;
        PostMessageW(g_policy.owner, WM_EMERGENCY_UNLOCK, 0, 0);
    }
    const bool win = key == VK_LWIN || key == VK_RWIN;
    const bool desired = !g_policy.paused && ((foreground && g_policy.blocked[key]) || (win && g_policy.alwaysWin));
    const bool emergencyStroke = key == g_policy.emergency && g_emergencyLatched;
    const bool blocked = g_presses.Process(key, down, up, desired || emergencyStroke,
        key == VK_PAUSE || key == VK_CANCEL, key == VK_SNAPSHOT);
    if (first && foreground && g_policy.test)
        PostMessageW(g_policy.test, ProfileEditor::WM_TEST_KEY, key, blocked);
    if (up && key == g_policy.emergency) g_emergencyLatched = false;
    if (!blocked && foreground && !g_policy.paused && !g_policy.test)
    {
        if (first && g_policy.chat[key])
        {
            g_chatTracked.set(key);
            PostMessageW(g_policy.owner, WM_GAME_CHAT_KEY, key, (static_cast<LPARAM>(g_policy.session) << 1) | 1);
        }
        if (up && g_chatTracked[key])
        {
            g_chatTracked.reset(key);
            PostMessageW(g_policy.owner, WM_GAME_CHAT_KEY, key, static_cast<LPARAM>(g_policy.session) << 1);
        }
    }
    else if (up) g_chatTracked.reset(key);
    return blocked ? 1 : CallNextHookEx(g_hook, code, message, data);
}

DWORD WINAPI InputThread(void*)
{
    MSG msg{};
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    g_presses = KeyPressTracker{};
    g_policy = Policy{};
    g_emergencyLatched = false;
    g_emergencyPending = false;
    g_chatTracked.reset();
    for (UINT key = 0; key < 256; ++key) g_presses.Seed(key, (GetAsyncKeyState(key) & 0x8000) != 0);
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
    g_healthy.store(g_hook != nullptr);
    SetEvent(g_ready);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == kUpdatePolicy)
        {
            std::unique_ptr<Policy> policy(reinterpret_cast<Policy*>(msg.lParam));
            if (policy->foreground != g_policy.foreground || policy->session != g_policy.session) g_chatTracked.reset();
            g_policy = *policy;
            if (g_emergencyPending)
            {
                if (policy->paused) g_emergencyPending = false;
                g_policy.paused = true;
            }
        }
    }
    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
    while (PeekMessageW(&msg, nullptr, kUpdatePolicy, kUpdatePolicy, PM_REMOVE))
        delete reinterpret_cast<Policy*>(msg.lParam);
    g_healthy.store(false);
    return 0;
}
}

bool EffectiveProfileWindowsKey(const GameProfile& profile)
{
    return profile.inheritWindowsKey ? g_windowsKeyGuardEnabled : profile.lockWindowsKey;
}

void RefreshKeyboardPolicy()
{
    if (!g_threadId) return;
    auto policy = std::make_unique<Policy>();
    policy->owner = g_hWnd;
    policy->session = g_gameSession;
    policy->paused = g_protectionPaused || !g_protectionEnabled;
    policy->emergency = g_emergencyKey;
    policy->alwaysWin = g_windowsKeyGuardEnabled && g_windowsKeyGuardScope == WindowsKeyGuardScope::Always;
    if (g_inGameProtection && !g_activeGameExeName.empty())
    {
        const GameProfile profile = GetGameProfileForExe(g_activeGameExeName);
        policy->foreground = g_savedWindow;
        if (profile.keyGuardEnabled)
        {
            for (UINT key : profile.blockedKeys) if (key < 256) policy->blocked.set(key);
            if (EffectiveProfileWindowsKey(profile))
            {
                policy->blocked.set(VK_LWIN);
                policy->blocked.set(VK_RWIN);
            }
        }
        if (profile.inputGuardEnabled)
        {
            for (UINT key : profile.chatKeys) if (key < 256) policy->chat.set(key);
            if (g_chatInputSuspended) { policy->chat.set(VK_RETURN); policy->chat.set(VK_ESCAPE); }
        }
    }
    if (g_testEditor)
    {
        policy->test = g_testEditor;
        policy->foreground = GetAncestor(g_testEditor, GA_ROOT);
        policy->blocked.reset();
        policy->chat.reset();
        policy->alwaysWin = false;
        if (g_testProfile.keyGuardEnabled)
        {
            for (UINT key : g_testProfile.blockedKeys) if (key < 256) policy->blocked.set(key);
            if (EffectiveProfileWindowsKey(g_testProfile)) { policy->blocked.set(VK_LWIN); policy->blocked.set(VK_RWIN); }
        }
    }
    // Ctrl+Pause can arrive as VK_CANCEL (Break), not VK_PAUSE.
    if (policy->blocked[VK_PAUSE]) policy->blocked.set(VK_CANCEL);
    if (PostThreadMessageW(g_threadId, kUpdatePolicy, 0, reinterpret_cast<LPARAM>(policy.get()))) policy.release();
}

bool ApplyWindowsKeyGuard()
{
    if (!g_thread)
    {
        g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_ready) return false;
        g_thread = CreateThread(nullptr, 0, InputThread, nullptr, 0, &g_threadId);
        if (g_thread) WaitForSingleObject(g_ready, INFINITE);
        CloseHandle(g_ready);
        g_ready = nullptr;
    }
    RefreshKeyboardPolicy();
    return g_healthy.load();
}

void DisableWindowsKeyGuard()
{
    if (g_thread)
    {
        PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = nullptr;
        g_threadId = 0;
    }
}

bool KeyboardGuardHealthy() { return g_healthy.load(); }

void StartKeyboardTest(HWND editor, const GameProfile& profile)
{
    g_testEditor = editor; g_testProfile = profile; RefreshKeyboardPolicy();
}
void StopKeyboardTest()
{
    if (!g_testEditor) return;
    HWND previous = g_testEditor;
    g_testEditor = nullptr;
    if (IsWindow(previous)) SendMessageW(previous, ProfileEditor::WM_TEST_KEY, 0, 0);
    RefreshKeyboardPolicy();
}

void ToggleWindowsKeyGuard()
{
    g_windowsKeyGuardEnabled = !g_windowsKeyGuardEnabled;
    ApplyWindowsKeyGuard();
    SaveConfig();
    UpdateMainWindow();
}
}
