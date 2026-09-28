// FFKeyLock.cpp : Application entry point.

#include "framework.h"

#include "AppState.h"
#include "Config.h"
#include "Localization.h"
#include "Logger.h"
#include "MainWindow.h"
#include "NotificationIdentity.h"
#include "TrayIcon.h"
#include "WindowsKeyGuard.h"
#include <shellapi.h>
#include <filesystem>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shcore.lib")

using namespace FFKeyLock;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR commandLine, _In_ int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&commonControls);
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wstring configOverride;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i + 1 < argc; ++i)
        if (wcscmp(argv[i], L"--config") == 0) configOverride = argv[++i];
    if (argv) LocalFree(argv);
    if (configOverride.empty())
    {
        const auto portable = std::filesystem::path(GetCurrentExePath()).parent_path() / L"portable.ini";
        if (GetFileAttributesW(portable.c_str()) != INVALID_FILE_ATTRIBUTES) configOverride = portable.wstring();
    }
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
        CloseHandle(mutex);
        return 0;
    }

    g_hInst = hInstance;
    g_portableMode = !configOverride.empty();
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    if (configOverride.empty()) InitializeLogger();
    Log(LogLevel::Info, L"Application starting.");
    LoadConfig(configOverride);
    if (configOverride.empty()) InitializeNotificationIdentity();

    RegisterMainWindowClass(hInstance);
    const UINT dpi = GetDpiForSystem();
    const int windowWidth = MulDiv(1080, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    const int windowHeight = MulDiv(820, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
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
        Log(LogLevel::Error, L"Failed to create main window.");
        ShutdownLogger();
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

    UpdateMainWindow();
    const bool background = commandLine && wcsstr(commandLine, L"--background");
    ShowWindow(g_hWnd, background ? SW_HIDE : nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!TranslateMainMessage(msg) && !IsDialogMessageW(g_hWnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (mutex)
    {
        CloseHandle(mutex);
    }
    if (SUCCEEDED(comInit))
    {
        CoUninitialize();
    }
    Log(LogLevel::Info, L"Application exiting.");
    ShutdownLogger();
    return static_cast<int>(msg.wParam);
}
