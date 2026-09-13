# Using `minigl_ppc.dll` (R200 WarpOS stack) — client guide

This document explains how to run a program linked against the R200
`minigl_ppc.dll` and how to build new PPC clients. It covers the
hardware-validated stack deployed to `DH2:ibdraw/` on the Amiga 4000
(Prometheus + Radeon 9200 + WarpOS PPC).

## 1. What the pieces are

| Artifact | Role |
|---|---|
| `minigl_ppc.dll` | The WarpOS PPC DLL (e.g. `DH2:ibdraw/minigl_ppc.dll`). Carries the R200 semantic frontend; talks over shared memory to the 68k host. |
| `minigl_ppc_host` | 68k helper (port `MiniGLPPC.R200.Host.1`). Validates the SMR, opens the 68k `minigl.library` R200 backend, and performs CP submission/fences/present. This **must be running** before the client starts. |
| `minigl.library` (`LIBS:`) | 68k v12+ direct-R200 backend on `Radeon9200.chip`/`Radeon3D.library` (installed in `LIBS:Picasso96/`). |
| `libminigl.a` | Client-side import archive. Provides `MiniGLOpen()`/`MiniGLClose()` and the Hyperion-loader glue. `MiniGLBase` is the DLL instance handle, not an Amiga `struct Library *`. |

Client flow: your program → `include/proto/minigl.h` inline wrappers →
import `libminigl.a` → Hyperion loader finds/loads the DLL →
`MGLGetDispatchTable()` → R200 frontend → SMR → 68k host → Radeon3D.

## 2. Runtime requirements (per session)

1. **Cold-booted machine** with the Picasso96 pair installed:
   `LIBS:Picasso96/Prometheus.card` + `LIBS:Picasso96/Radeon9200.chip`
   (Workbench on the Radeon), and `LIBS:minigl.library` present.
   After replacing `minigl.library` run `Avail Flush` before launching.
2. **`C:InitPPC` exactly once per boot** before any WarpOS/PPC client.
   Do not run it again for each program; a wedged PPC client needs a
   cold reboot, not another `InitPPC`.
3. **Start the host before the client**:
   ```
   Run >NIL: Execute DH2:ibdraw/hostrun
   ```
   The host exits when a client calls `MiniGLClose()` normally.
   **Start a fresh host for every client run.**
4. Console output from the DLL is gated behind `MGLPPC_VERBOSE`
   (SetEnv before launch). Normal runs leave it unset — console writes
   dominate frame time. `MGLPPC_UPLOAD_TRACE` must stay unset (it
   *halves* throughput when set because it also perturbs Sonnet's
   loader classification checks).

A wedged PPC client ignores Exec break signals: recover with a cold
power cycle. A warm reboot does not reliably reset the Radeon.

## 3. Writing a client

Include only the public headers:

```c
#include <proto/minigl.h>   /* MiniGLOpen/MiniGLClose + GL calls  */
```

`MiniGLOpen()`/`MiniGLClose()` come from the import archive.
Standard entry-point example (see `examples/r200_triangle_smoke.c`):

```c
#include <proto/minigl.h>

int main(void)
{
    if (!MiniGLOpen())                       return 20;
    mglChooseWindowMode(GL_TRUE);            /* GL_FALSE = fullscreen */
    mglChoosePixelDepth(16);                 /* 15/16/24/32           */
    mglChooseVertexBufferSize(1000);
    if (!mglCreateContext(0, 0, 640, 480)) { MiniGLClose(); return 20; }

    glViewport(0, 0, 640, 480);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glColor4ub(255, 64, 64, 255); glVertex3f(-0.75f, -0.65f, 0.0f);
    glColor4ub(64, 255, 96, 255); glVertex3f( 0.75f, -0.65f, 0.0f);
    glColor4ub(64, 128, 255,255); glVertex3f( 0.0f,  0.75f, 0.0f);
    glEnd();
    glFinish();
    mglSwitchDisplay();

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
```

Rules for clients:

- Dispatch clients must use `include/mgl/gl.h` via
  `MINIGL_DISPATCH_DECLARATIONS` and never import private
  context/vertex layouts.
- Always quit through the normal path (`mglDeleteContext()` +
  `MiniGLClose()`). Do not `Break` a fullscreen client and launch
  another straight after — run a small smoke probe first.
- Intentional no-ops/limits: `glClientActiveTextureARB`,
  `mglResizeContext`, `mglEmergencyCloseScreen`,
  `glMGLForceNewRenderPass`; `glMultiDrawArrays` loops
  `GLDrawArrays` internally.

Existing complete examples: `examples/r200_triangle_smoke.c`,
`examples/milestone_gears_stats_library.c` (supports `-frames N`
auto-exit and prints a `GEARS_RUN frames=... fps=...` summary),
`examples/r200_texture_update_test.c`, `examples/r200_bind_bench.c`.

## 4. Building on the Linux host

Toolchain: `ppc-amigaos-*` tools plus the MOS2WOS WarpOS SDK overlay
(defaults live in `Makefile.wos.gcc.common`).

```sh
# import archive + example WarpELFs
make -f Makefile.import.gcc              # lib/libminigl.a (+ -dispatch_ppc.a)
make -f Makefile.import.gcc testprograms # bin/examples/*_ppc.warpelf
```

For a standalone program, mimic the implicit rule:

```sh
PPC_CC=ppc-amigaos-gcc   # plus WOS_ARCH_CFLAGS/WOS_CPPFLAGS from Makefile.wos.gcc.common
PPC_LD=ppc-amigaos-ld
$PPC_CC -mcpu=604e -O2 -Wall -Iinclude -Ihyperion_dll -c myprog.c -o myprog.o
$PPC_LD $(WOS_LINK_FLAGS) -o myprog_ppc.warpelf \
    $(WOS_STARTFILE) myprog.o lib/libminigl.a \
    $(WOS_RUNTIME_LIBS) $(WOS_ENDFILE)
```

The produced `.warpelf` is ET_REL and must be converted to `.warpexe`/
loaded via WarpOS paths **on the Amiga**, because `Elf2Exe2` is
target-hosted. Link-contract requirements: `warpos-etrel.ld`,
`-fno-jump-tables`, `-d`, `-x`; `_start` at `.text+0`; no
undefined/COMMON symbols; only `R_PPC_ADDR32`, `R_PPC_ADDR16_LO`,
`R_PPC_ADDR16_HA`, `R_PPC_REL24` relocations.

## 5. Deployment (host ↔ Amiga)

- Serialize **all** Amiga filesystem/DOS operations (bridge requests
  collide otherwise). Never transfer/checksum in parallel.
- Destination names ≤ 16 characters; verify CRC32 of both ends after
  every transfer (truncation aliases can silently overwrite another
  test build).
- Utilities in WosKit `bin/r200-package/` are copies; never edit them.
- Deploy the DLL as ZIP:
  ```
  UnZip -o mglr200.zip -d DH2:ibdraw
  ```
  then CRC-verify `minigl_ppc.dll` and `minigl_ppc_host` on the Amiga.
- The 68k `minigl_ppc_host` may additionally need its protection bits
  fixed after replacement (Sonnet classifier fix):
  `(prot & ~0x00030000) | 0x00020000` — **68k host only**; never apply
  to PPC clients/DLLs. Verify bits as well as CRC.

## 6. Validated baseline

Full-screen precalc gears, 640×480×16, 3 buffers, in-flight BLIT
present, sync off: **134.529 FPS** (median-of-three protocol).
The optional PPC-side CP-emitter path (windowed 800×600×32) reached
68.337 FPS. 16-bit windowed presentation is known-slow — exclude it
from performance comparisons. A successful cross-build is *not*
deployment validation: record commit, CRCs, driver pair, CPU, mode,
windowed/fullscreen, cold/warm state, and every test you ran.

The complete step-by-step benchmarking procedure (boot protocol,
per-run host lifecycle, exact commands, known pitfalls) lives in
[`how_to_benchmark.md`](../how_to_benchmark.md) — follow it instead
of reconstructing the path.

## 7. What was wrong, and what PPC app builds must remember

Two independent regressions cost ~half the frame rate each:

1. **68k host misclassified as PPC.** Sonnet `LoadSeg` scans raw hunks
   for `RACE`, `powerpc.library`, `AmigaAMP`. The literal
   `MGLPPC_UPLOAD_TRACE` contains `RACE`, so the 68k host was loaded
   into PPC memory. Unchanged host `D0406387`: 67.774 FPS as PPC vs
   140.752 FPS as normal 68k. Fixed by constructing that option name
   at runtime plus `tools/check_68k_host.py`.
2. **PPC waits parked too long.** DOS `Delay(1)` quantizes to 50 Hz;
   measured `WaitTime(0,100)` averaged ~20.3 ms on this kernel.
   Fixed with a bounded 131,072-iteration polling budget before
   yielding; timeouts, epochs, retirement and ownership checks kept.

PPC app checklist:

- Build with `ppc-amigaos-*` + MOS2WOS overlay via
  `Makefile.import.gcc`; keep the ET_REL contract
  (`warpos-etrel.ld`, `-fno-jump-tables`, `-d -x`, `_start` at
  `.text+0`, no undefined/COMMON, only supported `R_PPC_*` relocs).
- Convert WarpELF to DLL/EXE with target-hosted `Elf2Exe2`; deploy
  as ZIP, CRC-verify both ends — equal size does not mean equal build.
- Never force 68k-host protection-bit fixes onto PPC DLLs/clients.
- No 68k `OpenLibrary()` per GL call; `MiniGLBase` is a Hyperion DLL
  handle. Use public `proto/minigl.h` + dispatch declarations only.
- `InitPPC` once per boot; fresh 68k host per client run; run the
  client from the directory containing the matched DLL.
- Keep `MGLPPC_VERBOSE`/`MGLPPC_UPLOAD_TRACE` unset and trace
  loggers compiled out for benchmarks.
- Never `Delay(1)` in a hot back-pressure loop; poll bounded-first,
  then yield, and measure actual elapsed ticks — not the requested
  timeout.
- Serialize Amiga file operations; destination names ≤ 16 chars;
  verify CRC and protection bits after every transfer.
