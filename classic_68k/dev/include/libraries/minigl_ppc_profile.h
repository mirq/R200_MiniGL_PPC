#ifndef LIBRARIES_MINIGL_PPC_PROFILE_H
#define LIBRARIES_MINIGL_PPC_PROFILE_H

#include <exec/types.h>

typedef struct MGLPPCClientProfile {
    ULONG vertexCalls;
    ULONG vertexTicks;
    ULONG sphereTicks;
    ULONG transformTicks;
    ULONG endCalls;
    ULONG endTicks;
    ULONG submitCalls;
    ULONG submitTicks;
} MGLPPCClientProfile;

void MGLPPCProfileReset(void);
void MGLPPCProfileRead(MGLPPCClientProfile *profile);
BOOL MGLPPCSubmitRawPayload(const ULONG *payload, ULONG payloadWords);

#endif
