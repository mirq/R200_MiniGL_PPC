#!/usr/bin/env python3
"""Extract one simple-frame Quake alias model as compact C test data.

Emits geometry plus the model's first skin expanded through gfx/palette.lmp,
and per-corner texture coordinates that apply Quake's onseam rule so the
model renders exactly like the in-game software renderer.
"""

import argparse
import struct


def read_pak_entry(path, wanted):
    with open(path, "rb") as pak:
        magic, directory_offset, directory_size = struct.unpack("<4sII", pak.read(12))
        if magic != b"PACK" or directory_size % 64:
            raise ValueError("not a Quake PAK")
        pak.seek(directory_offset)
        for _ in range(directory_size // 64):
            raw_name, offset, size = struct.unpack("<56sII", pak.read(64))
            name = raw_name.split(b"\0", 1)[0].decode("ascii")
            if name == wanted:
                pak.seek(offset)
                return pak.read(size)
    raise ValueError("entry not found: " + wanted)


def parse_mdl(data):
    header = struct.unpack_from("<ii3f3ff3f8if", data, 0)
    if header[0] != 0x4F504449 or header[1] != 6:
        raise ValueError("unsupported MDL")
    scale = header[2:5]
    origin = header[5:8]
    skins, skin_width, skin_height = header[12:15]
    vertex_count, triangle_count = header[15:17]
    offset = 84
    skin_data = None
    for index in range(skins):
        group = struct.unpack_from("<i", data, offset)[0]
        offset += 4
        if group:
            count = struct.unpack_from("<i", data, offset)[0]
            offset += 4 + count * 4
            if index == 0:
                skin_data = data[offset:offset + skin_width * skin_height]
            offset += count * skin_width * skin_height
        else:
            if index == 0:
                skin_data = data[offset:offset + skin_width * skin_height]
            offset += skin_width * skin_height
    texcoords = []
    for _ in range(vertex_count):
        onseam, s, t = struct.unpack_from("<iii", data, offset)
        offset += 12
        texcoords.append((onseam, s, t))
    triangles = []
    for _ in range(triangle_count):
        facesfront, a, b, c = struct.unpack_from("<iiii", data, offset)
        offset += 16
        triangles.append((facesfront, a, b, c))
    frame_type = struct.unpack_from("<i", data, offset)[0]
    offset += 4
    if frame_type:
        raise ValueError("first grouped frame is not supported")
    offset += 8 + 16
    vertices = []
    for _ in range(vertex_count):
        x, y, z, _ = struct.unpack_from("<BBBB", data, offset)
        offset += 4
        vertices.append((x * scale[0] + origin[0],
                         y * scale[1] + origin[1],
                         z * scale[2] + origin[2]))
    return (vertices, texcoords, triangles, skin_width, skin_height,
            skin_data)


def palette_from(pak):
    raw = read_pak_entry(pak, "gfx/palette.lmp")
    if len(raw) < 768:
        raise ValueError("palette.lmp too short")
    return [(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]) for i in range(256)]


def rows(values, width):
    for start in range(0, len(values), width):
        yield values[start:start + width]


def write_header(path, model, parsed, palette):
    (vertices, texcoords, triangles, skin_width, skin_height,
     skin_data) = parsed
    guard = "QUAKE_OGRE_MODEL_H"
    # Quake lays the skin out with the back beside the front, so a back-facing
    # triangle over an onseam vertex shifts half the skin WIDTH in S
    # (r_alias.c: s += skinwidth/2). That makes ST a property of the triangle
    # corner, not of the vertex, which indexed drawing cannot express: every
    # array is addressed by the same index. Duplicate each onseam vertex that
    # is used both ways so per-vertex ST stays correct under glDrawElements.
    flat_triangles = []
    positions = []
    seam_st = []
    expanded = {}
    for facesfront, a, b, c in triangles:
        for vertex in (a, b, c):
            onseam, s, t = texcoords[vertex]
            shifted = bool(onseam) and not facesfront
            key = (vertex, shifted)
            if key not in expanded:
                expanded[key] = len(positions)
                positions.append(vertices[vertex])
                seam_st.append((((s + skin_width // 2 if shifted else s) + 0.5)
                                / skin_width, (t + 0.5) / skin_height))
            flat_triangles.append(expanded[key])
    with open(path, "w", encoding="ascii", newline="\n") as out:
        out.write("/* Generated from %s by tools/extract_quake_mdl.py. */\n" % model)
        out.write("#ifndef %s\n#define %s\n\n" % (guard, guard))
        out.write("#define QUAKE_OGRE_VERTEX_COUNT %d\n" % len(positions))
        out.write("#define QUAKE_OGRE_INDEX_COUNT %d\n" % len(flat_triangles))
        out.write("#define QUAKE_OGRE_SKIN_WIDTH %d\n" % skin_width)
        out.write("#define QUAKE_OGRE_SKIN_HEIGHT %d\n\n" % skin_height)
        out.write("static const GLfloat g_quakeOgreXYZ[][3] = {\n")
        for x, y, z in positions:
            out.write("    {%.6ff, %.6ff, %.6ff},\n" % (x, y, z))
        out.write("};\n\n/* ST with Quake's onseam rule already resolved; "
                  "seam vertices used by both\n * front- and back-facing "
                  "triangles appear twice in the arrays above. */\n")
        out.write("static const GLfloat g_quakeOgreST[][3] = {\n")
        for s, t in seam_st:
            out.write("    {%.8ff, %.8ff, 1.0f},\n" % (s, t))
        out.write("};\n\nstatic const GLushort g_quakeOgreIndices[] = {\n")
        for row in rows(flat_triangles, 15):
            out.write("    " + ", ".join(str(value) for value in row) + ",\n")
        out.write("};\n\n/* Skin 0 expanded through gfx/palette.lmp, RGBA.\n")
        out.write(" *\n * clang-format off\n */\n")
        out.write("static const unsigned char g_quakeOgreSkin[][%d] = {\n"
                  % (skin_width * 4))
        for row in rows(skin_data, skin_width):
            cells = []
            for index in row:
                r, g, b = palette[index]
                cells.append("%d,%d,%d,255" % (r, g, b))
            out.write("    " + ", ".join(cells) + ",\n")
        out.write("};\n\n#endif\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pak")
    parser.add_argument("model")
    parser.add_argument("output")
    args = parser.parse_args()
    parsed = parse_mdl(read_pak_entry(args.pak, args.model))
    if parsed[5] is None:
        raise ValueError("model has no skin")
    write_header(args.output, args.model, parsed, palette_from(args.pak))


if __name__ == "__main__":
    main()
