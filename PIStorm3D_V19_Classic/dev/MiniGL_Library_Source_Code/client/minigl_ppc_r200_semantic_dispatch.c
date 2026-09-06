#include <exec/types.h>
#include <exec/libraries.h>

#define MINIGL_LIBRARY_BUILD 1
#include <libraries/minigl_dispatch.h>

GLcontext mini_CurrentContext = (GLcontext)0;

#define Lib_mglCreateContext R200PPCDispatchCreateContext
#define Lib_mglCreateContextFromID R200PPCDispatchCreateContextFromID
#define Lib_mglDeleteContext R200PPCDispatchDeleteContext
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

#include "../library/dispatch_table_r200.c"

void R200PPCDispatchExit(void)
{
    R200PPCDispatchDeleteContext();
    mini_CurrentContext = (GLcontext)0;
}

extern BOOL R200PPCPlatformOpen(void);
extern void R200PPCPlatformClose(void);

struct Library *MiniGLBase = NULL;
const MGLDispatchTable *MiniGLDispatch = NULL;

BOOL MiniGLOpen(void)
{
    if (MiniGLDispatch)
        return TRUE;
    if (!R200PPCPlatformOpen())
        return FALSE;
    MiniGLDispatch = &MiniGLDispatchTable;
    return TRUE;
}

void MiniGLClose(void)
{
    if (!MiniGLDispatch)
        return;
    R200PPCPlatformClose();
    mini_CurrentContext = (GLcontext)0;
    MiniGLDispatch = NULL;
}
