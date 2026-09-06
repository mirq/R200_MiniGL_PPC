#include <exec/types.h>
#include <exec/memory.h>
#include <string.h>

#include <libraries/minigl_ppc_batch.h>

extern APTR AllocVec(ULONG byteSize, ULONG requirements);
extern void FreeVec(APTR memoryBlock);

static ULONG FloatToBits(float value)
{
    union {
        float value;
        ULONG bits;
    } conversion;

    conversion.value = value;
    return conversion.bits;
}

static BOOL AppendRecord(MGLPPCBatch *batch, ULONG opcode,
                         ULONG argumentCount, const ULONG *arguments)
{
    ULONG offset = batch->message->payloadWords;
    ULONG index;

    if (argumentCount > 0xffffUL ||
        offset + argumentCount + 1 > batch->capacityWords)
        return FALSE;

    batch->message->payload[offset++] = (opcode << 16) | argumentCount;
    for (index = 0; index < argumentCount; ++index)
        batch->message->payload[offset++] = arguments[index];
    batch->message->payloadWords = offset;
    return TRUE;
}

BOOL MGLPPCBatchInit(MGLPPCBatch *batch, ULONG contextId, ULONG capacityWords)
{
    memset(batch, 0, sizeof(*batch));
    if (capacityWords == 0 || capacityWords > MGLPPC_MAX_BATCH_WORDS)
        return FALSE;

    batch->message = (MGLPPCMessage *)AllocVec(
        MGLPPC_MESSAGE_SIZE(capacityWords), MEMF_PUBLIC | MEMF_CLEAR);
    if (batch->message == NULL)
        return FALSE;

    batch->capacityWords = capacityWords;
    MGLPPCBatchReset(batch, contextId);
    return TRUE;
}

void MGLPPCBatchReset(MGLPPCBatch *batch, ULONG contextId)
{
    memset(batch->message, 0, MGLPPC_MESSAGE_HEADER_SIZE);
    batch->message->command = MGLPPC_CMD_EXECUTE_BATCH;
    batch->message->contextId = contextId;
}

void MGLPPCBatchDestroy(MGLPPCBatch *batch)
{
    if (batch->message != NULL)
        FreeVec(batch->message);
    memset(batch, 0, sizeof(*batch));
}

BOOL MGLPPCBatchClearColor(MGLPPCBatch *batch, float red, float green,
                           float blue, float alpha)
{
    ULONG arguments[4];

    arguments[0] = FloatToBits(red);
    arguments[1] = FloatToBits(green);
    arguments[2] = FloatToBits(blue);
    arguments[3] = FloatToBits(alpha);
    return AppendRecord(batch, MGLPPC_BATCH_CLEAR_COLOR, 4, arguments);
}

BOOL MGLPPCBatchClear(MGLPPCBatch *batch, ULONG mask)
{
    return AppendRecord(batch, MGLPPC_BATCH_CLEAR, 1, &mask);
}

BOOL MGLPPCBatchBeginPrimitive(MGLPPCBatch *batch, ULONG mode)
{
    return AppendRecord(batch, MGLPPC_BATCH_BEGIN, 1, &mode);
}

BOOL MGLPPCBatchColor4ub(MGLPPCBatch *batch, UBYTE red, UBYTE green,
                         UBYTE blue, UBYTE alpha)
{
    ULONG packed = ((ULONG)red << 24) | ((ULONG)green << 16) |
                   ((ULONG)blue << 8) | (ULONG)alpha;

    return AppendRecord(batch, MGLPPC_BATCH_COLOR4UB, 1, &packed);
}

BOOL MGLPPCBatchVertex3f(MGLPPCBatch *batch, float x, float y, float z)
{
    ULONG arguments[3];

    arguments[0] = FloatToBits(x);
    arguments[1] = FloatToBits(y);
    arguments[2] = FloatToBits(z);
    return AppendRecord(batch, MGLPPC_BATCH_VERTEX3F, 3, arguments);
}

BOOL MGLPPCBatchEndPrimitive(MGLPPCBatch *batch)
{
    return AppendRecord(batch, MGLPPC_BATCH_END, 0, NULL);
}

BOOL MGLPPCBatchPresent(MGLPPCBatch *batch)
{
    return AppendRecord(batch, MGLPPC_BATCH_PRESENT, 0, NULL);
}

BOOL MGLPPCBatchDelay(MGLPPCBatch *batch, ULONG ticks)
{
    return AppendRecord(batch, MGLPPC_BATCH_DELAY, 1, &ticks);
}

BOOL MGLPPCBatchSetState(MGLPPCBatch *batch, ULONG cap, BOOL enabled)
{
    ULONG arguments[2];

    arguments[0] = cap;
    arguments[1] = enabled ? 1UL : 0UL;
    return AppendRecord(batch, MGLPPC_BATCH_SET_STATE, 2, arguments);
}

BOOL MGLPPCBatchColor4f(MGLPPCBatch *batch, float red, float green,
                        float blue, float alpha)
{
    ULONG arguments[4];
    arguments[0] = FloatToBits(red); arguments[1] = FloatToBits(green);
    arguments[2] = FloatToBits(blue); arguments[3] = FloatToBits(alpha);
    return AppendRecord(batch, MGLPPC_BATCH_COLOR4F, 4, arguments);
}

BOOL MGLPPCBatchNormal3f(MGLPPCBatch *batch, float x, float y, float z)
{
    ULONG arguments[3];
    arguments[0] = FloatToBits(x); arguments[1] = FloatToBits(y);
    arguments[2] = FloatToBits(z);
    return AppendRecord(batch, MGLPPC_BATCH_NORMAL3F, 3, arguments);
}

BOOL MGLPPCBatchVertex4f(MGLPPCBatch *batch, float x, float y, float z,
                         float w)
{
    ULONG arguments[4];
    arguments[0] = FloatToBits(x); arguments[1] = FloatToBits(y);
    arguments[2] = FloatToBits(z); arguments[3] = FloatToBits(w);
    return AppendRecord(batch, MGLPPC_BATCH_VERTEX4F, 4, arguments);
}

BOOL MGLPPCBatchViewport(MGLPPCBatch *batch, LONG x, LONG y,
                         LONG width, LONG height)
{
    ULONG arguments[4];
    arguments[0] = (ULONG)x; arguments[1] = (ULONG)y;
    arguments[2] = (ULONG)width; arguments[3] = (ULONG)height;
    return AppendRecord(batch, MGLPPC_BATCH_VIEWPORT, 4, arguments);
}

BOOL MGLPPCBatchMatrixMode(MGLPPCBatch *batch, ULONG mode)
{ return AppendRecord(batch, MGLPPC_BATCH_MATRIX_MODE, 1, &mode); }

BOOL MGLPPCBatchLoadIdentity(MGLPPCBatch *batch)
{ return AppendRecord(batch, MGLPPC_BATCH_LOAD_IDENTITY, 0, NULL); }

BOOL MGLPPCBatchPushMatrix(MGLPPCBatch *batch)
{ return AppendRecord(batch, MGLPPC_BATCH_PUSH_MATRIX, 0, NULL); }

BOOL MGLPPCBatchPopMatrix(MGLPPCBatch *batch)
{ return AppendRecord(batch, MGLPPC_BATCH_POP_MATRIX, 0, NULL); }

BOOL MGLPPCBatchTranslatef(MGLPPCBatch *batch, float x, float y, float z)
{
    ULONG arguments[3];
    arguments[0] = FloatToBits(x); arguments[1] = FloatToBits(y);
    arguments[2] = FloatToBits(z);
    return AppendRecord(batch, MGLPPC_BATCH_TRANSLATEF, 3, arguments);
}

BOOL MGLPPCBatchRotatef(MGLPPCBatch *batch, float angle,
                        float x, float y, float z)
{
    ULONG arguments[4];
    arguments[0] = FloatToBits(angle); arguments[1] = FloatToBits(x);
    arguments[2] = FloatToBits(y); arguments[3] = FloatToBits(z);
    return AppendRecord(batch, MGLPPC_BATCH_ROTATEF, 4, arguments);
}

BOOL MGLPPCBatchScalef(MGLPPCBatch *batch, float x, float y, float z)
{
    ULONG arguments[3];
    arguments[0] = FloatToBits(x); arguments[1] = FloatToBits(y);
    arguments[2] = FloatToBits(z);
    return AppendRecord(batch, MGLPPC_BATCH_SCALEF, 3, arguments);
}

static BOOL AppendSixFloats(MGLPPCBatch *batch, ULONG opcode,
                            float a, float b, float c, float d,
                            float e, float f)
{
    ULONG arguments[6];
    arguments[0] = FloatToBits(a); arguments[1] = FloatToBits(b);
    arguments[2] = FloatToBits(c); arguments[3] = FloatToBits(d);
    arguments[4] = FloatToBits(e); arguments[5] = FloatToBits(f);
    return AppendRecord(batch, opcode, 6, arguments);
}

BOOL MGLPPCBatchFrustumf(MGLPPCBatch *batch, float left, float right,
                         float bottom, float top, float zNear, float zFar)
{ return AppendSixFloats(batch, MGLPPC_BATCH_FRUSTUMF, left, right,
                         bottom, top, zNear, zFar); }

BOOL MGLPPCBatchOrthof(MGLPPCBatch *batch, float left, float right,
                       float bottom, float top, float zNear, float zFar)
{ return AppendSixFloats(batch, MGLPPC_BATCH_ORTHOF, left, right,
                         bottom, top, zNear, zFar); }

BOOL MGLPPCBatchHint(MGLPPCBatch *batch, ULONG target, ULONG mode)
{
    ULONG arguments[2];
    arguments[0] = target; arguments[1] = mode;
    return AppendRecord(batch, MGLPPC_BATCH_HINT, 2, arguments);
}

BOOL MGLPPCBatchTexEnvi(MGLPPCBatch *batch, ULONG target, ULONG pname,
                        LONG param)
{
    ULONG arguments[3];
    arguments[0] = target; arguments[1] = pname; arguments[2] = (ULONG)param;
    return AppendRecord(batch, MGLPPC_BATCH_TEX_ENVI, 3, arguments);
}

BOOL MGLPPCBatchPixelStorei(MGLPPCBatch *batch, ULONG pname, LONG param)
{
    ULONG arguments[2];
    arguments[0] = pname; arguments[1] = (ULONG)param;
    return AppendRecord(batch, MGLPPC_BATCH_PIXEL_STOREI, 2, arguments);
}

BOOL MGLPPCBatchEnableSync(MGLPPCBatch *batch, BOOL enabled)
{
    ULONG value = enabled ? 1UL : 0UL;
    return AppendRecord(batch, MGLPPC_BATCH_MGL_ENABLE_SYNC, 1, &value);
}

BOOL MGLPPCBatchLockMode(MGLPPCBatch *batch, ULONG mode)
{ return AppendRecord(batch, MGLPPC_BATCH_MGL_LOCK_MODE, 1, &mode); }

BOOL MGLPPCBatchBindTexture(MGLPPCBatch *batch, ULONG target, ULONG texture)
{
    ULONG arguments[2];
    arguments[0] = target; arguments[1] = texture;
    return AppendRecord(batch, MGLPPC_BATCH_BIND_TEXTURE, 2, arguments);
}

BOOL MGLPPCBatchTexParameteri(MGLPPCBatch *batch, ULONG target, ULONG pname,
                              LONG param)
{
    ULONG arguments[3];
    arguments[0] = target; arguments[1] = pname; arguments[2] = (ULONG)param;
    return AppendRecord(batch, MGLPPC_BATCH_TEX_PARAMETERI, 3, arguments);
}

BOOL MGLPPCBatchTexGeni(MGLPPCBatch *batch, ULONG coord, ULONG mode, LONG map)
{
    ULONG arguments[3];
    arguments[0] = coord; arguments[1] = mode; arguments[2] = (ULONG)map;
    return AppendRecord(batch, MGLPPC_BATCH_TEX_GENI, 3, arguments);
}

BOOL MGLPPCBatchDrawT2fC4ubV3f(MGLPPCBatch *batch, ULONG mode,
                                const MGLPPCVertexT2fC4ubV3f *vertices,
                                ULONG vertexCount)
{
    ULONG argumentCount;
    ULONG offset = batch->message->payloadWords;
    ULONG index;

    if (vertices == NULL || vertexCount == 0 ||
        vertexCount > (0xffffUL - 2) / 6)
        return FALSE;
    argumentCount = 2 + vertexCount * 6;
    if (offset + argumentCount + 1 > batch->capacityWords)
        return FALSE;
    batch->message->payload[offset++] =
        (MGLPPC_BATCH_DRAW_T2F_C4UB_V3F << 16) | argumentCount;
    batch->message->payload[offset++] = mode;
    batch->message->payload[offset++] = vertexCount;
    for (index = 0; index < vertexCount; ++index) {
        batch->message->payload[offset++] = vertices[index].x;
        batch->message->payload[offset++] = vertices[index].y;
        batch->message->payload[offset++] = vertices[index].z;
        batch->message->payload[offset++] = vertices[index].s;
        batch->message->payload[offset++] = vertices[index].t;
        batch->message->payload[offset++] = vertices[index].color;
    }
    batch->message->payloadWords = offset;
    return TRUE;
}

void MGLPPCVertexC4ubV3fSet(MGLPPCVertexC4ubV3f *vertex,
                            float x, float y, float z,
                            UBYTE red, UBYTE green, UBYTE blue, UBYTE alpha)
{
    vertex->x = FloatToBits(x);
    vertex->y = FloatToBits(y);
    vertex->z = FloatToBits(z);
    vertex->color = ((ULONG)alpha << 24) | ((ULONG)red << 16) |
                    ((ULONG)green << 8) | (ULONG)blue;
}

BOOL MGLPPCBatchDrawC4ubV3f(MGLPPCBatch *batch, ULONG mode,
                            const MGLPPCVertexC4ubV3f *vertices,
                            ULONG vertexCount)
{
    ULONG argumentCount;
    ULONG offset = batch->message->payloadWords;
    ULONG index;

    if (vertices == NULL || vertexCount == 0 ||
        vertexCount > (0xffffUL - 2) / 4)
        return FALSE;
    argumentCount = 2 + vertexCount * 4;
    if (offset + argumentCount + 1 > batch->capacityWords)
        return FALSE;

    batch->message->payload[offset++] =
        (MGLPPC_BATCH_DRAW_C4UB_V3F << 16) | argumentCount;
    batch->message->payload[offset++] = mode;
    batch->message->payload[offset++] = vertexCount;
    for (index = 0; index < vertexCount; ++index) {
        batch->message->payload[offset++] = vertices[index].x;
        batch->message->payload[offset++] = vertices[index].y;
        batch->message->payload[offset++] = vertices[index].z;
        batch->message->payload[offset++] = vertices[index].color;
    }
    batch->message->payloadWords = offset;
    return TRUE;
}

BOOL MGLPPCBatchSubmit(MGLPPCTransport *transport, MGLPPCBatch *batch)
{
    return MGLPPCTransportCall(transport, batch->message);
}
