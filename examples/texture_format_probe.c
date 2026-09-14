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
#include <string.h>

#define PROBE_W 320
#define PROBE_H 240
#define PROBE_TOL 20

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
    UBYTE row[PROBE_W * 4];
    int i;

    memset(row, 0xa5, sizeof(row));
    glReadPixels(0, PROBE_H / 2, PROBE_W, 1,
                 GL_RGBA, GL_UNSIGNED_BYTE, row);
    for (i = 0; i < texW; ++i) {
        int x = (i * PROBE_W + PROBE_W / 2) / texW;
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
static void DumpCells(const char *id)
{
    char line[160];
    char *cursor = line;
    int x, y;
    UBYTE cell[4];

    cursor += sprintf(cursor, "PROBE id=%s cells=", id);
    for (y = 3; y >= 0; --y)
        for (x = 0; x < 4; ++x) {
            memset(cell, 0xa5, sizeof(cell));
            glReadPixels(x * PROBE_W / 4 + PROBE_W / 8,
                         y * PROBE_H / 4 + PROBE_H / 8, 1, 1,
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

int main(void)
{
    /* Biggest case below is a 4x1 RGBA readback scratch. */
    UBYTE scratch[16 * 4];
    GLuint name = 0;
    int opened = 0, live = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    puts("TEXTURE_FORMAT_PROBE BEGIN");

    if (MiniGLOpen() == 0) {
        puts("PROBE id=open result=FAIL MiniGLOpen failed");
        return 20;
    }
    opened = 1;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(32);
    if (mglCreateContext(0, 0, PROBE_W, PROBE_H) == 0) {
        puts("PROBE id=context result=FAIL mglCreateContext failed");
        goto cleanup;
    }
    live = 1;

    glViewport(0, 0, PROBE_W, PROBE_H);
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
        glReadPixels(PROBE_W / 8, PROBE_H / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage.outside", cell, 128, 128, 128);
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(3 * PROBE_W / 8, 3 * PROBE_H / 8, 1, 1,
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
        glReadPixels(PROBE_W / 8, PROBE_H / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage-rgba.outside", cell, 128, 128, 128);
        memset(cell, 0xa5, sizeof(cell));
        glReadPixels(3 * PROBE_W / 8, 3 * PROBE_H / 8, 1, 1,
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
        glReadPixels(3 * PROBE_W / 8, 3 * PROBE_H / 8, 1, 1,
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
        glReadPixels(3 * PROBE_W / 8, 3 * PROBE_H / 8, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, cell);
        ExpectRGB("subimage-align1.inside", cell, 255, 255, 255);
        glDeleteTextures(1, &name);
        name = 0;
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
