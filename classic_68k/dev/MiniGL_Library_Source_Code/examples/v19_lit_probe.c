#include <stdio.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

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

static int capture(void)
{
    glFlush();
    return mglLockBack(&lock) ? 0 : 1;
}

static void clear_scene(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void quad_at(GLfloat cx, GLfloat cy, GLfloat nz)
{
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.0f, nz);
    glVertex3f(cx - 0.25f, cy - 0.25f, -0.5f);
    glVertex3f(cx + 0.25f, cy - 0.25f, -0.5f);
    glVertex3f(cx + 0.25f, cy + 0.25f, -0.5f);
    glVertex3f(cx - 0.25f, cy + 0.25f, -0.5f);
    glEnd();
}

int main(void)
{
    UWORD background, unlit, lit;
    GLint err;

    if (!MiniGLOpen()) { printf("LP open fail\n"); return 21; }
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240)) {
        printf("LP ctx fail\n"); MiniGLClose(); return 22;
    }

    clear_scene();
    if (capture()) { printf("LP cap fail\n"); return 23; }
    background = sample(-0.8f, 0.0f);

    glColor3ub(255, 255, 255);
    clear_scene();
    quad_at(0.0f, 0.0f, 1.0f);
    if (capture()) { printf("LP cap fail\n"); return 23; }
    unlit = sample(0.0f, 0.0f);
    err = glGetError();
    printf("LP unlit=%04lx bg=%04lx err=%08lx\n",
           (unsigned long)unlit, (unsigned long)background,
           (unsigned long)err);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    err = glGetError();
    printf("LP after-enable err=%08lx isEnabled=%ld\n",
           (unsigned long)err, (long)glIsEnabled(GL_LIGHTING));

    clear_scene();
    quad_at(0.0f, 0.0f, 1.0f);
    if (capture()) { printf("LP cap fail\n"); return 23; }
    lit = sample(0.0f, 0.0f);
    err = glGetError();
    printf("LP lit=%04lx err=%08lx same_as_unlit=%ld same_as_bg=%ld\n",
           (unsigned long)lit, (unsigned long)err,
           (long)(lit == unlit), (long)(lit == background));

    /* Second opinion with a bright green unlit reference: if the lit
     * quad still renders WHITE, the colour is passing through and only
     * the lighting math is skipped; if BLACK/BACKGROUND, the draw is
     * dropped. */
    glDisable(GL_LIGHTING);
    glColor3ub(0, 255, 0);
    clear_scene();
    quad_at(0.0f, 0.0f, 1.0f);
    if (capture()) { printf("LP cap fail\n"); return 23; }
    printf("LP green_unlit=%04lx\n", (unsigned long)sample(0.0f, 0.0f));

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    clear_scene();
    quad_at(0.0f, 0.0f, 1.0f);
    if (capture()) { printf("LP cap fail\n"); return 23; }
    printf("LP green_lit=%04lx err=%08lx\n",
           (unsigned long)sample(0.0f, 0.0f), (unsigned long)glGetError());

    mglDeleteContext();
    MiniGLClose();
    printf("LP done\n");
    return 0;
}
