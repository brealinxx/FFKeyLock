// FFKeyLock.cpp : Application entry point.

#include "framework.h"

#include "AppState.h"
#include "Config.h"
#include "Localization.h"
#include "MainWindow.h"
#include "TrayIcon.h"
#include "WindowsKeyGuard.h"

using namespace FFKeyLock;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\FFKeyLock.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existingWindow = FindWindowW(kAppName, kAppName);
        if (existingWindow)
        {
            ShowWindow(existingWindow, SW_SHOWNORMAL);
            SetForegroundWindow(existingWindow);
        }
        return 0;
    }

    g_hInst = hInstance;
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    LoadConfig();
    ApplyWindowsKeyGuard();

    RegisterMainWindowClass(hInstance);
    g_hWnd = CreateWindowExW(
        0,
        kAppName,
        kAppName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        500,
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
    return static_cast<int>(msg.wParam);
}
