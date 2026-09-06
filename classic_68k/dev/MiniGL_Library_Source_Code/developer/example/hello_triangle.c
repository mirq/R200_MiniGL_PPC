#include <stdio.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

#define WIDTH  640
#define HEIGHT 480

int main(void)
{
    const GLubyte *renderer;

    if (!MiniGLOpen()) {
        puts("Could not open a compatible minigl.library");
        return 20;
    }

    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    mglChooseNumberOfBuffers(2);

    if (!mglCreateContext(0, 0, WIDTH, HEIGHT)) {
        puts("Could not create the MiniGL context");
        MiniGLClose();
        return 20;
    }

    renderer = glGetString(GL_RENDERER);
    printf("MiniGL renderer: %s\n",
           renderer ? (const char *)renderer : "<unknown>");

    glViewport(0, 0, WIDTH, HEIGHT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.25f, 0.15f);
    glVertex2f(0.0f, 0.75f);
    glColor3f(0.15f, 0.85f, 0.35f);
    glVertex2f(-0.75f, -0.65f);
    glColor3f(0.15f, 0.4f, 1.0f);
    glVertex2f(0.75f, -0.65f);
    glEnd();

    mglSwitchDisplay();
    glFinish();

    puts("Triangle presented; press Return to quit.");
    (void)getchar();

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
