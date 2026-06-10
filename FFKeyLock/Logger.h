#pragma once

#include <string>

namespace FFKeyLock
{
enum class LogLevel
{
    Info,
    Warning,
    Error
};

void InitializeLogger();
void ShutdownLogger();
void Log(LogLevel level, const std::wstring& message);
std::wstring GetLogDirectory();
std::wstring GetLogFilePath();
}
