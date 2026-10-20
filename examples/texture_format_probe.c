/* Standalone texture-format repro probe (PPC/WarpOS, public dispatch API only).
 *
 * Renders one textured quad per source format (GL_LUMINANCE,
 * GL_LUMINANCE_ALPHA, GL_ALPHA, MGL_UBYTE_BGRA, MGL_UBYTE_ARGB) plus a
 * TexSubImage2D patch case, then verifies pixels through the validated
 * command-27 glReadPixels path. Prints one PROBE line per case with
 * actual-vs-expected RGB so a broken expansion/stride shows up directly.
 *
 * Build (does NOT need a Makefile change; the generic pattern rule covers
 * every C file in examples):
 *   make -f Makefile.import.gcc bin/examples/texture_format_probe_ppc.warpelf
 * Deploy to DH2:wosbuild as texfmt_probe (<=16 chars), CRC-verify, run with
 * no arguments. Exit 0 = all pass, 20 = at least one failure.
 *
 * Expected values encode MiniGL's documented behaviour: GL_ALPHA expands to
 * white RGB (both the R200 ConvertPixel and the mglQ3 A8_ARGB agree on
 * r=g=b=255), NOT the GL-spec zero-RGB that Mesa produces. If a backend
 * disagrees, the probe flags it -- that is the point.
 *
 * All source data is client-side byte arrays: no type punning, no
 * host-endian assumptions (both 68k and PPC/WarpOS are big-endian; the
 * BGRA/ARGB byte orders under test are semantic, not endian, properties).
 */

#include <proto/minigl.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_W 320
#define PROBE_H 240
#define PROBE_MAXW 640
#define PROBE_MAXH 480
#define PROBE_TOL 20

/* Actual window (defaults match the original 320x240 probe window;
 * --width/--height override). The suite fails at 512x320x32, so the
 * window size is a bisection variable. */
static int g_winW = PROBE_W, g_winH = PROBE_H;
static int g_depth = 32;    /* window pixel depth: Q3 runs 16 -- present
                             * and readback must work at BOTH depths */
static int g_buffers = 0;   /* 0 = MiniGL default; Q3 windowed forces 2
                             * via mglChooseNumberOfBuffers -- repro flag */
static int g_sync = -1;     /* -1 = don't call mglEnableSync; Q3 calls
                             * mglEnableSync(r_finish) (=1) after create */
static int g_lock = 0;      /* 1 = Q3's mglLockMode(MGL_LOCK_SMART) */
static int g_loop = 0;      /* >0: present N continuously-drawn frames
                             * before the hold (Q3's render/present mode) */
static int g_drawbuf = 0;   /* 1 = call glDrawBuffer(GL_BACK) like Q3's
                             * GL_SetDefaultState does once at init */

static ULONG checks, failures;

/* Crash-safe progress log: open/append/close per line so the diag file
 * stays readable even when the launching shell wedges holding stdout
 * (observed with WarpOS exception requesters). */
static void LogLine(const char *line)
{
    FILE *f = fopen("DH2:wosbuild/texfmt_diag.log", "a");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

static int AbsDiff(int a, int b)
{
    return a > b ? a - b : b - a;
}

static int ExpectRGB(const char *id, const UBYTE *px,
                     int er, int eg, int eb)
{
    int ok;

    ++checks;
    ok = AbsDiff(px[0], er) <= PROBE_TOL &&
         AbsDiff(px[1], eg) <= PROBE_TOL &&
         AbsDiff(px[2], eb) <= PROBE_TOL;
    if (!ok)
        ++failures;
    printf("PROBE id=%s result=%s got=%d,%d,%d want=%d,%d,%d tol=%d\n",
           id, ok ? "PASS" : "FAIL",
           (int)px[0], (int)px[1], (int)px[2], er, eg, eb, PROBE_TOL);
    return ok;
}

static int ExpectError(const char *id, GLenum want)
{
    GLenum got = glGetError();
    int ok;

    ++checks;
    ok = (got == want);
    if (!ok)
        ++failures;
    printf("PROBE id=%s result=%s gl_error=%lu want=%lu\n",
           id, ok ? "PASS" : "FAIL",
           (unsigned long)got, (unsigned long)want);
    return ok;
}

/* Draw a Wx1 texture stretched over the full viewport; row y=0 is the
 * texture row. Identity matrices: NDC -1..1 maps the viewport. */
static void DrawStrip(int texW)
{
    int i;

    (void)texW;
    glBegin(GL_QUADS);
    for (i = 0; i < 1; ++i) {
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
    }
    glEnd();
}

/* Read back one horizontal strip: texel i centre sits at
 * x = (i+0.5)*W/texW, y = H/2 for a full-viewport Wx1 strip. */
static void ReadStrip(UBYTE *out, int texW)
{
    UBYTE row[PROBE_MAXW * 4];
    int i;

    memset(row, 0xa5, sizeof(row));
    glReadPixels(0, g_winH / 2, g_winW, 1,
                 GL_RGBA, GL_UNSIGNED_BYTE, row);
    for (i = 0; i < texW; ++i) {
        int x = (i * g_winW + g_winW / 2) / texW;
        out[i * 4 + 0] = row[x * 4 + 0];
        out[i * 4 + 1] = row[x * 4 + 1];
        out[i * 4 + 2] = row[x * 4 + 2];
        out[i * 4 + 3] = row[x * 4 + 3];
    }
}

static GLuint MakeTexture(void)
{
    GLuint name = 0;

    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    return name;
}

/* Diagnostic only: dump the R channel of all 16 cells of a 4x4 texture
 * drawn fullscreen, top row first. No pass/fail -- shows Swiss-cheese
 * uploads (partial direct transfers over stale VRAM) at a glance. */
/* Cell (cx,cy) of a WxH texture on the centred 90% quad: interior sample
 * point in GL pixels (probe read orientation, proven by the passing
 * rowmap cases). cy = 0 is the bottom row. Scales with the window. */
static void CellXY(int w, int h, int cx, int cy, int *px, int *py)
{
    *px = (g_winW / 20) + (cx * (g_winW * 9 / 10) + g_winW * 9 / 20) / w;
    *py = (g_winH / 20) + (cy * (g_winH * 9 / 10) + g_winH * 9 / 20) / h;
}

static void DumpCells(const char *id)
{
    char line[160];
    char *cursor = line;
    int x, y, px, py;
    UBYTE cell[4];

    cursor += sprintf(cursor, "PROBE id=%s cells=", id);
    for (y = 3; y >= 0; --y)
        for (x = 0; x < 4; ++x) {
            memset(cell, 0xa5, sizeof(cell));
            CellXY(4, 4, x, y, &px, &py);
            glReadPixels(px, py, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, cell);
            cursor += sprintf(cursor, "%s%d", (x || y != 3) ? "," : "",
                              (int)cell[0]);
        }
    puts(line);
}

static void DrawFullscreenTexturedQuad(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
    glEnd();
}

/* ---- Section 12 helpers: redefine bisection ----------------------- */

/* Bind an existing name and redefine it. This is the suite's
 * UploadRowmap path minus parameter setup: glTexImage2D on a texture
 * that already has an image. */
static void UploadInto(GLuint name, const UBYTE *img, int w, int h)
{
    glBindTexture(GL_TEXTURE_2D, name);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

/* The suite's DrawRowmapScene draw sequence, minus the upload. */
static void DrawRowmapFrame(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    DrawFullscreenTexturedQuad();
    glFinish();
}

/* Sample every row centre and count mismatches as probe failures.
 * The printed row list is the diagnosis if a case goes white. */
static void SampleRows(const char *id, int w, int h,
                       const UBYTE rows[4][4])
{
    char line[256];
    char *cursor = line;
    int y, px, py;
    UBYTE cell[4];

    cursor += sprintf(cursor, "PROBE id=%s rows=", id);
    for (y = 0; y < h; ++y) {
        CellXY(w, h, w / 2, y, &px, &py);
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, cell);
        cursor += sprintf(cursor, "%s%d,%d,%d", y ? ";" : "",
                          (int)cell[0], (int)cell[1], (int)cell[2]);
        ++checks;
        if (AbsDiff(cell[0], rows[y][0]) > PROBE_TOL ||
            AbsDiff(cell[1], rows[y][1]) > PROBE_TOL ||
            AbsDiff(cell[2], rows[y][2]) > PROBE_TOL)
            ++failures;
    }
    puts(line);
}

/* Sample every row centre from a full-frame captured buffer (same pixel
 * mapping as SampleRows / CellXY) and count mismatches as failures. */
static void SampleRowsCapture(const char *id, int w, int h,
                              const UBYTE rows[4][4], const UBYTE *cap)
{
    char line[256];
    char *cursor = line;
    int y, px, py;

    cursor += sprintf(cursor, "PROBE id=%s rows=", id);
    for (y = 0; y < h; ++y) {
        const UBYTE *cell;
        CellXY(w, h, w / 2, y, &px, &py);
        cell = cap + (py * g_winW + px) * 4;
        cursor += sprintf(cursor, "%s%d,%d,%d", y ? ";" : "",
                          (int)cell[0], (int)cell[1], (int)cell[2]);
        ++checks;
        if (AbsDiff(cell[0], rows[y][0]) > PROBE_TOL ||
            AbsDiff(cell[1], rows[y][1]) > PROBE_TOL ||
            AbsDiff(cell[2], rows[y][2]) > PROBE_TOL)
            ++failures;
    }
    puts(line);
}

/* Mirror of the suite's ResetGLState: everything the suite resets between
 * the benchmark loop and the verify draw. */
static void ResetSuiteState(void)
{
    glGetError();
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
    glViewport(0, 0, g_winW, g_winH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glGetError();
}

/* ---- Sections 13-18: Quake3 black-screen bisection (2026-09-29) -------
 *
 * The ioq3 -DAMIGAOS build only ever requests internalFormat
 * GL_RGB/GL_RGBA/GL_LUMINANCE with source GL_RGBA+UNSIGNED_BYTE (the
 * sized-format branches are compiled out), consumes GL_MAX_TEXTURE_SIZE
 * once at init and clamps <=0 to 0 (tr_init.c) -- after which the
 * Upload32 shrink loop collapses every texture to 0x0 (tr_image.c) --
 * and draws menu/UI through MGL_FLATFAN stretch pics (tr_backend.c).
 * Sections 13-18 replicate exactly that against the DEPLOYED dll first
 * (diagnosis) and the fixed build afterwards (validation).
 *
 *   13  environment: the queries Q3 makes at init (raw values + errors)
 *   14  internalFormat matrix, Q3 source format, 64x64
 *   15  mip completeness (default filter / explicit chain) + MGL_FLATFAN
 *   16  two-TMU MODULATE (Q3 world path preview)
 *   17  sized internal formats (informational on old dll, counted on the
 *       fixed build)
 *   18  oversize negative control (>2048 must stay rejected)
 * ---------------------------------------------------------------------- */

/* 64x64 quadrant replica of a Q3 menu texture: four solid quadrants far
 * from black and from each other, so black-out, channel swap and stride
 * errors are distinguishable. Alpha 255 everywhere. */
static UBYTE g_q3img[64 * 64 * 4];

/* Expected RGB per quadrant (bl, br, tl, tr) for each expansion class. */
static const int g_want_quad[4][3] = {
    { 250, 40, 40 }, { 40, 250, 40 }, { 40, 40, 250 }, { 250, 250, 250 }
};
static const int g_want_lum[4][3] = {   /* L expansion takes source R */
    { 250, 250, 250 }, { 40, 40, 40 }, { 40, 40, 40 }, { 250, 250, 250 }
};
static const int g_want_white[4][3] = { /* MiniGL ALPHA -> white RGB */
    { 255, 255, 255 }, { 255, 255, 255 },
    { 255, 255, 255 }, { 255, 255, 255 }
};
static const int g_want_solid[4][3] = { /* uniform (200,40,40) image */
    { 200, 40, 40 }, { 200, 40, 40 }, { 200, 40, 40 }, { 200, 40, 40 }
};

static void BuildQ3Image(void)
{
    int x, y;

    for (y = 0; y < 64; ++y)
        for (x = 0; x < 64; ++x) {
            int q = (y < 32 ? 0 : 2) + (x < 32 ? 0 : 1);
            UBYTE *px = g_q3img + (y * 64 + x) * 4;

            px[0] = (UBYTE)g_want_quad[q][0];
            px[1] = (UBYTE)g_want_quad[q][1];
            px[2] = (UBYTE)g_want_quad[q][2];
            px[3] = 255;
        }
}

/* Bind a fresh name and upload g_q3img with the given internalFormat.
 * Source is always GL_RGBA + UNSIGNED_BYTE -- exactly what Q3 sends.
 * Returns the post-upload GL error and clears it. */
static GLenum UploadQ3Image(GLuint *name, GLenum internalFormat)
{
    glGenTextures(1, name);
    glBindTexture(GL_TEXTURE_2D, *name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, 64, 64, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, g_q3img);
    return glGetError();
}

/* Draw the bound texture fullscreen through GL_QUADS or the Q3 menu
 * primitive MGL_FLATFAN (identical 4-vertex quad; a flatfan is a fan of
 * the same corners), then verify the four quadrant centres. */
static void VerifyQ3Quadrants(const char *idprefix, int flatfan,
                              const int want[4][3])
{
    static const int cellxy[4][2] = {
        { 1, 1 }, { 3, 1 }, { 1, 3 }, { 3, 3 }  /* quadrant -> /4 offset */
    };
    static const char *qname[4] = { "bl", "br", "tl", "tr" };
    UBYTE px[4];
    char id[64];
    int i;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);    /* ResetSuiteState leaves it disabled:
                                 * without this the draw is untextured
                                 * and paints the current (white) color */
    glBegin(flatfan ? MGL_FLATFAN : GL_QUADS);
    if (flatfan) {
        /* FLATFAN consumes DEVICE/pixel coordinates (pre-projected,
         * matrix-independent -- Q3's RE_StretchPic passes pixel coords).
         * NDC -1..1 fed to a fan would render a ~2x2-pixel triangle at
         * the origin and read back as clear color (the 2026-09-29 probe
         * false alarm). Full-window quad in pixels: */
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f((GLfloat)g_winW, 0.0f);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2f((GLfloat)g_winW, (GLfloat)g_winH);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, (GLfloat)g_winH);
    } else {
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
    }
    glEnd();
    glFinish();
    for (i = 0; i < 4; ++i) {
        sprintf(id, "%s.%s", idprefix, qname[i]);
        memset(px, 0xa5, sizeof(px));
        glReadPixels(cellxy[i][0] * g_winW / 4, cellxy[i][1] * g_winH / 4,
                     1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        ExpectRGB(id, px, want[i][0], want[i][1], want[i][2]);
    }
    sprintf(id, "%s.draw", idprefix);
    ExpectError(id, GL_NO_ERROR);
}

int main(int argc, char **argv)
{
    /* Biggest case below is a 4x1 RGBA readback scratch. */
    UBYTE scratch[16 * 4];
    GLuint name = 0;
    int opened = 0, live = 0;
    int index;

    for (index = 1; index < argc; ++index) {
        if (!strcmp(argv[index], "--width") && index + 1 < argc)
            g_winW = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--height") && index + 1 < argc)
            g_winH = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--depth") && index + 1 < argc)
            g_depth = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--buffers") && index + 1 < argc)
            g_buffers = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--sync") && index + 1 < argc)
            g_sync = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--lock") && index + 1 < argc)
            g_lock = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--loop") && index + 1 < argc)
            g_loop = atoi(argv[++index]);
        else if (!strcmp(argv[index], "--drawbuffer") && index + 1 < argc)
            g_drawbuf = atoi(argv[++index]);
        else {
            printf("usage: %s [--width W] [--height H] [--depth D] "
                   "[--buffers N]\n", argv[0]);
            return 10;
        }
    }
    if (g_winW < 64) g_winW = 64;
    if (g_winH < 64) g_winH = 64;
    if (g_winW > PROBE_MAXW) g_winW = PROBE_MAXW;
    if (g_winH > PROBE_MAXH) g_winH = PROBE_MAXH;

    setvbuf(stdout, NULL, _IONBF, 0);
    LogLine("BEGIN");
    printf("TEXTURE_FORMAT_PROBE BEGIN window=%dx%dx32\n",
           g_winW, g_winH);

    LogLine("pre-open");
    puts("PROBE progress pre-open");
    if (MiniGLOpen() == 0) {
        LogLine("open-FAIL");
        puts("PROBE id=open result=FAIL MiniGLOpen failed");
        return 20;
    }
    opened = 1;
    LogLine("open-ok");
    puts("PROBE progress open-ok");
    mglChooseWindowMode(GL_TRUE);
    LogLine("window-mode-ok");
    puts("PROBE progress window-mode-ok");
    mglChoosePixelDepth(g_depth);
    printf("PROBE progress pixel-depth-ok depth=%d\n", g_depth);
    {
        char db[32];
        sprintf(db, "pixel-depth-ok d=%d", g_depth);
        LogLine(db);
    }
    if (g_buffers > 0) {
        mglChooseNumberOfBuffers(g_buffers);
        printf("PROBE progress buffers=%d\n", g_buffers);
    }
    mglChooseGuardBand(GL_FALSE);
    mglChooseVertexBufferSize(4096);
    if (mglCreateContext(0, 0, g_winW, g_winH) == 0) {
        LogLine("context-FAIL");
        puts("PROBE id=context result=FAIL mglCreateContext failed");
        goto cleanup;
    }
    live = 1;
    LogLine("context-ok");
    puts("PROBE progress context-ok");
    if (g_sync >= 0) {
        mglEnableSync(g_sync ? GL_TRUE : GL_FALSE);
        printf("PROBE progress sync=%d\n", g_sync);
    }
    if (g_lock) {
        mglLockMode(MGL_LOCK_SMART);
        puts("PROBE progress lock=smart");
    }
    if (g_drawbuf) {
        glDrawBuffer(GL_BACK);
        puts("PROBE progress drawbuffer=back");
    }

    /* ---- 19b. EARLY placement bisect: same solid-texture draw run
     * before anything else, isolating late-state pollution (ResetSuiteState
     * TMU1 toggling), texcoord style, and size in one shot. ---- */
    {
        static UBYTE early[64 * 64 * 4];
        GLuint name;
        UBYTE px[4];
        int i;

        for (i = 0; i < 64 * 64; ++i) {
            early[i * 4 + 0] = 200;
            early[i * 4 + 1] = 40;
            early[i * 4 + 2] = 40;
            early[i * 4 + 3] = 255;
        }
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, early);
        ExpectError("early.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(1.0f, 1.0f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, 1.0f, 0.0f);
        glEnd();
        glFinish();
        memset(px, 0xa5, sizeof(px));
        glReadPixels(g_winW / 2, g_winH / 2, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, px);
        ExpectRGB("early.8.fulluv", px, 200, 40, 40);
        glDeleteTextures(1, &name);
    }

    glViewport(0, 0, g_winW, g_winH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    ExpectError("setup", GL_NO_ERROR);

    /* 1. GL_LUMINANCE 4x1 ramp through GL_REPLACE. A stride bug (reading
     * every 4th source byte as the next texel) shows as wrong grey levels
     * on texels 1..3. */
    {
        static const UBYTE lum[4] = { 0, 85, 170, 255 };
        static const int want[4] = { 0, 85, 170, 255 };
        int i;

        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 4, 1, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, lum);
        ExpectError("luminance.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawStrip(4);
        glFinish();
        ExpectError("luminance.draw", GL_NO_ERROR);
        ReadStrip(scratch, 4);
        for (i = 0; i < 4; ++i) {
            char id[48];
            sprintf(id, "luminance.texel%d", i);
            ExpectRGB(id, scratch + i * 4, want[i], want[i], want[i]);
        }
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 2. GL_LUMINANCE_ALPHA 2x1: opaque texel via REPLACE, half-alpha
     * texel blended over black. Checks both the L expansion and the A
     * channel in one draw. */
    {
        static const UBYTE la[4] = { 200, 255, 200, 128 };

        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 2, 1, 0,
                     GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la);
        ExpectError("lumalpha.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        DrawStrip(2);
        glDisable(GL_BLEND);
        glFinish();
        ExpectError("lumalpha.draw", GL_NO_ERROR);
        ReadStrip(scratch, 2);
        ExpectRGB("lumalpha.opaque", scratch + 0, 200, 200, 200);
        ExpectRGB("lumalpha.half", scratch + 4, 100, 100, 100);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 3. GL_ALPHA 2x1, MODULATE with white, blended over black. With
     * MiniGL's white-RGB expansion this is a white wash at full and half
     * strength; spec-zero-RGB would render black here. */
    {
        static const UBYTE alpha[2] = { 255, 128 };

        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 2, 1, 0,
                     GL_ALPHA, GL_UNSIGNED_BYTE, alpha);
        ExpectError("alpha.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        DrawStrip(2);
        glDisable(GL_BLEND);
        glFinish();
        ExpectError("alpha.draw", GL_NO_ERROR);
        ReadStrip(scratch, 2);
        ExpectRGB("alpha.full", scratch + 0, 255, 255, 255);
        ExpectRGB("alpha.half", scratch + 4, 128, 128, 128);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 4. MGL_UBYTE_BGRA: source bytes are B,G,R,A. A channel swap (e.g.
     * treating them as RGBA) renders texel0 reddish instead of blueish. */
    {
        static const UBYTE bgra[8] =
            { 200, 150, 100, 255, 10, 20, 30, 255 };

        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0,
                     MGL_UBYTE_BGRA, GL_UNSIGNED_BYTE, bgra);
        ExpectError("bgra.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawStrip(2);
        glFinish();
        ExpectError("bgra.draw", GL_NO_ERROR);
        ReadStrip(scratch, 2);
        ExpectRGB("bgra.texel0", scratch + 0, 100, 150, 200);
        ExpectRGB("bgra.texel1", scratch + 4, 30, 20, 10);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 5. MGL_UBYTE_ARGB: source bytes are A,R,G,B. */
    {
        static const UBYTE argb[8] =
            { 255, 100, 150, 200, 255, 30, 20, 10 };

        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0,
                     MGL_UBYTE_ARGB, GL_UNSIGNED_BYTE, argb);
        ExpectError("argb.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawStrip(2);
        glFinish();
        ExpectError("argb.draw", GL_NO_ERROR);
        ReadStrip(scratch, 2);
        ExpectRGB("argb.texel0", scratch + 0, 100, 150, 200);
        ExpectRGB("argb.texel1", scratch + 4, 30, 20, 10);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 6. TexSubImage2D with a GL_LUMINANCE patch: 4x4 mid-grey RGBA base,
     * 2x2 white-luminance patch at (1,1). Catches stride bugs in the
     * update path (mglQ3 tex_UpdateScanlineL8_ARGB / R200
     * UploadTextureRegion / PPC command-26). */
    {
        UBYTE base[4 * 4 * 4];
        static const UBYTE patch[4] = { 255, 255, 255, 255 };
        UBYTE cell[4];
        int x, y;

        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x) {
                base[(y * 4 + x) * 4 + 0] = 128;
                base[(y * 4 + x) * 4 + 1] = 128;
                base[(y * 4 + x) * 4 + 2] = 128;
                base[(y * 4 + x) * 4 + 3] = 255;
            }
        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, base);
        ExpectError("subimage.base", GL_NO_ERROR);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 2, 2,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, patch);
        ExpectError("subimage.patch", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
        glEnd();
        glFinish();
        ExpectError("subimage.draw", GL_NO_ERROR);
        /* Texture (0,0) is bottom-left; cell centres at 1/8,3/8,5/8,7/8
         * of each axis. Outside the patch: cell (0,0). Inside: (1,1). */
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(g_winW / 8, g_winH / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage.outside", cell, 128, 128, 128);
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(3 * g_winW / 8, 3 * g_winH / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage.inside", cell, 255, 255, 255);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 7. TexSubImage2D with a GL_RGBA control patch: same geometry as
     * case 6, white 2x2 RGBA patch over mid-grey. If this passes while
     * case 6 fails, the subimage path is fine and the bug is specific
     * to the LUMINANCE source format on that path. */
    {
        UBYTE base[4 * 4 * 4];
        static const UBYTE patch[2 * 2 * 4] = {
            255,255,255,255, 255,255,255,255,
            255,255,255,255, 255,255,255,255
        };
        UBYTE cell[4];
        int x, y;

        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x) {
                base[(y * 4 + x) * 4 + 0] = 128;
                base[(y * 4 + x) * 4 + 1] = 128;
                base[(y * 4 + x) * 4 + 2] = 128;
                base[(y * 4 + x) * 4 + 3] = 255;
            }
        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, base);
        ExpectError("subimage-rgba.base", GL_NO_ERROR);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 2, 2,
                        GL_RGBA, GL_UNSIGNED_BYTE, patch);
        ExpectError("subimage-rgba.patch", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawFullscreenTexturedQuad();
        glFinish();
        ExpectError("subimage-rgba.draw", GL_NO_ERROR);
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(g_winW / 8, g_winH / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage-rgba.outside", cell, 128, 128, 128);
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(3 * g_winW / 8, 3 * g_winH / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage-rgba.inside", cell, 255, 255, 255);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 8. Base-upload integrity: fresh 4x4 mid-grey RGBA texture, no patch
     * at all, dump all 16 cells. Tells whether a full direct upload can
     * leave stale/VRAM-garbage texels behind (Swiss cheese) or is solid.
     * If solid here, case 6's 170 comes from the patch step, not the base. */
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
        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, base);
        ExpectError("base16.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawFullscreenTexturedQuad();
        glFinish();
        ExpectError("base16.draw", GL_NO_ERROR);
        DumpCells("base16");
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 9. Palette-seeded LUMINANCE patch: seed color-table entry 127 to
     * red, then patch 2x2 with L bytes 0x7F. Three-way distinguisher:
     *   (127,127,127) -> correct luminance expansion;
     *   (255,0,0)     -> patch misread as GL_COLOR_INDEX + palette works;
     *   (0,0,0)       -> misread as GL_COLOR_INDEX + palette empty;
     *   (170,170,170) -> same wrong value as case 6 (stable misbehaviour).
     * The glColorTable error is reported informatively, not counted:
     * forwarding it over the wire is a separate capability. */
    {
        UBYTE palette[256 * 4];
        UBYTE base[4 * 4 * 4];
        static const UBYTE patch[4] = { 0x7f, 0x7f, 0x7f, 0x7f };
        UBYTE cell[4];
        int x, y;

        memset(palette, 0, sizeof(palette));
        palette[127 * 4 + 0] = 255;
        palette[127 * 4 + 3] = 255;
        glColorTable(GL_COLOR_TABLE, GL_RGBA, 256,
                     GL_RGBA, GL_UNSIGNED_BYTE, palette);
        printf("PROBE id=palette.seed info=1 gl_error=%lu\n",
               (unsigned long)glGetError());
        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x) {
                base[(y * 4 + x) * 4 + 0] = 128;
                base[(y * 4 + x) * 4 + 1] = 128;
                base[(y * 4 + x) * 4 + 2] = 128;
                base[(y * 4 + x) * 4 + 3] = 255;
            }
        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, base);
        ExpectError("subimage-pal.base", GL_NO_ERROR);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 2, 2,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, patch);
        ExpectError("subimage-pal.patch", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawFullscreenTexturedQuad();
        glFinish();
        ExpectError("subimage-pal.draw", GL_NO_ERROR);
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(3 * g_winW / 8, 3 * g_winH / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage-pal.inside", cell, 127, 127, 127);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 10. Case 6 repeated with explicit UNPACK_ALIGNMENT 1: removes the
     * default-alignment-4 row-padding question from the failing case.
     * If this passes while case 6 fails, the bug is unpack-stride
     * handling, not the L expansion itself. */
    {
        UBYTE base[4 * 4 * 4];
        static const UBYTE patch[4] = { 255, 255, 255, 255 };
        UBYTE cell[4];
        int x, y;

        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x) {
                base[(y * 4 + x) * 4 + 0] = 128;
                base[(y * 4 + x) * 4 + 1] = 128;
                base[(y * 4 + x) * 4 + 2] = 128;
                base[(y * 4 + x) * 4 + 3] = 255;
            }
        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, base);
        ExpectError("subimage-align1.base", GL_NO_ERROR);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 2, 2,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, patch);
        ExpectError("subimage-align1.patch", GL_NO_ERROR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawFullscreenTexturedQuad();
        glFinish();
        ExpectError("subimage-align1.draw", GL_NO_ERROR);
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(3 * g_winW / 8, 3 * g_winH / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage-align1.inside", cell, 255, 255, 255);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 11. Row-identity map: distinct solid rows pin down the exact
     * write stride, sample stride, and stale fill in one shot.
     *   11a 4x4 rows R/G/B/W, one sample per row;
     *   11b 8x2 rows dark/bright, one sample per row;
     *   11c 16x2 same colours (width*4 == 64: no pitch gap expected).
     * Informational only (no pass/fail): the pattern IS the result. */
    {
        UBYTE img4[4 * 4 * 4], img8[8 * 2 * 4], img16[16 * 2 * 4];
        static const UBYTE rows4[4][4] = {
            { 200, 0, 0, 255 }, { 0, 200, 0, 255 },
            { 0, 0, 200, 255 }, { 255, 255, 255, 255 }
        };
        int x, y, px, py;
        UBYTE cell[4];
        char line[192];
        char *cursor;

        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                memcpy(img4 + (y * 4 + x) * 4, rows4[y], 4);
        for (y = 0; y < 2; ++y)
            for (x = 0; x < 8; ++x) {
                img8[(y * 8 + x) * 4 + 0] = y ? 200 : 10;
                img8[(y * 8 + x) * 4 + 1] = y ? 210 : 20;
                img8[(y * 8 + x) * 4 + 2] = y ? 220 : 30;
                img8[(y * 8 + x) * 4 + 3] = 255;
            }
        for (y = 0; y < 2; ++y)
            for (x = 0; x < 16; ++x) {
                img16[(y * 16 + x) * 4 + 0] = y ? 200 : 10;
                img16[(y * 16 + x) * 4 + 1] = y ? 210 : 20;
                img16[(y * 16 + x) * 4 + 2] = y ? 220 : 30;
                img16[(y * 16 + x) * 4 + 3] = 255;
            }
        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img4);
        ExpectError("rowmap4.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawFullscreenTexturedQuad();
        glFinish();
        ExpectError("rowmap4.draw", GL_NO_ERROR);
        cursor = line + sprintf(line, "PROBE id=rowmap4 rows=");
        for (y = 0; y < 4; ++y) {
            CellXY(4, 4, 1, y, &px, &py);
            memset(cell, 0xa5, sizeof(cell));
            glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, cell);
            cursor += sprintf(cursor, "%s%d,%d,%d", y ? ";" : "",
                              (int)cell[0], (int)cell[1], (int)cell[2]);
        }
        puts(line);
        glDeleteTextures(1, &name);
        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 2, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img8);
        ExpectError("rowmap8.upload", GL_NO_ERROR);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawFullscreenTexturedQuad();
        glFinish();
        ExpectError("rowmap8.draw", GL_NO_ERROR);
        cursor = line + sprintf(line, "PROBE id=rowmap8 rows=");
        for (y = 0; y < 2; ++y) {
            CellXY(8, 2, 3, y, &px, &py);
            memset(cell, 0xa5, sizeof(cell));
            glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, cell);
            cursor += sprintf(cursor, "%s%d,%d,%d", y ? ";" : "",
                              (int)cell[0], (int)cell[1], (int)cell[2]);
        }
        puts(line);
        glDeleteTextures(1, &name);
        name = MakeTexture();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 2, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img16);
        ExpectError("rowmap16.upload", GL_NO_ERROR);
        glClear(GL_COLOR_BUFFER_BIT);
        DrawFullscreenTexturedQuad();
        glFinish();
        ExpectError("rowmap16.draw", GL_NO_ERROR);
        cursor = line + sprintf(line, "PROBE id=rowmap16 rows=");
        for (y = 0; y < 2; ++y) {
            CellXY(16, 2, 7, y, &px, &py);
            memset(cell, 0xa5, sizeof(cell));
            glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, cell);
            cursor += sprintf(cursor, "%s%d,%d,%d", y ? ";" : "",
                              (int)cell[0], (int)cell[1], (int)cell[2]);
        }
        puts(line);
        glDeleteTextures(1, &name);
        name = 0;
    }

    /* 12. Redefine bisection. The test suite re-uploads into the SAME
     * texture name every animation frame and then verifies, while this
     * probe always used a fresh name per upload (and passed). If a
     * redefinition of a live direct texture is what turns row0 white
     * in the suite, one of these cases reproduces it standalone:
     *   12a fresh name, single upload (control, known good);
     *   12b same name uploaded twice before the one draw;
     *   12c same name uploaded eight times, draw last;
     *   12d suite frame pattern: 3x (upload, draw, swap) then final
     *       upload + draw without swap;
     *   12e size-changing redefinition on one name: 4x4 drawn, then
     *       the same name redefined to 8x2 and drawn. */
    {
        UBYTE img4[4 * 4 * 4], img8[8 * 2 * 4];
        static const UBYTE rows4[4][4] = {
            { 200, 0, 0, 255 }, { 0, 200, 0, 255 },
            { 0, 0, 200, 255 }, { 255, 255, 255, 255 }
        };
        static const UBYTE rows8[2][4] = {
            { 10, 20, 30, 255 }, { 200, 210, 220, 255 }
        };
        GLuint fresh;
        int x, y, rep;

        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                memcpy(img4 + (y * 4 + x) * 4, rows4[y], 4);
        for (y = 0; y < 2; ++y)
            for (x = 0; x < 8; ++x)
                memcpy(img8 + (y * 8 + x) * 4, rows8[y], 4);

        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

        fresh = MakeTexture();
        UploadInto(fresh, img4, 4, 4);
        ExpectError("redef.a-once.upload", GL_NO_ERROR);
        DrawRowmapFrame();
        SampleRows("redef.a-once", 4, 4, rows4);
        glDeleteTextures(1, &fresh);

        fresh = MakeTexture();
        UploadInto(fresh, img4, 4, 4);
        UploadInto(fresh, img4, 4, 4);
        DrawRowmapFrame();
        SampleRows("redef.b-twice", 4, 4, rows4);
        glDeleteTextures(1, &fresh);

        fresh = MakeTexture();
        for (rep = 0; rep < 8; ++rep)
            UploadInto(fresh, img4, 4, 4);
        DrawRowmapFrame();
        SampleRows("redef.c-eight", 4, 4, rows4);
        glDeleteTextures(1, &fresh);

        fresh = MakeTexture();
        for (rep = 0; rep < 3; ++rep) {
            UploadInto(fresh, img4, 4, 4);
            DrawRowmapFrame();
            mglSwitchDisplay();
        }
        UploadInto(fresh, img4, 4, 4);
        DrawRowmapFrame();
        SampleRows("redef.d-swap", 4, 4, rows4);
        glDeleteTextures(1, &fresh);

        fresh = MakeTexture();
        UploadInto(fresh, img4, 4, 4);
        DrawRowmapFrame();
        UploadInto(fresh, img8, 8, 2);
        ExpectError("redef.e-resize.upload", GL_NO_ERROR);
        DrawRowmapFrame();
        SampleRows("redef.e-resize", 8, 2, rows8);
        glDeleteTextures(1, &fresh);
        fresh = 0;

        /* 12f: the suite's RunTest/VerifyRowIdentity pattern end to end,
         * 1:1: 15 benchmark-style (upload, draw, swap) frames, then
         * ResetGLState + draw(0), then three calibration-style solid
         * captures, then ResetGLState again, the final upload + draw,
         * and a full-frame capture the row samples come from. Combined
         * with --width 512 --height 320 this is the failing suite run
         * exactly. */
        {
            static UBYTE cap[PROBE_MAXW * PROBE_MAXH * 4];
            static const GLfloat calib[3][3] = {
                { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f }
            };
            int c;

            fresh = MakeTexture();
            for (rep = 0; rep < 15; ++rep) {
                UploadInto(fresh, img4, 4, 4);
                DrawRowmapFrame();
                mglSwitchDisplay();
            }
            ResetSuiteState();
            UploadInto(fresh, img4, 4, 4);
            DrawRowmapFrame();
            for (c = 0; c < 3; ++c) {
                glClearColor(calib[c][0], calib[c][1], calib[c][2], 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glFinish();
                glReadPixels(0, 0, g_winW, g_winH, GL_RGBA,
                             GL_UNSIGNED_BYTE, cap);
            }
            ResetSuiteState();
            UploadInto(fresh, img4, 4, 4);
            DrawRowmapFrame();
            glFinish();
            glReadPixels(0, 0, g_winW, g_winH, GL_RGBA,
                         GL_UNSIGNED_BYTE, cap);
            SampleRowsCapture("redef.f-suite", 4, 4, rows4, cap);
            glDeleteTextures(1, &fresh);
            fresh = 0;
        }
    }

    /* ---- 13. environment: Q3's init queries ------------------------ */
    ResetSuiteState();
    BuildQ3Image();
    LogLine("sec13-env");
    printf("PROBE id=env.err0 gl_error=%lu\n",
           (unsigned long)glGetError());
    printf("PROBE id=env.version str=%s\n", glGetString(GL_VERSION));
    printf("PROBE id=env.renderer str=%s\n", glGetString(GL_RENDERER));
    printf("PROBE id=env.extensions str=%s\n",
           glGetString(GL_EXTENSIONS));
    {
        GLint v;

        /* Decisive number: no GL_MAX_TEXTURE_SIZE case in glGetIntegerv
         * leaves the caller variable untouched + sticky INVALID_ENUM;
         * Q3 clamps <=0 to 0 and then shrinks every texture to 0x0. */
        v = (GLint)0x5A5A5A5A;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
        printf("PROBE id=env.maxtexsize value=%ld gl_error=%lu %s\n",
               (long)v, (unsigned long)glGetError(),
               (v == (GLint)0x5A5A5A5A) ? "UNWRITTEN" : "written");
        v = (GLint)0x5A5A5A5A;
        glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &v);
        printf("PROBE id=env.maxtexunits value=%ld gl_error=%lu %s\n",
               (long)v, (unsigned long)glGetError(),
               (v == (GLint)0x5A5A5A5A) ? "UNWRITTEN" : "written");
    }

    /* ---- 14. internalFormat matrix over Q3's exact source format ---- */
    {
        static const struct {
            const char *name;
            GLenum ifmt;
            const int (*want)[3];
            int nearest;    /* 1: NEAREST sampling control (upload +
                             * sampling sanity vs the LINEAR filter
                             * cases -- separates upload defects from
                             * filter-path defects in one run) */
        } cases[7] = {
            { "fmt.rgb",       GL_RGB,            g_want_quad, 0 },
            { "fmt.rgba",      GL_RGBA,           g_want_quad, 0 },
            { "fmt.rgba.nearest", GL_RGBA,        g_want_quad, 1 },
            { "fmt.rgb5a1",    GL_RGB5_A1,        g_want_quad, 0 },
            { "fmt.lum",       GL_LUMINANCE,      g_want_lum, 0 },
            { "fmt.alpha",     GL_ALPHA,          g_want_white, 0 },
            { "fmt.lumalpha",  GL_LUMINANCE_ALPHA, g_want_lum, 0 }
        };
        GLuint name;
        GLenum err;
        char id[64];
        int i;

        for (i = 0; i < 7; ++i) {
            ResetSuiteState();
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            err = UploadQ3Image(&name, cases[i].ifmt);
            if (cases[i].nearest) {
                glBindTexture(GL_TEXTURE_2D, name);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_NEAREST);
            }
            sprintf(id, "%s.upload", cases[i].name);
            ExpectError(id, GL_NO_ERROR);
            if (err == GL_NO_ERROR)
                VerifyQ3Quadrants(cases[i].name, 0, cases[i].want);
            else
                printf("PROBE id=%s skipped: upload failed\n",
                       cases[i].name);
            glDeleteTextures(1, &name);
        }
    }

    /* ---- 15. mip completeness + the menu primitive ------------------ */
    {
        static UBYTE solidcol[64 * 64 * 4];
        GLuint name;
        GLenum err;
        int lvl;

        /* 15a: default state (min filter GL_NEAREST_MIPMAP_LINEAR),
         * level 0 only. GL says incomplete; if this renders black,
         * any Q3 texture missing its tail mips goes black. */
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        /* deliberately no filter/wrap setup: pure default state */
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, g_q3img);
        ExpectError("mip.default.upload", GL_NO_ERROR);
        VerifyQ3Quadrants("mip.default", 0, g_want_quad);
        glDeleteTextures(1, &name);

        /* 15b: explicit GL_LINEAR control on a fresh name. */
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        err = UploadQ3Image(&name, GL_RGBA);
        ExpectError("mip.linear.upload", GL_NO_ERROR);
        if (err == GL_NO_ERROR)
            VerifyQ3Quadrants("mip.linear", 0, g_want_quad);
        glDeleteTextures(1, &name);

        /* 15c: Q3 world-texture replica -- explicit mipmap filter and a
         * full 7-level chain (64..1) of a SOLID colour (200,40,40), so
         * every level's centre sample is deterministic. Catches level>0
         * rejection on the direct texture path. */
        for (lvl = 0; lvl < 64 * 64 * 4; lvl += 4) {
            solidcol[lvl + 0] = 200;
            solidcol[lvl + 1] = 40;
            solidcol[lvl + 2] = 40;
            solidcol[lvl + 3] = 255;
        }
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (lvl = 0; lvl < 7; ++lvl) {
            int dim = 64 >> lvl;
            glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA, dim, dim, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, solidcol);
            if (glGetError() != GL_NO_ERROR) {
                printf("PROBE id=mip.chain.level%d err=FAIL\n", lvl);
                ++checks;
                ++failures;
            }
        }
        ExpectError("mip.chain.upload", GL_NO_ERROR);
        VerifyQ3Quadrants("mip.chain", 0, g_want_solid);
        glDeleteTextures(1, &name);

        /* 15d: the Q3 menu primitive MGL_FLATFAN (RE_StretchPic) vs the
         * GL_QUADS control, same texture, same corners. If the fan fails
         * while quads pass, Q3's black menu is a primitive-path bug, not
         * a texture-format bug. */
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        err = UploadQ3Image(&name, GL_RGBA);
        ExpectError("flatfan.quads.upload", GL_NO_ERROR);
        if (err == GL_NO_ERROR) {
            VerifyQ3Quadrants("flatfan.quads", 0, g_want_quad);
            VerifyQ3Quadrants("flatfan.fan", 1, g_want_quad);
        }
        glDeleteTextures(1, &name);
    }

    /* ---- 16. two-TMU MODULATE (Q3 world path preview) --------------- */
    {
        static UBYTE t0img[64 * 64 * 4], t1img[64 * 64];
        GLuint n0 = 0, n1 = 0;
        UBYTE px[4];
        int i;

        for (i = 0; i < 64 * 64; ++i) {
            t0img[i * 4 + 0] = 200;
            t0img[i * 4 + 1] = 200;
            t0img[i * 4 + 2] = 200;
            t0img[i * 4 + 3] = 255;
            t1img[i] = 128;
        }
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnable(GL_TEXTURE_2D);
        glGenTextures(1, &n0);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glBindTexture(GL_TEXTURE_2D, n0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, t0img);
        ExpectError("mtex.tmu0.upload", GL_NO_ERROR);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glGenTextures(1, &n1);
        glBindTexture(GL_TEXTURE_2D, n1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 64, 64, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, t1img);
        ExpectError("mtex.tmu1.upload", GL_NO_ERROR);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.5f, 0.5f);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.5f, 0.5f);
        glVertex3f(-1.0f, -1.0f, 0.0f);
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.5f, 0.5f);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.5f, 0.5f);
        glVertex3f( 1.0f, -1.0f, 0.0f);
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.5f, 0.5f);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.5f, 0.5f);
        glVertex3f( 1.0f,  1.0f, 0.0f);
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.5f, 0.5f);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.5f, 0.5f);
        glVertex3f(-1.0f,  1.0f, 0.0f);
        glEnd();
        glFinish();
        /* 200 * 128 / 255 = 100.4 -> 100 within tolerance. Black here
         * means a missing TMU texture; 200 means TMU1 was dropped. */
        memset(px, 0xa5, sizeof(px));
        glReadPixels(g_winW / 2, g_winH / 2, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, px);
        ExpectRGB("mtex.2tmu", px, 100, 100, 100);
        /* TMU0-only control with TMU1 still bound but disabled. */
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(0.5f, 0.5f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glTexCoord2f(0.5f, 0.5f); glVertex3f( 1.0f, -1.0f, 0.0f);
        glTexCoord2f(0.5f, 0.5f); glVertex3f( 1.0f,  1.0f, 0.0f);
        glTexCoord2f(0.5f, 0.5f); glVertex3f(-1.0f,  1.0f, 0.0f);
        glEnd();
        glFinish();
        memset(px, 0xa5, sizeof(px));
        glReadPixels(g_winW / 2, g_winH / 2, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, px);
        ExpectRGB("mtex.tmu0only", px, 200, 200, 200);
        glDeleteTextures(1, &n0);
        glDeleteTextures(1, &n1);
    }

    /* ---- 17. sized internal formats (fixed-build validation) -------- */
    {
        static const struct {
            const char *name;
            GLenum ifmt;
            const int (*want)[3];
        } cases[7] = {
            { "sized.rgba8", GL_RGBA8,            g_want_quad },
            { "sized.rgb8",  GL_RGB8,             g_want_quad },
            { "sized.rgb5",  GL_RGB5,             g_want_quad },
            { "sized.rgba4", 0x8052 /*GL_RGBA4*/, g_want_quad },
            { "sized.lum8",  GL_LUMINANCE8,       g_want_lum },
            { "sized.la8",   GL_LUMINANCE8_ALPHA8, g_want_lum },
            { "sized.a8",    GL_ALPHA8,           g_want_white }
        };
        GLuint name;
        GLenum err;
        int i;

        for (i = 0; i < 7; ++i) {
            ResetSuiteState();
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            err = UploadQ3Image(&name, cases[i].ifmt);
            /* Informational: the old deployed dll rejects these with
             * GL_INVALID_ENUM. Content verification only runs when the
             * upload is accepted (i.e. on the fixed build). */
            printf("PROBE id=%s.upload err=%lu info=1 (fixed build: 0)\n",
                   cases[i].name, (unsigned long)err);
            if (err == GL_NO_ERROR)
                VerifyQ3Quadrants(cases[i].name, 0, cases[i].want);
            glDeleteTextures(1, &name);
        }
    }

    /* ---- 18. oversize negative control ------------------------------ */
    {
        GLuint name;

        ResetSuiteState();
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4096, 4, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, g_q3img);
        ExpectError("oversize.upload", GL_INVALID_VALUE);
        glDeleteTextures(1, &name);
    }

    /* ---- 19. size bisect: where does white-texture start? ---------- */
    {
        static const int sizes[6] = { 8, 16, 32, 64, 128, 256 };
        static UBYTE solid[256 * 256 * 4];
        GLuint name;
        UBYTE px[4];
        char id[64];
        int s, i;

        for (i = 0; i < 256 * 256; ++i) {
            solid[i * 4 + 0] = 200;
            solid[i * 4 + 1] = 40;
            solid[i * 4 + 2] = 40;
            solid[i * 4 + 3] = 255;
        }
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        for (s = 0; s < 6; ++s) {
            int dim = sizes[s];

            glGenTextures(1, &name);
            glBindTexture(GL_TEXTURE_2D, name);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            GL_NEAREST);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dim, dim, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, solid);
            ExpectError("sizebisect.upload", GL_NO_ERROR);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBegin(GL_QUADS);
            glTexCoord2f(0.5f / dim, 0.5f / dim);
            glVertex3f(-1.0f, -1.0f, 0.0f);
            glTexCoord2f(1.0f - 0.5f / dim, 0.5f / dim);
            glVertex3f(1.0f, -1.0f, 0.0f);
            glTexCoord2f(1.0f - 0.5f / dim, 1.0f - 0.5f / dim);
            glVertex3f(1.0f, 1.0f, 0.0f);
            glTexCoord2f(0.5f / dim, 1.0f - 0.5f / dim);
            glVertex3f(-1.0f, 1.0f, 0.0f);
            glEnd();
            glFinish();
            memset(px, 0xa5, sizeof(px));
            glReadPixels(g_winW / 2, g_winH / 2, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, px);
            sprintf(id, "sizebisect.%d.nearest", dim);
            ExpectRGB(id, px, 200, 40, 40);
            glDeleteTextures(1, &name);
        }
        /* 16x16 with GL_LINEAR: separates filter-path from size-path.
         * If small+LINEAR is white, the defect tracks the filter; if it
         * passes, it tracks the size. */
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, solid);
        ExpectError("sizebisect.upload", GL_NO_ERROR);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(1.0f, 1.0f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, 1.0f, 0.0f);
        glEnd();
        glFinish();
        memset(px, 0xa5, sizeof(px));
        glReadPixels(g_winW / 2, g_winH / 2, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, px);
        ExpectRGB("sizebisect.16.linear", px, 200, 40, 40);
        glDeleteTextures(1, &name);
    }

    /* ---- 20. Q3 menu replica: RB_SetGL2D + RB_StretchPic pipeline ---
     * The menu draws tess surfaces (4 verts / 6 indexes) through
     * glDrawElements(GL_TRIANGLES, GL_UNSIGNED_SHORT) under glOrtho 2D
     * with SRC_ALPHA blending -- none of which the sections above test.
     * Variants decompose the path: A full replica; B blend off;
     * C arrays without glOrtho (NDC); D immediate mode with glOrtho.
     * Expectations are y-flipped for ortho variants: glOrtho maps y=0
     * to the top of the viewport, so readback quadrants swap rows. */
    {
        /* readback (bl, br, tl, tr) expectations for a pixel-space
         * ortho draw of g_q3img (vertically flipped vs g_want_quad) */
        static const int want_vflip[4][3] = {
            { 40, 40, 250 }, { 250, 250, 250 },
            { 250, 40, 40 }, { 40, 250, 40 }
        };
        static float verts[4][3];
        static float tuv[4][2];
        static UBYTE cols[4][4];
        static const GLushort idx[6] = { 3, 0, 2, 2, 0, 1 };
        GLuint name;
        GLenum err;
        int i;

        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        err = UploadQ3Image(&name, GL_RGBA);
        ExpectError("menu.upload", GL_NO_ERROR);
        if (err != GL_NO_ERROR)
            goto menu_done;

        verts[0][0] = 0.0f;           verts[0][1] = 0.0f;
        verts[1][0] = (float)g_winW;  verts[1][1] = 0.0f;
        verts[2][0] = (float)g_winW;  verts[2][1] = (float)g_winH;
        verts[3][0] = 0.0f;           verts[3][1] = (float)g_winH;
        verts[0][2] = verts[1][2] = verts[2][2] = verts[3][2] = 0.0f;
        tuv[0][0] = 0.0f; tuv[0][1] = 0.0f;
        tuv[1][0] = 1.0f; tuv[1][1] = 0.0f;
        tuv[2][0] = 1.0f; tuv[2][1] = 1.0f;
        tuv[3][0] = 0.0f; tuv[3][1] = 1.0f;
        for (i = 0; i < 4; ++i) {
            cols[i][0] = 255; cols[i][1] = 255;
            cols[i][2] = 255; cols[i][3] = 255;
        }

        /* Q3 2D state (RB_SetGL2D): ortho, two-sided, no depth,
         * SRC_ALPHA blend. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        /* A: full replica -- ortho + blend + arrays + DrawElements. */
        glViewport(0, 0, g_winW, g_winH);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (GLdouble)g_winW, (GLdouble)g_winH, 0.0, 0.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glVertexPointer(3, GL_FLOAT, 0, verts);
        glTexCoordPointer(2, GL_FLOAT, 0, tuv);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, cols);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
        glFinish();
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        {
            UBYTE px[4];
            char id[64];
            int ci;

            for (ci = 0; ci < 4; ++ci) {
                static const int cellxy2[4][2] = {
                    { 1, 1 }, { 3, 1 }, { 1, 3 }, { 3, 3 }
                };
                static const char *qn[4] = { "bl", "br", "tl", "tr" };
                sprintf(id, "menu.a-full.%s", qn[ci]);
                memset(px, 0xa5, sizeof(px));
                glReadPixels(cellxy2[ci][0] * g_winW / 4,
                             cellxy2[ci][1] * g_winH / 4, 1, 1,
                             GL_RGBA, GL_UNSIGNED_BYTE, px);
                ExpectRGB(id, px, want_vflip[ci][0],
                          want_vflip[ci][1], want_vflip[ci][2]);
            }
            sprintf(id, "menu.a-full.draw");
            ExpectError(id, GL_NO_ERROR);
        }

        /* B: same, blend disabled. */
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glDisable(GL_BLEND);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
        glFinish();
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        {
            UBYTE px[4];

            memset(px, 0xa5, sizeof(px));
            glReadPixels(g_winW / 4, g_winH / 4, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, px);
            ExpectRGB("menu.b-noblend.bl", px,
                      want_vflip[0][0], want_vflip[0][1],
                      want_vflip[0][2]);
        }

        /* C: arrays without glOrtho (identity projection, NDC quad,
         * full 0..1 texcoords) -- isolates the ortho transform. */
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, name);
        {
            static float ndc[4][3] = {
                { -1.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f },
                { 1.0f, 1.0f, 0.0f }, { -1.0f, 1.0f, 0.0f }
            };
            UBYTE px[4];

            glVertexPointer(3, GL_FLOAT, 0, ndc);
            glTexCoordPointer(2, GL_FLOAT, 0, tuv);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, cols);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
            glFinish();
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            memset(px, 0xa5, sizeof(px));
            glReadPixels(g_winW / 4, g_winH / 4, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, px);
            ExpectRGB("menu.c-ortho-off.bl", px,
                      g_want_quad[0][0], g_want_quad[0][1],
                      g_want_quad[0][2]);
        }

        /* D: immediate mode WITH ortho pixel coords -- isolates the
         * array draw from the ortho transform. */
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, name);
        glViewport(0, 0, g_winW, g_winH);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (GLdouble)g_winW, (GLdouble)g_winH, 0.0, 0.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f((float)g_winW, 0.0f);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2f((float)g_winW, (float)g_winH);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, (float)g_winH);
        glEnd();
        glFinish();
        {
            UBYTE px[4];

            memset(px, 0xa5, sizeof(px));
            glReadPixels(g_winW / 4, g_winH / 4, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, px);
            ExpectRGB("menu.d-imm-ortho.bl", px,
                      want_vflip[0][0], want_vflip[0][1],
                      want_vflip[0][2]);
        }

        glDeleteTextures(1, &name);
menu_done:
        ;
    }

    /* ---- 21. continuous render/present loop (Q3's frame mode) -------
     * Q3 draws a full frame then presents, hundreds of times. Alternate
     * solid RED/BLUE fullscreen frames g_loop times; the hold afterwards
     * must show BLUE (the last frame). A black or stale window here
     * reproduces the Q3 black-screen mode with the same present path. */
    if (g_loop > 0) {
        static UBYTE redf[64 * 64 * 4], bluef[64 * 64 * 4];
        GLuint name;
        int i, f;

        for (i = 0; i < 64 * 64; ++i) {
            redf[i * 4 + 0] = 220; redf[i * 4 + 1] = 20;
            redf[i * 4 + 2] = 20;  redf[i * 4 + 3] = 255;
            bluef[i * 4 + 0] = 20; bluef[i * 4 + 1] = 30;
            bluef[i * 4 + 2] = 220; bluef[i * 4 + 3] = 255;
        }
        ResetSuiteState();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, redf);
        ExpectError("loop.upload", GL_NO_ERROR);
        for (f = 0; f < g_loop; ++f) {
            /* re-upload the frame colour each iteration: same cost
             * shape as Q3's per-frame texture work */
            glBindTexture(GL_TEXTURE_2D, name);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64,
                            GL_RGBA, GL_UNSIGNED_BYTE,
                            (f & 1) ? bluef : redf);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
            glTexCoord2f(1.0f, 0.0f); glVertex3f(1.0f, -1.0f, 0.0f);
            glTexCoord2f(1.0f, 1.0f); glVertex3f(1.0f, 1.0f, 0.0f);
            glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, 1.0f, 0.0f);
            glEnd();
            mglSwitchDisplay();
        }
        {
            char lb[48];
            sprintf(lb, "loop-done frames=%d", g_loop);
            LogLine(lb);
            printf("PROBE loop done frames=%d\n", g_loop);
        }
        /* leave BLUE in the buffer for the hold */
        glBindTexture(GL_TEXTURE_2D, name);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64,
                        GL_RGBA, GL_UNSIGNED_BYTE, bluef);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(1.0f, 1.0f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, 1.0f, 0.0f);
        glEnd();
        mglSwitchDisplay();
        glDeleteTextures(1, &name);
    }

    mglSwitchDisplay();
    if (g_buffers > 0 || g_sync >= 0 || g_lock || g_loop > 0 || g_drawbuf) {
        /* Hold the final present so a mid-hold screenshot can verify
         * what the window actually displays (2026-09-30 black-present
         * reproduction attempt). */
        printf("PROBE holding present buffers=%d sync=%d lock=%d "
               "loop=%d for 30s\n",
               g_buffers, g_sync, g_lock, g_loop);
        Delay(1800);
    }

cleanup:
    LogLine(failures ? "cleanup-FAIL" : "cleanup-PASS");
    if (live)
        mglDeleteContext();
    if (opened)
        MiniGLClose();
    printf("TEXTURE_FORMAT_PROBE %s CHECKS=%lu FAILURES=%lu\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 20 : 0;
}
