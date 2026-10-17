"""Creates the main window + in-game overlay window (pywebview / WebView2)."""
from __future__ import annotations

import os
import sys
from pathlib import Path

import webview

from .api import Api

BG = "#0b0c10"


def web_dir() -> Path:
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2]))
    return base / "wandlike" / "web"


def run(dry_run: bool = False) -> None:
    api = Api(dry_run)
    debug_on = False
    try:  # the package is stripped from release builds, so this import simply fails there
        from .. import debug as dbg
        if dbg.enabled():
            dbg.setup(api)
            debug_on = True
    except ImportError:
        pass
    web = web_dir()
    title = "WandLike [DEBUG]" if debug_on else "WandLike"
    main = webview.create_window(title, str(web / "index.html"), js_api=api, width=1180, height=780,
                                 min_size=(920, 620), frameless=True, background_color=BG)
    overlay = webview.create_window("WandLike Overlay", str(web / "overlay.html"), js_api=api, width=320,
                                    height=440, frameless=True, on_top=True, hidden=True, resizable=False,
                                    x=40, y=80, background_color=BG)
    api.bind_windows(main, overlay)
    main.events.closed += lambda: os._exit(0)
    storage = Path(os.environ.get("APPDATA", Path.home())) / "WandLike" / "webview"
    storage.mkdir(parents=True, exist_ok=True)
    webview.start(storage_path=str(storage), debug=debug_on)  # debug=True enables right-click > Inspect
