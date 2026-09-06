# Quiet PPC DLL Gears Baseline

Session: 2026-09-06. Branch: `feature/r200-gears-hygiene`, based on
`2bfbdff`. These are single samples per configuration, as requested, not
three-run cold-boot medians or a release/performance acceptance claim.

## Measurement

`examples/milestone_gears_stats_library.c` now uses a separate fixed loop for
`-frames N`. Setup, texture loading, optional precalculation, initial drain,
reporting and teardown are outside the timed interval. Two `DateStamp` calls
bracket N frames and a final `glFinish`, so the last asynchronous PRESENT is
included. There are no per-frame timestamps, input polling, title updates,
console/file logs, or FPS calculations in that loop. `GEARS_RUN` reports
`frames * 50 / ticks`, plus the final GL error. Failed runs are not FPS samples.

Interactive runs retain input and their title readout; title changes now occur
only at the 30-frame sample boundary. Fullscreen defaults to two screen
buffers. Windowed R200 uses its ordinary offscreen surface and blit regardless
of that request (`buffers=0` in the summary means not applicable).

The normal and precalc executables both use the PPC DLL import archive, not
the older statically linked vbcc semantic frontend. Precalc records this exact
client's `glBegin/glNormal3f/glVertex3f/glEnd` geometry before timing, then
replays it. Matrix animation, immediate GL calls, texgen, serialization and
presentation remain. The scene is the existing default `show=1` envmapped gear,
not a new two-gear workload. Removing repeated geometry math does not remove
all PPC work or prove that the PPC frontend is never a bottleneck.

The unused `MGL_W_ONE_HINT` was removed from setup: the current client header
passes 230 while the R200 frontend expects 234. The new setup error check
exposed this pre-existing INVALID_ENUM (88), previously ignored. The hint is
advisory/no-op on R200; this does not change the public enum or dispatch ABI.

## Quiet Stack

`Makefile.dll.r200.gcc` defaults `MGLPPC_PROFILE=0`. This removes transport
profiling, timeline capture, live/worst-frame autoreport defaults, host stage
clocks, and periodic reporting/fflush. Runtime GL perf hints remain available.
`R200_PERF_TIMING=1` MUST remain enabled: the calibrated timebase is also used
for mailbox eligibility, polling and five-second deadlines, not just profiling.
Diagnostic builds can use `MGLPPC_PROFILE=1` with a separate object/output
directory. Changing a make variable alone does not invalidate existing objects.

Host idle detection now uses actual mailbox/message/ring/fence activity rather
than counters latched by the two-second diagnostic report. Its existing
500-empty-poll / `Delay(1)` policy remains; this decoupling does change when it
becomes idle compared with the old report-coupled code. No priority tuning,
new sleep timer, event protocol, native flip algorithm or driver change was made.
Cheap host execute/present counts remain for the shutdown consistency check.

## Cache Ownership

Removing incidental OS/diagnostic work exposed repeated-session failures.
Two missing ownership boundaries were corrected before collecting the final
matrix:

- Payload allocations are 32-byte aligned and cache-line isolated, retaining
  their original allocation pointers for `FreeVec`.
- Registration cleans recycled payload storage before granting it to the PPC.
- The host cleans its in-place `PatchRecordTargets` writes before publishing
  slot completion. A later writeback/invalidate must not overwrite a new PPC
  publication with old host cache contents.
- The host publishes the complete initialized ring control block before its
  registration reply. The PPC invalidates the new lease's control lines before
  validation/use, including addresses reused by a previous context. Entry zero
  shares a cache line with the last slot addresses.

Public ABI 3, protocol 15 and ring layout/version 5 are unchanged. These cache
operations are required handoffs, not diagnostic work to remove for speed.

The lifecycle harness now models dirty host payload lines across two ring wraps
and checks registration publication/invalidation. Removing the release clean
with `--regress-cache` reproduces stale-payload corruption. Removing the control
publication with `--regress-register` fails the registration handoff assertion.
These are CPU-only models, not a proof of every physical cache behavior.

## Final Samples

All runs: 640x480, 32-bit render targets, 150 frames, 1,000,000-byte shell stack,
`chrome.ppm`, diagnostics/host-priority environment unset. Fullscreen explicitly
requests `-buffers 2`. The final sweep ran in one operator-cold-booted session,
one `C:InitPPC`, in the order shown. Later rows are same-session runs, not
independent cold-boot trials. Bootstrap passed before and after the sweep.

| Order / Log Prefix | Mode | Sync Request | Variant | Ticks (50 Hz) | Seconds | FPS |
| --- | --- | --- | --- | ---: | ---: | ---: |
| 1 / rdp | Fullscreen BLIT | Off | Precalc | 384 | 7.68 | 19.531 |
| 2 / rd | Fullscreen BLIT | Off | Normal | 384 | 7.68 | 19.531 |
| 3 / rc | Fullscreen BLIT | On | Normal | 400 | 8.00 | 18.750 |
| 4 / rcp | Fullscreen BLIT | On | Precalc | 396 | 7.92 | 18.939 |
| 5 / rb | Fullscreen FLIP | On | Normal | 449 | 8.98 | 16.703 |
| 6 / rbp | Fullscreen FLIP | On | Precalc | 464 | 9.28 | 16.163 |
| 7 / ra | Windowed blit | On | Normal | 371 | 7.42 | 20.215 |
| 8 / rap | Windowed blit | On | Precalc | 376 | 7.52 | 19.946 |

Every row: `error=0`, host `executes=508 presents=150`, normal client/DLL/host
exit confirmed with `Status`. Total: 1,200 frames and 4,064 execute batches.
`MINIGL_PRESENT` was set before starting each host and unset after testing.
Forced BLIT does not become tear-free/vblank-locked merely because sync was
requested; its FPS is completed rendering/presentation throughput, not unique
monitor refreshes. No hardware timing counters ran during this matrix.

Precalc gives no consistent material speedup in these samples. This supports
investigating submission/presentation/scheduling rather than repeated gear trig,
but the single samples do not quantify those individual costs or exclude other
PPC work. The old 202.7 FPS figure remains invalid due to its timer bug; the old
12.8 FPS result also used a different client loop and buffer configuration, so
neither is a controlled before/after comparison for this matrix.

Logs: `DH2:gearsclean/<prefix>-client.log` and `<prefix>-host.log`.
Combined evidence: `DH2:gearsclean/baseline.log`, CRC-verified pull to
`/tmp/opencode/gears-hygiene/baseline.log` (4,146 bytes).

## Provenance

Physical endpoint `192.168.1.21:2345`; Amiga 4000/68060, Prometheus, Radeon
9200/RV280 and MPC7410/WarpOS per the target configuration. Bridge reports
Exec 40.10, PAL, 2,093,056 bytes chip and 396,296,192 bytes fast RAM total.
VRAM size, ROM contents and PPC core frequency were not independently re-read
in this session. Workbench is 1920x1080; the bridge reports an 8-bit BitMap
descriptor, which is not an independent P96 pixel-format query.

Active monitor ToolTypes read from `SYS:Devs/Monitors/Prometheus.info`:
`BOARDTYPE=Prometheus`, `SETTINGSFILE=SYS:Devs/Picasso96Settings`,
`IGNOREMASK=Yes`, `DMASize=1024K`, `CP=YES`, `Output=DVI`.
The settings path differs from the `.9200` path recommended in AGENTS.md;
it was recorded as found, not modified during this benchmark work.

| Artifact | Bytes | CRC32 |
| --- | ---: | --- |
| Installed `LIBS:minigl.library` 12.0 | 125896 | DC041695 |
| Installed `Radeon9200.chip` 3.0 | 76588 | 07A7193D |
| Installed `Prometheus.card` 7.602 | 7796 | 18A453D6 |
| Final `mgl.elf` | 321839 | 60FF8712 |
| Final `minigl_ppc.dll` | 257984 | 38FEBCCA |
| Final `minigl_ppc_host` | 34980 | 1EF63816 |
| `gears.elf` | 223754 | D89F543F |
| Converted `gears` | 186224 | 65B0B783 |
| `precalc.elf` | 225034 | 814DCCBF |
| Converted `precalc` | 187136 | 1F3401F7 |
| `chrome.ppm` | 196623 | 14371EDA |
| Final deployment `hygiene.zip` | 429519 | F4A17C85 |

The installed native library and driver pair were never replaced. The new pair
and clients live only in `DH2:gearsclean`. ZIP transfers and extracted changed
artifacts were CRC-verified; conversion used `DH2:wosbuild/Elf2Exe2`. ZIP entries
must retain execute protection for the 68k host.

PPC compiler: adtools GCC 8.4.0; linker 2.23.2; MOS2WOS overlay.
PPC flags remain 604e/SysV/big-endian/O2. Native host compiler remains
`/opt/amiga/bin/m68k-amigaos-gcc` 6.5.0b, 68020/O2. No compiler/CPU tuning was
mixed into the measurements. Original dirty `bin/`, `obj/`, trace-build makefile
changes and the supplied untracked patches were preserved, not used as the new
build outputs. Isolated builds used `/tmp/opencode/gears-hygiene`.

## Patches

The supplied `patches/01..06` and combined patch apply to the original
Warp3D/Voodoo tree and a MorphOS GCC 15.2 build, not this R200 GCC 8.4 path.
They were inspected, not applied. The GCC declaration fixes for `SYS_Input, 0L`
and `<stdio.h>` are already present. Header-layout replacement, Voodoo clipping,
texture, and array changes do not belong in this R200 hygiene baseline.
Scoped classic-OS packing and the existing WarpOS gateway/converter contract
remain necessary even with GCC 8; older GCC is not a reason to ignore ABI rules.

## Failures Retained

The initial quiet pair (`8D114EAF` DLL / `A0061AC8` host) failed in later
fullscreen precalc runs with `statebatch_bad`, `dwords=122` and invalid draw/
segment values. Logs `bp-*` and `dp-*` are retained; a cold retry passed but did
not resolve repeated-session failure. Payload-release/alignment alone
(`77FE9EC5` DLL / `D2992803` host) still failed on the fourth run (`qcp-*`),
reporting 150 attempted frames, `error=89`, and `statebatch_bad`. These are
failures, not 1.1 FPS benchmarks. Operator cold cycles were used after each
freeze. Old source-artifact ZIPs remain as `precache.zip` and `pre-reg.zip`.

After adding control publication/invalidation, the final eight-run sweep above
completed. That is bounded regression evidence, not a claim that all long-run
or cross-CPU cache issues are eliminated.

## Build And Checks

```sh
make -f Makefile.import.gcc gears-benchmarks
make -f Makefile.dll.r200.gcc warpelf
make -f Makefile.dll.r200.gcc bin/minigl_ppc_host
python3 tests/test_r200_context_lifecycle.py
python3 tests/test_r200_context_lifecycle.py --regress-crc
python3 tests/test_r200_context_lifecycle.py --regress-cache
python3 tests/test_r200_context_lifecycle.py --regress-register
python3 tests/test_r200_readback.py
python3 tests/test_r200_texture_order.py
```

New client and host builds pass without warnings. The DLL passes the converter
relocation/undefined/COMMON/start checks (134 fields bound) but still has the
pre-existing frontend warnings about its snapshot assertion, const qualifier,
indentation, unused native/probe functions and stub context metadata. These
were not suppressed or represented as a warning-free DLL build.
ASan/UBSan texture-layout (100,000 cases), readback-layout (3,456 cases),
readback and lifecycle tests pass; the three regression switches detect their
intended injected faults. No native present-algorithm optimization was attempted.
