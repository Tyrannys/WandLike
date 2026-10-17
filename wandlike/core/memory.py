"""Process-memory backend: read/write values through pointer chains.

Used by game plugins whose cheats can't be done via a console. A pointer spec is
    {"module": "game.exe", "offsets": [0x1234, 0x10, 0x8], "type": "int32"}
The first offset is added to the module base; every following offset is applied after
dereferencing a pointer. A single offset means a static address.
"""
from __future__ import annotations

import ctypes
import struct
from ctypes import wintypes

from . import winapi

PROCESS_ALL_ACCESS = 0x1F0FFF
kernel32 = winapi.kernel32
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                        ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]

TYPES = {"int32": "<i", "uint32": "<I", "int64": "<q", "float": "<f", "double": "<d",
         "int16": "<h", "uint8": "<B"}


class MemoryError_(RuntimeError):
    pass


class Process:
    def __init__(self, pid: int):
        self.pid = pid
        self.handle = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
        if not self.handle:
            raise MemoryError_(f"OpenProcess({pid}) failed (run as administrator?)")

    def close(self) -> None:
        if self.handle:
            kernel32.CloseHandle(self.handle)
            self.handle = None

    def read(self, addr: int, size: int) -> bytes:
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(self.handle, addr, buf, size, ctypes.byref(n)) or n.value != size:
            raise MemoryError_(f"read failed at {addr:#x}")
        return buf.raw

    def write(self, addr: int, data: bytes) -> None:
        n = ctypes.c_size_t()
        if not kernel32.WriteProcessMemory(self.handle, addr, data, len(data), ctypes.byref(n)):
            raise MemoryError_(f"write failed at {addr:#x}")

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
        fmt = TYPES[spec.get("type", "int32")]
        return struct.unpack(fmt, self.read(self.resolve(spec), struct.calcsize(fmt)))[0]

    def set(self, spec: dict, value) -> None:
        fmt = TYPES[spec.get("type", "int32")]
        v = float(value) if fmt in ("<f", "<d") else int(value)
        self.write(self.resolve(spec), struct.pack(fmt, v))

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
