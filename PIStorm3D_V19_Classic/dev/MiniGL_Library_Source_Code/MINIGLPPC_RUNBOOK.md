# MiniGLPPC WarpOS/R200 Runbook

## Validated configuration

- Physical 68060/WarpOS/Radeon R200 system
- MiniGLPPC bridge protocol v14 (with replay-v9 compatibility capability)
- Reference benchmark: windowed 800x600, 32-bit depth
- Exact `examples/gears_benchmark.c` PPC build
- `Work:minigl_demos/data/chrome.ppm` environment map
- FPS overlay enabled

The first successful protocol-v7 run on 2026-08-21 displayed the textured
rotating gear at 640x480x32 and reported approximately 5.5 FPS. That result is a
historical bring-up milestone, not the current performance baseline; see
`MINIGLPPC_130FPS_PLAN.md` for current protocol-v14 measurements.

## Build

From the SDK root on the Linux host:

```sh
make -f Makefile_warpos.vbcc clean
make -f Makefile_warpos.vbcc
PATH=/opt/amiga/bin:$PATH make -f Makefile_dispatch_r200.gcc gears-benchmarks
```

Relevant outputs:

```text
bin/miniglppc-bridge/minigl_ppc_host
bin/gears68k
bin/gears68k_precalc
bin/warpos/gearsppc
bin/warpos/gearsppc_altivec
bin/warpos/gearsppc_precalc
bin/warpos/run_gearsppc
```

The five benchmark binaries have one workload split:

| Binary | CPU | Gear geometry |
|---|---|---|
| `gears68k` | 68060 | Generated every frame |
| `gears68k_precalc` | 68060 | Recorded once, replayed as GL calls |
| `gearsppc` | PPC | Generated every frame |
| `gearsppc_altivec` | PPC/AltiVec | Generated every frame |
| `gearsppc_precalc` | PPC | Recorded once, replayed as GL calls |

The AltiVec build currently accelerates aligned matrix copies and reports
`vector_copies` and `scalar_copies`; it does not claim vectorized gear math.
The live variants intentionally restored per-frame trig evaluation, so results
from the older cached-trig benchmark require a fresh baseline.

Deploy the host and PPC executable as:

```text
Work:minigl_ppc_host
Work:gearsppc
Work:gearsppc_altivec
Work:gearsppc_precalc
Work:run_gearsppc
```

Set the launcher's AmigaDOS script protection bit (`Protect ... +S`) after a
raw file transfer.

## Cold-start procedure

After every Amiga restart, initialize WarpOS exactly once:

```text
InitPPC
```

Do not put another `InitPPC` invocation in the launcher. Re-running it in the
same boot has previously wedged WarpOS.

For the 800x600 reference configuration, run the convenience launcher:

```text
Execute Work:run_gearsppc
```

The equivalent manual commands are:

```text
Run >NIL: Work:minigl_ppc_host >NIL:
Wait 1 SECS
Work:gearsppc -width 800 -height 600 -depth 32 -window -overlay -envmap Work:minigl_demos/data/chrome.ppm
```

The wait is required so `MiniGLPPC.R200.Host.1` exists before PPC
`MiniGLOpen()` performs its protocol handshake. The explicit absolute PPM path
avoids dependence on the current directory.

## Recovery after a PPC exception

Do not repeatedly relaunch after a PowerPC exception. Restart the Amiga, wait
for Workbench and the bridge to return, call `InitPPC` once, and repeat the
cold-start procedure above.

Host and client must come from the same build because protocol version
mismatches return status 2.

## Step 0 optimization probes

Build all compile and hardware probes:

```sh
make -f Makefile_warpos.vbcc step0-probes
```

The portability target compiles the production `library/r200_unity.c` for PPC
and is complete when this command succeeds. Deploy these hardware artifacts:

```text
bin/miniglppc-bridge/minigl_ppc_host -> Work:minigl_ppc_host_step0
bin/warpos/minigl_warpos_vram_probe -> Work:minigl_warpos_vram_probe
bin/warpos/minigl_warpos_altivec_probe -> Work:minigl_warpos_altivec_probe
bin/warpos/run_altivec_probe -> Work:run_altivec_probe
```

For an isolated aperture measurement, launch the host without opening the
MiniGL dispatch ABI:

```text
Run >NIL: Work:minigl_ppc_host_step0 --probe-only >T:minigl_ppc_host_step0.log
Wait 1 SECS
Work:minigl_warpos_vram_probe
```

The host leases one private 64 KiB Radeon streaming segment. The PPC client
first writes one sentinel, waits for 68k confirmation, performs 64 timed passes,
and asks the host to verify all 16,384 final dwords before the lease is freed.
No display or command-ring memory is used. Treat a PPC exception during the
sentinel write as an unsupported aperture mapping and cold-reboot immediately.

The AltiVec executable performs a four-float arithmetic check and seeds/checks
VR20-VR31 across 200 `WaitTime()` calls. Run it alone first:

```text
Work:minigl_warpos_altivec_probe A
```

Then set the script bit and run two differently seeded processes concurrently:

```text
Protect Work:run_altivec_probe +S
Execute Work:run_altivec_probe
```

Both `Work:altivec_probe_a.log` and `Work:altivec_probe_b.log` must report
`status=ok`. An illegal-instruction exception or register mismatch rejects the
AltiVec path until the WarpOS vector-state contract is fixed.

The VRAM probe supports two diagnostic exits. `--abort-after-begin` verifies
that an unconfirmed lease is released; a normal probe immediately afterward
must not return `BUSY`. `--shutdown-host` cleanly exits a probe-only host.
If the client itself crashes while holding a lease, restart the host before the
next probe; there is no lease timeout or out-of-band abort command.

Execute phase attribution is available from the release driver ABI. Capture
`radeon3dinfo` immediately before and after one fixed workload and subtract
`R3DEXEC` calls, record/generated dwords, and copy/build/submit microseconds.

## Step 1 frontend link proof

Build the PPC proof and its same-source 68060 reference:

```sh
make -f Makefile_warpos.vbcc r200-frontend-proof
```

Deploy and run these in order after the normal one-time `InitPPC` setup:

```text
bin/miniglppc-bridge/minigl_r200_m68k_frontend_proof -> Work:minigl_r200_m68k_frontend_proof
bin/warpos/minigl_r200_warpos_frontend_proof -> Work:minigl_r200_warpos_frontend_proof

Work:minigl_r200_m68k_frontend_proof >Work:r200_record_68k.txt
Work:minigl_r200_warpos_frontend_proof >Work:r200_record_ppc.txt
```

Both files must be byte-identical. Their first line is:

```text
R200_FRONTEND_PROOF status=ok serial=9
```

The second line starts with `R200_SEMANTIC_RECORD dwords=320` and contains every
serialized dword in order:
untextured, resident-textured, and hardware-lit TCL triangles. The validated
physical run produced two byte-identical 2,951-byte files with CRC32
`587834ED`.

This proof uses exactly representable matrix inputs and the default enabled
directional light. It does not yet establish cross-CPU identity for general
rotations, transformed local/spot lights, fog, `MGL_FLAT*`, texgen, or streaming
commits.

The executables link the complete production `r200_unity.c`; Radeon3D and P96
calls are deliberately stubbed. The record itself is produced through the
production immediate-mode, pending-snapshot, and inline submit-slot serializer
without device ownership or submission.

## Historical Step 2 inline semantic smoke

Build the additive Step 2 host and PPC producer:

```sh
make -f Makefile_warpos.vbcc step2-semantic
```

Deploy these files sequentially:

```text
bin/miniglppc-bridge/minigl_ppc_host -> Work:minigl_ppc_host_step2
bin/warpos/minigl_r200_semantic_step2 -> Work:minigl_r200_semantic_step2
```

After the boot's one and only `InitPPC`, launch the host asynchronously, then
run the producer:

```text
Work:minigl_ppc_host_step2 >Work:minigl_ppc_host_step2.log
Work:minigl_r200_semantic_step2
```

The validated physical result is:

```text
STEP2_SEMANTIC status=ok frames=120 slots=2 executes=120 center=9ba059ff corner=140805ff
```

The producer alternates two public 8192-dword slots. It flushes each used PPC
cache range before publication; the host invalidates its 68k view, verifies the
generation and CRC32, retires the previous fence, calls `Radeon3DExecute()`, and
drains before each windowed present. The final barrier reads the back buffer and
requires the triangle center to differ from the cleared corner. Seven consecutive
runs, 840 frames total, produced the result above without a CRC, generation,
Execute, fence, or readback failure.

This was an initial matching-tree boundary test rather than the general PPC
MiniGL library. At that point it supported windowed untextured inline records
only; textures, fullscreen, resize, and context-loss recovery were outside its
scope, and the protocol-v10 replay client remained the application fallback.

## Historical Step 2 dispatch-library smoke (protocol v10)

Build the source-compatible WarpOS MiniGL archive and ordinary dispatch client:

```sh
make -f Makefile_warpos.vbcc r200-semantic-library
```

Deploy the host and client sequentially:

```text
bin/miniglppc-bridge/minigl_ppc_host -> Work:minigl_ppc_host_semantic_lib
bin/warpos/minigl_warpos_semantic_library_smoke -> Work:minigl_warpos_semantic_library_smoke
```

On filesystems with short filename limits, use distinct short names such as
`Work:mglppc_h10`, `Work:mglppc_tex10`, and `T:mglppc_tex10.log`. Do not append
`.log` to a long executable name: truncation can make the log collide with and
replace the executable.

After the boot's one and only `InitPPC`, launch the host asynchronously and run
the client:

```text
Work:minigl_ppc_host_semantic_lib >Work:minigl_ppc_host_semantic_lib.log
Work:minigl_warpos_semantic_library_smoke
```

The validated physical result at 320x240, 32 bpp, windowed is:

```text
PPC_SEMANTIC_LIBRARY status=ok frames=120
```

The protocol-v10 texture client was 208,920 bytes, CRC32 `AE45051F`. It exercised
`MiniGLOpen()`, the normal ABI-v4 dispatch wrappers, context creation, matrix and
immediate-mode calls, host-authoritative 4x4 RGBA upload, padded 3x2 RGB
level-0 redefinition, a four-chunk 64x64 RGBA redefinition, 120 textured
clear/draw/present frames, texture deletion, `glFinish()`, window-handle access,
second-context rejection, and clean `GL_INVALID_OPERATION` failures for
unsupported readback and back-buffer locking. `MiniGLClose()` destroys the
context and shuts down this single-client host; restart the host before opening
the archive again. The host stopped normally and native 68k bootstrap smokes
passed immediately before and after.

At that protocol-v10 milestone, this archive was matching-tree and supported one
windowed context. Texture V1 was limited to unit 0, non-zero names, RGB/RGBA
unsigned-byte `glTexImage2D`, and PPC-side filter/wrap state;
`glTexSubImage2D`, unit 1 texture ownership, other source formats, and
device-epoch refresh were unsupported. `glReadPixels`, `mglLockBack`,
screenshots, fullscreen, resize, and context-loss recovery were also
unsupported, so the replay-v9 archive was used when those operations were
required. These limitations describe the archived v10 validation client, not
the current protocol-v14 transport.
