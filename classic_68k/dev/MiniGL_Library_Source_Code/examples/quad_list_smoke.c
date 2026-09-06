#include <stdio.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <libraries/minigl_perf.h>
#include <radeon3d.h>

static MGLLockInfo lock;

static UWORD sample(GLfloat ndcx, GLfloat ndcy)
{
    UBYTE *base = (UBYTE *)lock.base_address;
    ULONG x = (ULONG)((ndcx + 1.0f) * 0.5f * (GLfloat)lock.width);
    ULONG y = (ULONG)((1.0f - ndcy) * 0.5f * (GLfloat)lock.height);

    if (x >= lock.width) x = lock.width - 1UL;
    if (y >= lock.height) y = lock.height - 1UL;
    return *(UWORD *)(base + y * lock.pitch + x * 2UL);
}

static void color(GLuint corner)
{
    static const GLubyte colors[4][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}
    };
    glColor3ub(colors[corner][0], colors[corner][1], colors[corner][2]);
}

static void colored_strip(GLfloat left, GLfloat right)
{
    glBegin(GL_TRIANGLE_STRIP);
    color(0); glVertex2f(left, -0.7f);
    color(1); glVertex2f(left, 0.7f);
    color(2); glVertex2f(right, -0.7f);
    color(3); glVertex2f(right, 0.7f);
    glEnd();
}

static void colored_reference(GLfloat left, GLfloat right)
{
    glBegin(GL_TRIANGLES);
    color(0); glVertex2f(left, -0.7f);
    color(1); glVertex2f(left, 0.7f);
    color(2); glVertex2f(right, -0.7f);
    color(2); glVertex2f(right, -0.7f);
    color(1); glVertex2f(left, 0.7f);
    color(3); glVertex2f(right, 0.7f);
    glEnd();
}

static void colored_quad(GLfloat left, GLfloat right)
{
    glBegin(GL_QUADS);
    color(0); glVertex2f(left, -0.7f);
    color(1); glVertex2f(right, -0.7f);
    color(2); glVertex2f(right, 0.7f);
    color(3); glVertex2f(left, 0.7f);
    glEnd();
}

static void colored_quad_reference(GLfloat left, GLfloat right)
{
    glBegin(GL_TRIANGLES);
    color(0); glVertex2f(left, -0.7f);
    color(1); glVertex2f(right, -0.7f);
    color(2); glVertex2f(right, 0.7f);
    color(0); glVertex2f(left, -0.7f);
    color(2); glVertex2f(right, 0.7f);
    color(3); glVertex2f(left, 0.7f);
    glEnd();
}

int main(void)
{
    GLint caps, quadAttempts, quadUsed, drawRecords;
    GLuint texture;
    static const UBYTE whiteTexture[16] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255
    };
    ULONG index;
    UWORD smallTexturedPixel = 0, largeTexturedPixel = 0;
    BOOL smallLock = FALSE, largeLock = FALSE;
    int result = 0;
    GLenum drawError = GL_NO_ERROR;

    if (!MiniGLOpen()) return 20;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240)) {
        MiniGLClose();
        return 21;
    }
    glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    colored_strip(-0.8f, -0.1f);
    colored_reference(0.1f, 0.8f);
    glFlush();
    if (!mglLockBack(&lock)) {
        result = 23;
        goto done;
    }
    if (sample(-0.625f, -0.35f) != sample(0.275f, -0.35f) ||
        sample(-0.275f, 0.35f) != sample(0.625f, 0.35f) ||
        sample(-0.45f, 0.0f) != sample(0.45f, 0.0f)) {
        result = 24;
        goto done;
    }

    glClear(GL_COLOR_BUFFER_BIT);
    colored_quad(-0.8f, -0.1f);
    colored_quad_reference(0.1f, 0.8f);
    glColor3ub(255, 255, 255);
    for (index = 0; index < 64UL; ++index) {
        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(-0.98f, -0.98f); glVertex2f(-0.98f, -0.94f);
        glVertex2f(-0.94f, -0.98f); glVertex2f(-0.94f, -0.94f);
        glEnd();
    }
    glFlush();

    glGetIntegerv(MGL_PERF_RADEON_CAPS, &caps);
    glGetIntegerv(MGL_PERF_QUAD_ATTEMPTS, &quadAttempts);
    glGetIntegerv(MGL_PERF_QUAD_USED, &quadUsed);
    glGetIntegerv(MGL_PERF_DRAW_RECORDS, &drawRecords);
    if (!(caps & RADEON3D_CAP_NATIVE_QUAD_LISTS) ||
        quadAttempts != 66 || quadUsed != 66 || drawRecords != 6) {
        result = 22;
        goto done;
    }
    if (!mglLockBack(&lock)) {
        result = 25;
        goto done;
    }
    if (sample(-0.625f, -0.35f) != sample(0.275f, -0.35f) ||
        sample(-0.275f, 0.35f) != sample(0.625f, 0.35f) ||
        sample(-0.45f, 0.0f) != sample(0.45f, 0.0f))
        result = 26;
    if (result)
        goto done;

    /* A VBUF record can exceed the old immediate-packet vertex limit.
     * Quake routinely merges textured triangle lists this large. */
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, whiteTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_TEXTURE_2D);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(255, 255, 255);
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0, 0); glVertex2f(-0.5f, -0.5f);
    glTexCoord2f(1, 0); glVertex2f(0.5f, -0.5f);
    glTexCoord2f(0.5f, 1); glVertex2f(0.0f, 0.5f);
    glEnd();
    glFlush();
    smallLock = mglLockBack(&lock);
    if (smallLock)
        smallTexturedPixel = sample(0.0f, 0.0f);
    if (!smallLock || smallTexturedPixel == 0) {
        result = 27;
        goto texture_done;
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    for (index = 0; index < 100UL; ++index) {
        glTexCoord2f(0, 0); glVertex2f(-0.5f, -0.5f);
        glTexCoord2f(1, 0); glVertex2f(0.5f, -0.5f);
        glTexCoord2f(0.5f, 1); glVertex2f(0.0f, 0.5f);
    }
    glEnd();
    glFlush();
    largeLock = mglLockBack(&lock);
    if (largeLock)
        largeTexturedPixel = sample(0.0f, 0.0f);
    if (!largeLock || largeTexturedPixel == 0)
        result = 28;
texture_done:
    /* Sample before the delete so a teardown error cannot masquerade as a
     * failure of the draw under test. */
    drawError = glGetError();
    glDeleteTextures(1, &texture);
done:
    printf("R200_QUAD_LIST status=%s caps=%08lx attempts=%ld used=%ld "
           "draw_records=%ld small_lock=%ld small=%04lx large_lock=%ld "
           "large=%04lx error=%lu result=%d\n",
           result ? "fail" : "ok", (ULONG)caps, (LONG)quadAttempts,
           (LONG)quadUsed, (LONG)drawRecords, (LONG)smallLock,
           (ULONG)smallTexturedPixel, (LONG)largeLock,
           (ULONG)largeTexturedPixel, (ULONG)drawError, result);
    mglDeleteContext();
    MiniGLClose();
    return result;
}
