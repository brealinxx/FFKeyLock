#include "InputLanguage.h"

#include "AppState.h"

namespace FFKeyLock
{
namespace
{
void RequestInputLanguage(HWND targetWindow, HKL layout)
{
    if (!layout)
    {
        return;
    }

    ActivateKeyboardLayout(layout, KLF_SETFORPROCESS);
    if (!targetWindow)
    {
        return;
    }

    PostMessageW(targetWindow, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(layout));
}
}

void SwitchToEnglish(HWND targetWindow)
{
    HKL english = LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE);
    RequestInputLanguage(targetWindow ? targetWindow : GetForegroundWindow(), english);
}

void SwitchToChinese(HWND targetWindow)
{
    HKL chinese = LoadKeyboardLayoutW(L"00000804", KLF_ACTIVATE);
    RequestInputLanguage(targetWindow ? targetWindow : GetForegroundWindow(), chinese);
}

void ApplySavedLayout(HWND fallbackWindow)
{
    if (g_savedLayout)
    {
        RequestInputLanguage(IsWindow(g_savedWindow) ? g_savedWindow : fallbackWindow, g_savedLayout);
    }
}

void RestoreSavedLayout()
{
    if (g_savedLayout)
    {
        RequestInputLanguage(IsWindow(g_savedWindow) ? g_savedWindow : GetForegroundWindow(), g_savedLayout);
        g_savedLayout = nullptr;
        g_savedWindow = nullptr;
    }
}
}
