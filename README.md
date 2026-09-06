# MiniGL V19 direct dispatch for WarpOS/PPC

This package provides the WarpOS/PPC build of MiniGL V19. It is independent of
the 68k `minigl.library`: a WarpOS game links the PPC import archive and loads a
Hyperion-style PPC DLL. After one bootstrap lookup, GL/MGL calls dispatch
straight to PPC function pointers.

The actual MiniGL implementation is now the **bundled `mglQ3` source tree**,
matching the Classic V19 package rather than requiring an external PPC backend
archive.

## Versions and compatibility

Public API level:

```
MINIGL_VERSION     19
MINIGL_MIN_VERSION 10
```

Dispatch ABI remains:

```
MINIGL_DISPATCH_ABI_VERSION 3
```

The released V10 dispatch layout is frozen as the prefix of the V19 table. No
V10 entry was removed, reordered or changed in size; the V19 additions are
appended after the old `glPolygonOffset` slot. Therefore an application already
compiled with the V10 WarpOS import library still sees the offsets it expects
and accepts the larger V19 table.

A newly compiled V19 application requires the complete V19 table and will
reject an old V10 DLL because its `structSize` is too small.

The Silent Hill reference DLL has a private three-entry fast-path extension at
the same tail boundary. Those slots are not part of this V19 API. Static
analysis of `SilentHill.elf` found no access to them: the game only uses the
frozen prefix, accepts a table of at least `0x224` bytes and is compatible with
the current `0x22c`-byte table. Another client that calls the private fast-path
slots is not compatible with the V19 tail.

The appended V19 entries are:

```
glClientActiveTextureARB
glInterleavedArrays
glMultiDrawArrays
mglEmergencyCloseScreen
glMGLForceNewRenderPass
```

`FromWindow` is intentionally **not** part of this API.

## Runtime path

```
WarpOS game
  -> libminigl_dispatch_ppc.a
  -> MiniGLOpen()
  -> dllLoadLibrary("minigl_ppc.dll", "MiniGLPPC.DLL")
  -> MGLGetDispatchTable()
  -> PPC MGLDispatchTable
  -> bundled mglQ3 PPC implementation
  -> Warp3DPPC.library
  -> installed Warp3D hardware driver
```

There is no 68k `OpenLibrary()` and no 68k/PPC transition in the GL hot path.
`MiniGLBase` is the Hyperion DLL instance handle on WarpOS.

The DLL is hardware-neutral. R200 support is selected below MiniGL by the
installed `Warp3DPPC.library`, `Warp3D.library` and Radeon-capable Warp3D
driver. This PPC path does not open `Radeon9200.chip` or use the separate 68k
direct-R200 backend ABI.

## Toolchain configuration

All GCC makefiles include `Makefile.wos.gcc.common`. Its local defaults point
at the GCC 8.4 cross-toolchain and MOS2WOS SDK overlay used for this build, and
can be overridden without editing a makefile:

```
make -f Makefile.dll.gcc warpelf \
    WOS_TOOLCHAIN_BIN=/path/to/cross/bin \
    MOS2WOS_ROOT=/path/to/mos2wos
```

The build uses the `ppc-amigaos-*` compiler/binutils for PPC SysV code but
replaces their AmigaOS 4 defaults with the WarpOS SDK headers, startup objects
and runtime libraries. The compiler does not support the historical `-warpup`
option.

## Build the bundled mglQ3 backend

Normally this is done automatically while building the DLL ELF:

```
make -f Makefile.dll.gcc warpelf
```

To build only the backend:

```
make -C mglQ3 -f Makefile.wos.gcc
```

Output:

```
mglQ3/lib/libmgl-wos.a
```

The GCC WarpOS build uses the same `mglQ3` sources as the Classic package.
The historical `makefile.vbccWOS` is retained as a reference/alternative.

## Build minigl_ppc.dll

On Linux, build the normalized PowerPC ELF input with:

```
make -f Makefile.dll.gcc warpelf
```

This builds `mglQ3/lib/libmgl-wos.a` and links:

```
bin/minigl_ppc.warpelf
```

The ELF link is normalized for historical `Elf2Exe2`: `_start` is defined at
`.text+0`, COMMON symbols are allocated, executable fragments are merged and
only `R_PPC_ADDR32`, `R_PPC_ADDR16_LO`, `R_PPC_ADDR16_HA` and `R_PPC_REL24`
relocations are permitted. The makefile fails if those invariants are broken.

`Elf2Exe2` is a target-hosted mixed 68k/WarpOS program from the historical PPC
GCC package and requires `ixemul.library` V48. Transfer the ELF to a compatible
WarpOS machine and run:

```
Stack 100000
Elf2Exe2 minigl_ppc.warpelf minigl_ppc.dll
```

The final runtime artifact is `bin/minigl_ppc.dll`. If `ELF2EXE2` names a
runnable converter on the build host, plain `make -f Makefile.dll.gcc` performs
both stages. The `.warpelf` file is not a stock AmigaDOS executable; direct
execution requires a separately installed ELF-aware `LoadSeg()` patch.

The old `BACKEND_INC` / `BACKEND_LIB` external-backend mechanism is no longer
needed.

## Build the client import archive

```
make -f Makefile.import.gcc
```

Creates:

```
lib/libminigl_dispatch_ppc.a
lib/libminigl.a
```

The two archives are byte-identical. `libminigl.a` is the drop-in historical
name. Use the compile and normalized ET_REL link flags from
`Makefile.wos.gcc.common`; `Makefile.import.gcc` is the canonical example and
does not use `-warpup` or `-lppcamiga`.

Build all import-linked diagnostic programs with:

```
make -f Makefile.import.gcc testprograms
```

Their normalized ELF files are written below `bin/examples/` and require the
same `Elf2Exe2` conversion before use on stock AmigaDOS.

Source code continues to use:

```c
#include <proto/minigl.h>

if (!MiniGLOpen())
    ...;

/* normal gl* / mgl* calls */

MiniGLClose();
```

## V19 backend notes

The WarpOS mglQ3 dispatch follows the conservative Classic behaviour where
appropriate:

- `glPolygonOffset` uses the mglQ3 implementation.
- `glInterleavedArrays` uses the mglQ3 implementation.
- `glMultiDrawArrays` is implemented as repeated `GLDrawArrays` calls.
- Radeon and Radeon2 Warp3D contexts expose the backend's generic multitexture
  and compiled-vertex-array implementations and report a specific renderer name.
- `glClientActiveTextureARB` is a no-op because mglQ3 has no independent
  client-active texture selector.
- `mglResizeContext` remains a no-op until the legacy fullscreen resource path
  is safe to expose.
- `mglEmergencyCloseScreen` and `glMGLForceNewRenderPass` are conservative
  no-ops on this backend.
- `mglGetInputWindowHandle` returns the ordinary mglQ3 window handle.

## Hyperion DLL details

The Hyperion runtime remains in `hyperion_dll/`. The DLL exports the MiniGL
bootstrap function:

```c
const MGLDispatchTable *MGLGetDispatchTable(void);
```

The required resource exports `dllFindResource`, `dllLoadResource` and
`dllFreeResource` are also supplied.

## WarpOS stub DLL

The backend-free diagnostic stub remains buildable with:

```
make -f Makefile.dll.stub.gcc warpelf
```

This creates `bin/stub/minigl_ppc.warpelf`; plain
`make -f Makefile.dll.stub.gcc` additionally invokes `Elf2Exe2` to create
`bin/stub/minigl_ppc.dll` when a compatible converter is available.

## Validation status

The real DLL ELF, stub ELF, import archives and all five example ELFs build
successfully from an empty object tree with the configured GCC 8.4 toolchain. The
ELF invariants and Silent Hill dispatch accesses have been checked statically.

Target conversion and graphical R200 execution still require a WarpOS setup on
which PPC tools run correctly. On the current Sonnet `powerpc.library` 17.15
test system, both the historical converter and initialization of
`ppclibemu` 0.9d raised a PowerPC exception. This does not invalidate the ELF
input, but it prevents claiming target runtime validation on that setup.

## Alignment / packing

Do not force two-byte packing on the MiniGL dispatch table. The table is a
PPC-to-PPC ABI and both sides compile the same structure with normal PPC
compiler layout. The Hyperion DLL glue keeps its own required packing around
classic Amiga system structures.
