/*
 * eg_device.c -- the MiniGL frontend's device service (the radeon3d.h
 * calls: open, memory segments, surfaces, bitmap import, indirect
 * dispatch, fences) on Evergreen.chip's 3D hand-off API
 * (evergreen-p96-driver include/evergreen3d.h).
 *
 * Memory: the chip hands the 3D client a 2 MB CPU-visible scratch block
 * and all VRAM above the CPU window (GPU-only). Scratch holds the shader
 * cache, the emitter's three data streams and the segments the frontend
 * asks for (its command buffers); depth buffers come from GPU-only VRAM.
 * Picasso96 bitmaps (render targets, textures) are imported by address.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

#include "radeon3d.h"
#include "radeon3d_records.h"
#include "p96_bitmap_api.h"
#include "eg_emit.h"

struct Library *Eg3DBase;
#include "evergreen3d.h"

/* scratch layout (offsets inside the scratch block) */
#define SHADER_BYTES    0x10000UL
#define STREAM_BYTES    0x40000UL       /* per submit slot */
#define SEG_FIRST       (SHADER_BYTES + EG_EMIT_SLOTS * STREAM_BYTES)
#define MAX_SEGS        16
#define MAX_AUX         8

struct EgSeg { BOOL used; ULONG off, bytes; };
struct EgAux { BOOL used; ULONG off, bytes; BOOL gpuOnly; };

struct Radeon3DDevice {
    struct Eg3DInfo info;
    struct EgSeg seg[MAX_SEGS];
    struct EgAux aux[MAX_AUX];
    ULONG gpuOnlyTop;
    ULONG lastFence;
    ULONG presentOff;           /* scratch: two halves of PRESENT_HALF bytes */
    ULONG presentFence[2];
    ULONG presentNext;
};

#define PRESENT_HALF    0x2000UL
#define PRESENT_DWORDS  ((PRESENT_HALF - 256UL) / 4UL)
static ULONG PresentWords[PRESENT_DWORDS];

static struct Radeon3DDevice Device;
static BOOL DeviceOpen;
static BOOL ScratchAlloc(ULONG bytes, ULONG *offOut);

static ULONG Caps(void)
{
    return RADEON3D_CAP_CP_READY | RADEON3D_CAP_FENCES | RADEON3D_CAP_BITMAP_IMPORT |
           RADEON3D_CAP_AUX_SURFACES | RADEON3D_CAP_INDIRECT_DISPATCH |
           RADEON3D_CAP_INDIRECT_RENDER | RADEON3D_CAP_MULTI_FENCE;
}

static void FillInfo(struct Radeon3DInfo *info)
{
    ULONG size;

    if (!info)
        return;
    size = info->Size && info->Size < sizeof(*info) ? info->Size : sizeof(*info);
    memset(info, 0, size);
    info->Size = size;
    info->Version = RADEON3D_IFACE_VERSION;
    info->Generation = 1;
    info->DeviceId = Device.info.DeviceId;
    info->Caps = Caps();
    info->InstalledVram = Device.info.VramSize;
    info->Picasso96Vram = Device.info.ApertureSize;
    info->MaxBatchDwords = RADEON3D_MAX_BATCH_DWORDS;
}

struct Radeon3DDevice *Radeon3DOpen(ULONG requestedVersion, struct Radeon3DInfo *info)
{
    (void)requestedVersion;
    if (DeviceOpen) {
        /* single-context library: a previous owner that died without
         * mglDeleteContext (a client stopped with Ctrl-C) left it open;
         * let its last work finish and take the device over */
        if (Device.lastFence)
            (void)Eg3DWait(Device.lastFence);
        DeviceOpen = FALSE;
    }
    if (!Eg3DBase)
        Eg3DBase = OpenLibrary((CONST_STRPTR)"Evergreen.chip", 1);
    if (!Eg3DBase)
        return NULL;
    memset(&Device, 0, sizeof(Device));
    Device.info.StructSize = sizeof(Device.info);
    if (!Eg3DGetInfo(&Device.info) || Device.info.ScratchSize < SEG_FIRST + 0x60000UL) {
        CloseLibrary(Eg3DBase);
        Eg3DBase = NULL;
        return NULL;
    }
    Device.gpuOnlyTop = Device.info.GpuOnlyOffset;
    if (!ScratchAlloc(2 * PRESENT_HALF, &Device.presentOff)) {
        CloseLibrary(Eg3DBase);
        Eg3DBase = NULL;
        return NULL;
    }
    Device.seg[MAX_SEGS - 1].used = TRUE;           /* keep it out of AllocSegment */
    Device.seg[MAX_SEGS - 1].off = Device.presentOff;
    Device.seg[MAX_SEGS - 1].bytes = 2 * PRESENT_HALF;
    EgEmitInit(Device.info.Aperture, Device.info.VramGpuBase,
               Device.info.ScratchOffset, SHADER_BYTES,
               Device.info.ScratchOffset + SHADER_BYTES, STREAM_BYTES);
    DeviceOpen = TRUE;
    FillInfo(info);
    return &Device;
}

void Radeon3DClose(struct Radeon3DDevice *device)
{
    if (device != &Device || !DeviceOpen)
        return;
    if (Device.lastFence)
        (void)Eg3DWait(Device.lastFence);
    DeviceOpen = FALSE;
    CloseLibrary(Eg3DBase);
    Eg3DBase = NULL;
}

BOOL Radeon3DGetInfo(struct Radeon3DDevice *device, struct Radeon3DInfo *info)
{
    if (device != &Device || !DeviceOpen)
        return FALSE;
    FillInfo(info);
    return TRUE;
}

/* CRTC 0 (the one Evergreen.chip scans the screen out with): little-endian
 * MMIO registers. */
static ULONG CrtcRead(ULONG reg)
{
    ULONG v = *(volatile ULONG *)((UBYTE *)Device.info.Registers + reg);
    return (v >> 24) | ((v >> 8) & 0xff00UL) | ((v << 8) & 0xff0000UL) | (v << 24);
}

/* Until the start of a vertical blank (bounded: ~2 us per read, at most
 * two frames at 20 Hz). The present copy, ~0.2 ms top to bottom, then runs
 * ahead of the beam: no tear line. Polled by the CPU, not the CP: a CP
 * register wait that never completes would take the engine down. */
static void WaitVblank(void)
{
    ULONG i;

    if (!Device.info.Registers || !(CrtcRead(0x6e70) & 1))     /* CRTC_CONTROL master enable */
        return;
    for (i = 0; i < 25000UL && (CrtcRead(0x6e8c) & 1); ++i)    /* CRTC_STATUS V_BLANK */
        ;
    for (i = 0; i < 25000UL && !(CrtcRead(0x6e8c) & 1); ++i)
        ;
}

/* The surface CRTC 0 scans out, as a GPU address, if it has src's pitch
 * (else dst's): Evergreen.chip moves the scanout on SetPanning, so with
 * more than two screen buffers the screen's own bitmap need not be the one
 * on the monitor. */
static ULONG ScanoutTarget(const struct Radeon3DSurface *src,
                           const struct Radeon3DSurface *dst, ULONG bpp)
{
    ULONG addr, pitch;

    if (!Device.info.Registers || !(CrtcRead(0x6e70) & 1) || !(CrtcRead(0x6800) & 1))
        return dst->GpuAddress;
    addr = CrtcRead(0x6810) & ~0xffUL;                         /* GRPH_PRIMARY_SURFACE_ADDRESS */
    pitch = (CrtcRead(0x6818) & 0x7fffUL) * bpp;               /* GRPH_PITCH, pixels */
    if (addr < Device.info.VramGpuBase ||
        addr - Device.info.VramGpuBase >= Device.info.VramSize || pitch != src->Pitch)
        return dst->GpuAddress;
    return addr;
}

/* The frame in src onto the visible dst with the 3D engine (EgEmitPresent):
 * Picasso96's screen-buffer flip does not move this card's scanout, and its
 * blit path is not safe right after 3D work. Ordered behind the frame on
 * the one ring. */
BOOL EgPresent(struct Radeon3DDevice *device, const struct Radeon3DSurface *src,
               const struct Radeon3DSurface *dst, ULONG width, ULONG height,
               BOOL waitVblank)
{
    ULONG half, off, n, bpp, i, fence;
    volatile ULONG *ib;

    if (device != &Device || !DeviceOpen || !src || !dst || src->Pitch != dst->Pitch)
        return FALSE;
    bpp = (src->Format == RADEON3D_FORMAT_R5G6B5PC || src->Format == EG_FORMAT_R5G6B5) ? 2 : 4;
    half = Device.presentNext;
    Device.presentNext ^= 1;
    if (Device.presentFence[half] && !Eg3DDone(Device.presentFence[half]))
        (void)Eg3DWait(Device.presentFence[half]);
    off = Device.info.ScratchOffset + Device.presentOff + half * PRESENT_HALF;
    n = EgEmitPresent(PresentWords, PRESENT_DWORDS,
                      (APTR)((UBYTE *)Device.info.Aperture + off + PRESENT_HALF - 256),
                      Device.info.VramGpuBase + off + PRESENT_HALF - 256,
                      src->GpuAddress, ScanoutTarget(src, dst, bpp), src->Pitch, src->Format,
                      width, height, FALSE);
    if (!n)
        return FALSE;
    ib = (volatile ULONG *)((UBYTE *)Device.info.Aperture + off);
    for (i = 0; i < n; ++i)
        ib[i] = PresentWords[i];
    (void)ib[n - 1];
    if (waitVblank && Device.lastFence)
        /* the frame drawn, the engine idle: the copy then starts at once */
        (void)Eg3DWait(Device.lastFence);
    if (!Eg3DLock())
        return FALSE;
    /* no task switch between seeing the blank start and the kick: a late
     * start could leave the copy finishing after the blank (interrupts
     * still run; the wait is at most a frame) */
    if (waitVblank) {
        Forbid();
        WaitVblank();
    }
    fence = Eg3DSubmit(off, n);
    if (waitVblank)
        Permit();
    Eg3DUnlock();
    if (!fence)
        return FALSE;
    Device.presentFence[half] = fence;
    Device.lastFence = fence;
    return TRUE;
}

/* first fit over the scratch block above the emitter's regions */
static BOOL ScratchAlloc(ULONG bytes, ULONG *offOut)
{
    ULONG off = SEG_FIRST, i;
    BOOL moved = TRUE;

    bytes = (bytes + 255UL) & ~255UL;
    while (moved) {
        moved = FALSE;
        for (i = 0; i < MAX_SEGS; ++i)
            if (Device.seg[i].used && off < Device.seg[i].off + Device.seg[i].bytes &&
                Device.seg[i].off < off + bytes) {
                off = Device.seg[i].off + Device.seg[i].bytes;
                moved = TRUE;
            }
        for (i = 0; i < MAX_AUX; ++i)
            if (Device.aux[i].used && !Device.aux[i].gpuOnly &&
                off < Device.aux[i].off + Device.aux[i].bytes &&
                Device.aux[i].off < off + bytes) {
                off = Device.aux[i].off + Device.aux[i].bytes;
                moved = TRUE;
            }
    }
    if (off + bytes > Device.info.ScratchSize)
        return FALSE;
    *offOut = off;
    return TRUE;
}

BOOL Radeon3DAllocSegment(struct Radeon3DDevice *device, ULONG bytes,
                          struct Radeon3DSegment *segment)
{
    ULONG i, off;

    if (device != &Device || !segment || !bytes)
        return FALSE;
    for (i = 0; i < MAX_SEGS && Device.seg[i].used; ++i)
        ;
    if (i == MAX_SEGS || !ScratchAlloc(bytes, &off))
        return FALSE;
    Device.seg[i].used = TRUE;
    Device.seg[i].off = off;
    Device.seg[i].bytes = (bytes + 255UL) & ~255UL;
    segment->Size = sizeof(*segment);
    segment->Version = RADEON3D_SEGMENT_VERSION;
    segment->Id = i + 1;
    segment->CpuAddress = (APTR)((UBYTE *)Device.info.Aperture + Device.info.ScratchOffset + off);
    segment->GpuAddress = Device.info.VramGpuBase + Device.info.ScratchOffset + off;
    segment->Bytes = bytes;
    return TRUE;
}

BOOL Radeon3DFreeSegment(struct Radeon3DDevice *device, ULONG segmentId)
{
    if (device != &Device || !segmentId || segmentId > MAX_SEGS)
        return FALSE;
    Device.seg[segmentId - 1].used = FALSE;
    return TRUE;
}

/* Depth (the frontend asks for R5G6B5PC: 16 bits a pixel) lives in
 * GPU-only VRAM, 1D-tiled: pitch and height in whole 8 x 8 tiles. Other
 * formats are CPU-visible scratch (the frontend's vertex stores). */
BOOL Radeon3DAllocSurface(struct Radeon3DDevice *device, ULONG width, ULONG height,
                          ULONG format, struct Radeon3DSurface *surface)
{
    ULONG i, bpp = format == RADEON3D_FORMAT_R5G6B5PC ? 2 : 4, pitch, rows, bytes, off;
    BOOL gpuOnly = format == RADEON3D_FORMAT_R5G6B5PC;

    if (device != &Device || !surface || !width || !height || width > 4096 || height > 4096)
        return FALSE;
    for (i = 0; i < MAX_AUX && Device.aux[i].used; ++i)
        ;
    if (i == MAX_AUX)
        return FALSE;
    pitch = ((width + 7UL) & ~7UL) * bpp;
    rows = (height + 7UL) & ~7UL;
    bytes = (pitch * rows + 0xffffUL) & ~0xffffUL;
    if (gpuOnly) {
        if (Device.gpuOnlyTop + bytes > Device.info.GpuOnlyOffset + Device.info.GpuOnlySize)
            return FALSE;
        off = Device.gpuOnlyTop;
        Device.gpuOnlyTop += bytes;
    } else if (ScratchAlloc(bytes, &off)) {
        off += Device.info.ScratchOffset;
    } else
        return FALSE;
    Device.aux[i].used = TRUE;
    Device.aux[i].off = gpuOnly ? off : off - Device.info.ScratchOffset;
    Device.aux[i].bytes = bytes;
    Device.aux[i].gpuOnly = gpuOnly;
    surface->Size = sizeof(*surface);
    surface->Version = RADEON3D_SURFACE_VERSION;
    surface->Generation = 1;
    surface->CpuAddress = gpuOnly ? NULL : (APTR)((UBYTE *)Device.info.Aperture + off);
    surface->GpuAddress = Device.info.VramGpuBase + off;
    surface->Pitch = pitch;
    surface->Width = width;
    surface->Height = height;
    surface->Format = format;
    surface->Handle = (APTR)&Device.aux[i];
    return TRUE;
}

BOOL Radeon3DImportBitMap(struct Radeon3DDevice *device, struct BitMap *bitmap,
                          struct Radeon3DSurface *surface)
{
    ULONG mem, offset, format;

    if (device != &Device || !bitmap || !surface ||
        !p96GetBitMapAttr(bitmap, P96_BITMAP_IS_P96))
        return FALSE;
    mem = p96GetBitMapAttr(bitmap, P96_BITMAP_MEMORY);
    if (mem < (ULONG)Device.info.Aperture)
        return FALSE;
    offset = mem - (ULONG)Device.info.Aperture;
    if (offset >= Device.info.ApertureSize || (offset & 255UL))
        return FALSE;
    switch (p96GetBitMapAttr(bitmap, P96_BITMAP_RGB_FORMAT)) {
    case RGBFB_R5G6B5PC: format = RADEON3D_FORMAT_R5G6B5PC; break;
    case RGBFB_B8G8R8A8: format = RADEON3D_FORMAT_B8G8R8A8; break;
    case RGBFB_R5G6B5:   format = EG_FORMAT_R5G6B5; break;
    case RGBFB_A8R8G8B8: format = EG_FORMAT_A8R8G8B8; break;
    case RGBFB_CLUT:     format = RADEON3D_FORMAT_CLUT8; break;
    default: return FALSE;
    }
    surface->Size = sizeof(*surface);
    surface->Version = RADEON3D_SURFACE_VERSION;
    surface->Generation = 1;
    surface->CpuAddress = (APTR)mem;
    surface->GpuAddress = Device.info.VramGpuBase + offset;
    surface->Pitch = p96GetBitMapAttr(bitmap, P96_BITMAP_BYTES_PER_ROW);
    surface->Width = p96GetBitMapAttr(bitmap, P96_BITMAP_WIDTH);
    surface->Height = p96GetBitMapAttr(bitmap, P96_BITMAP_HEIGHT);
    surface->Format = format;
    surface->Handle = (APTR)bitmap;
    return TRUE;
}

void Radeon3DReleaseSurface(struct Radeon3DDevice *device, struct Radeon3DSurface *surface)
{
    ULONG i;

    if (device != &Device || !surface)
        return;
    for (i = 0; i < MAX_AUX; ++i)
        if (surface->Handle == (APTR)&Device.aux[i] && Device.aux[i].used) {
            Device.aux[i].used = FALSE;
            /* GPU-only depth is a bump allocation: give back the top */
            if (Device.aux[i].gpuOnly &&
                Device.aux[i].off + Device.aux[i].bytes == Device.gpuOnlyTop)
                Device.gpuOnlyTop = Device.aux[i].off;
        }
    surface->Handle = NULL;
}

BOOL Radeon3DDispatchIndirect(struct Radeon3DDevice *device,
                              const struct Radeon3DIndirect *indirect, ULONG *fenceOut)
{
    struct EgSeg *s;
    ULONG fence;

    if (fenceOut)
        *fenceOut = 0;
    if (device != &Device || !indirect || !indirect->SegmentId ||
        indirect->SegmentId > MAX_SEGS || !indirect->DwordCount)
        return FALSE;
    s = &Device.seg[indirect->SegmentId - 1];
    if (!s->used || indirect->ByteOffset + indirect->DwordCount * 4 > s->bytes)
        return FALSE;
    if (!Eg3DLock())
        return FALSE;
    fence = Eg3DSubmit(Device.info.ScratchOffset + s->off + indirect->ByteOffset,
                       indirect->DwordCount);
    Eg3DUnlock();
    if (!fence)
        return FALSE;
    Device.lastFence = fence;
    if (fenceOut)
        *fenceOut = fence;
    return TRUE;
}

BOOL Radeon3DTestFence(struct Radeon3DDevice *device, ULONG fence)
{
    return device == &Device && (!fence || Eg3DDone(fence));
}

BOOL Radeon3DWaitFence(struct Radeon3DDevice *device, ULONG fence, ULONG timeoutMs)
{
    (void)timeoutMs;
    return device == &Device && (!fence || Eg3DWait(fence));
}

/* not offered (Caps) */
BOOL Radeon3DSubmit(struct Radeon3DDevice *d, const ULONG *c, ULONG n, ULONG f, ULONG *o)
{ (void)d; (void)c; (void)n; (void)f; if (o) *o = 0; return FALSE; }
BOOL Radeon3DInvalidateForTest(struct Radeon3DDevice *d) { (void)d; return FALSE; }
BOOL Radeon3DSubmitFence(struct Radeon3DDevice *d, ULONG *o) { (void)d; if (o) *o = 0; return FALSE; }
BOOL Radeon3DAcquireLease(struct Radeon3DDevice *d, struct Radeon3DLease *l) { (void)d; (void)l; return FALSE; }
BOOL Radeon3DReleaseLease(struct Radeon3DDevice *d, ULONG f) { (void)d; (void)f; return FALSE; }
BOOL Radeon3DHeartbeatLease(struct Radeon3DDevice *d) { (void)d; return FALSE; }
