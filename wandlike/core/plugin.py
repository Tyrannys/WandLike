"""Game plugins: declarative JSON describing a game and its cheats."""
from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, field
from pathlib import Path

GAMES_DIR = Path(__file__).resolve().parent.parent / "games"


@dataclass
class Cheat:
    id: str
    label: str
    kind: str                       # number | toggle | button
    section: str = "Player"
    info: str = ""
    # --- console backend
    set: str | None = None          # template for "set to value"
    add: str | None = None          # template for "add value"
    on: list[str] = field(default_factory=list)
    off: list[str] = field(default_factory=list)
    run: list[str] = field(default_factory=list)   # button commands
    interval: float | None = None   # toggle: repeat `on` every N seconds
    options: list[dict] = field(default_factory=list)  # [{id,label,type:"number",min,max,default}] ("interval" is special)
    # --- memory backend
    memory: dict | None = None
    # --- number range
    default: float = 0
    min: float = -1_000_000_000
    max: float = 1_000_000_000
    step: float = 100
    # --- metadata
    requires: str = ""              # DLC / precondition
    verified: str = "inferred"      # wiki | inferred | unsupported
    unsupported: str = ""           # reason if the cheat can't be done by this plugin
    launch: bool = False            # button: launch the game in console mode

    @property
    def available(self) -> bool:
        return self.verified != "unsupported"


@dataclass
class Game:
    id: str
    name: str
    exe: str
    window_title: str
    launch_args: str
    console: dict
    cheats: list[Cheat]
    notes: str = ""
    steam_appid: int | None = None
    hook: dict = field(default_factory=dict)   # named memory symbols found by byte pattern (see core/hook.py)
    # how the game is recognised in the user's libraries, e.g. {"steam_appid": 1158310} / {"epic": "<AppName>"} / {"gog": "<id>"}
    match: dict = field(default_factory=dict)
    version: int = 1                # bump when the plugin changes; newer versions replace older ones
    min_app: str = ""               # oldest WandLike that understands this plugin
    source: str = ""                # bundled | catalog | user (set by the loader, not stored in the file)

    def cheat(self, cid: str) -> Cheat:
        return next(c for c in self.cheats if c.id == cid)

    @property
    def match_spec(self) -> dict:
        return self.match or ({"steam_appid": self.steam_appid} if self.steam_appid else {})


MAX_PLUGIN_BYTES = 256 * 1024
ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}$")
MATCH_KEYS = {"steam_appid", "epic", "gog"}


def parse_game(raw: dict) -> Game:
    """Strictly build a Game from untrusted JSON: unknown keys, bad ids or oversize plugins raise."""
    raw = dict(raw)
    if not isinstance(raw.get("cheats"), list) or len(raw["cheats"]) > 300:
        raise ValueError("cheats must be a list of at most 300 entries")
    cheats = [Cheat(**c) for c in raw.pop("cheats")]
    raw.pop("source", None)
    g = Game(cheats=cheats, **raw)
    if not ID_RE.match(g.id):
        raise ValueError(f"bad game id {g.id!r}")
    if not set(g.match) <= MATCH_KEYS:
        raise ValueError(f"unknown match keys {set(g.match) - MATCH_KEYS}")
    if len({c.id for c in g.cheats}) != len(g.cheats):
        raise ValueError("duplicate cheat ids")
    _validate_hook(g)
    return g


def _validate_hook(g: Game) -> None:
    from .hook import SYMBOL_KEYS
    from .memory import TYPES, compile_pattern
    extra = set(g.hook) - {"symbols"}
    if extra:
        raise ValueError(f"unknown hook keys {sorted(extra)}")
    symbols = g.hook.get("symbols", {})
    for name, spec in symbols.items():
        if not ID_RE.match(name.lower().replace("_", "-")):
            raise ValueError(f"bad symbol name {name!r}")
        if set(spec) - SYMBOL_KEYS:
            raise ValueError(f"symbol {name}: unknown keys {sorted(set(spec) - SYMBOL_KEYS)}")
        if spec.get("type", "int32") not in TYPES:
            raise ValueError(f"symbol {name}: type must be one of {sorted(TYPES)}")
        if "aob" in spec:
            compile_pattern(spec["aob"])
        elif "rva" not in spec:
            raise ValueError(f"symbol {name}: needs an \"aob\" pattern (or an \"rva\" with \"exe_size\")")
        if "rip" in spec and set(spec["rip"]) != {"disp_at", "insn_len"}:
            raise ValueError(f"symbol {name}: rip needs exactly disp_at and insn_len")
    for c in g.cheats:
        sym = (c.memory or {}).get("symbol")
        if sym is not None and sym not in symbols:
            raise ValueError(f"cheat {c.id}: unknown hook symbol {sym!r}")


def load_game(path: Path) -> Game:
    if path.stat().st_size > MAX_PLUGIN_BYTES:
        raise ValueError("plugin file too large")
    return parse_game(json.loads(path.read_text(encoding="utf-8")))


def user_games_dir() -> Path:
    return Path(os.environ.get("APPDATA", Path.home())) / "WandLike" / "games"


def catalog_games_dir() -> Path:
    """Plugins downloaded from the GitHub catalog."""
    return Path(os.environ.get("APPDATA", Path.home())) / "WandLike" / "catalog" / "games"


def _app_version() -> tuple[int, ...]:
    from .. import version as V
    return tuple(int(p) for p in V.__version__.split(".") if p.isdigit())


def load_all(directory: Path = GAMES_DIR, include_user: bool = True, catalog_dir: Path | None = None) -> dict[str, Game]:
    """Bundled + catalog + user plugins. The highest `version` wins between bundled and catalog; a user plugin always wins."""
    games: dict[str, Game] = {}
    cat = catalog_dir if catalog_dir is not None else catalog_games_dir()
    sources = [(directory, "bundled"), (cat, "catalog")] + ([(user_games_dir(), "user")] if include_user else [])
    for d, label in sources:
        if not d.exists():
            continue
        for p in sorted(d.glob("*.json")):
            if p.name.startswith("_"):      # _index.json etc.
                continue
            try:
                g = load_game(p)
                if g.min_app and tuple(int(x) for x in g.min_app.split(".")) > _app_version():
                    print(f"skipping plugin {p.name}: needs WandLike {g.min_app}+")
                    continue
            except Exception as e:  # a bad plugin must not break the app
                print(f"skipping plugin {p}: {e}")
                continue
            g.source = label
            old = games.get(g.id)
            if old is None or label == "user" or g.version > old.version:
                games[g.id] = g
    return games
