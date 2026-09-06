/*
 * Bounded mapping measurement gate, derived from minigl_warpos_vram_probe.c
 * in /home/mirek/minigl_ppc/examples. No driver calls or guessed addresses.
 * Run only against an idle, dedicated protocol-v15 R200 host.
 *
 * The old VRAM_PROBE_BEGIN leases a NEW standalone segment and refuses a
 * live context. It cannot measure the registered geometry pool. Instead we
 * register our own idle ring and use only its returned vertex descriptors;
 * we never publish a ring entry or touch its control block.
 *
 * TEXTURE_DIRECT_BEGIN selects storage automatically. Reply value[6] bit 0
 * identifies aux VRAM; zero identifies the legacy P96-imported path. There
 * is no bounded command to force both classes. This benchmark intentionally
 * reports an incomplete gate (exit 5) even after successful measurements.
 * A host extension must explicitly select P96/aux, forbid silent fallback,
 * and return class, CPU address, extent, pitch, format, epoch and lease ID.
 * It also needs a lease-bound verify/release command (including abort),
 * host cache invalidation and full-pattern verification before release.
 * No command IDs are reserved here and no host implementation is changed.
 * Proposed exact host command contract (NOT implemented wire commands):
 *   MAPPING_PROBE_BEGIN, contextId required:
 *     request value[] = {class, geometryIndex, bytes, width, height,
 *                        requestedFormat, 0, 0};
 *     reply value[] = {cookie, cpuAddress, bytes, pitch, grantedFormat,
 *                      epoch, grantedClass, cacheFlags}.
 *     Classes: 0=existing registered geometry, 1=P96 import, 2=aux texture.
 *     Bound bytes to 64 KiB, texture shape to 128x128 BGRA32. Reject class
 *     fallback, active GPU users and unsupported cache maintenance. Pin
 *     the lease until END; never return a device/MMIO token as an address.
 *   MAPPING_PROBE_VERIFY:
 *     request = {cookie, epoch, seed, passes, bytes, 0, 0, 0};
 *     reply = {cookie, epoch, wordsChecked, firstBadIndex, observed,
 *              expected, 0, 0}; firstBadIndex=~0UL on success.
 *     Validate the bounded pattern word[i] = i ^ (seed + passes - 1)
 *     through the host's supported cache/readback path, not guessed aliases.
 *   MAPPING_PROBE_END:
 *     request = {cookie, epoch, 0, 0, 0, 0, 0, 0}; release/abort even an
 *     unverified lease. Unpin geometry without freeing its registered pool;
 *     free only probe-owned textures. Advertise via a new HELLO capability.
 *
 * Each sample: 64 KiB, 64 passes, identical volatile ULONG stores, followed
 * by either sync or dcbf per 32-byte line + sync ON EVERY PASS. Store and
 * maintenance intervals are measured separately and summed. Warmup is not
 * timed. Results include timer instrumentation/preemption, not GPU work or
 * PCI completion latency. sync alone does not clean copyback storage.
 * The clean phase includes the cost of writing back dirty data, not just
 * instruction overhead. Texture leases are never committed or sampled.
 * MEMF_PUBLIC is a cached staging baseline, NOT a GPU texture/upload test.
 *
 * Full 64-bit timebase deltas are reported in hex, with a rate calibrated
 * against timer.device EClock outside the measurement loops. Rate 0 means
 * calibration unavailable: raw ticks only, never an assumed CPU clock.
 * Geometry/texture CPU readback is not attempted: the old standalone probe's
 * readback handshake does not authorize reads from these other mappings.
 */
#include <libraries/minigl_ppc_transport.h>
#pragma pack(push,2)
#include <devices/timer.h>
#pragma pack(pop)
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdio.h>
#include <string.h>

#define BYTES MGLPPC_VRAM_PROBE_BYTES
#define PASSES MGLPPC_VRAM_PROBE_PASSES
#define WORDS MGLPPC_VRAM_PROBE_WORDS
#define REPEATS 3UL
#define REPLY_WORDS (MGLPPC_VERTEX_SEGMENTS * 3UL)

struct Library *TimerBase;
static ULONG TimebaseHz;

static unsigned long long Ticks(void)
{
    ULONG hi, lo, again;
    __asm__ volatile("1: mftbu %0\n\tmftb %1\n\tmftbu %2\n\t"
                     "cmpw %0,%2\n\tbne 1b"
                     : "=&r"(hi), "=&r"(lo), "=&r"(again) : : "cc", "memory");
    return ((unsigned long long)hi << 32) | lo;
}

static void Calibrate(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *request;
    struct EClockVal start, end;
    unsigned long long begin, finish, ecTicks, tbTicks;
    ULONG rate, endRate;
    if (!port)
        return;
    request = (struct timerequest *)CreateIORequest(port, sizeof(*request));
    if (request) {
        if (!OpenDevice(TIMERNAME, UNIT_ECLOCK, (struct IORequest *)request, 0)) {
            TimerBase = (struct Library *)request->tr_node.io_Device;
            rate = ReadEClock(&start);
            begin = Ticks();
            Delay(5);
            finish = Ticks();
            endRate = ReadEClock(&end);
            ecTicks = (((unsigned long long)end.ev_hi << 32) | end.ev_lo) -
                      (((unsigned long long)start.ev_hi << 32) | start.ev_lo);
            tbTicks = finish - begin;
            /* Bound the calibration arithmetic; a long debugger pause
             * produces raw-only results rather than overflow or a guess. */
            if (rate && rate == endRate && ecTicks && ecTicks <= ~0UL &&
                tbTicks && tbTicks <= ~0UL) {
                unsigned long long hz = tbTicks * rate / ecTicks;
                if (hz && hz <= ~0UL)
                    TimebaseHz = (ULONG)hz;
            }
            printf("CALIB ec_hz=%lu ec_ticks=0x%08lx%08lx "
                   "tb_ticks=0x%08lx%08lx tb_hz=%lu\n", rate,
                   (ULONG)(ecTicks >> 32), (ULONG)ecTicks,
                   (ULONG)(tbTicks >> 32), (ULONG)tbTicks, TimebaseHz);
            CloseDevice((struct IORequest *)request);
            TimerBase = NULL;
        }
        DeleteIORequest((struct IORequest *)request);
    }
    DeleteMsgPort(port);
}

static void Maintain(volatile ULONG *memory, ULONG clean)
{
    ULONG offset;
    if (clean) {
        for (offset = 0; offset < BYTES; offset += 32UL) {
            const volatile UBYTE *line = (const volatile UBYTE *)memory + offset;
            __asm__ volatile("dcbf 0,%0" : : "r"(line) : "memory");
        }
    }
    __asm__ volatile("sync" : : : "memory");
}

static void Store(volatile ULONG *memory, ULONG salt)
{
    ULONG i;
    for (i = 0; i < WORDS; ++i)
        memory[i] = i ^ salt;
}

static BOOL Measure(const char *kind, ULONG id, ULONG address, ULONG extent)
{
    volatile ULONG *memory = (volatile ULONG *)address;
    ULONG repeat, mode, pass, i;
    /* Alignment ensures dcbf cannot touch a cache line outside the lease. */
    if (!address || (address & 31UL) || extent < BYTES ||
        address > ~0UL - BYTES) {
        printf("MAP status=bad_bounds kind=%s id=%lu addr=%08lx bytes=%lu\n",
               kind, id, address, extent);
        return FALSE;
    }
    printf("MAP kind=%s id=%lu addr=%08lx lease_bytes=%lu\n",
           kind, id, address, extent);
    fflush(stdout);
    for (repeat = 0; repeat < REPEATS; ++repeat) {
        for (i = 0; i < 2; ++i) {
            unsigned long long stores = 0, maintenance = 0;
            mode = i ^ (repeat & 1UL);
            Store(memory, MGLPPC_VRAM_PROBE_PPC_SEED - 1UL);
            Maintain(memory, 1);
            for (pass = 0; pass < PASSES; ++pass) {
                unsigned long long begin, middle, end;
                begin = Ticks();
                Store(memory, MGLPPC_VRAM_PROBE_PPC_SEED + pass);
                middle = Ticks();
                Maintain(memory, mode);
                end = Ticks();
                stores += middle - begin;
                maintenance += end - middle;
            }
            /* Clean even the sync-only variant before reusing/releasing. */
            Maintain(memory, 1);
            printf("BENCH kind=%s id=%lu rep=%lu mode=%s bytes=%lu "
                   "passes=%lu total_bytes=%lu tb_hz=%lu "
                   "store_ticks=0x%08lx%08lx maintenance_ticks=0x%08lx%08lx "
                   "total_ticks=0x%08lx%08lx verification=not_performed\n",
                   kind, id, repeat, mode ? "store_clean_sync" : "store_sync",
                   (ULONG)BYTES, (ULONG)PASSES, (ULONG)(BYTES * PASSES), TimebaseHz,
                   (ULONG)(stores >> 32), (ULONG)stores,
                   (ULONG)(maintenance >> 32), (ULONG)maintenance,
                   (ULONG)((stores + maintenance) >> 32),
                   (ULONG)(stores + maintenance));
            fflush(stdout);
        }
    }
    return TRUE;
}

static void Request(MGLPPCMessage *message, ULONG context, ULONG command)
{
    memset(message, 0, MGLPPC_MESSAGE_SIZE(REPLY_WORDS));
    message->contextId = context;
    message->command = command;
}

static BOOL Call(MGLPPCTransport *transport, MGLPPCMessage *message)
{
    ULONG command = message->command;
    if (!MGLPPCTransportCall(transport, message)) {
        printf("HOST command=%lu status=transport_failed\n", command);
        return FALSE;
    }
    if (message->status != MGLPPC_STATUS_OK) {
        printf("HOST command=%lu status=%ld\n", command, message->status);
        return FALSE;
    }
    return TRUE;
}

int main(void)
{
    MGLPPCTransport transport;
    MGLPPCMessage *message = NULL;
    APTR slots = NULL, staging = NULL;
    ULONG context = 0, texture = 0, caps, count, i;
    BOOL direct = FALSE;
    int result = 20;

    Calibrate();
    printf("MAPPING_GATE protocol=%lu ticks=timebase_not_cpu_cycles "
           "calibration=%s\n",
           (ULONG)MGLPPC_PROTOCOL_VERSION, TimebaseHz ? "eclock" : "unavailable");
    if (!MGLPPCTransportOpen(&transport))
        return 20;
    /* REGISTER request has 8 words but the host replies with up to 12. */
    message = AllocVec(MGLPPC_MESSAGE_SIZE(REPLY_WORDS), MEMF_PUBLIC | MEMF_CLEAR);
    staging = AllocVec(BYTES + 31UL, MEMF_PUBLIC);
    if (!message || !staging)
        goto done;
    if (!Measure("cached_ram_staging_not_gpu_texture", 0,
                 ((ULONG)staging + 31UL) & ~31UL, BYTES))
        goto done;

    Request(message, 0, MGLPPC_CMD_HELLO);
    if (!Call(&transport, message))
        goto done;
    caps = message->value[3];
    Request(message, 0, MGLPPC_CMD_CREATE_CONTEXT);
    message->value[0] = 320;
    message->value[1] = 240;
    message->value[2] = 32;
    message->value[3] = 1;
    if (!Call(&transport, message))
        goto done;
    context = message->contextId;
    if (!context)
        goto done;

    if (caps & MGLPPC_CAP_SEMANTIC_RING) {
        slots = AllocVec(MGLPPC_RING_SLOTS * MGLPPC_SEMANTIC_SLOT_DWORDS *
                         sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
        if (!slots)
            goto done;
        Request(message, context, MGLPPC_CMD_REGISTER_SEMANTIC_RING);
        message->value[0] = MGLPPC_RING_SLOTS;
        message->value[1] = MGLPPC_SEMANTIC_SLOT_DWORDS;
        message->payloadWords = MGLPPC_RING_SLOTS;
        for (i = 0; i < MGLPPC_RING_SLOTS; ++i)
            message->payload[i] = (ULONG)slots +
                i * MGLPPC_SEMANTIC_SLOT_DWORDS * sizeof(ULONG);
        if (!Call(&transport, message))
            goto done;
        count = message->value[2];
        if (count > MGLPPC_VERTEX_SEGMENTS || message->payloadWords != count * 3UL)
            goto done;
        for (i = 0; i < count; ++i) {
            if (!Measure("registered_geometry", message->payload[i * 3 + 2],
                         message->payload[i * 3], message->payload[i * 3 + 1]))
                goto done;
        }
        if (!count)
            printf("SKIP kind=registered_geometry reason=no_segments_granted\n");
    } else {
        printf("SKIP kind=registered_geometry reason=no_ring_capability\n");
    }

    if (caps & MGLPPC_CAP_TEXTURE_DIRECT_V1) {
        ULONG address, pitch, bytes, flags;
        Request(message, context, MGLPPC_CMD_GEN_TEXTURES);
        message->value[0] = 1;
        if (!Call(&transport, message) || !(texture = message->value[0]))
            goto done;
        Request(message, context, MGLPPC_CMD_TEXTURE_DIRECT_BEGIN);
        message->value[0] = texture;
        message->value[1] = 128;
        message->value[2] = 128;
        message->value[3] = MGLPPC_TEXDIRECT_FMT_BGRA32;
        /* Abort even if BEGIN reports an error after a partial allocation. */
        direct = TRUE;
        if (!Call(&transport, message))
            goto done;
        address = message->value[1];
        pitch = message->value[2];
        bytes = message->value[3];
        flags = message->value[6];
        printf("TEXTURE pitch=%lu bytes=%lu format=%lu epoch=%lu flags=%08lx\n",
               pitch, bytes, message->value[5], message->value[4], flags);
        /* Raw stores need no pixel format interpretation. Require exactly
         * packed 128x128x4 rows so the same 64 KiB workload covers all pixels. */
        if (pitch != 512UL || bytes < BYTES || !message->value[4] || (flags & ~1UL))
            goto done;
        if (!Measure(flags & 1UL ? "aux_gpu_texture" : "p96_imported_gpu_texture",
                     texture, address, bytes))
            goto done;
        printf("SKIP kind=%s reason=no_storage_class_selector\n",
               flags & 1UL ? "p96_imported_gpu_texture" : "aux_gpu_texture");
    } else {
        printf("SKIP kind=p96_and_aux_gpu_texture reason=no_direct_capability\n");
    }
    result = 5;

done:
    if (message && direct) {
        Request(message, context, MGLPPC_CMD_TEXTURE_DIRECT_ABORT);
        message->value[0] = texture;
        if (!Call(&transport, message))
            result = 20;
    }
    if (message && context) {
        /* Host discards ring state before deleting context-owned segments
         * and textures. Keep the shared slot allocation alive until reply. */
        Request(message, context, MGLPPC_CMD_DESTROY_CONTEXT);
        if (!Call(&transport, message)) {
            result = 20;
            /* A live host may still own these pointers. Do not free them. */
            slots = NULL;
        }
    }
    if (slots) FreeVec(slots);
    if (staging) FreeVec(staging);
    if (message) FreeVec(message);
    MGLPPCTransportClose(&transport);
    printf("MAPPING_GATE status=%s exit=%d\n",
           result == 5 ? "incomplete_missing_forced_texture_classes_and_host_verify"
                       : "failed", result);
    return result;
}
