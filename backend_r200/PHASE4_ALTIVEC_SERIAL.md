# Phase 4 — PPC vertex serialization: call sites and measured baselines

Physical A4000/68060 + Prometheus + RV280 + MPC7410/WarpOS, bridge
`192.168.1.21:2345`. One cold-boot-equivalent boot (operator reboot,
`C:InitPPC` once). Host: `Work:p4/mglhost.p3a` = `32716DD9` (the
phase-3 single-fence host; the "~175 fps" previous combo). DLL:
`9E44E240` (quiet build of current HEAD) unless noted. All runs
`error=0`.

## Exact serialize call sites

`backend_r200/library/r200_geometry.c`, inside `FlushScreenVertices()`,
under `#if R200_PPC_FRONTEND && defined(__PPC__) && !defined(__m68k__)`,
gated on `hardwareTransform && useCommit` (streaming segment commits):

| Line | Call | Record shape |
| ---: | --- | --- |
| 571 | `MGLPPCSwapLit13(destination, serialSource, serialCount, unit0Mask, unit1Mask, fogMask)` | lit full, 13 dwords |
| 577 | `MGLPPCSwapLit10(destination, serialSource, serialCount, unit0Mask)` | lit compact, 10 dwords |
| 583 | `MGLPPCSwapTCL10(destination, serialSource, serialCount, unit0Mask, unit1Mask, fogMask)` | plain TCL full, 10 dwords |
| 589 | `MGLPPCSwapTCL7(destination, serialSource, serialCount, unit0Mask)` | plain TCL compact, 7 dwords |

Kernels: `backend_r200/client/minigl_ppc_serial.s` (scalar `stwbrx`
byte-reversing block copies; source stride always 13 dwords / 52 bytes,
destination stride = record `vertexDwords`). Destination is the
streaming vertex segment:
`context->segment.info.CpuAddress + buildSlot * SEGMENT_HALF_BYTES +
slot->segmentCursor` (VRAM aperture through the Prometheus bridge,
32-byte-aligned segment base, 4-byte-aligned per-draw offsets).

Hot shapes on these workloads: **TCL10** for gears and quads room-walk
(textured, unlit), **Lit13/10** only when lighting is enabled.

## Measured baselines (gears precalc 600f, fullscreen 640x480x16, 3 buffers, sync off)

| Config | fps | serialize ticks | serialize ms/frame | execute ms/frame | present ms/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| semantic (CP emit off), profiled client | 135.135 | 27,396,264 | 1.847 | 3.42 | 1.02 |
| CP emit (`MGLPPC_CPEMIT=1`), profiled client | 175.438 | 27,274,044 | 1.848 | 2.15 | 0.59 |
| CP emit, quiet client (operator reference) | 178.571 | — | — | — | — |

Raw timebase 24.74 MHz (the DLL's legacy clock estimate of ~34.6 MHz is
not used). Per run: 1,656,000 vertices, 15,000 draw records, 2,031
submits, `kernel_flushes=15000` (25/frame), `kernel_vertices=1656000` —
the PPC kernels run on every vertex on the 175 fps path, 110 vertices per
average flush.

## Room-walk baseline (semantic, 120f, fullscreen 640x480x16)

`suite quake.world-room-walk`, `--profile` (pixel verification fails
pre-existing: `room f128 want 59,44,25 got 34,29,15`):

| Metric | Value |
| --- | ---: |
| fps | 57.692 |
| serialize ticks | 6,480,693 (2.18 ms/frame @24.74 MHz) |
| execute ticks | 32,680,087 (11.0 ms/frame) |
| present ticks | 160,653 |
| submitted vertices | 587,160 (4,893/frame) |
| draw records | 2,400 (20/frame) |
| submits | 680 |
| splits | 480 |

## Workload notes / next steps

- The store cost model is ~27 cycles per 4-byte aperture store
  (1.85 ms / 27,600 stores per frame); instruction issue is not the
  limit. Width is the lever: 16-byte `stvx` blocks should cut the
  transaction count ~4x.
- Plan: stage the existing scalar kernel output in a 16-byte-aligned
  cacheable scratch (8 KiB, chunked), then copy scratch to the aperture
  with `stvx`; the copy handles the 4-byte destination alignment with a
  scalar head/tail and a shifted-block middle (`vperm` carry). Output
  bytes are identical by construction.
- Scalar kernels stay linked and are selected at runtime; AltiVec is
  never mandatory (603e/604e safety), with a `MGLPPPC_NOALTIVEC=1`
  override for A/B.

## Implementation (2026-09-16)

- `backend_r200/client/minigl_ppc_serial_altivec.s`:
  `MGLPPCVecCopyUnaligned(dst, src, bytes)` — scalar head to the 16-byte
  boundary, `lvsl`-controlled `vperm` shifted blocks for the middle,
  scalar tail. Only volatile registers (`v0`-`v4`), VRSAVE saved/restored.
- `backend_r200/client/minigl_ppc_serial_altivec.c`: the four vector
  wrappers chunk the flush into the 8 KiB scratch, run the unchanged
  scalar kernels into it, then call the copy. The CPU probe uses WarpOS
  `GetInfo(GETINFO_CPU)` (documented `CPUB_*` values plus the G4-class
  flag word measured on the 7410: 0x00C00000), `MGLPPC_NOALTIVEC=1`
  forces scalar and `MGLPPC_ALTIVEC=1` forces vector. Verify mode
  (`MGLPPC_ALTIVEC_VERIFY=1`) runs a cacheable self-test plus a
  cacheable replay and an aperture read-back comparison, logging to
  `DH2:mglppc_av.txt`.
- `r200_geometry.c` selects the vector wrapper only when the probe
  enabled it; the scalar path is byte-for-byte unchanged.

### Bug found and fixed by the on-hardware verification

The first kernels used a static `.rodata` table for the byte-shift
`vperm` control. The WarpOS loader does not guarantee the table's
16-byte alignment, and `lvx` ignores the low address bits, so the
control (and every shifted record) was silently offset — room-walk
rendered with missing/black textures while the aligned-path flushes
still passed. Replaced with `lvsl` on the source address (no table, no
alignment assumption). The kernel self-test and the byte-exact aperture
checks now pass for every destination alignment.

### Measured results (same boot, A/B on one DLL, diagnostics off)

| Workload | Vector serialize | Scalar serialize | Delta | fps |
| --- | ---: | ---: | ---: | --- |
| gears semantic 600f | 23.69M ticks | 27.41M ticks | **−13.6%** | 133.3 vs 132.7 |
| gears CP emit 600f | 23.16/23.21M ticks | 27.28M ticks | **−15.0%** | 177.5 vs 177.5 |
| room-walk suite 120f | 5.517M ticks | 6.472M ticks | **−14.8%** | present-bound |

Correctness: kernel self-test pass, cacheable replay byte-exact, aperture
read-back byte-exact, `gl_error=0`, and the pre-existing room-walk pixel
signature (`want 59,44,25 got 34,29,15`) is unchanged from the baseline
DLL. The deterministic room-walk `--dump` surface is **CRC `FFED5264`
for both the vector and the scalar run of the same DLL** (byte-identical
render).

## Three-boot validation (2026-09-16/17)

Quiet DLL `38F63C02` (`Work:p4/minigl_ppc.dll`, current HEAD + this
change), host `32716DD9`, `MGLPPC_HOSTPRI=-1`, all diagnostics unset.
Boot 1 cold; boots 2-3 warm reboots (operator instruction), `C:InitPPC`
once per boot. Each row is one run; medians across the three runs.

| Run | Boot 1 | Boot 2 | Boot 3 | Median |
| --- | ---: | ---: | ---: | ---: |
| semantic gears vector fps | 148.514 | 149.253 | 151.515 | **149.253** |
| semantic gears vector serialize | 23,550,277 | 23,534,802 | 23,571,285 | 23,550,277 |
| semantic gears scalar fps | 135.135 | 135.135 | **133.928** | 135.135 |
| semantic gears scalar serialize | 27,380,234 | 27,381,168 | 27,378,309 | 27,380,234 |
| CP gears vector fps | 220.588 | 214.285 | 222.222 | **220.588** |
| CP gears vector serialize | 22,869,125 | 22,876,926 | 22,886,319 | 22,876,926 |
| CP gears scalar fps | 176.470 | 177.514 | 172.413 | 176.470 |
| CP gears scalar serialize | 27,268,859 | 27,260,478 | 27,261,914 | 27,261,914 |
| room-walk vector fps | 58.823 | 59.405 | 57.692 | 58.823 |
| room-walk vector serialize | 5,479,471 | 5,491,311 | 5,532,596 | 5,491,311 |
| room-walk scalar fps | 59.405 | 59.405 | 59.405 | 59.405 |
| room-walk scalar serialize | 6,482,797 | 6,485,359 | 6,486,447 | 6,485,359 |

Medians: semantic gears **149.253 vs 135.135 fps (+10.4%)**, serialize
**−14.0%**; CP emit gears **220.588 vs 176.470 fps (+25.0%)**, serialize
**−16.1%**; room-walk serialize **−15.3%** (its fps is present-bound).
Every run `error=0` / `gl_error=0`, converter validation passes.

Caveats: the room-walk pixel verification fails on the reference
comparison both before and after this change (same signature), so it is
not a regression gate here. Only MPC7410 hardware was exercised; the
probe also accepts the documented `CPUB_G4/7410/7441` values, and the
scalar kernels remain the default on any CPU the probe does not
recognise.
