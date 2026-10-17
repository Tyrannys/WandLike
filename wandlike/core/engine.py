"""Engine: turns cheat actions into console lines / memory writes and runs them."""
from __future__ import annotations

import os
import queue
import subprocess
import threading
import time
from typing import Callable

from .plugin import Cheat, Game


def fmt_value(v: float) -> str:
    return str(int(v)) if float(v).is_integer() else str(v)


def console_lines(cheat: Cheat, action: str, value: float | None = None, mode: str = "set") -> list[str]:
    """Pure function: the console lines for an action. action: number|on|off|press."""
    if action == "number":
        tpl = cheat.set if (mode == "set" and cheat.set) else (cheat.add or cheat.set)
        if tpl is None:
            return []
        return [tpl.format(value=fmt_value(value))]
    if action == "on":
        return list(cheat.on)
    if action == "off":
        return list(cheat.off)
    if action == "press":
        return list(cheat.run)
    raise ValueError(action)


class ConsoleDriver:
    """Types commands into the game's in-game console. dry_run just logs them."""

    def __init__(self, game: Game, log: Callable[[str], None], dry_run: bool = False):
        self.game, self.log, self.dry_run = game, log, dry_run
        self.cfg = game.console
        self.hwnd: int | None = None

    def attach(self) -> bool:
        from . import winapi
        pids = winapi.find_pids(self.game.exe)
        self.hwnd = winapi.find_window(self.game.window_title, pids) if pids else None
        return bool(self.hwnd)

    def send(self, lines: list[str]) -> None:
        if not lines:
            return
        if self.dry_run:
            for ln in lines:
                self.log(f"[dry-run] {ln}")
            return
        from . import winapi
        if not self.hwnd or not winapi.user32.IsWindow(self.hwnd):
            if not self.attach():
                raise RuntimeError(f"{self.game.name} is not running")
        if not winapi.focus_window(self.hwnd):
            raise RuntimeError("could not focus the game window (is it minimised / elevated?)")
        key = self.cfg.get("toggle_scancode", 0x29)
        combo = self.cfg.get("toggle_combo")
        open_delay = self.cfg.get("open_delay", 0.2)
        winapi.tap_combo(combo) if combo else winapi.tap_scan(key)
        time.sleep(open_delay)
        for ln in lines:
            winapi.type_text(ln, self.cfg.get("type_delay", 0.008))
            time.sleep(0.05)
            winapi.tap_scan(winapi.SCAN["enter"])
            time.sleep(self.cfg.get("line_delay", 0.12))
            self.log(f"> {ln}")
        if self.cfg.get("close_after", True):
            winapi.tap_combo(combo) if combo else winapi.tap_scan(key)
            time.sleep(0.08)


class Engine:
    def __init__(self, game: Game, log: Callable[[str], None] = print, dry_run: bool = False):
        self.game, self.log, self.dry_run = game, log, dry_run
        self.driver = ConsoleDriver(game, log, dry_run)
        self.toggles: dict[str, bool] = {}
        self.opts: dict[str, dict] = {}
        self._last: dict[str, float] = {}
        self._q: queue.Queue = queue.Queue()
        self._stop = threading.Event()
        threading.Thread(target=self._worker, daemon=True).start()
        threading.Thread(target=self._repeater, daemon=True).start()

    # ------------------------------------------------------------ status
    def running(self) -> bool:
        if self.dry_run:
            return True
        from . import winapi
        return bool(winapi.find_pids(self.game.exe))

    def debug_mode_detected(self) -> bool | None:
        """True/False if we can read the process command line, else None."""
        if self.dry_run:
            return None
        try:
            out = subprocess.run(
                ["powershell", "-NoProfile", "-Command",
                 f"(Get-CimInstance Win32_Process -Filter \"Name='{self.game.exe}'\").CommandLine"],
                capture_output=True, text=True, timeout=8).stdout
        except Exception:
            return None
        if not out.strip():
            return None
        return "debug_mode" in out

    def launch(self, lib: dict | None = None) -> None:
        """Start the game through its store. `lib` is the detected library entry (store, id, install_dir, exe)."""
        args = self.game.launch_args
        store = (lib or {}).get("store") or ("steam" if self.game.steam_appid else "")
        if self.dry_run:
            self.log(f"[dry-run] launch via {store or 'unknown store'} with '{args}'")
        elif store == "steam":
            appid = (lib or {}).get("id") or self.game.steam_appid
            os.startfile(f"steam://run/{appid}//{args}/")  # type: ignore[attr-defined]
            self.log(f"Launching via Steam with '{args}'")
        elif store == "epic":
            os.startfile(f"com.epicgames.launcher://apps/{lib['id']}?action=launch&silent=true")  # type: ignore[attr-defined]
            self.log("Launching via Epic Games" + (" (launch options can't be passed; set them in Epic)" if args else ""))
        elif store == "gog" and lib.get("exe"):
            import shlex
            subprocess.Popen([os.path.join(lib["install_dir"], lib["exe"]), *shlex.split(args)], cwd=lib["install_dir"])
            self.log(f"Launching GOG install with '{args}'")
        else:
            self.log("Couldn't find this game in your libraries; launch it manually with: " + args)

    # ----------------------------------------------------------- actions
    def apply_number(self, cheat: Cheat, value: float, mode: str = "set") -> None:
        if cheat.memory:
            self._submit(lambda: self._mem_set(cheat, value))
        else:
            self._submit(lambda: self.driver.send(console_lines(cheat, "number", value, mode)))

    def set_toggle(self, cheat: Cheat, on: bool) -> None:
        self.toggles[cheat.id] = on
        if cheat.interval:
            self._last[cheat.id] = time.time()  # first repeat is one interval from now
            if not on:
                return  # repeater stops by itself
        self._submit(lambda: self.driver.send(console_lines(cheat, "on" if on else "off")))

    def set_option(self, cheat: Cheat, opt_id: str, value) -> None:
        self.opts.setdefault(cheat.id, {})[opt_id] = value

    def press(self, cheat: Cheat) -> None:
        if cheat.launch:
            self.launch()
        else:
            self._submit(lambda: self.driver.send(console_lines(cheat, "press")))

    def _mem_set(self, cheat: Cheat, value: float) -> None:
        from . import winapi
        from .memory import Process
        pids = winapi.find_pids(self.game.exe)
        if not pids:
            raise RuntimeError(f"{self.game.name} is not running")
        with Process(pids[0]) as p:
            p.set(cheat.memory, value)
        self.log(f"memory {cheat.id} = {value}")

    # ----------------------------------------------------------- workers
    def _submit(self, fn: Callable[[], None]) -> None:
        self._q.put(fn)

    def _worker(self) -> None:
        while not self._stop.is_set():
            fn = self._q.get()
            try:
                fn()
            except Exception as e:  # surface to UI log, keep running
                self.log(f"! {e}")

    def _repeater(self) -> None:
        last = self._last
        while not self._stop.is_set():
            now = time.time()
            for c in self.game.cheats:
                if c.kind == "toggle" and c.interval and self.toggles.get(c.id):
                    every = float(self.opts.get(c.id, {}).get("interval", c.interval))
                    if now - last.get(c.id, 0) >= every:
                        last[c.id] = now
                        self._submit(lambda c=c: self.driver.send(console_lines(c, "on")))
            time.sleep(0.5)

    def shutdown(self) -> None:
        self._stop.set()
