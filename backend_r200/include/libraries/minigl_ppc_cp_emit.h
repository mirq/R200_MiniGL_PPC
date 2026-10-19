#ifndef LIBRARIES_MINIGL_PPC_CP_EMIT_H
#define LIBRARIES_MINIGL_PPC_CP_EMIT_H

#include <exec/types.h>

#define MGLPPC_CP_DWORDS 8192UL

struct MGLPPCEmitSurface {
    ULONG cpuAddress, gpuAddress, pitch, width, height, format;
};

typedef char MGLPPCEmitSurfaceSizeCheck[
    sizeof(ULONG) == 4 && sizeof(struct MGLPPCEmitSurface) == 24 ? 1 : -1];

/* Resolve only live, driver-validated handles in the current context/epoch.
 * Texture CPU mappings must remain readable for the emitter's posted-write
 * drain. Zero and unknown handles are not surfaces. No OS calls are needed. */
typedef BOOL (*MGLPPCEmitResolve)(void *user, ULONG handle,
                                struct MGLPPCEmitSurface *out);

struct MGLPPCEmitVertices {
    ULONG gpuAddress, bytes, drawCount;
    BOOL stateBatch;
    const ULONG *offsets;
};

/* words MUST hold MGLPPC_CP_DWORDS dwords, disjoint from the inputs. Records
 * and offsets must remain stable throughout the call. vertices == NULL uses
 * inline records; otherwise gpuAddress/bytes describe a live vertex segment.
 * offsets holds drawCount byte offsets (commit), or drawCount offset/count
 * pairs (stateBatch, one HW-TCL header). At most 192 draws and 8192 record
 * dwords are accepted. The state-batch header's vertex count is ignored.
 *
 * Returns host-native CP dword count, with no endian conversion, or zero on
 * ANY failure. Discard partial words on failure; never publish them. This
 * only builds commands, without submission, MMIO or OS calls. Static scratch
 * is single-context/process and nonreentrant, including resolve callbacks. */
ULONG MGLPPCEmitBatch(ULONG *words, const ULONG *records, ULONG recordDwords,
                     const struct MGLPPCEmitVertices *vertices,
                     MGLPPCEmitResolve resolve, void *user);

/* Why the last MGLPPCEmitBatch returned 0: the emitter's FailStage, or
 * 0xffff when the adapter's own pre-checks rejected the batch. Failure
 * diagnostics only. */
extern ULONG MGLPPCEmitFailStage;

#endif
