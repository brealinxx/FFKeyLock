#include "GdiUtils.h"

namespace FFKeyLock
{
namespace GdiUtils
{
BufferedPaint::BufferedPaint(HDC target, const RECT& rect)
    : BufferedPaint(target, rect, rect)
{
}

BufferedPaint::BufferedPaint(HDC target, const RECT& rect, const RECT& flushRect)
    : target_(target), rect_(rect), flushRect_(flushRect)
{
    size_.cx = rect_.right - rect_.left;
    size_.cy = rect_.bottom - rect_.top;
    RECT clippedFlush{};
    IntersectRect(&clippedFlush, &flushRect_, &rect_);
    flushRect_ = clippedFlush;
    if (!target_ || size_.cx <= 0 || size_.cy <= 0)
    {
        return;
    }

    memory_ = CreateCompatibleDC(target_);
    if (!memory_)
    {
        return;
    }

    bitmap_ = CreateCompatibleBitmap(target_, size_.cx, size_.cy);
    if (!bitmap_)
    {
        DeleteDC(memory_);
        memory_ = nullptr;
        return;
    }

    oldBitmap_ = SelectObject(memory_, bitmap_);
    SetViewportOrgEx(memory_, -rect_.left, -rect_.top, nullptr);
}

BufferedPaint::~BufferedPaint()
{
    if (memory_ && oldBitmap_)
    {
        SelectObject(memory_, oldBitmap_);
    }
    if (bitmap_)
    {
        DeleteObject(bitmap_);
    }
    if (memory_)
    {
        DeleteDC(memory_);
    }
}

HDC BufferedPaint::Dc() const
{
    return IsValid() ? memory_ : target_;
}

bool BufferedPaint::IsValid() const
{
    return memory_ && bitmap_;
}

void BufferedPaint::Present()
{
    if (presented_)
    {
        return;
    }
    presented_ = true;
    if (!target_ || !memory_ || !bitmap_)
    {
        return;
    }

    SetViewportOrgEx(memory_, 0, 0, nullptr);
    const int flushWidth = flushRect_.right - flushRect_.left;
    const int flushHeight = flushRect_.bottom - flushRect_.top;
    if (flushWidth > 0 && flushHeight > 0)
    {
        BitBlt(target_, flushRect_.left, flushRect_.top, flushWidth, flushHeight,
            memory_, flushRect_.left - rect_.left, flushRect_.top - rect_.top, SRCCOPY);
    }
}

SelectObjectScope::SelectObjectScope(HDC hdc, HGDIOBJ object)
    : hdc_(hdc), oldObject_(hdc && object ? SelectObject(hdc, object) : nullptr)
{
}

SelectObjectScope::~SelectObjectScope()
{
    if (hdc_ && oldObject_)
    {
        SelectObject(hdc_, oldObject_);
    }
}

void FillRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border, int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    SelectObjectScope brushScope(hdc, brush);
    SelectObjectScope penScope(hdc, pen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawSeparator(HDC hdc, int left, int right, int y, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    SelectObjectScope penScope(hdc, pen);
    MoveToEx(hdc, left, y, nullptr);
    LineTo(hdc, right, y);
    DeleteObject(pen);
}
}
}
