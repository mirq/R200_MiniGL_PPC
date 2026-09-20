/* Local diagnostic storage only. NOT a shared-memory/wire ABI. */
#ifndef MINIGL_SUBIMAGE_TIMING_H
#define MINIGL_SUBIMAGE_TIMING_H

#include <exec/types.h>

#define MGL_LM_CAPACITY 128UL
#define MGL_LM_PHASES 16UL

/* Each phase is entered at most once per event. A seen bit distinguishes an
 * unvisited phase from a legitimate zero timestamp/duration. All arithmetic
 * is modulo 2^32; individual calls must be shorter than one clock rollover. */
typedef struct MGLLmTiming {
    ULONG start, end, phase, seen;
    ULONG begin[MGL_LM_PHASES], finish[MGL_LM_PHASES];
} MGLLmTiming;

static void MGLLmNext(MGLLmTiming *t, ULONG phase, ULONG now)
{
    if (t->phase < MGL_LM_PHASES)
        t->finish[t->phase] = now;
    t->phase = phase;
    if (phase < MGL_LM_PHASES) {
        t->seen |= 1UL << phase;
        t->begin[phase] = now;
    }
    t->end = now;
}

/* Caller zeroes its record before beginning; no allocation or I/O here. */
static void MGLLmBegin(MGLLmTiming *t, ULONG now)
{
    t->start = now;
    t->phase = MGL_LM_PHASES;
    MGLLmNext(t, 0, now);
}

#endif
