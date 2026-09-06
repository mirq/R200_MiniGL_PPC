/*
 * Visual matrix probe: renders every suspect effect ONCE into labelled
 * regions of a single frame, presents it, holds it on screen, then takes
 * pixel samples. Run alongside amiga_screenshot() so what the compositor
 * shows can be compared with what mglLockBack() reads.
 *
 * Regions (windowed 320x240):
 *   Q1 top-left     : textured MODULATE fill, texel alpha 128, SRC_ALPHA blend
 *   Q2 top-right    : untextured fill, diffuse alpha 0.5, SRC_ALPHA blend
 *   Q3 mid-left     : smooth-shaded fan (bright centre -> dark rim)
 *   Q4 mid-right    : additive GL_ONE/GL_ONE gray box, two stacked layers
 *   Q5 bottom-left  : three points, alpha 255 / 200 / 64 (alpharef 0.5)
 *   Q6 bottom-right : opaque white quad + green quad behind it (depth mask
 *                     off, depth test on) then same in front
 */
#include <stdio.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static MGLLockInfo lock;

static UWORD sample_at(ULONG x, ULONG y)
{
    UBYTE *base = (UBYTE *)lock.base_address;
    return *(UWORD *)(base + y * lock.pitch + x * 2UL);
}

static void rect_dev(GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1)
{
    glBegin(MGL_FLATFAN);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

static void rect_ndc(GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

static void delay_ticks(ULONG ticks)
{
    struct { ULONG seconds, micro; } tv;
    struct MsgPort dummy;
    (void)dummy;
    /* busy-wait via timer-less loop; ticks ~ 1/50s each */
    while (ticks--) {
        volatile ULONG spin = 20000UL;
        while (spin--) ;
    }
    (void)tv;
}

int main(void)
{
    GLubyte texels[4] = {255, 255, 255, 128};
    int i;

    if (!MiniGLOpen()) return 21;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240)) { MiniGLClose(); return 22; }
    glViewport(0, 0, 320, 240);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);

    /* Warm-up frame: defeats the known first-submission swallow. */
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor4f(1, 1, 1, 1);
    rect_dev(0, 0, 8, 8);
    glFlush();

    /* ---- the real frame ---- */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Q1 textured half-alpha fill (top-left quadrant). */
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0, 0); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1, 0); glVertex2f(160.0f, 0.0f);
    glTexCoord2f(1, 1); glVertex2f(160.0f, 120.0f);
    glTexCoord2f(0, 1); glVertex2f(0.0f, 120.0f);
    glEnd();

    /* Q2 diffuse-alpha fill (top-right quadrant). */
    glDisable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 0.5f);
    rect_dev(160.0f, 0.0f, 320.0f, 120.0f);

    /* Q3 smooth fan (mid-left). */
    glDisable(GL_BLEND);
    glColor4f(1, 1, 1, 1);
    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1, 1, 1); glVertex2f(-0.55f, -0.05f);
    glColor3f(0, 0, 0); glVertex2f(-0.25f, -0.05f);
    glColor3f(0, 0, 0); glVertex2f(-0.40f, 0.20f);
    glColor3f(0, 0, 0); glVertex2f(-0.70f, 0.20f);
    glColor3f(0, 0, 0); glVertex2f(-0.85f, -0.05f);
    glColor3f(0, 0, 0); glVertex2f(-0.55f, -0.05f);
    glEnd();
    glShadeModel(GL_FLAT);

    /* Q4 additive gray box x2 (mid-right). */
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glColor4f(0.25f, 0.25f, 0.25f, 1);
    rect_ndc(0.10f, -0.30f, 0.85f, 0.20f);
    rect_ndc(0.10f, -0.30f, 0.85f, 0.20f);
    glDisable(GL_BLEND);

    /* Q5 points row (bottom strip). */
    glPointSize(4.0f);
    glBegin(GL_POINTS);
    glColor4ub(255, 255, 255, 255); glVertex2f(-0.7f, -0.7f);
    glColor4ub(255, 255, 255, 200); glVertex2f( 0.0f, -0.7f);
    glColor4ub(255, 255, 255, 64);  glVertex2f(-0.35f, -0.85f);
    glEnd();

    /* Q6 depth-masked blending (bottom-right). */
    glEnable(GL_DEPTH_TEST);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_TRIANGLES);
    glVertex3f(0.15f, -0.9f, -0.5f);
    glVertex3f(0.95f, -0.9f, -0.5f);
    glVertex3f(0.55f, -0.35f, -0.5f);
    glEnd();
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0, 1, 0, 0.75f);
    glBegin(GL_TRIANGLES);
    glVertex3f(0.15f, -0.65f, 0.5f);
    glVertex3f(0.95f, -0.65f, 0.5f);
    glVertex3f(0.55f, -0.55f, 0.5f);
    glEnd();
    glColor4f(0, 0, 1, 0.75f);
    glBegin(GL_TRIANGLES);
    glVertex3f(0.15f, -0.45f, -0.9f);
    glVertex3f(0.95f, -0.45f, -0.9f);
    glVertex3f(0.55f, -0.38f, -0.9f);
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glColor4f(1, 1, 1, 1);
    glFinish();

    /* Give a slow/deferred submission a full 20 seconds before reading. */
    {
        ULONG settle;
        for (settle = 0; settle < 1000UL; ++settle)
            delay_ticks(1);
    }

    /* Dump the real surface contents, then sample through LockBack. */
    mglWriteShotPPM("T:vp.ppm");
    if (mglLockBack(&lock)) {
        ULONG w = lock.width, h = lock.height;
        printf("visualprobe: %ldx%ld\n", (long)w, (long)h);
        printf("Q1 texhalf=%04x\n", sample_at(w * 3 / 8, h / 4));
        printf("Q2 diffuse=%04x\n", sample_at(w * 5 / 8, h / 4));
        printf("Q3 fanc=%04x rim=%04x\n",
               sample_at(w / 2 - w * 3 / 20, h / 2 - h / 12),
               sample_at(w / 2 - w * 3 / 80, h / 2 + h / 14));
        printf("Q4 add1=%04x add2=%04x\n",
               sample_at(w * 5 / 8, h * 7 / 12),
               sample_at(w * 11 / 16, h * 7 / 12));
        printf("Q5 pA=%04x pB=%04x pC=%04x\n",
               sample_at(w / 8, h * 7 / 8),
               sample_at(w / 2, h * 7 / 8),
               sample_at(w * 3 / 8, h * 29 / 32));
        printf("Q6 hidden=%04x front=%04x\n",
               sample_at(w * 11 / 16, h * 13 / 16),
               sample_at(w * 11 / 16, h * 43 / 64));
    } else {
        printf("visualprobe: LOCK FAILED\n");
    }

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
