#!/usr/bin/env python3
"""Offline reference render of the testsuite ogre using the exact same
matrices as DrawQuakeOgre, to diagnose winding and skin alignment."""

import math
import struct
import sys

sys.path.insert(0, "tools")
from extract_quake_mdl import read_pak_entry, parse_mdl, palette_from

W, H = 640, 480


def transform(vertex, yaw_deg):
    """M = T(0,0,-4.2) . Ry(yaw) . Rx(-82) . S(0.035) . T(7,4,-5)."""
    x, y, z = vertex
    # T(7,4,-5) then S(0.035), both in object space.
    x, y, z = (x + 7.0) * 0.035, (y + 4.0) * 0.035, (z - 5.0) * 0.035
    a = math.radians(-82.0)
    y, z = y * math.cos(a) - z * math.sin(a), y * math.sin(a) + z * math.cos(a)
    b = math.radians(yaw_deg)
    x, z = x * math.cos(b) + z * math.sin(b), -x * math.sin(b) + z * math.cos(b)
    z -= 4.2
    # glFrustum(-1, 1, -0.625, 0.625, 1, 20): clip.x = x, clip.y = 1.6y, w = -z
    w = -z
    if w <= 0.001:
        return None
    return x / w, 1.6 * y / w, z


def render(parsed, palette, keep, yaw, out):
    (vertices, texcoords, triangles, sw, sh, skin) = parsed
    fb = [(0.015, 0.02, 0.035)] * (W * H)
    zbuf = [-1e9] * (W * H)
    for facesfront, a, b, c in triangles:
        pts = []
        for vi in (a, b, c):
            p = transform(vertices[vi], yaw)
            if p is None:
                pts = None
                break
            sx = (p[0] * 0.5 + 0.5) * W
            sy = (1.0 - (p[1] * 0.5 + 0.5)) * H
            pts.append((sx, sy, p[2], vi))
        if pts is None:
            continue
        (x0, y0, z0, _), (x1, y1, z1, _), (x2, y2, z2, _) = pts
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        # GL window coords: y down. CCW in NDC (y up) => area<0 here.
        front_ccw = area < 0
        if keep == "front" and not front_ccw:
            continue
        if keep == "back" and front_ccw:
            continue
        minx = max(0, int(min(x0, x1, x2)))
        maxx = min(W - 1, int(max(x0, x1, x2)) + 1)
        miny = max(0, int(min(y0, y1, y2)))
        maxy = min(H - 1, int(max(y0, y1, y2)) + 1)
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        for py in range(miny, maxy + 1):
            for px in range(minx, maxx + 1):
                w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den
                w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den
                w2 = 1.0 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                depth = w0 * z0 + w1 * z1 + w2 * z2
                if depth <= zbuf[py * W + px]:
                    continue
                zbuf[py * W + px] = depth
                st = []
                for vi in (a, b, c):
                    onseam, s, t = texcoords[vi]
                    if not facesfront and onseam:
                        s += sw // 2
                    st.append(((s + 0.5) / sw, (t + 0.5) / sh))
                su = w0 * st[0][0] + w1 * st[1][0] + w2 * st[2][0]
                sv = w0 * st[0][1] + w1 * st[1][1] + w2 * st[2][1]
                tx = min(sw - 1, max(0, int(su * sw)))
                ty = min(sh - 1, max(0, int(sv * sh)))
                r, g, b_ = palette[skin[ty * sw + tx]]
                fb[py * W + px] = (r / 255.0, g / 255.0, b_ / 255.0)
    with open(out, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        f.write(bytes(bytearray(
            tuple(int(v * 255) for px in fb for v in px))))
    print("wrote", out)


def main():
    pak = "/home/mirek/.wine/drive_c/GOG Games/Quake/id1/PAK0.PAK"
    parsed = parse_mdl(read_pak_entry(pak, "progs/ogre.mdl"))
    palette = palette_from(pak)
    render(parsed, palette, "front", 18.0, "/tmp/opencode/ogre_keepfront.ppm")
    render(parsed, palette, "back", 18.0, "/tmp/opencode/ogre_keepback.ppm")


if __name__ == "__main__":
    main()
