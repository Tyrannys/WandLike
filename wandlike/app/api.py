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
from ..core import library as libmod, steam, updater
from ..core.catalog import Catalog
from ..core.engine import Engine
from ..core.hotkeys import HotkeyManager, parse_combo
from ..core.library import Library
from ..core.plugin import Cheat, Game, load_all, user_games_dir
from ..core.settings import Settings

DEFAULT_APP = {"autoupdate": True, "start_with_windows": False, "overlay_hotkey": "Ctrl+Shift+F10",
               "hotkeys_enabled": True, "console_mode": "key", "accent": "teal", "catalog_sync": True,
               "auto_hook": True}
CATALOG_EVERY = 6 * 3600   # seconds between routine catalog syncs (an unchanged index is a cheap 304)
RESCAN_EVERY = 90          # seconds between cached library rescans (stat calls only)


class Api:
    def __init__(self, dry_run: bool = False, background: bool = True):
        self._dry = dry_run
        self._settings = Settings()
        self._library = Library()
        self._detected: list[dict] = self._library.scan()
        self._catalog = Catalog()
        self._sync_lock = threading.Lock()
        self._rev = 0
        self._notice_id = 0
        self._notices: collections.deque = collections.deque(maxlen=8)
        self._games: dict[str, Game] = load_all()
        self._engines: dict[str, Engine] = {}
        self._log: collections.deque = collections.deque(maxlen=120)
        self._windows: dict = {}
        self._running: dict[str, bool] = {}
        self._applied: dict[str, dict] = collections.defaultdict(dict)
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
        if background:
            threading.Thread(target=self._background, daemon=True).start()
            if self.app()["autoupdate"]:
                threading.Thread(target=self._check_update, daemon=True).start()

    # ------------------------------------------------------------ plumbing
    def bind_windows(self, main, overlay):
        self._windows = {"main": main, "overlay": overlay}

    def _notice(self, text: str):
        """A message the UI should pop up once (e.g. a game's cheats were updated)."""
        self._notice_id += 1
        self._notices.append({"id": self._notice_id, "text": text})

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
                "dry_run": self._dry, "debug": self._debug, "rev": self._rev,
                "catalog_only": self._catalog.entries_for_ui(set(self._games), self._detected)}

    def _lib(self, g: Game) -> dict | None:
        """The detected library entry (store, id, install dir) for a plugin, if the game is installed."""
        return libmod.find(self._detected, g.match_spec)

    @staticmethod
    def _steam_appid(g: Game):
        return g.steam_appid or g.match.get("steam_appid")

    def _game_card(self, g: Game) -> dict:
        d = self._g(g.id)
        lib = self._lib(g)
        return {"id": g.id, "name": g.name, "appid": self._steam_appid(g), "installed": bool(lib),
                "store": lib["store"] if lib else "", "source": g.source, "version": g.version,
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
                "source": "launch" if c.launch else "memory" if c.memory else "console",
                "options": [{**o, "value": opts.get(c.id, {}).get(o["id"], o.get("default"))} for o in c.options],
                "pinned": c.id in d.get("pins", []), "hotkey": hk.get(c.id, ""),
                "value": vals.get(c.id, c.default), "mode": modes.get(c.id, "set"),
            })
        return {**self._game_card(g), "notes": g.notes, "cheats": cheats, "auto_apply": d.get("auto_apply", False),
                "has_preset": bool(d.get("preset")), "running": self._running.get(gid, False),
                "needs_console": bool(g.console.get("requires_symbol")), "hooked": eng.hooked,
                "toggles": dict(eng.toggles)}

    def art(self, gid: str, kind: str) -> str:
        appid = self._steam_appid(self._games[gid])
        return steam.art_data_url(int(appid), kind) if appid else ""

    def state(self) -> dict:
        return {"running": dict(self._running),
                "toggles": {gid: dict(e.toggles) for gid, e in self._engines.items()},
                "applied": self._applied, "log": list(self._log)[-60:], "update": self._update,
                "progress": self._progress, "hk_errors": dict(self._hk_errors),
                "overlay": self._overlay_visible, "active": self._active, "rev": self._rev,
                "hooked": {gid: e.hooked for gid, e in self._engines.items()},
                "console_ready": {gid: e.console_ready()[0] for gid, e in self._engines.items()
                                  if e.game.console.get("requires_symbol")},
                "catalog": {"status": self._catalog.status, "last_sync": self._catalog.last_sync},
                "notices": list(self._notices),
                "library": {"count": len(self._detected), "scanned_at": self._library.scanned_at}}

    # -------------------------------------------------------------- cheats
    def set_number(self, gid: str, cid: str, value: float, mode: str = "set"):
        c = self._cheat(gid, cid)
        value = max(c.min, min(c.max, float(value)))
        eng = self._engine(gid)
        ok = eng.apply_number(c, value, mode)
        d = self._g(gid)
        d.setdefault("values", {})[cid] = value
        d.setdefault("modes", {})[cid] = mode
        self._settings.set(f"g.{gid}", d)
        if ok:
            self._applied.setdefault(gid, {})[cid] = [value, mode]
        return {"value": value, "ok": ok, "error": "" if ok else eng.last_error}

    def remember_value(self, gid: str, cid: str, value: float, mode: str = "set"):
        d = self._g(gid)
        d.setdefault("values", {})[cid] = value
        d.setdefault("modes", {})[cid] = mode
        self._settings.set(f"g.{gid}", d)

    def set_toggle(self, gid: str, cid: str, on: bool):
        """{on, ok, error}: `on` is the state now in effect, `error` says why a request was refused."""
        c = self._cheat(gid, cid)
        if not c.available:
            return {"on": False, "ok": False, "error": c.unsupported or "Not available"}
        eng = self._engine(gid)
        ok = eng.set_toggle(c, bool(on))
        return {"on": bool(on) and ok, "ok": ok, "error": "" if ok else eng.last_error}

    def press(self, gid: str, cid: str):
        eng = self._engine(gid)
        ok = eng.press(self._cheat(gid, cid))
        return {"ok": ok, "error": "" if ok else eng.last_error}

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
        g = self._games[gid]
        for cid in sorted(p["toggles"], key=lambda i: 0 if g.cheat(i).memory else 1):   # Enable Console before console cheats
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
        eng.launch(self._lib(self._games[gid]))
        self._gset(gid, last_played=int(time.time()))
        return "launching"

    # --------------------------------------------------------------- hooking
    def hook(self, gid: str) -> dict:
        """Attach to the running game. {ok, error, info}; refuses cleanly if the game is not running."""
        from ..core.hook import HookError
        try:
            return {"ok": True, "info": self._engine(gid).hook_attach()}
        except HookError as e:
            self._say(f"! {e}")
            return {"ok": False, "error": str(e)}

    def unhook(self, gid: str) -> dict:
        self._engine(gid).hook_detach()
        return {"ok": True}

    def hook_info(self, gid: str) -> dict:
        return self._engine(gid).hook.info()

    def _auto_hook(self, gid: str):
        time.sleep(4)  # let the game finish creating its window and mapping its code
        if self._running.get(gid) and not self._engine(gid).hooked:
            self.hook(gid)

    # ------------------------------------------------- library + game catalog
    def _reload_games(self):
        """Re-read plugins (bundled + catalog + user) after the catalog added or updated one."""
        new = load_all()
        for gid, eng in list(self._engines.items()):
            if gid not in new or new[gid].version != eng.game.version:
                eng.shutdown()
                del self._engines[gid]
        self._games = new
        self._rev += 1
        self._rebind_hotkeys()

    def _rescan(self, force: bool = False) -> bool:
        """Cached library scan; True if the set of detected games changed."""
        before = {(g["store"], g["id"]) for g in self._detected}
        self._detected = self._library.scan(force)
        changed = before != {(g["store"], g["id"]) for g in self._detected}
        if changed:
            self._rev += 1
        return changed

    def _sync_catalog(self):
        if not self.app()["catalog_sync"] or not self._sync_lock.acquire(blocking=False):
            return
        try:
            before = list(self._catalog.index)
            msgs = self._catalog.sync(self._detected)
            for m in msgs:
                self._say(m)
                if m.startswith(("Added support", "Updated support")):
                    self._notice(m)
            if any(not m.startswith("!") for m in msgs):
                self._reload_games()
            elif self._catalog.index != before:
                self._rev += 1  # the Explore list may have changed
        except Exception as e:
            self._say(f"! catalog sync failed: {e}")
        finally:
            self._sync_lock.release()

    def _background(self):
        time.sleep(2)
        self._sync_catalog()
        last_sync = time.time()
        while not self._stop.wait(RESCAN_EVERY):
            changed = self._rescan()
            if changed:
                self._say("Game library changed")
            if changed or time.time() - last_sync > CATALOG_EVERY:
                self._sync_catalog()
                last_sync = time.time()

    def rescan_library(self, full: bool = True) -> dict:
        """Settings > Rescan. `full` drops the cache and re-reads every manifest."""
        self._rescan(force=full)
        threading.Thread(target=self._sync_catalog, daemon=True).start()
        return {"count": len(self._detected), "stats": dict(self._library.stats)}

    def sync_catalog(self):
        threading.Thread(target=self._sync_catalog, daemon=True).start()

    def library_info(self) -> list[dict]:
        supported = {(lg["store"], lg["id"]) for g in self._games.values() if (lg := self._lib(g))}
        return [{"name": g["name"], "store": g["store"], "path": g["install_dir"],
                 "supported": (g["store"], g["id"]) in supported} for g in self._detected]

    # ------------------------------------------------------------- settings
    def settings(self) -> dict:
        return {**self.app(), "version": V.__version__, "repo": V.UPDATE_REPO,
                "plugins_dir": str(user_games_dir()), "frozen": bool(getattr(sys, "frozen", False)),
                "library_count": len(self._detected), "library_scanned_at": self._library.scanned_at,
                "catalog_status": self._catalog.status}

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
                    if self.app()["auto_hook"]:
                        threading.Thread(target=self._auto_hook, args=(gid,), daemon=True).start()
                    if self._g(gid).get("auto_apply") and self._g(gid).get("preset"):
                        threading.Thread(target=self._auto_apply, args=(gid,), daemon=True).start()
                elif was and not now:
                    self._say(f"{g.name} closed")
                    if gid in self._engines:
                        self._engines[gid].toggles.clear()
                        self._engines[gid].hook.detach()
                    self._applied[gid] = {}
            time.sleep(2)

    def _auto_apply(self, gid: str):
        time.sleep(20)  # give the game time to reach the map
        self._say("Applying saved cheats")
        self.apply_preset(gid)
