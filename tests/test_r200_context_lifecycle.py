"""Two contexts in one DLL session, using production lifecycle/transport code.

python3 tests/test_r200_context_lifecycle.py
python3 tests/test_r200_context_lifecycle.py --regress-crc

GPU services and allocation are mocked. RingPoll retains its actual inline path
and CRC/error/completion logic; unused streamed-commit branches are excluded.
--regress-crc removes only CreateContext's CRC rearm to reproduce the old fault.
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


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


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
typedef ULONG GLenum;
typedef unsigned char UBYTE;
typedef void *APTR;
typedef const char *CONST_STRPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_NO_ERROR 0
#define GL_INVALID_OPERATION 0x59
#define CACRF_ClearD 1
#define SUBMIT_SLOT_COUNT 2
#define SEGMENT_HALF_BYTES 32768
#define RADEON3D_LIBRARY_VERSION 1
#define RADEON3D_MAX_SEGMENT_BYTES 65536
#define RADEON3D_SEGMENT_V1_SIZE 32
#define RADEON3D_SEGMENT_VERSION 1
#define RADEON3D_STATE_BATCH_DRAW_V1_SIZE 8
#define RADEON3D_SUBMIT_FENCE 1
#define RADEON3D_CAP_ORDERED_COMMITS 1
#define MGL_R200_PRIVATE_SEGMENT_V1_VERSION 1
#define MGL_R200_PRIVATE_SEGMENT_V1_QUERY 1
#define MGL_R200_PRIVATE_SEGMENT_FREE_V1_QUERY 2
#define MGL_R200_PRIVATE_BRIDGE_COMPACT_TCL 1
#define R200PPCTimelineMark(...) ((void)0)
struct Message { UBYTE pad[18]; unsigned short mn_Length; };
'''
code += section(P, "#define MGLPPC_HOST_PORT_NAME", "/* Exec sees this structure").replace(
    "#ifndef MGLPPC_TEXTURE_VALIDATION_TEST", "")
code += r'''
typedef struct { ULONG protocolVersion; } MGLPPCTransport;
struct Radeon3DDevice { int unused; };
struct Radeon3DSegment { ULONG Size, Version; APTR CpuAddress; ULONG Bytes, Id, GpuAddress; };
struct Radeon3DInfo { ULONG Size, Generation, Caps; };
typedef struct { ULONG version, cpuAddress, bytes, segmentId, deviceEpoch; } MGLR200PrivateSegmentV1;
typedef struct { ULONG segmentId; } MGLR200PrivateSegmentFreeV1;
typedef struct { ULONG deviceToken, colorSurfaceToken, depthSurfaceToken,
    width, height, radeonCaps, batchDwords, flags, deviceEpoch; } MGLR200PrivateBridgeV1;
typedef MGLR200PrivateBridgeV1 MGLR200PrivateBridgeV2;
struct SubmitSlot { ULONG *records; ULONG recordCount, drawCount, segmentCursor;
    BOOL commitMode, stateBatch; ULONG *stateBatchDraws, *vertexOffsets; };
struct MiniGLR200Context {
    ULONG width, height, pixelDepth, radeonCaps, batchDwords, deviceEpoch;
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
code += "static struct { ULONG " + ", ".join(sorted(set(re.findall(r"PlatformProfile\.(\w+)", F)))) + "; } PlatformProfile;\n"
code += section(H, "static ULONG HostClockHz;", "static ULONG HostClock(void)")
code += r'''
static struct MiniGLR200Context proofContext, *R200PPCContext;
static MGLPPCHostState host;
static APTR Radeon9200Base;
static ULONG PlatformClockRate = 1000000, PlatformSessionStart, clockTicks;
static ULONG nativeCreates, nativeDeletes, nativeExecutes, subimages, shutdowns, transportCloses;
static ULONG error, allocations, frees, segmentAlloc;
static BOOL ringMode = TRUE, polling;
static UBYTE sharedRing[65536] __attribute__((aligned(64)));
static UBYTE vertexMemory[4][65536] __attribute__((aligned(64)));
static ULONG slots[8][8192], buildRecords[2][8192];
static ULONG RingPoll(MGLPPCHostState *state);
static void PollMailbox(MGLPPCHostState *state);
static void HandleMessage(MGLPPCMessage *message, MGLPPCHostState *state);
static ULONG PerfClock(void) { return ++clockTicks; }
static ULONG PerfClockRate(void) { return 1000000; }
static ULONG HostClock(void) { return PerfClock(); }
static void CacheClearE(APTR p, ULONG n, int f) { (void)p; (void)n; (void)f; }
static void poll(void) {
    if (!polling) { polling = TRUE; RingPoll(&host); PollMailbox(&host); polling = FALSE; }
}
static void MGLPPCInvalidateLine32(APTR p) { (void)p; poll(); }
static void MGLPPCFlushDataRange(APTR p, ULONG n) { (void)p; (void)n; }
static APTR AllocVec(ULONG bytes, ULONG flags) {
    (void)flags; assert(bytes == sizeof(slots[0]));
    return slots[allocations++ % 8];
}
static void FreeVec(APTR p) { assert(p); ++frees; }
static APTR OpenLibrary(CONST_STRPTR name, ULONG v) { (void)name; (void)v; return &host; }
static void CloseLibrary(APTR p) { assert(p == &host); }
static void mglChooseWindowMode(int n) { (void)n; }
static void mglChoosePixelDepth(int n) { (void)n; }
static void mglChooseVertexBufferSize(int n) { (void)n; }
static void mglChooseTextureBufferSize(int n) { (void)n; }
static void mglChooseMtexBufferSize(int n) { (void)n; }
static void mglChooseNumberOfBuffers(int n) { (void)n; }
static APTR mglCreateContext(int x, int y, int w, int h) {
    assert(!host.context && !x && !y && w == 320 && h == 240);
    ++nativeCreates; segmentAlloc = 0; return &host;
}
static void mglDeleteContext(void) { assert(host.context); ++nativeDeletes; }
static APTR mglGetWindowHandle(void) { return &host; }
static BOOL mglLockDisplay(void) { return TRUE; }
static void mglUnlockDisplay(void) {}
static void mglSwitchDisplay(void) {}
static void mglEnableSync(int enabled) { (void)enabled; }
static ULONG glGetError(void) { return 0; }
static BOOL QueryBackendBridge(MGLR200PrivateBridgeV1 *b) {
    memset(b, 0, sizeof(*b)); b->deviceToken = 1; b->colorSurfaceToken = 2;
    b->depthSurfaceToken = 3; b->width = 320; b->height = 240;
    b->batchDwords = 8192; b->deviceEpoch = nativeCreates; return TRUE;
}
static BOOL QueryBackendBridgeV2(MGLR200PrivateBridgeV2 *b) { return QueryBackendBridge(b); }
static BOOL Radeon3DGetInfo(struct Radeon3DDevice *d, struct Radeon3DInfo *i) {
    (void)d; i->Generation = nativeCreates; i->Caps = 1; return TRUE;
}
static BOOL Radeon3DWaitFence(struct Radeon3DDevice *d, ULONG f, ULONG timeout) {
    (void)d; (void)timeout; assert(f); return TRUE;
}
static BOOL Radeon3DExecute(struct Radeon3DDevice *d, ULONG *p, ULONG n, ULONG flags, ULONG *fence) {
    (void)d; (void)p; (void)flags; assert(n == 11); *fence = ++nativeExecutes; return TRUE;
}
static void PatchRecordTargets(ULONG *p, ULONG n, ULONG target) { (void)p; (void)n; (void)target; }
static void glGetIntegerv(ULONG query, GLint *p) {
    if (query == MGL_R200_PRIVATE_SEGMENT_FREE_V1_QUERY) return;
    assert(query == MGL_R200_PRIVATE_SEGMENT_V1_QUERY);
    MGLR200PrivateSegmentV1 *s = (void *)p;
    memset(s, 0, sizeof(*s));
    assert(segmentAlloc < 5);
    s->version = 1; s->bytes = 65536; s->deviceEpoch = nativeCreates;
    s->cpuAddress = (ULONG)(uintptr_t)(segmentAlloc ? vertexMemory[segmentAlloc - 1] : sharedRing);
    s->segmentId = ++segmentAlloc;
}
static void DiscardProbe(MGLPPCHostState *s) { (void)s; }
static void DiscardUpload(MGLPPCHostState *s) { (void)s; }
'''
code += section(H, "#define HOST_STAGE", "static void DiscardSemantic")
code += section(H, "static ULONG SemanticCRCTable[256];", "static BOOL QueryBackendBridge(")
for name in ("static BOOL DrainSemanticFence(", "static void DiscardSemantic(",
             "static BOOL RegisterSemanticSlots(", "static BOOL RegisterSemanticRing(",
             "static BOOL ExecuteSemanticSlot("):
    code += function(H, name)
ring = function(H, "static ULONG RingPoll(")
start = ring.index("        if (entry.flags & MGLPPC_SEMANTIC_FLAG_STATE_BATCH)")
end = ring.index(" else if (entry.dwords)", start)
ring = ring[:start] + "        if (entry.dwords)" + ring[end + len(" else if (entry.dwords)"):]
code += ring
code += r'''
static void HandleMessage(MGLPPCMessage *message, MGLPPCHostState *state) {
    message->status = MGLPPC_STATUS_OK;
    switch (message->command) {
    case MGLPPC_CMD_HELLO:
        message->value[3] = MGLPPC_CAP_SEMANTIC_INLINE | MGLPPC_CAP_CRC_FIRST_ONLY |
            (ringMode ? MGLPPC_CAP_SEMANTIC_RING | MGLPPC_CAP_MAILBOX_V1 : 0);
        break;
'''
for begin, end in (("    case MGLPPC_CMD_CREATE_CONTEXT:", "    case MGLPPC_CMD_CLEAR_PRESENT:"),
                   ("    case MGLPPC_CMD_DESTROY_CONTEXT:", "    case MGLPPC_CMD_EXECUTE_BATCH:"),
                   ("    case MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS:", "    case MGLPPC_CMD_BIND_TEXTURE:")):
    code += section(H, begin, end)
code += r'''
    case MGLPPC_CMD_REGISTER_SEMANTIC_RING: RegisterSemanticRing(message, state); break;
    case MGLPPC_CMD_REGISTER_SEMANTIC_SLOTS: RegisterSemanticSlots(message, state); break;
    case MGLPPC_CMD_EXECUTE_SEMANTIC_SLOT: ExecuteSemanticSlot(message, state); break;
    case MGLPPC_CMD_SEMANTIC_BARRIER:
        assert(message->contextId == state->contextId && message->value[5] == state->semanticEpoch);
        assert(DrainSemanticFence(state)); break;
    case MGLPPC_CMD_SHUTDOWN:
        assert(!state->context); ++shutdowns; DiscardProbe(state); state->running = 0; break;
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
code += function(H, "static void PollMailbox(")
code += section(F, "static ULONG PlatformCRCTable[256];", "/* A late completion")
code += r'''
static BOOL MGLPPCTransportOpen(MGLPPCTransport *t) { t->protocolVersion = 15; return TRUE; }
static void MGLPPCTransportClose(MGLPPCTransport *t) { (void)t; ++transportCloses; }
static BOOL MGLPPCTransportCall(MGLPPCTransport *t, MGLPPCMessage *m) {
    poll(); m->protocolVersion = t->protocolVersion; HandleMessage(m, &host); return TRUE;
}
static BOOL MGLPPCTransportSend(MGLPPCTransport *t, MGLPPCMessage *m) { return MGLPPCTransportCall(t, m); }
static BOOL MGLPPCTransportWait(MGLPPCTransport *t, MGLPPCMessage *m) { (void)t; (void)m; return TRUE; }
static void InitializeFrontendContext(struct MiniGLR200Context *c) {
    c->deviceEpoch = nativeCreates;
    for (ULONG i = 0; i < 2; ++i) c->submitSlots[i].records = buildRecords[i];
}
static void SetError(struct MiniGLR200Context *c, ULONG e) { assert(c); error = e; }
static BOOL FlushScreenVertices(struct MiniGLR200Context *c) { (void)c; return TRUE; }
static BOOL SubmitAndWait(struct MiniGLR200Context *c);
'''
for signature in ("static BOOL PlatformMailboxReady(", "static BOOL PlatformMailboxWait(",
                  "static BOOL PlatformCall(", "static void CopyRecordToRingSlot(",
                  "BOOL R200PPCPlatformOpen(", "BOOL R200PPCPlatformPublishSlot(",
                  "BOOL R200PPCPlatformWaitSlot(", "BOOL R200PPCPlatformBarrier(",
                  "void *R200PPCDispatchCreateContext(", "void R200PPCDispatchDeleteContext(",
                  "void R200PPCPlatformClose(", "static BOOL FrontendTextureBarrier("):
    body = function(F, signature)
    if "--regress-crc" in sys.argv and signature == "void *R200PPCDispatchCreateContext(":
        body = body.replace("FrontendPlatform.crcArmed = TRUE;", "")
    code += body
code += r'''
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
    proofContext.submitSlots[0].recordCount = 11;
}
static void texture(ULONG cmd) {
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = 1; /* same texture name in both contexts */
    assert(PlatformCall(cmd, FrontendPlatform.contextId));
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
                assert(R200PPCPlatformBarrier(&proofContext, TRUE)); poll();
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
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="r200-lifecycle-", dir="/tmp/opencode") as directory:
    source = Path(directory) / "test.c"
    binary = Path(directory) / "test"
    source.write_text(code)
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-variable", "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
                    "-fno-pie", "-no-pie", "-fsanitize=address,undefined",
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)] + (["expect-fault"] if "--regress-crc" in sys.argv else []), check=True)
