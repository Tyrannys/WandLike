"""Tools for people writing game plugins: validate (clear errors), preview (what gets typed), scaffold."""
from __future__ import annotations

import dataclasses
import difflib
import json
import re
import sys
from pathlib import Path

from .plugin import GAMES_DIR, ID_RE, MAX_PLUGIN_BYTES, Cheat, Game, parse_game, user_games_dir

VERIFIED = {"wiki", "inferred", "tested", "parsed", "unsupported"}
KINDS = {"number", "toggle", "button"}
GAME_REQUIRED = ["id", "name", "exe", "window_title", "launch_args", "console", "cheats"]
CHEAT_REQUIRED = ["id", "label", "kind"]
SKIP_EXE = re.compile(r"unins|redist|crash|setup|install|dxweb|vc_|launcher|helper|updater|eac|easyanticheat|bootstrap", re.I)


def _fields(cls) -> set[str]:
    return {f.name for f in dataclasses.fields(cls)}


def _unknown(keys, allowed: set[str], where: str, errors: list[str]) -> None:
    for k in keys:
        if k not in allowed:
            hint = difflib.get_close_matches(k, sorted(allowed), n=1)
            errors.append(f"{where}: unknown key \"{k}\"" + (f" (did you mean \"{hint[0]}\"?)" if hint else ""))


def validate_text(text: str) -> tuple[list[str], list[str]]:
    """(errors, warnings) for a plugin's JSON text. Errors would stop the plugin loading."""
    errors: list[str] = []
    warns: list[str] = []
    if len(text.encode("utf-8")) > MAX_PLUGIN_BYTES:
        return [f"file is larger than {MAX_PLUGIN_BYTES // 1024} KB"], warns
    try:
        raw = json.loads(text)
    except json.JSONDecodeError as e:
        return [f"not valid JSON: {e.msg} at line {e.lineno}, column {e.colno}"], warns
    if not isinstance(raw, dict):
        return ["the file must contain a JSON object"], warns

    _unknown([k for k in raw if k != "cheats"], _fields(Game) - {"source", "cheats"} | {"cheats"}, "plugin", errors)
    for k in GAME_REQUIRED:
        if k not in raw:
            errors.append(f"plugin: missing required key \"{k}\"")
    if isinstance(raw.get("id"), str) and not ID_RE.match(raw["id"]):
        errors.append(f"plugin: id \"{raw['id']}\" must be lowercase letters, digits, - or _ (max 64)")
    if isinstance(raw.get("exe"), str) and not raw["exe"].lower().endswith(".exe"):
        warns.append("plugin: exe should be the process name, e.g. \"game.exe\"")
    if not raw.get("match") and not raw.get("steam_appid"):
        warns.append("plugin: no \"match\" (e.g. {\"steam_appid\": 123}); the game can't be recognised in the library")
    if not isinstance(raw.get("version", 1), int) or raw.get("version", 1) < 1:
        errors.append("plugin: version must be a whole number >= 1")
    cheats = raw.get("cheats")
    if not isinstance(cheats, list) or not cheats:
        errors.append("plugin: \"cheats\" must be a non-empty list")
        cheats = []
    symbols = (raw.get("hook") or {}).get("symbols", {}) if isinstance(raw.get("hook"), dict) else {}

    seen: set[str] = set()
    for i, c in enumerate(cheats):
        where = f"cheat #{i + 1}" + (f" ({c.get('id')})" if isinstance(c, dict) and c.get("id") else "")
        if not isinstance(c, dict):
            errors.append(f"{where}: must be an object")
            continue
        _unknown(c, _fields(Cheat), where, errors)
        for k in CHEAT_REQUIRED:
            if k not in c:
                errors.append(f"{where}: missing required key \"{k}\"")
        if c.get("id") in seen:
            errors.append(f"{where}: duplicate id")
        seen.add(c.get("id"))
        kind = c.get("kind")
        if kind not in KINDS:
            errors.append(f"{where}: kind must be one of {sorted(KINDS)}")
            continue
        for key in ("set", "add"):
            if key in c:
                _check_template(c[key], where, key, errors)
        for key in ("on", "off", "run"):
            for line in c.get(key, []) or []:
                if "{{" in line or "}}" in line:
                    warns.append(f"{where}: \"{key}\" lines are sent as written; remove the doubled braces in {line!r}")
        mem = c.get("memory")
        if kind == "number" and not (c.get("set") or c.get("add") or mem):
            errors.append(f"{where}: a number cheat needs \"set\" and/or \"add\" (or \"memory\")")
        if kind == "toggle" and not (c.get("on") or mem or c.get("verified") == "unsupported"):
            errors.append(f"{where}: a toggle needs \"on\" (and usually \"off\") commands, or \"memory\"")
        if kind == "button" and not (c.get("run") or c.get("launch")):
            errors.append(f"{where}: a button needs \"run\" commands or \"launch\": true")
        if mem and "symbol" in mem and mem["symbol"] not in symbols:
            errors.append(f"{where}: memory symbol \"{mem['symbol']}\" is not defined under hook.symbols")
        if mem and kind == "toggle" and "on" not in mem:
            errors.append(f"{where}: a memory toggle needs \"on\" (and \"off\") values")
        if c.get("verified", "inferred") not in VERIFIED:
            errors.append(f"{where}: verified must be one of {sorted(VERIFIED)}")
        if c.get("verified") == "unsupported" and not c.get("unsupported"):
            warns.append(f"{where}: explain why in \"unsupported\" so users know what's missing")
        if kind == "number":
            lo, hi, d = c.get("min", -1e9), c.get("max", 1e9), c.get("default", 0)
            if not lo <= d <= hi:
                errors.append(f"{where}: default {d} is outside min/max")
        if not c.get("info"):
            warns.append(f"{where}: add an \"info\" sentence; it becomes the tooltip")

    if not errors:   # final authority: the real loader
        try:
            parse_game(raw)
        except Exception as e:
            errors.append(f"loader: {e}")
    return errors, warns


def _check_template(tpl, where: str, key: str, errors: list[str]) -> None:
    if not isinstance(tpl, str):
        errors.append(f"{where}: \"{key}\" must be a string")
        return
    if "{value}" not in tpl:
        errors.append(f"{where}: \"{key}\" must contain {{value}} where the number goes: {tpl!r}")
    try:
        tpl.format(value=1)
    except (KeyError, IndexError, ValueError) as e:
        errors.append(f"{where}: \"{key}\" has a stray brace ({e}); write literal {{ }} as {{{{ }}}} in set/add templates: {tpl!r}")


def validate_file(path: str | Path) -> tuple[list[str], list[str]]:
    try:
        return validate_text(Path(path).read_text(encoding="utf-8"))
    except OSError as e:
        return [f"cannot read file: {e}"], []


def preview(game: Game) -> list[str]:
    """What each cheat would do, without a game running."""
    from .engine import console_lines
    out = [f"{game.name}  (plugin v{game.version}, match {game.match_spec}, exe {game.exe})", ""]
    for c in game.cheats:
        tag = f"[{c.verified}]"
        if c.memory:
            out.append(f"{c.label:28} {tag:13} memory {json.dumps(c.memory)}")
        elif c.kind == "number":
            for mode in ("set", "add"):
                if (mode == "set" and c.set) or (mode == "add" and c.add):
                    out.append(f"{c.label:28} {tag:13} {mode:3} {c.default:g} -> {console_lines(c, 'number', c.default, mode)[0]}")
        elif c.kind == "toggle":
            out.append(f"{c.label:28} {tag:13} on  -> {console_lines(c, 'on') or '(nothing)'}")
            out.append(f"{'':28} {'':13} off -> {console_lines(c, 'off') or '(nothing)'}"
                       + (f"   repeats every {c.interval:g}s" if c.interval else ""))
        else:
            out.append(f"{c.label:28} {tag:13} run -> {console_lines(c, 'press') or ('launch game' if c.launch else '(nothing)')}")
    return out


# --------------------------------------------------------------- scaffolding
def slug(name: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return (s or "game")[:48]


def guess_exe(entry: dict) -> str:
    """Best guess at the game's process name from its install folder (Epic/GOG record it directly)."""
    if entry.get("exe"):
        return Path(entry["exe"]).name
    folder = Path(entry["install_dir"])
    exes = [p for pat in ("*.exe", "bin*/*.exe", "binaries/*.exe", "*/*.exe") for p in folder.glob(pat) if not SKIP_EXE.search(p.name)]
    exes = sorted(set(exes), key=lambda p: p.stat().st_size, reverse=True)
    return exes[0].name if exes else "game.exe"


def scaffold(entry: dict) -> dict:
    """A starter plugin for a detected library entry (see library.Library)."""
    match = ({"steam_appid": int(entry["id"])} if entry["store"] == "steam"
             else {entry["store"]: entry["id"]})
    exe = guess_exe(entry)
    plugin = {
        "id": slug(entry["name"]), "name": entry["name"], "version": 1, "match": match,
        "exe": exe, "window_title": entry["name"], "launch_args": "",
        "console": {"toggle_scancode": 41, "open_delay": 0.25, "line_delay": 0.15, "close_after": True},
        "notes": "Describe how this plugin drives the game (console key, launch option needed, limitations).",
        "cheats": [
            {"id": "gold", "label": "Set Gold", "kind": "number", "info": "Replace with a real console command.",
             "set": "setgold {value}", "default": 1000, "step": 1000, "verified": "inferred"},
            {"id": "god", "label": "God Mode", "kind": "toggle", "info": "Replace with real commands.",
             "on": ["god"], "off": ["god"], "verified": "inferred"},
        ],
    }
    if entry["store"] == "steam":
        plugin["steam_appid"] = int(entry["id"])
    return plugin


def default_output_dir() -> Path:
    return user_games_dir() if getattr(sys, "frozen", False) else GAMES_DIR
