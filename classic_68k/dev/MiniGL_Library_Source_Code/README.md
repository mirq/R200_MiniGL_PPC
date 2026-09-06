# minigl.library V10 - dispatch-table experiment

This tree uses a single dispatch-table LVO instead of one LVO per GL/MGL
function. `MiniGLOpen()` opens `minigl.library` V10, fetches the complete
`MGLDispatchTable` once and validates it
(`abiVersion == MINIGL_DISPATCH_ABI_VERSION`, currently 5,
`structSize >= sizeof(MGLDispatchTable)`). Public GL/MGL names are macros that
call the corresponding function pointer in `MiniGLDispatch`.

The only custom library vector is `MiniGLGetDispatchTableLVO` at -30.
The old stack-LVO and register-LVO client stubs have intentionally been
removed from this tree. V10 is therefore an intentional ABI break from the
experimental older libraries.

Application and game-port developers should start with
[`developer/README.md`](developer/README.md). It includes integration,
deployment, backend, and porting guidance plus GCC and vbcc example builds.

## GCC

The GCC cross-toolchain is installed outside the default `PATH`:

```sh
export PATH=/opt/amiga/bin:$PATH
```

PiStorm3D:
  make -f Makefile_dispatch.gcc
  make -f Makefile_dispatch.gcc importlib
  make -f Makefile_dispatch.gcc testprograms

Stub:
  make -f Makefile_dispatch_stub.gcc
  make -f Makefile_dispatch_stub.gcc importlib
  make -f Makefile_dispatch_stub.gcc testprograms

Classic:
  make -f Makefile_dispatch_classic.gcc

R200 (Radeon9200.chip):
  make -f Makefile_dispatch_r200.gcc dispatchtable   # regen library/dispatch_table_r200.c
  make -f Makefile_dispatch_r200.gcc
  make -f Makefile_dispatch_r200.gcc importlib
  make -f Makefile_dispatch_r200.gcc testprograms

Run the normal build after `dispatchtable`; regeneration changes the thunk
source but does not relink an already-built `minigl.library` by itself.

All GCC backends share one `lib/libminigl_dispatch.a`; whichever Makefile ran
`importlib` last owns it.

## R200 backend notes

- Targets physical Radeon R200-class hardware through `Radeon9200.chip`
  only — there is no emulated path, so a clean build proves nothing about
  behaviour. See AGENTS.md for the hardware-testing policy.
- Pixel depth is 8, 16 or 32 only; 8-bit is fullscreen-only. Several example
  programs default to `mglChoosePixelDepth(24)`, which yields a NULL context:
  pass `-depth 16` or `-depth 32`.
- `testprograms` builds the gears benchmark plus the phase4/5/6 smoke,
  acceptance and cull/shade clients described in AGENTS.md.
- The backend is split into modules under `library/` (`r200_math.c`,
  `r200_submit.c`, `r200_texture.c`, `r200_state.c`, `r200_geometry.c`,
  `r200_surface.c`) around the lifecycle core `minigl_r200.c`, sharing
  `minigl_r200_internal.h`. They compile together through
  `library/r200_unity.c` — add new modules there. After changing any of them,
  regenerate the thunk layer with `dispatchtable` — it collects `Lib_*`
  symbols from every backend object.

## vbcc

Use Makefile_dispatch.vbcc, Makefile_dispatch_stub.vbcc,
Makefile_dispatch_classic.vbcc or Makefile_dispatch_r200.vbcc. The PiStorm3D,
stub, and classic example targets link the three tiny client objects directly;
the R200 vbcc Makefile currently builds the library only.
