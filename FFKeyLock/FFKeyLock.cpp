// FFKeyLock.cpp : Application entry point.

#include "framework.h"

#include "AppState.h"
#include "Config.h"
#include "Localization.h"
#include "MainWindow.h"
#include "NotificationIdentity.h"
#include "TrayIcon.h"
#include "WindowsKeyGuard.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shcore.lib")

using namespace FFKeyLock;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&commonControls);
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\FFKeyLock.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existingWindow = FindWindowW(kAppName, kAppName);
        if (existingWindow)
        {
            ShowWindow(existingWindow, SW_SHOWNORMAL);
            SetForegroundWindow(existingWindow);
        }
        if (SUCCEEDED(comInit))
        {
            CoUninitialize();
        }
        return 0;
    }

    g_hInst = hInstance;
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    LoadConfig();
    InitializeNotificationIdentity();
    ApplyWindowsKeyGuard();

    RegisterMainWindowClass(hInstance);
    const UINT dpi = GetDpiForSystem();
    const int windowWidth = MulDiv(860, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    const int windowHeight = MulDiv(620, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    g_hWnd = CreateWindowExW(
        0,
        kAppName,
        kAppName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        hInstance,
        nullptr);
    if (!g_hWnd)
    {
        if (mutex)
        {
            CloseHandle(mutex);
        }
        if (SUCCEEDED(comInit))
        {
            CoUninitialize();
        }
        return 1;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    ShowTrayNotification(L"FFKeyLock", Text(L"已在后台运行。", L"FFKeyLock is running in the background."));

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex)
    {
        CloseHandle(mutex);
    }
    if (SUCCEEDED(comInit))
    {
        CoUninitialize();
    }
    return static_cast<int>(msg.wParam);
}
