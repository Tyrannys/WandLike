# WandLike

A free Wand/WeMod-style trainer framework for Windows. Every game is a small JSON plugin; the first is **Crusader Kings III**.

- Download the installer from the [Releases](https://github.com/Tyrannys/WandLike/releases) page (`WandLike-Setup.exe`). It installs per-user, no admin, and updates itself.
- Or run from source: `pip install -r requirements.txt` then `python -m wandlike` (Python 3.10+, Windows 10/11, WebView2 runtime, which Windows 11 includes).

## The app
- **Home / My Games / Explore**: your Steam, Epic and GOG libraries are scanned for supported games; art comes from Steam's local cache.
- **Game page**: collapsible sections, info tooltips, number steppers with *Set / Add* mode, On/Off toggles with an options gear, search, pins, favorites.
- **The big button** is **Play** when the game is closed, **Hook** when it is running, **Unhook** when hooked. Hooking first checks that the game is really running, then attaches to it for real-time memory cheats (see below).
- **Pin** cheats to get a *Pinned* section and an **in-game overlay** (default `Ctrl+Shift+F10`; run the game windowed/borderless).
- **Global hotkeys** per cheat, with conflict detection. **Saved cheats** (bookmark icon) re-apply your setup, optionally automatically when the game starts.
- **Settings**: hotkeys, overlay key, in-game console key, start with Windows, library rescan, catalog sync, update check.
- **Activity** (bottom-left) logs every command and memory write.

Not implemented: a phone remote and cloud sync.

## Two ways cheats work
1. **Console cheats** type commands into the game's own console (CK3: debug mode). Safe and patch-proof, but the game window briefly takes focus.
2. **Hooked (memory) cheats** attach to the game process and read/write named memory symbols in real time, with no keystrokes. Symbols are found by **byte pattern**, so they survive game patches. If a patch breaks a pattern, only that cheat reports *"this game version isn't supported yet"*. A *lock* option keeps a value pinned (that is how **Freeze Time** keeps CK3 paused even if you press Space).

Memory cheats need the game **hooked** (press **Hook**; WandLike also auto-hooks a few seconds after a hookable game starts, see *Settings*). Never hook a multiplayer or Ironman game.

## Crusader Kings III
1. Press **Play**. WandLike starts `ck3.exe -debug_mode` itself (Steam must be running), which avoids Steam's *Launch Game with custom arguments* prompt that `steam://` and `-applaunch` launches trigger. Or add `-debug_mode` to the Steam launch options yourself.
2. Use a **non-Ironman** game. Console commands disable achievements for that save.
3. Keep CK3 windowed/borderless. If your console key isn't the backtick key, change it in *Settings → In-game console key*.
4. For **Freeze Time**, press **Hook** first.

Status of each cheat (shown in its tooltip), as checked against CK3 1.20.1.1:
- **tested**: observed working in the game: Money, Prestige, Piety, Renown, Dread, Diplomacy, Martial, Stewardship, Intrigue, Learning, Prowess, King Age, **Freeze Time**.
- **parsed**: the game's script parser accepts it, but it needs a DLC/government to observe: Treasury (All Under Heaven), Influence (Roads to Power), Max County Control.
- **wiki**: documented console command, not individually observed: Stress, Instant Construction, Instant Movement, Fast Culture Progress.
- Notes: `Set King Age` uses `set_age` (the plain `age` console command *adds* years). Instant Movement has no "off" command in the game, so restart to clear it. Event pop-ups pause the game by a separate mechanism, so Freeze Time doesn't (and shouldn't) block them.

## Making a game plugin
A plugin is one JSON file. The helpers below work from source (`python -m wandlike ...`) or the debug exe.

```
python -m wandlike --find-games stellaris           # see what your libraries contain (store, id, folder)
python -m wandlike --new-game stellaris              # write a starter plugin (store id, exe guessed from the install folder)
python -m wandlike --validate path\to\plugin.json    # clear errors, typo suggestions ("did you mean set?")
python -m wandlike --preview path\to\plugin.json     # the exact lines each cheat would type, no game needed
```
`--new-game` writes to `wandlike/games/` when run from source, or to your plugins folder (`%APPDATA%\WandLike\games`, *Settings → Open folder*) from an installed build. Plugins in that folder load immediately and override catalog versions.

```json
{ "id": "mygame", "name": "My Game", "version": 1, "match": {"steam_appid": 12345},
  "exe": "game.exe", "window_title": "My Game", "launch_args": "-console",
  "console": {"toggle_scancode": 41},
  "cheats": [
    {"id": "gold", "label": "Set Gold", "kind": "number", "info": "Sets your gold.",
     "set": "setgold {value}", "add": "addgold {value}", "default": 1000, "step": 1000},
    {"id": "god",  "label": "God Mode", "kind": "toggle", "on": ["tgm"], "off": ["tgm"]},
    {"id": "freeze", "label": "Freeze Time", "kind": "toggle",
     "memory": {"symbol": "paused", "on": 1, "off": 0, "lock": 0.02}}
  ],
  "hook": {"symbols": {"paused": {
      "type": "uint8", "aob": "48 8B 0D ?? ?? ?? ?? 41 B8 FF FF FF FF 0F B6 51 20 E9",
      "rip": {"disp_at": 3, "insn_len": 7}, "deref": [32]}}}}
```
- `match` says how the game is recognised: `{"steam_appid": N}`, `{"epic": "<AppName>"}` or `{"gog": "<id>"}`.
- `kind`: `number` (`set` and/or `add` templates containing `{value}`; write literal braces as `{{ }}`), `toggle` (`on`/`off` lines, optional `interval` to repeat, optional `options`), `button` (`run` lines).
- `verified`: `tested`, `parsed`, `wiki`, `inferred` or `unsupported` (with an `unsupported` explanation). Be honest; it shows in the tooltip.
- **Symbols** (`hook.symbols`): `aob` is a byte pattern from the game's code (`??` = any byte). `rip` follows a RIP-relative operand to the variable it references; `deref` then follows a pointer chain (`[32]` = pointer + 0x20). `section` defaults to `.text`. A `rva` + `exe_size` can serve as a fallback for one exact build.
- A cheat's `memory` is `{"symbol", "on", "off", "lock"}` for toggles (`lock` re-asserts the value every N seconds) or `{"symbol", "type", "scale"}` for numbers.

### Finding memory symbols
`tools/` has two helpers (they need `.venv-build\Scripts\pip install capstone` for the disassembler):
- `tools/find_flag.py`: differences memory snapshots while you flip an in-game on/off state (it presses a key for you) and lists candidate flag bytes (`--heap` also scans private memory).
- `tools/pe_re.py`: loads the game exe from disk, finds MSVC RTTI vtables by class name and disassembles functions. That is how CK3's pause byte was found: the pause-command class reads `[global + 0x20]`, and the code around that read became the byte pattern.
Always test a candidate by writing it and watching the game, and check the pattern matches exactly once in the exe.

## Game library and catalog
- **Library scan** (`core/library.py`): Steam (all library folders), Epic (launcher manifests) and GOG (registry). Cached in `%APPDATA%\WandLike\library_cache.json`: manifests are re-read only if their size/mtime changed and folders re-listed only if their mtime changed, so a rescan is a few `stat` calls (every 90 s while the app runs; *Settings → Rescan* forces a full re-read).
- **Catalog** (`core/catalog.py`): plugin JSONs in `wandlike/games/` plus a generated `_index.json` are served from this repo. The app fetches the index (ETag, so unchanged is a 304) and downloads a plugin **only when a detected game matches it** and it's missing or older than the index version. Downloads must match the index SHA-256 and pass the strict loader. Precedence: your own plugins > highest `version` of bundled/catalog.
- **Publishing a plugin or update:** add/edit `wandlike/games/<id>.json`, **bump its `version`**, run `python tools/make_catalog.py`, commit and push to `main`. Installed apps pick it up on their next sync, no app release needed. A test fails if `_index.json` is stale. Use `"min_app": "1.2.0"` if the plugin needs a newer app.
- The repo must be **public**. Plugins are data, but they type commands into games and write memory, so only merge plugins you trust: the hash protects against corruption, not against a malicious commit.

## Building
Release and debug builds use `build.ps1`, which creates a private `.venv-build` and installs PyInstaller and pywebview into it.

### Release build
```
.\build.ps1                  # -> dist\WandLike-Setup.exe
.\build.ps1 -Version 1.2.3   # also sets the version
```

### Debug build (for testers and contributors)
Use this to see a terminal and everything the app is doing when something goes wrong.

**What you need:** Windows 10/11, [Python 3.10+](https://www.python.org/downloads/) on your `PATH` (tick *Add python.exe to PATH*), Git, and the WebView2 runtime (already on Windows 11).

```
git clone https://github.com/Tyrannys/WandLike.git
cd WandLike
.\build.ps1 -Debug        # or: .\build.ps1 --debug
```
The first build downloads PyInstaller and pywebview (a minute or two) and writes **`dist\WandLike-Debug.exe`**. Run it **from a terminal** (`.\dist\WandLike-Debug.exe`) so the log stays visible. It is standalone: no installer, it does not touch your installed copy.

If PowerShell refuses to run the script: `powershell -ExecutionPolicy Bypass -File .\build.ps1 -Debug`. To skip building, run from source with `pip install -r requirements.txt` and `python -m wandlike --debug`.

What the debug build adds:
- A **console window** with a timestamped log of every backend call, engine command, hook/memory write and uncaught exception, also saved to `%APPDATA%\WandLike\debug.log`.
- A red **DEBUG** pill and a **Debug page** in the sidebar: fake an update, pretend a game is running/closed, throw a test exception, send a raw line to a game's console, and view hook/library info, the log tail and your saved settings.
- Right-click → **Inspect** (WebView2 devtools).

**Reporting a problem:** reproduce it in the debug exe, then attach `%APPDATA%\WandLike\debug.log` (and the Debug page's *Environment* box) to the issue.

All debug code lives in `wandlike/debug/` and `wandlike/web/debug.js`. The release build excludes both and `build.ps1` fails if either ends up in the release exe; `--debug` or `WANDLIKE_DEBUG=1` do nothing in a release exe. Put new debug-only code in those two places and it never ships.

## Install, update, release
`WandLike-Setup.exe` installs per-user:
```
%LOCALAPPDATA%\WandLike\WandLike.exe   launcher + uninstaller (same binary as the setup)
%LOCALAPPDATA%\WandLike\app-X.Y.Z\     one folder per version (current + previous kept)
```
It adds Start Menu/Desktop shortcuts and an *Apps & features* entry. Settings live in `%APPDATA%\WandLike` and survive uninstall.

**Auto-update** uses GitHub Releases: pushing a tag `vX.Y.Z` runs `.github/workflows/release.yml`, which tests, builds and publishes `WandLike-Setup.exe`. Installed apps check on startup (and when you click the version label), show an *Update & restart* banner, verify the download against GitHub's SHA-256 digest, and install it silently. Version numbers: `+0.1.0` for major changes, `+0.0.1` for minor ones. The exes are unsigned, so SmartScreen may warn on first run.

## Layout
`wandlike/core/` engine (console + memory), `hook.py` (attach, symbols), `memory.py` (ReadProcessMemory, PE sections, AOB), `library.py`, `catalog.py`, `authoring.py`, `plugin.py` (schema) · `wandlike/app/` Api + window · `wandlike/web/` UI · `wandlike/games/` plugins + `_index.json` · `wandlike/debug/` debug-only · `installer/` · `tools/` (catalog, dev server, RE helpers) · `tests/`.
