# MiniGL Hardware Profile

Follow-up: [HOST_TARGET_CACHE.md](HOST_TARGET_CACHE.md) records the subsequently
validated helper cache and current helper/rollback identities. The measurements
below describe the earlier uncached helper.

2026-09-07. Validated driver work was committed separately as `98192a2` in
`/home/mirek/p96-driver`. Pre-existing lighting/operational edits were not staged.
The installed driver binary still includes the previously documented lighting
edit. Profiling changes in this MiniGL tree remain uncommitted.

## Measurement

Physical 68060/MPC7410 + Prometheus/RV280, bridge `192.168.1.21:2345`.
Same 640x480x32 fullscreen BLIT, two buffers, no sync, priority -1, 600-frame
precalc workload. Driver `9768419A`, card `18A453D6`, native library `2F05868E`
were unchanged. No driver installation or reboot occurred during this profiling
session. These are same-session diagnostic repeats, not cold-boot benchmarks.

An isolated detached `abac846` worktree at
`/tmp/opencode/minigl-profile-src` excludes the undeployed bind-cache changes.
Profile-off rebuilds reproduced the exact deployed binaries: normal client
`65B0B783`, precalc client `1F3401F7`, host `98DE2066`, DLL `31878AB6`.

The profiling client uses the existing `MGL_PERF_COUNTERS_HINT`: reset/enable
after setup, run the unchanged fixed loop and final glFinish, then disable and
query counters after timing. A stable 64-bit PPC timebase snapshot at each end
is calibrated against the existing whole-run DateStamp interval. The measured
rate is about 24.74 MHz; the DLL's short Delay(2) estimate ranged from 26.9 to
36.4 MHz and is not used to convert measurements. Protocol deadlines and their
existing clock calibration were not changed.

The host keeps stage counters in RAM. Native counters are captured before
context deletion and printed only after cleanup. The diagnostic DLL likewise
writes transport counters only after successful teardown, with a full-lifetime
wrap-risk indicator. Counters are 32-bit, overlapping and not CPU-exclusive.
All observed wrap/error indicators were zero.

## Main Result

Three complete transport profiles:

| Run | FPS | Publish Time/Frame | Ring-Space Wait/Frame | Non-Wait Publish |
| --- | ---: | ---: | ---: | ---: |
| full1 | 56.818 | 12.64 ms | 12.59 ms | 0.050 ms |
| full2 | 55.658 | 13.15 ms | 13.10 ms | 0.050 ms |
| full3 | 55.248 | 13.31 ms | 13.26 ms | 0.050 ms |

**About 99.6% of the publish interval is waiting for ring space.** This is the
`head - completed >= slotCount` loop in `R200PPCPlatformPublishSlot`, including
its refreshes and Delay(1) yields. It is not evidence that the 68k CPU or GPU is
busy for that entire interval. Present-record queue waits add roughly 1.55-1.70
ms/frame, recorded separately in barrierStallTicks.

The non-wait residual includes copying, vertex-cache flushes and publication.
The narrower publishFlushTicks counter covers only ring-slot data flushing,
not the earlier vertex-aperture flush. PublishCount=2631 includes 600 PRESENT
entries; actual rendering submissions remain 2031.

PPC frontend timing is stable at about 2.50 ms/frame; serialization is about
1.99 ms/frame. Frontend encloses serialization, so those must not be added.
Similarly, executeTicks measures publishing, not GPU execution. Tiny waitTicks
does not mean no waiting: ring-mode WaitSlot returns immediately, while the
real wait is charged inside publishing. drawTicks=0 is expected for this
immediate-mode workload; that counter covers the indexed-array path.
serializeStallTicks describes nested stalls already excluded through execute's
delta; it must not be subtracted from serializeTicks a second time.

## Host Breakdown

Representative full2 host intervals (inclusive, not additive across CPUs):

| Host Work | Approximate Time/Frame |
| --- | ---: |
| Entire ring processing | 15.60 ms |
| Radeon3D call bracket | 9.36 ms |
| Backend bridge query | 0.46 ms |
| Pre-submit retirement wait | 1.20 ms |
| Pre-present fence drain | 0.43 ms |
| Native present call | 2.83 ms |
| Native backend's internal present counter | 2.64 ms |

The native present counter is inside the host present bracket. Native geometry
counters are zero because the host submits directly to Radeon3D. Driver inner
phase deltas in full1 total about 6.74 ms/frame; the larger host call bracket
also includes service work outside those timers and possible descheduling.
That difference is not automatically MiniGL CPU overhead.

Every profiled run had 600 frames, 1,656,000 input/submitted vertices, 15,000 draw
records and 2031 actual submissions. Timed-loop texture uploads, upload copies,
evictions and residency ticks were zero. The whole-context transport profile
recorded one successful 196608-byte source texture upload during setup.

## Boundaries And Artifacts

Profiling enabled versus disabled on the same client was checked separately
from host profiling. Initial diagnostic runs were 55.555 FPS (frontend only),
56.285 (host only), and 55.762 (both, normal DLL). A final disabled-counter
control using the restored normal DLL/quiet host completed at 56.179 FPS.
These samples do not establish an optimization gain or precise overhead bound.

- Final profiling precalc client: `92597327`; normal: `7BECC805`.
- Final quiet diagnostic host: profile-off `98DE2066`, profile-on `1B851917`.
- Diagnostic DLL: `FAEBA5E4`, built with MGLPPC_PROFILE=1 and
  MGLPPC_AUTOREPORT=0. Normal rebuilt DLL: `31878AB6`.
- First client report used unsupported `%llu` formatting; its clock line is
  discarded. Final reports use 32-bit high/low fields and `%lu` only.
- Early host reports printed inside teardown; final host buffers native
  snapshots and prints after cleanup. Early host control/session totals are not
  used as exclusive execution costs.
- Build flags require distinct object/output directories; make does not track
  changed command-line defines automatically.

Raw evidence is under `DH2:mglprof`: `p1-*`, `h1-*`, `both1-*`, `full1-*`,
`full2-*`, `full3-*`, and `control-*`. Transport snapshots are `fullN-dll.log`.
Host downloads/builds are in `/tmp/opencode/mgl-profile/`.

The normal DLL was restored in the profiling directory and CRC-verified as
`31878AB6`; production `DH2:gearsstut` binaries were never replaced. The final
control exited normally, no host/client remains running, and temporary env
overrides were cleared.

## Next Focus

Profile the ring-space yield/pacing path and the host's per-entry consumption
work before touching vertex math, CRC or texture uploads. The coarse Delay(1)
inside a full-ring wait deserves a direct sleep-versus-busy-wait measurement;
do not remove it or any fence without proving scheduler/ownership safety.
