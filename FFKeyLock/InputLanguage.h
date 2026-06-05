#pragma once

#include "framework.h"

namespace FFKeyLock
{
void SwitchToEnglish(HWND targetWindow);
void SwitchToChinese(HWND targetWindow);
void ApplySavedLayout(HWND fallbackWindow);
void RestoreSavedLayout();
}
