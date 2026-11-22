#ifndef EG_EMIT_H
#define EG_EMIT_H

/* Native Evergreen command emitter for the MiniGL frontend's records
 * (eg_emit.c). */

#include <exec/types.h>
#include <libraries/minigl_ppc_cp_emit.h>

#define EG_EMIT_SLOTS 3UL

/* Surface formats beyond the frontend's PC ones (radeon3d.h): the
 * big-endian Picasso96 formats Evergreen.chip offers by default. */
#define EG_FORMAT_A8R8G8B8 4UL
#define EG_FORMAT_R5G6B5   5UL
/* bits copied as they are (present) */
#define EG_FORMAT_RAW16    6UL
#define EG_FORMAT_RAW32    7UL

/* VRAM regions (offsets from VRAM 0) the emitter owns: the shader cache and
 * EG_EMIT_SLOTS streams of vertices and constants, one per submit slot. */
void EgEmitInit(APTR vramCpu, ULONG vramGpu, ULONG shaderOff, ULONG shaderSize,
                ULONG streamOff, ULONG streamSlotSize);
void EgEmitFlushShaders(void);

/* One batch of records into words (host longs, at most capacity); slot
 * selects the stream, whose previous batch must have retired. Returns the
 * dword count, 0 on failure (EgEmitFailStage says why). */
ULONG EgEmitBatch(ULONG *words, ULONG capacity, ULONG slot,
                  const ULONG *records, ULONG recordDwords,
                  MGLPPCEmitResolve resolve, void *user);

/* The rendered buffer srcGpu onto the visible one dstGpu (same pitch and
 * format), optionally from the next vertical blank on. */
ULONG EgEmitPresent(ULONG *words, ULONG capacity, APTR vertCpu, ULONG vertGpu,
                    ULONG srcGpu, ULONG dstGpu, ULONG pitchBytes, ULONG format,
                    ULONG width, ULONG height, BOOL waitVblank);

/* EG_EXEC_ARRAYS: a draw straight from the client's vertex arrays, copied
 * into VRAM as they are (r200_egarrays.c). A hardware-TCL draw header
 * (RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS; its vertex-layout options are
 * ignored) followed by EG_ARRAYS_DWORDS of stream descriptors. Every
 * attribute is fetched from its own buffer; stride 0 repeats one value. */
#define EG_EXEC_ARRAYS      0x40UL
#define EG_ARRAYS_DWORDS    16UL
/* EG_EXEC_ARRAYS_TEXONLY: an EG_EXEC_ARRAYS record whose header equals the
 * record right before it but for the texture words (the frontend knows) */
#define EG_EXEC_ARRAYS_TEXONLY 0x42UL
/* EG_EXEC_ARRAYS_TEXW: the same, the header as its texture words only
 * (dwords 4, 10..13, 15..18 in that order), then the descriptors */
#define EG_EXEC_ARRAYS_TEXW 0x43UL
#define EG_TEXWORDS         9UL
#define EGA_PRIM            0   /* RADEON3D_EXEC_DRAW_* */
#define EGA_COUNT           1   /* indices (or vertices without indices) */
#define EGA_INDEX_GPU       2
#define EGA_INDEX_TYPE      3   /* EGA_INDEX_* */
#define EGA_INDEX_OFFSET    4   /* added to every index (VGT_INDX_OFFSET) */
#define EGA_FORMATS         5   /* EGA_POS_* | EGA_COL_* */
#define EGA_VERTS           6   /* elements each buffer holds */
#define EGA_POS_GPU         7
#define EGA_POS_STRIDE      8
#define EGA_COL_GPU         9
#define EGA_COL_STRIDE      10
#define EGA_TEX0_GPU        11
#define EGA_TEX0_STRIDE     12
#define EGA_TEX1_GPU        13
#define EGA_TEX1_STRIDE     14
#define EGA_OFFSETS         15  /* EGA_INTERLEAVED: colour, tex0, tex1 byte
                                 * offsets from the position (8 bits each) */
#define EGA_INDEX_NONE      0UL
#define EGA_INDEX_16        1UL     /* big-endian 16-bit */
#define EGA_INDEX_32        2UL     /* big-endian 32-bit */
#define EGA_POS_SIZE_MASK   0x7UL   /* 2, 3 or 4 floats */
#define EGA_INTERLEAVED     0x100UL /* the arrays one buffer: EGA_POS_GPU, EGA_POS_STRIDE */
#define EGA_COL_ARRAY       0x200UL /* the colour is an array in that buffer */
#define EGA_RESTART         0x800UL /* indices 0xffff restart the primitive */
#define EGA_COL_SHIFT       4
#define EGA_COL_UBYTE4      0UL     /* R, G, B, A bytes */
#define EGA_COL_FLOAT3      1UL
#define EGA_COL_FLOAT4      2UL
#define EGA_COL_ARGB        3UL     /* A, R, G, B bytes (MGL_UBYTE_ARGB) */

/* Texture storage formats, in texture state bits 14..15 (free in
 * radeon3d_records.h): 16-bit texels as the client gave them (big-endian
 * shorts, fetched with the 8-in-16 swap). */
#define EG_TEX_FORMAT_SHIFT 14
#define EG_TEX_FORMAT_MASK  (3UL << EG_TEX_FORMAT_SHIFT)
#define EG_TEX_FORMAT_8888  0UL
#define EG_TEX_FORMAT_4444  1UL     /* MGL_UNSIGNED_SHORT_4_4_4_4: A R G B */
#define EG_TEX_FORMAT_565   2UL     /* MGL_UNSIGNED_SHORT_5_6_5: R G B */
#define EG_TEX_FORMAT_L8    3UL     /* GL_LUMINANCE bytes */

/* eg_device.c */
struct Radeon3DDevice;
struct Radeon3DSurface;
BOOL EgPresent(struct Radeon3DDevice *device, const struct Radeon3DSurface *src,
               const struct Radeon3DSurface *dst, ULONG width, ULONG height,
               BOOL waitVblank);

BOOL EgFlip(struct Radeon3DDevice *device, const struct Radeon3DSurface *s, ULONG fence);
void EgFlipRestore(struct Radeon3DDevice *device);

extern ULONG EgEmitFailStage;
extern ULONG EgEmitFace;

#endif
