#pragma once

#include "../../framework.h"

namespace FFKeyLock
{
namespace ToggleSwitch
{
constexpr wchar_t ClassName[] = L"FFKeyLockToggleSwitch";
constexpr UINT StateChangedNotification = 1;

void Register(HINSTANCE instance);
HWND Create(HWND parent, int id, bool checked, int x, int y, int width, int height);
bool IsChecked(HWND hwnd);
void SetChecked(HWND hwnd, bool checked);
}
}
