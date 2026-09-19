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
    printf("BATCH_EXPANSION checks=%u failures=%u\n", Checks, Failures);
    return Failures ? 1 : 0;
}
