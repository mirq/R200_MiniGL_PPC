# Helper Target Snapshot Cache

2026-09-08. Host-only change on the existing dirty `abac846` tree. No PPC DLL,
native backend, driver, protocol, fence, slot-release or cache-maintenance
changes were included in this comparison. The helper cache, hardware probe and
this evidence are checkpointed separately from the earlier profiling work and
other pending DLL edits. Diagnostic artifact CRCs below include that earlier
profiling work; the quiet builds do not depend on it.

## Scope

`RingPoll()` keeps a validated `MGLR200PrivateBridgeV2` snapshot locally and
reuses it for subsequent rendering entries in the same burst. The native query
only copies context fields; it neither validates driver generation nor performs
recovery. The existing epoch comparison still runs for every rendering entry.

The snapshot is invalidated before PRESENT processing, on a sync-sequence
change, and before a state-batch failure/retry. It disappears on every return
from RingPoll, before any mailbox/control/lifecycle/event handler can run.
No other call inside the current burst changes the native target. Fatal paths
exit the burst, so they cannot leave a reusable snapshot behind.

This deliberately does not cache across host dispatch iterations, remove the
V1 lookup in DrainSemanticFence, or change external-reset detection. Future
native control/flush/recovery calls inside RingPoll require a fresh lifetime
review. `MGLPPC_HOST_TARGET_CACHE=0` provides a separately built control.

## Build And Profiling

All direct host targets built cleanly with the unchanged GCC 6.5/O2/68020
flags and existing native import archive. No native rebuild was invoked.
The cache-off quiet binary is byte-identical to the deployed `98DE2066` helper.

| Host | Bytes | CRC32 |
| --- | ---: | --- |
| offq, cache=0/profile=0 | 36536 | 98DE2066 |
| cacheq, cache=1/profile=0 | 36596 | 718DD2B5 |
| offp, cache=0/profile=1 | 43212 | AD51E02D |
| cachep, cache=1/profile=1 | 43364 | F58132A1 |

Use distinct `R200_HOST` output names when changing the flags. For example:

```text
make -f Makefile.dll.r200.gcc /tmp/opencode/helper-cache/cacheq R200_HOST=/tmp/opencode/helper-cache/cacheq MGLPPC_PROFILE=0 MGLPPC_PACING=0 MGLPPC_HOST_TRACE=0 MGLPPC_HOST_TARGET_CACHE=1
```

One matched, same-session 600-frame profiling pair:

| Metric | Cache Off | Cache On |
| --- | ---: | ---: |
| V2 queries, including one registration | 2032 | 746 |
| Ring cache hits | 0 | 1286 |
| Query-stage EClock ticks | 212271 | 105133 |
| Query elapsed per frame, 709379 Hz | 0.499 ms | 0.247 ms |
| FPS | 56.390 | 56.390 |

The cache removed 63.3% of V2 queries and about half the measured query time.
These are elapsed intervals, not exclusive CPU time. Other host and driver
work dominates the frame, so this does not predict a large throughput gain.

## Physical Acceptance

`examples/r200_target_cache.c` queues 16 clear/draw/present frames before each
RGB readback. It checks three groups in each of two recreated contexts. The
sample is away from the triangle; synchronous two-buffer FLIP expects the
previous frame in the new back buffer, whereas BLIT/window expect the latest
clear. Cache-off controls established those expected values on hardware.

- Cache-off window and FLIP: 6/6 RGB checks each, zero GL errors.
- Cache-on profiled window, BLIT and FLIP: 6/6 each, zero GL errors.
- Cache-on quiet FLIP: 6/6, zero errors.
- Profiled FLIP: queries fell from 194 to 98 with 96 hits; the cache was exercised
  while targets alternated, not merely while the same buffer stayed selected.
- Cache-on quiet PPC texture update: 7,127 checks, zero failures, two contexts,
  18 updates and 16 frames; host confirms 50 executions / 16 presents.
- Final installed-helper smoke: 64 gears frames, 217 executions / 64 presents,
  zero errors and normal shutdown.

The probe ELF is `34416AC1` (110814 bytes); converted `target` is `EA752362`
(88648 bytes). It builds through Makefile.import.gcc's explicit
`.../r200_target_cache_ppc.warpelf` target. No Python tests were added or run.
Recovery/error injection and native resizing were not exercised; the protocol
has no native resize route inside a RingPoll burst.

## Quiet Throughput

Three independent bridge cold reboots per configuration, alternating
A1/B1/A2/B2/A3/B3. These are not physical power cycles. Same native bootstrap,
then InitPPC exactly once per boot. 640x480x32 fullscreen BLIT, two buffers,
no sync, host priority -1, 1 MiB stack, 600-frame precalc plus final drain.
All diagnostics were off. Only the helper cache flag changed.

| Helper | A/B1 | A/B2 | A/B3 | Median FPS |
| --- | ---: | ---: | ---: | ---: |
| Cache off | 57.142 | 56.285 | 56.497 | 56.497 |
| Cache on | 57.692 | 57.142 | 56.818 | 57.142 |

DateStamp ticks: off 525/533/531; on 520/525/528. Median improvement is
1.14% calculated from ticks: modest, with overlapping sample ranges. Every
run completed 600 frames with zero errors and 2031 executions / 600 presents.

Fixed stack: PPC DLL `31878AB6`, precalc client `1F3401F7`, native library
`2F05868E`, Radeon9200.chip `9768419A`, Prometheus.card `18A453D6`. The other
agent's spin-first and undeployed bind-cache DLL changes were not included.
Target: physical 68060/MPC7410, RV280 5964/64 MiB VRAM, bridge
192.168.1.21:2345. Existing Prometheus/.9200/DVI monitor configuration unchanged.

Follow-up requested by the user: normal (non-precalc) gears at 640x480x16
fullscreen BLIT, two buffers, no sync, priority -1 completed 600 frames in
522 DateStamp ticks: **57.471 FPS**, zero errors, 2031 executions / 600 presents.
This was a single run in the existing healthy session, not another three-boot
comparison. Logs: `DH2:hostcache/g16-client.log` and `g16-host.log`.

## Deployment

The cached quiet helper is installed as `DH2:gearsstut/mglhost_pacing0`, CRC
`718DD2B5`. Its original is preserved as `DH2:hostcache/previous-host`, CRC
`98DE2066`. Driver/card/native/DLL files were not replaced. No helper/client
remains running and temporary environment settings were cleared.

Archive `DH2:hostcache/package.zip`: 133302 bytes, CRC `8D29DE5E`. Transfers,
extracted files and installed/rollback helpers were CRC-verified sequentially.
Raw logs are in `DH2:hostcache`; combined `runs.log`/`hosts.log` and the two
profile logs were CRC-verified on download into `/tmp/opencode/helper-cache`.
