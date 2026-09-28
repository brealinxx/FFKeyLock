#pragma once

#include "../framework.h"

namespace FFKeyLock
{
namespace DpiUtils
{
UINT GetWindowDpi(HWND hwnd);
int Scale(int value, UINT dpi);
int ScaleForWindow(HWND hwnd, int value);
}
}
