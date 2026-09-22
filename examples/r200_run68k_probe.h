/*
 * r200_run68k_probe.h - shared helpers for the PPC->68k direct-call probes.
 *
 * Purpose: measure and validate different ways for a *native PPC* WarpOS
 * application to call 68k code (specifically the 68k minigl.library and its
 * dispatch table), without the 68k host process.
 *
 * The bridge is the WarpUP powerpc.library PPC vector `Run68K` (LVO -300).
 * This file replicates exactly what the MOS2WOS LP stubs do internally
 * (see powerpc/warpup_macros.h PPCLP*): read the target address from
 * PowerPCBase-298 and call it with r3=PowerPCBase, r4=&PPCArgs.
 *
 * Two call shapes are supported, because 68k Amiga code has two ABIs:
 *   - library vectors (LVOs): arguments in D0-D7/A0-A6, base in A6.
 *   - plain C functions (the MiniGL dispatch table entries): arguments on
 *     the 68k stack, passed through PPCArgs.PP_Stack.
 *
 * Crash-durable logging: every stage appends one line to
 * Work:wosbuild/run68k_trace.log (open/append/close per line), so if the
 * machine wedges the last line names the failing call.
 */

#ifndef R200_RUN68K_PROBE_H
#define R200_RUN68K_PROBE_H

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <powerpc/powerpc.h>

/* ------------------------------------------------------------------ */
/* Run68K bridge                                                       */
/* ------------------------------------------------------------------ */

/* PPC vector offsets in powerpc.library (powerpc/powerpc_protos.h). */
#define RK68_RUN68K_LVO      (-300)
#define RK68_WAITFOR68K_LVO  (-306)
#define RK68_LOWLEVEL_LVO    (-318)

/* The SDK sets PowerPCBase in startupwos.o __start (PowerPCBase = r4). */
extern struct Library *PowerPCBase;

typedef struct RK68Call {
    struct PPCArgs args;
    ULONG          status;   /* Run68K return value (PPERR_*) */
} RK68Call;

/* Call a powerpc.library PPC vector exactly like the LP stubs do:
 * the target address is stored at ppcBase+2+lvo. */
static ULONG rk68_vector_lvo(void *ppcBase, LONG lvo, struct PPCArgs *args)
{
    ULONG target = *(volatile ULONG *)((char *)ppcBase + 2 + lvo);
    ULONG ret;

    __asm__ __volatile__(
        "mr    4,%3\n"
        "mr    3,%2\n"
        "stwu  1,-320(1)\n"
        "mfcr  12\n"
        "stw   12,124(1)\n"
        "mflr  12\n"
        "stw   12,128(1)\n"
        "mtlr  %1\n"
        "blrl\n"
        "mr    %0,3\n"
        "lwz   12,124(1)\n"
        "mtcr  12\n"
        "lwz   12,128(1)\n"
        "mtlr  12\n"
        "la    1,320(1)\n"
        : "=r"(ret)
        : "r"(target), "r"(ppcBase), "r"(args)
        : "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
          "32", "33", "34", "35", "36", "37", "38", "39", "40",
          "41", "42", "43", "44", "45", "ctr", "memory");

    return ret;
}

/* Run68K (the normal synchronous 68k call entry). */
static ULONG rk68_vector(void *ppcBase, struct PPCArgs *args)
{
    return rk68_vector_lvo(ppcBase, RK68_RUN68K_LVO, args);
}

static void rk68_init(RK68Call *c)
{
    memset(&c->args, 0, sizeof(c->args));
    c->status = (ULONG)-1;
}

/*
 * Library-vector call (register ABI):
 *   PP_Code = library base, PP_Offset = LVO (negative), A6 = library base,
 *   arguments in PP_Regs[PPREG_D0..].
 */
static void rk68_call_lvo(RK68Call *c, void *ppcBase, void *libBase,
                          LONG lvo, const ULONG *regs, int nregs)
{
    int i;

    rk68_init(c);
    c->args.PP_Code   = libBase;
    c->args.PP_Offset = lvo;
    c->args.PP_Flags  = 0;
    for (i = 0; i < nregs && i < 15; i++)
        c->args.PP_Regs[i] = regs[i];
    c->args.PP_Regs[PPREG_A6] = (ULONG)libBase;
    c->status = rk68_vector(ppcBase, &c->args);
}

/*
 * Plain C function call (stack ABI), used for the MiniGL dispatch table
 * entries. The 68k compiler pushes arguments on the stack; PP_Stack is
 * copied there by Run68K. argv[0] is the first (leftmost) argument.
 */
/*
 * Stack-ABI call for the MiniGL dispatch entries.
 *
 * WarpUP's 68k-side Run68K handler (powerpc.s Runk86) copies PP_StackSize
 * bytes from **PP_Stack + 24** onto the 68k stack; the 24-byte prefix is
 * mandatory ("Offset into stack must be 24, see docs Run68K") and is not
 * copied. Arguments are laid out first-argument-first: argv[0] lands at
 * 4(sp), argv[1] at 8(sp), ...
 */
#define RK68_STACK_PREFIX 24

static void rk68_call_c(RK68Call *c, void *ppcBase, void *entry,
                        void *libBase, const ULONG *argv, int argc)
{
    static UBYTE stack[RK68_STACK_PREFIX + 24 * 4] __attribute__((aligned(32)));
    ULONG *args = (ULONG *)(stack + RK68_STACK_PREFIX);
    int i;

    rk68_init(c);
    if (argc > 24)
        argc = 24;
    for (i = 0; i < argc; i++)
        args[i] = argv[i];
    c->args.PP_Code          = entry;
    c->args.PP_Offset        = 0;
    c->args.PP_Flags         = 0;
    c->args.PP_Stack         = stack;
    c->args.PP_StackSize     = (ULONG)argc * 4UL;
    c->args.PP_Regs[PPREG_A6] = (ULONG)libBase;
    c->status = rk68_vector(ppcBase, &c->args);
}

static ULONG rk68_result(const RK68Call *c)
{
    return c->args.PP_Regs[PPREG_D0];
}

/* ------------------------------------------------------------------ */
/* Timebase                                                            */
/* ------------------------------------------------------------------ */

static ULONG rk68_mftb(void)
{
    ULONG t;
    __asm__ __volatile__("mftb %0" : "=r"(t));
    return t;
}

/* Calibrate the 7410 timebase against a 1 s DOS Delay. Returns Hz. */
static ULONG rk68_tb_hz(void)
{
    struct DateStamp d0, d1;
    ULONG t0, t1;
    LONG secs, ticks;

    DateStamp(&d0);
    t0 = rk68_mftb();
    Delay(50);                       /* 1 s, dos.library */
    t1 = rk68_mftb();
    DateStamp(&d1);

    secs  = (d1.ds_Days - d0.ds_Days) * 86400L
          + (d1.ds_Minute - d0.ds_Minute) * 60L
          + (d1.ds_Tick - d0.ds_Tick) / 50L;
    ticks = (d1.ds_Tick - d0.ds_Tick) % 50L;
    if (secs <= 0 && ticks <= 0)
        return 0;
    if (secs <= 0)
        return (ULONG)((double)(t1 - t0) * 50.0 / (double)ticks);
    return (ULONG)((double)(t1 - t0) / ((double)secs + (double)ticks / 50.0));
}

/* ------------------------------------------------------------------ */
/* Crash-durable trace + result files                                  */
/* ------------------------------------------------------------------ */

#ifndef RK68_TRACE_FILE
#define RK68_TRACE_FILE  "Work:wosbuild/run68k_trace.log"
#endif
#ifndef RK68_RESULT_FILE
#define RK68_RESULT_FILE "Work:wosbuild/run68k_result.txt"
#endif

static void rk68_trace_raw(const char *s, LONG len, BOOL resultFile)
{
    BPTR f = Open((STRPTR)(resultFile ? RK68_RESULT_FILE : RK68_TRACE_FILE),
                  MODE_OLDFILE);

    if (!f)
        f = Open((STRPTR)(resultFile ? RK68_RESULT_FILE : RK68_TRACE_FILE),
                 MODE_NEWFILE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, (APTR)s, len);
        Write(f, (APTR)"\n", 1);
        Close(f);
    }
}

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void rk68_logfile(const char *fmt, BOOL resultFile, va_list ap)
{
    char buf[240];

    vsprintf(buf, fmt, ap);
    rk68_trace_raw(buf, (LONG)strlen(buf), resultFile);
}

static void rk68_trace(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    rk68_logfile(fmt, FALSE, ap);
    va_end(ap);
}

static void rk68_result_line(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    rk68_logfile(fmt, TRUE, ap);
    va_end(ap);
}

static void rk68_trace_reset(void)
{
    BPTR f = Open((STRPTR)RK68_TRACE_FILE, MODE_NEWFILE);

    if (f)
        Close(f);
    f = Open((STRPTR)RK68_RESULT_FILE, MODE_NEWFILE);
    if (f)
        Close(f);
}

/* ------------------------------------------------------------------ */
/* 68k ABI mirrors (packed 2-byte layout; MOS2WOS headers are natural) */
/* ------------------------------------------------------------------ */

struct rk68_Node {
    UBYTE  ln_Type;
    BYTE   ln_Pri;
    char  *ln_Name;
    struct rk68_Node *ln_Next;
    struct rk68_Node *ln_Prev;
} __attribute__((packed));

struct rk68_Message {
    struct rk68_Node mn_Node;      /* 14 bytes */
    APTR   mn_ReplyPort;
    UWORD  mn_Length;
} __attribute__((packed));

typedef char rk68_msg_size_check[sizeof(struct rk68_Message) == 20 ? 1 : -1];

/*
 * MiniGL dispatch table prefix offsets.
 *
 * The 68k minigl.library presents ABI 5; the PPC DLL presents ABI 3. The
 * first 128 function-pointer fields are identical in both tables, so the
 * field numbers from include/libraries/minigl_dispatch.h apply here.
 * Field i (1-based) is at byte 20 + (i-1)*4 (4 ULONGs + currentContext).
 *
 * A runtime check validates abiVersion before any entry is used.
 */
#define MGLD_FIELD_OFF(i)   (20UL + ((ULONG)(i) - 1UL) * 4UL)
#define MGLD_ABI_PREFIX     128UL   /* fields shared by ABI 3 and ABI 5 */

#define MGLD_ABI_VERSION_MIN 3UL
#define MGLD_ABI_VERSION_68K 5UL
#define MGLD_ABI_SIZE_MIN    0x22cUL  /* V10 table minimum */

#define MGLD_ENTRY_GLBegin          MGLD_FIELD_OFF(4)
#define MGLD_ENTRY_GLClear          MGLD_FIELD_OFF(7)
#define MGLD_ENTRY_GLColor4f        MGLD_FIELD_OFF(13)
#define MGLD_ENTRY_GLEnd            MGLD_FIELD_OFF(29)
#define MGLD_ENTRY_GLFinish         MGLD_FIELD_OFF(30)
#define MGLD_ENTRY_GLGetError       MGLD_FIELD_OFF(38)
#define MGLD_ENTRY_GLGetString      MGLD_FIELD_OFF(41)
#define MGLD_ENTRY_GLVertex4f       MGLD_FIELD_OFF(86)

/* minigl_r200.h enum value (the backend has its own numbering). */
#define MGLD_GL_TRIANGLES           191UL

static ULONG rk68_fbits(float f)
{
    union { float f; ULONG u; } v;

    v.f = f;
    return v.u;
}
#define MGLD_ENTRY_MGLCreateContext MGLD_FIELD_OFF(91)
#define MGLD_ENTRY_MGLDeleteContext MGLD_FIELD_OFF(93)
#define MGLD_ENTRY_MGLSwitchDisplay MGLD_FIELD_OFF(114)
#define MGLD_ENTRY_mglChooseNumberOfBuffers MGLD_FIELD_OFF(120)
#define MGLD_ENTRY_mglChoosePixelDepth      MGLD_FIELD_OFF(121)
#define MGLD_ENTRY_mglChooseVertexBufferSize MGLD_FIELD_OFF(123)
#define MGLD_ENTRY_mglChooseWindowMode      MGLD_FIELD_OFF(124)

#define MGLD_LO_GL_VERSION 0x1F02UL

/* Byte-wise 32-bit read: the 68k library's dispatch table can be only
 * 2-byte aligned (68k ABI), so a direct ULONG load on PPC may be unaligned.
 * WarpUP emulates unaligned loads, but byte-wise assembly is cheap and
 * deterministic. */
static ULONG rk68_read32(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;

    return ((ULONG)b[0] << 24) | ((ULONG)b[1] << 16) |
           ((ULONG)b[2] << 8) | (ULONG)b[3];
}

static ULONG rk68_entry(const void *table, ULONG off)
{
    return rk68_read32((const char *)table + off);
}

/* ------------------------------------------------------------------ */
/* MiniGL 68k library facts                                          */
/* ------------------------------------------------------------------ */

#define RK68_MGL_LVO_GETDISPATCH (-30)   /* MiniGLGetDispatchTableLVO */
#define RK68_MGL_VERSION         10UL

static void rk68_exec_lvo(RK68Call *c, void *ppcBase, void *execBase,
                          LONG lvo, const ULONG *regs, int nregs)
{
    rk68_call_lvo(c, ppcBase, execBase, lvo, regs, nregs);
}

static void rk68_dos_lvo(RK68Call *c, void *ppcBase, void *dosBase,
                         LONG lvo, const ULONG *regs, int nregs)
{
    rk68_call_lvo(c, ppcBase, dosBase, lvo, regs, nregs);
}

#endif /* R200_RUN68K_PROBE_H */
