# R200 Control Mailbox V1

This change targets the measured Exec-transition bottleneck, not aperture
bandwidth: equal geometry/p96 mappings measured about 80 MB/s, while 32
glTexImage calls took 5.9 seconds and the host spent only 272 ms handling
137 commands. No new hardware measurements or deployment accompany this change.

The active host is `backend_r200/minigl_ppc_host.c`. Its native library/import
comes from `classic_68k/dev/MiniGL_Library_Source_Code`,
including that tree's `library/` implementation, NOT its stale host source.
The native implementation is unchanged.

## Negotiation and Layout

Public dispatch ABI remains 3, message protocol remains 15 (including the
existing protocol-14 downgrade), and MGLPPCRingControlV1 remains version 5 with
no size or field changes. HELLO capability bit 8 advertises MAILBOX_V1 only
when ring support is compiled in and MGLPPC_NOMAILBOX is unset/empty. The
environment is read at HELLO; change it before starting the next session for
A/B testing. It does not revoke an already registered mailbox.

Ring registration requests flag bit 1. The host echoes it only after mailbox
initialization and cache clean, and returns the segment extent in value[4].
An older host ignores the flag; an older client ignores the new capability
and response fields. A failed first registration leaves no mailbox pointer;
a rejected duplicate registration preserves the existing live session.
If frontend ring validation fails after host acceptance, unregister must
complete before frontend slot buffers can be freed.

The mailbox begins at byte 4096 of the existing 65536-byte ring segment.
Both peers check 32-byte base alignment, a segment extent of at least 65536,
and non-wrapping address arithmetic. The frontend also verifies capability,
echoed flag, protocol, usable clock rate, magic, version, ring/header epoch
agreement, and initially zero request/ack serials before saving the pointer.

All fields are ULONG (4 bytes). Offsets below are relative to the mailbox:

| Offset | Size | Contents | Writer After Registration |
| --- | --- | --- | --- |
| 0 | 32 | magic, version, epoch, pad[5] | Immutable host header |
| 32 | 32 | reqSerial, command, contextId, pad[5] | PPC |
| 64 | 32 | reqValue[8] | PPC |
| 96 | 32 | ackSerial, status, pad[6] | Host |
| 128 | 32 | ackValue[8] | Host |

Total: 160 bytes, ending at segment byte 4256 (exclusive). Compile-time
assertions check ULONG size, total size, field/line offsets, non-overlap with
ring control, aligned offset, and fit in the segment. Neither CPU dirties the
other CPU's lines after registration. Each writer cleans values first, cleans
metadata/status with the old serial, then publishes and cleans the new serial
last. The host invalidates request lines using CacheClearE; the PPC invalidates
ack lines using the existing cache helper. Header invalidation on the PPC
never writes it. The existing helpers include the required cache ordering.

## Execution and Failure

Exactly one request may be outstanding. Serial numbers start at 1, advance
modulo 32 bits and skip zero. The host checks the expected next serial,
magic/version/epoch, allowed command, and current context before calling the
same HandleMessage used by Exec. Invalid requests receive an error ack without
invoking a semantic handler. Duplicate observed serials are not re-executed.

Only zero-payload BIND_TEXTURE, GET_INTEGER, TEXTURE_DIRECT_BEGIN,
TEXTURE_DIRECT_COMMIT, TEXTURE_DIRECT_ABORT, TEXTURE_SUBIMAGE_SHARED,
READ_PIXELS_SHARED, CP_SURFACE and TEXTURE_UPLOAD_SHARED use the mailbox.
The last one is gated on the HELLO capability bit
`MGLPPC_CAP_MAILBOX_UPLOAD_SHARED` (bit 13): a host without it rejects the
command with UNKNOWN_COMMAND, so the client keeps it on the Exec transport
against older hosts. PlatformCall copies successful transport replies,
including semantic errors, back into its existing control message. Other
commands retain Exec. Semantic errors do not
trigger an Exec retry; a conclusive ordinary direct-upload failure can still
use the existing explicit ABORT and shared-upload fallback.

The wait deadline is five seconds of calibrated PPC timebase ticks, not a
loop count. The existing PerfClockRate calibration is performed before
registration, outside the hot path. Negotiation requires a rate from 10000
through floor(0x7fffffff/5) Hz, so the deadline is representable within half
the low timebase's wrap period. Unsigned elapsed subtraction handles a wrap
during the wait. Ack reads are throttled to roughly one per 100 microseconds;
the intervening loop reads only the on-chip timebase, with no OS crosscalls.
Five seconds exceeds the native one-second waits; it is not a cancellation
guarantee. Scheduling delays can extend wall time, and a suspended client is
not guaranteed to observe a deadline across multiple timebase wraps.

A published timeout returns BUSY/failure, retains the serial and mailbox,
and poisons the session. It NEVER retries the request through Exec, posts
another request, or automatically ABORTs/falls back to shared upload. A
subsequent call makes a nonblocking acknowledgement check. Only the matching
serial in the original valid epoch conclusively releases the outstanding
reference; an unrelated ack or changed header cannot recover it.

A late result is discarded, not reported as success to the original caller.
Even after it arrives, the abandoned session permits only barrier and
lifecycle cleanup, not new mailbox operations, texture uploads or geometry
publication. This deliberately requires context deletion/recreation: a late
BIND or COMMIT may have changed the host while the frontend texture mirror
still describes the pre-call state. A late successful BEGIN's unused lease is
left for normal native context destruction, never implicitly filled or retried.

DeleteContext waits up to another five seconds for reconciliation before any
reference release. If unresolved, it returns retaining context, ring, slots,
staging buffers and transport; Close also returns without freeing them. A
later cleanup attempt may try again. After reconciliation, abandoned queued
frontend work is not submitted; already-published ring work is drained using
the existing barrier. Unregister and destroy must succeed before freeing
frontend slot references. The host polls one mailbox request before its Exec
loop and again before each incoming Exec message, so pending mailbox handlers
finish before lifecycle destruction/unregister. DiscardSemantic clears the
host mailbox pointer before freeing the segment. The frontend clears its
pointer after successful unregister and never reads the freed mailbox.
If destroy then fails, the retained context has streaming disabled and permits
only cleanup retries; it cannot reuse the segments already freed by unregister.

Permanent host failure or header corruption intentionally leaks/pins the
session rather than risking use-after-free. Public void teardown APIs cannot
return a cleanup error. Forced process termination/DLL unload is not converted
into cancellation by this protocol; callers must not assume a timed-out close
released all resources. Existing single-client, single-caller serialization
is required; this mailbox is not a multi-threaded queue.

## Validation and Remaining Risks

Build locally, without conversion/deployment:

```
make -f Makefile.dll.r200.gcc bin/minigl_ppc_host
make -f Makefile.dll.r200.gcc warpelf
```

The host target explicitly compiles this workspace's host against the native
import archive. The WarpELF target validates the converter relocation contract
and regenerated ABI-3 table. Existing Makefile dependencies cover the protocol
and frontend source; no dependency changes are needed.

Host mailbox command count and handler time are reported separately as
`mailbox` and `mailbox_us`; Exec control counts exclude mailbox operations.
Mailbox handler time is included in session busy time. Upload traces and the
periodic heartbeat are off unless MGLPPC_UPLOAD_TRACE is set; mailbox polling
adds no per-request printing. Use trace-off runs for comparisons.

CPU-only tests can check routing, publication order, allowed-command rejection,
serial wrap, low-timebase wrap, timeout/no-retry, late-ack reconciliation and
teardown reference retention. They cannot prove PCI visibility or native GPU
completion. Hardware fault-injection and old/new-peer A/B tests remain required
before claiming a speedup or release readiness. No global barrier was removed;
the existing ordering hazard between pending frontend geometry and control
handlers has not been changed or solved by this transport optimization.

## Pre-Hardware Review Follow-Up

These findings concern code that predated the mailbox change:

- Display cleanup: the actual native `library/r200_surface.c:1167..1172`
  implements LockDisplay as a context-existence check and UnlockDisplay as
  a no-op. Native `library/minigl_r200.c` DestroyContext already flushes,
  waits and releases resources; it has no display lock to release. Thus a
  native lock leak is a false positive for this backend, but the host's
  `displayLocked` flag really could survive a non-PRESENT teardown.
  DiscardSemantic now calls unlock only when that flag is set, then clears
  it. Repeated cleanup does not double-unlock. Native code is unchanged.
- Texture equality shortcut: removed the entire frontend source-shadow cache,
  including its structure, lookup/comparison/store helpers, allocation/copy
  and cleanup paths. Its key did not establish complete texture-state
  equivalence and the cache survived context deletion, so even identical
  bytes could incorrectly suppress a required upload. Every eligible call
  now follows the existing upload path. Conversion, staging, barriers,
  commit, reply mirroring and timeout/fallback handling are unchanged.
- Failed ring registration: bridge/info and ring-allocation validation failures
  after driver acquisition now unwind through DiscardSemantic. A returned
  segment lease is recorded before validating its CPU view and can be freed
  by ID without dereferencing a rejected pointer. The driver base/open flag
  and partial registration state are cleared, allowing inline fallback.
  Validation and live-session rejection still precede acquisition and never
  invoke this cleanup on an existing registration.

Both explicit host and WarpELF builds passed after these fixes. Scratch tests
in `/tmp/opencode/test_r200_registration.py` rerun the original mailbox tests
and exercise failed open/bridge/info/generation/allocation/reply validation,
inline fallback, partial-lease release, live-registration preservation, and
conditional/idempotent unlock for ring and inline cleanup. They also assert
that no shadow-cache identifiers remain. These are CPU-only stubbed-service
tests, not native/GPU or PCI validation. No hardware was accessed.
