#include "ProtectedProgramListView.h"

#include "ProtectedProgramCommands.h"
#include "../../AppState.h"
#include "../../Config.h"
#include "../../Localization.h"
#include "../../Platform/GdiUtils.h"
#include "../../ThemeManager.h"

#include <algorithm>
#include <cstdlib>

namespace FFKeyLock
{
namespace
{
int Scale(int value)
{
    return ThemeManager::Scale(value);
}

void DrawListText(HDC hdc, const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    GdiUtils::SelectObjectScope fontScope(hdc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(hdc, text.c_str(), -1, &rect, format);
}
}

void ProtectedProgramListView::SetBounds(RECT rect)
{
    bounds_ = rect;
    ClampSelection();
}

void ProtectedProgramListView::SetItems(std::vector<std::wstring>* names, std::unordered_map<std::wstring, std::wstring>* paths)
{
    names_ = names;
    paths_ = paths;
    ClampSelection();
}

void ProtectedProgramListView::Draw(HDC hdc, int scrollY)
{
    ClampSelection();

    RECT listRect = VisualBounds(scrollY);
    GdiUtils::FillRoundRect(hdc, listRect, ThemeManager::WindowColor(), ThemeManager::BorderColor(), Scale(6));

    RECT clipRect = listRect;
    InflateRect(&clipRect, -Scale(1), -Scale(1));
    HRGN previousClip = CreateRectRgn(0, 0, 0, 0);
    const int previousClipType = GetClipRgn(hdc, previousClip);
    HRGN listClip = CreateRectRgn(clipRect.left, clipRect.top, clipRect.right, clipRect.bottom);
    SelectClipRgn(hdc, listClip);
    DeleteObject(listClip);

    const int itemHeight = Scale(28);
    const int itemCount = names_ ? static_cast<int>(names_->size()) : 0;
    const int visibleRows = VisibleRows();
    if (itemCount == 0)
    {
        RECT emptyRect = clipRect;
        emptyRect.left += Scale(12);
        DrawListText(hdc, Text(L"尚未添加受保护程序", L"No protected programs yet"), emptyRect,
            ThemeManager::MutedTextColor(), ThemeManager::UiFont(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        for (int row = 0; row < visibleRows; ++row)
        {
            const int index = topIndex_ + row;
            if (index >= itemCount)
            {
                break;
            }

            RECT itemRect{
                clipRect.left,
                clipRect.top + row * itemHeight,
                clipRect.right,
                clipRect.top + (row + 1) * itemHeight
            };
            if (index == selectedIndex_)
            {
                HBRUSH selectedBrush = CreateSolidBrush(ThemeManager::ButtonHotColor());
                FillRect(hdc, &itemRect, selectedBrush);
                DeleteObject(selectedBrush);
            }

            RECT textRect = itemRect;
            textRect.left += Scale(10);
            textRect.right -= Scale(12);
            DrawListText(hdc, (*names_)[index], textRect, ThemeManager::TextColor(), ThemeManager::UiFont(),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            GdiUtils::DrawSeparator(hdc, itemRect.left + Scale(8), itemRect.right - Scale(8), itemRect.bottom - 1, ThemeManager::BorderColor());
        }
    }

    if (itemCount > visibleRows)
    {
        RECT track{ listRect.right - Scale(8), listRect.top + Scale(4), listRect.right - Scale(4), listRect.bottom - Scale(4) };
        HBRUSH trackBrush = CreateSolidBrush(ThemeManager::SurfaceColor());
        FillRect(hdc, &track, trackBrush);
        DeleteObject(trackBrush);

        const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
        const int thumbHeight = std::max(Scale(18), MulDiv(trackHeight, visibleRows, itemCount));
        const int maxTop = std::max(1, itemCount - visibleRows);
        const int thumbTop = track.top + MulDiv(trackHeight - thumbHeight, topIndex_, maxTop);
        RECT thumb{ track.left, thumbTop, track.right, thumbTop + thumbHeight };
        HBRUSH thumbBrush = CreateSolidBrush(ThemeManager::MutedTextColor());
        FillRect(hdc, &thumb, thumbBrush);
        DeleteObject(thumbBrush);
    }

    if (previousClipType == 1)
    {
        SelectClipRgn(hdc, previousClip);
    }
    else
    {
        SelectClipRgn(hdc, nullptr);
    }
    DeleteObject(previousClip);
}

bool ProtectedProgramListView::OnLeftDown(HWND panel, POINT clientPoint, int scrollY)
{
    const int index = HitTestItem(clientPoint, scrollY);
    return SelectItem(panel, index, scrollY);
}

bool ProtectedProgramListView::OnRightUp(HWND panel, POINT clientPoint, int scrollY)
{
    int index = HitTestItem(clientPoint, scrollY);
    if (index < 0 && clientPoint.x < 0 && clientPoint.y < 0)
    {
        index = selectedIndex_;
    }
    if (index < 0)
    {
        return false;
    }

    SelectItem(panel, index, scrollY);

    POINT screenPoint = clientPoint;
    if (clientPoint.x < 0 && clientPoint.y < 0)
    {
        RECT selectedRect = VisualBounds(scrollY);
        const int row = selectedIndex_ - topIndex_;
        selectedRect.top += row * Scale(28);
        selectedRect.bottom = selectedRect.top + Scale(28);
        screenPoint = POINT{ selectedRect.left + Scale(16), selectedRect.top + Scale(14) };
    }
    ClientToScreen(panel, &screenPoint);
    RoundedMenu::ShowAndDispatch(g_hWnd, screenPoint, ProtectedProgramCommands::BuildContextMenu());
    return true;
}

bool ProtectedProgramListView::OnWheel(HWND, POINT clientPoint, int scrollY, int delta)
{
    POINT logicalPoint{ clientPoint.x, clientPoint.y + scrollY };
    if (!PtInRect(&bounds_, logicalPoint))
    {
        return false;
    }

    const int itemCount = names_ ? static_cast<int>(names_->size()) : 0;
    const int visibleRows = VisibleRows();
    const int maxTop = std::max(0, itemCount - visibleRows);
    if (maxTop <= 0)
    {
        return false;
    }

    const int lines = std::max(1, std::abs(delta) / WHEEL_DELTA);
    topIndex_ += delta > 0 ? -lines : lines;
    topIndex_ = std::clamp(topIndex_, 0, maxTop);
    return true;
}

int ProtectedProgramListView::SelectedIndex() const
{
    return selectedIndex_;
}

std::wstring ProtectedProgramListView::SelectedName() const
{
    if (!names_ || selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(names_->size()))
    {
        return L"";
    }
    return (*names_)[selectedIndex_];
}

void ProtectedProgramListView::DeleteSelected()
{
    ClampSelection();
    const int selected = selectedIndex_;
    if (!names_ || !paths_ || selected < 0 || selected >= static_cast<int>(names_->size()))
    {
        MessageBoxW(g_hWnd,
            Text(L"请选择一个受保护程序。", L"Select an item in the protected program list first."),
            L"FFKeyLock",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring exeName = (*names_)[selected];
    names_->erase(names_->begin() + selected);
    paths_->erase(exeName);
    selectedIndex_ = std::min(selected, static_cast<int>(names_->size()) - 1);
    ClampSelection();
    SaveConfig();
}

void ProtectedProgramListView::Refresh()
{
    ClampSelection();
}

int ProtectedProgramListView::VisibleRows() const
{
    const int itemHeight = Scale(28);
    const int listHeight = bounds_.bottom - bounds_.top;
    return itemHeight > 0 ? std::max(1, listHeight / itemHeight) : 1;
}

int ProtectedProgramListView::HitTestItem(POINT point, int scrollY) const
{
    POINT logicalPoint{ point.x, point.y + scrollY };
    if (!PtInRect(&bounds_, logicalPoint))
    {
        return -1;
    }

    const int itemHeight = Scale(28);
    if (itemHeight <= 0)
    {
        return -1;
    }

    const int row = (logicalPoint.y - bounds_.top) / itemHeight;
    const int index = topIndex_ + row;
    return names_ && index >= 0 && index < static_cast<int>(names_->size()) ? index : -1;
}

bool ProtectedProgramListView::SelectItem(HWND panel, int index, int scrollY)
{
    if (!names_ || index < 0 || index >= static_cast<int>(names_->size()))
    {
        return false;
    }

    if (index == selectedIndex_)
    {
        return true;
    }

    const int oldIndex = selectedIndex_;
    selectedIndex_ = index;
    ClampSelection();
    InvalidateItem(panel, oldIndex, scrollY);
    InvalidateItem(panel, selectedIndex_, scrollY);
    return true;
}

void ProtectedProgramListView::ClampSelection()
{
    const int itemCount = names_ ? static_cast<int>(names_->size()) : 0;
    if (itemCount <= 0)
    {
        selectedIndex_ = -1;
        topIndex_ = 0;
        return;
    }

    selectedIndex_ = std::clamp(selectedIndex_, -1, itemCount - 1);
    const int visibleRows = VisibleRows();
    const int maxTop = std::max(0, itemCount - visibleRows);
    topIndex_ = std::clamp(topIndex_, 0, maxTop);
    if (selectedIndex_ >= 0)
    {
        if (selectedIndex_ < topIndex_)
        {
            topIndex_ = selectedIndex_;
        }
        else if (selectedIndex_ >= topIndex_ + visibleRows)
        {
            topIndex_ = std::min(maxTop, selectedIndex_ - visibleRows + 1);
        }
    }
}

void ProtectedProgramListView::InvalidateItem(HWND panel, int index, int scrollY) const
{
    if (!panel || index < 0 || index < topIndex_)
    {
        return;
    }

    const int row = index - topIndex_;
    if (row < 0 || row >= VisibleRows())
    {
        return;
    }

    RECT rect = VisualBounds(scrollY);
    rect.top += row * Scale(28);
    rect.bottom = rect.top + Scale(28);
    InvalidateRect(panel, &rect, FALSE);
}

RECT ProtectedProgramListView::VisualBounds(int scrollY) const
{
    RECT rect = bounds_;
    OffsetRect(&rect, 0, -scrollY);
    return rect;
}

std::wstring ProtectedProgramListView::PathForName(const std::wstring& name) const
{
    if (!paths_)
    {
        return L"";
    }
    const auto it = paths_->find(name);
    return it == paths_->end() ? L"" : it->second;
}
}
