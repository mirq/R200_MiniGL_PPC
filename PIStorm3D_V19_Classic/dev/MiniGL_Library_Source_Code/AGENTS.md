# PiStorm SDK V10

## V19 parity work (R200)

The active work plan for aligning the V10 R200 backend with the V19
`mglQ3` implementation lives in **[plan.md](plan.md)**. Progress is
recorded in its "Progress log" section — update it whenever a phase
starts or completes. Scope decisions recorded there:

- Parity target is *actual* V19 behavior on R200 only (header-only/
  dead V19 features are excluded with rationale in plan.md).
- `mglEmergencyCloseScreen()` uses safe terminal semantics, not V19's
  half-destroyed-context behavior.
- Dispatch ABI bumps to v5 with two appended fields
  (`GLInterleavedArrays`, `MGLEmergencyCloseScreen`); append-only so
  positional initializers in existing backend tables stay valid.

## What this repo is

Amiga m68k shared library (`minigl.library` V10) implementing a MiniGL/OpenGL subset via a single dispatch-table LVO at offset -30. Four backends: **pistorm3d** (main), **stub** (no-op, testing), **classic** (classic Amiga hardware), **r200** (Radeon R200-class hardware via the `Radeon9200.chip` semantic service). V10 is an intentional ABI break from older experimental trees — old stack-LVO/register-LVO client stubs are gone.

## Toolchain

Cross-compilation only. Required tools:
- **GCC path**: `m68k-amigaos-gcc`, `m68k-amigaos-as`, `m68k-amigaos-ar`
- **vbcc path**: `vc`, `vasmm68k_mot`

The GCC cross-toolchain is installed in **`/opt/amiga/bin`** (m68k-amigaos-gcc 6.5.0b) and is
**not on `PATH` by default**. Build with it explicitly:

```
PATH=/opt/amiga/bin:$PATH make -f Makefile_dispatch_r200.gcc
```

Do not substitute a Docker cross-compiler image: a different GCC major version produces a
different binary, so hardware results stop being comparable with the recorded baselines.

There is no configure step. The host system cannot run the output binaries.

## Build commands

### GCC — PiStorm3D (primary backend)
```
make -f Makefile_dispatch.gcc                  # build minigl.library → bin/dispatch-pistorm3d-gcc/
make -f Makefile_dispatch.gcc importlib        # build lib/libminigl_dispatch.a (client link lib)
make -f Makefile_dispatch.gcc testprograms     # build example binaries → bin/
make -f Makefile_dispatch.gcc clean
```

### GCC — Stub backend (no hardware needed)
```
make -f Makefile_dispatch_stub.gcc
make -f Makefile_dispatch_stub.gcc importlib
make -f Makefile_dispatch_stub.gcc testprograms
```

### GCC — Classic backend
```
make -f Makefile_dispatch_classic.gcc          # library only; fewer test programs
```

### GCC — R200 backend (Radeon9200.chip)
```
make -f Makefile_dispatch_r200.gcc dispatchtable # regenerate library/dispatch_table_r200.c from the backend object
make -f Makefile_dispatch_r200.gcc             # build minigl.library → bin/dispatch-r200-gcc/
make -f Makefile_dispatch_r200.gcc importlib
make -f Makefile_dispatch_r200.gcc testprograms  # gears + phase4/5/6 smoke/acceptance clients → bin/
make -f Makefile_dispatch_r200.gcc clean
```

Always run the normal build after `dispatchtable`; regenerating the C source
does not relink an already-built library.

### vbcc
```
make -f Makefile_dispatch.vbcc                 # pistorm3d
make -f Makefile_dispatch.vbcc testprograms
make -f Makefile_dispatch_stub.vbcc
make -f Makefile_dispatch_classic.vbcc
make -f Makefile_dispatch_r200.vbcc
```

**`importlib` must be built before `testprograms`** (testprograms links against `lib/libminigl_dispatch.a`).

## Directory layout

| Path | Role |
|---|---|
| `library/` | Library-side source (built into `minigl.library`) |
| `client/` | Client-side stubs (`minigl_base.c`, `minigl_open.c`, `minigl_dispatch_stub_gcc.s` / `..._vbcc.s`) — built into `libminigl_dispatch.a` |
| `include/libraries/minigl_dispatch.h` | The entire public API — `MGLDispatchTable` struct + inline wrappers for all GL/MGL calls |
| `include/` | All public headers |
| `examples/` | Example programs (link against `libminigl_dispatch.a`) |
| `lib/` | Output import library (`libminigl_dispatch.a`) and prebuilt `libminigl.a` |
| `bin/` | Output binaries and library files |
| `third_party/pistorm3d/` | PiStorm3D backend library and pre-built `.o` shims |
| `third_party/radeon3d/include/` | Vendored Radeon3D service client ABI headers (R200 backend only) |
| `third_party/warp3d/include/` | Vendored `Warp3D/Warp3D.h`, required transitively by `mgl/vertexbuffer.h` for every backend |
| `tools/generate_dispatch_r200.py` | Regenerates `library/dispatch_table_r200.c` from `minigl_dispatch.h` + the backend objects' defined `Lib_*` symbols |
| `obj_gcc_dispatch*/` | Compiler output dirs (generated, safe to delete via `clean`) |

## Key architecture facts

- **Single LVO**: the library exports only `MiniGLGetDispatchTableLVO` at -30. All GL functions are function pointers inside `MGLDispatchTable`.
- **Client side**: `MiniGLOpen()` opens the library, calls the LVO, validates `abiVersion == MINIGL_DISPATCH_ABI_VERSION` (currently 5) and `structSize >= sizeof(MGLDispatchTable)`, then stores the pointer in the global `MiniGLDispatch`. All `gl*`/`mgl*` calls in client code go through macros in `minigl_dispatch.h` that dereference `MiniGLDispatch`.
- **`MINIGL_LIBRARY_BUILD`** define: suppresses the client-side macros/inlines inside `minigl_dispatch.h`. Always set when compiling `library/` sources; never set when compiling `client/` or `examples/`.
- **`MGLD_CTX`** expands to `(*MiniGLDispatch->currentContext)`. `mglCreateContext` / `mglCreateContextFromID` set this automatically via the dispatch-table wrapper; do not set it manually.
- **glEnable/glDisable** are not in the dispatch table directly — they map to `MGLSetState(ctx, cap, GL_TRUE/GL_FALSE)`.
- **glColor3f / glVertex3f / glVertex2f** are not in the table either — they are inlined as calls to `GLColor4f` / `GLVertex4f` with filled-in alpha/w.
- **`-DFASTPATH`** compile flag enables the fastpath path in `examples/milestone_gears_library.c` (see `include/libraries/minigl_fastpath.h`).
- **vbcc pistorm3d build** additionally assembles `library/pistorm3d_stdio_stubs.s` with `vasmm68k_mot` and links pre-built `.o` files from `third_party/pistorm3d/` (64-bit shift helpers, memcmp).
- **PiStorm3D backend** requires `third_party/pistorm3d/libminiglv3d.a` (GCC) or `third_party/pistorm3d/miniglv3d.lib` (vbcc) — not included in this tree for the library link; must be present.
- **Classic backend** uses `../../mglQ3/include/` and
  `../../mglQ3/lib/libmgl.a` from the enclosing PiStorm3D repository by
  default. Override `CLASSIC_INC` and `CLASSIC_LIB` for a standalone build.
- **R200 backend** talks to hardware only through `Radeon9200.chip`'s semantic Radeon3D service (`third_party/radeon3d/include/`) — it never touches PCI/MMIO/CP/VRAM directly. The backend sources are split into modules under `library/` sharing `minigl_r200_internal.h` (structs, capacities, shared bases): `minigl_r200.c` is the lifecycle core (Amiga library vectors, timer, context create/destroy, `MGLInit()`/`MGLTerm()`, input handlers), `r200_math.c` (float/matrix helpers), `r200_submit.c` (submit slots, fences, recovery), `r200_texture.c` (residency, upload), `r200_state.c` (deferred-state snapshots incl. the hardware-TCL header, state setters, queries), `r200_geometry.c` (batching, clipping, hardware-TCL emit, immediate/arrays/FastPath), and `r200_surface.c` (formats, clear, readback, present, resize). **They are compiled together through `library/r200_unity.c`, not as separate objects**: the m68k toolchain has no usable LTO and per-module objects measurably cost cross-module inlining (gears fullscreen 16bpp dropped ~6% before the unity object was introduced; the unity build is at parity with the historical single-file build). When adding a module, add its `#include` to `r200_unity.c`. All are structured behind the same `MGLInit()`/`MGLTerm()` interface the classic and pistorm3d backends use; `library/dispatch_table_r200.c` is a generated thunk layer (`tools/generate_dispatch_r200.py`) binding the context-free `Lib_gl*`/`Lib_mgl*` entry points into the dispatch table. `library/minigl_r200.h` carries the backend's own self-contained GL typedefs/enums — note these use the backend's *own* error numbering (`GL_INVALID_OPERATION = 89U`, not mgl/gl.h's 0x502), so a client-observed error of 89 means invalid-operation raised by this backend — and must never be included alongside `mgl/gl.h` in the same translation unit; that's why `dispatch_table_r200.c` restates the `Lib_*` prototypes in SDK types instead of including `minigl_r200.h`.

## Common mistakes to avoid

- Do not add a new GL function by creating a new LVO. Add it as a new function pointer field in `MGLDispatchTable` and increment `MINIGL_DISPATCH_ABI_VERSION` in `include/libraries/minigl_dispatch.h`.
- `lib/libminigl_dispatch.a` is rebuilt by whichever Makefile runs `importlib` last. All four GCC Makefiles write to the same path, so the last build wins. Keep this in mind when switching backends.
- The stub GCC Makefile uses `-mhard-float -ffast-math -O2`; the pistorm3d GCC Makefile uses `-m68881 -O3 -mcrt=clib2`; the R200 GCC Makefile uses `-m68881 -O2 -ffreestanding -fno-builtin -nostartfiles`. They are not interchangeable.
- `clean` only removes the per-Makefile `OBJDIR` and `OUTDIR`; it does not remove the other backends' obj dirs.
- Every backend that links GCC's libm needs `math_shim.c`'s `MathIeeeDoubTransBase`; the packaged classic archive also needs `MathIeeeSingTransBase`. Omitting these fails the final link with undefined references to `cos`/`sin`/`exp`/`sqrt`/`tan`, not a compile error.
- The packaged `../../mglQ3/lib/libmgl.a` does not export `MGLResizeContext`, so the classic dispatch table implements resize as a no-op. Do not bind it directly unless the classic archive is replaced with one that exports the function.
- vbcc gives `mgl/minigl.h`'s `MGLAPI` wrappers external linkage unless `MGLAPI` is `static inline` — bare `inline` (the old vbcc branch) leaves a library build with undefined references to backend entry points it never calls, once a backend (like R200) has no classic MiniGL archive to satisfy them.
- After editing any `library/r200_*.c` / `library/minigl_r200.c`, regenerate the thunk layer with `make -f Makefile_dispatch_r200.gcc dispatchtable` and rebuild — the R200 dispatch table is derived from all backend objects' defined `Lib_*` symbols, not hand-maintained.

## R200 backend notes

Ported from an independent `MiniGL_R200` tree (186-vector stack ABI, Phases 0–6 complete on physical 68060/RV280) onto the V10 dispatch ABI. The rendering core is functionally unchanged from that port; only the entry layer changed. The pre-TCL software transform/clip pipeline (object-space clipper, native fan/strip heuristics) was deleted when hardware TCL became mandatory — the only remaining software clipper is the screen-space one serving `MGL_FLAT*`, whose vertices arrive pre-projected by definition.

- **152 dispatch table fields, 152 bound on R200** (ABI v5). The v5
  additions `GLInterleavedArrays` and `MGLEmergencyCloseScreen` are R200
  implemented; classic, PiStorm3D and the PPC frontend leave them NULL
  and the client wrappers refuse to call through a missing vector.
  See `plan.md` for the V19-parity scope, its exclusions, and progress.
- **`Radeon9200.chip` is the sole owner** of PCI, MMIO, CP, reset and VRAM. `MGLInit()` opens `Radeon9200.chip` (`RADEON3D_LIBRARY_VERSION` in `library/minigl_r200.h`), `Picasso96API.library`, `graphics.library`, `intuition.library` and the timer device; a machine without the chip driver fails `LibInit`, so `OpenLibrary("minigl.library", ...)` itself fails rather than a later GL call.
- **Pixel depth is 8, 16, or 32 only** (`SurfaceFormatForDepth()` in `minigl_r200.c`). 8-bit is fullscreen-only (a public CLUT8 palette cannot be remapped per frame in a window). `mglChoosePixelDepth(24)` — the default in several ported example programs — yields a NULL context; pass `-depth 16` or `-depth 32` explicitly.
- **Only one context-owning task at a time**; competing `mglCreateContext` calls are rejected. `LibClose` releases the calling task's context via `MGLBackendTaskClose()`; `LibExpunge` refuses to unload while `MGLBackendHasContext()` is true.
- Asynchronous semantic submission uses two immutable 8192-dword record slots.
  One Radeon3D Execute fence remains outstanding while the CPU fills the other
  slot; retire it before the next Execute because two simultaneously outstanding
  semantic Execute calls alias service-side staging and reproduce the historical
  Phase 6 hang. Texture-use fences are tracked separately from presentation and
  readback drains. Always re-run phase5 deferred ordering, phase6 primitives,
  and phase6 acceptance after changing this machinery.
- Draw state is deferred through immutable `PendingDrawState` snapshots.
  `semanticStateSerial` is bumped by semantic-header state **and by everything
  baked into the hardware-TCL record header**: the modelview/projection
  matrices (all matrix mutations and `glPopMatrix`), viewport, depth range,
  point size, shade model, polygon mode, cull face mode/front face, and cull
  enable — the serial is what stops geometry from being appended under a
  stale matrix. Fog-factor parameters and other per-vertex values stay in the
  vertex and do not bump it. A real semantic change splits pending geometry
  only when the next geometry is appended, while equivalent normalized headers
  take the serial fast path. Texture mutation/deletion, clear, presentation,
  readback, resize, context destruction, and shutdown remain ordering
  barriers. Textures named by pending snapshots must not be evicted, but
  disabled bound textures are not protected. Re-run Phase 6
  deferred-state/order and perspective tests after changing snapshot or
  serial logic.
- **Lighting state (ABI v4)** also bumps `semanticStateSerial` on every
  mutation: `glLight*`, `glLightModel*`, `glMaterial*`, color-material
  enable/mode, and `glEnable/glDisable` of `GL_LIGHTING`/`GL_LIGHTn`.
  Lit draws force the hardware-TCL path; the record header grows to
  44 + 32 (model-view + inverse model-view) + 26 (global ambient, eye
  vector, light control, front material) + 31 per enabled light dwords,
  and vertices become 13 dwords with an object-space normal XYZ after W.
  Light positions and spot directions transform by the modelview at
  call time (GL spec); eye-space derivations follow Mesa classic r200
  exactly — directional lights upload `_VP_inf_norm` plus the halfway
  vector `_h_inf_norm`, DCD/DCM stay zero, range cutoff is FLT_MAX and
  the constant-attenuation reciprocal degrades to FLT_MAX at zero; the
  eye vector uploads `(_EyeZDir.x, _EyeZDir.y, -_EyeZDir.z,
  _ModelViewInvScale)`. Colour material rewrites the material block from
  the current colour at capture time. Two-sided rendering is accepted but
  front-only: the semantic service has no back-face lighting.
- **FastPath v2 carries no normals**, so `mglBeginFastPath` is rejected
  with `GL_INVALID_OPERATION` while lighting is enabled. Extending
  FastPath to a v3 attribute bit is deferred until a client needs it.
- The TCL record vertex is 10 dwords (13 with normals): x, y, z, w,
  [normal XYZ,] one packed ARGB colour dword (same packing as the non-TCL
  paths), s0/t0, s1/t1, fog. Only list
  primitives (triangles, quads, points, lines) merge across
  `glBegin`/`glEnd` blocks; strips, fans, and loops flush at `glEnd`.
- **Sphere-map texgen (Radeon3D interface 12)** is hardware accelerated when
  both S and T generation are enabled for a textured unit. The record uses the
  78-dword texgen header, followed by 32 model-view/inverse-model-view dwords,
  and 13-dword normal vertices. Partial S/T state and older capabilities retain
  `GenerateSphereMap()` as a software fallback. The interface-12 raw probe is
  currently provisional: its center sample passes but four corner samples fail
  pixel equality (`r3dformats` result 102), although 32-bit gears renders
  coherently.
- 8192-dword batch size is the validated optimum; 16384 showed no gain and increased noise.
- `glTexCoord4*` and FastPath Q coordinates are pre-divided — the semantic record does not carry a reciprocal W.
- Windowed present is a single-surface blit; fullscreen uses three screen buffers and waits for safe/display messages before reusing them. Measured on a 68060/RV280 (see below): switching depth costs ~2%, switching window/fullscreen at fixed depth costs ~10%, almost entirely in the present stage.
- **Stack discipline**: snapshot captures and record serialization use
  context-resident scratch (`captureScratch`, `pendingDraw`), never large
  stack buffers — the maximum texgen+lit TCL header is 384 dwords, and CLI-launched
  clients run on a 4 KiB stack. `SamePendingDrawState` and
  `CapturePendingDrawState` are length-aware: they touch only the scalar
  prefix plus `headerDwords` dwords so unlit batches never pay for the
  worst-case lit header.
- **Remaining CPU-side per-vertex work**: the per-vertex fog factor still needs
  a fog atom in a future Radeon3D semantic interface. `MGL_FLAT*` FastPath vertices are pre-projected
  by that ABI, so they bypass TCL by construction unless a future FastPath v3
  delivers object-space vertices.

### Performance target

Benchmark and optimization work targets **800x600 @ 32 bpp, windowed, on the physical
Amiga (192.168.1.21:2345)**. Native 68k backend work uses the four-Ogre tests
(`quake.alias-ogre-x4-*`) as the primary application-workload proxy. MiniGLPPC
bridge work uses the envmapped gears benchmark as its fixed reference workload;
see `MINIGLPPC_130FPS_PLAN.md` for its current baseline and status. Driver
(`Radeon9200.chip`) modifications are in scope where attribution justifies them.

### Testing on real hardware

The R200 backend has no simulated/emulated path — `Radeon9200.chip` requires actual Radeon R200-class hardware. Treat a clean build and `nm -u` as empty as necessary but not sufficient; always confirm behaviour on physical hardware before trusting a change, and record what was actually run (mode, depth, windowed/fullscreen, warm/cold boot).

`make -f Makefile_dispatch_r200.gcc testprograms` builds the following core
smoke and acceptance clients in addition to the gears benchmark. This is a
selected list, not an exhaustive inventory of every example binary emitted by
the target:

| Binary | Exercises |
|---|---|
| `bootstrap_smoke_r200_gcc` | Context creation, `MiniGLDispatch->abiVersion`/`structSize`, an unsupported-vector call routing correctly through the table |
| `phase4_smoke_r200_gcc` | Transforms, clipping, depth buffer, vertex/color arrays, indexed draws |
| `phase5_smoke_r200_gcc` | Texture upload/subimage, alpha test, blend, mipmaps, wrap modes, texture residency stress (`--stress`) |
| `phase6_primitives_r200_gcc` | Fog (linear/exp/exp2), dual-unit texturing, scissor, points/lines/line-strip/line-loop |
| `phase6_acceptance_r200_gcc` | Deferred state/order and snapshot counters, compiled arrays + lock, FastPath v2, async fences, window move/resize, CP context-loss recovery, per-depth surface format, batch budget |
| `cull_shade_smoke_r200_gcc` | `glCullFace`/`glFrontFace`/`glShadeModel`/`glDrawBuffer` against pixel readback via `mglLockBack()` |
| `v19_parity_r200_gcc` | V19-parity suite (see `plan.md`): `glInterleavedArrays` (6 layouts + error paths), `mglHint` targets, `mglGetSupportedScreenModes` semantics, `mglDrawMultitexBuffer` queue/flush, pixel-readback verification. Takes `--fullscreen`, `-depth 8|16|32`, `-buffers 1|2|3` |
| `v19_emergency_r200_gcc` | `mglEmergencyCloseScreen`: idempotent no-op, terminal close, safe no-op delete, fresh context + render afterwards. Run LAST in a session; separate executable because it tears the context down |
| `v19_tri_buffers_gcc` | Long-run fullscreen triangle (`-seconds N`, default 20) for operator visual checks of present modes and buffer counts: takes `--fullscreen`, `-depth N`, `-buffers 1|2|3`, `-seconds N`. Rotates a triangle one full turn every ~4.8 s so present artifacts are obvious |
| `gears68k` / `gears68k_precalc` | The fixed-reference gears benchmark (fullscreen by default, `-window` to opt out). Also takes `-buffers 1|2|3` (added 2026-09-03 for present-mode demonstrations; when omitted, no call is made and the validated default 3 applies). Needs `data/chrome.ppm` alongside the binary when run with the envmap |
| `v19probe` | Diagnostic probe (raw centre-pixel readout, capture/flush state isolation). Not in `testprograms`; build it with the client compile line when chasing present/readback issues |

Each accepts `--fullscreen` (default windowed) where applicable; `phase6_acceptance_r200_gcc` additionally takes `--depth 8|16|32`. Deploy to `Work:minigl_testing/` and run directly — none require special setup beyond the library being installed in `LIBS:`.

Hardware-validated status (2026-09-04 session 4, see `plan.md`): parity 31/31
fullscreen at 16/32 bpp and buffer counts 1/2/3 plus windowed 16/32 on the
flip-fix build; emergency 6/6; phase4/5/6/6-acc/cull-shade/phaseE/quad-list/
perspective all pass. Phase7 lighting now passes fully after the driver selected
TCL `COLOR_0` for lit draws; blend_alpha FAILBITS remain pre-existing and were
A/B-verified against the v4 library. The
3-buffer fullscreen black screen is FIXED: the flip path's sync
CompletePendingFlip was clobbering the three-buffer rotation target;
the fix re-applies the rotation after the sync wait (library snapshot
`LIBS:minigl.library.v5-flipfix`, crc32 51699751). Flip present is
inherently slow (~1-2 vblank waits per present); MINIGL_PRESENT=BLIT
displays all buffer counts correctly and fast. 1-buffer fullscreen shows
scanout-race banding ("missing horizontal lines") — inherent to rendering
into the visible bitmap; a vblank pin in the single-buffer present
(installed build, crc32 5dba9d41) cannot remove it; operator reproduced
it with gears fullscreen `-buffers 1` and decided to leave it as is —
steer visual clients to 2+ buffers. The bridge screenshot tool reads a
stale bitmap for fullscreen custom screens (uniform gray) — operator
eyes are the display oracle. gears texture data lives in
`Work:minigl_testing/data/` (chrome.ppm from mglQ3/demos/data).

**All test binaries are deployed to `Work:minigl_testing/`** (create the
directory on first use). Keep the Amiga-side deployment names at most 16
characters, verify the CRC after every push, and run the deploy →
`Avail Flush` → health-probe sequence from the physical-device policy
before the first test of a session. Example:

```
makedir Work:minigl_testing            ; once
push ... -> Work:minigl_testing/bootstrap
push ... -> Work:minigl_testing/v19parity
Work:minigl_testing/bootstrap -depth 16
Work:minigl_testing/v19parity -depth 16
```

See `MINIGLPPC_130FPS_PLAN.md` for the current PPC baseline and status.
`PERFORMANCE.md`, `OPTIMIZATION_PLAN.md` and `GLITCH_HUNT_FINDINGS.md` are the
migrated PiStorm_SDK_v10 performance/glitch-hunt knowledge (the SDK tree's
worst-frame backend machinery and quake-tuned objdirs are still unported —
see `plan.md`).

## Physical-device automation policy

- Always use the physical Amiga at `192.168.1.21:2345`. Never use an emulator
  for R200 performance or acceptance testing.
- Use Amiga deployment binary names of at most 16 characters. Longer benchmark
  names can alias after filesystem truncation, causing one variant to overwrite
  another while commands appear to reference distinct files. Always verify the
  deployed CRC before running.
- **This is real hardware, not an emulator.** The `amiga_emulator_*` MCP tools
  do not apply to this target and must not be used on it — there is no
  emulator process to start or stop, and driving them against the physical
  machine only stalls requests. Recovery from a wedged/crashed state is a
  manual cold power-cycle by the operator; after that, run
  `tools/wait_bridge.sh` before reconnecting.
- A cold Amiga reboot takes approximately **80 seconds**. After requesting a
  reboot, wait for the bridge port to answer instead of sleeping a fixed
  time: run `tools/wait_bridge.sh` (polls TCP `192.168.1.21:2345`, returns as
  soon as the bridge responds, 600 s default timeout). Reconnect with an
  explicit `amiga_connect(host="192.168.1.21", port=2345)` afterwards — the
  default localhost endpoint fails.
- A **warm reboot does not reliably reset the Radeon card**: after a hard
  crash (grey screen / guru), `Radeon9200.chip` can come back in a degraded
  state where windowed presents fail with errors even though other GL work
  (gears fullscreen, smoke tests) still succeeds. Treat any grey screen as
  requiring a full power cycle before drawing conclusions about library
  changes.
- **Do not `Break` a running GL client and immediately launch another.**
  Killing a fullscreen GL app mid-frame (CTRL-C via `Break`) can leave CP
  submits in flight; relaunching Quake/probes right afterwards wedged the
  machine into a crash (2026-08-25). After stopping a GL client, run a
  small probe (e.g. `bootstrap_smoke`) to confirm the device is healthy
  before starting heavier clients, and prefer the client's own quit path.
- **Run `Avail Flush` after installing a new `LIBS:minigl.library`.**
  AmigaOS keeps the previously loaded resident library copy; without the
  flush, newly launched clients silently open the old code and benchmarks
  measure a stale binary while disk CRCs all match (2026-08-26).
- Automated benchmark loop: deploy the binary/configuration; run one timedemo;
  wait for completion (Quake `demo1` is about five minutes on this target);
  write its results to a `Work:` file; reboot; run `tools/wait_bridge.sh`;
  reconnect to the physical bridge; read the result file. Repeat three times
  and use the median. Between isolation runs, change only one variable.

### Quake Phase-0 loop

- Benchmark source: `/home/mirek/BlitzQuake_src/glquake`. Build it after the
  R200 `importlib` with `make -f Makefile.GCCAmiga68k_MiniGLDispatch`; output
  is `m68k_gcc/glquake/glquake68k_gcc`.
- The instrumented build accepts `-benchmarkcfg <file>` and executes that cfg
  after renderer cvars have registered. On timedemo completion it appends all
  MiniGL counters, including tick-to-ms conversions, to
  `id1/phase0_timedemo.log`.
- Deploy it as `Work:Games/BlitzQuake_68k/glquake_phase0`. The baseline cfg is
  `Work:Games/BlitzQuake_68k/id1/phase0_base.cfg`; it sets the fixed cvars and
  ends with `timedemo demo1`.
- Before a new series, delete `Work:Games/BlitzQuake_68k/id1/phase0_timedemo.log`.
  It is append-only, so archive or clear it between configurations.
- Launch from `Work:Games/BlitzQuake_68k` with a 1 MiB stack:
  `glquake_phase0 -width 640 -height 480 -bpp 32 -buf 3 -nojoy -nocdaudio -nopsx -nosound -benchmarkcfg phase0_base.cfg`.
  Omit `-windowmode` for fullscreen; do not use `-condebug`. `-buf 3` and
  `gl_videosync 0` provide three buffers without forced video sync.
- `demo1` measures about 969 frames and takes roughly six minutes at the
  current speed. It subsequently enters the normal demo loop; the result block
  is already complete once `demo1` ends. Reboot, run `tools/wait_bridge.sh`,
  and read the
  log before starting the next run.
