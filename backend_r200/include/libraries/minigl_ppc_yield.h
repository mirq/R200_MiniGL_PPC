#ifndef LIBRARIES_MINIGL_PPC_YIELD_H
#define LIBRARIES_MINIGL_PPC_YIELD_H

#include <exec/types.h>

#define MGLPPC_RING_YIELD_US 100UL
/* A short requested timeout still deschedules for a full scheduler interval on
 * some WarpOS kernels. Allow the consumer a bounded polling budget first. */
#define MGLPPC_RING_YIELD_SPINS 131072UL
void MGLPPCYield(void);
void MGLPPCYieldStats(ULONG *calls, ULONG *ticks, ULONG *maximum);

#endif
