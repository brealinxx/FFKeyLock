#pragma once
#include "../../framework.h"
#include <memory>
#include <string>
#include <vector>
namespace FFKeyLock::UI
{
// Scoped decoration of a native HMENU. Windows owns menu navigation, capture,
// submenus and accessibility; all item and gutter pixels use our palette.
class PopupMenuTheme
{
public:
    PopupMenuTheme(HMENU menu, UINT dpi);
    ~PopupMenuTheme();
    PopupMenuTheme(const PopupMenuTheme&) = delete;
    PopupMenuTheme& operator=(const PopupMenuTheme&) = delete;
    static bool Draw(const DRAWITEMSTRUCT& item);
    static bool Measure(MEASUREITEMSTRUCT& item);
    static LRESULT MenuChar(WPARAM value, HMENU menu);
private:
    struct Entry;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::vector<std::pair<HMENU, HBRUSH>> backgrounds_;
    HBRUSH background_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    void Decorate(HMENU menu);
};
UINT ShowPopupMenu(HWND owner, HMENU menu, POINT point, UINT flags = TPM_RIGHTBUTTON);
}
