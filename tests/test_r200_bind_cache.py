"""CPU-only bind-elision regressions using extracted production C functions.

python3 tests/test_r200_bind_cache.py -v

The frontend, local bind/validation/error helpers, mailbox and PlatformCall
paths are real code. Host replies, cache operations, allocation and geometry
submission are deterministic stubs, not GPU/coherency validation. Lifecycle
functions run through Exec registration; their early invalidation order is
also checked at source level. ASan/UBSan binaries live only in /tmp/opencode.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / "backend_r200/library"
FRONTEND = (LIBRARY / "minigl_r200_warpos_compile_probe.c").read_text()
FRONTEND = FRONTEND[FRONTEND.index("typedef struct R200PPCFrontendPlatform"):]
PROTOCOL = (ROOT / "backend_r200/include/libraries/minigl_ppc_protocol.h").read_text()
TRANSPORT = (ROOT / "backend_r200/include/libraries/minigl_ppc_transport.h").read_text()
EMITTER = (ROOT / "backend_r200/include/libraries/minigl_ppc_cp_emit.h").read_text()
INTERNAL = (LIBRARY / "minigl_r200_internal.h").read_text()


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def function(text, name):
    definitions = list(re.finditer(
        rf"^(?:static\s+)?(?:struct\s+\w+\s*\*|void\s*\*|void|BOOL|GLenum|ULONG)"
        rf"\s*\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", text, re.M))
    if len(definitions) != 1:
        raise AssertionError(f"Expected one definition of {name}, got {len(definitions)}")
    definition = definitions[0]
    depth = 0
    tokens = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]'
    for token in re.finditer(tokens, text[definition.end() - 1:], re.S):
        depth += (token[0] == "{") - (token[0] == "}")
        if depth == 0:
            return text[definition.start():definition.end() - 1 + token.end()] + "\n"
    raise AssertionError(f"Unterminated function {name}")


STUBS = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t ULONG, GLuint, GLenum;
typedef int32_t LONG, GLint, GLsizei;
typedef unsigned char UBYTE;
typedef void GLvoid;
typedef void *APTR;
typedef const UBYTE *CONST_STRPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MGLPPC_PROFILE 0
#define MGLPPC_CLIENT_TRACE 0
#define R200_PPC_FRONTEND 1
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define GVF_GLOBAL_ONLY 0x100
#define RADEON3D_SEGMENT_V1_SIZE 24
#define RADEON3D_SEGMENT_VERSION 1
#define R200PPCTraceUL(...) ((void)0)
#define R200PPCTraceUL2(...) ((void)0)
#define R200PPCTraceStr(...) ((void)0)
#define R200PPCTraceOpen() ((void)0)
#define R200PPCTraceClose() ((void)0)
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); } } while (0)
/* Only equality/type classification matters to these CPU stubs. */
enum { GL_NO_ERROR = 0, GL_INVALID_ENUM = 80, GL_INVALID_OPERATION = 82,
    GL_INVALID_VALUE = 83, GL_OUT_OF_MEMORY = 123, GL_TEXTURE_2D = 170,
    GL_NEAREST = 120, GL_REPEAT = 160, GL_RGB = 151, GL_RGBA = 155,
    GL_ALPHA = 10, GL_LUMINANCE = 11, GL_LUMINANCE_ALPHA = 12,
    GL_UNSIGNED_BYTE = 200, GL_UNSIGNED_SHORT = 201,
    MGL_UNSIGNED_SHORT_5_6_5 = 300, MGL_UNSIGNED_SHORT_4_4_4_4 = 301,
    MGL_UBYTE_BGRA = 302, MGL_UBYTE_ARGB = 303 };
enum { A = 1, B = 2, EPOCH = 7, CONTEXT = 19 };
struct Message { UBYTE pad[18]; unsigned short mn_Length; };
struct Window { ULONG IDCMPFlags; };
struct Radeon3DSurface { APTR Handle; };
struct Radeon3DSegment { ULONG Size, Version; APTR CpuAddress; ULONG Bytes, Id, GpuAddress; };
'''
STUBS += section(PROTOCOL, "#define MGLPPC_HOST_PORT_NAME", "/* Exec sees this structure").replace(
    "#ifndef MGLPPC_TEXTURE_VALIDATION_TEST", "")
STUBS += section(TRANSPORT, "typedef struct MGLPPCTransport", "BOOL MGLPPCTransportOpen")
STUBS += EMITTER.replace("#include <exec/types.h>", "")
for constant in ("MAX_TEXTURES", "MAX_TEXTURE_LEVELS", "SUBMIT_SLOT_COUNT"):
    definition = re.search(rf"^#define {constant}\s+.*$", INTERNAL, re.M)
    if definition is None:
        raise AssertionError(f"Missing production constant {constant}")
    STUBS += definition[0] + "\n"
STUBS += section(INTERNAL, "struct WorstFrameSlot {", "struct ArrayState {")
STUBS += section(INTERNAL, "struct TextureObject {", "/* Radeon3DExecute consumes")
STUBS += section(FRONTEND, "typedef struct R200PPCFrontendPlatform", "#define MGLPPC_REPLAY_BATCHES_PER_FRAME")
STUBS += r'''
struct SubmitSlot { ULONG records[8], recordCount, drawCount, segmentCursor; };
struct MiniGLR200Context {
    GLenum error;
    BOOL insideBegin, perfEnabled;
    ULONG activeTexture, boundTexture[2], semanticStateSerial, pendingSemanticStateSerial;
    ULONG deviceEpoch, textureUseSerial, batchSerial;
    struct PerfMetrics perf;
    struct TextureObject textures[MAX_TEXTURES];
    ULONG unpackAlignment, unpackRowLength, unpackSkipRows, unpackSkipPixels;
    struct SubmitSlot submitSlots[SUBMIT_SLOT_COUNT];
    struct Window *window;
    ULONG baseIDCMP, width, height, pixelDepth, radeonCaps, batchDwords;
    BOOL compactTclVertex, streamSegments;
    struct Radeon3DSurface surfaces[1], depthSurface;
    struct { struct Radeon3DSegment info; BOOL valid; } segment;
    struct Radeon3DSegment segmentPool[4];
    ULONG segmentPoolCount, segmentPoolNext;
};
static struct MiniGLR200Context proofContext, *R200PPCContext;
static MGLPPCMailboxV1 mailbox;
static MGLPPCRingControlV1 ring;
static UBYTE staging[64] __attribute__((aligned(32))), pixels[16];
static struct Window window;
static ULONG PlatformClockRate = 10000, clockTicks, hostBound, semanticBound;
static ULONG nextContext, nextEpoch, failCommand, failOccurrence;
static BOOL failTransport, holdAck, failFlush, failSubmit, failBarrier, failWait, stagingLive;
static BOOL generated[MAX_TEXTURES];
/* Shadow helpers extracted later; prototypes prevent implicit-decl noise
 * when the slice pulls functions out of production order. */
#define R200_SHADOW_TOTAL_MAX (2UL * 1024UL * 1024UL)
static void FrontendShadowFree(struct TextureObject *texture);
static BOOL FrontendShadowEnsure(struct MiniGLR200Context *context,
                                 struct TextureObject *texture,
                                 ULONG width, ULONG height);
static void FrontendShadowPatch(struct TextureObject *texture,
                                const UBYTE *source, GLsizei width,
                                GLsizei height, GLenum format, GLenum type,
                                ULONG sourceBytes, ULONG sourceStride,
                                GLint xoffset, GLint yoffset);
static struct Observations {
    ULONG command[MGLPPC_CMD_CP_SURFACE + 1], total, exec, headerReads, ackReads, flushes, clocks, delays;
    ULONG geometryFlushes, submits, barriers, waits, allocations, frees, yields;
} observed;
static void R200PPCConsolePrintf(const char *format, ...) { (void)format; }
static void MGLPPCIBTrace(const char *path, const char *text) { (void)path; (void)text; }
static LONG GetVar(CONST_STRPTR name, UBYTE *value, LONG size, LONG flags) {
    (void)name; (void)value; (void)size; CHECK(flags == GVF_GLOBAL_ONLY);
    return -1; /* Bind-cache cases do not opt into CP or startup smoke. */
}
static ULONG PerfClock(void) { ++observed.clocks; return clockTicks += 100; }
static void Delay(LONG ticks) { CHECK(ticks == 1); ++observed.delays; }
/* PlatformMailboxWait now yields through the bounded ring policy. */
#define MGLPPC_RING_YIELD_SPINS 131072UL
static void MGLPPCYield(void) { ++observed.yields; }
static void InitializeFrontendContext(struct MiniGLR200Context *c) {
    c->deviceEpoch = EPOCH; c->unpackAlignment = 4; c->perfEnabled = TRUE;
    c->semanticStateSerial = 11; c->pendingSemanticStateSerial = 9;
    c->textureUseSerial = 17; c->batchSerial = 23;
    for (ULONG i = 0; i < MAX_TEXTURES; ++i) {
        c->textures[i].minFilter = c->textures[i].magFilter = GL_NEAREST;
        c->textures[i].wrapS = c->textures[i].wrapT = GL_REPEAT;
    }
}
static BOOL FlushScreenVertices(struct MiniGLR200Context *c) {
    CHECK(c == &proofContext); ++observed.geometryFlushes; return !failFlush;
}
/* The frontend TexSubImage shadow path publishes its slot with Submit
 * (async) rather than SubmitAndWait before the direct recommit. */
static BOOL Submit(struct MiniGLR200Context *c) {
    CHECK(c == &proofContext); ++observed.submits; return !failSubmit;
}
static BOOL FrontendDirectTextureUpload(struct MiniGLR200Context *context,
                                        ULONG width, ULONG height,
                                        GLenum format, GLenum type,
                                        const UBYTE *source,
                                        ULONG sourceStride) {
    (void)context; (void)width; (void)height; (void)format; (void)type;
    (void)source; (void)sourceStride;
    return TRUE;
}
static BOOL SubmitAndWait(struct MiniGLR200Context *c) {
    CHECK(c == &proofContext); ++observed.submits; return !failSubmit;
}
static BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *c, BOOL present) {
    CHECK(c == &proofContext && !present); ++observed.barriers; return !failBarrier;
}
static BOOL R200PPCPlatformWaitSlot(struct MiniGLR200Context *c, ULONG index) {
    CHECK(c == &proofContext && index < SUBMIT_SLOT_COUNT); ++observed.waits; return !failWait;
}
static void MoveWindow(struct Window *w, LONG x, LONG y) { CHECK(w == &window); (void)x; (void)y; }
static APTR AllocVec(ULONG bytes, ULONG flags) {
    CHECK(bytes <= sizeof(staging) && flags == MEMF_PUBLIC && !stagingLive);
    ++observed.allocations; stagingLive = TRUE; return staging;
}
static void FreeVec(APTR address) {
    CHECK(address == staging && stagingLive); ++observed.frees; stagingLive = FALSE;
}
static void ZeroMemory(APTR address, ULONG bytes) { memset(address, 0, bytes); }

/* Acknowledged native operations may mutate before failure. Successful
 * temporary upload binding restores the last semantic bind. No GPU is run. */
static void hostCommand(MGLPPCMessage *m) {
    ULONG args[8]; memcpy(args, m->value, sizeof(args));
    CHECK(m->command <= MGLPPC_CMD_CP_SURFACE); ++observed.total; ++observed.command[m->command];
    BOOL failed = m->command == failCommand && observed.command[m->command] == failOccurrence;
    m->status = failed ? MGLPPC_STATUS_GL_ERROR : MGLPPC_STATUS_OK;
    memset(m->value, 0, sizeof(m->value));
    if (failed) m->value[0] = GL_OUT_OF_MEMORY;
    switch (m->command) {
    case MGLPPC_CMD_BIND_TEXTURE:
        CHECK(!FrontendPlatform.nativeBindingKnown); /* invalidate before publication */
        CHECK(m->contextId == FrontendPlatform.contextId && args[0] == GL_TEXTURE_2D);
        CHECK(args[1] < MAX_TEXTURES); hostBound = args[1];
        if (!failed) { semanticBound = hostBound; generated[hostBound] = TRUE; }
        break;
    case MGLPPC_CMD_GEN_TEXTURES:
        CHECK(!failed && args[0] <= 8);
        for (ULONG name = 1, out = 0; out < args[0]; ++name) {
            CHECK(name < MAX_TEXTURES);
            if (!generated[name]) { generated[name] = TRUE; m->value[out++] = name; }
        }
        break;
    case MGLPPC_CMD_DELETE_TEXTURES:
        CHECK(!FrontendPlatform.nativeBindingKnown && args[0] <= 7);
        for (ULONG i = 1; i <= args[0]; ++i) {
            ULONG name = args[i];
            if (name && name < MAX_TEXTURES) {
                generated[name] = FALSE;
                if (hostBound == name) hostBound = 0;
                if (semanticBound == name) semanticBound = 0;
            }
        }
        break;
    case MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED:
        CHECK(args[0] && args[0] < MAX_TEXTURES); hostBound = args[0];
        if (!failed) {
            struct TextureObject *t = &proofContext.textures[args[0]];
            hostBound = semanticBound;
            m->value[1] = (ULONG)(uintptr_t)pixels;
            m->value[2] = t->width; m->value[3] = t->height;
            m->value[4] = t->definedLevels; m->value[5] = t->levelCount;
            m->value[6] = t->contentSerial + 1; m->value[7] = proofContext.deviceEpoch;
        }
        break;
    case MGLPPC_CMD_CREATE_CONTEXT:
        CHECK(!FrontendPlatform.nativeBindingKnown);
        if (!failed) {
            m->contextId = ++nextContext; m->value[0] = (ULONG)(uintptr_t)&window;
            m->value[1] = args[0]; m->value[2] = args[1];
            m->value[3] = 1; m->value[4] = 2;
            hostBound = semanticBound = 0; memset(generated, 0, sizeof(generated));
        }
        break;
    case MGLPPC_CMD_REGISTER_SEMANTIC_SLOTS:
        CHECK(!FrontendPlatform.nativeBindingKnown); m->value[0] = ++nextEpoch; break;
    case MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS:
    case MGLPPC_CMD_DESTROY_CONTEXT:
        CHECK(!FrontendPlatform.nativeBindingKnown); break;
    default: break;
    }
}
static BOOL MGLPPCTransportCall(MGLPPCTransport *t, MGLPPCMessage *m) {
    CHECK(t == &FrontendPlatform.transport);
    if (t->pendingMessage) return FALSE;
    ++observed.exec;
    if (failTransport) return FALSE;
    hostCommand(m); return TRUE;
}
static void MGLPPCInvalidateLine32(APTR address) {
    if (address == &mailbox.ackSerial) ++observed.ackReads;
    else if (address == &mailbox) ++observed.headerReads;
    else CHECK(address == &mailbox.ackValue[0]);
}
static void acknowledge(void) {
    MGLPPCMessage m; memset(&m, 0, sizeof(m));
    m.command = mailbox.command; m.contextId = mailbox.contextId;
    memcpy(m.value, mailbox.reqValue, sizeof(m.value)); hostCommand(&m);
    memcpy(mailbox.ackValue, m.value, sizeof(m.value));
    mailbox.status = (ULONG)m.status; mailbox.ackSerial = mailbox.reqSerial;
}
static void MGLPPCFlushDataRange(APTR address, ULONG bytes) {
    ++observed.flushes;
    CHECK((address == staging && bytes <= sizeof(staging)) ||
          ((address == &mailbox.reqValue[0] || address == &mailbox.reqSerial) && bytes == 32));
    if (address == &mailbox.reqSerial && mailbox.reqSerial != mailbox.ackSerial && !holdAck)
        acknowledge();
}
'''

for filename, names in (
    ("minigl_r200.c", ("CurrentContext", "SetError", "BumpSemanticStateSerial")),
    ("r200_state.c", ("StateChangeAllowed",)),
    ("r200_surface.c", ("Lib_glGetError",)),
    ("r200_texture.c", ("Lib_glBindTexture",)),
):
    text = (LIBRARY / filename).read_text()
    STUBS += "".join(function(text, name) for name in names)

FUNCTIONS = (
    "PlatformMailboxReady", "PlatformMailboxWait", "PlatformCallFail", "PlatformCall",
    "R200PPCPlatformResetClientDefaults", "R200PPCIbSmokeAfterRegistration", "R200PPCDispatchCreateContext",
    "R200PPCDispatchDeleteContext", "FrontendTextureBarrier", "FrontendSetTransportError",
    "R200PPCDispatchGenTextures", "R200PPCDispatchBindTexture", "FrontendCopyTextureBytes",
    "FrontendEnsureTextureUploadBuffer", "FrontendMirrorTextureReply",
    "FrontendWireSourceFormat", "FrontendWireSourceType",
    "FrontendSourceToBGRA32", "FrontendStageTextureBGRA",
    "FrontendShadowFree", "FrontendShadowEnsure", "FrontendShadowPatch",
    "R200PPCDispatchTexSubImage2D", "R200PPCDispatchDeleteTextures",
)

CASES = r'''
static void reset(BOOL useMailbox) {
    memset(&proofContext, 0, sizeof(proofContext)); memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
    memset(&observed, 0, sizeof(observed)); memset(&mailbox, 0, sizeof(mailbox));
    memset(&ring, 0, sizeof(ring)); memset(generated, 0, sizeof(generated));
    R200PPCContext = &proofContext; InitializeFrontendContext(&proofContext);
    FrontendPlatform.opened = FrontendPlatform.registered = TRUE;
    FrontendPlatform.contextId = CONTEXT; FrontendPlatform.epoch = EPOCH;
    FrontendPlatform.peerProtocol = MGLPPC_PROTOCOL_VERSION;
    FrontendPlatform.capabilities = MGLPPC_CAP_TEXTURE_SUBIMAGE_V1;
    FrontendPlatform.mailbox = useMailbox ? &mailbox : NULL;
    FrontendPlatform.ring = useMailbox ? &ring : NULL;
    mailbox.magic = MGLPPC_MAILBOX_MAGIC; mailbox.version = MGLPPC_MAILBOX_VERSION; mailbox.epoch = EPOCH;
    ring.head = 5; ring.completed = 3; ring.retired = 2;
    FrontendPlatform.generation[0] = 41; FrontendPlatform.generation[1] = 43;
    FrontendPlatform.segmentLastEntry[0] = 5; FrontendPlatform.ringPostSerial = 5;
    proofContext.submitSlots[0].recordCount = 7; proofContext.submitSlots[0].drawCount = 2;
    proofContext.submitSlots[0].records[0] = 0xabcdef; proofContext.submitSlots[1].segmentCursor = 128;
    nextContext = CONTEXT; nextEpoch = EPOCH; hostBound = semanticBound = clockTicks = 0;
    failCommand = 0; failOccurrence = 1;
    failTransport = holdAck = failFlush = failSubmit = failBarrier = failWait = stagingLive = FALSE;
}
static void bind(GLuint name) { R200PPCDispatchBindTexture(GL_TEXTURE_2D, name); }
static void established(GLuint name) {
    bind(name); CHECK(Lib_glGetError() == GL_NO_ERROR);
    CHECK(FrontendPlatform.nativeBindingKnown && FrontendPlatform.nativeBoundTexture == name);
    CHECK(proofContext.boundTexture[0] == name && hostBound == name);
}
static void repeat(ULONG count) {
    struct MiniGLR200Context saved; R200PPCFrontendPlatform platform;
    MGLPPCMailboxV1 box; MGLPPCRingControlV1 control; struct Observations expected;
    memcpy(&saved, &proofContext, sizeof(saved)); memcpy(&platform, &FrontendPlatform, sizeof(platform));
    memcpy(&box, &mailbox, sizeof(box)); memcpy(&control, &ring, sizeof(control));
    memcpy(&expected, &observed, sizeof(expected));
    if (saved.perfEnabled) { saved.perf.stateCalls += count; saved.perf.stateNoops += count; }
    if (FrontendPlatform.mailbox) expected.headerReads += count;
    for (ULONG i = 0; i < count; ++i) bind(proofContext.boundTexture[0]);
    CHECK(!memcmp(&proofContext, &saved, sizeof(saved)));
    CHECK(!memcmp(&FrontendPlatform, &platform, sizeof(platform)));
    CHECK(!memcmp(&mailbox, &box, sizeof(box)) && !memcmp(&ring, &control, sizeof(control)));
    CHECK(!memcmp(&observed, &expected, sizeof(expected)));
}
static void define(GLuint name) {
    struct TextureObject *t = &proofContext.textures[name];
    t->generated = TRUE; t->width = t->height = 2; t->levelCount = t->definedLevels = 1;
    t->resident = TRUE; t->surface.Handle = pixels; t->residentDeviceEpoch = EPOCH;
    t->imageGeneration = t->residentGeneration = 13; t->surfaceGeneration = 29;
    t->contentSerial = 31; t->dirtyLevelMask = 1; t->queuedBatchSerial = 23;
}
static void subimage(void) {
    R200PPCDispatchTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}
int main(int argc, char **argv) {
    CHECK(argc == 3); BOOL useMailbox = !strcmp(argv[2], "mailbox");
    CHECK((uintptr_t)pixels + sizeof(pixels) < UINT32_MAX);
    CHECK((uintptr_t)staging + sizeof(staging) < UINT32_MAX);
    CHECK((uintptr_t)&window < UINT32_MAX); reset(useMailbox);
    if (!strcmp(argv[1], "first-repeat")) {
        for (GLuint name = 0; name <= A; ++name) {
            reset(useMailbox); established(name);
            CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 1);
            CHECK(useMailbox ? observed.exec == 0 && mailbox.reqSerial == 1 : observed.exec == 1);
            CHECK(!proofContext.textures[name].definedLevels && !proofContext.textures[name].resident);
            if (!name) CHECK(!proofContext.textures[0].generated);
            repeat(1000); proofContext.perfEnabled = FALSE; repeat(1000);
            CHECK(observed.total == 1 && !observed.submits && !observed.geometryFlushes);
        }
    } else if (!strcmp(argv[1], "generated")) {
        GLuint name; R200PPCDispatchGenTextures(1, &name); CHECK(name == A);
        CHECK(proofContext.textures[A].generated && !FrontendPlatform.nativeBindingKnown);
        established(A); repeat(64);
        define(A); repeat(64); /* Image/residency/generation are NOT bind-cache keys. */
        proofContext.textures[A].resident = FALSE; proofContext.textures[A].surface.Handle = NULL;
        proofContext.textures[A].residentGeneration = 0; repeat(64);
        CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 1);
        /* A stale ungenerated local name must neither skip nor publish knowledge.
         * The real local binder's same-name path does not implicitly generate it. */
        proofContext.textures[A].generated = FALSE;
        bind(A); CHECK(!FrontendPlatform.nativeBindingKnown && !proofContext.textures[A].generated);
        bind(A); CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 3);
        established(B); established(A); CHECK(proofContext.textures[A].generated); repeat(8);
    } else if (!strcmp(argv[1], "switch")) {
        established(A); established(B); established(A);
        CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 3); repeat(32);
        /* Native knowledge alone cannot bypass a differing local mirror. */
        proofContext.boundTexture[0] = B; established(A);
        CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 4);
        /* Local equality alone cannot bypass differing native knowledge. */
        FrontendPlatform.nativeBoundTexture = B; established(A);
        CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 5); repeat(32);
    } else if (!strcmp(argv[1], "validation")) {
        for (ULONG invalid = 0; invalid < 6; ++invalid) {
            reset(useMailbox); established(A); ULONG noops = proofContext.perf.stateNoops;
            if (invalid == 0) R200PPCDispatchBindTexture(GL_RGB, A);
            if (invalid == 1) bind(MAX_TEXTURES);
            if (invalid == 2) bind(UINT32_MAX);
            if (invalid == 3) { proofContext.activeTexture = 1; bind(A); proofContext.activeTexture = 0; }
            if (invalid == 4) { proofContext.insideBegin = TRUE; bind(A); proofContext.insideBegin = FALSE; }
            if (invalid == 5) { R200PPCContext = NULL; bind(A); R200PPCContext = &proofContext; }
            GLenum expected = invalid == 0 ? GL_INVALID_ENUM : invalid < 3 ? GL_INVALID_VALUE :
                invalid < 5 ? GL_INVALID_OPERATION : GL_NO_ERROR;
            CHECK(Lib_glGetError() == expected && proofContext.perf.stateNoops == noops);
            CHECK(observed.total == 1 && FrontendPlatform.nativeBindingKnown);
            CHECK(proofContext.boundTexture[0] == A && hostBound == A); repeat(8);
        }
    } else if (!strcmp(argv[1], "failed-bind")) {
        established(A); failCommand = MGLPPC_CMD_BIND_TEXTURE; failOccurrence = 2;
        bind(B); CHECK(Lib_glGetError() == GL_OUT_OF_MEMORY);
        CHECK(hostBound == B && proofContext.boundTexture[0] == A && !FrontendPlatform.nativeBindingKnown);
        CHECK(!proofContext.textures[B].generated);
        failCommand = 0; established(A); CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 3); repeat(32);
    } else if (!strcmp(argv[1], "call-failure")) {
        /* The common failure funnel must invalidate for EVERY command, not just BIND. */
        for (ULONG command = 1; command <= MGLPPC_CMD_CP_SURFACE; ++command) {
            reset(useMailbox); established(A);
            CHECK(!PlatformCallFail(command, "injected failure"));
            CHECK(!FrontendPlatform.nativeBindingKnown); established(A);
            CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 2);
        }
        reset(useMailbox); established(A); failCommand = MGLPPC_CMD_GET_INTEGER;
        CHECK(!PlatformCall(MGLPPC_CMD_GET_INTEGER, FrontendPlatform.contextId));
        CHECK(!FrontendPlatform.nativeBindingKnown); failCommand = 0; established(A);
        CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 2);
        if (!useMailbox) {
            failTransport = TRUE; CHECK(!PlatformCall(MGLPPC_CMD_GET_INTEGER, FrontendPlatform.contextId));
            CHECK(!FrontendPlatform.nativeBindingKnown); failTransport = FALSE; established(A);
        } else {
            CHECK(!PlatformCall(MGLPPC_CMD_BIND_TEXTURE, FrontendPlatform.contextId + 1));
            CHECK(!FrontendPlatform.nativeBindingKnown); established(A);
        }
    } else if (!strcmp(argv[1], "subimage")) {
        established(A); define(A); hostBound = B; subimage(); /* replay left another native binding */
        CHECK(Lib_glGetError() == GL_NO_ERROR && hostBound == A && FrontendPlatform.nativeBindingKnown);
        CHECK(proofContext.textures[A].imageGeneration == 14 && proofContext.textures[A].resident);
        repeat(32); CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 1);
        failCommand = MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED; failOccurrence = 2; subimage();
        CHECK(Lib_glGetError() == GL_OUT_OF_MEMORY && !FrontendPlatform.nativeBindingKnown);
        CHECK(!proofContext.textures[A].resident && !proofContext.textures[A].surface.Handle);
        CHECK(!proofContext.textures[A].residentGeneration && proofContext.textures[A].imageGeneration == 14);
        failCommand = 0; established(A); CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 2); repeat(32);
        subimage(); CHECK(FrontendPlatform.nativeBindingKnown && proofContext.textures[A].resident); repeat(8);
        CHECK(observed.submits == 3 && observed.barriers == 3 && observed.geometryFlushes == 3);
    } else if (!strcmp(argv[1], "gates")) {
        for (ULONG gate = 0; gate < 6; ++gate) {
            reset(useMailbox); established(A); ULONG noops = proofContext.perf.stateNoops;
            if (gate == 0) FrontendPlatform.opened = FALSE;
            if (gate == 1) FrontendPlatform.registered = FALSE;
            if (gate == 2) FrontendPlatform.contextId = 0;
            if (gate == 3) FrontendPlatform.transport.pendingMessage = &FrontendPlatform.execute[0];
            if (gate == 4) FrontendPlatform.mailboxPoisoned = TRUE;
            if (gate == 5) FrontendPlatform.mailboxAbandoned = TRUE;
            bind(A); CHECK(Lib_glGetError() == GL_INVALID_OPERATION);
            CHECK(!FrontendPlatform.nativeBindingKnown && observed.total == 1);
            CHECK(proofContext.perf.stateNoops == noops && proofContext.boundTexture[0] == A);
            FrontendPlatform.opened = FrontendPlatform.registered = TRUE;
            FrontendPlatform.contextId = CONTEXT; FrontendPlatform.transport.pendingMessage = NULL;
            FrontendPlatform.mailboxPoisoned = FrontendPlatform.mailboxAbandoned = FALSE;
            established(A); CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 2);
        }
    } else if (!strcmp(argv[1], "header")) {
        CHECK(useMailbox);
        for (ULONG cached = 0; cached < 2; ++cached)
        for (ULONG field = 0; field < 3; ++field) {
            reset(TRUE); established(A);
            FrontendPlatform.nativeBindingKnown = cached;
            if (field == 0) mailbox.magic ^= 1;
            if (field == 1) ++mailbox.version;
            if (field == 2) ++mailbox.epoch;
            ULONG serial = mailbox.reqSerial, reads = observed.headerReads, noops = proofContext.perf.stateNoops;
            bind(A); CHECK(Lib_glGetError() == GL_INVALID_OPERATION);
            CHECK(!FrontendPlatform.nativeBindingKnown && FrontendPlatform.mailboxPoisoned && FrontendPlatform.mailboxAbandoned);
            CHECK(observed.total == 1 && mailbox.reqSerial == serial && observed.headerReads == reads + 1);
            CHECK(proofContext.perf.stateNoops == noops && !observed.exec);
            mailbox.magic = MGLPPC_MAILBOX_MAGIC; mailbox.version = MGLPPC_MAILBOX_VERSION; mailbox.epoch = EPOCH;
            bind(A); CHECK(Lib_glGetError() == GL_INVALID_OPERATION && observed.total == 1);
        }
    } else if (!strcmp(argv[1], "outstanding")) {
        CHECK(useMailbox); established(A);
        FrontendPlatform.mailboxOutstanding = TRUE; ++FrontendPlatform.mailboxSerial;
        bind(A); CHECK(Lib_glGetError() == GL_INVALID_OPERATION && !FrontendPlatform.nativeBindingKnown);
        CHECK(FrontendPlatform.mailboxOutstanding && observed.total == 1);
        mailbox.reqSerial = mailbox.ackSerial = FrontendPlatform.mailboxSerial;
        established(A); CHECK(!FrontendPlatform.mailboxOutstanding && observed.total == 2);
        /* A completed, non-abandoned reference is released by the real Ready helper. */
        FrontendPlatform.mailboxOutstanding = TRUE;
        bind(A); CHECK(!FrontendPlatform.mailboxOutstanding && observed.total == 2 && FrontendPlatform.nativeBindingKnown);
        repeat(8);
        for (ULONG field = 0; field < 3; ++field) {
            reset(TRUE); established(A); FrontendPlatform.mailboxOutstanding = TRUE;
            FrontendPlatform.mailboxPoisoned = FrontendPlatform.mailboxAbandoned = TRUE;
            if (field == 0) mailbox.magic ^= 1;
            if (field == 1) ++mailbox.version;
            if (field == 2) ++mailbox.epoch;
            /* A matching ack serial with a bad header cannot release ownership. */
            CHECK(!PlatformMailboxReady() && FrontendPlatform.mailboxOutstanding && FrontendPlatform.mailboxPoisoned);
            bind(A); CHECK(Lib_glGetError() == GL_INVALID_OPERATION && !FrontendPlatform.nativeBindingKnown);
            CHECK(observed.total == 1 && FrontendPlatform.mailboxOutstanding);
        }
        reset(TRUE); established(A); FrontendPlatform.mailboxOutstanding = TRUE;
        FrontendPlatform.mailbox = NULL; bind(A);
        CHECK(Lib_glGetError() == GL_INVALID_OPERATION && !FrontendPlatform.nativeBindingKnown);
        CHECK(observed.total == 1 && FrontendPlatform.mailboxOutstanding);
    } else if (!strcmp(argv[1], "late-ack")) {
        CHECK(useMailbox); established(A); holdAck = TRUE; bind(B);
        CHECK(Lib_glGetError() == GL_INVALID_OPERATION && !FrontendPlatform.nativeBindingKnown);
        CHECK(FrontendPlatform.mailboxOutstanding && FrontendPlatform.mailboxPoisoned && FrontendPlatform.mailboxAbandoned);
        CHECK(observed.total == 1 && observed.exec == 0 && mailbox.reqSerial == 2);
        bind(A); CHECK(Lib_glGetError() == GL_INVALID_OPERATION && observed.total == 1);
        holdAck = FALSE; acknowledge(); CHECK(hostBound == B);
        CHECK(PlatformMailboxReady() && !FrontendPlatform.mailboxOutstanding && !FrontendPlatform.mailboxPoisoned);
        CHECK(FrontendPlatform.mailboxAbandoned && !FrontendPlatform.nativeBindingKnown);
        bind(A); CHECK(Lib_glGetError() == GL_INVALID_OPERATION && observed.total == 2 && !observed.exec);
        /* Even artificially retained knowledge must not make a late ack drawable. */
        FrontendPlatform.nativeBindingKnown = TRUE; FrontendPlatform.nativeBoundTexture = A;
        bind(A); CHECK(Lib_glGetError() == GL_INVALID_OPERATION && !FrontendPlatform.nativeBindingKnown);
        CHECK(observed.total == 2 && mailbox.reqSerial == 2 && proofContext.boundTexture[0] == A);
    } else if (!strcmp(argv[1], "stale-error")) {
        SetError(&proofContext, GL_INVALID_ENUM); bind(A);
        CHECK(FrontendPlatform.nativeBindingKnown && proofContext.error == GL_INVALID_ENUM);
        FrontendPlatform.control.status = MGLPPC_STATUS_GL_ERROR;
        FrontendPlatform.control.value[0] = GL_OUT_OF_MEMORY;
        repeat(16); CHECK(Lib_glGetError() == GL_INVALID_ENUM); repeat(16); CHECK(!Lib_glGetError());
        FrontendPlatform.mailboxPoisoned = TRUE; bind(A);
        CHECK(Lib_glGetError() == GL_INVALID_OPERATION); /* not the stale control GL error */
        CHECK(!FrontendPlatform.nativeBindingKnown); FrontendPlatform.mailboxPoisoned = FALSE;
        established(A); CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 2);
        SetError(&proofContext, GL_INVALID_VALUE); failCommand = MGLPPC_CMD_BIND_TEXTURE; failOccurrence = 3;
        bind(B); CHECK(!FrontendPlatform.nativeBindingKnown && Lib_glGetError() == GL_INVALID_VALUE);
    } else if (!strcmp(argv[1], "delete")) {
        GLuint names[] = {0, MAX_TEXTURES, B}; established(A);
        R200PPCDispatchDeleteTextures(0, NULL); repeat(4);
        R200PPCDispatchDeleteTextures(-1, names); CHECK(Lib_glGetError() == GL_INVALID_VALUE); repeat(4);
        R200PPCDispatchDeleteTextures(1, NULL); CHECK(Lib_glGetError() == GL_INVALID_VALUE); repeat(4);
        proofContext.insideBegin = TRUE; R200PPCDispatchDeleteTextures(1, names);
        proofContext.insideBegin = FALSE; CHECK(Lib_glGetError() == GL_INVALID_OPERATION); repeat(4);
        for (ULONG i = 0; i < 3; ++i) {
            R200PPCDispatchDeleteTextures(1, &names[i]); CHECK(!FrontendPlatform.nativeBindingKnown);
            established(A);
        }
        GLuint name = A; define(A); R200PPCDispatchDeleteTextures(1, &name);
        CHECK(!FrontendPlatform.nativeBindingKnown && !proofContext.textures[A].generated && !proofContext.boundTexture[0]);
        established(0); repeat(8); established(A); CHECK(proofContext.textures[A].generated);
        for (ULONG failure = 0; failure < 3; ++failure) {
            failFlush = failure == 0; failSubmit = failure == 1; failBarrier = failure == 2;
            ULONG before = observed.command[MGLPPC_CMD_DELETE_TEXTURES];
            R200PPCDispatchDeleteTextures(1, &name); CHECK(!FrontendPlatform.nativeBindingKnown);
            CHECK(Lib_glGetError() == GL_INVALID_OPERATION && observed.command[MGLPPC_CMD_DELETE_TEXTURES] == before);
            failFlush = failSubmit = failBarrier = FALSE; established(A);
        }
        /* A failed single chunk may already have removed the native binding. */
        failCommand = MGLPPC_CMD_DELETE_TEXTURES; failOccurrence = observed.command[failCommand] + 1;
        ULONG binds = observed.command[MGLPPC_CMD_BIND_TEXTURE];
        R200PPCDispatchDeleteTextures(1, &name);
        CHECK(Lib_glGetError() == GL_OUT_OF_MEMORY && !FrontendPlatform.nativeBindingKnown);
        CHECK(hostBound == 0 && proofContext.boundTexture[0] == A);
        failCommand = 0; established(A); CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == binds + 1);
    } else if (!strcmp(argv[1], "partial-delete")) {
        GLuint names[8]; established(A); R200PPCDispatchGenTextures(8, names);
        for (ULONG i = 0; i < 8; ++i) { CHECK(names[i] == i + 2); define(names[i]); }
        failCommand = MGLPPC_CMD_DELETE_TEXTURES; failOccurrence = 2;
        R200PPCDispatchDeleteTextures(8, names);
        CHECK(Lib_glGetError() == GL_OUT_OF_MEMORY && !FrontendPlatform.nativeBindingKnown);
        CHECK(observed.command[MGLPPC_CMD_DELETE_TEXTURES] == 2 && proofContext.boundTexture[0] == A);
        for (ULONG i = 0; i < 7; ++i) CHECK(!proofContext.textures[names[i]].generated);
        CHECK(proofContext.textures[names[7]].generated);
        failCommand = 0; established(A); CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == 2); repeat(8);
    } else if (!strcmp(argv[1], "partial-delete-bound")) {
        GLuint names[8]; R200PPCDispatchGenTextures(8, names);
        for (ULONG i = 0; i < 8; ++i) { CHECK(names[i] == i + 1); define(names[i]); }
        established(A); ULONG serial = proofContext.semanticStateSerial;
        failCommand = MGLPPC_CMD_DELETE_TEXTURES; failOccurrence = 2;
        R200PPCDispatchDeleteTextures(8, names);
        CHECK(Lib_glGetError() == GL_OUT_OF_MEMORY && !FrontendPlatform.nativeBindingKnown);
        CHECK(observed.command[MGLPPC_CMD_DELETE_TEXTURES] == 2);
        CHECK(!proofContext.boundTexture[0] && !proofContext.textures[A].generated);
        /* Do not accept the unchanged serial merely because cache invalidation worked. */
        CHECK(proofContext.semanticStateSerial == serial + 1);
        failCommand = 0; established(0); repeat(8);
    } else if (!strcmp(argv[1], "lifecycle")) {
        CHECK(!useMailbox); established(A); R200PPCPlatformResetClientDefaults();
        CHECK(!FrontendPlatform.nativeBindingKnown); established(A);
        CHECK(!R200PPCDispatchCreateContext(0, 0, 320, 240)); /* early rejection must invalidate too */
        CHECK(!FrontendPlatform.nativeBindingKnown); established(A);
        BOOL *failures[] = {&failFlush, &failSubmit, &failBarrier, &failWait};
        for (ULONG i = 0; i < 4; ++i) {
            *failures[i] = TRUE; R200PPCDispatchDeleteContext();
            CHECK(R200PPCContext && !FrontendPlatform.nativeBindingKnown && FrontendPlatform.registered);
            CHECK(!observed.command[MGLPPC_CMD_DESTROY_CONTEXT]); *failures[i] = FALSE; established(A);
        }
        failCommand = MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS; R200PPCDispatchDeleteContext();
        CHECK(R200PPCContext && !FrontendPlatform.nativeBindingKnown && FrontendPlatform.registered);
        failCommand = 0; established(A);
        ULONG oldContext = FrontendPlatform.contextId, oldEpoch = FrontendPlatform.epoch;
        R200PPCDispatchDeleteContext(); CHECK(!R200PPCContext && !FrontendPlatform.nativeBindingKnown);
        CHECK(R200PPCDispatchCreateContext(0, 0, 320, 240) == &proofContext);
        CHECK(FrontendPlatform.contextId > oldContext && FrontendPlatform.epoch > oldEpoch);
        CHECK(!FrontendPlatform.nativeBindingKnown && !proofContext.boundTexture[0]);
        ULONG binds = observed.command[MGLPPC_CMD_BIND_TEXTURE]; established(0); established(A);
        CHECK(observed.command[MGLPPC_CMD_BIND_TEXTURE] == binds + 2); repeat(8);
        failCommand = MGLPPC_CMD_DESTROY_CONTEXT; failOccurrence = observed.command[failCommand] + 1;
        R200PPCDispatchDeleteContext();
        CHECK(R200PPCContext && !FrontendPlatform.nativeBindingKnown && FrontendPlatform.mailboxAbandoned);
        failCommand = 0; R200PPCDispatchDeleteContext(); CHECK(!R200PPCContext);
        FrontendPlatform.nativeBindingKnown = TRUE; R200PPCDispatchDeleteContext();
        CHECK(!FrontendPlatform.nativeBindingKnown); FrontendPlatform.nativeBindingKnown = TRUE;
        failCommand = MGLPPC_CMD_CREATE_CONTEXT; failOccurrence = observed.command[failCommand] + 1;
        CHECK(!R200PPCDispatchCreateContext(0, 0, 320, 240) && !FrontendPlatform.nativeBindingKnown);
        FrontendPlatform.nativeBindingKnown = TRUE;
        failCommand = MGLPPC_CMD_REGISTER_SEMANTIC_SLOTS; failOccurrence = observed.command[failCommand] + 1;
        CHECK(!R200PPCDispatchCreateContext(0, 0, 320, 240));
        CHECK(!R200PPCContext && !FrontendPlatform.contextId && !FrontendPlatform.nativeBindingKnown);
        FrontendPlatform.nativeBindingKnown = TRUE; FrontendPlatform.opened = FALSE;
        CHECK(!R200PPCDispatchCreateContext(0, 0, 320, 240) && !FrontendPlatform.nativeBindingKnown);
    } else if (!strcmp(argv[1], "lifecycle-terminal")) {
        for (ULONG fault = 0; fault < 3; ++fault) {
            reset(useMailbox); established(A);
            FrontendPlatform.ibSmokeFailed = fault == 0;
            FrontendPlatform.cpFailed = fault == 1;
            if (fault == 2) { FrontendPlatform.cpEnabled = TRUE; FrontendPlatform.ring = &ring; ring.error = 1; }
            R200PPCFrontendPlatform saved = FrontendPlatform;
            struct Observations before = observed;
            R200PPCDispatchDeleteContext();
            CHECK(R200PPCContext && !memcmp(&FrontendPlatform, &saved, sizeof(saved)));
            CHECK(!memcmp(&observed, &before, sizeof(before)));
        }
    } else if (!strcmp(argv[1], "lifecycle-drain")) {
        CHECK(useMailbox); established(A); FrontendPlatform.mailboxOutstanding = TRUE;
        ++FrontendPlatform.mailboxSerial; R200PPCDispatchDeleteContext();
        CHECK(R200PPCContext && !FrontendPlatform.nativeBindingKnown && FrontendPlatform.registered);
        CHECK(FrontendPlatform.mailboxOutstanding && FrontendPlatform.mailboxPoisoned && FrontendPlatform.mailboxAbandoned);
        CHECK(observed.total == 1 && !observed.geometryFlushes && !observed.submits && !observed.barriers);
        CHECK(FrontendPlatform.mailbox == &mailbox && FrontendPlatform.ring == &ring);
        mailbox.ackSerial = FrontendPlatform.mailboxSerial; R200PPCDispatchDeleteContext();
        CHECK(!R200PPCContext && !FrontendPlatform.nativeBindingKnown && !FrontendPlatform.registered);
        CHECK(!FrontendPlatform.mailbox && !FrontendPlatform.ring && !FrontendPlatform.mailboxOutstanding);
        CHECK(!observed.geometryFlushes && !observed.submits);
    } else { CHECK(!"unknown case"); }
    if (stagingLive) FreeVec(staging);
    printf("PASS %s %s\n", argv[1], argv[2]); return 0;
}
'''


class BindCache(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix="r200-bind-cache-", dir="/tmp/opencode")
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        cls.binary = cls.directory / "test"
        source = cls.directory / "test.c"
        source.write_text(STUBS + "".join(function(FRONTEND, name) for name in FUNCTIONS) + CASES)
        result = subprocess.run([
            "cc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
            # The harness compiles an extracted subset; helpers retained
            # for the full frontend but unused in a slice are harmless.
            "-Wno-unused-function",
            "-fno-pie", "-no-pie", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", str(source), "-o", str(cls.binary),
        ], capture_output=True, text=True, timeout=60, cwd=cls.directory)
        if result.returncode:
            raise AssertionError(f"CPU harness compilation failed:\n{result.stdout}{result.stderr}")

    def cases(self, cases, modes=("exec", "mailbox")):
        for case in cases:
            for mode in modes:
                with self.subTest(case=case, mode=mode):
                    result = subprocess.run([str(self.binary), case, mode], capture_output=True,
                        text=True, timeout=10, cwd=self.directory, env={**os.environ,
                            "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0",
                            "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stderr, "")
                    self.assertEqual(result.stdout, f"PASS {case} {mode}\n")

    def test_first_repeat_generation_and_switches(self):
        self.cases(("first-repeat", "generated", "switch"))

    def test_validation_and_sticky_errors(self):
        self.cases(("validation", "stale-error"))

    def test_native_failures_and_clean_restoration(self):
        self.cases(("failed-bind", "call-failure", "subimage"))

    def test_transport_gates(self):
        self.cases(("gates",))
        self.cases(("header", "outstanding", "late-ack"), modes=("mailbox",))

    def test_delete_invalidation(self):
        self.cases(("delete", "partial-delete"))

    def test_partial_delete_binding_generation(self):
        self.cases(("partial-delete-bound",))

    def test_context_recreation(self):
        self.cases(("lifecycle",), modes=("exec",))
        self.cases(("lifecycle-drain",), modes=("mailbox",))
        self.cases(("lifecycle-terminal",))

    def test_bind_publication_source_order(self):
        body = re.sub(r"/\*.*?\*/|//[^\n]*|\s+", "",
                      function(FRONTEND, "R200PPCDispatchBindTexture"), flags=re.S)
        slow = body[body.rindex("FrontendPlatform.nativeBindingKnown=FALSE;"):]
        order = ("FrontendPlatform.nativeBindingKnown=FALSE;", "PlatformCall(MGLPPC_CMD_BIND_TEXTURE",
                 "FrontendSetTransportError(context);return;", "Lib_glBindTexture(target,texture);",
                 "FrontendPlatform.nativeBoundTexture=texture;", "FrontendPlatform.nativeBindingKnown=")
        positions = [slow.index(token) if index < len(order) - 1 else slow.rindex(token)
                     for index, token in enumerate(order)]
        self.assertEqual(positions, sorted(positions))

    def test_invalidation_source_order(self):
        invalidate = "FrontendPlatform.nativeBindingKnown=FALSE;"
        for name, following in (
            ("PlatformCallFail", "++pcFails;"),
            ("R200PPCPlatformResetClientDefaults", "FrontendPlatform.pixelDepth=32;"),
            ("R200PPCDispatchCreateContext", "if(!FrontendPlatform.opened||R200PPCContext)"),
            ("R200PPCDispatchDeleteContext", "if(!R200PPCContext)"),
        ):
            with self.subTest(function=name):
                body = re.sub(r"/\*.*?\*/|//[^\n]*|\s+", "", function(FRONTEND, name), flags=re.S)
                self.assertLess(body.index(invalidate), body.index(following))
                prefix = body[:body.index(invalidate)]
                if name == "R200PPCDispatchDeleteContext":
                    # Fatal transport state pins all mirrors too; ordinary
                    # teardown still invalidates before its first retryable exit.
                    self.assertIn("FrontendPlatform.ibSmokeFailed||FrontendPlatform.cpFailed", prefix)
                    self.assertIn("FrontendPlatform.ring->error", prefix)
                    self.assertEqual(prefix.count("return;"), 1)
                else:
                    self.assertNotIn("return", prefix)
        body = re.sub(r"/\*.*?\*/|//[^\n]*|\s+", "",
                      function(FRONTEND, "R200PPCDispatchDeleteTextures"), flags=re.S)
        order = ("if(!StateChangeAllowed(context))return;", "if(count<0||(!names&&count))",
                 "if(!count)return;", invalidate, "FrontendTextureBarrier(context)",
                 "PlatformCall(MGLPPC_CMD_DELETE_TEXTURES")
        positions = [body.index(token) for token in order]
        self.assertEqual(positions, sorted(positions))


if __name__ == "__main__":
    unittest.main()
