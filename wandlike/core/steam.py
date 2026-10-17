"""Steam library detection and local artwork (read from Steam's own cache)."""
from __future__ import annotations

import base64
import re
from pathlib import Path

_ART = {"cover": ("library_600x900.jpg",), "hero": ("library_hero.jpg",),
        "logo": ("logo.png",), "header": ("library_header.jpg", "header.jpg")}


def steam_root() -> Path | None:
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
            return Path(winreg.QueryValueEx(k, "SteamPath")[0])
    except OSError:
        return None


def library_dirs(root: Path | None = None) -> list[Path]:
    root = root or steam_root()
    if not root:
        return []
    dirs = [root / "steamapps"]
    vdf = root / "steamapps" / "libraryfolders.vdf"
    try:
        text = vdf.read_text(encoding="utf-8", errors="ignore")
        for m in re.finditer(r'"path"\s+"([^"]+)"', text):
            dirs.append(Path(m.group(1).replace("\\\\", "\\")) / "steamapps")
    except OSError:
        pass
    seen, out = set(), []
    for d in dirs:
        key = str(d).lower()
        if key not in seen and d.exists():
            seen.add(key)
            out.append(d)
    return out


def install_dir(appid: int) -> Path | None:
    """Folder the game is installed in, or None when not installed."""
    for lib in library_dirs():
        acf = lib / f"appmanifest_{appid}.acf"
        if acf.exists():
            m = re.search(r'"installdir"\s+"([^"]+)"', acf.read_text(encoding="utf-8", errors="ignore"))
            if m and (lib / "common" / m.group(1)).exists():
                return lib / "common" / m.group(1)
    return None


def art_path(appid: int, kind: str) -> Path | None:
    root = steam_root()
    if not root:
        return None
    base = root / "appcache" / "librarycache" / str(appid)
    for name in _ART.get(kind, ()):
        for p in ([base / name] if (base / name).exists() else list(base.glob(f"*/{name}"))):
            return p
    return None


_cache: dict[tuple[int, str], str] = {}


def art_data_url(appid: int, kind: str) -> str:
    key = (appid, kind)
    if key not in _cache:
        p = art_path(appid, kind)
        if not p:
            _cache[key] = ""
        else:
            mime = "image/png" if p.suffix == ".png" else "image/jpeg"
            _cache[key] = f"data:{mime};base64," + base64.b64encode(p.read_bytes()).decode()
    return _cache[key]
