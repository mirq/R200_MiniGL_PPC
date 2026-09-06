#include <exec/types.h>
#include <stdio.h>
#include <string.h>

#include <libraries/minigl_ppc_transport.h>

static ULONG FloatToBits(float value)
{
    union {
        float value;
        ULONG bits;
    } conversion;

    conversion.value = value;
    return conversion.bits;
}

static BOOL Call(MGLPPCTransport *transport, MGLPPCMessage *message,
                 ULONG command)
{
    message->command = command;
    if (!MGLPPCTransportCall(transport, message)) {
        printf("FAIL: command %lu transport failed\n", (unsigned long)command);
        return FALSE;
    }
    if (message->status != MGLPPC_STATUS_OK) {
        printf("FAIL: command %lu status %ld\n",
               (unsigned long)command, (long)message->status);
        return FALSE;
    }
    return TRUE;
}

int main(void)
{
    MGLPPCTransport transport;
    MGLPPCMessage message;
    ULONG contextId = 0;
    int result = 20;

    printf("MiniGLPPC R200 clear/present smoke\n");
    if (!MGLPPCTransportOpen(&transport)) {
        printf("FAIL: cannot create PPC reply port\n");
        return 20;
    }

    memset(&message, 0, sizeof(message));
    if (!Call(&transport, &message, MGLPPC_CMD_HELLO))
        goto shutdown;
    printf("OK: host MiniGL %lu.%lu\n",
           (unsigned long)message.value[0],
           (unsigned long)message.value[1]);

    memset(&message, 0, sizeof(message));
    message.value[0] = 320;
    message.value[1] = 240;
    message.value[2] = 32;
    message.value[3] = 1;
    if (!Call(&transport, &message, MGLPPC_CMD_CREATE_CONTEXT))
        goto shutdown;
    contextId = message.contextId;
    printf("OK: context %lu created\n", (unsigned long)contextId);

    memset(&message, 0, sizeof(message));
    message.contextId = contextId;
    message.value[0] = FloatToBits(0.05f);
    message.value[1] = FloatToBits(0.18f);
    message.value[2] = FloatToBits(0.42f);
    message.value[3] = FloatToBits(1.0f);
    message.value[4] = 150;
    if (!Call(&transport, &message, MGLPPC_CMD_CLEAR_PRESENT))
        goto destroy;
    printf("OK: blue R200 frame presented\n");
    result = 0;

destroy:
    if (contextId != 0) {
        memset(&message, 0, sizeof(message));
        message.contextId = contextId;
        if (!Call(&transport, &message, MGLPPC_CMD_DESTROY_CONTEXT))
            result = 20;
        else
            printf("OK: context destroyed\n");
    }

shutdown:
    memset(&message, 0, sizeof(message));
    if (!Call(&transport, &message, MGLPPC_CMD_SHUTDOWN))
        result = 20;
    MGLPPCTransportClose(&transport);
    if (result == 0)
        printf("OK: PPC-initiated R200 clear/present passed\n");
    return result;
}
