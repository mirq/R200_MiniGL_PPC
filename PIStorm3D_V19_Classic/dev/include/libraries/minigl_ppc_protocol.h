#ifndef LIBRARIES_MINIGL_PPC_PROTOCOL_H
#define LIBRARIES_MINIGL_PPC_PROTOCOL_H

#include <exec/types.h>
#include <exec/ports.h>
#include <stddef.h>

#define MGLPPC_HOST_PORT_NAME "MiniGLPPC.R200.Host.1"
#define MGLPPC_PROTOCOL_MAGIC 0x4d474c50UL
#define MGLPPC_PROTOCOL_VERSION 14UL

#define MGLPPC_CMD_HELLO 1UL
#define MGLPPC_CMD_SHUTDOWN 2UL
#define MGLPPC_CMD_CREATE_CONTEXT 3UL
#define MGLPPC_CMD_CLEAR_PRESENT 4UL
#define MGLPPC_CMD_DESTROY_CONTEXT 5UL
#define MGLPPC_CMD_EXECUTE_BATCH 6UL
#define MGLPPC_CMD_GEN_TEXTURES 7UL
#define MGLPPC_CMD_DELETE_TEXTURES 8UL
#define MGLPPC_CMD_TEXTURE_UPLOAD_BEGIN 9UL
#define MGLPPC_CMD_TEXTURE_UPLOAD_CHUNK 10UL
#define MGLPPC_CMD_TEXTURE_UPLOAD_END 11UL
#define MGLPPC_CMD_GET_INTEGER 12UL
#define MGLPPC_CMD_VRAM_PROBE_BEGIN 13UL
#define MGLPPC_CMD_VRAM_PROBE_CONFIRM 14UL
#define MGLPPC_CMD_VRAM_PROBE_END 15UL
#define MGLPPC_CMD_REGISTER_SEMANTIC_SLOTS 16UL
#define MGLPPC_CMD_EXECUTE_SEMANTIC_SLOT 17UL
#define MGLPPC_CMD_SEMANTIC_BARRIER 18UL
#define MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS 19UL
#define MGLPPC_CMD_BIND_TEXTURE 20UL
#define MGLPPC_CMD_REGISTER_SEMANTIC_RING 21UL

#define MGLPPC_CAP_REPLAY_V9        (1UL << 0)
#define MGLPPC_CAP_SEMANTIC_INLINE  (1UL << 1)
#define MGLPPC_CAP_SEMANTIC_TEXTURE_V1 (1UL << 2)
/* Host verifies the slot CRC only on the first Execute of each registered
 * session, so the PPC may skip the per-submit CRC computation afterwards. */
#define MGLPPC_CAP_CRC_FIRST_ONLY   (1UL << 3)
/* Phase B: doorbell submission ring in a non-cacheable streaming segment.
 * Publishes and completion travel through shared memory with no Exec
 * message on the hot path; only lifecycle operations use messages. */
#define MGLPPC_CAP_SEMANTIC_RING    (1UL << 4)
/* The 68k host drains the Intuition port and publishes events through the
 * shared ring, avoiding a PPC->68k Exec call for every rendered frame. */
#define MGLPPC_CAP_HOST_EVENTS      (1UL << 5)

#define MGLPPC_SEMANTIC_SLOT_COUNT 2UL
#define MGLPPC_SEMANTIC_SLOT_DWORDS 8192UL
#define MGLPPC_SEMANTIC_MAX_DRAWS 192UL
#define MGLPPC_SEMANTIC_FLAG_PRESENT (1UL << 0)
#define MGLPPC_SEMANTIC_FLAG_READBACK (1UL << 1)
/* Ring entry carries a header-only commit batch: the record chain is
 * followed by one dword per draw (byte offsets into the vertex segment
 * named by entry.segmentIx). */
#define MGLPPC_SEMANTIC_FLAG_COMMIT (1UL << 2)
/* Commit payload is one homogeneous TCL header followed by two dwords per
 * draw: segment byte offset and vertex count. */
#define MGLPPC_SEMANTIC_FLAG_STATE_BATCH (1UL << 3)
#define MGLPPC_TEXTURE_CHUNK_WORDS 1024UL

/* Submission ring (protocol v11). The control block lives in a
 * non-cacheable Radeon streaming segment leased by the host through the
 * backend's private segment query, so both CPUs access it without cache
 * maintenance. Record payloads stay in cacheable shared slots; the PPC
 * cleans them (dcbf+sync) before posting, the host invalidates
 * (CacheClearE) before Execute, exactly as in the v10 message path. */
#define MGLPPC_RING_MAGIC 0x52494e47UL
#define MGLPPC_RING_CONTROL_VERSION 5UL
#define MGLPPC_RING_SLOTS 8UL
#define MGLPPC_EVENT_SLOTS 16UL
#define MGLPPC_RING_SEGMENT_BYTES (64UL * 1024UL)
/* Vertex streaming segments leased with the ring; the PPC writes vertex
 * data through the aperture (91.8 MB/s measured) and commit batches
 * reference it, shrinking Execute records to draw headers. */
#define MGLPPC_VERTEX_SEGMENTS 4UL

typedef struct MGLPPCRingEntryV1 {
    ULONG slot;
    ULONG dwords;
    ULONG generation;
    ULONG flags;
    ULONG crc;
} MGLPPCRingEntryV1;

/* Commit entries (MGLPPC_SEMANTIC_FLAG_COMMIT) carry their metadata in the
 * slot buffer rather than the control block: dword[0]=draw count,
 * dword[1]=vertex segment pool index, then the header-only record chain,
 * then one dword of vertex byte offset per draw. State-batch entries instead
 * carry one header followed by offset/count descriptor pairs. The slot path is the
 * explicitly-maintained cache domain; the control block is only used for
 * the fields that Phase B proved reliable. */
#define MGLPPC_RING_COMMIT_HEADER_DWORDS 2UL
#define MGLPPC_RING_REGISTER_HOST_EVENTS (1UL << 0)

typedef struct MGLPPCRingControlV1 {
    ULONG magic;
    ULONG version;
    ULONG slotCount;
    ULONG slotDwords;
    ULONG epoch;
    ULONG head;         /* PPC advances after posting an entry */
    ULONG completed;    /* 68k advances after consuming an entry */
    ULONG error;        /* 68k sets nonzero on a fatal ring error */
    ULONG presentSeq;   /* 68k counts completed PRESENT entries */
    ULONG retired;      /* latest entry covered by a completed GPU fence */
    ULONG slotAddrs[MGLPPC_RING_SLOTS];
    MGLPPCRingEntryV1 entries[MGLPPC_RING_SLOTS];
    ULONG eventRegionPad[4];
    /* Client-published present-sync state (formerly eventRegionPad[0..1]):
     * the semantic protocol has no EnableSync command, so the frontend
     * stamps the current state + sequence on every publish and the host
     * applies mglEnableSync when the sequence moves. */
    ULONG syncEnabled;
    ULONG syncSeq;
    ULONG eventHead;
    ULONG eventHeadPad[7];
    ULONG eventTail;
    ULONG eventTailPad[7];
    ULONG eventClass[MGLPPC_EVENT_SLOTS];
    ULONG eventCode[MGLPPC_EVENT_SLOTS];
} MGLPPCRingControlV1;

#define MGLPPC_RING_CONTROL_DWORDS \
    (10UL + MGLPPC_RING_SLOTS + MGLPPC_RING_SLOTS * 5UL + \
     22UL + MGLPPC_EVENT_SLOTS * 2UL)
typedef char MGLPPCRingControlSizeCheck[
    sizeof(MGLPPCRingControlV1) ==
        MGLPPC_RING_CONTROL_DWORDS * sizeof(ULONG) ? 1 : -1];
typedef char MGLPPCRingSegmentSizeCheck[
    MGLPPC_RING_CONTROL_DWORDS * sizeof(ULONG) <=
        MGLPPC_RING_SEGMENT_BYTES ? 1 : -1];

#define MGLPPC_STATUS_OK 0L
#define MGLPPC_STATUS_BAD_MESSAGE 1L
#define MGLPPC_STATUS_BAD_VERSION 2L
#define MGLPPC_STATUS_UNKNOWN_COMMAND 3L
#define MGLPPC_STATUS_NO_CONTEXT 4L
#define MGLPPC_STATUS_CONTEXT_EXISTS 5L
#define MGLPPC_STATUS_CONTEXT_FAILED 6L
#define MGLPPC_STATUS_GL_ERROR 7L
#define MGLPPC_STATUS_BAD_BATCH 8L
#define MGLPPC_STATUS_BAD_PAYLOAD 9L
#define MGLPPC_STATUS_NO_MEMORY 10L
#define MGLPPC_STATUS_UNSUPPORTED 11L
#define MGLPPC_STATUS_BUSY 12L
#define MGLPPC_STATUS_VERIFY_FAILED 13L
#define MGLPPC_STATUS_STALE_GENERATION 14L

#define MGLPPC_VRAM_PROBE_BYTES (64UL * 1024UL)
#define MGLPPC_VRAM_PROBE_WORDS (MGLPPC_VRAM_PROBE_BYTES / sizeof(ULONG))
#define MGLPPC_VRAM_PROBE_PASSES 64UL
#define MGLPPC_VRAM_PROBE_68K_SEED 0x68000000UL
#define MGLPPC_VRAM_PROBE_PPC_SEED 0x74100000UL
#define MGLPPC_VRAM_PROBE_SENTINEL 0xa55a3cc3UL
/* Host-written sentinel the PPC must read back: validates PPC-side reads
 * of the streaming aperture, which the original probe never exercised. */
#define MGLPPC_VRAM_PROBE_HOST_INDEX (MGLPPC_VRAM_PROBE_WORDS / 2UL + 1UL)
#define MGLPPC_VRAM_PROBE_HOST_SENTINEL 0xc0ded00dUL

#define MGLPPC_BATCH_CLEAR_COLOR 1UL
#define MGLPPC_BATCH_CLEAR 2UL
#define MGLPPC_BATCH_BEGIN 3UL
#define MGLPPC_BATCH_COLOR4UB 4UL
#define MGLPPC_BATCH_VERTEX3F 5UL
#define MGLPPC_BATCH_END 6UL
#define MGLPPC_BATCH_PRESENT 7UL
#define MGLPPC_BATCH_DELAY 8UL
#define MGLPPC_BATCH_DRAW_C4UB_V3F 9UL
#define MGLPPC_BATCH_SET_STATE 10UL
#define MGLPPC_BATCH_COLOR4F 11UL
#define MGLPPC_BATCH_NORMAL3F 12UL
#define MGLPPC_BATCH_VERTEX4F 13UL
#define MGLPPC_BATCH_VIEWPORT 14UL
#define MGLPPC_BATCH_MATRIX_MODE 15UL
#define MGLPPC_BATCH_LOAD_IDENTITY 16UL
#define MGLPPC_BATCH_PUSH_MATRIX 17UL
#define MGLPPC_BATCH_POP_MATRIX 18UL
#define MGLPPC_BATCH_TRANSLATEF 19UL
#define MGLPPC_BATCH_ROTATEF 20UL
#define MGLPPC_BATCH_SCALEF 21UL
#define MGLPPC_BATCH_FRUSTUMF 22UL
#define MGLPPC_BATCH_ORTHOF 23UL
#define MGLPPC_BATCH_HINT 24UL
#define MGLPPC_BATCH_TEX_ENVI 25UL
#define MGLPPC_BATCH_PIXEL_STOREI 26UL
#define MGLPPC_BATCH_MGL_ENABLE_SYNC 27UL
#define MGLPPC_BATCH_MGL_LOCK_MODE 28UL
#define MGLPPC_BATCH_BIND_TEXTURE 29UL
#define MGLPPC_BATCH_TEX_PARAMETERI 30UL
#define MGLPPC_BATCH_TEX_GENI 31UL
#define MGLPPC_BATCH_DRAW_T2F_C4UB_V3F 32UL

typedef struct MGLPPCMessage {
    struct Message execMessage;
    ULONG magic;
    ULONG protocolVersion;
    ULONG command;
    ULONG sequence;
    LONG status;
    ULONG contextId;
    ULONG value[8];
    ULONG payloadWords;
    ULONG payload[1];
} MGLPPCMessage;

#define MGLPPC_MESSAGE_HEADER_SIZE ((ULONG)offsetof(MGLPPCMessage, payload))
#define MGLPPC_MESSAGE_SIZE(words) \
    (MGLPPC_MESSAGE_HEADER_SIZE + (ULONG)(words) * sizeof(ULONG))
#define MGLPPC_MAX_BATCH_WORDS \
    ((65535UL - MGLPPC_MESSAGE_HEADER_SIZE) / sizeof(ULONG))

/* Exec sees this structure from both CPUs, so its public prefix must match. */
typedef char MGLPPCExecMessageSizeCheck[(sizeof(struct Message) == 20) ? 1 : -1];
typedef char MGLPPCPayloadOffsetCheck[(offsetof(MGLPPCMessage, magic) == 20) ? 1 : -1];
typedef char MGLPPCPayloadStartCheck[(offsetof(MGLPPCMessage, payload) == 80) ? 1 : -1];

#endif
