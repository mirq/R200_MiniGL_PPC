# WarpOS R200 Profiling Findings

Date: 2026-09-05

## Scope

This session added automatic performance attribution to the direct-R200
WarpOS frontend and attempted to measure Silent Hill and the gears demo with
the same `minigl_ppc.dll` plus `minigl_ppc_host` setup.

## Profiling Changes

The R200 diagnostic DLL build now enables profiling automatically with
`R200_PERF_AUTOREPORT=1` and records:

- frontend frame, serialization, execute, wait, present, residency and draw
  ticks;
- submit counts and record/generated dword counts;
- texture image/subimage call count, successful calls and source bytes;
- direct, shared and chunked upload route counts;
- total texture-call and direct-upload ticks;
- texture barrier count and ticks;
- texture, buffer and drain wait attribution;
- the slowest completed frame;
- live checkpoints for frames 1 through 10 and every 30 frames afterwards;
- a final summary during normal context deletion.

The PPC path previously omitted frame completion and wait accounting. It now
closes the same frame buckets as the native path, and PPC submit-slot reuse is
included in wait attribution.

Relevant files:

- `Makefile.dll.r200.gcc`
- `backend_r200/include/libraries/minigl_ppc_semantic_profile.h`
- `backend_r200/library/minigl_r200.c`
- `backend_r200/library/minigl_r200_internal.h`
- `backend_r200/library/minigl_r200_warpos_compile_probe.c`
- `backend_r200/library/r200_submit.c`
- `backend_r200/library/r200_surface.c`

The current tree builds successfully with:

```text
make -f Makefile.dll.r200.gcc warpelf
```

Output:

```text
bin/minigl_ppc_r200.warpelf
```

The existing compiler warnings predate this profiling work. Dispatch table
generation reports 134 fields bound and zero unsupported.

## Silent Hill Results

Physical target: `192.168.1.21:2345`.

The profiling DLL successfully converted and loaded. One run reached direct
R200 context creation and reported:

```text
[WOS-GL] ready: MiniGL_R200 / Radeon3D semantic R200
[Psy-X] GR_InitialiseRenderer: MiniGL ACTIVE
[SH] All subsystems initialized. Entering MainLoop...
```

That run was reset before 30 frames, so the original live-report interval did
not preserve a counter sample. The interval was subsequently changed to report
every one of the first ten frames.

Three later controlled attempts did not reach MiniGL at all. Their client logs
stop after the Psy-X build banner, and their host logs contain only host startup
without a context-create request. ELF disassembly identifies the next call as:

```text
PsyX_Initialise
  -> SDL_Init(SDL_INIT_VIDEO)
  -> SDL_InitSubSystem
  -> SDL_StartTicks
  -> SDL_VideoInit
```

`SDL_VideoInit` then selects the CyberGraphX backend through
`CGX_Available`, `CGX_CreateDevice`, `CGX_VideoInit`, event-loop startup and
cursor initialization.

Therefore these failed runs performed no MiniGL texture uploads, submits or
presents. They cannot support a claim that texture streaming caused the
observed startup stall. The next useful Silent Hill instrumentation belongs in
the Psy-X/SDL CyberGraphX initialization path, with flushed markers around each
stage above. Only the linked ELF is available locally at
`/home/mirek/Downloads/SilentHill WOS/SilentHill.elf`; the source/build tree was
not available during this session.

Retained target logs:

- `Work:minigl_testing/sh-prof-client.log`
- `Work:minigl_testing/sh-prof2-client.log`
- `Work:minigl_testing/sh-prof3-client.log`
- `Work:minigl_testing/sh-prof4-client.log`
- corresponding `sh-prof*-host.log` files

## Gears Results

### Incorrect 68k Baseline

`Work:minigl_testing/gears` is not a PPC DLL client. Its summary explicitly
reports:

```text
variant=68k
frames=190
fps_x1000=880
perf_frames=0
```

It ran at approximately 0.880 FPS and all PPC backend counters were zero. This
result must not be used as PPC DLL performance evidence.

Retained log:

```text
Work:minigl_testing/gears-prof-client.log
```

### Actual PPC Binary

The real PPC executable was built from:

```text
bin/examples/milestone_gears_stats_library_ppc.warpelf
```

It was CRC-verified during transfer, converted on target and installed as:

```text
Work:minigl_testing/gears_ppc
```

The first PPC run created a 640x480x32 windowed R200 context and registered the
semantic ring successfully, but rendered zero frames. The client printed setup
and immediately returned from `MGLMainLoop`; the host reported:

```text
execute_msgs=0
barrier=0
```

Root cause: the PPC context mirror did not copy the host-created `struct Window
*`. `Lib_mglMainLoop()` checks `context->window` and returned immediately when
it was null.

The tree now assigns:

```c
proofContext.window = (struct Window *)FrontendPlatform.windowHandle;
```

after successful context creation. The fixed WarpELF builds and was converted
to `DH2:wosbuild/minigl_ppc_r200_gearsfix.dll`, then copied to
`Work:minigl_testing/minigl_ppc.dll`.

The fixed DLL was not cleanly runtime-validated. The first, unfixed PPC gears
process remained loaded during teardown together with its DLL/host tasks even
though the client log reached teardown and the host printed `stopped`. A second
launch occurred in that contaminated session and is not valid evidence.

Retained logs:

- `Work:minigl_testing/gears-ppc-client.log`
- `Work:minigl_testing/gears-ppc-host.log`

## Current Target State

The target session contains stale processes from the unfixed PPC gears run:

```text
gears_ppc
Work:SilentHill WOS/minigl_ppc_host
minigl_ppc.dll
```

Do not perform another PPC launch or DLL conversion in that session. Reboot
first and run `C:InitPPC` only once after boot.

The fixed DLL was installed only at:

```text
Work:minigl_testing/minigl_ppc.dll
```

`Work:SilentHill WOS/minigl_ppc.dll` was not updated with the final window-mirror
fix during this session.

## Next Validation

1. Cold reboot the target.
2. Run `C:InitPPC` once.
3. Verify no `gears_ppc`, `minigl_ppc_host` or `minigl_ppc.dll` process remains
   in `Status` before launch.
4. Start one host:

   ```text
   Run >NIL: "Work:SilentHill WOS/minigl_ppc_host" >Work:minigl_testing/gears-ppc-fix-host.log
   ```

5. From `Work:minigl_testing`, run:

   ```text
   gears_ppc -window >gears-ppc-fix-client.log
   ```

6. Let at least ten frames complete, press Escape once, and verify normal
   process teardown.
7. Inspect `PPCPERF_LIVE`, `PPCPERF`, `PPCPERF_TIME`, `PPCPERF_TEX` and
   `PPCPERF_WORST` lines plus the host session summary.
8. Do not resume Silent Hill texture conclusions until the Psy-X SDL video
   initialization stall is instrumented or a run reaches the first live frame
   checkpoint.

## Converter Constraint

One apparent `Elf2Exe2` failure was caused by a truncated transfer: the target
file was 208,896 bytes while the source WarpELF was 220,978 bytes. Retransfer
with CRC verification fixed conversion. Always verify transfer size/checksum
before treating an `Elf2Exe2` timeout as a WarpOS converter failure.
