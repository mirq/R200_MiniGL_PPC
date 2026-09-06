#include <stdio.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <proto/dos.h>

static UWORD sample(const MGLLockInfo *lock, ULONG x, ULONG y)
{
    UBYTE *pixel = (UBYTE *)lock->base_address + y * lock->pitch + x * 2UL;
    return (UWORD)(((UWORD)pixel[0] << 8) | pixel[1]);
}

static void triangle(GLfloat center, GLubyte red, GLubyte green, GLubyte blue)
{
    glColor3ub(red, green, blue);
    glBegin(GL_TRIANGLES);
    glVertex2f(center - 0.2f, -0.3f);
    glVertex2f(center + 0.2f, -0.3f);
    glVertex2f(center, 0.3f);
    glEnd();
}

int main(void)
{
    MGLLockInfo lock;

    if (!MiniGLOpen()) return 20;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240)) return 21;
    glDisable(GL_DEPTH_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    triangle(-0.6f, 255, 0, 0);
    triangle(0.0f, 0, 255, 0);
    triangle(0.6f, 0, 0, 255);
    glFinish();
    if (!mglLockBack(&lock)) return 22;
    printf("HW_TCL_COLOR red=%04lx green=%04lx blue=%04lx error=%08lx\n",
           (ULONG)sample(&lock, 64, 120),
           (ULONG)sample(&lock, 160, 120),
           (ULONG)sample(&lock, 256, 120), (ULONG)glGetError());
    mglUnlockDisplay();
    mglSwitchDisplay();
    Delay(250);
    mglDeleteContext();
    MiniGLClose();
    return 0;
}
