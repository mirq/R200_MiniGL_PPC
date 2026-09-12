# Native Flip Notification Wait Experiment

2026-09-07, based on `abac846`. **Not accepted as a performance change.**
Production sources and the installed native library were restored after
testing. `candidate.patch` preserves the implementation, diagnostic and CPU
tests; it is not applied in the normal build.

## Question And Implementation

Would replacing MiniGL's 100-TOF notification polling with a bounded
message/signal wait reduce the synchronized double-buffered FLIP stalls?

The prototype used an embedded per-context timer request, a separate task-owned
PA_SIGNAL reply port and a two-second UNIT_MICROHZ deadline. It retained all
GPU waits and two-buffer rotation. Both DBufInfo replies were consumed before
reuse, mapping the new request's SafeMessage to the old bitmap. The timer was
armed once per wait and reaped after completion or AbortIO.

Related safety work retained resources on notification failure, restored the
public wrapper's pre-cleared current-context pointer for retries, quarantined
ports when a failed context's owning task closed the library, and treated
GL_FRONT as intentional visible-buffer rendering. The host propagated retained
native contexts instead of reporting a successful destroy/shutdown.

The active public dispatch layouts remained unchanged: native ABI 5 (152/152
fields) and PPC ABI 3. There were no driver or PPC DLL changes.

## Results

The unmodified native rebuild matched the installed library byte-for-byte
by size and CRC: 125896 bytes, DC041695. Same GCC 6.5.0b, 68060/O2/native
freestanding flags. Tests used the fixed PPC DLL 31878AB6, precalc client
1F3401F7, host priority -1, 640x480x32, two buffers and forced FLIP with sync
on. Timing/pacing output was emitted after the run, not live.

| Order / Prefix | Native Library | Frames | Ticks (50 Hz) | FPS | Gaps >=50 ms | Worst Gap |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| base | Installed/unmodified | 600 | 1460 | 20.547 | 366 / 599 | 86.945 ms |
| smoke | Candidate | 12 | 30 | 20.000 | 8 / 11 | 67.925 ms |
| new | Candidate | 600 | 1503 | 19.960 | 393 / 599 | 102.430 ms |
| diag | Candidate with stage timing | 300 | 746 | 20.107 | 198 / 299 | 84.994 ms |
| back | Original restored | 600 | 1462 | 20.519 | 368 / 599 | 91.726 ms |

All runs returned `error=0`, matched host present counts and exited normally.
No freeze or forced termination occurred. These were same-session exploratory
trials, not independent physical cold-boot medians. Bootstrap passed before
testing and after restoring the original library.

## Decisive Attribution

The diagnostic measured actual `ChangeScreenBuffer` calls separately from
`WaitBufferNotifications`, with only end-of-context reporting:

```text
[mgl] flipdiag change_calls=300 change_ticks=6384631 notify_calls=300 notify_ticks=29948 clock=709379
```

- `ChangeScreenBuffer`: **30.0010 ms/call**, 9.0003 seconds total.
- Notification waiting: **0.1407 ms/call**, 0.0422 seconds total.
- Thus almost all of the measured native flip-specific time was inside the
  P96/Intuition call, not MiniGL's notification wait. The prototype did not
  materially improve either throughput or the long-gap tail.

The original native library was restored, CRC-verified and expunged with
`Avail Flush` before the return control. The previously accepted PPC
wait-ordering fix was kept. Native timeout/notification safety fixes may be
evaluated independently, but this experiment is not evidence of a speedup.

## Next Boundary

Instrument P96's driver callbacks, without hot-path printing, before changing
their behavior. Read-only inspection of `/home/mirek/p96-driver/src/radeon_mode.c`
found:

- `RadeonSetPanning` (767-832): direct pitch/offset writes, no explicit wait.
- `RadeonWaitVerticalSync` (972-983): up to 100000 status polls with a
  `RadeonDelayUs(1)` between unsuccessful polls; split measurements by `end`.
- `RadeonGetVSyncState` / `RadeonGetVBeamPos`: nonblocking reads which P96 may
  itself poll repeatedly.
- `RadeonSetGC` / `ProgramTiming`: programs timings on every invocation.
- `RadeonSetClock` / `ProgramClock`: unchanged-clock shortcut, otherwise a
  50 ms delay; count slow-path hits rather than assume it runs per flip.
- `radeon_accel.c:RadeonWaitBlitter` (1515-1525): another potential engine drain.

The driver clears BIF_VBLANKINTERRUPT. Closed-source P96/Intuition controls the
call order, polling and scheduling; these sources do not prove repeated
modesetting or identify how the measured 30 ms splits across those callbacks.

## Reproduction And Artifacts

The patch is relative to the repository root. To inspect/retest the archived
experiment, apply it to its base and run:

```sh
python3 -B tests/test_r200_display_wait.py
python3 tests/test_r200_context_lifecycle.py
```

The native display harness passed 10 groups under ASan/UBSan and exercises
production functions, including the public delete wrapper, late replies,
quarantine, GL_FRONT, and 2/3-buffer rotation. Generated mutations were rejected.
Mocks do not establish OS concurrency or physical cache/device behavior.

Build with the native Makefile's `dispatchtable` target followed by `all`.
`DISPLAY_WAIT_DIAG=1` selects the attribution build. Pre-existing native
compiler warnings were not suppressed; generated dispatch output remained
unchanged and the libraries had no unresolved symbols. The new host variants
built without warnings.

| Artifact | Bytes | CRC32 |
| --- | ---: | --- |
| Original/restored native library | 125896 | DC041695 |
| Candidate native library | 127404 | 744FB882 |
| Diagnostic native library | 127696 | E902D6C4 |
| Pacing host with retained-context checks | 37744 | 263846A0 |
| Quiet host with retained-context checks | 36644 | D1C8D8C7 |
| Candidate ZIP | 98065 | 6005F58F |
| Diagnostic ZIP | 58561 | C1883C85 |

Both host variants and the original/candidate/diagnostic libraries remain in
`/tmp/opencode/flip-wait/` and `DH2:flipwait`.
`DH2:flipwait/native-base` is the original rollback copy. The candidate ZIP and
diagnostic ZIP were CRC-verified, as were extracted/installed libraries and
host files. Raw `<prefix>-client.log` / `<prefix>-host.log` remain in
`DH2:flipwait`. No second InitPPC was run during this boot; ENV overrides were
cleared after the tests. The Picasso96 pair was not modified.
Combined `clients.log` (591 bytes) and `hosts.log` (7309 bytes) were pulled
with CRC verification to `/tmp/opencode/flip-wait/`. All five runs total
2112 frames and 7150 execute batches, with matching present counts and no GL
errors. The machine was left idle and healthy, with PPC initialized for that
boot and no test environment overrides remaining.
