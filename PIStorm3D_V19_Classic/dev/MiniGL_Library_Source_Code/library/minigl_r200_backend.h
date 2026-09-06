#ifndef MINIGL_R200_BACKEND_H
#define MINIGL_R200_BACKEND_H

#include <exec/types.h>
#include <exec/tasks.h>

#include "minigl_r200.h"

/*
 * Interface between the R200 rendering backend and whichever library glue
 * links it:
 *
 *   - library/minigl_r200_lib.c, the standalone resident minigl.library, or
 *   - minigl.library's shared skeleton, when the backend is linked as
 *     mgl_68060_r200.lib / libmgl_68060_r200.a.
 *
 * MGLInit() and MGLTerm() intentionally carry the names and signatures the
 * skeleton already expects from a classic MiniGL archive, so that no glue
 * change is needed to drive this backend. See BACKEND_ABI.md.
 *
 * SysBase must be valid before MGLInit() is called; the backend does not
 * define it.
 */
GLboolean MGLInit(void);
void MGLTerm(void);

/*
 * Release the context owned by task, if it owns one. Classic MiniGL has
 * nothing to release at close time, so the skeleton does not call this by
 * default; see patches/0002 for the guarded hook that does.
 */
void MGLBackendTaskClose(struct Task *task);

/* TRUE while a context is alive. The standalone glue refuses expunge then. */
BOOL MGLBackendHasContext(void);

/* Shared implementation for every public vector the backend does not provide. */
ULONG LibUnsupported(void);

#endif
