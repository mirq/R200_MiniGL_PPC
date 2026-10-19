#ifndef LIBRARIES_MINIGL_PPC_PROTOCOL_H
#define LIBRARIES_MINIGL_PPC_PROTOCOL_H

#ifdef MGLPPC_TEXTURE_VALIDATION_TEST
#include <stdint.h>
typedef uint32_t ULONG;
#else
#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <exec/types.h>
#include <exec/ports.h>
#ifdef WARPUP
#pragma pack(pop)
#endif
#include <stddef.h>
#endif

#define MGLPPC_HOST_PORT_NAME "MiniGLPPC.R200.Host.1"
#define MGLPPC_PROTOCOL_MAGIC 0x4d474c50UL
/* v15: MGLPPC_CMD_TEXTURE_UPLOAD_SHARED value[5] carries
 * format | (type << 16) so 16-bit sources (the game's R5G6B5 present
 * tiles) need no 32-bit expansion. v14 hosts/clients keep the old
 * layout (value[5] = format, type = GL_UNSIGNED_BYTE); the host
 * accepts both and the client downgrades after a BAD_VERSION HELLO. */
/* v16: interface-20 PPC engine lease commands. The driver capability is
 * echoed by REGISTER_SEMANTIC_RING, not HELLO (there is no driver session
 * yet at HELLO). MGLPPC_LEASE=1 selects an experimental PPC-only 3D path:
 * a refused/lost lease must never silently use 68k 3D submission. Without
 * that opt-in the existing bounded path remains available. */
#define MGLPPC_PROTOCOL_VERSION 16UL

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
/* Direct texture fill (Stage 1): the host allocates a fresh
 * never-sampled BGRA32 surface per upload, the PPC converts and
 * writes the pixels through the aperture, and COMMIT installs it.
 * BEGIN: value 0 texture name, 1 width, 2 height, 3 requested
 * surface format (0 = BGRA32, 1 = R5G6B5PC native 565); reply
 * 1 cpu address, 2 pitch, 3 bytes, 4 device epoch, 5 granted
 * format. COMMIT: value 0 texture name; reply mirrors the V1
 * texture bridge (1 surface token .. 7 device epoch). ABORT:
 * value 0 texture name. */
#define MGLPPC_TEXDIRECT_FMT_BGRA32 0UL
#define MGLPPC_TEXDIRECT_FMT_RGB565 1UL
#define MGLPPC_CMD_TEXTURE_DIRECT_BEGIN 23UL
#define MGLPPC_CMD_TEXTURE_DIRECT_COMMIT 24UL
#define MGLPPC_CMD_TEXTURE_DIRECT_ABORT 25UL
#define MGLPPC_CMD_TEXTURE_UPLOAD_SHARED 22UL
#define MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED 26UL
#define MGLPPC_CMD_READ_PIXELS_SHARED 27UL
/* Batched two-tile direct fill, negotiated by TEXTURE_DIRECT_BATCH_V1.
 * The Silent Hill intro/FMV uploads a 16-bit frame as a 256x256 plus a
 * 64x256 tile back to back; the frontend stages the first tile and folds
 * both tiles' BEGIN / COMMIT into one mailbox round trip each. Layout:
 * BEGIN2 request 0..3 = tile A name/width/height/requestFormat,
 * 4..7 = tile B (same fields). Reply 1/2 = A cpuAddress/pitch,
 * 3/4 = B cpuAddress/pitch, 5/6 = A/B granted format, 7 = device epoch.
 * COMMIT2 request 0/2 = A/B texture name, 1/3 = A/B srcAddress.
 * Reply 0 = committed mask (bit0 A, bit1 B), 1/2 = A/B surface token,
 * 3 = device epoch, 4/5 = A/B contentSerial,
 * 6/7 = A/B definedLevels | (levelCount << 16).
 * The host aborts a partially begun BEGIN2 before replying, so a failed
 * BEGIN2 leaves no host-side pending allocation. */
#define MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2 29UL
#define MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2 30UL
/* BEGIN request: payloadWords=1, payload[0]=contextId. Reply value[0..7]:
 * ring CPU address, ring GPU address, ring dwords, ring mask, BAR2 base,
 * nextFence, MaxBatchDwords, HeartbeatMs. HEARTBEAT: no payload; success
 * is the message status, NOT value[0]. END request: value[0]=lastFence;
 * success is the status. The driver expires a lease after five seconds
 * without renewal and rejects other Radeon3D submits while it is live. */
#define MGLPPC_CMD_LEASE_BEGIN 31UL
#define MGLPPC_CMD_LEASE_HEARTBEAT 32UL
#define MGLPPC_CMD_LEASE_END 33UL
/* CP-emitter metadata, negotiated at registration. Input value[0]=surface
 * token; reply: GL error, CPU/GPU address, pitch, width, height, format, epoch.
 * Read-only: no recovery, residency allocation, or native context mutation. */
#define MGLPPC_CMD_CP_SURFACE 28UL

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
/* The PPC publishes a cache-flushed MEMF_PUBLIC texture staging buffer and
 * the host consumes it synchronously after invalidating its data cache. */
#define MGLPPC_CAP_TEXTURE_SHARED_V1 (1UL << 6)
/* The backend leases a fresh aperture-mapped texture surface per
 * upload and the PPC fills it directly; the 68k never touches pixels. */
#define MGLPPC_CAP_TEXTURE_DIRECT_V1 (1UL << 7)
#define MGLPPC_CAP_MAILBOX_V1       (1UL << 8)
#define MGLPPC_CAP_TEXTURE_SUBIMAGE_V1 (1UL << 9)
#define MGLPPC_CAP_READ_PIXELS_V1 (1UL << 10)
/* HELLO advertises negotiation support only. REGISTER must also echo
 * RING_REGISTER_INDIRECT after probing the live context. Slot layout for an
 * MGLPPC_SEMANTIC_FLAG_INDIRECT entry: dword[0] = byte offset of the CP
 * stream inside the ring control segment (CP-native byte order), dword[1]
 * = dword count. The host supplies its own ring segment id. */
#define MGLPPC_CAP_INDIRECT (1UL << 11)
/* Trusted PPC emitter, windowed and fullscreen. Requires a live
 * render-transition capability at registration and ring control v6 (live
 * color-target publication) so fullscreen buffer rotation is followed;
 * distinct from the fixed startup smoke. */
#define MGLPPC_CAP_CP_EMIT (1UL << 12)
/* The host serves MGLPPC_CMD_TEXTURE_UPLOAD_SHARED through the mailbox
 * whitelist. Without this bit the client must send it over the Exec
 * transport: an old host would reject it with UNKNOWN_COMMAND. */
#define MGLPPC_CAP_MAILBOX_UPLOAD_SHARED (1UL << 13)
/* The host implements MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2/COMMIT2, the
 * batched two-tile direct fill. Advertised only with the mailbox and the
 * single-tile direct path enabled; the client falls back to the existing
 * BEGIN/COMMIT pair when absent. */
#define MGLPPC_CAP_TEXTURE_DIRECT_BATCH_V1 (1UL << 14)
/* W-80: the host understands zero-payload MGLPPC_SEMANTIC_FLAG_DRAIN ring
 * entries (close a fence-coalescing run, then drain). Advertised in HELLO
 * when the host was started with a fence group above 1; publication is
 * further gated on CP emit. A host without this bit never sees a DRAIN
 * entry, and a client without it never publishes one. */
#define MGLPPC_CAP_FENCE_DRAIN (1UL << 15)
/* Reserved from early v16 drafts. HELLO cannot advertise a live driver
 * capability; use the reply-only RING_REGISTER_PPC_RING bit instead. */
#define MGLPPC_CAP_PPC_RING (1UL << 16)
/* The host serves LEASE_BEGIN/HEARTBEAT/END through the shared mailbox.
 * The Exec-message round trip cost 2.7-6.8 ms per lease handover on the
 * reference machine (host handler 0.15-0.4 ms); release-before-present
 * pays two per frame. Without the bit the client keeps the Exec path. */
#define MGLPPC_CAP_MAILBOX_LEASE (1UL << 17)
/* Reserved startup-smoke storage, separate from control/mailbox lines.
 * Flush PPC writes and drain the aperture before publishing the descriptor. */
#define MGLPPC_IB_OFFSET 8192UL
#define MGLPPC_IB_SMOKE_DWORDS 8UL

/* Optional v15 command 27, negotiated by READ_PIXELS_V1. No payload; eight
 * words: x, y, width, height, format, type, shared address, exact byte count.
 * Only native GL_RGBA / GL_UNSIGNED_BYTE, canonical pack alignment 1 with
 * row length/skips zero. Native rectangle semantics (no clipping). Nonempty
 * output is capped at 2 MiB, with no partial transfer or chunk fallback.
 * Sender flushes geometry and completes the semantic barrier, cleans staging
 * BEFORE publication, then leaves it untouched until acknowledgement. Host
 * validates the entire RAM interval before cache access/write and cleans the
 * output before ack. Sender invalidates AFTER successful ack, then packs into
 * application memory. Failure never copies; timeout pins the same allocation
 * until existing late-completion teardown. Trusted peers, not a RAM sandbox. */
#define MGLPPC_READ_PIXELS_MAX_BYTES (2UL * 1024UL * 1024UL)

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
/* Ring entry asks the host to dispatch a client-built CP packet stream
 * (see MGLPPC_CAP_INDIRECT for the slot layout). */
#define MGLPPC_SEMANTIC_FLAG_INDIRECT (1UL << 4)
/* CP_EMIT slot contains [byteOffset, dwordCount, vertexSegmentMask]. The IB
 * is in a separate 256 KiB lease: one immutable 32 KiB slice per FIFO slot.
 * A slice is reusable only after retired, NOT after completed. No PRESENT
 * flag is accepted here; ordinary zero-payload barriers present/drain. */
#define MGLPPC_SEMANTIC_FLAG_CP_EMIT (1UL << 5)
/* Zero-payload drain request: the host must close any open fence-coalescing
 * run (interface-19 Radeon3DSubmitFence) before it may publish retirement,
 * then drain. The publisher spins on retired==head afterwards; without this
 * entry a trailing unfenced dispatch could never retire. Only published when
 * the client negotiated CP emit AND the host advertised fence coalescing;
 * a host without that support never sets entry.flags from this bit. */
#define MGLPPC_SEMANTIC_FLAG_DRAIN (1UL << 6)
#define MGLPPC_CP_SLICE_BYTES (8192UL * 4UL)
#define MGLPPC_CP_SEGMENT_BYTES (8UL * MGLPPC_CP_SLICE_BYTES)
/* ~64 KB chunks: the 65535-byte Exec message limit leaves 16363 payload
 * words, so 16000 still fits. Hosts bind chunk size only by payloadWords,
 * so a larger client chunk needs no negotiation against an older host. */
#define MGLPPC_TEXTURE_CHUNK_WORDS 16000UL

/* MGLPPC_CMD_TEXTURE_UPLOAD_SHARED value[] layout. Border must be 0.
 *   0 target, 1 level, 2 internal format, 3 width, 4 height,
 *   5 format (v14) or format | (type << 16) (v15),
 *   6 shared CPU address, 7 byte count.
 * Eligible types: GL_UNSIGNED_BYTE with GL_RGB / GL_RGBA /
 * MGL_UBYTE_BGRA / MGL_UBYTE_ARGB / GL_ALPHA / GL_LUMINANCE /
 * GL_LUMINANCE_ALPHA (v15 also carries BGRA/ARGB), and (v15)
 * MGL_UNSIGNED_SHORT_5_6_5 with GL_RGB / MGL_UNSIGNED_SHORT_4_4_4_4
 * with GL_RGBA. COLOR_INDEX is excluded: frontend/native palettes are
 * not synchronized.
 * Numbering contract: source formats/types arrive as DISPATCH-CLIENT
 * values (include/mgl/gl.h). The frontend translates the four
 * below-the-anchor tail constants (BGRA/ARGB/565/4444) to the native
 * tree's numbering before packing, so this handler and every native
 * call see native-header values. Formats below the anchor (RGB, RGBA,
 * alpha, luminance, luminance-alpha, unsigned-byte) are identical in
 * both headers. */
/* Shared staging uploads above this size fall back to the chunked path so
 * a maximum-size 2048x2048 upload cannot pin a permanent 16 MB buffer. */
#define MGLPPC_TEXTURE_SHARED_MAX_BYTES (2UL * 1024UL * 1024UL)

/* Optional v15 extension, gated by TEXTURE_SUBIMAGE_V1 (not version alone).
 * Command 26 has NO payload: textureName, level, x, y, width, height,
 * format | (type << 16), shared source address. Reply is the texture V1
 * residency tuple in value[1..7], with GL error in value[0].
 * Rows are tightly packed, unpack alignment 1, all other unpack fields 0.
 * Only the subrectangle crosses CPUs. The sender publishes old draws and
 * completes the existing semantic barrier BEFORE issuing this command.
 * Shared source must remain immutable/alive until completion, including a
 * timed-out mailbox request. Never retry a published request through Exec.
 * Byte count is derived with the helpers below, capped at SHARED_MAX_BYTES.
 * Zero-area calls are validated frontend no-ops, never sent on the wire.
 * Supported: unsigned-byte RGB/RGBA/alpha/luminance/luminance-alpha/BGRA/ARGB,
 * RGB 565 and RGBA 4444 (native big-endian halfwords). COLOR_INDEX is excluded:
 * frontend/native palette state is not synchronized. Other scalar/packed types
 * are rejected. No chunk fallback for oversized subimages. The host advertises
 * this capability ONLY after native scalar query 0x4d47500c returns schema V1
 * tag 0x54580100 (mask 0xffffff00) with SUBIMAGE_DIRECT_BGRA bit 0. With a
 * context, GL error must be zero; before context creation, native GetError's
 * contextless INVALID_OPERATION is also expected and never suffices without
 * the positive tag/bit. Protocol 15 / PPC ABI 3 / native ABI 5 alone do not
 * establish support.
 * Missing/malformed native capability disables command 26 even for image-backed
 * textures; the frontend returns INVALID_OPERATION before copying or sending.
 * A matched native library lazily materializes committed one-level BGRA direct
 * textures on first subimage. V2 preflight accepts their resident surface; V1
 * postquery forces/returns the resulting image-backed residency. Ordinary direct
 * full uploads gain no eager CPU image or native pixel copy. Other direct surface
 * formats and extending the direct texture's mip chain remain unsupported.
 * Native allocation/residency failure may follow mutation; caller must not draw
 * using the old mirrored handle after a failed update. The frontend conservatively
 * invalidates its residency mirror for any failed transmitted update; retry or
 * redefine before sampling again. A mailbox timeout retains staging and allows
 * only teardown after late completion, just like an abandoned direct COMMIT.
 * Frontend errors: INVALID_ENUM for target/source format/type; INVALID_VALUE
 * for dimensions, undefined level, bounds, NULL nonempty source or span overflow;
 * INVALID_OPERATION for missing capability, unit != 0, no texture or >2 MiB;
 * OUT_OF_MEMORY for staging allocation failure. Native GL errors are propagated.
 * Both mailbox and Exec enter the same validating host handler. */

/* GL enum names are expanded at the call site, after the platform GL header. */
#define MGLPPC_TEXTURE_SOURCE_BYTES(format, type) \
    ((type) == GL_UNSIGNED_BYTE ? \
        ((format) == GL_RGB ? 3UL : \
         ((format) == GL_RGBA || (format) == MGL_UBYTE_BGRA || \
          (format) == MGL_UBYTE_ARGB) ? 4UL : \
         (format) == GL_LUMINANCE_ALPHA ? 2UL : \
         ((format) == GL_ALPHA || (format) == GL_LUMINANCE) ? 1UL : 0UL) : \
     (((type) == MGL_UNSIGNED_SHORT_5_6_5 && (format) == GL_RGB) || \
      ((type) == MGL_UNSIGNED_SHORT_4_4_4_4 && (format) == GL_RGBA)) ? 2UL : 0UL)

/* Pure 32-bit arithmetic, shared by both CPUs and host-independent tests.
 * Includes the last row's bytes, not trailing padding. Overlapping source rows
 * (row length < width) are legal; neither skips nor row length bound the row. */
static __inline__ int MGLPPCTextureSpan(ULONG width, ULONG height, ULONG bpp,
    ULONG alignment, ULONG rowLength, ULONG skipRows, ULONG skipPixels,
    ULONG address, ULONG *rowBytes, ULONG *stride, ULONG *offset, ULONG *bytes)
{
    ULONG sourceWidth = rowLength ? rowLength : width;
    ULONG skip, last;
    if (!width || !height || !bpp || bpp > 4UL ||
        (alignment != 1UL && alignment != 2UL &&
         alignment != 4UL && alignment != 8UL) ||
        width > 0xffffffffUL / bpp || sourceWidth > 0xffffffffUL / bpp)
        return 0;
    *rowBytes = width * bpp;
    if (height > 0xffffffffUL / *rowBytes ||
        sourceWidth * bpp > 0xffffffffUL - (alignment - 1UL)) return 0;
    *bytes = height * *rowBytes;
    *stride = (sourceWidth * bpp + alignment - 1UL) & ~(alignment - 1UL);
    if (skipRows > 0xffffffffUL / *stride ||
        skipPixels > 0xffffffffUL / bpp) return 0;
    skip = skipRows * *stride;
    if (skip > 0xffffffffUL - skipPixels * bpp) return 0;
    *offset = skip + skipPixels * bpp;
    if (height - 1UL > (0xffffffffUL - *offset) / *stride) return 0;
    last = *offset + (height - 1UL) * *stride;
    if (*rowBytes > 0xffffffffUL - last) return 0;
    return address <= 0xffffffffUL - (last + *rowBytes);
}

static __inline__ int MGLPPCTextureRect(ULONG baseWidth, ULONG baseHeight,
    ULONG levels, ULONG defined, ULONG level, ULONG x, ULONG y,
    ULONG width, ULONG height)
{
    ULONG w, h;
    if (!baseWidth || !baseHeight || baseWidth > 2048UL || baseHeight > 2048UL ||
        !levels || levels > 12UL || level >= levels || !(defined & (1UL << level)))
        return 0;
    w = baseWidth >> level; if (!w) w = 1;
    h = baseHeight >> level; if (!h) h = 1;
    return x <= w && y <= h && width <= w - x && height <= h - y;
}

#ifndef MGLPPC_TEXTURE_VALIDATION_TEST
/* Submission ring (protocol v11). The control block lives in a
 * non-cacheable Radeon streaming segment leased by the host through the
 * backend's private segment query, so both CPUs access it without cache
 * maintenance. Record payloads stay in cacheable shared slots; the PPC
 * cleans them (dcbf+sync) before posting, the host invalidates
 * (CacheClearE) before Execute, exactly as in the v10 message path. */
#define MGLPPC_RING_MAGIC 0x52494e47UL
/* v6 appends the live color-target publication (colorToken/colorSeq). Both
 * peers must agree; a v5 peer fails ring registration and falls back. */
#define MGLPPC_RING_CONTROL_VERSION 6UL
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
#define MGLPPC_RING_REGISTER_MAILBOX (1UL << 1)
#define MGLPPC_RING_REGISTER_INDIRECT (1UL << 2)
/* Reply-only: acknowledgment is transport validation, NOT GPU retirement. */
#define MGLPPC_RING_REGISTER_IB_DRY (1UL << 3)
/* Request/echo. Reply value[5..7]=CP lease CPU address/id/bytes, vertex
 * payload tuples expand from 3 to 4 words: CPU address/bytes/id/GPU address.
 * Mutually exclusive with startup INDIRECT; protocol v15 optional extension. */
#define MGLPPC_RING_REGISTER_CP_EMIT (1UL << 4)
/* Reply-only: the live Radeon3D session advertises RADEON3D_CAP_PPC_RING
 * (interface 20), so the frontend may hold the PPC engine lease. The 68k
 * host only learns the driver caps while opening the session here — HELLO
 * cannot advertise them. Protocol v16. */
#define MGLPPC_RING_REGISTER_PPC_RING (1UL << 5)

/* Optional protocol-15 extension; never changes RingControlV1. Registration
 * echoes MAILBOX only after initializing this region, and returns the segment
 * extent in value[4]. Each 32-byte line has one writer after registration. */
#define MGLPPC_MAILBOX_OFFSET 4096UL
#define MGLPPC_MAILBOX_MAGIC 0x4d424f58UL
#define MGLPPC_MAILBOX_VERSION 1UL
typedef struct MGLPPCMailboxV1 {
    ULONG magic, version, epoch, headerPad[5];
    ULONG reqSerial, command, contextId, reqPad[5];
    ULONG reqValue[8];
    ULONG ackSerial, status, ackPad[6];
    ULONG ackValue[8];
} MGLPPCMailboxV1;
typedef char MGLPPCMailboxLayoutCheck[
    sizeof(ULONG) == 4 && sizeof(MGLPPCMailboxV1) == 160 &&
    offsetof(MGLPPCMailboxV1, magic) == 0 &&
    offsetof(MGLPPCMailboxV1, version) == 4 &&
    offsetof(MGLPPCMailboxV1, epoch) == 8 &&
    offsetof(MGLPPCMailboxV1, reqSerial) == 32 &&
    offsetof(MGLPPCMailboxV1, command) == 36 &&
    offsetof(MGLPPCMailboxV1, contextId) == 40 &&
    offsetof(MGLPPCMailboxV1, reqValue) == 64 &&
    offsetof(MGLPPCMailboxV1, ackSerial) == 96 &&
    offsetof(MGLPPCMailboxV1, status) == 100 &&
    offsetof(MGLPPCMailboxV1, ackValue) == 128 ? 1 : -1];
#define MGLPPC_MAILBOX_COMMAND_ALLOWED(cmd) \
    ((cmd) == MGLPPC_CMD_CP_SURFACE || \
     (cmd) == MGLPPC_CMD_BIND_TEXTURE || \
     (cmd) == MGLPPC_CMD_GET_INTEGER || \
     (cmd) == MGLPPC_CMD_TEXTURE_DIRECT_BEGIN || \
     (cmd) == MGLPPC_CMD_TEXTURE_DIRECT_COMMIT || \
     (cmd) == MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2 || \
     (cmd) == MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2 || \
     (cmd) == MGLPPC_CMD_TEXTURE_DIRECT_ABORT || \
     (cmd) == MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED || \
     (cmd) == MGLPPC_CMD_READ_PIXELS_SHARED || \
     (cmd) == MGLPPC_CMD_TEXTURE_UPLOAD_SHARED || \
     (cmd) == MGLPPC_CMD_LEASE_BEGIN || \
     (cmd) == MGLPPC_CMD_LEASE_HEARTBEAT || \
     (cmd) == MGLPPC_CMD_LEASE_END)

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
    /* v6 live color-target publication. The host owns the native display
     * rotation, so the PPC CP emitter cannot derive the current drawing
     * target from its own surfaces mirror (fullscreen exposes only
     * surfaces[0] at create). The host writes the live target token and a
     * monotonically increasing sequence after registration and after every
     * display switch; the PPC invalidates this line, compares the sequence
     * before each CP batch and re-stamps the target dword of every record
     * when the sequence moves. colorToken is 32-byte aligned and the pair
     * shares one line; the host writes token first, sequence last, then
     * CacheClearE, and the PPC reads sequence first. */
    ULONG colorToken;
    ULONG colorSeq;
} MGLPPCRingControlV1;

typedef char MGLPPCMailboxExtentCheck[
    sizeof(MGLPPCRingControlV1) <= MGLPPC_MAILBOX_OFFSET &&
    (MGLPPC_MAILBOX_OFFSET & 31UL) == 0 &&
    MGLPPC_MAILBOX_OFFSET + sizeof(MGLPPCMailboxV1) <=
        MGLPPC_RING_SEGMENT_BYTES ? 1 : -1];

#define MGLPPC_RING_CONTROL_DWORDS \
    (10UL + MGLPPC_RING_SLOTS + MGLPPC_RING_SLOTS * 5UL + \
     22UL + MGLPPC_EVENT_SLOTS * 2UL + 2UL)
typedef char MGLPPCRingControlSizeCheck[
    sizeof(MGLPPCRingControlV1) ==
        MGLPPC_RING_CONTROL_DWORDS * sizeof(ULONG) ? 1 : -1];
typedef char MGLPPCRingControlOffsetCheck[
    offsetof(MGLPPCRingControlV1, colorToken) ==
        (MGLPPC_RING_CONTROL_DWORDS - 2UL) * sizeof(ULONG) &&
    offsetof(MGLPPCRingControlV1, colorSeq) ==
        (MGLPPC_RING_CONTROL_DWORDS - 1UL) * sizeof(ULONG) &&
    (offsetof(MGLPPCRingControlV1, colorToken) & 31UL) == 0 ? 1 : -1];
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
#endif
