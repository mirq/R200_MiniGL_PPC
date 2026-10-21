"""Run production readback functions with deterministic CPU/GL stubs, no SDK.

python3 tests/test_r200_readback.py
The non-PIE harness keeps real static buffers inside the protocol's 32-bit RAM
address space. ASan/UBSan check accesses; cache/GPU behavior is mocked, not proven.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "backend_r200/include/libraries/minigl_ppc_protocol.h"
FRONTEND = (ROOT / "backend_r200/library/minigl_r200_warpos_compile_probe.c").read_text()
HOST = (ROOT / "backend_r200/minigl_ppc_host.c").read_text()


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


common = f'''
#define MGLPPC_TEXTURE_VALIDATION_TEST
#include "{PROTOCOL}"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef int32_t GLint, GLsizei, LONG;
typedef ULONG GLenum;
typedef unsigned char UBYTE;
typedef void GLvoid;
typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define CACRF_ClearD 1
enum {{ GL_NO_ERROR=0, GL_RGBA=155, GL_RGB=151, GL_UNSIGNED_BYTE=200,
    GL_INVALID_ENUM=80, GL_INVALID_VALUE=83, GL_INVALID_OPERATION=82,
    GL_OUT_OF_MEMORY=123, GL_PACK_ALIGNMENT=130, GL_PACK_ROW_LENGTH=131,
    GL_PACK_SKIP_ROWS=132, GL_PACK_SKIP_PIXELS=133,
    GL_UNPACK_ALIGNMENT=134, GL_UNPACK_ROW_LENGTH=135,
    GL_UNPACK_SKIP_ROWS=136, GL_UNPACK_SKIP_PIXELS=137 }};
typedef struct {{ ULONG value[8], protocolVersion, contextId, payloadWords;
    int32_t status; }} MGLPPCMessage;
'''
common += "\n".join(re.findall(r"^#define MGLPPC_STATUS_.*$", PROTOCOL.read_text(), re.M))

frontend = common + r'''
static struct MiniGLR200Context {
    ULONG insideBegin, width, height;
    ULONG packAlignment, packRowLength, packSkipRows, packSkipPixels;
} context;
static struct {
    ULONG peerProtocol, capabilities, contextId, textureUploadCapacity;
    BOOL mailboxAbandoned;
    UBYTE *textureUploadBuffer;
    MGLPPCMessage control;
} FrontendPlatform;
static UBYTE staging[4096] __attribute__((aligned(64))), output[256];
static ULONG error, stage, calls, allocs, frees, clean;
static BOOL ready, failAlloc, failBarrier;
static int failCall;
static struct MiniGLR200Context *CurrentContext(void) { return &context; }
static void SetError(struct MiniGLR200Context *c, GLenum e) { assert(c == &context); error = e; }
static BOOL StateChangeAllowed(struct MiniGLR200Context *c) {
    if (c->insideBegin) { SetError(c, GL_INVALID_OPERATION); return FALSE; }
    return TRUE;
}
static BOOL PlatformMailboxReady(void) { return ready; }
static BOOL FlushScreenVertices(struct MiniGLR200Context *c) {
    assert(c == &context && stage == 0); ++stage; return !failBarrier;
}
static BOOL SubmitAndWait(struct MiniGLR200Context *c) {
    assert(c == &context && stage == 1); ++stage; return TRUE;
}
static BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *c, BOOL present) {
    assert(c == &context && stage == 2 && !present); ++stage; return TRUE;
}
static APTR AllocVec(ULONG bytes, ULONG flags) {
    assert(bytes == 24 + 63 && flags == MEMF_PUBLIC && ready);
    ++allocs; return failAlloc ? NULL : staging + 1;
}
static void FreeVec(APTR p) { assert(p == staging + 1 && ready); ++frees; }
static void MGLPPCFlushDataRange(APTR p, ULONG bytes) {
    assert(p == staging + 32 && bytes == 24 && stage == 3);
    assert(clean == 0 ? calls == 0 : calls == 1);
    ++clean;
}
static BOOL PlatformCall(ULONG cmd, ULONG id) {
    ULONG *v = FrontendPlatform.control.value, i;
    assert(cmd == MGLPPC_CMD_READ_PIXELS_SHARED && id == 3 && clean == 1);
    assert(v[0] == 2 && v[1] == 3 && v[2] == 3 && v[3] == 2);
    assert(v[4] == GL_RGBA && v[5] == GL_UNSIGNED_BYTE);
    assert(v[6] == (ULONG)(uintptr_t)(staging + 32) && v[7] == 24);
    ++calls;
    /* Even a partial write followed by failure must not reach the caller. */
    for (i = 0; i < 24; ++i) staging[32 + i] = (UBYTE)i;
    if (failCall) {
        FrontendPlatform.control.status = failCall == 1 ? MGLPPC_STATUS_GL_ERROR : MGLPPC_STATUS_BUSY;
        v[0] = GL_OUT_OF_MEMORY;
        if (failCall == 2) { ready = FALSE; FrontendPlatform.mailboxAbandoned = TRUE; }
        return FALSE;
    }
    return TRUE;
}
'''
# The harness has one token numbering for both sides, so the wire's
# native-to-OpenGL error map (backend_r200/generated/r200_wire_enum.h) is the
# identity on the error codes and rejects every other word, as the real one.
frontend += r'''
static GLenum WireNativeErrorToGL(GLenum native)
{
    switch (native) {
    case GL_INVALID_ENUM: case GL_INVALID_VALUE: case GL_INVALID_OPERATION:
    case GL_OUT_OF_MEMORY: return native;
    default: return 0;
    }
}
'''
for signature in ("static BOOL FrontendTextureBarrier(",
                  "static void FrontendSetTransportError(",
                  "static BOOL FrontendEnsureTextureUploadBuffer(",
                  "void R200PPCDispatchReadPixels("):
    frontend += function(FRONTEND, signature)
frontend += r'''
static void reset(void) {
    memset(&context, 0, sizeof(context)); memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
    memset(output, 0xa5, sizeof(output));
    error = stage = calls = allocs = frees = clean = 0;
    ready = TRUE; failAlloc = failBarrier = FALSE; failCall = 0;
    context.width = 320; context.height = 240;
    context.packAlignment = 8; context.packRowLength = 5;
    context.packSkipRows = 1; context.packSkipPixels = 1;
    FrontendPlatform.peerProtocol = 15;
    FrontendPlatform.capabilities = MGLPPC_CAP_READ_PIXELS_V1;
    FrontendPlatform.contextId = 3;
}
static void readback(void) {
    R200PPCDispatchReadPixels(2, 3, 3, 2, GL_RGBA, GL_UNSIGNED_BYTE, output);
}
static void untouched(void) {
    ULONG i; for (i = 0; i < sizeof(output); ++i) assert(output[i] == 0xa5);
}
int main(void) {
    ULONG i;
    assert((uintptr_t)staging + sizeof(staging) < UINT32_MAX);
    assert((uintptr_t)output + sizeof(output) < UINT32_MAX);
    reset(); readback();
    assert(!error && clean == 2 && calls == 1 && allocs == 1 && !frees);
    for (i = 0; i < sizeof(output); ++i) {
        ULONG expected = i >= 28 && i < 40 ? i - 28 :
                         i >= 52 && i < 64 ? i - 40 : 0xa5;
        assert(output[i] == expected);
    }
    /* Reuse the same allocation; no new/free staging on the next request. */
    stage = calls = clean = 0; readback(); assert(allocs == 1 && !frees);
    reset(); failCall = 1; readback();
    assert(error == GL_OUT_OF_MEMORY && clean == 1); untouched();
    reset(); failCall = 2; readback();
    assert(error == GL_INVALID_OPERATION && clean == 1); untouched();
    readback(); assert(calls == 1 && allocs == 1 && !frees); untouched();
    ready = TRUE; readback(); /* late ack permits teardown only */
    assert(calls == 1 && allocs == 1 && !frees); untouched();
    reset(); failAlloc = TRUE; readback(); assert(error == GL_OUT_OF_MEMORY && !calls); untouched();
    reset(); failBarrier = TRUE; readback(); assert(error == GL_INVALID_OPERATION && !allocs); untouched();
    reset(); FrontendPlatform.capabilities = 0; readback(); assert(error == GL_INVALID_OPERATION && !stage); untouched();
    reset(); FrontendPlatform.peerProtocol = 14; readback(); assert(error == GL_INVALID_OPERATION && !stage);
    reset(); context.insideBegin = TRUE; readback(); assert(error == GL_INVALID_OPERATION && !stage);
    reset(); context.packRowLength = 0x7fffffff; readback(); assert(error == GL_INVALID_VALUE && !stage); untouched();
    reset(); R200PPCDispatchReadPixels(0,0,1024,513,GL_RGBA,GL_UNSIGNED_BYTE,output);
    assert(error == GL_INVALID_OPERATION && !stage); untouched();
    reset(); R200PPCDispatchReadPixels(-1,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,output);
    assert(error == GL_INVALID_VALUE && !stage); untouched();
    reset(); R200PPCDispatchReadPixels(0,0,1,1,GL_RGB,GL_UNSIGNED_BYTE,output);
    assert(error == GL_INVALID_ENUM && !stage); untouched();
    reset(); R200PPCDispatchReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    assert(error == GL_INVALID_VALUE && !stage);
    reset(); R200PPCDispatchReadPixels(0,0,0,1,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    assert(!error && !stage);
    puts("Production frontend: ordering, pack, reuse, errors, timeout/no-copy passed");
    return 0;
}
'''

host = common + r'''
struct Node { struct Node *ln_Succ; };
struct MemHeader { struct Node mh_Node; APTR mh_Lower, mh_Upper; };
static struct MemHeader region, sentinel;
static struct { struct { struct Node *lh_Head; } MemList; } sys;
#define SysBase (&sys)
typedef struct { APTR context; ULONG contextId; BOOL semanticRegistered, insidePrimitive;
    GLint pack[4], unpack[4]; } MGLPPCHostState;
static UBYTE buffer[4096] __attribute__((aligned(64)));
static ULONG clean, writes, fence, pack, err, locked;
static BOOL failFence, failNative, failSetup, failRestore, replaying, replayInside;
static GLint nativePack[4], savedPack[4];
'''
for name in ("HostPackNames", "HostUnpackNames"):
    start = HOST.index(f"static const GLenum {name}[4]")
    host += HOST[start:HOST.index("};", start) + 2] + "\n"
host += r'''
static void Forbid(void) { ++locked; }
static void Permit(void) { assert(locked == 1); --locked; }
static BOOL DrainSemanticFence(MGLPPCHostState *s) { assert(s->context && !locked); ++fence; return !failFence; }
static void CacheClearE(APTR p, ULONG bytes, ULONG flags) {
    assert(!locked && fence == 1 && p == buffer && bytes == 24 && flags == CACRF_ClearD);
    assert(pack == 4); assert(clean == 0 ? writes == 0 : writes == 1); ++clean;
}
static GLenum glGetError(void) { GLenum e = err; err = 0; return e; }
static void glPixelStorei(GLenum name, GLint value) {
    ULONG index = name - GL_PACK_ALIGNMENT;
    assert(index < 4);
    if (replaying) {
        if (replayInside) { err = GL_INVALID_OPERATION; return; }
        if (value < 0 || (!index && value != 1 && value != 2 && value != 4 && value != 8)) {
            err = GL_INVALID_VALUE; return;
        }
    } else {
        assert(index == pack % 4);
        assert(value == (pack < 4 ? (pack == 0 ? 1 : 0) : savedPack[index]));
        if (pack == 0 && failSetup) err = GL_OUT_OF_MEMORY;
        if (pack == 4 && failRestore) err = GL_INVALID_OPERATION;
        ++pack;
    }
    nativePack[index] = value;
}
static void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, GLvoid *p) {
    assert(x == 2 && y == 3 && w == 3 && h == 2 && fmt == GL_RGBA && type == GL_UNSIGNED_BYTE);
    assert(p == buffer && clean == 1 && pack == 4); ++writes;
    assert(nativePack[0] == 1 && !nativePack[1] && !nativePack[2] && !nativePack[3]);
    memset(p, 0x5a, 24); if (failNative) err = GL_INVALID_VALUE;
}
static void handle(MGLPPCMessage *message, MGLPPCHostState *state) {
    memcpy(savedPack, nativePack, sizeof(savedPack));
    message->status = MGLPPC_STATUS_OK;
    switch (MGLPPC_CMD_READ_PIXELS_SHARED) {
'''
host += HOST[HOST.index("    case MGLPPC_CMD_READ_PIXELS_SHARED:"):
             HOST.index("    case MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED:")]
host += r'''
    }
    assert(!memcmp(nativePack, savedPack, sizeof(nativePack)));
    assert(!memcmp(state->pack, savedPack, sizeof(nativePack)));
}
static BOOL replay(MGLPPCHostState *state, ULONG *arguments, ULONG argumentCount) {
    switch (26) {
'''
replay = HOST[HOST.index("        case MGLPPC_BATCH_PIXEL_STOREI:"):
              HOST.index("        case MGLPPC_BATCH_MGL_ENABLE_SYNC:")]
host += replay.replace("case MGLPPC_BATCH_PIXEL_STOREI:", "case 26:")
host += r'''
    }
    return TRUE;
}
static MGLPPCMessage msg;
static MGLPPCHostState state;
static void reset(void) {
    ULONG args[8] = {2,3,3,2,GL_RGBA,GL_UNSIGNED_BYTE,(ULONG)(uintptr_t)buffer,24};
    memset(&msg, 0, sizeof(msg)); memcpy(msg.value, args, sizeof(args));
    msg.contextId = msg.protocolVersion = 15;
    state.context = buffer; state.contextId = 15;
    state.semanticRegistered = TRUE; state.insidePrimitive = FALSE;
    clean = writes = fence = pack = err = locked = 0; failFence = failNative = FALSE;
    failSetup = failRestore = replaying = replayInside = FALSE;
'''
start = HOST.index("        state->unpack[0] = 4;")
host += HOST[start:HOST.index("        message->contextId =", start)].replace("state->", "state.")
host += r'''
    assert(state.pack[0] == 4 && !state.pack[1] && !state.pack[2] && !state.pack[3]);
    memcpy(nativePack, state.pack, sizeof(nativePack));
    region.mh_Node.ln_Succ = &sentinel.mh_Node; sentinel.mh_Node.ln_Succ = NULL;
    region.mh_Lower = buffer; region.mh_Upper = buffer + sizeof(buffer);
    sys.MemList.lh_Head = &region.mh_Node;
}
static void rejected(int status) {
    handle(&msg, &state); assert(msg.status == status && !writes && !clean && !locked);
}
int main(void) {
    ULONG i, arguments[2];
    const GLint nondefault[4] = {8, 17, 3, 5};
    assert((uintptr_t)buffer + sizeof(buffer) < UINT32_MAX);
    reset(); handle(&msg, &state); assert(!msg.status && !msg.value[0] && writes == 1 && clean == 2 && pack == 8);
    /* Actual replay tracking, not a fabricated snapshot of nondefault state. */
    for (i = 0; i < 2; ++i) {
        ULONG index;
        reset(); replaying = TRUE;
        for (index = 0; index < 4; ++index) {
            arguments[0] = HostPackNames[index]; arguments[1] = nondefault[index];
            assert(replay(&state, arguments, 2));
        }
        assert(!memcmp(state.pack, nondefault, sizeof(nondefault)));
        assert(!memcmp(nativePack, nondefault, sizeof(nondefault)));
        arguments[0] = GL_PACK_ALIGNMENT; arguments[1] = 3;
        assert(replay(&state, arguments, 2) && glGetError() == GL_INVALID_VALUE);
        arguments[0] = GL_PACK_ROW_LENGTH; arguments[1] = 0xffffffffU;
        assert(replay(&state, arguments, 2) && glGetError() == GL_INVALID_VALUE);
        state.insidePrimitive = replayInside = TRUE; arguments[1] = 99;
        assert(replay(&state, arguments, 2) && glGetError() == GL_INVALID_OPERATION);
        state.insidePrimitive = replayInside = replaying = FALSE;
        assert(!memcmp(state.pack, nondefault, sizeof(nondefault)));
        failNative = i != 0; handle(&msg, &state);
        assert(pack == 8 && clean == 2 && !err);
        assert(msg.status == (i ? MGLPPC_STATUS_GL_ERROR : MGLPPC_STATUS_OK));
        assert(msg.value[0] == (i ? GL_INVALID_VALUE : GL_NO_ERROR));
    }
    reset(); region.mh_Upper = buffer + 24; handle(&msg, &state); assert(!msg.status && writes == 1);
    reset(); region.mh_Upper = buffer + 23; rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); region.mh_Lower = buffer + 1; rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); msg.value[6] = UINT32_MAX - 4; rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); msg.value[6] = 0; rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); msg.value[7] = 23; rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); msg.payloadWords = 1; rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); msg.value[2] = 0; rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); msg.value[2] = 0xffffffffU; rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); msg.value[2] = 1024; msg.value[3] = 513; msg.value[7] = 1024*513*4;
    rejected(MGLPPC_STATUS_BAD_PAYLOAD);
    reset(); msg.value[4] = GL_RGB; rejected(MGLPPC_STATUS_GL_ERROR); assert(msg.value[0] == GL_INVALID_ENUM);
    reset(); msg.protocolVersion = 14; rejected(MGLPPC_STATUS_UNSUPPORTED);
    reset(); msg.contextId = 16; rejected(MGLPPC_STATUS_NO_CONTEXT);
    reset(); state.semanticRegistered = FALSE; rejected(MGLPPC_STATUS_GL_ERROR);
    reset(); state.insidePrimitive = TRUE; rejected(MGLPPC_STATUS_GL_ERROR);
    reset(); failFence = TRUE; rejected(MGLPPC_STATUS_GL_ERROR);
    reset(); failNative = TRUE; handle(&msg, &state);
    assert(msg.status == MGLPPC_STATUS_GL_ERROR && msg.value[0] == GL_INVALID_VALUE && clean == 2 && pack == 8);
    reset(); failSetup = TRUE; rejected(MGLPPC_STATUS_GL_ERROR);
    assert(msg.value[0] == GL_OUT_OF_MEMORY && pack == 8 && !err);
    reset(); err = GL_INVALID_ENUM; rejected(MGLPPC_STATUS_GL_ERROR);
    assert(msg.value[0] == GL_INVALID_ENUM && !pack && !err);
    reset(); failRestore = TRUE; handle(&msg, &state);
    assert(msg.status == MGLPPC_STATUS_GL_ERROR && msg.value[0] == GL_INVALID_OPERATION && pack == 8 && !err);
    reset(); failNative = failRestore = TRUE; handle(&msg, &state);
    assert(msg.status == MGLPPC_STATUS_GL_ERROR && msg.value[0] == GL_INVALID_VALUE && pack == 8 && !err);
    puts("Production host: bounds, replay tracking, pack restoration, first-error preservation, fence/cache passed");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="r200-readback-", dir="/tmp/opencode") as directory:
    for name, code in (("frontend", frontend), ("host", host)):
        source = Path(directory) / f"{name}.c"
        binary = Path(directory) / name
        source.write_text(code)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
                        "-fno-pie", "-no-pie", "-fsanitize=address,undefined",
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
