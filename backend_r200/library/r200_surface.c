#if R200_PPC_FRONTEND
#include <stdio.h>
#endif
#include "minigl_r200_internal.h"
#include "debug_output.h"

ULONG SurfaceFormatForDepth(ULONG depth)
{
    if (depth == 8UL) return RADEON3D_FORMAT_CLUT8;
    if (depth == 16UL) return RADEON3D_FORMAT_R5G6B5PC;
    if (depth == 32UL) return RADEON3D_FORMAT_B8G8R8A8;
    return 0;
}

ULONG P96FormatForDepth(ULONG depth)
{
    if (depth == 8UL) return RGBFB_CLUT;
    if (depth == 16UL) return RGBFB_R5G6B5PC;
    if (depth == 32UL) return RGBFB_B8G8R8A8;
    return 0;
}

ULONG P96FormatMaskForDepth(ULONG depth)
{
    if (depth == 8UL) return RGBFF_CLUT;
    if (depth == 16UL) return RGBFF_R5G6B5PC;
    if (depth == 32UL) return RGBFF_B8G8R8A8;
    return 0;
}

/* p96AllocBitMap needs a friend bitmap to pick VRAM placement and an RGB
 * format; without one the allocation cannot target the board at all, so a
 * friend is genuinely required for every texture and offscreen surface we
 * hand to Radeon3DImportBitMap. The original code opened a throwaway P96
 * screen for every allocation, costing tens of milliseconds each time
 * (screen mode setup plus teardown) - that dominated dynamic texture
 * residency. Instead one tiny seed bitmap per RGB format is allocated via
 * the temporary-screen path once and kept alive for the library's
 * lifetime; every later allocation, including per-frame lightmap uploads,
 * friends off it. The cache is indexed by depth (8/16/32) so a friend's
 * format always matches the requested rgbformat exactly.
 *
 * NOTE: FriendBitMap must never call AllocateColorBitMap - that would
 * recurse infinitely, because the cache pointer is only stored after the
 * allocating call returns. */
static struct BitMap *g_friendBitMaps[3];

static ULONG FriendIndex(ULONG depth)
{
    if (depth == 8UL)
        return 0;
    if (depth == 16UL)
        return 1;
    return 2;
}

static struct BitMap *AllocateSeedBitMap(ULONG width, ULONG height,
                                         ULONG depth)
{
    struct TagItem modeTags[] = {
        {P96_BESTMODE_FORMATS_ALLOWED, 0},
        {P96_BESTMODE_NOMINAL_WIDTH, 640UL},
        {P96_BESTMODE_NOMINAL_HEIGHT, 480UL},
        {P96_BESTMODE_DEPTH, 0},
        {TAG_DONE, 0}
    };
    struct TagItem screenTags[] = {
        {P96_SCREEN_DISPLAY_ID, 0},
        {P96_SCREEN_DEPTH, 0},
        {P96_SCREEN_RGB_FORMAT, 0},
        {P96_SCREEN_SHOW_TITLE, FALSE},
        {P96_SCREEN_BEHIND, TRUE},
        {P96_SCREEN_QUIET, TRUE},
        {TAG_DONE, 0}
    };
    struct Screen *friendScreen;
    struct BitMap *bitmap;
    ULONG format = P96FormatForDepth(depth);
    ULONG formatMask = P96FormatMaskForDepth(depth);
    ULONG mode;

    if (!formatMask)
        return NULL;
    modeTags[0].ti_Data = formatMask;
    modeTags[3].ti_Data = depth;
    screenTags[1].ti_Data = depth;
    screenTags[2].ti_Data = format;
    mode = p96BestModeIDTagList(modeTags);
    if (mode == 0xffffffffUL)
        return NULL;
    screenTags[0].ti_Data = mode;
    friendScreen = p96OpenScreenTagList(screenTags);
    if (!friendScreen)
        return NULL;
    bitmap = p96AllocBitMap(width, height, depth,
                            BMF_CLEAR | BMF_DISPLAYABLE,
                            friendScreen->RastPort.BitMap,
                            format);
    p96CloseScreen(friendScreen);
    return bitmap;
}

static struct BitMap *FriendBitMap(ULONG depth)
{
    ULONG index = FriendIndex(depth);

    if (!g_friendBitMaps[index])
        g_friendBitMaps[index] = AllocateSeedBitMap(16UL, 16UL, depth);
    return g_friendBitMaps[index];
}

void ReleaseFriendBitMap(void)
{
    ULONG index;

    /* Called from MGLTerm only: all contexts are gone by then, so no
     * allocations referencing these friends can still exist. */
    for (index = 0; index < 3UL; ++index) {
        if (g_friendBitMaps[index]) {
            p96FreeBitMap(g_friendBitMaps[index]);
            g_friendBitMaps[index] = NULL;
        }
    }
}

struct BitMap *AllocateColorBitMap(ULONG width, ULONG height,
                                          ULONG depth)
{
    struct BitMap *friend;
    ULONG format = P96FormatForDepth(depth);

    if (!format)
        return NULL;
    friend = FriendBitMap(depth);
    if (friend) {
        return p96AllocBitMap(width, height, depth,
                              BMF_CLEAR | BMF_DISPLAYABLE,
                              friend, format);
    }
    /* Seed allocation failed (no VRAM or no matching mode): fall back to
     * the historical per-allocation temporary screen so behaviour degrades
     * to the old slow path instead of failing outright. */
    return AllocateSeedBitMap(width, height, depth);
}

struct BitMap *AllocateRgb565BitMap(ULONG width, ULONG height)
{
    return AllocateColorBitMap(width, height, 16UL);
}

struct BitMap *AllocateArgbBitMap(ULONG width, ULONG height)
{
    return AllocateColorBitMap(width, height, 32UL);
}

ULONG DepthStorageWidth(ULONG width)
{
    /* The R200 depth buffer is tiled: for 16-bit Z a micro-tile is a 4x4
     * group of z-pixels and the macro arrangement is 8x2 of those groups,
     * i.e. 32 pixels wide, handled in pairs.  Tile addressing is a linear
     * function of the pitch in pixels divided by the tile width, so a pitch
     * that is not a whole number of tile pairs makes a 32-pixel column
     * alias across the row boundary and lose its depth writes; the stale
     * depth then rejects everything drawn there and a black bar appears.
     * Padding to 64 pixels matches what the Linux radeon DDX does for every
     * tiled surface (displayWidth = (virtualX + 63) & ~63), which is why a
     * width like 800 never occurs there.  800 stores as 832 (1664 bytes).
     *
     * The 64-pixel rule fits every width measured on hardware so far (320,
     * 512, 640, 768 and 896 clean, 800 striped) but has not been isolated
     * by a controlled test: the 832/736 fullscreen attempts ran inside a
     * 1024-wide screen and only reported the undrawn region. */
    return (width + 63UL) & ~63UL;
}

/* Allocation attempts made while looking for depth storage that does not
 * collide with the render targets.  Rejected bitmaps stay allocated for the
 * duration so the allocator hands out a different address next time. */
#define R200_DEPTH_PLACEMENT_TRIES 8UL

static BOOL SurfaceRangesOverlap(const struct Radeon3DSurface *a,
                                 const struct Radeon3DSurface *b)
{
    ULONG aStart = a->GpuAddress, aEnd = aStart + a->Pitch * a->Height;
    ULONG bStart = b->GpuAddress, bEnd = bStart + b->Pitch * b->Height;

    return aStart < bEnd && bStart < aEnd;
}

static struct BitMap *AllocateDepthBitMap(struct Radeon3DDevice *device,
                                          ULONG width, ULONG height,
                                          struct Radeon3DSurface *surface,
                                          const struct Radeon3DSurface *avoid,
                                          ULONG avoidCount)
{
    struct BitMap *rejects[R200_DEPTH_PLACEMENT_TRIES];
    struct BitMap *bitmap = NULL;
    ULONG rejectCount = 0;
    ULONG attempt, index;

    /* Fullscreen targets have two screen buffers, and depth has been
     * observed landing inside one of them: the buffers do not come from
     * p96AllocBitMap, so its allocator can hand the same VRAM out again.
     * Depth writes then scribble over the display buffer, which shows up as
     * bright banding across the frame.  The service cannot catch this - its
     * ValidDepthTarget() only sees the color surface of the current Execute,
     * never the buffer being displayed - so reject the placement here and
     * try again, holding the loser to force a different address. */
    width = DepthStorageWidth(width);
    for (attempt = 0; attempt < R200_DEPTH_PLACEMENT_TRIES; ++attempt) {
        BOOL collides = FALSE;

        bitmap = AllocateRgb565BitMap(width, height);
        if (!bitmap)
            break;
        surface->Size = sizeof(*surface);
        if (!Radeon3DImportBitMap(device, bitmap, surface)) {
            p96FreeBitMap(bitmap);
            bitmap = NULL;
            break;
        }
        for (index = 0; index < avoidCount; ++index) {
            if (avoid[index].GpuAddress &&
                SurfaceRangesOverlap(surface, &avoid[index]))
                collides = TRUE;
        }
        if (!collides)
            break;
        dbg_puts("[minigl] depth placement collides at ");
        dbg_hex32(surface->GpuAddress);
        dbg_puts(", retrying\n");
        rejects[rejectCount++] = bitmap;
        bitmap = NULL;
    }
    if (!bitmap && rejectCount) {
        /* Every attempt collided.  Keep the last one rather than failing
         * context creation: that is what shipped before this check existed. */
        bitmap = rejects[--rejectCount];
        surface->Size = sizeof(*surface);
        if (!Radeon3DImportBitMap(device, bitmap, surface)) {
            p96FreeBitMap(bitmap);
            bitmap = NULL;
        }
    }
    while (rejectCount)
        p96FreeBitMap(rejects[--rejectCount]);
    return bitmap;
}

/* Depth storage, service-allocated where the driver supports it.  A surface
 * from Radeon3DAllocSurface() lives in VRAM the driver reserved before
 * Picasso96 saw the card, so it cannot overlap a screen buffer at all and
 * the placement guessing above never runs.  *bitmapOut is NULL in that case:
 * the driver owns the memory and Radeon3DReleaseSurface() returns it.  Older
 * drivers fall back to the p96 bitmap and its retry loop. */
BOOL AllocateDepthStorage(struct Radeon3DDevice *device, ULONG radeonCaps,
                          ULONG width, ULONG height,
                          struct Radeon3DSurface *surface,
                          struct BitMap **bitmapOut,
                          const struct Radeon3DSurface *avoid,
                          ULONG avoidCount)
{
    *bitmapOut = NULL;
    surface->Size = sizeof(*surface);
    if ((radeonCaps & RADEON3D_CAP_AUX_SURFACES) &&
        Radeon3DAllocSurface(device, width, height,
                             RADEON3D_FORMAT_R5G6B5PC, surface))
        return TRUE;
    *bitmapOut = AllocateDepthBitMap(device, width, height, surface,
                                     avoid, avoidCount);
    return *bitmapOut != NULL;
}

BOOL DrawRectangle(const struct MiniGLR200Context *context,
                          ULONG *left, ULONG *top,
                          ULONG *right, ULONG *bottom)
{
    LONG l=0,t=0,r=(LONG)context->width,b=(LONG)context->height;
    if (context->scissorEnabled) {
        l=context->scissorX;
        r=l+context->scissorWidth;
        t=(LONG)context->height-context->scissorY-context->scissorHeight;
        b=t+context->scissorHeight;
        if (l<0)
            l=0;
        if (t<0)
            t=0;
        if (r>(LONG)context->width)
            r=context->width;
        if (b>(LONG)context->height)
            b=context->height;
    }
    if (l>=r || t>=b)
        return FALSE;
    *left=(ULONG)l; *top=(ULONG)t; *right=(ULONG)r; *bottom=(ULONG)b;
    return TRUE;
}

/* Drain both fullscreen notification ports, matching every message against
 * the per-buffer DBufInfo messages. Arrival order is irrelevant: each match
 * sets that buffer's bit in bufferSafe/bufferDisplayed. Messages that match
 * no live buffer (teardown races) are dropped. */
static void PumpBufferMessages(struct MiniGLR200Context *context)
{
    struct Message *message;
    ULONG index;

    while ((message = GetMsg(context->safePort))) {
        for (index = 0; index < R200_SCREEN_BUFFERS; ++index) {
            if (context->screenBuffers[index] &&
                message == &context->screenBuffers[index]->sb_DBufInfo
                               ->dbi_SafeMessage) {
                context->bufferSafe |= 1UL << index;
                break;
            }
        }
    }
    while ((message = GetMsg(context->displayPort))) {
        for (index = 0; index < R200_SCREEN_BUFFERS; ++index) {
            if (context->screenBuffers[index] &&
                message == &context->screenBuffers[index]->sb_DBufInfo
                               ->dbi_DispMessage) {
                context->bufferDisplayed |= 1UL << index;
                break;
            }
        }
    }
}

/* Complete an outstanding screen-buffer switch. When the pending buffer's
 * DispMessage arrives, that buffer becomes displayed and the previously
 * displayed buffer retires at the same vblank: its SafeMessage is consumed
 * by the same pump, so it can become the next drawing buffer immediately.
 * With block, waits up to ~2 seconds (100 TOFs) like the old code; without,
 * returns FALSE when the switch has not taken effect yet, and the caller
 * skips the flip -- the drawing buffer is neither displayed nor pending, so
 * the next frame may simply overwrite it (latest completed frame wins). */
static BOOL CompletePendingFlip(struct MiniGLR200Context *context, BOOL block)
{
    ULONG attempts;

    if (!context->pendingValid)
        return TRUE;
    for (attempts = 0; attempts < 100; ++attempts) {
        ULONG retired;
        ULONG completed;

        PumpBufferMessages(context);
        completed = context->bufferDisplayed &
                    (1UL << context->pendingBuffer);
        if (completed) {
            retired = context->displayedBuffer;
            context->displayedBuffer = context->pendingBuffer;
            context->pendingValid = FALSE;
            context->bufferDisplayed &= ~(1UL << context->displayedBuffer);
            context->bufferSafe &= ~(1UL << context->displayedBuffer);
            context->bufferSafe |= 1UL << retired;
            context->drawingBuffer = retired;
            return TRUE;
        }
        if (!block)
            return FALSE;
        WaitTOF();
    }
    dbg_puts("[mgl] cpf=timeout pend=");
    dbg_u32(context->pendingBuffer);
    dbg_puts(" disp=");
    dbg_u32(context->displayedBuffer);
    dbg_puts(" dispmask=");
    dbg_u32(context->bufferDisplayed);
    dbg_newline();
    return FALSE;
}

/* The frame about to be submitted targets TargetSurface(): the drawing
 * buffer, or the displayed buffer for glDrawBuffer(GL_FRONT). It must have
 * left the display (or never been displayed). Steady state always satisfies
 * this for the drawing buffer -- the bit was pumped when the buffer
 * retired -- so this only waits in the rare window right after a rotation,
 * on the first frames, or for front-buffer rendering. */
static BOOL WaitTargetBufferSafe(struct MiniGLR200Context *context)
{
    ULONG target = context->fullscreen &&
                           context->drawBuffer == GL_FRONT
                       ? context->displayedBuffer
                       : context->drawingBuffer;
    ULONG attempts;

    for (attempts = 0; attempts < 100; ++attempts) {
        PumpBufferMessages(context);
        if (context->bufferSafe & (1UL << target))
            return TRUE;
        WaitTOF();
    }
    return FALSE;
}

BOOL ChangeBuffer(struct MiniGLR200Context *context,
                  struct ScreenBuffer *buffer)
{
    ULONG attempts;

    for (attempts = 0; attempts < 100; ++attempts) {
        if (ChangeScreenBuffer(context->screen, buffer))
            return TRUE;
        WaitTOF();
    }
    return FALSE;
}

void DrainNotifications(struct MsgPort *port)
{
    if (port)
        while (GetMsg(port)) {}
}

void RestorePrimaryBuffer(struct MiniGLR200Context *context)
{
    if (!context->fullscreen || !context->screenOwned ||
        !context->screenBuffers[0])
        return;
    /* Finish any outstanding switch so exactly one switch (back to buffer
     * 0) is requested, then wait for it to take effect. */
    (void)CompletePendingFlip(context, TRUE);
    if (context->displayedBuffer != 0) {
        while (!ChangeScreenBuffer(context->screen,
                                   context->screenBuffers[0]))
            WaitTOF();
        context->pendingValid = TRUE;
        context->pendingBuffer = 0;
        context->bufferSafe &= ~(1UL << 0);
        context->bufferDisplayed &= ~(1UL << 0);
        (void)CompletePendingFlip(context, TRUE);
    }
}

void Lib_glClearColor(GLclampf red, GLclampf green, GLclampf blue,
                       GLclampf alpha)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!StateChangeAllowed(context)) return;
    context->clearColor = PackColor(red, green, blue, alpha);
}

void Lib_glClear(GLbitfield mask)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct SubmitSlot *slot;
    struct Radeon3DSurface *target;
    ULONG *record;
    ULONG left,top,right,bottom,generatedRequired;
    BOOL clearDepth;

    if (!context)
        return;
    if (context->insideBegin || (mask & ~(GL_COLOR_BUFFER_BIT |
                                          GL_DEPTH_BUFFER_BIT))) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!(mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)))
        return;
    /* glDrawBuffer(GL_NONE) suppresses colour writes; depth is unaffected. */
    if (context->drawBuffer == GL_NONE)
        mask &= ~GL_COLOR_BUFFER_BIT;
    if (!mask)
        return;
    if (!FlushScreenVertices(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    slot = BuildSubmitSlot(context);
    if (slot->recordCount && slot->stateBatch) {
        if (!Submit(context)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
        slot = BuildSubmitSlot(context);
    }
    if (!DrawRectangle(context,&left,&top,&right,&bottom)) return;
    target = TargetSurface(context);
    clearDepth = (mask & GL_DEPTH_BUFFER_BIT) && context->depthMask;
    generatedRequired = GeneratedClearDwords(clearDepth);
    if ((slot->recordCount + RADEON3D_EXEC_CLEAR_DWORDS >
             context->batchDwords ||
         slot->generatedDwordCount + generatedRequired >
             context->batchDwords) &&
        !Submit(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    slot = BuildSubmitSlot(context);
    record = slot->records + slot->recordCount;
    record[0] = RADEON3D_EXEC_CLEAR;
    record[1] = RADEON3D_EXEC_CLEAR_DWORDS;
    record[2] = (ULONG)target->Handle;
    record[3] = clearDepth
                    ? (ULONG)context->depthSurface.Handle : 0;
    record[4] = (mask & GL_COLOR_BUFFER_BIT ? RADEON3D_CLEAR_COLOR : 0) |
                ((mask & GL_DEPTH_BUFFER_BIT) && context->depthMask
                     ? RADEON3D_CLEAR_DEPTH : 0);
    if (!record[4])
        return;
    record[5] = context->clearColor;
    record[6] = FloatBits(context->clearDepth);
    record[7] = left;
    record[8] = top;
    record[9] = right;
    record[10] = bottom;
    slot->recordCount += RADEON3D_EXEC_CLEAR_DWORDS;
    slot->generatedDwordCount += generatedRequired;
    slot->lastDrawValid = FALSE;
    if (context->perfEnabled)
        context->perf.clearRecords++;
}

/*
 * glReadPixels/mglWriteShotPPM readback path: surfacePixel is a raw pixel
 * from a Radeon3DSurface in its native Format; rgba is written as
 * {red,green,blue,alpha}. CLUT8 has no palette visible at this layer, so its
 * index is exposed as greyscale -- callers reading back an 8-bit fullscreen
 * mode get an approximation, not true colour.
 */
static void ReadSurfacePixel(const UBYTE *surfacePixel, ULONG surfaceFormat,
                             UBYTE *rgba)
{
    if (surfaceFormat==RADEON3D_FORMAT_B8G8R8A8) {
        rgba[2]=surfacePixel[0]; rgba[1]=surfacePixel[1];
        rgba[0]=surfacePixel[2]; rgba[3]=surfacePixel[3];
    } else if (surfaceFormat==RADEON3D_FORMAT_R5G6B5PC) {
        UWORD value; CopyBytes(&value,surfacePixel,2);
        /* R5G6B5PC stores little-endian words; the CopyBytes above loaded
         * big-endian, so swap before decoding or every channel crosses. */
        value=(UWORD)((value<<8)|(value>>8));
        rgba[0]=(UBYTE)(((value>>11)&31)*255/31);
        rgba[1]=(UBYTE)(((value>>5)&63)*255/63);
        rgba[2]=(UBYTE)((value&31)*255/31);
        rgba[3]=255;
    } else {
        rgba[0]=rgba[1]=rgba[2]=surfacePixel[0]; rgba[3]=255;
    }
}

static void WriteDestPixel(const UBYTE *rgba, GLenum format, GLenum type,
                           UBYTE *destination)
{
    if (type==MGL_UNSIGNED_SHORT_5_6_5) {
        UWORD value=(UWORD)(((rgba[0]>>3)<<11)|((rgba[1]>>2)<<5)|(rgba[2]>>3));
        CopyBytes(destination,&value,2);
    } else if (type==MGL_UNSIGNED_SHORT_4_4_4_4) {
        UWORD value=(UWORD)(((rgba[0]>>4)<<12)|((rgba[1]>>4)<<8)|
                            ((rgba[2]>>4)<<4)|(rgba[3]>>4));
        CopyBytes(destination,&value,2);
    } else if (format==GL_ALPHA) destination[0]=rgba[3];
    else if (format==GL_LUMINANCE) destination[0]=rgba[0];
    else if (format==GL_LUMINANCE_ALPHA) {
        destination[0]=rgba[0]; destination[1]=rgba[3];
    } else if (format==GL_RGB) {
        destination[0]=rgba[0]; destination[1]=rgba[1]; destination[2]=rgba[2];
    } else if (format==GL_RGBA) {
        destination[0]=rgba[0]; destination[1]=rgba[1];
        destination[2]=rgba[2]; destination[3]=rgba[3];
    } else if (format==MGL_UBYTE_BGRA) {
        destination[0]=rgba[2]; destination[1]=rgba[1];
        destination[2]=rgba[0]; destination[3]=rgba[3];
    } else {
        destination[0]=rgba[3]; destination[1]=rgba[0];
        destination[2]=rgba[1]; destination[3]=rgba[2];
    }
}

static ULONG SurfaceBytesPerPixel(ULONG surfaceFormat)
{
    if (surfaceFormat==RADEON3D_FORMAT_CLUT8) return 1;
    if (surfaceFormat==RADEON3D_FORMAT_R5G6B5PC) return 2;
    return 4;
}

void Lib_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                      GLenum format, GLenum type, GLvoid *pixels)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct Radeon3DSurface *surface;
    ULONG destBytes, destWidth, destStride, surfaceBytesPerPixel;
    ULONG row, column;
    UBYTE *destBase;
    if (!context) return;
#if R200_PPC_FRONTEND
    (void)x; (void)y; (void)width; (void)height;
    (void)format; (void)type; (void)pixels;
    SetError(context, GL_INVALID_OPERATION);
    return;
#endif
    if (context->insideBegin) { SetError(context,GL_INVALID_OPERATION); return; }
    if (width<0 || height<0) { SetError(context,GL_INVALID_VALUE); return; }
    if (!width || !height) return;
    if (!pixels || !SourceFormatBytes(format,type,&destBytes)) {
        SetError(context,GL_INVALID_VALUE); return;
    }
    if (x<0 || y<0 || (ULONG)x+(ULONG)width>context->width ||
        (ULONG)y+(ULONG)height>context->height) {
        SetError(context,GL_INVALID_VALUE); return;
    }
    if (!FlushScreenVertices(context) || !SubmitAndWait(context)) {
        SetError(context,GL_INVALID_OPERATION); return;
    }
    surface = ReadSourceSurface(context);
    surfaceBytesPerPixel = SurfaceBytesPerPixel(surface->Format);
    destWidth = context->packRowLength ? context->packRowLength : (ULONG)width;
    destStride = destWidth*destBytes;
    destStride = (destStride+context->packAlignment-1UL) &
                 ~(context->packAlignment-1UL);
    destBase = (UBYTE *)pixels + context->packSkipRows*destStride +
              context->packSkipPixels*destBytes;
    for (row=0; row<(ULONG)height; ++row) {
        /* GL row 0 is the bottom of the window; surface row 0 is the top. */
        ULONG surfaceRow = context->height - 1UL - ((ULONG)y + row);
        const UBYTE *sourceRow = (const UBYTE *)surface->CpuAddress +
            surfaceRow*surface->Pitch + (ULONG)x*surfaceBytesPerPixel;
        UBYTE *destRow = destBase + row*destStride;
        for (column=0; column<(ULONG)width; ++column) {
            UBYTE rgba[4];
            ReadSurfacePixel(sourceRow+column*surfaceBytesPerPixel,
                             surface->Format, rgba);
            WriteDestPixel(rgba, format, type, destRow+column*destBytes);
        }
    }
}

static BOOL WritePPMHeader(BPTR file, ULONG width, ULONG height)
{
    char buffer[32];
    ULONG index = 0, value;
    buffer[index++]='P'; buffer[index++]='6'; buffer[index++]='\n';
    value=width;
    if (!value) buffer[index++]='0';
    else {
        char digits[10]; ULONG count=0;
        while (value) { digits[count++]=(char)('0'+value%10); value/=10; }
        while (count) buffer[index++]=digits[--count];
    }
    buffer[index++]=' ';
    value=height;
    if (!value) buffer[index++]='0';
    else {
        char digits[10]; ULONG count=0;
        while (value) { digits[count++]=(char)('0'+value%10); value/=10; }
        while (count) buffer[index++]=digits[--count];
    }
    buffer[index++]='\n'; buffer[index++]='2'; buffer[index++]='5';
    buffer[index++]='5'; buffer[index++]='\n';
    return Write(file,buffer,(LONG)index)==(LONG)index;
}

void Lib_mglWriteShotPPM(char *filename)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct Radeon3DSurface *surface;
    BPTR file;
    ULONG surfaceBytesPerPixel, x, y;
    UBYTE rowBuffer[3*2048];
    if (!context || !filename) return;
#if R200_PPC_FRONTEND
    SetError(context, GL_INVALID_OPERATION);
    return;
#endif
    if (context->insideBegin) { SetError(context,GL_INVALID_OPERATION); return; }
    if (!FlushScreenVertices(context) || !SubmitAndWait(context)) {
        SetError(context,GL_INVALID_OPERATION); return;
    }
    file = Open((CONST_STRPTR)filename, MODE_NEWFILE);
    if (!file) { SetError(context,GL_INVALID_OPERATION); return; }
    if (!WritePPMHeader(file, context->width, context->height)) {
        Close(file); SetError(context,GL_INVALID_OPERATION); return;
    }
    surface = ReadSourceSurface(context);
    surfaceBytesPerPixel = SurfaceBytesPerPixel(surface->Format);
    for (y=0; y<context->height; ++y) {
        /* PPM rows run top-to-bottom; the surface's row 0 is already the top. */
        const UBYTE *sourceRow = (const UBYTE *)surface->CpuAddress +
            y*surface->Pitch;
        for (x=0; x<context->width && x<2048UL; ++x) {
            UBYTE rgba[4];
            ReadSurfacePixel(sourceRow+x*surfaceBytesPerPixel,
                             surface->Format, rgba);
            rowBuffer[x*3]=rgba[0]; rowBuffer[x*3+1]=rgba[1];
            rowBuffer[x*3+2]=rgba[2];
        }
        if (Write(file,rowBuffer,(LONG)(3*(context->width<2048UL ?
                                          context->width : 2048UL))) < 0) {
            Close(file); SetError(context,GL_INVALID_OPERATION); return;
        }
    }
    Close(file);
}

void Lib_glFlush(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context)
        return;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!FlushScreenVertices(context) || !Submit(context))
        SetError(context, GL_INVALID_OPERATION);
}

void Lib_glFinish(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (!context)
        return;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!FlushScreenVertices(context) || !SubmitAndWait(context)
#if R200_PPC_FRONTEND
        || !R200PPCPlatformBarrier(context, FALSE)
#endif
       )
        SetError(context, GL_INVALID_OPERATION);
}

GLenum Lib_glGetError(void)
{
    struct MiniGLR200Context *context = CurrentContext();
    GLenum error;
    if (!context)
        return GL_INVALID_OPERATION;
    error = context->error;
    context->error = GL_NO_ERROR;
    return error;
}

void Lib_mglChoosePixelDepth(int depth)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (context) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!LibraryBase)
        return;
    ObtainSemaphore(&LibraryBase->lock);
    if (!LibraryBase->context)
        LibraryBase->pixelDepth = (ULONG)depth;
    ReleaseSemaphore(&LibraryBase->lock);
}

void Lib_mglChooseWindowMode(GLboolean windowMode)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (context) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!LibraryBase)
        return;
    ObtainSemaphore(&LibraryBase->lock);
    if (!LibraryBase->context)
        LibraryBase->windowMode = windowMode ? TRUE : FALSE;
    ReleaseSemaphore(&LibraryBase->lock);
}

/*
 * Pre-context hints with no lever in this backend (see the comment on
 * struct MiniGLR200Base in minigl_r200_base.h): accepted and stored, not
 * wired to any mechanism. Calling one after a context exists is still a
 * usage error, matching mglChoosePixelDepth/mglChooseWindowMode above.
 */
#define DEFINE_PRECONTEXT_BOOL_CHOOSER(fnName, field) \
    void fnName(GLboolean value) \
    { \
        struct MiniGLR200Context *context = CurrentContext(); \
        if (context) { \
            SetError(context, GL_INVALID_OPERATION); \
            return; \
        } \
        if (!LibraryBase) \
            return; \
        ObtainSemaphore(&LibraryBase->lock); \
        if (!LibraryBase->context) \
            LibraryBase->field = value ? TRUE : FALSE; \
        ReleaseSemaphore(&LibraryBase->lock); \
    }

#define DEFINE_PRECONTEXT_INT_CHOOSER(fnName, field) \
    void fnName(int value) \
    { \
        struct MiniGLR200Context *context = CurrentContext(); \
        if (context) { \
            SetError(context, GL_INVALID_OPERATION); \
            return; \
        } \
        if (!LibraryBase) \
            return; \
        ObtainSemaphore(&LibraryBase->lock); \
        if (!LibraryBase->context) \
            LibraryBase->field = (LONG)value; \
        ReleaseSemaphore(&LibraryBase->lock); \
    }

DEFINE_PRECONTEXT_BOOL_CHOOSER(Lib_mglChooseGuardBand, guardBand)
DEFINE_PRECONTEXT_BOOL_CHOOSER(Lib_mglProhibitAlphaFallback, prohibitAlphaFallback)
DEFINE_PRECONTEXT_BOOL_CHOOSER(Lib_mglProhibitMipMapping, prohibitMipMapping)
DEFINE_PRECONTEXT_BOOL_CHOOSER(Lib_mglProposeCloseDesktop, proposeCloseDesktop)
DEFINE_PRECONTEXT_INT_CHOOSER(Lib_mglChooseTextureBufferSize, textureBufferSize)
DEFINE_PRECONTEXT_INT_CHOOSER(Lib_mglChooseVertexBufferSize, vertexBufferSize)
DEFINE_PRECONTEXT_INT_CHOOSER(Lib_mglChooseMtexBufferSize, mtexBufferSize)

/*
 * V19 parity: mglChooseNumberOfBuffers() selects the fullscreen screen
 * buffer count (1 = diagnostic single buffer, 2 = double buffer, 3 = the
 * validated default). Clamped to the supported range; windowed contexts
 * always use a single surface plus blit present regardless. The value is
 * consumed by CreateFullscreenTarget() below.
 */
void Lib_mglChooseNumberOfBuffers(int number)
{
    struct MiniGLR200Context *context = CurrentContext();
    if (context) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!LibraryBase)
        return;
    ObtainSemaphore(&LibraryBase->lock);
    if (!LibraryBase->context) {
        if (number < 1)
            number = 1;
        if (number > 3)
            number = 3;
        LibraryBase->numberOfBuffers = (LONG)number;
    }
    ReleaseSemaphore(&LibraryBase->lock);
}

#undef DEFINE_PRECONTEXT_BOOL_CHOOSER
#undef DEFINE_PRECONTEXT_INT_CHOOSER

/*
 * V19 semantics (mglQ3 context.c): iterate every supported screen mode,
 * stop at the first mode the callback accepts (GL_TRUE) and return its
 * ID; return MGL_SM_BESTMODE when nothing is selected or no callback was
 * supplied. R200 keeps its 8/16/32-bit P96-only filter.
 */
GLint Lib_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn)
{
    ULONG displayID = 0xffffffffUL;
    if (!CallbackFn)
        return (GLint)MGL_SM_BESTMODE;
    while ((displayID = NextDisplayInfo(displayID)) != 0xffffffffUL) {
        struct DimensionInfo dims;
        struct NameInfo nameInfo;
        MGLScreenMode mode;
        ULONG depth, index;
        if (!p96GetModeIDAttr(displayID, P96_MODE_IS_P96))
            continue;
        depth = p96GetModeIDAttr(displayID, P96_MODE_DEPTH);
        if (!SurfaceFormatForDepth(depth))
            continue;
        if (GetDisplayInfoData(NULL, (APTR)&dims, sizeof(dims), DTAG_DIMS,
                               displayID) < sizeof(struct QueryHeader))
            continue;
        mode.id = (GLint)displayID;
        mode.width = (GLint)(dims.Nominal.MaxX - dims.Nominal.MinX + 1);
        mode.height = (GLint)(dims.Nominal.MaxY - dims.Nominal.MinY + 1);
        mode.bit_depth = (GLint)depth;
        mode.mode_name[0] = 0;
        if (GetDisplayInfoData(NULL, (APTR)&nameInfo, sizeof(nameInfo),
                               DTAG_NAME, displayID) >=
            sizeof(struct QueryHeader)) {
            for (index = 0; index < DISPLAYNAMELEN-1 &&
                            nameInfo.Name[index]; ++index)
                mode.mode_name[index] = (char)nameInfo.Name[index];
            mode.mode_name[index] = 0;
        }
        if (CallbackFn(&mode))
            return mode.id;
    }
    return (GLint)MGL_SM_BESTMODE;
}

BOOL CreateWindowTarget(struct MiniGLR200Context *context, ULONG width,
                        ULONG height)
{
    struct TagItem windowTags[17];

    context->bitmap = AllocateColorBitMap(width, height,
                                          context->pixelDepth);
    context->screen = LockPubScreen(NULL);
    context->screenLocked = context->screen != NULL;
    if (!context->bitmap || !context->screen)
        return FALSE;
    ZeroMemory(windowTags, sizeof(windowTags));
    windowTags[0].ti_Tag = WA_PubScreen;
    windowTags[0].ti_Data = (ULONG)context->screen;
    windowTags[1].ti_Tag = WA_InnerWidth;
    windowTags[1].ti_Data = width;
    windowTags[2].ti_Tag = WA_InnerHeight;
    windowTags[2].ti_Data = height;
    windowTags[3].ti_Tag = WA_Title;
    windowTags[3].ti_Data = (ULONG)"MiniGL_R200";
    windowTags[4].ti_Tag = WA_DragBar;
    windowTags[4].ti_Data = TRUE;
    windowTags[5].ti_Tag = WA_DepthGadget;
    windowTags[5].ti_Data = TRUE;
    windowTags[6].ti_Tag = WA_CloseGadget;
    windowTags[6].ti_Data = TRUE;
    windowTags[7].ti_Tag = WA_Activate;
    windowTags[7].ti_Data = TRUE;
    windowTags[8].ti_Tag = WA_SmartRefresh;
    windowTags[8].ti_Data = TRUE;
    windowTags[9].ti_Tag = WA_IDCMP;
    windowTags[9].ti_Data = IDCMP_CLOSEWINDOW;
    windowTags[10].ti_Tag = WA_SizeGadget;
    windowTags[10].ti_Data = TRUE;
    windowTags[11].ti_Tag = WA_SizeBBottom;
    windowTags[11].ti_Data = TRUE;
    windowTags[12].ti_Tag = WA_MinWidth;
    windowTags[12].ti_Data = 64;
    windowTags[13].ti_Tag = WA_MinHeight;
    windowTags[13].ti_Data = 64;
    windowTags[14].ti_Tag = WA_MaxWidth;
    windowTags[14].ti_Data = context->screen->Width;
    windowTags[15].ti_Tag = WA_MaxHeight;
    windowTags[15].ti_Data = context->screen->Height;
    windowTags[16].ti_Tag = TAG_DONE;
    context->window = OpenWindowTagList(NULL, windowTags);
    if (!context->window)
        return FALSE;
    context->baseIDCMP = IDCMP_CLOSEWINDOW;
    context->surfaces[0].Size = sizeof(context->surfaces[0]);
    if (!Radeon3DImportBitMap(context->device, context->bitmap,
                              &context->surfaces[0]) ||
        context->surfaces[0].Format != context->colorFormat ||
        context->surfaces[0].Width < width ||
        context->surfaces[0].Height != height)
        return FALSE;
    InitRastPort(&context->source);
    context->source.BitMap = context->bitmap;
    context->width = width;
    context->height = height;
    return TRUE;
}

BOOL CreateFullscreenTarget(struct MiniGLR200Context *context,
                            ULONG requestedWidth, ULONG requestedHeight,
                            ULONG requestedMode, BOOL modeSpecified)
{
    struct TagItem modeTags[] = {
        {P96_BESTMODE_FORMATS_ALLOWED, 0},
        {P96_BESTMODE_NOMINAL_WIDTH, 0},
        {P96_BESTMODE_NOMINAL_HEIGHT, 0},
        {P96_BESTMODE_DEPTH, 0},
        {TAG_DONE, 0}
    };
    struct TagItem screenTags[] = {
        {SA_DisplayID, 0},
        {SA_Depth, 0},
        {SA_ShowTitle, FALSE},
        {SA_Draggable, FALSE},
        {SA_Quiet, TRUE},
        {TAG_DONE, 0}
    };
    struct TagItem windowTags[] = {
        {WA_CustomScreen, 0},
        {WA_Left, 0},
        {WA_Top, 0},
        {WA_Width, 0},
        {WA_Height, 0},
        {WA_Flags, WFLG_ACTIVATE | WFLG_BORDERLESS | WFLG_BACKDROP |
                   WFLG_RMBTRAP},
        {WA_IDCMP, IDCMP_RAWKEY},
        {TAG_DONE, 0}
    };
    ULONG selectedMode;
    ULONG index;

    dbg_puts("[mgl] fs=begin\n");

    modeTags[0].ti_Data = P96FormatMaskForDepth(context->pixelDepth);
    modeTags[1].ti_Data = requestedWidth;
    modeTags[2].ti_Data = requestedHeight;
    modeTags[3].ti_Data = context->pixelDepth;
    screenTags[1].ti_Data = context->pixelDepth;
    selectedMode = modeSpecified ? requestedMode
                                 : p96BestModeIDTagList(modeTags);
    if (selectedMode == 0xffffffffUL) {
        dbg_puts("[mgl] fsfail=modeid\n");
        return FALSE;
    }
    if (!p96GetModeIDAttr(selectedMode, P96_MODE_IS_P96)) {
        dbg_puts("[mgl] fsfail=notp96\n");
        return FALSE;
    }
    if (p96GetModeIDAttr(selectedMode, P96_MODE_DEPTH) !=
        context->pixelDepth) {
        dbg_puts("[mgl] fsfail=depth\n");
        return FALSE;
    }
    if (p96GetModeIDAttr(selectedMode, P96_MODE_RGB_FORMAT) !=
        P96FormatForDepth(context->pixelDepth)) {
        dbg_puts("[mgl] fsfail=fmt\n");
        return FALSE;
    }
    screenTags[0].ti_Data = selectedMode;
    context->screen = OpenScreenTagList(NULL, screenTags);
    context->screenOwned = context->screen != NULL;
    if (!context->screen) {
        dbg_puts("[mgl] fsfail=screen\n");
        return FALSE;
    }
    if (context->pixelDepth == 8UL) {
        for (index = 0; index < 256UL; ++index) {
            ULONG red = ((index >> 5) & 7UL) * 0x24924924UL;
            ULONG green = ((index >> 2) & 7UL) * 0x24924924UL;
            ULONG blue = (index & 3UL) * 0x55555555UL;
            SetRGB32(&context->screen->ViewPort, index, red, green, blue);
        }
    }
    context->width = context->screen->Width;
    context->height = context->screen->Height;
    if (context->width < 64 || context->height < 64 ||
        context->width > 1920 || context->height > 1200) {
        dbg_puts("[mgl] fsfail=size\n");
        return FALSE;
    }
    windowTags[0].ti_Data = (ULONG)context->screen;
    windowTags[3].ti_Data = context->width;
    windowTags[4].ti_Data = context->height;
    context->window = OpenWindowTagList(NULL, windowTags);
    context->safePort = CreateMsgPort();
    context->displayPort = CreateMsgPort();
    if (!context->window || !context->safePort || !context->displayPort) {
        dbg_puts("[mgl] fsfail=windowport\n");
        return FALSE;
    }
    context->baseIDCMP = IDCMP_RAWKEY;
    {
        /* Temporary diagnostic knob: MINIGL_BUFFERS=1 renders straight
         * into the visible screen bitmap -- no AllocScreenBuffer copies,
         * no rotation, present is a no-op. Distinguishes render-path
         * corruption from screen-buffer-copy placement problems. */
        TEXT buf[8];
        if (GetVar("MINIGL_BUFFERS", buf, sizeof(buf), GVF_GLOBAL_ONLY) > 0 &&
            buf[0] == '1') {
            context->singleBuffer = TRUE;
            dbg_puts("[mgl] fs single-buffer\n");
        }
    }
    context->screenBuffers[0] =
        AllocScreenBuffer(context->screen, NULL, SB_SCREEN_BITMAP);
    if (!context->screenBuffers[0]) {
        dbg_puts("[mgl] fsfail=alloc0\n");
        return FALSE;
    }
    /* mglChooseNumberOfBuffers(): 1 = single buffer, 2 = double buffer,
     * 3 = the validated default (kept when no explicit choice was made). */
    context->screenBufferCount =
        LibraryBase && LibraryBase->numberOfBuffers >= 1 &&
                LibraryBase->numberOfBuffers <= 3
            ? (ULONG)LibraryBase->numberOfBuffers
            : 3UL;
    if (context->screenBufferCount == 1UL)
        context->singleBuffer = TRUE;
    if (!context->singleBuffer) {
        context->screenBuffers[1] =
            AllocScreenBuffer(context->screen, NULL, SB_COPY_BITMAP);
        if (!context->screenBuffers[1]) {
            dbg_puts("[mgl] fsfail=alloc1\n");
            return FALSE;
        }
        if (context->screenBufferCount >= 3UL) {
            context->screenBuffers[2] =
                AllocScreenBuffer(context->screen, NULL, SB_COPY_BITMAP);
            if (!context->screenBuffers[2]) {
                dbg_puts("[mgl] fsfail=alloc2\n");
                return FALSE;
            }
        }
    }
    for (index = 0; index < R200_SCREEN_BUFFERS; ++index) {
        if (!context->screenBuffers[index])
            continue;
        context->screenBuffers[index]->sb_DBufInfo
            ->dbi_SafeMessage.mn_ReplyPort = context->safePort;
        context->screenBuffers[index]->sb_DBufInfo
            ->dbi_DispMessage.mn_ReplyPort = context->displayPort;
        context->surfaces[index].Size = sizeof(context->surfaces[index]);
        if (!Radeon3DImportBitMap(
                context->device, context->screenBuffers[index]->sb_BitMap,
                &context->surfaces[index])) {
            dbg_puts("[mgl] fsfail=import\n");
            return FALSE;
        }
        /* The import derives Width from the bitmap pitch: at 1 byte per
         * pixel P96 pads rows from 800 to 832, so accept any surface at
         * least as wide as the drawable (the display fetch uses the
         * bitmap's own pitch, and the emitter clamps drawing to the
         * context size). Height has no padding and must match exactly. */
        if (context->surfaces[index].Format != context->colorFormat ||
            context->surfaces[index].Width < context->width ||
            context->surfaces[index].Height != context->height) {
            dbg_puts("[mgl] fsfail=importshape idx=");
            dbg_u32(index);
            dbg_puts(" fmt=");
            dbg_u32(context->surfaces[index].Format);
            dbg_u32(context->colorFormat);
            dbg_puts(" w=");
            dbg_u32(context->surfaces[index].Width);
            dbg_u32(context->width);
            dbg_puts(" h=");
            dbg_u32(context->surfaces[index].Height);
            dbg_u32(context->height);
            dbg_puts("\n");
            return FALSE;
        }
    }
    dbg_puts("[mgl] fs=ok buffers=");
    dbg_u32(context->screenBufferCount);
    dbg_newline();
    {
        TEXT present[12];
        if (GetVar("MINIGL_PRESENT", present, sizeof(present),
                   GVF_GLOBAL_ONLY) > 0) {
            if (present[0] == 'B' && present[1] == 'L' &&
                present[2] == 'I' && present[3] == 'T') {
                context->presentBlit = TRUE;
                dbg_puts("[mgl] fs present=blit\n");
            } else if (present[0] == 'F' && present[1] == 'L' &&
                       present[2] == 'I' && present[3] == 'P') {
                /* Force the vblank-locked screen-buffer flip: the blit
                 * present is not tear-free, so this is how a run separates
                 * present artifacts from render bugs. Sync has to be pinned
                 * on with it -- the flip path's buffer rotation depends on
                 * the sync waits, and without them it hands the client a
                 * buffer that is still displayed. */
                context->presentFlip = TRUE;
                context->syncEnabled = TRUE;
                context->syncForced = TRUE;
                dbg_puts("[mgl] fs present=flip\n");
            }
        }
    }
    if (context->singleBuffer) {
        /* Everything renders into the visible bitmap. */
        context->displayedBuffer = 0;
        context->drawingBuffer = 0;
        context->bufferSafe = 0;
        context->bufferDisplayed = 1UL << 0;
    } else {
        context->displayedBuffer = 0;
        context->drawingBuffer = 1;
        /* Buffers above 0 have never been displayed, so rendering into
         * them needs no retirement wait; buffer 0 is on screen. */
        context->bufferSafe = 0;
        for (index = 1; index < context->screenBufferCount; ++index)
            context->bufferSafe |= 1UL << index;
        context->bufferDisplayed = 0;
    }
    context->pendingValid = FALSE;
    context->pendingBuffer = 0;
    return TRUE;
}

GLboolean Lib_mglLockDisplay(void)
{
    return CurrentContext() ? GL_TRUE : GL_FALSE;
}

void Lib_mglUnlockDisplay(void) {}

void Lib_mglLockMode(GLenum lockMode)
{
    (void)lockMode;
}

GLboolean Lib_mglLockBack(MGLLockInfo *info)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct Radeon3DSurface *surface;
    if (!context || !info || context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    if (!FlushScreenVertices(context) || !SubmitAndWait(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    surface = TargetSurface(context);
    info->width = context->width;
    info->height = context->height;
    info->depth = context->pixelDepth;
    info->pixel_format = context->colorFormat;
    info->base_address = surface->CpuAddress;
    info->pitch = surface->Pitch;
    return GL_TRUE;
}

void Lib_mglResizeContext(GLsizei width, GLsizei height)
{
    struct MiniGLR200Context *context = CurrentContext();
    struct BitMap *newBitmap = NULL, *newDepthBitmap = NULL;
    struct BitMap *oldBitmap, *oldDepthBitmap;
    struct Radeon3DSurface newSurface, newDepthSurface;
    struct Radeon3DSurface oldSurface, oldDepthSurface;
    ULONG oldWidth, oldHeight, storageWidth;

    if (!context) return;
    if (width < 64 || height < 64 || width > 1920 || height > 1200) {
        SetError(context, GL_INVALID_VALUE);
        return;
    }
    if (context->fullscreen || context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (!FlushScreenVertices(context) || !SubmitAndWait(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (width == (GLsizei)context->width &&
        height == (GLsizei)context->height)
        return;

    ZeroMemory(&newSurface, sizeof(newSurface));
    ZeroMemory(&newDepthSurface, sizeof(newDepthSurface));
    storageWidth = ((ULONG)width + 31UL) & ~31UL;
    newBitmap = AllocateColorBitMap(storageWidth, (ULONG)height,
                                    context->pixelDepth);
    newSurface.Size = sizeof(newSurface);
    /* Import the color surface first so the depth allocation can be placed
     * around it (see AllocateDepthBitMap). */
    if (!newBitmap ||
        !Radeon3DImportBitMap(context->device, newBitmap, &newSurface))
        goto fail;
    if (!AllocateDepthStorage(context->device, context->radeonCaps,
                              (ULONG)width, (ULONG)height, &newDepthSurface,
                              &newDepthBitmap, &newSurface, 1UL) ||
        newSurface.Format != context->colorFormat ||
        newSurface.Width < (ULONG)width ||
        newSurface.Height != (ULONG)height ||
        newDepthSurface.Format != RADEON3D_FORMAT_R5G6B5PC ||
        newDepthSurface.Width < (ULONG)width ||
        newDepthSurface.Height != (ULONG)height)
        goto fail;

    oldWidth = context->width;
    oldHeight = context->height;
    oldBitmap = context->bitmap;
    oldDepthBitmap = context->depthBitmap;
    oldSurface = context->surfaces[0];
    oldDepthSurface = context->depthSurface;
    WindowLimits(context->window,
                 64 + context->window->BorderLeft + context->window->BorderRight,
                 64 + context->window->BorderTop + context->window->BorderBottom,
                 context->screen->Width, context->screen->Height);
    SizeWindow(context->window,
               width + context->window->BorderLeft +
                   context->window->BorderRight - context->window->Width,
               height + context->window->BorderTop +
                   context->window->BorderBottom - context->window->Height);
    WaitTOF();
    context->bitmap = newBitmap;
    context->depthBitmap = newDepthBitmap;
    context->surfaces[0] = newSurface;
    context->depthSurface = newDepthSurface;
    context->source.BitMap = newBitmap;
    context->width = (ULONG)width;
    context->height = (ULONG)height;

    if (context->viewportX == 0 && context->viewportY == 0 &&
        context->viewportWidth == oldWidth &&
        context->viewportHeight == oldHeight) {
        context->viewportWidth = (ULONG)width;
        context->viewportHeight = (ULONG)height;
    } else {
        if (context->viewportX >= (ULONG)width)
            context->viewportX = (ULONG)width - 1;
        if (context->viewportY >= (ULONG)height)
            context->viewportY = (ULONG)height - 1;
        if (context->viewportWidth >
            (ULONG)width - context->viewportX)
            context->viewportWidth = (ULONG)width - context->viewportX;
        if (context->viewportHeight >
            (ULONG)height - context->viewportY)
            context->viewportHeight = (ULONG)height - context->viewportY;
    }
    if (context->scissorX == 0 && context->scissorY == 0 &&
        context->scissorWidth == (GLsizei)oldWidth &&
        context->scissorHeight == (GLsizei)oldHeight) {
        context->scissorWidth = width;
        context->scissorHeight = height;
    } else {
        if (context->scissorX < 0) {
            context->scissorWidth =
                context->scissorWidth <= -context->scissorX
                    ? 0 : context->scissorWidth + context->scissorX;
            context->scissorX = 0;
        }
        if (context->scissorY < 0) {
            context->scissorHeight =
                context->scissorHeight <= -context->scissorY
                    ? 0 : context->scissorHeight + context->scissorY;
            context->scissorY = 0;
        }
        if (context->scissorX >= width) {
            context->scissorX = width;
            context->scissorWidth = 0;
        } else if (context->scissorWidth > width - context->scissorX) {
            context->scissorWidth = width - context->scissorX;
        }
        if (context->scissorY >= height) {
            context->scissorY = height;
            context->scissorHeight = 0;
        } else if (context->scissorHeight > height - context->scissorY) {
            context->scissorHeight = height - context->scissorY;
        }
    }
    BumpSemanticStateSerial(context);
    Radeon3DReleaseSurface(context->device, &oldSurface);
    Radeon3DReleaseSurface(context->device, &oldDepthSurface);
    p96FreeBitMap(oldBitmap);
    /* NULL when the driver owns the depth storage; releasing the surface
     * above already returned it to the aux pool. */
    if (oldDepthBitmap)
        p96FreeBitMap(oldDepthBitmap);
    return;

fail:
    if (newSurface.Handle)
        Radeon3DReleaseSurface(context->device, &newSurface);
    if (newDepthSurface.Handle)
        Radeon3DReleaseSurface(context->device, &newDepthSurface);
    if (newBitmap) p96FreeBitMap(newBitmap);
    if (newDepthBitmap) p96FreeBitMap(newDepthBitmap);
    SetError(context, GL_OUT_OF_MEMORY);
}

static void CopyWorstFrame(struct WorstFrameSlot *destination,
                           const struct WorstFrameSlot *source)
{
    destination->frameNumber = source->frameNumber;
    destination->ticks = source->ticks;
    destination->serializeTicks = source->serializeTicks;
    destination->executeTicks = source->executeTicks;
    destination->waitTicks = source->waitTicks;
    destination->presentTicks = source->presentTicks;
    destination->residencyTicks = source->residencyTicks;
}

/* Close out the current frame's perf window: fold the phase deltas into
 * the rank-ordered slowest-frame table (10 slots, rank 0 = worst) and
 * restart the frame clocks. Replaces the plain frames++ the present
 * paths used to do -- it subsumes the increment (ported from the
 * PiStorm_SDK_v10 lineage). */
void RecordPerfFrame(struct MiniGLR200Context *context)
{
    struct PerfMetrics *perf = &context->perf;
    struct WorstFrameSlot frame;
    ULONG now = PerfClock();
    ULONG insert = 0;
    ULONG move;

    frame.frameNumber = perf->frames + 1UL;
    frame.ticks = now - perf->frameStartTicks;
    frame.serializeTicks = perf->serializeTicks - perf->frameSerializeBase;
    frame.executeTicks = perf->executeTicks - perf->frameExecuteBase;
    frame.waitTicks = perf->waitTicks - perf->frameWaitBase;
    frame.presentTicks = perf->presentTicks - perf->framePresentBase;
    frame.residencyTicks =
        perf->textureResidencyTicks - perf->frameResidencyBase;

    perf->frames = frame.frameNumber;
    perf->frameStartTicks = now;
    perf->frameSerializeBase = perf->serializeTicks;
    perf->frameExecuteBase = perf->executeTicks;
    perf->frameWaitBase = perf->waitTicks;
    perf->framePresentBase = perf->presentTicks;
    perf->frameResidencyBase = perf->textureResidencyTicks;

    while (insert < perf->worstFrameCount &&
           perf->worstFrames[insert].ticks >= frame.ticks)
        ++insert;
    if (insert >= 10UL)
        return;
    if (perf->worstFrameCount < 10UL)
        ++perf->worstFrameCount;
    move = perf->worstFrameCount - 1UL;
    while (move > insert) {
        CopyWorstFrame(&perf->worstFrames[move],
                       &perf->worstFrames[move - 1UL]);
        --move;
    }
    CopyWorstFrame(&perf->worstFrames[insert], &frame);
}

void Lib_mglSwitchDisplay(void)
{
    struct MiniGLR200Context *context = CurrentContext();
#if R200_PPC_FRONTEND
    ULONG perfStart = 0, executeBefore = 0, waitBefore = 0;
    if (!context)
        return;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (context->perfEnabled) {
        executeBefore = context->perf.executeTicks;
        waitBefore = context->perf.waitTicks;
        perfStart = PerfClock();
    }
    {
        /* Same short-circuit order as the && chain below, but recording
         * which stage failed: the failure path is otherwise completely
         * silent and the frame counters never advance (the PPCPERF_LIVE
         * gate then never fires), so logs show a wedge with no cause. */
        static ULONG sdEnters, sdFails;
        BOOL flushOk, submitOk, barrierOk;
        ++sdEnters;
        if (sdEnters <= 8UL || (sdEnters & 511UL) == 0UL)
            R200PPCConsolePrintf("RING sd enter n=%lu\n",
                                 (unsigned long)sdEnters);
        R200PPCTraceUL("sd enter n", sdEnters);
        flushOk = FlushScreenVertices(context);
        submitOk = flushOk ? SubmitAndWait(context) : FALSE;
        barrierOk = (flushOk && submitOk) ?
            R200PPCPlatformBarrier(context, TRUE) : FALSE;
        if (!barrierOk) {
            ++sdFails;
            if (sdFails <= 16UL || (sdFails & 1023UL) == 0UL) {
                R200PPCTraceUL("sd FAIL n", sdFails);
                R200PPCTraceUL2("sd FAIL flush", flushOk ? 1UL : 0UL,
                                "submit", submitOk ? 1UL : 0UL);
            }
        }
        if (!barrierOk) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
    }
    if (context->perfEnabled) {
        ULONG elapsed = PerfClock() - perfStart;
        ULONG nested = (context->perf.executeTicks - executeBefore) +
                       (context->perf.waitTicks - waitBefore);
        context->perf.presentTicks += elapsed >= nested ? elapsed - nested : 0;
        RecordPerfFrame(context);
    }
    return;
#else
    ULONG perfStart = 0, executeBefore = 0, waitBefore = 0;

    if (!context)
        return;
    if (context->insideBegin) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    if (context->perfEnabled) {
        executeBefore = context->perf.executeTicks;
        waitBefore = context->perf.waitTicks;
        perfStart = PerfClock();
    }
    if (!FlushScreenVertices(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    /* The frame about to be submitted targets the drawing buffer; it must
     * have left the display. Steady state always satisfies this -- the bit
     * was pumped when the buffer retired -- so this only waits in the rare
     * window right after a rotation. */
    if (context->fullscreen && !context->singleBuffer) {
        if (!WaitTargetBufferSafe(context)) {
            SetError(context, GL_INVALID_OPERATION);
            return;
        }
    }
    if (!SubmitAndWait(context)) {
        SetError(context, GL_INVALID_OPERATION);
        return;
    }
    /* The drain above retired every outstanding fence; surfaces parked by
     * DeleteTextures/TexImage2D can now go back to the service, and so can
     * segments parked by async rotation. */
    ReapDeferredSurfaces(context);
    ReapDeferredSegments(context);
    if (!context->fullscreen) {
        ClipBlit(&context->source, 0, 0, context->window->RPort,
                 context->window->BorderLeft, context->window->BorderTop,
                 context->width, context->height, 0xc0);
        WaitBlit();
        if (context->perfEnabled) {
            ULONG elapsed = PerfClock() - perfStart;
            ULONG nested =
                (context->perf.executeTicks - executeBefore) +
                (context->perf.waitTicks - waitBefore);
            context->perf.presentTicks +=
                elapsed >= nested ? elapsed - nested : 0;
            RecordPerfFrame(context);
        }
        return;
    }
    {
        /* Fullscreen present. The frame just submitted targets the
         * drawing buffer. At most one screen-buffer switch may be
         * outstanding: complete it first (with sync enabled, wait for it;
         * the wait is bounded exactly like the old TOF-poll, but foreign
         * messages can no longer abort it), then request the switch that
         * displays this frame and rotate the drawing role onto the buffer
         * that just retired. With sync disabled and the previous switch
         * still outstanding, skip the flip: the drawing buffer is neither
         * displayed nor pending, so the next frame simply overwrites it
         * and the render loop never blocks on the display -- latest
         * completed frame wins. */
        ULONG drawn = context->drawingBuffer;
        ULONG rotateDisplayed = 0;

        if (context->singleBuffer) {
            /* Single-buffer mode: every frame renders straight into the
             * visible bitmap; nothing to present. The scanout race cannot
             * be eliminated, but with sync enabled the next frame's render
             * is pinned to the vblank so the race window sits at the top
             * of the screen instead of drifting band artifacts anywhere. */
            if (context->syncEnabled)
                WaitTOF();
            if (context->perfEnabled) {
                ULONG elapsed = PerfClock() - perfStart;
                ULONG nested =
                    (context->perf.executeTicks - executeBefore) +
                    (context->perf.waitTicks - waitBefore);
                context->perf.presentTicks +=
                    elapsed >= nested ? elapsed - nested : 0;
                RecordPerfFrame(context);
            }
            return;
        }
        if (!context->presentFlip &&
            (context->presentBlit || !context->syncEnabled)) {
            /* Present by copying the rendered buffer onto the visible
             * screen bitmap. ChangeScreenBuffer costs a full vblank wait
             * (~22-27 ms measured) per call on P96, so every present via
             * the flip path caps the pipeline far below what the GPU can
             * sustain; the copy window is ~2-4 ms against a 20 ms vblank.
             * MINIGL_PRESENT=BLIT forces this mode; it is also the default
             * with sync disabled. Tear-free clients enable sync and take
             * the flip path. */
            struct RastPort src;
            InitRastPort(&src);
            src.BitMap =
                context->screenBuffers[drawn]->sb_BitMap;
            ClipBlit(&src, 0, 0, &context->screen->RastPort, 0, 0,
                     context->width, context->height, 0xc0);
            WaitBlit();
            if (context->perfEnabled) {
                ULONG elapsed = PerfClock() - perfStart;
                ULONG nested =
                    (context->perf.executeTicks - executeBefore) +
                    (context->perf.waitTicks - waitBefore);
                context->perf.presentTicks +=
                    elapsed >= nested ? elapsed - nested : 0;
                RecordPerfFrame(context);
            }
            return;
        }
        {
            BOOL completed = CompletePendingFlip(context,
                                                 context->syncEnabled);

            if (completed) {
                if (!ChangeBuffer(context,
                                  context->screenBuffers[drawn])) {
                    dbg_puts("[mgl] flip=changefail\n");
                    SetError(context, GL_INVALID_OPERATION);
                    return;
                }
                context->pendingValid = TRUE;
                context->pendingBuffer = drawn;
                context->bufferSafe &= ~(1UL << drawn);
                context->bufferDisplayed &= ~(1UL << drawn);
                /* Sum of all buffer indices; the new drawing buffer is the
                 * one that is neither displayed, pending, nor just
                 * flipped. Three buffers only: with two buffers the sum
                 * degenerates to the just-flipped buffer (rendering into
                 * the displayed surface -> visible blink every second
                 * frame). Double buffering instead renders into the
                 * buffer that retires when this switch completes -- the
                 * one on screen right now -- and the safe-mask wait gates
                 * rendering until that retirement is observed. */
                if (context->screenBufferCount >= 3UL) {
                    rotateDisplayed = context->displayedBuffer;
                    context->drawingBuffer =
                        context->screenBufferCount *
                            (context->screenBufferCount - 1UL) / 2UL -
                        rotateDisplayed - drawn;
                } else {
                    context->drawingBuffer = context->displayedBuffer;
                }
                /* The rotation changes only the color target (header[2]);
                 * the depth surface is shared by every screen buffer. Patch
                 * the cached capture and the pending draw in place instead
                 * of bumping the semantic serial: a full
                 * CapturePendingDrawState rebuild here cost ~20 ms per frame
                 * on the 68060 and made fullscreen 22% slower than windowed.
                 * Front-buffer rendering targets the displayed buffer, so it
                 * still takes the full rebuild. */
                if (context->drawBuffer == GL_FRONT) {
                    BumpSemanticStateSerial(context);
                } else {
                    ULONG handle = (ULONG)
                        context->surfaces[context->drawingBuffer].Handle;
                    if (context->captureCacheValid)
                        context->captureCache.header[2] = handle;
                    if (context->pendingDrawValid)
                        context->pendingDraw.header[2] = handle;
                }
                if (context->syncEnabled &&
                    !CompletePendingFlip(context, TRUE)) {
                    SetError(context, GL_INVALID_OPERATION);
                    return;
                }
                /* CompletePendingFlip() retargets the drawing buffer onto
                 * the buffer that just retired -- the correct choice for
                 * two buffers, but it discards the three-buffer rotation
                 * picked above. Re-apply it once the switch is done. */
                if (context->screenBufferCount >= 3UL) {
                    context->drawingBuffer =
                        context->screenBufferCount *
                            (context->screenBufferCount - 1UL) / 2UL -
                        rotateDisplayed - drawn;
                }
            }
        }
    }
    if (context->perfEnabled) {
        ULONG elapsed = PerfClock() - perfStart;
        ULONG nested =
            (context->perf.executeTicks - executeBefore) +
            (context->perf.waitTicks - waitBefore);
        context->perf.presentTicks += elapsed >= nested ? elapsed - nested : 0;
        RecordPerfFrame(context);
    }
#endif
}

/*
 * No vector table here. Under the V10 dispatch ABI the library exports a
 * single LVO; library/dispatch_table_r200.c binds the Lib_* entry points
 * above into MGLDispatchTable.
 */
