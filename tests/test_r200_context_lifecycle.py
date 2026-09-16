"""Lifecycle and CP transport, using extracted production code on both CPUs.

python3 tests/test_r200_context_lifecycle.py
python3 tests/test_r200_context_lifecycle.py --regress-crc
python3 tests/test_r200_context_lifecycle.py --regress-cache
python3 tests/test_r200_context_lifecycle.py --regress-register
python3 tests/test_r200_context_lifecycle.py --regress-waits
python3 tests/test_r200_context_lifecycle.py --regress-ib-ack

GPU services, allocation and the separately tested MGLPPCEmitBatch are mocked.
The complete RingPoll and shared-payload target patcher are extracted,
including inline, COMMIT and STATE_BATCH. Native surface/indirect query handlers,
host registration/dispatch/fence polling/GET_INTEGER, lifecycle guards, and PPC
negotiation/publication/cache/barriers (including public glFinish) are real code.
The endian oracle models big-endian stores, not this host's memory byte order.
--regress-crc removes only CreateContext's CRC rearm to reproduce the old fault.
--regress-cache removes the post-service release clean to reproduce a dirty
host cache line overwriting the next PPC publication after ring wrap.
Scheduling opportunities are counted separately from DOS tick delays; the
production short-yield helper is checked by test_r200_transport_profile.py.
No SDK, native library or hardware is loaded. Test binaries stay in /tmp/opencode.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
F = (ROOT / "backend_r200/library/minigl_r200_warpos_compile_probe.c").read_text()
# Ignore the standalone compile-probe stubs preceding the real DLL frontend.
F = F[F.index("typedef struct R200PPCFrontendPlatform"):]
H = (ROOT / "backend_r200/minigl_ppc_host.c").read_text()
P = (ROOT / "backend_r200/include/libraries/minigl_ppc_protocol.h").read_text()
B = (ROOT / "backend_r200/include/libraries/minigl_r200_bridge.h").read_text()
N = (ROOT / "classic_68k/dev/MiniGL_Library_Source_Code/library/r200_state.c").read_text()
S = (ROOT / "backend_r200/library/r200_surface.c").read_text()
CORE = (ROOT / "classic_68k/dev/MiniGL_Library_Source_Code/library/minigl_r200.c").read_text()
R = (ROOT / "backend_r200/third_party/radeon3d/include/radeon3d.h").read_text()
E = (ROOT / "backend_r200/include/libraries/minigl_ppc_cp_emit.h").read_text()
for copy in ("classic_68k/dev/include/libraries/minigl_ppc_protocol.h",
             "classic_68k/dev/MiniGL_Library_Source_Code/include/libraries/minigl_ppc_protocol.h"):
    assert (ROOT / copy).read_text() == P, f"protocol header drift: {copy}"


def function(text, signature):
    # Skip prototypes, and ignore braces in diagnostics/comments. Production
    # logging changes must not silently alter which function gets compiled.
    match = re.search(re.escape(signature) + r"[^;{]*\{", text)
    assert match, f"Missing function definition: {signature}"
    depth = 1
    for token in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
                             text[match.end():], re.S):
        if token[0] == "{":
            depth += 1
        elif token[0] == "}":
            depth -= 1
            if not depth:
                return text[match.start():match.end() + token.end()] + "\n"
    raise AssertionError(f"Unterminated function: {signature}")


def section(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


code = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t ULONG;
typedef int32_t LONG, GLint;
typedef ULONG GLenum, GLuint;
typedef unsigned char UBYTE;
typedef void *APTR;
typedef UBYTE *STRPTR;
typedef const UBYTE *CONST_STRPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MGLPPC_PROFILE 0
#define MGLPPC_HOST_TRACE 0
#define MGLPPC_CLIENT_TRACE 0
#define MGLPPC_HOST_TARGET_CACHE 1
#define R200_PPC_FRONTEND 1
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_NO_ERROR 0
#define GL_INVALID_OPERATION 0x59
#define GL_INVALID_VALUE 0x58
#define GL_FRONT 0x404
#define GL_MAX_TEXTURE_SIZE 0xd33
#define MAX_TEXTURES 32
#define CACRF_ClearD 1
#define GVF_GLOBAL_ONLY 0x100
#define MEMF_PUBLIC 1
#define SUBMIT_SLOT_COUNT 2
#define SEGMENT_HALF_BYTES 32768
#define R200PPCTimelineMark(...) ((void)0)
#define HOST_PACING_PRESENT() ((void)0)
#define R200PPCTraceOpen(...) ((void)0)
#define R200PPCTraceClose(...) ((void)0)
#define R200PPCTraceStr(...) ((void)0)
#define R200PPCTraceUL(...) ((void)0)
#define R200PPCTraceUL2(...) ((void)0)
static void R200PPCConsolePrintf(const char *fmt, ...) { (void)fmt; }
struct Message { UBYTE pad[18]; unsigned short mn_Length; };
struct Window { ULONG IDCMPFlags; };
'''
code += section(P, "#define MGLPPC_HOST_PORT_NAME", "/* Exec sees this structure").replace(
    "#ifndef MGLPPC_TEXTURE_VALIDATION_TEST", "")
code += B.replace("#include <exec/types.h>", "")
yield_header = (ROOT / "backend_r200/include/libraries/minigl_ppc_yield.h").read_text()
code += next(line for line in yield_header.splitlines()
             if line.startswith("#define MGLPPC_RING_YIELD_SPINS ")) + "\n"
# Keep service constants and structures current. Only native-pointer ABI size
# assertions differ on this 64-bit host; fixed-width/prefix checks stay active.
for name in ("Radeon3DSegmentV1SizeCheck", "Radeon3DCommitV1SizeCheck",
             "Radeon3DCommitBatchV1SizeCheck", "Radeon3DStateBatchV1SizeCheck",
             "Radeon3DInfoV4SizeCheck", "Radeon3DSurfaceV1SizeCheck"):
    R, count = re.subn(rf"typedef char {name}\[[^;]+;", "", R)
    assert count == 1, f"Service ABI changed: {name}"
code += R.replace("#include <exec/types.h>", "")
code += E.replace("#include <exec/types.h>", "")
code += r'''
typedef struct { ULONG protocolVersion; } MGLPPCTransport;
struct Radeon3DDevice { int unused; };
struct SubmitSlot { ULONG *records; ULONG recordCount, drawCount, segmentCursor;
    BOOL commitMode, stateBatch; ULONG *stateBatchDraws, *vertexOffsets; };
struct HarnessTextureObject { void *shadow; ULONG shadowWidth, shadowHeight; };
static ULONG shadowFrees;
struct MiniGLR200Context {
    struct Window *window;
    ULONG baseIDCMP;
    ULONG width, height, pixelDepth, radeonCaps, batchDwords, deviceEpoch;
    /* Retained direct-texture shadows for the frontend delete loop. */
    struct HarnessTextureObject textures[32];
    BOOL compactTclVertex, insideBegin, streamSegments, syncEnabled;
    struct { APTR Handle; } surfaces[1], depthSurface;
    struct { struct Radeon3DSegment info; BOOL valid; } segment;
    struct Radeon3DSegment segmentPool[4];
    ULONG segmentPoolCount, segmentPoolNext, buildSlot;
    struct SubmitSlot submitSlots[2];
};
'''
code += section(H, "typedef struct MGLPPCHostState", "static const GLenum HostUnpackNames")
code += section(F, "typedef struct R200PPCFrontendPlatform", "#define MGLPPC_REPLAY_BATCHES_PER_FRAME")
# Keep both unconditional counters, but not the opening profile-only guard.
code += section(H, "static ULONG HostExecuteCount;", "#if MGLPPC_PROFILE")
code += r'''
static struct MiniGLR200Context proofContext, *R200PPCContext;
/* Production shadow release for the frontend delete loop: the harness
 * TextureObject only carries the pointer fields the release touches. */
static void FrontendShadowFree(struct HarnessTextureObject *texture) {
    if (texture->shadow) { ++shadowFrees; texture->shadow = NULL; }
}
static MGLPPCHostState host;
static struct Window window;
static struct IntuitionBase *IntuitionBase;
static ULONG FrontendConsoleVerbose;
static APTR Radeon9200Base;
static ULONG PlatformClockRate = 1000000, clockTicks, clockStep = 1;
static ULONG nativeCreates, nativeDeletes, nativeExecutes, subimages, shutdowns, transportCloses;
static ULONG error, allocations, frees, segmentAlloc;
static BOOL ringMode = TRUE, polling;
static UBYTE sharedRing[65536] __attribute__((aligned(64)));
static UBYTE vertexMemory[4][RADEON3D_MAX_SEGMENT_BYTES] __attribute__((aligned(64)));
static ULONG cpMemory[MGLPPC_CP_SEGMENT_BYTES / 4] __attribute__((aligned(64)));
static ULONG slots[8][8192] __attribute__((aligned(32))), buildRecords[2][8192];
/* Model dirty 68k lines: CacheClearE pushes old contents before invalidating.
 * A PPC overwrite before the host releases those lines must lose data. */
static ULONG hostCache[8][16], expectedClearColor;
static BOOL hostDirty[8];
static BOOL verifySemanticHandoff;
static ULONG semanticPath, semanticSlot, semanticWords, semanticPayloadWords;
static ULONG semanticInvalidates, semanticCleans, semanticCompleted, semanticCalls[3], semanticSegment;
static ULONG expectedRecords[2 * RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS];
static BOOL controlPublished;
static ULONG controlInvalidated;
static ULONG yieldCalls, delayCalls, refreshRetired, retireOnYield;
static BOOL holdHostUntilYield;
static BOOL smokeEnabled, driverIndirect = TRUE, hostPolls = TRUE;
static BOOL ibRetires = TRUE, corruptIb, wrongEpoch;
static ULONG ibDispatches, ibFenceTests, smokePayloadFlushes, smokeStreamFlushes;
static BOOL smokeEntryFlushed;
static const ULONG emittedWords[3] = {0x00000579, 0xdeadbeef, 0x81234567};
static ULONG mockEmitWord(ULONG index) {
    return index < 3 ? emittedWords[index] : 0x10203040U ^ index;
}
static BOOL cpOptIn, driverRender = TRUE, driverOrdered = TRUE, cpRetires = TRUE, cpDispatchOK = TRUE;
static BOOL cpZeroFence, observePublication, releaseCpOnYield;
static BOOL retireFenceOnYield, observeCpWait;
static ULONG peerCapsMask = UINT32_MAX, peerVersion = MGLPPC_PROTOCOL_VERSION;
static ULONG directAllocs, directFrees, allocationFailure, serviceGeneration;
static BOOL directLive[5];
static ULONG cpServiceCalls, cpFenceWaits, emitCalls, emitResult = 3, emitToken = 2;
static ULONG expectedCpCount = 4;
static ULONG cpFencePolls, retiredFlushes, nativeQueries, segmentFrees, libraryCloses;
static ULONG headRefreshes, retiredRefreshes, headRefreshClock;
static ULONG publicationStage, publications, surfaceQueries, controlCalls;
static ULONG heldCp[MGLPPC_CP_SEGMENT_BYTES / 4], heldSlots[8][8192], heldEmits;
static ULONG expectedVertexMask, bridgeDevice = 1, bridgeColor = 2;
static struct MGLPPCEmitVertices observedVertices;
static BOOL observedInline;
static void initNative(void);
static BOOL DrainSemanticFence(MGLPPCHostState *state);
static struct { ULONG lib_Version, lib_Revision, lib_OpenCnt; } library, *MiniGLBase = &library;
static BOOL QueryBackendSubimage(BOOL context) { (void)context; return TRUE; }
static void MGLPPCIBTrace(const char *p, const char *s) {
    (void)p; fputs(s, stdout);
    if (*s && s[strlen(s) - 1] != '\n') fputc('\n', stdout);
}
static ULONG RingPoll(MGLPPCHostState *state);
static BOOL PollMailbox(MGLPPCHostState *state);
static void PollQueuedFence(MGLPPCHostState *state);
static void HandleMessage(MGLPPCMessage *message, MGLPPCHostState *state);
static ULONG PerfClock(void) { clockTicks += clockStep; return clockTicks; }
static ULONG PerfClockRate(void) { return 1000000; }
static void CacheClearE(APTR p, ULONG n, int f) {
    (void)n; (void)f;
    if (p == sharedRing && n == sizeof(MGLPPCRingControlV1)) {
        MGLPPCRingControlV1 *c = (void *)sharedRing;
        assert(c->magic == MGLPPC_RING_MAGIC && c->slotCount == 8 && !c->head);
        for (ULONG i = 0; i < 8; ++i) assert(c->slotAddrs[i] == (ULONG)(uintptr_t)slots[i]);
        controlPublished = TRUE;
    }
    if (p == &((MGLPPCRingControlV1 *)sharedRing)->retired) ++retiredFlushes;
    for (ULONG i = 0; i < 8; ++i) {
        if (verifySemanticHandoff && p == slots[i]) {
            assert(i == semanticSlot && n == semanticPayloadWords * sizeof(ULONG) && f == CACRF_ClearD);
            /* Both the incoming invalidate and release clean must precede
             * public completion. The service observes the dirty target stores
             * between them, so an early/late/omitted clean cannot satisfy this. */
            assert(host.ringControl->completed == semanticCompleted);
            if (!semanticInvalidates) {
                assert(!hostDirty[i] && !semanticCleans && semanticCalls[semanticPath] == semanticCompleted);
                ++semanticInvalidates;
            } else {
                assert(semanticInvalidates == 1 && !semanticCleans && hostDirty[i]);
                assert(semanticCalls[semanticPath] == semanticCompleted + 1);
                ++semanticCleans;
            }
        }
        if (p == slots[i] && hostDirty[i]) {
            memcpy(p, hostCache[i], sizeof(hostCache[i]));
            hostDirty[i] = FALSE;
        }
    }
}
static void poll(void) {
    if (hostPolls && !polling && !holdHostUntilYield) {
        polling = TRUE; RingPoll(&host); PollMailbox(&host); PollQueuedFence(&host); polling = FALSE;
    }
}
/* Model host progress at either scheduling boundary, without assigning a
 * duration to a yield. Only Delay contributes to the DOS tick counter. */
static void yieldToHost(void) {
    ++yieldCalls; holdHostUntilYield = FALSE;
    if (releaseCpOnYield) {
        assert(host.ringControl->head == 8 && host.ringControl->completed == 8);
        assert(!host.ringControl->retired && emitCalls == heldEmits);
        assert(!memcmp(heldCp, cpMemory, sizeof(cpMemory)));
        assert(!memcmp(heldSlots, slots, sizeof(slots)));
        cpRetires = TRUE;
        assert(DrainSemanticFence(&host));
        releaseCpOnYield = FALSE;
    }
    if (retireOnYield) {
        host.ringControl->retired = retireOnYield;
        retireOnYield = 0;
    }
    if (retireFenceOnYield) {
        assert(host.ringControl->head == host.ringControl->completed);
        assert(host.ringControl->retired < host.ringControl->completed && host.semanticFence);
        assert(cpFencePolls && !cpRetires);
        cpRetires = TRUE; retireFenceOnYield = FALSE;
    }
    poll();
}
static void Delay(LONG ticks) {
    assert(ticks == 1); ++delayCalls; yieldToHost();
}
static void MGLPPCYield(void) { yieldToHost(); }
static LONG GetVar(CONST_STRPTR name, STRPTR buffer, LONG size, LONG flags) {
    (void)size; (void)flags;
    if (smokeEnabled && !strcmp((const char *)name, "MGLPPC_IBSMOKE")) {
        buffer[0] = '1'; return 1;
    }
    if (cpOptIn && !strcmp((const char *)name, "MGLPPC_CPEMIT")) {
        buffer[0] = '1'; return 1;
    }
    return -1;
}
static void MoveWindow(struct Window *w, LONG dx, LONG dy) {
    assert(w == &window); (void)dx; (void)dy;
}
static void MGLPPCInvalidateLine32(APTR p) {
    uintptr_t address = (uintptr_t)p, base = (uintptr_t)sharedRing;
    if (address >= base && address < base + sizeof(MGLPPCRingControlV1))
        controlInvalidated |= 1UL << ((address - base) / 32);
    if (refreshRetired && p == &((MGLPPCRingControlV1 *)sharedRing)->retired) {
        ((MGLPPCRingControlV1 *)sharedRing)->retired = refreshRetired;
        refreshRetired = 0;
    }
    if (observeCpWait) {
        MGLPPCRingControlV1 *c = (void *)sharedRing;
        if (p == &c->head) {
            if (headRefreshes) assert(clockTicks - headRefreshClock == 256 * clockStep);
            headRefreshClock = clockTicks;
            /* Simulate a host error on the first control cache line. Refreshing
             * retired's different line alone must not reveal this update. */
            if (++headRefreshes == 2) c->error = 1;
        } else if (p == &c->retired) {
            assert(++retiredRefreshes == headRefreshes && !c->retired);
        }
    }
    poll();
}
static void MGLPPCFlushDataRange(APTR p, ULONG n) {
    MGLPPCRingControlV1 *c = (void *)sharedRing;
    if (observePublication) {
        ULONG slot = c->head % MGLPPC_RING_SLOTS;
        if (p == (UBYTE *)proofContext.segment.info.CpuAddress +
                 proofContext.buildSlot * SEGMENT_HALF_BYTES) {
            assert(expectedVertexMask && publicationStage == 0);
            assert(n == proofContext.submitSlots[proofContext.buildSlot].segmentCursor);
            publicationStage = 1;
        } else if (p == (UBYTE *)cpMemory + slot * MGLPPC_CP_SLICE_BYTES) {
            assert(publicationStage == (expectedVertexMask ? 1U : 0U) && n == expectedCpCount * 4);
            for (ULONG i = 0; i < expectedCpCount; ++i) {
                ULONG word = i < emitResult ? mockEmitWord(i) : 0x80000000U;
                ULONG stored = ((ULONG *)p)[i];
                /* Padding must be in host-order staging before the CP flush,
                 * not appended to already-converted words afterwards. */
                assert(slots[slot][i] == word && stored == __builtin_bswap32(word));
                /* A big-endian PPC stores these four lanes; CP reads LE. */
                for (ULONG byte = 0; byte < 4; ++byte)
                    assert(((stored >> (24 - byte * 8)) & 255) ==
                           ((word >> (byte * 8)) & 255));
            }
            publicationStage = 2;
        } else if (p == slots[slot]) {
            assert(publicationStage == 2 && n == 12);
            assert(slots[slot][0] == slot * MGLPPC_CP_SLICE_BYTES);
            assert(slots[slot][1] == expectedCpCount && slots[slot][2] == expectedVertexMask);
            publicationStage = 3;
        } else if (p == &c->entries[slot]) {
            assert(publicationStage == 3 && n == sizeof(MGLPPCRingEntryV1));
            assert(c->entries[slot].slot == slot && c->entries[slot].dwords == 3);
            assert(c->entries[slot].flags == MGLPPC_SEMANTIC_FLAG_CP_EMIT);
            assert(c->entries[slot].generation == c->head + 1 && !c->entries[slot].crc);
            publicationStage = 4;
        } else if (p == &c->syncEnabled) {
            assert(publicationStage == 4 && n == 8);
            assert(c->syncEnabled == FrontendPlatform.syncEnabled);
            assert(c->syncSeq == FrontendPlatform.syncSeq);
            publicationStage = 5;
        } else if (p == &c->head) {
            assert(publicationStage == 5 && n == 4 && c->head == publications + 1);
            publicationStage = 0; ++publications;
        }
    }
    if (!smokeEnabled) return;
    if (p == sharedRing + MGLPPC_IB_OFFSET) {
        assert(n == 32); ++smokeStreamFlushes;
        if (corruptIb) ((ULONG *)p)[0] = 0;
    }
    if (p == slots[0] && n == 8) {
        assert(slots[0][0] == MGLPPC_IB_OFFSET && slots[0][1] == 8);
        ++smokePayloadFlushes;
    }
    if (p == &c->entries[0]) {
        assert(smokePayloadFlushes && smokeStreamFlushes);
        assert(c->head == 0 && c->entries[0].slot == 0 && c->entries[0].dwords == 2);
        smokeEntryFlushed = TRUE;
    }
    if (p == &c->head) assert(smokeEntryFlushed && c->head == 1);
}
static APTR AllocVec(ULONG bytes, ULONG flags) {
    assert(flags == MEMF_PUBLIC && bytes == sizeof(slots[0]) + 31);
    return slots[allocations++ % 8];
}
static void FreeVec(APTR p) { assert(p); ++frees; }
static APTR OpenLibrary(CONST_STRPTR name, ULONG v) { (void)name; (void)v; return &host; }
static void CloseLibrary(APTR p) { assert(p == &host); ++libraryCloses; }
static void mglChooseWindowMode(int n) { (void)n; }
static void mglChoosePixelDepth(int n) { (void)n; }
static void mglChooseVertexBufferSize(int n) { (void)n; }
static void mglChooseTextureBufferSize(int n) { (void)n; }
static void mglChooseMtexBufferSize(int n) { (void)n; }
static void mglChooseNumberOfBuffers(int n) { (void)n; }
static APTR mglCreateContext(int x, int y, int w, int h) {
    assert(!host.context && !x && !y && w == 320 && h == 240);
    ++nativeCreates; segmentAlloc = 0; initNative(); return &host;
}
static void mglDeleteContext(void) { assert(host.context); ++nativeDeletes; }
static APTR mglGetWindowHandle(void) { return &window; }
static BOOL mglLockDisplay(void) { return TRUE; }
static void mglUnlockDisplay(void) {}
static void mglSwitchDisplay(void) {}
static void mglEnableSync(int enabled) { (void)enabled; }
typedef struct { ULONG depth, width, height, pitch; APTR base_address; } MGLLockInfo;
static BOOL mglLockBack(MGLLockInfo *lock) { (void)lock; assert(0); return FALSE; }
static ULONG nativeError;
static ULONG glGetError(void) { ULONG e = nativeError; nativeError = 0; return e; }
static BOOL QueryBackendBridge(MGLR200PrivateBridgeV1 *b) {
    memset(b, 0, sizeof(*b)); b->deviceToken = 1; b->colorSurfaceToken = 2;
    b->depthSurfaceToken = 3; b->width = 320; b->height = 240;
    b->batchDwords = 8192; return TRUE;
}
static BOOL QueryBackendBridgeV2(MGLR200PrivateBridgeV2 *b) {
    MGLR200PrivateBridgeV1 v1;
    QueryBackendBridge(&v1); memset(b, 0, sizeof(*b));
    b->deviceToken = bridgeDevice; b->colorSurfaceToken = bridgeColor;
    b->deviceEpoch = 1 + wrongEpoch; return TRUE;
}
static BOOL Radeon3DGetInfo(struct Radeon3DDevice *d, struct Radeon3DInfo *i) {
    (void)d; i->Generation = serviceGeneration ? serviceGeneration : nativeCreates;
    i->Caps = RADEON3D_CAP_CP_READY | (driverOrdered ? RADEON3D_CAP_ORDERED_COMMITS : 0) |
        (driverIndirect ? RADEON3D_CAP_INDIRECT_DISPATCH : 0) |
        (driverRender ? RADEON3D_CAP_INDIRECT_RENDER : 0);
    return TRUE;
}
static BOOL Radeon3DWaitFence(struct Radeon3DDevice *d, ULONG f, ULONG timeout) {
    (void)d; (void)timeout; assert(f);
    if (f == 0x1234) { ++ibFenceTests; return ibRetires; }
    if (f >= 0x10000) { ++cpFenceWaits; return cpRetires; }
    return TRUE;
}
static BOOL Radeon3DTestFence(struct Radeon3DDevice *d, ULONG fence) {
    assert(d == (void *)(uintptr_t)host.ringDeviceToken && fence == host.semanticFence && fence);
    assert(host.cpEnabled); ++cpFencePolls; return cpRetires;
}
static void ObserveSemanticRecords(const ULONG *records, ULONG count, ULONG path) {
    uintptr_t address = (uintptr_t)records;
    /* Record real target stores into shared storage as dirty 68k cache lines.
     * In the regression variant, the next incoming invalidate writes these
     * lines over the newly published payload before the service sees it. */
    for (ULONG i = 0; i < 8; ++i) {
        if (address >= (uintptr_t)slots[i] && address < (uintptr_t)(slots[i] + 8192)) {
            memcpy(hostCache[i], slots[i], sizeof(hostCache[i]));
            hostDirty[i] = TRUE;
        }
    }
    if (!verifySemanticHandoff) return;
    assert(path == semanticPath && count == semanticWords && semanticInvalidates == 1 && !semanticCleans);
    assert(records == slots[semanticSlot] + (path ? MGLPPC_RING_COMMIT_HEADER_DWORDS : 0));
    assert(hostDirty[semanticSlot]);
    assert(records[2] == bridgeColor);
    assert(!memcmp(records, expectedRecords, count * sizeof(ULONG)));
    assert(!memcmp(slots, heldSlots, sizeof(slots)));
    assert(host.ringControl->completed == semanticCompleted && host.ringCompleted == semanticCompleted);
    ++semanticCalls[path];
}
static BOOL Radeon3DExecute(struct Radeon3DDevice *d, ULONG *p, ULONG n, ULONG flags, ULONG *fence) {
    assert(d == (void *)1 && flags == RADEON3D_SUBMIT_FENCE && n == 11);
    ObserveSemanticRecords(p, n, 0);
    if (!verifySemanticHandoff) assert(p[5] == expectedClearColor);
    *fence = ++nativeExecutes; return TRUE;
}
static BOOL Radeon3DCommitBatch(struct Radeon3DDevice *d,
                                 const struct Radeon3DCommitBatch *batch, ULONG *fence) {
    assert(verifySemanticHandoff && d == (void *)1);
    assert(batch->Size == sizeof(*batch) && batch->Version == RADEON3D_COMMIT_BATCH_VERSION);
    assert(batch->SegmentId == semanticSegment && batch->RecordCount == 2);
    assert(batch->Flags == RADEON3D_SUBMIT_FENCE);
    assert(batch->VertexOffsets == slots[semanticSlot] + 2 + semanticWords);
    assert(!memcmp(batch->VertexOffsets, heldSlots[semanticSlot] + 2 + semanticWords, 2 * sizeof(ULONG)));
    ObserveSemanticRecords(batch->Records, batch->RecordDwords, 1);
    *fence = ++nativeExecutes; return TRUE;
}
static BOOL Radeon3DCommitStateBatch(struct Radeon3DDevice *d,
                                      const struct Radeon3DStateBatch *batch, ULONG *fence) {
    assert(verifySemanticHandoff && d == (void *)1);
    assert(batch->Size == sizeof(*batch) && batch->Version == RADEON3D_STATE_BATCH_VERSION);
    assert(batch->Generation == host.ringDeviceGeneration && batch->SegmentId == semanticSegment);
    assert(batch->Primitive == expectedRecords[0] && batch->DrawCount == 2);
    assert(batch->Flags == RADEON3D_SUBMIT_FENCE);
    assert((const ULONG *)batch->Draws == slots[semanticSlot] + 2 + semanticWords);
    assert(!memcmp(batch->Draws, heldSlots[semanticSlot] + 2 + semanticWords, 4 * sizeof(ULONG)));
    ObserveSemanticRecords(batch->Header, batch->HeaderDwords, 2);
    *fence = ++nativeExecutes; return TRUE;
}
'''
code += function(H, "static void PatchRecordTargets(")
code += function(H, "static void PublishCpColorTarget(")
code += r'''
struct NativeContext {
    struct Radeon3DDevice *device;
    ULONG radeonCaps, segmentLeaseCount, deviceEpoch;
    struct { ULONG id; } segmentLeases[12];
    struct Radeon3DSurface surfaces[3], depthSurface;
    ULONG displayedBuffer, drawingBuffer, drawBuffer;
    BOOL fullscreen;
    struct TextureObject {
        BOOL generated, resident;
        ULONG residentDeviceEpoch;
        struct Radeon3DSurface surface;
    } textures[MAX_TEXTURES];
};
static struct NativeContext native;
static struct NativeContext *NativeCurrentContext(void) { return &native; }
static void initNative(void) {
    memset(&native, 0, sizeof(native));
    native.device = (void *)1; native.deviceEpoch = 1;
    native.radeonCaps = driverIndirect ? RADEON3D_CAP_INDIRECT_DISPATCH : 0;
    struct Radeon3DSurface s = {0};
    s.Size = RADEON3D_SURFACE_V1_SIZE; s.Version = RADEON3D_SURFACE_VERSION;
    s.Generation = nativeCreates; s.CpuAddress = vertexMemory[0]; s.GpuAddress = 0x81200000;
    s.Pitch = 1280; s.Width = 320; s.Height = 240;
    s.Format = RADEON3D_FORMAT_B8G8R8A8; s.Handle = (void *)2;
    native.surfaces[0] = s;
    s.Handle = (void *)3; s.GpuAddress += 0x100000; native.depthSurface = s;
    for (ULONG i = 0; i < MAX_TEXTURES; ++i) {
        native.textures[i].generated = native.textures[i].resident = TRUE;
        native.textures[i].residentDeviceEpoch = native.deviceEpoch;
        s.Handle = (void *)(uintptr_t)(16 + i); s.GpuAddress += 0x100000;
        native.textures[i].surface = s;
    }
}
static BOOL Radeon3DAllocSegment(struct Radeon3DDevice *d, ULONG bytes,
                                  struct Radeon3DSegment *s) {
    assert(d == (void *)1 && s->Size == sizeof(*s));
    ULONG index = directAllocs++;
    assert(index < 5);
    if (allocationFailure == index + 1) return FALSE;
    assert(bytes == (index ? RADEON3D_MAX_SEGMENT_BYTES : MGLPPC_CP_SEGMENT_BYTES));
    s->Version = RADEON3D_SEGMENT_VERSION; s->Id = 32 + index;
    s->Bytes = bytes; s->CpuAddress = index ? (void *)vertexMemory[index - 1] : cpMemory;
    s->GpuAddress = 0x93400000 + index * 0x100000;
    directLive[index] = TRUE;
    return TRUE;
}
static BOOL Radeon3DFreeSegment(struct Radeon3DDevice *d, ULONG id) {
    assert(d == (void *)1 && id >= 32 && id < 37 && directLive[id - 32]);
    assert(!host.semanticFence); /* No release of GPU-owned storage. */
    directLive[id - 32] = FALSE; ++directFrees; return TRUE;
}
static void SetNativeError(struct NativeContext *c, ULONG e) { (void)c; nativeError = e; }
static BOOL Radeon3DDispatchIndirect(struct Radeon3DDevice *d,
                                     const struct Radeon3DIndirect *ib, ULONG *f) {
    (void)d; assert(ib->Version == 1 && ib->Flags == 0);
    if (host.cpEnabled) {
        assert(ib->Size == sizeof(*ib) && ib->SegmentId == host.cpSegment.Id);
        assert(ib->ByteOffset == host.ringCompleted % 8 * MGLPPC_CP_SLICE_BYTES);
        assert(ib->DwordCount == expectedCpCount && !(ib->DwordCount & 1) && !host.semanticFence);
        for (ULONG i = 0; i < ib->DwordCount; ++i) {
            ULONG word = i < emitResult ? mockEmitWord(i) : 0x80000000U;
            assert(cpMemory[ib->ByteOffset / 4 + i] == __builtin_bswap32(word));
        }
        ++cpServiceCalls;
        *f = cpZeroFence ? 0 : 0x10000 + cpServiceCalls;
        return cpDispatchOK;
    }
    assert(ib->SegmentId == 0 && ib->ByteOffset == MGLPPC_IB_OFFSET && ib->DwordCount == 8);
    assert(((MGLPPCRingControlV1 *)sharedRing)->completed == 0);
    ++ibDispatches; *f = 0x1234; return TRUE;
}
#define ZeroMemory(p,n) memset(p,0,n)
#define SetError SetNativeError
#define MiniGLR200Context NativeContext
#define CurrentContext NativeCurrentContext
#define Lib_glGetIntegerv nativeQuery
'''
code += function(CORE, "struct Radeon3DSurface *TargetSurface(")
code += section(N, "void Lib_glGetIntegerv(", "    switch (name) {")
code += "    switch (name) {\n"
code += section(N, "    case MGL_R200_PRIVATE_SURFACE_V1_QUERY:", "    case GL_SHADE_MODEL:")
code += r'''
    default: assert(0);
    }
}
#undef SetError
#undef MiniGLR200Context
#undef CurrentContext
#undef Lib_glGetIntegerv
static void glGetIntegerv(ULONG query, GLint *p) {
    ++nativeQueries;
    if (query == MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY ||
        query == MGL_R200_PRIVATE_SURFACE_V1_QUERY ||
        query == MGL_R200_PRIVATE_TEXTURE_CAPS_V1_QUERY) {
        if (query == MGL_R200_PRIVATE_SURFACE_V1_QUERY) ++surfaceQueries;
        nativeQuery(query, p); return;
    }
    if (query == GL_MAX_TEXTURE_SIZE) { *p = 2048; return; }
    if (query == MGL_R200_PRIVATE_SEGMENT_FREE_V1_QUERY) { ++segmentFrees; return; }
    assert(query == MGL_R200_PRIVATE_SEGMENT_V1_QUERY);
    MGLR200PrivateSegmentV1 *s = (void *)p;
    memset(s, 0, sizeof(*s));
    assert(segmentAlloc < 5);
    s->version = 1; s->bytes = segmentAlloc ? sizeof(vertexMemory[0]) : sizeof(sharedRing);
    s->deviceEpoch = native.deviceEpoch;
    s->cpuAddress = (ULONG)(uintptr_t)(segmentAlloc ? vertexMemory[segmentAlloc - 1] : sharedRing);
    s->segmentId = ++segmentAlloc;
}
static void DiscardProbe(MGLPPCHostState *s) { (void)s; }
static void DiscardUpload(MGLPPCHostState *s) { (void)s; }
ULONG MGLPPCEmitBatch(ULONG *words, const ULONG *records, ULONG count,
                     const struct MGLPPCEmitVertices *vertices,
                     MGLPPCEmitResolve resolve, void *user) {
    struct MGLPPCEmitSurface surface;
    assert(user == &proofContext && records == buildRecords[proofContext.buildSlot] && count == 11);
    assert(host.ringControl->head - host.ringControl->retired < MGLPPC_RING_SLOTS);
    ++emitCalls;
    observedInline = vertices == NULL;
    if (vertices) observedVertices = *vertices;
    if (!resolve(user, emitToken, &surface)) return 0;
    assert(surface.gpuAddress != surface.cpuAddress && surface.pitch == 1280);
    memcpy(words, emittedWords, sizeof(emittedWords));
    for (ULONG i = 3; i < emitResult && i < MGLPPC_CP_DWORDS; ++i)
        words[i] = mockEmitWord(i);
    return emitResult;
}
'''
code += section(H, "#if MGLPPC_PROFILE\n#define HOST_START", "static BOOL DiscardSemantic")
code += section(H, "static ULONG SemanticCRCTable[256];", "static BOOL QueryBackendBridge(")
for name in ("static BOOL DrainSemanticFence(", "static BOOL DrainForSegment(", "static void PollQueuedFence(",
             "static BOOL DiscardSemantic(",
             "static LONG ReadUploadTraceOption(",
             "static BOOL QueryBackendIndirect(", "static BOOL QueryCpSurface(",
             "static BOOL ExecuteIndirectSmoke(", "static BOOL ExecuteCpEntry(",
             "static BOOL RegisterSemanticSlots(", "static BOOL RegisterSemanticRing(",
             "static BOOL ExecuteSemanticSlot("):
    body = function(H, name)
    if "--regress-register" in sys.argv and name == "static BOOL RegisterSemanticRing(":
        publication = "    CacheClearE((APTR)control, sizeof(*control), CACRF_ClearD);\n"
        assert body.count(publication) == 1
        body = body.replace(publication, "")
    code += body
ring = function(H, "static ULONG RingPoll(")
if "--regress-cache" in sys.argv:
    ring, releases = re.subn(
        r"(?m)^[ \t]*if\s*\(entry\.dwords\)\s*\n[ \t]*"
        r"CacheClearE\(slotAddr,\s*entry\.dwords\s*\*\s*sizeof\(ULONG\),\s*CACRF_ClearD\);[ \t]*\n",
        "", ring)
    assert releases == 1, "Expected exactly one post-service shared-payload release clean"
if "--regress-ib-ack" in sys.argv:
    ring = ring.replace("control->completed = state->ringCompleted;",
                        "if (!(entry.flags & MGLPPC_SEMANTIC_FLAG_INDIRECT)) control->completed = state->ringCompleted;")
code += ring
code += section(function(H, "static void HandleMessage("), "static void HandleMessage(",
                "    case MGLPPC_CMD_HELLO:")
for begin, end in (("    case MGLPPC_CMD_HELLO:", "    case MGLPPC_CMD_CLEAR_PRESENT:"),
                   ("    case MGLPPC_CMD_DESTROY_CONTEXT:", "    case MGLPPC_CMD_EXECUTE_BATCH:"),
                   ("    case MGLPPC_CMD_GET_INTEGER:", "    case MGLPPC_CMD_VRAM_PROBE_BEGIN:"),
                   ("    case MGLPPC_CMD_REGISTER_SEMANTIC_SLOTS:", "    case MGLPPC_CMD_BIND_TEXTURE:")):
    code += section(H, begin, end)
code += r'''
    default:
        /* Native texture commands are not the fault: assert their routing and
         * sequencing, while leaving pixel correctness to the public test. */
        assert(message->contextId == state->contextId && state->context);
        assert(!state->ringControl || state->ringControl->completed == state->ringControl->head);
        if (message->command == MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED) ++subimages;
        break;
    }
}
'''
code += function(H, "static BOOL PollMailbox(")
code += section(F, "static ULONG PlatformCRCTable[256];", "/* A late completion")
code += r'''
static BOOL MGLPPCTransportOpen(MGLPPCTransport *t) { t->protocolVersion = peerVersion; return TRUE; }
static void MGLPPCTransportClose(MGLPPCTransport *t) { (void)t; ++transportCloses; }
static BOOL MGLPPCTransportCall(MGLPPCTransport *t, MGLPPCMessage *m) {
    BOOL registration = m->command == MGLPPC_CMD_REGISTER_SEMANTIC_RING;
    poll(); m->protocolVersion = t->protocolVersion;
    m->magic = MGLPPC_PROTOCOL_MAGIC;
    m->execMessage.mn_Length = MGLPPC_MESSAGE_SIZE(m->payloadWords);
    if (registration) { controlPublished = FALSE; controlInvalidated = 0; }
    HandleMessage(m, &host);
    ++controlCalls;
    if (m->command == MGLPPC_CMD_SHUTDOWN && m->status == MGLPPC_STATUS_OK) ++shutdowns;
    if (m->command == MGLPPC_CMD_HELLO) {
        m->value[3] &= peerCapsMask & ~MGLPPC_CAP_HOST_EVENTS;
        if (!ringMode) m->value[3] &= ~(MGLPPC_CAP_SEMANTIC_RING | MGLPPC_CAP_MAILBOX_V1);
    }
    if (registration && m->status == MGLPPC_STATUS_OK) assert(controlPublished);
    return TRUE;
}
static BOOL MGLPPCTransportSend(MGLPPCTransport *t, MGLPPCMessage *m) { return MGLPPCTransportCall(t, m); }
static BOOL MGLPPCTransportWait(MGLPPCTransport *t, MGLPPCMessage *m) { (void)t; (void)m; return TRUE; }
static void InitializeFrontendContext(struct MiniGLR200Context *c) {
    c->deviceEpoch = 1; /* Native epochs restart per context, unlike registration epochs. */
    for (ULONG i = 0; i < 2; ++i) c->submitSlots[i].records = buildRecords[i];
}
static void SetError(struct MiniGLR200Context *c, ULONG e) { assert(c); error = e; }
static struct MiniGLR200Context *CurrentContext(void) { return R200PPCContext; }
static BOOL FlushScreenVertices(struct MiniGLR200Context *c) { (void)c; return TRUE; }
static BOOL SubmitAndWait(struct MiniGLR200Context *c);
'''
for signature in ("static BOOL PlatformMailboxReady(", "static BOOL PlatformMailboxWait(",
                  "static BOOL PlatformCallFail(", "static BOOL PlatformCall(",
                  "static void CopyRecordToRingSlot(", "void R200PPCPlatformResetClientDefaults(",
                    "BOOL R200PPCPlatformOpen(",                     "static BOOL R200PPCIbSmokeAfterRegistration(",
                   "static BOOL ResolveCpSurface(", "static void RefreshCpColorTarget(",
                   "static BOOL PublishCpSlot(",
                   "BOOL R200PPCPlatformPublishSlot(",
                  "BOOL R200PPCPlatformWaitSlot(", "BOOL R200PPCPlatformBarrier(",
                  "BOOL R200PPCPlatformRotateSegment(",
                  "void *R200PPCDispatchCreateContext(", "void R200PPCDispatchDeleteContext(",
                  "void R200PPCPlatformClose(", "static BOOL FrontendTextureBarrier("):
    body = function(F, signature)
    if "--regress-crc" in sys.argv and signature == "void *R200PPCDispatchCreateContext(":
        body = body.replace("FrontendPlatform.crcArmed = TRUE;", "")
    if "--regress-waits" in sys.argv:
        body = body.replace("if (!control->error &&", "if (0 && !control->error &&")
        # The old loop also yielded on its first iteration; the current spin
        # budget otherwise hides this mutation by rechecking the while condition.
        body = body.replace("if (spin && !(spin & (MGLPPC_RING_YIELD_SPINS - 1UL)))",
                            "if (!(spin & (MGLPPC_RING_YIELD_SPINS - 1UL)))")
    code += body
code += function(S, "void Lib_glFinish(")
code += r'''
static void testIndirectSmoke(void) {
    for (ULONG scenario = 0; scenario < 8; ++scenario) {
        MGLPPCRingControlV1 *c = (void *)sharedRing;
        ULONG deletesBefore = nativeDeletes;
        memset(&host, 0, sizeof(host)); memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
        memset(sharedRing, 0, sizeof(sharedRing)); memset(slots, 0xa5, sizeof(slots));
        memset(&native, 0, sizeof(native)); memset(hostDirty, 0, sizeof(hostDirty));
        native.device = (void *)1; native.radeonCaps = RADEON3D_CAP_INDIRECT_DISPATCH;
        native.segmentLeaseCount = 1; native.segmentLeases[0].id = 0;
        assert(QueryBackendIndirect()); /* Explicit probe must not dispatch segment zero. */
        {
            MGLR200PrivateIndirectDispatchV1 query = {0};
            query.flags = 2; query.dwordCount = 8;
            nativeQuery(MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY, (GLint *)&query);
            assert(glGetError() == GL_INVALID_OPERATION && !query.fenceOut);
            memset(&query, 0, sizeof(query));
            query.segmentId = 1; query.dwordCount = 8;
            nativeQuery(MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY, (GLint *)&query);
            assert(glGetError() == GL_INVALID_OPERATION && !query.fenceOut);
        }
        c->magic = MGLPPC_RING_MAGIC; c->version = MGLPPC_RING_CONTROL_VERSION;
        c->slotCount = 8; c->slotDwords = 8192; c->epoch = 7;
        for (ULONG i = 0; i < 8; ++i) {
            c->slotAddrs[i] = (ULONG)(uintptr_t)slots[i];
            c->entries[i].slot = 0xffffffff; /* Never choose a buffer from stale entry contents. */
            FrontendPlatform.ringSlots[i] = slots[i];
        }
        host.context = &host; host.ringControl = c; host.indirectProbeOk = TRUE;
        host.ringSegmentLeased = TRUE; host.ringSegmentId = 0;
        host.ringDeviceEpoch = 1; host.ibDryRun = scenario == 0;
        FrontendPlatform.ring = c; FrontendPlatform.epoch = 7;
        FrontendPlatform.ibSmokeRequested = TRUE;
        FrontendPlatform.ibSmokeConfirmed = scenario != 2;
        FrontendPlatform.ibSmokeDry = host.ibDryRun;
        FrontendPlatform.ibSegmentBytes = sizeof(sharedRing);
        R200PPCContext = &proofContext;
        smokeEnabled = TRUE; holdHostUntilYield = TRUE; hostPolls = scenario != 3;
        ibRetires = scenario != 4; corruptIb = scenario == 5; wrongEpoch = scenario == 6;
        if (scenario == 7) c->slotAddrs[0] = 0;
        ibDispatches = ibFenceTests = smokePayloadFlushes = smokeStreamFlushes = 0;
        yieldCalls = delayCalls = 0;
        smokeEntryFlushed = FALSE;
        BOOL ok = R200PPCIbSmokeAfterRegistration();
        assert(yieldCalls == delayCalls); /* Startup smoke still uses DOS Delay(1). */
        if (scenario < 2) {
            assert(ok && !FrontendPlatform.ibSmokeFailed);
            assert(c->head == 1 && c->completed == 1 && c->retired == 1 && !c->error);
            assert(smokePayloadFlushes == 1 && smokeStreamFlushes == 1 && delayCalls == 1);
            assert(ibDispatches == (scenario == 1) && ibFenceTests == (scenario == 1));
            assert(!host.semanticExecutes); /* Preserve first normal-inline CRC verification. */
        } else {
            assert(!ok && FrontendPlatform.ibSmokeFailed);
            assert(!c->completed && !c->retired);
            assert(ibDispatches == (scenario == 4));
            assert(delayCalls <= 250);
            R200PPCDispatchDeleteContext();
            assert(nativeDeletes == deletesBefore && R200PPCContext);
            if (scenario == 2 || scenario == 7) assert(!c->head && !smokeStreamFlushes);
            if (scenario == 4) assert(host.semanticFence == 0x1234);
        }
    }
    smokeEnabled = FALSE; hostPolls = ibRetires = TRUE; wrongEpoch = corruptIb = FALSE;
    R200PPCContext = NULL;
    puts("Indirect smoke: dry/gpu acknowledgment, segment zero, no stale-slot ownership, fail-closed deadlines passed");
}

static BOOL SubmitAndWait(struct MiniGLR200Context *c) {
    if (c->submitSlots[0].recordCount) {
        if (!R200PPCPlatformPublishSlot(c, 0)) return FALSE;
        c->submitSlots[0].recordCount = 0;
    }
    return R200PPCPlatformWaitSlot(c, 0);
}
static void clearPrefix(void) {
    /* Same inline record shape as glClear before a streamed state-batch draw. */
    static const ULONG clear[11] = {1,11,2,0,1,0xff102030,0,0,0,320,240};
    memcpy(buildRecords[0], clear, sizeof(clear));
    buildRecords[0][5] = ++expectedClearColor;
    proofContext.submitSlots[0].recordCount = 11;
}
static void texture(ULONG cmd) {
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = 1; /* same texture name in both contexts */
    assert(PlatformCall(cmd, FrontendPlatform.contextId));
}
static void testWaitRefresh(void) {
    ULONG before, needed, delaysBefore = delayCalls;
    clearPrefix();
    /* No host polling occurs on publication until the ring appears full.
     * The cache refresh services the host: readiness must skip any yield. */
    for (ULONG i = 0; i < 8; ++i) assert(R200PPCPlatformPublishSlot(&proofContext, 0));
    before = yieldCalls;
    assert(R200PPCPlatformPublishSlot(&proofContext, 0));
    assert(yieldCalls == before);
    poll();
    for (ULONG i = 0; i < 8; ++i) assert(R200PPCPlatformPublishSlot(&proofContext, 0));
    before = yieldCalls;
    assert(R200PPCPlatformBarrier(&proofContext, TRUE));
    assert(yieldCalls == before);
    poll();
    assert(R200PPCPlatformPublishSlot(&proofContext, 0));
    before = yieldCalls;
    assert(R200PPCPlatformBarrier(&proofContext, FALSE));
    assert(yieldCalls == before);

    /* If fresh state really is still busy, a yield must remain available. */
    for (ULONG i = 0; i < 8; ++i) assert(R200PPCPlatformPublishSlot(&proofContext, 0));
    holdHostUntilYield = TRUE;
    before = yieldCalls;
    assert(R200PPCPlatformPublishSlot(&proofContext, 0));
    assert(yieldCalls == before + 1 && !holdHostUntilYield);
    poll();
    for (ULONG i = 0; i < 8; ++i) assert(R200PPCPlatformPublishSlot(&proofContext, 0));
    holdHostUntilYield = TRUE;
    before = yieldCalls;
    assert(R200PPCPlatformBarrier(&proofContext, TRUE));
    assert(yieldCalls == before + 1 && !holdHostUntilYield);
    poll();
    assert(R200PPCPlatformPublishSlot(&proofContext, 0));
    holdHostUntilYield = TRUE;
    before = yieldCalls;
    assert(R200PPCPlatformBarrier(&proofContext, FALSE));
    assert(yieldCalls == before + 1 && !holdHostUntilYield);
    poll();

    needed = host.ringCompleted;
    host.ringControl->retired = 0;
    for (ULONG i = 0; i < 4; ++i) FrontendPlatform.segmentLastEntry[i] = needed;
    for (ULONG i = 0; i < 2; ++i) proofContext.submitSlots[i].segmentCursor = 64;
    refreshRetired = needed;
    before = yieldCalls;
    assert(R200PPCPlatformRotateSegment(&proofContext, 32));
    assert(yieldCalls == before && !refreshRetired);
    assert(proofContext.segmentPoolNext == 1);
    for (ULONG i = 0; i < 2; ++i) assert(!proofContext.submitSlots[i].segmentCursor);
    for (ULONG i = 0; i < 4; ++i) FrontendPlatform.segmentLastEntry[i] = needed + 1;
    retireOnYield = needed + 1;
    before = yieldCalls;
    assert(R200PPCPlatformRotateSegment(&proofContext, 32));
    assert(yieldCalls == before + 1 && !retireOnYield);
    assert(proofContext.segmentPoolNext == 2);
    assert(delayCalls == delaysBefore); /* All four busy paths use short yields. */
    proofContext.submitSlots[0].recordCount = 0;
}

static void resetCp(void) {
    /* Each fault case is an independent session. Never use fixture reset as
     * evidence that production teardown released an unretired allocation. */
    memset(&host, 0, sizeof(host)); memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
    memset(&proofContext, 0, sizeof(proofContext)); memset(&native, 0, sizeof(native));
    memset(sharedRing, 0, sizeof(sharedRing)); memset(slots, 0xa5, sizeof(slots));
    memset(cpMemory, 0xa5, sizeof(cpMemory)); memset(hostDirty, 0, sizeof(hostDirty));
    memset(directLive, 0, sizeof(directLive));
    verifySemanticHandoff = FALSE; semanticInvalidates = semanticCleans = semanticCompleted = 0;
    memset(semanticCalls, 0, sizeof(semanticCalls));
    host.backendOpen = host.running = 1;
    R200PPCContext = NULL; Radeon9200Base = IntuitionBase = NULL;
    cpOptIn = driverIndirect = driverRender = driverOrdered = cpRetires = cpDispatchOK = TRUE;
    ringMode = hostPolls = ibRetires = TRUE;
    cpZeroFence = smokeEnabled = wrongEpoch = corruptIb = holdHostUntilYield = FALSE;
    observePublication = releaseCpOnYield = retireFenceOnYield = observeCpWait = FALSE;
    peerCapsMask = UINT32_MAX; peerVersion = MGLPPC_PROTOCOL_VERSION;
    directAllocs = directFrees = allocationFailure = serviceGeneration = 0;
    cpServiceCalls = cpFenceWaits = emitCalls = publications = publicationStage = 0;
    cpFencePolls = retiredFlushes = nativeQueries = segmentFrees = libraryCloses = 0;
    headRefreshes = retiredRefreshes = headRefreshClock = 0;
    nativeCreates = nativeDeletes = nativeExecutes = allocations = frees = nativeError = error = 0;
    HostExecuteCount = HostPresentCount = surfaceQueries = controlCalls = 0;
    yieldCalls = delayCalls = refreshRetired = retireOnYield = expectedVertexMask = 0;
    clockStep = 1;
    ibDispatches = 0; emitResult = 3; emitToken = bridgeColor = 2; bridgeDevice = 1;
    expectedCpCount = 4;
}

static APTR openCpContext(BOOL windowed) {
    assert(R200PPCPlatformOpen());
    FrontendPlatform.windowMode = windowed;
    return R200PPCDispatchCreateContext(0, 0, 320, 240);
}

static void startCp(void) {
    resetCp(); assert(openCpContext(TRUE));
    assert(FrontendPlatform.cpRequested && FrontendPlatform.cpEnabled && host.cpEnabled);
    assert(!FrontendPlatform.ibSmokeRequested && !FrontendPlatform.ibSmokeConfirmed);
    assert(!host.indirectProbeOk && !ibDispatches && !host.cpDispatches);
}

static void finishCp(void) {
    observePublication = FALSE; hostPolls = TRUE;
    proofContext.submitSlots[0].recordCount = proofContext.submitSlots[1].recordCount = 0;
    R200PPCPlatformClose();
    assert(!R200PPCContext && !host.context && !FrontendPlatform.opened);
    assert(allocations == frees && !Radeon9200Base);
    for (ULONG i = 0; i < 5; ++i) assert(!directLive[i]);
}

static void primeCp(void) {
    struct MGLPPCEmitSurface surface;
    assert(ResolveCpSurface(&proofContext, 2, &surface));
    hostPolls = FALSE; /* Deterministic explicit consumer steps below. */
}

static void submitCp(ULONG kind, ULONG segment, ULONG build) {
    struct SubmitSlot *slot = &proofContext.submitSlots[build];
    static ULONG offsets[] = {64, 192};
    static ULONG draws[] = {64, 3, 192, 6};
    ULONG records[11];
    clearPrefix();
    if (build) {
        memcpy(buildRecords[build], buildRecords[0], sizeof(records));
        proofContext.submitSlots[0].recordCount = 0;
    }
    slot->recordCount = 11; slot->drawCount = kind ? 2 : 0;
    slot->commitMode = kind != 0; slot->stateBatch = kind == 2;
    slot->vertexOffsets = offsets; slot->stateBatchDraws = draws;
    slot->segmentCursor = 256; proofContext.buildSlot = build;
    proofContext.segment.info = proofContext.segmentPool[segment];
    expectedVertexMask = kind ? 1UL << segment : 0;
    memcpy(records, slot->records, sizeof(records));
    assert(R200PPCPlatformPublishSlot(&proofContext, build));
    assert(!memcmp(records, slot->records, sizeof(records)));
    assert(observedInline == !kind);
    if (kind) {
        assert(observedVertices.gpuAddress == 0x93500000 + segment * 0x100000);
        assert(observedVertices.gpuAddress != (ULONG)(uintptr_t)proofContext.segment.info.CpuAddress);
        assert(observedVertices.bytes == sizeof(vertexMemory[segment]));
        assert(observedVertices.drawCount == 2 && observedVertices.stateBatch == (kind == 2));
        assert(observedVertices.offsets == (kind == 2 ? draws : offsets));
        assert(FrontendPlatform.segmentLastEntry[segment] == FrontendPlatform.ringPostSerial);
    }
    slot->recordCount = 0;
}

static void testCpNegotiation(void) {
    for (ULONG scenario = 0; scenario < 14; ++scenario) {
        resetCp();
        switch (scenario) {
        case 0: cpOptIn = FALSE; break;
        case 3: driverRender = FALSE; break; /* Smoke-capable is not render-capable. */
        case 4: driverIndirect = FALSE; break;
        case 5: peerCapsMask &= ~MGLPPC_CAP_CP_EMIT; break;
        case 6: ringMode = FALSE; break;
        case 7: peerVersion = 14; break;
        case 8: smokeEnabled = TRUE; break;
        case 9: allocationFailure = 1; break;
        case 10: allocationFailure = 4; break;
        case 11: serviceGeneration = 77; break;
        case 12: cpOptIn = driverRender = FALSE; break;
        case 13: cpOptIn = driverIndirect = driverRender = FALSE; break;
        }
        APTR created = openCpContext(scenario != 2);
        assert(!!created == (scenario < 2 || scenario >= 12));
        assert(FrontendPlatform.cpEnabled == (scenario == 1));
        assert(host.cpEnabled == (scenario == 1));
        assert(!cpServiceCalls && !ibDispatches && !host.cpDispatches && !host.cpDwords);
        if (scenario == 1) {
            MGLPPCMessage *reply = (void *)FrontendPlatform.registerMessageWords;
            assert(reply->value[3] & MGLPPC_RING_REGISTER_CP_EMIT);
            assert(!(reply->value[3] & (MGLPPC_RING_REGISTER_INDIRECT | MGLPPC_RING_REGISTER_IB_DRY)));
            assert(reply->value[5] == (ULONG)(uintptr_t)cpMemory && reply->value[6] == 32);
            assert(reply->value[7] == sizeof(cpMemory) && reply->payloadWords == 16);
            assert(FrontendPlatform.cpAddress == cpMemory && FrontendPlatform.cpBytes == sizeof(cpMemory));
            assert(directAllocs == 5 && segmentAlloc == 1 && surfaceQueries == 1);
            assert(proofContext.segmentPoolCount == 4 && proofContext.streamSegments);
            for (ULONG i = 0; i < 4; ++i) {
                const struct Radeon3DSegment *s = &proofContext.segmentPool[i];
                assert(s->CpuAddress == vertexMemory[i] && s->Bytes == sizeof(vertexMemory[i]));
                assert(s->Id == 33 + i && s->Id == host.vertexSegmentIds[i]);
                assert(s->GpuAddress == 0x93500000 + i * 0x100000);
                assert(reply->payload[i * 4 + 3] == s->GpuAddress);
            }
        } else if (scenario == 0 || scenario >= 12) {
            assert(!directAllocs && !surfaceQueries && !FrontendPlatform.cpRequested);
            assert(((MGLPPCMessage *)FrontendPlatform.registerMessageWords)->payloadWords == 12);
            for (ULONG i = 0; i < 4; ++i) assert(!proofContext.segmentPool[i].GpuAddress);
            clearPrefix(); assert(FrontendTextureBarrier(&proofContext));
            assert(nativeExecutes == 1 && !emitCalls && !host.cpDispatches);
        } else {
            assert(error == GL_INVALID_OPERATION && FrontendPlatform.cpRequested);
            /* A negotiated semantic/Exec fallback cannot masquerade as CP. */
            assert(!created && !emitCalls && !nativeExecutes);
        }
        finishCp();
    }
    puts("CP negotiation: opt-in, window/protocol/live bit27 gates, no silent fallback, GPU tuples passed");
}

static void testCpPublication(void) {
    startCp(); primeCp(); observePublication = TRUE;
    for (ULONG i = 0; i < 24; ++i) {
        ULONG slot = i % MGLPPC_RING_SLOTS;
        submitCp(i % 3, i % 4, i % 2);
        assert(publications == i + 1 && !publicationStage);
        assert(host.ringControl->completed == i);
        assert(slots[slot][0] == slot * MGLPPC_CP_SLICE_BYTES && slots[slot][1] == 4);
        assert(slots[slot][3] == 0x80000000U);
        assert(cpMemory[slot * MGLPPC_CP_DWORDS + 3] == 0x00000080U);
        assert(RingPoll(&host) == 1 && !RingPoll(&host));
        assert(host.cpDispatches == i + 1 && host.cpDwords == (i + 1) * 4);
        assert(host.semanticExecutes == i + 1 && HostExecuteCount == i + 1);
        assert(host.ringControl->completed == i + 1 && host.ringControl->retired == i);
        assert(host.queuedSegmentMask == expectedVertexMask && host.semanticFence);
    }
    assert(cpServiceCalls == 24 && emitCalls == 24 && !nativeExecutes && !ibDispatches);
    assert(!host.indirectProbeOk && !FrontendPlatform.ibSmokeConfirmed);
    observePublication = FALSE;
    assert(R200PPCPlatformBarrier(&proofContext, TRUE));
    assert(RingPoll(&host) == 1 && !host.semanticFence);
    assert(host.ringControl->retired == 25 && host.ringControl->presentSeq == 25);
    assert(host.cpDispatches == 24 && HostPresentCount == 1);
    finishCp();
    puts("CP publication: 3 emitted + PACKET2 = 4 dispatched, BE exactly-once flush order, 3 ring laps, real counts passed");
}

static void testCpPaddingBoundaries(void) {
    for (ULONG generated = 8191; generated <= 8192; ++generated) {
        startCp(); primeCp(); observePublication = TRUE;
        emitResult = generated; expectedCpCount = 8192;
        for (ULONG slot = 0; slot < MGLPPC_RING_SLOTS; ++slot) {
            size_t offset = slot * MGLPPC_CP_SLICE_BYTES;
            size_t end = offset + MGLPPC_CP_SLICE_BYTES;
            memcpy(heldCp, cpMemory, sizeof(cpMemory)); memcpy(heldSlots, slots, sizeof(slots));
            submitCp(0, 0, 0);
            assert(publications == slot + 1 && !publicationStage);
            assert(slots[slot][1] == 8192 && host.ringControl->entries[slot].dwords == 3);
            ULONG tail = generated == 8191 ? 0x80000000U : mockEmitWord(8191);
            assert(slots[slot][8191] == tail);
            assert(cpMemory[slot * MGLPPC_CP_DWORDS + 8191] == __builtin_bswap32(tail));
            /* Exact-capacity writes may touch neither neighboring slot/slice.
             * The final slot also meets ASan's enclosing allocation redzones. */
            assert(!memcmp(cpMemory, heldCp, offset) && !memcmp(slots, heldSlots, offset));
            assert(!memcmp((UBYTE *)cpMemory + end, (UBYTE *)heldCp + end, sizeof(cpMemory) - end));
            assert(!memcmp((UBYTE *)slots + end, (UBYTE *)heldSlots + end, sizeof(slots) - end));
            assert(RingPoll(&host) == 1 && !host.ringControl->error);
            assert(host.cpDispatches == slot + 1 && host.cpDwords == (slot + 1) * 8192);
        }
        assert(cpServiceCalls == 8 && emitCalls == 8 && !nativeExecutes && !ibDispatches);
        finishCp();
    }
    puts("CP padding boundaries: 8191 -> 8192 and 8192 unchanged, all slices and staging guards intact");
}

static void testCpRetirement(void) {
    startCp(); primeCp();
    for (ULONG i = 0; i < 8; ++i) {
        submitCp(1, i % 4, 0); assert(RingPoll(&host) == 1);
    }
    /* Fresh retirement makes the CP slice reusable without a scheduling call. */
    host.ringControl->retired = 0; refreshRetired = 8;
    ULONG before = yieldCalls, delaysBefore = delayCalls;
    submitCp(1, 0, 0);
    assert(yieldCalls == before && delayCalls == delaysBefore && !refreshRetired);
    assert(host.ringControl->retired == 8 && host.ringControl->head == 9);
    assert(RingPoll(&host) == 1 && cpServiceCalls == 9);
    finishCp();

    startCp(); primeCp(); observePublication = TRUE;
    for (ULONG i = 0; i < 8; ++i) {
        submitCp(1, i % 4, 0); assert(RingPoll(&host) == 1);
    }
    /* A conservative/stale public retirement watermark may lag completion.
     * The live latest fence still covers every submitted slice. */
    host.ringControl->retired = 0; cpRetires = FALSE;
    heldEmits = emitCalls; memcpy(heldCp, cpMemory, sizeof(cpMemory));
    memcpy(heldSlots, slots, sizeof(slots)); releaseCpOnYield = TRUE;
    before = yieldCalls; delaysBefore = delayCalls;
    submitCp(1, 0, 0);
    assert(yieldCalls == before + 1 && !releaseCpOnYield);
    assert(delayCalls == delaysBefore);
    assert(host.ringControl->retired == 8 && host.ringControl->head == 9);
    assert(RingPoll(&host) == 1 && cpServiceCalls == 9);
    observePublication = FALSE; cpRetires = FALSE;
    memcpy(heldCp, cpMemory, sizeof(cpMemory));
    memcpy(heldSlots, slots, sizeof(slots));
    R200PPCDispatchDeleteContext();
    assert(R200PPCContext && host.context && host.semanticFence);
    assert(FrontendPlatform.registered && host.cpSegmentLeased && !frees && !directFrees);
    assert(host.ringControl->completed == 9 && host.ringControl->retired == 8);
    assert(!memcmp(heldCp, cpMemory, sizeof(cpMemory)) && !memcmp(heldSlots, slots, sizeof(slots)));
    for (ULONG i = 0; i < 5; ++i) assert(directLive[i]);
    cpRetires = TRUE; finishCp();
    assert(directFrees == 5 && frees == 8 && cpServiceCalls == 9);
    assert(delayCalls == delaysBefore);

    startCp(); primeCp();
    for (ULONG i = 0; i < 8; ++i) {
        submitCp(1, i % 4, 0); assert(RingPoll(&host) == 1);
    }
    host.ringControl->retired = 0; cpRetires = FALSE;
    memcpy(heldCp, cpMemory, sizeof(cpMemory)); memcpy(heldSlots, slots, sizeof(slots));
    clearPrefix(); clockStep = 10000; before = clockTicks;
    assert(!R200PPCPlatformPublishSlot(&proofContext, 0) && FrontendPlatform.cpFailed);
    assert(clockTicks - before >= PlatformClockRate * 5 && clockTicks - before < PlatformClockRate * 6);
    assert(emitCalls == 8 && cpServiceCalls == 8 && host.ringControl->head == 8);
    assert(!memcmp(heldCp, cpMemory, sizeof(cpMemory)) && !memcmp(heldSlots, slots, sizeof(slots)));
    before = controlCalls; cpRetires = TRUE;
    R200PPCPlatformClose();
    assert(R200PPCContext && host.context && !frees && !directFrees && before == controlCalls);
    puts("CP ownership: completed cannot release slices; delayed/timeout retirement pins buffers, safe drain frees once");
}

static void testCpIdleFinish(void) {
    for (ULONG ordered = 0; ordered < 2; ++ordered) {
        resetCp(); driverOrdered = ordered;
        assert(openCpContext(TRUE)); primeCp();
        assert(!!(host.ringRadeonCaps & RADEON3D_CAP_ORDERED_COMMITS) == ordered);
        submitCp(0, 0, 0); cpRetires = FALSE;
        PollQueuedFence(&host);
        assert(!cpFencePolls && !host.ringControl->retired); /* Still unconsumed. */
        assert(RingPoll(&host) == 1 && !host.queuedSegmentMask);
        ULONG fence = host.semanticFence, flushes = retiredFlushes;
        for (ULONG i = 1; i <= 3; ++i) {
            PollQueuedFence(&host);
            assert(cpFencePolls == i && host.semanticFence == fence);
            assert(host.ringControl->completed == 1 && !host.ringControl->retired);
            assert(retiredFlushes == flushes && !cpFenceWaits);
        }
        cpRetires = TRUE; PollQueuedFence(&host);
        assert(cpFencePolls == 4 && !host.semanticFence && host.ringControl->retired == 1);
        assert(retiredFlushes == flushes + 1 && !host.queuedSegmentMask);
        PollQueuedFence(&host); assert(cpFencePolls == 4); /* No redundant test after retirement. */

        submitCp(0, 0, 0); assert(RingPoll(&host) == 1);
        assert(host.ringControl->completed == 2 && host.ringControl->retired == 1);
        cpRetires = FALSE; retireFenceOnYield = hostPolls = TRUE;
        ULONG before = yieldCalls, calls = controlCalls, delaysBefore = delayCalls;
        Lib_glFinish(); /* Actual public finish -> actual non-present PPC barrier. */
        assert(!error && yieldCalls == before + 1 && !retireFenceOnYield);
        assert(delayCalls == delaysBefore);
        assert(cpFencePolls > 4 && !cpFenceWaits && !host.semanticFence);
        assert(host.ringControl->retired == 2 && host.ringControl->head == 2);
        assert(!host.ringControl->presentSeq && calls == controlCalls && cpServiceCalls == 2);

        hostPolls = FALSE; submitCp(0, 0, 0); assert(RingPoll(&host) == 1);
        hostPolls = TRUE; cpRetires = FALSE; before = yieldCalls;
        Lib_glFinish();
        assert(error == GL_INVALID_OPERATION && yieldCalls > before);
        assert(delayCalls == delaysBefore);
        assert(host.ringControl->completed == 3 && host.ringControl->retired == 2);
        assert(host.semanticFence && !directFrees && !frees && !host.ringControl->error);
        cpRetires = TRUE; error = 0; Lib_glFinish();
        assert(!error && host.ringControl->retired == 3 && cpServiceCalls == 3);
        finishCp();
    }
    puts("CP idle/finish: mask-zero clear retirement tests the fence with/without ORDERED, glFinish waits and times out safely");
}

static void testCpErrorRefresh(void) {
    startCp(); primeCp();
    for (ULONG i = 0; i < 8; ++i) {
        submitCp(0, 0, 0); assert(RingPoll(&host) == 1);
    }
    host.ringControl->retired = 0; cpRetires = FALSE;
    memcpy(heldCp, cpMemory, sizeof(cpMemory)); memcpy(heldSlots, slots, sizeof(slots));
    clearPrefix(); observeCpWait = TRUE;
    ULONG before = yieldCalls, delaysBefore = delayCalls;
    assert(!R200PPCPlatformPublishSlot(&proofContext, 0));
    observeCpWait = FALSE;
    assert(headRefreshes == 2 && retiredRefreshes == 2 && yieldCalls == before);
    assert(delayCalls == delaysBefore);
    assert(FrontendPlatform.cpFailed && host.ringControl->error && !host.ringControl->retired);
    assert(host.ringControl->completed == 8 && host.ringControl->head == 8 && emitCalls == 8);
    assert(!memcmp(heldCp, cpMemory, sizeof(cpMemory)) && !memcmp(heldSlots, slots, sizeof(slots)));
    puts("CP wait refresh: both control lines refreshed every 256 spins; head-line error alone stops publication");
}

static void testCpTeardownGuards(void) {
    const ULONG commands[] = {MGLPPC_CMD_DESTROY_CONTEXT, MGLPPC_CMD_SHUTDOWN,
                              MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS};
    for (ULONG fault = 0; fault < 4; ++fault) {
        startCp(); primeCp(); submitCp(0, 0, 0);
        if (fault != 2) assert(RingPoll(&host) == 1);
        if (fault == 0) host.cpFailed = TRUE;
        if (fault == 1) host.ringControl->error = 1;
        cpRetires = fault == 2; /* Published-but-unconsumed, or an unretired fence. */
        memcpy(heldCp, cpMemory, sizeof(cpMemory)); memcpy(heldSlots, slots, sizeof(slots));
        MGLPPCHostState saved = host;
        assert(!DiscardSemantic(&host) && !memcmp(&host, &saved, sizeof(host)));
        for (ULONG i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
            MGLPPCMessage m = {0};
            m.magic = MGLPPC_PROTOCOL_MAGIC; m.protocolVersion = MGLPPC_PROTOCOL_VERSION;
            m.execMessage.mn_Length = MGLPPC_MESSAGE_HEADER_SIZE;
            m.command = commands[i]; m.contextId = host.contextId; m.value[5] = host.ringEpoch;
            HandleMessage(&m, &host);
            assert(m.status == (fault >= 2 && commands[i] == MGLPPC_CMD_SHUTDOWN ?
                                MGLPPC_STATUS_BUSY : MGLPPC_STATUS_GL_ERROR));
            assert(host.context == saved.context && host.running && host.contextId == saved.contextId);
            assert(host.cpSegmentLeased && host.cpEnabled && host.semanticRegistered);
            assert(host.mailbox == saved.mailbox && host.ringControl == saved.ringControl);
            assert(host.ringSegmentLeased && host.vertexSegmentCount == 4 && Radeon9200Base);
            assert(!nativeDeletes && !directFrees && !frees && !segmentFrees && !libraryCloses);
            for (ULONG j = 0; j < 5; ++j) assert(directLive[j]);
            assert(!memcmp(heldCp, cpMemory, sizeof(cpMemory)) && !memcmp(heldSlots, slots, sizeof(slots)));
        }
        if (fault < 2) {
            /* The global guard must reject ordinary controls, not only teardown.
             * A late successful fence cannot clear a terminal CP error. */
            cpRetires = TRUE; PollQueuedFence(&host);
            assert(host.ringControl->retired == 1 && host.cpFailed);
            MGLPPCMessage m = {0};
            m.magic = MGLPPC_PROTOCOL_MAGIC; m.protocolVersion = MGLPPC_PROTOCOL_VERSION;
            m.execMessage.mn_Length = MGLPPC_MESSAGE_HEADER_SIZE;
            m.command = MGLPPC_CMD_GET_INTEGER; m.contextId = host.contextId;
            m.value[0] = MGL_R200_PRIVATE_TEXTURE_CAPS_V1_QUERY;
            ULONG queries = nativeQueries;
            HandleMessage(&m, &host);
            assert(m.status == MGLPPC_STATUS_GL_ERROR && nativeQueries == queries);
            assert(!DiscardSemantic(&host) && !directFrees && !segmentFrees);
        } else {
            cpRetires = TRUE;
            if (fault == 2) assert(RingPoll(&host) == 1);
            PollQueuedFence(&host); finishCp();
            assert(directFrees == 5 && segmentFrees == 1 && nativeDeletes == 1);
        }
    }
    puts("CP teardown: real discard/DESTROY/SHUTDOWN/unregister refuse error, unconsumed work and fence failure without releasing resources");
}

static void testIntegerQueries(void) {
    for (ULONG mailbox = 0; mailbox < 2; ++mailbox) {
        resetCp();
        if (!mailbox) peerCapsMask &= ~MGLPPC_CAP_MAILBOX_V1;
        assert(openCpContext(TRUE));
        ULONG queries = nativeQueries, direct = directAllocs, segments = segmentAlloc;
        /* Test the whole reserved namespace, including unknown future selectors.
         * Native descriptor handlers are compiled: bypassing the real guard
         * would hand those handlers a scalar stack slot under ASan. */
        for (ULONG selector = 0x4d475000; selector <= 0x4d475fff; ++selector) {
            if (selector == MGL_R200_PRIVATE_TEXTURE_CAPS_V1_QUERY) continue;
            memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
            FrontendPlatform.control.value[0] = selector;
            assert(!PlatformCall(MGLPPC_CMD_GET_INTEGER, FrontendPlatform.contextId));
            assert(FrontendPlatform.control.status == MGLPPC_STATUS_UNSUPPORTED);
            assert(FrontendPlatform.control.value[0] == selector && nativeQueries == queries);
        }
        FrontendPlatform.control.value[0] = MGL_R200_PRIVATE_TEXTURE_CAPS_V1_QUERY;
        assert(PlatformCall(MGLPPC_CMD_GET_INTEGER, FrontendPlatform.contextId));
        assert(FrontendPlatform.control.value[0] == (MGL_R200_PRIVATE_TEXTURE_CAPS_V1_TAG |
               MGL_R200_PRIVATE_TEXTURE_CAP_SUBIMAGE_DIRECT_BGRA));
        FrontendPlatform.control.value[0] = GL_MAX_TEXTURE_SIZE;
        assert(PlatformCall(MGLPPC_CMD_GET_INTEGER, FrontendPlatform.contextId));
        assert(FrontendPlatform.control.value[0] == 2048 && nativeQueries == queries + 2);
        assert(!cpServiceCalls && directAllocs == direct && segmentAlloc == segments);
        assert(!PlatformCall(MGLPPC_CMD_GET_INTEGER, FrontendPlatform.contextId + 1));
        assert(nativeQueries == queries + 2);
        finishCp();
    }
    puts("GET_INTEGER: real mailbox/Exec handler rejects all private descriptors; only tagged caps and ordinary scalars reach native queries");
}

static void testCpMetadata(void) {
    for (ULONG mailbox = 0; mailbox < 2; ++mailbox) {
        resetCp();
        if (!mailbox) peerCapsMask &= ~MGLPPC_CAP_MAILBOX_V1;
        assert(openCpContext(TRUE));
        struct MGLPPCEmitSurface s, cached;
        ULONG before = surfaceQueries;
        assert(ResolveCpSurface(&proofContext, 2, &s));
        assert(s.cpuAddress == (ULONG)(uintptr_t)vertexMemory[0] && s.gpuAddress == 0x81200000);
        assert(s.pitch == 1280 && s.width == 320 && s.height == 240 && s.format == RADEON3D_FORMAT_B8G8R8A8);
        assert(ResolveCpSurface(&proofContext, 2, &cached) && !memcmp(&s, &cached, sizeof(s)));
        assert(surfaceQueries == before + 1);
        assert(ResolveCpSurface(&proofContext, 3, &s));
        assert(s.gpuAddress == native.depthSurface.GpuAddress);
        assert(ResolveCpSurface(&proofContext, 16, &s));
        before = surfaceQueries;
        texture(MGLPPC_CMD_BIND_TEXTURE);
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] = GL_MAX_TEXTURE_SIZE;
        assert(PlatformCall(MGLPPC_CMD_GET_INTEGER, FrontendPlatform.contextId));
        assert(FrontendPlatform.control.value[0] == 2048);
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[5] = host.semanticEpoch;
        assert(PlatformCall(MGLPPC_CMD_SEMANTIC_BARRIER, FrontendPlatform.contextId));
        assert(ResolveCpSurface(&proofContext, 16, &cached) && !memcmp(&s, &cached, sizeof(s)));
        assert(surfaceQueries == before);
        const ULONG mutations[] = {MGLPPC_CMD_TEXTURE_UPLOAD_BEGIN, MGLPPC_CMD_TEXTURE_UPLOAD_CHUNK,
            MGLPPC_CMD_TEXTURE_UPLOAD_END, MGLPPC_CMD_TEXTURE_UPLOAD_SHARED,
            MGLPPC_CMD_TEXTURE_DIRECT_BEGIN, MGLPPC_CMD_TEXTURE_DIRECT_COMMIT,
            MGLPPC_CMD_TEXTURE_DIRECT_ABORT, MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED,
            MGLPPC_CMD_DELETE_TEXTURES, MGLPPC_CMD_GEN_TEXTURES, MGLPPC_CMD_READ_PIXELS_SHARED};
        for (ULONG i = 0; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
            native.textures[0].surface.GpuAddress += 16;
            texture(mutations[i]);
            assert(!FrontendPlatform.cpSurfaceCount);
            assert(ResolveCpSurface(&proofContext, 16, &s));
            assert(s.gpuAddress == native.textures[0].surface.GpuAddress);
            assert(surfaceQueries == ++before);
        }
        /* Bounded cache replacement never mistakes another token for a hit. */
        FrontendPlatform.cpSurfaceCount = 0;
        for (ULONG i = 0; i < 17; ++i) assert(ResolveCpSurface(&proofContext, 16 + i, &s));
        assert(FrontendPlatform.cpSurfaceCount == 1 && surfaceQueries == before + 17);
        assert(ResolveCpSurface(&proofContext, 16, &s) && surfaceQueries == before + 18);
        before = surfaceQueries;
        assert(!ResolveCpSurface(&proofContext, 0, &s) && surfaceQueries == before);
        assert(!ResolveCpSurface(&proofContext, 0xdead, &s) && surfaceQueries == before + 1);
        ULONG entries = FrontendPlatform.cpSurfaceCount;
        ++proofContext.deviceEpoch;
        assert(!ResolveCpSurface(&proofContext, 16, &s) && surfaceQueries == before + 2);
        assert(FrontendPlatform.cpSurfaceCount == entries); /* Old-epoch reply is not cached. */
        --proofContext.deviceEpoch;
        FrontendPlatform.cpSurfaceCount = 0;
        ++native.deviceEpoch;
        assert(!ResolveCpSurface(&proofContext, 2, &s)); /* Host rejects native epoch drift. */
        --native.deviceEpoch;
        serviceGeneration = nativeCreates + 1;
        assert(!ResolveCpSurface(&proofContext, 2, &s)); /* Native rejects stale imports. */
        serviceGeneration = 0;
        ULONG epoch = proofContext.deviceEpoch, registrationEpoch = FrontendPlatform.epoch;
        R200PPCDispatchDeleteContext();
        assert(!host.context && !R200PPCContext);
        directAllocs = 0;
        assert(R200PPCDispatchCreateContext(0, 0, 320, 240));
        assert(epoch == 1 && proofContext.deviceEpoch == epoch && !FrontendPlatform.cpSurfaceCount);
        assert(FrontendPlatform.epoch > registrationEpoch);
        before = surfaceQueries;
        assert(ResolveCpSurface(&proofContext, 16, &s) && surfaceQueries == before + 1);
        assert(s.gpuAddress == native.textures[0].surface.GpuAddress);
        finishCp();
    }
    puts("CP metadata: real native/host/PPC roundtrip, mailbox+Exec, hits, invalidations, eviction, epoch changes passed");
}

static void testCpBadMetadata(void) {
    for (ULONG fault = 0; fault < 28; ++fault) {
        startCp(); primeCp(); submitCp(0, 0, 0);
        MGLPPCRingControlV1 *c = (void *)sharedRing;
        MGLPPCRingEntryV1 *e = &c->entries[0];
        switch (fault) {
        case 0: slots[0][1] = 0; break;
        case 1: slots[0][1] = MGLPPC_CP_DWORDS + 2; break; /* Even but oversized. */
        case 2: slots[0][1] = UINT32_MAX; break;
        case 3: slots[0][0] = 1; break;
        case 4: slots[0][0] = MGLPPC_CP_SLICE_BYTES; break;
        case 5: slots[0][0] = UINT32_MAX; break;
        case 6: host.cpSegment.Bytes = 8; break;
        case 7: host.cpSegment.Bytes = 0; break;
        case 8: slots[0][2] = 1UL << MGLPPC_VERTEX_SEGMENTS; break;
        case 9: slots[0][2] = UINT32_MAX; break;
        case 10: e->dwords = 2; break;
        case 11: e->dwords = 4; break;
        case 12: e->flags |= MGLPPC_SEMANTIC_FLAG_PRESENT; break;
        case 13: e->flags |= MGLPPC_SEMANTIC_FLAG_COMMIT; break;
        case 14: e->flags |= MGLPPC_SEMANTIC_FLAG_INDIRECT; break;
        case 15: e->slot = 1; break;
        case 16: e->slot = MGLPPC_RING_SLOTS; break;
        case 17: e->generation = 0; break;
        case 18: host.cpEnabled = FALSE; break;
        case 19: host.cpFailed = TRUE; break;
        case 20: host.cpSegmentLeased = FALSE; break;
        case 21: wrongEpoch = TRUE; break;
        case 22: bridgeDevice = 2; break;
        case 23: bridgeColor = 3; break;
        case 24: e->dwords = MGLPPC_SEMANTIC_SLOT_DWORDS + 1; break;
        case 25: slots[0][1] = 3; break; /* Former unpadded three-word submission. */
        case 26: slots[0][1] = 1; break;
        case 27: slots[0][1] = 8191; break;
        }
        assert(!RingPoll(&host) && c->error && !c->completed && !c->retired);
        assert(!cpServiceCalls && !nativeExecutes && !host.cpDispatches && !host.cpDwords);
        assert(!RingPoll(&host)); /* A failed descriptor is never retried. */
    }
    puts("CP consumer: 28 bad count/offset/mask/entry/session tuples, including odd 1/3/8191, rejected before dispatch");
}

static void testCpFailures(void) {
    for (ULONG fault = 0; fault < 9; ++fault) {
        startCp(); primeCp(); clearPrefix();
        MGLPPCRingControlV1 *c = (void *)sharedRing;
        switch (fault) {
        case 0: emitResult = 0; break;
        case 1: emitResult = MGLPPC_CP_DWORDS + 1; break;
        case 2: emitToken = 0; break;
        case 3: FrontendPlatform.cpAddress = NULL; break;
        case 4: --FrontendPlatform.cpBytes; break;
        case 5: c->slotAddrs[0] = 0; break;
        case 6: ++c->epoch; break;
        case 7: c->error = 1; break;
        case 8:
            proofContext.submitSlots[0].commitMode = TRUE;
            proofContext.submitSlots[0].drawCount = 1;
            proofContext.segment.info.Id = UINT32_MAX;
            break;
        }
        memcpy(heldCp, cpMemory, sizeof(cpMemory));
        assert(!R200PPCPlatformPublishSlot(&proofContext, 0) && FrontendPlatform.cpFailed);
        assert(!c->head && !memcmp(heldCp, cpMemory, sizeof(cpMemory)));
        assert(!cpServiceCalls && !host.cpDispatches && !nativeExecutes);
        ULONG emits = emitCalls, calls = controlCalls;
        assert(!R200PPCPlatformPublishSlot(&proofContext, 0) && emitCalls == emits);
        R200PPCPlatformClose();
        assert(R200PPCContext && host.context && !directFrees && !frees && calls == controlCalls);
    }
    for (ULONG fault = 0; fault < 3; ++fault) {
        startCp(); primeCp(); submitCp(1, 0, 0);
        if (fault == 0) cpDispatchOK = FALSE;
        if (fault == 1) cpZeroFence = TRUE;
        if (fault == 2) {
            assert(RingPoll(&host) == 1);
            cpRetires = FALSE; submitCp(1, 1, 0);
        }
        assert(!RingPoll(&host) && host.cpFailed && host.ringControl->error);
        assert(cpServiceCalls == 1 && host.cpDispatches == (fault == 2));
        assert(host.cpDwords == (fault == 2 ? 4U : 0U));
        assert(host.ringControl->completed == (fault == 2));
        assert(!host.ringControl->retired && !nativeExecutes);
        ULONG calls = controlCalls;
        cpRetires = TRUE; /* Late retirement does not authorize replay after fatal error. */
        R200PPCPlatformClose();
        assert(R200PPCContext && host.context && !directFrees && !frees && calls == controlCalls);
        assert(!RingPoll(&host) && cpServiceCalls == 1);
    }
    puts("CP failures: partial/oversized emit, no fallback, failed dispatch/fence, terminal buffer retention passed");
}

static void testSharedSemanticHandoff(void) {
    const char *names[] = {"inline", "COMMIT", "STATE_BATCH"};
    for (ULONG path = 0; path < 3; ++path) {
        resetCp(); cpOptIn = FALSE;
        assert(openCpContext(TRUE) && !host.cpEnabled && !FrontendPlatform.cpEnabled);
        hostPolls = FALSE; verifySemanticHandoff = TRUE; semanticPath = path;
        for (ULONG serial = 0; serial < 24; ++serial) {
            ULONG build = serial % SUBMIT_SLOT_COUNT;
            struct SubmitSlot *slot = &proofContext.submitSlots[build];
            ULONG original[2 * RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS];
            ULONG offsets[] = {64 + serial * 128, 1024 + serial * 128};
            ULONG draws[] = {offsets[0], 3, offsets[1], 6};
            semanticSlot = serial % MGLPPC_RING_SLOTS;
            semanticWords = path ? RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS * (path == 1 ? 2 : 1) : 11;
            semanticPayloadWords = semanticWords + (path == 1 ? 4 : path == 2 ? 6 : 0);
            semanticInvalidates = semanticCleans = 0; semanticCompleted = serial;
            bridgeColor = 0x9000 + serial; /* Live target differs from both recorded tokens. */
            memset(slot->records, 0, semanticWords * sizeof(ULONG));
            if (!path) {
                const ULONG clear[] = {RADEON3D_EXEC_CLEAR, 11, 0x123, 0, RADEON3D_CLEAR_COLOR,
                    0xff112200 + serial, 0, 0, 0, 320, 240};
                memcpy(slot->records, clear, sizeof(clear));
            } else {
                for (ULONG record = 0; record < (path == 1 ? 2U : 1U); ++record) {
                    ULONG *r = slot->records + record * RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS;
                    r[0] = record ? RADEON3D_EXEC_DRAW_QUADS : RADEON3D_EXEC_DRAW_TRIANGLES;
                    r[1] = RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS;
                    r[2] = 0x100 + serial * 2 + record;
                    r[5] = RADEON3D_DRAW_HW_TCL | RADEON3D_DRAW_EXTENDED_VERTEX | RADEON3D_DRAW_FRAGMENT_STATE;
                    r[8] = 320; r[9] = 240; r[10] = record ? 4 : 3;
                    for (ULONG j = 0; j < 16; ++j) r[21 + j] = j % 5 == 0 ? 0x3f800000 : 0;
                    r[24] = 0x3e800000 + serial * 0x1000; /* Distinct non-target data on every reuse. */
                    r[37] = r[39] = 0x43200000; r[38] = 0xc2f00000; r[40] = 0x42f00000;
                    r[41] = r[42] = 0x3f000000;
                }
            }
            memcpy(original, slot->records, semanticWords * sizeof(ULONG));
            memcpy(expectedRecords, original, semanticWords * sizeof(ULONG));
            expectedRecords[2] = bridgeColor;
            if (path == 1) expectedRecords[RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS + 2] = bridgeColor;
            slot->recordCount = semanticWords; slot->commitMode = path != 0;
            slot->stateBatch = path == 2; slot->drawCount = path ? 2 : 0;
            slot->vertexOffsets = offsets; slot->stateBatchDraws = draws; slot->segmentCursor = 256;
            proofContext.buildSlot = build;
            proofContext.segment.info = proofContext.segmentPool[serial % MGLPPC_VERTEX_SEGMENTS];
            semanticSegment = proofContext.segment.info.Id;
            assert(R200PPCPlatformPublishSlot(&proofContext, build));
            slot->recordCount = 0;
            assert(host.ringControl->entries[semanticSlot].dwords == semanticPayloadWords);
            if (path) {
                assert(slots[semanticSlot][0] == 2 && slots[semanticSlot][1] == serial % MGLPPC_VERTEX_SEGMENTS);
                assert(!memcmp(slots[semanticSlot] + 2 + semanticWords,
                               path == 1 ? offsets : draws, (path == 1 ? 2 : 4) * sizeof(ULONG)));
            }
            memcpy(heldSlots, slots, sizeof(slots));
            /* Only the live color targets may change in the published payload.
             * All descriptors, other record fields and unused slots stay intact. */
            ULONG recordBase = path ? MGLPPC_RING_COMMIT_HEADER_DWORDS : 0;
            heldSlots[semanticSlot][recordBase + 2] = bridgeColor;
            if (path == 1)
                heldSlots[semanticSlot][recordBase + RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS + 2] = bridgeColor;
            assert(RingPoll(&host) == 1 && !RingPoll(&host));
            assert(semanticInvalidates == 1 && semanticCleans == 1 && semanticCalls[path] == serial + 1);
            assert(!memcmp(slots, heldSlots, sizeof(slots)));
            assert(!memcmp(slot->records, original, semanticWords * sizeof(ULONG)));
            for (ULONG i = 0; i < MGLPPC_RING_SLOTS; ++i) assert(!hostDirty[i]);
            assert(host.ringControl->completed == serial + 1 && !host.ringControl->error);
            assert(host.semanticExecutes == serial + 1 && HostExecuteCount == serial + 1);
        }
        assert(!emitCalls && !cpServiceCalls && !ibDispatches && nativeExecutes == 24);
        verifySemanticHandoff = FALSE;
        assert(DrainSemanticFence(&host)); finishCp();
        printf("Shared semantic %s: 24 submits, two wraps, exact target patches, descriptors intact, clean before completion passed\n", names[path]);
    }
}

int main(int argc, char **argv) {
    (void)argv;
    BOOL expectFault = argc > 1;
    assert((uintptr_t)sharedRing + sizeof(sharedRing) < UINT32_MAX);
    for (ULONG mode = 0; mode < 2; ++mode) {
        ULONG previousId = 0, previousEpoch = 0;
        ringMode = mode == 0;
        memset(&host, 0, sizeof(host)); host.backendOpen = host.running = 1;
        assert(R200PPCPlatformOpen());
        for (ULONG context = 0; context < 2; ++context) {
            ULONG id, epoch, updatesBefore = subimages;
            assert(R200PPCDispatchCreateContext(0,0,320,240));
            id = FrontendPlatform.contextId; epoch = FrontendPlatform.epoch;
            assert(id == host.contextId && epoch == (ringMode ? host.ringEpoch : host.semanticEpoch));
            if (context && !expectFault) {
                MGLPPCMessage stale;
                memset(&stale, 0, sizeof(stale));
                stale.magic = MGLPPC_PROTOCOL_MAGIC; stale.protocolVersion = MGLPPC_PROTOCOL_VERSION;
                stale.execMessage.mn_Length = MGLPPC_MESSAGE_HEADER_SIZE;
                stale.command = MGLPPC_CMD_DESTROY_CONTEXT;
                stale.contextId = previousId;
                HandleMessage(&stale, &host);
                assert(stale.status == MGLPPC_STATUS_NO_CONTEXT && host.context);
                stale.command = MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS;
                stale.contextId = id;
                stale.value[5] = previousEpoch;
                HandleMessage(&stale, &host);
                assert(stale.status == MGLPPC_STATUS_BAD_PAYLOAD && host.semanticRegistered);
                stale.value[5] = 0; /* inactive epoch namespace must not authorize unregister */
                HandleMessage(&stale, &host);
                assert(stale.status == MGLPPC_STATUS_BAD_PAYLOAD && host.semanticRegistered);
            }
            assert(!host.semanticExecutes && !host.semanticFence && !host.queuedSegmentMask);
            assert(!FrontendPlatform.mailboxOutstanding && !FrontendPlatform.mailboxAbandoned &&
                   !FrontendPlatform.mailboxPoisoned && !FrontendPlatform.ringPostSerial);
            assert(!FrontendPlatform.mailboxSerial && !FrontendPlatform.pending[0]);
            if (ringMode) {
                assert(controlInvalidated == (1UL << ((sizeof(MGLPPCRingControlV1) + 31) / 32)) - 1UL);
                assert(host.mailbox && FrontendPlatform.mailbox == host.mailbox && !host.mailboxSerial);
                assert(!host.ringControl->head && !host.ringControl->completed && !host.ringControl->retired);
                for (ULONG i = 0; i < 8; ++i) assert(!host.ringLastGen[i]);
                for (ULONG i = 0; i < 4; ++i) assert(!FrontendPlatform.segmentLastEntry[i]);
            }
            /* Bind/full upload succeed before the first drawing barrier. */
            texture(MGLPPC_CMD_BIND_TEXTURE);
            texture(MGLPPC_CMD_TEXTURE_DIRECT_BEGIN);
            texture(MGLPPC_CMD_TEXTURE_DIRECT_COMMIT);
            clearPrefix();
            if (!FrontendTextureBarrier(&proofContext)) {
                assert(expectFault && ringMode && context == 1 && error == GL_INVALID_OPERATION);
                assert(host.ringControl->error && !host.ringControl->completed && host.ringControl->head == 1);
                assert(host.ringControl->entries[0].crc == 0 && SemanticCRC32(slots[0], 11) != 0);
                assert(!FrontendPlatform.mailboxOutstanding && !FrontendPlatform.mailboxPoisoned);
                assert(subimages == updatesBefore); /* command 26 was never sent */
                printf("REPRO: context2 id=%u epoch=%u CRC sent=0 expected=%08x ring error=1 completed=0 head=1 GL=0x%x\n",
                       id, epoch, SemanticCRC32(slots[0], 11), error);
                R200PPCPlatformClose();
                assert(R200PPCContext && host.context && host.running && !shutdowns);
                puts("REPRO: cleanup retains context; host remains running, mailbox is NOT poisoned");
                return 0;
            }
            assert(!expectFault || context == 0);
            assert(id > previousId && epoch > previousEpoch);
            previousId = id; previousEpoch = epoch;
            texture(MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED);
            clearPrefix(); assert(FrontendTextureBarrier(&proofContext));
            texture(MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED);
            assert(subimages == updatesBefore + 2);
            if (ringMode) {
                assert(host.mailboxSerial == 5 && FrontendPlatform.mailboxSerial == 5);
                assert(host.ringCompleted == 2 && FrontendPlatform.ringPostSerial == 2);
                /* Wrap the ring twice: every slot must be reusable without
                 * a later 68k writeback overwriting the new PPC payload. */
                for (ULONG i = 0; i < 16; ++i) {
                    clearPrefix(); assert(FrontendTextureBarrier(&proofContext));
                }
                for (ULONG i = 0; i < 8; ++i) assert(!hostDirty[i]);
                assert(R200PPCPlatformBarrier(&proofContext, TRUE)); poll();
                testWaitRefresh();
            }
            printf("PASS: %s context%u id=%u epoch=%u first/repeat submit + mailbox/Exec updates\n",
                   ringMode ? "ring" : "inline", context + 1, id, epoch);
            R200PPCDispatchDeleteContext();
            assert(!R200PPCContext && !host.context && !FrontendPlatform.registered);
            assert(!host.mailbox && !host.ringControl && !host.semanticRegistered);
            assert(!host.semanticFence && !host.queuedSegmentMask && !Radeon9200Base);
        }
        R200PPCPlatformClose();
        assert(!FrontendPlatform.opened && !host.running && !R200PPCContext);
    }
    assert(nativeCreates == 4 && nativeDeletes == 4 && shutdowns == 2 && transportCloses == 2);
    assert(allocations == frees);
    puts("Two-context lifecycle passed for ring/mailbox and Exec fallback; one DLL open per pair");
    testIndirectSmoke();
    testCpNegotiation();
    testCpPublication();
    testCpPaddingBoundaries();
    testCpRetirement();
    testCpIdleFinish();
    testCpErrorRefresh();
    testCpTeardownGuards();
    testIntegerQueries();
    testCpMetadata();
    testCpBadMetadata();
    testCpFailures();
    testSharedSemanticHandoff();
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="r200-lifecycle-", dir="/tmp/opencode") as directory:
    source = Path(directory) / "test.c"
    binary = Path(directory) / "test"
    source.write_text(code)
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-variable", "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
                    "-g", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(source), "-o", str(binary)], check=True, timeout=60)
    if "--regress-ib-ack" in sys.argv:
        completed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        assert completed.returncode != 0 and "ok && !FrontendPlatform.ibSmokeFailed" in completed.stderr
        print("REPRO: missing public completion acknowledgment times out the PPC smoke")
    elif "--regress-waits" in sys.argv:
        completed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        assert completed.returncode != 0 and "yieldCalls == before" in completed.stderr, completed.stdout + completed.stderr
        print("REPRO: not rechecking fresh completion forces an unnecessary yield")
    elif "--regress-register" in sys.argv:
        completed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        assert completed.returncode != 0 and "controlPublished" in completed.stderr
        print("REPRO: registration must publish the control block before replying")
    elif "--regress-cache" in sys.argv:
        completed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        assert completed.returncode != 0 and "p[5] == expectedClearColor" in completed.stderr
        print("REPRO: missing post-service release clean overwrites the next PPC payload after ring wrap")
    else:
        subprocess.run([str(binary)] + (["expect-fault"] if "--regress-crc" in sys.argv else []),
                       check=True, timeout=60)
