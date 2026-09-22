/*
 * r200_run68k_bridge_probe.c - can a native PPC WarpOS app call the 68k
 * minigl.library directly, and what does it cost?
 *
 * Stages (each writes Work:wosbuild/run68k_trace.log before it runs):
 *   0  environment (PowerPCBase/SysBase/DOSBase)
 *   1  open minigl.library v10 through the bridged OpenLibrary
 *   2  Run68K sanity: exec FindTask(NULL) via Run68K, cross-checked against
 *      the compiler-generated LP stub
 *   3  Run68K argument/write-back sanity: dos DateStamp into a PPC buffer
 *   4  Run68K library-vector call: minigl.library LVO -30
 *      (MiniGLGetDispatchTableLVO) -> dispatch table; validate ABI header
 *   5  stack-ABI call of context-free dispatch entries (PP_Stack), e.g.
 *      mglChooseWindowMode / mglChoosePixelDepth
 *   6  timing: empty loop, exec LVO, dos LVO, minigl LVO, dispatch entry,
 *      LP-stub call, self-loopback Exec message
 *   7  revalidation and alternatives: retry, WaitFor68K status check,
 *      optional Run68KLowLevel (-lowlevel)
 *   8  results file + summary
 *   9  dangerous, opt-in only: -async (PPF_ASYNC) and -raw (direct PPC call
 *      through a 68k function pointer). Both are expected to fail/trap.
 *
 * Build:  make -f Makefile.import.gcc \
 *             bin/examples/r200_run68k_bridge_probe_ppc.warpelf
 * Deploy: zip/push/UnZip, then Elf2Exe2 on the Amiga, then run it.
 * No MiniGL host and no minigl_ppc.dll are needed.
 */

#include "r200_run68k_probe.h"

/* packed 68k Library mirror (natural MOS2WOS layout reads wrong offsets) */
struct rk68_Library {
    UBYTE  ln_Type;
    BYTE   ln_Pri;
    char  *ln_Name;
    struct rk68_Library *ln_Next;
    struct rk68_Library *ln_Prev;
    UBYTE  lib_Flags;
    UBYTE  lib_Pad;
    UWORD  lib_NegSize;
    UWORD  lib_PosSize;
    UWORD  lib_Version;
    UWORD  lib_Revision;
    APTR   lib_IdString;
    ULONG  lib_Sum;
    UWORD  lib_OpenCnt;
} __attribute__((packed));
typedef char rk68_lib_size_check[sizeof(struct rk68_Library) == 34 ? 1 : -1];

/* exec/dos LVOs used here (from powerup/ppcinline/{exec,dos}.h) */
#define LVO_EXEC_FINDTASK  (-0x126)   /* -294 */
#define LVO_DOS_DATESTAMP  (-0x0c0)   /* -192 */

#define GL_TRUE_  1UL
#define GL_FALSE_ 0UL

static int  g_fail;
static ULONG g_tb_hz;
static void *g_ppcBase;
static struct Library *g_sysBase;
static struct Library *g_dosBase;
static struct Library *g_miniGLBase;
static void *g_table;
static BOOL  g_table_ok;

static void trace_passfail(const char *what, BOOL ok, ULONG status, ULONG value)
{
    rk68_trace("%s: %s status=%lu value=%08lx",
               what, ok ? "PASS" : "FAIL",
               (unsigned long)status, (unsigned long)value);
    rk68_result_line("%s: %s status=%lu value=%08lx",
                     what, ok ? "PASS" : "FAIL",
                     (unsigned long)status, (unsigned long)value);
    if (!ok)
        g_fail++;
}

/* ------------------------------------------------------------------ */

static BOOL stage0_env(void)
{
    rk68_trace("stage 0: ppcBase=%08lx SysBase=%08lx DOSBase=%08lx",
               (unsigned long)g_ppcBase,
               (unsigned long)g_sysBase, (unsigned long)g_dosBase);
    if (!g_ppcBase) {
        rk68_trace("stage 0: PowerPCBase is NULL - Run68K unavailable");
        return FALSE;
    }
    return TRUE;
}

static BOOL stage1_open(void)
{
    struct rk68_Library *lib;

    rk68_trace("stage 1: OpenLibrary(minigl.library,10)");
    g_miniGLBase = OpenLibrary((STRPTR)"minigl.library", RK68_MGL_VERSION);
    if (!g_miniGLBase) {
        rk68_trace("stage 1: FAIL open");
        rk68_result_line("open minigl.library: FAIL");
        g_fail++;
        return FALSE;
    }
    /* read version through the packed 68k layout */
    lib = (struct rk68_Library *)g_miniGLBase;
    rk68_trace("stage 1: base=%08lx version=%u.%u id=%lx",
               (unsigned long)g_miniGLBase,
               (unsigned)lib->lib_Version, (unsigned)lib->lib_Revision,
               (unsigned long)lib->lib_IdString);
    rk68_result_line("open minigl.library: PASS base=%08lx v%u.%u",
                     (unsigned long)g_miniGLBase,
                     (unsigned)lib->lib_Version, (unsigned)lib->lib_Revision);
    return TRUE;
}

/* Run68K a single exec call and compare against the compiler LP stub. */
static BOOL stage2_sanity(void)
{
    RK68Call c;
    ULONG regs[1];
    ULONG viaRun68K, viaLP;

    regs[0] = 0;                       /* A1 = NULL name */
    rk68_exec_lvo(&c, g_ppcBase, g_sysBase, LVO_EXEC_FINDTASK, regs, 1);
    viaRun68K = rk68_result(&c);
    viaLP = (ULONG)FindTask(NULL);

    rk68_trace("stage 2: FindTask Run68K status=%lu ret=%08lx; LP=%08lx",
               (unsigned long)c.status, (unsigned long)viaRun68K,
               (unsigned long)viaLP);

    trace_passfail("FindTask via Run68K",
                   c.status == 0 && viaRun68K != 0 &&
                   (viaLP == 0 || viaRun68K == viaLP),
                   c.status, viaRun68K);
    return c.status == 0;
}

static BOOL stage3_datestamp(void)
{
    struct DateStamp ds;
    RK68Call c;
    ULONG regs[2];
    BOOL ok;

    memset(&ds, 0, sizeof(ds));
    regs[0] = 0;                       /* unused */
    regs[1] = (ULONG)&ds;              /* D1 = buffer */
    rk68_dos_lvo(&c, g_ppcBase, g_dosBase, LVO_DOS_DATESTAMP, regs, 2);

    /* AmigaDOS DateStamp: days since 1978, minutes since midnight (0-1439),
     * ticks since the last minute (0-2999). */
    ok = (c.status == 0) && ds.ds_Days > 0 && ds.ds_Minute < 24 * 60 &&
         ds.ds_Tick < 3000;
    rk68_trace("stage 3: DateStamp status=%lu days=%ld min=%ld tick=%ld",
               (unsigned long)c.status, (long)ds.ds_Days,
               (long)ds.ds_Minute, (long)ds.ds_Tick);
    trace_passfail("DateStamp via Run68K", ok, c.status,
                   (ULONG)ds.ds_Tick);
    return ok;
}

static BOOL g_table_ok;

static BOOL stage4_minigl_lvo(void)
{
    RK68Call c;
    void *table;
    ULONG abi, size, flags, reserved, ctx;

    rk68_trace("stage 4: minigl.library LVO %d (MiniGLGetDispatchTableLVO)",
               (int)RK68_MGL_LVO_GETDISPATCH);
    rk68_call_lvo(&c, g_ppcBase, g_miniGLBase, RK68_MGL_LVO_GETDISPATCH,
                  NULL, 0);
    table = (void *)rk68_result(&c);
    rk68_trace("stage 4: status=%lu table=%08lx",
               (unsigned long)c.status, (unsigned long)table);

    /* 68k ABI only guarantees 2-byte alignment: reject odd addresses only. */
    if (c.status != 0 || !table || ((ULONG)table & 1)) {
        rk68_trace("stage 4: retrying LVO after Delay(1)");
        Delay(1);
        rk68_call_lvo(&c, g_ppcBase, g_miniGLBase,
                      RK68_MGL_LVO_GETDISPATCH, NULL, 0);
        table = (void *)rk68_result(&c);
        rk68_trace("stage 4: retry status=%lu table=%08lx",
                   (unsigned long)c.status, (unsigned long)table);
    }

    if (c.status != 0 || !table) {
        trace_passfail("minigl dispatch LVO", FALSE, c.status,
                       (ULONG)table);
        return FALSE;
    }

    g_table = table;   /* readable from subsequent stages */
    abi      = rk68_read32((const char *)table + 0);
    size     = rk68_read32((const char *)table + 4);
    flags    = rk68_read32((const char *)table + 8);
    reserved = rk68_read32((const char *)table + 12);
    ctx      = rk68_read32((const char *)table + 16);

    rk68_trace("stage 4: table abi=%lu size=%lu flags=%08lx res=%lu ctx=%08lx",
               (unsigned long)abi, (unsigned long)size,
               (unsigned long)flags, (unsigned long)reserved,
               (unsigned long)ctx);
    rk68_result_line("dispatch table: abi=%lu size=%lu flags=%08lx ctx=%08lx",
                     (unsigned long)abi, (unsigned long)size,
                     (unsigned long)flags, (unsigned long)ctx);

    g_table_ok = (abi == MGLD_ABI_VERSION_68K || abi == 3UL) &&
                 size >= MGLD_ABI_SIZE_MIN;
    trace_passfail("minigl dispatch table header", g_table_ok, c.status, abi);
    return g_table_ok;
}

static BOOL stage5_stack_calls(void)
{
    RK68Call c;
    ULONG arg;
    BOOL ok = TRUE;

    if (!g_table)
        return FALSE;

    /* mglChooseWindowMode(GL_TRUE): one stack argument, order-agnostic. */
    arg = GL_TRUE_;
    rk68_call_c(&c, g_ppcBase,
                (void *)rk68_entry(g_table, MGLD_ENTRY_mglChooseWindowMode),
                g_miniGLBase, &arg, 1);
    rk68_trace("stage 5: mglChooseWindowMode(TRUE) status=%lu", 
               (unsigned long)c.status);
    trace_passfail("mglChooseWindowMode via PP_Stack", c.status == 0,
                   c.status, rk68_result(&c));

    arg = 16;
    rk68_call_c(&c, g_ppcBase,
                (void *)rk68_entry(g_table, MGLD_ENTRY_mglChoosePixelDepth),
                g_miniGLBase, &arg, 1);
    rk68_trace("stage 5: mglChoosePixelDepth(16) status=%lu",
               (unsigned long)c.status);
    trace_passfail("mglChoosePixelDepth via PP_Stack", c.status == 0,
                   c.status, rk68_result(&c));

    arg = 2;
    rk68_call_c(&c, g_ppcBase,
                (void *)rk68_entry(g_table,
                                   MGLD_ENTRY_mglChooseNumberOfBuffers),
                g_miniGLBase, &arg, 1);
    rk68_trace("stage 5: mglChooseNumberOfBuffers(2) status=%lu",
               (unsigned long)c.status);
    trace_passfail("mglChooseNumberOfBuffers via PP_Stack", c.status == 0,
                   c.status, rk68_result(&c));

    arg = 1000;
    rk68_call_c(&c, g_ppcBase,
                (void *)rk68_entry(g_table,
                                   MGLD_ENTRY_mglChooseVertexBufferSize),
                g_miniGLBase, &arg, 1);
    rk68_trace("stage 5: mglChooseVertexBufferSize(1000) status=%lu",
               (unsigned long)c.status);
    trace_passfail("mglChooseVertexBufferSize via PP_Stack", c.status == 0,
                   c.status, rk68_result(&c));

    return ok;
}

/* ------------------------------------------------------------------ */

static ULONG bench_empty(int iters)
{
    volatile ULONG sink = 0;
    ULONG t0 = rk68_mftb(), t1, i;

    for (i = 0; i < (ULONG)iters; i++)
        sink = sink + 1;
    t1 = rk68_mftb();
    return t1 - t0;
}

static ULONG bench_findtask_lvo(int iters)
{
    RK68Call c;
    ULONG regs[1], t0, t1, i;

    regs[0] = 0;
    t0 = rk68_mftb();
    for (i = 0; i < (ULONG)iters; i++)
        rk68_exec_lvo(&c, g_ppcBase, g_sysBase, LVO_EXEC_FINDTASK, regs, 1);
    t1 = rk68_mftb();
    return t1 - t0;
}

static ULONG bench_findtask_lp(int iters)
{
    ULONG t0 = rk68_mftb(), t1, i, sink = 0;

    for (i = 0; i < (ULONG)iters; i++)
        sink += (ULONG)FindTask(NULL);
    t1 = rk68_mftb();
    if (!sink)
        rk68_trace("bench: LP FindTask sink=0");
    return t1 - t0;
}

static ULONG bench_minigl_lvo(int iters)
{
    RK68Call c;
    ULONG t0 = rk68_mftb(), t1, i;

    if (!g_miniGLBase)
        return 0;
    t0 = rk68_mftb();
    for (i = 0; i < (ULONG)iters; i++)
        rk68_call_lvo(&c, g_ppcBase, g_miniGLBase,
                      RK68_MGL_LVO_GETDISPATCH, NULL, 0);
    t1 = rk68_mftb();
    return t1 - t0;
}

static ULONG bench_chooser_c(int iters)
{
    RK68Call c;
    ULONG arg = GL_TRUE_, t0 = rk68_mftb(), t1, i;

    if (!g_table)
        return 0;
    t0 = rk68_mftb();
    for (i = 0; i < (ULONG)iters; i++)
        rk68_call_c(&c, g_ppcBase,
                    (void *)rk68_entry(g_table,
                                       MGLD_ENTRY_mglChooseWindowMode),
                    g_miniGLBase, &arg, 1);
    t1 = rk68_mftb();
    return t1 - t0;
}

static ULONG bench_datestamp_lvo(int iters)
{
    struct DateStamp ds;
    RK68Call c;
    ULONG regs[2], t0, t1, i;

    regs[0] = 0;
    regs[1] = (ULONG)&ds;
    t0 = rk68_mftb();
    for (i = 0; i < (ULONG)iters; i++)
        rk68_dos_lvo(&c, g_ppcBase, g_dosBase, LVO_DOS_DATESTAMP, regs, 2);
    t1 = rk68_mftb();
    return t1 - t0;
}

static ULONG bench_exec_loopback(int iters)
{
    static struct rk68_Message msg;
    struct MsgPort *port = CreateMsgPort();
    ULONG t0, t1, i;

    if (!port)
        return 0;
    t0 = rk68_mftb();
    for (i = 0; i < (ULONG)iters; i++) {
        PutMsg(port, (struct Message *)&msg);
        WaitPort(port);
        GetMsg(port);
    }
    t1 = rk68_mftb();
    DeleteMsgPort(port);
    return t1 - t0;
}

static void bench_show(const char *label, int iters, ULONG ticks)
{
    ULONG per = iters > 0 ? ticks / (ULONG)iters : 0;
    ULONG ns  = (g_tb_hz && iters > 0)
              ? (ULONG)(((double)ticks * 1.0e9) /
                        ((double)g_tb_hz * (double)iters))
              : 0;

    rk68_trace("bench %-24s iters=%d ticks=%lu ticks_per_call=%lu ns=%lu",
               label, iters, (unsigned long)ticks,
               (unsigned long)per, (unsigned long)ns);
    rk68_result_line("bench %-24s iters=%d ticks_per_call=%lu ns=%lu",
                     label, iters, (unsigned long)per, (unsigned long)ns);
}

static void stage6_timing(int iters)
{
    rk68_trace("stage 6: timing iters=%d tb_hz=%lu", iters,
               (unsigned long)g_tb_hz);
    bench_show("empty loop", iters, bench_empty(iters));
    bench_show("FindTask Run68K", iters, bench_findtask_lvo(iters));
    bench_show("FindTask LP stub", iters, bench_findtask_lp(iters));
    bench_show("DateStamp Run68K", iters, bench_datestamp_lvo(iters));
    bench_show("minigl LVO -30", iters, bench_minigl_lvo(iters));
    bench_show("chooser PP_Stack", iters, bench_chooser_c(iters));
    if (iters >= 100)
        bench_show("Exec loopback msg", iters, bench_exec_loopback(iters));
}

/* ------------------------------------------------------------------ */

static void stage7_alternatives(BOOL lowlevel)
{
    RK68Call c;
    ULONG regs[1];
    ULONG first, second;

    /* Revalidation: repeat the LVO call after a Delay and require the same
     * table pointer (catches a transient/one-shot failure). */
    regs[0] = 0;
    rk68_exec_lvo(&c, g_ppcBase, g_sysBase, LVO_EXEC_FINDTASK, regs, 1);
    first = rk68_result(&c);
    Delay(1);
    rk68_exec_lvo(&c, g_ppcBase, g_sysBase, LVO_EXEC_FINDTASK, regs, 1);
    second = rk68_result(&c);
    rk68_trace("stage 7: revalidate FindTask first=%08lx second=%08lx",
               (unsigned long)first, (unsigned long)second);
    trace_passfail("revalidate FindTask", first != 0 && first == second,
                   c.status, second);

    if (lowlevel) {
        RK68Call lc;

        rk68_trace("stage 7: Run68KLowLevel attempt");
        lc = c;
        lc.status = rk68_vector_lvo(g_ppcBase, RK68_LOWLEVEL_LVO, &lc.args);
        rk68_trace("stage 7: LowLevel status=%lu ret=%08lx",
                   (unsigned long)lc.status,
                   (unsigned long)rk68_result(&lc));
        rk68_result_line("Run68KLowLevel: status=%lu ret=%08lx",
                         (unsigned long)lc.status,
                         (unsigned long)rk68_result(&lc));
    }
}

/* ------------------------------------------------------------------ */

static void stage9_dangerous(BOOL doAsync, BOOL doRaw, BOOL doWaitfor)
{
    if (doWaitfor) {
        RK68Call c;
        ULONG regs[1], ret;

        /* KNOWN WEDGE (run #1): WaitFor68K after a synchronous Run68K blocks
         * the task forever - it does NOT return PPERR_WAITERR. Opt-in only;
         * requires a cold reboot. */
        rk68_trace("stage 9: DANGEROUS WaitFor68K after sync Run68K "
                   "(known to wedge; last trace if it hangs)");
        regs[0] = 0;
        rk68_exec_lvo(&c, g_ppcBase, g_sysBase, LVO_EXEC_FINDTASK, regs, 1);
        ret = rk68_vector_lvo(g_ppcBase, RK68_WAITFOR68K_LVO, &c.args);
        rk68_trace("stage 9: WaitFor68K returned status=%lu (unexpected)",
                   (unsigned long)ret);
    }

    if (doAsync) {
        RK68Call c;
        ULONG regs[1];

        rk68_trace("stage 9: DANGEROUS async Run68K (expect trap/halt)");
        regs[0] = 0;
        rk68_init(&c);
        c.args.PP_Code   = g_sysBase;
        c.args.PP_Offset = LVO_EXEC_FINDTASK;
        c.args.PP_Flags  = PPF_ASYNC;
        c.args.PP_Regs[PPREG_A1] = 0;
        c.args.PP_Regs[PPREG_A6] = (ULONG)g_sysBase;
        c.status = rk68_vector(g_ppcBase, &c.args);
        rk68_trace("stage 9: async returned status=%lu (unexpected)",
                   (unsigned long)c.status);
        rk68_result_line("async Run68K: returned status=%lu",
                         (unsigned long)c.status);
    }

    if (doRaw && g_table) {
        ULONG entry = rk68_entry(g_table, MGLD_ENTRY_mglChooseWindowMode);
        typedef void (*raw_fn)(void);

        rk68_trace("stage 9: DANGEROUS raw call to 68k entry %08lx",
                   (unsigned long)entry);
        ((raw_fn)entry)();
        rk68_trace("stage 9: raw call returned (unexpected)");
    }
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    BOOL lowlevel = FALSE, doAsync = FALSE, doRaw = FALSE;
    BOOL doWaitfor = FALSE;
    int iters = 1000;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    rk68_trace_reset();
    rk68_trace("=== run68k bridge probe start ===");

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-lowlevel"))
            lowlevel = TRUE;
        else if (!strcmp(argv[i], "-async"))
            doAsync = TRUE;
        else if (!strcmp(argv[i], "-raw"))
            doRaw = TRUE;
        else if (!strcmp(argv[i], "-waitfor"))
            doWaitfor = TRUE;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            iters = atoi(argv[++i]);
    }

    g_ppcBase = (void *)PowerPCBase;
    g_sysBase = (struct Library *)SysBase;
    g_dosBase = (struct Library *)DOSBase;

    printf("run68k bridge probe\n");
    printf("  PowerPCBase=%08lx SysBase=%08lx DOSBase=%08lx\n",
           (unsigned long)g_ppcBase, (unsigned long)g_sysBase,
           (unsigned long)g_dosBase);
    rk68_result_line("=== run68k bridge probe ===");
    rk68_result_line("env ppc=%08lx sys=%08lx dos=%08lx",
                     (unsigned long)g_ppcBase, (unsigned long)g_sysBase,
                     (unsigned long)g_dosBase);

    if (!stage0_env()) {
        printf("FAIL: PowerPCBase is NULL\n");
        return 20;
    }

    g_tb_hz = rk68_tb_hz();
    rk68_trace("stage 0: tb_hz=%lu", (unsigned long)g_tb_hz);
    rk68_result_line("timebase_hz=%lu", (unsigned long)g_tb_hz);

    if (!stage1_open())
        return 20;

    stage2_sanity();
    stage3_datestamp();
    stage4_minigl_lvo();
    stage5_stack_calls();
    stage6_timing(iters);
    stage7_alternatives(lowlevel);

    rk68_result_line("failures=%d", g_fail);
    rk68_trace("=== results written (failures=%d) ===", g_fail);
    printf("run68k bridge probe done: failures=%d, trace %s\n",
           g_fail, RK68_TRACE_FILE);

    stage9_dangerous(doAsync, doRaw, doWaitfor);
    return g_fail == 0 ? 0 : 1;
}
