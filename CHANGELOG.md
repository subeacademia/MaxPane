# Changelog

All notable changes to ReDockIT will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [1.0.1] - 2026-03-01

### Fixed
- **Startup deadlock**: REAPER could hang when loading a project with ReDockIT docked. The capture queue now defers `Main_OnCommand` calls during `LoadState()` to avoid calling REAPER APIs while the project is still loading.

### Changed
- Replaced raw `new`/`delete` with `std::unique_ptr` for safer resource management
- Replaced all `strncpy` calls with `safe_strncpy` helper for consistent null-termination
- Extracted magic numbers (colors, geometry, timing) into named constants in `config.h`
- Merged duplicate `FindWindowEnumProc`/`FindChildWindowEnumProc` into single function
- Debug logging is now conditional on `CMAKE_BUILD_TYPE=Debug` (no longer always enabled)
- Added `-Wshadow` and `-Wconversion` compiler warnings

### Removed
- Dead code: unused `BuildLists()` and `IsAnyCaptured()` methods

## [1.0.0] - 2025-03-01

### Added
- Native C++ REAPER extension (no script dependencies)
- Binary split tree layout engine with up to 16 panes
- Tabbed window management with drag-and-drop between panes
- 14 known REAPER windows with one-click capture
- Arbitrary window capture (including ReaImGui scripts)
- Dock frame detection for ReaImGui windows
- Async capture queue with retry logic
- Named workspaces (save/restore complete layouts)
- Favorites system with persistent action command strings
- Tab color palette (8 colors)
- 5 built-in layout presets
- Auto-open on REAPER startup (configurable)
- Full state persistence via REAPER ExtState
- Dockable container (integrates with REAPER's docker system)
- Context menus for panes and tabs
- Capture-by-click mode
- Conditional debug logging (Debug builds only)
- Cross-platform architecture via WDL/SWELL
