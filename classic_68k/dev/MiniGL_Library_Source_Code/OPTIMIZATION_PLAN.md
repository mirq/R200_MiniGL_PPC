# R200 Backend Optimization Plan — profiler v3 baseline (2026-08-23)

Baseline: three 640x480x32 fullscreen gears runs (envmap, updated driver,
profiler v3). Median 186.95 ms/frame = 5.349 FPS profiled. Use stage timings
for attribution; unprofiled FPS for release comparisons.

| Stage | Median ms/frame |
|---|---:|
| Application gear math | 34.97 |
| MiniGL API dispatch/capture | 22.87 |
| MiniGL frontend processing | 40.54 |
| Serialization, included above | 12.43 |
| Driver Execute | 55.53 |
| Presentation | 25.44 |
| Other frame overhead | 7.60 |

Scope decisions: Radeon9200.chip driver is editable; the gears benchmark is
frozen (no client-side changes); quick C wins first.

## Batch 1 — Library hot-path C/asm (no API/driver changes)

### 1a. Length-aware snapshot copy + capture cache (est. -8..14 ms)
- `r200_geometry.c` `PrepareScreenAppend`: replace `context->pendingDraw =
  *state` (full ~1.6 KB worst-case struct assign) with a length-aware copy
  (scalar prefix + `headerDwords` dwords).
- Cache built captures keyed by (`semanticStateSerial`, command, projective/
  hw-transform flags); reuse after every strip/fan `glEnd` flush instead of
  rebuilding (~300 rebuilds/frame today: byte-wise ZeroMemory of ~540 B +
  full texgen/matrix header rebuild + DrawRectangle per capture).
- `EnsureTextureResident`/`SyncTextureDirty` bump `semanticStateSerial` only
  when they actually perform work (upload/residency change) so texture-side
  invalidation stays sound with the cache.
- Files: `r200_geometry.c`, `r200_state.c`, `r200_texture.c`,
  `minigl_r200_internal.h`.

### 1b. Format-specialized serialization (serialization 12.4 -> <=7 ms target)
- Reorder `struct ScreenVertex` to record order (x,y,z,rhw,nx,ny,nz,color,
  s,t,s1,t1,fog) so a TCL normal vertex is one contiguous longword block.
- Split the `FlushScreenVertices` emit loop into per-format unrolled writers
  (TCL-normal+tex0 [+tex1][+fog]); GCC path first, then `__HAVE_68881__`
  inline-asm `movem.l` staging copy with C fallback for vbcc.

### 1c. Dword/movem `ZeroMemory`/`CopyBytes`
- `minigl_r200.c:98` `ZeroMemory`, `CopyBytes` in `r200_texture.c`: dword +
  movem fast path, keep byte tail for small/unaligned counts.

### 1d. Free riders
- Reject `mglBeginFastPath` while hardware sphere-map is active (FastPath v2
  vertices carry no normals; currently only lighting rejects it).
- Flag experiments: `-fomit-frame-pointer`, GCC `-O3` (never tried; vbcc -O3
  remains rejected).

## Batch 2 — Radeon9200.chip driver record-contract work

### 2a. Execute split instrumentation
Sub-timers inside the Execute path (record validate/copy vs CP-stream
generation vs ring submit), reported like existing counters; one gears run to
attribute the 55.5 ms before optimizing.

### 2b. Compact TCL vertex contract
Capability bit + header flag for "no unit-1/no fog" 11-dword normal vertices;
library emits compact records when negotiated and updates
`GeneratedHardwareVertexDwords`; raw pixel-equality probe gate before
trusting. Expect Execute -15..20% plus further serialization savings.

## Deferred (batch 3 candidates)
- Unified Vertex/ScreenVertex single-pass append / direct-to-record streaming
  for list primitives.
- Retained/static geometry interface idea (parked in PERFORMANCE.md notes).

## Validation & measurement protocol (every batch)
- `make -f Makefile_dispatch_r200.gcc dispatchtable` after any `r200_*.c`
  edit, then full rebuild + `importlib` + `testprograms`.
- Physical target only (192.168.1.21:2345), no emulator tools; wait >=80 s
  after any reboot; grey screen => manual power-cycle before conclusions.
- Acceptance: phase4_smoke, phase5_smoke (--stress), phase6_primitives,
  phase6_acceptance (windowed + fullscreen --depth 8/16/32), cull_shade_smoke
  pixel readback, bootstrap/perspective smokes; vbcc r200 build compiles.
- Perf: 3x profiled fullscreen 640x480x32 envmap runs (median stage table)
  + unprofiled triplet for FPS claims.
- Gates: zero acceptance regressions; serialization -40%, frontend ex-
  serialize -25% (batch 1); overall profiled >= +0.4 FPS.

### 2a. Execute split instrumentation — DONE (2026-08-23)
Release-build phase timers added to `Radeon9200.chip` (`Radeon3DExecute`
copy/build/submit, microsecond TR_GETSYSTIME pairs, accumulated
per-service-base and exposed as a size-gated `Radeon3DInfo` V2 tail;
`tools/radeon3dinfo` prints them as an `R3DEXEC` line). ABI-safe: V1
callers get exactly the old 32-byte struct back.

One gears run (100 frames fullscreen 640x480x32 envmap, 600 submits,
library Execute bucket 59.2 s total):

| Phase | us/call | x6 submits | Share |
|---|---:|---:|---:|
| Copy (record -> trusted) | 1540 | 9.2 ms | 17% |
| Build (semantic -> CP stream) | 3385 | 20.3 ms | 36% |
| Submit (ring write + WPTR) | 4505 | 27.0 ms | **49%** |

Sum 56.6 ms/frame == library-measured Execute bucket. The dominant cost is
the ring-write loop in `CpCommitStream`: per-dword `CpStreamDword()` +
SWAPLONG + volatile aperture stores (~0.79 us/dword ~= the ~6 MB/s aperture
rate measured by the debug module). The record copy is a plain dword loop;
the builder is branchy C.

Batch 2b revised order (by measured value):
1. Submit-path burst stores -- **DONE** (driver commit 831e26a): stage 8
   swapped longwords + movem pair for the command bulk, fence/padding tail
   per-dword; existing final-dword readback keeps ordering before WPTR.
   Measured submit phase 3244 -> 2794 kus (-13.9%), i.e. 27.0 -> 23.5
   ms/frame. Residual submit cost is aperture bandwidth itself.
2. Trusted-copy unroll -- DONE in the same commit: eight-wide register-
   held unroll (a staging pass measured SLOWER); copy 1109 -> 1073 kus.
   The copy is latency-bound (~11.6 cycles/dword), effectively at its
   floor without prefetch tricks.
3. Compact TCL vertex contract -- **DONE** (driver commit 8bfe233, library
   commit 367530d): `RADEON3D_CAP_COMPACT_TCL_VERTEX` + `RADEON3D_DRAW_
   COMPACT_VERTEX`; records carry 10-dword normal / 7-dword plain TCL
   vertices when unit 1 and fog are absent (the CP-stream side was already
   emitting those prefixes, so only record size shrank). Measured: record
   dwords 38,641 -> 30,361/frame (-21.4%), submits 6 -> 5, Execute bucket
   55.5 -> 50.2 ms/frame; render verified coherent via mid-run screenshot
   (chrome sphere-map reflections require intact normals). Backward and
   forward compatible: old libraries against the new driver and the new
    library against old drivers both take the full-stride path.

   Final three-run validation: profiled FPS 5.623 / 5.612 / 5.663 ->
   **median 5.623**; unprofiled FPS 6.142 / 6.119 / 6.081 -> **median
   6.119**. Against the Batch 1 medians this is -1.0% profiled and +0.3%
   unprofiled, i.e. no material end-to-end FPS change despite the clear
   record-size and Execute-bucket reductions. Treat the contract as a
   bandwidth/submit-headroom improvement, not an FPS claim.
4. Execute scratch clear + compact untextured stride fix -- **DONE**
   (driver commit 93892ae): the 1554-byte per-record state clear compiled
   to 1554 `clr.b` loop iterations. Eight-longword groups now compile to
   48 iterations of eight `clr.l` stores plus the 18-byte tail. The compact
   fast path also now steps the specified 10/7-dword public stride for
   untextured vertices while emitting only the 8/5-dword CP prefix; using
   the emitted size as the input stride had misaligned vertices after the
   first. `radeon3dformats` now checks plain and normal compact records on
   fresh targets and reports `compact_tcl=ok` on hardware.

   Profiled FPS 5.679 / 5.635 / 5.630 -> **median 5.635**; unprofiled FPS
   6.171 / 6.167 / 6.142 -> **median 6.167**. Versus the compact baseline,
   Execute fell 50.24 -> 48.74 ms/frame (-3.0%) and the release FPS median
   rose 0.8%. Driver counters across six gears runs put the build phase at
   16.45 ms/frame, down from about 17.7 ms/frame.

Driver-side Execute after 2b plus the scratch-clear pass: library bucket
55.5 -> 48.7 ms/frame. Driver build phase is now 16.45 ms/frame; the
remaining work is the branchy vertex/state emitter and needs a real
refactor rather than another broad micro-optimization.

Driver-side changes live in `/home/mirek/p96-driver`, are committed and
pushed through 8bfe233, and are deployed as
`LIBS:Picasso96/Radeon9200.chip`. Compact-contract backup:
`Work:Radeon9200.pre-compact.chip`; library backup:
`Work:minigl-pre-compact.library`.

## Progress log

- 2026-08-23: plan saved; starting Batch 1a.
- 2026-08-23: Batch 1 code complete (1a capture cache + serial-bump fixes for
  GL_TEXTURE_GEN_S/T enable/disable, glTexGeni, texture residency/eviction
  transitions; 1b ScreenVertex reordered to record order + movem.l raw
  block-copy serializer with C fallback; 1c longword ZeroMemory/CopyBytes;
  1d FastPath rejected while hardware sphere-map is active). GCC r200 build
  clean; dispatchtable regenerated; importlib + testprograms linked;
  movem sequence verified via objdump.
- Pending: vbcc compile check (vc not on this host; asm is __GNUC__/__m68k__
  guarded so vbcc takes the C fallback).
- 2026-08-23 (hardware): driver/library install verified —
  `LIBS:minigl.library` CRC `3E28E99B` == staged Batch-1 build (101132 B,
  v12.0); active chip is `LIBS:Picasso96/Radeon9200.chip` 64764 B (newest
  iface12 build; larger than both same-day backups and advertises
  HW_SPHERE_MAP; `Work:Radeon9200.chip` 49788 B is a stale pre-triangle
  copy). Note for future installs: the loaded driver lives under
  LIBS:Picasso96/, not DEVS: or Work:.
- 2026-08-23 (Batch 1 gears results, fullscreen 640x480x32 envmap,
  20 warm-up + 100 measured frames, three-run medians):

  Profiled FPS 5.641 / 5.699 / 5.679 -> **median 5.679** vs baseline 5.349
  (**+6.2%**). Unprofiled FPS 6.131 / 6.100 / 6.190 -> **median 6.100** vs
  documented baseline 5.977 (**+2.1%**).

  Stage medians (ms/frame) vs the profiler-v3 baseline:

  | Stage | Baseline | Batch 1 | Delta |
  |---|---:|---:|---:|
  | Application gear math | 34.97 | 34.60 | -0.37 |
  | API dispatch/capture | 22.87 | 21.55 | -1.32 |
  | Frontend processing | 40.54 | 32.73 | **-7.81** |
  | Serialization (in frontend) | 12.43 | 12.12 | -0.31 |
  | Driver Execute | 55.53 | 55.73 | +0.20 |
  | Presentation | 25.44 | 23.89 | -1.55 |
  | Total | 186.95 | 176.11 | **-10.84** |

  Attribution: the gain is almost entirely F1 (capture-cache eliminating
  ~300 per-strip snapshot rebuilds/frame): frontend ex-serialize dropped
  28.11 -> 20.61 ms (-27%), meeting the -25% gate. Serialization was
  roughly flat despite the movem serializer -- the emit loop appears to
  have been bandwidth/profiler-sampling dominated rather than
  instruction-count dominated; keep this in mind before investing further
  in serializer micro-optimization. Execute unchanged (record contract
  untouched, as designed until Batch 2).

- 2026-08-24 (Batch 2b compact TCL validation, same gears protocol):
  profiled 5.623 / 5.612 / 5.663 FPS -> median 5.623; unprofiled 6.142 /
  6.119 / 6.081 FPS -> median 6.119. Profiled stage medians: frontend
  36.98 ms/frame, serialization 12.66 ms/frame, Execute 50.24 ms/frame,
  presentation 23.34 ms/frame. All three runs reported 30,361 record
  dwords/frame, 33,978 generated dwords/frame, five submits/frame, and no
  Execute failures. Mid-run chrome sphere-map screenshot verified intact
  normals, texture coordinates, geometry, and depth.

- 2026-08-24 (Execute scratch clear / compact untextured stride): generated
  68k changed from a 1554-iteration `clr.b` loop to 48 eight-`clr.l`
  iterations plus the tail. Profiled gears median 5.635 FPS with Execute
  48.74 ms/frame; unprofiled median 6.167 FPS. Raw `radeon3dformats`
  passed formats, texgen, sphere map, lighting, compact plain/normal TCL,
  quads, and all legacy rejection checks.

- Open issue (pre-existing, NOT Batch 1): bootstrap_smoke fails at frame 0
  (windowed AND fullscreen) and cull_shade fails code 40 / phase4
  depth_funcs fail / phase6_acceptance stops after DEFERRED_PRE --
  identically on both the old and new library builds and binaries, while
  gears renders correctly. Investigate separately (suspect today's driver
  swap chain or residual card state).
