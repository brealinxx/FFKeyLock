# FFKeyLock

Current version: **v0.3**

FFKeyLock is a lightweight Windows tray utility for keeping game input reliable. It watches the foreground window, detects configured protected executables, and switches the input language to English while a protected program is active.

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

FFKeyLock is built directly on Win32 without a browser runtime, managed framework, or background service. It runs as a small tray app, stores settings in a simple config file, and only polls the foreground window at a short interval for program detection.

## Main Features

- Protects game sessions by switching the active input language to English.
- Disables or enables the Windows key to prevent accidental interruptions while gaming.
- Restores the previous input language after leaving a protected program window.
- Automatically detects known executables from a configurable protected program list.
- Lets you add the current foreground program or browse for an `.exe` file.
- Provides a protected program list with selection, scrolling, right-click actions, and folder opening.
- Provides quick tray controls for protection, auto detection, startup, notifications, and input switching.
- Supports Chinese and English UI text from `Settings -> Language`.
- Can start with Windows for always-on protection.

## Install

Download the latest installer from [GitHub Releases](https://github.com/brealinxx/FFKeyLock/releases/latest):

```text
FFKeyLock-Setup.exe
```

The installer supports English and Simplified Chinese wizard languages. During installation, you can also choose the default display language used by FFKeyLock on first launch.

## Build

Open `FFKeyLock.slnx` in Visual Studio, or build with MSBuild:

```powershell
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=x64 /m
```

The release executable is generated at:

```text
x64/Release/FFKeyLock.exe
```

## Package

Install Inno Setup, then compile the installer script:

```powershell
ISCC.exe installer/FFKeyLock.iss
```

The installer is generated under `installer/output/`.

## Release Automation

GitHub Actions includes:

- `ci.yml`: builds the project on pushes and pull requests.
- `release.yml`: when a tag like `v0.3.0` is pushed, builds the app, packages it with Inno Setup, and uploads artifacts to the matching GitHub Release.

## Configuration

User settings are stored in:

```text
%APPDATA%\FFKeyLock\config.ini
```

The config includes protection state, auto-detection state, UI language, theme, notification options, and the protected executable list.

## Changelog

See [CHANGELOG-en.md](CHANGELOG-en.md).

## License

See [LICENSE](LICENSE).
