/*
 * Hardware check for the Phase E library-only functions: glPolygonMode,
 * glPolygonOffset, glColorMask, glColorTable/GL_COLOR_INDEX.
 *
 * Runs in a 320x240 16-bit window and reads the rendered surface back with
 * mglLockBack(), following the same readback-comparison pattern as
 * cull_shade_smoke.c.
 */
#include <stdio.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static MGLLockInfo lock;
static UWORD background;

#define CENTER_X 0.0f
#define CENTER_Y 0.0f
#define EDGE_X -0.4f
#define EDGE_Y 0.0f
#define VERTEX_X -0.8f
#define VERTEX_Y -0.6f

static UWORD sample(GLfloat ndcx, GLfloat ndcy)
{
    UBYTE *base = (UBYTE *)lock.base_address;
    ULONG x = (ULONG)((ndcx + 1.0f) * 0.5f * (GLfloat)lock.width);
    ULONG y = (ULONG)((1.0f - ndcy) * 0.5f * (GLfloat)lock.height);

    if (x >= lock.width) x = lock.width - 1UL;
    if (y >= lock.height) y = lock.height - 1UL;
    return *(UWORD *)(base + y * lock.pitch + x * 2UL);
}

static int nonbackground_near(GLfloat ndcx, GLfloat ndcy, ULONG radius)
{
    UBYTE *base = (UBYTE *)lock.base_address;
    LONG x = (LONG)((ndcx + 1.0f) * 0.5f * (GLfloat)lock.width);
    LONG y = (LONG)((1.0f - ndcy) * 0.5f * (GLfloat)lock.height);
    LONG dx, dy;

    for (dy = -(LONG)radius; dy <= (LONG)radius; ++dy)
        for (dx = -(LONG)radius; dx <= (LONG)radius; ++dx) {
            LONG sx = x + dx, sy = y + dy;
            if (sx >= 0 && sy >= 0 && sx < (LONG)lock.width &&
                sy < (LONG)lock.height &&
                *(UWORD *)(base + sy * lock.pitch + sx * 2L) != background)
                return 1;
        }
    return 0;
}

static int capture(void)
{
    glFlush();
    return mglLockBack(&lock) ? 0 : 1;
}

static void clear_scene(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* CENTER is well inside; EDGE is the midpoint of the left sloped edge. */
static void big_triangle(void)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.6f);
    glVertex2f(0.8f, -0.6f);
    glVertex2f(0.0f, 0.6f);
    glEnd();
}

static int expect(int condition, const char *what, int code)
{
    if (condition)
        return 0;
    printf("phaseE: FAILED %s (code %d)\n", what, code);
    return code;
}

int main(int argc, char **argv)
{
    GLboolean fullscreen = argc == 2 && !strcmp(argv[1], "--fullscreen");
    int width = fullscreen ? 640 : 320;
    int height = fullscreen ? 480 : 240;
    UBYTE palette[2 * 4];
    UBYTE indexImage[8 * 8];
    int i;
    UWORD centerPixel, edgePixel;
    int result;

    if (!MiniGLOpen()) return 21;
    mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, width, height)) { MiniGLClose(); return 22; }

    clear_scene();
    if (capture()) { mglDeleteContext(); MiniGLClose(); return 23; }
    background = sample(CENTER_X, CENTER_Y);
    glColor3ub(255, 255, 255);

    /* --- glPolygonMode: GL_FILL is the default; solid center pixel. --- */
    clear_scene(); big_triangle();
    if ((result = capture())) { result = 30; goto done; }
    if ((result = expect(sample(CENTER_X, CENTER_Y) != background,
                         "default fill center", 31)))
        goto done;

    /* --- GL_LINE: center empty, edge and vertex are drawn. --- */
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if ((result = expect(glGetError() == GL_NO_ERROR,
                         "line polygon mode accepted", 29)))
        goto done;
    clear_scene(); big_triangle();
    if ((result = capture())) { result = 32; goto done; }
    centerPixel = sample(CENTER_X, CENTER_Y);
    if ((result = expect(centerPixel == background, "line mode hollow center",
                         33)))
        goto done;
    if ((result = expect(nonbackground_near(EDGE_X, EDGE_Y, 3),
                         "line mode edge drawn",
                         34)))
        goto done;
    if ((result = expect(nonbackground_near(VERTEX_X, VERTEX_Y, 3),
                         "line mode vertex drawn",
                         44)))
        goto done;

    /* --- GL_POINT: center and edge midpoint empty, vertex is drawn. --- */
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    glPointSize(5.0f);
    clear_scene(); big_triangle();
    if ((result = capture())) { result = 35; goto done; }
    if ((result = expect(sample(CENTER_X, CENTER_Y) == background,
                         "point mode hollow center", 36)))
        goto done;
    if ((result = expect(sample(EDGE_X, EDGE_Y) == background,
                         "point mode hollow edge", 37)))
        goto done;
    if ((result = expect(nonbackground_near(VERTEX_X, VERTEX_Y, 3),
                          "point mode vertex drawn", 38)))
        goto done;
    glPointSize(1.0f);

    /* --- Restoring GL_FILL resumes normal rendering. --- */
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    clear_scene(); big_triangle();
    if ((result = capture())) { result = 39; goto done; }
    if ((result = expect(sample(CENTER_X, CENTER_Y) != background,
                         "fill mode restored", 40)))
        goto done;

    /* Rejected mode leaves state alone: still GL_FILL, center still solid. */
    glPolygonMode(GL_FRONT_AND_BACK, GL_QUADS);
    if ((result = expect(glGetError() == GL_INVALID_ENUM,
                         "polygon mode enum", 41)))
        goto done;
    clear_scene(); big_triangle();
    if ((result = capture())) { result = 42; goto done; }
    if ((result = expect(sample(CENTER_X, CENTER_Y) != background,
                         "fill mode unchanged after bad call", 43)))
        goto done;

    /* --- glColorMask: identity is a no-op, an actual mask is refused. --- */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if ((result = expect(glGetError() == GL_NO_ERROR,
                         "color mask identity accepted", 50)))
        goto done;
    glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
    if ((result = expect(glGetError() == GL_INVALID_OPERATION,
                         "color mask refuses real mask", 51)))
        goto done;

    /* --- glPolygonOffset: negative units bias a fragment nearer the eye,
     * winning an otherwise-tied GL_LESS depth test; disabled, it must not. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor3ub(0, 255, 0);
    big_triangle();
    glPolygonOffset(0.0f, -50.0f);
    glEnable(GL_POLYGON_OFFSET_FILL);
    if ((result = expect(glIsEnabled(GL_POLYGON_OFFSET_FILL),
                         "polygon offset enabled query", 60)))
        goto done;
    glColor3ub(255, 0, 0);
    big_triangle();
    glDisable(GL_POLYGON_OFFSET_FILL);
    if ((result = capture())) { result = 61; goto done; }
    if ((result = expect(sample(CENTER_X, CENTER_Y) != background,
                         "offset winner drawn", 62)))
        goto done;
    /* Won't assert the exact RGB565 value (unknown packing here); just that
     * the second (offset, red) draw overwrote the first (green) pixel. */
    edgePixel = sample(CENTER_X, CENTER_Y);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor3ub(0, 255, 0);
    big_triangle();
    glColor3ub(255, 0, 0);
    big_triangle(); /* same Z, offset disabled: must NOT overwrite */
    if ((result = capture())) { result = 63; goto done; }
    centerPixel = sample(CENTER_X, CENTER_Y);
    if ((result = expect(centerPixel != edgePixel,
                         "offset disabled does not overwrite equal Z",
                         64)))
        goto done;
    glDisable(GL_DEPTH_TEST);
    glColor3ub(255, 255, 255);

    /* --- glColorTable + GL_COLOR_INDEX texture. --- */
    palette[0] = 255; palette[1] = 0; palette[2] = 0; palette[3] = 255;
    palette[4] = 0; palette[5] = 255; palette[6] = 0; palette[7] = 255;
    glColorTable(GL_COLOR_TABLE, GL_RGBA, 2, GL_RGBA, GL_UNSIGNED_BYTE,
                palette);
    if ((result = expect(glGetError() == GL_NO_ERROR, "color table load",
                         70)))
        goto done;

    {
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        for (i = 0; i < 8 * 8; ++i) indexImage[i] = 0;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX, 8, 8, 0,
                    GL_COLOR_INDEX, GL_UNSIGNED_BYTE, indexImage);
        if ((result = expect(glGetError() == GL_NO_ERROR,
                             "paletted texture upload index0", 71)))
            goto done;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        clear_scene();
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(-0.8f, -0.8f);
        glTexCoord2f(1, 0); glVertex2f(0.8f, -0.8f);
        glTexCoord2f(1, 1); glVertex2f(0.8f, 0.8f);
        glTexCoord2f(0, 1); glVertex2f(-0.8f, 0.8f);
        glEnd();
        if ((result = capture())) { result = 72; goto done; }
        centerPixel = sample(CENTER_X, CENTER_Y);

        for (i = 0; i < 8 * 8; ++i) indexImage[i] = 1;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX, 8, 8, 0,
                    GL_COLOR_INDEX, GL_UNSIGNED_BYTE, indexImage);
        clear_scene();
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(-0.8f, -0.8f);
        glTexCoord2f(1, 0); glVertex2f(0.8f, -0.8f);
        glTexCoord2f(1, 1); glVertex2f(0.8f, 0.8f);
        glTexCoord2f(0, 1); glVertex2f(-0.8f, 0.8f);
        glEnd();
        if ((result = capture())) { result = 73; goto done; }
        edgePixel = sample(CENTER_X, CENTER_Y);
        if ((result = expect(centerPixel != edgePixel,
                             "index0 vs index1 differ", 74)))
            goto done;
        if ((result = expect(centerPixel != background &&
                             edgePixel != background,
                             "paletted texture drawn", 75)))
            goto done;
        glDisable(GL_TEXTURE_2D);
        glDeleteTextures(1, &texture);
    }

    result = 0;
    printf("MINIGL_PHASEE mode=%s depth=16 size=%lux%lu "
          "polygonmode=pass colormask=pass polygonoffset=pass "
          "colortable=pass\n",
          fullscreen ? "fullscreen" : "window", lock.width, lock.height);

done:
    mglDeleteContext();
    MiniGLClose();
    return result;
}
