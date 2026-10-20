#include "minigl_r200_internal.h"
#include "r200_batch_budget.h"
#include <math.h>
#include "debug_output.h"
#if MGLPPC_PROFILE
/* EXPERIMENT: FlushScreenVertices submit reasons (rotate, mode, mismatch,
 * statebatch-full, records, generated, maxdraws). */
ULONG R200FlushSplit[8];
#define FLUSH_SPLIT(n) (++R200FlushSplit[n])
#else
#define FLUSH_SPLIT(n) ((void)0)
#endif


/* The VAP fetches vertex records from the segment in little-endian, while
 * the CPU builds them big-endian. Inline Execute vertices are swapped by
 * the CP ring writer; streaming vertices bypass the ring, so the frontend
 * must produce ring-order dwords itself. */
static __inline__ ULONG SwapSegmentWord(ULONG value)
{
#if defined(__GNUC__) && defined(__m68k__)
    __asm__ __volatile__ ("ror.w #8,%0\n\tswap %0\n\tror.w #8,%0"
                          : "=d" (value) : "0" (value));
    return value;
#else
    return (value << 24) | ((value << 8) & 0x00ff0000UL) |
           ((value >> 8) & 0x0000ff00UL) | (value >> 24);
#endif
}

/* The streaming movem serializers below assume the record-relevant
 * ScreenVertex fields sit at their documented offsets (texture set at
 * byte 32, colour at byte 28). Guard them like the PendingDrawState
 * alignment invariant rather than trusting struct layout by eye. */
#ifdef __GNUC__
typedef char ScreenVertexSerializeLayoutCheck[
    __builtin_offsetof(struct ScreenVertex, color) == 28UL &&
    __builtin_offsetof(struct ScreenVertex, s) == 32UL &&
    sizeof(struct ScreenVertex) == 13UL * sizeof(ULONG) ? 1 : -1];
#elif defined(__VBCC__) && defined(__PPC__)
#include <stddef.h>
typedef char ScreenVertexSerializeLayoutCheck[
    offsetof(struct ScreenVertex, color) == 28UL &&
    offsetof(struct ScreenVertex, s) == 32UL &&
    sizeof(struct ScreenVertex) == 13UL * sizeof(ULONG) ? 1 : -1];
#endif

/* Mismatch submits a frame must burn before expanding slots into per-draw
 * records. Gears-level workloads mismatch ~3x per frame and keep the compact
 * state-batch shape; the threshold sits above them and below the world-scene
 * split rates (~13/frame) it exists for. Reset per present.
 * Build-time override (Makefile MGLPPC_EXPAND_MIN) for the Quake A/B:
 * 2026-09-20 CP_TRIG evidence shows ~8 mismatch submits/frame at 4. */
#ifndef R200_EXPAND_MISMATCH_MIN
#define R200_EXPAND_MISMATCH_MIN 4UL
#endif

static BOOL CanExpandStateBatch(struct MiniGLR200Context *context,
                               struct SubmitSlot *slot, ULONG nextHeader,
                               ULONG nextGenerated, ULONG *expandedGenerated)
{
    ULONG expandedRecords;
    if (!slot->drawCount ||
        slot->recordCount > context->batchDwords / slot->drawCount ||
        nextGenerated > context->batchDwords)
        return FALSE;
    expandedRecords = slot->drawCount * slot->recordCount;
    if (nextHeader > context->batchDwords - expandedRecords ||
        !R200SlotPayloadFits(context->batchDwords,
                            expandedRecords + nextHeader,
                            slot->drawCount + 1UL, FALSE, R200_PPC_FRONTEND))
        return FALSE;
    return R200ExpandedBatchCost(slot->generatedDwordCount, slot->drawCount,
                                COMMIT_FETCH_DWORDS,
                                context->batchDwords - nextGenerated,
                                (slot->records[5] & RADEON3D_DRAW_LIGHTING) != 0,
                                expandedGenerated);
}

/* Expand a compact state batch into one full record per draw. The caller has
 * already validated capacity with CanExpandStateBatch(). The copy must run
 * backwards: every destination record sits at or above the shared header it
 * copies from. Kept as a pure helper so the accounting invariants (record
 * count, per-draw vertex counts, last-draw cursor) are CPU-testable; the
 * 2026-09-20 grey-screen incident makes this path hardware-sensitive. */
static void R200ExpandStateBatch(struct SubmitSlot *slot,
                                 ULONG expandedGenerated)
{
    ULONG draws = slot->drawCount;
    ULONG headerDwords = slot->recordCount;
    ULONG index = draws;

    while (index-- > 1UL) {
        ULONG *destination = slot->records + index * headerDwords;
        ULONG source;

        for (source = 0; source < headerDwords; ++source)
            destination[source] = slot->records[source];
        destination[10] = slot->stateBatchDraws[index].VertexCount;
    }
    slot->records[10] = slot->stateBatchDraws[0].VertexCount;
    slot->recordCount = draws * headerDwords;
    slot->generatedDwordCount = expandedGenerated;
    slot->lastDrawOffset = (draws - 1UL) * headerDwords;
    slot->lastDrawHeaderDwords = headerDwords;
    slot->lastDrawValid = TRUE;
    slot->stateBatch = FALSE;
    slot->expanded = TRUE;
}

static BOOL PrepareScreenAppend(struct MiniGLR200Context *context,
                                ULONG command)
{
    struct PendingDrawState *state;
    ULONG normalizedCommand = command ? command : RADEON3D_EXEC_DRAW_TRIANGLES;

#if R200_PPC_FRONTEND && defined(__PPC__) && !defined(__m68k__)
    /* The draw header bakes in the current texture surface handles. A
     * deferred direct-texture tile must be installed (and its mirror
     * serialized) before this capture reads them, or the record would
     * name the surface the upload is replacing. */
#if MGLPPC_PROFILE
    {
        ULONG pendingStart = PerfClock();
        BOOL pendingOk = R200PPCPendingUploadFlush(context);

        context->perf.indexScanTicks += PerfClock() - pendingStart;
        if (!pendingOk)
            return FALSE;
    }
#else
    if (!R200PPCPendingUploadFlush(context))
        return FALSE;
#endif
#endif
    if (context->screenVertexCount && context->pendingDrawValid &&
        context->pendingSemanticStateSerial == context->semanticStateSerial &&
        context->screenCommand == normalizedCommand &&
        context->pendingProjectiveRequest == context->screenProjective &&
        context->pendingHardwareTransformRequest ==
            context->screenHardwareTransform) {
        if (context->perfEnabled)
            context->perf.snapshotFastHits++;
        return TRUE;
    }
    if (context->perfEnabled)
        context->perf.snapshotBuilds++;
    /* Strips and fans flush at glEnd, so screenVertexCount is usually zero
     * here even though nothing semantic changed since the previous block.
     * Reuse the last built snapshot when its serial, primitive command,
     * and layout requests all match; CapturePendingDrawState is a full
     * header rebuild plus a DrawRectangle/residency pass. Residency and
     * texture-identity changes bump the serial themselves, so a matching
     * serial guarantees the cached surface handles are still live. */
    if (context->captureCacheValid &&
        context->captureCacheSerial == context->semanticStateSerial &&
        context->captureCacheCommand == normalizedCommand &&
        context->captureCacheHardwareTransform ==
            context->screenHardwareTransform &&
        context->captureCacheProjective == context->screenProjective) {
        state = &context->captureCache;
        if (context->perfEnabled)
            context->perf.captureCacheHits++;
    } else {
        ULONG previousFloatMode = BeginSemanticFloatMode();
        BOOL captured;
#if MGLPPC_PROFILE
        ULONG captureStart = PerfClock();
#endif

        state = &context->captureScratch;
        captured = CapturePendingDrawState(context, command, state);
        EndSemanticFloatMode(previousFloatMode);
#if MGLPPC_PROFILE
        context->perf.matrixTicks += PerfClock() - captureStart;
#endif
        if (!captured)
            return FALSE;
        /* Residency work inside the capture can legitimately have bumped
         * the serial (fresh surface handles); key the cache on the value
         * as of now, after the capture completed. */
        CopyPendingDrawState(&context->captureCache, state);
        context->captureCacheSerial = context->semanticStateSerial;
        context->captureCacheCommand = normalizedCommand;
        context->captureCacheHardwareTransform =
            context->screenHardwareTransform;
        context->captureCacheProjective = context->screenProjective;
        context->captureCacheValid = TRUE;
    }
    if (context->screenVertexCount &&
        (!context->pendingDrawValid ||
         context->screenCommand != normalizedCommand ||
         !SamePendingDrawState(&context->pendingDraw, state))) {
        if (context->perfEnabled)
            context->perf.deferredSplits++;
        if (!FlushScreenVertices(context))
            return FALSE;
    } else if (context->screenVertexCount) {
        /* A state call may normalize away (for example BlendFunc while
         * blending is disabled). Observe it without splitting geometry.
         * The command cannot change here: a different primitive flushed
         * above, because appending it to the buffered vertices would
         * change the record's vertex-count arithmetic (quads must stay a
         * multiple of four, triangles a multiple of three). */
        context->pendingSemanticStateSerial = context->semanticStateSerial;
        context->screenCommand = normalizedCommand;
        context->pendingProjectiveRequest = context->screenProjective;
        context->pendingHardwareTransformRequest =
            context->screenHardwareTransform;
    }
    if (!context->screenVertexCount) {
        CopyPendingDrawState(&context->pendingDraw, state);
        context->pendingDrawValid = TRUE;
        context->pendingSemanticStateSerial = context->semanticStateSerial;
        context->screenCommand = normalizedCommand;
        context->pendingProjectiveRequest = context->screenProjective;
        context->pendingHardwareTransformRequest =
            context->screenHardwareTransform;
    }
    return TRUE;
}

BOOL FlushScreenVertices(struct MiniGLR200Context *context)
{
    struct SubmitSlot *slot = BuildSubmitSlot(context);
    const struct PendingDrawState *state = &context->pendingDraw;
#if R200_DUMP_FIRST_BATCHES
    /* Temporary diagnostics: capture the flush inputs before any early
     * out, so a silent drop is attributable. */
    {
        static LONG flushDumps = 6;
        if (flushDumps > 0) {
            extern struct DosLibrary *DOSBase;
            BPTR file;
            char path[32];
            ULONG data[8];
            static const char hex[] = "0123456789";
            int pos = 0, digit;
            ULONG value = (ULONG)(6 - flushDumps);
            char tail[4];
            int tailPos = 0;
            --flushDumps;
            if (DOSBase) {
                file = 0;
                value = 6 - flushDumps - 1UL;
                if (!value)
                    tail[tailPos++] = '0';
                while (value) {
                    tail[tailPos++] = hex[value % 10UL];
                    value /= 10UL;
                }
                path[pos++] = 'T'; path[pos++] = ':'; path[pos++] = 'r';
                path[pos++] = '2'; path[pos++] = '0'; path[pos++] = '0';
                path[pos++] = '_'; path[pos++] = 'f'; path[pos++] = 'l';
                path[pos++] = 'u'; path[pos++] = 's'; path[pos++] = 'h';
                while (tailPos) path[pos++] = tail[--tailPos];
                path[pos++] = '.'; path[pos++] = 'b'; path[pos++] = 'i';
                path[pos++] = 'n'; path[pos] = 0;
                data[0] = context->screenVertexCount;
                data[1] = context->pendingDrawValid;
                data[2] = state->drawEnabled;
                data[3] = state->drawRectangleValid;
                data[4] = state->headerDwords;
                data[5] = state->hardwareTransform;
                data[6] = context->error;
                data[7] = context->insideBegin;
                file = Open((STRPTR)path, MODE_NEWFILE);
                if (file) {
                    Write(file, data, sizeof(data));
                    Close(file);
                }
            }
        }
    }
#endif
    const struct PendingDrawState *stateUnused = state;
    (void)stateUnused;
    BOOL extendedVertex, hardwareTransform, texgen;
    ULONG headerDwords, vertexDwords;
    ULONG required, generatedRequired, growthRequired, generatedGrowth;
    ULONG *header = state->header;
    ULONG *record, *destination, index;
    BOOL merge = FALSE;
    BOOL useCommit, stateBatch;
    ULONG vertexBytes;
    ULONG serializeStart = 0, executeBefore = 0, waitBefore = 0;
#if R200_PPC_FRONTEND
    ULONG stallBefore = 0;
#endif
    /* Loop-invariant attribute masks for the streaming movem serializers:
     * ANDing with 0 collapses a disabled texture unit's or fog's stale
     * source dwords to zero without branching inside the vertex loop. */
    ULONG unit0Mask = state->textured[0] ? 0xFFFFFFFFUL : 0UL;
    ULONG unit1Mask = state->textured[1] ? 0xFFFFFFFFUL : 0UL;
    ULONG fogMask = state->fog ? 0xFFFFFFFFUL : 0UL;

    if (!context->screenVertexCount)
        return TRUE;
    if (!context->pendingDrawValid)
        return FALSE;
#if R200_PPC_FRONTEND && defined(__PPC__) && !defined(__m68k__)
    /* Geometry being flushed is the last chance to install a deferred
     * direct-texture tile before the records naming the texture are
     * published. The staging buffer is PPC memory, so the client's texels
     * were already consumed at the upload call. */
    if (!R200PPCPendingUploadFlush(context))
        return FALSE;
#endif
    headerDwords = state->headerDwords;
    vertexDwords = state->vertexDwords;
    extendedVertex = headerDwords == RADEON3D_EXEC_DRAW_EXTENDED_HEADER_DWORDS;
    hardwareTransform = state->hardwareTransform;
    texgen = (header[5] & RADEON3D_DRAW_TEXGEN) != 0;
    if (context->perfEnabled) {
        context->perf.flushCalls++;
        context->perf.lastBoundTexture0 = state->textureName[0];
        if (state->textured[0])
            context->perf.flushTextured0++;
        else
            context->perf.flushUntextured0++;
    }
    if (context->perfEnabled) {
        executeBefore = context->perf.executeTicks;
        waitBefore = context->perf.waitTicks;
#if R200_PPC_FRONTEND
        stallBefore = R200PPCPlatformStallTicks();
#endif
        serializeStart = PerfClock();
    }
    if (!state->drawEnabled) {
        context->screenVertexCount = 0;
        context->screenCommand = RADEON3D_EXEC_DRAW_TRIANGLES;
        context->pendingDrawValid = FALSE;
        return TRUE;
    }
    if (!state->drawRectangleValid) {
        if (context->perfEnabled)
            context->perf.dropDrawRectangle++;
        context->screenVertexCount=0;
        context->screenCommand=RADEON3D_EXEC_DRAW_TRIANGLES;
        context->pendingDrawValid=FALSE;
        return TRUE;
    }
    /* Validate reciprocal W as a floating-point attribute, never against the
     * drawable's coordinate range. The Radeon3D service consumes it as RHW.
     * Reject an impossible value rather than silently clamping and changing
     * interpolation. */
    if (state->perspective) {
        for (index=0;index<context->screenVertexCount;++index) {
            GLfloat rhw=context->screenVertices[index].rhw;
            if (!FloatFinite(rhw) || rhw<=0.0f) {
                if (context->perfEnabled) context->perf.rhwClamps++;
                SetError(context,GL_INVALID_OPERATION);
                context->screenVertexCount=0;
                context->screenCommand=RADEON3D_EXEC_DRAW_TRIANGLES;
                context->pendingDrawValid=FALSE;
                return FALSE;
            }
        }
    }
    vertexBytes = context->screenVertexCount * vertexDwords * 4UL;
    /* Streaming commits place the vertices in this slot's segment half and
     * send header-only records. The RV280 fetch caches never re-read
     * rewritten lines at the same address, so each half fills monotonically;
     * when the build slot's half can no longer hold a draw, rotate to a
     * freshly allocated segment (a different VRAM region, so resetting the
     * cursors cannot re-present old addresses) instead of writing past the
     * half. If rotation is impossible the flush degrades to the inline
     * Execute path, whose vertices travel in the record itself. */
    useCommit = R200_STREAM_COMMITS
                    ? (context->streamSegments && context->segment.valid &&
                       hardwareTransform)
                    : FALSE;
    if (useCommit && !SegmentHalfFits(context, slot, vertexBytes)) {
        FLUSH_SPLIT(0);
        if (RotateStreamSegment(context, vertexBytes)) {
            slot = BuildSubmitSlot(context);
            /* Rotation submitted the pending records (the old segment's
             * fence may still be in flight -- its region is never
             * rewritten), parked or freed it, and landed us on a fresh
             * slot whose half is empty. */
        } else {
            useCommit = FALSE;
            slot = BuildSubmitSlot(context);
        }
    }
    /* A commit record carries no vertex tail and requires hardware TCL.
     * Quake mixes object-space world draws with pre-projected MGL_FLAT
     * primitives, so a mode change must close the current homogeneous slot
     * rather than forcing the later draw into the earlier mode. */
    if (slot->modeDecided && slot->commitMode != useCommit) {
        FLUSH_SPLIT(1);
        if (!Submit(context))
            return FALSE;
        slot = BuildSubmitSlot(context);
        /* Submit() lands us on the sibling slot, whose write cursor is its
         * own monotonic value: re-validate ITS remaining space before this
         * flush may commit into it, or the vertex tail would be written past
         * the half (corrupting the sibling region or memory beyond the
         * segment) and the offset table would carry out-of-range entries,
         * which the service rejects at its stage 44. */
        if (useCommit && !SegmentHalfFits(context, slot, vertexBytes)) {
            StreamDiagEvent(context, 0x20UL, vertexBytes,
                            slot->segmentCursor);
            useCommit = FALSE;
        }
    }
    if (!slot->modeDecided) {
        slot->commitMode = useCommit;
        slot->modeDecided = TRUE;
    }
    required = useCommit
        ? headerDwords
        : headerDwords + context->screenVertexCount * vertexDwords;
    generatedRequired = hardwareTransform
        ? GeneratedHardwareDrawDwords(useCommit ? 0UL
                                                : context->screenVertexCount,
                                       state->depth, state->textured[0],
                                       state->textured[1], state->fog,
                                       texgen, state->normalVertex,
                                       state->enabledLights) +
              (useCommit ? COMMIT_FETCH_DWORDS : 0UL)
        : GeneratedDrawDwords(context->screenVertexCount, state->depth,
                              state->textured[0], state->textured[1],
                              state->fog, state->perspective);
    header[1] = required;
    header[10] = context->screenVertexCount;
    /* Accumulation requires a slot that is empty or an unexpanded state
     * batch. An expanded slot keeps per-draw records and must never grow
     * its draw table again, or the entry claims more records than its
     * record stream holds and the service rejects the batch (stage 5).
     * Clear-prefix slots keep the legacy flow: the mismatch block below
     * submits the clear and the next slot accumulates compactly. */
    stateBatch = useCommit &&
                 (context->radeonCaps &
                  RADEON3D_CAP_COMMIT_STATE_BATCH) != 0 &&
                 !slot->expanded;
    if (stateBatch && slot->recordCount) {
        BOOL sameState = slot->stateBatch &&
                         slot->recordCount == headerDwords;
        ULONG expandedGenerated = 0;

        for (index = 0; sameState && index < headerDwords; ++index)
            if (index != 10 && slot->records[index] != header[index])
                sameState = FALSE;
        if (!sameState &&
            slot->stateBatch &&
            context->mismatchSubmits >= R200_EXPAND_MISMATCH_MIN &&
            CanExpandStateBatch(context, slot, headerDwords,
                                generatedRequired, &expandedGenerated)) {
            /* A differing header no longer forces a submit: expand the
             * pending same-header batch into per-draw full records -- each
             * carries its own texture/matrix state -- and keep filling this
             * slot in the plain commit-record shape that the host COMMIT
             * entry and the service already consume. */
            R200ExpandStateBatch(slot, expandedGenerated);
            stateBatch = FALSE;
        } else if (!sameState) {
            /* A clear may precede the first draw without deciding slot mode.
             * State batches cannot carry clears, so submit that prefix through
             * Execute instead of presenting an empty CommitBatch. */
            if (!slot->drawCount)
                slot->commitMode = FALSE;
            ++context->mismatchSubmits;
            FLUSH_SPLIT(2);
            if (context->perfEnabled)
                context->perf.deferredSplits++;
            if (!Submit(context))
                return FALSE;
            slot = BuildSubmitSlot(context);
            if (!SegmentHalfFits(context, slot, vertexBytes)) {
                StreamDiagEvent(context, 0x22UL, vertexBytes,
                                slot->segmentCursor);
                useCommit = FALSE;
                stateBatch = FALSE;
                required = headerDwords +
                           context->screenVertexCount * vertexDwords;
                generatedRequired = GeneratedHardwareDrawDwords(
                    context->screenVertexCount, state->depth,
                    state->textured[0], state->textured[1], state->fog,
                    texgen, state->normalVertex, state->enabledLights);
            }
            slot->commitMode = useCommit;
            slot->modeDecided = TRUE;
        }
    }
    if (stateBatch) {
        required = slot->drawCount ? 0UL : headerDwords;
        generatedRequired = slot->drawCount
            ? COMMIT_FETCH_DWORDS
            : GeneratedHardwareDrawDwords(
                  0UL, state->depth, state->textured[0], state->textured[1],
                  state->fog, texgen, state->normalVertex, state->enabledLights) +
                  COMMIT_FETCH_DWORDS;
        growthRequired = required;
        generatedGrowth = generatedRequired;
        if (slot->drawCount >= MAX_SLOT_DRAWS ||
            !R200SlotPayloadFits(context->batchDwords, headerDwords,
                                 slot->drawCount + 1UL, TRUE, R200_PPC_FRONTEND) ||
            slot->generatedDwordCount + generatedGrowth >
                context->batchDwords) {
            FLUSH_SPLIT(3);
            if (!Submit(context))
                return FALSE;
            slot = BuildSubmitSlot(context);
            if (!SegmentHalfFits(context, slot, vertexBytes)) {
                StreamDiagEvent(context, 0x23UL, vertexBytes,
                                slot->segmentCursor);
                useCommit = FALSE;
                stateBatch = FALSE;
            } else {
                slot->commitMode = TRUE;
                slot->modeDecided = TRUE;
                required = headerDwords;
                growthRequired = headerDwords;
                generatedRequired = GeneratedHardwareDrawDwords(
                    0UL, state->depth, state->textured[0],
                    state->textured[1], state->fog, texgen, state->normalVertex,
                    state->enabledLights) + COMMIT_FETCH_DWORDS;
                generatedGrowth = generatedRequired;
            }
        }
        if (stateBatch) {
            if (!slot->drawCount) {
                header[1] = headerDwords;
                for (index = 0; index < headerDwords; ++index)
                    slot->records[index] = header[index];
                slot->stateBatch = TRUE;
            }
            slot->vertexOffsets[slot->drawCount] =
                context->buildSlot * SEGMENT_HALF_BYTES +
                slot->segmentCursor;
            slot->stateBatchDraws[slot->drawCount].OffsetBytes =
                slot->vertexOffsets[slot->drawCount];
            slot->stateBatchDraws[slot->drawCount].VertexCount =
                context->screenVertexCount;
            slot->drawCount++;
            slot->lastDrawValid = FALSE;
            destination = (ULONG *)
                ((ULONG)context->segment.info.CpuAddress +
                 context->buildSlot * SEGMENT_HALF_BYTES +
                 slot->segmentCursor);
            goto serialize_vertices;
        }
        required = headerDwords +
                   context->screenVertexCount * vertexDwords;
        generatedRequired = GeneratedHardwareDrawDwords(
            context->screenVertexCount, state->depth, state->textured[0],
            state->textured[1], state->fog, texgen, state->normalVertex,
            state->enabledLights);
    }
    header[1] = required;
    if (slot->lastDrawValid &&
        slot->lastDrawOffset < slot->recordCount) {
        record = slot->records + slot->lastDrawOffset;
        merge = slot->lastDrawHeaderDwords == headerDwords &&
                ((header[0] == RADEON3D_EXEC_DRAW_TRIANGLES &&
                  record[0] == RADEON3D_EXEC_DRAW_TRIANGLES) ||
                 (header[0] == RADEON3D_EXEC_DRAW_QUADS &&
                  record[0] == RADEON3D_EXEC_DRAW_QUADS)) &&
                slot->lastDrawOffset + record[1] == slot->recordCount &&
                record[10] + context->screenVertexCount <=
                    (header[0] == RADEON3D_EXEC_DRAW_QUADS
                          ? state->quadVertexLimit
                         : MAX_SCREEN_VERTICES);
        for (index = 2; merge && index < headerDwords; ++index)
            if (index != 10 && record[index] != header[index])
                merge = FALSE;
    }
    /* A merged commit record grows only in its vertex count: the vertices
     * live in the segment, so neither the record nor the generated stream
     * gets longer. */
    growthRequired = merge
        ? (useCommit ? 0UL : context->screenVertexCount * vertexDwords)
        : required;
    generatedGrowth = merge
        ? (useCommit ? 0UL
                     : context->screenVertexCount *
                           state->generatedVertexDwords)
        : generatedRequired;
    if (!R200SlotPayloadFits(context->batchDwords,
                            slot->recordCount + growthRequired,
                            useCommit ? slot->drawCount + (merge ? 0UL : 1UL) : 0UL,
                            FALSE, R200_PPC_FRONTEND) ||
        slot->generatedDwordCount + generatedGrowth >
            context->batchDwords ||
        (useCommit && slot->drawCount >= MAX_SLOT_DRAWS)) {
        if (!R200SlotPayloadFits(context->batchDwords,
                                slot->recordCount + growthRequired,
                                useCommit ? slot->drawCount + (merge ? 0UL : 1UL) : 0UL,
                                FALSE, R200_PPC_FRONTEND))
            FLUSH_SPLIT(4);
        else if (slot->generatedDwordCount + generatedGrowth >
                 context->batchDwords)
            FLUSH_SPLIT(5);
        else
            FLUSH_SPLIT(6);
        if (!Submit(context)) return FALSE;
        slot = BuildSubmitSlot(context);
        /* The landed sibling's half has its own monotonic cursor: degrade
         * this flush to the inline shape when it cannot hold the vertices
         * (see the mode-change comment above), and rebuild the record size
         * the degraded shape needs. */
        if (useCommit && !SegmentHalfFits(context, slot, vertexBytes)) {
            StreamDiagEvent(context, 0x21UL, vertexBytes,
                            slot->segmentCursor);
            useCommit = FALSE;
        }
        slot->commitMode = useCommit;
        slot->modeDecided = TRUE;
        merge = FALSE;
        required = useCommit
            ? headerDwords
            : headerDwords + context->screenVertexCount * vertexDwords;
        generatedRequired = hardwareTransform
            ? GeneratedHardwareDrawDwords(
                  useCommit ? 0UL : context->screenVertexCount,
                  state->depth, state->textured[0], state->textured[1],
                  state->fog, texgen, state->normalVertex, state->enabledLights) +
                  (useCommit ? COMMIT_FETCH_DWORDS : 0UL)
            : GeneratedDrawDwords(context->screenVertexCount, state->depth,
                                  state->textured[0], state->textured[1],
                                  state->fog, state->perspective);
        /* header[1] was baked before the Submit; the degrade above changed
         * the record shape, so re-patch the length or the service walks the
         * inline vertex tail as records. */
        header[1] = required;
        growthRequired = required;
        generatedGrowth = generatedRequired;
    }
    if (merge) {
        record = slot->records + slot->lastDrawOffset;
        destination = record + record[1];
        record[1] += growthRequired;
        record[10] += context->screenVertexCount;
        if (useCommit)
            destination = (ULONG *)
                ((ULONG)context->segment.info.CpuAddress +
                 context->buildSlot * SEGMENT_HALF_BYTES +
                 slot->segmentCursor);
    } else {
        slot->lastDrawOffset = slot->recordCount;
        slot->lastDrawHeaderDwords = headerDwords;
        slot->lastDrawValid = TRUE;
        record = slot->records + slot->recordCount;
        for (index = 0; index < headerDwords; ++index)
            record[index] = header[index];
        destination = record + headerDwords;
        if (useCommit) {
            slot->vertexOffsets[slot->drawCount++] =
                context->buildSlot * SEGMENT_HALF_BYTES +
                slot->segmentCursor;
            destination = (ULONG *)
                ((ULONG)context->segment.info.CpuAddress +
                 context->buildSlot * SEGMENT_HALF_BYTES +
                 slot->segmentCursor);
        }
    }
serialize_vertices:
#if R200_PPC_FRONTEND && defined(__PPC__) && !defined(__m68k__)
    /* PPC mirrors of the m68k movem serializers: the commit-mode record
     * shapes are contiguous ScreenVertex prefixes (plain TCL relocates
     * the colour dword from lane 7), so the whole flush is one
     * byte-reversing block copy with disabled-attribute masks. Shapes
     * that do not match a kernel fall through to the generic loop. */
    if (hardwareTransform && useCommit) {
        const ULONG *serialSource = (const ULONG *)context->screenVertices;
        ULONG serialCount = context->screenVertexCount;
        BOOL vector = MGLPPCAltiVecVertexEnabled;

        if (state->normalVertex &&
            vertexDwords == R200_LIT_VERTEX_DWORDS) {
            context->perf.serializeKernelFlushes++;
            context->perf.serializeKernelVertices += serialCount;
            if (vector)
                MGLPPCSwapLit13Vector(destination, serialSource, serialCount,
                                      unit0Mask, unit1Mask, fogMask);
            else
                MGLPPCSwapLit13(destination, serialSource, serialCount,
                                unit0Mask, unit1Mask, fogMask);
        } else if (state->normalVertex &&
                   vertexDwords == R200_LIT_VERTEX_COMPACT_DWORDS) {
            context->perf.serializeKernelFlushes++;
            context->perf.serializeKernelVertices += serialCount;
            if (vector)
                MGLPPCSwapLit10Vector(destination, serialSource, serialCount,
                                      unit0Mask);
            else
                MGLPPCSwapLit10(destination, serialSource, serialCount,
                                unit0Mask);
        } else if (!state->normalVertex &&
                   vertexDwords == R200_TCL_VERTEX_DWORDS) {
            context->perf.serializeKernelFlushes++;
            context->perf.serializeKernelVertices += serialCount;
            if (vector)
                MGLPPCSwapTCL10Vector(destination, serialSource, serialCount,
                                      unit0Mask, unit1Mask, fogMask);
            else
                MGLPPCSwapTCL10(destination, serialSource, serialCount,
                                unit0Mask, unit1Mask, fogMask);
        } else if (!state->normalVertex &&
                   vertexDwords == R200_TCL_VERTEX_COMPACT_DWORDS) {
            context->perf.serializeKernelFlushes++;
            context->perf.serializeKernelVertices += serialCount;
            if (vector)
                MGLPPCSwapTCL7Vector(destination, serialSource, serialCount,
                                     unit0Mask);
            else
                MGLPPCSwapTCL7(destination, serialSource, serialCount,
                               unit0Mask);
        } else {
            goto serialize_generic;
        }
        goto serialize_done;
    }
#endif
serialize_generic:
    for (index = 0; index < context->screenVertexCount; ++index) {
        struct ScreenVertex *vertex = &context->screenVertices[index];
        if (hardwareTransform && !useCommit && state->normalVertex &&
            vertexDwords == R200_LIT_VERTEX_DWORDS &&
            sizeof(struct ScreenVertex) == 13UL * sizeof(ULONG)) {
            /* Interface-11 normal vertices are x,y,z,rhw,nx,ny,nz,colour,
             * s0,t0,s1,t1,fog -- exactly the reordered ScreenVertex
             * layout. FloatBits is bitwise, so serialization is a raw
             * longword block copy plus zero patches for attributes the
             * record omits. */
            const ULONG *source = (const ULONG *)vertex;
#if defined(__GNUC__) && defined(__m68k__)
            __asm__ __volatile__ (
                "movem.l (%0),%%d0-%%d7\n\t"
                "movem.l %%d0-%%d7,(%1)\n\t"
                "movem.l 32(%0),%%d0-%%d4\n\t"
                "movem.l %%d0-%%d4,32(%1)"
                :
                : "a" (source), "a" (destination)
                : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
                  "memory");
#else
            destination[0] = source[0];
            destination[1] = source[1];
            destination[2] = source[2];
            destination[3] = source[3];
            destination[4] = source[4];
            destination[5] = source[5];
            destination[6] = source[6];
            destination[7] = source[7];
            destination[8] = source[8];
            destination[9] = source[9];
            destination[10] = source[10];
            destination[11] = source[11];
            destination[12] = source[12];
#endif
            if (!state->textured[0]) {
                destination[8] = 0;
                destination[9] = 0;
            }
            if (!state->textured[1]) {
                destination[10] = 0;
                destination[11] = 0;
            }
            if (!state->fog)
                destination[12] = 0;
            destination += vertexDwords;
            continue;
        }
        if (hardwareTransform && !useCommit && state->normalVertex &&
            vertexDwords == R200_LIT_VERTEX_COMPACT_DWORDS &&
            sizeof(struct ScreenVertex) == 13UL * sizeof(ULONG)) {
            /* Compact interface-11 normal vertex: x,y,z,rhw,nx,ny,nz,
             * colour,s,t -- exactly the first ten longwords of the
             * reordered ScreenVertex; no unit-1 or fog tail exists. */
            const ULONG *source = (const ULONG *)vertex;

            destination[0] = source[0];
            destination[1] = source[1];
            destination[2] = source[2];
            destination[3] = source[3];
            destination[4] = source[4];
            destination[5] = source[5];
            destination[6] = source[6];
            destination[7] = source[7];
            destination[8] = source[8];
            destination[9] = source[9];
            if (!state->textured[0]) {
                destination[8] = 0;
                destination[9] = 0;
            }
            destination += vertexDwords;
            continue;
        }
#if defined(__GNUC__) && defined(__m68k__)
        if (hardwareTransform && useCommit && state->normalVertex &&
            vertexDwords == R200_LIT_VERTEX_DWORDS) {
            /* Streaming variant of the block-copy path above: the same
             * two movem groups in record order, but every dword is
             * byte-swapped in registers before its single VRAM store
             * because the VAP fetches segment vertices little-endian.
             * Colour swaps like any other dword -- ring-order dwords are
             * little-endian across the whole vertex. Disabled units and
             * fog are collapsed to zero with AND masks instead of
             * branches; swapping zero stays zero. */
            const ULONG *source = (const ULONG *)vertex;
            __asm__ __volatile__ (
                "movem.l (%[src]),%%d0-%%d7\n\t"
                "ror.w #8,%%d0\n\tswap %%d0\n\tror.w #8,%%d0\n\t"
                "ror.w #8,%%d1\n\tswap %%d1\n\tror.w #8,%%d1\n\t"
                "ror.w #8,%%d2\n\tswap %%d2\n\tror.w #8,%%d2\n\t"
                "ror.w #8,%%d3\n\tswap %%d3\n\tror.w #8,%%d3\n\t"
                "ror.w #8,%%d4\n\tswap %%d4\n\tror.w #8,%%d4\n\t"
                "ror.w #8,%%d5\n\tswap %%d5\n\tror.w #8,%%d5\n\t"
                "ror.w #8,%%d6\n\tswap %%d6\n\tror.w #8,%%d6\n\t"
                "ror.w #8,%%d7\n\tswap %%d7\n\tror.w #8,%%d7\n\t"
                "movem.l %%d0-%%d7,(%[dst])"
                :
                : [src] "a" (source), [dst] "a" (destination)
                : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
                  "memory");
            __asm__ __volatile__ (
                "movem.l 32(%[src]),%%d0-%%d4\n\t"
                "and.l %[m0],%%d0\n\tand.l %[m0],%%d1\n\t"
                "and.l %[m1],%%d2\n\tand.l %[m1],%%d3\n\t"
                "and.l %[mf],%%d4\n\t"
                "ror.w #8,%%d0\n\tswap %%d0\n\tror.w #8,%%d0\n\t"
                "ror.w #8,%%d1\n\tswap %%d1\n\tror.w #8,%%d1\n\t"
                "ror.w #8,%%d2\n\tswap %%d2\n\tror.w #8,%%d2\n\t"
                "ror.w #8,%%d3\n\tswap %%d3\n\tror.w #8,%%d3\n\t"
                "ror.w #8,%%d4\n\tswap %%d4\n\tror.w #8,%%d4\n\t"
                "movem.l %%d0-%%d4,32(%[dst])"
                :
                : [src] "a" (source), [dst] "a" (destination),
                  [m0] "d" (unit0Mask), [m1] "d" (unit1Mask),
                  [mf] "d" (fogMask)
                : "d0", "d1", "d2", "d3", "d4", "memory");
            destination += vertexDwords;
            continue;
        }
        if (hardwareTransform && useCommit && state->normalVertex &&
            vertexDwords == R200_LIT_VERTEX_COMPACT_DWORDS) {
            /* Compact lit stream vertex: x,y,z,rhw,nx,ny,nz,colour,s,t.
             * Burst one carries the first eight dwords verbatim-swapped;
             * burst two carries only the masked, swapped unit-0 pair. */
            const ULONG *source = (const ULONG *)vertex;
            __asm__ __volatile__ (
                "movem.l (%[src]),%%d0-%%d7\n\t"
                "ror.w #8,%%d0\n\tswap %%d0\n\tror.w #8,%%d0\n\t"
                "ror.w #8,%%d1\n\tswap %%d1\n\tror.w #8,%%d1\n\t"
                "ror.w #8,%%d2\n\tswap %%d2\n\tror.w #8,%%d2\n\t"
                "ror.w #8,%%d3\n\tswap %%d3\n\tror.w #8,%%d3\n\t"
                "ror.w #8,%%d4\n\tswap %%d4\n\tror.w #8,%%d4\n\t"
                "ror.w #8,%%d5\n\tswap %%d5\n\tror.w #8,%%d5\n\t"
                "ror.w #8,%%d6\n\tswap %%d6\n\tror.w #8,%%d6\n\t"
                "ror.w #8,%%d7\n\tswap %%d7\n\tror.w #8,%%d7\n\t"
                "movem.l %%d0-%%d7,(%[dst])"
                :
                : [src] "a" (source), [dst] "a" (destination)
                : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
                  "memory");
            __asm__ __volatile__ (
                "movem.l 32(%[src]),%%d0-%%d1\n\t"
                "and.l %[m0],%%d0\n\tand.l %[m0],%%d1\n\t"
                "ror.w #8,%%d0\n\tswap %%d0\n\tror.w #8,%%d0\n\t"
                "ror.w #8,%%d1\n\tswap %%d1\n\tror.w #8,%%d1\n\t"
                "movem.l %%d0-%%d1,32(%[dst])"
                :
                : [src] "a" (source), [dst] "a" (destination),
                  [m0] "d" (unit0Mask)
                : "d0", "d1", "memory");
            destination += vertexDwords;
            continue;
        }
        if (hardwareTransform && useCommit && !state->normalVertex &&
            vertexDwords == R200_TCL_VERTEX_DWORDS) {
            /* Plain TCL stream vertex, full stride x,y,z,w,colour,
             * s,t,s1,t1,fog. The colour dword moves from source lane 7
             * into record lane 4 inside the registers, so both bursts
             * stay contiguous movem transfers. */
            const ULONG *source = (const ULONG *)vertex;
            __asm__ __volatile__ (
                "movem.l (%[src]),%%d0-%%d3\n\t"
                "move.l 28(%[src]),%%d4\n\t"
                "ror.w #8,%%d0\n\tswap %%d0\n\tror.w #8,%%d0\n\t"
                "ror.w #8,%%d1\n\tswap %%d1\n\tror.w #8,%%d1\n\t"
                "ror.w #8,%%d2\n\tswap %%d2\n\tror.w #8,%%d2\n\t"
                "ror.w #8,%%d3\n\tswap %%d3\n\tror.w #8,%%d3\n\t"
                "ror.w #8,%%d4\n\tswap %%d4\n\tror.w #8,%%d4\n\t"
                "movem.l %%d0-%%d4,(%[dst])\n\t"
                "movem.l 32(%[src]),%%d0-%%d4\n\t"
                "and.l %[m0],%%d0\n\tand.l %[m0],%%d1\n\t"
                "and.l %[m1],%%d2\n\tand.l %[m1],%%d3\n\t"
                "and.l %[mf],%%d4\n\t"
                "ror.w #8,%%d0\n\tswap %%d0\n\tror.w #8,%%d0\n\t"
                "ror.w #8,%%d1\n\tswap %%d1\n\tror.w #8,%%d1\n\t"
                "ror.w #8,%%d2\n\tswap %%d2\n\tror.w #8,%%d2\n\t"
                "ror.w #8,%%d3\n\tswap %%d3\n\tror.w #8,%%d3\n\t"
                "ror.w #8,%%d4\n\tswap %%d4\n\tror.w #8,%%d4\n\t"
                "movem.l %%d0-%%d4,20(%[dst])"
                :
                : [src] "a" (source), [dst] "a" (destination),
                  [m0] "d" (unit0Mask), [m1] "d" (unit1Mask),
                  [mf] "d" (fogMask)
                : "d0", "d1", "d2", "d3", "d4", "memory");
            destination += vertexDwords;
            continue;
        }
        if (hardwareTransform && useCommit && !state->normalVertex &&
            vertexDwords == R200_TCL_VERTEX_COMPACT_DWORDS) {
            /* Compact plain TCL stream vertex: x,y,z,w,colour,s,t. */
            const ULONG *source = (const ULONG *)vertex;
            __asm__ __volatile__ (
                "movem.l (%[src]),%%d0-%%d3\n\t"
                "move.l 28(%[src]),%%d4\n\t"
                "ror.w #8,%%d0\n\tswap %%d0\n\tror.w #8,%%d0\n\t"
                "ror.w #8,%%d1\n\tswap %%d1\n\tror.w #8,%%d1\n\t"
                "ror.w #8,%%d2\n\tswap %%d2\n\tror.w #8,%%d2\n\t"
                "ror.w #8,%%d3\n\tswap %%d3\n\tror.w #8,%%d3\n\t"
                "ror.w #8,%%d4\n\tswap %%d4\n\tror.w #8,%%d4\n\t"
                "movem.l %%d0-%%d4,(%[dst])\n\t"
                "movem.l 32(%[src]),%%d0-%%d1\n\t"
                "and.l %[m0],%%d0\n\tand.l %[m0],%%d1\n\t"
                "ror.w #8,%%d0\n\tswap %%d0\n\tror.w #8,%%d0\n\t"
                "ror.w #8,%%d1\n\tswap %%d1\n\tror.w #8,%%d1\n\t"
                "movem.l %%d0-%%d1,20(%[dst])"
                :
                : [src] "a" (source), [dst] "a" (destination),
                  [m0] "d" (unit0Mask)
                : "d0", "d1", "d2", "d3", "d4", "memory");
            destination += vertexDwords;
            continue;
        }
#endif
        if (hardwareTransform) {
            /* Streaming records are fetched little-endian by the VAP; the
             * swap happens in registers before the single VRAM store so
             * the segment is written exactly once. */
            #define STORE_DWORD(i, expr) \
                (destination[i] = (useCommit ? SwapSegmentWord((ULONG)(expr)) \
                                             : (ULONG)(expr)))
            STORE_DWORD(0, FloatBits(vertex->x));
            STORE_DWORD(1, FloatBits(vertex->y));
            STORE_DWORD(2, FloatBits(vertex->z));
            STORE_DWORD(3, FloatBits(vertex->rhw));
            if (state->normalVertex) {
                /* Interface-11 normal vertices insert the object-space
                 * XYZ triple after W; the packed colour shifts to dword
                 * 7. */
                STORE_DWORD(4, FloatBits(vertex->nx));
                STORE_DWORD(5, FloatBits(vertex->ny));
                STORE_DWORD(6, FloatBits(vertex->nz));
                STORE_DWORD(7, vertex->color);
                STORE_DWORD(8, state->textured[0]
                                   ? FloatBits(vertex->s) : 0);
                STORE_DWORD(9, state->textured[0]
                                   ? FloatBits(vertex->t) : 0);
                if (vertexDwords > 10UL) {
                    STORE_DWORD(10, state->textured[1]
                                        ? FloatBits(vertex->s1) : 0);
                    STORE_DWORD(11, state->textured[1]
                                        ? FloatBits(vertex->t1) : 0);
                    STORE_DWORD(12, state->fog ? FloatBits(vertex->fog)
                                               : 0);
                }
            } else {
            if (vertexDwords == R200_TCL_VERTEX_COMPACT_DWORDS) {
                /* Compact plain TCL vertex: x,y,z,w,colour,s,t. */
                STORE_DWORD(4, vertex->color);
                STORE_DWORD(5, state->textured[0]
                                   ? FloatBits(vertex->s) : 0);
                STORE_DWORD(6, state->textured[0]
                                   ? FloatBits(vertex->t) : 0);
            } else {
            STORE_DWORD(4, vertex->color);
            STORE_DWORD(5, state->textured[0] ? FloatBits(vertex->s) : 0);
            STORE_DWORD(6, state->textured[0] ? FloatBits(vertex->t) : 0);
            STORE_DWORD(7, state->textured[1] ? FloatBits(vertex->s1) : 0);
            STORE_DWORD(8, state->textured[1] ? FloatBits(vertex->t1) : 0);
            STORE_DWORD(9, state->fog ? FloatBits(vertex->fog) : 0);
            }
            }
            #undef STORE_DWORD
            destination += vertexDwords;
            continue;
        }
        GLfloat clipW=state->perspective ? 1.0f/vertex->rhw : 1.0f;
        destination[0] = FloatBits(state->perspective ?
            (vertex->x*(2.0f/(GLfloat)state->drawableWidth)-1.0f)*clipW : vertex->x);
        destination[1] = FloatBits(state->perspective ?
            (1.0f-vertex->y*(2.0f/(GLfloat)state->drawableHeight))*clipW : vertex->y);
        destination[2] = FloatBits(state->perspective ?
            (vertex->z*2.0f-1.0f)*clipW : vertex->z);
        destination[3] = state->textured[0] ? FloatBits(vertex->s) : 0;
        destination[4] = state->textured[0] ? FloatBits(vertex->t) : 0;
        destination[5] = vertex->color;
        if (extendedVertex) {
            destination[6] = state->textured[1] ? FloatBits(vertex->s1) : 0;
            destination[7] = state->textured[1] ? FloatBits(vertex->t1) : 0;
            destination[8] = state->fog ? FloatBits(vertex->fog) :
                               state->perspective
                                     ? FloatBits(clipW) : 0;
        }
        destination += vertexDwords;
    }
serialize_done:
    if (useCommit) {
        /* The segment lives in VRAM behind a posted-write bridge. The ring
         * submit only drains the MMIO path; without a readback from the
         * aperture itself the GPU can fetch stale (zero) tail vertices,
         * which rendered as truncated shapes and missing draws. */
        volatile ULONG *segmentTail =
            (volatile ULONG *)
                ((ULONG)context->segment.info.CpuAddress +
                 context->buildSlot * SEGMENT_HALF_BYTES +
                 slot->segmentCursor + vertexBytes - sizeof(ULONG));
        (void)*segmentTail;
        slot->segmentCursor += vertexBytes;
    }
    slot->recordCount += growthRequired;
    slot->generatedDwordCount += generatedGrowth;
    if (state->textured[0])
        MarkTextureQueued(context, state->textureName[0]);
    if (state->textured[1])
        MarkTextureQueued(context, state->textureName[1]);
    if (context->perfEnabled) {
        context->perf.submittedVertices += context->screenVertexCount;
        if (!merge)
            context->perf.drawRecords++;
        else
            context->perf.drawMerges++;
    }
    context->screenVertexCount = 0;
    context->screenCommand = RADEON3D_EXEC_DRAW_TRIANGLES;
    context->pendingDrawValid = FALSE;
    if (context->perfEnabled) {
        ULONG elapsed = PerfClock() - serializeStart;
        ULONG nested = (context->perf.executeTicks - executeBefore) +
                       (context->perf.waitTicks - waitBefore);
        context->perf.serializeTicks += elapsed >= nested ? elapsed - nested : 0;
#if R200_PPC_FRONTEND
        context->perf.serializeStallTicks +=
            R200PPCPlatformStallTicks() - stallBefore;
#endif
    }
    return TRUE;
}

static void GenerateSphereMap(struct MiniGLR200Context *context,
                               const struct Vertex *vertex,
                               struct ClipVertex *output,
                               ULONG unitMask)
{
    const GLfloat *m = context->modelview[context->modelviewDepth];
    const GLfloat *n = context->normalMatrix;
    GLfloat ux = m[0]*vertex->x + m[4]*vertex->y +
                 m[8]*vertex->z + m[12]*vertex->w;
    GLfloat uy = m[1]*vertex->x + m[5]*vertex->y +
                 m[9]*vertex->z + m[13]*vertex->w;
    GLfloat uz = m[2]*vertex->x + m[6]*vertex->y +
                 m[10]*vertex->z + m[14]*vertex->w;
    GLfloat uw = 1.0f;
    GLfloat lengthSquared, inverse, nx, ny, nz, twiceDot, rx, ry, rz;
    ULONG unit;
    ULONG sampleStart = 0;
    BOOL sampled = FALSE;
#if R200_SPHEREMAP_CACHE
    struct SphereMapCacheEntry *cached;
    ULONG key[11], index, cacheIndex;
    BOOL cacheHit = FALSE;
#endif

    /* One sample per 64 calls keeps timer overhead below the noise floor while
     * providing a representative cost for this per-vertex hot path. */
    if (context->perfEnabled) {
        context->perf.sphereMapCalls++;
        if ((context->perf.sphereMapCalls & 63UL) == 0) {
            sampled = TRUE;
            context->perf.sphereMapSamples++;
            sampleStart = PerfClock();
        }
    }
#if R200_SPHEREMAP_CACHE
    key[0] = FloatBits(vertex->x);  key[1] = FloatBits(vertex->y);
    key[2] = FloatBits(vertex->z);  key[3] = FloatBits(vertex->w);
    key[4] = FloatBits(vertex->nx); key[5] = FloatBits(vertex->ny);
    key[6] = FloatBits(vertex->nz); key[7] = FloatBits(vertex->s);
    key[8] = FloatBits(vertex->t);  key[9] = FloatBits(vertex->s1);
    key[10] = FloatBits(vertex->t1);
    cacheIndex = key[0] ^ key[3] ^ key[4] ^ key[6] ^ key[7] ^ key[9];
    cached = &context->sphereMapCache[cacheIndex &
                                      (SPHEREMAP_CACHE_SIZE - 1UL)];
    if (cached->valid &&
        cached->generation == context->sphereMapGeneration) {
        for (index = 0; index < 11; ++index)
            if (cached->key[index] != key[index])
                break;
        if (index == 11) {
            output->s = cached->s; output->t = cached->t;
            output->s1 = cached->s1; output->t1 = cached->t1;
            cacheHit = TRUE;
            if (context->perfEnabled)
                context->perf.sphereMapCacheHits++;
            goto done;
        }
    }
    if (context->perfEnabled)
        context->perf.sphereMapCacheMisses++;
#endif

    /* The normal model-view path is affine (translate/rotate/scale), so its
     * homogeneous row is exactly (0, 0, 0, 1). Avoid its four FPU products
     * for every sphere-mapped vertex, but retain full projective-model-view
     * behaviour for applications that supply such a matrix explicitly. */
    if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f || m[15] != 1.0f)
        uw = m[3]*vertex->x + m[7]*vertex->y +
             m[11]*vertex->z + m[15]*vertex->w;
    if (uw != 0.0f && uw != 1.0f) {
        inverse = 1.0f / uw;
        ux *= inverse; uy *= inverse; uz *= inverse;
    }
    lengthSquared = ux*ux + uy*uy + uz*uz;
    if (lengthSquared <= 0.0f)
        goto done;
    inverse = ReciprocalSqrt(lengthSquared);
    ux *= inverse; uy *= inverse; uz *= inverse;

#if R200_NORMAL_TRANSFORM_CACHE
    if (context->sphereNormalValid &&
        context->sphereObjectNX == vertex->nx &&
        context->sphereObjectNY == vertex->ny &&
        context->sphereObjectNZ == vertex->nz) {
        nx = context->sphereEyeNX;
        ny = context->sphereEyeNY;
        nz = context->sphereEyeNZ;
        if (context->perfEnabled)
            context->perf.normalCacheHits++;
    } else {
#endif
        nx = n[0]*vertex->nx + n[1]*vertex->ny + n[2]*vertex->nz;
        ny = n[3]*vertex->nx + n[4]*vertex->ny + n[5]*vertex->nz;
        nz = n[6]*vertex->nx + n[7]*vertex->ny + n[8]*vertex->nz;
#if R200_NORMAL_TRANSFORM_CACHE
        context->sphereObjectNX = vertex->nx;
        context->sphereObjectNY = vertex->ny;
        context->sphereObjectNZ = vertex->nz;
        context->sphereEyeNX = nx;
        context->sphereEyeNY = ny;
        context->sphereEyeNZ = nz;
        context->sphereNormalValid = TRUE;
        if (context->perfEnabled)
            context->perf.normalCacheMisses++;
    }
#endif
    twiceDot = 2.0f * (nx*ux + ny*uy + nz*uz);
    rx = ux - nx*twiceDot;
    ry = uy - ny*twiceDot;
    rz = uz - nz*twiceDot + 1.0f;
    lengthSquared = rx*rx + ry*ry + rz*rz;
    if (lengthSquared <= 0.0f)
        goto done;
    inverse = 0.5f * ReciprocalSqrt(lengthSquared);
    for (unit = 0; unit < 2; ++unit) {
        if (!(unitMask & (1UL << unit)))
            continue;
        if (context->textureGenS[unit]) {
            if (unit == 0)
                output->s = rx*inverse + 0.5f;
            else
                output->s1 = rx*inverse + 0.5f;
        }
        if (context->textureGenT[unit]) {
            if (unit == 0)
                output->t = ry*inverse + 0.5f;
            else
                output->t1 = ry*inverse + 0.5f;
        }
    }
done:
#if R200_SPHEREMAP_CACHE
    if (!cacheHit) {
        cached->generation = context->sphereMapGeneration;
        cached->valid = TRUE;
        for (index = 0; index < 11; ++index)
            cached->key[index] = key[index];
        cached->s = output->s; cached->t = output->t;
        cached->s1 = output->s1; cached->t1 = output->t1;
    }
#endif
    if (sampled)
        context->perf.sphereMapTicks += PerfClock() - sampleStart;
}

static ULONG SoftwareSphereMapMask(const struct MiniGLR200Context *context,
                                   ULONG hardwareMask)
{
    ULONG mask = 0, unit;

    for (unit = 0; unit < 2UL; ++unit)
        if ((context->textureGenS[unit] || context->textureGenT[unit]) &&
            !(hardwareMask & (1UL << unit)))
            mask |= 1UL << unit;
    return mask;
}

static BOOL AppendScreenTriangle(struct MiniGLR200Context *context,
                                 const struct ScreenVertex *a,
                                 const struct ScreenVertex *b,
                                 const struct ScreenVertex *c)
{
    if (context->screenVertexCount+3>SCREEN_VERTEX_CAPACITY &&
        !FlushScreenVertices(context)) return FALSE;
    if (!PrepareScreenAppend(context, RADEON3D_EXEC_DRAW_TRIANGLES))
        return FALSE;
    context->screenVertices[context->screenVertexCount++]=*a;
    context->screenVertices[context->screenVertexCount++]=*b;
    context->screenVertices[context->screenVertexCount++]=*c;
    return TRUE;
}

/*
 * Facing test in window space. Radeon3D screen coordinates flip Y relative
 * to OpenGL window space, so the signed area computed here is the negative
 * of the OpenGL window-space area: a front-facing GL_CCW triangle has a
 * negative screen-space area.
 */
static BOOL CullScreenTriangle(const struct MiniGLR200Context *context,
                               const struct ScreenVertex *a,
                               const struct ScreenVertex *b,
                               const struct ScreenVertex *c)
{
    GLfloat area;
    BOOL front;

    area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (context->minTriArea > 0.0f) {
        GLfloat magnitude = area < 0.0f ? -area : area;
        if (magnitude < context->minTriArea)
            return TRUE;
    }
    if (!context->cullFaceEnabled)
        return FALSE;
    if (context->cullFaceMode == GL_FRONT_AND_BACK)
        return TRUE;
    if (area == 0.0f)
        return TRUE;
    front = context->frontFace == GL_CCW ? (area < 0.0f) : (area > 0.0f);
    return context->cullFaceMode == GL_FRONT ? front : !front;
}

/* Defined below. */
static BOOL AppendScreenTriangleEdges(struct MiniGLR200Context *context,
                                      const struct ScreenVertex *a,
                                      const struct ScreenVertex *b,
                                      const struct ScreenVertex *c);
static BOOL AppendScreenTriangleVertices(struct MiniGLR200Context *context,
                                         const struct ScreenVertex *a,
                                         const struct ScreenVertex *b,
                                         const struct ScreenVertex *c);

/*
 * Polygon-facing triangle emission: culls, then applies flat shading using
 * the provoking vertex colour the caller recorded in context->flatColor.
 * Points and lines bypass this and go straight to AppendScreenTriangle().
 */
static BOOL AppendScreenPolygonTriangle(struct MiniGLR200Context *context,
                                        const struct ScreenVertex *a,
                                        const struct ScreenVertex *b,
                                        const struct ScreenVertex *c)
{
    struct ScreenVertex flatA, flatB, flatC;

    if (CullScreenTriangle(context, a, b, c))
        return TRUE;
    /*
     * Front/back polygon mode isn't tracked separately (glPolygonMode's
     * `face` is accepted but the more recent call always wins for both
     * faces) -- a documented simplification, not a per-face GL_FRONT vs.
     * GL_BACK distinction.
     */
    if (context->polygonMode == GL_LINE)
        return AppendScreenTriangleEdges(context, a, b, c);
    if (context->polygonMode == GL_POINT)
        return AppendScreenTriangleVertices(context, a, b, c);
    if (context->shadeModel != GL_FLAT)
        return AppendScreenTriangle(context, a, b, c);
    flatA = *a; flatB = *b; flatC = *c;
    flatA.color = flatB.color = flatC.color = context->flatColor;
    return AppendScreenTriangle(context, &flatA, &flatB, &flatC);
}

static GLfloat ScreenPlaneDistance(const struct MiniGLR200Context *context,
                                   const struct ScreenVertex *vertex,
                                   ULONG plane)
{
    switch (plane) {
    case 0: return vertex->x;
    case 1: return (GLfloat)context->width - vertex->x;
    case 2: return vertex->y;
    default: return (GLfloat)context->height - vertex->y;
    }
}

static struct ScreenVertex InterpolateScreenVertex(
    const struct ScreenVertex *a, const struct ScreenVertex *b,
    GLfloat amount, BOOL perspective)
{
    struct ScreenVertex output;
    ULONG shift;
#define LERP_SCREEN(field) \
    output.field = a->field + (b->field - a->field) * amount
    LERP_SCREEN(x); LERP_SCREEN(y); LERP_SCREEN(z);
    LERP_SCREEN(rhw);
    if (perspective && output.rhw > 0.0f) {
        GLfloat inverse=1.0f/output.rhw;
        output.s=(a->s*a->rhw+(b->s*b->rhw-a->s*a->rhw)*amount)*inverse;
        output.t=(a->t*a->rhw+(b->t*b->rhw-a->t*a->rhw)*amount)*inverse;
        output.s1=(a->s1*a->rhw+(b->s1*b->rhw-a->s1*a->rhw)*amount)*inverse;
        output.t1=(a->t1*a->rhw+(b->t1*b->rhw-a->t1*a->rhw)*amount)*inverse;
    } else {
        LERP_SCREEN(s); LERP_SCREEN(t);
        LERP_SCREEN(s1); LERP_SCREEN(t1);
    }
    LERP_SCREEN(fog);
#undef LERP_SCREEN
    output.color = 0;
    for (shift = 0; shift < 32; shift += 8) {
        GLfloat first = (GLfloat)((a->color >> shift) & 255UL);
        GLfloat second = (GLfloat)((b->color >> shift) & 255UL);
        ULONG channel = (ULONG)(first + (second - first) * amount + 0.5f);
        if (channel > 255UL) channel = 255UL;
        output.color |= channel << shift;
    }
    return output;
}

static ULONG ClipScreenPolygon(const struct MiniGLR200Context *context,
                               struct ScreenVertex *vertices, ULONG count,
                               BOOL perspective)
{
    struct ScreenVertex temporary[12];
    ULONG plane, vertexIndex;
    for (plane = 0; plane < 4 && count; ++plane) {
        ULONG outputCount = 0, index;
        struct ScreenVertex previous = vertices[count - 1];
        GLfloat previousDistance =
            ScreenPlaneDistance(context, &previous, plane);
        for (index = 0; index < count; ++index) {
            struct ScreenVertex current = vertices[index];
            GLfloat currentDistance =
                ScreenPlaneDistance(context, &current, plane);
            BOOL previousInside = previousDistance >= 0.0f;
            BOOL currentInside = currentDistance >= 0.0f;
            if (previousInside != currentInside) {
                GLfloat amount = previousDistance /
                                 (previousDistance - currentDistance);
                temporary[outputCount++] =
                    InterpolateScreenVertex(&previous, &current,
                                            amount, perspective);
                /* Intersections lie exactly on the clipping plane. PPC
                 * fmadds can otherwise produce -1.430511e-5 for the console
                 * top edge: 510 + (-600 * 0.85f). */
                switch (plane) {
                case 0: temporary[outputCount - 1].x = 0.0f; break;
                case 1: temporary[outputCount - 1].x = (GLfloat)context->width; break;
                case 2: temporary[outputCount - 1].y = 0.0f; break;
                default: temporary[outputCount - 1].y = (GLfloat)context->height; break;
                }
            }
            if (currentInside)
                temporary[outputCount++] = current;
            previous = current;
            previousDistance = currentDistance;
        }
        count = outputCount;
        for (index = 0; index < count; ++index)
            vertices[index] = temporary[index];
    }
    /* A later plane's interpolation can drift off an earlier plane by an
     * ULP. Only positions are bounded; interpolated attributes are unchanged.
     * NaNs remain NaNs and are still rejected by the service. */
    for (vertexIndex = 0; vertexIndex < count; ++vertexIndex) {
        if (vertices[vertexIndex].x < 0.0f) vertices[vertexIndex].x = 0.0f;
        if (vertices[vertexIndex].x > (GLfloat)context->width)
            vertices[vertexIndex].x = (GLfloat)context->width;
        if (vertices[vertexIndex].y < 0.0f) vertices[vertexIndex].y = 0.0f;
        if (vertices[vertexIndex].y > (GLfloat)context->height)
            vertices[vertexIndex].y = (GLfloat)context->height;
    }
    return count;
}

static BOOL EmitClippedScreenTriangle(struct MiniGLR200Context *context,
                                      const struct ScreenVertex *a,
                                      const struct ScreenVertex *b,
                                      const struct ScreenVertex *c,
                                      BOOL polygonRules)
{
    struct ScreenVertex vertices[12];
    ULONG count,triangle;
    if (a->x>=0.0f && a->x<=(GLfloat)context->width &&
        a->y>=0.0f && a->y<=(GLfloat)context->height &&
        b->x>=0.0f && b->x<=(GLfloat)context->width &&
        b->y>=0.0f && b->y<=(GLfloat)context->height &&
        c->x>=0.0f && c->x<=(GLfloat)context->width &&
        c->y>=0.0f && c->y<=(GLfloat)context->height)
        return polygonRules ? AppendScreenPolygonTriangle(context,a,b,c) :
                              AppendScreenTriangle(context,a,b,c);
    vertices[0]=*a; vertices[1]=*b; vertices[2]=*c;
    count=ClipScreenPolygon(context,vertices,3,context->screenProjective);
    for (triangle=1;triangle+1<count;++triangle) {
        BOOL ok=polygonRules ?
            AppendScreenPolygonTriangle(context,&vertices[0],
                                         &vertices[triangle],
                                         &vertices[triangle+1]) :
            AppendScreenTriangle(context,&vertices[0],&vertices[triangle],
                                 &vertices[triangle+1]);
        if (!ok) return FALSE;
    }
    return TRUE;
}

static void ScreenVertexFromFlat(const struct Vertex *input,
                                 struct ScreenVertex *output)
{
    output->x = input->x;
    output->y = input->y;
    output->z = input->z;
    if (output->z < 0.0f) output->z = 0.0f;
    if (output->z > 1.0f) output->z = 1.0f;
    output->color = input->color;
    output->s = input->s; output->t = input->t;
    output->s1 = input->s1; output->t1 = input->t1;
    output->fog = input->fog;
    output->rhw = 1.0f;
}

static BOOL EmitFlatTriangle(struct MiniGLR200Context *context,
                             const struct Vertex *a,
                             const struct Vertex *b,
                             const struct Vertex *c)
{
    struct ScreenVertex polygon[12];
    ULONG count, triangle;
#if R200_DUMP_FIRST_BATCHES
    /* Temporary diagnostics: first flat triangles, coords and area. */
    {
        static LONG flatDumps = 8;
        if (flatDumps > 0) {
            extern struct DosLibrary *DOSBase;
            BPTR file;
            char path[32];
            ULONG data[10];
            static const char digits[] = "0123456789";
            int pos = 0, digitPos = 0;
            LONG index = (LONG)(8 - flatDumps);
            ULONG value;
            char tail[8];
            --flatDumps;
            if (DOSBase) {
                value = (ULONG)index;
                if (!value)
                    tail[digitPos++] = '0';
                while (value) {
                    tail[digitPos++] = digits[value % 10UL];
                    value /= 10UL;
                }
                path[pos++] = 'T'; path[pos++] = ':'; path[pos++] = 'r';
                path[pos++] = '2'; path[pos++] = '0'; path[pos++] = '0';
                path[pos++] = '_'; path[pos++] = 'f'; path[pos++] = 't';
                while (digitPos) path[pos++] = tail[--digitPos];
                path[pos++] = '.'; path[pos++] = 'b'; path[pos++] = 'i';
                path[pos++] = 'n'; path[pos] = 0;
                data[0] = context->vertexCount;
                data[1] = FloatBits(a->x);
                data[2] = FloatBits(a->y);
                data[3] = FloatBits(b->x);
                data[4] = FloatBits(b->y);
                data[5] = FloatBits(c->x);
                data[6] = FloatBits(c->y);
                data[7] = context->width;
                data[8] = context->height;
                data[9] = context->screenVertexCount;
                file = Open((STRPTR)path, MODE_NEWFILE);
                if (file) {
                    (void)Write(file, data, sizeof(data));
                    Close(file);
                }
            }
        }
    }
#endif
    ScreenVertexFromFlat(a, &polygon[0]);
    ScreenVertexFromFlat(b, &polygon[1]);
    ScreenVertexFromFlat(c, &polygon[2]);
    /* Flat vertices are already in drawable screen space. Most fast-path
     * clients submit wholly visible geometry, so avoid running every triangle
     * through the four-plane polygon clipper. Degenerate connector triangles
     * used to join strips can be discarded before serialization as well. */
    {
        GLfloat area =
            (polygon[1].x - polygon[0].x) *
                (polygon[2].y - polygon[0].y) -
            (polygon[2].x - polygon[0].x) *
                (polygon[1].y - polygon[0].y);
        if (area == 0.0f)
        {
            if (context->perfEnabled)
                context->perf.dropFlatDegenerate++;
            return TRUE;
        }
        if (polygon[0].x >= 0.0f &&
            polygon[0].x <= (GLfloat)context->width &&
            polygon[0].y >= 0.0f &&
            polygon[0].y <= (GLfloat)context->height &&
            polygon[1].x >= 0.0f &&
            polygon[1].x <= (GLfloat)context->width &&
            polygon[1].y >= 0.0f &&
            polygon[1].y <= (GLfloat)context->height &&
            polygon[2].x >= 0.0f &&
            polygon[2].x <= (GLfloat)context->width &&
            polygon[2].y >= 0.0f &&
            polygon[2].y <= (GLfloat)context->height)
            return AppendScreenPolygonTriangle(context, &polygon[0],
                                                &polygon[1], &polygon[2]);
    }
    /* MGL_FLAT* is already projected screen-space geometry. Its texture
     * coordinates must remain affine even when a preceding 3D draw used RHW. */
    count = ClipScreenPolygon(context, polygon, 3, FALSE);
    for (triangle = 1; triangle + 1 < count; ++triangle)
        if (!AppendScreenPolygonTriangle(context, &polygon[0],
                                         &polygon[triangle],
                                         &polygon[triangle + 1]))
            return FALSE;
    return TRUE;
}

static BOOL EmitFlatPrimitive(struct MiniGLR200Context *context,
                              const struct Vertex *vertices, ULONG count,
                              GLenum primitive)
{
    ULONG index;
    if (count < 3)
        return TRUE;
    if (primitive == MGL_FLATFAN) {
        for (index = 1; index + 1 < count; ++index) {
            context->flatColor = vertices[index + 1].color;
            if (!EmitFlatTriangle(context, &vertices[0], &vertices[index],
                                  &vertices[index + 1]))
                return FALSE;
        }
    } else {
        for (index = 0; index + 2 < count; ++index) {
            const struct Vertex *a = &vertices[index];
            const struct Vertex *b = &vertices[index + 1];
            context->flatColor = vertices[index + 2].color;
            if (index & 1UL) {
                const struct Vertex *swap = a;
                a = b;
                b = swap;
            }
            if (!EmitFlatTriangle(context, a, b, &vertices[index + 2]))
                return FALSE;
        }
    }
    return TRUE;
}


/*
 * GL_LINE/GL_POINT polygon-mode decomposition. These work from
 * already-clipped, already screen-space corners (called post-cull from
 * AppendScreenPolygonTriangle), so there is no transform or clip-plane step
 * here -- just the fixed-width screen-space quad construction reused for
 * exact visual consistency with genuine GL_POINTS/GL_LINES primitives.
 */
static BOOL AppendScreenSegment(struct MiniGLR200Context *context,
                                const struct ScreenVertex *p0,
                                const struct ScreenVertex *p1)
{
    struct ScreenVertex a=*p0,b=*p0,c=*p1,d=*p1;
    GLfloat dx=p1->x-p0->x,dy=p1->y-p0->y;
    GLfloat length=(GLfloat)sqrt((double)(dx*dx+dy*dy));
    GLfloat px,py;
    if (length==0.0f)
        return TRUE;
    px=-dy*0.5f/length; py=dx*0.5f/length;
    a.x+=px; a.y+=py; b.x-=px; b.y-=py;
    c.x-=px; c.y-=py; d.x+=px; d.y+=py;
    return EmitClippedScreenTriangle(context,&a,&b,&c,FALSE) &&
           EmitClippedScreenTriangle(context,&a,&c,&d,FALSE);
}

static BOOL AppendScreenTriangleEdges(struct MiniGLR200Context *context,
                                      const struct ScreenVertex *a,
                                      const struct ScreenVertex *b,
                                      const struct ScreenVertex *c)
{
    return AppendScreenSegment(context,a,b) &&
           AppendScreenSegment(context,b,c) &&
           AppendScreenSegment(context,c,a);
}

/* Whole quads fit one QUADS record: four vertices per sized point instead
 * of the six of the two-triangle decomposition. Edge points keep the
 * clipped triangle path because non-TCL records must stay inside the
 * render target. */
static BOOL AppendScreenQuad(struct MiniGLR200Context *context,
                             const struct ScreenVertex *a,
                             const struct ScreenVertex *b,
                             const struct ScreenVertex *c,
                             const struct ScreenVertex *d)
{
    if (context->screenVertexCount+4>SCREEN_VERTEX_CAPACITY &&
        !FlushScreenVertices(context)) return FALSE;
    if (!PrepareScreenAppend(context, RADEON3D_EXEC_DRAW_QUADS))
        return FALSE;
    context->screenVertices[context->screenVertexCount++]=*a;
    context->screenVertices[context->screenVertexCount++]=*b;
    context->screenVertices[context->screenVertexCount++]=*c;
    context->screenVertices[context->screenVertexCount++]=*d;
    return TRUE;
}

static BOOL AppendScreenPoint(struct MiniGLR200Context *context,
                              const struct ScreenVertex *p)
{
    struct ScreenVertex a=*p,b=*p,c=*p,d=*p;
    GLfloat half=context->pointSize*0.5f;
    a.x-=half; a.y-=half; b.x+=half; b.y-=half;
    c.x+=half; c.y+=half; d.x-=half; d.y+=half;
    if (a.x>=0.0f && a.x<=(GLfloat)context->width &&
        b.x>=0.0f && b.x<=(GLfloat)context->width &&
        c.x>=0.0f && c.x<=(GLfloat)context->width &&
        d.x>=0.0f && d.x<=(GLfloat)context->width &&
        a.y>=0.0f && a.y<=(GLfloat)context->height &&
        b.y>=0.0f && b.y<=(GLfloat)context->height &&
        c.y>=0.0f && c.y<=(GLfloat)context->height &&
        d.y>=0.0f && d.y<=(GLfloat)context->height)
        return AppendScreenQuad(context,&a,&b,&c,&d);
    return EmitClippedScreenTriangle(context,&a,&b,&c,FALSE) &&
           EmitClippedScreenTriangle(context,&a,&c,&d,FALSE);
}

static BOOL AppendScreenTriangleVertices(struct MiniGLR200Context *context,
                                         const struct ScreenVertex *a,
                                         const struct ScreenVertex *b,
                                         const struct ScreenVertex *c)
{
    return AppendScreenPoint(context,a) &&
           AppendScreenPoint(context,b) &&
           AppendScreenPoint(context,c);
}

/*
 * Points wider than one pixel.
 *
 * The hardware ignores the size we program for point primitives under
 * hardware TCL - measured on RV280, every RADEON3D_EXEC_DRAW_POINTS record
 * rasterises exactly one pixel whatever RE_POINTSIZE, the point-sprite vport
 * scale, or SE_TCL_POINT_SPRITE_CNTL contain.
 * Size the points here instead: transform each vertex on the CPU exactly the
 * way the record header tells the hardware to, then expand it into a
 * screen-space quad through the same path polygon-mode GL_POINT decomposition
 * already uses.
 *
 * Default-sized points keep the cheap hardware path, so this costs nothing
 * unless a client actually asks for large points.
 */
static BOOL EmitSizedPoints(struct MiniGLR200Context *context,
                            const struct Vertex *vertices, ULONG count)
{
    const GLfloat *matrix;
    GLfloat xScale, xOffset, yScale, yOffset, zScale, zOffset;
    ULONG index;

    if (!context->combinedMatrixValid) {
        MultiplyMatrix(context->combinedMatrix,
                       context->projection[context->projectionDepth],
                       context->modelview[context->modelviewDepth]);
        context->combinedMatrixValid = TRUE;
    }
    matrix = context->combinedMatrix;

    /* Same viewport and depth-range mapping CapturePendingDrawState writes
     * into header dwords 37..42. */
    xScale = (GLfloat)context->viewportWidth * 0.5f;
    xOffset = (GLfloat)context->viewportX + xScale;
    yScale = -((GLfloat)context->viewportHeight * 0.5f);
    yOffset = (GLfloat)(context->height - context->viewportY -
                        context->viewportHeight) +
              (GLfloat)context->viewportHeight * 0.5f;
    zScale = (context->depthFar - context->depthNear) * 0.5f;
    zOffset = (context->depthFar + context->depthNear) * 0.5f;

    /* Screen-space vertices are pre-projected. Any pending hardware-transformed
     * geometry is split off by PrepareScreenAppend, which compares
     * pendingHardwareTransformRequest against this flag; that has to be handled
     * there rather than here because screenHardwareTransform rests at FALSE
     * between appends and so says nothing about the pending batch. */
    context->screenHardwareTransform = FALSE;
    context->screenProjective = FALSE;

    for (index = 0; index < count; ++index) {
        const struct Vertex *input = &vertices[index];
        struct ScreenVertex point;
        GLfloat clipX, clipY, clipZ, clipW, inverseW;

        clipX = matrix[0] * input->x + matrix[4] * input->y +
                matrix[8] * input->z + matrix[12] * input->w;
        clipY = matrix[1] * input->x + matrix[5] * input->y +
                matrix[9] * input->z + matrix[13] * input->w;
        clipZ = matrix[2] * input->x + matrix[6] * input->y +
                matrix[10] * input->z + matrix[14] * input->w;
        clipW = matrix[3] * input->x + matrix[7] * input->y +
                matrix[11] * input->z + matrix[15] * input->w;

        /* Points at or behind the eye plane have no screen position. */
        if (clipW <= 0.0f)
            continue;
        inverseW = 1.0f / clipW;

        point.x = xScale * (clipX * inverseW) + xOffset;
        point.y = yScale * (clipY * inverseW) + yOffset;
        point.z = zScale * (clipZ * inverseW) + zOffset;
        if (point.z < 0.0f) point.z = 0.0f;
        if (point.z > 1.0f) point.z = 1.0f;
        point.rhw = inverseW;
        point.nx = input->nx;
        point.ny = input->ny;
        point.nz = input->nz;
        point.color = input->color;
        point.s = input->s;
        point.t = input->t;
        point.s1 = input->s1;
        point.t1 = input->t1;
        point.fog = input->fog;

        if (!AppendScreenPoint(context, &point))
            return FALSE;
    }
    return TRUE;
}

static void CopyHardwareVertex(struct MiniGLR200Context *context,
                                const struct Vertex *input,
                                struct ScreenVertex *output,
                                ULONG hardwareSphereMap,
                                ULONG softwareSphereMap)
{
    /* Raw longword moves, never the FPU: the field mapping is
     * Vertex{x,y,z,w,nx,ny,nz,s,t,s1,t1,fog,color} ->
     * ScreenVertex{x,y,z,rhw,nx,ny,nz,color,s,t,s1,t1,fog}. FloatBits
     * copies are bitwise, so integer moves are exact. */
    const ULONG *in = (const ULONG *)input;
    ULONG *out = (ULONG *)output;
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
    if (context->lightingEnabled || hardwareSphereMap) {
        out[4] = in[4];
        out[5] = in[5];
        out[6] = in[6];
    }
    out[7] = in[12];
    out[8] = in[7];
    out[9] = in[8];
    out[10] = in[9];
    out[11] = in[10];
    out[12] = in[11];
    if (softwareSphereMap) {
        struct ClipVertex generated;
        generated.s = input->s;
        generated.t = input->t;
        generated.s1 = input->s1;
        generated.t1 = input->t1;
        GenerateSphereMap(context, input, &generated, softwareSphereMap);
        output->s = generated.s;
        output->t = generated.t;
        output->s1 = generated.s1;
        output->t1 = generated.t1;
    }
}

static BOOL HardwareListCommand(ULONG command)
{
    /* Only list primitives may concatenate across glBegin/glEnd blocks.
     * Strip, fan and loop sequences are order-dependent: appending a second
     * sequence to an open one would bridge them with spurious geometry. */
    return command == RADEON3D_EXEC_DRAW_TRIANGLES ||
           command == RADEON3D_EXEC_DRAW_QUADS ||
           command == RADEON3D_EXEC_DRAW_POINTS ||
           command == RADEON3D_EXEC_DRAW_LINES;
}

static BOOL EmitHardwareBatch(struct MiniGLR200Context *context,
                               const struct Vertex *vertices, ULONG count,
                               ULONG command)
{
    ULONG vertexLimit = command == RADEON3D_EXEC_DRAW_QUADS
                            ? MAX_QUAD_VERTICES : MAX_SCREEN_VERTICES;
    ULONG hardwareSphereMap = HardwareSphereMapMask(context);
    ULONG softwareSphereMap = SoftwareSphereMapMask(context,
                                                     hardwareSphereMap);
    ULONG index;

    if (!count || count > vertexLimit)
        return FALSE;
    if (context->screenVertexCount + count > vertexLimit &&
        !FlushScreenVertices(context))
        return FALSE;
    context->screenHardwareTransform = TRUE;
    context->screenProjective = FALSE;
#if MGLPPC_PROFILE
    /* Profile builds: split EmitHardwareBatch into append preparation
     * (appendTicks), per-vertex copy (vertexFetchTicks) and the flush
     * (drawTicks, inclusive of its nested serialize/execute). */
    {
        ULONG t0 = PerfClock(), t1, t2;
        BOOL ok;

        if (!PrepareScreenAppend(context, command)) {
            context->screenHardwareTransform = FALSE;
            return FALSE;
        }
        t1 = PerfClock();
        for (index = 0; index < count; ++index)
            CopyHardwareVertex(context, &vertices[index],
                               &context->screenVertices[context->screenVertexCount++],
                               hardwareSphereMap, softwareSphereMap);
        t2 = PerfClock();
        context->perf.appendTicks += t1 - t0;
        context->perf.vertexFetchTicks += t2 - t1;
        context->screenHardwareTransform = FALSE;
        if (HardwareListCommand(command))
            return TRUE;
        ok = FlushScreenVertices(context);
        context->perf.drawTicks += PerfClock() - t2;
        return ok;
    }
#else
    if (!PrepareScreenAppend(context, command)) {
        context->screenHardwareTransform = FALSE;
        return FALSE;
    }
    for (index = 0; index < count; ++index)
        CopyHardwareVertex(context, &vertices[index],
                           &context->screenVertices[context->screenVertexCount++],
                           hardwareSphereMap, softwareSphereMap);
    context->screenHardwareTransform = FALSE;
    if (HardwareListCommand(command))
        return TRUE;
    return FlushScreenVertices(context);
#endif
}

static BOOL EmitHardwareTriangle(struct MiniGLR200Context *context,
                                 const struct Vertex *a,
                                 const struct Vertex *b,
                                 const struct Vertex *c)
{
    struct Vertex triangle[3];
    triangle[0] = *a;
    triangle[1] = *b;
    triangle[2] = *c;
    return EmitHardwareBatch(context, triangle, 3UL,
                             RADEON3D_EXEC_DRAW_TRIANGLES);
}

static BOOL EmitHardwareLine(struct MiniGLR200Context *context,
                             const struct Vertex *a,
                             const struct Vertex *b)
{
    struct Vertex line[2];
    line[0] = *a;
    line[1] = *b;
    return EmitHardwareBatch(context, line, 2UL, RADEON3D_EXEC_DRAW_LINES);
}

/* Flat-shaded triangles take the provoking vertex's colour on every
 * fragment; GL_POLYGON takes the first vertex, but the hardware shades the
 * last vertex of each fan triangle. Emit such polygons as flat triangles. */
static BOOL EmitHardwareFlatFanTriangle(struct MiniGLR200Context *context,
                                        const struct Vertex *a,
                                        const struct Vertex *b,
                                        const struct Vertex *c,
                                        ULONG color)
{
    struct Vertex triangle[3];
    triangle[0] = *a;
    triangle[1] = *b;
    triangle[2] = *c;
    triangle[0].color = triangle[1].color = triangle[2].color = color;
    return EmitHardwareBatch(context, triangle, 3UL,
                             RADEON3D_EXEC_DRAW_TRIANGLES);
}

static BOOL EmitHardwarePolygonEdges(struct MiniGLR200Context *context,
                                     const struct Vertex *vertices,
                                     ULONG count, GLenum primitive)
{
    ULONG index;

    switch (primitive) {
    case GL_TRIANGLES:
        for (index = 0; index + 2UL < count; index += 3UL)
            if (!EmitHardwareLine(context, &vertices[index],
                                  &vertices[index + 1UL]) ||
                !EmitHardwareLine(context, &vertices[index + 1UL],
                                  &vertices[index + 2UL]) ||
                !EmitHardwareLine(context, &vertices[index + 2UL],
                                  &vertices[index]))
                return FALSE;
        return TRUE;
    case GL_TRIANGLE_FAN:
        for (index = 1; index + 1UL < count; ++index)
            if (!EmitHardwareLine(context, &vertices[0], &vertices[index]) ||
                !EmitHardwareLine(context, &vertices[index],
                                  &vertices[index + 1UL]) ||
                !EmitHardwareLine(context, &vertices[index + 1UL],
                                  &vertices[0]))
                return FALSE;
        return TRUE;
    case GL_POLYGON:
        for (index = 0; index < count; ++index)
            if (!EmitHardwareLine(context, &vertices[index],
                                  &vertices[(index + 1UL) % count]))
                return FALSE;
        return TRUE;
    case GL_TRIANGLE_STRIP:
        for (index = 0; index + 2UL < count; ++index)
            if (!EmitHardwareLine(context, &vertices[index],
                                  &vertices[index + 1UL]) ||
                !EmitHardwareLine(context, &vertices[index + 1UL],
                                  &vertices[index + 2UL]) ||
                !EmitHardwareLine(context, &vertices[index + 2UL],
                                  &vertices[index]))
                return FALSE;
        return TRUE;
    case GL_QUADS:
        for (index = 0; index + 3UL < count; index += 4UL)
            if (!EmitHardwareLine(context, &vertices[index],
                                  &vertices[index + 1UL]) ||
                !EmitHardwareLine(context, &vertices[index + 1UL],
                                  &vertices[index + 2UL]) ||
                !EmitHardwareLine(context, &vertices[index + 2UL],
                                  &vertices[index + 3UL]) ||
                !EmitHardwareLine(context, &vertices[index + 3UL],
                                  &vertices[index]))
                return FALSE;
        return TRUE;
    case GL_QUAD_STRIP:
        for (index = 0; index + 3UL < count; index += 2UL)
            if (!EmitHardwareLine(context, &vertices[index],
                                  &vertices[index + 1UL]) ||
                !EmitHardwareLine(context, &vertices[index + 1UL],
                                  &vertices[index + 3UL]) ||
                !EmitHardwareLine(context, &vertices[index + 3UL],
                                  &vertices[index + 2UL]) ||
                !EmitHardwareLine(context, &vertices[index + 2UL],
                                  &vertices[index]))
                return FALSE;
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL EmitPrimitiveInternal(struct MiniGLR200Context *context,
                                  const struct Vertex *vertices, ULONG count,
                                  GLenum primitive)
{
    ULONG index, batch;

    if (primitive == MGL_FLATFAN || primitive == MGL_FLATSTRIP) {
        context->screenHardwareTransform = FALSE;
        context->screenProjective = FALSE;
        return EmitFlatPrimitive(context, vertices, count, primitive);
    }
    if (SoftwareSphereMapMask(context, HardwareSphereMapMask(context)) &&
        !context->normalMatrixValid)
        BuildNormalMatrix(context);
    if ((primitive == GL_TRIANGLES || primitive == GL_TRIANGLE_FAN ||
         primitive == GL_POLYGON || primitive == GL_TRIANGLE_STRIP ||
         primitive == GL_QUAD_STRIP || primitive == GL_QUADS) &&
        context->polygonMode == GL_POINT) {
        ULONG pointCount = count;
        if (primitive == GL_TRIANGLES)
            pointCount -= pointCount % 3UL;
        else if (primitive == GL_QUADS)
            pointCount &= ~3UL;
        else if (primitive == GL_QUAD_STRIP)
            pointCount = pointCount >= 4UL ? pointCount & ~1UL : 0;
        else if (pointCount < 3UL)
            pointCount = 0;
        return !pointCount || EmitHardwareBatch(
            context, vertices, pointCount, RADEON3D_EXEC_DRAW_POINTS);
    }
    if ((primitive == GL_TRIANGLES || primitive == GL_TRIANGLE_FAN ||
         primitive == GL_POLYGON || primitive == GL_TRIANGLE_STRIP ||
         primitive == GL_QUAD_STRIP || primitive == GL_QUADS) &&
        context->polygonMode == GL_LINE)
        return EmitHardwarePolygonEdges(context, vertices, count, primitive);
    if (context->perfEnabled && count == 4UL &&
        (primitive == GL_TRIANGLE_STRIP || primitive == GL_QUADS)) {
        context->perf.quadAttempts++;
        context->perf.quadUsed++;
    }
    switch (primitive) {
    case GL_POINTS:
        if (context->pointSize > 1.5f)
            return EmitSizedPoints(context, vertices, count);
        for (index = 0; index < count; index += batch) {
            batch = count - index;
            if (batch > MAX_SCREEN_VERTICES) batch = MAX_SCREEN_VERTICES;
            if (!EmitHardwareBatch(context, vertices + index, batch,
                                   RADEON3D_EXEC_DRAW_POINTS))
                return FALSE;
        }
        return TRUE;
    case GL_LINES:
        for (index = 0; index + 1 < count; index += batch) {
            batch = count - index;
            if (batch > MAX_SCREEN_VERTICES - 1UL)
                batch = MAX_SCREEN_VERTICES - 1UL;
            batch &= ~1UL;
            if (!EmitHardwareBatch(context, vertices + index, batch,
                                   RADEON3D_EXEC_DRAW_LINES))
                return FALSE;
        }
        return TRUE;
    case GL_LINE_STRIP:
        if (count <= MAX_SCREEN_VERTICES)
            return count < 2UL || EmitHardwareBatch(
                context, vertices, count, RADEON3D_EXEC_DRAW_LINE_STRIP);
        for (index = 0; index + 1 < count; ++index)
            if (!EmitHardwareLine(context, &vertices[index],
                                  &vertices[index + 1]))
                return FALSE;
        return TRUE;
    case GL_LINE_LOOP:
        if (count <= MAX_SCREEN_VERTICES)
            return count < 2UL || EmitHardwareBatch(
                context, vertices, count, RADEON3D_EXEC_DRAW_LINE_LOOP);
        for (index = 0; index < count; ++index)
            if (!EmitHardwareLine(context, &vertices[index],
                                  &vertices[(index + 1UL) % count]))
                return FALSE;
        return TRUE;
    case GL_TRIANGLES:
        for (index = 0; index + 2 < count; index += batch) {
            batch = count - index;
            if (batch > MAX_SCREEN_VERTICES) batch = MAX_SCREEN_VERTICES;
            batch -= batch % 3UL;
            if (!EmitHardwareBatch(context, vertices + index, batch,
                                   RADEON3D_EXEC_DRAW_TRIANGLES))
                return FALSE;
        }
        return TRUE;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        if (count < 3UL)
            return TRUE;
        if (primitive == GL_POLYGON && context->shadeModel == GL_FLAT) {
            /* Flat shading on GL_POLYGON takes the first vertex's colour;
             * the hardware picks the last vertex of each fan triangle. */
            for (index = 1; index + 1UL < count; ++index)
                if (!EmitHardwareFlatFanTriangle(
                        context, &vertices[0], &vertices[index],
                        &vertices[index + 1UL], vertices[0].color))
                    return FALSE;
            return TRUE;
        }
        if (count <= MAX_SCREEN_VERTICES)
            return EmitHardwareBatch(context, vertices, count,
                                     RADEON3D_EXEC_DRAW_TRI_FAN);
        for (index = 1; index + 1 < count; ++index)
            if (!EmitHardwareTriangle(context, &vertices[0], &vertices[index],
                                      &vertices[index + 1]))
                return FALSE;
        return TRUE;
    case GL_TRIANGLE_STRIP:
        if (count < 3UL)
            return TRUE;
        /* A smooth, unculled 4-vertex strip is exactly the R200 quad
         * (v0,v1,v3,v2): the same two triangles over the same v1-v2
         * diagonal, so coverage and Gouraud interpolation are identical.
         * Emitting it as a quad lets it merge with other quad batches
         * where a strip sequence could not. */
        if (count == 4UL && !context->cullFaceEnabled &&
            context->shadeModel != GL_FLAT) {
            struct Vertex quad[4];
            quad[0] = vertices[0];
            quad[1] = vertices[1];
            quad[2] = vertices[3];
            quad[3] = vertices[2];
            return EmitHardwareBatch(context, quad, 4UL,
                                     RADEON3D_EXEC_DRAW_QUADS);
        }
        if (count <= MAX_SCREEN_VERTICES)
            return EmitHardwareBatch(context, vertices, count,
                                     RADEON3D_EXEC_DRAW_TRI_STRIP);
        for (index = 0; index + 2 < count; ++index) {
            const struct Vertex *a = &vertices[index];
            const struct Vertex *b = &vertices[index + 1];
            if (index & 1UL) {
                const struct Vertex *swap = a;
                a = b;
                b = swap;
            }
            if (!EmitHardwareTriangle(context, a, b, &vertices[index + 2]))
                return FALSE;
        }
        return TRUE;
    case GL_QUAD_STRIP:
        count &= ~1UL;
        if (count < 4UL)
            return TRUE;
        if (count <= MAX_SCREEN_VERTICES)
            return EmitHardwareBatch(context, vertices, count,
                                     RADEON3D_EXEC_DRAW_TRI_STRIP);
        for (index = 0; index + 2 < count; ++index) {
            const struct Vertex *a = &vertices[index];
            const struct Vertex *b = &vertices[index + 1];
            if (index & 1UL) {
                const struct Vertex *swap = a;
                a = b;
                b = swap;
            }
            if (!EmitHardwareTriangle(context, a, b, &vertices[index + 2]))
                return FALSE;
        }
        return TRUE;
    case GL_QUADS:
        for (index = 0; index + 3UL < count; index += 4UL) {
            struct Vertex quad[4];
            quad[0] = vertices[index + 3UL];
            quad[1] = vertices[index];
            quad[2] = vertices[index + 1UL];
            quad[3] = vertices[index + 2UL];
            if (context->shadeModel == GL_FLAT) {
                /* GL takes the flat colour from the quad's fourth vertex,
                 * but the R200 quad order above ends on the third. */
                quad[0].color = quad[1].color =
                quad[2].color = quad[3].color =
                    vertices[index + 3UL].color;
            }
            if (!EmitHardwareBatch(context, quad, 4UL,
                                   RADEON3D_EXEC_DRAW_QUADS))
                return FALSE;
        }
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL EmitPrimitive(struct MiniGLR200Context *context,
                          const struct Vertex *vertices, ULONG count,
                          GLenum primitive)
{
    ULONG start, executeBefore, waitBefore, nested, elapsed;
    BOOL result;

    if (!context->perfEnabled)
        return EmitPrimitiveInternal(context, vertices, count, primitive);
    context->perf.primitives++;
    context->perf.inputVertices += count;
    executeBefore = context->perf.executeTicks;
    waitBefore = context->perf.waitTicks;
    start = PerfClock();
    result = EmitPrimitiveInternal(context, vertices, count, primitive);
    elapsed = PerfClock() - start;
    nested = (context->perf.executeTicks - executeBefore) +
             (context->perf.waitTicks - waitBefore);
    context->perf.frontendTicks += elapsed >= nested ? elapsed - nested : 0;
    return result;
}

void Lib_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (context)
        context->currentColor = PackColor(red, green, blue, alpha);
}

void Lib_glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    Lib_glColor4f(red, green, blue, 1.0f);
}

void Lib_glColor4fv(GLfloat *value)
{
    if (value)
        Lib_glColor4f(value[0], value[1], value[2], value[3]);
}

void Lib_glColor3fv(GLfloat *value)
{
    if (value)
        Lib_glColor3f(value[0], value[1], value[2]);
}

void Lib_glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (context)
        context->currentColor = ((ULONG)alpha << 24) | ((ULONG)red << 16) |
                                ((ULONG)green << 8) | blue;
}

void Lib_glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    Lib_glColor4ub(red, green, blue, 255);
}

void Lib_glColor4ubv(GLubyte *value)
{
    if (value)
        Lib_glColor4ub(value[0], value[1], value[2], value[3]);
}

void Lib_glColor3ubv(GLubyte *value)
{
    if (value)
        Lib_glColor3ub(value[0], value[1], value[2]);
}

void Lib_glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    context->currentNormalX = x;
    context->currentNormalY = y;
    context->currentNormalZ = z;
}

/*
 * V19-parity mglDrawMultitexBuffer support. Classic MiniGL queued
 * GL_POLYGON draws while texture unit 1 was enabled and replayed them as
 * two textured passes when the client called mglDrawMultitexBuffer()
 * (mglQ3 draw.c d_DrawMtexPoly / MGLDrawMultitexBuffer). The R200 backend
 * reproduces that contract with a context-owned queue: polygons are
 * captured at glEnd() with both bound texture names, then flushed as a
 * unit-0 pass followed by a unit-1 pass carrying the requested blend
 * factors and texture environment.
 *
 * Differences from V19, all deliberate (see plan.md): the queue lives in
 * the context instead of process globals, overflow errors instead of
 * silently dropping when allocation was not possible, and the second pass
 * is made equal-depth-safe (depth writes disabled, test widened to
 * GL_LEQUAL) so the pass cannot be hidden by the first pass's depth.
 */

static ULONG MtexQueueCapacity(void)
{
    ULONG vertices = LibraryBase && (ULONG)LibraryBase->mtexBufferSize > 0
                         ? (ULONG)LibraryBase->mtexBufferSize
                         : R200_MTEX_DEFAULT_VERTS;
    /* V19 reserved two vertex slots per queued polygon. */
    ULONG polygons = vertices / 8UL;
    if (polygons < R200_MTEX_MIN_POLYGONS)
        polygons = R200_MTEX_MIN_POLYGONS;
    if (polygons > R200_MTEX_MAX_POLYGONS)
        polygons = R200_MTEX_MAX_POLYGONS;
    return polygons;
}

BOOL CaptureMtexPolygon(struct MiniGLR200Context *context)
{
    struct MtexPolygon *polygon;
    ULONG index, capacity;

    if (context->vertexCount < 3UL)
        return FALSE;
    if (context->vertexCount > 16UL)
        return FALSE; /* V19 queued arbitrary n-gons; R200 queue now holds up to 16 */
    if (!context->mtexQueue) {
        capacity = MtexQueueCapacity();
        context->mtexQueue = AllocMem(capacity * sizeof(struct MtexPolygon),
                                      MEMF_PUBLIC | MEMF_CLEAR);
        if (!context->mtexQueue)
            return FALSE; /* allocation failure: draw normally */
        context->mtexQueueCapacity = capacity;
    }
    if (context->mtexQueueCount >= context->mtexQueueCapacity) {
        /* V19 silently dropped polygons past capacity. */
        if (context->perfEnabled) context->perf.dropTexture1++;
        return TRUE;
    }
    polygon = &context->mtexQueue[context->mtexQueueCount++];
    polygon->vertexCount = context->vertexCount;
    for (index = 0; index < context->vertexCount; ++index)
        polygon->vertices[index] = context->vertices[index];
    polygon->texture0 = context->boundTexture[0];
    polygon->texture1 = context->boundTexture[1];
    return TRUE;
}

void FreeMtexQueue(struct MiniGLR200Context *context)
{
    if (context->mtexQueue) {
        FreeMem(context->mtexQueue,
                context->mtexQueueCapacity * sizeof(struct MtexPolygon));
        context->mtexQueue = NULL;
        context->mtexQueueCapacity = 0;
        context->mtexQueueCount = 0;
    }
}

void FlushMtexBuffer(struct MiniGLR200Context *context, GLenum blendSource,
                     GLenum blendDestination, GLenum textureEnv)
{
    ULONG savedActiveTexture, savedBlendSource, savedBlendDestination;
    ULONG savedBlendEnabled, savedDepthFunc, savedDepthMask;
    ULONG savedTextureEnabled0, savedTextureEnabled1;
    GLenum savedTextureEnv1;
    BOOL stateChanged = FALSE;
    ULONG index;

    if (context->insideBegin || context->mtexQueueCount == 0)
        return;
    /* Ordering barrier: pending normal geometry must be submitted before
     * the queued passes so present/readback order stays sane. */
    if (!FlushScreenVertices(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    savedActiveTexture = context->activeTexture;
    savedBlendSource = context->blendSource;
    savedBlendDestination = context->blendDestination;
    savedBlendEnabled = context->blendEnabled;
    savedDepthFunc = context->depthFunc;
    savedDepthMask = context->depthMask;
    savedTextureEnabled0 = context->textureEnabled[0];
    savedTextureEnabled1 = context->textureEnabled[1];
    savedTextureEnv1 = context->textureEnv[1];

    /* V19 mapped unknown environments to REPLACE (MODULATE -> W3D_MODULATE,
     * DECAL -> W3D_DECAL, everything else -> W3D_REPLACE). */
    if (textureEnv != GL_MODULATE && textureEnv != GL_DECAL)
        textureEnv = GL_REPLACE;

    if (!context->blendEnabled) {
        context->blendEnabled = TRUE;
        stateChanged = TRUE;
    }
    if (context->blendSource != blendSource ||
        context->blendDestination != blendDestination) {
        context->blendSource = blendSource;
        context->blendDestination = blendDestination;
        stateChanged = TRUE;
    }
    /* Equal-depth safety for the second pass (V19 wrote depth during both
     * passes and relied on depth test being off in its demo). */
    if (context->depthTest) {
        context->depthFunc = GL_LEQUAL;
        context->depthMask = FALSE;
        stateChanged = TRUE;
    }
    if (stateChanged)
        BumpSemanticStateSerial(context);

    for (index = 0; index < context->mtexQueueCount; ++index) {
        struct MtexPolygon *polygon = &context->mtexQueue[index];
        BOOL passFailed = FALSE;

        /* Pass 1: unit 0 only. */
        context->textureEnabled[0] = TRUE;
        context->textureEnabled[1] = FALSE;
        context->activeTexture = 0;
        BumpSemanticStateSerial(context);
        if (polygon->texture0 != context->boundTexture[0]) {
            context->boundTexture[0] = polygon->texture0;
            BumpSemanticStateSerial(context);
        }
        if (!EmitPrimitive(context, polygon->vertices, polygon->vertexCount,
                           GL_POLYGON))
            passFailed = TRUE;

        /* Pass 2: unit 1 with the requested environment. */
        if (!passFailed) {
            context->textureEnabled[0] = FALSE;
            context->textureEnabled[1] = TRUE;
            context->textureEnv[1] = textureEnv;
            context->activeTexture = 1;
            BumpSemanticStateSerial(context);
            if (polygon->texture1 != context->boundTexture[1]) {
                context->boundTexture[1] = polygon->texture1;
                BumpSemanticStateSerial(context);
            }
            if (!EmitPrimitive(context, polygon->vertices,
                               polygon->vertexCount, GL_POLYGON))
                passFailed = TRUE;
        }
        if (passFailed) {
            SetError(context, GL_INVALID_OPERATION);
            break;
        }
    }

    /* Restore captured state. */
    context->activeTexture = savedActiveTexture;
    context->textureEnabled[0] = savedTextureEnabled0;
    context->textureEnabled[1] = savedTextureEnabled1;
    context->textureEnv[1] = savedTextureEnv1;
    context->blendSource = savedBlendSource;
    context->blendDestination = savedBlendDestination;
    context->blendEnabled = savedBlendEnabled;
    context->depthFunc = savedDepthFunc;
    context->depthMask = savedDepthMask;
    BumpSemanticStateSerial(context);
    context->mtexQueueCount = 0;
}

void Lib_mglDrawMultitexBuffer(GLenum BSrc, GLenum BDst, GLenum TexEnv)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    FlushMtexBuffer(context, BSrc, BDst, TexEnv);
}

void Lib_glBegin(GLenum primitive)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context)
        return;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!ValidPrimitive(primitive)) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    context->insideBegin = TRUE;
    context->primitive = primitive;
    context->vertexCount = 0;
    context->texCoordFeedMask = 0;
}

static void AddVertex(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct Vertex *vertex;

    if (!context || !context->insideBegin || context->activeFastPath) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (context->vertexCount >= MAX_VERTICES) {
        SetError(context, GL_OUT_OF_MEMORY);
        return;
    }
    vertex = &context->vertices[context->vertexCount++];
    vertex->x = x;
    vertex->y = y;
    vertex->z = z;
    vertex->w = w;
    vertex->nx = context->currentNormalX;
    vertex->ny = context->currentNormalY;
    vertex->nz = context->currentNormalZ;
    vertex->t = context->currentT[0];
    vertex->s = context->currentS[0];
    vertex->s1 = context->currentS[1];
    vertex->t1 = context->currentT[1];
    vertex->fog = FogFactor(context,x,y,z,w);
    vertex->color = context->currentColor;
}

void Lib_glVertex2f(GLfloat x, GLfloat y) { AddVertex(x, y, 0.0f, 1.0f); }
void Lib_glVertex3f(GLfloat x, GLfloat y, GLfloat z) { AddVertex(x, y, z, 1.0f); }
void Lib_glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    AddVertex(x, y, z, w);
}
void Lib_glVertex2fv(GLfloat *v) { if (v) Lib_glVertex2f(v[0], v[1]); }
void Lib_glVertex3fv(GLfloat *v) { if (v) Lib_glVertex3f(v[0], v[1], v[2]); }
void Lib_glVertex4fv(GLfloat *v) { if (v) Lib_glVertex4f(v[0], v[1], v[2], v[3]); }

void Lib_glEnd(void)
{
    struct MiniGLR200Context *context = CurrentContext();

    if (!context || !context->insideBegin || context->activeFastPath) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    context->insideBegin = FALSE;
    /* V19 parity: GL_POLYGON with texture unit 1 enabled goes to the
     * two-pass multitexture queue instead of drawing immediately --
     * but only for the classic fake-multitexture feed order, where unit 0
     * is fed explicitly and unit 1 implicitly (glMultiTexCoord2fARB on
     * unit 0 while unit 1 is active, then glTexCoord2f, as Quake's
     * R_RenderBrushPoly/mglMtexTV23fv does). A client that drives unit 1
     * explicitly with glMultiTexCoord2fARB (Quake's R_DrawSequentialPoly
     * real-multitexture path) draws immediately through both units, like
     * QUADS/TRIANGLE_FAN always do. */
    if (context->primitive == GL_POLYGON && context->textureEnabled[1] &&
        !(context->texCoordFeedMask & 2UL) &&
        CaptureMtexPolygon(context))
        return;
    if (!EmitPrimitive(context, context->vertices, context->vertexCount,
                       context->primitive))
        SetError(context, GL_INVALID_OPERATION);
}

void Lib_glTexCoord2f(GLfloat s, GLfloat t)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (context) {
        ULONG unit = context->activeTexture;
        if (unit > 1UL) unit = 0;
        context->currentS[unit]=s;
        context->currentT[unit]=t;
    }
}
void Lib_glTexCoord2fv(GLfloat *value)
{
    if (value)
        Lib_glTexCoord2f(value[0],value[1]);
}
void Lib_glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    (void)r;
    if (q!=0.0f)
        Lib_glTexCoord2f(s/q,t/q);
    else
        SetError(CurrentContext(),GL_INVALID_VALUE);
}
void Lib_glTexCoord4fv(GLfloat *value)
{
    if (value)
        Lib_glTexCoord4f(value[0],value[1],value[2],value[3]);
}

void Lib_glActiveTextureARB(GLenum unit)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context))
        return;
    if (unit!=GL_TEXTURE0_ARB && unit!=GL_TEXTURE1_ARB) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    if (context->activeTexture==(ULONG)(unit-GL_TEXTURE0_ARB)) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->activeTexture=(ULONG)(unit-GL_TEXTURE0_ARB);
}

void Lib_glMultiTexCoord2fARB(GLenum unit, GLfloat s, GLfloat t)
{
    struct MiniGLR200Context *context=CurrentContext(); ULONG index;
    if (!context) return;
    if (unit!=GL_TEXTURE0_ARB && unit!=GL_TEXTURE1_ARB) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    index=(ULONG)(unit-GL_TEXTURE0_ARB);
    context->currentS[index]=s; context->currentT[index]=t;
    /* Explicit per-unit feed (see Lib_glEnd): distinguishes Quake's real
     * dual-unit path from the classic fake-multitexture feed order. The
     * legacy implicit glTexCoord2f deliberately does not set this. */
    context->texCoordFeedMask |= 1UL << index;
}

void Lib_glMultiTexCoord2fvARB(GLenum unit, GLfloat *value)
{
    if (value) Lib_glMultiTexCoord2fARB(unit,value[0],value[1]);
}

void Lib_glTexCoordPointer(GLint size, GLenum type, GLsizei stride,
                           const GLvoid *pointer)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (size<1 || size>4) { SetError(context,GL_INVALID_VALUE); return; }
    if (type!=GL_FLOAT) { SetError(context,GL_INVALID_ENUM); return; }
    if (stride<0) { SetError(context,GL_INVALID_VALUE); return; }
    context->texcoordArray.pointer=pointer; context->texcoordArray.size=size;
    context->texcoordArray.type=type;
    context->texcoordArray.stride=stride ? (ULONG)stride : size*4UL;
}

static ULONG TypeSize(GLenum type)
{
    switch (type) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: case MGL_UBYTE_ARGB: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: return 4;
    case GL_DOUBLE: return 8;
    default: return 0;
    }
}
static GLfloat ReadNumber(const UBYTE *pointer, GLenum type)
{
    switch (type) {
    case GL_BYTE: { signed char v; CopyBytes(&v,pointer,1); return v; }
    case GL_UNSIGNED_BYTE: { UBYTE v; CopyBytes(&v,pointer,1); return v; }
    case GL_SHORT: { short v; CopyBytes(&v,pointer,2); return v; }
    case GL_UNSIGNED_SHORT: { UWORD v; CopyBytes(&v,pointer,2); return v; }
    case GL_INT: { int v; CopyBytes(&v,pointer,4); return (GLfloat)v; }
    case GL_UNSIGNED_INT: { unsigned int v; CopyBytes(&v,pointer,4); return (GLfloat)v; }
    case GL_FLOAT: { GLfloat v; CopyBytes(&v,pointer,4); return v; }
    default: { GLdouble v; CopyBytes(&v,pointer,8); return (GLfloat)v; }
    }
}

static GLfloat ReadColor(const UBYTE *pointer, GLenum type)
{
    GLfloat value = ReadNumber(pointer, type);
    switch (type) {
    case GL_UNSIGNED_BYTE: return value / 255.0f;
    case GL_BYTE: return value <= 0.0f ? 0.0f : value / 127.0f;
    case GL_UNSIGNED_SHORT: return value / 65535.0f;
    case GL_SHORT: return value <= 0.0f ? 0.0f : value / 32767.0f;
    case GL_UNSIGNED_INT: return value / 4294967295.0f;
    case GL_INT: return value <= 0.0f ? 0.0f : value / 2147483647.0f;
    default: return value;
    }
}

void Lib_glVertexPointer(GLint size, GLenum type, GLsizei stride,
                         const GLvoid *pointer)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG bytes = TypeSize(type);
    if (!StateChangeAllowed(context)) return;
    if (size < 2 || size > 4) { SetError(context, GL_INVALID_VALUE); return; }
    if (!bytes || (type != GL_SHORT && type != GL_INT &&
                   type != GL_FLOAT && type != GL_DOUBLE)) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (stride < 0) { SetError(context, GL_INVALID_VALUE); return; }
    context->vertexArray.pointer = pointer;
    context->vertexArray.size = size;
    context->vertexArray.type = type;
    context->vertexArray.stride = stride ? (ULONG)stride : size * bytes;
}

void Lib_glColorPointer(GLint size, GLenum type, GLsizei stride,
                        const GLvoid *pointer)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG bytes = TypeSize(type);
    if (!StateChangeAllowed(context)) return;
    if (size < 3 || size > 4) { SetError(context, GL_INVALID_VALUE); return; }
    if (!bytes || (type == MGL_UBYTE_ARGB && size != 4)) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (stride < 0) { SetError(context, GL_INVALID_VALUE); return; }
    context->colorArray.pointer = pointer;
    context->colorArray.size = size;
    context->colorArray.type = type;
    context->colorArray.stride = stride ? (ULONG)stride : size * bytes;
}

void Lib_glNormalPointer(GLenum type, GLsizei stride,
                         const GLvoid *pointer)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG bytes;
    if (!StateChangeAllowed(context)) return;
    bytes = TypeSize(type);
    if (!bytes || type == MGL_UBYTE_ARGB ||
        (type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT &&
         type != GL_UNSIGNED_SHORT && type != GL_INT &&
         type != GL_UNSIGNED_INT && type != GL_FLOAT && type != GL_DOUBLE)) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (!pointer || stride < 0) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    context->normalArray.pointer = pointer;
    context->normalArray.size = 3;
    context->normalArray.type = type;
    context->normalArray.stride = stride ? (ULONG)stride : 3UL * bytes;
}

void Lib_glEnableClientState(GLenum state)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (state == GL_VERTEX_ARRAY) context->vertexArray.enabled = TRUE;
    else if (state == GL_COLOR_ARRAY) context->colorArray.enabled = TRUE;
    else if (state == GL_TEXTURE_COORD_ARRAY) context->texcoordArray.enabled = TRUE;
    else if (state == GL_NORMAL_ARRAY) {
        if (!(context->radeonCaps & RADEON3D_CAP_HW_LIGHTING)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        context->normalArray.enabled = TRUE;
    }
    else SetError(context, GL_INVALID_ENUM);
}

void Lib_glDisableClientState(GLenum state)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (state == GL_VERTEX_ARRAY) context->vertexArray.enabled = FALSE;
    else if (state == GL_COLOR_ARRAY) context->colorArray.enabled = FALSE;
    else if (state == GL_TEXTURE_COORD_ARRAY) context->texcoordArray.enabled = FALSE;
    else if (state == GL_NORMAL_ARRAY) context->normalArray.enabled = FALSE;
    else SetError(context, GL_INVALID_ENUM);
}

/*
 * V19-parity glInterleavedArrays: configure vertex/colour/texcoord client
 * arrays from one interleaved buffer. Mirrors mglQ3's GLInterleavedArrays
 * (vertexarray.c): the six classic layouts, client-state REPLACED (not
 * merged) with exactly the arrays the format contains, packed stride when
 * stride == 0, unsupported formats disabling every client array and raising
 * GL_INVALID_ENUM (V19's misspelled GL_INVALIE_ENUM never reached the error
 * path because its error checking was compiled out; the R200 backend always
 * reports it). Normals are never part of an interleaved layout, so the
 * normal array is left disabled.
 */
void Lib_glInterleavedArrays(GLenum format, GLsizei stride,
                             const GLvoid *pointer)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG packed;
    if (!StateChangeAllowed(context)) return;
    if (stride < 0) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (!pointer) {
        /* A null buffer cannot address any layout; V19 dereferenced it
         * unchecked. Refuse rather than fault. */
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    switch (format) {
    case GL_V3F:
        context->vertexArray.enabled = TRUE;
        context->vertexArray.pointer = pointer;
        context->vertexArray.size = 3;
        context->vertexArray.type = GL_FLOAT;
        context->vertexArray.stride =
            stride ? (ULONG)stride : 3UL * sizeof(GLfloat);
        context->colorArray.enabled = FALSE;
        context->texcoordArray.enabled = FALSE;
        break;
    case GL_C4UB_V3F:
        packed = stride ? (ULONG)stride : 4UL + 3UL * sizeof(GLfloat);
        context->vertexArray.enabled = TRUE;
        context->vertexArray.pointer = (const UBYTE *)pointer + 4;
        context->vertexArray.size = 3;
        context->vertexArray.type = GL_FLOAT;
        context->vertexArray.stride = packed;
        context->colorArray.enabled = TRUE;
        context->colorArray.pointer = pointer;
        context->colorArray.size = 4;
        context->colorArray.type = GL_UNSIGNED_BYTE;
        context->colorArray.stride = packed;
        context->texcoordArray.enabled = FALSE;
        break;
    case GL_C3F_V3F:
        packed = stride ? (ULONG)stride : 6UL * sizeof(GLfloat);
        context->vertexArray.enabled = TRUE;
        context->vertexArray.pointer = (const UBYTE *)pointer + 3 * sizeof(GLfloat);
        context->vertexArray.size = 3;
        context->vertexArray.type = GL_FLOAT;
        context->vertexArray.stride = packed;
        context->colorArray.enabled = TRUE;
        context->colorArray.pointer = pointer;
        context->colorArray.size = 3;
        context->colorArray.type = GL_FLOAT;
        context->colorArray.stride = packed;
        context->texcoordArray.enabled = FALSE;
        break;
    case GL_T2F_V3F:
        packed = stride ? (ULONG)stride : 5UL * sizeof(GLfloat);
        context->vertexArray.enabled = TRUE;
        context->vertexArray.pointer = (const UBYTE *)pointer + 2 * sizeof(GLfloat);
        context->vertexArray.size = 3;
        context->vertexArray.type = GL_FLOAT;
        context->vertexArray.stride = packed;
        context->texcoordArray.enabled = TRUE;
        context->texcoordArray.pointer = pointer;
        context->texcoordArray.size = 2;
        context->texcoordArray.type = GL_FLOAT;
        context->texcoordArray.stride = packed;
        context->colorArray.enabled = FALSE;
        break;
    case GL_T2F_C4UB_V3F:
        packed = stride ? (ULONG)stride : 5UL * sizeof(GLfloat) + 4UL;
        context->vertexArray.enabled = TRUE;
        context->vertexArray.pointer = (const UBYTE *)pointer + 12;
        context->vertexArray.size = 3;
        context->vertexArray.type = GL_FLOAT;
        context->vertexArray.stride = packed;
        context->colorArray.enabled = TRUE;
        context->colorArray.pointer = (const UBYTE *)pointer + 8;
        context->colorArray.size = 4;
        context->colorArray.type = GL_UNSIGNED_BYTE;
        context->colorArray.stride = packed;
        context->texcoordArray.enabled = TRUE;
        context->texcoordArray.pointer = pointer;
        context->texcoordArray.size = 2;
        context->texcoordArray.type = GL_FLOAT;
        context->texcoordArray.stride = packed;
        break;
    case GL_T2F_C3F_V3F:
        packed = stride ? (ULONG)stride : 8UL * sizeof(GLfloat);
        context->vertexArray.enabled = TRUE;
        /* [S T][C3][V3]: vertex at byte 20, colour at 8, texcoords at 0. */
        context->vertexArray.pointer = (const UBYTE *)pointer + 5 * sizeof(GLfloat);
        context->vertexArray.size = 3;
        context->vertexArray.type = GL_FLOAT;
        context->vertexArray.stride = packed;
        context->colorArray.enabled = TRUE;
        context->colorArray.pointer = (const UBYTE *)pointer + 2 * sizeof(GLfloat);
        context->colorArray.size = 3;
        context->colorArray.type = GL_FLOAT;
        context->colorArray.stride = packed;
        context->texcoordArray.enabled = TRUE;
        context->texcoordArray.pointer = pointer;
        context->texcoordArray.size = 2;
        context->texcoordArray.type = GL_FLOAT;
        context->texcoordArray.stride = packed;
        break;
    default:
        /* V19 cleared all client state on an unknown format. */
        context->vertexArray.enabled = FALSE;
        context->colorArray.enabled = FALSE;
        context->texcoordArray.enabled = FALSE;
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    context->normalArray.enabled = FALSE;
}

static BOOL FetchArrayVertex(struct MiniGLR200Context *context, ULONG index,
                              struct Vertex *vertex)
{
    const struct ArrayState *array = &context->vertexArray;
    const UBYTE *input;
    ULONG bytes, color = context->currentColor;
    GLfloat values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ULONG component;
    if (!array->enabled || !array->pointer) return FALSE;
    bytes = TypeSize(array->type);
    input = array->pointer + index * array->stride;
    for (component = 0; component < (ULONG)array->size; ++component)
        values[component] = ReadNumber(input + component * bytes, array->type);
    if (context->colorArray.enabled && context->colorArray.pointer) {
        const struct ArrayState *colors = &context->colorArray;
        GLfloat rgba[4] = {1.0f,1.0f,1.0f,1.0f};
        bytes = TypeSize(colors->type);
        input = colors->pointer + index * colors->stride;
        if (colors->type == MGL_UBYTE_ARGB) {
            color = ((ULONG)input[0] << 24) | ((ULONG)input[1] << 16) |
                    ((ULONG)input[2] << 8) | (ULONG)input[3];
        } else {
            for (component = 0; component < (ULONG)colors->size; ++component)
                rgba[component] = ReadColor(input + component * bytes,
                                            colors->type);
            color = PackColor(rgba[0],rgba[1],rgba[2],rgba[3]);
        }
    }
    vertex->x=values[0]; vertex->y=values[1]; vertex->z=values[2];
    vertex->w=values[3]; vertex->color=color;
    vertex->nx=context->currentNormalX;
    vertex->ny=context->currentNormalY;
    vertex->nz=context->currentNormalZ;
    if (context->normalArray.enabled && context->normalArray.pointer) {
        const struct ArrayState *normals=&context->normalArray;
        bytes=TypeSize(normals->type);
        input=normals->pointer+index*normals->stride;
        vertex->nx=ReadNumber(input,normals->type);
        vertex->ny=ReadNumber(input+bytes,normals->type);
        vertex->nz=ReadNumber(input+bytes*2,normals->type);
    }
    vertex->s=context->currentS[0]; vertex->t=context->currentT[0];
    vertex->s1=context->currentS[1]; vertex->t1=context->currentT[1];
    vertex->fog=FogFactor(context,vertex->x,vertex->y,vertex->z,vertex->w);
    if (context->texcoordArray.enabled && context->texcoordArray.pointer) {
        const struct ArrayState *texcoords=&context->texcoordArray;
        bytes=TypeSize(texcoords->type);
        input=texcoords->pointer+index*texcoords->stride;
        vertex->s=ReadNumber(input,texcoords->type);
        vertex->t=texcoords->size > 1
                      ? ReadNumber(input+bytes,texcoords->type) : 0.0f;
    }
    return TRUE;
}

static BOOL FetchDrawVertex(struct MiniGLR200Context *context, ULONG index,
                            struct Vertex *vertex)
{
    if (context->lockedVertices && index >= context->lockedFirst &&
        index - context->lockedFirst < context->lockedCount) {
        struct Vertex live;
        *vertex = context->lockedVertices[index - context->lockedFirst];
        /* EXT_compiled_vertex_array freezes the VERTEX ARRAY only. Quake's
         * stage iterators call glLockArrays BEFORE ComputeTexCoords and then
         * rewrite the per-stage colour/texcoord arrays (tess.svars.*), so
         * serving the locked copy for those fields made every draw sample
         * the PREVIOUS surface's texture coordinates: scrambled/solid glyphs
         * for 2D text, white world base textures, noisy model skins. Keep
         * positions/normals/fog from the lock, but read colour and texture
         * coordinates live. */
        if (FetchArrayVertex(context, index, &live)) {
            vertex->color = live.color;
            vertex->s = live.s;
            vertex->t = live.t;
            vertex->s1 = live.s1;
            vertex->t1 = live.t1;
        }
        vertex->nx = context->currentNormalX;
        vertex->ny = context->currentNormalY;
        vertex->nz = context->currentNormalZ;
        vertex->fog = FogFactor(context,vertex->x,vertex->y,
                                vertex->z,vertex->w);
        return TRUE;
    }
    return FetchArrayVertex(context, index, vertex);
}

void Lib_glLockArrays(GLuint first, GLsizei count)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct Vertex *vertices;
    ULONG index, bytes;

    if (!StateChangeAllowed(context)) return;
    if (context->lockedVertices) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (count <= 0 || (ULONG)count > 0xffffffffUL - first) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (!context->vertexArray.enabled || !context->vertexArray.pointer ||
        (ULONG)count > 0xffffffffUL / sizeof(struct Vertex)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    bytes = (ULONG)count * sizeof(struct Vertex);
    vertices = AllocMem(bytes, MEMF_PUBLIC);
    if (!vertices) {
        SetError(context, GL_OUT_OF_MEMORY);
        return;
    }
    for (index = 0; index < (ULONG)count; ++index) {
        if (!FetchArrayVertex(context, first + index, &vertices[index])) {
            FreeMem(vertices, bytes);
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
    }
    context->lockedVertices = vertices;
    context->lockedFirst = first;
    context->lockedCount = (ULONG)count;
}

void Lib_glUnlockArrays(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (!context->lockedVertices) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    FreeMem(context->lockedVertices,
            context->lockedCount * sizeof(struct Vertex));
    context->lockedVertices = NULL;
    context->lockedFirst = 0;
    context->lockedCount = 0;
}

void Lib_glArrayElement(GLint index)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct Vertex vertex;
    if (!context || !context->insideBegin || context->activeFastPath) {
        SetError(context, GL_INVALID_OPERATION); return;
    }
    if (index < 0) { SetError(context, GL_INVALID_VALUE); return; }
    if (!FetchDrawVertex(context, (ULONG)index, &vertex)) {
        SetError(context, GL_INVALID_OPERATION); return;
    }
    if (context->vertexCount >= MAX_VERTICES) {
        SetError(context, GL_OUT_OF_MEMORY); return;
    }
    context->vertices[context->vertexCount++] = vertex;
}

void Lib_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG index;
    if (!OutsideBeginAllowed(context)) return;
    if (first < 0 || count < 0) { SetError(context, GL_INVALID_VALUE); return; }
    if (!count) return;
    if ((ULONG)count > MAX_VERTICES || !context->vertexArray.enabled) {
        SetError(context, GL_INVALID_OPERATION); return;
    }
    for (index=0; index<(ULONG)count; ++index)
        if (!FetchDrawVertex(context, (ULONG)first+index,
                             &context->vertices[index])) {
            SetError(context, GL_INVALID_OPERATION); return;
        }
    if (!EmitPrimitive(context, context->vertices, (ULONG)count, mode))
        SetError(context, GL_INVALID_ENUM);
}

static ULONG ReadIndex(const UBYTE *indices, GLenum type, ULONG index)
{
    /* Byte-assembled loads instead of CopyBytes: this runs twice per
     * drawn index (cache sizing used to make it more) and an out-of-line
     * byte-loop call per read dominated the frontend. Big-endian assembly
     * matches the native-order load it replaces. */
    if (type == GL_UNSIGNED_BYTE) return indices[index];
    if (type == GL_UNSIGNED_SHORT) {
        const UBYTE *p = indices + index * 2UL;
        return ((ULONG)p[0] << 8) | (ULONG)p[1];
    }
    {
        const UBYTE *p = indices + index * 4UL;
        return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
               ((ULONG)p[2] << 8) | (ULONG)p[3];
    }
}

/*
 * Fused indexed-draw append: resolve one index and pack it straight into
 * the hardware-TCL staging buffer without materialising an expanded vertex
 * array first. Alias meshes re-reference vertices heavily, so the cache
 * hit path is a pointer into the cache entry -- no per-index struct copy
 * survives at all. Batch capacity, state capture and flush boundaries are
 * exactly the ones EmitHardwareBatch applies, so record merging and the
 * streaming commit path see identical geometry.
 */
static BOOL AppendResolvedTclVertex(struct MiniGLR200Context *context,
                                    const struct Vertex *vertex,
                                    ULONG hardwareSphereMap,
                                    ULONG softwareSphereMap)
{
    /* The staging buffer carries one primitive label at a time. Indexed
     * triangles may never land in a buffer that still holds quads, points
     * or lines: the flush would emit, say, a QUADS record whose vertex
     * count is not a multiple of four and the service would reject the
     * whole commit. Vertices are appended one per call from an empty
     * (multiple-of-three) buffer, so the original capacity test flushes
     * at exactly MAX_SCREEN_VERTICES and stays a legal triangle count. */
    if (!context->screenVertexCount ||
        context->screenVertexCount >= MAX_SCREEN_VERTICES ||
        context->screenCommand != RADEON3D_EXEC_DRAW_TRIANGLES) {
        if (context->screenVertexCount &&
            !FlushScreenVertices(context))
            return FALSE;
        context->screenHardwareTransform = TRUE;
        context->screenProjective = FALSE;
        if (!PrepareScreenAppend(context, RADEON3D_EXEC_DRAW_TRIANGLES)) {
            context->screenHardwareTransform = FALSE;
            return FALSE;
        }
    }
    CopyHardwareVertex(context, vertex,
                       &context->screenVertices[context->screenVertexCount++],
                       hardwareSphereMap, softwareSphereMap);
    return TRUE;
}

void Lib_glDrawElements(GLenum mode, GLsizei count, GLenum type,
                        const GLvoid *indices)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG index;
    GLboolean useCache = GL_TRUE;
    if (!OutsideBeginAllowed(context)) return;
    if (count < 0) { SetError(context, GL_INVALID_VALUE); return; }
    if (!count) return;
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT &&
        type != GL_UNSIGNED_INT) { SetError(context, GL_INVALID_ENUM); return; }
    if (!indices || !context->vertexArray.enabled || (ULONG)count > MAX_VERTICES) {
        SetError(context, GL_INVALID_OPERATION); return;
    }
    /* Alias meshes reference each vertex many times: gather every unique
     * index exactly once into a generation-stamped cache. The cache is a
     * fixed 64K-entry table allocated at context creation covering the
     * whole BYTE/SHORT index space, so there is no per-draw max-index
     * scan; UINT-indexed draws (and cache-less contexts) fetch directly. */
    ULONG drawStart = context->perfEnabled ? PerfClock() : 0;
    useCache = type != GL_UNSIGNED_INT && context->indexCacheVerts != NULL;
    if (useCache) {
        if (++context->indexCacheGeneration == 0UL)
            context->indexCacheGeneration = 1UL;
    }
    {
        /* The common indexed case (triangle lists, fill polygon mode) appends
         * resolved vertices straight into the hardware-TCL staging buffer;
         * the expanded array and its second full pass over vertex data are
         * only needed by the decomposed paths (points sizing, polygon-mode
         * line/point emission, flat fans). */
        ULONG hardwareSphereMap = HardwareSphereMapMask(context);
        ULONG softwareSphereMap = SoftwareSphereMapMask(context,
                                                        hardwareSphereMap);
        GLboolean fast = mode == GL_TRIANGLES &&
                         context->polygonMode != GL_POINT &&
                         context->polygonMode != GL_LINE;
        if (fast && softwareSphereMap && !context->normalMatrixValid)
            BuildNormalMatrix(context);
        if (context->perfEnabled && fast) {
            context->perf.primitives++;
            context->perf.inputVertices += (ULONG)count;
        }
        for (index=0; index<(ULONG)count; ++index) {
            ULONG value = ReadIndex(indices,type,index);
            const struct Vertex *source;
            struct Vertex resolved;
            if (useCache && context->indexCacheStamp[value] ==
                            context->indexCacheGeneration) {
                source = &context->indexCacheVerts[value];
            } else {
                source = useCache ? &context->indexCacheVerts[value]
                                  : &resolved;
                /* Fetch straight into the cache slot: misses fill the
                 * entry once and later references alias it. */
                if (!FetchDrawVertex(context, value,
                                     (struct Vertex *)source)) {
                    SetError(context, GL_INVALID_OPERATION); return;
                }
                if (context->perfEnabled)
                    context->perf.vertexFetches++;
                if (useCache)
                    context->indexCacheStamp[value] =
                        context->indexCacheGeneration;
            }
            if (fast) {
                if (!AppendResolvedTclVertex(context, source,
                                             hardwareSphereMap,
                                             softwareSphereMap)) {
                    SetError(context, GL_INVALID_ENUM);
                    return;
                }
                continue;
            }
            context->vertices[index] = *source;
        }
        if (!fast &&
            !EmitPrimitive(context, context->vertices, (ULONG)count, mode))
            SetError(context, GL_INVALID_ENUM);
        if (context->perfEnabled)
            context->perf.drawTicks += PerfClock() - drawStart;
    }
}

static void ClearFastPath(MGLFastPath *fp)
{
    ULONG callerSize = fp->size;
    UBYTE *bytes = (UBYTE *)fp;
    ULONG index;
    for (index = 0; index < sizeof(*fp); ++index)
        bytes[index] = 0;
    fp->size = callerSize;
    fp->version = MGL_FASTPATH_VERSION;
}

ULONG Lib_mglGetFastPathCapabilities(void)
{
    return MGL_FASTPATH_POSITION | MGL_FASTPATH_COLOR |
           MGL_FASTPATH_TEXCOORD0 | MGL_FASTPATH_TEXCOORD1 |
           MGL_FASTPATH_TEXCOORD_Q;
}

GLboolean Lib_mglBeginFastPath(GLenum primitive, ULONG attributes,
                               MGLFastPath *fp)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct FastVertex layout;
    UBYTE *base = (UBYTE *)&layout;
    ULONG supported = Lib_mglGetFastPathCapabilities();
    ULONG none = MGL_FASTPATH_OFFSET_NONE;

    if (!context) return GL_FALSE;
    if (!fp || fp->size < sizeof(*fp)) {
        SetError(context, GL_INVALID_VALUE);
        return GL_FALSE;
    }
    if (context->insideBegin || context->activeFastPath) {
        SetError(context, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    if (context->lightingEnabled || HardwareSphereMapMask(context)) {
        /* FastPath v2 vertices carry no normals; lit draws and hardware
         * sphere-mapped draws both feed object-space normals to the TCL
         * record, so they must go through the immediate/array paths. */
        SetError(context, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    if (!ValidPrimitive(primitive)) {
        SetError(context, GL_INVALID_ENUM);
        return GL_FALSE;
    }
    if (!(attributes & MGL_FASTPATH_POSITION)) {
        SetError(context, GL_INVALID_VALUE);
        return GL_FALSE;
    }
    if (attributes & ~supported) {
        SetError(context, GL_INVALID_ENUM);
        return GL_FALSE;
    }
    if (!context->fastBuffer) {
        context->fastBufferBytes = MAX_VERTICES * sizeof(struct FastVertex);
        context->fastBuffer = AllocMem(context->fastBufferBytes,
                                       MEMF_PUBLIC | MEMF_CLEAR);
        if (!context->fastBuffer) {
            context->fastBufferBytes = 0;
            SetError(context, GL_OUT_OF_MEMORY);
            return GL_FALSE;
        }
    }

    ClearFastPath(fp);
    fp->requestedAttributes = attributes;
    fp->supportedAttributes = supported;
    fp->flags = MGL_FASTPATH_FLAG_DIRECT_BUFFER;
    fp->primitive = primitive;
    fp->vertexBuffer = context->fastBuffer;
    fp->vertexStride = sizeof(struct FastVertex);
    fp->vertexCapacity = MAX_VERTICES;
    fp->firstVertex = 0;
    fp->offsetX = (ULONG)((UBYTE *)&layout.x - base);
    fp->offsetY = (ULONG)((UBYTE *)&layout.y - base);
    fp->offsetZ = (ULONG)((UBYTE *)&layout.z - base);
    fp->offsetW = (ULONG)((UBYTE *)&layout.w - base);
    fp->offsetR = (attributes & MGL_FASTPATH_COLOR)
                      ? (ULONG)((UBYTE *)&layout.r - base) : none;
    fp->offsetG = (attributes & MGL_FASTPATH_COLOR)
                      ? (ULONG)((UBYTE *)&layout.g - base) : none;
    fp->offsetB = (attributes & MGL_FASTPATH_COLOR)
                      ? (ULONG)((UBYTE *)&layout.b - base) : none;
    fp->offsetA = (attributes & MGL_FASTPATH_COLOR)
                      ? (ULONG)((UBYTE *)&layout.a - base) : none;
    fp->offsetU0 = (attributes & MGL_FASTPATH_TEXCOORD0)
                       ? (ULONG)((UBYTE *)&layout.u0 - base) : none;
    fp->offsetV0 = (attributes & MGL_FASTPATH_TEXCOORD0)
                       ? (ULONG)((UBYTE *)&layout.v0 - base) : none;
    fp->offsetU1 = (attributes & MGL_FASTPATH_TEXCOORD1)
                       ? (ULONG)((UBYTE *)&layout.u1 - base) : none;
    fp->offsetV1 = (attributes & MGL_FASTPATH_TEXCOORD1)
                       ? (ULONG)((UBYTE *)&layout.v1 - base) : none;
    fp->offsetQ = (attributes & MGL_FASTPATH_TEXCOORD_Q)
                      ? (ULONG)((UBYTE *)&layout.q - base) : none;
    fp->offsetNormalIndex = none;
    fp->offsetNormalX = fp->offsetNormalY = fp->offsetNormalZ = none;
    fp->currentColor[0] = ((context->currentColor >> 16) & 255) / 255.0f;
    fp->currentColor[1] = ((context->currentColor >> 8) & 255) / 255.0f;
    fp->currentColor[2] = (context->currentColor & 255) / 255.0f;
    fp->currentColor[3] = ((context->currentColor >> 24) & 255) / 255.0f;
    fp->currentTex0[0] = context->currentS[0];
    fp->currentTex0[1] = context->currentT[0];
    fp->currentTex1[0] = context->currentS[1];
    fp->currentTex1[1] = context->currentT[1];
    fp->currentQ = 1.0f;
    fp->privateData = context;
    context->activeFastPath = fp;
    context->insideBegin = TRUE;
    context->primitive = primitive;
    mglFastPathRefreshPointers(fp);
    return GL_TRUE;
}

void Lib_mglEndFastPath(MGLFastPath *fp)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG attributes, count, index;
    BOOL valid = TRUE;

    if (!context) return;
    if (!fp || fp != context->activeFastPath ||
        fp->version != MGL_FASTPATH_VERSION || fp->privateData != context) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    attributes = fp->requestedAttributes;
    count = fp->vertexCount;
    if (count > fp->vertexCapacity || count > MAX_VERTICES) {
        count = MAX_VERTICES;
        SetError(context, GL_INVALID_OPERATION);
        valid = FALSE;
    }
    for (index = 0; valid && index < count; ++index) {
        const struct FastVertex *input =
            (const struct FastVertex *)(context->fastBuffer +
                                       index * sizeof(struct FastVertex));
        struct Vertex *output = &context->vertices[index];
        GLfloat q = (attributes & MGL_FASTPATH_TEXCOORD_Q) ? input->q : 1.0f;
        if (q == 0.0f) {
            SetError(context, GL_INVALID_VALUE);
            valid = FALSE;
            break;
        }
        output->x = input->x; output->y = input->y;
        output->z = input->z; output->w = input->w;
        output->nx = context->currentNormalX;
        output->ny = context->currentNormalY;
        output->nz = context->currentNormalZ;
        output->color = (attributes & MGL_FASTPATH_COLOR)
                            ? PackColor(input->r, input->g, input->b, input->a)
                            : context->currentColor;
        output->s = (attributes & MGL_FASTPATH_TEXCOORD0)
                        ? input->u0 / q : context->currentS[0];
        output->t = (attributes & MGL_FASTPATH_TEXCOORD0)
                        ? input->v0 / q : context->currentT[0];
        output->s1 = (attributes & MGL_FASTPATH_TEXCOORD1)
                         ? input->u1 / q : context->currentS[1];
        output->t1 = (attributes & MGL_FASTPATH_TEXCOORD1)
                         ? input->v1 / q : context->currentT[1];
        output->fog = FogFactor(context, output->x, output->y,
                                output->z, output->w);
    }
    context->insideBegin = FALSE;
    context->activeFastPath = NULL;
    fp->privateData = NULL;
    fp->currentVertex = NULL;
    if (valid && !EmitPrimitive(context, context->vertices, count,
                                fp->primitive))
        SetError(context, GL_INVALID_OPERATION);
}
