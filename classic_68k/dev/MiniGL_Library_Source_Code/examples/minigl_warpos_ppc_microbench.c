/*
 * WarpOS/PPC per-vertex cost micro-benchmark.
 *
 * Times, on the real 7410, the client-side components of the gears
 * benchmark's per-vertex scene cost so optimization effort lands where the
 * cycles actually are:
 *
 *   1. double sin+cos via the linked ppcmath (what fcos/fsin expand to)
 *   2. float FMA chain (vbcc -O3 scalar codegen)
 *   3. indirect call through a 4-float-arg function pointer (dispatch shape)
 *
 * Timing: on-chip mftb, calibrated against the timer.device EClock once
 * (no OS calls inside the measured loops).  vbcc's stdio has no %f support,
 * so all outputs are integers; us_x100 = microseconds x 100 per iteration.
 */
#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <libraries/minigl_ppc_protocol.h>
#ifdef MINIGLPPC_SEMANTIC_PROFILE
#include <libraries/minigl_ppc_semantic_profile.h>
#endif

/* Frontend context layout introspection (archive-internal struct). */
extern ULONG R200PPCDebugAlignFacts(ULONG *facts, ULONG maxFacts);
#include <stdio.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static ULONG RawClock(void) = "\tmftb\tr3";

extern void Delay(LONG ticks);

static struct MsgPort BenchPort;
static struct timerequest BenchReq;

#define ITERS 20000UL

static double sink_d;
static float sink_f;
static volatile ULONG sink_u;

/* dispatch-shape target: same ABI as the frontend's GLVertex4f */
static void FakeVertex4f(float x, float y, float z, float w)
{
    sink_f += x + y + z + w;
}

/* external-linkage twin: cannot be inlined across TUs */
void ExtVertex4f(float x, float y, float z, float w)
{
    sink_f += x + y + z + w;
}

typedef void (*Vertex4fFn)(float, float, float, float);

static ULONG OpenBenchTimer(void)
{
    memset(&BenchPort, 0, sizeof(BenchPort));
    BenchPort.mp_Node.ln_Type = NT_MSGPORT;
    BenchPort.mp_Flags = PA_IGNORE;
    memset(&BenchReq, 0, sizeof(BenchReq));
    BenchReq.tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    BenchReq.tr_node.io_Message.mn_ReplyPort = &BenchPort;
    BenchReq.tr_node.io_Message.mn_Length = sizeof(BenchReq);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK,
                   (struct IORequest *)&BenchReq, 0))
        return 0;
    TimerBase = BenchReq.tr_node.io_Device;
    return 1;
}

int main(void)
{
    ULONG ec0, ec1, tb0, tb1, i;
    struct EClockVal ecv;
    double tb_hz;

    if (!OpenBenchTimer()) return 12;

    printf("PPC micro-bench: calibrating mftb against EClock...\n");
    fflush(stdout);

    ReadEClock(&ecv); ec0 = ecv.ev_lo;
    tb0 = RawClock();
    Delay(2);
    tb1 = RawClock();
    ReadEClock(&ecv); ec1 = ecv.ev_lo;
    {
        ULONG ecTicks = ec1 - ec0;
        ULONG tbTicks = tb1 - tb0;
        ULONG ecHz = ReadEClock(&ecv);
        if (!ecTicks || !tbTicks || !ecHz) {
            printf("FAIL: calibration clocks did not advance\n");
            return 5;
        }
        tb_hz = (double)tbTicks * (double)ecHz / (double)ecTicks;
        printf("calib: ec_ticks=%lu tb_ticks=%lu ec_hz=%lu tb_hz=%lu "
               "(window_us=%lu)\n",
               (unsigned long)ecTicks, (unsigned long)tbTicks,
               (unsigned long)ecHz, (unsigned long)tb_hz,
               (unsigned long)((double)ecTicks * 1000000.0 / (double)ecHz));
        if (tb_hz < 1.0) { printf("FAIL: zero timebase\n"); return 5; }
    }

    /* 1. double sin+cos, gear-argument shape (2*M_PI*i/30) */
    {
        ULONG t0 = RawClock();
        double acc = 0.0;
        for (i = 0; i < ITERS; ++i) {
            double a = 2.0 * M_PI * (double)i / 30.0;
            acc += sin(a) + cos(a);
        }
        sink_d = acc;
        printf("BENCH d_sincos iters=%lu total_us_x100=%lu us_x100_per_iter=%lu\n",
               (unsigned long)ITERS,
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0),
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0
                               / (double)ITERS));
        fflush(stdout);
    }

    /* 2. float FMA chain (vertex math shape) */
    {
        ULONG t0 = RawClock();
        float acc = 0.5f, x = 1.0f;
        for (i = 0; i < ITERS; ++i) {
            float a = 2.0f * (float)M_PI * (float)i / 30.0f;
            acc += a * x + acc * 0.5f;
        }
        sink_f = acc;
        printf("BENCH f_fma iters=%lu total_us_x100=%lu us_x100_per_iter=%lu\n",
               (unsigned long)ITERS,
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0),
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0
                               / (double)ITERS));
        fflush(stdout);
    }

    /* 2b/2c: misaligned vs aligned float stores. -amiga-align puts
     * structs at 2-byte alignment; a misaligned lfs/stfs on 7410 traps
     * into the OS alignment handler. If misaligned stores cost ~us each
     * while aligned ones cost ~ns, that is the whole gears mystery. */
    {
        static UBYTE buf[2048];
        GLfloat *mis = (GLfloat *)(buf + 2);
        GLfloat *ali = (GLfloat *)(buf + 64);
        ULONG t0, n = 20000UL, k;
        GLfloat acc = 0.5f;
        for (k = 0; k < 1024; ++k) ali[k] = 0.0f;
        t0 = RawClock();
        for (i = 0; i < n; ++i)
            mis[i & 1023] = acc + (GLfloat)i;
        printf("BENCH f_store_misaligned iters=%lu total_us_x100=%lu "
               "us_x100_per_store=%lu\n",
               (unsigned long)n,
               (unsigned long)((double)(RawClock() - t0) / tb_hz
                               * 100000000.0),
               (unsigned long)((double)(RawClock() - t0) / tb_hz
                               * 100000000.0 / (double)n));
        fflush(stdout);
        t0 = RawClock();
        for (i = 0; i < n; ++i)
            ali[i & 1023] = acc + (GLfloat)i;
        printf("BENCH f_store_aligned iters=%lu total_us_x100=%lu "
               "us_x100_per_store=%lu\n",
               (unsigned long)n,
               (unsigned long)((double)(RawClock() - t0) / tb_hz
                               * 100000000.0),
               (unsigned long)((double)(RawClock() - t0) / tb_hz
                               * 100000000.0 / (double)n));
        fflush(stdout);
        sink_f = mis[3] + ali[3];
    }

    /* 3. indirect dispatch call, GLVertex4f shape */
    {
        Vertex4fFn fn = FakeVertex4f;
        ULONG t0 = RawClock();
        for (i = 0; i < ITERS; ++i) {
            fn(0.5f, 0.25f, 0.125f, 1.0f);
        }
        sink_u = (ULONG)sink_f;
        printf("BENCH dispatch4f iters=%lu total_us_x100=%lu us_x100_per_iter=%lu\n",
               (unsigned long)ITERS,
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0),
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0
                               / (double)ITERS));
        fflush(stdout);
    }

    /* 3b. NON-inlinable indirect call (volatile pointer + external target) */
    {
        static volatile Vertex4fFn vfn = ExtVertex4f;
        ULONG t0 = RawClock();
        for (i = 0; i < ITERS; ++i) {
            vfn(0.5f, 0.25f, 0.125f, 1.0f);
        }
        sink_u = (ULONG)sink_f;
        printf("BENCH extcall4f iters=%lu total_us_x100=%lu us_x100_per_iter=%lu\n",
               (unsigned long)ITERS,
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0),
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0
                               / (double)ITERS));
        fflush(stdout);
    }

    /* 3c. direct external call (non-inlinable, static-link resolved) */
    {
        ULONG t0 = RawClock();
        for (i = 0; i < ITERS; ++i) {
            ExtVertex4f(0.5f, 0.25f, 0.125f, 1.0f);
        }
        sink_u = (ULONG)sink_f;
        printf("BENCH extdirect4f iters=%lu total_us_x100=%lu us_x100_per_iter=%lu\n",
               (unsigned long)ITERS,
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0),
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0
                               / (double)ITERS));
        fflush(stdout);
    }

    /* 3d. double sin alone and cos alone (isolates each from the pair) */
    {
        ULONG t0 = RawClock();
        double acc = 0.0;
        for (i = 0; i < ITERS; ++i)
            acc += sin(2.0 * M_PI * (double)i / 30.0);
        sink_d = acc;
        printf("BENCH d_sin iters=%lu total_us_x100=%lu us_x100_per_iter=%lu\n",
               (unsigned long)ITERS,
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0),
               (unsigned long)((double)(RawClock() - t0) / tb_hz * 100000000.0
                               / (double)ITERS));
        fflush(stdout);
    }

    /* 4. GL capture: real frontend + ring, gears vertex shape.
     * Links against the semantic frontend; requires the 68k host running. */
    if (MiniGLOpen()) {
        static const ULONG glVerts = 2000UL, glReps = 5UL;
        ULONG t0, rep, usX100;
        mglChooseWindowMode(GL_TRUE);
        mglChoosePixelDepth(32);
        if (!mglCreateContext(0, 0, 320, 240)) {
            printf("GL_SETUP context=fail\n");
        } else {
            static GLfloat probeF[64];
            static UBYTE probeB[64];
            static struct { UBYTE b; GLfloat f; } probeS;
            ULONG facts[8];
            ULONG nf = R200PPCDebugAlignFacts(facts, 8UL);
            printf("ALIGN client: static_f=%lu static_b=%lu "
                   "static_struct=%lu structfloat=%lu\n",
                   (unsigned long)((ULONG)probeF & 3UL),
                   (unsigned long)((ULONG)probeB & 3UL),
                   (unsigned long)((ULONG)&probeS & 3UL),
                   (unsigned long)((ULONG)&probeS.f & 3UL));
            if (nf)
                printf("ALIGN frontend: ctx=%lu vertices=%lu "
                       "screenV=%lu pendingDraw=%lu sizeofVertex=%lu "
                       "ctxMinusV=%lu\n",
                       (unsigned long)facts[0], (unsigned long)facts[1],
                       (unsigned long)facts[2], (unsigned long)facts[3],
                       (unsigned long)facts[4], (unsigned long)facts[5]);
            fflush(stdout);
            static const ULONG capVerts = 2000UL, capReps = 5UL;
            ULONG t0, rep, usX100;
            glEnable(GL_DEPTH_TEST);

            /* 4-preempt: calibrated spin loop, ~300 ms window. Effective
             * cycles/iter vs the short-loop f_fma result exposes PPC
             * timeslice starvation (WarpOS preemption). */
            {
                ULONG spins = 0;
                t0 = RawClock();
                while ((RawClock() - t0) < 300UL * 24569UL) ++spins;
                usX100 = (ULONG)((double)(RawClock() - t0) / tb_hz
                                 * 100000000.0);
                printf("BENCH preempt_probe wall_us_x100=%lu spins=%lu "
                       "ns_per_spin_x100=%lu\n",
                       (unsigned long)usX100, (unsigned long)spins,
                       (unsigned long)((double)usX100 * 10.0 /
                                       (double)spins));
                fflush(stdout);
            }

            /* warmup one loop (context/segment lazily initialized) */
            glBegin(GL_TRIANGLE_STRIP);
            for (i = 0; i < 64; ++i)
                glVertex3f(0.5f, 0.5f, 0.0f);
            glEnd();
            (void)glGetError();

            /* 4a-capture: capture ONLY, one open begin, no flush */
            for (rep = 0; rep < capReps; ++rep) {
                glBegin(GL_TRIANGLE_STRIP);
                t0 = RawClock();
                for (i = 0; i < capVerts; ++i)
                    glVertex3f(0.5f, 0.5f, 0.0f);
                usX100 = (ULONG)((double)(RawClock() - t0) / tb_hz
                                 * 100000000.0);
                glEnd();
                printf("BENCH capture_only rep=%lu verts=%lu "
                       "total_us_x100=%lu us_x100_per_vertex=%lu\n",
                       (unsigned long)rep, (unsigned long)capVerts,
                       (unsigned long)usX100,
                       (unsigned long)((double)usX100 / (double)capVerts));
                fflush(stdout);
            }
            (void)glGetError();

            /* 4a2-capture with fog enabled (GL_EXP2) */
            glEnable(GL_FOG);
            glFogi(GL_FOG_MODE, GL_EXP2);
            glFogf(GL_FOG_DENSITY, 0.5f);
            for (rep = 0; rep < 2; ++rep) {
                glBegin(GL_TRIANGLE_STRIP);
                t0 = RawClock();
                for (i = 0; i < capVerts; ++i)
                    glVertex3f(0.5f, 0.5f, 0.0f);
                usX100 = (ULONG)((double)(RawClock() - t0) / tb_hz
                                 * 100000000.0);
                glEnd();
                printf("BENCH capture_fog rep=%lu verts=%lu "
                       "total_us_x100=%lu us_x100_per_vertex=%lu\n",
                       (unsigned long)rep, (unsigned long)capVerts,
                       (unsigned long)usX100,
                       (unsigned long)((double)usX100 / (double)capVerts));
                fflush(stdout);
            }
            glDisable(GL_FOG);
            (void)glGetError();

            /* 4a4: glDrawArrays — one call, frontend-internal per-vertex
             * loop. If this is fast, batching works around the per-call
             * penalty; if it is slow, the frontend code region itself is
             * fetch-penalized. */
            {
                static GLfloat verts[3000 * 3];
                static GLfloat norms[3000 * 3];
                GLint v;
                for (v = 0; v < 3000; ++v) {
                    verts[v * 3 + 0] = 0.5f;
                    verts[v * 3 + 1] = 0.5f;
                    verts[v * 3 + 2] = 0.0f;
                    norms[v * 3 + 0] = 0.0f;
                    norms[v * 3 + 1] = 0.0f;
                    norms[v * 3 + 2] = 1.0f;
                }
                glEnableClientState(GL_VERTEX_ARRAY);
                glEnableClientState(GL_NORMAL_ARRAY);
                glVertexPointer(3, GL_FLOAT, 0, verts);
                glNormalPointer(GL_FLOAT, 0, norms);
                for (rep = 0; rep < 3; ++rep) {
                    t0 = RawClock();
                    glDrawArrays(GL_TRIANGLES, 0, 3000);
                    (void)glGetError();
                    usX100 = (ULONG)((double)(RawClock() - t0) / tb_hz
                                     * 100000000.0);
                    printf("BENCH drawarrays rep=%lu verts=3000 "
                           "total_us_x100=%lu us_x100_per_vertex=%lu\n",
                           (unsigned long)rep, (unsigned long)usX100,
                           (unsigned long)((double)usX100 / 3000.0));
                    fflush(stdout);
                }
                glDisableClientState(GL_VERTEX_ARRAY);
                glDisableClientState(GL_NORMAL_ARRAY);
            }

            /* 4a3-capture with trig in the loop (client math shape) */
            for (rep = 0; rep < 2; ++rep) {
                double acc = 0.0;
                glBegin(GL_TRIANGLE_STRIP);
                t0 = RawClock();
                for (i = 0; i < capVerts; ++i) {
                    double a = 2.0 * M_PI * (double)i / 30.0;
                    acc += sin(a) + cos(a);
                    glVertex3f(0.5f, 0.5f, 0.0f);
                }
                usX100 = (ULONG)((double)(RawClock() - t0) / tb_hz
                                 * 100000000.0);
                glEnd();
                sink_d = acc;
                printf("BENCH capture_trig rep=%lu verts=%lu "
                       "total_us_x100=%lu us_x100_per_vertex=%lu\n",
                       (unsigned long)rep, (unsigned long)capVerts,
                       (unsigned long)usX100,
                       (unsigned long)((double)usX100 / (double)capVerts));
                fflush(stdout);
            }
            (void)glGetError();

            /* 4a: glVertex3f only */
            t0 = RawClock();
            for (rep = 0; rep < glReps; ++rep) {
                glBegin(GL_TRIANGLE_STRIP);
                for (i = 0; i < glVerts; ++i)
                    glVertex3f(0.5f, 0.5f, 0.0f);
                glEnd();
            }
            (void)glGetError();
            usX100 = (ULONG)((double)(RawClock() - t0) / tb_hz
                             * 100000000.0);
            printf("BENCH gl_vtx3f verts=%lu reps=%lu total_us_x100=%lu "
                   "us_x100_per_vertex=%lu\n",
                   (unsigned long)glVerts, (unsigned long)glReps,
                   (unsigned long)usX100,
                   (unsigned long)(usX100 /
                                   (double)(glVerts * glReps)));
            fflush(stdout);

            /* 4b: glNormal3f + glVertex3f (gears shape) */
            t0 = RawClock();
            for (rep = 0; rep < glReps; ++rep) {
                glBegin(GL_TRIANGLE_STRIP);
                for (i = 0; i < glVerts; ++i) {
                    glNormal3f(0.0f, 0.0f, 1.0f);
                    glVertex3f(0.5f, 0.5f, 0.0f);
                }
                glEnd();
            }
            (void)glGetError();
            usX100 = (ULONG)((double)(RawClock() - t0) / tb_hz
                             * 100000000.0);
            printf("BENCH gl_norm_vtx verts=%lu reps=%lu total_us_x100=%lu "
                   "us_x100_per_vertex=%lu\n",
                   (unsigned long)glVerts, (unsigned long)glReps,
                   (unsigned long)usX100,
                   (unsigned long)(usX100 /
                                   (double)(glVerts * glReps)));
            fflush(stdout);

            /* 4c: begin/end block overhead */
            t0 = RawClock();
            for (rep = 0; rep < 200UL; ++rep) {
                glBegin(GL_TRIANGLES);
                glVertex3f(0.0f, 0.0f, 0.0f);
                glVertex3f(1.0f, 0.0f, 0.0f);
                glVertex3f(0.0f, 1.0f, 0.0f);
                glEnd();
            }
            (void)glGetError();
            usX100 = (ULONG)((double)(RawClock() - t0) / tb_hz
                             * 100000000.0);
            printf("BENCH gl_blocks blocks=200 total_us_x100=%lu "
                   "us_x100_per_block=%lu\n",
                   (unsigned long)usX100,
                   (unsigned long)(usX100 / 200.0));
            fflush(stdout);

            mglDeleteContext();
#ifdef MINIGLPPC_SEMANTIC_PROFILE
        {
            const MGLPPCSemanticPlatformProfile *sp = R200PPCPlatformProfile();
            ULONG rate = sp->clockRate;
#define USX(t) ((unsigned long)((double)(t) / (double)rate * 1000000.0))
            printf("SEM_PROFILE publishes=%lu send_us=%lu crc_us=%lu "
                   "flush_us=%lu waits=%lu wait_us=%lu barriers=%lu "
                   "barrier_us=%lu stall_us=%lu chunks=%lu chunk_us=%lu "
                   "tb_hz=%lu\n",
                   (unsigned long)sp->publishCount,
                   USX(sp->publishSendTicks), USX(sp->publishCrcTicks),
                   USX(sp->publishFlushTicks),
                   (unsigned long)sp->replyWaitCount,
                   USX(sp->replyWaitTicks),
                   (unsigned long)sp->barrierCount,
                   USX(sp->barrierTicks),
                   USX(sp->ringStallTicks),
                   (unsigned long)sp->chunkCount, USX(sp->chunkTicks),
                   (unsigned long)rate);
#undef USX
            R200PPCTimelineDump();
        }
#endif
        }
        MiniGLClose();
    } else {
        printf("GL_SETUP open=fail (host running?) -- GL benches skipped\n");
    }

    printf("DONE sink_int=%lu\n",
           (unsigned long)(sink_d + (double)sink_f));
    CloseDevice((struct IORequest *)&BenchReq);
    return 0;
}
