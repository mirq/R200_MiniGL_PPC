# Deferred texture-bind sync: resolved (2026-09-13)

Goal: remove the ~20-33 per-frame `MGLPPC_CMD_BIND_TEXTURE` mailbox
round-trips (room-walk's dominant control-message cost, ~2.3 ms of 68k time
each). Texture state already travels in every draw record header
(`r200_state.c`: header[4] surface token, [12] size, [13] texstate).

## Root cause of the first failure (fixed)

The first elision build broke `quake.alias-ogre` (no ogre rendered,
`submitted=0`). Not a bind-semantics problem: the `HostTextureShadowSync()`
flush was placed AFTER `R200PPCDispatchTexImage2D` had already filled the
shared `FrontendPlatform.control.value[0..7]` with the upload parameters.
`PlatformCall` both sends and overwrites that buffer, so the flush's BIND
reply clobbered the following `TEXTURE_UPLOAD_SHARED` request (host got
`glTexImage2D(0, ..., 0, 0, ...)` -> upload failed -> texture never
materialized -> every draw of that texture dropped).

Why the earlier tests passed and misdirected the bisection: the direct
upload path (name-explicit, shadow-free) requires power-of-two dimensions.
Blend 1x1 and lightmap 64x64 took the direct path (no flush, no clobber);
the ogre skin 264x194 is non-power-of-two and takes the shared path.
Fix: flush BEFORE the wrapper prepares its control buffer, in both the
shared and the chunked `UPLOAD_BEGIN` branches.

## Validated result (deployed DLL E9200E10, exe CRC from ELF 2B1907DF)

Baseline DLL `6C74A72D` vs deferral DLL, same boot discipline, fullscreen
640x480x16, fresh verified host per run:

| Test | Baseline | Deferral |
| --- | ---: | ---: |
| gears precalc 600f | 135.1 / 132.7 fps | **133.9 fps**, error=0 |
| room-walk 120f | 12.0 fps | **21.0 fps (+75%)** |
| room-walk 600f | 12.6 fps (baseline photo run) | **21.3 fps** |
| quake.alias-ogre 60f | PASS | **PASS** (pixel-verified, submitted=all) |
| quake.lightmap-update 60f | PASS | **PASS** (pixel-correct) |
| blend.texture-alpha | PASS | PASS |

Per-frame semantic mailbox messages: ~33 -> ~0 for room-walk rendering;
uploads still flush one bind each (rare). The remaining room-walk frame
cost is now dominated by the 12.7 submits/frame x 68k ring handling
(improvement 2) and the end-of-frame present.

The bind wrapper no longer sends any mailbox command; the host's
`state->semanticBoundTexture` is maintained exclusively by the upload-path
flushes, which are the only consumers.

## Pre-existing defect found during validation (NOT fixed here, backlog)

`quake.world-room-walk` renders a subset of surfaces (arches, door frames,
pillars, ceiling panels - the light gray metal texture class) as black/white
horizontal stripes on BOTH DLL variants; photo A/B confirmed identical
striping on baseline `6C74A72D`. The suite's pixel verify fails identically
on both (`room f0 want 69,53,34 got 88,78,66`). Pre-existing PPC-frontend
correctness bug, separate from the bind work; photos retained beside the
run logs (user camera, 600-frame runs on both DLLs).

## Deployment record

- Source: `R200PPCDispatchBindTexture` defers (no mailbox);
  `HostTextureShadowSync()` flushed at the TOP of the shared and chunked
  upload branches (before control.value[] is prepared); context-create
  reset + delete-textures invalidation kept.
- Deployed: `Work:ibdraw/minigl_ppc.dll` CRC `E9200E10` (305,380 bytes);
  ELF `2B1907DF` (373,781 bytes). Baseline retained as
  `minigl_ppc.dll.pre-binddef`. Staged: `DH2:wosbuild/mglbind3.dll`.
- Host (`mglhost`) and `minigl.library` unchanged.
- Host-restart discipline reminder: one fresh host per client run,
  port verified before launch (the documented restart race bit twice
  during this session).
