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
    web = web_dir()
    main = webview.create_window("WandLike", str(web / "index.html"), js_api=api, width=1180, height=780,
                                 min_size=(920, 620), frameless=True, background_color=BG)
    overlay = webview.create_window("WandLike Overlay", str(web / "overlay.html"), js_api=api, width=320,
                                    height=440, frameless=True, on_top=True, hidden=True, resizable=False,
                                    x=40, y=80, background_color=BG)
    api.bind_windows(main, overlay)
    main.events.closed += lambda: os._exit(0)
    storage = Path(os.environ.get("APPDATA", Path.home())) / "WandLike" / "webview"
    storage.mkdir(parents=True, exist_ok=True)
    webview.start(storage_path=str(storage), debug=bool(os.environ.get("WANDLIKE_DEBUG")))
