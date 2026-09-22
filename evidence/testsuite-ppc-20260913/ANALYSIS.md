# Room-walk vs gears performance attribution (physical Amiga, 2026-09-13)

Setup: same boot, same profiler stack (`Work:ibdraw/profts`, prof DLL CRC
FA60C334, prof host bits 00020000), fullscreen 640x480x16, MGLPPC_HOSTPRI=-1.

| Workload | FPS | ms/frame |
| --- | ---: | ---: |
| gears precalc, 600 frames, quiet stack (baseline this session) | 135.1 | 7.4 |
| gears precalc, 600 frames, profiler stack | 129.9 | 7.7 |
| suite quake.world-room-walk, 120 frames, quiet stack | 12.0 | 83.3 |
| suite quake.world-room-walk, 120 frames, profiler stack | 11.8 | 85.0 |

Profiler overhead is small; the two stacks agree.

## The comparison that explains both numbers

| Per-frame cost driver | gears | room-walk |
| --- | ---: | ---: |
| PPC publishes (ring) | 4.4 | 14.1 |
| 68k executes (ring) | 3.4 | 13.1 |
| PPC semantic control msgs (PPC_CONTROL) | **0.01** | **33.2** |
| PPC yields (PPC_YIELD) | **0.06** | **11.2** |
| 68k mailbox messages | ~0 | 33.0 |
| 68k per-execute ring cost (ring_us/executes) | 2.2 ms | 3.4 ms |
| 68k per-mailbox-message cost | ~0.5 ms (3 msgs) | 5.8 ms avg |
| 68k host busy during session | 34% | ~90% during render |

Frame time in both cases is dominated by 68k-side per-message cost times the
number of semantic messages per frame:

- gears: 3.4 executes x ~2.2 ms = ~7.5 ms/frame -> 130 fps.
- room-walk: 13.1 executes x ~3.4 ms + 33 control msgs x ~2.3 ms = ~110 ms
  of 68k work per 85 ms frame (partially overlapped) -> 12 fps.

Gears is fast NOT because the GPU or the PPC are fast, but because the
precalculated client issues ~10x fewer semantic messages per frame. The 68k
host is 66% idle during gears and saturated during room-walk. The 68k host is
"sufficient" exactly as long as the per-frame message count stays near
gears' ~4.

## PPC side is not the problem

Room-walk PPC frontend CPU: serialize 1.6 ms + draw-record build 4.7 ms +
publish ~0.5 ms = ~7 ms/frame, fully overlapped. PPC_CONTROL accounts for
95% of the client's lifetime (878.5M of 925.4M ticks) - the PPC is blocked
waiting for the 68k, not computing. Publish stalls are negligible
(93,906 ticks total; the 131,072-iteration spin budget from the yield fix
works on the ring path).

## The yield pathology from docs/reference/HOST_LOADER_REGRESSION.md is still active on the control path

Room-walk: PPC_YIELD calls=1450 (11.2/frame), average actual yield
500,660 ticks = **15.07 ms** per yield (requested 100 us; max observed
501,875 ticks). Gears: 37 yields per 600 frames. The documented
WaitTime(0,100) ~20 ms actual quantum still parks the PPC on every control
round-trip whose 131,072-iteration spin budget expires. This is a symptom,
not the root cause: the 68k genuinely needs the time to process the message.
Spinning longer would only burn PCI/double-clock; the fix is fewer messages.

## GPU is not the bottleneck

| Diagnostic | FPS |
| --- | ---: |
| room-walk baseline | 12.0 |
| --room-nodepth (removes depth test, adds overdraw) | 10.5 |
| --room-zprepass (reduces fragment work) | 11.2 |

Both variants shift fps by <15% despite large GPU-work changes - and both
ADD draw records/submits, which adds 68k message cost. Fragment/depth
throughput is not the limiter. Texture residency is irrelevant (uploads
31 calls / 321 KB once, residency ticks 0 during measurement).

## Improvement options (ranked by expected gain)

1. **Move per-frame state changes into the command stream.** ~33 control
   mailbox round-trips/frame (texture binds for the texture-chained batches,
   matrix and state changes) each cost ~2.3 ms of 68k time. Encoding them as
   record-level commands in the ring (like gears' precalculated geometry)
   removes ~50-60 ms/frame -> roughly 2-3x fps on its own.
2. **Merge submits.** 20 draw records become 13 submits (1.5 records/submit,
   merges=0). Coalescing to ~4-5 executes/frame (gears-level) saves
   ~9 x 3.4 ms ~ 30 ms/frame.
3. **Cheaper 68k ring handler.** 2.2-3.4 ms per execute is the floor that
   caps even gears at ~130 fps. Profiling the handler (endian copy is
   already PPC-side) or batching multiple ring entries per Radeon3DCommitBatch.
4. **Present (24 ms/frame on 68k for room-walk).** Mostly end-of-frame fence
   for queued GPU work; improves automatically once 1-2 shrink the queue.
5. NOT worth it: GPU-side fragment/depth optimizations (zprepass showed no
   gain), texture residency, PPC frontend micro-optimization.

Realistic target after 1+2: ~4 executes + <5 control msgs per frame =
~23 ms/frame = 35-45 fps (~3x).

## Session defects found along the way

- Per-test runs that start a fresh host on a fixed 0.5-2 s Delay after client
  exit intermittently attach against stale host/SMR state: every Execute is
  then rejected client-side (exec_fail == submits, submitted=0, black
  readbacks) for the whole session. Host-restart verification (port check) or
  an epoch handshake reset is needed. Reproduced 18/18 runs in script mode,
  0/3 with verified manual host start.
- quake.particles-mixed wedged the machine once (run-all mode, 120 frames,
  quiet stack; T: log shows the capture step starting). Repro attempt deferred;
  requires cold power cycle per recovery rules.
- depth.less FAILs deterministically when healthy (pixel reference mismatch);
  room-walk verify FAILs with same-hue-but-brighter samples
  (want 69,53,34 got 88,78,66). Real correctness findings, uninvestigated.

## Deployment record

- Build: commit 9b8fa12 + dirty tree (branch fix/r200-glquake-texture-defects)
- Suite: bin/examples/minigl_testsuite_ppc.warpelf CRC 9BE36E10 ->
  Work:ibdraw/mgltestsuite CRC F9B0C429 (786,752 bytes)
- Profiler DLL: Work:ibdraw/profts/minigl_ppc.dll CRC FA60C334 (309,492 bytes)
- Profiler host: Work:ibdraw/profts/mglhost_prof, prot 00020000
- Baseline precalc this session: 135.135 fps, error=0
- Evidence logs in this directory (pulled, CRC-verified).
