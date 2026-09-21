#include "AppMenus.h"
#include "../../AppState.h"
#include "../../Config.h"
#include "../../GameProtection.h"
#include "../../Localization.h"
#include "../../Resource.h"
namespace FFKeyLock::UI
{
void AddMenuItem(HMENU menu, UINT id, const wchar_t* cn, const wchar_t* en, bool checked = false)
{
    AppendMenuW(menu,MF_STRING | (checked ? MF_CHECKED : 0),id,Text(cn,en));
}
void Sep(HMENU menu) { AppendMenuW(menu,MF_SEPARATOR,0,nullptr); }
HMENU PauseMenu()
{
    HMENU menu = CreatePopupMenu();
    AddMenuItem(menu,IDM_RESUME,L"恢复保护",L"Resume protection");
    AddMenuItem(menu,IDM_PAUSE_30,L"暂停 30 秒",L"Pause for 30 seconds");
    AddMenuItem(menu,IDM_PAUSE_300,L"暂停 5 分钟",L"Pause for 5 minutes");
    AddMenuItem(menu,IDM_PAUSE_MANUAL,L"暂停直到手动恢复",L"Pause until resumed");
    return menu;
}
HMENU CreateAppMenu()
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), settings = CreatePopupMenu(), help = CreatePopupMenu();
    AddMenuItem(file,IDM_ADD_GAME_FILE,L"选择游戏文件…",L"Choose game executable…");
    AddMenuItem(file,IDM_RUNNING_PROGRAMS,L"从运行中程序选择…",L"Choose a running program…");
    AddMenuItem(file,IDM_ADD_CURRENT_GAME,L"添加最近的前台程序",L"Add recent foreground program"); Sep(file);
    AddMenuItem(file,IDM_IMPORT_PROFILES,L"导入配置库…",L"Import profile library…");
    AddMenuItem(file,IDM_EXPORT_PROFILES,L"导出配置库…",L"Export profile library…"); Sep(file);
    AddMenuItem(file,IDM_EXIT,L"退出 FFKeyLock",L"Exit FFKeyLock");
    AddMenuItem(settings,IDM_PROTECTION,L"启用保护",L"Enable protection",g_protectionEnabled);
    AddMenuItem(settings,IDM_AUTO_DETECT,L"自动检测前台游戏",L"Detect foreground games",g_autoDetectEnabled);
    AddMenuItem(settings,IDM_WINDOWS_KEY_GUARD,L"新游戏默认锁定 Win 键",L"Block Win by default for new games",g_windowsKeyGuardEnabled);
    HMENU scope = CreatePopupMenu();
    AddMenuItem(scope,IDM_WINKEY_SCOPE_PROTECTED,L"仅受保护游戏前台",L"Protected foreground only",g_windowsKeyGuardScope == WindowsKeyGuardScope::ProtectedForeground);
    AddMenuItem(scope,IDM_WINKEY_SCOPE_ALWAYS,L"全局 Win 键（影响桌面）",L"Global Win blocking (affects desktop)",g_windowsKeyGuardScope == WindowsKeyGuardScope::Always);
    AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(scope),Text(L"Win 键作用范围",L"Windows key scope"));
    HMENU emergency = CreatePopupMenu();
    AddMenuItem(emergency,IDM_EMERGENCY_BACK,L"Ctrl + Alt + Backspace",L"Ctrl + Alt + Backspace",g_emergencyKey == VK_BACK);
    AddMenuItem(emergency,IDM_EMERGENCY_END,L"Ctrl + Alt + End",L"Ctrl + Alt + End",g_emergencyKey == VK_END);
    AddMenuItem(emergency,IDM_EMERGENCY_HOME,L"Ctrl + Alt + Home",L"Ctrl + Alt + Home",g_emergencyKey == VK_HOME);
    AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(emergency),Text(L"紧急解除快捷键",L"Emergency shortcut"));
    AddMenuItem(settings,IDM_RETRY_HOOK,L"重新连接键盘拦截",L"Reconnect keyboard hook"); Sep(settings);
    AddMenuItem(settings,IDM_STARTUP,L"开机启动（直接进入托盘）",L"Start with Windows (in tray)",IsStartupEnabled());
    HMENU theme = CreatePopupMenu(), language = CreatePopupMenu(), notifications = CreatePopupMenu();
    AddMenuItem(theme,IDM_THEME_SYSTEM,L"跟随系统",L"System",g_themePreference == ThemePreference::System);
    AddMenuItem(theme,IDM_THEME_LIGHT,L"浅色",L"Light",g_themePreference == ThemePreference::Light);
    AddMenuItem(theme,IDM_THEME_DARK,L"深色",L"Dark",g_themePreference == ThemePreference::Dark);
    AddMenuItem(language,IDM_LANGUAGE_CHINESE,L"简体中文",L"简体中文",!IsEnglish());
    AddMenuItem(language,IDM_LANGUAGE_ENGLISH,L"English",L"English",IsEnglish());
    AddMenuItem(notifications,IDM_OVERLAY_NOTIFICATIONS,L"游戏浮层提示",L"Game overlay",g_overlayNotificationsEnabled);
    AddMenuItem(notifications,IDM_NOTIFICATIONS,L"系统通知",L"System notifications",g_notificationsEnabled);
    AddMenuItem(notifications,IDM_TEST_NOTIFICATION,L"测试通知",L"Test notification");
    AddMenuItem(notifications,IDM_MUTE_NOTIFICATIONS,L"全部静音",L"Mute all");
    AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(theme),Text(L"主题",L"Theme"));
    AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(language),Text(L"语言",L"Language"));
    AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(notifications),Text(L"通知",L"Notifications")); Sep(settings);
    AddMenuItem(settings,IDM_OPEN_CONFIG_DIR,L"打开配置目录",L"Open configuration folder");
    AddMenuItem(settings,IDM_OPEN_LOG_DIR,L"打开日志目录",L"Open log folder");
    AddMenuItem(settings,IDM_RESET_CONFIG,L"重置配置…",L"Reset settings…");
    AddMenuItem(settings,IDM_CLEAR_LOCAL_DATA,L"删除本地数据并退出…",L"Delete local data and exit…");
    AddMenuItem(help,IDM_HELP_USAGE,L"使用说明",L"How to use");
    AddMenuItem(help,IDM_CHECK_UPDATES,L"检查更新",L"Check for updates");
    AddMenuItem(help,IDM_ABOUT,L"关于",L"About");
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(file),Text(L"游戏与配置(&F)",L"&Games"));
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(PauseMenu()),Text(L"暂停(&P)",L"&Pause"));
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(settings),Text(L"设置(&S)",L"&Settings"));
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(help),Text(L"帮助(&H)",L"&Help"));
    return bar;
}

HMENU CreateTrayMenu()
{
    HMENU menu = PauseMenu(); Sep(menu);
    HMENU quick = CreatePopupMenu();
    AddMenuItem(quick,IDM_PROTECTION,L"启用保护",L"Enable protection",g_protectionEnabled);
    AddMenuItem(quick,IDM_AUTO_DETECT,L"自动检测",L"Auto detection",g_autoDetectEnabled);
    AddMenuItem(quick,IDM_WINDOWS_KEY_GUARD,L"Win 键默认锁定",L"Default Win key blocking",g_windowsKeyGuardEnabled);
    AddMenuItem(quick,IDM_STARTUP,L"开机启动",L"Start with Windows",IsStartupEnabled());
    AddMenuItem(quick,IDM_NOTIFICATIONS,L"系统通知",L"System notifications",g_notificationsEnabled);
    AddMenuItem(quick,IDM_OVERLAY_NOTIFICATIONS,L"浮层提示",L"Overlay",g_overlayNotificationsEnabled);
    Sep(quick);
    AddMenuItem(quick,IDM_SWITCH_ENGLISH,L"切换为英文",L"Switch to English");
    AddMenuItem(quick,IDM_SWITCH_CHINESE,L"切换为中文",L"Switch to Chinese");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(quick),Text(L"快捷设置",L"Quick settings"));
    AddMenuItem(menu,IDM_ADD_CURRENT_GAME,L"添加当前程序",L"Add current program");
    AddMenuItem(menu,IDM_SHOW_WINDOW,L"打开游戏配置",L"Open game profiles");
    AddMenuItem(menu,IDM_EXIT,L"退出",L"Exit");
    return menu;
}
}
