/* Long back-pressure waits eventually offer the scheduler a turn. A short
 * request can still sleep for a full quantum; callers must spend their bounded
 * polling budget first. The runtime opened PowerPCBase before DLL entry. */
#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <exec/types.h>
#include <exec/libraries.h>
#ifdef WARPUP
#pragma pack(pop)
#endif
#include <powerpc/powerpc_protos.h>
#include <libraries/minigl_ppc_yield.h>

#ifndef MGLPPC_PROFILE
#define MGLPPC_PROFILE 0
#endif
#if MGLPPC_PROFILE
static ULONG YieldCalls, YieldTicks, YieldMaximum;
static ULONG YieldClock(void)
{
    ULONG value;
    __asm__ volatile ("mftb %0" : "=r" (value));
    return value;
}
#endif

void MGLPPCYield(void)
{
#if MGLPPC_PROFILE
    ULONG start = YieldClock(), elapsed;
#endif
    (void)WaitTime(0UL, MGLPPC_RING_YIELD_US);
#if MGLPPC_PROFILE
    elapsed = YieldClock() - start;
    ++YieldCalls;
    YieldTicks += elapsed;
    if (elapsed > YieldMaximum) YieldMaximum = elapsed;
#endif
}

void MGLPPCYieldStats(ULONG *calls, ULONG *ticks, ULONG *maximum)
{
#if MGLPPC_PROFILE
    *calls = YieldCalls;
    *ticks = YieldTicks;
    *maximum = YieldMaximum;
#else
    *calls = *ticks = *maximum = 0;
#endif
}
