# Host classification and PPC wait regression

Investigation: 2026-09-10 through 2026-09-12, physical Amiga only.
The PPC command emitter and streamed-vertex architecture are retained.

## Confirmed host root cause

The installed Sonnet loader incorrectly identifies a 68k executable as PPC if
it finds the four uppercase bytes `RACE` anywhere in its loaded hunks. The name
`MGLPPC_UPLOAD_TRACE` contains exactly that signature. This was introduced with
the upload diagnostics, but its effect precedes execution of the option reads.

Source: `/home/mirek/SonnetAmiga/sonnetlib/powerpc.s`, commit
`34077367f68847729bc079d67167da38dfdeee96`:

- 3422-3453: raw-hunk scan, including the `RACE` comparison at 3434.
- 3455-3459: cache the PPC classification in file protection bits.
- 3469-3479: ordinary files instead clear the task's PPC flag and reload through
  the normal loader.
- 3526-3543 and 3251-3339: custom loading/allocation can select PPC memory and
  mark the task PPC. This is inappropriate for the 68k host's working data.

Hardware isolation used the same old static precalc client, native ABI-4
library, current driver pair, host priority -1, fullscreen 640x480x16, three
buffers, BLIT, sync off, 600 frames. The exact same host executable
`DH2:ibdraw/newhost-v4` (CRC32 **D0406387**, 40568 bytes) measured:

| File classification | FPS |
| --- | ---: |
| `0x00010000` (PPC) | 67.774 |
| `0x00020000` (normal 68k) | 140.752 |

Only metadata changed; CRC was verified unchanged before and after. All 2,031
submissions and 600 presents completed. The old static client's timer excludes
its final teardown; host counts and host timing independently corroborated the
fast reference. This is a single-run diagnostic comparison, not a cold-run
statistical study.

The source-block bisection was misleading until code layout was held fixed:
removing the HELLO environment block also removed the signature. Local-only
lookups, static buffers, moving reads to startup, and replacing the actual
GetVar instructions with equal-size stubs all retained the signature and stayed
slow. The options were unset, priority and global cache flags did not change,
and host idle sleeps were zero during rendering. Restoring old ring/main/fence
code and changing payload-header copying did not fix the regression.

## Permanent host protection

`ReadUploadTraceOption()` constructs the original environment-variable name
through a volatile buffer. It preserves the option and its behavior without
embedding the classifier signature in the binary. `tools/check_68k_host.py`
scans every linked host for the three known Sonnet classifier signatures and
fails the build if one appears. Quiet and trace/profile builds pass the check.

**Existing file metadata must also be corrected.** Replacing file contents or
checking CRC alone does not necessarily reset Sonnet's cached classification.
For the 68k host only, preserve unrelated protection bits and set:

```c
newProtection = (oldProtection & ~0x00030000UL) | 0x00020000UL;
```

Use the bridge's full protection-bit getter/setter, then read the bits back.
Do not apply this to the genuine PPC DLL or PPC clients. A fresh corrected host
was observed to change automatically from `00000000` to `00020000` on loading.

Corrected ABI-5 host: **0FAA4EFA**, 40588 bytes. Restored current native library:
**62343673**. Driver/card remain **CEB838A6 / 18A453D6**. No kernel or driver
replacement was needed for this correction. With this current consumer stack,
the old static precalc client reached **142.021 FPS**.

## Remaining DLL pacing cost and correction

The current DLL client still measured **61.475 FPS** fullscreen after fixing
host classification. A matching instrumented run measured **59.760 FPS** and
263 DOS `Delay(1)` sleeps over 600 frames. Publish/present ring-stall counters
covered about 8.15 seconds. Unlike the old static frontend's busy polling,
these waits could park the producer until the next 50 Hz DOS tick.

Five back-pressure wait sites now use `MGLPPCYield()`, a PPC-native
`WaitTime(0, 100)` call. Spin budgets, readiness refreshes, epoch checks,
retirement ownership, and timeout bounds are retained. Mailbox and fixed IB
smoke waits retain their existing policy. Profile output explicitly reports
`yield_us=100`; the old `*_sleeps` fields now count these short yields and must
not be multiplied by a DOS tick to interpret new results.

With the same profiled workload, this changed **59.760 -> 102.040 FPS**, with
the same 1,656,000 vertices, 15,000 draw records, 2,031 submissions and zero GL
errors. The shared ring-stall counter fell from 201,561,000 to 99,050,645 raw
timebase ticks. These counters include setup/teardown and overlap other stages;
the client reports the measured timebase conversion rate.

## Workload and visual constraints

- The current DLL precalc client and the old static precalc client are distinct
  executables, even though both replay the same immediate-mode gear geometry.
- User-reported single-buffer fullscreen rendering was completely corrupted;
  its FPS is not an accepted rendering result. No further one-buffer runs.
- Windowed 16-bit presentation is a separate slow path: 2.802 FPS in the CP test,
  with normal completion and no GL error. Do not use it as the throughput target.
- The CP emitter remains opt-in and window-only. Fullscreen measurements here
  use semantic submission; CP acceptance must show positive `CP_RUN` counts.

## Verification and evidence

### Initial quiet recovery (2026-09-12, superseded below)

| Test | Before short yields | After | Result |
| --- | ---: | ---: | --- |
| Current DLL precalc, fullscreen 640x480x16, 3 buffers, sync off | 61.475 FPS | **101.694 FPS** | 600 frames, GL error 0 |
| Current DLL precalc, CP emitter, windowed 800x600x32, sync off | 46.875 FPS | **69.444 FPS** | 600 frames, GL error 0 |

The CP run recorded `CP_RUN dispatches=2031 dwords=511620 failed=0`; this was
real PPC-authored command submission. The fullscreen run uses the semantic
path because the CP emitter is still window-only. These are single samples,
as requested, in the current boot rather than three independent cold runs.
Do not describe the fullscreen number as a CP-emitter fullscreen result.

The final CP texture-update acceptance run passed **7,127 checks**, 2 contexts,
18 updates and 16 frames, with `CP_RUN dispatches=50 dwords=6360 failed=0`.

Initial 8,192-spin working set in `DH2:ibdraw` (superseded by the final DLL below):

| File | CRC32 | Bytes | Classification |
| --- | --- | ---: | --- |
| `mglhost` / `mglhost-fixed` | `0FAA4EFA` | 40588 | normal 68k `00020000` |
| `minigl_ppc.dll` | `A02F353A` | 294752 | genuine PPC; never force normal |
| `yield.elf` | `DBEF9743` | 360501 | PPC conversion input |
| `precalc` | `1F3401F7` | 187136 | unchanged quiet PPC client |

The previous DLL is `DH2:ibdraw/before-yield.dll` (`77239FB3`). The previous
host is `DH2:ibdraw/host-before` (`F98CB3DF`); it retains the bad signature and
is evidence, not the preferred executable. The active native library is again
ABI 5 (`62343673`), not the temporary ABI-4 historical package. Driver/card
remain `CEB838A6 / 18A453D6` throughout this continuation.

Copying the corrected host over `mglhost` **did retain the old `00010000`
classification**. It was explicitly corrected to `00020000` and read back.
This independently demonstrates why checking only the new file CRC is not enough.

Selected raw results are archived in `host_loader_evidence/` beside this note.
The final native bootstrap passed and no client, DLL, or host remained running.
The current boot already has PPC initialized. `MGLPPC_HOSTPRI=-1` remains the
explicit benchmark setting; CP-emitter/smoke/verbose/upload-trace toggles and
native present/buffer overrides were unset after testing.

### Full performance recovery: avoid unnecessary scheduler intervals

Stopping at 101 FPS did not recover the historical 130-142 FPS range. Further
measurement found **zero host idle sleeps during rendering**, including with
the corrected fast host, so its idle policy was left unchanged.

Direct timebase measurement around `WaitTime(0,100)` showed 198 calls consuming
99,165,482 ticks at a measured 24,665,830 Hz: about **20.305 ms per call**, not
100 us. The requested timeout was short but actual rescheduling was not. The
Sonnet implementation queues a deadline and services it from the scheduler's
wait-list scan (`ppcfunctions.p:9242-9391`, `setupppc.p:3183-3224`); no kernel
patch was made. The old 8,192-iteration polling budget therefore still yielded
too readily while a slot could become available much sooner.

The final policy permits **131,072 bounded polling iterations** before a yield,
keeping the existing cache refreshes, error checks, retirement requirements,
five-million-iteration limits and CP time deadline. Eventual yielding remains.
The profiled comparison changed **100.671 -> 136.363 FPS**, with yield calls
falling **198 -> 33**. `PPC_YIELD` now reports actual elapsed timebase ticks
separately from the requested duration, with explicit DLL-lifetime scope.

Final quiet results (600 frames each, one requested sample per condition):

| Current DLL precalc workload | FPS | GL error | Submission proof |
| --- | ---: | ---: | --- |
| Fullscreen 640x480x16, three buffers, BLIT, sync off | **134.529** | 0 | 2,031 submissions / 600 presents |
| Windowed 800x600x32, CP emitter, sync off | **68.337** | 0 | 2,031 IBs / 511,620 CP dwords / 600 presents |

The CP texture-update acceptance test again passed **7,127 checks**, two
contexts and 18 updates. The fullscreen result uses semantic submission, not
an unimplemented fullscreen CP mode. The CP emitter remains in the DLL and is
validated separately; windowed 32-bit throughput is not the fullscreen target.

Final DLL: `DH2:ibdraw/minigl_ppc.dll`, CRC32 **02605851**, 294768 bytes.
Conversion input: `DH2:ibdraw/spin131.elf`, CRC32 **3910CD27**, 360550 bytes.
The 8,192-spin quiet DLL remains as `DH2:ibdraw/spin8k.dll` (**A02F353A**).
Host/native/driver/card identities are unchanged from the initial quiet recovery.
The profile-only DLL in `DH2:ibpace` is not the quiet installed working DLL.

All 12 local Python test scripts passed with the final polling policy. The
production yield-statistics arithmetic is tested across a 32-bit timebase wrap;
the ready/no-yield and terminal-retention regression checks remain active.
Raw recovery logs are archived beside the earlier results.
The final native bootstrap passed; Status and the port list confirmed no
remaining client, DLL or host. Native ABI 5 and the normal host classification
were rechecked. Driver and card were not changed in the polling investigation.

`test_68k_host_classifier.py` checks signature detection and the production
option-name helper. The lifecycle/transport tests cover all three semantic
handoffs, all five yielding waits, CP ownership and retirement, error retention,
and the real yield helper's `WaitTime(0,100)` arguments. All 12 Python scripts
and the six existing regression modes passed. Existing backend warnings remain;
the changed host and yield helper compiled without new warnings.

Target evidence remains under `DH2:ibdraw` (old/new/probe logs, `class-fixed.log`,
`fixed-static.log`) and `DH2:ibpace` (`pace-client.log`, `yield-client.log`).
Current-session CRC-verified pulls and build manifests are under
`/tmp/opencode/hello-probes`; do not rely on temporary files surviving another
harness restart. The target retains the full historical probe set.
