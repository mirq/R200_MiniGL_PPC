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
#endif
static ULONG YieldBurstStart, YieldLast;

static ULONG YieldClock(void)
{
    ULONG value;
    __asm__ volatile ("mftb %0" : "=r" (value));
    return value;
}

/* The requested 100 us sleep parks for a whole scheduler quantum on this
 * kernel (~15 ms measured; 150 parks cost 2.2 s of a 19.8 s Quake demo3 at
 * 800x600), while a GPU batch the caller waits on retires in well under
 * that. Calls arriving back to back belong to one wait: keep spinning until
 * that wait has lasted MGLPPC_RING_YIELD_SPIN_TICKS, then park as before.
 * The time base is bus/4 (25-35 MHz on the reference machines), so the
 * tick constants are conservative wall-time bounds. */
void MGLPPCYield(void)
{
    ULONG now = YieldClock();
#if MGLPPC_PROFILE
    ULONG elapsed;
#endif

    if (now - YieldLast > MGLPPC_RING_YIELD_GAP_TICKS)
        YieldBurstStart = now;
    YieldLast = now;
    if (now - YieldBurstStart < MGLPPC_RING_YIELD_SPIN_TICKS)
        return;
    (void)WaitTime(0UL, MGLPPC_RING_YIELD_US);
    YieldLast = YieldClock();
#if MGLPPC_PROFILE
    elapsed = YieldLast - now;
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
