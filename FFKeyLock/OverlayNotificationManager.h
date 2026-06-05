#pragma once

#include "framework.h"

#include <string>

namespace FFKeyLock
{
class OverlayNotificationManager
{
public:
    static void ShowInfo(const std::wstring& title, const std::wstring& message);
    static void ShowSuccess(const std::wstring& title, const std::wstring& message);
    static void ShowWarning(const std::wstring& title, const std::wstring& message);
    static void ShowError(const std::wstring& title, const std::wstring& message);
    static void Shutdown();
};
}
