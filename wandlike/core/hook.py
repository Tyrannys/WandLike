"""Hooking: attach to a running game and expose named memory symbols for real-time cheats.

A plugin's "hook" section describes symbols, each found by byte pattern (so it survives patches):

    "hook": {"symbols": {"paused": {
        "type": "uint8",
        "aob": "80 3D ?? ?? ?? ?? 00 0F 85",          # bytes in the code that reference the variable ('??' = any)
        "rip": {"disp_at": 2, "insn_len": 7},         # RIP-relative operand: target = match + insn_len + int32 at match+disp_at
        "section": ".text",                           # where to search (default .text)
        "rva": "0x1234567", "exe_size": 101123704,    # optional fallback, used only if the exe size matches
        "deref": [16, 8]                              # optional pointer chain applied when reading/writing
    }}}

Attaching resolves every symbol once. Symbols that can't be found are reported (`unresolved`) and only the
cheats that need them fail, with a clear message.
"""
from __future__ import annotations

import re
import struct
import threading
from typing import Callable

from . import winapi
from .memory import MemoryError_, Process, TYPES, compile_pattern

CHUNK = 4 << 20
SYMBOL_KEYS = {"type", "aob", "rip", "offset", "rva", "exe_size", "section", "index", "deref", "unique"}


class HookError(RuntimeError):
    pass


def _int(v) -> int:
    return int(v, 16) if isinstance(v, str) else int(v)


class Hook:
    def __init__(self, game, log: Callable[[str], None] = print, dry_run: bool = False):
        self.game, self.log, self.dry_run = game, log, dry_run
        self.proc: Process | None = None
        self.pid = self.base = self.size = 0
        self.symbols: dict[str, int] = {}        # name -> absolute address of the symbol
        self.unresolved: dict[str, str] = {}
        self._lock = threading.RLock()
        self._fake: dict[str, float] = {}        # dry-run memory

    # --------------------------------------------------------------- state
    @property
    def attached(self) -> bool:
        return bool(self.pid) if self.dry_run else bool(self.proc)

    def alive(self) -> bool:
        return True if self.dry_run else bool(self.proc and self.proc.alive())

    def info(self) -> dict:
        return {"attached": self.attached, "pid": self.pid, "base": hex(self.base), "size": self.size,
                "resolved": {n: hex(a - self.base) for n, a in self.symbols.items()},
                "unresolved": dict(self.unresolved)}

    # -------------------------------------------------------------- attach
    def attach(self) -> dict:
        """Hook the running game. Raises HookError with a user-facing reason."""
        with self._lock:
            if self.attached:
                return self.info()
            specs = self.game.hook.get("symbols", {})
            if self.dry_run:
                self.pid, self.base, self.size = 4242, 0x140000000, 0
                self.symbols = {n: 0x140100000 + i * 8 for i, n in enumerate(specs)}
                self.unresolved = {}
                self.log(f"[dry-run] hooked {self.game.name} ({len(specs)} symbols)")
                return self.info()
            pids = winapi.find_pids(self.game.exe)
            if not pids:
                raise HookError(f"{self.game.name} is not running")
            try:
                proc = Process(pids[0])
            except MemoryError_ as e:
                raise HookError(str(e)) from e
            rng = winapi.module_range(pids[0], self.game.exe)
            if not rng:
                proc.close()
                raise HookError(f"could not find {self.game.exe} in the process")
            self.proc, self.pid, (self.base, self.size) = proc, pids[0], rng
            try:
                self.symbols, self.unresolved = self._resolve(specs)
            except Exception:
                self.detach()
                raise
            self.log(f"Hooked {self.game.name} (pid {self.pid}, {len(self.symbols)}/{len(specs)} symbols resolved)")
            for name, why in self.unresolved.items():
                self.log(f"! symbol '{name}': {why}")
            return self.info()

    def detach(self) -> None:
        with self._lock:
            was = self.attached
            if self.proc:
                self.proc.close()
            self.proc, self.pid, self.base, self.size = None, 0, 0, 0
            self.symbols, self.unresolved, self._fake = {}, {}, {}
            if was:
                self.log(f"Unhooked {self.game.name}")

    # ------------------------------------------------------------- resolve
    def _resolve(self, specs: dict) -> tuple[dict[str, int], dict[str, str]]:
        found: dict[str, int] = {}
        missing: dict[str, str] = {}
        todo: dict[str, dict] = {}
        for name, spec in specs.items():
            if "aob" in spec:
                todo[name] = spec
            elif "rva" in spec:
                if spec.get("exe_size") and int(spec["exe_size"]) != self.size:
                    missing[name] = (f"fixed address is for a different game build (exe size {self.size}, "
                                     f"expected {spec['exe_size']}); this version isn't supported yet")
                else:
                    found[name] = self.base + _int(spec["rva"])
            else:
                missing[name] = "symbol has neither 'aob' nor 'rva'"
        if todo:
            hits = self._scan(todo)
            for name, spec in todo.items():
                addrs = hits.get(name, [])
                if not addrs and "rva" in spec and int(spec.get("exe_size", -1)) == self.size:
                    found[name] = self.base + _int(spec["rva"])      # patch-specific fallback
                elif not addrs:
                    missing[name] = "byte pattern not found: this game version isn't supported yet"
                elif len(addrs) > 1 and spec.get("unique", True) and "index" not in spec:
                    missing[name] = f"byte pattern is ambiguous ({len(addrs)} matches); make it longer"
                else:
                    match = addrs[int(spec.get("index", 0))]
                    found[name] = self._target(match, spec)
        return found, missing

    def _target(self, match: int, spec: dict) -> int:
        rip = spec.get("rip")
        if rip:
            disp, = struct.unpack("<i", self.proc.read(match + int(rip["disp_at"]), 4))
            return match + int(rip["insn_len"]) + disp
        return match + int(spec.get("offset", 0))

    def _scan(self, specs: dict[str, dict]) -> dict[str, list[int]]:
        """One pass over each needed section, searching every pattern at once."""
        sections = {s["name"]: s for s in self.proc.pe_sections(self.base)}
        by_section: dict[str, dict[str, re.Pattern]] = {}
        for name, spec in specs.items():
            by_section.setdefault(spec.get("section", ".text"), {})[name] = compile_pattern(spec["aob"])
        hits: dict[str, list[int]] = {n: [] for n in specs}
        for sec_name, patterns in by_section.items():
            sec = sections.get(sec_name)
            if not sec:
                continue
            start, size = self.base + sec["rva"], sec["size"]
            tail, tail_base = b"", start
            for addr, data in self.proc.read_region_chunks(start, size, CHUNK):
                buf = tail + data
                buf_base = addr - len(tail)
                for name, rx in patterns.items():
                    pos = 0
                    while len(hits[name]) < 4 and (m := rx.search(buf, pos)):
                        a = buf_base + m.start()
                        if a not in hits[name]:
                            hits[name].append(a)
                        pos = m.start() + 1
                tail = buf[-64:]
        return hits

    # ---------------------------------------------------------- read/write
    def _address(self, spec: dict) -> tuple[int, str]:
        """(address, type) for a cheat's memory spec: a symbol reference or a raw pointer chain."""
        if "symbol" in spec:
            name = spec["symbol"]
            sym = self.game.hook.get("symbols", {}).get(name, {})
            if name not in self.symbols:
                why = self.unresolved.get(name, "symbol is not part of this plugin")
                raise HookError(f"'{name}' isn't available: {why}")
            addr, vtype = self.symbols[name], spec.get("type") or sym.get("type", "int32")
            for off in sym.get("deref", []):
                addr = struct.unpack("<Q", self.proc.read(addr, 8))[0] + int(off)
            return addr, vtype
        return self.proc.resolve(spec), spec.get("type", "int32")

    def read(self, spec: dict):
        with self._lock:
            if not self.attached:
                raise HookError("not hooked")
            if self.dry_run:
                return self._fake.get(spec.get("symbol", "raw"), 0)
            addr, vtype = self._address(spec)
            return self.proc.read_value(addr, vtype)

    def write(self, spec: dict, value) -> None:
        with self._lock:
            if not self.attached:
                raise HookError("not hooked")
            value = value * spec.get("scale", 1)
            if self.dry_run:
                self._fake[spec.get("symbol", "raw")] = value
                self.log(f"[dry-run] memory {spec.get('symbol', 'raw')} = {value}")
                return
            addr, vtype = self._address(spec)
            self.proc.write_value(addr, value, vtype)
