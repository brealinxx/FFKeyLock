#pragma once

#include "../../framework.h"

#include <string>
#include <vector>

namespace FFKeyLock
{
struct RoundedMenuItem
{
    UINT id = 0;
    std::wstring text;
    std::wstring shortcut;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
    bool title = false;
    std::vector<RoundedMenuItem> submenu;
};

class RoundedMenu
{
public:
    static UINT Show(HWND owner, POINT anchor, const std::vector<RoundedMenuItem>& items, bool alignBottom = false);
    static void ShowAndDispatch(HWND owner, POINT anchor, const std::vector<RoundedMenuItem>& items, bool alignBot tom = false);
};
}
