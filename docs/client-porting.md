# Building a client against MiniGL 29 (dispatch ABI 5)

This is the guide for **game and application developers** who build a WarpOS
PPC program that renders through `minigl_ppc.dll`: new ports, and existing
ports (Quake, Quake 3, Silent Hill, ...) that were built against the V19
(ABI 3) headers and must be rebuilt. It covers the SDK, the build, the
link-time contract, the target-side conversion, the runtime setup and the
source changes V19 code needs. Developers of MiniGL itself want
[building.md](building.md) and [dispatch-abi.md](dispatch-abi.md) instead.

## 1. What changed for clients, in one paragraph

MiniGL 29 follows upstream minigl.library 29.1: the dispatch table has
upstream's layout (dispatch ABI **5**, 208 slots, `0x354` bytes) and the GL
tokens in `<mgl/gl.h>` carry **OpenGL's own values** (`GL_TRIANGLES` is now
`0x0004`, not an auto-numbered enum). A binary built against the V19 headers
passes different integers for the same names, so `MiniGLOpen()` compares the
ABI version for **equality** and refuses the pairing in both directions:
a V19 binary does not start against this DLL, and a rebuilt binary does not
start against a V19 DLL. Nothing else about the programming model changed:
`MiniGLOpen()`/`MiniGLClose()`, `mglCreateContext()`, the `gl*`/`mgl*` calls
and the 68k host stay as they were. New calls are available (lighting,
display lists, GLU, GLUT, `glCopyTexImage2D`, ...; per-call status in
[dispatch-abi.md](dispatch-abi.md#22-fields-130208)).

## 2. What you need

| Item | Where |
|---|---|
| Headers | `include/` of this repository (`proto/minigl.h`, `libraries/minigl*.h`, `mgl/*.h`) |
| Client archive | `lib/libminigl.a` (identical to `lib/libminigl_dispatch_ppc.a`): `MiniGLBase`, `MiniGLDispatch`, `MiniGLOpen`, `MiniGLClose` and the Hyperion DLL loader |
| Loader/open sources (optional) | `client/minigl_ppc_open.c`, `client/minigl_ppc_base.c`, `hyperion_dll/` -- compile them yourself to choose where the DLL is loaded from (section 5) |
| Cross compiler | `ppc-amigaos-gcc` (adtools GCC 8.x) with the MOS2WOS WarpOS SDK overlay; paths are set in `Makefile.wos.gcc.common` (`WOS_TOOLCHAIN_BIN`, `MOS2WOS_ROOT`) |
| On the Amiga | `DH2:wosbuild/Elf2Exe2` (target-hosted ELF converter, a PPC program), the runtime files of section 7 |

The GitHub release ships the headers, `libminigl.a`, the client sources and
this guide as an SDK archive; you do not need to build MiniGL itself.

## 3. Compiling

Use the WarpOS compiler settings of `Makefile.wos.gcc.common`; the ones that
matter for MiniGL clients:

```make
include $(MGL_DIR)/Makefile.wos.gcc.common   # WOS_ARCH_CFLAGS, WOS_CPPFLAGS, link vars
CFLAGS = $(WOS_ARCH_CFLAGS) -O2 -Wall -I$(MGL_DIR)/include $(WOS_CPPFLAGS)
```

`WOS_ARCH_CFLAGS` carries `-mcall-sysv -mbig-endian -mmultiple -fno-pic
-fno-pie -fno-jump-tables` and `-mcpu=$(CPU)` (`604e` by default, `7400`
for G4-tuned code). **`-fno-jump-tables` is mandatory**: the converter
accepts only absolute and relative relocations, and jump tables produce
others. AltiVec code (`-maltivec`) builds, but AltiVec paths have hung the
PPC on the reference MPC7410/WarpOS setup; ship them switchable.

In the source, include only the public header:

```c
#include <proto/minigl.h>   /* MiniGLOpen/MiniGLClose + every gl*, glu*, glut*, mgl* call */
```

Every call is a static inline wrapper that fetches the slot from
`MiniGLDispatch` and passes the current context; there are no link-time GL
symbols. Do **not** put `mglQ3/include` (the bundled Warp3D backend's private
headers) on a client's include path, and do not include `mgl/context.h`:
clients only ever see an opaque `GLcontext`.

## 4. Linking: the ET_REL contract

The output is an ET_REL `.warpelf` that `Elf2Exe2` turns into a WarpOS
executable on the Amiga. Link it exactly like `Makefile.import.gcc` does:

```make
game.warpelf: $(OBJS) $(MGL_DIR)/lib/libminigl.a
	$(LD) $(WOS_LINK_FLAGS) $(WOS_LIBRARY_DIRS) -o $@ \
		$(WOS_STARTFILE) $(OBJS) $(MGL_DIR)/lib/libminigl.a \
		$(WOS_RUNTIME_LIBS) $(WOS_ENDFILE)
	$(call WOS_VALIDATE_ETREL,$@)
```

- `WOS_LINK_FLAGS` is `-r -EB -d -x -T warpos-etrel.ld`.
- **`startupwos.o` (`$(WOS_STARTFILE)`) must be the first object on the line**,
  before your objects and before the archive. With anything in front of it
  the converted program dies before `main()` with a PowerPC Data Storage
  exception (DAR near `0x10`) that looks like a bug in your code. The
  `_start at .text+0` check below still passes on a wrong-order link, so keep
  the order by hand.
- `WOS_VALIDATE_ETREL` checks `_start` at `.text+0`, no undefined or COMMON
  symbols, and only `R_PPC_ADDR32`, `R_PPC_ADDR16_LO`, `R_PPC_ADDR16_HA` and
  `R_PPC_REL24` relocations. Keep the check in your build.

## 5. Choosing where the DLL is loaded from

`MiniGLOpen()` loads `MINIGL_PPC_DLLNAME`, `"minigl_ppc.dll"` by default,
which the Hyperion loader resolves relative to the program's current
directory. Games usually want either the game directory or one fixed path.
Compile the open code yourself with the name you want and link it **before**
`libminigl.a`, so it replaces the archive member:

```make
$(B)/minigl_ppc_open.o: $(MGL_DIR)/client/minigl_ppc_open.c
	$(CC) $(CFLAGS) -I$(MGL_DIR)/hyperion_dll \
	  -DMINIGL_PPC_DLLNAME="\"Work:games/mygame/minigl_ppc.dll\"" -c $< -o $@
```

This is how the Quake (`MGL_DLL_OVERRIDE`) and Quake 3 makefiles do it. A
program that ships its **own copy** of the open code (Silent Hill does) must
take the version and size checks from the headers, never literals:

```c
if (dispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION ||       /* 5 */
    dispatch->structSize < (ULONG)sizeof(MGLDispatchTable) ||     /* 0x354 */
    !dispatch->currentContext)
    fail();
```

Prefer `client/minigl_ppc_open.c` unchanged: it also performs the
`MGLPPCClientAttach`/`MGLPPCClientDetach` handshake that lets `MiniGLClose()`
release the transport cleanly.

## 6. Converting on the Amiga

`Elf2Exe2` is target-hosted and is itself a WarpOS PPC program: run
`C:InitPPC` (once per boot) **before** it, or it dies with a PowerPC
Instruction Storage exception and the machine needs a cold reboot.

```
C:InitPPC                                       ; once per boot, if your setup needs it
Stack 100000
DH2:wosbuild/Elf2Exe2 Work:mygame/game.warpelf Work:mygame/game >Work:mygame/e2e.txt
```

Run it in the foreground with its output captured; it fails silently
otherwise. Transfer files as ZIP and compare CRC32 on both ends: same-size
builds are not necessarily the same build.

## 7. Runtime setup

| File | Install |
|---|---|
| `minigl_ppc.dll` | where your `MINIGL_PPC_DLLNAME` points (game directory by default) |
| `minigl_ppc_host` | anywhere; start it before the game |
| `minigl.library` (68k, R200 backend) | `LIBS:` |
| `Radeon9200.chip` + `Prometheus.card` | `LIBS:Picasso96/`, as a matched pair, cold reboot after changing them |

A typical launch script (the one the Quake 3 release uses):

```
SetEnv MGLPPC_NOALTIVEC 1
SetEnv MGLPPC_CPEMIT 1          ; PPC builds the GPU command stream
SetEnv MGLPPC_HOSTPRI -1
SetEnv MGLPPC_CPPRESENT 1       ; GPU-side present copy
Stack 65536
Run >NIL: minigl_ppc_host >NIL:
Wait 6
Stack 1048576
game <args>
```

- `C:InitPPC` exactly once per boot before any PPC program, if the setup needs
  it. Never twice; after a PPC crash, reboot instead.
- `Stack 1048576` for the game.
- Quit through the program's own exit path (`mglDeleteContext()` then
  `MiniGLClose()`); never `Break` a fullscreen client.
- Leave diagnostic variables (`MGLPPC_VERBOSE`, `MGLPPC_UPLOAD_TRACE`)
  unset for normal play; console output costs frame time.
- `MINIGL_CTILE=1` (tiled render target) helps large fullscreen modes; the
  in-game screenshot then saves a scrambled image.

## 8. Migrating V19 (ABI 3) source

1. **Rebuild everything** that includes a MiniGL header or links
   `libminigl.a` against the new SDK. Mixing objects built against the old
   and new headers passes wrong token values silently.
2. **Remove hard-coded GL numbers.** Any literal that stood for a V19 token
   value is wrong now. Known cases: the V19 "classic tail" values for
   `MGL_UBYTE_BGRA`/`MGL_UBYTE_ARGB`/`MGL_UNSIGNED_SHORT_5_6_5`/`_4_4_4_4`
   (233..236, or 237..240 in older 68k headers), and any `GL_*` value copied
   into a table or a config file. Use the names; they now have OpenGL's
   values, which also means code written for desktop OpenGL can pass its own
   constants.
3. **Own copies of the open/loader code**: take the checks from the headers
   (section 5).
4. `mglResizeContext()` now returns `GLboolean` (always `GL_FALSE` on the
   PPC backends: no resize happens).
5. `mglEmergencyCloseScreen()` and `glMGLForceNewRenderPass()` still compile
   but no longer have table slots; they do nothing.
6. Headers that defined GL tokens themselves (`#ifndef GL_FOO / #define GL_FOO
   <value>`) should now see MiniGL's definitions first; compile once without
   `-w` and look for "redefined" warnings: there must be none.
7. `MiniGLOpen()` failing straight away after the rebuild almost always
   means an old DLL is being loaded; check which file `MINIGL_PPC_DLLNAME`
   resolves to.

## 9. vbcc / PowerOpen clients

The DLL is built with the SysV PPC ABI; vbcc and some older WarpOS
toolchains use PowerOpen, which places arguments 9+ and integers following
floating-point arguments differently. The DLL detects the caller's ABI when
it is opened and bridges the calls that need it:

| Call | PowerOpen status |
|---|---|
| `glTexImage2D`, `glTexSubImage2D` (10 arguments) | bridged; or call the exported block helpers `MGLTexImage2DBlock`/`MGLTexSubImage2DBlock` (`dllGetProcAddress`) |
| `glCopyTexImage2D` | safe: its 9th argument (`border`) is never read |
| `glCopyTexSubImage2D` (9th argument `height`) | **not bridged yet** |
| GLU quadrics (`gluSphere`, `gluCylinder`, `gluDisk`) and `glutSolid*` (doubles followed by integers) | **not bridged yet** |
| everything else | identical in both ABIs |

Clients built with `ppc-amigaos-gcc` and the flags above (SysV) are
unaffected. Silent Hill (MOS2WOS) is one of the clients whose texture uploads
needed the bridge, so treat MOS2WOS output as PowerOpen for the calls in this
table until you have checked your toolchain's calling convention.

## 10. Example: rebuilding an existing MOS2WOS port (Silent Hill)

The Silent Hill WarpOS port (`SilentHill.elf`, MOS2WOS) carries its own
`MiniGLOpen` that requires `abiVersion == 3` and `structSize > 547`. Its
current binary therefore falls back to its software renderer with this DLL
("[WOS-GL] MiniGLOpen failed ... -> software renderer"). Two ways forward:

- **Rebuild against MiniGL 29** (recommended):
  1. Point the include path at this SDK's `include/` and link this
     `libminigl.a` (or the client sources, section 5).
  2. In the port's `MiniGLOpen`, replace the literal `3` and `547` with
     `MINIGL_DISPATCH_ABI_VERSION` and `sizeof(MGLDispatchTable)`, or switch
     to `client/minigl_ppc_open.c` with
     `-DMINIGL_PPC_DLLNAME="\"minigl_ppc.dll\""` (game directory).
  3. Check the texture upload code for numeric format/type values
     (section 8 point 2); the port uses `MGL_UBYTE_BGRA`-style uploads.
  4. Keep `startupwos.o` first on the link line; convert with `Elf2Exe2`
     after `InitPPC`; ship the new `minigl_ppc.dll` next to the executable.
  5. Run `abi5_smoke` once on the target to confirm the DLL/host pair, then
     the game.
- **Keep the old binary**: the game loads `minigl_ppc.dll` from its own
  directory, so an ABI-3 (V19-era) R200 DLL can stay there while other
  programs use MiniGL 29. The protocol between DLL and 68k host did not
  change, so both DLLs work with the same host.

## 11. Checklist

- [ ] `#include <proto/minigl.h>` only; include path is this SDK's `include/`.
- [ ] No numeric GL constants; no `mglQ3/include` on the path.
- [ ] `-fno-jump-tables`, `-mcall-sysv`; `startupwos.o` first; ET_REL check passes.
- [ ] DLL name/path chosen deliberately (section 5); own open code uses the header constants.
- [ ] Converted with `Elf2Exe2` after `InitPPC`; CRC32 verified on both ends.
- [ ] Host started before the game; `Stack 1048576`; normal quit path.
- [ ] `abi5_smoke` passes on the target with the DLL/host pair you ship.

Reference results with this version (A4000, MPC7410, Radeon 9200, see
[performance.md](performance.md)): Quake demo3 800x600x16 61.3 FPS, Quake 3
timedemo `four` 1024x768x16 with sound 49.0 FPS, identical to the V19 stack.
