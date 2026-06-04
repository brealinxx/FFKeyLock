#pragma once

#include <string>

namespace FFKeyLock
{
std::wstring GetCurrentExePath();
void SaveConfig();
void LoadConfig();
}
