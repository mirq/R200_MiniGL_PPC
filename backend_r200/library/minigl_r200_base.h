#ifndef MINIGL_R200_BASE_H
#define MINIGL_R200_BASE_H

#include <exec/types.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>

struct MiniGLR200Context;

struct MiniGLR200Base {
    struct Library lib;
    BPTR segList;
    struct SignalSemaphore lock;
    struct Task *owner;
    struct MiniGLR200Context *context;
    BOOL windowMode;
    ULONG pixelDepth;
    /*
     * Pre-context tuning hints (mglChoose*, mglProhibit*, mglProposeCloseDesktop).
     * Set only before the first mglCreateContext*() call, mirroring
     * windowMode/pixelDepth above. numberOfBuffers (1..3, consumed by
     * CreateFullscreenTarget), prohibitMipMapping (GL_TEXTURE_MIN_FILTER
     * mipmap clamp) and mtexBufferSize (mglDrawMultitexBuffer queue
     * capacity) have real effects. guardBand, textureBufferSize,
     * vertexBufferSize, alphaFallback prohibition and closeDesktop have no
     * lever in this backend's fixed batch/vertex-buffer sizing (see
     * AGENTS.md on the validated 8192-dword batch budget); they are
     * accepted and stored for introspection but do not change behaviour.
     */
    BOOL guardBand;
    LONG numberOfBuffers;
    LONG textureBufferSize;
    LONG vertexBufferSize;
    LONG mtexBufferSize;
    BOOL prohibitAlphaFallback;
    BOOL prohibitMipMapping;
    BOOL proposeCloseDesktop;
};

#endif
