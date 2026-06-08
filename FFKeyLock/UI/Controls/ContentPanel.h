#pragma once

#include "../../framework.h"

namespace FFKeyLock
{
namespace ContentPanel
{
using LayoutCallback = int (*)(HWND panel, int width, int height, int scrollY);
using StaticCtlColorCallback = HBRUSH (*)(HWND panel, HDC hdc, HWND control);

void Register(HINSTANCE instance);
HWND Create(HWND parent, HINSTANCE instance);
void SetLayoutCallback(HWND panel, LayoutCallback callback);
void SetStaticCtlColorCallback(HWND panel, StaticCtlColorCallback callback);
void Relayout(HWND panel);
void SetScrollY(HWND panel, int scrollY);
int ScrollY(HWND panel);
}
}
