#pragma once

#include <string>

namespace FFKeyLock
{
std::wstring GetCurrentExePath();
std::wstring GetAppDataDirectory();
void SaveConfig();
void LoadConfig();
bool ClearLocalDataAndRegistry();
}
