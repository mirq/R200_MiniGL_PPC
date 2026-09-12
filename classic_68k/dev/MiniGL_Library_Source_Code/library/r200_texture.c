#include "minigl_r200_internal.h"

BOOL IsMipFilter(ULONG filter)
{
    return filter == GL_NEAREST_MIPMAP_NEAREST ||
           filter == GL_LINEAR_MIPMAP_NEAREST ||
           filter == GL_NEAREST_MIPMAP_LINEAR ||
           filter == GL_LINEAR_MIPMAP_LINEAR;
}

BOOL SyncTextureDirty(struct MiniGLR200Context *context,
                             struct TextureObject *texture);

BOOL EvictTextureDefer(struct MiniGLR200Context *context,
                       struct TextureObject *texture, BOOL allowDefer);

BOOL EvictTexture(struct MiniGLR200Context *context,
                          struct TextureObject *texture)
{
    return EvictTextureDefer(context, texture, FALSE);
}

static void DetachResidency(struct MiniGLR200Context *context,
                            struct TextureObject *texture)
{
    texture->bitmap = NULL;
    texture->resident = FALSE;
    texture->residentGeneration = 0;
    texture->residentDeviceEpoch = 0;
    texture->surfaceGeneration = 0;
    ZeroMemory(&texture->surface, sizeof(texture->surface));
    /* A released surface invalidates any capture header that named it
     * (glTexImage2D redefinition, DeleteTextures, LRU eviction), so the
     * snapshot cache must not outlive the old identity. */
    BumpSemanticStateSerial(context);
}

BOOL EvictTextureDefer(struct MiniGLR200Context *context,
                       struct TextureObject *texture, BOOL allowDefer)
{
    ULONG fence = texture->lastUseFence;
    ULONG index;

    if (!texture->resident)
        return TRUE;
    if (TextureQueued(context, texture) && !Submit(context))
        return FALSE;
    if (!texture->resident)
        return TRUE;
    fence = texture->lastUseFence;
    if (fence && texture->lastUseDeviceEpoch == context->deviceEpoch) {
        struct SubmitSlot *holder = NULL;
        for (index = 0; index < SUBMIT_SLOT_COUNT; ++index) {
            struct SubmitSlot *slot = &context->submitSlots[index];
            if (slot->inFlight && slot->fence == fence &&
                slot->deviceEpoch == texture->lastUseDeviceEpoch) {
                holder = slot;
                break;
            }
        }
        if (!holder) {
            /* The last use already retired; nothing references the
             * surface and it can go now. */
        } else if (allowDefer) {
            /* GL deletion is allowed to outlive the name: park the
             * surface until its fence retires instead of stalling the
             * pipeline on a GPU idled mid-frame. ReapDeferredSurfaces()
             * releases it at the next present drain. */
            struct DeferredSurface *node =
                AllocMem(sizeof(*node), MEMF_PUBLIC);
            if (node) {
                node->surface = texture->surface;
                node->bitmap = texture->bitmap;
                node->fence = fence;
                node->deviceEpoch = texture->lastUseDeviceEpoch;
                node->next = context->deferredSurfaces;
                context->deferredSurfaces = node;
                texture->lastUseFence = 0;
                texture->lastUseDeviceEpoch = 0;
                DetachResidency(context, texture);
                return TRUE;
            }
            /* Node allocation failed: fall through and pay the wait. */
            if (!WaitTextureUse(context, texture))
                return FALSE;
        } else {
            /* Eviction under VRAM pressure must reclaim the space
             * synchronously or the retrying allocation would spin. */
            if (!WaitTextureUse(context, texture))
                return FALSE;
        }
    }
    if (!texture->resident)
        return TRUE;
    if (texture->surface.Handle)
        Radeon3DReleaseSurface(context->device, &texture->surface);
    if (texture->bitmap)
        p96FreeBitMap(texture->bitmap);
    texture->lastUseFence = 0;
    texture->lastUseDeviceEpoch = 0;
    DetachResidency(context, texture);
    return TRUE;
}

void ReapDeferredSurfaces(struct MiniGLR200Context *context)
{
    struct DeferredSurface **link = &context->deferredSurfaces;
    ULONG index;
    BOOL pending = FALSE;

    if (!*link)
        return;
    /* Called after a drain. Do not poll old public tokens: Radeon3D accepts
     * only its latest fence, and ReleaseSurface clears that token. If any
     * native submission is still live, retain current-epoch surfaces rather
     * than implicitly draining it and invalidating its token through release. */
    for (index = 0; index < SUBMIT_SLOT_COUNT; ++index)
        if (context->submitSlots[index].inFlight &&
            context->submitSlots[index].deviceEpoch == context->deviceEpoch)
            pending = TRUE;

    while (*link) {
        struct DeferredSurface *node = *link;
        BOOL sameDevice = context->device &&
                          node->deviceEpoch == context->deviceEpoch;

        if (sameDevice && pending) {
            link = &node->next;
            continue;
        }
        *link = node->next;
        /* Closing the old device already destroyed its handles. */
        if (sameDevice && node->surface.Handle)
            Radeon3DReleaseSurface(context->device, &node->surface);
        if (node->bitmap)
            p96FreeBitMap(node->bitmap);
        FreeMem(node, sizeof(*node));
    }
}

struct TextureObject *FindEvictionCandidate(
    struct MiniGLR200Context *context, struct TextureObject *exclude)
{
    struct TextureObject *best = NULL, *bestQueued = NULL;
    ULONG bestUse = ~0UL, bestQueuedUse = ~0UL, index;
    for (index = 1; index < MAX_TEXTURES; ++index) {
        struct TextureObject *candidate = &context->textures[index];
        if (candidate == exclude || candidate->direct ||
            !candidate->resident ||
            (context->textureEnabled[0] &&
             index == context->boundTexture[0]) ||
            (context->textureEnabled[1] &&
             index == context->boundTexture[1]) ||
            (context->pendingDrawValid && context->screenVertexCount &&
             ((context->pendingDraw.textured[0] &&
               index == context->pendingDraw.textureName[0]) ||
              (context->pendingDraw.textured[1] &&
               index == context->pendingDraw.textureName[1]))))
            continue;
        if (TextureQueued(context, candidate)) {
            if (!bestQueued || candidate->lastUseSerial < bestQueuedUse) {
                bestQueued = candidate;
                bestQueuedUse = candidate->lastUseSerial;
            }
        } else if (!best || candidate->lastUseSerial < bestUse) {
            best = candidate;
            bestUse = candidate->lastUseSerial;
        }
    }
    return best ? best : bestQueued;
}

BOOL EnsureTextureResident(struct MiniGLR200Context *context,
                                  struct TextureObject *texture)
{
    ULONG expectedPitch, storageHeight;
    ULONG residencyStart = context->perfEnabled ? PerfClock() : 0;
    UBYTE *output;
    /* A direct-fill texture has no shadow image to resurrect: it is
     * only ever valid while its committed surface is resident. */
    if (texture->direct)
        return texture->resident &&
               texture->residentDeviceEpoch == context->deviceEpoch;
    if (texture->resident)
        return texture->residentDeviceEpoch == context->deviceEpoch &&
               (texture->dirtyLevelMask ||
                texture->residentGeneration == texture->imageGeneration);
    if (!texture->image || !texture->width || !texture->height)
        return FALSE;
    expectedPitch = (texture->width * 4UL + 63UL) & ~63UL;
    storageHeight = (texture->imageBytes + expectedPitch - 1UL) / expectedPitch;
    texture->bitmap = AllocateArgbBitMap(texture->width, storageHeight);
    while (!texture->bitmap) {
        struct TextureObject *candidate =
            FindEvictionCandidate(context, texture);
        if (!candidate || !EvictTexture(context, candidate))
            return FALSE;
        if (context->perfEnabled)
            context->perf.textureEvictions++;
        texture->bitmap = AllocateArgbBitMap(texture->width, storageHeight);
    }
    if (!texture->bitmap)
        return FALSE;
    texture->surface.Size = sizeof(texture->surface);
    if (!Radeon3DImportBitMap(context->device, texture->bitmap,
                              &texture->surface) ||
        texture->surface.Format != RADEON3D_FORMAT_B8G8R8A8 ||
        texture->surface.Pitch * texture->surface.Height < texture->imageBytes) {
        if (texture->surface.Handle)
            Radeon3DReleaseSurface(context->device, &texture->surface);
        p96FreeBitMap(texture->bitmap);
        texture->bitmap = NULL;
        {
            struct TextureObject *candidate =
                FindEvictionCandidate(context, texture);
            if (candidate && EvictTexture(context, candidate)) {
                if (context->perfEnabled)
                    context->perf.textureEvictions++;
                return EnsureTextureResident(context, texture);
            }
        }
        return FALSE;
    }
    output = texture->surface.CpuAddress;
    CopyMem(texture->image, output, texture->imageBytes);
    (void)output[texture->imageBytes - 1UL];
    texture->resident = TRUE;
    texture->residentGeneration = texture->imageGeneration;
    texture->residentDeviceEpoch = context->deviceEpoch;
    texture->surfaceGeneration = texture->surface.Generation;
    texture->dirtyLevelMask = 0;
    /* Fresh residency means a possibly-new surface handle: capture
     * headers naming this texture must be rebuilt, not reused. */
    BumpSemanticStateSerial(context);
    if (context->perfEnabled)
        context->perf.textureResidencyTicks += PerfClock() - residencyStart;
    return TRUE;
}

ULONG TextureState(const struct MiniGLR200Context *context,
                          const struct TextureObject *texture, ULONG unit)
{
    ULONG state = 0, minFilter, levels = 1;
    if (context->textureEnv[unit] == GL_MODULATE)
        state |= RADEON3D_TEX_MODULATE;
    if (texture->wrapS == GL_REPEAT)
        state |= RADEON3D_TEX_REPEAT_S;
    if (texture->wrapT == GL_REPEAT)
        state |= RADEON3D_TEX_REPEAT_T;
    if (texture->magFilter == GL_LINEAR)
        state |= RADEON3D_TEX_MAG_LINEAR;
    switch (texture->minFilter) {
    case GL_LINEAR: minFilter=RADEON3D_TEX_MIN_LINEAR; break;
    case GL_NEAREST_MIPMAP_NEAREST: minFilter=RADEON3D_TEX_MIN_NEAREST_MIPMAP_NEAREST; break;
    case GL_LINEAR_MIPMAP_NEAREST: minFilter=RADEON3D_TEX_MIN_LINEAR_MIPMAP_NEAREST; break;
    case GL_NEAREST_MIPMAP_LINEAR: minFilter=RADEON3D_TEX_MIN_NEAREST_MIPMAP_LINEAR; break;
    case GL_LINEAR_MIPMAP_LINEAR: minFilter=RADEON3D_TEX_MIN_LINEAR_MIPMAP_LINEAR; break;
    default: minFilter=RADEON3D_TEX_MIN_NEAREST; break;
    }
    if (IsMipFilter(texture->minFilter))
        levels = texture->levelCount;
    state |= minFilter << RADEON3D_TEX_MIN_SHIFT;
    state |= (levels - 1UL) << RADEON3D_TEX_LEVELS_SHIFT;
    state |= (texture->contentSerial & 0xffffUL) <<
             RADEON3D_TEX_CONTENT_SHIFT;
    return state;
}

BOOL PowerOfTwo(ULONG value)
{ return value && !(value & (value-1UL)); }

void TextureLevelLayout(const struct TextureObject *texture, ULONG level,
                               ULONG *offset, ULONG *stride,
                               ULONG *width, ULONG *height)
{
    ULONG current, w=texture->width, h=texture->height, position=0;
    ULONG alignment=PowerOfTwo(texture->width) &&
                    PowerOfTwo(texture->height) ? 31UL : 63UL;
    for (current=0; current<level; ++current) {
        ULONG rowStride=(w*4UL+alignment)&~alignment;
        position += rowStride*h;
        if (w>1)
            w>>=1;
        if (h>1)
            h>>=1;
    }
    *offset=position;
    *stride=(w*4UL+alignment)&~alignment;
    *width=w; *height=h;
}

void MarkTextureDirty(struct MiniGLR200Context *context,
                             struct TextureObject *texture, ULONG level,
                             ULONG left, ULONG top, ULONG width, ULONG height)
{
    struct TextureDirtyRect *dirty;
    ULONG bit;
    if (!width || !height)
        return;
    bit = 1UL << level;
    dirty = &texture->dirtyRects[level];
    if (texture->dirtyLevelMask & bit) {
        if (left < dirty->left) dirty->left = (UWORD)left;
        if (top < dirty->top) dirty->top = (UWORD)top;
        if (left + width > dirty->right)
            dirty->right = (UWORD)(left + width);
        if (top + height > dirty->bottom)
            dirty->bottom = (UWORD)(top + height);
        if (context->perfEnabled)
            context->perf.textureUploadCoalesced++;
    } else {
        dirty->left = (UWORD)left;
        dirty->top = (UWORD)top;
        dirty->right = (UWORD)(left + width);
        dirty->bottom = (UWORD)(top + height);
        texture->dirtyLevelMask |= bit;
    }
    if (++texture->imageGeneration == 0)
        texture->imageGeneration = 1;
    if (context->perfEnabled) {
        context->perf.textureUploadUpdates++;
        if (texture->resident)
            context->perf.textureUploadDeferred++;
    }
}

BOOL SyncTextureDirty(struct MiniGLR200Context *context,
                             struct TextureObject *texture)
{
    ULONG level, mask, start = 0, copied = 0;
    UBYTE *output;
    if (texture->direct)
        return TRUE;
    if (!texture->resident || !texture->dirtyLevelMask)
        return TRUE;
    /* Records already carrying this surface handle must see the previous
     * generation. Submit that dependency, then wait only for the texture-use
     * fence before changing mapped pixels. Unrelated submitted work remains
     * asynchronous and presentation fences stay independent. */
    if (TextureQueued(context, texture)) {
        if (context->perfEnabled)
            context->perf.textureUploadBarriers++;
        if (!Submit(context))
            return FALSE;
        if (!texture->resident)
            return FALSE;
    }
    if (!WaitTextureUse(context, texture))
        return FALSE;
    output = (UBYTE *)texture->surface.CpuAddress;
    if (!output || texture->residentDeviceEpoch != context->deviceEpoch)
        return FALSE;
    if (context->perfEnabled)
        start = PerfClock();
    mask = texture->dirtyLevelMask;
    for (level = 0; level < texture->levelCount; ++level) {
        ULONG levelOffset, levelStride, levelWidth, levelHeight;
        ULONG row, rowBytes;
        struct TextureDirtyRect *dirty;
        if (!(mask & (1UL << level)))
            continue;
        dirty = &texture->dirtyRects[level];
        TextureLevelLayout(texture, level, &levelOffset, &levelStride,
                           &levelWidth, &levelHeight);
        rowBytes = ((ULONG)dirty->right - (ULONG)dirty->left) * 4UL;
        for (row = dirty->top; row < dirty->bottom; ++row) {
            ULONG offset = levelOffset + row * levelStride +
                           (ULONG)dirty->left * 4UL;
            CopyMem(texture->image + offset, output + offset, rowBytes);
            copied += rowBytes;
        }
    }
    texture->dirtyLevelMask = 0;
    texture->residentGeneration = texture->imageGeneration;
    if (++texture->contentSerial == 0)
        texture->contentSerial = 1;
    if (context->perfEnabled) {
        context->perf.textureUploadTicks += PerfClock() - start;
        context->perf.textureUploadBytes += copied;
        context->perf.textureUploadCopies++;
    }
    return TRUE;
}

ULONG TextureStorageBytes(ULONG width, ULONG height, ULONG levels)
{
    ULONG total=0, level;
    ULONG alignment=PowerOfTwo(width) && PowerOfTwo(height) ? 31UL : 63UL;
    for (level=0; level<levels; ++level) {
        ULONG stride=(width*4UL+alignment)&~alignment;
        total += stride*height;
        if (width>1)
            width>>=1;
        if (height>1)
            height>>=1;
    }
    return total;
}

ULONG FullMipLevelCount(ULONG width, ULONG height)
{
    ULONG levels=1;
    if (!PowerOfTwo(width) || !PowerOfTwo(height))
        return 1;
    while (width>1 || height>1) {
        if (width>1)
            width>>=1;
        if (height>1)
            height>>=1;
        ++levels;
    }
    return levels;
}

BOOL ReleaseTextureStorage(struct MiniGLR200Context *context,
                                  struct TextureObject *texture,
                                  BOOL allowDefer)
{
    if (!EvictTextureDefer(context, texture, allowDefer))
        return FALSE;
    if (texture->direct) {
        /* Release a pending direct allocation. A committed one was
         * already parked or released by EvictTextureDefer above. */
        if (texture->directSurface.Handle)
            Radeon3DReleaseSurface(context->device,
                                   &texture->directSurface);
        if (texture->bitmap)
            p96FreeBitMap(texture->bitmap);
        texture->bitmap = NULL;
        ZeroMemory(&texture->directSurface,
                   sizeof(texture->directSurface));
        texture->direct = FALSE;
    }
    if (texture->image) {

        FreeMem(texture->image,texture->imageBytes);
        context->texMemCurrent -= (GLint)texture->imageBytes;
    }
    if (texture->dirtyRects)
        FreeMem(texture->dirtyRects,
                texture->levelCount * sizeof(struct TextureDirtyRect));
    texture->image=NULL; texture->imageBytes=0; texture->width=0;
    texture->height=0; texture->definedLevels=0; texture->levelCount=0;
    texture->dirtyRects=NULL; texture->dirtyLevelMask=0;
    texture->imageGeneration=0; texture->residentGeneration=0;
    return TRUE;
}

/* Direct fill path for the PPC host: every upload gets a freshly
 * allocated surface, because the R200 texture cache never invalidates
 * an in-place rewrite (see Lib_glTexSubImage2D). Begin parks the
 * previous residency exactly like glTexImage2D, allocates the fresh
 * bitmap, imports it, and hands the aperture address to the PPC.
 * Commit installs the filled surface under the texture name. */
BOOL BeginDirectTexture(struct MiniGLR200Context *context,
                        ULONG textureName, ULONG width, ULONG height,
                        ULONG requestFormat,
                        struct MGLR200PrivateTexDirectBeginV2 *bridge)
{
    struct TextureObject *texture;
    ULONG want565 = requestFormat == MGL_R200_TEXDIRECT_FMT_RGB565;
    ULONG grantedFormat = want565 ? RADEON3D_FORMAT_R5G6B5PC
                                  : RADEON3D_FORMAT_B8G8R8A8;
    /* Aux surfaces measured 229 ms/tile from the PPC (3.6x slower than
     * the p96-imported region) and the driver's aux allocator returns
     * the same block to every texture - both verified on hardware.
     * Keep the aux path off until the driver allocator and mapping
     * are fixed. */
    ULONG auxSurface = FALSE;

    if (!context->device || !textureName || textureName >= MAX_TEXTURES ||
        !width || !height || width > 2048 || height > 2048 ||
        !PowerOfTwo(width) || !PowerOfTwo(height))
        return FALSE;
    texture = &context->textures[textureName];
    if (!texture->generated || !StateChangeAllowed(context))
        return FALSE;
    if (!FlushScreenVertices(context))
        return FALSE;
    if (!ReleaseTextureStorage(context, texture, TRUE))
        return FALSE;
    texture->direct = TRUE;
    texture->width = width;
    texture->height = height;
    texture->levelCount = 1;
    texture->definedLevels = 1;
    texture->dirtyLevelMask = 0;
    if (auxSurface && !want565) {
        /* Driver-owned aux VRAM: persistent, no per-frame allocation,
         * and the PPC fills it through the same copyback-aperture
         * mapping the vertex segments use. The GPU resolves aux
         * surfaces identically to imported ones. */
        texture->bitmap = NULL;
        ZeroMemory(&texture->directSurface,
                   sizeof(texture->directSurface));
        texture->directSurface.Size = sizeof(texture->directSurface);
        if (Radeon3DAllocSurface(context->device, width, height,
                                 RADEON3D_FORMAT_B8G8R8A8,
                                 &texture->directSurface) &&
            texture->directSurface.Format == grantedFormat &&
            texture->directSurface.CpuAddress) {
            bridge->cpuAddress = (ULONG)texture->directSurface.CpuAddress;
            bridge->pitch = texture->directSurface.Pitch;
            bridge->format = texture->directSurface.Format;
            bridge->bytes = texture->directSurface.Pitch * height;
            bridge->deviceEpoch = context->deviceEpoch;
            bridge->flags = 1UL;
            return TRUE;
        }
        if (texture->directSurface.Handle)
            Radeon3DReleaseSurface(context->device,
                                   &texture->directSurface);
        ZeroMemory(&texture->directSurface,
                   sizeof(texture->directSurface));
        texture->direct = FALSE;
        texture->width = 0;
        texture->height = 0;
        texture->levelCount = 0;
        texture->definedLevels = 0;
        auxSurface = FALSE;
    }
    texture->bitmap = want565 ? AllocateRgb565BitMap(width, height)
                              : AllocateArgbBitMap(width, height);
    while (!texture->bitmap) {
        struct TextureObject *candidate =
            FindEvictionCandidate(context, texture);
        if (!candidate || !EvictTexture(context, candidate))
            goto fail;
        texture->bitmap = want565 ? AllocateRgb565BitMap(width, height)
                                  : AllocateArgbBitMap(width, height);
    }
    texture->directSurface.Size = sizeof(texture->directSurface);
    if (!Radeon3DImportBitMap(context->device, texture->bitmap,
                              &texture->directSurface) ||
        texture->directSurface.Format != grantedFormat ||
        !texture->directSurface.CpuAddress) {
        if (texture->directSurface.Handle)
            Radeon3DReleaseSurface(context->device,
                                   &texture->directSurface);
        ZeroMemory(&texture->directSurface,
                   sizeof(texture->directSurface));
        goto fail;
    }
    /* p96's CLEAR wrote through its own mapping: push any dirty 68k
     * lines on the aperture alias before the PPC fills through it. */
    CacheClearE(texture->directSurface.CpuAddress,
                texture->directSurface.Pitch * height, CACRF_ClearD);
    bridge->cpuAddress = (ULONG)texture->directSurface.CpuAddress;
    bridge->pitch = texture->directSurface.Pitch;
    bridge->format = texture->directSurface.Format;
    bridge->bytes = texture->directSurface.Pitch * height;
    bridge->deviceEpoch = context->deviceEpoch;
    bridge->flags = 0UL;
    return TRUE;
fail:
    if (texture->bitmap) {
        p96FreeBitMap(texture->bitmap);
        texture->bitmap = NULL;
    }
    texture->direct = FALSE;
    texture->width = 0;
    texture->height = 0;
    texture->levelCount = 0;
    texture->definedLevels = 0;
    return FALSE;
}

BOOL CommitDirectTexture(struct MiniGLR200Context *context,
                         ULONG textureName, ULONG srcAddress,
                         struct MGLR200PrivateTexDirectCommitV2 *bridge)
{
    struct TextureObject *texture;
    ULONG copyBytes;

    if (!context->device || !textureName || textureName >= MAX_TEXTURES)
        return FALSE;
    texture = &context->textures[textureName];
    if (!texture->direct || !texture->directSurface.Handle ||
        !texture->bitmap)
        return FALSE;
    copyBytes = texture->directSurface.Pitch * texture->height;
    if (srcAddress) {
        /* Warp3D-model commit: the PPC converted the tile into cached
         * shared RAM; the 68k performs the one VRAM copy through its
         * own mapping, exactly like the residency path. */
        CacheClearE((APTR)srcAddress, copyBytes, CACRF_ClearD);
        CopyMem((APTR)srcAddress, texture->directSurface.CpuAddress,
                copyBytes);
        (void)((UBYTE *)texture->directSurface.CpuAddress)
            [copyBytes - 1UL];
    }
    /* srcAddress == 0: an aux surface the PPC already wrote directly
     * through the copyback aperture - the flush already made the
     * bytes GPU-visible. Nothing to copy. */
    texture->surface = texture->directSurface;
    ZeroMemory(&texture->directSurface, sizeof(texture->directSurface));
    texture->resident = TRUE;
    if (++texture->imageGeneration == 0)
        texture->imageGeneration = 1;
    texture->residentGeneration = texture->imageGeneration;
    texture->residentDeviceEpoch = context->deviceEpoch;
    texture->surfaceGeneration = texture->surface.Generation;
    texture->lastUseFence = 0;
    texture->lastUseDeviceEpoch = 0;
    texture->dirtyLevelMask = 0;
    /* contentSerial stays as-is: the fresh surface address already
     * guarantees the GPU cannot serve stale texels, and the shared
     * upload path's first commit also reports an unchanged serial. */
    /* Fresh surface handle: capture headers naming the texture must
     * be rebuilt, mirroring fresh residency in EnsureTextureResident. */
    BumpSemanticStateSerial(context);
    bridge->surfaceToken = (ULONG)texture->surface.Handle;
    bridge->width = texture->width;
    bridge->height = texture->height;
    bridge->definedLevels = texture->definedLevels;
    bridge->levelCount = texture->levelCount;
    bridge->contentSerial = texture->contentSerial;
    bridge->deviceEpoch = context->deviceEpoch;
    return TRUE;
}

void AbortDirectTexture(struct MiniGLR200Context *context,
                        ULONG textureName)
{
    struct TextureObject *texture;

    if (!context->device || !textureName || textureName >= MAX_TEXTURES)
        return;
    texture = &context->textures[textureName];
    if (!texture->direct)
        return;
    if (texture->directSurface.Handle)
        Radeon3DReleaseSurface(context->device, &texture->directSurface);
    if (texture->bitmap && !texture->resident)
        p96FreeBitMap(texture->bitmap);
    texture->bitmap = NULL;
    ZeroMemory(&texture->directSurface, sizeof(texture->directSurface));
    texture->direct = FALSE;
    texture->width = 0;
    texture->height = 0;
    texture->levelCount = 0;
    texture->definedLevels = 0;
}

BOOL SourceFormatBytes(GLenum format, GLenum type, ULONG *bytes)
{
    if (type==MGL_UNSIGNED_SHORT_5_6_5 ||
        type==MGL_UNSIGNED_SHORT_4_4_4_4) {
        *bytes=2;
        return TRUE;
    }
    if (type!=GL_UNSIGNED_BYTE)
        return FALSE;
    if (format==GL_ALPHA || format==GL_LUMINANCE || format==GL_COLOR_INDEX) *bytes=1;
    else if (format==GL_LUMINANCE_ALPHA) *bytes=2;
    else if (format==GL_RGB) *bytes=3;
    else if (format==GL_RGBA || format==MGL_UBYTE_BGRA ||
             format==MGL_UBYTE_ARGB) *bytes=4;
    else
        return FALSE;
    return TRUE;
}

void ConvertPixel(struct MiniGLR200Context *context,
                         const UBYTE *source, GLenum format, GLenum type,
                         UBYTE *destination)
{
    UBYTE r=255,g=255,b=255,a=255;
    /*
     * context->colorTable entries are themselves populated through this
     * same function (see Lib_glColorTable), so they are already stored in
     * this function's BGRA destination order -- a straight 4-byte copy,
     * no field shuffling needed.
     */
    if (format==GL_COLOR_INDEX) {
        CopyBytes(destination,context->colorTable[source[0]],4);
        return;
    }
    if (type==MGL_UNSIGNED_SHORT_5_6_5) {
        UWORD value; CopyBytes(&value,source,2);
        r=(UBYTE)(((value>>11)&31)*255/31);
        g=(UBYTE)(((value>>5)&63)*255/63);
        b=(UBYTE)((value&31)*255/31);
    } else if (type==MGL_UNSIGNED_SHORT_4_4_4_4) {
        UWORD value; CopyBytes(&value,source,2);
        r=(UBYTE)(((value>>12)&15)*17); g=(UBYTE)(((value>>8)&15)*17);
        b=(UBYTE)(((value>>4)&15)*17); a=(UBYTE)((value&15)*17);
    } else if (format==GL_ALPHA) a=source[0],r=g=b=255;
    else if (format==GL_LUMINANCE) r=g=b=source[0];
    else if (format==GL_LUMINANCE_ALPHA) r=g=b=source[0],a=source[1];
    else if (format==GL_RGB) r=source[0],g=source[1],b=source[2];
    else if (format==GL_RGBA) r=source[0],g=source[1],b=source[2],a=source[3];
    else if (format==MGL_UBYTE_BGRA) b=source[0],g=source[1],r=source[2],a=source[3];
    else a=source[0],r=source[1],g=source[2],b=source[3];
    destination[0]=b; destination[1]=g; destination[2]=r; destination[3]=a;
}

BOOL UploadTextureRegion(struct MiniGLR200Context *context,
                                struct TextureObject *texture, ULONG level,
                                ULONG xoffset, ULONG yoffset, ULONG width,
                                ULONG height, GLenum format, GLenum type,
                                const GLvoid *pixels)
{
    ULONG sourceBytes, levelOffset, levelStride, levelWidth, levelHeight;
    ULONG sourceWidth, sourceStride, row, column;
    const UBYTE *source;
    if (!pixels || !SourceFormatBytes(format,type,&sourceBytes))
        return FALSE;
    TextureLevelLayout(texture,level,&levelOffset,&levelStride,
                       &levelWidth,&levelHeight);
    if (xoffset>levelWidth || yoffset>levelHeight || width>levelWidth-xoffset ||
        height>levelHeight-yoffset)
        return FALSE;
    sourceWidth=context->unpackRowLength ? context->unpackRowLength : width;
    sourceStride=sourceWidth*sourceBytes;
    sourceStride=(sourceStride+context->unpackAlignment-1UL) &
                 ~(context->unpackAlignment-1UL);
    source=(const UBYTE *)pixels + context->unpackSkipRows*sourceStride +
           context->unpackSkipPixels*sourceBytes;
    for (row=0; row<height; ++row) {
        if (format==MGL_UBYTE_BGRA && type==GL_UNSIGNED_BYTE) {
            UBYTE *destination=texture->image+levelOffset+
                (yoffset+row)*levelStride+xoffset*4UL;
            const UBYTE *input=source+row*sourceStride;
            CopyMem(input,destination,width*4UL);
        } else {
            for (column=0; column<width; ++column)
                ConvertPixel(context,source+row*sourceStride+column*sourceBytes,
                             format,type,texture->image+levelOffset+
                             (yoffset+row)*levelStride+(xoffset+column)*4UL);
        }
    }
    texture->definedLevels |= 1UL<<level;
    /* Caller memory is consumed now, but the mapped Radeon surface is updated
     * lazily at the next draw that samples this texture. Consecutive updates
     * therefore collapse into one per-level rectangle without weakening GL
     * ordering; SyncTextureDirty() drains an older queued generation first. */
    MarkTextureDirty(context, texture, level, xoffset, yoffset, width, height);
    return TRUE;
}

void Lib_glGenTextures(GLsizei count, GLuint *textures)
{
    struct MiniGLR200Context *context=CurrentContext();
    ULONG name, output=0;
    if (!StateChangeAllowed(context))
        return;
    if (count<0 || (!textures && count)) {
        SetError(context,GL_INVALID_VALUE);
        return;
    }
    for (name=1; name<MAX_TEXTURES && output<(ULONG)count; ++name)
        if (!context->textures[name].generated) {
            context->textures[name].generated=TRUE;
            textures[output++]=name;
        }
    if (output<(ULONG)count)
        SetError(context,GL_OUT_OF_MEMORY);
}

void Lib_glBindTexture(GLenum target, GLuint name)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context))
        return;
    if (target!=GL_TEXTURE_2D) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    if (name>=MAX_TEXTURES) {
        SetError(context,GL_INVALID_VALUE);
        return;
    }
    if (context->boundTexture[context->activeTexture] == name) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->textures[name].generated=TRUE;
    context->boundTexture[context->activeTexture]=name;
    BumpSemanticStateSerial(context);
}

void Lib_glDeleteTextures(GLsizei count, const GLuint *names)
{
    struct MiniGLR200Context *context=CurrentContext(); ULONG index;
    BOOL bindingChanged=FALSE;
    if (!StateChangeAllowed(context))
        return;
    if (count<0 || (!names && count)) {
        SetError(context,GL_INVALID_VALUE);
        return;
    }
    if (!FlushScreenVertices(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    for (index=0; index<(ULONG)count; ++index) {
        GLuint name=names[index]; struct TextureObject *texture;
        if (!name || name>=MAX_TEXTURES)
            continue;
        texture=&context->textures[name];
        if (!ReleaseTextureStorage(context,texture,TRUE)) {
            SetError(context,GL_INVALID_OPERATION);
            continue;
        }
        texture->generated=FALSE;
        texture->minFilter=texture->magFilter=GL_NEAREST;
        texture->wrapS=texture->wrapT=GL_REPEAT;
        if (context->boundTexture[0]==name) {
            context->boundTexture[0]=0;
            bindingChanged=TRUE;
        }
        if (context->boundTexture[1]==name) {
            context->boundTexture[1]=0;
            bindingChanged=TRUE;
        }
    }
    if (bindingChanged)
        BumpSemanticStateSerial(context);
}

void Lib_glTexImage2D(GLenum target, GLint level, GLint internalFormat,
                      GLsizei width, GLsizei height, GLint border,
                      GLenum format, GLenum type, const GLvoid *pixels)
{
    struct MiniGLR200Context *context=CurrentContext();
    struct TextureObject *texture; ULONG levels,bytes,expectedWidth,expectedHeight;
    UBYTE *newImage;
    struct TextureDirtyRect *newDirtyRects;
    if (!StateChangeAllowed(context))
        return;
    if (target!=GL_TEXTURE_2D) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    if (level<0 || (ULONG)level>=MAX_TEXTURE_LEVELS || width<=0 || height<=0 || width>2048 ||
        height>2048 || border!=0) {
        SetError(context,GL_INVALID_VALUE);
        return;
    }
    if (internalFormat!=GL_RGB && internalFormat!=GL_RGB5_A1 &&
        internalFormat!=GL_RGBA &&
        internalFormat!=3 && internalFormat!=4 && internalFormat!=GL_ALPHA &&
        internalFormat!=GL_LUMINANCE && internalFormat!=GL_LUMINANCE_ALPHA &&
        internalFormat!=GL_COLOR_INDEX) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    if (!FlushScreenVertices(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    texture=&context->textures[context->boundTexture[context->activeTexture]];
    if (!level) {
        if (!ReleaseTextureStorage(context,texture,TRUE)) {
            SetError(context,GL_INVALID_OPERATION);
            return;
        }
        levels=FullMipLevelCount((ULONG)width,(ULONG)height);
        bytes=TextureStorageBytes((ULONG)width,(ULONG)height,levels);
        newImage=AllocMem(bytes,MEMF_PUBLIC|MEMF_CLEAR);
        newDirtyRects=AllocMem(levels*sizeof(struct TextureDirtyRect),
                               MEMF_PUBLIC|MEMF_CLEAR);
        if (!newImage || !newDirtyRects) {
            if (newImage) FreeMem(newImage,bytes);
            if (newDirtyRects)
                FreeMem(newDirtyRects,
                        levels*sizeof(struct TextureDirtyRect));
            SetError(context,GL_OUT_OF_MEMORY);
            return;
        }
        texture->image=newImage;
        texture->dirtyRects=newDirtyRects;
        texture->imageBytes=bytes; texture->width=(ULONG)width;
        texture->height=(ULONG)height; texture->levelCount=levels;
        context->texMemCurrent += (GLint)bytes;
        if (context->texMemCurrent > context->texMemPeak)
            context->texMemPeak = context->texMemCurrent;
    } else if (!texture->image || (ULONG)level>=texture->levelCount) {
        SetError(context,GL_INVALID_OPERATION);
        return;
    }
    TextureLevelLayout(texture,(ULONG)level,&bytes,&levels,
                       &expectedWidth,&expectedHeight);
    if ((ULONG)width!=expectedWidth || (ULONG)height!=expectedHeight ||
        !UploadTextureRegion(context,texture,(ULONG)level,0,0,(ULONG)width,
                             (ULONG)height,format,type,pixels))
        SetError(context,GL_INVALID_VALUE);
}

/*
 * A single context-global palette (target must be GL_COLOR_TABLE, the
 * classic ARB_imaging-style entry point -- not the EXT_paletted_texture
 * variant with a per-texture table bound via target=GL_TEXTURE_2D).
 * `internalformat` is accepted but not stored: every entry is expanded to
 * BGRA regardless, matching how every other texture source format is
 * always expanded to 4 bytes/texel in this backend. Consulted by
 * ConvertPixel() whenever a subsequent glTexImage2D/glTexSubImage2D call
 * uses format==GL_COLOR_INDEX.
 */
void Lib_glColorTable(GLenum target, GLenum internalformat, GLsizei width,
                      GLenum format, GLenum type, const GLvoid *data)
{
    struct MiniGLR200Context *context = CurrentContext();
    ULONG sourceBytes, index;
    const UBYTE *source;
    (void)internalformat;
    if (!StateChangeAllowed(context)) return;
    if (target != GL_COLOR_TABLE) {
        SetError(context, GL_INVALID_ENUM);
        return;
    }
    if (width <= 0 || width > 256) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (!data || !SourceFormatBytes(format,type,&sourceBytes)) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    source = (const UBYTE *)data;
    for (index = 0; index < (ULONG)width; ++index)
        ConvertPixel(context, source+index*sourceBytes, format, type,
                    context->colorTable[index]);
}

/* Only a first subimage pays for readback. BEGIN/COMMIT and residency queries
 * must not call this: ordinary PPC full uploads have no native pixel copy.
 * Keep the one defined level and its generations/sampler state unchanged.
 * The image uses TextureLevelLayout's pitch, NOT the leased surface pitch. */
static BOOL MaterializeDirectTexture(struct MiniGLR200Context *context,
                                     struct TextureObject *texture)
{
    struct Radeon3DSurface surface = texture->surface;
    ULONG width = texture->width, height = texture->height;
    ULONG epoch = context->deviceEpoch, rowBytes, span, bytes, stride, row;
    UBYTE *image;
    struct TextureDirtyRect *dirtyRects;

    if (!context->device || !texture->direct || !texture->generated ||
        !texture->resident || texture->residentDeviceEpoch != epoch ||
        texture->directSurface.Handle || texture->image || texture->imageBytes ||
        texture->dirtyRects || texture->dirtyLevelMask ||
        texture->levelCount != 1 || texture->definedLevels != 1 ||
        !width || !height || width > 2048UL || height > 2048UL ||
        !PowerOfTwo(width) || !PowerOfTwo(height) ||
        !surface.Handle || !surface.CpuAddress ||
        surface.Format != RADEON3D_FORMAT_B8G8R8A8 ||
        surface.Width < width || surface.Height < height ||
        surface.Pitch < width * 4UL) {
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    rowBytes = width * 4UL;
    if (height - 1UL > (0xffffffffUL - rowBytes) / surface.Pitch) {
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    span = (height - 1UL) * surface.Pitch + rowBytes;
    if ((ULONG)surface.CpuAddress > 0xffffffffUL - span) {
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    bytes = TextureStorageBytes(width, height, 1);
    stride = bytes / height;
    if (context->texMemCurrent < 0 ||
        (ULONG)context->texMemCurrent > 0x7fffffffUL - bytes) {
        SetError(context, GL_OUT_OF_MEMORY);
        return FALSE;
    }
    /* Allocate BOTH resources before any flush/wait or metadata mutation.
     * In particular, allocation failure must not clear direct or lose the only
     * copy of the texels. Match TexImage allocation flags and accounting. */
    image = AllocMem(bytes, MEMF_PUBLIC | MEMF_CLEAR);
    dirtyRects = AllocMem(sizeof(*dirtyRects), MEMF_PUBLIC | MEMF_CLEAR);
    if (!image || !dirtyRects) {
        if (image) FreeMem(image, bytes);
        if (dirtyRects) FreeMem(dirtyRects, sizeof(*dirtyRects));
        SetError(context, GL_OUT_OF_MEMORY);
        return FALSE;
    }
    if (!FlushScreenVertices(context) ||
        (TextureQueued(context, texture) && !Submit(context)) ||
        !WaitTextureUse(context, texture) ||
        context->deviceEpoch != epoch || !texture->resident ||
        texture->residentDeviceEpoch != epoch ||
        texture->surface.Handle != surface.Handle ||
        texture->surface.CpuAddress != surface.CpuAddress) {
        FreeMem(image, bytes);
        FreeMem(dirtyRects, sizeof(*dirtyRects));
        SetError(context, GL_INVALID_OPERATION);
        return FALSE;
    }
    /* BEGIN flushed p96's initialization before the PPC wrote the lease;
     * COMMIT requires the producer flush to finish. These 68k alias lines
     * are clean: discard stale cache contents before reading the live lease.
     * Never derive an address from GpuAddress or touch MMIO here. */
    CacheClearE(surface.CpuAddress, span, CACRF_ClearD);
    for (row = 0; row < height; ++row)
        CopyMem((const UBYTE *)surface.CpuAddress + row * surface.Pitch,
                image + row * stride, rowBytes);
    texture->image = image;
    texture->imageBytes = bytes;
    texture->dirtyRects = dirtyRects;
    texture->direct = FALSE;
    context->texMemCurrent += (GLint)bytes;
    if (context->texMemCurrent > context->texMemPeak)
        context->texMemPeak = context->texMemCurrent;
    return TRUE;
}

void Lib_glTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                         GLint yoffset, GLsizei width, GLsizei height,
                         GLenum format, GLenum type, const GLvoid *pixels)
{
    struct MiniGLR200Context *context=CurrentContext();
    struct TextureObject *texture;
    ULONG levelOffset, levelStride, levelWidth, levelHeight, sourceBytes;
    if (!StateChangeAllowed(context))
        return;
    if (target!=GL_TEXTURE_2D) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    texture=&context->textures[context->boundTexture[context->activeTexture]];
    if (!texture->image && !texture->direct) {
        SetError(context,GL_INVALID_OPERATION);
        return;
    }
    if (level<0 || (ULONG)level>=texture->levelCount || xoffset<0 || yoffset<0 ||
        width<0 || height<0 || !pixels || !SourceFormatBytes(format,type,&sourceBytes)) {
        SetError(context,GL_INVALID_VALUE);
        return;
    }
    TextureLevelLayout(texture,(ULONG)level,&levelOffset,&levelStride,
                       &levelWidth,&levelHeight);
    if ((ULONG)xoffset>levelWidth || (ULONG)yoffset>levelHeight ||
        (ULONG)width>levelWidth-(ULONG)xoffset ||
        (ULONG)height>levelHeight-(ULONG)yoffset) {
        SetError(context,GL_INVALID_VALUE);
        return;
    }
    if (!width || !height)
        return;
    if (texture->direct && !MaterializeDirectTexture(context,texture))
        return;
    if (!FlushScreenVertices(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!UploadTextureRegion(context,texture,(ULONG)level,(ULONG)xoffset,
                             (ULONG)yoffset,(ULONG)width,(ULONG)height,
                             format,type,pixels)) {
        SetError(context,GL_INVALID_VALUE);
        return;
    }
    /* An in-place texel rewrite is invisible to this GPU: once its texture
     * cache holds a surface's lines it keeps serving them for that
     * address, and neither texture-register rewrites nor HDP read-buffer
     * invalidates evict them (both verified). Re-import the residency
     * under a fresh address instead. The stale allocation is kept alive
     * until the replacement exists, because freeing first would hand the
     * allocator the very block whose cache tags are stale. The service's
     * texture-only delta path keeps the resulting handle change cheap. */
    /* Records appended to the build slot - including the ones the flush
     * above just produced - name the surface that is about to be swapped
     * out, but carry no fence until they are submitted. Without a fence
     * the stale surface takes the release path below instead of being
     * parked, and the service resolves the recorded handle to NULL and
     * rejects the whole batch. Submit first so the dependency is fenced. */
    if (TextureQueued(context, texture) && !Submit(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (texture->resident) {
        struct BitMap *staleBitmap = texture->bitmap;
        struct Radeon3DSurface staleSurface = texture->surface;
        ULONG staleFence = texture->lastUseFence;
        ULONG staleEpoch = texture->lastUseDeviceEpoch;

        if (context->perfEnabled)
            context->perf.textureUploadBarriers++;
        /* Nothing has to retire before the rewrite: the new texels go to
         * a freshly imported address, and submission order already
         * guarantees the GPU sees them only in records appended after
         * the import. The old surface, which in-flight records may still
         * read, is parked for ReapDeferredSurfaces() instead of being
         * released now - Radeon3DReleaseSurface waits for the device's
         * last fence, and releasing mid-frame would drain the whole
         * pipeline on every update. */
        texture->bitmap = NULL;
        texture->resident = FALSE;
        texture->residentGeneration = 0;
        texture->residentDeviceEpoch = 0;
        texture->surfaceGeneration = 0;
        texture->lastUseFence = 0;
        texture->lastUseDeviceEpoch = 0;
        ZeroMemory(&texture->surface, sizeof(texture->surface));
        if (staleFence && staleEpoch == context->deviceEpoch) {
            struct DeferredSurface *node =
                AllocMem(sizeof(*node), MEMF_PUBLIC);
            if (node) {
                node->surface = staleSurface;
                node->bitmap = staleBitmap;
                node->fence = staleFence;
                node->deviceEpoch = staleEpoch;
                node->next = context->deferredSurfaces;
                context->deferredSurfaces = node;
                staleSurface.Handle = NULL;
                staleBitmap = NULL;
            }
        }
        if (!EnsureTextureResident(context, texture)) {
            if (staleSurface.Handle)
                Radeon3DReleaseSurface(context->device, &staleSurface);
            if (staleBitmap)
                p96FreeBitMap(staleBitmap);
            SetError(context, GL_OUT_OF_MEMORY);
            return;
        }
        if (staleSurface.Handle)
            Radeon3DReleaseSurface(context->device, &staleSurface);
        if (staleBitmap)
            p96FreeBitMap(staleBitmap);
    }
}

void Lib_glTexParameteri(GLenum target, GLenum pname, GLint value)
{
    struct MiniGLR200Context *context=CurrentContext(); struct TextureObject *texture;
    if (!StateChangeAllowed(context))
        return;
    if (target!=GL_TEXTURE_2D) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    texture=&context->textures[context->boundTexture[context->activeTexture]];
    if (pname==GL_TEXTURE_MAG_FILTER) {
        if (value!=GL_NEAREST && value!=GL_LINEAR) {
            SetError(context,GL_INVALID_ENUM);
            return;
        }
        if (texture->magFilter == (ULONG)value) {
            if (context->perfEnabled) context->perf.stateNoops++;
            return;
        }
        texture->magFilter=value;
        BumpSemanticStateSerial(context);
    } else if (pname==GL_TEXTURE_MIN_FILTER) {
        if (value!=GL_NEAREST && value!=GL_LINEAR &&
            value!=GL_NEAREST_MIPMAP_NEAREST && value!=GL_LINEAR_MIPMAP_NEAREST &&
            value!=GL_NEAREST_MIPMAP_LINEAR && value!=GL_LINEAR_MIPMAP_LINEAR) {
            SetError(context,GL_INVALID_ENUM);
            return;
        }
        if (LibraryBase && LibraryBase->prohibitMipMapping &&
            (value==GL_NEAREST_MIPMAP_NEAREST || value==GL_NEAREST_MIPMAP_LINEAR))
            value=GL_NEAREST;
        else if (LibraryBase && LibraryBase->prohibitMipMapping &&
                 (value==GL_LINEAR_MIPMAP_NEAREST || value==GL_LINEAR_MIPMAP_LINEAR))
            value=GL_LINEAR;
        if (texture->minFilter == (ULONG)value) {
            if (context->perfEnabled) context->perf.stateNoops++;
            return;
        }
        texture->minFilter=value;
        BumpSemanticStateSerial(context);
    } else if (pname==GL_TEXTURE_WRAP_S || pname==GL_TEXTURE_WRAP_T) {
        if (value!=GL_REPEAT && value!=GL_CLAMP) {
            SetError(context,GL_INVALID_ENUM);
            return;
        }
        if ((pname==GL_TEXTURE_WRAP_S && texture->wrapS==(ULONG)value) ||
            (pname==GL_TEXTURE_WRAP_T && texture->wrapT==(ULONG)value)) {
            if (context->perfEnabled) context->perf.stateNoops++;
            return;
        }
        if (pname==GL_TEXTURE_WRAP_S)
            texture->wrapS=value;
        else
            texture->wrapT=value;
        BumpSemanticStateSerial(context);
    } else {
        SetError(context,GL_INVALID_ENUM);
    }
}
void Lib_glTexParameterf(GLenum target, GLenum pname, GLfloat value)
{ Lib_glTexParameteri(target,pname,(GLint)value); }

void Lib_glTexEnvi(GLenum target, GLenum pname, GLint value)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context))
        return;
    if (target!=GL_TEXTURE_ENV || pname!=GL_TEXTURE_ENV_MODE ||
        (value!=GL_MODULATE && value!=GL_REPLACE)) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    if (context->textureEnv[context->activeTexture] == (GLenum)value) {
        if (context->perfEnabled) context->perf.stateNoops++;
        return;
    }
    context->textureEnv[context->activeTexture]=value;
    BumpSemanticStateSerial(context);
}
void Lib_glTexEnvf(GLenum target, GLenum pname, GLfloat value)
{ Lib_glTexEnvi(target,pname,(GLint)value); }
void Lib_glTexEnviv(GLenum target, GLenum pname, GLint *value)
{ if (value) Lib_glTexEnvi(target,pname,*value); }
void Lib_glTexEnvfv(GLenum target, GLenum pname, GLfloat *value)
{ if (value) Lib_glTexEnvi(target,pname,(GLint)*value); }

void Lib_glTexGeni(GLenum coord, GLenum mode, GLenum map)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context))
        return;
    if ((coord!=GL_S && coord!=GL_T) || mode!=GL_TEXTURE_GEN_MODE ||
        map!=GL_SPHERE_MAP) {
        SetError(context,GL_INVALID_ENUM);
        return;
    }
    if (coord==GL_S)
        context->textureGenModeS[context->activeTexture]=map;
    else
        context->textureGenModeT[context->activeTexture]=map;
    /* The texgen mode is baked into the record header selection, so it is
     * capture-visible semantic state. */
    BumpSemanticStateSerial(context);
    InvalidateSphereMapCache(context);
}

void Lib_glPixelStorei(GLenum pname, GLint value)
{
    struct MiniGLR200Context *context=CurrentContext();
    if (!StateChangeAllowed(context))
        return;
    if (pname==GL_UNPACK_ALIGNMENT) {
        if (value!=1 && value!=2 && value!=4 && value!=8) {
            SetError(context,GL_INVALID_VALUE);
            return;
        }
        context->unpackAlignment=(ULONG)value;
    } else if (pname==GL_UNPACK_ROW_LENGTH) {
        if (value<0) {
            SetError(context,GL_INVALID_VALUE);
            return;
        }
        context->unpackRowLength=(ULONG)value;
    } else if (pname==GL_UNPACK_SKIP_PIXELS) {
        if (value<0) {
            SetError(context,GL_INVALID_VALUE);
            return;
        }
        context->unpackSkipPixels=(ULONG)value;
    } else if (pname==GL_UNPACK_SKIP_ROWS) {
        if (value<0) {
            SetError(context,GL_INVALID_VALUE);
            return;
        }
        context->unpackSkipRows=(ULONG)value;
    } else if (pname==GL_PACK_ALIGNMENT) {
        if (value!=1 && value!=2 && value!=4 && value!=8) {
            SetError(context,GL_INVALID_VALUE);
            return;
        }
        context->packAlignment=(ULONG)value;
    } else if (pname==GL_PACK_ROW_LENGTH) {
        if (value<0) {
            SetError(context,GL_INVALID_VALUE);
            return;
        }
        context->packRowLength=(ULONG)value;
    } else if (pname==GL_PACK_SKIP_PIXELS) {
        if (value<0) {
            SetError(context,GL_INVALID_VALUE);
            return;
        }
        context->packSkipPixels=(ULONG)value;
    } else if (pname==GL_PACK_SKIP_ROWS) {
        if (value<0) {
            SetError(context,GL_INVALID_VALUE);
            return;
        }
        context->packSkipRows=(ULONG)value;
    } else {
        SetError(context,GL_INVALID_ENUM);
    }
}


void CopyBytes(void *destination, const void *source, ULONG count)
{
    UBYTE *out = destination;
    const UBYTE *in = source;
    /* Small counts (the dominant ReadNumber case) stay on the byte loop;
     * larger copies move through unrolled longword chunks. */
    while (count && (((ULONG)out | (ULONG)in) & 3UL)) {
        *out++ = *in++;
        count--;
    }
    while (count >= 16UL) {
        ((ULONG *)out)[0] = ((const ULONG *)in)[0];
        ((ULONG *)out)[1] = ((const ULONG *)in)[1];
        ((ULONG *)out)[2] = ((const ULONG *)in)[2];
        ((ULONG *)out)[3] = ((const ULONG *)in)[3];
        out += 16;
        in += 16;
        count -= 16;
    }
    while (count--) *out++ = *in++;
}

void Lib_mglTexMemStat(GLint *Current, GLint *Peak)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context) return;
    if (Current) *Current = context->texMemCurrent;
    if (Peak) *Peak = context->texMemPeak;
}
