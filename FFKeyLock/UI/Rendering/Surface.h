#pragma once
#include "../../framework.h"
namespace FFKeyLock::UI
{
// Every owned window paints its complete invalid client region. WM_ERASEBKGND
// is suppressed; neither transparency nor old window bits are a paint source.
LRESULT PaintSurface(HWND window, UINT message, WPARAM dc = 0);
// A panel owns its outer corners; inset child windows own the scrolling area.
void MarkPanel(HWND window);
HBRUSH BackgroundBrush(HWND window);
COLORREF BackgroundColor(HWND window);
RECT PanelInterior(HWND window);
void InvalidateSurface(HWND window, bool immediate = false);
}
