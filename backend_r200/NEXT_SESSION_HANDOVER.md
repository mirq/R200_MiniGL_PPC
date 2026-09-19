# Handover — next session

Written at the end of the 2026-09-17 session. Read this file first, then
`AGENTS.md` (repo root), `backend_r200/BATCH_TWO_TILE_FINDINGS.md` (the
batched two-tile upload implementation and why it is opt-in) and
`backend_r200/PHASE4_ALTIVEC_SERIAL.md`.

## 1. State of the tree

Uncommitted work (worktree is dirty on purpose; nothing was committed):

- `backend_r200/include/libraries/minigl_ppc_protocol.h` (+ the two
  byte-identical `classic_68k/dev/...` copies):
  `MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2` (29), `..._COMMIT2` (30),
  `MGLPPC_CAP_TEXTURE_DIRECT_BATCH_V1` (bit 14), mailbox whitelist.
- `backend_r200/minigl_ppc_host.c`: BEGIN2/COMMIT2 handlers,
  capability, `direct_begin2/commit2` profile counters.
- `backend_r200/library/minigl_r200_warpos_compile_probe.c`: batched
  two-tile direct upload (deferred first tile, staging, flush points),
  refactors (`FrontendDirectWritePixels`,
  `FrontendDirectTextureUploadNamed`, `FrontendMirrorTextureValues`,
  `FrontendShadowStageSource`), `MGLPPC_BATCH` (default off) and
  `MGLPPC_BATCHDIAG` (profile builds, `DH2:wosbuild/batchdiag.txt`).
- `backend_r200/library/r200_geometry.c` + `minigl_r200_internal.h`:
  `R200PPCPendingUploadFlush` hooks in `PrepareScreenAppend` and
  `FlushScreenVertices`.
- `backend_r200/BATCH_TWO_TILE_FINDINGS.md`: all measurements.
- Protocol copies synchronized (the drift assertion in
  `tests/test_r200_context_lifecycle.py` passes).

Builds verified: `make -f Makefile.dll.r200.gcc warpelf`,
`MGLPPC_PROFILE=1 ... OBJDIR=obj/dll-r200-prof ... warpelf`,
`make -f Makefile.dll.r200.gcc host` (classifier check passes),
`make -f Makefile.import.gcc testprograms`.
`tests/test_r200_context_lifecycle.py` has the same pre-existing compile
errors as HEAD (HostRingMailboxActive/RingAcc/mismatchSubmits/AltiVec);
there are no new ones. `test_enum_parity`, `test_r200_bind_cache`,
`test_r200_host_profile`, `test_r200_transport_profile` also fail
identically at HEAD (pre-existing).

## 2. Headline result

The deployed pair at the start of the session (`abi9.dll` `AAEFDACC` +
`mglhost` `9C3F62B2`) is a **stale, much slower stack** than the current
source builds. Same boot, `benchmark.sh-intro 300f`: 20.4-22.4 fps for
the stale pair vs **63.6 fps** for the rebuilt current source (the
committed `5c0bcc3` multi-fence host is not in `9C3F62B2`). tex-upload
19.4 -> 44.6 fps; gears 138.9 -> 144.9.

The batched two-tile upload itself is implemented and works (311
BEGIN2+COMMIT2 pairs for 300 frames, 0 failures, pixel-correct) but on the
current host it is a **net loss** on the exact Silent Hill pattern:
29.3-29.6 fps batched vs 62.5-66.4 fps single-tile, because the deferred
BEGIN2 waits ~13 ms for host service while an immediate BEGIN is answered
in ~0.6 ms. It is therefore **opt-in** (`MGLPPC_BATCH=1`); the default is
the single-tile path. See `BATCH_TWO_TILE_FINDINGS.md` §4-6.

## 3. Deployment (end of session)

| item | CRC32 | location |
|---|---|---|
| quiet DLL warpelf | `20575C7C` | repo `bin/minigl_ppc_r200.warpelf` |
| quiet DLL converted | `E4A796C6` | `DH2:wosbuild/final2.dll` |
| profiled DLL warpelf | `120F9C50` | repo `bin/minigl_ppc_r200_prof.warpelf` |
| host | `E9002FAB` | repo `bin/minigl_ppc_host`, `DH2:ibdraw/mglhost` |
| active client DLL | `E4A796C6` | `Data:mgldiag/minigl_ppc.dll`, `DH2:ibdraw/minigl_ppc.dll` |
| rollback DLL | `AAEFDACC` | `DH2:ibdraw/dll.pre-batch`, `Data:mgldiag/dll.pre-batch` |
| rollback host | `9C3F62B2` | `DH2:ibdraw/mglhost.pre-batch` |

`LIBS:minigl.library` is the classic R200 build `CE49692E` (untouched).
Env: `MGLPPC_HOSTPRI=-1`; `MGLPPC_VERBOSE`, `MGLPPC_UPLOAD_TRACE`,
`MGLPPC_BATCH`, `MGLPPC_BATCHDIAG`, `MGLPPC_NODIRECT` unset/empty.
Client DLL backups from the previous session (`Work:wosbuild/abi9.dll`,
`Work:ibdraw/mglhost.prod`) are intact.

## 4. Hardware validation this session (same warm boot, not cold)

Quiet stack, batch off, fullscreen 640x480x16, `--nosync`, fresh host per
run, `MGLPPC_HOSTPRI=-1`:

| test | result |
|---|---|
| `benchmark.sh-intro --frames 300` | **PASS 63.829 fps**, gl_error=0 |
| `benchmark.tex-upload --frames 300` | **PASS 44.642 fps**, gl_error=0 |
| `quake.world-room-walk --frames 120` | 59.405 fps, same pre-existing pixel FAIL (`want 59,44,25 got 34,29,15`) |
| `quake.lightmap-update --frames 60` | PASS, pixel-correct |
| `quake.alias-ogre --frames 60` | PASS 150 fps |
| `precalc` gears 600f | 144.927 fps, error=0 |
| `r200_texture_update_test` | CHECKS=3495 FAILURES=1 (identical pre-existing INDEXED SOURCE failure) |
| `benchmark.sh-intro` with `MGLPPC_BATCH=1` | PASS 29.3-29.6 fps (regression, expected) |

Not done: cold-boot reference series (no operator power cycle), Silent
Hill client check, three-run medians.

## 5. Deployment rules learned the hard way

- **Never mix DLL and host builds.** Any stale/rebuilt combination (either
  direction) drops gears to a deterministic 7.8 fps with `presents=0`;
  a pristine HEAD `git worktree` build shows it too, so it is a
  pre-existing artifact/source mismatch, not this change. Deploy both
  from the same build and record both CRCs.
- The committed `bin/minigl_ppc_r200.warpelf` is **not** reproducible from
  HEAD (the external `~/p96-driver` `radeon3d_emit.c` moved), so always
  rebuild and use the new CRC.
- `hostrun` is idempotent: if a previous host is still alive, the new one
  exits and the old one serves the client. Verify the port appears in the
  port list before every timed run (the handover's `?` warning).
- `Elf2Exe2` usage is `Elf2Exe2 in.elf out.dll` (no `?` usage text; a
  `CD x & tool` command line gave "wrong number of arguments" — use full
  paths in one command).
- The MCP request layer can time out while the Amiga script is still
  running; poll the output file instead of re-issuing the run.

## 6. What to do next

1. **Deploy-and-measure the current stack on a cold boot** with the full
   three-run protocol. The source stack is 3x faster on sh-intro than the
   recorded baselines; the handover's old numbers in §4 of the previous
   handover are from the stale pair and should not be used as gates.
2. **Root-cause the deferred BEGIN2 service latency** (see
   `BATCH_TWO_TILE_FINDINGS.md` §6). If fixed, re-measure and flip the
   `MGLPPC_BATCH` default. Until then the batch stays off.
3. Re-baseline `benchmark.sh-intro`, `benchmark.tex-upload`, gears and
   room-walk on the new stack (cold boot, 3 runs, medians) and record the
   new numbers; the old 25.167 / 20.9 / 134.5-138.2 gates are obsolete.
4. Silent Hill gameplay check with the new stack (`MGLPPC_BATCH` unset):
   the upload path is now ~2x faster than the handover's baseline and the
   `present` stage may no longer dominate the same way.
5. Consider whether the batched protocol (BEGIN2/COMMIT2) is worth keeping
   at all if the host latency is not fixed; it is capability-gated and
   dead code when `MGLPPC_BATCH` is unset.

## 7. Ready-to-paste prompt for the next agent

"Read `backend_r200/NEXT_SESSION_HANDOVER.md`,
`backend_r200/BATCH_TWO_TILE_FINDINGS.md` and `AGENTS.md`. Machine is the
physical A4000 at 192.168.1.21:2345; after a cold boot wait for the
bridge, reconnect explicitly, run `C:InitPPC` once and
`SetEnv MGLPPC_HOSTPRI -1`. The deployed stack is the current source build
(quiet DLL `E4A796C6`, host `E9002FAB`); the batched two-tile upload is
opt-in via `MGLPPC_BATCH=1` and is currently a regression on
`benchmark.sh-intro` because of a ~13 ms deferred-BEGIN2 service latency.
First re-baseline the deployed stack on a cold boot (sh-intro,
tex-upload, gears, room-walk; three runs, medians), then either fix the
BEGIN2 latency or decide to drop the batched protocol. Do not use wide
aperture stores, do not re-enable the long-idle or wake-flag host designs,
and never mix DLL/host builds."
