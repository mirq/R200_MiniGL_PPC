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
/* Segment zero is a valid lease. Probe explicitly after context creation. */
#define MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_QUERY 0x4d475012UL
#define MGL_R200_PRIVATE_INDIRECT_DISPATCH_V1_VERSION 1UL
#define MGL_R200_PRIVATE_INDIRECT_PROBE (1UL << 31)
#define MGL_R200_PRIVATE_INDIRECT_SUPPORTED (1UL << 0)

/* Read-only metadata for a live surface owned by the current context. */
#define MGL_R200_PRIVATE_SURFACE_V1_QUERY 0x4d475013UL
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

#endif
