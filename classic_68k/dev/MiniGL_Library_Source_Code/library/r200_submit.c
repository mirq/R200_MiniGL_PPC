#include "minigl_r200_internal.h"
#include "debug_output.h"

struct SubmitSlot *BuildSubmitSlot(
    struct MiniGLR200Context *context)
{
    return &context->submitSlots[context->buildSlot];
}

/* Persistent, fixed-size event records survive a hard reboot and avoid the
 * formatting/runtime dependencies that caused trouble in this freestanding
 * library. This is intentionally low-volume: segment lifecycle and failures
 * only, never the normal per-draw path. */
void StreamDiagEvent(struct MiniGLR200Context *context, ULONG event,
                     ULONG argument0, ULONG argument1)
{
#if R200_STREAM_DIAG
    extern struct DosLibrary *DOSBase;
    static ULONG sequence;
    BPTR file;
    ULONG record[12];

    if (!DOSBase)
        return;
    file = Open((STRPTR)"Work:r200_streamdiag.bin", MODE_OLDFILE);
    if (file)
        (void)Seek(file, 0, OFFSET_END);
    else
        file = Open((STRPTR)"Work:r200_streamdiag.bin", MODE_NEWFILE);
    if (!file)
        return;
    record[0] = 0x52325344UL; /* 'R2SD' */
    record[1] = ++sequence;
    record[2] = event;
    record[3] = context ? context->buildSlot : 0xffffffffUL;
    record[4] = context ? context->submitSlots[0].segmentCursor : 0;
    record[5] = context ? context->submitSlots[1].segmentCursor : 0;
    record[6] = context && context->segment.valid
                    ? context->segment.info.Id : 0xffffffffUL;
    record[7] = context && context->segment.valid
                    ? context->segment.info.Bytes : 0;
    record[8] = context && context->segment.valid
                    ? (ULONG)context->segment.info.GpuAddress : 0;
    record[9] = argument0;
    record[10] = argument1;
    record[11] = context ? context->deviceEpoch : 0;
    (void)Write(file, record, sizeof(record));
    Close(file);
#else
    (void)context;
    (void)event;
    (void)argument0;
    (void)argument1;
#endif
}

void ResetSubmitSlot(struct SubmitSlot *slot)
{
    slot->recordCount = 0;
    slot->generatedDwordCount = 0;
    slot->queuedTextureCount = 0;
    slot->lastDrawOffset = 0;
    slot->lastDrawHeaderDwords = 0;
    slot->fence = 0;
    slot->deviceEpoch = 0;
    slot->lastDrawValid = FALSE;
    slot->inFlight = FALSE;
    slot->commitMode = FALSE;
    slot->modeDecided = FALSE;
    slot->stateBatch = FALSE;
    slot->drawCount = 0;
    /* segmentCursor deliberately survives the reset: the RV280 fetch
     * caches never re-read rewritten lines at the same address, so vertex
     * data must only ever be written at fresh segment offsets. */
}

BOOL EnsureStreamSegment(struct MiniGLR200Context *context)
{
    if (context->segment.valid)
        return TRUE;
    context->segment.info.Size = sizeof(context->segment.info);
    if (!Radeon3DAllocSegment(context->device, SEGMENT_BYTES,
                              &context->segment.info) ||
        context->segment.info.Version != RADEON3D_SEGMENT_VERSION) {
        StreamDiagEvent(context, 2UL, context->segment.info.Version, 0);
        return FALSE;
    }
    context->segment.valid = TRUE;
    StreamDiagEvent(context, 1UL, context->segment.info.Id,
                    (ULONG)context->segment.info.GpuAddress);
    return TRUE;
}

void ReleaseStreamSegment(struct MiniGLR200Context *context)
{
    ULONG index;
    if (context->segment.valid) {
        (void)Radeon3DFreeSegment(context->device,
                                  context->segment.info.Id);
        context->segment.valid = FALSE;
    }
    /* Teardown: retire parked segments. Each carries the fence of its last
     * commit; wait briefly for stragglers so the service never validates a
     * CommitBatch against an id that is already back in its pool. */
    for (index = 0; index < context->deferredSegmentCount; ++index) {
        struct DeferredSegment *entry = &context->deferredSegments[index];
        if (entry->deviceEpoch == context->deviceEpoch &&
            !Radeon3DTestFence(context->device, entry->fence))
            (void)Radeon3DWaitFence(context->device, entry->fence, 1000UL);
        (void)Radeon3DFreeSegment(context->device, entry->segmentId);
    }
    context->deferredSegmentCount = 0;
}

/* Return every parked segment whose fence has retired (or whose epoch died
 * with a recovered device). Cheap enough to call from present and from the
 * rotation path itself. */
void ReapDeferredSegments(struct MiniGLR200Context *context)
{
#if R200_ASYNC_ROTATION
    ULONG index, kept;
    if (!context->deferredSegmentCount)
        return;
    for (index = 0, kept = 0; index < context->deferredSegmentCount;
         ++index) {
        struct DeferredSegment *entry = &context->deferredSegments[index];
        if (entry->deviceEpoch != context->deviceEpoch ||
            Radeon3DTestFence(context->device, entry->fence)) {
            (void)Radeon3DFreeSegment(context->device, entry->segmentId);
        } else {
            context->deferredSegments[kept++] = *entry;
        }
    }
    context->deferredSegmentCount = kept;
#else
    (void)context;
#endif
}

BOOL SegmentHalfFits(struct MiniGLR200Context *context,
                     const struct SubmitSlot *slot, ULONG vertexBytes)
{
    (void)context;
    return slot->segmentCursor + vertexBytes <= SEGMENT_HALF_BYTES;
}

/*
 * Move to a freshly allocated segment once the build half cannot hold the
 * next draw.
 *
 * Streaming commits place vertices in the build slot's segment half, and
 * the RV280 fetch caches make rewriting recently fetched lines unsafe, so
 * each half fills monotonically. The pending commit-mode records still name
 * offsets inside the current segment and a CommitBatch carries exactly one
 * SegmentId, so they are submitted BEFORE the switch; after Submit at most
 * one slot is in flight (it drains its sibling before flipping) and that
 * slot's fence is the only reader guard the old segment needs.
 *
 * The new segment is allocated BEFORE the old one is released. The service
 * hands out the lowest free slot index, so holding the old allocation
 * forces the replacement onto a different pool slot - a different VRAM
 * region - instead of silently recycling the same addresses. With async
 * rotation (R200_ASYNC_ROTATION) the old segment is then parked with its
 * fence until ReapDeferredSegments returns it to the pool, so no mid-frame
 * WaitAllSubmissions stall occurs; when the park ring is full the rotation
 * falls back to retiring that single fence inline. Nothing else references
 * the old segment after the switch: record chains are empty, cursors reset,
 * and parked entries keep their own id/fence pairs.
 */
BOOL RotateStreamSegment(struct MiniGLR200Context *context,
                         ULONG vertexBytes)
{
#if R200_PPC_FRONTEND
    /* Pool rotation across the CPU boundary: land the records that
     * reference the current segment, then switch to the next host-leased
     * segment whose GPU reads have retired (platform-checked via ring
     * completion). On failure the flush degrades to the inline path. */
    StreamDiagEvent(context, 0x10UL, vertexBytes,
                    BuildSubmitSlot(context)->recordCount);
    if (!Submit(context)) {
        StreamDiagEvent(context, 0x11UL, vertexBytes, 0);
        return FALSE;
    }
    if (!R200PPCPlatformRotateSegment(context, vertexBytes)) {
        StreamDiagEvent(context, 0x14UL, 0, 0);
        return FALSE;
    }
    StreamDiagEvent(context, 0x15UL, context->segment.info.Id,
                    (ULONG)context->segment.info.GpuAddress);
    return TRUE;
#else
    struct Radeon3DSegment next;
    struct SubmitSlot *flown = NULL;
    ULONG index;

    StreamDiagEvent(context, 0x10UL, vertexBytes,
                    BuildSubmitSlot(context)->recordCount);
    if (!Submit(context)) {
        StreamDiagEvent(context, 0x11UL, vertexBytes, 0);
        return FALSE;
    }
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index) {
        if (context->submitSlots[index].inFlight) {
            flown = &context->submitSlots[index];
            break;
        }
    }
    next.Size = sizeof(next);
    if (!Radeon3DAllocSegment(context->device, SEGMENT_BYTES, &next) ||
        next.Version != RADEON3D_SEGMENT_VERSION) {
        StreamDiagEvent(context, 0x14UL, next.Version, 0);
        /* Keep the old segment valid and active: this flush degrades to
         * the inline path and smaller draws may still fit. */
        return FALSE;
    }
    StreamDiagEvent(context, 0x15UL, next.Id, (ULONG)next.GpuAddress);
#if R200_ASYNC_ROTATION
    ReapDeferredSegments(context);
#endif
    if (!flown) {
        /* Nothing outstanding: the old segment has no readers left. */
        (void)Radeon3DFreeSegment(context->device,
                                  context->segment.info.Id);
    } else if (flown->deviceEpoch != context->deviceEpoch) {
        /* A recovery raced the submit: the old segment died with its
         * epoch, nothing to free. */
    } else {
#if R200_ASYNC_ROTATION
        if (context->deferredSegmentCount < R200_DEFERRED_SEGMENT_MAX) {
            struct DeferredSegment *entry =
                &context->deferredSegments[context->deferredSegmentCount++];
            entry->segmentId = context->segment.info.Id;
            entry->fence = flown->fence;
            entry->deviceEpoch = context->deviceEpoch;
        } else
#endif
        {
            /* Ring full (or async disabled): retire this one fence now. */
            if (!WaitSubmitSlot(context, flown, SUBMIT_WAIT_DRAIN)) {
                /* Recovery dropped every segment of the dead epoch,
                 * including the freshly allocated one. */
                StreamDiagEvent(context, 0x12UL, vertexBytes, 0);
                return FALSE;
            }
            (void)Radeon3DFreeSegment(context->device,
                                      context->segment.info.Id);
        }
    }
    context->segment.info = next;
    context->segment.valid = TRUE;
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index)
        context->submitSlots[index].segmentCursor = 0;
    StreamDiagEvent(context, 0x16UL, next.Id, (ULONG)next.GpuAddress);
    return TRUE;
#endif
}

void InvalidateSubmissionState(struct MiniGLR200Context *context)
{
    ULONG index;
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index) {
        ResetSubmitSlot(&context->submitSlots[index]);
        /* The next epoch allocates a fresh segment; its write cursors
         * start at zero again (different GpuAddress region, so the
         * fetch-cache constraint is honoured by the reallocation). */
        context->submitSlots[index].segmentCursor = 0;
    }
    context->buildSlot = 0;
    context->completedFence = 0;
    /* Called with the device already closed during recovery: the segment
     * died with it and must be re-created for the next epoch. Parked
     * segments died with it too -- drop the entries WITHOUT freeing, the
     * ids mean nothing to the new device. */
    context->segment.valid = FALSE;
    context->deferredSegmentCount = 0;
    if (++context->batchSerial == 0)
        context->batchSerial = 1;
    for (index = 0; index < MAX_TEXTURES; ++index) {
        context->textures[index].queuedBatchSerial = 0;
        context->textures[index].lastUseFence = 0;
        context->textures[index].lastUseDeviceEpoch = 0;
    }
}

BOOL TextureQueued(const struct MiniGLR200Context *context,
                   const struct TextureObject *texture)
{
    const struct SubmitSlot *slot =
        &context->submitSlots[context->buildSlot];
    return slot->recordCount &&
           texture->queuedBatchSerial == context->batchSerial;
}

void MarkTextureQueued(struct MiniGLR200Context *context, GLuint name)
{
    struct SubmitSlot *slot = BuildSubmitSlot(context);
    struct TextureObject *texture;
    if (name >= MAX_TEXTURES)
        return;
    texture = &context->textures[name];
    if (texture->queuedBatchSerial != context->batchSerial) {
        texture->queuedBatchSerial = context->batchSerial;
        if (slot->queuedTextureCount < MAX_TEXTURES)
            slot->queuedTextureNames[slot->queuedTextureCount++] =
                (UWORD)name;
    }
    if (++context->textureUseSerial == 0)
        context->textureUseSerial = 1;
    texture->lastUseSerial = context->textureUseSerial;
}

BOOL RecoverDevice(struct MiniGLR200Context *context)
{
    struct Radeon3DInfo info;
    struct Radeon3DDevice *device;
    ULONG index, targetCount=context->fullscreen?2UL:1UL;

    Radeon3DClose(context->device);
    context->device=NULL;
    InvalidateSubmissionState(context);
    /* Every captured surface handle belongs to the old device. Submitted
     * work is already lost on recovery, so do not retain an unflushable
     * pending snapshot or its vertices across the new device epoch. */
    context->screenVertexCount = 0;
    context->screenCommand = RADEON3D_EXEC_DRAW_TRIANGLES;
    context->pendingDrawValid = FALSE;
    context->pendingSemanticStateSerial = 0;
    for (index=0;index<2;++index)
        ZeroMemory(&context->surfaces[index],sizeof(context->surfaces[index]));
    ZeroMemory(&context->depthSurface,sizeof(context->depthSurface));
    for (index=0;index<MAX_TEXTURES;++index) {
        struct TextureObject *texture=&context->textures[index];
        if (texture->bitmap)
            p96FreeBitMap(texture->bitmap);
        texture->bitmap=NULL; texture->resident=FALSE;
        texture->residentGeneration=0;
        texture->residentDeviceEpoch=0;
        texture->surfaceGeneration=0;
        texture->lastUseFence=0;
        texture->lastUseDeviceEpoch=0;
        ZeroMemory(&texture->surface,sizeof(texture->surface));
    }
    info.Size=sizeof(info);
    device=Radeon3DOpen(RADEON3D_IFACE_VERSION,&info);
    if (!device || (info.Caps&REQUIRED_CAPS)!=REQUIRED_CAPS ||
        (context->pixelDepth != 16UL &&
         !(info.Caps & RADEON3D_CAP_COLOR_TARGET_FORMATS))) {
        if (device)
            Radeon3DClose(device);
        return FALSE;
    }
    context->device=device;
    context->radeonGeneration = info.Generation;
    if (++context->deviceEpoch == 0)
        context->deviceEpoch = 1;
    context->batchDwords = info.MaxBatchDwords;
    if (!context->batchDwords || context->batchDwords > MAX_RECORD_DWORDS)
        context->batchDwords = MAX_RECORD_DWORDS;
    BumpSemanticStateSerial(context);
    for (index=0;index<targetCount;++index) {
        struct BitMap *bitmap=context->fullscreen
            ? context->screenBuffers[index]->sb_BitMap : context->bitmap;
        context->surfaces[index].Size=sizeof(context->surfaces[index]);
        if (!Radeon3DImportBitMap(device,bitmap,&context->surfaces[index]) ||
            context->surfaces[index].Format!=context->colorFormat ||
            context->surfaces[index].Width<context->width ||
            context->surfaces[index].Height<context->height)
            goto fail;
    }
    context->radeonCaps = info.Caps;
    context->depthSurface.Size=sizeof(context->depthSurface);
    /* A depth bitmap is re-imported into the new device; service-allocated
     * depth (no bitmap) died with the old device and is allocated again. */
    if (context->depthBitmap) {
        if (!Radeon3DImportBitMap(device,context->depthBitmap,
                                  &context->depthSurface))
            goto fail;
    } else if (!Radeon3DAllocSurface(device,context->width,context->height,
                                     RADEON3D_FORMAT_R5G6B5PC,
                                     &context->depthSurface)) {
        goto fail;
    }
    if (context->depthSurface.Format!=RADEON3D_FORMAT_R5G6B5PC ||
        context->depthSurface.Width<context->width ||
        context->depthSurface.Height<context->height)
        goto fail;
    context->streamSegments =
        (info.Caps & RADEON3D_CAP_STREAM_SEGMENTS) != 0;
    if (context->streamSegments && !EnsureStreamSegment(context))
        context->streamSegments = FALSE;
    return TRUE;
fail:
    Radeon3DClose(device);
    context->device=NULL;
    return FALSE;
}

BOOL WaitSubmitSlot(struct MiniGLR200Context *context,
                    struct SubmitSlot *slot,
                    enum SubmitWaitReason reason)
{
#if R200_PPC_FRONTEND
    ULONG slotIndex = (ULONG)(slot - context->submitSlots);
    (void)reason;
    if (!slot->inFlight)
        return TRUE;
    if (!R200PPCPlatformWaitSlot(context, slotIndex)) {
        ResetSubmitSlot(slot);
        return FALSE;
    }
    ResetSubmitSlot(slot);
    return TRUE;
#else
    ULONG start = 0;

    if (!slot->inFlight)
        return TRUE;
    if (slot->deviceEpoch != context->deviceEpoch) {
        ResetSubmitSlot(slot);
        return TRUE;
    }
    if (Radeon3DTestFence(context->device, slot->fence)) {
        context->completedFence = slot->fence;
        if (context->perfEnabled)
            context->perf.fencePollHits++;
        ResetSubmitSlot(slot);
        return TRUE;
    }
    if (context->perfEnabled) {
        context->perf.fenceWaits++;
        if (reason == SUBMIT_WAIT_BUFFER)
            context->perf.bufferWaits++;
        else if (reason == SUBMIT_WAIT_TEXTURE)
            context->perf.textureWaits++;
        else
            context->perf.drainWaits++;
        start = PerfClock();
    }
    if (Radeon3DWaitFence(context->device, slot->fence, 1000UL)) {
        context->completedFence = slot->fence;
        if (context->perfEnabled)
            context->perf.waitTicks += PerfClock() - start;
        ResetSubmitSlot(slot);
        return TRUE;
    }
    if (context->perfEnabled) {
        context->perf.waitTicks += PerfClock() - start;
        context->perf.waitFailures++;
    }
    StreamDiagEvent(context, 0x31UL,
                    (ULONG)(slot - context->submitSlots), slot->fence);
    (void)RecoverDevice(context);
    return FALSE;
#endif
}

BOOL WaitAllSubmissions(struct MiniGLR200Context *context)
{
    ULONG index;
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index)
        if (!WaitSubmitSlot(context, &context->submitSlots[index],
                            SUBMIT_WAIT_DRAIN))
            return FALSE;
    return TRUE;
}


#if R200_DUMP_FIRST_BATCHES
/* Temporary diagnostics: dump the first two submit batches (records and,
 * for streaming commits, the segment vertex bytes) so the first-batch
 * loss can be attributed to the library, the service, or the card. */
static void DumpBatchFile(struct MiniGLR200Context *context,
                          struct SubmitSlot *slot, ULONG batchIndex)
{
    extern struct DosLibrary *DOSBase;
    static const char *paths[8] = { "T:r200_batch1.bin", "T:r200_batch2.bin",
        "T:r200_batch3.bin", "T:r200_batch4.bin", "T:r200_batch5.bin",
        "T:r200_batch6.bin", "T:r200_batch7.bin", "T:r200_batch8.bin" };
    BPTR file;
    ULONG header[6];

    if (batchIndex < 1UL || batchIndex > 8UL || !DOSBase || !slot)
        return;
    file = Open((STRPTR)paths[batchIndex - 1UL], MODE_NEWFILE);
    if (!file)
        return;
    header[0] = 0x52324231UL; /* 'R2B1' */
    header[1] = batchIndex;
    header[2] = slot->recordCount;
    header[3] = slot->commitMode;
    header[4] = slot->segmentCursor;
    header[5] = slot->drawCount;
    (void)Write(file, header, sizeof(header));
    if (slot->recordCount)
        (void)Write(file, slot->records, slot->recordCount * sizeof(ULONG));
    if (slot->commitMode && slot->segmentCursor && context->segment.valid)
        (void)Write(file, (APTR)((ULONG)context->segment.info.CpuAddress +
                                 context->buildSlot * SEGMENT_HALF_BYTES),
                    slot->segmentCursor);
    Close(file);
}
#endif

/* Temporary diagnostics: capture every rejected commit batch (records,
 * offset table, submit-slot scalars and segment geometry) so a service
 * stage rejection can be attributed offline. Rotates over four files. */
static void DumpFailBatch(struct MiniGLR200Context *context,
                          struct SubmitSlot *slot)
{
    extern struct DosLibrary *DOSBase;
    static const char *paths[4] = { "T:r200_failbatch1.bin",
        "T:r200_failbatch2.bin", "T:r200_failbatch3.bin",
        "T:r200_failbatch4.bin" };
    static ULONG failDumpCount;
    BPTR file;
    ULONG header[12];
    ULONG batchIndex;

    ++failDumpCount;
    batchIndex = ((failDumpCount - 1UL) & 3UL);
    if (!DOSBase || !slot)
        return;
    file = Open((STRPTR)paths[batchIndex], MODE_NEWFILE);
    if (!file)
        return;
    header[0] = 0x52324631UL; /* 'R2F1' */
    header[1] = failDumpCount;
    header[2] = slot->recordCount;
    header[3] = slot->drawCount;
    header[4] = slot->segmentCursor;
    header[5] = context->buildSlot;
    header[6] = context->segment.info.Id;
    header[7] = context->segment.info.Bytes;
    header[8] = (ULONG)context->segment.info.GpuAddress;
    header[9] = slot->lastDrawOffset;
    header[10] = slot->lastDrawHeaderDwords;
    header[11] = SEGMENT_HALF_BYTES;
    (void)Write(file, header, sizeof(header));
    if (slot->recordCount)
        (void)Write(file, slot->records, slot->recordCount * sizeof(ULONG));
    if (slot->drawCount)
        (void)Write(file, slot->vertexOffsets,
                    slot->drawCount * sizeof(ULONG));
    Close(file);
}

BOOL Submit(struct MiniGLR200Context *context)
{
#if R200_PPC_FRONTEND
    struct SubmitSlot *slot = BuildSubmitSlot(context);
    struct SubmitSlot *next;
    ULONG currentIndex = context->buildSlot;
    ULONG nextIndex;

    if (!slot->recordCount)
        return TRUE;
    nextIndex = (currentIndex + 1UL) % SUBMIT_SLOT_COUNT;
    next = &context->submitSlots[nextIndex];
    if (!WaitSubmitSlot(context, next, SUBMIT_WAIT_BUFFER))
        return FALSE;
    ResetSubmitSlot(next);
    if (!R200PPCPlatformPublishSlot(context, currentIndex)) {
        ResetSubmitSlot(slot);
        return FALSE;
    }
    slot->inFlight = TRUE;
    slot->lastDrawValid = FALSE;
    if (++context->batchSerial == 0)
        context->batchSerial = 1;
    context->buildSlot = nextIndex;
    return TRUE;
#else
    struct SubmitSlot *slot = BuildSubmitSlot(context);
    struct SubmitSlot *next;
    ULONG fence = 0, index, nextIndex;
    ULONG start = 0;

    if (!slot->recordCount)
        return TRUE;
    /* Radeon3D's semantic Execute path has one hardware command stream. Keep
     * one fence outstanding while the CPU fills the other immutable record
     * slot, then retire that fence before translating/submitting the next
     * slot. This provides CPU/GPU overlap without the service-side staging
     * alias that occurs when two Execute calls are outstanding at once. */
    nextIndex = (context->buildSlot + 1UL) % SUBMIT_SLOT_COUNT;
    next = &context->submitSlots[nextIndex];
    if (!WaitSubmitSlot(context, next, SUBMIT_WAIT_BUFFER))
        return FALSE;
    ResetSubmitSlot(next);
    if (context->perfEnabled) {
        context->perf.submits++;
        context->perf.asyncSubmits++;
        context->perf.recordDwords += slot->recordCount;
        context->perf.generatedDwords += slot->generatedDwordCount;
        start = PerfClock();
    }
    if (slot->commitMode && context->streamSegments &&
        context->segment.valid) {
        /* Streaming path: headers only in the record chain, vertices
         * already resident in this slot's segment half. One ring submit
         * carries every draw; the service re-emits only state that
         * changed between them. */
        struct Radeon3DCommitBatch batch;

        if (slot->stateBatch) {
            struct Radeon3DStateBatch stateBatch;

            stateBatch.Size = sizeof(stateBatch);
            stateBatch.Version = RADEON3D_STATE_BATCH_VERSION;
            stateBatch.Generation = context->radeonGeneration;
            stateBatch.SegmentId = context->segment.info.Id;
            stateBatch.Primitive = slot->records[0];
            stateBatch.Header = slot->records;
            stateBatch.HeaderDwords = slot->recordCount;
            stateBatch.Draws = slot->stateBatchDraws;
            stateBatch.DrawCount = slot->drawCount;
            stateBatch.Flags = RADEON3D_SUBMIT_FENCE;
            if (Radeon3DCommitStateBatch(context->device, &stateBatch,
                                         &fence))
                goto submitted;
            {
                struct Radeon3DInfo info;

                dbg_puts("[minigl] state batch fail fence=");
                dbg_hex32(fence);
                dbg_puts(" rec=");
                dbg_u32(slot->recordCount);
                dbg_puts(" draws=");
                dbg_u32(slot->drawCount);
                dbg_puts(" op=");
                dbg_u32(slot->records[0]);
                dbg_puts(" len=");
                dbg_u32(slot->records[1]);
                dbg_puts(" generation=");
                dbg_u32(context->radeonGeneration);
                dbg_newline();
                ZeroMemory(&info, sizeof(info));
                info.Size = sizeof(info);
                if (Radeon3DGetInfo(context->device, &info)) {
                    dbg_puts("[minigl] state batch stage=");
                    dbg_u32(info.CommitFailStage);
                    dbg_puts(" service_generation=");
                    dbg_u32(info.Generation);
                    dbg_newline();
                }
            }
            if (context->perfEnabled) {
                context->perf.executeTicks += PerfClock() - start;
                context->perf.executeFailures++;
            }
            ResetSubmitSlot(slot);
            (void)RecoverDevice(context);
            return FALSE;
        }

        batch.Size = sizeof(batch);
        batch.Version = RADEON3D_COMMIT_BATCH_VERSION;
        batch.SegmentId = context->segment.info.Id;
        batch.Records = slot->records;
        batch.RecordDwords = slot->recordCount;
        batch.VertexOffsets = slot->vertexOffsets;
        batch.RecordCount = slot->drawCount;
        batch.Flags = RADEON3D_SUBMIT_FENCE;
        if (!Radeon3DCommitBatch(context->device, &batch, &fence)) {
            struct Radeon3DInfo info;

            if (context->perfEnabled) {
                context->perf.executeTicks += PerfClock() - start;
                context->perf.executeFailures++;
            }
            /* The service encodes its reject stage in the fence slot as
             * well, for the case where the info block is too old to carry
             * one. Label the two sources apart. */
            dbg_puts("[minigl] commit fail fencestage=");
            dbg_hex32(fence);
            dbg_puts(" rec=");
            dbg_u32(slot->recordCount);
            dbg_puts(" draws=");
            dbg_u32(slot->drawCount);
            dbg_puts(" slot=");
            dbg_u32(context->buildSlot);
            dbg_puts(" cursor=");
            dbg_u32(slot->segmentCursor);
            dbg_puts(" segId=");
            dbg_u32(batch.SegmentId);
            dbg_puts(" segBytes=");
            dbg_u32(context->segment.info.Bytes);
            dbg_puts(" off0=");
            dbg_u32(slot->drawCount ? slot->vertexOffsets[0] : 0);
            dbg_newline();
            ZeroMemory(&info, sizeof(info));
            info.Size = sizeof(info);
            /* Stage zero means "nothing failed", so it must not be read
             * from a service whose info block predates the field. */
            if (Radeon3DGetInfo(context->device, &info) &&
                info.Size >= RADEON3D_INFO_V3_SIZE) {
                dbg_puts("[minigl] service stage=");
                dbg_u32(info.CommitFailStage);
                dbg_newline();
            } else {
                dbg_puts("[minigl] service stage unavailable, info size=");
                dbg_u32(info.Size);
                dbg_newline();
            }
            DumpFailBatch(context, slot);
            StreamDiagEvent(context, 0x30UL, fence, slot->recordCount);
            ResetSubmitSlot(slot);
            (void)RecoverDevice(context);
            return FALSE;
        }
    } else if (!Radeon3DExecute(context->device, slot->records,
                                slot->recordCount, RADEON3D_SUBMIT_FENCE,
                                &fence)) {
        if (context->perfEnabled) {
            context->perf.executeTicks += PerfClock() - start;
            context->perf.executeFailures++;
        }
        ResetSubmitSlot(slot);
        (void)RecoverDevice(context);
        return FALSE;
    }
submitted:
    if (context->perfEnabled) {
        context->perf.executeTicks += PerfClock() - start;
    }
#if R200_DUMP_FIRST_BATCHES
    {
        static ULONG dumpedBatches;
        ++dumpedBatches;
        if (dumpedBatches <= 8UL)
            DumpBatchFile(context, slot, dumpedBatches);
    }
#endif
    slot->fence = fence;
    slot->deviceEpoch = context->deviceEpoch;
    slot->inFlight = TRUE;
    slot->lastDrawValid = FALSE;
    for (index = 0; index < slot->queuedTextureCount; ++index) {
        struct TextureObject *texture =
            &context->textures[slot->queuedTextureNames[index]];
        texture->lastUseFence = fence;
        texture->lastUseDeviceEpoch = context->deviceEpoch;
    }
    if (++context->batchSerial == 0)
        context->batchSerial = 1;

    context->buildSlot = nextIndex;
    return TRUE;
#endif
}

BOOL SubmitAndWait(struct MiniGLR200Context *context)
{
    return Submit(context) && WaitAllSubmissions(context);
}

BOOL WaitTextureUse(struct MiniGLR200Context *context,
                    struct TextureObject *texture)
{
    ULONG index;
    if (!texture->lastUseFence ||
        texture->lastUseDeviceEpoch != context->deviceEpoch)
        return TRUE;
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index) {
        struct SubmitSlot *slot = &context->submitSlots[index];
        if (slot->inFlight && slot->fence == texture->lastUseFence &&
            slot->deviceEpoch == texture->lastUseDeviceEpoch) {
            if (!WaitSubmitSlot(context, slot, SUBMIT_WAIT_TEXTURE))
                return FALSE;
            break;
        }
    }
    texture->lastUseFence = 0;
    texture->lastUseDeviceEpoch = 0;
    return TRUE;
}

ULONG GeneratedDrawDwords(ULONG vertexCount, BOOL depth,
                          BOOL textured, BOOL textured1, BOOL fog,
                          BOOL perspective)
{
    /* Keep this in step with the register and vertex expansion in the
     * driver's EmitExecuteState/EmitExecuteVertices path. */
    ULONG vertexDwords = 3UL + ((depth || perspective) ? 1UL : 0UL) +
                          (perspective ? 1UL : 0UL) +
                          (textured ? 2UL : 0UL) +
                          (textured1 ? 2UL : 0UL) +
                         (fog ? 1UL : 0UL);

    return 48UL + (depth ? 6UL : 0UL) +
           (perspective ? 12UL : 0UL) +
           (textured ? 14UL : 0UL) +
           (textured1 ? 22UL : 0UL) + (fog ? 2UL : 0UL) +
           vertexCount * vertexDwords;
}

ULONG GeneratedVertexDwords(BOOL depth, BOOL textured, BOOL textured1,
                            BOOL fog, BOOL perspective)
{
    return 3UL + ((depth || perspective) ? 1UL : 0UL) +
           (perspective ? 1UL : 0UL) + (textured ? 2UL : 0UL) +
           (textured1 ? 2UL : 0UL) + (fog ? 1UL : 0UL);
}

ULONG GeneratedHardwareVertexDwords(BOOL textured, BOOL textured1,
                                    BOOL fog, BOOL normals)
{
    /* CP-stream cost per TCL vertex: x,y,z,w and one packed ARGB colour,
     * plus the attributes the service actually emits. Normal vertices
     * insert an XYZ triple after W. Compact records (no unit 1 / fog)
     * end at unit-0 texture coordinates; otherwise the service still
     * drops the zero tail itself, matching this count either way. */
    return 5UL + (textured ? 2UL : 0UL) +
           (textured1 ? 2UL : 0UL) + (fog ? 1UL : 0UL) +
           (normals ? 3UL : 0UL);
}

ULONG GeneratedHardwareDrawDwords(ULONG vertexCount, BOOL depth,
                                  BOOL textured, BOOL textured1, BOOL fog,
                                  BOOL normals, ULONG enabledLights)
{
    /* Keep this in step with the TCL expansion in the service: the base
     * register block (48), viewport scalars since TCL is always
     * perspective (12), point and line sizing (4), the TCL input/output
         * register group (40), guard-clip scalars (7), and the MVP upload
         * (21). Lighting adds light-model and per-light control registers
         * plus global-ambient/eye/material vector uploads (~100 fixed)
         * and roughly 40 dwords per enabled light block. */
    return 132UL + (depth ? 6UL : 0UL) +
           (textured ? 14UL : 0UL) + (textured1 ? 22UL : 0UL) +
           (fog ? 2UL : 0UL) +
           (normals ? 100UL + enabledLights * 40UL : 0UL) +
           vertexCount * GeneratedHardwareVertexDwords(textured, textured1,
                                                       fog, normals);
}

ULONG GeneratedClearDwords(BOOL depth)
{
    return 66UL + (depth ? 12UL : 0UL);
}
