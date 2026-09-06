# Host Scheduling Follow-Up

2026-09-06, following quiet baseline commit `1cfc022`. Public ABI, DLL,
benchmark clients, double buffering, native library and Radeon driver pair
are unchanged. No per-frame timing/logging was re-enabled.

## Startup Safety Fix

The old host passed the first byte of `MGLPPC_HOSTPRI` to `SetTaskPri`:
`-1` became ASCII `'-'` (45), and `5` became 53. A busy host at those
priorities can starve input and the WarpOS gateway.

`ReadHostPriority` now parses a decimal integer and rejects malformed,
potentially truncated, and out-of-range settings before opening the backend.
The accepted range is -20 through +2. Unset/empty preserves the inherited
priority; **the default was not changed**. An explicitly configured priority
and its previous value are printed once at startup, outside benchmark timing.

The production parser passes 27 ASan/UBSan cases, including negative values,
overflow, bad suffixes, full buffers and unset/empty values. Real Amiga runs
verified that `5` and an oversized zero-padded value are rejected without
opening the backend or leaving a host running.

Do not set this variable for an older host: its ASCII parsing bug remains.
The isolated fixed binary is `DH2:gearsclean/mglhost-priority`. All test
environment overrides were removed after use; no ENVARC changes were made.

## Measurements

All samples: 150 frames, 640x480x32, the existing envmapped gear, one final
drain, 1,000,000-byte shell stack. Fullscreen uses two buffers. The series used
the already initialized physical Amiga session, not independent cold boots.
These are diagnostic single samples, not accepted cold-boot medians.

The first four runs isolate priority using the same precalc executable and
fullscreen BLIT with sync off. The return-to-zero control checks run order.

| Log Prefix | Priority | Mode | Variant | Ticks (50 Hz) | FPS |
| --- | ---: | --- | --- | ---: | ---: |
| p0 | 0 | Fullscreen BLIT, no sync | Precalc | 386 | 19.430 |
| p1 | -1 | Fullscreen BLIT, no sync | Precalc | 180 | 41.666 |
| p5 | -5 | Fullscreen BLIT, no sync | Precalc | 179 | 41.899 |
| p0b | 0 | Fullscreen BLIT, no sync | Precalc | 392 | 19.132 |
| p1n | -1 | Fullscreen BLIT, no sync | Normal | 179 | 41.899 |
| p1w | -1 | Windowed | Normal | 180 | 41.666 |
| p1wp | -1 | Windowed | Precalc | 183 | 40.983 |
| p1f | -1 | Fullscreen FLIP, sync on | Normal | 361 | 20.775 |
| p1fp | -1 | Fullscreen FLIP, sync on | Precalc | 362 | 20.718 |
| p1v | -1 | Fullscreen BLIT, no sync | Precalc | 183 | 40.983 |

All ten runs: `error=0`, `executes=508`, `presents=150`, and clean process
exit. Total 1,500 frames. Bootstrap passed before the series and after the
first nine runs; the last run also exited with no client/DLL/host left in
`Status`. The delayed host log close on p1fp was re-read successfully after
exit; it was not a render or teardown failure.

Raw files: `DH2:gearsclean/<prefix>-client.log` and `<prefix>-host.log`.
Combined `priorities.log` (1,172 bytes) and `pri-hosts.log` (4,430 bytes) were
CRC-verified on download into `/tmp/opencode/gears-hygiene/`.

## Interpretation And Next Gate

Lowering only the host priority to -1 approximately doubled BLIT throughput.
Restoring priority 0 restored the roughly 19 FPS result. This supports 68k
scheduling contention as a first-order bottleneck, rather than the intrinsic
cost of PPC dispatch or repeated gear trig. It does not attribute every wait
inside the driver or WarpOS gateway.

-5 offers no demonstrated advantage over -1 in these samples. Normal and
precalc are still essentially at parity, including at the higher throughput.
The safe next candidate is -1, not a progressively lower priority.

Synchronized fullscreen FLIP remains near 20.7 FPS while windowed/BLIT reaches
about 41 FPS. The next code investigation is therefore the native presenter,
not the event protocol or a rewrite of draw submission. The running code is
`classic_68k/dev/MiniGL_Library_Source_Code/library/r200_surface.c`:
`CompletePendingFlip`, `WaitTargetBufferSafe`, and `ChangeBuffer` use bounded
`WaitTOF` polling. `safePort` and `displayPort` are real `CreateMsgPort` ports.
Any replacement must wait on actual notification signals with a real bounded
timeout, not an unbounded `Wait`; preserve the two-buffer safe-target and
teardown invariants. No native presenter changes were made in this step.

Before making -1 the default or accepting a present optimization, perform
independent cold-boot comparisons with the full physical-device protocol and
validate input/audio/IO contention in an actual application, not only gears.
The ordinary host default and installed native stack remain unchanged now.

## Artifact Provenance

PPC DLL CRC `38FEBCCA`; precalc CRC `1F3401F7`, both re-checked on target.
Native `minigl.library` CRC `DC041695`; Radeon9200.chip CRC `07A7193D`;
Prometheus.card CRC `18A453D6`, all re-checked and unchanged.
Physical configuration and ToolTypes are recorded in
`GEARS_HYGIENE_BASELINE.md`. This session reported powerpc.library 17.15.

The first nine runs used the parser-fix host CRC `1D99F8A9` (36,536 bytes).
The final parser conservatively rejects a completely filled GetVar buffer;
its host CRC is `98DE2066`, also 36,536 bytes. Binary comparison shows exactly
one byte changed, the startup length limit; hot-path code is identical.
Run p1v and the oversized-setting rejection used this final binary.

Final host SHA256:
`a5b3626593fb7b2e09315d745c401ae5f4f1e5fc4bcddc55e6813ae6904706af`.
Deployment `pri-final.zip`: 19,470 bytes, CRC `00A8CF34`. Transfers and the
extracted host were CRC-verified. The baseline host file was not overwritten.

Build: `/opt/amiga/bin/m68k-amigaos-gcc` 6.5.0b, the same 68020/O2 flags,
`MGLPPC_PROFILE=0`, `MGLPPC_HOST_TRACE=0`; no warnings. The source change is
startup-only. All pre-existing unrelated worktree changes remain excluded.

```sh
python3 tests/test_r200_host_priority.py
python3 tests/test_r200_context_lifecycle.py
make -f Makefile.dll.r200.gcc /tmp/opencode/mglhost-priority R200_HOST=/tmp/opencode/mglhost-priority
```
