/* Explicit orderly shutdown for an orphaned R200 host. Do not use while a
 * GL client is live: it invalidates that client's context and shared leases.
 * The message and reply port stay alive until the host acknowledges cleanup. */
#include <libraries/minigl_ppc_transport.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    MGLPPCTransport transport;
    MGLPPCMessage message;
    BOOL ok;

    if (argc != 2 || strcmp(argv[1], "--orphan") != 0) {
        puts("usage: hoststop --orphan (only after the GL client has exited)");
        return 10;
    }
    if (!MGLPPCTransportOpen(&transport))
        return 20;
    memset(&message, 0, sizeof(message));
    message.command = MGLPPC_CMD_SHUTDOWN;
    ok = MGLPPCTransportCall(&transport, &message);
    MGLPPCTransportClose(&transport);
    if (!ok || message.status != MGLPPC_STATUS_OK) {
        puts("hoststop: shutdown not acknowledged");
        return 20;
    }
    puts("hoststop: shutdown acknowledged");
    return 0;
}
