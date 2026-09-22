# Building the PPC stack

Everything here is a cross-build on Linux. Only the final `Elf2Exe2`
conversion and all runtime validation happen on the Amiga.

## 1. Toolchains

| Target | Tool | Notes |
|---|---|---|
| PPC/WarpOS | `ppc-amigaos-*` + MOS2WOS WarpOS SDK overlay | configured in `Makefile.wos.gcc.common`; do not use `-warpup` or OS4 defaults |
| 68k host | `/opt/amiga/bin/m68k-amigaos-gcc` (`M68K_CC`) | host built `-mcpu=68060 -noixemul` |
| 68k library | `m68k-amigaos-*` via the classic tree's makefiles | `R200_NATIVE_ROOT` points there |
| Converter | `Elf2Exe2` | target-hosted (`DH2:wosbuild/Elf2Exe2`); not on Linux |

`Makefile.wos.gcc.common` holds the shared PPC configuration:

| Variable | Default | Purpose |
|---|---|---|
| `WOS_TOOLCHAIN_BIN` | `/home/mirek/ppc-amigaos-gcc/native-build/root-cross/bin` | cross toolchain dir |
| `MOS2WOS_ROOT` | `/home/mirek/Downloads/gg` | WarpOS SDK overlay root |
| `WOS_SDK_ROOT` | `$(MOS2WOS_ROOT)/ppc-warpos` | headers, startup, runtime libs |
| `CPU` | `604e` | PPC scheduling target |
| `ELF2EXE2` | `elf2exe2` | converter command for the `all` target |

Override any of them on the command line without editing files:

```sh
make -f Makefile.dll.r200.gcc warpelf \
    WOS_TOOLCHAIN_BIN=/path/to/cross/bin MOS2WOS_ROOT=/path/to/mos2wos
```

The compiler is used with `-mcall-sysv -mbig-endian -mmultiple -fno-pic`,
`-fno-pie`, `-fno-stack-protector`, no unwind tables and **`-fno-jump-tables`**
(the last is required by `Elf2Exe2`).

> Toolchain pinning matters for performance work. `AGENTS.md` forbids
> substituting a different GCC major or a Docker toolchain for performance
> comparisons: changed code generation invalidates every binary baseline.

## 2. Primary target: the direct-R200 stack

`Makefile.dll.r200.gcc` builds both sides of the stack and the package.

### 2.1 Targets

| Target | Output | What it does |
|---|---|---|
| `warpelf` | `bin/minigl_ppc_r200.warpelf` | PPC DLL ET_REL: frontend + dispatch table + transport + Hyperion runtime |
| `frontend` | `obj/dll-r200/r200_frontend.o` | only the unity frontend object (fast syntax/iterate check) |
| `native-r200` | 68k library/import in `classic_68k/...` | delegates to `Makefile_dispatch_r200.gcc all importlib` |
| `host` | `bin/minigl_ppc_host` | builds `native-r200` then the 68k host, then runs the classifier check |
| `stack` | both of the above | `warpelf host` |
| `package` | `bin/r200-package/` | converts the DLL (needs host `Elf2Exe2`), copies host + `minigl.library` + `libminigl.a` + `README.txt`, writes `SHA256SUMS` |
| `all` | `bin/minigl_ppc_r200.dll` | `warpelf` then `Elf2Exe2`; fails with exit 127 on a host without the converter |

Typical developer flow on Linux:

```sh
make -f Makefile.dll.r200.gcc warpelf     # PPC side
make -f Makefile.dll.r200.gcc host        # 68k side + classifier check
make -f Makefile.import.gcc testprograms  # client archive + examples
```

### 2.2 Tunables (build-time)

These are `?=` variables; set them on the `make` command line. They change
timing and/or code size, so use isolated `OBJDIR`/`WARPELF` paths when
comparing (make does not track flag changes):

| Variable | Default | Effect |
|---|---|---|
| `MGLPPC_PROFILE` | 0 | PPC frontend profiler buckets (`obj/dll-r200-prof`, profile DLL) |
| `MGLPPC_CLIENT_TRACE` | 0 | per-line `DH2:wosbuild/mglppc_client.log` logger (diagnostic, denies performance) |
| `MGLPPC_HOST_TRACE` | 0 | per-line host log + 2 s console reports (diagnostic) |
| `MGLPPC_PACING` | 0 | present interval histogram (shutdown output only) |
| `MGLPPC_IDLE_DIAG` | 0 | host idle sleep counters |
| `MGLPPC_AUTOREPORT` | 0 | live frontend console reports |
| `MGLPPC_CP_SLOTS` | 128 | CP surface-descriptor cache capacity |
| `MGLPPC_EXPAND_MIN` | 4 | mismatch submits before state-batch expansion |
| `MGLPPC_HOST_TARGET_CACHE` | 1 | host target-surface query cache |
| `MGLPPC_INFLIGHT` | 0 | 1 = conservative one-outstanding-fence host model (A/B only) |

Recommended diagnostic build (separate paths so production artifacts are never
overwritten):

```sh
MGLPPC_PROFILE=1 make -f Makefile.dll.r200.gcc \
    OBJDIR=obj/dll-r200-prof WARPELF=bin/minigl_ppc_r200_prof.warpelf warpelf
MGLPPC_PROFILE=1 make -f Makefile.dll.r200.gcc \
    R200_HOST=bin/minigl_ppc_host_prof host
```

### 2.3 What the PPC link contains

`OBJS` in `Makefile.dll.r200.gcc`:

```
minigl_ppc_dispatch_r200.o   positional ABI-3 table + legacy/attach handling
r200_frontend.o              the whole semantic frontend (unity translation unit)
r200_cp_emit.o               CP-emitter adapter (links the driver's emitter source)
radeon3d_emit.o              shared emitter from $(R200_EMITTER_ROOT)/src
minigl_ppc_transport.o       Exec-port client
minigl_ppc_yield.o           bounded yield helper
minigl_ppc_serial.o          stwbrx vertex/TCL kernels
minigl_ppc_serial_altivec.o  AltiVec copy kernels
minigl_ppc_serial_vector.o   AltiVec support/verification C code
minigl_warpos_cache_flush.o  dcbf/sync flush, store barrier, 565 swap
r200_poweropen_bridge.o      PowerOpen→SysV texture-upload bridge
dllstartup.o dll.o dllimport.o   Hyperion DLL runtime
```

`R200_EMITTER_ROOT` defaults to `../p96-driver` and supplies
`src/radeon3d_emit.c`. **The emitter source is external to this repo**, so a
committed `warpelf` is not reproducible from HEAD alone; always rebuild and
record the new CRC.

### 2.4 Generated dispatch table

The R200 dispatch table is generated from the public header:

```
include/libraries/minigl_dispatch.h
   + backend_r200/generated/r200_symbols.txt   (nm of Lib_* in r200_frontend.o)
   -> tools/generate_dispatch_r200.py
   -> backend_r200/generated/dispatch_table_abi3.c
```

Make rules regenerate `r200_symbols.txt` and the table automatically when the
frontend object or the dispatch header changes. The generated file must not be
hand-edited; see [dispatch-abi.md](dispatch-abi.md) for the generation
contract.

### 2.5 68k host and the classifier check

```sh
make -f Makefile.dll.r200.gcc host
```

1. Builds the 68k R200 `minigl.library` + import archive in `R200_NATIVE_ROOT`
   (default `classic_68k/dev/MiniGL_Library_Source_Code`; the old
   `~/Downloads` copy is not authoritative).
2. Compiles `backend_r200/minigl_ppc_host.c` for 68060 and links
   `lib/libminigl_dispatch.a`.
3. Runs `tools/check_68k_host.py` over the linked binary. It fails the build if
   the host contains the byte strings `RACE`, `powerpc.library` or `AmigaAMP`,
   because Sonnet's loader scans raw hunks and would classify the 68k host as a
   PPC program (that mistake roughly halves throughput: measured 67.774 FPS vs
   140.752 FPS for the same host). The runtime option `MGLPPC_UPLOAD_TRACE`
   contains `RACE`; the host builds the name at runtime instead.

The host binary is a plain 68k AmigaDOS executable (no `ixemul`).

## 3. Client import archive and examples

`Makefile.import.gcc`:

| Target | Output |
|---|---|
| `importlib` (default) | `lib/libminigl_dispatch_ppc.a` + byte-identical `lib/libminigl.a` |
| `testprograms` | `bin/examples/<name>_ppc.warpelf` for all 15 examples |
| `gears-benchmarks` | only the gears benchmark ELFs (`milestone_gears_stats_library`, `milestone_gears_precalc`) |

`libminigl.a` is the historical drop-in name; `lib/libminigl_dispatch_ppc.a` is
the same archive. The archive contains three objects: `client/minigl_ppc_base.c`
(globals), `client/minigl_ppc_open.c` (`MiniGLOpen`/`MiniGLClose`) and
`hyperion_dll/dll.c` (loader).

Examples:

```
library_test                    milestone_ballonly_library
milestone_bounce_interactive_library  milestone_gears_stats_library
milestone_gears_precalc         r200_legacy_smoke
r200_texture_upload_smoke       r200_triangle_smoke
warp_library                    r200_mapping_bench
r200_texture_update_test        r200_bind_bench
r200_ib_smoke                   r200_defect_probe
minigl_testsuite
```

`milestone_gears_precalc` is compiled from the same source with
`-DMGL_GEARS_PRECALC=1`. `r200_mapping_bench` additionally links
`backend_r200/client/minigl_ppc_transport.c`.

## 4. Legacy mglQ3 DLL and stub

| Makefile | `warpelf` output | Purpose |
|---|---|---|
| `Makefile.dll.gcc` | `bin/minigl_ppc.warpelf` | mglQ3/Warp3D DLL; also builds `mglQ3/lib/libmgl-wos.a` |
| `Makefile.dll.stub.gcc` | `bin/stub/minigl_ppc.warpelf` | backend-free diagnostic DLL (prints every call) |

`make -C mglQ3 -f Makefile.wos.gcc` builds only the shared backend archive
(`mglQ3/lib/libmgl-wos.a`). A separate legacy archive `mglQ3/lib/libmgl.a`
(and its 68k sibling `mglQ3/libmgl-q3.a`, built from the classic tree) are
different artifacts — do not assume one updates another.

These targets are not the R200 path; use them only for legacy backend work or
the stub diagnostics.

## 5. The ELF contract (`warpos-etrel.ld`)

The final PPC link is a relocatable ET_REL image because `Elf2Exe2` consumes it:

```
$(LD) -r -EB -d -x -T warpos-etrel.ld \
    $(WOS_STARTFILE) $(OBJS) $(WOS_RUNTIME_LIBS) $(WOS_ENDFILE)
```

`warpos-etrel.ld` places `.text` at zero, gives `.bss` as `NOLOAD`, absorbs
`COMMON` into `.sbss` and discards host-only metadata (`.eh_frame`, `.comment`,
`.gnu.attributes`, `.note.GNU-stack`).

`WOS_VALIDATE_ETREL` runs on every final ET_REL link and fails the build when:

- any symbol is undefined,
- `_start` is not at offset `00000000` type `T`,
- any `COMMON` symbol remains,
- any relocation other than `R_PPC_ADDR32`, `R_PPC_ADDR16_LO`,
  `R_PPC_ADDR16_HA`, `R_PPC_REL24` is present.

These invariants are not optional: violating them produces an ET_REL that
`Elf2Exe2` converts incorrectly or rejects on the target.

## 6. Conversion and packaging

```
ELF (Linux cross-build)  ->  Elf2Exe2 on WarpOS  ->  .dll / executable
```

`make -f Makefile.dll.r200.gcc all` or `package` needs `Elf2Exe2` on the build
host's `PATH`; on Linux it stops with exit 127 and the message that conversion
is target-hosted. The conversion is deterministic but produces a **different
CRC** than the ELF — record both.

`make -f Makefile.dll.r200.gcc package` produces:

```
bin/r200-package/minigl_ppc.dll     (converted)
bin/r200-package/minigl_ppc_host    (68k)
bin/r200-package/minigl.library     (68k direct-R200)
bin/r200-package/libminigl.a        (client archive)
bin/r200-package/README.txt
bin/r200-package/SHA256SUMS
```

## 7. Classic-tree builds (used by the PPC stack)

The 68k side is owned by `classic_68k/dev/MiniGL_Library_Source_Code/` and its
`AGENTS.md`; the PPC makefile only calls it:

```sh
make -C classic_68k/dev/MiniGL_Library_Source_Code \
     -f Makefile_dispatch_r200.gcc all importlib
```

Notes that affect the PPC stack:

- The R200 backend is freestanding 68060/68881 and must have its dispatch table
  regenerated after any `r200_*.c` frontend edit (`dispatchtable` target).
- `dev/include/`, `dev/lib/`, `classic_68k/bin/`, `classic_68k/demos/` and the
  root `bin/` are packaged release copies; the makefiles do not update them.
- Builds may need to be run in separate invocations (clean, then build) after
  header changes.

## 8. Common build failures

| Symptom | Cause | Fix |
|---|---|---|
| `Elf2Exe2 is target-hosted … exit 127` | converter not on host | use the `warpelf` target; convert on the Amiga |
| `check_68k_host.py` failure | host binary contains a banned classifier string | remove/inhibit the literal; the runtime builder exists for `MGLPPC_UPLOAD_TRACE` |
| frontend object does not rebuild after header edit | unity dependency list | make handles it; run `make clean` if a stale object is suspected |
| `libminigl_dispatch.a` not found during `host` | `native-r200` has not run | run `make -f Makefile.dll.r200.gcc host`, not the host compile alone |
| PPC link validation fails | undefined/COMMON/reloc violation | fix the link inputs; do not relax `WOS_VALIDATE_ETREL` |
| wrong SDK / OS4 headers | toolchain override incomplete | check `WOS_CPPFLAGS` and `MOS2WOS_ROOT` |

## 9. Reproducibility and deployment

- A successful cross-build is **not** deployment validation. Deploy per
  [client-guide.md](client-guide.md) and benchmark per
  [benchmarking.md](benchmarking.md).
- Always record commit + dirty state, artifact CRCs (ELF and converted), the
  driver pair, and the exact commands. Same-size WarpELFs can be different
  builds; only CRC identifies them.
