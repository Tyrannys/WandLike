"""Find installed games across Steam, Epic and GOG, caching so unchanged files are never re-read.

Cache (%APPDATA%/WandLike/library_cache.json) keeps
  files: path -> {mtime_ns, size, data}   parsed manifests, reused while (mtime, size) are unchanged
  dirs:  path -> {mtime_ns, names}        directory listings, reused while the directory's mtime is unchanged
A normal scan therefore costs a handful of stat() calls. `scan(force=True)` ignores the cache.
"""
from __future__ import annotations

import json
import os
import re
import time
from collections import Counter
from pathlib import Path
from typing import Callable

from . import steam

CACHE_VERSION = 1


def default_cache_path() -> Path:
    base = Path(os.environ.get("APPDATA", Path.home())) / "WandLike"
    base.mkdir(parents=True, exist_ok=True)
    return base / "library_cache.json"


def default_epic_dir() -> Path:
    return Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData")) / "Epic" / "EpicGamesLauncher" / "Data" / "Manifests"


class Library:
    def __init__(self, cache_path: Path | None = None, steam_root: Callable[[], Path | None] = steam.steam_root,
                 epic_dir: Path | None = None, scan_gog: bool = True):
        self.cache_path = cache_path or default_cache_path()
        self._steam_root = steam_root
        self.epic_dir = epic_dir if epic_dir is not None else default_epic_dir()
        self.scan_gog = scan_gog
        self.stats: Counter = Counter()
        self.games: list[dict] = []
        self._cache = self._load()
        self._seen_files: set[str] = set()
        self._seen_dirs: set[str] = set()

    # ------------------------------------------------------------ cache io
    def _load(self) -> dict:
        try:
            c = json.loads(self.cache_path.read_text(encoding="utf-8"))
            if c.get("v") == CACHE_VERSION:
                return c
        except (OSError, ValueError):
            pass
        return {"v": CACHE_VERSION, "files": {}, "dirs": {}, "games": [], "scanned_at": 0}

    def _save(self) -> None:
        text = json.dumps(self._cache, separators=(",", ":"))
        try:
            if self.cache_path.exists() and self.cache_path.read_text(encoding="utf-8") == text:
                return
            tmp = self.cache_path.with_suffix(".tmp")
            tmp.write_text(text, encoding="utf-8")
            os.replace(tmp, self.cache_path)
        except OSError:
            pass

    @property
    def scanned_at(self) -> float:
        return self._cache.get("scanned_at", 0)

    # ------------------------------------------------------ cached primitives
    def _parsed(self, path: Path, parse: Callable[[str], dict | None]) -> dict | None:
        """Parse a manifest, or reuse the cached result if the file is unchanged."""
        key = str(path)
        self._seen_files.add(key)
        try:
            st = path.stat()
        except OSError:
            return None
        hit = self._cache["files"].get(key)
        if hit and hit["mtime_ns"] == st.st_mtime_ns and hit["size"] == st.st_size:
            self.stats["files_cached"] += 1
            return hit["data"]
        try:
            data = parse(path.read_text(encoding="utf-8", errors="ignore"))
        except OSError:
            return None
        self.stats["files_parsed"] += 1
        self._cache["files"][key] = {"mtime_ns": st.st_mtime_ns, "size": st.st_size, "data": data}
        return data

    def _listing(self, directory: Path, pattern: str) -> list[str]:
        """File names matching pattern; re-listed only if the directory's mtime changed."""
        key = f"{directory}|{pattern}"
        self._seen_dirs.add(key)
        try:
            st = directory.stat()
        except OSError:
            return []
        hit = self._cache["dirs"].get(key)
        if hit and hit["mtime_ns"] == st.st_mtime_ns:
            self.stats["dirs_cached"] += 1
            return hit["names"]
        names = sorted(p.name for p in directory.glob(pattern))
        self.stats["dirs_listed"] += 1
        self._cache["dirs"][key] = {"mtime_ns": st.st_mtime_ns, "names": names}
        return names

    # ---------------------------------------------------------------- scan
    def scan(self, force: bool = False) -> list[dict]:
        if force:
            self._cache = {"v": CACHE_VERSION, "files": {}, "dirs": {}, "games": [], "scanned_at": 0}
        self.stats = Counter()
        self._seen_files, self._seen_dirs = set(), set()
        games = self._scan_steam() + self._scan_epic() + (self._scan_gog() if self.scan_gog else [])
        # drop cache entries for files/dirs that no longer exist in any source
        self._cache["files"] = {k: v for k, v in self._cache["files"].items() if k in self._seen_files}
        self._cache["dirs"] = {k: v for k, v in self._cache["dirs"].items() if k in self._seen_dirs}
        games.sort(key=lambda g: (g["store"], g["name"].lower()))
        self._cache["games"], self._cache["scanned_at"] = games, time.time()
        self.games = games
        self._save()
        return games

    # --------------------------------------------------------------- steam
    @staticmethod
    def _parse_vdf_paths(text: str) -> dict:
        return {"paths": [m.group(1).replace("\\\\", "\\") for m in re.finditer(r'"path"\s+"([^"]+)"', text)]}

    @staticmethod
    def _parse_acf(text: str) -> dict | None:
        def f(k):
            m = re.search(rf'"{k}"\s+"([^"]*)"', text)
            return m.group(1) if m else None
        appid, name, folder = f("appid"), f("name"), f("installdir")
        return {"appid": appid, "name": name, "installdir": folder} if appid and folder else None

    def _scan_steam(self) -> list[dict]:
        root = self._steam_root()
        if not root:
            return []
        libs = [Path(root) / "steamapps"]
        vdf = self._parsed(Path(root) / "steamapps" / "libraryfolders.vdf", self._parse_vdf_paths)
        libs += [Path(p) / "steamapps" for p in (vdf or {}).get("paths", [])]
        out, seen_libs = [], set()
        for lib in libs:
            if str(lib).lower() in seen_libs or not lib.exists():
                continue
            seen_libs.add(str(lib).lower())
            for name in self._listing(lib, "appmanifest_*.acf"):
                d = self._parsed(lib / name, self._parse_acf)
                if not d:
                    continue
                folder = lib / "common" / d["installdir"]
                if folder.is_dir():
                    out.append({"store": "steam", "id": d["appid"], "name": d["name"] or d["installdir"],
                                "install_dir": str(folder)})
        return out

    # ---------------------------------------------------------------- epic
    @staticmethod
    def _parse_item(text: str) -> dict | None:
        try:
            j = json.loads(text)
        except ValueError:
            return None
        if not j.get("AppName") or not j.get("InstallLocation"):
            return None
        return {"app": j["AppName"], "name": j.get("DisplayName") or j["AppName"], "loc": j["InstallLocation"],
                "exe": j.get("LaunchExecutable", "")}

    def _scan_epic(self) -> list[dict]:
        out = []
        for name in self._listing(self.epic_dir, "*.item"):
            d = self._parsed(self.epic_dir / name, self._parse_item)
            if d and Path(d["loc"]).is_dir():
                out.append({"store": "epic", "id": d["app"], "name": d["name"], "install_dir": d["loc"],
                            "exe": d["exe"]})
        return out

    # ----------------------------------------------------------------- gog
    def _scan_gog(self) -> list[dict]:
        """GOG keeps installs in the registry: no files to cache, and reading it is cheap."""
        out = []
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\GOG.com\Games") as root:
                for i in range(winreg.QueryInfoKey(root)[0]):
                    with winreg.OpenKey(root, winreg.EnumKey(root, i)) as k:
                        def v(n):
                            try:
                                return winreg.QueryValueEx(k, n)[0]
                            except OSError:
                                return ""
                        path = v("path")
                        if v("gameID") and path and Path(path).is_dir():
                            out.append({"store": "gog", "id": str(v("gameID")), "name": v("gameName") or str(v("gameID")),
                                        "install_dir": path, "exe": v("exe")})
        except OSError:
            pass
        return out


# ------------------------------------------------------------------ matching
def find(games: list[dict], match: dict) -> dict | None:
    """The detected game that satisfies a plugin's `match` spec, if any."""
    for g in games:
        if "steam_appid" in match and g["store"] == "steam" and g["id"] == str(match["steam_appid"]):
            return g
        if "epic" in match and g["store"] == "epic" and g["id"] == match["epic"]:
            return g
        if "gog" in match and g["store"] == "gog" and g["id"] == str(match["gog"]):
            return g
    return None
