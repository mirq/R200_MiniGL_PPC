/* Compile/link probe for the production R200 source under vbcc/WarpOS. */
#define NO_INLINE_LIBCALLS 1
/* DH2:wosbuild/mglppc_client.log trace logger. Default off: every emitted
 * line costs a 68k-side Open/Seek/Write/Close cycle. Define 1 to re-enable. */
#ifndef MGLPPC_CLIENT_TRACE
#define MGLPPC_CLIENT_TRACE 0
#endif
/* Transport attribution is optional; mailbox deadlines still need PerfClock. */
#ifndef MGLPPC_PROFILE
#define MGLPPC_PROFILE 0
#endif
/* The configured NDK clib headers annotate m68k entry points with this token.
 * For a compile-only PPC pass they are declarations, never linked calls. */
#ifndef __stdargs
#define __stdargs
#endif

#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <exec/types.h>
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>
#include <libraries/Picasso96.h>
#include <utility/tagitem.h>
#ifdef WARPUP
#pragma pack(pop)
#endif

/* The installed WarpOS target has no PPC-callable Picasso96 inline header.
 * Declarations are sufficient because this translation unit is never linked. */
#define P96_BITMAP_API_H 1

extern struct Library *P96Base;

struct BitMap *p96AllocBitMap(ULONG width, ULONG height, ULONG depth,
                              ULONG flags, struct BitMap *friendBitmap,
                              RGBFTYPE format);
void p96FreeBitMap(struct BitMap *bitmap);
ULONG p96GetBitMapAttr(struct BitMap *bitmap, ULONG attribute);
ULONG p96BestModeIDTagList(struct TagItem *tags);
ULONG p96GetModeIDAttr(ULONG mode, ULONG attribute);
struct Screen *p96OpenScreenTagList(struct TagItem *tags);
BOOL p96CloseScreen(struct Screen *screen);

#define P96_BITMAP_WIDTH           0UL
#define P96_BITMAP_HEIGHT          1UL
#define P96_BITMAP_MEMORY          3UL
#define P96_BITMAP_BYTES_PER_ROW   4UL
#define P96_BITMAP_BYTES_PER_PIXEL 5UL
#define P96_BITMAP_RGB_FORMAT      7UL
#define P96_BITMAP_IS_P96          8UL
#define P96_BITMAP_IS_ON_BOARD     9UL

#define P96_MODE_WIDTH      0UL
#define P96_MODE_HEIGHT     1UL
#define P96_MODE_DEPTH      2UL
#define P96_MODE_RGB_FORMAT 5UL
#define P96_MODE_IS_P96     6UL

#define P96_BESTMODE_DUMMY           (TAG_USER + 96UL)
#define P96_BESTMODE_FORMATS_ALLOWED (P96_BESTMODE_DUMMY + 1UL)
#define P96_BESTMODE_NOMINAL_WIDTH   (P96_BESTMODE_DUMMY + 3UL)
#define P96_BESTMODE_NOMINAL_HEIGHT  (P96_BESTMODE_DUMMY + 4UL)
#define P96_BESTMODE_DEPTH           (P96_BESTMODE_DUMMY + 5UL)

#define P96_SCREEN_DUMMY      (TAG_USER + 0x20000UL + 96UL)
#define P96_SCREEN_DEPTH      (P96_SCREEN_DUMMY + 5UL)
#define P96_SCREEN_DISPLAY_ID (P96_SCREEN_DUMMY + 0x12UL)
#define P96_SCREEN_SHOW_TITLE (P96_SCREEN_DUMMY + 0x14UL)
#define P96_SCREEN_BEHIND     (P96_SCREEN_DUMMY + 0x15UL)
#define P96_SCREEN_QUIET      (P96_SCREEN_DUMMY + 0x16UL)
#define P96_SCREEN_RGB_FORMAT (P96_SCREEN_DUMMY + 0x1dUL)

#include "r200_unity.c"

typedef char ProbeULONG32[sizeof(ULONG) == 4 ? 1 : -1];
typedef char ProbeLONG32[sizeof(LONG) == 4 ? 1 : -1];
typedef char ProbePointer32[sizeof(APTR) == 4 ? 1 : -1];
typedef char ProbeBOOL16[sizeof(BOOL) == 2 ? 1 : -1];
typedef char ProbeGLBoolean32[sizeof(GLboolean) == 4 ? 1 : -1];
typedef char ProbeGLSizei32[sizeof(GLsizei) == 4 ? 1 : -1];
typedef char ProbeGLFloat32[sizeof(GLfloat) == 4 ? 1 : -1];
typedef char ProbeGLDouble64[sizeof(GLdouble) == 8 ? 1 : -1];
typedef char ProbeScreenMode96[sizeof(MGLScreenMode) == 96 ? 1 : -1];
typedef char ProbeLockInfo24[sizeof(MGLLockInfo) == 24 ? 1 : -1];

#if defined(R200_FRONTEND_LINK_PROOF) || \
    defined(R200_FRONTEND_SEMANTIC_STEP2) || defined(R200_FRONTEND_LIBRARY)
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#if defined(R200_FRONTEND_SEMANTIC_STEP2) || defined(R200_FRONTEND_LIBRARY)
#include <libraries/minigl_ppc_transport.h>
#include <libraries/minigl_ppc_semantic_profile.h>
#if defined(R200_FRONTEND_LIBRARY)
#include <libraries/minigl_ppc_events.h>
#include <libraries/minigl_ppc_semantic_replay.h>
#include <libraries/minigl_ppc_trace.h>
#endif
#endif

/* ---- Unbuffered file trace --------------------------------------------
 * The DLL's stdio is a separate libc instance from the client's, so its
 * printf output never reaches the client's redirect while the process is
 * alive. This logger writes each line through its own dos.library handle.
 * The handle is opened and closed per line: a wedged client therefore
 * leaves the file unlocked and readable, which is the whole point when
 * diagnosing processes that never exit. The path lives on a persistent
 * volume on purpose -- T: is usually RAM-backed and would not survive the
 * reboot a wedged session eventually needs. */
#define TRACE_PATH "DH2:wosbuild/mglppc_client.log"
#define TRACE_CREATED 0xFFFFFFFFUL
#if MGLPPC_CLIENT_TRACE
static ULONG TraceCreated;
static ULONG TraceLines;
#endif

void R200PPCTraceOpen(void)
{
#if MGLPPC_CLIENT_TRACE
    BPTR fh;
    if (TraceCreated == TRACE_CREATED)
        return;
    /* Fresh file per run: truncate any previous session's trace. */
    fh = Open((STRPTR)TRACE_PATH, MODE_NEWFILE);
    if (fh)
        Close(fh);
    TraceCreated = TRACE_CREATED;
    TraceLines = 0;
#endif
}

void R200PPCTraceClose(void)
{
    /* No persistent handle to release; keep the created marker so a later
     * open call does not truncate evidence gathered this run. */
}

#if MGLPPC_CLIENT_TRACE
static void TraceHex(char **p, ULONG value)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[8];
    int n = 0;
    if (!value)
        tmp[n++] = '0';
    while (value) {
        tmp[n++] = digits[value & 15UL];
        value >>= 4;
    }
    while (n)
        *(*p)++ = tmp[--n];
}
#endif

static void TraceEmit(const char *label1, ULONG value1,
                      const char *label2, ULONG value2, int mode)
{
#if MGLPPC_CLIENT_TRACE
    char buf[208];
    char *p = buf;
    ULONG len;
    BPTR fh;
    ULONG tick = PerfClock();
    if (TraceCreated != TRACE_CREATED) {
        R200PPCTraceOpen();
        if (TraceCreated != TRACE_CREATED)
            return;
    }
    if (++TraceLines > 200000UL)
        return;
    *p++ = '#';
    TraceHex(&p, TraceLines);
    *p++ = '@';
    TraceHex(&p, tick);
    *p++ = ' ';
    switch (mode) {
    case 2:
        while (*label1)
            *p++ = *label1++;
        *p++ = '=';
        TraceHex(&p, value1);
        *p++ = ' ';
        while (*label2)
            *p++ = *label2++;
        *p++ = '=';
        TraceHex(&p, value2);
        break;
    case 1:
        while (*label1)
            *p++ = *label1++;
        *p++ = '=';
        TraceHex(&p, value1);
        break;
    default:
        while (*label1)
            *p++ = *label1++;
        break;
    }
    *p++ = '\n';
    len = (ULONG)(p - buf);
    fh = Open((STRPTR)TRACE_PATH, MODE_OLDFILE);
    if (!fh)
        return;
    Seek(fh, 0, OFFSET_END);
    (void)Write(fh, buf, len);
    Close(fh);
#else
    (void)label1;
    (void)value1;
    (void)label2;
    (void)value2;
    (void)mode;
#endif
}

void R200PPCTraceStr(const char *line)
{
    TraceEmit(line, 0, NULL, 0, 0);
}

void R200PPCTraceUL(const char *label, ULONG value)
{
    TraceEmit(label, value, NULL, 0, 1);
}

void R200PPCTraceUL2(const char *label1, ULONG value1,
                     const char *label2, ULONG value2)
{
    TraceEmit(label1, value1, label2, value2, 2);
}


/* Console diagnostics gate: the DLL_OUTPUT console.device path is slow
 * enough to dominate frame time on the 68k side, so every DLL print is
 * silent unless MGLPPC_VERBOSE is set in the environment. The trace file
 * (DH2:wosbuild/mglppc_client.log) is separately compile-time gated. */
static ULONG FrontendConsoleVerbose;

void R200PPCConsolePrintf(const char *fmt, ...)
{
    char buf[320];
    va_list ap;
    if (!FrontendConsoleVerbose)
        return;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    printf("%s", buf);
    fflush(stdout);
}

/* The link proof exercises CPU-side state only. These definitions are the
 * initial platform boundary: no P96 or Radeon3D operation may be reached. */

/* The link proof exercises CPU-side state only. These definitions are the
 * initial platform boundary: no P96 or Radeon3D operation may be reached. */
struct BitMap *p96AllocBitMap(ULONG width, ULONG height, ULONG depth,
                              ULONG flags, struct BitMap *friendBitmap,
                              RGBFTYPE format)
{
    (void)width; (void)height; (void)depth; (void)flags;
    (void)friendBitmap; (void)format;
    return NULL;
}
void p96FreeBitMap(struct BitMap *bitmap) { (void)bitmap; }
ULONG p96GetBitMapAttr(struct BitMap *bitmap, ULONG attribute)
{ (void)bitmap; (void)attribute; return 0; }
ULONG p96BestModeIDTagList(struct TagItem *tags) { (void)tags; return 0; }
ULONG p96GetModeIDAttr(ULONG mode, ULONG attribute)
{ (void)mode; (void)attribute; return 0; }
struct Screen *p96OpenScreenTagList(struct TagItem *tags)
{ (void)tags; return NULL; }
BOOL p96CloseScreen(struct Screen *screen) { (void)screen; return FALSE; }

struct Radeon3DDevice *Radeon3DOpen(ULONG version, struct Radeon3DInfo *info)
{ (void)version; (void)info; return NULL; }
void Radeon3DClose(struct Radeon3DDevice *device) { (void)device; }
BOOL Radeon3DGetInfo(struct Radeon3DDevice *device, struct Radeon3DInfo *info)
{ (void)device; (void)info; return FALSE; }
BOOL Radeon3DSubmit(struct Radeon3DDevice *device, const ULONG *commands,
                    ULONG count, ULONG flags, ULONG *fence)
{ (void)device; (void)commands; (void)count; (void)flags; (void)fence; return FALSE; }
BOOL Radeon3DTestFence(struct Radeon3DDevice *device, ULONG fence)
{ (void)device; (void)fence; return FALSE; }
BOOL Radeon3DWaitFence(struct Radeon3DDevice *device, ULONG fence, ULONG timeout)
{ (void)device; (void)fence; (void)timeout; return FALSE; }
BOOL Radeon3DImportBitMap(struct Radeon3DDevice *device, struct BitMap *bitmap,
                          struct Radeon3DSurface *surface)
{ (void)device; (void)bitmap; (void)surface; return FALSE; }
void Radeon3DReleaseSurface(struct Radeon3DDevice *device,
                            struct Radeon3DSurface *surface)
{ (void)device; (void)surface; }
BOOL Radeon3DExecute(struct Radeon3DDevice *device, const ULONG *records,
                     ULONG dwords, ULONG flags, ULONG *fence)
{ (void)device; (void)records; (void)dwords; (void)flags; (void)fence; return FALSE; }
BOOL Radeon3DInvalidateForTest(struct Radeon3DDevice *device)
{ (void)device; return FALSE; }
BOOL Radeon3DAllocSegment(struct Radeon3DDevice *device, ULONG bytes,
                          struct Radeon3DSegment *segment)
{ (void)device; (void)bytes; (void)segment; return FALSE; }
BOOL Radeon3DFreeSegment(struct Radeon3DDevice *device, ULONG id)
{ (void)device; (void)id; return FALSE; }
BOOL Radeon3DCommitDraw(struct Radeon3DDevice *device,
                        const struct Radeon3DCommit *commit, ULONG *fence)
{ (void)device; (void)commit; (void)fence; return FALSE; }
BOOL Radeon3DCommitBatch(struct Radeon3DDevice *device,
                         const struct Radeon3DCommitBatch *commit,
                         ULONG *fence)
{ (void)device; (void)commit; (void)fence; return FALSE; }
BOOL Radeon3DCommitStateBatch(struct Radeon3DDevice *device,
                              const struct Radeon3DStateBatch *batch,
                              ULONG *fence)
{ (void)device; (void)batch; (void)fence; return FALSE; }
BOOL Radeon3DAllocSurface(struct Radeon3DDevice *device, ULONG width,
                          ULONG height, ULONG format,
                          struct Radeon3DSurface *surface)
{ (void)device; (void)width; (void)height; (void)format; (void)surface;
  return FALSE; }

#if !defined(R200_FRONTEND_LIBRARY)
BOOL R200PPCPlatformRotateSegment(struct MiniGLR200Context *context,
                                  ULONG vertexBytes)
{ (void)context; (void)vertexBytes; return FALSE; }
BOOL R200PPCPlatformPublishSlot(struct MiniGLR200Context *context,
                                ULONG slotIndex)
{ (void)context; (void)slotIndex; return FALSE; }
BOOL R200PPCPlatformWaitSlot(struct MiniGLR200Context *context,
                            ULONG slotIndex)
{ (void)context; (void)slotIndex; return FALSE; }
BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *context, BOOL present)
{ (void)context; (void)present; return FALSE; }
#endif

void dbg_puts(const char *text) { (void)text; }
void dbg_newline(void) {}
void dbg_u32(ULONG value) { (void)value; }
void dbg_s32(LONG value) { (void)value; }
void dbg_hex32(ULONG value) { (void)value; }
void dbg_ptr(const void *pointer) { (void)pointer; }
void dbg_float_bits(float value) { (void)value; }

static struct MiniGLR200Context proofContext;

#if defined(R200_FRONTEND_LINK_PROOF)
static BOOL ExpectedMatrix(const GLfloat *matrix)
{
    static const GLfloat expected[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        1.0f, 2.0f, 3.0f, 1.0f
    };
    ULONG index;

    for (index = 0; index < 16; ++index)
        if (FloatBits(matrix[index]) != FloatBits(expected[index]))
            return FALSE;
    return TRUE;
}

static void EmitProofTriangle(BOOL textured)
{
    Lib_glBegin(GL_TRIANGLES);
    Lib_glColor4ub(255, 0, 0, 255);
    if (textured) Lib_glTexCoord2f(0.0f, 0.0f);
    Lib_glVertex3f(-1.0f, -1.0f, 0.0f);
    Lib_glColor4ub(0, 255, 0, 255);
    if (textured) Lib_glTexCoord2f(1.0f, 0.0f);
    Lib_glVertex3f(1.0f, -1.0f, 0.0f);
    Lib_glColor4ub(0, 0, 255, 255);
    if (textured) Lib_glTexCoord2f(0.5f, 1.0f);
    Lib_glVertex3f(0.0f, 1.0f, 0.0f);
    Lib_glEnd();
}

static BOOL BuildSemanticRecords(ULONG **recordOut, ULONG *dwordsOut)
{
    struct SubmitSlot *slot;
    struct TextureObject *texture;
    ULONG firstDwords = RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS +
                        3UL * RADEON3D_EXEC_HW_TCL_VERTEX_DWORDS;
    ULONG litDwords;

    proofContext.surfaces[0].Handle = (APTR)0x11223344UL;
    EmitProofTriangle(FALSE);
    if (proofContext.error != GL_NO_ERROR ||
        proofContext.screenVertexCount != 3UL ||
        !FlushScreenVertices(&proofContext))
        return FALSE;

    slot = &proofContext.submitSlots[proofContext.buildSlot];
    if (slot->recordCount != firstDwords ||
        slot->records[0] != RADEON3D_EXEC_DRAW_TRIANGLES ||
        slot->records[1] != slot->recordCount ||
        slot->records[2] != 0x11223344UL ||
        slot->records[10] != 3UL)
        return FALSE;

    Lib_glBindTexture(GL_TEXTURE_2D, 1UL);
    texture = &proofContext.textures[1];
    texture->width = 4;
    texture->height = 4;
    texture->levelCount = 1;
    texture->definedLevels = 1;
    texture->resident = TRUE;
    texture->residentDeviceEpoch = proofContext.deviceEpoch;
    texture->imageGeneration = 1;
    texture->residentGeneration = 1;
    texture->contentSerial = 7;
    texture->surface.Handle = (APTR)0x55667788UL;
    Lib_glEnable(GL_TEXTURE_2D);
    EmitProofTriangle(TRUE);
    if (proofContext.error != GL_NO_ERROR ||
        !FlushScreenVertices(&proofContext) ||
        slot->recordCount != firstDwords * 2UL ||
        slot->records[firstDwords + 1UL] != firstDwords ||
        slot->records[firstDwords + 4UL] != 0x55667788UL)
        return FALSE;

    Lib_glDisable(GL_TEXTURE_2D);
    proofContext.radeonCaps |= RADEON3D_CAP_HW_LIGHTING;
    Lib_glEnable(GL_LIGHTING);
    Lib_glEnable(GL_LIGHT0);
    EmitProofTriangle(FALSE);
    litDwords = R200_TCL_HEADER_DWORDS + R200_NORMAL_MATRICES_DWORDS +
                R200_LIGHT_STATE_DWORDS + R200_LIGHT_BLOCK_DWORDS +
                3UL * R200_LIT_VERTEX_DWORDS;
    if (proofContext.error != GL_NO_ERROR ||
        !FlushScreenVertices(&proofContext) ||
        slot->recordCount != firstDwords * 2UL + litDwords ||
        slot->records[firstDwords * 2UL + 1UL] != litDwords)
        return FALSE;
    *recordOut = slot->records;
    *dwordsOut = slot->recordCount;
    return TRUE;
}

static void PrintSemanticRecord(const ULONG *record, ULONG dwords)
{
    ULONG index;

    printf("R200_SEMANTIC_RECORD dwords=%lu", (unsigned long)dwords);
    for (index = 0; index < dwords; ++index)
        printf(" %08lx", (unsigned long)record[index]);
    printf("\n");
}

int main(void)
{
    ULONG *record;
    ULONG recordDwords;

    proofContext.width = 640;
    proofContext.height = 480;
    proofContext.pixelDepth = 32;
    proofContext.batchDwords = MAX_RECORD_DWORDS;
    InitializeFrontendContext(&proofContext);
    R200PPCContext = &proofContext;

    Lib_glMatrixMode(GL_MODELVIEW);
    Lib_glLoadIdentity();
    Lib_glTranslatef(1.0f, 2.0f, 3.0f);
    Lib_glScalef(2.0f, 3.0f, 4.0f);

    if (CurrentContext() != &proofContext ||
        !ExpectedMatrix(proofContext.modelview[0]) ||
        proofContext.semanticStateSerial != 4UL ||
        proofContext.viewportWidth != 640UL ||
        proofContext.viewportHeight != 480UL) {
        printf("R200_FRONTEND_PROOF status=fail serial=%lu\n",
               proofContext.semanticStateSerial);
        return 20;
    }
    if (!BuildSemanticRecords(&record, &recordDwords)) {
        printf("R200_FRONTEND_PROOF status=record_fail error=%lu\n",
               (unsigned long)proofContext.error);
        return 20;
    }
    printf("R200_FRONTEND_PROOF status=ok serial=%lu\n",
           (unsigned long)proofContext.semanticStateSerial);
    PrintSemanticRecord(record, recordDwords);
    return 0;
}
#endif
#if defined(R200_FRONTEND_SEMANTIC_STEP2)

extern void MGLPPCFlushDataRange(APTR address, ULONG bytes);
typedef char SemanticSlotCapacityCheck[
    MGLPPC_SEMANTIC_SLOT_DWORDS == MAX_RECORD_DWORDS ? 1 : -1];

static ULONG SemanticCRC32(const ULONG *words, ULONG count)
{
    const UBYTE *bytes = (const UBYTE *)words;
    ULONG crc = 0xffffffffUL;
    ULONG index;

    for (index = 0; index < count * sizeof(ULONG); ++index) {
        ULONG bit;
        crc ^= bytes[index];
        for (bit = 0; bit < 8UL; ++bit)
            crc = (crc >> 1) ^
                  ((crc & 1UL) ? 0xedb88320UL : 0UL);
    }
    return ~crc;
}

static BOOL CallCommand(MGLPPCTransport *transport, MGLPPCMessage *message,
                        ULONG command, ULONG contextId)
{
    message->command = command;
    message->contextId = contextId;
    message->payloadWords = 0;
    if (!MGLPPCTransportCall(transport, message)) {
        printf("STEP2 transport failure command=%lu\n", command);
        return FALSE;
    }
    if (message->status != MGLPPC_STATUS_OK) {
        printf("STEP2 host failure command=%lu status=%ld value0=%08lx\n",
               command, message->status, message->value[0]);
        return FALSE;
    }
    return TRUE;
}

static BOOL BuildSemanticFrame(ULONG slotIndex, ULONG frame,
                               ULONG *dwordsOut, ULONG *crcOut)
{
    struct SubmitSlot *slot = &proofContext.submitSlots[slotIndex];
    GLfloat offset = (GLfloat)((LONG)(frame % 9UL) - 4L) * 0.05f;

    proofContext.buildSlot = slotIndex;
    ResetSubmitSlot(slot);
    Lib_glClearColor(0.02f, 0.03f, 0.08f, 1.0f);
    Lib_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Lib_glBegin(GL_TRIANGLES);
    Lib_glColor4ub(255, 64, 32, 255);
    Lib_glVertex3f(-0.65f + offset, -0.55f, 0.0f);
    Lib_glColor4ub(32, 255, 96, 255);
    Lib_glVertex3f(0.65f + offset, -0.55f, 0.0f);
    Lib_glColor4ub(64, 128, 255, 255);
    Lib_glVertex3f(offset, 0.65f, 0.0f);
    Lib_glEnd();
    if (proofContext.error != GL_NO_ERROR ||
        !FlushScreenVertices(&proofContext) || !slot->recordCount)
        return FALSE;
    *dwordsOut = slot->recordCount;
    *crcOut = SemanticCRC32(slot->records, slot->recordCount);
    return TRUE;
}

int main(void)
{
    MGLPPCTransport transport;
    MGLPPCMessage control;
    MGLPPCMessage execute[MGLPPC_SEMANTIC_SLOT_COUNT];
    ULONG generation[MGLPPC_SEMANTIC_SLOT_COUNT] = { 0, 0 };
    ULONG dwords[MGLPPC_SEMANTIC_SLOT_COUNT];
    ULONG crc[MGLPPC_SEMANTIC_SLOT_COUNT];
    ULONG contextId = 0;
    ULONG epoch = 0;
    ULONG frame;
    ULONG current = 0;
    ULONG next;
    BOOL registered = FALSE;
    BOOL created = FALSE;
    int result = 20;

    memset(&transport, 0, sizeof(transport));
    memset(&control, 0, sizeof(control));
    memset(execute, 0, sizeof(execute));
    memset(&proofContext, 0, sizeof(proofContext));
    if (!MGLPPCTransportOpen(&transport)) {
        printf("STEP2 transport open failed\n");
        return 20;
    }

    if (!CallCommand(&transport, &control, MGLPPC_CMD_HELLO, 0) ||
        !(control.value[3] & MGLPPC_CAP_SEMANTIC_INLINE)) {
        printf("STEP2 semantic capability unavailable\n");
        goto done;
    }

    memset(&control, 0, sizeof(control));
    control.value[0] = 320;
    control.value[1] = 240;
    control.value[2] = 32;
    control.value[3] = 1;
    if (!CallCommand(&transport, &control, MGLPPC_CMD_CREATE_CONTEXT, 0))
        goto done;
    created = TRUE;
    contextId = control.contextId;
    if (control.value[1] != 320UL || control.value[2] != 240UL ||
        !control.value[3] || !control.value[4] || !control.value[6] ||
        control.value[6] > MGLPPC_SEMANTIC_SLOT_DWORDS) {
        printf("STEP2 invalid metadata size=%lux%lu target=%08lx depth=%08lx batch=%lu\n",
               control.value[1], control.value[2], control.value[3],
               control.value[4], control.value[6]);
        goto done;
    }

    proofContext.width = control.value[1];
    proofContext.height = control.value[2];
    proofContext.pixelDepth = 32;
    proofContext.surfaces[0].Handle = (APTR)control.value[3];
    proofContext.depthSurface.Handle = (APTR)control.value[4];
    proofContext.radeonCaps = control.value[5];
    proofContext.compactTclVertex = control.value[7] != 0;
    proofContext.batchDwords = control.value[6];
    if (!proofContext.batchDwords ||
        proofContext.batchDwords > MGLPPC_SEMANTIC_SLOT_DWORDS)
        proofContext.batchDwords = MGLPPC_SEMANTIC_SLOT_DWORDS;
    InitializeFrontendContext(&proofContext);
    R200PPCContext = &proofContext;

    memset(&control, 0, sizeof(control));
    control.value[0] = (ULONG)proofContext.submitSlots[0].records;
    control.value[1] = (ULONG)proofContext.submitSlots[1].records;
    control.value[2] = MGLPPC_SEMANTIC_SLOT_DWORDS;
    if (!CallCommand(&transport, &control,
                     MGLPPC_CMD_REGISTER_SEMANTIC_SLOTS, contextId))
        goto done;
    registered = TRUE;
    epoch = control.value[0];

    if (!BuildSemanticFrame(current, 0, &dwords[current], &crc[current])) {
        printf("STEP2 first frame build failed error=%lu\n",
               (unsigned long)proofContext.error);
        goto done;
    }

    for (frame = 0; frame < 120UL; ++frame) {
        MGLPPCMessage *message = &execute[current];
        struct SubmitSlot *slot = &proofContext.submitSlots[current];

        MGLPPCFlushDataRange(slot->records,
                             dwords[current] * sizeof(ULONG));
        memset(message, 0, sizeof(*message));
        message->command = MGLPPC_CMD_EXECUTE_SEMANTIC_SLOT;
        message->contextId = contextId;
        message->value[0] = current;
        message->value[1] = ++generation[current];
        message->value[2] = dwords[current];
        message->value[3] = MGLPPC_SEMANTIC_FLAG_PRESENT;
        message->value[4] = crc[current];
        message->value[5] = epoch;
        if (!MGLPPCTransportSend(&transport, message)) {
            printf("STEP2 publish failed frame=%lu\n", frame);
            goto done;
        }

        next = (current + 1UL) % MGLPPC_SEMANTIC_SLOT_COUNT;
        if (!BuildSemanticFrame(next, frame + 1UL,
                                &dwords[next], &crc[next])) {
            printf("STEP2 overlap build failed frame=%lu error=%lu\n",
                   frame, (unsigned long)proofContext.error);
            (void)MGLPPCTransportWait(&transport, message);
            goto done;
        }
        if (!MGLPPCTransportWait(&transport, message) ||
            message->status != MGLPPC_STATUS_OK) {
            printf("STEP2 execute failed frame=%lu status=%ld value0=%08lx\n",
                   frame, message->status, message->value[0]);
            goto done;
        }
        ResetSubmitSlot(slot);
        current = next;
    }

    memset(&control, 0, sizeof(control));
    control.value[5] = epoch;
    control.value[3] = MGLPPC_SEMANTIC_FLAG_READBACK;
    if (!CallCommand(&transport, &control, MGLPPC_CMD_SEMANTIC_BARRIER,
                     contextId))
        goto done;
    if (control.value[1] == control.value[2]) {
        printf("STEP2 readback failed center=%08lx corner=%08lx\n",
               control.value[1], control.value[2]);
        goto done;
    }
    printf("STEP2_SEMANTIC status=ok frames=120 slots=2 executes=%lu center=%08lx corner=%08lx\n",
           (unsigned long)control.value[0], control.value[1],
           control.value[2]);
    result = 0;

done:
    if (registered) {
        memset(&control, 0, sizeof(control));
        control.value[5] = epoch;
        (void)CallCommand(&transport, &control,
                          MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS, contextId);
    }
    if (created) {
        memset(&control, 0, sizeof(control));
        (void)CallCommand(&transport, &control,
                          MGLPPC_CMD_DESTROY_CONTEXT, contextId);
    }
    R200PPCContext = NULL;
    MGLPPCTransportClose(&transport);
    return result;
}
#endif
#if defined(R200_FRONTEND_LIBRARY)

#include <libraries/minigl_ppc_ib_trace.h>
#include <libraries/minigl_ppc_cp_emit.h>
#include <libraries/minigl_ppc_yield.h>

extern void MGLPPCFlushDataRange(APTR address, ULONG bytes);
extern void MGLPPCBlit565Swap(UBYTE *destination, const UBYTE *source,
                              ULONG bytes);
extern void MGLPPCStoreBarrier(void);
extern void MGLPPCInvalidateLine32(APTR address);
typedef struct R200PPCFrontendPlatform {
    MGLPPCTransport transport;
    MGLPPCMessage control;
    ULONG textureMessageWords[
        (MGLPPC_MESSAGE_SIZE(MGLPPC_TEXTURE_CHUNK_WORDS) +
         sizeof(ULONG) - 1UL) / sizeof(ULONG)];
    ULONG registerMessageWords[
        (MGLPPC_MESSAGE_SIZE(16) + sizeof(ULONG) - 1UL) / sizeof(ULONG)];
    MGLPPCMessage execute[SUBMIT_SLOT_COUNT];
    ULONG generation[SUBMIT_SLOT_COUNT];
    BOOL pending[SUBMIT_SLOT_COUNT];
    ULONG contextId;
    ULONG epoch;
    ULONG capabilities;
    ULONG peerProtocol;
    APTR windowHandle;
    BOOL crcFirstOnly;
    BOOL crcArmed;
    BOOL hostEventsConfirmed;
    volatile MGLPPCRingControlV1 *ring;
    volatile MGLPPCMailboxV1 *mailbox;
    ULONG mailboxSerial;
    BOOL mailboxOutstanding;
    BOOL mailboxPoisoned;
    BOOL mailboxAbandoned;
    BOOL nativeBindingKnown;
    BOOL ibSmokeRequested;
    BOOL ibSmokeConfirmed;
    BOOL ibSmokeDry;
    BOOL ibSmokeFailed;
    ULONG ibSegmentBytes;
    BOOL cpRequested, cpEnabled, cpFailed;
    APTR cpAddress;
    ULONG cpBytes, cpSurfaceCount;
    struct {
        ULONG token, epoch;
        struct MGLPPCEmitSurface surface;
    } cpSurfaces[16];
    GLuint nativeBoundTexture;
    /* Deferred host shadow-bind tracking: render binds do not sync the
     * 68k shadow (texture state travels in each draw record header); the
     * upload completers are the only shadow consumers and flush first. */
    GLuint hostShadowTexture;
    BOOL hostShadowDirty;
    APTR ringSlotAllocations[MGLPPC_RING_SLOTS];
    ULONG *ringSlots[MGLPPC_RING_SLOTS];
    ULONG ringPostSerial;
    ULONG segmentLastEntry[4];
    ULONG syncEnabled;
    ULONG syncSeq;
    int pixelDepth;
    int windowMode;
    int vertexBufferSize;
    int textureBufferSize;
    int mtexBufferSize;
    int numberOfBuffers;
    UBYTE *textureUploadBuffer;
    ULONG textureUploadCapacity;
    ULONG shadowBytesTotal;
    BOOL opened;
    BOOL registered;
} R200PPCFrontendPlatform;

static R200PPCFrontendPlatform FrontendPlatform;

#define MGLPPC_REPLAY_BATCHES_PER_FRAME 4UL
#define MGLPPC_REPLAY_MAX_DRAWS \
    (MGLPPC_REPLAY_BATCHES_PER_FRAME * MAX_SLOT_DRAWS)

struct MGLPPCReplayBatch {
    ULONG *records;
    ULONG *vertexOffsets;
    struct Radeon3DStateBatchDraw *stateBatchDraws;
    UBYTE *vertices;
    ULONG recordCount;
    ULONG drawCount;
    ULONG vertexBytes;
    ULONG firstVertexOffset;
    BOOL commitMode;
    BOOL stateBatch;
};

struct MGLPPCReplayFrame {
    ULONG firstBatch;
};

static struct {
    struct MGLPPCReplayFrame *frames;
    struct MGLPPCReplayBatch *batches;
    ULONG frameCapacity;
    ULONG batchCapacity;
    ULONG frameCount;
    ULONG batchCount;
    ULONG deviceEpoch;
    ULONG canonicalBytes;
    ULONG canonicalDrawCount;
    UBYTE *canonicalDrawSource[MGLPPC_REPLAY_MAX_DRAWS];
    ULONG canonicalDrawOffset[MGLPPC_REPLAY_MAX_DRAWS];
    ULONG canonicalDrawBytes[MGLPPC_REPLAY_MAX_DRAWS];
    BOOL active;
    BOOL valid;
    BOOL failed;
    BOOL replaying;
} ReplayCapture;

void MGLPPCSemanticReplayFree(void)
{
    ULONG index;

    if (ReplayCapture.batches) {
        for (index = 0; index < ReplayCapture.batchCount; ++index) {
            if (ReplayCapture.batches[index].records)
                FreeVec(ReplayCapture.batches[index].records);
            if (ReplayCapture.batches[index].vertexOffsets)
                FreeVec(ReplayCapture.batches[index].vertexOffsets);
            if (ReplayCapture.batches[index].stateBatchDraws)
                FreeVec(ReplayCapture.batches[index].stateBatchDraws);
            if (ReplayCapture.batches[index].vertices)
                FreeVec(ReplayCapture.batches[index].vertices);
        }
        FreeVec(ReplayCapture.batches);
    }
    if (ReplayCapture.frames)
        FreeVec(ReplayCapture.frames);
    memset(&ReplayCapture, 0, sizeof(ReplayCapture));
}

BOOL MGLPPCSemanticCaptureBegin(ULONG frameCount)
{
    ULONG frameBytes, batchBytes;

    if (!R200PPCContext || !FrontendPlatform.registered || !frameCount ||
        frameCount > (0xffffffffUL /
                      sizeof(*ReplayCapture.batches)) /
                         MGLPPC_REPLAY_BATCHES_PER_FRAME ||
        frameCount == 0xffffffffUL)
        return FALSE;
    MGLPPCSemanticReplayFree();
    frameBytes = (frameCount + 1UL) * sizeof(*ReplayCapture.frames);
    ReplayCapture.batchCapacity =
        frameCount * MGLPPC_REPLAY_BATCHES_PER_FRAME;
    batchBytes = ReplayCapture.batchCapacity * sizeof(*ReplayCapture.batches);
    ReplayCapture.frames = AllocVec(frameBytes, 0UL);
    ReplayCapture.batches = AllocVec(batchBytes, 0UL);
    if (!ReplayCapture.frames || !ReplayCapture.batches) {
        MGLPPCSemanticReplayFree();
        return FALSE;
    }
    memset(ReplayCapture.frames, 0, frameBytes);
    memset(ReplayCapture.batches, 0, batchBytes);
    ReplayCapture.frameCapacity = frameCount;
    ReplayCapture.deviceEpoch = R200PPCContext->deviceEpoch;
    ReplayCapture.active = TRUE;
    return TRUE;
}

static BOOL GrowReplayBatches(void)
{
    struct MGLPPCReplayBatch *batches;
    ULONG oldCapacity = ReplayCapture.batchCapacity;
    ULONG newCapacity, batchBytes;

    if (!oldCapacity ||
        oldCapacity > (0xffffffffUL /
                       sizeof(*ReplayCapture.batches)) / 2UL) {
        printf("SEMREPLAY batch_capacity_overflow batches=%lu capacity=%lu "
               "frame=%lu\n",
               (unsigned long)ReplayCapture.batchCount,
               (unsigned long)oldCapacity,
               (unsigned long)ReplayCapture.frameCount);
        fflush(stdout);
        ReplayCapture.failed = TRUE;
        return FALSE;
    }
    newCapacity = oldCapacity * 2UL;
    batchBytes = newCapacity * sizeof(*ReplayCapture.batches);
    batches = AllocVec(batchBytes, 0UL);
    if (!batches) {
        printf("SEMREPLAY batch_grow_alloc_fail batches=%lu capacity=%lu "
               "requested=%lu frame=%lu\n",
               (unsigned long)ReplayCapture.batchCount,
               (unsigned long)oldCapacity,
               (unsigned long)newCapacity,
               (unsigned long)ReplayCapture.frameCount);
        fflush(stdout);
        ReplayCapture.failed = TRUE;
        return FALSE;
    }
    memset(batches, 0, batchBytes);
    memcpy(batches, ReplayCapture.batches,
           ReplayCapture.batchCount * sizeof(*ReplayCapture.batches));
    FreeVec(ReplayCapture.batches);
    ReplayCapture.batches = batches;
    ReplayCapture.batchCapacity = newCapacity;
#if MGLPPC_PROFILE
    printf("SEMREPLAY batch_grow batches=%lu capacity=%lu new_capacity=%lu "
           "frame=%lu\n",
           (unsigned long)ReplayCapture.batchCount,
           (unsigned long)oldCapacity,
           (unsigned long)newCapacity,
           (unsigned long)ReplayCapture.frameCount);
    fflush(stdout);
#endif
    return TRUE;
}

static BOOL CaptureReplaySlot(struct MiniGLR200Context *context,
                              const struct SubmitSlot *slot,
                              ULONG slotIndex)
{
    struct MGLPPCReplayBatch *batch;
    ULONG recordBytes, offsetBytes;
    const char *failure;

    if (ReplayCapture.failed)
        return FALSE;
    if (ReplayCapture.frameCount >= ReplayCapture.frameCapacity) {
        printf("SEMREPLAY frame_capacity_overflow frame=%lu capacity=%lu "
               "batches=%lu\n",
               (unsigned long)ReplayCapture.frameCount,
               (unsigned long)ReplayCapture.frameCapacity,
               (unsigned long)ReplayCapture.batchCount);
        fflush(stdout);
        ReplayCapture.failed = TRUE;
        return FALSE;
    }
    if (ReplayCapture.batchCount >= ReplayCapture.batchCapacity &&
        !GrowReplayBatches())
        return FALSE;
    batch = &ReplayCapture.batches[ReplayCapture.batchCount];
    recordBytes = slot->recordCount * sizeof(ULONG);
    failure = "records_alloc";
    batch->records = AllocVec(recordBytes, 0UL);
    if (!batch->records) goto fail;
    memcpy(batch->records, slot->records, recordBytes);
    batch->recordCount = slot->recordCount;
    batch->drawCount = slot->drawCount;
    batch->commitMode = slot->commitMode;
    batch->stateBatch = slot->stateBatch;
    if (slot->commitMode && slot->drawCount) {
        ULONG segmentHalfBase = slotIndex * SEGMENT_HALF_BYTES;
        ULONG index;

        offsetBytes = slot->drawCount * sizeof(ULONG);
        failure = "offsets_alloc";
        batch->vertexOffsets = AllocVec(offsetBytes, 0UL);
        if (!batch->vertexOffsets) goto fail;
        if (slot->stateBatch) {
            failure = "state_draws_alloc";
            batch->stateBatchDraws = AllocVec(
                slot->drawCount * sizeof(*batch->stateBatchDraws), 0UL);
            if (!batch->stateBatchDraws) goto fail;
            memcpy(batch->stateBatchDraws, slot->stateBatchDraws,
                   slot->drawCount * sizeof(*batch->stateBatchDraws));
        }
        for (index = 0; index < slot->drawCount; ++index) {
            failure = "vertex_offset_range";
            if (slot->vertexOffsets[index] < segmentHalfBase)
                goto fail;
            batch->vertexOffsets[index] =
                slot->vertexOffsets[index] - segmentHalfBase;
        }
        batch->firstVertexOffset = batch->vertexOffsets[0];
        failure = "vertex_cursor_range";
        if (batch->firstVertexOffset > slot->segmentCursor) goto fail;
        batch->vertexBytes = slot->segmentCursor - batch->firstVertexOffset;
        if (batch->vertexBytes) {
            failure = "vertices_alloc";
            batch->vertices = AllocVec(batch->vertexBytes, 0UL);
            if (!batch->vertices) goto fail;
            memcpy(batch->vertices,
                   (const UBYTE *)context->segment.info.CpuAddress +
                        slotIndex * SEGMENT_HALF_BYTES +
                        batch->firstVertexOffset,
                   batch->vertexBytes);
        }
    }
    ReplayCapture.batchCount++;
    return TRUE;

fail:
    printf("SEMREPLAY capture_fail reason=%s batch=%lu frame=%lu records=%lu "
           "draws=%lu cursor=%lu vertex_bytes=%lu\n",
           failure, (unsigned long)ReplayCapture.batchCount,
           (unsigned long)ReplayCapture.frameCount,
           (unsigned long)slot->recordCount,
           (unsigned long)slot->drawCount,
           (unsigned long)slot->segmentCursor,
           (unsigned long)batch->vertexBytes);
    fflush(stdout);
    if (batch->records) FreeVec(batch->records);
    if (batch->vertexOffsets) FreeVec(batch->vertexOffsets);
    if (batch->stateBatchDraws) FreeVec(batch->stateBatchDraws);
    if (batch->vertices) FreeVec(batch->vertices);
    memset(batch, 0, sizeof(*batch));
    ReplayCapture.failed = TRUE;
    return FALSE;
}

BOOL MGLPPCSemanticCaptureEnd(void)
{
    ULONG frameIndex, batchIndex, drawIndex, canonicalDraw;

    if (!ReplayCapture.active)
        return FALSE;
    ReplayCapture.active = FALSE;
    ReplayCapture.valid = !ReplayCapture.failed &&
                          ReplayCapture.frameCount ==
                              ReplayCapture.frameCapacity;
    if (!ReplayCapture.valid)
        return FALSE;
    for (batchIndex = ReplayCapture.frames[0].firstBatch;
         batchIndex < ReplayCapture.frames[1].firstBatch; ++batchIndex) {
        const struct MGLPPCReplayBatch *batch =
            &ReplayCapture.batches[batchIndex];
        for (drawIndex = 0; drawIndex < batch->drawCount; ++drawIndex) {
            ULONG start = batch->vertexOffsets[drawIndex] -
                          batch->firstVertexOffset;
            ULONG end = drawIndex + 1UL < batch->drawCount
                ? batch->vertexOffsets[drawIndex + 1UL] -
                      batch->firstVertexOffset
                : batch->vertexBytes;
            if (ReplayCapture.canonicalDrawCount >=
                    MGLPPC_REPLAY_MAX_DRAWS || end < start ||
                end - start > SEGMENT_BYTES -
                                  ReplayCapture.canonicalBytes) {
                printf("SEMREPLAY canonical_fail draw=%lu start=%lu end=%lu "
                       "total=%lu\n",
                       (unsigned long)ReplayCapture.canonicalDrawCount,
                       (unsigned long)start, (unsigned long)end,
                       (unsigned long)ReplayCapture.canonicalBytes);
                ReplayCapture.valid = FALSE;
                return FALSE;
            }
            canonicalDraw = ReplayCapture.canonicalDrawCount++;
            ReplayCapture.canonicalDrawSource[canonicalDraw] =
                batch->vertices + start;
            ReplayCapture.canonicalDrawOffset[canonicalDraw] =
                ReplayCapture.canonicalBytes;
            ReplayCapture.canonicalDrawBytes[canonicalDraw] = end - start;
            ReplayCapture.canonicalBytes += end - start;
        }
    }
    for (frameIndex = 0; frameIndex < ReplayCapture.frameCount;
         ++frameIndex) {
        ULONG frameFirst = ReplayCapture.frames[frameIndex].firstBatch;
        ULONG frameEnd = ReplayCapture.frames[frameIndex + 1UL].firstBatch;
        canonicalDraw = 0;
        for (batchIndex = frameFirst; batchIndex < frameEnd; ++batchIndex) {
            const struct MGLPPCReplayBatch *batch =
                &ReplayCapture.batches[batchIndex];
            for (drawIndex = 0; drawIndex < batch->drawCount;
                 ++drawIndex, ++canonicalDraw) {
                ULONG start = batch->vertexOffsets[drawIndex] -
                              batch->firstVertexOffset;
                ULONG end = drawIndex + 1UL < batch->drawCount
                    ? batch->vertexOffsets[drawIndex + 1UL] -
                          batch->firstVertexOffset
                    : batch->vertexBytes;
                if (canonicalDraw >= ReplayCapture.canonicalDrawCount ||
                    end < start ||
                    end - start !=
                        ReplayCapture.canonicalDrawBytes[canonicalDraw]) {
                    printf("SEMREPLAY layout_fail frame=%lu draw=%lu "
                           "start=%lu end=%lu expected=%lu count=%lu\n",
                           (unsigned long)frameIndex,
                           (unsigned long)canonicalDraw,
                           (unsigned long)start, (unsigned long)end,
                           (unsigned long)(canonicalDraw <
                               ReplayCapture.canonicalDrawCount
                                   ? ReplayCapture.canonicalDrawBytes[
                                         canonicalDraw]
                                   : 0),
                           (unsigned long)ReplayCapture.canonicalDrawCount);
                    ReplayCapture.valid = FALSE;
                    return FALSE;
                }
                if (memcmp(batch->vertices + start,
                           ReplayCapture.canonicalDrawSource[canonicalDraw],
                           end - start) != 0) {
                    const ULONG *actual =
                        (const ULONG *)(batch->vertices + start);
                    const ULONG *expected = (const ULONG *)
                        ReplayCapture.canonicalDrawSource[canonicalDraw];
                    ULONG word, words = (end - start) / sizeof(ULONG);
                    for (word = 0; word < words; ++word)
                        if (actual[word] != expected[word]) break;
                    printf("SEMREPLAY data_fail frame=%lu draw=%lu word=%lu "
                           "actual=%08lx expected=%08lx bytes=%lu\n",
                           (unsigned long)frameIndex,
                           (unsigned long)canonicalDraw,
                           (unsigned long)word,
                           (unsigned long)(word < words ? actual[word] : 0),
                           (unsigned long)(word < words ? expected[word] : 0),
                           (unsigned long)(end - start));
                    ReplayCapture.valid = FALSE;
                    return FALSE;
                }
            }
        }
        if (canonicalDraw != ReplayCapture.canonicalDrawCount) {
            ReplayCapture.valid = FALSE;
            return FALSE;
        }
    }
    return ReplayCapture.valid;
}

ULONG MGLPPCSemanticCapturedFrames(void)
{
    return ReplayCapture.frameCount;
}

BOOL MGLPPCSemanticReplayFrame(ULONG frameIndex)
{
    struct MiniGLR200Context *context = R200PPCContext;
    ULONG batchIndex, batchEnd, canonicalDraw = 0;

    if (!ReplayCapture.valid || !context ||
        context->deviceEpoch != ReplayCapture.deviceEpoch ||
        frameIndex >= ReplayCapture.frameCount || context->insideBegin)
        return FALSE;
    if (!ReplayCapture.replaying) {
        ULONG slotIndex, draw;
        UBYTE *destination;
        if (ReplayCapture.canonicalBytes > context->segment.info.Bytes ||
            !RotateStreamSegment(context, ReplayCapture.canonicalBytes))
            return FALSE;
        destination = (UBYTE *)context->segment.info.CpuAddress;
        for (draw = 0; draw < ReplayCapture.canonicalDrawCount; ++draw)
            memcpy(destination + ReplayCapture.canonicalDrawOffset[draw],
                   ReplayCapture.canonicalDrawSource[draw],
                   ReplayCapture.canonicalDrawBytes[draw]);
        MGLPPCFlushDataRange(destination, ReplayCapture.canonicalBytes);
        for (slotIndex = 0; slotIndex < SUBMIT_SLOT_COUNT; ++slotIndex)
            context->submitSlots[slotIndex].segmentCursor = 0;
        ReplayCapture.replaying = TRUE;
    }
    batchIndex = ReplayCapture.frames[frameIndex].firstBatch;
    batchEnd = ReplayCapture.frames[frameIndex + 1UL].firstBatch;
    for (; batchIndex < batchEnd; ++batchIndex) {
        const struct MGLPPCReplayBatch *capture =
            &ReplayCapture.batches[batchIndex];
        struct SubmitSlot *slot = BuildSubmitSlot(context);
        ULONG index;

        if (slot->recordCount)
            return FALSE;
        if (capture->commitMode && capture->drawCount) {
            if (!context->segment.valid)
                return FALSE;
            for (index = 0; index < capture->drawCount;
                 ++index, ++canonicalDraw) {
                slot->vertexOffsets[index] =
                    ReplayCapture.canonicalDrawOffset[canonicalDraw];
                if (capture->stateBatch) {
                    slot->stateBatchDraws[index].OffsetBytes =
                        slot->vertexOffsets[index];
                    slot->stateBatchDraws[index].VertexCount =
                        capture->stateBatchDraws[index].VertexCount;
                }
            }
        }
        memcpy(slot->records, capture->records,
               capture->recordCount * sizeof(ULONG));
        slot->recordCount = capture->recordCount;
        slot->drawCount = capture->drawCount;
        slot->commitMode = capture->commitMode;
        slot->stateBatch = capture->stateBatch;
        slot->modeDecided = TRUE;
        if (!Submit(context))
            return FALSE;
    }
    return WaitAllSubmissions(context) &&
           R200PPCPlatformBarrier(context, TRUE);
}

/* Alignment introspection for the PPC tuning work: reports where the
 * frontend's float-bearing objects actually landed (amiga-align puts
 * statics at 2-byte alignment; a 2-mod-4 float member traps on 7410). */
ULONG R200PPCDebugAlignFacts(ULONG *facts, ULONG maxFacts)
{
    ULONG count = 0;
    if (!facts || maxFacts < 6UL) return 0UL;
    facts[count++] = (ULONG)&proofContext & 3UL;
    facts[count++] = (ULONG)((char *)&proofContext.vertices) & 3UL;
    facts[count++] = (ULONG)((char *)&proofContext.screenVertices) & 3UL;
    facts[count++] = (ULONG)((char *)&proofContext.pendingDraw) & 3UL;
    facts[count++] = (ULONG)sizeof(struct Vertex) & 3UL;
    facts[count++] = (ULONG)((char *)&proofContext -
                             (char *)&proofContext.vertices) & 3UL;
    return count;
}

/* This calibrated clock is required for mailbox negotiation and deadlines,
 * even when all transport attribution is compiled out. */
static ULONG PlatformClockRate;

#if MGLPPC_PROFILE
static MGLPPCSemanticPlatformProfile PlatformProfile;
static unsigned long long PlatformProfileStart;
static ULONG PlatformSessionStart;
static MGLPPCTimelineEntry PlatformTimeline[MGLPPC_TIMELINE_ENTRIES];
static ULONG PlatformTimelineCount;

/* Diagnostic lifetime only, independent of the legacy protocol clocks. */
static unsigned long long PlatformProfileClock64(void)
{
    ULONG upper, lower, again;
    do {
        __asm__ volatile("mftbu %0\n\tmftb %1\n\tmftbu %2"
                         : "=r"(upper), "=r"(lower), "=r"(again)
                         : : "memory");
    } while (upper != again);
    return ((unsigned long long)upper << 32) | lower;
}
#endif

void R200PPCTimelineMark(ULONG ev, ULONG a, ULONG b)
{
#if MGLPPC_PROFILE
    MGLPPCTimelineEntry *slot;
    if (PlatformTimelineCount >= MGLPPC_TIMELINE_ENTRIES)
        return;
    slot = &PlatformTimeline[PlatformTimelineCount++];
    slot->tsc = PerfClock();
    slot->ev = ev;
    slot->a = a;
    slot->b = b;
#else
    (void)ev;
    (void)a;
    (void)b;
#endif
}

void R200PPCTimelineDump(void)
{
#if MGLPPC_PROFILE
    ULONG index;
    printf("MGLTL begin count=%lu overflow=%lu\n",
           (unsigned long)(PlatformTimelineCount <
                               MGLPPC_TIMELINE_ENTRIES
                               ? PlatformTimelineCount
                               : MGLPPC_TIMELINE_ENTRIES),
           (unsigned long)(PlatformTimelineCount >
                               MGLPPC_TIMELINE_ENTRIES
                               ? PlatformTimelineCount -
                                     MGLPPC_TIMELINE_ENTRIES
                               : 0));
    for (index = 0; index < PlatformTimelineCount &&
                    index < MGLPPC_TIMELINE_ENTRIES;
         ++index) {
        printf("MGLTL t=%lu ev=%lu a=%lu b=%lu\n",
               (unsigned long)PlatformTimeline[index].tsc,
               (unsigned long)PlatformTimeline[index].ev,
               (unsigned long)PlatformTimeline[index].a,
               (unsigned long)PlatformTimeline[index].b);
    }
    printf("MGLTL end\n");
    fflush(stdout);
#endif
}

/* Must stay bit-identical to the host's SemanticCRC32(): same reflected
 * polynomial, same init and final inversion. Table-driven only to keep the
 * per-submit cost off the frame time; the emitted values are unchanged. */
static ULONG PlatformCRCTable[256];
static int PlatformCRCTableReady;

static void PlatformCRC32Init(void)
{
    ULONG index, bit, value;

    for (index = 0; index < 256UL; ++index) {
        value = index;
        for (bit = 0; bit < 8UL; ++bit)
            value = (value >> 1) ^
                    ((value & 1UL) ? 0xedb88320UL : 0UL);
        PlatformCRCTable[index] = value;
    }
    PlatformCRCTableReady = 1;
}

static ULONG PlatformCRC32(const ULONG *words, ULONG count)
{
    const UBYTE *bytes = (const UBYTE *)words;
    ULONG total = count * sizeof(ULONG);
    ULONG crc = 0xffffffffUL;
    ULONG index;

    if (!PlatformCRCTableReady)
        PlatformCRC32Init();
    for (index = 0; index < total; ++index)
        crc = (crc >> 8) ^
              PlatformCRCTable[(crc ^ bytes[index]) & 0xffUL];
    return ~crc;
}

/* A late completion only releases the outstanding reference. Its result is
 * discarded: the failed caller has already returned and must never be replayed.
 * No Exec calls, writes, or PCI polling loop in this nonblocking check. */
static BOOL PlatformMailboxReady(void)
{
    volatile MGLPPCMailboxV1 *box = FrontendPlatform.mailbox;
    if (!FrontendPlatform.mailboxOutstanding)
        return !FrontendPlatform.mailboxPoisoned;
    if (!box)
        return FALSE;
    MGLPPCInvalidateLine32((APTR)&box->ackSerial);
    if (box->ackSerial != FrontendPlatform.mailboxSerial)
        return FALSE;
    MGLPPCInvalidateLine32((APTR)box);
    if (box->magic != MGLPPC_MAILBOX_MAGIC ||
        box->version != MGLPPC_MAILBOX_VERSION ||
        box->epoch != FrontendPlatform.epoch)
        return FALSE;
    FrontendPlatform.mailboxOutstanding = FALSE;
    FrontendPlatform.mailboxPoisoned = FALSE;
    return TRUE;
}

static BOOL PlatformMailboxWait(void)
{
    ULONG start = PerfClock();
    ULONG interval = PlatformClockRate / 10000UL; /* at most 10 kHz PCI reads */
    ULONG last = start - interval;
    ULONG relax = 0;
#if MGLPPC_CLIENT_TRACE
    static ULONG mwWaits;
    ++mwWaits;
    if (mwWaits <= 8UL || (mwWaits & 4095UL) == 0UL)
        R200PPCTraceUL("mbox wait enter serial",
                       FrontendPlatform.mailboxSerial);
#endif
    for (;;) {
        ULONG now = PerfClock();
        if (now - last >= interval) {
            if (PlatformMailboxReady()) {
#if MGLPPC_CLIENT_TRACE
                R200PPCTraceUL("mbox wait ok serial",
                               FrontendPlatform.mailboxSerial);
#endif
                return TRUE;
            }
            last = now;
        }
        if (now - start >= PlatformClockRate * 5UL) {
            FrontendPlatform.mailboxPoisoned = TRUE;
            FrontendPlatform.mailboxAbandoned = TRUE;
            R200PPCConsolePrintf("RING mbox TIMEOUT serial=%lu\n",
                   (unsigned long)FrontendPlatform.mailboxSerial);
            R200PPCTraceUL("mbox TIMEOUT serial",
                           FrontendPlatform.mailboxSerial);
            return FALSE;
        }
        /* Yield rarely, on the same bounded budget as the ring wait
         * rework in HOST_LOADER_REGRESSION.md: a 68k-side sleep
         * (Delay(1), WaitTime(0,100)) quantizes to a 50 Hz tick, about
         * 20 ms of actual latency on this kernel, so yielding every 256
         * spins added 1-4 of those to every control round-trip. The 68k
         * host runs on a separate physical CPU and is never starved by
         * a PPC spin; the 100 us check interval already bounds the
         * mailbox register reads, and the eventual yield only trips for
         * a host that is genuinely not answering. */
        if (!(++relax & (MGLPPC_RING_YIELD_SPINS - 1UL)))
            MGLPPCYield();
    }
}

/* One funnel for every PlatformCall failure: a poisoned mailbox turns the
 * frame loop into a hot retry loop that must stay observable without
 * flooding the trace file. The console copy keeps the evidence visible in
 * a photograph even when the on-disk trace cannot be written. */
static BOOL PlatformCallFail(ULONG command, const char *why)
{
    static ULONG pcFails;
    /* A failed control may have changed native binding before its error or
     * lost reply. Only a subsequent acknowledged bind can re-establish it. */
    FrontendPlatform.nativeBindingKnown = FALSE;
    ++pcFails;
    if (pcFails <= 32UL || (pcFails & 8191UL) == 0UL) {
        R200PPCConsolePrintf("RING pcall FAIL cmd=%lu n=%lu (%s)\n",
               (unsigned long)command, (unsigned long)pcFails, why);
        R200PPCTraceUL2("pcall FAIL cmd", command, "n", pcFails);
        R200PPCTraceStr(why);
    }
    return FALSE;
}

static BOOL PlatformCall(ULONG command, ULONG contextId)
{
#if MGLPPC_PROFILE
    ULONG profileStart = PerfClock();
#endif
    BOOL result;

    if (command != MGLPPC_CMD_CP_SURFACE && command != MGLPPC_CMD_GET_INTEGER &&
        command != MGLPPC_CMD_BIND_TEXTURE && command != MGLPPC_CMD_SEMANTIC_BARRIER)
        FrontendPlatform.cpSurfaceCount = 0;

    FrontendPlatform.control.command = command;
    FrontendPlatform.control.contextId = contextId;
    FrontendPlatform.control.payloadWords = 0;
    FrontendPlatform.control.status = MGLPPC_STATUS_BUSY;
    if (!PlatformMailboxReady())
        return PlatformCallFail(command, "pcall notready");
    /* A late BIND/COMMIT can leave frontend mirrors stale. Completion permits
     * teardown, not rendering or an implicit retry of the abandoned upload. */
    if (FrontendPlatform.mailboxAbandoned &&
        command != MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS &&
        command != MGLPPC_CMD_DESTROY_CONTEXT &&
        command != MGLPPC_CMD_SHUTDOWN &&
        command != MGLPPC_CMD_SEMANTIC_BARRIER)
        return PlatformCallFail(command, "pcall abandoned");
    if (FrontendPlatform.mailbox &&
        MGLPPC_MAILBOX_COMMAND_ALLOWED(command)) {
        volatile MGLPPCMailboxV1 *box = FrontendPlatform.mailbox;
        ULONG index;
        if (contextId != FrontendPlatform.contextId || !contextId)
            return PlatformCallFail(command, "pcall ctxmismatch");
        MGLPPCInvalidateLine32((APTR)box);
        if (box->magic != MGLPPC_MAILBOX_MAGIC ||
            box->version != MGLPPC_MAILBOX_VERSION ||
            box->epoch != FrontendPlatform.epoch) {
            FrontendPlatform.mailboxPoisoned = TRUE;
            FrontendPlatform.mailboxAbandoned = TRUE;
            return PlatformCallFail(command, "pcall magic");
        }
        for (index = 0; index < 8; ++index)
            box->reqValue[index] = FrontendPlatform.control.value[index];
        MGLPPCFlushDataRange((APTR)&box->reqValue[0], 32);
        box->command = command;
        box->contextId = contextId;
        MGLPPCFlushDataRange((APTR)&box->reqSerial, 32);
        if (!++FrontendPlatform.mailboxSerial)
            FrontendPlatform.mailboxSerial = 1;
        FrontendPlatform.mailboxOutstanding = TRUE;
        box->reqSerial = FrontendPlatform.mailboxSerial;
        MGLPPCFlushDataRange((APTR)&box->reqSerial, 32);
        result = PlatformMailboxWait();
        if (result) {
            MGLPPCInvalidateLine32((APTR)&box->ackValue[0]);
            FrontendPlatform.control.status = (LONG)box->status;
            for (index = 0; index < 8; ++index)
                FrontendPlatform.control.value[index] = box->ackValue[index];
            result = FrontendPlatform.control.status == MGLPPC_STATUS_OK;
        } else {
            return PlatformCallFail(command, "pcall waittimeout");
        }
        /* A published timeout must NEVER fall through to Exec. */
    } else {
#if MGLPPC_CLIENT_TRACE
        static ULONG pcTransport;
        ++pcTransport;
        if (pcTransport <= 8UL)
            R200PPCTraceUL2("pcall transport cmd", command, "n", pcTransport);
#endif
        result = MGLPPCTransportCall(&FrontendPlatform.transport,
                                 &FrontendPlatform.control) &&
             FrontendPlatform.control.status == MGLPPC_STATUS_OK;
    }
#if MGLPPC_PROFILE
    /* Shared texture uploads are attributed to the chunk bucket by their
     * caller; counting them here too would double-charge the profile. */
    if (command != MGLPPC_CMD_SEMANTIC_BARRIER &&
        command != MGLPPC_CMD_TEXTURE_UPLOAD_SHARED) {
        PlatformProfile.controlCount++;
        PlatformProfile.controlTicks += PerfClock() - profileStart;
    }
#endif
    if (!result)
        return PlatformCallFail(command, "pcall status");
    return result;
}

const MGLPPCSemanticPlatformProfile *R200PPCPlatformProfile(void)
{
#if MGLPPC_PROFILE
    PlatformProfile.clockRate = PlatformClockRate;
    PlatformProfile.sessionTicks = PerfClock() - PlatformSessionStart;
    return &PlatformProfile;
#else
    static const MGLPPCSemanticPlatformProfile disabled;
    return &disabled;
#endif
}

ULONG R200PPCPlatformStallTicks(void)
{
#if MGLPPC_PROFILE
    return PlatformProfile.ringStallTicks;
#else
    return 0;
#endif
}

BOOL MGLPPCUsesHostEvents(void)
{
    return FrontendPlatform.ring && FrontendPlatform.hostEventsConfirmed;
}

BOOL MGLPPCPollEvent(MGLPPCEvent *event)
{
    volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
    ULONG tail, index;

    if (!event || !MGLPPCUsesHostEvents())
        return FALSE;
    MGLPPCInvalidateLine32((APTR)&control->eventHead);
    tail = control->eventTail;
    if (tail == control->eventHead)
        return FALSE;
    index = tail % MGLPPC_EVENT_SLOTS;
    MGLPPCInvalidateLine32((APTR)&control->eventClass[index]);
    MGLPPCInvalidateLine32((APTR)&control->eventCode[index]);
    event->eventClass = control->eventClass[index];
    event->code = (UWORD)control->eventCode[index];
    control->eventTail = tail + 1UL;
    MGLPPCFlushDataRange((APTR)&control->eventTail,
                         sizeof(control->eventTail));
    return TRUE;
}

static void CopyRecordToRingSlot(const ULONG *source, ULONG *destination,
                                 ULONG dwords)
{
    ULONG index;
    for (index = 0; index < dwords; ++index)
        destination[index] = source[index];
}

/* Per-client pre-context defaults. FrontendPlatform state survives a
 * partially failed close (PlatformClose early-returns while a context or
 * mailbox is stuck), so a second client admitted into the same DLL process
 * would otherwise inherit the previous client's mglChooseWindowMode /
 * mglChoosePixelDepth choices -- the "asked for fullscreen, got a window"
 * bug. Called from both dispatch admission paths. */
void R200PPCPlatformResetClientDefaults(void)
{
    FrontendPlatform.nativeBindingKnown = FALSE;
    FrontendPlatform.pixelDepth = 32;
    /* MiniGL's native default is fullscreen; only an explicit
     * mglChooseWindowMode(GL_TRUE) (the client's -window flag) opens a
     * window on the Workbench screen. */
    FrontendPlatform.windowMode = FALSE;
    FrontendPlatform.syncEnabled = 1;
    FrontendPlatform.syncSeq = 1;
}

BOOL R200PPCPlatformOpen(void)
{
    if (FrontendPlatform.opened)
        return TRUE;
    memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
#if MGLPPC_PROFILE
    memset(&PlatformProfile, 0, sizeof(PlatformProfile));
    PlatformProfileStart = PlatformProfileClock64();
#endif
    R200PPCPlatformResetClientDefaults();
    FrontendPlatform.peerProtocol = 15UL;
    /* The mirror borrows the host-created window, and the ABI-3 input
     * path (mglKeyFunc -> UpdateInputIDCMP, plus SetPointer/ClearPointer)
     * calls Intuition on it. The 68k native backend opens intuition in
     * its own open; the PPC frontend must do the same or the first
     * ModifyIDCMP jumps through the NULL base -- on WarpOS that surfaces
     * as "illegal run68k function execution" and MasterControl halts the
     * client mid-setup. */
    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary((STRPTR)"intuition.library", 39UL);
    if (!IntuitionBase)
        return FALSE;
    {
        UBYTE verbose[8];
        verbose[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_VERBOSE", verbose, (LONG)sizeof(verbose),
               GVF_GLOBAL_ONLY);
        FrontendConsoleVerbose = verbose[0] ? 1UL : 0UL;
    }
    if (!MGLPPCTransportOpen(&FrontendPlatform.transport))
        return FALSE;
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    if (!PlatformCall(MGLPPC_CMD_HELLO, 0) &&
        FrontendPlatform.control.status == MGLPPC_STATUS_BAD_VERSION) {
        /* A protocol-14 host rejects the HELLO outright; downgrade and
         * retry once so the older binary pair still runs. */
        FrontendPlatform.peerProtocol = 14UL;
        FrontendPlatform.transport.protocolVersion = 14UL;
        memset(&FrontendPlatform.control, 0,
               sizeof(FrontendPlatform.control));
        (void)PlatformCall(MGLPPC_CMD_HELLO, 0);
    }
    if (!(FrontendPlatform.control.value[3] & MGLPPC_CAP_SEMANTIC_INLINE)) {
        MGLPPCTransportClose(&FrontendPlatform.transport);
        memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
        return FALSE;
    }
    FrontendPlatform.capabilities = FrontendPlatform.control.value[3];
    FrontendPlatform.crcFirstOnly =
        (FrontendPlatform.capabilities & MGLPPC_CAP_CRC_FIRST_ONLY) != 0;
    FrontendPlatform.crcArmed = TRUE;
    PlatformClockRate = PerfClockRate();
#if MGLPPC_PROFILE
    PlatformSessionStart = PerfClock();
#endif
    FrontendPlatform.opened = TRUE;
    return TRUE;
}

/* No ordinary GL work starts until the single startup entry is acknowledged.
 * Timeout is terminal: retain shared storage instead of risking GPU reuse. */
static BOOL R200PPCIbSmokeAfterRegistration(void)
{
    volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
    volatile MGLPPCRingEntryV1 *entry;
    volatile ULONG *ibWords;
    ULONG *slotData;
    ULONG head, slot, index, start, polls;
    const char *failure = "unsupported";
    char line[160];

    if (!FrontendPlatform.ibSmokeRequested)
        return TRUE;
    if (!control || !FrontendPlatform.ibSmokeConfirmed ||
        FrontendPlatform.ibSegmentBytes < MGLPPC_RING_SEGMENT_BYTES ||
        (ULONG)control > 0xffffffffUL - MGLPPC_RING_SEGMENT_BYTES ||
        PlatformClockRate < 10000UL || PlatformClockRate > 0x7fffffffUL / 5UL)
        goto failed;
    failure = "registration";
    MGLPPCInvalidateLine32((APTR)&control->head);
    MGLPPCInvalidateLine32((APTR)&control->retired);
    head = control->head;
    if (control->magic != MGLPPC_RING_MAGIC ||
        control->version != MGLPPC_RING_CONTROL_VERSION ||
        control->epoch != FrontendPlatform.epoch ||
        control->slotCount != MGLPPC_RING_SLOTS ||
        control->slotDwords != MGLPPC_SEMANTIC_SLOT_DWORDS || control->error ||
        head || control->completed != head || control->retired != head)
        goto failed;
    slot = head % MGLPPC_RING_SLOTS;
    slotData = FrontendPlatform.ringSlots[slot];
    if (!slotData || control->slotAddrs[slot] != (ULONG)slotData)
        goto failed;
    sprintf(line, "IBPPC ready mode=%s ctrl=%08lx slot=%lu epoch=%lu\n",
            FrontendPlatform.ibSmokeDry ? "dry" : "gpu", (unsigned long)control,
            (unsigned long)slot, (unsigned long)FrontendPlatform.epoch);
    MGLPPCIBTrace("DH2:ibppc.log", line);

    ibWords = (volatile ULONG *)((UBYTE *)control + MGLPPC_IB_OFFSET);
    ibWords[0] = __builtin_bswap32(0x00000579UL); /* SCRATCH_REG1 packet */
    ibWords[1] = __builtin_bswap32(0xdeadbeefUL);
    for (index = 2; index < MGLPPC_IB_SMOKE_DWORDS; ++index)
        ibWords[index] = __builtin_bswap32(0x80000000UL);
    MGLPPCFlushDataRange((APTR)ibWords, MGLPPC_IB_SMOKE_DWORDS * sizeof(ULONG));
    (void)ibWords[MGLPPC_IB_SMOKE_DWORDS - 1UL];
    slotData[0] = MGLPPC_IB_OFFSET;
    slotData[1] = MGLPPC_IB_SMOKE_DWORDS;
    MGLPPCFlushDataRange((APTR)slotData, 2UL * sizeof(ULONG));
    entry = &control->entries[slot];
    entry->slot = slot;
    entry->dwords = 2UL;
    entry->generation = ++FrontendPlatform.ringPostSerial;
    entry->flags = MGLPPC_SEMANTIC_FLAG_INDIRECT;
    entry->crc = 0UL;
    MGLPPCFlushDataRange((APTR)entry, sizeof(*entry));
    control->head = head + 1UL;
    MGLPPCFlushDataRange((APTR)&control->head, sizeof(control->head));
    MGLPPCIBTrace("DH2:ibppc.log", "IBPPC published head=1 payload=8192,8\n");
    start = PerfClock();
    failure = "ack_timeout";
    for (polls = 0; polls < 250UL; ++polls) {
        MGLPPCInvalidateLine32((APTR)&control->completed);
        MGLPPCInvalidateLine32((APTR)&control->retired);
        if (control->error || control->epoch != FrontendPlatform.epoch) {
            failure = "host_error_or_epoch";
            break;
        }
        if (control->completed == head + 1UL && control->retired == head + 1UL) {
            MGLPPCIBTrace("DH2:ibppc.log", FrontendPlatform.ibSmokeDry ?
                         "IBPPC ack=1 mode=dry gpu_proof=0\n" :
                         "IBPPC ack=1 mode=gpu\n");
            return TRUE;
        }
        if (PerfClock() - start >= PlatformClockRate * 5UL)
            break;
        Delay(1);
    }
failed:
    FrontendPlatform.ibSmokeFailed = TRUE;
    sprintf(line, "IBPPC failed stage=%s stop=1 retained=1\n", failure);
    MGLPPCIBTrace("DH2:ibppc.log", line);
    return FALSE;
}

static BOOL ResolveCpSurface(void *user, ULONG token, struct MGLPPCEmitSurface *out)
{
    struct MiniGLR200Context *context = user;
    ULONG index;
    if (!token || !FrontendPlatform.cpEnabled || FrontendPlatform.cpFailed)
        return FALSE;
    for (index = 0; index < FrontendPlatform.cpSurfaceCount; ++index)
        if (FrontendPlatform.cpSurfaces[index].token == token &&
            FrontendPlatform.cpSurfaces[index].epoch == context->deviceEpoch) {
            *out = FrontendPlatform.cpSurfaces[index].surface;
            return TRUE;
        }
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = token;
    if (!PlatformCall(MGLPPC_CMD_CP_SURFACE, FrontendPlatform.contextId) ||
        FrontendPlatform.control.value[0] != GL_NO_ERROR ||
        FrontendPlatform.control.value[7] != context->deviceEpoch)
        return FALSE;
    out->cpuAddress = FrontendPlatform.control.value[1];
    out->gpuAddress = FrontendPlatform.control.value[2];
    out->pitch = FrontendPlatform.control.value[3];
    out->width = FrontendPlatform.control.value[4];
    out->height = FrontendPlatform.control.value[5];
    out->format = FrontendPlatform.control.value[6];
    if (FrontendPlatform.cpSurfaceCount == 16UL)
        FrontendPlatform.cpSurfaceCount = 0;
    index = FrontendPlatform.cpSurfaceCount++;
    FrontendPlatform.cpSurfaces[index].token = token;
    FrontendPlatform.cpSurfaces[index].epoch = context->deviceEpoch;
    FrontendPlatform.cpSurfaces[index].surface = *out;
    return TRUE;
}

static BOOL PublishCpSlot(struct MiniGLR200Context *context, struct SubmitSlot *slot)
{
    volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
    volatile MGLPPCRingEntryV1 *entry;
    volatile ULONG *destination;
    struct MGLPPCEmitVertices vertices;
    ULONG head, ringSlot, count, index, segmentIx = 0xffffffffUL;
    ULONG *staging, start = PerfClock(), spin = 0;
    const char *failure = "ring";
    if (!control || !FrontendPlatform.cpAddress ||
        FrontendPlatform.cpBytes < MGLPPC_CP_SEGMENT_BYTES)
        goto failed;
    head = control->head;
    /* Completion releases CPU metadata, retirement releases GPU-read slices. */
    for (;;) {
        if (!(spin & 255UL)) {
            MGLPPCInvalidateLine32((APTR)&control->head);
            MGLPPCInvalidateLine32((APTR)&control->retired);
        }
        if (control->error || control->epoch != FrontendPlatform.epoch)
            goto failed;
        if (head - control->retired < MGLPPC_RING_SLOTS)
            break;
        if (PerfClock() - start >= PlatformClockRate * 5UL || ++spin > 5000000UL)
            goto failed;
        if (!(spin & (MGLPPC_RING_YIELD_SPINS - 1UL)))
            MGLPPCYield();
    }
    ringSlot = head % MGLPPC_RING_SLOTS;
    staging = FrontendPlatform.ringSlots[ringSlot];
    if (!staging || control->slotAddrs[ringSlot] != (ULONG)staging)
        goto failed;
    memset(&vertices, 0, sizeof(vertices));
    if (slot->commitMode && slot->drawCount) {
        for (index = 0; index < context->segmentPoolCount; ++index)
            if (context->segmentPool[index].Id == context->segment.info.Id) {
                segmentIx = index;
                break;
            }
        if (segmentIx == 0xffffffffUL)
            goto failed;
        vertices.gpuAddress = context->segment.info.GpuAddress;
        vertices.bytes = context->segment.info.Bytes;
        vertices.drawCount = slot->drawCount;
        vertices.stateBatch = slot->stateBatch;
        vertices.offsets = slot->stateBatch ? (const ULONG *)slot->stateBatchDraws :
                                            slot->vertexOffsets;
    }
    failure = "emit_or_surface";
    count = MGLPPCEmitBatch(staging, slot->records, slot->recordCount,
                            vertices.drawCount ? &vertices : NULL,
                            ResolveCpSurface, context);
    if (!count || count > MGLPPC_CP_DWORDS)
        goto failed;
    /* Classic Radeon CP dispatch requires an even dword count (Linux
     * radeon_cp_dispatch_indirect); padding is part of the endian conversion. */
    if (count & 1UL)
        staging[count++] = 0x80000000UL;
    if (vertices.drawCount)
        MGLPPCFlushDataRange((APTR)((ULONG)context->segment.info.CpuAddress +
            context->buildSlot * SEGMENT_HALF_BYTES), slot->segmentCursor);
    destination = (volatile ULONG *)((UBYTE *)FrontendPlatform.cpAddress +
                                     ringSlot * MGLPPC_CP_SLICE_BYTES);
    for (index = 0; index < count; ++index)
        destination[index] = __builtin_bswap32(staging[index]);
    MGLPPCFlushDataRange((APTR)destination, count * sizeof(ULONG));
    (void)destination[count - 1UL];
    staging[0] = ringSlot * MGLPPC_CP_SLICE_BYTES;
    staging[1] = count;
    staging[2] = vertices.drawCount ? 1UL << segmentIx : 0;
    MGLPPCFlushDataRange(staging, 3UL * sizeof(ULONG));
    entry = &control->entries[ringSlot];
    entry->slot = ringSlot;
    entry->dwords = 3UL;
    entry->generation = ++FrontendPlatform.ringPostSerial;
    entry->flags = MGLPPC_SEMANTIC_FLAG_CP_EMIT;
    entry->crc = 0;
    MGLPPCFlushDataRange((APTR)entry, sizeof(*entry));
    control->syncEnabled = FrontendPlatform.syncEnabled;
    control->syncSeq = FrontendPlatform.syncSeq;
    MGLPPCFlushDataRange((APTR)&control->syncEnabled, 2UL * sizeof(ULONG));
    control->head = head + 1UL;
    MGLPPCFlushDataRange((APTR)&control->head, sizeof(control->head));
    if (vertices.drawCount)
        FrontendPlatform.segmentLastEntry[segmentIx] = FrontendPlatform.ringPostSerial;
    return TRUE;
failed:
    FrontendPlatform.cpFailed = TRUE;
    MGLPPCIBTrace("DH2:cpfail.log", failure);
    R200PPCConsolePrintf("CP publish failed: %s\n", failure);
    return FALSE;
}

BOOL R200PPCPlatformPublishSlot(struct MiniGLR200Context *context,
                                ULONG slotIndex)
{
    struct SubmitSlot *slot;
    MGLPPCMessage *message;
#if MGLPPC_PROFILE
    ULONG profileStart, crcStart, flushStart, stallStart;
    ULONG stallSleeps = 0, stallEntered = 0;
#endif
    ULONG spin;
    ULONG crc = 0;
    BOOL crcArmedThisPublish = FALSE;

    if (FrontendPlatform.ibSmokeFailed || FrontendPlatform.cpFailed || FrontendPlatform.mailboxAbandoned || !PlatformMailboxReady() ||
        context != &proofContext || !FrontendPlatform.registered ||
        slotIndex >= SUBMIT_SLOT_COUNT || FrontendPlatform.pending[slotIndex])
        return FALSE;
    slot = &context->submitSlots[slotIndex];
    if (!slot->recordCount || slot->recordCount > MGLPPC_SEMANTIC_SLOT_DWORDS)
        return FALSE;
    if (FrontendPlatform.cpEnabled)
        return PublishCpSlot(context, slot);
#if defined(R200_FRONTEND_LIBRARY)
    if (ReplayCapture.active)
        CaptureReplaySlot(context, slot, slotIndex);
#endif
#if MGLPPC_PROFILE
    profileStart = PerfClock();
#endif
    if (FrontendPlatform.ring) {
        volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
        volatile MGLPPCRingEntryV1 *entry;
        ULONG ringSlot, total, draws = 0, segmentIx = 0xFFFFFFFFUL;
#if MGLPPC_PROFILE || MGLPPC_CLIENT_TRACE
        static BOOL firstPublish = TRUE;
#endif
#if MGLPPC_PROFILE
        R200PPCTimelineMark(MGLPPC_TL_PUBLISH_ENTER, slotIndex, 0);
#endif
        if (slot->commitMode && slot->drawCount) {
            ULONG poolIndex;
            for (poolIndex = 0; poolIndex < context->segmentPoolCount;
                 ++poolIndex) {
                if (context->segmentPool[poolIndex].Id ==
                    context->segment.info.Id) {
                    segmentIx = poolIndex;
                    break;
                }
            }
            if (segmentIx == 0xFFFFFFFFUL) {
#if MGLPPC_PROFILE
                PlatformProfile.publishSendTicks +=
                    PerfClock() - profileStart;
#endif
                return FALSE;
            }
            draws = slot->drawCount;
        }
#if MGLPPC_PROFILE || MGLPPC_CLIENT_TRACE
        if (firstPublish) {
            firstPublish = FALSE;
            R200PPCConsolePrintf("RING publish0 slot=%lu dwords=%lu commit=%lu\n",
                   (unsigned long)slotIndex,
                   (unsigned long)slot->recordCount,
                   (unsigned long)draws);
            R200PPCTraceUL2("publish0 slot", slotIndex,
                            "draws", draws);
        }
#endif
        if (!FrontendPlatform.crcFirstOnly || FrontendPlatform.crcArmed)
            crcArmedThisPublish = TRUE;
#if MGLPPC_PROFILE
        stallStart = PerfClock();
#endif
        spin = 0;
#if MGLPPC_PROFILE
        stallSleeps = 0;
        stallEntered =
            (control->head - control->completed) >= control->slotCount;
        if (stallEntered)
            PlatformProfile.publishStallEnters++;
#endif
#if MGLPPC_CLIENT_TRACE
        {
            static ULONG pubStalls;
            ++pubStalls;
            if (pubStalls <= 8UL || (pubStalls & 2047UL) == 0UL)
                R200PPCTraceUL2("publish stall enter n", pubStalls,
                                "completed", control->completed);
        }
#endif
        while ((control->head - control->completed) >= control->slotCount) {
            if (!(spin & 255UL)) {
                MGLPPCInvalidateLine32((APTR)&control->head);
                /* A cached full-ring snapshot must not force a tick sleep
                 * after the host has already released space. */
                if (!control->error &&
                    (control->head - control->completed) < control->slotCount)
                    break;
            }
            /* Spend the bounded polling budget first: even a short WaitTime
             * request can cost a full kernel quantum after space is available.
             * Readiness, timeout and ownership checks remain in this loop. */
            if (spin && !(spin & (MGLPPC_RING_YIELD_SPINS - 1UL))) {
#if MGLPPC_PROFILE
                ++stallSleeps;
#endif
                MGLPPCYield();
            }
            if (control->error || ++spin > 5000000UL) {
                R200PPCConsolePrintf("RING publish=FAIL stall head=%lu completed=%lu "
                       "err=%lu spin=%lu\n",
                       (unsigned long)control->head,
                       (unsigned long)control->completed,
                       (unsigned long)control->error,
                       (unsigned long)spin);
                R200PPCTraceUL2("publish FAIL head", control->head,
                                "completed", control->completed);
#if MGLPPC_PROFILE
                PlatformProfile.publishSendTicks +=
                    PerfClock() - profileStart;
#endif
                return FALSE;
            }
        }
#if MGLPPC_PROFILE
        {
            ULONG stalled = PerfClock() - stallStart;
            PlatformProfile.ringStallTicks += stalled;
            PlatformProfile.publishStallTicks += stalled;
            PlatformProfile.publishStallSleeps += stallSleeps;
            if (stallEntered && !stallSleeps)
                PlatformProfile.publishStallNoSleep++;
        }
#endif
        ringSlot = control->head % control->slotCount;
        if (draws) {
            FrontendPlatform.ringSlots[ringSlot][0] = draws;
            FrontendPlatform.ringSlots[ringSlot][1] = segmentIx;
            CopyRecordToRingSlot(slot->records,
                                 FrontendPlatform.ringSlots[ringSlot] + 2UL,
                                 slot->recordCount);
            if (slot->stateBatch) {
                CopyRecordToRingSlot(
                    (const ULONG *)slot->stateBatchDraws,
                    FrontendPlatform.ringSlots[ringSlot] + 2UL +
                        slot->recordCount,
                    draws * (RADEON3D_STATE_BATCH_DRAW_V1_SIZE /
                             sizeof(ULONG)));
                total = slot->recordCount + draws * 2UL + 2UL;
            } else {
                CopyRecordToRingSlot(
                    slot->vertexOffsets,
                    FrontendPlatform.ringSlots[ringSlot] + 2UL +
                        slot->recordCount,
                    draws);
                total = slot->recordCount + draws + 2UL;
            }
        } else {
            CopyRecordToRingSlot(slot->records,
                                 FrontendPlatform.ringSlots[ringSlot],
                                 slot->recordCount);
            total = slot->recordCount;
        }
        if (draws)
            /* Vertex bytes went through the copyback aperture mapping;
             * push them to VRAM before the GPU can fetch them. */
            MGLPPCFlushDataRange(
                (APTR)((ULONG)context->segment.info.CpuAddress +
                       context->buildSlot * SEGMENT_HALF_BYTES),
                slot->segmentCursor);
#if MGLPPC_PROFILE
        flushStart = PerfClock();
#endif
        MGLPPCFlushDataRange(FrontendPlatform.ringSlots[ringSlot],
                             total * sizeof(ULONG));
#if MGLPPC_PROFILE
        PlatformProfile.publishFlushTicks += PerfClock() - flushStart;
#endif
        if (crcArmedThisPublish) {
#if MGLPPC_PROFILE
            crcStart = PerfClock();
#endif
            crc = PlatformCRC32(FrontendPlatform.ringSlots[ringSlot],
                                total);
#if MGLPPC_PROFILE
            PlatformProfile.publishCrcTicks += PerfClock() - crcStart;
#endif
            FrontendPlatform.crcArmed = FALSE;
        }
        entry = &control->entries[ringSlot];
        entry->slot = ringSlot;
        entry->dwords = total;
        entry->generation = ++FrontendPlatform.ringPostSerial;
        if (!entry->generation)
            entry->generation = ++FrontendPlatform.ringPostSerial;
        entry->flags = draws ? MGLPPC_SEMANTIC_FLAG_COMMIT : 0;
        if (draws && slot->stateBatch)
            entry->flags |= MGLPPC_SEMANTIC_FLAG_STATE_BATCH;
        entry->crc = crc;
        MGLPPCFlushDataRange((APTR)entry, sizeof(*entry));
        control->syncEnabled = FrontendPlatform.syncEnabled;
        control->syncSeq = FrontendPlatform.syncSeq;
        MGLPPCFlushDataRange((APTR)&control->syncEnabled,
                             2UL * sizeof(ULONG));
        control->head = control->head + 1;
        MGLPPCFlushDataRange((APTR)&control->head, sizeof(control->head));
        if (segmentIx != 0xFFFFFFFFUL)
            FrontendPlatform.segmentLastEntry[segmentIx] =
                FrontendPlatform.ringPostSerial;
#if MGLPPC_PROFILE
        R200PPCTimelineMark(MGLPPC_TL_PUBLISH_EXIT, total, draws);
        PlatformProfile.publishSendTicks += PerfClock() - profileStart;
        PlatformProfile.publishCount++;
#endif
        return TRUE;
    }
    if (!FrontendPlatform.crcFirstOnly || FrontendPlatform.crcArmed) {
#if MGLPPC_PROFILE
        crcStart = PerfClock();
#endif
        crc = PlatformCRC32(slot->records, slot->recordCount);
#if MGLPPC_PROFILE
        PlatformProfile.publishCrcTicks += PerfClock() - crcStart;
#endif
        FrontendPlatform.crcArmed = FALSE;
    }
    message = &FrontendPlatform.execute[slotIndex];
#if MGLPPC_PROFILE
    flushStart = PerfClock();
#endif
    MGLPPCFlushDataRange(slot->records, slot->recordCount * sizeof(ULONG));
#if MGLPPC_PROFILE
    PlatformProfile.publishFlushTicks += PerfClock() - flushStart;
#endif
    memset(message, 0, sizeof(*message));
    message->command = MGLPPC_CMD_EXECUTE_SEMANTIC_SLOT;
    message->contextId = FrontendPlatform.contextId;
    message->value[0] = slotIndex;
    message->value[1] = ++FrontendPlatform.generation[slotIndex];
    if (!message->value[1])
        message->value[1] = ++FrontendPlatform.generation[slotIndex];
    message->value[2] = slot->recordCount;
    message->value[4] = crc;
    message->value[5] = FrontendPlatform.epoch;
    if (!MGLPPCTransportSend(&FrontendPlatform.transport, message))
        return FALSE;
    FrontendPlatform.pending[slotIndex] = TRUE;
#if MGLPPC_PROFILE
    PlatformProfile.publishSendTicks += PerfClock() - profileStart;
    PlatformProfile.publishCount++;
#endif
    return TRUE;
}

BOOL R200PPCPlatformWaitSlot(struct MiniGLR200Context *context,
                            ULONG slotIndex)
{
    MGLPPCMessage *message;
#if MGLPPC_PROFILE
    ULONG waitStart, waitElapsed;
#endif
    BOOL ok;
    (void)context;
    if (slotIndex >= SUBMIT_SLOT_COUNT)
        return FALSE;
    if (FrontendPlatform.ring) {
        /* Ring publishes copy synchronously into the ring slot, so the
         * frontend build slot is reusable immediately. */
        return TRUE;
    }
    if (!FrontendPlatform.pending[slotIndex])
        return TRUE;
    message = &FrontendPlatform.execute[slotIndex];
#if MGLPPC_PROFILE
    waitStart = PerfClock();
#endif
    ok = MGLPPCTransportWait(&FrontendPlatform.transport, message);
#if MGLPPC_PROFILE
    waitElapsed = PerfClock() - waitStart;
    PlatformProfile.replyWaitCount++;
    PlatformProfile.replyWaitTicks += waitElapsed;
    if (waitElapsed > PlatformProfile.replyWaitMaxTicks)
        PlatformProfile.replyWaitMaxTicks = waitElapsed;
#endif
    FrontendPlatform.pending[slotIndex] = FALSE;
    return ok && message->status == MGLPPC_STATUS_OK;
}

BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *context, BOOL present)
{
#if MGLPPC_PROFILE
    ULONG profileStart, stallStart, drainStart;
    ULONG stallSleeps = 0, stallEntered = 0;
#endif
    ULONG spin;
    volatile MGLPPCRingControlV1 *control;
    BOOL result = TRUE;
    (void)context;
    if (!PlatformMailboxReady())
        return FALSE;
#if MGLPPC_PROFILE
    profileStart = PerfClock();
#endif
    control = FrontendPlatform.ring;
#if MGLPPC_PROFILE
    R200PPCTimelineMark(MGLPPC_TL_BARRIER_ENTER, present ? 1UL : 0UL, 0);
#endif
    if (control) {
#if MGLPPC_PROFILE || MGLPPC_CLIENT_TRACE
        static BOOL firstBarrier = TRUE;
        if (firstBarrier) {
            firstBarrier = FALSE;
            R200PPCConsolePrintf("RING barrier0 present=%d\n", present ? 1 : 0);
            R200PPCTraceUL("barrier0 present", present ? 1UL : 0UL);
        }
#endif
        if (present) {
            volatile MGLPPCRingEntryV1 *entry;
            ULONG ringSlot;
#if MGLPPC_CLIENT_TRACE
            static ULONG pwWaits;
            ++pwWaits;
            if (pwWaits <= 8UL || (pwWaits & 2047UL) == 0UL)
                R200PPCTraceUL2("present wait enter n", pwWaits,
                                "completed", control->completed);
#endif
#if MGLPPC_PROFILE
            stallStart = PerfClock();
#endif
            spin = 0;
#if MGLPPC_PROFILE
            stallSleeps = 0;
            stallEntered =
                (control->head - control->completed) >= control->slotCount;
            if (stallEntered)
                PlatformProfile.barrierStallEnters++;
#endif
            while ((control->head - control->completed) >=
                   control->slotCount) {
                if (!(spin & 255UL)) {
                    MGLPPCInvalidateLine32((APTR)&control->head);
                    if (!control->error &&
                        (control->head - control->completed) < control->slotCount)
                        break;
                }
                /* Same bounded polling/yield policy as the publish loop. */
                if (spin && !(spin & (MGLPPC_RING_YIELD_SPINS - 1UL))) {
#if MGLPPC_PROFILE
                    ++stallSleeps;
#endif
                    MGLPPCYield();
                }
                if (control->error || ++spin > 5000000UL) {
                    R200PPCConsolePrintf("RING present_wait_fail head=%lu completed=%lu "
                           "error=%lu spin=%lu slots=%lu\n",
                           (unsigned long)control->head,
                           (unsigned long)control->completed,
                           (unsigned long)control->error,
                           (unsigned long)spin,
                           (unsigned long)control->slotCount);
                    R200PPCTraceUL2("present_wait FAIL head", control->head,
                                    "completed", control->completed);
                    result = FALSE;
                    break;
                }
            }
#if MGLPPC_PROFILE
            {
                ULONG stalled = PerfClock() - stallStart;
                PlatformProfile.ringStallTicks += stalled;
                PlatformProfile.barrierStallTicks += stalled;
                PlatformProfile.barrierStallSleeps += stallSleeps;
                if (stallEntered && !stallSleeps)
                    PlatformProfile.barrierStallNoSleep++;
            }
#endif
            if (result) {
                ringSlot = control->head % control->slotCount;
                entry = &control->entries[ringSlot];
                entry->slot = ringSlot;
                entry->dwords = 0;
                entry->generation = ++FrontendPlatform.ringPostSerial;
                if (!entry->generation)
                    entry->generation = ++FrontendPlatform.ringPostSerial;
                entry->flags = MGLPPC_SEMANTIC_FLAG_PRESENT;
                entry->crc = 0;
                MGLPPCFlushDataRange((APTR)entry, sizeof(*entry));
                control->head = control->head + 1;
                MGLPPCFlushDataRange((APTR)&control->head,
                                     sizeof(control->head));
#if MGLPPC_PROFILE
                PlatformProfile.publishCount++;
#endif
            }
        }
        /* A ring present is ordered after this frame's submits and before the
         * next frame's submits. Let the PPC build ahead while the host drains
         * the fence and blits; explicit barriers still wait below. */
        if (result && !present) {
#if MGLPPC_CLIENT_TRACE
            static ULONG drainWaits;
            ++drainWaits;
            if (drainWaits <= 8UL || (drainWaits & 2047UL) == 0UL)
                R200PPCTraceUL2("barrier drain enter n", drainWaits,
                                "head", control->head);
#endif
#if MGLPPC_PROFILE
            drainStart = PerfClock();
#endif
            spin = 0;
#if MGLPPC_PROFILE
            stallSleeps = 0;
            stallEntered = control->completed != control->head;
            if (stallEntered)
                PlatformProfile.drainStallEnters++;
            R200PPCTimelineMark(MGLPPC_TL_DRAIN_ENTER, control->head, 0);
#endif
            while ((FrontendPlatform.cpEnabled ? control->retired : control->completed) != control->head) {
                if (!(spin & 255UL)) {
                    MGLPPCInvalidateLine32((APTR)&control->completed);
                    if (FrontendPlatform.cpEnabled)
                        MGLPPCInvalidateLine32((APTR)&control->retired);
                    if (!control->error &&
                        (FrontendPlatform.cpEnabled ? control->retired : control->completed) == control->head)
                        break;
                }
                /* Spin budget before the first sleep; see the publish loop. */
                if (spin && !(spin & (MGLPPC_RING_YIELD_SPINS - 1UL))) {
#if MGLPPC_PROFILE
                    ++stallSleeps;
#endif
                    MGLPPCYield();
                }
                if (control->error || ++spin > 5000000UL) {
                    result = FALSE;
                    R200PPCConsolePrintf("RING barrier=FAIL drain head=%lu "
                           "completed=%lu err=%lu spin=%lu\n",
                           (unsigned long)control->head,
                           (unsigned long)control->completed,
                           (unsigned long)control->error,
                           (unsigned long)spin);
                    R200PPCTraceUL2("barrier drain FAIL head", control->head,
                                    "completed", control->completed);
                    break;
                }
            }
#if MGLPPC_PROFILE
            R200PPCTimelineMark(MGLPPC_TL_DRAIN_EXIT, control->completed,
                                spin);
            PlatformProfile.drainTicks += PerfClock() - drainStart;
            PlatformProfile.drainStallSleeps += stallSleeps;
            if (stallEntered && !stallSleeps)
                PlatformProfile.drainStallNoSleep++;
#endif
        }
#if MGLPPC_PROFILE || MGLPPC_CLIENT_TRACE
        if (result && present) {
            static BOOL firstPresent = TRUE;
            if (firstPresent) {
                firstPresent = FALSE;
                R200PPCConsolePrintf("RING present0 ok head=%lu completed=%lu\n",
                       (unsigned long)control->head,
                       (unsigned long)control->completed);
                R200PPCTraceUL2("present0 ok head", control->head,
                                "completed", control->completed);
            }
        }
#endif
    } else {
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[3] =
            present ? MGLPPC_SEMANTIC_FLAG_PRESENT : 0;
        FrontendPlatform.control.value[5] = FrontendPlatform.epoch;
        result = PlatformCall(MGLPPC_CMD_SEMANTIC_BARRIER,
                              FrontendPlatform.contextId);
    }
#if MGLPPC_PROFILE
    R200PPCTimelineMark(MGLPPC_TL_BARRIER_EXIT, result ? 1UL : 0UL,
                        control ? control->completed : 0);
    PlatformProfile.barrierCount++;
    PlatformProfile.barrierTicks += PerfClock() - profileStart;
#endif
#if defined(R200_FRONTEND_LIBRARY)
    if (result && present && ReplayCapture.active) {
        ReplayCapture.frameCount++;
        if (ReplayCapture.frameCount <= ReplayCapture.frameCapacity)
            ReplayCapture.frames[ReplayCapture.frameCount].firstBatch =
                ReplayCapture.batchCount;
    }
#endif
    return result;
}

BOOL R200PPCPlatformRotateSegment(struct MiniGLR200Context *context,
                                  ULONG vertexBytes)
{
    volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
    ULONG next, spin = 0, index;
#if MGLPPC_PROFILE
    ULONG stallStart = 0, stallSleeps = 0, stallEntered = 0;
#endif
#if MGLPPC_CLIENT_TRACE
    static ULONG rotStalls;
#endif
    (void)vertexBytes;
    if (!context || !context->segmentPoolCount || !control)
        return FALSE;
    /* Retirement is host-owned. Refresh before selecting a segment, rather
     * than sleeping on an old snapshot when the whole pool is already free. */
    MGLPPCInvalidateLine32((APTR)&control->retired);
    /* Prefer the next segment in rotation order, but if it is not retired
     * yet, take ANY pool segment whose GPU reads have retired: with a pool
     * of four and one outstanding, another segment is almost always free,
     * and each fence round-trip otherwise costs ~0.3 s of spin. */
    for (index = 0; index < context->segmentPoolCount; ++index) {
        ULONG candidate = (context->segmentPoolNext + 1UL + index) %
                          context->segmentPoolCount;
        if (control->retired >=
            FrontendPlatform.segmentLastEntry[candidate]) {
            next = candidate;
            goto haveSegment;
        }
    }
    next = (context->segmentPoolNext + 1UL) % context->segmentPoolCount;
    /* Host consumption releases ring slots, but only a completed later fence
     * retires the GPU's ordered reads from a vertex segment. */
#if MGLPPC_CLIENT_TRACE
    ++rotStalls;
    if (rotStalls <= 8UL || (rotStalls & 2047UL) == 0UL)
        R200PPCTraceUL2("rotate enter n", rotStalls,
                        "retired", control->retired);
#endif
#if MGLPPC_PROFILE
    stallStart = PerfClock();
    stallSleeps = 0;
    stallEntered = 1;
    PlatformProfile.rotateStallEnters++;
    R200PPCTimelineMark(MGLPPC_TL_STALL_ENTER, next,
                        FrontendPlatform.segmentLastEntry[next]);
#endif
    while (control->retired < FrontendPlatform.segmentLastEntry[next]) {
        /* This spin used to be unyielding with dcbf+sync every iteration:
         * 5M bus transactions that starved the whole box right around the
         * segment wrap (frames 2-3), then failed silently. */
        if (!(spin & 255UL)) {
            MGLPPCInvalidateLine32((APTR)&control->retired);
            if (!control->error &&
                control->retired >= FrontendPlatform.segmentLastEntry[next])
                break;
        }
        /* Spin budget before the first sleep; see the publish loop. */
        if (spin && !(spin & (MGLPPC_RING_YIELD_SPINS - 1UL))) {
#if MGLPPC_PROFILE
            ++stallSleeps;
#endif
            MGLPPCYield();
        }
#if MGLPPC_PROFILE || MGLPPC_CLIENT_TRACE
        if (!(spin & 500000UL)) {
            R200PPCConsolePrintf("RING rotate spin retired=%lu need=%lu spin=%lu\n",
                   (unsigned long)control->retired,
                   (unsigned long)FrontendPlatform.segmentLastEntry[next],
                   (unsigned long)spin);
            R200PPCTraceUL2("rotate spin retired", control->retired,
                            "need", FrontendPlatform.segmentLastEntry[next]);
        }
#endif
        if (control->error || ++spin > 5000000UL) {
            R200PPCConsolePrintf("RING rotate=FAIL retired=%lu need=%lu spin=%lu\n",
                   (unsigned long)control->retired,
                   (unsigned long)FrontendPlatform.segmentLastEntry[next],
                   (unsigned long)spin);
            R200PPCTraceUL2("rotate FAIL retired", control->retired,
                            "need", FrontendPlatform.segmentLastEntry[next]);
#if MGLPPC_PROFILE
            PlatformProfile.rotateStallTicks += PerfClock() - stallStart;
            PlatformProfile.rotateStallSleeps += stallSleeps;
            R200PPCTimelineMark(MGLPPC_TL_STALL_EXIT, next, spin);
#endif
            return FALSE;
        }
    }
haveSegment:
#if MGLPPC_PROFILE
    /* The fast path jumps straight here without ever entering the wait, so
     * only a real stall may charge ticks or a no-sleep resolution. */
    if (stallEntered) {
        PlatformProfile.rotateStallTicks += PerfClock() - stallStart;
        PlatformProfile.rotateStallSleeps += stallSleeps;
        if (!stallSleeps)
            PlatformProfile.rotateStallNoSleep++;
        R200PPCTimelineMark(MGLPPC_TL_STALL_EXIT, next, spin);
    }
#endif
    context->segment.info = context->segmentPool[next];
    context->segment.valid = TRUE;
    context->segmentPoolNext = next;
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index)
        context->submitSlots[index].segmentCursor = 0;
    return TRUE;
}

void *R200PPCDispatchCreateContext(int offx, int offy, int width, int height)
{
    ULONG index;
    FrontendPlatform.nativeBindingKnown = FALSE;
    (void)offx;
    (void)offy;
    if (!FrontendPlatform.opened || R200PPCContext)
        return NULL;
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = (ULONG)width;
    FrontendPlatform.control.value[1] = (ULONG)height;
    FrontendPlatform.control.value[2] = (ULONG)FrontendPlatform.pixelDepth;
    /* Fullscreen creates are host-side: the 68k backend owns the screen
     * buffers and the flip/blit machine, and every record's target handle
     * is resolved there. The client only forwards the requested mode. */
    FrontendPlatform.control.value[3] =
        (ULONG)FrontendPlatform.windowMode;
    FrontendPlatform.control.value[4] =
        (ULONG)FrontendPlatform.vertexBufferSize;
    FrontendPlatform.control.value[5] =
        (ULONG)FrontendPlatform.textureBufferSize;
    FrontendPlatform.control.value[6] =
        (ULONG)FrontendPlatform.mtexBufferSize;
    FrontendPlatform.control.value[7] =
        (ULONG)FrontendPlatform.numberOfBuffers;
    R200PPCConsolePrintf("CLIENT create sent\n");
    R200PPCTraceOpen();
    R200PPCTraceUL2("create begin w", (ULONG)width, "h", (ULONG)height);
    R200PPCTraceUL2("create windowMode",
                    FrontendPlatform.windowMode ? 1UL : 0UL,
                    "depth", FrontendPlatform.pixelDepth);
    if (!PlatformCall(MGLPPC_CMD_CREATE_CONTEXT, 0) ||
        !FrontendPlatform.control.contextId ||
        FrontendPlatform.control.value[1] != (ULONG)width ||
        FrontendPlatform.control.value[2] != (ULONG)height ||
        !FrontendPlatform.control.value[3] ||
        !FrontendPlatform.control.value[4]) {
        R200PPCTraceStr("create FAIL");
        return NULL;
    }
    R200PPCTraceUL2("create ok ctx", FrontendPlatform.control.contextId,
                    "win", FrontendPlatform.control.value[0]);
    /* The host starts every context with its shadow unbound (host resets
     * semanticBoundTexture on create); the PPC mirror must agree. */
    FrontendPlatform.hostShadowTexture = 0;
    FrontendPlatform.hostShadowDirty = TRUE;

    memset(&proofContext, 0, sizeof(proofContext));
    proofContext.width = FrontendPlatform.control.value[1];
    proofContext.height = FrontendPlatform.control.value[2];
    proofContext.pixelDepth = (ULONG)FrontendPlatform.pixelDepth;
    proofContext.surfaces[0].Handle =
        (APTR)FrontendPlatform.control.value[3];
    proofContext.depthSurface.Handle =
        (APTR)FrontendPlatform.control.value[4];
    proofContext.radeonCaps = FrontendPlatform.control.value[5];
    proofContext.batchDwords = FrontendPlatform.control.value[6];
    proofContext.compactTclVertex =
        FrontendPlatform.control.value[7] != 0;
    if (!proofContext.batchDwords ||
        proofContext.batchDwords > MGLPPC_SEMANTIC_SLOT_DWORDS)
        proofContext.batchDwords = MGLPPC_SEMANTIC_SLOT_DWORDS;
    InitializeFrontendContext(&proofContext);
    R200PPCContext = &proofContext;
    FrontendPlatform.contextId = FrontendPlatform.control.contextId;
    FrontendPlatform.windowHandle =
        (APTR)FrontendPlatform.control.value[0];
    /* The ABI-3 main loop consumes IDCMP directly from the host-created
     * window. Keep the PPC context mirror wired to that shared Window so
     * legacy demos do not return from mglMainLoop without rendering. */
    proofContext.window = (struct Window *)FrontendPlatform.windowHandle;
    /* The mirror starts from the shared window's real IDCMP mask so a
     * later ModifyIDCMP (mglKeyFunc) cannot strip masks the client never
     * asked for -- notably IDCMP_CLOSEWINDOW, the clean exit path of the
     * ABI-3 main loop. */
    proofContext.baseIDCMP =
        proofContext.window ? proofContext.window->IDCMPFlags : 0;
    R200PPCTraceUL2("create mirror win", (ULONG)proofContext.window,
                    "idcmp", proofContext.baseIDCMP);

    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.ring = NULL;
    FrontendPlatform.mailbox = NULL;
    FrontendPlatform.mailboxSerial = 0;
    FrontendPlatform.mailboxOutstanding = FALSE;
    FrontendPlatform.mailboxPoisoned = FALSE;
    FrontendPlatform.mailboxAbandoned = FALSE;
    FrontendPlatform.ringPostSerial = 0;
    FrontendPlatform.ibSmokeConfirmed = FALSE;
    FrontendPlatform.ibSmokeDry = FALSE;
    FrontendPlatform.ibSmokeFailed = FALSE;
    FrontendPlatform.ibSegmentBytes = 0;
    FrontendPlatform.cpEnabled = FrontendPlatform.cpFailed = FALSE;
    FrontendPlatform.cpAddress = NULL;
    FrontendPlatform.cpBytes = FrontendPlatform.cpSurfaceCount = 0;
    {
        UBYTE flag[8];
        flag[0] = 0;
        FrontendPlatform.ibSmokeRequested =
            GetVar((CONST_STRPTR)"MGLPPC_IBSMOKE", flag, sizeof(flag),
                   GVF_GLOBAL_ONLY) > 0 && flag[0] == '1';
        flag[0] = 0;
        FrontendPlatform.cpRequested =
            GetVar((CONST_STRPTR)"MGLPPC_CPEMIT", flag, sizeof(flag),
                   GVF_GLOBAL_ONLY) > 0 && flag[0] == '1';
    }
    /* The host verifies the first Execute of EACH registration, not DLL open. */
    FrontendPlatform.crcArmed = TRUE;
    FrontendPlatform.hostEventsConfirmed = FALSE;
    if (FrontendPlatform.capabilities & MGLPPC_CAP_SEMANTIC_RING) {
        MGLPPCMessage *registerMessage =
            (MGLPPCMessage *)FrontendPlatform.registerMessageWords;
        BOOL ringOk = TRUE;
        BOOL ringAccepted = FALSE;
        R200PPCConsolePrintf("RING reg=begin slots=%lu dwords=%lu\n",
               (unsigned long)MGLPPC_RING_SLOTS,
               (unsigned long)MGLPPC_SEMANTIC_SLOT_DWORDS);
        R200PPCTraceStr("ring reg begin");
        for (index = 0; index < MGLPPC_RING_SLOTS; ++index) {
            /* Both CPUs maintain whole cache lines. Keep allocator metadata
             * and unrelated allocations out of every shared payload line. */
            FrontendPlatform.ringSlotAllocations[index] =
                AllocVec(MGLPPC_SEMANTIC_SLOT_DWORDS * sizeof(ULONG) + 31UL,
                         MEMF_PUBLIC);
            FrontendPlatform.ringSlots[index] =
                FrontendPlatform.ringSlotAllocations[index] ?
                (ULONG *)(((ULONG)FrontendPlatform.ringSlotAllocations[index] +
                           31UL) & ~31UL) : NULL;
            if (!FrontendPlatform.ringSlots[index])
                ringOk = FALSE;
        }
        if (ringOk) {
            memset(registerMessage, 0,
                   MGLPPC_MESSAGE_SIZE(MGLPPC_RING_SLOTS));
            registerMessage->command = MGLPPC_CMD_REGISTER_SEMANTIC_RING;
            registerMessage->contextId = FrontendPlatform.contextId;
            registerMessage->value[0] = MGLPPC_RING_SLOTS;
            registerMessage->value[1] = MGLPPC_SEMANTIC_SLOT_DWORDS;
#if !R200_FRONTEND_LIBRARY
            /* ABI-3 DLL clients poll the returned window's UserPort themselves;
             * the dispatch table has no private-ring event consumer. Only the
             * standalone frontend can opt into forwarding and drain it. */
            if (FrontendPlatform.capabilities & MGLPPC_CAP_HOST_EVENTS)
                registerMessage->value[3] =
                    MGLPPC_RING_REGISTER_HOST_EVENTS;
#endif
            registerMessage->payloadWords = MGLPPC_RING_SLOTS;
            if (FrontendPlatform.ibSmokeRequested &&
                (FrontendPlatform.capabilities & MGLPPC_CAP_INDIRECT))
                registerMessage->value[3] |= MGLPPC_RING_REGISTER_INDIRECT;
            if (FrontendPlatform.cpRequested &&
                (FrontendPlatform.capabilities & MGLPPC_CAP_CP_EMIT))
                registerMessage->value[3] |= MGLPPC_RING_REGISTER_CP_EMIT;
            if ((FrontendPlatform.capabilities & MGLPPC_CAP_MAILBOX_V1) &&
                FrontendPlatform.peerProtocol == MGLPPC_PROTOCOL_VERSION &&
                PlatformClockRate >= 10000UL &&
                PlatformClockRate <= 0x7fffffffUL / 5UL)
                registerMessage->value[3] |= MGLPPC_RING_REGISTER_MAILBOX;
            for (index = 0; index < MGLPPC_RING_SLOTS; ++index)
                registerMessage->payload[index] =
                    (ULONG)FrontendPlatform.ringSlots[index];
            ringOk =
                MGLPPCTransportCall(&FrontendPlatform.transport,
                                    registerMessage) &&
                registerMessage->status == MGLPPC_STATUS_OK;
            R200PPCTraceUL("ring reg transport", ringOk ? 1UL : 0UL);
            ringAccepted = ringOk;
            if (ringOk && registerMessage->value[0]) {
                /* A new lease can reuse the previous context's address.
                 * Do not merge old cached slot addresses into entry writes. */
                ULONG offset;
                for (offset = 0; offset < sizeof(MGLPPCRingControlV1);
                     offset += 32UL)
                    MGLPPCInvalidateLine32(
                        (APTR)(registerMessage->value[0] + offset));
            }
            ringOk = ringOk &&
                registerMessage->value[0] &&
                ((volatile MGLPPCRingControlV1 *)
                     registerMessage->value[0])->magic == MGLPPC_RING_MAGIC &&
                ((volatile MGLPPCRingControlV1 *)
                     registerMessage->value[0])->version ==
                    MGLPPC_RING_CONTROL_VERSION &&
                ((volatile MGLPPCRingControlV1 *)
                     registerMessage->value[0])->slotCount ==
                    MGLPPC_RING_SLOTS &&
                ((volatile MGLPPCRingControlV1 *)
                     registerMessage->value[0])->slotDwords ==
                    MGLPPC_SEMANTIC_SLOT_DWORDS;
            if (ringOk) {
                ULONG vertexCount = registerMessage->value[2];
                FrontendPlatform.hostEventsConfirmed =
                    (registerMessage->value[3] &
                     MGLPPC_RING_REGISTER_HOST_EVENTS) != 0;
                FrontendPlatform.ring = (volatile MGLPPCRingControlV1 *)
                    registerMessage->value[0];
                FrontendPlatform.epoch = registerMessage->value[1];
                FrontendPlatform.ibSmokeConfirmed =
                    (registerMessage->value[3] & MGLPPC_RING_REGISTER_INDIRECT) != 0;
                FrontendPlatform.ibSmokeDry =
                    (registerMessage->value[3] & MGLPPC_RING_REGISTER_IB_DRY) != 0;
                FrontendPlatform.ibSegmentBytes = registerMessage->value[4];
                FrontendPlatform.cpEnabled =
                    (registerMessage->value[3] & MGLPPC_RING_REGISTER_CP_EMIT) != 0;
                if (FrontendPlatform.cpEnabled) {
                    if (!registerMessage->value[5] || (registerMessage->value[5] & 31UL) ||
                        registerMessage->value[7] < MGLPPC_CP_SEGMENT_BYTES ||
                        registerMessage->value[5] > 0xffffffffUL - registerMessage->value[7] ||
                        vertexCount != MGLPPC_VERTEX_SEGMENTS ||
                        registerMessage->payloadWords != vertexCount * 4UL) {
                        FrontendPlatform.cpFailed = TRUE;
                        return NULL;
                    }
                    FrontendPlatform.cpAddress = (APTR)registerMessage->value[5];
                    FrontendPlatform.cpBytes = registerMessage->value[7];
                }
                if ((FrontendPlatform.capabilities & MGLPPC_CAP_MAILBOX_V1) &&
                    (registerMessage->value[3] & MGLPPC_RING_REGISTER_MAILBOX) &&
                    FrontendPlatform.peerProtocol == MGLPPC_PROTOCOL_VERSION &&
                    PlatformClockRate >= 10000UL &&
                    PlatformClockRate <= 0x7fffffffUL / 5UL &&
                    !(registerMessage->value[0] & 31UL) &&
                    registerMessage->value[4] >= MGLPPC_RING_SEGMENT_BYTES &&
                    registerMessage->value[0] <=
                        0xffffffffUL - registerMessage->value[4]) {
                    volatile MGLPPCMailboxV1 *box =
                        (volatile MGLPPCMailboxV1 *)(registerMessage->value[0] +
                                                    MGLPPC_MAILBOX_OFFSET);
                    MGLPPCInvalidateLine32((APTR)box);
                    MGLPPCInvalidateLine32((APTR)&box->reqSerial);
                    MGLPPCInvalidateLine32((APTR)&box->ackSerial);
                    if (box->magic == MGLPPC_MAILBOX_MAGIC &&
                        box->version == MGLPPC_MAILBOX_VERSION &&
                        box->epoch == FrontendPlatform.epoch &&
                        box->epoch == FrontendPlatform.ring->epoch &&
                        box->epoch && !box->reqSerial && !box->ackSerial)
                        FrontendPlatform.mailbox = box;
                }
                if (vertexCount > 4UL)
                    vertexCount = 4UL;
                for (index = 0; index < vertexCount; ++index) {
                    ULONG stride = FrontendPlatform.cpEnabled ? 4UL : 3UL;
                    proofContext.segmentPool[index].Size =
                        RADEON3D_SEGMENT_V1_SIZE;
                    proofContext.segmentPool[index].Version =
                        RADEON3D_SEGMENT_VERSION;
                    proofContext.segmentPool[index].CpuAddress =
                        (APTR)registerMessage->payload[index * stride];
                    proofContext.segmentPool[index].Bytes =
                        registerMessage->payload[index * stride + 1UL];
                    proofContext.segmentPool[index].Id =
                        registerMessage->payload[index * stride + 2UL];
                    proofContext.segmentPool[index].GpuAddress = FrontendPlatform.cpEnabled ?
                        registerMessage->payload[index * stride + 3UL] : 0;
                    FrontendPlatform.segmentLastEntry[index] = 0;
                }
                proofContext.segmentPoolCount = vertexCount;
                proofContext.segmentPoolNext = 0;
#if !defined(MGLPPC_DISABLE_STREAM)
                /* With naturally aligned frontend vertices, streaming cuts
                 * the gears host Execute bucket enough to more than double
                 * throughput. Keep an explicit inline fallback for A/B and
                 * recovery testing. */
                if (vertexCount) {
                    proofContext.segment.info =
                        proofContext.segmentPool[0];
                    proofContext.segment.valid = TRUE;
                    proofContext.streamSegments = TRUE;
                }
#endif
                 R200PPCConsolePrintf("RING reg=ok ctrl=%08lx epoch=%lu vseg=%lu\n",
                        (unsigned long)FrontendPlatform.ring,
                        (unsigned long)FrontendPlatform.epoch,
                        (unsigned long)vertexCount);
                 R200PPCTraceUL2("ring reg ok ctrl",
                                 (ULONG)FrontendPlatform.ring,
                                 "vseg", vertexCount);
    R200PPCTraceUL("ring reg mailbox",
                   FrontendPlatform.mailbox ? 1UL : 0UL);
    /* Park the render window on the right half of a 1920-wide screen so
     * the console window stays readable for photographed diagnostics. */
    if (proofContext.window) {
        MoveWindow(proofContext.window, 1260, 120);
    }
            }
        }
        if (!ringOk) {
            FrontendPlatform.mailbox = NULL;
            if (ringAccepted) {
                /* The host owns references even if reply validation fails.
                 * Do not free slot storage until it acknowledges unregister. */
                FrontendPlatform.epoch = registerMessage->value[1];
                FrontendPlatform.registered = TRUE;
                memset(&FrontendPlatform.control, 0,
                       sizeof(FrontendPlatform.control));
                FrontendPlatform.control.value[5] = FrontendPlatform.epoch;
                if (!PlatformCall(MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS,
                                  FrontendPlatform.contextId))
                    return NULL;
                FrontendPlatform.registered = FALSE;
            }
            R200PPCConsolePrintf("RING reg=fallback\n");
            for (index = 0; index < MGLPPC_RING_SLOTS; ++index) {
                if (FrontendPlatform.ringSlots[index]) {
                    FreeVec(FrontendPlatform.ringSlotAllocations[index]);
                    FrontendPlatform.ringSlotAllocations[index] = NULL;
                    FrontendPlatform.ringSlots[index] = NULL;
                }
            }
        }
    }
    if (!FrontendPlatform.ring) {
        FrontendPlatform.control.value[0] =
            (ULONG)proofContext.submitSlots[0].records;
        FrontendPlatform.control.value[1] =
            (ULONG)proofContext.submitSlots[1].records;
        FrontendPlatform.control.value[2] = MGLPPC_SEMANTIC_SLOT_DWORDS;
        if (!PlatformCall(MGLPPC_CMD_REGISTER_SEMANTIC_SLOTS,
                          FrontendPlatform.contextId)) {
            R200PPCContext = NULL;
            memset(&FrontendPlatform.control, 0,
                   sizeof(FrontendPlatform.control));
            (void)PlatformCall(MGLPPC_CMD_DESTROY_CONTEXT,
                               FrontendPlatform.contextId);
            FrontendPlatform.contextId = 0;
            return NULL;
        }
        FrontendPlatform.epoch = FrontendPlatform.control.value[0];
    }
    FrontendPlatform.registered = TRUE;
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index) {
        FrontendPlatform.generation[index] = 0;
        FrontendPlatform.pending[index] = FALSE;
    }
    if (FrontendPlatform.cpRequested && !FrontendPlatform.cpEnabled) {
        /* Do not benchmark a silent semantic fallback as PPC command emission. */
        R200PPCConsolePrintf("CP emitter unavailable; context rejected\n");
        SetError(&proofContext, GL_INVALID_OPERATION);
        return NULL;
    }
    if (!R200PPCIbSmokeAfterRegistration()) {
        SetError(&proofContext, GL_INVALID_OPERATION);
        return NULL;
    }
    return &proofContext;
}

void *R200PPCDispatchCreateContextFromID(GLint id, GLint *width, GLint *height)
{
    (void)id;
    (void)width;
    (void)height;
    return NULL;
}

/* Retained direct-texture shadow lifecycle (definitions live below
 * FrontendSourceToBGRA32). */
static void FrontendShadowFree(struct TextureObject *texture);
static BOOL FrontendShadowEnsure(struct MiniGLR200Context *context,
                                 struct TextureObject *texture,
                                 ULONG width, ULONG height);

void R200PPCDispatchDeleteContext(void)
{
    ULONG index;
    if (FrontendPlatform.ibSmokeFailed || FrontendPlatform.cpFailed ||
        (FrontendPlatform.cpEnabled && FrontendPlatform.ring && FrontendPlatform.ring->error))
        return; /* No reuse or teardown traffic after unacknowledged smoke work. */
    FrontendPlatform.nativeBindingKnown = FALSE;
    R200PPCTraceUL("delctx call ctx", (ULONG)R200PPCContext);
    if (!R200PPCContext) {
        R200PPCTraceStr("delctx skip (no ctx)");
        return;
    }
    R200PPCTraceStr("delctx enter");
    /* Retain ALL references if the host has not conclusively completed the
     * abandoned operation. A later DeleteContext/Close may try the drain again. */
    if (!PlatformMailboxReady() && !PlatformMailboxWait()) {
        R200PPCTraceStr("delctx FAIL mailbox drain");
        return;
    }
#if MGLPPC_PROFILE && R200_PERF_AUTOREPORT
    {
        const struct PerfMetrics *perf = &proofContext.perf;
        ULONG clockKHz = PlatformClockRate / 1000UL;
        printf("PPCPROF chunkCalls=%lu chunkMs=%lu ctrlCalls=%lu ctrlMs=%lu "
               "barrierCalls=%lu barrierMs=%lu clock=%lu\n",
               (unsigned long)PlatformProfile.chunkCount,
               clockKHz ? (unsigned long)(PlatformProfile.chunkTicks /
                                          clockKHz) : 0UL,
               (unsigned long)PlatformProfile.controlCount,
               clockKHz ? (unsigned long)(PlatformProfile.controlTicks /
                                          clockKHz) : 0UL,
               (unsigned long)PlatformProfile.barrierCount,
               clockKHz ? (unsigned long)(PlatformProfile.barrierTicks /
                                          clockKHz) : 0UL,
               (unsigned long)PlatformClockRate);
        printf("PPCPERF frames=%lu prim=%lu verts=%lu submitted=%lu "
               "submits=%lu recordDW=%lu clock=%lu\n",
               (unsigned long)perf->frames,
               (unsigned long)perf->primitives,
               (unsigned long)perf->inputVertices,
               (unsigned long)perf->submittedVertices,
               (unsigned long)perf->submits,
               (unsigned long)perf->recordDwords,
               (unsigned long)perf->clockRate);
        printf("PPCPERF_TIME frontend=%lu serialize=%lu execute=%lu wait=%lu "
               "present=%lu residency=%lu draw=%lu upload=%lu\n",
               (unsigned long)perf->frontendTicks,
               (unsigned long)perf->serializeTicks,
               (unsigned long)perf->executeTicks,
               (unsigned long)perf->waitTicks,
               (unsigned long)perf->presentTicks,
               (unsigned long)perf->textureResidencyTicks,
               (unsigned long)perf->drawTicks,
               (unsigned long)PlatformProfile.textureTicks);
        printf("PPCPERF_TEX calls=%lu ok=%lu bytes=%lu direct=%lu "
               "directTicks=%lu shared=%lu chunked=%lu barriers=%lu "
               "barrierTicks=%lu "
               "texWaits=%lu bufferWaits=%lu drainWaits=%lu\n",
               (unsigned long)PlatformProfile.textureCalls,
               (unsigned long)PlatformProfile.textureSuccesses,
               (unsigned long)PlatformProfile.textureBytes,
               (unsigned long)PlatformProfile.textureDirectCalls,
               (unsigned long)PlatformProfile.textureDirectTicks,
               (unsigned long)PlatformProfile.textureSharedCalls,
               (unsigned long)PlatformProfile.textureChunkedCalls,
               (unsigned long)PlatformProfile.textureBarrierCalls,
               (unsigned long)PlatformProfile.textureBarrierTicks,
               (unsigned long)perf->textureWaits,
               (unsigned long)perf->bufferWaits,
               (unsigned long)perf->drainWaits);
        if (perf->worstFrameCount) {
            const struct WorstFrameSlot *worst = &perf->worstFrames[0];
            printf("PPCPERF_WORST frame=%lu total=%lu serialize=%lu "
                   "execute=%lu wait=%lu present=%lu residency=%lu\n",
                   (unsigned long)worst->frameNumber,
                   (unsigned long)worst->ticks,
                   (unsigned long)worst->serializeTicks,
                   (unsigned long)worst->executeTicks,
                   (unsigned long)worst->waitTicks,
                   (unsigned long)worst->presentTicks,
                   (unsigned long)worst->residencyTicks);
        }
        fflush(stdout);
        /* The printf block above goes to the DLL's own console and never
         * reaches the client's redirect; mirror the essentials into the
         * trace file so a clean teardown leaves on-disk evidence. */
        R200PPCTraceUL2("SUMMARY frames", (ULONG)perf->frames,
                        "verts", (ULONG)perf->inputVertices);
        R200PPCTraceUL2("SUMMARY feTicks", (ULONG)perf->frontendTicks,
                        "serTicks", (ULONG)perf->serializeTicks);
        R200PPCTraceUL2("SUMMARY execTicks", (ULONG)perf->executeTicks,
                        "waitTicks", (ULONG)perf->waitTicks);
        R200PPCTraceUL2("SUMMARY presTicks", (ULONG)perf->presentTicks,
                        "drawTicks", (ULONG)perf->drawTicks);
        R200PPCTraceUL2("SUMMARY pubCount", (ULONG)PlatformProfile.publishCount,
                        "ringStall", (ULONG)PlatformProfile.ringStallTicks);
        R200PPCTraceUL2("SUMMARY drainTicks", (ULONG)PlatformProfile.drainTicks,
                        "pubFlush", (ULONG)PlatformProfile.publishFlushTicks);
        /* Stall split, mirrored into the trace so an aborted run that never
         * reaches the teardown report still leaves the sleep/no-sleep
         * evidence on disk. */
        R200PPCTraceUL2("SUMMARY pubStallEnter",
                        (ULONG)PlatformProfile.publishStallEnters,
                        "pubStallSleeps",
                        (ULONG)PlatformProfile.publishStallSleeps);
        R200PPCTraceUL2("SUMMARY pubStallNoSleep",
                        (ULONG)PlatformProfile.publishStallNoSleep,
                        "barStallEnter",
                        (ULONG)PlatformProfile.barrierStallEnters);
        R200PPCTraceUL2("SUMMARY barStallSleeps",
                        (ULONG)PlatformProfile.barrierStallSleeps,
                        "barStallNoSleep",
                        (ULONG)PlatformProfile.barrierStallNoSleep);
        R200PPCTraceUL2("SUMMARY drainStallEnter",
                        (ULONG)PlatformProfile.drainStallEnters,
                        "drainStallSleeps",
                        (ULONG)PlatformProfile.drainStallSleeps);
        R200PPCTraceUL2("SUMMARY rotStallEnter",
                        (ULONG)PlatformProfile.rotateStallEnters,
                        "rotStallSleeps",
                        (ULONG)PlatformProfile.rotateStallSleeps);
        R200PPCTraceUL2("SUMMARY rotStallNoSleep",
                        (ULONG)PlatformProfile.rotateStallNoSleep,
                        "rotStallTicks",
                        (ULONG)PlatformProfile.rotateStallTicks);
    }
#endif
#if defined(R200_FRONTEND_LIBRARY)
    MGLPPCSemanticReplayFree();
#endif
    /* Retained direct-texture shadows are PPC-side allocations tied to
     * this context; release them before the context struct is buried. */
    for (index = 0; index < MAX_TEXTURES; ++index)
        FrontendShadowFree(&proofContext.textures[index]);
    if (!proofContext.insideBegin && !FrontendPlatform.mailboxAbandoned) {
        if (!FlushScreenVertices(&proofContext) ||
            !SubmitAndWait(&proofContext)) {
            R200PPCTraceStr("delctx FAIL flush/submit");
            return;
        }
    }
    if (FrontendPlatform.registered &&
        !R200PPCPlatformBarrier(&proofContext, FALSE)) {
        R200PPCTraceStr("delctx FAIL barrier");
        return;
    }
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index)
        if (!R200PPCPlatformWaitSlot(&proofContext, index)) {
            R200PPCTraceUL("delctx FAIL waitslot", index);
            return;
        }
    if (FrontendPlatform.registered) {
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[5] = FrontendPlatform.epoch;
        if (!PlatformCall(MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS,
                          FrontendPlatform.contextId)) {
            R200PPCTraceStr("delctx FAIL unregister");
            return;
        }
        FrontendPlatform.mailbox = NULL;
        FrontendPlatform.ring = NULL;
        FrontendPlatform.registered = FALSE;
        /* Destroy may fail after unregister freed all host streaming segments.
         * Keep a retryable teardown tombstone, never a drawable stale context. */
        FrontendPlatform.mailboxAbandoned = TRUE;
        proofContext.streamSegments = FALSE;
        proofContext.segment.valid = FALSE;
        proofContext.segmentPoolCount = 0;
    }
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    if (!PlatformCall(MGLPPC_CMD_DESTROY_CONTEXT,
                      FrontendPlatform.contextId)) {
        R200PPCTraceStr("delctx FAIL destroy");
        return;
    }
    for (index = 0; index < MGLPPC_RING_SLOTS; ++index) {
        if (FrontendPlatform.ringSlots[index]) {
            FreeVec(FrontendPlatform.ringSlotAllocations[index]);
            FrontendPlatform.ringSlotAllocations[index] = NULL;
            FrontendPlatform.ringSlots[index] = NULL;
        }
    }
#if MGLPPC_PROFILE
    {
        const MGLPPCSemanticPlatformProfile *profile = R200PPCPlatformProfile();
        unsigned long long lifetimeTicks =
            PlatformProfileClock64() - PlatformProfileStart;
        FILE *report = fopen("DH2:mglprof/dll-profile.log", "w");

        /* Teardown only, after the final drain/destroy and before clearing the
         * context. Raw 32-bit counters overlap; never sum them or use the
         * legacy clock calibration for conversion. I/O failure is nonfatal. */
        if (report) {
            fprintf(report, "PPC_TRANSPORT_PROFILE scope=whole_context_including_setup "
                    "snapshot=after_destroy counters_since=platform_open "
                    "overlapping_nested=1 mutually_exclusive=0 "
                    "counter_bits=32 ticks=raw_timebase\n");
            fprintf(report, "PPC_TRANSPORT_CLOCK clock_hz_legacy=%lu "
                    "session_ticks=%lu clock_hz_legacy_trusted=0 "
                    "conversion=client_whole_run_tb_hz\n",
                    (unsigned long)profile->clockRate,
                    (unsigned long)profile->sessionTicks);
            /* Covers setup, teardown and recreated contexts, not just the
             * client's fixed frame loop or the post-HELLO legacy session. */
            fprintf(report, "PPC_TRANSPORT_LIFETIME elapsed_ticks_hi=%lu "
                    "elapsed_ticks_lo=%lu counter_wrap_risk=%lu "
                    "lifetime_since=profile_reset\n",
                    (unsigned long)(lifetimeTicks >> 32),
                    (unsigned long)(ULONG)lifetimeTicks,
                    (unsigned long)(lifetimeTicks > 0xffffffffULL));
            fprintf(report, "PPC_PUBLISH count=%lu send_ticks=%lu stall_ticks=%lu "
                    "flush_ticks=%lu crc_ticks=%lu\n",
                    (unsigned long)profile->publishCount,
                    (unsigned long)profile->publishSendTicks,
                    (unsigned long)profile->publishStallTicks,
                    (unsigned long)profile->publishFlushTicks,
                    (unsigned long)profile->publishCrcTicks);
            fprintf(report, "PPC_BARRIER count=%lu ticks=%lu stall_ticks=%lu "
                    "ring_stall_ticks=%lu drain_ticks=%lu\n",
                    (unsigned long)profile->barrierCount,
                    (unsigned long)profile->barrierTicks,
                    (unsigned long)profile->barrierStallTicks,
                    (unsigned long)profile->ringStallTicks,
                    (unsigned long)profile->drainTicks);
            fprintf(report, "PPC_REPLY_WAIT count=%lu ticks=%lu max_ticks=%lu\n",
                    (unsigned long)profile->replyWaitCount,
                    (unsigned long)profile->replyWaitTicks,
                    (unsigned long)profile->replyWaitMaxTicks);
            fprintf(report, "PPC_CONTROL count=%lu ticks=%lu\n",
                    (unsigned long)profile->controlCount,
                    (unsigned long)profile->controlTicks);
            fprintf(report, "PPC_CHUNK count=%lu ticks=%lu\n",
                    (unsigned long)profile->chunkCount,
                    (unsigned long)profile->chunkTicks);
            fprintf(report, "PPC_TEXTURE calls=%lu successes=%lu bytes=%lu "
                    "ticks=%lu direct_calls=%lu direct_ticks=%lu "
                    "shared_calls=%lu chunked_calls=%lu barrier_calls=%lu "
                    "barrier_ticks=%lu\n",
                    (unsigned long)profile->textureCalls,
                    (unsigned long)profile->textureSuccesses,
                    (unsigned long)profile->textureBytes,
                    (unsigned long)profile->textureTicks,
                    (unsigned long)profile->textureDirectCalls,
                    (unsigned long)profile->textureDirectTicks,
                    (unsigned long)profile->textureSharedCalls,
                    (unsigned long)profile->textureChunkedCalls,
                    (unsigned long)profile->textureBarrierCalls,
                    (unsigned long)profile->textureBarrierTicks);
            /* Ring back-pressure split: enters counts waits that found no
             * space, nosleep those that got space back from a cache refresh
             * alone, sleeps the number of short PPC-native yields taken.
             * The requested duration is reported explicitly; actual scheduling
             * latency can be longer. rotate_* waits on GPU retirement, not ring space,
             * and its ticks are NOT part of ring_stall_ticks. */
            fprintf(report, "PPC_STALL_SPLIT yield_us=%lu spin_budget=%lu publish_enters=%lu "
                    "publish_nosleep=%lu publish_sleeps=%lu "
                    "barrier_enters=%lu barrier_nosleep=%lu "
                    "barrier_sleeps=%lu drain_enters=%lu drain_nosleep=%lu "
                    "drain_sleeps=%lu rotate_enters=%lu rotate_nosleep=%lu "
                    "rotate_sleeps=%lu rotate_ticks=%lu\n",
                    (unsigned long)MGLPPC_RING_YIELD_US,
                    (unsigned long)MGLPPC_RING_YIELD_SPINS,
                    (unsigned long)profile->publishStallEnters,
                    (unsigned long)profile->publishStallNoSleep,
                    (unsigned long)profile->publishStallSleeps,
                    (unsigned long)profile->barrierStallEnters,
                    (unsigned long)profile->barrierStallNoSleep,
                    (unsigned long)profile->barrierStallSleeps,
                    (unsigned long)profile->drainStallEnters,
                    (unsigned long)profile->drainStallNoSleep,
                    (unsigned long)profile->drainStallSleeps,
                    (unsigned long)profile->rotateStallEnters,
                    (unsigned long)profile->rotateStallNoSleep,
                    (unsigned long)profile->rotateStallSleeps,
                    (unsigned long)profile->rotateStallTicks);
            {
                ULONG calls, ticks, maximum;
                MGLPPCYieldStats(&calls, &ticks, &maximum);
                fprintf(report, "PPC_YIELD scope=dll_lifetime requested_us=%lu "
                        "calls=%lu ticks=%lu max_ticks=%lu\n",
                        (unsigned long)MGLPPC_RING_YIELD_US, (unsigned long)calls,
                        (unsigned long)ticks, (unsigned long)maximum);
            }
            fclose(report);
        }
    }
#endif
    R200PPCContext = NULL;
    FrontendPlatform.registered = FALSE;
    FrontendPlatform.contextId = 0;
    FrontendPlatform.epoch = 0;
    FrontendPlatform.windowHandle = NULL;
    FrontendPlatform.mailboxAbandoned = FALSE;
    memset(&proofContext, 0, sizeof(proofContext));
    R200PPCTraceStr("delctx done");
    R200PPCTraceClose();
}

void R200PPCPlatformClose(void)
{
    R200PPCTraceStr("platclose enter");
    if (!FrontendPlatform.opened) {
        R200PPCTraceStr("platclose skip (not open)");
        return;
    }
    R200PPCDispatchDeleteContext();
    if (R200PPCContext || !PlatformMailboxReady()) {
        R200PPCTraceUL2("platclose stall ctx", (ULONG)R200PPCContext,
                        "mbok", PlatformMailboxReady() ? 1UL : 0UL);
        return;
    }
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    R200PPCTraceStr("platclose shutdown");
    (void)PlatformCall(MGLPPC_CMD_SHUTDOWN, 0);
    R200PPCTraceStr("platclose transport close");
    MGLPPCTransportClose(&FrontendPlatform.transport);
    if (FrontendPlatform.textureUploadBuffer)
        FreeVec(FrontendPlatform.textureUploadBuffer);
    memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
    R200PPCTraceStr("platclose done");
}

#if MGLPPC_PROFILE && R200_PERF_AUTOREPORT
static ULONG R200PPCOutsideTicks;
#endif

void R200PPCDispatchSwitchDisplay(void)
{
#if MGLPPC_PROFILE && R200_PERF_AUTOREPORT
    static ULONG lastLiveWall, lastLivePrintEnd;
    static ULONG lastFe, lastSer, lastExec, lastWait, lastPres;
    static ULONG lastDraw, lastStall, lastDrain, lastVerts, lastPrims;
    static ULONG lastSubmits, lastPublish;
#endif
    Lib_mglSwitchDisplay();
#if MGLPPC_PROFILE && R200_PERF_AUTOREPORT
    /* Wall time the client spent outside the present path: gear() GL calls
     * plus any 68k transitions the example makes (FPS overlay). */
    R200PPCOutsideTicks += PerfClock() - lastLivePrintEnd;
    lastLivePrintEnd = 0;
    if (R200PPCContext && R200PPCContext->perf.frames &&
        (R200PPCContext->perf.frames <= 10UL ||
         !(R200PPCContext->perf.frames % 30UL))) {
        const struct PerfMetrics *perf = &R200PPCContext->perf;
        ULONG wallNow = PerfClock();
        ULONG wall = wallNow - lastLiveWall;
        ULONG fe = perf->frontendTicks - lastFe;
        ULONG ser = perf->serializeTicks - lastSer;
        ULONG exec = perf->executeTicks - lastExec;
        ULONG wait = perf->waitTicks - lastWait;
        ULONG pres = perf->presentTicks - lastPres;
        ULONG draw = perf->drawTicks - lastDraw;
        ULONG stall = PlatformProfile.ringStallTicks - lastStall;
        ULONG drain = PlatformProfile.drainTicks - lastDrain;
        ULONG verts = perf->inputVertices - lastVerts;
        ULONG prims = perf->primitives - lastPrims;
        ULONG submits = perf->submits - lastSubmits;
        ULONG publish = PlatformProfile.publishCount - lastPublish;
        ULONG bucketSum = fe + ser + exec + wait + pres + draw + stall + drain;
        R200PPCConsolePrintf("PPCPERF_LIVE frames=%lu sessionTicks=%lu serialize=%lu "
               "execute=%lu wait=%lu present=%lu texCalls=%lu texOK=%lu "
               "texBytes=%lu texTicks=%lu direct=%lu shared=%lu "
               "chunked=%lu texBarriers=%lu texBarrierTicks=%lu\n",
               (unsigned long)perf->frames,
               (unsigned long)(wallNow - PlatformSessionStart),
               (unsigned long)perf->serializeTicks,
               (unsigned long)perf->executeTicks,
               (unsigned long)perf->waitTicks,
               (unsigned long)perf->presentTicks,
               (unsigned long)PlatformProfile.textureCalls,
               (unsigned long)PlatformProfile.textureSuccesses,
               (unsigned long)PlatformProfile.textureBytes,
               (unsigned long)PlatformProfile.textureTicks,
               (unsigned long)PlatformProfile.textureDirectCalls,
               (unsigned long)PlatformProfile.textureSharedCalls,
               (unsigned long)PlatformProfile.textureChunkedCalls,
               (unsigned long)PlatformProfile.textureBarrierCalls,
               (unsigned long)PlatformProfile.textureBarrierTicks);
        /* Per-frame delta line: wall minus every measured bucket is the
         * honest "outside the DLL" gap (example code + descheduling). */
        R200PPCConsolePrintf("PPCDELTA frames=%lu wall=%lu fe=%lu ser=%lu exec=%lu "
               "wait=%lu pres=%lu draw=%lu stall=%lu drain=%lu "
               "verts=%lu prims=%lu submits=%lu pub=%lu outside=%lu "
               "gap=%lu\n",
               (unsigned long)perf->frames,
               (unsigned long)wall,
               (unsigned long)fe,
               (unsigned long)ser,
               (unsigned long)exec,
               (unsigned long)wait,
               (unsigned long)pres,
               (unsigned long)draw,
               (unsigned long)stall,
               (unsigned long)drain,
               (unsigned long)verts,
               (unsigned long)prims,
               (unsigned long)submits,
               (unsigned long)publish,
               (unsigned long)R200PPCOutsideTicks,
               (unsigned long)(wall > bucketSum ? wall - bucketSum : 0UL));
        lastFe = perf->frontendTicks;
        lastSer = perf->serializeTicks;
        lastExec = perf->executeTicks;
        lastWait = perf->waitTicks;
        lastPres = perf->presentTicks;
        lastDraw = perf->drawTicks;
        lastStall = PlatformProfile.ringStallTicks;
        lastDrain = PlatformProfile.drainTicks;
        lastVerts = perf->inputVertices;
        lastPrims = perf->primitives;
        lastSubmits = perf->submits;
        lastPublish = PlatformProfile.publishCount;
        R200PPCOutsideTicks = 0;
        lastLiveWall = wallNow;
        fflush(stdout);
        /* Charge nothing here; the NEXT live report's outside= carries the
         * printf cost via lastLivePrintEnd below. */
        lastLivePrintEnd = PerfClock();
    }
    if (!lastLivePrintEnd)
        lastLivePrintEnd = PerfClock();
#endif
}
void *R200PPCDispatchGetWindowHandle(void)
{ return FrontendPlatform.windowHandle; }
GLboolean R200PPCDispatchLockDisplay(void)
{ return R200PPCContext ? GL_TRUE : GL_FALSE; }
void R200PPCDispatchUnlockDisplay(void) {}
GLboolean R200PPCDispatchLockBack(MGLLockInfo *info)
{ (void)info; SetError(R200PPCContext, GL_INVALID_OPERATION); return GL_FALSE; }
void R200PPCDispatchChoosePixelDepth(int depth)
{
    if (R200PPCContext)
        return;
    if (depth == 15)
        depth = 16;
    else if (depth == 24)
        depth = 32;
    FrontendPlatform.pixelDepth = depth;
}

void R200PPCDispatchEnableSync(GLboolean enabled)
{
    /* The semantic protocol has no EnableSync command; the present mode
     * (blit vs vblank-locked flip) is decided by the host's context, so
     * the state is published through the ring control block and applied
     * by the host on the next slot it consumes. */
    FrontendPlatform.syncEnabled = enabled != 0;
    FrontendPlatform.syncSeq++;
    if (R200PPCContext)
        R200PPCContext->syncEnabled = enabled != 0;
}
void R200PPCDispatchChooseWindowMode(GLboolean enabled)
{ if (!R200PPCContext) FrontendPlatform.windowMode = enabled != 0; }
void R200PPCDispatchChooseVertexBufferSize(int size)
{ if (!R200PPCContext) FrontendPlatform.vertexBufferSize = size; }
void R200PPCDispatchChooseTextureBufferSize(int size)
{ if (!R200PPCContext) FrontendPlatform.textureBufferSize = size; }
void R200PPCDispatchChooseMtexBufferSize(int size)
{ if (!R200PPCContext) FrontendPlatform.mtexBufferSize = size; }
void R200PPCDispatchChooseNumberOfBuffers(int count)
{ if (!R200PPCContext) FrontendPlatform.numberOfBuffers = count; }

static BOOL FrontendTextureBarrier(struct MiniGLR200Context *context)
{
#if MGLPPC_PROFILE
    ULONG start = PerfClock();
    PlatformProfile.textureBarrierCalls++;
#endif
    if (!PlatformMailboxReady() || FrontendPlatform.mailboxAbandoned ||
        !context || context->insideBegin || !FlushScreenVertices(context) ||
        !SubmitAndWait(context) || !R200PPCPlatformBarrier(context, FALSE)) {
#if MGLPPC_PROFILE
        PlatformProfile.textureBarrierTicks += PerfClock() - start;
#endif
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
#if MGLPPC_PROFILE
    PlatformProfile.textureBarrierTicks += PerfClock() - start;
#endif
    return TRUE;
}

static void FrontendSetTransportError(struct MiniGLR200Context *context)
{
    GLenum error = FrontendPlatform.control.status == MGLPPC_STATUS_GL_ERROR &&
                   FrontendPlatform.control.value[0] != GL_NO_ERROR
        ? (GLenum)FrontendPlatform.control.value[0] : GL_INVALID_OPERATION;
    SetError(context, error);
}

void R200PPCDispatchGenTextures(GLsizei count, GLuint *textures)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLsizei done = 0;
    if (!StateChangeAllowed(context)) return;
    if (count < 0 || (!textures && count)) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    while (done < count) {
        ULONG chunk = (ULONG)(count - done), index;
        if (chunk > 8UL) chunk = 8UL;
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] = chunk;
        if (!PlatformCall(MGLPPC_CMD_GEN_TEXTURES,
                          FrontendPlatform.contextId)) {
            FrontendSetTransportError(context);
            return;
        }
        for (index = 0; index < chunk; ++index) {
            GLuint name = FrontendPlatform.control.value[index];
            if (!name || name >= MAX_TEXTURES ||
                context->textures[name].generated) {
                SetError(context, GL_INVALID_OPERATION);
                return;
            }
            context->textures[name].generated = TRUE;
            textures[done + (GLsizei)index] = name;
        }
        done += (GLsizei)chunk;
    }
}

void R200PPCDispatchBindTexture(GLenum target, GLuint texture)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (target != GL_TEXTURE_2D) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (texture >= MAX_TEXTURES) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (context->activeTexture != 0) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (context->boundTexture[0] == texture &&
        (!texture || context->textures[texture].generated)) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    /* Texture state reaches the GPU inside every draw record's header
     * (surface token, size and filter dwords are baked in at draw-build
     * time), so a render-path bind needs no host round-trip. The 68k
     * shadow binding is consumed only by the upload completers; they
     * flush the deferred bind through HostTextureShadowSync() before
     * their mailbox command is PREPARED -- the flush must not run after
     * this wrapper's own control.value[] is set up, because a PlatformCall
     * both reads and overwrites that shared buffer. */
    if (FrontendPlatform.hostShadowTexture != texture)
        FrontendPlatform.hostShadowDirty = TRUE;
    Lib_glBindTexture(target, texture);
}

static BOOL HostTextureShadowSync(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLuint bound;

    if (!context || !FrontendPlatform.opened ||
        !FrontendPlatform.registered || !FrontendPlatform.contextId)
        return TRUE;
    bound = context->boundTexture[0];
    if (!FrontendPlatform.hostShadowDirty &&
        FrontendPlatform.hostShadowTexture == bound)
        return TRUE;
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = GL_TEXTURE_2D;
    FrontendPlatform.control.value[1] = bound;
    if (!PlatformCall(MGLPPC_CMD_BIND_TEXTURE, FrontendPlatform.contextId)) {
        FrontendSetTransportError(context);
        return FALSE;
    }
    FrontendPlatform.hostShadowTexture = bound;
    FrontendPlatform.hostShadowDirty = FALSE;
    return TRUE;
}

static ULONG FrontendTextureLayout(struct MiniGLR200Context *context,
                                   GLsizei width, GLsizei height,
                                   GLenum format, GLenum type,
                                   ULONG *rowBytes, ULONG *sourceStride,
                                   ULONG *sourceOffset)
{
    ULONG components, alignment, sourceWidth;
    ULONG skipRowBytes, skipPixelBytes;
    ULONG w = (ULONG)width, h = (ULONG)height;
    if (width <= 0 || height <= 0) return 0;
    /* components is bytes per texel: 3/4 for byte sources, 2 for the
     * 16-bit halfword sources the game's present path uploads, and the
     * 1/2-byte luminance-class sources the native backend accepts. */
    if (type == GL_UNSIGNED_BYTE) {
        if (format == GL_RGB) components = 3;
        else if (format == GL_RGBA || format == MGL_UBYTE_BGRA ||
                 format == MGL_UBYTE_ARGB) components = 4;
        else if (format == GL_LUMINANCE_ALPHA) components = 2;
        else if (format == GL_ALPHA || format == GL_LUMINANCE ||
                 format == GL_COLOR_INDEX) components = 1;
        else return 0;
    } else if ((type == MGL_UNSIGNED_SHORT_5_6_5 && format == GL_RGB) ||
               (type == MGL_UNSIGNED_SHORT_4_4_4_4 && format == GL_RGBA)) {
        components = 2;
    } else {
        return 0;
    }
    if (w > 0xffffffffUL / h || w * h > 0xffffffffUL / components) return 0;
    *rowBytes = w * components;
    sourceWidth = context->unpackRowLength
        ? context->unpackRowLength : w;
    if (sourceWidth > 0xffffffffUL / components)
        return 0;
    alignment = context->unpackAlignment;
    if (alignment != 1UL && alignment != 2UL &&
        alignment != 4UL && alignment != 8UL)
        return 0;
    if (sourceWidth * components > 0xffffffffUL - (alignment - 1UL)) return 0;
    *sourceStride = (sourceWidth * components + alignment - 1UL) &
                    ~(alignment - 1UL);
    if (context->unpackSkipRows &&
        *sourceStride > 0xffffffffUL / context->unpackSkipRows)
        return 0;
    skipRowBytes = context->unpackSkipRows * *sourceStride;
    if (context->unpackSkipPixels > 0xffffffffUL / components)
        return 0;
    skipPixelBytes = context->unpackSkipPixels * components;
    if (skipRowBytes > 0xffffffffUL - skipPixelBytes)
        return 0;
    *sourceOffset = skipRowBytes + skipPixelBytes;
    return *rowBytes * h;
}

static void FrontendCopyTextureBytes(UBYTE *destination, const UBYTE *source,
                                     ULONG offset, ULONG bytes,
                                     ULONG rowBytes, ULONG sourceStride)
{
    while (bytes) {
        ULONG row = offset / rowBytes;
        ULONG column = offset - row * rowBytes;
        ULONG span = rowBytes - column;
        if (span > bytes) span = bytes;
        memcpy(destination, source + row * sourceStride + column, span);
        destination += span;
        offset += span;
        bytes -= span;
    }
}

static BOOL FrontendEnsureTextureUploadBuffer(ULONG bytes)
{
    UBYTE *buffer;

    /* A timed-out upload/readback may still be accessing this allocation. */
    if (!PlatformMailboxReady() || FrontendPlatform.mailboxAbandoned)
        return FALSE;
    if (FrontendPlatform.textureUploadCapacity >= bytes)
        return TRUE;
    buffer = (UBYTE *)AllocVec(bytes, MEMF_PUBLIC);
    if (!buffer)
        return FALSE;
    if (FrontendPlatform.textureUploadBuffer)
        FreeVec(FrontendPlatform.textureUploadBuffer);
    FrontendPlatform.textureUploadBuffer = buffer;
    FrontendPlatform.textureUploadCapacity = bytes;
    return TRUE;
}

void R200PPCDispatchReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                               GLenum format, GLenum type, GLvoid *pixels)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG rowBytes, stride, offset, bytes, row;
    UBYTE *shared;
    if (!StateChangeAllowed(context)) return;
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (FrontendPlatform.peerProtocol < 15UL ||
        !(FrontendPlatform.capabilities & MGLPPC_CAP_READ_PIXELS_V1) ||
        !PlatformMailboxReady() || FrontendPlatform.mailboxAbandoned) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!width || !height) return;
    if (!pixels || !MGLPPCTextureSpan(width, height, 4,
            context->packAlignment, context->packRowLength,
            context->packSkipRows, context->packSkipPixels, (ULONG)pixels,
            &rowBytes, &stride, &offset, &bytes)) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (bytes > MGLPPC_READ_PIXELS_MAX_BYTES) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (x < 0 || y < 0 || (ULONG)x > context->width ||
        (ULONG)y > context->height || (ULONG)width > context->width - (ULONG)x ||
        (ULONG)height > context->height - (ULONG)y) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (!FrontendTextureBarrier(context)) return;
    /* Keep both boundary cache lines inside the retained allocation. */
    if (!FrontendEnsureTextureUploadBuffer(bytes + 63UL)) {
        SetError(context, GL_OUT_OF_MEMORY);
        return;
    }
    shared = (UBYTE *)(((ULONG)FrontendPlatform.textureUploadBuffer + 31UL) & ~31UL);
    MGLPPCFlushDataRange(shared, bytes);
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = x;
    FrontendPlatform.control.value[1] = y;
    FrontendPlatform.control.value[2] = width;
    FrontendPlatform.control.value[3] = height;
    FrontendPlatform.control.value[4] = format;
    FrontendPlatform.control.value[5] = type;
    FrontendPlatform.control.value[6] = (ULONG)shared;
    FrontendPlatform.control.value[7] = bytes;
    if (!PlatformCall(MGLPPC_CMD_READ_PIXELS_SHARED, FrontendPlatform.contextId)) {
        FrontendSetTransportError(context);
        return;
    }
    /* These lines stayed clean while the host owned them. dcbf invalidates
     * without writing stale data back; sync completes before the CPU reads. */
    MGLPPCFlushDataRange(shared, bytes);
    for (row = 0; row < (ULONG)height; ++row)
        memcpy((UBYTE *)pixels + offset + row * stride,
               shared + row * rowBytes, rowBytes);
}

/* Full-frame screenshot via the command-27 readback path. The frame is
 * fetched in row bands (the shared staging buffer is capped), converted
 * RGBA->RGB PPC-side, and written as binary PPM (P6) through stdio.
 * Rows are read in GL order (bottom-up) but written top-to-bottom, so
 * each band is walked backwards; the band at GL y is the PPM row span
 * [height-y-rows, height-y). The 68k implementation (r200_surface.c)
 * reads its own surface directly; this frontend variant exists because
 * the DLL never owns the drawable. */
void R200PPCDispatchWriteShotPPM(char *filename)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG bandRows, bandBytes, width, height, top, row;
    UBYTE *shared;
    UBYTE *rowBuffer;
    FILE *file;

    if (!context || !filename)
        return;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (FrontendPlatform.peerProtocol < 15UL ||
        !(FrontendPlatform.capabilities & MGLPPC_CAP_READ_PIXELS_V1) ||
        !PlatformMailboxReady() || FrontendPlatform.mailboxAbandoned) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    width = context->width;
    height = context->height;
    /* A single row must fit the staging cap for banding to work. */
    if (!width || !height || width > MGLPPC_READ_PIXELS_MAX_BYTES / 4UL) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    bandRows = MGLPPC_READ_PIXELS_MAX_BYTES / (width * 4UL);
    if (bandRows > height)
        bandRows = height;
    bandBytes = width * 4UL * bandRows;
    if (!FrontendTextureBarrier(context)) return;
    /* Keep both boundary cache lines inside the retained allocation. */
    if (!FrontendEnsureTextureUploadBuffer(bandBytes + 63UL)) {
        SetError(context, GL_OUT_OF_MEMORY);
        return;
    }
    shared = (UBYTE *)(((ULONG)FrontendPlatform.textureUploadBuffer + 31UL)
                       & ~31UL);
    rowBuffer = AllocVec(width * 3UL, MEMF_PUBLIC);
    if (!rowBuffer) {
        SetError(context, GL_OUT_OF_MEMORY);
        return;
    }
    file = fopen(filename, "wb");
    if (!file) {
        FreeVec(rowBuffer);
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (fprintf(file, "P6\n%lu %lu\n255\n",
                (unsigned long)width, (unsigned long)height) <= 0)
        goto write_failed;
    for (top = 0; top < height; top += bandRows) {
        ULONG rows = height - top < bandRows ? height - top : bandRows;
        ULONG yGL = height - top - rows; /* lowest GL row of the band */
        ULONG readBytes = width * 4UL * rows;
        memset(&FrontendPlatform.control, 0,
               sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] = 0UL;
        FrontendPlatform.control.value[1] = yGL;
        FrontendPlatform.control.value[2] = width;
        FrontendPlatform.control.value[3] = rows;
        FrontendPlatform.control.value[4] = GL_RGBA;
        FrontendPlatform.control.value[5] = GL_UNSIGNED_BYTE;
        FrontendPlatform.control.value[6] = (ULONG)shared;
        FrontendPlatform.control.value[7] = readBytes;
        MGLPPCFlushDataRange(shared, readBytes);
        if (!PlatformCall(MGLPPC_CMD_READ_PIXELS_SHARED,
                          FrontendPlatform.contextId)) {
            FrontendSetTransportError(context);
            goto write_failed;
        }
        MGLPPCFlushDataRange(shared, readBytes);
        /* Shared rows are GL-ordered; the PPM wants the band reversed. */
        for (row = rows; row-- > 0;) {
            const UBYTE *src = shared + row * width * 4UL;
            ULONG x;
            for (x = 0; x < width; ++x) {
                rowBuffer[x * 3UL + 0] = src[x * 4UL + 0];
                rowBuffer[x * 3UL + 1] = src[x * 4UL + 1];
                rowBuffer[x * 3UL + 2] = src[x * 4UL + 2];
            }
            if (fwrite(rowBuffer, 1UL, width * 3UL, file) != width * 3UL)
                goto write_failed;
        }
    }
    if (fclose(file) != 0) {
        FreeVec(rowBuffer);
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    FreeVec(rowBuffer);
    return;
write_failed:
    fclose(file);
    FreeVec(rowBuffer);
    if (!context->error)
        SetError(context, GL_INVALID_OPERATION);
}

static void FrontendMirrorTextureReply(struct MiniGLR200Context *context)
{
    struct TextureObject *texture;
    ULONG generation;

    texture = &context->textures[context->boundTexture[context->activeTexture]];
    generation = texture->imageGeneration + 1UL;
    if (!generation) generation = 1;
    texture->generated = TRUE;
    texture->resident = TRUE;
    texture->surface.Handle = (APTR)FrontendPlatform.control.value[1];
    texture->width = FrontendPlatform.control.value[2];
    texture->height = FrontendPlatform.control.value[3];
    texture->definedLevels = FrontendPlatform.control.value[4];
    texture->levelCount = FrontendPlatform.control.value[5];
    texture->contentSerial = FrontendPlatform.control.value[6];
    texture->residentDeviceEpoch = FrontendPlatform.control.value[7];
    texture->imageGeneration = generation;
    texture->residentGeneration = generation;
    texture->dirtyLevelMask = 0;
    BumpSemanticStateSerial(context);
}

/* Direct fill: the host leases a fresh never-sampled BGRA32 surface,
 * the PPC converts the source rows into it through the aperture,
 * flushes, and COMMIT installs it. Byte order and the 565/4444
 * expansion mirror the 68k ConvertPixel() exactly. */
/* One source texel -> BGRA32 surface dword ((b<<24)|(g<<16)|(r<<8)|a),
 * matching the native ConvertPixel()'s destination order exactly. All
 * supported unsigned-byte source formats plus the two big-endian 565 /
 * 4444 halfword types; anything else returns the opaque-white dword the
 * format checks upstream already reject. Luminance-class expansions
 * mirror ConvertPixel: L=a?255:L for RGB, alpha-only keeps rgb=255. */
static ULONG FrontendSourceToBGRA32(GLenum format, GLenum type,
                                    const UBYTE *src)
{
    ULONG r = 255, g = 255, b = 255, a = 255;
    if (type == MGL_UNSIGNED_SHORT_5_6_5) {
        ULONG value = ((ULONG)src[0] << 8) | src[1];
        r = ((value >> 11) & 31UL) * 255UL / 31UL;
        g = ((value >> 5) & 63UL) * 255UL / 63UL;
        b = (value & 31UL) * 255UL / 31UL;
    } else if (type == MGL_UNSIGNED_SHORT_4_4_4_4) {
        ULONG value = ((ULONG)src[0] << 8) | src[1];
        r = ((value >> 12) & 15UL) * 17UL;
        g = ((value >> 8) & 15UL) * 17UL;
        b = ((value >> 4) & 15UL) * 17UL;
        a = (value & 15UL) * 17UL;
    } else if (format == GL_ALPHA) {
        a = src[0];
    } else if (format == GL_LUMINANCE) {
        r = g = b = src[0];
    } else if (format == GL_LUMINANCE_ALPHA) {
        r = g = b = src[0];
        a = src[1];
    } else if (format == GL_RGB) {
        r = src[0]; g = src[1]; b = src[2];
    } else if (format == GL_RGBA) {
        r = src[0]; g = src[1]; b = src[2]; a = src[3];
    } else if (format == MGL_UBYTE_BGRA) {
        b = src[0]; g = src[1]; r = src[2]; a = src[3];
    } else if (format == MGL_UBYTE_ARGB) {
        a = src[0]; r = src[1]; g = src[2]; b = src[3];
    }
    return (b << 24) | (g << 16) | (r << 8) | a;
}

/* The 68k host and the native library compile against the classic
 * client enum (MGL_UBYTE_BGRA=237, MGL_UNSIGNED_SHORT_5_6_5=239, ...),
 * while dispatch clients compile against include/mgl/gl.h (233/235/...)
 * — the WOS header re-anchored GL_TEXTURE0_ARB and shifted the tail.
 * Source formats/types cross the protocol as raw client values, so
 * translate the four below-the-anchor tail constants to the native
 * numbering when packing a wire message. Everything else is identical
 * in both headers and passes through unchanged. */
static GLenum FrontendWireSourceFormat(GLenum format)
{
    if (format == MGL_UBYTE_BGRA) return (GLenum)237U;
    if (format == MGL_UBYTE_ARGB) return (GLenum)238U;
    return format;
}

static GLenum FrontendWireSourceType(GLenum type)
{
    if (type == MGL_UNSIGNED_SHORT_5_6_5) return (GLenum)239U;
    if (type == MGL_UNSIGNED_SHORT_4_4_4_4) return (GLenum)240U;
    return type;
}

/* Convert a client source patch into the staging buffer as canonical
 * BGRA8888 (same texel expansion as the native ConvertPixel and as the
 * direct path), reading the source with the client unpack layout and
 * writing tightly packed rows. The native UploadTextureRegion has a
 * per-row CopyMem fast path for MGL_UBYTE_BGRA sources, so canonical
 * staging keeps the 68060 out of its per-texel conversion loop -- the
 * single largest cost the profile host attributed to shared subimage
 * updates (HOST_SUBIMAGE native_texsub_us). */
static void FrontendStageTextureBGRA(UBYTE *destination,
                                     const UBYTE *source, GLsizei width,
                                     GLsizei height, GLenum format,
                                     GLenum type, ULONG sourceBytes,
                                     ULONG rowBytes, ULONG sourceStride)
{
    ULONG row, column;
    for (row = 0; row < (ULONG)height; ++row) {
        const UBYTE *srcRow = source + row * sourceStride;
        UBYTE *dstRow = destination + row * (ULONG)width * 4UL;
        for (column = 0; column < (ULONG)width; ++column) {
            ULONG value = FrontendSourceToBGRA32(
                format, type, srcRow + column * sourceBytes);
            dstRow[column * 4UL + 0] = (UBYTE)(value >> 24);
            dstRow[column * 4UL + 1] = (UBYTE)(value >> 16);
            dstRow[column * 4UL + 2] = (UBYTE)(value >> 8);
            dstRow[column * 4UL + 3] = (UBYTE)value;
        }
    }
    (void)rowBytes;
}


/* Retained canonical shadows: per-texture level-0 BGRA mirror of the
 * source the direct path last committed, kept PPC-side so a following
 * TexSubImage patches the shadow and re-commits the full image through
 * the direct path -- the whole residency/redraw stays on the proven
 * direct-pipeline rails with no 68k copyback and no new residency
 * churn. Bounded by R200_SHADOW_TOTAL_MAX so a pathological client
 * cannot balloon PPC memory; overflow frees only the new shadow. */
#define R200_SHADOW_TOTAL_MAX (2UL * 1024UL * 1024UL)

static void FrontendShadowFree(struct TextureObject *texture)
{
    if (texture->shadow) {
        FrontendPlatform.shadowBytesTotal -=
            texture->shadowWidth * texture->shadowHeight * 4UL;
        FreeVec(texture->shadow);
        texture->shadow = NULL;
        texture->shadowWidth = 0;
        texture->shadowHeight = 0;
    }
}

static BOOL FrontendShadowEnsure(struct MiniGLR200Context *context,
                                 struct TextureObject *texture,
                                 ULONG width, ULONG height)
{
    ULONG bytes = width * height * 4UL;
    UBYTE *shadow;
    (void)context;
    if (texture->shadow && texture->shadowWidth == width &&
        texture->shadowHeight == height)
        return TRUE;
    FrontendShadowFree(texture);
    if (bytes > MGLPPC_TEXTURE_SHARED_MAX_BYTES ||
        FrontendPlatform.shadowBytesTotal + bytes > R200_SHADOW_TOTAL_MAX)
        return FALSE;
    shadow = AllocVec(bytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!shadow)
        return FALSE;
    texture->shadow = shadow;
    texture->shadowWidth = width;
    texture->shadowHeight = height;
    FrontendPlatform.shadowBytesTotal += bytes;
    return TRUE;
}

/* Patch region [x|x+w) x [y|y+h) of the retained shadow from client
 * texels (canonical BGRA expansion). */
static void FrontendShadowPatch(struct TextureObject *texture,
                                const UBYTE *source, GLsizei width,
                                GLsizei height, GLenum format, GLenum type,
                                ULONG sourceBytes, ULONG sourceStride,
                                GLint xoffset, GLint yoffset)
{
    ULONG py;
    for (py = 0; py < (ULONG)height; ++py) {
        const UBYTE *srcRow = source + (ULONG)py * sourceStride;
        UBYTE *dstRow = texture->shadow +
            (ULONG)(yoffset + py) * texture->shadowWidth * 4UL +
            (ULONG)xoffset * 4UL;
        ULONG column;
        for (column = 0; column < (ULONG)width; ++column) {
            ULONG value = FrontendSourceToBGRA32(
                format, type, srcRow + column * sourceBytes);
            dstRow[column * 4UL + 0] = (UBYTE)(value >> 24);
            dstRow[column * 4UL + 1] = (UBYTE)(value >> 16);
            dstRow[column * 4UL + 2] = (UBYTE)(value >> 8);
            dstRow[column * 4UL + 3] = (UBYTE)value;
        }
    }
}

static BOOL FrontendDirectTextureUpload(struct MiniGLR200Context *context,
                                        ULONG width, ULONG height,
                                        GLenum format, GLenum type,
                                        const UBYTE *source,
                                        ULONG sourceStride)
{
    ULONG textureName = context->boundTexture[context->activeTexture];
    /* R5G6B5PC texture surfaces are rejected by the service's import
     * path (verified on hardware): always request BGRA32. */
    ULONG requestFormat = MGLPPC_TEXDIRECT_FMT_BGRA32;
    ULONG grantedFormat;
    ULONG row, column, pitch, bytes;
    ULONG sourceBytes;
    UBYTE *destination;

    if (type == MGL_UNSIGNED_SHORT_5_6_5 ||
        type == MGL_UNSIGNED_SHORT_4_4_4_4)
        sourceBytes = 2;
    else if (format == GL_RGB) sourceBytes = 3;
    else if (format == GL_LUMINANCE_ALPHA) sourceBytes = 2;
    else if (format == GL_ALPHA || format == GL_LUMINANCE) sourceBytes = 1;
    else sourceBytes = 4;

    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = textureName;
    FrontendPlatform.control.value[1] = width;
    FrontendPlatform.control.value[2] = height;
    FrontendPlatform.control.value[3] = requestFormat;
    if (!PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_BEGIN,
                      FrontendPlatform.contextId))
        return FALSE;
    pitch = FrontendPlatform.control.value[2];
    bytes = FrontendPlatform.control.value[3];
    grantedFormat = FrontendPlatform.control.value[5];
    /* Validate the row and full span before multiplying or writing VRAM. */
    if (!width || !height || !pitch || width > pitch / 4UL ||
        height > bytes / pitch ||
        FrontendPlatform.control.value[4] != context->deviceEpoch ||
        grantedFormat != RADEON3D_FORMAT_B8G8R8A8)
        return FALSE;
    /* Both surface classes are written in place by the PPC through the
     * aperture (the proven-fast discipline), and COMMIT installs
     * without a copy (srcAddress = 0). The staging/68k-copy hybrid
     * measured slower on every path and is retired. */
    destination = (UBYTE *)FrontendPlatform.control.value[1];
    if (!destination || pitch * height > 0xffffffffUL - (ULONG)destination)
        return FALSE;
    for (row = 0; row < height; ++row) {
        const UBYTE *srcRow = source + row * sourceStride;
        UBYTE *out = destination + row * pitch;
        if (grantedFormat == RADEON3D_FORMAT_R5G6B5PC) {
            /* Source big-endian 565 halfwords -> R5G6B5PC stores
             * little-endian words. The asm blit uses stswi (one store
             * instruction per 32 bytes) because every aperture store
             * opcode costs ~1 us on this machine. */
            ULONG dstRowBytes = width * 2UL;
            ULONG rowBlit = dstRowBytes & ~31UL;
            if (((ULONG)out & 3UL) == 0UL && rowBlit) {
                MGLPPCBlit565Swap(out, srcRow, rowBlit);
                for (column = rowBlit; column < dstRowBytes; column += 2) {
                    out[column + 1] = srcRow[column];
                    out[column] = srcRow[column + 1];
                }
            } else {
                for (column = 0; column < width; ++column) {
                    ULONG value = ((ULONG)srcRow[column * 2UL] << 8) |
                                  srcRow[column * 2UL + 1UL];
                    out[column * 2UL + 0] = (UBYTE)(value & 0xffUL);
                    out[column * 2UL + 1] = (UBYTE)(value >> 8);
                }
            }
            continue;
        }
        /* One 32-bit store per pixel: the aperture does not absorb
         * byte stores, so four 8-bit writes per texel cost a bus
         * transaction each and dominate the upload. */
        if (((ULONG)out & 3UL) == 0UL) {
            ULONG *out32 = (ULONG *)out;
            for (column = 0; column < width; ++column)
                out32[column] = FrontendSourceToBGRA32(
                    format, type, srcRow + column * sourceBytes);
        } else {
            for (column = 0; column < width; ++column) {
                ULONG value = FrontendSourceToBGRA32(
                    format, type, srcRow + column * sourceBytes);
                out[column * 4UL + 0] = (UBYTE)(value >> 24);
                out[column * 4UL + 1] = (UBYTE)(value >> 16);
                out[column * 4UL + 2] = (UBYTE)(value >> 8);
                out[column * 4UL + 3] = (UBYTE)value;
            }
        }
    }
    MGLPPCFlushDataRange((APTR)destination, pitch * height);
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = textureName;
    FrontendPlatform.control.value[1] = 0UL;
    if (!PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_COMMIT,
                      FrontendPlatform.contextId))
        return FALSE;
    return TRUE;
}

void R200PPCDispatchTexImage2D(GLenum target, GLint level,
                               GLint internalFormat, GLsizei width,
                               GLsizei height, GLint border, GLenum format,
                               GLenum type, const GLvoid *pixels)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct TextureObject *texture;
    MGLPPCMessage *chunkMessage =
        (MGLPPCMessage *)FrontendPlatform.textureMessageWords;
    const UBYTE *source = (const UBYTE *)pixels;
    ULONG rowBytes = 0, sourceStride = 0, sourceOffset = 0;
    ULONG total = 0;
    ULONG offset = 0, generation;
#if MGLPPC_PROFILE
    ULONG chunkStart;
    ULONG textureStart;
#endif
    BOOL chunkOk;
    BOOL sharedUpload = FALSE;
    if (!StateChangeAllowed(context)) return;
    if (!(FrontendPlatform.capabilities & MGLPPC_CAP_SEMANTIC_TEXTURE_V1)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    /* Internalformat accepts the same resolved list as the native
     * backend (raw component counts 3/4 included, GL 1.0 style). The
     * DIRECT path never forwards it, the shared/chunked paths pass it
     * through to native glTexImage2D; validating here gives the client
     * the documented error code at the documented call. */
    if (internalFormat != GL_RGB && internalFormat != GL_RGB5_A1 &&
        internalFormat != GL_RGBA &&
        internalFormat != 3 && internalFormat != 4 &&
        internalFormat != GL_ALPHA &&
        internalFormat != GL_LUMINANCE &&
        internalFormat != GL_LUMINANCE_ALPHA &&
        internalFormat != GL_COLOR_INDEX) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    /* The game's present path uploads 16-bit tiles as
     * (MGL_UNSIGNED_SHORT_5_6_5, GL_UNSIGNED_SHORT); normalise to the
     * type-driven forms the native backend implements. */
    if (format == MGL_UNSIGNED_SHORT_5_6_5 && type == GL_UNSIGNED_SHORT) {
        format = GL_RGB;
        type = MGL_UNSIGNED_SHORT_5_6_5;
    } else if (format == MGL_UNSIGNED_SHORT_4_4_4_4 &&
               type == GL_UNSIGNED_SHORT) {
        format = GL_RGBA;
        type = MGL_UNSIGNED_SHORT_4_4_4_4;
    }
    if (type != GL_UNSIGNED_BYTE &&
        FrontendPlatform.peerProtocol < 15UL) {
        /* A protocol-14 host cannot carry a 16-bit type. */
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (format != GL_RGB && format != GL_RGBA &&
        format != MGL_UBYTE_BGRA && format != MGL_UBYTE_ARGB &&
        format != GL_ALPHA && format != GL_LUMINANCE &&
        format != GL_LUMINANCE_ALPHA) {
        /* COLOR_INDEX is native-accepted but the frontend and native
         * palettes are not synchronized across CPUs (see protocol
         * notes); reject it here like the subimage path always has. */
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (format != GL_RGB && format != GL_RGBA &&
        format != MGL_UBYTE_BGRA && format != MGL_UBYTE_ARGB &&
        type != GL_UNSIGNED_BYTE) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (type != GL_UNSIGNED_BYTE && type != MGL_UNSIGNED_SHORT_5_6_5 &&
        type != MGL_UNSIGNED_SHORT_4_4_4_4) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if ((type == MGL_UNSIGNED_SHORT_5_6_5 && format != GL_RGB) ||
        (type == MGL_UNSIGNED_SHORT_4_4_4_4 && format != GL_RGBA)) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    total = FrontendTextureLayout(context, width, height, format, type,
                                  &rowBytes, &sourceStride, &sourceOffset);
    if (!pixels || !total || target != GL_TEXTURE_2D || level < 0 ||
        (ULONG)level >= MAX_TEXTURE_LEVELS || width > 2048 || height > 2048 ||
        border != 0) {
        SetError(context, target != GL_TEXTURE_2D ? GL_INVALID_ENUM
                                                  : GL_INVALID_VALUE);
        return;
    }
#if MGLPPC_PROFILE
    textureStart = PerfClock();
    PlatformProfile.textureCalls++;
    PlatformProfile.textureBytes += total;
#endif
    if (context->activeTexture != 0 || context->boundTexture[0] == 0) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    texture = &context->textures[context->boundTexture[0]];
    if ((FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_DIRECT_V1) &&
        level == 0 && width <= 2048 && height <= 2048 &&
        total <= MGLPPC_TEXTURE_SHARED_MAX_BYTES &&
        !(width & (width - 1UL)) && !(height & (height - 1UL))) {
        /* Publish old texture snapshots before BEGIN can replace their handle.
         * Mailbox dispatch precedes RingPoll on the host: consume the ring
         * first, but leave the GPU fence wait to BEGIN's DrainSemanticFence. */
#if MGLPPC_PROFILE
        chunkStart = PerfClock();
        PlatformProfile.textureBarrierCalls++;
#endif
        if (!PlatformMailboxReady() || FrontendPlatform.mailboxAbandoned ||
            !FlushScreenVertices(context) || !Submit(context) ||
            (FrontendPlatform.ring &&
             !R200PPCPlatformBarrier(context, FALSE))) {
#if MGLPPC_PROFILE
            PlatformProfile.textureBarrierTicks += PerfClock() - chunkStart;
#endif
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
#if MGLPPC_PROFILE
        PlatformProfile.textureBarrierTicks += PerfClock() - chunkStart;
        chunkStart = PerfClock();
        PlatformProfile.textureDirectCalls++;
#endif
        if (FrontendDirectTextureUpload(context, (ULONG)width,
                                        (ULONG)height, format, type,
                                        source + sourceOffset,
                                        sourceStride)) {
#if MGLPPC_PROFILE
            PlatformProfile.textureDirectTicks += PerfClock() - chunkStart;
#endif
            if (!FrontendPlatform.control.value[1] ||
                !FrontendPlatform.control.value[2] ||
                !FrontendPlatform.control.value[3] ||
                !FrontendPlatform.control.value[4] ||
                !FrontendPlatform.control.value[5] ||
                FrontendPlatform.control.value[7] != context->deviceEpoch) {
                SetError(context, GL_INVALID_OPERATION);
                return;
            }
            FrontendMirrorTextureReply(context);
            /* Retain this level-0 image as the canonical shadow so a
             * following TexSubImage can patch-and-recommit here. */
            {
                struct TextureObject *shadowTarget =
                    &context->textures[context->boundTexture[0]];
                ULONG shadowSourceBytes =
                    MGLPPC_TEXTURE_SOURCE_BYTES(format, type);
                if (FrontendShadowEnsure(context, shadowTarget,
                                         (ULONG)width, (ULONG)height))
                    FrontendStageTextureBGRA(
                        shadowTarget->shadow, source + sourceOffset,
                        width, height, format, type, shadowSourceBytes,
                        rowBytes, sourceStride);
            }
#if MGLPPC_PROFILE
            PlatformProfile.textureSuccesses++;
            PlatformProfile.textureTicks += PerfClock() - textureStart;
#endif
            return;
        }
#if MGLPPC_PROFILE
        PlatformProfile.textureDirectTicks += PerfClock() - chunkStart;
#endif
        if (FrontendPlatform.mailboxAbandoned) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        /* Direct failed: recover the host's pending state, then fall
         * through to the shared staging path, which re-creates the
         * whole texture from scratch. */
        FrontendShadowFree(texture);
        memset(&FrontendPlatform.control, 0,
               sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] =
            context->boundTexture[context->activeTexture];
        if (!PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_ABORT,
                          FrontendPlatform.contextId)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
    }
    if (!FrontendTextureBarrier(context)) return;

    {
    /* Shared staging converts the client texels to canonical BGRA8888
     * on the PPC (native row-copy fast path) instead of handing raw
     * client rows to the 68k ConvertPixel loop. v14 hosts cannot carry
     * the tail types, so only peer protocol 15 canonicalizes. */
    ULONG sharedBytes = total;
    GLboolean canonical = FALSE;
    if (FrontendPlatform.peerProtocol >= 15UL) {
        sharedBytes = (ULONG)width * (ULONG)height * 4UL;
        canonical = TRUE;
    }
    if ((FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_SHARED_V1) &&
        total <= MGLPPC_TEXTURE_SHARED_MAX_BYTES &&
        FrontendEnsureTextureUploadBuffer(canonical ? sharedBytes : total)) {
        /* Flush the deferred bind BEFORE this branch prepares its own
         * control.value[]: PlatformCall sends and overwrites the shared
         * buffer, so a later flush would corrupt the upload message. */
        if (!HostTextureShadowSync())
            return;
        if (canonical)
            FrontendStageTextureBGRA(FrontendPlatform.textureUploadBuffer,
                                     source + sourceOffset, width, height,
                                     format, type,
                                     MGLPPC_TEXTURE_SOURCE_BYTES(format,
                                                                 type),
                                     rowBytes, sourceStride);
        else
            FrontendCopyTextureBytes(FrontendPlatform.textureUploadBuffer,
                                     source + sourceOffset, 0, total,
                                     rowBytes, sourceStride);
        MGLPPCFlushDataRange(FrontendPlatform.textureUploadBuffer,
                             canonical ? sharedBytes : total);
        memset(&FrontendPlatform.control, 0,
               sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] = target;
        FrontendPlatform.control.value[1] = (ULONG)level;
        FrontendPlatform.control.value[2] = (ULONG)internalFormat;
        FrontendPlatform.control.value[3] = (ULONG)width;
        FrontendPlatform.control.value[4] = (ULONG)height;
        if (canonical) {
            FrontendPlatform.control.value[5] =
                (ULONG)(FrontendWireSourceFormat(MGL_UBYTE_BGRA) |
                        (GL_UNSIGNED_BYTE << 16));
        } else {
            FrontendPlatform.control.value[5] =
                FrontendPlatform.peerProtocol >= 15UL
                    ? (FrontendWireSourceFormat(format) |
                       (FrontendWireSourceType(type) << 16)) : format;
        }
        FrontendPlatform.control.value[6] =
            (ULONG)FrontendPlatform.textureUploadBuffer;
        FrontendPlatform.control.value[7] = canonical ? sharedBytes : total;
#if MGLPPC_PROFILE
        chunkStart = PerfClock();
#endif
        chunkOk = PlatformCall(MGLPPC_CMD_TEXTURE_UPLOAD_SHARED,
                               FrontendPlatform.contextId);
#if MGLPPC_PROFILE
        PlatformProfile.chunkTicks += PerfClock() - chunkStart;
        PlatformProfile.chunkCount++;
#endif
        if (!chunkOk) {
            /* The host reports the native glTexImage2D error in
             * value[0] with MGLPPC_STATUS_GL_ERROR; surface that code
             * instead of masking it as INVALID_OPERATION. */
            FrontendSetTransportError(context);
            return;
        }
        sharedUpload = TRUE;
#if MGLPPC_PROFILE
        PlatformProfile.textureSharedCalls++;
#endif
    }
    }

    if (!sharedUpload) {
#if MGLPPC_PROFILE
        PlatformProfile.textureChunkedCalls++;
#endif
        /* Flush the deferred bind BEFORE preparing this message; see the
         * shared-path comment above for the control-buffer clobber risk. */
        if (!HostTextureShadowSync())
            return;
        memset(&FrontendPlatform.control, 0,
               sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] = target;
        FrontendPlatform.control.value[1] = (ULONG)level;
        FrontendPlatform.control.value[2] = (ULONG)internalFormat;
        FrontendPlatform.control.value[3] = (ULONG)width;
        FrontendPlatform.control.value[4] = (ULONG)height;
        FrontendPlatform.control.value[5] = (ULONG)border;
        FrontendPlatform.control.value[6] =
            (ULONG)FrontendWireSourceFormat(format);
        FrontendPlatform.control.value[7] =
            (ULONG)FrontendWireSourceType(type);
        if (!PlatformCall(MGLPPC_CMD_TEXTURE_UPLOAD_BEGIN,
                          FrontendPlatform.contextId)) {
            FrontendSetTransportError(context);
            return;
        }
        while (offset < total) {
            ULONG bytes = total - offset;
            if (bytes > MGLPPC_TEXTURE_CHUNK_WORDS * sizeof(ULONG))
                bytes = MGLPPC_TEXTURE_CHUNK_WORDS * sizeof(ULONG);
            memset(chunkMessage, 0,
                   MGLPPC_MESSAGE_SIZE(MGLPPC_TEXTURE_CHUNK_WORDS));
            chunkMessage->command = MGLPPC_CMD_TEXTURE_UPLOAD_CHUNK;
            chunkMessage->contextId = FrontendPlatform.contextId;
            chunkMessage->value[0] = offset;
            chunkMessage->value[1] = bytes;
            chunkMessage->payloadWords =
                (bytes + sizeof(ULONG) - 1UL) / sizeof(ULONG);
            FrontendCopyTextureBytes((UBYTE *)chunkMessage->payload,
                                     source + sourceOffset,
                                     offset, bytes, rowBytes, sourceStride);
#if MGLPPC_PROFILE
            chunkStart = PerfClock();
#endif
            chunkOk = MGLPPCTransportCall(&FrontendPlatform.transport,
                                          chunkMessage);
#if MGLPPC_PROFILE
            PlatformProfile.chunkTicks += PerfClock() - chunkStart;
            PlatformProfile.chunkCount++;
#endif
            if (!chunkOk || chunkMessage->status != MGLPPC_STATUS_OK) {
                SetError(context, GL_INVALID_OPERATION);
                return;
            }
            offset += bytes;
        }
        memset(&FrontendPlatform.control, 0,
               sizeof(FrontendPlatform.control));
        if (!PlatformCall(MGLPPC_CMD_TEXTURE_UPLOAD_END,
                          FrontendPlatform.contextId)) {
            FrontendSetTransportError(context);
            return;
        }
    }
    if (!FrontendPlatform.control.value[1] ||
        !FrontendPlatform.control.value[2] ||
        !FrontendPlatform.control.value[3] ||
        !FrontendPlatform.control.value[4] ||
        !FrontendPlatform.control.value[5] ||
        FrontendPlatform.control.value[7] != context->deviceEpoch) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    texture = &context->textures[context->boundTexture[context->activeTexture]];
    /* A shared/chunked (image-backed) redefinition invalidates any
     * retained direct shadow: subimages of image-backed textures must go
     * through the native canonical path. */
    FrontendShadowFree(texture);
    generation = texture->imageGeneration + 1UL;
    if (!generation) generation = 1;
    texture->generated = TRUE;
    texture->resident = TRUE;
    texture->surface.Handle = (APTR)FrontendPlatform.control.value[1];
    texture->width = FrontendPlatform.control.value[2];
    texture->height = FrontendPlatform.control.value[3];
    texture->definedLevels = FrontendPlatform.control.value[4];
    texture->levelCount = FrontendPlatform.control.value[5];
    texture->contentSerial = FrontendPlatform.control.value[6];
    texture->residentDeviceEpoch = FrontendPlatform.control.value[7];
    texture->imageGeneration = generation;
    texture->residentGeneration = generation;
    texture->dirtyLevelMask = 0;
    BumpSemanticStateSerial(context);
#if MGLPPC_PROFILE
    PlatformProfile.textureSuccesses++;
    PlatformProfile.textureTicks += PerfClock() - textureStart;
#endif
}

void R200PPCDispatchTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLsizei width, GLsizei height,
                                  GLenum format, GLenum type,
                                  const GLvoid *pixels)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct TextureObject *texture;
    ULONG name, bpp, rowBytes, stride, offset, bytes;
#if MGLPPC_PROFILE
    ULONG textureStart;
    ULONG subChunkStart = 0;
#endif
    if (!StateChangeAllowed(context)) return;
    if (target != GL_TEXTURE_2D) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (FrontendPlatform.peerProtocol < 15UL ||
        !(FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_SUBIMAGE_V1)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (format == MGL_UNSIGNED_SHORT_5_6_5 && type == GL_UNSIGNED_SHORT) {
        format = GL_RGB; type = MGL_UNSIGNED_SHORT_5_6_5;
    } else if (format == MGL_UNSIGNED_SHORT_4_4_4_4 && type == GL_UNSIGNED_SHORT) {
        format = GL_RGBA; type = MGL_UNSIGNED_SHORT_4_4_4_4;
    }
    bpp = MGLPPC_TEXTURE_SOURCE_BYTES(format, type);
    if (!bpp) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    name = context->boundTexture[0];
    if (context->activeTexture != 0 || !name || name >= MAX_TEXTURES) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    texture = &context->textures[name];
    if (!texture->generated || !texture->definedLevels) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (level < 0 || (ULONG)level >= MAX_TEXTURE_LEVELS || xoffset < 0 ||
        yoffset < 0 || width < 0 || height < 0 ||
        !MGLPPCTextureRect(texture->width, texture->height, texture->levelCount,
            texture->definedLevels, level, xoffset, yoffset, width, height)) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (!width || !height) return;
    if (!pixels || !MGLPPCTextureSpan(width, height, bpp,
            context->unpackAlignment, context->unpackRowLength,
            context->unpackSkipRows, context->unpackSkipPixels, (ULONG)pixels,
            &rowBytes, &stride, &offset, &bytes)) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (bytes > MGLPPC_TEXTURE_SHARED_MAX_BYTES) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
#if MGLPPC_PROFILE
    textureStart = PerfClock();

    PlatformProfile.textureCalls++;
    PlatformProfile.textureBytes += bytes;
    PlatformProfile.textureSharedCalls++;
#endif
    /* Canonical BGRA staging: the wire carries width*height*4 bytes, so
     * the shared cap applies to the canonical size (the host validates
     * the staging span at bpp 4). */
    bytes = (ULONG)width * (ULONG)height * 4UL;
    if (bytes > MGLPPC_TEXTURE_SHARED_MAX_BYTES) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    /* Fast path: a retained direct-eligible shadow lets this subimage
     * patch PPC-side memory and re-commit the WHOLE image through the
     * direct pipeline (fresh BEGIN surface per commit, aperture-rate
     * fill, no 68k copyback, no native residency churn). Same
     * eligibility rules as the direct TexImage path plus the shadow. */
    if (texture->shadow &&
        texture->shadowWidth == (ULONG)texture->width &&
        texture->shadowHeight == (ULONG)texture->height &&
        level == 0 &&
        (FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_DIRECT_V1) &&
        texture->width >= (ULONG)(xoffset + width) &&
        texture->height >= (ULONG)(yoffset + height) &&
        !FrontendPlatform.mailboxAbandoned &&
        !(texture->width & (texture->width - 1UL)) &&
        !(texture->height & (texture->height - 1UL))) {
#if MGLPPC_PROFILE
        subChunkStart = PerfClock();
        PlatformProfile.textureBarrierCalls++;
#endif
        /* The shadow patch precedes publishing: the client's texels are
         * the truth until the commit completes. */
        FrontendShadowPatch(texture, (const UBYTE *)pixels + offset,
                            width, height, format, type,
                            bpp, stride, xoffset, yoffset);
        if (!PlatformMailboxReady() ||
            !FlushScreenVertices(context) || !Submit(context) ||
            (FrontendPlatform.ring &&
             !R200PPCPlatformBarrier(context, FALSE))) {
#if MGLPPC_PROFILE
            PlatformProfile.textureBarrierTicks += PerfClock() - subChunkStart;
#endif
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
#if MGLPPC_PROFILE
        PlatformProfile.textureBarrierTicks += PerfClock() - subChunkStart;
        subChunkStart = PerfClock();
        PlatformProfile.textureDirectCalls++;
#endif
        if (FrontendDirectTextureUpload(context, texture->shadowWidth,
                                        texture->shadowHeight,
                                        MGL_UBYTE_BGRA, GL_UNSIGNED_BYTE,
                                        texture->shadow,
                                        texture->shadowWidth * 4UL)) {
#if MGLPPC_PROFILE
            PlatformProfile.textureDirectTicks += PerfClock() - subChunkStart;
#endif
            if (!FrontendPlatform.control.value[1] ||
                !FrontendPlatform.control.value[2] ||
                !FrontendPlatform.control.value[3] ||
                FrontendPlatform.control.value[4] != texture->definedLevels ||
                FrontendPlatform.control.value[5] != texture->levelCount ||
                FrontendPlatform.control.value[7] != context->deviceEpoch) {
                SetError(context, GL_INVALID_OPERATION);
                return;
            }
            FrontendMirrorTextureReply(context);
#if MGLPPC_PROFILE
            PlatformProfile.textureSuccesses++;
            PlatformProfile.textureTicks += PerfClock() - textureStart;
#endif
            return;
        }
#if MGLPPC_PROFILE
        PlatformProfile.textureDirectTicks += PerfClock() - subChunkStart;
#endif
        if (FrontendPlatform.mailboxAbandoned) {
            /* The shadow now leads the native content; a later recommit
             * or TexImage can still converge. Keep the shadow. */
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        memset(&FrontendPlatform.control, 0,
               sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] = name;
        if (!PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_ABORT,
                          FrontendPlatform.contextId)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        /* Fall through to the canonical shared path below; the shadow
         * stays the committed PPC-side truth for the next attempt. */
    }
    /* Old snapshots must be published and fenced before native invalidation. */
    if (!FrontendTextureBarrier(context)) return;
    if (!FrontendEnsureTextureUploadBuffer(bytes)) {
        SetError(context, GL_OUT_OF_MEMORY);
        return;
    }
    /* Canonical BGRA8888 staging keeps the native path on its per-row
     * CopyMem instead of the 68k per-texel ConvertPixel loop. */
    FrontendStageTextureBGRA(FrontendPlatform.textureUploadBuffer,
        (const UBYTE *)pixels + offset, width, height, format, type,
        bpp, rowBytes, stride);
    MGLPPCFlushDataRange(FrontendPlatform.textureUploadBuffer, bytes);
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = name;
    FrontendPlatform.control.value[1] = level;
    FrontendPlatform.control.value[2] = xoffset;
    FrontendPlatform.control.value[3] = yoffset;
    FrontendPlatform.control.value[4] = width;
    FrontendPlatform.control.value[5] = height;
    FrontendPlatform.control.value[6] =
        (ULONG)(FrontendWireSourceFormat(MGL_UBYTE_BGRA) |
                (GL_UNSIGNED_BYTE << 16));
    FrontendPlatform.control.value[7] = (ULONG)FrontendPlatform.textureUploadBuffer;
    /* Native may fail AFTER replacing residency. Never keep a stale handle,
     * even when transport completion is lost. A later successful update or
     * TexImage can restore this mirror; old draws are already published. */
    texture->resident = FALSE;
    texture->surface.Handle = NULL;
    texture->residentGeneration = 0;
    BumpSemanticStateSerial(context);
    if (!PlatformCall(MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED,
                      FrontendPlatform.contextId)) {
        FrontendSetTransportError(context);
        return;
    }
    if (!FrontendPlatform.control.value[1] ||
        FrontendPlatform.control.value[2] != texture->width ||
        FrontendPlatform.control.value[3] != texture->height ||
        FrontendPlatform.control.value[4] != texture->definedLevels ||
        FrontendPlatform.control.value[5] != texture->levelCount ||
        FrontendPlatform.control.value[7] != context->deviceEpoch) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    FrontendMirrorTextureReply(context);
#if MGLPPC_PROFILE
    PlatformProfile.textureSuccesses++;
    PlatformProfile.textureTicks += PerfClock() - textureStart;
#endif
}

void R200PPCDispatchDeleteTextures(GLsizei count, const GLuint *names)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLsizei done = 0;
    BOOL bindingChanged = FALSE;
    if (!StateChangeAllowed(context)) return;
    if (count < 0 || (!names && count)) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (!count) return;
    FrontendPlatform.nativeBindingKnown = FALSE;
    /* The host resets its shadow binding when a deleted name was bound;
     * force a resync before the next shadow-consuming upload. */
    FrontendPlatform.hostShadowDirty = TRUE;
    if (!FrontendTextureBarrier(context)) return;
    while (done < count) {
        ULONG chunk = (ULONG)(count - done), index;
        if (chunk > 7UL) chunk = 7UL;
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] = chunk;
        for (index = 0; index < chunk; ++index)
            FrontendPlatform.control.value[index + 1UL] =
                names[done + (GLsizei)index];
        if (!PlatformCall(MGLPPC_CMD_DELETE_TEXTURES,
                          FrontendPlatform.contextId)) {
            if (bindingChanged)
                BumpSemanticStateSerial(context);
            FrontendSetTransportError(context);
            return;
        }
        for (index = 0; index < chunk; ++index) {
            GLuint name = names[done + (GLsizei)index];
            struct TextureObject *texture;
            if (!name || name >= MAX_TEXTURES) continue;
            texture = &context->textures[name];
            /* Free the retained shadow before the struct is zeroed or
             * the PPC allocation leaks. */
            FrontendShadowFree(texture);
            ZeroMemory(texture, sizeof(*texture));
            texture->minFilter = texture->magFilter = GL_NEAREST;
            texture->wrapS = texture->wrapT = GL_REPEAT;
            if (context->boundTexture[0] == name) {
                context->boundTexture[0] = 0;
                bindingChanged = TRUE;
            }
            if (context->boundTexture[1] == name) {
                context->boundTexture[1] = 0;
                bindingChanged = TRUE;
            }
        }
        done += (GLsizei)chunk;
    }
    if (bindingChanged) BumpSemanticStateSerial(context);
}

#endif
#endif
