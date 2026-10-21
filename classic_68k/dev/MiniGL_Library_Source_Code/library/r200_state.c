#include "minigl_r200_internal.h"
#include <math.h>
#include "debug_output.h"

/* Normal/texgen/lighting header emission, defined with the lighting helpers. */
static void WriteSphereMapHeader(struct MiniGLR200Context *context,
                                 struct PendingDrawState *state,
                                 ULONG *options);
static void WriteNormalMatrices(struct MiniGLR200Context *context,
                                struct PendingDrawState *state,
                                ULONG *options);
static void WriteLightingHeader(struct MiniGLR200Context *context,
                                 struct PendingDrawState *state,
                                 ULONG *options);

static void CopyVector4(GLfloat *destination, const GLfloat *source)
{
    destination[0] = source[0];
    destination[1] = source[1];
    destination[2] = source[2];
    destination[3] = source[3];
}

static ULONG DepthFunctionOption(GLenum function)
{
    switch (function) {
    case GL_LESS: return RADEON3D_DEPTH_LESS;
    case GL_LEQUAL: return RADEON3D_DEPTH_LEQUAL;
    case GL_EQUAL: return RADEON3D_DEPTH_EQUAL;
    case GL_GEQUAL: return RADEON3D_DEPTH_GEQUAL;
    case GL_GREATER: return RADEON3D_DEPTH_GREATER;
    case GL_NOTEQUAL: return RADEON3D_DEPTH_NOTEQUAL;
    case GL_NEVER: return RADEON3D_DEPTH_NEVER;
    default: return RADEON3D_DEPTH_ALWAYS;
    }
}

static ULONG BlendOption(GLenum factor)
{
    switch (factor) {
    case GL_ZERO: return RADEON3D_BLEND_ZERO;
    case GL_ONE: return RADEON3D_BLEND_ONE;
    case GL_SRC_COLOR: return RADEON3D_BLEND_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return RADEON3D_BLEND_ONE_MINUS_SRC_COLOR;
    case GL_DST_COLOR: return RADEON3D_BLEND_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return RADEON3D_BLEND_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA: return RADEON3D_BLEND_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return RADEON3D_BLEND_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return RADEON3D_BLEND_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return RADEON3D_BLEND_ONE_MINUS_DST_ALPHA;
    /* Saturate is source-only on RV280; unreachable through
     * Lib_glBlendFunc validation, so fall back to a factor that is
     * legal in both source and destination positions. */
    default: return RADEON3D_BLEND_ZERO;
    }
}

static ULONG FragmentState(const struct MiniGLR200Context *context)
{
    ULONG state = 0;
    if (context->alphaTestEnabled)
        state |= RADEON3D_FRAGMENT_ALPHA_TEST |
                 (DepthFunctionOption(context->alphaFunc) <<
                  RADEON3D_FRAGMENT_ALPHA_FUNC_SHIFT) |
                 ((ULONG)ColorByte(context->alphaRef) <<
                  RADEON3D_FRAGMENT_ALPHA_REF_SHIFT);
    if (context->blendEnabled)
        state |= RADEON3D_FRAGMENT_BLEND |
                 (BlendOption(context->blendSource) <<
                  RADEON3D_FRAGMENT_SRC_SHIFT) |
                 (BlendOption(context->blendDestination) <<
                  RADEON3D_FRAGMENT_DST_SHIFT);
    return state;
}

ULONG HardwareSphereMapMask(const struct MiniGLR200Context *context)
{
    ULONG mask = 0, unit;

    if (!(context->radeonCaps & RADEON3D_CAP_HW_SPHERE_MAP))
        return 0;
    for (unit = 0; unit < 2UL; ++unit)
        if (context->textureEnabled[unit] &&
            context->textureGenS[unit] && context->textureGenT[unit] &&
            context->textureGenModeS[unit] == GL_SPHERE_MAP &&
            context->textureGenModeT[unit] == GL_SPHERE_MAP)
            mask |= 1UL << unit;
    return mask;
}

static BOOL HardwareSphereMapEnabled(const struct MiniGLR200Context *context)
{
    return HardwareSphereMapMask(context) != 0;
}

static ULONG NativeQuadVertexLimit(const struct MiniGLR200Context *context)
{
    BOOL perspective = context->screenProjective && !context->fogEnabled;
    BOOL extendedVertex = context->fogEnabled || context->textureEnabled[1] ||
                   perspective;
    BOOL fragmentStatePresent = extendedVertex || context->textureEnabled[0] ||
                   context->blendEnabled || context->alphaTestEnabled;
    BOOL textured = context->textureEnabled[0];
    BOOL textured1 = context->textureEnabled[1];
    ULONG headerDwords = extendedVertex ? RADEON3D_EXEC_DRAW_EXTENDED_HEADER_DWORDS :
                         fragmentStatePresent ? RADEON3D_EXEC_DRAW_FRAGMENT_HEADER_DWORDS :
                                   RADEON3D_EXEC_DRAW_HEADER_DWORDS;
    ULONG publicVertexDwords = extendedVertex ? RADEON3D_EXEC_EXTENDED_VERTEX_DWORDS :
                                         RADEON3D_EXEC_VERTEX_DWORDS;
    ULONG generatedFixed = GeneratedDrawDwords(
        0, context->depthTest, textured, textured1, context->fogEnabled,
        perspective);
    ULONG generatedVertexDwords = GeneratedVertexDwords(
        context->depthTest, textured, textured1, context->fogEnabled,
        perspective);
    ULONG limit = extendedVertex ? RADEON3D_IMMD_MAX_EXTENDED_QUAD_VERTICES :
                           RADEON3D_IMMD_MAX_QUAD_VERTICES;
    ULONG recordLimit;
    ULONG generatedLimit;

    if (context->screenHardwareTransform) {
        BOOL sphereMap = HardwareSphereMapEnabled(context);
        BOOL normalVertex = context->lightingEnabled || sphereMap;
        BOOL compactVertex = context->compactTclVertex &&
                             !context->textureEnabled[1] &&
                             !context->fogEnabled;
        ULONG enabledLights = 0, scan;

        if (context->lightingEnabled)
            for (scan = 0; scan < R200_MAX_LIGHTS; ++scan)
                if (context->lights[scan].enabled)
                    enabledLights++;
        headerDwords = sphereMap ? R200_TEXGEN_HEADER_DWORDS :
                                  R200_TCL_HEADER_DWORDS;
        if (normalVertex)
            headerDwords += R200_NORMAL_MATRICES_DWORDS;
        if (context->lightingEnabled)
            headerDwords += R200_LIGHT_STATE_DWORDS +
                            R200_LIGHT_BLOCK_DWORDS * enabledLights;
        publicVertexDwords =
            compactVertex ? (normalVertex ? R200_LIT_VERTEX_COMPACT_DWORDS
                                          : R200_TCL_VERTEX_COMPACT_DWORDS)
                          : normalVertex ? R200_LIT_VERTEX_DWORDS
                                         : R200_TCL_VERTEX_DWORDS;
        generatedFixed = GeneratedHardwareDrawDwords(
            0, context->depthTest, textured, textured1,
            context->fogEnabled, normalVertex, enabledLights);
        generatedVertexDwords = GeneratedHardwareVertexDwords(
            textured, textured1, context->fogEnabled,
            normalVertex);
        limit = RADEON3D_IMMD_MAX_EXTENDED_QUAD_VERTICES;
    }
    recordLimit = context->batchDwords > headerDwords
                            ? (context->batchDwords - headerDwords) /
                                  publicVertexDwords
                            : 0;
    generatedLimit = context->batchDwords > generatedFixed
                               ? (context->batchDwords - generatedFixed) /
                                     generatedVertexDwords
                               : 0;

    if (limit > recordLimit) limit = recordLimit;
    if (limit > generatedLimit) limit = generatedLimit;
    return limit & ~3UL;
}

BOOL SamePendingDrawState(const struct PendingDrawState *first,
                                 const struct PendingDrawState *second)
{
    /* Length-aware compare: the scalar prefix is fixed, the header is
     * compared only up to the captured length so unlit batches never pay
     * for the worst-case lighting header. */
    const ULONG *a = (const ULONG *)first;
    const ULONG *b = (const ULONG *)second;
    ULONG index;

    if (first->headerDwords != second->headerDwords)
        return FALSE;
    for (index = 0; index < R200_SNAPSHOT_SCALAR_DWORDS; ++index)
        if (a[index] != b[index])
            return FALSE;
    for (index = 0; index < first->headerDwords; ++index)
        if (first->header[index] != second->header[index])
            return FALSE;
    return TRUE;
}

void CopyPendingDrawState(struct PendingDrawState *destination,
                          const struct PendingDrawState *source)
{
    /* Length-aware copy: only the scalar prefix plus the used header span
     * moves, so captures never pay for the worst-case lighting/texgen
     * tail of the struct. */
    ULONG *d = (ULONG *)destination;
    const ULONG *s = (const ULONG *)source;
    ULONG index, count = R200_SNAPSHOT_SCALAR_DWORDS + source->headerDwords;

    for (index = 0; index < count; ++index)
        d[index] = s[index];
}

BOOL CapturePendingDrawState(struct MiniGLR200Context *context,
                             ULONG command,
                             struct PendingDrawState *state)
{
    struct TextureObject *texture = NULL, *texture1 = NULL;
    struct Radeon3DSurface *target;
    BOOL fragmentStatePresent, extendedVertex;
    ULONG options = 0, vertexState = 0;

    /* Resolve the lit layout first so only the used prefix gets zeroed:
     * unlit batches must not pay memset/compare costs for the worst-case
     * lighting header. */
    state->lit = context->lightingEnabled &&
                  (context->radeonCaps & RADEON3D_CAP_HW_LIGHTING) != 0;
    state->hardwareSphereMap = HardwareSphereMapEnabled(context);
    state->normalVertex = state->lit || state->hardwareSphereMap;
    state->hardwareTransform = context->screenHardwareTransform ||
                               state->normalVertex;
    state->perspective = !state->hardwareTransform &&
                         context->screenProjective && !context->fogEnabled &&
                         (context->textureEnabled[0] ||
                          context->textureEnabled[1]);
    extendedVertex = state->hardwareTransform || context->fogEnabled ||
                     context->textureEnabled[1] || state->perspective;
    fragmentStatePresent = state->hardwareTransform || extendedVertex ||
              context->textureEnabled[0] ||
               context->blendEnabled || context->alphaTestEnabled;
    if (state->normalVertex) {
        ULONG light, enabledLights = 0;
        BOOL compactVertex = context->compactTclVertex &&
                             !context->textureEnabled[1] &&
                             !context->fogEnabled;
        if (state->lit)
            for (light = 0; light < R200_MAX_LIGHTS; ++light)
                if (context->lights[light].enabled) enabledLights++;
        state->enabledLights = enabledLights;
        state->headerDwords = (state->hardwareSphereMap
                                   ? R200_TEXGEN_HEADER_DWORDS
                                   : R200_TCL_HEADER_DWORDS) +
                              R200_NORMAL_MATRICES_DWORDS;
        if (state->lit)
            state->headerDwords += R200_LIGHT_STATE_DWORDS +
                                   R200_LIGHT_BLOCK_DWORDS * enabledLights;
        state->vertexDwords = compactVertex
                                  ? R200_LIT_VERTEX_COMPACT_DWORDS
                                  : R200_LIT_VERTEX_DWORDS;
    } else {
        state->enabledLights = 0;
        state->headerDwords = state->hardwareTransform
                                  ? RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS
                                  : extendedVertex
                                        ? RADEON3D_EXEC_DRAW_EXTENDED_HEADER_DWORDS
                                  : fragmentStatePresent
                                        ? RADEON3D_EXEC_DRAW_FRAGMENT_HEADER_DWORDS
                                        : RADEON3D_EXEC_DRAW_HEADER_DWORDS;
        state->vertexDwords = state->hardwareTransform
                                  ? (context->compactTclVertex &&
                                     !context->textureEnabled[1] &&
                                     !context->fogEnabled
                                         ? R200_TCL_VERTEX_COMPACT_DWORDS
                                         : RADEON3D_EXEC_HW_TCL_VERTEX_DWORDS)
                                  : extendedVertex
                                        ? RADEON3D_EXEC_EXTENDED_VERTEX_DWORDS
                                        : RADEON3D_EXEC_VERTEX_DWORDS;
    }
    /* Clear the scalar prefix plus the used header span (dwords 1 and 10
     * are patched by the flush after capture and must start zeroed). The
     * scalars resolved above live inside that prefix, so save and restore
     * them across the memset. */
    {
        ULONG savedHeaderDwords = state->headerDwords;
        ULONG savedVertexDwords = state->vertexDwords;
        ULONG savedEnabledLights = state->enabledLights;
        BOOL savedLit = state->lit;
        BOOL savedNormalVertex = state->normalVertex;
        BOOL savedHardwareSphereMap = state->hardwareSphereMap;
        BOOL savedHardwareTransform = state->hardwareTransform;

        ZeroMemory(state, (R200_SNAPSHOT_SCALAR_DWORDS +
                           state->headerDwords) * sizeof(ULONG));
        state->headerDwords = savedHeaderDwords;
        state->vertexDwords = savedVertexDwords;
        state->enabledLights = savedEnabledLights;
        state->lit = savedLit;
        state->normalVertex = savedNormalVertex;
        state->hardwareSphereMap = savedHardwareSphereMap;
        state->hardwareTransform = savedHardwareTransform;
    }
    state->depth = context->depthTest;
    state->fog = context->fogEnabled;
    state->drawableWidth = context->width;
    state->drawableHeight = context->height;
    state->quadVertexLimit = NativeQuadVertexLimit(context);
    state->drawEnabled = context->drawBuffer != GL_NONE;
    state->header[0] = command ? command : RADEON3D_EXEC_DRAW_TRIANGLES;
    if (!state->drawEnabled)
        return TRUE;
    state->drawRectangleValid = DrawRectangle(
        context, &state->header[6], &state->header[7],
        &state->header[8], &state->header[9]);
    if (!state->drawRectangleValid)
        return TRUE;

    target = TargetSurface(context);
    state->header[2] = (ULONG)target->Handle;
    state->header[3] = state->depth
                           ? (ULONG)context->depthSurface.Handle : 0;
    if (context->textureEnabled[0]) {
        if (context->boundTexture[0] >= MAX_TEXTURES) {
            if (context->perfEnabled) context->perf.dropTexture0++;
            return FALSE;
        }
        texture = &context->textures[context->boundTexture[0]];
        if (!EnsureTextureResident(context, texture)) {
            if (context->perfEnabled) context->perf.dropTexture0++;
            return FALSE;
        }
        if (IsMipFilter(texture->minFilter) &&
            texture->definedLevels !=
                ((1UL << texture->levelCount) - 1UL)) {
            if (context->perfEnabled) context->perf.dropTexture0Mips++;
            return FALSE;
        }
        if (!SyncTextureDirty(context, texture)) {
            if (context->perfEnabled) context->perf.dropTexture0++;
            return FALSE;
        }
        state->textured[0] = TRUE;
        state->textureName[0] = context->boundTexture[0];
        state->header[4] = (ULONG)texture->surface.Handle;
        /* A resident texture always has a live handle; if that invariant
         * ever breaks, drop the draw rather than emit a record the service
         * will resolve to NULL and reject the whole batch for. */
        if (!state->header[4]) {
            if (context->perfEnabled) context->perf.dropTexture0++;
            return FALSE;
        }
        state->header[12] = (texture->width - 1UL) |
                            ((texture->height - 1UL) << 16);
        state->header[13] = TextureState(context, texture, 0);
        options |= RADEON3D_DRAW_TEXTURED;
    }
    if (context->textureEnabled[1]) {
        if (context->boundTexture[1] >= MAX_TEXTURES) {
            if (context->perfEnabled) context->perf.dropTexture1++;
            return FALSE;
        }
        texture1 = &context->textures[context->boundTexture[1]];
        if (!EnsureTextureResident(context, texture1)) {
            if (context->perfEnabled) context->perf.dropTexture1++;
            return FALSE;
        }
        if (IsMipFilter(texture1->minFilter) &&
            texture1->definedLevels !=
                ((1UL << texture1->levelCount) - 1UL)) {
            if (context->perfEnabled) context->perf.dropTexture1Mips++;
            return FALSE;
        }
        if (!SyncTextureDirty(context, texture1)) {
            if (context->perfEnabled) context->perf.dropTexture1++;
            return FALSE;
        }
        state->textured[1] = TRUE;
        state->textureName[1] = context->boundTexture[1];
        state->header[15] = (ULONG)texture1->surface.Handle;
        if (!state->header[15]) {
            if (context->perfEnabled) context->perf.dropTexture1++;
            return FALSE;
        }
        state->header[17] = (texture1->width - 1UL) |
                            ((texture1->height - 1UL) << 16);
        state->header[18] = TextureState(context, texture1, 1);
        vertexState |= RADEON3D_VERTEX_TEXTURE1;
    }
    if (fragmentStatePresent) {
        options |= RADEON3D_DRAW_FRAGMENT_STATE;
        state->header[14] = FragmentState(context);
    }
    if (extendedVertex) {
        options |= RADEON3D_DRAW_EXTENDED_VERTEX;
        if (state->perspective)
            vertexState |= RADEON3D_VERTEX_CLIP_COORDINATES;
        if (state->fog)
            vertexState |= RADEON3D_VERTEX_FOG;
        state->header[19] = vertexState;
        state->header[20] = state->fog ? context->fogColor : 0;
    }
    if (state->hardwareTransform) {
        ULONG index;
        GLfloat viewportTop = (GLfloat)(context->height - context->viewportY -
                                        context->viewportHeight);
        ULONG transformFlags =
            ((ULONG)(context->pointSize * 16.0f) <<
             RADEON3D_TRANSFORM_POINT_SIZE_SHIFT) &
            RADEON3D_TRANSFORM_POINT_SIZE_MASK;

        options |= RADEON3D_DRAW_HW_TCL;
        /* Compact strides are only valid when unit 1 and fog are absent
         * from the vertex; the service rejects the combination otherwise.
         * vertexDwords above already encodes the same decision. */
        if (context->compactTclVertex &&
            !state->textured[1] && !state->fog)
            options |= RADEON3D_DRAW_COMPACT_VERTEX;
        if (!context->combinedMatrixValid) {
            MultiplyMatrix(context->combinedMatrix,
                           context->projection[context->projectionDepth],
                           context->modelview[context->modelviewDepth]);
            context->combinedMatrixValid = TRUE;
        }
        for (index = 0; index < 16UL; ++index)
            state->header[21UL + index] =
                FloatBits(context->combinedMatrix[index]);
        state->header[37] = FloatBits((GLfloat)context->viewportWidth * 0.5f);
        state->header[38] = FloatBits((GLfloat)context->viewportX +
                                     (GLfloat)context->viewportWidth * 0.5f);
        state->header[39] = FloatBits((GLfloat)context->viewportHeight * -0.5f);
        state->header[40] = FloatBits(viewportTop +
                                     (GLfloat)context->viewportHeight * 0.5f);
        state->header[41] = FloatBits((context->depthFar -
                                      context->depthNear) * 0.5f);
        state->header[42] = FloatBits((context->depthFar +
                                      context->depthNear) * 0.5f +
                                     ((context->zOffsetEnabled
                                           ? context->zOffset : 0.0f) +
                                      (context->polygonOffsetFillEnabled
                                           ? context->polygonOffsetUnits
                                           : 0.0f)) * (1.0f / 65535.0f));
        if (context->cullFaceEnabled) {
            if (context->cullFaceMode == GL_FRONT ||
                context->cullFaceMode == GL_FRONT_AND_BACK)
                transformFlags |= RADEON3D_TRANSFORM_CULL_FRONT;
            if (context->cullFaceMode == GL_BACK ||
                context->cullFaceMode == GL_FRONT_AND_BACK)
                transformFlags |= RADEON3D_TRANSFORM_CULL_BACK;
        }
        if (context->frontFace == GL_CCW)
            transformFlags |= RADEON3D_TRANSFORM_FRONT_CCW;
        if (context->shadeModel == GL_FLAT)
            transformFlags |= RADEON3D_TRANSFORM_FLAT_SHADE;
        if (context->polygonMode == GL_LINE &&
            (command == RADEON3D_EXEC_DRAW_TRIANGLES ||
             command == RADEON3D_EXEC_DRAW_TRI_FAN ||
             command == RADEON3D_EXEC_DRAW_TRI_STRIP ||
             command == RADEON3D_EXEC_DRAW_QUADS))
            transformFlags |= RADEON3D_TRANSFORM_POLYGON_LINE;
        else if (context->polygonMode == GL_POINT &&
                 (command == RADEON3D_EXEC_DRAW_TRIANGLES ||
                  command == RADEON3D_EXEC_DRAW_TRI_FAN ||
                  command == RADEON3D_EXEC_DRAW_TRI_STRIP ||
                  command == RADEON3D_EXEC_DRAW_QUADS))
            transformFlags |= RADEON3D_TRANSFORM_POLYGON_POINT;
        state->header[43] = transformFlags;
        if (state->hardwareSphereMap)
            WriteSphereMapHeader(context, state, &options);
        if (state->normalVertex)
            WriteNormalMatrices(context, state, &options);
        if (state->lit)
            WriteLightingHeader(context, state, &options);
    }
    if (state->depth) {
        options |= RADEON3D_DRAW_DEPTH_LESS |
                   RADEON3D_DRAW_DEPTH_FUNC(
                       DepthFunctionOption(context->depthFunc));
        if (context->depthMask)
            options |= RADEON3D_DRAW_DEPTH_WRITE;
    }
    state->header[5] = options;
    state->generatedVertexDwords = state->hardwareTransform
        ? GeneratedHardwareVertexDwords(state->textured[0],
                                         state->textured[1], state->fog,
                                         state->normalVertex)
        : GeneratedVertexDwords(state->depth, state->textured[0],
                                state->textured[1], state->fog,
                                state->perspective);
    return TRUE;
}

void Lib_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        (ULONG)x + (ULONG)width > context->width ||
        (ULONG)y + (ULONG)height > context->height) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (context->viewportX == (ULONG)x &&
        context->viewportY == (ULONG)y &&
        context->viewportWidth == (ULONG)width &&
        context->viewportHeight == (ULONG)height) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->viewportX = (ULONG)x;
    context->viewportY = (ULONG)y;
    context->viewportWidth = (ULONG)width;
    context->viewportHeight = (ULONG)height;
    /* The viewport is baked into hardware-TCL header dwords 37..40. */
    BumpSemanticStateSerial(context);
}

void Lib_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (width<0 || height<0) { SetError(context,GL_INVALID_VALUE); return; }
    if (context->scissorX==x && context->scissorY==y &&
        context->scissorWidth==width && context->scissorHeight==height) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->scissorX=x; context->scissorY=y;
    context->scissorWidth=width; context->scissorHeight=height;
    BumpSemanticStateSerial(context);
}

void Lib_glPointSize(GLfloat size)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (size<=0.0f) { SetError(context,GL_INVALID_VALUE); return; }
    if (size > 64.0f) size = 64.0f;
    if (context->pointSize == size) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->pointSize = size;
    /* Point size rides in hardware-TCL header dword 43. */
    BumpSemanticStateSerial(context);
}

void Lib_mglMinTriArea(GLfloat area)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context)) return;
    context->minTriArea = area<0.0f ? 0.0f : area;
}

void Lib_mglSetZOffset(GLfloat offset)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (context->zOffset == offset) return;
    context->zOffset = offset;
    if (context->zOffsetEnabled)
        BumpSemanticStateSerial(context);
}

/* Slope-scaled `factor` term not implemented; the constant-units bias is
 * applied where screen-space depth is finalized for pre-projected vertices. */
void Lib_glPolygonOffset(GLfloat factor, GLfloat units)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context)) return;
    (void)factor;
    if (context->polygonOffsetUnits == units) return;
    context->polygonOffsetUnits = units;
    if (context->polygonOffsetFillEnabled)
        BumpSemanticStateSerial(context);
}

void Lib_mglEnableSync(GLboolean enable)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (context->syncForced) return;
    context->syncEnabled = enable ? TRUE : FALSE;
}

void Lib_glHint(GLenum target, GLenum mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (target == MGL_PERF_COUNTERS_HINT) {
        if (!PerfClockRate()) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        if (mode == GL_NICEST) {
            ResetPerf(context);
            context->perfEnabled = TRUE;
        } else if (mode == GL_FASTEST) {
            context->perfEnabled = FALSE;
        } else if (mode == GL_DONT_CARE) {
            ResetPerf(context);
            context->perfEnabled = FALSE;
        } else {
            SetError(context, GL_INVALID_ENUM);
        }
        return;
    }
    if (target != MGL_W_ONE_HINT &&
        target != GL_FOG_HINT &&
        target != GL_PERSPECTIVE_CORRECTION_HINT) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    /* V19 forwarded fog/perspective hints to Warp3D quality controls and
     * kept W-one as a projection flag; the Radeon3D semantic interface has
     * no quality lever, so V19 clients' standard hint calls are accepted
     * as advisory no-ops (see plan.md). */
    (void)target;
    (void)mode;
}

BOOL StateChangeAllowed(struct MiniGLR200Context *context)
{
    if (!context)
        return FALSE;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    if (context->perfEnabled)
        context->perf.stateCalls++;
    return TRUE;
}

BOOL OutsideBeginAllowed(struct MiniGLR200Context *context)
{
    if (!context)
        return FALSE;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}

void Lib_glMatrixMode(GLenum mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    context->matrixMode = mode;
}

void Lib_glLoadIdentity(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (StateChangeAllowed(context)) {
        Identity(CurrentMatrix(context));
        InvalidateNormalMatrix(context);
    }
}

void Lib_glLoadMatrixf(const GLfloat *matrix)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (!matrix) { SetError(context, GL_INVALID_VALUE); return; }
    CopyMatrix(CurrentMatrix(context), matrix);
    InvalidateNormalMatrix(context);
}

void Lib_glLoadMatrixd(const GLdouble *matrix)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat converted[16];
    ULONG index;
    if (!StateChangeAllowed(context)) return;
    if (!matrix) { SetError(context, GL_INVALID_VALUE); return; }
    for (index = 0; index < 16; ++index) converted[index] = (GLfloat)matrix[index];
    CopyMatrix(CurrentMatrix(context), converted);
    InvalidateNormalMatrix(context);
}

void Lib_glMultMatrixf(const GLfloat *matrix)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (!matrix) { SetError(context, GL_INVALID_VALUE); return; }
    MultCurrentMatrix(context, matrix);
}

void Lib_glMultMatrixd(const GLdouble *matrix)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat converted[16];
    ULONG index;
    if (!StateChangeAllowed(context)) return;
    if (!matrix) { SetError(context, GL_INVALID_VALUE); return; }
    for (index = 0; index < 16; ++index) converted[index] = (GLfloat)matrix[index];
    MultCurrentMatrix(context, converted);
}

void Lib_glPushMatrix(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (context->matrixMode == GL_PROJECTION) {
        if (context->projectionDepth + 1 >= PROJECTION_STACK_DEPTH) {
            SetError(context, GL_STACK_OVERFLOW); return;
        }
        CopyMatrix(context->projection[context->projectionDepth + 1],
                   context->projection[context->projectionDepth]);
        ++context->projectionDepth;
    } else {
        if (context->modelviewDepth + 1 >= MODELVIEW_STACK_DEPTH) {
            SetError(context, GL_STACK_OVERFLOW); return;
        }
        CopyMatrix(context->modelview[context->modelviewDepth + 1],
                   context->modelview[context->modelviewDepth]);
        ++context->modelviewDepth;
        context->normalMatrixValid = FALSE;
    }
}

void Lib_glPopMatrix(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (context->matrixMode == GL_PROJECTION) {
        if (!context->projectionDepth) { SetError(context, GL_STACK_UNDERFLOW); return; }
        --context->projectionDepth;
        context->combinedMatrixValid = FALSE;
    } else {
        if (!context->modelviewDepth) { SetError(context, GL_STACK_UNDERFLOW); return; }
        --context->modelviewDepth;
        context->normalMatrixValid = FALSE;
        context->combinedMatrixValid = FALSE;
    }
    /* The restored stack entry changes the baked hardware-TCL MVP header. */
    BumpSemanticStateSerial(context);
}

void Lib_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat matrix[16];
    if (!StateChangeAllowed(context)) return;
    Identity(matrix); matrix[12] = x; matrix[13] = y; matrix[14] = z;
    MultCurrentMatrix(context, matrix);
}
void Lib_glTranslated(GLdouble x, GLdouble y, GLdouble z)
{ Lib_glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z); }

void Lib_glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat matrix[16];
    if (!StateChangeAllowed(context)) return;
    Identity(matrix); matrix[0] = x; matrix[5] = y; matrix[10] = z;
    MultCurrentMatrix(context, matrix);
}
void Lib_glScaled(GLdouble x, GLdouble y, GLdouble z)
{ Lib_glScalef((GLfloat)x, (GLfloat)y, (GLfloat)z); }

void Lib_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat matrix[16], length, sine, cosine, oneMinus;
    if (!StateChangeAllowed(context)) return;
    length = (GLfloat)sqrt((double)(x*x + y*y + z*z));
    if (length == 0.0f) return;
    x /= length; y /= length; z /= length;
    sine = (GLfloat)sin((double)angle * 0.017453292519943295);
    cosine = (GLfloat)cos((double)angle * 0.017453292519943295);
    oneMinus = 1.0f - cosine;
    Identity(matrix);
    matrix[0] = x*x*oneMinus + cosine;
    matrix[4] = x*y*oneMinus - z*sine;
    matrix[8] = x*z*oneMinus + y*sine;
    matrix[1] = y*x*oneMinus + z*sine;
    matrix[5] = y*y*oneMinus + cosine;
    matrix[9] = y*z*oneMinus - x*sine;
    matrix[2] = z*x*oneMinus - y*sine;
    matrix[6] = z*y*oneMinus + x*sine;
    matrix[10] = z*z*oneMinus + cosine;
    MultCurrentMatrix(context, matrix);
}
void Lib_glRotated(GLdouble a, GLdouble x, GLdouble y, GLdouble z)
{ Lib_glRotatef((GLfloat)a, (GLfloat)x, (GLfloat)y, (GLfloat)z); }

/*
 * xyz is a bitmask selecting one of the seven unit/diagonal axes GLROT_001,
 * GLROT_010, GLROT_100, GLROT_011, GLROT_101, GLROT_110, GLROT_111 (see
 * mgl/gl.h) rather than a pointer -- Lib_glRotatef already normalizes its
 * axis vector, so decoding the bits into 0/1 components and forwarding to it
 * reproduces the "simplified rotation routine" exactly.
 */
static void DecodeRotatefEXTAxis(GLint xyz, GLfloat *x, GLfloat *y, GLfloat *z)
{
    *x = (xyz & GLROT_100) ? 1.0f : 0.0f;
    *y = (xyz & GLROT_010) ? 1.0f : 0.0f;
    *z = (xyz & GLROT_001) ? 1.0f : 0.0f;
}

void Lib_glRotatefEXT(GLfloat angle, const GLint xyz)
{
    GLfloat x, y, z;
    DecodeRotatefEXTAxis(xyz, &x, &y, &z);
    Lib_glRotatef(angle, x, y, z);
}

void Lib_glRotatefEXTs(GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat matrix[16], x, y, z, length, oneMinus;
    if (!StateChangeAllowed(context)) return;
    DecodeRotatefEXTAxis(xyz, &x, &y, &z);
    length = (GLfloat)sqrt((double)(x*x + y*y + z*z));
    if (length == 0.0f) return;
    x /= length; y /= length; z /= length;
    oneMinus = 1.0f - cos_an;
    Identity(matrix);
    matrix[0] = x*x*oneMinus + cos_an;
    matrix[4] = x*y*oneMinus - z*sin_an;
    matrix[8] = x*z*oneMinus + y*sin_an;
    matrix[1] = y*x*oneMinus + z*sin_an;
    matrix[5] = y*y*oneMinus + cos_an;
    matrix[9] = y*z*oneMinus - x*sin_an;
    matrix[2] = z*x*oneMinus - y*sin_an;
    matrix[6] = z*y*oneMinus + x*sin_an;
    matrix[10] = z*z*oneMinus + cos_an;
    MultCurrentMatrix(context, matrix);
}

void Lib_glFrustum(GLdouble left, GLdouble right, GLdouble bottom,
                   GLdouble top, GLdouble nearValue, GLdouble farValue)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat matrix[16];
    if (!StateChangeAllowed(context)) return;
    if (left == right || bottom == top || nearValue <= 0.0 ||
        farValue <= 0.0 || nearValue == farValue) {
        SetError(context, GL_INVALID_VALUE); return;
    }
    Identity(matrix);
    matrix[0] = (GLfloat)(2.0 * nearValue / (right-left));
    matrix[5] = (GLfloat)(2.0 * nearValue / (top-bottom));
    matrix[8] = (GLfloat)((right+left)/(right-left));
    matrix[9] = (GLfloat)((top+bottom)/(top-bottom));
    matrix[10] = (GLfloat)(-(farValue+nearValue)/(farValue-nearValue));
    matrix[11] = -1.0f;
    matrix[14] = (GLfloat)(-2.0*farValue*nearValue/(farValue-nearValue));
    matrix[15] = 0.0f;
    MultCurrentMatrix(context, matrix);
}

void Lib_glOrtho(GLdouble left, GLdouble right, GLdouble bottom,
                 GLdouble top, GLdouble nearValue, GLdouble farValue)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat matrix[16];
    if (!StateChangeAllowed(context)) return;
    if (left == right || bottom == top || nearValue == farValue) {
        SetError(context, GL_INVALID_VALUE); return;
    }
    Identity(matrix);
    matrix[0] = (GLfloat)(2.0/(right-left));
    matrix[5] = (GLfloat)(2.0/(top-bottom));
    matrix[10] = (GLfloat)(-2.0/(farValue-nearValue));
    matrix[12] = (GLfloat)(-(right+left)/(right-left));
    matrix[13] = (GLfloat)(-(top+bottom)/(top-bottom));
    matrix[14] = (GLfloat)(-(farValue+nearValue)/(farValue-nearValue));
    MultCurrentMatrix(context, matrix);
}

void Lib_gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat nearValue,
                        GLfloat farValue)
{
    GLfloat ymax;
    if (aspect == 0.0f) { SetError(CurrentContext(), GL_INVALID_VALUE); return; }
    ymax = nearValue * (GLfloat)tan((double)fovy * 0.008726646259971648);
    Lib_glFrustum(-ymax * aspect, ymax * aspect, -ymax, ymax,
                  nearValue, farValue);
}

void Lib_gluLookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy,
                   GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat matrix[16], bx=ex-cx, by=ey-cy, bz=ez-cz;
    GLfloat rx, ry, rz, tx, ty, tz, length;
    if (!StateChangeAllowed(context)) return;
    length = (GLfloat)sqrt((double)(bx*bx+by*by+bz*bz));
    if (length == 0.0f) { SetError(context, GL_INVALID_VALUE); return; }
    bx/=length; by/=length; bz/=length;
    rx=uy*bz-uz*by; ry=uz*bx-ux*bz; rz=ux*by-uy*bx;
    length=(GLfloat)sqrt((double)(rx*rx+ry*ry+rz*rz));
    if (length == 0.0f) { SetError(context, GL_INVALID_VALUE); return; }
    rx/=length; ry/=length; rz/=length;
    tx=by*rz-bz*ry; ty=bz*rx-bx*rz; tz=bx*ry-by*rx;
    Identity(matrix);
    matrix[0]=rx; matrix[4]=ry; matrix[8]=rz;
    matrix[1]=tx; matrix[5]=ty; matrix[9]=tz;
    matrix[2]=bx; matrix[6]=by; matrix[10]=bz;
    MultCurrentMatrix(context, matrix);
    Lib_glTranslatef(-ex, -ey, -ez);
}

void Lib_glDepthRange(GLclampd nearValue, GLclampd farValue)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (nearValue < 0.0) nearValue = 0.0; if (nearValue > 1.0) nearValue = 1.0;
    if (farValue < 0.0) farValue = 0.0; if (farValue > 1.0) farValue = 1.0;
    if (context->depthNear == (GLfloat)nearValue &&
        context->depthFar == (GLfloat)farValue) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->depthNear = (GLfloat)nearValue;
    context->depthFar = (GLfloat)farValue;
    /* The depth range is baked into hardware-TCL header dwords 41..42. */
    BumpSemanticStateSerial(context);
}

void Lib_glClearDepth(GLclampd depth)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (depth < 0.0) depth = 0.0; if (depth > 1.0) depth = 1.0;
    context->clearDepth = (GLfloat)depth;
}

void Lib_glDepthMask(GLboolean flag)
{
    struct MiniGLR200Context *context = CurrentContext();
    BOOL enabled = flag ? TRUE : FALSE;
    if (!StateChangeAllowed(context)) return;
    if (context->depthMask == enabled) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->depthMask = enabled;
    BumpSemanticStateSerial(context);
}

void Lib_glDepthFunc(GLenum function)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (function != GL_NEVER && function != GL_LESS && function != GL_EQUAL &&
        function != GL_LEQUAL && function != GL_GREATER &&
        function != GL_NOTEQUAL && function != GL_GEQUAL &&
        function != GL_ALWAYS) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (context->depthFunc == function) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->depthFunc = function;
    BumpSemanticStateSerial(context);
}

void Lib_glFogf(GLenum pname, GLfloat param)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (pname==GL_FOG_MODE) {
        GLenum mode=(GLenum)param;
        if (mode!=GL_LINEAR && mode!=GL_EXP && mode!=GL_EXP2) {
            SetError(context,GL_INVALID_ENUM); return;
        }
        if (context->fogMode==mode) goto noop;
        context->fogMode=mode;
    } else if (pname==GL_FOG_DENSITY) {
        if (param<0.0f) { SetError(context,GL_INVALID_VALUE); return; }
        if (context->fogDensity==param) goto noop;
        context->fogDensity=param;
    } else if (pname==GL_FOG_START) {
        if (context->fogStart==param) goto noop;
        context->fogStart=param;
    } else if (pname==GL_FOG_END) {
        if (context->fogEnd==param) goto noop;
        context->fogEnd=param;
    }
    else SetError(context,GL_INVALID_ENUM);
    return;
noop:
    if (context->perfEnabled) context->perf.stateNoops++;
}

void Lib_glFogfv(GLenum pname, GLfloat *param)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!param || !StateChangeAllowed(context)) return;
    if (pname==GL_FOG_COLOR)
    {
        ULONG color=PackColor(param[0],param[1],param[2],param[3]) &
                    0x00ffffffUL;
        if (context->fogColor==color) {
            if (context->perfEnabled) context->perf.stateNoops++;
            return;
        }
        context->fogColor=color;
        BumpSemanticStateSerial(context);
    }
    else
        Lib_glFogf(pname,param[0]);
}

void Lib_glFogi(GLenum pname, GLint param)
{ Lib_glFogf(pname,(GLfloat)param); }

void Lib_glEnable(GLenum capability)
{
    struct MiniGLR200Context *context = CurrentContext();
    BOOL semanticChange = FALSE;
    if (!StateChangeAllowed(context)) return;
    if (capability == GL_DEPTH_TEST) {
        if (context->depthTest) goto noop;
        context->depthTest = TRUE;
        semanticChange = TRUE;
    } else if (capability == GL_TEXTURE_2D) {
        if (context->textureEnabled[context->activeTexture]) goto noop;
        context->textureEnabled[context->activeTexture] = TRUE;
        semanticChange = TRUE;
    } else if (capability == GL_FOG) {
        if (context->fogEnabled) goto noop;
        context->fogEnabled = TRUE;
        semanticChange = TRUE;
    } else if (capability == GL_BLEND) {
        if (context->blendEnabled) goto noop;
        context->blendEnabled = TRUE;
        semanticChange = TRUE;
    } else if (capability == GL_ALPHA_TEST) {
        if (context->alphaTestEnabled) goto noop;
        context->alphaTestEnabled = TRUE;
        semanticChange = TRUE;
    } else if (capability == GL_SCISSOR_TEST) {
        if (context->scissorEnabled) goto noop;
        context->scissorEnabled = TRUE;
        semanticChange = TRUE;
    } else if (capability == GL_CULL_FACE) {
        if (context->cullFaceEnabled) goto noop;
        context->cullFaceEnabled = TRUE;
        /* Cull enable selects hardware-TCL header cull bits. */
        semanticChange = TRUE;
    }
    else if (capability == GL_TEXTURE_GEN_S)
    {
        if (context->textureGenS[context->activeTexture]) goto noop;
        context->textureGenS[context->activeTexture] = TRUE;
        /* Texgen enable selects the texgen record header and normal
         * vertices, so it is capture-visible semantic state. */
        BumpSemanticStateSerial(context);
        InvalidateSphereMapCache(context);
    }
    else if (capability == GL_TEXTURE_GEN_T)
    {
        if (context->textureGenT[context->activeTexture]) goto noop;
        context->textureGenT[context->activeTexture] = TRUE;
        BumpSemanticStateSerial(context);
        InvalidateSphereMapCache(context);
    }
    else if (capability == GL_POLYGON_OFFSET_FILL) {
        if (context->polygonOffsetFillEnabled) goto noop;
        context->polygonOffsetFillEnabled = TRUE;
        semanticChange = TRUE;
    } else if (capability == MGL_Z_OFFSET) {
        if (context->zOffsetEnabled) goto noop;
        context->zOffsetEnabled = TRUE;
        semanticChange = TRUE;
    } else if (capability == GL_LIGHTING) {
        if (!(context->radeonCaps & RADEON3D_CAP_HW_LIGHTING)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        if (context->lightingEnabled) goto noop;
        context->lightingEnabled = TRUE;
        semanticChange = TRUE;
    } else if (capability >= GL_LIGHT0 &&
               capability < GL_LIGHT0 + (GLenum)R200_MAX_LIGHTS) {
        struct LightState *light =
            &context->lights[capability - GL_LIGHT0];
        if (!(context->radeonCaps & RADEON3D_CAP_HW_LIGHTING)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        if (light->enabled) goto noop;
        light->enabled = TRUE;
        semanticChange = TRUE;
    } else if (capability == GL_NORMALIZE || capability == GL_RESCALE_NORMAL) {
        BOOL *flag = capability == GL_NORMALIZE
                         ? &context->normalizeEnabled
                         : &context->rescaleEnabled;
        /* The service keeps hardware normal normalization enabled for lit
         * records; both client flags are tracked and reported but need no
         * header state of their own. */
        if (*flag) goto noop;
        *flag = TRUE;
    } else if (capability == GL_COLOR_MATERIAL) {
        if (context->colorMaterialEnabled) goto noop;
        context->colorMaterialEnabled = TRUE;
        semanticChange = TRUE;
    } else { SetError(context, GL_INVALID_ENUM); return; }
    if (semanticChange)
        BumpSemanticStateSerial(context);
    return;
noop:
    if (context->perfEnabled) context->perf.stateNoops++;
}

void Lib_glDisable(GLenum capability)
{
    struct MiniGLR200Context *context = CurrentContext();
    BOOL semanticChange = FALSE;
    if (!StateChangeAllowed(context)) return;
    if (capability == GL_DEPTH_TEST) {
        if (!context->depthTest) goto noop;
        context->depthTest = FALSE;
        semanticChange = TRUE;
    } else if (capability == GL_TEXTURE_2D) {
        if (!context->textureEnabled[context->activeTexture]) goto noop;
        context->textureEnabled[context->activeTexture] = FALSE;
        semanticChange = TRUE;
    } else if (capability == GL_FOG) {
        if (!context->fogEnabled) goto noop;
        context->fogEnabled = FALSE;
        semanticChange = TRUE;
    } else if (capability == GL_BLEND) {
        if (!context->blendEnabled) goto noop;
        context->blendEnabled = FALSE;
        semanticChange = TRUE;
    } else if (capability == GL_ALPHA_TEST) {
        if (!context->alphaTestEnabled) goto noop;
        context->alphaTestEnabled = FALSE;
        semanticChange = TRUE;
    } else if (capability == GL_SCISSOR_TEST) {
        if (!context->scissorEnabled) goto noop;
        context->scissorEnabled = FALSE;
        semanticChange = TRUE;
    } else if (capability == GL_CULL_FACE) {
        if (!context->cullFaceEnabled) goto noop;
        context->cullFaceEnabled = FALSE;
        /* Cull enable selects hardware-TCL header cull bits. */
        semanticChange = TRUE;
    }
    else if (capability == GL_TEXTURE_GEN_S)
    {
        if (!context->textureGenS[context->activeTexture]) goto noop;
        context->textureGenS[context->activeTexture] = FALSE;
        BumpSemanticStateSerial(context);
        InvalidateSphereMapCache(context);
    }
    else if (capability == GL_TEXTURE_GEN_T)
    {
        if (!context->textureGenT[context->activeTexture]) goto noop;
        context->textureGenT[context->activeTexture] = FALSE;
        BumpSemanticStateSerial(context);
        InvalidateSphereMapCache(context);
    }
    else if (capability == GL_POLYGON_OFFSET_FILL) {
        if (!context->polygonOffsetFillEnabled) goto noop;
        context->polygonOffsetFillEnabled = FALSE;
        semanticChange = TRUE;
    } else if (capability == MGL_Z_OFFSET) {
        if (!context->zOffsetEnabled) goto noop;
        context->zOffsetEnabled = FALSE;
        semanticChange = TRUE;
    } else if (capability == GL_LIGHTING) {
        if (!(context->radeonCaps & RADEON3D_CAP_HW_LIGHTING)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        if (!context->lightingEnabled) goto noop;
        context->lightingEnabled = FALSE;
        semanticChange = TRUE;
    } else if (capability >= GL_LIGHT0 &&
               capability < GL_LIGHT0 + (GLenum)R200_MAX_LIGHTS) {
        struct LightState *light =
            &context->lights[capability - GL_LIGHT0];
        if (!(context->radeonCaps & RADEON3D_CAP_HW_LIGHTING)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        if (!light->enabled) goto noop;
        light->enabled = FALSE;
        semanticChange = TRUE;
    } else if (capability == GL_NORMALIZE || capability == GL_RESCALE_NORMAL) {
        BOOL *flag = capability == GL_NORMALIZE
                         ? &context->normalizeEnabled
                         : &context->rescaleEnabled;
        if (!*flag) goto noop;
        *flag = FALSE;
    } else if (capability == GL_COLOR_MATERIAL) {
        if (!context->colorMaterialEnabled) goto noop;
        context->colorMaterialEnabled = FALSE;
        semanticChange = TRUE;
    } else { SetError(context, GL_INVALID_ENUM); return; }
    if (semanticChange)
        BumpSemanticStateSerial(context);
    return;
noop:
    if (context->perfEnabled) context->perf.stateNoops++;
}

GLboolean Lib_glIsEnabled(GLenum capability)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return GL_FALSE;
    if (capability == GL_DEPTH_TEST) return context->depthTest;
    if (capability == GL_TEXTURE_2D)
        return context->textureEnabled[context->activeTexture];
    if (capability == GL_FOG) return context->fogEnabled;
    if (capability == GL_BLEND) return context->blendEnabled;
    if (capability == GL_ALPHA_TEST) return context->alphaTestEnabled;
    if (capability == GL_SCISSOR_TEST) return context->scissorEnabled;
    if (capability == GL_CULL_FACE) return context->cullFaceEnabled;
    if (capability == GL_TEXTURE_GEN_S)
        return context->textureGenS[context->activeTexture];
    if (capability == GL_TEXTURE_GEN_T)
        return context->textureGenT[context->activeTexture];
    if (capability == GL_POLYGON_OFFSET_FILL)
        return context->polygonOffsetFillEnabled;
    if (capability == MGL_Z_OFFSET)
        return context->zOffsetEnabled;
    if (capability == GL_LIGHTING) return context->lightingEnabled;
    if (capability >= GL_LIGHT0 &&
        capability < GL_LIGHT0 + (GLenum)R200_MAX_LIGHTS)
        return context->lights[capability - GL_LIGHT0].enabled;
    if (capability == GL_NORMALIZE) return context->normalizeEnabled;
    if (capability == GL_RESCALE_NORMAL) return context->rescaleEnabled;
    if (capability == GL_COLOR_MATERIAL) return context->colorMaterialEnabled;
    SetError(context, GL_INVALID_ENUM);
    return GL_FALSE;
}

/*
 * No colour-write-mask primitive exists in the current Radeon3D draw record
 * (R200_UNBOUND_DISPATCH_PLAN.md Phase E); an actual mask can't be honoured.
 * The common defensive restore-to-default call (GL_TRUE x4) succeeds as a
 * no-op -- the hardware always writes all four channels, which is exactly
 * what that call means -- anything else is refused rather than silently
 * rendered incorrectly.
 */
void Lib_glColorMask(GLboolean red, GLboolean green, GLboolean blue,
                     GLboolean alpha)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (!red || !green || !blue || !alpha)
        SetError(context, GL_INVALID_OPERATION);
}

void Lib_glBlendFunc(GLenum source, GLenum destination)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (source != GL_ZERO && source != GL_ONE && source != GL_SRC_COLOR &&
        source != GL_ONE_MINUS_SRC_COLOR && source != GL_DST_COLOR &&
        source != GL_ONE_MINUS_DST_COLOR && source != GL_SRC_ALPHA &&
        source != GL_ONE_MINUS_SRC_ALPHA && source != GL_DST_ALPHA &&
        source != GL_ONE_MINUS_DST_ALPHA &&
        source != GL_SRC_ALPHA_SATURATE) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (destination == GL_SRC_ALPHA_SATURATE ||
        (destination != GL_ZERO && destination != GL_ONE &&
         destination != GL_SRC_COLOR &&
         destination != GL_ONE_MINUS_SRC_COLOR &&
         destination != GL_DST_COLOR &&
         destination != GL_ONE_MINUS_DST_COLOR &&
         destination != GL_SRC_ALPHA &&
         destination != GL_ONE_MINUS_SRC_ALPHA &&
         destination != GL_DST_ALPHA &&
         destination != GL_ONE_MINUS_DST_ALPHA)) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (context->blendSource == source &&
        context->blendDestination == destination) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->blendSource = source;
    context->blendDestination = destination;
    BumpSemanticStateSerial(context);
}

void Lib_glAlphaFunc(GLenum function, GLclampf reference)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (function != GL_NEVER && function != GL_LESS && function != GL_EQUAL &&
        function != GL_LEQUAL && function != GL_GREATER &&
        function != GL_NOTEQUAL && function != GL_GEQUAL &&
        function != GL_ALWAYS) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (reference < 0.0f) reference=0.0f;
    if (reference > 1.0f) reference=1.0f;
    if (context->alphaFunc==function && context->alphaRef==reference) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->alphaFunc=function;
    context->alphaRef=reference;
    BumpSemanticStateSerial(context);
}

void Lib_glShadeModel(GLenum mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (mode != GL_FLAT && mode != GL_SMOOTH) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (context->shadeModel == mode) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->shadeModel = mode;
    /* Shading rides in hardware-TCL header dword 43. */
    BumpSemanticStateSerial(context);
}

/*
 * `face` is validated but not tracked separately for GL_FRONT vs. GL_BACK
 * (see the note in AppendScreenPolygonTriangle()); GL_FRONT_AND_BACK is the
 * overwhelmingly common real-world call and is handled exactly.
 */
void Lib_glPolygonMode(GLenum face, GLenum mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (context->polygonMode == mode) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->polygonMode = mode;
    /* Polygon mode rides in hardware-TCL header dword 43. */
    BumpSemanticStateSerial(context);
}

void Lib_glCullFace(GLenum mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (context->cullFaceMode == mode) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->cullFaceMode = mode;
    /* Cull selection rides in hardware-TCL header dword 43. */
    BumpSemanticStateSerial(context);
}

void Lib_glFrontFace(GLenum mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (mode != GL_CW && mode != GL_CCW) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (context->frontFace == mode) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->frontFace = mode;
    /* Winding rides in hardware-TCL header dword 43. */
    BumpSemanticStateSerial(context);
}

/*
 * Buffer selection. In fullscreen double-buffered mode GL_FRONT targets the
 * displayed buffer and GL_BACK the buffer being drawn; a windowed context has
 * a single off-screen surface that mglSwitchDisplay() blits, so both select
 * it. GL_NONE discards rendering. Vertices already queued belong to the old
 * target. The immutable pending-state snapshot preserves that target until
 * the next draw append detects the changed effective header and splits.
 */
void Lib_glDrawBuffer(GLenum mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_NONE) {
        SetError(context, GL_INVALID_ENUM); return;
    }
    if (mode == context->drawBuffer) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->drawBuffer = mode;
    BumpSemanticStateSerial(context);
}

static BOOL WorstFrameCounter(struct MiniGLR200Context *context,
                              GLenum name, GLint *value)
{
    ULONG index;

    if (name >= MGL_PERF_WORST_FRAME_NUMBER_BASE &&
        name < MGL_PERF_WORST_FRAME_NUMBER_BASE + MGL_PERF_WORST_SLOTS) {
        index = name - MGL_PERF_WORST_FRAME_NUMBER_BASE;
        *value = (GLint)context->perf.worstFrames[index].frameNumber;
    } else if (name >= MGL_PERF_WORST_FRAME_TICKS_BASE &&
               name < MGL_PERF_WORST_FRAME_TICKS_BASE + MGL_PERF_WORST_SLOTS) {
        index = name - MGL_PERF_WORST_FRAME_TICKS_BASE;
        *value = (GLint)context->perf.worstFrames[index].ticks;
    } else if (name >= MGL_PERF_WORST_SERIALIZE_TICKS_BASE &&
               name < MGL_PERF_WORST_SERIALIZE_TICKS_BASE +
                          MGL_PERF_WORST_SLOTS) {
        index = name - MGL_PERF_WORST_SERIALIZE_TICKS_BASE;
        *value = (GLint)context->perf.worstFrames[index].serializeTicks;
    } else if (name >= MGL_PERF_WORST_EXECUTE_TICKS_BASE &&
               name < MGL_PERF_WORST_EXECUTE_TICKS_BASE +
                          MGL_PERF_WORST_SLOTS) {
        index = name - MGL_PERF_WORST_EXECUTE_TICKS_BASE;
        *value = (GLint)context->perf.worstFrames[index].executeTicks;
    } else if (name >= MGL_PERF_WORST_WAIT_TICKS_BASE &&
               name < MGL_PERF_WORST_WAIT_TICKS_BASE + MGL_PERF_WORST_SLOTS) {
        index = name - MGL_PERF_WORST_WAIT_TICKS_BASE;
        *value = (GLint)context->perf.worstFrames[index].waitTicks;
    } else if (name >= MGL_PERF_WORST_PRESENT_TICKS_BASE &&
               name < MGL_PERF_WORST_PRESENT_TICKS_BASE +
                          MGL_PERF_WORST_SLOTS) {
        index = name - MGL_PERF_WORST_PRESENT_TICKS_BASE;
        *value = (GLint)context->perf.worstFrames[index].presentTicks;
    } else if (name >= MGL_PERF_WORST_RESIDENCY_TICKS_BASE &&
               name < MGL_PERF_WORST_RESIDENCY_TICKS_BASE +
                          MGL_PERF_WORST_SLOTS) {
        index = name - MGL_PERF_WORST_RESIDENCY_TICKS_BASE;
        *value = (GLint)context->perf.worstFrames[index].residencyTicks;
    } else {
        return FALSE;
    }
    return TRUE;
}

void Lib_glGetIntegerv(GLenum name, GLint *value)
{
    struct MiniGLR200Context *context = CurrentContext();
    /* HELLO precedes context creation. This selector is deliberately scalar
     * so it is also safe through bounded generic integer-query transports. */
    if (name == MGL_R200_PRIVATE_TEXTURE_CAPS_V1_QUERY) {
        if (value)
            *value = (GLint)(MGL_R200_PRIVATE_TEXTURE_CAPS_V1_TAG |
                MGL_R200_PRIVATE_TEXTURE_CAP_SUBIMAGE_DIRECT_BGRA |
                MGL_R200_PRIVATE_TEXTURE_CAP_DIRECT_PARK);
        else if (context)
            SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (!context && name != MGL_R200_PRIVATE_SURFACE_V1_QUERY)
        return;
    if (!value) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    switch (name) {
    case GL_MAX_LIGHTS:
        *value = (GLint)R200_MAX_LIGHTS;
        break;
    case GL_MAX_TEXTURE_SIZE:
        /* Same limit Lib_glTexImage2D enforces. This case is mandatory:
         * the default path leaves *value untouched and only raises
         * GL_INVALID_ENUM, so clients trusting the query (Quake3 bakes
         * it into glConfig.maxTextureSize at init) read stack garbage
         * and can collapse every texture to 0x0. */
        *value = (GLint)R200_MAX_TEXTURE_DIMENSION;
        break;
    case GL_MAX_TEXTURE_UNITS_ARB:
        /* Two TMUs: the backend drives unit 0 and unit 1 and the
         * semantic service textures both (hardware-verified by the
         * texture_format_probe multitexture section). Quake3 gates its
         * multitexture path on this query answering > 1. */
        *value = 2;
        break;
    case MGL_R200_PRIVATE_TEXTURE_V1_QUERY:
        {
            MGLR200PrivateTextureV1 *bridge =
                (MGLR200PrivateTextureV1 *)value;
            ULONG textureName = bridge->textureName;
            struct TextureObject *texture;
            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_TEXTURE_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->textureName = textureName;
            if (!textureName || textureName >= MAX_TEXTURES) {
                SetError(context, GL_INVALID_VALUE);
                break;
            }
            texture = &context->textures[textureName];
            if (!texture->generated || !texture->image ||
                !EnsureTextureResident(context, texture) ||
                !SyncTextureDirty(context, texture) ||
                !texture->surface.Handle) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            bridge->surfaceToken = (ULONG)texture->surface.Handle;
            bridge->width = texture->width;
            bridge->height = texture->height;
            bridge->definedLevels = texture->definedLevels;
            bridge->levelCount = texture->levelCount;
            bridge->contentSerial = texture->contentSerial;
            bridge->deviceEpoch = context->deviceEpoch;
        }
        break;
    case MGL_R200_PRIVATE_TEXTURE_V2_QUERY:
        {
            MGLR200PrivateTextureV2 *bridge =
                (MGLR200PrivateTextureV2 *)value;
            ULONG textureName = bridge->textureName;
            struct TextureObject *texture;
            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_TEXTURE_V2_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->textureName = textureName;
            if (!textureName || textureName >= MAX_TEXTURES) {
                SetError(context, GL_INVALID_VALUE);
                break;
            }
            texture = &context->textures[textureName];
            if (!texture->generated ||
                !EnsureTextureResident(context, texture) ||
                !SyncTextureDirty(context, texture) ||
                !texture->surface.Handle) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            bridge->surfaceToken = (ULONG)texture->surface.Handle;
            bridge->width = texture->width;
            bridge->height = texture->height;
            bridge->definedLevels = texture->definedLevels;
            bridge->levelCount = texture->levelCount;
            bridge->contentSerial = texture->contentSerial;
            bridge->deviceEpoch = context->deviceEpoch;
            bridge->surfaceCpuAddress =
                (ULONG)texture->surface.CpuAddress;
            bridge->surfacePitch = texture->surface.Pitch;
            bridge->surfaceFormat = texture->surface.Format;
            bridge->surfaceBytes =
                texture->surface.Pitch * texture->surface.Height;
        }
        break;
    case MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V2_QUERY:
        {
            MGLR200PrivateTexDirectBeginV2 *bridge =
                (MGLR200PrivateTexDirectBeginV2 *)value;
            ULONG textureName = bridge->textureName;
            ULONG width = bridge->width;
            ULONG height = bridge->height;
            ULONG requestFormat = bridge->requestFormat;
            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V2_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->textureName = textureName;
            if (!BeginDirectTexture(context, textureName, width, height,
                                    requestFormat, bridge))
                SetError(context, GL_INVALID_OPERATION);
        }
        break;
    case MGL_R200_PRIVATE_TEXDIRECT_COMMIT_V2_QUERY:
        {
            MGLR200PrivateTexDirectCommitV2 *bridge =
                (MGLR200PrivateTexDirectCommitV2 *)value;
            ULONG textureName = bridge->textureName;
            ULONG srcAddress = bridge->srcAddress;
            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_TEXDIRECT_COMMIT_V2_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->textureName = textureName;
            if (!CommitDirectTexture(context, textureName, srcAddress,
                                     bridge))
                SetError(context, GL_INVALID_OPERATION);
        }
        break;
    case MGL_R200_PRIVATE_TEXDIRECT_REAP_V1_QUERY:
        {
            MGLR200PrivateTexDirectReapV1 *bridge =
                (MGLR200PrivateTexDirectReapV1 *)value;
            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_TEXDIRECT_REAP_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->released = ReapHostParkedSurfaces(context);
        }
        break;
    case MGL_R200_PRIVATE_TEXDIRECT_ABORT_V1_QUERY:
        {
            MGLR200PrivateTexDirectAbortV1 *bridge =
                (MGLR200PrivateTexDirectAbortV1 *)value;
            ULONG textureName = bridge->textureName;
            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_TEXDIRECT_ABORT_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->textureName = textureName;
            AbortDirectTexture(context, textureName);
        }
        break;
    case MGL_R200_PRIVATE_BRIDGE_V1_QUERY:
        {
            MGLR200PrivateBridgeV1 *bridge =
                (MGLR200PrivateBridgeV1 *)value;
            bridge->version = MGL_R200_PRIVATE_BRIDGE_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->deviceToken = (ULONG)context->device;
            /* The live target, not surfaces[0]: fullscreen presents rotate
             * the drawing buffer every flip, and a client that stamps a
             * stale handle into its records paints the buffer the display
             * has moved off. */
            bridge->colorSurfaceToken =
                (ULONG)TargetSurface(context)->Handle;
            bridge->depthSurfaceToken =
                (ULONG)context->depthSurface.Handle;
            bridge->width = context->width;
            bridge->height = context->height;
            bridge->radeonCaps = context->radeonCaps;
            bridge->batchDwords = context->batchDwords;
            bridge->flags = context->compactTclVertex
                ? MGL_R200_PRIVATE_BRIDGE_COMPACT_TCL : 0;
        }
        break;
    case MGL_R200_PRIVATE_BRIDGE_V2_QUERY:
        {
            MGLR200PrivateBridgeV2 *bridge =
                (MGLR200PrivateBridgeV2 *)value;
            bridge->version = MGL_R200_PRIVATE_BRIDGE_V2_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->deviceToken = (ULONG)context->device;
            bridge->colorSurfaceToken =
                (ULONG)TargetSurface(context)->Handle;
            bridge->depthSurfaceToken =
                (ULONG)context->depthSurface.Handle;
            bridge->width = context->width;
            bridge->height = context->height;
            bridge->radeonCaps = context->radeonCaps;
            bridge->batchDwords = context->batchDwords;
            bridge->flags = context->compactTclVertex
                ? MGL_R200_PRIVATE_BRIDGE_COMPACT_TCL : 0;
            bridge->deviceEpoch = context->deviceEpoch;
        }
        break;
    case MGL_R200_PRIVATE_SEGMENT_V1_QUERY:
        {
            MGLR200PrivateSegmentV1 *bridge =
                (MGLR200PrivateSegmentV1 *)value;
            ULONG bytes = bridge->bytes;
            struct Radeon3DSegment segment;
            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_SEGMENT_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
            if (!context->device || !bytes ||
                bytes > RADEON3D_MAX_SEGMENT_BYTES ||
                context->segmentLeaseCount >= RADEON3D_MAX_SEGMENTS) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            ZeroMemory(&segment, sizeof(segment));
            segment.Size = sizeof(segment);
            if (!Radeon3DAllocSegment(context->device, bytes, &segment) ||
                segment.Version != RADEON3D_SEGMENT_VERSION ||
                !segment.CpuAddress || !segment.Bytes) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            bridge->segmentId = segment.Id;
            bridge->cpuAddress = (ULONG)segment.CpuAddress;
            bridge->bytes = segment.Bytes;
            bridge->deviceEpoch = context->deviceEpoch;
            context->segmentLeases[context->segmentLeaseCount].id =
                segment.Id;
            context->segmentLeases[context->segmentLeaseCount].bytes =
                segment.Bytes;
            context->segmentLeaseCount++;
        }
        break;
    case MGL_R200_PRIVATE_SEGMENT_FREE_V1_QUERY:
        {
            MGLR200PrivateSegmentFreeV1 *bridge =
                (MGLR200PrivateSegmentFreeV1 *)value;
            ULONG segmentId = bridge->segmentId;
            ULONG index, kept;
            BOOL found = FALSE;
            for (index = 0; index < context->segmentLeaseCount; ++index) {
                if (context->segmentLeases[index].id == segmentId) {
                    found = TRUE;
                    break;
                }
            }
            if (!found) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            if (!context->device ||
                !Radeon3DFreeSegment(context->device, segmentId)) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            for (index = kept = 0; index < context->segmentLeaseCount;
                 ++index) {
                if (context->segmentLeases[index].id != segmentId)
                    context->segmentLeases[kept++] =
                        context->segmentLeases[index];
            }
            context->segmentLeaseCount = kept;
            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_SEGMENT_FREE_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
        }
        break;
    case MGL_R200_PRIVATE_PRESENT_V1_QUERY:
        {
            MGLR200PrivatePresentV1 *bridge =
                (MGLR200PrivatePresentV1 *)value;

            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_PRESENT_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
            if (!context) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            /* Not eligible is not an error: prepared stays 0. A failed
             * flush/submit inside the preparation already set its error. */
            (void)R200PreparePresentCp(context, bridge);
        }
        break;
    case MGL_R200_PRIVATE_SURFACE_V1_QUERY:
        {
            MGLR200PrivateSurfaceV1 *bridge =
                (MGLR200PrivateSurfaceV1 *)value;
            ULONG surfaceToken = bridge->surfaceToken;
            const struct Radeon3DSurface *surface;
            struct Radeon3DInfo info;
            ULONG index, bytesPerPixel, rowBytes, span;

            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_SURFACE_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->surfaceToken = surfaceToken;
            if (!context || !surfaceToken || !context->device ||
                !context->deviceEpoch) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            /* Compare opaque tokens only; pending direct allocations and
             * non-target screen buffers are not queryable live surfaces. */
            surface = TargetSurface(context);
            if ((ULONG)surface->Handle != surfaceToken) {
                surface = &context->depthSurface;
                if ((ULONG)surface->Handle != surfaceToken) {
                    surface = NULL;
                    for (index = 0; index < MAX_TEXTURES; ++index) {
                        const struct TextureObject *texture =
                            &context->textures[index];
                        if (texture->generated && texture->resident &&
                            texture->residentDeviceEpoch ==
                                context->deviceEpoch &&
                            (ULONG)texture->surface.Handle == surfaceToken) {
                            surface = &texture->surface;
                            break;
                        }
                    }
                }
            }
            /* Native recovery has its own epoch; only the live service can
             * tell us whether an imported generation survived a reset. */
            ZeroMemory(&info, sizeof(info));
            info.Size = sizeof(info);
            if (!surface ||
                !Radeon3DGetInfo(context->device, &info) ||
                !(info.Caps & RADEON3D_CAP_CP_READY) || !info.Generation ||
                surface->Generation != info.Generation ||
                surface->Size < RADEON3D_SURFACE_V1_SIZE ||
                surface->Version != RADEON3D_SURFACE_VERSION ||
                !surface->CpuAddress || (surface->GpuAddress & 15UL) ||
                !surface->Pitch || (surface->Pitch & 63UL) ||
                !surface->Width || surface->Width > 65536UL ||
                !surface->Height || surface->Height > 65536UL ||
                surface->Height > 0xffffffffUL / surface->Pitch) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            switch (surface->Format) {
            case RADEON3D_FORMAT_CLUT8: bytesPerPixel = 1UL; break;
            case RADEON3D_FORMAT_R5G6B5PC: bytesPerPixel = 2UL; break;
            case RADEON3D_FORMAT_B8G8R8A8: bytesPerPixel = 4UL; break;
            default: bytesPerPixel = 0; break;
            }
            rowBytes = surface->Width * bytesPerPixel;
            if (!bytesPerPixel || rowBytes > surface->Pitch) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            span = (surface->Height - 1UL) * surface->Pitch + rowBytes;
            if (span - 1UL > 0xffffffffUL - (ULONG)surface->CpuAddress ||
                span - 1UL > 0xffffffffUL - surface->GpuAddress) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            bridge->cpuAddress = (ULONG)surface->CpuAddress;
            bridge->gpuAddress = surface->GpuAddress;
            bridge->pitch = surface->Pitch;
            bridge->width = surface->Width;
            bridge->height = surface->Height;
            bridge->format = surface->Format |
                (context->colorTiled &&
                 surface == &context->surfaces[context->colorTileIndex]
                     ? MGL_R200_SURFACE_FORMAT_TILED : 0UL);
            bridge->deviceEpoch = context->deviceEpoch;
            bridge->generation = surface->Generation;
        }
        break;
    case MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY:
        {
            MGLR200PrivateIndirectDispatchV1 *bridge =
                (MGLR200PrivateIndirectDispatchV1 *)value;
            ULONG segmentId = bridge->segmentId;
            ULONG byteOffset = bridge->byteOffset;
            ULONG dwordCount = bridge->dwordCount;
            ULONG flags = bridge->flags;
            ULONG idx, fence = 0;
            struct Radeon3DIndirect indirect;
            BOOL result;

            ZeroMemory(bridge, sizeof(*bridge));
            bridge->version = MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_VERSION;
            bridge->structSize = sizeof(*bridge);
            bridge->segmentId = segmentId;
            if (!context->device ||
                !(context->radeonCaps & RADEON3D_CAP_INDIRECT_DISPATCH)) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            if (flags == MGL_R200_PRIVATE_INDIRECT_PROBE &&
                !segmentId && !byteOffset && !dwordCount) {
                bridge->flags = MGL_R200_PRIVATE_INDIRECT_SUPPORTED;
                break;
            }
            if (flags || !dwordCount) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            for (idx = 0; idx < context->segmentLeaseCount; ++idx) {
                if (context->segmentLeases[idx].id == segmentId)
                    break;
            }
            if (idx >= context->segmentLeaseCount) {
                SetError(context, GL_INVALID_OPERATION);
                break;
            }
            ZeroMemory(&indirect, sizeof(indirect));
            indirect.Size = sizeof(indirect);
            indirect.Version = RADEON3D_INDIRECT_VERSION;
            indirect.SegmentId = segmentId;
            indirect.ByteOffset = byteOffset;
            indirect.DwordCount = dwordCount;
            result = Radeon3DDispatchIndirect(context->device, &indirect,
                                              &fence);
            bridge->fenceOut = fence;
            if (!result)
                SetError(context, GL_INVALID_OPERATION);
        }
        break;
    case GL_SHADE_MODEL: *value = (GLint)context->shadeModel; break;
    case GL_CULL_FACE_MODE: *value = (GLint)context->cullFaceMode; break;
    case GL_FRONT_FACE: *value = (GLint)context->frontFace; break;
    case GL_DRAW_BUFFER: *value = (GLint)context->drawBuffer; break;
    case MGL_PERF_CLOCK_RATE: *value = (GLint)context->perf.clockRate; break;
    case MGL_PERF_FRAMES: *value = (GLint)context->perf.frames; break;
    case MGL_PERF_PRIMITIVES: *value = (GLint)context->perf.primitives; break;
    case MGL_PERF_INPUT_VERTICES: *value = (GLint)context->perf.inputVertices; break;
    case MGL_PERF_SUBMITTED_VERTICES: *value = (GLint)context->perf.submittedVertices; break;
    case MGL_PERF_DRAW_RECORDS: *value = (GLint)context->perf.drawRecords; break;
    case MGL_PERF_CLEAR_RECORDS: *value = (GLint)context->perf.clearRecords; break;
    case MGL_PERF_SUBMITS: *value = (GLint)context->perf.submits; break;
    case MGL_PERF_RECORD_DWORDS: *value = (GLint)context->perf.recordDwords; break;
    case MGL_PERF_GENERATED_DWORDS: *value = (GLint)context->perf.generatedDwords; break;
    case MGL_PERF_FRONTEND_TICKS: *value = (GLint)context->perf.frontendTicks; break;
    case MGL_PERF_EXECUTE_TICKS: *value = (GLint)context->perf.executeTicks; break;
    case MGL_PERF_WAIT_TICKS: *value = (GLint)context->perf.waitTicks; break;
    case MGL_PERF_PRESENT_TICKS: *value = (GLint)context->perf.presentTicks; break;
    case MGL_PERF_MATRIX_TICKS: *value = (GLint)context->perf.matrixTicks; break;
    case MGL_PERF_SERIALIZE_TICKS: *value = (GLint)context->perf.serializeTicks; break;
    case MGL_PERF_SERIALIZE_STALL_TICKS: *value = (GLint)context->perf.serializeStallTicks; break;
    case MGL_PERF_SERIALIZE_KERNEL_FLUSHES: *value = (GLint)context->perf.serializeKernelFlushes; break;
    case MGL_PERF_SERIALIZE_KERNEL_VERTICES: *value = (GLint)context->perf.serializeKernelVertices; break;
    case MGL_PERF_SPHEREMAP_CALLS: *value = (GLint)context->perf.sphereMapCalls; break;
    case MGL_PERF_SPHEREMAP_SAMPLES: *value = (GLint)context->perf.sphereMapSamples; break;
    case MGL_PERF_SPHEREMAP_TICKS: *value = (GLint)context->perf.sphereMapTicks; break;
    case MGL_PERF_SPHEREMAP_CACHE_HITS: *value = (GLint)context->perf.sphereMapCacheHits; break;
    case MGL_PERF_SPHEREMAP_CACHE_MISSES: *value = (GLint)context->perf.sphereMapCacheMisses; break;
    case MGL_PERF_NORMAL_CACHE_HITS: *value = (GLint)context->perf.normalCacheHits; break;
    case MGL_PERF_NORMAL_CACHE_MISSES: *value = (GLint)context->perf.normalCacheMisses; break;
    case MGL_PERF_TEXTURE_RESIDENCY_TICKS: *value = (GLint)context->perf.textureResidencyTicks; break;
    case MGL_PERF_RADEON_CAPS: *value = (GLint)context->radeonCaps; break;
    case MGL_PERF_NATIVE_ATTEMPTS: *value = (GLint)context->perf.nativeAttempts; break;
    case MGL_PERF_NATIVE_USED: *value = (GLint)context->perf.nativeUsed; break;
    case MGL_PERF_NATIVE_FALLBACK_CAPS: *value = (GLint)context->perf.nativeFallbackCaps; break;
    case MGL_PERF_NATIVE_FALLBACK_CULL: *value = (GLint)context->perf.nativeFallbackCull; break;
    case MGL_PERF_NATIVE_FALLBACK_FLAT: *value = (GLint)context->perf.nativeFallbackFlat; break;
    case MGL_PERF_NATIVE_FALLBACK_SIZE: *value = (GLint)context->perf.nativeFallbackSize; break;
    case MGL_PERF_NATIVE_FALLBACK_CLIP: *value = (GLint)context->perf.nativeFallbackClip; break;
    case MGL_PERF_QUAD_ATTEMPTS: *value = (GLint)context->perf.quadAttempts; break;
    case MGL_PERF_QUAD_USED: *value = (GLint)context->perf.quadUsed; break;
    case MGL_PERF_QUAD_FALLBACK_CAPS: *value = (GLint)context->perf.quadFallbackCaps; break;
    case MGL_PERF_QUAD_FALLBACK_STATE: *value = (GLint)context->perf.quadFallbackState; break;
    case MGL_PERF_QUAD_FALLBACK_CLIP: *value = (GLint)context->perf.quadFallbackClip; break;
    case MGL_PERF_FLUSH_CALLS: *value = (GLint)context->perf.flushCalls; break;
    case MGL_PERF_FLUSH_TEXTURED0: *value = (GLint)context->perf.flushTextured0; break;
    case MGL_PERF_FLUSH_UNTEXTURED0: *value = (GLint)context->perf.flushUntextured0; break;
    case MGL_PERF_DROP_DRAW_RECT: *value = (GLint)context->perf.dropDrawRectangle; break;
    case MGL_PERF_DROP_TEXTURE0: *value = (GLint)context->perf.dropTexture0; break;
    case MGL_PERF_DROP_TEXTURE0_MIPS: *value = (GLint)context->perf.dropTexture0Mips; break;
    case MGL_PERF_DROP_TEXTURE1: *value = (GLint)context->perf.dropTexture1; break;
    case MGL_PERF_DROP_TEXTURE1_MIPS: *value = (GLint)context->perf.dropTexture1Mips; break;
    case MGL_PERF_DROP_FLAT_DEGENERATE: *value = (GLint)context->perf.dropFlatDegenerate; break;
    case MGL_PERF_LAST_BOUND_TEXTURE0: *value = (GLint)context->perf.lastBoundTexture0; break;
    case MGL_PERF_EXECUTE_FAILURES: *value = (GLint)context->perf.executeFailures; break;
    case MGL_PERF_WAIT_FAILURES: *value = (GLint)context->perf.waitFailures; break;
    case MGL_PERF_RHW_CLAMPS: *value = (GLint)context->perf.rhwClamps; break;
    case MGL_PERF_TEXTURE_UPLOAD_TICKS: *value = (GLint)context->perf.textureUploadTicks; break;
    case MGL_PERF_TEXTURE_UPLOAD_BYTES: *value = (GLint)context->perf.textureUploadBytes; break;
    case MGL_PERF_TEXTURE_UPLOAD_COPIES: *value = (GLint)context->perf.textureUploadCopies; break;
    case MGL_PERF_TEXTURE_UPLOAD_UPDATES: *value = (GLint)context->perf.textureUploadUpdates; break;
    case MGL_PERF_TEXTURE_UPLOAD_COALESCED: *value = (GLint)context->perf.textureUploadCoalesced; break;
    case MGL_PERF_TEXTURE_UPLOAD_BARRIERS: *value = (GLint)context->perf.textureUploadBarriers; break;
    case MGL_PERF_TEXTURE_UPLOAD_DEFERRED: *value = (GLint)context->perf.textureUploadDeferred; break;
    case MGL_PERF_TEXTURE_EVICTIONS: *value = (GLint)context->perf.textureEvictions; break;
    case MGL_PERF_ASYNC_SUBMITS: *value = (GLint)context->perf.asyncSubmits; break;
    case MGL_PERF_FENCE_WAITS: *value = (GLint)context->perf.fenceWaits; break;
    case MGL_PERF_FENCE_POLL_HITS: *value = (GLint)context->perf.fencePollHits; break;
    case MGL_PERF_BUFFER_WAITS: *value = (GLint)context->perf.bufferWaits; break;
    case MGL_PERF_TEXTURE_WAITS: *value = (GLint)context->perf.textureWaits; break;
    case MGL_PERF_DRAIN_WAITS: *value = (GLint)context->perf.drainWaits; break;
    case MGL_PERF_STATE_CALLS: *value = (GLint)context->perf.stateCalls; break;
    case MGL_PERF_STATE_NOOPS: *value = (GLint)context->perf.stateNoops; break;
    case MGL_PERF_DEFERRED_SPLITS: *value = (GLint)context->perf.deferredSplits; break;
    case MGL_PERF_DRAW_MERGES: *value = (GLint)context->perf.drawMerges; break;
    case MGL_PERF_SNAPSHOT_BUILDS: *value = (GLint)context->perf.snapshotBuilds; break;
    case MGL_PERF_SNAPSHOT_FAST_HITS: *value = (GLint)context->perf.snapshotFastHits; break;
    case MGL_PERF_INDEX_SCAN_TICKS: *value = (GLint)context->perf.indexScanTicks; break;
    case MGL_PERF_VERTEX_FETCH_TICKS: *value = (GLint)context->perf.vertexFetchTicks; break;
    case MGL_PERF_APPEND_TICKS: *value = (GLint)context->perf.appendTicks; break;
    case MGL_PERF_VERTEX_FETCHES: *value = (GLint)context->perf.vertexFetches; break;
    case MGL_PERF_DRAW_TICKS: *value = (GLint)context->perf.drawTicks; break;
    case MGL_PERF_WORST_FRAME_COUNT:
        *value = (GLint)context->perf.worstFrameCount;
        break;
    default:
        if (WorstFrameCounter(context, name, value))
            break;
        SetError(context, GL_INVALID_ENUM);
        break;
    }
}

static GLboolean IsEnableCapability(GLenum capability)
{
    return capability == GL_DEPTH_TEST || capability == GL_TEXTURE_2D ||
           capability == GL_FOG || capability == GL_BLEND ||
           capability == GL_ALPHA_TEST || capability == GL_SCISSOR_TEST ||
           capability == GL_CULL_FACE || capability == GL_TEXTURE_GEN_S ||
           capability == GL_TEXTURE_GEN_T ||
           capability == GL_POLYGON_OFFSET_FILL ||
           capability == MGL_Z_OFFSET;
}

void Lib_glGetBooleanv(GLenum name, GLboolean *value)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context)
        return;
    if (!value) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (IsEnableCapability(name)) {
        *value = Lib_glIsEnabled(name);
        return;
    }
    switch (name) {
    case GL_DEPTH_WRITEMASK: *value = context->depthMask ? GL_TRUE : GL_FALSE; break;
    default: SetError(context, GL_INVALID_ENUM); break;
    }
}

static void UnpackColor(ULONG packed, GLfloat *out)
{
    out[3] = (GLfloat)((packed >> 24) & 0xffU) / 255.0f;
    out[0] = (GLfloat)((packed >> 16) & 0xffU) / 255.0f;
    out[1] = (GLfloat)((packed >> 8) & 0xffU) / 255.0f;
    out[2] = (GLfloat)(packed & 0xffU) / 255.0f;
}

void Lib_glGetFloatv(GLenum name, GLfloat *value)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context)
        return;
    if (!value) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    switch (name) {
    case GL_CURRENT_COLOR: UnpackColor(context->currentColor, value); break;
    case GL_COLOR_CLEAR_VALUE: UnpackColor(context->clearColor, value); break;
    case GL_DEPTH_CLEAR_VALUE: value[0] = context->clearDepth; break;
    case GL_DEPTH_RANGE:
        value[0] = context->depthNear;
        value[1] = context->depthFar;
        break;
    case GL_ALPHA_TEST_REF: value[0] = context->alphaRef; break;
    case GL_FOG_COLOR: UnpackColor(context->fogColor, value); break;
    case GL_FOG_DENSITY: value[0] = context->fogDensity; break;
    case GL_FOG_START: value[0] = context->fogStart; break;
    case GL_FOG_END: value[0] = context->fogEnd; break;
    case GL_VIEWPORT:
        value[0] = (GLfloat)context->viewportX;
        value[1] = (GLfloat)context->viewportY;
        value[2] = (GLfloat)context->viewportWidth;
        value[3] = (GLfloat)context->viewportHeight;
        break;
    case GL_SCISSOR_BOX:
        value[0] = (GLfloat)context->scissorX;
        value[1] = (GLfloat)context->scissorY;
        value[2] = (GLfloat)context->scissorWidth;
        value[3] = (GLfloat)context->scissorHeight;
        break;
    case GL_MODELVIEW_MATRIX:
        CopyMatrix(value, context->modelview[context->modelviewDepth]);
        break;
    case GL_PROJECTION_MATRIX:
        CopyMatrix(value, context->projection[context->projectionDepth]);
        break;
    case GL_CURRENT_NORMAL:
        value[0] = context->currentNormalX;
        value[1] = context->currentNormalY;
        value[2] = context->currentNormalZ;
        break;
    case GL_LIGHT_MODEL_AMBIENT:
        value[0] = context->lightModelAmbient[0];
        value[1] = context->lightModelAmbient[1];
        value[2] = context->lightModelAmbient[2];
        value[3] = context->lightModelAmbient[3];
        break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        value[0] = context->lightModelLocalViewer ? 1.0f : 0.0f;
        break;
    case GL_LIGHT_MODEL_TWO_SIDE:
        value[0] = context->lightModelTwoSide ? 1.0f : 0.0f;
        break;
    case GL_COLOR_MATERIAL_FACE:
        value[0] = (GLfloat)context->colorMaterialFace;
        break;
    case GL_COLOR_MATERIAL_PARAM:
        value[0] = (GLfloat)context->colorMaterialMode;
        break;
    default: SetError(context, GL_INVALID_ENUM); break;
    }
}

/*
 * Debug-only matrix dumps. The R200 build is -ffreestanding/-nostartfiles
 * (no stdio), so this writes to the serial port via exec's RawDoFmt/
 * RawPutChar; RawDoFmt has no %f, so each float is split into a scaled
 * integer and 3-digit fraction and printed as "%ld.%03ld ".
 */
static void PrintFractional3Digits(ULONG frac)
{
    char digits[4];
    digits[0] = (char)('0' + (frac / 100) % 10);
    digits[1] = (char)('0' + (frac / 10) % 10);
    digits[2] = (char)('0' + frac % 10);
    digits[3] = 0;
    dbg_puts(digits);
}

static void PrintFloatDebug(GLfloat value)
{
    LONG scaled = (LONG)(value * 1000.0f + (value >= 0.0f ? 0.5f : -0.5f));
    LONG whole = scaled / 1000;
    LONG frac = scaled % 1000;
    if (frac < 0) frac = -frac;
    dbg_s32(whole);
    dbg_puts(".");
    PrintFractional3Digits((ULONG)frac);
    dbg_puts(" ");
}

static void PrintMatrixDebug(const char *label, const GLfloat *matrix)
{
    ULONG row, column;
    dbg_puts(label);
    dbg_newline();
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column)
            PrintFloatDebug(matrix[column * 4 + row]);
        dbg_newline();
    }
}

static const GLfloat *MatrixForPrintMode(struct MiniGLR200Context *context,
                                         int mode, ULONG *stackDepthOut)
{
    if (mode == (int)GL_PROJECTION) {
        if (stackDepthOut) *stackDepthOut = context->projectionDepth;
        return context->projection[context->projectionDepth];
    }
    if (stackDepthOut) *stackDepthOut = context->modelviewDepth;
    return context->modelview[context->modelviewDepth];
}

void Lib_mglPrintMatrix(int mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    PrintMatrixDebug(mode == (int)GL_PROJECTION ? "GL_PROJECTION_MATRIX"
                                                    : "GL_MODELVIEW_MATRIX",
                        MatrixForPrintMode(context, mode, NULL));
}

void Lib_mglPrintMatrixStack(int mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG depth, level;
    const GLfloat (*stack)[16];
    if (!context) return;
    if (mode == (int)GL_PROJECTION) {
        stack = context->projection;
        depth = context->projectionDepth;
    } else {
        stack = context->modelview;
        depth = context->modelviewDepth;
    }
    for (level = 0; level <= depth; ++level)
        PrintMatrixDebug(mode == (int)GL_PROJECTION
                                 ? "GL_PROJECTION_MATRIX level"
                                 : "GL_MODELVIEW_MATRIX level",
                            stack[level]);
}

/* ------------------------------------------------------------------ */
/* Fixed-function lighting (interface 11)                              */
/*                                                                     */
/* Semantics follow Mesa classic r200: directional lights upload the   */
/* normalized eye-space direction plus the halfway vector, local lights */
/* the raw eye-space position plus the negated normalized spot         */
/* direction; DCD/DCM stay zero; the range cutoff is +FLT_MAX; and the  */
/* constant-attenuation reciprocal degrades to FLT_MAX for zero.       */
/* Light positions/directions transform by the modelview at glLightfv  */
/* call time per the GL specification.                                 */
/* ------------------------------------------------------------------ */

#define R200_FLOAT_MAX_BITS 0x7f7fffffUL

void InitializeLightingState(struct MiniGLR200Context *context)
{
    struct LightState *defaults;
    ULONG light;

    for (light = 0; light < R200_MAX_LIGHTS; ++light) {
        defaults = &context->lights[light];
        ZeroMemory(defaults, sizeof(*defaults));
        defaults->ambient[0] = defaults->ambient[1] =
            defaults->ambient[2] = defaults->ambient[3] = 0.0f;
        if (!light) {
            defaults->diffuse[0] = defaults->diffuse[1] =
                defaults->diffuse[2] = defaults->diffuse[3] = 1.0f;
            defaults->specular[0] = defaults->specular[1] =
                defaults->specular[2] = defaults->specular[3] = 1.0f;
        }
        /* GL default: directional light shining down -Z, i.e. position
         * (0, 0, 1, 0). */
        defaults->position[2] = 1.0f;
        defaults->eyePosition[2] = 1.0f;
        defaults->spotCutoff = 180.0f;
        defaults->spotCosCutoff = -1.0f;
        defaults->constantAttenuation = 1.0f;
    }
    /* GL-spec material defaults: black emission, 0.2 ambient,
     * 0.8 diffuse, black specular. */
    context->frontEmissive[3] = 1.0f;
    context->frontAmbient[0] = context->frontAmbient[1] =
        context->frontAmbient[2] = 0.2f;
    context->frontAmbient[3] = 1.0f;
    context->frontDiffuse[0] = context->frontDiffuse[1] =
        context->frontDiffuse[2] = 0.8f;
    context->frontDiffuse[3] = 1.0f;
    context->frontSpecular[3] = 1.0f;
    context->frontShininess = 0.0f;
    context->lightModelAmbient[3] = 1.0f;
    context->colorMaterialFace = GL_FRONT_AND_BACK;
    context->colorMaterialMode = GL_AMBIENT_AND_DIFFUSE;
    context->modelviewInvScale = 1.0f;
    context->eyeZDir[2] = 1.0f;
}

static BOOL ValidLight(GLenum light)
{
    return light >= GL_LIGHT0 && light < GL_LIGHT0 + (GLenum)R200_MAX_LIGHTS;
}

static void TransformPointByCurrentModelview(struct MiniGLR200Context *context,
                                             const GLfloat *input,
                                             GLfloat *output)
{
    const GLfloat *m = context->modelview[context->modelviewDepth];

    output[0] = m[0]*input[0] + m[4]*input[1] + m[8]*input[2] + m[12]*input[3];
    output[1] = m[1]*input[0] + m[5]*input[1] + m[9]*input[2] + m[13]*input[3];
    output[2] = m[2]*input[0] + m[6]*input[1] + m[10]*input[2] + m[14]*input[3];
    output[3] = m[3]*input[0] + m[7]*input[1] + m[11]*input[2] + m[15]*input[3];
}

static void Normalize3(GLfloat *vector)
{
    GLfloat squared = RoundFloat32(vector[0]*vector[0]);
    GLfloat inverse;

    squared = RoundFloat32(
        squared + RoundFloat32(vector[1]*vector[1]));
    squared = RoundFloat32(
        squared + RoundFloat32(vector[2]*vector[2]));
    inverse = ReciprocalSqrt(squared);

    if (FloatFinite(inverse) && inverse > 0.0f) {
        vector[0] = RoundFloat32(vector[0] * inverse);
        vector[1] = RoundFloat32(vector[1] * inverse);
        vector[2] = RoundFloat32(vector[2] * inverse);
    } else {
        vector[0] = 0.0f;
        vector[1] = 0.0f;
        vector[2] = 1.0f;
    }
}

static void SetLightfv(struct MiniGLR200Context *context, GLenum light,
                       GLenum pname, const GLfloat *params)
{
    struct LightState *state;
    GLfloat transformed[4];

    if (!ValidLight(light)) { SetError(context, GL_INVALID_ENUM); return; }
    if (!params) { SetError(context, GL_INVALID_VALUE); return; }
    state = &context->lights[light - GL_LIGHT0];
    switch (pname) {
    case GL_AMBIENT:
        CopyVector4(state->ambient, params);
        break;
    case GL_DIFFUSE:
        CopyVector4(state->diffuse, params);
        break;
    case GL_SPECULAR:
        CopyVector4(state->specular, params);
        break;
    case GL_POSITION:
        /* GL transforms the position by the current modelview at call
         * time and keeps the eye-space result thereafter. */
        CopyVector4(state->position, params);
        TransformPointByCurrentModelview(context, params, transformed);
        CopyVector4(state->eyePosition, transformed);
        break;
    case GL_SPOT_DIRECTION:
        state->spotDirection[0] = params[0];
        state->spotDirection[1] = params[1];
        state->spotDirection[2] = params[2];
        if (!context->normalMatrixValid)
            BuildNormalMatrix(context);
        {
            const GLfloat *n = context->normalMatrix;
            transformed[0] = n[0]*params[0] + n[3]*params[1] + n[6]*params[2];
            transformed[1] = n[1]*params[0] + n[4]*params[1] + n[7]*params[2];
            transformed[2] = n[2]*params[0] + n[5]*params[1] + n[8]*params[2];
        }
        Normalize3(transformed);
        state->eyeSpotDirection[0] = transformed[0];
        state->eyeSpotDirection[1] = transformed[1];
        state->eyeSpotDirection[2] = transformed[2];
        break;
    case GL_SPOT_EXPONENT:
        if (params[0] < 0.0f || params[0] > 128.0f) {
            SetError(context, GL_INVALID_VALUE); return;
        }
        state->spotExponent = params[0];
        break;
    case GL_SPOT_CUTOFF:
        if ((params[0] < 0.0f || params[0] > 90.0f) &&
            params[0] != 180.0f) {
            SetError(context, GL_INVALID_VALUE); return;
        }
        state->spotCutoff = params[0];
        state->spotCosCutoff = params[0] == 180.0f
                                   ? -1.0f
                                   : (GLfloat)cos(params[0] *
                                          (3.14159265f / 180.0f));
        break;
    case GL_CONSTANT_ATTENUATION:
        if (params[0] < 0.0f) { SetError(context, GL_INVALID_VALUE); return; }
        state->constantAttenuation = params[0];
        break;
    case GL_LINEAR_ATTENUATION:
        if (params[0] < 0.0f) { SetError(context, GL_INVALID_VALUE); return; }
        state->linearAttenuation = params[0];
        break;
    case GL_QUADRATIC_ATTENUATION:
        if (params[0] < 0.0f) { SetError(context, GL_INVALID_VALUE); return; }
        state->quadraticAttenuation = params[0];
        break;
    default:
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    BumpSemanticStateSerial(context);
}

void Lib_glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    SetLightfv(context, light, pname, params);
}

void Lib_glLightiv(GLenum light, GLenum pname, const GLint *params)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat converted[4];
    GLint index;
    if (!StateChangeAllowed(context)) return;
    if (!params) { SetError(context, GL_INVALID_VALUE); return; }
    if (pname == GL_SPOT_CUTOFF || pname == GL_SPOT_EXPONENT ||
        pname == GL_CONSTANT_ATTENUATION ||
        pname == GL_LINEAR_ATTENUATION ||
        pname == GL_QUADRATIC_ATTENUATION)
        converted[0] = (GLfloat)params[0];
    else
        for (index = 0; index < 4; ++index)
            converted[index] = (GLfloat)params[index];
    SetLightfv(context, light, pname, converted);
}

void Lib_glLightf(GLenum light, GLenum pname, GLfloat param)
{
    Lib_glLightfv(light, pname, &param);
}

void Lib_glLighti(GLenum light, GLenum pname, GLint param)
{
    Lib_glLightiv(light, pname, &param);
}

void Lib_glLightModelfv(GLenum pname, const GLfloat *params)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (!params) { SetError(context, GL_INVALID_VALUE); return; }
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        CopyVector4(context->lightModelAmbient, params);
        break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        context->lightModelLocalViewer = params[0] != 0.0f;
        break;
    case GL_LIGHT_MODEL_TWO_SIDE:
        /* Accepted but not honoured: the interface-11 service renders
         * front material only. */
        context->lightModelTwoSide = params[0] != 0.0f;
        break;
    default:
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    BumpSemanticStateSerial(context);
}

void Lib_glLightModeliv(GLenum pname, const GLint *params)
{
    GLfloat converted[4];
    GLint index;
    if (!params) return;
    if (pname == GL_LIGHT_MODEL_LOCAL_VIEWER ||
        pname == GL_LIGHT_MODEL_TWO_SIDE)
        converted[0] = (GLfloat)params[0];
    else
        for (index = 0; index < 4; ++index)
            converted[index] = (GLfloat)params[index];
    Lib_glLightModelfv(pname, converted);
}

void Lib_glLightModelf(GLenum pname, GLfloat param)
{
    Lib_glLightModelfv(pname, &param);
}

void Lib_glLightModeli(GLenum pname, GLint param)
{
    Lib_glLightModeliv(pname, &param);
}

static BOOL ValidMaterialFace(GLenum face)
{
    return face == GL_FRONT || face == GL_BACK ||
           face == GL_FRONT_AND_BACK;
}

/* The hardware consumes one front-material block. Back-face calls are
 * validated then ignored: the interface-11 service is front-only. */
static GLfloat * MaterialTarget(struct MiniGLR200Context *context,
                                GLenum face, GLenum pname)
{
    (void)face;
    switch (pname) {
    case GL_EMISSION: return context->frontEmissive;
    case GL_AMBIENT: return context->frontAmbient;
    case GL_AMBIENT_AND_DIFFUSE: return NULL;
    case GL_DIFFUSE: return context->frontDiffuse;
    case GL_SPECULAR: return context->frontSpecular;
    default: return NULL;
    }
}

static void SetMaterialfv(struct MiniGLR200Context *context, GLenum face,
                          GLenum pname, const GLfloat *params)
{
    GLfloat *target;

    if (!ValidMaterialFace(face)) { SetError(context, GL_INVALID_ENUM); return; }
    if (!params) { SetError(context, GL_INVALID_VALUE); return; }
    if (face == GL_BACK) return;
    if (pname == GL_SHININESS) {
        if (params[0] < 0.0f || params[0] > 128.0f) {
            SetError(context, GL_INVALID_VALUE); return;
        }
        context->frontShininess = params[0];
    } else {
        target = MaterialTarget(context, face, pname);
        if (!target) {
            if (pname != GL_AMBIENT_AND_DIFFUSE) {
                SetError(context, GL_INVALID_ENUM);
                return;
            }
            CopyVector4(context->frontAmbient, params);
            CopyVector4(context->frontDiffuse, params);
        } else {
            CopyVector4(target, params);
        }
    }
    BumpSemanticStateSerial(context);
}

void Lib_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    SetMaterialfv(context, face, pname, params);
}

void Lib_glMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLfloat converted[4];
    GLint index;
    if (!StateChangeAllowed(context)) return;
    if (!params) { SetError(context, GL_INVALID_VALUE); return; }
    if (pname == GL_SHININESS)
        converted[0] = (GLfloat)params[0];
    else
        for (index = 0; index < 4; ++index)
            converted[index] = (GLfloat)params[index];
    SetMaterialfv(context, face, pname, converted);
}

void Lib_glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    Lib_glMaterialfv(face, pname, &param);
}

void Lib_glMateriali(GLenum face, GLenum pname, GLint param)
{
    Lib_glMaterialiv(face, pname, &param);
}

void Lib_glColorMaterial(GLenum face, GLenum mode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    if (!ValidMaterialFace(face) ||
        (mode != GL_EMISSION && mode != GL_AMBIENT && mode != GL_DIFFUSE &&
         mode != GL_SPECULAR && mode != GL_AMBIENT_AND_DIFFUSE)) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (face == GL_BACK) face = GL_FRONT;
    if (context->colorMaterialFace == face &&
        context->colorMaterialMode == mode) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->colorMaterialFace = face;
    context->colorMaterialMode = mode;
    if (context->colorMaterialEnabled)
        BumpSemanticStateSerial(context);
}

void Lib_glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    struct MiniGLR200Context *context = CurrentContext();
    const struct LightState *state;
    if (!context) return;
    if (!ValidLight(light) || !params) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    state = &context->lights[light - GL_LIGHT0];
    switch (pname) {
    case GL_AMBIENT: CopyVector4(params, state->ambient); break;
    case GL_DIFFUSE: CopyVector4(params, state->diffuse); break;
    case GL_SPECULAR: CopyVector4(params, state->specular); break;
    case GL_POSITION: CopyVector4(params, state->position); break;
    case GL_SPOT_DIRECTION:
        params[0] = state->spotDirection[0];
        params[1] = state->spotDirection[1];
        params[2] = state->spotDirection[2];
        break;
    case GL_SPOT_EXPONENT: params[0] = state->spotExponent; break;
    case GL_SPOT_CUTOFF: params[0] = state->spotCutoff; break;
    case GL_CONSTANT_ATTENUATION:
        params[0] = state->constantAttenuation; break;
    case GL_LINEAR_ATTENUATION:
        params[0] = state->linearAttenuation; break;
    case GL_QUADRATIC_ATTENUATION:
        params[0] = state->quadraticAttenuation; break;
    default:
        SetError(context, GL_INVALID_ENUM);
    }
}

void Lib_glGetLightiv(GLenum light, GLenum pname, GLint *params)
{
    GLfloat values[4];
    GLint index;
    if (!params) return;
    Lib_glGetLightfv(light, pname, values);
    if (CurrentContext() && !CurrentContext()->error) {
        if (pname == GL_SPOT_DIRECTION) {
            params[0] = (GLint)values[0];
            params[1] = (GLint)values[1];
            params[2] = (GLint)values[2];
        } else {
            for (index = 0; index < 4; ++index)
                params[index] = (GLint)values[index];
        }
    }
}

void Lib_glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    if (!ValidMaterialFace(face) || !params) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    switch (pname) {
    case GL_EMISSION: CopyVector4(params, context->frontEmissive); break;
    case GL_AMBIENT: CopyVector4(params, context->frontAmbient); break;
    case GL_DIFFUSE: CopyVector4(params, context->frontDiffuse); break;
    case GL_SPECULAR: CopyVector4(params, context->frontSpecular); break;
    case GL_SHININESS: params[0] = context->frontShininess; break;
    default:
        SetError(context, GL_INVALID_ENUM);
    }
}

void Lib_glGetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
    GLfloat values[4];
    GLint index;
    if (!params) return;
    Lib_glGetMaterialfv(face, pname, values);
    if (CurrentContext() && !CurrentContext()->error) {
        if (pname == GL_SHININESS) {
            params[0] = (GLint)values[0];
        } else {
            for (index = 0; index < 4; ++index)
                params[index] = (GLint)(values[index] > 1.0f ? 1.0f
                                                    : values[index]);
        }
    }
}

/* Colour-material substitution reads the current colour as GL floats. */
static void CurrentColorFloats(const struct MiniGLR200Context *context,
                               GLfloat *rgba)
{
    ULONG color = context->currentColor;

    rgba[0] = (GLfloat)((color >> 16) & 0xffUL) / 255.0f;
    rgba[1] = (GLfloat)((color >> 8) & 0xffUL) / 255.0f;
    rgba[2] = (GLfloat)(color & 0xffUL) / 255.0f;
    rgba[3] = (GLfloat)((color >> 24) & 0xffUL) / 255.0f;
}

static void MaterialChannel(const struct MiniGLR200Context *context,
                            GLenum channel, const GLfloat *material,
                            GLfloat *destination)
{
    GLfloat currentColor[4];

    if (context->colorMaterialEnabled &&
        (context->colorMaterialMode == channel ||
         context->colorMaterialMode == GL_AMBIENT_AND_DIFFUSE)) {
        CurrentColorFloats(context, currentColor);
        destination[0] = currentColor[0];
        destination[1] = currentColor[1];
        destination[2] = currentColor[2];
        destination[3] = currentColor[3];
        return;
    }
    destination[0] = material[0];
    destination[1] = material[1];
    destination[2] = material[2];
    destination[3] = material[3];
}

static void WriteMaterialBlock(const struct MiniGLR200Context *context,
                               ULONG *block)
{
    GLfloat channel[4];

    MaterialChannel(context, GL_EMISSION, context->frontEmissive, channel);
    block[0] = FloatBits(channel[0]);
    block[1] = FloatBits(channel[1]);
    block[2] = FloatBits(channel[2]);
    block[3] = FloatBits(channel[3]);
    MaterialChannel(context, GL_AMBIENT, context->frontAmbient, channel);
    block[4] = FloatBits(channel[0]);
    block[5] = FloatBits(channel[1]);
    block[6] = FloatBits(channel[2]);
    block[7] = FloatBits(channel[3]);
    MaterialChannel(context, GL_DIFFUSE, context->frontDiffuse, channel);
    block[8] = FloatBits(channel[0]);
    block[9] = FloatBits(channel[1]);
    block[10] = FloatBits(channel[2]);
    block[11] = FloatBits(channel[3]);
    MaterialChannel(context, GL_SPECULAR, context->frontSpecular, channel);
    block[12] = FloatBits(channel[0]);
    block[13] = FloatBits(channel[1]);
    block[14] = FloatBits(channel[2]);
    block[15] = FloatBits(channel[3]);
    block[16] = FloatBits(context->frontShininess);
}

static BOOL LightUsesRangeAttenuation(const struct MiniGLR200Context *context,
                                      const struct LightState *light)
{
    GLfloat constant = light->constantAttenuation;

    if (light->eyePosition[3] == 0.0f)
        return FALSE;
    if ((constant == 0.0f || constant == 1.0f) &&
        light->linearAttenuation == 0.0f &&
        light->quadraticAttenuation == 0.0f)
        return FALSE;
    return TRUE;
}

static void WriteLightBlock(const struct MiniGLR200Context *context,
                            const struct LightState *light,
                            ULONG *block)
{
    GLfloat vector[4], direction[3];

    CopyVector4(vector, light->ambient);
    block[R200_LIGHT_AMBIENT + 0] = FloatBits(vector[0]);
    block[R200_LIGHT_AMBIENT + 1] = FloatBits(vector[1]);
    block[R200_LIGHT_AMBIENT + 2] = FloatBits(vector[2]);
    block[R200_LIGHT_AMBIENT + 3] = FloatBits(vector[3]);
    CopyVector4(vector, light->diffuse);
    block[R200_LIGHT_DIFFUSE + 0] = FloatBits(vector[0]);
    block[R200_LIGHT_DIFFUSE + 1] = FloatBits(vector[1]);
    block[R200_LIGHT_DIFFUSE + 2] = FloatBits(vector[2]);
    block[R200_LIGHT_DIFFUSE + 3] = FloatBits(vector[3]);
    CopyVector4(vector, light->specular);
    block[R200_LIGHT_SPECULAR + 0] = FloatBits(vector[0]);
    block[R200_LIGHT_SPECULAR + 1] = FloatBits(vector[1]);
    block[R200_LIGHT_SPECULAR + 2] = FloatBits(vector[2]);
    block[R200_LIGHT_SPECULAR + 3] = FloatBits(vector[3]);

    if (light->eyePosition[3] != 0.0f) {
        /* Local light: raw eye-space position, W nonzero marks locality,
         * and the direction slot carries the negated normalized spot
         * direction. */
        block[R200_LIGHT_POSITION + 0] = FloatBits(light->eyePosition[0]);
        block[R200_LIGHT_POSITION + 1] = FloatBits(light->eyePosition[1]);
        block[R200_LIGHT_POSITION + 2] = FloatBits(light->eyePosition[2]);
        block[R200_LIGHT_POSITION + 3] = FloatBits(light->eyePosition[3]);
        direction[0] = -light->eyeSpotDirection[0];
        direction[1] = -light->eyeSpotDirection[1];
        direction[2] = -light->eyeSpotDirection[2];
    } else {
        /* Directional light: Mesa uploads the normalized eye-space
         * direction in the position slot and the halfway vector between
         * that direction and the eye axis in the direction slot. */
        GLfloat view[3];

        view[0] = light->eyePosition[0];
        view[1] = light->eyePosition[1];
        view[2] = light->eyePosition[2];
        Normalize3(view);
        if (!FloatFinite(view[0]) || (view[0] == 0.0f &&
            view[1] == 0.0f && view[2] == 0.0f)) {
            view[0] = 0.0f;
            view[1] = 0.0f;
            view[2] = 1.0f;
        }
        block[R200_LIGHT_POSITION + 0] = FloatBits(view[0]);
        block[R200_LIGHT_POSITION + 1] = FloatBits(view[1]);
        block[R200_LIGHT_POSITION + 2] = FloatBits(view[2]);
        block[R200_LIGHT_POSITION + 3] = FloatBits(0.0f);
        direction[0] = view[0] + context->eyeZDir[0];
        direction[1] = view[1] + context->eyeZDir[1];
        direction[2] = view[2] + context->eyeZDir[2];
        Normalize3(direction);
    }
    block[R200_LIGHT_DIRECTION + 0] = FloatBits(direction[0]);
    block[R200_LIGHT_DIRECTION + 1] = FloatBits(direction[1]);
    block[R200_LIGHT_DIRECTION + 2] = FloatBits(direction[2]);
    block[R200_LIGHT_DIRECTION + 3] = FloatBits(0.0f);

    block[R200_LIGHT_ATTEN_QUADRATIC] =
        FloatBits(light->quadraticAttenuation);
    block[R200_LIGHT_ATTEN_LINEAR] = FloatBits(light->linearAttenuation);
    block[R200_LIGHT_ATTEN_CONSTANT] =
        FloatBits(light->constantAttenuation);
    block[23] = FloatBits(0.0f); /* reserved */

    /* DCD/DCM stay zero exactly as Mesa classic leaves them. */
    block[R200_LIGHT_SPOT_DCD] = FloatBits(0.0f);
    block[R200_LIGHT_SPOT_DCM] = FloatBits(0.0f);
    block[R200_LIGHT_SPOT_EXPONENT] = FloatBits(light->spotExponent);
    block[R200_LIGHT_SPOT_CUTOFF] = FloatBits(light->spotCosCutoff);
    block[R200_LIGHT_SPECULAR_THRESH] = FloatBits(0.0f);
    block[R200_LIGHT_RANGE_CUTOFF_SQRD] = R200_FLOAT_MAX_BITS;
    block[R200_LIGHT_ATTEN_CONSTANT_INV] =
        light->constantAttenuation > 0.0f
            ? FloatBits(1.0f / light->constantAttenuation)
            : R200_FLOAT_MAX_BITS;
}

static void WriteSphereMapHeader(struct MiniGLR200Context *context,
                                 struct PendingDrawState *state,
                                 ULONG *options)
{
    ULONG mask = HardwareSphereMapMask(context);
    ULONG unit;

    for (unit = 0; unit < 2UL; ++unit) {
        ULONG *matrix;

        if (!(mask & (1UL << unit)))
            continue;
        state->header[44UL + unit] = RADEON3D_TEXGEN_MODE_SPHERE_MAP |
                                      RADEON3D_TEXGEN_GEN_S |
                                      RADEON3D_TEXGEN_GEN_T;
        matrix = state->header + 46UL + unit * 16UL;
        matrix[0] = FloatBits(1.0f);
        matrix[5] = FloatBits(1.0f);
        matrix[10] = FloatBits(1.0f);
        matrix[15] = FloatBits(1.0f);
    }
    *options |= RADEON3D_DRAW_TEXGEN;
}

static void WriteNormalMatrices(struct MiniGLR200Context *context,
                                struct PendingDrawState *state,
                                ULONG *options)
{
    ULONG offset = state->hardwareSphereMap ? R200_TEXGEN_HEADER_DWORDS :
                                              R200_TCL_HEADER_DWORDS;
    ULONG *matrices = state->header + offset;
    ULONG index;

    BuildLightingMatrices(context);
    for (index = 0; index < 16UL; ++index) {
        matrices[index] =
            FloatBits(context->modelview[context->modelviewDepth][index]);
        matrices[16UL + index] = FloatBits(context->inverseModelview[index]);
    }
    *options |= RADEON3D_DRAW_NORMALS;
}

static void WriteLightingHeader(struct MiniGLR200Context *context,
                                 struct PendingDrawState *state,
                                 ULONG *options)
{
    ULONG normalOffset = state->hardwareSphereMap
                             ? R200_TEXGEN_HEADER_DWORDS
                             : R200_TCL_HEADER_DWORDS;
    ULONG *lightState;
    ULONG *block;
    ULONG lightControl = 0;
    ULONG light, nextBlock;

    lightState = state->header + normalOffset + R200_NORMAL_MATRICES_DWORDS;
    lightState[0] = FloatBits(context->lightModelAmbient[0]);
    lightState[1] = FloatBits(context->lightModelAmbient[1]);
    lightState[2] = FloatBits(context->lightModelAmbient[2]);
    lightState[3] = FloatBits(context->lightModelAmbient[3]);
    /* Eye vector: Mesa uploads (_EyeZDir.x, _EyeZDir.y, -_EyeZDir.z,
     * rescale factor). The service keeps hardware normal normalization
     * enabled regardless. */
    lightState[4] = FloatBits(context->eyeZDir[0]);
    lightState[5] = FloatBits(context->eyeZDir[1]);
    lightState[6] = FloatBits(-context->eyeZDir[2]);
    lightState[7] = FloatBits(context->modelviewInvScale);

    for (light = 0; light < R200_MAX_LIGHTS; ++light) {
        const struct LightState *source = &context->lights[light];

        if (!source->enabled)
            continue;
        lightControl |= 1UL << light;
        if (source->spotCutoff != 180.0f)
            lightControl |= 1UL << (RADEON3D_LIGHT_SPOT_SHIFT + light);
        if (LightUsesRangeAttenuation(context, source))
            lightControl |= 1UL << (RADEON3D_LIGHT_ATTEN_SHIFT + light);
    }
    if (context->lightModelLocalViewer)
        lightControl |= RADEON3D_LIGHT_LOCAL_VIEWER;
    lightState[8] = lightControl;

    WriteMaterialBlock(context, lightState + 9);

    nextBlock = 0;
    for (light = 0; light < R200_MAX_LIGHTS; ++light) {
        if (!context->lights[light].enabled)
            continue;
        block = lightState + R200_LIGHT_STATE_DWORDS +
                R200_LIGHT_BLOCK_DWORDS * nextBlock;
        WriteLightBlock(context, &context->lights[light], block);
        nextBlock++;
    }

    *options |= RADEON3D_DRAW_LIGHTING;
}
