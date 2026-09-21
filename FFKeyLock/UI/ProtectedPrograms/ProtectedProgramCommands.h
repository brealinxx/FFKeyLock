#pragma once

#include "../../framework.h"

#include <string>
#include <vector>

namespace FFKeyLock
{
namespace ProtectedProgramCommands
{
void CopyNameToClipboard(HWND owner, const std::wstring& name);
void OpenProgramFolder(HWND owner, const std::wstring& exeName, const std::wstring& path);
}
}
