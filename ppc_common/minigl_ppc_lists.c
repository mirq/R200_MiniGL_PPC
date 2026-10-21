/*
 * Display lists (upstream minigl.library 29.1 slots 162..167), shared by
 * every PPC DLL variant.
 *
 * Recording swaps the compilable slots of the DLL's own dispatch table for
 * recording thunks between glNewList and glEndList (generated:
 * minigl_ppc_lists_thunks.c, tools/generate_list_thunks.py). Clients re-read
 * the slot on every call, so outside compilation nothing pays for lists.
 * Replay calls the real entry points saved in ListReal. Slots GL 1.1 does
 * not compile -- queries, client state, pixel store, glFinish/glFlush,
 * texture object creation, context management -- are never swapped and so
 * execute immediately, as specified.
 *
 * glArrayElement/glDrawArrays/glDrawElements/glMultiDrawArrays compile to
 * the immediate commands they generate, dereferencing the client arrays at
 * compile time (GL 1.1 5.4). That needs the backend to describe its arrays
 * (MGLPPCClientArray); a backend that cannot reports GL_INVALID_OPERATION
 * for an array draw inside a list. glTexImage2D/glTexSubImage2D are not
 * compiled: they execute immediately even under GL_COMPILE (documented
 * deviation; their table slots may be ABI-bridge trampolines).
 *
 * One list namespace per DLL, matching the one current context the DLL
 * supports; MGLGeneric_ListsContextGone frees it with the context.
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

#define LIST_MAX_NESTING 64UL   /* GL_MAX_LIST_NESTING */
#define LIST_MAX_NAMES   65536UL

struct ListSlot {
    ULONG *words;       /* NULL: reserved by glGenLists or empty */
    ULONG count;
    ULONG reserved;     /* the name is in use (glIsList) */
};

static MGLDispatchTable ListReal;       /* real entry points while compiling */
static struct ListSlot *ListSlots;
static ULONG ListSlotCount;
static GLuint ListCompiling;            /* name being compiled, 0 if none */
static GLenum ListMode;
static ULONG *ListBuffer;
static ULONG ListUsed, ListCapacity;
static BOOL ListOverflow;               /* an append failed: GL_OUT_OF_MEMORY */
static BOOL ListRealValid;

static BOOL ListExecuting(void)
{
    return ListMode == GL_COMPILE_AND_EXECUTE;
}

/* Reserves room for one command; NULL (and the overflow flag) when memory
 * runs out, in which case the command is dropped from the list. */
static ULONG *ListAppend(ULONG op, ULONG words)
{
    ULONG need = ListUsed + 1UL + words;
    ULONG *slot;

    if (ListOverflow)
        return NULL;
    if (need > ListCapacity) {
        ULONG capacity = ListCapacity ? ListCapacity * 2UL : 256UL;
        ULONG *grown;
        while (capacity < need)
            capacity *= 2UL;
        grown = (ULONG *)AllocVec(capacity * sizeof(ULONG), MEMF_PUBLIC);
        if (!grown) {
            ListOverflow = TRUE;
            return NULL;
        }
        if (ListBuffer) {
            CopyMem(ListBuffer, grown, ListUsed * sizeof(ULONG));
            FreeVec(ListBuffer);
        }
        ListBuffer = grown;
        ListCapacity = capacity;
    }
    slot = ListBuffer + ListUsed;
    *slot = op | (words << 16);
    ListUsed = need;
    return slot + 1;
}

/* Value counts of the pname-dependent vector arguments (GL 1.1 tables). */
static ULONG ListFogCount(GLenum pname)
{
    return pname == GL_FOG_COLOR ? 4UL : 1UL;
}

static ULONG ListTexEnvCount(GLenum pname)
{
    return pname == GL_TEXTURE_ENV_COLOR ? 4UL : 1UL;
}

static ULONG ListLightCount(GLenum pname)
{
    switch (pname) {
    case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_POSITION:
        return 4UL;
    case GL_SPOT_DIRECTION:
        return 3UL;
    default:
        return 1UL;
    }
}

static ULONG ListMaterialCount(GLenum pname)
{
    switch (pname) {
    case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_EMISSION:
    case GL_AMBIENT_AND_DIFFUSE:
        return 4UL;
    default:
        return 1UL;
    }
}

static ULONG ListLightModelCount(GLenum pname)
{
    return pname == GL_LIGHT_MODEL_AMBIENT ? 4UL : 1UL;
}

/* Word-level stores and loads. Floats and doubles move as raw bits, so a
 * replayed value is bit-identical to the recorded one; doubles are copied
 * out through a local so no FP load ever sees a 4-byte-aligned double. */
static ULONG *ListStoreFloat(ULONG *w, GLfloat value)
{
    union { GLfloat f; ULONG u; } bits;
    bits.f = value;
    *w++ = bits.u;
    return w;
}

static ULONG *ListStoreDouble(ULONG *w, GLdouble value)
{
    union { GLdouble d; ULONG u[2]; } bits;
    bits.d = value;
    *w++ = bits.u[0];
    *w++ = bits.u[1];
    return w;
}

static const ULONG *ListLoadFloat(const ULONG *r, GLfloat *value)
{
    union { GLfloat f; ULONG u; } bits;
    bits.u = *r++;
    *value = bits.f;
    return r;
}

static const ULONG *ListLoadDouble(const ULONG *r, GLdouble *value)
{
    union { GLdouble d; ULONG u[2]; } bits;
    bits.u[0] = *r++;
    bits.u[1] = *r++;
    *value = bits.d;
    return r;
}

static ULONG *ListStore_f(ULONG *w, const GLfloat *v, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        w = ListStoreFloat(w, v[i]);
    return w;
}

static ULONG *ListStore_d(ULONG *w, const GLdouble *v, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        w = ListStoreDouble(w, v[i]);
    return w;
}

static ULONG *ListStore_b(ULONG *w, const GLubyte *v, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        *w++ = v[i];
    return w;
}

static ULONG *ListStore_w(ULONG *w, const GLboolean *v, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        *w++ = (ULONG)v[i];
    return w;
}

static const ULONG *ListLoad_f(const ULONG *r, GLfloat *v, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        r = ListLoadFloat(r, &v[i]);
    return r;
}

static const ULONG *ListLoad_d(const ULONG *r, GLdouble *v, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        r = ListLoadDouble(r, &v[i]);
    return r;
}

static const ULONG *ListLoad_b(const ULONG *r, GLubyte *v, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        v[i] = (GLubyte)*r++;
    return r;
}

static const ULONG *ListLoad_w(const ULONG *r, GLboolean *v, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        v[i] = (GLboolean)*r++;
    return r;
}

#include "minigl_ppc_lists_thunks.c"

/* Commands compiled by hand, numbered after the generated ones. */
enum {
    LIST_OP_CALLLIST = LIST_OP_FIRST_MANUAL
};

static void ListRecordFloats(ULONG op, ULONG count, const GLfloat *v)
{
    ULONG *w = ListAppend(op, count);
    if (w)
        ListStore_f(w, v, count);
}

/* ---- array draws ------------------------------------------------------ */

/* One component of element `index` of an array, as a float. normalize maps
 * the integer types onto [0,1]/[-1,1] the way GL does for colours and
 * normals. */
static GLfloat ListFetch(const MGLPPCArray *array, ULONG index, ULONG component,
                         BOOL normalize)
{
    const UBYTE *base = (const UBYTE *)array->pointer + index * (ULONG)array->stride;

    switch (array->type) {
    case GL_FLOAT: {
        GLfloat v;
        CopyMem((APTR)(base + component * 4UL), &v, sizeof(v));
        return v;
    }
    case GL_DOUBLE: {
        GLdouble v;
        CopyMem((APTR)(base + component * 8UL), &v, sizeof(v));
        return (GLfloat)v;
    }
    case GL_UNSIGNED_BYTE: {
        GLubyte v = base[component];
        return normalize ? (GLfloat)v / 255.0f : (GLfloat)v;
    }
    case GL_BYTE: {
        GLbyte v = (GLbyte)base[component];
        return normalize ? (2.0f * (GLfloat)v + 1.0f) / 255.0f : (GLfloat)v;
    }
    case GL_UNSIGNED_SHORT: {
        GLushort v;
        CopyMem((APTR)(base + component * 2UL), &v, sizeof(v));
        return normalize ? (GLfloat)v / 65535.0f : (GLfloat)v;
    }
    case GL_SHORT: {
        GLshort v;
        CopyMem((APTR)(base + component * 2UL), &v, sizeof(v));
        return normalize ? (2.0f * (GLfloat)v + 1.0f) / 65535.0f : (GLfloat)v;
    }
    case GL_UNSIGNED_INT: {
        GLuint v;
        CopyMem((APTR)(base + component * 4UL), &v, sizeof(v));
        return normalize ? (GLfloat)((GLdouble)v / 4294967295.0) : (GLfloat)v;
    }
    case GL_INT: {
        GLint v;
        CopyMem((APTR)(base + component * 4UL), &v, sizeof(v));
        return normalize ? (GLfloat)((2.0 * (GLdouble)v + 1.0) / 4294967295.0)
                         : (GLfloat)v;
    }
    case MGL_UBYTE_ARGB:    /* packed A,R,G,B bytes; component is R,G,B,A */
        return (GLfloat)base[(component + 1UL) & 3UL] / 255.0f;
    case MGL_UBYTE_BGRA:    /* packed B,G,R,A bytes */
        return (GLfloat)base[component == 3UL ? 3UL : 2UL - component] / 255.0f;
    default:
        return 0.0f;
    }
}

struct ListArrays {
    MGLPPCArray vertex, color, normal, texcoord0, texcoord1;
};

static BOOL ListGetArrays(GLcontext context, struct ListArrays *arrays)
{
    return MGLPPCClientArray(context, GL_VERTEX_ARRAY, &arrays->vertex) &&
           MGLPPCClientArray(context, GL_COLOR_ARRAY, &arrays->color) &&
           MGLPPCClientArray(context, GL_NORMAL_ARRAY, &arrays->normal) &&
           MGLPPCClientArray(context, GL_TEXTURE0_ARB, &arrays->texcoord0) &&
           MGLPPCClientArray(context, GL_TEXTURE1_ARB, &arrays->texcoord1);
}

/* Records the immediate commands element `index` generates (GL 1.1 2.8:
 * normal, colour, texture coordinates, then the vertex last). */
static void ListRecordElement(const struct ListArrays *arrays, ULONG index)
{
    GLfloat v[4];
    ULONG i;

    if (arrays->normal.enabled) {
        for (i = 0; i < 3UL; ++i)
            v[i] = ListFetch(&arrays->normal, index, i, TRUE);
        ListRecordFloats(LIST_OP_GLNORMAL3F, 3UL, v);
    }
    if (arrays->color.enabled) {
        v[3] = 1.0f;
        for (i = 0; i < (ULONG)arrays->color.size && i < 4UL; ++i)
            v[i] = ListFetch(&arrays->color, index, i, TRUE);
        ListRecordFloats(LIST_OP_GLCOLOR4F, 4UL, v);
    }
    if (arrays->texcoord0.enabled) {
        v[0] = v[1] = v[2] = 0.0f;
        v[3] = 1.0f;
        for (i = 0; i < (ULONG)arrays->texcoord0.size && i < 4UL; ++i)
            v[i] = ListFetch(&arrays->texcoord0, index, i, FALSE);
        ListRecordFloats(LIST_OP_GLTEXCOORD4F, 4UL, v);
    }
    if (arrays->texcoord1.enabled) {
        ULONG *w = ListAppend(LIST_OP_GLMULTITEXCOORD2FARB, 3UL);
        if (w) {
            *w++ = GL_TEXTURE1_ARB;
            w = ListStoreFloat(w, ListFetch(&arrays->texcoord1, index, 0, FALSE));
            ListStoreFloat(w, arrays->texcoord1.size > 1 ?
                           ListFetch(&arrays->texcoord1, index, 1, FALSE) : 0.0f);
        }
    }
    if (arrays->vertex.enabled) {
        v[2] = 0.0f;
        v[3] = 1.0f;
        for (i = 0; i < (ULONG)arrays->vertex.size && i < 4UL; ++i)
            v[i] = ListFetch(&arrays->vertex, index, i, FALSE);
        ListRecordFloats(LIST_OP_GLVERTEX4F, 4UL, v);
    }
}

static void ListRecordWord(ULONG op, ULONG value)
{
    ULONG *w = ListAppend(op, 1UL);
    if (w)
        *w = value;
}

static BOOL ListArraysOrError(GLcontext context, struct ListArrays *arrays)
{
    if (ListGetArrays(context, arrays))
        return TRUE;
    MGLPPCSetError(context, GL_INVALID_OPERATION);
    return FALSE;
}

static void ListRec_GLArrayElement(GLcontext context, GLint i)
{
    struct ListArrays arrays;
    if (i >= 0 && ListArraysOrError(context, &arrays))
        ListRecordElement(&arrays, (ULONG)i);
    if (ListExecuting())
        ListReal.GLArrayElement(context, i);
}

static void ListRecordDrawArrays(GLcontext context, GLenum mode, GLint first,
                                 GLsizei count)
{
    struct ListArrays arrays;
    GLsizei i;
    if (first < 0 || count <= 0 || !ListArraysOrError(context, &arrays))
        return;
    ListRecordWord(LIST_OP_GLBEGIN, mode);
    for (i = 0; i < count; ++i)
        ListRecordElement(&arrays, (ULONG)(first + i));
    ListAppend(LIST_OP_GLEND, 0);
}

static void ListRec_GLDrawArrays(GLcontext context, GLenum mode, GLint first,
                                 GLsizei count)
{
    ListRecordDrawArrays(context, mode, first, count);
    if (ListExecuting())
        ListReal.GLDrawArrays(context, mode, first, count);
}

static void ListRec_GLMultiDrawArrays(GLcontext context, GLenum mode,
                                      const GLint *first, const GLsizei *count,
                                      GLsizei primcount)
{
    GLsizei i;
    if (first && count)
        for (i = 0; i < primcount; ++i)
            ListRecordDrawArrays(context, mode, first[i], count[i]);
    if (ListExecuting())
        ListReal.GLMultiDrawArrays(context, mode, first, count, primcount);
}

static void ListRec_GLDrawElements(GLcontext context, GLenum mode,
                                   GLsizei count, GLenum type,
                                   const GLvoid *indices)
{
    struct ListArrays arrays;
    GLsizei i;
    if (indices && count > 0 &&
        (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT ||
         type == GL_UNSIGNED_INT) &&
        ListArraysOrError(context, &arrays)) {
        ListRecordWord(LIST_OP_GLBEGIN, mode);
        for (i = 0; i < count; ++i) {
            ULONG index;
            if (type == GL_UNSIGNED_BYTE)
                index = ((const GLubyte *)indices)[i];
            else if (type == GL_UNSIGNED_SHORT)
                index = ((const GLushort *)indices)[i];
            else
                index = ((const GLuint *)indices)[i];
            ListRecordElement(&arrays, index);
        }
        ListAppend(LIST_OP_GLEND, 0);
    }
    if (ListExecuting())
        ListReal.GLDrawElements(context, mode, count, type, indices);
}

static void ListReplay(GLcontext context, GLuint list, ULONG depth);

/* A nested glCallList compiles to a reference: GL resolves the name when
 * the outer list executes, not when it is compiled. */
static void ListRec_GLCallList(GLcontext context, GLuint list)
{
    ListRecordWord(LIST_OP_CALLLIST, list);
    if (ListExecuting())
        ListReplay(context, list, 1);
}

static void ListInstall(void)
{
    MGLDispatchTable *table = MGLPPCSelfTable;
    ListReal = *table;
    ListRealValid = TRUE;
    ListInstallGenerated(table);
    table->GLArrayElement = ListRec_GLArrayElement;
    table->GLDrawArrays = ListRec_GLDrawArrays;
    table->GLMultiDrawArrays = ListRec_GLMultiDrawArrays;
    table->GLDrawElements = ListRec_GLDrawElements;
    table->GLCallList = ListRec_GLCallList;
}

static void ListUninstall(void)
{
    *MGLPPCSelfTable = ListReal;
}

/* ---- names ------------------------------------------------------------ */

static void ListFreeSlot(GLuint list)
{
    if (list < ListSlotCount && ListSlots[list].words) {
        FreeVec(ListSlots[list].words);
        ListSlots[list].words = NULL;
        ListSlots[list].count = 0;
    }
}

static BOOL ListGrowNames(ULONG names)
{
    struct ListSlot *grown;
    ULONG count = ListSlotCount ? ListSlotCount : 64UL;
    if (names > LIST_MAX_NAMES)
        return FALSE;
    while (count < names)
        count *= 2UL;
    if (count > LIST_MAX_NAMES)
        count = LIST_MAX_NAMES;
    if (count <= ListSlotCount)
        return TRUE;
    grown = (struct ListSlot *)AllocVec(count * sizeof(*grown),
                                        MEMF_PUBLIC | MEMF_CLEAR);
    if (!grown)
        return FALSE;
    if (ListSlots) {
        CopyMem(ListSlots, grown, ListSlotCount * sizeof(*grown));
        FreeVec(ListSlots);
    }
    ListSlots = grown;
    ListSlotCount = count;
    return TRUE;
}

GLuint MGLGeneric_GLGenLists(GLcontext context, GLsizei range)
{
    ULONG first, run, name;
    if (ListCompiling) {
        MGLPPCSetError(context, GL_INVALID_OPERATION);
        return 0;
    }
    if (range < 0) {
        MGLPPCSetError(context, GL_INVALID_VALUE);
        return 0;
    }
    if (!range)
        return 0;
    /* First run of `range` free names, starting at 1 (0 is never a list). */
    for (first = 1, run = 0, name = 1; ; ++name) {
        if (name >= ListSlotCount && !ListGrowNames(name + (ULONG)range)) {
            MGLPPCSetError(context, GL_OUT_OF_MEMORY);
            return 0;
        }
        if (ListSlots[name].reserved) {
            run = 0;
            first = name + 1UL;
            continue;
        }
        if (++run == (ULONG)range)
            break;
    }
    for (name = first; name < first + (ULONG)range; ++name)
        ListSlots[name].reserved = TRUE;
    return (GLuint)first;
}

void MGLGeneric_GLDeleteLists(GLcontext context, GLuint list, GLsizei range)
{
    GLuint name;
    if (ListCompiling) {
        MGLPPCSetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (range < 0) {
        MGLPPCSetError(context, GL_INVALID_VALUE);
        return;
    }
    for (name = list; name < list + (GLuint)range && name < ListSlotCount; ++name) {
        if (!name)
            continue;
        ListFreeSlot(name);
        ListSlots[name].reserved = FALSE;
    }
}

GLboolean MGLGeneric_GLIsList(GLcontext context, GLuint list)
{
    if (ListCompiling) {
        MGLPPCSetError(context, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    return list && list < ListSlotCount && ListSlots[list].reserved
           ? GL_TRUE : GL_FALSE;
}

void MGLGeneric_GLNewList(GLcontext context, GLuint list, GLenum mode)
{
    if (!list) {
        MGLPPCSetError(context, GL_INVALID_VALUE);
        return;
    }
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE) {
        MGLPPCSetError(context, GL_INVALID_ENUM);
        return;
    }
    if (ListCompiling) {
        MGLPPCSetError(context, GL_INVALID_OPERATION);
        return;
    }
    ListCompiling = list;
    ListMode = mode;
    ListUsed = 0;
    ListOverflow = FALSE;
    ListInstall();
}

void MGLGeneric_GLEndList(GLcontext context)
{
    ULONG *words = NULL;
    if (!ListCompiling) {
        MGLPPCSetError(context, GL_INVALID_OPERATION);
        return;
    }
    ListUninstall();
    /* GL 1.1: the new definition replaces the old one only at glEndList. */
    if (!ListOverflow && ListUsed) {
        words = (ULONG *)AllocVec(ListUsed * sizeof(ULONG), MEMF_PUBLIC);
        if (words)
            CopyMem(ListBuffer, words, ListUsed * sizeof(ULONG));
        else
            ListOverflow = TRUE;
    }
    if (ListOverflow) {
        MGLPPCSetError(context, GL_OUT_OF_MEMORY);
    } else if (ListCompiling < LIST_MAX_NAMES &&
               (ListCompiling < ListSlotCount ||
                ListGrowNames(ListCompiling + 1UL))) {
        ListFreeSlot(ListCompiling);
        ListSlots[ListCompiling].words = words;
        ListSlots[ListCompiling].count = ListUsed;
        ListSlots[ListCompiling].reserved = TRUE;
        words = NULL;
    } else {
        MGLPPCSetError(context, GL_OUT_OF_MEMORY);
    }
    if (words)
        FreeVec(words);
    ListCompiling = 0;
    ListMode = 0;
}

static void ListReplay(GLcontext context, GLuint list, ULONG depth)
{
    const ULONG *r, *end;
    if (depth > LIST_MAX_NESTING || list >= ListSlotCount ||
        !ListSlots[list].words)
        return;    /* undefined names execute nothing (GL 1.1 5.4) */
    if (!ListRealValid) {
        ListReal = *MGLPPCSelfTable;
        ListRealValid = TRUE;
    }
    r = ListSlots[list].words;
    end = r + ListSlots[list].count;
    while (r < end) {
        ULONG op = *r & 0xffffUL, words = *r >> 16;
        const ULONG *payload = r + 1;
        if (op == LIST_OP_CALLLIST)
            ListReplay(context, (GLuint)payload[0], depth + 1UL);
        else
            ListReplayGenerated(context, op, payload);
        r = payload + words;
    }
}

void MGLGeneric_GLCallList(GLcontext context, GLuint list)
{
    ListReplay(context, list, 1);
}

void MGLGeneric_ListsContextGone(void)
{
    ULONG name;
    if (ListCompiling) {
        ListUninstall();
        ListCompiling = 0;
        ListMode = 0;
    }
    for (name = 0; name < ListSlotCount; ++name)
        if (ListSlots[name].words)
            FreeVec(ListSlots[name].words);
    if (ListSlots)
        FreeVec(ListSlots);
    if (ListBuffer)
        FreeVec(ListBuffer);
    ListSlots = NULL;
    ListSlotCount = 0;
    ListBuffer = NULL;
    ListUsed = ListCapacity = 0;
}
