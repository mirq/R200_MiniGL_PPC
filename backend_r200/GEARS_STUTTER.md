# Periodic Stutter Investigation

2026-09-07, based on `d1525ed`. The user confirmed the symptom occurs in the
fixed `-frames` benchmark, not only in the interactive title/input loop.
This step changes PPC wait ordering, not the native presenter or driver.

## Avoidable Tick Sleeps

The publish, present-ring-space and explicit-drain loops refreshed shared
completion state but still executed `Delay(1)` before checking whether the
refresh had made the condition ready. The first iteration uses spin zero, so
it always reached that sleep, even when the cached busy/full snapshot was
already obsolete. On this machine that adds a nominal 20 ms DOS tick.

Segment rotation also scanned a cached retirement value and could sleep
before its first refresh. Retirement now refreshes before pool selection;
all four waits recheck refreshed readiness before sleeping.

Necessary yields remain when fresh state is genuinely busy. The existing
poll throttling, timeout limits, cache handoffs, ring ordering, GPU-fence
retirement and double-buffer safety rules are unchanged. Host consumption is
not substituted for GPU retirement. No `WaitBlit` or native flip wait was
removed, and host idle pacing/priority defaults were not changed here.

`tests/test_r200_context_lifecycle.py` exercises ready-after-refresh publish,
present and drain paths, fresh retirement selection, and genuine busy-ring /
unretired-pool cases that still yield. `--regress-waits` disables the new
readiness checks in the test translation unit and detects the unnecessary
sleep. Existing cache, registration, CRC, readback and texture-order tests
continue to pass.

## Optional Pacing Diagnostic

`MGLPPC_PACING=1` builds a separate host diagnostic without enabling
`MGLPPC_PROFILE` or trace logging. It takes one native EClock sample after each
successful completed present and keeps only an interval histogram and the 16
longest gaps/frame numbers in static RAM. There are no allocations, divisions,
or print/file writes in the sampled path. All output occurs during normal
shutdown, after the client's timed rendering interval.

The first present seeds the timestamp; N presents give N-1 intervals. Bins
are noncumulative, with millisecond boundaries 1/5/10/20/33/50/100 rounded up
to whole clock ticks. Frame indices identify the successful present ending
each gap. This measures completed native presentation, not physical scanout
or the PPC's asynchronous return from `mglSwitchDisplay`.

It is off by default. The pacing-disabled host is byte-identical to the
existing priority-fixed host (`98DE2066`). Unit tests cover bin boundaries,
unsigned clock wrap, top-gap ordering/ties, reporting after timer close, and
zero clock samples/output on the disabled path. Diagnostic state spans the
whole host session; a context recreation gap would count in a multi-context
run. Every benchmark here uses one context per host.

## Physical Results

All runs used the physical target, host priority -1, unchanged precalc client,
640x480x32, the existing envmapped gear, and a 1,000,000-byte shell stack.
Fullscreen explicitly used two buffers. BLIT runs disabled sync; FLIP runs
enabled sync. No live title/input/profiling was reintroduced into the client.
These are same-session A/B diagnostics and long-run checks, not independent
cold-boot medians or full release acceptance.

The first three runs used the same pacing-enabled host, changing only the DLL
in an A/B/A order. Both old and new runs emitted identical execute/present
counts for a given workload.

| Order / Prefix | DLL | Mode | Frames | Ticks (50 Hz) | FPS | Gaps >=33 ms | Gaps >=50 ms | Worst Gap |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 / base | Baseline | BLIT | 1500 | 1762 | 42.565 | 135 | 19 | 98.837 ms |
| 2 / fix | Wait ordering | BLIT | 1500 | 1653 | 45.372 | 69 | 0 | 48.866 ms |
| 3 / base2 | Baseline | BLIT | 1500 | 1754 | 42.759 | 155 | 26 | 100.286 ms |
| 4 / qbase | Baseline | BLIT, pacing off | 3000 | 3516 | 42.662 | n/a | n/a | n/a |
| 5 / qfix | Wait ordering | BLIT, pacing off | 3000 | 3220 | 46.583 | n/a | n/a | n/a |
| 6 / ffix | Wait ordering | FLIP | 600 | 1494 | 20.080 | 594 | 385 | 92.332 ms |
| 7 / fbase | Baseline | FLIP | 600 | 1487 | 20.174 | 587 | 387 | 98.818 ms |
| 8 / qwin | Wait ordering | Windowed, pacing off | 1500 | 1703 | 44.039 | n/a | n/a | n/a |

BLIT interval histograms (bins <1, 1-5, 5-10, 10-20, 20-33, 33-50,
50-100, >=100 ms; each row totals 1499):

```text
base:  0,0,0,643,721,116,19,0
fix:   0,0,0,678,752, 69, 0,0
base2: 0,0,0,700,644,129,25,1
```

All eight runs completed with `error=0`, matching host present counts and
clean client/DLL/host exits: 13,200 frames and 44,682 execute batches total.
The host's shutdown pacing report sometimes finished after the client's first
`Status`; its later completion was verified before another launch. No forced
termination or hardware freeze occurred. Bootstrap passed before and after
the series. Test environment overrides were removed; PPC remains initialized
in the current boot.

The 3000-frame baseline exceeded the tool's synchronous capture timeout but
completed normally in 70.32 s, as verified from its log and idle process state.
The changed quiet run was collected asynchronously and completed in 64.40 s.
Because collector activity differed for those two runs, their approximately
9.2% FPS difference is supporting evidence, not a precision performance claim.
The matched diagnostic A/B/A shows roughly 6% higher throughput and a clear
reduction in the long-gap tail. No unseen frame-pacing values are inferred
for the pacing-disabled runs.

## Remaining Work

This removes one demonstrated source of intermittent BLIT delays, not every
source of stutter. The changed run still has some 33-49 ms gaps. BLIT is also
unsynchronized with monitor refresh, so completed-present timing is not a
guarantee of perfectly even visible motion or tear-free scanout.

The FLIP comparison shows no material throughput improvement and still has
85-99 ms pauses. Its next target remains the native
`classic_68k/dev/MiniGL_Library_Source_Code/library/r200_surface.c` notification
waits. Replace coarse `WaitTOF` polling only with a real bounded message/signal
wait, preserving synchronous two-buffer retirement and teardown. Do not remove
the final two-buffer flip completion to obtain higher submission-only FPS.
No native library or driver change was made in this step.

## Artifacts And Evidence

Everything is isolated in `DH2:gearsstut`; `DH2:gearsclean` remains unchanged.
The selected test DLL is the changed build, and `baseline.dll` preserves the
prior DLL. Native library/driver configuration is the one recorded in
`GEARS_HYGIENE_BASELINE.md` and `GEARS_SCHEDULING.md`.

| Artifact | Bytes | CRC32 |
| --- | ---: | --- |
| Baseline DLL | 257984 | 38FEBCCA |
| `waitfix.elf` | 322011 | ED2C475A |
| `waitfix.dll` / selected `minigl_ppc.dll` | 258144 | 31878AB6 |
| Pacing-enabled host `mglhost_pacing1` | 37632 | 00732E13 |
| Quiet host `mglhost_pacing0` | 36536 | 98DE2066 |
| Unchanged `precalc` | 187136 | 1F3401F7 |
| Deployment `stutter.zip` | 185294 | 1AF89C9A |

Transfers, extracted new artifacts and DLL selections were CRC-verified.
Conversion used target `Elf2Exe2`; no second InitPPC was run in this boot.
Raw `<prefix>-client.log` and `<prefix>-host.log` files remain on DH2.
Combined `runs.log` (954 bytes) and `pacing.log` (8818 bytes) were downloaded
with CRC verification to `/tmp/opencode/gears-stutter/`.

The host builds with pacing on/off without warnings. The WarpELF passes the
unchanged 134-field dispatch and converter-contract checks; the pre-existing
frontend compile warnings remain, as documented in the hygiene baseline.

```sh
python3 tests/test_r200_host_pacing.py
python3 tests/test_r200_context_lifecycle.py
python3 tests/test_r200_context_lifecycle.py --regress-waits
make -f Makefile.dll.r200.gcc warpelf
make -f Makefile.dll.r200.gcc /tmp/opencode/mglhost_pacing1 R200_HOST=/tmp/opencode/mglhost_pacing1 MGLPPC_PACING=1
```
