# R200 Backend Performance Baseline

## Indexed-draw fusion (2026-08-26)

Target mode per AGENTS.md: **800x600 @ 32 bpp windowed**, workload
`quake.alias-ogre-x4-static` (four Ogre meshes, 1304 triangles, 39120 indexed
vertices/frame). Baseline was the pre-fusion library on the same boot.

Three changes to `library/r200_geometry.c` + `library/minigl_r200.c`:

1. `Lib_glDrawElements` triangle-list draws append resolved vertices straight
   into the TCL staging buffer (`AppendResolvedTclVertex`); the expanded
   `struct Vertex` array and its full extra pass are gone for the common case.
2. The per-draw max-index scan is deleted: the index cache is a fixed 64K-entry
   table allocated once per context (BYTE/SHORT indices are always in range;
   UINT-indexed draws bypass the cache).
3. `CopyHardwareVertex` packs with raw longword moves (the Vertex→ScreenVertex
   mapping is bitwise), never field-wise float copies; `ReadIndex` assembles
   indices from bytes instead of calling out to `CopyBytes`.

| Measurement | Before | After | Change |
|---|---:|---:|---:|
| x4-static unprofiled median FPS | 8.458 | 10.940 | **+29%** |
| Single-Ogre FPS | 26.43 | 32.34 | +22% |
| Gears 640x480x16 fullscreen profiled FPS | 5.695 | 6.62–6.66 | **+17%** |

Post-change profiled frame budget at target mode (109→93 ms/frame):

| Section | ms/frame |
|---|---:|
| glDrawElements total (`MGL_PERF_DRAW_TICKS`) | ~80 |
| — record serialization (inside draw) | ~28 |
| — index loop, cache resolve, vertex pack | ~52 |
| Execute / fence wait / present | ~15 |

Gears gains from the packing change alone (immediate mode shares
`CopyHardwareVertex` via `EmitHardwareBatch`). Correctness: all three
`quake.alias-ogre-x4-*`, phase4/phase6 smoke+acceptance, cull_shade pass;
phase5's DEFERRED subtest fails identically on the pre-change library
(pre-existing commit-reject, `rec=99 off0=131408`, service stage unknown —
separate issue).

Operational note: after installing a new `LIBS:minigl.library`, run `Avail
Flush` before launching clients — AmigaOS keeps the previously loaded resident
copy otherwise, and benchmarks silently measure the old binary.

## Interface-12 hardware sphere map (2026-08-23)

Radeon3D interface 12 moves `GL_SPHERE_MAP` generation to TCL when both S and
T generation are enabled. MiniGL submits object-space normals, the model-view
and inverse model-view matrices, and an identity texture matrix; partial texgen
state retains the software fallback. Three 640x480x32 fullscreen runs produced:

| Measurement | Software sphere map | Hardware sphere map | Change |
|---|---:|---:|---:|
| Profiled median FPS | 5.309 | 5.695 | +7.3% |
| Unprofiled median FPS | 5.724 | 5.977 | +4.4% |
| Frontend ms/frame | 65.50 | 39.42 | -39.8% |
| Execute ms/frame | 41.40 | 56.35 | +36.1% |
| Serialization ms/frame | 10.10 | 12.35 | +22.3% |
| Semantic submits/frame | 4 | 6 | +50.0% |
| Record dwords/frame | 28,711 | 38,641 | +34.6% |
| Generated dwords/frame | 23,198 | 33,978 | +46.5% |

`spheremap_calls` falls from 2760/frame to zero. The gain is much smaller than
the software texgen cost in isolation because normals enlarge vertices from 10
to 13 dwords, the texgen/normal header grows to 110 dwords, and the 8192-dword
service limit raises submission count. The next optimization target is a more
compact generated-texcoord vertex contract or retained geometry, not further
MiniGL sphere-map arithmetic work.

The real gears scene renders coherently, but the interface-12 raw driver probe
is not yet pixel-exact: its center sample passes while four corner samples read
the background texel (`r3dformats` result 102). Keep this result provisional
until that probe expectation or the driver mapping is resolved.

## Library 12 software sphere-map baseline (2026-08-23)

Physical 68060/RV280, corrected Radeon3D interface-11 driver, fullscreen
640x480x32, chrome sphere map, 20 warm-up and 100 measured frames. Three
profiled runs measured 5.309, 5.261, and 5.347 FPS; the representative median
run has this frame budget:

| Section | ms/frame | Frame share |
|---|---:|---:|
| MiniGL frontend | 65.50 | 34.8% |
| Radeon3D Execute | 41.40 | 22.0% |
| Fence wait | 0.23 | 0.1% |
| Fullscreen present | 24.32 | 12.9% |
| Application/API and uninstrumented remainder | 56.92 | 30.2% |
| **Whole frame** | **188.36** | **100.0%** |

Serialization is 10.10 ms/frame and is already included in the frontend row.
Each frame processes 300 immediate-mode primitives and 2760 vertices, emits 25
draw records in four semantic submits, copies 28,711 semantic record dwords,
and makes the driver generate 23,198 CP-stream dwords. Fence blocking is
negligible, so this workload is CPU/submission/presentation limited rather than
waiting for the GPU.

The benchmark now accepts `-noenvmap` to disable texture and sphere-map texgen
without changing its geometry. Controlled profile medians are:

| 640x480x32 configuration | FPS | ms/frame | Frontend | Execute | Present |
|---|---:|---:|---:|---:|---:|
| Fullscreen, sphere map | 5.309 | 188.36 | 65.50 | 41.40 | 24.32 |
| Fullscreen, no envmap | 6.700 | 149.26 | 31.79 | 34.14 | 25.03 |
| Windowed, sphere map | 5.984 | 167.11 | 65.70 | 41.34 | 2.82 |

Sphere mapping therefore costs about 39 ms/frame: it remains a CPU per-vertex
operation because Radeon3D hardware texgen currently exposes object-linear
generation, not reflection/sphere-map generation. It also expands the generated
stream from 17,328 to 23,198 dwords/frame. Fullscreen buffer ownership and flip
notification cost another approximately 21 ms/frame compared with the windowed
control.

Profiling itself is material on a 68060. Three otherwise identical unprofiled
runs measured 5.744, 5.718, and 5.724 FPS (5.724 median, 174.70 ms/frame).
Unprofiled `-noenvmap` measured 7.408 FPS median (134.99 ms/frame). Use
unprofiled FPS for release comparisons and profiled runs only for attribution.

Hardware TCL offloads transform and clipping arithmetic, but the CPU still
captures thousands of immediate calls, computes sphere-map coordinates,
normalizes and serializes semantic records, and runs the driver's validation
and CP-stream builder inside `Radeon3DExecute()`. The highest-value next steps
are hardware sphere-map texgen and retained/static geometry submission; after
those, split the 41 ms Execute bucket into service copy/validation, stream
generation, and CP-submit timings.

## Mandatory hardware transform, packed-colour TCL vertices (2026-08-22)

> 2026-08-23: the backend source split into `r200_*.c` modules was validated
> against this baseline. Compiled per-module it cost ~6% fullscreen 16bpp
> (5.39 vs 5.77 FPS; frontend +15%, serialization ticks doubled) from lost
> cross-module inlining; compiled through `library/r200_unity.c` it measures
> 5.797/5.788 FPS — parity with the single-file build below.
>
> 2026-08-23b: with the interface-11 driver and the ABI-v4 lighting build,
> unlit gears measures 5.42–5.45 FPS against 5.69 for the pre-lighting
> library on the same driver build (frontend +370k ticks/50 frames,
> serialization +73k; execute ticks identical). The delta is library-side,
> not the driver: bigger snapshot structs, extra per-append branches, and
> code-layout shifts in a unity object sized ~2 kLOC larger. Not yet
> recovered; treat 5.7+ as the target before shipping lighting work.

Interface 9 `RADEON3D_CAP_HW_TRANSFORM_CLIP`: transform and homogeneous
clipping execute on the TCL unit; MiniGL submits object-space geometry. The
TCL record vertex is 10 dwords — x, y, z, w, one packed ARGB colour dword
(the same packing as the non-TCL paths), s0/t0, s1/t1, fog — replacing the
13-dword float-RGBA layout. Matrix/guard-clip uploads are cached separately
from the register block in the service, and only list primitives
(triangles/quads/points/lines) merge across `glBegin`/`glEnd` blocks.

Physical 68060/RV280, warm machine, same 50-frame/10-warm-up protocol, and
same `Work:gears_bench_r200_gcc` invocation as below:

| Config | FPS | vs quad-list 4.967 | vs old-ABI 4.930 |
|---|---:|---:|---:|
| Fullscreen 640x480 16bpp | 5.734 | +15.4% | +16.3% |
| Windowed 640x480 32bpp | 6.376 | — | +19.9% |

Mean per-frame attribution, fullscreen 16bpp (vs quad-list baseline):

| Section | Quad-list | HW TCL |
|---|---:|---:|
| MiniGL frontend | 82.7 ms | 57.8 ms |
| Radeon3D execute | 37.0 ms | 37.8 ms |
| Fence wait | 3.4 ms | 1.3 ms |
| Present | 20.2 ms | 21.9 ms |
| Serialization | 6.7 ms | 9.0 ms |

Per frame: 2760 submitted vertices, 25 draw records, 23198 record dwords,
28713 generated CP dwords, all 280 short strips using the quad path, zero
execute/wait failures. The frontend saves ~25 ms/frame by moving transform,
clipping, and viewport mapping to the TCL unit; the slightly higher generated
dword count reflects the TCL's 5-dword untextured vertex versus the 3-dword
pre-transform one, which the fixed-state caching absorbs.

## Reproduction

Build and deploy:

```text
make -f Makefile_dispatch_r200.gcc
make -f Makefile_dispatch_r200.gcc importlib
make -f Makefile_dispatch_r200.gcc testprograms
```

Copy `bin/dispatch-r200-gcc/minigl.library` over `LIBS:minigl.library` (back up the
old one first — `Radeon9200.chip` v3.0 must be present, otherwise `OpenLibrary`
fails at load), then run the gears benchmark:

```text
Work:minigl_demos/gears_bench_v10 -benchmark -profile -width 640 -height 480 -depth 16 -frames 50 -warmup 10 -envmap Work:minigl_demos/data/chrome.ppm
```

Each run discards 10 warm-up frames and measures 50 frames with `ReadEClock()` at
709379 Hz. The summary line is `MINIGL_GEARS_SUMMARY version=1`; `fps_x1000` is
the whole measured loop including event processing and presentation. Counter
times are cumulative EClock ticks, opt-in via `MGL_PERF_COUNTERS_HINT` — normal
applications do not execute EClock reads.

## Measured on the V10 dispatch ABI (2026-08-19)

Physical target: 68060, PAL 50 Hz, Radeon RV280/R200-class path, `Radeon9200.chip`
v3.0, warm machine. Same per-frame workload as the accepted old-ABI configuration
(3320 submitted vertices, 21834 generated CP dwords). Two consecutive runs for
fullscreen 16bpp and windowed 32bpp, one for fullscreen 32bpp.

| Config | Run 1 | Run 2 | vs old-ABI 4.930 |
|---|---:|---:|---:|
| Fullscreen 640x480 16bpp | 4.946 | 4.932 | ~0% |
| Windowed 640x480 32bpp | 5.317 | 5.316 | +7.8% |
| Fullscreen 640x480 32bpp | 4.822 | — | −2.2% |

Mean per-frame attribution (ms):

| Section | FS 16bpp | FS 32bpp | Win 32bpp |
|---|---:|---:|---:|
| MiniGL frontend | 79.0 | 78.4 | 77.1 |
| Radeon3D execute | 43.5 | 44.5 | 44.3 |
| Fence wait | 3.2 | 6.0 | 6.1 |
| Present | 19.2 | 21.3 | 1.9 |

The remaining frame time is application/API work outside the measured sections.

Takeaways:

- The V10 dispatch ABI is performance-neutral versus the retired 186-vector
  stack ABI: 4.932/4.946 vs the old 4.930 mean on the same hardware.
- Windowed is ~10% faster than fullscreen at fixed depth because present drops
  from ~20 ms (vblank-bound double-buffer wait on `dbi_DispMessage`) to ~2 ms
  (single-surface blit).
- Depth 16→32 at fullscreen costs ~2%, almost entirely in the present stage.

## Native quad-list coalescing (2026-08-21)

Radeon3D interface 8 adds bounded native quad-list records. MiniGL recognizes
smooth, fill-mode, uncullable, fully unclipped four-vertex triangle strips,
reorders each strip as `0,1,3,2`, and coalesces up to 63 quads per record.

Physical 68060/RV280 fullscreen 640x480x16 results after a cold reboot, using
10 warm-up and 50 measured frames:

| Metric | Instrumented pre-quad | Quad-list mean |
|---|---:|---:|
| FPS | 4.692 | 4.967 |
| Submitted vertices/frame | 3320 | 2760 |
| Draw records/frame | 27 | 25 |
| Record dwords/frame | 20336 | 16946 |
| Generated CP dwords/frame | 21834 | 18338 |
| MiniGL frontend | 85.7 ms | 82.7 ms |
| Radeon3D execute | 44.5 ms | 37.0 ms |
| Fence wait | 3.4 ms | 3.4 ms |
| Present | 20.8 ms | 20.2 ms |
| Serialization | 7.9 ms | 6.7 ms |

The two quad-list runs were 4.971 and 4.963 FPS. All 280 eligible short strips
per frame used the quad path; capability, state, and clipping fallbacks were
zero. The raw interface-8 two-quad test, MiniGL 65-quad boundary/order test,
windowed Phase 4/5/6/cull-shade/acceptance tests, and fullscreen Phase 4/6 and
acceptance tests passed.

## BlitzQuake Phase 1 results

Physical 68060/RV280, fullscreen 640x480x32, three cold-boot runs of `demo1`
on 2026-08-22. The benchmark used three buffers, disabled video sync, joystick,
CD audio, PSX support, and sound, and rendered 969 timed frames per run.

| Run | Seconds | FPS |
|---|---:|---:|
| 1 | 377.000 | 2.570 |
| 2 | 367.870 | 2.634 |
| 3 | 370.873 | 2.613 |
| **Median** | **370.873** | **2.613** |

All runs completed with zero Radeon3D execute failures, wait failures, and RHW
clamps. The complete instrumented log is archived on the target as
`Work:Games/BlitzQuake_68k/id1/phase1_results.log`.

## BlitzQuake Phase 2 texture-upload results

Physical 68060/RV280, fullscreen 640x480x32, three cold-boot runs using the
same `demo1`, three-buffer configuration, and GCC 6.5.0b build as Phase 1.
Each run waited 420 seconds before rebooting; the result log was read only
after the subsequent 80-second reboot.

| Run | Seconds | FPS |
|---|---:|---:|
| 1 | 369.160 | 2.625 |
| 2 | 365.494 | 2.651 |
| 3 | 366.563 | 2.643 |
| **Median** | **366.563** | **2.643** |

Median versus Phase 1: elapsed time improved by **1.16%** and FPS improved by
**1.15%** (2.613 to 2.643). The geometry workload was unchanged to within demo
startup noise: draw records remained 44,598, while median record/generated
dwords changed by less than 0.004%.

Metric-wise median performance counters:

| Counter | Phase 1 | Phase 2 | Change |
|---|---:|---:|---:|
| Submits | 4,190 | 4,169 | **-0.50%** |
| Frontend ticks | 78,536,507 | 77,746,703 | **-1.01%** |
| Serialize ticks | 35,237,577 | 33,938,863 | **-3.69%** |
| Execute ticks | 36,411,849 | 36,647,910 | +0.65% |
| Wait ticks | 11,698,299 | 11,781,581 | +0.71% |
| Present ticks | 15,818,777 | 15,995,686 | +1.12% |
| Texture residency ticks | 8,264,810 | 5,552,887 | **-32.81%** |

The large residency reduction and 21 fewer submits confirm that dirty-region
copies, deferred synchronization, and LRU eviction removed CPU-side texture
work without changing the rendered workload. Execute, wait, and present moved
by less than 1.2%, so the end-to-end gain remains modest rather than scaling
with the 32.8% residency improvement. All three runs reported zero execute
failures, wait failures, and RHW clamps.

Focused Phase 5 hardware coverage additionally verified that two resident
subimage updates coalesced into one 128-byte surface copy, performed no submit
before first use, and preserved old-draw/update/new-draw ordering. The full log
is archived as
`Work:Games/BlitzQuake_68k/id1/phase2_texture_upload_results.log`.

## BlitzQuake Phase 2 asynchronous-submission results

Step 2 uses two immutable semantic record slots. `Radeon3DExecute()` returns
without a fence wait, allowing the CPU to serialize into the other slot while
the first batch runs. The previous fence is retired before issuing another
semantic Execute; this preserves the service's single command-stream staging
invariant while still overlapping frontend and GPU work. Texture modification
waits use per-texture fences, while present/readback/shutdown use drain waits.

Physical 68060/RV280, fullscreen 640x480x32, three cold-boot `demo1` runs with
the same configuration as Step 1:

| Run | Seconds | FPS |
|---|---:|---:|
| 1 | 349.967 | 2.769 |
| 2 | 351.833 | 2.754 |
| 3 | 351.087 | 2.760 |
| **Median** | **351.087** | **2.760** |

Every asynchronous run exceeded the fastest Step 1 run (2.651 FPS). Median
elapsed time improved by **4.22%** and FPS by **4.43%** versus Step 1.

Metric-wise medians:

| Counter | Step 1 sync | Step 2 async | Change |
|---|---:|---:|---:|
| Submits | 4,169 | 4,169 | 0.00% |
| Frontend ticks | 77,746,703 | 77,264,066 | **-0.62%** |
| Serialize ticks | 33,938,863 | 34,026,209 | +0.26% |
| Execute ticks | 36,647,910 | 36,389,020 | **-0.71%** |
| Wait ticks | 11,781,581 | 1,230,614 | **-89.56%** |
| Present ticks | 15,995,686 | 16,228,715 | +1.46% |
| Texture residency ticks | 5,552,887 | 5,670,482 | +2.12% |

The median run submitted 4,169 asynchronous batches. Of their fences, 3,470
were already complete when polled and only 699 required `WaitFence`: 72 record
buffer reuse waits, 3 texture dependency waits, and 626 presentation/readback
drains (the reason medians do not sum exactly is that each column is computed
independently). Thus blocking work moved overwhelmingly to explicit drain
boundaries instead of following every batch. All runs reported zero execute
failures, wait failures, and RHW clamps; record and generated dwords changed by
only 0.001%.

The fullscreen 640x480x16 gears benchmark produced 4.504 FPS median
(4.504/4.480/4.523), above the approximately 4.42 FPS synchronous result on
the same post-interpolation-quality code. Full Phase 4/5/6, residency stress,
multitexture/fog, cull/shade, quad-list, Phase E, and perspective tests passed
in windowed and applicable fullscreen/depth modes. The raw Quake log is
archived as `Work:Games/BlitzQuake_68k/id1/phase2_async_results.log`.

## BlitzQuake Phase 2 deferred-state results

Step 3 removes eager geometry flushes from state setters. Geometry now captures
an immutable normalized semantic-state snapshot; a real state change splits the
pending draw only when more geometry is appended. A wrap-safe semantic serial
avoids rebuilding equal snapshots, while redundant validated state calls become
no-ops. Texture mutation/deletion, clear, presentation, readback, resize,
destruction, and shutdown remain explicit ordering boundaries. BlitzQuake also
restores its texture-binding cache and tracks the active texture unit correctly.

Physical 68060/RV280, fullscreen 640x480x32, three cold-boot `demo1` runs with
the same three-buffer configuration as Steps 1 and 2:

| Run | Seconds | FPS |
|---|---:|---:|
| 1 | 321.356 | 3.015 |
| 2 | 325.632 | 2.976 |
| 3 | 322.773 | 3.002 |
| **Median** | **322.773** | **3.002** |

Median versus Step 2 asynchronous submission: elapsed time improved by
**8.06%** and FPS by **8.77%**. Versus the Phase 1 median, elapsed time improved
by **12.97%** and FPS by **14.89%**.

Metric-wise medians:

| Counter | Step 2 async | Step 3 deferred state | Change |
|---|---:|---:|---:|
| Draw records | 44,598 | 43,087 | **-3.39%** |
| Submits | 4,169 | 4,302 | +3.19% |
| Record dwords | 29,136,097 | 28,960,400 | **-0.60%** |
| Frontend ticks | 77,264,066 | 96,387,021 | +24.75% |
| Serialize ticks | 34,026,209 | 15,562,155 | **-54.27%** |
| Execute ticks | 36,389,020 | 35,989,906 | **-1.10%** |
| Wait ticks | 1,230,614 | 1,522,369 | +23.71% |
| Present ticks | 16,228,715 | 16,308,319 | +0.49% |
| Texture residency ticks | 5,670,482 | 4,335,329 | **-23.55%** |

The Step 3 median recorded 220,585 state calls, including 5,658 redundant
no-ops, 35,317 deferred splits, 43,216 full snapshot builds, and 1,108,709
serial fast-path hits. A separate cold Step 2 replay with the extended logger
recorded 303,332 flush calls for the same 970-frame interval; Step 3 recorded
43,087, an **85.80% reduction**. This replay is used only for the newly added
flush counter, not as the Step 2 timing baseline.

All three runs reported zero execute failures, wait failures, and RHW clamps.
Windowed and applicable fullscreen 8/16/32-bit Phase 4/5/6, residency stress,
multitexture/fog, perspective, cull/shade, quad-list, and Phase E coverage passed.
The post-deploy fullscreen 640x480x16 gears sanity run measured 4.928 FPS over
60 frames. The raw Quake results are archived as
`Work:Games/BlitzQuake_68k/id1/phase2_step3_results.log`; the Step 2 flush replay
is `Work:Games/BlitzQuake_68k/id1/phase2_step2_flush_remeasure.log`.

## MiniGLTestSuite room-walk optimization (2026-08-27)

The target was `quake.world-room-walk` at 800x600x32 on the physical 68060/RV280.
Each reported checkpoint is the median of three 120-frame runs after 10 warm-up
frames. Pixel verification remained enabled and was not included in the timing.

| Checkpoint | Windowed FPS | Fullscreen FPS | Result |
|---|---:|---:|---|
| Initial baseline | 7.348 | 6.500 | Reference |
| Persistent resolved-vertex cache | 10.444 | 8.981 | Accepted |
| Retained compact HW-TCL blocks | 13.056 | 10.790 | Superseded: texture batches were merged incorrectly |
| Correct indexed state boundaries and bind-only capture | 12.249 | 10.242 | Accepted |
| Phase 4 synchronized library | 12.288 | 10.158 | Accepted |
| Partial execute-state clear in `Radeon9200.chip` | 12.319 | 10.273 | Rejected: 0.8-1.1% gain |
| Direct retained index key | 13.995 | 11.354 | Accepted |
| 4096-dword async overlap split | 15.736 | 12.505 | Accepted for room-walk only |

The accepted result improves windowed performance by **114.2%** and fullscreen
performance by **92.4%** over the initial baseline. Windowed exceeds the 2x
target of 14.696 FPS; fullscreen remains 3.8% below its 13.000 FPS target. Warm
vertex fetches fell from 2234 per frame to zero. The corrected workload emits 41
records and 30 state splits per frame; earlier 20-record measurements were
visually invalid because texture-state boundaries were lost.

Retained-block profiling showed the compact cache was already fully effective:
all 4920 measured candidates hit, with zero misses, zero serialized dwords and
zero segment rotations. The remaining cost was the seven-dword payload hash on
every hit. Replacing it for cached BYTE/SHORT `glDrawElements` batches with an
index-sequence hash seeded by `indexCacheGeneration` reduced retained lookup
ticks from 1398723 to 156355 (**88.8%**) and total serialize ticks from 1613610
to 380877 (**76.4%**) over 120 frames. Software sphere-map fallback retains the
full payload hash because its generated S/T values also depend on modelview
state.

After retention made the whole frame fit one compact header-only commit batch,
the GPU had no CPU overlap: profiling showed 120 submits for 120 frames followed
by a drain at every presentation. A 4096 generated-dword commit threshold keeps
the validated 8192-dword service limit but splits the frame into two submits.
Measured submits became 240, wait ticks fell from 1396923 to 468360 (**66.5%**),
and execute ticks rose only from 816472 to 888735 over 120 frames.
Fullscreen profiling after the split left only 232169 wait ticks (2.7 ms/frame)
but 2198533 presentation ticks (25.8 ms/frame); the PAL display-flip path now
dominates the remaining 3.8% gap, so a third submit cannot reach 13 FPS by itself.

The same 4096-dword split regressed the full Quake `demo1` timedemo from the
previous 2.807 FPS median to 2.506 FPS: submits increased from 7.93 to 13.81 per
frame and Execute failures increased from one to nine. Full service batching is
therefore the default again. `SetEnv R200_GENLIMIT 4096` re-enables the split for
controlled room-walk measurements; values below 1024 or at/above the service
batch limit are ignored. `SetEnv R200_NORETAINED 1` and
`SetEnv R200_NO_BINDCAPTURE 1` disable those respective fast paths for hardware
bisects. `SetEnv R200_NOSTREAM 1` forces inline Execute records instead of
streamed segment commits, isolating segment visibility and offset/fence faults.
The variables are sampled once when a context is created.

The persistent resolved-array cache assumes unchanged client-array contents
while pointer, format, enable state and relevant current attributes remain
unchanged. In-place unlocked writes through an unchanged pointer are not
detectable by this API and require an explicit pointer/state change before the
next indexed draw. This restriction is accepted for the current static Quake
workloads and must be revisited before claiming general mutable-array support.

Quake worst-frame diagnostics subsequently identified two repeatable service
recoveries per `demo1`. Both followed a generated-budget submit that flipped to
the other nearly-full segment half: the draw fit the old half but not the new
one, so `FlushScreenVertices()` degraded it to inline Execute. The resulting
inline submissions failed and dropped pending frame geometry. The post-submit
fit check now rotates to a fresh segment first, matching the normal pre-submit
path; inline remains only the allocation-failure fallback.

The final library is `Work:MiniGLTestSuite/minigl.phase6-overlap4096.library`,
117020 bytes, installed as `LIBS:minigl.library` with CRC32 `AB036F8D`. The retained
driver is the pre-Phase-5 `Radeon9200.chip`, 71824 bytes, CRC32 `C132AD84`.
`bootstrap_smoke`, `phase4_smoke`, `phase5_smoke`, `phase6_primitives`,
`phase6_acceptance --depth 32`, and `cull_shade_smoke` all passed with these
artifacts.

The first fullscreen room context after the cold-boot windowed series still
rendered only the base-texture pass (`want 63,47,28`, `got 90,77,61`). `Avail
Flush` recovered it; the following three fullscreen runs all passed and form the
11.354 median above. The failed run is excluded. This first-resident/mode-switch
ordering defect remains a separate correctness issue.

## Historical optimization log (retired MiniGL_R200 tree, for reference)

The gears benchmark was used to tune both the MiniGL frontend and the
`Radeon9200.chip` execute path. All checkpoints used the same physical-machine
command and a warm library after 10 discarded frames.

| Checkpoint | Three-run mean FPS | Result |
|---|---:|---|
| Phase 0 baseline | 3.154 | Reference |
| Small strips → mergeable triangle lists | 3.308 | Accepted |
| Deferred triangle-list record construction | 3.329 | Accepted |
| Runtime batch limit (4096 on that driver) | 3.317 | Accepted, neutral |
| One-step reciprocal square root for sphere map | 3.345 | Accepted |
| Direct clip-outcode comparisons | 3.494 | Accepted |
| Out-pointer transforms, pointer rotation, deferred flush fix | 4.369 | Accepted |
| Cached projection-modelview matrix | 4.574 | Accepted |
| Asynchronous submits | 4.604 | Rejected: Phase 6 primitives hung |
| Direct `SysBase->ThisTask` context ownership check | 4.684 | Accepted |
| Fused draw-vertex validation and command emission (driver) | 4.764 | Accepted |
| Exact execute-state reuse within one generated batch (driver) | 4.861 | Accepted |
| 8192-dword service and MiniGL batches | 4.930 | Accepted |
| 16384-dword batches | 4.932 | Rejected: noise; execute ticks worsened |

Final accepted synchronous configuration: 4.930 FPS mean (202.84 ms/frame), 56.3%
faster than baseline. Rejected experiments: asynchronous submits (completed gears
but hung multitexture/fog acceptance — rolled back completely), deferred
fullscreen display notification (4.610, `ChangeBuffer()` already syncs),
vbcc `-O3` (+48% library size, hung Phase 6 primitives — `-O2` remains required),
and explicit viewport/texgen invariant passing (4.836, vbcc `-O2` scheduling was
better).
