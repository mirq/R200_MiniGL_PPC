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

extern ULONG EgEmitFailStage;
extern ULONG EgEmitFace;

#endif
