#include <dos/dos.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

#define PI 3.14159265358979f

int main(int argc, char **argv)
{
    void *context;
    int frame;
    int frameCount;
    int seconds = 20;
    GLint depth = 16;
    GLint buffers = 0;
    GLboolean windowMode = GL_TRUE;
    int argument;
    const char *modeName = "window";

    for (argument = 1; argument < argc; ++argument) {
        if (!strcmp(argv[argument], "--fullscreen")) {
            windowMode = GL_FALSE;
            modeName = "fullscreen";
        } else if (!strcmp(argv[argument], "-buffers") &&
                   argument + 1 < argc) {
            buffers = atoi(argv[++argument]);
        } else if (!strcmp(argv[argument], "-depth") &&
                   argument + 1 < argc) {
            depth = atoi(argv[++argument]);
        } else if (!strcmp(argv[argument], "-seconds") &&
                   argument + 1 < argc) {
            seconds = atoi(argv[++argument]);
        } else {
            printf("Usage: v19_tri_buffers [--fullscreen]"
                   " [-depth N] [-buffers N] [-seconds N]\n");
            return 10;
        }
    }
    frameCount = seconds * 50;

    if (!MiniGLOpen()) {
        printf("v19_tri_buffers: cannot open minigl.library\n");
        return 20;
    }
    mglChooseWindowMode(windowMode);
    mglChoosePixelDepth((int)depth);
    if (buffers >= 1 && buffers <= 3)
        mglChooseNumberOfBuffers((int)buffers);
    context = mglCreateContext(0, 0, 640, 480);
    if (!context) {
        printf("v19_tri_buffers: context creation failed\n");
        MiniGLClose();
        return 20;
    }
    printf("v19_tri_buffers: mode=%s depth=%ld buffers=%ld frames=%ld\n",
           modeName, (long)depth, (long)buffers, (long)frameCount);

    for (frame = 0; frame < frameCount; ++frame) {
        float angle = (float)frame * (2.0f * PI / 240.0f);
        float cosA = cosf(angle);
        float sinA = sinf(angle);
        glClearColor(0.04f, 0.07f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3ub(255, 64, 48);
        glVertex2f(-0.72f * cosA + 0.58f * sinA,
                   -0.72f * sinA - 0.58f * cosA);
        glColor3ub(48, 224, 112);
        glVertex2f(0.72f * cosA + 0.58f * sinA,
                   0.72f * sinA - 0.58f * cosA);
        glColor3ub(64, 128, 255);
        glVertex2f(0.0f * cosA - 0.72f * sinA,
                   0.0f * sinA + 0.72f * cosA);
        glEnd();
        mglSwitchDisplay();
        if (glGetError() != GL_NO_ERROR) {
            printf("v19_tri_buffers: render error at frame %ld\n",
                   (long)frame);
            mglDeleteContext();
            MiniGLClose();
            return 20;
        }
        Delay(1);
    }

    mglDeleteContext();
    MiniGLClose();
    printf("v19_tri_buffers: pass mode=%s buffers=%ld frames=%ld\n",
           modeName, (long)buffers, (long)frameCount);
    return 0;
}
