/* Pure dword accounting; caller supplies the platform's ULONG/BOOL types.
 * No structure layout changes, allocation, or byte-order-dependent access. */
#ifndef R200_BATCH_BUDGET_H
#define R200_BATCH_BUDGET_H

static __inline__ BOOL R200SlotPayloadFits(ULONG limit, ULONG records,
                                         ULONG draws, BOOL stateBatch,
                                         BOOL ring)
{
    ULONG remaining;
    if (records > limit)
        return FALSE;
    remaining = limit - records;
    if (draws && ring) {
        if (remaining < 2UL)
            return FALSE;
        remaining -= 2UL; /* ring commit prefix: draw count, segment index */
    }
    if (stateBatch)
        return draws <= remaining / 2UL;
    return !ring || draws <= remaining;
}

static __inline__ BOOL R200ExpandedBatchCost(ULONG compact, ULONG draws,
                                           ULONG fetch, ULONG limit,
                                           BOOL lighting,
                                           ULONG *expanded)
{
    ULONG each;
    if (!draws || (fetch && draws - 1UL > compact / fetch))
        return FALSE;
    /* Expanded copies of an unlit shared header differ only in vertex
     * count. The emitter caches their state and matrices, so expanding the
     * record representation does not multiply GPU state-command cost.
     * Lit records do re-emit lighting payloads on the cached path: keep the
     * conservative full-state bound for those until separately optimized. */
    if (!lighting) {
        if (compact > limit)
            return FALSE;
        *expanded = compact;
        return TRUE;
    }
    /* Compact batch: one full state+fetch, then fetch-only descriptors. */
    each = compact - (draws - 1UL) * fetch;
    if (each > limit / draws)
        return FALSE;
    *expanded = each * draws;
    return TRUE;
}
#endif
