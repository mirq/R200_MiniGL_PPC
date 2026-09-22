/*
 * r200_run68k_context_probe.c - can a PPC WarpOS task own and drive a 68k
 * MiniGL context directly through Run68K?
 *
 * This is the end-to-end companion to r200_run68k_bridge_probe: it creates
 * a real 68k minigl.library context, calls GL entry points through the
 * dispatch table with the stack ABI, presents, and deletes the context.
 *
 * Stages (trace file Work:wosbuild/run68k_ctx_trace.log):
 *   0  environment
 *   1  open minigl.library, fetch dispatch table via LVO -30
 *   2  context-free choosers (window mode/depth/buffers/vertex buffer)
 *   3  MGLCreateContext(0,0,320,240)  <- 4-argument stack order check
 *   4  GLGetString(GL_VERSION/RENDERER/EXTENSIONS)
 *   5  GLClear + GLGetError
 *   6  MGLSwitchDisplay (present)              [disable with -nopresent]
 *   7  timing: GLGetString, GLClear
 *   8  MGLDeleteContext + CloseLibrary
 *
 * Exit code 0 when all executed stages passed.
 */

/* dedicated trace/result files for this probe */
#define RK68_TRACE_FILE  "Work:wosbuild/run68k_ctx_trace.log"
#define RK68_RESULT_FILE "Work:wosbuild/run68k_ctx_result.txt"

#include "r200_run68k_probe.h"

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

#define LVO_EXEC_FINDTASK   (-0x126)

/* The 68k R200 backend uses its OWN enum numbering (minigl_r200.h), which
 * differs from standard GL values. Use the backend's numbers. */
#define GL_COLOR_BUFFER_BIT 0x00000001UL   /* minigl_r200.h */
#define GL_DEPTH_BUFFER_BIT 0x00000002UL   /* minigl_r200.h */
#define GL_VENDOR           205UL          /* minigl_r200.h */
#define GL_VERSION          206UL          /* minigl_r200.h */
#define GL_RENDERER         148UL          /* minigl_r200.h */
#define GL_EXTENSIONS       57UL           /* minigl_r200.h */

#define CTX_TRACE_FILE  RK68_TRACE_FILE

static int  g_fail;
static ULONG g_tb_hz;
static void *g_ppcBase;
static struct Library *g_sysBase;
static struct Library *g_dosBase;
static struct Library *g_miniGLBase;
static void *g_table;
static void *g_ctx;

static void passfail(const char *what, BOOL ok, ULONG status, ULONG value)
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

static ULONG entry_off(ULONG off)
{
    return rk68_read32((const char *)g_table + off);
}

static void ctx_call(RK68Call *c, void *entry, const ULONG *argv, int argc)
{
    rk68_call_c(c, g_ppcBase, entry, g_miniGLBase, argv, argc);
}

/* ------------------------------------------------------------------ */

static BOOL open_and_table(void)
{
    RK68Call c;
    ULONG abi, size;

    g_ppcBase = (void *)PowerPCBase;
    g_sysBase = (struct Library *)SysBase;
    g_dosBase = (struct Library *)DOSBase;
    if (!g_ppcBase) {
        rk68_trace("stage 0: PowerPCBase NULL");
        return FALSE;
    }
    rk68_trace("stage 0: ppc=%08lx sys=%08lx dos=%08lx",
               (unsigned long)g_ppcBase, (unsigned long)g_sysBase,
               (unsigned long)g_dosBase);

    g_miniGLBase = OpenLibrary((STRPTR)"minigl.library", RK68_MGL_VERSION);
    if (!g_miniGLBase) {
        rk68_trace("stage 1: open minigl.library FAILED");
        g_fail++;
        return FALSE;
    }

    rk68_call_lvo(&c, g_ppcBase, g_miniGLBase, RK68_MGL_LVO_GETDISPATCH,
                  NULL, 0);
    g_table = (void *)rk68_result(&c);
    if (c.status != 0 || !g_table) {
        rk68_trace("stage 1: LVO -30 status=%lu table=%08lx",
                   (unsigned long)c.status, (unsigned long)g_table);
        g_fail++;
        return FALSE;
    }
    abi  = rk68_read32((const char *)g_table + 0);
    size = rk68_read32((const char *)g_table + 4);
    rk68_trace("stage 1: table=%08lx abi=%lu size=%lu",
               (unsigned long)g_table, (unsigned long)abi,
               (unsigned long)size);
    rk68_result_line("table abi=%lu size=%lu", (unsigned long)abi,
                     (unsigned long)size);
    passfail("dispatch table", abi == MGLD_ABI_VERSION_68K &&
             size >= MGLD_ABI_SIZE_MIN, c.status, abi);
    return TRUE;
}

static void choosers(BOOL fullscreen)
{
    RK68Call c;
    ULONG arg;

    arg = fullscreen ? 0 : 1;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_mglChooseWindowMode),
             &arg, 1);
    passfail("chooseWindowMode(TRUE)", c.status == 0, c.status,
             rk68_result(&c));

    arg = 16;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_mglChoosePixelDepth), &arg, 1);
    passfail("choosePixelDepth(16)", c.status == 0, c.status,
             rk68_result(&c));

    arg = 2;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_mglChooseNumberOfBuffers), &arg, 1);
    passfail("chooseNumberOfBuffers(2)", c.status == 0, c.status,
             rk68_result(&c));

    arg = 1000;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_mglChooseVertexBufferSize), &arg, 1);
    passfail("chooseVertexBufferSize(1000)", c.status == 0, c.status,
             rk68_result(&c));
}

static BOOL create_context(void)
{
    RK68Call c;
    ULONG argv[4];
    ULONG ctxField;

    /* mglCreateContext(offx, offy, w, h) - 4 stack arguments. */
    argv[0] = 0;
    argv[1] = 0;
    argv[2] = 320;
    argv[3] = 240;

    rk68_trace("stage 3: MGLCreateContext(0,0,320,240)");
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_MGLCreateContext), argv, 4);
    g_ctx = (void *)rk68_result(&c);
    ctxField = rk68_read32((const char *)g_table + 16);
    {
        ULONG ctxValue = ctxField ? rk68_read32((const char *)ctxField) : 0;
        rk68_trace("stage 3: status=%lu ctx=%08lx ctxStorage=%08lx "
                   "currentContext=%08lx",
                   (unsigned long)c.status, (unsigned long)g_ctx,
                   (unsigned long)ctxField, (unsigned long)ctxValue);
        ctxField = ctxValue;   /* compare the current context value */
    }

    /* The raw Lib_mglCreateContext entry returns the context but does NOT
     * set *currentContext: that is the client wrapper's job
     * (minigl_dispatch.h: mglCreateContext -> *(MiniGLDispatch->currentContext)).
     * A direct consumer must publish it itself if it relies on the global. */
    passfail("MGLCreateContext", c.status == 0 && g_ctx != NULL,
             c.status, (ULONG)g_ctx);

    /* A second call without delete must be rejected, not crash. */
    if (g_ctx) {
        RK68Call c2;
        ULONG second;

        ctx_call(&c2, (void *)entry_off(MGLD_ENTRY_MGLCreateContext),
                 argv, 4);
        second = rk68_result(&c2);
        rk68_trace("stage 3: second create ctx=%08lx", (unsigned long)second);
    }
    return g_ctx != NULL;
}

static void print_string_entry(const char *label, ULONG name)
{
    RK68Call c;
    ULONG argv[2];
    const char *s;
    char buf[80];
    int i;

    argv[0] = (ULONG)g_ctx;
    argv[1] = name;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLGetString), argv, 2);
    s = (const char *)rk68_result(&c);

    if (c.status != 0 || !s) {
        rk68_trace("stage 4: %s status=%lu ptr=%08lx", label,
                   (unsigned long)c.status, (unsigned long)s);
        passfail(label, FALSE, c.status, (ULONG)s);
        return;
    }
    for (i = 0; i < (int)sizeof(buf) - 1 && s[i]; i++)
        buf[i] = s[i];
    buf[i] = 0;

    rk68_trace("stage 4: %s = '%s'", label, buf);
    rk68_result_line("%s = %s", label, buf);
    passfail(label, TRUE, c.status, (ULONG)s);
}

static void draw_triangle(void)
{
    RK68Call c;
    ULONG a[5];
    int i;
    static const float tri[3][3] = {
        { -0.75f, -0.65f, 0.0f },
        {  0.75f, -0.65f, 0.0f },
        {  0.00f,  0.75f, 0.0f }
    };
    static const float col[3][3] = {
        { 1.0f, 0.2f, 0.2f },
        { 0.2f, 1.0f, 0.3f },
        { 0.3f, 0.4f, 1.0f }
    };

    a[0] = (ULONG)g_ctx;
    a[1] = MGLD_GL_TRIANGLES;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLBegin), a, 2);
    passfail("GLBegin(TRIANGLES)", c.status == 0, c.status, rk68_result(&c));

    for (i = 0; i < 3; i++) {
        a[0] = (ULONG)g_ctx;
        a[1] = rk68_fbits(col[i][0]);
        a[2] = rk68_fbits(col[i][1]);
        a[3] = rk68_fbits(col[i][2]);
        a[4] = rk68_fbits(1.0f);
        ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLColor4f), a, 5);

        a[0] = (ULONG)g_ctx;
        a[1] = rk68_fbits(tri[i][0]);
        a[2] = rk68_fbits(tri[i][1]);
        a[3] = rk68_fbits(tri[i][2]);
        a[4] = rk68_fbits(1.0f);
        ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLVertex4f), a, 5);
    }

    a[0] = (ULONG)g_ctx;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLEnd), a, 1);
    passfail("GLEnd", c.status == 0, c.status, rk68_result(&c));

    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLFinish), a, 1);
    passfail("GLFinish", c.status == 0, c.status, rk68_result(&c));
}

static void clear_and_error(void)
{
    RK68Call c;
    ULONG argv[2];
    ULONG err;

    argv[0] = (ULONG)g_ctx;
    argv[1] = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLClear), argv, 2);
    passfail("GLClear(color|depth)", c.status == 0, c.status,
             rk68_result(&c));

    {
        ULONG one = (ULONG)g_ctx;
        ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLGetError), &one, 1);
        err = rk68_result(&c);
        rk68_trace("stage 5: GLGetError=%lu", (unsigned long)err);
        passfail("GLGetError", err == 0, c.status, err);
    }
}

static void present(void)
{
    RK68Call c;
    ULONG one = (ULONG)g_ctx;

    rk68_trace("stage 6: MGLSwitchDisplay (may block; last trace if wedged)");
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_MGLSwitchDisplay), &one, 1);
    rk68_trace("stage 6: MGLSwitchDisplay status=%lu", 
               (unsigned long)c.status);
    passfail("MGLSwitchDisplay", c.status == 0, c.status, rk68_result(&c));
}

static void timing(int iters)
{
    RK68Call c;
    ULONG argv[2], t0, t1, per, ns, i;
    ULONG ticks;

    rk68_trace("stage 7: timing iters=%d tb_hz=%lu", iters,
               (unsigned long)g_tb_hz);
    rk68_result_line("timebase_hz=%lu", (unsigned long)g_tb_hz);

    argv[0] = (ULONG)g_ctx;
    argv[1] = GL_VERSION;
    t0 = rk68_mftb();
    for (i = 0; i < (ULONG)iters; i++)
        ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLGetString), argv, 2);
    t1 = rk68_mftb();
    ticks = t1 - t0;
    per = iters > 0 ? ticks / (ULONG)iters : 0;
    ns = (g_tb_hz && iters > 0)
       ? (ULONG)((double)ticks * 1.0e9 / ((double)g_tb_hz * iters)) : 0;
    rk68_trace("bench GLGetString tpc=%lu ns=%lu", (unsigned long)per,
               (unsigned long)ns);
    rk68_result_line("bench GLGetString   tpc=%lu ns=%lu",
                     (unsigned long)per, (unsigned long)ns);

    argv[1] = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
    t0 = rk68_mftb();
    for (i = 0; i < (ULONG)iters; i++)
        ctx_call(&c, (void *)entry_off(MGLD_ENTRY_GLClear), argv, 2);
    t1 = rk68_mftb();
    ticks = t1 - t0;
    per = iters > 0 ? ticks / (ULONG)iters : 0;
    ns = (g_tb_hz && iters > 0)
       ? (ULONG)((double)ticks * 1.0e9 / ((double)g_tb_hz * iters)) : 0;
    rk68_trace("bench GLClear tpc=%lu ns=%lu", (unsigned long)per,
               (unsigned long)ns);
    rk68_result_line("bench GLClear       tpc=%lu ns=%lu",
                     (unsigned long)per, (unsigned long)ns);
}

static void destroy(void)
{
    RK68Call c;
    ULONG one = (ULONG)g_ctx;

    if (!g_ctx)
        return;
    ctx_call(&c, (void *)entry_off(MGLD_ENTRY_MGLDeleteContext), &one, 1);
    rk68_trace("stage 8: MGLDeleteContext status=%lu", 
               (unsigned long)c.status);
    passfail("MGLDeleteContext", c.status == 0, c.status, rk68_result(&c));
    g_ctx = NULL;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    BOOL doPresent = TRUE;
    BOOL fullscreen = FALSE;
    int iters = 200;
    int hold = 0;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    rk68_trace_reset();
    rk68_trace("=== run68k context probe start ===");
    rk68_result_line("=== run68k context probe ===");

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-nopresent"))
            doPresent = FALSE;
        else if (!strcmp(argv[i], "-fs"))
            fullscreen = TRUE;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            iters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-hold") && i + 1 < argc)
            hold = atoi(argv[++i]);
    }

    printf("run68k context probe\n");

    if (!open_and_table())
        return 20;

    g_tb_hz = rk68_tb_hz();
    rk68_trace("tb_hz=%lu", (unsigned long)g_tb_hz);

    choosers(fullscreen);
    if (!create_context()) {
        printf("context creation failed - see %s\n", CTX_TRACE_FILE);
        CloseLibrary(g_miniGLBase);
        return 1;
    }

    print_string_entry("GL_VENDOR", GL_VENDOR);
    print_string_entry("GL_VERSION", GL_VERSION);
    print_string_entry("GL_RENDERER", GL_RENDERER);
    print_string_entry("GL_EXTENSIONS", GL_EXTENSIONS);
    clear_and_error();
    draw_triangle();

    if (doPresent) {
        present();
        if (hold > 0) {
            rk68_trace("stage 6: holding presented frame for %d s", hold);
            Delay((LONG)hold * 50L);
        }
    } else {
        rk68_trace("stage 6: present skipped (-nopresent)");
    }

    if (iters > 0)
        timing(iters);

    destroy();
    CloseLibrary(g_miniGLBase);

    rk68_result_line("failures=%d", g_fail);
    rk68_trace("=== context probe done (failures=%d) ===", g_fail);
    printf("run68k context probe done: failures=%d (trace %s)\n",
           g_fail, CTX_TRACE_FILE);
    return g_fail == 0 ? 0 : 1;
}
