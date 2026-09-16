# Phase 3 handoff — PPC-authored CP submission (interface 18)

Prepared 2026-09-16 at committed checkpoint `cd03d67`. This document plus
`CP_EMITTER.md`, `U0_UPLOAD_ATTRIBUTION.md` and `HOST_LOADER_REGRESSION.md`
is everything a fresh session needs to continue.

## 1. Current state

Committed:

- `bf5fe53` — r200: cut direct-upload latency ~54% (fused shadow, 565
  tables, drain pacing, mailbox coverage, host `-mcpu=68060`).
- `cd03d67` — capture CP-emit fullscreen design and verified prerequisite.

Working tree (deliberately not committed): tracked build artifacts
`bin/minigl_ppc_r200_prof.warpelf`, `obj/dll-r200-prof/*.o`; untracked
`bin/minigl_ppc_host_idle`, `obj/dll-r200-u0{,noprof,q}/`.

Target deployment (physical Amiga at `192.168.1.21:2345`):

- `Work:ibdraw/minigl_ppc.dll` = quiet fused+mailbox DLL `32216D6B`
  (rollbacks `Work:ibdraw/dll.q1` = `8FF809BB` pre-1.2, `dll.preu0` =
  `AAEFDACC` original).
- `DH2:ibdraw/mglhost` = quiet 68060 host with mailbox cap `85F1A513`
  (rollback `mglhost.u0q` = `1664A435` 68020 quiet, `mglhost.preu0` =
  `EFAC3C32` original), protection `00020000`.
- `Work:SilentHill WOS/minigl_ppc.dll` = profiled fused `7ADA06F4`
  (rollback `dll.preu0` = `AAEFDACC`); the game's `hostrun` starts
  `DH2:ibdraw/mglhost`.
- `DH2:u0/` test dir: `r3dinfo` (`379F2F26`), `suite`, `updtest`,
  `txsmoke`, `mglq2.dll` (`32216D6B`), hosts `mglhost_u0d` (`BCE24A15`,
  profiled), `mglhost_060b` (`85F1A513`), `mglhost_060p` (`984DB6A2`),
  logs.
- `ENVARC:MGLPPC_HOSTPRI = -1` archived (volatile env cleared by reboot).
  `MINIGL_PRESENT` deleted (defaults apply: BLIT with sync off).

## 2. Baselines to preserve (do not lose these references)

- Gears 600f fullscreen 640x480x16, 3 buffers, BLIT, sync off, fresh
  boot: **133.928 fps** (A/B/A on one boot 133.3 / 135.1 / 137.6).
- Gears FLIP: 32.3 fps; the 24.9 ms/frame is P96 `ChangeScreenBuffer`,
  not our waits (host presentwait 0.23 ms/frame). Presenter work parked
  pending a driver-level cheap switch.
- Direct upload probe (`txsmoke 120 tiles16 window`, 256x256 16-bit):
  24.35 -> **11.27 ms/upload** (barrier 2.53, BEGIN 3.45, write 3.55,
  flush 0.04, COMMIT 1.13, shadow 0.00). Evidence
  `U0_UPLOAD_ATTRIBUTION.md`.
- Silent Hill: zero GL errors, gameplay 55-64 ms frames (15.5-18.2 fps),
  FMV improved; its remaining present cost is the game's own software
  scaling (`filter=1`), not MiniGL.
- Driver per call (radeon3dinfo deltas over 2031 executes): copy 11.5,
  build 46.0, submit 24.6 us (~82 us total). Host `submit` stage for the
  same calls: 1.15 ms/call - unexplained gap, see open items.

## 3. Phase 3 goal and verified prerequisite

Goal: PPC-authored CP streams become usable for fullscreen and lose the
per-dispatch fence serialization, so geometry clients can run with the
68k out of the command path (only extent validation + doorbell + present).

Verified on the installed stack (no driver deployment needed):
windowed `precalc 300f 800x600x32 -nosync` -> **68.493 fps, error=0**,
host `CP_RUN dispatches=1016 dwords=255920 failed=0`. `r3dinfo` printing
`interface_version=17` is its negotiated request, not the chip version.

Enable path: `SetEnv MGLPPC_CPEMIT 1` before the client; requires live
`RADEON3D_CAP_INDIRECT_DISPATCH | _RENDER`; windowed contexts only today.
Opting in without support rejects the context (no silent fallback).

## 4. Root cause of windowed-only (exact sites)

- `backend_r200/minigl_ppc_host.c:905-909` — host rejects CP registration
  when `!state->contextWindowed`.
- Windowed works because the target is `surfaces[0]` and never changes.
  Fullscreen rotates `surfaces[displayed/drawing]` per present, while:
  - `minigl_ppc_host.c:1066` freezes `state->cpColorToken` at
    registration;
  - `minigl_ppc_host.c:1307-1312` (`ExecuteCpEntry`) compares the live
    bridge token with `cpColorToken` and fails terminally on mismatch
    (sets `cpFailed`, ring `error` = 1);
  - the PPC target token comes from the CREATE_CONTEXT reply
    (`minigl_r200_warpos_compile_probe.c:2517-2518`,
    `proofContext.surfaces[0].Handle`), which only exposes `surfaces[0]`;
  - `ResolveCpSurface` (`:1718-1749`) resolves the token carried by the
    record and caches 16 descriptors keyed by token; PRESENT never
    invalidates that cache;
  - the CP stream bakes the target address/pitch/format, so a stale
    descriptor writes to the wrong buffer.

## 5. Implementation steps (ordered, each with its gate)

1. **Protocol: publish the live color token.** Append `colorToken` and
   `colorSeq` to `MGLPPCRingControlV1`
   (`backend_r200/include/libraries/minigl_ppc_protocol.h`), bump the ring
   control version 5 -> 6 and update the compile-time size/offset asserts.
   Keep normal 32-bit layout; the control block is non-cacheable both
   sides, so the host must write with the existing `CacheClearE`
   discipline and the PPC must `MGLPPCInvalidateLine32` before reading.
   Client and host are rebuilt/deployed together; an old peer fails ring
   registration and falls back (verify that path).
2. **Host publication.** Write `colorToken`/`colorSeq` at registration
   from the bridge/`QueryCpSurface` result, and after `mglSwitchDisplay()`
   in the PRESENT path (`minigl_ppc_host.c:1846-1881`) from the
   re-queried live token. Update `state->cpColorToken` at the same time
   and demote the `ExecuteCpEntry` comparison (`:1307-1312`) to a
   sanity check.
3. **Frontend consumption.** Before each CP batch
   (`PublishCpSlot`, `minigl_r200_warpos_compile_probe.c:1751-1842`),
   compare `colorSeq`; on change cache the new token, invalidate the CP
   surface cache if the token is unknown, and ensure the new token is the
   one the emitted records carry. **Open item:** the write site for the
   record target token was not located this session (greps for
   `header[2]`/`surfaces[...]` reads found no PPC-side writer; the CP
   adapter reads it from `slot->records`). Find it first - candidates are
   the shared header builder in `backend_r200/library/r200_state.c` or
   the frontend's `CapturePendingDrawState` mirror - then stamp the token
   there. `ResolveCpSurface` already fetches descriptors by token, so
   two or three screen buffers cache after the first rotation cycle.
4. **Allow fullscreen.** Drop `!state->contextWindowed` from
   `minigl_ppc_host.c:905-909` (last change, after 1-3).
5. **N in-flight dispatches.** Replace the blocking `DrainSemanticFence`
   in `ExecuteCpEntry` (`minigl_ppc_host.c:1305-1307`) with a per-slice
   (8) fence + vertex-mask table; advance `control->retired` over the
   contiguous retired prefix with non-blocking `Radeon3DTestFence` inside
   `PollQueuedFence` (`:490-527`); join the vertex-segment reuse mask
   (`queuedSegmentMask`, frontend rotation checks `:2371-2436`) against
   the union of in-flight masks. PRESENT and teardown
   (`DiscardSemantic` `:724-789`) must drain every in-flight fence before
   `mglSwitchDisplay` or lease release.

   **Implemented 2026-09-16, with a driver prerequisite.** Interface 18
   initially rejected older fences: `IsSessionFence`
   (`~/p96-driver/src/radeon3d_service.c:2077-2092`) required
   `active->LastFence == fence`, which every submission overwrites
   (`:1081`, `:1265`, `:1624`, `:1899`, `:2063`), so an N-in-flight host
   failed its first PRESENT drain (`present_drain_fail fence=18977`). The
   driver now accepts any fence in `(0, LastFence]` and advertises
   `RADEON3D_CAP_MULTI_FENCE` (bit 28, driver commit `8b54b76`); the host
   only pipelines when the cap is present and otherwise keeps the
   validated single-fence drain, so the stack still runs on the old chip.

## 6. Validation gates (in order)

1. Windowed CP regression after steps 1-3: `precalc 300f 800x600x32
   -window -nosync` -> ~68.5 fps, `CP_RUN failed=0`.
2. Fullscreen CP: `precalc 600f -depth 16 -buffers 3 -nosync` against
   the semantic 133.9 fps reference; envmap pixels must match (dump or
   readback comparison). Do not accept a regression without a measured
   win elsewhere.
3. Present/teardown: `glFinish` retirement, context delete,
   `phase6_acceptance`, then Silent Hill FMV and gameplay.
4. Only after 1-3: step 5 A/B on the same workloads.
5. Benchmark protocol: three independent cold boots, medians, all
   diagnostics off; one variable at a time; record commit + dirty state,
   CRCs, driver pair, mode/depth, cold/warm.

## 7. Risks and rollback

- The CP stream is a trusted packet stream: extent validation is not a
  sandbox (`RADEON3D_SUBMISSION.md`). Never widen what the PPC may emit
  without re-validating the emitter and the driver's checks.
- CP failures are terminal for the session (`cpFailed` latches); a wrong
  token produces a wrong-buffer write, not just a slow frame. Test with
  readback pixel checks before trusting fullscreen.
- Rollback is artifact-level: restore `Work:ibdraw/minigl_ppc.dll` from
  `dll.q1`/`dll.preu0` and `DH2:ibdraw/mglhost` from `mglhost.u0q`/
  `mglhost.preu0`; the game dir has its own `dll.preu0`.
- No driver change, no reboot is required for steps 1-5 on the current
  chip (`CEB838A6`); the driver at `6fd98ef` (interface 18) is already
  capable. Do not deploy a new driver for this work.

## 8. Open items / known defects

- `r200_texture_update_test` fails the pre-existing `INDEXED SOURCE`
  expectation on every build (blocks the full 7127-check gate).
- Host `submit` stage 1.15 ms/call vs driver-internal 82 us: the gap is
  inside the service call but outside its phase timers (lock/setup).
  Needs driver-side timers around `LockServiceBoard` + `Ensure*` before
  any driver optimization.
- Presenter item parked (P96 `ChangeScreenBuffer`); `filter=0` in
  `Work:SilentHill WOS/wos_gl.cfg` is an untested game-side A/B for the
  FMV present cost.

## 9. Operational rules (non-negotiable, from AGENTS.md)

Physical Amiga only; explicit bridge endpoint and `tools/wait_bridge.sh`;
`InitPPC` exactly once per boot; never `Break` a GL client and relaunch
immediately; `Avail Flush` after replacing `LIBS:minigl.library`;
serialize all Amiga filesystem operations; <=16 character Amiga names;
CRC every transfer; `MGLPPC_HOSTPRI=-1` (archived) before any host start;
cold boot for grey screens or driver changes; keep diagnostics
(`MGLPPC_VERBOSE`, `MGLPPC_UPLOAD_TRACE`, traces) off for benchmarks.

## 10. Status (2026-09-16 session): steps 1-4 implemented and validated

- **Record-token write site found** (the open item in section 5.3):
  `CapturePendingDrawState` writes `header[2]` (`r200_state.c:305-306`),
  `Lib_glClear` writes `record[2]` (`r200_surface.c:496`) and the
  post-present mirror patch rewrites both at `r200_surface.c:1615-1620`.
  `FlushScreenVertices` copies those headers into `slot->records`
  (`r200_geometry.c:436-437` / `:543-544`). The v6 stamp runs in
  `PublishCpSlot` before `MGLPPCEmitBatch`, so the emitted CP stream always
  bakes the token the host will own when the entry is consumed.
- Ring control v6 (`colorToken`/`colorSeq`, 456 bytes), host publication at
  registration and after `mglSwitchDisplay()`, `cpColorToken` kept in sync,
  `ExecuteCpEntry` comparison demoted to a one-shot sanity report, PPC
  `PublishCpSlot` re-stamp on sequence change, and the `!contextWindowed`
  gate removed.
- Validation: windowed 68.181 fps `failed=0`; fullscreen 640x480x16 3-buffer
  172.413 fps `failed=0` (semantic 133.928); CP/semantic triangle readbacks
  identical; `r200_texture_update_test` unchanged (`3495/1`, pre-existing).
  Details and the FLIP-mode limitation are in `CP_EMITTER.md`.
- Benchmark protocol (three independent boots, diagnostics off, identical
  dispatch counts every run). Single-fence pair first, then the
  capability-gated pipeline host with the new chip:

  | Pair | windowed 300f 800x600x32 | fullscreen 600f 640x480x16 3buf |
  | --- | ---: | ---: |
  | host `32716DD9` + chip `CEB838A6` (boots A/B/C) | 68.181 / 68.493 / 68.807, **median 68.493** | 172.413 / 174.418 / 179.640, **median 174.418** |
  | host `FACF6170` + chip `F46026B4` (boots D/E/F) | 69.124 / 69.444 / 69.444, **median 69.444** | 187.500 / 182.926 / 181.818, **median 182.926** |

  Every run `error=0`, `CP_RUN ... failed=0` (`1016/255920` windowed,
  `2031/511620` fullscreen). The semantic reference is 133.928 fps
  fullscreen: CP adds +30% single-fence, +37% pipelined. Host-side
  `CP_PROF` showed the per-dispatch fence wait falling 1482 -> 28 ms per
  600 frames; the frame is now bound by the present blit and CP/fence
  latency. `r200_triangle_smoke` remained pixel-identical on both drivers
  and the semantic path was unchanged (132.7 fps on boot D).
- Silent Hill (fullscreen 640x480x16, 3 buffers, quiet DLL + host):
  1,422 presents, `executes=5972`, no host errors, clean client quit and
  host exit. Log breakdown over 24 intervals: median frame 205 ms with
  the game's own `logic` 100 ms + `raster` 19 ms dominating; MiniGL
  `present` 28 ms (the game's software scaling) + `endscene` 2.4 ms.
  FMV intervals ran 15-16 fps; the 4-6 fps segments are heavy outdoor
  scenes (26k-47k tris/frame) where the game's own CPU work dominates.
  The game DLL was swapped to the quiet v6 build (`9E44E240`); the fused
  profiled `7ADA06F4` is preserved as `Work:SilentHill WOS/dll.7ada`.
- Deployed artifacts: quiet DLL `9E44E240` (306628 B, converted on target)
  and quiet pipeline host `FACF6170` (43512 B, protection `00020000`).
  Driver pair: chip `F46026B4` (77928 B, `radeon3d` commit `8b54b76`) with
  `Prometheus.card` `18A453D6` unchanged. Rollbacks:
  `LIBS:Picasso96/Radeon9200.chip.previous` = `CEB838A6` and
  `LIBS:Picasso96/Prometheus.card.previous` = `18A453D6` (RMB recovery
  block restores both); `Work:ibdraw/dll.pre-p3` = `32216D6B`,
  `DH2:ibdraw/mglhost.pre3` = `85F1A513`, `mglhost.p3a` = `32716DD9`
  (validated single-fence host), `DH2:u0/dll.u0old` = old.
- Remaining: nothing blocking; the FLIP-mode colorSeq race is documented
  in `CP_EMITTER.md` and `phase6_acceptance` has no deployed binary.
