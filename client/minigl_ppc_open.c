#include <exec/types.h>
#include <libraries/minigl.h>
#include <libraries/minigl_dispatch.h>
#include "dll.h"

typedef const MGLDispatchTable *(*MGLGetDispatchTableFn)(void);
typedef BOOL (*MGLPPCClientAttachFn)(void);
typedef void (*MGLPPCClientDetachFn)(void);

static MGLPPCClientDetachFn MiniGLDetach;

BOOL MiniGLOpen(void)
{
    void *hinst;
    MGLGetDispatchTableFn getDispatch;
    MGLPPCClientAttachFn attach;
    MGLPPCClientDetachFn detach;
    const MGLDispatchTable *dispatch;

    if (MiniGLBase && MiniGLDispatch)
        return TRUE;

    hinst = dllLoadLibrary((char *)MINIGL_PPC_DLLNAME,
                           (char *)MINIGL_PPC_PORTNAME);
    if (!hinst)
        return FALSE;

    getDispatch = (MGLGetDispatchTableFn)
        dllGetProcAddress(hinst, (char *)"MGLGetDispatchTable");
    if (!getDispatch) {
        dllFreeLibrary(hinst);
        return FALSE;
    }

    attach = (MGLPPCClientAttachFn)
        dllGetProcAddress(hinst, (char *)"MGLPPCClientAttach");
    detach = (MGLPPCClientDetachFn)
        dllGetProcAddress(hinst, (char *)"MGLPPCClientDetach");
    if ((attach && !detach) || (!attach && detach) || (attach && !attach())) {
        dllFreeLibrary(hinst);
        return FALSE;
    }

    /* Both caller and returned function are WarpOS/PPC code.
     * abiVersion is compared for EQUALITY (as upstream minigl_open.c does):
     * ABI 5 changed the GL token values, so no pairing across the 3 -> 5
     * boundary can work in either direction. structSize keeps its "at least
     * mine" test, which is what lets an appended slot keep older ABI-5
     * clients working. */
    dispatch = getDispatch();
    if (!dispatch ||
        dispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION ||
        dispatch->structSize < (ULONG)sizeof(MGLDispatchTable) ||
        !dispatch->currentContext) {
        if (detach)
            detach();
        dllFreeLibrary(hinst);
        return FALSE;
    }

    MiniGLBase = hinst;
    MiniGLDispatch = dispatch;
    MiniGLDetach = detach;
    return TRUE;
}

void MiniGLClose(void)
{
    void *hinst = MiniGLBase;

    if (MiniGLDetach)
        MiniGLDetach();

    /* Invalidate pointers before unloading the PPC DLL. */
    MiniGLDetach = 0;
    MiniGLDispatch = 0;
    MiniGLBase = 0;

    if (hinst)
        dllFreeLibrary(hinst);
}
