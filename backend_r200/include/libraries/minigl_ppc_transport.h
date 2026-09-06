#ifndef LIBRARIES_MINIGL_PPC_TRANSPORT_H
#define LIBRARIES_MINIGL_PPC_TRANSPORT_H

#include <libraries/minigl_ppc_protocol.h>

typedef struct MGLPPCTransport {
    struct MsgPort *replyPort;
    struct MsgPort *hostPort;
    ULONG nextSequence;
    /* 0 = MGLPPC_PROTOCOL_VERSION; set to 14 to talk to an old host. */
    ULONG protocolVersion;
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
