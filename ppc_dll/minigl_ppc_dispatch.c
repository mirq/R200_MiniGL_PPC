#define MINIGL_LIBRARY_BUILD 1
#include <exec/types.h>
#include <libraries/minigl_dispatch.h>
#include "dll.h"

/* The bundled mglQ3 core owns mini_CurrentContext (context.c). */
extern GLcontext mini_CurrentContext;

/* Present in compatible MiniGL backends but not declared by every old header. */
extern void MGLLockMode(GLcontext context, GLenum lockMode);
extern void mglChooseNumberOfBuffers(int number);
extern void mglChooseVertexBufferSize(int size);
extern void mglChooseWindowMode(GLboolean flag);
extern void mglProposeCloseDesktop(GLboolean closeme);

/* API additions not implemented independently by the classic mglQ3 core. */
static void PPCStub_GLClientActiveTextureARB(GLcontext context, GLenum unit)
{
    (void)context;
    (void)unit;
}

/* MGLResizeContext is disabled in mglQ3 until fullscreen resource handling
 * is safe again. Keep the public slot but make it a no-op. */
static void PPCStub_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height)
{
    (void)context;
    (void)width;
    (void)height;
}

static void *PPC_MGLGetInputWindowHandle(GLcontext context)
{
    return MGLGetWindowHandle(context);
}

static void PPC_GLMultiDrawArrays(GLcontext context, GLenum mode,
    const GLint *first, const GLsizei *count, GLsizei primcount)
{
    GLsizei i;

    if (!first || !count || primcount <= 0)
        return;

    for (i = 0; i < primcount; ++i)
        if (count[i] > 0)
            GLDrawArrays(context, mode, first[i], count[i]);
}

/* Same conservative behaviour as the Classic mglQ3 backend. */
static void PPCStub_MGLEmergencyCloseScreen(GLcontext context) { (void)context; }
static void PPCStub_MGLForceNewRenderPass(GLcontext context) { (void)context; }

/* The bundled mglQ3 core has no FastPath direct vertex buffer. */
static GLboolean PPCStub_mglBeginFastPath(GLenum primitive, ULONG attributes,
                                          MGLFastPath *fp)
{
    (void)primitive; (void)attributes; (void)fp;
    return GL_FALSE;
}
static void PPCStub_mglEndFastPath(MGLFastPath *fp) { (void)fp; }
static ULONG PPCStub_mglGetFastPathCapabilities(void) { return 0; }

#ifndef MINIGL_PPC_BACKEND_FLAGS
#define MINIGL_PPC_BACKEND_FLAGS MINIGL_BACKEND_FLAG_PPC
#endif

static const MGLDispatchTable PPCDispatchTable = {
    MINIGL_DISPATCH_ABI_VERSION,
    (ULONG)sizeof(MGLDispatchTable),
    MINIGL_PPC_BACKEND_FLAGS,
    0,
    &mini_CurrentContext,
    GLActiveTextureARB,
    GLAlphaFunc,
    GLArrayElement,
    GLBegin,
    GLBindTexture,
    GLBlendFunc,
    GLClear,
    GLClearColor,
    GLClearDepth,
    GLColor3fv,
    GLColor3ubv,
    GLColor4f,
    GLColor4fv,
    GLColor4ub,
    GLColor4ubv,
    GLColorMask,
    GLColorPointer,
    GLColorTable,
    GLCullFace,
    GLDeleteTextures,
    GLDepthFunc,
    GLDepthMask,
    GLDepthRange,
    GLDisableClientState,
    GLDrawArrays,
    GLDrawBuffer,
    GLDrawElements,
    GLEnableClientState,
    GLEnd,
    GLFinish,
    GLFlush,
    GLFogf,
    GLFogfv,
    GLFrontFace,
    GLFrustum,
    GLGenTextures,
    GLGetBooleanv,
    GLGetError,
    GLGetFloatv,
    GLGetIntegerv,
    GLGetString,
    GLHint,
    GLIsEnabled,
    GLLoadIdentity,
    GLLoadMatrixd,
    GLLoadMatrixf,
    GLLockArrays,
    GLMatrixMode,
    GLMultMatrixd,
    GLMultMatrixf,
    GLMultiTexCoord2fARB,
    GLMultiTexCoord2fvARB,
    GLNormal3f,
    GLOrtho,
    GLPixelStorei,
    GLPointSize,
    GLPolygonMode,
    GLPopMatrix,
    GLPushMatrix,
    GLReadPixels,
    GLRotated,
    GLRotatef,
    GLRotatefEXT,
    GLRotatefEXTs,
    GLScaled,
    GLScalef,
    GLScissor,
    GLShadeModel,
    GLTexCoord2f,
    GLTexCoord2fv,
    GLTexCoord4f,
    GLTexCoord4fv,
    GLTexCoordPointer,
    GLTexEnvi,
    GLTexGeni,
    GLTexImage2D,
    GLTexParameteri,
    GLTexSubImage2D,
    GLTranslated,
    GLTranslatef,
    GLULookAt,
    GLUPerspective,
    GLUnlockArrays,
    GLVertex2fv,
    GLVertex3fv,
    GLVertex4f,
    GLVertex4fv,
    GLVertexPointer,
    GLViewport,
    MGLClearPointer,
    MGLCreateContext,
    MGLCreateContextFromID,
    MGLDeleteContext,
    MGLDrawMultitexBuffer,
    MGLEnableSync,
    MGLExit,
    PPC_MGLGetInputWindowHandle,
    MGLGetWindowHandle,
    MGLIdleFunc,
    MGLKeyFunc,
    MGLLockBack,
    MGLLockDisplay,
    MGLLockMode,
    MGLMainLoop,
    MGLMinTriArea,
    MGLMouseFunc,
    MGLPrintMatrix,
    MGLPrintMatrixStack,
    PPCStub_MGLResizeContext,
    MGLSetPointer,
    MGLSetState,
    MGLSetZOffset,
    MGLSpecialFunc,
    MGLSwitchDisplay,
    MGLTexMemStat,
    MGLUnlockDisplay,
    MGLWriteShotPPM,
    mglChooseGuardBand,
    mglChooseMtexBufferSize,
    mglChooseNumberOfBuffers,
    mglChoosePixelDepth,
    mglChooseTextureBufferSize,
    mglChooseVertexBufferSize,
    mglChooseWindowMode,
    mglGetSupportedScreenModes,
    mglProhibitAlphaFallback,
    mglProhibitMipMapping,
    mglProposeCloseDesktop,
    GLPolygonOffset,
    PPCStub_GLClientActiveTextureARB,
    GLInterleavedArrays,
    PPC_GLMultiDrawArrays,
    PPCStub_MGLEmergencyCloseScreen,
    PPCStub_MGLForceNewRenderPass,
    PPCStub_mglBeginFastPath,
    PPCStub_mglEndFastPath,
    PPCStub_mglGetFastPathCapabilities,
};

/* The only MiniGL bootstrap symbol the WarpOS client resolves by name. */
const MGLDispatchTable * __saveds MGLGetDispatchTable(void)
{
    return &PPCDispatchTable;
}

/* Hyperion DLL runtime requires these three resource exports. */
void * __saveds dllFindResource(int type, char *name)
{
    (void)type; (void)name;
    return 0;
}
void * __saveds dllLoadResource(void *resource)
{
    (void)resource;
    return 0;
}
void __saveds dllFreeResource(void *resource)
{
    (void)resource;
}

/* Export table consumed by dllstartup.c. */
dll_tExportSymbol DLL_ExportSymbols[] = {
    { (void *)dllFindResource,        (char *)"dllFindResource" },
    { (void *)dllLoadResource,        (char *)"dllLoadResource" },
    { (void *)dllFreeResource,        (char *)"dllFreeResource" },
    { (void *)MGLGetDispatchTable,    (char *)"MGLGetDispatchTable" },
    { 0, 0 }
};

/* No implicit imports are required by the MiniGL PPC DLL itself. */
dll_tImportSymbol DLL_ImportSymbols[] = {
    { 0, 0, 0, 0 }
};

int __saveds DLL_Init(void)
{
    mini_CurrentContext = (GLcontext)0;
    return MGLInit() == GL_TRUE;
}

void __saveds DLL_DeInit(void)
{
    mini_CurrentContext = (GLcontext)0;
    MGLTerm();
}
