#pragma once

#include "framework.h"
#include "AppState.h"

#include <string>

namespace FFKeyLock
{
bool IsStartupEnabled();
void SetStartupEnabled(bool enabled);
bool InitializeForegroundDetection();
void ShutdownForegroundDetection();
void DetectForegroundGame();
void LeaveGameProtection();
bool IsGameChatControlKey(UINT virtualKey);
void HandleGameChatKey(UINT virtualKey, bool keyDown);
bool ShouldBlockWindowsKeyForActiveGame();
void ResumeGameProtectionAfterChatTimeout();
void AddProgramAsGame(HWND targetWindow);
bool AddGameExeName(std::wstring exeName);
GameProfile GetGameProfileForExe(const std::wstring& exeName);
void SetGameProfileForExe(const std::wstring& exeName, GameProfile profile);
}
