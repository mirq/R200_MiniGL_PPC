# PPC CP Emitter

Opt-in: `SetEnv MGLPPC_CPEMIT 1` before starting the client. Ordinary rendering
is unchanged when unset. This is separate from `MGLPPC_IBSMOKE`; do not enable
both. Currently limited to windowed contexts with stable render targets.

## Ownership

The PPC DLL links the same `src/radeon3d_emit.c` as the chip, using
`R200_EMITTER_ROOT` (default sibling `../p96-driver`). The adapter supports
inline semantic records, streamed commits and homogeneous state batches.
It resets emitter state per submission, not per draw, and does not change the
driver's register programming. Vertices retain the existing streaming path.

Protocol v15 remains unchanged; CP_EMIT is explicitly requested and echoed at
registration. It requires live INDIRECT_DISPATCH and INDIRECT_RENDER caps;
an opted-in client rejects missing support instead of silently benchmarking a
semantic fallback. The host leases one 256 KiB CP segment and four vertex
segments from the context's existing device. Vertex reply tuples include the
driver's GPU address. No second device or direct PPC MMIO/CP-ring access exists.

Each of eight FIFO positions owns a separate 32 KiB CP slice. The PPC stages
host-native words, pads odd lengths with PACKET2 (classic Linux Radeon indirect
dispatch requires an even dword count), then swaps once and flushes before
publishing `[byteOffset, dwordCount, vertexSegmentMask]`. A slice is reused only
after **retired**, not **completed**. The host retires its previous fence before
dispatching another, retains vertex-use masks, and performs ordinary presents.
`CP_RUN dispatches=... dwords=... failed=...` is printed once at shutdown and
distinguishes real CP submissions from the old startup smoke.

Native private query `0x4d475013` returns only context-owned, live surface
descriptors after service-generation checks, without recovery or residency
changes. The PPC caches 16 descriptors, invalidated on resource/lifecycle
commands; binding alone does not replace residency. There is no per-frame
metadata query for gears' unchanged textures. Generic scalar GET_INTEGER
rejects the private descriptor namespace to prevent descriptor writes into a
four-byte destination.

CP failures retain storage and reject further work. Host teardown rejects
unconsumed entries, failed retirement and terminal CP errors. `glFinish` waits
for actual CP retirement, including a final unpresented clear/inline draw.
These are trusted packets: extent validation does not sandbox their register
or VRAM references.

## Validation Status

Implemented and cross-built on 2026-09-10. Hardware triangle readback and all
7,127 texture-update checks passed on the CP path, with positive CP submission
counts and normal teardown. The subsequent loader/pacing investigation is in
`HOST_LOADER_REGRESSION.md`; it supersedes the original performance assumptions.
Local checks: all 11 Python test scripts and six regression modes pass. The
real emitter adapter has 15 ASan/UBSan tests (including 4,000 mutations), the
surface bridge has nine, and the driver dispatch harness has 79 cases. The
host, adapter, shared PPC emitter and chip compile without new warnings;
pre-existing native/frontend and rebuilt-card warnings remain.

Quiet FPS uses `GEARS_RUN` with `MGLPPC_PROFILE=0`, client/host traces compiled
out, and verbose/upload/IB-smoke diagnostics unset. Existing optional fine-grain
transport/host profile subdivisions do not yet account for CP-specific
emit/wait/submit work; do not use those buckets to attribute this path. Wall
time and shutdown CP counts remain the benchmark evidence.

## Build And Baseline

The corresponding driver implementation is committed in `p96-driver` at
`6fd98ef` (interface 18). `R200_EMITTER_ROOT` must resolve to that compatible
driver tree; the shared emitter is compiled from its source, not an old object.

Initial artifacts below are from the 2026-09-10 bring-up. For the final
classified-host and bounded-polling artifacts/results, use
`HOST_LOADER_REGRESSION.md` (current quiet DLL CRC 02605851).

Initial isolated artifacts: `/tmp/opencode/ibdraw`; target staging: `DH2:ibdraw`.

| Artifact | Target CRC32 | Bytes |
| --- | --- | ---: |
| Chip | `CEB838A6` | 77940 |
| Card, preserved known-good baseline | `18A453D6` | 7796 |
| Native MiniGL | `62343673` | 127252 |
| Host | `F98CB3DF` | 40564 |
| PPC WarpELF | `953151AF` | 361421 |
| Target-converted DLL | `77239FB3` | 295680 |
| Triangle with pixel checks | `C6F60689` | 89456 |
| Texture-update acceptance client | `F829BE0E` | 97248 |

The freshly rebuilt card (`441CBE1C`) is archived as `DH2:ibdraw/cardbuild`,
not selected for deployment: preserve the known-good card binary rather than
introducing a second artifact change. Both components are still replaced as a
matched pair, with both active files backed up before installation.

The unchanged semantic stack produced **52.264 FPS** in one operator-rebooted
600-frame run, 800x600x32 windowed, envmap, sync off, ordinary (not precalculated)
geometry, error zero. Host priority -1; 2,031 executes and 600 presents. It is a
single-sample reference, not a three-cold-run median. Logs are preserved under
`/tmp/opencode/ibdraw/bench/{sample1,host1,health1}.log`; original matched
artifacts remain in `DH2:ibsafe` and the baseline client in `DH2:ibbench`.

## Phase 3: fullscreen enablement (design captured 2026-09-16)

Prerequisite verified on the installed stack (quiet DLL `32216D6B`, host
`85F1A513`, chip `CEB838A6`): windowed `precalc 300f 800x600x32 -nosync`
ran **68.493 fps, error=0** with `CP_RUN dispatches=1016 dwords=255920
failed=0`. The live driver supports trusted indirect dispatch; no driver
deployment is required (an `r3dinfo` report of interface 17 is its
negotiated request, not the chip version).

### Implementation: live color target (ring control v6)

The live color-target steps are implemented:

- `MGLPPCRingControlV1` (ring control v6) appends `colorToken`/`colorSeq`;
  the existing compile-time size/offset asserts cover the new pair, which
  sits 32-byte aligned at the end of the block (456 bytes / 114 dwords).
- The host publishes the live token at CP registration and after every
  `mglSwitchDisplay()` in the PRESENT path (`PublishCpColorTarget`); it
  writes the token first, then the incremented sequence, with `CacheClearE`.
  `state->cpColorToken` follows every publication. The `ExecuteCpEntry`
  comparison is demoted to a one-shot `cp_token_mismatch` report and never
  latches `cpFailed`.
- The frontend compares `colorSeq` before each CP batch in `PublishCpSlot`
  (invalidating the line first). On a move it stamps the new token into the
  record target dwords with exactly the host's `PatchRecordTargets` walk;
  `ResolveCpSurface` then fetches the descriptor through
  `MGLPPC_CMD_CP_SURFACE`. Rotation between two or three screen buffers
  settles in the 16-entry descriptor cache after the first cycle.
- `!state->contextWindowed` is gone from the host registration gate.

The record target dword is written on the PPC side in three places:
`CapturePendingDrawState` (`r200_state.c:305-306`, `header[2]`),
`Lib_glClear` (`r200_surface.c:496`, `record[2]`) and the post-present
mirror patch (`r200_surface.c:1615-1620`); `FlushScreenVertices` copies
those headers into `slot->records` (`r200_geometry.c:436-437` state batch,
`:543-544` plain). The v6 stamp overrides the target at CP publish time,
which is where the emitted CP stream bakes address/pitch/format.

Measured this session (quiet DLL `9E44E240` + host, three independent
boots, diagnostics off, identical shader/dword counts every run). First on
the old chip `CEB838A6`, then on the new chip `F46026B4` with the
capability-gated pipeline host:

| Pair | windowed 300f | fullscreen 600f |
| --- | ---: | ---: |
| host `32716DD9`, chip `CEB838A6` (single fence) | 68.181 / 68.493 / 68.807, **median 68.493** | 172.413 / 174.418 / 179.640, **median 174.418** |
| host `FACF6170`, chip `F46026B4` (pipelined) | 69.124 / 69.444 / 69.444, **median 69.444** | 187.500 / 182.926 / 181.818, **median 182.926** |

Every run `error=0`, `CP_RUN ... failed=0` (`1016/255920` windowed,
`2031/511620` fullscreen). The semantic reference is 133.928 fps
fullscreen; CP adds **+30%** single-fence and **+37%** pipelined.

- `r200_triangle_smoke 16 fullscreen` with CP: pixels pass; CP and semantic
  readbacks are byte-identical (`center=115,145,164 corner=8,12,24`), on
  both drivers.
- `r200_texture_update_test` (windowed, CP): `CHECKS=3495 FAILURES=1`, the
  pre-existing `INDEXED SOURCE` expectation; `CP_RUN dispatches=24
  dwords=3024 failed=0` on both drivers.
- Silent Hill (fullscreen, quiet DLL + host): 1,422 presents,
  `executes=5972`, no host errors, clean quit. The game's 4-6 fps outdoor
  segments are its own CPU load (median 205 ms frame: logic 100 + raster
  19); MiniGL's share is `present` 28 ms (game software scaling) plus
  `endscene` 2.4 ms. FMV intervals ran 15-16 fps.

Host-CPU attribution (`CP_PROF`, 600 fullscreen frames): the per-dispatch
fence wait fell from **1482 ms to 28 ms** when the pipeline is active; the
CP-entry total fell 1912 -> 623 ms. End-to-end the frame is now bound by
the present blit and CP/fence latency (`presentwait` 1.19 s, `present`
0.77 s over the same run), not by the host's serialization.

Known limitation: with `MINIGL_PRESENT=FLIP` the native drawing buffer
rotates on the host, and a CP batch emitted after the present but before the
PPC observes the new `colorSeq` can still carry the previous target. The
host reports `cp_token_mismatch` once and keeps the session alive. BLIT
presentation (the default with sync off and the validated path) has a stable
drawing target and is unaffected.

### Step 5 (N in-flight): implemented with a driver fence-range change

An N-in-flight host first failed on hardware because
`Radeon3DTestFence`/`Radeon3DWaitFence` (`radeon3d_service.c:2094-2129`)
route through `IsSessionFence` (`:2077-2092`), which only accepted
`active->LastFence == fence`; every submission overwrites `LastFence`
(`:1081`, `:1265`, `:1624`, `:1899`, `:2063`), so an older fence could
never be retired (`present_drain_fail fence=18977`).

The driver now accepts any fence in `(0, active->LastFence]`
(wrap-tolerant, monotonic per session) and advertises
`RADEON3D_CAP_MULTI_FENCE` (bit 28, driver commit `8b54b76`). The host
(`minigl_ppc_host.c`) enables the fence queue only when the cap is present:
one submission per ring slot in flight, non-blocking retirement of the
contiguous prefix in `PollQueuedFence`, `control->retired` advanced only
over completed fences, and a full drain before PRESENT/teardown. Without
the cap (old driver) the host keeps the validated drain-before-dispatch
model unchanged, so the DLL/host pair still runs on the old chip.

### Original design notes

Blockers for fullscreen, with exact sites:

- Host rejects CP for non-windowed contexts: `minigl_ppc_host.c:905-909`
  (`!state->contextWindowed`).
- Windowed works only because the target is `surfaces[0]` forever.
  Fullscreen rotates `surfaces[displayed/drawing]` per present, while:
  - the host freezes `state->cpColorToken` at registration (`:1066`) and
    `ExecuteCpEntry` fails terminally when the live bridge token differs
    (`:1307-1312` -> `cpFailed`);
  - the PPC target token comes from the CREATE_CONTEXT reply
    (`minigl_r200_warpos_compile_probe.c:2517-2518` sets
    `proofContext.surfaces[0].Handle`), valid for windowed only;
  - `ResolveCpSurface` (`:1718-1749`) resolves whatever token the record
    carries and caches 16 descriptors keyed by token; PRESENT never
    invalidates that cache;
  - the emitted CP stream bakes the target address/pitch/format, so a
    stale target descriptor produces a wrong stream, not just a delay.

Implementation plan:

1. Publish the live color token: append `colorToken` + `colorSeq` to
   `MGLPPCRingControlV1` (version bump 5 -> 6, update the compile-time
   size/offset asserts in `include/libraries/minigl_ppc_protocol.h`).
   Host writes it after `mglSwitchDisplay()` in the PRESENT path
   (`minigl_ppc_host.c:1846-1881`) and once at registration from
   `QueryBackendBridgeV2()`/`QueryCpSurface()`.
2. Frontend: before building each CP batch, compare `colorSeq`; on change
   stamp the new token into the record target field (the dword the host
   otherwise patches via `PatchRecordTargets`) and let `ResolveCpSurface`
   fetch the descriptor through `MGLPPC_CMD_CP_SURFACE`. Two or three
   screen buffers cache after the first rotation cycle.
3. Host: refresh `cpColorToken` per present; drop `!contextWindowed` from
   `:905-909`; keep the `ExecuteCpEntry` token comparison as a
   post-refresh sanity check, not a terminal failure.
4. Validation order: windowed regression (68.5 fps, `failed=0`), then
   fullscreen gears `-depth 16 -buffers 3 -nosync` against the semantic
   **133.9 fps** reference with the envmap pixels compared, then
   present/teardown stress (`glFinish`, context delete) and the game FMV.
5. Only after that: N-in-flight dispatches. Replace the blocking
   `DrainSemanticFence` in `ExecuteCpEntry` (`:1305-1307`) with a
   per-slice fence/mask table (8 slices), advance `control->retired` over
   the contiguous retired prefix using non-blocking
   `Radeon3DTestFence` in `PollQueuedFence`, and join the vertex-segment
   reuse mask against the union of in-flight masks. PRESENT and teardown
   must drain all in-flight fences before `mglSwitchDisplay`/lease free.
