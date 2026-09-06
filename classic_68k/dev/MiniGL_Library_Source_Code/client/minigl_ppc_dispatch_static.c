#include <libraries/minigl_ppc_dispatch.h>

static ULONG AddU32(ULONG left, ULONG right)
{
    return left + right;
}

static float MulAddF32(float value, float scale, float bias)
{
    return value * scale + bias;
}

static ULONG XorWords(const ULONG *words, ULONG count)
{
    ULONG result = 0;
    ULONG index;

    for (index = 0; index < count; ++index)
        result ^= words[index];

    return result;
}

static const MGLPPCDispatchTable Dispatch = {
    MINIGLPPC_DISPATCH_ABI_VERSION,
    sizeof(MGLPPCDispatchTable),
    MINIGLPPC_BACKEND_FLAG_STATIC_PROOF,
    0,
    AddU32,
    MulAddF32,
    XorWords
};

const MGLPPCDispatchTable *MiniGLPPCGetStaticDispatchTable(void)
{
    return &Dispatch;
}
