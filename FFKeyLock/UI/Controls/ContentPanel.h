#pragma once

#include "../../framework.h"

namespace FFKeyLock
{
namespace ContentPanel
{
using LayoutCallback = int (*)(HWND panel, int width, int height, int scrollY);
using PaintCallback = void (*)(HWND panel, HDC hdc, const RECT& client, int scrollY);
using ButtonHitTestCallback = int (*)(HWND panel, POINT point, int scrollY);
using ButtonStateCallback = void (*)(HWND panel, int id, bool hot, bool pressed);
using MouseClickCallback = void (*)(HWND panel, POINT point, int scrollY);
using MouseWheelCallback = bool (*)(HWND panel, POINT point, int scrollY, int delta);

void Register(HINSTANCE instance);
HWND Create(HWND parent, HINSTANCE instance);
void SetLayoutCallback(HWND panel, LayoutCallback callback);
void SetPaintCallback(HWND panel, PaintCallback callback);
void SetButtonHitTestCallback(HWND panel, ButtonHitTestCallback callback);
void SetButtonStateCallback(HWND panel, ButtonStateCallback callback);
void SetMouseClickCallback(HWND panel, MouseClickCallback callback);
void SetMouseWheelCallback(HWND panel, MouseWheelCallback callback);
void Relayout(HWND panel);
void SetScrollY(HWND panel, int scrollY);
int ScrollY(HWND panel);
}
}
