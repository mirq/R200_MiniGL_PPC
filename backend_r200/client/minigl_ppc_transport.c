#include <libraries/minigl_ppc_transport.h>
#include <libraries/minigl_ppc_trace.h>
#include <proto/exec.h>
#include <string.h>

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
    if (transport->pendingMessage != NULL) {
        R200PPCTraceStr("tclose drain enter");
        (void)MGLPPCTransportWait(transport, transport->pendingMessage);
        R200PPCTraceStr("tclose drain exit");
    }
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
    ULONG version = transport->protocolVersion
        ? transport->protocolVersion : MGLPPC_PROTOCOL_VERSION;

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
    message->protocolVersion = version;
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
    ULONG version;
    static ULONG twWaits;
    BOOL traced;

    if (transport->replyPort == NULL || transport->pendingMessage != message)
        return FALSE;

    sequence = message->sequence;
    version = transport->protocolVersion
        ? transport->protocolVersion : MGLPPC_PROTOCOL_VERSION;

    /* WaitPort() below is unbounded: a lost host reply blocks this task
     * forever at zero CPU. Trace the first waits so a wedged process's
     * trace file ends on a "twait enter" line with no matching exit. */
    traced = ++twWaits <= 8UL;
    if (traced)
        R200PPCTraceUL("twait enter seq", sequence);
    WaitPort(transport->replyPort);
    reply = GetMsg(transport->replyPort);
    transport->pendingMessage = NULL;
    if (traced)
        R200PPCTraceUL("twait exit seq", sequence);
    if (reply != &message->execMessage) {
        if (traced)
            R200PPCTraceStr("twait foreign reply");
        return FALSE;
    }

    return message->magic == MGLPPC_PROTOCOL_MAGIC &&
           message->protocolVersion == version &&
           message->sequence == sequence;
}
