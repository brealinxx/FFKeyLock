#include "Config.h"

#include "AppState.h"
#include "Localization.h"
#include "Logger.h"
#include "StringUtils.h"
#include "GameProtection.h"

#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <fstream>

namespace FFKeyLock
{
namespace
{
bool g_preserveInvalidConfig = false;
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


GameProfile DefaultGameProfile()
{
    GameProfile profile{};
    profile.lockWindowsKey = g_windowsKeyGuardEnabled;
    profile.showNotifications = g_notificationsEnabled || g_overlayNotificationsEnabled;
    return profile;
}

std::vector<std::wstring> SplitDelimited(const std::wstring& value, wchar_t delimiter)
{
    std::vector<std::wstring> fields;
    size_t start = 0;
    while (start <= value.size())
    {
        const size_t end = value.find(delimiter, start);
        fields.push_back(value.substr(start, end == std::wstring::npos ? end : end - start));
        if (end == std::wstring::npos)
        {
            break;
        }
        start = end + 1;
    }
    return fields;
}

std::wstring JoinChatKeys(const std::vector<UINT>& keys)
{
    std::wstring result;
    for (UINT key : keys)
    {
        if (!result.empty())
        {
            result += L",";
        }
        result += std::to_wstring(key);
    }
    return result;
}

std::vector<UINT> ParseChatKeys(const std::wstring& value)
{
    std::vector<UINT> keys;
    for (const std::wstring& field : SplitDelimited(value, L','))
    {
        wchar_t* end = nullptr;
        const unsigned long parsed = wcstoul(field.c_str(), &end, 10);
        if (end != field.c_str() && parsed > 0 && parsed < 256)
        {
            const UINT key = static_cast<UINT>(parsed);
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
            {
                keys.push_back(key);
            }
        }
    }
    if (keys.empty())
    {
        keys.push_back(VK_RETURN);
    }
    return keys;
}

void NormalizeProfile(GameProfile& profile)
{
    profile.restoreTimeoutMs = std::clamp(profile.restoreTimeoutMs, 1000U, 300000U);
    std::vector<UINT> normalized;
    for (UINT key : profile.chatKeys)
    {
        if (key > 0 && key < 256 && std::find(normalized.begin(), normalized.end(), key) == normalized.end())
        {
            normalized.push_back(key);
        }
    }
    profile.chatKeys = normalized.empty() ? std::vector<UINT>{ VK_RETURN } : std::move(normalized);
}

std::unordered_map<std::wstring, GameProfile> SplitGameProfiles(const std::wstring& value)
{
    std::unordered_map<std::wstring, GameProfile> profiles;
    for (const std::wstring& record : SplitDelimited(value, L'|'))
    {
        const std::vector<std::wstring> fields = SplitDelimited(record, L'^');
        if (fields.size() < 7)
        {
            continue;
        }

        const std::wstring exeName = ToLower(Trim(fields[0]));
        if (exeName.empty())
        {
            continue;
        }

        GameProfile profile = DefaultGameProfile();
        profile.targetLanguage = _wcsicmp(fields[1].c_str(), L"zh") == 0
            ? ProtectedInputLanguage::Chinese
            : ProtectedInputLanguage::English;
        profile.chatKeys = ParseChatKeys(fields[2]);
        profile.chatMode = _wcsicmp(fields[3].c_str(), L"hold") == 0
            ? ChatActivationMode::Hold
            : ChatActivationMode::Toggle;
        profile.restoreTimeoutMs = static_cast<UINT>(wcstoul(fields[4].c_str(), nullptr, 10));
        profile.lockWindowsKey = wcstoul(fields[5].c_str(), nullptr, 10) != 0;
        profile.showNotifications = wcstoul(fields[6].c_str(), nullptr, 10) != 0;
        NormalizeProfile(profile);
        profiles[exeName] = std::move(profile);
    }
    return profiles;
}


std::wstring ReadValue(const std::wstring& path, const std::wstring& section, const wchar_t* key, const wchar_t* fallback = L"")
{
    std::vector<wchar_t> buffer(512);
    for (;;)
    {
        const DWORD size = GetPrivateProfileStringW(section.c_str(), key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
        if (size < buffer.size() - 1) return std::wstring(buffer.data(), size);
        if (buffer.size() >= 65536) return L"";
        buffer.resize(buffer.size() * 2);
    }
}

bool ParseKeysStrict(const std::wstring& text, std::vector<UINT>& result, bool allowEmpty)
{
    result.clear();
    if (text.empty()) return allowEmpty;
    for (const auto& field : SplitDelimited(text, L','))
    {
        wchar_t* end = nullptr;
        const unsigned long value = wcstoul(field.c_str(), &end, 10);
        if (end == field.c_str() || *end || value == 0 || value >= 256) return false;
        result.push_back(static_cast<UINT>(value));
    }
    return true;
}

bool ReadLibrary(const std::wstring& path, std::vector<std::wstring>& names,
    std::unordered_map<std::wstring, std::wstring>& paths,
    std::unordered_map<std::wstring, GameProfile>& profiles)
{
    const auto countText = ReadValue(path, L"Library", L"Count");
    wchar_t* countEnd = nullptr;
    const long count = wcstol(countText.c_str(), &countEnd, 10);
    if (countEnd == countText.c_str() || *countEnd) return false;
    if (GetPrivateProfileIntW(L"Settings", L"Version", 0, path.c_str()) != 2 || count < 0 || count > 4096) return false;
    for (int index = 0; index < count; ++index)
    {
        const std::wstring section = L"Game." + std::to_wstring(index);
        const std::wstring identity = ToLower(Trim(ReadValue(path, section, L"Identity")));
        if (identity.empty() || profiles.contains(identity) || identity.find_first_of(L"\r\n") != std::wstring::npos ||
            ToLower(std::filesystem::path(identity).extension().wstring()) != L".exe") return false;
        bool flagsValid = true;
        auto flag = [&](const wchar_t* key, bool fallback) {
            const auto value = ReadValue(path, section, key, fallback ? L"1" : L"0");
            if (value != L"0" && value != L"1") flagsValid = false;
            return value == L"1";
        };
        GameProfile profile{};
        profile.enabled = flag(L"Enabled", true);
        profile.inputGuardEnabled = flag(L"InputGuard", true);
        profile.keyGuardEnabled = flag(L"KeyGuard", true);
        profile.inheritWindowsKey = flag(L"InheritWindowsKey", false);
        profile.lockWindowsKey = flag(L"LockWindowsKey", false);
        profile.showNotifications = flag(L"Notifications", true);
        if (!flagsValid) return false;
        const auto language = ReadValue(path, section, L"Language", L"en");
        const auto mode = ReadValue(path, section, L"ChatMode", L"toggle");
        if ((language != L"en" && language != L"zh") || (mode != L"hold" && mode != L"toggle")) return false;
        profile.targetLanguage = ReadValue(path, section, L"Language", L"en") == L"zh" ? ProtectedInputLanguage::Chinese : ProtectedInputLanguage::English;
        profile.chatMode = ReadValue(path, section, L"ChatMode", L"toggle") == L"hold" ? ChatActivationMode::Hold : ChatActivationMode::Toggle;
        profile.restoreTimeoutMs = GetPrivateProfileIntW(section.c_str(), L"TimeoutMs", 12000, path.c_str());
        if (profile.restoreTimeoutMs < 1000 || profile.restoreTimeoutMs > 300000 ||
            !ParseKeysStrict(ReadValue(path, section, L"ChatKeys", L"13"), profile.chatKeys, false) ||
            !ParseKeysStrict(ReadValue(path, section, L"BlockedKeys"), profile.blockedKeys, true)) return false;
        NormalizeGameProfile(profile);
        names.push_back(identity);
        const auto programPath = ReadValue(path, section, L"Path");
        if (!programPath.empty() && (!std::filesystem::path(programPath).is_absolute() ||
            ToLower(std::filesystem::path(programPath).extension().wstring()) != L".exe")) return false;
        if (!programPath.empty()) paths[identity] = programPath;
        profiles[identity] = std::move(profile);
    }
    return true;
}

bool WriteConfiguration(const std::wstring& path)
{
    if (path.empty() || g_gameExeNames.size() > 4096) return false;
    const std::wstring temporary = path + L".tmp";
    // Build once, write once, then replace. No per-field synchronous disk writes.
    std::wstring contents(1, static_cast<wchar_t>(0xfeff));
    std::wstring previousSection;
    bool ok = true;
    auto write = [&](const std::wstring& section, const wchar_t* key, const std::wstring& value) {
        if (value.find_first_of(L"\r\n") != std::wstring::npos) { ok = false; return; }
        if (section != previousSection) { contents += L"\r\n[" + section + L"]\r\n"; previousSection = section; }
        contents += std::wstring(key) + L"=" + value + L"\r\n";
    };
    auto flag = [&](const std::wstring& section, const wchar_t* key, bool value) { write(section, key, value ? L"1" : L"0"); };
    write(L"Settings", L"Version", L"2");
    flag(L"Settings", kProtectionKey, g_protectionEnabled);
    flag(L"Settings", kAutoDetectKey, g_autoDetectEnabled);
    flag(L"Settings", kWindowsKeyGuardKey, g_windowsKeyGuardEnabled);
    write(L"Settings", kWindowsKeyGuardScopeKey, g_windowsKeyGuardScope == WindowsKeyGuardScope::Always ? L"always" : L"protected");
    flag(L"Settings", kNotificationsKey, g_notificationsEnabled);
    flag(L"Settings", kOverlayNotificationsKey, g_overlayNotificationsEnabled);
    write(L"Settings", kLanguageKey, IsEnglish() ? L"en" : L"zh");
    write(L"Settings", kThemeKey, g_themePreference == ThemePreference::Dark ? L"dark" : g_themePreference == ThemePreference::Light ? L"light" : L"system");
    write(L"Settings", L"EmergencyKey", std::to_wstring(g_emergencyKey));
    write(L"Library", L"Count", std::to_wstring(g_gameExeNames.size()));
    for (size_t index = 0; index < g_gameExeNames.size(); ++index)
    {
        const auto& identity = g_gameExeNames[index];
        const auto section = L"Game." + std::to_wstring(index);
        const GameProfile profile = GetGameProfileForExe(identity);
        write(section, L"Identity", identity);
        const auto found = g_gameExePaths.find(identity);
        write(section, L"Path", found == g_gameExePaths.end() ? L"" : found->second);
        flag(section, L"Enabled", profile.enabled);
        flag(section, L"InputGuard", profile.inputGuardEnabled);
        flag(section, L"KeyGuard", profile.keyGuardEnabled);
        flag(section, L"InheritWindowsKey", profile.inheritWindowsKey);
        flag(section, L"LockWindowsKey", profile.lockWindowsKey);
        flag(section, L"Notifications", profile.showNotifications);
        write(section, L"Language", profile.targetLanguage == ProtectedInputLanguage::Chinese ? L"zh" : L"en");
        write(section, L"ChatMode", profile.chatMode == ChatActivationMode::Hold ? L"hold" : L"toggle");
        write(section, L"TimeoutMs", std::to_wstring(profile.restoreTimeoutMs));
        write(section, L"ChatKeys", JoinChatKeys(profile.chatKeys));
        write(section, L"BlockedKeys", JoinChatKeys(profile.blockedKeys));
    }
    if (!ok) return false;
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const DWORD bytes = static_cast<DWORD>(contents.size() * sizeof(wchar_t));
    ok = WriteFile(file, contents.data(), bytes, &written, nullptr) && written == bytes;
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (ok)
    {
        ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
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

bool SaveConfig()
{
    if (g_preserveInvalidConfig)
    {
        if (!CopyFileW(g_configPath.c_str(), (g_configPath + L".invalid.bak").c_str(), TRUE))
        {
            g_configError = Text(L"原配置需要备份，但备份失败。请先导出当前配置或处理已有备份。", L"Could not preserve the original configuration. Export current profiles or resolve the existing backup first.");
            return false;
        }
        g_preserveInvalidConfig = false;
    }
    const bool ok = WriteConfiguration(g_configPath);
    g_configError = ok ? L"" : Text(L"配置保存失败，修改仅在本次运行生效。请检查目录权限或磁盘空间。", L"Could not save settings. Changes apply only to this session; check permissions and disk space.");
    if (!ok) Log(LogLevel::Error, g_configError);
    return ok;
}

void LoadConfig(const std::wstring& path)
{
    g_configError.clear();
    g_preserveInvalidConfig = false;
    g_configPath = path.empty() ? GetConfigPath() : path;
    Log(LogLevel::Info, L"Loading configuration from: " + g_configPath);
    g_protectionEnabled = GetPrivateProfileIntW(kConfigSection, kProtectionKey, 1, g_configPath.c_str()) != 0;
    g_autoDetectEnabled = GetPrivateProfileIntW(kConfigSection, kAutoDetectKey, 1, g_configPath.c_str()) != 0;
    g_windowsKeyGuardEnabled = GetPrivateProfileIntW(kConfigSection, kWindowsKeyGuardKey, 0, g_configPath.c_str()) != 0;
    wchar_t windowsKeyScope[16]{};
    GetPrivateProfileStringW(kConfigSection, kWindowsKeyGuardScopeKey, L"protected", windowsKeyScope,
        static_cast<DWORD>(std::size(windowsKeyScope)), g_configPath.c_str());
    g_windowsKeyGuardScope = _wcsicmp(windowsKeyScope, L"always") == 0
        ? WindowsKeyGuardScope::Always
        : WindowsKeyGuardScope::ProtectedForeground;
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

    g_emergencyKey = GetPrivateProfileIntW(kConfigSection, L"EmergencyKey", VK_BACK, g_configPath.c_str());
    if (g_emergencyKey != VK_BACK && g_emergencyKey != VK_END && g_emergencyKey != VK_HOME) g_emergencyKey = VK_BACK;
    g_gameExeNames.clear(); g_gameExePaths.clear(); g_gameProfiles.clear();
    if (GetPrivateProfileIntW(kConfigSection, L"Version", 0, g_configPath.c_str()) >= 2)
    {
        if (!ReadLibrary(g_configPath, g_gameExeNames, g_gameExePaths, g_gameProfiles))
        {
            g_gameExeNames.clear(); g_gameExePaths.clear(); g_gameProfiles.clear();
            g_preserveInvalidConfig = true;
            g_configError = Text(L"配置文件损坏或版本不支持。原文件未修改，请从备份导入。", L"Invalid or unsupported configuration. Original file is unchanged; import a backup.");
        }
        return;
    }
    const bool firstRun = GetFileAttributesW(g_configPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    wchar_t buffer[8192]{};
    GetPrivateProfileStringW(kConfigSection, kGamesKey, L"", buffer, static_cast<DWORD>(std::size(buffer)), g_configPath.c_str());
    g_gameExeNames = SplitGames(buffer);

    wchar_t pathBuffer[16384]{};
    GetPrivateProfileStringW(kConfigSection, kGamePathsKey, L"", pathBuffer, static_cast<DWORD>(std::size(pathBuffer)), g_configPath.c_str());
    g_gameExePaths = SplitGamePaths(pathBuffer);

    wchar_t profileBuffer[32768]{};
    GetPrivateProfileStringW(kConfigSection, kGameProfilesKey, L"", profileBuffer,
        static_cast<DWORD>(std::size(profileBuffer)), g_configPath.c_str());
    g_gameProfiles = SplitGameProfiles(profileBuffer);

    bool configMigrated = false;
    if (firstRun && g_gameExeNames.empty())
    {
        g_gameExeNames = { L"r5apex.exe", L"apex.exe", L"legend.exe", L"mir2.exe", L"mir3.exe" };
        configMigrated = true;
    }

    for (const std::wstring& game : g_gameExeNames)
    {
        if (!g_gameProfiles.contains(game))
        {
            g_gameProfiles[game] = DefaultGameProfile();
            configMigrated = true;
        }
    }
    if (configMigrated || !firstRun)
    {
        if (!firstRun && !CopyFileW(g_configPath.c_str(), (g_configPath + L".v1.bak").c_str(), TRUE) && GetLastError() != ERROR_FILE_EXISTS)
            g_configError = Text(L"旧配置备份失败，尚未迁移文件。", L"Could not back up the old configuration. Migration was not written.");
        else SaveConfig();
    }
}

bool ExportProfiles(const std::wstring& path)
{
    return WriteConfiguration(path);
}

bool ImportProfiles(const std::wstring& path)
{
    std::vector<std::wstring> names;
    std::unordered_map<std::wstring, std::wstring> paths;
    std::unordered_map<std::wstring, GameProfile> profiles;
    if (!ReadLibrary(path, names, paths, profiles)) return false;
    const auto oldNames = g_gameExeNames;
    const auto oldPaths = g_gameExePaths;
    const auto oldProfiles = g_gameProfiles;
    for (const auto& identity : names)
    {
        if (!g_gameProfiles.contains(identity)) g_gameExeNames.push_back(identity);
        g_gameProfiles[identity] = profiles.at(identity);
        if (paths.contains(identity)) g_gameExePaths[identity] = paths.at(identity);
    }
    if (SaveConfig()) return true;
    g_gameExeNames = oldNames;
    g_gameExePaths = oldPaths;
    g_gameProfiles = oldProfiles;
    return false;
}

bool ClearLocalDataAndRegistry()
{
    Log(LogLevel::Warning, L"Clearing local data and registry entries.");

    if (g_portableMode)
    {
        const bool removed = DeleteFileW(g_configPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
        g_configPath.clear();
        return removed;
    }

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
    g_gameProfiles.clear();
    return success;
}
}
