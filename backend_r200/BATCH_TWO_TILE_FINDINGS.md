# Batched two-tile direct upload (BEGIN2/COMMIT2) — implementation and findings

2026-09-17, physical A4000/68060 + Prometheus + RV280 + MPC7410/WarpOS,
bridge `192.168.1.21:2345`, one long boot (warm, no cold cycle available
during the session). Client `Data:mgldiag/mgltsuiteM` (930556 bytes),
quiet DLL `E4A796C6` (warpelf `20575C7C`), profiled DLL `120F9C50`,
host `E9002FAB`.

## 1. What was asked

The handover (`NEXT_SESSION_HANDOVER.md`) targeted the four mailbox round
trips of Silent Hill's two-tile 16-bit upload (256x256 + 64x256 per frame,
BEGIN/COMMIT per tile):

| sub-step | 256x256 | 64x256 |
|---|---:|---:|
| begin | 3827 us | 3534 us |
| write | 5905 us | 1496 us |
| commit | 2521 us | 2527 us |

## 2. What was implemented

- Protocol (`include/libraries/minigl_ppc_protocol.h`, both classic copies
  kept byte-identical for `tests/test_r200_context_lifecycle.py`):
  - `MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2` (29) / `..._COMMIT2` (30);
  - `MGLPPC_CAP_TEXTURE_DIRECT_BATCH_V1` (1UL << 14), advertised by the
    host only with the mailbox and the direct path enabled;
  - both commands added to `MGLPPC_MAILBOX_COMMAND_ALLOWED`.
  - Reply layouts fit the existing 8-word `ackValue` (no mailbox struct
    change, protocol version stays 15; capability-gated so an old host is
    never sent the new commands).
- Host (`minigl_ppc_host.c`): `BEGIN2` (two distinct named textures,
  drains once, begins both; aborts a partially begun pair before replying)
  and `COMMIT2` (commits both; `value[0]` = committed mask, per-tile
  tokens/epoch/serial/levels). Profile counters `direct_begin2/commit2`.
- Frontend (`library/minigl_r200_warpos_compile_probe.c`):
  - `FrontendDirectWritePixels` extracted from the direct write loop;
    added a dword-copy fast path for canonical `MGL_UBYTE_BGRA` sources
    (byte-identical on this big-endian CPU: `FrontendSourceToBGRA32`
    returns the source dword unchanged for BGRA);
  - `FrontendDirectTextureUploadNamed` / `FrontendMirrorTextureValues` /
    `FrontendShadowStageSource` refactors so a deferred tile can be
    completed without the currently-bound texture;
  - deferred first tile (`pendingDirect`): a 565 upload is converted into
    PPC memory (the texture's own retained shadow when it owns one, else
    the shared upload buffer) at call time — the client's texels are
    consumed exactly at the call — and BEGIN/COMMIT are deferred until the
    second upload arrives, then one BEGIN2 + one COMMIT2 carries the pair;
  - flush points (`R200PPCPendingUploadFlush`): `PrepareScreenAppend`,
    `FlushScreenVertices`, `R200PPCPlatformBarrier`, `TexImage2D`
    (incompatible/same-name second uploads), `TexSubImage2D`,
    `DeleteTextures`, context create/delete. Fallbacks: any mismatch
    (different epoch, non-565, non-POT, same texture name, missing second
    upload, staging failure) completes tile A through the ordinary
    single-tile path first.
  - Opt-in gate: `MGLPPC_BATCH=1` enables the batch; default OFF.
  - `MGLPPC_BATCHDIAG=1` (profile builds) dumps per-phase `PerfClock`
    ticks to `DH2:wosbuild/batchdiag.txt` at context teardown.

## 3. The batched path works

Profiled host, `benchmark.sh-intro --frames 300`, `MGLPPC_BATCH=1`:

```
MiniGLPPC host: direct_begin2=311 direct_begin2_us=871006
                direct_commit2=311 direct_commit2_us=84816
MiniGLPPC host: uploads=622 upload_fails=0
MINIGL_TEST ... result=PASS ... gl_error=0 detail="320x256 frame as
  256x256 + 64x256 uploads renders"
```

311 pairs for 300 measured frames + 11 warm-up frames; both tiles of every
frame travel in one BEGIN2 and one COMMIT2; zero upload failures, zero GL
errors, pixel verification PASS.

## 4. Measured result: the batch is a net loss on the current stack

Same DLL, same host, same boot, only the `MGLPPC_BATCH` toggle changes:

| workload | batch OFF (default) | batch ON |
|---|---:|---:|
| benchmark.sh-intro 300f | **62.5 – 66.4 fps** | **29.3 – 29.6 fps** |
| benchmark.tex-upload 300f | 43.7 fps | 46.4 fps |

Phase attribution (profile build, `MGLPPC_BATCHDIAG=1`, per upload):

| phase | single-tile path | batched path |
|---|---:|---:|
| BEGIN round trip | 2.1 ms | — |
| BEGIN2 round trip | — | 15.7 ms (22 ms at the true 24.74 MHz timebase) |
| write (convert + aperture) | 1.4 ms | 2.7 ms/pair |
| COMMIT / COMMIT2 | 0.6 ms | 2.0 ms/pair |
| pre-BEGIN barrier | (see below) | 3.0 – 3.7 ms |

The host's own handler time is 1.5 ms for BEGIN and 2.8 ms for BEGIN2, so
the extra ~13 ms of the deferred BEGIN2 is **mailbox service latency, not
handler work**: the request is published right after the client's barrier
and the host does not pick it up for ~13 ms. The immediate BEGIN of the
single-tile path is answered in ~0.6 ms of transport.

Ruled out so far:
- The host idle sleep (`MGLPPC_IDLE_SLEEP_MS 1500`): a never-sleeping host
  build (`mglhost.ns`, sleep constant 0) gave 29.6 fps, unchanged.
- Host handler work: measured directly by the host (2.8 ms).
- The never-sleeping host also rules out the long-idle spin discussed in
  the handover's Do-NOT list.

The single-tile path does pay the round trips (622 BEGIN + 622 COMMIT for
311 uploads), but each is cheap (~2.1/0.6 ms), and it never lands in the
slow window the deferred BEGIN2 hits.

**Decision: `MGLPPC_BATCH` defaults to off.** The implementation and the
protocol stay in the tree (capability-gated, host-compatible) so the
latency window can be attacked separately; see §6.

## 5. Broader finding: the recorded baselines came from a stale stack

A/B on this boot, `benchmark.sh-intro 300f`:

| pair | fps |
|---|---:|
| deployed pair at handover (`abi9.dll` `AAEFDACC` + `mglhost` `9C3F62B2`) | 20.4 – 22.4 |
| current source, rebuilt (quiet DLL `E4A796C6` + host `E9002FAB`, batch off) | 63.6 |

Same boot, same suite binary. `benchmark.tex-upload`: 19.4 -> 44.6.
gears 600f: 138.9 -> 144.9. room-walk 120f: 58.8 -> 59.4 (same
pre-existing pixel FAIL `room f128 want 59,44,25 got 34,29,15`, identical
before/after). The rebuilt stack includes the committed `5c0bcc3`
multi-fence host commit, which is not in the deployed `9C3F62B2` host.

Two deployment hazards found while isolating this:

1. **Mixed pairs are pathological.** Any combination of the stale
   deployed DLL/host with a rebuilt binary (either direction) drops gears
   to a deterministic **7.8 fps** with `presents=0` and exactly 5 executes
   per frame (e.g. `abi9` + new host, new DLL + `9C3F62B2`). A pristine
   `git worktree` HEAD build (`head1.dll` + a HEAD-built host, none of the
   changes here) shows the same 7.8 fps. Always deploy the DLL and host
   from the same build.
2. **The committed `bin/*.warpelf` artifacts are not reproducible from
   HEAD.** `bin/minigl_ppc_r200.warpelf` (= `abi9` = `89A87926`) rebuilds
   to `A759AC2B` because `radeon3d_emit.c` comes from the external
   `~/p96-driver` tree, which has moved since the artifact was committed.
   A rebuild is a different (and for sh-intro much faster) stack; record
   the rebuilt CRCs, not the committed ones.

## 6. Next step for the batch (if it is worth revisiting)

Root-cause the ~13 ms host service latency of the deferred BEGIN2. The
request is published from `FrontendDirectBatchTry` right after the client's
`FlushScreenVertices + Submit + R200PPCPlatformBarrier(FALSE)`. The host
handler itself is 2.8 ms, so the host is inside some other main-loop work
when the request lands (present/fence/ring work of the previous frame).

Candidate experiments:
- Host-side max poll-gap counter (time between consecutive `PollMailbox`
  calls) to identify the blocking stage.
- Make the batch's pre-BEGIN2 barrier also wait for `control->retired`
  (GPU retirement) so the request is published when the host pipeline is
  quiet. Expect this to move the wait into the barrier (net zero unless
  the host is otherwise idle).
- Move the deferred pair's BEGIN2 earlier: after tile A is staged (its
  call), rather than at tile B's call, is impossible without knowing B;
  but a different overlap (COMMIT(A) + BEGIN(B) in one command, three
  round trips) may avoid the window.
- Re-measure once the deployed host matches HEAD (the timestamped
  window may be a property of the pre-5c0bcc3 host only).

## 7. Deployment record (end of session)

| item | CRC32 | size | location |
|---|---|---:|---|
| quiet DLL warpelf | `20575C7C` | 387428 | repo `bin/minigl_ppc_r200.warpelf` |
| quiet DLL converted | `E4A796C6` | 317284 | `DH2:wosbuild/final2.dll` |
| profiled DLL warpelf | `120F9C50` | 398803 | repo `bin/minigl_ppc_r200_prof.warpelf` |
| host | `E9002FAB` | 44440 | repo `bin/minigl_ppc_host`, `DH2:ibdraw/mglhost` |
| active client DLL | `E4A796C6` | 317284 | `Data:mgldiag/minigl_ppc.dll`, `DH2:ibdraw/minigl_ppc.dll` |
| rollback DLL | `AAEFDACC` | 305112 | `DH2:ibdraw/dll.pre-batch`, `Data:mgldiag/dll.pre-batch` |
| rollback host | `9C3F62B2` | 41052 | `DH2:ibdraw/mglhost.pre-batch` |

Env at end of session: `MGLPPC_HOSTPRI=-1`; `MGLPPC_VERBOSE`,
`MGLPPC_UPLOAD_TRACE`, `MGLPPC_BATCH`, `MGLPPC_BATCHDIAG`, `MGLPPC_NODIRECT`
unset/empty. `LIBS:minigl.library` untouched (`CE49692E`).

Validation runs on this boot (quiet stack, batch off):

```
benchmark.sh-intro 300f   PASS fps_x1000=63829 gl_error=0
benchmark.tex-upload 300f PASS fps_x1000=44642 gl_error=0
quake.world-room-walk 120 PASS(pre-existing pixel FAIL) fps_x1000=59405
quake.lightmap-update 60  PASS fps_x1000=100000
quake.alias-ogre 60       PASS fps_x1000=150000
precalc gears 600f        fps=144.927 error=0
r200_texture_update_test  CHECKS=3495 FAILURES=1 (identical to the U0
                          pre-existing INDEXED SOURCE failure)
```

Not validated: a cold-boot reference series (no operator cold cycle in
this session), and the Silent Hill client itself (not launched).
