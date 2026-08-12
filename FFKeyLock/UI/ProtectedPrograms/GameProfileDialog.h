#pragma once

#include "../../AppState.h"

namespace FFKeyLock::GameProfileDialog
{
bool Show(HWND owner, const std::wstring& exeName, const GameProfile& current, GameProfile& result);
}
