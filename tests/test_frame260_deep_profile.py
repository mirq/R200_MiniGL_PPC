"""CPU-only extraction tests: production records, subimage paths and formatters.

No Amiga/driver/native library execution. Run with python3 from any directory.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / "backend_r200/include"
PPC_PATH = "backend_r200/library/minigl_r200_warpos_compile_probe.c"
HOST_PATH = "backend_r200/minigl_ppc_host.c"
ppc = (ROOT / PPC_PATH).read_text()
host = (ROOT / HOST_PATH).read_text()


def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def preprocess(text):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    text = "#define PPC_LM_NEXT(x)\n#define HOST_LM_NEXT(x)\n" + text
    out = subprocess.run(["cc", "-E", "-P", "-x", "c", "-DMGLPPC_PROFILE=0", "-"],
                         input=text, text=True, capture_output=True, check=True).stdout
    # New null macros leave empty statements; the empty-image return gained
    # braces solely for the profile-only result assignment.
    out = re.sub(r"\s+", "", out).replace("{return;}", "return;")
    return out.replace(";", "")


base_ppc = subprocess.check_output(["git", "show", "HEAD:" + PPC_PATH], cwd=ROOT, text=True)
base_host = subprocess.check_output(["git", "show", "HEAD:" + HOST_PATH], cwd=ROOT, text=True)
ppc_body = between(ppc, "#if MGLPPC_PROFILE\nstatic void FrontendProfiledTexSubImage2D(",
                   "void R200PPCDispatchDeleteTextures(")
base_body = between(base_ppc, "void R200PPCDispatchTexSubImage2D(",
                    "void R200PPCDispatchDeleteTextures(")
assert preprocess(ppc_body) == preprocess(base_body), "non-profile PPC operation order changed"
host_case = between(host, "    case MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED:",
                    "    case MGLPPC_CMD_TEXTURE_UPLOAD_SHARED:")
base_case = between(base_host, "    case MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED:",
                    "    case MGLPPC_CMD_TEXTURE_UPLOAD_SHARED:")
assert preprocess(host_case) == preprocess(base_case), "non-profile host operation order changed"

# Teardown output is not reachable from rendering or the host handler.
close = between(ppc, "void R200PPCPlatformClose(void)", "#if MGLPPC_PROFILE && R200_PERF_AUTOREPORT")
assert close.index("MGLPPCTransportClose(") < close.index("FreeVec(") < close.index("PpcLmDump();")
assert ppc.count("PpcLmDump();") == 1
delete = between(ppc, "void R200PPCDispatchDeleteContext(void)", "void R200PPCPlatformClose(void)")
assert delete.index("PlatformCall(MGLPPC_CMD_DESTROY_CONTEXT,") < delete.index("FrontendContextProfileDump();")
assert host.count("HostLmDump();") == 1
assert host.rindex("MiniGLClose();") < host.index("HostLmDump();")
assert "glFinish(" not in host_case and "glGetError(" not in between(
    host, "static void HostLmSnapshot(", "static void HostLmDump(")
assert "glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);" in host
cp_format = between(ppc, "fprintf(cpPhaseLog,", "fclose(cpPhaseLog);")
assert cp_format.count("%lu") == 6 and cp_format.count("(unsigned long)CpPhase") == 6
assert '"rot=%lu tex=' not in cp_format

ppc_profile = between(ppc, "#if MGLPPC_PROFILE\n#include <libraries/minigl_subimage_timing.h>",
                      "void R200PPCTimelineMark(")
host_profile = between(host, "#if MGLPPC_PROFILE\n#include <libraries/minigl_subimage_timing.h>",
                       "/* The stdout redirect dies")
for profile in (ppc_profile, host_profile):
    for call in re.findall(r"(?:fprintf|printf)\((.*?)\);", profile, re.S):
        assert len(re.findall(r"%(?:l[du]|s)", call)) <= 4
    assert not re.search(r"\b(?:AllocVec|malloc|glFinish|Delay)\(", profile)

common = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <exec/types.h>
typedef uint32_t GLenum;
typedef int32_t GLint, GLsizei;
typedef void GLvoid;
#define TRUE 1
#define FALSE 0
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 88
#define GL_INVALID_OPERATION 89
#define GL_INVALID_VALUE 90
#define GL_OUT_OF_MEMORY 123
#define GL_TEXTURE_2D 1
#define GL_RGB 2
#define GL_RGBA 3
#define GL_UNSIGNED_BYTE 4
#define GL_UNSIGNED_SHORT 5
#define GL_COLOR_INDEX 6
#define GL_LUMINANCE 7
#define MGL_UNSIGNED_SHORT_5_6_5 20
#define MGL_UNSIGNED_SHORT_4_4_4_4 21
#define MGL_UBYTE_BGRA 22
#define MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED 30
#define MGLPPC_CMD_TEXTURE_DIRECT_ABORT 31
#define MGLPPC_STATUS_OK 0
#define MGLPPC_STATUS_GL_ERROR 1
#define MGLPPC_STATUS_NO_CONTEXT 2
#define MGLPPC_STATUS_UNSUPPORTED 3
#define MGLPPC_STATUS_BAD_PAYLOAD 4
#define MGLPPC_TEXTURE_SHARED_MAX_BYTES 4194304
#define MGLPPC_TEXTURE_SOURCE_BYTES(f,t) ((t)==GL_UNSIGNED_BYTE ? ((f)==GL_LUMINANCE ? 1 : 4) : 0)
static ULONG now, clockReads, operations, failAt, nativeCalls, queries;
static BOOL outputAllowed;
static ULONG Clock(void) { ++clockReads; return now++; }
static BOOL Op(void) { now += 10; return ++operations != failAt; }
static BOOL MGLPPCTextureRect(ULONG tw, ULONG th, ULONG lc, ULONG defined,
    ULONG level, ULONG x, ULONG y, ULONG w, ULONG h) {
    return level < lc && defined && x <= tw && y <= th && w <= tw-x && h <= th-y;
}
static BOOL MGLPPCTextureSpan(ULONG w, ULONG h, ULONG bpp, ULONG align,
    ULONG rl, ULONG sr, ULONG sp, ULONG address, ULONG *row, ULONG *stride,
    ULONG *offset, ULONG *bytes) {
    (void)align; (void)rl; (void)sr; (void)sp;
    if (!address || !bpp) return FALSE;
    *row = *stride = w*bpp; *offset = 0; *bytes = w*h*bpp; return TRUE;
}
static int Report(FILE *file, const char *fmt, ...) __attribute__((format(printf,2,3)));
static int Report(FILE *file, const char *fmt, ...) {
    assert(outputAllowed); va_list args; va_start(args, fmt);
    int n = vfprintf(file, fmt, args); va_end(args); return n;
}
#define fprintf Report
#define printf(...) Report(stdout, __VA_ARGS__)
'''

ppc_stubs = r'''
#define PerfClock Clock
#define MAX_TEXTURES 2048
#define MAX_TEXTURE_LEVELS 12
#define MGLPPC_CAP_TEXTURE_SUBIMAGE_V1 1
#define MGLPPC_CAP_TEXTURE_DIRECT_V1 2
struct TextureObject {
    BOOL generated, resident;
    ULONG definedLevels, levelCount, width, height, shadowWidth, shadowHeight;
    ULONG residentGeneration;
    UBYTE *shadow;
    struct { APTR Handle; } surface;
};
struct MiniGLR200Context {
    ULONG boundTexture[2], activeTexture, deviceEpoch;
    ULONG unpackAlignment, unpackRowLength, unpackSkipRows, unpackSkipPixels;
    GLenum error;
    struct TextureObject textures[MAX_TEXTURES];
};
static struct MiniGLR200Context context, *live = &context;
static struct {
    ULONG contextId, peerProtocol, capabilities;
    BOOL mailboxAbandoned;
    void *mailbox, *ring, *textureUploadBuffer;
    struct { ULONG value[8]; } control;
} FrontendPlatform;
static struct {
    ULONG textureCalls, textureBytes, textureSharedCalls, textureBarrierCalls;
    ULONG textureBarrierTicks, textureDirectCalls, textureDirectTicks;
    ULONG textureSuccesses, textureTicks;
} PlatformProfile;
static ULONG PlatformClockRate = 999999999;
static int directMode, openMode, opens, closes;
static struct MiniGLR200Context *CurrentContext(void) { return live; }
static BOOL StateChangeAllowed(struct MiniGLR200Context *c) { return c != NULL; }
static void SetError(struct MiniGLR200Context *c, GLenum e) { if (c && !c->error) c->error = e; }
static BOOL R200PPCPendingUploadFlush(struct MiniGLR200Context *c) { (void)c; return Op(); }
static BOOL PlatformMailboxReady(void) { return Op(); }
static BOOL FlushScreenVertices(struct MiniGLR200Context *c) { (void)c; return Op(); }
static BOOL Submit(struct MiniGLR200Context *c) { (void)c; return Op(); }
static BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *c, BOOL present) {
    (void)c; assert(!present); return Op();
}
static void FrontendShadowPatch(struct TextureObject *t, const UBYTE *src,
    ULONG w, ULONG h, GLenum f, GLenum ty, ULONG bpp, ULONG stride, ULONG x, ULONG y) {
    (void)t;(void)src;(void)w;(void)h;(void)f;(void)ty;(void)bpp;(void)stride;(void)x;(void)y; now += 17;
}
static BOOL FrontendTextureBarrier(struct MiniGLR200Context *c) {
    if (!Op()) { SetError(c, GL_INVALID_OPERATION); return FALSE; } return TRUE;
}
static BOOL FrontendEnsureTextureUploadBuffer(ULONG bytes) { assert(bytes); return Op(); }
static void FrontendStageTextureBGRA(void *dst, const UBYTE *src, ULONG w,
    ULONG h, GLenum f, GLenum ty, ULONG bpp, ULONG row, ULONG stride) {
    (void)dst;(void)src;(void)w;(void)h;(void)f;(void)ty;(void)bpp;(void)row;(void)stride; now += 23;
}
static void MGLPPCFlushDataRange(void *dst, ULONG bytes) { (void)dst; assert(bytes); now += 7; }
static ULONG FrontendWireSourceFormat(ULONG f) { return f; }
static void FrontendDropCachedSurface(ULONG handle) { (void)handle; now += 3; }
static void BumpSemanticStateSerial(struct MiniGLR200Context *c) { (void)c; }
static void Reply(void) {
    ULONG *v = FrontendPlatform.control.value;
    v[1]=42; v[2]=v[3]=128; v[4]=v[5]=1; v[7]=9;
}
static BOOL PlatformCall(ULONG command, ULONG id) {
    assert(id==1); assert(command==30 || command==31);
    if (!Op()) return FALSE;
    Reply(); return TRUE;
}
static BOOL FrontendDirectTextureUpload(struct MiniGLR200Context *c, ULONG w,
    ULONG h, GLenum f, GLenum ty, const UBYTE *src, ULONG stride, UBYTE *shadow) {
    (void)c;(void)w;(void)h;(void)f;(void)ty;(void)src;(void)stride;(void)shadow;
    now += 31; if (directMode==2) FrontendPlatform.mailboxAbandoned=TRUE;
    Reply(); return directMode==1;
}
static void FrontendSetTransportError(struct MiniGLR200Context *c) { SetError(c,GL_INVALID_OPERATION); }
static void FrontendMirrorTextureReply(struct MiniGLR200Context *c) { (void)c; now += 5; }
static FILE *OpenReport(const char *path, const char *mode) {
    assert(outputAllowed && !strcmp(path,"DH2:wosbuild/q260deep/ppc_lm.txt") && !strcmp(mode,"w"));
    ++opens; return openMode ? NULL : stdout;
}
static int CloseReport(FILE *f) { assert(outputAllowed && f==stdout); ++closes; return 0; }
#define fopen OpenReport
#define fclose CloseReport
'''
ppc_main = r'''
static void ResetCall(void) {
    memset(&context,0,sizeof(context)); live=&context;
    context.boundTexture[0]=1027; context.deviceEpoch=9;
    struct TextureObject *t=&context.textures[1027];
    t->generated=1; t->width=t->height=t->shadowWidth=t->shadowHeight=128;
    t->levelCount=t->definedLevels=1;
    FrontendPlatform.contextId=1; FrontendPlatform.peerProtocol=15;
    FrontendPlatform.capabilities=3; FrontendPlatform.mailbox=(void *)1;
    FrontendPlatform.mailboxAbandoned=FALSE; operations=0; failAt=0;
}
static void Call(GLsizei w) {
    R200PPCDispatchTexSubImage2D(GL_TEXTURE_2D,0,0,53,w,22,GL_LUMINANCE,GL_UNSIGNED_BYTE,(void *)0x1000);
}
int main(void) {
    assert(sizeof(struct PpcLmRecord) <= 256);
    ResetCall(); now=0xfffffff0U; Call(128);
    struct PpcLmRecord *r=&PpcLmRecords[0];
    assert(r->result==1 && r->path==1 && r->name==1027 && r->sharedOrder==1);
    assert(r->sourceBytes==2816 && r->wireBytes==11264);
    ULONG sum=0;
    for (ULONG i=0;i<MGL_LM_PHASES;++i) if(r->time.seen&(1U<<i)) sum+=r->time.finish[i]-r->time.begin[i];
    assert(sum==(ULONG)(r->time.end-r->time.start) && r->time.end<r->time.start);
    for (ULONG fail=1;fail<=4;++fail) {
        ResetCall(); failAt=fail; Call(128);
        r=&PpcLmRecords[PpcLmCount-1]; assert(!r->result && r->errorAfter);
        assert(r->time.phase==MGL_LM_PHASES);
    }
    ResetCall(); Call(0); assert(PpcLmRecords[PpcLmCount-1].result==2);
    ResetCall(); live=NULL; Call(128); assert(!PpcLmRecords[PpcLmCount-1].result);
    for (directMode=0;directMode<=2;++directMode) {
        ResetCall(); context.textures[1027].shadow=(UBYTE *)0x2000; Call(128);
        r=&PpcLmRecords[PpcLmCount-1];
        assert(r->path==(directMode==0 ? 3U : 2U));
        assert(r->result==(directMode==2 ? 0U : 1U));
        assert(r->time.seen&(1U<<PPC_LM_DIRECT));
        assert(!!r->sharedOrder==(directMode==0));
    }
    ResetCall(); context.error=GL_INVALID_VALUE; Call(128);
    assert(PpcLmRecords[PpcLmCount-1].result==1 && context.error==GL_INVALID_VALUE);
    while (PpcLmSerial<130) { ResetCall(); Call(128); }
    assert(PpcLmCount==128 && PpcLmDropped==2 && !opens);
    outputAllowed=TRUE; PpcLmDump(); PpcLmDump(); assert(opens==1 && closes==1);
    PpcLmDumped=FALSE; openMode=1; PpcLmDump(); PpcLmDump(); assert(opens==2 && closes==1);
    return 0;
}
'''

host_stubs = r'''
#include <libraries/minigl_perf.h>
#define HostClock Clock
#define ProbeTicks Clock
#define MGL_R200_PRIVATE_TEXTURE_V2_QUERY 800
#define MGL_R200_PRIVATE_TEXTURE_V2_VERSION 2
#define CACRF_ClearD 1
typedef struct { ULONG textureName, version, structSize, surfaceToken, deviceEpoch;
    ULONG width,height,levelCount,definedLevels,contentSerial; } MGLR200PrivateTextureV2;
typedef MGLR200PrivateTextureV2 MGLR200PrivateTextureV1;
typedef struct { ULONG command, contextId, sequence, value[8], payloadWords, protocolVersion; LONG status; } MGLPPCMessage;
typedef struct { void *context; ULONG contextId, fenceCount, semanticBoundTexture;
    BOOL nativeSubimage, semanticRegistered, insidePrimitive; GLint unpack[4]; } MGLPPCHostState;
struct MemHeader { struct { struct MemHeader *ln_Succ; } mh_Node; void *mh_Lower, *mh_Upper; };
static struct MemHeader tail, region={{&tail},(void *)0x1000,(void *)0x400000};
static struct { struct { struct MemHeader *lh_Head; } MemList; } execBase={{&region}}, *SysBase=&execBase;
static ULONG HostClockHz=709379, HostNativeEnableError;
static ULONG HostSubimageCount,HostSubimageTicks,HostSubimagePreflightTicks,HostSubimageNativeTicks,HostSubimageBridgeTicks;
static const GLenum HostUnpackNames[4]={0,1,2,3};
static GLenum errorState;
static ULONG residency=0xfffffff0U, upload=11, uploadBytes=0, evictions=0;
static void Forbid(void) { }
static void Permit(void) { }
static BOOL DrainSemanticFence(MGLPPCHostState *state) { (void)state; return Op(); }
static GLenum glGetError(void) { GLenum e=errorState; errorState=0; return e; }
static void glGetIntegerv(GLenum name, GLint *value) {
    if(name==MGL_R200_PRIVATE_TEXTURE_V2_QUERY) {
        MGLR200PrivateTextureV2 *v=(void *)value;
        v->version=Op()?2:0; v->structSize=sizeof(*v); v->surfaceToken=42; v->deviceEpoch=9;
        v->width=v->height=128; v->levelCount=v->definedLevels=1; return;
    }
    ++queries; now+=2;
    switch(name) {
    case MGL_PERF_CLOCK_RATE:*value=709379;break;
    case MGL_PERF_TEXTURE_RESIDENCY_TICKS:*value=(GLint)residency;break;
    case MGL_PERF_TEXTURE_UPLOAD_TICKS:*value=(GLint)upload;break;
    case MGL_PERF_TEXTURE_UPLOAD_BYTES:*value=(GLint)uploadBytes;break;
    case MGL_PERF_TEXTURE_EVICTIONS:*value=(GLint)evictions;break;
    default:assert(0);
    }
}
static void glBindTexture(GLenum target, ULONG name) { (void)target;(void)name; if(!Op())errorState=GL_INVALID_OPERATION; }
static void CacheClearE(APTR address, ULONG bytes, ULONG flags) { (void)address;(void)bytes;(void)flags; now+=3; }
static void glPixelStorei(GLenum name, GLint value) { (void)name;(void)value; }
static void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y,
    GLsizei w, GLsizei h, GLenum f, GLenum type, const GLvoid *pixels) {
    (void)target;(void)level;(void)x;(void)y;(void)f;(void)type;(void)pixels;
    ++nativeCalls; if(!Op())errorState=GL_INVALID_VALUE;
    residency+=41; upload+=29; uploadBytes+=w*h*4; evictions+=1;
}
static BOOL QueryBackendTexture(ULONG name, MGLR200PrivateTextureV1 *v) {
    (void)name; memset(v,0,sizeof(*v)); v->surfaceToken=42; return Op();
}
'''
host_main = r'''
static void Call(ULONG fail, BOOL context, ULONG width) {
    MGLPPCHostState state={0}; MGLPPCMessage msg={0};
    state.context=context?&state:NULL; state.contextId=1;
    state.nativeSubimage=state.semanticRegistered=TRUE; state.fenceCount=2;
    msg.command=MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED; msg.contextId=1; msg.protocolVersion=15;
    msg.value[0]=1027; msg.value[3]=53; msg.value[4]=width; msg.value[5]=22;
    msg.value[6]=MGL_UBYTE_BGRA|(GL_UNSIGNED_BYTE<<16); msg.value[7]=0x1000;
    operations=0; failAt=fail; errorState=0;
    HostLmBegin(&msg,Clock(),TRUE);
    if(HostLmActive) { HostLmActive->handlerEntered=1; HostLmActive->handlerStart=Clock(); }
    HandleSubimage(&msg,&state);
    HOST_LM_HANDLER_END(); HostLmEnd(&msg,Clock());
}
int main(void) {
    assert(sizeof(struct HostLmRecord)<=272);
    now=0xfffffff0U; Call(0,TRUE,128);
    struct HostLmRecord *r=&HostLmRecords[0];
    assert(r->nativeSamples==3 && queries==10 && nativeCalls==1 && !r->status);
    assert((ULONG)(r->nativeAfter[1]-r->nativeBefore[1])==41);
    assert(r->nativeBefore[0]==709379 && r->nativeAfter[0]==709379);
    assert(r->fenceCount==2 && r->time.end<r->time.start);
    ULONG sum=0;
    for(ULONG i=0;i<MGL_LM_PHASES;++i) if(r->time.seen&(1U<<i)) sum+=r->time.finish[i]-r->time.begin[i];
    assert(sum==(ULONG)(r->time.end-r->time.start));
    for(ULONG fail=1;fail<=6;++fail) {
        Call(fail,TRUE,128); r=&HostLmRecords[HostLmCount-1];
        assert(r->status==MGLPPC_STATUS_GL_ERROR && r->error && !HostLmActive);
        assert(r->nativeSamples==(fail<=3?0U:3U));
    }
    ULONG before=queries;
    Call(0,FALSE,128); assert(HostLmRecords[HostLmCount-1].status==MGLPPC_STATUS_NO_CONTEXT);
    Call(0,TRUE,129); assert(HostLmRecords[HostLmCount-1].status==MGLPPC_STATUS_GL_ERROR);
    assert(before==queries);
    MGLPPCMessage rejected={0}; rejected.command=MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED;
    rejected.value[0]=1027; rejected.status=MGLPPC_STATUS_GL_ERROR;
    HostLmBegin(&rejected,Clock(),TRUE); HostLmEnd(&rejected,Clock());
    r=&HostLmRecords[HostLmCount-1]; assert(!r->handlerEntered && !r->error && !r->nativeSamples);
    while(HostLmSerial<130) Call(0,TRUE,128);
    assert(HostLmCount==128 && HostLmDropped==2);
    before=queries; Call(0,TRUE,128); assert(queries==before); /* No snapshots for dropped records. */
    outputAllowed=TRUE; HostLmDump(); HostLmDump();
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="q260-deep-tests-", dir="/tmp/opencode") as temp:
    temp = Path(temp)
    (temp / "exec").mkdir()
    (temp / "exec/types.h").write_text(
        "#ifndef TEST_TYPES\n#define TEST_TYPES\n#include <stdint.h>\n"
        "typedef uint32_t ULONG; typedef int32_t LONG; typedef int BOOL;"
        "typedef uint8_t UBYTE; typedef void *APTR;\n#endif\n")
    programs = {
        "ppc": common + ppc_stubs + ppc_profile + ppc_body + ppc_main,
        "host": common + host_stubs + host_profile +
            "static void HandleSubimage(MGLPPCMessage *message, MGLPPCHostState *state) {\n"
            "switch(message->command) {\n" + host_case + "}\n}\n" + host_main,
    }
    for name, program in programs.items():
        source = temp / (name + ".c")
        binary = temp / name
        source.write_text(program)
        subprocess.run(["cc", "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-variable", "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
                        "-DMGLPPC_PROFILE=1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-pie", "-no-pie", "-I"+str(temp), "-I"+str(INC),
                        str(source), "-o", str(binary)], check=True)
        output = subprocess.run([str(binary)], text=True, capture_output=True, check=True).stdout
        assert output.count("capacity=128 records=128") == 1
        if name == "ppc":
            assert "bytes=2816" in output and "bgra_rect_bytes=11264" in output
            assert "path=3" in output and "path=2" in output
        else:
            assert "key=residency_ticks delta=41" in output
            assert "key=upload_ticks delta=29" in output
            assert "key=upload_bytes delta=11264" in output
            assert "stage=fence_drain" in output and "stage=perf_query_before" in output
        print(name + ": production paths/records, early exits, rollover, drop bounds, one-shot output passed")
print("profile-off operation order identical to HEAD; CP_PHASE six-field formatter and cleanup placement passed")
