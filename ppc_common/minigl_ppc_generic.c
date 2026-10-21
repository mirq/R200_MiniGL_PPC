/*
 * Backend-independent PPC entry points; see minigl_ppc_generic.h.
 *
 * glGetDoublev, glNormal3fv, gluErrorString and the framebuffer copies (for
 * backends without their own) live here; the colour-index calls are
 * permanent no-ops. Display lists, GLU and GLUT have files of their own
 * (minigl_ppc_lists.c, minigl_ppc_glu.c, minigl_ppc_glut.c).
 */
#define MINIGL_LIBRARY_BUILD 1
#include "minigl_ppc_generic.h"

#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <exec/memory.h>
#include <proto/exec.h>
#ifdef WARPUP
#pragma pack(pop)
#endif

/* How many values glGet* writes for pname. Unknown names report one value,
 * so a pname this table does not know can never write past a one-element
 * buffer. */
static int GetValueCount(GLenum pname)
{
    switch (pname) {
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
        return 16;
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
    case GL_COLOR_CLEAR_VALUE:
    case GL_CURRENT_COLOR:
    case GL_FOG_COLOR:
    case GL_CURRENT_TEXTURE_COORDS:
        return 4;
    case GL_DEPTH_RANGE:
    case GL_MAX_VIEWPORT_DIMS:
    case GL_POLYGON_MODE:
        return 2;
    default:
        return 1;
    }
}

void MGLGeneric_GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params)
{
    GLfloat values[16];
    int count, i;

    if (!params)
        return;
    count = GetValueCount(pname);
    for (i = 0; i < 16; ++i)
        values[i] = 0.0f;
    MGLPPCSelfTable->GLGetFloatv(context, pname, values);
    for (i = 0; i < count; ++i)
        params[i] = (GLdouble)values[i];
}

void MGLGeneric_GLNormal3fv(GLcontext context, GLfloat *n)
{
    /* NULL leaves the current normal untouched, as upstream 29.1. */
    if (n)
        MGLPPCSelfTable->GLNormal3f(context, n[0], n[1], n[2]);
}

/*
 * glCopyTexImage2D / glCopyTexSubImage2D for backends that have no copy of
 * their own: glReadPixels of the source rectangle as RGBA8, then the normal
 * texture upload. Pack and unpack alignment are forced to 4 for the
 * duration (GL defines the copies in terms of the framebuffer, not of pixel
 * store state) and restored; a backend that also honours row length or skip
 * parameters must provide its own copy instead (the R200 frontend does).
 * border is never read: GL requires 0, and a PowerOpen client passes this
 * ninth argument where this SysV code does not look.
 */
static GLubyte *GenericReadRect(GLcontext context, GLint x, GLint y,
                                GLsizei width, GLsizei height)
{
    GLubyte *pixels;
    ULONG bytes;
    if ((ULONG)height > 0x7fffffffUL / ((ULONG)width * 4UL)) {
        MGLPPCSetError(context, GL_INVALID_VALUE);
        return (GLubyte *)0;
    }
    bytes = (ULONG)width * 4UL * (ULONG)height;
    pixels = (GLubyte *)AllocVec(bytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!pixels) {
        MGLPPCSetError(context, GL_OUT_OF_MEMORY);
        return (GLubyte *)0;
    }
    MGLPPCSelfTable->GLReadPixels(context, x, y, width, height, GL_RGBA,
                                  GL_UNSIGNED_BYTE, pixels);
    return pixels;
}

static void GenericAlign(GLcontext context, GLint *pack, GLint *unpack)
{
    MGLPPCSelfTable->GLGetIntegerv(context, GL_PACK_ALIGNMENT, pack);
    MGLPPCSelfTable->GLGetIntegerv(context, GL_UNPACK_ALIGNMENT, unpack);
    MGLPPCSelfTable->GLPixelStorei(context, GL_PACK_ALIGNMENT, 4);
    MGLPPCSelfTable->GLPixelStorei(context, GL_UNPACK_ALIGNMENT, 4);
}

static void GenericRestore(GLcontext context, GLint pack, GLint unpack)
{
    MGLPPCSelfTable->GLPixelStorei(context, GL_PACK_ALIGNMENT, pack);
    MGLPPCSelfTable->GLPixelStorei(context, GL_UNPACK_ALIGNMENT, unpack);
}

void MGLGeneric_GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    GLint pack = 4, unpack = 4;
    GLubyte *pixels;
    (void)border;
    if (width <= 0 || height <= 0) {
        if (width < 0 || height < 0) MGLPPCSetError(context, GL_INVALID_VALUE);
        return;
    }
    GenericAlign(context, &pack, &unpack);
    pixels = GenericReadRect(context, x, y, width, height);
    if (pixels) {
        MGLPPCTexImage2D(context, target, level, (GLint)internalformat, width,
                         height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        FreeVec(pixels);
    }
    GenericRestore(context, pack, unpack);
}

void MGLGeneric_GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    GLint pack = 4, unpack = 4;
    GLubyte *pixels;
    if (width <= 0 || height <= 0) {
        if (width < 0 || height < 0) MGLPPCSetError(context, GL_INVALID_VALUE);
        return;
    }
    GenericAlign(context, &pack, &unpack);
    pixels = GenericReadRect(context, x, y, width, height);
    if (pixels) {
        MGLPPCTexSubImage2D(context, target, level, xoffset, yoffset, width,
                            height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        FreeVec(pixels);
    }
    GenericRestore(context, pack, unpack);
}

void MGLGeneric_GLIndexi(GLcontext context, GLint c) { (void)context; (void)c; }
void MGLGeneric_GLIndexiv(GLcontext context, const GLint *c) { (void)context; (void)c; }
void MGLGeneric_GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    (void)context; (void)type; (void)stride; (void)pointer;
}

/* GLU error text needs no renderer. Same strings as upstream 29.1. */
const GLubyte *MGLGeneric_GLUErrorString(GLenum errCode)
{
    switch (errCode) {
    case GL_NO_ERROR: return (const GLubyte *)"no error";
    case GL_INVALID_ENUM: case GLU_INVALID_ENUM: return (const GLubyte *)"invalid enum";
    case GL_INVALID_VALUE: case GLU_INVALID_VALUE: return (const GLubyte *)"invalid value";
    case GL_INVALID_OPERATION: case GLU_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
    case GL_OUT_OF_MEMORY: case GLU_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
    case GL_STACK_OVERFLOW: return (const GLubyte *)"stack overflow";
    case GL_STACK_UNDERFLOW: return (const GLubyte *)"stack underflow";
    case GLU_INCOMPATIBLE_GL_VERSION: return (const GLubyte *)"incompatible GL version";
    default: return (const GLubyte *)0;
    }
}
