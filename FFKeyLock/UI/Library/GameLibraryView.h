#pragma once
#include "../../framework.h"
#include <string>
#include <vector>
#include <unordered_map>
namespace FFKeyLock::UI
{
struct LibraryItem { std::wstring title, detail, iconPath; };
class GameLibraryView
{
public:
    HWND Create(HWND parent, int id);
    HWND Window() const { return window_; }
    HWND List() const { return list_; }
    void SetItems(std::vector<LibraryItem> items);
    void Select(int index);
    int Selected() const;
    void RefreshTheme();
private:
    HWND window_ = nullptr, list_ = nullptr, bar_ = nullptr;
    int id_ = 0, rowHeight_ = 1, wheel_ = 0;
    bool updating_ = false;
    std::vector<LibraryItem> items_;
    std::unordered_map<std::wstring, HICON> icons_;
    void Layout();
    void SyncScroll();
    void ScrollTo(int row);
    void Draw(const DRAWITEMSTRUCT& item);
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ListProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
};
}
