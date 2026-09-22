# Troubleshooting and recovery

This document is the symptom → cause → fix guide for the physical A4000 stack.
For the benchmark procedure itself see [benchmarking.md](benchmarking.md); for
client setup see [client-guide.md](client-guide.md).

## 1. Machine-level recovery

| Symptom | Cause | Action |
|---|---|---|
| Grey screen, guru, or hard GPU hang | anything | **Operator cold power cycle.** Warm reboot does not reliably reset the Radeon. |
| Wedged PPC client: no output, ignores break signals | WarpOS client stuck | Cold power cycle. Do **not** run `InitPPC` again as a recovery. |
| Windowed presentation broken, fullscreen still works | Radeon not reset after a crash | Cold power cycle; a warm reboot leaves this degraded. |
| Grey/degraded boot after a driver change | bad `Radeon9200.chip`/`Prometheus.card` pair | Hold the right mouse button during the startup recovery block (`C:TestRMB`) to restore both `.previous` files, then cold boot. |
| Workbench on the native fallback screen | ToolTypes mismatch | Pair `BOARDTYPE=Prometheus` with `SETTINGSFILE=SYS:Devs/Picasso96Settings.9200`; reboot. |
| Bridge unreachable after boot | boot not finished / bridge not running | Use `tools/wait_bridge.sh` (80–100 s, polled), reconnect to `192.168.1.21:2345` explicitly, never a fixed sleep or localhost. |

Hard resets lose `Work:` directory metadata but preserve `DH2:` — keep
diagnostic evidence on `DH2:`.

## 2. Client startup

| Symptom | Cause | Fix |
|---|---|---|
| `could not open minigl_ppc.dll` | client CWD does not contain the DLL; or loader mismatch | `CD` to the directory containing the matched `minigl_ppc.dll` before launch |
| Client hangs before drawing | no host running, or stale port | Start a fresh host; verify `MiniGLPPC.R200.Host.1` in the port list |
| Host exits immediately after start | another host is already alive (`hostrun` is idempotent in effect) | Check for existing host; don't stack them |
| Client fails only after a previous aborted run | aborted run consumed the host | Start a fresh host per run, including after failures |
| `can't find texture data/chrome.ppm` | missing file relative to CWD | `makedir data; copy chrome.ppm data/chrome.ppm` |
| Launch of a fullscreen client after an interruption wedges the machine | in-flight CP work | Run a small probe (`r200_triangle_smoke`, `r200_ib_smoke`) before heavy clients; prefer normal quit |
| ~7.8 FPS, `presents=0` | DLL and host from different builds | Deploy both halves from one build; record both CRCs |
| Very low FPS, wrong ambient numbers | an old/stale deployed pair (e.g. sh-intro 20.4–22.4 vs 63.6) | Rebuild and redeploy the current source; verify CRCs |

## 3. Performance anomalies

| Symptom | Cause | Fix |
|---|---|---|
| Throughput roughly halved after a host rebuild | Sonnet classified the 68k host as PPC (banned literal, e.g. `RACE` in a diagnostic string) | Keep the `tools/check_68k_host.py` step; for the **68k host only** set `(bits & ~0x00030000) \| 0x00020000`, verify bits + CRC |
| Throughput roughly halved | `MGLPPC_HOSTPRI` unset after reboot | `SetEnv MGLPPC_HOSTPRI -1` before every session |
| Console output dominates frame time | `MGLPPC_VERBOSE`/diagnostic logging enabled | Unset; build traces out (`MGLPPC_HOST_TRACE=0`, `MGLPPC_CLIENT_TRACE=0`) |
| Uploads unexpectedly slow | `MGLPPC_UPLOAD_TRACE` left set | Unset it; the option is diagnostic only |
| Silent Hill/texture workload slower than expected | `MGLPPC_BATCH=1` (two-tile deferral regression) | Leave it unset; see [textures.md](textures.md) |
| Windowed 16-bit far slower than fullscreen | known P96 per-pixel conversion (~180 ms/frame) | Never benchmark or target 16-bit windowed; use 32-bit windowed or 16-bit fullscreen |
| FLIP presentation much slower than BLIT | `ChangeScreenBuffer` 22–27 ms/call | Use BLIT (default) unless sync/FLIP is required |
| First upload after idle takes ~20 ms | host was parked in its 1500 ms idle sleep | Keep the host warm (run a probe or avoid long idle before measurement) |
| Publish stalls ~99% of frame | ring space wait: the host is the bottleneck | Attribute before optimizing; see [performance.md](performance.md) |
| Numbers don't reproduce | profiler/quiet mixing, different boot state, different stack CRCs | Follow [benchmarking.md](benchmarking.md) strictly; one variable at a time |

## 4. Protocol and correctness failures

| Symptom | Meaning | Action |
|---|---|---|
| `VERIFY_FAILED` status | cache/ownership verification failed | Treat as a defect; check flush/publish discipline (do not just retry) |
| `STALE_GENERATION` status | resource generation/epoch mismatch | Do not reuse the resource; check device epoch/reset handling |
| Mailbox call times out (5 s) | host did not ack | Session is **poisoned** by design: no retry/abort; recover with lifecycle cleanup and a cold reboot if wedged |
| Ring `error` becomes nonzero | fatal ring/execution error | Host stops consuming; surface a GL error and rebuild state; investigate before rerunning |
| Context creation fails | required driver capability missing | Check the driver pair/interface version; `REQUIRED_CAPS` must all be present |
| GL error after an operation | GL-level reject | Most commonly upload format/size violations (readback: only RGBA/UBYTE, ≤2 MiB) |
| Pixels swapped in channels | big-endian surface/format mismatch | Check the enum translation and canonical BGRA path; run the pixel tests |

## 5. Sonnet loader classification (important, subtle)

Sonnet's `LoadSeg` scans raw hunk bytes for `RACE`, `powerpc.library` and
`AmigaAMP`. A 68k host containing any of them is loaded into PPC memory and
runs ~2x slower (measured: 67.774 FPS versus 140.752 for the identical binary).
The literal `MGLPPC_UPLOAD_TRACE` contains `RACE`, so the host builds that name
at runtime and the build runs `tools/check_68k_host.py` over the linked host.

Rules:

- Never add those strings literally to the host. Prefer runtime construction.
- The protection bits cache classification: for the **68k host only**, after
  replacing a previously misclassified file, set
  `(bits & ~0x00030000) | 0x00020000` and verify bits **and** CRC.
- Never apply that override to genuine PPC clients or the DLL — it can make
  them load as 68k and fail.
- The static proof and unchanged-binary evidence are in
  [reference/HOST_LOADER_REGRESSION.md](reference/HOST_LOADER_REGRESSION.md).

## 6. Amiga file/DOS operations

| Rule | Why |
|---|---|
| Serialize **all** filesystem/DOS/bridge operations | Concurrent requests collide through shared temp files and produce missing/misleading results |
| Deployment names ≤ 16 characters | Longer names alias after truncation and can silently overwrite another build |
| Verify destination CRC32 after every transfer | Equal size ≠ equal build; only CRC identifies a WarpELF |
| Record both ELF and converted CRC | `Elf2Exe2` output differs from its input |
| Verify the **active** path after copy, not the staging path | Staging verification proves nothing about what runs |
| `Avail Flush` after replacing `LIBS:minigl.library` | AmigaOS may keep the old resident copy despite matching disk CRC |
| Prefer normal quit; don't Break + relaunch | In-flight CP work can wedge the machine |
| `Copy` a big log before `Type` | `Type` on a redirected host log can wedge the bridge's 30 s window |
| Pulls run ~19 KB/s | A 900 KiB PPM times out; read a 20-byte header to verify screenshots |
| Dismiss stray requesters after aborted scripts | A "Please insert volume …" requester stalls everything; screenshot and cancel first |
| Use ≥20 iterations for `DateStamp` timings | A 10-iteration loop carries ±2 ms/iteration |

## 7. Deployment checklist

1. Build; record CRCs (ELF and converted).
2. Back up active files to `.pre-<tag>`.
3. Transfer serialized; verify destination CRC.
4. Convert on target with `Elf2Exe2 in.elf out.dll` (one command, full paths).
5. Copy over the active path; verify that path's CRC.
6. 68k host only: reapply classification bits; verify bits + CRC.
7. `LIBS:minigl.library` changed → `Avail Flush`; driver pair changed → cold
   reboot with both files replaced together and `.previous` copies present.
8. Run a small service probe before a heavy client.
9. Record: commit + dirty state, CRCs, driver pair, mode/depth/buffers/sync,
   cold/warm state, exact commands, results.

## 8. Quick diagnostics

- `r200_triangle_smoke` — minimal windowed triangle; the standard "does the
  stack come up" probe.
- `r200_ib_smoke` — indirect-buffer smoke; validates ring/IB wiring without a
  full scene.
- `r200_defect_probe` — isolated defect/perf probe used in sessions.
- `r200_texture_update_test` — pixel-verified texture updates (a single
  pre-existing INDEXED SOURCE failure is known and not a stack regression).
- `MGLPPC_PROFILE` builds — attribute a slowdown before touching code; see
  [benchmarking.md](benchmarking.md#6-profiling-builds-and-attribution).
- Host counters at shutdown (`CP_RUN dispatches/dwords/failed`, profile
  buckets) — the first place to look when the ring or CP path misbehaves.

When in doubt, change one variable, re-run the standard workload, and compare
against a recorded stack identity (commit + CRCs).
