#!/usr/bin/env python3
"""Census a PPM: report non-black pixel bounding boxes and mean colors,
splitting the frame into a coarse grid so misplaced geometry shows up."""
import sys

import struct


def census(path):
    data = open(path, "rb").read()
    # header P6\nW H\n255\n
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pix = parts[3]
    assert len(pix) >= w * h * 3, (len(pix), w * h * 3)
    xs, ys = [], []
    colors = {}
    for y in range(h):
        row = pix[y * w * 3:(y + 1) * w * 3]
        for x in range(w):
            r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            if r or g or b:
                xs.append(x)
                ys.append(y)
                colors[(r >> 4, g >> 4, b >> 4)] = \
                    colors.get((r >> 4, g >> 4, b >> 4), 0) + 1
    print(f"== {path} {w}x{h}")
    if not xs:
        print("   EMPTY (all black)")
        return
    top = sorted(colors.items(), key=lambda kv: -kv[1])[:5]
    print(f"   non-black: {len(xs)} px  bbox=({min(xs)},{min(ys)}).."
          f"({max(xs)},{max(ys)})")
    print("   top colors (4-bit rgb : count):",
          ", ".join(f"{c}:{n}" for c, n in top))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        census(p)
