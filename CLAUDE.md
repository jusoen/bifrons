# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bifrons (formerly DC++): a dual-pane file manager in C++20 and Qt 6 Widgets, a clean rewrite inspired by Double Commander. It follows DC's behaviour (key bindings, `cm_*` command names, dialog flow) but shares no code with it. MIT licence, so only LGPLv3 Qt modules may be used, linked dynamically (currently Core, Gui, Widgets, Concurrent, and Test for tests). Do not add GPL-only Qt modules or GPL libraries.

## Build and test

Windows only at present (MSVC, Qt 6.11.1 at `C:\Qt\6.11.1\msvc2022_64`, vcpkg from the VS install).

```
build.bat                  :: configure, build and run all tests (preset msvc-debug)
build.bat msvc-release     :: RelWithDebInfo build
```

`build.bat` calls `vcvars64.bat` itself, so it works from any shell. From Git Bash use `cmd //c "C:\\Users\\jmitchell\\Source\\bifrons\\build.bat"`. The executable is `build\msvc-debug\bifrons.exe`, with the Qt runtime deployed next to it.

Close a running `bifrons.exe` and wait for it to exit before building, or the link fails with LNK1168. `taskkill //IM bifrons.exe` closes it gracefully, so tabs and settings are saved.

Tests are Qt Test executables, one per `tests/tst_*.cpp`, registered with `bifrons_add_test()` in `tests/CMakeLists.txt`. Only `src/core` is unit tested. To run one suite or one test function:

```
ctest --preset msvc-debug -R tst_localfilesource        :: from a Developer Command Prompt
PATH="/c/Qt/6.11.1/msvc2022_64/bin:$PATH" build/msvc-debug/tst_localfilesource.exe uncPathsWalkUpToServer -o out.txt,txt
```

When running a test exe directly from Git Bash, write results with `-o file,txt`, because its stdout is not reliably captured. Warnings are `/W4 /permissive-`. Keep builds warning-free.

## Architecture

**Two libraries.** `src/core` builds `bifrons_core`, which depends on QtCore only so it can be tested headless. `src/ui` is the Widgets front end and links it. Platform calls (NetShareEnum, GetDriveTypeW, CreateProcess flags) sit behind `#if defined(Q_OS_WIN)` in the unit that needs them.

**File sources.** A panel never touches the disk directly. It browses a `FileSource` (`core/filesource.h`): `LocalFileSource` for the file system, including UNC paths (listing `//server` enumerates its shares), and `SearchResultFileSource` for find-files results fed into a panel. Paths use `/` internally. `displayPath()` converts them for the user.

**Listing is asynchronous.** `FilePanel` runs `FileSource::list()` on the Qt thread pool through a `QFutureWatcher`. A new `setFuture` supersedes the previous load, so a slow listing never overwrites a newer one. Anything that might touch a network path must stay off the UI thread: a `QFileInfo` check on an unreachable share blocks for about 21 s (the SMB timeout). `LocalFileSource::isNetworkPath()` is the UI-safe test. An unreachable network folder stays as the panel's path (so it is saved and Ctrl+R retries it), with a centred message over the list and the terminal button disabled.

**File operations.** Copy, move and delete are `FileOperation` subclasses (`core/localfileoperation.*`). `planTransfer()` (`core/transfer.*`) maps a destination typed in the copy/move dialog to per-item targets using DC's rules, and `tst_transfer` documents them. `OperationRunner` runs an operation on its own thread. Collisions and errors reach the user through a query handler that blocks the worker until the UI answers.

**Window structure.** `MainWindow` holds two sides in a `QSplitter` (double-click the handle to recentre). Each side is a `PanelToolbar` (drives, back, forward, up, home, terminal) above a `PanelTabs` (`QTabWidget`, one `FilePanel` per tab). The toolbar follows the side's current tab through `FilePanel::navigationChanged`. A `FilePanel` is a `PathBar` (breadcrumbs that switch to a line edit on click), a `QTableView` over `FileListModel`, and a status line. Each panel keeps its own folder history. Tab drags that leave their row, and all Ctrl+drags (duplicate), use a custom in-row drag in `PanelTabs` with `TabGhost` overlays rather than `QDrag`.

**Commands.** User actions are registered in `MainWindow::createCommands()` through `CommandRegistry` with DC's `cm_*` names (`cm_Copy`, `cm_RunTerm`, `cm_ViewHistoryPrev`, ...), a caption and a window-wide shortcut. Menus are built from those names, so a new command needs only one `add()` call and an entry in a menu list.

**Settings.** `QSettings` INI at `%APPDATA%\bifrons\bifrons.ini` (organisation and app name from `core/appinfo`). `MainWindow` loads and saves window geometry, the splitter, tabs per side (path and sort) and favourites. `main.cpp` copies a legacy `%APPDATA%\dcpp\dcpp.ini` across on first run.

## Conventions

- Commit subjects use a type prefix: `ADD:`, `FIX:`, `UPD:`, `DEL:`.
- Everything is in `namespace bifrons`. Source files are lower case and `#pragma once` is used in headers.
- User-visible strings go through `tr()`. Error text produced in `core` is plain `QStringLiteral`.
