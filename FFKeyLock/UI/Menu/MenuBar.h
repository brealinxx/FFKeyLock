#pragma once
#include "../../framework.h"
#include <vector>
namespace FFKeyLock::UI
{
class MenuBar
{
public:
    HWND Create(HWND parent);
    HWND Window() const { return window_; }
    void SetMenu(HMENU menu); // takes ownership
    void RefreshTheme();
    bool Translate(MSG& message);
private:
    HWND window_ = nullptr, previousFocus_ = nullptr;
    HMENU pendingMenu_ = nullptr;
    HMENU menu_ = nullptr, activePopup_ = nullptr, selectedMenu_ = nullptr;
    std::vector<HWND> buttons_;
    int active_ = -1, next_ = -1;
    UINT selectedFlags_ = 0;
    bool tracking_ = false, altPressed_ = false;
    void FocusMenu();
    void Layout();
    void Open(int index);
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ButtonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK Filter(int, WPARAM, LPARAM);
    static thread_local MenuBar* tracked_;
};
}
