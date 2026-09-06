#ifndef LIBRARIES_MINIGL_PPC_BATCH_H
#define LIBRARIES_MINIGL_PPC_BATCH_H

#include <libraries/minigl_ppc_transport.h>

/* MiniGL's enum values are intentionally not desktop OpenGL values. */
#define MGLPPC_GL_COLOR_BUFFER_BIT 0x00000001UL
#define MGLPPC_GL_DEPTH_BUFFER_BIT 0x00000002UL
#define MGLPPC_GL_TRIANGLES 191UL
#define MGLPPC_GL_TRIANGLE_FAN 192UL
#define MGLPPC_GL_TRIANGLE_STRIP 193UL
#define MGLPPC_DEFAULT_BATCH_WORDS 8192UL

typedef struct MGLPPCBatch {
    MGLPPCMessage *message;
    ULONG capacityWords;
} MGLPPCBatch;

typedef struct MGLPPCVertexC4ubV3f {
    ULONG x;
    ULONG y;
    ULONG z;
    /* Stored as AARRGGBB so the 68k backend can consume it without conversion. */
    ULONG color;
} MGLPPCVertexC4ubV3f;

typedef struct MGLPPCVertexT2fC4ubV3f {
    ULONG x;
    ULONG y;
    ULONG z;
    ULONG s;
    ULONG t;
    ULONG color;
} MGLPPCVertexT2fC4ubV3f;

BOOL MGLPPCBatchInit(MGLPPCBatch *batch, ULONG contextId,
                     ULONG capacityWords);
void MGLPPCBatchReset(MGLPPCBatch *batch, ULONG contextId);
void MGLPPCBatchDestroy(MGLPPCBatch *batch);
BOOL MGLPPCBatchClearColor(MGLPPCBatch *batch, float red, float green,
                           float blue, float alpha);
BOOL MGLPPCBatchClear(MGLPPCBatch *batch, ULONG mask);
BOOL MGLPPCBatchBeginPrimitive(MGLPPCBatch *batch, ULONG mode);
BOOL MGLPPCBatchColor4ub(MGLPPCBatch *batch, UBYTE red, UBYTE green,
                         UBYTE blue, UBYTE alpha);
BOOL MGLPPCBatchVertex3f(MGLPPCBatch *batch, float x, float y, float z);
BOOL MGLPPCBatchEndPrimitive(MGLPPCBatch *batch);
BOOL MGLPPCBatchPresent(MGLPPCBatch *batch);
BOOL MGLPPCBatchDelay(MGLPPCBatch *batch, ULONG ticks);
BOOL MGLPPCBatchSetState(MGLPPCBatch *batch, ULONG cap, BOOL enabled);
BOOL MGLPPCBatchColor4f(MGLPPCBatch *batch, float red, float green,
                        float blue, float alpha);
BOOL MGLPPCBatchNormal3f(MGLPPCBatch *batch, float x, float y, float z);
BOOL MGLPPCBatchVertex4f(MGLPPCBatch *batch, float x, float y, float z,
                         float w);
BOOL MGLPPCBatchViewport(MGLPPCBatch *batch, LONG x, LONG y,
                         LONG width, LONG height);
BOOL MGLPPCBatchMatrixMode(MGLPPCBatch *batch, ULONG mode);
BOOL MGLPPCBatchLoadIdentity(MGLPPCBatch *batch);
BOOL MGLPPCBatchPushMatrix(MGLPPCBatch *batch);
BOOL MGLPPCBatchPopMatrix(MGLPPCBatch *batch);
BOOL MGLPPCBatchTranslatef(MGLPPCBatch *batch, float x, float y, float z);
BOOL MGLPPCBatchRotatef(MGLPPCBatch *batch, float angle,
                        float x, float y, float z);
BOOL MGLPPCBatchScalef(MGLPPCBatch *batch, float x, float y, float z);
BOOL MGLPPCBatchFrustumf(MGLPPCBatch *batch, float left, float right,
                         float bottom, float top, float zNear, float zFar);
BOOL MGLPPCBatchOrthof(MGLPPCBatch *batch, float left, float right,
                       float bottom, float top, float zNear, float zFar);
BOOL MGLPPCBatchHint(MGLPPCBatch *batch, ULONG target, ULONG mode);
BOOL MGLPPCBatchTexEnvi(MGLPPCBatch *batch, ULONG target, ULONG pname,
                        LONG param);
BOOL MGLPPCBatchPixelStorei(MGLPPCBatch *batch, ULONG pname, LONG param);
BOOL MGLPPCBatchEnableSync(MGLPPCBatch *batch, BOOL enabled);
BOOL MGLPPCBatchLockMode(MGLPPCBatch *batch, ULONG mode);
BOOL MGLPPCBatchBindTexture(MGLPPCBatch *batch, ULONG target, ULONG texture);
BOOL MGLPPCBatchTexParameteri(MGLPPCBatch *batch, ULONG target, ULONG pname,
                              LONG param);
BOOL MGLPPCBatchTexGeni(MGLPPCBatch *batch, ULONG coord, ULONG mode,
                        LONG map);
BOOL MGLPPCBatchDrawT2fC4ubV3f(MGLPPCBatch *batch, ULONG mode,
                                const MGLPPCVertexT2fC4ubV3f *vertices,
                                ULONG vertexCount);
void MGLPPCVertexC4ubV3fSet(MGLPPCVertexC4ubV3f *vertex,
                            float x, float y, float z,
                            UBYTE red, UBYTE green, UBYTE blue, UBYTE alpha);
BOOL MGLPPCBatchDrawC4ubV3f(MGLPPCBatch *batch, ULONG mode,
                            const MGLPPCVertexC4ubV3f *vertices,
                            ULONG vertexCount);
BOOL MGLPPCBatchSubmit(MGLPPCTransport *transport, MGLPPCBatch *batch);

#endif
