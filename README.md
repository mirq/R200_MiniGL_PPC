# MiniGL V19 for WarpOS/PPC — direct-R200 stack

MiniGL is an OpenGL-1.x-style 3D API for Amiga systems: a small `gl*`/`mgl*`
surface, one global current context, and a positional **dispatch table**
instead of per-call library opening. This repository builds the **WarpOS/PPC
implementation** of MiniGL V19 for the Amiga 4000 (68060) + Prometheus +
Radeon 9200 + PMC MPC7410/PPC configuration.

The primary product is the **direct-R200 stack**: a PPC DLL (`minigl_ppc.dll`)
that carries the GL semantics and talks over shared memory to a 68k helper
(`minigl_ppc_host`), which drives `Radeon9200.chip` through its bounded
Radeon3D service. There is **no Warp3D driver for the R200**; that absence is
why this stack exists.

```
 PPC client (libminigl.a)
   -> inline wrappers -> MGLDispatchTable     (one indirect call, no 68k transition)
   -> minigl_ppc.dll                          (semantic frontend, PPC)
   -> ring / mailbox / Exec messages          (shared memory, big-endian)
   -> minigl_ppc_host                         (68k helper, port MiniGLPPC.R200.Host.1)
   -> LIBS:minigl.library                     (68k direct-R200 backend)
   -> Radeon3D service in Radeon9200.chip      -> RV280
```

## What is in the box

| Artifact | Runs on | Role |
|---|---|---|
| `minigl_ppc.dll` | PPC/WarpOS | The GL implementation: state, matrices, lighting, batching, command generation |
| `minigl_ppc_host` | 68k | Executes semantic batches, manages fences/present, answers queries |
| `LIBS:minigl.library` | 68k | Direct-R200 backend (built in `classic_68k/`) |
| `LIBS:Picasso96/{Radeon9200.chip,Prometheus.card}` | 68k | Matched driver pair (separate `~/p96-driver` tree) |
| `libminigl.a` | PPC | Client import archive: `MiniGLOpen`/`MiniGLClose` + Hyperion loader |
| `mglQ3` / `Makefile.dll.gcc` | PPC | Legacy alternate DLL over Warp3D (not the R200 path) |

## Quick start (client)

```c
#include <proto/minigl.h>

int main(void)
{
    if (!MiniGLOpen()) return 20;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 640, 480)) { MiniGLClose(); return 20; }

    glViewport(0, 0, 640, 480);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* ... draw ... */
    glFinish();
    mglSwitchDisplay();

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
```

Per session: cold boot with the matched driver pair, `C:InitPPC` **once**, then
start a **fresh host** and run the client from the directory containing the
matched `minigl_ppc.dll`. Full details:
[docs/client-guide.md](docs/client-guide.md).

## Quick start (build, Linux host)

```sh
# PPC DLL (ET_REL; final .dll conversion is target-hosted Elf2Exe2)
make -f Makefile.dll.r200.gcc warpelf

# 68k host (also builds the 68k library via R200_NATIVE_ROOT)
make -f Makefile.dll.r200.gcc host

# client import archive + examples
make -f Makefile.import.gcc
make -f Makefile.import.gcc testprograms
```

Build system, targets and the ET_REL contract:
[docs/building.md](docs/building.md).

## Versions and compatibility

```
MINIGL_VERSION               29
MINIGL_MIN_VERSION           29
MINIGL_DISPATCH_ABI_VERSION   5
sizeof(MGLDispatchTable)     0x354
```

The dispatch ABI follows upstream minigl.library **29.1**: slots 1..205 are
upstream's layout field for field (the released WarpOS V10 layout is still
the frozen prefix through `GLPolygonOffset`), the GL tokens carry OpenGL's own
values, and the FastPath V2 entries follow as slots 206..208. New since V19:
fixed-function lighting, display lists, GLU (quadrics, `gluBuild2DMipmaps`),
GLUT, framebuffer-to-texture copies, `glLineWidth`, the GL 1.1 query and edge
flag entry points and `mglCreateContextFromWindow`/`FromBitMap`; the per-slot
status for each DLL is in [docs/dispatch-abi.md](docs/dispatch-abi.md).
`MiniGLOpen` requires the ABI to match exactly, so V19 (ABI 3) clients must be
rebuilt. The Silent Hill binary insists on ABI 3 and needs an ABI-3 DLL in
its own directory. Full ABI and synchronization rules:
[docs/dispatch-abi.md](docs/dispatch-abi.md).

## Design rules

- **One indirect call per GL call**, no 68k transitions in the hot path.
- **Bounded resources**: two 8192-dword submit slots, an 8 x 8192-dword ring,
  four vertex segments, ≤2 MiB texture staging, no per-frame heap churn.
- **The PPC never touches PCI, MMIO, the CP ring or VRAM.** Only the driver
  service does; caps are negotiated at context creation.
- **Both CPUs are big-endian**; byte swapping is confined to GPU-consumed
  payloads at the aperture boundary.
- **Diagnostics are opt-in**; console/trace logging is compiled out of release
  builds because it dominates frame time.

## Backends

- **direct-R200 (primary)** — PPC frontend + 68k host + `Radeon9200.chip`
  (`Makefile.dll.r200.gcc`). Protocol 15, ring control v6, mailbox v1.
- **legacy mglQ3/Warp3D** — shared GL core over `Warp3DPPC.library`
  (`Makefile.dll.gcc`). Same dispatch ABI and client flow, different rendering
  architecture; not used on the R200 target.
- **stub** — backend-free diagnostic DLL that logs every call
  (`Makefile.dll.stub.gcc`).

Internals: [docs/frontend-r200.md](docs/frontend-r200.md),
[docs/transport-protocol.md](docs/transport-protocol.md),
[docs/host-68k.md](docs/host-68k.md), [docs/textures.md](docs/textures.md).

## Performance at a glance

All on the physical machine; numbers depend on stack CRCs and boot state and
are catalogued with conditions in [docs/performance.md](docs/performance.md):

| Workload | Result |
|---|---:|
| Precalc gears, fullscreen 640x480x16, 3 buffers, sync off | 144.927 FPS (current source, warm boot) |
| Same, CP-emitter mode, fullscreen | 174.4–182.9 FPS (3-run medians) |
| Windowed 800x600x32, CP emitter | 68.3–69.4 FPS |
| Texture upload suite | 44.642 FPS |
| Quake room walk, 800x600x32 | 59.405 FPS |

Measured cycle-level data (aperture store ~27 cycles/4 B, AltiVec serialization
−14% to −16% ticks, 68060 copy floor ~11.6 cycles/dword, `WaitTime` ~20.3 ms
quantum) and the full regression catalogue are in
[docs/performance.md](docs/performance.md). Benchmark procedure:
[docs/benchmarking.md](docs/benchmarking.md).

## Documentation

Start at [docs/README.md](docs/README.md). It indexes:

- [overview](docs/overview.md) and [architecture](docs/architecture.md)
- [building](docs/building.md), [dispatch ABI](docs/dispatch-abi.md),
  [client guide](docs/client-guide.md)
- internals: [frontend](docs/frontend-r200.md),
  [transport](docs/transport-protocol.md), [68k host](docs/host-68k.md),
  [textures](docs/textures.md)
- [performance](docs/performance.md) and [benchmarking](docs/benchmarking.md)
- [testing](docs/testing.md) and [troubleshooting](docs/troubleshooting.md)
- deep reference: [mailbox](docs/reference/MAILBOX_V1.md),
  [CP emitter](docs/reference/CP_EMITTER.md),
  [host loader regression](docs/reference/HOST_LOADER_REGRESSION.md)

`docs/` must be kept current: any change committed/pushed to the remote updates
the affected documents in the same change set (see `AGENTS.md`).

## Validation status

Binary validation is cross-build plus hardware runs on the physical A4000.
There is no emulator path for the R200 backend. A clean build is necessary but
not sufficient: record commit + dirty state, artifact CRCs, driver pair, mode,
depth, buffers, sync, cold/warm state, and the exact tests run.
