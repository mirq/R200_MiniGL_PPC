#ifndef MINIGL_R200_INTERNAL_H
#define MINIGL_R200_INTERNAL_H

/*
 * Shared internals of the R200 backend modules (r200_math.c, r200_submit.c,
 * r200_texture.c, r200_state.c, r200_geometry.c, r200_surface.c and the
 * lifecycle core in minigl_r200.c). Nothing here is public API: clients keep
 * using <libraries/minigl_dispatch.h>.
 */

#define __USE_NEW_TIMEVAL__

#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <devices/timer.h>
#include <dos/dos.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/resident.h>
#include <exec/semaphores.h>
#include <graphics/displayinfo.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/radeon3d.h>
#include <proto/timer.h>
#include <radeon3d.h>
#include <utility/tagitem.h>

#include <p96_bitmap_api.h>
#ifdef WARPUP
#pragma pack(pop)
#endif

#include "minigl_r200.h"
#include <libraries/minigl_perf.h>
#include <libraries/minigl_r200_bridge.h>
#include <libraries/minigl_fastpath.h>
#include <libraries/minigl_ppc_trace.h>
#include "minigl_r200_base.h"

/* ------------------------------------------------------------------ */
/* Build-time configuration                                            */
/* ------------------------------------------------------------------ */

#ifndef R200_PERF_TIMING
#define R200_PERF_TIMING 1
#endif

#ifndef R200_RSQRT_NEWTON_STEPS
#define R200_RSQRT_NEWTON_STEPS 1
#endif

#ifndef R200_RSQRT_USE_FPU_SQRT
#define R200_RSQRT_USE_FPU_SQRT 0
#endif

#ifndef R200_SPHEREMAP_CACHE
#define R200_SPHEREMAP_CACHE 0
#endif

#ifndef R200_NORMAL_TRANSFORM_CACHE
#define R200_NORMAL_TRANSFORM_CACHE 1
#endif

/* Capability set MGLInit() requires from Radeon9200.chip. */
#define REQUIRED_CAPS (RADEON3D_CAP_CP_READY | RADEON3D_CAP_FENCES | \
                       RADEON3D_CAP_BITMAP_IMPORT | \
                       RADEON3D_CAP_PHASE2_EXECUTE | \
                       RADEON3D_CAP_DEPTH_FUNCS | \
                       RADEON3D_CAP_TEXTURE_STATE | \
                       RADEON3D_CAP_FOG_MULTITEX | \
                       RADEON3D_CAP_HW_TRANSFORM_CLIP)

/* ------------------------------------------------------------------ */
/* Capacities                                                          */
/* ------------------------------------------------------------------ */

#define MAX_VERTICES 4096UL
#define MAX_SCREEN_VERTICES RADEON3D_IMMD_MAX_VERTICES
#define MAX_QUAD_VERTICES RADEON3D_IMMD_MAX_QUAD_VERTICES

/*
 * The R200 depth surface misbehaves at specific window widths unless its
 * 16-bit pitch is a multiple of 256 bytes (800 wide -> 1600-byte pitch
 * loses whole 32-pixel tile columns to a stale depth clear; 512/640/768
 * with aligned pitches are clean). p96 derives the pitch from the pixel
 * width, so the depth storage is padded in pixels until the pitch aligns.
 * Rendering bounds never change: the scissor and draw rectangle stay at
 * the client-visible width.
 */
ULONG DepthStorageWidth(ULONG width);
/* Triangle lists may contain 255 vertices; quad batches are limited to 252. */
#define SCREEN_VERTEX_CAPACITY MAX_SCREEN_VERTICES
#define MAX_RECORD_DWORDS RADEON3D_MAX_BATCH_DWORDS
#define MAX_GENERATED_DWORDS RADEON3D_MAX_BATCH_DWORDS
#define SUBMIT_SLOT_COUNT 2UL
/* Maximum header-only draw records one slot can hold: the batch budget
 * divided by the smallest TCL header (44 dwords). */
#define MAX_SLOT_DRAWS 192UL
/* Streaming-segment layout: one segment, two halves, one per submit slot.
 * The existing fence ping-pong then guards vertex-buffer reuse without
 * extra waits. Sized for the worst TCL draw (255 lit vertices, 13 dwords)
 * times a comfortable draw count. */
#define SEGMENT_BYTES (256UL * 1024UL)
#define SEGMENT_HALF_BYTES (SEGMENT_BYTES / 2UL)
/* Ring dwords a committed draw generates in place of inline vertices:
 * worst-case LOAD_VBPNTR for 6 arrays (1 packet + 1 count + 9 descriptor
 * words) plus the 2-dword DRAW_VBUF_2. The minimum emission is 8. */
#define COMMIT_FETCH_DWORDS 13UL
/* Segment streaming is the accepted default: the T5 acceptance matrix
 * passed with it enabled and gears gains ~13%. Define this to 0 to fall
 * back to the inline Execute path. */
#ifndef R200_STREAM_COMMITS
#define R200_STREAM_COMMITS 1
#endif
#ifndef R200_DUMP_FIRST_BATCHES
#define R200_DUMP_FIRST_BATCHES 0
#endif
#ifndef R200_STREAM_DIAG
#define R200_STREAM_DIAG 0
#endif
/* Async segment rotation: park a fully-submitted segment until its fence
 * retires instead of draining every submission mid-frame. Define to 0 to
 * restore the synchronous drain-and-free rotation. */
#ifndef R200_ASYNC_ROTATION
#define R200_ASYNC_ROTATION 1
#endif

/* The WarpOS frontend uses the same state and geometry implementation but
 * owns its current-context slot locally instead of consulting the 68k Exec
 * task owner. Device and surface ownership remain behind its platform layer. */
#ifndef R200_PPC_FRONTEND
#define R200_PPC_FRONTEND 0
#endif
#define MODELVIEW_STACK_DEPTH 41UL
#define PROJECTION_STACK_DEPTH 6UL
/*
 * Texture names are an OpenGL namespace, not a residency limit.  Legacy
 * MiniGL clients use sparse fixed names (BlitzQuake uses 1024..1087 for
 * lightmaps and 2000..2005 for sky faces), so a 64-entry namespace rejects
 * otherwise valid binds and makes the following upload overwrite whichever
 * low-numbered texture remained bound.  Resident Radeon surfaces are still
 * allocated lazily and evicted independently by EnsureTextureResident().
 */
#define MAX_TEXTURES 2048UL
#define MAX_TEXTURE_LEVELS 12UL
#define MIN_NATIVE_PRIMITIVE_VERTICES 12UL

#if R200_SPHEREMAP_CACHE
#define SPHEREMAP_CACHE_SIZE 256UL
#endif

/* Interface-11 lighting record geometry. A lit draw is a hardware-TCL
 * record whose header grows by the model-view + inverse-model-view pair,
 * fixed light state, and one dense block per enabled light; vertices gain
 * an object-space normal XYZ after W. See RADEON3D_SUBMISSION.md. */
#define R200_MAX_LIGHTS 8UL

/* Fullscreen screen buffers: displayed, pending switch, drawing. The
 * third buffer lets the render loop keep submitting while a switch is
 * outstanding (sync disabled); see CompletePendingFlip() in
 * r200_surface.c. */
#define R200_SCREEN_BUFFERS 3UL
#define R200_NORMAL_MATRICES_DWORDS RADEON3D_EXEC_NORMAL_MATRICES_DWORDS
#define R200_LIGHT_STATE_DWORDS RADEON3D_EXEC_LIGHT_STATE_DWORDS
#define R200_LIGHT_BLOCK_DWORDS RADEON3D_EXEC_LIGHT_BLOCK_DWORDS
#define R200_TCL_HEADER_DWORDS RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS
#define R200_TEXGEN_HEADER_DWORDS RADEON3D_EXEC_DRAW_TEXGEN_HEADER_DWORDS
#define R200_MAX_HEADER_DWORDS \
    (R200_TEXGEN_HEADER_DWORDS + R200_NORMAL_MATRICES_DWORDS + \
     R200_LIGHT_STATE_DWORDS + R200_LIGHT_BLOCK_DWORDS * R200_MAX_LIGHTS)
#define R200_TCL_VERTEX_DWORDS RADEON3D_EXEC_HW_TCL_VERTEX_DWORDS
#define R200_LIT_VERTEX_DWORDS RADEON3D_EXEC_HW_TCL_NORMAL_VERTEX_DWORDS
/* RADEON3D_DRAW_COMPACT_VERTEX strides: unit-1 texture and fog dwords
 * omitted instead of carried as zeros. */
#define R200_LIT_VERTEX_COMPACT_DWORDS 10UL
#define R200_TCL_VERTEX_COMPACT_DWORDS 7UL

/* Light-block dword offsets (24 vector dwords then 7 scalars), matching
 * Mesa classic r200's LIT_* scalar order. */
#define R200_LIGHT_AMBIENT 0UL
#define R200_LIGHT_DIFFUSE 4UL
#define R200_LIGHT_SPECULAR 8UL
#define R200_LIGHT_POSITION 12UL
#define R200_LIGHT_DIRECTION 16UL
#define R200_LIGHT_ATTEN_QUADRATIC 20UL
#define R200_LIGHT_ATTEN_LINEAR 21UL
#define R200_LIGHT_ATTEN_CONSTANT 22UL
#define R200_LIGHT_SPOT_DCD 24UL
#define R200_LIGHT_SPOT_DCM 25UL
#define R200_LIGHT_SPOT_EXPONENT 26UL
#define R200_LIGHT_SPOT_CUTOFF 27UL
#define R200_LIGHT_SPECULAR_THRESH 28UL
#define R200_LIGHT_RANGE_CUTOFF_SQRD 29UL
#define R200_LIGHT_ATTEN_CONSTANT_INV 30UL

/* ------------------------------------------------------------------ */
/* Data structures                                                     */
/* ------------------------------------------------------------------ */

struct Vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat w;
    GLfloat nx;
    GLfloat ny;
    GLfloat nz;
    GLfloat s;
    GLfloat t;
    GLfloat s1;
    GLfloat t1;
    GLfloat fog;
    ULONG color;
};

struct FastVertex {
    GLfloat x, y, z, w;
    GLfloat r, g, b, a;
    GLfloat u0, v0, u1, v1, q;
};

/* Field order mirrors the interface-11 hardware-TCL normal-vertex record
 * (x,y,z,w,nx,ny,nz,colour,s0,t0,s1,t1,fog): serialization of the hot lit/
 * sphere-mapped path is then a raw longword block copy instead of thirteen
 * field-wise float-to-bit conversions. Accesses are all by name. */
struct ScreenVertex {
    GLfloat x, y, z;
    GLfloat rhw;
    GLfloat nx, ny, nz;
    ULONG color;
    GLfloat s, t;
    GLfloat s1, t1, fog;
};

struct ClipVertex {
    GLfloat x, y, z, w;
    GLfloat s, t;
    GLfloat s1, t1, fog;
    ULONG color;
};

/* V19-parity multitexture queue (mglDrawMultitexBuffer). One completed
 * GL_POLYGON with texture unit 1 enabled, captured at glEnd() and drawn
 * in two textured passes at flush time. Vertices are stored fully
 * evaluated (colour, both texture-coordinate sets, fog) exactly as the
 * immediate path produced them. */
struct MtexPolygon {
    struct Vertex vertices[16];
    ULONG vertexCount;
    GLuint texture0;
    GLuint texture1;
};

/* Default V19 queue budget: 4096 vertex slots, two slots reserved per
 * polygon -> ~512 polygons. Override with mglChooseMtexBufferSize(). */
#define R200_MTEX_DEFAULT_VERTS 4096UL
#define R200_MTEX_MIN_POLYGONS 16UL
#define R200_MTEX_MAX_POLYGONS 4096UL

#if R200_SPHEREMAP_CACHE
struct SphereMapCacheEntry {
    ULONG generation;
    BOOL valid;
    ULONG key[11];
    GLfloat s, t, s1, t1;
};
#endif

struct WorstFrameSlot {
    ULONG frameNumber;
    ULONG ticks;
    ULONG serializeTicks;
    ULONG executeTicks;
    ULONG waitTicks;
    ULONG presentTicks;
    ULONG residencyTicks;
};

struct PerfMetrics {
    ULONG clockRate;
    ULONG frames;
    ULONG primitives;
    ULONG inputVertices;
    ULONG submittedVertices;
    ULONG drawRecords;
    ULONG clearRecords;
    ULONG submits;
    ULONG recordDwords;
    ULONG generatedDwords;
    ULONG frontendTicks;
    ULONG executeTicks;
    ULONG waitTicks;
    ULONG presentTicks;
    ULONG matrixTicks;
    ULONG serializeTicks;
    ULONG serializeStallTicks;
    ULONG serializeKernelFlushes;
    ULONG serializeKernelVertices;
    ULONG sphereMapCalls;
    ULONG sphereMapSamples;
    ULONG sphereMapTicks;
    ULONG sphereMapCacheHits;
    ULONG sphereMapCacheMisses;
    ULONG normalCacheHits;
    ULONG normalCacheMisses;
    ULONG textureResidencyTicks;
    ULONG nativeAttempts;
    ULONG nativeUsed;
    ULONG nativeFallbackCaps;
    ULONG nativeFallbackCull;
    ULONG nativeFallbackFlat;
    ULONG nativeFallbackSize;
    ULONG nativeFallbackClip;
    ULONG quadAttempts;
    ULONG quadUsed;
    ULONG quadFallbackCaps;
    ULONG quadFallbackState;
    ULONG quadFallbackClip;
    ULONG flushCalls;
    ULONG flushTextured0;
    ULONG flushUntextured0;
    ULONG dropDrawRectangle;
    ULONG dropTexture0;
    ULONG dropTexture0Mips;
    ULONG dropTexture1;
    ULONG dropTexture1Mips;
    ULONG dropFlatDegenerate;
    ULONG lastBoundTexture0;
    ULONG executeFailures;
    ULONG waitFailures;
    ULONG rhwClamps;
    ULONG textureUploadTicks;
    ULONG textureUploadBytes;
    ULONG textureUploadCopies;
    ULONG textureUploadUpdates;
    ULONG textureUploadCoalesced;
    ULONG textureUploadBarriers;
    ULONG textureUploadDeferred;
    ULONG textureEvictions;
    ULONG asyncSubmits;
    ULONG fenceWaits;
    ULONG fencePollHits;
    ULONG bufferWaits;
    ULONG textureWaits;
    ULONG drainWaits;
    ULONG stateCalls;
    ULONG stateNoops;
    ULONG deferredSplits;
    ULONG drawMerges;
    ULONG snapshotBuilds;
    ULONG snapshotFastHits;
    ULONG indexScanTicks;
    ULONG vertexFetchTicks;
    ULONG appendTicks;
    ULONG vertexFetches;
    ULONG drawTicks;
    ULONG captureCacheHits;
    /* Worst-frame attribution (ported from the PiStorm_SDK_v10 lineage).
     * Per-frame phase deltas land in the rank-ordered slowest-frame table;
     * clients read it back through the MGL_PERF_WORST_* glGetIntegerv
     * ranges. Library-internal state only -- no dispatch/ABI impact. */
    ULONG frameStartTicks;
    ULONG frameSerializeBase;
    ULONG frameExecuteBase;
    ULONG frameWaitBase;
    ULONG framePresentBase;
    ULONG frameResidencyBase;
    struct WorstFrameSlot worstFrames[10];
    ULONG worstFrameCount;
};

struct ArrayState {
    const UBYTE *pointer;
    ULONG stride;
    GLenum type;
    GLint size;
    BOOL enabled;
    /* 4-byte size alignment: float members in instances must not trap */
    UWORD __sizePad2;
};

struct TextureDirtyRect {
    UWORD left, top, right, bottom;
};

struct TextureObject {
    UBYTE *image;
    struct TextureDirtyRect *dirtyRects;
    ULONG imageBytes;
    ULONG width, height;
    ULONG definedLevels;
    ULONG levelCount;
    ULONG dirtyLevelMask;
    ULONG imageGeneration;
    ULONG residentGeneration;
    ULONG residentDeviceEpoch;
    ULONG surfaceGeneration;
    ULONG queuedBatchSerial;
    ULONG contentSerial;
    ULONG lastUseSerial;
    ULONG lastUseFence;
    ULONG lastUseDeviceEpoch;
    ULONG minFilter, magFilter;
    ULONG wrapS, wrapT;
    struct BitMap *bitmap;
    struct Radeon3DSurface surface;
    BOOL generated;
    BOOL resident;
    /* PPC-frontend only: retained canonical BGRA8888 shadow of the
     * level-0 image of a direct-fill texture. Letting a TexSubImage
     * patch this shadow and re-commit the full image through the direct
     * path keeps every residency change on the fresh-address rule while
     * the write stays on the PPC aperture (native copyback measured
     * ~5.8 MB/s). Zeroed for every other build. */
    UBYTE *shadow;
    ULONG shadowWidth, shadowHeight;
};

/* Radeon3DExecute consumes semantic records asynchronously.  A slot remains
 * immutable from Execute until its fence completes; the second slot lets the
 * CPU serialize the following batch while the first one is in flight. */
struct SubmitSlot {
    ULONG records[MAX_RECORD_DWORDS];
    ULONG recordCount;
    ULONG generatedDwordCount;
    ULONG queuedTextureCount;
    UWORD queuedTextureNames[MAX_TEXTURES];
    ULONG lastDrawOffset;
    ULONG lastDrawHeaderDwords;
    ULONG fence;
    ULONG deviceEpoch;
    BOOL lastDrawValid;
    BOOL inFlight;
    /* Streaming-commit mode: records carry headers only; vertices live in
     * this slot's segment half. vertexOffsets has one byte offset per
     * draw record, in record order. */
    BOOL commitMode;
    /* Slot mode is fixed by the first *draw*, not by a clear: a clear
     * rides in either kind of slot, so letting it decide would push a
     * whole frame's geometry back onto the legacy path. */
    BOOL modeDecided;
    ULONG stateBatch;
    /* Set when the pending state batch was expanded into per-draw records
     * (header mismatch under the adaptive threshold): the slot then stays
     * on the full-record commit shape and must never accumulate batch
     * draws again. */
    BOOL expanded;
    ULONG drawCount;
    ULONG vertexOffsets[MAX_SLOT_DRAWS];
    struct Radeon3DStateBatchDraw stateBatchDraws[MAX_SLOT_DRAWS];
    ULONG segmentCursor;
};

/* Streaming-segment state shared by both slots. The segment is allocated
 * once per device epoch; each slot writes vertices only into its own half
 * and Submit retires the other half's fence before it flips, so no extra
 * synchronization beyond the existing ping-pong is required. */
struct StreamSegment {
    struct Radeon3DSegment info;
    BOOL valid;
    /* 4-byte size alignment: float members in instances must not trap */
    UWORD __sizePad2;
};

/* A segment whose last commit is still in flight. The id stays allocated
 * (the service validates it at execute time and the RV280 may still be
 * fetching from it) until the recorded fence retires; ReapDeferredSegments
 * then returns it to the service pool. */
struct DeferredSegment {
    ULONG segmentId;
    ULONG fence;
    ULONG deviceEpoch;
};

/* Pool bound: the service exposes RADEON3D_MAX_SEGMENTS (8). Holding one
 * active plus a few deferred segments never exhausts it, and the
 * lowest-free-slot allocation policy keeps replacements in different VRAM
 * regions while any deferred entry survives. */
#define R200_DEFERRED_SEGMENT_MAX 4UL

/* Canonical semantic draw state. Dynamic record length and vertex count are
 * zero here; every other serialized header field is captured before the first
 * vertex is appended and remains immutable until that geometry is flushed. */
/* Fixed-function vertex-lighting state for one OpenGL light. Object-space
 * inputs are kept verbatim for queries; the derived eye-space values follow
 * Mesa classic r200 semantics and are refreshed by RefreshLightDerived(). */
struct LightState {
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat specular[4];
    GLfloat position[4];          /* object-space, as submitted */
    GLfloat eyePosition[4];       /* transformed at glLightfv time */
    GLfloat spotDirection[3];     /* object-space, as submitted */
    GLfloat eyeSpotDirection[3];  /* inverse-modelview-rotated, normalized */
    GLfloat spotExponent;
    GLfloat spotCutoff;
    GLfloat spotCosCutoff;
    GLfloat constantAttenuation;
    GLfloat linearAttenuation;
    GLfloat quadraticAttenuation;
    BOOL enabled;
    /* 4-byte size alignment: float members in instances must not trap */
    UWORD __sizePad2;
};

/* Pending snapshot. The variable-length header sits LAST so captures can
 * zero and compare only the used prefix; the fixed scalar prefix is
 * headerDwords + everything before the header array. */
struct PendingDrawState {
    ULONG headerDwords;
    ULONG vertexDwords;
    ULONG generatedVertexDwords;
    ULONG textureName[2];
    ULONG drawableWidth;
    ULONG drawableHeight;
    ULONG quadVertexLimit;
    BOOL depth;
    BOOL textured[2];
    BOOL fog;
    BOOL perspective;
    BOOL hardwareTransform;
    BOOL drawEnabled;
    BOOL drawRectangleValid;
    BOOL normalVertex;
    BOOL hardwareSphereMap;
    BOOL lit;
    ULONG enabledLights;
    /* BOOL is 16-bit on AmigaOS. Keep the variable-length ULONG header
     * aligned so the length-aware ULONG copy/compare includes every byte. */
    UWORD headerPadding;
    ULONG header[R200_MAX_HEADER_DWORDS];
};

#define R200_SNAPSHOT_SCALAR_DWORDS \
    ((ULONG *)&((const struct PendingDrawState *)0)->header - \
     (ULONG *)((const struct PendingDrawState *)0))

/* The length-aware snapshot copy and compare treat the scalar prefix as a
 * whole number of dwords. Adding a field that unaligns the header silently
 * drops its trailing bytes from both, so enforce the invariant here rather
 * than trusting the padding comment above. */
#ifdef __GNUC__
typedef char R200SnapshotAlignCheck[
    __builtin_offsetof(struct PendingDrawState, header) % 4UL == 0 &&
    R200_SNAPSHOT_SCALAR_DWORDS * 4UL ==
        __builtin_offsetof(struct PendingDrawState, header) ? 1 : -1];
#endif

/* Surfaces whose TextureObject went away (DeleteTextures, TexImage2D
 * redefinition) while a submitted record still names them. The node keeps
 * the VRAM surface alive until its fence retires; ReapDeferredSurfaces()
 * releases it once retirement is observable, so deleting a just-used
 * texture never blocks the CPU mid-frame. */
struct DeferredSurface {
    struct DeferredSurface *next;
    struct Radeon3DSurface surface;
    struct BitMap *bitmap;
    ULONG fence;
    ULONG deviceEpoch;
};

struct MiniGLR200Context {
    struct Radeon3DDevice *device;
    struct Radeon3DSurface surfaces[3];
    struct Radeon3DSurface depthSurface;
    struct BitMap *bitmap;
    struct BitMap *depthBitmap;
    struct Screen *screen;
    struct Window *window;
    struct RastPort source;
    struct ScreenBuffer *screenBuffers[3];
    struct MsgPort *safePort;
    struct MsgPort *displayPort;
    ULONG width;
    ULONG height;
    ULONG pixelDepth;
    ULONG colorFormat;
    ULONG radeonCaps;
    ULONG radeonGeneration;
    /* Radeon3D_CAP_COMPACT_TCL_VERTEX: TCL draw records omit the unit-1
     * texture and fog vertex dwords (stride 10 with normals, 7 plain). */
    ULONG compactTclVertex;
    ULONG batchDwords;
    ULONG viewportX;
    ULONG viewportY;
    ULONG viewportWidth;
    ULONG viewportHeight;
    GLint scissorX, scissorY;
    GLsizei scissorWidth, scissorHeight;
    ULONG clearColor;
    ULONG currentColor;
    GLfloat currentNormalX, currentNormalY, currentNormalZ;
    struct SubmitSlot submitSlots[SUBMIT_SLOT_COUNT];
    struct StreamSegment segment;
    /* Segments parked between submit and fence retirement (async
     * rotation). Entries are epoch-stamped; a device recovery drops them
     * without freeing because the segments died with the device. */
    struct DeferredSegment deferredSegments[R200_DEFERRED_SEGMENT_MAX];
    ULONG deferredSegmentCount;
    ULONG streamSegments;
    ULONG buildSlot;
    ULONG batchSerial;
    ULONG textureUseSerial;
    ULONG deviceEpoch;
    ULONG completedFence;
    struct Vertex vertices[MAX_VERTICES];
    struct ScreenVertex screenVertices[SCREEN_VERTEX_CAPACITY];
    ULONG vertexCount;
    ULONG screenVertexCount;
    ULONG screenCommand;
    /* Projective layout requested by the primitive currently being emitted.
     * pendingDraw captures the immutable effective value at first append. */
    ULONG screenProjective;
    ULONG screenHardwareTransform;
    struct PendingDrawState pendingDraw;
    ULONG semanticStateSerial;
    ULONG pendingSemanticStateSerial;
    /* Mismatch submits since the last present: gates when a differing
     * header may expand the slot into per-draw records instead of
     * submitting. Dense same-state workloads (gears) mismatch rarely and
     * must keep the compact state-batch shape; split-heavy world scenes
     * cross the threshold within a frame and collapse their submits. */
    ULONG mismatchSubmits;
    ULONG pendingProjectiveRequest;
    /* Transform mode the pending batch was opened with. screenHardwareTransform
     * itself is transient (EmitHardwareBatch raises it only around its append),
     * so the batch's mode has to be remembered separately or screen-space and
     * hardware-TCL vertices can be appended to the same record. */
    ULONG pendingHardwareTransformRequest;
    ULONG pendingDrawValid;
    GLenum primitive;
    GLenum error;
    GLfloat modelview[MODELVIEW_STACK_DEPTH][16];
    GLfloat projection[PROJECTION_STACK_DEPTH][16];
    ULONG modelviewDepth;
    ULONG projectionDepth;
    GLenum matrixMode;
    GLfloat depthNear;
    GLfloat depthFar;
    GLfloat clearDepth;
    ULONG zOffsetEnabled;
    GLenum depthFunc;
    ULONG depthTest;
    ULONG depthMask;
    struct ArrayState vertexArray;
    struct ArrayState colorArray;
    struct ArrayState texcoordArray;
    struct ArrayState normalArray;
    /* Fixed-function lighting (interface 11). */
    struct LightState lights[R200_MAX_LIGHTS];
    GLfloat frontEmissive[4];
    GLfloat frontAmbient[4];
    GLfloat frontDiffuse[4];
    GLfloat frontSpecular[4];
    GLfloat frontShininess;
    GLfloat lightModelAmbient[4];
    ULONG lightModelLocalViewer;
    ULONG lightModelTwoSide;       /* accepted; hardware is front-only */
    GLenum colorMaterialFace;
    GLenum colorMaterialMode;
    ULONG colorMaterialEnabled;
    ULONG lightingEnabled;
    ULONG normalizeEnabled;        /* GL_NORMALIZE */
    ULONG rescaleEnabled;          /* GL_RESCALE_NORMAL */
    GLfloat inverseModelview[16]; /* valid when inverseModelviewValid */
    ULONG inverseModelviewValid;
    GLfloat eyeZDir[3];           /* Mesa _EyeZDir equivalent */
    GLfloat modelviewInvScale;    /* Mesa _ModelViewInvScale equivalent */
    /* Scratch space for state captures and header serialization. Kept in
     * the context rather than on the stack so the library never demands
     * more than a few hundred bytes of client stack. */
    struct PendingDrawState captureScratch;
    /* Last built capture, keyed by (semanticStateSerial, command,
     * hardware-transform request, projective request). Strips and fans
     * flush at every glEnd, which clears screenVertexCount and would
     * otherwise force a full CapturePendingDrawState rebuild per block
     * even though nothing semantic changed. */
    struct PendingDrawState captureCache;
    ULONG captureCacheSerial;
    ULONG captureCacheCommand;
    ULONG captureCacheHardwareTransform;
    ULONG captureCacheProjective;
    ULONG captureCacheValid;
    struct Vertex *lockedVertices;
    ULONG lockedFirst;
    ULONG lockedCount;
    UBYTE *fastBuffer;
    ULONG fastBufferBytes;
    MGLFastPath *activeFastPath;
    struct TextureObject textures[MAX_TEXTURES];
    /* Surfaces awaiting fence retirement after their texture object was
     * released; see struct DeferredSurface. Reaped at present and in
     * DestroyContext, both of which follow a full drain. */
    struct DeferredSurface *deferredSurfaces;
    GLuint boundTexture[2];
    /* glDrawElements gathers each referenced index once into this cache
     * (generation-stamped, so no clearing between draws) and expands the
     * index list from it. Alias models reference the same vertices many
     * times, so this skips most per-index array decoding work. */
    struct Vertex *indexCacheVerts;
    ULONG *indexCacheStamp;
    ULONG indexCacheSize;
    ULONG indexCacheGeneration;
    /* mglDrawMultitexBuffer queue: polygons captured at glEnd() while
     * unit 1 is enabled, flushed two-pass by Lib_mglDrawMultitexBuffer().
     * Lazily allocated in context (never on the stack; see AGENTS.md
     * stack discipline), freed by FreeMtexQueue() at destroy. */
    struct MtexPolygon *mtexQueue;
    ULONG mtexQueueCapacity;
    ULONG mtexQueueCount;
    GLfloat currentS[2], currentT[2];
    GLenum textureEnv[2];
    GLenum textureGenModeS[2], textureGenModeT[2];
    GLfloat normalMatrix[9];
    GLfloat combinedMatrix[16];
    ULONG activeTexture;
    GLenum fogMode;
    GLfloat fogStart, fogEnd, fogDensity;
    ULONG fogColor;
    GLenum blendSource, blendDestination;
    GLenum shadeModel;
    GLenum cullFaceMode;
    GLenum frontFace;
    GLenum drawBuffer;
    GLenum readBuffer;
    ULONG flatColor;
    ULONG cullFaceEnabled;
    GLenum alphaFunc;
    GLfloat alphaRef;
    ULONG unpackAlignment, unpackRowLength, unpackSkipPixels, unpackSkipRows;
    ULONG packAlignment, packRowLength, packSkipPixels, packSkipRows;
    GLint texMemCurrent, texMemPeak;
    KeyHandlerFn keyFunc;
    SpecialHandlerFn specialFunc;
    MouseHandlerFn mouseFunc;
    IdleFn idleFunc;
    ULONG baseIDCMP;
    GLbitfield mouseButtonState;
    GLfloat minTriArea;
    GLfloat zOffset;
    ULONG syncEnabled;
    /* MINIGL_PRESENT=FLIP pins sync on so mglEnableSync() cannot drop the
     * flip path back to the unsynced blit mid-run. */
    ULONG syncForced;
    GLenum polygonMode;
    GLfloat polygonOffsetUnits;
    ULONG polygonOffsetFillEnabled;
    UBYTE colorTable[256][4];
    ULONG textureEnabled[2];
    ULONG textureGenS[2], textureGenT[2];
    ULONG normalMatrixValid;
    ULONG combinedMatrixValid;
    ULONG fogEnabled;
    ULONG blendEnabled;
    ULONG alphaTestEnabled;
    ULONG scissorEnabled;
    GLfloat pointSize;
    ULONG displayedBuffer;
    ULONG drawingBuffer;
    /* Fullscreen flip state machine. One switch may be outstanding at a
     * time; per-buffer notification bits are set by pumping the safe and
     * display message ports, so message arrival order never matters and a
     * stale foreign message cannot abort a wait (the old 2-buffer
     * WaitForNotification returned failure on any unexpected message).
     * bufferSafe bit i: buffer i's SafeMessage has arrived since it last
     * left the display (or it was never displayed) -- only a safe buffer
     * may be submitted as a render target. bufferDisplayed bit i: buffer
     * i's DispMessage arrived since its switch was requested. */
    ULONG pendingBuffer;
    ULONG pendingValid;
    ULONG bufferSafe;
    ULONG bufferDisplayed;
    ULONG presentBlit;
    ULONG presentFlip;
    ULONG singleBuffer;
    /* Allocated fullscreen screen-buffer count (1..3). 1 is the
     * diagnostic single-buffer mode; 2 ping-pongs with the flip wait;
     * 3 is the validated default. Set by mglChooseNumberOfBuffers(). */
    ULONG screenBufferCount;
    ULONG insideBegin;
    ULONG fullscreen;
    ULONG screenLocked;
    ULONG screenOwned;
    ULONG perfEnabled;
    struct PerfMetrics perf;
#if R200_SPHEREMAP_CACHE
    ULONG sphereMapGeneration;
    struct SphereMapCacheEntry sphereMapCache[SPHEREMAP_CACHE_SIZE];
#endif
#if R200_NORMAL_TRANSFORM_CACHE
    ULONG sphereNormalValid;
    GLfloat sphereObjectNX, sphereObjectNY, sphereObjectNZ;
    GLfloat sphereEyeNX, sphereEyeNY, sphereEyeNZ;
#endif
    /* Streaming segments leased by the private bridge for cross-CPU
     * transport (ring control block plus PPC vertex streams). Sized to
     * RADEON3D_MAX_SEGMENTS: the ring registration alone needs five
     * leases (control block + four vertex segments), and a cap of four
     * silently downgraded every session to three vertex segments. */
    struct R200SegmentLease {
        ULONG id;
        ULONG bytes;
    } segmentLeases[8];
    ULONG segmentLeaseCount;
#if R200_PPC_FRONTEND
    /* Host-leased vertex streaming segments (interface 13). The PPC writes
     * vertices through their CPU aperture mappings and rotates among them;
     * the host submits commit batches against the matching segment ids. */
    struct Radeon3DSegment segmentPool[4];
    ULONG segmentPoolCount;
    ULONG segmentPoolNext;
#endif
    /* Bitmask of texture units fed explicitly with glMultiTexCoord2fARB
     * during the current glBegin/glEnd (bit N = unit N). The legacy
     * implicit glTexCoord2f does not set bits. Lib_glEnd uses bit 1 to
     * tell Quake's real dual-unit path (explicit unit-1 feed: draw
     * immediately) from the classic fake-multitexture feed order
     * (implicit unit-1 feed: capture into the mtex queue). Appended at
     * the end: no existing field offsets change (68k strict alignment:
     * ULONG after ULONG). Big-endian safe: single-word bitmask. */
    ULONG texCoordFeedMask;
};

/* ------------------------------------------------------------------ */
/* Shared library bases                                                */
/* ------------------------------------------------------------------ */

/* Defined by minigl_library.c, which receives it in LibInit. */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;

/* Opened by MGLInit() (minigl_r200.c), closed by MGLTerm(). */
extern struct MiniGLR200Base *LibraryBase;
extern struct Library *Radeon9200Base;
extern struct Library *P96Base;
extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;
#if R200_PPC_FRONTEND
extern struct Library *TimerBase;
#else
extern struct Device *TimerBase;
#endif

/* ------------------------------------------------------------------ */
/* Math and matrix helpers (r200_math.c)                               */
/* ------------------------------------------------------------------ */

/* Type pun, not a real function: the serialization loop calls this once
 * per float field, and an out-of-line copy cost ~24k call/returns per
 * gears frame on the PPC. Keep it inline in every consumer. */
static __inline__ ULONG FloatBits(GLfloat value)
{
    union {
        GLfloat f;
        ULONG u;
    } bits;
    bits.f = value;
    return bits.u;
}

GLfloat RoundFloat32(GLfloat value);
ULONG BeginSemanticFloatMode(void);
void EndSemanticFloatMode(ULONG previousMode);
BOOL FloatFinite(GLfloat value);
GLubyte ColorByte(GLfloat value);
ULONG PackColor(GLfloat red, GLfloat green, GLfloat blue,
                   GLfloat alpha);
GLfloat ReciprocalSqrt(GLfloat value);
void Identity(GLfloat *matrix);
void CopyMatrix(GLfloat *destination, const GLfloat *source);
void MultiplyMatrix(GLfloat *output, const GLfloat *left,
                    const GLfloat *right);
GLfloat *CurrentMatrix(struct MiniGLR200Context *context);
void InvalidateSphereMapCache(struct MiniGLR200Context *context);
void InvalidateNormalMatrix(struct MiniGLR200Context *context);
void MultCurrentMatrix(struct MiniGLR200Context *context,
                             const GLfloat *matrix);
void BuildNormalMatrix(struct MiniGLR200Context *context);
BOOL Mat4Inverse(const GLfloat *input, GLfloat *output);
void BuildLightingMatrices(struct MiniGLR200Context *context);
void InitializeLightingState(struct MiniGLR200Context *context);
GLfloat FogFactor(const struct MiniGLR200Context *context, GLfloat x,
                  GLfloat y, GLfloat z, GLfloat w);

/* ------------------------------------------------------------------ */
/* Submission, fences, recovery (r200_submit.c)                        */
/* ------------------------------------------------------------------ */

enum SubmitWaitReason {
    SUBMIT_WAIT_DRAIN,
    SUBMIT_WAIT_BUFFER,
    SUBMIT_WAIT_TEXTURE
};

struct SubmitSlot *BuildSubmitSlot(struct MiniGLR200Context *context);
void ResetSubmitSlot(struct SubmitSlot *slot);
void InvalidateSubmissionState(struct MiniGLR200Context *context);
BOOL TextureQueued(const struct MiniGLR200Context *context,
                   const struct TextureObject *texture);
void MarkTextureQueued(struct MiniGLR200Context *context, GLuint name);
BOOL RecoverDevice(struct MiniGLR200Context *context);
BOOL WaitSubmitSlot(struct MiniGLR200Context *context,
                    struct SubmitSlot *slot,
                    enum SubmitWaitReason reason);
BOOL WaitAllSubmissions(struct MiniGLR200Context *context);
BOOL Submit(struct MiniGLR200Context *context);
BOOL SubmitAndWait(struct MiniGLR200Context *context);
BOOL WaitTextureUse(struct MiniGLR200Context *context,
                    struct TextureObject *texture);
BOOL EnsureStreamSegment(struct MiniGLR200Context *context);
void ReleaseStreamSegment(struct MiniGLR200Context *context);
void ReapDeferredSegments(struct MiniGLR200Context *context);
BOOL SegmentHalfFits(struct MiniGLR200Context *context,
                     const struct SubmitSlot *slot, ULONG vertexBytes);
void StreamDiagEvent(struct MiniGLR200Context *context, ULONG event,
                     ULONG argument0, ULONG argument1);
BOOL RotateStreamSegment(struct MiniGLR200Context *context,
                         ULONG vertexBytes);
ULONG GeneratedDrawDwords(ULONG vertexCount, BOOL depth, BOOL textured,
                          BOOL textured1, BOOL fog, BOOL perspective);
ULONG GeneratedVertexDwords(BOOL depth, BOOL textured, BOOL textured1,
                            BOOL fog, BOOL perspective);
ULONG GeneratedHardwareVertexDwords(BOOL textured, BOOL textured1,
                                    BOOL fog, BOOL normals);
ULONG GeneratedHardwareDrawDwords(ULONG vertexCount, BOOL depth,
                                   BOOL textured, BOOL textured1, BOOL fog,
                                   BOOL texgen, BOOL normals, ULONG enabledLights);
ULONG GeneratedClearDwords(BOOL depth);

/* ------------------------------------------------------------------ */
/* Texture objects, residency, upload (r200_texture.c)                 */
/* ------------------------------------------------------------------ */

BOOL IsMipFilter(ULONG filter);
BOOL EnsureTextureResident(struct MiniGLR200Context *context,
                           struct TextureObject *texture);
BOOL SyncTextureDirty(struct MiniGLR200Context *context,
                      struct TextureObject *texture);
ULONG TextureState(const struct MiniGLR200Context *context,
                   const struct TextureObject *texture, ULONG unit);
void ReapDeferredSurfaces(struct MiniGLR200Context *context);
void CopyBytes(void *destination, const void *source, ULONG count);
BOOL SourceFormatBytes(GLenum format, GLenum type, ULONG *bytes);

/* ------------------------------------------------------------------ */
/* Geometry batch flush (minigl_r200.c until r200_geometry.c lands)    */
/* ------------------------------------------------------------------ */

BOOL FlushScreenVertices(struct MiniGLR200Context *context);

/* mglDrawMultitexBuffer support (r200_geometry.c). */
BOOL CaptureMtexPolygon(struct MiniGLR200Context *context);
void FlushMtexBuffer(struct MiniGLR200Context *context, GLenum blendSource,
                     GLenum blendDestination, GLenum textureEnv);
void FreeMtexQueue(struct MiniGLR200Context *context);

struct BitMap *AllocateColorBitMap(ULONG width, ULONG height, ULONG depth);
struct BitMap *AllocateRgb565BitMap(ULONG width, ULONG height);
struct BitMap *AllocateArgbBitMap(ULONG width, ULONG height);
BOOL AllocateDepthStorage(struct Radeon3DDevice *device, ULONG radeonCaps,
                          ULONG width, ULONG height,
                          struct Radeon3DSurface *surface,
                          struct BitMap **bitmapOut,
                          const struct Radeon3DSurface *avoid,
                          ULONG avoidCount);
void ReleaseFriendBitMap(void);

/* ------------------------------------------------------------------ */
/* Deferred draw-state snapshots and state setters (r200_state.c)      */
/* ------------------------------------------------------------------ */

BOOL CapturePendingDrawState(struct MiniGLR200Context *context,
                             ULONG command,
                             struct PendingDrawState *state);
ULONG HardwareSphereMapMask(const struct MiniGLR200Context *context);

/* Cross-module state services */
void ResetPerf(struct MiniGLR200Context *context);
void RecordPerfFrame(struct MiniGLR200Context *context);
BOOL DrawRectangle(const struct MiniGLR200Context *context, ULONG *left,
                   ULONG *top, ULONG *right, ULONG *bottom);
BOOL SamePendingDrawState(const struct PendingDrawState *first,
                          const struct PendingDrawState *second);
void CopyPendingDrawState(struct PendingDrawState *destination,
                          const struct PendingDrawState *source);
BOOL OutsideBeginAllowed(struct MiniGLR200Context *context);

/* ------------------------------------------------------------------ */
/* Surfaces, present, resize, readback (r200_surface.c)                */
/* ------------------------------------------------------------------ */

ULONG SurfaceFormatForDepth(ULONG depth);
ULONG P96FormatForDepth(ULONG depth);
ULONG P96FormatMaskForDepth(ULONG depth);
BOOL ChangeBuffer(struct MiniGLR200Context *context,
                  struct ScreenBuffer *buffer);
void DrainNotifications(struct MsgPort *port);
void RestorePrimaryBuffer(struct MiniGLR200Context *context);
BOOL CreateWindowTarget(struct MiniGLR200Context *context, ULONG width,
                        ULONG height);
BOOL CreateFullscreenTarget(struct MiniGLR200Context *context,
                            ULONG requestedWidth, ULONG requestedHeight,
                            ULONG requestedMode, BOOL modeSpecified);

/* ------------------------------------------------------------------ */
/* Core services (minigl_r200.c)                                       */
/* ------------------------------------------------------------------ */

void ZeroMemory(APTR memory, ULONG bytes);
ULONG PerfClock(void);
ULONG PerfClockRate(void);
struct MiniGLR200Context *CurrentContext(void);
/* context must be zero-initialized before frontend defaults are applied. */
void InitializeFrontendContext(struct MiniGLR200Context *context);
#if R200_PPC_FRONTEND
extern struct MiniGLR200Context *R200PPCContext;
BOOL R200PPCPlatformPublishSlot(struct MiniGLR200Context *context,
                                ULONG slotIndex);
BOOL R200PPCPlatformWaitSlot(struct MiniGLR200Context *context,
                            ULONG slotIndex);
BOOL R200PPCPlatformRotateSegment(struct MiniGLR200Context *context,
                                  ULONG vertexBytes);
BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *context, BOOL present);
/* Completes the deferred first tile of the batched two-tile direct upload
 * (see the frontend's pendingDirect state): BEGIN + aperture fill + COMMIT
 * through the ordinary single-tile path. Returns TRUE when there is nothing
 * pending. Called from every point that can observe the texture or
 * invalidate its staging buffer. */
BOOL R200PPCPendingUploadFlush(struct MiniGLR200Context *context);
ULONG R200PPCPlatformStallTicks(void);
/* Clear the CP attribution counters (descriptor cache stats, publish
 * phases, wait classification) at a measurement boundary - called from
 * Lib_glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST) so timedemo reports cover
 * only the demo interval. The descriptor cache itself is untouched. */
void R200PPCResetCpAttribution(void);
/* PPC serialization kernels (client/minigl_ppc_serial.s): byte-reversing
 * block copies from the ScreenVertex array into a streaming vertex
 * segment, mirroring the m68k movem serializers in r200_geometry.c.
 * Source stride is always 13 dwords; destination stride is the record's
 * vertexDwords. Masks are 0x00000000/0xffffffff selects for disabled
 * texture/fog attributes. */
void MGLPPCSwapLit13(ULONG *destination, const ULONG *source, ULONG count,
                     ULONG unit0Mask, ULONG unit1Mask, ULONG fogMask);
void MGLPPCSwapLit10(ULONG *destination, const ULONG *source, ULONG count,
                     ULONG unit0Mask);
void MGLPPCSwapTCL10(ULONG *destination, const ULONG *source, ULONG count,
                     ULONG unit0Mask, ULONG unit1Mask, ULONG fogMask);
void MGLPPCSwapTCL7(ULONG *destination, const ULONG *source, ULONG count,
                    ULONG unit0Mask);
/* AltiVec staging variants (client/minigl_ppc_serial_altivec.c): the same
 * scalar kernels write a 16-byte-aligned cacheable scratch, then 16-byte
 * vector blocks move it to the aperture. Byte-identical output; only used
 * when MGLPPCAltiVecVertexEnabled is set by the runtime CPU probe. */
extern BOOL MGLPPCAltiVecVertexEnabled;
void MGLPPCAltiVecVertexProbe(void);
void MGLPPCSwapLit13Vector(ULONG *destination, const ULONG *source,
                           ULONG count, ULONG unit0Mask, ULONG unit1Mask,
                           ULONG fogMask);
void MGLPPCSwapLit10Vector(ULONG *destination, const ULONG *source,
                           ULONG count, ULONG unit0Mask);
void MGLPPCSwapTCL10Vector(ULONG *destination, const ULONG *source,
                           ULONG count, ULONG unit0Mask, ULONG unit1Mask,
                           ULONG fogMask);
void MGLPPCSwapTCL7Vector(ULONG *destination, const ULONG *source,
                          ULONG count, ULONG unit0Mask);
#endif
void SetError(struct MiniGLR200Context *context, GLenum error);
#if R200_PPC_FRONTEND && defined(MGLPPC_ERROR_ORIGIN)
void R200PPCErrorAt(const char *file, ULONG line, GLenum error);
#define SetError(context, error) \
    (R200PPCErrorAt(__FILE__, __LINE__, (error)), (SetError)((context), (error)))
#endif
void BumpSemanticStateSerial(struct MiniGLR200Context *context);
BOOL StateChangeAllowed(struct MiniGLR200Context *context);
BOOL ValidPrimitive(GLenum primitive);
struct Radeon3DSurface *TargetSurface(struct MiniGLR200Context *context);
struct Radeon3DSurface *ReadSourceSurface(struct MiniGLR200Context *context);


#endif /* MINIGL_R200_INTERNAL_H */
