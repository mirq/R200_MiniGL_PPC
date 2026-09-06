#ifndef LIBRARIES_MINIGL_PPC_TRANSPORT_H
#define LIBRARIES_MINIGL_PPC_TRANSPORT_H

#include <libraries/minigl_ppc_protocol.h>

typedef struct MGLPPCTransport {
    struct MsgPort *replyPort;
    struct MsgPort *hostPort;
    ULONG nextSequence;
    MGLPPCMessage *pendingMessage;
} MGLPPCTransport;

BOOL MGLPPCTransportOpen(MGLPPCTransport *transport);
void MGLPPCTransportClose(MGLPPCTransport *transport);
BOOL MGLPPCTransportSend(MGLPPCTransport *transport,
                         MGLPPCMessage *message);
BOOL MGLPPCTransportWait(MGLPPCTransport *transport,
                         MGLPPCMessage *message);
BOOL MGLPPCTransportCall(MGLPPCTransport *transport,
                          MGLPPCMessage *message);

#endif
