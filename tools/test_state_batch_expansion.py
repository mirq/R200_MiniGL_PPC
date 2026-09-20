#!/usr/bin/env python3
"""Invariant regression for the state-batch expansion helper (plan 2026-09-20
§8.3, W-22). The 2026-09-20 grey-screen incident came from lowering
R200_EXPAND_MISMATCH_MIN, so the expansion path needs a CPU gate before any
further hardware threshold experiment.

Extracts the real R200ExpandStateBatch() from r200_geometry.c and compiles it
against a minimal SubmitSlot whose records[] is the LAST field, so an
out-of-bounds expansion lands in the ASan global redzone. Checks the
accounting invariants across draw counts and header sizes that
CanExpandStateBatch can accept (draws * headerDwords <= MAX_RECORD_DWORDS):

  recordCount == draws * headerDwords
  records[i*H + k] == shared header, k != 10
  records[i*H + 10] == stateBatchDraws[i].VertexCount
  lastDrawOffset == (draws-1)*H, lastDrawHeaderDwords == H
  lastDrawValid TRUE, stateBatch FALSE, expanded TRUE
  generatedDwordCount == expandedGenerated

Usage: python3 tools/test_state_batch_expansion.py
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
GEOMETRY = ROOT / "backend_r200/library/r200_geometry.c"


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


def load():
    source = GEOMETRY.read_text()
    helper = function(source, "static void R200ExpandStateBatch(")
    # The helper must stay pure: no clock, no context, no OS calls.
    for banned in ("PerfClock", "context->", "AllocMem", "Radeon3D"):
        if banned in helper:
            raise AssertionError(f"expansion helper is no longer pure: {banned}")
    return helper


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MAX_RECORD_DWORDS 8192UL
#define MAX_SLOT_DRAWS 192UL

struct StateBatchDraw { ULONG OffsetBytes, VertexCount; };

/* records[] is deliberately last so an overrun hits the ASan global
 * redzone instead of a neighbouring field. */
struct SubmitSlot {
    ULONG drawCount, recordCount, generatedDwordCount;
    ULONG lastDrawOffset, lastDrawHeaderDwords;
    BOOL lastDrawValid, stateBatch, expanded;
    struct StateBatchDraw stateBatchDraws[MAX_SLOT_DRAWS];
    ULONG records[MAX_RECORD_DWORDS];
};

'''

CHECKS = r'''
static ULONG VertexCountFor(ULONG index) { return 3UL * (index % 5UL) + 3UL; }

static void RunOne(ULONG draws, ULONG headerDwords, ULONG *checks) {
    static struct SubmitSlot slot;
    ULONG i, k;

    assert(draws >= 1UL && draws <= MAX_SLOT_DRAWS);
    assert(headerDwords >= 11UL);
    assert(draws * headerDwords <= MAX_RECORD_DWORDS);

    memset(&slot, 0, sizeof(slot));
    slot.drawCount = draws;
    slot.recordCount = headerDwords;
    slot.stateBatch = TRUE;
    slot.expanded = FALSE;
    for (k = 0; k < headerDwords; ++k)
        slot.records[k] = 0x10000000UL + k;
    for (i = 0; i < draws; ++i) {
        slot.stateBatchDraws[i].OffsetBytes = i * 512UL;
        slot.stateBatchDraws[i].VertexCount = VertexCountFor(i);
    }

    R200ExpandStateBatch(&slot, 0x12345678UL);

    assert(slot.recordCount == draws * headerDwords);
    assert(slot.generatedDwordCount == 0x12345678UL);
    assert(slot.lastDrawOffset == (draws - 1UL) * headerDwords);
    assert(slot.lastDrawHeaderDwords == headerDwords);
    assert(slot.lastDrawValid == TRUE);
    assert(slot.stateBatch == FALSE);
    assert(slot.expanded == TRUE);
    assert(slot.drawCount == draws);

    for (i = 0; i < draws; ++i) {
        const ULONG *record = slot.records + i * headerDwords;
        assert(record[10] == VertexCountFor(i));
        for (k = 0; k < headerDwords; ++k) {
            if (k == 10UL)
                continue;
            assert(record[k] == 0x10000000UL + k);
        }
    }
    ++*checks;
}

int main(void) {
    static const ULONG headers[] = {11, 12, 16, 43, 44, 75, 76, 100, 127, 256, 384};
    static const ULONG draws[] = {1, 2, 3, 4, 7, 8, 16, 63, 64, 100, 191, 192};
    ULONG checks = 0;
    unsigned h, d;

    for (h = 0; h < sizeof(headers) / sizeof(headers[0]); ++h)
        for (d = 0; d < sizeof(draws) / sizeof(draws[0]); ++d)
            if (draws[d] * headers[h] <= MAX_RECORD_DWORDS)
                RunOne(draws[d], headers[h], &checks);

    /* Exact capacity boundary: the largest accepted expansion fills the
     * record buffer to its last dword without overrunning it. */
    RunOne(1, MAX_RECORD_DWORDS, &checks);
    RunOne(MAX_SLOT_DRAWS, 11, &checks);

    printf("STATE_EXPANSION checks=%lu failures=0\n", (unsigned long)checks);
    return 0;
}
'''


def main():
    helper = load()
    with tempfile.TemporaryDirectory(prefix="r200-state-expand-", dir="/tmp/opencode") as directory:
        path = pathlib.Path(directory)
        source = path / "check.c"
        source.write_text(HARNESS + helper + CHECKS)
        binary = path / "check"
        flags = ["cc", "-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fno-pie", "-no-pie", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all"]
        build = subprocess.run(flags + [str(source), "-o", str(binary)],
                               capture_output=True, text=True, timeout=60)
        if build.returncode or build.stderr:
            raise SystemExit("compile failed:\n" + build.stdout + build.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True,
                                timeout=60,
                                env={"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                                     "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        if result.returncode or result.stderr:
            raise SystemExit("run failed:\n" + result.stdout + result.stderr)
        sys.stdout.write(result.stdout)


if __name__ == "__main__":
    main()
