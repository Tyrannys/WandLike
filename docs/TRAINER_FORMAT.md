# Writing a trainer definition

A trainer definition is one JSON file per game. Hexwright reads it, shows the cheats as toggles / number boxes /
buttons, and applies them to the game you attach to. The same files are served from this repo's
`wandlike/games/` folder (the catalog), so a definition you contribute reaches everyone's app without a release.

```
hexctl find-games stellaris              see what your libraries contain (store, id, folder)
hexctl new-game stellaris                write a starter definition (store id, exe guessed from the install folder)
hexctl validate path\to\game.json        every problem at once, with typo suggestions ("did you mean set?")
hexctl preview  path\to\game.json        the exact lines / memory writes each cheat would do, no game needed
```

Drop a finished file in `%APPDATA%\Hexwright\games\` and the app loads it at once. Your own files override catalog ones.
Unknown keys, bad ids, files over 256 KB and dangling symbol references are errors: the loader is deliberately strict.

## Top level

| key | required | meaning |
|---|---|---|
| `id` | yes | lowercase id (`a-z 0-9 _ -`), also the file name |
| `name` | yes | shown in the UI |
| `exe` | yes | the game's process name, e.g. `ck3.exe` |
| `window_title` | yes | part of the game window's title (used to find the window for console typing) |
| `match` | no | how the game is recognised in your libraries: `{"steam_appid": N}`, `{"epic": "<AppName>"}` or `{"gog": "<id>"}` |
| `steam_appid` | no | shortcut for `match`, also used for launching and artwork |
| `launch_args` | no | arguments used by the Play button |
| `notes` | no | shown under the game header |
| `version` | no | informational only. Updates are detected by file *content*, never by this number |
| `min_app` | no | oldest Hexwright that understands this file (older apps skip it politely) |
| `exe_sha256` | no | list of exe SHA-256 hashes the definition was written for; any other build shows a warning |
| `console` | no | how console cheats are typed (below) |
| `hook` | no | `{"symbols": {...}}`, named memory locations (below) |
| `cheats` | yes | the cheat list |

## Cheats

```json
{"id": "gold",   "label": "Set Gold",  "kind": "number", "set": "setgold {value}", "add": "addgold {value}",
 "default": 1000, "min": 0, "max": 99999999, "step": 1000, "info": "Sets your gold.", "verified": "tested"},
{"id": "god",    "label": "God Mode",  "kind": "toggle", "on": ["tgm"], "off": ["tgm"]},
{"id": "freeze", "label": "Freeze Time", "kind": "toggle", "memory": {"symbol": "paused", "on": 1, "off": 0, "lock": 0.02}},
{"id": "heal",   "label": "Heal",      "kind": "button", "run": ["heal player"]}
```

- `kind`
  - `number`: `set` and/or `add` console templates containing `{value}` (write literal braces as `{{` `}}`), or a `memory` block.
  - `toggle`: `on` / `off` console lines, an optional `interval` (repeat `on` every N seconds), optional `options`, or a `memory` block.
  - `button`: `run` lines; `"launch": true` makes the button start the game.
- `section` groups cheats on the game page (default `Player`). `requires` is shown as "Requires ..." (DLC, government).
- `verified` is shown in the tooltip, so be honest: `tested` (you saw it work), `parsed` (the game accepts it, you could not observe it),
  `wiki` (documented elsewhere), `inferred`, or `unsupported` (with an `unsupported` explanation; the row is disabled).
- `options` (toggles): `[{"id": "speed", "label": "Speed", "min": 1, "max": 10, "default": 2}]` become number boxes while the toggle is on.

### Memory cheats

```json
"memory": {"symbol": "gold_ptr", "type": "int32", "scale": 1}                  // number: writes value * scale
"memory": {"symbol": "paused", "on": 1, "off": 0, "lock": 0.02}                // toggle: re-asserts `on` every 0.02 s
"memory": {"symbol": "console", "on": 1, "restore": true, "lock": 0.25}        // toggle: puts the original value back
"memory": {"chain": "game.exe+5C7E378,20,8", "type": "float"}                  // inline pointer chain, no symbol
```

`lock` keeps a value pinned while the toggle is on; `restore` writes back the value the game had instead of `off`.

## Symbols (`hook.symbols`)

A symbol is a named memory location. There are three ways to find one; combine them as needed.

```json
"hook": {"symbols": {
  "paused": {"type": "uint8", "section": ".text",
             "aob": "48 8B 0D ?? ?? ?? ?? 41 B8 FF FF FF FF 0F B6 51 20 E9",
             "rip": {"disp_at": 3, "insn_len": 7}, "deref": [32]},
  "gold":   {"type": "int32", "chain": "game.exe+1A2B3C,10,8"},
  "fixed":  {"type": "float", "rva": 19088736, "exe_size": 98765432}
}}
```

- `aob`: a byte pattern from the game's code. `??` (or `?`) is any byte; `4?` / `?B` fix one nibble. Because it is searched
  in the running game, it survives patches that move the code. A pattern that matches more than once is refused unless you set
  `"unique": false` and pick one with `index`.
- `rip`: the pattern sits on an instruction with a RIP-relative operand (`mov rcx,[rip+disp32]`). The symbol is the variable it
  references: `disp_at` is the offset of the 32-bit displacement inside the match, `insn_len` the instruction length.
  Without `rip`, `offset` is added to the match address.
- `deref`: after finding the address, follow this pointer chain on every read / write (`[32]` = read a pointer, add 0x20).
- `chain`: `module+rva,off1,off2` (hexadecimal). The module base plus the first number, then each later number is added after
  reading a pointer. This is the "pointer chain from a module base" that survives game restarts. A chain with no module name
  starts from an absolute address.
- `rva` + `exe_size` (or a definition-level `exe_sha256`): a fixed address, trusted only for that exact build.
- `type`: `uint8`, `int16`, `int32`, `int64`, `float`, `double`.

### Finding a symbol

1. Attach the **Scanner** page to the game. Scan for a value you can see (exact), change it in game, scan again (changed /
   increased / decreased), until a few addresses remain. Add one to the address list; freeze or edit it to check it is the right one.
2. If the address moves every run, look for what points at it: scan for the address as an 8-byte value, and repeat until you reach
   an address inside the game's module (`game.exe+offset`). That is a pointer chain.
3. For something that survives patches, find the code that reads it (a disassembler such as x64dbg, or `tools/pe_re.py`),
   write a byte pattern around the instruction with `??` for the bytes that change, and test it:
   `hexctl aob game.exe "48 8B 0D ?? ?? ?? ?? 41 B8"` must report exactly one match.
4. `hexctl read game.exe game.exe+5C7E378,20,8 int32` checks a chain.

Always test a candidate by writing it and watching the game.

## Console

```json
"console": {"toggle_scancode": 41, "open_delay": 0.25, "line_delay": 0.15, "type_delay": 0.008,
            "close_after": true, "requires_symbol": "console"}
```

Console cheats type into the game's own console. `toggle_scancode` (41 = backtick) or `toggle_combo` (`["shift","alt","c"]`) opens it.
`requires_symbol` names a symbol that must read non-zero first: if the console cannot be opened Hexwright refuses to type
anything, so keystrokes never reach the game as hotkeys.

## Catalog and updates

Definitions in `wandlike/games/` plus a generated `_index.json` of content hashes are served from this repo. Hexwright fetches the
index (ETag, so "unchanged" is a 304), and for each **installed** game compares the hash with the definition it already has. If the
content differs it downloads the new file, verifies the SHA-256 and the strict loader, and swaps it in live. Uninstalled games are
never downloaded. Precedence: your own files > downloaded > bundled in the exe.

To update a definition: edit `wandlike/games/<id>.json`, commit, push to `main`. The `catalog-index` workflow runs
`hexctl catalog` and commits the new `_index.json`. No version bump, no app release. The repository must be public.
Definitions type commands into games and write memory, so only merge ones you trust: the hash guards against corruption, not
against a malicious commit.
