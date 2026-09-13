#define MINIGL_LIBRARY_BUILD 1

#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <exec/types.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#ifdef WARPUP
#pragma pack(pop)
#endif

#include <libraries/minigl_dispatch.h>
#include "dll.h"

GLcontext mini_CurrentContext = (GLcontext)0;

extern BOOL R200PPCPlatformOpen(void);
extern void R200PPCPlatformClose(void);
extern void *R200PPCDispatchCreateContext(int offx, int offy, int width,
                                         int height);
extern void *R200PPCDispatchCreateContextFromID(GLint id, GLint *width,
                                               GLint *height);
extern void R200PPCDispatchDeleteContext(void);
extern void R200PPCPlatformResetClientDefaults(void);
extern void R200PPCTraceStr(const char *line);

static struct Task *R200ClientOwner;
static BOOL R200LegacyClient;

static void R200ReleaseLegacyClient(void)
{
    R200PPCTraceStr("release legacy enter");
    if (!R200LegacyClient || R200ClientOwner != FindTask(NULL)) {
        R200PPCTraceStr("release legacy skip");
        return;
    }
    R200PPCPlatformClose();
    mini_CurrentContext = (GLcontext)0;
    R200ClientOwner = NULL;
    R200LegacyClient = FALSE;
    R200PPCTraceStr("release legacy done");
}

void *R200ABI3CreateContext(int offx, int offy, int width, int height)
{
    void *context = R200PPCDispatchCreateContext(offx, offy, width, height);
    if (!context)
        R200ReleaseLegacyClient();
    return context;
}

void *R200ABI3CreateContextFromID(GLint id, GLint *width, GLint *height)
{
    void *context = R200PPCDispatchCreateContextFromID(id, width, height);
    if (!context)
        R200ReleaseLegacyClient();
    return context;
}

void R200ABI3DeleteContext(void)
{
    R200PPCDispatchDeleteContext();
    R200ReleaseLegacyClient();
}

/* Context, display and texture ownership lives on the 68k R200 host. */
#define Lib_mglCreateContext R200ABI3CreateContext
#define Lib_mglCreateContextFromID R200ABI3CreateContextFromID
#define Lib_mglDeleteContext R200ABI3DeleteContext
#define Lib_mglExit R200PPCDispatchExit
#define Lib_mglSwitchDisplay R200PPCDispatchSwitchDisplay
#define Lib_mglGetWindowHandle R200PPCDispatchGetWindowHandle
#define Lib_mglGetInputWindowHandle R200PPCDispatchGetWindowHandle
#define Lib_mglLockDisplay R200PPCDispatchLockDisplay
#define Lib_mglUnlockDisplay R200PPCDispatchUnlockDisplay
#define Lib_mglLockBack R200PPCDispatchLockBack
#define Lib_mglChoosePixelDepth R200PPCDispatchChoosePixelDepth
#define Lib_mglChooseWindowMode R200PPCDispatchChooseWindowMode
#define Lib_mglChooseVertexBufferSize R200PPCDispatchChooseVertexBufferSize
#define Lib_mglChooseTextureBufferSize R200PPCDispatchChooseTextureBufferSize
#define Lib_mglChooseMtexBufferSize R200PPCDispatchChooseMtexBufferSize
#define Lib_mglChooseNumberOfBuffers R200PPCDispatchChooseNumberOfBuffers
#define Lib_mglEnableSync R200PPCDispatchEnableSync
#define Lib_glBindTexture R200PPCDispatchBindTexture
#define Lib_glDeleteTextures R200PPCDispatchDeleteTextures
#define Lib_glGenTextures R200PPCDispatchGenTextures
#define Lib_glTexImage2D R200PPCDispatchTexImage2D
#define Lib_glTexSubImage2D R200PPCDispatchTexSubImage2D
#define Lib_glReadPixels R200PPCDispatchReadPixels
#define Lib_mglWriteShotPPM R200PPCDispatchWriteShotPPM
#define Lib_mglEmergencyCloseScreen R200PPCDispatchEmergencyCloseScreen

#include "../backend_r200/generated/dispatch_table_abi3.c"

typedef char R200DispatchABI3SizeCheck[
    sizeof(MGLDispatchTable) == 0x22c ? 1 : -1];

void R200PPCDispatchExit(void)
{
    R200PPCTraceStr("dispatch exit enter");
    R200ABI3DeleteContext();
    mini_CurrentContext = (GLcontext)0;
    R200PPCTraceStr("dispatch exit done");
}

void R200PPCDispatchEmergencyCloseScreen(void)
{
    R200PPCDispatchExit();
}

BOOL __saveds MGLPPCClientAttach(void)
{
    struct Task *task = FindTask(NULL);

    if (R200ClientOwner) {
        if (R200ClientOwner != task)
            return FALSE;
        R200LegacyClient = FALSE;
        return TRUE;
    }
    if (!R200PPCPlatformOpen())
        return FALSE;
    R200PPCPlatformResetClientDefaults();
    R200ClientOwner = task;
    R200LegacyClient = FALSE;
    return TRUE;
}

void __saveds MGLPPCClientDetach(void)
{
    if (!R200ClientOwner || R200ClientOwner != FindTask(NULL))
        return;
    R200PPCPlatformClose();
    mini_CurrentContext = (GLcontext)0;
    R200ClientOwner = NULL;
    R200LegacyClient = FALSE;
}

/* Old ABI-3 import archives know only this export. Their normal context
 * delete path releases the transport while still executing in this task. */
const MGLDispatchTable * __saveds MGLGetDispatchTable(void)
{
    struct Task *task = FindTask(NULL);

    if (R200ClientOwner)
        return R200ClientOwner == task ? &MiniGLDispatchTable : NULL;
    if (!R200PPCPlatformOpen())
        return NULL;
    R200PPCPlatformResetClientDefaults();
    R200ClientOwner = task;
    R200LegacyClient = TRUE;
    return &MiniGLDispatchTable;
}

void * __saveds dllFindResource(int type, char *name)
{
    (void)type;
    (void)name;
    return NULL;
}

void * __saveds dllLoadResource(void *resource)
{
    (void)resource;
    return NULL;
}

void __saveds dllFreeResource(void *resource)
{
    (void)resource;
}

dll_tExportSymbol DLL_ExportSymbols[] = {
    { (void *)dllFindResource,       (char *)"dllFindResource" },
    { (void *)dllLoadResource,       (char *)"dllLoadResource" },
    { (void *)dllFreeResource,       (char *)"dllFreeResource" },
    { (void *)MGLGetDispatchTable,   (char *)"MGLGetDispatchTable" },
    { (void *)MGLPPCClientAttach,    (char *)"MGLPPCClientAttach" },
    { (void *)MGLPPCClientDetach,    (char *)"MGLPPCClientDetach" },
    { NULL, NULL }
};

dll_tImportSymbol DLL_ImportSymbols[] = {
    { NULL, NULL, NULL, NULL }
};

int __saveds DLL_Init(void)
{
    mini_CurrentContext = (GLcontext)0;
    R200ClientOwner = NULL;
    R200LegacyClient = FALSE;
    return 1;
}

void __saveds DLL_DeInit(void)
{
    /* A legacy client may unload without the detach export. Its reply port is
     * owned by that client task, so teardown here would wait on the wrong task. */
    mini_CurrentContext = (GLcontext)0;
    R200ClientOwner = NULL;
    R200LegacyClient = FALSE;
}
