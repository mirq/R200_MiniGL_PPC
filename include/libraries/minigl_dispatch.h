#ifndef LIBRARIES_MINIGL_DISPATCH_H
#define LIBRARIES_MINIGL_DISPATCH_H

#include <exec/types.h>

/* Import MiniGL types/prototypes but suppress its normal static API macros. */
#ifndef MINIGL_DISPATCH_DECLARATIONS
#define MINIGL_DISPATCH_DECLARATIONS
#define MINIGL_DIRECT_RESTORE_DISPATCH_DECLARATIONS
#endif
#ifndef USE_MGLAPI
#define USE_MGLAPI
#define MINIGL_DIRECT_RESTORE_USE_MGLAPI
#endif
#include <mgl/gl.h>
#include <libraries/minigl_fastpath.h>
#ifdef MINIGL_DIRECT_RESTORE_USE_MGLAPI
#undef USE_MGLAPI
#undef MINIGL_DIRECT_RESTORE_USE_MGLAPI
#endif
#ifdef MINIGL_DIRECT_RESTORE_DISPATCH_DECLARATIONS
#undef MINIGL_DISPATCH_DECLARATIONS
#undef MINIGL_DIRECT_RESTORE_DISPATCH_DECLARATIONS
#endif

#define MINIGL_DISPATCH_ABI_VERSION 3UL
#define MINIGL_BACKEND_FLAG_STUB      (1UL << 0)
#define MINIGL_BACKEND_FLAG_CLASSIC   (1UL << 1)
#define MINIGL_BACKEND_FLAG_PISTORM3D (1UL << 2)
#define MINIGL_BACKEND_FLAG_PPC       (1UL << 3)
/* ABI 3 assigned bit 3 to the PPC implementation. The direct R200 semantic
 * frontend is that PPC implementation, so keep the bit and expose its name. */
#define MINIGL_BACKEND_FLAG_R200      MINIGL_BACKEND_FLAG_PPC

typedef struct MGLDispatchTable {
    ULONG abiVersion;
    ULONG structSize;
    ULONG backendFlags;
    ULONG reserved;
    GLcontext *currentContext;
    void (*GLActiveTextureARB)(GLcontext context, GLenum unit);
    void (*GLAlphaFunc)(GLcontext context, GLenum func, GLclampf ref);
    void (*GLArrayElement)(GLcontext context, GLint i);
    void (*GLBegin)(GLcontext context, GLenum mode);
    void (*GLBindTexture)(GLcontext context, GLenum target, GLuint texture);
    void (*GLBlendFunc)(GLcontext context, GLenum sfactor, GLenum dfactor);
    void (*GLClear)(GLcontext context, GLbitfield mask);
    void (*GLClearColor)(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
    void (*GLClearDepth)(GLcontext context, GLclampd depth);
    void (*GLColor3fv)(GLcontext context, GLfloat *v);
    void (*GLColor3ubv)(GLcontext context, GLubyte *v);
    void (*GLColor4f)(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
    void (*GLColor4fv)(GLcontext context, GLfloat *v);
    void (*GLColor4ub)(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa);
    void (*GLColor4ubv)(GLcontext context, GLubyte *v);
    void (*GLColorMask)(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
    void (*GLColorPointer)(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    void (*GLColorTable)(GLcontext context, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data);
    void (*GLCullFace)(GLcontext context, GLenum mode);
    void (*GLDeleteTextures)(GLcontext context, GLsizei n, const GLuint *textures);
    void (*GLDepthFunc)(GLcontext context, GLenum func);
    void (*GLDepthMask)(GLcontext context, GLboolean flag);
    void (*GLDepthRange)(GLcontext context, GLclampd n, GLclampd f);
    void (*GLDisableClientState)(GLcontext context, GLenum cap);
    void (*GLDrawArrays)(GLcontext context, GLenum mode, GLint first, GLsizei count);
    void (*GLDrawBuffer)(GLcontext context, GLenum mode);
    void (*GLDrawElements)(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
    void (*GLEnableClientState)(GLcontext context, GLenum cap);
    void (*GLEnd)(GLcontext context);
    void (*GLFinish)(GLcontext context);
    void (*GLFlush)(GLcontext context);
    void (*GLFogf)(GLcontext context, GLenum pname, GLfloat param);
    void (*GLFogfv)(GLcontext context, GLenum pname, GLfloat *param);
    void (*GLFrontFace)(GLcontext context, GLenum mode);
    void (*GLFrustum)(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
    void (*GLGenTextures)(GLcontext context, GLsizei n, GLuint *textures);
    void (*GLGetBooleanv)(GLcontext context, GLenum pname, GLboolean *params);
    GLenum (*GLGetError)(GLcontext context);
    void (*GLGetFloatv)(GLcontext context, GLenum pname, GLfloat *params);
    void (*GLGetIntegerv)(GLcontext context, GLenum pname, GLint *params);
    const GLubyte* (*GLGetString)(GLcontext context, GLenum name);
    void (*GLHint)(GLcontext context, GLenum target, GLenum mode);
    GLboolean (*GLIsEnabled)(GLcontext context, GLenum cap);
    void (*GLLoadIdentity)(GLcontext context);
    void (*GLLoadMatrixd)(GLcontext context, const GLdouble *m);
    void (*GLLoadMatrixf)(GLcontext context, const GLfloat *m);
    void (*GLLockArrays)(GLcontext context, GLuint first, GLsizei count);
    void (*GLMatrixMode)(GLcontext context, GLenum mode);
    void (*GLMultMatrixd)(GLcontext context, const GLdouble *m);
    void (*GLMultMatrixf)(GLcontext context, const GLfloat *m);
    void (*GLMultiTexCoord2fARB)(GLcontext context, GLenum unit, GLfloat s, GLfloat t);
    void (*GLMultiTexCoord2fvARB)(GLcontext context, GLenum unit, GLfloat *v);
    void (*GLNormal3f)(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
    void (*GLOrtho)(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
    void (*GLPixelStorei)(GLcontext context, GLenum pname, GLint param);
    void (*GLPointSize)(GLcontext context, GLfloat size);
    void (*GLPolygonMode)(GLcontext context, GLenum face, GLenum mode);
    void (*GLPopMatrix)(GLcontext context);
    void (*GLPushMatrix)(GLcontext context);
    void (*GLReadPixels)(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
    void (*GLRotated)(GLcontext context, GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
    void (*GLRotatef)(GLcontext context, GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
    void (*GLRotatefEXT)(GLcontext context, GLfloat angle, const GLint xyz);
    void (*GLRotatefEXTs)(GLcontext context, GLfloat sin_an, GLfloat cos_an, const GLint xyz);
    void (*GLScaled)(GLcontext context, GLdouble x, GLdouble y, GLdouble z);
    void (*GLScalef)(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
    void (*GLScissor)(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height);
    void (*GLShadeModel)(GLcontext context, GLenum mode);
    void (*GLTexCoord2f)(GLcontext context, GLfloat s, GLfloat t);
    void (*GLTexCoord2fv)(GLcontext context, GLfloat *v);
    void (*GLTexCoord4f)(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
    void (*GLTexCoord4fv)(GLcontext context, GLfloat *v);
    void (*GLTexCoordPointer)(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    void (*GLTexEnvi)(GLcontext context, GLenum target, GLenum pname, GLint param);
    void (*GLTexGeni)(GLcontext context, GLenum coord, GLenum mode, GLenum map);
    void (*GLTexImage2D)(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
    void (*GLTexParameteri)(GLcontext context, GLenum target, GLenum pname, GLint param);
    void (*GLTexSubImage2D)(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
    void (*GLTranslated)(GLcontext context, GLdouble x, GLdouble y, GLdouble z);
    void (*GLTranslatef)(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
    void (*GLULookAt)(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz);
    void (*GLUPerspective)(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar);
    void (*GLUnlockArrays)(GLcontext context);
    void (*GLVertex2fv)(GLcontext context, GLfloat *v);
    void (*GLVertex3fv)(GLcontext context, GLfloat *v);
    void (*GLVertex4f)(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
    void (*GLVertex4fv)(GLcontext context, GLfloat *v);
    void (*GLVertexPointer)(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    void (*GLViewport)(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height);
    void (*MGLClearPointer)(GLcontext context);
    void * (*MGLCreateContext)(int offx, int offy, int w, int h);
    void * (*MGLCreateContextFromID)(GLint ID, GLint *w, GLint *h);
    void (*MGLDeleteContext)(GLcontext context);
    void (*MGLDrawMultitexBuffer)(GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv);
    void (*MGLEnableSync)(GLcontext context, GLboolean enable);
    void (*MGLExit)(GLcontext context);
    void * (*MGLGetInputWindowHandle)(GLcontext context);
    void * (*MGLGetWindowHandle)(GLcontext context);
    void (*MGLIdleFunc)(GLcontext context, IdleFn i);
    void (*MGLKeyFunc)(GLcontext context, KeyHandlerFn k);
    GLboolean (*MGLLockBack)(GLcontext context, MGLLockInfo *info);
    GLboolean (*MGLLockDisplay)(GLcontext context);
    void (*MGLLockMode)(GLcontext context, GLenum lockMode);
    void (*MGLMainLoop)(GLcontext context);
    void (*MGLMinTriArea)(GLcontext context, GLfloat area);
    void (*MGLMouseFunc)(GLcontext context, MouseHandlerFn m);
    void (*MGLPrintMatrix)(GLcontext context, int mode);
    void (*MGLPrintMatrixStack)(GLcontext context, int mode);
    void (*MGLResizeContext)(GLcontext context, GLsizei width, GLsizei height);
    void (*MGLSetPointer)(GLcontext context);
    void (*MGLSetState)(GLcontext context, GLenum cap, GLboolean state);
    void (*MGLSetZOffset)(GLcontext context, GLfloat offset);
    void (*MGLSpecialFunc)(GLcontext context, SpecialHandlerFn s);
    void (*MGLSwitchDisplay)(GLcontext context);
    void (*MGLTexMemStat)(GLcontext context, GLint *Current, GLint *Peak);
    void (*MGLUnlockDisplay)(GLcontext context);
    void (*MGLWriteShotPPM)(GLcontext context, char *filename);
    void (*mglChooseGuardBand)(GLboolean flag);
    void (*mglChooseMtexBufferSize)(int size);
    void (*mglChooseNumberOfBuffers)(int number);
    void (*mglChoosePixelDepth)(int depth);
    void (*mglChooseTextureBufferSize)(int size);
    void (*mglChooseVertexBufferSize)(int size);
    void (*mglChooseWindowMode)(GLboolean flag);
    GLint (*mglGetSupportedScreenModes)(MGLScreenModeCallback CallbackFn);
    void (*mglProhibitAlphaFallback)(GLboolean flag);
    void (*mglProhibitMipMapping)(GLboolean flag);
    void (*mglProposeCloseDesktop)(GLboolean closeme);
    void (*GLPolygonOffset)(GLcontext context, GLfloat factor, GLfloat units);

    /* V10 compatibility boundary: every field above is the released WarpOS
     * V10 prefix. V19 only appends fields below this point. */
    /* Appended API additions -- existing dispatch field offsets stay stable. */
    void (*GLClientActiveTextureARB)(GLcontext context, GLenum unit);
    void (*GLInterleavedArrays)(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer);
    void (*GLMultiDrawArrays)(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);
    void (*MGLEmergencyCloseScreen)(GLcontext context);
    void (*MGLForceNewRenderPass)(GLcontext context);

    /* FastPath direct vertex-buffer submission (V19; see
     * libraries/minigl_fastpath.h). These entry points keep the DLL's
     * implicit-current-context model: no GLcontext argument. */
    GLboolean (*mglBeginFastPath)(GLenum primitive, ULONG attributes, MGLFastPath *fp);
    void (*mglEndFastPath)(MGLFastPath *fp);
    ULONG (*mglGetFastPathCapabilities)(void);
} MGLDispatchTable;

extern const MGLDispatchTable *MiniGLDispatch;

/* Block-argument texture uploads (POWEROPEN client ABI bridge).
 *
 * vbcc/WarpOS clients use the PowerOpen ABI, which places arguments 9+
 * at different stack offsets than the SysV ABI used by the GCC-built
 * DLL. glTexImage2D/glTexSubImage2D are the only table entries with
 * more than eight parameters (context + 9), so PowerOpen clients
 * marshal them through these block helpers (context + one pointer,
 * register-only, identical in both ABIs). The DLL exports them by name
 * (MGLTexImage2DBlock / MGLTexSubImage2DBlock); clients fetch the
 * addresses with dllGetProcAddress() and fall back to the ordinary
 * table entry when the DLL does not provide them. */
typedef struct
{
    GLenum target;
    GLint level;
    GLint internalformat;
    GLsizei width;
    GLsizei height;
    GLint border;
    GLenum format;
    GLenum type;
    const GLvoid *pixels;
} MGLTexImage2DArgs;

typedef struct
{
    GLenum target;
    GLint level;
    GLint xoffset;
    GLint yoffset;
    GLsizei width;
    GLsizei height;
    GLenum format;
    GLenum type;
    const GLvoid *pixels;
} MGLTexSubImage2DArgs;

#ifndef MINIGL_LIBRARY_BUILD
#define MGLD_CTX (*MiniGLDispatch->currentContext)

static __inline__ void MGLD_glActiveTextureARB(GLenum unit)
{
    MiniGLDispatch->GLActiveTextureARB(MGLD_CTX, unit);
}
#define glActiveTextureARB MGLD_glActiveTextureARB

static __inline__ void MGLD_glClientActiveTextureARB(GLenum unit)
{
    MiniGLDispatch->GLClientActiveTextureARB(MGLD_CTX, unit);
}
#define glClientActiveTextureARB MGLD_glClientActiveTextureARB

static __inline__ void MGLD_glAlphaFunc(GLenum func, GLclampf ref)
{
    MiniGLDispatch->GLAlphaFunc(MGLD_CTX, func, ref);
}
#define glAlphaFunc MGLD_glAlphaFunc

static __inline__ void MGLD_glArrayElement(GLint i)
{
    MiniGLDispatch->GLArrayElement(MGLD_CTX, i);
}
#define glArrayElement MGLD_glArrayElement

static __inline__ void MGLD_glBegin(GLenum mode)
{
    MiniGLDispatch->GLBegin(MGLD_CTX, mode);
}
#define glBegin MGLD_glBegin

static __inline__ void MGLD_glBindTexture(GLenum target, GLuint texture)
{
    MiniGLDispatch->GLBindTexture(MGLD_CTX, target, texture);
}
#define glBindTexture MGLD_glBindTexture

static __inline__ void MGLD_glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    MiniGLDispatch->GLBlendFunc(MGLD_CTX, sfactor, dfactor);
}
#define glBlendFunc MGLD_glBlendFunc

static __inline__ void MGLD_glClear(GLbitfield mask)
{
    MiniGLDispatch->GLClear(MGLD_CTX, mask);
}
#define glClear MGLD_glClear

static __inline__ void MGLD_glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    MiniGLDispatch->GLClearColor(MGLD_CTX, red, green, blue, alpha);
}
#define glClearColor MGLD_glClearColor

static __inline__ void MGLD_glClearDepth(GLclampd depth)
{
    MiniGLDispatch->GLClearDepth(MGLD_CTX, depth);
}
#define glClearDepth MGLD_glClearDepth

static __inline__ void MGLD_glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    MiniGLDispatch->GLColor4f(MGLD_CTX, red, green, blue, 1.0f);
}
#define glColor3f MGLD_glColor3f

static __inline__ void MGLD_glColor3fv(GLfloat *v)
{
    MiniGLDispatch->GLColor3fv(MGLD_CTX, v);
}
#define glColor3fv MGLD_glColor3fv

static __inline__ void MGLD_glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    MiniGLDispatch->GLColor4ub(MGLD_CTX, red, green, blue, 255);
}
#define glColor3ub MGLD_glColor3ub

static __inline__ void MGLD_glColor3ubv(GLubyte *v)
{
    MiniGLDispatch->GLColor3ubv(MGLD_CTX, v);
}
#define glColor3ubv MGLD_glColor3ubv

static __inline__ void MGLD_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    MiniGLDispatch->GLColor4f(MGLD_CTX, red, green, blue, alpha);
}
#define glColor4f MGLD_glColor4f

static __inline__ void MGLD_glColor4fv(GLfloat *v)
{
    MiniGLDispatch->GLColor4fv(MGLD_CTX, v);
}
#define glColor4fv MGLD_glColor4fv

static __inline__ void MGLD_glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    MiniGLDispatch->GLColor4ub(MGLD_CTX, red, green, blue, alpha);
}
#define glColor4ub MGLD_glColor4ub

static __inline__ void MGLD_glColor4ubv(GLubyte *v)
{
    MiniGLDispatch->GLColor4ubv(MGLD_CTX, v);
}
#define glColor4ubv MGLD_glColor4ubv

static __inline__ void MGLD_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    MiniGLDispatch->GLColorMask(MGLD_CTX, red, green, blue, alpha);
}
#define glColorMask MGLD_glColorMask

static __inline__ void MGLD_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLColorPointer(MGLD_CTX, size, type, stride, pointer);
}
#define glColorPointer MGLD_glColorPointer

static __inline__ void MGLD_glColorTable(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
    MiniGLDispatch->GLColorTable(MGLD_CTX, target, internalformat, width, format, type, data);
}
#define glColorTable MGLD_glColorTable

static __inline__ void MGLD_glColorTableEXT(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
    MiniGLDispatch->GLColorTable(MGLD_CTX, target, internalformat, width, format, type, data);
}
#define glColorTableEXT MGLD_glColorTableEXT

static __inline__ void MGLD_glCullFace(GLenum mode)
{
    MiniGLDispatch->GLCullFace(MGLD_CTX, mode);
}
#define glCullFace MGLD_glCullFace

static __inline__ void MGLD_glDeleteTextures(GLsizei n, const GLuint *textures)
{
    MiniGLDispatch->GLDeleteTextures(MGLD_CTX, n, textures);
}
#define glDeleteTextures MGLD_glDeleteTextures

static __inline__ void MGLD_glDepthFunc(GLenum func)
{
    MiniGLDispatch->GLDepthFunc(MGLD_CTX, func);
}
#define glDepthFunc MGLD_glDepthFunc

static __inline__ void MGLD_glDepthMask(GLboolean flag)
{
    MiniGLDispatch->GLDepthMask(MGLD_CTX, flag);
}
#define glDepthMask MGLD_glDepthMask

static __inline__ void MGLD_glDepthRange(GLclampd n, GLclampd f)
{
    MiniGLDispatch->GLDepthRange(MGLD_CTX, n, f);
}
#define glDepthRange MGLD_glDepthRange

static __inline__ void MGLD_glDisable(GLenum cap)
{
    MiniGLDispatch->MGLSetState(MGLD_CTX, cap, GL_FALSE);
}
#define glDisable MGLD_glDisable

static __inline__ void MGLD_glDisableClientState(GLenum cap)
{
    MiniGLDispatch->GLDisableClientState(MGLD_CTX, cap);
}
#define glDisableClientState MGLD_glDisableClientState

static __inline__ void MGLD_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    MiniGLDispatch->GLDrawArrays(MGLD_CTX, mode, first, count);
}
#define glDrawArrays MGLD_glDrawArrays

static __inline__ void MGLD_glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLInterleavedArrays(MGLD_CTX, format, stride, pointer);
}
#define glInterleavedArrays MGLD_glInterleavedArrays

static __inline__ void MGLD_glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    MiniGLDispatch->GLMultiDrawArrays(MGLD_CTX, mode, first, count, primcount);
}
#define glMultiDrawArrays MGLD_glMultiDrawArrays

static __inline__ void MGLD_glDrawBuffer(GLenum mode)
{
    MiniGLDispatch->GLDrawBuffer(MGLD_CTX, mode);
}
#define glDrawBuffer MGLD_glDrawBuffer

static __inline__ void MGLD_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *pointer)
{
    MiniGLDispatch->GLDrawElements(MGLD_CTX, mode, count, type, pointer);
}
#define glDrawElements MGLD_glDrawElements

static __inline__ void MGLD_glEnable(GLenum cap)
{
    MiniGLDispatch->MGLSetState(MGLD_CTX, cap, GL_TRUE);
}
#define glEnable MGLD_glEnable

static __inline__ void MGLD_glEnableClientState(GLenum cap)
{
    MiniGLDispatch->GLEnableClientState(MGLD_CTX, cap);
}
#define glEnableClientState MGLD_glEnableClientState

static __inline__ void MGLD_glEnd(void)
{
    MiniGLDispatch->GLEnd(MGLD_CTX);
    
}
#define glEnd MGLD_glEnd

static __inline__ void MGLD_glFinish(void)
{
    MiniGLDispatch->GLFinish(MGLD_CTX);
}
#define glFinish MGLD_glFinish

static __inline__ void MGLD_glFlush(void)
{
    MiniGLDispatch->GLFlush(MGLD_CTX);
}
#define glFlush MGLD_glFlush

static __inline__ void MGLD_glFogf(GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLFogf(MGLD_CTX, pname, param);
}
#define glFogf MGLD_glFogf

static __inline__ void MGLD_glFogfv(GLenum pname, GLfloat *param)
{
    MiniGLDispatch->GLFogfv(MGLD_CTX, pname, param);
}
#define glFogfv MGLD_glFogfv

static __inline__ void MGLD_glFogi(GLenum pname, GLint param)
{
    MiniGLDispatch->GLFogf(MGLD_CTX, pname, (GLfloat)param);
}
#define glFogi MGLD_glFogi

static __inline__ void MGLD_glFrontFace(GLenum mode)
{
    MiniGLDispatch->GLFrontFace(MGLD_CTX, mode);
}
#define glFrontFace MGLD_glFrontFace

static __inline__ void MGLD_glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    MiniGLDispatch->GLFrustum(MGLD_CTX, left, right, bottom, top, zNear, zFar);
}
#define glFrustum MGLD_glFrustum

static __inline__ void MGLD_glGenTextures(GLsizei n, GLuint *textures)
{
    MiniGLDispatch->GLGenTextures(MGLD_CTX, n, textures);
}
#define glGenTextures MGLD_glGenTextures

static __inline__ void MGLD_glGetBooleanv(GLenum pname, GLboolean *params)
{
    MiniGLDispatch->GLGetBooleanv(MGLD_CTX, pname, params);
}
#define glGetBooleanv MGLD_glGetBooleanv

static __inline__ GLenum MGLD_glGetError(void)
{
    return MiniGLDispatch->GLGetError(MGLD_CTX);
}
#define glGetError MGLD_glGetError

static __inline__ void MGLD_glGetFloatv(GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetFloatv(MGLD_CTX, pname, params);
}
#define glGetFloatv MGLD_glGetFloatv

static __inline__ void MGLD_glGetIntegerv(GLenum pname, GLint *params)
{
    MiniGLDispatch->GLGetIntegerv(MGLD_CTX, pname, params);
}
#define glGetIntegerv MGLD_glGetIntegerv

static __inline__ const GLubyte * MGLD_glGetString(GLenum name)
{
    return MiniGLDispatch->GLGetString(MGLD_CTX, name);
}
#define glGetString MGLD_glGetString

static __inline__ void MGLD_glHint(GLenum target, GLenum mode)
{
    MiniGLDispatch->GLHint(MGLD_CTX, target, mode);
}
#define glHint MGLD_glHint

static __inline__ GLboolean MGLD_glIsEnabled(GLenum cap)
{
    return MiniGLDispatch->GLIsEnabled(MGLD_CTX, cap);
}
#define glIsEnabled MGLD_glIsEnabled

static __inline__ void MGLD_glLoadIdentity(void)
{
    MiniGLDispatch->GLLoadIdentity(MGLD_CTX);
}
#define glLoadIdentity MGLD_glLoadIdentity

static __inline__ void MGLD_glLoadMatrixd(const GLdouble *m)
{
    MiniGLDispatch->GLLoadMatrixd(MGLD_CTX, m);
}
#define glLoadMatrixd MGLD_glLoadMatrixd

static __inline__ void MGLD_glLoadMatrixf(const GLfloat *m)
{
    MiniGLDispatch->GLLoadMatrixf(MGLD_CTX, m);
}
#define glLoadMatrixf MGLD_glLoadMatrixf

static __inline__ void MGLD_glLockArrays(GLuint first, GLsizei count)
{
    MiniGLDispatch->GLLockArrays(MGLD_CTX, first, count);
}
#define glLockArrays MGLD_glLockArrays

static __inline__ void MGLD_glMatrixMode(GLenum mode)
{
    MiniGLDispatch->GLMatrixMode(MGLD_CTX, mode);
}
#define glMatrixMode MGLD_glMatrixMode

static __inline__ void MGLD_glMultiTexCoord2fARB(GLenum unit, GLfloat s, GLfloat t)
{
    MiniGLDispatch->GLMultiTexCoord2fARB(MGLD_CTX, unit, s, t);
}
#define glMultiTexCoord2fARB MGLD_glMultiTexCoord2fARB

static __inline__ void MGLD_glMultiTexCoord2fvARB(GLenum unit, GLfloat *v)
{
    MiniGLDispatch->GLMultiTexCoord2fvARB(MGLD_CTX, unit, v);
}
#define glMultiTexCoord2fvARB MGLD_glMultiTexCoord2fvARB

static __inline__ void MGLD_glMultMatrixd(const GLdouble *m)
{
    MiniGLDispatch->GLMultMatrixd(MGLD_CTX, m);
}
#define glMultMatrixd MGLD_glMultMatrixd

static __inline__ void MGLD_glMultMatrixf(const GLfloat *m)
{
    MiniGLDispatch->GLMultMatrixf(MGLD_CTX, m);
}
#define glMultMatrixf MGLD_glMultMatrixf

static __inline__ void MGLD_glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLNormal3f(MGLD_CTX, x, y, z);
}
#define glNormal3f MGLD_glNormal3f

static __inline__ void MGLD_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    MiniGLDispatch->GLOrtho(MGLD_CTX, left, right, bottom, top, zNear, zFar);
}
#define glOrtho MGLD_glOrtho

static __inline__ void MGLD_glPixelStorei(GLenum pname, GLint param)
{
    MiniGLDispatch->GLPixelStorei(MGLD_CTX, pname, param);
}
#define glPixelStorei MGLD_glPixelStorei

static __inline__ void MGLD_glPointSize(GLfloat s)
{
    MiniGLDispatch->GLPointSize(MGLD_CTX, s);
}
#define glPointSize MGLD_glPointSize

static __inline__ void MGLD_glPolygonMode(GLenum face, GLenum mode)
{
    MiniGLDispatch->GLPolygonMode(MGLD_CTX, face, mode);
}
#define glPolygonMode MGLD_glPolygonMode

static __inline__ void MGLD_glPolygonOffset(GLfloat factor, GLfloat units)
{
    MiniGLDispatch->GLPolygonOffset(MGLD_CTX, factor, units);
}
#define glPolygonOffset MGLD_glPolygonOffset

static __inline__ void MGLD_glPopMatrix(void)
{
    MiniGLDispatch->GLPopMatrix(MGLD_CTX);
}
#define glPopMatrix MGLD_glPopMatrix

static __inline__ void MGLD_glPushMatrix(void)
{
    MiniGLDispatch->GLPushMatrix(MGLD_CTX);
}
#define glPushMatrix MGLD_glPushMatrix

static __inline__ void MGLD_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    MiniGLDispatch->GLReadPixels(MGLD_CTX, x, y, width, height, format, type, pixels);
}
#define glReadPixels MGLD_glReadPixels

static __inline__ void MGLD_glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLRotated(MGLD_CTX, angle, x, y, z);
}
#define glRotated MGLD_glRotated

static __inline__ void MGLD_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLRotatef(MGLD_CTX, angle, x, y, z);
}
#define glRotatef MGLD_glRotatef

static __inline__ void MGLD_glRotatefEXT(GLfloat angle, const GLint xyz)
{
    MiniGLDispatch->GLRotatefEXT(MGLD_CTX, angle, xyz);
}
#define glRotatefEXT MGLD_glRotatefEXT

static __inline__ void MGLD_glRotatefEXTs(GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
    MiniGLDispatch->GLRotatefEXTs(MGLD_CTX, sin_an, cos_an, xyz);
}
#define glRotatefEXTs MGLD_glRotatefEXTs

static __inline__ void MGLD_glScaled(GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLScaled(MGLD_CTX, x, y, z);
}
#define glScaled MGLD_glScaled

static __inline__ void MGLD_glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLScalef(MGLD_CTX, x, y, z);
}
#define glScalef MGLD_glScalef

static __inline__ void MGLD_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLScissor(MGLD_CTX, x, y, width, height);
}
#define glScissor MGLD_glScissor

static __inline__ void MGLD_glShadeModel(GLenum mode)
{
    MiniGLDispatch->GLShadeModel(MGLD_CTX, mode);
}
#define glShadeModel MGLD_glShadeModel

static __inline__ void MGLD_glTexCoord2f(GLfloat s, GLfloat t)
{
    MiniGLDispatch->GLTexCoord2f(MGLD_CTX, s, t);
}
#define glTexCoord2f MGLD_glTexCoord2f

static __inline__ void MGLD_glTexCoord2fv(GLfloat *v)
{
    MiniGLDispatch->GLTexCoord2fv(MGLD_CTX, v);
}
#define glTexCoord2fv MGLD_glTexCoord2fv

static __inline__ void MGLD_glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    MiniGLDispatch->GLTexCoord4f(MGLD_CTX, s, t, r, q);
}
#define glTexCoord4f MGLD_glTexCoord4f

static __inline__ void MGLD_glTexCoord4fv(GLfloat *v)
{
    MiniGLDispatch->GLTexCoord4fv(MGLD_CTX, v);
}
#define glTexCoord4fv MGLD_glTexCoord4fv

static __inline__ void MGLD_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLTexCoordPointer(MGLD_CTX, size, type, stride, pointer);
}
#define glTexCoordPointer MGLD_glTexCoordPointer

static __inline__ void MGLD_glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLTexEnvi(MGLD_CTX, target, pname, (GLint)param);
}
#define glTexEnvf MGLD_glTexEnvf

static __inline__ void MGLD_glTexEnvfv(GLenum target, GLenum pname, GLfloat *param)
{
    MiniGLDispatch->GLTexEnvi(MGLD_CTX, target, pname, (GLint)*param);
}
#define glTexEnvfv MGLD_glTexEnvfv

static __inline__ void MGLD_glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    MiniGLDispatch->GLTexEnvi(MGLD_CTX, target, pname, param);
}
#define glTexEnvi MGLD_glTexEnvi

static __inline__ void MGLD_glTexEnviv(GLenum target, GLenum pname, GLint *param)
{
    MiniGLDispatch->GLTexEnvi(MGLD_CTX, target, pname, *param);
}
#define glTexEnviv MGLD_glTexEnviv

static __inline__ void MGLD_glTexGeni(GLenum coord, GLenum mode, GLenum map)
{
    MiniGLDispatch->GLTexGeni(MGLD_CTX, coord, mode, map);
}
#define glTexGeni MGLD_glTexGeni

static __inline__ void MGLD_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    MiniGLDispatch->GLTexImage2D(MGLD_CTX, target, level, internalformat, width, height, border, format, type, pixels);
}
#define glTexImage2D MGLD_glTexImage2D

static __inline__ void MGLD_glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLTexParameteri(MGLD_CTX, target, pname, (GLint)param);
}
#define glTexParameterf MGLD_glTexParameterf

static __inline__ void MGLD_glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    MiniGLDispatch->GLTexParameteri(MGLD_CTX, target, pname, param);
}
#define glTexParameteri MGLD_glTexParameteri

static __inline__ void MGLD_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    MiniGLDispatch->GLTexSubImage2D(MGLD_CTX, target, level, xoffset, yoffset, width, height, format, type, pixels);
}
#define glTexSubImage2D MGLD_glTexSubImage2D

static __inline__ void MGLD_glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLTranslated(MGLD_CTX, x, y, z);
}
#define glTranslated MGLD_glTranslated

static __inline__ void MGLD_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLTranslatef(MGLD_CTX, x, y, z);
}
#define glTranslatef MGLD_glTranslatef

static __inline__ void MGLD_gluLookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLULookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz);
}
#define gluLookAt MGLD_gluLookAt

static __inline__ void MGLD_glUnlockArrays(void)
{
    MiniGLDispatch->GLUnlockArrays(MGLD_CTX);
}
#define glUnlockArrays MGLD_glUnlockArrays

static __inline__ void MGLD_gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUPerspective(fovy, aspect, znear, zfar);
}
#define gluPerspective MGLD_gluPerspective

static __inline__ void MGLD_glVertex2f(GLfloat x, GLfloat y)
{
    MiniGLDispatch->GLVertex4f(MGLD_CTX, x, y, 0.0f, 1.0f);
}
#define glVertex2f MGLD_glVertex2f

static __inline__ void MGLD_glVertex2fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex2fv(MGLD_CTX, v);
}
#define glVertex2fv MGLD_glVertex2fv

static __inline__ void MGLD_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLVertex4f(MGLD_CTX, x, y, z, 1.0f);
}
#define glVertex3f MGLD_glVertex3f

static __inline__ void MGLD_glVertex3fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex3fv(MGLD_CTX, v);
}
#define glVertex3fv MGLD_glVertex3fv

static __inline__ void MGLD_glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    MiniGLDispatch->GLVertex4f(MGLD_CTX, x, y, z, w);
}
#define glVertex4f MGLD_glVertex4f

static __inline__ void MGLD_glVertex4fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex4fv(MGLD_CTX, v);
}
#define glVertex4fv MGLD_glVertex4fv

static __inline__ void MGLD_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLVertexPointer(MGLD_CTX, size, type, stride, pointer);
}
#define glVertexPointer MGLD_glVertexPointer

static __inline__ void MGLD_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLViewport(MGLD_CTX, x, y, width, height);
}
#define glViewport MGLD_glViewport

static __inline__ void MGLD_mglChoosePixelDepth(int depth)
{
    if ((MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_PISTORM3D) && depth == 32)
        depth = 24;
    MiniGLDispatch->mglChoosePixelDepth(depth);
}
#define mglChoosePixelDepth MGLD_mglChoosePixelDepth

static __inline__ void MGLD_mglClearPointer(void)
{
    MiniGLDispatch->MGLClearPointer(MGLD_CTX);
}
#define mglClearPointer MGLD_mglClearPointer

static __inline__ void * MGLD_mglCreateContext(int offx, int offy, int w, int h)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContext(offx, offy, w, h);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}
#define mglCreateContext MGLD_mglCreateContext

static __inline__ void * MGLD_mglCreateContextFromID(GLint id, GLint *w, GLint *h)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromID(id, w, h);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}
#define mglCreateContextFromID MGLD_mglCreateContextFromID


static __inline__ void MGLD_mglDeleteContext(void)
{
    if ((*MiniGLDispatch->currentContext)) {
        GLcontext context = (*MiniGLDispatch->currentContext);
        (*MiniGLDispatch->currentContext) = (GLcontext)0;
        MiniGLDispatch->MGLDeleteContext(context);
    }
}
#define mglDeleteContext MGLD_mglDeleteContext

static __inline__ void MGLD_mglDrawMultitexBuffer(GLenum s, GLenum d, GLenum env)
{
    MiniGLDispatch->MGLDrawMultitexBuffer(MGLD_CTX, s, d, env);
}
#define mglDrawMultitexBuffer MGLD_mglDrawMultitexBuffer

static __inline__ void MGLD_mglEnableSync(GLboolean enable)
{
    MiniGLDispatch->MGLEnableSync(MGLD_CTX, enable);
}
#define mglEnableSync MGLD_mglEnableSync

static __inline__ void MGLD_mglExit(void)
{
    MiniGLDispatch->MGLExit(MGLD_CTX);
}
#define mglExit MGLD_mglExit

static __inline__ void MGLD_mglEmergencyCloseScreen(void)
{
    MiniGLDispatch->MGLEmergencyCloseScreen(MGLD_CTX);
}
#define mglEmergencyCloseScreen MGLD_mglEmergencyCloseScreen

static __inline__ void MGLD_glMGLForceNewRenderPass(void)
{
    MiniGLDispatch->MGLForceNewRenderPass(MGLD_CTX);
}
#define glMGLForceNewRenderPass MGLD_glMGLForceNewRenderPass

static __inline__ void * MGLD_mglGetWindowHandle(void)
{
    return MiniGLDispatch->MGLGetWindowHandle(MGLD_CTX);
}
#define mglGetWindowHandle MGLD_mglGetWindowHandle

static __inline__ void MGLD_mglIdleFunc(IdleFn i)
{
    MiniGLDispatch->MGLIdleFunc(MGLD_CTX, i);
}
#define mglIdleFunc MGLD_mglIdleFunc

static __inline__ void MGLD_mglKeyFunc(KeyHandlerFn k)
{
    MiniGLDispatch->MGLKeyFunc(MGLD_CTX, k);
}
#define mglKeyFunc MGLD_mglKeyFunc

static __inline__ GLboolean MGLD_mglLockBack(MGLLockInfo *info)
{
    return MiniGLDispatch->MGLLockBack(MGLD_CTX, info);
}
#define mglLockBack MGLD_mglLockBack

static __inline__ GLboolean MGLD_mglLockDisplay(void)
{
    return MiniGLDispatch->MGLLockDisplay(MGLD_CTX);
}
#define mglLockDisplay MGLD_mglLockDisplay

static __inline__ void MGLD_mglLockMode(GLenum lockMode)
{
    MiniGLDispatch->MGLLockMode(MGLD_CTX, lockMode);
}
#define mglLockMode MGLD_mglLockMode

static __inline__ void MGLD_mglMainLoop(void)
{
    MiniGLDispatch->MGLMainLoop(MGLD_CTX);
}
#define mglMainLoop MGLD_mglMainLoop

static __inline__ void MGLD_mglMinTriArea(GLfloat area)
{
    MiniGLDispatch->MGLMinTriArea(MGLD_CTX, area);
}
#define mglMinTriArea MGLD_mglMinTriArea

static __inline__ void MGLD_mglMouseFunc(MouseHandlerFn m)
{
    MiniGLDispatch->MGLMouseFunc(MGLD_CTX, m);
}
#define mglMouseFunc MGLD_mglMouseFunc

static __inline__ void MGLD_mglPrintMatrix(GLenum mode)
{
    MiniGLDispatch->MGLPrintMatrix(MGLD_CTX, mode);
}
#define mglPrintMatrix MGLD_mglPrintMatrix

static __inline__ void MGLD_mglPrintMatrixStack(GLenum mode)
{
    MiniGLDispatch->MGLPrintMatrixStack(MGLD_CTX, mode);
}
#define mglPrintMatrixStack MGLD_mglPrintMatrixStack

static __inline__ void MGLD_mglResizeContext(GLsizei width, GLsizei height)
{
    MiniGLDispatch->MGLResizeContext(MGLD_CTX, width, height);
}
#define mglResizeContext MGLD_mglResizeContext

static __inline__ void MGLD_mglSetPointer(void)
{
    MiniGLDispatch->MGLSetPointer(MGLD_CTX);
}
#define mglSetPointer MGLD_mglSetPointer

static __inline__ void MGLD_mglSetZOffset(GLfloat offset)
{
    MiniGLDispatch->MGLSetZOffset(MGLD_CTX, offset);
}
#define mglSetZOffset MGLD_mglSetZOffset

static __inline__ void MGLD_mglSpecialFunc(SpecialHandlerFn s)
{
    MiniGLDispatch->MGLSpecialFunc(MGLD_CTX, s);
}
#define mglSpecialFunc MGLD_mglSpecialFunc

static __inline__ void MGLD_mglSwitchDisplay(void)
{
    MiniGLDispatch->MGLSwitchDisplay(MGLD_CTX);
}
#define mglSwitchDisplay MGLD_mglSwitchDisplay

static __inline__ void MGLD_mglTexMemStat(GLint *Current, GLint *Peak)
{
    MiniGLDispatch->MGLTexMemStat(MGLD_CTX, Current, Peak);
}
#define mglTexMemStat MGLD_mglTexMemStat

static __inline__ void MGLD_mglUnlockDisplay(void)
{
    MiniGLDispatch->MGLUnlockDisplay(MGLD_CTX);
}
#define mglUnlockDisplay MGLD_mglUnlockDisplay

static __inline__ void MGLD_mglWriteShotPPM(char *filename)
{
    MiniGLDispatch->MGLWriteShotPPM(MGLD_CTX, filename);
}
#define mglWriteShotPPM MGLD_mglWriteShotPPM

static __inline__ void MGLD_mglChooseNumberOfBuffers(int number)
{
    MiniGLDispatch->mglChooseNumberOfBuffers(number);
}
#define mglChooseNumberOfBuffers MGLD_mglChooseNumberOfBuffers

static __inline__ void MGLD_mglChooseVertexBufferSize(int size)
{
    MiniGLDispatch->mglChooseVertexBufferSize(size);
}
#define mglChooseVertexBufferSize MGLD_mglChooseVertexBufferSize

static __inline__ void MGLD_mglChooseWindowMode(GLboolean flag)
{
    MiniGLDispatch->mglChooseWindowMode(flag);
}
#define mglChooseWindowMode MGLD_mglChooseWindowMode

static __inline__ void MGLD_mglProposeCloseDesktop(GLboolean closeme)
{
    MiniGLDispatch->mglProposeCloseDesktop(closeme);
}
#define mglProposeCloseDesktop MGLD_mglProposeCloseDesktop

static __inline__ void MGLD_mglChooseGuardBand(GLboolean flag)
{
    MiniGLDispatch->mglChooseGuardBand(flag);
}
#define mglChooseGuardBand MGLD_mglChooseGuardBand

static __inline__ void MGLD_mglChooseMtexBufferSize(int size)
{
    MiniGLDispatch->mglChooseMtexBufferSize(size);
}
#define mglChooseMtexBufferSize MGLD_mglChooseMtexBufferSize

static __inline__ void MGLD_mglChooseTextureBufferSize(int size)
{
    MiniGLDispatch->mglChooseTextureBufferSize(size);
}
#define mglChooseTextureBufferSize MGLD_mglChooseTextureBufferSize

static __inline__ void MGLD_mglProhibitAlphaFallback(GLboolean flag)
{
    MiniGLDispatch->mglProhibitAlphaFallback(flag);
}
#define mglProhibitAlphaFallback MGLD_mglProhibitAlphaFallback

static __inline__ void MGLD_mglProhibitMipMapping(GLboolean flag)
{
    MiniGLDispatch->mglProhibitMipMapping(flag);
}
#define mglProhibitMipMapping MGLD_mglProhibitMipMapping

static __inline__ GLint MGLD_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn)
{
    return MiniGLDispatch->mglGetSupportedScreenModes(CallbackFn);
}
#define mglGetSupportedScreenModes MGLD_mglGetSupportedScreenModes

static __inline__ void * MGLD_mglGetInputWindowHandle(void)
{
    return MiniGLDispatch->MGLGetInputWindowHandle(MGLD_CTX);
}
#define mglGetInputWindowHandle MGLD_mglGetInputWindowHandle

static __inline__ GLboolean MGLD_mglBeginFastPath(GLenum primitive,
                                                  ULONG attributes,
                                                  MGLFastPath *fp)
{
    return MiniGLDispatch->mglBeginFastPath(primitive, attributes, fp);
}
#define mglBeginFastPath MGLD_mglBeginFastPath

static __inline__ void MGLD_mglEndFastPath(MGLFastPath *fp)
{
    MiniGLDispatch->mglEndFastPath(fp);
}
#define mglEndFastPath MGLD_mglEndFastPath

static __inline__ ULONG MGLD_mglGetFastPathCapabilities(void)
{
    return MiniGLDispatch->mglGetFastPathCapabilities();
}
#define mglGetFastPathCapabilities MGLD_mglGetFastPathCapabilities

#endif /* !MINIGL_LIBRARY_BUILD */

#endif
