#!/usr/bin/env python3
"""Decode T:r200_batchN.bin dumps written by DumpBatchFile (R200 backend).

Layout: 24-byte header (magic 'R2B1', batchIndex, recordDwords, commitMode,
segmentCursor, drawCount), then recordDwords big-endian ULONGs (the record
chain), then segmentCursor bytes of segment payload when commitMode != 0.

Record layout (opcode 2..9 draws):
  [0] opcode [1] total length [2] color surface id [3] depth surface id
  [4] texture surface id [5] options [6..9] scissor rect l,t,r,b
  [10] vertexCount [11..] fragment/texture state (fragment header)
  extended: [15] vertexState [16] fogColor [17] tex1 state ...
  HW-TCL header is 44 dwords; TCL matrices follow at [44..75], normals at
  [76..107] when NORMALS set.
Vertices follow the header (inline mode) as IEEE-754 big-endian floats.
"""
import math
import struct
import sys

OPCODES = {1: "CLEAR", 2: "TRIANGLES", 3: "TRI_STRIP", 4: "TRI_FAN",
           5: "QUADS", 6: "POINTS", 7: "LINES", 8: "LINE_STRIP",
           9: "LINE_LOOP"}
OPT_BITS = [(1 << 0, "TEXTURED"), (1 << 1, "EXTENDED_VERTEX"),
            (1 << 2, "HW_TCL"), (1 << 3, "BILINEAR"), (1 << 4, "ALPHA_BLEND"),
            (1 << 5, "DEPTH_LESS"), (1 << 6, "DEPTH_WRITE"),
            (1 << 7, "COMPACT_VERTEX"), (1 << 8, "NORMALS"),
            (1 << 9, "TEXGEN"), (1 << 10, "LIGHTING")]


def options_str(v):
    return "|".join(name for bit, name in OPT_BITS if v & bit) or "none"


def f32(x):
    return struct.unpack(">f", struct.pack(">I", x))[0]


def decode(path):
    data = open(path, "rb").read()
    magic, batch, recdwords, commit, segcursor, drawcount = struct.unpack(
        ">6I", data[:24])
    if magic != 0x52324231:
        print(f"{path}: bad magic {magic:#x}")
        return
    print(f"== {path}: batch={batch} records={recdwords}dwords "
          f"commit={commit} segCursor={segcursor} draws={drawcount}")
    words = list(struct.unpack(f">{recdwords}I", data[24:24 + 4 * recdwords]))
    off = 0
    draw_index = 0
    while off < len(words):
        op = words[off]
        length = words[off + 1]
        if op == 1:
            print(f"[{off:5}] CLEAR len={length}")
            off += length
            continue
        name = OPCODES.get(op, f"op{op}")
        opts = words[off + 5]
        rect = words[off + 6:off + 10]
        vcount = words[off + 10]
        hw_tcl = bool(opts & (1 << 2))
        ext = bool(opts & (1 << 1))
        frag = not ext and length > 15
        if hw_tcl:
            header_dwords = 44
        elif ext:
            header_dwords = 21
        elif frag:
            header_dwords = 15
        else:
            header_dwords = 11
        normal_vertex = bool(opts & (1 << 8))
        if hw_tcl and normal_vertex:
            header_dwords += 32
        textured = bool(opts & 1)
        compact = bool(opts & (1 << 7)) and False  # untextured uses full
        stride = (10 if not normal_vertex else 13)
        if compact:
            stride = 7 if not normal_vertex else 10
        print(f"[{off:5}] {name} len={length} opts={opts:#x}({options_str(opts)}) "
              f"rect=({rect[0]},{rect[1]})-({rect[2]},{rect[3]}) "
              f"vcount={vcount} hdr={header_dwords} stride={stride} "
              f"draw#{draw_index}")
        if length < header_dwords + (0 if commit else vcount * stride):
            print(f"       !! length shorter than header+vertices")
        # a few interesting header dwords
        if frag:
            print(f"       texOff={words[off+11]:#x} wh="
                  f"({(words[off+12]&0xffff)+1},{(words[off+12]>>16)+1})")
        if ext:
            print(f"       vertexState={words[off+15]:#x} "
                  f"fogColor={words[off+16]:#08x}")
        if hw_tcl:
            mvp = [f32(w) for w in words[off + 44:off + 60]]
            print(f"       MVP row0={[round(v,3) for v in mvp[:4]]}")
            print(f"       MVP row1={[round(v,3) for v in mvp[4:8]]}")
            print(f"       MVP row2={[round(v,3) for v in mvp[8:12]]}")
            print(f"       MVP row3={[round(v,3) for v in mvp[12:16]]}")
        if not commit and vcount:
            base = off + header_dwords
            shown = min(vcount, 6)
            for v in range(shown):
                vs = words[base + v * stride:base + (v + 1) * stride]
                vals = [f32(w) for w in vs[:4]]
                print(f"       v{v}: xy=({vals[0]:.2f},{vals[1]:.2f}) "
                      f"z={vals[2]:.3f} rhw={vals[3]:.3f} "
                      f"argb={vs[4]:#010x}"
                      + ("" if stride <= 5 else
                         f" st=({f32(vs[5]):.2f},{f32(vs[6]):.2f})"
                         if stride >= 7 else ""))
            if vcount > shown:
                print(f"       ... {vcount - shown} more vertices")
        off += length
        draw_index += 1


if __name__ == "__main__":
    for p in sys.argv[1:]:
        decode(p)
