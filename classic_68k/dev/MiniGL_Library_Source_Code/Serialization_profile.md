# Serialization Profile — MiniGLPPC envmapped gears

Attribution of the `scene`, `serialization` and `switch/display` timing
buckets to exact call paths, plus the optimization options those paths
expose.

Workload: `gearsppc_altivec`, windowed, profiling enabled,
envmap on, overlay off, 10 warm-up + 300 measured frames, physical
68060 + RV280 at `192.168.1.21:2345`.

Sources: `/tmp/opencode/p800.log` (client), `/tmp/opencode/h800.log`
(host). Split-counter rerun: `/tmp/opencode/serialization_profile_gears.log`
and `/tmp/opencode/serialization_profile_host.log`.

---

## 1. Measured baseline

800x600 @ 32 bpp, windowed:

| Bucket | Per frame | Nesting |
|---|---:|---|
| Frame | 16.914 ms | top level |
| Scene | 11.482 ms | inside frame |
| Frontend | 10.213 ms | inside scene |
| Serialization | 9.337 ms | inside frontend |
| Switch/display | 5.425 ms | inside frame, sibling of scene |

Wall clock: 58.974 FPS, 16.957 ms/frame (clean, unprofiled: 59.072 FPS).

68k host pipeline, same run: 16.956 ms/frame total — submit-fence wait
7.914 ms, submit/build 3.397 ms, present-fence wait 1.487 ms, window
present 3.163 ms.

The buckets are **hierarchical, not additive**, and the PPC producer and
the 68k consumer run concurrently.

Per frame the workload issues 300 `glEnd()` calls, 2760 vertices,
25 draw records and ~4.5 ring publishes.

---

## 2. Bucket → call path

### 2.1 Scene — `benchmarkProfile.sceneTicks`

Measured in `examples/gears_benchmark.c`'s frame loop.

```
oneFrame()
  ProfiledPushMatrix / Translatef / Rotatef / TexEnvi / Color3f
  gear()                                      examples/…:766
    flat_face()                               examples/…:534
    draw_inside() / draw_outside()            examples/…:578, :611
    tooth_side()                              examples/…:644
      glBegin() → ProfiledBegin               examples/…:309
      glNormal3f() → ProfiledNormal3f         examples/…:317
      glVertex3f() → ProfiledVertex3f         examples/…:325
      glEnd()   → ProfiledEnd → Lib_glEnd     library/r200_geometry.c:1993
```

Includes: application gear math and cached trigonometry, matrix/texture
state calls, immediate-mode dispatch, and **all backend work triggered
synchronously by `glEnd()`** — including serialization and any ring
back-pressure incurred there.

Excludes `glClear()` and `mglSwitchDisplay()`, which have their own
timers.

### 2.2 Frontend — `context->perf.frontendTicks`

```
Lib_glEnd()                                   library/r200_geometry.c:1993
  EmitPrimitive()                             library/r200_geometry.c:1856
    EmitPrimitiveInternal()                   library/r200_geometry.c:1667
      EmitHardwareBatch()                     library/r200_geometry.c:1523
        CopyHardwareVertex()                  library/r200_geometry.c:1471
        FlushScreenVertices()                 library/r200_geometry.c:119
```

Timed at `library/r200_geometry.c:1867-1874`; it subtracts only
`executeTicks` and `waitTicks`. On the PPC frontend both of those stay
zero (`Submit()` takes the `R200_PPC_FRONTEND` branch,
`library/r200_submit.c:539-561`), so frontend time **includes**
serialization and ring publication.

For this workload 280 of 300 primitives are 4-vertex triangle strips
rewritten as R200 quads (`library/r200_geometry.c:1788-1797`); quads are
list commands (`HardwareListCommand`, `:1512`) and therefore accumulate
in `screenVertices` across `glBegin`/`glEnd` blocks instead of flushing.
The remaining ~20 are the long fans/strips from `flat_face` /
`draw_inside` / `draw_outside`, which are not list commands and force a
flush at each `glEnd`.

### 2.3 Serialization — `context->perf.serializeTicks`

Charged **only** inside `FlushScreenVertices()`, opened at
`library/r200_geometry.c:206-210` and closed at `:816-820`.

Contents, in execution order:

1. Snapshot/merge decision — `PrepareScreenAppend` cache
   (`:32-117`), `SamePendingDrawState` (`library/r200_state.c:167`),
   merge test (`library/r200_geometry.c:408-424`).
2. Slot capacity and mode checks, `SegmentHalfFits`, possible
   `RotateStreamSegment` (`:255-266`).
3. State-batch header handling (`:307-406`) — with
   `RADEON3D_CAP_COMMIT_STATE_BATCH` present, one header per slot and
   an `(offset, count)` pair per draw.
4. Header copy into the record (`:483-500`).
5. **Per-vertex packing loop** (`:501-786`).
6. Posted-write readback of the segment tail (`:787-798`).
7. Nested `Submit()` when a budget or state change closes the batch
   (`:272`, `:323`, `:358`, `:440`), which on PPC calls
   `R200PPCPlatformPublishSlot()`
   (`library/minigl_r200_warpos_compile_probe.c:1165`) — ring-slot
   stall, record copy, `dcbf` of the segment and the ring slot, CRC
   (first publish only), and the `head` bump.

### 2.4 Switch/display — `benchmarkProfile.switchTicks`

Producer side (`examples/gears_benchmark.c`):

```
mglSwitchDisplay()
  R200PPCDispatchSwitchDisplay()   library/minigl_r200_warpos_compile_probe.c:1733
    Lib_mglSwitchDisplay()         library/r200_surface.c:1114-1127
      FlushScreenVertices()
      SubmitAndWait()              library/r200_submit.c:742
        Submit() → R200PPCPlatformPublishSlot()
        WaitAllSubmissions()       (no-op under the ring, :1341-1345)
      R200PPCPlatformBarrier(ctx, TRUE)
                                   library/minigl_r200_warpos_compile_probe.c:1360
```

The PPC call performs **no blit**. It publishes trailing geometry and
posts a present marker into the ring. Its cost is dominated by waiting
for a free ring entry (`:1376-1397`).

Consumer side, concurrent, on the 68k:

```
RingPoll()                         examples/minigl_ppc_host.c:579
  DrainForSegment() → DrainSemanticFence() → Radeon3DWaitFence()   :249, :227
  Radeon3DCommitStateBatch() / Radeon3DCommitBatch() / Radeon3DExecute()
  on MGLPPC_SEMANTIC_FLAG_PRESENT:                                 :920-953
    DrainSemanticFence()           → HostPresentWaitTicks
    mglSwitchDisplay()             → native 68k Lib_mglSwitchDisplay()
      SubmitAndWait(); ClipBlit(); WaitBlit()   library/r200_surface.c:1143-1180
    mglUnlockDisplay()
```

Host fence waits and the blit are *causes* of producer back-pressure but
are not nested inside the client's switch timer.

---

## 3. What the serialization number actually is

The split-counter rerun completed at **59.071 FPS** (300 measured frames,
10 warm-up), effectively identical to the clean 59.072 FPS reference:

| Counter | Total ticks | Per measured frame |
|---|---:|---:|
| `serialize_ticks` | 69,691,823 | 9.387 ms |
| `serialize_stall_ticks` | 56,278,341 | 7.581 ms |
| serialization excluding ring stall | 13,413,482 | **1.807 ms** |
| client scene | 85,578,353 | 11.528 ms |
| client switch | 39,703,633 | 5.349 ms |

In the platform profile (which also covers warm-up/setup), total ring stall
was 3.954 s, split as 3.528 s publish stall and 0.426 s present-barrier
stall. `ring_stall` is accumulated in exactly two places, both inside the
client buckets:

- `R200PPCPlatformPublishSlot()` — `…compile_probe.c:1218-1235`, reached
  from `Submit()` inside `FlushScreenVertices()` (charged to
  serialization) and from `mglSwitchDisplay()` (charged to switch).
- `R200PPCPlatformBarrier(present)` — `…compile_probe.c:1379-1397`
  (charged to switch).

So **80.8% of the serialization bucket is producer back-pressure**, not
vertex packing. The PPC builds frames faster than the 68k/GPU side retires
them; the frame rate is set by the consumer. The rerun's host stages remain
consistent with the first run: 7.921 ms/frame in submit-fence/segment waits,
3.438 ms/frame in submit/build, 1.488 ms/frame in present-fence wait and
3.176 ms/frame in window present (310 host frames including warm-up).

Residual serializer CPU work is **1.807 ms/frame**. Assembly can recover
part of that, but it cannot increase FPS while the consumer remains the
limiting stage.

---

## 4. Optimization catalogue

### A. Instrumentation - completed

Split `ringStallTicks` into `publishStallTicks` and `barrierStallTicks`
in `include/libraries/minigl_ppc_semantic_profile.h` and the two
accumulation sites. Add a `serializeStallTicks` counter to
`struct PerfMetrics` so `FlushScreenVertices` records nested stall
alongside the inclusive total. Without
this, "serialization" conflates packing with back-pressure and every
subsequent measurement is ambiguous. These counters and the benchmark output
were added on 2026-08-31 and produced the measured split above.

### B. Consumer-side — where the frame time actually is

The host spends **7.914 ms/frame** in `HostWaitTicks`. Two mechanisms
produce it:

1. **Unconditional drain on the inline path.**
   `examples/minigl_ppc_host.c:860-872` calls `DrainSemanticFence()`
   before every non-commit `Radeon3DExecute()`. The gears frame's
   `glClear` takes this path, so each frame pays one full fence wait
   before any geometry is submitted. The ring slot is owned by index and
   there are 8 of them; the drain looks stronger than the ownership
   discipline requires. Verify whether it can be reduced to
   `DrainForSegment()` semantics (it touches no vertex segment at all).

2. **Vertex-segment reuse.** `SEGMENT_HALF_BYTES` is 128 KiB
   (`library/minigl_r200_internal.h:105-106`) and the workload writes
   2760 x 40 B = **110 KiB of vertices per frame**, i.e. about one
   `RotateStreamSegment()` per frame against the 3 segments the host
   allocated (`vseg=3` in the log; `MGLPPC_VERTEX_SEGMENTS` is 4).
   Reuse of a still-queued segment forces `DrainForSegment()` to wait
   (`examples/minigl_ppc_host.c:249-257`). Raising the number of
   segments, or the segment size, directly buys producer/consumer
   overlap. This is the highest expected-value change in the list.

3. **Windowed present** costs 3.163 ms/frame in `ClipBlit` + `WaitBlit`
   (`library/r200_surface.c:1166-1170`) on the 68k. A driver-side blit
   (Radeon3D 2D engine) instead of a CPU `ClipBlit` would remove most of
   it and is in scope per the project's driver policy.

#### B.1/B.2 outcome (2026-08-31): attempted, measured, host changes REVERTED

B.2's root cause was found and kept: the host's ring registration needs
five leases (control block + four vertex segments) but
`library/r200_state.c` capped leases at 4, silently downgrading every
session to three vertex segments. The cap now matches
`RADEON3D_MAX_SEGMENTS` and the host registers **vseg=4**.

B.1 (removing the inline-path drain and the blocking `DrainForSegment`)
was built and measured head-to-head at **6000 frames** (the 300-frame
sprint overstated it: 63.1 vs 59.5):

| Host variant | 6000-frame FPS | Host GPU wait | Submit CPU | PPC stall |
|---|---:|---:|---:|---:|
| Original (drain-paced) | **59.527** | 6.59 ms | 2.37 ms | 11.31 ms |
| No-drain + fence poll | 55.854 | 7.02 ms | 2.56 ms | 13.86 ms |

The total GPU wait is invariant - the drains were never the GPU cost -
but removing them made everything around them worse: submits and the
blit execute against a deep GPU queue (MMIO contention), and the host's
present-boundary wait blocks ring retirement in one concentrated window
instead of overlapping producer work mid-frame. The mid-frame drains are
load-bearing pacing. The host source was reverted to the original; the
The no-drain variant was retained only as a temporary comparison probe.

Lesson recorded: future host/driver A/Bs must use 6000-frame runs;
300-frame sprints overstate by up to ~6%.

### C. Ring depth and pipelining

`MGLPPC_RING_SLOTS` is 8 (`include/libraries/minigl_ppc_protocol.h:70`)
and the producer stalls at `(head - completed) >= slotCount`. Because
`completed` only advances as the host retires entries, the effective
depth is bounded by the host's fence discipline, not by the slot count —
raising slots alone will not help until B.1/B.2 are fixed. Re-measure
after B.

### D. PPC serializer codegen - IMPLEMENTED, measured bus-bound

The fast serialization paths in `FlushScreenVertices` were m68k-only:
`library/r200_geometry.c` guarded them with
`#if defined(__GNUC__) && defined(__m68k__)`, so the PPC/vbcc build fell
through to the generic per-field loop with an out-of-line `FloatBits`
call per float. S1 (2026-08-31) added PPC equivalents:

- `client/minigl_ppc_serial.s`: four `stwbrx` kernels
  (`MGLPPCSwapLit13/Lit10/TCL10/TCL7`) mirroring the m68k movem shapes;
  linked into both semantic archives, the frontend proof and step2.
- `FloatBits` became a `static inline` union pun in
  `minigl_r200_internal.h` (removing ~24k out-of-line calls per frame).
- Dispatch in `FlushScreenVertices` ahead of the generic loop, with
  `serialize_kernel_flushes` / `serialize_kernel_vertices` counters
  (`MGL_PERF_SERIALIZE_KERNEL_*`) and a vbcc-compatible ScreenVertex
  layout assertion.

Measured on the physical machine (800x600x32 windowed, 300 frames,
logs `serializer_fast_gears.log`, `serializer_fast2_gears.log`):

| Metric | Baseline | S1 |
|---|---:|---:|
| fps_x1000 | 59071 | 59447 / 59463 |
| serialize_ticks | 69,691,823 | 68,140,046 |
| serialize_stall_ticks | 56,278,341 | 54,747,419 |
| serialize CPU (ticks - stall) | 1.807 ms/frame | 1.803 ms/frame |
| kernel coverage | - | 7500 flushes, 828000/828000 vertices |

The counters prove the kernels execute on 100% of commit flushes and
vertices, rendering stays correct (semantic smoke `status=ok frames=120`,
two clean benchmark runs) - and the CPU residual **did not move**.
Conclusion: the ~1.8 ms serializer residual is **VRAM-aperture store
bandwidth, not CPU instructions**. 110 KiB/frame through the PCI
copyback aperture at ~61 MB/s is the wall; the PPC simply issues stores
faster than the bus drains them. Further instruction-side work (AltiVec,
different addressing, S2 struct reordering) cannot reduce this below the
bus time for this byte volume; only fewer bytes or a different write
path could. The S1 kernels still stand: they are correct, slightly
positive (+0.6-0.7% FPS through reduced issue stalls and variance), and
they cut PPC CPU cycles, which becomes relevant only once the consumer
stops being the limiting stage.

### E. Byte-volume reduction (helps B.2 directly)

Every byte removed from the vertex stream postpones a segment rotation
and therefore a host fence wait.

- The vertex is already compact (10 dwords). The remaining passenger is
  the object-space normal, required by hardware sphere-map texgen; it
  cannot be dropped for this workload.
- The 4-vertex-strip → quad rewrite (`:1788-1797`) is byte-neutral and
  buys merging; do not extend it to longer strips, which would duplicate
  shared vertices roughly 2× and make B.2 worse.
- An indexed draw atom in a future Radeon3D semantic interface would cut
  the gear vertex volume substantially; the current ABI has none.

### F. Non-issues, ruled out by this profile

- `MAX_SLOT_DRAWS` is 192 (`library/minigl_r200_internal.h:100`) against
  25 draw records/frame — not a limit.
- Header cost is already amortized: `RADEON3D_CAP_COMMIT_STATE_BATCH` is
  present, so the ~110-dword texgen+normal header is emitted once per
  state batch (~3/frame), not per draw.
- Publish CRC is armed once per session (`crc_us = 26` for the whole
  run).
- `publishFlushTicks` (`flush_us = 871`) is negligible.

---

## 5. Verification protocol

Per the project policy, a clean build and an empty `nm -u` are necessary
but not sufficient. For each change, on the physical machine:

1. Install the library, then `Avail Flush`.
2. Run the clean (unprofiled) binary for the FPS number — that is the
   result of record.
3. Run the profiled binary once for attribution; do not quote profiled
   FPS as the result.
4. Reboot, `tools/wait_bridge.sh`, reconnect explicitly to
   `192.168.1.21:2345`.
5. Three runs, take the median; change one variable between series.
6. Re-run `phase5` deferred ordering, `phase6` primitives and `phase6`
   acceptance after any change to submission, snapshot or serial logic.
