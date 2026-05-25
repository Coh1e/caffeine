# Caffeine

A tiny Windows tray utility that keeps your PC awake. Launch it and your
computer stops sleeping; double-click the tray icon to quit and restore
normal power behavior. No window, no settings dialog — just the tray.

- **Language:** C++20, pure Win32 (no Qt, no .NET, no MFC, no Electron).
- **Footprint:** single source file, links only `user32` and `shell32`.
- **Permissions:** runs as a normal user. No admin, no service.

## Behavior

| Action | Effect |
|---|---|
| **Launch** | Keeps the system awake immediately (display may still sleep). |
| **Right-click** tray icon | Menu: `阻止息屏` (checkable), `退出`. |
| `阻止息屏` (checked) | Also keeps the display on (`ES_DISPLAY_REQUIRED`). |
| **Double-click** tray icon | Quit — restores normal sleep and screen-off. |
| Single left-click | Nothing. |

Tooltip reads `Caffeine：防休眠中（屏幕仍会息屏）` by default, and
`Caffeine：防休眠 + 防息屏` once `阻止息屏` is enabled.

Only one instance runs per user session (a named mutex blocks duplicates).

## Build

Requires the MSVC toolchain (Visual Studio Build Tools or full VS).

```
cmake -S . -B build
cmake --build build --config Release
```

The executable is produced at `build\Release\caffeine.exe`.

## What Caffeine *can* prevent

- The system going to sleep due to idleness.
- The display turning off due to idleness — only while `阻止息屏` is enabled.

## What Caffeine *cannot* prevent

- The user manually clicking **Sleep** / **Shut down** / **Hibernate**.
- A laptop sleeping when the lid is closed (lid-close policy).
- Enterprise Group Policy that forces lock or hibernate.
- Low-battery hibernation.
- Forced reboot from Windows Update.

`SetThreadExecutionState` is a *hint* to the power manager, not an override.

## Verifying it actually works

While Caffeine is running, in a command prompt:

```
powercfg /requests
```

You should see `[PROCESS] caffeine.exe` under `SYSTEM:`. Enable `阻止息屏`
and rerun — it should also appear under `DISPLAY:`. Quit (double-click) and
rerun — `caffeine.exe` disappears from both sections.

## File map

| File | Purpose |
|---|---|
| `main.cpp` | All the logic — RAII guards, tray/window handling, message loop. |
| `resource.h` | Resource and command IDs. |
| `caffeine.rc` | References the icon + manifest. |
| `caffeine.manifest` | Common Controls v6 + per-monitor DPI awareness. |
| `caffeine.ico` | Coffee-cup tray icon. |
| `CMakeLists.txt` | CMake build. |

## Implementation notes

- Hidden **top-level** window (not `HWND_MESSAGE`) so it receives the
  `TaskbarCreated` shell broadcast and can re-add the icon if Explorer restarts.
- `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED [| ES_DISPLAY_REQUIRED])`
  while running; `ES_CONTINUOUS` to release. The RAII destructor unconditionally
  releases on exit.
- Tray callback uses `NOTIFYICON_VERSION_4`, with mouse events in `LOWORD(lParam)`.
- Single-instance via `Local\CaffeineTraySingleton` named mutex.

## Credits / 致谢

This project was inspired by, and reuses an asset from, the original
**Caffeine** by Kyle Leong — thank you.

- Coffee-cup tray icon (`caffeine.ico`) is taken from
  [kyleleong/caffeine](https://github.com/kyleleong/caffeine)
  (MIT License, © 2020 Kyle Leong). The interaction design also drew
  inspiration from that project. 感谢 Kyle Leong。
- That icon in turn originates from
  [famfamfam Silk Icons](https://www.famfamfam.com/lab/icons/silk/)
  (CC-BY 2.5, by Mark James) and
  [Freepik](https://www.flaticon.com/authors/freepik/). 一并致谢原作者。

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the upstream MIT
license text.
