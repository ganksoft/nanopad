# Changelog

All notable user-facing changes to Nanopad will be documented in this file.

This project follows a simple changelog format inspired by Keep a Changelog.

## [1.1.0] - 2026-06-21

### Added
- Zoom the text in and out with Ctrl+mouse wheel.
- View menu Zoom In, Zoom Out, and Restore Default Zoom commands with Ctrl+Plus, Ctrl+Minus, and Ctrl+0 shortcuts.
- Current zoom percentage shown in the status bar.

## [1.0.9] - 2026-04-17

### Added
- Native ARM64 build alongside x64.
- Release now ships both x64 and ARM64 zips.
- CI builds and verifies both platforms.

## [1.0.8] - 2026-04-14

### Fixed
- Fixed command-line parsing for paths with leading whitespace (e.g. .reg file "Edit" context menu).

## [1.0.7] - 2026-04-08

### Added
- Reload the open document when it changes on disk outside Nanopad.

### Changed
- Prompt before reloading if you have unsaved edits.

## [1.0.6] - 2026-04-07

### Changed
- Improved packaging polish and release path handling.
- Internal cleanup ahead of future releases.

## [1.0.5] - 2026-04-07

### Changed
- Moved update-available notifications into the status bar.
- Improved status bar DPI handling.

## [1.0.4] - 2026-04-06

### Fixed
- Improved Notepad replacement and redirect behavior.

### Changed
- Assorted polish and release packaging updates.

## [1.0.3] - 2026-04-03

### Added
- Added winget packaging metadata for distribution updates.

### Changed
- Refreshed project documentation.

## [1.0.2] - 2026-03-22

### Changed
- Added the project name and link to the About dialog.

## [1.0.1] - 2026-03-22

### Fixed
- Fixed printer DPI scaling behavior.

### Changed
- Refreshed project documentation.

## [1.0.0] - 2026-03-21

### Added
- Initial public release.
