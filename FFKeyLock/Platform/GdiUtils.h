#pragma once

#include "../framework.h"

namespace FFKeyLock
{
namespace GdiUtils
{
class SelectObjectScope
{
public:
    SelectObjectScope(HDC hdc, HGDIOBJ object);
    ~SelectObjectScope();

    SelectObjectScope(const SelectObjectScope&) = delete;
    SelectObjectScope& operator=(const SelectObjectScope&) = delete;

private:
    HDC hdc_ = nullptr;
    HGDIOBJ oldObject_ = nullptr;
};

void FillRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border, int radius);
void DrawSeparator(HDC hdc, int left, int right, int y, COLORREF color);
}
}
