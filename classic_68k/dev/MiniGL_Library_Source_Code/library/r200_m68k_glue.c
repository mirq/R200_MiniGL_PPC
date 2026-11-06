/*
 * 68k platform glue for minigl68k.library: the PPC-validated R200 frontend
 * (backend_r200/library, R200_PPC_FRONTEND=0) running natively on the 68k,
 * drawing through the CP emitter (CpSubmitSlot in r200_submit.c).
 *
 * Provides the hooks the backend-independent layer (ppc_common: display
 * lists, GLU, generic helpers) expects from a platform, and GLUT stubs.
 */
#include <exec/types.h>
#include <libraries/minigl_dispatch.h>
#include "minigl_ppc_generic.h"

extern MGLDispatchTable MiniGLDispatchTable;
GLboolean R200PPCGetClientArray(GLenum array, const GLvoid **pointer,
                                GLint *size, GLenum *type, GLsizei *stride,
                                GLboolean *enabled);
void R200PPCSetError(GLenum error);

MGLDispatchTable *const MGLPPCSelfTable = &MiniGLDispatchTable;

void MGLPPCSetError(GLcontext context, GLenum error)
{
    (void)context;
    R200PPCSetError(error);
}

static void M68KTexImage2D(GLcontext c, GLenum target, GLint level,
                           GLint internalformat, GLsizei width, GLsizei height,
                           GLint border, GLenum format, GLenum type,
                           const GLvoid *pixels)
{
    MiniGLDispatchTable.GLTexImage2D(c, target, level, internalformat, width,
                                     height, border, format, type, pixels);
}

static void M68KTexSubImage2D(GLcontext c, GLenum target, GLint level,
                              GLint xoffset, GLint yoffset, GLsizei width,
                              GLsizei height, GLenum format, GLenum type,
                              const GLvoid *pixels)
{
    MiniGLDispatchTable.GLTexSubImage2D(c, target, level, xoffset, yoffset,
                                        width, height, format, type, pixels);
}

void (*const MGLPPCTexImage2D)(GLcontext, GLenum, GLint, GLint, GLsizei,
                               GLsizei, GLint, GLenum, GLenum,
                               const GLvoid *) = M68KTexImage2D;
void (*const MGLPPCTexSubImage2D)(GLcontext, GLenum, GLint, GLint, GLint,
                                  GLsizei, GLsizei, GLenum, GLenum,
                                  const GLvoid *) = M68KTexSubImage2D;

GLboolean MGLPPCClientArray(GLcontext context, GLenum array, MGLPPCArray *out)
{
    (void)context;
    return R200PPCGetClientArray(array, &out->pointer, &out->size, &out->type,
                                 &out->stride, &out->enabled);
}

/* GLUT is not provided by the 68k library. */
void MGLGeneric_GLUTInit(int *argcp, char **argv) { (void)argcp; (void)argv; }
void MGLGeneric_GLUTInitDisplayMode(unsigned int mode) { (void)mode; }
void MGLGeneric_GLUTInitWindowSize(int w, int h) { (void)w; (void)h; }
void MGLGeneric_GLUTInitWindowPosition(int x, int y) { (void)x; (void)y; }
int MGLGeneric_GLUTCreateWindow(const char *t) { (void)t; return 0; }
void MGLGeneric_GLUTMainLoop(void) {}
void MGLGeneric_GLUTDisplayFunc(void (*f)(void)) { (void)f; }
void MGLGeneric_GLUTIdleFunc(void (*f)(void)) { (void)f; }
void MGLGeneric_GLUTKeyboardFunc(void (*f)(unsigned char, int, int)) { (void)f; }
void MGLGeneric_GLUTReshapeFunc(void (*f)(int, int)) { (void)f; }
void MGLGeneric_GLUTSwapBuffers(void) {}
void MGLGeneric_GLUTPostRedisplay(void) {}
int MGLGeneric_GLUTGet(GLenum s) { (void)s; return 0; }
void MGLGeneric_GLUTGameModeString(const char *s) { (void)s; }
int MGLGeneric_GLUTEnterGameMode(void) { return 0; }
void MGLGeneric_GLUTLeaveGameMode(void) {}
int MGLGeneric_GLUTGameModeGet(GLenum q) { (void)q; return 0; }
void MGLGeneric_GLUTSolidCube(GLdouble s) { (void)s; }
void MGLGeneric_GLUTSolidSphere(GLdouble r, GLint a, GLint b) { (void)r; (void)a; (void)b; }
void MGLGeneric_GLUTSolidCone(GLdouble b, GLdouble h, GLint s, GLint t) { (void)b; (void)h; (void)s; (void)t; }
void MGLGeneric_GLUTSolidTorus(GLdouble i, GLdouble o, GLint s, GLint r) { (void)i; (void)o; (void)s; (void)r; }
void MGLGeneric_GLUTSolidDodecahedron(void) {}
