#pragma once
#include "../../framework.h"
namespace FFKeyLock::UI
{
void ApplyTheme(HWND root);
HBRUSH HandleCtlColor(HWND parent, HDC dc, HWND control);
bool DrawCheckBox(const NMCUSTOMDRAW& item, LRESULT& result);
void DrawButton(const DRAWITEMSTRUCT& item);
}
