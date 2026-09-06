#include <stdio.h>
#include <libraries/minigl.h>
#include <libraries/minigl_dispatch.h>
#include "dll.h"

typedef const MGLDispatchTable *(*GetDispatchFn)(void);

static void LegacyClose(void *instance)
{
    MiniGLDispatch = NULL;
    MiniGLBase = NULL;
    if (instance)
        dllFreeLibrary(instance);
}

int main(void)
{
    const MGLDispatchTable *dispatch;
    GetDispatchFn getDispatch;
    void *instance;

    setvbuf(stdout, NULL, _IONBF, 0);
    puts("legacy: load");
    instance = dllLoadLibrary((char *)MINIGL_PPC_DLLNAME,
                              (char *)MINIGL_PPC_PORTNAME);
    if (!instance)
        return 20;
    getDispatch = (GetDispatchFn)
        dllGetProcAddress(instance, (char *)"MGLGetDispatchTable");
    dispatch = getDispatch ? getDispatch() : NULL;
    if (!dispatch || dispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION ||
        dispatch->structSize < (ULONG)sizeof(*dispatch)) {
        LegacyClose(instance);
        return 20;
    }
    MiniGLBase = instance;
    MiniGLDispatch = dispatch;

    mglChooseWindowMode(GL_TRUE);
    puts("legacy: create");
    if (!mglCreateContext(0, 0, 320, 240)) {
        puts("legacy: create failed");
        LegacyClose(instance);
        return 20;
    }
    glClearColor(0.15f, 0.05f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    mglSwitchDisplay();
    puts("legacy: delete");
    mglDeleteContext();
    LegacyClose(instance);
    puts("legacy: done");
    return 0;
}
