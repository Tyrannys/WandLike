# WandLike

A Wand/WeMod-style trainer framework. Each game is a JSON plugin in `wandlike/games/`; the first is **Crusader Kings III**.

```
python -m wandlike              # GUI
python -m wandlike --dry-run    # GUI, logs commands instead of typing them
python -m wandlike --list       # games + cheats
python -m wandlike --run ck3 dread 100        # headless
python -m wandlike --run ck3 instabuild on
```
Python 3.10+, Windows, no third-party packages.

## The app
A Wand-style desktop app (pywebview/WebView2, frameless window, dark theme), with everything free:
- **Home / My Games / Explore**: Steam libraries are scanned for supported games; cover and hero art come from Steam's local cache. Favorites, recently played, search.
- **Game page**: Play (launches through Steam, with `-debug_mode` for CK3), collapsible sections, info tooltips, number steppers with *Set/Add* mode, On/Off toggles with an options gear, search.
- **Pin** cheats (pin icon) → they appear in a *Pinned* section and in the **in-game overlay** (default `Ctrl+Shift+F10`; run the game windowed/borderless).
- **Global hotkeys** per cheat (keyboard icon on a row), with conflict detection.
- **Saved cheats**: bookmark icon saves your enabled toggles and applied values; *Apply saved* re-applies them; *Auto-apply* does it ~20 s after the game starts.
- **Settings**: hotkeys on/off, overlay hotkey, console key (backtick or Shift+Alt+C), start with Windows, update check.
- Activity log (bottom-left) shows each command sent to the game.
Not implemented: a phone remote and cloud sync.

Preview the UI in any browser with `python tools/dev_server.py` (dry-run backend).

## Install, update, release
Run `dist\WandLike-Setup.exe` (build it with `.\build.ps1`; needs Python 3.10+, it creates `.venv-build` and installs PyInstaller there). It installs per-user, no admin, Wand-style:
```
%LOCALAPPDATA%\WandLike\WandLike.exe   launcher + uninstaller (same binary as the setup)
%LOCALAPPDATA%\WandLike\app-1.0.0\     one folder per version (current + previous kept)
```
It adds Start Menu/Desktop shortcuts and an *Apps & features* uninstall entry. Settings live in `%APPDATA%\WandLike` and survive uninstall; extra game plugins can be dropped in `%APPDATA%\WandLike\games\*.json`.

**Auto-update** uses GitHub Releases:
1. Set `UPDATE_REPO = "owner/repo"` in `wandlike/version.py`.
2. Push a tag `vX.Y.Z`. `.github/workflows/release.yml` runs the tests, builds, and publishes `WandLike-Setup.exe` to the release. (Or build locally with `.\build.ps1 -Version X.Y.Z` and upload `dist\WandLike-Setup.exe` yourself; the asset must keep that name.)
3. Installed apps check on startup (and when you click the version label), show an *Update & restart* banner, download the asset (SHA-256 verified against GitHub's digest), and run it silently; it waits for the app to exit, installs the new `app-X` folder and relaunches.

Only releases of that repo, only non-draft/non-prerelease, are accepted. The exes are unsigned, so SmartScreen may warn on first run.

## CK3 setup
1. Click **Enable Console** (launches via Steam with `-debug_mode`), or add `-debug_mode` to the launch options.
2. Play a **non-Ironman** game (using the console disables achievements for that save).
3. Start WandLike, keep CK3 in windowed/borderless mode, and use the cheats. Each action briefly focuses CK3, opens the console (backtick, scancode 0x29), types the command, and closes the console.
   - If your keyboard layout uses another console key, set `toggle_scancode` or `toggle_combo` (e.g. `["shift","alt","c"]`) in `games/ck3.json`.
   - Make sure the console is **closed** when you press a cheat, otherwise the toggle inverts.

### Cheat status
All 19 cheats from the Wand list plus Treasury and Influence are present.
- **wiki**: command documented on the CK3 wiki.
- **inferred**: "Set" for gold/prestige/piety/renown/treasury/influence uses `effect add_X = { value = N subtract = X }`. This is built from script values, not tested in-game. Use the `=`/`+` button on a row to switch to the wiki-verified *add* command if it errors. `Max County Control` is also inferred.
- **Freeze Time** is disabled: it needs a memory offset for the pause flag, and CK3 isn't installed on the dev machine. Treasury needs *All Under Heaven*, Influence needs *Roads to Power*.
- Instant Movement has no "off" command in the game; restart to clear. Instant Construction's command is itself a toggle.
- Unlike Wand, the console can't be used in Ironman.

## Adding a game
Drop a JSON file in `wandlike/games/`:
```json
{ "id": "mygame", "name": "My Game", "exe": "game.exe", "window_title": "My Game",
  "steam_appid": 12345, "launch_args": "-console", "console": {"toggle_scancode": 41},
  "cheats": [
    {"id": "gold", "label": "Set Gold", "kind": "number", "set": "setgold {value}", "default": 100},
    {"id": "god",  "label": "God Mode", "kind": "toggle", "on": ["tgm"], "off": ["tgm"]},
    {"id": "hp",   "label": "Set Health", "kind": "number",
     "memory": {"module": "game.exe", "offsets": ["0x1A2B30", "0x10", "0x58"], "type": "float"}}
  ]}
```
- `kind`: `number`, `toggle` (`on`/`off` lists, optional `interval` to repeat), `button` (`run` list).
- Console templates use `{value}`; write literal braces as `{{ }}` in `set`/`add` only.
- A cheat with a `memory` block uses the pointer-chain backend (`core/memory.py`) instead of the console. Needs the right offsets for that game version.

## Layout
`core/engine.py` actions → console/memory · `core/winapi.py` window focus + SendInput · `core/memory.py` ReadProcessMemory · `core/plugin.py` JSON schema · `ui/app.py` Tk interface · `tests/`.
