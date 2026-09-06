/*
 * TCL-only bisection: same effects as bisectprobe but WITHOUT the two
 * screen-space FLATFAN fills, so the batch contains only hardware-TCL
 * records. If everything renders here, the record-stream corruption is
 * specific to mixing FLAT and TCL records in one submit.
 */
#include <stdio.h>
#include <string.h>
#include <libraries/minigl_perf.h>
#include <proto/dos.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>


static void drawFan(GLfloat cx)
{
    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1, 1, 1); glVertex2f(cx, -0.05f);
    glColor3f(0, 0, 0); glVertex2f(cx + 0.30f, -0.05f);
    glColor3f(0, 0, 0); glVertex2f(cx + 0.15f, 0.25f);
    glColor3f(0, 0, 0); glVertex2f(cx - 0.15f, 0.25f);
    glColor3f(0, 0, 0); glVertex2f(cx - 0.30f, -0.05f);
    glColor3f(0, 0, 0); glVertex2f(cx, -0.05f);
    glEnd();
    glShadeModel(GL_FLAT);
}

static void drawTris(GLfloat dy)
{
    glColor4f(0.3f, 0.3f, 0.3f, 1);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.6f + dy);
    glVertex2f(-0.2f, -0.6f + dy);
    glVertex2f(-0.5f, -0.1f + dy);
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

/* Raw back-buffer sample at NDC coords (windowed 320x240).
 * Surface stores little-endian R5G6B5PC words; LockBack hands them over
 * big-endian, so printed values need a byte swap to decode. */
static MGLLockInfo li;
static int locked;

static UWORD sampleNdc(GLfloat nx, GLfloat ny)
{
    const UBYTE *base;
    UWORD v;
    int px = (int)(((nx + 1.0f) * 0.5f) * 320.0f);
    int py = (int)(((1.0f - ny) * 0.5f) * 240.0f);
    if (!locked) { glFlush(); if (!mglLockBack(&li)) return 0xbeef; locked = 1; }
    base = (const UBYTE *)li.base_address;
    if (px < 0) px = 0; if (px > 319) px = 319;
    if (py < 0) py = 0; if (py > 239) py = 239;
    v = *(const UWORD *)(base + (ULONG)py * li.pitch + (ULONG)px * 2UL);
    return v;
}

int main(int argc, char **argv)
{
    int frame;
    int width = 320, height = 240;

    if (!MiniGLOpen()) return 21;
    if (argc == 2 && !strcmp(argv[1], "--fullscreen")) {
        mglChooseWindowMode(GL_FALSE);
        mglChoosePixelDepth(16);
        width = 640;
        height = 480;
    } else {
        mglChooseWindowMode(GL_TRUE);
        mglChoosePixelDepth(16);
    }
    if (!mglCreateContext(0, 0, width, height)) { MiniGLClose(); return 22; }
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
    glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFinish();

    /* Matrix: which record sequences survive one submit?
     * f0 fan+fan   identical state (shade SMOOTH both)
     * f1 fan+tris  state change (shade model + colour)
     * f2 tris+fan  reversed
     * f3 tris+tris identical state, different position
     * f4 fan+fan   second changes blend to additive
     * f5 tris+tris second changes point size (control dword) */
    for (frame = 0; frame < 6; ++frame) {
        int step;
        UWORD sA, sB;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (frame == 0) {
            drawFan(-0.55f); drawFan(0.45f);
            glFinish();
            sA = sampleNdc(-0.55f, -0.05f); sB = sampleNdc(0.45f, -0.05f);
            printf("tcl f0 fanA=%04x fanB=%04x (ffff=rendered)\n", sA, sB);
        mglWriteShotPPM("T:tc1.ppm");
        } else if (frame == 1) {
            drawFan(-0.55f); drawTris(-0.2f);
            glFinish();
            sA = sampleNdc(-0.55f, -0.05f); sB = sampleNdc(-0.5f, -0.43f - 0.2f);
            printf("tcl f1 fanA=%04x triB=%04x (tri expect 694a)\n", sA, sB);
        mglWriteShotPPM("T:tc2.ppm");
        } else if (frame == 2) {
            drawTris(-0.2f); drawFan(-0.55f);
            glFinish();
            sA = sampleNdc(-0.5f, -0.43f - 0.2f); sB = sampleNdc(-0.55f, -0.05f);
            printf("tcl f2 triA=%04x fanB=%04x\n", sA, sB);
        mglWriteShotPPM("T:tc3.ppm");
        } else if (frame == 3) {
            drawTris(-0.2f); drawTris(0.3f);
            glFinish();
            sA = sampleNdc(-0.5f, -0.43f - 0.2f); sB = sampleNdc(-0.5f, -0.43f + 0.3f);
            printf("tcl f3 triA=%04x triB=%04x\n", sA, sB);
        mglWriteShotPPM("T:tc4.ppm");
        } else if (frame == 4) {
            drawFan(-0.55f);
            glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
            glColor4f(0.5f, 0.5f, 0.5f, 1);
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(0.45f, -0.05f);
            glVertex2f(0.75f, -0.05f);
            glVertex2f(0.60f, 0.25f);
            glVertex2f(0.30f, 0.25f);
            glVertex2f(0.15f, -0.05f);
            glEnd();
            glDisable(GL_BLEND);
            glFinish();
            sA = sampleNdc(-0.55f, -0.05f); sB = sampleNdc(0.45f, -0.05f);
            printf("tcl f4 fanA=%04x addB=%04x (add ~8080 over black)\n", sA, sB);
        mglWriteShotPPM("T:tc5.ppm");
        } else {
            drawTris(-0.2f);
            glPointSize(4.0f);
            glColor4f(1, 1, 1, 1);
            glBegin(GL_POINTS);
            glVertex2f(0.5f, 0.0f);
            glEnd();
            glPointSize(1.0f);
            glFinish();
            sA = sampleNdc(-0.5f, -0.43f - 0.2f); sB = sampleNdc(0.5f, 0.0f);
            printf("tcl f5 triA=%04x pointB=%04x (point expect ffff)\n", sA, sB);
        mglWriteShotPPM("T:tc6.ppm");
        }
        glColor4f(1, 1, 1, 1);
        glFinish();
        {
            GLint records;
            glGetIntegerv(MGL_PERF_DRAW_RECORDS, &records);
            printf("tcl: frame=%d err=%lx records=%ld\n", frame,
                   (unsigned long)glGetError(), (long)records);
        }
        for (step = 0; step < 100UL; ++step)
            Delay(1);
    }

    /* f6: a LONE triangles record in its own submit */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawTris(-0.2f);
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f6 lonetris=%04x err=%lx\n",
           sampleNdc(-0.5f, -0.63f), (unsigned long)glGetError());

    /* f7: triangles, explicit submit boundary, then a fan */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawTris(-0.2f);
    glColor4f(1, 1, 1, 1);
    glFinish();
    drawFan(-0.55f);
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f7 tri=%04x fan=%04x\n",
           sampleNdc(-0.5f, -0.63f), sampleNdc(-0.55f, -0.05f));

    /* f8-f12: which opcodes survive? identical flat-gray state each time. */
    glColor4f(0.3f, 0.3f, 0.3f, 1);

    /* f8: one quad via glBegin(GL_QUADS) covering ndc x .1...7 y -.4...2 */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_QUADS);
    glVertex2f(0.1f, -0.4f); glVertex2f(0.7f, -0.4f);
    glVertex2f(0.7f, 0.2f); glVertex2f(0.1f, 0.2f);
    glEnd();
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f8 quad=%04x (expect 694a)\n", sampleNdc(0.4f, -0.1f));
        mglWriteShotPPM("T:tc7.ppm");

    /* f9: triangle strip, 4 vertices, same region */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLE_STRIP);
    glVertex2f(-0.7f, -0.4f); glVertex2f(-0.1f, -0.4f);
    glVertex2f(-0.7f, 0.2f); glVertex2f(-0.1f, 0.2f);
    glEnd();
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f9 strip=%04x (expect 694a)\n", sampleNdc(-0.4f, -0.1f));
        mglWriteShotPPM("T:tc8.ppm");

    /* f10: GL_POLYGON (convex pentagon, left side) */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_POLYGON);
    glVertex2f(-0.9f, -0.5f); glVertex2f(-0.3f, -0.5f);
    glVertex2f(-0.1f, 0.0f); glVertex2f(-0.6f, 0.4f);
    glVertex2f(-0.95f, 0.1f);
    glEnd();
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f10 polygon=%04x (expect 694a)\n", sampleNdc(-0.55f, -0.05f));
        mglWriteShotPPM("T:tc9.ppm");

    /* f11: six-vertex triangle list (two triangles, both visible regions) */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.6f); glVertex2f(-0.2f, -0.6f);
    glVertex2f(-0.5f, -0.1f);
    glVertex2f(0.2f, -0.6f); glVertex2f(0.8f, -0.6f);
    glVertex2f(0.5f, -0.1f);
    glEnd();
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f11 tri6a=%04x tri6b=%04x (expect 694a)\n",
           sampleNdc(-0.5f, -0.43f), sampleNdc(0.5f, -0.43f));

    /* f12: points, three white dots along y=-0.2 */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPointSize(4.0f);
    glBegin(GL_POINTS);
    glVertex2f(-0.5f, -0.2f); glVertex2f(0.0f, -0.2f);
    glVertex2f(0.5f, -0.2f);
    glEnd();
    glPointSize(1.0f);
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f12 ptA=%04x ptB=%04x ptC=%04x (expect ffff)\n",
            sampleNdc(-0.5f, -0.2f), sampleNdc(0.0f, -0.2f),
            sampleNdc(0.5f, -0.2f));

    /* f13: SMOOTH-shaded triangles (same region as the failing flat ones) */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLES);
    glColor4f(1, 1, 1, 1);   glVertex2f(-0.8f, -0.6f);
    glColor4f(0.3f, 0.3f, 0.3f, 1); glVertex2f(-0.2f, -0.6f);
    glColor4f(0.3f, 0.3f, 0.3f, 1); glVertex2f(-0.5f, -0.1f);
    glEnd();
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f13 smoothtris=%04x (nonblack = shade is the killer)\n",
           sampleNdc(-0.65f, -0.5f));
    mglWriteShotPPM("T:tc13.ppm");

    /* f14: FLAT-shaded fan (same shape as the passing smooth one) */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glShadeModel(GL_FLAT);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(-0.55f, -0.05f);
    glVertex2f(-0.25f, -0.05f);
    glVertex2f(-0.40f, 0.25f);
    glVertex2f(-0.70f, 0.25f);
    glVertex2f(-0.85f, -0.05f);
    glEnd();
    glColor4f(1, 1, 1, 1);
    glFinish();
    printf("tcl f14 flatfan=%04x (0000 = flat kills fans too)\n",
           sampleNdc(-0.55f, -0.05f));
    mglWriteShotPPM("T:tc14.ppm");
    glShadeModel(GL_FLAT);

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
