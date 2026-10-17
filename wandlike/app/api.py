"""Python side of the web UI: everything the front-end can call (exposed through pywebview)."""
from __future__ import annotations

import collections
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

from .. import version as V
from ..core import steam, updater
from ..core.engine import Engine
from ..core.hotkeys import HotkeyManager, parse_combo
from ..core.plugin import Cheat, Game, load_all, user_games_dir
from ..core.settings import Settings

DEFAULT_APP = {"autoupdate": True, "start_with_windows": False, "overlay_hotkey": "Ctrl+Shift+F10",
               "hotkeys_enabled": True, "console_mode": "key", "accent": "teal"}


class Api:
    def __init__(self, dry_run: bool = False):
        self._dry = dry_run
        self._settings = Settings()
        self._games: dict[str, Game] = load_all()
        self._engines: dict[str, Engine] = {}
        self._log: collections.deque = collections.deque(maxlen=120)
        self._windows: dict = {}
        self._running: dict[str, bool] = {}
        self._applied: dict[str, dict] = {gid: {} for gid in self._games}
        self._hk = HotkeyManager()
        self._hk_errors: dict[str, str] = {}
        self._update: dict | None = None
        self._progress: float | None = None
        self._active = next(iter(self._games), "")
        self._overlay_visible = False
        self._debug = False  # flipped by wandlike.debug.setup() in debug builds only
        self._stop = threading.Event()
        self._rebind_hotkeys()
        threading.Thread(target=self._monitor, daemon=True).start()
        if self.app()["autoupdate"]:
            threading.Thread(target=self._check_update, daemon=True).start()

    # ------------------------------------------------------------ plumbing
    def bind_windows(self, main, overlay):
        self._windows = {"main": main, "overlay": overlay}

    def _say(self, msg: str):
        if msg.strip():
            self._log.append({"t": time.strftime("%H:%M:%S"), "msg": msg})

    def _engine(self, gid: str) -> Engine:
        if gid not in self._engines:
            eng = Engine(self._games[gid], self._say, self._dry)
            self._apply_console_setting(eng)
            for c in self._games[gid].cheats:
                for oid, val in self._g(gid).get("options", {}).get(c.id, {}).items():
                    eng.set_option(c, oid, val)
            self._engines[gid] = eng
        return self._engines[gid]

    def _apply_console_setting(self, eng: Engine):
        if self.app()["console_mode"] == "combo":
            eng.driver.cfg["toggle_combo"] = ["shift", "alt", "c"]
        else:
            eng.driver.cfg.pop("toggle_combo", None)

    def app(self) -> dict:
        return {**DEFAULT_APP, **self._settings.get("app", {})}

    def _g(self, gid: str) -> dict:
        return self._settings.get(f"g.{gid}", {})

    def _gset(self, gid: str, **kw):
        d = self._g(gid)
        d.update(kw)
        self._settings.set(f"g.{gid}", d)

    def _cheat(self, gid: str, cid: str) -> Cheat:
        return self._games[gid].cheat(cid)

    # ----------------------------------------------------------- bootstrap
    def bootstrap(self) -> dict:
        return {"version": V.__version__, "repo": V.UPDATE_REPO, "app": self.app(),
                "games": [self._game_card(g) for g in self._games.values()],
                "dry_run": self._dry, "debug": self._debug}

    def _game_card(self, g: Game) -> dict:
        d = self._g(g.id)
        return {"id": g.id, "name": g.name, "appid": g.steam_appid,
                "installed": bool(g.steam_appid and steam.install_dir(g.steam_appid)),
                "favorite": d.get("favorite", False), "last_played": d.get("last_played", 0),
                "cheat_count": sum(1 for c in g.cheats if c.available and c.kind != "button")}

    def game(self, gid: str) -> dict:
        g = self._games[gid]
        self._active = gid
        d = self._g(gid)
        hk, vals, modes, opts = d.get("hotkeys", {}), d.get("values", {}), d.get("modes", {}), d.get("options", {})
        eng = self._engine(gid)
        cheats = []
        for c in g.cheats:
            cheats.append({
                "id": c.id, "label": c.label, "kind": c.kind, "section": c.section, "info": c.info,
                "requires": c.requires, "verified": c.verified, "unsupported": c.unsupported,
                "available": c.available, "default": c.default, "min": c.min, "max": c.max, "step": c.step,
                "has_modes": bool(c.add and c.set), "launch": c.launch,
                "options": [{**o, "value": opts.get(c.id, {}).get(o["id"], o.get("default"))} for o in c.options],
                "pinned": c.id in d.get("pins", []), "hotkey": hk.get(c.id, ""),
                "value": vals.get(c.id, c.default), "mode": modes.get(c.id, "set"),
            })
        return {**self._game_card(g), "notes": g.notes, "cheats": cheats, "auto_apply": d.get("auto_apply", False),
                "has_preset": bool(d.get("preset")), "running": self._running.get(gid, False),
                "toggles": dict(eng.toggles)}

    def art(self, gid: str, kind: str) -> str:
        appid = self._games[gid].steam_appid
        return steam.art_data_url(appid, kind) if appid else ""

    def state(self) -> dict:
        return {"running": dict(self._running),
                "toggles": {gid: dict(e.toggles) for gid, e in self._engines.items()},
                "applied": self._applied, "log": list(self._log)[-60:], "update": self._update,
                "progress": self._progress, "hk_errors": dict(self._hk_errors),
                "overlay": self._overlay_visible, "active": self._active}

    # -------------------------------------------------------------- cheats
    def set_number(self, gid: str, cid: str, value: float, mode: str = "set"):
        c = self._cheat(gid, cid)
        value = max(c.min, min(c.max, float(value)))
        self._engine(gid).apply_number(c, value, mode)
        self._applied.setdefault(gid, {})[cid] = [value, mode]
        d = self._g(gid)
        d.setdefault("values", {})[cid] = value
        d.setdefault("modes", {})[cid] = mode
        self._settings.set(f"g.{gid}", d)
        return value

    def remember_value(self, gid: str, cid: str, value: float, mode: str = "set"):
        d = self._g(gid)
        d.setdefault("values", {})[cid] = value
        d.setdefault("modes", {})[cid] = mode
        self._settings.set(f"g.{gid}", d)

    def set_toggle(self, gid: str, cid: str, on: bool):
        c = self._cheat(gid, cid)
        if not c.available:
            return False
        self._engine(gid).set_toggle(c, bool(on))
        return bool(on)

    def press(self, gid: str, cid: str):
        self._engine(gid).press(self._cheat(gid, cid))

    def set_option(self, gid: str, cid: str, opt_id: str, value):
        c = self._cheat(gid, cid)
        self._engine(gid).set_option(c, opt_id, value)
        d = self._g(gid)
        d.setdefault("options", {}).setdefault(cid, {})[opt_id] = value
        self._settings.set(f"g.{gid}", d)

    def pin(self, gid: str, cid: str, pinned: bool):
        d = self._g(gid)
        pins = set(d.get("pins", []))
        pins.add(cid) if pinned else pins.discard(cid)
        self._gset(gid, pins=sorted(pins))

    def favorite(self, gid: str, fav: bool):
        self._gset(gid, favorite=bool(fav))

    # ------------------------------------------------------------- presets
    def save_preset(self, gid: str):
        eng = self._engine(gid)
        preset = {"toggles": [cid for cid, on in eng.toggles.items() if on],
                  "numbers": self._applied.get(gid, {})}
        self._gset(gid, preset=preset)
        self._say(f"Saved {len(preset['toggles'])} toggles and {len(preset['numbers'])} values for {self._games[gid].name}")

    def apply_preset(self, gid: str):
        p = self._g(gid).get("preset")
        if not p:
            self._say("No saved cheats yet")
            return
        for cid in p["toggles"]:
            self.set_toggle(gid, cid, True)
        for cid, (v, mode) in p["numbers"].items():
            self.set_number(gid, cid, v, mode)

    def set_auto_apply(self, gid: str, on: bool):
        self._gset(gid, auto_apply=bool(on))

    # ------------------------------------------------------------- hotkeys
    def set_hotkey(self, gid: str, cid: str, combo: str) -> str:
        """Assign (or clear with ''). Returns '' or an error message."""
        if combo and not parse_combo(combo):
            return "Unrecognised key combination"
        d = self._g(gid)
        hk = d.setdefault("hotkeys", {})
        if combo:
            hk[cid] = combo
        else:
            hk.pop(cid, None)
        self._settings.set(f"g.{gid}", d)
        self._rebind_hotkeys()
        return self._hk_errors.get(f"{gid}.{cid}", "")

    def _rebind_hotkeys(self):
        self._hk.clear()
        self._hk_errors.clear()
        if not self.app()["hotkeys_enabled"]:
            return
        ov = self.app()["overlay_hotkey"]
        if ov:
            _, err = self._hk.register(ov, self.toggle_overlay)
            if err:
                self._hk_errors["overlay"] = err
        for gid, g in self._games.items():
            for cid, combo in self._g(gid).get("hotkeys", {}).items():
                _, err = self._hk.register(combo, lambda gid=gid, cid=cid: self._fire(gid, cid))
                if err:
                    self._hk_errors[f"{gid}.{cid}"] = err

    def _fire(self, gid: str, cid: str):
        c = self._cheat(gid, cid)
        if c.kind == "toggle":
            now = self._engine(gid).toggles.get(cid, False)
            self.set_toggle(gid, cid, not now)
        elif c.kind == "number":
            d = self._g(gid)
            self.set_number(gid, cid, d.get("values", {}).get(cid, c.default), d.get("modes", {}).get(cid, "set"))
        else:
            self.press(gid, cid)

    # ------------------------------------------------------------ launching
    def play(self, gid: str):
        eng = self._engine(gid)
        if eng.running():
            return "running"
        g = self._games[gid]
        eng.launch()
        self._gset(gid, last_played=int(time.time()))
        return "launching"

    # ------------------------------------------------------------- settings
    def settings(self) -> dict:
        return {**self.app(), "version": V.__version__, "repo": V.UPDATE_REPO,
                "plugins_dir": str(user_games_dir()), "frozen": bool(getattr(sys, "frozen", False))}

    def set_setting(self, key: str, value):
        if key not in DEFAULT_APP:
            return
        a = self.app()
        a[key] = value
        self._settings.set("app", a)
        if key in ("overlay_hotkey", "hotkeys_enabled"):
            self._rebind_hotkeys()
        elif key == "console_mode":
            for e in self._engines.values():
                self._apply_console_setting(e)
        elif key == "start_with_windows":
            self._set_startup(bool(value))
        return self._hk_errors.get("overlay", "")

    def _set_startup(self, on: bool):
        try:
            import winreg
            exe = self._launcher_path()
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run",
                                0, winreg.KEY_SET_VALUE) as k:
                if on and exe:
                    winreg.SetValueEx(k, V.APP_NAME, 0, winreg.REG_SZ, f'"{exe}"')
                else:
                    try:
                        winreg.DeleteValue(k, V.APP_NAME)
                    except OSError:
                        pass
        except OSError as e:
            self._say(f"! could not change startup setting: {e}")

    @staticmethod
    def _launcher_path() -> str | None:
        if not getattr(sys, "frozen", False):
            return None
        root = Path(sys.executable).resolve().parent.parent  # .../WandLike/app-X/WandLike.exe
        launcher = root / f"{V.APP_NAME}.exe"
        return str(launcher if launcher.exists() else sys.executable)

    def open_plugins_folder(self):
        d = user_games_dir()
        d.mkdir(parents=True, exist_ok=True)
        os.startfile(str(d))  # type: ignore[attr-defined]

    def open_url(self, url: str):
        if url.startswith("https://"):
            os.startfile(url)  # type: ignore[attr-defined]

    # -------------------------------------------------------------- updates
    def _check_update(self, manual: bool = False):
        try:
            self._update = updater.check()
            if manual and not self._update:
                self._say(f"You're up to date (v{V.__version__})")
        except Exception as e:
            if manual:
                self._say(f"! update check failed: {e}")

    def check_update(self):
        if not V.UPDATE_REPO:
            self._say("Auto-update is not configured (UPDATE_REPO is empty)")
            return
        threading.Thread(target=self._check_update, args=(True,), daemon=True).start()

    def install_update(self):
        info = self._update
        if not info:
            return

        def work():
            try:
                path = updater.download(info, lambda f: setattr(self, "_progress", f))
                updater.apply(path)
            except Exception as e:
                self._progress = None
                self._say(f"! update failed: {e}")
                return
            time.sleep(0.3)
            os._exit(0)
        threading.Thread(target=work, daemon=True).start()

    # --------------------------------------------------------------- windows
    def win(self, action: str):
        w = self._windows.get("main")
        if not w:
            return
        if action == "minimize":
            w.minimize()
        elif action == "maximize":
            w.maximize()
        elif action == "restore":
            w.restore()
        elif action == "close":
            w.destroy()

    def toggle_overlay(self):
        w = self._windows.get("overlay")
        if not w:
            return
        if self._overlay_visible:
            w.hide()
        else:
            w.show()
            try:
                w.on_top = True
            except Exception:
                pass
        self._overlay_visible = not self._overlay_visible

    # --------------------------------------------------------------- monitor
    def _monitor(self):
        from ..core import winapi
        while not self._stop.is_set():
            for gid, g in self._games.items():
                now = bool(winapi.find_pids(g.exe)) if not self._dry else False
                was = self._running.get(gid, False)
                self._running[gid] = now
                if now and not was:
                    self._gset(gid, last_played=int(time.time()))
                    self._say(f"{g.name} detected")
                    if self._g(gid).get("auto_apply") and self._g(gid).get("preset"):
                        threading.Thread(target=self._auto_apply, args=(gid,), daemon=True).start()
                elif was and not now:
                    self._say(f"{g.name} closed")
                    if gid in self._engines:
                        self._engines[gid].toggles.clear()
                    self._applied[gid] = {}
            time.sleep(2)

    def _auto_apply(self, gid: str):
        time.sleep(20)  # give the game time to reach the map
        self._say("Applying saved cheats")
        self.apply_preset(gid)
