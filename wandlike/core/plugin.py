"""Game plugins: declarative JSON describing a game and its cheats."""
from __future__ import annotations

import json
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
    steam_appid: int | None
    launch_args: str
    console: dict
    cheats: list[Cheat]
    notes: str = ""

    def cheat(self, cid: str) -> Cheat:
        return next(c for c in self.cheats if c.id == cid)


def load_game(path: Path) -> Game:
    raw = json.loads(path.read_text(encoding="utf-8"))
    cheats = [Cheat(**c) for c in raw.pop("cheats")]
    return Game(cheats=cheats, **raw)


def user_games_dir() -> Path:
    import os
    return Path(os.environ.get("APPDATA", Path.home())) / "WandLike" / "games"


def load_all(directory: Path = GAMES_DIR, include_user: bool = True) -> dict[str, Game]:
    """Bundled plugins plus any the user dropped into %APPDATA%/WandLike/games."""
    games = {}
    dirs = [directory] + ([user_games_dir()] if include_user else [])
    for d in dirs:
        for p in sorted(d.glob("*.json")):
            try:
                g = load_game(p)
            except Exception as e:  # a bad user plugin must not break the app
                print(f"skipping plugin {p}: {e}")
                continue
            games[g.id] = g
    return games
