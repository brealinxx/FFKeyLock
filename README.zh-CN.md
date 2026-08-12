# FFKeyLock

FFKeyLock 是一个轻量级 Windows 托盘工具，用来减少游戏时输入法误切换带来的干扰。它会监听前台窗口变化，为每个受保护程序应用独立配置，并在游戏失去焦点后恢复原输入法。

[English](README.md)

## 技术栈

- 原生 Win32 桌面应用
- C++20
- Windows Shell 托盘图标 API
- Windows 输入语言 API
- INI 用户配置
- Visual Studio / MSBuild 工程
- Inno Setup 安装包

## 轻量设计

FFKeyLock 直接基于 Win32 编写，不依赖浏览器运行时、托管框架或后台服务。前台检测使用 `SetWinEventHook` 事件驱动机制，仅保留低频定时器容错，因此响应更快，空闲时也无需持续轮询。

## 主要功能

- 每个受保护程序可独立配置目标输入法、聊天按键、聊天模式、恢复时间、Win 键和通知。
- 使用事件驱动前台检测，降低空闲资源占用并提升切换响应速度。
- 聊天按键支持 Enter、T、Y、`/` 等组合。
- 支持“切换聊天”和“按住聊天”模式，并可通过 Enter/Esc 或独立超时恢复保护。
- Win 键默认只在受保护程序位于前台时锁定，同时提供“始终锁定”高级范围。
- 离开受保护程序窗口后恢复之前的输入语言。
- 根据可配置的受保护程序列表自动检测目标程序。
- 支持添加当前前台程序，或通过浏览选择 `.exe` 文件添加程序。
- 提供受保护程序列表，支持选择、滚动、右键操作和打开文件所在目录。
- 托盘菜单可快速控制保护模式、自动检测、开机启动、通知和输入法切换。
- 提供现代 NVIDIA 风格 Overlay，并自动避让 Windows 原生 Toast 通知。
- 自绘按钮支持键盘导航，并遵循 Windows“减少动画”设置。
- 支持在 `设置 -> 语言` 中切换中文和英文界面。
- 支持开机启动，便于常驻保护。

## 安装

可以从 [GitHub Releases](https://github.com/brealinxx/FFKeyLock/releases/latest) 下载最新安装包：

```text
FFKeyLock-Setup-v0.5.0-x64.exe
```

## 构建

可以用 Visual Studio 打开 `FFKeyLock.slnx`，也可以使用 MSBuild 构建：

```powershell
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=x64 /m
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=Win32 /m
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=ARM64 /m
```

Release 可执行文件会生成到：

```text
x64/Release/FFKeyLock.exe
Release/FFKeyLock.exe
ARM64/Release/FFKeyLock.exe
```

## 打包

安装 Inno Setup 后，编译安装脚本：

```powershell
ISCC.exe installer/FFKeyLock.iss /DAppArchitecture=x64
ISCC.exe installer/FFKeyLock.iss /DAppArchitecture=x86
ISCC.exe installer/FFKeyLock.iss /DAppArchitecture=arm64
```

安装包会生成到 `installer/output/`。

## 配置

用户设置保存于：

```text
%APPDATA%\FFKeyLock\config.ini
```

配置内容包括全局保护设置、Win 键锁定范围、界面语言、主题、通知选项、受保护程序列表和每个游戏的独立配置。

## 更新日志

见 [CHANGELOG.md](CHANGELOG.md)。

## 许可证

见 [LICENSE](LICENSE)。
