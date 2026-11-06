/*
 * Client import stub for minigl68k.library (the PPC-validated R200 frontend
 * built for the 68k; ABI 5, OpenGL token values). Same contract as the
 * classic MiniGLOpen(): open the library, fetch the dispatch table through
 * LVO -30, accept only ABI 5.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <libraries/minigl_dispatch.h>

#ifndef MINIGL68K_NAME
#define MINIGL68K_NAME "minigl68k.library"
#endif
#define MINIGL68K_VERSION 12

void *MiniGLBase = 0;
/* Set when the opened library is the R200 backend with the LVO -36
 * extension table (minigl68k_ext.h); any other MiniGL of this ABI runs
 * without it, on the plain GL path. */
ULONG MiniGL68KExtOK = 0;
const MGLDispatchTable *MiniGLDispatch = 0;

extern const MGLDispatchTable *MiniGLGetDispatchTableLVO(void);

/* Candidates in order: the program's own drawer first (PROGDIR:Libs, then
 * PROGDIR:), then LIBS: by name. One that is not this ABI (another MiniGL
 * of the same name) is closed again and the next is tried. */
static BOOL MiniGLTry(CONST_STRPTR name)
{
    const MGLDispatchTable *dispatch;

    MiniGLBase = OpenLibrary(name, MINIGL68K_VERSION);
    if (!MiniGLBase)
        return FALSE;
    dispatch = MiniGLGetDispatchTableLVO();
    if (!dispatch || dispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION ||
        dispatch->structSize < sizeof(MGLDispatchTable)) {
        CloseLibrary((struct Library *)MiniGLBase);
        MiniGLBase = 0;
        MiniGLDispatch = 0;
        return FALSE;
    }
    MiniGLDispatch = dispatch;
    {
        const struct Library *lib = (const struct Library *)MiniGLBase;
        const char *id = (const char *)lib->lib_IdString;
        ULONG i;

        MiniGL68KExtOK = 0;
        if (lib->lib_NegSize >= 36 && id)
            for (i = 0; id[i] && i < 200; ++i)
                if (id[i] == 'R' && id[i + 1] == '2' && id[i + 2] == '0' &&
                    id[i + 3] == '0') {
                    MiniGL68KExtOK = 1;
                    break;
                }
    }
    return TRUE;
}

BOOL MiniGLOpen(void)
{
    if (MiniGLBase && MiniGLDispatch)
        return TRUE;
#ifdef MINIGL68K_PROGDIR
    if (MiniGLTry((CONST_STRPTR)"PROGDIR:Libs/" MINIGL68K_NAME) ||
        MiniGLTry((CONST_STRPTR)"PROGDIR:" MINIGL68K_NAME))
        return TRUE;
#endif
    return MiniGLTry((CONST_STRPTR)MINIGL68K_NAME);
}

void MiniGLClose(void)
{
    MiniGLDispatch = 0;
    MiniGL68KExtOK = 0;
    if (MiniGLBase) {
        CloseLibrary((struct Library *)MiniGLBase);
        MiniGLBase = 0;
    }
}
