#ifndef LIBRARIES_MINIGL_PPC_SEMANTIC_PROFILE_H
#define LIBRARIES_MINIGL_PPC_SEMANTIC_PROFILE_H

#include <exec/types.h>

/* WarpOS PPC semantic frontend transport attribution. All tick counters use
 * the on-chip timebase (mftb) so sampling adds no cross-CPU calls to the
 * measured path; clockRate converts ticks to seconds. */
typedef struct MGLPPCSemanticPlatformProfile {
    ULONG publishCount;
    ULONG publishSendTicks;
    ULONG publishCrcTicks;
    ULONG publishFlushTicks;
    ULONG replyWaitCount;
    ULONG replyWaitTicks;
    ULONG replyWaitMaxTicks;
    ULONG barrierCount;
    ULONG barrierTicks;
    ULONG controlCount;
    ULONG controlTicks;
    ULONG chunkCount;
    ULONG chunkTicks;
    ULONG sessionTicks;
    ULONG clockRate;
    /* ringStallTicks is the sum of the two counters below; it predates
     * them and is kept so older log parsers still work. The split matters
     * because publish stall is charged to the caller's bucket (usually
     * serialization) while barrier stall is charged to switch/display. */
    ULONG ringStallTicks;
    ULONG publishStallTicks;
    ULONG barrierStallTicks;
    ULONG drainTicks;
    ULONG textureCalls;
    ULONG textureSuccesses;
    ULONG textureBytes;
    ULONG textureTicks;
    ULONG textureDirectCalls;
    ULONG textureDirectTicks;
    ULONG textureSharedCalls;
    ULONG textureChunkedCalls;
    ULONG textureBarrierCalls;
    ULONG textureBarrierTicks;
    /* Ring back-pressure split. Each wait loop reports how often it was
     * entered with no space (enters), how often it got space back from a
     * cache refresh alone (noSleep), and how many scheduling yields it
     * cost (sleeps). Current DLL builds request short PPC-native waits and
     * report yield_us; historical builds used DOS tick sleeps. These counts
     * do not measure actual sleep duration or exclusively GPU latency. */
    ULONG publishStallEnters;
    ULONG publishStallNoSleep;
    ULONG publishStallSleeps;
    ULONG barrierStallEnters;
    ULONG barrierStallNoSleep;
    ULONG barrierStallSleeps;
    ULONG drainStallEnters;
    ULONG drainStallNoSleep;
    ULONG drainStallSleeps;
    /* Segment rotation waits on GPU retirement, not on ring space, and had
     * no timer at all before; rotateStallTicks is not part of
     * ringStallTicks and must not be added to it. */
    ULONG rotateStallEnters;
    ULONG rotateStallNoSleep;
    ULONG rotateStallSleeps;
    ULONG rotateStallTicks;
} MGLPPCSemanticPlatformProfile;

const MGLPPCSemanticPlatformProfile *R200PPCPlatformProfile(void);

/* Running total of ring back-pressure, readable from the serializer so a
 * flush can report how much of its own interval was spent waiting for the
 * host rather than packing vertices. */
ULONG R200PPCPlatformStallTicks(void);

/* Frame timeline (D0): mftb-stamped event ring, dumped after the run and
 * correlated against EClock sync pairs taken by the client. */
#define MGLPPC_TIMELINE_ENTRIES 2048UL

#define MGLPPC_TL_SYNC 1UL
#define MGLPPC_TL_PUBLISH_ENTER 2UL
#define MGLPPC_TL_PUBLISH_EXIT 3UL
#define MGLPPC_TL_BARRIER_ENTER 4UL
#define MGLPPC_TL_BARRIER_EXIT 5UL
#define MGLPPC_TL_DRAIN_ENTER 6UL
#define MGLPPC_TL_DRAIN_EXIT 7UL
#define MGLPPC_TL_STALL_ENTER 8UL
#define MGLPPC_TL_STALL_EXIT 9UL
#define MGLPPC_TL_FRAME 10UL
#define MGLPPC_TL_SCENE 11UL
#define MGLPPC_TL_CLEAR 12UL
#define MGLPPC_TL_SWITCH 13UL

typedef struct MGLPPCTimelineEntry {
    ULONG tsc;
    ULONG ev;
    ULONG a;
    ULONG b;
} MGLPPCTimelineEntry;

void R200PPCTimelineMark(ULONG ev, ULONG a, ULONG b);
void R200PPCTimelineDump(void);

#endif
