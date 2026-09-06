#ifndef LIBRARIES_MINIGL_PPC_EVENTS_H
#define LIBRARIES_MINIGL_PPC_EVENTS_H

#include <exec/types.h>

typedef struct MGLPPCEvent {
    ULONG eventClass;
    UWORD code;
} MGLPPCEvent;

BOOL MGLPPCUsesHostEvents(void);
BOOL MGLPPCPollEvent(MGLPPCEvent *event);

#endif
