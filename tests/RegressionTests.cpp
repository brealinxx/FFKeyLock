#include "../FFKeyLock/AppState.h"
#include "../FFKeyLock/Config.h"
#include "../FFKeyLock/GameProtection.h"
#include "../FFKeyLock/KeyPolicy.h"
#include "../FFKeyLock/WindowsKeyGuard.h"
#include "../FFKeyLock/ThemeManager.h"
#include "../FFKeyLock/UI/Profiles/ProfileEditor.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace FFKeyLock;
int checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) throw std::runtime_error("Failed: " #expr); } while (false)
void Reset()
{
    g_gameExeNames.clear(); g_gameExePaths.clear(); g_gameProfiles.clear();
    g_notificationsEnabled = false; g_overlayNotificationsEnabled = false;
    g_protectionEnabled = true; g_autoDetectEnabled = true; g_protectionPaused = false;
    g_windowsKeyGuardEnabled = false; g_emergencyKey = VK_BACK;
}
void PressTests()
{
    KeyPressTracker tracker;
    CHECK(tracker.Process(VK_F1,true,false,true));
    CHECK(tracker.Process(VK_F1,true,false,false)); // Held while focus changes.
    CHECK(tracker.Process(VK_F1,false,true,false));
    CHECK(!tracker.Process(VK_F1,true,false,false));
    CHECK(!tracker.Process(VK_F1,true,false,true)); // Already delivered down keeps its up.
    CHECK(!tracker.Process(VK_F1,false,true,true));
    CHECK(tracker.Process(VK_F1,true,false,true));
    CHECK(tracker.Process(VK_F1,false,true,true));
    tracker.Seed(VK_LWIN,true);
    CHECK(!tracker.Process(VK_LWIN,true,false,true));
    CHECK(!tracker.Process(VK_LWIN,false,true,true));
    CHECK(tracker.Process(VK_LWIN,true,false,true));
    CHECK(tracker.Process(VK_PAUSE,true,false,true,true));
    CHECK(!tracker.Process(VK_PAUSE,true,false,false,true));
    CHECK(!tracker.Process(999,true,false,true));
    CHECK(tracker.Process(VK_SNAPSHOT,true,false,true));
    CHECK(tracker.Process(VK_SNAPSHOT,false,true,false));
    CHECK(tracker.Process(VK_SCROLL,true,false,true));
    CHECK(tracker.Process(VK_SCROLL,false,true,true));
    CHECK(tracker.Process(VK_SNAPSHOT,false,true,true,false,true));
    CHECK(!tracker.Process(VK_SNAPSHOT,false,true,false,false,true));
}
void ConfigTests(const std::filesystem::path& directory)
{
    const auto config = (directory / L"roundtrip.ini").wstring();
    Reset(); g_configPath = config;
    const std::wstring first = L"c:\\游戏甲\\game.exe", second = L"d:\\游戏乙\\game.exe";
    GameProfile cat = NewGameProfile(); ApplyCatPreset(cat); cat.inputGuardEnabled = false; cat.inheritWindowsKey = false;
    cat.blockedKeys.erase(std::find(cat.blockedKeys.begin(),cat.blockedKeys.end(),UINT(VK_F2)));
    g_gameExeNames = {first,second}; g_gameExePaths[first] = first; g_gameExePaths[second] = second;
    g_gameProfiles[first] = cat; g_gameProfiles[second] = GameProfile{};
    CHECK(SaveConfig());
    CHECK(MatchGameIdentity(L"C:\\游戏甲\\GAME.EXE") == first);
    CHECK(MatchGameIdentity(L"D:\\游戏乙\\game.exe") == second);
    CHECK(MatchGameIdentity(L"E:\\game.exe").empty());
    Reset(); LoadConfig(config);
    CHECK(g_gameExeNames.size() == 2);
    CHECK(g_gameExePaths.at(first) == first);
    CHECK(!g_gameProfiles.at(first).inputGuardEnabled);
    CHECK(g_gameProfiles.at(first).blockedKeys.size() == 14);
    CHECK(std::find(g_gameProfiles.at(first).blockedKeys.begin(),g_gameProfiles.at(first).blockedKeys.end(),UINT(VK_F2)) == g_gameProfiles.at(first).blockedKeys.end());
    const auto saved = g_gameProfiles.at(first).blockedKeys;
    ToggleWindowsKeyGuard();
    CHECK(g_gameProfiles.at(first).blockedKeys == saved);
    CHECK(!g_gameProfiles.at(first).lockWindowsKey);
    DisableWindowsKeyGuard();
    const auto exportPath = (directory / L"export.ini").wstring(); CHECK(ExportProfiles(exportPath));
    Reset(); g_configPath = config; CHECK(SaveConfig()); LoadConfig(config);
    CHECK(g_gameExeNames.empty()); // Deliberately empty libraries stay empty.
    CHECK(ImportProfiles(exportPath)); CHECK(g_gameExeNames.size() == 2);
    const auto broken = directory / L"broken.ini";
    { std::ofstream file(broken); file << "[Settings]\nVersion=2\n[Library]\nCount=1\n[Game.0]\nIdentity=bad.exe\nBlockedKeys=112,nope\n"; }
    const auto oldNames = g_gameExeNames; CHECK(!ImportProfiles(broken.wstring())); CHECK(g_gameExeNames == oldNames);
    g_configPath = (directory / L"missing-directory" / L"settings.ini").wstring();
    CHECK(!SaveConfig()); CHECK(!g_configError.empty());
    g_configPath = config;
    GameProfile dirty; dirty.blockedKeys = {0,999,VK_LCONTROL,VK_RMENU,VK_BACK,VK_F3,VK_F3};
    NormalizeGameProfile(dirty); CHECK(dirty.blockedKeys == std::vector<UINT>{VK_F3});
    ApplyCatPreset(dirty); CHECK(dirty.blockedKeys.size() == 15);
    g_gameProfiles[L"legacy.exe"] = GameProfile{}; g_gameExeNames.push_back(L"legacy.exe");
    g_gameExePaths[L"legacy.exe"] = L"C:\\old\\legacy.exe";
    CHECK(MatchGameIdentity(L"C:\\old\\legacy.exe") == L"legacy.exe");
    CHECK(MatchGameIdentity(L"D:\\other\\legacy.exe").empty());
    CHECK(AddGameExeName(L"C:\\old\\legacy.exe"));
    CHECK(!g_gameProfiles.contains(L"legacy.exe"));
    CHECK(g_gameProfiles.contains(L"c:\\old\\legacy.exe"));
    Reset(); g_configPath = config;
    for (int index = 0; index < 300; ++index)
    {
        const auto identity = L"c:\\游戏库\\game-" + std::to_wstring(index) + L".exe";
        g_gameExeNames.push_back(identity); g_gameExePaths[identity] = identity;
        g_gameProfiles[identity] = cat;
    }
    CHECK(SaveConfig()); CHECK(std::filesystem::file_size(config) > 32768);
    LoadConfig(config); CHECK(g_gameExeNames.size() == 300);
    CHECK(g_gameProfiles.at(L"c:\\游戏库\\game-299.exe").blockedKeys.size() == 14);
}
void MigrationTests(const std::filesystem::path& directory)
{
    const auto legacy = directory / L"legacy.ini";
    { std::ofstream file(legacy); file << "[Settings]\nGames=example.exe\nGameProfiles=example.exe^zh^13,84^hold^9000^1^0\n"; }
    Reset(); LoadConfig(legacy.wstring());
    CHECK(g_gameExeNames.size() == 1);
    CHECK(g_gameProfiles.at(L"example.exe").targetLanguage == ProtectedInputLanguage::Chinese);
    CHECK(g_gameProfiles.at(L"example.exe").chatMode == ChatActivationMode::Hold);
    CHECK(g_gameProfiles.at(L"example.exe").restoreTimeoutMs == 9000);
    CHECK(g_gameProfiles.at(L"example.exe").lockWindowsKey);
    CHECK(!g_gameProfiles.at(L"example.exe").inheritWindowsKey);
    CHECK(std::filesystem::exists(legacy.wstring()+L".v1.bak"));
    CHECK(GetPrivateProfileIntW(L"Settings",L"Version",0,legacy.c_str()) == 2);
    const auto empty = directory / L"empty-legacy.ini";
    { std::ofstream file(empty); file << "[Settings]\nGames=\n"; }
    Reset(); LoadConfig(empty.wstring()); CHECK(g_gameExeNames.empty());
    const auto invalid = directory / L"invalid-version.ini";
    { std::ofstream file(invalid); file << "[Settings]\nVersion=99\n"; }
    Reset(); LoadConfig(invalid.wstring()); CHECK(!g_configError.empty());
    CHECK(GetPrivateProfileIntW(L"Settings",L"Version",0,invalid.c_str()) == 99);
    CHECK(SaveConfig()); CHECK(std::filesystem::exists(invalid.wstring()+L".invalid.bak"));
}

void ForegroundTests()
{
    Reset();
    GameProfile a{}, b{}; a.inputGuardEnabled = false; b.inputGuardEnabled = false;
    a.lockWindowsKey = true; a.showNotifications = false; b.showNotifications = false;
    g_gameExeNames = {L"a.exe", L"b.exe"}; g_gameProfiles[L"a.exe"] = a; g_gameProfiles[L"b.exe"] = b;
    ApplyForegroundGame(L"a.exe", nullptr);
    CHECK(g_inGameProtection); CHECK(g_activeGameExeName == L"a.exe"); CHECK(ShouldBlockWindowsKeyForActiveGame());
    CHECK(g_savedLayout == nullptr); // Key-only mode never changes the input layout.
    const UINT aSession = g_gameSession;
    ApplyForegroundGame(L"b.exe", nullptr);
    CHECK(g_activeGameExeName == L"b.exe"); CHECK(!ShouldBlockWindowsKeyForActiveGame()); CHECK(g_gameSession != aSession);
    ApplyForegroundGame(L"", nullptr); CHECK(!g_inGameProtection); CHECK(g_activeGameExeName.empty());
    ApplyForegroundGame(L"a.exe", nullptr); PauseProtection();
    ApplyForegroundGame(L"a.exe", nullptr); CHECK(!g_inGameProtection); CHECK(g_protectionPaused);
    g_protectionPaused = false; ApplyForegroundGame(L"a.exe", nullptr); CHECK(g_inGameProtection);
    g_protectionEnabled = false; ApplyForegroundGame(L"a.exe", nullptr); CHECK(!g_inGameProtection);
    g_protectionEnabled = true; ApplyForegroundGame(L"a.exe", nullptr); CHECK(g_inGameProtection);
    g_autoDetectEnabled = false; ApplyForegroundGame(L"a.exe", nullptr); CHECK(!g_inGameProtection);
    g_autoDetectEnabled = true; g_gameProfiles[L"a.exe"].enabled = false;
    ApplyForegroundGame(L"a.exe", nullptr); CHECK(!g_inGameProtection); CHECK(g_currentDetectedGameName == L"a.exe");
    g_gameProfiles[L"a.exe"].enabled = true; ApplyForegroundGame(L"a.exe", nullptr); CHECK(g_inGameProtection);
    g_chatInputSuspended = true; CHECK(ShouldBlockWindowsKeyForActiveGame()); // Chat never unlocks the keyboard.
    LeaveGameProtection(); CHECK(!g_chatInputSuspended); CHECK(g_savedWindow == nullptr);
}
// Hidden native windows exercise the actual editor message paths without
// touching the user's running instance, desktop focus or daily configuration.
void EditorTests()
{
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&common);
    g_hInst = GetModuleHandleW(nullptr);
    const HWND host = CreateWindowExW(0, L"STATIC", L"UI regression", WS_OVERLAPPEDWINDOW,
        0, 0, 900, 700, nullptr, nullptr, g_hInst, nullptr);
    CHECK(host != nullptr);
    g_themePreference = ThemePreference::Dark;
    ThemeManager::Initialize(GetDpiForWindow(host));
    const HWND editor = ProfileEditor::CreateEditor(host);
    CHECK(editor != nullptr);
    const int dpi = GetDpiForWindow(editor);
    std::cout << "Hidden editor DPI: " << dpi << '\n';
    auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
    SetWindowPos(editor, nullptr, 0, 0, scale(520), scale(340), SWP_NOZORDER | SWP_NOACTIVATE);
    const HWND content = FindWindowExW(editor, nullptr, L"FFKeyLockScrollContent", nullptr);
    const HWND scrollBar = FindWindowExW(editor, nullptr, L"SCROLLBAR", nullptr);
    auto item = [content](int id) { return GetDlgItem(content, id); };
    auto bounds = [](HWND hwnd, HWND relative) {
        RECT r{}; GetWindowRect(hwnd, &r); MapWindowPoints(nullptr, relative, reinterpret_cast<POINT*>(&r), 2); return r;
    };
    GameProfile original = NewGameProfile(); ApplyCatPreset(original); original.inputGuardEnabled = false;
    ProfileEditor::LoadEditor(editor, L"ui-test.exe", original);
    CHECK(!ProfileEditor::IsDirty(editor));
    const HWND key = item(3000 + VK_F2);
    CHECK(key != nullptr);
    CHECK(GetNextDlgTabItem(content, item(2100), FALSE) == item(2102));
    CHECK(GetNextDlgTabItem(content, item(2102), FALSE) == item(2101));
    CHECK((GetWindowLongPtrW(key, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX);
    const RECT before = bounds(key, content);
    const LRESULT font = SendMessageW(key, WM_GETFONT, 0, 0);
    const RECT viewport = bounds(editor, host);
    SendMessageW(editor, WM_VSCROLL, SB_LINEDOWN, 0);
    CHECK(GetScrollPos(scrollBar, SB_CTL) == scale(32));
    const RECT after = bounds(key, content);
    CHECK(EqualRect(&before, &after));
    CHECK(SendMessageW(key, WM_GETFONT, 0, 0) == font);
    CHECK(bounds(content, editor).top == -GetScrollPos(scrollBar, SB_CTL));
    SendMessageW(editor, WM_VSCROLL, SB_BOTTOM, 0);
    SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(scrollBar, SB_CTL, &info);
    CHECK(info.nPos == info.nMax - static_cast<int>(info.nPage) + 1);
    SendMessageW(editor, WM_VSCROLL, SB_LINEDOWN, 0);
    CHECK(GetScrollPos(scrollBar, SB_CTL) == info.nPos);
    SendMessageW(editor, WM_VSCROLL, SB_TOP, 0);
    SendMessageW(editor, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), 0);
    CHECK(GetScrollPos(scrollBar, SB_CTL) == 0);
    SendMessageW(editor, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), 0);
    UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    CHECK(lines == 0 || GetScrollPos(scrollBar, SB_CTL) > 0);
    SendMessageW(key, BM_CLICK, 0, 0);
    CHECK(SendMessageW(key, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    CHECK(ProfileEditor::IsDirty(editor));
    const int savedScroll = GetScrollPos(scrollBar, SB_CTL);
    g_themePreference = ThemePreference::Light;
    ThemeManager::Initialize(dpi); ProfileEditor::RefreshEditorTheme(editor);
    CHECK(ProfileEditor::IsDirty(editor));
    CHECK(SendMessageW(key, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    CHECK(GetScrollPos(scrollBar, SB_CTL) == savedScroll);
    GameProfile draft{}; CHECK(ProfileEditor::ReadEditor(editor, draft));
    CHECK(std::find(draft.blockedKeys.begin(), draft.blockedKeys.end(), UINT(VK_F2)) == draft.blockedKeys.end());
    CHECK(std::find(draft.blockedKeys.begin(), draft.blockedKeys.end(), UINT(VK_F1)) != draft.blockedKeys.end());
    SendMessageW(item(2115), BM_CLICK, 0, 0); // Expand input language settings.
    SendMessageW(editor, WM_VSCROLL, SB_BOTTOM, 0);
    const int expandedBottom = GetScrollPos(scrollBar, SB_CTL);
    SendMessageW(item(2115), BM_CLICK, 0, 0); // Collapse while scrolled to the end.
    CHECK(GetScrollPos(scrollBar, SB_CTL) < expandedBottom);
    CHECK(bounds(content, editor).top == -GetScrollPos(scrollBar, SB_CTL));
    // Crossing the no-scroll boundary must not resize the key grid.
    SetWindowPos(editor, nullptr, 0, 0, scale(520), scale(1800), SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(GetScrollPos(scrollBar, SB_CTL) == 0);
    const RECT tall = bounds(key, content);
    CHECK(EqualRect(&before, &tall));
    SetWindowPos(editor, nullptr, 0, 0, scale(520), scale(340), SWP_NOZORDER | SWP_NOACTIVATE);
    const RECT restored = bounds(editor, host);
    CHECK(EqualRect(&viewport, &restored));
    g_themePreference = ThemePreference::Dark;
    ThemeManager::Initialize(dpi); ProfileEditor::RefreshEditorTheme(editor);
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, scale(520), scale(340));
    HGDIOBJ previous = SelectObject(dc, bitmap);
    NMCUSTOMDRAW draw{}; draw.hdr.hwndFrom = item(2100); draw.hdr.code = NM_CUSTOMDRAW;
    draw.dwDrawStage = CDDS_PREPAINT; draw.hdc = dc; GetClientRect(item(2100), &draw.rc);
    CHECK(SendMessageW(content, WM_NOTIFY, 2100, reinterpret_cast<LPARAM>(&draw)) == CDRF_SKIPDEFAULT);
    CHECK(GetPixel(dc, draw.rc.right - 2, draw.rc.bottom - 2) == ThemeManager::WindowColor());
    SendMessageW(item(2117), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    CHECK(GetPixel(dc, 0, 0) == ThemeManager::WindowColor());
    SendMessageW(scrollBar, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    CHECK(GetPixel(dc, 0, scale(100)) == ThemeManager::WindowColor());
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
    DestroyWindow(host); ThemeManager::Shutdown();
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        CHECK(argc == 2);
        const auto directory = std::filesystem::absolute(argv[1]) / (L"run-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        PressTests(); ConfigTests(directory); MigrationTests(directory); ForegroundTests(); EditorTests();
        const auto previousDpiContext = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
        EditorTests();
        SetThreadDpiAwarenessContext(previousDpiContext);
        Reset(); PauseProtection(); CHECK(g_protectionPaused); CHECK(g_pauseUntil == 0);
        g_autoDetectEnabled = false; ResumeProtection(); CHECK(!g_protectionPaused);
        std::cout << "PASS: " << checks << " regression checks\n";
        return 0;
    }
    catch (const std::exception& error) { DisableWindowsKeyGuard(); std::cerr << error.what() << '\n'; return 1; }
}
