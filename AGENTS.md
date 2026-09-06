# Repository Guide

## Build and Validation

- There is no default root `Makefile`; always select one with `-f`.
- Linux-hosted builds require the `ppc-amigaos-*` GCC/binutils tools and the MOS2WOS WarpOS SDK overlay. Defaults and override points are in `Makefile.wos.gcc.common`.
- Build the real normalized ELF and bundled backend with `make -f Makefile.dll.gcc warpelf`; outputs are `bin/minigl_ppc.warpelf` and `mglQ3/lib/libmgl-wos.a`.
- Build the final DLL with `make -f Makefile.dll.gcc` only where the target-hosted `Elf2Exe2` can run; output is `bin/minigl_ppc.dll`.
- Build only the backend with `make -C mglQ3 -f Makefile.wos.gcc`.
- Build the client archives with `make -f Makefile.import.gcc`; outputs are the byte-identical `lib/libminigl_dispatch_ppc.a` and `lib/libminigl.a`.
- Build all example programs with `make -f Makefile.import.gcc testprograms`; outputs go under `bin/examples/`.
- Build the backend-free diagnostic ELF with `make -f Makefile.dll.stub.gcc warpelf`; plain `make -f Makefile.dll.stub.gcc` additionally converts it when `Elf2Exe2` is runnable.
- The repository has no host-side automated test suite. At minimum, run the relevant builds; when the cross-toolchain is unavailable, use the same commands with `make -n`. Runtime validation requires WarpOS with `minigl_ppc.dll` available to the examples.
- Clean each product through its owning makefile. In particular, `make -f Makefile.dll.gcc clean` also cleans the bundled backend.
- `warpos-etrel.ld`, `-fno-jump-tables`, `-d` and `-x` are part of the converter contract. Final ET_REL links must have `_start` at `.text+0`, no undefined/COMMON symbols and only `R_PPC_ADDR32`, `R_PPC_ADDR16_LO`, `R_PPC_ADDR16_HA` and `R_PPC_REL24` relocations.

## Runtime Boundaries

- Consumer flow is `include/proto/minigl.h` -> inline wrappers in `include/libraries/minigl_dispatch.h` -> `client/` import archive -> Hyperion loader in `hyperion_dll/` -> `MGLGetDispatchTable()` in `ppc_dll/` -> bundled implementation in `mglQ3/`.
- `MiniGLBase` is a Hyperion DLL instance handle, not an Amiga `struct Library *`; this path must not introduce a 68k `OpenLibrary()` or a 68k/PPC transition for GL calls.
- `mglQ3/src/context.c` owns `mini_CurrentContext`. The real DLL glue references it; only the standalone stub defines its own instance.
- Dispatch clients use the public declarations in `include/mgl/gl.h` through `MINIGL_DISPATCH_DECLARATIONS`; they must not import private context/vertex layouts. `include/mgl/` and the bundled backend's `mglQ3/include/mgl/` differ and must not be bulk-synced.
- The PPC DLL opens `Warp3DPPC.library`; R200 selection belongs to the installed Warp3D driver layer. Do not add the separate 68k `Radeon9200.chip` direct-backend ABI to this path.

## backend_r200: WarpOS direct-R200 stack (active work)

- There is **no Warp3D / Warp3DPPC driver for the R200**. That absence is the
  reason this repository builds its own `minigl_ppc.dll`: it replaces an old
  DLL that used to launch Warp3D stuff. Do not route this work through
  Warp3D/Warp3DPPC, and do not spend time looking for an R200 Warp3D driver —
  it does not exist.
- The current `backend_r200` design: the PPC DLL (`minigl_ppc.dll`) contains
  the R200 semantic frontend and talks over shared memory (ring + mailbox +
  serial transport, protocol v15) to a 68k helper process
  (`backend_r200/minigl_ppc_host.c`, port `MiniGLPPC.R200.Host.1`), which
  calls the 68k `minigl.library` v12 direct-R200 backend (built from
  `R200_NATIVE_ROOT`, using `Radeon9200.chip` via `Radeon3D.library`).
  PPC code cannot call that 68k library directly (mixed-mode calls from the
  PPC side are the "illegal run68k function execution" trap).
- Build: `make -f Makefile.dll.r200.gcc warpelf` (host: `... bin/minigl_ppc_host`).
  `Elf2Exe2` is target-hosted (`DH2:wosbuild/Elf2Exe2`). Deploy as ZIP
  (`UnZip -o x.zip -d DH2:wosbuild`) and CRC32-verify both ends; three
  302k+ WarpELFs of the same size can be different builds — only CRC
  identifies them.
- Target session rules: `C:InitPPC` exactly once per boot; a wedged PPC
  client ignores Exec break signals and needs a cold reboot; hard resets lose
  `Work:` filesystem directory metadata but preserve `DH2:` — keep diagnostic
  evidence on `DH2:` (`DH2:wosbuild/mglppc_client.log` client trace,
  `DH2:wosbuild/mglppc_host.log` host stage reports; both per-line
  open/append/close).
- DLL console output is gated behind `MGLPPC_VERBOSE` (SetEnv before launch);
  it is silent by default because console.device writes dominated frame time.
- Amiga-side process names in `Status` are 68k views; WarpOS PPC clients show
  up only as their launching CLI process.

## Dispatch ABI

- Treat `MGLDispatchTable` as a positional ABI. ABI version stays `3`; preserve every released V10 field through `GLPolygonOffset` in exact order and append new fields only after that boundary.
- Any dispatch API change must keep the struct and client wrappers in `include/libraries/minigl_dispatch.h`, the real positional initializer in `ppc_dll/minigl_ppc_dispatch.c`, and the stub initializer/implementations in `ppc_dll/minigl_ppc_dispatch_stub.c` synchronized.
- Do not force two-byte packing on the dispatch table; both sides use normal PPC layout.
- Do not add `FromWindow`/`CreateContextFromWindow`; it is intentionally outside this API.
- Preserve the backend's intentional fallbacks unless implementing and validating the missing behavior: `glClientActiveTextureARB`, `mglResizeContext`, `mglEmergencyCloseScreen`, and `glMGLForceNewRenderPass` are no-ops; `glMultiDrawArrays` loops over `GLDrawArrays`; `mglGetInputWindowHandle` returns the ordinary window handle.
- The Silent Hill reference DLL has a private ABI-3 fast-path tail at offsets `0x218..0x220`, while V19 uses those offsets for its appended API. `SilentHill.elf` was verified not to access those slots and is prefix-compatible; do not claim compatibility with other clients that call the private fast-path extension.
