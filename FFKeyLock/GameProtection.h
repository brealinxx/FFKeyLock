#pragma once

#include "framework.h"

#include <string>

namespace FFKeyLock
{
bool IsStartupEnabled();
void SetStartupEnabled(bool enabled);
void DetectForegroundGame();
void LeaveGameProtection();
void ToggleGameChatInputMode();
void ResumeGameProtectionAfterChatTimeout();
void AddProgramAsGame(HWND targetWindow);
bool AddGameExeName(std::wstring exeName);
}
