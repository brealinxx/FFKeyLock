#include "StringUtils.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace FFKeyLock
{
std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::wstring Trim(std::wstring value)
{
    const wchar_t* whitespace = L" \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::wstring::npos)
    {
        return L"";
    }

    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

std::wstring GetExeNameFromPath(const std::wstring& path)
{
    return ToLower(std::filesystem::path(path).filename().wstring());
}
}
