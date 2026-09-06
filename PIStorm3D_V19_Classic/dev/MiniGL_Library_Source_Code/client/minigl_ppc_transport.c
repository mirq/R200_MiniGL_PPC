#include <exec/types.h>
#include <exec/ports.h>
#include <string.h>

#include <libraries/minigl_ppc_transport.h>

/* WarpOS amiga.lib supplies PPC-to-68k bridges for these Exec calls. */
extern struct MsgPort *CreateMsgPort(void);
extern void DeleteMsgPort(struct MsgPort *port);
extern void Forbid(void);
extern void Permit(void);
extern struct MsgPort *FindPort(const char *name);
extern void PutMsg(struct MsgPort *port, struct Message *message);
extern struct Message *WaitPort(struct MsgPort *port);
extern struct Message *GetMsg(struct MsgPort *port);

BOOL MGLPPCTransportOpen(MGLPPCTransport *transport)
{
    memset(transport, 0, sizeof(*transport));
    transport->replyPort = CreateMsgPort();
    transport->nextSequence = 1;
    return transport->replyPort != NULL;
}

void MGLPPCTransportClose(MGLPPCTransport *transport)
{
    /* A queued Exec message owns its reply port until the 68k host replies.
     * There is no safe local cancellation; host loss requires the same cold
     * recovery as any other interrupted hardware command. */
    if (transport->pendingMessage != NULL)
        (void)MGLPPCTransportWait(transport, transport->pendingMessage);
    if (transport->replyPort != NULL)
        DeleteMsgPort(transport->replyPort);
    memset(transport, 0, sizeof(*transport));
}

BOOL MGLPPCTransportCall(MGLPPCTransport *transport, MGLPPCMessage *message)
{
    return MGLPPCTransportSend(transport, message) &&
           MGLPPCTransportWait(transport, message);
}

BOOL MGLPPCTransportSend(MGLPPCTransport *transport, MGLPPCMessage *message)
{
    struct MsgPort *hostPort;
    ULONG messageSize;

    if (transport->replyPort == NULL || transport->pendingMessage != NULL)
        return FALSE;

    if (message->payloadWords > MGLPPC_MAX_BATCH_WORDS)
        return FALSE;
    messageSize = MGLPPC_MESSAGE_SIZE(message->payloadWords);
    if (messageSize > 65535UL)
        return FALSE;

    message->execMessage.mn_ReplyPort = transport->replyPort;
    message->execMessage.mn_Length = (UWORD)messageSize;
    message->magic = MGLPPC_PROTOCOL_MAGIC;
    message->protocolVersion = MGLPPC_PROTOCOL_VERSION;
    message->sequence = transport->nextSequence++;
    message->status = MGLPPC_STATUS_OK;

    Forbid();
    hostPort = transport->hostPort;
    if (hostPort == NULL) {
        hostPort = FindPort(MGLPPC_HOST_PORT_NAME);
        transport->hostPort = hostPort;
    }
    if (hostPort != NULL)
        PutMsg(hostPort, &message->execMessage);
    Permit();

    if (hostPort == NULL)
        return FALSE;

    transport->pendingMessage = message;
    return TRUE;
}

BOOL MGLPPCTransportWait(MGLPPCTransport *transport, MGLPPCMessage *message)
{
    struct Message *reply;
    ULONG sequence;

    if (transport->replyPort == NULL || transport->pendingMessage != message)
        return FALSE;

    sequence = message->sequence;

    WaitPort(transport->replyPort);
    reply = GetMsg(transport->replyPort);
    transport->pendingMessage = NULL;
    if (reply != &message->execMessage)
        return FALSE;

    return message->magic == MGLPPC_PROTOCOL_MAGIC &&
           message->protocolVersion == MGLPPC_PROTOCOL_VERSION &&
           message->sequence == sequence;
}
