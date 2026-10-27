#ifndef RADEON3D_EMIT_H
#define RADEON3D_EMIT_H

/* Dual-target R200 command emitter. Consumes validated semantic draw state
 * and produces the CP dword stream. No ExecBase, no locking, no I/O: the
 * same source drives the 68k service today and the PPC frontend tomorrow.
 * Surface state arrives as resolved value descriptors; how a surface handle
 * maps to a descriptor is caller policy (session table on the 68k, a
 * query-and-cache on the PPC). */

#include <exec/types.h>

#include "radeon3d_records.h" /* record format: consumer-owned since 0.41 */

/* Shared float-bit validators: identical semantics on both sides of the
 * emitter boundary, so they live here rather than in either consumer. */
static inline ULONG UnsignedFloatBits(ULONG value)
{
    ULONG top = 0;
    ULONG scan = value;

    if (!value)
        return 0;
    while (scan >>= 1)
        ++top;
    return ((127UL + top) << 23) |
           ((value << (23UL - top)) & 0x007fffffUL);
}

static inline ULONG UnsignedHalfFloatBits(ULONG value)
{
    ULONG bits = UnsignedFloatBits(value);
    return bits ? bits - (1UL << 23) : 0;
}

static inline BOOL ValidFloat(ULONG bits)
{
    return (bits & 0x7f800000UL) != 0x7f800000UL;
}

static inline BOOL ValidPositiveFloat(ULONG bits)
{
    return bits != 0 && !(bits & 0x80000000UL) &&
           (bits & 0x7f800000UL) != 0x7f800000UL;
}

static inline BOOL ValidScreenCoordinate(ULONG bits, ULONG limit)
{
    return !(bits & 0x80000000UL) &&
           (bits & 0x7f800000UL) != 0x7f800000UL &&
           bits <= UnsignedFloatBits(limit);
}

/* Record option flag defined alongside the service ABI constants. */
#define RADEON3D_EXEC_SUPPRESS_COLOR_WRITE 0x80000000UL

#include "../src/radeon_regs.h"

struct Radeon3DEmitSurface {
    APTR CpuAddress;
    ULONG GpuAddress;
    ULONG Pitch;
    ULONG Width;
    ULONG Height;
    ULONG Format;
    /* Macro-tiled colour target: RB3D_COLORPITCH gets COLOR_TILE_ENABLE. */
    BOOL Tiled;
};

/*
 * Field order is load-bearing. A capture is ~1.6 KB and the emitter builds
 * one per record, which is far too much to zero in full for a draw that uses
 * none of the optional blocks. Captures are never copied -- Live and Build
 * swap -- so a buffer handed to a builder still holds the record before last,
 * and only the first zone is cleared:
 *
 *   [start, GlobalAmbient)      zeroed per record -- rounded up to the next
 *                               dword boundary, which on the m68k ABI
 *                               slightly overlaps GlobalAmbient[0];
 *                               always compared
 *   [GlobalAmbient, ModelProjection)  read only while Lighting is set, and
 *                               per light only when that light's enable bit
 *                               is set in the freshly written LightControl
 *   [ModelProjection, end)      matrices; read only while the matching flag
 *                               (HardwareTcl, TexGen, NormalVertex) is set
 *
 * The rule that makes the stale tail safe: a builder that sets one of those
 * flags must write that block in full. Both zones are conditioned on flags
 * living in the cleared first zone, so nothing reads a value it did not
 * write. Moving a member between zones changes that contract; keep the zone
 * comments below in step with any addition.
 */
struct Radeon3DEmitState {
    struct Radeon3DEmitSurface Color;
    BOOL ColorValid;
    struct Radeon3DEmitSurface Depth;
    BOOL DepthValid;
    struct Radeon3DEmitSurface Texture;
    BOOL TextureValid;
    struct Radeon3DEmitSurface Texture1;
    BOOL Texture1Valid;
    ULONG Options;
    ULONG Left;
    ULONG Top;
    ULONG Right;
    ULONG Bottom;
    BOOL ClearDepth;
    BOOL FragmentStatePresent;
    BOOL ExtendedVertex;
    BOOL HardwareTcl;
    ULONG TextureOffset;
    ULONG TextureWidth;
    ULONG TextureHeight;
    ULONG TextureState;
    ULONG FragmentState;
    ULONG TextureBytes;
    ULONG Texture1Offset;
    ULONG Texture1Width;
    ULONG Texture1Height;
    ULONG Texture1State;
    ULONG Texture1Bytes;
    ULONG VertexState;
    ULONG FogColor;
    ULONG Viewport[6];
    ULONG TransformFlags;
    BOOL TexGen;
    ULONG TexGenState[2];
    BOOL NormalVertex;
    BOOL Lighting;
    ULONG LightControl;
    /* --- lighting zone: meaningful only while Lighting is set --- */
    ULONG GlobalAmbient[4];
    ULONG EyeVector[4];
    ULONG Material[17];
    ULONG Lights[8][31];
    /* --- matrix zone: never read back from the live capture. Each block is
     *     written in full by the record that sets HardwareTcl / TexGen /
     *     NormalVertex, and EmitExecuteStateCached diffs it against its own
     *     shadows, so SameExecuteState skips it entirely. --- */
    ULONG ModelProjection[16];
    ULONG TexGenMatrix[2][16];
    ULONG ModelView[16];
    ULONG InvModelView[16];
};

/* Resolve a raw handle dword from a record into *slot and return slot, or
 * NULL when the handle is not usable. Called at most once per record field. */
typedef struct Radeon3DEmitSurface *(*Radeon3DEmitResolveFn)(
    void *user, ULONG handle, struct Radeon3DEmitSurface *slot);

struct Radeon3DEmitter {
    ULONG *Words;
    ULONG Count;
    ULONG InterfaceVersion;
    ULONG FailStage;
    Radeon3DEmitResolveFn Resolve;
    void *ResolveUser;
    ULONG ResolveSlot;
    struct Radeon3DEmitSurface Surfaces[4];
    /* Two captures alternating roles. Build receives the record currently
     * being parsed; Live is what was last emitted to the card. A state
     * change swaps the two pointers, so carrying a capture forward costs a
     * pointer exchange rather than a ~1.6 KB structure copy. Both are set
     * by the caller before the first record of a submission. */
    struct Radeon3DEmitState Captures[2];
    struct Radeon3DEmitState *Live;
    struct Radeon3DEmitState *Build;
    BOOL StateValid;
    /* Consumer extension (MiniGL, not driver ABI): the value each register
     * was last given in this submission, so a full state re-emission skips
     * the registers that did not change. An emulator changes blend or
     * texture-combine state between most primitives, and each such change
     * re-emitted the whole ~65-register block. Valid only within one
     * submission (RegCacheGen moves on in Radeon3DEmitResetCaptures); a
     * register's slot is (reg >> 2) & 127, a collision is only a miss. */
    ULONG RegCacheGen;
    ULONG RegCacheTag[128];
    ULONG RegCacheKey[128];
    ULONG RegCacheValue[128];
    /* The MVP matrix and guard-clip scalars are tracked separately from the
     * register block so a matrix-only change between records re-emits ~21
     * dwords instead of the whole TCL state. */
    BOOL GuardClipEmitted;
    BOOL MatrixValid;
    ULONG Matrix[16];
    BOOL TexGenMatrixValid[2];
    ULONG TexGenMatrix[2][16];
    BOOL ModelViewValid;
    ULONG ModelView[16];
    BOOL InvModelViewValid;
    ULONG InvModelView[16];
    /* Commit path: vertices live in a streaming segment; LOAD_VBPNTR +
     * VBUF_2 packets replace the inline vertex stream. The per-record
     * fetch address comes from CommitVertexOffsets[CommitDrawIndex]. */
    BOOL CommitVbuf;
    /* Consumer extension (MiniGL, not driver ABI): depth-only clears keep
     * colour with a no-op blend (the pre-PLANE_MASK_ENABLE workaround)
     * instead of a zero plane mask. Set for A/B (MGLPPC_CLEARBLEND=1). */
    BOOL ClearBlend;
    ULONG CommitSegmentGpuBase;
    ULONG CommitSegmentBytes;
    const ULONG *CommitVertexOffsets;
    ULONG CommitDrawIndex;
    ULONG CommitVbufAddress;
    /* Indexed commit draws: per-draw index-list offsets (NULL = none) and
     * the current draw's, or 0 for a plain DRAW_VBUF_2. */
    const ULONG *CommitIndexOffsets;
    ULONG CommitIndexOffset;
};

/* Point Live/Build at the emitter's two captures. Call once per submission,
 * before the first record; the roles alternate from there. */
static inline void Radeon3DEmitResetCaptures(struct Radeon3DEmitter *emitter)
{
    emitter->Live = &emitter->Captures[0];
    emitter->Build = &emitter->Captures[1];
    emitter->StateValid = FALSE;
    if (++emitter->RegCacheGen == 0) {
        ULONG index;
        for (index = 0; index < 128UL; ++index)
            emitter->RegCacheTag[index] = 0;
        emitter->RegCacheGen = 1;
    }
}

BOOL Radeon3DEmitStream(struct Radeon3DEmitter *emitter,
                        const ULONG *records, ULONG recordDwords);
BOOL Radeon3DEmitDraw(struct Radeon3DEmitter *emitter,
                      const ULONG *record, ULONG length,
                      ULONG primitiveType);
BOOL Radeon3DEmitVbufDraw(struct Radeon3DEmitter *emitter,
                          ULONG commitOffset, ULONG vertexCount,
                          ULONG primitiveType);
BOOL Radeon3DEmitReuseDraw(struct Radeon3DEmitter *emitter,
                           const ULONG *record, ULONG length,
                           ULONG primitiveType);
BOOL Radeon3DEmitClear(struct Radeon3DEmitter *emitter,
                       const ULONG *record, ULONG length);
BOOL Radeon3DEmitPrimitiveType(ULONG opcode, ULONG *primitiveType);

#endif
