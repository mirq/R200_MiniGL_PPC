"""CPU-only shared-emitter adapter tests: python3 tests/test_r200_cp_emit.py -v.

Compiles the actual adapter and /home/mirek/p96-driver/src/radeon3d_emit.c
as separate translation units, with ASan/UBSan. Native builder reset/loops
are extracted from radeon3d_service.c for the reference paths. No emitter
implementation is copied, replaced or mocked. Only exec/types and the six
pointer-bearing public ABI size assertions need host fixtures, all generated
under /tmp/opencode. Production headers and the driver remain untouched.

Non-PIE places synthetic readable texture storage below 4 GiB, so the actual
posted-write drain runs too. This checks CPU command generation, not PPC
cache coherency, VRAM ownership, byte-lane transport, submission or rendering.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = Path("/home/mirek/p96-driver")
ADAPTER = ROOT / "backend_r200/library/r200_cp_emit.c"
INCLUDE = ROOT / "backend_r200/include"


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def reference_builders():
    service = (DRIVER / "src/radeon3d_service.c").read_text()
    execute = section(service, "BOOL Radeon3DExecute(", "BOOL Radeon3DAllocSegment(")
    commit = section(service, "static BOOL CommitRecords(", "BOOL Radeon3DCommitDraw(")
    state = section(service, "BOOL Radeon3DCommitStateBatch(", "BOOL Radeon3DDispatchIndirect(")
    execute_init = section(execute, "    emitter->Words = generated;", "    buildStart =")
    commit_init = section(commit, "    emitter->Words = generated;", "    phase = ServiceExecTicks")
    state_init = section(state, "    emitter->Words = generated;", "    /* The first descriptor")
    state_loop = section(state, "    trusted[10] =", "    result = streamBuilt;")
    clock = "    phase = ServiceExecTicks(base);\n"
    if state_loop.count(clock) != 1:
        raise AssertionError("Native state-batch builder changed; review reference extraction")
    state_loop = state_loop.replace(clock, "")
    return r'''
static ULONG reference(ULONG *generated, const ULONG *records, ULONG recordDwords,
                       const struct MGLPPCEmitVertices *v) {
    static struct Radeon3DEmitter storage;
    struct Radeon3DEmitter *emitter = &storage;
    struct { ULONG GpuAddress, Bytes; } segment = {0, 0}, *slot = &segment;
    const ULONG *vertexOffsets = v ? v->offsets : NULL;
    BOOL streamBuilt;
    emitter->InterfaceVersion = RADEON3D_IFACE_VERSION;
    emitter->Resolve = native_resolve;
    emitter->ResolveUser = surfaces;
    emitter->ResolveSlot = 0;
    emitter->FailStage = 0;
    if (v) { slot->GpuAddress = v->gpuAddress; slot->Bytes = v->bytes; }
    if (v && v->stateBatch) {
        static ULONG trusted[RADEON3D_MAX_BATCH_DWORDS];
        struct Radeon3DStateBatch request = {0};
        struct Radeon3DStateBatchDraw *draws;
        ULONG primitiveType, index;
        request.HeaderDwords = recordDwords;
        request.DrawCount = v->drawCount;
        CHECK(Radeon3DEmitPrimitiveType(records[0], &primitiveType));
        memcpy(trusted, records, recordDwords * sizeof(ULONG));
        draws = (struct Radeon3DStateBatchDraw *)(trusted + recordDwords);
        for (index = 0; index < request.DrawCount; ++index) {
            draws[index].OffsetBytes = v->offsets[index * 2];
            draws[index].VertexCount = v->offsets[index * 2 + 1];
        }
''' + state_init + state_loop + r'''
    } else if (v) {
''' + commit_init + r'''
        streamBuilt = Radeon3DEmitStream(emitter, records, recordDwords);
    } else {
''' + execute_init + r'''
        streamBuilt = Radeon3DEmitStream(emitter, records, recordDwords);
    }
    emitter->CommitVbuf = FALSE;
    emitter->CommitSegmentGpuBase = 0;
    emitter->CommitSegmentBytes = 0;
    emitter->CommitVertexOffsets = NULL;
    emitter->CommitDrawIndex = 0;
    emitter->CommitVbufAddress = 0;
    emitter->StateValid = FALSE;
    return streamBuilt ? emitter->Count : 0;
}
'''


TYPES = r'''
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint16_t UWORD;
typedef int16_t WORD;
typedef uint8_t UBYTE;
typedef int8_t BYTE;
typedef int BOOL;
typedef void *APTR;
#define TRUE 1
#define FALSE 0
#endif
'''

HARNESS = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <libraries/minigl_ppc_cp_emit.h>
#include <radeon3d_emit.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); } } while (0)
#define TCL (RADEON3D_DRAW_HW_TCL | RADEON3D_DRAW_EXTENDED_VERTEX | RADEON3D_DRAW_FRAGMENT_STATE)
#define MAX_HEADER (RADEON3D_EXEC_DRAW_TEXGEN_HEADER_DWORDS + RADEON3D_EXEC_NORMAL_MATRICES_DWORDS + RADEON3D_EXEC_LIGHT_STATE_DWORDS + RADEON3D_EXEC_MAX_LIGHT_BLOCKS * RADEON3D_EXEC_LIGHT_BLOCK_DWORDS)
#define FULL (TCL | RADEON3D_DRAW_TEXTURED | RADEON3D_DRAW_TEXGEN | RADEON3D_DRAW_NORMALS | RADEON3D_DRAW_LIGHTING | RADEON3D_DRAW_DEPTH_LESS | RADEON3D_DRAW_DEPTH_WRITE)
enum { COLOR = 1, DEPTH, TEX0, TEX1 };
static UBYTE pixels[4][16384] __attribute__((aligned(64)));
static struct MGLPPCEmitSurface surfaces[4];
static ULONG resolveCalls, published, partialFailures;
static ULONG output[MGLPPC_CP_DWORDS], expected[MGLPPC_CP_DWORDS];

static void init_surfaces(void) {
    for (ULONG i = 0; i < 4; ++i) {
        CHECK((uintptr_t)pixels[i] <= UINT32_MAX - sizeof(pixels[i]));
        surfaces[i] = (struct MGLPPCEmitSurface){(ULONG)(uintptr_t)pixels[i],
            0x81200000U + i * 0x100000U, i == 0 ? 256U : 128U, 64, 32,
            i == 0 ? RADEON3D_FORMAT_B8G8R8A8 : RADEON3D_FORMAT_R5G6B5PC};
    }
}

static BOOL resolve(void *user, ULONG handle, struct MGLPPCEmitSurface *out) {
    CHECK(user == surfaces && handle != 0);
    ++resolveCalls;
    if (handle < COLOR || handle > TEX1) return FALSE;
    *out = surfaces[handle - 1];
    return TRUE;
}

static struct Radeon3DEmitSurface *native_resolve(
    void *user, ULONG handle, struct Radeon3DEmitSurface *out) {
    CHECK(user == surfaces);
    if (handle < COLOR || handle > TEX1) return NULL;
    const struct MGLPPCEmitSurface *s = &surfaces[handle - 1];
    *out = (struct Radeon3DEmitSurface){(APTR)(uintptr_t)s->cpuAddress,
        s->gpuAddress, s->pitch, s->width, s->height, s->format};
    return out;
}
'''

CASES = r'''
/* Use exact-sized heap inputs/output so ASan catches reads past a truncated
 * record or writes past the 8192-dword API capacity, not just past a fixture. */
static ULONG run(const ULONG *r, ULONG n, const struct MGLPPCEmitVertices *v,
                 int success) {
    ULONG *input = malloc(n * sizeof(ULONG));
    ULONG *words = malloc(MGLPPC_CP_DWORDS * sizeof(ULONG));
    ULONG offsets[2 * RADEON3D_STATE_BATCH_MAX_DRAWS];
    ULONG count, offsetWords = v ? v->drawCount * (v->stateBatch ? 2 : 1) : 0;
    CHECK(input && words && n && n <= MGLPPC_CP_DWORDS);
    CHECK(offsetWords <= 2 * RADEON3D_STATE_BATCH_MAX_DRAWS);
    memcpy(input, r, n * sizeof(ULONG));
    if (offsetWords) memcpy(offsets, v->offsets, offsetWords * sizeof(ULONG));
    for (ULONG i = 0; i < MGLPPC_CP_DWORDS; ++i) words[i] = 0xa5a5a5a5U;
    count = MGLPPCEmitBatch(words, input, n, v, resolve, surfaces);
    CHECK(count <= MGLPPC_CP_DWORDS && !memcmp(input, r, n * sizeof(ULONG)));
    if (offsetWords) CHECK(!memcmp(offsets, v->offsets, offsetWords * sizeof(ULONG)));
    if (success >= 0) CHECK(!!count == success);
    if (count) {
        ++published;
        memcpy(output, words, count * sizeof(ULONG));
        if (success == 1) {
            ULONG nativeCount = reference(expected, r, n, v);
            CHECK(nativeCount == count && !memcmp(words, expected, count * sizeof(ULONG)));
        }
    } else if (words[0] != 0xa5a5a5a5U) ++partialFailures;
    free(words);
    free(input);
    return count;
}

static void identity(ULONG *matrix) {
    for (ULONG i = 0; i < 16; ++i) matrix[i] = i % 5 == 0 ? 0x3f800000U : 0;
}

static ULONG make_header(ULONG *r, ULONG opcode, ULONG options,
                         ULONG vertexState, ULONG lights) {
    ULONG n = options & RADEON3D_DRAW_TEXGEN ? RADEON3D_EXEC_DRAW_TEXGEN_HEADER_DWORDS :
        options & RADEON3D_DRAW_HW_TCL ? RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS :
        options & RADEON3D_DRAW_EXTENDED_VERTEX ? RADEON3D_EXEC_DRAW_EXTENDED_HEADER_DWORDS :
        options & RADEON3D_DRAW_FRAGMENT_STATE ? RADEON3D_EXEC_DRAW_FRAGMENT_HEADER_DWORDS :
        RADEON3D_EXEC_DRAW_HEADER_DWORDS;
    memset(r, 0, MAX_HEADER * sizeof(ULONG));
    r[0] = opcode; r[2] = COLOR; r[5] = options;
    r[6] = 1; r[7] = 2; r[8] = 63; r[9] = 31; r[10] = 3;
    if (options & RADEON3D_DRAW_DEPTH_LESS) r[3] = DEPTH;
    if (options & RADEON3D_DRAW_TEXTURED) {
        r[4] = TEX0;
        if (options & RADEON3D_DRAW_FRAGMENT_STATE) {
            r[12] = (31U << 16) | 63U; r[13] = RADEON3D_TEX_MODULATE;
        }
    }
    if (options & RADEON3D_DRAW_EXTENDED_VERTEX) {
        r[19] = vertexState;
        if (vertexState & RADEON3D_VERTEX_TEXTURE1) {
            r[15] = TEX1; r[17] = (31U << 16) | 63U; r[18] = RADEON3D_TEX_MODULATE;
        }
        if (vertexState & RADEON3D_VERTEX_FOG) r[20] = 0x123456U;
    }
    if (options & RADEON3D_DRAW_HW_TCL) {
        identity(r + 21);
        r[22] = 0x3e800000U; r[24] = 0xbe800000U;
        r[37] = r[38] = 0x42000000U;
        r[39] = 0xc1800000U; r[40] = 0x41800000U;
        r[41] = r[42] = 0x3f000000U;
        r[43] = (16U << RADEON3D_TRANSFORM_POINT_SIZE_SHIFT) | RADEON3D_TRANSFORM_FRONT_CCW;
    }
    if (options & RADEON3D_DRAW_TEXGEN) {
        for (ULONG unit = 0; unit < 2; ++unit) {
            if (unit ? vertexState & RADEON3D_VERTEX_TEXTURE1 : options & RADEON3D_DRAW_TEXTURED) {
                r[44 + unit] = RADEON3D_TEXGEN_MODE_OBJECT_LINEAR |
                    RADEON3D_TEXGEN_GEN_S | RADEON3D_TEXGEN_GEN_T | RADEON3D_TEXGEN_GEN_Q;
                identity(r + 46 + 16 * unit);
            }
        }
    }
    if (options & (RADEON3D_DRAW_NORMALS | RADEON3D_DRAW_LIGHTING)) {
        identity(r + n); identity(r + n + 16); n += RADEON3D_EXEC_NORMAL_MATRICES_DWORDS;
    }
    if (options & RADEON3D_DRAW_LIGHTING) {
        for (ULONG i = 0; i < RADEON3D_EXEC_LIGHT_STATE_DWORDS; ++i) r[n + i] = 0x3f000000U;
        r[n + 8] = lights | (lights << 16) | (lights << 24) | RADEON3D_LIGHT_LOCAL_VIEWER;
        n += RADEON3D_EXEC_LIGHT_STATE_DWORDS;
        for (ULONG light = 0; light < 8; ++light) if (lights & (1U << light)) {
            for (ULONG i = 0; i < RADEON3D_EXEC_LIGHT_BLOCK_DWORDS; ++i) r[n + i] = 0x3f000000U;
            n += RADEON3D_EXEC_LIGHT_BLOCK_DWORDS;
        }
    }
    r[1] = n;
    return n;
}

static ULONG inline_vertices(ULONG *r, ULONG count) {
    ULONG options = r[5], n = r[1];
    BOOL tcl = !!(options & RADEON3D_DRAW_HW_TCL);
    BOOL normal = !!(options & (RADEON3D_DRAW_NORMALS | RADEON3D_DRAW_LIGHTING));
    BOOL extended = !!(options & RADEON3D_DRAW_EXTENDED_VERTEX);
    ULONG stride = tcl ? (options & RADEON3D_DRAW_COMPACT_VERTEX ? (normal ? 10 : 7) :
                         (normal ? 13 : 10)) : extended ? 9 : 6;
    memset(r + n, 0, count * stride * sizeof(ULONG));
    for (ULONG i = 0; i < count; ++i) {
        ULONG *v = r + n + i * stride;
        v[0] = tcl ? 0xbf400000U : 0x3f800000U;
        v[1] = 0x40000000U; v[2] = 0x3f000000U;
        if (tcl) {
            v[3] = 0x3f800000U; v[normal ? 7 : 4] = 0x89abcdefU;
            if (normal) v[6] = 0x3f800000U;
            if (options & RADEON3D_DRAW_TEXTURED) v[normal ? 8 : 5] = v[normal ? 9 : 6] = 0x3e800000U;
            if (r[19] & RADEON3D_VERTEX_TEXTURE1) v[normal ? 10 : 7] = v[normal ? 11 : 8] = 0x3f000000U;
            if (r[19] & RADEON3D_VERTEX_FOG) v[normal ? 12 : 9] = 0x3f000000U;
        } else {
            v[5] = 0x89abcdefU;
            if (options & RADEON3D_DRAW_TEXTURED) v[3] = v[4] = 0x3e800000U;
            if (extended && r[19] & RADEON3D_VERTEX_TEXTURE1) v[6] = v[7] = 0x3e800000U;
            if (extended && r[19] & RADEON3D_VERTEX_FOG) v[8] = 0x3f000000U;
        }
    }
    r[10] = count; r[1] = n + count * stride;
    return r[1];
}

static ULONG clear_record(ULONG *r) {
    const ULONG clear[] = {RADEON3D_EXEC_CLEAR, RADEON3D_EXEC_CLEAR_DWORDS,
        COLOR, 0, RADEON3D_CLEAR_COLOR, 0x89abcdefU, 0, 1, 2, 63, 31};
    memcpy(r, clear, sizeof(clear));
    return RADEON3D_EXEC_CLEAR_DWORDS;
}

static BOOL contains_pair(ULONG n, ULONG first, ULONG second) {
    for (ULONG i = 0; i + 1 < n; ++i) if (output[i] == first && output[i + 1] == second) return TRUE;
    return FALSE;
}

int main(int argc, char **argv) {
    static ULONG r[MGLPPC_CP_DWORDS], chain[MGLPPC_CP_DWORDS];
    static ULONG saved[MGLPPC_CP_DWORDS], offsets[2 * RADEON3D_STATE_BATCH_MAX_DRAWS];
    struct MGLPPCEmitVertices v = {0x93400000U, 65536, 1, FALSE, offsets};
    ULONG n, h;
    CHECK(argc == 2);
    init_surfaces();
    if (!strcmp(argv[1], "inline")) {
        n = clear_record(r);
        ULONG emitted = run(r, n, NULL, 1);
        CHECK(contains_pair(emitted, RADEON_CP_PACKET0(R200_RB3D_COLOROFFSET, 0), 0x81200000U));
        CHECK(output[emitted - 3] == 0x3f800000U && output[emitted - 2] == 0x41f80000U);
        CHECK(output[emitted - 1] == 0x89abcdefU);
        h = make_header(r + n, RADEON3D_EXEC_DRAW_TRIANGLES, 0, 0, 0);
        CHECK(h == 11);
        n += inline_vertices(r + n, 3);
        emitted = run(r, n, NULL, 1);
        CHECK(output[emitted - 3] == 0x3f800000U && output[emitted - 2] == 0x40000000U);
        CHECK(output[emitted - 1] == 0x89abcdefU);
        clear_record(r); r[3] = DEPTH; r[4] |= RADEON3D_CLEAR_DEPTH; r[6] = 0x3f800000U;
        run(r, 11, NULL, 1);
        for (ULONG format = 1; format <= 3; ++format) {
            surfaces[0].format = format;
            run(r, 11, NULL, 1);
        }
    } else if (!strcmp(argv[1], "variants")) {
        const ULONG flags[] = {0, RADEON3D_DRAW_FRAGMENT_STATE | RADEON3D_DRAW_TEXTURED,
            RADEON3D_DRAW_EXTENDED_VERTEX | RADEON3D_DRAW_FRAGMENT_STATE | RADEON3D_DRAW_TEXTURED,
            TCL, TCL | RADEON3D_DRAW_COMPACT_VERTEX, TCL | RADEON3D_DRAW_NORMALS,
            TCL | RADEON3D_DRAW_LIGHTING, FULL, FULL | RADEON3D_DRAW_COMPACT_VERTEX};
        for (ULONG i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
            ULONG vertexState = i == 2 || i == 7 ? RADEON3D_VERTEX_TEXTURE1 | RADEON3D_VERTEX_FOG : 0;
            h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, flags[i], vertexState, 0xff);
            if (i == 7 || i == 8) CHECK(h == MAX_HEADER && h == 384);
            if (flags[i] & RADEON3D_DRAW_HW_TCL) run(r, h, &v, 1);
            n = inline_vertices(r, 3);
            ULONG emitted = run(r, n, NULL, 1);
            if (flags[i] & RADEON3D_DRAW_HW_TCL) {
                CHECK(contains_pair(emitted, RADEON_CP_PACKET0(R200_SE_VPORT_YSCALE, 0), 0xc1800000U));
                CHECK(contains_pair(emitted, RADEON_CP_PACKET0_ONE(R200_SE_TCL_VECTOR_DATA_REG, 15), r[21]));
            }
        }
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, FULL, 0, 0x81);
        r[44] = RADEON3D_TEXGEN_MODE_SPHERE_MAP | RADEON3D_TEXGEN_GEN_S | RADEON3D_TEXGEN_GEN_T;
        run(r, h, &v, 1);
        run(r, inline_vertices(r, 3), NULL, 1);
    } else if (!strcmp(argv[1], "commit")) {
        n = clear_record(r);
        h = make_header(r + n, RADEON3D_EXEC_DRAW_TRIANGLES, FULL, 0, 1); n += h;
        r[n++] = RADEON3D_EXEC_REUSE_TRIANGLES; r[n++] = RADEON3D_EXEC_REUSE_DWORDS; r[n++] = 6;
        n += make_header(r + n, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
        offsets[0] = 0; offsets[1] = 512; offsets[2] = 1024; v.drawCount = 3;
        run(r, n, &v, 1);
        r[0] = RADEON3D_EXEC_REUSE_TRIANGLES; r[1] = 3; r[2] = 3; v.drawCount = 1;
        run(r, 3, &v, 0); /* no reuse across submissions */
        n = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
        n += clear_record(r + n);
        r[n++] = RADEON3D_EXEC_REUSE_TRIANGLES; r[n++] = 3; r[n++] = 3; v.drawCount = 2;
        run(r, n, &v, 0); /* a clear is not reusable TCL state */
    } else if (!strcmp(argv[1], "state")) {
        for (ULONG op = RADEON3D_EXEC_DRAW_TRIANGLES; op <= RADEON3D_EXEC_DRAW_LINE_LOOP; ++op) {
            for (ULONG variant = 0; variant < 3; ++variant) {
                ULONG options = variant == 0 ? TCL : variant == 1 ? TCL | RADEON3D_DRAW_COMPACT_VERTEX : FULL;
                h = make_header(r, op, options, variant == 2 ? RADEON3D_VERTEX_TEXTURE1 | RADEON3D_VERTEX_FOG : 0, 0xff);
                r[10] = 0xdeadbeefU;
                v.drawCount = 3; v.stateBatch = TRUE;
                ULONG counts[3] = {3, 6, 9};
                if (op == RADEON3D_EXEC_DRAW_QUADS || op == RADEON3D_EXEC_DRAW_LINES) {
                    counts[0] = 4; counts[1] = 8; counts[2] = 12;
                }
                if (op == RADEON3D_EXEC_DRAW_POINTS) counts[0] = 1;
                for (ULONG i = 0; i < 3; ++i) { offsets[2 * i] = i * 1024; offsets[2 * i + 1] = counts[i]; }
                ULONG emitted = run(r, h, &v, 1);
                CHECK(r[10] == 0xdeadbeefU);
                memcpy(saved, output, emitted * sizeof(ULONG));
                for (ULONG i = 0; i < 3; ++i) {
                    memcpy(chain + i * h, r, h * sizeof(ULONG));
                    chain[i * h + 10] = counts[i]; offsets[i] = i * 1024;
                }
                v.stateBatch = FALSE;
                ULONG repeated = run(chain, h * 3, &v, 1);
                /* Native full-header parsing uploads lighting vectors even
                 * for equal captures; homogeneous batches avoid that work.
                 * Reuse records below are byte-identical for every variant. */
                if (options & RADEON3D_DRAW_LIGHTING) CHECK(repeated > emitted);
                else CHECK(repeated == emitted && !memcmp(saved, output, emitted * sizeof(ULONG)));
                n = h;
                for (ULONG i = 1; i < 3; ++i) {
                    chain[n++] = op - RADEON3D_EXEC_DRAW_TRIANGLES + RADEON3D_EXEC_REUSE_TRIANGLES;
                    chain[n++] = 3; chain[n++] = counts[i];
                }
                CHECK(run(chain, n, &v, 1) == emitted);
                CHECK(!memcmp(saved, output, emitted * sizeof(ULONG)));
            }
        }
        /* Both public draw-count boundaries are usable, not just rejected. */
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
        v.stateBatch = TRUE; v.drawCount = RADEON3D_STATE_BATCH_MAX_DRAWS;
        for (ULONG i = 0; i < v.drawCount; ++i) { offsets[i * 2] = 0; offsets[i * 2 + 1] = 3; }
        run(r, h, &v, 1);
        memcpy(chain, r, h * sizeof(ULONG)); n = h;
        for (ULONG i = 1; i < v.drawCount; ++i) { chain[n++] = RADEON3D_EXEC_REUSE_TRIANGLES; chain[n++] = 3; chain[n++] = 3; }
        memset(offsets, 0, sizeof(offsets)); v.stateBatch = FALSE;
        run(chain, n, &v, 1);
    } else if (!strcmp(argv[1], "immutable")) {
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, FULL, 0, 0xff);
        v.stateBatch = TRUE; offsets[1] = 3;
        size_t bytes = (size_t)sysconf(_SC_PAGESIZE);
        void *mapping = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(mapping != MAP_FAILED && bytes >= h * sizeof(ULONG));
        memcpy(mapping, r, h * sizeof(ULONG));
        CHECK(mprotect(mapping, bytes, PROT_READ) == 0);
        CHECK(MGLPPCEmitBatch(output, mapping, h, &v, resolve, surfaces) != 0);
        CHECK(munmap(mapping, bytes) == 0);
    } else if (!strcmp(argv[1], "reset")) {
        for (ULONG i = 0; i < 48; ++i) {
            ULONG options = i % 3 == 0 ? FULL : i % 3 == 1 ? TCL : TCL | RADEON3D_DRAW_LIGHTING;
            h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, options, 0, 1U << (i % 8));
            surfaces[0].gpuAddress = 0x81200000U + i * 8192;
            v.stateBatch = i % 2; offsets[1] = 3;
            run(r, h, &v, 1);
            run(r, inline_vertices(r, 3), NULL, 1);
            r[2] = 0xfedcba98U; run(r, r[1], NULL, 0);
            r[2] = COLOR; run(r, r[1], NULL, 1);
        }
    } else if (!strcmp(argv[1], "arguments")) {
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
        CHECK(!MGLPPCEmitBatch(NULL, r, h, NULL, resolve, surfaces));
        CHECK(!MGLPPCEmitBatch(output, NULL, h, NULL, resolve, surfaces));
        CHECK(!MGLPPCEmitBatch(output, r, h, NULL, NULL, surfaces));
        CHECK(!MGLPPCEmitBatch(output, r, 0, NULL, resolve, surfaces));
        CHECK(!MGLPPCEmitBatch(output, r, MGLPPC_CP_DWORDS + 1, NULL, resolve, surfaces));
        CHECK(!MGLPPCEmitBatch(output, r, UINT32_MAX, NULL, resolve, surfaces));
        for (ULONG state = 0; state < 2; ++state) {
            v.stateBatch = state;
            v.offsets = NULL; CHECK(!MGLPPCEmitBatch(output, r, h, &v, resolve, surfaces)); v.offsets = offsets;
            v.drawCount = 0; CHECK(!MGLPPCEmitBatch(output, r, h, &v, resolve, surfaces));
            v.drawCount = 193; CHECK(!MGLPPCEmitBatch(output, r, h, &v, resolve, surfaces));
            v.drawCount = UINT32_MAX; CHECK(!MGLPPCEmitBatch(output, r, h, &v, resolve, surfaces)); v.drawCount = 1;
            v.bytes = 0; CHECK(!MGLPPCEmitBatch(output, r, h, &v, resolve, surfaces));
            v.bytes = RADEON3D_MAX_SEGMENT_BYTES + 1; CHECK(!MGLPPCEmitBatch(output, r, h, &v, resolve, surfaces)); v.bytes = 65536;
            v.gpuAddress = 0x93400001U; CHECK(!MGLPPCEmitBatch(output, r, h, &v, resolve, surfaces));
            v.gpuAddress = 0xfffffff0U; CHECK(!MGLPPCEmitBatch(output, r, h, &v, resolve, surfaces)); v.gpuAddress = 0x93400000U;
        }
        CHECK(resolveCalls == 0);
    } else if (!strcmp(argv[1], "chains")) {
        const ULONG lengths[] = {0, 1, 2, 10, 12, UINT32_MAX};
        for (ULONG i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            clear_record(r); r[1] = lengths[i]; run(r, 11, NULL, 0);
        }
        clear_record(r); run(r, 1, NULL, 0); run(r, 10, NULL, 0);
        r[11] = RADEON3D_EXEC_CLEAR; run(r, 12, NULL, 0);
        r[0] = 0xfeedfaceU; run(r, 11, NULL, 0);
        r[0] = RADEON3D_EXEC_DRAW_TRIANGLES; r[1] = 2; run(r, 2, &v, 0);
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
        v.drawCount = 2; run(r, h, &v, 0);
        v.drawCount = 1; memcpy(r + h, r, h * sizeof(ULONG)); run(r, h * 2, &v, 0);
        r[h] = RADEON3D_EXEC_REUSE_TRIANGLES; r[h + 1] = 2;
        v.drawCount = 2; run(r, h + 2, &v, 0);
        r[h + 1] = 3; r[h + 2] = 3; run(r, h + 3, NULL, 0);
        n = make_header(r, RADEON3D_EXEC_DRAW_POINTS, 0, 0, 0);
        n = inline_vertices(r, 1);
        for (ULONG i = 0; i < 193; ++i) memcpy(chain + i * n, r, n * sizeof(ULONG));
        run(chain, n * 192, NULL, 1); run(chain, n * 193, NULL, 0);
        v.stateBatch = TRUE; v.drawCount = 1; offsets[1] = 3;
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
        r[1] = h - 1; run(r, h, &v, 0); r[1] = h;
        run(r, h - 1, &v, 0); r[0] = RADEON3D_EXEC_CLEAR; run(r, h, &v, 0);
        r[0] = RADEON3D_EXEC_REUSE_TRIANGLES; run(r, h, &v, 0);
        make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, FULL, 0, 0xff);
        r[1] = MAX_HEADER + 1; run(r, MAX_HEADER + 1, &v, 0);
        r[1] = MGLPPC_CP_DWORDS; run(r, MGLPPC_CP_DWORDS, &v, 0);
    } else if (!strcmp(argv[1], "descriptors")) {
        const ULONG fields[] = {0, 1, 2, 3, 4, 5, 2, 2, 2, 3, 4, 2, 0, 1, 3, 4};
        const ULONG values[] = {0, 0x81200001U, 0, 0, 0, 99, 1, 128, 255,
            65537, 65537, 0xffffffc0U, 0xfffffff0U, 0xffffffc0U, UINT32_MAX, UINT32_MAX};
        for (ULONG i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
            init_surfaces();
            ((ULONG *)&surfaces[0])[fields[i]] = values[i];
            run(r, clear_record(r), NULL, 0);
        }
        init_surfaces(); clear_record(r); r[2] = 0xfedcba98U; run(r, 11, NULL, 0);
        r[2] = 0; run(r, 11, NULL, 0);
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, FULL, RADEON3D_VERTEX_TEXTURE1, 0);
        for (ULONG field = 2; field <= 4; ++field) {
            ULONG value = r[field]; r[field] = 0xfedcba98U; run(r, h, &v, 0); r[field] = value;
        }
        r[15] = 0xfedcba98U; run(r, h, &v, 0); r[15] = TEX1;
        /* The fragment texture path divides by pitch and reads the last
         * texel. Bad resolver results must fail before either operation. */
        surfaces[TEX0 - 1].pitch = 0; run(r, h, &v, 0);
        init_surfaces(); surfaces[TEX0 - 1].cpuAddress = 0; run(r, h, &v, 0);
        init_surfaces(); surfaces[TEX0 - 1].width = 65; run(r, h, &v, 0);
        init_surfaces(); surfaces[TEX0 - 1].gpuAddress = surfaces[0].gpuAddress; run(r, h, &v, 0);
    } else if (!strcmp(argv[1], "offsets")) {
        for (ULONG state = 0; state < 2; ++state) {
            h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
            v.stateBatch = state; v.drawCount = 2; offsets[0] = 0;
            if (!state) { memcpy(r + h, r, h * sizeof(ULONG)); n = h * 2; offsets[1] = 120; }
            else { n = h; offsets[1] = 3; offsets[2] = 120; offsets[3] = 6; }
            ULONG secondOffset = state ? 2 : 1;
            const ULONG bad[] = {1, 2, 3, 65536, UINT32_MAX};
            for (ULONG i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
                offsets[secondOffset] = bad[i]; run(r, n, &v, 0);
            }
            offsets[secondOffset] = 65532; run(r, n, &v, 0);
            offsets[secondOffset] = 120;
            const ULONG counts[] = {0, 1, 2, 4, 65536, UINT32_MAX};
            for (ULONG draw = 0; draw < 2; ++draw) {
                ULONG *count = state ? &offsets[draw * 2 + 1] : &r[draw * h + 10];
                for (ULONG i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) { *count = counts[i]; run(r, n, &v, 0); }
                *count = 3;
            }
            run(r, n, &v, 1);
        }
        v.drawCount = 1; v.stateBatch = FALSE;
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL | RADEON3D_DRAW_COMPACT_VERTEX, 0, 0);
        offsets[0] = v.bytes - 3 * 7 * 4; run(r, h, &v, 1);
        offsets[0] += 4; run(r, h, &v, 0);
        v.gpuAddress = 0xffff0000U; offsets[0] -= 4; run(r, h, &v, 1);
    } else if (!strcmp(argv[1], "capacity")) {
        ULONG one = run(r, clear_record(r), NULL, 1);
        clear_record(r + 11); ULONG two = run(r, 22, NULL, 1);
        ULONG last = 1 + (MGLPPC_CP_DWORDS - one) / (two - one);
        CHECK((last + 1) * 11 <= MGLPPC_CP_DWORDS);
        for (ULONG i = 0; i <= last; ++i) clear_record(r + i * 11);
        run(r, last * 11, NULL, 1);
        ULONG before = published; run(r, (last + 1) * 11, NULL, 0); CHECK(published == before);
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
        v.drawCount = 150;
        for (ULONG i = 0; i < v.drawCount; ++i) { memcpy(chain + i * h, r, h * sizeof(ULONG)); chain[i * h + 6] = i % 2; }
        run(chain, h * v.drawCount, &v, 0);
        CHECK(partialFailures >= 2);
    } else if (!strcmp(argv[1], "partial")) {
        h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES, TCL, 0, 0);
        memcpy(r + h, r, h * sizeof(ULONG)); r[h + 2] = 99; v.drawCount = 2;
        ULONG before = published;
        run(r, h * 2, &v, 0);
        v.stateBatch = TRUE; offsets[1] = 3; offsets[2] = 120; offsets[3] = 4;
        run(r, h, &v, 0);
        n = inline_vertices(r, 3); r[n - 10] = 0x7f800000U; run(r, n, NULL, 0);
        CHECK(published == before && partialFailures == 3);
        r[n - 10] = 0xbf400000U; run(r, n, NULL, 1);
    } else if (!strcmp(argv[1], "mutations")) {
        ULONG random = 0x13579bdfU;
        for (ULONG iteration = 0; iteration < 4000; ++iteration) {
            BOOL state = iteration % 3 == 0, commit = iteration % 3 != 2;
            h = make_header(r, RADEON3D_EXEC_DRAW_TRIANGLES,
                iteration & 1 ? FULL | RADEON3D_DRAW_COMPACT_VERTEX : TCL, 0, 0xff);
            n = commit ? h : inline_vertices(r, 3);
            v.stateBatch = state; v.drawCount = 1; offsets[0] = 0; offsets[1] = 3;
            random = random * 1664525U + 1013904223U;
            if (iteration % 5 == 0) n = 1 + random % n;
            else if (iteration % 5 == 1 && commit) offsets[state ? random % 2 : 0] = random;
            else r[random % n] ^= 1U << (random % 32);
            run(r, n, commit ? &v : NULL, -1);
        }
    } else CHECK(!"unknown case");
    printf("PASS %s\n", argv[1]);
    return 0;
}
'''


class SharedEmitter(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix="r200-cp-emit-", dir="/tmp/opencode")
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        (cls.directory / "exec").mkdir()
        (cls.directory / "exec/types.h").write_text(TYPES)
        public = (DRIVER / "include/radeon3d.h").read_text()
        # Only Amiga public structs containing native pointers change size on
        # this 64-bit host. Keep fixed-width and prefix checks fully active.
        for name in ("Radeon3DSegmentV1SizeCheck", "Radeon3DCommitV1SizeCheck",
                     "Radeon3DCommitBatchV1SizeCheck", "Radeon3DStateBatchV1SizeCheck",
                     "Radeon3DInfoV4SizeCheck", "Radeon3DSurfaceV1SizeCheck"):
            public, count = re.subn(rf"typedef char {name}\[[^;]+;",
                f"/* Host-only pointer ABI: {name}. */", public)
            if count != 1:
                raise AssertionError(f"Public ABI changed: expected one {name}")
        cls.public = cls.directory / "host_radeon3d.h"
        cls.public.write_text(public)
        source = cls.directory / "test.c"
        source.write_text(HARNESS + reference_builders() + CASES)
        cls.binary = cls.directory / "test"
        cls.flags = ["cc", "-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                     "-fno-pie", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                     "-fno-sanitize-recover=all", "-I", str(cls.directory), "-I", str(INCLUDE),
                     "-I", str(DRIVER / "include"), "-include", str(cls.public)]
        # Separate .o files also make accidental unity inclusion or a copied
        # emitter implementation fail at link time through duplicate symbols.
        objects = []
        for name, path in (("adapter", ADAPTER), ("emitter", DRIVER / "src/radeon3d_emit.c"),
                           ("test", source)):
            obj = cls.directory / f"{name}.o"
            result = subprocess.run(cls.flags + ["-c", str(path), "-o", str(obj)],
                capture_output=True, text=True, timeout=60, cwd=cls.directory)
            if result.returncode or result.stderr:
                raise AssertionError(f"{name} compilation failed:\n{result.stdout}{result.stderr}")
            objects.append(str(obj))
        result = subprocess.run(["cc", "-no-pie", "-fsanitize=address,undefined", *objects,
                                 "-o", str(cls.binary)], capture_output=True, text=True,
                                timeout=60, cwd=cls.directory)
        if result.returncode or result.stderr:
            raise AssertionError(f"Host link failed:\n{result.stdout}{result.stderr}")

    def case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True,
            timeout=30, cwd=self.directory, env={**os.environ,
                "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout, f"PASS {name}\n")

    def test_inline_clear_and_untextured_host_order(self):
        self.case("inline")

    def test_real_tcl_compact_texgen_normals_lighting(self):
        self.case("variants")

    def test_commit_reference_and_clear_reuse_boundaries(self):
        self.case("commit")

    def test_state_reference_commit_and_reuse_equivalence(self):
        self.case("state")

    def test_read_only_state_header(self):
        self.case("immutable")

    def test_per_batch_reset_and_resolver_changes(self):
        self.case("reset")

    def test_invalid_arguments(self):
        self.case("arguments")

    def test_bounded_record_chains_and_counts(self):
        self.case("chains")

    def test_unknown_handles_and_invalid_descriptors(self):
        self.case("descriptors")

    def test_vertex_offsets_counts_and_32bit_overflow(self):
        self.case("offsets")

    def test_output_capacity(self):
        self.case("capacity")

    def test_partial_failures_never_publish_a_count(self):
        self.case("partial")

    def test_sanitized_record_and_offset_mutations(self):
        self.case("mutations")

    def test_public_header_isolation_and_fixed32_layout(self):
        source = self.directory / "header.c"
        source.write_text(r'''
#include <stddef.h>
#include <libraries/minigl_ppc_cp_emit.h>
#ifdef RADEON3D_EMIT_H
#error emitter internals leaked into frontend unity
#endif
static BOOL ValidFloat(ULONG bits) { return bits != 0; }
typedef char CheckCapacity[MGLPPC_CP_DWORDS == 8192 ? 1 : -1];
typedef char CheckSurface[sizeof(struct MGLPPCEmitSurface) == 24 ? 1 : -1];
typedef char CheckFormat[offsetof(struct MGLPPCEmitSurface, format) == 20 ? 1 : -1];
typedef char CheckVertices[offsetof(struct MGLPPCEmitVertices, stateBatch) == 12 ? 1 : -1];
BOOL probe(void) { return ValidFloat(1); }
''')
        result = subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
            "-I", str(self.directory), "-I", str(INCLUDE), "-fsyntax-only", str(source)],
            capture_output=True, text=True, timeout=30, cwd=self.directory)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")

    def test_driver_capacity_mismatch_is_a_compile_error(self):
        mismatch = self.directory / "capacity.h"
        mismatch.write_text('#include "host_radeon3d.h"\n'
                            '#undef RADEON3D_MAX_BATCH_DWORDS\n'
                            '#define RADEON3D_MAX_BATCH_DWORDS 8193UL\n')
        result = subprocess.run(self.flags + ["-include", str(mismatch), "-fsyntax-only", str(ADAPTER)],
            capture_output=True, text=True, timeout=30, cwd=self.directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MGLPPCCapacityCheck", result.stderr)


if __name__ == "__main__":
    unittest.main()
