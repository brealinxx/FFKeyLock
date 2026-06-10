#include "Config.h"

#include "AppState.h"
#include "Localization.h"
#include "Logger.h"
#include "StringUtils.h"

#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace FFKeyLock
{
namespace
{
std::wstring GetStartMenuShortcutPath()
{
    PWSTR startMenu = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_StartMenu, KF_FLAG_CREATE, nullptr, &startMenu)))
    {
        std::filesystem::path folder(startMenu);
        folder /= L"Programs";
        std::error_code ignored;
        std::filesystem::create_directories(folder, ignored);
        path = (folder / L"FFKeyLock.lnk").wstring();
        CoTaskMemFree(startMenu);
    }
    return path;
}

std::wstring GetConfigPath()
{
    const std::wstring appDataDirectory = GetAppDataDirectory();
    return !appDataDirectory.empty()
        ? (std::filesystem::path(appDataDirectory) / L"config.ini").wstring()
        : (std::filesystem::path(GetCurrentExePath()).parent_path() / L"config.ini").wstring();
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

std::unordered_map<std::wstring, std::wstring> SplitGamePaths(const std::wstring& value)
{
    std::unordered_map<std::wstring, std::wstring> paths;
    size_t start = 0;
    while (start <= value.size())
    {
        const size_t end = value.find(L'|', start);
        std::wstring item = Trim(value.substr(start, end == std::wstring::npos ? end : end - start));
        const size_t separator = item.find(L'=');
        if (separator != std::wstring::npos)
        {
            std::wstring exe = ToLower(Trim(item.substr(0, separator)));
            std::wstring path = Trim(item.substr(separator + 1));
            if (!exe.empty() && !path.empty())
            {
                paths[exe] = path;
            }
        }

        if (end == std::wstring::npos)
        {
            break;
        }
        start = end + 1;
    }

    return paths;
}

std::wstring JoinGamePaths()
{
    std::wstring joined;
    for (const auto& game : g_gameExeNames)
    {
        const auto path = g_gameExePaths.find(game);
        if (path == g_gameExePaths.end() || path->second.empty())
        {
            continue;
        }

        if (!joined.empty())
        {
            joined += L"|";
        }
        joined += game;
        joined += L"=";
        joined += path->second;
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

std::wstring GetAppDataDirectory()
{
    PWSTR appData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &appData)))
    {
        std::filesystem::path folder(appData);
        folder /= kAppName;
        std::error_code ignored;
        std::filesystem::create_directories(folder, ignored);
        path = folder.wstring();
        CoTaskMemFree(appData);
    }
    return path;
}

void SaveConfig()
{
    Log(LogLevel::Info, L"Saving configuration.");
    WritePrivateProfileStringW(kConfigSection, kProtectionKey, g_protectionEnabled ? L"1" : L"0", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kAutoDetectKey, g_autoDetectEnabled ? L"1" : L"0", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kWindowsKeyGuardKey, g_windowsKeyGuardEnabled ? L"1" : L"0", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kNotificationsKey, g_notificationsEnabled ? L"1" : L"0", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kOverlayNotificationsKey, g_overlayNotificationsEnabled ? L"1" : L"0", g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kLanguageKey, IsEnglish() ? L"en" : L"zh", g_configPath.c_str());
    const wchar_t* theme = L"system";
    if (g_themePreference == ThemePreference::Light)
    {
        theme = L"light";
    }
    else if (g_themePreference == ThemePreference::Dark)
    {
        theme = L"dark";
    }
    WritePrivateProfileStringW(kConfigSection, kThemeKey, theme, g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kGamesKey, JoinGames().c_str(), g_configPath.c_str());
    WritePrivateProfileStringW(kConfigSection, kGamePathsKey, JoinGamePaths().c_str(), g_configPath.c_str());
}

void LoadConfig()
{
    g_configPath = GetConfigPath();
    Log(LogLevel::Info, L"Loading configuration from: " + g_configPath);
    g_protectionEnabled = GetPrivateProfileIntW(kConfigSection, kProtectionKey, 1, g_configPath.c_str()) != 0;
    g_autoDetectEnabled = GetPrivateProfileIntW(kConfigSection, kAutoDetectKey, 1, g_configPath.c_str()) != 0;
    g_windowsKeyGuardEnabled = GetPrivateProfileIntW(kConfigSection, kWindowsKeyGuardKey, 0, g_configPath.c_str()) != 0;
    g_notificationsEnabled = GetPrivateProfileIntW(kConfigSection, kNotificationsKey, 1, g_configPath.c_str()) != 0;
    g_overlayNotificationsEnabled = GetPrivateProfileIntW(kConfigSection, kOverlayNotificationsKey, 1, g_configPath.c_str()) != 0;

    wchar_t language[16]{};
    GetPrivateProfileStringW(kConfigSection, kLanguageKey, L"zh", language, static_cast<DWORD>(std::size(language)), g_configPath.c_str());
    g_language = (_wcsicmp(language, L"en") == 0 || _wcsicmp(language, L"english") == 0)
        ? UiLanguage::English
        : UiLanguage::Chinese;

    wchar_t theme[16]{};
    GetPrivateProfileStringW(kConfigSection, kThemeKey, L"system", theme, static_cast<DWORD>(std::size(theme)), g_configPath.c_str());
    if (_wcsicmp(theme, L"dark") == 0)
    {
        g_themePreference = ThemePreference::Dark;
    }
    else if (_wcsicmp(theme, L"light") == 0)
    {
        g_themePreference = ThemePreference::Light;
    }
    else
    {
        g_themePreference = ThemePreference::System;
    }

    wchar_t buffer[8192]{};
    GetPrivateProfileStringW(kConfigSection, kGamesKey, L"", buffer, static_cast<DWORD>(std::size(buffer)), g_configPath.c_str());
    g_gameExeNames = SplitGames(buffer);

    wchar_t pathBuffer[16384]{};
    GetPrivateProfileStringW(kConfigSection, kGamePathsKey, L"", pathBuffer, static_cast<DWORD>(std::size(pathBuffer)), g_configPath.c_str());
    g_gameExePaths = SplitGamePaths(pathBuffer);

    if (g_gameExeNames.empty())
    {
        g_gameExeNames = { L"r5apex.exe", L"apex.exe", L"legend.exe", L"mir2.exe", L"mir3.exe" };
        SaveConfig();
    }
}

bool ClearLocalDataAndRegistry()
{
    Log(LogLevel::Warning, L"Clearing local data and registry entries.");

    RegDeleteKeyValueW(HKEY_CURRENT_USER, kStartupRunKey, kAppName);

    const std::wstring shortcutPath = GetStartMenuShortcutPath();
    if (!shortcutPath.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(shortcutPath, ignored);
    }

    const std::wstring appDataDirectory = GetAppDataDirectory();
    ShutdownLogger();

    bool success = true;
    if (!appDataDirectory.empty())
    {
        std::error_code error;
        std::filesystem::remove_all(appDataDirectory, error);
        success = !error;
    }

    g_configPath.clear();
    g_gameExeNames.clear();
    g_gameExePaths.clear();
    return success;
}
}
