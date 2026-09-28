#include "ProtectedProgramCommands.h"

#include "../../Localization.h"
#include "../../Resource.h"

#include <shellapi.h>

namespace FFKeyLock
{
namespace ProtectedProgramCommands
{
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
