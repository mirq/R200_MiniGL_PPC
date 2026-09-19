/* Pure accounting tests for the actual frontend/native budget helpers. */
#include <stdint.h>
#include <stdio.h>
typedef uint32_t ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#include "r200_batch_budget.h"

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    printf("BUDGET_FAIL line=%d\n", __LINE__); } } while (0)

int main(void)
{
    ULONG cost, records, draws;
    int ring, shared;
    CHECK(!R200SlotPayloadFits(8192, 8184, 186, FALSE, TRUE));
    CHECK(!R200SlotPayloadFits(8192, 8052, 183, FALSE, TRUE));
    CHECK(R200SlotPayloadFits(8192, 7998, 192, FALSE, TRUE));
    CHECK(!R200SlotPayloadFits(8192, 7999, 192, FALSE, TRUE));
    CHECK(R200SlotPayloadFits(8192, 7808, 192, TRUE, FALSE));
    CHECK(!R200SlotPayloadFits(8192, 7808, 192, TRUE, TRUE));
    CHECK(R200SlotPayloadFits(8192, 44, 192, TRUE, TRUE));
    CHECK(R200SlotPayloadFits(8192, 8192, 0, FALSE, TRUE));
    CHECK(R200SlotPayloadFits(8192, 8192, 186, FALSE, FALSE));
    CHECK(!R200SlotPayloadFits(8192, UINT32_MAX, 1, FALSE, TRUE));
    CHECK(!R200SlotPayloadFits(8192, 44, UINT32_MAX, TRUE, TRUE));
    CHECK(R200ExpandedBatchCost(100 + 64*13, 64, 13, 8192, TRUE, &cost));
    CHECK(cost == 64*113);
    CHECK(!R200ExpandedBatchCost(100 + 100*13, 100, 13, 8192, TRUE, &cost));
    CHECK(R200ExpandedBatchCost(100 + 64*13, 64, 13, 1000, FALSE, &cost));
    CHECK(cost == 932);
    CHECK(R200ExpandedBatchCost(100 + 100*13, 100, 13, 8192, FALSE, &cost));
    CHECK(cost == 1400);
    CHECK(!R200ExpandedBatchCost(932, 64, 13, 931, FALSE, &cost));
    CHECK(!R200ExpandedBatchCost(1, 0, 13, 8192, FALSE, &cost));
    CHECK(!R200ExpandedBatchCost(1, 100, 13, 8192, FALSE, &cost));
    CHECK(!R200ExpandedBatchCost(UINT32_MAX, UINT32_MAX, 13, 8192, FALSE, &cost));
    for (ring=0; ring<=1; ++ring)
        for (shared=0; shared<=1; ++shared)
            for (draws=0; draws<=192; ++draws)
                for (records=0; records<=8250; records+=11) {
                    uint64_t total = (uint64_t)records +
                        (draws && ring ? 2 : 0) +
                        (uint64_t)draws * (shared ? 2 : ring ? 1 : 0);
                    CHECK(R200SlotPayloadFits(8192, records, draws, shared, ring)
                          == (total <= 8192));
                }
    printf("BATCH_BUDGET checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
