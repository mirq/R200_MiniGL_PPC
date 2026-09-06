/*
 * Alignment ground-truth probe (generated): offsetof()&3 for every float
 * member under the archive's flags. All members must pass.
 */
#include <exec/types.h>
#include <stddef.h>
#include <stdio.h>
#include "minigl_r200_internal.h"

static int bad = 0;
#define CHK(s, m) \
    do { \
        long o = (long)offsetof(struct s, m); \
        if (o & 3L) { printf("BAD " #s "." #m " off=%ld mod4=%ld\n", o, o & 3L); bad = 1; } \
    } while (0)

int main(void)
{

    CHK(MGLFastPath, w);
    CHK(MGLFastPath, a);
    CHK(MGLFastPath, v0);
    CHK(MGLFastPath, v1);
    CHK(MGLFastPath, q);
    CHK(MGLFastPath, currentColor);
    CHK(MGLFastPath, currentTex0);
    CHK(MGLFastPath, currentTex1);
    CHK(MGLFastPath, currentQ);
    CHK(Vertex, x);
    CHK(Vertex, y);
    CHK(Vertex, z);
    CHK(Vertex, w);
    CHK(Vertex, nx);
    CHK(Vertex, ny);
    CHK(Vertex, nz);
    CHK(Vertex, s);
    CHK(Vertex, t);
    CHK(Vertex, s1);
    CHK(Vertex, t1);
    CHK(Vertex, fog);
    CHK(FastVertex, x);
    CHK(FastVertex, y);
    CHK(FastVertex, z);
    CHK(FastVertex, w);
    CHK(FastVertex, r);
    CHK(FastVertex, g);
    CHK(FastVertex, b);
    CHK(FastVertex, a);
    CHK(FastVertex, u0);
    CHK(FastVertex, v0);
    CHK(FastVertex, u1);
    CHK(FastVertex, v1);
    CHK(FastVertex, q);
    CHK(ScreenVertex, x);
    CHK(ScreenVertex, y);
    CHK(ScreenVertex, z);
    CHK(ScreenVertex, rhw);
    CHK(ScreenVertex, nx);
    CHK(ScreenVertex, ny);
    CHK(ScreenVertex, nz);
    CHK(ScreenVertex, s);
    CHK(ScreenVertex, t);
    CHK(ScreenVertex, s1);
    CHK(ScreenVertex, t1);
    CHK(ScreenVertex, fog);
    CHK(ClipVertex, x);
    CHK(ClipVertex, y);
    CHK(ClipVertex, z);
    CHK(ClipVertex, w);
    CHK(ClipVertex, s);
    CHK(ClipVertex, t);
    CHK(ClipVertex, s1);
    CHK(ClipVertex, t1);
    CHK(ClipVertex, fog);
    CHK(LightState, ambient);
    CHK(LightState, diffuse);
    CHK(LightState, specular);
    CHK(LightState, position);
    CHK(LightState, eyePosition);
    CHK(LightState, spotDirection);
    CHK(LightState, eyeSpotDirection);
    CHK(LightState, spotExponent);
    CHK(LightState, spotCutoff);
    CHK(LightState, spotCosCutoff);
    CHK(LightState, constantAttenuation);
    CHK(LightState, linearAttenuation);
    CHK(LightState, quadraticAttenuation);
    CHK(MiniGLR200Context, currentNormalX);
    CHK(MiniGLR200Context, currentNormalY);
    CHK(MiniGLR200Context, currentNormalZ);
    CHK(MiniGLR200Context, depthNear);
    CHK(MiniGLR200Context, depthFar);
    CHK(MiniGLR200Context, clearDepth);
    CHK(MiniGLR200Context, frontEmissive);
    CHK(MiniGLR200Context, frontAmbient);
    CHK(MiniGLR200Context, frontDiffuse);
    CHK(MiniGLR200Context, frontSpecular);
    CHK(MiniGLR200Context, frontShininess);
    CHK(MiniGLR200Context, lightModelAmbient);
    CHK(MiniGLR200Context, inverseModelview);
    CHK(MiniGLR200Context, eyeZDir);
    CHK(MiniGLR200Context, modelviewInvScale);
    CHK(MiniGLR200Context, currentT);
    CHK(MiniGLR200Context, normalMatrix);
    CHK(MiniGLR200Context, combinedMatrix);
    CHK(MiniGLR200Context, fogStart);
    CHK(MiniGLR200Context, fogEnd);
    CHK(MiniGLR200Context, fogDensity);
    CHK(MiniGLR200Context, alphaRef);
    CHK(MiniGLR200Context, minTriArea);
    CHK(MiniGLR200Context, zOffset);
    CHK(MiniGLR200Context, polygonOffsetUnits);
    CHK(MiniGLR200Context, pointSize);
    CHK(MiniGLR200Context, sphereObjectNX);
    CHK(MiniGLR200Context, sphereObjectNY);
    CHK(MiniGLR200Context, sphereObjectNZ);
    CHK(MiniGLR200Context, sphereEyeNX);
    CHK(MiniGLR200Context, sphereEyeNY);
    CHK(MiniGLR200Context, sphereEyeNZ);
    /* array/member bases the vertex hot path writes through */
    {
        long o1 = (long)offsetof(struct MiniGLR200Context, vertices);
        long o2 = (long)offsetof(struct MiniGLR200Context, screenVertices);
        long o3 = (long)offsetof(struct MiniGLR200Context, pendingDraw);
        long o4 = (long)offsetof(struct MiniGLR200Context, submitSlots);
        long o5 = (long)offsetof(struct MiniGLR200Context, segment);
        long o6 = (long)offsetof(struct MiniGLR200Context, currentColor);
        long o7 = (long)offsetof(struct MiniGLR200Context, currentS);
        printf("bases vertices=%ld screenV=%ld pendingDraw=%ld "
               "slots=%ld segment=%ld currentColor=%ld currentS=%ld\n",
               o1 & 3L, o2 & 3L, o3 & 3L, o4 & 3L, o5 & 3L, o6 & 3L,
               o7 & 3L);
        if ((o1 | o2 | o3 | o4 | o5 | o6 | o7) & 3L) bad = 1;
    }
    printf("SIZEOF RastPort=%ld Radeon3DSurface=%ld SubmitSlot=%ld "
           "StreamSegment=%ld DeferredSegment=%ld PendingDrawState=%ld "
           "TextureObject=%ld LightState=%ld ArrayState=%ld "
           "PerfMetrics=%ld R200SegmentLease=%ld Vertex=%ld "
           "ScreenVertex=%ld MGLPPCRingControlV1=%ld\n",
           (long)sizeof(struct RastPort),
           (long)sizeof(struct Radeon3DSurface),
           (long)sizeof(struct SubmitSlot),
           (long)sizeof(struct StreamSegment),
           (long)sizeof(struct DeferredSegment),
           (long)sizeof(struct PendingDrawState),
           (long)sizeof(struct TextureObject),
           (long)sizeof(struct LightState),
           (long)sizeof(struct ArrayState),
           (long)sizeof(struct PerfMetrics),
           (long)sizeof(struct R200SegmentLease),
           (long)sizeof(struct Vertex),
           (long)sizeof(struct ScreenVertex),
           (long)sizeof(struct MGLPPCRingControlV1));
    if (!bad) printf("ALIGN OK: all float members 4-aligned\n");
    fflush(stdout);
    return bad;
}
