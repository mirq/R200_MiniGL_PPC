/*
 * Hardware check for glCullFace/glFrontFace, glShadeModel and glDrawBuffer.
 *
 * Runs in a 320x240 16-bit window and reads the rendered surface back with
 * mglLockBack(). Every check is a comparison between sampled pixels (equal to
 * the cleared background, or equal to each other), so it does not depend on
 * the surface pixel layout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>


static MGLLockInfo lock;
static UWORD background;

/* Sample points, given as normalised device coordinates inside each shape. */
#define LEFT_X -0.5f
#define LEFT_Y -0.2f
#define RIGHT_X 0.5f
#define RIGHT_Y -0.2f
#define STRIP0_X -0.133f
#define STRIP0_Y -0.167f
#define STRIP1_X 0.133f
#define STRIP1_Y 0.167f

/* Two well-separated points on the same scanline inside the left triangle. */
#define SHADE_A_X -0.65f
#define SHADE_A_Y -0.5f
#define SHADE_B_X -0.35f
#define SHADE_B_Y -0.5f

static UWORD sample(GLfloat ndcx, GLfloat ndcy)
{
    UBYTE *base = (UBYTE *)lock.base_address;
    ULONG x = (ULONG)((ndcx + 1.0f) * 0.5f * (GLfloat)lock.width);
    ULONG y = (ULONG)((1.0f - ndcy) * 0.5f * (GLfloat)lock.height);

    if (x >= lock.width) x = lock.width - 1UL;
    if (y >= lock.height) y = lock.height - 1UL;
    return *(UWORD *)(base + y * lock.pitch + x * 2UL);
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

/* Counter-clockwise in GL window coordinates (Y up), left half of the view. */
static void left_triangle(void)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.6f);
    glVertex2f(-0.2f, -0.6f);
    glVertex2f(-0.5f, 0.6f);
    glEnd();
}

/* Same shape mirrored into the right half, wound clockwise. */
static void right_triangle(void)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(0.2f, -0.6f);
    glVertex2f(0.5f, 0.6f);
    glVertex2f(0.8f, -0.6f);
    glEnd();
}

/*
 * Oversized versions of the same two triangles: they run past the frustum, so
 * they take the clipping path in the primitive assembler rather than the
 * direct screen-space path.
 */
static void left_triangle_clipped(void)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(-1.6f, -1.8f);
    glVertex2f(-0.05f, -1.8f);
    glVertex2f(-0.5f, 1.8f);
    glEnd();
}

static void right_triangle_clipped(void)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(0.05f, -1.8f);
    glVertex2f(0.5f, 1.8f);
    glVertex2f(1.6f, -1.8f);
    glEnd();
}

/* Four-vertex strip; both triangles are clockwise in window coordinates. */
static void strip(void)
{
    glBegin(GL_TRIANGLE_STRIP);
    glVertex2f(-0.4f, -0.5f);
    glVertex2f(-0.4f, 0.5f);
    glVertex2f(0.4f, -0.5f);
    glVertex2f(0.4f, 0.5f);
    glEnd();
}

static int expect(int condition, const char *what, int code)
{
    if (condition)
        return 0;
    printf("cull_shade: FAILED %s (code %d)\n", what, code);
    return code;
}

static int check_pair(int drawLeft, int drawRight, const char *what,
                      int code)
{
    UWORD left, right;
    int result;

    if ((result = capture()))
        return code;
    left = sample(LEFT_X, LEFT_Y);
    right = sample(RIGHT_X, RIGHT_Y);
    if ((result = expect(drawLeft ? left != background : left == background,
                         what, code)))
        return result;
    return expect(drawRight ? right != background : right == background,
                  what, code + 1);
}

int main(int argc, char **argv)
{
    UWORD smoothA, smoothB, flatA, flatB, reference;
    GLboolean fullscreen = GL_FALSE;
    int argument;
    int width = 320;
    int height = 240;
    int depth = 16;
    APTR backAddress;
    GLint query;
    int result;

    for (argument = 1; argument < argc; ++argument) {
        if (!strcmp(argv[argument], "--fullscreen"))
            fullscreen = GL_TRUE;
        else if (!strcmp(argv[argument], "-width") && argument + 1 < argc)
            width = atoi(argv[++argument]);
        else if (!strcmp(argv[argument], "-height") && argument + 1 < argc)
            height = atoi(argv[++argument]);
        else if (!strcmp(argv[argument], "-depth") && argument + 1 < argc)
            depth = atoi(argv[++argument]);
        else
            return 10;
    }

    if (!MiniGLOpen()) return 21;
    mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
    mglChoosePixelDepth(depth);
    if (!mglCreateContext(0, 0, width, height)) { MiniGLClose(); return 22; }

    clear_scene();
    if (capture()) { mglDeleteContext(); MiniGLClose(); return 23; }
    if ((int)lock.depth != depth || !lock.base_address ||
        lock.width < 64 || lock.height < 64) {
        mglDeleteContext(); MiniGLClose(); return 24;
    }
    background = sample(LEFT_X, LEFT_Y);
    glColor3ub(255, 255, 255);

    /* Defaults: smooth shading, culling off, GL_BACK/GL_CCW, GL_BACK buffer. */
    glGetIntegerv(GL_SHADE_MODEL, &query);
    if ((result = expect(query == (GLint)GL_SMOOTH, "default shade model", 30)))
        goto done;
    glGetIntegerv(GL_CULL_FACE_MODE, &query);
    if ((result = expect(query == (GLint)GL_BACK, "default cull face", 31)))
        goto done;
    glGetIntegerv(GL_FRONT_FACE, &query);
    if ((result = expect(query == (GLint)GL_CCW, "default front face", 32)))
        goto done;
    glGetIntegerv(GL_DRAW_BUFFER, &query);
    if ((result = expect(query == (GLint)GL_BACK, "default draw buffer", 33)))
        goto done;
    if ((result = expect(!glIsEnabled(GL_CULL_FACE), "cull off by default",
                         34)))
        goto done;

    /* Culling disabled: both windings draw. */
    clear_scene(); left_triangle(); right_triangle();
    if ((result = check_pair(1, 1, "cull disabled", 40))) goto done;

    /* GL_BACK/GL_CCW: the clockwise triangle is back-facing. */
    glEnable(GL_CULL_FACE);
    if ((result = expect(glIsEnabled(GL_CULL_FACE), "cull enabled", 42)))
        goto done;
    clear_scene(); left_triangle(); right_triangle();
    if ((result = check_pair(1, 0, "cull back", 43))) goto done;

    /* Same, through the clipping path. */
    clear_scene(); left_triangle_clipped(); right_triangle_clipped();
    if ((result = check_pair(1, 0, "cull back clipped", 45))) goto done;

    /* Reversed winding convention swaps which triangle survives. */
    glFrontFace(GL_CW);
    clear_scene(); left_triangle(); right_triangle();
    if ((result = check_pair(0, 1, "front face cw", 47))) goto done;

    /* Culling front faces with the default winding does the same. */
    glFrontFace(GL_CCW);
    glCullFace(GL_FRONT);
    clear_scene(); left_triangle(); right_triangle();
    if ((result = check_pair(0, 1, "cull front", 49))) goto done;

    /* GL_FRONT_AND_BACK removes both. */
    glCullFace(GL_FRONT_AND_BACK);
    clear_scene(); left_triangle(); right_triangle();
    if ((result = check_pair(0, 0, "cull front and back", 51))) goto done;

    /*
     * Triangle strips: both triangles of the strip face the same way, so
     * culling must take all of it or none of it.
     */
    glCullFace(GL_BACK);
    clear_scene(); strip();
    if ((result = capture())) { result = 53; goto done; }
    if ((result = expect(sample(STRIP0_X, STRIP0_Y) == background &&
                         sample(STRIP1_X, STRIP1_Y) == background,
                         "strip culled", 54)))
        goto done;
    glFrontFace(GL_CW);
    clear_scene(); strip();
    if ((result = capture())) { result = 55; goto done; }
    if ((result = expect(sample(STRIP0_X, STRIP0_Y) != background &&
                         sample(STRIP1_X, STRIP1_Y) != background,
                         "strip drawn", 56)))
        goto done;
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);

    /* Smooth shading interpolates across the triangle. */
    clear_scene();
    glBegin(GL_TRIANGLES);
    glColor3ub(255, 0, 0); glVertex2f(-0.8f, -0.6f);
    glColor3ub(0, 255, 0); glVertex2f(-0.2f, -0.6f);
    glColor3ub(0, 0, 255); glVertex2f(-0.5f, 0.6f);
    glEnd();
    if ((result = capture())) { result = 60; goto done; }
    smoothA = sample(SHADE_A_X, SHADE_A_Y);
    smoothB = sample(SHADE_B_X, SHADE_B_Y);
    if ((result = expect(smoothA != smoothB, "smooth shading", 61)))
        goto done;

    /* Flat shading takes the provoking (third) vertex colour everywhere. */
    glShadeModel(GL_FLAT);
    glGetIntegerv(GL_SHADE_MODEL, &query);
    if ((result = expect(query == (GLint)GL_FLAT, "shade model query", 62)))
        goto done;
    clear_scene();
    glBegin(GL_TRIANGLES);
    glColor3ub(255, 0, 0); glVertex2f(-0.8f, -0.6f);
    glColor3ub(0, 255, 0); glVertex2f(-0.2f, -0.6f);
    glColor3ub(0, 0, 255); glVertex2f(-0.5f, 0.6f);
    glEnd();
    if ((result = capture())) { result = 63; goto done; }
    flatA = sample(SHADE_A_X, SHADE_A_Y);
    flatB = sample(SHADE_B_X, SHADE_B_Y);
    if ((result = expect(flatA == flatB, "flat shading uniform", 64)))
        goto done;

    /* The uniform colour must be the third vertex's blue, not red or green. */
    clear_scene();
    glColor3ub(0, 0, 255);
    left_triangle();
    if ((result = capture())) { result = 65; goto done; }
    reference = sample(SHADE_A_X, SHADE_A_Y);
    if ((result = expect(flatA == reference, "flat provoking vertex", 66)))
        goto done;
    glShadeModel(GL_SMOOTH);
    glColor3ub(255, 255, 255);

    /* glDrawBuffer(GL_NONE) discards clears and draws. */
    clear_scene();
    left_triangle();
    if ((result = capture())) { result = 70; goto done; }
    smoothA = sample(LEFT_X, LEFT_Y);
    glDrawBuffer(GL_NONE);
    glGetIntegerv(GL_DRAW_BUFFER, &query);
    if ((result = expect(query == (GLint)GL_NONE, "draw buffer query", 71)))
        goto done;
    glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    right_triangle();
    if ((result = capture())) { result = 72; goto done; }
    if ((result = expect(sample(LEFT_X, LEFT_Y) == smoothA &&
                         sample(RIGHT_X, RIGHT_Y) == background,
                         "draw buffer none", 73)))
        goto done;

    /* Restoring GL_BACK resumes rendering. */
    glDrawBuffer(GL_BACK);
    clear_scene();
    right_triangle();
    if ((result = capture())) { result = 75; goto done; }
    backAddress = lock.base_address;
    if ((result = expect(sample(RIGHT_X, RIGHT_Y) != background,
                         "draw buffer back", 76)))
        goto done;

    /*
     * GL_FRONT selects the displayed buffer. Fullscreen contexts are double
     * buffered, so it is a different surface that must not contain what was
     * just drawn to the back; a windowed context has only the one surface.
     */
    glDrawBuffer(GL_FRONT);
    if ((result = capture())) { result = 77; goto done; }
    if ((result = expect(fullscreen ? lock.base_address != backAddress
                                    : lock.base_address == backAddress,
                         "front buffer selection", 78)))
        goto done;

    /*
     * Clear the front buffer (it has never been drawn to in this run) and put
     * the left triangle on it. The back buffer must keep only its own right
     * triangle, and the front only the left one.
     */
    clear_scene();
    left_triangle();
    if ((result = capture())) { result = 90; goto done; }
    if ((result = expect(sample(LEFT_X, LEFT_Y) != background &&
                         (!fullscreen ||
                          sample(RIGHT_X, RIGHT_Y) == background),
                         "draw buffer front", 91)))
        goto done;

    glDrawBuffer(GL_BACK);
    if ((result = capture())) { result = 92; goto done; }
    if (fullscreen &&
        (result = expect(lock.base_address == backAddress &&
                         sample(RIGHT_X, RIGHT_Y) != background &&
                         sample(LEFT_X, LEFT_Y) == background,
                         "back buffer preserved", 93)))
        goto done;

    if ((result = expect(glGetError() == GL_NO_ERROR, "no error so far", 80)))
        goto done;

    /* Rejected arguments. */
    glShadeModel(GL_TRIANGLES);
    if ((result = expect(glGetError() == GL_INVALID_ENUM,
                         "glShadeModel enum", 81)))
        goto done;
    glCullFace(GL_CCW);
    if ((result = expect(glGetError() == GL_INVALID_ENUM,
                         "glCullFace enum", 82)))
        goto done;
    glFrontFace(GL_FRONT);
    if ((result = expect(glGetError() == GL_INVALID_ENUM,
                         "glFrontFace enum", 83)))
        goto done;
    glDrawBuffer(GL_FLAT);
    if ((result = expect(glGetError() == GL_INVALID_ENUM,
                         "glDrawBuffer enum", 84)))
        goto done;

    /* A rejected call must leave the state alone. */
    glGetIntegerv(GL_CULL_FACE_MODE, &query);
    if ((result = expect(query == (GLint)GL_BACK, "cull face unchanged", 85)))
        goto done;
    glGetIntegerv(GL_DRAW_BUFFER, &query);
    if ((result = expect(query == (GLint)GL_BACK, "draw buffer unchanged",
                         86)))
        goto done;

    result = 0;
    printf("MINIGL_CULL_SHADE mode=%s depth=16 size=%lux%lu "
           "cull=pass frontface=pass strip=pass flat=pass drawbuffer=pass "
           "enums=pass\n", fullscreen ? "fullscreen" : "window",
           lock.width, lock.height);

done:
    mglDeleteContext();
    MiniGLClose();
    return result;
}
