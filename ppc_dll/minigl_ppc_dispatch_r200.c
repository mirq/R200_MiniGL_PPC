#define MINIGL_LIBRARY_BUILD 1

#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <stddef.h>
#include <exec/types.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#ifdef WARPUP
#pragma pack(pop)
#endif

#include <libraries/minigl_dispatch.h>
#include "dll.h"
#include "minigl_ppc_generic.h"

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
extern void R200PPCSetError(GLenum error);
extern GLboolean R200PPCGetClientArray(GLenum array, const GLvoid **pointer,
                                       GLint *size, GLenum *type,
                                       GLsizei *stride, GLboolean *enabled);

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
    MGLGeneric_ListsContextGone();
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

/* GLTexImage2D/glTexSubImage2D are the only table entries with more than
 * eight arguments; PowerOpen (vbcc/WarpOS) clients pass the overflow
 * arguments at different stack offsets than the SysV ABI this DLL is
 * built with. Route both slots through the ABI-bridging trampolines in
 * backend_r200/client/r200_poweropen_bridge.s. */
extern void R200PowerTexImage2D(void);
extern void R200PowerTexSubImage2D(void);
#define R200_TABLE_GLTexImage2D R200PowerTexImage2D
#define R200_TABLE_GLTexSubImage2D R200PowerTexSubImage2D

/* The exported object is upstream 29.1's table (840 bytes, as MiniGLWOS's)
 * followed by the three FastPath slots that the pre-alignment R200 header
 * had at its end. Its structSize, 852, is what a client built on that header
 * checks for (MiniGLOpen refuses a table smaller than its own), so those
 * binaries keep loading this DLL; a client built on the current header only
 * needs 840. The slots report FastPath as unavailable, and the one client
 * that used it (GLQuake's r_worldbatch, off by default) then draws through
 * immediate mode. */
static GLboolean R200Legacy_mglBeginFastPath(GLenum primitive, ULONG attributes, void *fp)
{
    (void)primitive; (void)attributes; (void)fp;
    return GL_FALSE;
}
static void R200Legacy_mglEndFastPath(void *fp)
{
    (void)fp;
}
static ULONG R200Legacy_mglGetFastPathCapabilities(void)
{
    return 0;
}

struct R200LegacyTail {
    GLboolean (*mglBeginFastPath)(GLenum primitive, ULONG attributes, void *fp);
    void (*mglEndFastPath)(void *fp);
    ULONG (*mglGetFastPathCapabilities)(void);
};

struct R200DispatchExport {
    MGLDispatchTable table;
    struct R200LegacyTail legacy;
};

#define R200_DISPATCH_OBJECT_BEGIN struct R200DispatchExport R200DispatchExport = { {
#define R200_DISPATCH_OBJECT_END }, { R200Legacy_mglBeginFastPath, \
    R200Legacy_mglEndFastPath, R200Legacy_mglGetFastPathCapabilities } };
#define R200_DISPATCH_STRUCT_SIZE ((ULONG)sizeof(struct R200DispatchExport))
#define MiniGLDispatchTable (R200DispatchExport.table)

#include "../backend_r200/generated/dispatch_table_abi5.c"

/* ABI 5: upstream 29.1's table, 840 bytes, as MiniGLWOS's; the exported
 * object with the legacy tail is 852 (0x354), as the old header's table. */
typedef char R200DispatchABI5SizeCheck[
    sizeof(MGLDispatchTable) == 0x348 ? 1 : -1];
typedef char R200DispatchExportSizeCheck[
    sizeof(struct R200DispatchExport) == 0x354 &&
    offsetof(struct R200DispatchExport, legacy) == 0x348 ? 1 : -1];

/* Hooks for the backend-independent layer (ppc_common). */
MGLDispatchTable *const MGLPPCSelfTable = &MiniGLDispatchTable;

void MGLPPCSetError(GLcontext context, GLenum error)
{
    (void)context;
    R200PPCSetError(error);
}

/* The generated SysV thunks; the table slots are the PowerOpen bridges. */
void (*const MGLPPCTexImage2D)(GLcontext, GLenum, GLint, GLint, GLsizei,
                               GLsizei, GLint, GLenum, GLenum,
                               const GLvoid *) = R200_GLTexImage2D;
void (*const MGLPPCTexSubImage2D)(GLcontext, GLenum, GLint, GLint, GLint,
                                  GLsizei, GLsizei, GLenum, GLenum,
                                  const GLvoid *) = R200_GLTexSubImage2D;

GLboolean MGLPPCClientArray(GLcontext context, GLenum array, MGLPPCArray *out)
{
    (void)context;
    return R200PPCGetClientArray(array, &out->pointer, &out->size, &out->type,
                                 &out->stride, &out->enabled);
}

/* PowerOpen (vbcc) client ABI bridge -- see MGLTexImage2DArgs in
 * include/libraries/minigl_dispatch.h. PowerOpen places arguments 9+
 * at different stack offsets than the SysV ABI this DLL is built with,
 * so a vbcc client cannot call the ten-argument table entries
 * directly. These two helpers take only register arguments (context +
 * one pointer), which both ABIs pass identically, and forward to the
 * normal frontend entry points. Exported by name; clients fetch them
 * with dllGetProcAddress(). */
void __saveds MGLTexImage2DBlock(GLcontext context,
                                 const MGLTexImage2DArgs *args)
{
    (void)context;
    Lib_glTexImage2D(args->target, args->level, args->internalformat,
                     args->width, args->height, args->border,
                     args->format, args->type, args->pixels);
}

void __saveds MGLTexSubImage2DBlock(GLcontext context,
                                    const MGLTexSubImage2DArgs *args)
{
    (void)context;
    Lib_glTexSubImage2D(args->target, args->level, args->xoffset,
                        args->yoffset, args->width, args->height,
                        args->format, args->type, args->pixels);
}

void R200PPCDispatchExit(void)
{
    R200PPCTraceStr("dispatch exit enter");
    R200ABI3DeleteContext();
    mini_CurrentContext = (GLcontext)0;
    R200PPCTraceStr("dispatch exit done");
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
    MGLGeneric_ListsContextGone();
    R200PPCPlatformClose();
    mini_CurrentContext = (GLcontext)0;
    R200ClientOwner = NULL;
    R200LegacyClient = FALSE;
}

/* Import archives without MGLPPCClientAttach (the pre-attach "legacy" clients)
 * know only this export. Their normal context delete path releases the
 * transport while still executing in this task. */
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
    { (void *)MGLTexImage2DBlock,    (char *)"MGLTexImage2DBlock" },
    { (void *)MGLTexSubImage2DBlock, (char *)"MGLTexSubImage2DBlock" },
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
