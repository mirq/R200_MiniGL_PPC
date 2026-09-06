/*
 * Commit-batch bisection: renders the visualprobe effects cumulatively,
 * one additional effect per frame, dumping the surface after every frame.
 * Frame N contains effects Q1..QN, so the first dump that goes wrong names
 * the effect whose submission corrupts the batch.
 */
#include <stdio.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static MGLLockInfo lock;

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

static void q1_textured_fill(void)
{
    GLubyte texels[4] = {255, 255, 255, 128};
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
}

static void q2_diffuse_fill(void)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 0.5f);
    rect_dev(160.0f, 0.0f, 320.0f, 120.0f);
}

static void q3_smooth_fan(void)
{
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
}

static void q4_additive_box(void)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glColor4f(0.25f, 0.25f, 0.25f, 1);
    rect_ndc(0.10f, -0.30f, 0.85f, 0.20f);
    rect_ndc(0.10f, -0.30f, 0.85f, 0.20f);
    glDisable(GL_BLEND);
}

static void q5_points(void)
{
    glPointSize(4.0f);
    glBegin(GL_POINTS);
    glColor4ub(255, 255, 255, 255); glVertex2f(-0.7f, -0.7f);
    glColor4ub(255, 255, 255, 200); glVertex2f( 0.0f, -0.7f);
    glColor4ub(255, 255, 255, 64);  glVertex2f(-0.35f, -0.85f);
    glEnd();
}

static void q6_depth(void)
{
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
}

typedef void (*effect_fn)(void);
static const effect_fn effects[6] = {
    q1_textured_fill, q2_diffuse_fill, q3_smooth_fan,
    q4_additive_box, q5_points, q6_depth
};
static const char *names[6] = { "q1", "q2", "q3", "q4", "q5", "q6" };

int main(void)
{
    int frame;

    if (!MiniGLOpen()) return 21;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240)) { MiniGLClose(); return 22; }
    glViewport(0, 0, 320, 240);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);

    /* Warm-up: defeats nothing in particular, matches real usage. */
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor4f(1, 1, 1, 1);
    rect_dev(0, 0, 8, 8);
    glFlush();

    for (frame = 0; frame < 6; ++frame) {
        int step;
        char path[32];
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (step = 0; step <= frame; ++step)
            effects[step]();
        glColor4f(1, 1, 1, 1);
        glFinish();
        for (step = 0; step < 200UL; ++step)
            Delay(1);
        path[0] = 'T'; path[1] = ':'; path[2] = 'v'; path[3] = 'p';
        path[4] = '_';
        path[5] = names[frame][0]; path[6] = names[frame][1];
        path[7] = '.'; path[8] = 'p'; path[9] = 'p'; path[10] = 'm';
        path[11] = 0;
        mglWriteShotPPM(path);
    }

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
