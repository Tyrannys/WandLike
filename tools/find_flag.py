"""Find a game's on/off flag (pause, god mode, ...) by differencing memory snapshots.

    python tools/find_flag.py --exe ck3.exe --window "Crusader Kings III" --key space [--heap] [--cycles 6]

Start with the flag in its *off-state* (e.g. game paused). The tool presses --key to flip it, waits, and
snapshots after every flip. A byte is a candidate if it is identical in every off-state snapshot,
identical in every on-state snapshot, and different between the two. Output: candidates with their
module RVA / section, ready to put in a plugin's "hook.symbols" (use `find_code_refs` to build a pattern).
"""
from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from wandlike.core import winapi  # noqa: E402
from wandlike.core.memory import Process  # noqa: E402

_NONZERO = bytes(0 if i == 0 else 1 for i in range(256))


def snapshot(proc: Process, ranges: list[tuple[int, int]]) -> dict[int, bytes]:
    snap = {}
    for base, size in ranges:
        buf = bytearray(size)
        for addr, data in proc.read_region_chunks(base, size):
            buf[addr - base:addr - base + len(data)] = data
        snap[base] = bytes(buf)
    return snap


PAGE = 4096


def page_hashes(proc: Process, ranges: list[tuple[int, int]]) -> dict[int, bytes]:
    """address -> 8-byte hash of each 4 KB page (cheap enough to cover gigabytes of heap)."""
    import hashlib
    out: dict[int, bytes] = {}
    for base, size in ranges:
        for addr, data in proc.read_region_chunks(base, size):
            for off in range(0, len(data) - PAGE + 1, PAGE):
                out[addr + off] = hashlib.blake2b(data[off:off + PAGE], digest_size=8).digest()
    return out


def stage1_pages(proc: Process, ranges, flip, cycles: int) -> list[int]:
    """Pages that are identical in every off-state snapshot yet different in every on-state snapshot."""
    off, on = [page_hashes(proc, ranges)], []
    for i in range(cycles):
        flip()
        on.append(page_hashes(proc, ranges))
        flip()
        off.append(page_hashes(proc, ranges))
        print(f"  stage1 cycle {i + 1}/{cycles}")
    keep = []
    for addr, h in off[0].items():
        if all(o.get(addr) == h for o in off[1:]) and all(o.get(addr, h) != h for o in on):
            keep.append(addr)
    return keep


def diff_mask(a: bytes, b: bytes) -> bytes:
    """One byte per input byte: 1 where a and b differ."""
    x = (int.from_bytes(a, "little") ^ int.from_bytes(b, "little")).to_bytes(len(a), "little")
    return x.translate(_NONZERO)


def candidates(off_snaps: list[dict], on_snaps: list[dict]) -> list[tuple[int, int, int]]:
    """(address, off_value, on_value) for bytes constant per state but different between states."""
    out = []
    for base in off_snaps[0]:
        size = len(off_snaps[0][base])
        same_off = bytearray(b"\x01" * size)
        same_on = bytearray(b"\x01" * size)
        for s in off_snaps[1:]:
            m = diff_mask(off_snaps[0][base], s[base])
            same_off = bytearray(a & (1 - b) for a, b in zip(same_off, m)) if size < 4096 else _and_not(same_off, m)
        for s in on_snaps[1:]:
            m = diff_mask(on_snaps[0][base], s[base])
            same_on = _and_not(same_on, m)
        differs = diff_mask(off_snaps[0][base], on_snaps[0][base])
        keep = _and(_and(same_off, same_on), differs)
        for m in re.finditer(b"\x01", bytes(keep)):
            i = m.start()
            out.append((base + i, off_snaps[0][base][i], on_snaps[0][base][i]))
    return out


def _and_not(a: bytearray, b: bytes) -> bytearray:
    n = len(a)
    r = (int.from_bytes(a, "little") & ~int.from_bytes(b, "little") & int.from_bytes(b"\x01" * n, "little"))
    return bytearray(r.to_bytes(n, "little"))


def _and(a: bytearray, b: bytearray) -> bytearray:
    n = len(a)
    return bytearray((int.from_bytes(a, "little") & int.from_bytes(b, "little")).to_bytes(n, "little"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--window", required=True)
    ap.add_argument("--key", default="space", help="scan-code name from winapi.SCAN that flips the flag")
    ap.add_argument("--cycles", type=int, default=5)
    ap.add_argument("--wait", type=float, default=1.5)
    ap.add_argument("--heap", action="store_true", help="also scan private writable memory (slow, large)")
    ap.add_argument("--stage1-cycles", type=int, default=3)
    ap.add_argument("--out", help="write every candidate to this JSON file")
    ns = ap.parse_args()

    pids = winapi.find_pids(ns.exe)
    if not pids:
        print(f"{ns.exe} is not running")
        return 1
    hwnd = winapi.find_window(ns.window, pids)
    if not hwnd:
        print("game window not found")
        return 1
    with Process(pids[0]) as proc:
        base, size = winapi.module_range(pids[0], ns.exe)
        secs = [s for s in proc.pe_sections(base) if s["writable"]]
        ranges = [(base + s["rva"], s["size"]) for s in secs]
        print(f"module {ns.exe} @ {base:#x} size {size}; writable sections: "
              + ", ".join(f"{s['name']}({s['size'] // 1024} KB)" for s in secs))
        if ns.heap:
            ranges += [(b, s) for b, s, _, t in proc.regions(writable_only=True) if t == 0x20000 and s <= 64 << 20]
        total = sum(s for _, s in ranges)
        print(f"scanning {total / 1048576:.1f} MB over {len(ranges)} ranges, {ns.cycles} cycles")
        scan = winapi.SCAN[ns.key]

        def flip():
            winapi.focus_window(hwnd)
            winapi.tap_scan(scan)
            time.sleep(ns.wait)

        if ns.heap:      # too big to keep whole snapshots: first narrow down to the pages that behave
            pages = stage1_pages(proc, ranges, flip, ns.stage1_cycles)
            print(f"stage 1 kept {len(pages)} pages ({len(pages) * PAGE / 1048576:.1f} MB)")
            ranges = [(a, PAGE) for a in pages]

        off, on = [], []
        time.sleep(0.5)
        off.append(snapshot(proc, ranges))
        for i in range(ns.cycles):
            flip()
            on.append(snapshot(proc, ranges))
            flip()
            off.append(snapshot(proc, ranges))
            print(f"  cycle {i + 1}/{ns.cycles}")
        cands = candidates(off, on)
        print(f"{len(cands)} candidate bytes")
        # booleans (0/1 or 0/nonzero) are the likely flags: list them first
        cands.sort(key=lambda c: (0 if {c[1], c[2]} <= {0, 1} else 1 if 0 in (c[1], c[2]) and max(c[1], c[2]) < 16 else 2, c[0]))
        if ns.out:
            import json
            Path(ns.out).write_text(json.dumps([{"addr": hex(a), "rva": a - base, "off": o, "on": n} for a, o, n in cands]))
        for addr, ov, nv in cands[:40]:
            sec = next((s["name"] for s in secs if base + s["rva"] <= addr < base + s["rva"] + s["size"]), "heap")
            print(f"  {addr:#x}  rva={addr - base:#x}  [{sec}]  off={ov} on={nv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
