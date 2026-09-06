#ifndef MINIGL_PPC_TRACE_H
#define MINIGL_PPC_TRACE_H

#include <exec/types.h>

/*
 * Unbuffered frontend trace logger (DH2:wosbuild/mglppc_client.log).
 *
 * The DLL and the client executable each embed their own libc instance, so
 * printf/fflush inside the frontend never reliably reaches the client's
 * redirected stdout while the process is alive. This logger owns its own
 * dos.library file handle and writes through immediately, which makes it
 * usable for diagnosing processes that wedge before exiting.
 *
 * Writes are line-oriented and rate limited at the call sites; the logger
 * itself caps total lines so a hot failure loop cannot fill the disk.
 *
 * Every emitted line costs a 68k-side Open/Seek/Write/Close cycle, so the
 * logger is compile-time gated: MGLPPC_CLIENT_TRACE defaults to 0 and all
 * call sites (including the shared transport code) are compiled out.
 * Define MGLPPC_CLIENT_TRACE=1 project-wide to re-enable.
 */
#ifndef MGLPPC_CLIENT_TRACE
#define MGLPPC_CLIENT_TRACE 0
#endif

/* With MGLPPC_CLIENT_TRACE 0 the definitions below become empty bodies and
 * the transport call sites are gated off; declarations stay so the library
 * files keep compiling cleanly. */
void R200PPCTraceOpen(void);
void R200PPCTraceClose(void);
void R200PPCTraceStr(const char *line);
void R200PPCTraceUL(const char *label, ULONG value);
void R200PPCTraceUL2(const char *label1, ULONG value1,
                     const char *label2, ULONG value2);

/* Console diagnostics (DLL_OUTPUT), silent unless the environment sets
 * MGLPPC_VERBOSE. Same rate limiting discipline as the trace calls. */
void R200PPCConsolePrintf(const char *fmt, ...);

#endif
