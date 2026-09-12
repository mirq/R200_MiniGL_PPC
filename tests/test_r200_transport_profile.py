"""CPU-only checks of the production PPC teardown report, frame wrapper and yield helper."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
frontend = (root / "backend_r200/library/minigl_r200_warpos_compile_probe.c").read_text()
header = (root / "backend_r200/include/libraries/minigl_ppc_semantic_profile.h").read_text()
include = root / "backend_r200/include"
makefile = (root / "Makefile.dll.r200.gcc").read_text()
assert re.search(r"^MGLPPC_AUTOREPORT\s*\?=\s*0$", makefile, re.M)
assert "-DR200_PERF_AUTOREPORT=$(MGLPPC_AUTOREPORT)" in makefile
assert "-DR200_PERF_AUTOREPORT=$(MGLPPC_PROFILE)" not in makefile
assert "#define MGLPPC_CLIENT_TRACE 0" in frontend
assert frontend.count('fopen("DH2:mglprof/dll-profile.log", "w")') == 1


def function(signature):
    start = frontend.index(signature)
    end = frontend.index("{", start) + 1
    depth = 1
    while depth:
        depth += (frontend[end] == "{") - (frontend[end] == "}")
        end += 1
    return frontend[start:end] + "\n"


profile_match = re.search(r"typedef struct MGLPPCSemanticPlatformProfile \{.*?"
                          r"\} MGLPPCSemanticPlatformProfile;", header, re.S)
assert profile_match
profile_type = profile_match.group()
fields = re.findall(r"ULONG (\w+);", profile_type)
groups = {
    "PPC_TRANSPORT_CLOCK": {"clock_hz_legacy": "clockRate", "session_ticks": "sessionTicks"},
    "PPC_PUBLISH": {"count": "publishCount", "send_ticks": "publishSendTicks",
                    "stall_ticks": "publishStallTicks", "flush_ticks": "publishFlushTicks",
                    "crc_ticks": "publishCrcTicks"},
    "PPC_BARRIER": {"count": "barrierCount", "ticks": "barrierTicks",
                    "stall_ticks": "barrierStallTicks", "ring_stall_ticks": "ringStallTicks",
                    "drain_ticks": "drainTicks"},
    "PPC_REPLY_WAIT": {"count": "replyWaitCount", "ticks": "replyWaitTicks",
                       "max_ticks": "replyWaitMaxTicks"},
    "PPC_CONTROL": {"count": "controlCount", "ticks": "controlTicks"},
    "PPC_CHUNK": {"count": "chunkCount", "ticks": "chunkTicks"},
    "PPC_TEXTURE": {"calls": "textureCalls", "successes": "textureSuccesses",
                    "bytes": "textureBytes", "ticks": "textureTicks",
                    "direct_calls": "textureDirectCalls", "direct_ticks": "textureDirectTicks",
                    "shared_calls": "textureSharedCalls", "chunked_calls": "textureChunkedCalls",
                    "barrier_calls": "textureBarrierCalls", "barrier_ticks": "textureBarrierTicks"},
    "PPC_STALL_SPLIT": {"publish_enters": "publishStallEnters", "publish_nosleep": "publishStallNoSleep",
                         "publish_sleeps": "publishStallSleeps", "barrier_enters": "barrierStallEnters",
                         "barrier_nosleep": "barrierStallNoSleep", "barrier_sleeps": "barrierStallSleeps",
                         "drain_enters": "drainStallEnters", "drain_nosleep": "drainStallNoSleep",
                         "drain_sleeps": "drainStallSleeps", "rotate_enters": "rotateStallEnters",
                         "rotate_nosleep": "rotateStallNoSleep", "rotate_sleeps": "rotateStallSleeps",
                         "rotate_ticks": "rotateStallTicks"},
}
assert sorted(fields) == sorted(field for group in groups.values() for field in group.values())
teardown = function("void R200PPCDispatchDeleteContext(void)")
report = teardown[teardown.index("const MGLPPCSemanticPlatformProfile *profile ="):
                  teardown.index("fclose(report);") + len("fclose(report);")]
# Lifetime values, requested duration, and separate DLL-lifetime yield statistics.
assert re.findall(r"%[\w.]+", report) == ["%lu"] * (len(fields) + 9)
assert "glHint(" not in report and "glGet" not in report
platform_open = function("BOOL R200PPCPlatformOpen(void)")
reset_end = platform_open.index("    R200PPCPlatformResetClientDefaults();")
assert platform_open.index("memset(&PlatformProfile,") < platform_open.index(
    "PlatformProfileStart = PlatformProfileClock64();") < reset_end
assert platform_open.index("PlatformSessionStart = PerfClock();") > platform_open.index(
    "PlatformCall(MGLPPC_CMD_HELLO,")
assert frontend.count("PlatformProfileStart =") == 1
assert frontend.count("PlatformProfileClock64()") == 2
lifetime_clock = function("static unsigned long long PlatformProfileClock64(void)")
assert r'"mftbu %0\n\tmftb %1\n\tmftbu %2"' in lifetime_clock
# Only the register reads are mocked; exercise the production retry/combine logic.
lifetime_clock, replacements = re.subn(
    r'__asm__ volatile\(.*?: : "memory"\);',
    "ReadTimebase(&upper, &lower, &again);", lifetime_clock, flags=re.S)
assert replacements == 1

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <libraries/minigl_ppc_yield.h>
typedef uint32_t ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define R200_PERF_AUTOREPORT 0
#define R200_FRONTEND_LIBRARY 1
#define SUBMIT_SLOT_COUNT 2
#define MGLPPC_RING_SLOTS 8
#define MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS 1
#define MGLPPC_CMD_DESTROY_CONTEXT 2
#define R200PPCTraceUL(...) ((void)0)
#define R200PPCTraceUL2(...) ((void)0)
#define R200PPCTraceStr(...) ((void)0)
#define R200PPCTraceClose(...) ((void)0)
struct MiniGLR200Context {
    BOOL insideBegin, streamSegments;
    struct { BOOL valid; } segment;
    ULONG segmentPoolCount, alive;
};
static struct MiniGLR200Context proofContext, *R200PPCContext;
struct Ring { ULONG error; };
static struct {
    BOOL opened, registered, mailboxAbandoned, nativeBindingKnown;
    BOOL ibSmokeFailed, cpFailed, cpEnabled;
    ULONG contextId, epoch;
    struct { ULONG value[8]; } control;
    struct Ring *ring;
    void *mailbox, *windowHandle;
    void *ringSlots[MGLPPC_RING_SLOTS], *ringSlotAllocations[MGLPPC_RING_SLOTS];
} FrontendPlatform;
static unsigned mode, step, frees, frames, opens, writes, closes, clockReads;
static unsigned tbSamples;
static BOOL tbRetry;
static unsigned long long tbNow, teardownTicks;
static BOOL mailboxReady;
static char output[4096];
static size_t outputUsed;
static BOOL PlatformMailboxReady(void) { return mailboxReady; }
static BOOL PlatformMailboxWait(void) { return FALSE; }
static void MGLPPCSemanticReplayFree(void) { assert(++step == 1); }
static BOOL FlushScreenVertices(struct MiniGLR200Context *ctx) {
    assert(ctx == &proofContext && ++step == 2); return TRUE;
}
static BOOL SubmitAndWait(struct MiniGLR200Context *ctx) {
    assert(ctx == &proofContext && ++step == 3); return TRUE;
}
static BOOL R200PPCPlatformBarrier(struct MiniGLR200Context *ctx, BOOL present) {
    assert(ctx == &proofContext && !present && ++step == 4); return TRUE;
}
static BOOL R200PPCPlatformWaitSlot(struct MiniGLR200Context *ctx, ULONG slot) {
    assert(ctx == &proofContext && ++step == 5 + slot); return TRUE;
}
static BOOL PlatformCall(ULONG command, ULONG contextId) {
    assert(contextId == 42);
    assert(++step == 6 + command);
    if (command == MGLPPC_CMD_UNREGISTER_SEMANTIC_SLOTS)
        assert(FrontendPlatform.control.value[5] == 7);
    return TRUE;
}
static void FreeVec(void *memory) {
    assert(memory && step == 8);
    if (++frees == MGLPPC_RING_SLOTS) tbNow += teardownTicks;
}
static void Lib_mglSwitchDisplay(void) { ++frames; ++tbNow; }
'''
code += profile_type + r'''
#if MGLPPC_PROFILE
static MGLPPCSemanticPlatformProfile PlatformProfile;
void MGLPPCYieldStats(ULONG *calls, ULONG *ticks, ULONG *maximum) {
    assert(step == 8);
    *calls = mode == 4 ? 0 : 2;
    *ticks = mode == 4 ? 0 : 248;
    *maximum = mode == 4 ? 0 : 200;
}
static unsigned long long PlatformProfileStart;
static ULONG PlatformClockRate = 123456789, PlatformSessionStart;
static void ReadTimebase(ULONG *upper, ULONG *lower, ULONG *again) {
    ++tbSamples;
    *upper = *again = (ULONG)(tbNow >> 32);
    *lower = (ULONG)tbNow;
    if (tbRetry) {
        --*upper; /* Simulate a low-word rollover between the two upper reads. */
        tbRetry = FALSE;
    }
}
static ULONG PerfClock(void) {
    assert(step == 8 && frees == MGLPPC_RING_SLOTS);
    ++clockReads;
    return 21;
}
static FILE *ReportOpen(const char *path, const char *flags) {
    assert(step == 8 && frees == MGLPPC_RING_SLOTS && proofContext.alive);
    assert(R200PPCContext == &proofContext && clockReads == 1);
    assert(!strcmp(path, "DH2:mglprof/dll-profile.log") && !strcmp(flags, "w"));
    ++opens;
    tbNow += 111; /* Report I/O must not enter this report's lifetime snapshot. */
    return mode == 1 ? NULL : stdout;
}
static int ReportPrint(FILE *file, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
static int ReportPrint(FILE *file, const char *format, ...) {
    va_list args;
    int count;
    assert(file == stdout && proofContext.alive);
    ++writes;
    if (mode == 2) return -1;
    va_start(args, format);
    count = vsnprintf(output + outputUsed, sizeof(output) - outputUsed, format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(output) - outputUsed);
    outputUsed += (size_t)count;
    return count;
}
static int ReportClose(FILE *file) {
    assert(file == stdout && proofContext.alive); ++closes;
    return mode == 3 ? EOF : 0;
}
#define fopen ReportOpen
#define fprintf ReportPrint
#define fclose ReportClose
#endif
'''
code += "#if MGLPPC_PROFILE\n" + lifetime_clock + "#endif\n"
# Exercise the production open/reset prefix; HELLO and other setup take mock time.
code += platform_open[:reset_end] + "FrontendPlatform.opened = TRUE; return TRUE;\n}\n"
code += function("const MGLPPCSemanticPlatformProfile *R200PPCPlatformProfile(void)")
code += teardown + function("void R200PPCDispatchSwitchDisplay(void)")
code += r'''
int main(void) {
    const struct MiniGLR200Context empty = {0};
    unsigned long long expectedStart = 0;
    unsigned samplesBeforeLoop;
    for (mode = 0; mode < 10; ++mode) {
        step = frees = frames = opens = writes = closes = clockReads = 0;
        tbSamples = 0; tbRetry = TRUE;
        outputUsed = 0; output[0] = 0;
        if (mode != 8) {
            memset(&FrontendPlatform, 0, sizeof(FrontendPlatform));
            tbNow = ((3ULL + mode * 4ULL) << 32) | 0xfffffff0ULL;
            expectedStart = tbNow;
        }
        assert(R200PPCPlatformOpen());
#if MGLPPC_PROFILE
        assert(PlatformProfileStart == expectedStart);
        assert(tbSamples == (mode == 8 ? 0u : 2u));
        assert(PlatformProfile.publishCount == (mode == 8 ? 0x80000000u : 0u));
#else
        (void)expectedStart;
        assert(!tbSamples);
#endif
        /* The loop is always 600 ticks. Test UINT32_MAX exactly, long setup
         * before HELLO, combined setup/teardown overflow, then a second
         * context without a platform reset and a real reopen (modes 8/9). */
        if (mode == 5) tbNow += UINT32_MAX - 600ULL;
        if (mode == 6 || mode == 8) tbNow += 0x100000000ULL;
        if (mode == 7) tbNow += 0x80000000ULL;
        teardownTicks = mode == 7 ? 0x80000000ULL :
                        mode == 5 || mode == 6 ? 0 : 31;
        tbRetry = TRUE;
        proofContext.alive = 1;
        R200PPCContext = &proofContext;
        FrontendPlatform.contextId = 42;
        FrontendPlatform.epoch = 7;
        FrontendPlatform.registered = TRUE;
        for (unsigned i = 0; i < MGLPPC_RING_SLOTS; ++i) {
            FrontendPlatform.ringSlots[i] = &proofContext;
            FrontendPlatform.ringSlotAllocations[i] = &proofContext;
        }
#if MGLPPC_PROFILE
        PlatformSessionStart = UINT32_MAX - 9;
        if (mode != 8) {
'''
for index, field in enumerate(fields):
    code += f"            PlatformProfile.{field} = mode == 4 ? 0 : {0x80000000 + index}u;\n"
code += r'''
        } else {
            ++PlatformProfile.publishCount;
        }
#endif
        samplesBeforeLoop = tbSamples;
        for (unsigned i = 0; i < 600; ++i) R200PPCDispatchSwitchDisplay();
        assert(frames == 600 && !opens && !writes && !clockReads);
        assert(tbSamples == samplesBeforeLoop);
        mailboxReady = FALSE;
        R200PPCDispatchDeleteContext();
        assert(R200PPCContext == &proofContext && !step && !opens && !clockReads);
        assert(tbSamples == samplesBeforeLoop);
        mailboxReady = TRUE;
        R200PPCDispatchDeleteContext();
        assert(!R200PPCContext && !FrontendPlatform.contextId);
        assert(!FrontendPlatform.registered && !FrontendPlatform.mailboxAbandoned);
        assert(!memcmp(&proofContext, &empty, sizeof(empty)));
        assert(step == 8 && frees == MGLPPC_RING_SLOTS);
#if MGLPPC_PROFILE
        assert(opens == 1 && clockReads == 1);
        assert(tbSamples == samplesBeforeLoop + 2);
        assert(PlatformProfileStart == expectedStart);
        assert(writes == (mode == 1 ? 0 : 11));
        assert(closes == (mode == 1 ? 0 : 1));
        assert(PlatformProfile.clockRate == PlatformClockRate);
        assert(PlatformProfile.sessionTicks == 31);
        if (mode == 0 || mode >= 4) fputs(output, stdout);
#else
        assert(!opens && !writes && !closes && !clockReads && !outputUsed);
        assert(!tbSamples);
#endif
        R200PPCDispatchDeleteContext();
        assert(step == 8 && frees == MGLPPC_RING_SLOTS);
        assert(opens == (MGLPPC_PROFILE ? 1u : 0u));
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="dll-profile-test-", dir="/tmp/opencode") as directory:
    directory = Path(directory)
    # Compile the production helper as its own translation unit. Only the SDK
    # declarations and WaitTime boundary are stubbed; no kernel code is copied.
    (directory / "exec").mkdir()
    (directory / "powerpc").mkdir()
    (directory / "exec/types.h").write_text(
        "#ifndef TEST_EXEC_TYPES_H\n#define TEST_EXEC_TYPES_H\n"
        "#include <stdint.h>\ntypedef uint32_t ULONG;\n#endif\n")
    (directory / "exec/libraries.h").write_text("#include <exec/types.h>\n")
    (directory / "powerpc/powerpc_protos.h").write_text(
        "#include <exec/types.h>\nULONG WaitTime(ULONG signals, ULONG microseconds);\n")
    yield_source = directory / "yield-test.c"
    yield_source.write_text(r'''
#include <assert.h>
#include <powerpc/powerpc_protos.h>
#include <libraries/minigl_ppc_yield.h>
static ULONG calls;
ULONG WaitTime(ULONG signals, ULONG microseconds) {
    assert(signals == 0UL && microseconds == 100UL);
    ++calls;
    return calls == 1 ? 0UL : UINT32_MAX;
}
int main(void) {
    ULONG count, ticks, maximum;
    assert(MGLPPC_RING_YIELD_US == 100UL);
    assert(MGLPPC_RING_YIELD_SPINS == 131072UL);
    assert((MGLPPC_RING_YIELD_SPINS & (MGLPPC_RING_YIELD_SPINS - 1UL)) == 0);
    assert(MGLPPC_RING_YIELD_SPINS < 5000000UL);
    MGLPPCYield(); assert(calls == 1);
    MGLPPCYield(); assert(calls == 2);
    MGLPPCYieldStats(&count, &ticks, &maximum);
    assert(count == 0 && ticks == 0 && maximum == 0);
    return 0;
}
''')
    yield_binary = directory / "yield-test"
    subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DWARPUP",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-pie", "-no-pie",
                    "-I", str(directory), "-I", str(include),
                    str(root / "backend_r200/client/minigl_ppc_yield.c"), str(yield_source),
                    "-o", str(yield_binary)], check=True, timeout=60, cwd=directory)
    subprocess.run([str(yield_binary)], check=True, timeout=10, cwd=directory)
    print("PPC yield helper: production MGLPPCYield calls WaitTime once with signals=0, microseconds=100")
    # Exercise the production profiling arithmetic too, mocking only the
    # register read. The first interval crosses the 32-bit timebase boundary.
    helper = (root / "backend_r200/client/minigl_ppc_yield.c").read_text()
    clock_instruction = '__asm__ volatile ("mftb %0" : "=r" (value));'
    assert helper.count(clock_instruction) == 1
    helper = '#include <exec/types.h>\nextern ULONG TestYieldClock(void);\n' + helper.replace(
        clock_instruction, "value = TestYieldClock();")
    (directory / "yield-profile.c").write_text(helper)
    yield_source.write_text(r'''
#include <assert.h>
#include <powerpc/powerpc_protos.h>
#include <libraries/minigl_ppc_yield.h>
static ULONG position;
ULONG TestYieldClock(void) {
    static const ULONG values[] = {0xfffffff0UL, 0x20UL, 100UL, 300UL};
    assert(position < 4); return values[position++];
}
ULONG WaitTime(ULONG signals, ULONG microseconds) {
    assert(signals == 0 && microseconds == 100); return 0;
}
int main(void) {
    ULONG count, ticks, maximum;
    MGLPPCYieldStats(&count, &ticks, &maximum);
    assert(!count && !ticks && !maximum && !position);
    MGLPPCYield();
    MGLPPCYieldStats(&count, &ticks, &maximum);
    assert(count == 1 && ticks == 48 && maximum == 48);
    MGLPPCYield();
    MGLPPCYieldStats(&count, &ticks, &maximum);
    assert(count == 2 && ticks == 248 && maximum == 200 && position == 4);
    return 0;
}
''')
    subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DMGLPPC_PROFILE=1", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                    "-I", str(directory), "-I", str(include), str(directory / "yield-profile.c"),
                    str(yield_source), "-o", str(yield_binary)], check=True, timeout=60)
    subprocess.run([str(yield_binary)], check=True, timeout=10)
    for enabled in (0, 1):
        binary = directory / f"test{enabled}"
        subprocess.run(["cc", "-x", "c", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                        "-I", str(directory), "-I", str(include),
                        f"-DMGLPPC_PROFILE={enabled}", "-", "-o", str(binary)],
                       input=code, text=True, check=True)
        output = subprocess.run([str(binary)], text=True, capture_output=True, check=True).stdout
        if not enabled:
            assert not output
            continue
        reports = output.split("PPC_TRANSPORT_PROFILE ")[1:]
        modes = (0, 4, 5, 6, 7, 8, 9)
        assert len(reports) == len(modes)
        for mode, text in zip(modes, reports):
            assert "scope=whole_context_including_setup" in text
            assert "snapshot=after_destroy counters_since=platform_open" in text
            assert "overlapping_nested=1 mutually_exclusive=0" in text
            assert "counter_bits=32 ticks=raw_timebase" in text
            assert "clock_hz_legacy_trusted=0 conversion=client_whole_run_tb_hz" in text
            assert "lifetime_since=profile_reset" in text
            lines = {line.split()[0]: dict(re.findall(r"(\w+)=(\d+)", line))
                     for line in text.splitlines()[1:]}
            lifetime = lines.pop("PPC_TRANSPORT_LIFETIME")
            expected_ticks = {5: 0xffffffff, 6: 0x100000000 + 600,
                              7: 0x100000000 + 600, 8: 0x200000000 + 1342}.get(mode, 631)
            assert int(lifetime["elapsed_ticks_hi"]) == expected_ticks >> 32
            assert int(lifetime["elapsed_ticks_lo"]) == expected_ticks & 0xffffffff
            assert int(lifetime["counter_wrap_risk"]) == (expected_ticks > 0xffffffff)
            yields = lines.pop("PPC_YIELD")
            assert yields == {"requested_us": "100", "calls": "0" if mode == 4 else "2",
                              "ticks": "0" if mode == 4 else "248",
                              "max_ticks": "0" if mode == 4 else "200"}
            assert lines.keys() == groups.keys()
            # Legacy *_sleeps fields count yields, not 50 Hz ticks or a latency
            # bound. The request stays 100 us even in the all-zero-counter case.
            assert int(lines["PPC_STALL_SPLIT"].pop("yield_us")) == 100
            assert int(lines["PPC_STALL_SPLIT"].pop("spin_budget")) == 131072
            assert lines["PPC_STALL_SPLIT"].keys() == groups["PPC_STALL_SPLIT"].keys()
            values = {field: 0 if mode == 4 else 0x80000000 + i for i, field in enumerate(fields)}
            if mode == 8:
                values["publishCount"] += 1
            values.update(clockRate=123456789, sessionTicks=31)
            for name, group in groups.items():
                for key, field in group.items():
                    assert int(lines[name][key]) == values[field], (name, key)
print("PPC transport profile: quiet 600-frame wrapper, all raw uint32 fields, clock/session snapshot, "
      "stable TB retry, lifetime wrap/setup/teardown/recreation/reopen, zero values, 100-us yield metadata, disabled path "
      "and nonfatal open/write/close failures passed")
