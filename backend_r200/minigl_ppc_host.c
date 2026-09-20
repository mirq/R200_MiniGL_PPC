#include <devices/timer.h>
#include <exec/io.h>
#include <exec/execbase.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Diagnostic clocks/stage reports are separate from protocol probe timing. */
#ifndef MGLPPC_PROFILE
#define MGLPPC_PROFILE 0
#endif

#ifndef MGLPPC_PACING
#define MGLPPC_PACING 0
#endif

#ifndef MGLPPC_IDLE_DIAG
#define MGLPPC_IDLE_DIAG 0
#endif

#if MGLPPC_IDLE_DIAG
static ULONG IdleEmptySleeps, IdleActiveSleeps, IdleRenderSleeps;
static ULONG IdleActiveTicks, IdleRenderTicks, IdleWakeBacklog;
#endif

/* Set 0 only for an otherwise identical lookup-cost comparison. */
#ifndef MGLPPC_HOST_TARGET_CACHE
#define MGLPPC_HOST_TARGET_CACHE 1
#endif

/* DH2:wosbuild/mglppc_host.log trace logger. Default off: each emitted line
 * costs a 68k Open/Seek/Write/Close cycle. Define 1 to enable file traces.
 * MGLPPC_PROFILE keeps counters in RAM and reports after shutdown cleanup. */
#ifndef MGLPPC_HOST_TRACE
#define MGLPPC_HOST_TRACE 0
#endif

#include <libraries/minigl_ppc_protocol.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <libraries/minigl_r200_bridge.h>
#include <proto/radeon3d.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <libraries/minigl_ppc_ib_trace.h>

#if defined(MGLPPC_REJECT_DUMP) && MGLPPC_REJECT_DUMP
/* Preserve failure reports even while stdout is exclusively locked by DOS.
 * Native direct-upload refusal has a normal fallback, so do not let those
 * messages consume the bounded ring/presentation-failure log. */
static int HostFailurePrintf(const char *format, ...)
{
    static unsigned reports;
    va_list args;
    int result;
    if (reports < 32 && !strstr(format, "dbeg nativefail") &&
        (strstr(format, "fail") || strstr(format, "error") ||
         strstr(format, "bad"))) {
        char line[512];
        BPTR file;
        va_start(args, format);
        vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        file = Open((CONST_STRPTR)"DH2:wosbuild/qhosterr.log",
                    reports ? MODE_OLDFILE : MODE_NEWFILE);
        if (file) {
            Seek(file, 0, OFFSET_END);
            Write(file, line, strlen(line));
            Close(file);
        }
        ++reports;
    }
    va_start(args, format);
    result = vprintf(format, args);
    va_end(args);
    return result;
}
#define printf HostFailurePrintf

static void WriteRejectWords(BPTR file, const ULONG *words, ULONG count)
{
    UBYTE bytes[128];
    while (count) {
        ULONG i, n = count > 32UL ? 32UL : count;
        for (i = 0; i < n; ++i) {
            ULONG value = words[i];
            bytes[i*4] = value >> 24;
            bytes[i*4+1] = value >> 16;
            bytes[i*4+2] = value >> 8;
            bytes[i*4+3] = value;
        }
        Write(file, bytes, n * 4UL);
        words += n;
        count -= n;
    }
}
#endif

#if MGLPPC_PROFILE
#include <libraries/minigl_perf.h>
#endif

/* Simultaneous GPU submissions tracked when the driver validates ranged
 * fences (RADEON3D_CAP_MULTI_FENCE). One CP slice per ring slot bounds the
 * CP path; semantic batches are bounded by the four vertex segments. Without
 * the cap the queue never exceeds one entry and the host drains per
 * dispatch, exactly as before. */
#ifndef MGLPPC_INFLIGHT_SLOTS
#define MGLPPC_INFLIGHT_SLOTS MGLPPC_RING_SLOTS
#endif

typedef struct MGLPPCHostState {
    int backendOpen;
    void *context;
    ULONG contextId;
    ULONG contextSerial;
    int running;
    int insidePrimitive;
    int displayLocked;
    UBYTE *uploadPixels;
    ULONG uploadBytes;
    ULONG uploadReceived;
    ULONG indirectProbeOk;
    int ibDryRun;
    BOOL cpEnabled, cpSegmentLeased, cpFailed;
    struct Radeon3DSegment cpSegment;
    ULONG cpColorToken, cpColorSeq, cpDispatches, cpDwords;
    int cpTokenMismatchReported;
    ULONG uploadArgs[8];
    /* Native glGetIntegerv exposes neither pack nor unpack. Track host writes. */
    GLint unpack[4];
    GLint pack[4];
    struct Radeon3DDevice *probeDevice;
    struct Radeon3DSegment probeSegment;
    ULONG probeCookie;
    ULONG nextProbeCookie;
    ULONG probe68kMicros;
    int probeConfirmed;
    ULONG *semanticSlots[MGLPPC_SEMANTIC_SLOT_COUNT];
    ULONG semanticGeneration[MGLPPC_SEMANTIC_SLOT_COUNT];
    ULONG semanticEpoch;
    /* In-flight GPU submissions, oldest first. Only populated beyond one
     * entry when the driver advertises ranged fence validation. semanticFence
     * mirrors fenceValue[0] for the poll/idle checks. */
    ULONG semanticFence;
    ULONG fenceValue[MGLPPC_INFLIGHT_SLOTS];
    ULONG fenceSerial[MGLPPC_INFLIGHT_SLOTS];
    ULONG fenceMask[MGLPPC_INFLIGHT_SLOTS];
    ULONG fenceCount;
    /* W-80 fence coalescing (driver interface 19): fence every Nth CP
     * dispatch instead of every dispatch, then close the run with
     * Radeon3DSubmitFence() before any drain. In-order CP execution makes
     * the closing fence cover every earlier dispatch, so retirement keeps
     * its exact meaning while the GPU stops draining once per dispatch.
     * fenceGroupSize 1 disables coalescing (legacy behaviour). */
    ULONG fenceGroupSize;
    ULONG fenceGroupCount;
    ULONG pendingSegMask;
    ULONG pendingUnfenced;
    BOOL fenceCoalesce;
    ULONG semanticExecutes;
    ULONG semanticBoundTexture;
    int semanticRegistered;
    int semanticDriverOpen;
    int contextWindowed;
    struct Window *contextWindow;
    volatile MGLPPCRingControlV1 *ringControl;
    volatile MGLPPCMailboxV1 *mailbox;
    ULONG mailboxSerial;
    int mailboxDisabled;
    ULONG ringSegmentId;
    int ringSegmentLeased;
    ULONG ringDeviceEpoch;
    ULONG ringDeviceGeneration;
    ULONG ringDeviceToken;
    ULONG ringRadeonCaps;
    ULONG queuedSegmentMask;
    ULONG ringEpoch;
    ULONG ringCompleted;
    int ringSeenEntry;
    ULONG ringSyncSeq;
    ULONG ringLastGen[MGLPPC_RING_SLOTS];
    ULONG vertexSegmentIds[MGLPPC_VERTEX_SEGMENTS];
    ULONG vertexSegmentCount;
    int hostEventsEnabled;
    ULONG droppedEvents;
    int directBeginLogged;
    int directCommitLogged;
    int sharedLogged;
    int directDisabled;
    int headerLogged;
    int indirectOkLogged;
    ULONG uploadCount;
    ULONG uploadFails;
    ULONG lastHeartbeat;
    int verboseUploads;
    BOOL nativeSubimage;
} MGLPPCHostState;

static const GLenum HostUnpackNames[4] = {
    GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
    GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS
};
static const GLenum HostPackNames[4] = {
    GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
    GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS
};

struct Library *Radeon9200Base;
struct Device *TimerBase;
static struct timerequest TimerRequest;
static struct MsgPort TimerPort;
static ULONG TimerClockRate;

/* How long the host must be continuously idle before it sleeps. A running
 * client's inter-command gaps are milliseconds; a game frame's PPC-only
 * phase is tens of milliseconds. Any sleep inside those windows costs the
 * client a full Delay(1) tick (~20 ms) on its next command, which is what
 * made Silent Hill's present stage 61-79 ms instead of ~35 ms. Sleep only
 * after a second and a half of real idle; spin otherwise. Spinning costs
 * the 68k host nothing it needs while a client is rendering, and it keeps
 * every command served in microseconds. The tick limit is cached once:
 * dividing in the idle loop cost thousands of 68k cycles per frame. */
#define MGLPPC_IDLE_SLEEP_MS 1500UL
static ULONG HostIdleSleepTicks;

static BOOL OpenTimerClock(void)
{
    struct EClockVal value;

    TimerPort.mp_Node.ln_Type = NT_MSGPORT;
    TimerPort.mp_Flags = PA_IGNORE;
    TimerPort.mp_MsgList.lh_Head =
        (struct Node *)&TimerPort.mp_MsgList.lh_Tail;
    TimerPort.mp_MsgList.lh_Tail = NULL;
    TimerPort.mp_MsgList.lh_TailPred =
        (struct Node *)&TimerPort.mp_MsgList.lh_Head;
    TimerRequest.tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    TimerRequest.tr_node.io_Message.mn_ReplyPort = &TimerPort;
    TimerRequest.tr_node.io_Message.mn_Length = sizeof(TimerRequest);
    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)&TimerRequest, 0))
        return FALSE;
    TimerBase = TimerRequest.tr_node.io_Device;
    TimerClockRate = ReadEClock(&value);
    if (TimerClockRate != 0) {
        HostIdleSleepTicks = (TimerClockRate / 1000UL) * MGLPPC_IDLE_SLEEP_MS;
        return TRUE;
    }
    CloseDevice((struct IORequest *)&TimerRequest);
    TimerBase = NULL;
    return FALSE;
}

static ULONG ProbeTicks(void)
{
    struct EClockVal value;

    ReadEClock(&value);
    return value.ev_lo;
}

static ULONG ProbeMicros(ULONG start, ULONG end)
{
    unsigned long long ticks = (ULONG)(end - start);

    return (ULONG)(ticks * 1000000ULL / TimerClockRate);
}

#if MGLPPC_PACING
/* Full host session, not per context: recreation gaps count between contexts.
 * Frame numbers are 1-based successful presents; a gap ends at its frame. */
static const ULONG HostPacingLimitsMs[7] = { 1, 5, 10, 20, 33, 50, 100 };
static ULONG HostPacingLimits[7], HostPacingBins[8];
static ULONG HostPacingPresents, HostPacingPrevious;
static struct { ULONG frame, ticks; } HostPacingGaps[16];

static void HostPacingInit(void)
{
    ULONG i;

    for (i = 0; i < 7; ++i)
        HostPacingLimits[i] = (ULONG)(((unsigned long long)TimerClockRate *
                                     HostPacingLimitsMs[i] + 999ULL) / 1000ULL);
}

static void HostPacingPresent(void)
{
    ULONG now = ProbeTicks(), ticks, i;

    if (++HostPacingPresents == 1) {
        HostPacingPrevious = now;
        return;
    }
    ticks = (ULONG)(now - HostPacingPrevious);
    HostPacingPrevious = now;
    for (i = 0; i < 7 && ticks >= HostPacingLimits[i]; ++i) { }
    ++HostPacingBins[i];
    if (HostPacingGaps[15].frame && ticks <= HostPacingGaps[15].ticks)
        return;
    /* Descending insertion, retaining earlier frames on equal gaps. */
    for (i = 15; i && (!HostPacingGaps[i - 1].frame ||
                      ticks > HostPacingGaps[i - 1].ticks); --i)
        HostPacingGaps[i] = HostPacingGaps[i - 1];
    HostPacingGaps[i].frame = HostPacingPresents;
    HostPacingGaps[i].ticks = ticks;
}

static void HostPacingReport(void)
{
    ULONG i;

    /* TimerClockRate is saved at open; ProbeMicros never reads TimerBase. */
    printf("HOST_PACING scope=host_session presents=%lu intervals=%lu "
           "eclock_hz=%lu\n", (unsigned long)HostPacingPresents,
           (unsigned long)(HostPacingPresents ? HostPacingPresents - 1UL : 0UL),
           (unsigned long)TimerClockRate);
    printf("HOST_PACING histogram=noncumulative "
           "bounds=lower_inclusive_upper_exclusive "
           "limits_ms=1,5,10,20,33,50,100 rounding=ceil boundaries_ticks=0");
    for (i = 0; i < 7; ++i)
        printf(",%lu", (unsigned long)HostPacingLimits[i]);
    printf(",inf counts=");
    for (i = 0; i < 8; ++i)
        printf("%s%lu", i ? "," : "", (unsigned long)HostPacingBins[i]);
    printf("\n");
    for (i = 0; i < 16 && HostPacingGaps[i].frame; ++i)
        printf("HOST_GAP rank=%lu frame=%lu ticks=%lu us=%lu\n",
               (unsigned long)(i + 1UL), (unsigned long)HostPacingGaps[i].frame,
               (unsigned long)HostPacingGaps[i].ticks,
               (unsigned long)ProbeMicros(0, HostPacingGaps[i].ticks));
    fflush(stdout);
}

#define HOST_PACING_INIT() HostPacingInit()
#define HOST_PACING_PRESENT() HostPacingPresent()
#define HOST_PACING_REPORT() HostPacingReport()
#else
#define HOST_PACING_INIT() do { } while (0)
#define HOST_PACING_PRESENT() do { } while (0)
#define HOST_PACING_REPORT() do { } while (0)
#endif

/* Table generated from the same reflected polynomial the bit loop used, so
 * every emitted value is unchanged and the wire contract is untouched. The
 * per-slot CRC runs on both CPUs over the whole record range every submit,
 * which made the byte-at-a-time bit loop a measurable share of the frame. */
static ULONG SemanticCRCTable[256];
static int SemanticCRCTableReady;

static void SemanticCRC32Init(void)
{
    ULONG index, bit, value;

    for (index = 0; index < 256UL; ++index) {
        value = index;
        for (bit = 0; bit < 8UL; ++bit)
            value = (value >> 1) ^
                    ((value & 1UL) ? 0xedb88320UL : 0UL);
        SemanticCRCTable[index] = value;
    }
    SemanticCRCTableReady = 1;
}

static ULONG SemanticCRC32(const ULONG *words, ULONG count)
{
    const UBYTE *bytes = (const UBYTE *)words;
    ULONG total = count * sizeof(ULONG);
    ULONG crc = 0xffffffffUL;
    ULONG index;

    if (!SemanticCRCTableReady)
        SemanticCRC32Init();
    for (index = 0; index < total; ++index)
        crc = (crc >> 8) ^
              SemanticCRCTable[(crc ^ bytes[index]) & 0xffUL];
    return ~crc;
}

static BOOL QueryBackendBridge(MGLR200PrivateBridgeV1 *bridge)
{
    memset(bridge, 0, sizeof(*bridge));
    glGetIntegerv(MGL_R200_PRIVATE_BRIDGE_V1_QUERY, (GLint *)bridge);
    return bridge->version == MGL_R200_PRIVATE_BRIDGE_V1_VERSION &&
           bridge->structSize == sizeof(*bridge) &&
           bridge->deviceToken && bridge->colorSurfaceToken &&
           bridge->depthSurfaceToken && bridge->width && bridge->height &&
           bridge->batchDwords;
}

#if MGLPPC_PROFILE
static ULONG HostBridgeQueries, HostBridgeCacheHits;
#endif

static BOOL QueryBackendBridgeV2(MGLR200PrivateBridgeV2 *bridge)
{
#if MGLPPC_PROFILE
    ++HostBridgeQueries;
#endif
    memset(bridge, 0, sizeof(*bridge));
    glGetIntegerv(MGL_R200_PRIVATE_BRIDGE_V2_QUERY, (GLint *)bridge);
    return bridge->version == MGL_R200_PRIVATE_BRIDGE_V2_VERSION &&
           bridge->structSize == sizeof(*bridge) &&
           bridge->deviceToken && bridge->colorSurfaceToken &&
           bridge->depthSurfaceToken && bridge->width && bridge->height &&
           bridge->batchDwords;
}

/* Stamp the live target while the host owns the slot. Fullscreen presents can
 * rotate away from the target token originally mirrored by the PPC. */
static void PatchRecordTargets(ULONG *records, ULONG recordDwords,
                               ULONG targetToken)
{
    ULONG offset = 0;

    if (!records || !targetToken)
        return;
    while (offset + 3UL <= recordDwords) {
        ULONG opcode = records[offset];
        ULONG length = records[offset + 1UL];

        /* Clears and draws are the record types that name a color target;
         * stop at anything else rather than overwriting a payload dword. */
        if (opcode < RADEON3D_EXEC_CLEAR ||
            opcode > RADEON3D_EXEC_DRAW_LINE_LOOP ||
            !length || length > recordDwords - offset)
            break;
        records[offset + 2UL] = targetToken;
        offset += length;
    }
}

/* Ring control v6: publish the live color target for the PPC CP emitter.
 * The native display rotation lives on this side (screen-buffer flip/blit),
 * so the PPC cannot derive the current drawing target from its own mirrors.
 * The PPC reads the token only after observing a new sequence value: write
 * the token first, then the incremented sequence, then clean both dwords
 * into the non-cacheable control block. Only used while CP is enabled. */
static void PublishCpColorTarget(MGLPPCHostState *state, ULONG token)
{
    volatile MGLPPCRingControlV1 *control = state->ringControl;

    if (!control || !token)
        return;
    control->colorToken = token;
    CacheClearE((APTR)&control->colorToken, sizeof(ULONG), CACRF_ClearD);
    ++state->cpColorSeq;
    if (!state->cpColorSeq)
        ++state->cpColorSeq;
    control->colorSeq = state->cpColorSeq;
    CacheClearE((APTR)&control->colorToken, 2UL * sizeof(ULONG), CACRF_ClearD);
}

/* Sonnet's legacy LoadSeg heuristic scans every hunk for the uppercase RACE
 * signature. Embedding the full upload-trace option name misclassifies this
 * 68k host as PPC, redirecting loading/allocations to PPC memory. Volatile
 * construction preserves the public option name without emitting that marker.
 * check_68k_host.py verifies the linked executable, not just this source. */
static LONG ReadUploadTraceOption(UBYTE *value, LONG bytes)
{
    volatile UBYTE name[] = "MGLPPC_UPLOAD_T?ACE";
    name[15] = 'R';
    return GetVar((CONST_STRPTR)name, value, bytes, GVF_GLOBAL_ONLY);
}

static BOOL QueryBackendSubimage(BOOL hasContext)
{
    GLint reply = 0;
    GLenum error;
    (void)glGetError();
    glGetIntegerv(MGL_R200_PRIVATE_TEXTURE_CAPS_V1_QUERY, &reply);
    error = glGetError();
    /* Native GetError itself returns INVALID_OPERATION without a context.
     * HELLO must still check the positive tagged scalar, never that error alone. */
    return (error == GL_NO_ERROR ||
            (!hasContext && error == GL_INVALID_OPERATION)) &&
        ((ULONG)reply & MGL_R200_PRIVATE_TEXTURE_CAPS_V1_TAG_MASK) ==
            MGL_R200_PRIVATE_TEXTURE_CAPS_V1_TAG &&
        ((ULONG)reply & MGL_R200_PRIVATE_TEXTURE_CAP_SUBIMAGE_DIRECT_BGRA) != 0;
}

static BOOL QueryBackendTexture(ULONG textureName,
                                MGLR200PrivateTextureV1 *bridge)
{
    memset(bridge, 0, sizeof(*bridge));
    bridge->textureName = textureName;
    glGetIntegerv(MGL_R200_PRIVATE_TEXTURE_V1_QUERY, (GLint *)bridge);
    return bridge->version == MGL_R200_PRIVATE_TEXTURE_V1_VERSION &&
           bridge->structSize == sizeof(*bridge) &&
           bridge->textureName == textureName && bridge->surfaceToken &&
           bridge->width && bridge->height && bridge->definedLevels &&
           bridge->levelCount && bridge->deviceEpoch;
}

static BOOL BeginBackendDirectTexture(ULONG textureName, ULONG width,
                                      ULONG height, ULONG requestFormat,
                                      MGLR200PrivateTexDirectBeginV2 *bridge)
{
    memset(bridge, 0, sizeof(*bridge));
    bridge->textureName = textureName;
    bridge->width = width;
    bridge->height = height;
    bridge->requestFormat = requestFormat;
    glGetIntegerv(MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V2_QUERY,
                  (GLint *)bridge);
    if (bridge->version != MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V2_VERSION ||
        bridge->structSize != sizeof(*bridge)) {
        printf("host: dbeg badbridge ver=%lu sz=%lu want=%lu err=%04lx\n",
               (unsigned long)bridge->version,
               (unsigned long)bridge->structSize,
               (unsigned long)MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V2_VERSION,
               (unsigned long)glGetError());
        fflush(stdout);
        return FALSE;
    }
    if (!bridge->cpuAddress || !bridge->pitch || !bridge->bytes ||
        !bridge->deviceEpoch) {
        /* A refused direct upload falls back to the ordinary upload path.
         * Always drain the probe error, but do not log this routine fallback
         * in a release/performance host. */
        GLenum nativeError = glGetError();
#if MGLPPC_HOST_TRACE || (defined(MGLPPC_REJECT_DUMP) && MGLPPC_REJECT_DUMP)
        printf("host: dbeg nativefail err=%04lx fmt=%lu tex=%lu %lux%lu "
               "req=%lu\n",
               (unsigned long)nativeError,
               (unsigned long)bridge->format,
               (unsigned long)textureName,
               (unsigned long)width, (unsigned long)height,
               (unsigned long)requestFormat);
        fflush(stdout);
#else
        (void)nativeError;
#endif
        return FALSE;
    }
    return TRUE;
}

static BOOL CommitBackendDirectTexture(ULONG textureName,
                                       ULONG srcAddress,
                                       MGLR200PrivateTexDirectCommitV2 *bridge)
{
    memset(bridge, 0, sizeof(*bridge));
    bridge->textureName = textureName;
    bridge->srcAddress = srcAddress;
    glGetIntegerv(MGL_R200_PRIVATE_TEXDIRECT_COMMIT_V2_QUERY,
                  (GLint *)bridge);
    return bridge->version == MGL_R200_PRIVATE_TEXDIRECT_COMMIT_V2_VERSION &&
           bridge->structSize == sizeof(*bridge) && bridge->surfaceToken &&
           bridge->width && bridge->height && bridge->deviceEpoch;
}

static void AbortBackendDirectTexture(ULONG textureName)
{
    MGLR200PrivateTexDirectAbortV1 bridge;
    memset(&bridge, 0, sizeof(bridge));
    bridge.textureName = textureName;
    glGetIntegerv(MGL_R200_PRIVATE_TEXDIRECT_ABORT_V1_QUERY,
                  (GLint *)&bridge);
}

/* Publish `retired` monotonically; the PPC reuses CP slices on this value. */
static void PublishRetired(MGLPPCHostState *state, ULONG retired)
{
    volatile MGLPPCRingControlV1 *control = state->ringControl;

    if (!control || retired > state->ringCompleted)
        return;
    if ((LONG)(retired - control->retired) <= 0)
        return;
    control->retired = retired;
    if (state->cpEnabled)
        CacheClearE((APTR)&control->retired, sizeof(ULONG), CACRF_ClearD);
}

/* Everything before the oldest in-flight submission is retired; with none
 * outstanding, every consumed entry is. W-80: a pending fence-less run makes
 * "no queued fence" mean "nothing provably finished" rather than "everything
 * consumed is done", so retirement must hold still until the run is closed. */
static void AdvanceRetired(MGLPPCHostState *state)
{
    if (!state->ringControl)
        return;
    if (state->fenceCount)
        PublishRetired(state, state->fenceSerial[0]);
    else if (state->pendingUnfenced)
        return;
    else
        PublishRetired(state, state->ringCompleted);
}

static void RecomputeQueuedMask(MGLPPCHostState *state)
{
    ULONG index, mask = 0;

    for (index = 0; index < state->fenceCount; ++index)
        mask |= state->fenceMask[index];
    state->queuedSegmentMask = mask;
}

/* Queue a submission's fence, covering every ring entry up to `serial`. With
 * the queue full the caller must drain first. */
static BOOL RecordFence(MGLPPCHostState *state, ULONG serial, ULONG fence,
                        ULONG mask)
{
    ULONG index;

    if (!fence || state->fenceCount >= MGLPPC_INFLIGHT_SLOTS)
        return FALSE;
    index = state->fenceCount++;
    state->fenceValue[index] = fence;
    state->fenceSerial[index] = serial;
    state->fenceMask[index] = mask;
    state->queuedSegmentMask |= mask;
    state->semanticFence = state->fenceValue[0];
    return TRUE;
}

static void PopFence(MGLPPCHostState *state)
{
    ULONG index;

    if (!state->fenceCount)
        return;
    --state->fenceCount;
    for (index = 0; index < state->fenceCount; ++index) {
        state->fenceValue[index] = state->fenceValue[index + 1];
        state->fenceSerial[index] = state->fenceSerial[index + 1];
        state->fenceMask[index] = state->fenceMask[index + 1];
    }
    RecomputeQueuedMask(state);
    state->semanticFence = state->fenceCount ? state->fenceValue[0] : 0;
}

static void ClearFences(MGLPPCHostState *state)
{
    state->fenceCount = 0;
    state->queuedSegmentMask = 0;
    state->semanticFence = 0;
}

/* W-80: close an open fence-coalescing run. Every drain needs a fence that
 * covers all submitted dispatches, including the fence-less tail, so submit
 * a fence-only kick when something is pending. In-order CP execution makes
 * that fence cover every earlier dispatch. */
static BOOL CloseFenceGroup(MGLPPCHostState *state)
{
    ULONG fence = 0;

    if (!state->fenceCoalesce || !state->pendingUnfenced)
        return TRUE;
    if (!state->ringDeviceToken ||
        !Radeon3DSubmitFence((struct Radeon3DDevice *)state->ringDeviceToken,
                             &fence) || !fence)
        return FALSE;
    if (!RecordFence(state, state->ringCompleted, fence,
                     state->pendingSegMask)) {
        state->cpFailed = TRUE;
        return FALSE;
    }
    state->pendingSegMask = 0;
    state->pendingUnfenced = 0;
    state->fenceGroupCount = 0;
    return TRUE;
}

/* Blocking drain of every in-flight submission, oldest first. PRESENT,
 * teardown and every path that needs GPU quiescence (texture BEGIN, readback,
 * inline semantic execute) uses this. */
static BOOL DrainSemanticFence(MGLPPCHostState *state)
{
    MGLR200PrivateBridgeV1 bridge;

    /* A pending fence-less run must be closed before the queue can be
     * considered complete. */
    if (!CloseFenceGroup(state))
        return FALSE;
    if (!state->fenceCount) {
        state->queuedSegmentMask = 0;
        AdvanceRetired(state);
        return TRUE;
    }
    if (!state->context || !QueryBackendBridge(&bridge))
        return FALSE;
    while (state->fenceCount) {
        if (!Radeon3DWaitFence((struct Radeon3DDevice *)bridge.deviceToken,
                               state->fenceValue[0], 1000UL))
            return FALSE;
        PopFence(state);
    }
    AdvanceRetired(state);
    return TRUE;
}

static BOOL DrainForSegment(MGLPPCHostState *state, ULONG segmentIx)
{
    ULONG bit = 1UL << segmentIx;

    /* A fence-less run may still reference this segment even though no
     * queued fence names it; draining closes the run first. */
    if (state->pendingUnfenced && (state->pendingSegMask & bit))
        return DrainSemanticFence(state);
    if (!(state->ringRadeonCaps & RADEON3D_CAP_ORDERED_COMMITS) ||
        (state->queuedSegmentMask & bit))
        return DrainSemanticFence(state);
    return TRUE;
}

static void PollQueuedFence(MGLPPCHostState *state)
{
    ULONG fullMask;

    if (!state->ringDeviceToken || !state->vertexSegmentCount ||
        !state->ringControl ||
        state->ringCompleted != state->ringControl->head)
        return;
    /* No fence outstanding: consumption alone retires the ring. Without
     * this, a client parked in the segment-rotate wait deadlocks whenever
     * no further ring entry arrives to trigger DrainForSegment -- which is
     * exactly the end-of-frame condition. */
    if (!state->fenceCount) {
        AdvanceRetired(state);
        return;
    }
    if (!state->cpEnabled && !(state->ringRadeonCaps & RADEON3D_CAP_ORDERED_COMMITS)) {
        /* Without ordered commits every entry executes synchronously on
         * this task, so a drained ring means all reads are done. */
        ClearFences(state);
        AdvanceRetired(state);
        return;
    }
    fullMask = (1UL << state->vertexSegmentCount) - 1UL;
    if (!state->cpEnabled && (state->queuedSegmentMask & fullMask) != fullMask &&
        !(state->queuedSegmentMask))
        return;
    /* Non-blocking retirement over the contiguous prefix. Submissions are
     * ordered, so testing the oldest is enough to advance `retired`; this
     * only works for every queued fence when the driver validates a range
     * (MULTI_FENCE); otherwise the queue holds at most one entry. */
    while (state->fenceCount &&
           Radeon3DTestFence(
               (struct Radeon3DDevice *)state->ringDeviceToken,
               state->fenceValue[0]))
        PopFence(state);
    AdvanceRetired(state);
}

/* Cheap shutdown counts are not activity latches or benchmark timings. */
static ULONG HostExecuteCount;
static ULONG HostPresentCount;

/* Per-Execute stage attribution. Diagnostics only: EClock reads are outside
 * the protocol and cost a few microseconds each. */
#if MGLPPC_PROFILE
static ULONG HostClockHz;
static ULONG HostWaitTicks;
static ULONG HostClearTicks;
static ULONG HostCrcTicks;
static ULONG HostQueryTicks;
static ULONG HostSubmitTicks;
static ULONG HostPresentWaitTicks;
static ULONG HostPresentTicks;
static ULONG HostTotalTicks;
static ULONG HostExecuteMsgCount;
static ULONG HostBarrierCount;
static ULONG HostBarrierTicks;
static ULONG HostControlCount;
static ULONG HostControlTicks;
static ULONG HostRingTicks;
static ULONG HostMailboxCount;
static ULONG HostMailboxTicks;
/* U0 direct-upload handler split. BEGIN's handler time includes its
 * DrainSemanticFence; COMMIT deliberately performs no drain. */
static ULONG HostDirectBeginCount;
static ULONG HostDirectBeginTicks;
static ULONG HostDirectCommitCount;
static ULONG HostDirectCommitTicks;
/* Batched two-tile rounds: BEGIN2/COMMIT2 message counts, profiled builds. */
static ULONG HostDirectBegin2Count;
static ULONG HostDirectBegin2Ticks;
static ULONG HostDirectCommit2Count;
static ULONG HostDirectCommit2Ticks;
static ULONG HostIdleTicks;
static ULONG HostWallStart;
static ULONG HostWallTicks;
static ULONG HostPollLoops;
static ULONG HostMailboxPollTicks, HostMailboxPollCalls;
static ULONG HostFencePollTicks, HostFencePollCalls;
static ULONG HostEventPollTicks, HostEventPollCalls;
/* CP dispatch attribution: per-entry drain (pending fence wait), bridge
 * query + indirect submit, and whole-entry wall time. Profiled builds only. */
static ULONG HostCpEntries;
static ULONG HostCpDrainTicks;
static ULONG HostCpDispatchTicks;
static ULONG HostCpTotalTicks;
/* Subimage stage attribution (MGLPPC-profile builds only). */
static ULONG HostSubimageCount;
static ULONG HostSubimageTicks;
static ULONG HostSubimagePreflightTicks;
static ULONG HostSubimageNativeTicks;
static ULONG HostSubimageBridgeTicks;
static GLenum HostNativeEnableError;
static BOOL HostNativeReported;

static ULONG HostClock(void)
{
    struct EClockVal clockValue;
    ULONG frequency;

    frequency = ReadEClock(&clockValue);
    if (!HostClockHz)
        HostClockHz = frequency;
    return clockValue.ev_lo;
}

static const struct { GLenum name; const char *label; } HostNativeCounters[] = {
    { MGL_PERF_CLOCK_RATE, "clock_hz" },
    { MGL_PERF_FRAMES, "frames" },
    { MGL_PERF_SERIALIZE_TICKS, "serialize_ticks" },
    { MGL_PERF_EXECUTE_TICKS, "execute_ticks" },
    { MGL_PERF_WAIT_TICKS, "wait_ticks" },
    { MGL_PERF_PRESENT_TICKS, "present_ticks" },
    { MGL_PERF_TEXTURE_RESIDENCY_TICKS, "residency_ticks" },
    { MGL_PERF_TEXTURE_UPLOAD_TICKS, "upload_ticks" },
    { MGL_PERF_TEXTURE_UPLOAD_BYTES, "upload_bytes" },
    { MGL_PERF_TEXTURE_UPLOAD_COPIES, "upload_copies" },
    { MGL_PERF_TEXTURE_UPLOAD_UPDATES, "upload_updates" },
    { MGL_PERF_TEXTURE_UPLOAD_COALESCED, "upload_coalesced" },
    { MGL_PERF_TEXTURE_UPLOAD_BARRIERS, "upload_barriers" },
    { MGL_PERF_TEXTURE_UPLOAD_DEFERRED, "upload_deferred" },
    { MGL_PERF_TEXTURE_EVICTIONS, "texture_evictions" },
    { MGL_PERF_SUBMITS, "submits" },
    { MGL_PERF_ASYNC_SUBMITS, "async_submits" },
    { MGL_PERF_FENCE_WAITS, "fence_waits" },
    { MGL_PERF_FENCE_POLL_HITS, "fence_poll_hits" },
    { MGL_PERF_TEXTURE_WAITS, "texture_waits" },
    { MGL_PERF_BUFFER_WAITS, "buffer_waits" },
    { MGL_PERF_DRAIN_WAITS, "drain_waits" },
    { MGL_PERF_EXECUTE_FAILURES, "execute_failures" },
    { MGL_PERF_WAIT_FAILURES, "wait_failures" }
};
enum { HOST_NATIVE_COUNTER_COUNT =
    sizeof(HostNativeCounters) / sizeof(HostNativeCounters[0]) };
static struct HostNativeProfileRecord {
    ULONG contextId;
    GLenum enableError, pendingError;
    GLint values[HOST_NATIVE_COUNTER_COUNT];
    GLenum queryErrors[HOST_NATIVE_COUNTER_COUNT];
} HostNativeRecords[8];
static ULONG HostNativeRecordCount, HostNativeOverflow;

static void HostNativeProfileCapture(const MGLPPCHostState *state)
{
    struct HostNativeProfileRecord *record;
    ULONG index;

    if (!state->context || HostNativeReported)
        return;
    /* Keep the per-context guard even for an overflowed snapshot. */
    HostNativeReported = TRUE;
    if (HostNativeRecordCount >=
        sizeof(HostNativeRecords) / sizeof(HostNativeRecords[0])) {
        ++HostNativeOverflow;
        return;
    }
    record = &HostNativeRecords[HostNativeRecordCount++];
    record->contextId = state->contextId;
    record->enableError = HostNativeEnableError;
    /* Teardown only, before DiscardSemantic can clear a pending GL error.
     * Separate rendering errors from query errors without doing any I/O. */
    record->pendingError = glGetError();
    for (index = 0; index < HOST_NATIVE_COUNTER_COUNT; ++index) {
        record->values[index] = 0;
        glGetIntegerv(HostNativeCounters[index].name, &record->values[index]);
        record->queryErrors[index] = glGetError();
    }
}

static void HostNativeProfileReport(void)
{
    ULONG slot, index;

    printf("HOST_NATIVE cache_capacity=%lu records=%lu overflow_contexts=%lu "
           "overflow_policy=drop_new output=after_cleanup\n",
           (unsigned long)(sizeof(HostNativeRecords) / sizeof(HostNativeRecords[0])),
           (unsigned long)HostNativeRecordCount, (unsigned long)HostNativeOverflow);
    for (slot = 0; slot < HostNativeRecordCount; ++slot) {
        const struct HostNativeProfileRecord *record = &HostNativeRecords[slot];
        ULONG queryErrors = 0;

        printf("HOST_NATIVE context=%lu scope=whole_context_including_setup "
               "end=before_teardown counter_bits=32 "
               "coverage=native_backend_only excludes=host_direct_service_calls "
               "overlaps_host_stages=1 "
               "enable_error=%lu pending_gl_error=%lu\n",
               (unsigned long)record->contextId,
               (unsigned long)record->enableError,
               (unsigned long)record->pendingError);
        for (index = 0; index < HOST_NATIVE_COUNTER_COUNT; ++index) {
            if (record->queryErrors[index] != GL_NO_ERROR)
                ++queryErrors;
            printf("HOST_NATIVE context=%lu %s=%lu query_error=%lu\n",
                   (unsigned long)record->contextId,
                   HostNativeCounters[index].label,
                   (unsigned long)(ULONG)record->values[index],
                   (unsigned long)record->queryErrors[index]);
        }
        printf("HOST_NATIVE context=%lu query_errors=%lu\n",
               (unsigned long)record->contextId, (unsigned long)queryErrors);
    }
    fflush(stdout);
}
#endif

/* The stdout redirect dies in the FFS cache on every hard reset of a
 * wedged session -- exactly when the evidence matters. This companion
 * logger opens/appends/closes per line, so the file survives and stays
 * readable while the host lives. */
static void HostTraceOpen(void)
{
#if MGLPPC_HOST_TRACE
    BPTR fh = Open((STRPTR)"DH2:wosbuild/mglppc_host.log",
                   MODE_NEWFILE);
    if (fh)
        Close(fh);
#endif
}

static void HostTraceLine(const char *line)
{
#if MGLPPC_HOST_TRACE
    BPTR fh = Open((STRPTR)"DH2:wosbuild/mglppc_host.log",
                   MODE_OLDFILE);
    if (!fh)
        return;
    Seek(fh, 0, OFFSET_END);
    Write(fh, (APTR)line, (LONG)strlen(line));
    Close(fh);
#else
    (void)line;
#endif
}

#if MGLPPC_PROFILE
#define HOST_START(stage) do { (stage) = HostClock(); } while (0)
#define HOST_STAGE(field, stage) \
    do { ULONG now_ = HostClock(); (field) += now_ - (stage); (stage) = now_; } \
    while (0)
#else
#define HOST_START(stage) do { } while (0)
#define HOST_STAGE(field, stage) do { } while (0)
#endif

static BOOL DiscardSemantic(MGLPPCHostState *state, BOOL force)
{
    /* A lifecycle request is the producer's quiescence handshake. Never
     * release GPU-readable leases after an error or an unconsumed publish. */
    BOOL ok = TRUE;
    if (state->cpSegmentLeased &&
        (state->cpFailed || (state->ringControl &&
         (state->ringControl->error || state->ringCompleted != state->ringControl->head)) ||
         !DrainSemanticFence(state))) {
        if (!force)
            return FALSE;
        /* Forced teardown (DESTROY_CONTEXT after a dead ring): proceed
         * best-effort. Bailing here would hold the context -- and with it
         * the display and the port -- until the next cold boot. The bounded
         * fence drain in the body below is the best remaining attempt, and
         * a GPU that ignores fences only recovers by reboot either way. */
        ok = FALSE;
    }
    /* Main loop drains published mailbox work before any lifecycle message. */
    state->mailbox = NULL;
    (void)DrainSemanticFence(state);
    if (state->cpSegmentLeased) {
        ULONG index;
        for (index = 0; index < state->vertexSegmentCount; ++index)
            (void)Radeon3DFreeSegment((struct Radeon3DDevice *)state->ringDeviceToken,
                                     state->vertexSegmentIds[index]);
        (void)Radeon3DFreeSegment((struct Radeon3DDevice *)state->ringDeviceToken,
                                 state->cpSegment.Id);
        state->cpSegmentLeased = FALSE;
    }
    state->cpEnabled = FALSE;
    if (state->displayLocked) {
        mglUnlockDisplay();
        state->displayLocked = 0;
    }
    if (state->ringControl) {
        volatile MGLPPCRingControlV1 *control = state->ringControl;
        ULONG spin = 0;
        /* The PPC stops posting at error; bounded wait covers a client
         * that died mid-session. */
        while (control->completed != control->head && spin < 5000000UL)
            ++spin;
        control->error = 1;
        state->ringControl = NULL;
        state->ringCompleted = 0;
        state->hostEventsEnabled = 0;
    }
    /* A registration can acquire a lease before its CPU view is validated. */
    if (state->context && state->ringSegmentLeased) {
        MGLR200PrivateSegmentFreeV1 segmentFree;
        memset(&segmentFree, 0, sizeof(segmentFree));
        segmentFree.segmentId = state->ringSegmentId;
        glGetIntegerv(MGL_R200_PRIVATE_SEGMENT_FREE_V1_QUERY,
                      (GLint *)&segmentFree);
        (void)glGetError();
    }
    state->ringSegmentId = 0;
    state->ringSegmentLeased = 0;
    state->indirectProbeOk = 0;
    memset(state->semanticSlots, 0, sizeof(state->semanticSlots));
    memset(state->semanticGeneration, 0, sizeof(state->semanticGeneration));
    state->semanticRegistered = 0;
    /* Keep registration epochs monotonic across discard/recreate, like ringEpoch. */
    ClearFences(state);
    state->semanticExecutes = 0;
    state->ringDeviceToken = 0;
    state->ringDeviceGeneration = 0;
    state->ringRadeonCaps = 0;
    state->fenceCoalesce = FALSE;
    state->fenceGroupCount = 0;
    state->pendingSegMask = 0;
    state->pendingUnfenced = 0;
    state->queuedSegmentMask = 0;
    state->vertexSegmentCount = 0;
    if (state->semanticDriverOpen) {
        CloseLibrary(Radeon9200Base);
        Radeon9200Base = NULL;
        state->semanticDriverOpen = 0;
    }
    return ok;
}

static BOOL RegisterSemanticSlots(MGLPPCMessage *message,
                                  MGLPPCHostState *state)
{
    ULONG index;

    if (!state->context || message->contextId != state->contextId) {
        message->status = MGLPPC_STATUS_NO_CONTEXT;
        return FALSE;
    }
    /* Fullscreen contexts register the same way: the slot buffers are
     * client memory and the backend owns target selection and presents. */
    if (state->semanticRegistered ||
        message->value[2] != MGLPPC_SEMANTIC_SLOT_DWORDS) {
        message->status = state->semanticRegistered ? MGLPPC_STATUS_BUSY
                                                    : MGLPPC_STATUS_UNSUPPORTED;
        return FALSE;
    }
    for (index = 0; index < MGLPPC_SEMANTIC_SLOT_COUNT; ++index) {
        /* -amiga-align gives embedded ULONG arrays two-byte alignment.
         * Execute copies the byte stream into service-owned aligned staging
         * before parsing, so an even source address is the real requirement. */
        if (!message->value[index] || (message->value[index] & 1UL)) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            return FALSE;
        }
    }
    if (message->value[0] == message->value[1]) {
        message->status = MGLPPC_STATUS_BAD_PAYLOAD;
        return FALSE;
    }
    if (Radeon9200Base != NULL) {
        message->status = MGLPPC_STATUS_BUSY;
        return FALSE;
    }
    Radeon9200Base = OpenLibrary((CONST_STRPTR)"Radeon9200.chip",
                                 RADEON3D_LIBRARY_VERSION);
    if (!Radeon9200Base) {
        message->status = MGLPPC_STATUS_UNSUPPORTED;
        return FALSE;
    }
    state->semanticDriverOpen = 1;
    for (index = 0; index < MGLPPC_SEMANTIC_SLOT_COUNT; ++index)
        state->semanticSlots[index] = (ULONG *)message->value[index];
    state->semanticEpoch++;
    if (!state->semanticEpoch)
        state->semanticEpoch = 1;
    state->semanticRegistered = 1;
    state->semanticExecutes = 0;
    message->value[0] = state->semanticEpoch;
    return TRUE;
}

static BOOL QueryBackendIndirect(void)
{
    MGLR200PrivateIndirectDispatchV1 probe;
    memset(&probe, 0, sizeof(probe));
    probe.flags = MGL_R200_PRIVATE_INDIRECT_PROBE;
    (void)glGetError();
    glGetIntegerv(MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY,
                  (GLint *)&probe);
    return glGetError() == GL_NO_ERROR &&
           probe.version == MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_VERSION &&
           probe.structSize == sizeof(probe) &&
           probe.flags == MGL_R200_PRIVATE_INDIRECT_SUPPORTED &&
           !probe.fenceOut;
}

static BOOL QueryCpSurface(MGLPPCHostState *state, ULONG token,
                           MGLR200PrivateSurfaceV1 *surface)
{
    memset(surface, 0, sizeof(*surface));
    surface->surfaceToken = token;
    (void)glGetError();
    glGetIntegerv(MGL_R200_PRIVATE_SURFACE_V1_QUERY, (GLint *)surface);
    return glGetError() == GL_NO_ERROR &&
        surface->version == MGL_R200_PRIVATE_SURFACE_V1_VERSION &&
        surface->structSize == sizeof(*surface) && surface->surfaceToken == token &&
        surface->deviceEpoch == state->ringDeviceEpoch &&
        surface->generation == state->ringDeviceGeneration && surface->cpuAddress;
}

static BOOL RegisterSemanticRing(MGLPPCMessage *message,
                                 MGLPPCHostState *state)
{
    volatile MGLPPCRingControlV1 *control;
    MGLR200PrivateSegmentV1 segment;
    MGLR200PrivateBridgeV2 bridge;
    struct Radeon3DInfo info;
    ULONG count = message->value[0];
    ULONG slotDwords = message->value[1];
    ULONG index;
    BOOL cpRequested = (message->value[3] & MGLPPC_RING_REGISTER_CP_EMIT) != 0;

    /* A failed first registration must never leave an enabled mailbox. A
     * duplicate registration preserves the live session and is rejected. */
    if (!state->ringControl)
        state->mailbox = NULL;

    if (!state->context || message->contextId != state->contextId) {
        message->status = MGLPPC_STATUS_NO_CONTEXT;
        return FALSE;
    }
    if (state->semanticRegistered || state->ringControl) {
        message->status = MGLPPC_STATUS_BUSY;
        return FALSE;
    }
    if (count != MGLPPC_RING_SLOTS ||
        slotDwords != MGLPPC_SEMANTIC_SLOT_DWORDS ||
        message->payloadWords != count) {
        message->status = MGLPPC_STATUS_BAD_PAYLOAD;
        return FALSE;
    }
    /* Fullscreen is allowed: ring control v6 publishes the live color target
     * after every display switch, and the PPC stamps each CP batch with it.
     * Windowed contexts keep the stable surfaces[0] target. */
    if (cpRequested && (message->protocolVersion != MGLPPC_PROTOCOL_VERSION ||
        (message->value[3] & MGLPPC_RING_REGISTER_INDIRECT))) {
        message->status = MGLPPC_STATUS_UNSUPPORTED;
        return FALSE;
    }
    for (index = 0; index < count; ++index) {
        if (!message->payload[index] || (message->payload[index] & 1UL)) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            return FALSE;
        }
    }
    if (Radeon9200Base != NULL) {
        message->status = MGLPPC_STATUS_BUSY;
        return FALSE;
    }
    Radeon9200Base = OpenLibrary((CONST_STRPTR)"Radeon9200.chip",
                                 RADEON3D_LIBRARY_VERSION);
    if (!Radeon9200Base) {
        message->status = MGLPPC_STATUS_UNSUPPORTED;
        return FALSE;
    }
    state->semanticDriverOpen = 1;
    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    if (!QueryBackendBridgeV2(&bridge) ||
        !Radeon3DGetInfo((struct Radeon3DDevice *)bridge.deviceToken,
                         &info) || !info.Generation) {
        message->status = MGLPPC_STATUS_UNSUPPORTED;
        goto failed;
    }
    state->ringDeviceGeneration = info.Generation;
    state->ringDeviceToken = bridge.deviceToken;
    state->ringRadeonCaps = info.Caps;
    /* W-80: coalescing needs the new driver capability AND an explicit
     * group size above 1. Default is 1 (fence every dispatch). */
    state->fenceCoalesce =
        (state->fenceGroupSize > 1UL &&
         (info.Caps & RADEON3D_CAP_FENCE_COALESCE) != 0) ? TRUE : FALSE;
    state->queuedSegmentMask = 0;
    state->indirectProbeOk =
        (message->value[3] & MGLPPC_RING_REGISTER_INDIRECT) &&
        (info.Caps & RADEON3D_CAP_INDIRECT_DISPATCH) && QueryBackendIndirect();
    if (cpRequested && (info.Caps & (RADEON3D_CAP_INDIRECT_DISPATCH |
                                   RADEON3D_CAP_INDIRECT_RENDER)) !=
                                  (RADEON3D_CAP_INDIRECT_DISPATCH |
                                   RADEON3D_CAP_INDIRECT_RENDER)) {
        message->status = MGLPPC_STATUS_UNSUPPORTED;
        goto failed;
    }
    if (MGLPPC_HOST_TRACE || state->verboseUploads) {
        printf("MiniGLPPC host: ring caps=%08lx ordered=%lu gen=%lu\n",
               (unsigned long)info.Caps,
               (unsigned long)((info.Caps & RADEON3D_CAP_ORDERED_COMMITS)
                               ? 1UL : 0UL),
               (unsigned long)info.Generation);
        fflush(stdout);
    }
#if MGLPPC_HOST_TRACE
    {
        char rep[128];
        sprintf(rep, "host: ring caps=%08lx ordered=%lu gen=%lu\n",
                (unsigned long)info.Caps,
                (unsigned long)((info.Caps & RADEON3D_CAP_ORDERED_COMMITS)
                                ? 1UL : 0UL),
                (unsigned long)info.Generation);
        HostTraceLine(rep);
    }
#endif
    memset(&segment, 0, sizeof(segment));
    segment.bytes = MGLPPC_RING_SEGMENT_BYTES;
    if (MGLPPC_HOST_TRACE || state->verboseUploads) {
        printf("MiniGLPPC host: msg=ring_reg\n");
        fflush(stdout);
    }
    glGetIntegerv(MGL_R200_PRIVATE_SEGMENT_V1_QUERY, (GLint *)&segment);
    state->ringSegmentId = segment.segmentId;
    state->ringSegmentLeased =
        segment.version == MGL_R200_PRIVATE_SEGMENT_V1_VERSION &&
        segment.cpuAddress != 0;
    if (glGetError() != GL_NO_ERROR ||
        segment.version != MGL_R200_PRIVATE_SEGMENT_V1_VERSION ||
        !segment.cpuAddress ||
        segment.bytes < sizeof(MGLPPCRingControlV1)) {
        printf("MiniGLPPC host: ring_seg=fail\n");
        fflush(stdout);
        message->status = MGLPPC_STATUS_UNSUPPORTED;
        goto failed;
    }
    if (MGLPPC_HOST_TRACE || state->verboseUploads) {
        printf("MiniGLPPC host: ring_seg=ok addr=%08lx bytes=%lu\n",
               (unsigned long)segment.cpuAddress,
               (unsigned long)segment.bytes);
        fflush(stdout);
    }
    control = (volatile MGLPPCRingControlV1 *)segment.cpuAddress;
    control->magic = MGLPPC_RING_MAGIC;
    control->version = MGLPPC_RING_CONTROL_VERSION;
    control->slotCount = count;
    control->slotDwords = slotDwords;
    state->ringEpoch++;
    if (!state->ringEpoch)
        state->ringEpoch = 1;
    control->epoch = state->ringEpoch;
    control->head = 0;
    control->completed = 0;
    control->error = 0;
    control->presentSeq = 0;
    control->retired = 0;
    ClearFences(state);
    control->eventHead = 0;
    control->eventTail = 0;
    control->syncEnabled = 0;
    control->syncSeq = 0;
    state->ringSyncSeq = 0;
    for (index = 0; index < count; ++index) {
        control->slotAddrs[index] = message->payload[index];
        /* Discard dirty cache state from a previous owner before the PPC's
         * first publication. Registration has not granted producer access yet. */
        CacheClearE((APTR)message->payload[index], slotDwords * sizeof(ULONG),
                    CACRF_ClearD);
    }
    state->ringControl = control;
    state->ringDeviceEpoch = segment.deviceEpoch;
    state->ringCompleted = 0;
    memset(state->ringLastGen, 0, sizeof(state->ringLastGen));
    state->semanticRegistered = 1;
    state->semanticExecutes = 0;
    state->hostEventsEnabled =
        (message->value[3] & MGLPPC_RING_REGISTER_HOST_EVENTS) != 0;
    state->droppedEvents = 0;
    if (!state->mailboxDisabled &&
        message->protocolVersion == MGLPPC_PROTOCOL_VERSION &&
        (message->value[3] & MGLPPC_RING_REGISTER_MAILBOX) &&
        !(segment.cpuAddress & 31UL) &&
        segment.bytes >= MGLPPC_RING_SEGMENT_BYTES &&
        segment.cpuAddress <= 0xffffffffUL - segment.bytes) {
#ifndef MGLPPC_DISABLE_RING
        volatile MGLPPCMailboxV1 *mailbox = (volatile MGLPPCMailboxV1 *)
            (segment.cpuAddress + MGLPPC_MAILBOX_OFFSET);
        memset((void *)mailbox, 0, sizeof(*mailbox));
        mailbox->magic = MGLPPC_MAILBOX_MAGIC;
        mailbox->version = MGLPPC_MAILBOX_VERSION;
        mailbox->epoch = state->ringEpoch;
        CacheClearE((APTR)mailbox, sizeof(*mailbox), CACRF_ClearD);
        state->mailboxSerial = 0;
        state->mailbox = mailbox;
#endif
    }
    message->value[0] = segment.cpuAddress;
    message->value[1] = state->ringEpoch;
    message->value[3] = state->hostEventsEnabled
        ? MGLPPC_RING_REGISTER_HOST_EVENTS : 0;
    if (state->mailbox)
        message->value[3] |= MGLPPC_RING_REGISTER_MAILBOX;
    if (state->indirectProbeOk) {
        message->value[3] |= MGLPPC_RING_REGISTER_INDIRECT;
        if (state->ibDryRun)
            message->value[3] |= MGLPPC_RING_REGISTER_IB_DRY;
    }
    message->value[4] = segment.bytes;
    state->vertexSegmentCount = 0;
    if (cpRequested) {
        MGLR200PrivateSurfaceV1 surface;
        if (!QueryCpSurface(state, bridge.colorSurfaceToken, &surface)) {
            message->status = MGLPPC_STATUS_UNSUPPORTED;
            goto failed;
        }
        state->cpColorToken = bridge.colorSurfaceToken;
        state->cpColorSeq = 0;
        PublishCpColorTarget(state, bridge.colorSurfaceToken);
        memset(&state->cpSegment, 0, sizeof(state->cpSegment));
        state->cpSegment.Size = sizeof(state->cpSegment);
        if (!Radeon3DAllocSegment((struct Radeon3DDevice *)state->ringDeviceToken,
                                  MGLPPC_CP_SEGMENT_BYTES, &state->cpSegment)) {
            message->status = MGLPPC_STATUS_NO_MEMORY;
            goto failed;
        }
        state->cpSegmentLeased = TRUE;
        if (state->cpSegment.Version != RADEON3D_SEGMENT_VERSION ||
            !state->cpSegment.CpuAddress || ((ULONG)state->cpSegment.CpuAddress & 31UL) ||
            state->cpSegment.Bytes < MGLPPC_CP_SEGMENT_BYTES ||
            (ULONG)state->cpSegment.CpuAddress > 0xffffffffUL - state->cpSegment.Bytes) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            goto failed;
        }
        message->value[5] = (ULONG)state->cpSegment.CpuAddress;
        message->value[6] = state->cpSegment.Id;
        message->value[7] = state->cpSegment.Bytes;
    }
    for (index = 0; index < MGLPPC_VERTEX_SEGMENTS; ++index) {
        MGLR200PrivateSegmentV1 vertexSegment;
        if (cpRequested) {
            struct Radeon3DSegment vertex;
            memset(&vertex, 0, sizeof(vertex));
            vertex.Size = sizeof(vertex);
            if (!Radeon3DAllocSegment((struct Radeon3DDevice *)state->ringDeviceToken,
                                      RADEON3D_MAX_SEGMENT_BYTES, &vertex))
                break;
            state->vertexSegmentIds[state->vertexSegmentCount++] = vertex.Id;
            if (!vertex.CpuAddress || ((ULONG)vertex.CpuAddress & 31UL) ||
                vertex.Bytes != RADEON3D_MAX_SEGMENT_BYTES ||
                (ULONG)vertex.CpuAddress > 0xffffffffUL - vertex.Bytes ||
                vertex.GpuAddress > 0xffffffffUL - vertex.Bytes) {
                message->status = MGLPPC_STATUS_BAD_PAYLOAD;
                goto failed;
            }
            message->payload[index * 4UL] = (ULONG)vertex.CpuAddress;
            message->payload[index * 4UL + 1UL] = vertex.Bytes;
            message->payload[index * 4UL + 2UL] = vertex.Id;
            message->payload[index * 4UL + 3UL] = vertex.GpuAddress;
            continue;
        }
        memset(&vertexSegment, 0, sizeof(vertexSegment));
        vertexSegment.bytes = RADEON3D_MAX_SEGMENT_BYTES;
        glGetIntegerv(MGL_R200_PRIVATE_SEGMENT_V1_QUERY,
                      (GLint *)&vertexSegment);
        if (glGetError() != GL_NO_ERROR ||
            vertexSegment.version != MGL_R200_PRIVATE_SEGMENT_V1_VERSION ||
            !vertexSegment.cpuAddress ||
            vertexSegment.bytes < RADEON3D_MAX_SEGMENT_BYTES / 2UL)
            break;
        state->vertexSegmentIds[state->vertexSegmentCount++] =
            vertexSegment.segmentId;
        message->payload[index * 3UL] = vertexSegment.cpuAddress;
        message->payload[index * 3UL + 1UL] = vertexSegment.bytes;
        message->payload[index * 3UL + 2UL] = vertexSegment.segmentId;
    }
    message->value[2] = state->vertexSegmentCount;
    message->payloadWords = state->vertexSegmentCount * (cpRequested ? 4UL : 3UL);
    if (cpRequested) {
        if (state->vertexSegmentCount != MGLPPC_VERTEX_SEGMENTS) {
            message->status = MGLPPC_STATUS_NO_MEMORY;
            goto failed;
        }
        state->cpEnabled = TRUE;
        state->cpFailed = FALSE;
        message->value[3] |= MGLPPC_RING_REGISTER_CP_EMIT;
    }
    /* Publish the new addresses and epoch before the producer may cache any
     * control lines (entry zero shares a line with the last slot addresses). */
    CacheClearE((APTR)control, sizeof(*control), CACRF_ClearD);
    if (state->indirectProbeOk) {
        char line[160];
        sprintf(line, "IBHOST registered mode=%s ctrl=%08lx segment=%lu epoch=%lu\n",
                state->ibDryRun ? "dry" : "gpu", (unsigned long)control,
                (unsigned long)state->ringSegmentId, (unsigned long)state->ringEpoch);
        MGLPPCIBTrace("DH2:ibhost.log", line);
    }
    if (MGLPPC_HOST_TRACE || state->verboseUploads) {
        printf("MiniGLPPC host: ring_init_done vertex_segments=%lu\n",
               (unsigned long)state->vertexSegmentCount);
        fflush(stdout);
    }
    return TRUE;

failed:
    /* Only reached after this attempt opened the driver. Live registrations
     * are rejected above, before acquiring or releasing any resources. */
    (void)DiscardSemantic(state, FALSE);
    return FALSE;
}

static void PollContextEvents(MGLPPCHostState *state)
{
    volatile MGLPPCRingControlV1 *control = state->ringControl;
    struct IntuiMessage *message;

    if (!control || !state->hostEventsEnabled)
        return;
    /* Resolve the window lazily: the 68k library created it during
     * CREATE_CONTEXT, and polling it here (instead of inside the create
     * reply) keeps the keyboard path out of the create transaction. */
    if (!state->contextWindow)
        state->contextWindow = (struct Window *)mglGetWindowHandle();
    if (!state->contextWindow)
        return;
    while ((message = (struct IntuiMessage *)
            GetMsg(state->contextWindow->UserPort)) != NULL) {
        ULONG head = control->eventHead;
        ULONG index;
        CacheClearE((APTR)&control->eventTail, sizeof(control->eventTail),
                    CACRF_ClearD);
        if (head - control->eventTail < MGLPPC_EVENT_SLOTS) {
            index = head % MGLPPC_EVENT_SLOTS;
            control->eventClass[index] = message->Class;
            control->eventCode[index] = message->Code;
            CacheClearE((APTR)&control->eventClass[index],
                        sizeof(control->eventClass[index]), CACRF_ClearD);
            CacheClearE((APTR)&control->eventCode[index],
                        sizeof(control->eventCode[index]), CACRF_ClearD);
            control->eventHead = head + 1UL;
            CacheClearE((APTR)&control->eventHead,
                        sizeof(control->eventHead), CACRF_ClearD);
        } else if (++state->droppedEvents == 1UL) {
            printf("MiniGLPPC host: event queue overflow\n");
            fflush(stdout);
        }
        ReplyMsg((struct Message *)message);
    }
}

/* Consume posted ring entries. The control block is non-cacheable on both
 * CPUs, so head/completed/entry reads need no maintenance; record payloads
 * are cacheable and get the same dcbf/CacheClearE discipline as the v10
 * message path. Returns elapsed ticks (at least one for processed work) with
 * profiling, or the processed count without it; zero means no work. */
static BOOL ExecuteIndirectSmoke(MGLPPCHostState *state,
                                  const MGLPPCRingEntryV1 *entry,
                                  ULONG *slotAddr)
{
    MGLR200PrivateIndirectDispatchV1 dispatch;
    MGLR200PrivateBridgeV2 bridge;
    volatile ULONG *words;
    ULONG offset, count, i;
    char line[256];
    const char *failure = "descriptor";

    /* This is a single startup smoke, not the future general IB producer. */
    if (!state->indirectProbeOk || !state->ringSegmentLeased ||
        entry->flags != MGLPPC_SEMANTIC_FLAG_INDIRECT || entry->dwords != 2UL ||
        state->ringCompleted || state->ringControl->head != 1UL)
        goto failed;
    CacheClearE(slotAddr, 2UL * sizeof(ULONG), CACRF_ClearD);
    offset = slotAddr[0];
    count = slotAddr[1];
    if (offset != MGLPPC_IB_OFFSET || count != MGLPPC_IB_SMOKE_DWORDS)
        goto failed;
    failure = "epoch_or_drain";
    if (!QueryBackendBridgeV2(&bridge) ||
        bridge.deviceEpoch != state->ringDeviceEpoch || !DrainSemanticFence(state))
        goto failed;
    words = (volatile ULONG *)((UBYTE *)state->ringControl + offset);
    CacheClearE((APTR)words, count * sizeof(ULONG), CACRF_ClearD);
    sprintf(line, "IBHOST payload slot=%lu offset=%lu dwords=%lu generation=%lu mode=%s\n",
            (unsigned long)entry->slot, (unsigned long)offset,
            (unsigned long)count, (unsigned long)entry->generation,
            state->ibDryRun ? "dry" : "gpu");
    MGLPPCIBTrace("DH2:ibhost.log", line);
    /* One fixed smoke is checked before trusting any PPC-authored GPU work. */
    for (i = 0; i < count; ++i) {
        ULONG expected = i == 0 ? 0x79050000UL :
                         i == 1 ? 0xefbeaddeUL : 0x00000080UL;
        if (words[i] != expected) {
            sprintf(line, "IBHOST bytes_fail word=%lu got=%08lx expected=%08lx\n",
                    (unsigned long)i, (unsigned long)words[i],
                    (unsigned long)expected);
            MGLPPCIBTrace("DH2:ibhost.log", line);
            return FALSE;
        }
    }
    MGLPPCIBTrace("DH2:ibhost.log", "IBHOST bytes=79050000,efbeadde,00000080x6 ok\n");
    if (!state->ibDryRun) {
        memset(&dispatch, 0, sizeof(dispatch));
        dispatch.version = MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_VERSION;
        dispatch.structSize = sizeof(dispatch);
        dispatch.segmentId = state->ringSegmentId;
        dispatch.byteOffset = offset;
        dispatch.dwordCount = count;
        (void)glGetError();
        glGetIntegerv(MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY,
                      (GLint *)&dispatch);
        failure = "dispatch";
        if (glGetError() != GL_NO_ERROR || !dispatch.fenceOut)
            goto failed;
        sprintf(line, "IBHOST submitted fence=%lu\n", (unsigned long)dispatch.fenceOut);
        MGLPPCIBTrace("DH2:ibhost.log", line);
        failure = "retirement";
        if (!RecordFence(state, state->ringCompleted, dispatch.fenceOut, 0) ||
            !DrainSemanticFence(state))
            goto failed;
        MGLPPCIBTrace("DH2:ibhost.log", "IBHOST gpu_retired=1\n");
    } else {
        MGLPPCIBTrace("DH2:ibhost.log", "IBHOST dry_validated=1 gpu_submitted=0\n");
    }
    state->ringLastGen[entry->slot] = entry->generation;
    return TRUE;

failed:
    sprintf(line, "IBHOST failed stage=%s no_ack=1\n", failure);
    MGLPPCIBTrace("DH2:ibhost.log", line);
    return FALSE;
}

/* Diagnostics: which ExecuteCpEntry check rejected the entry. Set on every
 * failure path so the caller can print the stage; failure-only, so quiet
 * runs are unaffected. */
static const char *CpFailStage;

static BOOL ExecuteCpEntry(MGLPPCHostState *state,
                           const MGLPPCRingEntryV1 *entry, ULONG *payload)
{
    struct Radeon3DIndirect indirect;
    MGLR200PrivateBridgeV2 bridge;
    ULONG mask, fence = 0;
    BOOL wantFence = TRUE;
#if MGLPPC_PROFILE
    ULONG cpStage, cpEntryStart;
#endif
    CpFailStage = "state";
    if (!state->cpEnabled || state->cpFailed || !state->cpSegmentLeased ||
        entry->flags != MGLPPC_SEMANTIC_FLAG_CP_EMIT || entry->dwords != 3UL ||
        entry->slot != state->ringCompleted % MGLPPC_RING_SLOTS)
        return FALSE;
    CpFailStage = "payload";
    CacheClearE(payload, 3UL * sizeof(ULONG), CACRF_ClearD);
    memset(&indirect, 0, sizeof(indirect));
    indirect.Size = sizeof(indirect);
    indirect.Version = RADEON3D_INDIRECT_VERSION;
    indirect.SegmentId = state->cpSegment.Id;
    indirect.ByteOffset = payload[0];
    indirect.DwordCount = payload[1];
    mask = payload[2];
    if (indirect.ByteOffset != entry->slot * MGLPPC_CP_SLICE_BYTES ||
        !indirect.DwordCount || (indirect.DwordCount & 1UL) ||
        indirect.DwordCount > RADEON3D_MAX_BATCH_DWORDS ||
        indirect.ByteOffset >= state->cpSegment.Bytes ||
        indirect.DwordCount > (state->cpSegment.Bytes - indirect.ByteOffset) / 4UL ||
        (mask & ~((1UL << state->vertexSegmentCount) - 1UL)))
        return FALSE;
    HOST_START(cpEntryStart);
    HOST_START(cpStage);
    /* Only a driver that validates ranged fences lets several submissions
     * stay in flight. Without the cap this drains every time, exactly the
     * validated single-outstanding-fence model. */
    if (state->fenceCount >= MGLPPC_INFLIGHT_SLOTS ||
        !(state->ringRadeonCaps & RADEON3D_CAP_MULTI_FENCE)) {
        CpFailStage = "drain";
        if (!DrainSemanticFence(state)) {
            HOST_STAGE(HostCpDrainTicks, cpStage);
            return FALSE;
        }
    }
    HOST_STAGE(HostCpDrainTicks, cpStage);
    /* W-80 fence decision, after any drain (which may have closed a run and
     * reset the counter): a dispatch fences when coalescing is off or this
     * dispatch would complete a group. */
    wantFence = (!state->fenceCoalesce ||
                 state->fenceGroupCount + 1UL >= state->fenceGroupSize) ?
                TRUE : FALSE;
    CpFailStage = "bridge";
    if (!QueryBackendBridgeV2(&bridge) ||
        bridge.deviceEpoch != state->ringDeviceEpoch ||
        bridge.deviceToken != state->ringDeviceToken) {
        CpFailStage = "dispatch";
        return FALSE;
    }
    if (!wantFence)
        indirect.Flags = RADEON3D_INDIRECT_NO_FENCE;
    if (!Radeon3DDispatchIndirect((struct Radeon3DDevice *)state->ringDeviceToken,
                                  &indirect, &fence) ||
        (wantFence && !fence)) {
        CpFailStage = "dispatch";
        return FALSE;
    }
    HOST_STAGE(HostCpDispatchTicks, cpStage);
    CpFailStage = "record";
    if (wantFence) {
        mask |= state->pendingSegMask;
        state->pendingSegMask = 0;
        state->pendingUnfenced = 0;
        state->fenceGroupCount = 0;
        if (!RecordFence(state, state->ringCompleted, fence, mask))
            return FALSE;
    } else {
        state->pendingSegMask |= mask;
        ++state->pendingUnfenced;
        ++state->fenceGroupCount;
    }
    /* Sanity only: the PPC stamped the target it observed through the v6
     * publication. A mismatch means the native drawing buffer moved between
     * the PPC's last read and this dispatch (for example an out-of-band
     * retarget). The stream is already built, so keep the session alive,
     * refresh the publication and report once. Do NOT latch cpFailed here. */
    if (bridge.colorSurfaceToken != state->cpColorToken) {
        state->cpColorToken = bridge.colorSurfaceToken;
        PublishCpColorTarget(state, bridge.colorSurfaceToken);
        if (!state->cpTokenMismatchReported) {
            state->cpTokenMismatchReported = 1;
            printf("MiniGLPPC host: cp_token_mismatch live=%08lx seq=%lu\n",
                   (unsigned long)bridge.colorSurfaceToken,
                   (unsigned long)state->cpColorSeq);
            fflush(stdout);
        }
    }
    state->ringLastGen[entry->slot] = entry->generation;
    ++state->semanticExecutes;
    ++state->cpDispatches;
    state->cpDwords += indirect.DwordCount;
    ++HostExecuteCount;
#if MGLPPC_PROFILE
    ++HostCpEntries;
    HOST_STAGE(HostCpTotalTicks, cpEntryStart);
#endif
    return TRUE;
}

/* Consecutive plain-COMMIT ring entries share one vertex segment until it
 * rotates, so they can ride to the GPU in a single CommitBatch call. The
 * merged call amortizes the per-call service overhead (segment drain, lock,
 * bridge query, fence) and, more importantly, resets the driver emitter's
 * Live-state cache once instead of per entry -- fewer generated CP dwords.
 * Flush triggers: non-plain entry, PRESENT, segment change, capacity, end
 * of queue. Retirement is unaffected: control->retired still advances only
 * after the merged batch's fence drains. */
static ULONG RingAccRecords[MGLPPC_SEMANTIC_SLOT_DWORDS];
static ULONG RingAccOffsets[192UL];
static ULONG RingAccRecordDwords;
static ULONG RingAccDraws;
static ULONG RingAccSegmentIx;
static ULONG RingAccEntries;
static BOOL RingAccValid;

static BOOL FlushCommitAccumulator(MGLPPCHostState *state,
                                   MGLR200PrivateBridgeV2 *bridge,
                                   BOOL *bridgeValid)
{
    struct Radeon3DCommitBatch batch;
    struct Radeon3DInfo info;
    ULONG fence = 0;
#if MGLPPC_PROFILE
    ULONG stageStart;
#endif

    if (!RingAccValid)
        return TRUE;
    HOST_START(stageStart);
    if (!DrainForSegment(state, RingAccSegmentIx)) {
        printf("MiniGLPPC host: acc_drain_fail\n");
        fflush(stdout);
        RingAccValid = FALSE;
        return FALSE;
    }
    HOST_STAGE(HostWaitTicks, stageStart);
    if (!state->displayLocked) {
        if (!mglLockDisplay()) {
            RingAccValid = FALSE;
            return FALSE;
        }
        state->displayLocked = 1;
    }
    HOST_START(stageStart);
    if ((!*bridgeValid && !QueryBackendBridgeV2(bridge)) ||
        bridge->deviceEpoch != state->ringDeviceEpoch) {
        mglUnlockDisplay();
        state->displayLocked = 0;
        RingAccValid = FALSE;
        return FALSE;
    }
    *bridgeValid = MGLPPC_HOST_TARGET_CACHE;
    HOST_STAGE(HostQueryTicks, stageStart);
    memset(&batch, 0, sizeof(batch));
    batch.Size = sizeof(batch);
    batch.Version = RADEON3D_COMMIT_BATCH_VERSION;
    batch.SegmentId = state->vertexSegmentIds[RingAccSegmentIx];
    batch.Records = RingAccRecords;
    batch.RecordDwords = RingAccRecordDwords;
    PatchRecordTargets(RingAccRecords, RingAccRecordDwords,
                       bridge->colorSurfaceToken);
    batch.VertexOffsets = RingAccOffsets;
    batch.RecordCount = RingAccDraws;
    batch.Flags = RADEON3D_SUBMIT_FENCE;
    HOST_START(stageStart);
    if (!Radeon3DCommitBatch(
            (struct Radeon3DDevice *)bridge->deviceToken,
            &batch, &fence)) {
        memset(&info, 0, sizeof(info));
        info.Size = sizeof(info);
        if (Radeon3DGetInfo((struct Radeon3DDevice *)bridge->deviceToken,
                            &info) && info.Size >= RADEON3D_INFO_V3_SIZE)
            printf("MiniGLPPC host: acc_commit_fail draws=%lu rec=%lu "
                   "stage=%lu\n",
                   (unsigned long)RingAccDraws,
                   (unsigned long)RingAccRecordDwords,
                   (unsigned long)info.CommitFailStage);
        else
            printf("MiniGLPPC host: acc_commit_fail draws=%lu rec=%lu\n",
                   (unsigned long)RingAccDraws,
                   (unsigned long)RingAccRecordDwords);
        fflush(stdout);
        mglUnlockDisplay();
        state->displayLocked = 0;
        RingAccValid = FALSE;
        return FALSE;
    }
    HOST_STAGE(HostSubmitTicks, stageStart);
    /* This fenced submission also covers any fence-less dispatches of an
     * open W-80 run: fold their segment mask into the record and close the
     * run, or the group counter and pending mask would describe a run that
     * no longer exists. No-op when coalescing is off (pending are zero). */
    {
        ULONG folded = (1UL << RingAccSegmentIx) | state->pendingSegMask;

        if (!RecordFence(state,
                         state->ringCompleted ? state->ringCompleted - 1UL : 0UL,
                         fence, folded)) {
            mglUnlockDisplay();
            state->displayLocked = 0;
            RingAccValid = FALSE;
            return FALSE;
        }
    }
    state->pendingSegMask = 0;
    state->pendingUnfenced = 0;
    state->fenceGroupCount = 0;
    RingAccValid = FALSE;
    return TRUE;
}

/* Set while a mailbox handler is executing. A handler that needs the ring
 * (none do today) must not recurse back into RingPoll's inner mailbox
 * service; ordinary RingPoll execution leaves it zero. */
static BOOL HostRingMailboxActive;
static BOOL PollMailbox(MGLPPCHostState *state);

static ULONG RingPoll(MGLPPCHostState *state)
{
    volatile MGLPPCRingControlV1 *control = state->ringControl;
    MGLR200PrivateBridgeV2 bridge;
    /* No native control handler runs inside this burst. PRESENT can change
     * TargetSurface, so it invalidates below. Returning drops the snapshot
     * before mailbox, lifecycle, resize or other native calls can run. */
    BOOL bridgeValid = FALSE;
#if MGLPPC_PROFILE
    ULONG ringStart;
#endif
    ULONG processed = 0;

    if (!control || control->error)
        return 0;
    if (state->ringCompleted >= control->head)
        return 0;
    HOST_START(ringStart);
    while (state->ringCompleted < control->head) {
        /* Serve a pending upload control between entries. The PPC blocks in
         * its mailbox wait while this host is inside a multi-entry submit
         * burst, and that burst is the dominant begin/commit round-trip
         * latency (U0: BEGIN round trip 4.4 ms of which the host handler is
         * 1.6 ms). No mailbox handler may run while absorbed-but-unsubmitted
         * records still reference texture handles a BEGIN/COMMIT can
         * replace, so the accumulator is flushed first. The mailbox handlers
         * do not enter RingPoll, so this service cannot recurse. */
        if (!HostRingMailboxActive) {
            if (RingAccValid && !control->error) {
                if (!FlushCommitAccumulator(state, &bridge, &bridgeValid))
                    control->error = 1;
            }
            if (!control->error && PollMailbox(state)) {
                ++processed;
                if (state->ringCompleted >= control->head)
                    break;
                if (control->error)
                    break;
            }
        }
        volatile MGLPPCRingEntryV1 *source =
            &control->entries[state->ringCompleted % MGLPPC_RING_SLOTS];
        MGLPPCRingEntryV1 entry;
        ULONG *slotAddr;
        ULONG fence = 0;
        ULONG crc;
#if MGLPPC_PROFILE
        ULONG stageStart;
#endif

        if ((MGLPPC_HOST_TRACE || state->verboseUploads) &&
            !state->ringSeenEntry) {
            state->ringSeenEntry = 1;
            printf("MiniGLPPC host: ring_first_entry\n");
            fflush(stdout);
        }
        /* The 68k may hold this line from the register-time init writes;
         * drop the cached copy so the entry fields are re-fetched from
         * VRAM (the PPC pushed them with dcbf before advancing head). */
        CacheClearE((APTR)&control->entries[state->ringCompleted %
                                            MGLPPC_RING_SLOTS],
                    sizeof(MGLPPCRingEntryV1), CACRF_ClearD);
        entry.slot = source->slot;
        entry.dwords = source->dwords;
        entry.generation = source->generation;
        entry.flags = source->flags;
        entry.crc = source->crc;
        /* The client stamps its present-sync state beside every entry;
         * apply changes before executing the slot so the present mode
         * (blit vs vblank-locked flip) follows the client's mglEnableSync. */
        if (control->syncSeq != state->ringSyncSeq) {
            bridgeValid = FALSE;
            state->ringSyncSeq = control->syncSeq;
            mglEnableSync(control->syncEnabled ? GL_TRUE : GL_FALSE);
        }
        if (entry.slot >= MGLPPC_RING_SLOTS ||
            entry.dwords > MGLPPC_SEMANTIC_SLOT_DWORDS ||
            entry.dwords > control->slotDwords ||
            entry.generation <= state->ringLastGen[entry.slot]) {
            printf("MiniGLPPC host: ring_bad_entry slot=%lu dwords=%lu "
                   "generation=%lu last=%lu slot_dwords=%lu\n",
                   (unsigned long)entry.slot,
                   (unsigned long)entry.dwords,
                   (unsigned long)entry.generation,
                   (unsigned long)(entry.slot < MGLPPC_RING_SLOTS
                       ? state->ringLastGen[entry.slot] : 0UL),
                   (unsigned long)control->slotDwords);
            fflush(stdout);
            control->error = 1;
            break;
        }
        slotAddr = (ULONG *)control->slotAddrs[entry.slot];
        /* A non-plain entry (state batch, CP emit, indirect smoke) or any
         * entry the specialised branches below submit out of band must not
         * jump ahead of absorbed commit records. */
        if (RingAccValid &&
            (!(entry.flags & MGLPPC_SEMANTIC_FLAG_COMMIT) ||
             (entry.flags & (MGLPPC_SEMANTIC_FLAG_STATE_BATCH |
                             MGLPPC_SEMANTIC_FLAG_CP_EMIT |
                             MGLPPC_SEMANTIC_FLAG_INDIRECT)))) {
            if (!FlushCommitAccumulator(state, &bridge, &bridgeValid)) {
                control->error = 1;
                CacheClearE((APTR)&control->error, sizeof(control->error), CACRF_ClearD);
                break;
            }
        }
        if (entry.flags & MGLPPC_SEMANTIC_FLAG_DRAIN) {
            /* W-80: the publisher wants every submitted dispatch retired, so
             * close the fence-coalescing run and drain. The zero-payload
             * entry consumed no slot data. */
            if (!DrainSemanticFence(state)) {
                printf("MiniGLPPC host: drain_entry_fail fence=%lu "
                       "completed=%lu head=%lu\n",
                       (unsigned long)state->semanticFence,
                       (unsigned long)state->ringCompleted,
                       (unsigned long)control->head);
                fflush(stdout);
                control->error = 1;
                CacheClearE((APTR)&control->error, sizeof(control->error), CACRF_ClearD);
                break;
            }
            /* Keep the slot-generation invariant true for this slot even
             * though the entry carried no payload. */
            state->ringLastGen[entry.slot] = entry.generation;
            bridgeValid = FALSE;
        } else if (entry.flags & MGLPPC_SEMANTIC_FLAG_CP_EMIT) {
            if (!ExecuteCpEntry(state, &entry, slotAddr)) {
                printf("MiniGLPPC host: cp_emit_fail stage=%s slot=%lu "
                       "dwords=%lu gen=%lu last=%lu fences=%lu disp=%lu\n",
                       CpFailStage ? CpFailStage : "none",
                       (unsigned long)entry.slot,
                       (unsigned long)entry.dwords,
                       (unsigned long)entry.generation,
                       (unsigned long)state->ringLastGen[entry.slot],
                       (unsigned long)state->fenceCount,
                       (unsigned long)state->cpDispatches);
                fflush(stdout);
                state->cpFailed = TRUE;
                control->error = 1;
                CacheClearE((APTR)&control->error, sizeof(control->error), CACRF_ClearD);
                break;
            }
            bridgeValid = FALSE;
        } else if (entry.flags & MGLPPC_SEMANTIC_FLAG_INDIRECT) {
            if (!ExecuteIndirectSmoke(state, &entry, slotAddr)) {
                control->error = 1;
                CacheClearE((APTR)&control->error, sizeof(control->error), CACRF_ClearD);
                break;
            }
            bridgeValid = FALSE;
        } else if (entry.flags & MGLPPC_SEMANTIC_FLAG_STATE_BATCH) {
            struct Radeon3DStateBatch batch;
            ULONG draws, segmentIx, headerDwords;

            if (!(entry.flags & MGLPPC_SEMANTIC_FLAG_COMMIT) ||
                entry.dwords < MGLPPC_RING_COMMIT_HEADER_DWORDS + 2UL) {
                printf("MiniGLPPC host: statebatch_bad_flags flags=%08lx "
                       "dwords=%lu\n",
                       (unsigned long)entry.flags,
                       (unsigned long)entry.dwords);
                fflush(stdout);
                control->error = 1;
                break;
            }
            CacheClearE(slotAddr, entry.dwords * sizeof(ULONG),
                        CACRF_ClearD);
            draws = slotAddr[0];
            segmentIx = slotAddr[1];
            if (!draws || draws > MGLPPC_SEMANTIC_MAX_DRAWS ||
                draws > (entry.dwords -
                         MGLPPC_RING_COMMIT_HEADER_DWORDS) / 2UL ||
                segmentIx >= state->vertexSegmentCount) {
                printf("MiniGLPPC host: statebatch_bad draws=%lu dwords=%lu "
                       "segix=%lu vsegs=%lu\n",
                       (unsigned long)draws,
                       (unsigned long)entry.dwords,
                       (unsigned long)segmentIx,
                       (unsigned long)state->vertexSegmentCount);
                fflush(stdout);
                control->error = 1;
                break;
            }
            headerDwords = entry.dwords -
                           MGLPPC_RING_COMMIT_HEADER_DWORDS - draws * 2UL;
            HOST_START(stageStart);
            if (!DrainForSegment(state, segmentIx)) {
                printf("MiniGLPPC host: statebatch_drain_fail segix=%lu "
                       "fence=%lu queued=%08lx completed=%lu head=%lu\n",
                       (unsigned long)segmentIx,
                       (unsigned long)state->semanticFence,
                       (unsigned long)state->queuedSegmentMask,
                       (unsigned long)state->ringCompleted,
                       (unsigned long)control->head);
                fflush(stdout);
                control->error = 1;
                break;
            }
            HOST_STAGE(HostWaitTicks, stageStart);
            if (!state->displayLocked) {
                if (!mglLockDisplay()) {
                    printf("MiniGLPPC host: statebatch_lock_fail\n");
                    fflush(stdout);
                    control->error = 1;
                    break;
                }
                state->displayLocked = 1;
            }
            HOST_START(stageStart);
#if MGLPPC_PROFILE
            if (bridgeValid)
                ++HostBridgeCacheHits;
#endif
            if ((!bridgeValid && !QueryBackendBridgeV2(&bridge)) ||
                bridge.deviceEpoch != state->ringDeviceEpoch) {
                mglUnlockDisplay();
                state->displayLocked = 0;
                printf("MiniGLPPC host: statebatch_bridge_fail "
                       "expected_epoch=%lu\n",
                       (unsigned long)state->ringDeviceEpoch);
                fflush(stdout);
                control->error = 1;
                break;
            }
            bridgeValid = MGLPPC_HOST_TARGET_CACHE;
            if ((MGLPPC_HOST_TRACE || state->verboseUploads) &&
                !state->headerLogged) {
                ULONG hi;
                printf("host: hdr dwords=%lu:",
                       (unsigned long)headerDwords);
                for (hi = 0; hi < headerDwords && hi < 24UL; ++hi)
                    printf(" %08lx",
                           (unsigned long)slotAddr[
                               MGLPPC_RING_COMMIT_HEADER_DWORDS + hi]);
                printf("\n");
                fflush(stdout);
                state->headerLogged = 1;
            }
            HOST_STAGE(HostQueryTicks, stageStart);
            memset(&batch, 0, sizeof(batch));
            batch.Size = sizeof(batch);
            batch.Version = RADEON3D_STATE_BATCH_VERSION;
            batch.Generation = state->ringDeviceGeneration;
            batch.SegmentId = state->vertexSegmentIds[segmentIx];
            batch.Primitive = slotAddr[MGLPPC_RING_COMMIT_HEADER_DWORDS];
            batch.Header = slotAddr + MGLPPC_RING_COMMIT_HEADER_DWORDS;
            batch.HeaderDwords = headerDwords;
            /* One header shared by every draw in the batch. */
            if (headerDwords > 2UL)
                slotAddr[MGLPPC_RING_COMMIT_HEADER_DWORDS + 2UL] =
                    bridge.colorSurfaceToken;
            batch.Draws = (const struct Radeon3DStateBatchDraw *)
                (slotAddr + MGLPPC_RING_COMMIT_HEADER_DWORDS +
                 headerDwords);
            batch.DrawCount = draws;
            batch.Flags = RADEON3D_SUBMIT_FENCE;
            HOST_START(stageStart);
            /* Diagnostic: sentinel so a FALSE return that wrote no stage
             * (paths that cannot fill fenceOut) is distinguishable from 0. */
            fence = 0xC0DE0000UL;
            if (!Radeon3DCommitStateBatch(
                    (struct Radeon3DDevice *)bridge.deviceToken,
                    &batch, &fence)) {
                struct Radeon3DInfo info;
                BOOL retry = FALSE;

#if defined(MGLPPC_REJECT_DUMP) && MGLPPC_REJECT_DUMP
                /* Failure-only diagnostic, flushed independently of stdout.
                 * Bounded text avoids host-endian binary dump assumptions.
                 * Never enabled in normal/performance hosts. */
                {
                    BPTR dump = Open((CONST_STRPTR)"DH2:wosbuild/qreject.log",
                                     MODE_NEWFILE);
                    if (dump) {
                        ULONG hi;
                        char line[160];
                        int length = snprintf(line, sizeof(line),
                            "fence=%08lx hdr=%lu draws=%lu prim=%lu gen=%lu\n",
                            (unsigned long)fence,
                            (unsigned long)batch.HeaderDwords,
                            (unsigned long)batch.DrawCount,
                            (unsigned long)batch.Primitive,
                            (unsigned long)batch.Generation);
                        Write(dump, line, length);
                        for (hi = 0; hi < batch.HeaderDwords && hi < 256UL; ++hi) {
                            length = snprintf(line, sizeof(line), "H %lu %08lx\n",
                                (unsigned long)hi,
                                (unsigned long)batch.Header[hi]);
                            Write(dump, line, length);
                        }
                        for (hi = 0; hi < batch.DrawCount && hi < 16UL; ++hi) {
                            length = snprintf(line, sizeof(line), "D %lu %lu %lu\n",
                                (unsigned long)hi,
                                (unsigned long)batch.Draws[hi].OffsetBytes,
                                (unsigned long)batch.Draws[hi].VertexCount);
                            Write(dump, line, length);
                        }
                        Close(dump);
                    }
                }
#endif

                printf("host: sbcommit_fail fence=%08lx hdr=%lu draws=%lu "
                       "prim=%lu seg=%lu gen=%lu gen_drv=%lu epoch=%lu "
                       "token=%08lx\n",
                       (unsigned long)fence,
                       (unsigned long)headerDwords,
                       (unsigned long)draws,
                       (unsigned long)batch.Primitive,
                       (unsigned long)segmentIx,
                       (unsigned long)batch.Generation,
                       (unsigned long)state->ringDeviceGeneration,
                       (unsigned long)bridge.deviceEpoch,
                       (unsigned long)bridge.colorSurfaceToken);
                fflush(stdout);

                bridgeValid = FALSE;
                memset(&info, 0, sizeof(info));
                info.Size = sizeof(info);
                if (Radeon3DGetInfo(
                        (struct Radeon3DDevice *)bridge.deviceToken,
                        &info) && info.Generation &&
                    info.CommitFailStage == 81UL &&
                    info.Generation != batch.Generation) {
                    batch.Generation = info.Generation;
                    state->ringDeviceGeneration = info.Generation;
                    retry = Radeon3DCommitStateBatch(
                        (struct Radeon3DDevice *)bridge.deviceToken,
                        &batch, &fence);
                }
                if (!retry) {
                    struct Radeon3DInfo postInfo;
                    memset(&postInfo, 0, sizeof(postInfo));
                    postInfo.Size = sizeof(postInfo);
                    printf("host: postfail getinfo=%d gen=%lu stage=%lu "
                           "glerr=%04lx\n",
                           Radeon3DGetInfo(
                               (struct Radeon3DDevice *)bridge.deviceToken,
                               &postInfo) ? 1 : 0,
                           (unsigned long)postInfo.Generation,
                           (unsigned long)postInfo.CommitFailStage,
                           (unsigned long)glGetError());
                    fflush(stdout);
                    mglUnlockDisplay();
                    state->displayLocked = 0;
                    printf("MiniGLPPC host: statebatch_commit_fail "
                           "segix=%lu generation=%lu driver_generation=%lu "
                           "stage=%lu\n",
                           (unsigned long)segmentIx,
                           (unsigned long)batch.Generation,
                           (unsigned long)info.Generation,
                           (unsigned long)info.CommitFailStage);
                    fflush(stdout);
                    control->error = 1;
                    break;
                }
            }
            HOST_STAGE(HostSubmitTicks, stageStart);
            if (!RecordFence(state, state->ringCompleted, fence,
                             1UL << segmentIx)) {
                mglUnlockDisplay();
                state->displayLocked = 0;
                control->error = 1;
                break;
            }
            state->ringLastGen[entry.slot] = entry.generation;
            state->semanticExecutes++;
            HostExecuteCount++;
        } else if (entry.flags & MGLPPC_SEMANTIC_FLAG_COMMIT) {
            /* Header-only draw chain with the commit header at the front:
             * dword[0]=draw count, dword[1]=vertex segment pool index,
             * then records, then one vertex byte offset per draw. All of
             * it travelled (and was cache-maintained) as slot data. */
            ULONG draws, segmentIx, recordDwords;
            if (entry.dwords < MGLPPC_RING_COMMIT_HEADER_DWORDS + 1UL) {
                control->error = 1;
                break;
            }
            CacheClearE(slotAddr, entry.dwords * sizeof(ULONG),
                        CACRF_ClearD);
            draws = slotAddr[0];
            segmentIx = slotAddr[1];
            if (!draws || draws > 192UL ||
                draws > entry.dwords - MGLPPC_RING_COMMIT_HEADER_DWORDS ||
                segmentIx >= state->vertexSegmentCount) {
                printf("MiniGLPPC host: commit_bad dwords=%lu draws=%lu "
                       "segix=%lu vsegs=%lu\n",
                       (unsigned long)entry.dwords,
                       (unsigned long)draws,
                       (unsigned long)segmentIx,
                       (unsigned long)state->vertexSegmentCount);
                fflush(stdout);
                control->error = 1;
                break;
            }
            recordDwords = entry.dwords - 2UL - draws;
            /* Absorb into the accumulator instead of submitting per entry;
             * flush when the segment changes, the entry presents, or the
             * merged batch would exceed the service limits. The PRESENT
             * case must flush before the loop tail drains and flips. */
            if (RingAccValid &&
                (segmentIx != RingAccSegmentIx ||
                 (entry.flags & MGLPPC_SEMANTIC_FLAG_PRESENT) ||
                 RingAccDraws + draws > 192UL ||
                 RingAccRecordDwords + recordDwords > MGLPPC_SEMANTIC_SLOT_DWORDS)) {
                if (!FlushCommitAccumulator(state, &bridge, &bridgeValid)) {
                    control->error = 1;
                    CacheClearE((APTR)&control->error, sizeof(control->error), CACRF_ClearD);
                    break;
                }
            }
            if (!RingAccValid) {
                RingAccValid = TRUE;
                RingAccRecordDwords = 0;
                RingAccDraws = 0;
                RingAccEntries = 0;
                RingAccSegmentIx = segmentIx;
            }
            for (crc = 0; crc < recordDwords; ++crc)
                RingAccRecords[RingAccRecordDwords + crc] =
                    slotAddr[MGLPPC_RING_COMMIT_HEADER_DWORDS + crc];
            for (crc = 0; crc < draws; ++crc)
                RingAccOffsets[RingAccDraws + crc] =
                    slotAddr[MGLPPC_RING_COMMIT_HEADER_DWORDS +
                             recordDwords + crc];
            RingAccRecordDwords += recordDwords;
            RingAccDraws += draws;
            RingAccEntries++;
            state->ringLastGen[entry.slot] = entry.generation;
            state->semanticExecutes++;
            HostExecuteCount++;
            if (entry.flags & MGLPPC_SEMANTIC_FLAG_PRESENT) {
                if (!FlushCommitAccumulator(state, &bridge, &bridgeValid)) {
                    control->error = 1;
                    CacheClearE((APTR)&control->error, sizeof(control->error), CACRF_ClearD);
                    break;
                }
            }
        } else if (entry.dwords) {
            HOST_START(stageStart);
            if (!DrainSemanticFence(state)) {
                printf("MiniGLPPC host: inline_drain_fail fence=%lu "
                       "completed=%lu head=%lu\n",
                       (unsigned long)state->semanticFence,
                       (unsigned long)state->ringCompleted,
                       (unsigned long)control->head);
                fflush(stdout);
                control->error = 1;
                break;
            }
            HOST_STAGE(HostWaitTicks, stageStart);
            CacheClearE(slotAddr, entry.dwords * sizeof(ULONG),
                        CACRF_ClearD);
#ifdef MGLPPC_HOST_VERIFY_CRC
            crc = SemanticCRC32(slotAddr, entry.dwords);
#else
            if (state->semanticExecutes == 0)
                crc = SemanticCRC32(slotAddr, entry.dwords);
            else
                crc = entry.crc;
#endif
            if (crc != entry.crc) {
#if defined(MGLPPC_REJECT_DUMP) && MGLPPC_REJECT_DUMP
                printf("inline crc_fail expected=%08lx actual=%08lx\n",
                       (unsigned long)entry.crc, (unsigned long)crc);
#endif
                control->error = 1;
                break;
            }
            if (!state->displayLocked) {
                if (!mglLockDisplay()) {
                    control->error = 1;
                    break;
                }
                state->displayLocked = 1;
            }
            HOST_START(stageStart);
#if MGLPPC_PROFILE
            if (bridgeValid)
                ++HostBridgeCacheHits;
#endif
            if ((!bridgeValid && !QueryBackendBridgeV2(&bridge)) ||
                bridge.deviceEpoch != state->ringDeviceEpoch) {
                mglUnlockDisplay();
                state->displayLocked = 0;
                control->error = 1;
                break;
            }
            bridgeValid = MGLPPC_HOST_TARGET_CACHE;
            HOST_STAGE(HostQueryTicks, stageStart);
            /* After the CRC check: the patch changes the slot contents. */
            PatchRecordTargets(slotAddr, entry.dwords,
                               bridge.colorSurfaceToken);
            HOST_START(stageStart);
            if (!Radeon3DExecute((struct Radeon3DDevice *)bridge.deviceToken,
                                  slotAddr, entry.dwords,
                                  RADEON3D_SUBMIT_FENCE, &fence)) {
#if defined(MGLPPC_REJECT_DUMP) && MGLPPC_REJECT_DUMP
                BPTR dump = Open((CONST_STRPTR)"DH2:wosbuild/qinline.bin", MODE_NEWFILE);
                printf("inline execute_fail words=%lu completed=%lu flags=%08lx\n",
                       (unsigned long)entry.dwords,
                       (unsigned long)state->ringCompleted,
                       (unsigned long)entry.flags);
                if (dump) {
                    ULONG header[4] = {0x51494e31UL, entry.dwords,
                                       entry.flags, state->ringCompleted};
                    WriteRejectWords(dump, header, 4);
                    if (entry.dwords <= MGLPPC_SEMANTIC_SLOT_DWORDS)
                        WriteRejectWords(dump, slotAddr, entry.dwords);
                    Close(dump);
                }
#endif
                mglUnlockDisplay();
                state->displayLocked = 0;
                control->error = 1;
                break;
            }
            HOST_STAGE(HostSubmitTicks, stageStart);
            /* The inline path drained above, so the queue has room. */
            if (!RecordFence(state, state->ringCompleted, fence, 0)) {
                mglUnlockDisplay();
                state->displayLocked = 0;
                control->error = 1;
                break;
            }
            state->ringLastGen[entry.slot] = entry.generation;
            state->semanticExecutes++;
            HostExecuteCount++;
        }
        /* Release host-dirty target patches before the next PPC publication. */
        if (entry.dwords)
            CacheClearE(slotAddr, entry.dwords * sizeof(ULONG), CACRF_ClearD);
        state->ringCompleted++;
        processed++;
        if (entry.flags & MGLPPC_SEMANTIC_FLAG_PRESENT) {
            bridgeValid = FALSE;
            HOST_START(stageStart);
            if (!DrainSemanticFence(state)) {
                printf("MiniGLPPC host: present_drain_fail fence=%lu "
                       "completed=%lu head=%lu\n",
                       (unsigned long)state->semanticFence,
                       (unsigned long)state->ringCompleted,
                       (unsigned long)control->head);
                fflush(stdout);
                control->error = 1;
                break;
            }
            HOST_STAGE(HostPresentWaitTicks, stageStart);
            HOST_START(stageStart);
            mglSwitchDisplay();
            HOST_STAGE(HostPresentTicks, stageStart);
            HostPresentCount++;
            mglUnlockDisplay();
            state->displayLocked = 0;
            {
                GLenum presentError = glGetError();
                if (presentError != GL_NO_ERROR) {
                    printf("MiniGLPPC host: present_gl_error error=%lu "
                           "completed=%lu head=%lu\n",
                           (unsigned long)presentError,
                           (unsigned long)state->ringCompleted,
                           (unsigned long)control->head);
                    fflush(stdout);
                    control->error = 1;
                    break;
                }
            }
            /* A fullscreen switch may have retargeted the native drawing
             * buffer. Re-query and publish it (ring control v6) so the PPC
             * stamps its next CP batch for the surface this host will own
             * when that entry is consumed. The bridge snapshot is stale
             * after the switch; keep bridgeValid FALSE for the next entry. */
            if (state->cpEnabled) {
                MGLR200PrivateBridgeV2 liveBridge;
                if (QueryBackendBridgeV2(&liveBridge) &&
                    liveBridge.deviceEpoch == state->ringDeviceEpoch) {
                    state->cpColorToken = liveBridge.colorSurfaceToken;
                    PublishCpColorTarget(state, liveBridge.colorSurfaceToken);
                }
            }
            HOST_PACING_PRESENT();
            control->presentSeq = state->ringCompleted;
        }
        control->completed = state->ringCompleted;
        /* Publish the contiguous retired prefix, not just the fence-less
         * case: with several submissions in flight only completed fences
         * may retire slices. */
        if (state->cpEnabled)
            AdvanceRetired(state);
        if (entry.flags & MGLPPC_SEMANTIC_FLAG_INDIRECT) {
            char line[128];
            control->retired = state->ringCompleted;
            CacheClearE((APTR)&control->retired, sizeof(control->retired), CACRF_ClearD);
            CacheClearE((APTR)&control->completed, sizeof(control->completed), CACRF_ClearD);
            sprintf(line, "IBHOST ack=%lu mode=%s\n", (unsigned long)state->ringCompleted,
                    state->ibDryRun ? "dry" : "gpu");
            MGLPPCIBTrace("DH2:ibhost.log", line);
        }
    }
    /* Queue drained: the GPU must not sit idle on absorbed-but-unsubmitted
     * records, and the next burst may begin with a present-only barrier. */
    if (RingAccValid && !control->error) {
        if (!FlushCommitAccumulator(state, &bridge, &bridgeValid))
            control->error = 1;
    }
#if MGLPPC_PROFILE
    ringStart = HostClock() - ringStart;
    return ringStart ? ringStart : (processed ? 1UL : 0UL);
#else
    return processed;
#endif
}

static BOOL ExecuteSemanticSlot(MGLPPCMessage *message,
                                 MGLPPCHostState *state)
{
    MGLR200PrivateBridgeV1 bridge;
    ULONG slotIndex = message->value[0];
    ULONG generation = message->value[1];
    ULONG dwords = message->value[2];
    ULONG crc;
    ULONG fence = 0;
#if MGLPPC_PROFILE
    ULONG stageStart, totalStart;
#endif

    if (!state->context || message->contextId != state->contextId) {
        message->status = MGLPPC_STATUS_NO_CONTEXT;
        return FALSE;
    }
    if (state->ringControl ||
        !state->semanticRegistered ||
        message->value[5] != state->semanticEpoch) {
        message->status = MGLPPC_STATUS_BAD_PAYLOAD;
        return FALSE;
    }
    if (slotIndex >= MGLPPC_SEMANTIC_SLOT_COUNT || generation == 0 ||
        dwords == 0 || dwords > MGLPPC_SEMANTIC_SLOT_DWORDS ||
        (message->value[3] & ~MGLPPC_SEMANTIC_FLAG_PRESENT)) {
        message->status = MGLPPC_STATUS_BAD_PAYLOAD;
        return FALSE;
    }
    if (generation <= state->semanticGeneration[slotIndex]) {
        message->status = MGLPPC_STATUS_STALE_GENERATION;
        return FALSE;
    }
#if MGLPPC_PROFILE
    totalStart = stageStart = HostClock();
#endif
    if (!DrainSemanticFence(state)) {
        message->status = MGLPPC_STATUS_GL_ERROR;
        return FALSE;
    }
    HOST_STAGE(HostWaitTicks, stageStart);
    CacheClearE(state->semanticSlots[slotIndex], dwords * sizeof(ULONG),
                CACRF_ClearD);
    HOST_STAGE(HostClearTicks, stageStart);
#ifdef MGLPPC_HOST_VERIFY_CRC
    crc = SemanticCRC32(state->semanticSlots[slotIndex], dwords);
#else
    /* Host-side verification cost ~25 ms per Execute on the 68060 (~83 ms per
     * gears frame), far more than the submit it guards. Keep the tripwire for
     * the first Execute of each registered session -- it catches gross
     * coherence or layout breakage immediately and re-arms after recovery --
     * and skip it per frame otherwise. Slot ownership plus the dcbf/sync and
     * CacheClearE discipline protect the payload; define
     * MGLPPC_HOST_VERIFY_CRC to restore full verification. */
    if (state->semanticExecutes == 0)
        crc = SemanticCRC32(state->semanticSlots[slotIndex], dwords);
    else
        crc = message->value[4];
#endif
    HOST_STAGE(HostCrcTicks, stageStart);
    if (crc != message->value[4]) {
        message->status = MGLPPC_STATUS_VERIFY_FAILED;
        message->value[0] = crc;
        return FALSE;
    }
    if (!state->displayLocked) {
        if (!mglLockDisplay()) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            return FALSE;
        }
        state->displayLocked = 1;
    }
    if (!QueryBackendBridge(&bridge)) {
        mglUnlockDisplay();
        state->displayLocked = 0;
        message->status = MGLPPC_STATUS_GL_ERROR;
        return FALSE;
    }
    HOST_STAGE(HostQueryTicks, stageStart);
    if (!Radeon3DExecute((struct Radeon3DDevice *)bridge.deviceToken,
                          state->semanticSlots[slotIndex],
                          dwords, RADEON3D_SUBMIT_FENCE, &fence)) {
        mglUnlockDisplay();
        state->displayLocked = 0;
        message->status = MGLPPC_STATUS_GL_ERROR;
        return FALSE;
    }
    HOST_STAGE(HostSubmitTicks, stageStart);
    if (!RecordFence(state, state->ringCompleted, fence, 0)) {
        mglUnlockDisplay();
        state->displayLocked = 0;
        message->status = MGLPPC_STATUS_GL_ERROR;
        return FALSE;
    }
    state->semanticGeneration[slotIndex] = generation;
    state->semanticExecutes++;
    if (message->value[3] & MGLPPC_SEMANTIC_FLAG_PRESENT) {
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            return FALSE;
        }
        HOST_STAGE(HostPresentWaitTicks, stageStart);
        mglSwitchDisplay();
        HOST_STAGE(HostPresentTicks, stageStart);
        HostPresentCount++;
        mglUnlockDisplay();
        state->displayLocked = 0;
        if (glGetError() != GL_NO_ERROR) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            return FALSE;
        }
        HOST_PACING_PRESENT();
    }
    HOST_STAGE(HostTotalTicks, totalStart);
    HostExecuteCount++;
    message->value[0] = state->semanticExecutes;
    return TRUE;
}

static void ProbeStore(volatile ULONG *destination, ULONG salt)
{
    ULONG index;

    for (index = 0; index < MGLPPC_VRAM_PROBE_WORDS; ++index)
        destination[index] = index ^ salt;
}

static void DiscardProbe(MGLPPCHostState *state)
{
    if (state->probeDevice && state->probeSegment.Version)
        (void)Radeon3DFreeSegment(state->probeDevice,
                                  state->probeSegment.Id);
    if (state->probeDevice)
        Radeon3DClose(state->probeDevice);
    if (Radeon9200Base)
        CloseLibrary(Radeon9200Base);
    Radeon9200Base = NULL;
    state->probeDevice = NULL;
    memset(&state->probeSegment, 0, sizeof(state->probeSegment));
    state->probeCookie = 0;
    state->probe68kMicros = 0;
    state->probeConfirmed = 0;
}

static BOOL BeginVramProbe(MGLPPCMessage *message,
                           MGLPPCHostState *state)
{
    struct Radeon3DInfo info;
    volatile ULONG *memory;
    volatile ULONG drain;
    ULONG start;
    ULONG end;
    ULONG pass;
    ULONG sentinelIndex = MGLPPC_VRAM_PROBE_WORDS / 2UL;

    if (state->context || state->displayLocked || state->probeDevice) {
        message->status = MGLPPC_STATUS_BUSY;
        return FALSE;
    }
    if (message->value[0] != MGLPPC_VRAM_PROBE_BYTES ||
        message->value[1] != MGLPPC_VRAM_PROBE_PASSES || !TimerClockRate) {
        message->status = MGLPPC_STATUS_BAD_PAYLOAD;
        return FALSE;
    }
    Radeon9200Base = OpenLibrary((CONST_STRPTR)"Radeon9200.chip",
                                 RADEON3D_LIBRARY_VERSION);
    if (!Radeon9200Base) {
        message->status = MGLPPC_STATUS_UNSUPPORTED;
        return FALSE;
    }
    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    state->probeDevice = Radeon3DOpen(RADEON3D_IFACE_VERSION, &info);
    if (!state->probeDevice ||
        !(info.Caps & RADEON3D_CAP_STREAM_SEGMENTS)) {
        message->status = MGLPPC_STATUS_UNSUPPORTED;
        DiscardProbe(state);
        return FALSE;
    }
    memset(&state->probeSegment, 0, sizeof(state->probeSegment));
    state->probeSegment.Size = sizeof(state->probeSegment);
    if (!Radeon3DAllocSegment(state->probeDevice,
                              MGLPPC_VRAM_PROBE_BYTES,
                              &state->probeSegment) ||
        state->probeSegment.Bytes != MGLPPC_VRAM_PROBE_BYTES ||
        !state->probeSegment.CpuAddress) {
        message->status = MGLPPC_STATUS_NO_MEMORY;
        DiscardProbe(state);
        return FALSE;
    }

    memory = (volatile ULONG *)state->probeSegment.CpuAddress;
    ProbeStore(memory, MGLPPC_VRAM_PROBE_68K_SEED - 1UL);
    drain = memory[MGLPPC_VRAM_PROBE_WORDS - 1UL];
    (void)drain;
    start = ProbeTicks();
    for (pass = 0; pass < MGLPPC_VRAM_PROBE_PASSES; ++pass)
        ProbeStore(memory, MGLPPC_VRAM_PROBE_68K_SEED + pass);
    drain = memory[MGLPPC_VRAM_PROBE_WORDS - 1UL];
    end = ProbeTicks();
    (void)drain;
    state->probe68kMicros = ProbeMicros(start, end);
    state->probeCookie = state->nextProbeCookie++;
    if (!state->probeCookie)
        state->probeCookie = state->nextProbeCookie++;

    message->value[0] = (ULONG)state->probeSegment.CpuAddress;
    message->value[1] = state->probeSegment.Bytes;
    message->value[2] = MGLPPC_VRAM_PROBE_PASSES;
    message->value[3] = state->probe68kMicros;
    message->value[4] = MGLPPC_VRAM_PROBE_PPC_SEED;
    message->value[5] = sentinelIndex;
    message->value[6] = MGLPPC_VRAM_PROBE_SENTINEL;
    message->value[7] = state->probeCookie;
    return TRUE;
}

static BOOL ConfirmVramProbe(MGLPPCMessage *message,
                             MGLPPCHostState *state)
{
    volatile ULONG *memory;

    if (!state->probeDevice || !state->probeSegment.CpuAddress ||
        message->value[0] != state->probeCookie || !state->probeCookie) {
        message->status = MGLPPC_STATUS_BAD_PAYLOAD;
        return FALSE;
    }
    memory = (volatile ULONG *)state->probeSegment.CpuAddress;
    if (memory[MGLPPC_VRAM_PROBE_WORDS / 2UL] !=
        MGLPPC_VRAM_PROBE_SENTINEL) {
        message->status = MGLPPC_STATUS_VERIFY_FAILED;
        DiscardProbe(state);
        return FALSE;
    }
    state->probeConfirmed = 1;
    memory[MGLPPC_VRAM_PROBE_HOST_INDEX] = MGLPPC_VRAM_PROBE_HOST_SENTINEL;
    {
        volatile ULONG drain = memory[MGLPPC_VRAM_PROBE_HOST_INDEX];
        (void)drain;
    }
    return TRUE;
}

static BOOL EndVramProbe(MGLPPCMessage *message,
                         MGLPPCHostState *state)
{
    volatile ULONG *memory;
    ULONG expectedSalt = MGLPPC_VRAM_PROBE_PPC_SEED +
                         MGLPPC_VRAM_PROBE_PASSES - 1UL;
    ULONG probe68kMicros = state->probe68kMicros;
    ULONG probePpcMicros = message->value[1];
    ULONG index;
    BOOL valid = TRUE;

    if (!state->probeDevice || !state->probeConfirmed ||
        message->value[0] != state->probeCookie || !state->probeCookie) {
        message->status = MGLPPC_STATUS_BAD_PAYLOAD;
        DiscardProbe(state);
        return FALSE;
    }
    memory = (volatile ULONG *)state->probeSegment.CpuAddress;
    for (index = 0; index < MGLPPC_VRAM_PROBE_WORDS; ++index) {
        if (memory[index] != (index ^ expectedSalt)) {
            valid = FALSE;
            break;
        }
    }
    if (message->value[2] !=
        ((MGLPPC_VRAM_PROBE_WORDS - 1UL) ^ expectedSalt) ||
        message->value[3] != MGLPPC_VRAM_PROBE_HOST_SENTINEL) {
        valid = FALSE;
    }
    DiscardProbe(state);
    message->value[0] = probe68kMicros;
    message->value[1] = probePpcMicros;
    message->value[2] = MGLPPC_VRAM_PROBE_BYTES;
    message->value[3] = MGLPPC_VRAM_PROBE_PASSES;
    message->value[4] = index;
    message->value[5] = (MGLPPC_VRAM_PROBE_WORDS - 1UL) ^ expectedSalt;
    if (!valid)
        message->status = MGLPPC_STATUS_VERIFY_FAILED;
    return valid;
}

static void DiscardUpload(MGLPPCHostState *state)
{
    if (state->uploadPixels != NULL)
        FreeVec(state->uploadPixels);
    state->uploadPixels = NULL;
    state->uploadBytes = 0;
    state->uploadReceived = 0;
}

static ULONG TextureByteCount(ULONG width, ULONG height,
                              ULONG format, ULONG type)
{
    ULONG components;

    if (width == 0 || height == 0)
        return 0;
    if (type == GL_UNSIGNED_BYTE) {
        if (format == GL_RGB)
            components = 3;
        else if (format == GL_RGBA)
            components = 4;
        /* Luminance-class and direct byte-order sources: same values
         * the native backend's SourceFormatBytes() accepts (the host
         * compiles against the native tree's client header, and the
         * frontend translates the below-the-anchor tail constants
         * before they cross the wire). */
        else if (format == MGL_UBYTE_BGRA || format == MGL_UBYTE_ARGB)
            components = 4;
        else if (format == GL_LUMINANCE_ALPHA)
            components = 2;
        else if (format == GL_ALPHA || format == GL_LUMINANCE ||
                 format == GL_COLOR_INDEX)
            components = 1;
        else
            return 0;
    } else if ((type == MGL_UNSIGNED_SHORT_5_6_5 && format == GL_RGB) ||
               (type == MGL_UNSIGNED_SHORT_4_4_4_4 && format == GL_RGBA)) {
        components = 2;
    } else {
        return 0;
    }
    if (width > 0xffffffffUL / height ||
        width * height > 0xffffffffUL / components)
        return 0;
    return width * height * components;
}

static void CompleteTextureUpload(MGLPPCHostState *state,
                                  MGLPPCMessage *message,
                                  const ULONG *args, const GLvoid *pixels)
{
    MGLR200PrivateTextureV1 textureBridge;
    ULONG textureName;

    (void)glGetError();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!state->insidePrimitive)
        state->unpack[0] = 1;
    glTexImage2D(args[0], (LONG)args[1], (LONG)args[2], args[3], args[4],
                 (LONG)args[5], args[6], args[7], pixels);
    message->value[0] = glGetError();
    if (message->value[0] != GL_NO_ERROR) {
        message->status = MGLPPC_STATUS_GL_ERROR;
        return;
    }
    if (!state->semanticRegistered)
        return;
    textureName = state->semanticBoundTexture;
    if (!QueryBackendTexture(textureName, &textureBridge) ||
        glGetError() != GL_NO_ERROR) {
        message->status = MGLPPC_STATUS_GL_ERROR;
        message->value[0] = GL_INVALID_OPERATION;
        return;
    }
    message->value[1] = textureBridge.surfaceToken;
    message->value[2] = textureBridge.width;
    message->value[3] = textureBridge.height;
    message->value[4] = textureBridge.definedLevels;
    message->value[5] = textureBridge.levelCount;
    message->value[6] = textureBridge.contentSerial;
    message->value[7] = textureBridge.deviceEpoch;
}

static float BitsToFloat(ULONG bits)
{
    union {
        ULONG bits;
        float value;
    } conversion;

    conversion.bits = bits;
    return conversion.value;
}

static BOOL ExecuteBatch(MGLPPCMessage *message, MGLPPCHostState *state)
{
    ULONG offset = 0;

    if (message->payloadWords > MGLPPC_MAX_BATCH_WORDS ||
        MGLPPC_MESSAGE_SIZE(message->payloadWords) >
            message->execMessage.mn_Length)
        return FALSE;

    while (offset < message->payloadWords) {
        ULONG header = message->payload[offset++];
        ULONG opcode = header >> 16;
        ULONG argumentCount = header & 0xffffUL;
        const ULONG *arguments;

        if (argumentCount > message->payloadWords - offset)
            return FALSE;
        arguments = &message->payload[offset];

        switch (opcode) {
        case MGLPPC_BATCH_CLEAR_COLOR:
            if (argumentCount != 4)
                return FALSE;
            glClearColor(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                         BitsToFloat(arguments[2]), BitsToFloat(arguments[3]));
            break;
        case MGLPPC_BATCH_CLEAR:
            if (argumentCount != 1)
                return FALSE;
            glClear(arguments[0]);
            break;
        case MGLPPC_BATCH_BEGIN:
            if (argumentCount != 1 || state->insidePrimitive)
                return FALSE;
            glBegin(arguments[0]);
            state->insidePrimitive = 1;
            break;
        case MGLPPC_BATCH_COLOR4UB:
            if (argumentCount != 1)
                return FALSE;
            glColor4ub((arguments[0] >> 24) & 0xff,
                       (arguments[0] >> 16) & 0xff,
                       (arguments[0] >> 8) & 0xff,
                       arguments[0] & 0xff);
            break;
        case MGLPPC_BATCH_VERTEX3F:
            if (argumentCount != 3)
                return FALSE;
            glVertex3f(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                       BitsToFloat(arguments[2]));
            break;
        case MGLPPC_BATCH_END:
            if (argumentCount != 0 || !state->insidePrimitive)
                return FALSE;
            glEnd();
            state->insidePrimitive = 0;
            break;
        case MGLPPC_BATCH_PRESENT:
            if (argumentCount != 0)
                return FALSE;
            mglSwitchDisplay();
            state->displayLocked = 0;
            break;
        case MGLPPC_BATCH_DELAY:
            if (argumentCount != 1)
                return FALSE;
            Delay(arguments[0]);
            break;
        case MGLPPC_BATCH_DRAW_C4UB_V3F:
            if (argumentCount < 6 || arguments[1] == 0 ||
                arguments[1] > (0xffffffffUL - 2) / 4 ||
                argumentCount != 2 + arguments[1] * 4)
                return FALSE;
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, 16, &arguments[2]);
            glColorPointer(4, MGL_UBYTE_ARGB, 16,
                           (const UBYTE *)&arguments[2] + 12);
            glDrawArrays(arguments[0], 0, arguments[1]);
            glDisableClientState(GL_COLOR_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
            break;
        case MGLPPC_BATCH_SET_STATE:
            if (argumentCount != 2 || arguments[1] > 1)
                return FALSE;
            if (arguments[1])
                glEnable(arguments[0]);
            else
                glDisable(arguments[0]);
            break;
        case MGLPPC_BATCH_COLOR4F:
            if (argumentCount != 4) return FALSE;
            glColor4f(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                      BitsToFloat(arguments[2]), BitsToFloat(arguments[3]));
            break;
        case MGLPPC_BATCH_NORMAL3F:
            if (argumentCount != 3) return FALSE;
            glNormal3f(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                       BitsToFloat(arguments[2]));
            break;
        case MGLPPC_BATCH_VERTEX4F:
            if (argumentCount != 4) return FALSE;
            glVertex4f(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                       BitsToFloat(arguments[2]), BitsToFloat(arguments[3]));
            break;
        case MGLPPC_BATCH_VIEWPORT:
            if (argumentCount != 4) return FALSE;
            glViewport((LONG)arguments[0], (LONG)arguments[1],
                       (LONG)arguments[2], (LONG)arguments[3]);
            break;
        case MGLPPC_BATCH_MATRIX_MODE:
            if (argumentCount != 1) return FALSE;
            glMatrixMode(arguments[0]);
            break;
        case MGLPPC_BATCH_LOAD_IDENTITY:
            if (argumentCount != 0) return FALSE;
            glLoadIdentity();
            break;
        case MGLPPC_BATCH_PUSH_MATRIX:
            if (argumentCount != 0) return FALSE;
            glPushMatrix();
            break;
        case MGLPPC_BATCH_POP_MATRIX:
            if (argumentCount != 0) return FALSE;
            glPopMatrix();
            break;
        case MGLPPC_BATCH_TRANSLATEF:
            if (argumentCount != 3) return FALSE;
            glTranslatef(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                         BitsToFloat(arguments[2]));
            break;
        case MGLPPC_BATCH_ROTATEF:
            if (argumentCount != 4) return FALSE;
            glRotatef(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                      BitsToFloat(arguments[2]), BitsToFloat(arguments[3]));
            break;
        case MGLPPC_BATCH_SCALEF:
            if (argumentCount != 3) return FALSE;
            glScalef(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                     BitsToFloat(arguments[2]));
            break;
        case MGLPPC_BATCH_FRUSTUMF:
            if (argumentCount != 6) return FALSE;
            glFrustum(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                      BitsToFloat(arguments[2]), BitsToFloat(arguments[3]),
                      BitsToFloat(arguments[4]), BitsToFloat(arguments[5]));
            break;
        case MGLPPC_BATCH_ORTHOF:
            if (argumentCount != 6) return FALSE;
            glOrtho(BitsToFloat(arguments[0]), BitsToFloat(arguments[1]),
                    BitsToFloat(arguments[2]), BitsToFloat(arguments[3]),
                    BitsToFloat(arguments[4]), BitsToFloat(arguments[5]));
            break;
        case MGLPPC_BATCH_HINT:
            if (argumentCount != 2) return FALSE;
            glHint(arguments[0], arguments[1]);
            break;
        case MGLPPC_BATCH_TEX_ENVI:
            if (argumentCount != 3) return FALSE;
            glTexEnvi(arguments[0], arguments[1], (LONG)arguments[2]);
            break;
        case MGLPPC_BATCH_PIXEL_STOREI:
            if (argumentCount != 2) return FALSE;
            glPixelStorei(arguments[0], (LONG)arguments[1]);
            if (!state->insidePrimitive && (LONG)arguments[1] >= 0) {
                ULONG index;
                for (index = 0; index < 4; ++index)
                    if (index || arguments[1] == 1 || arguments[1] == 2 ||
                        arguments[1] == 4 || arguments[1] == 8) {
                        if (arguments[0] == HostUnpackNames[index])
                            state->unpack[index] = (GLint)arguments[1];
                        if (arguments[0] == HostPackNames[index])
                            state->pack[index] = (GLint)arguments[1];
                    }
            }
            break;
        case MGLPPC_BATCH_MGL_ENABLE_SYNC:
            if (argumentCount != 1 || arguments[0] > 1) return FALSE;
            mglEnableSync(arguments[0] ? GL_TRUE : GL_FALSE);
            break;
        case MGLPPC_BATCH_MGL_LOCK_MODE:
            if (argumentCount != 1) return FALSE;
            mglLockMode(arguments[0]);
            break;
        case MGLPPC_BATCH_BIND_TEXTURE:
            if (argumentCount != 2) return FALSE;
            glBindTexture(arguments[0], arguments[1]);
            if (!state->insidePrimitive && arguments[0] == GL_TEXTURE_2D &&
                arguments[1] < 2048UL)
                state->semanticBoundTexture = arguments[1];
            break;
        case MGLPPC_BATCH_TEX_PARAMETERI:
            if (argumentCount != 3) return FALSE;
            glTexParameteri(arguments[0], arguments[1], (LONG)arguments[2]);
            break;
        case MGLPPC_BATCH_TEX_GENI:
            if (argumentCount != 3) return FALSE;
            glTexGeni(arguments[0], arguments[1], (LONG)arguments[2]);
            break;
        case MGLPPC_BATCH_DRAW_T2F_C4UB_V3F:
            if (argumentCount < 8 || arguments[1] == 0 ||
                arguments[1] > (0xffffffffUL - 2) / 6 ||
                argumentCount != 2 + arguments[1] * 6)
                return FALSE;
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glVertexPointer(3, GL_FLOAT, 24, &arguments[2]);
            glTexCoordPointer(2, GL_FLOAT, 24,
                              (const UBYTE *)&arguments[2] + 12);
            glColorPointer(4, MGL_UBYTE_ARGB, 24,
                           (const UBYTE *)&arguments[2] + 20);
            glDrawArrays(arguments[0], 0, arguments[1]);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
            break;
        default:
            return FALSE;
        }

        offset += argumentCount;
    }

    return TRUE;
}

static void HandleMessage(MGLPPCMessage *message,
                          MGLPPCHostState *state)
{
    if (message->magic != MGLPPC_PROTOCOL_MAGIC) {
        message->status = MGLPPC_STATUS_BAD_MESSAGE;
        return;
    }
    /* v14 and v15 peers are both served; the cmd-22 value[5] layout is
     * selected per message below. */
    if (message->protocolVersion != MGLPPC_PROTOCOL_VERSION &&
        message->protocolVersion != 14UL) {
        message->status = MGLPPC_STATUS_BAD_VERSION;
        return;
    }
    if (message->payloadWords > MGLPPC_MAX_BATCH_WORDS ||
        MGLPPC_MESSAGE_SIZE(message->payloadWords) >
            message->execMessage.mn_Length) {
        message->status = MGLPPC_STATUS_BAD_MESSAGE;
        return;
    }

    message->status = MGLPPC_STATUS_OK;
    if (state->cpSegmentLeased && (state->cpFailed ||
        (state->ringControl && state->ringControl->error))) {
        state->cpFailed = TRUE;
        message->status = MGLPPC_STATUS_GL_ERROR;
        return;
    }
    switch (message->command) {
    case MGLPPC_CMD_HELLO:
        if (!state->backendOpen) {
            message->status = MGLPPC_STATUS_UNSUPPORTED;
            break;
        }
        message->value[0] = MiniGLBase->lib_Version;
        message->value[1] = MiniGLBase->lib_Revision;
        message->value[2] = MiniGLBase->lib_OpenCnt;
        state->nativeSubimage = QueryBackendSubimage(state->context != NULL);
        {
        UBYTE nodirect[8], trace[8], nomailbox[8];
        nodirect[0] = 0;
        trace[0] = 0;
        nomailbox[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_NOMAILBOX", nomailbox,
               (LONG)sizeof(nomailbox), GVF_GLOBAL_ONLY);
        state->mailboxDisabled = nomailbox[0] ? 1 : 0;
        GetVar((CONST_STRPTR)"MGLPPC_NODIRECT", nodirect,
               (LONG)sizeof(nodirect), GVF_GLOBAL_ONLY);
        state->directDisabled = nodirect[0] ? 1 : 0;
        ReadUploadTraceOption(trace, (LONG)sizeof(trace));
        state->verboseUploads = trace[0] ? 1 : 0;
        {
            UBYTE ibdry[8];
            ibdry[0] = 0;
            state->ibDryRun =
                GetVar((CONST_STRPTR)"MGLPPC_IBDRY", ibdry,
                       (LONG)sizeof(ibdry), GVF_GLOBAL_ONLY) > 0 && ibdry[0] == '1';
        }
        }
        message->value[3] = MGLPPC_CAP_REPLAY_V9 |
                            MGLPPC_CAP_SEMANTIC_INLINE |
                            MGLPPC_CAP_SEMANTIC_TEXTURE_V1 |
                            MGLPPC_CAP_HOST_EVENTS |
                            MGLPPC_CAP_TEXTURE_SHARED_V1 |
                            MGLPPC_CAP_READ_PIXELS_V1 |
                            (message->protocolVersion >= 15UL && state->nativeSubimage ?
                             MGLPPC_CAP_TEXTURE_SUBIMAGE_V1 : 0UL) |
                            (state->directDisabled ? 0UL :
                             MGLPPC_CAP_TEXTURE_DIRECT_V1)
#ifndef MGLPPC_DISABLE_RING
                            | MGLPPC_CAP_SEMANTIC_RING
                            | MGLPPC_CAP_INDIRECT | MGLPPC_CAP_CP_EMIT
                            | (state->mailboxDisabled ? 0UL :
                               (MGLPPC_CAP_MAILBOX_V1 |
                                MGLPPC_CAP_MAILBOX_UPLOAD_SHARED |
                                (state->directDisabled ? 0UL :
                                 MGLPPC_CAP_TEXTURE_DIRECT_BATCH_V1)))
#endif
#ifdef MGLPPC_HOST_VERIFY_CRC
                            ;
#else
                            | MGLPPC_CAP_CRC_FIRST_ONLY;
#endif
        /* W-80: the drain-entry protocol is offered from the moment a group
         * size is configured. Actual coalescing additionally requires the
         * driver capability, but a DRAIN entry with coalescing off is a
         * harmless extra drain, so the intent bit can be advertised here. */
        if (state->fenceGroupSize > 1UL)
            message->value[3] |= MGLPPC_CAP_FENCE_DRAIN;
        break;
    case MGLPPC_CMD_SHUTDOWN:
        if (state->cpSegmentLeased && state->context) {
            message->status = MGLPPC_STATUS_BUSY;
            break;
        }
#if MGLPPC_PROFILE
        HostNativeProfileCapture(state);
#endif
        if (!DiscardSemantic(state, FALSE)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        DiscardProbe(state);
        state->running = 0;
        break;
    case MGLPPC_CMD_CREATE_CONTEXT:
        if (!state->backendOpen) {
            message->status = MGLPPC_STATUS_UNSUPPORTED;
            break;
        }
        if (state->context != NULL) {
            message->status = MGLPPC_STATUS_CONTEXT_EXISTS;
            break;
        }
        mglChooseWindowMode(message->value[3] ? GL_TRUE : GL_FALSE);
        mglChoosePixelDepth((int)message->value[2]);
        if (message->value[4] != 0)
            mglChooseVertexBufferSize((int)message->value[4]);
        if (message->value[5] != 0)
            mglChooseTextureBufferSize((int)message->value[5]);
        if (message->value[6] != 0)
            mglChooseMtexBufferSize((int)message->value[6]);
        if (message->value[7] != 0)
            mglChooseNumberOfBuffers((int)message->value[7]);
        state->context = mglCreateContext(0, 0,
                                          (int)message->value[0],
                                          (int)message->value[1]);
        if (state->context == NULL) {
            message->status = MGLPPC_STATUS_CONTEXT_FAILED;
            break;
        }
        if (!++state->contextSerial)
            ++state->contextSerial;
        state->contextId = state->contextSerial;
        state->insidePrimitive = 0;
        state->contextWindowed = message->value[3] != 0;
        printf("MiniGLPPC host: create windowed=%lu w=%lu h=%lu "
               "depth=%lu\n",
               (unsigned long)state->contextWindowed,
               (unsigned long)message->value[0],
               (unsigned long)message->value[1],
               (unsigned long)message->value[2]);
        fflush(stdout);
#if MGLPPC_HOST_TRACE
        {
            char rep[128];
            sprintf(rep, "host: create windowed=%lu w=%lu h=%lu "
                    "depth=%lu\n",
                    (unsigned long)state->contextWindowed,
                    (unsigned long)message->value[0],
                    (unsigned long)message->value[1],
                    (unsigned long)message->value[2]);
            HostTraceLine(rep);
        }
#endif
        /* Reply value[0] is the window handle; the client runs
         * ModifyIDCMP(RAWKEY) against it, so it must be resolved now, not
         * lazily -- a NULL here silences the keyboard entirely. */
        state->contextWindow = (struct Window *)mglGetWindowHandle();
        state->semanticBoundTexture = 0;
        state->unpack[0] = 4;
        state->unpack[1] = state->unpack[2] = state->unpack[3] = 0;
        /* Native CreateContext clears the context and sets both alignments to 4. */
        state->pack[0] = 4;
        state->pack[1] = state->pack[2] = state->pack[3] = 0;
        message->contextId = state->contextId;
        {
            MGLR200PrivateBridgeV1 bridge;
            if (!QueryBackendBridge(&bridge) ||
                glGetError() != GL_NO_ERROR) {
                mglDeleteContext();
                state->context = NULL;
                state->contextId = 0;
                state->contextWindowed = 0;
                state->contextWindow = NULL;
                message->contextId = 0;
                message->status = MGLPPC_STATUS_UNSUPPORTED;
                break;
            }
            message->value[0] = (ULONG)state->contextWindow;
            message->value[1] = bridge.width;
            message->value[2] = bridge.height;
            message->value[3] = bridge.colorSurfaceToken;
            message->value[4] = bridge.depthSurfaceToken;
            message->value[5] = bridge.radeonCaps;
            message->value[6] = bridge.batchDwords;
            message->value[7] =
                (bridge.flags & MGL_R200_PRIVATE_BRIDGE_COMPACT_TCL) != 0;
        }
#if MGLPPC_PROFILE
        /* The successful bridge check consumed GL_NO_ERROR. Clear only the
         * hint's own error here; diagnostics must not fail context creation. */
        glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);
        HostNativeEnableError = glGetError();
        HostNativeReported = FALSE;
#endif
        break;
    case MGLPPC_CMD_CLEAR_PRESENT:
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        glClearColor(BitsToFloat(message->value[0]),
                     BitsToFloat(message->value[1]),
                     BitsToFloat(message->value[2]),
                     BitsToFloat(message->value[3]));
        glClear(GL_COLOR_BUFFER_BIT);
        mglSwitchDisplay();
        message->value[0] = glGetError();
        if (message->value[0] != GL_NO_ERROR) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        if (message->value[4] != 0)
            Delay(message->value[4]);
        break;
    case MGLPPC_CMD_DESTROY_CONTEXT:
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
#if MGLPPC_PROFILE
        HostNativeProfileCapture(state);
#endif
        /* Force the release: a dead ring must not hold the context (and the
         * display) until reboot. An error status is reported but the
         * context is released regardless. */
        if (!DiscardSemantic(state, TRUE))
            message->status = MGLPPC_STATUS_GL_ERROR;
        mglDeleteContext();
        DiscardUpload(state);
        state->context = NULL;
        state->contextId = 0;
        state->insidePrimitive = 0;
        state->contextWindowed = 0;
        state->contextWindow = NULL;
        state->semanticBoundTexture = 0;
        break;
    case MGLPPC_CMD_EXECUTE_BATCH:
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (state->semanticRegistered) {
            message->status = MGLPPC_STATUS_BUSY;
            break;
        }
        if (!state->displayLocked) {
            if (!mglLockDisplay()) {
                message->status = MGLPPC_STATUS_GL_ERROR;
                message->value[0] = GL_INVALID_OPERATION;
                break;
            }
            state->displayLocked = 1;
        }
        /* Report errors caused by this batch, not stale errors left by a
         * preceding texture/context command. */
        (void)glGetError();
        if (!ExecuteBatch(message, state)) {
            message->status = MGLPPC_STATUS_BAD_BATCH;
            break;
        }
        if (!state->insidePrimitive) {
            message->value[0] = glGetError();
            if (message->value[0] != GL_NO_ERROR)
                message->status = MGLPPC_STATUS_GL_ERROR;
        }
        break;
    case MGLPPC_CMD_GEN_TEXTURES:
        {
            GLuint textures[8] = { 0 };
            ULONG count = message->value[0];
            ULONG i;
            if (state->context == NULL || message->contextId != state->contextId) {
                message->status = MGLPPC_STATUS_NO_CONTEXT;
                break;
            }
            if (count == 0 || count > 8) {
                message->status = MGLPPC_STATUS_BAD_PAYLOAD;
                break;
            }
            (void)glGetError();
            glGenTextures((GLsizei)count, textures);
            message->value[0] = glGetError();
            if (message->value[0] != GL_NO_ERROR) {
                message->status = MGLPPC_STATUS_GL_ERROR;
                break;
            }
            for (i = 0; i < count; ++i)
                message->value[i] = textures[i];
        }
        break;
    case MGLPPC_CMD_DELETE_TEXTURES:
        {
            GLuint textures[8];
            ULONG count = message->value[0];
            ULONG i;
            if (state->context == NULL || message->contextId != state->contextId) {
                message->status = MGLPPC_STATUS_NO_CONTEXT;
                break;
            }
            if (count == 0 || count > 7) {
                message->status = MGLPPC_STATUS_BAD_PAYLOAD;
                break;
            }
            if (!DrainSemanticFence(state)) {
                message->status = MGLPPC_STATUS_GL_ERROR;
                break;
            }
            for (i = 0; i < count; ++i)
                textures[i] = message->value[i + 1];
            (void)glGetError();
            glDeleteTextures((GLsizei)count, textures);
            message->value[0] = glGetError();
            if (message->value[0] != GL_NO_ERROR)
                message->status = MGLPPC_STATUS_GL_ERROR;
            else
                for (i = 0; i < count; ++i)
                    if (textures[i] == state->semanticBoundTexture)
                        state->semanticBoundTexture = 0;
        }
        break;
    case MGLPPC_CMD_TEXTURE_UPLOAD_BEGIN:
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        DiscardUpload(state);
        state->uploadBytes = TextureByteCount(message->value[3],
                                               message->value[4],
                                               message->value[6],
                                               message->value[7]);
        if (state->uploadBytes == 0) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        state->uploadPixels = AllocVec(state->uploadBytes, MEMF_ANY);
        if (state->uploadPixels == NULL) {
            message->status = MGLPPC_STATUS_NO_MEMORY;
            state->uploadBytes = 0;
            break;
        }
        memcpy(state->uploadArgs, message->value, sizeof(state->uploadArgs));
        break;
    case MGLPPC_CMD_TEXTURE_UPLOAD_CHUNK:
        if (state->uploadPixels == NULL ||
            state->uploadReceived > state->uploadBytes ||
            message->value[0] != state->uploadReceived ||
            message->value[1] > message->payloadWords * sizeof(ULONG) ||
            message->value[1] > state->uploadBytes - state->uploadReceived) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        memcpy(state->uploadPixels + state->uploadReceived,
               message->payload, message->value[1]);
        state->uploadReceived += message->value[1];
        break;
    case MGLPPC_CMD_TEXTURE_UPLOAD_END:
        if (state->uploadPixels == NULL ||
            state->uploadReceived != state->uploadBytes) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            DiscardUpload(state);
            break;
        }
        /* Never leave request fields in a reply: an early return below
         * must not let the frontend read request data as bridge results. */
        message->value[1] = 0;
        message->value[2] = 0;
        message->value[3] = 0;
        message->value[4] = 0;
        message->value[5] = 0;
        message->value[6] = 0;
        message->value[7] = 0;
        CompleteTextureUpload(state, message, state->uploadArgs,
                              state->uploadPixels);
        DiscardUpload(state);
        break;
    case MGLPPC_CMD_TEXTURE_DIRECT_BEGIN:
        {
        MGLR200PrivateTexDirectBeginV2 bridge;
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        {
        ULONG t0 = state->verboseUploads ? ProbeTicks() : 0;
        BOOL ok = BeginBackendDirectTexture(message->value[0],
                                            message->value[1],
                                            message->value[2],
                                            message->value[3], &bridge);
        ++state->uploadCount;
        if (!ok)
            ++state->uploadFails;
        if (state->verboseUploads) {
            printf("host: dbeg n=%lu tex=%lu wh=%lux%lu ok=%d ticks=%lu hz=%lu "
                   "addr=%08lx pitch=%lu flags=%lu\n",
                   (unsigned long)state->uploadCount,
                   (unsigned long)message->value[0],
                   (unsigned long)message->value[1],
                   (unsigned long)message->value[2],
                   ok ? 1 : 0,
                    (unsigned long)(ProbeTicks() - t0),
                    (unsigned long)TimerClockRate,
                   (unsigned long)bridge.cpuAddress,
                   (unsigned long)bridge.pitch,
                   (unsigned long)bridge.flags);
            fflush(stdout);
        }
        if (!ok) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        }
        message->value[1] = bridge.cpuAddress;
        message->value[2] = bridge.pitch;
        message->value[3] = bridge.bytes;
        message->value[4] = bridge.deviceEpoch;
        message->value[5] = bridge.format;
        message->value[6] = bridge.flags;
        }
        break;
    case MGLPPC_CMD_TEXTURE_DIRECT_COMMIT:
        {
        MGLR200PrivateTexDirectCommitV2 bridge;
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        /* Deliberately no DrainSemanticFence: the filled surface is
         * fresh, so no in-flight record references it and installing
         * it must not wait for the pipeline. */
        {
        ULONG t0 = state->verboseUploads ? ProbeTicks() : 0;
        ULONG srcAddress = message->value[1];
        BOOL ok = CommitBackendDirectTexture(message->value[0],
                                             srcAddress, &bridge);
        if (state->verboseUploads) {
            printf("host: dcom n=%lu tex=%lu ok=%d ticks=%lu hz=%lu token=%08lx "
                   "v7=%08lx\n",
                   (unsigned long)state->uploadCount,
                   (unsigned long)message->value[0],
                   ok ? 1 : 0,
                   (unsigned long)(ProbeTicks() - t0),
                   (unsigned long)TimerClockRate,
                   (unsigned long)bridge.surfaceToken,
                   (unsigned long)bridge.deviceEpoch);
            fflush(stdout);
        }
        if (!ok) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        }
        message->value[1] = bridge.surfaceToken;
        message->value[2] = bridge.width;
        message->value[3] = bridge.height;
        message->value[4] = bridge.definedLevels;
        message->value[5] = bridge.levelCount;
        message->value[6] = bridge.contentSerial;
        message->value[7] = bridge.deviceEpoch;
        }
        break;
    case MGLPPC_CMD_TEXTURE_DIRECT_ABORT:
        {
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        AbortBackendDirectTexture(message->value[0]);
        }
        break;
    case MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2:
        {
        MGLR200PrivateTexDirectBeginV2 bridgeA, bridgeB;
        BOOL okA, okB;
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (message->value[0] == message->value[4] || !message->value[0] ||
            !message->value[4]) {
            /* Two distinct named textures are required: the second Begin
             * would release the first tile's pending allocation. */
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        {
        ULONG t0 = state->verboseUploads ? ProbeTicks() : 0;
        okA = BeginBackendDirectTexture(message->value[0], message->value[1],
                                        message->value[2], message->value[3],
                                        &bridgeA);
        okB = okA ? BeginBackendDirectTexture(message->value[4],
                                              message->value[5],
                                              message->value[6],
                                              message->value[7], &bridgeB)
                  : FALSE;
        if (okA)
            ++state->uploadCount;
        if (okB)
            ++state->uploadCount;
        if (!okA || !okB)
            ++state->uploadFails;
        if (state->verboseUploads) {
            printf("host: dbeg2 n=%lu tex=%lu %lux%lu tex=%lu %lux%lu ok=%d ticks=%lu "
                   "hz=%lu addr=%08lx/%08lx pitch=%lu/%lu\n",
                   (unsigned long)state->uploadCount,
                   (unsigned long)message->value[0],
                   (unsigned long)message->value[1],
                   (unsigned long)message->value[2],
                   (unsigned long)message->value[4],
                   (unsigned long)message->value[5],
                   (unsigned long)message->value[6],
                   okA && okB ? 1 : 0,
                   (unsigned long)(ProbeTicks() - t0),
                   (unsigned long)TimerClockRate,
                   (unsigned long)bridgeA.cpuAddress,
                   (unsigned long)bridgeB.cpuAddress,
                   (unsigned long)bridgeA.pitch,
                   (unsigned long)bridgeB.pitch);
            fflush(stdout);
        }
        }
        if (!okA || !okB) {
            /* Transactional: never leave a half-begun pair behind. */
            if (okA)
                AbortBackendDirectTexture(message->value[0]);
            if (okB)
                AbortBackendDirectTexture(message->value[4]);
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        message->value[0] = 0;
        message->value[1] = bridgeA.cpuAddress;
        message->value[2] = bridgeA.pitch;
        message->value[3] = bridgeB.cpuAddress;
        message->value[4] = bridgeB.pitch;
        message->value[5] = bridgeA.format;
        message->value[6] = bridgeB.format;
        message->value[7] = bridgeA.deviceEpoch;
        }
        break;
    case MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2:
        {
        MGLR200PrivateTexDirectCommitV2 bridgeA, bridgeB;
        BOOL okA, okB;
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (message->value[0] == message->value[2] || !message->value[0] ||
            !message->value[2]) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        /* Deliberately no DrainSemanticFence: both filled surfaces are
         * fresh, so installing them must not wait for the pipeline. */
        {
        ULONG t0 = state->verboseUploads ? ProbeTicks() : 0;
        okA = CommitBackendDirectTexture(message->value[0], message->value[1],
                                         &bridgeA);
        okB = CommitBackendDirectTexture(message->value[2], message->value[3],
                                         &bridgeB);
        if (state->verboseUploads) {
            printf("host: dcom2 tex=%lu ok=%d token=%08lx tex=%lu ok=%d token=%08lx "
                   "ticks=%lu hz=%lu\n",
                   (unsigned long)message->value[0], okA ? 1 : 0,
                   (unsigned long)bridgeA.surfaceToken,
                   (unsigned long)message->value[2], okB ? 1 : 0,
                   (unsigned long)bridgeB.surfaceToken,
                   (unsigned long)(ProbeTicks() - t0),
                   (unsigned long)TimerClockRate);
            fflush(stdout);
        }
        }
        message->value[0] = (okA ? 1UL : 0UL) | (okB ? 2UL : 0UL);
        message->value[1] = bridgeA.surfaceToken;
        message->value[2] = bridgeB.surfaceToken;
        message->value[3] = bridgeA.deviceEpoch;
        message->value[4] = bridgeA.contentSerial;
        message->value[5] = bridgeB.contentSerial;
        message->value[6] = bridgeA.definedLevels |
                            (bridgeA.levelCount << 16);
        message->value[7] = bridgeB.definedLevels |
                            (bridgeB.levelCount << 16);
        if (!okA || !okB)
            message->status = MGLPPC_STATUS_GL_ERROR;
        }
        break;
    case MGLPPC_CMD_READ_PIXELS_SHARED:
        {
        ULONG args[8], rowBytes, stride, offset, bytes, index;
        struct MemHeader *region;
        BOOL writable = FALSE;
        GLenum error, restoreError;
        memcpy(args, message->value, sizeof(args));
        memset(message->value, 0, sizeof(message->value));
        if (!state->context || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (message->protocolVersion < 15UL) {
            message->status = MGLPPC_STATUS_UNSUPPORTED;
            break;
        }
        if (!state->semanticRegistered || state->insidePrimitive) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            message->value[0] = GL_INVALID_OPERATION;
            break;
        }
        if (args[4] != GL_RGBA || args[5] != GL_UNSIGNED_BYTE) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            message->value[0] = GL_INVALID_ENUM;
            break;
        }
        if (message->payloadWords || args[2] > 0x7fffffffUL ||
            args[3] > 0x7fffffffUL || !args[6] ||
            !MGLPPCTextureSpan(args[2], args[3], 4, 1, 0, 0, 0, args[6],
                &rowBytes, &stride, &offset, &bytes) ||
            bytes != args[7] || bytes > MGLPPC_READ_PIXELS_MAX_BYTES) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        /* Validate all bytes, not just TypeOfMem(start), before cache access.
         * Allocation ownership remains the trusted sender's responsibility. */
        Forbid();
        for (region = (struct MemHeader *)SysBase->MemList.lh_Head;
             region->mh_Node.ln_Succ;
             region = (struct MemHeader *)region->mh_Node.ln_Succ) {
            if (args[6] >= (ULONG)region->mh_Lower &&
                args[6] < (ULONG)region->mh_Upper &&
                bytes <= (ULONG)region->mh_Upper - args[6]) {
                writable = TRUE;
                break;
            }
        }
        Permit();
        if (!writable) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            message->value[0] = GL_INVALID_OPERATION;
            break;
        }
        error = glGetError();
        if (!error) {
            /* Temporary canonical pack; the tracker retains the native state
             * from before this request, including nondefault replayed values. */
            for (index = 0; index < 4; ++index)
                glPixelStorei(HostPackNames[index], index ? 0 : 1);
            error = glGetError();
            if (!error) {
                CacheClearE((APTR)args[6], bytes, CACRF_ClearD);
                glReadPixels((GLint)args[0], (GLint)args[1], args[2], args[3],
                             args[4], args[5], (GLvoid *)args[6]);
                error = glGetError();
                CacheClearE((APTR)args[6], bytes, CACRF_ClearD);
            }
            for (index = 0; index < 4; ++index)
                glPixelStorei(HostPackNames[index], state->pack[index]);
            restoreError = glGetError();
            if (!error) error = restoreError;
        }
        message->value[0] = error;
        if (error) message->status = MGLPPC_STATUS_GL_ERROR;
        }
        break;
    case MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED:
        {
        ULONG args[8], format, type, bpp, bytes, rowBytes, stride, offset, index;
        MGLR200PrivateTextureV1 bridge;
        MGLR200PrivateTextureV2 preflight;
        struct MemHeader *region;
        BOOL readable = FALSE;
        GLenum error;
#if MGLPPC_PROFILE
        ULONG subStart = ProbeTicks(), preflightStart, nativeStart, bridgeStart;
#endif
        memcpy(args, message->value, sizeof(args));
        memset(message->value, 0, sizeof(message->value));
        if (!state->context || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (message->protocolVersion < 15UL || !state->nativeSubimage) {
            message->status = MGLPPC_STATUS_UNSUPPORTED;
            break;
        }
        if (!state->semanticRegistered || state->insidePrimitive) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            message->value[0] = GL_INVALID_OPERATION;
            break;
        }
        format = args[6] & 0xffffUL;
        type = args[6] >> 16;
        bpp = MGLPPC_TEXTURE_SOURCE_BYTES(format, type);
        if (message->payloadWords || !args[0] || args[0] >= 2048UL ||
            args[1] >= 12UL || args[2] > 2048UL || args[3] > 2048UL ||
            args[4] > 2048UL || args[5] > 2048UL || !args[7] ||
            !MGLPPCTextureSpan(args[4], args[5], bpp, 1, 0, 0, 0, args[7],
                &rowBytes, &stride, &offset, &bytes) ||
            bytes > MGLPPC_TEXTURE_SHARED_MAX_BYTES) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        /* TypeOfMem(start) alone admits ranges crossing RAM holes. Require the
         * entire span in one Exec RAM region before any cache operation/read.
         * Like other shared commands this is a trusted-peer ABI, not an
         * allocation-ownership sandbox; the sender owns the live allocation. */
        Forbid();
        for (region = (struct MemHeader *)SysBase->MemList.lh_Head;
             region->mh_Node.ln_Succ;
             region = (struct MemHeader *)region->mh_Node.ln_Succ) {
            if (args[7] >= (ULONG)region->mh_Lower &&
                args[7] < (ULONG)region->mh_Upper &&
                bytes <= (ULONG)region->mh_Upper - args[7]) {
                readable = TRUE;
                break;
            }
        }
        Permit();
        if (!readable) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            message->value[0] = GL_INVALID_OPERATION;
            break;
        }
        (void)glGetError();
        /* V1 requires an image and would reject direct textures before native
         * subimage gets the chance to materialize them. V2 checks residency
         * without forcing backing on the ordinary direct-upload hot path. */
        memset(&preflight, 0, sizeof(preflight));
        preflight.textureName = args[0];
#if MGLPPC_PROFILE
        ++HostSubimageCount;
        preflightStart = ProbeTicks();
#endif
        glGetIntegerv(MGL_R200_PRIVATE_TEXTURE_V2_QUERY, (GLint *)&preflight);
#if MGLPPC_PROFILE
        HostSubimagePreflightTicks += ProbeTicks() - preflightStart;
#endif
        error = glGetError();
        if (error || preflight.version != MGL_R200_PRIVATE_TEXTURE_V2_VERSION ||
            preflight.structSize != sizeof(preflight) ||
            preflight.textureName != args[0] || !preflight.surfaceToken ||
            !preflight.deviceEpoch) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            message->value[0] = error ? error : GL_INVALID_OPERATION;
            break;
        }
        if (!MGLPPCTextureRect(preflight.width, preflight.height, preflight.levelCount,
                preflight.definedLevels, args[1], args[2], args[3], args[4], args[5])) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            message->value[0] = GL_INVALID_VALUE;
            break;
        }
        /* Never rely on the last replayed draw's texture. Native active unit
         * stays zero on this protocol; explicitly bind the packet's name. */
        glBindTexture(GL_TEXTURE_2D, args[0]);
        error = glGetError();
        if (error) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            message->value[0] = error;
            break;
        }
        CacheClearE((APTR)args[7], bytes, CACRF_ClearD);
        for (index = 0; index < 4; ++index)
            glPixelStorei(HostUnpackNames[index], index ? 0 : 1);
#if MGLPPC_PROFILE
        nativeStart = ProbeTicks();
#endif
        glTexSubImage2D(GL_TEXTURE_2D, args[1], args[2], args[3],
                       args[4], args[5], format, type, (const GLvoid *)args[7]);
#if MGLPPC_PROFILE
        HostSubimageNativeTicks += ProbeTicks() - nativeStart;
#endif
        error = glGetError();
        for (index = 0; index < 4; ++index)
            glPixelStorei(HostUnpackNames[index], state->unpack[index]);
        glBindTexture(GL_TEXTURE_2D, state->semanticBoundTexture);
        if (!error) error = glGetError();
#if MGLPPC_PROFILE
        bridgeStart = ProbeTicks();
#endif
        if (!QueryBackendTexture(args[0], &bridge)) {
#if MGLPPC_PROFILE
            HostSubimageBridgeTicks += ProbeTicks() - bridgeStart;
#endif
            error = glGetError();
            if (!error) error = GL_INVALID_OPERATION;
        }
#if MGLPPC_PROFILE
        else
            HostSubimageBridgeTicks += ProbeTicks() - bridgeStart;
        HostSubimageTicks += ProbeTicks() - subStart;
#endif
        if (!error) error = glGetError();
        message->value[0] = error;
        if (error) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        message->value[1] = bridge.surfaceToken;
        message->value[2] = bridge.width;
        message->value[3] = bridge.height;
        message->value[4] = bridge.definedLevels;
        message->value[5] = bridge.levelCount;
        message->value[6] = bridge.contentSerial;
        message->value[7] = bridge.deviceEpoch;
        }
        break;
    case MGLPPC_CMD_TEXTURE_UPLOAD_SHARED:
        {
        ULONG args[8];
        ULONG bytes;
        ULONG uploadFormat, uploadType;
        APTR pixels = (APTR)message->value[6];
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        if (message->protocolVersion >= 15UL) {
            uploadFormat = message->value[5] & 0xffffUL;
            uploadType = message->value[5] >> 16;
        } else {
            uploadFormat = message->value[5];
            uploadType = GL_UNSIGNED_BYTE;
        }
        bytes = TextureByteCount(message->value[3], message->value[4],
                                 uploadFormat, uploadType);
        if ((MGLPPC_HOST_TRACE || state->verboseUploads) &&
            !state->sharedLogged) {
            printf("host: upload_shared bytes=%lu fmt=%lx type=%lx\n",
                   (unsigned long)bytes, (unsigned long)uploadFormat,
                   (unsigned long)uploadType);
            fflush(stdout);
            state->sharedLogged = 1;
        }
        if (message->value[0] != GL_TEXTURE_2D || pixels == NULL ||
            bytes == 0 || bytes != message->value[7] ||
            bytes > MGLPPC_TEXTURE_SHARED_MAX_BYTES ||
            TypeOfMem(pixels) == 0) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        DiscardUpload(state);
        /* The PPC publishes its own address in value[6]; validate it
         * above before cache maintenance dereferences it. */
        CacheClearE(pixels, bytes, CACRF_ClearD);
        args[0] = message->value[0];
        args[1] = message->value[1];
        args[2] = message->value[2];
        args[3] = message->value[3];
        args[4] = message->value[4];
        args[5] = 0;
        args[6] = uploadFormat;
        args[7] = uploadType;
        message->value[1] = 0;
        message->value[2] = 0;
        message->value[3] = 0;
        message->value[4] = 0;
        message->value[5] = 0;
        message->value[6] = 0;
        message->value[7] = 0;
        CompleteTextureUpload(state, message, args, pixels);
        }
        break;
    case MGLPPC_CMD_GET_INTEGER:
        if (state->context == NULL || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        /* Only the tagged texture-capability query is scalar in this private
         * namespace. Descriptor queries must never receive a four-byte local. */
        if ((message->value[0] & 0xfffff000UL) == 0x4d475000UL &&
            message->value[0] != MGL_R200_PRIVATE_TEXTURE_CAPS_V1_QUERY) {
            message->status = MGLPPC_STATUS_UNSUPPORTED;
            break;
        }
        {
            GLint value = 0;
            glGetIntegerv(message->value[0], &value);
            message->value[0] = (ULONG)value;
        }
        break;
    case MGLPPC_CMD_VRAM_PROBE_BEGIN:
        (void)BeginVramProbe(message, state);
        break;
    case MGLPPC_CMD_VRAM_PROBE_CONFIRM:
        (void)ConfirmVramProbe(message, state);
        break;
    case MGLPPC_CMD_VRAM_PROBE_END:
        (void)EndVramProbe(message, state);
        break;
    case MGLPPC_CMD_REGISTER_SEMANTIC_SLOTS:
        (void)RegisterSemanticSlots(message, state);
        break;
    case MGLPPC_CMD_REGISTER_SEMANTIC_RING:
        (void)RegisterSemanticRing(message, state);
        break;
    case MGLPPC_CMD_EXECUTE_SEMANTIC_SLOT:
        (void)ExecuteSemanticSlot(message, state);
        break;
    case MGLPPC_CMD_SEMANTIC_BARRIER:
        if (!state->context || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (!state->semanticRegistered ||
            message->value[5] != state->semanticEpoch ||
            (message->value[3] & ~(MGLPPC_SEMANTIC_FLAG_PRESENT |
                                    MGLPPC_SEMANTIC_FLAG_READBACK))) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        if (message->value[3] & MGLPPC_SEMANTIC_FLAG_PRESENT) {
            mglSwitchDisplay();
            mglUnlockDisplay();
            state->displayLocked = 0;
            if (glGetError() != GL_NO_ERROR)
                message->status = MGLPPC_STATUS_GL_ERROR;
        }
        if (message->status == MGLPPC_STATUS_OK &&
            (message->value[3] & MGLPPC_SEMANTIC_FLAG_READBACK)) {
            MGLLockInfo lock;
            UBYTE *base;
            if (!mglLockBack(&lock) || lock.depth != 32 ||
                !lock.base_address || !lock.width || !lock.height) {
                message->status = MGLPPC_STATUS_GL_ERROR;
                break;
            }
            base = (UBYTE *)lock.base_address;
            message->value[1] = *(ULONG *)(base +
                (lock.height / 2UL) * lock.pitch +
                (lock.width / 2UL) * sizeof(ULONG));
            message->value[2] = *(ULONG *)(base + 4UL * lock.pitch +
                                           4UL * sizeof(ULONG));
        }
        message->value[0] = state->semanticExecutes;
        break;
    case MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS:
        if (!state->context || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        if (!state->semanticRegistered ||
            message->value[5] != (state->ringControl ? state->ringEpoch :
                                                       state->semanticEpoch)) {
            message->status = MGLPPC_STATUS_BAD_PAYLOAD;
            break;
        }
        if (!DrainSemanticFence(state)) {
            message->status = MGLPPC_STATUS_GL_ERROR;
            break;
        }
        message->value[0] = state->semanticExecutes;
        if (!DiscardSemantic(state, FALSE))
            message->status = MGLPPC_STATUS_GL_ERROR;
        break;
    case MGLPPC_CMD_CP_SURFACE:
        {
            MGLR200PrivateSurfaceV1 surface;
            if (!state->cpEnabled || state->cpFailed || !state->context ||
                message->contextId != state->contextId || message->payloadWords ||
                !QueryCpSurface(state, message->value[0], &surface)) {
                message->status = MGLPPC_STATUS_UNSUPPORTED;
                break;
            }
            message->value[0] = GL_NO_ERROR;
            message->value[1] = surface.cpuAddress;
            message->value[2] = surface.gpuAddress;
            message->value[3] = surface.pitch;
            message->value[4] = surface.width;
            message->value[5] = surface.height;
            message->value[6] = surface.format;
            message->value[7] = surface.deviceEpoch;
        }
        break;
    case MGLPPC_CMD_BIND_TEXTURE:
        if (!state->context || message->contextId != state->contextId) {
            message->status = MGLPPC_STATUS_NO_CONTEXT;
            break;
        }
        (void)glGetError();
        glBindTexture(message->value[0], message->value[1]);
        message->value[0] = glGetError();
        if (message->value[0] != GL_NO_ERROR)
            message->status = MGLPPC_STATUS_GL_ERROR;
        else
            state->semanticBoundTexture = message->value[1];
        break;
    default:
        message->status = MGLPPC_STATUS_UNKNOWN_COMMAND;
        break;
    }
}

static BOOL PollMailbox(MGLPPCHostState *state)
{
    volatile MGLPPCMailboxV1 *box = state->mailbox;
    MGLPPCMessage message;
    ULONG serial, next, index;
#if MGLPPC_PROFILE
    ULONG start;
#endif

    if (!box)
        return FALSE;
    /* Never write the request lines after registration. CacheClearE also
     * invalidates them; no dirty host data can overwrite PPC publication. */
    CacheClearE((APTR)&box->reqSerial, 32, CACRF_ClearD);
    serial = box->reqSerial;
    if (serial == state->mailboxSerial)
        return FALSE;
    HOST_START(start);
    next = state->mailboxSerial + 1UL;
    if (!next)
        next = 1;
    CacheClearE((APTR)&box->reqValue[0], 32, CACRF_ClearD);
    memset(&message, 0, sizeof(message));
    message.execMessage.mn_Length = MGLPPC_MESSAGE_HEADER_SIZE;
    message.magic = MGLPPC_PROTOCOL_MAGIC;
    message.protocolVersion = MGLPPC_PROTOCOL_VERSION;
    message.sequence = serial;
    message.command = box->command;
    message.contextId = box->contextId;
    for (index = 0; index < 8; ++index)
        message.value[index] = box->reqValue[index];
    if (serial != next || box->magic != MGLPPC_MAILBOX_MAGIC ||
        box->version != MGLPPC_MAILBOX_VERSION ||
        box->epoch != state->ringEpoch)
        message.status = MGLPPC_STATUS_STALE_GENERATION;
    else if (!MGLPPC_MAILBOX_COMMAND_ALLOWED(message.command))
        message.status = MGLPPC_STATUS_UNKNOWN_COMMAND;
    else if (!state->context || message.contextId != state->contextId)
        message.status = MGLPPC_STATUS_NO_CONTEXT;
    else {
        /* Prevent a handler that reaches back into RingPoll from recursing
         * into the inner mailbox service. */
        HostRingMailboxActive = TRUE;
        HandleMessage(&message, state);
        HostRingMailboxActive = FALSE;
    }
    for (index = 0; index < 8; ++index)
        box->ackValue[index] = message.value[index];
    CacheClearE((APTR)&box->ackValue[0], 32, CACRF_ClearD);
    box->status = (ULONG)message.status;
    CacheClearE((APTR)&box->ackSerial, 32, CACRF_ClearD);
    box->ackSerial = serial;
    CacheClearE((APTR)&box->ackSerial, 32, CACRF_ClearD);
    state->mailboxSerial = serial;
#if MGLPPC_PROFILE
    {
        ULONG mailboxElapsed = HostClock() - start;
        HostMailboxCount++;
        HostMailboxTicks += mailboxElapsed;
        if (message.command == MGLPPC_CMD_TEXTURE_DIRECT_BEGIN) {
            HostDirectBeginCount++;
            HostDirectBeginTicks += mailboxElapsed;
        } else if (message.command == MGLPPC_CMD_TEXTURE_DIRECT_COMMIT) {
            HostDirectCommitCount++;
            HostDirectCommitTicks += mailboxElapsed;
        } else if (message.command == MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2) {
            HostDirectBegin2Count++;
            HostDirectBegin2Ticks += mailboxElapsed;
        } else if (message.command == MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2) {
            HostDirectCommit2Count++;
            HostDirectCommit2Ticks += mailboxElapsed;
        }
    }
#endif
    return TRUE;
}

/* Startup only. A busy host must never turn an ASCII digit/sign into a
 * priority above input.device. Unset/empty preserves the inherited priority. */
/* W-80: MGLPPC_FENCE_GROUP selects how many CP dispatches share one fence.
 * Unset/1 keeps the legacy fence-every-dispatch behaviour. Values are
 * clamped to the ring slot count so every retirement window still contains
 * a fence; the value only takes effect when the driver advertises
 * RADEON3D_CAP_FENCE_COALESCE at registration. */
static ULONG ReadFenceGroupSize(void)
{
    char value[16], *end;
    LONG length;
    long parsed;
    ULONG index;

    length = GetVar((CONST_STRPTR)"MGLPPC_FENCE_GROUP", (UBYTE *)value,
                    sizeof(value), GVF_GLOBAL_ONLY);
    if (length <= 0)
        return 1;
    if ((ULONG)length >= sizeof(value) - 1UL)
        return 1;
    /* Digits only: strtol would accept leading space or signs, and anything
     * unusual must fall back to the safe legacy group of one. */
    for (index = 0; index < (ULONG)length; ++index)
        if (value[index] < '0' || value[index] > '9')
            return 1;
    value[length] = '\0';
    parsed = strtol(value, &end, 10);
    if (end == value || *end || parsed < 2L)
        return 1;
    if (parsed > (long)MGLPPC_RING_SLOTS)
        parsed = (long)MGLPPC_RING_SLOTS;
    return (ULONG)parsed;
}

static BOOL ReadHostPriority(LONG *priority, BOOL *specified)
{
    char value[16], *end;
    LONG length;
    long parsed;

    *specified = FALSE;
    length = GetVar((CONST_STRPTR)"MGLPPC_HOSTPRI", (UBYTE *)value,
                    sizeof(value), GVF_GLOBAL_ONLY);
    if (length <= 0)
        return TRUE;
    /* GetVar may report only the copied prefix when its buffer is full. */
    if ((ULONG)length >= sizeof(value) - 1UL)
        return FALSE;
    value[length] = '\0';
    parsed = strtol(value, &end, 10);
    if (end == value || *end || parsed < -20 || parsed > 2)
        return FALSE;
    *priority = (LONG)parsed;
    *specified = TRUE;
    return TRUE;
}

int main(int argc, char **argv)
{
    struct MsgPort *hostPort;
    struct MsgPort *existingPort;
    MGLPPCHostState state;
    LONG hostPriority = 0;
    BOOL prioritySpecified;

    memset(&state, 0, sizeof(state));
    state.running = 1;
    state.nextProbeCookie = 1;

    /* Idempotent launch: if a host already owns the service port, exit
     * successfully instead of starting a second one. Probe it first so a
     * healthy host and a wedged one are distinguishable -- a wedged host
     * holds the port name but never answers, and the only recovery for that
     * is a reboot. Runs before any backend or timer setup, so the duplicate
     * launch path costs nothing. */
    existingPort = FindPort((CONST_STRPTR)MGLPPC_HOST_PORT_NAME);
    if (existingPort != NULL) {
        /* Idempotent launch: a second host exits successfully instead of
         * adding another port. The atomic FindPort/AddPort window below is
         * still there for true simultaneous starts. */
        printf("MiniGLPPC host: already running, exiting\n");
        fflush(stdout);
        return 0;
    }

    if (!ReadHostPriority(&hostPriority, &prioritySpecified)) {
        printf("MiniGLPPC host: MGLPPC_HOSTPRI must be an integer from -20 to 2\n");
        return 20;
    }
    state.fenceGroupSize = ReadFenceGroupSize();
    if (state.fenceGroupSize > 1UL) {
        printf("MiniGLPPC host: fence group=%lu (driver capability gated)\n",
               (unsigned long)state.fenceGroupSize);
        fflush(stdout);
    }
    if (argc > 1 && strcmp(argv[1], "--probe-only") == 0) {
        printf("MiniGLPPC host: probe-only mode\n");
    } else if (!MiniGLOpen()) {
        printf("MiniGLPPC host: cannot open MiniGL dispatch ABI\n");
        return 20;
    } else {
        state.backendOpen = 1;
    }
    printf("MiniGLPPC host: step=backend_open\n");
    fflush(stdout);
    if (!OpenTimerClock()) {
        printf("MiniGLPPC host: cannot open EClock timer\n");
        if (state.backendOpen)
            MiniGLClose();
        return 20;
    }
    printf("MiniGLPPC host: step=timer\n");
    fflush(stdout);

    hostPort = CreateMsgPort();
    if (hostPort == NULL) {
        printf("MiniGLPPC host: cannot create message port\n");
        CloseDevice((struct IORequest *)&TimerRequest);
        TimerBase = NULL;
        if (state.backendOpen)
            MiniGLClose();
        return 20;
    }

    hostPort->mp_Node.ln_Name = (char *)MGLPPC_HOST_PORT_NAME;
    hostPort->mp_Node.ln_Pri = 0;
    Forbid();
    existingPort = FindPort((CONST_STRPTR)MGLPPC_HOST_PORT_NAME);
    if (existingPort == NULL)
        AddPort(hostPort);
    Permit();
    if (existingPort != NULL) {
        printf("MiniGLPPC host: another host is already running\n");
        DeleteMsgPort(hostPort);
        CloseDevice((struct IORequest *)&TimerRequest);
        TimerBase = NULL;
        if (state.backendOpen)
            MiniGLClose();
        return 10;
    }
    if (state.backendOpen)
        printf("MiniGLPPC host: ready, MiniGL %u.%u\n",
               (unsigned int)MiniGLBase->lib_Version,
               (unsigned int)MiniGLBase->lib_Revision);
    else
        printf("MiniGLPPC host: ready for probes\n");
    printf("MiniGLPPC host: step=port_added\n");
    fflush(stdout);

    HOST_PACING_INIT();
#if MGLPPC_PROFILE
    HostClockHz = TimerClockRate;
    HostWallStart = HostClock();
#endif
    if (prioritySpecified) {
        LONG previous = SetTaskPri(FindTask(NULL), hostPriority);
        printf("MiniGLPPC host: priority=%ld previous=%ld\n",
               (long)hostPriority, (long)previous);
    }
    printf("MiniGLPPC host: step=service_enter\n");
    fflush(stdout);
    HostTraceOpen();
    HostTraceLine("host: service_enter\n");
    while (state.running) {
        MGLPPCMessage *message;
        ULONG ringBusy;
        BOOL workHandled;
        static ULONG idleLoops;
        static ULONG idleSince;
#if MGLPPC_PROFILE
        ULONG handleStart, handleElapsed;
        ULONG pollStart = 0;
        BOOL sample = (++HostPollLoops & 15UL) == 0UL;

        /* RAM-only, unscaled samples of top-level polls every 16th loop.
         * Count sampled calls even when elapsed ticks are zero. The extra
         * PollMailbox before each Exec message is not sampled here. */
        if (sample)
            pollStart = HostClock();
#endif
        workHandled = PollMailbox(&state);
#if MGLPPC_PROFILE
        if (sample) {
            HostMailboxPollTicks += HostClock() - pollStart;
            ++HostMailboxPollCalls;
        }
#endif
        while ((message = (MGLPPCMessage *)GetMsg(hostPort)) != NULL) {
            /* Published mailbox work precedes destruction/unregister, even
             * when an Exec message arrives during the previous handler. */
            (void)PollMailbox(&state);
            HOST_START(handleStart);
            if (message->execMessage.mn_Length >=
                    MGLPPC_MESSAGE_HEADER_SIZE)
                HandleMessage(message, &state);
#if MGLPPC_PROFILE
            handleElapsed = HostClock() - handleStart;
            if (message->command == MGLPPC_CMD_EXECUTE_SEMANTIC_SLOT) {
                HostExecuteMsgCount++;
            } else if (message->command == MGLPPC_CMD_SEMANTIC_BARRIER) {
                HostBarrierCount++;
                HostBarrierTicks += handleElapsed;
            } else {
                HostControlCount++;
                HostControlTicks += handleElapsed;
            }
#endif
            ReplyMsg(&message->execMessage);
            workHandled = TRUE;
        }
        ringBusy = RingPoll(&state);
#if MGLPPC_PROFILE
        HostRingTicks += ringBusy;
        if (sample)
            pollStart = HostClock();
#endif
        /* A fence completed by this poll still counts as work this pass. */
        if (state.semanticFence)
            workHandled = TRUE;
        PollQueuedFence(&state);
#if MGLPPC_PROFILE
        if (sample) {
            HostFencePollTicks += HostClock() - pollStart;
            ++HostFencePollCalls;
        }
        if (sample)
            pollStart = HostClock();
#endif
        PollContextEvents(&state);
#if MGLPPC_PROFILE
        if (sample) {
            HostEventPollTicks += HostClock() - pollStart;
            ++HostEventPollCalls;
        }
#endif
        /* A pure busy loop pins the 68k CPU at 100% forever, starving
         * everything else on the machine even when no client is running.
         * Once the loop has been idle for a while, sleep 20ms per pass:
         * pending work keeps the loop busy and never delays, while an idle
         * host costs a couple of percent instead of a whole core. Activity
         * must not depend on lastExecutes/lastPresents: those report-coupled
         * latches do not advance with profiling off. Use actual mailbox,
         * message, ring and fence work, including newly published entries. */
        if (workHandled || ringBusy || state.semanticFence ||
            (state.ringControl && !state.ringControl->error &&
             state.ringCompleted < state.ringControl->head)) {
            idleLoops = 0;
            idleSince = 0;
        } else {
            ++idleLoops;
            if (idleLoops == 1UL)
                idleSince = ProbeTicks();
            else if (!(idleLoops & 1023UL) && HostIdleSleepTicks &&
                     (ULONG)(ProbeTicks() - idleSince) >=
                         HostIdleSleepTicks) {
#if MGLPPC_IDLE_DIAG
            ULONG sleepStart = ProbeTicks();
            BOOL active = state.semanticRegistered != 0;
            BOOL rendering = active && HostExecuteCount != 0;
#endif
            Delay(1);
#if MGLPPC_IDLE_DIAG
            ULONG elapsed = ProbeTicks() - sleepStart;
            if (active) {
                ++IdleActiveSleeps;
                IdleActiveTicks += elapsed;
                if (rendering) {
                    ++IdleRenderSleeps;
                    IdleRenderTicks += elapsed;
                }
                if (state.ringControl &&
                    state.ringControl->head != state.ringCompleted)
                    ++IdleWakeBacklog;
            } else {
                ++IdleEmptySleeps;
            }
#endif
            idleLoops = 0;
            idleSince = 0;
            }
        }
    }
#if MGLPPC_PROFILE
    HostWallTicks = HostClock() - HostWallStart;
#endif

    RemPort(hostPort);
    DeleteMsgPort(hostPort);
#if MGLPPC_PROFILE
    HostNativeProfileCapture(&state);
#endif
    if (!DiscardSemantic(&state, FALSE))
        return 20; /* Normal SHUTDOWN cannot reach here with a live CP context. */
    if (state.cpDispatches)
        printf("CP_RUN dispatches=%lu dwords=%lu failed=%lu\n",
               (unsigned long)state.cpDispatches, (unsigned long)state.cpDwords,
               (unsigned long)state.cpFailed);
#if MGLPPC_PROFILE
    if (HostCpEntries && HostClockHz) {
        ULONG ticksPerMs = HostClockHz / 1000UL;

        if (!ticksPerMs)
            ticksPerMs = 1;
        printf("CP_PROF entries=%lu drain_ms=%lu dispatch_ms=%lu "
               "total_ms=%lu\n",
               (unsigned long)HostCpEntries,
               (unsigned long)(HostCpDrainTicks / ticksPerMs),
               (unsigned long)(HostCpDispatchTicks / ticksPerMs),
               (unsigned long)(HostCpTotalTicks / ticksPerMs));
        fflush(stdout);
    }
#endif
    if (state.context != NULL)
        mglDeleteContext();
    DiscardProbe(&state);
    DiscardUpload(&state);
    if (TimerBase) {
        CloseDevice((struct IORequest *)&TimerRequest);
        TimerBase = NULL;
    }
    if (state.backendOpen)
        MiniGLClose();
#if MGLPPC_PROFILE
    HostNativeProfileReport();
    if (HostClockHz) {
        ULONG busy = HostControlTicks + HostBarrierTicks +
                     HostTotalTicks + HostRingTicks + HostMailboxTicks;
        HostIdleTicks = HostWallTicks > busy ? HostWallTicks - busy : 0;
        printf("MiniGLPPC host: attribution "
               "scope=host_session_including_setup_idle_context_teardown "
               "inclusive=1 mutually_exclusive=0 counter_bits=32 "
               "busy=control+barrier+total+ring+mailbox "
               "idle=max(wall-busy,0) idle_is_cpu_idle=0\n");
        printf("MiniGLPPC host: session_ticks wall=%lu busy=%lu idle=%lu\n",
               (unsigned long)HostWallTicks, (unsigned long)busy,
               (unsigned long)HostIdleTicks);
        printf("MiniGLPPC host: session wall_us=%lu idle_us=%lu "
               "msgs control=%lu control_us=%lu barrier=%lu barrier_us=%lu "
               "execute_msgs=%lu ring_us=%lu\n",
               (unsigned long)ProbeMicros(0, HostWallTicks),
               (unsigned long)ProbeMicros(0, HostIdleTicks),
               (unsigned long)HostControlCount,
               (unsigned long)ProbeMicros(0, HostControlTicks),
               (unsigned long)HostBarrierCount,
               (unsigned long)ProbeMicros(0, HostBarrierTicks),
               (unsigned long)HostExecuteMsgCount,
               (unsigned long)ProbeMicros(0, HostRingTicks));
        fflush(stdout);
        printf("MiniGLPPC host: mailbox=%lu mailbox_us=%lu\n",
               (unsigned long)HostMailboxCount,
               (unsigned long)ProbeMicros(0, HostMailboxTicks));
        printf("MiniGLPPC host: direct_begin=%lu direct_begin_us=%lu "
               "direct_commit=%lu direct_commit_us=%lu\n",
               (unsigned long)HostDirectBeginCount,
               (unsigned long)ProbeMicros(0, HostDirectBeginTicks),
               (unsigned long)HostDirectCommitCount,
               (unsigned long)ProbeMicros(0, HostDirectCommitTicks));
        printf("MiniGLPPC host: direct_begin2=%lu direct_begin2_us=%lu "
               "direct_commit2=%lu direct_commit2_us=%lu\n",
               (unsigned long)HostDirectBegin2Count,
               (unsigned long)ProbeMicros(0, HostDirectBegin2Ticks),
               (unsigned long)HostDirectCommit2Count,
               (unsigned long)ProbeMicros(0, HostDirectCommit2Ticks));
        printf("MiniGLPPC host: uploads=%lu upload_fails=%lu\n",
               (unsigned long)state.uploadCount,
               (unsigned long)state.uploadFails);
        printf("HOST_SUBIMAGE count=%lu handler_us=%lu preflight_us=%lu "
               "native_texsub_us=%lu bridge_query_us=%lu\n",
               (unsigned long)HostSubimageCount,
               (unsigned long)ProbeMicros(0, HostSubimageTicks),
               (unsigned long)ProbeMicros(0, HostSubimagePreflightTicks),
               (unsigned long)ProbeMicros(0, HostSubimageNativeTicks),
               (unsigned long)ProbeMicros(0, HostSubimageBridgeTicks));
        printf("HOST_BRIDGE_CACHE enabled=%lu scope=ring_poll_burst "
               "v2_queries_including_registration=%lu hits=%lu\n",
               (unsigned long)MGLPPC_HOST_TARGET_CACHE,
               (unsigned long)HostBridgeQueries,
               (unsigned long)HostBridgeCacheHits);
        printf("HOST_POLL scope=host_session polls=top_level_only "
               "sample_period_loops=16 ticks=sampled_unscaled "
               "calls=sampled_including_zero_tick loops=%lu eclock_hz=%lu\n",
               (unsigned long)HostPollLoops, (unsigned long)HostClockHz);
        printf("HOST_POLL mailbox_ticks=%lu mailbox_calls=%lu "
               "fence_ticks=%lu fence_calls=%lu event_ticks=%lu event_calls=%lu\n",
               (unsigned long)HostMailboxPollTicks,
               (unsigned long)HostMailboxPollCalls,
               (unsigned long)HostFencePollTicks,
               (unsigned long)HostFencePollCalls,
               (unsigned long)HostEventPollTicks,
               (unsigned long)HostEventPollCalls);
    }
    if (HostClockHz) {
        printf("MiniGLPPC host: executes=%lu presents=%lu eclock_hz=%lu\n",
               (unsigned long)HostExecuteCount,
               (unsigned long)HostPresentCount,
               (unsigned long)HostClockHz);
        printf("MiniGLPPC host: stage_ticks wait=%lu clear=%lu crc=%lu "
               "query=%lu submit=%lu presentwait=%lu present=%lu total=%lu "
               "inclusive=1 mutually_exclusive=0 total_scope=execute_slot_only\n",
               (unsigned long)HostWaitTicks, (unsigned long)HostClearTicks,
               (unsigned long)HostCrcTicks, (unsigned long)HostQueryTicks,
               (unsigned long)HostSubmitTicks,
               (unsigned long)HostPresentWaitTicks,
               (unsigned long)HostPresentTicks,
               (unsigned long)HostTotalTicks);
        fflush(stdout);
    }
#else
    if (HostExecuteCount)
        printf("MiniGLPPC host: executes=%lu presents=%lu\n",
               (unsigned long)HostExecuteCount,
               (unsigned long)HostPresentCount);
#endif
    HOST_PACING_REPORT();
#if MGLPPC_IDLE_DIAG
    printf("HOST_IDLE scope=whole_host render_scope=registered_after_first_execute "
           "empty_sleeps=%lu active_sleeps=%lu active_us=%lu "
           "render_sleeps=%lu render_us=%lu wake_backlog=%lu\n",
           (unsigned long)IdleEmptySleeps, (unsigned long)IdleActiveSleeps,
           (unsigned long)ProbeMicros(0, IdleActiveTicks),
           (unsigned long)IdleRenderSleeps,
           (unsigned long)ProbeMicros(0, IdleRenderTicks),
           (unsigned long)IdleWakeBacklog);
#endif
    printf("MiniGLPPC host: stopped\n");
    return 0;
}
