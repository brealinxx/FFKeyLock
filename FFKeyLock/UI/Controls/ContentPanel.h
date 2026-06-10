#pragma once

#include "../../framework.h"

namespace FFKeyLock
{
namespace ContentPanel
{
using LayoutCallback = int (*)(HWND panel, int width, int height, int scrollY);
using PaintCallback = void (*)(HWND panel, HDC hdc, const RECT& client, int scrollY);
using EnsureLayoutCallback = void (*)(HWND panel);
using ButtonHitTestCallback = int (*)(HWND panel, POINT point, int scrollY);
using ButtonStateCallback = void (*)(HWND panel, int id, bool hot, bool pressed);
using MouseDownCallback = bool (*)(HWND panel, POINT point, int scrollY);
using MouseClickCallback = void (*)(HWND panel, POINT point, int scrollY);
using RightClickCallback = void (*)(HWND panel, POINT point, int scrollY);
using MouseWheelCallback = bool (*)(HWND panel, POINT point, int scrollY, int delta);

void Register(HINSTANCE instance);
HWND Create(HWND parent, HINSTANCE instance);
void SetLayoutCallback(HWND panel, LayoutCallback callback);
void SetPaintCallback(HWND panel, PaintCallback callback);
void SetEnsureLayoutCallback(HWND panel, EnsureLayoutCallback callback);
void SetButtonHitTestCallback(HWND panel, ButtonHitTestCallback callback);
void SetButtonStateCallback(HWND panel, ButtonStateCallback callback);
void SetMouseDownCallback(HWND panel, MouseDownCallback callback);
void SetMouseClickCallback(HWND panel, MouseClickCallback callback);
void SetRightClickCallback(HWND panel, RightClickCallback callback);
void SetMouseWheelCallback(HWND panel, MouseWheelCallback callback);
void Relayout(HWND panel);
void SetScrollY(HWND panel, int scrollY);
int ScrollY(HWND panel);
}
}
