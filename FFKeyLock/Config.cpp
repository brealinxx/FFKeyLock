#include "Config.h"

#include "AppState.h"
#include "Localization.h"
#include "StringUtils.h"

#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace FFKeyLock
{
namespace
{
std::wstring GetConfigPath()
{
    PWSTR appData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &appData)))
    {
        std::filesystem::path folder(appData);
        folder /= kAppName;
        std::error_code ignored;
        std::filesystem::create_directories(folder, ignored);
        path = (folder / L"config.ini").wstring();
        CoTaskMemFree(appData);
    }

    if (path.empty())
    {
        path = (std::filesystem::path(GetCurrentExePath()).parent_path() / L"config.ini").wstring();
    }

    return path;
}

std::vector<std::wstring> SplitGames(const std::wstring& value)
{
    std::vector<std::wstring> games;
    size_t start = 0;
    while (start <= value.size())
    {
        const size_t end = value.find(L'|', start);
        std::wstring item = Trim(value.substr(start, end == std::wstring::npos ? end : end - start));
        if (!item.empty())
        {
            item = ToLower(item);
            if (std::find(games.begin(), games.end(), item) == games.end())
            {
                games.push_back(item);
            }
        }

        if (end == std::wstring::npos)
        {
            break;
        }
        start = end + 1;
    }

    return games;
}

std::wstring JoinGames()
{
    std::wstring joined;
    for (const auto& game : g_gameExeNames)
    {
        if (!joined.empty())
        {
            joined += L"|";
        }
        joined += game;
    }
    return joined;
}
}

std::wstring GetCurrentExePath()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (size == path.size())
    {
        path.resize(path.size() * 2, L'\0');
        size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }

    path.resize(size);
    return path;
}

void SaveConfig()
{
    WritePrivateProfileStringW(kConfigSection, kProtectionKey, g_protectionEnabled ? L"1" : L"0", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kAutoDetectKey, g_autoDetectEnabled ? L"1" : L"0", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kWindowsKeyGuardKey, g_windowsKeyGuardEnabled ? L"1" : L"0", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kLanguageKey, IsEnglish() ? L"en" : L"zh", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kGamesKey, JoinGames().c_str(), g_configPath.c_str());
}

void LoadConfig()
{
    g_configPath = GetConfigPath();
    g_protectionEnabled = GetPrivateProfileIntW(kConfigSection, kProtectionKey, 1, g_configPath.c_str()) != 0;
    g_autoDetectEnabled = GetPrivateProfileIntW(kConfigSection, kAutoDetectKey, 1, g_configPath.c_str()) != 0;
    g_windowsKeyGuardEnabled = GetPrivateProfileIntW(kConfigSection, kWindowsKeyGuardKey, 0, g_configPath.c_str()) != 0;

    wchar_t language[16]{};
    GetPrivateProfileStringW(kConfigSection, kLanguageKey, L"zh", language, static_cast<DWORD>(std::size(language)), g_configPath.c_str());
    g_language = (_wcsicmp(language, L"en") == 0 || _wcsicmp(language, L"english") == 0)
        ? UiLanguage::English
        : UiLanguage::Chinese;

    wchar_t buffer[8192]{};
    GetPrivateProfileStringW(kConfigSection, kGamesKey, L"", buffer, static_cast<DWORD>(std::size(buffer)), g_configPath.c_str());
    g_gameExeNames = SplitGames(buffer);

    if (g_gameExeNames.empty())
    {
        g_gameExeNames = { L"r5apex.exe", L"apex.exe", L"legend.exe", L"mir2.exe", L"mir3.exe" };
        SaveConfig();
    }
}
}
