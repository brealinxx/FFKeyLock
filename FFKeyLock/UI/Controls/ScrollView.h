#pragma once
#include "../../framework.h"
namespace FFKeyLock::UI
{
// Fixed viewport; child coordinates are logical content coordinates. The
// viewport itself never moves or inherits a native non-client scrollbar.
class ScrollView
{
public:
    void Create(HWND viewport);
    HWND Content() const { return content_; }
    HWND Bar() const { return bar_; }
    int Position() const { return position_; }
    int ContentWidth();
    void SetExtent(int height);
    void ScrollTo(int position);
    void Reset();
    void OnWheel(WPARAM value);
    void OnScroll(WPARAM value);
    void EnsureVisible(HWND child);
    void TrackFocus(HWND child);
private:
    HWND viewport_ = nullptr, clip_ = nullptr, content_ = nullptr, bar_ = nullptr;
    UINT dpi_ = 96;
    int height_ = 0, position_ = 0, remainder_ = 0;
    static LRESULT CALLBACK ContentProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK FocusProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
};
}
