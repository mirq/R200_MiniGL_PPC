#include <dos/dos.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/minigl.h>
#include <libraries/minigl_perf.h>
#include <clib/minigl_open_protos.h>

#define PI 3.14159265358979f

int main(int argc, char **argv)
{
    void *context;
    int frame;
    int frameCount = 200;
    GLint count = 0;
    int index;

    (void)argc;
    (void)argv;

    if (!MiniGLOpen()) {
        printf("v19_worst_probe: cannot open minigl.library\n");
        return 20;
    }
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(32);
    context = mglCreateContext(0, 0, 512, 320);
    if (!context) {
        printf("v19_worst_probe: context creation failed\n");
        MiniGLClose();
        return 20;
    }
    glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);

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
            printf("v19_worst_probe: render error at frame %ld\n",
                   (long)frame);
            mglDeleteContext();
            MiniGLClose();
            return 20;
        }
        Delay(1);
    }

    glGetIntegerv(MGL_PERF_WORST_FRAME_COUNT, &count);
    printf("v19_worst_probe: frames=%ld worst_count=%ld\n",
           (long)frameCount, (long)count);
    if (count > 5)
        count = 5;
    for (index = 0; index < count; ++index) {
        GLint number = 0, ticks = 0, serialize = 0, execute = 0;
        GLint wait = 0, present = 0, residency = 0;

        glGetIntegerv(MGL_PERF_WORST_FRAME_NUMBER(index), &number);
        glGetIntegerv(MGL_PERF_WORST_FRAME_TICKS(index), &ticks);
        glGetIntegerv(MGL_PERF_WORST_SERIALIZE_TICKS(index), &serialize);
        glGetIntegerv(MGL_PERF_WORST_EXECUTE_TICKS(index), &execute);
        glGetIntegerv(MGL_PERF_WORST_WAIT_TICKS(index), &wait);
        glGetIntegerv(MGL_PERF_WORST_PRESENT_TICKS(index), &present);
        glGetIntegerv(MGL_PERF_WORST_RESIDENCY_TICKS(index), &residency);
        printf("v19_worst_probe: rank=%ld frame=%ld total=%ld "
               "serialize=%ld execute=%ld wait=%ld present=%ld "
               "residency=%ld\n",
               (long)index, (long)number, (long)ticks, (long)serialize,
               (long)execute, (long)wait, (long)present, (long)residency);
    }

    glHint(MGL_PERF_COUNTERS_HINT, GL_FASTEST);
    mglDeleteContext();
    MiniGLClose();
    printf("v19_worst_probe: pass\n");
    return 0;
}
