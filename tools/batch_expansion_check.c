/* Compare real driver emission for shared-state and expanded records.
 * Reuse the CPU-only fixture; no service, MMIO or target execution. */
#define main TexMatrixFixtureMain
#include "tex_matrix_check.c"
#undef main
#include "r200_batch_budget.h"
#include "generated_cost.inc"

static ULONG BatchOffsets[192];
static ULONG SharedWords[RADEON3D_MAX_BATCH_DWORDS];

#define TEST_DEPTH 1UL
#define TEST_FOG 2UL
#define TEST_DUAL 4UL
#define TEST_COMPACT 8UL
#define TEST_SERIAL0 16UL
#define TEST_SERIAL1 32UL

static struct Radeon3DEmitSurface *ResolveDepth(void *user, ULONG handle,
                                               struct Radeon3DEmitSurface *surface)
{
    struct Radeon3DEmitSurface *result = Resolve(user, handle, surface);
    if (result && handle == 4UL) {
        result->Format = RADEON3D_FORMAT_R5G6B5PC;
        result->Pitch = 128;
    }
    return result;
}

static ULONG EmitCopies(BOOL expanded, ULONG draws, BOOL texgen,
                        BOOL normals, int lights, ULONG features, ULONG primitive)
{
    ULONG i;
    Setup(TRUE, texgen, normals, lights);
    Emitter.Resolve = ResolveDepth;
    if (features & TEST_DEPTH) {
        Record[3] = 4;
        Record[5] |= RADEON3D_DRAW_DEPTH_LESS | RADEON3D_DRAW_DEPTH_WRITE |
                     RADEON3D_DRAW_DEPTH_FUNC(RADEON3D_DEPTH_LEQUAL);
    }
    if (features & TEST_DUAL) {
        Record[15] = 3;
        Record[17] = Record[12];
        Record[19] |= RADEON3D_VERTEX_TEXTURE1;
    }
    if (features & TEST_FOG) {
        Record[19] |= RADEON3D_VERTEX_FOG;
        Record[20] = 0x00112233UL;
    }
    if (features & TEST_COMPACT)
        Record[5] |= RADEON3D_DRAW_COMPACT_VERTEX;
    if (features & TEST_SERIAL0)
        Record[13] = 0x0005001fUL;
    if (features & TEST_SERIAL1)
        Record[18] = 0xffff001fUL;
    Record[0] = primitive == R200_CP_VC_CNTL_PRIM_TYPE_QUADS
                  ? RADEON3D_EXEC_DRAW_QUADS
                  : primitive == R200_CP_VC_CNTL_PRIM_TYPE_TRI_FAN
                    ? RADEON3D_EXEC_DRAW_TRI_FAN : RADEON3D_EXEC_DRAW_TRIANGLES;
    Emitter.CommitSegmentBytes = 131072;
    Emitter.CommitVertexOffsets = BatchOffsets;
    for (i = 0; i < draws; ++i)
        BatchOffsets[i] = i * 512UL;
    for (i = 0; i < draws; ++i) {
        ULONG count = (primitive == R200_CP_VC_CNTL_PRIM_TYPE_QUADS ? 4UL : 3UL) *
                      (1UL + i % 3UL);
        if (!expanded && i) {
            CHECK(Radeon3DEmitVbufDraw(&Emitter, BatchOffsets[i], count,
                                      primitive));
        } else {
            Record[10] = count;
            CHECK(Radeon3DEmitDraw(&Emitter, Record, Record[1], primitive));
        }
    }
    return Emitter.Count;
}

static void CheckVariant(ULONG draws, BOOL texgen, BOOL normals, int lights,
                         ULONG features, ULONG primitive)
{
    ULONG shared = EmitCopies(FALSE, draws, texgen, normals, lights, features, primitive);
    ULONG expanded;
    ULONG compactBudget = GeneratedHardwareDrawDwords(
        0, (features & TEST_DEPTH) != 0, TRUE, texgen || (features & TEST_DUAL),
        (features & TEST_FOG) != 0, texgen, normals,
        lights < 0 ? 0UL : (ULONG)lights) +
        draws * 13UL;
    ULONG expandedBudget = 0;
    BOOL fits = R200ExpandedBatchCost(compactBudget, draws, 13UL, 8192UL,
                                      lights >= 0, &expandedBudget);
    ULONG repeatedLight = lights < 0 ? 0UL : 43UL + 39UL * (ULONG)lights;
    memcpy(SharedWords, Words, shared * sizeof(ULONG));
    expanded = EmitCopies(TRUE, draws, texgen, normals, lights, features, primitive);
    CHECK(expanded == shared + (draws - 1UL) * repeatedLight);
    if (lights < 0)
        CHECK(shared == expanded &&
              memcmp(SharedWords, Words, shared * sizeof(ULONG)) == 0);
    CHECK(fits);
    CHECK(expanded <= expandedBudget);
    printf("EXPANSION draws=%lu texgen=%d normals=%d lights=%d features=%lu prim=%lu "
           "shared=%lu expanded=%lu budget=%lu\n",
           (unsigned long)draws, texgen, normals, lights,
           (unsigned long)features, (unsigned long)primitive,
           (unsigned long)shared, (unsigned long)expanded,
           (unsigned long)expandedBudget);
}

static void CheckCopies(ULONG draws, BOOL texgen, BOOL normals, int lights)
{
    CheckVariant(draws, texgen, normals, lights, 0, R200_CP_VC_CNTL_PRIM_TYPE_TRI_LIST);
}

/* Mixed chain (plan 2026-09-20 §8.3 W-22): a compact state batch of
 * `sharedDraws` identical headers, then `differingDraws` headers that differ
 * from it and from each other, emitted through the real commit stream walk
 * (the frontend's commit path, Radeon3DEmitStream). Verifies the frontend's
 * generatedDwordCount accounting upper-bounds the actual output for the
 * sequence that the R200_EXPAND_MISMATCH_MIN threshold controls. */
#define MIXED_MAX_DRAWS 192UL
static ULONG MixedOffsets[MIXED_MAX_DRAWS];
static ULONG MixedChain[RADEON3D_MAX_BATCH_DWORDS];

static void CheckMixed(ULONG sharedDraws, ULONG differingDraws,
                       BOOL texgen, BOOL normals, int lights)
{
    ULONG header, i, total, actual, estimate, expanded = 0;
    ULONG g, compact, nextGenerated, limit, maxDiffering;

    if (!sharedDraws || sharedDraws + differingDraws > MIXED_MAX_DRAWS)
        return;
    header = Setup(TRUE, texgen, normals, lights);
    /* CanExpandStateBatch's first bound: compact records must fit. */
    if (header > 8192UL / sharedDraws)
        return;

    /* The frontend charges one full commit draw for the first draw of the
     * state batch, one fetch descriptor per extra shared draw, then the
     * R200ExpandedBatchCost estimate for the expansion plus one full draw
     * per differing record. If expansion does not fit, the frontend submits
     * instead and this mixed chain is not realizable - skip it. */
    g = GeneratedHardwareDrawDwords(0, FALSE, TRUE, FALSE, FALSE, texgen,
                                    normals,
                                    lights < 0 ? 0UL : (ULONG)lights);
    compact = g + sharedDraws * 13UL;
    nextGenerated = g + 13UL;
    limit = 8192UL - nextGenerated;
    if (!R200ExpandedBatchCost(compact, sharedDraws, 13UL, limit,
                               lights >= 0, &expanded))
        return;
    /* The frontend submits before an append that would exceed the budget, so
     * only the largest prefix of differing draws is realizable. */
    maxDiffering = (8192UL - expanded) / (g + 13UL);
    if (differingDraws > maxDiffering)
        differingDraws = maxDiffering;
    if (!differingDraws)
        return;

    Emitter.CommitVertexOffsets = MixedOffsets;
    Emitter.CommitSegmentBytes = (sharedDraws + differingDraws) * 512UL;
    for (i = 0; i < sharedDraws; ++i) {
        Record[10] = 3UL * (1UL + i % 3UL);
        memcpy(MixedChain + i * header, Record, header * sizeof(ULONG));
        MixedOffsets[i] = i * 512UL;
    }
    for (i = 0; i < differingDraws; ++i) {
        ULONG index = sharedDraws + i;

        Record[10] = 3UL * (1UL + (i + 1UL) % 3UL);
        /* Vary unit 0's handle for single-texture chains. Dual-texture chains
         * keep the validated unit0=2/unit1=3 pair (Setup) and differ only in
         * sampler state, matching the uniform cases CheckVariant covers. */
        Record[4] = texgen ? 2UL : ((i & 1UL) ? 3UL : 2UL);
        /* Valid differing sampler state: levels=6, minFilter=LINEAR
         * (0x0005001f). A mipmap filter with levels=1 is rejected by the
         * emitter's texture validation. */
        Record[13] = 0x0005001fUL;
        memcpy(MixedChain + index * header, Record, header * sizeof(ULONG));
        MixedOffsets[index] = index * 512UL;
    }
    total = (sharedDraws + differingDraws) * header;
    CHECK(total <= RADEON3D_MAX_BATCH_DWORDS);
    if (!Radeon3DEmitStream(&Emitter, MixedChain, total)) {
        printf("MIXED_FAIL shared=%lu differing=%lu texgen=%d normals=%d "
               "lights=%d total=%lu stage=%lu\n",
               (unsigned long)sharedDraws, (unsigned long)differingDraws,
               texgen, normals, lights, (unsigned long)total,
               (unsigned long)Emitter.FailStage);
        CHECK(0);
        return;
    }
    actual = Emitter.Count;
    CHECK(actual != 0);
    estimate = expanded + differingDraws * (g + 13UL);
    CHECK(estimate <= 8192UL);
    CHECK(actual <= estimate);
    printf("MIXED shared=%lu differing=%lu texgen=%d normals=%d lights=%d "
           "actual=%lu estimate=%lu\n",
           (unsigned long)sharedDraws, (unsigned long)differingDraws,
           texgen, normals, lights, (unsigned long)actual,
           (unsigned long)estimate);
}

int main(void)
{
    static const ULONG counts[] = {1, 2, 8, 64};
    unsigned i;
    Checks = Failures = 0;
    for (i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
        CheckCopies(counts[i], FALSE, FALSE, -1);
        CheckCopies(counts[i], FALSE, TRUE, -1);
        CheckCopies(counts[i], TRUE, FALSE, -1);
        CheckCopies(counts[i], TRUE, TRUE, -1);
        CheckVariant(counts[i], FALSE, FALSE, -1, TEST_DEPTH | TEST_COMPACT,
                     R200_CP_VC_CNTL_PRIM_TYPE_TRI_LIST);
        CheckVariant(counts[i], FALSE, FALSE, -1, TEST_DEPTH | TEST_DUAL | TEST_FOG,
                     R200_CP_VC_CNTL_PRIM_TYPE_TRI_LIST);
        CheckVariant(counts[i], FALSE, TRUE, -1, TEST_DEPTH | TEST_COMPACT,
                     R200_CP_VC_CNTL_PRIM_TYPE_QUADS);
        CheckVariant(counts[i], FALSE, FALSE, -1, TEST_DUAL,
                     R200_CP_VC_CNTL_PRIM_TYPE_TRI_FAN);
        CheckVariant(counts[i], FALSE, FALSE, -1,
                     TEST_DEPTH | TEST_COMPACT | TEST_SERIAL0,
                     R200_CP_VC_CNTL_PRIM_TYPE_TRI_LIST);
        CheckVariant(counts[i], TRUE, FALSE, -1, TEST_SERIAL1,
                     R200_CP_VC_CNTL_PRIM_TYPE_TRI_LIST);
        CheckVariant(counts[i], FALSE, FALSE, -1,
                     TEST_DEPTH | TEST_DUAL | TEST_FOG | TEST_SERIAL0 | TEST_SERIAL1,
                     R200_CP_VC_CNTL_PRIM_TYPE_TRI_LIST);
    }
    CheckVariant(180, FALSE, FALSE, -1, TEST_DEPTH | TEST_COMPACT,
                 R200_CP_VC_CNTL_PRIM_TYPE_TRI_LIST);
    CheckCopies(8, FALSE, TRUE, 0);
    CheckCopies(8, FALSE, TRUE, 1);
    CheckCopies(8, TRUE, TRUE, 8);
    {
        static const ULONG sharedCounts[] = {1, 2, 4, 8, 16, 32, 64};
        static const ULONG differingCounts[] = {1, 2, 4, 8, 16, 32, 64};
        unsigned s, d;
        for (s = 0; s < sizeof(sharedCounts) / sizeof(sharedCounts[0]); ++s)
            for (d = 0; d < sizeof(differingCounts) / sizeof(differingCounts[0]); ++d) {
                CheckMixed(sharedCounts[s], differingCounts[d], FALSE, FALSE, -1);
                CheckMixed(sharedCounts[s], differingCounts[d], FALSE, TRUE, 0);
                CheckMixed(sharedCounts[s], differingCounts[d], TRUE, TRUE, 1);
            }
    }
    printf("BATCH_EXPANSION checks=%u failures=%u\n", Checks, Failures);
    return Failures ? 1 : 0;
}
