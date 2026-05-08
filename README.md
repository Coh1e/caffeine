# Amped (满血)

A tiny Caffeine-style Windows tray utility. Click the tray icon to keep your PC awake; click again to release.

- **Language:** C++20, pure Win32 (no Qt, no .NET, no MFC, no Electron).
- **Footprint:** single source file, links only `user32` and `shell32`.
- **Permissions:** runs as a normal user. No admin, no service.

## Build

Requires the MSVC toolchain (Visual Studio Build Tools or full VS) with `cl.exe` and `rc.exe` on `PATH`. Open a **"Developer Command Prompt for VS"** (or run `vcvarsall.bat` in your shell) so those tools resolve.

### Option A — `build.bat` (simplest)

```
build.bat
```

This regenerates `full.ico` / `empty.ico` if missing, compiles the resource script, then builds `amped.exe`.

### Option B — CMake

```
cmake -S . -B build
cmake --build build --config Release
```

### Raw `cl` command (if you prefer)

```
rc /nologo /fo amped.res amped.rc
cl /nologo /std:c++20 /W4 /EHsc /O2 /utf-8 main.cpp amped.res /link /SUBSYSTEM:WINDOWS /OUT:amped.exe user32.lib shell32.lib
```

## Run

Double-click `amped.exe`.

| Action | Effect |
|---|---|
| **Left-click** tray icon | Toggle 满血 ↔ 空杯 |
| **Right-click** tray icon | Menu: `Toggle Amped`, `Exit` |
| Tooltip when 满血 | `Amped: full power, keeping PC awake` |
| Tooltip when 空杯 | `Amped: empty, normal sleep allowed` |

Yellow lightning ⚡ = keep-awake is on. Gray outline = idle, normal sleep allowed. Default state on launch is **empty** — you opt in with a click.

Only one instance can run per user session (a named mutex blocks duplicates).

## What Amped *can* prevent

- The system going to sleep due to idleness.
- The display turning off due to idleness (we set `ES_DISPLAY_REQUIRED`).

## What Amped *cannot* prevent

- The user manually clicking **Sleep** / **Shut down** / **Hibernate**.
- A laptop sleeping when the lid is closed (lid-close policy).
- Enterprise Group Policy that forces lock or hibernate.
- Low-battery hibernation.
- Forced reboot from Windows Update.
- Power-off / shut-down.

`SetThreadExecutionState` is a *hint* to the power manager, not an override.

## Verifying it actually works

While Amped is in the **满血** state, in an admin command prompt:

```
powercfg /requests
```

You should see `[PROCESS] amped.exe` listed under both `SYSTEM:` and `DISPLAY:`. Toggle to **空杯** and rerun — `amped.exe` should disappear from those sections.

## Customizing the icon

`make_icons.ps1` draws a stylized lightning bolt with `System.Drawing` and packages 16/32/48 px frames into multi-resolution `.ico` files. Two ways to customize:

1. **Tweak the design** — edit the polygon vertices or fill colors near the top of `make_icons.ps1`, then rerun:
   ```
   powershell -ExecutionPolicy Bypass -File make_icons.ps1
   ```
2. **Drop in your own** — replace `full.ico` / `empty.ico` with any pair of multi-resolution icons of your choosing, then rebuild.

`full_preview.png` and `empty_preview.png` are 128 px PNG previews written next to the icons for visual eyeballing — they are **not** used at build time.

## File map

| File | Purpose |
|---|---|
| `main.cpp` | All the logic — RAII guards, tray/window handling, message loop. |
| `resource.h` | Resource and command IDs. |
| `amped.rc` | References icons + manifest. |
| `amped.manifest` | Common Controls v6 + per-monitor DPI awareness. |
| `make_icons.ps1` | Generates `full.ico` / `empty.ico` (and 128 px PNG previews). |
| `build.bat` | One-shot build: icons → rc → cl. |
| `CMakeLists.txt` | CMake build (with the same icon-generation step wired in). |

## Implementation notes

- Hidden **top-level** window (not `HWND_MESSAGE`) so it receives the `TaskbarCreated` shell broadcast and can re-add the icon if Explorer restarts.
- `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)` while active; `ES_CONTINUOUS` to release. RAII destructor unconditionally releases on exit.
- Tray callback uses `NOTIFYICON_VERSION_4`, with mouse events arriving in `LOWORD(lParam)`.
- Single-instance via `Local\AmpedTraySingleton` named mutex.
