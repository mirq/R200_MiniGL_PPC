#ifndef LIBRARIES_MINIGL68K_EXT_H
#define LIBRARIES_MINIGL68K_EXT_H

/*
 * minigl68k.library extension (LVO -36): resident vertex buffers for the
 * 68k R200 frontend. Static geometry is written once into VRAM and drawn
 * every frame with hardware TCL from small index lists, so its vertices
 * never cross the Zorro bus again.
 *
 * Vertex layout (MGL68K_RESIDENT_STRIDE bytes, every dword stored
 * LITTLE-ENDIAN, i.e. byte-swapped from the 68k's native order):
 *     x, y, z, w (float), colour (ARGB), s, t (float)
 * It is the R200 compact hardware-TCL vertex; DrawResident only accepts a
 * draw whose current GL state uses exactly that layout (one texture unit
 * or none, no fog, no lighting) and returns FALSE otherwise, so the caller
 * can fall back to its ordinary GL path.
 *
 * DrawResident uses the current GL state (bound texture, blend, depth,
 * matrices) like any other draw and keeps the order of the GL stream.
 * Indices are 16-bit, relative to byteOffset, triangles only.
 */
#include <exec/types.h>

#define MGL68K_EXT_VERSION 2UL
#define MGL68K_RESIDENT_STRIDE 28UL
#define MGL68K_RESIDENT_MAX_BYTES (256UL * 1024UL)

struct MGL68KStreamDraw;

typedef struct MGL68KExt {
    ULONG version;
    ULONG structSize;
    /* Returns a handle (0 = failure) and the buffer's CPU address. bytes is
     * at most MGL68K_RESIDENT_MAX_BYTES. The buffer lives until
     * ResidentFree or until the context is deleted. */
    ULONG (*ResidentAlloc)(ULONG bytes, APTR *cpuAddress);
    void (*ResidentFree)(ULONG handle);
    /* GL_TRIANGLES from the resident buffer. FALSE: not drawn (state
     * layout not supported, out of space, no context); draw it otherwise. */
    BOOL (*DrawResident)(ULONG handle, ULONG byteOffset,
                         const UWORD *indices, ULONG count);
    /* Version 2: the 16-bit indices (little-endian) are already resident
     * in indexHandle at indexOffset (4-byte aligned): no per-frame index
     * traffic at all. */
    BOOL (*DrawResidentIndexed)(ULONG handle, ULONG byteOffset,
                                ULONG indexHandle, ULONG indexOffset,
                                ULONG count);
    /* Makes every defined texture resident now (a load-time upload, as a
     * real driver does at glTexImage2D); returns how many were uploaded.
     * Present when structSize covers it. */
    ULONG (*PrepareTextures)(void);
    /* MGL68K_LAYOUT_NO_W: resident vertices carry no W: textured
     * x,y,z,colour,s,t (6 dwords), untextured x,y,z,colour (4). */
    ULONG layoutFlags;
    /* A vertex store in VRAM outside the resident pool (up to ~3.5 MB):
     * a handle usable in MGL68KStreamDraw, 0 on failure. Lives until the
     * context is deleted. */
    ULONG (*StoreAlloc)(ULONG bytes, APTR *cpuAddress);
    /* Indexed triangles whose attributes come from separate streams: the
     * GL state must be textured, unlit, no fog (as DrawResident). */
    BOOL (*DrawStreams)(const struct MGL68KStreamDraw *draw);
    /* GL_POINTS at the current point size (sized points are hardware
     * point sprites), indices resident. Present when structSize covers it. */
    BOOL (*DrawResidentPoints)(ULONG handle, ULONG byteOffset,
                               ULONG indexHandle, ULONG indexOffset,
                               ULONG count);
} MGL68KExt;

/* Handles are resident buffers or stores; offsets in bytes, strides in
 * dwords. Position x,y,z (no W), colour ARGB, texcoord s,t; every dword
 * little-endian. */
typedef struct MGL68KStreamDraw {
    ULONG posHandle, posOffset, posStride;
    ULONG colHandle, colOffset, colStride;
    ULONG texHandle, texOffset, texStride;
    ULONG indexHandle, indexOffset, count;
} MGL68KStreamDraw;

#define MGL68K_LAYOUT_NO_W 1UL

#endif
