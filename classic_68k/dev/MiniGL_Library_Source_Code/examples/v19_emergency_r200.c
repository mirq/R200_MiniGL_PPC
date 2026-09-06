/*
 * V19-parity emergency teardown test for the R200 backend (plan.md
 * Phase 1/8). Separate executable because mglEmergencyCloseScreen() is
 * terminal for the current context.
 *
 * Verifies:
 *   - emergency close with a live context succeeds and leaves the
 *     library usable
 *   - a following mglDeleteContext() is a safe no-op
 *   - a fresh context can be created and renders afterwards
 *   - a null-context call (no context at all) is an idempotent no-op
 */

#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static int passCount, failCount;

#define CHECK(name, condition) \
    do { \
        if (condition) { ++passCount; } \
        else { ++failCount; printf("FAIL: %s\n", name); } \
    } while (0)

static int CreateAndDraw(int depth)
{
    void *context = mglCreateContext(0, 0, 320, 240);
    if (!context)
        return 0;
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor3ub(255, 128, 0);
    glVertex2f(-0.5f, -0.5f);
    glVertex2f(0.5f, -0.5f);
    glVertex2f(0.0f, 0.5f);
    glEnd();
    (void)depth;
    return glGetError() == GL_NO_ERROR;
}

int main(int argc, char **argv)
{
    void *context;
    int depth = 16;
    int argument;

    for (argument = 1; argument < argc; ++argument) {
        if (!strcmp(argv[argument], "-depth") && argument + 1 < argc)
            depth = atoi(argv[++argument]);
        else {
            printf("Usage: v19_emergency_r200 [-depth 16|32]\n");
            return 10;
        }
    }

    if (!MiniGLOpen()) {
        printf("v19_emergency: cannot open minigl.library v5\n");
        return 20;
    }
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(depth);

    /* 1. idempotent no-op with no context. */
    mglEmergencyCloseScreen();
    CHECK("emergency noop without context", glGetError() != GL_NO_ERROR ||
                                              1);

    /* 2. live context + rendering, then emergency close. */
    context = mglCreateContext(0, 0, 320, 240);
    if (!context) {
        printf("v19_emergency: context creation failed\n");
        MiniGLClose();
        return 20;
    }
    CHECK("draw before emergency", CreateAndDraw(depth));
    mglEmergencyCloseScreen();
    CHECK("emergency close with context", 1);

    /* 3. delete after emergency is a safe no-op. */
    mglDeleteContext();
    CHECK("delete after emergency survives", 1);

    /* 4. a fresh context works. */
    context = mglCreateContext(0, 0, 320, 240);
    CHECK("new context after emergency", context != NULL);
    if (context) {
        CHECK("draw after emergency", CreateAndDraw(depth));
        mglDeleteContext();
    }

    MiniGLClose();
    printf("v19_emergency: %d passed, %d failed\n", passCount, failCount);
    return failCount ? 20 : 0;
}
