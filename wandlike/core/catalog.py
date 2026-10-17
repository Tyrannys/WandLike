"""Game catalog: plugin JSONs live in the GitHub repo and are pulled once a matching game is detected.

Repo layout (served from raw.githubusercontent.com):
    wandlike/games/_index.json   [{id, name, version, file, sha256, match, cheats, min_app}]  (tools/make_catalog.py)
    wandlike/games/<id>.json     the plugins

Only the index is fetched routinely (with ETag, so an unchanged index costs a 304). A plugin file is
downloaded only if a detected game matches it AND it is missing locally or the index lists a newer version.
Downloads must match the index's SHA-256 and pass the strict plugin loader before they are kept.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Callable

from .. import version as V
from . import library
from .plugin import MAX_PLUGIN_BYTES, catalog_games_dir, parse_game

RAW = "https://raw.githubusercontent.com/{repo}/{branch}/wandlike/games/{name}"
FILE_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}\.json$")
MAX_INDEX_BYTES = 512 * 1024


def normalize(data: bytes) -> bytes:
    """Hash over LF line endings so Windows checkouts and GitHub agree."""
    return data.replace(b"\r\n", b"\n")


def sha256(data: bytes) -> str:
    return hashlib.sha256(normalize(data)).hexdigest()


def http_get(url: str, etag: str | None = None, timeout: float = 10) -> tuple[int, str | None, bytes]:
    """(status, etag, body). 304 -> empty body. Other HTTP errors are returned as (code, None, b'')."""
    headers = {"User-Agent": f"{V.APP_NAME}/{V.__version__}"}
    if etag:
        headers["If-None-Match"] = etag
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=timeout) as r:
            return r.status, r.headers.get("ETag"), r.read(MAX_INDEX_BYTES + 1)
    except urllib.error.HTTPError as e:
        return e.code, None, b""


def _ver(s: str) -> tuple[int, ...]:
    return tuple(int(p) for p in s.split(".") if p.isdigit())


class Catalog:
    def __init__(self, repo: str = V.UPDATE_REPO, branch: str = V.CATALOG_BRANCH, directory: Path | None = None,
                 fetch: Callable = http_get, app_version: str = V.__version__):
        self.repo, self.branch, self.fetch, self.app_version = repo, branch, fetch, app_version
        self.dir = directory or catalog_games_dir().parent
        self.games_dir = self.dir / "games"
        self.games_dir.mkdir(parents=True, exist_ok=True)
        self.index: list[dict] = []
        self.status = "not synced yet"
        self.last_sync = 0.0
        self._load_cached_index()

    # ------------------------------------------------------------- helpers
    def _url(self, name: str) -> str:
        return RAW.format(repo=self.repo, branch=self.branch, name=name)

    def _load_cached_index(self) -> None:
        try:
            self.index = json.loads((self.dir / "index.json").read_text(encoding="utf-8")).get("games", [])
        except (OSError, ValueError):
            self.index = []

    def fetch_index(self) -> bool:
        """Refresh the index; returns True if it changed. Raises nothing: problems land in self.status."""
        etag_file = self.dir / "index.etag"
        etag = etag_file.read_text().strip() if etag_file.exists() and self.index else None
        status, new_etag, body = self.fetch(self._url("_index.json"), etag)
        self.last_sync = time.time()
        if status == 304:
            self.status = "catalog up to date"
            return False
        if status in (404, 403):
            self.status = "catalog unavailable (is the repository public?)"
            return False
        if status != 200 or len(body) > MAX_INDEX_BYTES:
            self.status = f"catalog fetch failed (HTTP {status})"
            return False
        try:
            data = json.loads(body.decode("utf-8"))
            games = data["games"]
            assert isinstance(games, list)
        except (ValueError, KeyError, AssertionError):
            self.status = "catalog index was not valid JSON"
            return False
        (self.dir / "index.json").write_bytes(body)
        if new_etag:
            etag_file.write_text(new_etag)
        self.index = games
        self.status = f"catalog: {len(games)} games listed"
        return True

    def local_versions(self) -> dict[str, int]:
        out = {}
        for p in self.games_dir.glob("*.json"):
            try:
                g = parse_game(json.loads(p.read_text(encoding="utf-8")))
                out[g.id] = g.version
            except Exception:
                continue
        return out

    # ---------------------------------------------------------------- sync
    def sync(self, detected: list[dict], have: dict[str, int]) -> list[str]:
        """Fetch the index, then download plugins for detected games that we lack or that are outdated.

        `have` maps game id -> version already loaded (bundled or catalog). Returns human-readable messages."""
        if not self.repo:
            self.status = "catalog disabled (no repository configured)"
            return []
        self.fetch_index()
        messages = []
        for entry in self.index:
            try:
                gid, ver, fname, digest = entry["id"], int(entry["version"]), entry["file"], entry["sha256"]
                match = entry["match"]
            except (KeyError, TypeError, ValueError):
                continue
            if not FILE_RE.match(fname) or not library.find(detected, match):
                continue
            if have.get(gid, 0) >= ver:
                continue
            if entry.get("min_app") and _ver(entry["min_app"]) > _ver(self.app_version):
                messages.append(f"{entry.get('name', gid)} needs a newer WandLike (v{entry['min_app']}+)")
                continue
            msg = self._download(entry, gid, ver, fname, digest)
            if msg:
                messages.append(msg)
                have[gid] = ver
        return messages

    def _download(self, entry: dict, gid: str, ver: int, fname: str, digest: str) -> str | None:
        status, _, body = self.fetch(self._url(fname), None)
        if status != 200 or len(body) > MAX_PLUGIN_BYTES:
            return f"! could not download {fname} (HTTP {status})"
        if sha256(body) != digest.lower():
            return f"! {fname} failed its SHA-256 check and was discarded"
        try:
            g = parse_game(json.loads(body.decode("utf-8")))
        except Exception as e:
            return f"! {fname} is not a valid plugin: {e}"
        if g.id != gid or g.version != ver:
            return f"! {fname} does not match its index entry"
        tmp = self.games_dir / (fname + ".tmp")
        tmp.write_bytes(body)
        os.replace(tmp, self.games_dir / fname)
        return f"Added support for {g.name} (plugin v{g.version})"

    # ------------------------------------------------------------- queries
    def entries_for_ui(self, loaded_ids: set[str], detected: list[dict]) -> list[dict]:
        """Index entries that have no plugin loaded yet (shown on Explore)."""
        out = []
        for e in self.index:
            if not isinstance(e, dict) or e.get("id") in loaded_ids or "id" not in e:
                continue
            out.append({"id": e["id"], "name": e.get("name", e["id"]), "cheat_count": int(e.get("cheats", 0)),
                        "installed": bool(library.find(detected, e.get("match", {})))})
        return out
