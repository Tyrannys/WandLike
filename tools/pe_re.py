"""Static analysis helpers for a game exe (no debugger needed): load the PE image, find MSVC RTTI vtables,
disassemble functions with capstone. Used to locate code to build "hook" byte patterns from.

    from pe_re import Image
    img = Image(r"F:\\...\\ck3.exe")
    vt = img.vtable("VCPauseGameCommand")      # RVA of the first virtual function slot
    for fn in img.vtable_functions(vt): print(img.disasm(fn, 60))
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_64, Cs


class Image:
    def __init__(self, path: str):
        raw = Path(path).read_bytes()
        pe = struct.unpack_from("<I", raw, 0x3C)[0]
        nsec, = struct.unpack_from("<H", raw, pe + 6)
        opt = struct.unpack_from("<H", raw, pe + 20)[0]
        self.base, = struct.unpack_from("<Q", raw, pe + 24 + 24)
        size_img, = struct.unpack_from("<I", raw, pe + 24 + 56)
        self.mem = bytearray(size_img)
        self.sections = []
        off = pe + 24 + opt
        for _ in range(nsec):
            name = raw[off:off + 8].rstrip(b"\0").decode()
            vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", raw, off + 8)
            flags, = struct.unpack_from("<I", raw, off + 36)
            self.mem[rva:rva + rawsize] = raw[rawptr:rawptr + rawsize]
            self.sections.append((name, rva, max(vsize, rawsize), flags))
            off += 40
        self.cs = Cs(CS_ARCH_X86, CS_MODE_64)
        self.cs.detail = False

    def sec(self, name: str):
        return next(s for s in self.sections if s[0] == name)

    def u32(self, rva: int) -> int:
        return struct.unpack_from("<I", self.mem, rva)[0]

    def u64(self, rva: int) -> int:
        return struct.unpack_from("<Q", self.mem, rva)[0]

    def va(self, rva: int) -> int:
        return self.base + rva

    def find_all(self, needle: bytes, lo: int = 0, hi: int | None = None):
        hi = hi or len(self.mem)
        i = self.mem.find(needle, lo, hi)
        while i != -1:
            yield i
            i = self.mem.find(needle, i + 1, hi)

    # ------------------------------------------------------------------ RTTI
    def vtable(self, class_name: str) -> int | None:
        """RVA of the first virtual-function slot of a class with MSVC RTTI (name without the .?AV prefix)."""
        td_name = b".?AV" + class_name.encode() + b"@@"
        s = self.mem.find(td_name)
        if s < 0:
            return None
        td = s - 16                                   # TypeDescriptor starts 16 bytes before the name
        rdata = self.sec(".rdata")
        # Complete Object Locator: signature=1, offset, cdOffset, pTypeDescriptor(rva), pClassDescriptor(rva), pSelf(rva)
        for i in self.find_all(struct.pack("<I", td), rdata[1], rdata[1] + rdata[2]):
            col = i - 12
            if col > 0 and self.u32(col) == 1 and self.u32(col + 20) == col:
                col_va = struct.pack("<Q", self.va(col))
                for j in self.find_all(col_va, rdata[1], rdata[1] + rdata[2]):
                    return j + 8                       # vtable[-1] holds the COL pointer
        return None

    def vtable_functions(self, vt_rva: int, max_slots: int = 64) -> list[int]:
        out = []
        text = self.sec(".text")
        for k in range(max_slots):
            p = self.u64(vt_rva + 8 * k)
            r = p - self.base
            if not (text[1] <= r < text[1] + text[2]):
                break
            out.append(r)
        return out

    # ------------------------------------------------------------ disassembly
    def disasm(self, rva: int, count: int = 80, annotate: bool = True) -> str:
        code = bytes(self.mem[rva:rva + count * 15])
        lines = []
        for n, ins in enumerate(self.cs.disasm(code, self.va(rva))):
            if n >= count:
                break
            text = f"{ins.address - self.base:08x}  {ins.bytes.hex():<24} {ins.mnemonic} {ins.op_str}"
            m = re.search(r"rip ([+-]) 0x([0-9a-f]+)\]", ins.op_str)
            if annotate and m:
                tgt = ins.address + ins.size + (int(m.group(2), 16) * (1 if m.group(1) == "+" else -1))
                text += f"    ; -> {tgt - self.base:#x}"
            lines.append(text)
            if ins.mnemonic == "ret" and n > 3:
                pass
        return "\n".join(lines)


if __name__ == "__main__":
    img = Image(sys.argv[1])
    print("base", hex(img.base), [(s[0], hex(s[1]), hex(s[2])) for s in img.sections])
    for cls in sys.argv[2:]:
        vt = img.vtable(cls)
        print(cls, "vtable rva", hex(vt) if vt else None)
        if vt:
            for f in img.vtable_functions(vt):
                print("  fn", hex(f))
