#include "GdiUtils.h"

namespace FFKeyLock
{
namespace GdiUtils
{
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
