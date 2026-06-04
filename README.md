# FFKeyLock

FFKeyLock is a lightweight Windows tray utility for keeping game input reliable. It watches the foreground window, detects configured game executables, and switches the input language to English while a protected game is active.

[简体中文](README.zh-CN.md)

## Tech Stack

- Native Win32 desktop application
- C++20
- Windows Shell tray icon APIs
- Windows input language APIs
- INI-based user configuration
- Visual Studio / MSBuild project

## Why It Is Lightweight

FFKeyLock is built directly on Win32 without a browser runtime, managed framework, or background service. It runs as a small tray app, stores settings in a simple config file, and only polls the foreground window at a short interval for game detection.

## Main Features

- Protects game sessions by switching the active input language to English.
- Disables or enables the Windows key to prevent accidental interruptions while gaming.
- Restores the previous input language after leaving a protected game window.
- Automatically detects known game executables from a configurable list.
- Lets you add the current foreground program or browse for an `.exe` file.
- Provides quick tray controls for protection, auto detection, startup, and input switching.
- Supports Chinese and English UI text from `Settings -> Language`.
- Can start with Windows for always-on protection.

## Build

Open `FFKeyLock.slnx` in Visual Studio, or build with MSBuild:

```powershell
MSBuild.exe FFKeyLock.slnx /p:Configuration=Debug /p:Platform=x64 /m
```

The debug executable is generated at `x64/Debug/FFKeyLock.exe`.

## Configuration

User settings are stored in:

```text
%APPDATA%\FFKeyLock\config.ini
```

The config includes protection state, auto-detection state, UI language, and the protected game executable list.

## License

See [LICENSE](LICENSE).
