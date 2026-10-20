#include <libraries/minigl_ppc_transport.h>
#include <libraries/minigl_ppc_yield.h>
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
#if MGLPPC_CLIENT_TRACE
        R200PPCTraceStr("tclose drain enter");
#endif
        (void)MGLPPCTransportWait(transport, transport->pendingMessage);
#if MGLPPC_CLIENT_TRACE
        R200PPCTraceStr("tclose drain exit");
#endif
    }
    if (transport->replyPort != NULL)
        DeleteMsgPort(transport->replyPort);
    memset(transport, 0, sizeof(*transport));
}

BOOL MGLPPCTransportCall(MGLPPCTransport *transport, MGLPPCMessage *message)
{
    BOOL result;

    MGLPPCWatch[4] = message->command;
    MGLPPCWatchSet(3UL, message->command);
    result = MGLPPCTransportSend(transport, message) &&
             MGLPPCTransportWait(transport, message);
    MGLPPCWatchSet(4UL, message->command);
    return result;
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
#if MGLPPC_CLIENT_TRACE
    static ULONG twWaits;
    BOOL traced;
#endif

    if (transport->replyPort == NULL || transport->pendingMessage != message)
        return FALSE;

    sequence = message->sequence;
    version = transport->protocolVersion
        ? transport->protocolVersion : MGLPPC_PROTOCOL_VERSION;

    /* WaitPort() below is unbounded: a lost host reply blocks this task
     * forever at zero CPU. Trace the first waits so a wedged process's
     * trace file ends on a "twait enter" line with no matching exit. */
#if MGLPPC_CLIENT_TRACE
    traced = ++twWaits <= 8UL;
    if (traced)
        R200PPCTraceUL("twait enter seq", sequence);
#endif
    WaitPort(transport->replyPort);
    reply = GetMsg(transport->replyPort);
    transport->pendingMessage = NULL;
#if MGLPPC_CLIENT_TRACE
    if (traced)
        R200PPCTraceUL("twait exit seq", sequence);
#endif
    if (reply != &message->execMessage) {
#if MGLPPC_CLIENT_TRACE
        if (traced)
            R200PPCTraceStr("twait foreign reply");
#endif
        return FALSE;
    }

    return message->magic == MGLPPC_PROTOCOL_MAGIC &&
           message->protocolVersion == version &&
           message->sequence == sequence;
}
