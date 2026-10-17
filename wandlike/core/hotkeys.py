"""Global hotkeys via RegisterHotKey on a dedicated message-loop thread."""
from __future__ import annotations

import ctypes
import queue
import threading
from ctypes import wintypes
from typing import Callable

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
user32.RegisterHotKey.argtypes = [wintypes.HWND, ctypes.c_int, wintypes.UINT, wintypes.UINT]
user32.UnregisterHotKey.argtypes = [wintypes.HWND, ctypes.c_int]
user32.PostThreadMessageW.argtypes = [wintypes.DWORD, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

MOD = {"alt": 1, "ctrl": 2, "shift": 4, "win": 8}
NOREPEAT = 0x4000
WM_HOTKEY, WM_APP, WM_QUIT = 0x0312, 0x8000, 0x0012

NAMED = {"space": 0x20, "enter": 0x0D, "tab": 0x09, "esc": 0x1B, "backspace": 0x08, "insert": 0x2D,
         "delete": 0x2E, "home": 0x24, "end": 0x23, "pageup": 0x21, "pagedown": 0x22,
         "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28, "`": 0xC0, "-": 0xBD, "=": 0xBB,
         "[": 0xDB, "]": 0xDD, ";": 0xBA, "'": 0xDE, ",": 0xBC, ".": 0xBE, "/": 0xBF, "\\": 0xDC}


def parse_combo(combo: str) -> tuple[int, int] | None:
    """'Ctrl+Shift+F5' -> (mods, vk); None if invalid. A modifier-less key is allowed."""
    mods, vk = 0, None
    for part in [p.strip().lower() for p in combo.split("+") if p.strip()]:
        if part in MOD:
            mods |= MOD[part]
        elif part in NAMED:
            vk = NAMED[part]
        elif len(part) == 1 and part.isalnum():
            vk = ord(part.upper())
        elif part.startswith("f") and part[1:].isdigit() and 1 <= int(part[1:]) <= 24:
            vk = 0x70 + int(part[1:]) - 1
        elif part.startswith("num") and part[3:].isdigit() and 0 <= int(part[3:]) <= 9:
            vk = 0x60 + int(part[3:])
        else:
            return None
    return (mods, vk) if vk is not None else None


class HotkeyManager:
    def __init__(self):
        self._cmds: queue.Queue = queue.Queue()
        self._handlers: dict[int, Callable[[], None]] = {}
        self._next = 1
        self._tid = 0
        self._ready = threading.Event()
        threading.Thread(target=self._loop, daemon=True).start()
        self._ready.wait(2)

    def _loop(self):
        self._tid = kernel32.GetCurrentThreadId()
        msg = wintypes.MSG()
        user32.PeekMessageW(ctypes.byref(msg), None, WM_APP, WM_APP, 0)  # create the queue
        self._ready.set()
        while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
            if msg.message == WM_APP:
                while not self._cmds.empty():
                    self._cmds.get()()
            elif msg.message == WM_HOTKEY:
                fn = self._handlers.get(int(msg.wParam))
                if fn:
                    threading.Thread(target=fn, daemon=True).start()

    def _run(self, fn):
        done, box = threading.Event(), {}

        def wrapped():
            try:
                box["r"] = fn()
            finally:
                done.set()
        self._cmds.put(wrapped)
        user32.PostThreadMessageW(self._tid, WM_APP, 0, 0)
        done.wait(2)
        return box.get("r")

    def register(self, combo: str, handler: Callable[[], None]) -> tuple[int | None, str]:
        """Returns (id, '') or (None, reason)."""
        parsed = parse_combo(combo)
        if not parsed:
            return None, "unrecognised key combination"
        mods, vk = parsed

        def do():
            hid = self._next
            self._next += 1
            if not user32.RegisterHotKey(None, hid, mods | NOREPEAT, vk):
                return None
            self._handlers[hid] = handler
            return hid
        hid = self._run(do)
        return (hid, "") if hid else (None, "already used by another program")

    def clear(self):
        def do():
            for hid in list(self._handlers):
                user32.UnregisterHotKey(None, hid)
            self._handlers.clear()
        self._run(do)

    def close(self):
        self.clear()
        user32.PostThreadMessageW(self._tid, WM_QUIT, 0, 0)
