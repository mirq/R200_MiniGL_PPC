# Handover — next session: batch the Silent Hill two-tile upload round trips

Written at the end of the 2026-09-16/17 session. Read this file first, then
`AGENTS.md` (repo root) and `backend_r200/PHASE4_ALTIVEC_SERIAL.md`.

## 1. The task

Silent Hill's intro logo and FMV upload a 320x256 16-bit frame every frame as
two power-of-two tiles (256x256 + 64x256). Measured per-tile cost on the
committed stack (diagnostic build, `benchmark.sh-intro`):

| sub-step | 256x256 | 64x256 |
|---|---:|---:|
| begin (DIRECT_BEGIN round trip) | 3827 us | 3534 us |
| write (565->BGRA + aperture stores) | 5905 us | 1496 us |
| commit (DIRECT_COMMIT round trip) | 2521 us | 2527 us |

=> about 20 ms per frame in the upload path, of which **~12.4 ms is the four
mailbox round trips**. Goal: cut that to two round trips per frame, saving
~5-6 ms/frame (visible in the logo/FMV pacing and in `present`).

### Preferred implementation (batch both tiles)

- `backend_r200/include/libraries/minigl_ppc_protocol.h`: add commands
  `MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2` and `..._COMMIT2` (the mailbox already
  carries 8 `value` words each way; tile B can travel in `value[4..7]` of the
  BEGIN2 request and its reply in `value[4..7]` of the ack). Keep the existing
  single-tile commands untouched for one-tile frames.
- `backend_r200/minigl_ppc_host.c`: cases `MGLPPC_CMD_TEXTURE_DIRECT_BEGIN2`
  and `..._COMMIT2` next to the existing handlers (~lines 2840/2888). They
  must reuse `BeginBackendDirectTexture`/`CommitBackendDirectTexture` and the
  same fence discipline (BEGIN drains, COMMIT deliberately does not).
- `backend_r200/library/minigl_r200_warpos_compile_probe.c`:
  `FrontendDirectTextureUpload` (~line 4000) and
  `R200PPCDispatchTexImage2D` (~line 4110). Batch is only possible when two
  uploads of the same context/epoch are issued back to back, i.e. the frontend
  must queue the first tile's BEGIN result and the second tile's parameters,
  then issue one BEGIN2 + one COMMIT2. Fall back to the existing pair whenever
  anything does not match (different epoch, non-565 or converted source, second
  upload missing, shadow present in a conflicting way).

### Cheaper fallback (if the protocol change is unwanted)

Overlap the two tiles: issue BEGIN(tile B) before COMMIT(tile A) and defer A's
commit by one publish. The commit reply values (surfaceToken/width/height/
levels/epoch) must then be captured before the next request's ack overwrites
`ackValue`; if that cannot be done safely, prefer the BEGIN2/COMMIT2 design.

### Do NOT

- Do not use 16-byte `stvx`/vector stores for the texture aperture. Measured
  25.2 -> 13.6 fps on `benchmark.sh-intro` (unlike the vertex aperture in
  PHASE4_ALTIVEC_SERIAL.md, store width is not a lever here).
- Do not retry `stswi` burst stores — wedges the GPU (needs a cold cycle).
- Do not re-enable the "long-idle host" spin (sleep only after 1.5 s) for
  suite/benchmark work: it breaks the suite's `glReadPixels` capture and
  measured 16.5 vs 25.2 fps on `sh-intro`. It is only beneficial to Silent
  Hill gameplay (`present` 61-79 -> 37-41 ms) — resolve the capture conflict
  before ever shipping it.
- Do not re-enable the wake-flag/Signal design: unconditional PPC `Signal()`
  in the publish path cost 23% of gears.

## 2. Machine and deployments (state at handover)

Hardware: A4000/060 + Prometheus + Radeon 9200 + PMC 7410 WarpOS. Bridge
`192.168.1.21:2345` (TCP). After a cold boot: `tools/wait_bridge.sh`, then
`amiga_connect(host="192.168.1.21", port=2345)`, `C:InitPPC` **once**, then
`SetEnv MGLPPC_HOSTPRI -1`.

Deployed and verified:
- `LIBS:minigl.library` = `CE49692E` (classic R200 build).
- client DLL in `ibdraw`, `games/quake`, `Data:mgldiag`, `SilentHill WOS` =
  `AAEFDACC` (= `Work:wosbuild/abi9.dll`, warpelf `89A87926`, committed build).
- `Work:ibdraw/mglhost` = `9C3F62B2` (committed; keep `mglhost.prod` as the
  same file). **After every copy set `Protection ... 00020000`** with the
  bridge protect tool (AmigaDOS `Protect` cannot set those bits; a zeroed
  classification halves throughput).
- env: `MGLPPC_HOSTPRI=-1`; `MGLPPC_VERBOSE`, `MGLPPC_UPLOAD_TRACE`,
  `MGLPPC_NODIRECT`, `MGLPPC_COMMITACC`, `MGLPPC_NOACC` all unset.
- rollback/reference artifacts kept: `Work:ibdraw/mglhost.pre-merge`
  (`573F97A3`), `minigl_ppc.dll.pre-binddef` (`6C74A72D`),
  `Work:ibdraw/minigl.library` (`62343673`).

Machine state at handover: a leftover `precalc` from a timed-out script may
still hold `DH2:ibdraw/gears_keep.log`, and a host port may be up. Clean up:

    Run >NIL: Execute DH2:ibdraw/hoststop --orphan
    Run >NIL: Execute DH2:ibdraw/hostrun

If the host's port name does not appear in the port list (shows as `?`),
stop and restart the host before measuring; that instance reads ~half speed.

## 3. Repo / commits

- `f3caee6` suite: `benchmark.sh-intro` (local, **not pushed**).
- `4303d41` r200 ppc: record three-boot AltiVec serialization results.
- Earlier session work already pushed: `86d348f` (PowerOpen ABI bridge +
  classic enum compat), `277470a` (benchmark.tex-upload).
- Working tree should be clean apart from `bin/` artifacts after rebuilds.

## 4. Build / deploy / test procedure

1. Build the DLL: `make -f Makefile.dll.r200.gcc warpelf` -> record CRC of
   `bin/minigl_ppc_r200.warpelf`. Profiled variant:
   `MGLPPC_PROFILE=1 make -f Makefile.dll.r200.gcc OBJDIR=obj/dll-r200-prof
   WARPELF=bin/minigl_ppc_r200_prof.warpelf warpelf`.
2. Build the host: `make -f Makefile.dll.r200.gcc host` (the classifier check
   must pass).
3. Push (serialized, one file at a time; >500 KB in parts and `Join` on the
   Amiga). Convert on target: `Work:wosbuild/Elf2Exe2 x.elf x.dll`. Verify the
   **active** path CRC after every copy.
4. Suite: `make -f Makefile.import.gcc testprograms` ->
   `bin/examples/minigl_testsuite_ppc.warpelf`; push in 2 parts, Join,
   Elf2Exe2 to `Data:mgldiag/mgltsuiteM` (same 16-char name or later).
5. Benchmarks (fullscreen, `--nosync`, host restarted before each run,
   `MGLPPC_HOSTPRI=-1`):
   - `mgltsuiteM --test benchmark.sh-intro --fullscreen --width 640
     --height 480 --depth 16 --frames 300 --nosync` — baseline **25.167 PASS**.
   - `mgltsuiteM --test benchmark.tex-upload ...` — baseline **20.9**.
   - gears: `CD DH2:ibdraw` +
     `precalc -frames 600 -width 640 -height 480 -depth 16 -buffers 3
     -nosync -nofps` — baseline **134.5-138.2**.
   - room test: list ids with `mgltsuiteM --list` and run the room-walk
     benchmark; record the number before and after the change.
6. Notes: the suite's capture (`glReadPixels`) is flaky — a FAIL with
   "orientation: capture failed" or "calibration failed r=.." is usually the
   capture, not the change; re-run. A cold power cycle is required for
   reference-grade numbers (a warm/degraded boot reads ~94 instead of ~135 on
   gears).

## 5. Ready-to-paste prompt for the next agent

"Read `backend_r200/NEXT_SESSION_HANDOVER.md` and `AGENTS.md`. Machine is the
physical A4000 at 192.168.1.21:2345; after a cold boot wait for the bridge,
reconnect explicitly, run `C:InitPPC` once and `SetEnv MGLPPC_HOSTPRI -1`.
Clean up the leftover host/precalc first (`hoststop --orphan`, then
`hostrun`). Implement the batched two-tile texture upload (BEGIN2/COMMIT2)
described in section 1, build DLL+host, deploy with protection bits
`00020000` on the host, and verify: `benchmark.sh-intro` (baseline 25.167
PASS), `benchmark.tex-upload` (20.9), gears (134.5-138.2) and the suite's
room-walk benchmark. Keep the per-upload breakdown in mind: begin 3.8/3.5 ms,
write 5.9/1.5 ms, commit 2.5/2.5 ms; target ~5-6 ms/frame. Do not use wide
aperture stores and do not re-enable the long-idle/wake host designs."
