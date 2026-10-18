# Hexwright

A free, original trainer / mod launcher for **single-player** Windows games, written in C++20 with a native Dear ImGui interface.
Attach to a running game, toggle cheats, scan memory Cheat Engine style, keep an address list, and let per-game
definition files (JSON) do the work. The first supported game is **Crusader Kings III**.

> Formerly "WandLike". Hexwright is a complete C++ rewrite of that Python app; the game definition format is unchanged.

- **Install:** download `Hexwright-Setup.exe` from [Releases](https://github.com/Tyrannys/WandLike/releases). It installs per user into
  `%LOCALAPPDATA%\Hexwright` (no admin needed), adds Start menu / desktop shortcuts, and updates itself.
- **Requirements:** Windows 10/11 x64 and a Direct3D 11 capable GPU (falls back to the software renderer). It can attach to 64-bit and 32-bit games.

## Safety: single-player only

Hexwright is built to be hard to misuse online, and there is no override:

- It **refuses to attach** if an anti-cheat is running (EasyAntiCheat, BattlEye, Vanguard, FACEIT, Ricochet, nProtect GameGuard, Xigncode,
  Javelin, PunkBuster, ...) or the target is a known multiplayer / competitive game. The message says what triggered it.
  Settings → Safety lists everything blocked.
- No anti-cheat bypass or evasion, no driver or kernel code, no network packet manipulation.
- It only opens the one process you pick, with the minimum rights (read, write, query), never debug or terminate rights.
- If the game exits, everything is released cleanly and the UI tells you.

## What it does

- **Games**: your Steam, Epic and GOG libraries are scanned for games that have a trainer definition. Cover art comes from Steam's local cache.
- **Trainer page**: sections, search, pins, favorites, On/Off switches, number steppers with Set / Add, per-cheat **global hotkeys**,
  saved presets (optionally applied when the game starts), an exe-hash check that warns when the game build isn't the one the
  definition was written for. The big button is **Play** (closed), **Hook** (running) or **Unhook** (hooked); the app also hooks by itself.
  Every cheat needs the game hooked; console cheats also need the console to be openable (see below).
- **Scanner**: process picker with a safety verdict, first scan (exact / unknown initial / bigger / smaller / between), next scan
  (exact / increased / decreased / changed / unchanged / increased by / decreased by), types byte / int16 / int32 / int64 / float / double,
  progress and cancel.
- **Address list**: add scan results or a pointer chain (`game.exe+5C7E378,20,8`), edit values, **freeze** them, save / load per game
  (`%APPDATA%\Hexwright\tables\<exe>.json`). Entries inside a module are stored module-relative so they survive restarts.
- **AOB scan**: find a byte pattern with wildcards in the main module's code.
- **Overlay**: a small always-on-top list of active cheats (default `Ctrl+Shift+F10`; run the game windowed or borderless).
- **Settings**: hotkeys, overlay key, console key, start with Windows, library rescan, catalog sync, update check.

## Two ways cheats work

1. **Console cheats** type commands into the game's own console. The game window briefly takes focus. If the console can't be opened the
   cheat is refused *before* anything is typed, so keystrokes can never land in the game as hotkeys.
2. **Memory cheats** read and write named memory symbols with no keystrokes. Symbols are found by **byte pattern**, so they survive
   game patches; if a patch breaks one, only that cheat reports "this game version isn't supported yet". A *lock* keeps a value pinned.

Run the game, **Hook**, and unhook (or exit the game) when you are done: toggled memory cheats restore the game's own values.

## Crusader Kings III

1. Press **Play** (normal launch, **no `-debug_mode` option**). Hexwright hooks once the game is running.
2. Switch on **Enable Console**. It sets the byte the game checks before opening its console, so the console (the `` ` `` key) and the
   console-based cheats work without debug mode. The game clears that byte when a new game loads, so Hexwright keeps it set while the
   toggle is on and puts the original value back when you turn it off or unhook. While it is on the game shows its "DEBUG:" tooltips.
3. Use a **non-Ironman** game you don't mind modding: console commands disable achievements for that save.
4. If your console key isn't backtick, set it in Settings.

**Freeze Time** and **Enable Console** are memory cheats and work as soon as the game is hooked; every other CK3 cheat is a console cheat.
Each cheat's tooltip says how it was checked (tested / parsed / wiki). Tested on CK3 1.20.1.1: Enable Console, Money, Prestige, Piety, Renown,
Dread, the six skills, King Age and Freeze Time.

## Writing a trainer definition

A definition is one JSON file; the full reference is in [docs/TRAINER_FORMAT.md](docs/TRAINER_FORMAT.md). A taste:

```json
{ "id": "mygame", "name": "My Game", "exe": "game.exe", "window_title": "My Game",
  "match": {"steam_appid": 12345},
  "console": {"toggle_scancode": 41},
  "hook": {"symbols": {"paused": {
      "type": "uint8", "aob": "48 8B 0D ?? ?? ?? ?? 41 B8 FF FF FF FF 0F B6 51 20 E9",
      "rip": {"disp_at": 3, "insn_len": 7}, "deref": [32]}}},
  "cheats": [
    {"id": "gold", "label": "Set Gold", "kind": "number", "set": "setgold {value}", "add": "addgold {value}", "default": 1000, "step": 1000},
    {"id": "god", "label": "God Mode", "kind": "toggle", "on": ["tgm"], "off": ["tgm"]},
    {"id": "freeze", "label": "Freeze Time", "kind": "toggle", "memory": {"symbol": "paused", "on": 1, "off": 0, "lock": 0.02}}
  ] }
```

`hexctl` (built next to the app, also attached to releases) helps you author them:

```
hexctl find-games stellaris        # what your libraries contain
hexctl new-game stellaris          # starter definition
hexctl validate game.json          # every problem at once, with typo suggestions
hexctl preview game.json           # what each cheat would do, no game needed
hexctl aob game.exe "48 8B 0D ?? ?? ?? ??"      # signature scan in a running game
hexctl read game.exe game.exe+5C7E378,20,8 int32 # check a pointer chain
hexctl scan game.exe int32 1000                  # first scan from the command line
hexctl catalog                      # regenerate wandlike/games/_index.json
```

Your own definitions go in `%APPDATA%\Hexwright\games\` and load immediately.

## Game catalog (definitions update without an app release)

Definitions in `wandlike/games/` plus a generated `_index.json` of SHA-256 content hashes are served from this repo. For each **installed**
game Hexwright compares the index hash with the definition it has; if the content differs it downloads, verifies and swaps it in live.
Uninstalled games are never downloaded. To update a definition: edit the file, push to `main`, and the `catalog-index` workflow refreshes the index.
No version bump, no app release. (The folder keeps its old name so installed 1.x clients keep working.)

## Building

Needs Windows 10/11 and Visual Studio 2022+ (or Build Tools) with **Desktop development with C++**. CMake and Ninja come with it; all other
dependencies are vendored in `third_party/` (nlohmann/json, doctest, Dear ImGui, stb_image; licenses in `third_party/licenses/`).

```
git clone https://github.com/Tyrannys/WandLike.git
cd WandLike
.\build.ps1                      # tests + dist\Hexwright-Setup.exe + dist\hexctl.exe
.\build.ps1 -Version 2.0.1       # also sets the version
.\build.ps1 -TestOnly            # just build and run the unit tests
```

If PowerShell refuses to run the script: `powershell -ExecutionPolicy Bypass -File .\build.ps1`.
By hand: `. .\tools\vsenv.ps1; cmake -S . -B build\win -G Ninja -DCMAKE_BUILD_TYPE=Release; cmake --build build\win; build\win\hextests.exe`.

### Debug build (for testers and contributors)

```
.\build.ps1 -Debug               # or: .\build.ps1 --debug   ->   dist\Hexwright-Debug.exe
```
Standalone (no installer, doesn't touch an installed copy). Run it **from a terminal**: it shows a console with a timestamped log of every
hook, memory write, console command, update check and error, also saved to `%APPDATA%\Hexwright\debug.log`. To reproduce a problem, use the
debug exe and attach `debug.log` to the issue. A normal build can also be started with `--debug` to get the log file.

### Tests

`hextests.exe` (doctest) covers every core module: scanner (all scan kinds and types), AOB patterns and chunk edges, the pointer resolver
(including a real 32-bit process), definitions and validation, the trainer engine, the address table and freezing, hotkeys, the safety
blocklist, library / catalog / updater with injected network, and authoring helpers.

## Install, update, release

`Hexwright-Setup.exe` installs per user:

```
%LOCALAPPDATA%\Hexwright\Hexwright.exe   launcher + uninstaller (same binary as the setup)
%LOCALAPPDATA%\Hexwright\app-X.Y.Z\      one folder per version (current + previous kept)
```
It adds shortcuts and an *Apps & features* entry. Settings live in `%APPDATA%\Hexwright` and survive uninstall. On first install it
migrates settings from, and removes, the old WandLike install.

**Auto-update** uses GitHub Releases: pushing a tag `vX.Y.Z` runs `.github/workflows/release.yml`, which builds, tests and publishes
`Hexwright-Setup.exe` (plus a `WandLike-Setup.exe` copy so installed 1.x apps can update across the rewrite). Installed apps check at startup,
verify the download against GitHub's SHA-256 digest and install it silently. The exes are unsigned, so SmartScreen may warn on first run.

## Layout

`src/core` engine (process, memory, scanner, AOB, pointers, definitions, trainer, safety, library, catalog, updater) ·
`src/app` ImGui interface · `src/setup` installer / launcher · `src/cli` `hexctl` · `tests` unit tests · `wandlike/games` definitions + catalog index ·
`third_party` vendored libraries · `docs` ([architecture](docs/ARCHITECTURE.md), [plan](docs/PLAN.md), [definition format](docs/TRAINER_FORMAT.md)) ·
`tools/pe_re.py` optional reverse-engineering helper (Python + capstone).
