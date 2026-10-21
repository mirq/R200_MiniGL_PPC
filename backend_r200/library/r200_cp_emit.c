/* Link separately from the frontend unity and the driver's radeon3d_emit.c.
 * All CP encoding and semantic validation remain in that shared emitter. */
#include <stddef.h>
#include <libraries/minigl_ppc_cp_emit.h>
#include <radeon3d_emit.h>

#ifndef MGL_R200_SURFACE_FORMAT_TILED
/* minigl_r200_bridge.h: macro-tiled colour target flag in the format word */
#define MGL_R200_SURFACE_FORMAT_TILED 0x80000000UL
#endif

typedef char MGLPPCCapacityCheck[
    MGLPPC_CP_DWORDS == RADEON3D_MAX_BATCH_DWORDS ? 1 : -1];

/* Radeon3DEmitDraw's largest header: texgen, both normal matrices, lighting
 * state and a dense block for every enabled light. No inline vertices here. */
#define MGLPPC_EMIT_MAX_HEADER \
    (RADEON3D_EXEC_DRAW_TEXGEN_HEADER_DWORDS + \
     RADEON3D_EXEC_NORMAL_MATRICES_DWORDS + RADEON3D_EXEC_LIGHT_STATE_DWORDS + \
     RADEON3D_EXEC_MAX_LIGHT_BLOCKS * RADEON3D_EXEC_LIGHT_BLOCK_DWORDS)

typedef char MGLPPCHeaderCapacityCheck[
    MGLPPC_EMIT_MAX_HEADER <= MGLPPC_CP_DWORDS ? 1 : -1];

static struct Radeon3DEmitter MGLPPCEmitter;
ULONG MGLPPCEmitFailStage;
/* Draw index reached by the emitter when it rejected a batch. */
ULONG MGLPPCEmitFailDraw;
static ULONG MGLPPCEmitHeader[MGLPPC_EMIT_MAX_HEADER];

struct MGLPPCEmitResolver {
    MGLPPCEmitResolve resolve;
    void *user;
};

static struct Radeon3DEmitSurface *MGLPPCResolveSurface(
    void *user, ULONG handle, struct Radeon3DEmitSurface *slot)
{
    struct MGLPPCEmitResolver *resolver = user;
    struct MGLPPCEmitSurface surface = {0};
    ULONG bytesPerPixel, rowBytes, span;

    if (!handle || !resolver->resolve(resolver->user, handle, &surface))
        return NULL;
    /* The library marks a macro-tiled colour target in the format word. */
    slot->Tiled = (surface.format & MGL_R200_SURFACE_FORMAT_TILED) != 0;
    surface.format &= ~MGL_R200_SURFACE_FORMAT_TILED;
    switch (surface.format) {
    case RADEON3D_FORMAT_CLUT8: bytesPerPixel = 1UL; break;
    case RADEON3D_FORMAT_R5G6B5PC: bytesPerPixel = 2UL; break;
    case RADEON3D_FORMAT_B8G8R8A8: bytesPerPixel = 4UL; break;
    default: return NULL;
    }
    /* The emitter relies on the native import/allocation checks for nonzero
     * pitch and bounded extents, notably before texture division and reads.
     * Actual mapping ownership/lifetime is still the resolver's contract. */
    if (!surface.cpuAddress || !surface.pitch || (surface.pitch & 63UL) ||
        !surface.width || surface.width > 65536UL ||
        !surface.height || surface.height > 65536UL ||
        surface.height > 0xffffffffUL / surface.pitch)
        return NULL;
    rowBytes = surface.width * bytesPerPixel;
    if (rowBytes > surface.pitch)
        return NULL;
    span = (surface.height - 1UL) * surface.pitch + rowBytes;
    if (span - 1UL > 0xffffffffUL - surface.cpuAddress ||
        span - 1UL > 0xffffffffUL - surface.gpuAddress)
        return NULL;
    slot->CpuAddress = (APTR)(unsigned long)surface.cpuAddress;
    slot->GpuAddress = surface.gpuAddress;
    slot->Pitch = surface.pitch;
    slot->Width = surface.width;
    slot->Height = surface.height;
    slot->Format = surface.format;
    return slot;
}

/* Pre-emit validation rejects record a distinct MGLPPCEmitFailStage
 * (0x1001..) so a publish failure can name the check that refused it. */
#define EMIT_REJECT(stage) do { MGLPPCEmitFailStage = (stage); return 0; } while (0)

ULONG MGLPPCEmitBatch(ULONG *words, const ULONG *records, ULONG recordDwords,
                     const struct MGLPPCEmitVertices *vertices,
                     MGLPPCEmitResolve resolve, void *user)
{
    struct Radeon3DEmitter *emitter = &MGLPPCEmitter;
    struct MGLPPCEmitResolver resolver;
    ULONG index, walk, draws = 0, primitiveType = 0;
    BOOL stateBatch = vertices && vertices->stateBatch;
    BOOL built;

    MGLPPCEmitFailStage = 0xffffUL;
    if (!words || !records || !resolve || !recordDwords ||
        recordDwords > MGLPPC_CP_DWORDS)
        EMIT_REJECT(0x1001UL);
    if (vertices && (!vertices->offsets || !vertices->drawCount ||
        vertices->drawCount > RADEON3D_STATE_BATCH_MAX_DRAWS ||
        !vertices->bytes || vertices->bytes > RADEON3D_MAX_SEGMENT_BYTES ||
        (vertices->gpuAddress & 3UL) ||
        vertices->bytes - 1UL > 0xffffffffUL - vertices->gpuAddress))
        EMIT_REJECT(0x1002UL);

    if (stateBatch) {
        if (recordDwords < RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS ||
            recordDwords > MGLPPC_EMIT_MAX_HEADER ||
            vertices->drawCount > (MGLPPC_CP_DWORDS - recordDwords) / 2UL ||
            records[1] != recordDwords ||
            !Radeon3DEmitPrimitiveType(records[0], &primitiveType))
            EMIT_REJECT(0x1003UL);
    } else {
        /* The shared emitter consumes offsets without an array length. Walk
         * first, as CommitRecords does, before it can index that array. */
        walk = 0;
        while (walk < recordDwords) {
            ULONG opcode, length;

            if (recordDwords - walk < 2UL)
                EMIT_REJECT(0x1004UL);
            opcode = records[walk];
            length = records[walk + 1UL];
            if (length < 2UL || length > recordDwords - walk)
                EMIT_REJECT(0x1005UL);
            if (opcode >= RADEON3D_EXEC_DRAW_TRIANGLES &&
                opcode <= RADEON3D_EXEC_DRAW_LINE_LOOP) {
                if (length < RADEON3D_EXEC_DRAW_HEADER_DWORDS)
                    EMIT_REJECT(0x1006UL);
                ++draws;
            } else if (vertices && opcode >= RADEON3D_EXEC_REUSE_TRIANGLES &&
                       opcode <= RADEON3D_EXEC_REUSE_LINE_LOOP) {
                if (length != RADEON3D_EXEC_REUSE_DWORDS)
                    EMIT_REJECT(0x1007UL);
                ++draws;
            } else if (opcode != RADEON3D_EXEC_CLEAR ||
                       length != RADEON3D_EXEC_CLEAR_DWORDS)
                EMIT_REJECT(0x1008UL);
            if (draws > RADEON3D_STATE_BATCH_MAX_DRAWS)
                EMIT_REJECT(0x1009UL);
            walk += length;
        }
        if (vertices && draws != vertices->drawCount)
            EMIT_REJECT(0x100AUL);
    }
    if (vertices) {
        for (index = 0; index < vertices->drawCount; ++index) {
            ULONG offset = vertices->offsets[index * (stateBatch ? 2UL : 1UL)];
            ULONG indexOffset = vertices->indexOffsets
                ? vertices->indexOffsets[index] : 0UL;

            if ((offset & 3UL) || offset >= vertices->bytes ||
                (stateBatch && !vertices->offsets[index * 2UL + 1UL]))
                EMIT_REJECT(0x100BUL);
            if (indexOffset &&
                ((indexOffset & 3UL) || indexOffset <= offset ||
                 indexOffset >= vertices->bytes))
                EMIT_REJECT(0x100CUL);
        }
    }

    resolver.resolve = resolve;
    resolver.user = user;
    emitter->Words = words;
    emitter->Count = 0;
    emitter->InterfaceVersion = RADEON3D_IFACE_VERSION;
    emitter->FailStage = 0;
    emitter->Resolve = MGLPPCResolveSurface;
    emitter->ResolveUser = &resolver;
    emitter->ResolveSlot = 0;
    /* Match the native per-submission reset; never clear the large captures.
     * Their optional tails are predicate-gated and written by the emitter. */
    Radeon3DEmitResetCaptures(emitter);
    emitter->Live->TextureState = 0;
    emitter->Live->Texture1State = 0;
    emitter->GuardClipEmitted = FALSE;
    emitter->MatrixValid = FALSE;
    emitter->TexGenMatrixValid[0] = FALSE;
    emitter->TexGenMatrixValid[1] = FALSE;
    emitter->ModelViewValid = FALSE;
    emitter->InvModelViewValid = FALSE;
    emitter->CommitVbuf = vertices != NULL;
    emitter->ClearBlend = MGLPPCClearBlend != 0;
    emitter->CommitSegmentGpuBase = vertices ? vertices->gpuAddress : 0;
    emitter->CommitSegmentBytes = vertices ? vertices->bytes : 0;
    emitter->CommitVertexOffsets = vertices ? vertices->offsets : NULL;
    emitter->CommitIndexOffsets = vertices ? vertices->indexOffsets : NULL;
    emitter->CommitIndexOffset = 0;
    emitter->CommitDrawIndex = 0;
    emitter->CommitVbufAddress = 0;

    if (stateBatch) {
        for (index = 0; index < recordDwords; ++index)
            MGLPPCEmitHeader[index] = records[index];
        MGLPPCEmitHeader[10] = vertices->offsets[1];
        built = Radeon3DEmitDraw(emitter, MGLPPCEmitHeader, recordDwords,
                                primitiveType);
        for (index = 1; built && index < vertices->drawCount; ++index) {
            emitter->CommitIndexOffset = vertices->indexOffsets
                ? vertices->indexOffsets[index] : 0UL;
            built = Radeon3DEmitVbufDraw(emitter, vertices->offsets[index * 2UL],
                vertices->offsets[index * 2UL + 1UL], primitiveType);
        }
    } else {
        built = Radeon3DEmitStream(emitter, records, recordDwords);
        if (vertices && emitter->CommitDrawIndex != vertices->drawCount)
            built = FALSE;
    }
    MGLPPCEmitFailDraw = emitter->CommitDrawIndex;
    emitter->CommitVbuf = FALSE;
    emitter->CommitSegmentGpuBase = 0;
    emitter->CommitSegmentBytes = 0;
    emitter->CommitVertexOffsets = NULL;
    emitter->CommitIndexOffsets = NULL;
    emitter->CommitIndexOffset = 0;
    emitter->CommitDrawIndex = 0;
    emitter->CommitVbufAddress = 0;
    emitter->StateValid = FALSE;
    emitter->ResolveUser = NULL;
    emitter->Words = NULL;
    MGLPPCEmitFailStage = built ? 0 : emitter->FailStage;
    return built ? emitter->Count : 0;
}
