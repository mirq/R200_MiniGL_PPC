#include <exec/types.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct timeval {
    ULONG tv_secs;
    ULONG tv_micro;
};

struct PPCBase;
extern struct PPCBase *PowerPCBase;
VOID __GetSysTimePPC(void *, struct timeval *) =
    "\tlwz\tr0,-682(r3)\n\tmtlr\tr0\n\tblrl";
#define GetSysTimePPC(value) __GetSysTimePPC(PowerPCBase, (value))

#include <libraries/minigl_ppc_batch.h>

#define TWO_PI 6.28318530717958647692
#define FRAME_COUNT 240
#define MAX_GEAR_VERTICES 1200

typedef struct GearVertex {
    float x;
    float y;
    ULONG color;
} GearVertex;

typedef struct GearMesh {
    GearVertex *vertices;
    ULONG vertexCount;
} GearMesh;

static BOOL Call(MGLPPCTransport *transport, MGLPPCMessage *message,
                 ULONG command)
{
    message->command = command;
    if (!MGLPPCTransportCall(transport, message)) {
        printf("FAIL: command %lu transport failed\n", (unsigned long)command);
        return FALSE;
    }
    if (message->status != MGLPPC_STATUS_OK) {
        printf("FAIL: command %lu status %ld\n",
               (unsigned long)command, (long)message->status);
        return FALSE;
    }
    return TRUE;
}

static ULONG ElapsedMicros(const struct timeval *start,
                           const struct timeval *end)
{
    LONG seconds = (LONG)(end->tv_secs - start->tv_secs);
    LONG micros = (LONG)end->tv_micro - (LONG)start->tv_micro;

    if (micros < 0) {
        --seconds;
        micros += 1000000;
    }
    return (ULONG)seconds * 1000000UL + (ULONG)micros;
}

static ULONG FloatToBits(float value)
{
    union {
        float value;
        ULONG bits;
    } conversion;

    conversion.value = value;
    return conversion.bits;
}

static BOOL AddGearVertex(GearMesh *mesh, ULONG capacity, double angle,
                          float radius, ULONG color)
{
    GearVertex *vertex;

    if (mesh->vertexCount >= capacity)
        return FALSE;
    vertex = &mesh->vertices[mesh->vertexCount++];
    vertex->x = (float)cos(angle) * radius;
    vertex->y = (float)sin(angle) * radius;
    vertex->color = color;
    return TRUE;
}

static BOOL CreateGear(GearMesh *mesh, int teeth,
                       UBYTE red, UBYTE green, UBYTE blue)
{
    static const float outerRadius[5] = {0.72f, 1.0f, 1.0f, 0.72f, 0.72f};
    const float innerRadius = 0.34f;
    ULONG segmentCount = (ULONG)teeth * 4UL;
    ULONG capacity = (segmentCount + 1UL) * 2UL;
    ULONG boundary;

    memset(mesh, 0, sizeof(*mesh));
    mesh->vertices = malloc(sizeof(*mesh->vertices) * capacity);
    if (mesh->vertices == NULL)
        return FALSE;

    for (boundary = 0; boundary <= segmentCount; ++boundary) {
        ULONG wrapped = boundary == segmentCount ? 0 : boundary;
        ULONG tooth = wrapped / 4UL;
        ULONG segment = wrapped & 3UL;
        double angle = (double)boundary * TWO_PI / (double)segmentCount;
        UBYTE shade = (tooth & 1) ? 24 : 0;
        UBYTE r = red > shade ? red - shade : 0;
        UBYTE g = green > shade ? green - shade : 0;
        UBYTE b = blue > shade ? blue - shade : 0;
        ULONG color = (255UL << 24) | ((ULONG)r << 16) |
                      ((ULONG)g << 8) | (ULONG)b;

        if (!AddGearVertex(mesh, capacity, angle, innerRadius, color) ||
            !AddGearVertex(mesh, capacity, angle,
                           outerRadius[segment], color))
            return FALSE;
    }

    return TRUE;
}

static void DestroyGear(GearMesh *mesh)
{
    free(mesh->vertices);
    memset(mesh, 0, sizeof(*mesh));
}

static BOOL TransformGear(MGLPPCVertexC4ubV3f *vertices, ULONG capacity,
                          ULONG *vertexCount, const GearMesh *mesh,
                          float centerX, float centerY, float scale,
                          double rotation)
{
    float cosine = (float)cos(rotation);
    float sine = (float)sin(rotation);
    ULONG index;

    if (mesh->vertexCount > capacity - *vertexCount)
        return FALSE;

    for (index = 0; index < mesh->vertexCount; ++index) {
        const GearVertex *source = &mesh->vertices[index];
        MGLPPCVertexC4ubV3f *destination = &vertices[(*vertexCount)++];
        float x = centerX + scale *
            (cosine * source->x - sine * source->y);
        float y = centerY + scale *
            (sine * source->x + cosine * source->y);

        destination->x = FloatToBits(x);
        destination->y = FloatToBits(y);
        destination->z = 0;
        destination->color = source->color;
    }

    return TRUE;
}

int main(void)
{
    MGLPPCTransport transport;
    MGLPPCMessage message;
    MGLPPCBatch batch;
    MGLPPCVertexC4ubV3f *vertices = NULL;
    GearMesh gear1;
    GearMesh gear2;
    GearMesh gear3;
    ULONG contextId = 0;
    ULONG buildMicros = 0;
    ULONG submitMicros = 0;
    ULONG timedFrames = 0;
    BOOL batchReady = FALSE;
    int frame;
    int result = 20;

    printf("MiniGLPPC animated gears (PPC geometry)\n");
    memset(&gear1, 0, sizeof(gear1));
    memset(&gear2, 0, sizeof(gear2));
    memset(&gear3, 0, sizeof(gear3));
    if (!MGLPPCTransportOpen(&transport)) {
        printf("FAIL: cannot create PPC reply port\n");
        return 20;
    }

    memset(&message, 0, sizeof(message));
    message.value[0] = 480;
    message.value[1] = 360;
    message.value[2] = 32;
    message.value[3] = 1;
    if (!Call(&transport, &message, MGLPPC_CMD_CREATE_CONTEXT))
        goto shutdown;
    contextId = message.contextId;

    if (!MGLPPCBatchInit(&batch, contextId, MGLPPC_DEFAULT_BATCH_WORDS)) {
        printf("FAIL: cannot allocate gears command batch\n");
        goto destroy;
    }
    batchReady = TRUE;

    vertices = malloc(sizeof(*vertices) * MAX_GEAR_VERTICES);
    if (vertices == NULL || !CreateGear(&gear1, 20, 220, 64, 48) ||
        !CreateGear(&gear2, 12, 64, 205, 112) ||
        !CreateGear(&gear3, 10, 64, 116, 235)) {
        printf("FAIL: cannot allocate PPC gear vertices\n");
        goto destroy;
    }

    for (frame = 0; frame < FRAME_COUNT; ++frame) {
        struct timeval frameStart;
        struct timeval buildDone;
        struct timeval submitDone;
        double angle = (double)frame * 0.025;
        ULONG vertexCount = 0;
        ULONG gear1Count;
        ULONG gear2Count;
        ULONG gear3Count;

        GetSysTimePPC(&frameStart);
        MGLPPCBatchReset(&batch, contextId);
        if (!TransformGear(vertices, MAX_GEAR_VERTICES, &vertexCount,
                           &gear1, -0.28f, 0.02f, 0.48f, angle)) {
            printf("FAIL: gear transform overflow at frame %ld\n", (long)frame);
            goto destroy;
        }
        gear1Count = vertexCount;
        if (!TransformGear(vertices, MAX_GEAR_VERTICES, &vertexCount,
                           &gear2, 0.43f, 0.24f, 0.32f,
                           -angle * 20.0 / 12.0 + 0.08)) {
            printf("FAIL: gear transform overflow at frame %ld\n", (long)frame);
            goto destroy;
        }
        gear2Count = vertexCount - gear1Count;
        if (!TransformGear(vertices, MAX_GEAR_VERTICES, &vertexCount,
                           &gear3, 0.37f, -0.43f, 0.27f,
                           angle * 2.0 + 0.16)) {
            printf("FAIL: gear transform overflow at frame %ld\n", (long)frame);
            goto destroy;
        }
        gear3Count = vertexCount - gear1Count - gear2Count;

        if (!MGLPPCBatchClearColor(&batch, 0.025f, 0.035f, 0.07f, 1.0f) ||
            !MGLPPCBatchClear(&batch, MGLPPC_GL_COLOR_BUFFER_BIT) ||
            !MGLPPCBatchDrawC4ubV3f(&batch, MGLPPC_GL_TRIANGLE_STRIP,
                                    vertices, gear1Count) ||
            !MGLPPCBatchDrawC4ubV3f(&batch, MGLPPC_GL_TRIANGLE_STRIP,
                                    vertices + gear1Count, gear2Count) ||
            !MGLPPCBatchDrawC4ubV3f(&batch, MGLPPC_GL_TRIANGLE_STRIP,
                                    vertices + gear1Count + gear2Count,
                                    gear3Count) ||
            !MGLPPCBatchPresent(&batch) ||
            (frame == FRAME_COUNT - 1 &&
             !MGLPPCBatchDelay(&batch, 100))) {
            printf("FAIL: gears batch overflow at frame %ld\n", (long)frame);
            goto destroy;
        }

        GetSysTimePPC(&buildDone);
        if (!MGLPPCBatchSubmit(&transport, &batch)) {
            printf("FAIL: gears transport at frame %ld\n", (long)frame);
            goto destroy;
        }
        GetSysTimePPC(&submitDone);

        if (frame != FRAME_COUNT - 1) {
            buildMicros += ElapsedMicros(&frameStart, &buildDone);
            submitMicros += ElapsedMicros(&buildDone, &submitDone);
            ++timedFrames;
        }
        if (batch.message->status != MGLPPC_STATUS_OK) {
            printf("FAIL: gears status %ld, GL error %08lx at frame %ld\n",
                   (long)batch.message->status,
                   (unsigned long)batch.message->value[0], (long)frame);
            goto destroy;
        }

        if ((frame % 60) == 0)
            printf("frame %ld/%ld, batch %lu words\n",
                   (long)frame, (long)FRAME_COUNT,
                   (unsigned long)batch.message->payloadWords);
    }

    printf("average: PPC build %lu us, bridge+render %lu us per frame\n",
           (unsigned long)(buildMicros / timedFrames),
           (unsigned long)(submitMicros / timedFrames));

    result = 0;

destroy:
    DestroyGear(&gear3);
    DestroyGear(&gear2);
    DestroyGear(&gear1);
    free(vertices);
    if (batchReady)
        MGLPPCBatchDestroy(&batch);
    if (contextId != 0) {
        memset(&message, 0, sizeof(message));
        message.contextId = contextId;
        if (!Call(&transport, &message, MGLPPC_CMD_DESTROY_CONTEXT))
            result = 20;
    }

shutdown:
    memset(&message, 0, sizeof(message));
    if (!Call(&transport, &message, MGLPPC_CMD_SHUTDOWN))
        result = 20;
    MGLPPCTransportClose(&transport);
    if (result == 0)
        printf("OK: PPC animated gears completed\n");
    return result;
}
