#ifndef LIBRARIES_MINIGL_R200_BRIDGE_H
#define LIBRARIES_MINIGL_R200_BRIDGE_H

#include <exec/types.h>

/* Matching-tree private bridge between the 68k R200 backend and its PPC host.
 * Each descriptor version has a distinct query so an older caller is never
 * overrun by a larger backend structure. The tokens are opaque outside the
 * 68k host and must not be dereferenced by the PPC frontend. */
#define MGL_R200_PRIVATE_BRIDGE_V1_QUERY 0x4d475001UL
#define MGL_R200_PRIVATE_BRIDGE_V1_VERSION 1UL
#define MGL_R200_PRIVATE_BRIDGE_COMPACT_TCL (1UL << 0)
#define MGL_R200_PRIVATE_BRIDGE_V2_QUERY 0x4d475005UL
#define MGL_R200_PRIVATE_BRIDGE_V2_VERSION 2UL
#define MGL_R200_PRIVATE_TEXTURE_V1_QUERY 0x4d475002UL
#define MGL_R200_PRIVATE_TEXTURE_V1_VERSION 1UL
/* Streaming-segment lease through the backend's Radeon device, so the
 * bridge host never opens a second Radeon3D device. The caller sets
 * `bytes` before the lease query; the backend owns the segment lifetime
 * table and frees leftovers at context destruction. */
#define MGL_R200_PRIVATE_SEGMENT_V1_QUERY 0x4d475003UL
#define MGL_R200_PRIVATE_SEGMENT_V1_VERSION 1UL
#define MGL_R200_PRIVATE_SEGMENT_FREE_V1_QUERY 0x4d475004UL
#define MGL_R200_PRIVATE_SEGMENT_FREE_V1_VERSION 1UL
/* Trusted indirect dispatch. Segment zero is a valid lease, not a probe.
 * Probe explicitly after context creation; the service trusts packet contents,
 * so an extent check does NOT sandbox their register or VRAM references. */
#define MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY 0x4d475012UL
#define MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_VERSION 1UL
#define MGL_R200_PRIVATE_INDIRECT_PROBE (1UL << 31)
#define MGL_R200_PRIVATE_INDIRECT_SUPPORTED (1UL << 0)

/* Read-only metadata for a live surface owned by the current context. */
#define MGL_R200_PRIVATE_SURFACE_V1_QUERY 0x4d475013UL
/* Format flag on surface/present descriptors: the surface is rendered
 * macro-tiled (R200_COLOR_TILE_ENABLE) and must be read with tiling. */
#define MGL_R200_SURFACE_FORMAT_TILED 0x80000000UL
#define MGL_R200_PRIVATE_SURFACE_V1_VERSION 1UL

/* surfaceToken is the only input; all other fields are native-owned output.
 * Failure clears metadata, retaining version/structSize and the input token.
 * deviceEpoch alone cannot validate a surface after a driver reset. */
typedef struct MGLR200PrivateSurfaceV1 {
    ULONG version;
    ULONG structSize;
    ULONG surfaceToken;
    ULONG cpuAddress;
    ULONG gpuAddress;
    ULONG pitch;
    ULONG width;
    ULONG height;
    ULONG format;
    ULONG deviceEpoch;
    ULONG generation;
} MGLR200PrivateSurfaceV1;

typedef char MGLR200PrivateSurfaceV1SizeCheck[
    sizeof(MGLR200PrivateSurfaceV1) == 44 ? 1 : -1];

/* Bounded, context-independent query: writes exactly one GLint, never a
 * descriptor. Bits 31..8 identify texture-capability schema V1; bits 7..0
 * are capabilities. Unknown/older backends cannot pass the positive tag test.
 * No public dispatch ABI or library-version change is implied. */
#define MGL_R200_PRIVATE_TEXTURE_CAPS_V1_QUERY 0x4d47500cUL
#define MGL_R200_PRIVATE_TEXTURE_CAPS_V1_TAG 0x54580100UL
#define MGL_R200_PRIVATE_TEXTURE_CAPS_V1_TAG_MASK 0xffffff00UL
/* Native subimage can lazily back committed one-level BGRA direct textures;
 * V2 texture preflight is allocation-free for direct textures, V1 postquery
 * returns the new image-backed residency. No eager BEGIN/COMMIT pixel copy. */
#define MGL_R200_PRIVATE_TEXTURE_CAP_SUBIMAGE_DIRECT_BGRA (1UL << 0)
/* TEXDIRECT_BEGIN_V2 honours MGL_R200_TEXDIRECT_REQ_PARK_OLD and the
 * TEXDIRECT_REAP_V1 query exists. */
#define MGL_R200_PRIVATE_TEXTURE_CAP_DIRECT_PARK (1UL << 1)

typedef struct MGLR200PrivateBridgeV1 {
    ULONG version;
    ULONG structSize;
    ULONG deviceToken;
    ULONG colorSurfaceToken;
    ULONG depthSurfaceToken;
    ULONG width;
    ULONG height;
    ULONG radeonCaps;
    ULONG batchDwords;
    ULONG flags;
} MGLR200PrivateBridgeV1;

typedef char MGLR200PrivateBridgeV1SizeCheck[
    sizeof(MGLR200PrivateBridgeV1) == 40 ? 1 : -1];

/* V2 = V1 plus deviceEpoch, so ring consumers can detect that a recovery
 * invalidated the leased control-block segment. */
typedef struct MGLR200PrivateBridgeV2 {
    ULONG version;
    ULONG structSize;
    ULONG deviceToken;
    ULONG colorSurfaceToken;
    ULONG depthSurfaceToken;
    ULONG width;
    ULONG height;
    ULONG radeonCaps;
    ULONG batchDwords;
    ULONG flags;
    ULONG deviceEpoch;
} MGLR200PrivateBridgeV2;

typedef char MGLR200PrivateBridgeV2SizeCheck[
    sizeof(MGLR200PrivateBridgeV2) == 44 ? 1 : -1];

/* textureName is the input; all other fields are backend-owned output. */
typedef struct MGLR200PrivateTextureV1 {
    ULONG version;
    ULONG structSize;
    ULONG textureName;
    ULONG surfaceToken;
    ULONG width;
    ULONG height;
    ULONG definedLevels;
    ULONG levelCount;
    ULONG contentSerial;
    ULONG deviceEpoch;
} MGLR200PrivateTextureV1;

typedef char MGLR200PrivateTextureV1SizeCheck[
    sizeof(MGLR200PrivateTextureV1) == 40 ? 1 : -1];

/* Lease: input is `bytes`; all other fields are backend-owned output.
 * Free: input is `segmentId`; the query succeeds with no GL error when
 * the id was leased by this context. */
typedef struct MGLR200PrivateSegmentV1 {
    ULONG version;
    ULONG structSize;
    ULONG segmentId;
    ULONG cpuAddress;
    ULONG bytes;
    ULONG deviceEpoch;
} MGLR200PrivateSegmentV1;

typedef struct MGLR200PrivateSegmentFreeV1 {
    ULONG version;
    ULONG structSize;
    ULONG segmentId;
} MGLR200PrivateSegmentFreeV1;

typedef char MGLR200PrivateSegmentV1SizeCheck[
    sizeof(MGLR200PrivateSegmentV1) == 24 ? 1 : -1];

/* Indirect dispatch: inputs are segmentId/byteOffset/dwordCount and flags
 * (0 = dispatch, INDIRECT_PROBE = capability query with zero inputs); fenceOut
 * is the backend-owned retiring fence. The segment must be leased by this
 * context through the segment query. */
typedef struct MGLR200PrivateIndirectDispatchV1 {
    ULONG version;
    ULONG structSize;
    ULONG segmentId;
    ULONG byteOffset;
    ULONG dwordCount;
    ULONG flags;
    ULONG fenceOut;
} MGLR200PrivateIndirectDispatchV1;

typedef char MGLR200PrivateIndirectDispatchV1SizeCheck[
    sizeof(MGLR200PrivateIndirectDispatchV1) == 28 ? 1 : -1];

/* V2 = V1 plus the live surface's aperture mapping, so the 68k host can
 * hand a writable address to the PPC frontend for direct texture fills. */
#define MGL_R200_PRIVATE_TEXTURE_V2_QUERY 0x4d475006UL
#define MGL_R200_PRIVATE_TEXTURE_V2_VERSION 2UL

typedef struct MGLR200PrivateTextureV2 {
    ULONG version;
    ULONG structSize;
    ULONG textureName;
    ULONG surfaceToken;
    ULONG width;
    ULONG height;
    ULONG definedLevels;
    ULONG levelCount;
    ULONG contentSerial;
    ULONG deviceEpoch;
    ULONG surfaceCpuAddress;
    ULONG surfacePitch;
    ULONG surfaceFormat;
    ULONG surfaceBytes;
} MGLR200PrivateTextureV2;

typedef char MGLR200PrivateTextureV2SizeCheck[
    sizeof(MGLR200PrivateTextureV2) == 56 ? 1 : -1];

/* Direct texture fill: the backend allocates a fresh never-sampled
 * surface per upload (the R200 texture cache never invalidates in-place
 * rewrites), hands the aperture address to the PPC, and installs it at
 * commit. Input: textureName/width/height; the rest is backend output. */
#define MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V1_QUERY 0x4d475007UL
#define MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V1_VERSION 1UL

typedef struct MGLR200PrivateTexDirectBeginV1 {
    ULONG version;
    ULONG structSize;
    ULONG textureName;
    ULONG width;
    ULONG height;
    ULONG cpuAddress;
    ULONG pitch;
    ULONG format;
    ULONG bytes;
    ULONG deviceEpoch;
} MGLR200PrivateTexDirectBeginV1;

typedef char MGLR200PrivateTexDirectBeginV1SizeCheck[
    sizeof(MGLR200PrivateTexDirectBeginV1) == 40 ? 1 : -1];

/* Input: textureName. Output mirrors the V1 texture bridge so a caller
 * can update its texture mirror with one code path. */
#define MGL_R200_PRIVATE_TEXDIRECT_COMMIT_V1_QUERY 0x4d475008UL
#define MGL_R200_PRIVATE_TEXDIRECT_COMMIT_V1_VERSION 1UL

typedef struct MGLR200PrivateTexDirectCommitV1 {
    ULONG version;
    ULONG structSize;
    ULONG textureName;
    ULONG surfaceToken;
    ULONG width;
    ULONG height;
    ULONG definedLevels;
    ULONG levelCount;
    ULONG contentSerial;
    ULONG deviceEpoch;
} MGLR200PrivateTexDirectCommitV1;

typedef char MGLR200PrivateTexDirectCommitV1SizeCheck[
    sizeof(MGLR200PrivateTexDirectCommitV1) == 40 ? 1 : -1];

/* Input: textureName. Releases a pending direct allocation. */
#define MGL_R200_PRIVATE_TEXDIRECT_ABORT_V1_QUERY 0x4d475009UL
#define MGL_R200_PRIVATE_TEXDIRECT_ABORT_V1_VERSION 1UL

typedef struct MGLR200PrivateTexDirectAbortV1 {
    ULONG version;
    ULONG structSize;
    ULONG textureName;
} MGLR200PrivateTexDirectAbortV1;

typedef char MGLR200PrivateTexDirectAbortV1SizeCheck[
    sizeof(MGLR200PrivateTexDirectAbortV1) == 12 ? 1 : -1];

/* V2 adds the requested surface format: a 565 source can be stored
 * into a native R5G6B5PC surface, halving the aperture traffic. The
 * backend grants the requested format when the import succeeds and
 * reports the granted format back. */
#define MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V2_QUERY 0x4d47500aUL
#define MGL_R200_PRIVATE_TEXDIRECT_BEGIN_V2_VERSION 3UL
#define MGL_R200_TEXDIRECT_FMT_BGRA32 0UL
#define MGL_R200_TEXDIRECT_FMT_RGB565 1UL
/* requestFormat flag (CAP_DIRECT_PARK): instead of releasing the previous
 * residency - Radeon3DReleaseSurface waits for the device's newest fence,
 * a full drain - Begin moves it to a host-parked list and reports
 * MGL_R200_TEXDIRECT_FLAG_PARKED. The caller owns its lifetime and frees
 * it with TEXDIRECT_REAP_V1 once every submission that may sample it has
 * retired. */
#define MGL_R200_TEXDIRECT_REQ_PARK_OLD (1UL << 8)
#define MGL_R200_TEXDIRECT_FMT_MASK 0xffUL

typedef struct MGLR200PrivateTexDirectBeginV2 {
    ULONG version;
    ULONG structSize;
    ULONG textureName;
    ULONG width;
    ULONG height;
    ULONG requestFormat;
    ULONG cpuAddress;
    ULONG pitch;
    ULONG format;
    ULONG bytes;
    ULONG deviceEpoch;
    /* bit0: surface is driver-owned aux VRAM - the PPC writes it
     * directly through the copyback aperture and COMMIT must not
     * copy. bit1 (MGL_R200_TEXDIRECT_FLAG_PARKED): the previous
     * residency was moved to the host-parked list. */
    ULONG flags;
} MGLR200PrivateTexDirectBeginV2;

#define MGL_R200_TEXDIRECT_FLAG_PARKED (1UL << 1)

typedef char MGLR200PrivateTexDirectBeginV2SizeCheck[
    sizeof(MGLR200PrivateTexDirectBeginV2) == 48 ? 1 : -1];

/* V2 adds the staging-buffer address: the PPC converts into cached
 * shared RAM and the 68k performs the single VRAM copy on commit,
 * mirroring the Warp3D driver model (the PPC never touches the slow
 * aperture window). */
#define MGL_R200_PRIVATE_TEXDIRECT_COMMIT_V2_QUERY 0x4d47500bUL
#define MGL_R200_PRIVATE_TEXDIRECT_COMMIT_V2_VERSION 2UL

typedef struct MGLR200PrivateTexDirectCommitV2 {
    ULONG version;
    ULONG structSize;
    ULONG textureName;
    ULONG srcAddress;
    ULONG surfaceToken;
    ULONG width;
    ULONG height;
    ULONG definedLevels;
    ULONG levelCount;
    ULONG contentSerial;
    ULONG deviceEpoch;
} MGLR200PrivateTexDirectCommitV2;

typedef char MGLR200PrivateTexDirectCommitV2SizeCheck[
    sizeof(MGLR200PrivateTexDirectCommitV2) == 44 ? 1 : -1];

/* Releases every surface parked by MGL_R200_TEXDIRECT_REQ_PARK_OLD. The
 * caller guarantees that all GPU work which may sample them has retired
 * (so the release's newest-fence wait returns at once). Output: released
 * count. */
#define MGL_R200_PRIVATE_TEXDIRECT_REAP_V1_QUERY 0x4d475015UL
#define MGL_R200_PRIVATE_TEXDIRECT_REAP_V1_VERSION 1UL

typedef struct MGLR200PrivateTexDirectReapV1 {
    ULONG version;
    ULONG structSize;
    ULONG released;
} MGLR200PrivateTexDirectReapV1;

typedef char MGLR200PrivateTexDirectReapV1SizeCheck[
    sizeof(MGLR200PrivateTexDirectReapV1) == 12 ? 1 : -1];

/* Fullscreen BLIT-mode present prepared for a CP-queued copy (host-side
 * MGLPPC_CPPRESENT). The library runs every part of mglSwitchDisplay()'s
 * BLIT branch except the ClipBlit/WaitBlit and reports the copy: back
 * buffer (source) onto the screen bitmap (destination), width x height
 * from the origin. prepared=0 means not eligible (FLIP, windowed, single
 * buffer, sync on without BLIT, no imported front surface): nothing was
 * done and the caller presents with mglSwitchDisplay(). */
#define MGL_R200_PRIVATE_PRESENT_V1_QUERY 0x4d475014UL
#define MGL_R200_PRIVATE_PRESENT_V1_VERSION 1UL

typedef struct MGLR200PrivatePresentV1 {
    ULONG version;
    ULONG structSize;
    ULONG prepared;
    ULONG deviceEpoch;
    ULONG srcGpuAddress;
    ULONG srcPitch;
    ULONG dstGpuAddress;
    ULONG dstPitch;
    ULONG width;
    ULONG height;
    ULONG format;      /* RADEON3D_FORMAT_* of both surfaces */
} MGLR200PrivatePresentV1;

typedef char MGLR200PrivatePresentV1SizeCheck[
    sizeof(MGLR200PrivatePresentV1) == 44 ? 1 : -1];

#endif
