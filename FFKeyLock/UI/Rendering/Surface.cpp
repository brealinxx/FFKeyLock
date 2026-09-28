#include "Surface.h"
#include "../../ThemeManager.h"
#include "../../Platform/DpiUtils.h"
#include "../../Platform/GdiUtils.h"
#include <algorithm>
namespace FFKeyLock::UI
{
namespace
{
constexpr wchar_t kPanel[] = L"FFKeyLock.Panel";
LRESULT CALLBACK PanelProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_NCDESTROY)
    {
        RemovePropW(window, kPanel);
        RemoveWindowSubclass(window, PanelProc, id);
    }
    return DefSubclassProc(window, message, w, l);
}
bool InPanel(HWND window)
{
    for (HWND current = window; current; current = GetParent(current))
        if (GetPropW(current, kPanel)) return true;
    return false;
}
}
void MarkPanel(HWND window)
{
    SetPropW(window, kPanel, reinterpret_cast<HANDLE>(1));
    SetWindowSubclass(window, PanelProc, 1, 0);
}
HBRUSH BackgroundBrush(HWND window) { return InPanel(window) ? ThemeManager::SurfaceBrush() : ThemeManager::WindowBrush(); }
COLORREF BackgroundColor(HWND window) { return InPanel(window) ? ThemeManager::SurfaceColor() : ThemeManager::WindowColor(); }
RECT PanelInterior(HWND window)
{
    RECT r{}; GetClientRect(window, &r);
    const int inset = DpiUtils::ScaleForWindow(window, ThemeManager::PanelInset);
    const int x = std::min(inset, std::max(0, static_cast<int>(r.right) / 2));
    const int y = std::min(inset, std::max(0, static_cast<int>(r.bottom) / 2));
    InflateRect(&r, -x, -y); return r;
}
LRESULT PaintSurface(HWND window, UINT message, WPARAM dc)
{
    if (message == WM_ERASEBKGND) return TRUE;
    PAINTSTRUCT paint{};
    HDC target = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(dc) : BeginPaint(window, &paint);
    RECT client{}; GetClientRect(window, &client);
    GdiUtils::BufferedPaint buffer(target, client);
    if (GetPropW(window, kPanel))
    {
        FillRect(buffer.Dc(), &client, BackgroundBrush(GetParent(window)));
        GdiUtils::FillRoundRect(buffer.Dc(), client, ThemeManager::SurfaceColor(), ThemeManager::PanelBorderColor(),
            DpiUtils::ScaleForWindow(window, ThemeManager::PanelRadius * 2));
    }
    else FillRect(buffer.Dc(), &client, BackgroundBrush(window));
    buffer.Present();
    if (message != WM_PRINTCLIENT) EndPaint(window, &paint);
    return 0;
}
void InvalidateSurface(HWND window, bool immediate)
{
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME |
        (immediate ? RDW_UPDATENOW : 0));
}
}
