#ifndef MINIGL_PPC_GENERIC_H
#define MINIGL_PPC_GENERIC_H

/*
 * Backend-independent entry points shared by every PPC DLL variant (R200,
 * bundled mglQ3, stub). They are written only against the DLL's own dispatch
 * table, so each DLL compiles this code against its own headers and token
 * values and links its own copy.
 *
 * Each DLL provides the hooks below.
 */
#include <libraries/minigl_dispatch.h>

/* The table the DLL returns from MGLGetDispatchTable(). Writable: display
 * list compilation swaps slots in it (minigl_ppc_lists.c). */
extern MGLDispatchTable *const MGLPPCSelfTable;

/* Raises a GL error on the context, first error wins, as the backend's own
 * entry points do. */
void MGLPPCSetError(GLcontext context, GLenum error);

/* SysV entry points for glTexImage2D/glTexSubImage2D. Generic code calls
 * these, never the table slots, which a DLL may replace with PowerOpen
 * ABI-bridge trampolines. */
extern void (*const MGLPPCTexImage2D)(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
extern void (*const MGLPPCTexSubImage2D)(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);

/* One client array as the backend holds it: the client's own pointer, the
 * element size and type, and the effective stride in bytes (never 0). */
typedef struct {
    const GLvoid *pointer;
    GLint size;
    GLenum type;
    GLsizei stride;
    GLboolean enabled;
} MGLPPCArray;

/* Describes GL_VERTEX_ARRAY, GL_COLOR_ARRAY, GL_NORMAL_ARRAY, or the texture
 * coordinate array of unit GL_TEXTURE0_ARB/GL_TEXTURE1_ARB. GL_FALSE when
 * the backend cannot describe its arrays (display lists then refuse array
 * draws with GL_INVALID_OPERATION). */
GLboolean MGLPPCClientArray(GLcontext context, GLenum array, MGLPPCArray *out);

/* GL entry points with a generic implementation. */
void MGLGeneric_GLGetDoublev(GLcontext context, GLenum pname, GLdouble *params);
void MGLGeneric_GLNormal3fv(GLcontext context, GLfloat *n);
void MGLGeneric_GLCopyTexImage2D(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void MGLGeneric_GLCopyTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
/* Colour-index rendering does not exist on any PPC backend: documented
 * no-ops, as upstream Classic. */
void MGLGeneric_GLIndexi(GLcontext context, GLint c);
void MGLGeneric_GLIndexiv(GLcontext context, const GLint *c);
void MGLGeneric_GLIndexPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer);

/* Display lists (minigl_ppc_lists.c). */
GLuint MGLGeneric_GLGenLists(GLcontext context, GLsizei range);
void MGLGeneric_GLDeleteLists(GLcontext context, GLuint list, GLsizei range);
GLboolean MGLGeneric_GLIsList(GLcontext context, GLuint list);
void MGLGeneric_GLNewList(GLcontext context, GLuint list, GLenum mode);
void MGLGeneric_GLEndList(GLcontext context);
void MGLGeneric_GLCallList(GLcontext context, GLuint list);
/* Frees every list and leaves compilation; the DLL calls it when the
 * context goes away. */
void MGLGeneric_ListsContextGone(void);

/* GLU. */
GLint MGLGeneric_GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data);
const GLubyte *MGLGeneric_GLUErrorString(GLenum errCode);
GLUquadricObj *MGLGeneric_GLUNewQuadric(void);
void MGLGeneric_GLUDeleteQuadric(GLUquadricObj *q);
void MGLGeneric_GLUQuadricNormals(GLUquadricObj *q, GLenum normals);
void MGLGeneric_GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords);
void MGLGeneric_GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle);
void MGLGeneric_GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation);
void MGLGeneric_GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn);
void MGLGeneric_GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks);
void MGLGeneric_GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks);
void MGLGeneric_GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops);

/* GLUT. */
void MGLGeneric_GLUTInit(int *argcp, char **argv);
void MGLGeneric_GLUTInitDisplayMode(unsigned int mode);
void MGLGeneric_GLUTInitWindowSize(int width, int height);
void MGLGeneric_GLUTInitWindowPosition(int x, int y);
int MGLGeneric_GLUTCreateWindow(const char *title);
void MGLGeneric_GLUTMainLoop(void);
void MGLGeneric_GLUTDisplayFunc(void (*func)(void));
void MGLGeneric_GLUTIdleFunc(void (*func)(void));
void MGLGeneric_GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y));
void MGLGeneric_GLUTReshapeFunc(void (*func)(int width, int height));
void MGLGeneric_GLUTSwapBuffers(void);
void MGLGeneric_GLUTPostRedisplay(void);
int MGLGeneric_GLUTGet(GLenum state);
void MGLGeneric_GLUTGameModeString(const char *string);
int MGLGeneric_GLUTEnterGameMode(void);
void MGLGeneric_GLUTLeaveGameMode(void);
int MGLGeneric_GLUTGameModeGet(GLenum query);
void MGLGeneric_GLUTSolidCube(GLdouble size);
void MGLGeneric_GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks);
void MGLGeneric_GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks);
void MGLGeneric_GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings);
void MGLGeneric_GLUTSolidDodecahedron(void);

#endif
