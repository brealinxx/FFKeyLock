#pragma once

#include "../../framework.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace FFKeyLock
{
class ProtectedProgramListView
{
public:
    void SetBounds(RECT rect);
    void SetItems(std::vector<std::wstring>* names, std::unordered_map<std::wstring, std::wstring>* paths);
    void Draw(HDC hdc, int scrollY);
    bool OnLeftDown(HWND panel, POINT clientPoint, int scrollY);
    bool OnRightUp(HWND panel, POINT clientPoint, int scrollY);
    bool OnWheel(HWND panel, POINT clientPoint, int scrollY, int delta);
    int SelectedIndex() const;
    std::wstring SelectedName() const;
    void DeleteSelected();
    void Refresh();

private:
    int VisibleRows() const;
    int HitTestItem(POINT point, int scrollY) const;
    bool SelectItem(HWND panel, int index, int scrollY);
    void ClampSelection();
    void InvalidateItem(HWND panel, int index, int scrollY) const;
    RECT VisualBounds(int scrollY) const;
    std::wstring PathForName(const std::wstring& name) const;

    RECT bounds_{};
    std::vector<std::wstring>* names_ = nullptr;
    std::unordered_map<std::wstring, std::wstring>* paths_ = nullptr;
    int selectedIndex_ = -1;
    int topIndex_ = 0;
};
}
