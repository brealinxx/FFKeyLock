#pragma once
#include "AppState.h"

namespace FFKeyLock
{
bool ApplyWindowsKeyGuard();
void DisableWindowsKeyGuard();
void ToggleWindowsKeyGuard();
void RefreshKeyboardPolicy();
bool KeyboardGuardHealthy();
bool EffectiveProfileWindowsKey(const GameProfile& profile);
void StartKeyboardTest(HWND editor, const GameProfile& profile);
void StopKeyboardTest();
}
