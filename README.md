# DC++

A cross-platform, dual-pane file manager in C++ and Qt 6. It is a clean rewrite inspired by [Double Commander](https://github.com/doublecmd/doublecmd), whose behaviour (key bindings, `cm_*` command names, dialog flow) it follows. No code is shared with the Pascal original.

## Scope

Planned features, delivered in phases:

| Phase | Feature |
|---|---|
| 0 | Project scaffold, build and test setup |
| 1 | Dual-pane core: columns view, navigation, marking, copy, move, new folder, delete to recycle bin |
| 2 | Tabs per panel |
| 3 | Find files (name, content, size, date) |
| 4 | Multi-rename |
| 5 | Archives (browse, extract, pack) via libarchive |

Out of scope for now: plugins, built-in viewer and editor, network file systems, directory sync, scripting.

## Requirements

- Qt 6.5 or later (developed against 6.11.1, `msvc2022_64` kit)
- CMake 3.25 or later and Ninja
- A C++20 compiler (MSVC from Visual Studio 2022 or later on Windows)
- vcpkg for third-party libraries (none needed until phase 5)

## Building

On Windows, from any prompt:

```
build.bat              :: debug build, then run the tests
build.bat msvc-release :: release build
```

`build.bat` sets up the MSVC environment, then runs the CMake presets. To run the steps yourself from a Developer Command Prompt:

```
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

The executable is `build\msvc-debug\dcpp.exe`. The Qt runtime is copied next to it after each build, so it runs in place. If Qt is installed somewhere other than `C:\Qt\6.11.1\msvc2022_64`, set `CMAKE_PREFIX_PATH` in a `CMakeUserPresets.json`.

## Layout

- `src/core/`: platform-neutral logic (file sources, sorting, masks, copy/move/delete operations). Depends on QtCore only and is unit tested.
- `src/ui/`: Qt Widgets front end (main window, panels, dialogs, command registry).
- `tests/`: Qt Test unit tests for `src/core`, run with `ctest`.

Panels never touch the disk directly. They browse a `FileSource`, so archives and search results can later appear in a panel as just another source.

## Licence

MIT. See [LICENSE](LICENSE).
