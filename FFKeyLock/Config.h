#pragma once

#include <string>

namespace FFKeyLock
{
std::wstring GetCurrentExePath();
std::wstring GetAppDataDirectory();
bool SaveConfig();
void LoadConfig(const std::wstring& path = L"");
bool ExportProfiles(const std::wstring& path);
bool ImportProfiles(const std::wstring& path);
bool ClearLocalDataAndRegistry();
}
