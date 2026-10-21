/*
 * GLU for every PPC DLL variant (upstream minigl.library 29.1 slots
 * 168..178 and 202): gluBuild2DMipmaps and the quadric objects.
 *
 * Written against the GLU 1.3 specification, not derived from the SGI
 * sample implementation. The geometry is drawn through the DLL's own
 * dispatch table in immediate mode, so it lands in a display list when one
 * is being compiled, exactly as GLU's does.
 *
 * Quadric conventions (GLU 1.3 6.3): an angle theta runs from the +y axis
 * towards +x, so x = r sin(theta), y = r cos(theta); texture s follows
 * theta from 0 to 1, t runs along z (cylinder: 0 at z = 0; sphere: 0 at
 * -z). GLU_OUTSIDE puts the normals and the front faces (counter-clockwise)
 * on the outside, GLU_INSIDE on the inside.
 */
#define MINIGL_LIBRARY_BUILD 1
#include "minigl_ppc_generic.h"
#include <math.h>

#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <exec/memory.h>
#include <proto/exec.h>
#ifdef WARPUP
#pragma pack(pop)
#endif

#define GLU_PI 3.14159265358979323846

struct GLUquadricObj_t {
    GLenum normals;       /* GLU_NONE, GLU_FLAT, GLU_SMOOTH */
    GLboolean texture;
    GLenum drawStyle;     /* GLU_FILL, GLU_LINE, GLU_SILHOUETTE, GLU_POINT */
    GLenum orientation;   /* GLU_OUTSIDE, GLU_INSIDE */
    MGLUfuncptr error;
};

#define T (*MGLPPCSelfTable)
#define CTX (*MGLPPCSelfTable->currentContext)

static void QuadricError(GLUquadricObj *q, GLenum code)
{
    if (q && q->error)
        ((void (*)(GLenum))q->error)(code);
}

GLUquadricObj *MGLGeneric_GLUNewQuadric(void)
{
    GLUquadricObj *q = (GLUquadricObj *)AllocVec(sizeof(*q),
                                                  MEMF_PUBLIC | MEMF_CLEAR);
    if (!q)
        return (GLUquadricObj *)0;
    q->normals = GLU_SMOOTH;
    q->texture = GL_FALSE;
    q->drawStyle = GLU_FILL;
    q->orientation = GLU_OUTSIDE;
    return q;
}

void MGLGeneric_GLUDeleteQuadric(GLUquadricObj *q)
{
    if (q)
        FreeVec(q);
}

void MGLGeneric_GLUQuadricNormals(GLUquadricObj *q, GLenum normals)
{
    if (!q) return;
    if (normals != GLU_NONE && normals != GLU_FLAT && normals != GLU_SMOOTH) {
        QuadricError(q, GLU_INVALID_ENUM);
        return;
    }
    q->normals = normals;
}

void MGLGeneric_GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords)
{
    if (q)
        q->texture = textureCoords ? GL_TRUE : GL_FALSE;
}

void MGLGeneric_GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle)
{
    if (!q) return;
    if (drawStyle != GLU_FILL && drawStyle != GLU_LINE &&
        drawStyle != GLU_SILHOUETTE && drawStyle != GLU_POINT) {
        QuadricError(q, GLU_INVALID_ENUM);
        return;
    }
    q->drawStyle = drawStyle;
}

void MGLGeneric_GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation)
{
    if (!q) return;
    if (orientation != GLU_OUTSIDE && orientation != GLU_INSIDE) {
        QuadricError(q, GLU_INVALID_ENUM);
        return;
    }
    q->orientation = orientation;
}

void MGLGeneric_GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn)
{
    if (q && which == GLU_ERROR)
        q->error = fn;
}

/* One quadric vertex: optional normal (already oriented), optional texture
 * coordinates, then the position. */
static void QuadricVertex(const GLUquadricObj *q, GLfloat nx, GLfloat ny,
                          GLfloat nz, GLfloat s, GLfloat t, GLfloat x,
                          GLfloat y, GLfloat z)
{
    GLcontext c = CTX;
    if (q->normals != GLU_NONE) {
        if (q->orientation == GLU_INSIDE)
            T.GLNormal3f(c, -nx, -ny, -nz);
        else
            T.GLNormal3f(c, nx, ny, nz);
    }
    if (q->texture)
        T.GLTexCoord2f(c, s, t);
    T.GLVertex4f(c, x, y, z, 1.0f);
}

/*
 * gluCylinder. Facet normals for GLU_FLAT are taken at the facet's angular
 * centre; smooth normals tilt by the cone slope (base - top) / height.
 */
void MGLGeneric_GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks)
{
    GLcontext c = CTX;
    GLdouble length, zNormal, xyNormal;
    GLint i, j;

    if (!q) return;
    if (base < 0.0 || top < 0.0 || height < 0.0 || slices < 2 || stacks < 1) {
        QuadricError(q, GLU_INVALID_VALUE);
        return;
    }
    length = sqrt((base - top) * (base - top) + height * height);
    if (length == 0.0) length = 1.0;
    zNormal = (base - top) / length;
    xyNormal = height / length;

    if (q->drawStyle == GLU_FILL) {
        for (j = 0; j < stacks; ++j) {
            GLdouble z0 = height * j / stacks, z1 = height * (j + 1) / stacks;
            GLdouble r0 = base + (top - base) * j / stacks;
            GLdouble r1 = base + (top - base) * (j + 1) / stacks;
            if (q->normals == GLU_FLAT) {
                T.GLBegin(c, GL_QUADS);
                for (i = 0; i < slices; ++i) {
                    GLdouble a0 = 2.0 * GLU_PI * i / slices;
                    GLdouble a1 = 2.0 * GLU_PI * (i + 1) / slices;
                    GLdouble am = 0.5 * (a0 + a1);
                    GLfloat nx = (GLfloat)(sin(am) * xyNormal);
                    GLfloat ny = (GLfloat)(cos(am) * xyNormal);
                    GLfloat nz = (GLfloat)zNormal;
                    /* (i,j) (i,j+1) (i+1,j+1) (i+1,j): +z then +theta,
                     * counter-clockwise from outside; reversed inside. */
                    GLdouble a[4] = { a0, a0, a1, a1 };
                    GLdouble zz[4] = { z0, z1, z1, z0 };
                    GLdouble rr[4] = { r0, r1, r1, r0 };
                    GLint k;
                    for (k = 0; k < 4; ++k) {
                        GLint m = q->orientation == GLU_INSIDE ? 3 - k : k;
                        QuadricVertex(q, nx, ny, nz,
                                      (GLfloat)((m < 2 ? i : i + 1) / (GLdouble)slices),
                                      (GLfloat)((m == 0 || m == 3 ? j : j + 1) / (GLdouble)stacks),
                                      (GLfloat)(rr[m] * sin(a[m])),
                                      (GLfloat)(rr[m] * cos(a[m])), (GLfloat)zz[m]);
                    }
                }
                T.GLEnd(c);
            } else {
                T.GLBegin(c, GL_QUAD_STRIP);
                for (i = 0; i <= slices; ++i) {
                    GLdouble a = 2.0 * GLU_PI * (i == slices ? 0 : i) / slices;
                    GLfloat sa = (GLfloat)sin(a), ca = (GLfloat)cos(a);
                    GLfloat nx = (GLfloat)(sa * xyNormal);
                    GLfloat ny = (GLfloat)(ca * xyNormal);
                    GLfloat s = (GLfloat)i / (GLfloat)slices;
                    GLfloat t0 = (GLfloat)j / (GLfloat)stacks;
                    GLfloat t1 = (GLfloat)(j + 1) / (GLfloat)stacks;
                    if (q->orientation == GLU_INSIDE) {
                        QuadricVertex(q, nx, ny, (GLfloat)zNormal, s, t1,
                                      (GLfloat)r1 * sa, (GLfloat)r1 * ca, (GLfloat)z1);
                        QuadricVertex(q, nx, ny, (GLfloat)zNormal, s, t0,
                                      (GLfloat)r0 * sa, (GLfloat)r0 * ca, (GLfloat)z0);
                    } else {
                        QuadricVertex(q, nx, ny, (GLfloat)zNormal, s, t0,
                                      (GLfloat)r0 * sa, (GLfloat)r0 * ca, (GLfloat)z0);
                        QuadricVertex(q, nx, ny, (GLfloat)zNormal, s, t1,
                                      (GLfloat)r1 * sa, (GLfloat)r1 * ca, (GLfloat)z1);
                    }
                }
                T.GLEnd(c);
            }
        }
        return;
    }

    if (q->drawStyle == GLU_POINT) {
        T.GLBegin(c, GL_POINTS);
        for (j = 0; j <= stacks; ++j) {
            GLdouble z = height * j / stacks;
            GLdouble r = base + (top - base) * j / stacks;
            for (i = 0; i < slices; ++i) {
                GLdouble a = 2.0 * GLU_PI * i / slices;
                QuadricVertex(q, (GLfloat)(sin(a) * xyNormal),
                              (GLfloat)(cos(a) * xyNormal), (GLfloat)zNormal,
                              (GLfloat)i / (GLfloat)slices,
                              (GLfloat)j / (GLfloat)stacks,
                              (GLfloat)(r * sin(a)), (GLfloat)(r * cos(a)),
                              (GLfloat)z);
            }
        }
        T.GLEnd(c);
        return;
    }

    /* GLU_LINE: every stack ring and every slice line. GLU_SILHOUETTE: the
     * slice lines and the two end rings only. */
    for (j = 0; j <= stacks; ++j) {
        GLdouble z = height * j / stacks;
        GLdouble r = base + (top - base) * j / stacks;
        if (q->drawStyle == GLU_SILHOUETTE && j != 0 && j != stacks)
            continue;
        T.GLBegin(c, GL_LINE_LOOP);
        for (i = 0; i < slices; ++i) {
            GLdouble a = 2.0 * GLU_PI * i / slices;
            QuadricVertex(q, (GLfloat)(sin(a) * xyNormal),
                          (GLfloat)(cos(a) * xyNormal), (GLfloat)zNormal,
                          (GLfloat)i / (GLfloat)slices,
                          (GLfloat)j / (GLfloat)stacks,
                          (GLfloat)(r * sin(a)), (GLfloat)(r * cos(a)),
                          (GLfloat)z);
        }
        T.GLEnd(c);
    }
    for (i = 0; i < slices; ++i) {
        GLdouble a = 2.0 * GLU_PI * i / slices;
        T.GLBegin(c, GL_LINE_STRIP);
        for (j = 0; j <= stacks; ++j) {
            GLdouble z = height * j / stacks;
            GLdouble r = base + (top - base) * j / stacks;
            QuadricVertex(q, (GLfloat)(sin(a) * xyNormal),
                          (GLfloat)(cos(a) * xyNormal), (GLfloat)zNormal,
                          (GLfloat)i / (GLfloat)slices,
                          (GLfloat)j / (GLfloat)stacks,
                          (GLfloat)(r * sin(a)), (GLfloat)(r * cos(a)),
                          (GLfloat)z);
        }
        T.GLEnd(c);
    }
}

/* Point on the unit sphere at angle theta and polar angle phi (0 at +z). */
static void SpherePoint(GLdouble theta, GLdouble phi, GLfloat *v)
{
    v[0] = (GLfloat)(sin(phi) * sin(theta));
    v[1] = (GLfloat)(sin(phi) * cos(theta));
    v[2] = (GLfloat)cos(phi);
}

void MGLGeneric_GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks)
{
    GLcontext c = CTX;
    GLfloat r;
    GLint i, j;

    if (!q) return;
    if (radius < 0.0 || slices < 2 || stacks < 1) {
        QuadricError(q, GLU_INVALID_VALUE);
        return;
    }
    r = (GLfloat)radius;

    if (q->drawStyle == GLU_FILL) {
        for (j = 0; j < stacks; ++j) {
            GLdouble p0 = GLU_PI * j / stacks, p1 = GLU_PI * (j + 1) / stacks;
            GLfloat t0 = 1.0f - (GLfloat)j / (GLfloat)stacks;
            GLfloat t1 = 1.0f - (GLfloat)(j + 1) / (GLfloat)stacks;
            T.GLBegin(c, q->normals == GLU_FLAT ? GL_QUADS : GL_QUAD_STRIP);
            for (i = 0; i <= slices; ++i) {
                GLdouble a = 2.0 * GLU_PI * (i == slices ? 0 : i) / slices;
                GLfloat s = (GLfloat)i / (GLfloat)slices;
                GLfloat up[3], down[3];
                if (q->normals == GLU_FLAT) {
                    /* Facet (i, i+1) x (j, j+1), normal at its centre,
                     * corners lower, upper, next upper, next lower: +z
                     * then +theta, counter-clockwise from outside. */
                    GLdouble a1 = 2.0 * GLU_PI * (i + 1) / slices;
                    GLfloat n[3], v[4][3], tex[4][2];
                    GLint k;
                    if (i == slices) break;
                    SpherePoint(0.5 * (a + a1), 0.5 * (p0 + p1), n);
                    SpherePoint(a, p1, v[0]); tex[0][0] = s; tex[0][1] = t1;
                    SpherePoint(a, p0, v[1]); tex[1][0] = s; tex[1][1] = t0;
                    SpherePoint(a1, p0, v[2]);
                    tex[2][0] = (GLfloat)(i + 1) / (GLfloat)slices; tex[2][1] = t0;
                    SpherePoint(a1, p1, v[3]);
                    tex[3][0] = tex[2][0]; tex[3][1] = t1;
                    for (k = 0; k < 4; ++k) {
                        GLint m = q->orientation == GLU_INSIDE ? 3 - k : k;
                        QuadricVertex(q, n[0], n[1], n[2], tex[m][0], tex[m][1],
                                      r * v[m][0], r * v[m][1], r * v[m][2]);
                    }
                    continue;
                }
                SpherePoint(a, p0, up);
                SpherePoint(a, p1, down);
                /* Lower then upper per column: +z then +theta, which is
                 * counter-clockwise from outside. */
                if (q->orientation == GLU_INSIDE) {
                    QuadricVertex(q, up[0], up[1], up[2], s, t0,
                                  r * up[0], r * up[1], r * up[2]);
                    QuadricVertex(q, down[0], down[1], down[2], s, t1,
                                  r * down[0], r * down[1], r * down[2]);
                } else {
                    QuadricVertex(q, down[0], down[1], down[2], s, t1,
                                  r * down[0], r * down[1], r * down[2]);
                    QuadricVertex(q, up[0], up[1], up[2], s, t0,
                                  r * up[0], r * up[1], r * up[2]);
                }
            }
            T.GLEnd(c);
        }
        return;
    }

    if (q->drawStyle == GLU_POINT) {
        T.GLBegin(c, GL_POINTS);
        for (j = 0; j <= stacks; ++j)
            for (i = 0; i < slices; ++i) {
                GLfloat v[3];
                SpherePoint(2.0 * GLU_PI * i / slices, GLU_PI * j / stacks, v);
                QuadricVertex(q, v[0], v[1], v[2], (GLfloat)i / (GLfloat)slices,
                              1.0f - (GLfloat)j / (GLfloat)stacks,
                              r * v[0], r * v[1], r * v[2]);
                if (j == 0 || j == stacks)
                    break;    /* the poles are one point each */
            }
        T.GLEnd(c);
        return;
    }

    /* GLU_LINE and GLU_SILHOUETTE: latitude rings and longitude arcs. */
    for (j = 1; j < stacks; ++j) {
        T.GLBegin(c, GL_LINE_LOOP);
        for (i = 0; i < slices; ++i) {
            GLfloat v[3];
            SpherePoint(2.0 * GLU_PI * i / slices, GLU_PI * j / stacks, v);
            QuadricVertex(q, v[0], v[1], v[2], (GLfloat)i / (GLfloat)slices,
                          1.0f - (GLfloat)j / (GLfloat)stacks,
                          r * v[0], r * v[1], r * v[2]);
        }
        T.GLEnd(c);
    }
    for (i = 0; i < slices; ++i) {
        T.GLBegin(c, GL_LINE_STRIP);
        for (j = 0; j <= stacks; ++j) {
            GLfloat v[3];
            SpherePoint(2.0 * GLU_PI * i / slices, GLU_PI * j / stacks, v);
            QuadricVertex(q, v[0], v[1], v[2], (GLfloat)i / (GLfloat)slices,
                          1.0f - (GLfloat)j / (GLfloat)stacks,
                          r * v[0], r * v[1], r * v[2]);
        }
        T.GLEnd(c);
    }
}

/* gluDisk: the z = 0 plane, normal +z outside (-z inside); texture
 * coordinates map the outer radius onto [0,1]. */
static void DiskVertex(const GLUquadricObj *q, GLdouble radius, GLdouble a,
                       GLdouble outer)
{
    GLfloat x = (GLfloat)(radius * sin(a)), y = (GLfloat)(radius * cos(a));
    QuadricVertex(q, 0.0f, 0.0f, 1.0f,
                  (GLfloat)(0.5 + radius * sin(a) / (2.0 * outer)),
                  (GLfloat)(0.5 + radius * cos(a) / (2.0 * outer)),
                  x, y, 0.0f);
}

void MGLGeneric_GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    GLcontext c = CTX;
    GLint i, l;

    if (!q) return;
    if (inner < 0.0 || outer < 0.0 || inner > outer || slices < 2 || loops < 1) {
        QuadricError(q, GLU_INVALID_VALUE);
        return;
    }
    if (outer == 0.0)
        return;

    if (q->drawStyle == GLU_FILL) {
        for (l = 0; l < loops; ++l) {
            GLdouble r0 = inner + (outer - inner) * l / loops;
            GLdouble r1 = inner + (outer - inner) * (l + 1) / loops;
            T.GLBegin(c, GL_QUAD_STRIP);
            for (i = 0; i <= slices; ++i) {
                GLdouble a = 2.0 * GLU_PI * (i == slices ? 0 : i) / slices;
                /* Outer then inner: counter-clockwise seen from +z. */
                if (q->orientation == GLU_INSIDE) {
                    DiskVertex(q, r0, a, outer);
                    DiskVertex(q, r1, a, outer);
                } else {
                    DiskVertex(q, r1, a, outer);
                    DiskVertex(q, r0, a, outer);
                }
            }
            T.GLEnd(c);
        }
        return;
    }

    if (q->drawStyle == GLU_POINT) {
        T.GLBegin(c, GL_POINTS);
        for (l = 0; l <= loops; ++l)
            for (i = 0; i < slices; ++i)
                DiskVertex(q, inner + (outer - inner) * l / loops,
                           2.0 * GLU_PI * i / slices, outer);
        T.GLEnd(c);
        return;
    }

    /* GLU_LINE: every ring and every spoke. GLU_SILHOUETTE: the boundary
     * rings only (the inner one when there is a hole). */
    for (l = 0; l <= loops; ++l) {
        GLdouble r = inner + (outer - inner) * l / loops;
        if (q->drawStyle == GLU_SILHOUETTE && l != 0 && l != loops)
            continue;
        if (r == 0.0)
            continue;
        T.GLBegin(c, GL_LINE_LOOP);
        for (i = 0; i < slices; ++i)
            DiskVertex(q, r, 2.0 * GLU_PI * i / slices, outer);
        T.GLEnd(c);
    }
    if (q->drawStyle == GLU_LINE)
        for (i = 0; i < slices; ++i) {
            GLdouble a = 2.0 * GLU_PI * i / slices;
            T.GLBegin(c, GL_LINE_STRIP);
            for (l = 0; l <= loops; ++l)
                DiskVertex(q, inner + (outer - inner) * l / loops, a, outer);
            T.GLEnd(c);
        }
}

/* ---- gluBuild2DMipmaps ------------------------------------------------ */

static GLint MipComponents(GLenum format)
{
    switch (format) {
    case GL_RGBA: case MGL_UBYTE_BGRA: case MGL_UBYTE_ARGB: return 4;
    case GL_RGB: return 3;
    case GL_LUMINANCE_ALPHA: return 2;
    case GL_ALPHA: case GL_LUMINANCE: return 1;
    default: return 0;
    }
}

static GLsizei FloorPowerOfTwo(GLsizei value)
{
    GLsizei p = 1;
    while (p * 2 <= value)
        p *= 2;
    return p;
}

/* Box filter: each destination texel averages the source texels whose
 * centres fall in its footprint (at least one). Handles both shrinking and
 * the GLU power-of-two rescale. */
static void MipScale(const GLubyte *src, GLsizei sw, GLsizei sh, ULONG srcStride,
                     GLubyte *dst, GLsizei dw, GLsizei dh, GLint comps)
{
    GLsizei x, y;
    GLint k;
    for (y = 0; y < dh; ++y) {
        GLsizei y0 = (GLsizei)((ULONG)y * (ULONG)sh / (ULONG)dh);
        GLsizei y1 = (GLsizei)(((ULONG)y + 1UL) * (ULONG)sh / (ULONG)dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (x = 0; x < dw; ++x) {
            GLsizei x0 = (GLsizei)((ULONG)x * (ULONG)sw / (ULONG)dw);
            GLsizei x1 = (GLsizei)(((ULONG)x + 1UL) * (ULONG)sw / (ULONG)dw);
            ULONG sum[4] = { 0, 0, 0, 0 }, n = 0;
            GLsizei sx, sy;
            if (x1 <= x0) x1 = x0 + 1;
            for (sy = y0; sy < y1; ++sy)
                for (sx = x0; sx < x1; ++sx) {
                    const GLubyte *p = src + (ULONG)sy * srcStride +
                                       (ULONG)sx * (ULONG)comps;
                    for (k = 0; k < comps; ++k)
                        sum[k] += p[k];
                    ++n;
                }
            for (k = 0; k < comps; ++k)
                dst[((ULONG)y * (ULONG)dw + (ULONG)x) * (ULONG)comps + (ULONG)k] =
                    (GLubyte)((sum[k] + n / 2UL) / n);
        }
    }
}

/*
 * GLU 1.3 4.2: rescale to a power of two no larger than GL_MAX_TEXTURE_SIZE,
 * then define every level down to 1x1 with a box filter. The rescale rounds
 * down to a power of two (GLU allows either way; down never needs more
 * memory than the source). Only
 * GL_UNSIGNED_BYTE sources (the type every backend here uploads) are
 * accepted. The source honours GL_UNPACK_ALIGNMENT; levels upload tightly
 * packed with the alignment temporarily 1.
 */
GLint MGLGeneric_GLUBuild2DMipmaps(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data)
{
    GLint comps = MipComponents(format), maxSize = 0, unpack = 4, level = 0;
    ULONG srcStride;
    GLsizei w, h;
    GLubyte *cur, *next;

    if (width < 1 || height < 1)
        return GLU_INVALID_VALUE;
    if (!comps || type != GL_UNSIGNED_BYTE || target != GL_TEXTURE_2D)
        return GLU_INVALID_ENUM;
    if (!data)
        return GLU_INVALID_VALUE;

    MGLPPCSelfTable->GLGetIntegerv(context, GL_MAX_TEXTURE_SIZE, &maxSize);
    if (maxSize < 64) maxSize = 256;
    MGLPPCSelfTable->GLGetIntegerv(context, GL_UNPACK_ALIGNMENT, &unpack);
    if (unpack != 1 && unpack != 2 && unpack != 4 && unpack != 8) unpack = 4;
    srcStride = ((ULONG)width * (ULONG)comps + (ULONG)unpack - 1UL) &
                ~((ULONG)unpack - 1UL);

    w = FloorPowerOfTwo(width);
    h = FloorPowerOfTwo(height);
    if (w > maxSize) w = maxSize;
    if (h > maxSize) h = maxSize;

    cur = (GLubyte *)AllocVec((ULONG)w * (ULONG)h * (ULONG)comps, MEMF_PUBLIC);
    if (!cur)
        return GLU_OUT_OF_MEMORY;
    MipScale((const GLubyte *)data, width, height, srcStride, cur, w, h, comps);

    MGLPPCSelfTable->GLPixelStorei(context, GL_UNPACK_ALIGNMENT, 1);
    for (;;) {
        GLsizei nw, nh;
        MGLPPCTexImage2D(context, target, level, internalFormat, w, h, 0,
                         format, type, cur);
        if (w == 1 && h == 1)
            break;
        nw = w > 1 ? w / 2 : 1;
        nh = h > 1 ? h / 2 : 1;
        next = (GLubyte *)AllocVec((ULONG)nw * (ULONG)nh * (ULONG)comps,
                                   MEMF_PUBLIC);
        if (!next) {
            FreeVec(cur);
            MGLPPCSelfTable->GLPixelStorei(context, GL_UNPACK_ALIGNMENT, unpack);
            return GLU_OUT_OF_MEMORY;
        }
        MipScale(cur, w, h, (ULONG)w * (ULONG)comps, next, nw, nh, comps);
        FreeVec(cur);
        cur = next;
        w = nw;
        h = nh;
        ++level;
    }
    FreeVec(cur);
    MGLPPCSelfTable->GLPixelStorei(context, GL_UNPACK_ALIGNMENT, unpack);
    return 0;
}
