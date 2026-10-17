"""Generate assets/app.ico (teal rounded square + lightning bolt) with no dependencies."""
import struct
import sys
import zlib
from pathlib import Path

N = 256
BOLT = [(150, 28), (66, 146), (118, 146), (98, 228), (192, 100), (138, 100)]


def inside(poly, x, y):
    c = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            c = not c
        j = i
    return c


def rounded(x, y, r=48, lo=8, hi=N - 8):
    cx = min(max(x, lo + r), hi - r)
    cy = min(max(y, lo + r), hi - r)
    return lo <= x <= hi and lo <= y <= hi and (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def pixel(x, y):
    # 2x2 supersample for soft edges
    acc = [0, 0, 0, 0]
    for dx in (0.25, 0.75):
        for dy in (0.25, 0.75):
            px, py = x + dx, y + dy
            if inside(BOLT, px, py):
                c = (232, 234, 238, 255)
            elif rounded(px, py):
                t = py / N
                c = (int(31 - 16 * t), int(122 - 61 * t), int(140 - 69 * t), 255)
            else:
                c = (0, 0, 0, 0)
            a = c[3]
            acc = [acc[0] + c[0] * a, acc[1] + c[1] * a, acc[2] + c[2] * a, acc[3] + a]
    if acc[3] == 0:
        return (0, 0, 0, 0)
    return (acc[0] // acc[3], acc[1] // acc[3], acc[2] // acc[3], acc[3] // 4)


def png():
    raw = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(N)) for y in range(N))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(out):
    data = png()
    ico = struct.pack("<HHH", 0, 1, 1) + struct.pack("<BBBBHHII", 0, 0, 0, 0, 1, 32, len(data), 22) + data
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_bytes(ico)
    print("wrote", out, len(ico), "bytes")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "assets/app.ico")
