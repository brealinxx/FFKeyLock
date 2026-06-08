#pragma once

#include "../framework.h"

namespace FFKeyLock
{
namespace GdiUtils
{
class BufferedPaint
{
public:
    BufferedPaint(HDC target, const RECT& rect);
    ~BufferedPaint();

    BufferedPaint(const BufferedPaint&) = delete;
    BufferedPaint& operator=(const BufferedPaint&) = delete;

    HDC Dc() const;
    bool IsValid() const;

private:
    HDC target_ = nullptr;
    HDC memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    RECT rect_{};
    SIZE size_{};
};

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
