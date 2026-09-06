/* Compile/link probe for the production R200 source under vbcc/WarpOS. */
#define NO_INLINE_LIBCALLS 1
/* The configured NDK clib headers annotate m68k entry points with this token.
 * For a compile-only PPC pass they are declarations, never linked calls. */
#ifndef __stdargs
#define __stdargs
#endif

#include <exec/types.h>
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>
#include <libraries/Picasso96.h>
#include <utility/tagitem.h>

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
#include <string.h>
#if defined(R200_FRONTEND_SEMANTIC_STEP2) || defined(R200_FRONTEND_LIBRARY)
#include <libraries/minigl_ppc_transport.h>
#include <libraries/minigl_ppc_semantic_profile.h>
#if defined(R200_FRONTEND_LIBRARY)
#include <libraries/minigl_ppc_events.h>
#include <libraries/minigl_ppc_semantic_replay.h>
#endif
#endif

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

extern void MGLPPCFlushDataRange(APTR address, ULONG bytes);
extern void MGLPPCStoreBarrier(void);
extern void MGLPPCInvalidateLine32(APTR address);
extern APTR AllocVec(ULONG size, ULONG flags);
extern void FreeVec(APTR memory);

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
    APTR windowHandle;
    BOOL crcFirstOnly;
    BOOL crcArmed;
    BOOL hostEventsConfirmed;
    volatile MGLPPCRingControlV1 *ring;
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
    printf("SEMREPLAY batch_grow batches=%lu capacity=%lu new_capacity=%lu "
           "frame=%lu\n",
           (unsigned long)ReplayCapture.batchCount,
           (unsigned long)oldCapacity,
           (unsigned long)newCapacity,
           (unsigned long)ReplayCapture.frameCount);
    fflush(stdout);
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

static MGLPPCSemanticPlatformProfile PlatformProfile;
static ULONG PlatformSessionStart;
static ULONG PlatformClockRate;

static MGLPPCTimelineEntry PlatformTimeline[MGLPPC_TIMELINE_ENTRIES];
static ULONG PlatformTimelineCount;

void R200PPCTimelineMark(ULONG ev, ULONG a, ULONG b)
{
    MGLPPCTimelineEntry *slot;
    if (PlatformTimelineCount >= MGLPPC_TIMELINE_ENTRIES)
        return;
    slot = &PlatformTimeline[PlatformTimelineCount++];
    slot->tsc = PerfClock();
    slot->ev = ev;
    slot->a = a;
    slot->b = b;
}

void R200PPCTimelineDump(void)
{
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

static BOOL PlatformCall(ULONG command, ULONG contextId)
{
    ULONG profileStart = PerfClock();
    BOOL result;

    FrontendPlatform.control.command = command;
    FrontendPlatform.control.contextId = contextId;
    FrontendPlatform.control.payloadWords = 0;
    result = MGLPPCTransportCall(&FrontendPlatform.transport,
                                 &FrontendPlatform.control) &&
             FrontendPlatform.control.status == MGLPPC_STATUS_OK;
    if (command != MGLPPC_CMD_SEMANTIC_BARRIER) {
        PlatformProfile.controlCount++;
        PlatformProfile.controlTicks += PerfClock() - profileStart;
    }
    return result;
}

const MGLPPCSemanticPlatformProfile *R200PPCPlatformProfile(void)
{
    PlatformProfile.clockRate = PlatformClockRate;
    PlatformProfile.sessionTicks = PerfClock() - PlatformSessionStart;
    return &PlatformProfile;
}

ULONG R200PPCPlatformStallTicks(void)
{
    return PlatformProfile.ringStallTicks;
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

BOOL R200PPCPlatformOpen(void)
{
    if (FrontendPlatform.opened)
        return TRUE;
    memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
    memset(&PlatformProfile, 0, sizeof(PlatformProfile));
    FrontendPlatform.pixelDepth = 32;
    FrontendPlatform.windowMode = TRUE;
    FrontendPlatform.syncEnabled = 1;
    FrontendPlatform.syncSeq = 1;
    if (!MGLPPCTransportOpen(&FrontendPlatform.transport))
        return FALSE;
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    if (!PlatformCall(MGLPPC_CMD_HELLO, 0) ||
        !(FrontendPlatform.control.value[3] & MGLPPC_CAP_SEMANTIC_INLINE)) {
        MGLPPCTransportClose(&FrontendPlatform.transport);
        memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
        return FALSE;
    }
    FrontendPlatform.capabilities = FrontendPlatform.control.value[3];
    FrontendPlatform.crcFirstOnly =
        (FrontendPlatform.capabilities & MGLPPC_CAP_CRC_FIRST_ONLY) != 0;
    FrontendPlatform.crcArmed = TRUE;
    PlatformClockRate = PerfClockRate();
    PlatformSessionStart = PerfClock();
    FrontendPlatform.opened = TRUE;
    return TRUE;
}

BOOL R200PPCPlatformPublishSlot(struct MiniGLR200Context *context,
                                ULONG slotIndex)
{
    struct SubmitSlot *slot;
    MGLPPCMessage *message;
    ULONG profileStart, crcStart, flushStart, stallStart, spin;
    ULONG crc = 0;
    BOOL crcArmedThisPublish = FALSE;

    if (context != &proofContext || !FrontendPlatform.registered ||
        slotIndex >= SUBMIT_SLOT_COUNT || FrontendPlatform.pending[slotIndex])
        return FALSE;
    slot = &context->submitSlots[slotIndex];
    if (!slot->recordCount || slot->recordCount > MGLPPC_SEMANTIC_SLOT_DWORDS)
        return FALSE;
#if defined(R200_FRONTEND_LIBRARY)
    if (ReplayCapture.active)
        CaptureReplaySlot(context, slot, slotIndex);
#endif
    profileStart = PerfClock();
    if (FrontendPlatform.ring) {
        volatile MGLPPCRingControlV1 *control = FrontendPlatform.ring;
        volatile MGLPPCRingEntryV1 *entry;
        ULONG ringSlot, total, draws = 0, segmentIx = 0xFFFFFFFFUL;
        static BOOL firstPublish = TRUE;
        R200PPCTimelineMark(MGLPPC_TL_PUBLISH_ENTER, slotIndex, 0);
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
                PlatformProfile.publishSendTicks +=
                    PerfClock() - profileStart;
                return FALSE;
            }
            draws = slot->drawCount;
        }
        if (firstPublish) {
            firstPublish = FALSE;
            printf("RING publish0 slot=%lu dwords=%lu commit=%lu\n",
                   (unsigned long)slotIndex,
                   (unsigned long)slot->recordCount,
                   (unsigned long)draws);
            fflush(stdout);
        }
        if (!FrontendPlatform.crcFirstOnly || FrontendPlatform.crcArmed)
            crcArmedThisPublish = TRUE;
        stallStart = PerfClock();
        spin = 0;
        while ((control->head - control->completed) >= control->slotCount) {
            MGLPPCInvalidateLine32((APTR)&control->head);
            if (control->error || ++spin > 5000000UL) {
                printf("RING publish=FAIL stall head=%lu completed=%lu "
                       "err=%lu spin=%lu\n",
                       (unsigned long)control->head,
                       (unsigned long)control->completed,
                       (unsigned long)control->error,
                       (unsigned long)spin);
                fflush(stdout);
                PlatformProfile.publishSendTicks +=
                    PerfClock() - profileStart;
                return FALSE;
            }
        }
        {
            ULONG stalled = PerfClock() - stallStart;
            PlatformProfile.ringStallTicks += stalled;
            PlatformProfile.publishStallTicks += stalled;
        }
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
        flushStart = PerfClock();
        MGLPPCFlushDataRange(FrontendPlatform.ringSlots[ringSlot],
                             total * sizeof(ULONG));
        PlatformProfile.publishFlushTicks += PerfClock() - flushStart;
        if (crcArmedThisPublish) {
            crcStart = PerfClock();
            crc = PlatformCRC32(FrontendPlatform.ringSlots[ringSlot],
                                total);
            PlatformProfile.publishCrcTicks += PerfClock() - crcStart;
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
        R200PPCTimelineMark(MGLPPC_TL_PUBLISH_EXIT, total, draws);
        PlatformProfile.publishSendTicks += PerfClock() - profileStart;
        PlatformProfile.publishCount++;
        return TRUE;
    }
    if (!FrontendPlatform.crcFirstOnly || FrontendPlatform.crcArmed) {
        crcStart = PerfClock();
        crc = PlatformCRC32(slot->records, slot->recordCount);
        PlatformProfile.publishCrcTicks += PerfClock() - crcStart;
        FrontendPlatform.crcArmed = FALSE;
    }
    message = &FrontendPlatform.execute[slotIndex];
    flushStart = PerfClock();
    MGLPPCFlushDataRange(slot->records, slot->recordCount * sizeof(ULONG));
    PlatformProfile.publishFlushTicks += PerfClock() - flushStart;
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
    PlatformProfile.publishSendTicks += PerfClock() - profileStart;
    PlatformProfile.publishCount++;
    return TRUE;
}

BOOL R200PPCPlatformWaitSlot(struct MiniGLR200Context *context,
                            ULONG slotIndex)
{
    MGLPPCMessage *message;
    ULONG waitStart, waitElapsed;
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
    waitStart = PerfClock();
    ok = MGLPPCTransportWait(&FrontendPlatform.transport, message);
    waitElapsed = PerfClock() - waitStart;
    PlatformProfile.replyWaitCount++;
    PlatformProfile.replyWaitTicks += waitElapsed;
    if (waitElapsed > PlatformProfile.replyWaitMaxTicks)
        PlatformProfile.replyWaitMaxTicks = waitElapsed;
    FrontendPlatform.pending[slotIndex] = FALSE;
    return ok && message->status == MGLPPC_STATUS_OK;
}

BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *context, BOOL present)
{
    ULONG profileStart, stallStart, drainStart, spin;
    volatile MGLPPCRingControlV1 *control;
    BOOL result = TRUE;
    (void)context;
    profileStart = PerfClock();
    control = FrontendPlatform.ring;
    R200PPCTimelineMark(MGLPPC_TL_BARRIER_ENTER, present ? 1UL : 0UL, 0);
    if (control) {
        static BOOL firstBarrier = TRUE;
        if (firstBarrier) {
            firstBarrier = FALSE;
            printf("RING barrier0 present=%d\n", present ? 1 : 0);
            fflush(stdout);
        }
        if (present) {
            volatile MGLPPCRingEntryV1 *entry;
            ULONG ringSlot;
            stallStart = PerfClock();
            spin = 0;
            while ((control->head - control->completed) >=
                   control->slotCount) {
                MGLPPCInvalidateLine32((APTR)&control->head);
                if (control->error || ++spin > 5000000UL) {
                    printf("RING present_wait_fail head=%lu completed=%lu "
                           "error=%lu spin=%lu slots=%lu\n",
                           (unsigned long)control->head,
                           (unsigned long)control->completed,
                           (unsigned long)control->error,
                           (unsigned long)spin,
                           (unsigned long)control->slotCount);
                    fflush(stdout);
                    result = FALSE;
                    break;
                }
            }
            {
                ULONG stalled = PerfClock() - stallStart;
                PlatformProfile.ringStallTicks += stalled;
                PlatformProfile.barrierStallTicks += stalled;
            }
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
                PlatformProfile.publishCount++;
            }
        }
        /* A ring present is ordered after this frame's submits and before the
         * next frame's submits. Let the PPC build ahead while the host drains
         * the fence and blits; explicit barriers still wait below. */
        if (result && !present) {
            drainStart = PerfClock();
            spin = 0;
            R200PPCTimelineMark(MGLPPC_TL_DRAIN_ENTER, control->head, 0);
            while (control->completed != control->head) {
                MGLPPCInvalidateLine32((APTR)&control->completed);
                if (control->error || ++spin > 5000000UL) {
                    result = FALSE;
                    printf("RING barrier=FAIL drain head=%lu "
                           "completed=%lu err=%lu spin=%lu\n",
                           (unsigned long)control->head,
                           (unsigned long)control->completed,
                           (unsigned long)control->error,
                           (unsigned long)spin);
                    fflush(stdout);
                    break;
                }
            }
            R200PPCTimelineMark(MGLPPC_TL_DRAIN_EXIT, control->completed,
                                spin);
            PlatformProfile.drainTicks += PerfClock() - drainStart;
        }
    } else {
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[3] =
            present ? MGLPPC_SEMANTIC_FLAG_PRESENT : 0;
        FrontendPlatform.control.value[5] = FrontendPlatform.epoch;
        result = PlatformCall(MGLPPC_CMD_SEMANTIC_BARRIER,
                              FrontendPlatform.contextId);
    }
    R200PPCTimelineMark(MGLPPC_TL_BARRIER_EXIT, result ? 1UL : 0UL,
                        control ? control->completed : 0);
    PlatformProfile.barrierCount++;
    PlatformProfile.barrierTicks += PerfClock() - profileStart;
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
    (void)vertexBytes;
    if (!context || !context->segmentPoolCount || !control)
        return FALSE;
    next = (context->segmentPoolNext + 1UL) % context->segmentPoolCount;
    /* Host consumption releases ring slots, but only a completed later fence
     * retires the GPU's ordered reads from a vertex segment. */
    R200PPCTimelineMark(MGLPPC_TL_STALL_ENTER, next,
                        FrontendPlatform.segmentLastEntry[next]);
    while (control->retired < FrontendPlatform.segmentLastEntry[next]) {
        MGLPPCInvalidateLine32((APTR)&control->retired);
        if (control->error || ++spin > 5000000UL) {
            R200PPCTimelineMark(MGLPPC_TL_STALL_EXIT, next, spin);
            return FALSE;
        }
    }
    R200PPCTimelineMark(MGLPPC_TL_STALL_EXIT, next, spin);
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
    printf("CLIENT create sent\n");
    fflush(stdout);
    if (!PlatformCall(MGLPPC_CMD_CREATE_CONTEXT, 0) ||
        !FrontendPlatform.control.contextId ||
        FrontendPlatform.control.value[1] != (ULONG)width ||
        FrontendPlatform.control.value[2] != (ULONG)height ||
        !FrontendPlatform.control.value[3] ||
        !FrontendPlatform.control.value[4])
        return NULL;

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

    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.ring = NULL;
    FrontendPlatform.ringPostSerial = 0;
    FrontendPlatform.hostEventsConfirmed = FALSE;
    if (FrontendPlatform.capabilities & MGLPPC_CAP_SEMANTIC_RING) {
        MGLPPCMessage *registerMessage =
            (MGLPPCMessage *)FrontendPlatform.registerMessageWords;
        BOOL ringOk = TRUE;
        printf("RING reg=begin slots=%lu dwords=%lu\n",
               (unsigned long)MGLPPC_RING_SLOTS,
               (unsigned long)MGLPPC_SEMANTIC_SLOT_DWORDS);
        fflush(stdout);
        for (index = 0; index < MGLPPC_RING_SLOTS; ++index) {
            FrontendPlatform.ringSlots[index] =
                AllocVec(MGLPPC_SEMANTIC_SLOT_DWORDS * sizeof(ULONG), 0UL);
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
            if (FrontendPlatform.capabilities & MGLPPC_CAP_HOST_EVENTS)
                registerMessage->value[3] =
                    MGLPPC_RING_REGISTER_HOST_EVENTS;
            registerMessage->payloadWords = MGLPPC_RING_SLOTS;
            for (index = 0; index < MGLPPC_RING_SLOTS; ++index)
                registerMessage->payload[index] =
                    (ULONG)FrontendPlatform.ringSlots[index];
            ringOk =
                MGLPPCTransportCall(&FrontendPlatform.transport,
                                    registerMessage) &&
                registerMessage->status == MGLPPC_STATUS_OK &&
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
                if (vertexCount > 4UL)
                    vertexCount = 4UL;
                for (index = 0; index < vertexCount; ++index) {
                    proofContext.segmentPool[index].Size =
                        RADEON3D_SEGMENT_V1_SIZE;
                    proofContext.segmentPool[index].Version =
                        RADEON3D_SEGMENT_VERSION;
                    proofContext.segmentPool[index].CpuAddress =
                        (APTR)registerMessage->payload[index * 3UL];
                    proofContext.segmentPool[index].Bytes =
                        registerMessage->payload[index * 3UL + 1UL];
                    proofContext.segmentPool[index].Id =
                        registerMessage->payload[index * 3UL + 2UL];
                    proofContext.segmentPool[index].GpuAddress = 0;
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
                printf("RING reg=ok ctrl=%08lx epoch=%lu vseg=%lu\n",
                       (unsigned long)FrontendPlatform.ring,
                       (unsigned long)FrontendPlatform.epoch,
                       (unsigned long)vertexCount);
                fflush(stdout);
            }
        }
        if (!ringOk) {
            printf("RING reg=fallback\n");
            fflush(stdout);
            for (index = 0; index < MGLPPC_RING_SLOTS; ++index) {
                if (FrontendPlatform.ringSlots[index]) {
                    FreeVec(FrontendPlatform.ringSlots[index]);
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
    return &proofContext;
}

void *R200PPCDispatchCreateContextFromID(GLint id, GLint *width, GLint *height)
{
    (void)id;
    (void)width;
    (void)height;
    return NULL;
}

void R200PPCDispatchDeleteContext(void)
{
    ULONG index;
    if (!R200PPCContext)
        return;
#if defined(R200_FRONTEND_LIBRARY)
    MGLPPCSemanticReplayFree();
#endif
    if (!proofContext.insideBegin) {
        (void)FlushScreenVertices(&proofContext);
        (void)SubmitAndWait(&proofContext);
        (void)R200PPCPlatformBarrier(&proofContext, FALSE);
    }
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index)
        (void)R200PPCPlatformWaitSlot(&proofContext, index);
    if (FrontendPlatform.ring) {
        /* Drained above; the host frees the segment at unregister. */
        FrontendPlatform.ring = NULL;
    }
    if (FrontendPlatform.registered) {
        memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
        FrontendPlatform.control.value[5] = FrontendPlatform.epoch;
        (void)PlatformCall(MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS,
                           FrontendPlatform.contextId);
    }
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    (void)PlatformCall(MGLPPC_CMD_DESTROY_CONTEXT,
                       FrontendPlatform.contextId);
    for (index = 0; index < MGLPPC_RING_SLOTS; ++index) {
        if (FrontendPlatform.ringSlots[index]) {
            FreeVec(FrontendPlatform.ringSlots[index]);
            FrontendPlatform.ringSlots[index] = NULL;
        }
    }
    R200PPCContext = NULL;
    FrontendPlatform.registered = FALSE;
    FrontendPlatform.contextId = 0;
    FrontendPlatform.epoch = 0;
    FrontendPlatform.windowHandle = NULL;
    memset(&proofContext, 0, sizeof(proofContext));
}

void R200PPCPlatformClose(void)
{
    if (!FrontendPlatform.opened)
        return;
    R200PPCDispatchDeleteContext();
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    (void)PlatformCall(MGLPPC_CMD_SHUTDOWN, 0);
    MGLPPCTransportClose(&FrontendPlatform.transport);
    memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
}

void R200PPCDispatchSwitchDisplay(void)
{
    Lib_mglSwitchDisplay();
    if (R200PPCContext && !R200PPCContext->insideBegin &&
        R200PPCContext->error) {
        printf("RING switch_error=%lu\n",
               (unsigned long)R200PPCContext->error);
        fflush(stdout);
    }
}
void *R200PPCDispatchGetWindowHandle(void)
{ return FrontendPlatform.windowHandle; }
GLboolean R200PPCDispatchLockDisplay(void)
{ return R200PPCContext ? GL_TRUE : GL_FALSE; }
void R200PPCDispatchUnlockDisplay(void) {}
GLboolean R200PPCDispatchLockBack(MGLLockInfo *info)
{ (void)info; SetError(R200PPCContext, GL_INVALID_OPERATION); return GL_FALSE; }
void R200PPCDispatchChoosePixelDepth(int depth)
{ if (!R200PPCContext) FrontendPlatform.pixelDepth = depth; }

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
    if (!context || context->insideBegin || !FlushScreenVertices(context) ||
        !SubmitAndWait(context) || !R200PPCPlatformBarrier(context, FALSE)) {
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
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
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = target;
    FrontendPlatform.control.value[1] = texture;
    if (!PlatformCall(MGLPPC_CMD_BIND_TEXTURE, FrontendPlatform.contextId)) {
        FrontendSetTransportError(context);
        return;
    }
    Lib_glBindTexture(target, texture);
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
    if (width <= 0 || height <= 0 || type != GL_UNSIGNED_BYTE) return 0;
    if (format == GL_RGB) components = 3;
    else if (format == GL_RGBA) components = 4;
    else return 0;
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
    ULONG chunkStart;
    BOOL chunkOk;
    if (!StateChangeAllowed(context)) return;
    if (!(FrontendPlatform.capabilities & MGLPPC_CAP_SEMANTIC_TEXTURE_V1)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (format != GL_RGB && format != GL_RGBA) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (type != GL_UNSIGNED_BYTE) {
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
    if (context->activeTexture != 0 || context->boundTexture[0] == 0) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!FrontendTextureBarrier(context)) return;
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    FrontendPlatform.control.value[0] = target;
    FrontendPlatform.control.value[1] = (ULONG)level;
    FrontendPlatform.control.value[2] = (ULONG)internalFormat;
    FrontendPlatform.control.value[3] = (ULONG)width;
    FrontendPlatform.control.value[4] = (ULONG)height;
    FrontendPlatform.control.value[5] = (ULONG)border;
    FrontendPlatform.control.value[6] = format;
    FrontendPlatform.control.value[7] = type;
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
        chunkStart = PerfClock();
        chunkOk = MGLPPCTransportCall(&FrontendPlatform.transport,
                                      chunkMessage);
        PlatformProfile.chunkTicks += PerfClock() - chunkStart;
        PlatformProfile.chunkCount++;
        if (!chunkOk || chunkMessage->status != MGLPPC_STATUS_OK) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        offset += bytes;
    }
    memset(&FrontendPlatform.control, 0, sizeof(FrontendPlatform.control));
    if (!PlatformCall(MGLPPC_CMD_TEXTURE_UPLOAD_END,
                      FrontendPlatform.contextId)) {
        FrontendSetTransportError(context);
        return;
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

void R200PPCDispatchTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLsizei width, GLsizei height,
                                  GLenum format, GLenum type,
                                  const GLvoid *pixels)
{
    (void)target; (void)level; (void)xoffset; (void)yoffset;
    (void)width; (void)height; (void)format; (void)type; (void)pixels;
    SetError(CurrentContext(), GL_INVALID_OPERATION);
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
            FrontendSetTransportError(context);
            return;
        }
        for (index = 0; index < chunk; ++index) {
            GLuint name = names[done + (GLsizei)index];
            struct TextureObject *texture;
            if (!name || name >= MAX_TEXTURES) continue;
            texture = &context->textures[name];
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
