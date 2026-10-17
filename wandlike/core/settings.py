"""Persistent per-user settings (pins, last-used values, console key override)."""
from __future__ import annotations

import json
import os
from pathlib import Path


def _path() -> Path:
    base = Path(os.environ.get("APPDATA", Path.home())) / "WandLike"
    base.mkdir(parents=True, exist_ok=True)
    return base / "settings.json"


class Settings:
    def __init__(self, path: Path | None = None):
        self.path = path or _path()
        try:
            self.data = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            self.data = {}

    def get(self, key, default=None):
        return self.data.get(key, default)

    def set(self, key, value) -> None:
        self.data[key] = value
        try:
            self.path.write_text(json.dumps(self.data, indent=2), encoding="utf-8")
        except OSError:
            pass
