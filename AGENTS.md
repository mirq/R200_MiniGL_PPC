# Repository Guide — MiniGL V19 WarpOS + Classic + Radeon driver

## Target hardware (physical device — never an emulator)

- Amiga 4000 (68060) with a Prometheus PCI adapter, a Radeon 9200 (RV280)
  in the Prometheus, and a PMC PPC card with a MPC7410 CPU running WarpOS.
- This is real hardware. Do NOT use `amiga_emulator_*` lifecycle/state
  commands and do not treat the local fs-uae status as the target; those
  belong to an unrelated local emulator process.
- Primary AmigaBridge endpoint: `192.168.1.21:2345`. Always connect with
  that explicit host and port; localhost defaults fail. After a cold boot
  (80-100 s) poll with `/home/mirek/minigl_ppc/tools/wait_bridge.sh`, then
  reconnect explicitly; do not rely on a fixed sleep.
- This machine cannot be recovered through software: after a grey screen,
  guru, or hard GPU hang, require an operator cold power cycle. A warm
  reboot does not reliably reset the Radeon and can leave windowed
  presentation broken while simpler tests still pass.

## Three cooperating trees

- `.` (this repo): the WarpOS PPC stack — `minigl_ppc.dll` semantic
  frontend (`backend_r200/`), Hyperion loader (`hyperion_dll/`), dispatch
  ABI (`include/`, `ppc_dll/`), client archives and examples.
- `classic_68k/`: the classic 68k V19 driver tree.
  `dev/MiniGL_Library_Source_Code/` owns the 68k `minigl.library`
  (dispatch tables, wrapper, import stubs); `dev/MiniGL_Library_Source_Code/AGENTS.md`
  carries the detailed V10 dispatch ABI, R200, MiniGLPPC/WarpOS,
  physical-device and benchmarking knowledge (paths rooted there).
- `~/p96-driver`: the Picasso96 matched pair — `Prometheus.card`
  (PCI/board owner) and `Radeon9200.chip` (RV280 + Radeon3D service).
  Its `R200_3D_PROGRESS.md` and `RADEON3D_SUBMISSION.md` track results
  and the Radeon3D service contract.

## Documentation — must stay current (mandatory)

- `docs/` is the single source of developer documentation for this repo and is
  **PPC-first**: the WarpOS/PPC direct-R200 stack is the primary subject.
  `docs/README.md` is the index; a new developer starts there.
- **Whenever code, protocol, ABI, build, performance or session-rule behavior
  is changed and committed/pushed to the remote, the affected documents must
  be updated in the same change set.** A doc update is part of the change,
  never a follow-up task.
- Minimum update map:
  - dispatch/API/ABI → `docs/dispatch-abi.md`
  - protocol/ring/mailbox/commands/caps → `docs/transport-protocol.md`
  - 68k host behavior/options → `docs/host-68k.md`
  - PPC frontend internals/batching → `docs/frontend-r200.md`
  - texture paths/formats/costs → `docs/textures.md`
  - build system/makefiles/toolchain/outputs → `docs/building.md`
  - performance → `docs/performance.md` (measured values + conditions; label
    measured/derived/estimated; never invent cycle counts)
  - hardware/benchmark procedure → `docs/benchmarking.md`
  - tests/tools → `docs/testing.md`
  - client runtime/deployment/recovery → `docs/client-guide.md`,
    `docs/troubleshooting.md`
- New documents must be added to the `docs/README.md` index. Moving or
  deleting a document requires updating every reference (grep the whole repo,
  including `classic_68k/` and source comments).
- Historical session notes, plans, handovers and findings files are not
  documentation: fold durable facts into `docs/` and delete the note; git
  history preserves the rest.
- Deep specifications and regression evidence live in `docs/reference/`; keep
  their cross-references valid when moving them.

## Build and Validation

- There is no default root `Makefile`; always select one with `-f`. There
  is no host-side automated test suite anywhere: at minimum run the
  relevant builds; runtime validation requires the physical machine.
- PPC/WarpOS builds need the `ppc-amigaos-*` tools and the MOS2WOS WarpOS
  SDK overlay (defaults in `Makefile.wos.gcc.common`).
  - `make -f Makefile.dll.gcc warpelf` → `bin/minigl_ppc.warpelf` +
    `mglQ3/lib/libmgl-wos.a`. Final DLL conversion (`make -f
    Makefile.dll.gcc`) only where target-hosted `Elf2Exe2` runs.
  - `make -C mglQ3 -f Makefile.wos.gcc` builds only the bundled backend.
  - `make -f Makefile.import.gcc` → client archives
    (`lib/libminigl_dispatch_ppc.a`, byte-identical `lib/libminigl.a`);
    `... testprograms` builds `bin/examples/`.
  - `make -f Makefile.dll.stub.gcc warpelf` builds the backend-free
    diagnostic ELF.
  - R200 stack: `make -f Makefile.dll.r200.gcc warpelf` (DLL), `host`
    (68k `bin/minigl_ppc_host` via `m68k-amigaos-gcc`), `stack` (both),
    `package` (installs into `bin/r200-package/`). `R200_NATIVE_ROOT`
    points at `classic_68k/dev/MiniGL_Library_Source_Code`
    (the 68k minigl.library source; the `~/Downloads` copy is the
    previous location and no longer the authoritative one).
- Classic tree (`cd classic_68k`):
  - `make -C mglQ3 -f makefile` → directly linked `mglQ3/libmgl-q3.a`.
    `mglQ3/lib/libmgl.a` is a different legacy artifact (built from
    `mglQ3/Build_gcc`, host-specific paths); do not assume one updates
    the other. Clean and build in separate invocations.
  - 68k library backends from `dev/MiniGL_Library_Source_Code/`:
    classic/stub (hard-float, `-noixemul`), `Makefile_dispatch.gcc`
    (clib2, 020/68881),
    R200 (freestanding 68060/68881; `dispatchtable` target first). vbcc
    and WarpOS/PPC builds are owned by the `*.vbcc` makefiles; preserve
    each backend's flags.
  - `dev/include/`, `dev/lib/`, root `bin/`, `demos/` are packaged
    release copies; makefiles do not update them.
- p96-driver: cross-compile-only with `/opt/amiga/bin/m68k-amigaos-gcc`
  via `make`. Do not substitute a Docker toolchain or different GCC major
  version for performance comparisons — changed code generation invalidates
  binary and hardware baselines. Keep `src/startup.c` and `src/library.c`
  first in link order (`_start` and resident data in the first code hunk).
  Treat warnings as defects.
- Converter contract (PPC side): `warpos-etrel.ld`, `-fno-jump-tables`,
  `-d`, `-x`. Final ET_REL links must have `_start` at `.text+0`, no
  undefined/COMMON symbols, and only `R_PPC_ADDR32`, `R_PPC_ADDR16_LO`,
  `R_PPC_ADDR16_HA`, `R_PPC_REL24` relocations.

## Runtime Boundaries

- Consumer flow: `include/proto/minigl.h` → inline wrappers in
  `include/libraries/minigl_dispatch.h` → `client/` import archive →
  Hyperion loader → `MGLGetDispatchTable()` in `ppc_dll/` → bundled
  implementation (`mglQ3/` or the R200 frontend). `MiniGLBase` is a
  Hyperion DLL instance handle, not an Amiga `struct Library *`; no 68k
  `OpenLibrary()` and no 68k/PPC transition per GL call.
- `mglQ3/src/context.c` owns `mini_CurrentContext`. Dispatch clients use
  `include/mgl/gl.h` via `MINIGL_DISPATCH_DECLARATIONS` and must not
  import private context/vertex layouts. `include/mgl/` and
  `mglQ3/include/mgl/` differ and must not be bulk-synced.
- Classic shared library: one public LVO
  (`MiniGLGetDispatchTableLVO`), one global current context; `LibClose`/
  `LibExpunge` clear it. Shared-library clients use `<proto/minigl.h>`,
  `MiniGLOpen()`/`MiniGLClose()`, `libminigl.a`; directly linked clients
  use `mgl/` headers and `MGLInit()`/`MGLTerm()`.

## backend_r200: WarpOS direct-R200 stack

- There is **no Warp3D/Warp3DPPC driver for the R200**; that absence is
  why this repo builds its own `minigl_ppc.dll`. Do not route this work
  through Warp3D/Warp3DPPC.
- Design: the PPC DLL carries the R200 semantic frontend and talks over
  shared memory (ring + mailbox + serial transport, protocol v15) to the
  68k helper `backend_r200/minigl_ppc_host.c` (port
  `MiniGLPPC.R200.Host.1`), which calls the 68k `minigl.library` v12
  direct-R200 backend (uses `Radeon9200.chip` via `Radeon3D.library`).
  PPC code cannot call that 68k library directly (mixed-mode calls trip
  the "illegal run68k function execution" trap).
- `Elf2Exe2` is target-hosted (`DH2:wosbuild/Elf2Exe2`). Deploy as ZIP
  (`UnZip -o x.zip -d DH2:wosbuild`) and CRC32-verify both ends; three
  302k+ WarpELFs of the same size can be different builds — only CRC
  identifies them.
- Target session rules: `C:InitPPC` exactly once per boot before any
  WarpOS/PPC client; a wedged PPC client ignores Exec break signals and
  needs a cold reboot (recover from a wedged launch with a reboot, not
  another `InitPPC`). Hard resets lose `Work:` directory metadata but
  preserve `DH2:` — keep diagnostic evidence on `DH2:`
  (`DH2:wosbuild/mglppc_client.log`, `DH2:wosbuild/mglppc_host.log`;
  per-line open/append/close). Amiga-side `Status` shows only 68k views.
- DLL console output is gated behind `MGLPPC_VERBOSE` (SetEnv before
  launch); it is silent by default because console.device writes
  dominated frame time. Benchmark runs must leave `MGLPPC_VERBOSE`,
  `MGLPPC_UPLOAD_TRACE` and similar diagnostics unset, and the trace
  loggers compiled out (see `MGLPPC_CLIENT_TRACE` / `MGLPPC_HOST_TRACE`).

## Dispatch ABI

- `MGLDispatchTable` is a positional ABI. ABI version stays `3`;
  preserve every released V10 field through `GLPolygonOffset` in exact
  order and append new fields only after that boundary.
- Any dispatch API change must keep the struct and client wrappers in
  `include/libraries/minigl_dispatch.h`, the real positional initializer
  (`ppc_dll/minigl_ppc_dispatch.c`, and the R200 generated table), and
  the stub initializer/implementations synchronized — including all four
  `library/dispatch_table_*.c` in the classic tree and the packaged copy
  under `dev/include/`.
- Normal PPC layout; do not force two-byte packing. Do not add
  `FromWindow`/`CreateContextFromWindow`.
- Intentional fallbacks unless implemented and validated:
  `glClientActiveTextureARB`, `mglResizeContext`,
  `mglEmergencyCloseScreen`, `glMGLForceNewRenderPass` are no-ops;
  `glMultiDrawArrays` loops `GLDrawArrays`; `mglGetInputWindowHandle`
  returns the ordinary window handle.
- The Silent Hill reference DLL has a private ABI-3 fast-path tail at
  offsets `0x218..0x220`; `SilentHill.elf` was verified not to access
  those slots (prefix-compatible). Do not claim compatibility for other
  clients that call the private extension.

## Radeon driver architecture (~/p96-driver)

- `Prometheus.card` owns PCI enumeration, board claiming, configuration,
  address translation, interrupt registration, and the shared DMA arena.
- `Radeon9200.chip` owns RV280 init, display, cursor, 2D callbacks, and
  the Radeon3D service; it must not enumerate independently. After the
  validated Prometheus handoff (passed in `BoardInfo.CardData`; per-board
  state in `BoardInfo.ChipData`), `Radeon9200.chip` is sole owner of
  Radeon MMIO, CP submission/reset, and private VRAM. GL clients must use
  the bounded semantic Radeon3D service and never touch PCI, MMIO, the CP
  ring, or VRAM directly.
- BAR0 = framebuffer aperture, BAR2 = RV280 MMIO; validate ownership,
  type, address, size before use. Radeon registers are little-endian:
  `SWAPWORD()`/`SWAPLONG()` at mapped MMIO boundaries only (never for PCI
  config or addresses). Hot path needs mapped, endian-correct volatile
  aperture access; per-register PCI-library calls are too expensive.
- Use the complete supplied Picasso96 CardDevelop 3.6 `BoardInfo`; never
  replace it wholesale. Callbacks are entered without the chip library
  base guaranteed in A6. Do not disable or alter an unclaimed board.
- Initial PCI IDs: ATI `1002:5960/5961/5964`. CRTC0 + primary VGA DAC is
  the conservative path; internal TMDS/DVI only for a validated COMBIOS
  table; CRTC1, external TMDS, TV out, overlays, interrupts out of scope.
  64x64 ARGB cursor and CP ring use private VRAM reservations; `DMASIZE`
  is a page-aligned high-VRAM arena published through Prometheus DMA
  vectors — a valid positive arena is required.
- P96 2D uses bounded direct MMIO even with the CP active
  (`FillRect`, `InvertRect`, `BlitRect`, `DrawLine`, JAM1/JAM2 templates,
  complete 4-bit minterm copies). FIFO/idle waits are bounded; failed
  recovery must not permit unsafe VRAM rendering.
- Radeon3D is an active bounded service (driver 3.0, interface 17; do not
  copy older interface numbers from consumer notes). Follow
  `RADEON3D_SUBMISSION.md` and reconcile consumer headers under
  `third_party/radeon3d/include/` before ABI changes.
- Check fixed-function behaviour against Mesa 7.11.2 classic r200
  (`src/mesa/drivers/dri/r200`, plus `src/mesa/tnl/t_vb_texgen.c`,
  `t_vb_light.c` for exact arithmetic) before writing it and before
  trusting a probe's expected values; hand-derived expectations are a
  common source of false failures.
- Sources preference: local Picasso96 CardDevelop headers/examples;
  Prometheus headers/source; Mesa R200; NetBSD `radeonfb`. Never inspect
  or copy `/home/mirek/warp3d-r9200/Prometheus/PrometheusCard`.

## Physical-device session and deployment rules

- Sonnet LoadSeg scans raw hunk bytes for `RACE`, `powerpc.library`, and
  `AmigaAMP`. In particular, the literal `MGLPPC_UPLOAD_TRACE` falsely marks a
  68k host as PPC and can halve throughput. Keep the linked-host classifier
  check in `Makefile.dll.r200.gcc`; preserve the runtime option via its builder.
  File protection bits cache classification: for the **68k host only**, retain
  unrelated bits and set `(bits & ~0x00030000) | 0x00020000` after replacing a
  previously misclassified file. Verify bits as well as CRC. Never apply this
  classification override to genuine PPC clients/DLLs. See
  `docs/reference/HOST_LOADER_REGRESSION.md` for the unchanged-binary proof.

- Never push, pull, copy, checksum, or otherwise mutate/query Amiga files
  in parallel. Serialize all Amiga filesystem and DOS operations;
  concurrent bridge requests collide through shared temp files and
  produce missing or misleading results.
- Amiga-side deployment names at most 16 characters; verify destination
  CRC32 after every transfer (long names can alias after truncation and
  silently overwrite another test build).
- Install the matched pair as `LIBS:Picasso96/Radeon9200.chip` +
  `LIBS:Picasso96/Prometheus.card`; replace both together, then cold
  reboot. Keep the last known-good pair as
  `LIBS:Picasso96/Radeon9200.chip.previous` +
  `LIBS:Picasso96/Prometheus.card.previous` (never only one component).
  Before an experimental pair, copy both active files to those names.
- `S:startup-sequence` recovery block runs before `LoadMonDrvs`: holding
  the right mouse button makes `C:TestRMB` return `WARN` and restores
  both `.previous` files only when both exist. Reserve RMB for recovery;
  never hold it for a normal experimental boot.
- Monitor ToolTypes before cold reboot: pair `BOARDTYPE=Prometheus` with
  `SETTINGSFILE=SYS:Devs/Picasso96Settings.9200`; changing only
  `BOARDTYPE` can leave Workbench on the native fallback screen.
- After replacing `LIBS:minigl.library`, run `Avail Flush` before
  launching clients (AmigaOS may retain/reopen the old resident code
  despite matching disk CRC).
- Prefer each GL client's normal quit path. Do not `Break` a fullscreen
  client and immediately launch another (in-flight CP work can wedge the
  machine); after an interruption run a small bootstrap/service probe
  before a heavier workload.
- A successful cross-build is not deployment validation. Record commit +
  dirty state, artifact CRCs/hashes, backend/driver pair, CPU and board,
  VRAM/ROM, ToolTypes, mode/depth, windowed/fullscreen, cold/warm state,
  and exact tests run on hardware.

## Benchmarking

- Change one variable at a time; three independent runs with the full
  cold-boot protocol; report the median and every sample.
- Benchmarks use the `-frames N` auto-exit path and read the
  console-independent `GEARS_RUN frames=... fps=...` summary.
- Diagnostic loggers (client trace, host trace, verbose console,
  per-line marker logs) must be compiled out or env-gated OFF for
  benchmark runs; console.device writes dominate frame time.
