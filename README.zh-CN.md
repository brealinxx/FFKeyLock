# FFKeyLock

FFKeyLock 是一个轻量级 Windows 托盘工具，用来减少游戏时输入法误切换带来的干扰。它会检测当前前台窗口，识别已配置的受保护程序，并在保护状态下把输入法切换到英文。

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

FFKeyLock 直接基于 Win32 编写，不依赖浏览器运行时、托管框架或后台服务。程序以小型托盘应用运行，使用简单配置文件保存设置，并通过短间隔轮询前台窗口完成程序检测。

## 主要功能

- 游戏保护时自动切换当前输入语言到英文。
- 支持禁用或启用 Windows 键，减少游戏中误触导致的打断。
- 离开受保护程序窗口后恢复之前的输入语言。
- 根据可配置的受保护程序列表自动检测目标程序。
- 支持添加当前前台程序，或通过浏览选择 `.exe` 文件添加程序。
- 提供受保护程序列表，支持选择、滚动、右键操作和打开文件所在目录。
- 托盘菜单可快速控制保护模式、自动检测、开机启动、通知和输入法切换。
- 支持在 `设置 -> 语言` 中切换中文和英文界面。
- 支持开机启动，便于常驻保护。

## 安装

可以从 [GitHub Releases](https://github.com/brealinxx/FFKeyLock/releases/latest) 下载最新安装包：

```text
FFKeyLock-Setup-v0.3.0.exe
```

## 构建

可以用 Visual Studio 打开 `FFKeyLock.slnx`，也可以使用 MSBuild 构建：

```powershell
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=x64 /m
```

Release 可执行文件会生成到：

```text
x64/Release/FFKeyLock.exe
```

## 打包

安装 Inno Setup 后，编译安装脚本：

```powershell
ISCC.exe installer/FFKeyLock.iss
```

安装包会生成到 `installer/output/`。

## 配置

用户设置保存于：

```text
%APPDATA%\FFKeyLock\config.ini
```

配置内容包括保护开关、自动检测开关、界面语言、主题、通知选项和受保护程序列表。

## 更新日志

见 [CHANGELOG.md](CHANGELOG.md)。

## 许可证

见 [LICENSE](LICENSE)。
