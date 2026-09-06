#ifndef LIBRARIES_MINIGL_PPC_DISPATCH_H
#define LIBRARIES_MINIGL_PPC_DISPATCH_H

#include <exec/types.h>

#define MINIGLPPC_DISPATCH_ABI_VERSION 1UL
#define MINIGLPPC_BACKEND_FLAG_STATIC_PROOF (1UL << 0)

typedef struct MGLPPCDispatchTable {
    ULONG abiVersion;
    ULONG structSize;
    ULONG backendFlags;
    ULONG reserved;
    ULONG (*AddU32)(ULONG left, ULONG right);
    float (*MulAddF32)(float value, float scale, float bias);
    ULONG (*XorWords)(const ULONG *words, ULONG count);
} MGLPPCDispatchTable;

/* Phase 0 entry point. The resident library will expose the same table ABI. */
const MGLPPCDispatchTable *MiniGLPPCGetStaticDispatchTable(void);

#endif
