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

## Improvement 2: adaptive submit merging (2026-09-13, same session)

The state-batch split-on-header-mismatch cost room-walk 12.7 submits/frame
(1.57 records each). A differing header no longer always submits: after a
frame has burned `R200_EXPAND_MISMATCH_MIN` (4) mismatch submits, further
mismatches expand the pending same-header batch into per-draw full records
in place and keep filling the slot on the plain commit-record shape, which
the host COMMIT entry and the driver already consume. A per-slot `expanded`
flag keeps expanded slots off the batch-accumulation path (accumulating
into an expanded slot claims more draws than the record stream holds ->
service stage-5 reject -> sticky ring error -> gray screen; the first
version hit exactly that), and the threshold keeps dense same-state
workloads on the compact shape.

| Workload | baseline | bind-deferral | + adaptive merge |
| --- | ---: | ---: | ---: |
| gears precalc 600f | 132.7-135.1 | 133.9 | **138.2 fps** |
| room-walk 120f | 12.0 | 21.0 | **27.6 fps (2.3x)** |
| room-walk submits/frame | 12.7 | 12.7 | **5.7** (4 compact + expansions) |
| lightmap-update 60f | PASS | PASS | **PASS** |
| quake.alias-ogre 60f | PASS | PASS | **PASS** |
| blend.texture-alpha 60f | PASS | PASS | PASS |

Tuning note: an unconditional expansion variant measured room-walk 31.7 fps
(2.0 submits/frame) but regressed gears to 91.2 fps - full 44-dword headers
per draw cost the driver more than the compact state-batch shape it replaced.
The threshold preserves both. Two intermediate bugs are documented for the
record: the flush clobber above, and a control.value clobber in the upload
path (same shared-buffer rule: PlatformCall sends and overwrites
control.value[], so it must run before a caller prepares its own message).

## Improvement 3: host commit coalescing + suite --nosync + sync-off default (2026-09-14)

Profile of the post-merge stack (profiler host + DLL) showed the room-walk
frame fully serialized on the 68k: submit ~1.7 ms/execute x 5.7 plus a
~22 ms present stage. Two changes landed:

1. **Host commit coalescing** (`minigl_ppc_host.c`): consecutive plain
   COMMIT ring entries sharing one vertex segment now ride to the GPU in a
   single CommitBatch call (records/offsets concatenated, one fence, one
   emitter pass - its Live-state cache carries across entries). Flush
   triggers: non-plain entry, PRESENT, segment change, 192-draw/8192-dword
   capacity, end of queue. Retirement semantics untouched (control->retired
   still advances only after the merged fence drains). Measured neutral on
   the current workloads: the host consumes an entry (~2 ms) faster than
   the PPC produces the next (~6 ms), so bursts rarely queue - it engages
   during the vsync-shadow window and after stalls. Kept: correct, cheap,
   and it protects against regressions if entry production gets burstier.
2. **Suite `--nosync` + stack sync-off default**: profiling exposed the
   real room-walk ceiling - the present stage was vblank-locked (22 ms),
   which had also capped every simple suite test at the familiar ~33 fps
   (two 60 Hz vsyncs). The suite gained `--nosync` (mirroring the gears
   client's -nosync), and `R200PPCPlatformResetClientDefaults` now starts
   `syncEnabled=0` (published with syncSeq 1, so the host applies it at the
   first ring entry; no present escapes with the library's sync-on
   default). Apps restore vsync with `mglEnableSync(GL_TRUE)`.

Result on the final deployed pair (DLL `8560A1BC`, host `9C3F62B2`,
suite `34B4A730`), fullscreen 640x480x16, verified fresh host per run:

| Workload | session start | final |
| --- | ---: | ---: |
| room-walk 120f (default present mode) | 12.0 fps | **59.4 fps (4.9x)** |
| quake.alias-ogre 60f | 32.6 fps (sync-capped) | **PASS 142.9 fps** |
| gears precalc 600f | 132.7-135.1 | **135.7 fps** (explicit -nosync) |
| lightmap-update 60f | PASS | PASS (14.4 fps nosync - the test's own per-frame TexImage2D replacement dominates; sync had masked it) |

Room-walk's remaining budget at 60 fps (16.7 ms): ~10 ms host submit/present
chain and ~6 ms PPC frontend, now overlapped; further gains need improvement
3's original target (the ~1 ms fixed cost inside Radeon3DCommitBatch, driver
side) or GPU-side work. The two-point model (fixed ~0.9 ms + ~39 us/record)
still stands for that future work.

Session tooling: `Work:ibdraw/hoststop --orphan` (new 68k helper) cleanly
shuts down an orphaned host whose client died with a poisoned ring - use it
instead of a cold cycle whenever no GL client is live.

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
