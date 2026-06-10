#include "ProtectedProgramCommands.h"

#include "../../Localization.h"
#include "../../Resource.h"

#include <shellapi.h>

namespace FFKeyLock
{
namespace ProtectedProgramCommands
{
namespace
{
RoundedMenuItem MenuItem(UINT id, const std::wstring& text, const std::wstring& shortcut = L"", bool checked = false, bool enabled = true)
{
    RoundedMenuItem item{};
    item.id = id;
    item.text = text;
    item.shortcut = shortcut;
    item.checked = checked;
    item.enabled = enabled;
    return item;
}

RoundedMenuItem MenuSeparator()
{
    RoundedMenuItem item{};
    item.separator = true;
    item.enabled = false;
    return item;
}
}

std::vector<RoundedMenuItem> BuildContextMenu()
{
    return {
        MenuItem(IDM_COPY_GAME_NAME, Text(L"复制名称", L"Copy name"), L"Ctrl+C"),
        MenuItem(IDM_OPEN_GAME_FOLDER, Text(L"跳转到该程序文件夹", L"Open program folder")),
        MenuSeparator(),
        MenuItem(IDM_DELETE_SELECTED_GAME, Text(L"删除选中", L"Delete selected")),
    };
}

void CopyNameToClipboard(HWND owner, const std::wstring& name)
{
    if (name.empty() || !OpenClipboard(owner))
    {
        return;
    }

    EmptyClipboard();
    const size_t bytes = (name.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory)
    {
        void* data = GlobalLock(memory);
        if (data)
        {
            CopyMemory(data, name.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory)
    {
        GlobalFree(memory);
    }
    CloseClipboard();
}

void OpenProgramFolder(HWND owner, const std::wstring& exeName, const std::wstring& path)
{
    if (exeName.empty() || path.empty())
    {
        MessageBoxW(owner,
            Text(L"这个条目只保存了程序名称，没有保存文件路径。请通过“浏览添加受保护程序...”重新添加一次即可记录路径。",
                L"This item only has an executable name, not a saved file path. Add it again with Browse to record its path."),
            L"FFKeyLock",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring argument = L"/select,\"" + path + L"\"";
    ShellExecuteW(owner, L"open", L"explorer.exe", argument.c_str(), nullptr, SW_SHOWNORMAL);
}
}
}
