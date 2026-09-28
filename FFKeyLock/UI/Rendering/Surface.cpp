#include "Surface.h"
#include "../../ThemeManager.h"
namespace FFKeyLock::UI
{
LRESULT PaintSurface(HWND window, UINT message, WPARAM dc)
{
    if (message == WM_ERASEBKGND) return TRUE;
    PAINTSTRUCT paint{};
    HDC target = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(dc) : BeginPaint(window, &paint);
    RECT client{}; GetClientRect(window, &client);
    FillRect(target, &client, ThemeManager::WindowBrush());
    if (message != WM_PRINTCLIENT) EndPaint(window, &paint);
    return 0;
}
void InvalidateSurface(HWND window, bool immediate)
{
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME |
        (immediate ? RDW_UPDATENOW : 0));
}
}
