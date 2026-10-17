"""Engine: turns cheat actions into console lines / memory writes and runs them."""
from __future__ import annotations

import os
import queue
import subprocess
import threading
import time
from typing import Callable

from .hook import Hook, HookError
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
        self.hook = Hook(game, log, dry_run)
        self.toggles: dict[str, bool] = {}
        self.opts: dict[str, dict] = {}
        self._last: dict[str, float] = {}
        self._lock_last: dict[str, float] = {}
        self._q: queue.Queue = queue.Queue()
        self._stop = threading.Event()
        threading.Thread(target=self._worker, daemon=True).start()
        threading.Thread(target=self._repeater, daemon=True).start()
        threading.Thread(target=self._locker, daemon=True).start()

    # ------------------------------------------------------------ status
    @property
    def hooked(self) -> bool:
        return self.hook.attached

    def hook_attach(self) -> dict:
        """Hook the game after checking it is actually running. Raises HookError with a readable reason."""
        if not self.running():
            raise HookError(f"{self.game.name} is not running")
        return self.hook.attach()

    def hook_detach(self) -> None:
        """Unhook: stop locks, put locked values back to their off state, then release the process."""
        for c in self.game.cheats:
            if c.memory and self.toggles.pop(c.id, False) and c.kind == "toggle" and "off" in c.memory:
                try:
                    self.hook.write(c.memory, c.memory["off"])
                except Exception:
                    pass
        self.hook.detach()

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

    def _find_exe(self, install_dir: str | None):
        """The game exe inside its install folder (top level, a bin folder, or up to 3 levels down)."""
        from pathlib import Path
        if not install_dir:
            return None
        root, want = Path(install_dir), self.game.exe.lower()
        for sub in ("", "binaries", "bin", "bin64", "x64", "Binaries/Win64"):
            p = root / sub / self.game.exe
            if p.exists():
                return p
        for p in root.glob("*/*/*"):
            if p.name.lower() == want:
                return p
        return None

    def launch(self, lib: dict | None = None) -> None:
        """Start the game through its store. `lib` is the detected library entry (store, id, install_dir, exe)."""
        args = self.game.launch_args
        store = (lib or {}).get("store") or ("steam" if self.game.steam_appid else "")
        if self.dry_run:
            self.log(f"[dry-run] launch via {store or 'unknown store'} with '{args}'")
        elif store == "steam":
            appid = (lib or {}).get("id") or self.game.steam_appid
            direct = self._find_exe((lib or {}).get("install_dir")) if args else None
            if direct and (direct.parent / "steam_appid.txt").exists():
                # Starting the exe ourselves avoids Steam's "custom arguments" confirmation prompt (steam:// and
                # -applaunch both show it). steam_appid.txt lets the game talk to the running Steam client.
                import shlex
                subprocess.Popen([str(direct), *shlex.split(args)], cwd=str(direct.parent))
                self.log(f"Launching {direct.name} directly with '{args}'")
                return
            os.startfile(f"steam://run/{appid}//{args}/" if args else f"steam://rungameid/{appid}")  # type: ignore[attr-defined]
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
    def apply_number(self, cheat: Cheat, value: float, mode: str = "set") -> bool:
        """True if applied (console commands are queued and count as applied)."""
        if cheat.memory:
            try:
                if mode == "add":
                    value += self.hook.read(cheat.memory) / cheat.memory.get("scale", 1)
                self.hook.write(cheat.memory, value)
                self.log(f"memory {cheat.id} = {value:g}")
                return True
            except (HookError, Exception) as e:
                self.log(f"! {cheat.label}: {self._why(e)}")
                return False
        self._submit(lambda: self.driver.send(console_lines(cheat, "number", value, mode)))
        return True

    def _why(self, e: Exception) -> str:
        return "hook the game first (press Hook)" if str(e) == "not hooked" else str(e)

    def set_toggle(self, cheat: Cheat, on: bool) -> bool:
        """True if the toggle took effect (a memory toggle fails when the game is not hooked)."""
        if cheat.memory:
            try:
                self.hook.write(cheat.memory, cheat.memory["on"] if on else cheat.memory.get("off", 0))
            except Exception as e:
                self.log(f"! {cheat.label}: {self._why(e)}")
                self.toggles[cheat.id] = False
                return False
            self.toggles[cheat.id] = on
            self._lock_last[cheat.id] = time.time()
            return True
        self.toggles[cheat.id] = on
        if cheat.interval:
            self._last[cheat.id] = time.time()  # first repeat is one interval from now
            if not on:
                return True  # repeater stops by itself
        self._submit(lambda: self.driver.send(console_lines(cheat, "on" if on else "off")))
        return True

    def set_option(self, cheat: Cheat, opt_id: str, value) -> None:
        self.opts.setdefault(cheat.id, {})[opt_id] = value

    def press(self, cheat: Cheat) -> None:
        if cheat.launch:
            self.launch()
        else:
            self._submit(lambda: self.driver.send(console_lines(cheat, "press")))

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

    def _locker(self) -> None:
        """Keep memory toggles with a "lock" interval pinned (e.g. Freeze Time re-asserts the pause flag)."""
        while not self._stop.is_set():
            time.sleep(0.02)
            if not self.hook.attached:
                continue
            now = time.time()
            for c in self.game.cheats:
                lock = (c.memory or {}).get("lock")
                if lock and c.kind == "toggle" and self.toggles.get(c.id) and now - self._lock_last.get(c.id, 0) >= lock:
                    self._lock_last[c.id] = now
                    try:
                        self.hook.write(c.memory, c.memory["on"])
                    except Exception as e:
                        self.toggles[c.id] = False
                        self.log(f"! {c.label} stopped: {self._why(e)}")

    def shutdown(self) -> None:
        self._stop.set()
