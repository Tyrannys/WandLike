"""Process-memory access: read/write, pointer chains, region walking, PE sections and AOB scanning.

A raw pointer spec is
    {"module": "game.exe", "offsets": [0x1234, 0x10, 0x8], "type": "int32"}
The first offset is added to the module base; every following offset is applied after dereferencing a
pointer. A single offset means a static address. Plugins normally use named hook symbols instead
(see hook.py), which are located by byte pattern so they survive game patches.
"""
from __future__ import annotations

import ctypes
import re
import struct
from ctypes import wintypes
from typing import Iterator

from . import winapi

# Least rights that still allow read, write, region queries and waiting on the process.
ACCESS = 0x0010 | 0x0020 | 0x0008 | 0x0400 | 0x00100000  # VM_READ | VM_WRITE | VM_OPERATION | QUERY_INFORMATION | SYNCHRONIZE
kernel32 = winapi.kernel32
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                        ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]

TYPES = {"int32": "<i", "uint32": "<I", "int64": "<q", "uint64": "<Q", "float": "<f", "double": "<d",
         "int16": "<h", "uint16": "<H", "uint8": "<B", "int8": "<b"}

MEM_COMMIT, PAGE_GUARD, PAGE_NOACCESS = 0x1000, 0x100, 0x01
WRITABLE = 0x04 | 0x08 | 0x40 | 0x80   # READWRITE | WRITECOPY | EXECUTE_READWRITE | EXECUTE_WRITECOPY
READABLE = 0x02 | 0x04 | 0x08 | 0x20 | 0x40 | 0x80


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p), ("AllocationProtect", wintypes.DWORD),
                ("PartitionId", wintypes.WORD), ("RegionSize", ctypes.c_size_t), ("State", wintypes.DWORD),
                ("Protect", wintypes.DWORD), ("Type", wintypes.DWORD)]


kernel32.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
kernel32.VirtualQueryEx.restype = ctypes.c_size_t


class MemoryError_(RuntimeError):
    pass


class Process:
    def __init__(self, pid: int):
        self.pid = pid
        self.handle = kernel32.OpenProcess(ACCESS, False, pid)
        if not self.handle:
            raise MemoryError_(f"could not open process {pid} (access denied: is the game running as administrator?)")

    def close(self) -> None:
        if self.handle:
            kernel32.CloseHandle(self.handle)
            self.handle = None

    def alive(self) -> bool:
        return bool(self.handle) and kernel32.WaitForSingleObject(self.handle, 0) == 0x102  # WAIT_TIMEOUT: still running

    # ----------------------------------------------------------- raw access
    def read(self, addr: int, size: int) -> bytes:
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(self.handle, addr, buf, size, ctypes.byref(n)) or n.value != size:
            raise MemoryError_(f"read failed at {addr:#x}")
        return buf.raw

    def try_read(self, addr: int, size: int) -> bytes | None:
        try:
            return self.read(addr, size)
        except MemoryError_:
            return None

    def write(self, addr: int, data: bytes) -> None:
        n = ctypes.c_size_t()
        if not kernel32.WriteProcessMemory(self.handle, addr, data, len(data), ctypes.byref(n)):
            raise MemoryError_(f"write failed at {addr:#x}")

    # --------------------------------------------------------------- values
    def read_value(self, addr: int, vtype: str = "int32"):
        fmt = TYPES[vtype]
        return struct.unpack(fmt, self.read(addr, struct.calcsize(fmt)))[0]

    def write_value(self, addr: int, value, vtype: str = "int32") -> None:
        fmt = TYPES[vtype]
        self.write(addr, struct.pack(fmt, float(value) if fmt[-1] in "fd" else int(value)))

    # -------------------------------------------------------- pointer chains
    def resolve(self, spec: dict) -> int:
        offsets = [int(o) if not isinstance(o, str) else int(o, 16) for o in spec["offsets"]]
        base = winapi.module_base(self.pid, spec["module"]) if spec.get("module") else 0
        if base is None:
            raise MemoryError_(f"module {spec['module']} not found")
        addr = base + offsets[0]
        for off in offsets[1:]:
            addr = struct.unpack("<Q", self.read(addr, 8))[0] + off
        return addr

    def get(self, spec: dict):
        return self.read_value(self.resolve(spec), spec.get("type", "int32"))

    def set(self, spec: dict, value) -> None:
        self.write_value(self.resolve(spec), value, spec.get("type", "int32"))

    # ---------------------------------------------------------- inspection
    def regions(self, writable_only: bool = False, lo: int = 0, hi: int = 0x7FFFFFFEFFFF) -> Iterator[tuple[int, int, int, int]]:
        """Committed, readable regions as (base, size, protect, type)."""
        addr, mbi = lo, MBI()
        while addr < hi and kernel32.VirtualQueryEx(self.handle, addr, ctypes.byref(mbi), ctypes.sizeof(mbi)):
            base, size = int(mbi.BaseAddress or 0), int(mbi.RegionSize)
            prot = int(mbi.Protect)
            if (mbi.State == MEM_COMMIT and not prot & (PAGE_GUARD | PAGE_NOACCESS) and prot & READABLE
                    and (not writable_only or prot & WRITABLE)):
                yield base, size, prot, int(mbi.Type)
            addr = base + size

    def read_region_chunks(self, base: int, size: int, chunk: int = 1 << 22) -> Iterator[tuple[int, bytes]]:
        """Read a region in chunks, skipping unreadable pages instead of failing."""
        off = 0
        while off < size:
            n = min(chunk, size - off)
            data = self.try_read(base + off, n)
            if data is not None:
                yield base + off, data
            off += n

    def pe_sections(self, base: int) -> list[dict]:
        """Section table of the PE image mapped at `base` (name, rva, size, flags)."""
        hdr = self.read(base, 0x1000)
        pe = struct.unpack_from("<I", hdr, 0x3C)[0]
        if hdr[pe:pe + 4] != b"PE\0\0":
            raise MemoryError_("not a PE image")
        nsec, = struct.unpack_from("<H", hdr, pe + 6)
        opt_size, = struct.unpack_from("<H", hdr, pe + 20)
        out, off = [], pe + 24 + opt_size
        for _ in range(nsec):
            name = hdr[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, rva, _, _, _, _, _, _, flags = struct.unpack_from("<IIIIIIHHI", hdr, off + 8)
            out.append({"name": name, "rva": rva, "size": vsize, "flags": flags,
                        "writable": bool(flags & 0x80000000), "executable": bool(flags & 0x20000000)})
            off += 40
        return out

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


# ------------------------------------------------------------------ patterns
def compile_pattern(pattern: str) -> re.Pattern:
    """'48 8B ?? 05' -> compiled bytes regex ('??' / '?' match any byte)."""
    parts = pattern.split()
    if not parts:
        raise ValueError("empty pattern")
    rx = b""
    for p in parts:
        if p in ("?", "??"):
            rx += b"."
        elif re.fullmatch(r"[0-9A-Fa-f]{2}", p):
            rx += re.escape(bytes([int(p, 16)]))
        else:
            raise ValueError(f"bad byte {p!r} in pattern")
    return re.compile(rx, re.DOTALL)


def scan_bytes(data: bytes, pattern: str, limit: int = 8) -> list[int]:
    """Offsets of up to `limit` matches of `pattern` in `data` (overlapping matches included)."""
    rx, out, pos = compile_pattern(pattern), [], 0
    while len(out) < limit and (m := rx.search(data, pos)):
        out.append(m.start())
        pos = m.start() + 1
    return out
