#include <dos/dos.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility/tagitem.h>

#include <p96_bitmap_api.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

struct Library *P96Base;

#define WINDOW_FRAME_COUNT 20
#define FULLSCREEN_FRAME_COUNT 100

int main(int argc, char **argv)
{
    void *context;
    int frame;
    int frameCount;
    GLboolean windowMode = GL_TRUE;
    BOOL explicitMode = FALSE;
    ULONG modeId = 0;
    GLint width = 640;
    GLint height = 480;
    GLint depth = 16;
    GLint buffers = 0;
    int argument;
    const char *modeName = "window";

    for (argument = 1; argument < argc; ++argument) {
        if (!strcmp(argv[argument], "--fullscreen")) {
            windowMode = GL_FALSE;
            modeName = "fullscreen";
        } else if (!strcmp(argv[argument], "--modeid")) {
            struct TagItem modeTags[] = {
                {P96_BESTMODE_FORMATS_ALLOWED, RGBFF_R5G6B5PC},
                {P96_BESTMODE_NOMINAL_WIDTH, 640UL},
                {P96_BESTMODE_NOMINAL_HEIGHT, 480UL},
                {P96_BESTMODE_DEPTH, 16UL},
                {TAG_DONE, 0}
            };
            windowMode = GL_FALSE;
            explicitMode = TRUE;
            modeName = "modeid";
            P96Base = OpenLibrary("Picasso96API.library", 2);
            if (P96Base)
                modeId = p96BestModeIDTagList(modeTags);
            if (P96Base)
                CloseLibrary(P96Base);
            P96Base = NULL;
            if (modeId == 0xffffffffUL) {
                printf("MiniGL_R200 smoke: no RGB565 mode ID\n");
                return 20;
            }
        } else if (!strcmp(argv[argument], "-buffers") &&
                   argument + 1 < argc) {
            buffers = atoi(argv[++argument]);
        } else if (!strcmp(argv[argument], "-width") &&
                   argument + 1 < argc) {
            width = atoi(argv[++argument]);
        } else if (!strcmp(argv[argument], "-height") &&
                   argument + 1 < argc) {
            height = atoi(argv[++argument]);
        } else if (!strcmp(argv[argument], "-depth") &&
                   argument + 1 < argc) {
            depth = atoi(argv[++argument]);
        } else {
            printf("Usage: minigl_r200_smoke [--fullscreen|--modeid]"
                   " [-width N] [-height N] [-depth N]\n");
            return 10;
        }
    }
    frameCount = windowMode ? WINDOW_FRAME_COUNT : FULLSCREEN_FRAME_COUNT;

    if (!MiniGLOpen()) {
        printf("MiniGL_R200 smoke: cannot open minigl.library v4+\n");
        return 20;
    }
    mglChooseWindowMode(windowMode);
    mglChoosePixelDepth((int)depth);
    if (buffers >= 1 && buffers <= 3)
        mglChooseNumberOfBuffers((int)buffers);
    context = explicitMode
                  ? mglCreateContextFromID((GLint)modeId, &width, &height)
                  : mglCreateContext(0, 0, width, height);
    if (!context) {
        printf("MiniGL_R200 smoke: context creation failed\n");
        MiniGLClose();
        return 20;
    }

    printf("MiniGL_R200 smoke: mode=%s vendor=%s renderer=%s version=%s\n",
           modeName,
           glGetString(GL_VENDOR), glGetString(GL_RENDERER),
           glGetString(GL_VERSION));
    /*
     * mglProposeCloseDesktop() is one of the R200 backend's unimplemented
     * dispatch table entries; it must still route through the table and
     * raise GL_INVALID_OPERATION rather than crash or no-op silently.
     */
    mglProposeCloseDesktop(GL_FALSE);
    if (glGetError() != GL_INVALID_OPERATION) {
        printf("MiniGL_R200 smoke: unsupported-vector behavior failed\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }
    if (MiniGLDispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION ||
        MiniGLDispatch->structSize < (ULONG)sizeof(MGLDispatchTable)) {
        printf("MiniGL_R200 smoke: dispatch table abi mismatch\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }
    printf("MiniGL_R200 smoke: abi_tail=pass mode_size=%ldx%ld\n",
           (long)width, (long)height);
    for (frame = 0; frame < frameCount; ++frame) {
        glClearColor(0.04f, 0.07f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3ub(255, 64, 48);
        glVertex2f(-0.72f, -0.58f);
        glColor3ub(48, 224, 112);
        glVertex2f(0.72f, -0.58f);
        glColor3ub(64, 128, 255);
        glVertex2f(0.0f, 0.72f);
        glEnd();
        mglSwitchDisplay();
        if (glGetError() != GL_NO_ERROR) {
            printf("MiniGL_R200 smoke: render error at frame %ld\n",
                   (long)frame);
            mglDeleteContext();
            MiniGLClose();
            return 20;
        }
        Delay(1);
    }

    mglDeleteContext();
    MiniGLClose();
    printf("MiniGL_R200 smoke: pass mode=%s frames=%ld\n",
           modeName, (long)frameCount);
    return 0;
}
