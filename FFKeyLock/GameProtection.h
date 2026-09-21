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
void PauseProtection(UINT milliseconds = 0);
void ResumeProtection();
std::wstring GetProgramPath(HWND window);
std::wstring GameDisplayName(const std::wstring& identity);
void NormalizeGameProfile(GameProfile& profile);
GameProfile NewGameProfile();
void ApplyCatPreset(GameProfile& profile);
std::wstring MatchGameIdentity(const std::wstring& processPath);
void ApplyForegroundGame(const std::wstring& identity, HWND window);
}
