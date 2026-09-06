#include <exec/types.h>
#include <libraries/minigl_ppc_dispatch.h>
#include <stdio.h>

int main(void)
{
    static const ULONG words[] = {
        0x01234567UL,
        0x89abcdefUL,
        0x13579bdfUL,
        0x2468ace0UL
    };
    const MGLPPCDispatchTable *dispatch;
    ULONG integerResult;
    ULONG wordResult;
    float floatResult;

    printf("MiniGLPPC static dispatch smoke\n");

    dispatch = MiniGLPPCGetStaticDispatchTable();
    if (dispatch == NULL) {
        printf("FAIL: dispatch table is NULL\n");
        return 20;
    }
    if (dispatch->abiVersion != MINIGLPPC_DISPATCH_ABI_VERSION) {
        printf("FAIL: ABI version %lu (expected %lu)\n",
               (unsigned long)dispatch->abiVersion,
               (unsigned long)MINIGLPPC_DISPATCH_ABI_VERSION);
        return 20;
    }
    if (dispatch->structSize < sizeof(MGLPPCDispatchTable)) {
        printf("FAIL: dispatch table is too small (%lu)\n",
               (unsigned long)dispatch->structSize);
        return 20;
    }
    if ((dispatch->backendFlags & MINIGLPPC_BACKEND_FLAG_STATIC_PROOF) == 0) {
        printf("FAIL: static proof backend flag is absent\n");
        return 20;
    }

    integerResult = dispatch->AddU32(0x12345678UL, 0x11111111UL);
    if (integerResult != 0x23456789UL) {
        printf("FAIL: integer dispatch returned %08lx\n",
               (unsigned long)integerResult);
        return 20;
    }

    floatResult = dispatch->MulAddF32(1.5f, 2.0f, 0.25f);
    if (floatResult < 3.249f || floatResult > 3.251f) {
        printf("FAIL: float dispatch returned %f\n", (double)floatResult);
        return 20;
    }

    wordResult = dispatch->XorWords(words, sizeof(words) / sizeof(words[0]));
    if (wordResult != 0xbfb7bfb7UL) {
        printf("FAIL: pointer dispatch returned %08lx\n",
               (unsigned long)wordResult);
        return 20;
    }

    printf("OK: PPC dispatch ABI %lu, size %lu\n",
           (unsigned long)dispatch->abiVersion,
           (unsigned long)dispatch->structSize);
    printf("OK: integer, float and pointer calls passed\n");
    return 0;
}
