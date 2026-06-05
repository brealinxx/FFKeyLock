#include "NotificationIdentity.h"

#include "AppState.h"
#include "Config.h"

#include <propkey.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <filesystem>

#pragma comment(lib, "Propsys.lib")

namespace FFKeyLock
{
namespace
{
constexpr wchar_t kAppUserModelId[] = L"Brelin.FFKeyLock";

std::wstring FindNotificationIconPath()
{
    const std::filesystem::path exePath(GetCurrentExePath());
    const std::filesystem::path exeFolder = exePath.parent_path();

    const std::filesystem::path candidates[] = {
        exeFolder / L"FFKeyLock.ico",
        exeFolder.parent_path() / L"FFKeyLock" / L"FFKeyLock.ico",
        exeFolder.parent_path().parent_path() / L"FFKeyLock" / L"FFKeyLock.ico",
    };

    for (const auto& candidate : candidates)
    {
        std::error_code ignored;
        if (std::filesystem::exists(candidate, ignored))
        {
            return candidate.wstring();
        }
    }

    return exePath.wstring();
}

std::wstring GetShortcutPath()
{
    PWSTR startMenu = nullptr;
    std::wstring shortcutPath;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_StartMenu, KF_FLAG_CREATE, nullptr, &startMenu)))
    {
        std::filesystem::path folder(startMenu);
        folder /= L"Programs";
        std::error_code ignored;
        std::filesystem::create_directories(folder, ignored);
        shortcutPath = (folder / L"FFKeyLock.lnk").wstring();
        CoTaskMemFree(startMenu);
    }
    return shortcutPath;
}

void CreateNotificationShortcut()
{
    const std::wstring shortcutPath = GetShortcutPath();
    if (shortcutPath.empty())
    {
        return;
    }

    IShellLinkW* shellLink = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink))))
    {
        return;
    }

    const std::wstring exePath = GetCurrentExePath();
    const std::wstring iconPath = FindNotificationIconPath();
    shellLink->SetPath(exePath.c_str());
    shellLink->SetArguments(L"");
    shellLink->SetIconLocation(iconPath.c_str(), 0);

    IPropertyStore* propertyStore = nullptr;
    if (SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(&propertyStore))))
    {
        PROPVARIANT value{};
        value.vt = VT_LPWSTR;
        value.pwszVal = const_cast<PWSTR>(kAppUserModelId);
        propertyStore->SetValue(PKEY_AppUserModel_ID, value);
        propertyStore->Commit();
        propertyStore->Release();
    }

    IPersistFile* persistFile = nullptr;
    if (SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(&persistFile))))
    {
        persistFile->Save(shortcutPath.c_str(), TRUE);
        persistFile->Release();
    }

    shellLink->Release();
}
}

void InitializeNotificationIdentity()
{
    SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);
    CreateNotificationShortcut();
}
}
