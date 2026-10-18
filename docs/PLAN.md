# Hexwright: task plan

Built incrementally in the order of the brief. Every core module ships with unit tests before the UI depends on it.

| # | Milestone | Done when |
|---|-----------|-----------|
| 0 | CMake skeleton, fetched deps, CI | `cmake --build` produces core lib + test runner; tests run |
| 1 | Process picker + memory access + safety | list/attach/detach processes (x86 + x64), blocklist refuses, tests |
| 2 | Memory scanner | exact, unknown initial, increased/decreased/changed/unchanged for i8/i16/i32/i64/f32/f64; tests against a live buffer |
| 3 | AOB + pointer chains | wildcard patterns, RIP resolution, multi-level chains from a module base; tests |
| 4 | Address list / tables | freeze, edit, save/load per game |
| 5 | Trainer loader | JSON definitions (compatible with the existing game files), version/hash warning, toggles, sliders, global hotkeys |
| 6 | Game support plumbing | library scan (Steam/Epic/GOG) with cache, catalog by content hash, self-update |
| 7 | UI | ImGui app: Home, process picker, scanner + address list, trainer pages, settings, overlay |
| 8 | Packaging | per-user installer/launcher in `%LOCALAPPDATA%\Hexwright`, migration from the old Python build, release workflow |
| 9 | Live verification | Crusader Kings III: hook, Enable Console, Freeze Time, console cheats on the native build |

Risks and how they are handled
* **Scanner speed on multi-GB processes**: chunked reads, region filtering (private/writable by default), 8 MB buffers,
  progressive narrowing stores only `(address, previous value)`.
* **32-bit targets**: a 64-bit Hexwright reads/writes WOW64 processes normally; pointer width comes from the target.
* **Admin rights**: opening an elevated game fails with a clear "run Hexwright as administrator" prompt; the app never
  self-elevates silently.
* **Anti-cheat false positives**: the blocklist is strict by design (requirement), shown with the matching name.
