#define MINIGL_LIBRARY_BUILD 1
#include <exec/types.h>
#include <libraries/minigl_dispatch.h>
#include "dll.h"
#include "minigl_ppc_generic.h"

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
static GLboolean PPCStub_MGLResizeContext(GLcontext context, GLsizei width, GLsizei height)
{
    (void)context;
    (void)width;
    (void)height;
    return GL_FALSE; /* as upstream 29.1: no resize took place */
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

/*
 * Upstream 29.1 slots 133..205 the Warp3D mglQ3 core does not implement.
 * Same answers as upstream's Classic table (libraryroot/library/
 * dispatch_table_classic.c), which drives the same mglQ3 lineage: state that
 * Warp3D cannot express is accepted and ignored, queries answer
 * deterministically, and calls that would have to draw or create something
 * report GL_INVALID_OPERATION. The context-free GLU/GLUT entry points and
 * glGetDoublev come from ppc_common/minigl_ppc_generic.c.
 */
static void PPCUnsupported(GLcontext context)
{
    /* Not GLFlagError: the release config compiles that macro out. */
    if (context && context->CurrentError == GL_NO_ERROR)
        context->CurrentError = GL_INVALID_OPERATION;
}

/* No blend equation on Warp3D: ADD with the colour factors on both
 * channels, which is what the core already does. */
static void PPCStub_GLBlendEquation(GLcontext context, GLenum mode) { (void)context; (void)mode; }
static void PPCStub_GLBlendFuncSeparate(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    (void)srcAlpha; (void)dstAlpha;
    GLBlendFunc(context, srcRGB, dstRGB);
}
static GLboolean PPCStub_GLIsTexture(GLcontext context, GLuint texture) { (void)context; (void)texture; return GL_FALSE; }
static void PPCStub_mglChooseZBufferDepth(int bits) { (void)bits; }
/* Lines stay one pixel wide; texgen is sphere-map only, no planes. */
static void PPCStub_GLLineWidth(GLcontext context, GLfloat width) { (void)context; (void)width; }
static void PPCStub_GLTexGenfv(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params)
{
    (void)context; (void)coord; (void)pname; (void)params;
}
/* "Resident" asks the caller to do nothing; "not resident" could send it
 * re-uploading every texture. */
static GLboolean PPCStub_GLAreTexturesResident(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences)
{
    (void)context; (void)n; (void)textures; (void)residences;
    return GL_TRUE;
}
static void PPCStub_GLEdgeFlag(GLcontext context, GLboolean flag) { (void)context; (void)flag; }
static void PPCStub_GLEdgeFlagPointer(GLcontext context, GLsizei stride, const GLvoid *pointer) { (void)context; (void)stride; (void)pointer; }
static void PPCStub_GLEdgeFlagv(GLcontext context, const GLboolean *flag) { (void)context; (void)flag; }
static void PPCStub_GLGetPointerv(GLcontext context, GLenum pname, GLvoid **params)
{
    (void)context; (void)pname;
    if (params) *params = (GLvoid *)0;
}
static void PPCStub_GLPrioritizeTextures(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    (void)context; (void)n; (void)textures; (void)priorities;
}
static void PPCStub_GLReadBuffer(GLcontext context, GLenum mode) { (void)context; (void)mode; }
/* No fixed-function lighting on Warp3D. */
static void PPCStub_GLLightf(GLcontext context, GLenum light, GLenum pname, GLfloat param) { (void)light; (void)pname; (void)param; PPCUnsupported(context); }
static void PPCStub_GLLightfv(GLcontext context, GLenum light, GLenum pname, const GLfloat *params) { (void)light; (void)pname; (void)params; PPCUnsupported(context); }
static void PPCStub_GLMaterialf(GLcontext context, GLenum face, GLenum pname, GLfloat param) { (void)face; (void)pname; (void)param; PPCUnsupported(context); }
static void PPCStub_GLMaterialfv(GLcontext context, GLenum face, GLenum pname, const GLfloat *params) { (void)face; (void)pname; (void)params; PPCUnsupported(context); }
static void PPCStub_GLLightModelf(GLcontext context, GLenum pname, GLfloat param) { (void)pname; (void)param; PPCUnsupported(context); }
static void PPCStub_GLLightModelfv(GLcontext context, GLenum pname, const GLfloat *params) { (void)pname; (void)params; PPCUnsupported(context); }
static void PPCStub_GLGetLightfv(GLcontext context, GLenum light, GLenum pname, GLfloat *params) { (void)light; (void)pname; (void)params; PPCUnsupported(context); }
static void PPCStub_GLGetMaterialfv(GLcontext context, GLenum face, GLenum pname, GLfloat *params) { (void)face; (void)pname; (void)params; PPCUnsupported(context); }
static void PPCStub_GLColorMaterial(GLcontext context, GLenum face, GLenum mode) { (void)face; (void)mode; PPCUnsupported(context); }
static void PPCStub_GLNormalPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer) { (void)type; (void)stride; (void)pointer; PPCUnsupported(context); }
/* Display lists come from the generic layer (ppc_common); it frees them
 * when the context goes away. */
static void PPC_MGLDeleteContext(GLcontext context)
{
    MGLGeneric_ListsContextGone();
    MGLDeleteContext(context);
}
/* GL_TEXTURE_ENV_MODE is the one texenv parameter Warp3D takes. */
static void PPCStub_GLTexEnvfv(GLcontext context, GLenum target, GLenum pname, const GLfloat *params)
{
    if (!context) return;
    if (!params) { PPCUnsupported(context); return; }
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) {
        if (context->CurrentError == GL_NO_ERROR) context->CurrentError = GL_INVALID_ENUM;
        return;
    }
    GLTexEnvi(context, target, pname, (GLint)params[0]);
}

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

/* Writable: display-list compilation swaps slots in it. */
static MGLDispatchTable PPCDispatchTable = {
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
    PPC_MGLDeleteContext,
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
    PPCStub_GLBlendEquation,
    PPCStub_GLBlendFuncSeparate,
    PPCStub_GLIsTexture,
    PPCStub_mglChooseZBufferDepth,
    PPCStub_GLLineWidth,
    PPCStub_GLTexGenfv,
    MGLGeneric_GLCopyTexImage2D,
    MGLGeneric_GLCopyTexSubImage2D,
    PPCStub_GLAreTexturesResident,
    PPCStub_GLEdgeFlag,
    PPCStub_GLEdgeFlagPointer,
    PPCStub_GLEdgeFlagv,
    MGLGeneric_GLGetDoublev,
    PPCStub_GLGetPointerv,
    MGLGeneric_GLIndexi,
    MGLGeneric_GLIndexiv,
    MGLGeneric_GLIndexPointer,
    PPCStub_GLPrioritizeTextures,
    PPCStub_GLReadBuffer,
    MGLCreateContextFromWindow,
    MGLCreateContextFromBitMap,
    PPCStub_GLLightf,
    PPCStub_GLLightfv,
    PPCStub_GLMaterialf,
    PPCStub_GLMaterialfv,
    PPCStub_GLLightModelf,
    PPCStub_GLLightModelfv,
    PPCStub_GLGetLightfv,
    PPCStub_GLGetMaterialfv,
    MGLGeneric_GLGenLists,
    MGLGeneric_GLDeleteLists,
    MGLGeneric_GLIsList,
    MGLGeneric_GLNewList,
    MGLGeneric_GLEndList,
    MGLGeneric_GLCallList,
    MGLGeneric_GLUBuild2DMipmaps,
    MGLGeneric_GLUErrorString,
    MGLGeneric_GLUNewQuadric,
    MGLGeneric_GLUDeleteQuadric,
    MGLGeneric_GLUQuadricNormals,
    MGLGeneric_GLUQuadricTexture,
    MGLGeneric_GLUQuadricDrawStyle,
    MGLGeneric_GLUQuadricOrientation,
    MGLGeneric_GLUCylinder,
    MGLGeneric_GLUSphere,
    MGLGeneric_GLUDisk,
    MGLGeneric_GLUTInit,
    MGLGeneric_GLUTInitDisplayMode,
    MGLGeneric_GLUTInitWindowSize,
    MGLGeneric_GLUTInitWindowPosition,
    MGLGeneric_GLUTCreateWindow,
    MGLGeneric_GLUTMainLoop,
    MGLGeneric_GLUTDisplayFunc,
    MGLGeneric_GLUTIdleFunc,
    MGLGeneric_GLUTKeyboardFunc,
    MGLGeneric_GLUTReshapeFunc,
    MGLGeneric_GLUTSwapBuffers,
    MGLGeneric_GLUTPostRedisplay,
    MGLGeneric_GLUTGet,
    MGLGeneric_GLUTGameModeString,
    MGLGeneric_GLUTEnterGameMode,
    MGLGeneric_GLUTLeaveGameMode,
    MGLGeneric_GLUTGameModeGet,
    MGLGeneric_GLUTSolidCube,
    MGLGeneric_GLUTSolidSphere,
    MGLGeneric_GLUTSolidCone,
    MGLGeneric_GLUTSolidTorus,
    MGLGeneric_GLUTSolidDodecahedron,
    PPCStub_GLNormalPointer,
    MGLGeneric_GLUQuadricCallback,
    PPCStub_GLColorMaterial,
    PPCStub_GLTexEnvfv,
    MGLGeneric_GLNormal3fv,
    PPCStub_mglBeginFastPath,
    PPCStub_mglEndFastPath,
    PPCStub_mglGetFastPathCapabilities,
};

typedef char PPCDispatchABI5SizeCheck[
    sizeof(MGLDispatchTable) == 0x354 ? 1 : -1];

/* Hooks for the backend-independent layer (ppc_common). */
MGLDispatchTable *const MGLPPCSelfTable = &PPCDispatchTable;

void MGLPPCSetError(GLcontext context, GLenum error)
{
    if (context && context->CurrentError == GL_NO_ERROR)
        context->CurrentError = error;
}

/* This DLL has no PowerOpen bridge; the table slots are the SysV calls. */
void (*const MGLPPCTexImage2D)(GLcontext, GLenum, GLint, GLint, GLsizei,
                               GLsizei, GLint, GLenum, GLenum,
                               const GLvoid *) = GLTexImage2D;
void (*const MGLPPCTexSubImage2D)(GLcontext, GLenum, GLint, GLint, GLint,
                                  GLsizei, GLsizei, GLenum, GLenum,
                                  const GLvoid *) = GLTexSubImage2D;

/* mglQ3 retargets client array pointers at its own vertex buffer when the
 * vertex-array pipeline is active, so it cannot hand back the client's
 * arrays: array draws inside a display list are refused. */
GLboolean MGLPPCClientArray(GLcontext context, GLenum array, MGLPPCArray *out)
{
    (void)context; (void)array; (void)out;
    return GL_FALSE;
}

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
