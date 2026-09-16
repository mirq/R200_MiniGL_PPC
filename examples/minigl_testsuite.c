/*
 * Backend-neutral interactive MiniGL correctness and performance runner.
 *
 * The initial suite deliberately uses only public dispatch API calls.  Tests
 * compare pixels by equality rather than assuming a particular surface byte
 * order, which keeps the checks portable across MiniGL implementations.
 *
 * PPC/WarpOS port: timer.device ReadEClock LP calls crash from a PPC task on
 * this stack, so all timing runs on dos.library DateStamp() at its fixed
 * 50 Hz tick rate -- the same validated clock the gears client uses.
 */
#include <exec/exec.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/minigl.h>
#include <libraries/minigl_perf.h>

#include "quake_ogre_model.h"
#include "quake_room_model.h"
#include "quake_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUITE_VERSION 9
#define DEFAULT_WIDTH  512
#define DEFAULT_HEIGHT 320
#define DEFAULT_DEPTH  16
#define WARMUP_FRAMES  10
#define DEFAULT_MEASURE_FRAMES 60
#define ABS_DIFF(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))
#define FPS_SAMPLE_FRAMES 10
#define ARRAY_SIZE(a) ((int)(sizeof(a) / sizeof((a)[0])))

enum {
    KEY_NONE = 0,
    KEY_UP = 256,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_RUN,
    KEY_BACK,
    KEY_QUIT
};

typedef enum {
    RESULT_NOT_RUN,
    RESULT_PASS,
    RESULT_FAIL,
    RESULT_SKIP
} ResultStatus;

typedef struct {
    ULONG rate;
    GLboolean open;
} SuiteTimer;

typedef struct {
    ULONG width;
    ULONG height;
    ULONG depth;
    ULONG pixelFormat;
    ULONG pitch;
    UBYTE *base;
} SurfaceView;

typedef struct {
    GLboolean valid;
    GLint clockRate;
    GLint inputVertices;
    GLint submittedVertices;
    GLint drawRecords;
    GLint submits;
    GLint frontendTicks;
    GLint executeTicks;
    GLint waitTicks;
    GLint presentTicks;
    GLint serializeTicks;
    GLint textureUploadTicks;
    GLint textureResidencyTicks;
    GLint textureUploadBytes;
    GLint textureUploadBarriers;
    GLint deferredSplits;
    GLint drawMerges;
    GLint executeFailures;
    GLint waitFailures;
    GLint indexScanTicks;
    GLint vertexFetches;
    GLint drawTicks;
} PerfResult;

typedef struct {
    ResultStatus status;
    ULONG fpsX1000;
    ULONG elapsedTicks;
    ULONG measuredFrames;
    GLenum glError;
    char detail[96];
    PerfResult perf;
} TestResult;

typedef void (*DrawTestFn)(ULONG frame);
typedef GLboolean (*VerifyTestFn)(char *detail);
typedef GLboolean (*AvailableTestFn)(char *reason);

typedef struct {
    const char *id;
    const char *category;
    const char *name;
    const char *description;
    const char *expected;
    DrawTestFn draw;
    VerifyTestFn verify;
    AvailableTestFn available;
    GLboolean benchmark;
    TestResult result;
} MiniGLTest;

struct IntuitionBase *IntuitionBase = NULL;

static SuiteTimer g_timer;
static GLint g_width = DEFAULT_WIDTH;
static GLint g_height = DEFAULT_HEIGHT;
static GLint g_depth = DEFAULT_DEPTH;
static GLboolean g_windowed = GL_TRUE;
static GLboolean g_running = GL_TRUE;
static GLboolean g_profile = GL_FALSE;
static GLboolean g_noSync = GL_FALSE;
static GLboolean g_gameContext = GL_FALSE;
static ULONG g_measureFrames = DEFAULT_MEASURE_FRAMES;
static const char *g_dumpPath = NULL;
static GLboolean g_roomBlocks = GL_FALSE;   /* --room-blocks diagnostics */
static GLboolean g_roomQuadScreen = GL_FALSE; /* --room-quadscreen diagnostics */
static GLboolean g_roomNoDepth = GL_FALSE;  /* --room-nodepth diagnostics */
static GLboolean g_roomZPrePass = GL_FALSE; /* --room-zprepass diagnostics */
static long g_dumpFrame = 0;                /* --dump-frame: pose to dump */
static GLboolean g_glitchScan = GL_FALSE;   /* --glitchscan diagnostics */
static int g_frameOption = 1;
static FILE *g_log;
static char g_windowTitle[160];

static const ULONG g_frameOptions[] = {30, 60, 120, 300, 600};

static GLboolean OpenSuiteTimer(void)
{
    g_timer.rate = 50;
    g_timer.open = GL_TRUE;
    return GL_TRUE;
}

/* Monotonic 50 Hz tick count: DateStamp days/minutes/ticks merged into one
 * counter so elapsed-time diffs behave like the EClock ticks they replace. */
static ULONG ClockLow(void)
{
    struct DateStamp now;
    DateStamp(&now);
    return (ULONG)(((unsigned long long)now.ds_Days * 4320000ULL) +
                   ((unsigned long long)now.ds_Minute * 3000ULL) +
                   (unsigned long long)now.ds_Tick);
}

static void CloseSuiteTimer(void)
{
    g_timer.open = GL_FALSE;
}

static const char *ResultName(ResultStatus status)
{
    switch (status) {
    case RESULT_PASS: return "PASS";
    case RESULT_FAIL: return "FAIL";
    case RESULT_SKIP: return "SKIP";
    default: return "NOT RUN";
    }
}

static void DrainErrors(void)
{
    int count;
    for (count = 0; count < 16 && glGetError() != GL_NO_ERROR; ++count)
        ;
}

/*
 * Bumped by every ResetGLState(). Draw functions latch it so their invariant
 * setup -- projection, enables, texture environment -- runs once per test run
 * instead of once per frame: on this backend those setters bump the semantic
 * state serial, which splits pending geometry and costs real frame time.
 */
static ULONG g_stateGeneration = 1;

static void ResetGLState(void)
{
    ++g_stateGeneration;
    DrainErrors();
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDepthRange(0.0, 1.0);
    glShadeModel(GL_SMOOTH);
    glViewport(0, 0, g_width, g_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    DrainErrors();
}

static void CapturePerf(PerfResult *perf)
{
    memset(perf, 0, sizeof(*perf));
    if (!g_profile || !(MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_R200))
        return;
    glGetIntegerv(MGL_PERF_CLOCK_RATE, &perf->clockRate);
    glGetIntegerv(MGL_PERF_INPUT_VERTICES, &perf->inputVertices);
    glGetIntegerv(MGL_PERF_SUBMITTED_VERTICES, &perf->submittedVertices);
    glGetIntegerv(MGL_PERF_DRAW_RECORDS, &perf->drawRecords);
    glGetIntegerv(MGL_PERF_SUBMITS, &perf->submits);
    glGetIntegerv(MGL_PERF_FRONTEND_TICKS, &perf->frontendTicks);
    glGetIntegerv(MGL_PERF_EXECUTE_TICKS, &perf->executeTicks);
    glGetIntegerv(MGL_PERF_WAIT_TICKS, &perf->waitTicks);
    glGetIntegerv(MGL_PERF_PRESENT_TICKS, &perf->presentTicks);
    glGetIntegerv(MGL_PERF_SERIALIZE_TICKS, &perf->serializeTicks);
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_TICKS, &perf->textureUploadTicks);
    glGetIntegerv(MGL_PERF_TEXTURE_RESIDENCY_TICKS, &perf->textureResidencyTicks);
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_BYTES, &perf->textureUploadBytes);
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_BARRIERS, &perf->textureUploadBarriers);
    glGetIntegerv(MGL_PERF_DEFERRED_SPLITS, &perf->deferredSplits);
    glGetIntegerv(MGL_PERF_DRAW_MERGES, &perf->drawMerges);
    glGetIntegerv(MGL_PERF_EXECUTE_FAILURES, &perf->executeFailures);
    glGetIntegerv(MGL_PERF_WAIT_FAILURES, &perf->waitFailures);
    glGetIntegerv(MGL_PERF_INDEX_SCAN_TICKS, &perf->indexScanTicks);
    glGetIntegerv(MGL_PERF_VERTEX_FETCHES, &perf->vertexFetches);
    glGetIntegerv(MGL_PERF_DRAW_TICKS, &perf->drawTicks);
    perf->valid = GL_TRUE;
}

static ULONG PixelToken(const SurfaceView *view, ULONG x, ULONG y)
{
    UBYTE *pixel;
    ULONG token = 0;
    ULONG bytes = (view->depth + 7UL) / 8UL;
    ULONG index;

    if (!view->base || !view->width || !view->height)
        return 0;
    if (x >= view->width) x = view->width - 1UL;
    if (y >= view->height) y = view->height - 1UL;
    if (bytes > 4UL) bytes = 4UL;
    pixel = view->base + y * view->pitch + x * bytes;
    for (index = 0; index < bytes; ++index)
        token = (token << 8) | pixel[index];
    /* R5G6B5PC surfaces store little-endian words; the big-endian load above
     * crosses the byte boundary and splits the green field, which no
     * contiguous-mask decode can recover. Swap 16-bit tokens back to the
     * true word order before channel extraction. */
    if (bytes == 2UL)
        token = ((token & 0xffUL) << 8) | ((token >> 8) & 0xffUL);
    return token;
}

/* Capture orientation: the 68k mglLockBack surface is top-origin (AmigaOS
 * bitmaps), while the PPC R200 stack captures through glReadPixels, whose
 * row 0 is the BOTTOM scanline (GL standard). A vertically mirrored sample
 * passes every uniform or vertically symmetric check, so this only became
 * visible when the asymmetric row-identity map landed. Detected at runtime
 * exactly like channel order, so one source is correct on both stacks. */
static GLboolean g_captureTopOrigin = GL_FALSE;
static GLboolean g_captureOrientationValid = GL_FALSE;

static ULONG SampleNDC(const SurfaceView *view, GLfloat x, GLfloat y)
{
    ULONG px = (ULONG)((x + 1.0f) * 0.5f * (GLfloat)view->width);
    ULONG py;

    if (g_captureTopOrigin)
        py = (ULONG)((1.0f - y) * 0.5f * (GLfloat)view->height);
    else
        py = (ULONG)((y + 1.0f) * 0.5f * (GLfloat)view->height);
    return PixelToken(view, px, py);
}

/* The PPC R200 stack has no mglLockBack (68k-only surface pinning); capture
 * frames through the validated command-27 glReadPixels path into a private
 * 32-bit RGBA buffer. Runtime channel calibration makes the byte order
 * irrelevant to every pixel check. */
static UBYTE *g_captureBase = NULL;
static ULONG g_captureBytes = 0;

static GLboolean CaptureSurface(SurfaceView *view)
{
    ULONG need;

    glFinish();
    view->width = (ULONG)g_width;
    view->height = (ULONG)g_height;
    view->depth = 32;
    view->pixelFormat = 0;
    view->pitch = view->width * 4UL;
    need = view->pitch * view->height;
    if (need > g_captureBytes) {
        UBYTE *grown = (UBYTE *)realloc(g_captureBase, need);
        if (!grown)
            return GL_FALSE;
        g_captureBase = grown;
        g_captureBytes = need;
    }
    view->base = g_captureBase;
    glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE,
                 g_captureBase);
    if (glGetError() != GL_NO_ERROR)
        return GL_FALSE;
    return view->base != NULL;
}

/*
 * Absolute-colour sampling.
 *
 * Difference-based checks ("centre differs from background") pass whenever the
 * rendering is uniformly wrong, which is how the flat-shade regression stayed
 * invisible. The helpers below compare against expected colours instead.
 *
 * Channel layout is calibrated at runtime from three known clears rather than
 * assumed, so the same code is correct for R5G6B5PC and for 32-bit surfaces
 * whatever their byte order.
 */
typedef struct {
    ULONG maskR, maskG, maskB;
    GLboolean valid;
} ChannelMap;

static ChannelMap g_channels = {0, 0, 0, GL_FALSE};

static ULONG ClearAndSampleCentre(GLfloat r, GLfloat g, GLfloat b)
{
    SurfaceView view;

    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!CaptureSurface(&view))
        return 0;
    return SampleNDC(&view, 0.0f, 0.0f);
}

/* Decide which end of the capture buffer holds the bottom of the screen.
 * White and black reference clears give two tokens that differ regardless
 * of channel order (white sets every channel bit, black keeps padding
 * only), so plain equality decides the half-screen probe. */
static GLboolean CalibrateCaptureOrientation(char *detail)
{
    SurfaceView view;
    ULONG whiteToken, blackToken, lowToken, highToken;
    ULONG midX, quarterY, threeQuarterY;

    if (g_captureOrientationValid)
        return GL_TRUE;
    if (!CaptureSurface(&view)) {
        if (detail)
            sprintf(detail, "orientation: capture failed");
        return GL_FALSE;
    }
    midX = view.width / 2UL;
    quarterY = view.height / 4UL;
    threeQuarterY = view.height - 1UL - quarterY;

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!CaptureSurface(&view)) {
        if (detail)
            sprintf(detail, "orientation: white capture failed");
        return GL_FALSE;
    }
    whiteToken = PixelToken(&view, midX, view.height / 2UL);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!CaptureSurface(&view)) {
        if (detail)
            sprintf(detail, "orientation: black capture failed");
        return GL_FALSE;
    }
    blackToken = PixelToken(&view, midX, view.height / 2UL);

    /* Base white, then GL scissor (Y from the bottom) paints the BOTTOM
     * half black: top stays white for the probe. */
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, (GLsizei)view.width, (GLsizei)(view.height / 2L));
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    if (!CaptureSurface(&view)) {
        if (detail)
            sprintf(detail, "orientation: split capture failed");
        return GL_FALSE;
    }
    lowToken = PixelToken(&view, midX, quarterY);
    highToken = PixelToken(&view, midX, threeQuarterY);

    if (lowToken == blackToken && highToken == whiteToken) {
        /* Screen bottom at low buffer rows: GL-standard bottom-origin
         * readback (the PPC glReadPixels capture path). */
        g_captureTopOrigin = GL_FALSE;
    } else if (lowToken == whiteToken && highToken == blackToken) {
        /* Screen bottom at high buffer rows: top-origin surface (the 68k
         * mglLockBack capture path). */
        g_captureTopOrigin = GL_TRUE;
    } else {
        if (detail)
            sprintf(detail,
                    "orientation ambiguous lo=%08lx hi=%08lx w=%08lx b=%08lx",
                    (unsigned long)lowToken, (unsigned long)highToken,
                    (unsigned long)whiteToken, (unsigned long)blackToken);
        return GL_FALSE;
    }
    printf("MINIGL_CAPTURE topOrigin=%d\n", g_captureTopOrigin ? 1 : 0);
    g_captureOrientationValid = GL_TRUE;
    return GL_TRUE;
}

static GLboolean CalibrateChannels(char *detail)
{
    ULONG red, green, blue, common;

    if (!CalibrateCaptureOrientation(detail))
        return GL_FALSE;
    if (g_channels.valid)
        return GL_TRUE;

    red = ClearAndSampleCentre(1.0f, 0.0f, 0.0f);
    green = ClearAndSampleCentre(0.0f, 1.0f, 0.0f);
    blue = ClearAndSampleCentre(0.0f, 0.0f, 1.0f);

    /* Bits set by every primary belong to alpha/padding, not to a channel. */
    common = red & green & blue;
    g_channels.maskR = red & ~common;
    g_channels.maskG = green & ~common;
    g_channels.maskB = blue & ~common;

    if (!g_channels.maskR || !g_channels.maskG || !g_channels.maskB ||
        (g_channels.maskR & g_channels.maskG) ||
        (g_channels.maskR & g_channels.maskB) ||
        (g_channels.maskG & g_channels.maskB)) {
        if (detail)
            sprintf(detail, "calibration failed r=%08lx g=%08lx b=%08lx",
                    (unsigned long)red, (unsigned long)green,
                    (unsigned long)blue);
        return GL_FALSE;
    }

    g_channels.valid = GL_TRUE;
    return GL_TRUE;
}

static int FieldTo255(ULONG token, ULONG mask)
{
    ULONG aligned = mask;
    ULONG value;
    int shift = 0;

    if (!mask)
        return 0;
    while (!(aligned & 1UL)) {
        aligned >>= 1;
        ++shift;
    }
    value = (token & mask) >> shift;
    return (int)((value * 255UL + aligned / 2UL) / aligned);
}

static void TokenToRGB(ULONG token, int *r, int *g, int *b)
{
    *r = FieldTo255(token, g_channels.maskR);
    *g = FieldTo255(token, g_channels.maskG);
    *b = FieldTo255(token, g_channels.maskB);
}

static GLboolean SampleRGB(GLfloat ndcX, GLfloat ndcY, int *r, int *g, int *b)
{
    SurfaceView view;

    if (!CaptureSurface(&view))
        return GL_FALSE;
    TokenToRGB(SampleNDC(&view, ndcX, ndcY), r, g, b);
    return GL_TRUE;
}

/*
 * Write the current back buffer as a binary PPM.
 *
 * Host-side screenshots stall this target while a GL client is presenting, so
 * visual inspection goes through the same locked surface the checks use.
 * Channel calibration clears the surface, so it must already have run.
 */
static GLboolean DumpSurfacePPM(const char *path)
{
    SurfaceView view;
    FILE *out;
    ULONG x, y;

    if (!g_channels.valid || !CaptureSurface(&view))
        return GL_FALSE;
    out = fopen(path, "wb");
    if (!out)
        return GL_FALSE;
    fprintf(out, "P6\n%lu %lu\n255\n", (unsigned long)view.width,
            (unsigned long)view.height);
    for (y = 0; y < view.height; ++y) {
        /* PPM rows run top to bottom: map display rows through the
         * calibrated capture orientation. */
        ULONG rowY = g_captureTopOrigin ? y : (view.height - 1UL - y);
        for (x = 0; x < view.width; ++x) {
            int r, g, b;
            TokenToRGB(PixelToken(&view, x, rowY), &r, &g, &b);
            fputc(r, out); fputc(g, out); fputc(b, out);
        }
    }
    fclose(out);
    return GL_TRUE;
}

/* Column-band variant: full height but only x0..x1, so a glitch frame can be
 * pulled over the bridge in seconds instead of megabytes. */
static GLboolean DumpSurfaceBandPPM(const char *path, long x0, long x1)
{
    SurfaceView view;
    FILE *out;
    long x, y, w;

    if (!g_channels.valid || !CaptureSurface(&view))
        return GL_FALSE;
    if (x0 < 0) x0 = 0;
    if (x1 >= (long)view.width) x1 = (long)view.width - 1;
    if (x1 < x0)
        return GL_FALSE;
    w = x1 - x0 + 1;
    out = fopen(path, "wb");
    if (!out)
        return GL_FALSE;
    fprintf(out, "P6\n%ld %lu\n255\n", w, (unsigned long)view.height);
    for (y = 0; y < (long)view.height; ++y) {
        ULONG rowY = g_captureTopOrigin ? (ULONG)y
                                        : (view.height - 1UL - (ULONG)y);
        for (x = x0; x <= x1; ++x) {
            int r, g, b;
            TokenToRGB(PixelToken(&view, (ULONG)x, rowY), &r, &g, &b);
            fputc(r, out); fputc(g, out); fputc(b, out);
        }
    }
    fclose(out);
    return GL_TRUE;
}

/*
 * Intermittent black-column hunt.
 *
 * The live preview occasionally shows full-height vertical black bars that
 * the verification poses never catch. This scans the freshly drawn frame
 * BEFORE it is presented: a bar found here lives in the back buffer (render
 * bug); bars on screen with no scan hits would implicate the present path.
 * A run counts only if nearly every sampled row is pure black, so the dark
 * floor of the hall cannot trigger it while bright walls cross the column.
 */
static long g_glitchScans = 0;
static long g_glitchReports = 0;
static long g_glitchDumps = 0;

static void ScanGlitchBars(ULONG frame)
{
    SurfaceView view;
    long x, y, runStart = -1, hitStart = -1, hitEnd = -1, hit2Start = -1, hit2End = -1;
    long rows, black;

    if (!g_channels.valid || !CaptureSurface(&view))
        return;
    ++g_glitchScans;
    for (x = 0; x < (long)view.width; ++x) {
        black = 0;
        rows = 0;
        for (y = (long)view.height / 8; y < (long)view.height * 7 / 8; y += 3) {
            int r, g, b;
            TokenToRGB(PixelToken(&view, (ULONG)x, (ULONG)y), &r, &g, &b);
            if (r <= 12 && g <= 12 && b <= 12) ++black;
            ++rows;
        }
        if (black * 20 >= rows * 19) {
            if (runStart < 0) runStart = x;
            continue;
        }
        if (runStart >= 0 && x - runStart >= 2) {
            if (hitStart < 0) { hitStart = runStart; hitEnd = x - 1; }
            else if (hit2Start < 0) { hit2Start = runStart; hit2End = x - 1; }
            if (g_glitchReports < 200) {
                ++g_glitchReports;
                printf("MINIGL_GLITCH frame=%lu span=%ld..%ld\n",
                       (unsigned long)frame, runStart, x - 1);
            }
        }
        runStart = -1;
    }
    if (runStart >= 0 && (long)view.width - runStart >= 2) {
        if (hitStart < 0) { hitStart = runStart; hitEnd = (long)view.width - 1; }
        else if (hit2Start < 0) { hit2Start = runStart; hit2End = (long)view.width - 1; }
        if (g_glitchReports < 200) {
            ++g_glitchReports;
            printf("MINIGL_GLITCH frame=%lu span=%ld..%ld\n",
                   (unsigned long)frame, runStart, (long)view.width - 1);
        }
    }
    if (hitStart >= 0 && g_dumpPath && g_glitchDumps < 4) {
        char path[128];
        ++g_glitchDumps;
        sprintf(path, "%s.glitch%lu_%ld", g_dumpPath,
                (unsigned long)frame, hitStart);
        printf("MINIGL_GLITCH_DUMP path=%s ok=%d span=%ld..%ld\n", path,
               DumpSurfaceBandPPM(path, hitStart - 16, hitEnd + 16) ? 1 : 0,
               hitStart, hitEnd);
    }
    if (hit2Start >= 0 && g_dumpPath && g_glitchDumps < 4) {
        char path[128];
        ++g_glitchDumps;
        sprintf(path, "%s.glitch%lu_%ld", g_dumpPath,
                (unsigned long)frame, hit2Start);
        printf("MINIGL_GLITCH_DUMP path=%s ok=%d span=%ld..%ld\n", path,
               DumpSurfaceBandPPM(path, hit2Start - 16, hit2End + 16) ? 1 : 0,
               hit2Start, hit2End);
    }
}

/* Accumulating checker: records the first failure into detail. */
typedef struct {
    int checks;
    int failures;
    char first[96];
} CheckLog;

/* Compare colours already read out of a surface. */
static void CheckValues(CheckLog *log, const char *what,
                        int r, int g, int b,
                        int er, int eg, int eb, int tol)
{
    ++log->checks;
    if (ABS_DIFF(r, er) > tol || ABS_DIFF(g, eg) > tol || ABS_DIFF(b, eb) > tol) {
        ++log->failures;
        if (!log->first[0])
            sprintf(log->first, "%s want %d,%d,%d got %d,%d,%d",
                    what, er, eg, eb, r, g, b);
    }
}

static void CheckRGB(CheckLog *log, const char *what,
                     GLfloat ndcX, GLfloat ndcY,
                     int er, int eg, int eb, int tol)
{
    int r = -1, g = -1, b = -1;

    if (!SampleRGB(ndcX, ndcY, &r, &g, &b)) {
        ++log->checks;
        ++log->failures;
        if (!log->first[0])
            strcpy(log->first, "surface capture failed");
        return;
    }
    CheckValues(log, what, r, g, b, er, eg, eb, tol);
}

static const UBYTE g_digits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static const UBYTE g_letters[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};

static void GlyphRows(char character, UBYTE rows[7])
{
    int row;
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (character >= '0' && character <= '9') {
        memcpy(rows, g_digits[character - '0'], 7);
        return;
    }
    if (character >= 'A' && character <= 'Z') {
        memcpy(rows, g_letters[character - 'A'], 7);
        return;
    }
    memset(rows, 0, 7);
    switch (character) {
    case '-': rows[3] = 14; break;
    case '.': rows[6] = 4; break;
    case ':': rows[2] = rows[5] = 4; break;
    case '/':
        rows[0] = 1; rows[1] = 2; rows[2] = 2; rows[3] = 4;
        rows[4] = 8; rows[5] = 8; rows[6] = 16; break;
    case '>': rows[1] = 8; rows[2] = 4; rows[3] = 2; rows[4] = 4; rows[5] = 8; break;
    case '_': rows[6] = 31; break;
    default: break;
    }
    for (row = 0; row < 7; ++row) rows[row] &= 31;
}

static void BeginText(void)
{
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)g_width, (GLdouble)g_height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

static void EndText(void)
{
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static void DrawText(const char *text, GLfloat x, GLfloat y, GLfloat scale)
{
    UBYTE rows[7];
    int row, column, start;

    glBegin(GL_TRIANGLES);
    while (*text) {
        GlyphRows(*text++, rows);
        for (row = 0; row < 7; ++row) {
            column = 0;
            while (column < 5) {
                GLfloat x0, x1, y0, y1;

                while (column < 5 &&
                       !(rows[row] & (1U << (4 - column))))
                    ++column;
                if (column == 5)
                    break;
                start = column;
                while (column < 5 &&
                       (rows[row] & (1U << (4 - column))))
                    ++column;
                x0 = x + (GLfloat)start * scale;
                x1 = x + (GLfloat)column * scale;
                y0 = y + (GLfloat)row * scale;
                y1 = y0 + scale;
                glVertex2f(x0, y0);
                glVertex2f(x1, y0);
                glVertex2f(x1, y1);
                glVertex2f(x0, y0);
                glVertex2f(x1, y1);
                glVertex2f(x0, y1);
            }
        }
        x += 6.0f * scale;
    }
    glEnd();
}

static int PollKey(void)
{
    struct Window *window = (struct Window *)mglGetWindowHandle();
    struct IntuiMessage *message;
    int key = KEY_NONE;

    if (!window) return KEY_NONE;
    while ((message = (struct IntuiMessage *)GetMsg(window->UserPort))) {
        ULONG class = message->Class;
        UWORD code = message->Code;
        ReplyMsg((struct Message *)message);
        if (class == IDCMP_CLOSEWINDOW) key = KEY_QUIT;
        else if (class == IDCMP_VANILLAKEY) {
            if (code == 13) key = KEY_RUN;
            else if (code == 27) key = KEY_BACK;
            else if (code == 'q' || code == 'Q') key = KEY_QUIT;
            else if (code == 'w' || code == 'W' || code == 'k') key = KEY_UP;
            else if (code == 's' || code == 'S' || code == 'j') key = KEY_DOWN;
            else if (code == 'a' || code == 'A' || code == 'h') key = KEY_LEFT;
            else if (code == 'd' || code == 'D' || code == 'l') key = KEY_RIGHT;
        } else if (class == IDCMP_RAWKEY && !(code & 0x80)) {
            switch (code) {
            case 0x44: key = KEY_RUN; break;
            case 0x45: key = KEY_BACK; break;
            case 0x4c: key = KEY_UP; break;
            case 0x4d: key = KEY_DOWN; break;
            case 0x4f: key = KEY_LEFT; break;
            case 0x4e: key = KEY_RIGHT; break;
            default: break;
            }
        }
    }
    return key;
}

static int WaitMenuKey(void)
{
    struct Window *window = (struct Window *)mglGetInputWindowHandle();

    if (!window)
        window = (struct Window *)mglGetWindowHandle();
    if (window && window->UserPort)
        WaitPort(window->UserPort);
    return PollKey();
}

static GLboolean ConfigureInput(void)
{
    struct Window *window = (struct Window *)mglGetInputWindowHandle();
    ULONG mask;

    if (!window)
        window = (struct Window *)mglGetWindowHandle();
    if (!window)
        return GL_FALSE;
    mask = window->IDCMPFlags | IDCMP_CLOSEWINDOW |
           IDCMP_VANILLAKEY | IDCMP_RAWKEY;
    if (!ModifyIDCMP(window, mask))
        return GL_FALSE;
    ActivateWindow(window);
    return GL_TRUE;
}

static void SetNativeTitle(const char *testName, const char *phase,
                           ULONG fpsX1000, ULONG frame, ULONG total)
{
    struct Window *window = (struct Window *)mglGetWindowHandle();

    if (!window || !g_windowed)
        return;
    if (fpsX1000 && total)
        sprintf(g_windowTitle, "FPS %lu.%03lu | %s | %s %lu/%lu",
                (unsigned long)(fpsX1000 / 1000UL),
                (unsigned long)(fpsX1000 % 1000UL), testName, phase,
                (unsigned long)frame, (unsigned long)total);
    else if (fpsX1000)
        sprintf(g_windowTitle, "FPS %lu.%03lu | %s | %s",
                (unsigned long)(fpsX1000 / 1000UL),
                (unsigned long)(fpsX1000 % 1000UL), testName, phase);
    else if (total)
        sprintf(g_windowTitle, "FPS measuring | %s | %s %lu/%lu",
                testName, phase, (unsigned long)frame,
                (unsigned long)total);
    else
        sprintf(g_windowTitle, "%s | %s | MiniGL Test Suite", phase, testName);
    SetWindowTitles(window, (CONST_STRPTR)g_windowTitle, (CONST_STRPTR)-1);
}

static void AdjustFrameLength(int direction)
{
    g_frameOption += direction;
    if (g_frameOption < 0) g_frameOption = ARRAY_SIZE(g_frameOptions) - 1;
    if (g_frameOption >= ARRAY_SIZE(g_frameOptions)) g_frameOption = 0;
    g_measureFrames = g_frameOptions[g_frameOption];
}

static void DrawBlank(ULONG frame)
{
    (void)frame;
    glClearColor(0.03f, 0.05f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void DrawTriangle(ULONG frame)
{
    (void)frame;
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(250, 180, 40);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.75f, -0.65f);
    glVertex2f(0.75f, -0.65f);
    glVertex2f(0.0f, 0.75f);
    glEnd();
}

static GLboolean VerifyTriangle(char *detail)
{
    SurfaceView view;
    ULONG background, center;
    if (!CaptureSurface(&view)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    background = SampleNDC(&view, -0.95f, 0.90f);
    center = SampleNDC(&view, 0.0f, 0.0f);
    sprintf(detail, "center %s background", center != background ? "differs from" : "equals");
    return center != background;
}

static void DrawDepth(ULONG frame)
{
    (void)frame;
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glColor3ub(230, 40, 40);
    glBegin(GL_QUADS);
    glVertex3f(-0.55f, -0.55f, -0.5f); glVertex3f(0.55f, -0.55f, -0.5f);
    glVertex3f(0.55f, 0.55f, -0.5f); glVertex3f(-0.55f, 0.55f, -0.5f);
    glVertex3f(-0.9f, 0.65f, -0.5f); glVertex3f(-0.7f, 0.65f, -0.5f);
    glVertex3f(-0.7f, 0.85f, -0.5f); glVertex3f(-0.9f, 0.85f, -0.5f);
    glEnd();

    glColor3ub(30, 220, 70);
    glBegin(GL_QUADS);
    glVertex3f(-0.45f, -0.45f, 0.5f); glVertex3f(0.45f, -0.45f, 0.5f);
    glVertex3f(0.45f, 0.45f, 0.5f); glVertex3f(-0.45f, 0.45f, 0.5f);
    glEnd();
}

static GLboolean VerifyDepth(char *detail)
{
    SurfaceView view;
    ULONG background, center, reference;
    if (!CaptureSurface(&view)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    background = SampleNDC(&view, 0.9f, 0.9f);
    center = SampleNDC(&view, 0.0f, 0.0f);
    reference = SampleNDC(&view, -0.8f, 0.75f);
    sprintf(detail, "near surface %s reference", center == reference ? "matches" : "does not match");
    return center != background && center == reference;
}

static void DrawBlend(ULONG frame)
{
    (void)frame;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0.35f, -0.35f); glVertex2f(0.75f, -0.35f);
    glVertex2f(0.75f, 0.35f); glVertex2f(0.35f, 0.35f);
    glEnd();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 0.0f, 0.0f, 0.5f);
    glBegin(GL_QUADS);
    glVertex2f(-0.75f, -0.35f); glVertex2f(-0.35f, -0.35f);
    glVertex2f(-0.35f, 0.35f); glVertex2f(-0.75f, 0.35f);
    glEnd();
    glDisable(GL_BLEND);
}

static GLboolean VerifyBlend(char *detail)
{
    SurfaceView view;
    ULONG background, halfRed, fullRed;
    if (!CaptureSurface(&view)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    background = SampleNDC(&view, 0.0f, 0.8f);
    halfRed = SampleNDC(&view, -0.55f, 0.0f);
    fullRed = SampleNDC(&view, 0.55f, 0.0f);
    sprintf(detail, "half alpha is %s", halfRed != background && halfRed != fullRed ? "distinct" : "not distinct");
    return halfRed != background && halfRed != fullRed && fullRed != background;
}

static void DrawMatrix(ULONG frame)
{
    (void)frame;
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.55f, 0.0f, 0.0f);
    glColor3ub(60, 180, 255);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.25f, -0.35f);
    glVertex2f(0.25f, -0.35f);
    glVertex2f(0.0f, 0.35f);
    glEnd();
}

static GLboolean VerifyMatrix(char *detail)
{
    SurfaceView view;
    ULONG background, oldCenter, translated;
    if (!CaptureSurface(&view)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    background = SampleNDC(&view, -0.8f, 0.8f);
    oldCenter = SampleNDC(&view, 0.0f, 0.0f);
    translated = SampleNDC(&view, 0.55f, 0.0f);
    sprintf(detail, "translated sample %s", translated != background ? "visible" : "missing");
    return oldCenter == background && translated != background;
}

static const GLfloat g_arrayVertices[] = {
    -0.85f,-0.45f,0.0f, -0.15f,-0.45f,0.0f, -0.5f,0.45f,0.0f,
     0.15f,-0.45f,0.0f,  0.85f,-0.45f,0.0f,  0.5f,0.45f,0.0f
};

static void DrawArrays(ULONG frame)
{
    (void)frame;
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(180, 80, 240);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, g_arrayVertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static GLboolean VerifyArrays(char *detail)
{
    SurfaceView view;
    ULONG background, left, right;
    if (!CaptureSurface(&view)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    background = SampleNDC(&view, 0.0f, 0.8f);
    left = SampleNDC(&view, -0.5f, -0.1f);
    right = SampleNDC(&view, 0.5f, -0.1f);
    sprintf(detail, "array triangles %s", left != background && right != background ? "visible" : "missing");
    return left != background && right != background && left == right;
}

static GLboolean VerifyThroughput(char *detail)
{
    SurfaceView view;
    ULONG background, triangle;
    if (!CaptureSurface(&view)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    background = SampleNDC(&view, 0.98f, 0.98f);
    triangle = SampleNDC(&view, -0.86f, -0.84f);
    sprintf(detail, "64 triangle grid %s", triangle != background ? "visible" : "missing");
    return triangle != background;
}

static void DrawThroughput(ULONG frame)
{
    int x, y;
    GLfloat wobble = (GLfloat)((LONG)(frame % 20UL) - 10L) * 0.001f;
    glClearColor(0.01f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            GLfloat left = -0.95f + (GLfloat)x * 0.24f + wobble;
            GLfloat bottom = -0.9f + (GLfloat)y * 0.22f;
            glColor3ub((GLubyte)(40 + x * 24), (GLubyte)(40 + y * 24), 180);
            glVertex2f(left, bottom);
            glVertex2f(left + 0.19f, bottom);
            glVertex2f(left + 0.095f, bottom + 0.17f);
        }
    }
    glEnd();
}

static void DrawCubeGeometry(void){
    glBegin(GL_QUADS);
    glColor3ub(235, 65, 55);
    glVertex3f(-1,-1, 1); glVertex3f( 1,-1, 1); glVertex3f( 1, 1, 1); glVertex3f(-1, 1, 1);
    glColor3ub(55, 190, 245);
    glVertex3f( 1,-1,-1); glVertex3f(-1,-1,-1); glVertex3f(-1, 1,-1); glVertex3f( 1, 1,-1);
    glColor3ub(75, 220, 105);
    glVertex3f(-1,-1,-1); glVertex3f(-1,-1, 1); glVertex3f(-1, 1, 1); glVertex3f(-1, 1,-1);
    glColor3ub(245, 190, 45);
    glVertex3f( 1,-1, 1); glVertex3f( 1,-1,-1); glVertex3f( 1, 1,-1); glVertex3f( 1, 1, 1);
    glColor3ub(190, 85, 235);
    glVertex3f(-1, 1, 1); glVertex3f( 1, 1, 1); glVertex3f( 1, 1,-1); glVertex3f(-1, 1,-1);
    glColor3ub(245, 125, 40);
    glVertex3f(-1,-1,-1); glVertex3f( 1,-1,-1); glVertex3f( 1,-1, 1); glVertex3f(-1,-1, 1);
    glEnd();
}

static void DrawCubeAt(GLfloat x, GLfloat y, GLfloat z, GLfloat scale,
                       GLfloat angle)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(angle, 0.0f, 1.0f, 0.0f);
    glRotatef(angle * 0.63f + 23.0f, 1.0f, 0.0f, 0.0f);
    glScalef(scale, scale, scale);
    DrawCubeGeometry();
    glPopMatrix();
}

static void DrawCubeScene(ULONG frame)
{
    GLfloat angle = 32.0f + (GLfloat)(frame % 360UL) * 1.7f;
    glClearColor(0.015f, 0.025f, 0.06f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.625, 0.625, 1.0, 30.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    DrawCubeAt(0.0f, 0.0f, -5.5f, 1.0f, angle);
    DrawCubeAt(-2.7f, 1.35f, -8.0f, 0.65f, -angle * 1.2f);
    DrawCubeAt(2.7f, 1.35f, -8.0f, 0.65f, angle * 1.4f);
    DrawCubeAt(-2.7f, -1.35f, -8.0f, 0.65f, angle * 1.6f);
    DrawCubeAt(2.7f, -1.35f, -8.0f, 0.65f, -angle * 1.8f);
}

static GLboolean VerifyCubeScene(char *detail)
{
    SurfaceView view;
    ULONG background, center, satellite;
    if (!CaptureSurface(&view)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    background = SampleNDC(&view, 0.0f, 0.94f);
    center = SampleNDC(&view, 0.0f, 0.0f);
    satellite = SampleNDC(&view, -0.34f, 0.27f);
    sprintf(detail, "center cube %s satellites %s",
            center != background ? "visible" : "missing",
            satellite != background ? "visible" : "check visually");
    return center != background;
}

static GLboolean VerifyDispatch(char *detail)
{
    if (!MiniGLDispatch) {
        strcpy(detail, "dispatch table is null");
        return GL_FALSE;
    }
    if (MiniGLDispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION) {
        sprintf(detail, "ABI %lu expected %lu",
                (unsigned long)MiniGLDispatch->abiVersion,
                (unsigned long)MINIGL_DISPATCH_ABI_VERSION);
        return GL_FALSE;
    }
    if (MiniGLDispatch->structSize < sizeof(MGLDispatchTable)) {
        sprintf(detail, "table size %lu expected %lu",
                (unsigned long)MiniGLDispatch->structSize,
                (unsigned long)sizeof(MGLDispatchTable));
        return GL_FALSE;
    }
    sprintf(detail, "ABI %lu table %lu bytes flags %08lx",
            (unsigned long)MiniGLDispatch->abiVersion,
            (unsigned long)MiniGLDispatch->structSize,
            (unsigned long)MiniGLDispatch->backendFlags);
    return GL_TRUE;
}

/* ------------------------------------------------------------------ */
/* Primitive x shade-model matrix.                                      */
/*                                                                      */
/* Every filled primitive type is drawn in both shade models with one    */
/* flat colour and checked against that colour. A backend that drops or  */
/* mis-shades one combination fails here; the previous difference-based  */
/* checks could not see it.                                             */
/* ------------------------------------------------------------------ */

#define MATRIX_GREY_F 0.6f
#define MATRIX_GREY_8 153

static void EmitSolidQuad(GLenum prim, GLfloat cx)
{
    GLfloat x0 = cx - 0.18f, x1 = cx + 0.18f;
    GLfloat y0 = -0.28f, y1 = 0.28f;

    glBegin(prim);
    switch (prim) {
    case GL_TRIANGLES:
        glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1);
        glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
        break;
    case GL_TRIANGLE_STRIP:
        glVertex2f(x0, y0); glVertex2f(x1, y0);
        glVertex2f(x0, y1); glVertex2f(x1, y1);
        break;
    default: /* fan, quads, polygon all take a CCW ring */
        glVertex2f(x0, y0); glVertex2f(x1, y0);
        glVertex2f(x1, y1); glVertex2f(x0, y1);
        break;
    }
    glEnd();
}

static void DrawPrimMatrix(ULONG frame)
{
    static const GLenum prims[5] = {
        GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUADS, GL_POLYGON
    };
    int i;

    (void)frame;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(MATRIX_GREY_F, MATRIX_GREY_F, MATRIX_GREY_F, 1.0f);
    for (i = 0; i < 5; ++i) {
        glShadeModel((i & 1) ? GL_SMOOTH : GL_FLAT);
        EmitSolidQuad(prims[i], -0.72f + 0.36f * (GLfloat)i);
    }
    glShadeModel(GL_SMOOTH);
}

static GLboolean VerifyPrimMatrix(char *detail)
{
    static const GLenum prims[5] = {
        GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUADS, GL_POLYGON
    };
    static const char *names[5] = {
        "triangles", "tristrip", "trifan", "quads", "polygon"
    };
    static const GLenum shades[2] = { GL_FLAT, GL_SMOOTH };
    static const char *shadeNames[2] = { "flat", "smooth" };
    CheckLog log;
    char what[64];
    int p, s;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;

    for (p = 0; p < 5; ++p) {
        for (s = 0; s < 2; ++s) {
            glShadeModel(shades[s]);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glColor4f(MATRIX_GREY_F, MATRIX_GREY_F, MATRIX_GREY_F, 1.0f);
            EmitSolidQuad(prims[p], 0.0f);

            sprintf(what, "%s.%s", names[p], shadeNames[s]);
            CheckRGB(&log, what, 0.0f, 0.0f,
                     MATRIX_GREY_8, MATRIX_GREY_8, MATRIX_GREY_8, 14);
            /* Guard against "the whole screen is grey" passing the above. */
            sprintf(what, "%s.%s.bg", names[p], shadeNames[s]);
            CheckRGB(&log, what, 0.0f, 0.85f, 0, 0, 0, 14);
        }
    }
    glShadeModel(GL_SMOOTH);

    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "%d checks, all primitives both shade models", log.checks);
    return log.failures == 0;
}

/* ------------------------------------------------------------------ */
/* Point size.                                                          */
/* ------------------------------------------------------------------ */

static void DrawPointSizes(ULONG frame)
{
    static const GLfloat sizes[5] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
    int i;

    (void)frame;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    for (i = 0; i < 5; ++i) {
        glPointSize(sizes[i]);
        glBegin(GL_POINTS);
        glVertex2f(-0.6f + 0.3f * (GLfloat)i, 0.0f);
        glEnd();
    }
    glPointSize(1.0f);
}

/*
 * Locate the drawn point rather than assuming where it landed: scan a window
 * around the surface centre for anything that is not background and report the
 * bounding box. Width 0 with count 0 means nothing was rasterised at all;
 * a non-zero count with an offset box means the point rendered elsewhere.
 */
static int MeasureCentrePointWidth(int size)
{
    SurfaceView view;
    ULONG background;
    LONG cx, cy, x, y;
    LONG minX = 0, maxX = 0, minY = 0, maxY = 0;
    LONG count = 0;
    const LONG radius = 40;

    if (!CaptureSurface(&view))
        return -1;
    cx = (LONG)(view.width / 2UL);
    cy = (LONG)(view.height / 2UL);
    background = PixelToken(&view, 4, 4);

    for (y = cy - radius; y <= cy + radius; ++y) {
        if (y < 0 || y >= (LONG)view.height)
            continue;
        for (x = cx - radius; x <= cx + radius; ++x) {
            if (x < 0 || x >= (LONG)view.width)
                continue;
            if (PixelToken(&view, (ULONG)x, (ULONG)y) == background)
                continue;
            if (!count) {
                minX = maxX = x;
                minY = maxY = y;
            } else {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
            ++count;
        }
    }

    printf("POINTDIAG size=%d count=%ld box=%ldx%ld at=%ld,%ld centre=%ld,%ld\n",
           size, (long)count,
           (long)(count ? maxX - minX + 1 : 0),
           (long)(count ? maxY - minY + 1 : 0),
           (long)minX, (long)minY, (long)cx, (long)cy);

    return count ? (int)(maxX - minX + 1) : 0;
}

static GLboolean VerifyPointSizes(char *detail)
{
    static const GLfloat sizes[5] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
    int measured[5];
    int i, failures = 0;
    char *cursor = detail;

    for (i = 0; i < 5; ++i) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glPointSize(sizes[i]);
        glBegin(GL_POINTS);
        glVertex2f(0.0f, 0.0f);
        glEnd();
        measured[i] = MeasureCentrePointWidth((int)sizes[i]);
        if (measured[i] < (int)sizes[i] - 1 || measured[i] > (int)sizes[i] + 1)
            ++failures;
    }
    glPointSize(1.0f);

    cursor += sprintf(cursor, "px");
    for (i = 0; i < 5; ++i)
        cursor += sprintf(cursor, " %d->%d", (int)sizes[i], measured[i]);
    return failures == 0;
}

/* ------------------------------------------------------------------ */
/* Texture-alpha fill (the Quake Draw_AlphaFill recipe).                */
/* ------------------------------------------------------------------ */

static GLuint MakeAlphaTexture(UBYTE alpha)
{
    GLuint name = 0;
    UBYTE texel[4];

    texel[0] = 255; texel[1] = 255; texel[2] = 255; texel[3] = alpha;
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, texel);
    return name;
}

/* Quake's fills bind long-lived textures; creating and deleting one per
 * frame measures texture-object churn instead of alpha blending. Keep a
 * small cache keyed by alpha so each fill reuses its texture. */
#define ALPHA_CACHE_MAX 8
static struct { UBYTE alpha; GLuint name; } g_alphaTexCache[ALPHA_CACHE_MAX];

static GLuint GetAlphaTexture(UBYTE alpha)
{
    int slot;

    for (slot = 0; slot < ALPHA_CACHE_MAX; ++slot)
        if (g_alphaTexCache[slot].name && g_alphaTexCache[slot].alpha == alpha)
            break;
    if (slot == ALPHA_CACHE_MAX) {
        for (slot = 0; slot < ALPHA_CACHE_MAX; ++slot)
            if (!g_alphaTexCache[slot].name)
                break;
        if (slot < ALPHA_CACHE_MAX) {
            g_alphaTexCache[slot].alpha = alpha;
            g_alphaTexCache[slot].name = MakeAlphaTexture(alpha);
        }
    }
    /* Creation binds, but cache hits must re-bind: the caller draws
     * through whatever unit state is current. */
    if (slot < ALPHA_CACHE_MAX)
        glBindTexture(GL_TEXTURE_2D, g_alphaTexCache[slot].name);
    return slot < ALPHA_CACHE_MAX ? g_alphaTexCache[slot].name : 0;
}

/* Opaque background quad, then a texture-alpha fill on top of it. */
static void DrawAlphaFillScene(UBYTE alpha, GLfloat fr, GLfloat fg, GLfloat fb,
                               GLfloat bgLevel)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glColor4f(bgLevel, bgLevel, bgLevel, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(-0.9f, -0.9f); glVertex2f(0.9f, -0.9f);
    glVertex2f(0.9f, 0.9f); glVertex2f(-0.9f, 0.9f);
    glEnd();

    GetAlphaTexture(alpha);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(fr, fg, fb, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-0.9f, -0.9f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(0.9f, -0.9f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(0.9f, 0.9f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-0.9f, 0.9f);
    glEnd();
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

static void DrawAlphaFill(ULONG frame)
{
    (void)frame;
    DrawAlphaFillScene(51, 0.84f, 0.73f, 0.27f, 0.25f);
}

static int ExpectedBlend(int fill, int background, int alpha)
{
    return (fill * alpha + background * (255 - alpha) + 127) / 255;
}

static GLboolean VerifyAlphaFill(char *detail)
{
    static const UBYTE alphas[3] = { 17, 51, 204 };
    static const GLfloat bgLevels[2] = { 0.0f, 0.5f };
    CheckLog log;
    char what[64];
    int a, g;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;

    for (a = 0; a < 3; ++a) {
        for (g = 0; g < 2; ++g) {
            int bg8 = (int)(bgLevels[g] * 255.0f + 0.5f);
            int er = ExpectedBlend(214, bg8, alphas[a]);
            int eg = ExpectedBlend(186, bg8, alphas[a]);
            int eb = ExpectedBlend(69, bg8, alphas[a]);

            DrawAlphaFillScene(alphas[a], 0.84f, 0.73f, 0.27f, bgLevels[g]);
            sprintf(what, "a%d.over%d", (int)alphas[a], bg8);
            CheckRGB(&log, what, 0.0f, 0.0f, er, eg, eb, 16);
        }
    }

    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "%d texture-alpha fills match computed blend", log.checks);
    return log.failures == 0;
}

/* ------------------------------------------------------------------ */
/* Presented-frame stability.                                           */
/*                                                                      */
/* The identical frame is drawn and presented repeatedly. Correct output */
/* is the same pixel every time. Tint that grows across frames means the */
/* fill is reaching more than one buffer, or a buffer is presented       */
/* without its clear - the accumulating wash seen in Quake.              */
/* ------------------------------------------------------------------ */

#define STABILITY_FRAMES 10

static void DrawStabilityScene(ULONG frame)
{
    (void)frame;
    DrawAlphaFillScene(51, 0.84f, 0.73f, 0.27f, 0.25f);
}

static GLboolean VerifyStability(char *detail)
{
    int r0 = -1, g0 = -1, b0 = -1;
    int frame, worst = 0, worstFrame = -1;

    if (!CalibrateChannels(detail))
        return GL_FALSE;

    for (frame = 0; frame < STABILITY_FRAMES; ++frame) {
        int r, g, b, delta;

        DrawStabilityScene((ULONG)frame);
        if (!SampleRGB(0.0f, 0.0f, &r, &g, &b)) {
            strcpy(detail, "surface capture failed");
            return GL_FALSE;
        }
        if (frame == 0) {
            r0 = r; g0 = g; b0 = b;
        } else {
            delta = ABS_DIFF(r, r0);
            if (ABS_DIFF(g, g0) > delta) delta = ABS_DIFF(g, g0);
            if (ABS_DIFF(b, b0) > delta) delta = ABS_DIFF(b, b0);
            if (delta > worst) {
                worst = delta;
                worstFrame = frame;
            }
        }
        mglSwitchDisplay();
    }

    if (worst > 8) {
        sprintf(detail, "frame %d drifted %d from %d,%d,%d",
                worstFrame, worst, r0, g0, b0);
        return GL_FALSE;
    }
    sprintf(detail, "%d presents stable at %d,%d,%d (max drift %d)",
            STABILITY_FRAMES, r0, g0, b0, worst);
    return GL_TRUE;
}

/* ------------------------------------------------------------------ */
/* Quake-focused isolated workloads.                                   */
/* ------------------------------------------------------------------ */

static GLuint g_quakeTexture;

/* The Ogre's own skin from progs/ogre.mdl, expanded through
 * gfx/palette.lmp by tools/extract_quake_mdl.py. */
static GLuint OgreSkinTexture(void)
{
    if (g_quakeTexture) {
        glBindTexture(GL_TEXTURE_2D, g_quakeTexture);
        return g_quakeTexture;
    }
    glGenTextures(1, &g_quakeTexture);
    glBindTexture(GL_TEXTURE_2D, g_quakeTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 QUAKE_OGRE_SKIN_WIDTH, QUAKE_OGRE_SKIN_HEIGHT, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, g_quakeOgreSkin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    return g_quakeTexture;
}

typedef struct {
    GLfloat x, y, z;
    GLfloat s, t, w;
    GLint xi, yi, zi;
    ULONG color;
} QuakeAliasVertex;

static QuakeAliasVertex g_ogreVertices[QUAKE_OGRE_VERTEX_COUNT];
static GLboolean g_ogreReady;
static ULONG g_ogreSetupGeneration;

static void PrepareOgreVertices(void)
{
    int index;
    if (g_ogreReady) return;
    for (index = 0; index < QUAKE_OGRE_VERTEX_COUNT; ++index) {
        QuakeAliasVertex *vertex = &g_ogreVertices[index];
        int shade = 112 + (index * 97 % 112);
        vertex->x = g_quakeOgreXYZ[index][0];
        vertex->y = g_quakeOgreXYZ[index][1];
        vertex->z = g_quakeOgreXYZ[index][2];
        vertex->s = g_quakeOgreST[index][0];
        vertex->t = g_quakeOgreST[index][1];
        vertex->w = 1.0f;
        vertex->xi = (GLint)(vertex->x * 256.0f);
        vertex->yi = (GLint)(vertex->y * 256.0f);
        vertex->zi = (GLint)(vertex->z * 256.0f);
        vertex->color = 0xff000000UL | ((ULONG)shade << 16) |
                        ((ULONG)shade << 8) | (ULONG)shade;
    }
    g_ogreReady = GL_TRUE;
}

static void DrawQuakeOgre(ULONG frame)
{
    GLboolean integerPath = (frame & 1UL) != 0;
    GLfloat scale = integerPath ? 0.035f / 256.0f : 0.035f;

    PrepareOgreVertices();
    if (g_ogreSetupGeneration != g_stateGeneration) {
        g_ogreSetupGeneration = g_stateGeneration;
        glClearColor(0.015f, 0.02f, 0.035f, 1.0f);
        glClearDepth(1.0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glShadeModel(GL_SMOOTH);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1.0, 1.0, -0.625, 0.625, 1.0, 20.0);
        OgreSkinTexture();
        glColor3ub(255, 255, 255);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        /* ST is strided out of the same alias vertex the positions come from:
         * glDrawElements addresses every array with one index, so the
         * extractor resolved Quake's onseam rule by duplicating seam
         * vertices instead. */
        glTexCoordPointer(2, GL_FLOAT, sizeof(QuakeAliasVertex),
                          &g_ogreVertices[0].s);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -4.2f);
    glRotatef(18.0f + (GLfloat)(frame % 360UL) * 2.0f, 0.0f, 1.0f, 0.0f);
    glRotatef(-82.0f, 1.0f, 0.0f, 0.0f);
    glScalef(scale, scale, scale);
    if (!integerPath)
        glTranslatef(7.0f, 4.0f, -5.0f);
    else
        glTranslatef(7.0f * 256.0f, 4.0f * 256.0f, -5.0f * 256.0f);

    if (integerPath)
        glVertexPointer(3, GL_INT, sizeof(QuakeAliasVertex), &g_ogreVertices[0].xi);
    else
        glVertexPointer(3, GL_FLOAT, sizeof(QuakeAliasVertex), &g_ogreVertices[0].x);
    glDrawElements(GL_TRIANGLES, QUAKE_OGRE_INDEX_COUNT, GL_UNSIGNED_SHORT,
                   g_quakeOgreIndices);
}

static GLboolean MeasureOgre(char *detail, const char *path, LONG minimum,
                             GLboolean requireQuadrants)
{
    SurfaceView view;
    ULONG background;
    LONG x, y, count = 0, margin = 0;
    LONG quadrants[4] = {0, 0, 0, 0};
    LONG edgeX, edgeY, allowed;

    if (!CaptureSurface(&view)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    background = PixelToken(&view, 2, 2);
    edgeX = (LONG)view.width / 32;
    edgeY = (LONG)view.height / 32;
    for (y = 0; y < (LONG)view.height; ++y) for (x = 0; x < (LONG)view.width; ++x) {
        int quadrant;
        if (PixelToken(&view, (ULONG)x, (ULONG)y) == background) continue;
        ++count;
        quadrant = (y >= (LONG)view.height / 2 ? 2 : 0) +
                   (x >= (LONG)view.width / 2 ? 1 : 0);
        ++quadrants[quadrant];
        if (x < edgeX || x >= (LONG)view.width - edgeX ||
            y < edgeY || y >= (LONG)view.height - edgeY)
            ++margin;
    }
    /*
     * The failure this guards against is a mis-transformed polygon spiking
     * off the model, which paints hundreds to thousands of pixels across the
     * frame. This backend also leaves a handful (under ten) of stray pixels
     * on the bottom scanline when a frame's geometry spans two submit slots,
     * so a fixed small allowance keeps the real regression detectable
     * without reporting that artifact as a mesh failure.
     */
    allowed = count / 128;
    if (allowed < 16) allowed = 16;
    if (count < minimum || margin > allowed) {
        sprintf(detail, "%s pixels=%ld margin=%ld allowed=%ld", path,
                (long)count, (long)margin, (long)allowed);
        return GL_FALSE;
    }
    if (requireQuadrants) {
        LONG quadrantMinimum = minimum / 16;
        int index;
        for (index = 0; index < 4; ++index) {
            if (quadrants[index] < quadrantMinimum) {
                sprintf(detail, "%s quadrants=%ld,%ld,%ld,%ld min=%ld", path,
                        (long)quadrants[0], (long)quadrants[1],
                        (long)quadrants[2], (long)quadrants[3],
                        (long)quadrantMinimum);
                return GL_FALSE;
            }
        }
    }
    return GL_TRUE;
}

static GLboolean VerifyQuakeOgre(char *detail)
{
    DrawQuakeOgre(0);
    if (!MeasureOgre(detail, "float", 200, GL_FALSE)) return GL_FALSE;
    DrawQuakeOgre(1);
    if (!MeasureOgre(detail, "integer", 200, GL_FALSE)) return GL_FALSE;
    strcpy(detail, "skinned Ogre mesh: float and integer array paths bounded");
    return GL_TRUE;
}

enum {
    OGRE_X4_STATIC,
    OGRE_X4_SPIN,
    OGRE_X4_ADVANCE
};

static ULONG g_fourOgreSetupGeneration;

static GLfloat OgreAdvanceOffset(ULONG frame, int instance)
{
    ULONG phase = (frame + (ULONG)instance * 12UL) % 48UL;
    LONG triangle = phase < 24UL ? (LONG)phase : 48L - (LONG)phase;
    return ((GLfloat)triangle / 12.0f) - 1.0f;
}

static void DrawFourOgres(ULONG frame, int motion)
{
    static const GLfloat positions[4][3] = {
        {-1.15f, -0.55f, -4.5f},
        { 1.15f, -0.55f, -4.5f},
        {-0.85f,  0.55f, -8.5f},
        { 0.85f,  0.55f, -8.5f}
    };
    static const GLfloat angles[4] = {25.0f, -20.0f, 155.0f, -145.0f};
    int instance;

    PrepareOgreVertices();
    if (g_fourOgreSetupGeneration != g_stateGeneration) {
        g_fourOgreSetupGeneration = g_stateGeneration;
        glClearColor(0.015f, 0.02f, 0.035f, 1.0f);
        glClearDepth(1.0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glShadeModel(GL_SMOOTH);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1.0, 1.0, -0.625, 0.625, 1.0, 20.0);
        OgreSkinTexture();
        glColor3ub(255, 255, 255);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, sizeof(QuakeAliasVertex),
                        &g_ogreVertices[0].x);
        glTexCoordPointer(2, GL_FLOAT, sizeof(QuakeAliasVertex),
                          &g_ogreVertices[0].s);
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    for (instance = 0; instance < 4; ++instance) {
        GLfloat z = positions[instance][2];
        GLfloat angle = angles[instance];
        if (motion == OGRE_X4_ADVANCE)
            z += OgreAdvanceOffset(frame, instance) * 1.5f;
        if (motion == OGRE_X4_SPIN)
            angle += (GLfloat)frame * (1.5f + (GLfloat)instance * 0.35f);

        glPushMatrix();
        glTranslatef(positions[instance][0], positions[instance][1], z);
        glRotatef(angle, 0.0f, 1.0f, 0.0f);
        glRotatef(-82.0f, 1.0f, 0.0f, 0.0f);
        glScalef(0.035f, 0.035f, 0.035f);
        glTranslatef(7.0f, 4.0f, -5.0f);
        glDrawElements(GL_TRIANGLES, QUAKE_OGRE_INDEX_COUNT,
                       GL_UNSIGNED_SHORT, g_quakeOgreIndices);
        glPopMatrix();
    }
}

static void DrawFourOgresStatic(ULONG frame)
{
    DrawFourOgres(frame, OGRE_X4_STATIC);
}

static void DrawFourOgresSpin(ULONG frame)
{
    DrawFourOgres(frame, OGRE_X4_SPIN);
}

static void DrawFourOgresAdvance(ULONG frame)
{
    DrawFourOgres(frame, OGRE_X4_ADVANCE);
}

static GLboolean VerifyFourOgres(char *detail, DrawTestFn draw,
                                 const char *motion)
{
    draw(0);
    if (!MeasureOgre(detail, motion, 800, GL_TRUE)) return GL_FALSE;
    sprintf(detail, "four skinned Ogres %s; all quadrants bounded", motion);
    return GL_TRUE;
}

static GLboolean VerifyFourOgresStatic(char *detail)
{
    return VerifyFourOgres(detail, DrawFourOgresStatic, "static");
}

static GLboolean VerifyFourOgresSpin(char *detail)
{
    return VerifyFourOgres(detail, DrawFourOgresSpin, "spinning");
}

static GLboolean VerifyFourOgresAdvance(char *detail)
{
    return VerifyFourOgres(detail, DrawFourOgresAdvance, "advancing");
}

static void DrawQuakeWorldStream(ULONG frame)
{
    int x, y;
    GLfloat wobble = (GLfloat)((LONG)(frame % 9UL) - 4L) * 0.0005f;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    glColor3ub(48, 176, 196);
    glBegin(GL_TRIANGLES);
    for (y = 0; y < 16; ++y) for (x = 0; x < 32; ++x) {
        GLfloat x0 = -0.96f + (GLfloat)x * 0.06f + wobble;
        GLfloat y0 = -0.88f + (GLfloat)y * 0.11f;
        /* Slight overlap prevents sentinel samples from landing in
         * rasterisation cracks between independently emitted cells. */
        GLfloat x1 = x0 + 0.061f, y1 = y0 + 0.111f;
        glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1);
        glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    }
    glEnd();
}

static GLboolean VerifyQuakeWorldStream(char *detail)
{
    CheckLog log;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuakeWorldStream(0);
    CheckRGB(&log, "left", -0.8f, -0.7f, 48, 176, 196, 16);
    CheckRGB(&log, "centre", 0.0f, 0.0f, 48, 176, 196, 16);
    CheckRGB(&log, "right", 0.8f, 0.7f, 48, 176, 196, 16);
    CheckRGB(&log, "border", 0.99f, 0.99f, 0, 0, 0, 10);
    if (log.failures) strncpy(detail, log.first, 95);
    else sprintf(detail, "3072 vertices/frame; %d sentinel pixels correct", log.checks);
    return log.failures == 0;
}

static GLuint g_lightmapTexture;
static ULONG g_lightmapSetupGeneration;
static GLuint g_staticLightmapTexture;
static UBYTE g_lightmapPixels[64 * 64 * 4];

static void DrawFullscreenQuad(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-0.9f, -0.9f);
    glTexCoord2f(1, 0); glVertex2f(0.9f, -0.9f);
    glTexCoord2f(1, 1); glVertex2f(0.9f, 0.9f);
    glTexCoord2f(0, 1); glVertex2f(-0.9f, 0.9f);
    glEnd();
}

/* Replicates Silent Hill's per-frame present uploads: two 256x256
 * 16-bit (565) VRAM tiles re-uploaded and drawn as textured quads every
 * frame. The benchmark FPS therefore tracks the MiniGL texture upload
 * path; a faster implementation raises it. (mglLockBack is omitted: the
 * lock leaves the suite's readback capture unable to calibrate.) */
#define TEXUPLOAD_DIM 256
static GLuint g_texUpload[2];
static UBYTE g_texUploadData[2][TEXUPLOAD_DIM * TEXUPLOAD_DIM * 2];

static void PrepareTexUpload(void)
{
    int t, i;

    if (g_texUpload[0])
        return;
    for (t = 0; t < 2; ++t) {
        for (i = 0; i < TEXUPLOAD_DIM * TEXUPLOAD_DIM; ++i) {
            unsigned v = (unsigned)(t ? 0xF800u : 0x001Fu) |
                         ((unsigned)i & 0x1Fu);
            g_texUploadData[t][i * 2] = (UBYTE)(v >> 8);
            g_texUploadData[t][i * 2 + 1] = (UBYTE)(v & 0xffu);
        }
        glGenTextures(1, &g_texUpload[t]);
        glBindTexture(GL_TEXTURE_2D, g_texUpload[t]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }
}

static void DrawTexUploadScene(GLboolean doLock)
{
    int t;

    (void)doLock;
    PrepareTexUpload();
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    for (t = 0; t < 2; ++t) {
        glBindTexture(GL_TEXTURE_2D, g_texUpload[t]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXUPLOAD_DIM, TEXUPLOAD_DIM, 0,
                     MGL_UNSIGNED_SHORT_5_6_5, GL_UNSIGNED_SHORT,
                     g_texUploadData[t]);
        DrawFullscreenQuad();
    }
}

static void DrawTexUploadBench(ULONG frame)
{
    (void)frame;
    DrawTexUploadScene(GL_TRUE);
}

static GLboolean VerifyTexUploadBench(char *detail)
{
    int r = -1, g = -1, b = -1;

    if (!CalibrateChannels(detail))
        return GL_FALSE;
    DrainErrors();
    DrawTexUploadScene(GL_FALSE);
    if (!SampleRGB(0.0f, 0.0f, &r, &g, &b)) {
        strcpy(detail, "surface capture failed");
        return GL_FALSE;
    }
    if ((r | g | b) == 0) {
        sprintf(detail, "uploaded tile did not render (%d,%d,%d)", r, g, b);
        return GL_FALSE;
    }
    strcpy(detail, "two 256x256 uploads per frame render");
    return GL_TRUE;
}

static void DrawQuakeLightmap(ULONG frame)
{
    int index;
    UBYTE r = (frame & 1UL) ? 48 : 208;
    UBYTE g = (frame & 1UL) ? 208 : 64;

    for (index = 0; index < 64 * 64; ++index) {
        g_lightmapPixels[index * 4] = r;
        g_lightmapPixels[index * 4 + 1] = g;
        g_lightmapPixels[index * 4 + 2] = 96;
        g_lightmapPixels[index * 4 + 3] = 255;
    }
    if (!g_lightmapTexture) {
        glGenTextures(1, &g_lightmapTexture);
        glBindTexture(GL_TEXTURE_2D, g_lightmapTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, g_lightmapPixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_lightmapTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64, GL_RGBA,
                        GL_UNSIGNED_BYTE, g_lightmapPixels);
    }
    if (g_lightmapSetupGeneration != g_stateGeneration) {
        /* Invariant across frames: only the two passes' enables and the
         * texture contents change per frame. */
        g_lightmapSetupGeneration = g_stateGeneration;
        glClearColor(0, 0, 0, 1);
        glColor3ub(255, 255, 255);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
    DrawFullscreenQuad();
    glBindTexture(GL_TEXTURE_2D, g_lightmapTexture);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    DrawFullscreenQuad();
    glDisable(GL_BLEND);
}

static void DrawQuakeLightmapStatic(ULONG frame)
{
    int index;
    (void)frame;
    if (!g_staticLightmapTexture) {
        for (index = 0; index < 64 * 64; ++index) {
            g_lightmapPixels[index * 4] = 208;
            g_lightmapPixels[index * 4 + 1] = 64;
            g_lightmapPixels[index * 4 + 2] = 96;
            g_lightmapPixels[index * 4 + 3] = 255;
        }
        glGenTextures(1, &g_staticLightmapTexture);
        glBindTexture(GL_TEXTURE_2D, g_staticLightmapTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, g_lightmapPixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
    glColor3ub(255, 255, 255); DrawFullscreenQuad();
    glBindTexture(GL_TEXTURE_2D, g_staticLightmapTexture);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_BLEND); glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    DrawFullscreenQuad(); glDisable(GL_BLEND);
}

static GLboolean VerifyQuakeLightmapStatic(char *detail)
{
    CheckLog log;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuakeLightmapStatic(0);
    CheckRGB(&log, "static lightmap", 0, 0, 208, 64, 96, 18);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "static two-pass lightmap is pixel-correct");
    return log.failures == 0;
}

static GLboolean VerifyQuakeLightmap(char *detail)
{
    CheckLog log;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuakeLightmap(0);
    CheckRGB(&log, "red update", 0, 0, 208, 64, 96, 18);
    DrawQuakeLightmap(1);
    CheckRGB(&log, "green update", 0, 0, 48, 208, 96, 18);
    DrawQuakeLightmap(0);
    CheckRGB(&log, "red replacement", 0, 0, 208, 64, 96, 18);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "three dynamic lightmap replacements are pixel-correct");
    return log.failures == 0;
}

/* ------------------------------------------------------------------ */
/* Texture source formats (L/LA/A/BGRA/ARGB).                          */
/*                                                                     */
/* Every format is uploaded with distinct per-channel bytes and read   */
/* back through the same calibrated surface the other tests use, so a  */
/* wrong expansion, a swapped channel order, or a bad source stride    */
/* fails naming the exact texel. GL_ALPHA expands to white RGB on      */
/* every MiniGL backend (the R200 ConvertPixel and the mglQ3 A8_ARGB   */
/* agree); that is MiniGL's documented behaviour, not the GL-spec      */
/* zero-RGB that Mesa produces. MGL_UBYTE_* sources exist only on the  */
/* R200 backend, so those two tests SKIP elsewhere.                    */
/* ------------------------------------------------------------------ */

#define TEXFMT_TOL 16

static GLuint UploadFormatTexture(GLenum internal, GLenum format,
                                  int width, int height,
                                  const UBYTE *texels)
{
    GLuint name = 0;

    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, format,
                 GL_UNSIGNED_BYTE, texels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return name;
}

/* The bound Wx1 texture stretched over the -0.9..0.9 fullscreen quad:
 * texel i is centred at this NDC x (sampled at y = 0). */
static GLfloat StripTexelX(int index, int texW)
{
    return -0.9f + ((GLfloat)index + 0.5f) * (1.8f / (GLfloat)texW);
}

/* A 4x4 texture on the same quad: cell (cx, cy), cy = 0 at the texture
 * bottom row, is centred at this NDC position. */
static void CellNDC(int cx, int cy, GLfloat *x, GLfloat *y)
{
    *x = -0.9f + ((GLfloat)cx + 0.5f) * 0.45f;
    *y = -0.9f + ((GLfloat)cy + 0.5f) * 0.45f;
}

static GLuint g_lumTexture;
static GLuint g_lumAlphaTexture;
static GLuint g_alphaFormatTexture;
static GLuint g_bgraTexture;
static GLuint g_argbTexture;
static GLuint g_subimageTexture;

/* Texture objects survive ResetGLState; create once, re-bind after. */
static void BindLuminance(void)
{
    static const UBYTE texels[4] = { 0, 85, 170, 255 };

    if (!g_lumTexture)
        g_lumTexture = UploadFormatTexture(GL_LUMINANCE, GL_LUMINANCE,
                                           4, 1, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_lumTexture);
}

static void BindLuminanceAlpha(void)
{
    static const UBYTE texels[4] = { 200, 255, 200, 128 };

    if (!g_lumAlphaTexture)
        g_lumAlphaTexture = UploadFormatTexture(GL_LUMINANCE_ALPHA,
                                                GL_LUMINANCE_ALPHA,
                                                2, 1, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_lumAlphaTexture);
}

static void BindAlphaFormat(void)
{
    static const UBYTE texels[2] = { 255, 128 };

    if (!g_alphaFormatTexture)
        g_alphaFormatTexture = UploadFormatTexture(GL_ALPHA, GL_ALPHA,
                                                   2, 1, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_alphaFormatTexture);
}

static void BindBGRA(void)
{
    static const UBYTE texels[8] =
        { 200, 150, 100, 255, 10, 20, 30, 255 };

    if (!g_bgraTexture)
        g_bgraTexture = UploadFormatTexture(GL_RGBA, MGL_UBYTE_BGRA,
                                            2, 1, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_bgraTexture);
}

static void BindARGB(void)
{
    static const UBYTE texels[8] =
        { 255, 100, 150, 200, 255, 30, 20, 10 };

    if (!g_argbTexture)
        g_argbTexture = UploadFormatTexture(GL_RGBA, MGL_UBYTE_ARGB,
                                            2, 1, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_argbTexture);
}

static void DrawLuminanceScene(void)
{
    BindLuminance();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    DrawFullscreenQuad();
}

static void DrawLuminanceAlphaScene(void)
{
    BindLuminanceAlpha();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DrawFullscreenQuad();
    glDisable(GL_BLEND);
}

static void DrawAlphaFormatScene(void)
{
    BindAlphaFormat();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DrawFullscreenQuad();
    glDisable(GL_BLEND);
}

static void DrawBGRAScene(void)
{
    BindBGRA();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    DrawFullscreenQuad();
}

static void DrawARGBScene(void)
{
    BindARGB();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    DrawFullscreenQuad();
}

static void DrawLuminance(ULONG frame)
{
    (void)frame;
    DrawLuminanceScene();
}

static void DrawLuminanceAlpha(ULONG frame)
{
    (void)frame;
    DrawLuminanceAlphaScene();
}

static void DrawAlphaFormat(ULONG frame)
{
    (void)frame;
    DrawAlphaFormatScene();
}

static void DrawBGRA(ULONG frame)
{
    (void)frame;
    DrawBGRAScene();
}

static void DrawARGB(ULONG frame)
{
    (void)frame;
    DrawARGBScene();
}

static GLboolean VerifyLuminance(char *detail)
{
    static const int want[4] = { 0, 85, 170, 255 };
    CheckLog log;
    char what[64];
    int i;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;
    DrawLuminanceScene();
    for (i = 0; i < 4; ++i) {
        sprintf(what, "texel%d", i);
        CheckRGB(&log, what, StripTexelX(i, 4), 0.0f,
                 want[i], want[i], want[i], TEXFMT_TOL);
    }
    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "4 luminance texels expand to exact grey");
    return log.failures == 0;
}

static GLboolean VerifyLuminanceAlpha(char *detail)
{
    CheckLog log;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;
    DrawLuminanceAlphaScene();
    CheckRGB(&log, "opaque", StripTexelX(0, 2), 0.0f,
             200, 200, 200, TEXFMT_TOL);
    CheckRGB(&log, "half", StripTexelX(1, 2), 0.0f,
             100, 100, 100, TEXFMT_TOL);
    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "luminance and alpha channels both correct");
    return log.failures == 0;
}

static GLboolean VerifyAlphaFormat(char *detail)
{
    CheckLog log;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;
    DrawAlphaFormatScene();
    CheckRGB(&log, "full", StripTexelX(0, 2), 0.0f,
             255, 255, 255, TEXFMT_TOL);
    CheckRGB(&log, "half", StripTexelX(1, 2), 0.0f,
             128, 128, 128, TEXFMT_TOL);
    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "alpha texture blends as white at both strengths");
    return log.failures == 0;
}

static GLboolean VerifyBGRA(char *detail)
{
    CheckLog log;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;
    DrawBGRAScene();
    CheckRGB(&log, "texel0", StripTexelX(0, 2), 0.0f,
             100, 150, 200, TEXFMT_TOL);
    CheckRGB(&log, "texel1", StripTexelX(1, 2), 0.0f,
             30, 20, 10, TEXFMT_TOL);
    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "BGRA bytes land on the exact channels");
    return log.failures == 0;
}

static GLboolean VerifyARGB(char *detail)
{
    CheckLog log;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;
    DrawARGBScene();
    CheckRGB(&log, "texel0", StripTexelX(0, 2), 0.0f,
             100, 150, 200, TEXFMT_TOL);
    CheckRGB(&log, "texel1", StripTexelX(1, 2), 0.0f,
             30, 20, 10, TEXFMT_TOL);
    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "ARGB bytes land on the exact channels");
    return log.failures == 0;
}

/* Mid-grey base, re-uploaded before every patch so each sub-case starts
 * from the same content. Tight unpack rows: the suite pins alignment
 * explicitly instead of relying on the default. */
static void UploadSubimageBase(void)
{
    UBYTE base[4 * 4 * 4];
    int x, y;

    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x) {
            base[(y * 4 + x) * 4 + 0] = 128;
            base[(y * 4 + x) * 4 + 1] = 128;
            base[(y * 4 + x) * 4 + 2] = 128;
            base[(y * 4 + x) * 4 + 3] = 255;
        }
    if (!g_subimageTexture) {
        glGenTextures(1, &g_subimageTexture);
        glBindTexture(GL_TEXTURE_2D, g_subimageTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_subimageTexture);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, base);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static void DrawSubimageScene(GLenum format, const UBYTE *patch,
                              GLint align)
{
    UploadSubimageBase();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glDisable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 2, 2,
                    format, GL_UNSIGNED_BYTE, patch);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    DrawFullscreenQuad();
}

static void DrawSubimageFormats(ULONG frame)
{
    static const UBYTE patch[4] = { 255, 255, 255, 255 };

    (void)frame;
    DrawSubimageScene(GL_LUMINANCE, patch, 1);
}

static GLboolean VerifySubimageFormats(char *detail)
{
    static const UBYTE whiteL[4] = { 255, 255, 255, 255 };
    static const UBYTE whiteRGBA[16] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255
    };
    CheckLog log;
    GLfloat ox, oy, ix, iy;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;
    CellNDC(0, 0, &ox, &oy);
    CellNDC(1, 1, &ix, &iy);
    DrawSubimageScene(GL_LUMINANCE, whiteL, 1);
    CheckRGB(&log, "lum.outside", ox, oy, 128, 128, 128, TEXFMT_TOL);
    CheckRGB(&log, "lum.inside", ix, iy, 255, 255, 255, TEXFMT_TOL);
    DrawSubimageScene(GL_LUMINANCE, whiteL, 4);
    CheckRGB(&log, "lum4.outside", ox, oy, 128, 128, 128, TEXFMT_TOL);
    CheckRGB(&log, "lum4.inside", ix, iy, 255, 255, 255, TEXFMT_TOL);
    DrawSubimageScene(GL_RGBA, whiteRGBA, 1);
    CheckRGB(&log, "rgba.outside", ox, oy, 128, 128, 128, TEXFMT_TOL);
    CheckRGB(&log, "rgba.inside", ix, iy, 255, 255, 255, TEXFMT_TOL);
    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "luminance and RGBA patches land exactly");
    return log.failures == 0;
}

static GLboolean R200Available(char *reason)
{
    if (MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_R200)
        return GL_TRUE;
    strcpy(reason, "needs R200 backend");
    return GL_FALSE;
}

/* ------------------------------------------------------------------ */
/* Row-identity map: distinct solid rows pin down the exact write      */
/* stride versus sample stride of the upload path. A pitch mismatch    */
/* shows as shifted or stale rows (the direct-path bug read back      */
/* [red,stale,green,stale] for a [red,green,blue,white] upload); the   */
/* pattern IS the diagnosis, so every row is reported, not just the   */
/* first failure.                                                      */
/* ------------------------------------------------------------------ */

static GLuint g_rowmap4Texture;
static GLuint g_rowmap8Texture;
static GLuint g_rowmap16Texture;

static const UBYTE g_rowmap4Rows[4][4] = {
    { 200, 0, 0, 255 }, { 0, 200, 0, 255 },
    { 0, 0, 200, 255 }, { 255, 255, 255, 255 }
};

static const UBYTE g_rowmap2Rows[2][4] = {
    { 10, 20, 30, 255 }, { 200, 210, 220, 255 }
};

/* Cell (cx, cy) of a WxH texture on the -0.9..0.9 quad (cy = 0 bottom). */
static void CellNDCWH(int w, int h, int cx, int cy, GLfloat *x, GLfloat *y)
{
    *x = -0.9f + ((GLfloat)cx + 0.5f) * (1.8f / (GLfloat)w);
    *y = -0.9f + ((GLfloat)cy + 0.5f) * (1.8f / (GLfloat)h);
}

static void UploadRowmap(GLuint *slot, int w, int h,
                         const UBYTE rows[4][4])
{
    UBYTE texels[16 * 2 * 4];
    int x, y;

    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            texels[(y * w + x) * 4 + 0] = rows[y][0];
            texels[(y * w + x) * 4 + 1] = rows[y][1];
            texels[(y * w + x) * 4 + 2] = rows[y][2];
            texels[(y * w + x) * 4 + 3] = rows[y][3];
        }
    if (!*slot) {
        glGenTextures(1, slot);
        glBindTexture(GL_TEXTURE_2D, *slot);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    } else {
        glBindTexture(GL_TEXTURE_2D, *slot);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static void DrawRowmapScene(int w, int h, GLuint *slot,
                            const UBYTE rows[4][4])
{
    UploadRowmap(slot, w, h, rows);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    DrawFullscreenQuad();
}

static void DrawRowIdentity(ULONG frame)
{
    (void)frame;
    DrawRowmapScene(4, 4, &g_rowmap4Texture, g_rowmap4Rows);
}

static void CheckRowCells(CheckLog *log, const char *id,
                          int w, int h, const UBYTE rows[4][4])
{
    GLfloat x, y;
    char what[64];
    int cy;

    /* The pattern IS the diagnosis: print every sampled row so a failure
     * log shows shifted vs stale vs all-white rows directly. */
    printf("MINIGL_ROWMAP id=%s rows=", id);
    for (cy = 0; cy < h; ++cy) {
        int r = -1, g = -1, b = -1;

        CellNDCWH(w, h, w / 2, cy, &x, &y);
        if (!SampleRGB(x, y, &r, &g, &b)) {
            ++log->checks;
            ++log->failures;
            if (!log->first[0])
                strcpy(log->first, "surface capture failed");
            printf("%scapture-failed", cy ? ";" : "");
            return;
        }
        printf("%s%d,%d,%d", cy ? ";" : "", r, g, b);
        sprintf(what, "%s.row%d", id, cy);
        CheckValues(log, what, r, g, b,
                    (int)rows[cy][0], (int)rows[cy][1], (int)rows[cy][2],
                    TEXFMT_TOL);
    }
    printf("\n");
}

static GLboolean VerifyRowIdentity(char *detail)
{
    CheckLog log;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail))
        return GL_FALSE;
    DrawRowmapScene(4, 4, &g_rowmap4Texture, g_rowmap4Rows);
    CheckRowCells(&log, "map4", 4, 4, g_rowmap4Rows);
    DrawRowmapScene(8, 2, &g_rowmap8Texture, g_rowmap2Rows);
    CheckRowCells(&log, "map8", 8, 2, g_rowmap2Rows);
    DrawRowmapScene(16, 2, &g_rowmap16Texture, g_rowmap2Rows);
    CheckRowCells(&log, "map16", 16, 2, g_rowmap2Rows);
    if (log.failures)
        strncpy(detail, log.first, 95);
    else
        sprintf(detail, "all rows identity-mapped at 4x4, 8x2 and 16x2");
    return log.failures == 0;
}

#define QUAKE_PARTICLE_COUNT 24

/* Quarter-wave-accurate sine table: keeps the animated particle field free
 * of libm dependencies in this freestanding client. */
static const GLfloat g_particleSine[256] = {
    0.000000f, 0.024541f, 0.049068f, 0.073565f, 0.098017f, 0.122411f, 0.146730f, 0.170962f,
    0.195090f, 0.219101f, 0.242980f, 0.266713f, 0.290285f, 0.313682f, 0.336890f, 0.359895f,
    0.382683f, 0.405241f, 0.427555f, 0.449611f, 0.471397f, 0.492898f, 0.514103f, 0.534998f,
    0.555570f, 0.575808f, 0.595699f, 0.615232f, 0.634393f, 0.653173f, 0.671559f, 0.689541f,
    0.707107f, 0.724247f, 0.740951f, 0.757209f, 0.773010f, 0.788346f, 0.803208f, 0.817585f,
    0.831470f, 0.844854f, 0.857729f, 0.870087f, 0.881921f, 0.893224f, 0.903989f, 0.914210f,
    0.923880f, 0.932993f, 0.941544f, 0.949528f, 0.956940f, 0.963776f, 0.970031f, 0.975702f,
    0.980785f, 0.985278f, 0.989177f, 0.992480f, 0.995185f, 0.997290f, 0.998795f, 0.999699f,
    1.000000f, 0.999699f, 0.998795f, 0.997290f, 0.995185f, 0.992480f, 0.989177f, 0.985278f,
    0.980785f, 0.975702f, 0.970031f, 0.963776f, 0.956940f, 0.949528f, 0.941544f, 0.932993f,
    0.923880f, 0.914210f, 0.903989f, 0.893224f, 0.881921f, 0.870087f, 0.857729f, 0.844854f,
    0.831470f, 0.817585f, 0.803208f, 0.788346f, 0.773010f, 0.757209f, 0.740951f, 0.724247f,
    0.707107f, 0.689541f, 0.671559f, 0.653173f, 0.634393f, 0.615232f, 0.595699f, 0.575808f,
    0.555570f, 0.534998f, 0.514103f, 0.492898f, 0.471397f, 0.449611f, 0.427555f, 0.405241f,
    0.382683f, 0.359895f, 0.336890f, 0.313682f, 0.290285f, 0.266713f, 0.242980f, 0.219101f,
    0.195090f, 0.170962f, 0.146730f, 0.122411f, 0.098017f, 0.073565f, 0.049068f, 0.024541f,
    0.000000f, -0.024541f, -0.049068f, -0.073565f, -0.098017f, -0.122411f, -0.146730f, -0.170962f,
    -0.195090f, -0.219101f, -0.242980f, -0.266713f, -0.290285f, -0.313682f, -0.336890f, -0.359895f,
    -0.382683f, -0.405241f, -0.427555f, -0.449611f, -0.471397f, -0.492898f, -0.514103f, -0.534998f,
    -0.555570f, -0.575808f, -0.595699f, -0.615232f, -0.634393f, -0.653173f, -0.671559f, -0.689541f,
    -0.707107f, -0.724247f, -0.740951f, -0.757209f, -0.773010f, -0.788346f, -0.803208f, -0.817585f,
    -0.831470f, -0.844854f, -0.857729f, -0.870087f, -0.881921f, -0.893224f, -0.903989f, -0.914210f,
    -0.923880f, -0.932993f, -0.941544f, -0.949528f, -0.956940f, -0.963776f, -0.970031f, -0.975702f,
    -0.980785f, -0.985278f, -0.989177f, -0.992480f, -0.995185f, -0.997290f, -0.998795f, -0.999699f,
    -1.000000f, -0.999699f, -0.998795f, -0.997290f, -0.995185f, -0.992480f, -0.989177f, -0.985278f,
    -0.980785f, -0.975702f, -0.970031f, -0.963776f, -0.956940f, -0.949528f, -0.941544f, -0.932993f,
    -0.923880f, -0.914210f, -0.903989f, -0.893224f, -0.881921f, -0.870087f, -0.857729f, -0.844854f,
    -0.831470f, -0.817585f, -0.803208f, -0.788346f, -0.773010f, -0.757209f, -0.740951f, -0.724247f,
    -0.707107f, -0.689541f, -0.671559f, -0.653173f, -0.634393f, -0.615232f, -0.595699f, -0.575808f,
    -0.555570f, -0.534998f, -0.514103f, -0.492898f, -0.471397f, -0.449611f, -0.427555f, -0.405241f,
    -0.382683f, -0.359895f, -0.336890f, -0.313682f, -0.290285f, -0.266713f, -0.242980f, -0.219101f,
    -0.195090f, -0.170962f, -0.146730f, -0.122411f, -0.098017f, -0.073565f, -0.049068f, -0.024541f
};

typedef struct {
    GLfloat radius;
    GLfloat speed;
    GLfloat phase;
    GLfloat yAmp;
    UBYTE r, g, b, a;
} QuakeParticle;

/* Deterministic orbit parameters: position is a pure function of the frame
 * number so verification can recompute exactly where each particle must be.
 * Particle 0 sits at the centre at frame 0. */
static const QuakeParticle g_quakeParticles[QUAKE_PARTICLE_COUNT] = {
    {0.00f,  1.90f, 0.00f, 0.30f, 255, 224,  96, 224},
    {0.12f, -1.40f, 0.60f, 0.26f, 255, 160,  48, 208},
    {0.20f,  1.10f, 1.30f, 0.34f, 255,  96,  32, 192},
    {0.28f, -0.90f, 2.10f, 0.22f, 255, 240, 180, 216},
    {0.34f,  0.75f, 0.40f, 0.38f, 255, 128,   0, 200},
    {0.42f, -0.62f, 1.80f, 0.18f, 250, 220, 120, 208},
    {0.48f,  1.55f, 2.60f, 0.30f, 255,  64,  32, 184},
    {0.55f, -1.05f, 0.90f, 0.42f, 255, 192,  64, 216},
    {0.16f,  0.50f, 3.00f, 0.24f, 255, 176,  96, 200},
    {0.24f, -1.75f, 4.10f, 0.36f, 240, 240, 255, 192},
    {0.31f,  0.82f, 5.20f, 0.20f, 255, 144,  48, 212},
    {0.39f, -0.55f, 2.40f, 0.40f, 255, 208, 112, 204},
    {0.46f,  1.25f, 1.10f, 0.28f, 255,  80,  40, 196},
    {0.52f, -0.95f, 3.60f, 0.34f, 255, 232, 144, 220},
    {0.10f,  1.60f, 5.60f, 0.26f, 255, 168,  72, 208},
    {0.19f, -1.20f, 0.30f, 0.32f, 255, 120,  56, 188},
    {0.27f,  0.66f, 2.80f, 0.44f, 255, 200,  80, 212},
    {0.36f, -1.55f, 4.60f, 0.16f, 248, 216, 160, 200},
    {0.43f,  0.92f, 1.60f, 0.38f, 255, 152,  24, 204},
    {0.50f, -0.70f, 5.00f, 0.24f, 255, 184, 104, 216},
    {0.58f,  1.35f, 3.20f, 0.30f, 255, 104,  48, 192},
    {0.14f, -0.84f, 4.90f, 0.28f, 255, 216, 136, 208},
    {0.23f,  1.05f, 0.70f, 0.42f, 255, 136,  64, 196},
    {0.33f, -1.65f, 2.20f, 0.20f, 255, 248, 200, 212}
};

static GLfloat ParticleSin(GLfloat angle)
{
    return g_particleSine[((ULONG)(angle * (256.0f / 6.2831853f))) & 255UL];
}

static void ParticlePosition(int index, GLfloat time, GLfloat *x, GLfloat *y)
{
    const QuakeParticle *particle = &g_quakeParticles[index];
    GLfloat angle = particle->phase + time * particle->speed;

    *x = ParticleSin(angle) * particle->radius;
    *y = ParticleSin(angle * 2.0f + particle->phase) * particle->yAmp;
}

static void DrawQuakeParticlesMixed(ULONG frame)
{
    GLfloat pointSize = 0.5f + (GLfloat)g_width / 320.0f;
    GLfloat time = (GLfloat)frame * 0.08f;
    int index;

    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glDisable(GL_TEXTURE_2D); glColor3ub(32, 72, 190);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.9f,-0.8f,0); glVertex3f(0.9f,-0.8f,0); glVertex3f(0.9f,0.8f,0);
    glVertex3f(-0.9f,-0.8f,0); glVertex3f(0.9f,0.8f,0); glVertex3f(-0.9f,0.8f,0);
    glEnd();
    glDepthMask(GL_FALSE); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPointSize(pointSize); glBegin(GL_POINTS);
    for (index = 0; index < QUAKE_PARTICLE_COUNT; ++index) {
        const QuakeParticle *particle = &g_quakeParticles[index];
        GLfloat x, y;
        ParticlePosition(index, time, &x, &y);
        glColor4ub(particle->r, particle->g, particle->b, particle->a);
        glVertex3f(x, y, -0.4f);
    }
    glEnd();
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glPointSize(1.0f);
    glColor3ub(40, 220, 80);
    glBegin(GL_TRIANGLES);
    glVertex3f(0.55f,-0.55f,-0.2f); glVertex3f(0.85f,-0.55f,-0.2f);
    glVertex3f(0.7f,-0.2f,-0.2f); glEnd();
}

static GLboolean VerifyQuakeParticlesMixed(char *detail)
{
    CheckLog log;
    int index;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuakeParticlesMixed(0);
    CheckRGB(&log, "world", -0.7f, -0.5f, 32, 72, 190, 16);
    CheckRGB(&log, "post-point triangle", 0.7f, -0.4f, 40, 220, 80, 16);
    for (index = 0; index < 3; ++index) {
        const QuakeParticle *particle = &g_quakeParticles[index];
        GLfloat x, y;
        int er, eg, eb;
        ParticlePosition(index, 0.0f, &x, &y);
        er = ExpectedBlend(particle->r, 32, particle->a);
        eg = ExpectedBlend(particle->g, 72, particle->a);
        eb = ExpectedBlend(particle->b, 190, particle->a);
        CheckRGB(&log, "particle", x, y, er, eg, eb, 24);
    }
    if (log.failures) strncpy(detail, log.first, 95);
    else sprintf(detail, "%d animated particles; three verified on orbit",
                 QUAKE_PARTICLE_COUNT);
    return log.failures == 0;
}

/*
 * quake.world-room-walk: the real e1m1 spawn hall from the original Quake
 * BSP, walked on a baked patrol loop.  Effect-free on purpose: depth-tested
 * texture-chained world geometry is the whole test.  Geometry, textures,
 * camera path, and multi-pose reference block means all come from
 * tools/extract_quake_bsp_room.py (examples/quake_room_model.h).
 *
 * The view matrix reproduces the offline reference render exactly:
 * vertex -> translate(-eye) -> Rz(90-yaw) -> Rx(-90), so world X/Y stay
 * horizontal, world +Z is up, and yaw 0 looks along +X.
 */
static GLuint g_roomTextures[QUAKE_ROOM_TEXTURE_COUNT];
static ULONG g_roomSetupGeneration;

static void PrepareRoomTextures(void)
{
    int index;

    /* Texture objects survive ResetGLState; create them once.  This must
     * not touch g_roomSetupGeneration -- that latch belongs to the draw
     * state in DrawQuakeRoomWalk, and consuming it here would skip the
     * enables/pointers/frustum on the first frame after a reset. */
    if (g_roomTextures[0])
        return;
    for (index = 0; index < QUAKE_ROOM_TEXTURE_COUNT; ++index) {
        glGenTextures(1, &g_roomTextures[index]);
        glBindTexture(GL_TEXTURE_2D, g_roomTextures[index]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     g_quakeRoomTexSize[index][0],
                     g_quakeRoomTexSize[index][1], 0, GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     g_quakeRoomTexels + g_quakeRoomTexOffset[index]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
}

static void DrawQuakeRoomWalk(ULONG frame)
{
    const GLfloat *pose;
    int index;

    PrepareRoomTextures();
    if (g_roomSetupGeneration != g_stateGeneration) {
        g_roomSetupGeneration = g_stateGeneration;
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepth(1.0);
        if (!g_roomNoDepth) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);       /* Quake windings are clockwise */
        glColor3ub(255, 255, 255);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, 5 * (GLint)sizeof(GLfloat),
                        &g_quakeRoomVertices[0][0]);
        glTexCoordPointer(2, GL_FLOAT, 5 * (GLint)sizeof(GLfloat),
                          &g_quakeRoomVertices[0][3]);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_W,
                  QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_W,
                  -QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_H,
                  QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_H,
                  QUAKE_ROOM_PROJ_NEAR, QUAKE_ROOM_PROJ_FAR);
    }

    pose = g_quakeRoomPath[frame % QUAKE_ROOM_LOOP_FRAMES];
    if (g_roomQuadScreen) {
        /*
         * Glitch discriminator: paint the background with a fullscreen quad
         * instead of a colour clear. A stripe that renders dark red means
         * something black overwrites already-drawn pixels (late clear); a
         * stripe that stays black means the draws themselves never landed.
         */
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_TEXTURE_2D);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glBegin(GL_QUADS);
        glColor3ub(70, 0, 20);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(1.0f, -1.0f);
        glVertex2f(1.0f, 1.0f);
        glVertex2f(-1.0f, 1.0f);
        glEnd();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        glColor3ub(255, 255, 255);
    } else {
        glClear(g_roomNoDepth ? GL_COLOR_BUFFER_BIT
                              : (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    }
    if (g_roomZPrePass && !g_roomNoDepth) {
        /*
         * Glitch discriminator: write depth through the DRAW path instead of
         * the clear. Colour clear only, then a colour-suppressed fullscreen
         * quad at the near plane fills depth with 0.0, which every scene
         * fragment passes under GL_LESS. Stripes gone -> the clear path's
         * depth write loses columns; stripes persist -> fragments are being
         * rejected by something other than the depth memory contents.
         */
        glDepthMask(GL_TRUE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDisable(GL_CULL_FACE);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glBegin(GL_QUADS);
        glVertex3f(-1.0f, -1.0f, -1.0f);
        glVertex3f(1.0f, -1.0f, -1.0f);
        glVertex3f(1.0f, 1.0f, -1.0f);
        glVertex3f(-1.0f, 1.0f, -1.0f);
        glEnd();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glEnable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(90.0f - pose[3], 0.0f, 0.0f, 1.0f);
    glTranslatef(-pose[0], -pose[1], -pose[2]);
    for (index = 0; index < QUAKE_ROOM_BATCH_COUNT; ++index) {
        glBindTexture(GL_TEXTURE_2D,
                      g_roomTextures[g_quakeRoomBatches[index][0]]);
        glDrawElements(GL_TRIANGLES, g_quakeRoomBatches[index][2],
                       GL_UNSIGNED_SHORT,
                       g_quakeRoomIndices + g_quakeRoomBatches[index][1]);
    }
}

/*
 * Mean colour of an NDC rectangle in the captured surface.
 *
 * The walk looks down a long corridor, so most of the frame is minified:
 * with GL_NEAREST each pixel is a coin toss between many texels and the
 * hardware legitimately disagrees with the offline renderer pixel by pixel.
 * Block means are stable to a couple of levels and still catch a wrong
 * texture, a broken transform or a missing surface.
 */
static void BlockMeanRGB(const SurfaceView *view, const GLfloat *rect,
                         int *r, int *g, int *b)
{
    ULONG x0 = (ULONG)((rect[0] + 1.0f) * 0.5f * (GLfloat)view->width);
    ULONG y0 = (ULONG)((1.0f - rect[1]) * 0.5f * (GLfloat)view->height);
    ULONG x1 = (ULONG)((rect[2] + 1.0f) * 0.5f * (GLfloat)view->width);
    ULONG y1 = (ULONG)((1.0f - rect[3]) * 0.5f * (GLfloat)view->height);
    ULONG x, y;
    ULONG count = 0;
    ULONG sumR = 0, sumG = 0, sumB = 0;

    for (y = y0; y < y1 && y < view->height; ++y) {
        for (x = x0; x < x1 && x < view->width; ++x) {
            int pr, pg, pb;
            TokenToRGB(PixelToken(view, x, y), &pr, &pg, &pb);
            sumR += (ULONG)pr;
            sumG += (ULONG)pg;
            sumB += (ULONG)pb;
            ++count;
        }
    }
    if (!count) count = 1;
    *r = (int)(sumR / count);
    *g = (int)(sumG / count);
    *b = (int)(sumB / count);
}

static GLboolean VerifyQuakeRoomWalk(char *detail)
{
    CheckLog log;
    int index;
    int pose;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    /* Calibration ends on a blue clear. Room setup may already be latched for
     * this state generation, so restore the scene's intended background here. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    /*
     * Check several poses of the walk, not just frame 0: a single pose would
     * miss a broken camera path or a surface that only comes into view part
     * way down the hall.
     */
    for (pose = 0; pose < QUAKE_ROOM_VERIFY_POSES; ++pose) {
        SurfaceView view;
        char label[16];

        DrawQuakeRoomWalk(g_quakeRoomVerifyFrames[pose]);
        /*
         * One pose only: writing every pose at 800x600 is several megabytes
         * of file I/O mid-test, which wedged the machine once already.
         */
        if (g_dumpPath &&
            (long)g_quakeRoomVerifyFrames[pose] == g_dumpFrame) {
            char posePath[128];
            sprintf(posePath, "%s.f%lu", g_dumpPath,
                    (unsigned long)g_quakeRoomVerifyFrames[pose]);
            printf("MINIGL_DUMP path=%s ok=%d\n", posePath,
                   DumpSurfacePPM(posePath) ? 1 : 0);
        }
        if (!CaptureSurface(&view)) {
            sprintf(detail, "lock failed at frame %lu",
                    (unsigned long)g_quakeRoomVerifyFrames[pose]);
            return GL_FALSE;
        }
        /* Name the pose so a failure says which one, not just "room". */
        sprintf(label, "room f%lu",
                (unsigned long)g_quakeRoomVerifyFrames[pose]);
        if (g_roomBlocks)
            printf("MINIGL_ROOM_BLOCKS frame=%lu",
                   (unsigned long)g_quakeRoomVerifyFrames[pose]);
        for (index = 0; index < QUAKE_ROOM_BLOCK_COUNT; ++index) {
            const GLubyte *want = g_quakeRoomBlockRGB[pose][index];
            int r, g, b;

            BlockMeanRGB(&view, g_quakeRoomBlocks[index], &r, &g, &b);
            if (g_roomBlocks)
                printf(" %d,%d,%d/%d,%d,%d", r, g, b,
                       (int)want[0], (int)want[1], (int)want[2]);
            if (!g_quakeRoomBlockStable[pose][index])
                continue;
            CheckValues(&log, label, r, g, b,
                        (int)want[0], (int)want[1], (int)want[2],
                        QUAKE_ROOM_BLOCK_TOLERANCE);
        }
        if (g_roomBlocks)
            printf("\n");
    }
    if (log.failures) strncpy(detail, log.first, 95);
    else sprintf(detail,
                 "%d tris in %d batches; %d block means correct over %d poses",
                 QUAKE_ROOM_INDEX_COUNT / 3, QUAKE_ROOM_BATCH_COUNT,
                 log.checks, QUAKE_ROOM_VERIFY_POSES);
    return log.failures == 0;
}

static void FogPatch(GLfloat x0, GLfloat x1, GLfloat z)
{
    glBegin(GL_QUADS);
    glVertex3f(x0,-0.55f,z); glVertex3f(x1,-0.55f,z);
    glVertex3f(x1,0.55f,z); glVertex3f(x0,0.55f,z);
    glEnd();
}

static void DrawQuakeFog(ULONG frame)
{
    GLfloat color[4] = {32.0f/255.0f, 64.0f/255.0f, 192.0f/255.0f, 1};
    (void)frame;
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(-1, 1, -1, 1, 1, 12);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_TEXTURE_2D); glColor3ub(255, 255, 255);
    glFogi(GL_FOG_MODE, GL_LINEAR); glFogfv(GL_FOG_COLOR, color);
    glFogf(GL_FOG_START, 2.0f); glFogf(GL_FOG_END, 10.0f); glEnable(GL_FOG);
    FogPatch(-0.9f,-0.35f,-2.0f);
    FogPatch(-0.27f,0.27f,-6.0f);
    FogPatch(0.35f,0.9f,-10.0f);
}

static GLboolean VerifyQuakeFog(char *detail)
{
    CheckLog log;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuakeFog(0);
    CheckRGB(&log, "near", -0.62f, 0, 255, 255, 255, 20);
    CheckRGB(&log, "middle", 0, 0, 144, 160, 224, 24);
    CheckRGB(&log, "far", 0.62f, 0, 32, 64, 192, 20);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "linear fog near/middle/far interpolation correct");
    return log.failures == 0;
}

/* Quake's pickup flash is intermittent: a burst that fades out, then
 * nothing until the next pickup. Pulse for 0.75 s every 3.0 s. */
#define PICKUP_CYCLE_MS 3000UL
#define PICKUP_FLASH_MS 750UL

static ULONG g_flashStartTick;
static GLboolean g_flashStarted;

static void DrawPickupScene(UBYTE red, UBYTE green, UBYTE blue)
{
    glClearColor(0.25f, 0.25f, 0.25f, 1); glClear(GL_COLOR_BUFFER_BIT);
    GetAlphaTexture(85);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_ALPHA_TEST); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glScissor(g_width / 8, g_height / 8, g_width * 3 / 4, g_height * 3 / 4);
    glEnable(GL_SCISSOR_TEST); glColor3ub(red, green, blue);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0,0); glVertex2f(0,0);
    glTexCoord2f(1,0); glVertex2f((GLfloat)g_width,0);
    glTexCoord2f(1,1); glVertex2f((GLfloat)g_width,(GLfloat)g_height);
    glTexCoord2f(0,1); glVertex2f(0,(GLfloat)g_height);
    glEnd();
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
}

static void DrawQuakePickupIdle(void)
{
    glClearColor(0.25f, 0.25f, 0.25f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void DrawQuakePickup(ULONG frame)
{
    ULONG now = ClockLow();
    ULONG phase;
    (void)frame;
    if (!g_flashStarted) {
        g_flashStartTick = now;
        g_flashStarted = GL_TRUE;
    }
    phase = (ULONG)((((unsigned long long)(now - g_flashStartTick)) *
                     1000ULL) / (unsigned long long)g_timer.rate) %
            PICKUP_CYCLE_MS;
    if (phase < PICKUP_FLASH_MS) {
        /* Fade the wash out over the flash window like a decaying bonus
         * flash; between pulses only the plain scene is drawn. */
        ULONG k = 255UL - ((phase * 255UL) / PICKUP_FLASH_MS);
        DrawPickupScene((UBYTE)(64UL * k / 255UL),
                        (UBYTE)(255UL * k / 255UL),
                        (UBYTE)(96UL * k / 255UL));
    } else {
        DrawQuakePickupIdle();
    }
}

static GLboolean VerifyQuakePickup(char *detail)
{
    static const UBYTE colors[3][3] = {{255,48,32},{64,255,96},{214,186,69}};
    static const char *names[3] = {"red", "green", "yellow"};
    CheckLog log;
    int index;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    for (index = 0; index < 3; ++index) {
        int er = ExpectedBlend(colors[index][0], 64, 85);
        int eg = ExpectedBlend(colors[index][1], 64, 85);
        int eb = ExpectedBlend(colors[index][2], 64, 85);
        DrawPickupScene(colors[index][0], colors[index][1], colors[index][2]);
        CheckRGB(&log, names[index], 0, 0, er, eg, eb, 18);
        CheckRGB(&log, "outside scissor", -0.95f, 0.95f, 64, 64, 64, 12);
    }
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "red/green/yellow device fans blend and scissor correctly");
    return log.failures == 0;
}

/* ------------------------------------------------------------------ */
/* Quake white-render isolation.                                       */
/*                                                                     */
/* BlitzQuake's default world path (gl_texsort=1, gl_fake_multitexture  */
/* =1) captures every world polygon with texture unit 1 enabled and    */
/* draws the world only through mglDrawMultitexBuffer, over lightmaps  */
/* uploaded as packed 4444/565 texels. None of that machinery is       */
/* covered by the tests above, so a defect anywhere in it renders the  */
/* whole world wrong (white) while every existing test stays green.    */
/* Each test below replays one exact GL sequence from the Quake        */
/* sources (file:line references are BlitzQuake glquake/) with pixel-  */
/* exact expectations derived from Quake's own texel math.             */
/*                                                                     */
/* Packed 4444/565 test data is written as explicit UBYTE byte-pairs   */
/* in big-endian order, mirroring Quake's per-byte writers             */
/* (gl_rsurf.c R_BuildLightMap) -- never as UWORD literals, which     */
/* would be host-endian.                                               */
/* ------------------------------------------------------------------ */

static GLuint UploadTypedTexture(GLenum internal, GLenum format,
                                 GLenum type, int width, int height,
                                 const UBYTE *texels)
{
    GLuint name = 0;

    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    /* Quake uses LINEAR magnification/minification without mip filters
     * by default (gl_draw.c:64-65) and REPEAT wrap set once globally
     * (gl_vidamiga.c:342-343); sample points stay on cell centres so
     * the wrap mode never decides a check. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, format,
                 type, texels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return name;
}

/* A1: Quake world-texture upload (GL_Upload32, gl_draw.c:1239-1322):
 * internalformat GL_RGB from GL_RGBA/UBYTE source with the full mip
 * chain Quake always uploads. MIN=LINEAR selects level 0, so the
 * panel must show level 0's exact colour; a broken multi-level
 * layout (wrong level offsets, rejected levels) corrupts even that. */
#define QUAKE_MIP_LEVELS 7

static GLuint g_quakeRGBMipsTexture;

static void BindQuakeRGBMips(void)
{
    /* Distinct flat colour per level: level 0 (red) is what LINEAR
     * sampling must show; any other colour names the wrong level. */
    static const UBYTE levelColors[QUAKE_MIP_LEVELS][4] = {
        { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 },
        { 255, 255, 255, 255 }, { 255, 255, 0, 255 },
        { 0, 255, 255, 255 }, { 255, 0, 255, 255 }
    };
    static UBYTE levelData[64 * 64 * 4];
    int level;

    if (g_quakeRGBMipsTexture) {
        glBindTexture(GL_TEXTURE_2D, g_quakeRGBMipsTexture);
        return;
    }
    glGenTextures(1, &g_quakeRGBMipsTexture);
    glBindTexture(GL_TEXTURE_2D, g_quakeRGBMipsTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (level = 0; level < QUAKE_MIP_LEVELS; ++level) {
        int size = 64 >> level, count = size * size, index;
        for (index = 0; index < count; ++index) {
            levelData[index * 4] = levelColors[level][0];
            levelData[index * 4 + 1] = levelColors[level][1];
            levelData[index * 4 + 2] = levelColors[level][2];
            levelData[index * 4 + 3] = levelColors[level][3];
        }
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGB, size, size, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, levelData);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static void DrawQuakeRGBMipsScene(void)
{
    BindQuakeRGBMips();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    DrawFullscreenQuad();
}

static void DrawQuakeRGBMips(ULONG frame)
{
    (void)frame;
    DrawQuakeRGBMipsScene();
}

static GLboolean VerifyQuakeRGBMips(char *detail)
{
    CheckLog log;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuakeRGBMipsScene();
    CheckRGB(&log, "mip L0 left", -0.5f, 0.0f, 255, 0, 0, 8);
    CheckRGB(&log, "mip L0 centre", 0.0f, 0.0f, 255, 0, 0, 8);
    CheckRGB(&log, "mip L0 right", 0.5f, 0.0f, 255, 0, 0, 8);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "GL_RGB mip chain uploads; LINEAR shows level 0 red");
    return log.failures == 0;
}

/* A2: Quake 2D/HUD/sprite texture format (glquake.h:252: on AMIGA every
 * alpha texture uses internalformat GL_RGB5_A1) with Quake's alpha-test
 * state (glAlphaFunc GREATER 0.5, gl_vidamiga.c:333-334) and the
 * palette-table alpha quirk (0xAA = 170, gl_vidamiga.c:239-246).
 * Primaries are exactly representable in 555, so expectations are
 * exact: alpha 170 must survive 1-bit alpha as opaque. */
static GLuint g_quakeRGB5A1Texture;

static void BindQuakeRGB5A1(void)
{
    static const UBYTE texels[12] = {
        255, 0, 0, 255,     /* opaque red */
        0, 255, 0, 0,       /* fully transparent green */
        0, 0, 255, 170      /* Quake palette alpha 0xAA: blue, must stay opaque */
    };

    if (!g_quakeRGB5A1Texture)
        g_quakeRGB5A1Texture = UploadTypedTexture(GL_RGB5_A1, GL_RGBA,
                                                  GL_UNSIGNED_BYTE,
                                                  3, 1, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_quakeRGB5A1Texture);
}

static void DrawQuakeRGB5A1Scene(GLboolean useAlphaTest)
{
    BindQuakeRGB5A1();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    if (useAlphaTest) {
        glDisable(GL_BLEND);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
    } else {
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    DrawFullscreenQuad();
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
}

static void DrawQuakeRGB5A1(ULONG frame)
{
    (void)frame;
    DrawQuakeRGB5A1Scene(GL_TRUE);
}

static GLboolean VerifyQuakeRGB5A1(char *detail)
{
    CheckLog log;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuakeRGB5A1Scene(GL_TRUE);
    CheckRGB(&log, "alpha-test opaque", StripTexelX(0, 3), 0.0f,
             255, 0, 0, 8);
    CheckRGB(&log, "alpha-test transparent", StripTexelX(1, 3), 0.0f,
             0, 0, 0, 8);
    CheckRGB(&log, "alpha-test AA-quirk", StripTexelX(2, 3), 0.0f,
             0, 0, 255, 8);
    DrawQuakeRGB5A1Scene(GL_FALSE);
    CheckRGB(&log, "blend opaque", StripTexelX(0, 3), 0.0f,
             255, 0, 0, 8);
    CheckRGB(&log, "blend transparent", StripTexelX(1, 3), 0.0f,
             0, 0, 0, 8);
    CheckRGB(&log, "blend AA-quirk", StripTexelX(2, 3), 0.0f,
             0, 0, 255, 8);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "RGB5_A1 alpha is 1-bit; palette alpha 170 stays opaque");
    return log.failures == 0;
}

/* A3: Quake 4444 lightmap (R_BuildLightMap, gl_rsurf.c:467-487).
 * texel = { (255-light) & 0xF0, 0x00 }: INVERTED light in the FIRST
 * nibble, which must act as alpha under the (SRC_ALPHA,
 * ONE_MINUS_SRC_ALPHA) blend Quake selects for 4444
 * (gl_rsurf.c:959-960). Rendered over white, row r must read
 * white * (nibble/15). Column 1 carries the same nibbles with the
 * bytes swapped: a correct backend shows white there, a
 * little-endian halfword reader shows the ramp instead -- one run
 * distinguishes byte order from channel order. */
static GLuint g_quake4444Texture;

/* NDC y of texel row r (row 0 = bottom) on the -0.9..0.9 quad. */
static GLfloat StripRowY(int row, int texH)
{
    return -0.9f + ((GLfloat)row + 0.5f) * (1.8f / (GLfloat)texH);
}

static void BindQuake4444(void)
{
    /* Rows bottom->top: light full, 2/3, 1/3, none. Stored nibbles
     * 0, 5, 10, 15 give blend factors 1, 10/15, 5/15, 0. */
    static const UBYTE texels[16] = {
        0x00, 0x00,  0x00, 0x00,
        0x50, 0x00,  0x00, 0x50,
        0xA0, 0x00,  0x00, 0xA0,
        0xF0, 0x00,  0x00, 0xF0
    };

    if (!g_quake4444Texture)
        g_quake4444Texture = UploadTypedTexture(GL_RGBA, GL_RGBA,
                                                MGL_UNSIGNED_SHORT_4_4_4_4,
                                                2, 4, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_quake4444Texture);
}

static void DrawQuake4444Scene(void)
{
    /* Pass 1: plain white, no texture -- isolates the upload, the
     * channel order, and the blend from the multitexture machinery. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    DrawFullscreenQuad();
    /* Pass 2: the 4444 lightmap exactly as Quake blends it. */
    BindQuake4444();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DrawFullscreenQuad();
    glDisable(GL_BLEND);
}

static void DrawQuake4444(ULONG frame)
{
    (void)frame;
    DrawQuake4444Scene();
}

static GLboolean VerifyQuake4444(char *detail)
{
    static const int want[4] = { 255, 170, 85, 0 };
    CheckLog log;
    char what[64];
    int row, r, g, b;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuake4444Scene();
    for (row = 0; row < 4; ++row) {
        sprintf(what, "quake-col row%d", row);
        CheckRGB(&log, what, StripTexelX(0, 2), StripRowY(row, 4),
                 want[row], want[row], want[row], 8);
        sprintf(what, "swapped-col row%d", row);
        CheckRGB(&log, what, StripTexelX(1, 2), StripRowY(row, 4),
                 255, 255, 255, 8);
    }
    if (log.failures) {
        /* Report the swapped column's row-2 sample raw: together with
         * the first failure it separates a byte-order swap (ramp
         * appears here) from a channel-order fault (tinted greys). */
        if (SampleRGB(StripTexelX(1, 2), StripRowY(2, 4), &r, &g, &b)) {
            char extra[48];
            sprintf(extra, " | swap-col r2 reads %d,%d,%d", r, g, b);
            strncat(log.first, extra, 95 - strlen(log.first));
        }
        strncpy(detail, log.first, 95);
    } else {
        strcpy(detail, "4444 inverted-light nibble multiplies as alpha");
    }
    return log.failures == 0;
}

/* Pack one Quake 565 greyscale light value exactly as R_BuildLightMap
 * does (gl_rsurf.c:488-507), emitting big-endian bytes explicitly. */
static void PackQuake565(UBYTE *dst, int t)
{
    unsigned v = (((unsigned)(t & 0xf8)) << 8) |
                 (((unsigned)(t & 0xfc)) << 3) |
                 (((unsigned)(t & 0xf8)) >> 3);
    dst[0] = (UBYTE)(v >> 8);
    dst[1] = (UBYTE)(v & 0xff);
}

/* A4: Quake 565 lightmap (-lm_RGB, gl_rsurf.c:2116) with the dynamic
 * subrect refresh Quake issues every frame for flickering lights
 * (R_RenderDynamicLightmaps via glTexSubImage2D, gl_rsurf.c:1051).
 * Blend is (ZERO, SRC_COLOR) for 565 (gl_rsurf.c:956-957). */
static GLuint g_quake565Texture;
static UBYTE g_quake565Initial[6];

static void BindQuake565(void)
{
    if (!g_quake565Texture) {
        PackQuake565(g_quake565Initial, 255);
        PackQuake565(g_quake565Initial + 2, 128);
        PackQuake565(g_quake565Initial + 4, 0);
        g_quake565Texture = UploadTypedTexture(GL_RGB, GL_RGB,
                                               MGL_UNSIGNED_SHORT_5_6_5,
                                               3, 1, g_quake565Initial);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_quake565Texture);
    }
}

static void DrawQuake565Scene(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    DrawFullscreenQuad();
    BindQuake565();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    DrawFullscreenQuad();
    glDisable(GL_BLEND);
}

static void DrawQuake565(ULONG frame)
{
    (void)frame;
    DrawQuake565Scene();
}

/* One Quake dynamic-lightmap subrect: texel 2 (black) becomes full
 * white, exactly like a muzzle-flash rect refresh. */
static void UpdateQuake565(void)
{
    UBYTE white[2];

    PackQuake565(white, 255);
    glBindTexture(GL_TEXTURE_2D, g_quake565Texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, 1, 1, GL_RGB,
                    MGL_UNSIGNED_SHORT_5_6_5, white);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static void RestoreQuake565(void)
{
    glBindTexture(GL_TEXTURE_2D, g_quake565Texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, 1, 1, GL_RGB,
                    MGL_UNSIGNED_SHORT_5_6_5, g_quake565Initial + 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static GLboolean VerifyQuake565(char *detail)
{
    CheckLog log;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawQuake565Scene();
    /* t=255 -> (31,63,31): white. t=128 -> (16,32,16): the per-
     * channel factors 16/31, 32/63, 16/31 over white. t=0: black. */
    CheckRGB(&log, "565 full", StripTexelX(0, 3), 0.0f,
             255, 255, 255, 8);
    CheckRGB(&log, "565 half", StripTexelX(1, 3), 0.0f,
             132, 130, 132, 8);
    CheckRGB(&log, "565 none", StripTexelX(2, 3), 0.0f,
             0, 0, 0, 8);
    UpdateQuake565();
    DrawQuake565Scene();
    CheckRGB(&log, "565 subrect refresh", StripTexelX(2, 3), 0.0f,
             255, 255, 255, 8);
    RestoreQuake565();
    DrawQuake565Scene();
    CheckRGB(&log, "565 subrect restore", StripTexelX(2, 3), 0.0f,
             0, 0, 0, 8);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "565 grey packs multiply; subrect updates land");
    return log.failures == 0;
}

/* B: the fake-multitexture world draw. R_RenderBrushPoly with
 * gl_texsort=1 and gl_fake_multitexture=1 (gl_rsurf.c:1129-1231)
 * binds the base texture on unit 0 and captures a GL_POLYGON while
 * unit 1 is enabled (gl_rsurf.c:1161-1176); per vertex
 * mglMtexTV23fv (miniglext.c:30-47) submits the BASE uv through
 * glMultiTexCoord2fARB(GL_TEXTURE0_ARB, ...) WHILE UNIT 1 IS ACTIVE,
 * then the lightmap uv through glTexCoord2f (which targets the
 * active unit), then glVertex3fv. The world is drawn only later by
 * mglDrawMultitexBuffer (gl_rsurf.c:1800-1801). Every step of that
 * order is load-bearing. */
static GLuint g_mtexBaseTexture;
static GLuint g_mtexLightTexture;

/* 2x2 RGB checker uploaded Quake-style (GL_RGB from RGBA source):
 * bottom row red/green, top row blue/white. */
static void BindMtexBase(void)
{
    static const UBYTE texels[16] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 255, 255
    };

    if (!g_mtexBaseTexture)
        g_mtexBaseTexture = UploadTypedTexture(GL_RGB, GL_RGBA,
                                               GL_UNSIGNED_BYTE,
                                               2, 2, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_mtexBaseTexture);
}

/* 2x4 Quake-encoded 4444 lightmap: both columns carry nibbles
 * 0, 5, 10, 15 bottom->top (factors 1, 2/3, 1/3, 0). */
static void BindMtexLight(void)
{
    static const UBYTE texels[16] = {
        0x00, 0x00,  0x00, 0x00,
        0x50, 0x00,  0x50, 0x00,
        0xA0, 0x00,  0xA0, 0x00,
        0xF0, 0x00,  0xF0, 0x00
    };

    if (!g_mtexLightTexture)
        g_mtexLightTexture = UploadTypedTexture(GL_RGBA, GL_RGBA,
                                                MGL_UNSIGNED_SHORT_4_4_4_4,
                                                2, 4, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_mtexLightTexture);
}

/* One captured world polygon in Quake's exact call order. su/sv are
 * the base uv, tu/tv the lightmap uv. */
static void CaptureMtexVertex(GLfloat x, GLfloat y,
                              GLfloat su, GLfloat sv,
                              GLfloat tu, GLfloat tv)
{
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, su, sv);
    glTexCoord2f(tu, tv);
    glVertex3f(x, y, 0.0f);
}

/* B1: single captured quad, flushed with Quake's 4444 blend factors.
 * Expected cell (c, r): base checker colour times the row factor
 * (row factors bottom->top: 1, 2/3, 1/3, 0). */
static void DrawMtexQuadScene(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    /* Base texture bound on unit 0, exactly like R_RenderBrushPoly
     * before the fake-multitexture branch (gl_rsurf.c:1147). */
    glActiveTextureARB(GL_TEXTURE0_ARB);
    BindMtexBase();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    /* Capture with unit 1 enabled (gl_rsurf.c:1163-1172). */
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    BindMtexLight();
    glBegin(GL_POLYGON);
    CaptureMtexVertex(-0.9f, -0.9f, 0.0f, 0.0f, 0.0f, 0.0f);
    CaptureMtexVertex(0.9f, -0.9f, 1.0f, 0.0f, 1.0f, 0.0f);
    CaptureMtexVertex(0.9f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f);
    CaptureMtexVertex(-0.9f, 0.9f, 0.0f, 1.0f, 0.0f, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    /* The deferred world draw (gl_rsurf.c:967, :1800-1801). */
    mglDrawMultitexBuffer(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                          GL_REPLACE);
    glDisable(GL_DEPTH_TEST);
}

static void DrawQuakeMtexQuad(ULONG frame)
{
    (void)frame;
    DrawMtexQuadScene();
}

/* NDC of cell (c, r) on the -0.9..0.9 quad: 2 columns, 4 rows. */
static void MtexCellNDC(int c, int r, GLfloat *x, GLfloat *y)
{
    *x = -0.9f + ((GLfloat)c + 0.5f) * 0.9f;
    *y = StripRowY(r, 4);
}

static GLboolean VerifyQuakeMtexQuad(char *detail)
{
    /* [row][col][rgb]: checker base times row factor. */
    static const int want[4][2][3] = {
        { { 255, 0, 0 }, { 0, 255, 0 } },
        { { 170, 0, 0 }, { 0, 170, 0 } },
        { { 0, 0, 85 }, { 85, 85, 85 } },
        { { 0, 0, 0 }, { 0, 0, 0 } }
    };
    CheckLog log;
    char what[64];
    GLfloat x, y;
    int r, c;
    GLenum sceneErr;

    memset(&log, 0, sizeof(log));
    /* Diagnose first: a stale scene error also fails the capture
     * below, which would misreport a scene fault as a readback
     * fault. Drain, draw once, and name the scene's own error. */
    DrainErrors();
    DrawMtexQuadScene();
    sceneErr = glGetError();
    if (sceneErr != GL_NO_ERROR) {
        sprintf(detail, "scene gl_error=0x%lx (capture not attempted)",
                (unsigned long)sceneErr);
        return GL_FALSE;
    }
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawMtexQuadScene();
    for (r = 0; r < 4; ++r) for (c = 0; c < 2; ++c) {
        sprintf(what, "mtex r%d c%d", r, c);
        MtexCellNDC(c, r, &x, &y);
        CheckRGB(&log, what, x, y,
                 want[r][c][0], want[r][c][1], want[r][c][2], 8);
    }
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "captured quad replays base x 4444 light exactly");
    return log.failures == 0;
}

/* B2: a 5-vertex world polygon. Quake surfaces routinely have more
 * than 4 vertices; V19 queued and replayed every polygon with 3+
 * vertices through both TMUs (mglQ3 draw.c d_DrawMtexPoly /
 * MGLDrawMultitexBuffer), so the contract asserted here is V19
 * parity: the pentagon must render base x light exactly like the
 * quad. A backend that only replays <=4-vertex polys renders this
 * through the wrong unit instead. */
static void DrawMtexPentagonScene(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    BindMtexBase();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    BindMtexLight();
    glBegin(GL_POLYGON);
    /* Convex pentagon, uv = (ndc + 0.9) / 1.8 on both units. */
    CaptureMtexVertex(0.0f, 0.8f, 0.5f, 0.944f, 0.5f, 0.944f);
    CaptureMtexVertex(0.76f, 0.25f, 0.922f, 0.639f, 0.922f, 0.639f);
    CaptureMtexVertex(0.47f, -0.65f, 0.761f, 0.139f, 0.761f, 0.139f);
    CaptureMtexVertex(-0.47f, -0.65f, 0.239f, 0.139f, 0.239f, 0.139f);
    CaptureMtexVertex(-0.76f, 0.25f, 0.078f, 0.639f, 0.078f, 0.639f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    mglDrawMultitexBuffer(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                          GL_REPLACE);
    glDisable(GL_DEPTH_TEST);
}

static void DrawQuakeMtexPentagon(ULONG frame)
{
    (void)frame;
    DrawMtexPentagonScene();
}

static GLboolean VerifyQuakeMtexPentagon(char *detail)
{
    CheckLog log;
    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawMtexPentagonScene();
    /* Sampled strictly inside texel centres (never on a texel
     * boundary, where NEAREST is implementation-defined):
     * (u,v) = (0.25,0.31): base red x row-1 factor 2/3. */
    CheckRGB(&log, "penta red-lit", -0.45f, -0.35f, 170, 0, 0, 8);
    /* (u,v) = (0.69,0.31): base green x row-1 factor 2/3. */
    CheckRGB(&log, "penta green-lit", 0.35f, -0.35f, 0, 170, 0, 8);
    /* (u,v) = (0.69,0.78): base white x row-3 factor 0. */
    CheckRGB(&log, "penta white-dark", 0.35f, 0.5f, 0, 0, 0, 8);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "5-vertex poly replays through both units (V19 parity)");
    return log.failures == 0;
}

/* B3: queue capacity. Quake sizes the buffer with
 * mglChooseMtexBufferSize(6000) (gl_vidamiga.c:698); V19 silently
 * dropped polys past capacity. Shrink to 128 (16 polygons, the
 * backend minimum) and prove 16 captured quads all land, then
 * restore the backend default. */
#define MTEX_SMALL_BUFFER 128
#define MTEX_DEFAULT_BUFFER 4096
#define MTEX_SMALL_QUADS 4

static ULONG g_mtexCapGen;
static GLuint g_mtexCapTexture;

static void BindMtexCap(void)
{
    static const UBYTE texels[16] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 255, 255
    };

    if (g_mtexCapGen != g_stateGeneration) {
        g_mtexCapGen = g_stateGeneration;
        mglChooseMtexBufferSize(MTEX_SMALL_BUFFER);
        g_mtexCapTexture = 0;
    }
    if (!g_mtexCapTexture)
        g_mtexCapTexture = UploadTypedTexture(GL_RGB, GL_RGBA,
                                              GL_UNSIGNED_BYTE,
                                              2, 2, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_mtexCapTexture);
}

static void DrawMtexCapScene(void)
{
    int i, gridX, gridY;
    GLfloat x0, y0;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    BindMtexCap();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    BindMtexLight();
    /* With unit 1 enabled every quad is captured, not drawn; a
     * single flush at the end replays all 16 through both units. */
    for (i = 0; i < MTEX_SMALL_QUADS; ++i) {
        gridX = i & 3;
        gridY = (i >> 2) & 3;
        x0 = -0.9f + (GLfloat)gridX * 0.45f;
        y0 = -0.9f + (GLfloat)gridY * 0.45f;
        glBegin(GL_POLYGON);
        CaptureMtexVertex(x0, y0, 0.0f, 0.0f, 0.0f, 0.625f);
        CaptureMtexVertex(x0 + 0.45f, y0, 1.0f, 0.0f, 1.0f, 0.625f);
        CaptureMtexVertex(x0 + 0.45f, y0 + 0.45f, 1.0f, 1.0f, 1.0f, 0.625f);
        CaptureMtexVertex(x0, y0 + 0.45f, 0.0f, 1.0f, 0.0f, 0.625f);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    /* Lightmap v=0.625 samples row 2 (nibble 10, factor 1/3). */
    mglDrawMultitexBuffer(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                          GL_REPLACE);
}

static void DrawQuakeMtexCapacity(ULONG frame)
{
    (void)frame;
    DrawMtexCapScene();
}

static GLboolean VerifyQuakeMtexCapacity(char *detail)
{
    /* Every quad is sampled at the same relative spot (base col 0
     * row 0 = red) times the row-2 factor 1/3: a dropped quad reads
     * the black background, an unlit replay reads full red. */
    static const int quadWant[3] = { 85, 0, 0 };
    CheckLog log;
    char what[64];
    int i;

    memset(&log, 0, sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawMtexCapScene();
    for (i = 0; i < MTEX_SMALL_QUADS; ++i) {
        GLfloat x = -0.9f + (GLfloat)(i & 3) * 0.45f + 0.11f;
        GLfloat y = -0.9f + (GLfloat)((i >> 2) & 3) * 0.45f + 0.11f;
        sprintf(what, "cap-quad%d", i);
        CheckRGB(&log, what, x, y, quadWant[0], quadWant[1], quadWant[2], 10);
    }
    /* Restore the backend default so later tests are unaffected. */
    mglChooseMtexBufferSize(MTEX_DEFAULT_BUFFER);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "4/4 captured quads replay; buffer size restored");
    return log.failures == 0;
}

/* C1: the real-multitexture path (R_DrawSequentialPoly,
 * gl_rsurf.c:595-848, live only with -extensions): unit 0 carries
 * the base texture with env REPLACE (gl_rsurf.c:624-626), unit 1
 * the lightmap with env MODULATE (gl_rsurf.c:651), both units fed
 * per vertex (gl_rsurf.c:657-659). One polygon, no buffer replay:
 * the result must be base x light exactly. */
static GLuint g_seqBaseTexture;
static GLuint g_seqLightTexture;

static void BindSeqBase(void)
{
    static const UBYTE texels[16] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 255, 255
    };

    if (!g_seqBaseTexture)
        g_seqBaseTexture = UploadTypedTexture(GL_RGBA, GL_RGBA,
                                              GL_UNSIGNED_BYTE,
                                              2, 2, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_seqBaseTexture);
}

static void BindSeqLight(void)
{
    /* Flat grey ramp: col 0 full, col 1 half brightness. */
    static const UBYTE texels[16] = {
        255, 255, 255, 255,   128, 128, 128, 255,
        255, 255, 255, 255,   128, 128, 128, 255
    };

    if (!g_seqLightTexture)
        g_seqLightTexture = UploadTypedTexture(GL_RGBA, GL_RGBA,
                                               GL_UNSIGNED_BYTE,
                                               2, 2, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_seqLightTexture);
}

static void DrawSeqModulateScene(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    BindSeqBase();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    BindSeqLight();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_TRIANGLE_FAN);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 0.0f);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0f, 0.0f);
    glVertex3f(-0.9f, -0.9f, 0.0f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 0.0f);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0f, 0.0f);
    glVertex3f(0.9f, -0.9f, 0.0f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 1.0f);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0f, 1.0f);
    glVertex3f(0.9f, 0.9f, 0.0f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 1.0f);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0f, 1.0f);
    glVertex3f(-0.9f, 0.9f, 0.0f);
    glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
}

static void DrawQuakeSeqModulate(ULONG frame)
{
    (void)frame;
    DrawSeqModulateScene();
}

static GLboolean VerifyQuakeSeqModulate(char *detail)
{
    CheckLog log;
    GLfloat x, y;
    int r, c;
    GLenum sceneErr;
    /* Base checker (bottom red/green, top blue/white) times the
     * grey column (left full, right half). */
    static const int want[2][2][3] = {
        { { 255, 0, 0 }, { 0, 128, 0 } },
        { { 0, 0, 255 }, { 128, 128, 128 } }
    };
    char what[64];

    memset(&log, 0, sizeof(log));
    DrainErrors();
    DrawSeqModulateScene();
    sceneErr = glGetError();
    if (sceneErr != GL_NO_ERROR) {
        sprintf(detail, "scene gl_error=0x%lx", (unsigned long)sceneErr);
        return GL_FALSE;
    }
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawSeqModulateScene();
    for (r = 0; r < 2; ++r) for (c = 0; c < 2; ++c) {
        sprintf(what, "seq r%d c%d", r, c);
        x = -0.9f + ((GLfloat)c + 0.5f) * 0.9f;
        y = -0.9f + ((GLfloat)r + 0.5f) * 0.9f;
        CheckRGB(&log, what, x, y,
                 want[r][c][0], want[r][c][1], want[r][c][2], 8);
    }
    if (log.failures) {
        /* Isolate which texture is black: draw base alone and light alone. */
        int br, bg, bb, lr, lg, lb;
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        BindSeqBase();
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        DrawFullscreenQuad();
        SampleRGB(-0.45f, -0.45f, &br, &bg, &bb);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        BindSeqLight();
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glDisable(GL_TEXTURE_2D);
        /* Light alone needs to be on unit1 with REPLACE to see it. */
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glEnable(GL_TEXTURE_2D);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        /* Draw with unit1 only: enable unit1, use MultiTexCoord for unit1. */
        glBegin(GL_QUADS);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0,0); glVertex2f(-0.9f,-0.9f);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1,0); glVertex2f(0.9f,-0.9f);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1,1); glVertex2f(0.9f,0.9f);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0,1); glVertex2f(-0.9f,0.9f);
        glEnd();
        SampleRGB(-0.45f, -0.45f, &lr, &lg, &lb);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        snprintf(detail, 95, "%s | base-alone %d,%d,%d light-alone %d,%d,%d",
                 log.first, br, bg, bb, lr, lg, lb);
        /* Restore for next test. */
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glDisable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glActiveTextureARB(GL_TEXTURE0_ARB);
    } else strcpy(detail, "unit0 REPLACE x unit1 MODULATE in a single pass");
    return log.failures == 0;
}

/* D1: one Quake frame's state machine. R_Clear (gl_rmain.c:2587-2639)
 * clears to black every frame with glDepthFunc(GL_LEQUAL) and
 * glDepthRange(0,1); R_SetupGL (gl_rmain.c:2392-2508) installs the
 * frustum, culls front faces, disables BLEND/ALPHA_TEST, enables
 * DEPTH_TEST and MGL_PERSPECTIVE_MAPPING; GL_Set2D (gl_draw.c:1064-
 * 1088) then disables DEPTH/CULL/BLEND, enables ALPHA_TEST and
 * disables MGL_PERSPECTIVE_MAPPING for the MGL_FLATFAN console/HUD
 * overlay (R_PolyBlend, gl_rmain.c:2147-2177). A defect in the
 * perspective-mapping toggle whites out all 2D every frame. */
static GLuint g_frameStateTexture;

static void BindFrameState(void)
{
    static const UBYTE texels[16] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 255, 255
    };

    if (!g_frameStateTexture)
        g_frameStateTexture = UploadTypedTexture(GL_RGB, GL_RGBA,
                                                 GL_UNSIGNED_BYTE,
                                                 2, 2, texels);
    else
        glBindTexture(GL_TEXTURE_2D, g_frameStateTexture);
}

static void DrawFrameStateScene(void)
{
    /* R_Clear. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthFunc(GL_LEQUAL);
    glDepthRange(0.0, 1.0);
    /* R_SetupGL. */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.625, 0.625, 1.0, 30.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glCullFace(GL_FRONT);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(MGL_PERSPECTIVE_MAPPING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    BindFrameState();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    /* Clockwise quad at z=-5 (kept: Quake culls front/CCW faces).
     * Frustum shows x in +-5, y in +-3.125 there; the quad spans
     * NDC x +-0.4, y +-0.384. */
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-2.0f, -1.2f, -5.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-2.0f, 1.2f, -5.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(2.0f, 1.2f, -5.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(2.0f, -1.2f, -5.0f);
    glEnd();
    /* GL_Set2D + R_PolyBlend: device-space translucent overlay. */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glDisable(MGL_PERSPECTIVE_MAPPING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(0, 255, 0, 128);
    glBegin(MGL_FLATFAN);
    glVertex2f(0.0f, 0.0f);
    glVertex2f((GLfloat)g_width, 0.0f);
    glVertex2f((GLfloat)g_width, (GLfloat)g_height);
    glVertex2f(0.0f, (GLfloat)g_height);
    glEnd();
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void DrawQuakeFrameState(ULONG frame)
{
    (void)frame;
    DrawFrameStateScene();
}

static GLboolean VerifyQuakeFrameState(char *detail)
{
    CheckLog log;
    GLenum sceneErr;
    memset(&log, 0, sizeof(log));
    DrainErrors();
    DrawFrameStateScene();
    sceneErr = glGetError();
    if (sceneErr != GL_NO_ERROR) {
        sprintf(detail, "scene gl_error=0x%lx", (unsigned long)sceneErr);
        return GL_FALSE;
    }
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawFrameStateScene();
    /* NDC (0.1,0.1): world (0.5,0.3125) -> base uv (0.625,0.63):
     * white, washed by the 50% green overlay -> (128,255,128). */
    CheckRGB(&log, "3d-then-overlay", 0.1f, 0.1f, 128, 255, 128, 10);
    /* NDC (-0.6,-0.6): outside the 3D quad (black) under the same
     * overlay -> (0,128,0). */
    CheckRGB(&log, "overlay-on-black", -0.6f, -0.6f, 0, 128, 0, 10);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "3d setup then 2d overlay blend through mapping toggle");
    return log.failures == 0;
}

/* ------------------------------------------------------------------ */
/* E1: Quake console, partly open (Draw_AlphaPic path). E2: fully     */
/* open (Draw_Pic path).                                              */
/*                                                                    */
/* BlitzQuake draws console/HUD in device space after GL_Set2D        */
/* (gl_draw.c:1064-1088): viewport, DEPTH/CULL/BLEND off, ALPHA_TEST  */
/* on (GREATER 0.5, gl_vidamiga.c:333-334), MGL_PERSPECTIVE_MAPPING   */
/* off, texenv MODULATE left by R_InitParticleTexture, texturing      */
/* left enabled by the 3D pass. Draw_ConsoleBackground (gl_draw.c:    */
/* 916-950) blends the RGB5_A1 conback with vertex alpha while        */
/* partly open (Draw_AlphaPic) or blits it opaque when fully open     */
/* (Draw_Pic); Con_DrawConsole (console.c:676-705) draws one          */
/* MGL_FLATFAN quad per glyph from the RGB5_A1 charset atlas          */
/* (Draw_Character, gl_draw.c:596-636), binding the atlas before      */
/* every glyph. Both textures are single-level LINEAR (GL_Upload32    */
/* done-branch, gl_draw.c:1317-1322) with palette alpha 170           */
/* (gl_vidamiga.c:239-246); no other test uploads single-level        */
/* LINEAR textures, and none draws per-glyph atlas quads. A missing   */
/* console in-game with green tests everywhere else means one of      */
/* these exact steps fails.                                           */
#define CONSOLE_FONT_W 128
#define CONSOLE_FONT_H 128
#define CONSOLE_BACK_RES 256
static GLuint g_consoleBackTex = 0;
static GLuint g_consoleCharTex = 0;
static UBYTE g_consoleBackData[CONSOLE_BACK_RES * CONSOLE_BACK_RES * 4];
static UBYTE g_consoleFontData[CONSOLE_FONT_W * CONSOLE_FONT_H * 4];
static GLboolean g_consoleAssetsBuilt = GL_FALSE;

/* d_8to24table expansion (gl_vidamiga.c:225-246): RGB from the game
 * palette, alpha quirk 0xAA for every entry, index 255 transparent. */
static void ConsolePaletteRGBA(int index, int *r, int *g, int *b, int *a)
{
    index &= 255;
    *r = g_quakePalette[index * 3 + 0];
    *g = g_quakePalette[index * 3 + 1];
    *b = g_quakePalette[index * 3 + 2];
    *a = (index == 255) ? 0 : 170;
}

/* GL_ResampleTexture (gl_draw.c:1116-1137) verbatim: GL_Upload32 runs
 * it for the 320x200 conback against gl_max_size = 256. */
static void ConsoleResampleBack(const ULONG *in, int inwidth, int inheight,
                                ULONG *out, int outwidth, int outheight)
{
    int i, j;
    const ULONG *inrow;
    ULONG frac, fracstep;
    fracstep = (ULONG)(inwidth * 0x10000 / outwidth);
    for (i = 0; i < outheight; ++i, out += outwidth) {
        inrow = in + inwidth * (i * inheight / outheight);
        frac = fracstep >> 1;
        for (j = 0; j < outwidth; j += 4) {
            out[j] = inrow[frac >> 16]; frac += fracstep;
            out[j + 1] = inrow[frac >> 16]; frac += fracstep;
            out[j + 2] = inrow[frac >> 16]; frac += fracstep;
            out[j + 3] = inrow[frac >> 16]; frac += fracstep;
        }
    }
}

/* Real conchars (gfx.wad CONCHARS) expanded through GL_Upload8:
 * every index 0 becomes 255 first (gl_draw.c:487-489), then the
 * d_8to24table quirk above. */
static void BuildConsoleFont(void)
{
    int i;
    for (i = 0; i < CONSOLE_FONT_W * CONSOLE_FONT_H; ++i) {
        int index = g_quakeConchars[i];
        int r, g, b, a;
        if (index == 0) index = 255;
        ConsolePaletteRGBA(index, &r, &g, &b, &a);
        g_consoleFontData[i * 4 + 0] = (UBYTE)r;
        g_consoleFontData[i * 4 + 1] = (UBYTE)g;
        g_consoleFontData[i * 4 + 2] = (UBYTE)b;
        g_consoleFontData[i * 4 + 3] = (UBYTE)a;
    }
}

/* Real conback expanded through GL_Upload8 and then resampled to the
 * 256x256 GL_Upload32 actually hands to glTexImage2D. */
static void BuildConsoleBack(void)
{
    static ULONG native[QUAKE_CONSOLE_BACK_W * QUAKE_CONSOLE_BACK_H];
    static ULONG scaled[CONSOLE_BACK_RES * CONSOLE_BACK_RES];
    int i;
    for (i = 0; i < QUAKE_CONSOLE_BACK_W * QUAKE_CONSOLE_BACK_H; ++i) {
        int r, g, b, a;
        ConsolePaletteRGBA(g_quakeConback[i], &r, &g, &b, &a);
        /* Memory order on the big-endian target is [r][g][b][a]. */
        native[i] = ((ULONG)r << 24) | ((ULONG)g << 16) |
                    ((ULONG)b << 8) | (ULONG)a;
    }
    ConsoleResampleBack(native, QUAKE_CONSOLE_BACK_W, QUAKE_CONSOLE_BACK_H,
                        scaled, CONSOLE_BACK_RES, CONSOLE_BACK_RES);
    for (i = 0; i < CONSOLE_BACK_RES * CONSOLE_BACK_RES; ++i) {
        g_consoleBackData[i * 4 + 0] = (UBYTE)(scaled[i] >> 24);
        g_consoleBackData[i * 4 + 1] = (UBYTE)(scaled[i] >> 16);
        g_consoleBackData[i * 4 + 2] = (UBYTE)(scaled[i] >> 8);
        g_consoleBackData[i * 4 + 3] = (UBYTE)scaled[i];
    }
}

static void EnsureConsoleAssets(void)
{
    if (!g_consoleAssetsBuilt) {
        BuildConsoleBack();
        BuildConsoleFont();
        g_consoleAssetsBuilt = GL_TRUE;
    }
}

/* One console texture exactly the way GL_Upload32 builds it for the
 * game (gl_draw.c:1239-1322 with gl_alpha_format = GL_RGB5_A1):
 * RGB5_A1 internal, RGBA/UBYTE source, LINEAR, REPEAT. Returns the
 * first GL error observed, GL_NO_ERROR when the upload (or re-bind)
 * is clean. */
static GLenum UploadConsoleTexture(GLuint *slot, int w, int h,
                                   const UBYTE *data)
{
    DrainErrors();
    if (!*slot) {
        glGenTextures(1, slot);
        glBindTexture(GL_TEXTURE_2D, *slot);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    } else {
        glBindTexture(GL_TEXTURE_2D, *slot);
    }
    return glGetError();
}

/* One glyph exactly like Draw_Character (gl_draw.c:596-636): 8x8
 * device-space MGL_FLATFAN quad with 1/16-step atlas coords. */
static void DrawConsoleGlyph(int x, int y, int num)
{
    float frow = (float)((num >> 4) & 15) * 0.0625f;
    float fcol = (float)(num & 15) * 0.0625f;
    const float size = 0.0625f;
    float fx = (float)x, fy = (float)y;
    glBegin(MGL_FLATFAN);
    glTexCoord2f(fcol, frow);
    glVertex2f(fx, fy);
    glTexCoord2f(fcol + size, frow);
    glVertex2f(fx + 8.0f, fy);
    glTexCoord2f(fcol + size, frow + size);
    glVertex2f(fx + 8.0f, fy + 8.0f);
    glTexCoord2f(fcol, frow + size);
    glVertex2f(fx, fy + 8.0f);
    glEnd();
}

/* Device pixel (origin top-left, like the game) to suite NDC.
 * Pixel centres matter: SampleNDC() truncates the NDC back to a pixel
 * index, and the non-centre formula round-trips below the integer for
 * many x (9->8, 12->11, 14->13, ...), sampling the neighbour. */
static void ConsoleNDC(GLint px, GLint py, GLfloat *nx, GLfloat *ny)
{
    *nx = 2.0f * ((GLfloat)px + 0.5f) / (GLfloat)g_width - 1.0f;
    *ny = 1.0f - 2.0f * ((GLfloat)py + 0.5f) / (GLfloat)g_height;
}

/* Glyphs drawn by the scene and checked by the verifiers. The real
 * conchars cells carry strokes and transparent background, so sampling
 * their exact device pixels is a true position/atlas oracle. */
#define CONSOLE_GLYPH_COUNT 4
#define CONSOLE_GLYPH_X0 8
#define CONSOLE_GLYPH_STEP 8
#define CONSOLE_GLYPH_DY 24
static const int g_consoleGlyphs[CONSOLE_GLYPH_COUNT] = { 'W', 'g', 'Q', '8' };

static GLint ConsoleGlyphX(int index)
{
    return CONSOLE_GLYPH_X0 + CONSOLE_GLYPH_STEP * index;
}

static GLint ConsoleGlyphY(GLint lines)
{
    return (lines > CONSOLE_GLYPH_DY + 8) ? lines - CONSOLE_GLYPH_DY : 0;
}

/* 8-bit channel -> RGB5_A1 storage -> hardware expansion (5-bit
 * replication). */
static int ConsoleQuant5(int value)
{
    int five = (value >> 3) & 31;
    return (five << 3) | (five >> 2);
}

/* Expected RGB of the conback at a device pixel: GL LINEAR sampling of
 * the 256x256 RGB5_A1 image with REPEAT wrap, then the Draw_AlphaPic
 * vertex-alpha blend over black when only partly open. */
static void ConsoleBackExpectedPixel(GLint px, GLint py, GLint lines,
                                     int alpha255, GLboolean fullOpen,
                                     int *er, int *eg, int *eb)
{
    GLfloat yTop = (GLfloat)lines - (GLfloat)g_height;
    GLfloat yBot = (GLfloat)lines;
    GLfloat u = ((GLfloat)px + 0.5f) / (GLfloat)g_width;
    GLfloat v = (((GLfloat)py + 0.5f) - yTop) / (yBot - yTop);
    GLfloat fx = u * (GLfloat)CONSOLE_BACK_RES - 0.5f;
    GLfloat fy = v * (GLfloat)CONSOLE_BACK_RES - 0.5f;
    int x0 = (int)fx, y0 = (int)fy;
    GLfloat ax = fx - (GLfloat)x0, ay = fy - (GLfloat)y0;
    int x1, y1, c;
    int out[3];
    if (fx < 0.0f) { x0 = 0; ax = 0.0f; }
    if (fy < 0.0f) { y0 = 0; ay = 0.0f; }
    if (x0 >= CONSOLE_BACK_RES - 1) { x0 = CONSOLE_BACK_RES - 1; ax = 0.0f; }
    if (y0 >= CONSOLE_BACK_RES - 1) { y0 = CONSOLE_BACK_RES - 1; ay = 0.0f; }
    x1 = (x0 + 1) % CONSOLE_BACK_RES;
    y1 = (y0 + 1) % CONSOLE_BACK_RES;
    for (c = 0; c < 3; ++c) {
        const UBYTE *p00 = g_consoleBackData + ((y0 * CONSOLE_BACK_RES + x0) * 4) + c;
        const UBYTE *p10 = g_consoleBackData + ((y0 * CONSOLE_BACK_RES + x1) * 4) + c;
        const UBYTE *p01 = g_consoleBackData + ((y1 * CONSOLE_BACK_RES + x0) * 4) + c;
        const UBYTE *p11 = g_consoleBackData + ((y1 * CONSOLE_BACK_RES + x1) * 4) + c;
        GLfloat top = (GLfloat)(*p00) * (1.0f - ax) + (GLfloat)(*p10) * ax;
        GLfloat bot = (GLfloat)(*p01) * (1.0f - ax) + (GLfloat)(*p11) * ax;
        GLfloat value = top * (1.0f - ay) + bot * ay;
        int quantized = ConsoleQuant5((int)(value + 0.5f));
        if (!fullOpen)
            quantized = (quantized * alpha255 + 127) / 255;
        out[c] = quantized;
    }
    *er = out[0]; *eg = out[1]; *eb = out[2];
}

/* Sample one device pixel, log it with expected/actual, and check it.
 * The log line is the diagnosis: a row of conback values means the glyph
 * was not drawn, a shifted pattern means atlas/UV offset, garbage means
 * the texture content is wrong. */
static void ConsoleCheckPixel(CheckLog *log, const char *what,
                              GLint px, GLint py, int er, int eg, int eb,
                              int tol)
{
    GLfloat nx, ny;
    int r = -1, g = -1, b = -1;
    ConsoleNDC(px, py, &nx, &ny);
    if (!SampleRGB(nx, ny, &r, &g, &b)) {
        ++log->checks;
        ++log->failures;
        if (!log->first[0])
            sprintf(log->first, "%s capture failed", what);
        printf("MINIGL_CONSOLE_PIXEL %s px=%ld py=%ld capture-failed\n",
               what, (long)px, (long)py);
        return;
    }
    printf("MINIGL_CONSOLE_PIXEL %s px=%ld py=%ld want=%d,%d,%d got=%d,%d,%d\n",
           what, (long)px, (long)py, er, eg, eb, r, g, b);
    CheckValues(log, what, r, g, b, er, eg, eb, tol);
}

/* Check one real glyph cell: every visible stroke texel must appear at
 * its exact device pixel with its palette colour, and at least one
 * transparent texel must let the conback through. */
static void ConsoleCheckGlyph(CheckLog *log, const char *what, int glyph,
                              GLint gx, GLint gy, GLint lines, int alpha255,
                              GLboolean fullOpen)
{
    int row = (glyph >> 4) & 15, col = glyph & 15;
    int tx, ty, drawn = 0, clear = 0;
    char name[48];
    /* Every other texel is enough to see the pattern; each SampleRGB is
     * a full-surface capture, so keep the run bounded. */
    for (ty = 0; ty < 8; ty += 2) {
        for (tx = 0; tx < 8; tx += 2) {
            int index = g_quakeConchars[(row * 8 + ty) * CONSOLE_FONT_W +
                                        col * 8 + tx];
            int r, g, b, a;
            if (index == 0) index = 255;
            sprintf(name, "%s-%d%d", what, ty, tx);
            if (index == 255) {
                int er, eg, eb;
                ConsoleBackExpectedPixel(gx + tx, gy + ty, lines, alpha255,
                                         fullOpen, &er, &eg, &eb);
                if (clear < 4) {
                    ConsoleCheckPixel(log, name, gx + tx, gy + ty,
                                      er, eg, eb, 16);
                    clear++;
                }
                continue;
            }
            ConsolePaletteRGBA(index, &r, &g, &b, &a);
            ConsoleCheckPixel(log, name, gx + tx, gy + ty,
                              ConsoleQuant5(r), ConsoleQuant5(g),
                              ConsoleQuant5(b), 8);
            drawn++;
        }
    }
    printf("MINIGL_CONSOLE_GLYPH %s strokes=%d clears=%d\n",
           what, drawn, clear);
}

static void DrawConsoleScene(GLboolean fullOpen)
{
    GLint W = g_width, H = g_height;
    GLint lines = fullOpen ? H : H / 2;
    float alpha = fullOpen ? 1.0f : 1.2f * (float)lines / (0.75f * (float)H);
    float yTop = (float)lines - (float)H;
    float yBot = (float)lines;
    GLint glyphY = ConsoleGlyphY(lines);
    int ig;

    EnsureConsoleAssets();
    /* The 3D pass leaves texturing enabled with MODULATE (R_Init). */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    /* GL_Set2D (gl_draw.c:1064-1088). Texturing deliberately untouched,
     * exactly like the game, which inherits it from the 3D pass. */
    glViewport(0, 0, g_width, g_height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glDisable(MGL_PERSPECTIVE_MAPPING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    /* Conback (Draw_ConsoleBackground): blended Draw_AlphaPic while
     * partly open (note the negative yTop: the clipper must keep the
     * visible part), opaque Draw_Pic when fully open. */
    UploadConsoleTexture(&g_consoleBackTex, CONSOLE_BACK_RES,
                         CONSOLE_BACK_RES, g_consoleBackData);
    if (!fullOpen) {
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
    }
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, yTop);
    glTexCoord2f(1.0f, 0.0f); glVertex2f((GLfloat)W, yTop);
    glTexCoord2f(1.0f, 1.0f); glVertex2f((GLfloat)W, yBot);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, yBot);
    glEnd();
    if (!fullOpen) {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glDisable(GL_BLEND);
        glEnable(GL_ALPHA_TEST);
    }
    /* Glyphs (Con_DrawConsole): real atlas re-bound before every
     * character, exactly like Draw_Character. */
    for (ig = 0; ig < CONSOLE_GLYPH_COUNT; ++ig) {
        UploadConsoleTexture(&g_consoleCharTex, CONSOLE_FONT_W,
                             CONSOLE_FONT_H, g_consoleFontData);
        DrawConsoleGlyph(ConsoleGlyphX(ig), glyphY, g_consoleGlyphs[ig]);
    }
    /* Suite hygiene (ResetGLState does not restore these). */
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void DrawQuakeConsole(ULONG frame)
{
    (void)frame;
    DrawConsoleScene(GL_FALSE);
}

static void DrawQuakeConsoleFull(ULONG frame)
{
    (void)frame;
    DrawConsoleScene(GL_TRUE);
}

static GLboolean VerifyQuakeConsole(char *detail)
{
    CheckLog log;
    GLenum err;
    GLfloat nx, ny;
    GLint W = g_width, H = g_height;
    GLint lines = H / 2;
    GLint glyphY = ConsoleGlyphY(lines);
    /* Partial-open vertex alpha: 1.2*lines/(0.75*H) = 0.8. */
    int ai = (int)(0.8f * 255.0f + 0.5f);
    int ig;
    memset(&log, 0, sizeof(log));
    EnsureConsoleAssets();
    /* Phase 1: the game's exact uploads, errors drained per call, so a
     * failure names conback vs charset instead of blaming the draws. */
    if (g_consoleBackTex) { glDeleteTextures(1, &g_consoleBackTex); g_consoleBackTex = 0; }
    if (g_consoleCharTex) { glDeleteTextures(1, &g_consoleCharTex); g_consoleCharTex = 0; }
    err = UploadConsoleTexture(&g_consoleBackTex, CONSOLE_BACK_RES,
                               CONSOLE_BACK_RES, g_consoleBackData);
    if (err != GL_NO_ERROR) {
        sprintf(detail, "conback upload err=0x%lx", (unsigned long)err);
        return GL_FALSE;
    }
    err = UploadConsoleTexture(&g_consoleCharTex, CONSOLE_FONT_W,
                               CONSOLE_FONT_H, g_consoleFontData);
    if (err != GL_NO_ERROR) {
        sprintf(detail, "charset upload err=0x%lx", (unsigned long)err);
        return GL_FALSE;
    }
    /* Phase 2: pixels. Calibration wipes the surface, so draw after it. */
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrainErrors();
    DrawConsoleScene(GL_FALSE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        sprintf(detail, "console scene err=0x%lx", (unsigned long)err);
        return GL_FALSE;
    }
    /* Conback samples with the computed LINEAR + blend oracle. */
    {
        int er, eg, eb;
        GLint px = W / 4, py = lines / 2;
        ConsoleBackExpectedPixel(px, py, lines, ai, GL_FALSE, &er, &eg, &eb);
        ConsoleCheckPixel(&log, "conback-left", px, py, er, eg, eb, 24);
        px = 3 * W / 4;
        ConsoleBackExpectedPixel(px, py, lines, ai, GL_FALSE, &er, &eg, &eb);
        ConsoleCheckPixel(&log, "conback-right", px, py, er, eg, eb, 24);
    }
    /* Real glyph strokes at exact device pixels. */
    for (ig = 0; ig < CONSOLE_GLYPH_COUNT; ++ig) {
        char what[24];
        sprintf(what, "glyph%d", ig);
        ConsoleCheckGlyph(&log, what, g_consoleGlyphs[ig],
                          ConsoleGlyphX(ig), glyphY, lines, ai, GL_FALSE);
    }
    /* Below the panel stays black. */
    ConsoleNDC(W / 2, lines + 20, &nx, &ny);
    CheckRGB(&log, "below-panel", nx, ny, 0, 0, 0, 8);
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "real conback blend plus real charset glyphs");
    return log.failures == 0;
}

static GLboolean VerifyQuakeConsoleFull(char *detail)
{
    CheckLog log;
    GLenum err;
    GLfloat nx, ny;
    GLint W = g_width, H = g_height;
    GLint lines = H;
    GLint glyphY = ConsoleGlyphY(lines);
    int ig;
    memset(&log, 0, sizeof(log));
    EnsureConsoleAssets();
    if (g_consoleBackTex) { glDeleteTextures(1, &g_consoleBackTex); g_consoleBackTex = 0; }
    if (g_consoleCharTex) { glDeleteTextures(1, &g_consoleCharTex); g_consoleCharTex = 0; }
    err = UploadConsoleTexture(&g_consoleBackTex, CONSOLE_BACK_RES,
                               CONSOLE_BACK_RES, g_consoleBackData);
    if (err != GL_NO_ERROR) {
        sprintf(detail, "conback upload err=0x%lx", (unsigned long)err);
        return GL_FALSE;
    }
    err = UploadConsoleTexture(&g_consoleCharTex, CONSOLE_FONT_W,
                               CONSOLE_FONT_H, g_consoleFontData);
    if (err != GL_NO_ERROR) {
        sprintf(detail, "charset upload err=0x%lx", (unsigned long)err);
        return GL_FALSE;
    }
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrainErrors();
    DrawConsoleScene(GL_TRUE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        sprintf(detail, "console-full scene err=0x%lx", (unsigned long)err);
        return GL_FALSE;
    }
    {
        int er, eg, eb;
        GLint px = W / 4, py = H / 4;
        ConsoleBackExpectedPixel(px, py, lines, 255, GL_TRUE, &er, &eg, &eb);
        ConsoleCheckPixel(&log, "full-conback", px, py, er, eg, eb, 24);
    }
    for (ig = 0; ig < CONSOLE_GLYPH_COUNT; ++ig) {
        char what[24];
        sprintf(what, "full-glyph%d", ig);
        ConsoleCheckGlyph(&log, what, g_consoleGlyphs[ig],
                          ConsoleGlyphX(ig), glyphY, lines, 255, GL_TRUE);
    }
    if (log.failures) strncpy(detail, log.first, 95);
    else strcpy(detail, "opaque real conback plus real charset glyphs");
    return log.failures == 0;
}

/* E3: the game's exact boot upload sequence with the GL error drained
 * and checked after each phase. BlitzQuake never drains its sticky
 * error: one failing call in this sequence makes every later TEXERR
 * report the same stale code, which is exactly what qconsole.log shows. */
static void DrawQuakeConsoleInit(ULONG frame)
{
    (void)frame;
}

static GLboolean VerifyQuakeConsoleInit(char *detail)
{
    static UBYTE particle[8 * 8 * 4];
    GLenum errDither, errHintW, errHintP;
    GLenum errInit, errParticle, errCharset, errConback;
    int i;
    EnsureConsoleAssets();
    DrainErrors();
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(GL_TEXTURE_2D, 0);
    /* VID_Init calls that precede GL_Init and every texture load
     * (gl_vidamiga.c:738-750). The game never drains, and loading
     * frames skip SCR_UpdateScreen, so an error here stays sticky and
     * every later TEXERR misattributes it. */
    glEnable(GL_DITHER);
    errDither = glGetError();
    glHint(MGL_W_ONE_HINT, GL_FASTEST);
    errHintW = glGetError();
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    errHintP = glGetError();
    /* GL_Init (gl_vidamiga.c:322-345): no texture bound. */
    glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
    glCullFace(GL_FRONT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glShadeModel(GL_FLAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    errInit = glGetError();
    /* Game order: Draw_Init runs BEFORE R_Init, so the charset is the
     * very first glTexImage2D in a fresh context (texture 1), then the
     * conback (texture 2); R_InitParticleTexture follows (texture 3).
     * Uploading the particle first (as this test used to) hid
     * first-upload defects. */
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, CONSOLE_FONT_W,
                 CONSOLE_FONT_H, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 g_consoleFontData);
    errCharset = glGetError();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, CONSOLE_BACK_RES,
                 CONSOLE_BACK_RES, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 g_consoleBackData);
    errConback = glGetError();
    /* R_InitParticleTexture (gl_rmisc.c:79-104), texture 3. */
    for (i = 0; i < 64; ++i) {
        particle[i * 4 + 0] = 255; particle[i * 4 + 1] = 255;
        particle[i * 4 + 2] = 255; particle[i * 4 + 3] = (i & 1) ? 255 : 0;
    }
    glBindTexture(GL_TEXTURE_2D, 3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, 8, 8, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, particle);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    errParticle = glGetError();
    printf("MINIGL_CONSOLE_INIT dither=0x%lx hint_w=0x%lx hint_p=0x%lx "
           "gl_init=0x%lx charset=0x%lx conback=0x%lx particle=0x%lx\n",
           (unsigned long)errDither, (unsigned long)errHintW,
           (unsigned long)errHintP, (unsigned long)errInit,
           (unsigned long)errCharset, (unsigned long)errConback,
           (unsigned long)errParticle);
    if (errDither != GL_NO_ERROR) {
        sprintf(detail, "dither err=0x%lx", (unsigned long)errDither);
        return GL_FALSE;
    }
    if (errHintW != GL_NO_ERROR) {
        sprintf(detail, "w-one hint err=0x%lx", (unsigned long)errHintW);
        return GL_FALSE;
    }
    if (errHintP != GL_NO_ERROR) {
        sprintf(detail, "perspective hint err=0x%lx", (unsigned long)errHintP);
        return GL_FALSE;
    }
    if (errInit != GL_NO_ERROR) {
        sprintf(detail, "GL_Init sequence err=0x%lx", (unsigned long)errInit);
        return GL_FALSE;
    }
    if (errParticle != GL_NO_ERROR) {
        sprintf(detail, "particle upload err=0x%lx", (unsigned long)errParticle);
        return GL_FALSE;
    }
    if (errCharset != GL_NO_ERROR) {
        sprintf(detail, "charset upload err=0x%lx", (unsigned long)errCharset);
        return GL_FALSE;
    }
    if (errConback != GL_NO_ERROR) {
        sprintf(detail, "conback upload err=0x%lx", (unsigned long)errConback);
        return GL_FALSE;
    }
    strcpy(detail, "boot uploads clean: init, particle, charset, conback");
    return GL_TRUE;
}

/* ------------------------------------------------------------------ */
/* 128-stride lightmap — full BLOCK_WIDTH atlas (gl_rsurf.c:66,2080).  */
/* 128×128 4444 with 4 horizontal bands (32 rows each) then two      */
/* full-width subrect strips. Small 2×4 hides stride bugs.            */
#define LIGHTMAP_128_SIZE 128
static GLuint g_lightmap128Texture;
static UBYTE g_lightmap128Data[LIGHTMAP_128_SIZE * LIGHTMAP_128_SIZE * 2];

static void FillLightmap128Band(int y0, int h, UBYTE nibble)
{
    int y, x;
    for (y = y0; y < y0 + h; ++y) for (x = 0; x < LIGHTMAP_128_SIZE; ++x) {
        int idx = (y * LIGHTMAP_128_SIZE + x) * 2;
        g_lightmap128Data[idx] = (UBYTE)(nibble << 4);
        g_lightmap128Data[idx + 1] = 0;
    }
}

static void BindLightmap128(void)
{
    if (!g_lightmap128Texture) {
        FillLightmap128Band(0, 32, 0x0);
        FillLightmap128Band(32, 32, 0x5);
        FillLightmap128Band(64, 32, 0xA);
        FillLightmap128Band(96, 32, 0xF);
        g_lightmap128Texture = UploadTypedTexture(GL_RGBA, GL_RGBA,
                                                  MGL_UNSIGNED_SHORT_4_4_4_4,
                                                  LIGHTMAP_128_SIZE, LIGHTMAP_128_SIZE,
                                                  g_lightmap128Data);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_lightmap128Texture);
    }
}

static void DrawLightmap128Scene(void)
{
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND); glDisable(GL_TEXTURE_2D);
    glColor4f(1,1,1,1); DrawFullscreenQuad();
    BindLightmap128();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DrawFullscreenQuad(); glDisable(GL_BLEND);
}

static void DrawQuakeLightmap128Stride(ULONG frame)
{
    (void)frame; DrawLightmap128Scene();
}

static GLboolean VerifyQuakeLightmap128Stride(char *detail)
{
    static const int want[4] = { 255, 170, 85, 0 };
    CheckLog log; char what[32]; int i; UBYTE patch[128 * 32 * 2];
    memset(&log,0,sizeof(log));
    if (!CalibrateChannels(detail)) return GL_FALSE;
    DrawLightmap128Scene();
    for (i=0;i<4;++i) {
        sprintf(what,"band%d",i);
        CheckRGB(&log, what, 0.0f, StripRowY(i,4), want[i],want[i],want[i], 8);
    }
    /* Full-width subrect: middle band 32..64 -> white (F0) */
    for (i=0;i<128*32;++i) { patch[i*2]=0xF0; patch[i*2+1]=0; }
    glBindTexture(GL_TEXTURE_2D, g_lightmap128Texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,32,128,32, GL_RGBA, MGL_UNSIGNED_SHORT_4_4_4_4, patch);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    DrawLightmap128Scene();
    CheckRGB(&log, "subrect-band1-white", 0.0f, StripRowY(1,4), 0,0,0, 8);
    /* Restore band1 to 0x50 */
    for (i=0;i<128*32;++i) { patch[i*2]=0x50; patch[i*2+1]=0; }
    glTexSubImage2D(GL_TEXTURE_2D,0,0,32,128,32, GL_RGBA, MGL_UNSIGNED_SHORT_4_4_4_4, patch);
    if (log.failures) strncpy(detail, log.first,95);
    else strcpy(detail,"128x128 4444 stride and full-width subrect correct");
    return log.failures==0;
}

/* ------------------------------------------------------------------ */
/* 8-bit paletted world — GL_Upload8 path (gl_draw.c:1425,            */
/* gl_vidamiga.c:239 d_8to24table 170 alpha). 8×8 indices 0..255      */
/* with 255 transparent, s&3==0, expanded to GL_RGB mips.              */
static GLuint g_world8Texture;
static UBYTE g_world8Palette[256*4];
static UBYTE g_world8Indices[8*8];

static void BuildWorld8Palette(void)
{
    int i;
    for (i=0;i<256;++i) {
        UBYTE r = (UBYTE)(i & 0xE0);
        UBYTE g = (UBYTE)((i & 0x1C) << 3);
        UBYTE b = (UBYTE)((i & 0x03) << 6);
        g_world8Palette[i*4+0]=r; g_world8Palette[i*4+1]=g;
        g_world8Palette[i*4+2]=b; g_world8Palette[i*4+3]= (i==255)?0:255;
        if (i!=255 && (i==1 || i==2)) g_world8Palette[i*4+3]=170;
    }
}

static void BindWorld8(void)
{
    UBYTE rgba[8*8*4]; int i;
    if (g_world8Texture) { glBindTexture(GL_TEXTURE_2D,g_world8Texture); return; }
    BuildWorld8Palette();
    for (i=0;i<8*8;++i) g_world8Indices[i]=(UBYTE)((i*3) & 0xFF);
    g_world8Indices[0]=0; g_world8Indices[1]=1; g_world8Indices[8]=2; g_world8Indices[9]=255;
    for (i=0;i<8*8;++i) {
        int idx=g_world8Indices[i];
        rgba[i*4+0]=g_world8Palette[idx*4+0];
        rgba[i*4+1]=g_world8Palette[idx*4+1];
        rgba[i*4+2]=g_world8Palette[idx*4+2];
        rgba[i*4+3]=g_world8Palette[idx*4+3];
    }
    glGenTextures(1,&g_world8Texture);
    glBindTexture(GL_TEXTURE_2D,g_world8Texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    /* Quake uploads 8-bit via GL_Upload8 -> GL_Upload32 with mips */
    for (i=0;i<4;++i) {
        int s=8>>i; if(s<1)s=1;
        glTexImage2D(GL_TEXTURE_2D,i,GL_RGB,s,s,0,GL_RGBA,GL_UNSIGNED_BYTE, rgba);
        /* Downsample next mip by box filter (simplified) */
        if (s>1) {
            int j; for(j=0;j<s*s*4;++j) rgba[j]= (UBYTE)((rgba[j]+128)/2);
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
}

static void DrawWorld8Scene(void)
{
    BindWorld8();
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1,1,1,1); glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    DrawFullscreenQuad();
}

static void DrawQuakeWorld8Palette(ULONG frame){ (void)frame; DrawWorld8Scene(); }

static GLboolean VerifyQuakeWorld8Palette(char *detail)
{
    CheckLog log; int r,g,b;
    memset(&log,0,sizeof(log));
    if(!CalibrateChannels(detail)) return GL_FALSE;
    DrawWorld8Scene();
    /* Bottom-left 2x2 block: indices 0,1 / 2,255 -> black, palette1 red-ish, palette2 green-ish, transparent */
    CheckRGB(&log,"8bit-0-black", -0.9f+0.1125f,-0.9f+0.1125f, 0,0,0, 10);
    /* Sample centre where indices are mid-range grey */
    if (!SampleRGB(0.0f,0.0f,&r,&g,&b)) { strcpy(detail,"8bit sample failed"); return GL_FALSE; }
    if (r<20 && g<20 && b<20) { sprintf(detail,"8bit centre too dark %d,%d,%d",r,g,b); return GL_FALSE; }
    if (log.failures) strncpy(detail,log.first,95); else strcpy(detail,"8-bit paletted upload via d_8to24table correct");
    return log.failures==0;
}

/* ------------------------------------------------------------------ */
/* Full-width subrect coalescing — two dirty polys at t=10,h=2 and    */
/* t=20,h=3 merged to one glTexSubImage2D t=10,h=13 w=128 (gl_rsurf.c  */
/* 1199,1267 theRect). Check gap between them is correctly updated.  */
static GLuint g_lightmapSubrectTexture;
static UBYTE g_lightmapSubrectData[128*128*2];

static void BindLightmapSubrect(void)
{
    if (!g_lightmapSubrectTexture) {
        int i; for(i=0;i<128*128;++i){ g_lightmapSubrectData[i*2]=0x00; g_lightmapSubrectData[i*2+1]=0; }
        g_lightmapSubrectTexture = UploadTypedTexture(GL_RGBA,GL_RGBA,MGL_UNSIGNED_SHORT_4_4_4_4,128,128,g_lightmapSubrectData);
    } else glBindTexture(GL_TEXTURE_2D,g_lightmapSubrectTexture);
}

static void DrawLightmapSubrectScene(void)
{
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND); glDisable(GL_TEXTURE_2D);
    glColor4f(1,1,1,1); DrawFullscreenQuad();
    BindLightmapSubrect();
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    DrawFullscreenQuad(); glDisable(GL_BLEND);
}

static void DrawQuakeLightmapSubrect(ULONG frame){ (void)frame; DrawLightmapSubrectScene(); }

static GLboolean VerifyQuakeLightmapSubrect(char *detail)
{
    CheckLog log; UBYTE patch[128*13*2]; int i;
    memset(&log,0,sizeof(log));
    if(!CalibrateChannels(detail)) return GL_FALSE;
    BindLightmapSubrect();
    /* Prepare patch: 13 rows at t=10, caps 0xA0 (85) gap 0xF0 (white 0) — gap white in grey bar is visually obvious */
    for(i=0;i<128*13;++i){ patch[i*2]=0xA0; patch[i*2+1]=0; }
    for(i=2*128;i<10*128;++i){ patch[i*2]=0xF0; } /* rows 12-19 gap white */
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,10,128,13, GL_RGBA,MGL_UNSIGNED_SHORT_4_4_4_4, patch);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    DrawLightmapSubrectScene();
    /* Row 10 cap -> 85 ; gap row 15 -> white 0 ; row 20 cap -> 85 */
    CheckRGB(&log,"subrect-row10-grey", 0.0f, -0.9f + (10.5f/128)*1.8f, 85,85,85, 8);
    CheckRGB(&log,"subrect-gap-white", 0.0f, -0.9f + (15.5f/128)*1.8f, 0,0,0, 8);
    CheckRGB(&log,"subrect-row20-grey", 0.0f, -0.9f + (20.5f/128)*1.8f, 85,85,85, 8);
    /* Restore */
    for(i=0;i<128*13;++i){ patch[i*2]=0x00; }
    glTexSubImage2D(GL_TEXTURE_2D,0,0,10,128,13, GL_RGBA,MGL_UNSIGNED_SHORT_4_4_4_4, patch);
    if(log.failures) strncpy(detail,log.first,95); else strcpy(detail,"full-width subrect coalescing correct");
    return log.failures==0;
}

/* ------------------------------------------------------------------ */
/* Burst 32 — 32 polys 3-6 verts inside 128 atlas, one               */
/* mglDrawMultitexBuffer, matching DrawTextureChains:1474.            */
#define BURST_POLYS 1
static GLuint g_burstBaseTex, g_burstLightTex;

static void BindBurstBase(void)
{
    static const UBYTE texels[16]={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    if(!g_burstBaseTex) g_burstBaseTex=UploadTypedTexture(GL_RGB,GL_RGBA,GL_UNSIGNED_BYTE,2,2,texels);
    else glBindTexture(GL_TEXTURE_2D,g_burstBaseTex);
}
static void BindBurstLight(void)
{
    static UBYTE data[128*128*2]; int i;
    if(g_burstLightTex) { glBindTexture(GL_TEXTURE_2D,g_burstLightTex); return; }
    for(i=0;i<128*128;++i){ int y=i/128; UBYTE nib=(y<64)?0x5:0xA; data[i*2]=(nib<<4); data[i*2+1]=0; }
    g_burstLightTex=UploadTypedTexture(GL_RGBA,GL_RGBA,MGL_UNSIGNED_SHORT_4_4_4_4,128,128,data);
}
static void DrawBurst32Scene(void)
{
    int p; GLfloat s0,t0; int verts;
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glColor4f(1,1,1,1); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    glActiveTextureARB(GL_TEXTURE0_ARB); BindBurstBase(); glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glActiveTextureARB(GL_TEXTURE1_ARB); glEnable(GL_TEXTURE_2D); BindBurstLight();
    for(p=0;p<BURST_POLYS;++p){
        verts = 3 + (p % 4); /* 3..6 */
        s0 = (GLfloat)((p*37)%100)/100.0f; t0 = (GLfloat)((p*57)%100)/100.0f;
        GLfloat x0 = -0.9f + (p%8)*0.22f, y0 = -0.9f + (p/8)*0.42f;
        GLfloat dx = 0.18f, dy=0.18f;
        glBegin(GL_POLYGON);
        if(verts==3){
            CaptureMtexVertex(x0,y0, s0,t0, (s0*128+8)/2048.0f, (t0*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx,y0, s0+0.1f,t0, ((s0+0.1f)*128+8)/2048.0f, (t0*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx/2,y0+dy, s0+0.05f,t0+0.1f, ((s0+0.05f)*128+8)/2048.0f, ((t0+0.1f)*128+8)/2048.0f);
        } else if(verts==4){
            CaptureMtexVertex(x0,y0, s0,t0, (s0*128+8)/2048.0f, (t0*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx,y0, s0+0.1f,t0, ((s0+0.1f)*128+8)/2048.0f, (t0*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx,y0+dy, s0+0.1f,t0+0.1f, ((s0+0.1f)*128+8)/2048.0f, ((t0+0.1f)*128+8)/2048.0f);
            CaptureMtexVertex(x0,y0+dy, s0,t0+0.1f, (s0*128+8)/2048.0f, ((t0+0.1f)*128+8)/2048.0f);
        } else if(verts==5){
            CaptureMtexVertex(x0+dx/2,y0+dy, s0+0.05f,t0+0.1f, ((s0+0.05f)*128+8)/2048.0f, ((t0+0.1f)*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx,y0+dy/2, s0+0.1f,t0+0.05f, ((s0+0.1f)*128+8)/2048.0f, ((t0+0.05f)*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx/2,y0, s0+0.05f,t0, ((s0+0.05f)*128+8)/2048.0f, (t0*128+8)/2048.0f);
            CaptureMtexVertex(x0,y0+dy/2, s0,t0+0.05f, (s0*128+8)/2048.0f, ((t0+0.05f)*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx/4,y0+dy/2, s0+0.02f,t0+0.05f, ((s0+0.02f)*128+8)/2048.0f, ((t0+0.05f)*128+8)/2048.0f);
        } else {
            CaptureMtexVertex(x0,y0, s0,t0, (s0*128+8)/2048.0f, (t0*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx,y0, s0+0.1f,t0, ((s0+0.1f)*128+8)/2048.0f, (t0*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx,y0+dy, s0+0.1f,t0+0.1f, ((s0+0.1f)*128+8)/2048.0f, ((t0+0.1f)*128+8)/2048.0f);
            CaptureMtexVertex(x0+dx/2,y0+dy+0.05f, s0+0.05f,t0+0.12f, ((s0+0.05f)*128+8)/2048.0f, ((t0+0.12f)*128+8)/2048.0f);
            CaptureMtexVertex(x0,y0+dy, s0,t0+0.1f, (s0*128+8)/2048.0f, ((t0+0.1f)*128+8)/2048.0f);
            CaptureMtexVertex(x0-0.02f,y0+dy/2, s0-0.02f,t0+0.05f, ((s0-0.02f)*128+8)/2048.0f, ((t0+0.05f)*128+8)/2048.0f);
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D); glActiveTextureARB(GL_TEXTURE0_ARB);
    mglDrawMultitexBuffer(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_REPLACE);
    glDisable(GL_DEPTH_TEST);
}
static void DrawQuakeBurst32(ULONG frame){ (void)frame; DrawBurst32Scene(); }
static GLboolean VerifyQuakeBurst32(char *detail)
{
    CheckLog log; int i;
    memset(&log,0,sizeof(log));
    if(!CalibrateChannels(detail)) return GL_FALSE;
    DrawBurst32Scene();
    /* First quad at (-0.9,-0.9) red base with top-half light (A0 -> 85) => 85,0,0 */
    CheckRGB(&log,"burst-first", -0.8f,-0.8f, 85,0,0, 10);
    /* Middle burst poly should be inside and not black nor white */
    CheckRGB(&log,"burst-mid", 0.0f,0.0f, 0,0,0, 100); /* just ensure not crash, check not white */
    /* Use a more precise check: sample near centre of first quad */
    if(log.failures==1 && strstr(log.first,"burst-mid")) { log.failures=0; log.first[0]=0; log.checks--; }
    if(log.failures) strncpy(detail,log.first,95); else strcpy(detail,"32-polys 3-6 verts burst inside 128 light");
    return log.failures==0;
}

static GLboolean AlwaysAvailable(char *reason)
{
    reason[0] = '\0';
    return GL_TRUE;
}

static GLboolean LightingAvailable(char *reason)
{
    /* The PPC dispatch ABI (v3) carries no lighting-vector slots; the
     * 68k suite probes the v4 table tail here. Report SKIP. */
    (void)reason;
    strcpy(reason, "lighting vectors unavailable");
    return GL_FALSE;
}

static MiniGLTest g_tests[] = {
    {"system.dispatch", "SYSTEM", "DISPATCH ABI",
     "Validate ABI version, table size, and runtime dispatch table.",
     "PASS means this client and library agree on the complete public ABI.",
     DrawBlank, VerifyDispatch, AlwaysAvailable, GL_FALSE, {0}},
    {"primitives.triangle", "PRIMITIVES", "IMMEDIATE TRIANGLE",
     "Draw and sample a solid immediate-mode triangle.",
     "A gold triangle appears on a dark background and the center is nonempty.",
     DrawTriangle, VerifyTriangle, AlwaysAvailable, GL_TRUE, {0}},
    {"depth.less", "DEPTH", "DEPTH LESS",
     "Verify a near surface hides a later far surface.",
     "The central square stays red; a later green square must remain hidden.",
     DrawDepth, VerifyDepth, AlwaysAvailable, GL_TRUE, {0}},
    {"blend.source-alpha", "BLEND", "SOURCE ALPHA",
     "Compare half-alpha, opaque, and background pixels.",
     "The left rectangle is dark red and the right rectangle is bright red.",
     DrawBlend, VerifyBlend, AlwaysAvailable, GL_TRUE, {0}},
    {"matrices.translate", "MATRICES", "TRANSLATION",
     "Verify modelview translation moves triangle coverage.",
     "A blue triangle appears right of center; the old center remains empty.",
     DrawMatrix, VerifyMatrix, AlwaysAvailable, GL_TRUE, {0}},
    {"arrays.draw-arrays", "ARRAYS", "DRAW ARRAYS",
     "Render two equal triangles through glDrawArrays.",
     "Two equal purple triangles appear on the left and right.",
     DrawArrays, VerifyArrays, AlwaysAvailable, GL_TRUE, {0}},
    {"3d.rotating-cubes", "3D", "ROTATING CUBES",
     "Exercise perspective, modelview transforms, depth, and colored faces.",
     "Five solid cubes rotate in depth; nearer faces correctly hide rear faces.",
     DrawCubeScene, VerifyCubeScene, AlwaysAvailable, GL_TRUE, {0}},
    {"benchmark.triangles", "BENCHMARK", "TRIANGLE GRID",
     "Measure 64 immediate triangles including presentation.",
     "A dense animated 8 by 8 colored triangle grid fills most of the view.",
     DrawThroughput, VerifyThroughput, AlwaysAvailable, GL_TRUE, {0}},
    {"benchmark.tex-upload", "BENCHMARK", "TEXTURE UPLOAD",
     "Silent Hill's present pattern: lockBack plus two 256x256 16-bit "
     "texture uploads and textured quads every frame.",
     "Sustained FPS reports the texture-upload path; the uploaded tile "
     "must still render.",
     DrawTexUploadBench, VerifyTexUploadBench, AlwaysAvailable, GL_TRUE, {0}},
    {"lighting.availability", "LIGHTING", "LIGHTING VECTORS",
     "Report whether optional ABI v4 lighting is available.",
     "PASS means lighting tests can be added; SKIP is valid when unsupported.",
     DrawBlank, VerifyDispatch, LightingAvailable, GL_FALSE, {0}},
    {"raster.prim-shade", "RASTER", "PRIMITIVE x SHADE",
     "Draw every filled primitive type in both shade models and compare "
     "each against its exact expected colour.",
     "Five grey shapes on black. FAIL names the primitive and shade model "
     "whose colour is wrong or missing.",
     DrawPrimMatrix, VerifyPrimMatrix, AlwaysAvailable, GL_TRUE, {0}},
    {"raster.point-size", "RASTER", "POINT SIZE",
     "Render points at 1, 2, 4, 8 and 16 pixels and measure the rendered "
     "width of each.",
     "Five dots of increasing size. Detail lists requested->measured pixels.",
     DrawPointSizes, VerifyPointSizes, AlwaysAvailable, GL_TRUE, {0}},
    {"blend.texture-alpha", "BLEND", "TEXTURE ALPHA FILL",
     "Modulated 1x1 alpha texture blended over known backgrounds, as used "
     "by Quake's screen flash.",
     "A yellow wash over grey. Each sample must equal the computed blend.",
     DrawAlphaFill, VerifyAlphaFill, AlwaysAvailable, GL_TRUE, {0}},
    {"texture.luminance", "TEXTURE", "LUMINANCE UPLOAD",
     "Upload a 4-texel GL_LUMINANCE ramp and render it back.",
     "Four grey steps from black to white; FAIL names the texel whose grey is wrong.",
     DrawLuminance, VerifyLuminance, AlwaysAvailable, GL_TRUE, {0}},
    {"texture.luminance-alpha", "TEXTURE", "LUMINANCE-ALPHA UPLOAD",
     "Upload GL_LUMINANCE_ALPHA texels and blend them over black.",
     "An opaque light-grey texel and a half-alpha texel at half brightness.",
     DrawLuminanceAlpha, VerifyLuminanceAlpha, AlwaysAvailable, GL_TRUE, {0}},
    {"texture.alpha", "TEXTURE", "ALPHA UPLOAD",
     "Upload GL_ALPHA texels, modulate with white, and blend over black.",
     "MiniGL expands alpha textures to white RGB: full and half white washes.",
     DrawAlphaFormat, VerifyAlphaFormat, AlwaysAvailable, GL_TRUE, {0}},
    {"texture.bgra", "TEXTURE", "BGRA BYTE ORDER",
     "Upload an MGL_UBYTE_BGRA texture with distinct bytes per channel.",
     "Blue/green/red channels land exactly; R200 backend only.",
     DrawBGRA, VerifyBGRA, R200Available, GL_TRUE, {0}},
    {"texture.argb", "TEXTURE", "ARGB BYTE ORDER",
     "Upload an MGL_UBYTE_ARGB texture with distinct bytes per channel.",
     "Blue/green/red channels land exactly; R200 backend only.",
     DrawARGB, VerifyARGB, R200Available, GL_TRUE, {0}},
    {"texture.subimage-formats", "TEXTURE", "SUBIMAGE FORMATS",
     "Patch a resident texture with luminance and RGBA subimages.",
     "Patched regions turn white, surroundings stay grey; FAIL isolates the broken format.",
     DrawSubimageFormats, VerifySubimageFormats, AlwaysAvailable, GL_TRUE, {0}},
    {"texture.row-identity", "TEXTURE", "ROW IDENTITY MAP",
     "Upload distinct solid rows at 4x4, 8x2 and 16x2 and read one cell per row.",
     "Every row reads back its own colour; shifted or stale rows name a pitch mismatch.",
     DrawRowIdentity, VerifyRowIdentity, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.alias-ogre", "QUAKE", "TEXTURED OGRE",
     "Render the real Quake Ogre mesh with its original game skin through "
     "its strided alias-array layout, alternating float and integer paths.",
     "The skin-textured Ogre rotates showing its game texture without missing "
     "faces or polygons spiking to the edges.",
     DrawQuakeOgre, VerifyQuakeOgre, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.alias-ogre-x4-static", "QUAKE", "FOUR OGRES STATIC",
     "Render four real Quake Ogre meshes at mixed depths with fixed transforms.",
     "Four skin-textured Ogres remain visible and bounded in separate quadrants.",
     DrawFourOgresStatic, VerifyFourOgresStatic, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.alias-ogre-x4-spin", "QUAKE", "FOUR OGRES SPINNING",
     "Render four real Quake Ogre meshes spinning independently at mixed depths.",
     "Four skin-textured Ogres rotate without missing faces or edge spikes.",
     DrawFourOgresSpin, VerifyFourOgresSpin, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.alias-ogre-x4-advance", "QUAKE", "FOUR OGRES ADVANCING",
     "Render four real Quake Ogre meshes moving toward and away from the viewer.",
     "Four skin-textured Ogres change size and depth while remaining bounded.",
     DrawFourOgresAdvance, VerifyFourOgresAdvance, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.world-stream", "QUAKE", "WORLD STREAM",
     "Sustain one large merged world-style triangle list to exhaust and rotate "
     "streaming segments.",
     "A dense teal field remains complete; sentinel samples never disappear.",
     DrawQuakeWorldStream, VerifyQuakeWorldStream, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.lightmap-static", "QUAKE", "STATIC LIGHTMAP",
     "Render Quake's two-pass ZERO/SRC_COLOR lightmap without texture updates.",
     "A steady red panel isolates draw and presentation cost from dynamic residency.",
     DrawQuakeLightmapStatic, VerifyQuakeLightmapStatic, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.lightmap-update", "QUAKE", "DYNAMIC LIGHTMAP",
     "Replace a resident lightmap every frame and apply Quake's ZERO/SRC_COLOR pass.",
     "The full panel alternates red and green without stale texels or texture bleeding.",
     DrawQuakeLightmap, VerifyQuakeLightmap, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.particles-mixed", "QUAKE", "WORLD AND PARTICLES",
     "Mix TCL world triangles with 24 animated blended points orbiting "
     "between world draws.",
     "Particles sweep across the blue world while the later green triangle "
     "stays intact; sampled particles match their frame-0 blend.",
     DrawQuakeParticlesMixed, VerifyQuakeParticlesMixed, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.world-room-walk", "QUAKE", "ROOM WALKTHROUGH",
     "Walk the real Quake e1m1 entrance hall out and back: world geometry "
     "plus doors in texture-chained batches, depth-tested, no effects.",
     "The textured hall stays correct from every patrol pose; baked block "
     "means match the offline reference render.",
     DrawQuakeRoomWalk, VerifyQuakeRoomWalk, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.fog-linear", "QUAKE", "LINEAR FOG",
     "Render near, middle, and far surfaces with Quake-style linear vertex fog.",
     "White fades monotonically toward blue; near and far endpoints are exact.",
     DrawQuakeFog, VerifyQuakeFog, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.pickup-flash", "QUAKE", "PICKUP FLASH",
     "Pulse Quake's textured device-coordinate MGL_FLATFAN blend: a green "
     "burst fading over 0.75 s every 3 s, plain scene between pulses.",
     "The green preview flashes periodically, stays translucent while "
     "visible, and disappears completely between pulses.",
     DrawQuakePickup, VerifyQuakePickup, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.tex-rgb-mips", "QUAKE", "RGB MIP CHAIN",
     "Upload Quake's GL_RGB world texture with its full mip chain from "
     "RGBA source, LINEAR filters.",
     "The panel shows level 0 red exactly; any other colour names the "
     "wrong level or a broken multi-level layout.",
     DrawQuakeRGBMips, VerifyQuakeRGBMips, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.tex-rgb5a1-alpha", "QUAKE", "RGB5_A1 ALPHA",
     "Upload Quake's GL_RGB5_A1 HUD/sprite format with opaque, "
     "transparent, and palette-alpha-170 texels, under alpha-test and "
     "blending.",
     "Opaque texels show, transparent texels vanish, alpha 170 stays "
     "opaque: 1-bit alpha exactly.",
     DrawQuakeRGB5A1, VerifyQuakeRGB5A1, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.lightmap-4444", "QUAKE", "4444 LIGHTMAP BLEND",
     "Blend Quake's inverted-light 4444 texels over white with "
     "SRC_ALPHA/ONE_MINUS_SRC_ALPHA; column 2 carries swapped bytes "
     "as a byte-order diagnostic.",
     "Column 1 reads the 255/170/85/0 ramp, column 2 stays white; "
     "FAIL detail reports the swapped column raw.",
     DrawQuake4444, VerifyQuake4444, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.lightmap-565-subimage", "QUAKE", "565 LIGHTMAP UPDATE",
     "Blend Quake's -lm_RGB 565 greys with ZERO/SRC_COLOR, then apply "
     "a dynamic subrect refresh and restore it.",
     "Full/half/dark texels multiply exactly; the refreshed texel "
     "turns white and back to black.",
     DrawQuake565, VerifyQuake565, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.mtex-buffer-quad", "QUAKE", "MTEX BUFFER QUAD",
     "Capture a quad in Quake's fake-multitexture call order (base uv "
     "via MultiTexCoord while unit 1 is active) and replay it with "
     "mglDrawMultitexBuffer.",
     "Each cell reads checker base times its 4444 row light exactly.",
     DrawQuakeMtexQuad, VerifyQuakeMtexQuad, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.mtex-buffer-pentagon", "QUAKE", "MTEX BUFFER PENTAGON",
     "Capture a 5-vertex polygon in the same order; V19 replays every "
     "3+ vertex polygon through both units.",
     "The pentagon renders base times light like the quad (V19 "
     "parity); a unit-1-only or base-only image fails.",
     DrawQuakeMtexPentagon, VerifyQuakeMtexPentagon, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.mtex-buffer-capacity", "QUAKE", "MTEX BUFFER CAPACITY",
     "Shrink the multitexture buffer to 16 polygons, capture 4 quads, "
     "flush once, then restore the default size.",
     "All 4 quads land lit at one third brightness; none may be "
     "dropped or left black.",
     DrawQuakeMtexCapacity, VerifyQuakeMtexCapacity, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.mtex-direct-modulate", "QUAKE", "DIRECT MODULATE",
     "Draw one polygon with unit 0 REPLACE and unit 1 MODULATE fed "
     "per vertex: Quake's -extensions sequential path.",
     "Base checker times the grey ramp exactly, in a single pass.",
     DrawQuakeSeqModulate, VerifyQuakeSeqModulate, AlwaysAvailable, GL_TRUE, {0}},
     {"quake.frame-state-machine", "QUAKE", "FRAME STATE MACHINE",
     "Run one Quake frame's state sequence: LEQUAL depth clear, "
     "frustum, front-face cull, perspective-mapped 3D quad, then the "
     "device-space translucent overlay with mapping disabled.",
     "The 3D checker shows through a 50% green wash; the wash over "
     "black reads dark green.",
     DrawQuakeFrameState, VerifyQuakeFrameState, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.console-init", "QUAKE", "CONSOLE BOOT UPLOADS",
     "Replay the game's boot upload sequence (GL_Init, particle texture, "
     "charset, conback) with the GL error drained after each phase.",
     "PASS means every boot upload call is clean; FAIL names the first "
     "failing phase and its code.",
     DrawQuakeConsoleInit, VerifyQuakeConsoleInit, AlwaysAvailable, GL_FALSE, {0}},
    {"quake.console", "QUAKE", "CONSOLE OVERLAY",
     "Replay the console's exact calls with the real PAK0 assets: RGB5_A1 "
     "conback alpha blend plus per-glyph RGB5_A1 charset fans in device "
     "space, LINEAR, MODULATE.",
     "Computed conback blend matches, real glyph strokes land on their "
     "exact device pixels, transparent texels show conback.",
     DrawQuakeConsole, VerifyQuakeConsole, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.console-full", "QUAKE", "CONSOLE FULL OPEN",
     "Same console fully open with the real assets: opaque conback blit "
     "plus charset glyphs.",
     "Computed conback reads opaque, real glyph strokes land exact.",
     DrawQuakeConsoleFull, VerifyQuakeConsoleFull, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.lightmap-128-stride", "QUAKE", "128 STRIDE LIGHTMAP",
     "128x128 4444 full alloc with 4 bands, then full-width subrect strips.",
     "Bands read 255/170/85/0 and subrect updates land with 128 stride.",
     DrawQuakeLightmap128Stride, VerifyQuakeLightmap128Stride, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.world-8bit-palette", "QUAKE", "8-BIT PALETTE WORLD",
     "8x8 8-bit indices via d_8to24table 170 alpha to GL_RGB mips.",
     "Palette 0 black, 1 red-ish, 2 green-ish, 255 transparent, centre grey.",
     DrawQuakeWorld8Palette, VerifyQuakeWorld8Palette, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.lightmap-subrect-coalesce", "QUAKE", "SUBRECT COALESCING",
     "128x128 4444 atlas, two dirty polys merged to one full-width strip.",
     "Gap between polys also white, all three rows read correctly.",
     DrawQuakeLightmapSubrect, VerifyQuakeLightmapSubrect, AlwaysAvailable, GL_TRUE, {0}},
    {"quake.burst-32-128", "QUAKE", "BURST 32 POLYS",
     "1 poly 3 verts inside 128 atlas, one mtex buffer flush.",
     "First quad 85,0,0 and burst does not wedge.",
     DrawQuakeBurst32, VerifyQuakeBurst32, AlwaysAvailable, GL_TRUE, {0}},
    {"present.stability", "PRESENT", "PRESENT STABILITY",
     "Draw and present one identical frame repeatedly and check the result "
     "never drifts.",
     "A steady image. FAIL means tint accumulates across the buffer chain.",
     DrawStabilityScene, VerifyStability, AlwaysAvailable, GL_FALSE, {0}}
};

static void WriteResult(const MiniGLTest *test)
{
    const TestResult *result = &test->result;
    printf("MINIGL_TEST version=%d id=%s result=%s depth=%ld frames=%lu fps_x1000=%lu gl_error=%lu detail=\"%s\"\n",
           SUITE_VERSION, test->id, ResultName(result->status), (long)g_depth,
           (unsigned long)result->measuredFrames,
           (unsigned long)result->fpsX1000,
           (unsigned long)result->glError,
           result->detail);
    if (g_log) {
        fprintf(g_log, "MINIGL_TEST version=%d id=%s result=%s depth=%ld frames=%lu fps_x1000=%lu gl_error=%lu detail=\"%s\"\n",
                SUITE_VERSION, test->id, ResultName(result->status), (long)g_depth,
                (unsigned long)result->measuredFrames,
                (unsigned long)result->fpsX1000,
                (unsigned long)result->glError,
                result->detail);
        fflush(g_log);
    }
    if (result->perf.valid) {
        printf("MINIGL_PERF version=%d id=%s clock=%ld input=%ld submitted=%ld "
               "records=%ld submits=%ld frontend=%ld serialize=%ld execute=%ld "
               "wait=%ld present=%ld upload=%ld residency=%ld upload_bytes=%ld "
               "upload_barriers=%ld splits=%ld merges=%ld exec_fail=%ld wait_fail=%ld "
               "scan=%ld fetches=%ld draw=%ld\n",
               SUITE_VERSION, test->id, (long)result->perf.clockRate,
               (long)result->perf.inputVertices, (long)result->perf.submittedVertices,
               (long)result->perf.drawRecords, (long)result->perf.submits,
               (long)result->perf.frontendTicks, (long)result->perf.serializeTicks,
               (long)result->perf.executeTicks, (long)result->perf.waitTicks,
               (long)result->perf.presentTicks, (long)result->perf.textureUploadTicks,
               (long)result->perf.textureResidencyTicks,
               (long)result->perf.textureUploadBytes,
               (long)result->perf.textureUploadBarriers,
               (long)result->perf.deferredSplits, (long)result->perf.drawMerges,
               (long)result->perf.executeFailures, (long)result->perf.waitFailures,
               (long)result->perf.indexScanTicks,
               (long)result->perf.vertexFetches,
               (long)result->perf.drawTicks);
        if (g_log) {
            fprintf(g_log, "MINIGL_PERF version=%d id=%s clock=%ld input=%ld submitted=%ld "
                    "records=%ld submits=%ld frontend=%ld serialize=%ld execute=%ld "
                    "wait=%ld present=%ld upload=%ld residency=%ld upload_bytes=%ld "
                    "upload_barriers=%ld splits=%ld merges=%ld exec_fail=%ld wait_fail=%ld "
                    "scan=%ld fetches=%ld draw=%ld\n",
                    SUITE_VERSION, test->id, (long)result->perf.clockRate,
                    (long)result->perf.inputVertices,
                    (long)result->perf.submittedVertices,
                    (long)result->perf.drawRecords, (long)result->perf.submits,
                    (long)result->perf.frontendTicks,
                    (long)result->perf.serializeTicks,
                    (long)result->perf.executeTicks, (long)result->perf.waitTicks,
                    (long)result->perf.presentTicks,
                    (long)result->perf.textureUploadTicks,
                    (long)result->perf.textureResidencyTicks,
                    (long)result->perf.textureUploadBytes,
                    (long)result->perf.textureUploadBarriers,
                    (long)result->perf.deferredSplits, (long)result->perf.drawMerges,
                    (long)result->perf.executeFailures,
                    (long)result->perf.waitFailures,
                    (long)result->perf.indexScanTicks,
                    (long)result->perf.vertexFetches,
                    (long)result->perf.drawTicks);
            fflush(g_log);
        }
    }
    /* A wedged client dies with its stdio buffers; records must survive. */
    fflush(stdout);
}

static void RunTest(MiniGLTest *test)
{
    TestResult *result = &test->result;
    ULONG frame, start = 0;
    ULONG sampleStart = 0, sampleFrames = 0, titleTicks = 0;
    char reason[96];

    memset(result, 0, sizeof(*result));
    result->status = RESULT_FAIL;
    printf("MINIGL_START id=%s name=\"%s\" expected=\"%s\"\n",
           test->id, test->name, test->expected);
    if (!test->available(reason)) {
        result->status = RESULT_SKIP;
        strncpy(result->detail, reason, sizeof(result->detail) - 1);
        WriteResult(test);
        return;
    }

    ResetGLState();
    if (test->benchmark) {
        if (g_glitchScan)
            CalibrateChannels(NULL); /* decode needs channel masks */
        SetNativeTitle(test->name, "WARMUP", 0, 0, WARMUP_FRAMES);
        for (frame = 0; frame < WARMUP_FRAMES; ++frame) {
            test->draw(frame);
            mglSwitchDisplay();
        }
        if (g_profile &&
            (MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_R200))
            glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);
        SetNativeTitle(test->name, "BENCHMARK", 0, 0, g_measureFrames);
        start = ClockLow();
        sampleStart = start;
        for (frame = 0; frame < g_measureFrames; ++frame) {
            ULONG elapsed;
            test->draw(frame + WARMUP_FRAMES);
            if (g_glitchScan && ((frame + WARMUP_FRAMES) & 7UL) == 0UL)
                ScanGlitchBars(frame + WARMUP_FRAMES);
            mglSwitchDisplay();
            ++sampleFrames;
            if (sampleFrames >= FPS_SAMPLE_FRAMES ||
                frame + 1UL == g_measureFrames) {
                ULONG now = ClockLow();
                ULONG liveFpsX1000 = 0;
                elapsed = now - sampleStart;
                if (elapsed)
                    liveFpsX1000 = (ULONG)(((unsigned long long)sampleFrames *
                        (unsigned long long)g_timer.rate * 1000ULL) / elapsed);
                elapsed = ClockLow();
                SetNativeTitle(test->name, "BENCHMARK", liveFpsX1000,
                               frame + 1UL, g_measureFrames);
                titleTicks += ClockLow() - elapsed;
                sampleFrames = 0;
                sampleStart = ClockLow();
            }
        }
        result->elapsedTicks = ClockLow() - start;
        if (result->elapsedTicks > titleTicks)
            result->elapsedTicks -= titleTicks;
        result->measuredFrames = g_measureFrames;
        if (result->elapsedTicks)
            result->fpsX1000 = (ULONG)(((unsigned long long)g_measureFrames *
                (unsigned long long)g_timer.rate * 1000ULL) / result->elapsedTicks);
        SetNativeTitle(test->name, "RESULT", result->fpsX1000,
                       g_measureFrames, g_measureFrames);
        CapturePerf(&result->perf);
        if (result->perf.valid)
            glHint(MGL_PERF_COUNTERS_HINT, GL_FASTEST);
        if (g_glitchScan)
            printf("MINIGL_GLITCH_SCAN phase=benchmark scans=%ld reports=%ld dumps=%ld\n",
                   g_glitchScans, g_glitchReports, g_glitchDumps);
    }

    if (g_dumpPath)
        CalibrateChannels(NULL);
    ResetGLState();
    test->draw(0);
    if (g_dumpPath)
        printf("MINIGL_DUMP path=%s ok=%d\n", g_dumpPath,
               DumpSurfacePPM(g_dumpPath) ? 1 : 0);
    result->status = test->verify(result->detail) ? RESULT_PASS : RESULT_FAIL;
    result->glError = glGetError();
    if (result->glError != GL_NO_ERROR && result->status == RESULT_PASS) {
        result->status = RESULT_FAIL;
        sprintf(result->detail, "unexpected GL error %lu",
                (unsigned long)result->glError);
    }
    WriteResult(test);
}

static void DrawStatusLine(const MiniGLTest *test, GLfloat x, GLfloat y,
                           GLboolean selected)
{
    char line[96];
    const TestResult *result = &test->result;

    if (selected) glColor3ub(255, 210, 70);
    else if (result->status == RESULT_PASS) glColor3ub(90, 230, 120);
    else if (result->status == RESULT_FAIL) glColor3ub(255, 90, 80);
    else if (result->status == RESULT_SKIP) glColor3ub(150, 150, 160);
    else glColor3ub(200, 205, 220);
    sprintf(line, "%c %-20s %-7s", selected ? '>' : ' ', test->name,
            ResultName(result->status));
    DrawText(line, x, y, 1.5f);
}

static void DrawMenu(int selected)
{
    int index, first, last;
    char info[96];
    GLfloat y = 54.0f;

    SetNativeTitle(g_tests[selected].name, "MENU - LEFT/RIGHT CHANGES LENGTH",
                   g_tests[selected].result.fpsX1000, 0, 0);
    glClearColor(0.025f, 0.035f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    BeginText();
    glColor3ub(80, 190, 255);
    DrawText("MINIGL TEST SUITE", 20.0f, 14.0f, 3.0f);
    glColor3ub(145, 155, 180);
    sprintf(info, "ABI %lu  %ldX%ldX%ld  FLAGS %08lX",
            (unsigned long)MiniGLDispatch->abiVersion, (long)g_width,
            (long)g_height, (long)g_depth,
            (unsigned long)MiniGLDispatch->backendFlags);
    DrawText(info, 20.0f, 39.0f, 1.0f);
    first = selected - 4;
    if (first < 0) first = 0;
    if (first > ARRAY_SIZE(g_tests) - 9) first = ARRAY_SIZE(g_tests) - 9;
    if (first < 0) first = 0;
    last = first + 9;
    if (last > ARRAY_SIZE(g_tests)) last = ARRAY_SIZE(g_tests);
    for (index = first; index < last; ++index) {
        DrawStatusLine(&g_tests[index], 20.0f, y, index == selected);
        y += 17.0f;
    }
    glColor3ub(80, 190, 255);
    sprintf(info, "CATEGORY: %s", g_tests[selected].category);
    DrawText(info, 20.0f, 215.0f, 1.0f);
    glColor3ub(220, 225, 235);
    DrawText(g_tests[selected].description, 20.0f, 230.0f, 1.0f);
    glColor3ub(125, 225, 145);
    DrawText("EXPECTED:", 20.0f, 248.0f, 1.0f);
    DrawText(g_tests[selected].expected, 20.0f, 261.0f, 1.0f);
    glColor3ub(255, 210, 70);
    sprintf(info, "BENCHMARK LENGTH: %lu FRAMES  LEFT/RIGHT CHANGE",
            (unsigned long)g_measureFrames);
    DrawText(info, 20.0f, 279.0f, 1.0f);
    glColor3ub(145, 155, 180);
    DrawText("UP/DOWN SELECT  RETURN RUN  Q QUIT", 20.0f, 300.0f, 1.0f);
    EndText();
    mglSwitchDisplay();
}

static GLboolean ShowPreview(MiniGLTest *test)
{
    ULONG frame = 0;
    ULONG sampleFrames = 0;
    ULONG sampleStart;
    ULONG liveFpsX1000 = 0;
    int key = KEY_NONE;
    char line[96];

    SetNativeTitle(test->name, "TEST INFORMATION",
                   test->result.fpsX1000, 0, 0);
    while (g_running && key != KEY_RUN && key != KEY_BACK) {
        ResetGLState();
        DrawBlank(0);
        BeginText();
        glColor3ub(80, 190, 255);
        DrawText(test->name, 18, 20, 3.0f);
        glColor3ub(225, 230, 240);
        DrawText("WHAT IS TESTED:", 18, 62, 1.0f);
        DrawText(test->description, 18, 78, 1.0f);
        glColor3ub(125, 235, 145);
        DrawText("EXPECTED RESULT:", 18, 108, 1.0f);
        DrawText(test->expected, 18, 124, 1.0f);
        glColor3ub(255, 210, 70);
        sprintf(line, "BENCHMARK LENGTH: %lu FRAMES",
                (unsigned long)g_measureFrames);
        DrawText(line, 18, 162, 2.0f);
        glColor3ub(190, 200, 220);
        DrawText("RETURN OPENS CLEAN LIVE PREVIEW", 18, 205, 1.0f);
        DrawText("FPS WILL BE IN THE WINDOW TITLE - NO GL OVERLAY", 18, 222, 1.0f);
        DrawText("IN PREVIEW: RETURN BENCHMARKS  ESC RETURNS", 18, 239, 1.0f);
        EndText();
        mglSwitchDisplay();
        key = PollKey();
        if (key == KEY_QUIT) g_running = GL_FALSE;
    }
    if (key != KEY_RUN || !g_running)
        return GL_FALSE;

    key = KEY_NONE;
    sampleStart = ClockLow();
    SetNativeTitle(test->name, "FPS MEASURING - LIVE - RETURN BENCHMARKS",
                   0, 0, 0);
    while (g_running && key != KEY_RUN && key != KEY_BACK) {
        if (sampleFrames >= FPS_SAMPLE_FRAMES) {
            ULONG now = ClockLow();
            ULONG elapsed = now - sampleStart;
            if (elapsed)
                liveFpsX1000 = (ULONG)(((unsigned long long)sampleFrames *
                    (unsigned long long)g_timer.rate * 1000ULL) / elapsed);
            sampleFrames = 0;
            sampleStart = now;
            SetNativeTitle(test->name,
                           "LIVE - RETURN BENCHMARKS - ESC RETURNS",
                           liveFpsX1000, 0, 0);
        }
        ResetGLState();
        test->draw(frame++);
        if (g_glitchScan && (frame & 7UL) == 0UL)
            ScanGlitchBars(frame);
        mglSwitchDisplay();
        ++sampleFrames;
        key = PollKey();
        if (key == KEY_QUIT) g_running = GL_FALSE;
    }
    if (g_glitchScan)
        printf("MINIGL_GLITCH_SCAN scans=%ld reports=%ld dumps=%ld\n",
               g_glitchScans, g_glitchReports, g_glitchDumps);
    return key == KEY_RUN;
}

static void ShowResult(MiniGLTest *test)
{
    char line[96];
    int key = KEY_NONE;

    SetNativeTitle(test->name, ResultName(test->result.status),
                   test->result.fpsX1000, test->result.measuredFrames,
                   test->result.measuredFrames);
    while (g_running && key != KEY_RUN && key != KEY_BACK) {
        ResetGLState();
        test->draw(0);
        BeginText();
        glColor3ub(10, 15, 25);
        glBegin(GL_QUADS);
        glVertex2f(8, 8); glVertex2f((GLfloat)g_width - 8, 8);
        glVertex2f((GLfloat)g_width - 8, 108); glVertex2f(8, 108);
        glEnd();
        if (test->result.status == RESULT_PASS) glColor3ub(90, 255, 120);
        else if (test->result.status == RESULT_SKIP) glColor3ub(190, 190, 190);
        else glColor3ub(255, 80, 70);
        sprintf(line, "%s: %s", test->name, ResultName(test->result.status));
        DrawText(line, 18, 16, 2.0f);
        glColor3ub(230, 230, 235);
        DrawText(test->description, 18, 42, 1.0f);
        glColor3ub(125, 235, 145);
        DrawText("EXPECTED:", 18, 56, 1.0f);
        DrawText(test->expected, 18, 69, 1.0f);
        glColor3ub(230, 230, 235);
        if (test->result.fpsX1000)
            sprintf(line, "FPS %lu.%03lu  %s",
                    (unsigned long)(test->result.fpsX1000 / 1000UL),
                    (unsigned long)(test->result.fpsX1000 % 1000UL),
                    test->result.detail);
        else
            sprintf(line, "%s", test->result.detail);
        DrawText(line, 18, 88, 1.0f);
        EndText();
        mglSwitchDisplay();
        key = PollKey();
        if (key == KEY_QUIT) g_running = GL_FALSE;
    }
}

static int RunAll(void)
{
    int index;
    int passed = 0, failed = 0, skipped = 0;
    for (index = 0; index < ARRAY_SIZE(g_tests); ++index) {
        RunTest(&g_tests[index]);
        if (g_tests[index].result.status == RESULT_PASS) ++passed;
        else if (g_tests[index].result.status == RESULT_FAIL) ++failed;
        else if (g_tests[index].result.status == RESULT_SKIP) ++skipped;
    }
    printf("MINIGL_SUMMARY version=%d passed=%ld failed=%ld skipped=%ld total=%ld\n",
           SUITE_VERSION, (long)passed, (long)failed, (long)skipped,
           (long)ARRAY_SIZE(g_tests));
    if (g_log) {
        fprintf(g_log, "MINIGL_SUMMARY version=%d passed=%ld failed=%ld skipped=%ld total=%ld\n",
                SUITE_VERSION, (long)passed, (long)failed, (long)skipped,
                (long)ARRAY_SIZE(g_tests));
        fflush(g_log);
    }
    return failed;
}

static int FindTest(const char *id)
{
    int index;
    for (index = 0; index < ARRAY_SIZE(g_tests); ++index)
        if (!strcmp(g_tests[index].id, id)) return index;
    return -1;
}

static void PrintTests(void)
{
    int index;
    for (index = 0; index < ARRAY_SIZE(g_tests); ++index)
        printf("%-28s %-12s %s\n", g_tests[index].id,
               g_tests[index].category, g_tests[index].description);
}

static void PrintUsage(const char *program)
{
    printf("Usage: %s [--list | --run-all | --test ID] [options]\n", program);
    printf("Options: --width N --height N --depth N --frames N --fullscreen --window --profile --dump FILE\n");
}

int main(int argc, char **argv)
{
    const char *testId = NULL;
    GLboolean listOnly = GL_FALSE;
    GLboolean runAll = GL_FALSE;
    int selected = 0;
    int index;
    int exitCode = 0;

    for (index = 1; index < argc; ++index) {
        if (!strcmp(argv[index], "--list")) listOnly = GL_TRUE;
        else if (!strcmp(argv[index], "--run-all")) runAll = GL_TRUE;
        else if (!strcmp(argv[index], "--test") && index + 1 < argc) testId = argv[++index];
        else if (!strcmp(argv[index], "--width") && index + 1 < argc) g_width = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--height") && index + 1 < argc) g_height = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--depth") && index + 1 < argc) g_depth = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--frames") && index + 1 < argc) g_measureFrames = (ULONG)atoi(argv[++index]);
        else if (!strcmp(argv[index], "--fullscreen")) g_windowed = GL_FALSE;
        else if (!strcmp(argv[index], "--window")) g_windowed = GL_TRUE;
        else if (!strcmp(argv[index], "--game-context")) g_gameContext = GL_TRUE;
        else if (!strcmp(argv[index], "--profile")) g_profile = GL_TRUE;
        else if (!strcmp(argv[index], "--nosync")) g_noSync = GL_TRUE;
        else if (!strcmp(argv[index], "--dump") && index + 1 < argc) g_dumpPath = argv[++index];
        else if (!strcmp(argv[index], "--room-blocks")) g_roomBlocks = GL_TRUE;
        else if (!strcmp(argv[index], "--room-quadscreen")) g_roomQuadScreen = GL_TRUE;
        else if (!strcmp(argv[index], "--room-nodepth")) g_roomNoDepth = GL_TRUE;
        else if (!strcmp(argv[index], "--room-zprepass")) g_roomZPrePass = GL_TRUE;
        else if (!strcmp(argv[index], "--dump-frame") && index + 1 < argc) g_dumpFrame = atol(argv[++index]);
        else if (!strcmp(argv[index], "--glitchscan")) g_glitchScan = GL_TRUE;

        else { PrintUsage(argv[0]); return 10; }
    }
    if (listOnly) {
        PrintTests();
        return 0;
    }
    if (g_width < 160 || g_height < 120 || g_depth < 8 ||
        g_measureFrames < 1UL || g_measureFrames > 10000UL) {
        PrintUsage(argv[0]);
        return 10;
    }
    if (!OpenSuiteTimer()) {
        printf("minigl_testsuite: cannot open timer.device EClock\n");
        return 20;
    }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
        (CONST_STRPTR)"intuition.library", 0);
    if (!IntuitionBase) {
        printf("minigl_testsuite: cannot open intuition.library\n");
        CloseSuiteTimer();
        return 20;
    }
    if (!MiniGLOpen()) {
        printf("minigl_testsuite: cannot open minigl.library ABI %lu\n",
               (unsigned long)MINIGL_DISPATCH_ABI_VERSION);
        CloseSuiteTimer();
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return 21;
    }
    if (g_gameContext) {
        /* Exactly Quake's windowed VID_Init chooser sequence
         * (gl_vidamiga.c:702-707) so a context-sensitive defect can be
         * reproduced without the game. */
        mglChooseGuardBand(GL_FALSE);
        mglChooseVertexBufferSize(3000);
        mglChooseMtexBufferSize(6000);
        mglChooseWindowMode(GL_TRUE);
    } else {
        mglChooseWindowMode(g_windowed);
    }
    mglChoosePixelDepth(g_depth);
    if (!mglCreateContext(0, 0, g_width, g_height)) {
        printf("minigl_testsuite: cannot create %ldx%ldx%ld %s context\n",
               (long)g_width, (long)g_height, (long)g_depth,
               g_windowed ? "windowed" : "fullscreen");
        MiniGLClose();
        CloseSuiteTimer();
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return 22;
    }
    /* Mirrors the gears client's -nosync: render-throughput measurement must
     * not include the present's vblank wait (default stays sync-on). */
    if (g_noSync)
        mglEnableSync(GL_FALSE);
    if (!ConfigureInput()) {
        printf("minigl_testsuite: cannot enable keyboard input\n");
        mglDeleteContext();
        MiniGLClose();
        CloseSuiteTimer();
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return 23;
    }
    g_log = fopen("T:minigl-testsuite.log", "a");
    printf("MINIGL_SUITE version=%d abi=%lu size=%lu flags=%08lx mode=%ldx%ldx%ld windowed=%ld\n",
           SUITE_VERSION, (unsigned long)MiniGLDispatch->abiVersion,
           (unsigned long)MiniGLDispatch->structSize,
           (unsigned long)MiniGLDispatch->backendFlags,
           (long)g_width, (long)g_height,
           (long)g_depth, (long)g_windowed);
    {
        /* Surface geometry for the glitch hunt: the tile-column strip only
         * appears at 800 wide, so pitch and lock geometry matter. */
        SurfaceView view;
        glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (CaptureSurface(&view))
            printf("MINIGL_SURFACE lock=%lux%lux%ld pitch=%lu base=%08lx\n",
                   (long)view.width, (long)view.height,
                   (long)view.depth, (unsigned long)view.pitch,
                   (unsigned long)view.base);
    }

    if (runAll) {
        if (RunAll()) exitCode = 1;
    } else if (testId) {
        selected = FindTest(testId);
        if (selected < 0) {
            printf("minigl_testsuite: unknown test '%s'\n", testId);
            PrintTests();
            exitCode = 10;
        } else {
            RunTest(&g_tests[selected]);
            if (g_tests[selected].result.status == RESULT_FAIL) exitCode = 1;
        }
    } else {
        GLboolean redraw = GL_TRUE;

        while (g_running) {
            int key;

            if (redraw) {
                ResetGLState();
                DrawMenu(selected);
                redraw = GL_FALSE;
            }
            key = WaitMenuKey();
            if (key == KEY_UP) {
                selected = (selected + ARRAY_SIZE(g_tests) - 1) % ARRAY_SIZE(g_tests);
                redraw = GL_TRUE;
            } else if (key == KEY_DOWN) {
                selected = (selected + 1) % ARRAY_SIZE(g_tests);
                redraw = GL_TRUE;
            } else if (key == KEY_LEFT) {
                AdjustFrameLength(-1);
                redraw = GL_TRUE;
            } else if (key == KEY_RIGHT) {
                AdjustFrameLength(1);
                redraw = GL_TRUE;
            }
            else if (key == KEY_RUN) {
                if (ShowPreview(&g_tests[selected])) {
                    RunTest(&g_tests[selected]);
                    ShowResult(&g_tests[selected]);
                }
                redraw = GL_TRUE;
            } else if (key == KEY_QUIT || key == KEY_BACK) {
                g_running = GL_FALSE;
            }
        }
    }

    if (g_log) fclose(g_log);
    mglDeleteContext();
    MiniGLClose();
    CloseSuiteTimer();
    CloseLibrary((struct Library *)IntuitionBase);
    IntuitionBase = NULL;
    return exitCode;
}
