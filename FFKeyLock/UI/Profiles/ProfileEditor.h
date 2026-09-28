#pragma once

#include "../../AppState.h"

namespace FFKeyLock::ProfileEditor
{
HWND CreateEditor(HWND parent);
void LoadEditor(HWND editor, const std::wstring& identity, const GameProfile& profile);
bool ReadEditor(HWND editor, GameProfile& profile);
bool IsDirty(HWND editor);
void RefreshEditorTheme(HWND editor);
std::wstring KeyName(UINT key);
constexpr UINT WM_PROFILE_DIRTY = WM_APP + 40;
constexpr UINT WM_TEST_KEY = WM_APP + 41;
}
