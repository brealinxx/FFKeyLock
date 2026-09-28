#include "PopupMenu.h"
#include "../../ThemeManager.h"
#include "../../Platform/DpiUtils.h"
#include "../../Platform/GdiUtils.h"
#include <algorithm>
#include <cwctype>
#include <shellscalingapi.h>
namespace FFKeyLock::UI
{
struct PopupMenuTheme::Entry
{
    HMENU menu = nullptr;
    UINT index = 0, type = 0, dpi = 96;
    ULONG_PTR data = 0;
    HFONT font = nullptr;
    std::wstring text;
    bool separator = false, submenu = false;
};
PopupMenuTheme::PopupMenuTheme(HMENU menu, UINT dpi) : dpi_(dpi)
{
    LOGFONTW font{}; GetObjectW(ThemeManager::UiFont(), sizeof(font), &font);
    font.lfHeight = -MulDiv(10, dpi, 72); font_ = CreateFontIndirectW(&font);
    background_ = CreateSolidBrush(ThemeManager::MenuBackgroundColor());
    Decorate(menu);
}
void PopupMenuTheme::Decorate(HMENU menu)
{
    MENUINFO background{sizeof(background), MIM_BACKGROUND}; GetMenuInfo(menu, &background);
    backgrounds_.push_back({menu, background.hbrBack}); background.hbrBack = background_; SetMenuInfo(menu, &background);
    for (int index = 0; index < GetMenuItemCount(menu); ++index)
    {
        MENUITEMINFOW info{sizeof(info)}; info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_SUBMENU | MIIM_STRING;
        GetMenuItemInfoW(menu, index, TRUE, &info);
        auto entry = std::make_unique<Entry>();
        entry->menu = menu; entry->index = index; entry->type = info.fType; entry->data = info.dwItemData;
        entry->dpi = dpi_; entry->font = font_; entry->separator = (info.fType & MFT_SEPARATOR) != 0; entry->submenu = info.hSubMenu != nullptr;
        entry->text.resize(info.cch + 1); info.dwTypeData = entry->text.data(); ++info.cch;
        GetMenuItemInfoW(menu, index, TRUE, &info); entry->text.resize(wcslen(entry->text.c_str()));
        if (info.hSubMenu) Decorate(info.hSubMenu);
        info.fMask = MIIM_FTYPE | MIIM_DATA; info.fType |= MFT_OWNERDRAW; info.dwItemData = reinterpret_cast<ULONG_PTR>(entry.get());
        SetMenuItemInfoW(menu, index, TRUE, &info); entries_.push_back(std::move(entry));
    }
}
PopupMenuTheme::~PopupMenuTheme()
{
    for (const auto& entry : entries_) if (IsMenu(entry->menu))
    {
        MENUITEMINFOW info{sizeof(info)}; info.fMask = MIIM_FTYPE | MIIM_DATA; info.fType = entry->type; info.dwItemData = entry->data;
        SetMenuItemInfoW(entry->menu, entry->index, TRUE, &info);
    }
    for (const auto& [menu, brush] : backgrounds_) if (IsMenu(menu))
    { MENUINFO info{sizeof(info), MIM_BACKGROUND}; info.hbrBack = brush; SetMenuInfo(menu, &info); }
    DeleteObject(background_); DeleteObject(font_);
}
bool PopupMenuTheme::Measure(MEASUREITEMSTRUCT& item)
{
    if (item.CtlType != ODT_MENU || !item.itemData) return false;
    const auto& entry = *reinterpret_cast<const Entry*>(item.itemData);
    auto s = [&](int n) { return DpiUtils::Scale(n, entry.dpi); };
    HDC dc = GetDC(nullptr); SIZE size{};
    { GdiUtils::SelectObjectScope font(dc, entry.font); GetTextExtentPoint32W(dc, entry.text.c_str(), static_cast<int>(entry.text.size()), &size); }
    ReleaseDC(nullptr, dc);
    item.itemWidth = entry.separator ? s(160) : std::max(s(160), static_cast<int>(size.cx) + s(64));
    item.itemHeight = s(entry.separator ? 9 : 32); return true;
}
bool PopupMenuTheme::Draw(const DRAWITEMSTRUCT& item)
{
    if (item.CtlType != ODT_MENU || !item.itemData) return false;
    // A menu may reuse its drawing DC for later items/highlight changes.
    // Remove our previous explicit glyph clip; the DC's system-visible region
    // still bounds drawing to the native menu window.
    SelectClipRgn(item.hDC, nullptr);
    const auto& entry = *reinterpret_cast<const Entry*>(item.itemData);
    auto s = [&](int n) { return DpiUtils::Scale(n, entry.dpi); };
    const bool selected = (item.itemState & ODS_SELECTED) != 0, disabled = (item.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    GdiUtils::BufferedPaint buffer(item.hDC, item.rcItem); HDC dc = buffer.Dc();
    HBRUSH brush = CreateSolidBrush(selected ? ThemeManager::MenuHoverColor() : ThemeManager::MenuBackgroundColor());
    FillRect(dc, &item.rcItem, brush); DeleteObject(brush);
    if (entry.separator)
        GdiUtils::DrawSeparator(dc, item.rcItem.left + s(12), item.rcItem.right - s(12), (item.rcItem.top + item.rcItem.bottom) / 2, ThemeManager::MenuSeparatorColor());
    else
    {
        const COLORREF text = disabled ? ThemeManager::DisabledTextColor() : selected && ThemeManager::HighContrast() ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ThemeManager::TextColor();
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, text); GdiUtils::SelectObjectScope font(dc, entry.font);
        RECT label = item.rcItem; label.left += s(30); label.right -= s(26);
        DrawTextW(dc, entry.text.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER | DT_LEFT | ((item.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
        if (item.itemState & ODS_CHECKED)
        { RECT check = item.rcItem; check.left += s(5); check.right = check.left + s(22); DrawTextW(dc, L"✓", 1, &check, DT_SINGLELINE | DT_CENTER | DT_VCENTER); }
        if (entry.submenu)
        { RECT arrow = item.rcItem; arrow.left = arrow.right - s(22); arrow.right -= s(4); DrawTextW(dc, L"›", 1, &arrow, DT_SINGLELINE | DT_CENTER | DT_VCENTER); }
    }
    buffer.Present();
    // HMENU draws a system submenu arrow after WM_DRAWITEM, even for an
    // owner-drawn row. This renderer already supplied that glyph; exclude its
    // gutter from the remaining system draw for this item (not the next item).
    if (entry.submenu)
        ExcludeClipRect(item.hDC, item.rcItem.right - s(26), item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
    return true;
}
LRESULT PopupMenuTheme::MenuChar(WPARAM value, HMENU menu)
{
    for (int i = 0; i < GetMenuItemCount(menu); ++i)
    {
        MENUITEMINFOW info{sizeof(info)}; info.fMask = MIIM_DATA | MIIM_STATE; GetMenuItemInfoW(menu, i, TRUE, &info);
        if (!info.dwItemData || (info.fState & MFS_DISABLED)) continue;
        const auto& text = reinterpret_cast<const Entry*>(info.dwItemData)->text;
        for (size_t pos = 0; pos + 1 < text.size(); ++pos)
            if (text[pos] == L'&')
            {
                if (text[pos + 1] == L'&') { ++pos; continue; }
                if (towupper(text[pos + 1]) == towupper(LOWORD(value))) return MAKELRESULT(i, MNC_EXECUTE);
            }
    }
    return MAKELRESULT(0, MNC_IGNORE);
}
UINT ShowPopupMenu(HWND owner, HMENU menu, POINT point, UINT flags)
{
    UINT x = GetDpiForWindow(owner), y = x;
    GetDpiForMonitor(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y);
    PopupMenuTheme theme(menu, x);
    return TrackPopupMenuEx(menu, flags | TPM_RETURNCMD, point.x, point.y, owner, nullptr);
}
}
