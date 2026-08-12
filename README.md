# FFKeyLock


FFKeyLock is a lightweight Windows tray utility for keeping game input reliable. It listens for foreground-window changes, applies a dedicated profile to each protected executable, and restores the previous input language after the game loses focus.

[简体中文](README.zh-CN.md)

## Tech Stack

- Native Win32 desktop application
- C++20
- Windows Shell tray icon APIs
- Windows input language APIs
- INI-based user configuration
- Visual Studio / MSBuild project
- Inno Setup installer

## Why It Is Lightweight

FFKeyLock is built directly on Win32 without a browser runtime, managed framework, or background service. Foreground detection is event-driven through `SetWinEventHook`, with only a low-frequency fallback timer, so the app remains responsive without continuously polling while idle.

## Main Features

- Gives every protected executable its own target input language, chat keys, chat mode, restore timeout, Windows-key behavior, and notification preference.
- Uses event-driven foreground detection for faster response and lower idle overhead.
- Supports customizable chat keys such as Enter, T, Y, and `/`.
- Supports toggle-to-chat and hold-to-chat modes, with Enter/Esc restoration and per-game timeout fallback.
- Locks the Windows key only while a protected program is in the foreground by default, with an advanced always-lock scope.
- Restores the previous input language after leaving a protected program window.
- Automatically detects known executables from a configurable protected program list.
- Lets you add the current foreground program or browse for an `.exe` file.
- Provides a protected program list with selection, scrolling, right-click actions, and folder opening.
- Provides quick tray controls for protection, auto detection, startup, notifications, and input switching.
- Provides a modern NVIDIA-inspired overlay positioned above native Windows toast notifications.
- Supports keyboard navigation for custom controls and respects the Windows reduced-animation setting.
- Supports Chinese and English UI text from `Settings -> Language`.
- Can start with Windows for always-on protection.

## Install

Download the latest installer from [GitHub Releases](https://github.com/brealinxx/FFKeyLock/releases/latest):

```text
FFKeyLock-Setup-v0.5.0-x64.exe
```

## Build

Open `FFKeyLock.slnx` in Visual Studio, or build with MSBuild:

```powershell
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=x64 /m
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=Win32 /m
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=ARM64 /m
```

Release executables are generated at:

```text
x64/Release/FFKeyLock.exe
Release/FFKeyLock.exe
ARM64/Release/FFKeyLock.exe
```

## Package

Install Inno Setup, then compile the installer script:

```powershell
ISCC.exe installer/FFKeyLock.iss /DAppArchitecture=x64
ISCC.exe installer/FFKeyLock.iss /DAppArchitecture=x86
ISCC.exe installer/FFKeyLock.iss /DAppArchitecture=arm64
```

Installers are generated under `installer/output/` with the target architecture in the file name.

## Configuration

User settings are stored in:

```text
%APPDATA%\FFKeyLock\config.ini
```

The config includes global protection settings, Windows-key lock scope, UI language, theme, notification options, the protected executable list, and per-game profiles.

## Changelog

See [CHANGELOG-en.md](CHANGELOG-en.md).

## License

See [LICENSE](LICENSE).
