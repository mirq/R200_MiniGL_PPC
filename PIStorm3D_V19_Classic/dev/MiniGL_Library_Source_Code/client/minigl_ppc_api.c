#include <exec/types.h>
#include <exec/libraries.h>
#include <string.h>

#define MINIGL_LIBRARY_BUILD
#include <libraries/minigl_dispatch.h>
#include <libraries/minigl_ppc_batch.h>
#include <libraries/minigl_ppc_profile.h>

#define PPC_PRIMITIVE_VERTICES 512
#define PPC_DRAW_VERTICES 4096
#define PPC_MODELVIEW_STACK 32

typedef struct PPCImmediateVertex {
    float x, y, z;
    float s, t;
    ULONG color;
} PPCImmediateVertex;

typedef struct MGLPPCClientState {
    MGLPPCTransport transport;
    MGLPPCBatch batch;
    GLcontext currentContext;
    ULONG contextId;
    GLenum lastError;
    BOOL opened;
    BOOL batchReady;
    BOOL insidePrimitive;
    int pixelDepth;
    int windowMode;
    int vertexBufferSize;
    int textureBufferSize;
    int mtexBufferSize;
    int numberOfBuffers;
    void *windowHandle;
    GLenum primitiveMode;
    ULONG primitiveCount;
    PPCImmediateVertex primitive[PPC_PRIMITIVE_VERTICES];
    GLenum drawMode;
    ULONG drawCount;
    MGLPPCVertexT2fC4ubV3f draw[PPC_DRAW_VERTICES];
    float normalX, normalY, normalZ;
    float eyeNormalX, eyeNormalY, eyeNormalZ;
    BOOL normalDirty;
    ULONG currentColor;
    BOOL textureEnabled;
    BOOL texgenS;
    BOOL texgenT;
    GLenum matrixMode;
    float modelview[16];
    float modelviewStack[PPC_MODELVIEW_STACK][16];
    ULONG modelviewDepth;
    float projection[16];
    float projectionStack[PPC_MODELVIEW_STACK][16];
    ULONG projectionDepth;
    LONG viewportX, viewportY, viewportWidth, viewportHeight;
    LONG drawableWidth, drawableHeight;
} MGLPPCClientState;

static MGLPPCClientState Client;
static MGLPPCClientProfile ClientProfile;
static BOOL ClientProfileEnabled;

static ULONG ReadPPCClock(void) = "\tmftb\tr3";

void MGLPPCProfileReset(void)
{
    memset(&ClientProfile, 0, sizeof(ClientProfile));
    ClientProfileEnabled = TRUE;
}

void MGLPPCProfileRead(MGLPPCClientProfile *profile)
{
    if (profile) memcpy(profile, &ClientProfile, sizeof(*profile));
    ClientProfileEnabled = FALSE;
}

struct Library *MiniGLBase = NULL;
const MGLDispatchTable *MiniGLDispatch = NULL;

static ULONG ClientFloatBits(float value)
{
    union { float value; ULONG bits; } conversion;
    conversion.value = value;
    return conversion.bits;
}

static float FastInvSqrt(float value)
{
    union { float value; ULONG bits; } conversion;
    float half;
    if (value <= 0.0f) return 0.0f;
    half = value * 0.5f;
    conversion.value = value;
    conversion.bits = 0x5f3759dfUL - (conversion.bits >> 1);
    conversion.value *= 1.5f - half * conversion.value * conversion.value;
    conversion.value *= 1.5f - half * conversion.value * conversion.value;
    return conversion.value;
}

static void FastSinCosDegrees(float degrees, float *sine, float *cosine)
{
    float x, x2, cosSign = 1.0f;
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    x = degrees * 0.017453292519943295f;
    if (x > 1.5707963267948966f) {
        x = 3.1415926535897932f - x;
        cosSign = -1.0f;
    } else if (x < -1.5707963267948966f) {
        x = -3.1415926535897932f - x;
        cosSign = -1.0f;
    }
    x2 = x * x;
    *sine = x * (1.0f + x2 * (-0.1666666667f +
             x2 * (0.0083333333f + x2 * -0.0001984127f)));
    *cosine = cosSign * (1.0f + x2 * (-0.5f +
               x2 * (0.0416666667f + x2 * (-0.0013888889f +
               x2 * 0.0000248016f))));
}

static void MatrixIdentity(float *matrix)
{
    ULONG i;
    for (i = 0; i < 16; ++i) matrix[i] = 0.0f;
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static void MatrixMultiply(float *left, const float *right)
{
    float result[16];
    ULONG row, column, k;
    for (column = 0; column < 4; ++column)
        for (row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (k = 0; k < 4; ++k)
                value += left[k * 4 + row] * right[column * 4 + k];
            result[column * 4 + row] = value;
        }
    memcpy(left, result, sizeof(result));
}

static void TransformToScreen(PPCImmediateVertex *vertex)
{
    const float *m = Client.modelview;
    const float *p = Client.projection;
    float x = vertex->x, y = vertex->y, z = vertex->z;
    float ex = m[0]*x + m[4]*y + m[8]*z + m[12];
    float ey = m[1]*x + m[5]*y + m[9]*z + m[13];
    float ez = m[2]*x + m[6]*y + m[10]*z + m[14];
    float ew = m[3]*x + m[7]*y + m[11]*z + m[15];
    float cx = p[0]*ex + p[4]*ey + p[8]*ez + p[12]*ew;
    float cy = p[1]*ex + p[5]*ey + p[9]*ez + p[13]*ew;
    float cz = p[2]*ex + p[6]*ey + p[10]*ez + p[14]*ew;
    float cw = p[3]*ex + p[7]*ey + p[11]*ez + p[15]*ew;
    float inverse = cw != 0.0f ? 1.0f / cw : 1.0f;
    float ndcX = cx * inverse, ndcY = cy * inverse, ndcZ = cz * inverse;
    float top = (float)(Client.drawableHeight - Client.viewportY -
                        Client.viewportHeight);
    vertex->x = (float)Client.viewportX +
                (ndcX + 1.0f) * (float)Client.viewportWidth * 0.5f;
    vertex->y = top + (1.0f - ndcY) *
                (float)Client.viewportHeight * 0.5f;
    vertex->z = (ndcZ + 1.0f) * 0.5f;
}

static BOOL ClientSubmitBatch(void)
{
    MGLPPCMessage *message;
    ULONG profileStart = ClientProfileEnabled ? ReadPPCClock() : 0;

    if (!Client.batchReady || Client.batch.message->payloadWords == 0)
        return TRUE;
    message = Client.batch.message;
    if (!MGLPPCBatchSubmit(&Client.transport, &Client.batch)) {
        Client.lastError = GL_INVALID_OPERATION;
        return FALSE;
    }
    if (message->status != MGLPPC_STATUS_OK) {
        Client.lastError = message->status == MGLPPC_STATUS_GL_ERROR &&
                           message->value[0] != GL_NO_ERROR
            ? message->value[0] : GL_INVALID_OPERATION;
        return FALSE;
    }
    if (message->value[0] != GL_NO_ERROR)
        Client.lastError = message->value[0];
    MGLPPCBatchReset(&Client.batch, Client.contextId);
    if (ClientProfileEnabled) {
        ClientProfile.submitCalls++;
        ClientProfile.submitTicks += ReadPPCClock() - profileStart;
    }
    return TRUE;
}

static BOOL ClientFlushDraws(void)
{
    ULONG offset = 0;
    ULONG primitiveSize;
    if (Client.drawCount == 0) return TRUE;
    if (Client.drawMode == GL_TRIANGLE_STRIP) {
        while (offset < Client.drawCount) {
            ULONG availableWords = Client.batch.capacityWords -
                                   Client.batch.message->payloadWords;
            ULONG availableVertices = availableWords > 3UL
                                          ? (availableWords - 3UL) / 6UL : 0UL;
            ULONG count = Client.drawCount - offset;
            if (availableVertices < 3UL) {
                if (!ClientSubmitBatch()) return FALSE;
                continue;
            }
            if (count > availableVertices) count = availableVertices;
            if (!MGLPPCBatchDrawT2fC4ubV3f(
                    &Client.batch,
                    Client.textureEnabled && (Client.texgenS || Client.texgenT)
                        ? MGL_FLATSTRIP : Client.drawMode,
                    Client.draw + offset, count)) {
                Client.lastError = GL_OUT_OF_MEMORY;
                return FALSE;
            }
            if (offset + count < Client.drawCount) {
                offset += count - 2UL;
                if (!ClientSubmitBatch()) return FALSE;
            } else {
                offset += count;
            }
        }
        Client.drawCount = 0;
        return TRUE;
    }
    primitiveSize = Client.drawMode == GL_TRIANGLES ? 3UL : 2UL;
    while (offset < Client.drawCount) {
        ULONG availableWords = Client.batch.capacityWords -
                               Client.batch.message->payloadWords;
        ULONG availableVertices = availableWords > 3UL
                                      ? (availableWords - 3UL) / 6UL : 0UL;
        ULONG count;
        availableVertices -= availableVertices % primitiveSize;
        if (availableVertices == 0) {
            if (!ClientSubmitBatch()) return FALSE;
            continue;
        }
        count = Client.drawCount - offset;
        if (count > availableVertices) count = availableVertices;
        count -= count % primitiveSize;
        if (!MGLPPCBatchDrawT2fC4ubV3f(&Client.batch, Client.drawMode,
                                        Client.draw + offset, count)) {
            Client.lastError = GL_OUT_OF_MEMORY;
            return FALSE;
        }
        offset += count;
        if (offset < Client.drawCount && !ClientSubmitBatch()) return FALSE;
    }
    Client.drawCount = 0;
    return TRUE;
}

static BOOL ClientFlush(void)
{
    return ClientFlushDraws() && ClientSubmitBatch();
}

BOOL MGLPPCSubmitRawPayload(const ULONG *payload, ULONG payloadWords)
{
    if (!payload || payloadWords == 0 ||
        payloadWords > Client.batch.capacityWords || Client.insidePrimitive) {
        Client.lastError = GL_INVALID_VALUE;
        return FALSE;
    }
    if (!ClientFlush()) return FALSE;
    memcpy(Client.batch.message->payload, payload,
           payloadWords * sizeof(ULONG));
    Client.batch.message->payloadWords = payloadWords;
    return ClientSubmitBatch();
}

#define RECORD(call) do { \
    if (!(call)) { \
        if (!ClientSubmitBatch() || !(call)) \
            Client.lastError = GL_OUT_OF_MEMORY; \
    } \
} while (0)

static void SphereCoordinates(float x, float y, float z, float *s, float *t)
{
    const float *m = Client.modelview;
    float ux = m[0]*x + m[4]*y + m[8]*z + m[12];
    float uy = m[1]*x + m[5]*y + m[9]*z + m[13];
    float uz = m[2]*x + m[6]*y + m[10]*z + m[14];
    float nx, ny, nz;
    float inverse, lengthSquared, dot, rx, ry, rz;

    inverse = FastInvSqrt(ux*ux + uy*uy + uz*uz);
    if (inverse > 0.0f) { ux *= inverse; uy *= inverse; uz *= inverse; }
    if (Client.normalDirty) {
        nx = m[0]*Client.normalX + m[4]*Client.normalY + m[8]*Client.normalZ;
        ny = m[1]*Client.normalX + m[5]*Client.normalY + m[9]*Client.normalZ;
        nz = m[2]*Client.normalX + m[6]*Client.normalY + m[10]*Client.normalZ;
        inverse = FastInvSqrt(nx*nx + ny*ny + nz*nz);
        if (inverse > 0.0f) { nx *= inverse; ny *= inverse; nz *= inverse; }
        Client.eyeNormalX = nx; Client.eyeNormalY = ny; Client.eyeNormalZ = nz;
        Client.normalDirty = FALSE;
    } else {
        nx = Client.eyeNormalX; ny = Client.eyeNormalY; nz = Client.eyeNormalZ;
    }
    dot = 2.0f * (nx*ux + ny*uy + nz*uz);
    rx = ux - nx*dot;
    ry = uy - ny*dot;
    rz = uz - nz*dot + 1.0f;
    lengthSquared = rx*rx + ry*ry + rz*rz;
    inverse = 0.5f * FastInvSqrt(lengthSquared);
    if (inverse > 0.0f) {
        *s = rx * inverse + 0.5f;
        *t = ry * inverse + 0.5f;
    } else {
        *s = *t = 0.5f;
    }
}

static BOOL AddDrawVertex(GLenum mode, const PPCImmediateVertex *vertex)
{
    MGLPPCVertexT2fC4ubV3f *output;
    if (Client.drawCount != 0 && Client.drawMode != mode &&
        !ClientFlushDraws())
        return FALSE;
    if (Client.drawCount == PPC_DRAW_VERTICES && !ClientFlushDraws())
        return FALSE;
    Client.drawMode = mode;
    output = &Client.draw[Client.drawCount++];
    output->x = ClientFloatBits(vertex->x);
    output->y = ClientFloatBits(vertex->y);
    output->z = ClientFloatBits(vertex->z);
    output->s = ClientFloatBits(vertex->s);
    output->t = ClientFloatBits(vertex->t);
    output->color = vertex->color;
    return TRUE;
}

static BOOL AddTriangle(const PPCImmediateVertex *a,
                        const PPCImmediateVertex *b,
                        const PPCImmediateVertex *c)
{
    if ((Client.drawCount != 0 && Client.drawMode != GL_TRIANGLES) ||
        Client.drawCount + 3 > PPC_DRAW_VERTICES) {
        if (!ClientFlushDraws()) return FALSE;
    }
    return AddDrawVertex(GL_TRIANGLES, a) &&
           AddDrawVertex(GL_TRIANGLES, b) &&
           AddDrawVertex(GL_TRIANGLES, c);
}

static void PPCGLBegin(GLcontext context, GLenum mode)
{
    (void)context;
    if (Client.insidePrimitive) { Client.lastError = GL_INVALID_OPERATION; return; }
    if (mode != GL_TRIANGLES && mode != GL_TRIANGLE_STRIP &&
        mode != GL_TRIANGLE_FAN && mode != GL_QUADS && mode != GL_LINES) {
        Client.lastError = GL_INVALID_ENUM;
        return;
    }
    Client.primitiveMode = mode;
    Client.primitiveCount = 0;
    Client.insidePrimitive = TRUE;
}

static void PPCGLEnd(GLcontext context)
{
    ULONG index;
    ULONG profileStart = ClientProfileEnabled ? ReadPPCClock() : 0;
    (void)context;
    if (!Client.insidePrimitive) { Client.lastError = GL_INVALID_OPERATION; return; }
    Client.insidePrimitive = FALSE;
    if (Client.primitiveCount == 0) return;
    if (Client.primitiveMode == GL_TRIANGLE_STRIP) {
        if (Client.drawCount != 0 && Client.drawMode == GL_TRIANGLE_STRIP) {
            PPCImmediateVertex previous;
            memcpy(&previous, &Client.draw[Client.drawCount - 1],
                   sizeof(previous));
            if (!AddDrawVertex(GL_TRIANGLE_STRIP, &previous) ||
                !AddDrawVertex(GL_TRIANGLE_STRIP, &Client.primitive[0])) return;
        } else if (Client.drawCount != 0 && !ClientFlushDraws()) {
            return;
        }
        for (index = 0; index < Client.primitiveCount; ++index)
            if (!AddDrawVertex(GL_TRIANGLE_STRIP,
                               &Client.primitive[index])) return;
        goto done;
    }
    if (!ClientFlushDraws()) return;
    if (!MGLPPCBatchDrawT2fC4ubV3f(
            &Client.batch,
            Client.textureEnabled && (Client.texgenS || Client.texgenT) &&
            Client.primitiveMode == GL_TRIANGLE_FAN
                ? MGL_FLATFAN : Client.primitiveMode,
            (const MGLPPCVertexT2fC4ubV3f *)Client.primitive,
            Client.primitiveCount)) {
        if (!ClientSubmitBatch() ||
            !MGLPPCBatchDrawT2fC4ubV3f(
                &Client.batch,
                Client.textureEnabled && (Client.texgenS || Client.texgenT) &&
                Client.primitiveMode == GL_TRIANGLE_FAN
                    ? MGL_FLATFAN : Client.primitiveMode,
                (const MGLPPCVertexT2fC4ubV3f *)Client.primitive,
                Client.primitiveCount)) {
            Client.lastError = GL_OUT_OF_MEMORY;
        }
    }
done:
    if (ClientProfileEnabled) {
        ClientProfile.endCalls++;
        ClientProfile.endTicks += ReadPPCClock() - profileStart;
    }
}

static void PPCGLClear(GLcontext context, GLbitfield mask)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchClear(&Client.batch, mask)); }

static void PPCGLClearColor(GLcontext context, GLclampf r, GLclampf g,
                            GLclampf b, GLclampf a)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchClearColor(&Client.batch, r, g, b, a)); }

static void PPCGLColor4f(GLcontext context, GLfloat r, GLfloat g,
                         GLfloat b, GLfloat a)
{
    (void)context;
    Client.currentColor = ((ULONG)(a * 255.0f) << 24) |
                          ((ULONG)(r * 255.0f) << 16) |
                          ((ULONG)(g * 255.0f) << 8) |
                          (ULONG)(b * 255.0f);
}

static void PPCGLColor4ub(GLcontext context, GLubyte r, GLubyte g,
                          GLubyte b, GLubyte a)
{ (void)context; Client.currentColor = ((ULONG)a << 24) | ((ULONG)r << 16) |
                                        ((ULONG)g << 8) | (ULONG)b; }

static void PPCGLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z)
{
    (void)context;
    Client.normalX = x; Client.normalY = y; Client.normalZ = z;
    Client.normalDirty = TRUE;
}

static void PPCGLVertex4f(GLcontext context, GLfloat x, GLfloat y,
                          GLfloat z, GLfloat w)
{
    PPCImmediateVertex *vertex;
    ULONG profileStart = ClientProfileEnabled ? ReadPPCClock() : 0;
    ULONG phaseStart;
    (void)context;
    if (!Client.insidePrimitive) { Client.lastError = GL_INVALID_OPERATION; return; }
    if (Client.primitiveCount == PPC_PRIMITIVE_VERTICES) {
        Client.lastError = GL_OUT_OF_MEMORY;
        return;
    }
    vertex = &Client.primitive[Client.primitiveCount++];
    if (w != 0.0f && w != 1.0f) { x /= w; y /= w; z /= w; }
    vertex->x = x; vertex->y = y; vertex->z = z;
    vertex->color = Client.currentColor;
    vertex->s = vertex->t = 0.0f;
    phaseStart = ClientProfileEnabled ? ReadPPCClock() : 0;
    if (Client.textureEnabled && (Client.texgenS || Client.texgenT))
        SphereCoordinates(x, y, z, &vertex->s, &vertex->t);
    if (ClientProfileEnabled)
        ClientProfile.sphereTicks += ReadPPCClock() - phaseStart;
    phaseStart = ClientProfileEnabled ? ReadPPCClock() : 0;
    if (Client.textureEnabled && (Client.texgenS || Client.texgenT))
        TransformToScreen(vertex);
    if (ClientProfileEnabled) {
        ClientProfile.transformTicks += ReadPPCClock() - phaseStart;
        ClientProfile.vertexCalls++;
        ClientProfile.vertexTicks += ReadPPCClock() - profileStart;
    }
}

static void PPCGLViewport(GLcontext context, GLint x, GLint y,
                          GLsizei width, GLsizei height)
{
    (void)context;
    if (!ClientFlushDraws()) return;
    Client.viewportX = x; Client.viewportY = y;
    Client.viewportWidth = width; Client.viewportHeight = height;
    RECORD(MGLPPCBatchViewport(&Client.batch, x, y, width, height));
}

static void PPCGLMatrixMode(GLcontext context, GLenum mode)
{
    (void)context;
    if (!ClientFlushDraws()) return;
    Client.matrixMode = mode;
    RECORD(MGLPPCBatchMatrixMode(&Client.batch, mode));
}

static void PPCGLLoadIdentity(GLcontext context)
{
    (void)context;
    if (!ClientFlushDraws()) return;
    if (Client.matrixMode == GL_MODELVIEW) {
        MatrixIdentity(Client.modelview);
        Client.normalDirty = TRUE;
    }
    else if (Client.matrixMode == GL_PROJECTION) MatrixIdentity(Client.projection);
    RECORD(MGLPPCBatchLoadIdentity(&Client.batch));
}

static void PPCGLPushMatrix(GLcontext context)
{
    (void)context;
    if (!ClientFlushDraws()) return;
    if (Client.matrixMode == GL_MODELVIEW) {
        if (Client.modelviewDepth == PPC_MODELVIEW_STACK) {
            Client.lastError = GL_STACK_OVERFLOW; return;
        }
        memcpy(Client.modelviewStack[Client.modelviewDepth++], Client.modelview,
               sizeof(Client.modelview));
    } else if (Client.matrixMode == GL_PROJECTION) {
        if (Client.projectionDepth == PPC_MODELVIEW_STACK) {
            Client.lastError = GL_STACK_OVERFLOW; return;
        }
        memcpy(Client.projectionStack[Client.projectionDepth++],
               Client.projection, sizeof(Client.projection));
    }
    RECORD(MGLPPCBatchPushMatrix(&Client.batch));
}

static void PPCGLPopMatrix(GLcontext context)
{
    (void)context;
    if (!ClientFlushDraws()) return;
    if (Client.matrixMode == GL_MODELVIEW) {
        if (Client.modelviewDepth == 0) { Client.lastError = GL_STACK_UNDERFLOW; return; }
        memcpy(Client.modelview, Client.modelviewStack[--Client.modelviewDepth],
               sizeof(Client.modelview));
        Client.normalDirty = TRUE;
    } else if (Client.matrixMode == GL_PROJECTION) {
        if (Client.projectionDepth == 0) { Client.lastError = GL_STACK_UNDERFLOW; return; }
        memcpy(Client.projection,
               Client.projectionStack[--Client.projectionDepth],
               sizeof(Client.projection));
    }
    RECORD(MGLPPCBatchPopMatrix(&Client.batch));
}

static void PPCGLTranslatef(GLcontext context, GLfloat x, GLfloat y, GLfloat z)
{
    float matrix[16];
    (void)context;
    if (!ClientFlushDraws()) return;
    if (Client.matrixMode == GL_MODELVIEW) {
        MatrixIdentity(matrix); matrix[12] = x; matrix[13] = y; matrix[14] = z;
        MatrixMultiply(Client.modelview, matrix);
        Client.normalDirty = TRUE;
    } else if (Client.matrixMode == GL_PROJECTION) {
        MatrixIdentity(matrix); matrix[12] = x; matrix[13] = y; matrix[14] = z;
        MatrixMultiply(Client.projection, matrix);
    }
    RECORD(MGLPPCBatchTranslatef(&Client.batch, x, y, z));
}

static void PPCGLTranslated(GLcontext context, GLdouble x, GLdouble y, GLdouble z)
{ PPCGLTranslatef(context, (GLfloat)x, (GLfloat)y, (GLfloat)z); }

static void PPCGLRotatef(GLcontext context, GLfloat angle,
                         GLfloat x, GLfloat y, GLfloat z)
{
    float matrix[16], length, c, s, oneMinusC;
    (void)context;
    if (!ClientFlushDraws()) return;
    if (Client.matrixMode == GL_MODELVIEW) {
        length = FastInvSqrt(x*x + y*y + z*z);
        if (length > 0.0f) {
            x *= length; y *= length; z *= length;
            FastSinCosDegrees(angle, &s, &c);
            oneMinusC = 1.0f - c;
            MatrixIdentity(matrix);
            matrix[0]=x*x*oneMinusC+c; matrix[4]=x*y*oneMinusC-z*s;
            matrix[8]=x*z*oneMinusC+y*s; matrix[1]=y*x*oneMinusC+z*s;
            matrix[5]=y*y*oneMinusC+c; matrix[9]=y*z*oneMinusC-x*s;
            matrix[2]=z*x*oneMinusC-y*s; matrix[6]=z*y*oneMinusC+x*s;
            matrix[10]=z*z*oneMinusC+c;
            MatrixMultiply(Client.modelview, matrix);
            Client.normalDirty = TRUE;
        }
    } else if (Client.matrixMode == GL_PROJECTION) {
        length = FastInvSqrt(x*x + y*y + z*z);
        if (length > 0.0f) {
            x *= length; y *= length; z *= length;
            FastSinCosDegrees(angle, &s, &c);
            oneMinusC = 1.0f - c;
            MatrixIdentity(matrix);
            matrix[0]=x*x*oneMinusC+c; matrix[4]=x*y*oneMinusC-z*s;
            matrix[8]=x*z*oneMinusC+y*s; matrix[1]=y*x*oneMinusC+z*s;
            matrix[5]=y*y*oneMinusC+c; matrix[9]=y*z*oneMinusC-x*s;
            matrix[2]=z*x*oneMinusC-y*s; matrix[6]=z*y*oneMinusC+x*s;
            matrix[10]=z*z*oneMinusC+c;
            MatrixMultiply(Client.projection, matrix);
        }
    }
    RECORD(MGLPPCBatchRotatef(&Client.batch, angle, x, y, z));
}

static void PPCGLRotated(GLcontext context, GLdouble angle,
                         GLdouble x, GLdouble y, GLdouble z)
{ PPCGLRotatef(context, (GLfloat)angle, (GLfloat)x, (GLfloat)y, (GLfloat)z); }

static void PPCGLScalef(GLcontext context, GLfloat x, GLfloat y, GLfloat z)
{
    float matrix[16];
    (void)context;
    if (!ClientFlushDraws()) return;
    if (Client.matrixMode == GL_MODELVIEW) {
        MatrixIdentity(matrix); matrix[0]=x; matrix[5]=y; matrix[10]=z;
        MatrixMultiply(Client.modelview, matrix);
        Client.normalDirty = TRUE;
    } else if (Client.matrixMode == GL_PROJECTION) {
        MatrixIdentity(matrix); matrix[0]=x; matrix[5]=y; matrix[10]=z;
        MatrixMultiply(Client.projection, matrix);
    }
    RECORD(MGLPPCBatchScalef(&Client.batch, x, y, z));
}

static void PPCGLScaled(GLcontext context, GLdouble x, GLdouble y, GLdouble z)
{ PPCGLScalef(context, (GLfloat)x, (GLfloat)y, (GLfloat)z); }

static void PPCGLFrustum(GLcontext context, GLdouble l, GLdouble r,
                         GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    float matrix[16];
    (void)context;
    if (!ClientFlushDraws()) return;
    MatrixIdentity(matrix);
    matrix[0] = (float)(2.0*n/(r-l));
    matrix[5] = (float)(2.0*n/(t-b));
    matrix[8] = (float)((r+l)/(r-l));
    matrix[9] = (float)((t+b)/(t-b));
    matrix[10] = (float)(-(f+n)/(f-n));
    matrix[11] = -1.0f;
    matrix[14] = (float)(-(2.0*f*n)/(f-n));
    matrix[15] = 0.0f;
    if (Client.matrixMode == GL_PROJECTION) MatrixMultiply(Client.projection, matrix);
    else if (Client.matrixMode == GL_MODELVIEW) {
        MatrixMultiply(Client.modelview, matrix);
        Client.normalDirty = TRUE;
    }
    RECORD(MGLPPCBatchFrustumf(&Client.batch, (float)l, (float)r,
                               (float)b, (float)t, (float)n, (float)f));
}

static void PPCGLOrtho(GLcontext context, GLdouble l, GLdouble r,
                       GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    float matrix[16];
    (void)context;
    if (!ClientFlushDraws()) return;
    MatrixIdentity(matrix);
    matrix[0] = (float)(2.0/(r-l)); matrix[5] = (float)(2.0/(t-b));
    matrix[10] = (float)(-2.0/(f-n)); matrix[12] = (float)(-(r+l)/(r-l));
    matrix[13] = (float)(-(t+b)/(t-b)); matrix[14] = (float)(-(f+n)/(f-n));
    if (Client.matrixMode == GL_PROJECTION) MatrixMultiply(Client.projection, matrix);
    else if (Client.matrixMode == GL_MODELVIEW) {
        MatrixMultiply(Client.modelview, matrix);
        Client.normalDirty = TRUE;
    }
    RECORD(MGLPPCBatchOrthof(&Client.batch, (float)l, (float)r,
                             (float)b, (float)t, (float)n, (float)f));
}

static void PPCGLHint(GLcontext context, GLenum target, GLenum mode)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchHint(&Client.batch, target, mode)); }

static void PPCGLTexEnvi(GLcontext context, GLenum target, GLenum pname,
                         GLint param)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchTexEnvi(&Client.batch, target, pname, param)); }

static void PPCGLBindTexture(GLcontext context, GLenum target, GLuint texture)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchBindTexture(&Client.batch, target, texture)); }

static void PPCGLTexParameteri(GLcontext context, GLenum target, GLenum pname,
                               GLint param)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchTexParameteri(&Client.batch, target, pname, param)); }

static void PPCGLTexGeni(GLcontext context, GLenum coord, GLenum mode, GLenum map)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchTexGeni(&Client.batch, coord, mode, map)); }

static void PPCGLPixelStorei(GLcontext context, GLenum pname, GLint param)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchPixelStorei(&Client.batch, pname, param)); }

static void PPCMGLSetState(GLcontext context, GLenum cap, GLboolean enabled)
{
    (void)context;
    if (cap == GL_TEXTURE_GEN_S) { Client.texgenS = enabled != 0; return; }
    if (cap == GL_TEXTURE_GEN_T) { Client.texgenT = enabled != 0; return; }
    if (!ClientFlushDraws()) return;
    if (cap == GL_TEXTURE_2D) Client.textureEnabled = enabled != 0;
    RECORD(MGLPPCBatchSetState(&Client.batch, cap, enabled));
}

static void PPCGLFlush(GLcontext context)
{ (void)context; (void)ClientFlush(); }

static GLenum PPCGLGetError(GLcontext context)
{
    GLenum error;
    (void)context;
    if (!Client.insidePrimitive)
        (void)ClientFlush();
    error = Client.lastError;
    Client.lastError = GL_NO_ERROR;
    return error;
}

static BOOL ClientCommand(MGLPPCMessage *message, ULONG command)
{
    message->command = command;
    message->contextId = Client.contextId;
    if (!MGLPPCTransportCall(&Client.transport, message)) {
        Client.lastError = GL_INVALID_OPERATION;
        return FALSE;
    }
    if (message->status != MGLPPC_STATUS_OK) {
        Client.lastError = message->status == MGLPPC_STATUS_GL_ERROR &&
                           message->value[0] != GL_NO_ERROR
            ? message->value[0] : GL_INVALID_OPERATION;
        return FALSE;
    }
    return TRUE;
}

static void PPCGLGenTextures(GLcontext context, GLsizei n, GLuint *textures)
{
    MGLPPCMessage message;
    GLsizei done = 0;
    (void)context;
    if (n < 0 || (n > 0 && textures == NULL) || Client.insidePrimitive) {
        Client.lastError = GL_INVALID_VALUE;
        return;
    }
    if (!ClientFlush()) return;
    while (done < n) {
        ULONG count = (ULONG)(n - done);
        ULONG i;
        if (count > 8) count = 8;
        memset(&message, 0, sizeof(message));
        message.value[0] = count;
        if (!ClientCommand(&message, MGLPPC_CMD_GEN_TEXTURES)) return;
        for (i = 0; i < count; ++i)
            textures[done + (GLsizei)i] = message.value[i];
        done += (GLsizei)count;
    }
}

static void PPCGLDeleteTextures(GLcontext context, GLsizei n,
                                const GLuint *textures)
{
    MGLPPCMessage message;
    GLsizei done = 0;
    (void)context;
    if (n < 0 || (n > 0 && textures == NULL) || Client.insidePrimitive) {
        Client.lastError = GL_INVALID_VALUE;
        return;
    }
    if (!ClientFlush()) return;
    while (done < n) {
        ULONG count = (ULONG)(n - done);
        ULONG i;
        if (count > 7) count = 7;
        memset(&message, 0, sizeof(message));
        message.value[0] = count;
        for (i = 0; i < count; ++i)
            message.value[i + 1] = textures[done + (GLsizei)i];
        if (!ClientCommand(&message, MGLPPC_CMD_DELETE_TEXTURES)) return;
        done += (GLsizei)count;
    }
}

static ULONG ClientTextureByteCount(GLsizei width, GLsizei height,
                                    GLenum format, GLenum type)
{
    ULONG components;
    ULONG w = (ULONG)width, h = (ULONG)height;
    if (width <= 0 || height <= 0 || type != GL_UNSIGNED_BYTE) return 0;
    if (format == GL_RGB) components = 3;
    else if (format == GL_RGBA) components = 4;
    else return 0;
    if (w > 0xffffffffUL / h || w * h > 0xffffffffUL / components) return 0;
    return w * h * components;
}

static void PPCGLTexImage2D(GLcontext context, GLenum target, GLint level,
                            GLint internalformat, GLsizei width, GLsizei height,
                            GLint border, GLenum format, GLenum type,
                            const GLvoid *pixels)
{
    MGLPPCMessage begin, end;
    MGLPPCMessage *chunkMessage;
    const UBYTE *source = (const UBYTE *)pixels;
    ULONG total = ClientTextureByteCount(width, height, format, type);
    ULONG offset = 0;
    (void)context;
    if (total == 0 || pixels == NULL || Client.insidePrimitive) {
        Client.lastError = GL_INVALID_VALUE;
        return;
    }
    if (!ClientFlush()) return;
    memset(&begin, 0, sizeof(begin));
    begin.value[0] = target; begin.value[1] = (ULONG)level;
    begin.value[2] = (ULONG)internalformat; begin.value[3] = (ULONG)width;
    begin.value[4] = (ULONG)height; begin.value[5] = (ULONG)border;
    begin.value[6] = format; begin.value[7] = type;
    if (!ClientCommand(&begin, MGLPPC_CMD_TEXTURE_UPLOAD_BEGIN)) return;

    chunkMessage = Client.batch.message;
    while (offset < total) {
        ULONG byteCount = total - offset;
        if (byteCount > MGLPPC_TEXTURE_CHUNK_WORDS * sizeof(ULONG))
            byteCount = MGLPPC_TEXTURE_CHUNK_WORDS * sizeof(ULONG);
        MGLPPCBatchReset(&Client.batch, Client.contextId);
        chunkMessage->command = MGLPPC_CMD_TEXTURE_UPLOAD_CHUNK;
        chunkMessage->value[0] = offset;
        chunkMessage->value[1] = byteCount;
        chunkMessage->payloadWords = (byteCount + sizeof(ULONG) - 1) / sizeof(ULONG);
        memcpy(chunkMessage->payload, source + offset, byteCount);
        if (!MGLPPCTransportCall(&Client.transport, chunkMessage) ||
            chunkMessage->status != MGLPPC_STATUS_OK) {
            Client.lastError = GL_OUT_OF_MEMORY;
            MGLPPCBatchReset(&Client.batch, Client.contextId);
            return;
        }
        offset += byteCount;
    }
    MGLPPCBatchReset(&Client.batch, Client.contextId);
    memset(&end, 0, sizeof(end));
    (void)ClientCommand(&end, MGLPPC_CMD_TEXTURE_UPLOAD_END);
}

static void PPCGLGetIntegerv(GLcontext context, GLenum pname, GLint *params)
{
    MGLPPCMessage message;
    (void)context;
    if (params == NULL || Client.insidePrimitive) {
        Client.lastError = GL_INVALID_VALUE;
        return;
    }
    if (!ClientFlush()) return;
    memset(&message, 0, sizeof(message));
    message.value[0] = pname;
    if (ClientCommand(&message, MGLPPC_CMD_GET_INTEGER))
        *params = (GLint)message.value[0];
}

static void PPCChoosePixelDepth(int depth) { Client.pixelDepth = depth; }
static void PPCChooseWindowMode(GLboolean enabled) { Client.windowMode = enabled != 0; }
static void PPCChooseVertexBufferSize(int size) { Client.vertexBufferSize = size; }
static void PPCChooseTextureBufferSize(int size) { Client.textureBufferSize = size; }
static void PPCChooseMtexBufferSize(int size) { Client.mtexBufferSize = size; }
static void PPCChooseNumberOfBuffers(int count) { Client.numberOfBuffers = count; }

static void *PPCMGLCreateContext(int offx, int offy, int width, int height)
{
    MGLPPCMessage message;
    (void)offx; (void)offy;

    if (!Client.opened || Client.currentContext != NULL)
        return NULL;
    memset(&message, 0, sizeof(message));
    message.command = MGLPPC_CMD_CREATE_CONTEXT;
    message.value[0] = width; message.value[1] = height;
    message.value[2] = Client.pixelDepth; message.value[3] = Client.windowMode;
    message.value[4] = Client.vertexBufferSize;
    message.value[5] = Client.textureBufferSize;
    message.value[6] = Client.mtexBufferSize;
    message.value[7] = Client.numberOfBuffers;
    if (!MGLPPCTransportCall(&Client.transport, &message) ||
        message.status != MGLPPC_STATUS_OK || message.contextId == 0)
        return NULL;
    Client.contextId = message.contextId;
    Client.windowHandle = (void *)message.value[0];
    if (!MGLPPCBatchInit(&Client.batch, Client.contextId,
                         MGLPPC_MAX_BATCH_WORDS)) {
        message.command = MGLPPC_CMD_DESTROY_CONTEXT;
        (void)MGLPPCTransportCall(&Client.transport, &message);
        Client.contextId = 0;
        return NULL;
    }
    Client.batchReady = TRUE;
    Client.normalX = Client.normalY = 0.0f;
    Client.normalZ = 1.0f;
    Client.normalDirty = TRUE;
    Client.currentColor = 0xffffffffUL;
    Client.matrixMode = GL_MODELVIEW;
    Client.modelviewDepth = 0;
    Client.projectionDepth = 0;
    Client.drawCount = 0;
    Client.primitiveCount = 0;
    Client.textureEnabled = FALSE;
    Client.texgenS = Client.texgenT = FALSE;
    MatrixIdentity(Client.modelview);
    MatrixIdentity(Client.projection);
    Client.drawableWidth = Client.viewportWidth = width;
    Client.drawableHeight = Client.viewportHeight = height;
    Client.viewportX = Client.viewportY = 0;
    Client.currentContext = (GLcontext)&Client;
    return Client.currentContext;
}

static void PPCMGLDeleteContext(GLcontext context)
{
    MGLPPCMessage message;
    (void)context;
    if (Client.contextId == 0)
        return;
    if (!Client.insidePrimitive)
        (void)ClientFlush();
    memset(&message, 0, sizeof(message));
    message.command = MGLPPC_CMD_DESTROY_CONTEXT;
    message.contextId = Client.contextId;
    (void)MGLPPCTransportCall(&Client.transport, &message);
    if (Client.batchReady)
        MGLPPCBatchDestroy(&Client.batch);
    Client.batchReady = FALSE;
    Client.contextId = 0;
    Client.currentContext = NULL;
    Client.insidePrimitive = FALSE;
    Client.windowHandle = NULL;
}

static void PPCMGLSwitchDisplay(GLcontext context)
{
    (void)context;
    if (!ClientFlushDraws()) return;
    RECORD(MGLPPCBatchPresent(&Client.batch));
    (void)ClientSubmitBatch();
}

static void PPCMGLEnableSync(GLcontext context, GLboolean enabled)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchEnableSync(&Client.batch, enabled)); }

static void PPCMGLLockMode(GLcontext context, GLenum mode)
{ (void)context; if (ClientFlushDraws()) RECORD(MGLPPCBatchLockMode(&Client.batch, mode)); }

static GLboolean PPCMGLLockDisplay(GLcontext context)
{ (void)context; return GL_TRUE; }

static void PPCMGLUnlockDisplay(GLcontext context) { (void)context; }
static void *PPCMGLGetWindowHandle(GLcontext context)
{ (void)context; return Client.windowHandle; }

static const MGLDispatchTable PPCDispatch = {
    .abiVersion = MINIGL_DISPATCH_ABI_VERSION,
    .structSize = sizeof(MGLDispatchTable),
    .backendFlags = 0,
    .currentContext = &Client.currentContext,
    .GLBegin = PPCGLBegin,
    .GLBindTexture = PPCGLBindTexture,
    .GLClear = PPCGLClear,
    .GLClearColor = PPCGLClearColor,
    .GLColor4f = PPCGLColor4f,
    .GLColor4ub = PPCGLColor4ub,
    .GLDeleteTextures = PPCGLDeleteTextures,
    .GLEnd = PPCGLEnd,
    .GLFinish = PPCGLFlush,
    .GLFlush = PPCGLFlush,
    .GLFrustum = PPCGLFrustum,
    .GLGenTextures = PPCGLGenTextures,
    .GLGetError = PPCGLGetError,
    .GLGetIntegerv = PPCGLGetIntegerv,
    .GLHint = PPCGLHint,
    .GLLoadIdentity = PPCGLLoadIdentity,
    .GLMatrixMode = PPCGLMatrixMode,
    .GLNormal3f = PPCGLNormal3f,
    .GLOrtho = PPCGLOrtho,
    .GLPixelStorei = PPCGLPixelStorei,
    .GLPopMatrix = PPCGLPopMatrix,
    .GLPushMatrix = PPCGLPushMatrix,
    .GLRotated = PPCGLRotated,
    .GLRotatef = PPCGLRotatef,
    .GLScaled = PPCGLScaled,
    .GLScalef = PPCGLScalef,
    .GLTexEnvi = PPCGLTexEnvi,
    .GLTexGeni = PPCGLTexGeni,
    .GLTexImage2D = PPCGLTexImage2D,
    .GLTexParameteri = PPCGLTexParameteri,
    .GLTranslated = PPCGLTranslated,
    .GLTranslatef = PPCGLTranslatef,
    .GLVertex4f = PPCGLVertex4f,
    .GLViewport = PPCGLViewport,
    .MGLCreateContext = PPCMGLCreateContext,
    .MGLDeleteContext = PPCMGLDeleteContext,
    .MGLEnableSync = PPCMGLEnableSync,
    .MGLGetInputWindowHandle = PPCMGLGetWindowHandle,
    .MGLGetWindowHandle = PPCMGLGetWindowHandle,
    .MGLLockDisplay = PPCMGLLockDisplay,
    .MGLLockMode = PPCMGLLockMode,
    .MGLSetState = PPCMGLSetState,
    .MGLSwitchDisplay = PPCMGLSwitchDisplay,
    .MGLUnlockDisplay = PPCMGLUnlockDisplay,
    .mglChooseMtexBufferSize = PPCChooseMtexBufferSize,
    .mglChooseNumberOfBuffers = PPCChooseNumberOfBuffers,
    .mglChoosePixelDepth = PPCChoosePixelDepth,
    .mglChooseTextureBufferSize = PPCChooseTextureBufferSize,
    .mglChooseVertexBufferSize = PPCChooseVertexBufferSize,
    .mglChooseWindowMode = PPCChooseWindowMode
};

BOOL MiniGLOpen(void)
{
    MGLPPCMessage message;

    if (Client.opened)
        return TRUE;
    memset(&Client, 0, sizeof(Client));
    Client.pixelDepth = 32;
    Client.windowMode = TRUE;
    if (!MGLPPCTransportOpen(&Client.transport))
        return FALSE;
    memset(&message, 0, sizeof(message));
    message.command = MGLPPC_CMD_HELLO;
    if (!MGLPPCTransportCall(&Client.transport, &message) ||
        message.status != MGLPPC_STATUS_OK) {
        MGLPPCTransportClose(&Client.transport);
        memset(&Client, 0, sizeof(Client));
        return FALSE;
    }
    Client.opened = TRUE;
    MiniGLDispatch = &PPCDispatch;
    return TRUE;
}

void MiniGLClose(void)
{
    MGLPPCMessage message;

    if (!Client.opened)
        return;
    if (Client.currentContext != NULL)
        PPCMGLDeleteContext(Client.currentContext);
    memset(&message, 0, sizeof(message));
    message.command = MGLPPC_CMD_SHUTDOWN;
    (void)MGLPPCTransportCall(&Client.transport, &message);
    MGLPPCTransportClose(&Client.transport);
    MiniGLDispatch = NULL;
    memset(&Client, 0, sizeof(Client));
}
