#include "WindowsKeyGuard.h"

#include "AppState.h"
#include "Config.h"
#include "Localization.h"
#include "MainWindow.h"
#include "TrayIcon.h"

namespace FFKeyLock
{
namespace
{
HHOOK g_keyboardHook = nullptr;

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_windowsKeyGuardEnabled)
    {
        const auto* keyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (keyboard && (keyboard->vkCode == VK_LWIN || keyboard->vkCode == VK_RWIN))
        {
            return 1;
        }
    }

    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}
}

bool ApplyWindowsKeyGuard()
{
    if (!g_windowsKeyGuardEnabled)
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
}

void ToggleWindowsKeyGuard()
{
    g_windowsKeyGuardEnabled = !g_windowsKeyGuardEnabled;
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
