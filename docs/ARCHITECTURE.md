# Hexwright: architecture

Hexwright is a Windows desktop trainer / mod launcher for **single-player** games, written in C++20. It lets you
attach to a running game, scan its memory, keep an address list, and run per-game trainers (toggles, sliders,
hotkeys, an in-game overlay). It is original code and branding; it is not affiliated with any other trainer.

## Layers

```
 ┌──────────────────────────── hexwright.exe (src/app) ────────────────────────────┐
 │  Dear ImGui + Direct3D11 UI: Home, Games, Process/Scanner, Address list,        │
 │  Trainer pages, Settings, overlay window                                          │
 └───────────────▲──────────────────────────────────────────────────────────────────┘
                 │ plain C++ calls (no UI code below this line)
 ┌───────────────┴──────────────── hexcore (src/core, static library) ──────────────┐
 │ proc      process list, modules, bitness, RAII handle                             │
 │ memory    ReadProcessMemory / WriteProcessMemory / VirtualQueryEx, PE sections    │
 │ value     typed values (i8..i64, f32, f64) parse / format / compare               │
 │ scanner   exact / unknown / increased / decreased / changed / unchanged scans     │
 │ aob       byte patterns with wildcards, RIP-relative resolution                   │
 │ pointer   multi-level chains from a module base                                   │
 │ table     address list (freeze, edit, save per game)                              │
 │ trainer   definition loader + validator, version (exe hash) check, hook engine,   │
 │           cheat gating, locks, console driver, hotkeys                            │
 │ safety    multiplayer / anti-cheat blocklist -> refuses to attach                 │
 │ library   Steam / Epic / GOG detection with a manifest cache                      │
 │ catalog   game definitions pulled from GitHub (content hash, ETag)                │
 │ updater   GitHub Releases self-update                                             │
 │ http, hash, json, util, settings                                                  │
 └────────────────────────────────────────────────────────────────────────────────────┘
 setup (src/setup)  Hexwright-Setup.exe: installer + launcher + uninstaller (one binary)
 cli   (src/cli)    hexctl.exe: validate / preview / new-game / find-games / scan helpers
 tests/             doctest unit tests for every core module
```

## Data flow for a cheat

1. The user picks (or the app detects) a running process. `safety::check` runs first; if the process, one of its
   modules, or a running anti-cheat service is on the blocklist, attaching is refused with a plain message.
2. `Session::attach` opens the process with the minimum rights (VM read/write/operation/query), records
   bitness (x86/x64), image base/size and the exe SHA-256.
3. If a trainer definition matches the process, its **symbols** are resolved once: by AOB signature
   (patch resilient), by `module+rva` (valid only for listed exe hashes) or by pointer chain. Unresolved symbols
   disable only the cheats that need them.
4. Cheats are gated centrally (`Trainer::gate`): the process must be attached; console cheats additionally need
   the game's console to be openable, so keystrokes can never leak into the game.
5. Memory cheats write typed values, optionally **locked** (re-asserted on a timer) and **restored** on off/detach.

## Safety (hard requirements)

* Single-player only: blocklist of multiplayer/competitive titles and known anti-cheat processes, services and
  kernel drivers (EasyAntiCheat, BattlEye, Vanguard, FACEIT, Ricochet, PunkBuster, XIGNCODE3, nProtect GameGuard,
  EA Javelin, Denuvo-style AC, mhyprot). Detected -> refuse, no override.
* No kernel code, no driver, no hiding, no anti-cheat bypass, no network packet manipulation.
* Only the process the user explicitly selected is ever opened, and it is closed on detach or when it exits.
* The process handle is opened with least rights; failures (access denied, process gone) are reported, never fatal.

## Packaging and updates

* Per-user install in `%LOCALAPPDATA%\Hexwright` (no admin): a launcher/updater `Hexwright.exe` plus one
  `app-X.Y.Z\Hexwright.exe` folder per version, Start Menu/Desktop shortcuts, an *Apps & features* entry.
* Updates: GitHub Releases (`Hexwright-Setup.exe`, SHA-256 verified), silent in-place install, previous version kept.
* Game definitions (JSON) live in the repo and are pulled by content hash when the game is detected; no app
  release or version bump is needed to update a game.
* Settings and tables live in `%APPDATA%\Hexwright`.

## Build

CMake + Ninja + MSVC (C++20, static CRT, x64). Dependencies are fetched by CMake at configure time: Dear ImGui,
nlohmann/json, stb_image (UI), doctest (tests). Everything else is the Windows SDK (WinHTTP, BCrypt, D3D11, Toolhelp).
