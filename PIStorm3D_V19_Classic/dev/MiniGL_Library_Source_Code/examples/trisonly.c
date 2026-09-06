/* Minimal: one clear submit, then one lone GL_TRIANGLES submit, exit.
 * With DUMP_FIRST_BATCHES builds this yields clean single-record batches
 * in T:r200_batchN.bin for host-side decode. */
#include <stdio.h>
#include <proto/dos.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

int main(void)
{
    if (!MiniGLOpen()) return 21;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240)) { MiniGLClose(); return 22; }
    glViewport(0, 0, 320, 240);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
    glClearColor(0, 0, 0, 1);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFinish();
    Delay(50);

    glColor4f(0.3f, 0.3f, 0.3f, 1);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.6f);
    glVertex2f(-0.2f, -0.6f);
    glVertex2f(-0.5f, -0.1f);
    glEnd();
    glColor4f(1, 1, 1, 1);
    glFinish();

    printf("trisonly: done err=%lx\n", (unsigned long)glGetError());
    mglDeleteContext();
    MiniGLClose();
    return 0;
}
