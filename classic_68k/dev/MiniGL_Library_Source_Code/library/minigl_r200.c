#include "minigl_r200_internal.h"
#include <math.h>
#include "debug_output.h"

/*
 * The client-visible current-context slot (context_state_r200.c owns the
 * definition; see context_access.h for the full story). Declared here in
 * SDK-pointer form instead of including context_access.h, because that
 * header pulls in mgl/gl.h whose typedefs collide with this backend's
 * self-contained minigl_r200.h.
 */
struct GLcontext_t;
extern struct GLcontext_t *mini_CurrentContext;

#ifdef __VBCC__
#define REGARG(reg, type, name) __reg(reg) type name
#else
#define REGARG(reg, type, name) type name __asm(reg)
#endif

#define LIB_NAME "minigl.library"
#define LIB_VERSION 7
#define LIB_REVISION 0

/* Opened by MGLInit(), closed by MGLTerm(); shared by all backend modules. */
struct Library *Radeon9200Base;
struct Library *P96Base;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Device *TimerBase;

static TimeRequest_Type TimerRequest;
static struct MsgPort TimerPort;
static ULONG TimerRate;

#if R200_PPC_FRONTEND && defined(__VBCC__)
/* The WarpOS PPC frontend has no 68k timer.device context; ReadEClock would
 * be a cross-CPU bridge call inside every measured section. Sample the
 * on-chip timebase instead and calibrate it once against dos.library Delay(). */
extern void Delay(LONG ticks);
static ULONG PPCRawClock(void) = "\tmftb\tr3";
static ULONG PPCRawClockRate;

static ULONG PPCRawClockRateCalibrated(void)
{
    ULONG start, end;
    if (!PPCRawClockRate) {
        start = PPCRawClock();
        Delay(2);
        end = PPCRawClock();
        PPCRawClockRate = end > start ? (end - start) * 25UL : 1UL;
    }
    return PPCRawClockRate;
}
#endif

/*
 * minigl_library.c owns the Amiga library base, the ROMTag and the version
 * string for every V10 backend. What used to live in this backend's own
 * library base is now a private instance, brought up by MGLInit(). Its
 * lib/segList fields are unused here.
 */
static struct MiniGLR200Base R200BackendBase;
struct MiniGLR200Base *LibraryBase;
#if R200_PPC_FRONTEND
struct MiniGLR200Context *R200PPCContext;
#endif

static void InitExecList(struct List *list)
{
    list->lh_Head = (struct Node *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
}

static BOOL OpenTimer(void)
{
    struct EClockVal value;

    ZeroMemory(&TimerPort, sizeof(TimerPort));
    ZeroMemory(&TimerRequest, sizeof(TimerRequest));
    TimerPort.mp_Node.ln_Type = NT_MSGPORT;
    TimerPort.mp_Flags = PA_IGNORE;
    InitExecList(&TimerPort.mp_MsgList);
    TimerRequest.tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    TimerRequest.tr_node.io_Message.mn_ReplyPort = &TimerPort;
    TimerRequest.tr_node.io_Message.mn_Length = sizeof(TimerRequest);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK,
                   (struct IORequest *)&TimerRequest, 0))
        return FALSE;
    TimerBase = TimerRequest.tr_node.io_Device;
    TimerRate = ReadEClock(&value);
    if (!TimerRate) {
        CloseDevice((struct IORequest *)&TimerRequest);
        TimerBase = NULL;
        return FALSE;
    }
    return TRUE;
}

static ULONG ReadClock(void)
{
    struct EClockVal value;
    ReadEClock(&value);
    return value.ev_lo;
}

ULONG PerfClock(void)
{
#if !R200_PERF_TIMING
    return 0;
#elif R200_PPC_FRONTEND && defined(__VBCC__)
    return PPCRawClock();
#else
    return ReadClock();
#endif
}

ULONG PerfClockRate(void)
{
#if R200_PPC_FRONTEND && defined(__VBCC__)
    return PPCRawClockRateCalibrated();
#else
    return TimerRate;
#endif
}

void ResetPerf(struct MiniGLR200Context *context)
{
    ZeroMemory(&context->perf, sizeof(context->perf));
    context->perf.clockRate = PerfClockRate();
    context->perf.frameStartTicks = PerfClock();
}

void BumpSemanticStateSerial(struct MiniGLR200Context *context)
{
    if (++context->semanticStateSerial == 0) {
        context->semanticStateSerial = 1;
        /* Do not let a wrapped generation alias an old pending snapshot. */
        context->pendingSemanticStateSerial = 0;
    }
}

void ZeroMemory(APTR memory, ULONG bytes)
{
    /* Capture snapshots zero up to ~540 bytes per rebuild; move the bulk
     * through longword stores instead of the old byte loop. */
    UBYTE *output = memory;
    while (bytes && ((ULONG)output & 3UL)) {
        *output++ = 0;
        bytes--;
    }
    while (bytes >= 16UL) {
        ULONG *words = (ULONG *)output;
        words[0] = 0;
        words[1] = 0;
        words[2] = 0;
        words[3] = 0;
        output += 16;
        bytes -= 16;
    }
    while (bytes--)
        *output++ = 0;
}

struct MiniGLR200Context *CurrentContext(void)
{
#if R200_PPC_FRONTEND
    return R200PPCContext;
#else
    if (!LibraryBase || LibraryBase->owner != SysBase->ThisTask)
        return NULL;
    return LibraryBase->context;
#endif
}

void InitializeFrontendContext(struct MiniGLR200Context *context)
{
    ULONG textureIndex;

    context->viewportWidth = context->width;
    context->viewportHeight = context->height;
    context->scissorWidth = context->width;
    context->scissorHeight = context->height;
    context->pointSize = 1.0f;
    context->clearColor = 0xff000000UL;
    context->currentColor = 0xffffffffUL;
    context->currentNormalZ = 1.0f;
    context->matrixMode = GL_MODELVIEW;
    Identity(context->modelview[0]);
    Identity(context->projection[0]);
    context->depthNear = 0.0f;
    context->depthFar = 1.0f;
    context->clearDepth = 1.0f;
    context->depthFunc = GL_LESS;
    context->depthMask = TRUE;
    context->textureEnv[0] = GL_MODULATE;
    context->textureEnv[1] = GL_MODULATE;
    context->textureGenModeS[0] = context->textureGenModeS[1] = GL_SPHERE_MAP;
    context->textureGenModeT[0] = context->textureGenModeT[1] = GL_SPHERE_MAP;
    context->fogMode = GL_EXP;
    context->fogStart = 1.0f;
    context->fogEnd = 0.0f;
    context->fogDensity = 1.0f;
    context->fogColor = 0;
    context->blendSource = GL_ONE;
    context->blendDestination = GL_ZERO;
    context->alphaFunc = GL_ALWAYS;
    context->shadeModel = GL_SMOOTH;
    context->cullFaceMode = GL_BACK;
    context->frontFace = GL_CCW;
    context->drawBuffer = GL_BACK;
    context->readBuffer = GL_BACK;
    context->flatColor = context->currentColor;
    context->unpackAlignment = 4;
    context->packAlignment = 4;
    context->texMemCurrent = 0;
    context->texMemPeak = 0;
    context->syncEnabled = TRUE;
    context->polygonMode = GL_FILL;
    context->polygonOffsetUnits = 0.0f;
    context->polygonOffsetFillEnabled = FALSE;
    InitializeLightingState(context);
    context->batchSerial = 1;
    context->textureUseSerial = 1;
    context->deviceEpoch = 1;
    context->semanticStateSerial = 1;
    ResetPerf(context);
    for (textureIndex = 0; textureIndex < MAX_TEXTURES; ++textureIndex) {
        context->textures[textureIndex].minFilter = GL_NEAREST;
        context->textures[textureIndex].magFilter = GL_NEAREST;
        context->textures[textureIndex].wrapS = GL_REPEAT;
        context->textures[textureIndex].wrapT = GL_REPEAT;
    }
}

BOOL ValidPrimitive(GLenum primitive)
{
    return primitive == GL_TRIANGLES || primitive == GL_TRIANGLE_FAN ||
           primitive == GL_TRIANGLE_STRIP || primitive == GL_QUADS ||
           primitive == GL_QUAD_STRIP || primitive == GL_POLYGON ||
           primitive == GL_POINTS || primitive == GL_LINES ||
           primitive == GL_LINE_STRIP || primitive == GL_LINE_LOOP ||
           primitive == MGL_FLATFAN || primitive == MGL_FLATSTRIP;
}

struct Radeon3DSurface *TargetSurface(
    struct MiniGLR200Context *context)
{
    ULONG buffer = context->drawBuffer == GL_FRONT ? context->displayedBuffer
                                                   : context->drawingBuffer;
    return &context->surfaces[context->fullscreen ? buffer : 0];
}

struct Radeon3DSurface *ReadSourceSurface(
    struct MiniGLR200Context *context)
{
    ULONG buffer = context->readBuffer == GL_FRONT ? context->displayedBuffer
                                                    : context->drawingBuffer;
    return &context->surfaces[context->fullscreen ? buffer : 0];
}

void SetError(struct MiniGLR200Context *context, GLenum error)
{
    if (context && context->error == GL_NO_ERROR)
        context->error = error;
}


static void DestroyContext(struct MiniGLR200Context *context)
{
    ULONG index;

    if (!context)
        return;
    if (context->activeFastPath) {
        context->activeFastPath->privateData = NULL;
        context->activeFastPath->currentVertex = NULL;
    }
    if (context->fastBuffer)
        FreeMem(context->fastBuffer, context->fastBufferBytes);
    if (context->lockedVertices)
        FreeMem(context->lockedVertices,
                context->lockedCount * sizeof(struct Vertex));
    FreeMtexQueue(context);
    if (context->indexCacheVerts)
        FreeMem(context->indexCacheVerts,
                context->indexCacheSize * sizeof(struct Vertex));
    if (context->indexCacheStamp)
        FreeMem(context->indexCacheStamp,
                context->indexCacheSize * sizeof(ULONG));
    (void)FlushScreenVertices(context);
    (void)SubmitAndWait(context);
    /* Everything retired: deferred texture surfaces can be freed before
     * the generic texture release pass below runs. */
    ReapDeferredSurfaces(context);
    RestorePrimaryBuffer(context);
    if (context->device) {
        for (index = 0; index < R200_SCREEN_BUFFERS; ++index) {
            if (context->surfaces[index].Handle)
                Radeon3DReleaseSurface(context->device,
                                       &context->surfaces[index]);
        }
        if (context->depthSurface.Handle)
            Radeon3DReleaseSurface(context->device, &context->depthSurface);
        for (index = 0; index < MAX_TEXTURES; ++index)
            if (context->textures[index].surface.Handle)
                Radeon3DReleaseSurface(context->device,
                                       &context->textures[index].surface);
        while (context->segmentLeaseCount)
            Radeon3DFreeSegment(context->device,
                                context->segmentLeases
                                    [--context->segmentLeaseCount].id);
    }
    if (context->window)
        CloseWindow(context->window);
    if (context->screenLocked)
        UnlockPubScreen(NULL, context->screen);
    if (context->bitmap)
        p96FreeBitMap(context->bitmap);
    if (context->depthBitmap)
        p96FreeBitMap(context->depthBitmap);
    for (index = 0; index < MAX_TEXTURES; ++index) {
        if (context->textures[index].bitmap)
            p96FreeBitMap(context->textures[index].bitmap);
        if (context->textures[index].image)
            FreeMem(context->textures[index].image,
                    context->textures[index].imageBytes);
        if (context->textures[index].dirtyRects)
            FreeMem(context->textures[index].dirtyRects,
                    context->textures[index].levelCount *
                        sizeof(struct TextureDirtyRect));
    }
    for (index = R200_SCREEN_BUFFERS; index-- > 0;) {
        if (context->screenBuffers[index]) {
            context->screenBuffers[index]->sb_DBufInfo
                ->dbi_SafeMessage.mn_ReplyPort = NULL;
            context->screenBuffers[index]->sb_DBufInfo
                ->dbi_DispMessage.mn_ReplyPort = NULL;
            FreeScreenBuffer(context->screen, context->screenBuffers[index]);
        }
    }
    if (context->screenOwned)
        CloseScreen(context->screen);
    DrainNotifications(context->safePort);
    DrainNotifications(context->displayPort);
    if (context->safePort)
        DeleteMsgPort(context->safePort);
    if (context->displayPort)
        DeleteMsgPort(context->displayPort);
    if (context->device) {
        ReleaseStreamSegment(context);
        Radeon3DClose(context->device);
    }
    FreeMem(context, sizeof(*context));
}

/*
 * Backend bring-up and teardown, with the names and signatures declared in
 * minigl_r200_backend.h. The glue below drives them; minigl.library's shared
 * skeleton calls the same pair when this backend is linked as an archive.
 * SysBase must already be valid.
 */
static BOOL BackendReady;

GLboolean MGLInit(void);
void MGLTerm(void);
BOOL MGLBackendHasContext(void);
void MGLBackendTaskClose(struct Task *task);

GLboolean MGLInit(void)
{
    if (BackendReady)
        return GL_TRUE;

    /* State that LibInit() used to set up in the backend's own library base. */
    InitSemaphore(&R200BackendBase.lock);
    R200BackendBase.owner = NULL;
    R200BackendBase.context = NULL;
    /* Classic MiniGL defaults to fullscreen. Windowed clients opt in with
     * mglChooseWindowMode(GL_TRUE) before creating the context. */
    R200BackendBase.windowMode = FALSE;
    R200BackendBase.pixelDepth = 16;
    LibraryBase = &R200BackendBase;

    StreamDiagEvent(NULL, 0x103UL, 0, 0);
    Radeon9200Base = OpenLibrary("Radeon9200.chip", RADEON3D_LIBRARY_VERSION);
    StreamDiagEvent(NULL, 0x104UL, Radeon9200Base != NULL, 0);
    P96Base = OpenLibrary("Picasso96API.library", 2);
    StreamDiagEvent(NULL, 0x105UL, P96Base != NULL, 0);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    (void)OpenTimer();
    StreamDiagEvent(NULL, 0x106UL, GfxBase != NULL,
                    IntuitionBase != NULL);
    if (!Radeon9200Base || !P96Base || !GfxBase || !IntuitionBase) {
        MGLTerm();
        return GL_FALSE;
    }
    BackendReady = TRUE;
    return GL_TRUE;
}

void MGLTerm(void)
{
    if (TimerBase) {
        CloseDevice((struct IORequest *)&TimerRequest);
        TimerBase = NULL;
        TimerRate = 0;
    }
    ReleaseFriendBitMap();
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (P96Base)
        CloseLibrary(P96Base);
    if (Radeon9200Base)
        CloseLibrary(Radeon9200Base);
    IntuitionBase = NULL;
    GfxBase = NULL;
    P96Base = NULL;
    Radeon9200Base = NULL;
    LibraryBase = NULL;
    BackendReady = FALSE;
}

BOOL MGLBackendHasContext(void)
{
    return LibraryBase && LibraryBase->context ? TRUE : FALSE;
}

void MGLBackendTaskClose(struct Task *task)
{
    if (!LibraryBase)
        return;
    ObtainSemaphore(&LibraryBase->lock);
    if (LibraryBase->owner == task) {
        DestroyContext(LibraryBase->context);
        LibraryBase->context = NULL;
        LibraryBase->owner = NULL;
    }
    ReleaseSemaphore(&LibraryBase->lock);
}

/*
 * The Amiga library lifecycle (LibInit/LibOpen/LibClose/LibExpunge, the vector
 * table and the ROMTag) lives in minigl_library.c for every V10 backend. That
 * skeleton calls MGLInit()/MGLTerm() above and the two hooks below.
 */

ULONG LibUnsupported(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    SetError(context, GL_INVALID_OPERATION);
    return 0;
}

const GLubyte *Lib_glGetString(GLenum name)
{
    static const GLubyte vendor[] = "MiniGL_R200";
    static const GLubyte renderer[] = "Radeon3D semantic R200";
    static const GLubyte version[] = "1.1 MiniGL_R200 phase6";
    static const GLubyte extensions[] =
        "GL_ARB_multitexture GL_MGL_performance_counters";
    switch (name) {
    case GL_VENDOR: return vendor;
    case GL_RENDERER: return renderer;
    case GL_VERSION: return version;
    case GL_EXTENSIONS: return extensions;
    default: SetError(CurrentContext(), GL_INVALID_ENUM); return NULL;
    }
}

static void *CreateContext(int width, int height, ULONG requestedMode,
                           BOOL modeSpecified)
{
    struct MiniGLR200Context *context;
    struct Radeon3DInfo info;
    struct Task *task = FindTask(NULL);

    if (!LibraryBase || width < 64 || height < 64 || width > 1920 ||
        height > 1200)
        return NULL;
    ObtainSemaphore(&LibraryBase->lock);
    if (LibraryBase->owner && LibraryBase->owner != task) {
        ReleaseSemaphore(&LibraryBase->lock);
        return NULL;
    }
    if (LibraryBase->context) {
        ReleaseSemaphore(&LibraryBase->lock);
        return LibraryBase->context;
    }
    if (!SurfaceFormatForDepth(LibraryBase->pixelDepth) ||
        (LibraryBase->pixelDepth == 8UL && LibraryBase->windowMode &&
         !modeSpecified)) {
        ReleaseSemaphore(&LibraryBase->lock);
        return NULL;
    }
    StreamDiagEvent(NULL, 0x110UL, (ULONG)width, (ULONG)height);
    context = AllocMem(sizeof(*context), MEMF_PUBLIC | MEMF_CLEAR);
    if (!context) {
        ReleaseSemaphore(&LibraryBase->lock);
        return NULL;
    }
    StreamDiagEvent(context, 0x111UL, sizeof(*context),
                    LibraryBase->pixelDepth);
    /* Fixed-capacity index cache sized for the whole GLushort/BYTE index
     * space: indexed Alias-style meshes re-reference vertices heavily and
     * the old grow-on-demand scheme paid a full max-index scan on every
     * draw call just to size the table. Four megabytes of fast RAM is
     * cheap next to that per-draw scan; allocation failure only disables
     * the cache (DrawElements then fetches every index directly). */
    context->indexCacheVerts = AllocMem(65536UL * sizeof(struct Vertex),
                                        MEMF_PUBLIC | MEMF_CLEAR);
    context->indexCacheStamp = AllocMem(65536UL * sizeof(ULONG),
                                        MEMF_PUBLIC | MEMF_CLEAR);
    if (context->indexCacheVerts && context->indexCacheStamp)
        context->indexCacheSize = 65536UL;
    else {
        if (context->indexCacheVerts)
            FreeMem(context->indexCacheVerts,
                    65536UL * sizeof(struct Vertex));
        if (context->indexCacheStamp)
            FreeMem(context->indexCacheStamp, 65536UL * sizeof(ULONG));
        context->indexCacheVerts = NULL;
        context->indexCacheStamp = NULL;
        context->indexCacheSize = 0;
    }
    ZeroMemory(&info, sizeof(info));
    info.Size = sizeof(info);
    context->device = Radeon3DOpen(RADEON3D_IFACE_VERSION, &info);
    info.Caps = Radeon3DRecordCaps(info.Caps);
    StreamDiagEvent(context, 0x112UL, context->device != NULL, info.Caps);
    context->radeonCaps = info.Caps;
    context->radeonGeneration = info.Generation;
    context->compactTclVertex =
        (info.Caps & RADEON3D_CAP_COMPACT_TCL_VERTEX) != 0;
    context->batchDwords = info.MaxBatchDwords;
    if (!context->batchDwords || context->batchDwords > MAX_RECORD_DWORDS)
        context->batchDwords = MAX_RECORD_DWORDS;
    context->pixelDepth = LibraryBase->pixelDepth;
    context->colorFormat = SurfaceFormatForDepth(context->pixelDepth);
    if (!context->device ||
        (info.Caps & REQUIRED_CAPS) != REQUIRED_CAPS ||
        (context->pixelDepth != 16UL &&
         !(info.Caps & RADEON3D_CAP_COLOR_TARGET_FORMATS))) {
        dbg_puts("[mgl] ctxfail=device\n");
        goto fail;
    }
    context->fullscreen = modeSpecified || !LibraryBase->windowMode;
    dbg_puts("[mgl] ctx fullscreen=");
    dbg_u32(context->fullscreen);
    dbg_puts(" depth=");
    dbg_u32(context->pixelDepth);
    dbg_puts(" mode=");
    dbg_u32(requestedMode);
    dbg_newline();
    StreamDiagEvent(context, 0x113UL, context->fullscreen,
                    context->pixelDepth);
    if (context->fullscreen) {
        if (!CreateFullscreenTarget(context, (ULONG)width, (ULONG)height,
                                    requestedMode, modeSpecified))
            goto fail;
    } else if (!CreateWindowTarget(context, (ULONG)width, (ULONG)height)) {
        goto fail;
    }
    StreamDiagEvent(context, 0x114UL, context->width, context->height);
    /* Both window and fullscreen targets get the padded, tile-aligned depth
     * storage.  Padding fullscreen used to blow out whole regions of the
     * frame; that was not the padding itself but the depth bitmap landing
     * inside a screen buffer.  A driver with an aux surface pool places depth
     * out of Picasso96's reach entirely; on older ones the render targets are
     * passed in for the placement retry to allocate around. */
    if (!AllocateDepthStorage(context->device, context->radeonCaps,
                              context->width, context->height,
                              &context->depthSurface,
                              &context->depthBitmap,
                              context->surfaces,
                              context->singleBuffer
                                  ? 1UL
                                  : (context->fullscreen
                                      ? context->screenBufferCount
                                      : 1UL)) ||
        context->depthSurface.Format != RADEON3D_FORMAT_R5G6B5PC ||
        context->depthSurface.Width < context->width ||
        context->depthSurface.Height != context->height) {
        dbg_puts("[mgl] ctxfail=depth\n");
        goto fail;
    }
    StreamDiagEvent(context, 0x115UL, context->depthSurface.GpuAddress,
                    context->depthSurface.Format);
    context->streamSegments =
        (info.Caps & RADEON3D_CAP_STREAM_SEGMENTS) != 0;
    if (context->streamSegments && !EnsureStreamSegment(context))
        context->streamSegments = FALSE;
    InitializeFrontendContext(context);
    LibraryBase->owner = task;
    LibraryBase->context = context;
    ReleaseSemaphore(&LibraryBase->lock);
    return context;

fail:
    DestroyContext(context);
    ReleaseSemaphore(&LibraryBase->lock);
    return NULL;
}

void *Lib_mglCreateContext(int offx, int offy, int width, int height)
{
    (void)offx;
    (void)offy;
    return CreateContext(width, height, 0, FALSE);
}

void *Lib_mglCreateContextFromID(GLint id, GLint *width, GLint *height)
{
    struct MiniGLR200Context *context;

    if (!width || !height || id == 0)
        return NULL;
    context = CreateContext(640, 480, (ULONG)id, TRUE);
    if (context) {
        *width = (GLint)context->width;
        *height = (GLint)context->height;
    }
    return context;
}

void Lib_mglDeleteContext(void)
{
    if (!LibraryBase)
        return;
    ObtainSemaphore(&LibraryBase->lock);
    if (LibraryBase->owner == FindTask(NULL)) {
        DestroyContext(LibraryBase->context);
#if MGL_PRESENT_DIAG
void R200PresentDiagReport(void);
#endif

        LibraryBase->context = NULL;
        LibraryBase->owner = NULL;
#if MGL_PRESENT_DIAG
    R200PresentDiagReport();
#endif
    }
    ReleaseSemaphore(&LibraryBase->lock);
}

void Lib_mglExit(void) { Lib_mglDeleteContext(); }

/*
 * V19-parity mglEmergencyCloseScreen, with safe shared-library semantics
 * (see plan.md): V19's version tore down only the Intuition objects and
 * left a half-live context behind, which a shared library cannot allow.
 * The R200 implementation is terminal for the current context — it runs
 * the full context teardown (bounded retirement, all surfaces, windows,
 * screens, device) under the library lock, clears the dispatch-visible
 * current context, and leaves the backend ready for MiniGLClose() or a
 * fresh mglCreateContext(). Subsequent mglDeleteContext() calls are
 * no-ops. Safe to call with no context (idempotent no-op), matching V19's
 * null-context acceptance.
 */
void Lib_mglEmergencyCloseScreen(void)
{
    if (!LibraryBase)
        return;
    ObtainSemaphore(&LibraryBase->lock);
    if (LibraryBase->owner == FindTask(NULL)) {
        DestroyContext(LibraryBase->context);
        LibraryBase->context = NULL;
        LibraryBase->owner = NULL;
        mini_CurrentContext = (struct GLcontext_t *)0;
    }
    ReleaseSemaphore(&LibraryBase->lock);
}

void *Lib_mglGetWindowHandle(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    return context ? context->window : NULL;
}

void *Lib_mglGetInputWindowHandle(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    return context ? context->window : NULL;
}

/*
 * Widens the window's IDCMP mask to cover every handler currently
 * registered, on top of the mask captured at window-open time
 * (IDCMP_CLOSEWINDOW windowed, IDCMP_RAWKEY fullscreen). Called whenever a
 * handler is installed so the validated open-time masks are never touched.
 */
static void UpdateInputIDCMP(struct MiniGLR200Context *context)
{
    ULONG mask = context->baseIDCMP;
    if (context->keyFunc) mask |= IDCMP_VANILLAKEY;
    if (context->specialFunc) mask |= IDCMP_RAWKEY;
    if (context->mouseFunc) mask |= IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    if (context->window)
        ModifyIDCMP(context->window, mask);
}

void Lib_mglKeyFunc(KeyHandlerFn k)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    context->keyFunc = k;
    UpdateInputIDCMP(context);
}

void Lib_mglSpecialFunc(SpecialHandlerFn s)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    context->specialFunc = s;
    UpdateInputIDCMP(context);
}

void Lib_mglMouseFunc(MouseHandlerFn m)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    context->mouseFunc = m;
    UpdateInputIDCMP(context);
}

void Lib_mglIdleFunc(IdleFn i)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    context->idleFunc = i;
}

/*
 * Amiga raw key codes for the function and cursor keys, decoded only from
 * IDCMP_RAWKEY (never IDCMP_VANILLAKEY: these keys have no ASCII rendering
 * and Intuition never sends VANILLAKEY for them, so there is no double
 * delivery to both the key and special handlers).
 */
static BOOL DecodeSpecialKey(UWORD code, MGLspecial *out)
{
    switch (code) {
    case 0x4C: *out = MGLKEY_CUP; return TRUE;
    case 0x4D: *out = MGLKEY_CDOWN; return TRUE;
    case 0x4F: *out = MGLKEY_CLEFT; return TRUE;
    case 0x4E: *out = MGLKEY_CRIGHT; return TRUE;
    case 0x50: *out = MGLKEY_F1; return TRUE;
    case 0x51: *out = MGLKEY_F2; return TRUE;
    case 0x52: *out = MGLKEY_F3; return TRUE;
    case 0x53: *out = MGLKEY_F4; return TRUE;
    case 0x54: *out = MGLKEY_F5; return TRUE;
    case 0x55: *out = MGLKEY_F6; return TRUE;
    case 0x56: *out = MGLKEY_F7; return TRUE;
    case 0x57: *out = MGLKEY_F8; return TRUE;
    case 0x58: *out = MGLKEY_F9; return TRUE;
    case 0x59: *out = MGLKEY_F10; return TRUE;
    default: return FALSE;
    }
}

static void DispatchInputMessage(struct MiniGLR200Context *context,
                                 struct IntuiMessage *message, BOOL *exitLoop)
{
    ULONG class = message->Class;
    UWORD code = message->Code;
    WORD mouseX = message->MouseX;
    WORD mouseY = message->MouseY;
    ReplyMsg((struct Message *)message);
    switch (class) {
    case IDCMP_CLOSEWINDOW:
        *exitLoop = TRUE;
        break;
    case IDCMP_VANILLAKEY:
        if (context->keyFunc)
            context->keyFunc((char)code);
        break;
    case IDCMP_RAWKEY: {
        MGLspecial special;
        if (!(code & 0x80) && context->specialFunc &&
            DecodeSpecialKey(code, &special))
            context->specialFunc(special);
        break;
    }
    case IDCMP_MOUSEBUTTONS:
        if (code == SELECTDOWN) context->mouseButtonState |= 1UL;
        else if (code == SELECTUP) context->mouseButtonState &= ~1UL;
        else if (code == MENUDOWN) context->mouseButtonState |= 2UL;
        else if (code == MENUUP) context->mouseButtonState &= ~2UL;
        if (context->mouseFunc)
            context->mouseFunc(mouseX, mouseY, context->mouseButtonState);
        break;
    case IDCMP_MOUSEMOVE:
        if (context->mouseFunc)
            context->mouseFunc(mouseX, mouseY, context->mouseButtonState);
        break;
    default:
        break;
    }
}

void Lib_mglMainLoop(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct IntuiMessage *message;
    BOOL exitLoop = FALSE;
    if (!context || !context->window) return;
    for (;;) {
        if (!context->idleFunc)
            Wait(1UL << context->window->UserPort->mp_SigBit);
        while ((message = (struct IntuiMessage *)
                    GetMsg(context->window->UserPort))) {
            DispatchInputMessage(context, message, &exitLoop);
            if (exitLoop) return;
        }
        if (context->idleFunc)
            context->idleFunc();
    }
}

void Lib_mglSetPointer(void)
{
    static const UWORD blankPointerData[2] = {0, 0};
    struct MiniGLR200Context *context = CurrentContext();
    if (!context || !context->window) return;
    SetPointer(context->window, (UWORD *)blankPointerData, 1, 16, 0, 0);
}

void Lib_mglClearPointer(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context || !context->window) return;
    ClearPointer(context->window);
}
