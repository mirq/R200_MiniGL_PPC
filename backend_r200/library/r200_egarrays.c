/*
 * r200_egarrays.c -- Evergreen: glDrawElements and glDrawArrays straight
 * from the client's vertex arrays (included by r200_geometry.c).
 *
 * The array bytes the draw uses go into VRAM as the client holds them
 * (big-endian floats, RGBA bytes), one copy over the bus, at the call; the
 * GPU fetches every attribute from its own buffer in the client's layout
 * and walks the client's indices (DRAW_INDEX). No struct Vertex, no record
 * vertex tail, no per-vertex CPU work. The draw state is the hardware-TCL
 * capture every other draw uses; the record is EG_EXEC_ARRAYS (eg_emit.h).
 *
 * Where the data goes: a ring in a Picasso96 bitmap, one part per submit
 * slot. Submit retires the next slot before building into it, so the part
 * of the slot being built is never read by the GPU while it is written.
 *
 * Anything the path does not cover (lighting, fog, texgen, texture
 * matrices, other array types, lines and points) returns FALSE before
 * touching any state, and the caller draws it the old way.
 */

#define EG_RING_SLOT_BYTES   0x80000UL     /* 512 KB per submit slot: */
#define EG_RING_DATA_BYTES   0x70000UL     /*   arrays, constants, vertices */
#define EG_RING_INDEX_BYTES  0x10000UL     /*   immediate draws' indices */
#define EG_CACHE_BYTES       0x400000UL    /* 4 MB of resident arrays */
#define EG_CACHE_SLOTS       2048UL        /* a power of two */
#define EG_CACHE_PROBE       8UL

/* RadeonGL's array hints (GL_RGL_static_arrays, GL_RGL_dynamic_arrays,
 * GL_RGL_arrays_tag; its README): under the static hints the program
 * promises the bytes at an address do not change, so an array span or an
 * index list is keyed by its address, length and the tag and copied into
 * VRAM once. GL_FASTEST forgets every key. */
#define EG_HINT_STATIC_ARRAYS   0x52474c01UL
#define EG_HINT_STATIC_INDICES  0x52474c02UL
#define EG_HINT_DYNAMIC_ARRAYS  0x52474c03UL
#define EG_HINT_ARRAYS_TAG      0x52474c04UL

struct EgCacheEntry {
    const UBYTE *start;
    ULONG bytes, tag, gen;
    ULONG off;                  /* in the cache bitmap; ~0 = empty */
};

#define EG_ARRAYS_GENERATED  352UL         /* worst case command dwords a draw */

static void EgDiagSurface(const char *what, const struct Radeon3DSurface *s, ULONG bytes)
{
    dbg_puts("[egarrays] ");
    dbg_puts(what);
    dbg_puts(" gpu=");
    dbg_hex32(s->GpuAddress);
    dbg_puts(" end=");
    dbg_hex32(s->GpuAddress + bytes);
    dbg_newline();
}

/* VRAM range [start, end) of a Picasso96 bitmap, 0 if none */
static ULONG EgBitMapRange(struct BitMap *bm, ULONG *end)
{
    ULONG mem, bytes;

    *end = 0;
    if (!bm || !p96GetBitMapAttr(bm, P96_BITMAP_IS_P96))
        return 0;
    mem = p96GetBitMapAttr(bm, P96_BITMAP_MEMORY);
    bytes = p96GetBitMapAttr(bm, P96_BITMAP_BYTES_PER_ROW) *
            p96GetBitMapAttr(bm, P96_BITMAP_HEIGHT);
    *end = mem + bytes;
    return mem;
}

/* Does [start, end) (CPU addresses) overlap a screen buffer, a render
 * target or the depth buffer? The fullscreen buffers are not Picasso96
 * allocations, so its allocator hands their memory out again. */
static BOOL EgCollides(struct MiniGLR200Context *context, ULONG start, ULONG end)
{
    ULONG i, a, b;

    for (i = 0; i < 3UL; ++i) {
        const struct Radeon3DSurface *t = &context->surfaces[i];
        if (t->CpuAddress) {
            a = (ULONG)t->CpuAddress;
            b = a + t->Pitch * t->Height;
            if (start < b && a < end)
                return TRUE;
        }
        if (context->screenBuffers[i]) {
            a = EgBitMapRange(context->screenBuffers[i]->sb_BitMap, &b);
            if (a && start < b && a < end)
                return TRUE;
        }
    }
    if (context->screen) {
        a = EgBitMapRange(context->screen->RastPort.BitMap, &b);
        if (a && start < b && a < end)
            return TRUE;
    }
    if (context->depthSurface.CpuAddress) {
        a = (ULONG)context->depthSurface.CpuAddress;
        b = a + context->depthSurface.Pitch * context->depthSurface.Height;
        if (start < b && a < end)
            return TRUE;
    }
    return FALSE;
}

/* A CPU-visible VRAM block of bytes (an ARGB bitmap 1024 wide) clear of
 * every surface EgCollides knows; rejected placements are held while
 * retrying so the allocator moves on. */
static struct BitMap *EgAllocBlock(struct MiniGLR200Context *context, ULONG bytes,
                                   struct Radeon3DSurface *surface)
{
    struct BitMap *rejects[12], *bm = NULL;
    ULONG n = 0, start;

    while (n < 12UL) {
        bm = AllocateArgbBitMap(1024UL, bytes / 4096UL);
        if (!bm)
            break;
        ZeroMemory(surface, sizeof(*surface));
        surface->Size = sizeof(*surface);
        if (!Radeon3DImportBitMap(context->device, bm, surface) || !surface->CpuAddress ||
            surface->Pitch * surface->Height < bytes) {
            p96FreeBitMap(bm);
            bm = NULL;
            break;
        }
        start = (ULONG)surface->CpuAddress;
        if (!EgCollides(context, start, start + bytes))
            break;
        dbg_puts("[egarrays] placement collides at ");
        dbg_hex32(surface->GpuAddress);
        dbg_puts(", retrying\n");
        rejects[n++] = bm;
        bm = NULL;
    }
    while (n)
        p96FreeBitMap(rejects[--n]);
    return bm;
}

static ULONG EgMask = 31;

static BOOL EgRingReady(struct MiniGLR200Context *context)
{
    ULONG index;

    if (context->egRingBitMap && context->egRingEpoch == context->deviceEpoch)
        return TRUE;
    {
        /* MGLEgArrays=0: the old path; MGLEgCache=0: no resident copies */
        char v[8];
        if (GetVar("MGLEgArrays", v, sizeof(v), 0) > 0 && v[0] == '0') {
            context->egArraysOff = TRUE;
            return FALSE;
        }
        if (GetVar("MGLEgCache", v, sizeof(v), 0) > 0 && v[0] == '0')
            context->egCacheOff = TRUE;
        /* MGLEgMask: bit 0 ARGB colours, 1 other colour arrays, 2 constant
         * colour, 3 indexed, 4 glDrawArrays (default all) */
        EgMask = 31;
        if (GetVar("MGLEgMask", v, sizeof(v), 0) > 0)
            EgMask = (ULONG)(v[0] - '0') * (v[1] >= '0' ? 10UL : 1UL) +
                     (v[1] >= '0' ? (ULONG)(v[1] - '0') : 0UL);
    }
    if (context->egRingBitMap) {
        p96FreeBitMap(context->egRingBitMap);
        context->egRingBitMap = NULL;
    }
    context->egRingBitMap = EgAllocBlock(context, SUBMIT_SLOT_COUNT * EG_RING_SLOT_BYTES,
                                         &context->egRing);
    if (!context->egRingBitMap) {
        context->egArraysOff = TRUE;
        return FALSE;
    }
    context->egRingEpoch = context->deviceEpoch;
    EgDiagSurface("ring", &context->egRing, SUBMIT_SLOT_COUNT * EG_RING_SLOT_BYTES);
    for (index = 0; index < 3UL; ++index)
        if (context->surfaces[index].CpuAddress)
            EgDiagSurface("target", &context->surfaces[index],
                          context->surfaces[index].Pitch * context->surfaces[index].Height);
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index) {
        context->egRingSerial[index] = 0;
        context->egRingCursor[index] = 0;
    }
    return TRUE;
}

static void EgCopyLongs(volatile ULONG *dst, const ULONG *src, ULONG longs);

BOOL EgArraysHint(struct MiniGLR200Context *context, GLenum target, GLenum mode)
{
    if (target < EG_HINT_STATIC_ARRAYS || target > EG_HINT_ARRAYS_TAG)
        return FALSE;
    if (target == EG_HINT_ARRAYS_TAG) {
        context->egTag = mode;
        return TRUE;
    }
    if (mode != GL_NICEST && mode != GL_DONT_CARE && mode != GL_FASTEST) {
        SetError(context, GL_INVALID_ENUM);
        return TRUE;
    }
    if (target == EG_HINT_DYNAMIC_ARRAYS) {
        context->egDynamic = mode == GL_NICEST;
        return TRUE;
    }
    if (target == EG_HINT_STATIC_ARRAYS)
        context->egStaticArrays = mode == GL_NICEST;
    else
        context->egStaticIndices = mode == GL_NICEST;
    if (mode == GL_FASTEST)
        context->egCacheGen++;  /* every address key, arrays and indices */
    return TRUE;
}

static void EgCacheClear(struct MiniGLR200Context *context)
{
    ULONG index;

    for (index = 0; index < EG_CACHE_SLOTS; ++index)
        context->egCache[index].off = 0xffffffffUL;
    context->egCacheTop = 0;
}

static BOOL EgCacheReady(struct MiniGLR200Context *context)
{
    if (context->egCacheBitMap && context->egCacheEpoch == context->deviceEpoch)
        return TRUE;
    if (context->egCacheOff)
        return FALSE;
    if (!context->egCache) {
        context->egCache = AllocMem(EG_CACHE_SLOTS * sizeof(struct EgCacheEntry), MEMF_PUBLIC);
        if (!context->egCache) {
            context->egCacheOff = TRUE;
            return FALSE;
        }
    }
    if (context->egCacheBitMap) {
        p96FreeBitMap(context->egCacheBitMap);
        context->egCacheBitMap = NULL;
    }
    context->egCacheBitMap = EgAllocBlock(context, EG_CACHE_BYTES, &context->egCacheSurface);
    if (!context->egCacheBitMap) {
        context->egCacheOff = TRUE;
        return FALSE;
    }
    context->egCacheEpoch = context->deviceEpoch;
    EgDiagSurface("cache", &context->egCacheSurface, EG_CACHE_BYTES);
    EgCacheClear(context);
    return TRUE;
}

static ULONG EgMissBytes[2], EgMissCount[2], EgHitCount[2];

static ULONG EgCacheHash(const UBYTE *start, ULONG bytes, ULONG tag, ULONG gen)
{
    ULONG h = (ULONG)start * 2654435761UL;
    h ^= bytes * 40503UL + tag * 2246822519UL + gen * 3266489917UL;
    return (h ^ (h >> 15)) & (EG_CACHE_SLOTS - 1UL);
}

/* The GPU address of a resident copy of bytes at start, copied in on a
 * miss; 0: not cacheable now (the caller streams it). A full cache is
 * emptied once every batch that may read it has retired. */
/* A place for interleaved records from off on (4-aligned) where the first
 * record (delta bytes in) lies a whole number of records from the start of
 * its 16 MB block: the emitter then shares one vertex buffer resource among
 * all draws of that stride (ArraysTail, eg_emit.c). */
static ULONG EgAlignFor(ULONG gpuBase, ULONG off, ULONG delta, ULONG stride)
{
    ULONG k;

    off = (off + 3UL) & ~3UL;
    if (stride < 8UL || (stride & 3UL) || (delta & 3UL))
        return off;
    for (k = 0; k < stride && ((gpuBase + off + delta) & 0xffffffUL) % stride; k += 4UL)
        off += 4UL;
    return off;
}

static ULONG EgCacheGet(struct MiniGLR200Context *context, const UBYTE *start, ULONG bytes,
                        BOOL tagged, ULONG delta, ULONG stride)
{
    struct EgCacheEntry *e, *free = NULL;
    ULONG tag = tagged ? context->egTag : 0, gen = context->egCacheGen, h, probe, off;

    if (!EgCacheReady(context) || bytes > EG_CACHE_BYTES / 4UL)
        return 0;
    h = EgCacheHash(start, bytes, tag, gen);
    for (probe = 0; probe < EG_CACHE_PROBE; ++probe) {
        e = &context->egCache[(h + probe) & (EG_CACHE_SLOTS - 1UL)];
        if (e->off == 0xffffffffUL) {
            if (!free)
                free = e;
            continue;
        }
        if (e->start == start && e->bytes == bytes && e->tag == tag && e->gen == gen) {
            context->egCacheHits++;
            EgHitCount[tag ? 1 : 0]++;
            return context->egCacheSurface.GpuAddress + e->off;
        }
        if (!free && e->gen != gen)
            free = e;               /* a forgotten key's slot */
    }
    off = EgAlignFor(context->egCacheSurface.GpuAddress, context->egCacheTop, delta, stride);
    if (!free || off + bytes > EG_CACHE_BYTES) {
        /* full: empty it behind every batch that may read it */
        if (!Submit(context) || !WaitAllSubmissions(context))
            return 0;
        EgCacheClear(context);
        context->egCacheFlushes++;
        free = &context->egCache[h];
        off = EgAlignFor(context->egCacheSurface.GpuAddress, 0, delta, stride);
    }
    EgCopyLongs((volatile ULONG *)((UBYTE *)context->egCacheSurface.CpuAddress + off),
                (const ULONG *)start, (bytes + 3UL) >> 2);
    free->start = start;
    free->bytes = bytes;
    free->tag = tag;
    free->gen = gen;
    free->off = off;
    context->egCacheTop = off + bytes;
    context->egCacheMisses++;
    EgMissBytes[tag ? 1 : 0] += bytes;
    EgMissCount[tag ? 1 : 0]++;
    return context->egCacheSurface.GpuAddress + off;
}

static ULONG EgRingAllocFor(struct MiniGLR200Context *context, ULONG bytes,
                            volatile ULONG **cpu, ULONG delta, ULONG stride);

/* The build slot's ring part starts empty once per batch. */
static ULONG EgRingUsed(struct MiniGLR200Context *context)
{
    ULONG slot = context->buildSlot;

    if (context->egRingSerial[slot] != context->batchSerial) {
        context->egRingSerial[slot] = context->batchSerial;
        context->egRingCursor[slot] = 0;
        context->egImmIdxCursor[slot] = 0;
    }
    return context->egRingCursor[slot];
}

/* bytes (a multiple of 4) of the build slot's part: GPU address, CPU
 * address in *cpu. The caller checked the room (EgRingUsed). */
static ULONG EgRingAlloc(struct MiniGLR200Context *context, ULONG bytes,
                         volatile ULONG **cpu)
{
    return EgRingAllocFor(context, bytes, cpu, 0, 0);
}

static ULONG EgRingAllocFor(struct MiniGLR200Context *context, ULONG bytes,
                            volatile ULONG **cpu, ULONG delta, ULONG stride)
{
    ULONG slot = context->buildSlot;
    ULONG pos = stride ? EgAlignFor(context->egRing.GpuAddress + slot * EG_RING_SLOT_BYTES,
                                    EgRingUsed(context), delta, stride)
                       : (EgRingUsed(context) + 31UL) & ~31UL;
    ULONG off = slot * EG_RING_SLOT_BYTES + pos;

    context->egRingCursor[slot] = pos + bytes;
    *cpu = (volatile ULONG *)((UBYTE *)context->egRing.CpuAddress + off);
    return context->egRing.GpuAddress + off;
}

/* Longword stores only: no CopyMem or MOVE16 into VRAM. */
static ULONG EgCopiedLongs, EgImmLongs;
static void EgCopyLongs(volatile ULONG *dst, const ULONG *src, ULONG longs)
{
    EgCopiedLongs += longs;
    while (longs >= 4UL) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        dst += 4;
        src += 4;
        longs -= 4UL;
    }
    while (longs--)
        *dst++ = *src++;
}

struct EgArrayRegion {
    const UBYTE *start;
    ULONG bytes;
    ULONG gpu;
};

static BOOL EgArrayUsable(const struct ArrayState *a, ULONG minSize, ULONG maxSize)
{
    return a->type == GL_FLOAT && (ULONG)a->size >= minSize && (ULONG)a->size <= maxSize &&
           !(((ULONG)a->pointer | a->stride) & 3UL) && a->stride <= 2040UL;
}

/* why draws took the old path: (source line, count) */
ULONG EgSubmitWhy[4];
ULONG EgLmBytes, EgLmChanged, EgLmRowsSame;
static ULONG EgFailLine[16], EgFailCount[16], EgDrawn;
static void EgFailNote(ULONG line)
{
    ULONG i;
    for (i = 0; i < 16UL; ++i)
        if (EgFailLine[i] == line || !EgFailLine[i]) {
            EgFailLine[i] = line;
            EgFailCount[i]++;
            return;
        }
}
#define EG_FAIL() do { EgFailNote(__LINE__); return FALSE; } while (0)

static BOOL EgArraysDraw(struct MiniGLR200Context *context, GLenum mode,
                         ULONG first, ULONG count, const UBYTE *indices, GLenum type)
{
    const struct ArrayState *va = &context->vertexArray;
    const struct ArrayState *ca = &context->colorArray;
    const struct ArrayState *ta[2];
    struct PendingDrawState *state;
    struct SubmitSlot *slot;
    struct EgArrayRegion region[4];
    const struct ArrayState *regionArray[4];
    ULONG regions = 0, prim, lo, hi, n, i, colFormat = EGA_COL_UBYTE4, need, ringBytes;
    ULONG indexBytes = 0, indexType = EGA_INDEX_NONE, gpu[4], stride[4], *record, *ga;
    ULONG cached[4], indexCached = 0, alignDelta = 0, alignStride = 0;
    BOOL haveColor, haveTex[2], merged;
    volatile ULONG *cpu;

    if (context->egArraysOff || !context->device || context->insideBegin)
        EG_FAIL();
    switch (mode) {
    case GL_TRIANGLES:      prim = RADEON3D_EXEC_DRAW_TRIANGLES; break;
    case GL_TRIANGLE_STRIP: prim = RADEON3D_EXEC_DRAW_TRI_STRIP; break;
    case GL_TRIANGLE_FAN:   prim = RADEON3D_EXEC_DRAW_TRI_FAN; break;
    case GL_QUADS:          prim = RADEON3D_EXEC_DRAW_QUADS; break;
    case GL_POLYGON:
        /* a fan; flat shading would take each triangle's last vertex */
        if (context->shadeModel == GL_FLAT)
            EG_FAIL();
        prim = RADEON3D_EXEC_DRAW_TRI_FAN;
        break;
    default:
        EG_FAIL();
    }
    ta[0] = &context->texcoordArray;
    ta[1] = &context->texcoord1Array;
    if (context->lightingEnabled || context->fogEnabled || context->paletteLookup ||
        context->textureMatrixActive[0] || context->textureMatrixActive[1] ||
        context->polygonMode != GL_FILL ||
        (context->normalArray.enabled && context->normalArray.pointer) ||
        !va->enabled || !va->pointer || !EgArrayUsable(va, 2, 4))
        EG_FAIL();
    if (context->textureGenS[0] || context->textureGenT[0] ||
        context->textureGenS[1] || context->textureGenT[1]) {
        ULONG hardwareSphereMap = HardwareSphereMapMask(context);
        if (hardwareSphereMap || SoftwareSphereMapMask(context, hardwareSphereMap))
            EG_FAIL();
    }
    haveColor = ca->enabled && ca->pointer;
    if (haveColor) {
        if (((ULONG)ca->pointer | ca->stride) & 3UL || ca->stride > 2040UL)
            EG_FAIL();
        if (ca->type == GL_UNSIGNED_BYTE && ca->size == 4)
            colFormat = EGA_COL_UBYTE4;
        else if (ca->type == MGL_UBYTE_ARGB)
            colFormat = EGA_COL_ARGB;
        else if (ca->type == GL_FLOAT && ca->size == 3)
            colFormat = EGA_COL_FLOAT3;
        else if (ca->type == GL_FLOAT && ca->size == 4)
            colFormat = EGA_COL_FLOAT4;
        else
            EG_FAIL();
    }
    for (i = 0; i < 2UL; ++i) {
        haveTex[i] = ta[i]->enabled && ta[i]->pointer;
        if (haveTex[i] && !EgArrayUsable(ta[i], 2, 3))
            EG_FAIL();
    }

    if (!(EgMask & (haveColor ? (colFormat == EGA_COL_ARGB ? 1UL : 2UL) : 4UL)) ||
        !(EgMask & (indices ? 8UL : 16UL)))
        EG_FAIL();
    /* the element range the draw reads */
    if (indices) {
        if (context->lockedCount) {
            lo = context->lockedFirst;
            hi = lo + context->lockedCount;
        } else {
            lo = 0xffffffffUL;
            hi = 0;
            for (i = 0; i < count; ++i) {
                ULONG v = type == GL_UNSIGNED_SHORT ? ((const UWORD *)indices)[i]
                        : type == GL_UNSIGNED_BYTE ? indices[i]
                        : ((const ULONG *)indices)[i];
                if (v < lo) lo = v;
                if (v >= hi) hi = v + 1UL;
            }
        }
        indexType = type == GL_UNSIGNED_INT ? EGA_INDEX_32 : EGA_INDEX_16;
        indexBytes = type == GL_UNSIGNED_INT ? count * 4UL : ((count + 1UL) & ~1UL) * 2UL;
        if ((type != GL_UNSIGNED_BYTE && ((ULONG)indices & 1UL)) ||
            (type == GL_UNSIGNED_INT && ((ULONG)indices & 3UL)))
            EG_FAIL();
    } else {
        lo = first;
        hi = first + count;
    }
    if (hi <= lo)
        return TRUE;
    n = hi - lo;

    /* the regions to copy: one per array, merged when they interleave */
    regionArray[regions] = va;
    region[regions].start = va->pointer + lo * va->stride;
    region[regions++].bytes = (n - 1UL) * va->stride + (ULONG)va->size * 4UL;
    if (haveColor) {
        regionArray[regions] = ca;
        region[regions].start = ca->pointer + lo * ca->stride;
        region[regions++].bytes = (n - 1UL) * ca->stride +
                                  (colFormat == EGA_COL_FLOAT3 ? 12UL :
                                   colFormat == EGA_COL_FLOAT4 ? 16UL : 4UL);
    }
    for (i = 0; i < 2UL; ++i)
        if (haveTex[i]) {
            regionArray[regions] = ta[i];
            region[regions].start = ta[i]->pointer + lo * ta[i]->stride;
            region[regions++].bytes = (n - 1UL) * ta[i]->stride + 8UL;
        }
    {
        const UBYTE *u0 = region[0].start, *u1 = region[0].start + region[0].bytes;
        ULONG sum = 0;
        for (i = 0; i < regions; ++i) {
            if (region[i].start < u0) u0 = region[i].start;
            if (region[i].start + region[i].bytes > u1) u1 = region[i].start + region[i].bytes;
            sum += region[i].bytes;
        }
        /* interleaved arrays share their bytes: one copy of the span */
        merged = (ULONG)(u1 - u0) <= sum;
        ringBytes = merged ? (ULONG)(u1 - u0) + 32UL : sum + regions * 32UL;
        if (merged) {
            region[0].start = u0;
            region[0].bytes = (ULONG)(u1 - u0);
        }
    }
    ringBytes += indexBytes + 32UL + 3UL * 32UL;   /* indices, constants */
    if (ringBytes > EG_RING_DATA_BYTES)
        EG_FAIL();
    if (!EgRingReady(context))
        EG_FAIL();

    /* the draw state, as every hardware-TCL draw captures it */
    if (context->screenVertexCount && !FlushScreenVertices(context))
        EG_FAIL();
    context->screenHardwareTransform = TRUE;
    context->screenProjective = FALSE;
    context->egCaptureOnly = TRUE;
    {
    EGT_BEGIN();
    i = PrepareScreenAppend(context, prim);
    EGT_END(8);
    }
    context->egCaptureOnly = FALSE;
    context->screenHardwareTransform = FALSE;
    if (!i)
        EG_FAIL();
    state = context->egCaptured;
    if (!state->drawEnabled || !state->drawRectangleValid)
        return TRUE;                /* drawn as nothing, as the old path does */
    if (!state->hardwareTransform || state->normalVertex || state->lit || state->fog ||
        state->hardwareSphereMap ||
        state->headerDwords != RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS ||
        (state->header[5] & (RADEON3D_DRAW_TEXGEN | RADEON3D_DRAW_LIGHTING |
                             RADEON3D_DRAW_PALETTE | RADEON3D_DRAW_NORMALS)))
        EG_FAIL();

    /* interleaved: where the first record lies in the region */
    if (merged) {
        alignStride = regionArray[0]->stride;
        alignDelta = (ULONG)((regionArray[0]->pointer + lo * regionArray[0]->stride) -
                             region[0].start);
    }
    /* resident copies under the static hints (before the room check:
     * a full cache is emptied behind a submit) */
    {
        EGT_BEGIN();
        ULONG pass, flushes;
        for (pass = 0; pass < 2UL; ++pass) {
            flushes = context->egCacheFlushes;
            for (i = 0; i < 4UL; ++i)
                cached[i] = 0;
            indexCached = 0;
            if (context->egStaticArrays && !context->egDynamic)
                for (i = 0; i < (merged ? 1UL : regions); ++i)
                    /* the tag keys the model format (with colours) only */
                    cached[i] = EgCacheGet(context, region[i].start, region[i].bytes,
                                           haveColor, merged ? alignDelta : 0,
                                           merged ? alignStride : 0);
            /* 2-byte aligned lists too: the 68060 reads misaligned longs */
            if (indices && context->egStaticIndices && type == GL_UNSIGNED_SHORT)
                indexCached = EgCacheGet(context, indices, indexBytes, FALSE, 0, 0);
            if (flushes == context->egCacheFlushes)
                break;
        }
        EGT_END(9);
        if (flushes != context->egCacheFlushes)
            EG_FAIL();
    }

    /* room: records, commands and ring in one batch */
    need = state->headerDwords + EG_ARRAYS_DWORDS;
    slot = BuildSubmitSlot(context);
    if ((slot->commitMode && slot->drawCount) ||
        slot->recordCount + need > context->batchDwords ||
        slot->generatedDwordCount + EG_ARRAYS_GENERATED > R200_CP_SLOT_DWORDS - 1024UL ||
        EgRingUsed(context) + ringBytes > EG_RING_DATA_BYTES) {
        EgSubmitWhy[1]++;
        if (!Submit(context))
            EG_FAIL();
        slot = BuildSubmitSlot(context);
        if (slot->recordCount + need > context->batchDwords ||
            EgRingUsed(context) + ringBytes > EG_RING_DATA_BYTES)
            EG_FAIL();
    }

    /* the arrays */
    {
    EGT_BEGIN();
    for (i = 0; i < (merged ? 1UL : regions); ++i) {
        if (cached[i]) {
            region[i].gpu = cached[i];
            continue;
        }
        region[i].gpu = EgRingAllocFor(context, region[i].bytes, &cpu,
                                       merged ? alignDelta : 0, merged ? alignStride : 0);
        EgCopyLongs(cpu, (const ULONG *)region[i].start, region[i].bytes >> 2);
    }
    EGT_END(10);
    }
    for (i = 0; i < regions; ++i) {
        const struct EgArrayRegion *r = merged ? &region[0] : &region[i];
        gpu[i] = r->gpu + (ULONG)((regionArray[i]->pointer + lo * regionArray[i]->stride) -
                                  r->start);
        stride[i] = regionArray[i]->stride;
    }
    record = slot->records + slot->recordCount;
    for (i = 0; i < state->headerDwords; ++i)
        record[i] = state->header[i];
    record[0] = EG_EXEC_ARRAYS;
    record[1] = need;
    record[10] = count;
    ga = record + state->headerDwords;
    for (i = 0; i < EG_ARRAYS_DWORDS; ++i)
        ga[i] = 0;
    ga[EGA_PRIM] = prim;
    ga[EGA_COUNT] = count;
    ga[EGA_VERTS] = n;
    ga[EGA_FORMATS] = (ULONG)va->size | (colFormat << EGA_COL_SHIFT);
    ga[EGA_POS_GPU] = gpu[0];
    ga[EGA_POS_STRIDE] = stride[0];
    if (merged && stride[0] &&
        (!state->textured[0] || haveTex[0]) && (!state->textured[1] || haveTex[1])) {
        /* one buffer when every array is a record field at a small offset
         * after the position, with the position's stride */
        ULONG k, offs = 0, shift[4], n2 = 0;
        BOOL il = TRUE;
        shift[0] = 0;                       /* colour */
        shift[1] = 8;                       /* tex0 */
        shift[2] = 16;                      /* tex1 */
        k = 1;
        if (haveColor) {
            ULONG o = gpu[k] - gpu[0];
            if (stride[k] != stride[0] || o > 252UL || (o & 3UL)) il = FALSE;
            offs |= o << shift[0];
            ++k;
        }
        for (n2 = 0; n2 < 2UL; ++n2)
            if (haveTex[n2]) {
                ULONG o = gpu[k] - gpu[0];
                if (stride[k] != stride[0] || o > 252UL || (o & 3UL)) il = FALSE;
                offs |= o << shift[1 + n2];
                ++k;
            }
        if (il) {
            ga[EGA_FORMATS] |= EGA_INTERLEAVED | (haveColor ? EGA_COL_ARRAY : 0UL);
            ga[EGA_OFFSETS] = offs;
        }
    }
    i = 1;
    if (haveColor) {
        ga[EGA_COL_GPU] = gpu[i];
        ga[EGA_COL_STRIDE] = stride[i++];
    } else {
        /* the current colour, R, G, B, A bytes, for every vertex: the same
         * ring word as the last draw's while the colour stays (one vertex
         * buffer resource less to send) */
        ULONG c = context->currentColor;
        if (context->egColorSerial != context->batchSerial ||
            context->egColorSlot != context->buildSlot || context->egColor != c ||
            !context->egColorGpu) {
            context->egColorGpu = EgRingAlloc(context, 4UL, &cpu);
            cpu[0] = (((c >> 16) & 0xffUL) << 24) | (((c >> 8) & 0xffUL) << 16) |
                     ((c & 0xffUL) << 8) | (c >> 24);
            context->egColor = c;
            context->egColorSerial = context->batchSerial;
            context->egColorSlot = context->buildSlot;
        }
        ga[EGA_COL_GPU] = context->egColorGpu;
        ga[EGA_COL_STRIDE] = 0;
    }
    {
        ULONG unit;
        for (unit = 0; unit < 2UL; ++unit) {
            ULONG gi = unit ? EGA_TEX1_GPU : EGA_TEX0_GPU;
            if (haveTex[unit]) {
                ga[gi] = gpu[i];
                ga[gi + 1] = stride[i++];
            } else if (state->textured[unit]) {
                const ULONG *st = (const ULONG *)&context->currentS[unit];
                const ULONG *tt = (const ULONG *)&context->currentT[unit];
                ga[gi] = EgRingAlloc(context, 8UL, &cpu);
                cpu[0] = *st;
                cpu[1] = *tt;
                ga[gi + 1] = 0;
            }
        }
    }
    /* the indices, as the client holds them (the GPU swaps them) */
    if (indices) {
        ga[EGA_INDEX_TYPE] = indexType;
        ga[EGA_INDEX_OFFSET] = (ULONG)-(LONG)lo;
        if (indexCached)
            ga[EGA_INDEX_GPU] = indexCached;
        else if ((ga[EGA_INDEX_GPU] = EgRingAlloc(context, indexBytes, &cpu)), type == GL_UNSIGNED_INT)
            EgCopyLongs(cpu, (const ULONG *)indices, count);
        else if (type == GL_UNSIGNED_SHORT)
            /* aligned or not: the 68060 reads misaligned longs itself */
            EgCopyLongs(cpu, (const ULONG *)indices, indexBytes >> 2);
        else {
            for (i = 0; i + 1UL < count; i += 2UL)
                *cpu++ = ((ULONG)indices[i] << 16) | indices[i + 1];
            if (count & 1UL)
                *cpu = (ULONG)indices[count - 1UL] << 16;
        }
    }
    slot->recordCount += need;
    slot->generatedDwordCount += EG_ARRAYS_GENERATED;
    if (!slot->modeDecided) {
        slot->commitMode = FALSE;
        slot->modeDecided = TRUE;
    }
    slot->lastDrawValid = FALSE;
    if (state->textured[0])
        MarkTextureQueued(context, state->textureName[0]);
    if (state->textured[1])
        MarkTextureQueued(context, state->textureName[1]);
    EgDrawn++;
    if (context->perfEnabled) {
        context->perf.primitives++;
        context->perf.drawRecords++;
        context->perf.inputVertices += count;
        context->perf.submittedVertices += n;
    }
    return TRUE;
}

/* glBegin/glEnd draws with hardware transform: the vertices straight into
 * the ring as interleaved records (x, y, z[, w], ARGB colour[, s, t[, s1,
 * t1]]), each written once, drawn as an EG_EXEC_ARRAYS record. Triangles,
 * fans, polygons and quads become indexed triangle lists (the indices in
 * the slot's index lane), so the next draw with the same state extends the
 * same record (GLQuake's per-polygon water and sky); points stay a plain
 * list. FALSE: not covered, nothing touched (the caller takes the old path). */
static BOOL EgImmediateDraw(struct MiniGLR200Context *context, const struct Vertex *v,
                            ULONG count, GLenum mode)
{
    struct PendingDrawState *state;
    struct SubmitSlot *slot;
    ULONG prim, vN, idxN, i, posDw, strideDw, vbytes, ibytes, need, *record, *ga, slotIndex;
    ULONG base;
    BOOL w4 = FALSE, tex1, texAny, merge, indexed;
    volatile ULONG *cpu;

    if (context->egArraysOff || !context->device || context->insideBegin ||
        context->fogEnabled || context->lightingEnabled || context->paletteLookup ||
        context->polygonMode != GL_FILL)
        return FALSE;
    indexed = TRUE;
    switch (mode) {
    case GL_TRIANGLES:
        prim = RADEON3D_EXEC_DRAW_TRIANGLES;
        vN = idxN = count - count % 3UL;
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        if (context->shadeModel == GL_FLAT)
            return FALSE;
        prim = RADEON3D_EXEC_DRAW_TRIANGLES;
        vN = count >= 3UL ? count : 0;
        idxN = vN ? (count - 2UL) * 3UL : 0;
        break;
    case GL_QUADS:
        if (context->shadeModel == GL_FLAT)
            return FALSE;
        prim = RADEON3D_EXEC_DRAW_TRIANGLES;
        vN = count & ~3UL;
        idxN = (count / 4UL) * 6UL;
        break;
    case GL_POINTS:
        if (context->pointSize > 255.0f)
            return FALSE;
        prim = RADEON3D_EXEC_DRAW_POINTS;
        vN = idxN = count;
        indexed = FALSE;
        break;
    default:
        return FALSE;
    }
    if (context->textureGenS[0] || context->textureGenT[0] ||
        context->textureGenS[1] || context->textureGenT[1]) {
        ULONG hardwareSphereMap = HardwareSphereMapMask(context);
        if (hardwareSphereMap || SoftwareSphereMapMask(context, hardwareSphereMap))
            return FALSE;
    }
    if (!vN)
        return TRUE;
    for (i = 0; i < vN; ++i)
        if (v[i].w != 1.0f) {
            w4 = TRUE;
            break;
        }
    if (!EgRingReady(context))
        return FALSE;
    if (context->screenVertexCount && !FlushScreenVertices(context))
        return FALSE;
    context->screenHardwareTransform = TRUE;
    context->screenProjective = FALSE;
    context->egCaptureOnly = TRUE;
    i = PrepareScreenAppend(context, prim);
    context->egCaptureOnly = FALSE;
    context->screenHardwareTransform = FALSE;
    if (!i)
        return FALSE;
    state = context->egCaptured;
    if (!state->drawEnabled || !state->drawRectangleValid)
        return TRUE;
    if (!state->hardwareTransform || state->normalVertex || state->lit || state->fog ||
        state->hardwareSphereMap ||
        state->headerDwords != RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS ||
        (state->header[5] & (RADEON3D_DRAW_TEXGEN | RADEON3D_DRAW_LIGHTING |
                             RADEON3D_DRAW_PALETTE | RADEON3D_DRAW_NORMALS)))
        return FALSE;
    tex1 = state->textured[1];
    texAny = state->textured[0] || tex1;
    posDw = w4 ? 4UL : 3UL;
    strideDw = posDw + 1UL + (texAny ? 2UL : 0UL) + (tex1 ? 2UL : 0UL);
    vbytes = vN * strideDw * 4UL;
    ibytes = indexed ? idxN * 2UL : 0;
    if (vbytes + 64UL > EG_RING_DATA_BYTES || ibytes + 8UL > EG_RING_INDEX_BYTES)
        return FALSE;
    need = state->headerDwords + EG_ARRAYS_DWORDS;
    slot = BuildSubmitSlot(context);
    slotIndex = context->buildSlot;
    (void)EgRingUsed(context);
    /* the same state right after the previous immediate draw: extend it */
    merge = context->egImmValid && context->egImmSerial == context->batchSerial &&
            context->egImmSlot == slotIndex &&
            context->egImmRecords == slot->recordCount &&
            context->egImmRingEnd == context->egRingCursor[slotIndex] &&
            context->egImmPrim == prim && context->egImmStride == strideDw &&
            context->egImmVerts + vN <= 65535UL &&
            context->egRingCursor[slotIndex] + vbytes <= EG_RING_DATA_BYTES &&
            context->egImmIdxCursor[slotIndex] + ibytes + 4UL <= EG_RING_INDEX_BYTES;
    if (merge) {
        record = slot->records + context->egImmRecord;
        for (i = 2; i < state->headerDwords; ++i)
            if (i != 10UL && record[i] != state->header[i]) {
                merge = FALSE;
                break;
            }
    }
    if (!merge) {
        if ((slot->commitMode && slot->drawCount) ||
            slot->recordCount + need > context->batchDwords ||
            slot->generatedDwordCount + EG_ARRAYS_GENERATED > R200_CP_SLOT_DWORDS - 1024UL ||
            EgRingUsed(context) + vbytes + 64UL > EG_RING_DATA_BYTES ||
            context->egImmIdxCursor[slotIndex] + ibytes + 8UL > EG_RING_INDEX_BYTES) {
            if (!Submit(context))
                return FALSE;
            slot = BuildSubmitSlot(context);
            slotIndex = context->buildSlot;
            if (slot->recordCount + need > context->batchDwords ||
                EgRingUsed(context) + vbytes + 64UL > EG_RING_DATA_BYTES)
                return FALSE;
        }
        record = slot->records + slot->recordCount;
        for (i = 0; i < state->headerDwords; ++i)
            record[i] = state->header[i];
        record[0] = EG_EXEC_ARRAYS;
        record[1] = need;
        record[10] = 0;
        ga = record + state->headerDwords;
        for (i = 0; i < EG_ARRAYS_DWORDS; ++i)
            ga[i] = 0;
        ga[EGA_PRIM] = prim;
        ga[EGA_FORMATS] = posDw | (EGA_COL_ARGB << EGA_COL_SHIFT) | EGA_INTERLEAVED |
                          EGA_COL_ARRAY;
        ga[EGA_OFFSETS] = (posDw * 4UL) | ((posDw * 4UL + 4UL) << 8) |
                          ((posDw * 4UL + 12UL) << 16);
        ga[EGA_POS_STRIDE] = strideDw * 4UL;
        ga[EGA_POS_GPU] = EgRingAlloc(context, vbytes, &cpu);
        if (indexed) {
            /* the lane starts on a long */
            context->egImmIdxCursor[slotIndex] = (context->egImmIdxCursor[slotIndex] + 3UL) & ~3UL;
            ga[EGA_INDEX_TYPE] = EGA_INDEX_16;
            ga[EGA_INDEX_GPU] = context->egRing.GpuAddress + slotIndex * EG_RING_SLOT_BYTES +
                                EG_RING_DATA_BYTES + context->egImmIdxCursor[slotIndex];
        }
        context->egImmRecord = slot->recordCount;
        context->egImmVerts = 0;
        slot->recordCount += need;
        slot->generatedDwordCount += EG_ARRAYS_GENERATED;
        if (!slot->modeDecided) {
            slot->commitMode = FALSE;
            slot->modeDecided = TRUE;
        }
        slot->lastDrawValid = FALSE;
    } else {
        cpu = (volatile ULONG *)((UBYTE *)context->egRing.CpuAddress +
                                 slotIndex * EG_RING_SLOT_BYTES +
                                 context->egRingCursor[slotIndex]);
        context->egRingCursor[slotIndex] += vbytes;
        ga = record + state->headerDwords;
    }
    base = context->egImmVerts;
    ga[EGA_COUNT] += idxN;
    ga[EGA_VERTS] += vN;
    record[10] += idxN;
    EgImmLongs += vN * strideDw + (ibytes >> 2);
    /* the vertices, each once */
    for (i = 0; i < vN; ++i) {
        const struct Vertex *p = &v[i];
        *cpu++ = *(const ULONG *)&p->x;
        *cpu++ = *(const ULONG *)&p->y;
        *cpu++ = *(const ULONG *)&p->z;
        if (w4)
            *cpu++ = *(const ULONG *)&p->w;
        *cpu++ = p->color;
        if (texAny) {
            *cpu++ = *(const ULONG *)&p->s;
            *cpu++ = *(const ULONG *)&p->t;
        }
        if (tex1) {
            *cpu++ = *(const ULONG *)&p->s1;
            *cpu++ = *(const ULONG *)&p->t1;
        }
    }
    /* the indices: big-endian shorts, two a longword where aligned */
    if (indexed) {
        UBYTE *lane = (UBYTE *)context->egRing.CpuAddress + slotIndex * EG_RING_SLOT_BYTES +
                      EG_RING_DATA_BYTES;
        ULONG at = context->egImmIdxCursor[slotIndex], pending = 0, havePending = 0, k;
#define EG_IDX(val) do { ULONG v_ = (val) + base; \
        if (!havePending && (at & 2UL)) { *(volatile UWORD *)(lane + at) = (UWORD)v_; at += 2; } \
        else if (!havePending) { pending = v_; havePending = 1; } \
        else { *(volatile ULONG *)(lane + at) = (pending << 16) | v_; at += 4; havePending = 0; } \
        } while (0)
        if (mode == GL_TRIANGLES) {
            for (k = 0; k < idxN; ++k)
                EG_IDX(k);
        } else if (mode == GL_QUADS) {
            for (k = 0; k < vN; k += 4UL) {
                EG_IDX(k); EG_IDX(k + 1UL); EG_IDX(k + 2UL);
                EG_IDX(k); EG_IDX(k + 2UL); EG_IDX(k + 3UL);
            }
        } else {
            for (k = 1; k + 1UL < vN; ++k) {
                EG_IDX(0UL); EG_IDX(k); EG_IDX(k + 1UL);
            }
        }
        if (havePending) {
            *(volatile UWORD *)(lane + at) = (UWORD)pending;
            at += 2;
        }
#undef EG_IDX
        context->egImmIdxCursor[slotIndex] = at;
    }
    context->egImmVerts += vN;
    context->egImmValid = TRUE;
    context->egImmSerial = context->batchSerial;
    context->egImmSlot = slotIndex;
    context->egImmHdrGen = context->egHdrGen - 1UL;     /* compared before a merge */
    context->egImmRecords = slot->recordCount;
    context->egImmRingEnd = context->egRingCursor[slotIndex];
    context->egImmPrim = prim;
    context->egImmStride = strideDw;
    if (state->textured[0])
        MarkTextureQueued(context, state->textureName[0]);
    if (tex1)
        MarkTextureQueued(context, state->textureName[1]);
    EgDrawn++;
    return TRUE;
}

/* Direct immediate mode: glBegin captures the state and picks the record
 * layout (x, y, z, ARGB colour[, s, t[, s1, t1]]), glVertex writes each
 * vertex straight into the ring, glEnd adds the indices and the record (or
 * extends the last one) as EgImmediateDraw does. Anything unusual (w != 1,
 * a full ring part, glArrayElement) reads the written vertices back and
 * continues the old way (EgDirectAbort). */
ULONG EgDirRefuse[10];
#define EG_DIR_REFUSE(n) do { EgDirRefuse[n]++; return FALSE; } while (0)
static BOOL EgDirectBegin(struct MiniGLR200Context *context, GLenum mode)
{
    struct PendingDrawState *state;
    ULONG prim, i, slotIndex, room, start;

    /* the state of the last direct glBegin, unchanged (water and sky:
     * polygon after polygon): its checks and capture stand */
    if (context->egDirOkSerial == context->semanticStateSerial &&
        context->egDirOkGen == context->egHdrGen && context->egDirOkMode == mode &&
        context->egDirOkSerial && !context->screenVertexCount && EgRingReady(context)) {
        prim = context->egDirPrim;
        state = context->egDirState;
        goto ring;
    }
    context->egDirOkSerial = 0;
    if (context->egArraysOff || !context->device || context->fogEnabled ||
        context->lightingEnabled || context->paletteLookup ||
        context->polygonMode != GL_FILL || context->activeFastPath ||
        context->textureMatrixActive[0] || context->textureMatrixActive[1] ||
        context->textureGenS[0] || context->textureGenT[0] ||
        context->textureGenS[1] || context->textureGenT[1])
        EG_DIR_REFUSE(0);
    switch (mode) {
    case GL_TRIANGLES:
        prim = RADEON3D_EXEC_DRAW_TRIANGLES;
        break;
    case GL_TRIANGLE_FAN:
    case GL_QUADS:
        /* flat: the split moves the provoking vertex, which only matters
         * when the colour varies (checked per vertex, EgDirectEnd) */
        prim = RADEON3D_EXEC_DRAW_TRIANGLES;
        break;
    case GL_POLYGON:
        /* not where glEnd may queue it for the two-pass multitexture */
        if (context->textureEnabled[1])
            EG_DIR_REFUSE(2);
        prim = RADEON3D_EXEC_DRAW_TRIANGLES;
        break;
    case GL_POINTS:
        if (context->pointSize > 255.0f)
            EG_DIR_REFUSE(3);
        prim = RADEON3D_EXEC_DRAW_POINTS;
        break;
    default:
        EG_DIR_REFUSE(4);
    }
    if (!EgRingReady(context))
        EG_DIR_REFUSE(5);
    if (context->screenVertexCount && !FlushScreenVertices(context))
        return FALSE;
    context->insideBegin = FALSE;
    context->screenHardwareTransform = TRUE;
    context->screenProjective = FALSE;
    context->egCaptureOnly = TRUE;
    i = PrepareScreenAppend(context, prim);
    context->egCaptureOnly = FALSE;
    context->screenHardwareTransform = FALSE;
    context->insideBegin = TRUE;
    if (!i)
        EG_DIR_REFUSE(6);
    state = context->egCaptured;
    if (!state->drawEnabled || !state->drawRectangleValid ||
        !state->hardwareTransform || state->normalVertex || state->lit || state->fog ||
        state->hardwareSphereMap ||
        state->headerDwords != RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS ||
        (state->header[5] & (RADEON3D_DRAW_TEXGEN | RADEON3D_DRAW_LIGHTING |
                             RADEON3D_DRAW_PALETTE | RADEON3D_DRAW_NORMALS)))
        EG_DIR_REFUSE(7);
    context->egDirTex1 = state->textured[1];
    context->egDirTexAny = state->textured[0] || state->textured[1];
    context->egDirStride = 4UL + (context->egDirTexAny ? 2UL : 0UL) +
                           (context->egDirTex1 ? 2UL : 0UL);
    context->egDirOkSerial = context->semanticStateSerial;
    context->egDirOkGen = context->egHdrGen;
    context->egDirOkMode = mode;
ring:
    (void)EgRingUsed(context);
    slotIndex = context->buildSlot;
    start = EgAlignFor(context->egRing.GpuAddress + slotIndex * EG_RING_SLOT_BYTES,
                       context->egRingCursor[slotIndex], 0, context->egDirStride * 4UL);
    room = start + 64UL < EG_RING_DATA_BYTES ?
           (EG_RING_DATA_BYTES - 64UL - start) / (context->egDirStride * 4UL) : 0;
    if (room < 64UL) {
        /* a fresh slot for it (allowed: nothing staged yet) */
        if (!Submit(context))
            return FALSE;
        (void)EgRingUsed(context);
        slotIndex = context->buildSlot;
        start = EgAlignFor(context->egRing.GpuAddress + slotIndex * EG_RING_SLOT_BYTES,
                           context->egRingCursor[slotIndex], 0, context->egDirStride * 4UL);
        room = (EG_RING_DATA_BYTES - 64UL - start) / (context->egDirStride * 4UL);
    }
    if (room > 4096UL)
        room = 4096UL;
    context->egDirStart = start;
    context->egDirSlot = slotIndex;
    context->egDirSerial = context->batchSerial;
    context->egDirCpu = (volatile ULONG *)((UBYTE *)context->egRing.CpuAddress +
                                           slotIndex * EG_RING_SLOT_BYTES + context->egDirStart);
    context->egDirCap = room;
    context->egDirPrim = prim;
    context->egDirState = state;
    context->egDirFlat = context->shadeModel == GL_FLAT &&
                         (mode == GL_TRIANGLE_FAN || mode == GL_QUADS || mode == GL_POLYGON);
    context->egDirColor0 = context->currentColor;
    context->egDirVaried = FALSE;
    context->vertexCount = 0;
    context->egDirect = TRUE;
    return TRUE;
}

/* The vertices written so far back into context->vertices; the old way
 * from here on. */
static void EgDirectAbort(struct MiniGLR200Context *context)
{
    volatile ULONG *p = (volatile ULONG *)((UBYTE *)context->egRing.CpuAddress +
                                           context->egDirSlot * EG_RING_SLOT_BYTES +
                                           context->egDirStart);
    ULONG i;

    for (i = 0; i < context->vertexCount; ++i) {
        struct Vertex *v = &context->vertices[i];
        ULONG *f = (ULONG *)v;
        f[0] = p[0];                    /* x */
        f[1] = p[1];                    /* y */
        f[2] = p[2];                    /* z */
        v->w = 1.0f;
        v->color = p[3];
        v->nx = context->currentNormalX;
        v->ny = context->currentNormalY;
        v->nz = context->currentNormalZ;
        v->s = context->currentS[0];
        v->t = context->currentT[0];
        v->s1 = context->currentS[1];
        v->t1 = context->currentT[1];
        v->fog = 0.0f;
        p += 4;
        if (context->egDirTexAny) {
            *(ULONG *)&v->s = p[0];
            *(ULONG *)&v->t = p[1];
            p += 2;
        }
        if (context->egDirTex1) {
            *(ULONG *)&v->s1 = p[0];
            *(ULONG *)&v->t1 = p[1];
            p += 2;
        }
    }
    context->egDirect = FALSE;
}

/* glEnd in direct mode: the indices and the record. */
static BOOL EgDirectEnd(struct MiniGLR200Context *context)
{
    struct PendingDrawState *state = context->egDirState;
    struct SubmitSlot *slot = BuildSubmitSlot(context);
    GLenum mode = context->primitive;
    ULONG count = context->vertexCount, vN, idxN, i, need, *record, *ga, ibytes, base;
    ULONG slotIndex = context->buildSlot, strideDw = context->egDirStride, prim = context->egDirPrim;
    BOOL indexed = prim != RADEON3D_EXEC_DRAW_POINTS, merge;

    context->egDirect = FALSE;
    if (context->egDirFlat && context->egDirVaried)
        return FALSE;           /* flat with varying colour: the old way */
    switch (mode) {
    case GL_TRIANGLES:      vN = idxN = count - count % 3UL; break;
    case GL_QUADS:          vN = count & ~3UL; idxN = (count / 4UL) * 6UL; break;
    case GL_POINTS:         vN = idxN = count; break;
    default:                vN = count >= 3UL ? count : 0; idxN = vN ? (count - 2UL) * 3UL : 0; break;
    }
    if (!vN)
        return TRUE;
    if (slotIndex != context->egDirSlot || context->batchSerial != context->egDirSerial)
        return FALSE;           /* cannot happen: no submit inside Begin/End */
    ibytes = indexed ? idxN * 2UL : 0;
    if (context->egImmIdxCursor[slotIndex] + ibytes + 8UL > EG_RING_INDEX_BYTES)
        return FALSE;
    need = state->headerDwords + EG_ARRAYS_DWORDS;
    merge = context->egImmValid && context->egImmSerial == context->batchSerial &&
            context->egImmSlot == slotIndex &&
            context->egImmRecords == slot->recordCount &&
            context->egImmRingEnd == context->egDirStart &&
            context->egImmPrim == prim && context->egImmStride == strideDw &&
            context->egImmVerts + vN <= 65535UL;
    if (merge) {
        record = slot->records + context->egImmRecord;
        if (context->egImmHdrGen != context->egHdrGen)
            for (i = 2; i < state->headerDwords; ++i)
                if (i != 10UL && record[i] != state->header[i]) {
                    merge = FALSE;
                    break;
                }
    }
    if (!merge) {
        if ((slot->commitMode && slot->drawCount) ||
            slot->recordCount + need > context->batchDwords ||
            slot->generatedDwordCount + EG_ARRAYS_GENERATED > R200_CP_SLOT_DWORDS - 1024UL)
            return FALSE;       /* the old way: it submits as it needs */
        record = slot->records + slot->recordCount;
        for (i = 0; i < state->headerDwords; ++i)
            record[i] = state->header[i];
        record[0] = EG_EXEC_ARRAYS;
        record[1] = need;
        record[10] = 0;
        ga = record + state->headerDwords;
        for (i = 0; i < EG_ARRAYS_DWORDS; ++i)
            ga[i] = 0;
        ga[EGA_PRIM] = prim;
        ga[EGA_FORMATS] = 3UL | (EGA_COL_ARGB << EGA_COL_SHIFT) | EGA_INTERLEAVED |
                          EGA_COL_ARRAY;
        ga[EGA_OFFSETS] = 12UL | (16UL << 8) | (24UL << 16);
        ga[EGA_POS_STRIDE] = strideDw * 4UL;
        ga[EGA_POS_GPU] = context->egRing.GpuAddress + slotIndex * EG_RING_SLOT_BYTES +
                          context->egDirStart;
        if (indexed) {
            context->egImmIdxCursor[slotIndex] = (context->egImmIdxCursor[slotIndex] + 3UL) & ~3UL;
            ga[EGA_INDEX_TYPE] = EGA_INDEX_16;
            ga[EGA_INDEX_GPU] = context->egRing.GpuAddress + slotIndex * EG_RING_SLOT_BYTES +
                                EG_RING_DATA_BYTES + context->egImmIdxCursor[slotIndex];
        }
        context->egImmRecord = slot->recordCount;
        context->egImmVerts = 0;
        slot->recordCount += need;
        slot->generatedDwordCount += EG_ARRAYS_GENERATED;
        if (!slot->modeDecided) {
            slot->commitMode = FALSE;
            slot->modeDecided = TRUE;
        }
        slot->lastDrawValid = FALSE;
    } else
        ga = record + state->headerDwords;
    /* the vertices are in place: the ring part ends behind them */
    context->egRingCursor[slotIndex] = context->egDirStart + count * strideDw * 4UL;
    base = context->egImmVerts;
    ga[EGA_COUNT] += idxN;
    ga[EGA_VERTS] += count;
    record[10] += idxN;
    EgImmLongs += count * strideDw + (ibytes >> 2);
    if (indexed) {
        UBYTE *lane = (UBYTE *)context->egRing.CpuAddress + slotIndex * EG_RING_SLOT_BYTES +
                      EG_RING_DATA_BYTES;
        ULONG at = context->egImmIdxCursor[slotIndex], pending = 0, havePending = 0, k;
#define EG_IDX(val) do { ULONG v_ = (val) + base; \
        if (!havePending && (at & 2UL)) { *(volatile UWORD *)(lane + at) = (UWORD)v_; at += 2; } \
        else if (!havePending) { pending = v_; havePending = 1; } \
        else { *(volatile ULONG *)(lane + at) = (pending << 16) | v_; at += 4; havePending = 0; } \
        } while (0)
        if (mode == GL_TRIANGLES) {
            for (k = 0; k < idxN; ++k)
                EG_IDX(k);
        } else if (mode == GL_QUADS) {
            for (k = 0; k < vN; k += 4UL) {
                EG_IDX(k); EG_IDX(k + 1UL); EG_IDX(k + 2UL);
                EG_IDX(k); EG_IDX(k + 2UL); EG_IDX(k + 3UL);
            }
        } else {
            for (k = 1; k + 1UL < vN; ++k) {
                EG_IDX(0UL); EG_IDX(k); EG_IDX(k + 1UL);
            }
        }
        if (havePending) {
            *(volatile UWORD *)(lane + at) = (UWORD)pending;
            at += 2;
        }
#undef EG_IDX
        context->egImmIdxCursor[slotIndex] = at;
    }
    context->egImmVerts += count;
    context->egImmValid = TRUE;
    context->egImmSerial = context->batchSerial;
    context->egImmSlot = slotIndex;
    context->egImmRecords = slot->recordCount;
    context->egImmRingEnd = context->egRingCursor[slotIndex];
    context->egImmPrim = prim;
    context->egImmStride = strideDw;
    context->egImmHdrGen = context->egHdrGen;
    if (state->textured[0])
        MarkTextureQueued(context, state->textureName[0]);
    if (state->textured[1])
        MarkTextureQueued(context, state->textureName[1]);
    EgDrawn++;
    return TRUE;
}

/* Context teardown (all submissions retired). */
void EgArraysRelease(struct MiniGLR200Context *context)
{
    ULONG i, k2;
    dbg_puts("[egarrays] drawn=");
    dbg_u32(EgDrawn);
    dbg_puts(" cache hits=");
    dbg_u32(context->egCacheHits);
    dbg_puts(" misses=");
    dbg_u32(context->egCacheMisses);
    dbg_puts(" flushes=");
    dbg_u32(context->egCacheFlushes);
    dbg_puts(" untagged hit/miss/bytes=");
    dbg_u32(EgHitCount[0]); dbg_puts("/"); dbg_u32(EgMissCount[0]); dbg_puts("/");
    dbg_u32(EgMissBytes[0]);
    dbg_puts(" tagged=");
    dbg_u32(EgHitCount[1]); dbg_puts("/"); dbg_u32(EgMissCount[1]); dbg_puts("/");
    dbg_u32(EgMissBytes[1]);
    {
        extern ULONG UpStatKey[8], UpStatTexels[8], UpStatCalls[8];
        dbg_puts(" uploads:");
        for (i = 0; i < 8UL && UpStatKey[i]; ++i) {
            dbg_puts(" ");
            dbg_hex32(UpStatKey[i]);
            dbg_puts("=");
            dbg_u32(UpStatCalls[i]);
            dbg_puts("/");
            dbg_u32(UpStatTexels[i]);
        }
    }
    {
        extern ULONG EgStatDraws, EgStatVtxDwords, CpBatches, CpCopyDwords, EgStatClears;
        dbg_puts(" emitted draws=");
        dbg_u32(EgStatDraws);
        dbg_puts(" oldvtxdw=");
        dbg_u32(EgStatVtxDwords);
        dbg_puts(" batches=");
        dbg_u32(CpBatches);
        dbg_puts(" ibdw=");
        dbg_u32(CpCopyDwords);
        {
            extern ULONG EgStatOldPrim[20], EgStatOldVerts[20];
            ULONG k;
            dbg_puts(" oldprims(tcl/screen):");
            for (k = 0; k < 20UL; ++k)
                if (EgStatOldPrim[k]) {
                    dbg_puts(" ");
                    dbg_u32(k);
                    dbg_puts("=");
                    dbg_u32(EgStatOldPrim[k]);
                    dbg_puts("/");
                    dbg_u32(EgStatOldVerts[k]);
                }
        }
        dbg_puts(" T(ticks/n):");
        for (k2 = 0; k2 < 16UL; ++k2) {
            dbg_puts(" ");
            dbg_u32(EgT[k2]);
            dbg_puts("/");
            dbg_u32(EgN[k2]);
        }
        {
            extern ULONG EgBumpAt[16], EgBumpN[16];
            dbg_puts(" bumps(at-Bump):");
            for (k2 = 0; k2 < 16UL && EgBumpAt[k2]; ++k2) {
                dbg_puts(" ");
                dbg_hex32(EgBumpAt[k2]);
                dbg_puts("=");
                dbg_u32(EgBumpN[k2]);
            }
        }
        {
            extern ULONG EgCalls;
            dbg_puts(" calls=");
            dbg_u32(EgCalls);
        }
        {
            extern ULONG EgLmBytes, EgLmChanged, EgLmRowsSame;
            dbg_puts(" lm bytes/changed/samerows=");
            dbg_u32(EgLmBytes); dbg_puts("/"); dbg_u32(EgLmChanged); dbg_puts("/"); dbg_u32(EgLmRowsSame);
        }
        {
            extern ULONG EgFastDraws, EgFullDraws;
            dbg_puts(" emit fast/full=");
            dbg_u32(EgFastDraws); dbg_puts("/"); dbg_u32(EgFullDraws);
        }
#if R200_PERF_TIMING
        {
            extern ULONG EgBreakAt[32], EgBreakN[32];
            ULONG k3;
            dbg_puts(" breaks:");
            for (k3 = 0; k3 < 32UL && EgBreakAt[k3]; ++k3) {
                dbg_puts(" ");
                dbg_hex32(EgBreakAt[k3]);
                dbg_puts("=");
                dbg_u32(EgBreakN[k3]);
            }
        }
#endif
        {
            ULONG k4;
            dbg_puts(" dirrefuse:");
            for (k4 = 0; k4 < 10UL; ++k4) { dbg_puts(" "); dbg_u32(EgDirRefuse[k4]); }
            dbg_puts(" dirfail=");
            dbg_u32(EgDirRefuse[8]);
        }
        dbg_puts(" copiedlongs=");
        dbg_u32(EgCopiedLongs);
        dbg_puts(" immlongs=");
        dbg_u32(EgImmLongs);
        dbg_puts(" submits tex/arrays=");
        dbg_u32(EgSubmitWhy[0]);
        dbg_puts("/");
        dbg_u32(EgSubmitWhy[1]);
    }
    dbg_puts(" old path:");
    for (i = 0; i < 16UL && EgFailLine[i]; ++i) {
        dbg_puts(" L");
        dbg_u32(EgFailLine[i]);
        dbg_puts("=");
        dbg_u32(EgFailCount[i]);
    }
    dbg_newline();
    if (context->egCacheBitMap)
        p96FreeBitMap(context->egCacheBitMap);
    context->egCacheBitMap = NULL;
    if (context->egCache)
        FreeMem(context->egCache, EG_CACHE_SLOTS * sizeof(struct EgCacheEntry));
    context->egCache = NULL;
    if (context->egRingBitMap)
        p96FreeBitMap(context->egRingBitMap);
    context->egRingBitMap = NULL;
    ZeroMemory(&context->egRing, sizeof(context->egRing));
}
