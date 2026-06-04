#pragma once

#include "framework.h"

namespace FFKeyLock
{
void UpdateMainWindow();
void ShowMainWindow();
void RememberExternalForegroundWindow(HWND hwnd);
HWND GetCommandTargetWindow();
void ShowTrayMenu();
ATOM RegisterMainWindowClass(HINSTANCE hInstance);
}
