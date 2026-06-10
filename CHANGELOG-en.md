# CHANGELOG

## v0.3

### Added

- Added `UI/ProtectedPrograms/ProtectedProgramListView` as a dedicated protected program list control.
- Added `UI/ProtectedPrograms/ProtectedProgramCommands` to centralize list-related commands.
- Added `UI/Main/MainContentView` as the entry point for future main content view extraction.
- Added an Inno Setup installer script with English / Simplified Chinese wizard support and first-launch display language selection.
- Added GitHub Actions workflows for CI and tag-based release publishing.

### Changed

- Refactored the main window UI structure to reduce the responsibility and complexity of `MainWindow.cpp`.
- Moved list drawing, hit testing, selection state, internal scrolling, and context menu logic out of `MainWindow.cpp`.
- Updated project version references to `v0.3` / `0.3.0`.
- Updated the Visual Studio / MSBuild project files to include the new UI modules.
- Updated `.gitignore` to ignore Visual Studio build directories and common intermediate files.

### Fixed

- Fixed mixed responsibilities caused by the main window directly owning protected list state.
- Fixed scattered list context menu hit-testing logic that made the main window harder to maintain.
- Fixed the risk of build artifacts being accidentally tracked by version control.
- Fixed the missing installer packaging and release automation path.

## v0.2

### Added

- Added a protected program list for executables that FFKeyLock should guard.
- Added support for adding the current foreground program.
- Added support for adding `.exe` files through a file picker.
- Added a protected program browser window for viewing program names and recorded paths.
- Added a list context menu for copying program names or opening program folders.
- Added Chinese / English UI switching and theme settings.

### Changed

- Improved protected list configuration persistence so added or deleted items are saved consistently.
- Expanded settings, notification, and help menus with more common actions.
- Improved the main window layout for protected program buttons and the list area.

### Fixed

- Fixed missing guidance when an item only has a program name and no saved path.
- Fixed unstable selection state after deleting the selected protected program.
- Fixed unclear empty-state feedback when the protected program list has no items.

## v0.1

### Added

- Initial release with a lightweight native Win32 tray utility foundation.
- Added foreground window detection and protected executable recognition.
- Added game protection mode, switching input language to English while a protected program is active.
- Added input language restoration after leaving a protected program.
- Added basic toggles for protection mode, auto detection, and startup.
- Added Windows key locking to reduce accidental interruptions during games.
- Added INI-based configuration for user settings.

### Changed

- Built the app with native Win32, C++20, and Windows Shell tray icon APIs.
- Used short-interval foreground window polling for auto detection without an extra background service.
- Stored user configuration under `%APPDATA%\FFKeyLock\config.ini`.

### Fixed

- Fixed unreliable game input caused by accidental input language switching.
- Fixed missing automatic input language restoration after leaving a game.
- Fixed accidental Windows key presses interrupting gameplay.
