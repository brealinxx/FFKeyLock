#pragma once

#include "framework.h"

namespace FFKeyLock
{
HICON LoadAppIcon(int width, int height);
void ShowTrayNotification(const wchar_t* title, const wchar_t* message);
void AddTrayIcon();
void RemoveTrayIcon();
}
