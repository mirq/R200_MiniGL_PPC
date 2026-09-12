#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/minigl.h>

int main(int argc, char **argv)
{
    GLenum error;
    int depth = 0;
    GLboolean windowed = GL_TRUE;
    GLubyte center[4] = {0}, corner[4] = {0};
    int pixelsOk;

    if (argc > 1) {
        depth = atoi(argv[1]);
        if (depth != 15 && depth != 16 && depth != 24 && depth != 32) {
            puts("usage: r200_triangle_smoke [15|16|24|32]");
            return 10;
        }
    }
    if (argc > 2) {
        if (strcmp(argv[2], "fullscreen") == 0)
            windowed = GL_FALSE;
        else if (strcmp(argv[2], "window") != 0) {
            puts("usage: r200_triangle_smoke [15|16|24|32] [window|fullscreen]");
            return 10;
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    puts("triangle: open");
    if (!MiniGLOpen()) {
        puts("triangle: open failed");
        return 20;
    }

    mglChooseWindowMode(windowed);
    if (depth)
        mglChoosePixelDepth(depth);
    mglChooseVertexBufferSize(1000);
    printf("triangle: create depth=%d mode=%s\n", depth ? depth : 32,
           windowed ? "window" : "fullscreen");
    if (!mglCreateContext(0, 0, 640, 480)) {
        puts("triangle: create failed");
        MiniGLClose();
        return 20;
    }

    puts("triangle: draw");
    glViewport(0, 0, 640, 480);
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor4ub(255, 64, 64, 255);
    glVertex3f(-0.75f, -0.65f, 0.0f);
    glColor4ub(64, 255, 96, 255);
    glVertex3f(0.75f, -0.65f, 0.0f);
    glColor4ub(64, 128, 255, 255);
    glVertex3f(0.0f, 0.75f, 0.0f);
    glEnd();
    glFinish();
    glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    /* Interior interpolation stays within the three vertex colours; the
     * background is dark. Tolerances also cover RGB565 quantization. */
    pixelsOk = center[0] >= 50 && center[1] >= 50 && center[2] >= 50 &&
               corner[0] < 40 && corner[1] < 40 && corner[2] < 40;
    printf("triangle: pixels=%s center=%u,%u,%u corner=%u,%u,%u\n",
           pixelsOk ? "pass" : "FAIL", (unsigned)center[0], (unsigned)center[1],
           (unsigned)center[2], (unsigned)corner[0], (unsigned)corner[1],
           (unsigned)corner[2]);
    mglSwitchDisplay();
    error = glGetError();
    printf("triangle: error=0x%04lx\n", (unsigned long)error);

    mglDeleteContext();
    MiniGLClose();
    puts("triangle: done");
    return error == GL_NO_ERROR && pixelsOk ? 0 : 20;
}
