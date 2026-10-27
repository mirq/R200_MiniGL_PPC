# Porting a game engine to WarpOS + MiniGL

This guide covers what it takes to bring an existing game engine (written for
SDL/POSIX/Win32 with an OpenGL 1.x renderer) to WarpOS on the MiniGL R200
stack. [client-porting.md](client-porting.md) covers the SDK, compiling,
the ET_REL link, Elf2Exe2 and the runtime setup. This document covers the
rest: the platform layer, the renderer adaptation, plugin modules and the
bring-up order.

It is distilled from two ports:

| Port | Tree | State |
|---|---|---|
| Quake 3 (ioquake3) | `/home/mirek/quake3amiga`, `code/amiga/` | hardware-validated, 1024x768x16 timedemo `four` 49.0 FPS measured (see [performance.md](performance.md)) |
| Xash3D FWGS (Half-Life) | `/home/mirek/xash3d-fwgs`, `engine/platform/warpos/`, `Documentation/warpos.md` | builds and passes the ET_REL check, **not yet run on hardware** (2026-10) |

Everything marked *(Xash3D, not hardware-validated)* below is a technique
that builds and links but has not been run on the A4000 yet.

## 1. What a port consists of

| Subsystem | WarpOS implementation | Reference |
|---|---|---|
| Build | standalone makefile including `Makefile.wos.gcc.common` | `quake3amiga/makefile.wos_gcc_v19`, `xash3d-fwgs/makefile.warpos` |
| Startup, exit | `main()` from `startupwos.o`, `atexit` cleanup of the MiniGL context | `amiga_main.c`, `mgl_warpos.c` |
| Timer | PPC time base (`mftb`) calibrated against `DateStamp` | `amiga_shared.c` `Sys_Milliseconds`, `sys_warpos.c` |
| Files | newlib `open`/`read`/`fopen`, dos.library for listings and stat | `amiga_shared.c` `Sys_ListFiles`, `wos_dos.c` |
| Video | MiniGL context, `mglSwitchDisplay` | `amiga_glimp.c`, `vid_warpos.c` |
| GL entry points | the engine's own GL calls, or a name → wrapper table | `qgl.h`, `gl_warpos_procs.c` |
| Input | Intuition IDCMP on the MiniGL window | `amiga_input.c`, `in_warpos.c` |
| Sound | AHI looping dynamic sample, 68k channel-info hook | `amiga_snd.c`, `s_ahi.c` |
| Network | `bsdsocket.library`, or no networking | `net_ip.c`, Xash3D `XASH_NO_NETWORK` |
| Plugins | Hyperion message port DLLs | `code/amiga/dll/`, `engine/platform/warpos/dll/` |
| Threads | none | |

There is no `dlopen`, no `fork`/`exec`, no `mmap`, no usable pthreads and no
IPv6 in the MOS2WOS SDK. Plan the port around that from the start.

## 2. Build setup

Start from the client makefile in [client-porting.md](client-porting.md#4-linking-the-et_rel-contract)
and add the following.

### 2.1 Compiler flags

| Flag | Why |
|---|---|
| `-fno-jump-tables` | mandatory, already in `WOS_ARCH_CFLAGS` |
| `-fno-common` | the ET_REL check rejects COMMON symbols, old C code declares globals in headers |
| `-msdata=none` | keeps the code generator off small-data relocations |
| `-fsigned-char` | `char` is signed here, cast before `isalnum()`/`tolower()` and table lookups |
| `-ffast-math -O2` | what both ports use |
| `-MMD -MP` + `-include *.d` | the shared makefile has no header dependency tracking |
| `-w` | engines are noisy; compile **your new platform files** once with `-Wall -Wextra` to catch implicit declarations |

C++ code additionally:

| Flag | Why |
|---|---|
| `-fno-exceptions` | no unwinder, `.eh_frame` is discarded by `warpos-etrel.ld` |
| `-fno-rtti` | unless the code uses `dynamic_cast` (freevgui does) |
| `-fno-threadsafe-statics -fno-use-cxa-atexit` | no `__cxa_guard_*`, no `__cxa_atexit` |
| `-std=gnu++11` | when the code uses `nullptr` and friends |

### 2.2 Recognising the platform

Engines detect the OS from compiler macros and stop with `#error` on unknown
ones. `WOS_CPPFLAGS` defines `WARPUP`, `__PPC__` and `NEWLIB`; adtools GCC
also defines `__BIG_ENDIAN__`. Add a branch that keys on `WARPUP`, outside
any POSIX branch: WarpOS has a libc, but none of the POSIX process, thread,
signal and dynamic loading APIs.

If the detection header belongs to a submodule you don't want to edit, put a
patched copy in a directory of your own and place it first on the include
path (Xash3D: `engine/platform/warpos/include/build.h`). A header next to the
file that includes it with quotes still wins over `-I`; copy those neighbours
too, or force your copy in with `-include` when guard names match.

### 2.3 Libraries the engine normally loads at runtime

Link them into the executable. When several of them carry their own copy of
a common helper library, merge each one with `ld -r` and make everything but
its entry table local with `objcopy -G <table>` (Xash3D's waf static-linking
mode does the same, see `makefile.warpos`). Only truly separate programs
(game modules, mods) need the DLL mechanism of section 6.

### 2.4 C++ COMDAT groups

g++ puts inline methods, vtables and typeinfo into COMDAT group sections.
After the final `ld -r` they are still there as hundreds of `GROUP` and
`.text.<name>` sections, which Elf2Exe2 was never fed by the C ports.
*(Xash3D, not hardware-validated)*:

1. Merge all C++ objects (and `-lstdc++`, if needed) with one `ld -r`: this
   folds duplicate groups.
2. `objcopy -R .group` on the result: members become plain sections.
3. Do the final ET_REL link: the linker script now merges them into `.text`,
   `.rodata` and `.data`.

Check the result with `readelf -S`; only the sections of `warpos-etrel.ld`
should remain. The binutils of this toolchain have no
`--force-group-allocation`.

### 2.5 C++ runtime

`libstdc++.a` is in the SDK, but its `__cxa_pure_virtual` calls
`std::terminate`, which pulls the whole exception runtime and its
`.gcc_except_table` sections in. Provide your own `__cxa_pure_virtual` (and
`operator new`/`delete` over `malloc`/`free` when the code doesn't define
them), and link `libstdc++` only for what is left, typically the typeinfo
classes and `__dynamic_cast` for RTTI. Itanium names on this 32-bit target
use `j` for `size_t`: `_Znwj`, `_Znaj`, `_ZdlPv`, `_ZdlPvj`.

Static constructors are collected into `.ctors` (checked: g++ 8.4 emits
`.ctors`, not `.init_array`) under `__ctrslist`. Verify on hardware that a
global constructor has run before relying on it.

## 3. Platform layer rules

### 3.1 AmigaOS headers

- Include every AmigaOS header inside `#pragma pack(push,2)` /
  `#pragma pack(pop)`. Mismatched packing gives 24-byte `struct Message`
  instead of 20 and a Data Storage exception (`DAR=0xC`) far from the cause.
- Declare `struct FileInfoBlock` with `__attribute__((aligned(4)))`.
- Include the AmigaOS headers **before** the engine's headers in the same
  file. Engines like to `#define Msg ...`, `Remove`, `Wait`, `Delay`,
  `Signal`, `Lock`; exec and dos then break in strange ways.
- Keep AmigaOS headers out of the engine: put them only in the platform
  files and give those a plain C interface (Xash3D `wos_fs.h`, `wos_gl.h`).
- MOS2WOS doesn't open library bases for you beyond `SysBase`/`DOSBase`.
  Define and open `IntuitionBase`, `GfxBase`, `KeymapBase`, `AHIBase`
  (from the device) yourself.

### 3.2 68k calls cost frame time

Every dos/intuition/exec call from PPC goes through the 68k side; see
[run68k-direct-calls.md](run68k-direct-calls.md) for the measured cost
(0.4–0.7 ms per call). What bit the ports:

| Pattern | Fix |
|---|---|
| `select()` every frame (1.7 ms measured) | poll the network rarely when idle |
| streaming music from disk per frame | preload into RAM |
| unbuffered stdout, `fflush` per line | `setvbuf(stdout, ..., _IOFBF, 64K)`; make console output opt-in |
| `time()`/`localtime()` for every log line | timestamp only on request |
| `GetMsg()` on an empty port every frame | check `IsMsgPortEmpty()` (plain memory read) first |
| small file reads | bigger read buffers (Xash3D: 2 KB → 16 KB) |

Do not drop the per-frame input pump altogether: Quake 3 tried skipping
`GetMsg` and mouse look stuttered.

### 3.3 Timer

`timer.device` `ReadEClock`/`GetSysTime` and `GetSysTimePPC` crash from a
MOS2WOS PPC task, and `DateStamp` only ticks at 50 Hz. Read the time base
with `mftbu`/`mftb`/`mftbu`, start at the 24.74 MHz rate measured on the
MPC7410 board and recalibrate against `DateStamp` about once per second.
Accumulate time step by step so a rate change never makes the clock jump or
run backwards. Copy `Sys_Milliseconds` from `quake3amiga/code/amiga/amiga_shared.c`
(Xash3D's `Platform_DoubleTime` is the same algorithm in seconds).

`Delay()` has 20 ms granularity. Don't turn a 1 ms frame-limiter sleep into
`Delay(1)`; skip sleeps shorter than one tick.

### 3.4 Files and paths

- newlib `opendir`/`readdir`/`stat` fault inside the scan loop under MOS2WOS.
  List directories and test for files with `Lock`/`Examine`/`ExNext`
  (`fib_DirEntryType >= 0` is a directory), convert `fib_Date` with the
  1978-01-01 epoch (252460800 s after the Unix epoch).
- Engines build POSIX relative paths: `./valve`, `dir//file`, `../x`.
  AmigaDOS has no `.`, reads `//` as "parent of parent" and `Vol:/x` as
  "parent of the volume". Normalise every path before it reaches
  dos.library or newlib: drop `.` and empty components, resolve `..`
  lexically, map leading `..` to a leading `/`, keep `Vol:` as is and drop the
  slash after it (`WOS_FixPath` in Xash3D `wos_dos.c` does this).
- AmigaDOS file systems are case-insensitive; turn off any case-emulation
  directory scans the engine does.
- `dup()` isn't reliable, reopen the file instead.
- Keep logs open/append/close per line on a disk that survives a hard reset
  (`DH2:` on the reference machine), so the last line survives a hang.

### 3.5 Video

Context creation, as in `amiga_glimp.c`:

```c
MiniGLOpen();                          /* fails: no host running or wrong DLL */
mglChoosePixelDepth(16);               /* 16 fullscreen, 32 windowed */
mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
mglChooseNumberOfBuffers(fullscreen ? 3 : 2);
mglChooseVertexBufferSize(4096..8192);
mglCreateContext(0, 0, w, h);
mglEnableSync(fullscreen ? GL_FALSE : GL_TRUE);
mglLockMode(MGL_LOCK_SMART);
win = mglGetWindowHandle();            /* struct Window *, for input */
...
mglSwitchDisplay();                    /* present */
...
mglDeleteContext(); MiniGLClose();     /* also from atexit() */
```

- 16-bit fullscreen is the fast path; 16-bit windowed costs about 180 ms per
  frame (see [performance.md](performance.md)), so use 32 bits in a window.
- Contexts can't be resized, and recreating one loses every texture. Treat a
  video mode change as "restart required" unless the engine reloads all its
  textures itself.
- There is no framebuffer access (`mglLockBack` is unsupported), so a
  software renderer that writes pixels can't be used as a fallback.
- Report what the context really is to the engine: 565 colour, 16-bit Z,
  no stencil, no multisampling. Stencil effects then turn themselves off.

### 3.6 Input

- Call `ModifyIDCMP(win, IDCMP_RAWKEY|IDCMP_MOUSEBUTTONS|IDCMP_MOUSEMOVE|IDCMP_DELTAMOVE)`
  and set `WFLG_REPORTMOUSE`; with `DELTAMOVE` the `MouseX`/`MouseY` of
  `IDCMP_MOUSEMOVE` are relative, accumulate them for mouse look.
- Drain `win->UserPort` with `GetMsg`/`ReplyMsg` once per frame. Don't use
  `mglKeyFunc`/`mglMainLoop`: absolute mouse only and few decoded keys.
- Map raw keys through a 128-entry table; the US layout table of
  `amiga_input.c` / `in_warpos.c` covers keypad, Help and the Amiga keys.
  `IECODE_UP_PREFIX` marks key-up.
- NewMouse wheels arrive as raw keys `0x7A` (up) and `0x7B` (down).
- Get typed characters from `keymap.library` `MapRawKey` only while the
  engine wants text input, it is one more 68k call per key.
- `mglSetPointer()` blanks the pointer, `mglClearPointer()` restores it.

### 3.7 Sound (AHI)

Port `quake3amiga/code/amiga/amiga_snd.c`:

- `AHI_AllocAudio` with one channel and one sound, load the mixer ring as an
  `AHIST_DYNAMICSAMPLE` (`AHIST_S16S` is native big-endian 16-bit stereo) and
  play it looping.
- The play position comes from an `AHIET_CHANNELINFO` effect whose hook is
  68k code (a ten-word blob that stores `ahieci_Offset[0]` into
  `hook->h_Data`). A PPC function pointer can't be used as a hook.
- Allocate the ring with `AllocVecPPC(..., MEMF_PUBLIC|MEMF_CLEAR, 0)` and
  keep its size a power of two: mixers mask the position. Clamp
  `AHIDB_MaxPlaySamples`, on hi-fi units it reports tens of megabytes.
- The 68060 doesn't snoop the PPC data cache. After mixing, write the
  painted range back with `dcbf` per 32-byte line and a final `sync`.
  Track the flushed position yourself: engines nest "begin/submit painting"
  calls, and clear the whole buffer outside the mixer (flush everything then).
- Write 8-bit output signed.
- Quake 3 re-mixed its whole mix-ahead every frame, which cost about half the
  frame; it switched to incremental mixing. Check what your mixer does.

### 3.8 Network, threads, processes

- `bsdsocket.library` (Roadshow, Miami) provides IPv4 sockets. The SDK has no
  `getaddrinfo`, `sockaddr_storage` or IPv6 types; shim them over
  `gethostbyname` (`quake3amiga/code/qcommon/net_ip.c`) or build without
  networking and keep loopback.
- No threads: compile resolver or loader threads out.
- No `exec()`: features that restart the program (changing the game
  directory) can't work.

## 4. Renderer adaptation

### 4.1 Getting the GL calls through

MiniGL's `gl*` names are inline wrappers over the dispatch table, not
symbols, and there is no `GetProcAddress` for GL names.

- Renderers that call GL directly just include `<proto/minigl.h>` (Quake 3:
  `qgl.h` switches on `MINIGL_DISPATCH_CLIENT`).
- Renderers that load function pointers by name need a table of
  `{ "glBegin", (void *)glBegin }`. Taking the address of a static inline
  wrapper makes the compiler emit an out-of-line copy. Xash3D generates the
  table from `include/libraries/minigl_dispatch.h`
  (`sed -n 's/^#define \(gl[A-Za-z0-9_]*\) MGLD_.*/WOS_GL_PROC( \1 )/p'`) and
  puts its own wrappers in front of it. Keep this the only file including
  MiniGL headers: their macros clash with the engine's GL headers.
- Return non-NULL no-op stubs for core functions that are missing: renderers
  often fetch the whole GL 1.1 set and call some entries unconditionally.
  Return NULL for extension functions so the extension check fails cleanly.
- Provide the core names for multitexture (`glActiveTexture`,
  `glClientActiveTexture`, `glMultiTexCoord1f..4f`) on top of the `ARB` ones.
  `GL_VERSION` is "1.1 ...", so engines that require 1.3 core names for
  multitexture otherwise lose it.

### 4.2 What MiniGL R200 doesn't do, and what to do about it

Full per-call status is in [dispatch-abi.md](dispatch-abi.md); these are the
ones engines trip over.

| Missing or limited | What the ports do |
|---|---|
| texture env other than `GL_MODULATE`/`GL_REPLACE` (`GL_ADD`, `GL_DECAL`, `GL_BLEND`, combine) | map to `GL_MODULATE`, drop combiner parameters; turn off effects that rely on them (detail textures) |
| `glMatrixMode(GL_TEXTURE)` | track the mode, swallow matrix calls while it is selected |
| stencil buffer and calls | report 0 stencil bits, stub the calls |
| `glClipPlane`, `glPushAttrib`/`glPopAttrib`, 1D/3D/cube textures | stub |
| `glColorMask` with a channel off | `GL_INVALID_OPERATION`; avoid colour-masked passes |
| `GL_CLAMP_TO_EDGE`, border clamp | map to `GL_CLAMP` |
| `GL_BGR`/`GL_BGRA` sources | swizzle into a scratch buffer on upload |
| sized internal formats, `GL_INTENSITY` | every texture is stored at 32 bits anyway: map to `GL_RGB`/`GL_RGBA`, build intensity alpha from red |
| texture names ≥ 2048 | keep names below 2048, move hard-coded ranges (Xash3D's skybox names started at 5800) |
| `glReadPixels` other than `GL_RGBA`/`GL_UNSIGNED_BYTE` | read RGBA and convert; `MINIGL_CTILE=1` scrambles the result |
| `glTexParameter` pnames other than filters and wrap | drop |
| `glPolygonOffset` factor | only units are applied; Quake 3 uses `MGL_Z_OFFSET` + `mglSetZOffset` |
| 16-bit Z | expect z-fighting on large maps |
| at most 8192 vertices per `glBegin`/draw call | split batches |
| VBOs, compressed textures, anisotropy, shaders | absent, let the extension checks fail |

Performance notes from the Quake 3 port: indexed `GL_TRIANGLES` through
`glDrawElements` is the fastest path, `glLockArrays` only pays off for
multi-pass geometry, state changes split batches, `GL_NEAREST` and level-0
only textures help memory. See [performance.md](performance.md).

## 5. Data and endianness

Both CPUs are big-endian, so vertex and texture data go to MiniGL without
swapping, but the game's data files are little-endian. Check that the engine
swaps every on-disk structure (models, maps, sprites, wad/pak headers, sound
headers, network messages). Recent Xash3D FWGS handles big-endian; old id
code mostly does through `LittleLong`. Also check any `htons`-style macros a
network stub defines: they must be identity on big-endian.

## 6. Plugins: Hyperion DLLs

When game code lives in separate libraries (Quake 3 qagame/cgame/ui,
Half-Life server/client), build each one as its own WarpOS executable that
serves symbols on a public message port:

- Module side: `dllstartup.c` provides `main()`, creates the port named after
  `argv[1]` and answers `Open`/`Close`/`SymbolQuery`/`Kill` messages.
  Export `dllFindResource`, `dllLoadResource` and `dllFreeResource`, or the
  loader refuses the module. Compile `dll.h` inside `pack(2)` on both sides.
- Engine side: compile `hyperion_dll/dll.c` under `pack(2)`
  (`wos_hyperion_loader.c`), `dllLoadLibrary(path, portname)` starts the
  module with `SystemTags` and waits for its port, `dllGetProcAddress`
  queries a symbol.
- Calls run on the caller's task and stack: there is one flat address space,
  the module process only answers symbol queries (10 KB stack is enough).
- Every query is a message round trip. Export one function that returns the
  whole symbol table and look everything up locally (Xash3D:
  `XashWOS_GetExports`, table generated from `nm` of the module's
  relocatable link by `scripts/warpos/gen_exports.sh`; this covers
  `LINK_ENTITY_TO_CLASS` entity names and save/restore function names).
  *(Xash3D, not hardware-validated)*
- Don't link `libminigl.a` into a module.
- A module has its own newlib: its own heap and `FILE` table. Never free
  memory across the boundary, never call `exit()` in a module.
- Varargs travel in registers on PPC: id's `&arg` syscall trick doesn't
  work, copy with `va_arg` (Quake 3 `VM_DllSyscall`).
- Deployed file names stay at 16 characters or less (transfer limit).

## 7. Bring-up order

Each step is one hardware session; take a screenshot whenever the bridge
stops answering, a PowerPC exception requester looks like a hang.

1. Start with logging to a file and everything optional off (no sound, no
   video context): the engine should reach its file system and find the
   game data.
2. Open the MiniGL context and clear to a colour, quit cleanly.
3. 2D: console font, keyboard, a few textured quads.
4. Sound: play one sample.
5. Load the plugins, then a map.
6. Save/load, demos, then profile.

Run order on the machine: cold boot, `C:InitPPC` once, start
`minigl_ppc_host`, `Stack 1048576`, run the game from its directory with
`minigl_ppc.dll` next to it ([client-porting.md](client-porting.md#7-runtime-setup)).
After any MiniGL change, Quake 1 at 800x600 must stay above 60 FPS (median of
three runs, see [benchmarking.md](benchmarking.md)).

## 8. Checklist

- [ ] Platform detected through `WARPUP`, not as POSIX.
- [ ] `-fno-common -msdata=none -fsigned-char`, dependency files on.
- [ ] Runtime-loaded engine libraries linked in (`ld -r` + `objcopy -G`).
- [ ] C++: groups folded and stripped, own `__cxa_pure_virtual`, only the
      sections of `warpos-etrel.ld` in the output.
- [ ] AmigaOS headers under `pack(2)`, before engine headers, only in platform files.
- [ ] Library bases defined and opened by the port.
- [ ] Time base timer; no `GetSysTime`; no per-frame 68k calls you can avoid.
- [ ] dos.library listings/stat; paths normalised for AmigaDOS.
- [ ] Context: 16-bit fullscreen, no runtime resize, `atexit` cleanup.
- [ ] IDCMP input with `DELTAMOVE`, port peeked before `GetMsg`.
- [ ] AHI ring: public memory, power of two, `dcbf` flush, 68k hook.
- [ ] GL table with stubs for missing core calls; unsupported state mapped.
- [ ] Texture names < 2048; formats collapsed to RGB/RGBA; BGRA swizzled.
- [ ] Plugins as Hyperion DLLs with a single export table.
- [ ] ET_REL check passes for every `.warpelf`.
