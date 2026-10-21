#ifndef LIBRARIES_MINIGL_H
#define LIBRARIES_MINIGL_H

#include <exec/types.h>

/* WarpOS MiniGL API level, matching upstream minigl.library 29.x. 19 -> 29 is
 * a release boundary, not a routine bump: it moves together with
 * MINIGL_DISPATCH_ABI_VERSION 3 -> 5 (OpenGL token values, upstream slot
 * layout), which MiniGLOpen tests for equality, so a V19-era client is
 * refused rather than silently misbound. The frozen V10 prefix is unchanged. */
#ifndef MINIGL_VERSION
#define MINIGL_VERSION 29
#endif
#define MINIGL_MIN_VERSION 29

#ifndef MINIGL_PPC_DLLNAME
#define MINIGL_PPC_DLLNAME "minigl_ppc.dll"
#endif
#ifndef MINIGL_PPC_PORTNAME
#define MINIGL_PPC_PORTNAME "MiniGLPPC.DLL"
#endif

/* On WarpOS this is the Hyperion DLL instance handle, not struct Library *. */
extern void *MiniGLBase;

#endif
