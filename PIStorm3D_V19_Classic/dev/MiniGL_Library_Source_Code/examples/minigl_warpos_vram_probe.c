#include <exec/memory.h>
#include <exec/types.h>
#include <stdio.h>
#include <string.h>

#include <libraries/minigl_ppc_transport.h>

struct timeval {
    ULONG tv_secs;
    ULONG tv_micro;
};

struct PPCBase;
extern struct PPCBase *PowerPCBase;
extern APTR AllocVec(ULONG byteSize, ULONG requirements);
extern void FreeVec(APTR memoryBlock);

VOID __GetSysTimePPC(void *, struct timeval *) =
    "\tlwz\tr0,-682(r3)\n\tmtlr\tr0\n\tblrl";
#define GetSysTimePPC(value) __GetSysTimePPC(PowerPCBase, (value))

extern void MGLPPCPciWriteBarrier(void);

static ULONG ElapsedMicros(const struct timeval *start,
                           const struct timeval *end)
{
    ULONG seconds = end->tv_secs - start->tv_secs;
    LONG micros = (LONG)end->tv_micro - (LONG)start->tv_micro;

    if (micros < 0) {
        --seconds;
        micros += 1000000L;
    }
    return seconds * 1000000UL + (ULONG)micros;
}

static ULONG ThroughputKbps(ULONG bytes, ULONG passes, ULONG micros)
{
    unsigned long long total = (unsigned long long)bytes * passes;

    if (!micros)
        return 0;
    return (ULONG)(total * 1000000ULL / ((unsigned long long)micros * 1024ULL));
}

static BOOL Call(MGLPPCTransport *transport, MGLPPCMessage *message,
                 ULONG command)
{
    message->command = command;
    if (!MGLPPCTransportCall(transport, message)) {
        printf("VRAM_PROBE status=transport_failed command=%lu\n",
               (unsigned long)command);
        return FALSE;
    }
    if (message->status != MGLPPC_STATUS_OK) {
        printf("VRAM_PROBE status=host_error command=%lu code=%ld detail=%lu\n",
               (unsigned long)command, (long)message->status,
               (unsigned long)message->value[4]);
        return FALSE;
    }
    return TRUE;
}

static void StoreSequential(volatile ULONG *destination, ULONG salt)
{
    ULONG index;

    for (index = 0; index < MGLPPC_VRAM_PROBE_WORDS; ++index)
        destination[index] = index ^ salt;
}

int main(int argc, char **argv)
{
    MGLPPCTransport transport;
    MGLPPCMessage *message = NULL;
    volatile ULONG *memory;
    struct timeval start;
    struct timeval end;
    ULONG address;
    ULONG cookie;
    ULONG pass;
    ULONG ppcMicros;
    ULONG selfRead;
    ULONG hostRead;
    BOOL leaseActive = FALSE;
    int result = 20;

    if (!MGLPPCTransportOpen(&transport)) {
        printf("VRAM_PROBE status=transport_open_failed\n");
        return 20;
    }
    message = AllocVec(MGLPPC_MESSAGE_SIZE(0), MEMF_PUBLIC | MEMF_CLEAR);
    if (!message) {
        printf("VRAM_PROBE status=alloc_failed\n");
        goto done;
    }
    if (argc == 2 && !strcmp(argv[1], "--shutdown-host")) {
        if (Call(&transport, message, MGLPPC_CMD_SHUTDOWN)) {
            printf("VRAM_PROBE status=host_shutdown\n");
            result = 0;
        }
        goto done;
    }
    message->value[0] = MGLPPC_VRAM_PROBE_BYTES;
    message->value[1] = MGLPPC_VRAM_PROBE_PASSES;
    if (!Call(&transport, message, MGLPPC_CMD_VRAM_PROBE_BEGIN))
        goto done;
    leaseActive = TRUE;
    if (argc == 2 && !strcmp(argv[1], "--abort-after-begin")) {
        printf("VRAM_PROBE status=abort_after_begin\n");
        goto done;
    }

    address = message->value[0];
    cookie = message->value[7];
    if (!address || (address & 3UL) || !cookie ||
        message->value[1] != MGLPPC_VRAM_PROBE_BYTES ||
        message->value[2] != MGLPPC_VRAM_PROBE_PASSES) {
        printf("VRAM_PROBE status=bad_begin_reply\n");
        goto done;
    }
    memory = (volatile ULONG *)address;
    memory[message->value[5]] = message->value[6];
    MGLPPCPciWriteBarrier();
    memset(message->value, 0, sizeof(message->value));
    message->value[0] = cookie;
    if (!Call(&transport, message, MGLPPC_CMD_VRAM_PROBE_CONFIRM))
        goto done;
    StoreSequential(memory, MGLPPC_VRAM_PROBE_PPC_SEED - 1UL);
    MGLPPCPciWriteBarrier();
    GetSysTimePPC(&start);
    for (pass = 0; pass < MGLPPC_VRAM_PROBE_PASSES; ++pass)
        StoreSequential(memory, MGLPPC_VRAM_PROBE_PPC_SEED + pass);
    MGLPPCPciWriteBarrier();
    GetSysTimePPC(&end);
    ppcMicros = ElapsedMicros(&start, &end);

    /* Read-back validation: the original probe never exercised PPC reads
     * of the aperture, which the ring control block depends on. Flush
     * stdout before each read so a fault is attributed to the exact read. */
    printf("VRAM_PROBE ppc_readback=begin\n");
    fflush(stdout);
    selfRead = memory[MGLPPC_VRAM_PROBE_WORDS - 1UL];
    printf("VRAM_PROBE ppc_selfread=%08lx\n", (unsigned long)selfRead);
    fflush(stdout);
    hostRead = memory[MGLPPC_VRAM_PROBE_HOST_INDEX];
    printf("VRAM_PROBE ppc_hostread=%08lx\n", (unsigned long)hostRead);
    fflush(stdout);

    memset(message->value, 0, sizeof(message->value));
    message->value[0] = cookie;
    message->value[1] = ppcMicros;
    message->value[2] = selfRead;
    message->value[3] = hostRead;
    if (!Call(&transport, message, MGLPPC_CMD_VRAM_PROBE_END))
        goto done;
    leaseActive = FALSE;
    printf("VRAM_PROBE status=ok bytes=%lu passes=%lu 68k_us=%lu "
           "ppc_us=%lu 68k_kbps=%lu ppc_kbps=%lu\n",
           (unsigned long)message->value[2],
           (unsigned long)message->value[3],
           (unsigned long)message->value[0],
           (unsigned long)message->value[1],
           (unsigned long)ThroughputKbps(message->value[2],
                                         message->value[3],
                                         message->value[0]),
           (unsigned long)ThroughputKbps(message->value[2],
                                         message->value[3],
                                         message->value[1]));
    result = 0;

done:
    /* END also discards an unconfirmed lease on the host. Always send it
     * after a successful BEGIN so malformed replies and confirm failures do
     * not leave subsequent probe attempts stuck at BUSY. */
    if (leaseActive && message) {
        memset(message->value, 0, sizeof(message->value));
        message->command = MGLPPC_CMD_VRAM_PROBE_END;
        message->value[0] = cookie;
        message->value[1] = 1;
        (void)MGLPPCTransportCall(&transport, message);
    }
    if (message) FreeVec(message);
    MGLPPCTransportClose(&transport);
    return result;
}
