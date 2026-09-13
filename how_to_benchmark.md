# How to benchmark the WarpOS PPC R200 stack (physical Amiga)

This is the exact, hardware-validated procedure for running the MiniGL
R200 gears benchmark on the physical Amiga 4000 (MPC7410, Prometheus +
Radeon 9200). Follow it top to bottom; every step below exists because
skipping it produced a wrong number, a stalled run, or a wedged machine.

Reference numbers for the fixed workload (single samples from the
validated sessions, see `backend_r200/HOST_LOADER_REGRESSION.md`):

| Workload | Command below | FPS |
| --- | --- | ---: |
| Fullscreen 640x480x16, 3 buffers, sync off | `gears-baseline` | **134.529** |
| Windowed 800x600x32, CP emitter, sync off | `benchrun` | 52–68 |

These are one sample per condition, not medians. Performance claims
require three independent cold-boot runs; report the median and every
sample.

## 0. Known-good deployed stack

Benchmarking is only meaningful against a verified deployment. Check
CRC32 of every artifact before the first run of a session (bridge
`amiga_checksum` / any CRC tool, serialized, one file at a time):

| File | CRC32 | Bytes |
| --- | --- | ---: |
| `Work:ibdraw/minigl_ppc.dll` | `02605851` | 294768 |
| `Work:ibdraw/mglhost` | `0FAA4EFA` | 40588 |
| `LIBS:minigl.library` | `62343673` | 127252 |
| `LIBS:Picasso96/Radeon9200.chip` | `CEB838A6` | 77940 |
| `LIBS:Picasso96/Prometheus.card` | `18A453D6` | 7796 |
| `Work:ibdraw/precalc` | `1F3401F7` | 187136 |

Plus:

- `Work:ibdraw/mglhost` protection bits must read `00020000`
  (normal 68k). The Sonnet loader caches classification in the
  protection bits; a CRC match alone does not prove fast loading.
  Never "fix" bits on the DLL or PPC clients — 68k host only.
- `Work:ibdraw/chrome.ppm` present (envmap texture for gears).
- Driver pair installed as the active matched set
  (`Radeon9200.chip` + `Prometheus.card`), Workbench on the Radeon.

If you replaced `LIBS:minigl.library`, run `Avail Flush` before the
first client. AmigaOS otherwise keeps the old resident copy and you
benchmark stale code while all disk CRCs match.

## 1. Boot protocol (once per boot)

1. Cold power cycle. A warm reboot does not reliably reset the Radeon;
   after any grey screen/guru/hang only a cold cycle counts.
2. Wait for the bridge (boot takes 80–100 s; poll, do not sleep a
   fixed time):
   ```
   /home/mirek/minigl_ppc/tools/wait_bridge.sh
   ```
3. Reconnect explicitly to the physical endpoint — localhost defaults
   fail:
   ```
   amiga_connect(host="192.168.1.21", port=2345)
   ```
4. Verify a clean process/port state (`Status`, port list): no
   leftover `mglhost`, no GL client, no `MiniGLPPC.R200.Host.1` port.
5. Run `C:InitPPC` **exactly once per boot**, before the first
   WarpOS/PPC client. Never run it again during the same boot; a
   wedged PPC client is recovered by a cold reboot, not another
   `InitPPC`.

## 2. Per-run protocol (every client run)

1. Benchmark environment, explicitly:

   ```
   SetEnv MGLPPC_HOSTPRI -1
   ```

   Keep `MGLPPC_VERBOSE`, `MGLPPC_UPLOAD_TRACE`, `MGLPPC_CPEMIT`,
   `MGLPPC_IBSMOKE` and friends **unset** (fresh boot = already unset;
   check with `GetEnv` if unsure). Console.device writes and upload
   traces dominate frame time and will poison the measurement. Trace
   loggers must be compiled out of the binaries (see `a26cc1a`).

2. **Start a fresh 68k host before every client run:**

   ```
   Run >NIL: Execute Work:ibdraw/hostrun
   ```

   The host exits when the client closes its context — including
   failed/aborted client runs (missing data file, DLL open failure).
   If you forget this, the next client reports
   `could not open minigl_ppc.dll` or hangs on the absent port.
   Verify the port came up before launching the client:
   `MiniGLPPC.R200.Host.1` must appear in the port list.

3. **Run the client with the DLL directory as the current
   directory** — the Hyperion loader resolves `minigl_ppc.dll` from
   the client's CWD:

   ```
   CD Work:ibdraw
   ```

4. Gears needs its envmap texture at `data/chrome.ppm` *relative to
   the CWD*, even though no `-envmap` flag is passed for the baseline
   workload. One-time setup:

   ```
   CD Work:ibdraw
   makedir data
   copy chrome.ppm data/chrome.ppm
   ```

   Missing file symptom: `milestone_gears_stats_library: can't find
   texture data/chrome.ppm`, and the aborted run consumes the host.

## 3. The benchmark commands

Fullscreen reference workload (the ~134 FPS number):

```
CD Work:ibdraw
precalc -frames 600 -width 640 -height 480 -depth 16 -buffers 3 -nosync -nofps >Work:ibdraw/baselineN.log
Type Work:ibdraw/baselineN.log
```

Windowed CP-emitter workload (needs a CP-capable stack and
`MGLPPC_CPEMIT 1`; see `backend_r200/CP_EMITTER.md`; the existing
script runs 800x600x32 windowed):

```
Execute Work:ibdraw/benchrun
```

Read the `GEARS_RUN frames=... fps=...` summary line from the log. It
is printed by the `-frames N` auto-exit path and is independent of
console rendering cost. `error=0` is mandatory; discard any run with
a GL error or a wedged teardown and re-run after recovery.

Do not use 16-bit *windowed* presentation as a throughput target
(known-slow path, ~3 FPS). Fullscreen 16-bit with 3 buffers is the
reference; 1-buffer fullscreen shows scanout banding (visual only).

## 4. Run hygiene (rules that produced bad data when violated)

- Serialize ALL Amiga filesystem/DOS operations. Concurrent bridge
  requests collide through shared temp files and produce missing or
  misleading results. One transfer/checksum/command at a time.
- Amiga-side deployment names at most 16 characters; verify
  destination CRC32 after every transfer (truncated names can alias
  and silently overwrite another build; equal size != equal build).
- Change one variable at a time. The comparison set is: same boot
  protocol, same stack CRCs, same env, same command line.
- Between isolation runs: let the client exit through its normal quit
  path. Never `Break` a fullscreen GL client and immediately launch
  another — in-flight CP work can wedge the machine. After any
  interruption, run a small bootstrap/service probe before the next
  heavy client.
- After each run confirm clean state (`Status`, no host port) before
  starting the next one.
- A successful cross-build is not deployment validation. Record:
  commit + dirty state, artifact CRCs, backend/driver pair, CPU and
  board, mode/depth, windowed/fullscreen, buffers, sync state,
  cold/warm boot state, and the exact commands run.

## 5. Recovery

- Wedged PPC client (ignores break signals, no output): operator cold
  power cycle. Then repeat section 1. Do not `Break` + relaunch.
- Degraded windowed presentation after a crash while simpler tests
  pass: the Radeon was not reset — only a cold cycle fixes it.
- Bridge unreachable after boot: re-check `Status` via a fresh
  connection; the bridge (`c:amiga-bridge`) must be running; use
  `wait_bridge.sh`, not fixed sleeps.

## 6. Where the reference scripts live

All on the Amiga in `Work:ibdraw/` (evidence retained from the
validated sessions):

| Script | Purpose |
| --- | --- |
| `hostrun` | Start the 68k host (`mglhost >host.log`) |
| `benchrun` | Windowed 800x600x32 CP gears run into `run.log` |
| `precalc` | The fixed fullscreen reference client binary |
| `gears` | The non-precalc variant used by `benchrun` |

The logs (`*-client.log`, `*-host.log`, `baseline*.log`, `run.log`)
are append-evidence; do not delete them when re-running — write new
files instead.
