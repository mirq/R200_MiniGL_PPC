# Client guide — running and writing PPC clients

This guide covers the direct-R200 stack: `minigl_ppc.dll` +
`minigl_ppc_host` + 68k `minigl.library` + `Radeon9200.chip` on the physical
A4000. Read [troubleshooting.md](troubleshooting.md) before deploying anything
new.

## 1. The pieces

| Artifact | Role |
|---|---|
| `minigl_ppc.dll` | WarpOS PPC DLL carrying the R200 semantic frontend. Place beside the client; the Hyperion loader resolves it from the client's current directory. |
| `minigl_ppc_host` | 68k helper on port `MiniGLPPC.R200.Host.1`. Must be running before the client starts. One host per client run. |
| `LIBS:minigl.library` | 68k direct-R200 MiniGL backend (classic tree build). |
| `LIBS:Picasso96/Radeon9200.chip` + `Prometheus.card` | Matched driver pair; Workbench on the Radeon. |
| `libminigl.a` | Client import archive: `MiniGLOpen`/`MiniGLClose` + Hyperion loader. |

Flow: client → inline wrappers → import archive → Hyperion loader →
`MGLGetDispatchTable()` → R200 frontend → shared memory → 68k host →
`minigl.library` → Radeon3D → RV280.

## 2. Writing a client

Include the public header only:

```c
#include <proto/minigl.h>   /* MiniGLOpen/MiniGLClose + all gl*/mgl* calls */
```

Skeleton (see `examples/r200_triangle_smoke.c` for the full version):

```c
#include <proto/minigl.h>

int main(void)
{
    if (!MiniGLOpen())
        return 20;

    mglChooseWindowMode(GL_TRUE);       /* GL_FALSE = fullscreen */
    mglChoosePixelDepth(16);            /* 15/16/24/32            */
    mglChooseVertexBufferSize(1000);
    if (!mglCreateContext(0, 0, 640, 480)) {
        MiniGLClose();
        return 20;
    }

    glViewport(0, 0, 640, 480);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor4ub(255, 64, 64, 255);  glVertex3f(-0.75f, -0.65f, 0.0f);
    glColor4ub(64, 255, 96, 255);  glVertex3f( 0.75f, -0.65f, 0.0f);
    glColor4ub(64, 128, 255, 255); glVertex3f( 0.0f,  0.75f, 0.0f);
    glEnd();
    glFinish();
    mglSwitchDisplay();

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
```

Rules:

- Dispatch clients must use `include/mgl/gl.h` through
  `MINIGL_DISPATCH_DECLARATIONS`; never import the private context/vertex
  layouts from `mglQ3/include/mgl/`.
- Always quit through the normal path (`mglDeleteContext()` then
  `MiniGLClose()`), which detaches transport and shuts the host down. Do not
  `Break` a fullscreen client and immediately launch another; in-flight CP work
  can wedge the machine.
- Build with the flags from [building.md](building.md); the ET_REL contract is
  not optional.
- Check the return value of `mglBeginFastPath` before using the FastPath API.

### Intentional no-ops and limits

| Call | Behaviour |
|---|---|
| `glClientActiveTextureARB` | no-op (no independent client-active selector) |
| `mglResizeContext` | no-op (legacy fullscreen resource path not exposed) |
| `mglEmergencyCloseScreen` | no-op |
| `glMGLForceNewRenderPass` | no-op |
| `glMultiDrawArrays` | loops `GLDrawArrays` internally |
| `mglGetInputWindowHandle` | returns the ordinary window handle |
| `glGetString(GL_RENDERER)` | backend-specific string; do not parse it for caps |

## 3. Runtime requirements (every session)

1. **Cold boot** with the matched Picasso96 pair installed; Workbench on the
   Radeon. Monitor ToolTypes: `BOARDTYPE=Prometheus` +
   `SETTINGSFILE=SYS:Devs/Picasso96Settings.9200`.
2. **`C:InitPPC` exactly once per boot**, before any WarpOS/PPC client. Do not
   run it again; a wedged PPC client is recovered with a cold reboot.
3. **Start a fresh 68k host before every client run** (it exits when the client
   closes its context, including aborted runs):
   ```
   Run >NIL: Execute Work:ibdraw/hostrun
   ```
   Verify `MiniGLPPC.R200.Host.1` appears in the port list before launching.
4. **Run the client from the directory containing the matched
   `minigl_ppc.dll`.**
5. `SetEnv MGLPPC_HOSTPRI -1` for throughput runs (the host must be higher
   priority than the client). It is volatile; a reboot clears it.
6. If `LIBS:minigl.library` was replaced, run `Avail Flush` before the first
   client (AmigaOS may keep the old resident copy).
7. Keep diagnostic env vars unset for real runs (see below).

### Environment variables

| Variable | Side | Effect |
|---|---|---|
| `MGLPPC_VERBOSE` | PPC | enables DLL console output (costly; off by default) |
| `MGLPPC_UPLOAD_TRACE` | host | host upload trace; **never set for performance** (also historically caused Sonnet misclassification) |
| `MGLPPC_HOSTPRI` | host | host task priority; `-1` recommended |
| `MGLPPC_NODIRECT` | host | disables direct texture path (diagnostic) |
| `MGLPPC_NOMAILBOX` | host | disables mailbox (diagnostic) |
| `MGLPPC_IBDRY` | host | dry-run IB smoke (diagnostic) |
| `MGLPPC_FENCE_GROUP` | host | fence coalescing group size override |
| `MGLPPC_CPEMIT` | PPC | enable PPC CP-emitter mode (needs matching host/driver) |
| `MGLPPC_IBSMOKE` | PPC | IB smoke probe (not with CPEMIT) |
| `MGLPPC_BATCH` | PPC | opt-in batched two-tile texture upload (currently slower, see [textures.md](textures.md)) |
| `MGLPPC_BATCHDIAG` | PPC | batch diagnostics (profile builds) |
| `MGLPPC_NOALTIVEC` / `MGLPPC_ALTIVEC` / `MGLPPC_VERIFY` | PPC | AltiVec serialization override/verification |
| `MGLPPC_PROFILE` | both | profiler buckets/counters (profile builds) |
| `MGLPPC_PACING` | host | present histogram (profile builds) |
| `MGLPPC_LOG` / `MGLPPC_LOGFILE` | PPC | DLL console/file redirection; the loader defaults to `NIL:` |

`SetEnv NAME ""` leaves an empty variable that behaves as off but is not
"not found" — verify with `GetEnv` before a quiet run.

## 4. Build and deploy (quick reference)

Build (see [building.md](building.md) for details):

```sh
make -f Makefile.dll.r200.gcc warpelf
make -f Makefile.dll.r200.gcc host
make -f Makefile.import.gcc testprograms
```

Deploy:

1. Build on Linux; record CRC32 of the `.warpelf` **and** of the client.
2. Push via ZIP; verify destination CRC32 (names ≤ 16 characters).
3. Convert on target: `Elf2Exe2 in.elf out.dll` (full paths, one command).
   The converted file has a different CRC — record it.
4. Back up the active files, then replace and verify the **active** path CRCs.
5. 68k host only: after replacing, set protection bits
   `(bits & ~0x00030000) | 0x00020000` and read both bits and CRC back. Never
   apply this to PPC DLLs or PPC clients.
6. `Avail Flush` if `LIBS:minigl.library` changed.
7. Cold reboot if a driver (`Radeon9200.chip`/`Prometheus.card`) changed.

Serialize **all** Amiga filesystem/DOS operations; parallel bridge requests
collide through shared temp files.

## 5. Examples shipped in this repo

| Example | What it shows |
|---|---|
| `examples/r200_triangle_smoke.c` | smallest windowed triangle; use as a service probe |
| `examples/r200_legacy_smoke.c` | legacy ABI-3 client path |
| `examples/milestone_gears_stats_library.c` | gears with `-frames N` auto-exit and `GEARS_RUN` summary |
| `examples/milestone_gears_stats_library.c` + `-DMGL_GEARS_PRECALC=1` | precalculated gears (benchmark reference workload) |
| `examples/r200_texture_update_test.c` | pixel-verified texture update acceptance |
| `examples/r200_bind_bench.c`, `r200_mapping_bench.c` | bind/aperture micro-benchmarks |
| `examples/minigl_testsuite.c` | the full hardware test suite (see [testing.md](testing.md)) |
| `examples/quake_*.h` | Quake-derived test payloads used by the testsuite |

## 6. Benchmarks and probes

- Throughput runs follow [benchmarking.md](benchmarking.md) exactly: fresh
  host per run, `MGLPPC_HOSTPRI=-1`, `-frames N`, read the
  `GEARS_RUN frames=... fps=...` line, `error=0` mandatory.
- Do not use 16-bit *windowed* presentation as a throughput target (known-slow;
  see [performance.md](performance.md)).
- After an aborted or interrupted client, run a small probe
  (`r200_triangle_smoke` or `r200_ib_smoke`) before a heavy workload.

## 7. When something fails

- Client says `could not open minigl_ppc.dll`: no DLL in the current directory,
  or the DLL/loader mismatch. Check the CWD and the DLL CRC.
- Client hangs before drawing: the host is not running (or the port is stale).
  Check the port list; start a fresh host.
- Everything is at ~7.8 FPS with `presents=0`: DLL and host builds are mixed
  (stale pair). Deploy both from the same build; see
  [troubleshooting.md](troubleshooting.md).
- A wedged PPC client ignores break signals: operator cold power cycle.

Full symptom/cause tables are in [troubleshooting.md](troubleshooting.md).
