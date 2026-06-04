#include "TrayIcon.h"

#include "AppState.h"
#include "Resource.h"

#include <shellapi.h>
#include <strsafe.h>

namespace FFKeyLock
{
HICON LoadAppIcon(int width, int height)
{
    HICON icon = reinterpret_cast<HICON>(LoadImageW(
        g_hInst,
        MAKEINTRESOURCEW(IDI_FFKEYLOCK),
        IMAGE_ICON,
        width,
        height,
        LR_DEFAULTCOLOR));

    return icon ? icon : LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_FFKEYLOCK));
}

void ShowTrayNotification(const wchar_t* title, const wchar_t* message)
{
    if (!g_trayIcon)
    {
        g_trayIcon = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO | NIF_ICON;
    nid.hIcon = g_trayIcon;
    nid.hBalloonIcon = g_trayIcon;
    StringCchCopyW(nid.szInfoTitle, std::size(nid.szInfoTitle), title);
    StringCchCopyW(nid.szInfo, std::size(nid.szInfo), message);
    nid.dwInfoFlags = NIIF_USER;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void AddTrayIcon()
{
    if (!g_trayIcon)
    {
        g_trayIcon = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_trayIcon;
    StringCchCopyW(nid.szTip, std::size(nid.szTip), L"FFKeyLock");
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void RemoveTrayIcon()
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (g_trayIcon)
    {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }
}
}
