#include "Logger.h"

#include "AppState.h"

#include <shlobj.h>
#include <strsafe.h>

#include <filesystem>
#include <mutex>

namespace FFKeyLock
{
namespace
{
std::mutex g_logMutex;
HANDLE g_logFile = INVALID_HANDLE_VALUE;
std::wstring g_logDirectory;
std::wstring g_logFilePath;

std::wstring AppDataDirectory()
{
    PWSTR appData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &appData)))
    {
        path = (std::filesystem::path(appData) / kAppName).wstring();
        CoTaskMemFree(appData);
    }
    return path;
}

const wchar_t* LevelText(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Warning:
        return L"WARN";
    case LogLevel::Error:
        return L"ERROR";
    case LogLevel::Info:
    default:
        return L"INFO";
    }
}

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
    {
        return {};
    }

    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), bytes, nullptr, nullptr);
    return result;
}
}

void InitializeLogger()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        return;
    }

    std::filesystem::path directory = AppDataDirectory();
    if (directory.empty())
    {
        directory = std::filesystem::current_path() / kAppName;
    }
    directory /= L"logs";

    std::error_code ignored;
    std::filesystem::create_directories(directory, ignored);
    g_logDirectory = directory.wstring();
    g_logFilePath = (directory / L"ffkeylock.log").wstring();

    g_logFile = CreateFileW(
        g_logFilePath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}

void ShutdownLogger()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}

void Log(LogLevel level, const std::wstring& message)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t prefix[96]{};
    StringCchPrintfW(
        prefix,
        std::size(prefix),
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] ",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds,
        LevelText(level));

    std::string line = ToUtf8(std::wstring(prefix) + message + L"\r\n");
    if (!line.empty())
    {
        DWORD written = 0;
        WriteFile(g_logFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
}

std::wstring GetLogDirectory()
{
    if (g_logDirectory.empty())
    {
        std::filesystem::path directory = AppDataDirectory();
        if (!directory.empty())
        {
            directory /= L"logs";
            g_logDirectory = directory.wstring();
        }
    }
    return g_logDirectory;
}

std::wstring GetLogFilePath()
{
    if (g_logFilePath.empty())
    {
        std::filesystem::path directory = GetLogDirectory();
        if (!directory.empty())
        {
            g_logFilePath = (directory / L"ffkeylock.log").wstring();
        }
    }
    return g_logFilePath;
}
}
