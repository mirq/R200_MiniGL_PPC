# MiniGLPPC 130 FPS Plan

Status: active performance record, but the original Phase A-E roadmap is
superseded. The 130 FPS target is not met and currently has no accepted path:
the mandatory Phase E CP-stream approach was implemented, measured, and
rejected on 2026-08-30. The accepted configuration remains Radeon3D
interface 16 state batches plus ordered commits at a reboot-isolated median of
52.456 FPS. The measured foundation and subsequent work are retained in the
progress log below. Driver source: `/home/mirek/p96-driver`.

## Original decision log (2026-08-28, superseded where noted below)

- Success bar: **130 FPS** on the reference workload (gears,
  800x600 windowed, 32 bpp, envmap) on the physical
  68060 + Sonnet 7410 400 MHz + RV280 machine.
- `Radeon9200.chip` driver changes are in scope from Phase C onward
  (PPC-alias queries, submit-ring endpoint).
- Original execution order: **B then C**, then D, then E. Phases B-D were
  explored; Phase E was performance-rejected as recorded at the end of the
  progress log.
- The byte-identical 68k/PPC record gate may be relaxed to pixel-tolerance
  validation for FP-derived texgen coordinates (enables AltiVec
  `vrsqrtefp`); headers, matrices and all non-texgen state remain bit-exact.
- Keep the validated one-outstanding-Execute service invariant and the
  immutable-slot ownership discipline in every transport redesign.

## 0. Original physics estimate for 130 FPS (7.7 ms/frame)

Reference frame: gears 800x600 windowed envmap = 2760 vertices, ~30K record
dwords inline, ~34K generated CP dwords. Steady state with N-frame
pipelining is max(PPC work, host work):

| Work | Must live on | Budget |
|---|---|---:|
| Vertex capture + sphere-map texgen + record build (~2760 vtx) | PPC + AltiVec | ~3-4 ms |
| CP-stream generation (34K dwords) | PPC (Phase E; 16.45 ms on 68k) | ~1-2 ms |
| CP ring write (34K dwords) | PPC (1.5 ms at 91.8 MB/s vs ~25 ms at 5.4 MB/s) | ~1.5 ms |
| Present blit 800x600x32 | 68k host, overlapped with frame N+1 | ~2 ms |
| Control (message or doorbell per frame) | either | <0.2 ms |

The original estimate put the Phases B+C+D asymptote at ~20-30 FPS because 68k
`Radeon3DExecute()` (copy + build + submit, ~44 ms/frame at native gears record
sizes) remained. Later work exceeded that estimate and reached 52.456 FPS, but
also confirmed that the proposed Phase E implementation regressed performance.
Phase E was mandatory only for the original roadmap; that route is now closed.

Segment churn at 130 FPS: ~136 KB/frame vertex streaming into 8 x 256 KiB
iface-13 segments means rotation every ~2 frames; the rotation path must
pre-allocate/park without per-rotation service round trips.

Measured foundations (2026-08-27/28, physical target):

- PPC aperture writes 91,776-91,817 KiB/s vs 68k 5,276-5,426 KiB/s (17.1x).
- AltiVec arithmetic + VR20-VR31 retention across WarpOS task switches: pass.
- Semantic bridge today: 3.38 FPS @800x600 envmap vs native 68k 8.10 FPS.
- Per-Execute host cost after CRC gating: ~13.3 ms (copy ~1.5, build ~3.4,
  submit ~2.8-4.5 ms per call at gears sizes).
- Unattributed remainder: ~150 ms/frame (transport + host-side the PPC
  cannot see directly). Phase A owns this number.

## Phase A — Attribution (gates everything)

1. Host (`examples/minigl_ppc_host.c`): per-message handler busy time
   (GetMsg -> ReplyMsg), per-command counts (semantic execute, barrier,
   control), host busy vs idle split over the session. Extend the
   `stage_ticks` summary.
2. PPC platform layer (`library/minigl_r200_warpos_compile_probe.c`,
   `R200_FRONTEND_LIBRARY` section): mftb timebase counters around
   publish send, reply wait, barrier round trip; report ticks calibrated
   against measured FPS (no per-message OS calls in the timed path).
3. Split the "~32% frontend" claim into capture / serialize / texgen /
   slot-stall using the existing frontend perf counters in the PPC build.
4. Reconcile host-measured 13.3 ms/Execute against service telemetry
   (`radeon3dinfo` deltas around one fixed semantic session).
5. Verify vbcc `vc` targets the 7410 pipeline for C code (asm already
   uses `-m7410`); fix if generic.
6. Measure the PPC->68k message round-trip floor explicitly.

Gate: the missing ~150 ms/frame owned to +/-10 ms per component.

## Phase B — Transport: one round trip per frame (protocol v11)

1. Cache the host port; drop per-send `FindPort`/`Forbid`
   (`client/minigl_ppc_transport.c`).
2. Frame-batch descriptor message: one message per frame listing
   {slot, dwords, flags, gen, crc} executes; the host serializes Executes
   exactly as today; PRESENT rides the last entry; two frame descriptors
   in flight for N/N+1 overlap.
3. Slot pool: 2 sets x 4-6 x 8192 dwords (~64-96 KiB shared).
4. Shared-memory doorbell ring (doorbell dword in the validated uncached
   streaming aperture; host spins between Executes) only if Phase A shows
   message latency dominates at 1-2 round trips per frame.
5. Texture uploads: single cacheable staging buffer + one message per
   upload (replaces 1 KiB-word chunking).

Gates: step2 semantic smoke, 120-frame texture smoke, protocol-v10 replay
fallback, native Phase 4/5/6 smokes. Expected: ~6-9 FPS.

## Phase C — PPC-written streaming segments + driver aliases

1. Driver: private query returning a PPC-usable alias for a leased
   iface-13 segment's CPU mapping (probe first; add the query only if the
   lease mapping is 68k-private).
2. Port the commit path (`library/r200_geometry.c` commit destination,
   `library/r200_submit.c` rotation) to the PPC frontend: vertices stream
   directly into segment halves at 17x aperture speed; records shrink to
   draw headers (~30K -> ~2-4K dwords/frame).
3. Host per submit: small Execute + CommitBatch (~2-4 ms vs 13.3).
   Fence-parked async segment rotation reused; pool pre-allocation for the
   130 FPS rotation rate.
4. Commit-batch submission stays in the v11 frame message until Phase E.

Gates: cross-CPU header identity, pixel-equality vs native 68k (gears
envmap), phase5/6 acceptance, segment-rotation stress. Expected: >= 8-12 FPS.

## Phase D — AltiVec + codegen on the PPC frontend

1. Kernel inventory from Phase A counters; retune vbcc flags per hot TU.
2. AltiVec asm kernels with scalar fallbacks: 4x4 matrix/MVP combine,
   snapshot compare, vertex pack (`stvx`), sphere-map texgen via
   `vrsqrtefp` + Newton refine.
3. Texgen validated by pixel tolerance per the decision log; headers and
   matrices bit-exact; cross-CPU record-dump proof extended accordingly.

Expected: capture+texgen from ~80 ms toward ~3-4 ms.

## Phase E — PPC-native submission endpoint (performance-rejected)

This was the original final phase. It was implemented as the interface-17
CP-stream program, physically validated, and rejected after measuring below the
accepted interface-16 baseline. See the 2026-08-30 decision at the end of the
progress log and the archived `phase2-cp-stream` branches.

1. Submit ring in the uncached aperture: PPC writes CommitBatch
   descriptors + CP streams; 68k validates bounds cheaply and keeps
   device open/recovery/fences/present (Warp3D/Warp3DPPC split).
2. Port Execute build+submit into PPC-callable code kernel by kernel,
   preserving today's trust boundary (no unrestricted client CP).
3. Control shrinks to a per-frame doorbell + present request.

Gates: CP context-loss recovery, Phase 6 acceptance native + bridge,
cold/warm boot matrix, fence/segment-teardown/shutdown quarantine.
Original expected ceiling: 100-160 FPS. Hardware results did not support this
estimate; no current accepted configuration is expected to reach 130 FPS.

## Original milestone ladder (superseded by measured results)

| Stage | Expected FPS (800x600 envmap windowed) |
|---|---:|
| Initial protocol-v10 baseline | 3.4 |
| After Phase B | ~6-9 |
| After Phase C | >= 8-12 |
| After Phase D | ~20-30 |
| After Phase E | 100-160 (target 130) |

These values were planning estimates, not current status. The accepted measured
result is 52.456 FPS on protocol v14/interface 16; the proposed Phase E measured
46.8 FPS and was rejected.

## Hardware protocol (per AGENTS.md)

Physical target only (192.168.1.21:2345), no emulator tools. One
`InitPPC` per boot; `Avail Flush` after installing libraries; no
Break+relaunch of GL clients (small probe between clients);
`tools/wait_bridge.sh` after reboots. One variable per benchmark run,
three-run medians; record mode/depth/windowed and boot state.

## Progress log

- 2026-08-28: plan recorded. Starting Phase A instrumentation
  (host per-command busy counters, PPC mftb transport counters,
  frontend section split, vc CPU-target check).
- 2026-08-28 (Phase A instrumentation complete, all builds pass):
  - New `MINIGLPPC_130FPS_PLAN.md` (this file).
  - `library/minigl_r200.c`: mftb-based `PerfClock()` for the WarpOS PPC
    frontend (`R200_PPC_FRONTEND && __VBCC__`), calibrated once against
    `Delay(2)`; new `PerfClockRate()`; `Lib_glHint` perf-enable no longer
    requires the 68k `TimerBase`, so `-profile` now works on the PPC
    semantic path and the whole `MINIGL_GEARS_SUMMARY` perf pipeline
    (frontend/serialize/spheremap/wait ticks) lights up on the 7410.
  - `library/minigl_r200_warpos_compile_probe.c` (`R200_FRONTEND_LIBRARY`):
    new `MGLPPCSemanticPlatformProfile` + `R200PPCPlatformProfile()`
    measuring publish send, CRC, dcbf flush, reply wait (avg+max), barrier,
    control and texture-chunk messages, and session span, all in mftb
    ticks (no cross-CPU calls added to the measured path).
  - Protocol v10 additive: `MGLPPC_CAP_CRC_FIRST_ONLY` (1<<3). Host sets it
    unless built with `MGLPPC_HOST_VERIFY_CRC`; PPC skips the per-submit
    CRC computation after the first Execute (host already skipped
    verification there; removes ~1 ms/frame of pure waste on the PPC).
  - Host (`minigl_ppc_host.c`): per-class message counters (control /
    barrier / execute), handler busy ticks per class, idle ticks (time in
    `WaitPort`), wall session ticks; extended shutdown summary prints
    `session wall_us= idle_us= msgs control= control_us= barrier=...
    execute_msgs=`; existing per-Execute `stage_ticks` unchanged.
  - Historical clients: `minigl_warpos_semantic_library_smoke_profile` and
    the retired `milestone_gears_semantic_ppc_profile` printed
    `MGLPPC_SEMANTIC_PROFILE ...` (publishes/send/crc/flush/waits/max/
    barriers/controls/chunks/session, `tb_hz` for tick conversion).
    Make target: `make -f Makefile_warpos.vbcc r200-semantic-profile`.
  - Local validation: 68k host, WarpOS semantic archive + profile clients,
    replay smokes, native GCC r200 library + importlib + testprograms
    (dispatchtable regen: no diff), vbcc 68k r200 library (no new warnings
    vs baseline), and both cross-CPU frontend proofs (68k GCC + PPC vbcc).
- SUPERSEDED measurement recipe (hardware offline 2026-08-28):
  run the Phase A measurement session on 192.168.1.21 after an operator
  power-cycle + one `InitPPC`:
  1. `tools/wait_bridge.sh`, then `amiga_connect(host="192.168.1.21", port=2345)`.
  2. Push `bin/miniglppc-bridge/minigl_ppc_host` -> `Work:mglppc_hA`,
     `bin/warpos/minigl_warpos_semantic_library_smoke_profile` ->
     `Work:mglppc_sA`, `bin/warpos/gearsppc` -> `Work:mglppc_gA`, plus a
     native `bootstrap_smoke_r200_gcc` for
     pre/post health checks (protect scripts `+S` if script transfer).
  3. Native bootstrap smoke, then: `Run >NIL: Work:mglppc_hA
     >T:mglppc_hA.log`, `Wait 1 SECS`, then the profile smoke
     (`Work:mglppc_sA >T:mglppc_sA.log`). Host + client share protocol
     v10; `crc_first_only` is additive and backward compatible.
  4. Gears attribution: `Work:mglppc_gA -width 800 -height 600 -depth 32
     -window -overlay -profile -envmap Work:minigl_demos/data/chrome.ppm`
     (exact client; `-profile` now enables the library perf counters on PPC).
  5. Read `MINIGL_GEARS_SUMMARY` from the client log and the
     `session`/`stage_ticks` lines from the host log;
     reconcile with `radeon3dinfo` deltas captured before/after the gears
     run. Record mode/depth/boot state per AGENTS.md.
  6. Phase A gate: own the ~150 ms/frame remainder to +/-10 ms, then
     proceed to Phase B.
- 2026-08-28 (Phase A MEASURED — gate met, physical target, one `InitPPC`,
  bootstrap smoke pass before and after all PPC runs):
  - Deployment: host `Work:mglppc_hA`, profile smoke `Work:mglppc_sA`,
    profile gears `Work:mglppc_gA`, native `Work:mglppc_boot`. Smoke
    status=ok (120 frames); gears 130-frame sessions clean.
  - Baseline reproduced: **3.369 fps (297 ms/frame)** at 800x600x32
    windowed envmap unprofiled (historical 3.38). `-profile` distorts to
    2.13 fps: the client's per-vertex EClock sampling is two cross-CPU
    bridge calls per vertex. Library perf ticks are mftb-based and clean.
  - Cost attribution (per frame, unprofiled):
    | Item | ms | Evidence |
    |---|---:|---|
    | Host `Radeon3DExecute()` x5 | ~68-70 | host submit stage 13.6-13.7 ms/execute (8.69-8.91 s/650) |
    | Host handler total | ~103 | host busy 103 ms/frame incl. barrier 2.9 ms, control |
    | PPC message send x5 + barrier | ~20 | 2.0-2.6 ms per PutMsg (WarpOS bridge) |
    | Reply residual waits x5 | ~26 | avg 5.2 ms, max 29.7 ms |
    | Present barrier round trip | ~5-7 | PPC 5.3-6.9 ms vs host 2.9 ms handler |
    | PPC library frontend | ~15 | frontend 52.7M mftb (~27 MHz), serialize 43.5M |
    | Unaccounted (both CPUs idle) | ~150-190 | host idle 62-81% of wall; PPC idle ~88% |
  - Root causes, ranked: (1) the 2-slot reply-gated protocol serializes
    every publish behind the previous reply, so 6 round trips/frame run at
    ~5-8 ms each of which ~75% is pure WarpOS cross-CPU machinery — both
    CPUs idle most of the frame; (2) host `Radeon3DExecute` is 10x the
    smoke cost (13.7 vs 1.17 ms) at gears record sizes — per-dword 68k
    work scales with record size, confirming Phase C (segment streaming
    shrinks records to headers); (3) PPC frontend is NOT a bottleneck
    (the old "32% frontend" claim was profiler distortion).
  - CRC-first-only verified live: PPC crc 474 us total for a 650-execute
    session; host crc stage 27 ms total (tripwire only).
  - Calibration note: `Delay(2)` mftb calibration jitters (27.0/33.3/27.5
    MHz across sessions); treat tick ratios as reliable, absolute ms via
    EClock cross-check when precision matters.
  - Phase B decision (gate answer): even ONE message round trip per frame
    costs ~7-9 ms — a third of the 130 fps frame budget. The hot path
    must use the shared-memory doorbell ring (zero OS machinery, host
    spins between Executes); Exec messages remain only for lifecycle
    (context/texture/barrier). Frame-batch (v11) remains the first
    staging step to validate multi-slot rings over the existing port.
  - Artifact note: PPC `Submit()` does not bump the library perf counters
    (submits=0, record_dwords=0 in the summary) — cosmetic gap, host
    execute counts cover it.
- 2026-08-28 (Phase B IMPLEMENTED AND VALIDATED — protocol v11 ring path
  live on hardware):
  - Architecture: 8-slot submission ring. Control block lives in a 64 KiB
    non-cacheable streaming segment leased through the backend's device via
    new private bridge queries (`MGL_R200_PRIVATE_SEGMENT_V1_QUERY` /
    `_FREE_V1`, plus `MGLR200PrivateBridgeV2` with deviceEpoch). Record
    payloads stay in 8 cacheable PPC-allocated 8192-dword slots; the
    frontend's validated 2-slot build discipline is untouched — publish
    copies the built record into the next ring slot (copy-on-publish).
    Exec messages remain only for lifecycle (hello/create/register/
    textures/unregister/destroy/shutdown); the hot path is
    write-entry+dcbf+sync+head++ with zero OS calls.
  - Protocol: v11 (forces host/client pairing), `MGLPPC_CAP_SEMANTIC_RING`
    (1<<4), `MGLPPC_CMD_REGISTER_SEMANTIC_RING`, `MGLPPCRingControlV1`
    (head/completed/error/presentSeq + per-slot entries). v10 message path
    fully preserved as fallback (host builds: `MGLPPC_DISABLE_RING`).
  - PPC reads of the aperture were NEVER hardware-validated before (the
    original VRAM probe only tested PPC writes / 68k reads). The PPC maps
    the segment write-through + read-allocate: its first cached read of
    `completed` goes stale. Fix: `MGLPPCInvalidateLine32` (dcbf+sync on the
    control line) before every poll read. Verified by an extended VRAM
    probe (PPC self-read + host-sentinel readback).
  - Crash triage during bring-up (3 wedge/reboot cycles): 68k host alert
    #80000004 (illegal instruction) = `Radeon9200Base` never opened in the
    ring register path — first `Radeon3DExecute` proto call jumped through
    a NULL base. Fixed (mirrors `RegisterSemanticSlots`). Also fixed:
    PRESENT-only ring entries (dwords=0) must skip CacheClearE/CRC/Execute;
    bounded (5M-iteration) spins on every PPC wait so a dead host fails the
    session cleanly instead of hanging.
  - Host consumer loop: spin (GetMsg + RingPoll). A timer-sleep variant
    (2 ms UNIT_MICROHZ) was attempted to cut idle CPU and fix chunk-path
    slowdown under spin, and hung twice (first: PA_IGNORE reply port never
    signals — fixed with PA_SIGNAL+AllocSignal; still froze) — REVERTED to
    spin. Idle-sleep deferred; needs isolated investigation on hardware.
  - Hardware results (800x600x32 windowed envmap gears, exact client,
    3-run median): **3.290 fps** (3.293/3.290/3.294) vs v10 3.369 — parity
    within noise. Every session: 780/780 entries consumed (5 executes +
    1 present per frame), zero CRC/generation/execute failures, native
    bootstrap smoke passes before and after.
  - Transport per frame (ring vs v10): publish send 82-94 us vs 2.6 ms
    per submit; reply waits 25.8 ms -> 0; barrier round trip 5.3-6.9 ms ->
    3.3 ms (drain-dominated); ring_stall ~0-90 us total (host keeps ~6
    entries behind, i.e. ~1 frame of runway; never fills).
  - KEY Phase B finding: transport is no longer measurable, yet the frame
    stays ~300 ms. The unattributed remainder is therefore mostly PPC-side
    client cost (gear math + ~3.8k dispatch-table API calls/frame through
    the vbcc-compiled frontend), plus the 84 ms host Execute bucket. Phase
    C (segment streaming shrinks Execute to ~2-4 ms) and Phase D (PPC
    client/frontend speed) are both now directly load-bearing; first
    Phase D sub-step should be a clean client-side wall-clock split
    (gear math vs API capture vs submit) without per-call sampling.
  - Temporary diagnostics left in place (harmless): host step markers,
    RING reg/publish0/barrier0 client markers, switch_error print. Remove
    before Phase C benchmarking to keep logs small.
  - Local validation: native GCC r200 library + importlib + testprograms
    (dispatchtable regen: no diff), vbcc 68k r200 build, WarpOS archive +
    profile clients, cross-CPU frontend proofs. New LIBS:minigl.library
    (112916 B) installed on the target with Avail Flush;
    `Work:minigl-pre-ring.library` is the rollback copy.
- 2026-08-28 (Phase C IMPLEMENTED, MEASURED, HYPOTHESIS REFUTED on the
  PPC path — default reverted to inline ring):
  - Built the full vertex-streaming path: host leases 3 vertex segments
    (256 KiB class) alongside the ring via the backend private query;
    register reply carries {cpu,bytes,id} per segment; PPC pool wired into
    `proofContext.segmentPool` + `context->segment` with
    `streamSegments=TRUE`, so the SHARED frontend commit machinery
    (mode decision, half-fit guard, degrade-to-inline, header-only
    records, vertexOffsets) ran unchanged on the PPC. Ring commit entries
    carry {draws, segmentIx} in the slot buffer (slot[0]/[1] — commit
    metadata moved OUT of the control block after a copyback-visibility
    failure: the PPC aperture maps copyback on BOTH sides, so control
    fields need explicit dcbf flush on write and CacheClearE refetch on
    read; slot data was already the proven cache domain). Host consumer
    routes FLAG_COMMIT entries to `Radeon3DCommitBatch` (SegmentId from
    the host lease table, Records/VertexOffsets pointing into the ring
    slot); PPC pool rotation via `R200PPCPlatformRotateSegment`
    (completed-based reuse safety, bounded).
  - Bring-up found and fixed: NULL `Radeon9200Base` on the commit path
    (same class as Phase B's crash), PPC copyback entry-field visibility
    (host read draws=0 then 1 — raw dump proof; fixed by moving commit
    metadata into the flushed slot + dcbf on entry/head lines), and PPC
    vertex-data visibility (segment writes flushed before publish — the
    68k never needed this; the PPC aperture maps copyback, NOT the
    write-through the Phase A write-throughput probe suggested).
  - Functionally validated: smoke status=ok with every draw via
    CommitBatch (vseg=3 leased, zero service rejects, 120/120 executes
    + presents), gears sessions complete with 6 publishes/frame
    (25-27 draw header batches, records 6521 -> 2761 dwords).
  - **Measured (gears 800x600x32 windowed envmap): commit streaming =
    2.514 fps vs inline ring 3.290 median — a 24% REGRESSION.** The
    record shrink (2.4x fewer record dwords) does not pay for the PPC
    side: vertices leave the serializer through the copyback aperture
    mapping and need a ~120 KB dcbf flush per frame; after Phase B the
    transport was already near-zero, so the old Execute-per-dword cost
    the commit path was designed to remove is smaller than the new PPC
    store/flush cost. The "PPC writes 17x faster" probe result holds for
    tight sequential loops, not the serializer's interleaved pattern.
  - At this checkpoint the default was reverted to the inline ring (3.297 fps
    confirmed post-revert), and commit streaming was temporarily opt-in via
    `-DMGLPPC_ENABLE_STREAM=1`. This flag and default were later superseded:
    current source enables streaming unless built with
    `-DMGLPPC_DISABLE_STREAM`.
  - At this checkpoint Phase B's inline ring remained the shipped configuration: transport
    ~94 us/publish, zero reply waits, 780/780 entries consumed, 3.29 fps
    — the binding constraints are now the 68k CommitBatch/Execute bucket
    (~14 ms x 6) and PPC client-side cost; both are Phase D work.
  - Hardware hygiene: native bootstrap smoke passes after every session;
    ping 8.8.8.8 kept running per operator instruction; 6 reboots total
    this session (all clean recoveries via tools/wait_bridge.sh +
    InitPPC + ping).
- 2026-08-28 (D0 FRAME TIMELINE — DECISIVE: the plan's remaining
  phases re-ordered; Phase D client-side is the main line):
  - Instrumentation: `-stageprof` client mode (stage ticks only —
    WARNING: never combine with per-vertex EClock sampling on WarpOS;
    each sample is a PPC->68k bridge round trip and 2.8/frame-vertices
    cost minutes per run — gated ImmediateProfileStart/StateProfileStart
    off under stageprof), mftb event ring (2048 entries) in the platform
    layer with publish/barrier/drain enter-exit marks + client
    frame marks + EClock sync pairs, dumped as MGLTL lines.
  - Client stage split (gears 800x600x32 windowed envmap, -stageprof,
    130 frames): **scene 260.7 ms/frame (75%)** — gear math + ~2800 GL
    calls + capture; **switch 40.1 ms (12%)** — frontend flush + ring
    posts + drain; **clear 16.6 ms (5%)**; events 0.9 ms. Whole frame
    347 ms (2.88 fps under stageprof; 3.29 clean).
  - Timeline detail (one decoded frame, 8.04M mftb ticks): client build
    between submits ~7.4M ticks (92%), publish spans ~2.0M (24.5% —
    copy 26 KB + CRC + dcbf through the copyback ring slot), frame-end
    drain + present 0.55M (7%). Host is never the wait.
  - **CONCLUSION: the 68k is no longer the bottleneck — it now receives
    ~7% of the frame. ~92% is PPC client-side code: vbcc-compiled gear
    math + ~2800 per-vertex dispatch calls (93 us/vertex — slower than
    the 68060's GCC-compiled frontend despite 8x clock).** The Phase D
    premise is confirmed and quantified; the Phase E premise (68k in the
    frame path) is now secondary: the host execute bucket is pipelined
    behind the PPC and hidden.
  - REVISED Phase D order (main line):
    1. D1a cheap win: move ring slots into the leased UNCACHED streaming
       aperture (Phase C machinery, no commit) — eliminates the 26 KB
       copy + dcbf per submit (~24% of frame) by construction instead of
       by flush; consumer already reads the slots uncached.
    2. D1b: client vertex path — batch the ~2800 per-vertex dispatch
       calls (MGLPPCBatch-style or extend FastPath v3 to PPC); target
       <10 us/vertex from 93.
    3. D1c: AltiVec gear math + matrix kernels.
    4. D2 (was Phase E): driver submit-ring/PPC doorbell — only after
       D1 makes the PPC the bottleneck again, which the current numbers
       say is not yet the case.
   - Phase C commit streaming stays parked (opt-in define).
- 2026-08-29 (D1 ROOT CAUSE FOUND AND FIXED — misaligned float traps, not
  codegen; gears 3.29 -> 7.82 fps clean, 5.73 instrumented):
  - New `examples/minigl_warpos_ppc_microbench.c` (make target
    `ppc-microbench`): mftb calibrated against the timer.device EClock
    on the 7410 (real timebase ~24.55 MHz; the Delay(2)-based
    `PPCRawClockRateCalibrated` reads 51-62 MHz — ~2.2x too high, so
    all previously published mftb->us conversions are scaled wrong;
    EClock-based stage splits remain valid).
  - Isolation results (2000-vertex loops, host running, ring live):
    capture through glVertex3f was 84 us/vertex while inline PPC math
    runs at full speed (f_fma 72 ns/iter, preempt probe: 300 ms spin at
    100% CPU — no starvation); sin/cos via ppcmath 24.7/16 us per call;
    drawarrays (frontend-internal loop) 120-180 us/vertex.
  - Root cause: `CFLAGS -amiga-align` gives members 2-byte alignment.
    `MiniGLR200Context.vertices/screenVertices/pendingDraw/currentS`
    sat at offset%4==2, and floats after BOOL/UBYTE runs were odd
    everywhere. A misaligned lfs/stfs on the 7410 traps into the OS
    alignment handler: measured **4.47 us per misaligned float store
    vs 0.04 us aligned**. AddVertex's 13 float stores = ~84 us — the
    entire "93 us/vertex slower than the 68060" mystery. Not vbcc
    codegen (fmadds/lfs scheduling is fine), not preemption, not the
    transport (ring_stall ~0).
  - Fix in `library/minigl_r200_internal.h`: widen the context's 39
    2-byte `BOOL` members to `ULONG`, and pad `LightState` 130->132,
    `StreamSegment` 26->28, `ArrayState` 18->20 so every float member
    and every 4-byte instance base lands 4-aligned. Context is opaque
    and frontend-internal (no ABI impact); 68k side never reads PPC
    context memory. Verified by `examples/minigl_warpos_align_probe.c`
    (offsetof ground-truth probe): all float members mod4==0.
  - Post-fix microbench: capture_only 84 -> **0.29 us/vertex** (290x);
    capture_fog 0.48; gl_vtx3f with flushes now ring-stall-dominated
    (host Execute bucket) as expected.
  - Gears 800x600x32 windowed envmap, same binary/flags otherwise:
    client scene stage 273 -> **44 ms/frame**; publishes ~30 us each;
    **5.73 fps with -stageprof -profile instrumentation, 7.82 fps
    clean** (baseline 3.29). Native bootstrap smoke passes before and
    after; no library reinstall needed (68k library untouched).
  - Also fixed: timeline marks in the gears client are now gated by
    `MINIGLPPC_SEMANTIC_PROFILE` (they broke every non-profile build
    of the file).
   - NEW ranked constraints (replaces the D0 "92% PPC client-side"
     conclusion, which was trap distortion):
    1. 68k host Execute/drain bucket ~80-100 ms/frame (5 executes +
       present at ~16 ms each) — now visible as the barrier/switch
       stage; Phase C/E territory.
    2. Client gear trig: ppcmath sin/cos 16-25 us/call, 2-6 calls per
       vertex ≈ ~30-40 ms/frame — precompute static gear tables
       (trivial, next step).
    3. FPS overlay ~39 ms/frame when enabled — skip in benchmark mode.
     4. mftb calibration: replace Delay(2) calibration with the
        EClock-based method from the microbench (2 OS calls, exact).
- 2026-08-29 (Phase E driver attribution, physical 68060/RV280,
  800x600x32 windowed, 300 measured frames):
  - CommitBatch telemetry split over 454 calls was copy 964.6 ms,
    semantic build 1489.9 ms, and CP submit 289.3 ms: 2.12, 3.28,
    and 0.64 ms/call respectively. Copy plus semantic translation is
    the dominant driver work; the driver-owned CP tail is already small.
  - A direct-read experiment removed the CommitBatch trusted-record copy
    while leaving Execute, semantic validation, CP ownership, fences, and
    recovery unchanged. It passed native smoke and completed at 17.750 fps.
  - Direct parsing made the build phase cold-cache-bound: build rose to
    2353.5 ms over 450 calls (5.23 ms/call), while submit was 228.8 ms
    (0.51 ms/call). Combined driver work improved only ~0.30 ms/call;
    the prior copy had effectively prefetched the compact trusted buffer.
  - Decision: reject/revert direct parsing. Its marginal gain does not
    justify weakening the immutable trusted-record boundary. Phase E must
    reduce the amount of semantic state parsed (or generate a bounded,
    validated stream on PPC), not merely remove the staging copy.
- 2026-08-30 (reusable AltiVec foundation, physical 7410):
  - Added `lib/libminigl_altivec_warpos.a` and public
    `minigl_ppc_altivec.h`: aligned bulk copy, column-major 4x4 multiply,
    and vec4-array transform. Public wrappers require only normal 4-byte
    float alignment, select vector code at 16-byte alignment, and retain
    scalar references/fallbacks for validation and other PPC CPUs.
  - Raw kernels are isolated in `client/minigl_ppc_altivec.s`, use only
    volatile vector registers, and preserve VRSAVE. The hardware probe now
    checks aligned and fallback paths, scalar/vector tolerance, VRSAVE,
    and concurrent WarpOS vector-state retention.
  - 400 MHz 7410 results including wrapper/VRSAVE overhead: mat4 43,291
    scalar vs 12,718 vector ticks (**3.40x**); 256 vec4 transforms 215,356
    scalar vs 75,183 vector ticks (**2.86x**). Two concurrent probes pass.
  - AltiVec frontend consumption is opt-in via
    `make -f Makefile_warpos.vbcc r200-semantic-altivec`; the ordinary
    WarpOS semantic library remains safe on 603e/604e machines. The
    AltiVec semantic smoke completed 120 frames and native bootstrap passed
    afterward on the physical R200 system.
  - The current frontend integration vectorizes only bit-exact 64-byte
    matrix copies. Vector matrix multiplication remains a reusable API but
    is not enabled in record generation until cross-CPU matrix/header
    equivalence is added to the proof. This foundation therefore does not
    claim to reduce the measured 44 ms gears scene stage yet; the next
  material kernels are batched vertex packing and float sin/cos/table
  generation, followed by PPC-native CP-stream generation.
- 2026-08-30 (host-forwarded Intuition events, physical 7410/68060/RV280):
  - Current-speed attribution used the mftb timeline without `-stageprof`;
    the latter still distorts WarpOS because each EClock sample crosses to
    the 68k. At 18.888 fps, active rendering occupied about 21.2 ms/frame,
    followed by a 32.0 ms inter-frame gap containing the PPC-side
    `GetMsg(window->UserPort)` call.
  - An explicit `-noevents` diagnostic raised the same 800x600x32 windowed
    envmap workload to 46.303 fps, proving the 19 fps ceiling was the
    per-frame PPC-to-68k Exec call rather than geometry or GPU throughput.
  - Protocol ring control v4 adds a cache-line-isolated 16-entry Intuition
    event queue and `MGLPPC_CAP_HOST_EVENTS`. Registration explicitly opts in
    and the host acknowledges the request before the PPC disables direct
    polling. The 68k host drains the context window port; PPC clients consume
    events with `MGLPPCPollEvent()` using the existing cache-maintained shared
    control segment. Injected Escape stopped a 1300-frame run at frame 66,
    proving close/key delivery and clean shutdown.
  - Reboot-isolated clean trials (10 warmup + 130 measured, show=1) were
    **46.828 / 46.428 / 46.516 fps, median 46.516**, versus 18.888 before:
    a **2.46x** end-to-end gain. Back-to-back runs without reboot degraded
    to 26 fps because driver submit time rose from about 2.2 to 11.5 ms per
    Execute; those runs are excluded per the physical benchmark protocol.
  - New host attribution at 46.5 fps: roughly 17.6 of 21.5 ms/frame is in
    the ring consumer -- about 4.1 ms pre-submit fence drain, 3.1 ms
    semantic submit, 6.7 ms pre-present fence drain, and 3.1 ms windowed
    present. The PPC application/frontend is no longer the dominant bucket.
  - A full-256-KiB-segment-per-submit experiment reduced 22-frame smoke
    submits from 32 to 22, but regressed 46.6 to **43.448 fps**. Total fence
    time did not fall; it moved from intermediate drains to the present
    drain, while forced segment rotation added latency. Reverted. The next
    material boundary is PPC-generated CP submission, not more batching.
- 2026-08-30 (asynchronous windowed present, physical 7410/68060/RV280):
  - Ring-backed `mglSwitchDisplay()` now posts an ordered PRESENT entry and
    returns without draining that entry on the PPC. The host still drains the
    frame fence and completes the window blit before consuming the next FIFO
    submit. `glFinish`, teardown, segment reuse, ring-full backpressure, and
    non-ring/fullscreen paths remain synchronous.
  - A 120-frame semantic smoke passed. Reboot-isolated 800x600x32 windowed
    envmap trials were **55.504 / 55.587 / 54.702 fps, median 55.504**, a
    **19.3%** gain over the 46.516 host-event median.
  - A 1300-frame run held 55.094 fps. Host attribution over 1905 submits and
    1310 presents averaged about 2.12 ms per CommitBatch, 3.01 ms submit-fence
    wait, 6.87 ms present-fence wait per frame, and 3.29 ms window blit.
- 2026-08-30 (interface-15 bounded state batches, static validation only):
  - Added `RADEON3D_CAP_COMMIT_STATE_BATCH` and an appended `-132` service
    vector. One complete validated HW-TCL state header plus up to 192 bounded
    `{offset, vertexCount}` descriptors replaces the interface-14 record walk
    for homogeneous runs. The driver still owns trusted copies, handle and
    segment validation, semantic translation, CP generation, fences, and
    recovery; no raw packet/register access is exposed.
  - PPC protocol 13 carries a distinct state-batch ring shape. Mixed state,
    primitive, clear, unsupported-driver, and non-streaming cases retain the
    interface-14/legacy paths. Generation mismatch is refreshed and retried
    once by the host.
  - Driver build, GCC/vbcc ABI checks, `r3dstream` build, R200 library build,
    empty `nm -u`, ordinary and AltiVec WarpOS builds, header equality, and
    diff checks pass. Physical PPC validation is pending: the first smoke
    timed out and the bridge became unresponsive, requiring a manual cold
    power-cycle before logs can be collected and the updated binaries tested.
- 2026-08-30 (interface-15 physical validation and ordered-commit prototype):
  - After the cold power-cycle, native bootstrap and Phase 6 primitives passed,
    as did protocol-13 semantic smoke and a 20-frame state-batch gears run.
    A 100-frame attribution run measured 1.51 ms host submit time per Execute,
    down from interface 14's 2.12 ms, but retained about 2.97 ms of pre-submit
    fence wait per Execute.
  - Interface 16 now advertises `RADEON3D_CAP_ORDERED_COMMITS`: immutable
    Commit/CommitStateBatch streams enter the service-owned CP FIFO without an
    intermediate wait when they use distinct vertex segments. Protocol 14 / ring
    control v5 separates host consumption (`completed`) from GPU fence retirement
    (`retired`), and the PPC waits for retirement before reusing segment storage.
    The host drains on segment hazards, inline work, present and teardown; when
    all leased segments are queued, it polls the latest fence to prevent a
    producer/consumer circular wait. Older drivers retain drain-before-submit.
  - Driver ABI/r3dstream, native GCC, WarpOS semantic smoke/gears, header equality,
    `nm -u`, and diff checks pass. Physical interface-16 `r3dstream`, native
    bootstrap, 120-frame protocol smoke, 100-frame gears, and post-run native
    bootstrap all passed without a crash. The short gears run reached 52.929 fps;
    pre-submit wait fell to 0.83 ms/Execute, but host submit rose to 3.23 ms/Execute.
    This is not yet an accepted performance win versus the reboot-isolated
    55.504 fps baseline; it needs isolated trials and a commit-heavy acceptance
    workload before deciding whether to keep or revise the queue depth.
- 2026-08-30 (Phase 1 emitter extraction + full hardware verification and
  submission-shape ladder, physical 68060/RV280):
  - Reconciled the abandoned review session's work into both trees: the
    interface-15 state-batch client, the interface-16 ordered-commit/retired
    groundwork, AltiVec kernels, and the fence-polling host are now committed
    (minigl_ppc a65549f..9a1fbd9, p96-driver be435b4..a755fe0). Two builds
    were clean before any hardware work.
  - Phase 1 of the CP-stream program landed: the entire R200 CP emitter
    (state cache, register/matrix/vector emission, draw/clear/vbuf builders,
    stream walker, target validators) moved into dual-target
    `p96-driver/src/radeon3d_emit.c` + `include/radeon3d_emit.h` (1,952
    lines; service 4,142 -> 1,847). Surfaces became value descriptors with
    a Resolve callback hook; no SysBase/locking/IO in the module. The
    transform was diff-verified: only device->emitter, pointer->value state
    fills, and InterfaceVersion/FailStage redirection changed. State
    comparison moved from handle identity to descriptor content, which
    cannot change emitted bytes.
  - Hardware correctness on the extracted-emitter driver (CRC 29117CB2)
    with the state-batch library (CRC 3AB62EA9): native bootstrap smoke on
    every boot (5/5), protocol-14 ring semantic smoke 120 frames PASS,
    Phase 5 full suite PASS (image/subimage/mip/wrap/env/blend/alpha/
    stress), Phase 6 primitives PASS, Phase 6 acceptance PASS, cull/shade
    PASS. A standalone interface-13 r3dinfo client negotiates against the
    interface-16 driver, confirming backward compatibility.
  - Phase 5's `asyncSubmits==2` assertion failed with async=3 for BOTH the
    old and new client libraries on the new driver: the residency engine
    splits the final flush depending on when the driver makes the previous
    fence visible (phase6acc waits 54->48, polls 11->17 shifted the same
    way). Pixels and the deferred/barrier counters are identical. The
    assertion was relaxed to `>=2` with the ordering gates kept
    (minigl_ppc 9a1fbd9); the exact count is timing-sensitive, not an
    ordering contract.
  - Submission-shape ladder (reboot-isolated 800x600x32 windowed envmap,
    10 warmup + 300 measured, one InitPPC + background ping each):
    | Configuration | fps | driver copy/build/submit ms/call |
    |---|---:|---|
    | state-batch + ordered commits (iface-16) | **52.950 / 52.374 / 52.456, median 52.456** | 0.16 / 2.22 / 0.36 |
    | state-batch + drain-before-submit | 47.845 | 0.18 / 0.67 / 0.36 |
    | inline-Execute fallback + drain | 39.621 | 2.07 / 3.36 / 0.55 |
    - The state-batch build bucket grows under ordered commits because
      `RadeonPrepare3D` blocks on a busy GPU and its wait lands in the
      build timer; the drain config shows the same cost in the wait
      bucket instead. Both host-bound; ordered commits win by rebalancing.
  - The async-present entry's 55.504 baseline is not reproducible with the
    reconciled client: it was measured with a client-side commit-chain
    shape (1.47 submits/frame) that the state-batch rework removed. The
    reconciled client's cap-off fallback is inline Execute (1,902 record
    dwords/call), not the commit chain, so a true commit-chain A/B is no
    longer possible without client work.
  - Decision: interface-16 (state batches + ordered commits) is the
    accepted default; caps re-advertised (p96-driver a755fe0) and the
    machine left on that driver. Next wins, in order: (1) client-side
    state grouping to reduce ~3.5 batches/frame toward ~1.5 (each batch
    re-emits a full TCL header; ~7.8 ms/frame of 68k build time is
    currently hidden behind the 19 ms frame), (2) Phase 2 of the CP-stream
    program (PPC-side emission with the relocated emitter), which the
    extraction enables.
- 2026-08-30 (Phase 2 CP-stream program: implemented, measured, REJECTED):
  - PPC-side PM4 emission with validated client streams (Radeon3D interface
    17) was implemented and physically validated end to end (smoke, emitter
    proofs, 1350 streams, zero rejects, all native suites). Performance gate
    failed: 800x600x32 windowed envmap gears measured 46.8 FPS median
    (direct stream) and 43.5 (same-build state-batch control) vs the
    accepted 52.456 interface-16 baseline. Stage counters attributed the
    submit bucket to the per-submit GPU drain, validation, and copies.
  - A CP-to-CP prepare skip (Radeon3DSubmitStream no longer drains between
    back-to-back 3D submits) recovered +3.9% on the stream configuration
    but measured NEUTRAL on the accepted interface-16 baseline
    (51.540 vs 51.690 FPS single runs), because the per-frame present blit
    already forces the drain there.
  - Decision: interface-16 state batches remain the default. The complete
    Phase 2 work is archived on the `phase2-cp-stream` branches of
    minigl_ppc and p96-driver with full measurement records. The prepare
    skip is retained on the driver mainline as a validated, semantically
    correct no-op for the current workload.
