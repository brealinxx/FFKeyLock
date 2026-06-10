#pragma once

#include "../../UI/Menu/RoundedMenu.h"

#include <string>
#include <vector>

namespace FFKeyLock
{
namespace ProtectedProgramCommands
{
std::vector<RoundedMenuItem> BuildContextMenu();
void CopyNameToClipboard(HWND owner, const std::wstring& name);
void OpenProgramFolder(HWND owner, const std::wstring& exeName, const std::wstring& path);
}
}
