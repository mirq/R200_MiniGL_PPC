# PiStorm3D Repository Notes

## Build Boundaries

- There is no root build orchestrator. Run each makefile from the directory that contains it; their paths are relative to that directory.
- `mglQ3/src/` and `mglQ3/include/` build the directly linked MiniGL archive. Use `make -C mglQ3 -f makefile`; it produces `mglQ3/libmgl-q3.a`. Override `CPU`, `OBJDIR`, or `LIBNAME` on the command line when needed.
- `mglQ3/lib/libmgl.a` is a different, legacy output used by the classic shared-library backend. Its builder is `make -C mglQ3/Build_gcc -f makefile lib`, but that makefile contains host-specific `/mnt/d/amiga-gcc2` paths. Do not assume rebuilding `mglQ3/libmgl-q3.a` updates it.
- `dev/MiniGL_Library_Source_Code/` owns the `minigl.library` wrapper, backend dispatch tables, client import stubs, and library examples. Its makefiles consume the headers in its own `include/` tree.
- `dev/MiniGL_Library_Source_Code/AGENTS.md` contains the detailed V10 dispatch ABI, R200, MiniGLPPC/WarpOS, physical-device, and benchmarking knowledge migrated from the standalone development tree. Its relative paths are rooted in `dev/MiniGL_Library_Source_Code/`.
- `dev/include/`, `dev/lib/`, root `bin/`, and root `demos/` are packaged release copies/artifacts. The makefiles do not update them; explicitly synchronize intended release outputs after source builds.

## Commands

- Classic shared library: `make -C dev/MiniGL_Library_Source_Code -f Makefile_dispatch_classic.gcc all importlib testprograms`.
- Call-dispatch stub library: `make -C dev/MiniGL_Library_Source_Code -f Makefile_dispatch_stub.gcc all importlib testprograms`.
- PiStorm3D shared library: `make -C dev/MiniGL_Library_Source_Code -f Makefile_dispatch.gcc all importlib testprograms`. The vendored headers are present, but `third_party/pistorm3d/libminiglv3d.a` is not; provide `PISTORM3D_LIB=...` from a full backend checkout.
- R200 shared library: first run `make -C dev/MiniGL_Library_Source_Code -f Makefile_dispatch_r200.gcc dispatchtable`, then run `make -C dev/MiniGL_Library_Source_Code -f Makefile_dispatch_r200.gcc all importlib testprograms`.
- vbcc and WarpOS/PPC builds are owned by `Makefile_dispatch*.vbcc` and `Makefile_warpos.vbcc` in `dev/MiniGL_Library_Source_Code/`; preserve their backend-specific ABI and CPU flags.
- Build one GCC 6.5 demo with `make -C mglQ3/demos -f Makefile.gcc65 <name>` (for example, `gears`). Override `CROSS` if the toolchain prefix is not `/opt/amiga/bin/m68k-amigaos-`.
- Clean and build in separate invocations, for example `make -C mglQ3 -f makefile clean && make -C mglQ3 -f makefile`; `make clean all` can decide that existing outputs are already current before deleting them.
- Builds require the `m68k-amigaos-*` cross-tools and an Amiga SDK. The migrated shared-library makefiles preserve distinct backend ABIs: classic/stub use hard-float and `-noixemul`, PiStorm3D uses clib2 with 68020/68881, and R200 is freestanding 68060/68881. Preserve each makefile's flags.

## Shared-Library ABI

- `minigl.library` exposes one public LVO (`MiniGLGetDispatchTableLVO` in `minigl.fd`); GL/MGL calls go through `MGLDispatchTable` in `include/libraries/minigl_dispatch.h`.
- Keep existing dispatch fields in place. ABI additions must be appended, and changes must stay aligned across the dispatch header, all four `library/dispatch_table_*.c` implementations, and the packaged copy under `dev/include/`.
- The shared implementation deliberately uses one global current context. `LibClose` and `LibExpunge` clear it; do not introduce per-opener assumptions without redesigning the ABI/lifecycle.
- Shared-library clients use `<proto/minigl.h>`, `MiniGLOpen()`/`MiniGLClose()`, and `libminigl.a`; directly linked clients use the `mgl/` headers and `MGLInit()`/`MGLTerm()`.

## Verification

- There is no CI or host-side unit-test harness. `testprograms` only cross-compiles Amiga executables; meaningful rendering validation requires AmigaOS 3.x with RTG and the target `minigl.library`/graphics hardware.
- The stub backend is the cheapest shared-library build check because it does not require either rendering backend. Follow it with the affected backend build and an appropriate example/demo on Amiga.
- Some rendering demos need the separate `minigl_data.lha` data package referenced in `Readme.md`.

## Physical R200 Driver Installation

- The target is the physical Amiga at `192.168.1.21:2345`, not an emulator. Connect with that explicit host and port; localhost defaults and all `amiga_emulator_*` lifecycle/state commands are inapplicable.
- Never push, pull, copy, checksum, or otherwise mutate/query Amiga files in parallel. Serialize all Amiga filesystem and DOS operations; concurrent bridge requests can collide through shared temporary files and produce missing or misleading results.
- Keep Amiga-side deployment names at most 16 characters and verify the destination CRC32 after every transfer. Longer names can alias after filesystem truncation and silently overwrite another test build.
- Install the active matched driver pair as `LIBS:Picasso96/Radeon9200.chip` and `LIBS:Picasso96/Prometheus.card`. Replace both files together, then cold reboot before testing.
- Keep the last known-good matched pair as `LIBS:Picasso96/Radeon9200.chip.previous` and `LIBS:Picasso96/Prometheus.card.previous`. Do not use `.previous` for only one component.
- The Amiga `S:startup-sequence` recovery block runs before `LoadMonDrvs`. Holding the right mouse button during boot makes `C:TestRMB` return `WARN`; the block restores both `.previous` files to their active names, but only when both recovery files exist.
- Before installing an experimental pair, copy the currently validated active chip and card to the two `.previous` names. Do not hold the right mouse button during a normal experimental-driver boot; reserve it for recovery.
- Pair `BOARDTYPE=Prometheus` with `SETTINGSFILE=SYS:Devs/Picasso96Settings.9200` in the active monitor ToolTypes before the cold reboot. Changing only `BOARDTYPE` can leave Workbench on the native fallback screen.
- A cold reboot normally takes 80-100 seconds. Poll with `/home/mirek/minigl_ppc/tools/wait_bridge.sh`, then reconnect explicitly to `192.168.1.21:2345`; do not rely on a fixed sleep.
- A grey screen, guru, or hard GPU hang requires an operator cold power cycle. A warm reboot may leave the Radeon degraded, especially for windowed presentation, and is not a valid clean test state.

## Library And Client Deployment

- After replacing `LIBS:minigl.library`, run `Avail Flush` before launching a client. AmigaOS can otherwise reopen the old resident code even when the on-disk CRC matches the new artifact.
- Run `InitPPC` exactly once per boot before any WarpOS/PPC client. If a PPC launch hangs, check `Status` for a leftover process and reboot; do not run `InitPPC` again as recovery.
- Prefer each GL client's normal quit path. Do not `Break` a fullscreen client and immediately launch another because CP work may still be in flight; after an interruption, run a small bootstrap/service probe before a heavier workload.
- Runtime validation must use the backend's matching installed stack: the WarpOS DLL opens `Warp3DPPC.library`, while the native R200 backend uses the bounded Radeon3D service owned by `Radeon9200.chip`. Neither client path may take direct ownership of PCI, MMIO, CP submission, or VRAM.
- A successful cross-build is not deployment validation. Record the artifact CRC, backend/driver pair, CPU and board, mode/depth, windowed or fullscreen state, cold/warm state, and exact tests run on hardware.
