#pragma once

#include "../../framework.h"

namespace FFKeyLock
{
namespace RoundedButton
{
constexpr wchar_t ClassName[] = L"FFKeyLockRoundedButton";

void Register(HINSTANCE instance);
HWND Create(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height);
}
}
