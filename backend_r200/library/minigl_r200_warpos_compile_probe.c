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

#if defined(MGLPPC_ERROR_ORIGIN)
/* Failure-only origins, independent of console redirection/perf counters.
 * No open handle survives a call; cap disk use at 64 small records. */
void R200PPCErrorAt(const char *file, ULONG line, GLenum error)
{
    static ULONG reports;
    char text[224];
    BPTR handle;
    if (reports >= 64UL)
        return;
    snprintf(text, sizeof(text), "%lu %s:%lu value=%lu\n",
             (unsigned long)reports, file, (unsigned long)line,
             (unsigned long)error);
    handle = Open((STRPTR)"DH2:wosbuild/qorigin.log",
                  reports ? MODE_OLDFILE : MODE_NEWFILE);
    if (handle) {
        Seek(handle, 0, OFFSET_END);
        Write(handle, text, strlen(text));
        Close(handle);
    }
    ++reports;
}
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

/* Raw text line into the same per-line open/append/close trace file.
 * Used to mirror R200PPCConsolePrintf output (PPCPERF_LIVE/PPCDELTA) so a
 * fullscreen run leaves readable evidence while the client is still alive. */
static void TraceRaw(const char *text)
{
#if MGLPPC_CLIENT_TRACE
    char buf[384];
    char *p = buf;
    ULONG len;
    BPTR fh;
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
    TraceHex(&p, PerfClock());
    *p++ = ' ';
    while (*text && (ULONG)(p - buf) < sizeof(buf) - 2)
        *p++ = *text++;
    if (p[-1] != '\n')
        *p++ = '\n';
    len = (ULONG)(p - buf);
    fh = Open((STRPTR)TRACE_PATH, MODE_OLDFILE);
    if (!fh)
        return;
    Seek(fh, 0, OFFSET_END);
    (void)Write(fh, buf, len);
    Close(fh);
#else
    (void)text;
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
/* MGLPPC_NOPARK=1: subimage re-commits drain instead of parking (A/B). */
static ULONG FrontendNoPark;
/* MGLPPC_NOPOOL=1: no PPC-owned lightmap surface pools (A/B). */
static ULONG FrontendNoPool;
ULONG MGLPPCClearBlend;
static void FrontendLmPoolReset(void);

/* EXP-FORCENORMAL: A/B switch (MGLPPC_FORCENORMAL env) that puts
 * hardware-TCL draws on the normal-vertex layout. Diagnostic only. */
ULONG R200PPCForceNormalVertex;

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
    TraceRaw(buf);
}

/* --- DEBUG (render-path investigation): DH2:cpdbg.log -------------------
 * Always-on, event-gated trace. One Open/Seek/Write/Close per emitted line;
 * only stall/failure events are logged (bounded per run), never per frame. */
static void CpDbgPrintf(const char *fmt, ...)
{
    static BOOL opened = FALSE;
    char buf[256];
    va_list ap;
    BPTR fh;

    if (!opened) {
        opened = TRUE;
        fh = Open((CONST_STRPTR)"DH2:cpdbg.log", MODE_NEWFILE);
        if (fh)
            Close(fh);
    }
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    fh = Open((CONST_STRPTR)"DH2:cpdbg.log", MODE_OLDFILE);
    if (!fh)
        return;
    Seek(fh, 0, OFFSET_END);
    Write(fh, (APTR)buf, (LONG)strlen(buf));
    Close(fh);
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
BOOL Radeon3DInvalidateForTest(struct Radeon3DDevice *device)
{ (void)device; return FALSE; }
BOOL Radeon3DAllocSegment(struct Radeon3DDevice *device, ULONG bytes,
                          struct Radeon3DSegment *segment)
{ (void)device; (void)bytes; (void)segment; return FALSE; }
BOOL Radeon3DFreeSegment(struct Radeon3DDevice *device, ULONG id)
{ (void)device; (void)id; return FALSE; }
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
/* Ring-writer MMIO primitives (backend_r200/client/minigl_ppc_ring_mmio.s):
 * shapes copied from the hardware-validated p96-driver phase-0 helpers
 * (lwbrx/stwbrx + eieio; committed WPTR publish = store + readback). */
extern ULONG R200PPCMmioRead32(volatile void *address);
extern void R200PPCMmioWrite32(volatile void *address, ULONG value);
extern ULONG R200PPCMmioWriteCommit(volatile void *address, ULONG value);
extern void R200PPCCopyRingBr(volatile ULONG *destination,
                              const ULONG *source, ULONG dwords);
extern void R200PPCWriteBarrier(void);
/* CP surface-descriptor cache capacity. The cache holds every surface token
 * the emitter can resolve: colour target, depth, texture unit 0 and texture
 * unit 1. Resident textures dominate the working set, so the old 16 entries
 * caused a capacity miss every few draws (CP_SURF evidence 2026-09-20: ~36k
 * synchronous mailbox queries per timedemo). 128 entries cost 4 KiB.
 * Overridable at build time (Makefile MGLPPC_CP_SLOTS) for the 16/64/128
 * A/B; the safe invalidation rules do not depend on the capacity. */
#ifndef MGLPPC_CP_SURFACE_SLOTS
#define MGLPPC_CP_SURFACE_SLOTS 128UL
#endif

typedef struct R200PPCFrontendPlatform {
    MGLPPCTransport transport;
    MGLPPCMessage control;
    ULONG textureMessageWords[
        (MGLPPC_MESSAGE_SIZE(MGLPPC_TEXTURE_CHUNK_WORDS) +
         sizeof(ULONG) - 1UL) / sizeof(ULONG)];
    ULONG registerMessageWords[
        (MGLPPC_MESSAGE_SIZE(MGLPPC_RING_SLOTS > 16UL ? MGLPPC_RING_SLOTS : 16UL) +
         sizeof(ULONG) - 1UL) / sizeof(ULONG)];
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
    ULONG cpColorToken, cpColorSeq;
    /* Interface-20 PPC engine lease (protocol v16). leaseAvailable is the
     * reply-only register flag; leaseActive means this frontend currently
     * owns the ring (68k refuses all other submissions until LEASE_END). */
    BOOL leaseAvailable, leaseActive;
    BOOL leaseRequested;
    ULONG leaseLastFence;
    volatile ULONG *leaseRing;      /* CP ring CPU address (cached PPC view) */
    ULONG leaseRingGpu, leaseRingDwords, leaseRingMask;
    ULONG leaseBar2;                /* BAR2 MMIO base (GTT scratch regs) */
    ULONG leaseNextFence, leaseMaxBatch, leaseHeartbeatMs;
    ULONG leaseLastHeartbeat;       /* PerfClock sample of last heartbeat */
    ULONG leaseWptr;                /* local ring write cursor (masked) */
    /* Released for a present (C1): re-acquire once the host has consumed
     * the present, so the next batch stamps the host's new color target. */
    BOOL leaseResume;
    /* Ceiling diagnostics (default off): NOPRESENT skips presents while
     * the lease is held; NOIDLE 1 drops the per-batch WAIT_IDLE, 2 also
     * waits only for the previous batch's fence (one batch in flight). */
    BOOL leaseNoPresent;
    ULONG leaseNoIdle;
    /* v7 CP segment FIFO: next free byte, and per ring slot the serial,
     * start and size of the batch it published (bytes 0: no batch). */
    ULONG cpRingCursor;
    ULONG cpEntrySerial[MGLPPC_RING_SLOTS];
    ULONG cpEntryStart[MGLPPC_RING_SLOTS];
    ULONG cpEntryBytes[MGLPPC_RING_SLOTS];
    /* Emit staging for lease batches: batch dwords + 6-dword fence tail
     * + padding to the ring's 16-dword alignment. Kept separate from the
     * transport slot buffers so the lease path never fights the bounded
     * path for slot space. */
    ULONG leaseStaging[MGLPPC_CP_DWORDS + 16UL];
    struct {
        ULONG token, epoch;
        struct MGLPPCEmitSurface surface;
    } cpSurfaces[MGLPPC_CP_SURFACE_SLOTS];
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
    /* Deferred first tile of a two-tile direct upload (Silent Hill's intro
     * logo/FMV fills a 320x256 16-bit frame as a 256x256 plus a 64x256 tile
     * back to back). The tile is converted into a PPC-resident staging
     * buffer at TexImage2D time -- the client's texels are consumed exactly
     * at the call, as the GL contract requires -- and BEGIN/COMMIT are
     * deferred until the second tile arrives so both tiles share one BEGIN2
     * and one COMMIT2 mailbox round trip. Every path that can observe the
     * texture or invalidate the staging (draws, present, another upload,
     * delete, readback, context teardown) flushes the pending tile through
     * the ordinary single-tile direct path first. */
    BOOL pendingDirectActive;
    ULONG pendingDirectName;
    ULONG pendingDirectWidth;
    ULONG pendingDirectHeight;
    ULONG pendingDirectShadowName; /* unit-0 texture at queue time; 0 = none */
    ULONG pendingDirectEpoch;
    UBYTE *pendingDirectStaging;   /* converted BGRA tile, tight rows */
    BOOL pendingDirectStagingIsShadow;
    BOOL batchDisabled;            /* opt-in gate, see MGLPPC_BATCH */
    BOOL opened;
    BOOL registered;
} R200PPCFrontendPlatform;

/* Snapshot of the deferred-tile fields above, taken before the pipeline is
 * drained so the completion helpers never read a half-cleared state. */
typedef struct R200PPCPendingDirectUpload {
    ULONG textureName;
    ULONG width;
    ULONG height;
    ULONG shadowName;
    ULONG epoch;
    UBYTE *staging;
    BOOL stagingIsShadow;
} R200PPCPendingDirectUpload;

BOOL R200PPCPendingUploadFlush(struct MiniGLR200Context *context);

static R200PPCFrontendPlatform FrontendPlatform;

#if MGLPPC_PROFILE
static void BatchDiagDump(void);
/* Ordinary single-tile direct path phase attribution (same dump). */
static ULONG SingleDiagCalls;
static ULONG SingleDiagBeginTicks;
static ULONG SingleDiagWriteTicks;
static ULONG SingleDiagCommitTicks;
#endif

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

#if MGLPPC_PROFILE
#include <libraries/minigl_subimage_timing.h>
enum {
    PPC_LM_VALIDATE, PPC_LM_PENDING, PPC_LM_DIRECT_PATCH, PPC_LM_DIRECT_BARRIER,
    PPC_LM_DIRECT, PPC_LM_DIRECT_REPLY, PPC_LM_ABORT, PPC_LM_BARRIER,
    PPC_LM_STAGE, PPC_LM_CACHE, PPC_LM_REQUEST, PPC_LM_CALL, PPC_LM_MIRROR
};
static const char *const PpcLmPhases[] = {
    "validate", "pending_flush", "direct_patch", "direct_barrier",
    "direct_unclassified", "direct_reply", "direct_abort", "old_work_barrier",
    "bgra_stage", "cache_flush", "request_invalidate", "platform_call", "mirror"
};
static struct PpcLmRecord {
    MGLLmTiming time;
    ULONG serial, contextId, name, sharedOrder, path, result;
    GLint level, x, y, w, h;
    GLenum format, type, errorBefore, errorAfter;
    ULONG sourceBytes, wireBytes, mailbox;
} PpcLmRecords[MGL_LM_CAPACITY];
static ULONG PpcLmCount, PpcLmDropped, PpcLmSerial, PpcLmSharedOrder;
static BOOL PpcLmDumped;

static struct PpcLmRecord *PpcLmBegin(struct MiniGLR200Context *context,
    GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type)
{
    struct PpcLmRecord *r;
    ULONG start = PerfClock();
    ++PpcLmSerial;
    if (PpcLmCount == MGL_LM_CAPACITY) {
        ++PpcLmDropped;
        return NULL;
    }
    r = &PpcLmRecords[PpcLmCount++];
    memset(r, 0, sizeof(*r));
    MGLLmBegin(&r->time, start);
    r->serial = PpcLmSerial;
    r->contextId = FrontendPlatform.contextId;
    r->name = context ? context->boundTexture[context->activeTexture] : 0;
    r->errorBefore = context ? context->error : GL_NO_ERROR;
    r->level = level; r->x = x; r->y = y; r->w = w; r->h = h;
    r->format = format; r->type = type;
    return r;
}

/* Shared glTexImage2D histogram (profile builds): which textures take the
 * 68k shared upload path, how often and at what cost. */
#define SHARED_HISTO_SLOTS 24UL
static struct {
    ULONG name, level, w, h, format, type, count, ticks;
} SharedUploadHisto[SHARED_HISTO_SLOTS];
static ULONG SharedUploadHistoOverflow;

static void SharedUploadHistoNote(ULONG name, GLint level, GLsizei w,
                                  GLsizei h, GLenum format, GLenum type,
                                  ULONG ticks)
{
    ULONG i;

    for (i = 0; i < SHARED_HISTO_SLOTS; ++i) {
        if (!SharedUploadHisto[i].count) {
            SharedUploadHisto[i].name = name;
            SharedUploadHisto[i].level = (ULONG)level;
            SharedUploadHisto[i].w = (ULONG)w;
            SharedUploadHisto[i].h = (ULONG)h;
            SharedUploadHisto[i].format = format;
            SharedUploadHisto[i].type = type;
        }
        if (SharedUploadHisto[i].name == name &&
            SharedUploadHisto[i].level == (ULONG)level &&
            SharedUploadHisto[i].w == (ULONG)w &&
            SharedUploadHisto[i].h == (ULONG)h &&
            SharedUploadHisto[i].format == format) {
            ++SharedUploadHisto[i].count;
            SharedUploadHisto[i].ticks += ticks;
            return;
        }
    }
    ++SharedUploadHistoOverflow;
}

ULONG R200SubmitCallerGet(ULONG index, ULONG *offset);

static void SharedUploadHistoDump(FILE *file)
{
    ULONG i;

    fprintf(file, "SHARED_HISTO overflow=%lu\n",
            (unsigned long)SharedUploadHistoOverflow);
    for (i = 0; i < SHARED_HISTO_SLOTS && SharedUploadHisto[i].count; ++i)
        if (SharedUploadHisto[i].count > 2UL)
            fprintf(file, "SHARED name=%lu lvl=%lu %lux%lu fmt=%lx/%lx "
                    "n=%lu ticks=%lu\n",
                    (unsigned long)SharedUploadHisto[i].name,
                    (unsigned long)SharedUploadHisto[i].level,
                    (unsigned long)SharedUploadHisto[i].w,
                    (unsigned long)SharedUploadHisto[i].h,
                    (unsigned long)SharedUploadHisto[i].format,
                    (unsigned long)SharedUploadHisto[i].type,
                    (unsigned long)SharedUploadHisto[i].count,
                    (unsigned long)SharedUploadHisto[i].ticks);
}

/* At most four small varargs per call: no floats or %llu on WarpOS. */
static void PpcLmDump(void)
{
    FILE *file;
    ULONG i, phase;
    if (PpcLmDumped) return;
    PpcLmDumped = TRUE; /* An I/O failure must not cause repeated teardown I/O. */
    file = fopen("DH2:wosbuild/q260deep/ppc_lm.txt", "w");
    if (!file) return;
    fprintf(file, "PPC_LM capacity=%lu records=%lu dropped=%lu calls=%lu\n",
        (unsigned long)MGL_LM_CAPACITY, (unsigned long)PpcLmCount,
        (unsigned long)PpcLmDropped, (unsigned long)PpcLmSerial);
    fprintf(file, "PPC_LM clock_hz_legacy=%lu shared_attempts=%lu\n",
        (unsigned long)PlatformClockRate, (unsigned long)PpcLmSharedOrder);
    fprintf(file, "PPC_LM ticks=raw_timebase_low32 clock_hz_legacy_trusted=0 "
        "conversion=client_measured_tb_hz output=after_platform_cleanup "
        "overflow=drop_new phases=exclusive durations=uint32(end-start)\n");
    fprintf(file, "PPC_LM path=0:unselected,1:shared,2:direct_unclassified,3:direct_fallback_shared "
        "result=0:early_exit,1:success,2:empty "
        "old_work_barrier=ready+flush+SubmitAndWait+Barrier_FALSE "
        "direct_barrier=ready+flush+Submit+conditional_Barrier_FALSE "
        "native_host_stages_nested_in_platform_call=1\n");
    for (i = 0; i < PpcLmCount; ++i) {
        const struct PpcLmRecord *r = &PpcLmRecords[i];
#define PPC_LM_UL(v) ((unsigned long)(v))
        fprintf(file, "PPC_LM_ID seq=%lu context=%lu name=%lu shared_order=%lu\n",
            PPC_LM_UL(r->serial), PPC_LM_UL(r->contextId), PPC_LM_UL(r->name), PPC_LM_UL(r->sharedOrder));
        fprintf(file, "PPC_LM_RECT seq=%lu level=%ld x=%ld y=%ld\n",
            PPC_LM_UL(r->serial), (long)r->level, (long)r->x, (long)r->y);
        fprintf(file, "PPC_LM_SIZE seq=%lu w=%ld h=%ld\n",
            PPC_LM_UL(r->serial), (long)r->w, (long)r->h);
        fprintf(file, "PPC_LM_SOURCE seq=%lu format=%lu type=%lu bytes=%lu\n",
            PPC_LM_UL(r->serial), PPC_LM_UL(r->format), PPC_LM_UL(r->type), PPC_LM_UL(r->sourceBytes));
        fprintf(file, "PPC_LM_PATH seq=%lu path=%lu bgra_rect_bytes=%lu mailbox=%lu\n",
            PPC_LM_UL(r->serial), PPC_LM_UL(r->path), PPC_LM_UL(r->wireBytes), PPC_LM_UL(r->mailbox));
        fprintf(file, "PPC_LM_RESULT seq=%lu result=%lu error_before=%lu error_after=%lu\n",
            PPC_LM_UL(r->serial), PPC_LM_UL(r->result), PPC_LM_UL(r->errorBefore), PPC_LM_UL(r->errorAfter));
        fprintf(file, "PPC_LM_TOTAL seq=%lu start=%lu end=%lu ticks=%lu\n",
            PPC_LM_UL(r->serial), PPC_LM_UL(r->time.start), PPC_LM_UL(r->time.end),
            PPC_LM_UL((ULONG)(r->time.end - r->time.start)));
        for (phase = 0; phase < sizeof(PpcLmPhases)/sizeof(PpcLmPhases[0]); ++phase)
            if (r->time.seen & (1UL << phase))
                fprintf(file, "PPC_LM_PHASE seq=%lu stage=%s start=%lu end=%lu\n",
                    PPC_LM_UL(r->serial), PpcLmPhases[phase],
                    PPC_LM_UL(r->time.begin[phase]), PPC_LM_UL(r->time.finish[phase]));
#undef PPC_LM_UL
    }
    fclose(file);
}
#define PPC_LM_NEXT(phase) do { if (lm) MGLLmNext(&lm->time, (phase), PerfClock()); } while (0)
#else
#define PPC_LM_NEXT(phase) ((void)0)
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
        /* Yield VERY rarely, on the same bounded budget as the ring wait
         * rework in docs/reference/HOST_LOADER_REGRESSION.md: a 68k-side sleep
         * (Delay(1), WaitTime(0,100)) quantizes to a 50 Hz tick, about
         * 20 ms of actual latency on this kernel, so yielding added 1-4 of
         * those to every control round-trip. The 68k host runs on a
         * separate physical CPU and is never starved by a PPC spin; the
         * 100 us check interval already bounds the mailbox register reads.
         * CP_SURF evidence (2026-09-20): every yield actually parked a full
         * 20 ms tick (avg=max=20.0 ms over 2526 calls), so the budget is
         * now 32x the old one -- the 5 s deadline below still bounds a dead
         * host. */
        if (!(++relax & (MGLPPC_RING_YIELD_SPINS * 32UL - 1UL)))
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

/* CP surface-descriptor cache attribution (plan 2026-09-20). Counters are
 * plain increments on the emit path; the one-line summary prints at context
 * teardown only when CP is enabled, so quiet benchmarks gain one shutdown
 * console line, exactly like the host's CP_RUN report. */
/* Last descriptor-resolve failure (token, reason 1-4); failure
 * diagnostics for the cpfail.log emit line. */
static ULONG CpSurfFailToken, CpSurfFailReason, CpSurfFailValue;
static ULONG CpSurfHits, CpSurfMissQueries, CpSurfCapResets,
             CpSurfInvalidate, CpSurfEpochStale, CpSurfDropped, CpSurfCursor;

/* CP publish sub-phase attribution (plan 2026-09-20 W-10). Raw mftb ticks,
 * exclusive: descriptor-resolve time is subtracted from emit. Cumulative
 * whole-context (setup + load + demo); reported once at teardown. */
static ULONG CpPhasePublishes;
static ULONG CpPhaseWaitTicks, CpPhaseResolveTicks, CpPhaseEmitTicks,
             CpPhaseCopyTicks, CpPhasePublishTicks;

/* Wait-classification attribution (plan 2026-09-20 session 4). While the
 * publish wait loop stalls, sample the ring every 256 spins and attribute
 * the interval since the previous sample to the observed blocking state:
 *   host  - consumption backlog full (head - completed >= slots): the 68k
 *           host is not consuming entries fast enough;
 *   fence - consumption caught up but retirement lagging (completed >
 *           retired): GPU fences still pending (execution + observation);
 *   other - neither (sampling race or a momentarily stale line).
 * maxhost/maxpend record the deepest backlog / pending depth seen. The
 * final partial interval (<256 spins) is not attributed. Plain ULONG
 * counters; 32-bit wrap, like every other tick counter here. */
static ULONG CpWaitHostTicks, CpWaitFenceTicks, CpWaitOtherTicks;
static ULONG CpWaitMaxBacklog, CpWaitMaxPending;

/* Reset every CP attribution counter (descriptor cache, publish phases,
 * wait classification) without touching the cache itself. Called from
 * Lib_glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST) so a timedemo's counters
 * start at the demo boundary instead of covering context setup and map
 * load (plan 2026-09-20 W-11). */
void R200PPCResetCpAttribution(void)
{
    CpSurfHits = CpSurfMissQueries = CpSurfCapResets = 0;
    CpSurfInvalidate = CpSurfEpochStale = CpSurfDropped = 0;
    CpPhasePublishes = 0;
    CpPhaseWaitTicks = CpPhaseResolveTicks = CpPhaseEmitTicks = 0;
    CpPhaseCopyTicks = CpPhasePublishTicks = 0;
    CpWaitHostTicks = CpWaitFenceTicks = CpWaitOtherTicks = 0;
    CpWaitMaxBacklog = CpWaitMaxPending = 0;
}

/* Remove one token from the descriptor cache (swap-remove). Tokens are
 * unique among live surfaces, so the first match is the only match. */
static void FrontendDropCachedSurface(ULONG token)
{
    ULONG index;

    if (!token)
        return;
    for (index = 0; index < FrontendPlatform.cpSurfaceCount; ++index) {
        if (FrontendPlatform.cpSurfaces[index].token == token) {
            ULONG last = --FrontendPlatform.cpSurfaceCount;

            FrontendPlatform.cpSurfaces[index] =
                FrontendPlatform.cpSurfaces[last];
            ++CpSurfDropped;
            return;
        }
    }
}

/* Pre-command invalidation: the named texture's mirrored native surface is
 * about to be released or replaced. A released token is a raw handle address
 * the service can reallocate for a different surface within the same device
 * epoch (radeon3d_service.c: alloc/free handle), so a cached descriptor must
 * never survive its surface. Out-of-range names are ignored. */
static void FrontendDropCachedTextureSurface(ULONG textureName)
{
    if (!textureName || textureName >= MAX_TEXTURES)
        return;
    FrontendDropCachedSurface(
        (ULONG)proofContext.textures[textureName].surface.Handle);
}

/* Legacy chunked uploads (TEXTURE_UPLOAD_BEGIN/CHUNK/END) name the target
 * enum, not a texture; the native side acts on the currently bound texture.
 * Drop that texture's cached token before the command can replace it. */
static void FrontendDropCachedActiveTextureSurface(void)
{
    ULONG unit = proofContext.activeTexture;

    if (unit > 1UL)
        unit = 0;
    FrontendDropCachedTextureSurface(proofContext.boundTexture[unit]);
}

/* Descriptor-cache lifetime (plan 2026-09-20 §7 P0/W-01). Ordinary controls
 * no longer wipe the cache (the old blanket clear forced ~37k synchronous
 * mailbox queries in one timedemo), but every command that can release or
 * replace a texture surface drops the affected texture's cached token BEFORE
 * the native side can free it. A token is a raw handle address the service
 * can reallocate for a different surface in the same device epoch, so a
 * surviving entry could return a wrong address/pitch/format for a reused
 * token. Lifetime commands clear the whole cache; capacity overwrites the
 * oldest entry.
 *
 * Value layout matters: SUBIMAGE_SHARED and the DIRECT_* commands carry a
 * texture name in value[0]; UPLOAD_SHARED/UPLOAD_BEGIN/UPLOAD_END carry the
 * TARGET ENUM there (the native side acts on the currently bound texture).
 * Reading value[0] for those would drop the wrong texture (GL_TEXTURE_2D is
 * 174, a valid texture index) and leave the real target exposed.
 *
 * Residual risk (documented, W-06): native residency eviction during a
 * shared upload can free a DIFFERENT texture's surface without a reply field
 * naming it; the host reports texture_evictions, which was 0 on the
 * validated runs. Closing it needs an eviction counter in the shared-upload
 * reply (fold into the P1 host work); a blanket clear per shared upload is
 * too expensive during gameplay. */
static void FrontendDropCachedSurfacesForCommand(ULONG command)
{
    switch (command) {
    case MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS:
    case MGLPPC_CMD_DESTROY_CONTEXT:
    case MGLPPC_CMD_SHUTDOWN:
        if (FrontendPlatform.cpSurfaceCount)
            ++CpSurfInvalidate;
        FrontendPlatform.cpSurfaceCount = 0;
        break;
    case MGLPPC_CMD_TEXTURE_UPLOAD_SHARED:
    case MGLPPC_CMD_TEXTURE_UPLOAD_BEGIN:
    case MGLPPC_CMD_TEXTURE_UPLOAD_END:
        FrontendDropCachedActiveTextureSurface();
        break;
    case MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED:
    case MGLPPC_CMD_TEXTURE_DIRECT_BEGIN:
    case MGLPPC_CMD_TEXTURE_DIRECT_COMMIT:
    case MGLPPC_CMD_TEXTURE_DIRECT_ABORT:
        FrontendDropCachedTextureSurface(FrontendPlatform.control.value[0]);
        break;
    case MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2:
        FrontendDropCachedTextureSurface(FrontendPlatform.control.value[0]);
        FrontendDropCachedTextureSurface(FrontendPlatform.control.value[4]);
        break;
    case MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2:
        FrontendDropCachedTextureSurface(FrontendPlatform.control.value[0]);
        FrontendDropCachedTextureSurface(FrontendPlatform.control.value[2]);
        break;
    case MGLPPC_CMD_DELETE_TEXTURES:
        {
            ULONG dropIndex, dropCount = FrontendPlatform.control.value[0];

            if (dropCount > 7UL)
                dropCount = 7UL;
            for (dropIndex = 0; dropIndex < dropCount; ++dropIndex)
                FrontendDropCachedTextureSurface(
                    FrontendPlatform.control.value[dropIndex + 1UL]);
        }
        break;
    default:
        break;
    }
}

static BOOL PlatformCall(ULONG command, ULONG contextId)
{
#if MGLPPC_PROFILE
    ULONG profileStart = PerfClock();
#endif
    BOOL result;

    FrontendDropCachedSurfacesForCommand(command);

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
        MGLPPC_MAILBOX_COMMAND_ALLOWED(command) &&
        (command != MGLPPC_CMD_TEXTURE_UPLOAD_SHARED ||
         (FrontendPlatform.capabilities &
          MGLPPC_CAP_MAILBOX_UPLOAD_SHARED))) {
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

/* ---- Interface-20 PPC engine lease (protocol v16) ----
 * The frontend takes the lease over the transport (metadata only) and then
 * submits CP batches directly into the ring the driver published, skipping
 * the 68k entirely on the hot path. The heartbeat cadence is a fraction of
 * the driver's 5 s expiry bound; the reply layout matches the p96-driver
 * phase-3 gate descriptors (docs/09):
 *   value[0]=RingCpuAddress value[1]=RingGpuAddress value[2]=RingDwords
 *   value[3]=RingMask       value[4]=Bar2Base      value[5]=NextFence
 *   value[6]=MaxBatchDwords value[7]=HeartbeatMs                                   */

/* Heartbeat interval: renew at ~1/3 of the negotiated HeartbeatMs but never
 * below 100 ms or above 1500 ms (the driver bound is 5000 ms). */
static ULONG R200PPCLeaseHeartbeatMs(void)
{
    ULONG period = FrontendPlatform.leaseHeartbeatMs;
    if (!period)
        period = 500UL;
    if (period < 300UL)
        period = 300UL;
    if (period > 4500UL)
        period = 4500UL;
    return period;
}

/* Lease control round trip. A host advertising MGLPPC_CAP_MAILBOX_LEASE
 * serves it through the shared mailbox, which it polls ahead of Exec
 * messages; the Exec reply path cost 2.7-6.8 ms per handover against a
 * 0.15-0.4 ms host handler. Older hosts keep the Exec form, where
 * LEASE_BEGIN carries the context id as its single payload word. Request
 * and reply values live in FrontendPlatform.control either way. */
static BOOL LeaseCall(ULONG command)
{
    MGLPPCMessage *message = &FrontendPlatform.control;

    if (FrontendPlatform.mailbox && !FrontendPlatform.mailboxAbandoned &&
        (FrontendPlatform.capabilities & MGLPPC_CAP_MAILBOX_LEASE))
        return PlatformCall(command, FrontendPlatform.contextId);
    message->command = command;
    message->contextId = FrontendPlatform.contextId;
    message->payloadWords = command == MGLPPC_CMD_LEASE_BEGIN ? 1UL : 0UL;
    message->payload[0] = FrontendPlatform.contextId;
    message->status = MGLPPC_STATUS_BUSY;
    return MGLPPCTransportCall(&FrontendPlatform.transport, message) &&
           message->status == MGLPPC_STATUS_OK;
}

BOOL MGLPPCLeaseBegin(void)
{
    MGLPPCMessage *message = &FrontendPlatform.control;
    volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;

    if (!FrontendPlatform.leaseAvailable || FrontendPlatform.leaseActive ||
        !control)
        return FALSE;
    if (!PlatformMailboxReady())
        return FALSE;
    if (!LeaseCall(MGLPPC_CMD_LEASE_BEGIN))
        return FALSE;
    FrontendPlatform.leaseRing = (volatile ULONG *)message->value[0];
    FrontendPlatform.leaseRingGpu = message->value[1];
    FrontendPlatform.leaseRingDwords = message->value[2];
    FrontendPlatform.leaseRingMask = message->value[3];
    FrontendPlatform.leaseBar2 = message->value[4];
    FrontendPlatform.leaseNextFence = message->value[5];
    FrontendPlatform.leaseMaxBatch = message->value[6];
    FrontendPlatform.leaseHeartbeatMs = message->value[7];
    /* Sanity: ring must be 16-byte aligned, dword count a power of two, and
     * the granted batch limit must fit a whole burst. */
    if (!FrontendPlatform.leaseRing ||
        (FrontendPlatform.leaseRingDwords & (FrontendPlatform.leaseRingDwords - 1UL)) ||
        FrontendPlatform.leaseRingDwords < 4096UL ||
        !FrontendPlatform.leaseBar2 ||
        !FrontendPlatform.leaseMaxBatch ||
        FrontendPlatform.leaseMaxBatch > MGLPPC_CP_DWORDS) {
        /* Driver keeps ownership; nothing to release. */
        return FALSE;
    }
    FrontendPlatform.leaseActive = TRUE;
    FrontendPlatform.leaseLastHeartbeat = PerfClock();
    R200PPCConsolePrintf(
        "LEASE begin ring=%08lx dwords=%lu mask=%08lx bar2=%08lx "
        "fence=%lu max=%lu hb=%lums\n",
        (unsigned long)FrontendPlatform.leaseRing,
        (unsigned long)FrontendPlatform.leaseRingDwords,
        (unsigned long)FrontendPlatform.leaseRingMask,
        (unsigned long)FrontendPlatform.leaseBar2,
        (unsigned long)FrontendPlatform.leaseNextFence,
        (unsigned long)FrontendPlatform.leaseMaxBatch,
        (unsigned long)FrontendPlatform.leaseHeartbeatMs);
    return TRUE;
}

BOOL MGLPPCLeaseHeartbeat(void)
{
    if (!FrontendPlatform.leaseActive)
        return FALSE;
    if (!LeaseCall(MGLPPC_CMD_LEASE_HEARTBEAT)) {
        /* Stale lease: the driver already reclaimed the ring. Drop the
         * local ownership so the next Execute falls back to the bounded
         * paths; a later LEASE_BEGIN may re-take the lease. */
        FrontendPlatform.leaseActive = FALSE;
        R200PPCConsolePrintf("LEASE heartbeat lost (stale)\n");
        return FALSE;
    }
    FrontendPlatform.leaseLastHeartbeat = PerfClock();
    return TRUE;
}

static BOOL R200LeaseIdleFence(void);

BOOL MGLPPCLeaseEnd(void)
{
    MGLPPCMessage *message = &FrontendPlatform.control;
    BOOL ok;

    if (!FrontendPlatform.leaseActive)
        return TRUE;
    /* Without per-batch WAIT_IDLE the last fence only proves the CP read
     * the stream; hand the engine back idle as the driver's 2D restore
     * expects. */
    if (FrontendPlatform.leaseNoIdle && !FrontendPlatform.cpFailed)
        (void)R200LeaseIdleFence();
    message->value[0] = FrontendPlatform.leaseLastFence;
    ok = LeaseCall(MGLPPC_CMD_LEASE_END);
    FrontendPlatform.leaseActive = FALSE;
    if (!ok)
        R200PPCConsolePrintf("LEASE end failed status=%lu\n",
                             (unsigned long)message->status);
    return ok;
}

/* Renew the lease if the heartbeat cadence has elapsed. Called from the
 * frame path; a transport heartbeat costs one message round trip. */
static void R200PPCLeasePump(void)
{
    ULONG periodMs = R200PPCLeaseHeartbeatMs();
    ULONG elapsed = PerfClockDeltaMs(FrontendPlatform.leaseLastHeartbeat);

    if (FrontendPlatform.leaseActive && elapsed >= periodMs)
        (void)MGLPPCLeaseHeartbeat();
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
    /* Present default is no vblank wait: benchmark and game clients run
     * unsynced unless they explicitly mglEnableSync(GL_TRUE). The host
     * applies the published state at the first ring entry it consumes
     * (syncSeq 1 != the host's 0 initial), so no present escapes with the
     * library's own sync-on default. */
    FrontendPlatform.syncEnabled = 0;
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
    PpcLmCount = PpcLmDropped = PpcLmSerial = PpcLmSharedOrder = 0;
    PpcLmDumped = FALSE;
#endif
    R200PPCPlatformResetClientDefaults();
    /* The transport speaks MGLPPC_PROTOCOL_VERSION; the peer is assumed to
     * match until a BAD_VERSION HELLO downgrades it. This stayed at 15
     * through the v16 bump, which silently disabled the shared mailbox
     * (registration requires peerProtocol == MGLPPC_PROTOCOL_VERSION) and
     * sent every control call over the Exec round trip. */
    FrontendPlatform.peerProtocol = MGLPPC_PROTOCOL_VERSION;
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
        UBYTE verbose[8], nobatch[8];
        verbose[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_VERBOSE", verbose, (LONG)sizeof(verbose),
               GVF_GLOBAL_ONLY);
        FrontendConsoleVerbose = verbose[0] ? 1UL : 0UL;
        verbose[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_NOPARK", verbose, (LONG)sizeof(verbose),
               GVF_GLOBAL_ONLY);
        FrontendNoPark = verbose[0] == '1' ? 1UL : 0UL;
        verbose[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_NOPOOL", verbose, (LONG)sizeof(verbose),
               GVF_GLOBAL_ONLY);
        FrontendNoPool = verbose[0] == '1' ? 1UL : 0UL;
        verbose[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_FORCEMIP", verbose, (LONG)sizeof(verbose),
               GVF_GLOBAL_ONLY);
        {
            extern ULONG MGLPPCForceMip;
            MGLPPCForceMip = verbose[0] == '1' ? 1UL : 0UL;
        }
        verbose[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_CLEARBLEND", verbose, (LONG)sizeof(verbose),
               GVF_GLOBAL_ONLY);
        MGLPPCClearBlend = verbose[0] == '1' ? 1UL : 0UL;
        FrontendLmPoolReset();
        /* EXP-FORCENORMAL: A/B switch for the 2D layout investigation.
         * Forces hardware-TCL draws onto the normal-vertex layout (13 or
         * compact-10 dwords, DRAW_NORMALS set) so the TCL input state of
         * the known-good 3D path is used for 2D text too. Diagnostic only. */
        nobatch[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_FORCENORMAL", nobatch,
               (LONG)sizeof(nobatch), GVF_GLOBAL_ONLY);
        R200PPCForceNormalVertex = nobatch[0] ? 1UL : 0UL;
        /* The batched two-tile path is opt-in: on the current host the
         * deferred BEGIN2 lands in a window where the mailbox service
         * latency is ~16 ms (vs ~0.3 ms for an immediate BEGIN), which
         * costs more than the two round trips it saves. See
         * docs/textures.md. MGLPPC_BATCH=1 enables it. */
        nobatch[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_BATCH", nobatch, (LONG)sizeof(nobatch),
               GVF_GLOBAL_ONLY);
        FrontendPlatform.batchDisabled = nobatch[0] == '1' ? 0 : 1;
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

/* Descriptors of pooled lightmap holder surfaces (see FrontendLmPoolUpdate).
 * Kept outside the 128-entry descriptor cache: a pool rotates three tokens
 * per atlas every frame and would thrash it. Open addressing on the handle
 * address; entries live as long as the context (holders are never freed
 * before DestroyContext). */
#define R200_POOL_HASH 256UL

static struct {
    ULONG token, epoch;
    struct MGLPPCEmitSurface surface;
} FrontendPoolHash[R200_POOL_HASH];

static BOOL FrontendPoolResolve(ULONG token, ULONG epoch,
                                struct MGLPPCEmitSurface *out)
{
    ULONG index = (token >> 3) & (R200_POOL_HASH - 1UL), probe;

    for (probe = 0; probe < R200_POOL_HASH; ++probe) {
        if (!FrontendPoolHash[index].token)
            return FALSE;
        if (FrontendPoolHash[index].token == token) {
            if (FrontendPoolHash[index].epoch != epoch)
                return FALSE;
            *out = FrontendPoolHash[index].surface;
            return TRUE;
        }
        index = (index + 1UL) & (R200_POOL_HASH - 1UL);
    }
    return FALSE;
}

static BOOL FrontendPoolHashInsert(ULONG token, ULONG epoch,
                                   const struct MGLPPCEmitSurface *surface)
{
    ULONG index = (token >> 3) & (R200_POOL_HASH - 1UL), probe;

    for (probe = 0; probe < R200_POOL_HASH; ++probe) {
        if (!FrontendPoolHash[index].token ||
            FrontendPoolHash[index].token == token) {
            FrontendPoolHash[index].token = token;
            FrontendPoolHash[index].epoch = epoch;
            FrontendPoolHash[index].surface = *surface;
            return TRUE;
        }
        index = (index + 1UL) & (R200_POOL_HASH - 1UL);
    }
    return FALSE;
}

static BOOL ResolveCpSurfaceInner(void *user, ULONG token,
                                  struct MGLPPCEmitSurface *out)
{
    struct MiniGLR200Context *context = user;
    ULONG index;
    ULONG insertIndex = MGLPPC_CP_SURFACE_SLOTS; /* sentinel: append/ring */
    CpSurfFailToken = token;
    if (!token || !FrontendPlatform.cpEnabled || FrontendPlatform.cpFailed) {
        CpSurfFailReason = 1;
        return FALSE;
    }
    if (FrontendPoolResolve(token, context->deviceEpoch, out)) {
        ++CpSurfHits;
        return TRUE;
    }
    for (index = 0; index < FrontendPlatform.cpSurfaceCount; ++index)
        if (FrontendPlatform.cpSurfaces[index].token == token) {
            if (FrontendPlatform.cpSurfaces[index].epoch == context->deviceEpoch) {
                ++CpSurfHits;
                *out = FrontendPlatform.cpSurfaces[index].surface;
                return TRUE;
            }
            ++CpSurfEpochStale;
            insertIndex = index;
            break;
        }
    ++CpSurfMissQueries;
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = token;
    if (!PlatformCall(MGLPPC_CMD_CP_SURFACE, FrontendPlatform.contextId)) {
        CpSurfFailReason = 2;
        CpSurfFailValue = FrontendPlatform.control.status;
        return FALSE;
    }
    if (FrontendPlatform.control.value[0] != GL_NO_ERROR ||
        FrontendPlatform.control.value[7] != context->deviceEpoch) {
        CpSurfFailReason = FrontendPlatform.control.value[0] != GL_NO_ERROR
                               ? 3UL : 4UL;
        CpSurfFailValue = FrontendPlatform.control.value[0];
        return FALSE;
    }
    out->cpuAddress = FrontendPlatform.control.value[1];
    out->gpuAddress = FrontendPlatform.control.value[2];
    out->pitch = FrontendPlatform.control.value[3];
    out->width = FrontendPlatform.control.value[4];
    out->height = FrontendPlatform.control.value[5];
    out->format = FrontendPlatform.control.value[6];
    if (insertIndex < MGLPPC_CP_SURFACE_SLOTS) {
        /* An epoch-stale entry for this same token: replace it in place.
         * Appending would leave the stale entry to shadow the fresh one and
         * force a mailbox query on every resolve until the ring wraps. */
        index = insertIndex;
    } else if (FrontendPlatform.cpSurfaceCount == MGLPPC_CP_SURFACE_SLOTS) {
        /* Capacity: overwrite the oldest entry (ring) instead of wiping the
         * whole cache. Wiping forced the next 16 lookups through synchronous
         * mailbox queries (the CP_SURF miss storm of 2026-09-20). */
        ++CpSurfCapResets;
        index = CpSurfCursor++ % MGLPPC_CP_SURFACE_SLOTS;
    } else {
        index = FrontendPlatform.cpSurfaceCount++;
    }
    FrontendPlatform.cpSurfaces[index].token = token;
    FrontendPlatform.cpSurfaces[index].epoch = context->deviceEpoch;
    FrontendPlatform.cpSurfaces[index].surface = *out;
    return TRUE;
}

/* Timing wrapper: the emitter calls this per resolved surface, so the
 * descriptor work (cache lookup + rare mailbox query) is attributed
 * separately from command construction in PublishCpSlot. */
static BOOL ResolveCpSurface(void *user, ULONG token,
                             struct MGLPPCEmitSurface *out)
{
    ULONG t0 = PerfClock();
    BOOL ok = ResolveCpSurfaceInner(user, token, out);

    CpPhaseResolveTicks += PerfClock() - t0;
    return ok;
}

/* Ring control v6 live color target. The host owns the native display
 * rotation, so fullscreen CP records cannot bake a token the PPC derives
 * locally (its surfaces mirror only exposes surface 0 at create). The host
 * publishes the current drawing target plus a sequence that moves on every
 * display switch; a sequence change re-stamps every record target dword
 * (the same walk as the host's PatchRecordTargets) before the emitter
 * resolves them into CP words. ResolveCpSurface then fetches the matching
 * descriptor through MGLPPC_CMD_CP_SURFACE; the descriptor cache holds the
 * rotating screen-buffer and texture tokens (see MGLPPC_CP_SURFACE_SLOTS). */
static void RefreshCpColorTarget(volatile MGLPPCRingControlV1 *control,
                                 ULONG *records, ULONG recordDwords)
{
    ULONG token, offset = 0, seq;

    if (!control || !records || !recordDwords)
        return;
    MGLPPCInvalidateLine32((APTR)&control->colorToken);
    seq = control->colorSeq;
    if (seq == FrontendPlatform.cpColorSeq)
        return;
    token = control->colorToken;
    FrontendPlatform.cpColorSeq = seq;
    if (!token || token == FrontendPlatform.cpColorToken)
        return;
    FrontendPlatform.cpColorToken = token;
    while (offset + 3UL <= recordDwords) {
        ULONG opcode = records[offset];
        ULONG length = records[offset + 1UL];

        if (opcode < RADEON3D_EXEC_CLEAR ||
            opcode > RADEON3D_EXEC_DRAW_LINE_LOOP ||
            !length || length > recordDwords - offset)
            break;
        records[offset + 2UL] = token;
        offset += length;
    }
}

/* ---- Lease local ring submission (phase-3-validated chain) ----
 *
 * The frontend owns the CP ring while the lease is live; the 68k host
 * refuses every other submission. Per batch, the exact sequence the
 * p96-driver phase-3 gate validated across 20,000+ hardware batches
 * (including a full ring wrap and 10 s paced runs):
 *
 *   1. reserve space: poll RB_RPTR (BAR2+0x0710), bounded spins;
 *      available = (rptr - wptr - 1) & mask
 *   2. write the batch with stwbrx bursts (R200PPCCopyRingBr), splitting
 *      at the ring wrap; the stream carries the 6-dword fence tail
 *      (DSTCACHE_CTLSTAT flush, WAIT_UNTIL idle, SCRATCH_REG0 sequence)
 *      padded to the ring's 16-dword alignment
 *   3. read back the final dword (posted-write ordering check)
 *   4. publish WPTR (BAR2+0x0714) with a committed write (store +
 *      readback)
 *   5. retire locally by polling SCRATCH_REG0 (BAR2+0x15e0) for the
 *      sequence (SCRATCH advances in ring order)
 *
 * Lease sequences continue the driver's fence space from NextFence; the
 * 68k path is inactive until LEASE_END adopts the last sequence. */

/* Fence tail shape (p96-driver tools/phase3/phase3_regs.h). */
#define R200_LEASE_PACKET2          0x80000000UL
#define R200_LEASE_PACKET0(reg)     ((ULONG)(reg) >> 2)
#define R200_LEASE_DSTCACHE_CTLSTAT 0x1714UL
#define R200_LEASE_WAIT_UNTIL       0x1720UL
#define R200_LEASE_CACHE_FLUSH_ALL  0x0000000fUL
#define R200_LEASE_WAIT_IDLE \
    ((1UL << 9) | (1UL << 16) | (1UL << 17) | (1UL << 18))
#define R200_LEASE_SCRATCH_REG0     0x15e0UL
#define R200_LEASE_FENCE_TAIL       6UL
#define R200_LEASE_ALIGN            16UL
/* Both lease waits fail only after this much wall time with no RB_RPTR /
 * SCRATCH movement; any movement re-arms the deadline. */
#define R200_LEASE_WAIT_MS          1000UL
#define R200_LEASE_PUMP_MASK        255UL

/* BAR2 register offsets (phase-3 validated). */
#define R200_LEASE_RB_RPTR          0x0710UL
#define R200_LEASE_RB_WPTR          0x0714UL

static volatile void *R200LeaseReg(ULONG offset)
{
    return (volatile void *)((volatile UBYTE *)FrontendPlatform.leaseBar2 +
                             offset);
}

/* Lease wait policy, one step per poll. The previous loops tested
 * !(poll & 1023) from poll 0, so any batch whose fence had not landed on
 * the first read parked a full ~20 ms WaitTime tick (PublishCpSlot notes
 * the same kernel quantum) -- a ~15 fps ceiling at gears' 3.4 batches per
 * frame. Spin while the hardware moves; pump the heartbeat every 256 polls
 * (time-scheduled inside the pump); park only after
 * MGLPPC_RING_YIELD_SPINS polls with no movement; fail only after
 * R200_LEASE_WAIT_MS of wall time with no movement. */
struct R200LeaseWait {
    ULONG seen, spin, deadline;
};

/* Lease handover timing (PerfClock ticks), written once at context
 * teardown to DH2:leasestat.log. RAM counters only on the frame path. */
static ULONG LeaseStatEndTicks, LeaseStatBeginTicks, LeaseStatResumeTicks;
static ULONG LeaseStatFenceTicks, LeaseStatHandovers, LeaseStatParks;
static ULONG LeaseStatBatches;

static void R200LeaseWaitBegin(struct R200LeaseWait *wait, ULONG value)
{
    wait->seen = value;
    wait->spin = 0;
    wait->deadline = PerfClock() +
                     PlatformClockRate / 1000UL * R200_LEASE_WAIT_MS;
}

static const char *R200LeaseWaitStep(struct R200LeaseWait *wait, ULONG value,
                                     const char *timeout, BOOL leaseHeld)
{
    if (value != wait->seen) {
        R200LeaseWaitBegin(wait, value);
        return NULL;
    }
    if (!(++wait->spin & R200_LEASE_PUMP_MASK)) {
        if ((LONG)(PerfClock() - wait->deadline) >= 0)
            return timeout;
        if (leaseHeld) {
            R200PPCLeasePump();
            if (!FrontendPlatform.leaseActive)
                return "stalelease";
        }
    }
    if (wait->spin >= MGLPPC_RING_YIELD_SPINS) {
        wait->spin = 0;
        ++LeaseStatParks;
        MGLPPCYield();
    }
    return NULL;
}

static BOOL SubmitLeaseBatch(struct MiniGLR200Context *context,
                             struct SubmitSlot *slot)
{
    struct MGLPPCEmitVertices vertices;
    ULONG *staging = FrontendPlatform.leaseStaging;
    volatile ULONG *ring = FrontendPlatform.leaseRing;
    volatile void *rbRptr = R200LeaseReg(R200_LEASE_RB_RPTR);
    volatile void *rbWptr = R200LeaseReg(R200_LEASE_RB_WPTR);
    volatile void *scratch0 = R200LeaseReg(R200_LEASE_SCRATCH_REG0);
    ULONG mask = FrontendPlatform.leaseRingMask;
    ULONG ringDwords = FrontendPlatform.leaseRingDwords;
    ULONG maxBatch = FrontendPlatform.leaseMaxBatch;
    ULONG segmentIx = 0xffffffffUL;
    ULONG wptr, space, count, index, total, firstRun;
    ULONG sequence = 0, readback, newWptr, value, fenceStart, retire;
    struct R200LeaseWait wait;
    ULONG start = PerfClock();
    const char *failure = "ring";

    if (!ring || !FrontendPlatform.leaseBar2) {
        failure = "nodesc";
        goto failed;
    }
    memset(&vertices, 0, sizeof(vertices));
    if (slot->commitMode && slot->drawCount) {
        for (index = 0; index < context->segmentPoolCount; ++index)
            if (context->segmentPool[index].Id == context->segment.info.Id) {
                segmentIx = index;
                break;
            }
        if (segmentIx == 0xffffffffUL) {
            failure = "nosegment";
            goto failed;
        }
        vertices.gpuAddress = context->segment.info.GpuAddress;
        vertices.bytes = context->segment.info.Bytes;
        vertices.drawCount = slot->drawCount;
        vertices.stateBatch = slot->stateBatch;
        vertices.offsets = slot->stateBatch ? (const ULONG *)slot->stateBatchDraws :
                                            slot->vertexOffsets;
    }
    /* v6: resolve the drawing buffer before the emitter bakes the target. */
    RefreshCpColorTarget(FrontendPlatform.ring, slot->records,
                         slot->recordCount);
    count = MGLPPCEmitBatch(staging, slot->records, slot->recordCount,
                            vertices.drawCount ? &vertices : NULL,
                            ResolveCpSurface, context);
    if (!count || count > MGLPPC_CP_DWORDS) {
        char line[128];

        sprintf(line, "LEASE emit stage=%lu surf=%lu/%08lx/%lu rec=%lu "
                "draws=%lu sb=%lu fence=%lu\n",
                (unsigned long)MGLPPCEmitFailStage,
                (unsigned long)CpSurfFailReason,
                (unsigned long)CpSurfFailToken,
                (unsigned long)CpSurfFailValue,
                (unsigned long)slot->recordCount,
                (unsigned long)slot->drawCount,
                (unsigned long)slot->stateBatch,
                (unsigned long)FrontendPlatform.leaseNextFence);
        MGLPPCIBTrace("DH2:cpfail.log", line);
        failure = "emit";
        goto failed;
    }
    if (count > maxBatch) {
        /* The driver granted a smaller batch limit than the emitter can
         * produce; refuse rather than overflow the granted window. */
        failure = "maxbatch";
        goto failed;
    }
    /* Classic CP dispatch needs an even dword count. */
    if (count & 1UL)
        staging[count++] = R200_LEASE_PACKET2;
    if (vertices.drawCount)
        MGLPPCFlushDataRange((APTR)((ULONG)context->segment.info.CpuAddress +
            context->buildSlot * SEGMENT_HALF_BYTES), slot->segmentCursor);
    /* Fence tail: cache flush, full idle wait, scratch sequence; pad the
     * whole submission to the ring's 16-dword alignment with PACKET2. */
    sequence = FrontendPlatform.leaseNextFence;
    if (!sequence)
        sequence = 1UL;
    staging[count++] = R200_LEASE_PACKET0(R200_LEASE_DSTCACHE_CTLSTAT);
    staging[count++] = R200_LEASE_CACHE_FLUSH_ALL;
    if (!FrontendPlatform.leaseNoIdle) {
        staging[count++] = R200_LEASE_PACKET0(R200_LEASE_WAIT_UNTIL);
        staging[count++] = R200_LEASE_WAIT_IDLE;
    }
    staging[count++] = R200_LEASE_PACKET0(R200_LEASE_SCRATCH_REG0);
    staging[count++] = sequence;
    while (count & (R200_LEASE_ALIGN - 1UL))
        staging[count++] = R200_LEASE_PACKET2;
    total = count;

    /* Heartbeat pump before the (possibly long) ring wait. */
    R200PPCLeasePump();
    if (!FrontendPlatform.leaseActive) {
        failure = "stalelease";
        goto failed;
    }

    wptr = FrontendPlatform.leaseWptr;
    /* Reserve: available = (rptr - wptr - 1) & mask; the CP advances
     * RB_RPTR as it consumes dwords. */
    value = R200PPCMmioRead32(rbRptr) & mask;
    R200LeaseWaitBegin(&wait, value);
    for (;;) {
        space = (value - wptr - 1UL) & mask;
        if (space >= total)
            break;
        if ((failure = R200LeaseWaitStep(&wait, value, "reserve", TRUE)) != NULL)
            goto failed;
        value = R200PPCMmioRead32(rbRptr) & mask;
    }

    /* Write the stream at wptr, splitting at the ring wrap. The ring
     * dwords must arrive on the bus little-endian: stwbrx bursts. */
    firstRun = ringDwords - wptr;
    if (firstRun > total)
        firstRun = total;
    R200PPCCopyRingBr(ring + wptr, staging, firstRun);
    if (firstRun < total)
        R200PPCCopyRingBr(ring, staging + firstRun, total - firstRun);

    /* Posted-write ordering: the final dword must read back. */
    readback = R200PPCMmioRead32((volatile void *)
            (ring + ((wptr + total - 1UL) & mask)));
    if (readback != staging[total - 1UL]) {
        failure = "readback";
        goto failed;
    }

    /* Publish WPTR with the driver's committed shape (store + readback). */
    newWptr = (wptr + total) & mask;
    if (R200PPCMmioWriteCommit(rbWptr, newWptr) != newWptr) {
        failure = "wptr";
        goto failed;
    }
    FrontendPlatform.leaseWptr = newWptr;
    FrontendPlatform.leaseNextFence = sequence + 1UL;
    FrontendPlatform.leaseLastFence = sequence;

    /* Local retirement: SCRATCH_REG0 advances in ring order, so the
     * test is wrap-safe "at or past". */
    value = R200PPCMmioRead32(scratch0);
    R200LeaseWaitBegin(&wait, value);
    ++LeaseStatBatches;
    fenceStart = PerfClock();
    /* NOIDLE=2: the next build writes the other vertex half, last read by
     * the previous batch; one batch may stay in flight. */
    retire = FrontendPlatform.leaseNoIdle == 2UL ? sequence - 1UL : sequence;
    while ((LONG)(value - retire) < 0) {
        if ((failure = R200LeaseWaitStep(&wait, value, "fence", TRUE)) != NULL)
            goto failed;
        value = R200PPCMmioRead32(scratch0);
    }
    LeaseStatFenceTicks += PerfClock() - fenceStart;
    ++CpPhasePublishes;
    CpPhasePublishTicks += PerfClock() - start;
    return TRUE;
failed:
    /* A lease batch failure is terminal for the lease: release it (last
     * fence so the driver can drain what actually executed) and drop to
     * the bounded paths. */
    R200PPCConsolePrintf("LEASE submit failed: %s fence=%lu\n",
                         failure, (unsigned long)sequence);
    MGLPPCIBTrace("DH2:cpfail.log", failure);
    (void)MGLPPCLeaseEnd();
    FrontendPlatform.cpFailed = TRUE;
    return FALSE;
}

/* Flush + WAIT_IDLE + SCRATCH tail as its own submission, waited to
 * retirement: the engine is idle afterwards. */
static BOOL R200LeaseIdleFence(void)
{
    ULONG words[R200_LEASE_ALIGN];
    volatile void *rbRptr = R200LeaseReg(R200_LEASE_RB_RPTR);
    volatile void *scratch0 = R200LeaseReg(R200_LEASE_SCRATCH_REG0);
    ULONG mask = FrontendPlatform.leaseRingMask;
    ULONG wptr = FrontendPlatform.leaseWptr, value, count = 0, firstRun;
    ULONG sequence = FrontendPlatform.leaseNextFence;
    struct R200LeaseWait wait;

    if (!sequence)
        sequence = 1UL;
    words[count++] = R200_LEASE_PACKET0(R200_LEASE_DSTCACHE_CTLSTAT);
    words[count++] = R200_LEASE_CACHE_FLUSH_ALL;
    words[count++] = R200_LEASE_PACKET0(R200_LEASE_WAIT_UNTIL);
    words[count++] = R200_LEASE_WAIT_IDLE;
    words[count++] = R200_LEASE_PACKET0(R200_LEASE_SCRATCH_REG0);
    words[count++] = sequence;
    while (count < R200_LEASE_ALIGN)
        words[count++] = R200_LEASE_PACKET2;
    value = R200PPCMmioRead32(rbRptr) & mask;
    R200LeaseWaitBegin(&wait, value);
    while (((value - wptr - 1UL) & mask) < count) {
        if (R200LeaseWaitStep(&wait, value, "idle", TRUE) != NULL)
            return FALSE;
        value = R200PPCMmioRead32(rbRptr) & mask;
    }
    firstRun = FrontendPlatform.leaseRingDwords - wptr;
    if (firstRun > count)
        firstRun = count;
    R200PPCCopyRingBr(FrontendPlatform.leaseRing + wptr, words, firstRun);
    if (firstRun < count)
        R200PPCCopyRingBr(FrontendPlatform.leaseRing, words + firstRun,
                          count - firstRun);
    wptr = (wptr + count) & mask;
    if (R200PPCMmioWriteCommit(R200LeaseReg(R200_LEASE_RB_WPTR), wptr) != wptr)
        return FALSE;
    FrontendPlatform.leaseWptr = wptr;
    FrontendPlatform.leaseNextFence = sequence + 1UL;
    FrontendPlatform.leaseLastFence = sequence;
    value = R200PPCMmioRead32(scratch0);
    R200LeaseWaitBegin(&wait, value);
    while ((LONG)(value - sequence) < 0) {
        if (R200LeaseWaitStep(&wait, value, "idle", TRUE) != NULL)
            return FALSE;
        value = R200PPCMmioRead32(scratch0);
    }
    return TRUE;
}

/* LEASE_BEGIN plus local write-cursor sync from the live RB_WPTR. */
static BOOL R200PPCLeaseAcquire(void)
{
    if (!MGLPPCLeaseBegin())
        return FALSE;
    FrontendPlatform.leaseWptr =
        R200PPCMmioRead32(R200LeaseReg(R200_LEASE_RB_WPTR)) &
        FrontendPlatform.leaseRingMask;
    return TRUE;
}

/* C1 re-acquire: the lease was handed back for a present. Wait for the
 * host to consume the present (completed == head): it has then switched
 * the display and published the new color target, so the next batch
 * stamps the surface the host owns. Falling back to the bounded path
 * instead kept ~94% of gears' batches off the lease (1899 of 2031). The
 * wait is progress-aware like the lease waits; a host that stops
 * consuming for R200_LEASE_WAIT_MS leaves this batch on the bounded path. */
static void R200PPCLeaseResume(volatile MGLPPCRingControlV1 *control)
{
    struct R200LeaseWait wait;
    ULONG completed, t0;

    if (!FrontendPlatform.leaseResume || FrontendPlatform.leaseActive ||
        FrontendPlatform.cpFailed || !control)
        return;
    t0 = PerfClock();
    MGLPPCInvalidateLine32((APTR)&control->head);
    MGLPPCInvalidateLine32((APTR)&control->completed);
    completed = control->completed;
    R200LeaseWaitBegin(&wait, completed);
    while (completed != control->head) {
        if (control->error)
            return;
        if (R200LeaseWaitStep(&wait, completed, "present", FALSE) != NULL)
            return;
        MGLPPCInvalidateLine32((APTR)&control->head);
        MGLPPCInvalidateLine32((APTR)&control->completed);
        completed = control->completed;
    }
    LeaseStatResumeTicks += PerfClock() - t0;
    t0 = PerfClock();
    if (R200PPCLeaseAcquire()) {
        LeaseStatBeginTicks += PerfClock() - t0;
        ++LeaseStatHandovers;
    } else {
        /* Refused: stay on the bounded path for the rest of the run. */
        FrontendPlatform.leaseResume = FALSE;
        R200PPCConsolePrintf("LEASE re-acquire refused; bounded path\n");
    }
}

/* v7 CP segment FIFO. Batches are packed at 16-byte granularity; the
 * region of the oldest unretired batch-bearing entry is the tail, so a
 * batch's space is reused only after its entry retired -- the same GPU-read
 * guard the fixed slices had, at a fraction of the space per batch. A batch
 * that does not fit before the segment end wraps to offset 0 (the skipped
 * tail stays unused until it retires). Waits use the progress-aware policy
 * of the publish loop and are attributed to CP_WAIT fence. */
static BOOL CpRingFits(ULONG cursor, ULONG tail, BOOL empty, ULONG bytes,
                       ULONG *offset)
{
    if (empty) {
        *offset = cursor + bytes <= MGLPPC_CP_SEGMENT_BYTES ? cursor : 0;
        return TRUE;
    }
    if (cursor > tail) {
        if (cursor + bytes <= MGLPPC_CP_SEGMENT_BYTES) {
            *offset = cursor;
            return TRUE;
        }
        if (bytes < tail) {
            *offset = 0;
            return TRUE;
        }
        return FALSE;
    }
    /* cursor == tail with live batches means the FIFO is full. */
    if (cursor < tail && cursor + bytes < tail) {
        *offset = cursor;
        return TRUE;
    }
    return FALSE;
}

static BOOL CpRingReserve(volatile MGLPPCRingControlV1 *control, ULONG head,
                          ULONG bytes, ULONG *offset)
{
    ULONG spin = 0, retiredSeen = 0, waitStart = 0;

    for (;;) {
        ULONG serial, tail = 0;
        BOOL empty = TRUE;

        MGLPPCInvalidateLine32((APTR)&control->retired);
        for (serial = control->retired; serial != head; ++serial) {
            ULONG ringSlot = serial % MGLPPC_RING_SLOTS;

            if (FrontendPlatform.cpEntrySerial[ringSlot] == serial &&
                FrontendPlatform.cpEntryBytes[ringSlot]) {
                tail = FrontendPlatform.cpEntryStart[ringSlot];
                empty = FALSE;
                break;
            }
        }
        if (CpRingFits(FrontendPlatform.cpRingCursor, tail, empty, bytes,
                       offset)) {
            if (waitStart)
                CpWaitFenceTicks += PerfClock() - waitStart;
            return TRUE;
        }
        if (!waitStart) {
            waitStart = PerfClock();
            retiredSeen = control->retired;
        }
        if (control->error || control->epoch != FrontendPlatform.epoch ||
            PerfClock() - waitStart >= PlatformClockRate * 5UL)
            return FALSE;
        if (control->retired != retiredSeen) {
            retiredSeen = control->retired;
            spin = 0;
        }
        if (++spin >= MGLPPC_RING_YIELD_SPINS) {
            spin = 0;
            MGLPPCYield();
        }
    }
}

static BOOL PublishCpSlot(struct MiniGLR200Context *context, struct SubmitSlot *slot)
{
    volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
    volatile MGLPPCRingEntryV1 *entry;
    volatile ULONG *destination;
    struct MGLPPCEmitVertices vertices;
    ULONG head = 0, ringSlot = 0xffffffffUL, count = 0, index;
    ULONG segmentIx = 0xffffffffUL;
    ULONG stallReports = 0, stallReportNext = 0;
    ULONG cpRetiredSeen = 0;
    ULONG waitSamples = 0, waitSampleTime = 0, waitClass = 0;
    ULONG *staging, start = PerfClock(), spin = 0;
    ULONG waitStart, emitStart, copyStart, publishStart;
    ULONG cpBytes, cpOffset = 0;
    ULONG resolveBefore, emitElapsed, resolveDelta;
    const char *failure = "ring";
    /* Interface-20 lease: the frontend owns the ring; the bounded-path
     * wait/publish machinery below must not run. */
    R200PPCLeaseResume(control);
    if (FrontendPlatform.leaseActive && !FrontendPlatform.cpFailed)
        return SubmitLeaseBatch(context, slot);
    if (!control) {
        failure = "noctrl";
        goto failed;
    }
    if (!FrontendPlatform.cpAddress ||
        FrontendPlatform.cpBytes < MGLPPC_CP_SEGMENT_BYTES) {
        failure = "noctrl_addr";
        goto failed;
    }
    head = control->head;
    stallReportNext = PerfClock() + PlatformClockRate / 5UL;
    waitStart = PerfClock();
    /* Completion releases CPU metadata, retirement releases GPU-read slices. */
    for (;;) {
        if (!(spin & 255UL)) {
            MGLPPCInvalidateLine32((APTR)&control->head);
            MGLPPCInvalidateLine32((APTR)&control->retired);
            MGLPPCInvalidateLine32((APTR)&control->completed);
            /* Progress-aware yield budget (same policy as the validated
             * drain loop): as long as GPU retirement keeps moving, keep
             * spinning -- every yield parks a full ~20 ms tick on this
             * kernel (PPC_YIELD avg=max=20.0 ms, CP_SURF session). Only a
             * full budget with NO retirement progress earns a park. */
            if (spin && control->retired != cpRetiredSeen) {
                cpRetiredSeen = control->retired;
                spin = 0;
            } else if (!spin) {
                cpRetiredSeen = control->retired;
            }
        }
        if (control->error) {
            failure = "host_error";
            goto failed;
        }
        if (control->epoch != FrontendPlatform.epoch) {
            failure = "epoch";
            goto failed;
        }
        if (head - control->retired < MGLPPC_RING_SLOTS)
            break;
        if (PerfClock() - start >= PlatformClockRate * 5UL || ++spin > 5000000UL) {
            failure = "timeout";
            goto failed;
        }
        if (!(++waitSamples & 255UL)) {
            /* Genuinely stalling here (the break/timeout checks ran
             * first): sample the blocking state and attribute the time
             * since the previous sample to it. */
            ULONG now = PerfClock();
            ULONG backlog = head - control->completed;
            ULONG pending = control->completed - control->retired;
            ULONG cls = backlog >= MGLPPC_RING_SLOTS ? 1UL :
                        pending ? 2UL : 3UL;
            ULONG delta = now - waitSampleTime;

            if (waitClass) {
                if (waitClass == 1UL)
                    CpWaitHostTicks += delta;
                else if (waitClass == 2UL)
                    CpWaitFenceTicks += delta;
                else
                    CpWaitOtherTicks += delta;
            }
            waitClass = cls;
            waitSampleTime = now;
            if (backlog > CpWaitMaxBacklog)
                CpWaitMaxBacklog = backlog;
            if (pending > CpWaitMaxPending)
                CpWaitMaxPending = pending;
            if (stallReports < 40UL && stallReportNext &&
                now >= stallReportNext) {
                ++stallReports;
                stallReportNext = now + PlatformClockRate / 5UL;
                CpDbgPrintf("PUBSTALL spin=%lu head=%lu completed=%lu "
                            "retired=%lu err=%lu epoch=%lu/%lu "
                            "backlog=%lu pending=%lu\n",
                            (unsigned long)spin,
                            (unsigned long)head,
                            (unsigned long)control->completed,
                            (unsigned long)control->retired,
                            (unsigned long)control->error,
                            (unsigned long)control->epoch,
                            (unsigned long)FrontendPlatform.epoch,
                            (unsigned long)backlog,
                            (unsigned long)pending);
            }
        }
        if (!(spin & (MGLPPC_RING_YIELD_SPINS - 1UL)))
            MGLPPCYield();
    }
    CpPhaseWaitTicks += PerfClock() - waitStart;
    ringSlot = head % MGLPPC_RING_SLOTS;
    staging = FrontendPlatform.ringSlots[ringSlot];
    if (!staging || control->slotAddrs[ringSlot] != (ULONG)staging) {
        failure = "slotaddr";
        goto failed;
    }
    memset(&vertices, 0, sizeof(vertices));
    if (slot->commitMode && slot->drawCount) {
        for (index = 0; index < context->segmentPoolCount; ++index)
            if (context->segmentPool[index].Id == context->segment.info.Id) {
                segmentIx = index;
                break;
            }
        if (segmentIx == 0xffffffffUL) {
            failure = "nosegment";
            goto failed;
        }
        vertices.gpuAddress = context->segment.info.GpuAddress;
        vertices.bytes = context->segment.info.Bytes;
        vertices.drawCount = slot->drawCount;
        vertices.stateBatch = slot->stateBatch;
        vertices.offsets = slot->stateBatch ? (const ULONG *)slot->stateBatchDraws :
                                            slot->vertexOffsets;
    }
    failure = "emit_or_surface";
    /* v6: the host may have rotated the fullscreen drawing buffer since the
     * last batch; make the record targets match before the emitter bakes
     * address/pitch/format into the CP stream. */
    emitStart = PerfClock();
    resolveBefore = CpPhaseResolveTicks;
    RefreshCpColorTarget(control, slot->records, slot->recordCount);
    count = MGLPPCEmitBatch(staging, slot->records, slot->recordCount,
                            vertices.drawCount ? &vertices : NULL,
                            ResolveCpSurface, context);
    /* Emit excludes the nested descriptor-resolve time measured by the
     * wrapper, so the two buckets are additive. */
    emitElapsed = PerfClock() - emitStart;
    resolveDelta = CpPhaseResolveTicks - resolveBefore;
    CpPhaseEmitTicks += emitElapsed >= resolveDelta ? emitElapsed - resolveDelta : 0;
    if (!count || count > MGLPPC_CP_DWORDS) {
        failure = "emit";
        goto failed;
    }
    /* Classic Radeon CP dispatch requires an even dword count (Linux
     * radeon_cp_dispatch_indirect); padding is part of the endian conversion. */
    if (count & 1UL)
        staging[count++] = 0x80000000UL;
    if (vertices.drawCount)
        MGLPPCFlushDataRange((APTR)((ULONG)context->segment.info.CpuAddress +
            context->buildSlot * SEGMENT_HALF_BYTES), slot->segmentCursor);
    cpBytes = (count * sizeof(ULONG) + 15UL) & ~15UL;
    if (!CpRingReserve(control, head, cpBytes, &cpOffset)) {
        failure = control->error ? "host_error" : "cpspace";
        goto failed;
    }
    destination = (volatile ULONG *)((UBYTE *)FrontendPlatform.cpAddress +
                                     cpOffset);
    copyStart = PerfClock();
    for (index = 0; index < count; ++index)
        destination[index] = __builtin_bswap32(staging[index]);
    MGLPPCFlushDataRange((APTR)destination, count * sizeof(ULONG));
    (void)destination[count - 1UL];
    FrontendPlatform.cpEntrySerial[ringSlot] = head;
    FrontendPlatform.cpEntryStart[ringSlot] = cpOffset;
    FrontendPlatform.cpEntryBytes[ringSlot] = cpBytes;
    FrontendPlatform.cpRingCursor = cpOffset + cpBytes;
    staging[0] = cpOffset;
    staging[1] = count;
    staging[2] = vertices.drawCount ? 1UL << segmentIx : 0;
    MGLPPCFlushDataRange(staging, 3UL * sizeof(ULONG));
    CpPhaseCopyTicks += PerfClock() - copyStart;
    publishStart = PerfClock();
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
    CpPhasePublishTicks += PerfClock() - publishStart;
    ++CpPhasePublishes;
    if (vertices.drawCount)
        FrontendPlatform.segmentLastEntry[segmentIx] = FrontendPlatform.ringPostSerial;
    return TRUE;
failed:
    FrontendPlatform.cpFailed = TRUE;
    MGLPPCIBTrace("DH2:cpfail.log", failure);
    R200PPCConsolePrintf("CP publish failed: %s\n", failure);
    CpDbgPrintf("PUBFAIL %s head=%lu completed=%lu retired=%lu err=%lu "
                "epoch=%lu/%lu spin=%lu cpen=%lu count=%lu\n",
                failure,
                control ? (unsigned long)control->head : 0UL,
                control ? (unsigned long)control->completed : 0UL,
                control ? (unsigned long)control->retired : 0UL,
                control ? (unsigned long)control->error : 0UL,
                control ? (unsigned long)control->epoch : 0UL,
                (unsigned long)FrontendPlatform.epoch,
                (unsigned long)spin,
                (unsigned long)FrontendPlatform.cpEnabled,
                (unsigned long)count);
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
        slotIndex >= SUBMIT_SLOT_COUNT || FrontendPlatform.pending[slotIndex]) {
#if defined(MGLPPC_ERROR_ORIGIN)
        R200PPCErrorAt("publish guard", slotIndex, GL_INVALID_OPERATION);
#endif
        return FALSE;
    }
    slot = &context->submitSlots[slotIndex];
    if (!slot->recordCount || slot->recordCount > MGLPPC_SEMANTIC_SLOT_DWORDS) {
#if defined(MGLPPC_ERROR_ORIGIN)
        R200PPCErrorAt("publish record count", slot->recordCount, GL_INVALID_OPERATION);
#endif
        return FALSE;
    }
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
#if defined(MGLPPC_ERROR_ORIGIN)
                R200PPCErrorAt("publish missing segment", context->segment.info.Id,
                              GL_INVALID_OPERATION);
#endif
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
#if defined(MGLPPC_ERROR_ORIGIN)
                R200PPCErrorAt("publish ring stall", control->head, control->error);
                R200PPCErrorAt("publish ring completed", control->completed, spin);
#endif
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
        /* Validate the complete wire packet BEFORE copying any dwords.
         * A commit adds a two-word prefix and a draw-descriptor tail. */
        if (!R200SlotPayloadFits(MGLPPC_SEMANTIC_SLOT_DWORDS, slot->recordCount,
                                 draws, slot->stateBatch, TRUE) ||
            !R200SlotPayloadFits(control->slotDwords, slot->recordCount,
                                 draws, slot->stateBatch, TRUE)) {
#if defined(MGLPPC_ERROR_ORIGIN)
            R200PPCErrorAt("ring packet overflow records", slot->recordCount, draws);
#endif
            return FALSE;
        }
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
    if (!MGLPPCTransportSend(&FrontendPlatform.transport, message)) {
#if defined(MGLPPC_ERROR_ORIGIN)
        R200PPCErrorAt("execute send failed", slotIndex, slot->recordCount);
#endif
        return FALSE;
    }
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
#if defined(MGLPPC_ERROR_ORIGIN)
    if (!ok || message->status != MGLPPC_STATUS_OK)
        R200PPCErrorAt("execute reply status", slotIndex, message->status);
#endif
    return ok && message->status == MGLPPC_STATUS_OK;
}

/* W-80: publish a zero-payload drain request. The host closes any open
 * fence-coalescing run (interface-19 Radeon3DSubmitFence) and drains, so a
 * retirement spin that follows can complete even when the newest dispatch
 * was published without a fence. Only published when CP emit is active and
 * the host advertised MGLPPC_CAP_FENCE_DRAIN; otherwise retirement already
 * advances on its own and an unknown flag must not reach an older host. */
static BOOL PublishDrainEntry(volatile MGLPPCRingControlV1 *control)
{
    ULONG spin = 0;

    if (!FrontendPlatform.cpEnabled ||
        !(FrontendPlatform.capabilities & MGLPPC_CAP_FENCE_DRAIN))
        return TRUE;
    while ((control->head - control->completed) >= control->slotCount) {
        if (!(spin & 255UL)) {
            MGLPPCInvalidateLine32((APTR)&control->head);
            MGLPPCInvalidateLine32((APTR)&control->completed);
            if (!control->error &&
                (control->head - control->completed) < control->slotCount)
                break;
        }
        if (control->error || ++spin > 5000000UL) {
            R200PPCConsolePrintf("RING drain_publish_fail head=%lu "
                   "completed=%lu err=%lu\n",
                   (unsigned long)control->head,
                   (unsigned long)control->completed,
                   (unsigned long)control->error);
            return FALSE;
        }
    }
    {
        ULONG ringSlot = control->head % control->slotCount;
        volatile MGLPPCRingEntryV1 *entry = &control->entries[ringSlot];

        entry->slot = ringSlot;
        entry->dwords = 0;
        entry->generation = ++FrontendPlatform.ringPostSerial;
        if (!entry->generation)
            entry->generation = ++FrontendPlatform.ringPostSerial;
        entry->flags = MGLPPC_SEMANTIC_FLAG_DRAIN;
        entry->crc = 0;
        MGLPPCFlushDataRange((APTR)entry, sizeof(*entry));
        control->head = control->head + 1;
        MGLPPCFlushDataRange((APTR)&control->head, sizeof(control->head));
    }
    return TRUE;
}

BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *context, BOOL present)
{
#if MGLPPC_PROFILE
    ULONG profileStart, stallStart, drainStart;
    ULONG stallSleeps = 0, stallEntered = 0;
#endif
    ULONG spin;
    ULONG drainSeen = 0;
    ULONG cpCompletedSeen = 0;
    ULONG drainStallReports = 0;
    ULONG drainReportNext = 0;
    volatile MGLPPCRingControlV1 *control;
    BOOL result = TRUE;
    /* A deferred direct tile must be installed before this frame's work is
     * presented or drained. */
    if (!R200PPCPendingUploadFlush(context))
        return FALSE;
    if (present && context)
        context->mismatchSubmits = 0;
    if (present && FrontendPlatform.leaseNoPresent &&
        FrontendPlatform.leaseActive)
        return TRUE;
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

            /* C1: presentation stays on the 68k (display switch, P96 blit,
             * buffer rotation). Hand the engine back first: every lease
             * batch already retired through its WAIT_IDLE fence, and
             * LEASE_END travels the mailbox, which the host polls before
             * the ring entry published below. */
            if (FrontendPlatform.leaseActive) {
                ULONG t0 = PerfClock();

                (void)MGLPPCLeaseEnd();
                LeaseStatEndTicks += PerfClock() - t0;
                FrontendPlatform.leaseResume = TRUE;
            }
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
                    /* Progress-aware budget: consumption moving means keep
                     * spinning (each yield parks ~20 ms on this kernel). */
                    if (spin && control->completed != cpCompletedSeen) {
                        cpCompletedSeen = control->completed;
                        spin = 0;
                    } else if (!spin) {
                        cpCompletedSeen = control->completed;
                    }
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
                /* The present selects blit vs flip from the published
                 * sync state. PublishCpSlot carries it on every bounded
                 * entry, but a frame rendered entirely under the lease
                 * posts none, which left the host on the library's
                 * sync-on default: a vblank flip per frame (~22 ms). */
                control->syncEnabled = FrontendPlatform.syncEnabled;
                control->syncSeq = FrontendPlatform.syncSeq;
                MGLPPCFlushDataRange((APTR)&control->syncEnabled,
                                     2UL * sizeof(ULONG));
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
            /* W-80: request a fence close + drain so retirement can reach
             * head even when the newest dispatch was published fence-less. */
            if (!PublishDrainEntry(control))
                result = FALSE;
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
            drainSeen = FrontendPlatform.cpEnabled ? control->retired
                                                   : control->completed;
            drainReportNext = PerfClock() + PlatformClockRate / 5UL;
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
                    /* Reset the yield budget while the host keeps consuming:
                     * a WaitTime park costs ~20 ms while an active drain is
                     * only milliseconds; parking mid-drain is a net loss
                     * (U0: 119 of 120 frames yielded once each even though
                     * the host was consuming). Only a full budget with no
                     * progress earns a yield. */
                    {
                        ULONG now = FrontendPlatform.cpEnabled
                                        ? control->retired
                                        : control->completed;
                        if (now != drainSeen) {
                            drainSeen = now;
                            spin = 0;
                        }
                    }
                    if (drainStallReports < 40UL && drainReportNext &&
                        PerfClock() >= drainReportNext) {
                        ++drainStallReports;
                        drainReportNext = PerfClock() +
                                          PlatformClockRate / 5UL;
                        CpDbgPrintf("DRAINSTALL spin=%lu head=%lu "
                                    "completed=%lu retired=%lu err=%lu\n",
                                    (unsigned long)spin,
                                    (unsigned long)control->head,
                                    (unsigned long)control->completed,
                                    (unsigned long)control->retired,
                                    (unsigned long)control->error);
                    }
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
    /* W-80: the wait below needs a fence covering this segment; the newest
     * dispatch may be fence-less, so ask the host to close the run first. */
    if (!PublishDrainEntry(control)) {
        R200PPCConsolePrintf("RING rotate drain_publish failed\n");
        return FALSE;
    }
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
    /* Vector-state support is machine-global; the probe is idempotent and
     * also applies the MGLPPC_NOALTIVEC override. */
    MGLPPCAltiVecVertexProbe();
    FrontendPlatform.nativeBindingKnown = FALSE;
    /* No context can own a deferred tile before creation completes. */
    FrontendPlatform.pendingDirectActive = FALSE;
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
    {   /* EXPERIMENT (not for commit): MGLPPC_BATCHDW caps the batch budget. */
        UBYTE value[12];
        LONG cap;
        value[0] = 0;
        if (GetVar((CONST_STRPTR)"MGLPPC_BATCHDW", value, (LONG)sizeof(value),
                   GVF_GLOBAL_ONLY) > 0 && StrToLong(value, &cap) > 0 &&
            cap >= 1024L && (ULONG)cap < proofContext.batchDwords)
            proofContext.batchDwords = (ULONG)cap;
    }
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
    FrontendPlatform.cpColorToken = FrontendPlatform.cpColorSeq = 0;
    FrontendPlatform.cpRingCursor = 0;
    memset(FrontendPlatform.cpEntryBytes, 0,
           sizeof(FrontendPlatform.cpEntryBytes));
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
        flag[0] = 0;
        /* Interface-20 lease opt-in: MGLPPC_LEASE=1 asks the frontend to
         * take the PPC engine lease after ring registration. Without it
         * (or without the driver cap) the bounded CP path runs unchanged. */
        FrontendPlatform.leaseRequested =
            GetVar((CONST_STRPTR)"MGLPPC_LEASE", flag, sizeof(flag),
                   GVF_GLOBAL_ONLY) > 0 && flag[0] == '1';
        flag[0] = 0;
        FrontendPlatform.leaseNoPresent =
            GetVar((CONST_STRPTR)"MGLPPC_LEASE_NOPRESENT", flag, sizeof(flag),
                   GVF_GLOBAL_ONLY) > 0 && flag[0] == '1';
        flag[0] = 0;
        FrontendPlatform.leaseNoIdle =
            GetVar((CONST_STRPTR)"MGLPPC_LEASE_NOIDLE", flag, sizeof(flag),
                   GVF_GLOBAL_ONLY) > 0 && flag[0] >= '1' && flag[0] <= '2'
                ? (ULONG)(flag[0] - '0') : 0UL;
    }
    /* The host verifies the first Execute of EACH registration, not DLL open. */
    FrontendPlatform.crcArmed = TRUE;
    FrontendPlatform.hostEventsConfirmed = FALSE;
    FrontendPlatform.leaseAvailable = FALSE;
    FrontendPlatform.leaseActive = FALSE;
    FrontendPlatform.leaseResume = FALSE;
    FrontendPlatform.leaseLastFence = 0;
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
                /* Reply-only lease flag: the live driver session advertises
                 * RADEON3D_CAP_PPC_RING, so LEASE_BEGIN may be negotiated. */
                FrontendPlatform.leaseAvailable =
                    (registerMessage->value[3] & MGLPPC_RING_REGISTER_PPC_RING) != 0;
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
    /* Interface-20 lease acquisition (opt-in). The lease flips the CP hot
     * path to local ring submission; the bounded transport path remains
     * the fallback whenever the lease is not live. */
    if (FrontendPlatform.leaseRequested) {
        if (FrontendPlatform.leaseAvailable) {
            if (R200PPCLeaseAcquire()) {
                R200PPCConsolePrintf(
                    "LEASE active wptr=%lu nextfence=%lu\n",
                    (unsigned long)FrontendPlatform.leaseWptr,
                    (unsigned long)FrontendPlatform.leaseNextFence);
            } else {
                R200PPCConsolePrintf("LEASE begin refused; bounded path\n");
            }
        } else {
            R200PPCConsolePrintf(
                "LEASE cap not advertised by host; bounded path\n");
        }
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

/* Existing aggregate diagnostics also belong after the final destroy, not
 * ahead of a drain that can fail and leave the context alive. */
static void FrontendContextProfileDump(void)
{
#if MGLPPC_PROFILE
    BatchDiagDump();
#endif
    if (FrontendPlatform.cpEnabled &&
        (CpSurfMissQueries || CpSurfInvalidate || CpSurfCapResets ||
         CpSurfEpochStale || CpSurfDropped)) {
        printf("CP_SURF hit=%lu miss=%lu reset=%lu inval=%lu stale=%lu "
               "drop=%lu\n",
               (unsigned long)CpSurfHits,
               (unsigned long)CpSurfMissQueries,
               (unsigned long)CpSurfCapResets,
               (unsigned long)CpSurfInvalidate,
               (unsigned long)CpSurfEpochStale,
               (unsigned long)CpSurfDropped);
        fflush(stdout);
#if MGLPPC_PROFILE
        {
            FILE *cpSurfLog = fopen("DH2:mglprof/cp_surf.log", "a");
            if (cpSurfLog) {
                fprintf(cpSurfLog,
                        "CP_SURF hit=%lu miss=%lu reset=%lu inval=%lu "
                        "stale=%lu drop=%lu\n",
                        (unsigned long)CpSurfHits,
                        (unsigned long)CpSurfMissQueries,
                        (unsigned long)CpSurfCapResets,
                        (unsigned long)CpSurfInvalidate,
                        (unsigned long)CpSurfEpochStale,
                        (unsigned long)CpSurfDropped);
                fclose(cpSurfLog);
            }
        }
#endif
    }
    /* Raw mftb ticks, whole-context exclusive CP publish buckets (resolve
     * subtracted from emit); the legacy calibration is not a conversion.
     * Session-4 reporter fix: the previous printf/fprintf had stray format
     * strings after the call (parsed as comma expressions, harmless) and
     * the file variant carried nine conversions for six values (undefined
     * vararg reads: the file's commit/records/draws/rot/tex/clear/replay/
     * present fields were garbage). Both now print exactly the six
     * maintained phase counters, plus CP_WAIT. */
    if (FrontendPlatform.cpEnabled && CpPhasePublishes) {
        printf("CP_PHASE publishes=%lu "
               "wait=%lu resolve=%lu emit=%lu copy=%lu publish=%lu\n",
               (unsigned long)CpPhasePublishes,
               (unsigned long)CpPhaseWaitTicks,
               (unsigned long)CpPhaseResolveTicks,
               (unsigned long)CpPhaseEmitTicks,
               (unsigned long)CpPhaseCopyTicks,
               (unsigned long)CpPhasePublishTicks);
        printf("CP_WAIT host=%lu fence=%lu other=%lu "
               "maxhost=%lu maxpend=%lu\n",
               (unsigned long)CpWaitHostTicks,
               (unsigned long)CpWaitFenceTicks,
               (unsigned long)CpWaitOtherTicks,
               (unsigned long)CpWaitMaxBacklog,
               (unsigned long)CpWaitMaxPending);
        fflush(stdout);
#if MGLPPC_PROFILE
        {
            FILE *cpPhaseLog = fopen("DH2:mglprof/cp_phase.log", "a");
            if (cpPhaseLog) {
                fprintf(cpPhaseLog,
                        "CP_PHASE publishes=%lu wait=%lu resolve=%lu emit=%lu copy=%lu "
                        "publish=%lu\n",
                        (unsigned long)CpPhasePublishes,
                        (unsigned long)CpPhaseWaitTicks,
                        (unsigned long)CpPhaseResolveTicks,
                        (unsigned long)CpPhaseEmitTicks,
                        (unsigned long)CpPhaseCopyTicks,
                        (unsigned long)CpPhasePublishTicks);
                fprintf(cpPhaseLog,
                        "CP_WAIT host=%lu fence=%lu other=%lu "
                        "maxhost=%lu maxpend=%lu\n",
                        (unsigned long)CpWaitHostTicks,
                        (unsigned long)CpWaitFenceTicks,
                        (unsigned long)CpWaitOtherTicks,
                        (unsigned long)CpWaitMaxBacklog,
                        (unsigned long)CpWaitMaxPending);
                fclose(cpPhaseLog);
            }
        }
#endif
    }
}

#if MGLPPC_PROFILE
/* Profile builds: whole-run frontend attribution from the first present,
 * written once at teardown (DH2:mglprof/frontend.log). Wall minus the
 * DLL buckets is time in the client's own code. */
static ULONG FrontendFirstPresentTick;

static void FrontendAttributionDump(void)
{
    const struct PerfMetrics *perf;
    FILE *file;

    if (!R200PPCContext || !FrontendFirstPresentTick)
        return;
    perf = &R200PPCContext->perf;
    file = fopen("DH2:mglprof/frontend.log", "w");
    if (!file)
        return;
    fprintf(file, "FE frames=%lu wall=%lu clock_hz=%lu\n",
            (unsigned long)perf->frames,
            (unsigned long)(PerfClock() - FrontendFirstPresentTick),
            (unsigned long)PlatformClockRate);
    fprintf(file, "FE frontend=%lu serialize=%lu execute=%lu wait=%lu\n",
            (unsigned long)perf->frontendTicks,
            (unsigned long)perf->serializeTicks,
            (unsigned long)perf->executeTicks,
            (unsigned long)perf->waitTicks);
    fprintf(file, "FE present=%lu draw=%lu matrix=%lu fetch=%lu\n",
            (unsigned long)perf->presentTicks,
            (unsigned long)perf->drawTicks,
            (unsigned long)perf->matrixTicks,
            (unsigned long)perf->vertexFetchTicks);
    fprintf(file, "FE append=%lu indexscan=%lu stall=%lu drain=%lu\n",
            (unsigned long)perf->appendTicks,
            (unsigned long)perf->indexScanTicks,
            (unsigned long)PlatformProfile.ringStallTicks,
            (unsigned long)PlatformProfile.drainTicks);
    fprintf(file, "FE verts=%lu prims=%lu submits=%lu flushes=%lu "
            "states=%lu\n",
            (unsigned long)perf->inputVertices,
            (unsigned long)perf->primitives,
            (unsigned long)perf->submits,
            (unsigned long)perf->flushCalls,
            (unsigned long)perf->stateCalls);
    fprintf(file, "FE snapshot_builds=%lu snapshot_fast=%lu capture_hits=%lu "
            "deferred_splits=%lu\n",
            (unsigned long)perf->snapshotBuilds,
            (unsigned long)perf->snapshotFastHits,
            (unsigned long)perf->captureCacheHits,
            (unsigned long)perf->deferredSplits);
    {
        extern ULONG R200FlushSplit[8];
        fprintf(file, "FE split rotate=%lu mode=%lu mismatch=%lu sbfull=%lu "
                "records=%lu generated=%lu draws=%lu\n",
                (unsigned long)R200FlushSplit[0], (unsigned long)R200FlushSplit[1],
                (unsigned long)R200FlushSplit[2], (unsigned long)R200FlushSplit[3],
                (unsigned long)R200FlushSplit[4], (unsigned long)R200FlushSplit[5],
                (unsigned long)R200FlushSplit[6]);
    }
    fclose(file);
}
#endif

void R200PPCDispatchDeleteContext(void)
{
    ULONG index;
#if MGLPPC_PROFILE
    FrontendAttributionDump();
#endif
    /* Unacknowledged smoke work, a latched CP failure or a dead ring mean
     * drains, barriers and slot waits can no longer be answered here. The
     * host however still services the mailbox: if it never learns that the
     * context died it holds the context -- and with it a fullscreen screen
     * -- until the next cold boot. Skip only the risky waits below and
     * still deliver the mailbox-only DESTROY_CONTEXT so the host releases
     * everything and can exit when the client closes. */
    BOOL deadPipe = FrontendPlatform.ibSmokeFailed || FrontendPlatform.cpFailed ||
        (FrontendPlatform.cpEnabled && FrontendPlatform.ring &&
         FrontendPlatform.ring->error);
    FrontendPlatform.nativeBindingKnown = FALSE;
    /* A deferred tile cannot outlive its context; the host-side pending
     * direct allocation (if any BEGIN had run) dies with the context. */
    FrontendPlatform.pendingDirectActive = FALSE;
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
               "texWaits=%lu bufferWaits=%lu drainWaits=%lu "
               "dbegTicks=%lu dwriteTicks=%lu dflushTicks=%lu "
               "dcomTicks=%lu dshadowTicks=%lu\n",
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
               (unsigned long)perf->drainWaits,
               (unsigned long)PlatformProfile.textureDirectBeginTicks,
               (unsigned long)PlatformProfile.textureDirectWriteTicks,
               (unsigned long)PlatformProfile.textureDirectFlushTicks,
               (unsigned long)PlatformProfile.textureDirectCommitTicks,
               (unsigned long)PlatformProfile.textureDirectShadowTicks);
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
    if (!deadPipe && !proofContext.insideBegin && !FrontendPlatform.mailboxAbandoned) {
        if (!FlushScreenVertices(&proofContext) ||
            !SubmitAndWait(&proofContext)) {
            R200PPCTraceStr("delctx FAIL flush/submit");
            return;
        }
    }
    /* Release the PPC engine lease before the ring infrastructure it
     * bypassed is torn down. LEASE_END drains through the lease fence and
     * re-arms the 68k path; the last submission's sequence rides along. */
    if (FrontendPlatform.leaseActive) {
        (void)MGLPPCLeaseEnd();
        R200PPCTraceUL("delctx lease end", FrontendPlatform.leaseLastFence);
    }
    if (LeaseStatBatches) {
        char line[192];
        ULONG us = PlatformClockRate / 1000000UL;

        if (!us)
            us = 1;
        sprintf(line, "LEASESTAT batches=%lu handovers=%lu parks=%lu "
                "fence_us=%lu end_us=%lu begin_us=%lu resume_us=%lu "
                "mbox=%lu/%lu caps=%08lx\n",
                (unsigned long)LeaseStatBatches,
                (unsigned long)LeaseStatHandovers,
                (unsigned long)LeaseStatParks,
                (unsigned long)(LeaseStatFenceTicks / us),
                (unsigned long)(LeaseStatEndTicks / us),
                (unsigned long)(LeaseStatBeginTicks / us),
                (unsigned long)(LeaseStatResumeTicks / us),
                (unsigned long)(FrontendPlatform.mailbox != NULL),
                (unsigned long)FrontendPlatform.mailboxAbandoned,
                (unsigned long)FrontendPlatform.capabilities);
        MGLPPCIBTrace("DH2:leasestat.log", line);
    }
    if (!deadPipe && FrontendPlatform.registered &&
        !R200PPCPlatformBarrier(&proofContext, FALSE)) {
        R200PPCTraceStr("delctx FAIL barrier");
        return;
    }
    if (!deadPipe) {
        for (index = 0; index < SUBMIT_SLOT_COUNT; ++index)
            if (!R200PPCPlatformWaitSlot(&proofContext, index)) {
                R200PPCTraceUL("delctx FAIL waitslot", index);
                return;
            }
    }
    if (FrontendPlatform.registered && !deadPipe) {
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
        if (!deadPipe) {
            R200PPCTraceStr("delctx FAIL destroy");
            return;
        }
        /* A forced release answers GL_ERROR after releasing as much as it
         * could. Keep going so the client-side state stays consistent and
         * the platform close can still send SHUTDOWN. */
        R200PPCTraceStr("delctx deadring destroy best-effort");
    }
    if (deadPipe) {
        /* Mirror the frontend-local unregister state without host traffic;
         * the forced host-side DESTROY above released the registration for
         * real. */
        FrontendPlatform.mailbox = NULL;
        FrontendPlatform.ring = NULL;
    }
    for (index = 0; index < MGLPPC_RING_SLOTS; ++index) {
        if (FrontendPlatform.ringSlots[index]) {
            FreeVec(FrontendPlatform.ringSlotAllocations[index]);
            FrontendPlatform.ringSlotAllocations[index] = NULL;
            FrontendPlatform.ringSlots[index] = NULL;
        }
    }
    FrontendContextProfileDump();
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
            SharedUploadHistoDump(report);
            {
                ULONG i, offset = 0, count;

                for (i = 0; i < 24UL; ++i) {
                    count = R200SubmitCallerGet(i, &offset);
                    if (count)
                        fprintf(report, "SUBMIT_CALLER off=%08lx n=%lu\n",
                                (unsigned long)offset, (unsigned long)count);
                }
                fprintf(report, "SUBMIT_CALLER overflow=%lu\n",
                        (unsigned long)R200SubmitCallerGet(24UL, &offset));
            }
            fprintf(report, "PPC_TEXTURE calls=%lu successes=%lu bytes=%lu "
                    "ticks=%lu direct_calls=%lu direct_ticks=%lu "
                    "shared_calls=%lu chunked_calls=%lu barrier_calls=%lu "
                    "barrier_ticks=%lu direct_begin_ticks=%lu "
                    "direct_write_ticks=%lu direct_flush_ticks=%lu "
                    "direct_commit_ticks=%lu direct_shadow_ticks=%lu\n",
                    (unsigned long)profile->textureCalls,
                    (unsigned long)profile->textureSuccesses,
                    (unsigned long)profile->textureBytes,
                    (unsigned long)profile->textureTicks,
                    (unsigned long)profile->textureDirectCalls,
                    (unsigned long)profile->textureDirectTicks,
                    (unsigned long)profile->textureSharedCalls,
                    (unsigned long)profile->textureChunkedCalls,
                    (unsigned long)profile->textureBarrierCalls,
                    (unsigned long)profile->textureBarrierTicks,
                    (unsigned long)profile->textureDirectBeginTicks,
                    (unsigned long)profile->textureDirectWriteTicks,
                    (unsigned long)profile->textureDirectFlushTicks,
                    (unsigned long)profile->textureDirectCommitTicks,
                    (unsigned long)profile->textureDirectShadowTicks);
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
#if MGLPPC_PROFILE
    PpcLmDump();
#endif
    R200PPCTraceStr("platclose done");
}

#if MGLPPC_PROFILE && R200_PERF_AUTOREPORT
static ULONG R200PPCOutsideTicks;
#endif

/* Defined later in this TU; used by the MGLPPC_SHOT_EVERY present
 * diagnostic in R200PPCDispatchSwitchDisplay. */
void R200PPCDispatchWriteShotPPM(char *filename);

void R200PPCDispatchSwitchDisplay(void)
{
    /* MGLPPC_SHOT_EVERY diagnostic (default OFF, benchmark-safe): dump
     * the surface this present is about to blit to a PPM, so
     * render-vs-present divergence can be observed from inside a running
     * client (Quake3 black-window investigation, 2026-10-01).
     * SAFETY (first attempt wedged a real-hardware Q3 PPC task into an
     * exception): value 1 dumps ONLY the first present; N>1 dumps every
     * Nth present but at most one per 15 s wall clock, and all dumps go
     * to T: (never DH2:, which is slow under SFS). Dumps never run twice
     * concurrently. */
    static ULONG presentCount;
    static ULONG shotEvery;
    static ULONG lastShotTick;
    static int shotEnvRead;
    static int shotInProgress;

    if (!shotEnvRead) {
        TEXT buf[16];
        shotEnvRead = 1;
        buf[0] = 0;
        GetVar((CONST_STRPTR)"MGLPPC_SHOT_EVERY", buf, (LONG)sizeof(buf),
               GVF_GLOBAL_ONLY);
        shotEvery = 0;
        if (buf[0]) {
            const TEXT *p = buf;
            while (*p >= '0' && *p <= '9')
                shotEvery = shotEvery * 10UL + (ULONG)(*p++ - '0');
        }
        presentCount = 0;
    }
    if (shotEvery && !shotInProgress && ++presentCount % shotEvery == 0) {
        char shotName[80];
        /* 1 = first present only; N>1 throttled to one dump per 15 s */
        int take = (shotEvery == 1UL)
            ? (presentCount == 1UL)
            : PerfClockDeltaMs(lastShotTick) >= 15000UL;
        if (take) {
            lastShotTick = PerfClock();
            shotInProgress = 1;
            sprintf(shotName, "T:present_%lu.ppm",
                    (unsigned long)presentCount);
            R200PPCDispatchWriteShotPPM(shotName);
            shotInProgress = 0;
        }
    }
#if MGLPPC_PROFILE && R200_PERF_AUTOREPORT
    static ULONG lastLiveWall, lastLivePrintEnd;
    static ULONG lastFe, lastSer, lastExec, lastWait, lastPres;
    static ULONG lastDraw, lastStall, lastDrain, lastVerts, lastPrims;
    static ULONG lastSubmits, lastPublish;
#endif
#if MGLPPC_PROFILE
    if (!FrontendFirstPresentTick)
        FrontendFirstPresentTick = PerfClock();
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
    GLenum error = GL_INVALID_OPERATION;
    /* Forward a host-provided GL error only when it actually is one. The
     * mailbox message is reused in place, so a handler that fails without
     * writing value[0] leaves the REQUEST's value[0] in the reply (a token,
     * a count, a serial, an address). Classic MiniGL error codes are the
     * contiguous range GL_INVALID_ENUM (88) .. GL_OUT_OF_MEMORY (123);
     * anything outside that range cannot be a real error and would surface
     * as a bogus client-visible code. */
    if (FrontendPlatform.control.status == MGLPPC_STATUS_GL_ERROR) {
        GLenum reported = (GLenum)FrontendPlatform.control.value[0];
        if (reported >= GL_INVALID_ENUM && reported <= GL_OUT_OF_MEMORY)
            error = reported;
    }
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
    if (context->boundTexture[context->activeTexture] == texture &&
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
     * both reads and overwrites that shared buffer.
     * Quake uploads lightmaps while unit 1 is active (gl_rsurf.c:2160),
     * so binds must be allowed on either unit. Only unit 0's binding is
     * shadowed to the host (semanticBoundTexture); unit 1's binding is
     * local to the draw records. */
    if (context->activeTexture == 0 &&
        FrontendPlatform.hostShadowTexture != texture)
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

static void FrontendMirrorTextureValues(struct MiniGLR200Context *context,
                                        ULONG textureName, ULONG surfaceToken,
                                        ULONG width, ULONG height,
                                        ULONG definedLevels, ULONG levelCount,
                                        ULONG contentSerial, ULONG deviceEpoch)
{
    struct TextureObject *texture;
    ULONG generation;

    texture = &context->textures[textureName];
    /* The texture's previous surface token stops being valid here; drop any
     * cached descriptor so a later reallocation of the same handle address
     * cannot resolve to it (plan 2026-09-20 §7 P0/W-01). */
    FrontendDropCachedSurface((ULONG)texture->surface.Handle);
    generation = texture->imageGeneration + 1UL;
    if (!generation) generation = 1;
    texture->generated = TRUE;
    texture->resident = TRUE;
    texture->surface.Handle = (APTR)surfaceToken;
    texture->width = width;
    texture->height = height;
    texture->definedLevels = definedLevels;
    texture->levelCount = levelCount;
    texture->contentSerial = contentSerial;
    texture->residentDeviceEpoch = deviceEpoch;
    texture->imageGeneration = generation;
    texture->residentGeneration = generation;
    texture->dirtyLevelMask = 0;
    BumpSemanticStateSerial(context);
}

static void FrontendMirrorTextureReply(struct MiniGLR200Context *context)
{
    FrontendMirrorTextureValues(context,
        context->boundTexture[context->activeTexture],
        FrontendPlatform.control.value[1],
        FrontendPlatform.control.value[2],
        FrontendPlatform.control.value[3],
        FrontendPlatform.control.value[4],
        FrontendPlatform.control.value[5],
        FrontendPlatform.control.value[6],
        FrontendPlatform.control.value[7]);
}

/* Drop the frontend residency mirror of a texture whose native residency
 * may have been replaced by a failed update. Draws using the stale handle
 * must be dropped until a later successful upload restores the mirror. */
static void FrontendInvalidateTextureMirror(struct MiniGLR200Context *context,
                                            ULONG textureName)
{
    struct TextureObject *texture = &context->textures[textureName];

    /* A failed update may have released the native surface without a
     * replacement: drop its cached descriptor before clearing the mirror. */
    FrontendDropCachedSurface((ULONG)texture->surface.Handle);
    texture->resident = FALSE;
    texture->surface.Handle = NULL;
    texture->residentGeneration = 0;
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
        /* Quake's 4444 lightmaps (gl_rsurf.c R_BuildLightMap) store the
         * inverted light in the FIRST nibble of the first byte, which
         * must become alpha for the SRC_ALPHA blend. The R200 hardware
         * format is ARGB4444, so the high nibble is alpha. */
        ULONG value = ((ULONG)src[0] << 8) | src[1];
        a = ((value >> 12) & 15UL) * 17UL;
        r = ((value >> 8) & 15UL) * 17UL;
        g = ((value >> 4) & 15UL) * 17UL;
        b = (value & 15UL) * 17UL;
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

/* 565 -> 8-bit expansion tables, built once on first use. The values are
 * the exact integer expansions FrontendSourceToBGRA32 computes
 * (v*255/31, v*255/63), pre-shifted into the BGRA dword. 512 bytes total,
 * L1-resident; the streaming 565 paths were compute-bound on the two
 * division sequences per texel (U0 attribution). */
static ULONG Frontend565R[32];
static ULONG Frontend565G[64];
static ULONG Frontend565B[32];
static BOOL Frontend565TablesReady;

static void Frontend565TableInit(void)
{
    ULONG i;
    if (Frontend565TablesReady)
        return;
    for (i = 0; i < 32UL; ++i)
        Frontend565R[i] = (i * 255UL / 31UL) << 8;
    for (i = 0; i < 64UL; ++i)
        Frontend565G[i] = (i * 255UL / 63UL) << 16;
    for (i = 0; i < 32UL; ++i)
        Frontend565B[i] = (i * 255UL / 31UL) << 24;
    Frontend565TablesReady = TRUE;
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
    /* Paletted sources (Quake's GL_Upload8_EXT path) expand through the
     * PPC-side palette mirror that Lib_glColorTable maintains. The 68k
     * native palette is never synchronized across CPUs (see protocol
     * notes), so the frontend always canonicalizes indices to BGRA here
     * and the wire never carries raw indices. Entries are stored in BGRA
     * destination order (see ConvertPixel), hence the byte-exact copy. */
    const UBYTE (*palette)[4] = NULL;
    if (format == GL_COLOR_INDEX && type == GL_UNSIGNED_BYTE) {
        struct MiniGLR200Context *cc = CurrentContext();
        if (cc)
            palette = (const UBYTE (*)[4])cc->colorTable;
    }
    if (!palette && type == MGL_UNSIGNED_SHORT_5_6_5) {
        /* 565 sources are the streaming-lightmap/console-video case.
         * Table-driven expansion: the exact dwords the generic helper
         * computes, without the per-texel division sequences. */
        Frontend565TableInit();
        for (row = 0; row < (ULONG)height; ++row) {
            const UBYTE *srcRow = source + row * sourceStride;
            ULONG *dstRow = (ULONG *)(destination + row * (ULONG)width * 4UL);
            for (column = 0; column < (ULONG)width; ++column) {
                ULONG value = ((ULONG)srcRow[column * 2UL] << 8) |
                              srcRow[column * 2UL + 1UL];
                dstRow[column] = Frontend565R[(value >> 11) & 31UL] |
                                 Frontend565G[(value >> 5) & 63UL] |
                                 Frontend565B[value & 31UL] | 0xffUL;
            }
        }
        (void)rowBytes;
        return;
    }
    for (row = 0; row < (ULONG)height; ++row) {
        const UBYTE *srcRow = source + row * sourceStride;
        UBYTE *dstRow = destination + row * (ULONG)width * 4UL;
        for (column = 0; column < (ULONG)width; ++column) {
            ULONG value;
            if (palette) {
                const UBYTE *entry = palette[srcRow[column * sourceBytes]];
                value = ((ULONG)entry[0] << 24) | ((ULONG)entry[1] << 16) |
                        ((ULONG)entry[2] << 8) | (ULONG)entry[3];
            } else {
                value = FrontendSourceToBGRA32(
                    format, type, srcRow + column * sourceBytes);
            }
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
 * cannot exhaust PPC memory: 4 MiB holds every 128x128 lightmap atlas
 * of a full Quake map (35 x 64 KiB) with headroom. */
#define R200_SHADOW_TOTAL_MAX (4UL * 1024UL * 1024UL)

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

/* Convert/expand one source image into the aperture surface, row by row.
 * Kept as a standalone pass so the batched two-tile path can fill both
 * tiles (one of them from its PPC-resident staging) without duplicating
 * the conversion rules. */
static void FrontendDirectWritePixels(UBYTE *destination, ULONG pitch,
                                      ULONG grantedFormat,
                                      const UBYTE *source,
                                      ULONG sourceStride, ULONG width,
                                      ULONG height, GLenum format,
                                      GLenum type, UBYTE *shadowDestination)
{
    ULONG row, column, sourceBytes;

    if (type == MGL_UNSIGNED_SHORT_5_6_5 ||
        type == MGL_UNSIGNED_SHORT_4_4_4_4)
        sourceBytes = 2;
    else if (format == GL_RGB) sourceBytes = 3;
    else if (format == GL_LUMINANCE_ALPHA) sourceBytes = 2;
    else if (format == GL_ALPHA || format == GL_LUMINANCE) sourceBytes = 1;
    else sourceBytes = 4;

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
         * transaction each and dominate the upload. Burst stores
         * (stswi) to this aperture hang the GPU on real hardware, so
         * keep the per-texel store path. */
        if (type == MGL_UNSIGNED_SHORT_5_6_5 &&
            ((ULONG)out & 3UL) == 0UL) {
            /* Specialized 565 -> BGRA32 via the expansion tables (exact
             * dwords; no per-texel division). When the caller supplies a
             * canonical-shadow destination the same converted dword fills
             * both, so the shadow costs one cached store per texel. */
            ULONG *out32 = (ULONG *)out;
            ULONG *shadow32 = shadowDestination
                ? (ULONG *)(shadowDestination + row * width * 4UL) : NULL;
            Frontend565TableInit();
            for (column = 0; column < width; ++column) {
                ULONG value = ((ULONG)srcRow[column * 2UL] << 8) |
                              srcRow[column * 2UL + 1UL];
                value = Frontend565R[(value >> 11) & 31UL] |
                        Frontend565G[(value >> 5) & 63UL] |
                        Frontend565B[value & 31UL] | 0xffUL;
                out32[column] = value;
                if (shadow32)
                    shadow32[column] = value;
            }
            continue;
        }
        if (format == MGL_UBYTE_BGRA && type == GL_UNSIGNED_BYTE &&
            ((ULONG)out & 3UL) == 0UL) {
            /* Canonical BGRA staging -> aperture is a byte-order-preserving
             * dword copy on this big-endian CPU (FrontendSourceToBGRA32
             * returns the source dword unchanged for MGL_UBYTE_BGRA). */
            const ULONG *src32 = (const ULONG *)srcRow;
            ULONG *out32 = (ULONG *)out;
            ULONG *shadow32 = shadowDestination
                ? (ULONG *)(shadowDestination + row * width * 4UL) : NULL;
            for (column = 0; column < width; ++column) {
                ULONG value = src32[column];
                out32[column] = value;
                if (shadow32)
                    shadow32[column] = value;
            }
            continue;
        }
        if (((ULONG)out & 3UL) == 0UL) {
            ULONG *out32 = (ULONG *)out;
            ULONG *shadow32 = shadowDestination
                ? (ULONG *)(shadowDestination + row * width * 4UL) : NULL;
            for (column = 0; column < width; ++column) {
                ULONG value = FrontendSourceToBGRA32(
                    format, type, srcRow + column * sourceBytes);
                out32[column] = value;
                if (shadow32)
                    shadow32[column] = value;
            }
        } else {
            ULONG *shadow32 = shadowDestination
                ? (ULONG *)(shadowDestination + row * width * 4UL) : NULL;
            for (column = 0; column < width; ++column) {
                ULONG value = FrontendSourceToBGRA32(
                    format, type, srcRow + column * sourceBytes);
                out[column * 4UL + 0] = (UBYTE)(value >> 24);
                out[column * 4UL + 1] = (UBYTE)(value >> 16);
                out[column * 4UL + 2] = (UBYTE)(value >> 8);
                out[column * 4UL + 3] = (UBYTE)value;
                if (shadow32)
                    shadow32[column] = value;
            }
        }
    }
}

/* MGLPPC_TEXDIRECT_REQ_PARK_OLD for the next direct BEGIN; set only around
 * the subimage re-commit. */
static ULONG FrontendDirectRequestFlags;

/* Park-mode subimage ordering: the host must have consumed (dispatched)
 * every published entry before BEGIN, so a replaced residency is only
 * named by work the host already queued - which its reap waits out - and a
 * VRAM-pressure eviction inside BEGIN cannot free a surface an unconsumed
 * entry still names. Unlike R200PPCPlatformBarrier(FALSE) this does not
 * wait for the GPU to retire anything. */
static BOOL FrontendWaitRingConsumed(void)
{
    volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
    ULONG spin = 0;
    ULONG seen;

    if (!control)
        return TRUE;
    seen = control->completed;
    while (control->completed != control->head) {
        if (!(spin & 255UL)) {
            MGLPPCInvalidateLine32((APTR)&control->completed);
            if (control->completed != seen) {
                seen = control->completed;
                spin = 0;
            }
        }
        if (spin && !(spin & (MGLPPC_RING_YIELD_SPINS - 1UL)))
            MGLPPCYield();
        if (control->error || ++spin > 5000000UL)
            return FALSE;
    }
    return TRUE;
}

static BOOL FrontendDirectTextureUploadNamed(struct MiniGLR200Context *context,
                                             ULONG textureName, ULONG width,
                                             ULONG height, GLenum format,
                                             GLenum type,
                                             const UBYTE *source,
                                             ULONG sourceStride,
                                             UBYTE *shadowDestination)
{
    /* R5G6B5PC texture surfaces are rejected by the service's import
     * path (verified on hardware): always request BGRA32. */
    ULONG requestFormat = MGLPPC_TEXDIRECT_FMT_BGRA32;
    ULONG grantedFormat;
    ULONG pitch, bytes;
    UBYTE *destination;
#if MGLPPC_PROFILE
    /* U0 direct-upload latency split: begin / convert+write / flush / commit.
     * Enclosed by textureDirectTicks at the call sites. */
    ULONG phaseStart;
#endif

#if MGLPPC_PROFILE
    ULONG diagStart = PerfClock();
#endif
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = textureName;
    FrontendPlatform.control.value[1] = width;
    FrontendPlatform.control.value[2] = height;
    FrontendPlatform.control.value[3] = requestFormat |
                                        FrontendDirectRequestFlags;
#if MGLPPC_PROFILE
    phaseStart = PerfClock();
#endif
    if (!PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_BEGIN,
                      FrontendPlatform.contextId))
        return FALSE;
#if MGLPPC_PROFILE
    SingleDiagBeginTicks += PerfClock() - diagStart;
    diagStart = PerfClock();
#endif
#if MGLPPC_PROFILE
    PlatformProfile.textureDirectBeginTicks += PerfClock() - phaseStart;
    phaseStart = PerfClock();
#endif
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
    FrontendDirectWritePixels(destination, pitch, grantedFormat, source,
                              sourceStride, width, height, format, type,
                              shadowDestination);
#if MGLPPC_PROFILE
    SingleDiagWriteTicks += PerfClock() - diagStart;
    diagStart = PerfClock();
    PlatformProfile.textureDirectWriteTicks += PerfClock() - phaseStart;
    phaseStart = PerfClock();
#endif
    MGLPPCFlushDataRange((APTR)destination, pitch * height);
#if MGLPPC_PROFILE
    PlatformProfile.textureDirectFlushTicks += PerfClock() - phaseStart;
    phaseStart = PerfClock();
#endif
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = textureName;
    FrontendPlatform.control.value[1] = 0UL;
    if (!PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_COMMIT,
                      FrontendPlatform.contextId))
        return FALSE;
#if MGLPPC_PROFILE
    SingleDiagCommitTicks += PerfClock() - diagStart;
    ++SingleDiagCalls;
    PlatformProfile.textureDirectCommitTicks += PerfClock() - phaseStart;
#endif
    return TRUE;
}

static BOOL FrontendDirectTextureUpload(struct MiniGLR200Context *context,
                                        ULONG width, ULONG height,
                                        GLenum format, GLenum type,
                                        const UBYTE *source,
                                        ULONG sourceStride,
                                        UBYTE *shadowDestination)
{
    return FrontendDirectTextureUploadNamed(context,
        context->boundTexture[context->activeTexture], width, height,
        format, type, source, sourceStride, shadowDestination);
}

/* PPC-owned lightmap surface pools. A texture that keeps receiving
 * subimages (Quake's lightmap atlases, several per frame with dynamic
 * lights) gets R200_POOL_SLOTS library-owned holder surfaces, created once
 * through the direct BEGIN/COMMIT path under reserved texture names. An
 * update then needs no 68k at all: patch the shadow, publish the records
 * that still name the current slot (its last use is the ring head), pick
 * a slot whose last use has retired, copy the rows that changed since that
 * slot was last written, and point the texture's token at it. The R200
 * texture cache never re-reads a rewritten line (p96-driver docs/07), so a
 * slot is rewritten only after its last sampling retired and never while
 * it is the current one; the reuse distance (two other slots in between)
 * is what the free/re-import path already relied on. */
#define R200_POOL_SLOTS 3UL
#define R200_POOL_MAX 64UL
#define R200_POOL_HISTORY 8UL
#define R200_POOL_NONE 0xffffffffUL

struct FrontendLmPoolSlot {
    ULONG token, cpu, lastUse, written;
};

static struct FrontendLmPool {
    ULONG width, height, epoch, serial, current;
    ULONG histSerial[R200_POOL_HISTORY];
    UWORD histTop[R200_POOL_HISTORY], histEnd[R200_POOL_HISTORY];
    struct FrontendLmPoolSlot slot[R200_POOL_SLOTS];
} FrontendLmPools[R200_POOL_MAX];
static ULONG FrontendLmPoolCount;
static ULONG FrontendLmPoolNextName;
/* Per texture name: 0 unseen, 1 one subimage seen, 2+i pool i,
 * 255 not poolable. */
static UBYTE FrontendLmPoolState[MAX_TEXTURES];

static void FrontendLmPoolReset(void)
{
    memset(FrontendLmPoolState, 0, sizeof(FrontendLmPoolState));
    memset(FrontendPoolHash, 0, sizeof(FrontendPoolHash));
    FrontendLmPoolCount = 0;
    FrontendLmPoolNextName = MAX_TEXTURES - 1UL;
}

static struct FrontendLmPool *FrontendLmPoolCreate(
    struct MiniGLR200Context *context, struct TextureObject *texture)
{
    struct FrontendLmPool *pool;
    ULONG index;

    /* Holder names come from the top of the name space; GLQuake-class
     * clients number textures upward from 1. */
    if (FrontendLmPoolCount >= R200_POOL_MAX ||
        FrontendLmPoolNextName < MAX_TEXTURES - R200_POOL_MAX * R200_POOL_SLOTS)
        return NULL;
    pool = &FrontendLmPools[FrontendLmPoolCount];
    memset(pool, 0, sizeof(*pool));
    for (index = 0; index < R200_POOL_SLOTS; ++index) {
        ULONG holder = FrontendLmPoolNextName--;
        struct MGLPPCEmitSurface surface;
        ULONG token;

        if (!FrontendDirectTextureUploadNamed(context, holder, texture->width,
                                              texture->height, MGL_UBYTE_BGRA,
                                              GL_UNSIGNED_BYTE, texture->shadow,
                                              texture->width * 4UL, NULL) ||
            !FrontendPlatform.control.value[1] ||
            FrontendPlatform.control.value[7] != context->deviceEpoch)
            return NULL;
        token = FrontendPlatform.control.value[1];
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[0] = token;
        if (!PlatformCall(MGLPPC_CMD_CP_SURFACE, FrontendPlatform.contextId) ||
            FrontendPlatform.control.value[0] != GL_NO_ERROR ||
            !FrontendPlatform.control.value[1] ||
            FrontendPlatform.control.value[3] != texture->width * 4UL ||
            FrontendPlatform.control.value[4] != texture->width ||
            FrontendPlatform.control.value[5] != texture->height ||
            FrontendPlatform.control.value[6] != RADEON3D_FORMAT_B8G8R8A8 ||
            FrontendPlatform.control.value[7] != context->deviceEpoch)
            return NULL;
        surface.cpuAddress = FrontendPlatform.control.value[1];
        surface.gpuAddress = FrontendPlatform.control.value[2];
        surface.pitch = FrontendPlatform.control.value[3];
        surface.width = FrontendPlatform.control.value[4];
        surface.height = FrontendPlatform.control.value[5];
        surface.format = FrontendPlatform.control.value[6];
        if (!FrontendPoolHashInsert(token, context->deviceEpoch, &surface))
            return NULL;
        pool->slot[index].token = token;
        pool->slot[index].cpu = surface.cpuAddress;
        pool->slot[index].lastUse = 0;
        pool->slot[index].written = 0; /* the full shadow as of serial 0 */
    }
    pool->width = texture->width;
    pool->height = texture->height;
    pool->epoch = context->deviceEpoch;
    pool->current = R200_POOL_NONE;
    ++FrontendLmPoolCount;
    return pool;
}

/* The shadow already holds this update. Returns 1 when the pool served it,
 * 0 when the caller should take the direct re-commit, -1 on failure. */
static int FrontendLmPoolUpdate(struct MiniGLR200Context *context,
                                struct TextureObject *texture, ULONG name,
                                ULONG top, ULONG rows)
{
    volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
    struct FrontendLmPool *pool;
    UBYTE state = FrontendLmPoolState[name];
    ULONG index, pick, serial, first, end, pitch, spin = 0, generation;

    if (state == 255U)
        return 0;
    if (state < 2U) {
        if (!state) {
            FrontendLmPoolState[name] = 1;
            return 0;
        }
        pool = FrontendLmPoolCreate(context, texture);
        if (!pool) {
            FrontendLmPoolState[name] = 255;
            return 0;
        }
        FrontendLmPoolState[name] = (UBYTE)(2UL + (ULONG)(pool - FrontendLmPools));
    } else {
        pool = &FrontendLmPools[state - 2U];
    }
    if (pool->epoch != context->deviceEpoch ||
        pool->width != texture->width || pool->height != texture->height) {
        FrontendLmPoolState[name] = 255;
        return 0;
    }
    /* Records naming the current slot are already built (or are built by
     * this flush) and can only be published in the next ring entry, so
     * that entry is its last use. Publishing here would split the batch on
     * every upload; the pick loop publishes only when it must. */
    if (!FlushScreenVertices(context))
        return -1;
    if (pool->current != R200_POOL_NONE) {
        pool->slot[pool->current].lastUse = control->head + 1UL;
        /* A redefinition (TexImage2D) re-pointed the texture at a native
         * surface: the slots no longer mirror the shadow. */
        if ((ULONG)texture->surface.Handle != pool->slot[pool->current].token)
            for (index = 0; index < R200_POOL_SLOTS; ++index)
                pool->slot[index].written = R200_POOL_NONE;
    }
    serial = ++pool->serial;
    index = serial % R200_POOL_HISTORY;
    pool->histSerial[index] = serial;
    pool->histTop[index] = (UWORD)top;
    pool->histEnd[index] = (UWORD)(top + rows);
    /* Never the current slot; among retired ones the least recently used. */
    for (;;) {
        ULONG retired;

        MGLPPCInvalidateLine32((APTR)&control->retired);
        retired = control->retired;
        pick = R200_POOL_NONE;
        for (index = 0; index < R200_POOL_SLOTS; ++index) {
            if (index == pool->current ||
                (LONG)(retired - pool->slot[index].lastUse) < 0)
                continue;
            if (pick == R200_POOL_NONE ||
                (LONG)(pool->slot[index].lastUse -
                       pool->slot[pick].lastUse) < 0)
                pick = index;
        }
        if (pick != R200_POOL_NONE)
            break;
        /* A slot waiting on the unpublished entry can never retire until
         * that entry is published. */
        for (index = 0; index < R200_POOL_SLOTS; ++index)
            if (index != pool->current &&
                (LONG)(pool->slot[index].lastUse - control->head) > 0)
                break;
        if (index < R200_POOL_SLOTS) {
            if (!Submit(context))
                return -1;
            continue;
        }
        if (spin && !(spin & (MGLPPC_RING_YIELD_SPINS - 1UL)))
            MGLPPCYield();
        if (control->error || ++spin > 5000000UL)
            return -1;
    }
    /* Rows changed since this slot was last written. */
    first = 0;
    end = pool->height;
    if (pool->slot[pick].written != R200_POOL_NONE &&
        serial - pool->slot[pick].written <= R200_POOL_HISTORY) {
        ULONG s;

        first = pool->height;
        end = 0;
        for (s = pool->slot[pick].written + 1UL; s <= serial; ++s) {
            index = s % R200_POOL_HISTORY;
            if (pool->histSerial[index] != s) {
                first = 0;
                end = pool->height;
                break;
            }
            if (pool->histTop[index] < first)
                first = pool->histTop[index];
            if (pool->histEnd[index] > end)
                end = pool->histEnd[index];
        }
    }
    pitch = pool->width * 4UL;
    if (end > first) {
        /* The shadow is the surface's BGRA32 dword image (pitch == width*4
         * was verified at creation): a straight dword copy. */
        volatile ULONG *dst = (volatile ULONG *)(pool->slot[pick].cpu +
                                                 first * pitch);
        const ULONG *src = (const ULONG *)(texture->shadow + first * pitch);
        ULONG count = (end - first) * pool->width;

        while (count--)
            *dst++ = *src++;
        MGLPPCFlushDataRange((APTR)(pool->slot[pick].cpu + first * pitch),
                             (end - first) * pitch);
    }
    pool->slot[pick].written = serial;
    pool->current = pick;
    texture->surface.Handle = (APTR)pool->slot[pick].token;
    texture->resident = TRUE;
    generation = texture->imageGeneration + 1UL;
    if (!generation)
        generation = 1;
    texture->imageGeneration = generation;
    texture->residentGeneration = generation;
    texture->dirtyLevelMask = 0;
    BumpSemanticStateSerial(context);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Batched two-tile direct upload                                      */
/* ------------------------------------------------------------------ */

#if MGLPPC_PROFILE
/* File-based phase attribution for the two-tile batch (MGLPPC_BATCHDIAG=1):
 * accumulated PerfClock ticks per phase, dumped at context teardown.
 * Profile builds only: the quiet DLL must stay free of extra mftb reads. */
static ULONG BatchDiagFrames;
static ULONG BatchDiagPairs;
static ULONG BatchDiagSingles;
static ULONG BatchDiagBarrierTicks;
static ULONG BatchDiagBeginTicks;
static ULONG BatchDiagWriteTicks;
static ULONG BatchDiagCommitTicks;
static ULONG BatchDiagSingleTicks;

static void BatchDiagHex(char **p, ULONG value)
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

/* Append one phase-attribution line; called once at context teardown when
 * MGLPPC_BATCHDIAG=1. Uses DOS writes (no stdio buffering). */
static void BatchDiagDump(void)
{
    UBYTE enabled[8];
    char buf[256];
    char *p = buf;
    BPTR file;

    enabled[0] = 0;
    GetVar((CONST_STRPTR)"MGLPPC_BATCHDIAG", enabled, (LONG)sizeof(enabled),
           GVF_GLOBAL_ONLY);
    if (enabled[0] != '1')
        return;
    file = Open((STRPTR)"DH2:wosbuild/batchdiag.txt", MODE_NEWFILE);
    if (!file)
        return;
    *p++ = 'B'; *p++ = 'A'; *p++ = 'T'; *p++ = 'C'; *p++ = 'H';
    *p++ = 'D'; *p++ = 'I'; *p++ = 'A'; *p++ = 'G';
#define BATCHDIAG_FIELD(label, value)     do { *p++ = ' '; { const char *q = (label);          while (*q) *p++ = *q++; } *p++ = '=';          BatchDiagHex(&p, (value)); } while (0)
    BATCHDIAG_FIELD("pairs", BatchDiagPairs);
    BATCHDIAG_FIELD("singles", BatchDiagSingles);
    BATCHDIAG_FIELD("barrier", BatchDiagBarrierTicks);
    BATCHDIAG_FIELD("begin", BatchDiagBeginTicks);
    BATCHDIAG_FIELD("write", BatchDiagWriteTicks);
    BATCHDIAG_FIELD("commit", BatchDiagCommitTicks);
    BATCHDIAG_FIELD("single", BatchDiagSingleTicks);
    BATCHDIAG_FIELD("scalls", SingleDiagCalls);
    BATCHDIAG_FIELD("sbegin", SingleDiagBeginTicks);
    BATCHDIAG_FIELD("swrite", SingleDiagWriteTicks);
    BATCHDIAG_FIELD("scommit", SingleDiagCommitTicks);
    BATCHDIAG_FIELD("hz", PlatformClockRate);
#undef BATCHDIAG_FIELD
    *p++ = '\n';
    Write(file, buf, (LONG)(p - buf));
    Close(file);
}

#endif /* MGLPPC_PROFILE */

/* Staged-tile outcome codes for FrontendDirectBatchOffer. */
#define R200_BATCH_NONE 0      /* nothing pending; ordinary path applies */
#define R200_BATCH_DEFERRED 1  /* current tile staged, BEGIN deferred */
#define R200_BATCH_DONE 2      /* pending + current uploaded as one pair */
#define R200_BATCH_FALLBACK 3  /* pending consumed; current still to upload */

static void FrontendPendingDirectTake(R200PPCPendingDirectUpload *pending)
{
    pending->textureName = FrontendPlatform.pendingDirectName;
    pending->width = FrontendPlatform.pendingDirectWidth;
    pending->height = FrontendPlatform.pendingDirectHeight;
    pending->shadowName = FrontendPlatform.pendingDirectShadowName;
    pending->epoch = FrontendPlatform.pendingDirectEpoch;
    pending->staging = FrontendPlatform.pendingDirectStaging;
    pending->stagingIsShadow = FrontendPlatform.pendingDirectStagingIsShadow;
    FrontendPlatform.pendingDirectActive = FALSE;
}

static void FrontendDirectAbort(ULONG textureName)
{
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = textureName;
    (void)PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_ABORT,
                       FrontendPlatform.contextId);
}

/* Copy an already-uploaded tile into the retained shadow of `shadowName`
 * when that texture owns one and the tile was not written there in the
 * first place. Mirrors the single-tile path's post-commit shadow staging
 * (unit-0 keying). The source layout is the caller's upload layout. */
static void FrontendShadowStageSource(struct MiniGLR200Context *context,
                                      ULONG shadowName, const UBYTE *source,
                                      ULONG width, ULONG height,
                                      GLenum format, GLenum type,
                                      ULONG sourceBytes, ULONG rowBytes,
                                      ULONG sourceStride)
{
    struct TextureObject *target;

    if (!shadowName || shadowName >= MAX_TEXTURES)
        return;
    target = &context->textures[shadowName];
    if (FrontendShadowEnsure(context, target, width, height))
        FrontendStageTextureBGRA(target->shadow, source, width, height,
            format, type, sourceBytes, rowBytes, sourceStride);
}

/* Ordinary single-tile direct upload of one staged tile: BEGIN, fill the
 * aperture from the staged BGRA image, COMMIT, mirror. The caller must
 * have drained the pipeline before this call. */
static BOOL FrontendPendingUploadCommitStaged(
    struct MiniGLR200Context *context,
    const R200PPCPendingDirectUpload *pending)
{
    ULONG name = pending->textureName;
    UBYTE *shadowDestination = NULL;
    struct TextureObject *shadowTarget = NULL;
    BOOL fused = FALSE;

    if (!name || name >= MAX_TEXTURES || !context->textures[name].generated)
        return FALSE;
    if (!pending->staging)
        return FALSE;
    if (pending->stagingIsShadow &&
        context->textures[name].shadow != pending->staging) {
        /* The retained shadow that held the staged tile vanished; there is
         * nothing safe left to upload. */
        FrontendInvalidateTextureMirror(context, name);
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    if (!pending->stagingIsShadow && pending->shadowName &&
        pending->shadowName < MAX_TEXTURES) {
        shadowTarget = &context->textures[pending->shadowName];
        fused = shadowTarget == &context->textures[name] &&
                FrontendShadowEnsure(context, shadowTarget, pending->width,
                                     pending->height);
        if (fused)
            shadowDestination = shadowTarget->shadow;
    }
    {
#if MGLPPC_PROFILE
    ULONG phaseStart = PerfClock();
#endif
    if (!FrontendDirectTextureUploadNamed(context, name, pending->width,
            pending->height, MGL_UBYTE_BGRA, GL_UNSIGNED_BYTE, pending->staging,
            pending->width * 4UL, shadowDestination)) {
#if MGLPPC_PROFILE
        BatchDiagSingleTicks += PerfClock() - phaseStart;
        ++BatchDiagSingles;
#endif
        FrontendInvalidateTextureMirror(context, name);
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    if (!FrontendPlatform.control.value[1] ||
        FrontendPlatform.control.value[2] != pending->width ||
        FrontendPlatform.control.value[3] != pending->height ||
        !FrontendPlatform.control.value[4] ||
        !FrontendPlatform.control.value[5] ||
        FrontendPlatform.control.value[7] != context->deviceEpoch) {
        FrontendInvalidateTextureMirror(context, name);
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
#if MGLPPC_PROFILE
    BatchDiagSingleTicks += PerfClock() - phaseStart;
    ++BatchDiagSingles;
#endif
    }
    FrontendMirrorTextureValues(context, name,
        FrontendPlatform.control.value[1], FrontendPlatform.control.value[2],
        FrontendPlatform.control.value[3], FrontendPlatform.control.value[4],
        FrontendPlatform.control.value[5], FrontendPlatform.control.value[6],
        FrontendPlatform.control.value[7]);
    if (!pending->stagingIsShadow && !shadowDestination)
        FrontendShadowStageSource(context, pending->shadowName,
                                  pending->staging, pending->width,
                                  pending->height, MGL_UBYTE_BGRA,
                                  GL_UNSIGNED_BYTE, 4UL,
                                  pending->width * 4UL, pending->width * 4UL);
    return TRUE;
}

/* Complete (or discard) a deferred first tile. Safe to call from any
 * dispatch entry: it re-enters no flush hook, because the pending flag is
 * cleared before the pipeline is drained. */
BOOL R200PPCPendingUploadFlush(struct MiniGLR200Context *context)
{
    R200PPCPendingDirectUpload pending;

    if (!FrontendPlatform.pendingDirectActive)
        return TRUE;
    /* Take ownership before any call below can re-enter a flush hook. */
    FrontendPendingDirectTake(&pending);
    if (!context || !FrontendPlatform.opened ||
        FrontendPlatform.mailboxAbandoned ||
        !(FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_DIRECT_BATCH_V1))
        return TRUE;
    if (pending.epoch != context->deviceEpoch)
        return TRUE;
    if (!PlatformMailboxReady() ||
        !FlushScreenVertices(context) || !Submit(context) ||
        (FrontendPlatform.ring && !R200PPCPlatformBarrier(context, FALSE))) {
        if (pending.textureName && pending.textureName < MAX_TEXTURES)
            FrontendInvalidateTextureMirror(context, pending.textureName);
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    return FrontendPendingUploadCommitStaged(context, &pending);
}

/* Two-tile attempt: the pending first tile plus the current upload, one
 * BEGIN2 + one COMMIT2. Returns R200_BATCH_NONE when the pair does not
 * match (the caller's ordinary path then applies). */
static int FrontendDirectBatchTry(struct MiniGLR200Context *context,
                                  ULONG width, ULONG height, GLenum format,
                                  GLenum type, const UBYTE *source,
                                  ULONG sourceStride)
{
    R200PPCPendingDirectUpload pending;
    ULONG nameB = context->boundTexture[context->activeTexture];
    ULONG addrA, pitchA, addrB, pitchB, fmtA, fmtB, epoch, mask;
    ULONG stagingBytes = width * height * 4UL;
    UBYTE *destA, *destB, *shadowA = NULL, *shadowB = NULL;
    struct TextureObject *shadowTargetA, *shadowTargetB;
    BOOL fusedA = FALSE, fusedB = FALSE;

    if (!FrontendPlatform.pendingDirectActive)
        return R200_BATCH_NONE;
    if (FrontendPlatform.batchDisabled) {
        FrontendPlatform.pendingDirectActive = FALSE;
        return R200_BATCH_NONE;
    }
    FrontendPendingDirectTake(&pending);
    if (pending.epoch != context->deviceEpoch ||
        format != GL_RGB || type != MGL_UNSIGNED_SHORT_5_6_5 ||
        !nameB || nameB >= MAX_TEXTURES || nameB == pending.textureName ||
        !width || !height || !stagingBytes ||
        stagingBytes > MGLPPC_TEXTURE_SHARED_MAX_BYTES ||
        !PlatformMailboxReady() || FrontendPlatform.mailboxAbandoned ||
        context->insideBegin) {
        FrontendPlatform.pendingDirectActive = TRUE;
        return R200_BATCH_NONE;
    }
    /* The batch owns the pending state from here. */
    {
#if MGLPPC_PROFILE
    ULONG phaseStart = PerfClock();
#endif
    if (!FlushScreenVertices(context) || !Submit(context) ||
        (FrontendPlatform.ring && !R200PPCPlatformBarrier(context, FALSE))) {
        if (pending.textureName && pending.textureName < MAX_TEXTURES)
            FrontendInvalidateTextureMirror(context, pending.textureName);
        SetError(context, GL_INVALID_OPERATION);
        return R200_BATCH_FALLBACK;
    }
#if MGLPPC_PROFILE
    BatchDiagBarrierTicks += PerfClock() - phaseStart;
    phaseStart = PerfClock();
#endif
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = pending.textureName;
    FrontendPlatform.control.value[1] = pending.width;
    FrontendPlatform.control.value[2] = pending.height;
    FrontendPlatform.control.value[3] = MGLPPC_TEXDIRECT_FMT_BGRA32;
    FrontendPlatform.control.value[4] = nameB;
    FrontendPlatform.control.value[5] = width;
    FrontendPlatform.control.value[6] = height;
    FrontendPlatform.control.value[7] = MGLPPC_TEXDIRECT_FMT_BGRA32;
    if (!PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2,
                      FrontendPlatform.contextId)) {
        /* The host aborts a partially begun pair before replying: finish
         * tile A through the ordinary path so it is not lost, then let the
         * caller upload the current tile its usual way. */
        (void)FrontendPendingUploadCommitStaged(context, &pending);
        return R200_BATCH_FALLBACK;
    }
#if MGLPPC_PROFILE
    BatchDiagBeginTicks += PerfClock() - phaseStart;
    phaseStart = PerfClock();
#endif
    addrA = FrontendPlatform.control.value[1];
    pitchA = FrontendPlatform.control.value[2];
    addrB = FrontendPlatform.control.value[3];
    pitchB = FrontendPlatform.control.value[4];
    fmtA = FrontendPlatform.control.value[5];
    fmtB = FrontendPlatform.control.value[6];
    epoch = FrontendPlatform.control.value[7];
    if (!addrA || !pitchA || !addrB || !pitchB ||
        !pending.width || !pending.height ||
        pending.width > pitchA / 4UL || width > pitchB / 4UL ||
        pitchA > (0xffffffffUL - addrA) / pending.height ||
        pitchB > (0xffffffffUL - addrB) / height ||
        fmtA != RADEON3D_FORMAT_B8G8R8A8 ||
        fmtB != RADEON3D_FORMAT_B8G8R8A8 ||
        epoch != context->deviceEpoch) {
        FrontendDirectAbort(pending.textureName);
        FrontendDirectAbort(nameB);
        (void)FrontendPendingUploadCommitStaged(context, &pending);
        return R200_BATCH_FALLBACK;
    }
    destA = (UBYTE *)addrA;
    destB = (UBYTE *)addrB;
    /* Tile A owns its shadow when the staging buffer is that shadow. */
    if (!pending.stagingIsShadow && pending.shadowName &&
        pending.shadowName < MAX_TEXTURES) {
        shadowTargetA = &context->textures[pending.shadowName];
        fusedA = shadowTargetA == &context->textures[pending.textureName] &&
                 FrontendShadowEnsure(context, shadowTargetA, pending.width,
                                      pending.height);
        if (fusedA)
            shadowA = shadowTargetA->shadow;
    }
    FrontendDirectWritePixels(destA, pitchA, RADEON3D_FORMAT_B8G8R8A8,
                              pending.staging, pending.width * 4UL,
                              pending.width, pending.height, MGL_UBYTE_BGRA,
                              GL_UNSIGNED_BYTE, shadowA);
    MGLPPCFlushDataRange((APTR)destA, pitchA * pending.height);
    /* Tile B uses the caller's source layout; fuse its shadow the same way
     * the single-tile path does. */
    if (context->boundTexture[0] &&
        context->boundTexture[0] < MAX_TEXTURES) {
        shadowTargetB = &context->textures[context->boundTexture[0]];
        fusedB = shadowTargetB == &context->textures[nameB] &&
                 FrontendShadowEnsure(context, shadowTargetB, width, height);
        if (fusedB)
            shadowB = shadowTargetB->shadow;
    }
    FrontendDirectWritePixels(destB, pitchB, RADEON3D_FORMAT_B8G8R8A8,
                              source, sourceStride, width, height, format,
                              type, shadowB);
    MGLPPCFlushDataRange((APTR)destB, pitchB * height);
#if MGLPPC_PROFILE
    BatchDiagWriteTicks += PerfClock() - phaseStart;
    phaseStart = PerfClock();
#endif
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = pending.textureName;
    FrontendPlatform.control.value[1] = 0UL;
    FrontendPlatform.control.value[2] = nameB;
    FrontendPlatform.control.value[3] = 0UL;
    (void)PlatformCall(MGLPPC_CMD_TEXTURE_DIRECT_COMMIT2,
                       FrontendPlatform.contextId);
#if MGLPPC_PROFILE
    BatchDiagCommitTicks += PerfClock() - phaseStart;
    ++BatchDiagPairs;
#endif
    }
    /* value[3] is the device epoch in every COMMIT2 reply and 0 in the
     * request, so a stale/refused reply cannot look committed. A lost
     * reply must never be treated as a partial success. */
    mask = (!FrontendPlatform.mailboxAbandoned &&
            FrontendPlatform.control.value[3] == context->deviceEpoch)
        ? (FrontendPlatform.control.value[0] & 3UL) : 0UL;
    if ((mask & 1UL) && FrontendPlatform.control.value[1])
        FrontendMirrorTextureValues(context, pending.textureName,
            FrontendPlatform.control.value[1], pending.width, pending.height,
            FrontendPlatform.control.value[6] & 0xffffUL,
            FrontendPlatform.control.value[6] >> 16,
            FrontendPlatform.control.value[4],
            FrontendPlatform.control.value[3]);
    else {
        mask &= ~1UL;
        FrontendInvalidateTextureMirror(context, pending.textureName);
    }
    if ((mask & 2UL) && FrontendPlatform.control.value[2])
        FrontendMirrorTextureValues(context, nameB,
            FrontendPlatform.control.value[2], width, height,
            FrontendPlatform.control.value[7] & 0xffffUL,
            FrontendPlatform.control.value[7] >> 16,
            FrontendPlatform.control.value[5],
            FrontendPlatform.control.value[3]);
    else {
        mask &= ~2UL;
        FrontendInvalidateTextureMirror(context, nameB);
    }
    if ((mask & 3UL) != 3UL) {
        SetError(context, GL_INVALID_OPERATION);
        return R200_BATCH_FALLBACK;
    }
    if (!pending.stagingIsShadow && !shadowA)
        FrontendShadowStageSource(context, pending.shadowName,
                                  pending.staging, pending.width,
                                  pending.height, MGL_UBYTE_BGRA,
                                  GL_UNSIGNED_BYTE, 4UL,
                                  pending.width * 4UL, pending.width * 4UL);
    if (!fusedB)
        FrontendShadowStageSource(context, context->boundTexture[0], source,
                                  width, height, format, type,
                                  MGLPPC_TEXTURE_SOURCE_BYTES(format, type),
                                  width * 2UL, sourceStride);
#if MGLPPC_PROFILE
    ++BatchDiagFrames;
#endif
    return R200_BATCH_DONE;
}

/* Queue the current upload as the first tile of a pair when it is the
 * 565 shape the batched path was built for; otherwise complete any
 * deferred tile through the ordinary path. The caller has already
 * validated the direct-path eligibility of the current upload. */
static int FrontendDirectBatchOffer(struct MiniGLR200Context *context,
                                    ULONG width, ULONG height, GLenum format,
                                    GLenum type, const UBYTE *source,
                                    ULONG sourceStride)
{
    ULONG textureName = context->boundTexture[context->activeTexture];
    ULONG stagingBytes = width * height * 4UL;
    struct TextureObject *shadowTarget;
    UBYTE *staging;
    BOOL stagingIsShadow = FALSE;

    if (FrontendPlatform.pendingDirectActive) {
        int result = FrontendDirectBatchTry(context, width, height, format,
                                            type, source, sourceStride);
        if (result != R200_BATCH_NONE)
            return result;
        /* Incompatible second upload: finish tile A before this call can
         * invalidate its staging (same-texture redefinition, shadow free,
         * buffer reuse). */
        if (!R200PPCPendingUploadFlush(context))
            return R200_BATCH_FALLBACK;
    }
    if (FrontendPlatform.batchDisabled ||
        !(FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_DIRECT_BATCH_V1) ||
        format != GL_RGB || type != MGL_UNSIGNED_SHORT_5_6_5 ||
        context->insideBegin || !stagingBytes ||
        stagingBytes > MGLPPC_TEXTURE_SHARED_MAX_BYTES ||
        !PlatformMailboxReady() || FrontendPlatform.mailboxAbandoned)
        return R200_BATCH_NONE;
    if (!textureName || textureName >= MAX_TEXTURES ||
        !context->textures[textureName].generated)
        return R200_BATCH_NONE;
    /* Stage the converted tile in PPC memory: the texture's own retained
     * shadow when it owns one (no extra copy), otherwise the shared upload
     * buffer, whose only other users flush the pending tile first. */
    shadowTarget = (context->boundTexture[0] &&
                    context->boundTexture[0] < MAX_TEXTURES)
        ? &context->textures[context->boundTexture[0]] : NULL;
    if (shadowTarget == &context->textures[textureName] &&
        FrontendShadowEnsure(context, shadowTarget, width, height)) {
        staging = shadowTarget->shadow;
        stagingIsShadow = TRUE;
    } else {
        if (!FrontendEnsureTextureUploadBuffer(stagingBytes))
            return R200_BATCH_NONE;
        staging = FrontendPlatform.textureUploadBuffer;
    }
    FrontendStageTextureBGRA(staging, source, width, height, GL_RGB,
                             MGL_UNSIGNED_SHORT_5_6_5, 2UL, width * 2UL,
                             sourceStride);
    FrontendPlatform.pendingDirectActive = TRUE;
    FrontendPlatform.pendingDirectName = textureName;
    FrontendPlatform.pendingDirectWidth = width;
    FrontendPlatform.pendingDirectHeight = height;
    FrontendPlatform.pendingDirectShadowName = context->boundTexture[0];
    FrontendPlatform.pendingDirectEpoch = context->deviceEpoch;
    FrontendPlatform.pendingDirectStaging = staging;
    FrontendPlatform.pendingDirectStagingIsShadow = stagingIsShadow;
    return R200_BATCH_DEFERRED;
}

/* EXT_paletted_texture internalformat (Quake gl_draw.c:30). No stack
 * header defines it; the value is fixed by the extension. */
#define MGL_COLOR_INDEX8_EXT 0x80E5UL

/* DEBUG (2D-text investigation): bounded canonical-staging dump. For the
 * first uploads after boot, write wire texels to DH2:texdump.log so the
 * PPC-side BGRA conversion can be compared against the source TGA. The
 * font atlases carry WHITE RGB at every texel with the glyph mask in
 * ALPHA, so each dumped byte group reads B,G,R,A: a healthy texel is
 * either FFFFFF00 (transparent) or FFFFFFFF (glyph stroke). */
static void FrontendTexStageDump(ULONG name, ULONG width, ULONG height,
                                 ULONG level, ULONG format, ULONG type,
                                 const UBYTE *staging)
{
    static LONG dumpsLeft = 24;
    static BOOL opened = FALSE;
    static const char hex[] = "0123456789ABCDEF";
    ULONG probeOffsets[4];
    int probeCount = 0;
    char line[384];
    int pos = 0, i, b;
    BPTR fh;

    if (dumpsLeft <= 0 || !staging || !width || !height)
        return;
    /* Skip the tiny scratch/system textures so the bounded budget
     * reaches the large atlas uploads (fonts/logo/fog/console). */
    if (width * height < 4096UL)
        return;
    --dumpsLeft;
    probeOffsets[probeCount++] = 0;                     /* texel (0,0)  */
    if (height > 8UL)
        probeOffsets[probeCount++] = 8UL * width * 4UL; /* texel (0,8)  */
    if (width == 256UL && height == 256UL) {
        /* Glyph-interior probes matching the decoded TGA samples. */
        probeOffsets[probeCount++] = (40UL * 256UL + 115UL) * 4UL;
        probeOffsets[probeCount++] = (128UL * 256UL + 64UL) * 4UL;
    }
    pos += sprintf(line + pos,
                   "STAGE name=%lu %lux%lu lvl=%lu fmt=%lu type=%lu tex:",
                   (unsigned long)name, (unsigned long)width,
                   (unsigned long)height, (unsigned long)level,
                   (unsigned long)format, (unsigned long)type);
    for (i = 0; i < probeCount; ++i) {
        ULONG off = probeOffsets[i];
        line[pos++] = ' ';
        line[pos++] = '|';
        for (b = 0; b < 8; ++b) {
            UBYTE v = staging[off + (ULONG)b];
            line[pos++] = hex[v >> 4];
            line[pos++] = hex[v & 15];
        }
    }
    line[pos++] = '\n';
    line[pos] = 0;
    fh = Open((STRPTR)"DH2:texdump.log",
              opened ? MODE_OLDFILE : MODE_NEWFILE);
    opened = TRUE;
    if (fh) {
        Seek(fh, 0, OFFSET_END);
        Write(fh, (APTR)line, (LONG)pos);
        Close(fh);
    }
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
    BOOL directAllowed;
    /* Paletted sources: Quake's GL_Upload8_EXT path (gl_draw.c:30,
     * GL_COLOR_INDEX8_EXT internalformat with GL_COLOR_INDEX indices).
     * Indices always expand through the frontend palette mirror into
     * canonical BGRA staging (see FrontendStageTextureBGRA); the wire
     * never carries raw indices because the native palette is not
     * synchronized across CPUs. */
    BOOL paletted = (format == GL_COLOR_INDEX && type == GL_UNSIGNED_BYTE);
    /* Classic MiniGL clients (original V19 headers, 68k ports and
     * MOS2WOS conversions like Silent Hill) use the pre-re-anchor tail
     * numbering: BGRA 237, ARGB 238, 565 239, 4444 240, plus the legacy
     * 16-bit pairing (format = GL_UNSIGNED_SHORT, type = 565/4444).
     * Accept both numberings and normalise to the dispatch numbering
     * the validation, wire translation and native backend use. */
    if (format >= 237U && format <= 240U) format -= 4U;
    if (type >= 239U && type <= 240U) type -= 4U;
    if (format == GL_UNSIGNED_SHORT) {
        if (type == MGL_UNSIGNED_SHORT_5_6_5) format = GL_RGB;
        else if (type == MGL_UNSIGNED_SHORT_4_4_4_4) format = GL_RGBA;
    }
    if (!StateChangeAllowed(context)) return;
    if (!(FrontendPlatform.capabilities & MGLPPC_CAP_SEMANTIC_TEXTURE_V1)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    /* Internalformat accepts the same resolved list as the native
     * backend (raw component counts 3/4 included, GL 1.0 style, plus
     * the sized aliases the native gate accepts -- storage is always
     * canonical BGRA32, so sized aliases widen to their unsized class),
     * and the EXT_paletted_texture internalformat for paletted uploads
     * (always expanded PPC-side; never forwarded). The DIRECT path
     * never forwards it, the shared/chunked paths pass it through to
     * native glTexImage2D; validating here gives the client the
     * documented error code at the documented call. GL_RGBA4 has no
     * enumerator in mgl/gl.h, so the real-GL value is matched
     * literally, exactly like the native gate. */
    if (internalFormat != GL_RGB && internalFormat != GL_RGB5 &&
        internalFormat != GL_RGB8 &&
        internalFormat != GL_RGB5_A1 &&
        internalFormat != (GLint)0x8052 /* GL_RGBA4 */ &&
        internalFormat != GL_RGBA &&
        internalFormat != GL_RGBA8 &&
        internalFormat != 3 && internalFormat != 4 &&
        internalFormat != GL_ALPHA && internalFormat != GL_ALPHA8 &&
        internalFormat != GL_LUMINANCE && internalFormat != GL_LUMINANCE8 &&
        internalFormat != GL_LUMINANCE_ALPHA &&
        internalFormat != GL_LUMINANCE8_ALPHA8 &&
        internalFormat != GL_COLOR_INDEX &&
        internalFormat != (GLint)MGL_COLOR_INDEX8_EXT) {
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
        format != GL_LUMINANCE_ALPHA && !paletted) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (paletted && FrontendPlatform.peerProtocol < 15UL) {
        /* Paletted uploads canonicalize PPC-side (see below); a
         * protocol-14 host cannot carry the canonical BGRA packing. */
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
    if (context->boundTexture[context->activeTexture] == 0) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    texture = &context->textures[context->boundTexture[context->activeTexture]];
    /* Mip safety: a direct texture carries exactly ONE level (native
     * BeginDirectTexture sets levelCount=1) and a later level>0 upload on
     * it is rejected by the native TexImage2D. Quake uploads level 0
     * first and mips after, so the ONLY safe predictor is the source
     * class: single-channel uploads (GL_LUMINANCE/ALPHA/LUMINANCE_ALPHA -
     * the lightmap class) never carry mips. Do NOT gate on
     * texture->generated (the frontend BindTexture wrapper calls the
     * in-DLL Lib_glBindTexture, which marks EVERY bound name generated)
     * nor on mirror minFilter (its default GL_NEAREST is non-mip, and
     * Quake sets world-texture filters only AFTER uploading - both
     * verified by the 2026-09-20 trace: 267/267 direct uploads including
     * all world textures, their level>0 uploads then lost). */
    directAllowed = (format == GL_LUMINANCE || format == GL_ALPHA ||
                     format == GL_LUMINANCE_ALPHA);
    if ((FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_DIRECT_V1) &&
        directAllowed &&
        !paletted &&
        level == 0 && width <= 2048 && height <= 2048 &&
        total <= MGLPPC_TEXTURE_SHARED_MAX_BYTES &&
        !(width & (width - 1UL)) && !(height & (height - 1UL))) {
        UBYTE *fusedShadow = NULL;
        /* Two-tile batching: a 565 upload is either queued as the first
         * tile of a pair, paired with the deferred first tile, or the
         * deferred tile is completed before this upload proceeds. */
        {
            int batch = FrontendDirectBatchOffer(context, (ULONG)width,
                                                 (ULONG)height, format, type,
                                                 source + sourceOffset,
                                                 sourceStride);
            if (batch == R200_BATCH_DEFERRED || batch == R200_BATCH_DONE) {
#if MGLPPC_PROFILE
                PlatformProfile.textureDirectCalls++;
                if (batch == R200_BATCH_DONE)
                    PlatformProfile.textureSuccesses++;
#endif
                return;
            }
        }
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
        /* Fuse the canonical-shadow fill into the direct conversion pass:
         * the loop already computes each BGRA dword, so the shadow costs one
         * extra cached store per texel instead of a second conversion pass
         * (~6 ms per 256x256 upload, U0 attribution). The shadow belongs to
         * the UPLOAD TARGET regardless of unit: GLQuake uploads its
         * lightmaps while a world texture sits on unit 0, and the subimage
         * patch-and-recommit path needs the TARGET's own shadow.
         * Retain shadows ONLY for the directAllowed (lightmap) class: every
         * direct-uploaded texture would exhaust R200_SHADOW_TOTAL_MAX before
         * the last-loaded lightmaps (n=261-266 of 267) ever got one, and
         * their subimages silently fell back to the shared path (2026-09-20
         * trace evidence: 267 dbeg, no subimage recommissions). */
        if (directAllowed && FrontendShadowEnsure(context, texture,
                                                  (ULONG)width,
                                                  (ULONG)height))
            fusedShadow = texture->shadow;
        if (FrontendDirectTextureUpload(context, (ULONG)width,
                                        (ULONG)height, format, type,
                                        source + sourceOffset,
                                        sourceStride, fusedShadow)) {
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
            /* The fused pass above fills the canonical shadow whenever
             * Ensure succeeded. Stage it separately only when Ensure
             * failed (or the fused write was skipped), so a following
             * TexSubImage can patch-and-recommit. The shadow belongs to
             * the upload target - see the fused-stage comment above. */
            if (!fusedShadow) {
#if MGLPPC_PROFILE
                chunkStart = PerfClock();
#endif
                {
                    ULONG shadowSourceBytes =
                        MGLPPC_TEXTURE_SOURCE_BYTES(format, type);
                    if (directAllowed &&
                        FrontendShadowEnsure(context, texture,
                                             (ULONG)width, (ULONG)height))
                        FrontendStageTextureBGRA(
                            texture->shadow, source + sourceOffset,
                            width, height, format, type, shadowSourceBytes,
                            rowBytes, sourceStride);
                }
#if MGLPPC_PROFILE
                PlatformProfile.textureDirectShadowTicks +=
                    PerfClock() - chunkStart;
#endif
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
    } else if (!R200PPCPendingUploadFlush(context)) {
        /* Not direct-eligible: complete a deferred first tile before the
         * shared path can free/replace the staging shadow. */
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!FrontendTextureBarrier(context)) {
        CpDbgPrintf("TEXBARRIER fail w=%lu h=%lu level=%lu cpfail=%lu\n",
                    (unsigned long)width, (unsigned long)height,
                    (unsigned long)level,
                    (unsigned long)FrontendPlatform.cpFailed);
        return;
    }

    {
    /* Shared staging converts the client texels to canonical BGRA8888
     * on the PPC (native row-copy fast path) instead of handing raw
     * client rows to the 68k ConvertPixel loop. v14 hosts cannot carry
     * the tail types, so only peer protocol 15 canonicalizes --
     * paletted uploads always canonicalize (indices expand through the
     * frontend palette mirror; v14 paletted was rejected above). */
    ULONG sharedBytes = total;
    GLboolean canonical = FALSE;
    ULONG stageSourceBytes = MGLPPC_TEXTURE_SOURCE_BYTES(format, type);
    if (paletted) {
        sharedBytes = (ULONG)width * (ULONG)height * 4UL;
        canonical = TRUE;
        stageSourceBytes = 1UL;
    } else if (FrontendPlatform.peerProtocol >= 15UL) {
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
                                     format, type, stageSourceBytes,
                                     rowBytes, sourceStride);
        if (canonical)
            FrontendTexStageDump(
                (ULONG)context->boundTexture[context->activeTexture],
                (ULONG)width, (ULONG)height, (ULONG)level,
                (ULONG)format, (ULONG)type,
                FrontendPlatform.textureUploadBuffer);
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
        /* Paletted sources travel canonicalized (BGRA texels); the
         * native side only validates internalformat, so forward plain
         * GL_COLOR_INDEX instead of 0x80E5, which native rejects (its
         * storage is BGRA32 either way). */
        FrontendPlatform.control.value[2] = paletted
            ? (ULONG)GL_COLOR_INDEX : (ULONG)internalFormat;
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
        SharedUploadHistoNote(context->boundTexture[context->activeTexture],
                              level, width, height, format, type,
                              PerfClock() - chunkStart);
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
        /* Paletted uploads must go through canonical shared staging
         * (the chunked path would hand raw indices to the unsynchronized
         * native palette). Reaching here means staging was unavailable;
         * fail loudly instead of uploading wrong texels. */
        if (paletted) {
            FrontendSetTransportError(context);
            return;
        }
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
    /* The native TexImage2D released this texture's previous surface during
     * the command; drop its cached descriptor before the inline mirror
     * update hides the old handle (same-epoch reuse hazard, P0/W-01). The
     * pre-command drop already covers the normal path; this is the
     * belt-and-braces copy for any future caller ordering. */
    FrontendDropCachedSurface((ULONG)texture->surface.Handle);
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

#if MGLPPC_PROFILE
static void FrontendProfiledTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLsizei width, GLsizei height,
                                  GLenum format, GLenum type,
                                  const GLvoid *pixels, struct PpcLmRecord *lm)
#else
void R200PPCDispatchTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLsizei width, GLsizei height,
                                  GLenum format, GLenum type,
                                  const GLvoid *pixels)
#endif
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
    /* Classic numbering/pairing compatibility, see TexImage2D. */
    if (format >= 237U && format <= 240U) format -= 4U;
    if (type >= 239U && type <= 240U) type -= 4U;
    if (format == GL_UNSIGNED_SHORT) {
        if (type == MGL_UNSIGNED_SHORT_5_6_5) format = GL_RGB;
        else if (type == MGL_UNSIGNED_SHORT_4_4_4_4) format = GL_RGBA;
    }
    bpp = (format == GL_COLOR_INDEX && type == GL_UNSIGNED_BYTE)
              ? 1UL : MGLPPC_TEXTURE_SOURCE_BYTES(format, type);
    if (!bpp) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    name = context->boundTexture[context->activeTexture];
    if (!name || name >= MAX_TEXTURES) {
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
    if (!width || !height) {
#if MGLPPC_PROFILE
        if (lm) lm->result = 2;
#endif
        return;
    }
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

    if (lm) lm->sourceBytes = (ULONG)width * (ULONG)height * bpp;
    PlatformProfile.textureCalls++;
    PlatformProfile.textureBytes += bytes;
    PlatformProfile.textureSharedCalls++;
#endif
    /* Canonical BGRA staging: the wire carries width*height*4 bytes, so
     * the shared cap applies to the canonical size (the host validates
     * the staging span at bpp 4). */
    bytes = (ULONG)width * (ULONG)height * 4UL;
#if MGLPPC_PROFILE
    if (lm) lm->wireBytes = bytes;
#endif
    if (bytes > MGLPPC_TEXTURE_SHARED_MAX_BYTES) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    /* A deferred direct tile owns the texture's shadow (its staging) and
     * the shared upload buffer; complete it before either can be patched,
     * freed, or reused. */
    PPC_LM_NEXT(PPC_LM_PENDING);
    if (!R200PPCPendingUploadFlush(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    /* Fast path: a retained direct-eligible shadow lets this subimage
     * patch PPC-side memory and re-commit the WHOLE image through the
     * direct pipeline (fresh BEGIN surface per commit, aperture-rate
     * fill, no 68k copyback, no native residency churn). Same
     * eligibility rules as the direct TexImage path plus the shadow.
     * Paletted sources skip it (FrontendShadowPatch cannot expand
     * indices); they take the canonical shared path below. */
    if (texture->shadow &&
        format != GL_COLOR_INDEX &&
        texture->shadowWidth == (ULONG)texture->width &&
        texture->shadowHeight == (ULONG)texture->height &&
        level == 0 &&
        (FrontendPlatform.capabilities & MGLPPC_CAP_TEXTURE_DIRECT_V1) &&
        texture->width >= (ULONG)(xoffset + width) &&
        texture->height >= (ULONG)(yoffset + height) &&
        !FrontendPlatform.mailboxAbandoned &&
        !(texture->width & (texture->width - 1UL)) &&
        !(texture->height & (texture->height - 1UL))) {
        /* CP path with a parking host: wait for consumption only. */
        BOOL park = FrontendPlatform.cpEnabled && !FrontendPlatform.cpFailed &&
                    FrontendPlatform.ring && !FrontendPlatform.leaseActive &&
                    !FrontendNoPark &&
                    (FrontendPlatform.capabilities &
                     MGLPPC_CAP_TEXTURE_DIRECT_PARK);
        BOOL uploaded;
#if MGLPPC_PROFILE
        subChunkStart = PerfClock();
        PlatformProfile.textureBarrierCalls++;
        if (lm) lm->path = 2;
#endif
        PPC_LM_NEXT(PPC_LM_DIRECT_PATCH);
        /* The shadow patch precedes publishing: the client's texels are
         * the truth until the commit completes. */
        FrontendShadowPatch(texture, (const UBYTE *)pixels + offset,
                            width, height, format, type,
                            bpp, stride, xoffset, yoffset);
        if (park && !FrontendNoPool && PlatformMailboxReady() &&
            texture->width >= 16UL && texture->height >= 16UL &&
            texture->width <= 256UL && texture->height <= 256UL) {
            int pooled = FrontendLmPoolUpdate(context, texture, name,
                                              (ULONG)yoffset, (ULONG)height);

            if (pooled < 0) {
                SetError(context, GL_INVALID_OPERATION);
                return;
            }
            if (pooled) {
#if MGLPPC_PROFILE
                if (lm) {
                    lm->path = 4;
                    lm->result = 1;
                }
                PlatformProfile.textureSuccesses++;
                PlatformProfile.textureTicks += PerfClock() - textureStart;
#endif
                return;
            }
        }
        PPC_LM_NEXT(PPC_LM_DIRECT_BARRIER);
        if (!PlatformMailboxReady() ||
            !FlushScreenVertices(context) || !Submit(context) ||
            (FrontendPlatform.ring &&
             !(park ? FrontendWaitRingConsumed()
                    : R200PPCPlatformBarrier(context, FALSE)))) {
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
        PPC_LM_NEXT(PPC_LM_DIRECT);
        FrontendDirectRequestFlags = park ? MGLPPC_TEXDIRECT_REQ_PARK_OLD : 0UL;
        uploaded = FrontendDirectTextureUpload(context, texture->shadowWidth,
                                               texture->shadowHeight,
                                               MGL_UBYTE_BGRA, GL_UNSIGNED_BYTE,
                                               texture->shadow,
                                               texture->shadowWidth * 4UL,
                                               NULL);
        FrontendDirectRequestFlags = 0;
        if (uploaded) {
#if MGLPPC_PROFILE
            PlatformProfile.textureDirectTicks += PerfClock() - subChunkStart;
#endif
            PPC_LM_NEXT(PPC_LM_DIRECT_REPLY);
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
            if (lm) lm->result = 1;
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
        PPC_LM_NEXT(PPC_LM_ABORT);
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
#if MGLPPC_PROFILE
    if (lm) lm->path = lm->path == 2 ? 3 : 1;
#endif
    PPC_LM_NEXT(PPC_LM_BARRIER);
    if (!FrontendTextureBarrier(context)) return;
    PPC_LM_NEXT(PPC_LM_STAGE);
    if (!FrontendEnsureTextureUploadBuffer(bytes)) {
        SetError(context, GL_OUT_OF_MEMORY);
        return;
    }
    /* Canonical BGRA8888 staging keeps the native path on its per-row
     * CopyMem instead of the 68k per-texel ConvertPixel loop. */
    FrontendStageTextureBGRA(FrontendPlatform.textureUploadBuffer,
        (const UBYTE *)pixels + offset, width, height, format, type,
        bpp, rowBytes, stride);
    PPC_LM_NEXT(PPC_LM_CACHE);
    MGLPPCFlushDataRange(FrontendPlatform.textureUploadBuffer, bytes);
    PPC_LM_NEXT(PPC_LM_REQUEST);
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
     * TexImage can restore this mirror; old draws are already published.
     * Drop the cached descriptor BEFORE the mirror is cleared: the platform
     * pre-command drop reads this mirror, and a cleared handle would leave
     * the old token cached for a later reuse (plan 2026-09-20 §7 P0/W-01). */
    FrontendDropCachedSurface((ULONG)texture->surface.Handle);
    texture->resident = FALSE;
    texture->surface.Handle = NULL;
    texture->residentGeneration = 0;
    BumpSemanticStateSerial(context);
#if MGLPPC_PROFILE
    ++PpcLmSharedOrder; /* Attempts, not a new protocol field or native-call count. */
    if (lm) {
        lm->sharedOrder = PpcLmSharedOrder;
        lm->mailbox = FrontendPlatform.mailbox != NULL;
    }
#endif
    PPC_LM_NEXT(PPC_LM_CALL);
    if (!PlatformCall(MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED,
                      FrontendPlatform.contextId)) {
        FrontendSetTransportError(context);
        return;
    }
    PPC_LM_NEXT(PPC_LM_MIRROR);
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
    if (lm) lm->result = 1;
#endif
}

#if MGLPPC_PROFILE
/* One exit snapshot even for validation, barrier, allocation or transport
 * failure. Peek the sticky GL error; never consume or replace it. */
void R200PPCDispatchTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLsizei width, GLsizei height,
                                  GLenum format, GLenum type, const GLvoid *pixels)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct PpcLmRecord *lm = PpcLmBegin(context, level, xoffset, yoffset,
                                       width, height, format, type);
    FrontendProfiledTexSubImage2D(target, level, xoffset, yoffset, width, height,
                                 format, type, pixels, lm);
    if (lm) {
        lm->errorAfter = context ? context->error : GL_NO_ERROR;
        MGLLmNext(&lm->time, MGL_LM_PHASES, PerfClock());
    }
}
#endif

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
    /* Freeing a texture's retained shadow must not free a deferred tile's
     * staging; the flush also keeps the native handle mirrored. */
    if (!R200PPCPendingUploadFlush(context))
        return;
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
            /* The native surface is released with this name; drop its cached
             * descriptor before the handle can be reallocated elsewhere. */
            FrontendDropCachedSurface((ULONG)texture->surface.Handle);
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
