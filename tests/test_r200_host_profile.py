"""Compile the production quiet profiler and service loop with CPU-only stubs."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
host = (root / "backend_r200/minigl_ppc_host.c").read_text()
start = host.index("#if MGLPPC_PROFILE\nstatic ULONG HostClockHz;")
profile = host[start:host.index("\n#endif", start) + len("\n#endif")]
start = host.index("#if MGLPPC_PROFILE\n#define HOST_START")
macros = host[start:host.index("\n#endif", start) + len("\n#endif")]
start = host.index("#if MGLPPC_PROFILE\n        /* The successful bridge")
enable = host[start:host.index("\n#endif", start) + len("\n#endif")]
start = host.index("    while (state.running) {")
loop = host[start:host.index("#if MGLPPC_PROFILE\n    HostWallTicks", start)]
assert not re.search(r"\b(?:printf|sprintf|fflush|HostTraceLine)\s*\(", loop)
assert "MGLPPC_PROFILE ||" not in host
assert "host: hb" not in host and "lastReport" not in loop
assert "loopStart" not in loop
assert "if (HostExecuteCount && HostClockHz)" not in host
assert "inclusive=1 mutually_exclusive=0" in host
assert "ticks=sampled_unscaled" in host
assert "idle=max(wall-busy,0) idle_is_cpu_idle=0" in host
assert "HostControlTicks + HostBarrierTicks +" in host
assert "HostTotalTicks + HostRingTicks + HostMailboxTicks" in host
create = host[host.index("case MGLPPC_CMD_CREATE_CONTEXT:"):
              host.index("case MGLPPC_CMD_CLEAR_PRESENT:")]
assert create.index("MGLPPC_STATUS_CONTEXT_FAILED") < create.index(enable)
assert create.index("MGLPPC_STATUS_UNSUPPORTED", create.index("QueryBackendBridge")) < create.index(enable)
assert "message->" not in enable
cases = []
for command in ("DESTROY_CONTEXT", "SHUTDOWN"):
    start = host.index(f"case MGLPPC_CMD_{command}:")
    case = host[start:host.index("\n    case ", start + 1)]
    assert case.index("HostNativeProfileCapture(state)") < case.index("DiscardSemantic(state)")
    assert "HostNativeProfileReport" not in case
    cases.append(case)
assert cases[0].index("DiscardSemantic(state)") < cases[0].index("mglDeleteContext()")
assert host.count("HostNativeProfileCapture(state);") == 2
assert host.count("HostNativeProfileCapture(&state);") == 1
assert host.count("HostNativeProfileReport();") == 1
handler = host[host.index("static void HandleMessage("):host.index("static BOOL PollMailbox(")]
assert "HostNativeProfileReport" not in handler
assert "HostNativeProfile" not in host[host.index("static ULONG RingPoll("):
                                       host.index("static void ProbeStore(")]
capture = host[host.index("static void HostNativeProfileCapture("):
               host.index("static void HostNativeProfileReport(")]
assert not re.search(r"\b(?:printf|fprintf|sprintf|snprintf|puts|fflush|HostTraceLine|"
                     r"Open|Write|Close|AllocVec|malloc|calloc|HostClock|ProbeTicks|ReadEClock)\s*\(", capture)
report = profile[profile.index("static void HostNativeProfileReport("):]
assert not re.search(r"\b(?:glGetError|glGetIntegerv|glHint|HostClock|ReadEClock)\s*\(", report)
start = host.index("    RemPort(hostPort);", host.index(loop))
cleanup = host[start:host.index("    if (HostClockHz) {", start)] + "\n#endif\n"
ordered = ["RemPort(hostPort)", "DeleteMsgPort(hostPort)",
           "HostNativeProfileCapture(&state)", "DiscardSemantic(&state)",
           "mglDeleteContext()", "DiscardProbe(&state)", "DiscardUpload(&state)",
           "CloseDevice(", "MiniGLClose()", "HostNativeProfileReport()"]
assert [cleanup.index(call) for call in ordered] == sorted(cleanup.index(call) for call in ordered)
assert host.index("HostWallTicks = HostClock() - HostWallStart;") < start

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <libraries/minigl_perf.h>
typedef uint32_t ULONG, GLenum;
typedef int32_t GLint;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define GL_NO_ERROR 0
#define GL_NICEST 1
#define MGLPPC_MESSAGE_HEADER_SIZE 16
#define MGLPPC_CMD_EXECUTE_SEMANTIC_SLOT 1
#define MGLPPC_CMD_SEMANTIC_BARRIER 2
#define MGLPPC_CMD_DESTROY_CONTEXT 3
#define MGLPPC_CMD_SHUTDOWN 4
#define MGLPPC_STATUS_OK 0
#define MGLPPC_STATUS_NO_CONTEXT 1
#define MGLPPC_STATUS_GL_ERROR 2
#define MGLPPC_STATUS_BUSY 3
struct Message { ULONG mn_Length; };
typedef struct {
    struct Message execMessage;
    ULONG command, contextId, status;
} MGLPPCMessage;
struct Ring { ULONG error, head; };
typedef struct {
    int running, insidePrimitive, contextWindowed, backendOpen;
    void *context, *contextWindow;
    ULONG contextId, semanticFence, ringCompleted, semanticBoundTexture;
    BOOL cpSegmentLeased, cpFailed;
    ULONG cpDispatches, cpDwords;
    struct Ring *ringControl;
} MGLPPCHostState;
struct EClockVal { ULONG ev_lo; };
struct IORequest { int unused; };
static struct IORequest TimerRequest;
static void *TimerBase = (void *)1;
static ULONG now, clockReads, passes, step, queries, hintCalls, nativeId;
static ULONG deletes, portRemoved, portDeleted, printCalls;
static BOOL nativeLive, outputAllowed, backendLive = TRUE;
static GLenum glError, hintError, failQuery;
static int TestPrintf(const char *format, ...) {
    assert(outputAllowed && !nativeLive && !backendLive && !TimerBase);
    assert(portRemoved && portDeleted);
    ++printCalls;
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}
static int TestFlush(FILE *stream) {
    assert(outputAllowed && !nativeLive && !backendLive && !TimerBase);
    return fflush(stream);
}
#define printf TestPrintf
#define fflush TestFlush
static ULONG ReadEClock(struct EClockVal *clock) {
    assert(TimerBase && !outputAllowed);
    ++clockReads; clock->ev_lo = now; return 1000000;
}
static GLenum glGetError(void) {
    assert(nativeLive && backendLive && !outputAllowed);
    GLenum error = glError; glError = 0; return error;
}
static void glHint(GLenum name, GLenum mode) {
    assert(nativeLive && backendLive && !outputAllowed);
    assert(name == MGL_PERF_COUNTERS_HINT && mode == GL_NICEST && !glError);
    ++hintCalls; glError = hintError;
}
static void glGetIntegerv(GLenum name, GLint *value) {
    assert(nativeLive && backendLive && !outputAllowed);
    assert(!glError); ++queries;
    if (name == failQuery) { glError = 88; return; }
    *value = name == MGL_PERF_CLOCK_RATE ? 1000000 :
             name == MGL_PERF_FRAMES ? (GLint)(nativeId == 1 ? UINT32_MAX : nativeId * 100) : 7;
}
static BOOL PollMailbox(MGLPPCHostState *state) {
    (void)state; now += step; return 0;
}
static void *GetMsg(void *port) { (void)port; return NULL; }
static void ReplyMsg(struct Message *message) { (void)message; assert(0); }
static ULONG RingPoll(MGLPPCHostState *state) { (void)state; return 0; }
static void PollQueuedFence(MGLPPCHostState *state) { (void)state; now += step * 2; }
static void PollContextEvents(MGLPPCHostState *state) {
    now += step * 3;
    if (!(++passes % 32)) state->running = 0;
}
static void Delay(ULONG ticks) { (void)ticks; assert(0); }
'''
code += profile + "\n" + macros
code += "\nstatic void EnableCounters(void) {\n" + enable + "\n}\n"
code += r'''
static void StartContext(MGLPPCHostState *state, ULONG id) {
    assert(!nativeLive && !outputAllowed);
    state->context = state; state->contextId = id;
    state->running = state->backendOpen = 1;
    nativeLive = TRUE; nativeId = id; glError = 0;
    EnableCounters();
}
static BOOL DiscardSemantic(MGLPPCHostState *state) {
    assert(!outputAllowed);
    if (state->context) {
#if MGLPPC_PROFILE
        assert(HostNativeReported);
#endif
        (void)glGetError(); /* Simulate the existing cleanup error consumption. */
    }
    return TRUE;
}
static void mglDeleteContext(void) {
    assert(nativeLive && !outputAllowed); nativeLive = FALSE; ++deletes;
}
static void DiscardProbe(MGLPPCHostState *state) { (void)state; assert(!outputAllowed); }
static void DiscardUpload(MGLPPCHostState *state) { (void)state; assert(!outputAllowed); }
static void RemPort(void *port) { (void)port; assert(!outputAllowed); ++portRemoved; }
static void DeleteMsgPort(void *port) {
    (void)port; assert(portRemoved && !outputAllowed); ++portDeleted;
}
static void CloseDevice(struct IORequest *request) {
    (void)request; assert(!nativeLive && !outputAllowed);
}
static void MiniGLClose(void) {
    assert(!nativeLive && !TimerBase && !outputAllowed);
    backendLive = FALSE; outputAllowed = TRUE;
}
static void HandleMessage(MGLPPCMessage *message, MGLPPCHostState *state) {
    message->status = MGLPPC_STATUS_OK;
    switch (message->command) {
'''
# Exercise the actual lifecycle case bodies, not a no-op HandleMessage stub.
code += "\n".join(cases) + "\ndefault: assert(0);\n}\n}\n"
code += "static int ShutdownCleanup(MGLPPCHostState state) {\nvoid *hostPort = NULL;\n"
code += cleanup + "return 0;\n}\n"
code += "static void RunLoops(void) {\nMGLPPCHostState state = {0};\n"
code += "void *hostPort = NULL;\nstate.running = 1;\n" + loop + "}\n"
code += r'''
int main(int argc, char **argv) {
    MGLPPCHostState state = {0};
    MGLPPCMessage message = {0};
    if (argc > 1) {
        state.backendOpen = state.running = 1;
        if (!strcmp(argv[1], "live")) {
            StartContext(&state, 42);
            glError = 91; failQuery = MGL_PERF_CLOCK_RATE;
        }
        message.command = MGLPPC_CMD_SHUTDOWN;
        HandleMessage(&message, &state);
        HandleMessage(&message, &state);
        assert(!state.running && message.status == MGLPPC_STATUS_OK && !printCalls);
        assert(ShutdownCleanup(state) == 0);
        assert(!nativeLive && !clockReads);
        return 0;
    }
    step = 7; now = UINT32_MAX - 5;
    RunLoops(); /* Includes clock wrap in a sampled loop below. */
#if MGLPPC_PROFILE
    assert(HostPollLoops == 32 && clockReads == 12);
    assert(HostMailboxPollCalls == 2 && HostMailboxPollTicks == 14);
    assert(HostFencePollCalls == 2 && HostFencePollTicks == 28);
    assert(HostEventPollCalls == 2 && HostEventPollTicks == 42);
#endif
    step = 0;
    RunLoops(); /* Zero-tick calls still count and earlier samples survive. */
#if MGLPPC_PROFILE
    assert(HostPollLoops == 64 && clockReads == 24);
    assert(HostMailboxPollCalls == 4 && HostMailboxPollTicks == 14);
    assert(HostFencePollCalls == 4 && HostFencePollTicks == 28);
    assert(HostEventPollCalls == 4 && HostEventPollTicks == 42);
    HostPollLoops = 15; now = UINT32_MAX - 5; step = 7;
    RunLoops();
    assert(HostPollLoops == 47 && clockReads == 36);
    assert(HostMailboxPollCalls == 6 && HostMailboxPollTicks == 28);
    assert(HostFencePollCalls == 6 && HostFencePollTicks == 56);
    assert(HostEventPollCalls == 6 && HostEventPollTicks == 84);
#else
    assert(clockReads == 0);
#endif
    assert(!queries && !hintCalls);
    message.command = MGLPPC_CMD_DESTROY_CONTEXT;
    HandleMessage(&message, &state);
    assert(message.status == MGLPPC_STATUS_NO_CONTEXT && !deletes && !queries);
    StartContext(&state, 1);
#if MGLPPC_PROFILE
    assert(hintCalls == 1 && !glError && !HostNativeEnableError);
#endif
    glError = 89; failQuery = MGL_PERF_WAIT_TICKS;
    message.contextId = 2;
    HandleMessage(&message, &state); /* Invalid destroy must not capture or clear errors. */
    assert(message.status == MGLPPC_STATUS_NO_CONTEXT && nativeLive && !queries);
    assert(glError == 89);
    message.contextId = 1;
    HandleMessage(&message, &state);
    assert(message.status == MGLPPC_STATUS_OK && !state.context && !nativeLive && deletes == 1);
#if MGLPPC_PROFILE
    assert(queries == 24 && !glError);
    HostNativeProfileCapture(&state); /* No context, no duplicate snapshot. */
    assert(queries == 24 && HostNativeRecordCount == 1);
#endif
    hintError = 90; failQuery = 0;
    StartContext(&state, 2); /* Recreated context; failed hint is diagnostic only. */
#if MGLPPC_PROFILE
    assert(hintCalls == 2 && !glError && HostNativeEnableError == 90);
    HostNativeProfileCapture(&state);
    HostNativeProfileCapture(&state); /* Live-context guard also prevents repeats. */
#endif
    message.contextId = 2;
    HandleMessage(&message, &state);
    assert(!state.context && !nativeLive && message.status == MGLPPC_STATUS_OK);
#if MGLPPC_PROFILE
    assert(queries == 48 && !glError && HostNativeRecordCount == 2);
#endif
    hintError = 0;
    for (ULONG id = 3; id <= 9; ++id) {
        StartContext(&state, id);
#if MGLPPC_PROFILE
        if (id == 9) {
            HostNativeProfileCapture(&state);
            HostNativeProfileCapture(&state); /* A dropped context counts only once. */
        }
#endif
        message.contextId = id;
        HandleMessage(&message, &state);
        assert(!state.context && !nativeLive && message.status == MGLPPC_STATUS_OK);
    }
    StartContext(&state, 10);
    message.command = MGLPPC_CMD_SHUTDOWN;
    HandleMessage(&message, &state);
    HandleMessage(&message, &state);
    assert(!state.running && nativeLive && message.status == MGLPPC_STATUS_OK && !printCalls);
#if MGLPPC_PROFILE
    assert(HostNativeRecordCount == 8 && HostNativeOverflow == 2 && queries == 192);
#else
    assert(!queries && !hintCalls && !glError);
#endif
    ULONG clocksBefore = clockReads;
    assert(ShutdownCleanup(state) == 0); /* Actual main cleanup, then the only profile output call. */
    assert(!nativeLive && deletes == 10 && clockReads == clocksBefore);
#if MGLPPC_PROFILE
    assert(HostNativeRecordCount == 8 && HostNativeOverflow == 2 && queries == 192);
#endif
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="r200-profile-", dir="/tmp/opencode") as directory:
    source = Path(directory) / "test.c"
    source.write_text(code)
    for enabled in (0, 1):
        binary = Path(directory) / f"test{enabled}"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-Wno-unused-variable",
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                        f"-DMGLPPC_PROFILE={enabled}",
                        "-I" + str(root / "backend_r200/include"),
                        str(source), "-o", str(binary)], check=True)
        output = subprocess.run([str(binary)], check=True, text=True, capture_output=True).stdout
        if not enabled:
            assert not output
        else:
            assert output.count("scope=whole_context_including_setup") == 8
            assert "cache_capacity=8 records=8 overflow_contexts=2" in output
            assert "overflow_policy=drop_new output=after_cleanup" in output
            assert "enable_error=0 pending_gl_error=89" in output
            assert "frames=4294967295 query_error=0" in output
            assert "wait_ticks=0 query_error=88" in output
            assert "context=1 query_errors=1" in output
            assert "enable_error=90 pending_gl_error=0" in output
            assert "context=2 query_errors=0" in output
            assert "context=2 frames=200 query_error=0" in output
            assert "context=8 frames=800 query_error=0" in output
            assert "context=9 " not in output and "context=10 " not in output
        for mode in ("empty", "live"):
            output = subprocess.run([str(binary), mode], check=True, text=True, capture_output=True).stdout
            if not enabled:
                assert not output
            elif mode == "empty":
                assert "cache_capacity=8 records=0 overflow_contexts=0" in output
                assert "scope=whole_context_including_setup" not in output
            else:
                assert "cache_capacity=8 records=1 overflow_contexts=0" in output
                assert output.count("scope=whole_context_including_setup") == 1
                assert "context=42 frames=4200 query_error=0" in output
                assert "pending_gl_error=91" in output and "clock_hz=0 query_error=88" in output
print("Host profile: sampling, disabled path, production destroy/shutdown ordering, deferred output, errors and bounded cache passed")
