#!/usr/bin/env python3
"""Extract spawn-area world geometry of a Quake map as C test data.

Reads a BSP29 map from a Quake PAK, collects world faces visible from the
info_player_start through the PVS, and emits examples/quake_room_model.h:
interleaved XYZST vertices, per-texture index batches, mip-expanded RGB
textures, a deterministic camera patrol loop baked per frame, and multi-pose
reference block means rendered offline with the same math the suite's draw
function uses.  minigl_testsuite renders this as an effect-free textured
FPS-style room walkthrough (test quake.world-room-walk).
"""

import argparse
import math
import struct
from typing import List, Optional, Tuple

Colour = Optional[Tuple[int, int, int]]

# Must match minigl_testsuite.c's DrawQuakeRoomWalk projection.
PROJ_HALF_W = 1.0        # tan(hfov/2): 90 degree horizontal FOV
PROJ_HALF_H = 0.625      # tan(vfov/2) at 512x320 square pixels
PROJ_NEAR = 4.0
REF_WIDTH = 800          # reference render size (mutable: see main)
REF_HEIGHT = 500
REF_SUPERSAMPLE = 2      # block stability is measured against this oversampling
BLOCK_STABILITY = 6      # max mean drift under oversampling to trust a block
CONTENTS_EMPTY = -1
CONTENTS_SOLID = -2
EYE_HEIGHT = 22.0        # Quake viewheight above the player origin
LOOP_FRAMES = 256
VERIFY_FRAMES = (0, 64, 128, 192)   # poses checked by the suite
BLOCK_COLS = 4                      # verification grid across the frame
BLOCK_ROWS = 3
BLOCK_INSET = 0.06                  # skip the outer frame edge
MAX_TEX_DIM = 64         # largest stored mip level kept for the test
SUBMODEL_RANGE = 600.0   # keep brush entities this close to the walk
INVISIBLE_TEXTURES = ("trigger", "clip", "skip", "hint")
CULL_SIGN = 1.0          # screen-space winding kept; matches glCullFace(GL_FRONT)
BLOCK_TOLERANCE = 24     # channel tolerance on block means


def read_pak_entry(path, wanted):
    with open(path, "rb") as pak:
        magic, dir_off, dir_size = struct.unpack("<4sII", pak.read(12))
        if magic != b"PACK" or dir_size % 64:
            raise ValueError("not a Quake PAK")
        pak.seek(dir_off)
        for _ in range(dir_size // 64):
            raw_name, offset, size = struct.unpack("<56sII", pak.read(64))
            if raw_name.split(b"\0", 1)[0].decode("ascii") == wanted:
                pak.seek(offset)
                return pak.read(size)
    raise ValueError("entry not found: " + wanted)


class Bsp(object):
    def __init__(self, data):
        self.data = data
        version = struct.unpack_from("<i", data, 0)[0]
        if version != 29:
            raise ValueError("unsupported BSP version %d" % version)
        self.lumps = [struct.unpack_from("<ii", data, 4 + 8 * i) for i in range(15)]
        (entities_l, self.planes_l, self.miptex_l, self.vertexes_l,
         self.visibility_l, self.nodes_l, self.texinfo_l, self.faces_l,
         _lighting, self.clipnodes_l, self.leafs_l, self.marksurfaces_l,
         self.edges_l, self.surfedges_l, self.models_l) = self.lumps
        self.entities = data[entities_l[0]:entities_l[0] + entities_l[1]]
        # dmodel_t: mins[3] maxs[3] origin[3] floats, headnode[4], visleafs,
        # firstface, numfaces -> 64 bytes.  (Model 0 happens to read correctly
        # with a wrong stride because it sits at offset 0; the rest do not.)
        self.models = [struct.unpack_from("<9f7i", data, self.models_l[0] + 64 * i)
                       for i in range(self.models_l[1] // 64)]
        self.leaf_count = self.leafs_l[1] // 28

    def leaf_contents(self, index):
        return struct.unpack_from("<i", self.data, self.leafs_l[0] + 28 * index)[0]

    def leaf_bounds(self, index):
        base = self.leafs_l[0] + 28 * index
        return (struct.unpack_from("<3h", self.data, base + 8),
                struct.unpack_from("<3h", self.data, base + 14))

    def leaf_candidates(self, point):
        """Non-solid leaves whose bounds contain the point."""
        hits = []
        for index in range(self.leaf_count):
            contents = self.leaf_contents(index)
            if contents == -2:
                continue
            mins, maxs = self.leaf_bounds(index)
            if all(mins[i] <= point[i] < maxs[i] for i in range(3)):
                hits.append((index, contents))
        return hits

    def leaf_by_point(self, point):
        hits = self.leaf_candidates(point)
        if len(hits) != 1:
            raise ValueError("point matches %d non-solid leaves" % len(hits))
        return hits[0][0]

    def leaf_index(self, point):
        """Mod_PointInLeaf: children[0] is the front (d >= 0) side."""
        node = self.models[0][9]
        base = self.nodes_l[0]
        while node >= 0:
            planenum = struct.unpack_from("<i", self.data, base + 24 * node)[0]
            front, back = struct.unpack_from("<2h", self.data, base + 24 * node + 4)
            normal, dist = self.plane(planenum)
            side = (normal[0] * point[0] + normal[1] * point[1]
                    + normal[2] * point[2]) - dist
            node = front if side >= 0.0 else back
        return -node - 1

    def plane(self, index):
        """(normal, dist) of dplane_t index."""
        nx, ny, nz, dist = struct.unpack_from(
            "<4f", self.data, self.planes_l[0] + 20 * index)
        return (nx, ny, nz), dist

    def hull_contents(self, point, headnode):
        """SV_HullPointContents: walk the clipnodes of a collision hull.

        Hull 1 is expanded by the player bounding box, so an EMPTY result
        means a player could stand at `point` -- unlike a leaf bounds test,
        which only checks the AABB of a convex leaf and happily reports
        points buried inside walls as clear."""
        node = headnode
        base = self.clipnodes_l[0]
        while node >= 0:
            planenum, front, back = struct.unpack_from("<ihh", self.data,
                                                       base + 8 * node)
            normal, dist = self.plane(planenum)
            side = (normal[0] * point[0] + normal[1] * point[1]
                    + normal[2] * point[2]) - dist
            node = front if side >= 0.0 else back
        return node

    def point_is_clear(self, point):
        """True when the player hull fits at `point` (origin height)."""
        return self.hull_contents(point, self.models[0][10]) == CONTENTS_EMPTY

    def leaf_vis_row(self, leaf_index):
        visofs = struct.unpack_from("<i", self.data,
                                    self.leafs_l[0] + 28 * leaf_index + 4)[0]
        if visofs < 0:
            return None
        row_bytes = (self.models[0][13] + 7) // 8   # dmodel_t.visleafs
        row = bytearray()
        cursor = self.visibility_l[0] + visofs
        data = self.data
        while len(row) < row_bytes:
            code = data[cursor]
            if code:
                row.append(code)
                cursor += 1
            else:
                row.extend(b"\0" * data[cursor + 1])
                cursor += 2
        return bytes(row[:row_bytes])

    def visible_leafs(self, from_leaf):
        row = self.leaf_vis_row(from_leaf)
        if row is None:
            raise ValueError("spawn leaf has no visibility data")
        # qbsp numbers PVS bits over non-solid leafs: bit i is leaf i+1.
        visible = set()
        for bit in range(len(row) * 8):
            if row[bit >> 3] & (1 << (bit & 7)):
                visible.add(bit + 1)
        visible.add(from_leaf)
        return visible


def parse_spawn(entities):
    origin = angle = None
    for block in entities.decode("latin1").split("}"):
        if "info_player_start" not in block:
            continue
        for line in block.split("\n"):
            if '"origin"' in line:
                origin = tuple(float(x) for x in line.split('"')[3].split())
            elif '"angle"' in line:
                angle = float(line.split('"')[3])
        break
    if origin is None or angle is None:
        raise ValueError("map has no usable info_player_start")
    return origin, angle


def load_palette(pak_path):
    raw = read_pak_entry(pak_path, "gfx/palette.lmp")
    return [(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]) for i in range(256)]


class Room(object):
    """World geometry visible from anywhere along the camera path."""

    def __init__(self, bsp, path, palette):
        model = bsp.models[0]
        firstface = model[14]
        # Union of the PVS of every leaf the camera visits.  Using only the
        # spawn leaf's PVS would leave holes as soon as the walk moves on.
        visible = set()
        for entry in path:
            visible |= set(bsp.visible_leafs(bsp.leaf_index(entry[:3])))
        visible = sorted(visible)

        mt_base = bsp.miptex_l[0]
        miptex_count = struct.unpack_from("<i", bsp.data, mt_base)[0]
        miptex_ofs = struct.unpack_from("%di" % miptex_count, bsp.data, mt_base + 4)

        self.vertices = []      # flat [x, y, z, s, t]
        self.batches = []       # [texture_index, first_index, count]
        self.indices = []       # GLushort triangle indices
        self.textures = []      # (name, w, h, rgba bytes, orig_w, orig_h)
        vertex_ids = {}
        texture_ids = {}
        texinfo_cache = {}

        def texture_for(miptex):
            name, width, height = struct.unpack_from(
                "<16sII", bsp.data, mt_base + miptex_ofs[miptex])
            name = name.split(b"\0", 1)[0].decode("ascii")
            key = (name, width, height)
            if key in texture_ids:
                return texture_ids[key]
            level = 0
            while max(width, height) >> level > MAX_TEX_DIM:
                level += 1
            entry = mt_base + miptex_ofs[miptex]
            offsets = struct.unpack_from("<4I", bsp.data, entry + 24)
            offset = offsets[level]
            tw, th = width >> level, height >> level
            # Sky textures are two 128-wide layers side by side; Quake scrolls
            # them against each other.  With no effects we keep the solid back
            # layer only, so the face is opaque instead of a hole.
            keep_w = tw // 2 if name.startswith("sky") else tw
            pixels = bytearray()
            for row in range(th):
                start = entry + offset + row * tw
                for column in range(keep_w):
                    r, g, b = palette[bsp.data[start + column]]
                    pixels.extend((r, g, b, 255))
            texture_ids[key] = len(self.textures)
            # The stored mip may be smaller than the texture the map was built
            # against; texinfo vectors are in original texel units, so ST has
            # to be divided by the original size or everything tiles too often.
            self.textures.append((name, keep_w, th, bytes(pixels),
                                  keep_w << level, height))
            return texture_ids[key]

        seen_faces = set()

        def add_face(face_index, origin=(0.0, 0.0, 0.0)):
            if face_index in seen_faces:
                return
            seen_faces.add(face_index)
            _planenum, _side, first_edge, edge_count, texinfo = \
                struct.unpack_from("<hhihh", bsp.data,
                                   bsp.faces_l[0] + 20 * face_index)
            info_base = bsp.texinfo_l[0] + 40 * texinfo
            # TEX_SPECIAL faces (sky, water, slime, lava) are kept and drawn
            # as ordinary textured surfaces.  Quake gives them scrolling or
            # warping effects we deliberately do not run, but skipping the
            # faces outright punches see-through holes in the world.
            if texinfo not in texinfo_cache:
                vecs = struct.unpack_from("<8f", bsp.data, info_base)
                miptex = struct.unpack_from("<i", bsp.data, info_base + 32)[0]
                name = struct.unpack_from("<16s", bsp.data,
                                          mt_base + miptex_ofs[miptex])[0]
                name = name.split(b"\0", 1)[0].decode("ascii").lower()
                if name in INVISIBLE_TEXTURES:
                    # trigger/clip brushes are collision volumes; Quake never
                    # draws them and neither do we.
                    texinfo_cache[texinfo] = None
                else:
                    texindex = texture_for(miptex)
                    # ST divides by the original miptex size: texinfo vectors
                    # are in the map's texel units, not the stored mip's.
                    tw, th = (self.textures[texindex][4],
                              self.textures[texindex][5])
                    texinfo_cache[texinfo] = (vecs, tw, th, texindex)
            if texinfo_cache[texinfo] is None:
                return
            vecs, tw, th, texindex = texinfo_cache[texinfo]

            corners = []
            for i in range(edge_count):
                edge = struct.unpack_from(
                    "<i", bsp.data,
                    bsp.surfedges_l[0] + 4 * (first_edge + i))[0]
                slot = 0 if edge >= 0 else 1
                # dedge_t is two u16 vertex indexes: 4 bytes.
                vert = struct.unpack_from(
                    "<2H", bsp.data, bsp.edges_l[0] + 4 * abs(edge))[slot]
                x, y, z = struct.unpack_from(
                    "<3f", bsp.data, bsp.vertexes_l[0] + 12 * vert)
                s = (vecs[0] * x + vecs[1] * y + vecs[2] * z + vecs[3]) / tw
                t = (vecs[4] * x + vecs[5] * y + vecs[6] * z + vecs[7]) / th
                x, y, z = x + origin[0], y + origin[1], z + origin[2]
                key = (vert, texinfo, origin)
                if key not in vertex_ids:
                    vertex_ids[key] = len(self.vertices)
                    self.vertices.append((x, y, z, s, t))
                corners.append(vertex_ids[key])

            first_index = len(self.indices)
            # Convex BSP faces fan-triangulate preserving Quake winding.
            for i in range(1, edge_count - 1):
                self.indices.extend((corners[0], corners[i], corners[i + 1]))
            self.batches.append((texindex, first_index, 3 * (edge_count - 2)))

        for leaf in visible:
            if bsp.leaf_contents(leaf) == CONTENTS_SOLID:
                continue
            first_mark, mark_count = struct.unpack_from(
                "<2H", bsp.data, bsp.leafs_l[0] + 28 * leaf + 20)
            if not mark_count:
                continue
            marks = struct.unpack_from("%dH" % mark_count, bsp.data,
                                       bsp.marksurfaces_l[0] + 2 * first_mark)
            for mark in marks:
                add_face(firstface + mark)

        # Doors, buttons and platforms are brush entities living in BSP models
        # 1..N, which no leaf's marksurfaces reference.  Without them the
        # world has holes exactly where a closed door should be.  They are
        # taken in their build (closed) position, which is what a walkthrough
        # that triggers nothing would see.
        path_lo = [min(p[a] for p in path) for a in range(2)]
        path_hi = [max(p[a] for p in path) for a in range(2)]
        self.submodels = 0
        for model in bsp.models[1:]:
            mins, maxs, origin = model[0:3], model[3:6], model[6:9]
            gap = math.hypot(max(mins[0] - path_hi[0], path_lo[0] - maxs[0], 0.0),
                             max(mins[1] - path_hi[1], path_lo[1] - maxs[1], 0.0))
            if gap > SUBMODEL_RANGE:
                continue
            self.submodels += 1
            for face in range(model[14], model[14] + model[15]):
                add_face(face, origin)

        # Group indices per texture across the whole PVS: GLQuake chains
        # surfaces by texture, and one draw call per texture is what the
        # suite's batching machinery expects.
        per_texture = {}
        for texindex, first, count in self.batches:
            per_texture.setdefault(texindex, []).extend(
                self.indices[first:first + count])
        self.indices = []
        self.batches = []
        for texindex in sorted(per_texture):
            runs = per_texture[texindex]
            self.batches.append((texindex, len(self.indices), len(runs)))
            self.indices.extend(runs)


# --- Camera path -----------------------------------------------------------
#
# The spawn point of e1m1 sits in a corridor, not an open room: the player
# hull only clears ~50 units sideways but over 1000 units along +y.  The walk
# is therefore an out-and-back patrol down the hall -- forward, turn about,
# back, turn about -- which loops seamlessly and looks like ordinary FPS
# movement.  Clearance is tested with the BSP collision hull (hull 1, the
# player box); a leaf bounds test is not enough, because a convex leaf's AABB
# happily contains points buried inside walls.

WALK_MARGIN = 96.0       # stop short of the far wall by this much
SWAY_DEGREES = 18.0      # gentle look-around while walking
TURN_FRAMES = 32         # frames spent reversing at each end


def walk_extent(bsp, spawn, direction):
    """How far the player hull can travel from spawn along `direction`."""
    distance = 0.0
    while distance < 4096.0:
        probe = (spawn[0] + direction[0] * (distance + 4.0),
                 spawn[1] + direction[1] * (distance + 4.0),
                 spawn[2])
        if not bsp.point_is_clear(probe):
            break
        distance += 4.0
    return distance


def build_path(bsp, spawn, facing):
    eye_z = spawn[2] + EYE_HEIGHT
    heading = math.radians(facing)
    direction = (math.cos(heading), math.sin(heading))
    reach = walk_extent(bsp, spawn, direction) - WALK_MARGIN
    if reach < 64.0:
        raise ValueError("no room to walk from the spawn point")

    walk_frames = (LOOP_FRAMES - 2 * TURN_FRAMES) // 2
    if walk_frames < 8:
        raise ValueError("LOOP_FRAMES too small for the walk phases")

    def position(distance):
        return (spawn[0] + direction[0] * distance,
                spawn[1] + direction[1] * distance)

    path = []
    for frame in range(LOOP_FRAMES):
        phase = frame
        if phase < walk_frames:                     # walk out
            t = phase / float(walk_frames)
            distance = reach * t
            yaw = facing
        elif phase < walk_frames + TURN_FRAMES:     # turn about at the far end
            t = (phase - walk_frames) / float(TURN_FRAMES)
            distance = reach
            yaw = facing + 180.0 * t
        elif phase < 2 * walk_frames + TURN_FRAMES:  # walk back
            t = (phase - walk_frames - TURN_FRAMES) / float(walk_frames)
            distance = reach * (1.0 - t)
            yaw = facing + 180.0
        else:                                       # turn about at the start
            t = (phase - 2 * walk_frames - TURN_FRAMES) / float(TURN_FRAMES)
            distance = 0.0
            yaw = facing + 180.0 + 180.0 * t

        # Sway only while walking, and taper it to zero at both ends of a
        # leg so it joins the turns without a kink.
        sway = 0.0
        if phase < walk_frames:
            sway = math.sin(2.0 * math.pi * (phase / float(walk_frames)))
        elif walk_frames + TURN_FRAMES <= phase < 2 * walk_frames + TURN_FRAMES:
            leg = (phase - walk_frames - TURN_FRAMES) / float(walk_frames)
            sway = math.sin(2.0 * math.pi * leg)

        x, y = position(distance)
        path.append((x, y, eye_z, (yaw + SWAY_DEGREES * sway) % 360.0))

    for entry in path:
        if not bsp.point_is_clear((entry[0], entry[1], spawn[2])):
            raise ValueError("camera path leaves the player hull")
    return path, reach


# --- Offline reference render ----------------------------------------------

def view_transform(point, eye, yaw_deg):
    """World -> eye coordinates.  Exactly what glRotatef(-90,1,0,0),
    glRotatef(90-yaw,0,0,1), glTranslatef(-eye) produce in the C draw."""
    ca = math.cos(math.radians(90.0 - yaw_deg))
    sa = math.sin(math.radians(90.0 - yaw_deg))
    qx, qy, qz = point[0] - eye[0], point[1] - eye[1], point[2] - eye[2]
    xe = ca * qx - sa * qy
    ye = qz
    ze = -(sa * qx + ca * qy)
    return xe, ye, ze


def project(point, eye, yaw_deg):
    xe, ye, ze = view_transform(point, eye, yaw_deg)
    if ze > -PROJ_NEAR:
        return None
    inv = 1.0 / -ze
    return ((xe / PROJ_HALF_W) * inv, (ye / PROJ_HALF_H) * inv, -ze)


def clip_near(poly):
    """Sutherland-Hodgman against the near plane in eye space.

    Without this, any triangle with a vertex behind the eye is dropped
    whole, which punches large holes into the reference image -- hardware
    clips these properly, so the baked samples would not match.
    Vertices are (xe, ye, ze, s, t); everything is linear in eye space."""
    out = []
    count = len(poly)
    for i in range(count):
        cur = poly[i]
        nxt = poly[(i + 1) % count]
        cur_in = cur[2] <= -PROJ_NEAR
        nxt_in = nxt[2] <= -PROJ_NEAR
        if cur_in:
            out.append(cur)
        if cur_in != nxt_in:
            span = nxt[2] - cur[2]
            if abs(span) < 1e-12:
                continue
            f = (-PROJ_NEAR - cur[2]) / span
            out.append(tuple(cur[k] + (nxt[k] - cur[k]) * f for k in range(5)))
    return out


def render_reference(room, path, far):
    """Software-render frame 0 at REF_WIDTHxREF_HEIGHT with nearest sampling
    and perspective-correct interpolation.  Returns (colour rows, z rows)."""
    eye = (path[0][0], path[0][1], path[0][2])
    yaw = path[0][3]
    colour = [[None] * REF_WIDTH for _ in range(REF_HEIGHT)]  # type: List[List[Colour]]
    depth = [[far * 2.0] * REF_WIDTH for _ in range(REF_HEIGHT)]
    eye_space = [view_transform(v, eye, yaw) for v in room.vertices]
    textures = room.textures
    for texindex, first, count in room.batches:
        _name, tw, th, rgb, _ow, _oh = textures[texindex]
        for tri in range(first, first + count, 3):
            corners = room.indices[tri:tri + 3]
            poly = [eye_space[i] + (room.vertices[i][3], room.vertices[i][4])
                    for i in corners]
            if all(v[2] > -PROJ_NEAR for v in poly):
                continue
            poly = clip_near(poly)
            if len(poly) < 3:
                continue

            screen = []
            for xe, ye, ze, s, t in poly:
                invw = 1.0 / -ze
                sx = ((xe / PROJ_HALF_W) * invw + 1.0) * 0.5 * REF_WIDTH
                sy = (1.0 - (ye / PROJ_HALF_H) * invw) * 0.5 * REF_HEIGHT
                screen.append((sx, sy, invw, s * invw, t * invw))

            for fan in range(1, len(screen) - 1):
                v0, v1, v2 = screen[0], screen[fan], screen[fan + 1]
                px = (v0[0], v1[0], v2[0])
                py = (v0[1], v1[1], v2[1])
                if min(px) >= REF_WIDTH or max(px) < 0 or \
                   min(py) >= REF_HEIGHT or max(py) < 0:
                    continue
                area = ((px[1] - px[0]) * (py[2] - py[0]) -
                        (px[2] - px[0]) * (py[1] - py[0]))
                if abs(area) < 1e-9:
                    continue
                # The suite draws with glCullFace(GL_FRONT) because Quake
                # windings are clockwise.  Without matching that here, the
                # reference can shade the inside of a face the hardware
                # discards and bake a sample the hardware never produces.
                if area * CULL_SIGN < 0.0:
                    continue
                x_lo = max(int(min(px)), 0)
                x_hi = min(int(max(px)) + 1, REF_WIDTH)
                y_lo = max(int(min(py)), 0)
                y_hi = min(int(max(py)) + 1, REF_HEIGHT)
                iw = (v0[2], v1[2], v2[2])
                sw = (v0[3], v1[3], v2[3])
                twq = (v0[4], v1[4], v2[4])
                for sy in range(y_lo, y_hi):
                    cy = sy + 0.5
                    for sx in range(x_lo, x_hi):
                        cx = sx + 0.5
                        w0 = ((px[1] - cx) * (py[2] - cy) -
                              (px[2] - cx) * (py[1] - cy)) / area
                        w1 = ((px[2] - cx) * (py[0] - cy) -
                              (px[0] - cx) * (py[2] - cy)) / area
                        w2 = 1.0 - w0 - w1
                        # NaN-safe inside test: comparisons on NaN are False.
                        if not (w0 >= 0 and w1 >= 0 and w2 >= 0):
                            continue
                        invw = w0 * iw[0] + w1 * iw[1] + w2 * iw[2]
                        if invw <= 0.0:
                            continue
                        z = 1.0 / invw
                        if z >= depth[sy][sx]:
                            continue
                        s = (w0 * sw[0] + w1 * sw[1] + w2 * sw[2]) * z
                        t = (w0 * twq[0] + w1 * twq[1] + w2 * twq[2]) * z
                        tx = int(s * tw) % tw
                        ty = int(t * th) % th
                        base = (ty * tw + tx) * 4   # RGBA texels
                        depth[sy][sx] = z
                        colour[sy][sx] = (rgb[base], rgb[base + 1], rgb[base + 2])
    return colour, depth


def set_reference_size(width, height):
    """Switch the offline render resolution.

    Nearest-neighbour minification makes a block mean depend on which texels
    the sampling happens to hit, so each pose is also rendered oversampled;
    blocks whose mean moves are texel-noise dominated and are not verified."""
    global REF_WIDTH, REF_HEIGHT
    REF_WIDTH, REF_HEIGHT = width, height


def block_bounds():
    """NDC rectangles of the verification grid, shared by every pose."""
    rects = []
    for row in range(BLOCK_ROWS):
        for col in range(BLOCK_COLS):
            u0 = BLOCK_INSET + (1.0 - 2.0 * BLOCK_INSET) * col / BLOCK_COLS
            u1 = BLOCK_INSET + (1.0 - 2.0 * BLOCK_INSET) * (col + 1) / BLOCK_COLS
            v0 = BLOCK_INSET + (1.0 - 2.0 * BLOCK_INSET) * row / BLOCK_ROWS
            v1 = BLOCK_INSET + (1.0 - 2.0 * BLOCK_INSET) * (row + 1) / BLOCK_ROWS
            rects.append((u0 * 2.0 - 1.0, 1.0 - v0 * 2.0,
                          u1 * 2.0 - 1.0, 1.0 - v1 * 2.0))
    return rects


def block_means(colour):
    """Mean colour of each verification block.

    Per-pixel checks cannot be used here: the walk looks down a long
    corridor, where GL_NEAREST minification makes each pixel a coin toss
    between many texels, and the hardware and this renderer legitimately
    disagree pixel by pixel.  Averaging a block is stable to a couple of
    levels while still catching a wrong texture, transform or missing
    surface.
    """
    means = []
    for ndc_x0, ndc_y0, ndc_x1, ndc_y1 in block_bounds():
        x0 = int((ndc_x0 + 1.0) * 0.5 * REF_WIDTH)
        x1 = int((ndc_x1 + 1.0) * 0.5 * REF_WIDTH)
        y0 = int((1.0 - ndc_y0) * 0.5 * REF_HEIGHT)
        y1 = int((1.0 - ndc_y1) * 0.5 * REF_HEIGHT)
        total = [0, 0, 0]
        count = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                pixel = colour[y][x] or (0, 0, 0)
                total[0] += pixel[0]
                total[1] += pixel[1]
                total[2] += pixel[2]
                count += 1
        means.append(tuple(channel // count for channel in total))
    return means


def coverage_fraction(colour):
    filled = sum(1 for row in colour for c in row if c is not None)
    return filled / float(REF_WIDTH * REF_HEIGHT)


def write_header(path, map_name, room, path_table, samples, stable,
                 far, coverage):
    guard = "QUAKE_ROOM_MODEL_H"

    def rows(values, width):
        for start in range(0, len(values), width):
            yield values[start:start + width]

    with open(path, "w", encoding="ascii", newline="\n") as out:
        out.write("/* Generated from %s by tools/extract_quake_bsp_room.py.\n"
                  " *\n * World faces visible from the info_player_start through\n"
                  " * the PVS, with mip-expanded wall textures, a deterministic\n"
                  " * camera patrol loop, and multi-pose block means rendered\n"
                  " * offline with the same projection the suite draws with.\n"
                  " */\n" % map_name)
        out.write("#ifndef %s\n#define %s\n\n" % (guard, guard))
        out.write("#define QUAKE_ROOM_VERTEX_COUNT %d\n" % len(room.vertices))
        out.write("#define QUAKE_ROOM_INDEX_COUNT %d\n" % len(room.indices))
        out.write("#define QUAKE_ROOM_BATCH_COUNT %d\n" % len(room.batches))
        out.write("#define QUAKE_ROOM_TEXTURE_COUNT %d\n" % len(room.textures))
        out.write("#define QUAKE_ROOM_LOOP_FRAMES %d\n" % LOOP_FRAMES)
        out.write("#define QUAKE_ROOM_VERIFY_POSES %d\n" % len(VERIFY_FRAMES))
        out.write("#define QUAKE_ROOM_BLOCK_COUNT %d\n"
                  % (BLOCK_COLS * BLOCK_ROWS))
        out.write("#define QUAKE_ROOM_PROJ_NEAR %.1ff\n" % PROJ_NEAR)
        out.write("#define QUAKE_ROOM_PROJ_FAR %.1ff\n" % far)
        out.write("#define QUAKE_ROOM_PROJ_HALF_W %.4ff\n" % PROJ_HALF_W)
        out.write("#define QUAKE_ROOM_PROJ_HALF_H %.4ff\n" % PROJ_HALF_H)
        out.write("#define QUAKE_ROOM_BLOCK_TOLERANCE %d\n\n" % BLOCK_TOLERANCE)

        out.write("/* Interleaved position + world-mapped ST, GL_REPEAT wrap,\n"
                  " * GL_NEAREST filtering; stride is 5 floats. */\n")
        out.write("static const GLfloat g_quakeRoomVertices[]"
                  "[%d] = {\n" % 5)
        for v in room.vertices:
            out.write("    {%.3ff, %.3ff, %.3ff, %.6ff, %.6ff},\n" % v)
        out.write("};\n\nstatic const GLushort g_quakeRoomIndices[] = {\n")
        for row in rows(room.indices, 18):
            out.write("    " + ", ".join(str(i) for i in row) + ",\n")
        out.write("};\n\n/* Per-texture draw batches: {texture, first, count}.\n"
                  " * Consecutive same-texture faces are merged like GLQuake's\n"
                  " * texture-chained surfaces. */\n")
        out.write("static const GLint g_quakeRoomBatches[][3] = {\n")
        for texindex, first, count in room.batches:
            out.write("    {%d, %d, %d},\n" % (texindex, first, count))
        out.write("};\n\n")

        out.write("/* Mip-expanded through gfx/palette.lmp, RGBA8888 rows of\n"
                  " * exactly width*4 bytes (the format every suite texture\n"
                  " * uploads with on R200).  Textures:\n")
        for name, tw, th, _rgb, _ow, _oh in room.textures:
            out.write(" *   %-16s %dx%d\n" % (name, tw, th))
        out.write(" */\n")
        texels = bytearray()
        offsets = []
        sizes = []
        for _name, tw, th, rgb, _ow, _oh in room.textures:
            offsets.append(len(texels))
            sizes.append((tw, th))
            texels.extend(rgb)
        out.write("#define QUAKE_ROOM_TEXELS_SIZE %d\n" % len(texels))
        out.write("static const unsigned char g_quakeRoomTexels[] = {\n")
        for row in rows(texels, 24):
            out.write("    " + ", ".join(str(b) for b in row) + ",\n")
        out.write("};\n\nstatic const ULONG g_quakeRoomTexOffset[] = {\n")
        out.write("    " + ", ".join(str(o) for o in offsets) + "\n};\n")
        out.write("static const GLushort g_quakeRoomTexSize[][2] = {\n")
        for tw, th in sizes:
            out.write("    {%d, %d},\n" % (tw, th))
        out.write("};\n\n")

        out.write("/* Camera patrol: {x, y, eyeZ, yawDeg} per loop frame;\n"
                  " * pure function of frame %% QUAKE_ROOM_LOOP_FRAMES. */\n")
        out.write("static const GLfloat g_quakeRoomPath[][4] = {\n")
        for x, y, z, yaw in path_table:
            out.write("    {%.2ff, %.2ff, %.2ff, %.2ff},\n" % (x, y, z, yaw))
        out.write("};\n\n/* Verification grid in NDC: {x0, y0, x1, y1}. */\n")
        out.write("static const GLfloat g_quakeRoomBlocks[][4] = {\n")
        for rect in block_bounds():
            out.write("    {%.6ff, %.6ff, %.6ff, %.6ff},\n" % rect)
        out.write("};\n\n")
        out.write("/* Mean block colour per verified pose, rendered offline\n"
                  " * with the exact draw-time transform.  Block means, not\n"
                  " * point samples: GL_NEAREST minification down the corridor\n"
                  " * makes single pixels disagree between renderers, while\n"
                  " * the averages stay within a couple of levels.  Coverage\n"
                  " * of the verified frames was %s. */\n"
                  % ", ".join("%.1f%%" % (c * 100.0) for c in coverage))
        out.write("static const ULONG g_quakeRoomVerifyFrames[] = {\n    "
                  + ", ".join(str(f) for f in VERIFY_FRAMES) + "\n};\n")
        out.write("static const GLubyte g_quakeRoomBlockRGB"
                  "[QUAKE_ROOM_VERIFY_POSES][QUAKE_ROOM_BLOCK_COUNT][3] = {\n")
        for pose, frame in enumerate(VERIFY_FRAMES):
            out.write("    {   /* frame %d */\n" % frame)
            for r, g, b in samples[pose]:
                out.write("        {%d, %d, %d},\n" % (r, g, b))
            out.write("    },\n")
        out.write("};\n\n")
        out.write("/* 1 where the block mean survived %s.  Blocks that\n"
                  " * did not are dominated by\n"
                  " * which texels point sampling happens to hit and are not\n"
                  " * checked. */\n"
                  % ("%dx oversampling" % REF_SUPERSAMPLE))
        out.write("static const GLubyte g_quakeRoomBlockStable"
                  "[QUAKE_ROOM_VERIFY_POSES][QUAKE_ROOM_BLOCK_COUNT] = {\n")
        for pose, frame in enumerate(VERIFY_FRAMES):
            out.write("    {%s},   /* frame %d */\n"
                      % (", ".join(str(f) for f in stable[pose]), frame))
        out.write("};\n\n#endif\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pak")
    parser.add_argument("--map", default="maps/e1m1.bsp")
    parser.add_argument("output")
    parser.add_argument("--preview", help="write frame-0 render as PPM here")
    args = parser.parse_args()

    bsp = Bsp(read_pak_entry(args.pak, args.map))
    spawn, spawn_angle = parse_spawn(bsp.entities)
    palette = load_palette(args.pak)
    path, reach = build_path(bsp, spawn, spawn_angle)
    room = Room(bsp, path, palette)

    # The far plane has to cover the whole world from anywhere on the walk,
    # so size it from the bounding box of the geometry and the path together.
    lo = [min(min(v[a] for v in room.vertices),
              min(p[a] for p in path)) for a in range(3)]
    hi = [max(max(v[a] for v in room.vertices),
              max(p[a] for p in path)) for a in range(3)]
    diagonal = math.sqrt(sum((hi[a] - lo[a]) ** 2 for a in range(3)))
    far = 256.0
    while far < diagonal * 1.25:
        far *= 2.0

    samples = []
    stable = []
    coverage = []
    base_size = (REF_WIDTH, REF_HEIGHT)
    for frame in VERIFY_FRAMES:
        rotated = path[frame:] + path[:frame]

        set_reference_size(*base_size)
        colour, _depth = render_reference(room, rotated, far)
        coverage.append(coverage_fraction(colour))
        means = block_means(colour)
        if args.preview and frame == 0:
            with open(args.preview, "wb") as ppm:
                ppm.write(("P6\n%d %d\n255\n"
                           % (REF_WIDTH, REF_HEIGHT)).encode("ascii"))
                for row in colour:
                    for cell in row:
                        ppm.write(bytes(cell) if cell else b"\0\0\0")

        # Oversampled truth: a block whose mean shifts when the same view is
        # sampled more densely is dominated by nearest-neighbour texel noise,
        # and the hardware -- sampling at its own resolution -- will disagree.
        set_reference_size(base_size[0] * REF_SUPERSAMPLE,
                           base_size[1] * REF_SUPERSAMPLE)
        dense, _depth = render_reference(room, rotated, far)
        dense_means = block_means(dense)
        set_reference_size(*base_size)

        flags = []
        for coarse, fine in zip(means, dense_means):
            drift = max(abs(coarse[i] - fine[i]) for i in range(3))
            flags.append(1 if drift <= BLOCK_STABILITY else 0)
        samples.append(means)
        stable.append(flags)

    write_header(args.output, args.map, room, path, samples, stable,
                 far, coverage)
    triangles = len(room.indices) // 3
    print("map %s spawn (%.0f,%.0f,%.0f) angle %.0f" %
          ((args.map,) + tuple(spawn) + (spawn_angle,)))
    print("verts %d tris %d batches %d textures %d" %
          (len(room.vertices), triangles, len(room.batches), len(room.textures)))
    print("walk %.0f units out and back, loop %d frames, far %.0f" %
          (reach, LOOP_FRAMES, far))
    print("stable blocks per pose: %s" %
          " ".join("%d/%d" % (sum(f), len(f)) for f in stable))
    print("verified poses %s, %dx%d block grid, coverage %s" %
          (", ".join(str(f) for f in VERIFY_FRAMES), BLOCK_COLS, BLOCK_ROWS,
           " ".join("%.1f%%" % (c * 100.0) for c in coverage)))


if __name__ == "__main__":
    main()
