#include <exec/types.h>
#include <stdio.h>
#include <string.h>

#include <libraries/minigl_ppc_transport.h>

int main(void)
{
    MGLPPCTransport transport;
    MGLPPCMessage message;

    printf("MiniGLPPC transport smoke\n");

    if (!MGLPPCTransportOpen(&transport)) {
        printf("FAIL: cannot create PPC reply port\n");
        return 20;
    }

    memset(&message, 0, sizeof(message));
    message.command = MGLPPC_CMD_HELLO;
    if (!MGLPPCTransportCall(&transport, &message)) {
        printf("FAIL: host not found or HELLO transport failed\n");
        MGLPPCTransportClose(&transport);
        return 20;
    }
    if (message.status != MGLPPC_STATUS_OK) {
        printf("FAIL: HELLO status %ld\n", (long)message.status);
        MGLPPCTransportClose(&transport);
        return 20;
    }

    printf("OK: 68k host replied, MiniGL %lu.%lu, open count %lu\n",
           (unsigned long)message.value[0],
           (unsigned long)message.value[1],
           (unsigned long)message.value[2]);

    memset(&message, 0, sizeof(message));
    message.command = MGLPPC_CMD_SHUTDOWN;
    if (!MGLPPCTransportCall(&transport, &message) ||
        message.status != MGLPPC_STATUS_OK) {
        printf("FAIL: SHUTDOWN transport failed\n");
        MGLPPCTransportClose(&transport);
        return 20;
    }

    MGLPPCTransportClose(&transport);
    printf("OK: cross-CPU transport and shutdown passed\n");
    return 0;
}
