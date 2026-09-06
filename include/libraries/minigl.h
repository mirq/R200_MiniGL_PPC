#ifndef LIBRARIES_MINIGL_H
#define LIBRARIES_MINIGL_H

#include <exec/types.h>

/* WarpOS MiniGL API level.  V10 callers remain supported by the frozen
 * dispatch-table prefix; newer fields are appended only. */
#define MINIGL_VERSION 19
#define MINIGL_MIN_VERSION 10

#ifndef MINIGL_PPC_DLLNAME
#define MINIGL_PPC_DLLNAME "minigl_ppc.dll"
#endif
#ifndef MINIGL_PPC_PORTNAME
#define MINIGL_PPC_PORTNAME "MiniGLPPC.DLL"
#endif

/* On WarpOS this is the Hyperion DLL instance handle, not struct Library *. */
extern void *MiniGLBase;

#endif
