#include "WindowsKeyGuard.h"

#include "AppState.h"
#include "Config.h"
#include "GameProtection.h"
#include "Localization.h"
#include "MainWindow.h"
#include "TrayIcon.h"

#include <array>

namespace FFKeyLock
{
namespace
{
HHOOK g_keyboardHook = nullptr;
std::array<bool, 256> g_keyDown{};

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code != HC_ACTION)
    {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (!keyboard)
    {
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    const bool injected = (keyboard->flags & LLKHF_INJECTED) != 0;
    const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

    const UINT virtualKey = keyboard->vkCode;
    if (virtualKey < g_keyDown.size())
    {
        if (keyUp)
        {
            const bool wasTracked = g_keyDown[virtualKey];
            g_keyDown[virtualKey] = false;
            if (wasTracked && !injected && g_hWnd)
            {
                PostMessageW(g_hWnd, WM_GAME_CHAT_KEY, virtualKey, FALSE);
            }
        }
        else if (keyDown && !g_keyDown[virtualKey] && !injected && IsGameChatControlKey(virtualKey))
        {
            g_keyDown[virtualKey] = true;
            if (g_hWnd)
            {
                PostMessageW(g_hWnd, WM_GAME_CHAT_KEY, virtualKey, TRUE);
            }
        }
    }

    const bool blockWindowsKey = (g_windowsKeyGuardEnabled &&
        g_windowsKeyGuardScope == WindowsKeyGuardScope::Always)
        ? true
        : ShouldBlockWindowsKeyForActiveGame();
    if (blockWindowsKey && !injected &&
        (keyboard->vkCode == VK_LWIN || keyboard->vkCode == VK_RWIN))
    {
        return 1;
    }

    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}
}

bool ApplyWindowsKeyGuard()
{
    const bool needsKeyboardHook = g_protectionEnabled ||
        (g_windowsKeyGuardEnabled && g_windowsKeyGuardScope == WindowsKeyGuardScope::Always);
    if (!needsKeyboardHook)
    {
        DisableWindowsKeyGuard();
        return true;
    }

    if (g_keyboardHook)
    {
        return true;
    }

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInst, 0);
    if (!g_keyboardHook)
    {
        g_windowsKeyGuardEnabled = false;
        SaveConfig();
        ShowTrayNotification(L"FFKeyLock", Text(L"Win 键禁用失败。", L"Failed to disable the Windows key."));
        UpdateMainWindow();
        return false;
    }

    return true;
}

void DisableWindowsKeyGuard()
{
    if (g_keyboardHook)
    {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    g_keyDown.fill(false);
}

void ToggleWindowsKeyGuard()
{
    g_windowsKeyGuardEnabled = !g_windowsKeyGuardEnabled;
    for (const std::wstring& game : g_gameExeNames)
    {
        g_gameProfiles[game].lockWindowsKey = g_windowsKeyGuardEnabled;
    }
    ApplyWindowsKeyGuard();
    SaveConfig();
    UpdateMainWindow();

    ShowTrayNotification(
        L"FFKeyLock",
        g_windowsKeyGuardEnabled
            ? Text(L"Win 键已禁用。", L"Windows key is disabled.")
            : Text(L"Win 键已恢复。", L"Windows key is enabled."));
}
}
