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
/* GLUT, for the 22 GLUT slots. USE_MGLAPI (defined above) suppresses this
 * header's own direct-call glut* macros so the dispatch wrappers below win. */
#include <mgl/glut.h>
#include <libraries/minigl_fastpath.h>
#ifdef MINIGL_DIRECT_RESTORE_USE_MGLAPI
#undef USE_MGLAPI
#undef MINIGL_DIRECT_RESTORE_USE_MGLAPI
#endif
#ifdef MINIGL_DIRECT_RESTORE_DISPATCH_DECLARATIONS
#undef MINIGL_DISPATCH_DECLARATIONS
#undef MINIGL_DIRECT_RESTORE_DISPATCH_DECLARATIONS
#endif

/*
 * 3 -> 5 (V29 parity): matches upstream minigl.library 29.x. Upstream's 4 moved
 * the mgl/gl.h tokens to OpenGL's own values (a client built on the old header
 * passes different integers for the same name, which structSize cannot catch);
 * 5 is upstream's declared 29.0 release boundary. MiniGLOpen tests it for
 * EQUALITY, so every ABI-3 client must be rebuilt against this header.
 */
#define MINIGL_DISPATCH_ABI_VERSION 5UL
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
    GLboolean (*MGLResizeContext)(GLcontext context, GLsizei width, GLsizei height);
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
     * V10 prefix. Slots 130..205 below follow the upstream minigl.library
     * 29.1 (ABI 5) table exactly, so a client built against either header
     * reaches the same entry point at the same offset; the upstream append
     * history is kept in the comments. tools/check_dispatch_layout.py checks
     * this field by field against tools/reference/minigl_dispatch_v29_1.txt.
     * V19 had MGLEmergencyCloseScreen and MGLForceNewRenderPass at slots 133
     * and 134; upstream removed both, so they no longer have slots (their
     * client macros below are no-ops). */
    void (*GLClientActiveTextureARB)(GLcontext context, GLenum unit);
    void (*GLInterleavedArrays)(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer);
    void (*GLMultiDrawArrays)(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);
    void (*GLBlendEquation)(GLcontext context, GLenum mode);
    void (*GLBlendFuncSeparate)(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
    GLboolean (*GLIsTexture)(GLcontext context, GLuint texture);
    void (*mglChooseZBufferDepth)(int bits);
    /* APPENDED 2026-09-12 (GL 1.1 audit fix 4). glLineWidth had no entry
     * point at all until today, so shared-library clients had no way to ask
     * for a line wider than one pixel. At the END for the reason spelled out
     * above -- this table grows to 137 entries and every existing offset is
     * untouched, so a customer binary built on the 136-entry struct keeps
     * working against this library unchanged; only a client rebuilt on THIS
     * header needs a library at least this new, which is what structSize
     * already enforces. */
    void (*GLLineWidth)(GLcontext context, GLfloat width);
    /* APPENDED 2026-09-12 (audit fix 7). glTexGenfv is what makes
     * GL_OBJECT_LINEAR and GL_EYE_LINEAR reachable at all -- they are
     * defined by plane equations, and without this there is no way to
     * supply one. 138th entry, at the END for the reason above: every
     * existing offset is untouched. */
    void (*GLTexGenfv)(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params);
    /* APPENDED 2026-09-12 (audit fix 9), entries 139 and 140. The two
     * framebuffer-to-texture copies: without these a shared-library client
     * cannot reach the one capability the audit says a game cannot work
     * around. Order matches the audit row and the GL spec, defining form
     * first. At the END, so every existing offset is untouched. */
    void (*GLCopyTexImage2D)(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
    void (*GLCopyTexSubImage2D)(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
    /* APPENDED 2026-09-12 (GL 1.1 audit item 10), entries 141 to 151. The
     * eleven entry points whose gl* macros had no function behind them: the
     * static archive has had real ones since that fix, and without these
     * slots a shared-library client still cannot reach any of them. Order is
     * gl.h's own prototype order. At the END, so every existing offset is
     * untouched and a client built on the 140-entry struct keeps working. */
    GLboolean (*GLAreTexturesResident)(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences);
    void (*GLEdgeFlag)(GLcontext context, GLboolean flag);
    void (*GLEdgeFlagPointer)(GLcontext context, GLsizei stride, const GLvoid *pointer);
    void (*GLEdgeFlagv)(GLcontext context, const GLboolean *flag);
    void (*GLGetDoublev)(GLcontext context, GLenum pname, GLdouble *params);
    void (*GLGetPointerv)(GLcontext context, GLenum pname, GLvoid **params);
    void (*GLIndexi)(GLcontext context, GLint c);
    void (*GLIndexiv)(GLcontext context, const GLint *c);
    void (*GLIndexPointer)(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer);
    void (*GLPrioritizeTextures)(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities);
    void (*GLReadBuffer)(GLcontext context, GLenum mode);

    /* APPENDED 2026-09-20. Every existing offset stays put; a positional
     * initialiser rebinds silently if anything is inserted above. ABI version
     * deliberately NOT bumped -- structSize is the real guard, and its direction
     * is already right (new library + old client passes untouched, old library +
     * client built on this header is cleanly refused). */
    void * (*MGLCreateContextFromWindow)(struct Window *window);

    /* APPENDED 2026-09-20, after MGLCreateContextFromWindow. Same rule: end of
     * the struct, all three tables together, ABI version untouched. */
    void * (*MGLCreateContextFromBitMap)(struct BitMap *bitmap);

    /* APPENDED 2026-09-27 for minigl.library 28.0 -- 47 entries, slots 153..199,
     * taking the table from 153 to 200. Everything before this block is
     * untouched, which is what keeps existing clients working: MiniGLOpen's
     * guard is `dispatch->structSize < sizeof(MGLDispatchTable)`, so a client
     * built on the shorter struct still accepts this longer library, while one
     * rebuilt on this header correctly refuses an older one. ABI version stays
     * 3; structSize is the real guard.
     *
     * ALL THREE TABLES GROW TOGETHER. A short positional initialiser zero-fills
     * the trailing slots while sizeof still reports the full struct, so the ABI
     * check passes and the call NULL-derefs.
     *
     * These 47 are what made GL, GLU and GLUT reachable through the library
     * rather than only through the static archive: fixed-function lighting,
     * display lists, the GLU quadrics and mipmap builder, and GLUT. Base MiniGL
     * has none of them, so the classic table answers every one with a stub. */

    /* core GL -- fixed-function lighting */
    void (*GLLightf)(GLcontext context, GLenum light, GLenum pname, GLfloat param);
    void (*GLLightfv)(GLcontext context, GLenum light, GLenum pname, const GLfloat *params);
    void (*GLMaterialf)(GLcontext context, GLenum face, GLenum pname, GLfloat param);
    void (*GLMaterialfv)(GLcontext context, GLenum face, GLenum pname, const GLfloat *params);
    void (*GLLightModelf)(GLcontext context, GLenum pname, GLfloat param);
    void (*GLLightModelfv)(GLcontext context, GLenum pname, const GLfloat *params);
    void (*GLGetLightfv)(GLcontext context, GLenum light, GLenum pname, GLfloat *params);
    void (*GLGetMaterialfv)(GLcontext context, GLenum face, GLenum pname, GLfloat *params);

    /* core GL -- display lists */
    GLuint (*GLGenLists)(GLcontext context, GLsizei range);
    void (*GLDeleteLists)(GLcontext context, GLuint list, GLsizei range);
    GLboolean (*GLIsList)(GLcontext context, GLuint list);
    void (*GLNewList)(GLcontext context, GLuint list, GLenum mode);
    void (*GLEndList)(GLcontext context);
    void (*GLCallList)(GLcontext context, GLuint list);

    /* GLU */
    GLint (*GLUBuild2DMipmaps)(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data);
    const GLubyte * (*GLUErrorString)(GLenum errCode);
    GLUquadricObj * (*GLUNewQuadric)(void);
    void (*GLUDeleteQuadric)(GLUquadricObj *q);
    void (*GLUQuadricNormals)(GLUquadricObj *q, GLenum normals);
    void (*GLUQuadricTexture)(GLUquadricObj *q, GLboolean textureCoords);
    void (*GLUQuadricDrawStyle)(GLUquadricObj *q, GLenum drawStyle);
    void (*GLUQuadricOrientation)(GLUquadricObj *q, GLenum orientation);
    void (*GLUCylinder)(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks);
    void (*GLUSphere)(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks);
    void (*GLUDisk)(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops);

    /* GLUT */
    void (*GLUTInit)(int *argcp, char **argv);
    void (*GLUTInitDisplayMode)(unsigned int mode);
    void (*GLUTInitWindowSize)(int width, int height);
    void (*GLUTInitWindowPosition)(int x, int y);
    int (*GLUTCreateWindow)(const char *title);
    void (*GLUTMainLoop)(void);
    void (*GLUTDisplayFunc)(void (*func)(void));
    void (*GLUTIdleFunc)(void (*func)(void));
    void (*GLUTKeyboardFunc)(void (*func)(unsigned char key, int x, int y));
    void (*GLUTReshapeFunc)(void (*func)(int width, int height));
    void (*GLUTSwapBuffers)(void);
    void (*GLUTPostRedisplay)(void);
    int (*GLUTGet)(GLenum state);
    void (*GLUTGameModeString)(const char *string);
    int (*GLUTEnterGameMode)(void);
    void (*GLUTLeaveGameMode)(void);
    int (*GLUTGameModeGet)(GLenum query);
    void (*GLUTSolidCube)(GLdouble size);
    void (*GLUTSolidSphere)(GLdouble radius, GLint slices, GLint stacks);
    void (*GLUTSolidCone)(GLdouble base, GLdouble height, GLint slices, GLint stacks);
    void (*GLUTSolidTorus)(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings);
    void (*GLUTSolidDodecahedron)(void);
    /* APPENDED, and it must stay last: this table is a POSITIONAL initialiser,
     * so a member inserted anywhere above shifts every pointer after it in all
     * three tables at once. structSize is what lets an older library be
     * refused; the ABI version does not move for an append. */
    void (*GLNormalPointer)(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer);
    /* Slot 202, appended 2026-10-03. A quadric's GLU_ERROR callback, which is
     * the only error channel GLU gives a quadric -- every argument rejection in
     * gluCylinder, gluSphere, gluDisk and the three validating setters reports
     * through it. Takes no GLcontext: a quadric is not context state. */
    void (*GLUQuadricCallback)(GLUquadricObj *q, GLenum which, MGLUfuncptr fn);
    /* Slot 203, appended 2026-10-04. GL_COLOR_MATERIAL's parameter: which
     * material component tracks the per-vertex colour. The capability itself
     * goes through glEnable and needs no slot. */
    void (*GLColorMaterial)(GLcontext context, GLenum face, GLenum mode);
    /* Slot 204, appended 2026-10-04. GL_TEXTURE_ENV_COLOR, the constant
     * GL_BLEND's equation blends toward. The integer GLTexEnvi cannot carry
     * four floats and refuses that pname. */
    void (*GLTexEnvfv)(GLcontext context, GLenum target, GLenum pname, const GLfloat *params);
    /* Slot 205, appended 2026-10-05. glNormal3fv reaches its own entry point
     * rather than expanding to GLNormal3f in the header, so the NULL guard the
     * driver carries is reachable from both linkages. */
    void (*GLNormal3fv)(GLcontext context, GLfloat *n);

    /* PPC extension, after the last upstream slot (205): FastPath direct
     * vertex-buffer submission (see libraries/minigl_fastpath.h). These were
     * slots 135..137 in the V19 ABI-3 table. They keep the DLL's
     * implicit-current-context model: no GLcontext argument. Any further
     * upstream slots must be inserted BEFORE this block, which moves it. */
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

/* V19 had table slots for these two; upstream 29.x removed them and the V19
 * implementations were already no-ops. Kept as source-compatible no-ops. */
static __inline__ void MGLD_mglEmergencyCloseScreen(void)
{
}
#define mglEmergencyCloseScreen MGLD_mglEmergencyCloseScreen

static __inline__ void MGLD_glMGLForceNewRenderPass(void)
{
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

static __inline__ GLboolean MGLD_mglResizeContext(GLsizei width, GLsizei height)
{
    return MiniGLDispatch->MGLResizeContext(MGLD_CTX, width, height);
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

/* Client wrappers for the upstream 29.1 slots 133..205 and the convenience
 * aliases (glVertex*d/i/s, glTexCoord*, glActiveTexture, ...) built on older
 * slots. Taken from upstream libraryroot/include/libraries/minigl_dispatch.h
 * unchanged, so source written for 29.1 compiles the same way here. As
 * upstream: the GLU draw calls are guarded on a current context like
 * gluPerspective; the GLUT lifecycle calls are not, because glutInit runs
 * before a context exists. */

static __inline__ void MGLD_glNormal3fv(GLfloat *v)
{
    MiniGLDispatch->GLNormal3fv(MGLD_CTX, v);
}
#undef glNormal3fv
#define glNormal3fv MGLD_glNormal3fv

static __inline__ void MGLD_glLineWidth(GLfloat w)
{
    MiniGLDispatch->GLLineWidth(MGLD_CTX, w);
}
#undef glLineWidth
#define glLineWidth MGLD_glLineWidth

static __inline__ void MGLD_glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLTexGenfv(MGLD_CTX, coord, pname, params);
}
#undef glTexGenfv
#define glTexGenfv MGLD_glTexGenfv

static __inline__ void MGLD_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    MiniGLDispatch->GLCopyTexImage2D(MGLD_CTX, target, level, internalformat, x, y, width, height, border);
}
#undef glCopyTexImage2D
#define glCopyTexImage2D MGLD_glCopyTexImage2D

static __inline__ void MGLD_glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLCopyTexSubImage2D(MGLD_CTX, target, level, xoffset, yoffset, x, y, width, height);
}
#undef glCopyTexSubImage2D
#define glCopyTexSubImage2D MGLD_glCopyTexSubImage2D

static __inline__ void MGLD_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLNormalPointer(MGLD_CTX, type, stride, pointer);
}
#undef glNormalPointer
#define glNormalPointer MGLD_glNormalPointer

static __inline__ void MGLD_gluQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn)
{
    MiniGLDispatch->GLUQuadricCallback(q, which, fn);
}
#undef gluQuadricCallback
#define gluQuadricCallback MGLD_gluQuadricCallback

static __inline__ void MGLD_glColorMaterial(GLenum face, GLenum mode)
{
    MiniGLDispatch->GLColorMaterial(MGLD_CTX, face, mode);
}
#undef glColorMaterial
#define glColorMaterial MGLD_glColorMaterial

static __inline__ void * MGLD_mglCreateContextFromWindow(struct Window *window)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromWindow(window);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}
#undef mglCreateContextFromWindow
#define mglCreateContextFromWindow MGLD_mglCreateContextFromWindow

static __inline__ void * MGLD_mglCreateContextFromBitMap(struct BitMap *bitmap)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromBitMap(bitmap);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}
#undef mglCreateContextFromBitMap
#define mglCreateContextFromBitMap MGLD_mglCreateContextFromBitMap

static __inline__ GLboolean MGLD_glIsTexture(GLuint texture)
{
    return MiniGLDispatch->GLIsTexture(MGLD_CTX, texture);
}
#undef glIsTexture
#define glIsTexture MGLD_glIsTexture

static __inline__ void MGLD_mglChooseZBufferDepth(int bits)
{
    MiniGLDispatch->mglChooseZBufferDepth(bits);
}
#undef mglChooseZBufferDepth
#define mglChooseZBufferDepth MGLD_mglChooseZBufferDepth

static __inline__ void MGLD_glBlendEquation(GLenum mode)
{
    MiniGLDispatch->GLBlendEquation(MGLD_CTX, mode);
}
#undef glBlendEquation
#define glBlendEquation MGLD_glBlendEquation

static __inline__ void MGLD_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    MiniGLDispatch->GLBlendFuncSeparate(MGLD_CTX, srcRGB, dstRGB, srcAlpha, dstAlpha);
}
#undef glBlendFuncSeparate
#define glBlendFuncSeparate MGLD_glBlendFuncSeparate

static __inline__ GLboolean MGLD_glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
    return MiniGLDispatch->GLAreTexturesResident(MGLD_CTX, n, textures, residences);
}
#undef glAreTexturesResident
#define glAreTexturesResident MGLD_glAreTexturesResident

static __inline__ void MGLD_glEdgeFlag(GLboolean flag)
{
    MiniGLDispatch->GLEdgeFlag(MGLD_CTX, flag);
}
#undef glEdgeFlag
#define glEdgeFlag MGLD_glEdgeFlag

static __inline__ void MGLD_glEdgeFlagPointer(GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLEdgeFlagPointer(MGLD_CTX, stride, pointer);
}
#undef glEdgeFlagPointer
#define glEdgeFlagPointer MGLD_glEdgeFlagPointer

static __inline__ void MGLD_glEdgeFlagv(const GLboolean *flag)
{
    MiniGLDispatch->GLEdgeFlagv(MGLD_CTX, flag);
}
#undef glEdgeFlagv
#define glEdgeFlagv MGLD_glEdgeFlagv

static __inline__ void MGLD_glGetDoublev(GLenum pname, GLdouble *params)
{
    MiniGLDispatch->GLGetDoublev(MGLD_CTX, pname, params);
}
#undef glGetDoublev
#define glGetDoublev MGLD_glGetDoublev

static __inline__ void MGLD_glGetPointerv(GLenum pname, GLvoid **params)
{
    MiniGLDispatch->GLGetPointerv(MGLD_CTX, pname, params);
}
#undef glGetPointerv
#define glGetPointerv MGLD_glGetPointerv

static __inline__ void MGLD_glIndexi(GLint c)
{
    MiniGLDispatch->GLIndexi(MGLD_CTX, c);
}
#undef glIndexi
#define glIndexi MGLD_glIndexi

static __inline__ void MGLD_glIndexiv(const GLint *c)
{
    MiniGLDispatch->GLIndexiv(MGLD_CTX, c);
}
#undef glIndexiv
#define glIndexiv MGLD_glIndexiv

static __inline__ void MGLD_glIndexPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLIndexPointer(MGLD_CTX, type, stride, pointer);
}
#undef glIndexPointer
#define glIndexPointer MGLD_glIndexPointer

static __inline__ void MGLD_glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    MiniGLDispatch->GLPrioritizeTextures(MGLD_CTX, n, textures, priorities);
}
#undef glPrioritizeTextures
#define glPrioritizeTextures MGLD_glPrioritizeTextures

static __inline__ void MGLD_glReadBuffer(GLenum mode)
{
    MiniGLDispatch->GLReadBuffer(MGLD_CTX, mode);
}
#undef glReadBuffer
#define glReadBuffer MGLD_glReadBuffer

static __inline__ void MGLD_glLightf(GLenum light, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLLightf(MGLD_CTX, light, pname, param);
}
#undef glLightf
#define glLightf MGLD_glLightf

static __inline__ void MGLD_glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLLightfv(MGLD_CTX, light, pname, params);
}
#undef glLightfv
#define glLightfv MGLD_glLightfv

static __inline__ void MGLD_glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLMaterialf(MGLD_CTX, face, pname, param);
}
#undef glMaterialf
#define glMaterialf MGLD_glMaterialf

static __inline__ void MGLD_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLMaterialfv(MGLD_CTX, face, pname, params);
}
#undef glMaterialfv
#define glMaterialfv MGLD_glMaterialfv

static __inline__ void MGLD_glLightModelf(GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLLightModelf(MGLD_CTX, pname, param);
}
#undef glLightModelf
#define glLightModelf MGLD_glLightModelf

static __inline__ void MGLD_glLightModelfv(GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLLightModelfv(MGLD_CTX, pname, params);
}
#undef glLightModelfv
#define glLightModelfv MGLD_glLightModelfv

static __inline__ void MGLD_glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetLightfv(MGLD_CTX, light, pname, params);
}
#undef glGetLightfv
#define glGetLightfv MGLD_glGetLightfv

static __inline__ void MGLD_glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetMaterialfv(MGLD_CTX, face, pname, params);
}
#undef glGetMaterialfv
#define glGetMaterialfv MGLD_glGetMaterialfv

static __inline__ GLuint MGLD_glGenLists(GLsizei range)
{
    return MiniGLDispatch->GLGenLists(MGLD_CTX, range);
}
#undef glGenLists
#define glGenLists MGLD_glGenLists

static __inline__ void MGLD_glDeleteLists(GLuint list, GLsizei range)
{
    MiniGLDispatch->GLDeleteLists(MGLD_CTX, list, range);
}
#undef glDeleteLists
#define glDeleteLists MGLD_glDeleteLists

static __inline__ GLboolean MGLD_glIsList(GLuint list)
{
    return MiniGLDispatch->GLIsList(MGLD_CTX, list);
}
#undef glIsList
#define glIsList MGLD_glIsList

static __inline__ void MGLD_glNewList(GLuint list, GLenum mode)
{
    MiniGLDispatch->GLNewList(MGLD_CTX, list, mode);
}
#undef glNewList
#define glNewList MGLD_glNewList

static __inline__ void MGLD_glEndList(void)
{
    MiniGLDispatch->GLEndList(MGLD_CTX);
}
#undef glEndList
#define glEndList MGLD_glEndList

static __inline__ void MGLD_glCallList(GLuint list)
{
    MiniGLDispatch->GLCallList(MGLD_CTX, list);
}
#undef glCallList
#define glCallList MGLD_glCallList

static __inline__ GLint MGLD_gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data)
{
    return MiniGLDispatch->GLUBuild2DMipmaps(MGLD_CTX, target, internalFormat, width, height, format, type, data);
}
#undef gluBuild2DMipmaps
#define gluBuild2DMipmaps MGLD_gluBuild2DMipmaps

static __inline__ const GLubyte * MGLD_gluErrorString(GLenum errCode)
{
    return MiniGLDispatch->GLUErrorString(errCode);
}
#undef gluErrorString
#define gluErrorString MGLD_gluErrorString

static __inline__ GLUquadricObj * MGLD_gluNewQuadric(void)
{
    return MiniGLDispatch->GLUNewQuadric();
}
#undef gluNewQuadric
#define gluNewQuadric MGLD_gluNewQuadric

static __inline__ void MGLD_gluDeleteQuadric(GLUquadricObj *q)
{
    MiniGLDispatch->GLUDeleteQuadric(q);
}
#undef gluDeleteQuadric
#define gluDeleteQuadric MGLD_gluDeleteQuadric

static __inline__ void MGLD_gluQuadricNormals(GLUquadricObj *q, GLenum normals)
{
    MiniGLDispatch->GLUQuadricNormals(q, normals);
}
#undef gluQuadricNormals
#define gluQuadricNormals MGLD_gluQuadricNormals

static __inline__ void MGLD_gluQuadricTexture(GLUquadricObj *q, GLboolean textureCoords)
{
    MiniGLDispatch->GLUQuadricTexture(q, textureCoords);
}
#undef gluQuadricTexture
#define gluQuadricTexture MGLD_gluQuadricTexture

static __inline__ void MGLD_gluQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle)
{
    MiniGLDispatch->GLUQuadricDrawStyle(q, drawStyle);
}
#undef gluQuadricDrawStyle
#define gluQuadricDrawStyle MGLD_gluQuadricDrawStyle

static __inline__ void MGLD_gluQuadricOrientation(GLUquadricObj *q, GLenum orientation)
{
    MiniGLDispatch->GLUQuadricOrientation(q, orientation);
}
#undef gluQuadricOrientation
#define gluQuadricOrientation MGLD_gluQuadricOrientation

static __inline__ void MGLD_gluCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUCylinder(q, base, top, height, slices, stacks);
}
#undef gluCylinder
#define gluCylinder MGLD_gluCylinder

static __inline__ void MGLD_gluSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUSphere(q, radius, slices, stacks);
}
#undef gluSphere
#define gluSphere MGLD_gluSphere

static __inline__ void MGLD_gluDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUDisk(q, inner, outer, slices, loops);
}
#undef gluDisk
#define gluDisk MGLD_gluDisk

#undef glTexCoord2d
#define glTexCoord2d(s, t) MGLD_glTexCoord2f((GLfloat)(s), (GLfloat)(t))

#undef glTexCoord1d
#define glTexCoord1d(s) MGLD_glTexCoord2f((GLfloat)(s), 0.f)

#undef glTexCoord1dv
#define glTexCoord1dv(v) MGLD_glTexCoord2f((GLfloat)(v)[0], 0.f)

#undef glTexCoord1f
#define glTexCoord1f(s) MGLD_glTexCoord2f((GLfloat)(s), 0.f)

#undef glTexCoord1fv
#define glTexCoord1fv(v) MGLD_glTexCoord2f((GLfloat)(v)[0], 0.f)

#undef glTexCoord1i
#define glTexCoord1i(s) MGLD_glTexCoord2f((GLfloat)(s), 0.f)

#undef glTexCoord1iv
#define glTexCoord1iv(v) MGLD_glTexCoord2f((GLfloat)(v)[0], 0.f)

#undef glTexCoord1s
#define glTexCoord1s(s) MGLD_glTexCoord2f((GLfloat)(s), 0.f)

#undef glTexCoord1sv
#define glTexCoord1sv(v) MGLD_glTexCoord2f((GLfloat)(v)[0], 0.f)

#undef glTexCoord2dv
#define glTexCoord2dv(v) MGLD_glTexCoord2f((GLfloat)(v)[0], (GLfloat)(v)[1])

#undef glTexCoord2i
#define glTexCoord2i(s, t) MGLD_glTexCoord2f((GLfloat)(s), (GLfloat)(t))

#undef glTexCoord2iv
#define glTexCoord2iv(v) MGLD_glTexCoord2f((GLfloat)(v)[0], (GLfloat)(v)[1])

#undef glTexCoord2s
#define glTexCoord2s(s, t) MGLD_glTexCoord2f((GLfloat)(s), (GLfloat)(t))

#undef glTexCoord2sv
#define glTexCoord2sv(v) MGLD_glTexCoord2f((GLfloat)(v)[0], (GLfloat)(v)[1])

#undef glTexCoord3d
#define glTexCoord3d(s, t, r) MGLD_glTexCoord4f((GLfloat)(s), (GLfloat)(t), (GLfloat)(r), 1.f)

#undef glTexCoord3dv
#define glTexCoord3dv(v) MGLD_glTexCoord4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)

#undef glTexCoord3f
#define glTexCoord3f(s, t, r) MGLD_glTexCoord4f((GLfloat)(s), (GLfloat)(t), (GLfloat)(r), 1.f)

#undef glTexCoord3fv
#define glTexCoord3fv(v) MGLD_glTexCoord4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)

#undef glTexCoord3i
#define glTexCoord3i(s, t, r) MGLD_glTexCoord4f((GLfloat)(s), (GLfloat)(t), (GLfloat)(r), 1.f)

#undef glTexCoord3iv
#define glTexCoord3iv(v) MGLD_glTexCoord4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)

#undef glTexCoord3s
#define glTexCoord3s(s, t, r) MGLD_glTexCoord4f((GLfloat)(s), (GLfloat)(t), (GLfloat)(r), 1.f)

#undef glTexCoord3sv
#define glTexCoord3sv(v) MGLD_glTexCoord4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)

#undef glTexCoord4d
#define glTexCoord4d(s, t, r, q) MGLD_glTexCoord4f((GLfloat)(s), (GLfloat)(t), (GLfloat)(r), (GLfloat)(q))

#undef glTexCoord4dv
#define glTexCoord4dv(v) MGLD_glTexCoord4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])

#undef glTexCoord4i
#define glTexCoord4i(s, t, r, q) MGLD_glTexCoord4f((GLfloat)(s), (GLfloat)(t), (GLfloat)(r), (GLfloat)(q))

#undef glTexCoord4iv
#define glTexCoord4iv(v) MGLD_glTexCoord4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])

#undef glTexCoord4s
#define glTexCoord4s(s, t, r, q) MGLD_glTexCoord4f((GLfloat)(s), (GLfloat)(t), (GLfloat)(r), (GLfloat)(q))

#undef glTexCoord4sv
#define glTexCoord4sv(v) MGLD_glTexCoord4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])

#undef glVertex2d
#define glVertex2d(x, y) MGLD_glVertex2f((GLfloat)(x), (GLfloat)(y))

#undef glVertex2dv
#define glVertex2dv(v) MGLD_glVertex2f((GLfloat)(v)[0], (GLfloat)(v)[1])

#undef glVertex2iv
#define glVertex2iv(v) MGLD_glVertex2f((GLfloat)(v)[0], (GLfloat)(v)[1])

#undef glVertex2s
#define glVertex2s(x, y) MGLD_glVertex2f((GLfloat)(x), (GLfloat)(y))

#undef glVertex2sv
#define glVertex2sv(v) MGLD_glVertex2f((GLfloat)(v)[0], (GLfloat)(v)[1])

#undef glVertex3d
#define glVertex3d(x, y, z) MGLD_glVertex4f((GLfloat)(x), (GLfloat)(y), (GLfloat)(z), 1.f)

#undef glVertex3dv
#define glVertex3dv(v) MGLD_glVertex4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)

#undef glVertex3i
#define glVertex3i(x, y, z) MGLD_glVertex4f((GLfloat)(x), (GLfloat)(y), (GLfloat)(z), 1.f)

#undef glVertex3iv
#define glVertex3iv(v) MGLD_glVertex4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)

#undef glVertex3s
#define glVertex3s(x, y, z) MGLD_glVertex4f((GLfloat)(x), (GLfloat)(y), (GLfloat)(z), 1.f)

#undef glVertex3sv
#define glVertex3sv(v) MGLD_glVertex4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], 1.f)

#undef glVertex4d
#define glVertex4d(x, y, z, w) MGLD_glVertex4f((GLfloat)(x), (GLfloat)(y), (GLfloat)(z), (GLfloat)(w))

#undef glVertex4dv
#define glVertex4dv(v) MGLD_glVertex4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])

#undef glVertex4i
#define glVertex4i(x, y, z, w) MGLD_glVertex4f((GLfloat)(x), (GLfloat)(y), (GLfloat)(z), (GLfloat)(w))

#undef glVertex4iv
#define glVertex4iv(v) MGLD_glVertex4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])

#undef glVertex4s
#define glVertex4s(x, y, z, w) MGLD_glVertex4f((GLfloat)(x), (GLfloat)(y), (GLfloat)(z), (GLfloat)(w))

#undef glVertex4sv
#define glVertex4sv(v) MGLD_glVertex4f((GLfloat)(v)[0], (GLfloat)(v)[1], (GLfloat)(v)[2], (GLfloat)(v)[3])

#undef glLockArraysEXT
#define glLockArraysEXT(f, c) MGLD_glLockArrays(f, c)

#undef glUnlockArraysEXT
#define glUnlockArraysEXT()   MGLD_glUnlockArrays()

#undef glActiveTexture
#define glActiveTexture(unit)       MGLD_glActiveTextureARB(unit)

#undef glClientActiveTexture
#define glClientActiveTexture(unit) MGLD_glClientActiveTextureARB(unit)

/* glutInit registers a client-side atexit handler that deletes the current
 * context, because a GLUT program normally ends with exit() from a callback
 * and never reaches MiniGLClose(). Declared rather than pulled from
 * <stdlib.h> so this header does not drag a CRT header into every client TU;
 * the block is inside #ifndef MINIGL_LIBRARY_BUILD, so the DLL never sees it.
 * (Same as upstream 29.1.) */
extern int atexit(void (*func)(void));

static __inline__ void MGLD_glut_release_display(void)
{
    if (MiniGLDispatch && MiniGLDispatch->currentContext && MGLD_CTX)
    {
        MiniGLDispatch->MGLDeleteContext(MGLD_CTX);
        /* MGLDeleteContext never touches the global that every gl* wrapper
         * dereferences. */
        MGLD_CTX = 0;
    }
}

static __inline__ void MGLD_glutInit(int *argcp, char **argv)
{
    atexit(MGLD_glut_release_display);
    MiniGLDispatch->GLUTInit(argcp, argv);
}
#undef glutInit
#define glutInit MGLD_glutInit

static __inline__ void MGLD_glutInitDisplayMode(unsigned int mode)
{
    MiniGLDispatch->GLUTInitDisplayMode(mode);
}
#undef glutInitDisplayMode
#define glutInitDisplayMode MGLD_glutInitDisplayMode

static __inline__ void MGLD_glutInitWindowSize(int width, int height)
{
    MiniGLDispatch->GLUTInitWindowSize(width, height);
}
#undef glutInitWindowSize
#define glutInitWindowSize MGLD_glutInitWindowSize

static __inline__ void MGLD_glutInitWindowPosition(int x, int y)
{
    MiniGLDispatch->GLUTInitWindowPosition(x, y);
}
#undef glutInitWindowPosition
#define glutInitWindowPosition MGLD_glutInitWindowPosition

static __inline__ int MGLD_glutCreateWindow(const char *title)
{
    return MiniGLDispatch->GLUTCreateWindow(title);
}
#undef glutCreateWindow
#define glutCreateWindow MGLD_glutCreateWindow

static __inline__ void MGLD_glutMainLoop(void)
{
    MiniGLDispatch->GLUTMainLoop();
}
#undef glutMainLoop
#define glutMainLoop MGLD_glutMainLoop

#undef glutLeaveMainLoop
#define glutLeaveMainLoop() MGLD_mglExit()

static __inline__ void MGLD_glutDisplayFunc(void (*func)(void))
{
    MiniGLDispatch->GLUTDisplayFunc(func);
}
#undef glutDisplayFunc
#define glutDisplayFunc MGLD_glutDisplayFunc

static __inline__ void MGLD_glutIdleFunc(void (*func)(void))
{
    MiniGLDispatch->GLUTIdleFunc(func);
}
#undef glutIdleFunc
#define glutIdleFunc MGLD_glutIdleFunc

static __inline__ void MGLD_glutKeyboardFunc(void (*func)(unsigned char key, int x, int y))
{
    MiniGLDispatch->GLUTKeyboardFunc(func);
}
#undef glutKeyboardFunc
#define glutKeyboardFunc MGLD_glutKeyboardFunc

static __inline__ void MGLD_glutReshapeFunc(void (*func)(int width, int height))
{
    MiniGLDispatch->GLUTReshapeFunc(func);
}
#undef glutReshapeFunc
#define glutReshapeFunc MGLD_glutReshapeFunc

static __inline__ void MGLD_glutSwapBuffers(void)
{
    MiniGLDispatch->GLUTSwapBuffers();
}
#undef glutSwapBuffers
#define glutSwapBuffers MGLD_glutSwapBuffers

static __inline__ void MGLD_glutPostRedisplay(void)
{
    MiniGLDispatch->GLUTPostRedisplay();
}
#undef glutPostRedisplay
#define glutPostRedisplay MGLD_glutPostRedisplay

static __inline__ int MGLD_glutGet(GLenum state)
{
    return MiniGLDispatch->GLUTGet(state);
}
#undef glutGet
#define glutGet MGLD_glutGet

static __inline__ void MGLD_glutGameModeString(const char *string)
{
    MiniGLDispatch->GLUTGameModeString(string);
}
#undef glutGameModeString
#define glutGameModeString MGLD_glutGameModeString

static __inline__ int MGLD_glutEnterGameMode(void)
{
    return MiniGLDispatch->GLUTEnterGameMode();
}
#undef glutEnterGameMode
#define glutEnterGameMode MGLD_glutEnterGameMode

static __inline__ void MGLD_glutLeaveGameMode(void)
{
    MiniGLDispatch->GLUTLeaveGameMode();
}
#undef glutLeaveGameMode
#define glutLeaveGameMode MGLD_glutLeaveGameMode

static __inline__ int MGLD_glutGameModeGet(GLenum query)
{
    return MiniGLDispatch->GLUTGameModeGet(query);
}
#undef glutGameModeGet
#define glutGameModeGet MGLD_glutGameModeGet

static __inline__ void MGLD_glutSolidCube(GLdouble size)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidCube(size);
}
#undef glutSolidCube
#define glutSolidCube MGLD_glutSolidCube

static __inline__ void MGLD_glutSolidSphere(GLdouble radius, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidSphere(radius, slices, stacks);
}
#undef glutSolidSphere
#define glutSolidSphere MGLD_glutSolidSphere

static __inline__ void MGLD_glutSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidCone(base, height, slices, stacks);
}
#undef glutSolidCone
#define glutSolidCone MGLD_glutSolidCone

static __inline__ void MGLD_glutSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidTorus(innerRadius, outerRadius, sides, rings);
}
#undef glutSolidTorus
#define glutSolidTorus MGLD_glutSolidTorus

static __inline__ void MGLD_glutSolidDodecahedron(void)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidDodecahedron();
}
#undef glutSolidDodecahedron
#define glutSolidDodecahedron MGLD_glutSolidDodecahedron

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
