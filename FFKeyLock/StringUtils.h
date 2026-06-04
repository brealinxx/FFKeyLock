#pragma once

#include <string>

namespace FFKeyLock
{
std::wstring ToLower(std::wstring value);
std::wstring Trim(std::wstring value);
std::wstring GetExeNameFromPath(const std::wstring& path);
}
