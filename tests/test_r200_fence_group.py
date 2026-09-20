"""W-80 fence-coalescing bookkeeping (host side, CPU only).

python3 tests/test_r200_fence_group.py

Extracts the production ReadFenceGroupSize / PublishRetired / RecordFence /
CloseFenceGroup / AdvanceRetired functions and compiles them against a
minimal mock context. The invariants under test are the ones a hardware
mistake would corrupt:

- retirement never advances over a pending fence-less run (else the PPC
  would reuse vertex segments the GPU can still be reading);
- CloseFenceGroup submits exactly one closing fence and folds the pending
  segment mask into its record;
- failures do not clear the pending state;
- the env parser is conservative (anything odd means group=1).

Generated files stay in /tmp/opencode and are removed.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HOST = (ROOT / "backend_r200/minigl_ppc_host.c").read_text()


def function(signature):
    start = HOST.index(signature)
    end = HOST.index("\n}\n", start) + 3
    return HOST[start:end]


parser = function("static ULONG ReadFenceGroupSize(")
publish = function("static void PublishRetired(")
record = function("static BOOL RecordFence(")
close = function("static BOOL CloseFenceGroup(")
advance = function("static void AdvanceRetired(")

MOCKS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef unsigned char UBYTE;
typedef void *APTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MGLPPC_INFLIGHT_SLOTS 8UL
#define MGLPPC_RING_SLOTS 8UL
#define GVF_GLOBAL_ONLY 0x100
#define CACRF_ClearD 0
typedef const char *CONST_STRPTR;

static const char *environment;
static LONG GetVar(CONST_STRPTR name, UBYTE *buffer, LONG capacity, LONG flags)
{
    assert(!strcmp(name, "MGLPPC_FENCE_GROUP") && flags == GVF_GLOBAL_ONLY);
    if (!environment)
        return -1;
    {
        size_t length = strlen(environment);
        memcpy(buffer, environment,
               length < (size_t)capacity ? length : (size_t)capacity);
        return (LONG)(length < (size_t)capacity ? length
                                                : (size_t)capacity - 1);
    }
}

typedef struct MGLPPCRingControlV1 { ULONG retired; } MGLPPCRingControlV1;
struct Radeon3DDevice { int unused; };

struct State {
    volatile MGLPPCRingControlV1 *ringControl;
    ULONG ringCompleted, cpEnabled, fenceCount;
    ULONG fenceValue[MGLPPC_INFLIGHT_SLOTS];
    ULONG fenceSerial[MGLPPC_INFLIGHT_SLOTS];
    ULONG fenceMask[MGLPPC_INFLIGHT_SLOTS];
    ULONG queuedSegmentMask, semanticFence;
    ULONG fenceGroupSize, fenceGroupCount, pendingSegMask, pendingUnfenced;
    BOOL fenceCoalesce, cpFailed;
    ULONG ringDeviceToken;
};
typedef struct State MGLPPCHostState;

static BOOL closingOk;
static ULONG closingFence;
static unsigned closingCalls;
static BOOL Radeon3DSubmitFence(struct Radeon3DDevice *device, ULONG *fenceOut)
{
    assert(device != NULL && fenceOut != NULL);
    ++closingCalls;
    if (!closingOk)
        return FALSE;
    *fenceOut = closingFence;
    return TRUE;
}
static unsigned clears;
static void CacheClearE(APTR address, ULONG bytes, ULONG flags)
{
    (void)address; (void)bytes; (void)flags;
    ++clears;
}
'''

CHECKS = r'''
static MGLPPCRingControlV1 ring;
static MGLPPCHostState state;

static void resetState(void)
{
    memset(&state, 0, sizeof(state));
    memset(&ring, 0, sizeof(ring));
    state.ringControl = &ring;
    state.ringCompleted = 100;
    state.fenceCoalesce = TRUE;
    state.ringDeviceToken = 1;
    closingCalls = 0;
    closingOk = TRUE;
    closingFence = 555;
    clears = 0;
}

/* 1. Retirement must hold still while a fence-less run is in flight. */
static void retirement(void)
{
    resetState();
    AdvanceRetired(&state);
    assert(ring.retired == 100);            /* nothing outstanding: consume=done */

    resetState();
    state.pendingUnfenced = 1;
    AdvanceRetired(&state);
    assert(ring.retired == 0);              /* unfenced work: nothing proven */

    resetState();
    state.pendingUnfenced = 3;
    state.fenceCount = 1;
    state.fenceSerial[0] = 40;
    AdvanceRetired(&state);
    assert(ring.retired == 40);             /* closed prefix still publishes */

    resetState();
    state.fenceCount = 2;
    state.fenceSerial[0] = 10;
    state.fenceSerial[1] = 90;
    AdvanceRetired(&state);
    assert(ring.retired == 10);             /* oldest fence wins */
}

/* 2. CloseFenceGroup: no-op unless a coalesced run is open; otherwise one
 *    closing fence carrying the union of the pending segment masks. */
static void closing(void)
{
    resetState();
    assert(CloseFenceGroup(&state));
    assert(closingCalls == 0);

    resetState();
    state.fenceCoalesce = FALSE;
    state.pendingUnfenced = 2;
    assert(CloseFenceGroup(&state));
    assert(closingCalls == 0 && state.pendingUnfenced == 2);

    resetState();
    state.pendingUnfenced = 3;
    state.pendingSegMask = 0x5;
    state.fenceGroupCount = 3;
    assert(CloseFenceGroup(&state));
    assert(closingCalls == 1);
    assert(state.fenceCount == 1);
    assert(state.fenceSerial[0] == 100);
    assert(state.fenceMask[0] == 0x5);
    assert(state.pendingUnfenced == 0 && state.pendingSegMask == 0);
    assert(state.fenceGroupCount == 0 && !state.cpFailed);

    /* Failure paths keep the pending state so a later drain can retry. */
    resetState();
    state.pendingUnfenced = 2;
    state.pendingSegMask = 0x2;
    closingOk = FALSE;
    assert(!CloseFenceGroup(&state));
    assert(closingCalls == 1 && state.pendingUnfenced == 2);

    resetState();
    state.pendingUnfenced = 2;
    closingFence = 0;
    assert(!CloseFenceGroup(&state));
    assert(state.pendingUnfenced == 2);

    /* Fence queue full: the close cannot be recorded. */
    resetState();
    state.pendingUnfenced = 1;
    state.fenceCount = MGLPPC_INFLIGHT_SLOTS;
    assert(!CloseFenceGroup(&state));
    assert(state.cpFailed);
}

/* 3. Env parser: anything but a clean 2..8 means the legacy group of 1. */
static void parsing(void)
{
    static const struct { const char *text; ULONG group; } cases[] = {
        {NULL, 1}, {"", 1}, {"1", 1}, {"0", 1},
        {"2", 2}, {"4", 4}, {"8", 8}, {"9", 8}, {"64", 8},
        {" 4", 1}, {"4 ", 1}, {"04", 4}, {"+4", 1}, {"-2", 1},
        {"x", 1}, {"4x", 1}, {"0x4", 1},
        {"123456789012345", 1},              /* buffer-full prefix */
        {"99999999999999999999999999", 1},   /* too long: reject, never overflow */
    };
    unsigned index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        environment = cases[index].text;
        assert(ReadFenceGroupSize() == cases[index].group);
    }
}

int main(void)
{
    parsing();
    retirement();
    closing();
    printf("FENCE_GROUP checks passed\n");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="r200-fence-group-",
                                 dir="/tmp/opencode") as directory:
    path = Path(directory)
    source = path / "check.c"
    source.write_text(MOCKS + "\n" + parser + "\n" + publish + "\n" + record +
                      "\n" + close + "\n" + advance + "\n" + CHECKS)
    binary = path / "check"
    subprocess.run([
        "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wno-error=pointer-to-int-cast", "-Wno-pointer-to-int-cast",
        "-Wno-error=int-to-pointer-cast", "-Wno-int-to-pointer-cast",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        str(source), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
