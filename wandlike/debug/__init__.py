"""Debug-only helpers: verbose logging, API call tracing and test hooks.

This whole package is left out of release builds (build.ps1 passes --exclude-module wandlike.debug and
the release exe never ships web/debug.js), so nothing here can run in a shipped app. It switches on when
WANDLIKE_DEBUG=1 (the debug exe sets it) or when started from source with --debug.
"""
from __future__ import annotations

import faulthandler
import functools
import json
import logging
import os
import platform
import sys
import threading
import time
from pathlib import Path

log = logging.getLogger("wandlike.debug")
_forced_running: dict[str, bool] = {}   # exe name -> pretend running state
LOG_FILE = Path(os.environ.get("APPDATA", Path.home())) / "WandLike" / "debug.log"
_QUIET = {"state", "debug_log_tail", "debug_info"}  # polled calls: don't flood the log


def enabled() -> bool:
    return os.environ.get("WANDLIKE_DEBUG") == "1" or "--debug" in sys.argv


def _configure_logging() -> None:
    LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    fmt = logging.Formatter("%(asctime)s.%(msecs)03d %(levelname)-7s %(name)s: %(message)s", "%H:%M:%S")
    root = logging.getLogger()
    root.setLevel(logging.DEBUG)
    handlers = [logging.FileHandler(LOG_FILE, "w", encoding="utf-8")]
    if sys.stdout:  # the debug exe has a console; a windowed run does not
        handlers.append(logging.StreamHandler(sys.stdout))
    for h in handlers:
        h.setFormatter(fmt)
        root.addHandler(h)
    logging.getLogger("pywebview").setLevel(logging.DEBUG)


def _install_excepthooks() -> None:
    faulthandler.enable()

    def hook(exc_type, exc, tb):
        log.error("uncaught exception", exc_info=(exc_type, exc, tb))
    sys.excepthook = hook
    threading.excepthook = lambda a: log.error("thread %s crashed", a.thread.name if a.thread else "?",
                                              exc_info=(a.exc_type, a.exc_value, a.exc_traceback))


def _trace(api, name: str, fn):
    @functools.wraps(fn)
    def wrapper(*args, **kw):
        t0 = time.perf_counter()
        try:
            out = fn(*args, **kw)
        except Exception:
            log.exception("api.%s%r raised", name, args)
            raise
        if name not in _QUIET:
            shown = repr(out)
            log.debug("api.%s%r -> %s  (%.1f ms)", name, args, shown[:160], (time.perf_counter() - t0) * 1000)
        return out
    return wrapper


def _patch_find_pids() -> None:
    """Let the UI pretend a game is running (debug_fake_running) without launching it."""
    from ..core import winapi
    real = winapi.find_pids

    def fake(exe_name: str):
        forced = _forced_running.get(exe_name.lower())
        return real(exe_name) if forced is None else ([4242] if forced else [])
    winapi.find_pids = fake


def setup(api) -> None:
    """Called once from app.main.run() right after the Api is created."""
    _configure_logging()
    _install_excepthooks()
    _patch_find_pids()
    log.info("DEBUG MODE  python %s  %s  frozen=%s  pid=%d", platform.python_version(), platform.platform(),
             bool(getattr(sys, "frozen", False)), os.getpid())
    log.info("log file: %s", LOG_FILE)

    orig_say = api._say
    api._say = lambda msg: (log.info("engine: %s", msg), orig_say(msg))[1]  # mirror Activity log into the console

    api._debug = True
    for name in [n for n in dir(api) if not n.startswith("_") and callable(getattr(api, n))]:
        setattr(api, name, _trace(api, name, getattr(api, name)))

    def debug_info():
        from ..core import steam, winapi
        from ..version import UPDATE_REPO, __version__
        return {"version": __version__, "repo": UPDATE_REPO, "python": platform.python_version(),
                "frozen": bool(getattr(sys, "frozen", False)), "pid": os.getpid(), "log_file": str(LOG_FILE),
                "settings_file": str(api._settings.path), "steam_root": str(steam.steam_root()),
                "games": {gid: {"exe": g.exe, "pids": winapi.find_pids(g.exe),
                                "hwnd": winapi.find_window(g.window_title) if winapi.find_pids(g.exe) else None,
                                "install_dir": str(steam.install_dir(g.steam_appid) or "") if g.steam_appid else ""}
                          for gid, g in api._games.items()},
                "hotkey_errors": dict(api._hk_errors), "forced_running": dict(_forced_running)}

    def debug_send_console(gid: str, line: str):
        """Type one raw line into the game's console (dry-run logs it instead)."""
        log.info("raw console line for %s: %s", gid, line)
        api._engine(gid)._submit(lambda: api._engine(gid).driver.send([line]))

    def debug_fake_update(version: str = "9.9.9"):
        api._update = {"version": version, "url": "https://invalid.example/WandLike-Setup.exe", "sha256": None,
                       "notes": "Fake update injected from the debug page.", "page": ""}

    def debug_fake_running(gid: str, on):
        exe = api._games[gid].exe.lower()
        if on is None:
            _forced_running.pop(exe, None)
        else:
            _forced_running[exe] = bool(on)

    def debug_dump_settings():
        return api._settings.data

    def debug_log_tail(n: int = 80):
        try:
            return LOG_FILE.read_text(encoding="utf-8", errors="replace").splitlines()[-int(n):]
        except OSError:
            return []

    def debug_throw():
        raise RuntimeError("debug_throw: intentional test exception")

    for fn in (debug_info, debug_send_console, debug_fake_update, debug_fake_running, debug_dump_settings,
               debug_log_tail, debug_throw):
        setattr(api, fn.__name__, _trace(api, fn.__name__, fn))
    json.dumps(debug_info())  # fail fast if the report isn't serialisable
