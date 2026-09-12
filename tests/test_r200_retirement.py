"""Host-only retirement regression tests; no SDK, hardware, or runtime git use.

python3 tests/test_r200_retirement.py -v
python3 tests/test_r200_retirement.py --regress-fences -v

Compile the actual extracted reapers from BOTH trees with cc ASan/UBSan, for
R200_ASYNC_ROTATION=0 and 1. RecoverDevice is checked for source ordering only.
The default suite also rejects historical-polling mutations using the SAME
cleanup assertions as production. --regress-fences runs that comparison alone.
Service/cache/GPU behavior is mocked, not hardware-validated. Generated files
and binaries live only in /tmp/opencode and are removed on completion.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TREES = {
    "native": ROOT / "classic_68k/dev/MiniGL_Library_Source_Code/library",
    "ppc": ROOT / "backend_r200/library",
}
FUNCTIONS = {
    "ReapDeferredSurfaces": "r200_texture.c",
    "ReapDeferredSegments": "r200_submit.c",
    "RecoverDevice": "r200_submit.c",
}


def function(text, name):
    definitions = list(re.finditer(
        rf"^(?:void|BOOL)\s+{name}\s*\([^;{{}}]*\)\s*\{{", text, re.M))
    if len(definitions) != 1:
        raise AssertionError(f"Expected exactly one definition of {name}")
    definition = definitions[0]
    depth = 0
    # Count braces without interpreting comments or quoted literals as code.
    tokens = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]'
    for token in re.finditer(tokens, text[definition.end() - 1:], re.S):
        depth += (token[0] == "{") - (token[0] == "}")
        if depth == 0:
            return text[definition.start():definition.end() - 1 + token.end()] + "\n"
    raise AssertionError(f"Unterminated function {name}")


STUBS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define SUBMIT_SLOT_COUNT 2
#define R200_DEFERRED_SEGMENT_MAX 4
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL: %s\n", #e); exit(1); } } while (0)
enum { EPOCH = 7, OLD = 6, F1 = 101, F2 = 102 };
struct BitMap { ULONG id; };
struct Radeon3DSurface { void *Handle; };
struct Radeon3DDevice { ULONG latest; BOOL open; };
struct SubmitSlot { ULONG fence, deviceEpoch; BOOL inFlight; };
struct DeferredSurface {
    struct DeferredSurface *next;
    struct Radeon3DSurface surface;
    struct BitMap *bitmap;
    ULONG fence, deviceEpoch;
};
struct DeferredSegment { ULONG segmentId, fence, deviceEpoch; };
struct MiniGLR200Context {
    struct Radeon3DDevice *device;
    ULONG deviceEpoch;
    struct SubmitSlot submitSlots[SUBMIT_SLOT_COUNT];
    struct DeferredSurface *deferredSurfaces;
    struct DeferredSegment deferredSegments[R200_DEFERRED_SEGMENT_MAX];
    ULONG deferredSegmentCount;
};
static struct MiniGLR200Context c;
static struct Radeon3DDevice gpu;
static struct Allocation {
    struct DeferredSurface *node;
    struct BitMap *bitmap;
    ULONG id, epoch;
    BOOL handleLive;
} allocations[8];
static BOOL segments[8];
static ULONG fencePolls;
static char events[128];

/* Exact order log: surface, bitmap, node, segment, device close. */
static void event(char kind, ULONG id) {
    size_t n = strlen(events);
    CHECK(n + 2 < sizeof(events) && id < 8);
    events[n] = kind; events[n + 1] = (char)('0' + id); events[n + 2] = 0;
}
static void expect(const char *wanted) { CHECK(strcmp(events, wanted) == 0); }
static void reset(void) {
    for (ULONG i = 0; i < 8; ++i)
        CHECK(!allocations[i].node && !allocations[i].bitmap && !allocations[i].handleLive);
    CHECK(!fencePolls);
    memset(&c, 0, sizeof(c)); memset(allocations, 0, sizeof(allocations));
    memset(segments, 0, sizeof(segments)); events[0] = 0;
    gpu = (struct Radeon3DDevice){F2, TRUE}; c.device = &gpu; c.deviceEpoch = EPOCH;
}
static void park(ULONG id, ULONG epoch, ULONG fence, BOOL handle, BOOL bitmap) {
    struct Allocation *a = &allocations[id];
    struct DeferredSurface **link = &c.deferredSurfaces;
    CHECK(id > 0 && id < 8 && !a->node);
    a->id = id; a->epoch = epoch; a->handleLive = handle && epoch == EPOCH;
    a->node = calloc(1, sizeof(*a->node)); CHECK(a->node);
    if (bitmap) {
        a->bitmap = malloc(sizeof(*a->bitmap)); CHECK(a->bitmap); a->bitmap->id = id;
    }
    a->node->bitmap = a->bitmap; a->node->surface.Handle = handle ? a : NULL;
    a->node->fence = fence; a->node->deviceEpoch = epoch;
    while (*link) link = &(*link)->next;
    *link = a->node;
}
BOOL Radeon3DTestFence(struct Radeon3DDevice *device, ULONG fence) {
    CHECK(device == &gpu && gpu.open);
    ++fencePolls;
    fprintf(stderr, "TestFence fence=%u latest=%u\n", fence, gpu.latest);
    /* Completed F1 becomes unqueryable after F2; zero is never a valid token. */
    return fence != 0 && fence == gpu.latest;
}
static void Radeon3DReleaseSurface(struct Radeon3DDevice *device, struct Radeon3DSurface *s) {
    struct Allocation *a = s->Handle;
    CHECK(device == &gpu && gpu.open && c.device == device);
    CHECK(a && a->node && a->handleLive && a->epoch == c.deviceEpoch);
    CHECK(s == &a->node->surface);
    /* Touch real bitmap storage: a bitmap-first release must fail ASan/order checks. */
    if (a->bitmap) CHECK(a->bitmap->id == a->id);
    event('S', a->id); a->handleLive = FALSE; s->Handle = NULL; gpu.latest = 0;
}
static void p96FreeBitMap(struct BitMap *bitmap) {
    struct Allocation *a = &allocations[bitmap->id];
    CHECK(a->node && a->bitmap == bitmap && !a->handleLive);
    event('B', a->id); a->bitmap = NULL; free(bitmap);
}
static void FreeMem(void *node, ULONG bytes) {
    CHECK(bytes == sizeof(struct DeferredSurface));
    for (ULONG i = 1; i < 8; ++i) {
        struct Allocation *a = &allocations[i];
        if (a->node != node) continue;
        CHECK(!a->bitmap && !a->handleLive);
        event('N', i); a->node = NULL; free(node); return;
    }
    CHECK(!"unknown or double-freed node");
}
BOOL Radeon3DFreeSegment(struct Radeon3DDevice *device, ULONG id) {
    CHECK(device == &gpu && gpu.open && c.device == device);
    CHECK(id > 0 && id < 8 && segments[id]);
    event('G', id); segments[id] = FALSE; return TRUE;
}
static void Radeon3DClose(struct Radeon3DDevice *device) {
    CHECK(device == &gpu && gpu.open);
    event('C', 0); gpu.open = FALSE; gpu.latest = 0;
    for (ULONG i = 0; i < 8; ++i) allocations[i].handleLive = FALSE;
    memset(segments, 0, sizeof(segments));
}
'''

CASES = r'''
int main(int argc, char **argv) {
    CHECK(argc == 2); reset();
    /* Empty lists must not contact the service, including with a busy slot. */
    c.submitSlots[1] = (struct SubmitSlot){F2, EPOCH, TRUE};
    ReapDeferredSurfaces(&c); ReapDeferredSegments(&c);
    expect(""); CHECK(gpu.latest == F2 && !fencePolls);
    c.submitSlots[1].inFlight = FALSE;
    if (!strcmp(argv[1], "obsolete") || !strcmp(argv[1], "shared")) {
        BOOL shared = !strcmp(argv[1], "shared");
        gpu.latest = shared ? F2 : F1;
        park(1, EPOCH, gpu.latest, TRUE, TRUE);
        gpu.latest = F2; /* F1 retired before the next submit published F2. */
        if (shared) { park(2, EPOCH, F2, TRUE, TRUE); park(3, EPOCH, F2, TRUE, TRUE); }
        ReapDeferredSurfaces(&c);
        CHECK(c.deferredSurfaces == NULL);
        expect(shared ? "S1B1N1S2B2N2S3B3N3" : "S1B1N1"); CHECK(gpu.latest == 0);
    } else if (!strcmp(argv[1], "zero")) {
        park(1, EPOCH, 0, TRUE, TRUE); park(2, EPOCH, F2, FALSE, TRUE);
        park(3, EPOCH, 0, TRUE, FALSE); park(4, EPOCH, 0, FALSE, FALSE);
        ReapDeferredSurfaces(&c); CHECK(c.deferredSurfaces == NULL);
        expect("S1B1N1B2N2S3N3N4");
    } else if (!strcmp(argv[1], "surface-slots")) {
        for (ULONG slot = 0; slot < SUBMIT_SLOT_COUNT; ++slot)
        for (ULONG old = 0; old < 2; ++old)
        for (ULONG busy = 0; busy < 2; ++busy) {
            struct DeferredSurface saved[3];
            reset(); park(1, EPOCH, F1, TRUE, TRUE); park(2, EPOCH, F2, TRUE, TRUE);
            park(3, EPOCH, 0, TRUE, TRUE);
            for (ULONG i = 0; i < 3; ++i) memcpy(&saved[i], allocations[i + 1].node, sizeof(saved[i]));
            c.submitSlots[slot] = (struct SubmitSlot){F2, old ? OLD : EPOCH, busy};
            ReapDeferredSurfaces(&c); ReapDeferredSurfaces(&c);
            if (busy && !old) {
                expect(""); CHECK(gpu.latest == F2);
                CHECK(c.deferredSurfaces == allocations[1].node);
                for (ULONG i = 0; i < 3; ++i) {
                    CHECK(allocations[i + 1].node);
                    CHECK(!memcmp(&saved[i], allocations[i + 1].node, sizeof(saved[i])));
                }
            } else {
                CHECK(c.deferredSurfaces == NULL);
                expect("S1B1N1S2B2N2S3B3N3");
            }
            c.submitSlots[slot].inFlight = FALSE;
            ReapDeferredSurfaces(&c); CHECK(c.deferredSurfaces == NULL);
            expect("S1B1N1S2B2N2S3B3N3");
        }
    } else if (!strcmp(argv[1], "surface-epochs")) {
        /* Remove old nodes at head, middle and tail without losing retained links. */
        for (ULONG id = 1; id <= 5; ++id) park(id, id % 2 ? OLD : EPOCH, F1, TRUE, TRUE);
        c.submitSlots[1] = (struct SubmitSlot){F2, EPOCH, TRUE};
        ReapDeferredSurfaces(&c); ReapDeferredSurfaces(&c); expect("B1N1B3N3B5N5");
        CHECK(c.deferredSurfaces && c.deferredSurfaces == allocations[2].node);
        CHECK(allocations[2].node->next == allocations[4].node && allocations[4].node);
        CHECK(!allocations[4].node->next && gpu.latest == F2);
        c.submitSlots[1].inFlight = FALSE;
        ReapDeferredSurfaces(&c); CHECK(c.deferredSurfaces == NULL);
        expect("B1N1B3N3B5N5S2B2N2S4B4N4");
    } else if (!strcmp(argv[1], "surface-closed")) {
        park(1, EPOCH, F1, TRUE, TRUE); park(2, OLD, F2, TRUE, TRUE);
        park(3, EPOCH, 0, TRUE, TRUE);
        c.submitSlots[0] = (struct SubmitSlot){F2, EPOCH, TRUE};
        Radeon3DClose(c.device); c.device = NULL; /* Epoch/slots not invalidated yet. */
        ReapDeferredSurfaces(&c); CHECK(c.deferredSurfaces == NULL);
        expect("C0B1N1B2N2B3N3");
    } else if (!strcmp(argv[1], "segment-obsolete")) {
        CHECK(R200_ASYNC_ROTATION);
        c.deferredSegments[0] = (struct DeferredSegment){1, F1, EPOCH};
        c.deferredSegmentCount = 1; segments[1] = TRUE;
        ReapDeferredSegments(&c); CHECK(c.deferredSegmentCount == 0); expect("G1");
    } else if (!strcmp(argv[1], "segment-slots")) {
        CHECK(R200_ASYNC_ROTATION);
        for (ULONG slot = 0; slot < SUBMIT_SLOT_COUNT; ++slot)
        for (ULONG old = 0; old < 2; ++old)
        for (ULONG busy = 0; busy < 2; ++busy)
        for (ULONG match = 0; match < 2; ++match) {
            struct DeferredSegment entry = {1, F1, EPOCH};
            reset(); c.deferredSegments[0] = entry; c.deferredSegmentCount = 1; segments[1] = TRUE;
            c.submitSlots[slot] = (struct SubmitSlot){match ? F1 : F2, old ? OLD : EPOCH, busy};
            ReapDeferredSegments(&c); ReapDeferredSegments(&c);
            if (match && busy && !old) {
                CHECK(c.deferredSegmentCount == 1 && segments[1]); expect("");
                CHECK(!memcmp(&entry, &c.deferredSegments[0], sizeof(entry)));
            } else { CHECK(c.deferredSegmentCount == 0 && !segments[1]); expect("G1"); }
            c.submitSlots[slot].inFlight = FALSE;
            ReapDeferredSegments(&c); CHECK(c.deferredSegmentCount == 0); expect("G1");
        }
    } else if (!strcmp(argv[1], "segment-mixed") || !strcmp(argv[1], "segment-closed") ||
               !strcmp(argv[1], "segments-disabled")) {
        struct MiniGLR200Context saved;
        BOOL disabled = !strcmp(argv[1], "segments-disabled");
        CHECK(disabled == !R200_ASYNC_ROTATION);
        c.deferredSegments[0] = (struct DeferredSegment){1, F2, OLD};
        c.deferredSegments[1] = (struct DeferredSegment){2, F2, EPOCH};
        c.deferredSegments[2] = (struct DeferredSegment){3, F1, EPOCH};
        c.deferredSegments[3] = (struct DeferredSegment){4, F2, EPOCH};
        c.deferredSegmentCount = 4;
        /* ID 1 was reused by the new device; the old-epoch entry must not free it. */
        for (ULONG id = 1; id <= 4; ++id) segments[id] = TRUE;
        c.submitSlots[0] = (struct SubmitSlot){F1, OLD, TRUE};
        c.submitSlots[1] = (struct SubmitSlot){F2, EPOCH, TRUE};
        if (disabled) {
            memcpy(&saved, &c, sizeof(c));
            ReapDeferredSegments(&c); ReapDeferredSegments(&c);
            CHECK(!memcmp(&saved, &c, sizeof(c))); expect(""); CHECK(gpu.latest == F2);
            for (ULONG id = 1; id <= 4; ++id) CHECK(segments[id]);
        }
        if (disabled || !strcmp(argv[1], "segment-closed")) {
            Radeon3DClose(c.device); c.device = NULL; memcpy(&saved, &c, sizeof(c));
            ReapDeferredSegments(&c); ReapDeferredSegments(&c); expect("C0");
            if (disabled) CHECK(!memcmp(&saved, &c, sizeof(c)));
            else CHECK(c.deferredSegmentCount == 0);
        } else {
            ReapDeferredSegments(&c); ReapDeferredSegments(&c);
            CHECK(c.deferredSegmentCount == 2); expect("G3");
            CHECK(c.deferredSegments[0].segmentId == 2 && c.deferredSegments[1].segmentId == 4);
            for (ULONG i = 0; i < 2; ++i)
                CHECK(c.deferredSegments[i].fence == F2 && c.deferredSegments[i].deviceEpoch == EPOCH);
            CHECK(segments[1] && segments[2] && !segments[3] && segments[4]);
            c.submitSlots[1].inFlight = FALSE;
            ReapDeferredSegments(&c); CHECK(c.deferredSegmentCount == 0); expect("G3G2G4");
            CHECK(segments[1] && !segments[2] && !segments[3] && !segments[4]);
        }
    } else { CHECK(!"unknown case"); }
    /* Reaping again must neither change the log/count nor touch any freed storage. */
    char savedEvents[sizeof(events)]; memcpy(savedEvents, events, sizeof(events));
    ULONG savedCount = c.deferredSegmentCount;
    ReapDeferredSurfaces(&c); ReapDeferredSegments(&c);
    CHECK(!memcmp(savedEvents, events, sizeof(events)) && c.deferredSegmentCount == savedCount);
    CHECK(c.deferredSurfaces == NULL && !fencePolls);
    reset(); /* Also checks that every heap allocation/handle was reclaimed. */
    printf("PASS %s\n", argv[1]); return 0;
}
'''


def build(directory, name, bodies, rotation):
    source, binary = Path(directory) / f"{name}.c", Path(directory) / name
    source.write_text(STUBS + bodies["ReapDeferredSurfaces"] + bodies["ReapDeferredSegments"] + CASES)
    result = subprocess.run([
        "cc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fno-pie", "-no-pie", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", f"-DR200_ASYNC_ROTATION={rotation}",
        str(source), "-o", str(binary),
    ], capture_output=True, text=True, timeout=60, cwd=directory)
    if result.returncode:
        raise AssertionError(f"cc failed for {name}:\n{result.stdout}{result.stderr}")
    return binary


class Retirement(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bodies = {tree: {name: function((path / file).read_text(), name)
                            for name, file in FUNCTIONS.items()} for tree, path in TREES.items()}
        temporary = tempfile.TemporaryDirectory(prefix="r200-retirement-", dir="/tmp/opencode")
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = temporary.name
        cls.binaries = {(tree, rotation): build(cls.directory, f"{tree}-{rotation}", bodies, rotation)
                        for tree, bodies in cls.bodies.items() for rotation in (0, 1)}

    def run_case(self, binary, case):
        return subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10,
                              cwd=self.directory, env={**os.environ,
                                  "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0",
                                  "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})

    def passes(self, binary, case):
        result = self.run_case(binary, case)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout, f"PASS {case}\n")

    def test_native_ppc_parity_and_no_polling(self):
        for name in FUNCTIONS:
            with self.subTest(function=name):
                self.assertEqual(self.bodies["native"][name], self.bodies["ppc"][name])
                if name != "RecoverDevice":
                    self.assertNotIn("Radeon3DTestFence", self.bodies["native"][name])

    def test_recovery_source_order(self):
        sequence = ("Radeon3DClose(context->device);context->device=NULL;"
                    "ReapDeferredSurfaces(context);InvalidateSubmissionState(context);")
        for tree, bodies in self.bodies.items():
            with self.subTest(tree=tree):
                code = re.sub(r"/\*.*?\*/|//[^\n]*|\s+", "", bodies["RecoverDevice"], flags=re.S)
                self.assertIn(sequence, code)
                self.assertEqual(code.count("ReapDeferredSurfaces(context);"), 1)
                self.assertLess(code.index(sequence) + len(sequence), code.index("Radeon3DOpen("))

    def test_surfaces(self):
        for (tree, rotation), binary in self.binaries.items():
            for case in ("obsolete", "shared", "zero", "surface-slots", "surface-epochs", "surface-closed"):
                with self.subTest(tree=tree, rotation=rotation, case=case):
                    self.passes(binary, case)

    def test_segments(self):
        for tree in TREES:
            for case in ("segment-obsolete", "segment-slots", "segment-mixed", "segment-closed"):
                with self.subTest(tree=tree, case=case):
                    self.passes(self.binaries[tree, 1], case)
            with self.subTest(tree=tree, rotation=0):
                self.passes(self.binaries[tree, 0], "segments-disabled")

    def test_historical_polling_mutation(self):
        mutations = {
            "ReapDeferredSurfaces": ("if (sameDevice && pending)",
                "if (sameDevice && (pending || !node->fence || "
                "!Radeon3DTestFence(context->device, node->fence)))"),
            "ReapDeferredSegments": ("if (pending)",
                "if (pending || !Radeon3DTestFence(context->device, entry->fence))"),
        }
        for tree, bodies in self.bodies.items():
            mutated = dict(bodies)
            for name, (before, after) in mutations.items():
                self.assertEqual(mutated[name].count(before), 1, "Mutation anchor changed")
                mutated[name] = mutated[name].replace(before, after)
            binary = build(self.directory, f"{tree}-historical", mutated, 1)
            for case, polls, failure in (
                ("obsolete", "TestFence fence=101 latest=102\n", "c.deferredSurfaces == NULL"),
                ("shared", "TestFence fence=102 latest=102\n" + "TestFence fence=102 latest=0\n" * 2,
                 "c.deferredSurfaces == NULL"),
                ("segment-obsolete", "TestFence fence=101 latest=102\n", "c.deferredSegmentCount == 0"),
            ):
                with self.subTest(tree=tree, case=case):
                    self.passes(self.binaries[tree, 1], case)
                    result = self.run_case(binary, case)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, "")
                    # Reject the intended cleanup failure, not a compiler/sanitizer crash.
                    self.assertEqual(result.stderr, polls + f"FAIL: {failure}\n")


if __name__ == "__main__":
    regression = "--regress-fences" in sys.argv
    if regression:
        sys.argv.remove("--regress-fences")
    unittest.main(defaultTest="Retirement.test_historical_polling_mutation" if regression else None)
