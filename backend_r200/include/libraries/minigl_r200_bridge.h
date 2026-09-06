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
     * copy. */
    ULONG flags;
} MGLR200PrivateTexDirectBeginV2;

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

#endif
