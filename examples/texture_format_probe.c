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

static ULONG checks, failures;

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
        else {
            printf("usage: %s [--width W] [--height H]\n", argv[0]);
            return 10;
        }
    }
    if (g_winW < 64) g_winW = 64;
    if (g_winH < 64) g_winH = 64;
    if (g_winW > PROBE_MAXW) g_winW = PROBE_MAXW;
    if (g_winH > PROBE_MAXH) g_winH = PROBE_MAXH;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("TEXTURE_FORMAT_PROBE BEGIN window=%dx%dx32\n",
           g_winW, g_winH);

    if (MiniGLOpen() == 0) {
        puts("PROBE id=open result=FAIL MiniGLOpen failed");
        return 20;
    }
    opened = 1;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(32);
    if (mglCreateContext(0, 0, g_winW, g_winH) == 0) {
        puts("PROBE id=context result=FAIL mglCreateContext failed");
        goto cleanup;
    }
    live = 1;

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

    mglSwitchDisplay();

cleanup:
    if (live)
        mglDeleteContext();
    if (opened)
        MiniGLClose();
    printf("TEXTURE_FORMAT_PROBE %s CHECKS=%lu FAILURES=%lu\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 20 : 0;
}
