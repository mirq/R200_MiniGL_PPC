#ifndef LIBRARIES_MINIGL_PPC_YIELD_H
#define LIBRARIES_MINIGL_PPC_YIELD_H

#include <exec/types.h>

#define MGLPPC_RING_YIELD_US 100UL
/* A short requested timeout still deschedules for a full scheduler interval on
 * some WarpOS kernels. Allow the consumer a bounded polling budget first. */
#define MGLPPC_RING_YIELD_SPINS 131072UL
/* MGLPPCYield parks only once a wait (yield calls less than GAP apart) has
 * lasted SPIN ticks of the time base (~20 ms at 25 MHz). */
#define MGLPPC_RING_YIELD_SPIN_TICKS 500000UL
#define MGLPPC_RING_YIELD_GAP_TICKS 250000UL
void MGLPPCYield(void);
void MGLPPCWatchSet(ULONG site, ULONG value);
extern volatile ULONG MGLPPCWatch[8];
void MGLPPCYieldStats(ULONG *calls, ULONG *ticks, ULONG *maximum);

#endif
