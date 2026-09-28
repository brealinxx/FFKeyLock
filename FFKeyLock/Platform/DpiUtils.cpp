#include "DpiUtils.h"

namespace FFKeyLock
{
namespace DpiUtils
{
UINT GetWindowDpi(HWND hwnd)
{
    return hwnd ? GetDpiForWindow(hwnd) : USER_DEFAULT_SCREEN_DPI;
}

int Scale(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
}

int ScaleForWindow(HWND hwnd, int value)
{
    return Scale(value, GetWindowDpi(hwnd));
}
}
}
