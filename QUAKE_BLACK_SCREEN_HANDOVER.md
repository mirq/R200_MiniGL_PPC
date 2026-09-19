# glQuake black/frozen display + cleanup handover (2026-09-17, evening)

Repo focus: `/home/mirek/MiniGL_WOS_V19_mglQ3` (driver) and
`/home/mirek/BlitzQuake_src_vbcc` (client). Machine: physical A4000, 68060 +
MPC7410 WarpOS, Prometheus + Radeon 9200, bridge `192.168.1.21:2345`.

## 1. Symptom (reproducible)

glQuake as a MiniGL dispatch client: the engine runs the timedemo to
completion, but the display never shows gameplay.

- Fullscreen 640x480x16/32 with `+timedemo demo1` completes:
  `969 frames 13.9-14.0 seconds 69.3-69.6 fps` (`Work:games/quake/id1/qconsole.log`).
- Windowed 640x480x32: `969 frames 24.1 seconds 40.1 fps`.
- `+host_speeds 1` shows the frame is NOT idle: `gfx` (SCR_UpdateScreen)
  is 4-13 ms/frame, `server` 2-77 ms — the client renders and swaps.
  (Careful when reading: format is `<tot> tot <server> server <gfx> gfx <snd> snd`.)
- The visible window/screen keeps the last pre-map frame (the boot console
  with `THE NECROPOLIS` in the status line); in windowed mode ESC once cleared
  it to a black screen, then no further updates responded.
- Fullscreen has no keyboard input at all: the backend fullscreen window is
  created with `WFLG_BACKDROP` (`classic_68k/.../library/r200_surface.c`,
  `CreateFullscreenTarget` windowTags), and backdrop windows are not activated
  by Intuition. So ESC/console cannot be used in fullscreen.
- `mglshot` (bound to F12 in `id1/config.cfg`) writes `id1/quake00.ppm` /
  `quake01.ppm`; both are byte-identical (`CRC32 E1D131ED`, 921615 bytes)
  across separate runs and demo states, and sampled pixels are all black:
  **the readback path returns a constant black image** (separate bug, but it
  kills the easiest "what did we render" sensor).
- Gears (`precalc`, `Work:p4/gears_p4`) fullscreen works (131-220 fps) — it
  does no map/texture load and needs no input.
- Not CP-specific: the freeze also happens with `MGLPPC_CPEMIT` unset.
- Not pair-specific: both ibdraw `E4A796C6`+`E9002FAB` and p4 `38F63C02`+
  `32716DD9` freeze the same way.

## 2. Client fix implemented this session (Ctrl-C clean shutdown)

`glquake/sys_amiga_std.c` main loop now checks the Amiga break signal and
leaves through the normal quit path, so `VID_Shutdown` closes the MiniGL
context, the 68k host exits and its redirected log flushes:

```c
    if (CheckSignal(SIGBREAKF_CTRL_C)) {
      SetSignal(0, SIGBREAKF_CTRL_C);
      Con_Printf ("CTRL-C: shutting down...\n");
      Sys_Quit ();
    }
```

Use `Break glquakeWOS_dll` (or Ctrl-C on a CLI launch) to close a stuck
client. New client built and deployed:

| Item | CRC32 | Size | Location |
| --- | --- | --- | --- |
| Client (new, Ctrl-C) | `D9F69609` | 1295292 | `Work:games/quake/glquakeWOS_dll` |
| Client (previous production) | `22A0E33F` | 1295028 | `glquakeWOS_dll.pre-ctrlc` |
| Client source | — | — | `~/BlitzQuake_src_vbcc/glquake/sys_amiga_std.c` |
| Local build | `D9F69609` | 1295292 | `~/BlitzQuake_src_vbcc/build_wos_vbcc/glquakeWOS_dll` |

Note: `CheckSignal` is only evaluated once per host frame; a client stuck
inside one long GL call still needs a reset. The 68k host has no Ctrl-C
handler — consider adding one (`backend_r200/minigl_ppc_host.c` service loop).

## 3. Deployed artifacts / state (record before any new run)

- Game DLL `Work:games/quake/minigl_ppc.dll` = **`38F63C02`** (p4 AltiVec
  validation build). Backups: `minigl_ppc.dll.e4a` = `E4A796C6` (ibdraw),
  `minigl_ppc.dll.pre-e4a` = `BD4EE3BD`.
- Host: p4 pair `Work:p4/mglhost.p3a` = **`32716DD9`**, started by
  `Run >NIL: Execute Work:p4/hostrun_p3a`, log `Work:p4/host_p3a.log`.
  The ibdraw host `Work:ibdraw/mglhost` = `E9002FAB` (backup `.pre-e4a`).
- Env: `MGLPPC_HOSTPRI=-1` persists (ENVARC); `MGLPPC_CPEMIT` is volatile and
  was set to `1`; `MGLPPC_ALTIVEC`/`MGLPPC_NODIRECT`/`MINIGL_*` unset.
- Run scripts in `Work:games/quake`: `rungs` (fs 32), `rungs16` (fs 16),
  `rungswin` (windowed 32), `rungshs` (windowed + `+host_speeds 1`) — all
  `+timedemo demo1` (the client only executes args after `+`).
- Logs preserved: `id1/qconsole.log.fs32wedged`, `.fs32loop`, `.fs32_693`,
  plus the current `qconsole.log`.
- The p4 pair vs ibdraw pair gears discrepancy is still open: p4 CP+AltiVec
  = 220.588 fps (PHASE4 doc); ibdraw CP = 182.9 fps with `precalc` (600f) —
  suspected client (`precalc` vs `gears_p4`) and/or build difference.

## 4. Driver-code review findings (ranked; full detail from the review)

1. **Ring death at map load via the state-batch expander** (most likely):
   `backend_r200/library/r200_geometry.c:358-384` expands a same-header batch
   once `mismatchSubmits >= R200_EXPAND_MISMATCH_MIN` (4, line 42). Console/
   gears stay compact (~3 mismatches/frame); the textured world crosses the
   threshold at map load. If the expanded slot's record accounting
   (`recordCount`/`drawCount`/offsets) overruns the stream, the host rejects
   the commit at stage 5 and sets `control->error=1`
   (`backend_r200/minigl_ppc_host.c:1858-1867`), which is terminal: every
   later present/barrier fails, so the display keeps the last frame while the
   engine runs; readbacks never fill the shared buffer (constant black PPM).
   Diagnostics: host log for `statebatch_commit_fail`, `acc_commit_fail`,
   `commit_bad`, `ring_bad_entry`, `present_drain_fail`; PPC `RING barrier=FAIL`.
   Quick test: raise `R200_EXPAND_MISMATCH_MIN` very high (disable expansion)
   and re-run the map load; gears should be unaffected.
2. **Stale CP target** (`MGLPPC_CPEMIT=1` only): `RefreshCpColorTarget`
   re-stamps only when `colorSeq` changes; a missed publication renders a
   batch into the non-displayed buffer, present shows the other one.
   Host only reports `cp_token_mismatch`. Test with CPEMIT unset.
3. **Silent readback failures**: `R200PPCDispatchReadPixels/WriteShotPPM`
   copy the shared staging buffer even when the host rejects the command
   (`minigl_ppc_host.c:3309-3388`; frontend `:3628-3803`), so a failure looks
   like a valid all-black image (the observed constant `E1D131ED`). Fix: check
   `control.status`/`value[0]` and set an error; log host read-pixels status.
4. **Draw/read-buffer mirror split**: `glDrawBuffer(GL_FRONT)` only changes
   the PPC mirror; the host patches all non-CP records to host BACK
   (`PatchRecordTargets`, host `:355-375`). Harmless for default BACK clients,
   wrong for FRONT/readback interplay.
5. Deferred upload/batch knobs: `MGLPPC_BATCH` defaults off; the single-tile
   direct path + `R200PPCPendingUploadFlush` can drop geometry on flush
   failure (`r200_geometry.c:55-56,222-223`) — missing geometry, not a freeze.
6. Fullscreen buffer-rotation races (`CompletePendingFlip(block=FALSE)` skip,
   `WaitTargetBufferSafe` timeout) cause single stale frames, not a permanent
   freeze.

## 5. Recommended next steps (in order)

1. Re-run the windowed map load with the new Ctrl-C client, then
   `Break glquakeWOS_dll` and read `Work:p4/host_p3a.log` + the p4 host
   shutdown summary (`CP_RUN dispatches=...`, `executes= presents=`): any
   `control->error` trace confirms #1.
2. Test #1 directly: `R200_EXPAND_MISMATCH_MIN` disabled build (frontend),
   matched host, cold boot, run the map load and watch the display.
3. Fix #3 (surface readback failures) so future screenshots are trustworthy.
4. Only then chase present/buffer knobs (`MINIGL_PRESENT=BLIT/FLIP`,
   `MINIGL_BUFFERS=1`) and the WFLG_BACKDROP input issue.

## 6. Hygiene reminders that cost time

- Reset the machine with the bridge `reboot` yourself when a fullscreen
  client wedges; never `Break` a fullscreen GL client and immediately launch
  another. Cold boot after gray screens/gurus.
- Serialize ALL Amiga filesystem/DOS/input operations; the bridge's shared
  temp files collide otherwise.
- Deploy large files as 128 KB chunks (push ~20 KB/s, MCP call times out at
  ~1.3 MB); `Join` on the Amiga, verify destination CRC32.
- The bridge screenshot is unreliable for fullscreen P96 screens (noise).
- Use the `+` prefix for command-line console commands (`+timedemo demo1`).
- Never mix DLL and host builds; log files are DOS-locked while the host runs.
