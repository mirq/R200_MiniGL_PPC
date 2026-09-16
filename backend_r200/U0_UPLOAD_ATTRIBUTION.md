# U0 — Direct texture-upload latency: attribution and first fixes

2026-09-16, physical 68060/MPC7410 + Prometheus/RV280, bridge
`192.168.1.21:2345`, one boot, PPC initialized once. Workload:
`r200_texture_upload_smoke 120 tiles16 window` from `DH2:u0` — 480
256x256 16-bit tile uploads (the Silent Hill FMV/logos shape). Profiled
DLL plus profiled host; raw mftb counters; client-reported timebase
24,999,59x Hz.

## Baseline attribution (run1, per 256x256 upload, measured)

| Phase | ms |
| --- | ---: |
| Client barrier before BEGIN (flush+submit+drain to `completed`) | 6.09 |
| BEGIN round trip (host handler 1.56 ms incl. DrainSemanticFence) | 4.42 |
| PPC 565->BGRA convert + aperture write | 5.83 |
| aperture dcbf flush | 0.04 |
| COMMIT round trip (host handler 0.18 ms) | 1.04 |
| Canonical shadow conversion (`FrontendStageTextureBGRA`) | 6.89 |
| **Total (client glTexImage2D call)** | **24.35** |

The 20 ms PPC yield inside the drain loop was the single largest
barrier cost: `drain_enters=120 drain_sleeps=119` over a 120-frame run;
each `MGLPPCYield()` (WarpOS `WaitTime(0,100)`) actually parks ~20 ms
(HOST_LOADER_REGRESSION.md).

## Changes

1. **565 fast paths** (`minigl_r200_warpos_compile_probe.c`): row-tight
   565->BGRA loops in the direct write and in `FrontendStageTextureBGRA`,
   byte-identical dwords to `FrontendSourceToBGRA32` with the
   format/type test hoisted out. (Small win: the pass is store-bound.)
2. **Drain yield budget is progress-aware** (`R200PPCPlatformBarrier`):
   the spin budget resets whenever `completed`/`retired` advances, so the
   PPC only parks after a full budget with no movement. 119 -> 12-14
   sleeps per 120 frames.
3. **Host serves the mailbox inside `RingPoll`**
   (`minigl_ppc_host.c`): a pending BEGIN/COMMIT no longer waits for the
   host to finish a multi-entry submit burst. The commit accumulator is
   flushed before the mailbox handler runs, preserving the rule that no
   native control handler runs while absorbed records still reference
   texture handles a BEGIN/COMMIT can replace. Recursion guard added.
4. **Canonical shadow fused into the direct pass**
   (`FrontendDirectTextureUpload` takes an optional shadow destination):
   the loop writes the same converted dword to the aperture and to the
   shadow, removing the second full conversion pass. Only when the
   upload target owns the shadow (unit-0 keying, same rule as the
   subimage patch path). On direct-upload failure the fresh shadow is
   freed so a later subimage cannot patch uninitialized data.

## Result (same workload, same boot)

| Run | Client upload/upload | total run | shadow | barrier | begin |
| --- | ---: | ---: | ---: | ---: | ---: |
| run1 baseline | 24.35 ms | 13.34 s | 6.89 ms | 6.09 ms | 4.42 ms |
| run2 565 fast paths | 23.50 ms | 12.92 s | 5.86 ms | 6.09 ms | 4.43 ms |
| run3 + drain progress + host mailbox-in-ring | 20.24 ms | 11.36 s | 5.98 ms | 2.64 ms | 4.22 ms |
| run4 + fused shadow | **13.58 ms** | 8.16 s | 0.00 ms | 2.53 ms | 3.45 ms |
| run5 + 565 expansion tables | **11.27 ms** | **5.41 s** | 0.00 ms | 2.60 ms | 3.49 ms |

run5 per-upload: barrier 2.60 + begin 3.49 + write 3.55 + flush 0.04 +
commit 1.13 (~0.5 unaccounted). Tables: write 5.70 -> 3.55 ms, total
client run 8.16 -> 5.41 s.

**Host-idle caveat (observed run5-contaminated):** if the host is already
in its idle `Delay(1)` sleep (stale/orphaned host, or >1.5 s without
work), the next BEGIN pays ~20 ms and the measured begin round trip
becomes ~22 ms for every upload until the session ends. A game whose
logic stalls exceed the host idle threshold can show the same one-off
latency at segment transitions. Verify no stale host before every run
and consider whether the idle threshold suits the application.

Remaining targets: the aperture write (~3.6 ms; an R5G6B5PC destination
with the `stswi` blit would halve the bytes, but the native side's
`want565` import is not exercised and the PPC burst-store path has a
documented GPU-hang history), the BEGIN round trip (host handler +
transport), and the client barrier.

## Validation

- `r200_texture_update_test` (pixel-verified TexImage + TexSubImage
  patch/recommit, i.e. the fused-shadow consumer):
  - run1 baseline DLL: `FAIL CHECKS=3495 FAILURES=1` —
    `FAIL INDEXED SOURCE ACTUAL=0x00000000 EXPECTED=0x00000058`.
  - run4 fused DLL: identical failure and identical check count.
  - run5 (tables) DLL: identical failure and identical check count.
  - The failure is **pre-existing** in the current tree (indexed
    `GL_COLOR_INDEX` uploads are accepted, while this test still expects
    `GL_INVALID_ENUM`); it aborts the test before the later checks.
  - The subimage steps that did run (`FULL SAME SHAPE PIXEL ORDER`,
    `FIRST LAZY SUBIMAGE`, `REPEAT PARTIAL`, alignment steps) passed on
    both builds, exercising the fused shadow.
- Builds: profiled and non-profiled DLL/host compile without new
  warnings; `tools/check_68k_host.py` passes for both hosts.

## Artifacts (this session)

| Item | CRC32 |
| --- | --- |
| profiled DLL run5 (fused + tables, best) | `7ADA06F4` (312576 B, converted on target) |
| profiled DLL run4 (fused) | `667B0557` (312484 B) |
| profiled host (mailbox-in-ring) | `BCE24A15` (49760 B) |
| profiled DLL run1 (baseline+counter only) | `729DAB28` |
| update-test client | `3604C447` (ELF) |

Evidence: `/tmp/opencode/u0/{dll-profile-run1..4.log, host-run4.log,
smoke-run4.log, updtest-run4.log}`; target copies under `DH2:u0/`
(`dll_run1..4`, `mglhost_u0d`, `host*.log`).

## Phase 2.2 / 1.1 / 1.2 results (2026-09-16, same boot)

### 2.2 Presenter attribution — re-scoped, not implemented

Gears 600 frames with the profiled host, `MINIGL_PRESENT` forced:

| Mode | fps | native present_ticks | host present stage | presentwait |
| --- | ---: | ---: | ---: | ---: |
| BLIT (default, sync off) | 128.2 (quiet 133.9) | 0.70 s (1.17 ms/frame) | 1.25 ms/frame | 0.25 ms/frame |
| FLIP | 32.3 | 14.97 s (24.9 ms/frame) | 25.0 ms/frame | 0.23 ms/frame |

The flip cost is `ChangeScreenBuffer` itself (P96: ~22-27 ms per call, as
the code comment already documents), not the `WaitTOF` polling loops:
the host's presentwait is only 0.23 ms/frame. Replacing the polls with a
bounded signal wait would save at most the poll granularity on the
sync-enabled path; it would not change the P96 switch cost. Silent Hill
runs sync=0 (BLIT), so its 11-17 ms "present" bucket is the game's own
software scaling, not MiniGL. Item 2.2 parked pending a driver-level
cheap screen-buffer switch; not worth a library change as scoped.

### 1.1 Host `-mcpu=68060` — measured neutral

A/B/A on one boot, quiet fused DLL, 600-frame gears:
68020 host 137.6 / 133.3 fps, 68060 host 135.1 fps. The host's hot path is
service-call and memory bound, so instruction scheduling does not show.
The 68060 build is kept (the target is 68060-only; the linked native
import archive is already CPU=68060) and recorded as neutral.

### 1.2 Mailbox shared upload — implemented and validated

`MGLPPC_CMD_TEXTURE_UPLOAD_SHARED` joined the mailbox whitelist, gated by
the new `MGLPPC_CAP_MAILBOX_UPLOAD_SHARED` (bit 13) so an older host keeps
the Exec fallback.

Validation with the quiet pair + profiled host, suite
`quake.alias-ogre` (264x194 non-power-of-two skin = shared upload path):

```
MINIGL_TEST version=9 id=quake.alias-ogre result=PASS depth=16 frames=60
fps_x1000=5319 gl_error=0 detail="skinned Ogre mesh: float and integer
array paths bounded"
```

Host routing: `direct_begin=0` (no direct uploads, as expected for the
non-POT skin), `mailbox=5` with `control=7` (lifecycle only), so the
shared TexImage travelled through the mailbox. `r200_texture_update_test`
is unchanged (`CHECKS=3495 FAILURES=1`, the same pre-existing INDEXED
SOURCE expectation).

Transport A/B on the same workload (profiled hosts, quiet DLLs):

| Pair | suite result | control | mailbox |
| --- | --- | ---: | ---: |
| old (Exec for shared upload) | PASS, gl_error=0 | 8 msgs / 0.33 s | 4 msgs / 4.87 s |
| new (mailbox for shared upload) | PASS, gl_error=0 | 7 msgs / 0.25 s | 5 msgs / 4.98 s |

The shared upload moved from the Exec bucket to the mailbox bucket as
intended; end-to-end fps is unchanged within noise (5309 vs 5395
fps_x1000). The saving is the Exec `PutMsg`/`WaitPort` round trip
(documented at ~1-4 ms per control), which is invisible here because the
ogre test uploads few textures and their cost is dominated by the native
upload itself. The change is kept as strictly fewer Exec transitions for
texture-churn clients; it is gated by the new capability bit and can be
reverted by removing three small edits.

Artifacts: quiet DLL `32216D6B`, quiet host `85F1A513`, profiled host
`984DB6A2`; old pair preserved as `Work:ibdraw/dll.q1` (`8FF809BB`) and
`DH2:u0/mglhost_u0d` (`BCE24A15`).

## Next

1. Fix or waive the pre-existing `INDEXED SOURCE` expectation so the
   full 7127-check update suite runs as a gate.
2. Game check with the fused build (install to `Work:Silent Hill WOS`
   with backup; watch the logo/FMV split).
3. Attack the write (~5.7 ms): test whether an R5G6B5PC direct surface
   is accepted by the current service; if not, measure a 64-bit-store
   variant against the documented `stswi` GPU hang.
4. Attack BEGIN/barrier: host handler is 1.56 ms of the 3.45 ms round
   trip; the client barrier is now 2.53 ms.

## Silent Hill in-game result (2026-09-16)

Stack: DLL `7ADA06F4` + host `1664A435` + `MGLPPC_HOSTPRI=-1` (archived).
No GL errors in the session; 920/920 direct uploads, shadow ticks 0.

`[Psy-X] [WOS-PROF]` frame breakdown:

| Segment | frame | logic | raster | present | endscene | fps |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| best gameplay frames | 55-64 ms | 28-29 | 4.0 | 11-17 | 10.6 | 15.5-18.2 |
| FMV / intro | 70-158 ms | 17-88 | 8-16 | 29-69 | 6-10 | 6-14 |
| transitions / loads | 225-3147 ms | 181-3083 | - | 13-34 | 2 | - |

Findings:

- The texture-upload path is no longer a gameplay bottleneck; the
  per-frame GL cost is now dominated by **present** (11-17 ms gameplay,
  29-69 ms FMV) plus ~10.5 ms `endscene` (per-frame tile upload).
- Remaining gameplay time is the game's own PSX logic/raster (32-36 ms
  combined), not MiniGL.
- Session per-upload averages (BEGIN 11.0 / COMMIT 7.0 / write 3.2 ms)
  are inflated by uploads queueing behind the host's present work; the
  isolated probe figures are the achievable floor.
- **Critical operational finding:** `MGLPPC_HOSTPRI` is volatile; a
  reboot clears it and the host runs at priority 0, roughly halving
  throughput (observed as "terribly slow" after a reboot). It is now
  archived to `ENVARC:` on the target. Any future benchmark or game
  session must confirm the priority before starting the host.

Next target: the native presenter (`classic_68k/.../library/r200_surface.c`)
— `WaitTOF`-polling flip/present waits are the remaining GL cost, as
already scoped in `GEARS_STUTTER.md` ("Remaining Work").

## Gears confirmation on a fresh boot (2026-09-16)

Quiet fused DLL `8FF809BB` + host `1664A435` + `MGLPPC_HOSTPRI=-1`,
`precalc -frames 600 -width 640 -height 480 -depth 16 -buffers 3
-nosync -nofps`:

```
GEARS_RUN frames=600 ticks=224 fps=133.928 variant=ppc-dll precalc=1
mode=fullscreen depth=16 buffers=3 sync=0 error=0
```

Matches the historical 133-136 fps baseline: the upload optimizations are
neutral on the geometry path. A first attempt on an aged/warm boot measured
92.879 fps with identical artifacts and priority; the fresh-boot rerun
confirms that as a boot-state artifact, consistent with the repo's
cold-boot benchmark protocol.
