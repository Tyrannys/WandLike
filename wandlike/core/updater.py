"""Auto-update from GitHub Releases: check latest, download the setup exe, run it silently."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path
from typing import Callable

from .. import version as V

API = "https://api.github.com/repos/{repo}/releases/latest"
DETACHED = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP


def parse_version(v: str) -> tuple[int, ...]:
    v = v.strip().lstrip("vV")
    parts = []
    for p in v.split(".")[:4]:
        digits = "".join(ch for ch in p if ch.isdigit())
        parts.append(int(digits or 0))
    return tuple(parts)


def _get(url: str, timeout: float):
    req = urllib.request.Request(url, headers={"User-Agent": f"{V.APP_NAME}/{V.__version__}",
                                               "Accept": "application/vnd.github+json"})
    return urllib.request.urlopen(req, timeout=timeout)


def check(repo: str = V.UPDATE_REPO, current: str = V.__version__, timeout: float = 8,
          opener: Callable = _get) -> dict | None:
    """Return {version, url, sha256, notes, page} if a newer release exists, else None."""
    if not repo:
        return None
    with opener(API.format(repo=repo), timeout) as r:
        data = json.load(r)
    if data.get("draft") or data.get("prerelease"):
        return None
    tag = data.get("tag_name", "")
    if parse_version(tag) <= parse_version(current):
        return None
    for a in data.get("assets", []):
        if a.get("name") == V.SETUP_ASSET:
            url = a["browser_download_url"]
            if not url.startswith(f"https://github.com/{repo}/releases/download/"):
                return None
            digest = a.get("digest") or ""
            return {"version": tag.lstrip("vV"), "url": url,
                    "sha256": digest.split(":", 1)[1] if digest.startswith("sha256:") else None,
                    "notes": data.get("body", ""), "page": data.get("html_url", "")}
    return None


def download(info: dict, progress: Callable[[float], None] = lambda f: None,
             opener: Callable = _get) -> Path:
    dest_dir = Path(tempfile.mkdtemp(prefix="wandlike-update-"))
    dest = dest_dir / V.SETUP_ASSET
    h = hashlib.sha256()
    with opener(info["url"], 30) as r, open(dest, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while chunk := r.read(1 << 16):
            f.write(chunk)
            h.update(chunk)
            done += len(chunk)
            if total:
                progress(done / total)
    if info.get("sha256") and h.hexdigest().lower() != info["sha256"].lower():
        dest.unlink(missing_ok=True)
        raise RuntimeError("update download failed its SHA-256 check")
    return dest


def apply(setup: Path) -> None:
    """Launch the downloaded setup silently; the caller must exit right after."""
    if not getattr(sys, "frozen", False):
        raise RuntimeError("updates only apply to the installed build")
    subprocess.Popen([str(setup), "--silent", "--update", "--wait-pid", str(os.getpid())],
                     creationflags=DETACHED, close_fds=True)
