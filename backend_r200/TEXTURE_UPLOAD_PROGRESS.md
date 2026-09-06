# Texture Upload Implementation Gates

## Scope

Implement driver-managed texture uploads with correct GL image/subimage
semantics, bounded storage reuse, and ordered CPU/GPU visibility. Software
rendering is not an alternative. Preserve the public PPC ABI-3 dispatch layout.

Do not remove upload ordering barriers or enable persistent in-place PPC
updates until the visibility and mapping gates below pass.

## Build Provenance

- PPC frontend: this tree's `backend_r200/library/`, built through
  `Makefile.dll.r200.gcc`.
- Native backend: `R200_NATIVE_ROOT`, defaulting to
  `/home/mirek/Downloads/PIStorm3D_V19_Classic/dev/MiniGL_Library_Source_Code`.
  The host links that tree's dispatch import archive and opens its native
  `minigl.library`. This is NOT the older `/home/mirek/minigl_ppc` backend.
- Driver: `/home/mirek/p96-driver`. Existing worktree modifications predate
  this implementation and must be preserved.
- Target ELF conversion: `DH2:wosbuild/Elf2Exe2`, not a command on the current
  default Amiga shell path.

## Implemented So Far

- `examples/r200_texture_upload_smoke.c`: calibrated timebase reporting,
  separate pattern/upload/present intervals, retained upload errors, and
  corrected packed RGBA tile source rows. GPU completion and physical bus
  bandwidth are NOT measured by TexImage call duration.
- `examples/r200_mapping_bench.c`: identical bounded store/maintenance loops
  over registered geometry, the granted texture mapping, and cached RAM.
  Cannot force both P96 and aux storage with the existing transport; explicitly
  reports the full mapping gate incomplete. No sampling or verification claim.
- Host BEGIN/COMMIT telemetry reports raw ticks and frequency, not incorrectly
  labeled microseconds. COMMIT logging is gated by `MGLPPC_UPLOAD_TRACE`.
- Driver tree `tools/r3dtexupdate.c`: bounded, pixel-verified native service
  probe for 565/BGRA in-place writes, distinct live allocations and same-address
  reuse. Build target `r3dtexupdate`; no driver implementation change.

## Physical Run: 2026-09-05

Existing boot, not a controlled cold-boot performance comparison. Connected
to physical bridge `192.168.1.21:2345`. Waited for SilentHill/host/DLL processes
to exit; did not interrupt the fullscreen client. Bootstrap passed windowed
at depth 16 before running the new offscreen probe.

Installed driver pair, measured on disk (not replaced):

| Artifact | CRC32 | Bytes |
| --- | --- | --- |
| `LIBS:Picasso96/Radeon9200.chip` | `07A7193D` | 76588 |
| `LIBS:Picasso96/Prometheus.card` | `18A453D6` | 7796 |
| `Work:minigl_testing/r3dtexupdate` | `4EA25643` | 16800 |

Probe reported interface 17, generation 2, device 0x5964, caps 0x03fff7ff,
EClock 709379 Hz. Result:

```text
R3DTEXUPDATE status=PASS result=0 checks=176 pixel_failures=0 address_reuses=16
```

Complete logs: `Work:minigl_testing/texvis.log` and
`/tmp/opencode/texvis-20260905.log` (31388 bytes, pull CRC verified).

This demonstrates native service visibility for the tested 64x64 textures,
formats and waited Execute sequence. It does not establish PPC cache
visibility, streaming-commit behavior, asynchronous ordering or throughput.
It contradicts an unconditional claim that same-address texture updates are
impossible. One run is not the three-run cold-boot validation gate.

## Current Blocker

The converter blocker below was recovered after the operator reboot: Status
showed no stale PPC process, C:InitPPC succeeded, and Elf2Exe2 then completed.
The current blocker is loss of bridge connectivity following the subsequent
Silent Hill GL application launch. Display/game status needs operator
confirmation; do not infer a GPU hang from networking alone.

New PPC smoke ELF transferred with CRC verification to
`DH2:r200test/txsmoke.elf` (117381 bytes). Conversion command:

```text
DH2:wosbuild/Elf2Exe2 DH2:r200test/txsmoke.elf DH2:r200test/txsmoke
```

The command remained loaded in `Status` for several minutes without output.
No further PPC launches or repeated InitPPC were attempted. No new host,
MiniGL DLL/library, or driver was installed during this round. Recover with
an operator cold power cycle, reconnect, and run InitPPC once per boot before
the next PPC launch. Do not treat this stalled conversion as a texture test.

## Remaining Gates

1. Recover target and verify clean process/device state and active artifacts.
2. Convert and run calibrated PPC smoke and mapping measurements. Add an
   explicitly selected aux mapping/verification lease if needed; current
   mapping benchmark cannot claim full three-way coverage.
3. Verify PPC-written pixels and streaming draw-update-draw ordering, extending
   the native visibility probe beyond serialized Execute as necessary.
4. Implement bounded driver storage/update contract using the measured fast
   mapping and proven visibility mechanism. No speculative DMA assumptions.
5. Complete PPC image/subimage semantics, remove unsafe shadow equality skip,
   and replace global drains with validated resource-specific dependencies.
6. Stress formats, partial/mip updates, memory pressure, retirement and reset;
   then validate Silent Hill GL and geometry workloads over three independent
   controlled runs. Application acceptance remains pending; later same-session
   smoke improvements are recorded below.

## Recovered Session: Measured Control-Path Fix

Corrected an additional benchmark issue exposed on-target: this WarpOS libc
does not reliably support `%llu`. All exact 64-bit values now print as two
explicit ULONG hex halves; bounded derived rates use `%lu`. Discard the first
malformed calibration/report, not merely its error field.

Calibrated PPC timebase approximately 25 MHz. Identical mapping benchmark
loops measured roughly 80 MB/s in BOTH registered geometry and P96-imported
texture memory. Complete mapping evidence:
`/tmp/opencode/txmap-20260905.log`. Aux forcing and host verification remain
unimplemented; this is a partial mapping gate, not GPU-visibility evidence.

The host spent only 272 ms handling 137 controls while CPU upload calls took
several seconds. Implemented a negotiated single-slot control mailbox at byte
4096 of the existing ring segment, leaving public ABI and ring layout intact.
See `MAILBOX_V1.md` for the ownership, timeout and late-reply contract.

Review also removed unsafe byte-equality texture shadows and corrected failed
registration cleanup. Removed invisible per-upload TXERR stdout calls and an
unused direct-path staging allocation. No Radeon driver changes were needed
for these measured improvements.

Same-session A/B results (8 frames, 32 RGB565 tile uploads, windowed):

| Variant | CPU frame-loop time | Source payload bytes/s | GL errors |
| --- | --- | --- | --- |
| Mailbox off, diagnostic print retained | about 11.2 s | 710070 | 0 |
| Mailbox on, diagnostic print retained | about 3.8 s | 1246088 | 0 |
| Mailbox on, print/dead allocation removed | about 0.53 s | 15744776 | 0 |

Host counters confirm exact routing: 137 Exec controls become 8 Exec controls
plus 129 mailbox controls. Mailbox handling took 61105 us in the intermediate
run. Logs `/tmp/opencode/mb-off-host.log` and `mb-on-host.log`.

Fullscreen stress of the cleaned build: 120 frames, 480 uploads, error=0,
source payload throughput 15192512 bytes/s. Raw log remains
`DH2:r200test/mb-stress.log`. This is not three independent cold-boot samples
and does not pixel-verify the PPC output.

Tested DLL: CRC32 `4EEA2DE8`, 234840 bytes. Copied from DH2:r200test into
Work:SilentHill WOS alongside the tested host, retaining `dll.premb` in the
game directory and `dll.prebox`/`host.prebox` in the test directory.
SilentHill.elf launched with existing configuration unchanged and output
redirected to `Work:minigl_testing/sh-mailbox.log`. Bridge connectivity was
lost afterwards; application acceptance remains UNVERIFIED.

## Subimage Implementation (Not Deployed)

Added capability-negotiated command 26 / bit 9, canonical PPC staging for
subrectangles, validated host span/bounds/unpack restoration and residency
mirroring. A private scalar native capability query (`0x4d47500c`, tagged
reply) prevents advertising support against older native libraries.

Native r200_texture.c materializes canonical CPU image backing lazily on the
first subimage to a committed direct BGRA surface, after last-use completion.
Ordinary direct full uploads gain no 68k copy or backing allocation. Both
allocations precede mutation and failure preserves the original texture.

Builds and sanitizer/stubbed-handler tests pass, including 100000 span cases,
allocation rollback, preserved texels, and mailbox timeout/late completion.
Independent code review found no significant defect. Physical cache visibility
and draw-update-draw correctness remain unvalidated. Subimages over 2 MiB,
indexed sources and extending direct mip chains remain limitations.

Candidate artifacts, NOT the files running on the Amiga:
`/tmp/opencode/minigl_ppc_host_subimage`,
`/tmp/opencode/minigl_ppc_r200_subimage.warpelf`, and the rebuilt native tree's
`bin/dispatch-r200-gcc/minigl.library`. Do not accidentally package this native
candidate as if it were the installed, validated full-image stack.

## Recovered Game Logs And Input Ownership

After the user's requested reboot, recovered complete stdout (54755 bytes) to
`/tmp/opencode/sh-stack-recovered.log` and host output (643 bytes) to
`/tmp/opencode/sh-stack-host-recovered.log`. Both transfers CRC-verified.

The game reports MiniGL ACTIVE and successful R200 fullscreen 16-bit context
creation. Its later generic `Renderer: WarpOS software (no GL)` banner is not
evidence of failing MiniGL: the specific WOS-GL path reports the hardware-backed
tile presenter active. No alternate renderer was selected by these changes.

The retained run reaches the main loop and sector-streaming operations. Typical
profile: frame 630.1 ms, logic 557.3 ms, raster 10.9 ms, present 50.6 ms.
Worst retained sample: frame 11592.0 ms, logic 11495.6 ms, present 19.8 ms.
Later steady samples remain about 1.2-1.5 fps, with logic around 620-790 ms
and present around 35-69 ms. Rendering optimization is not sufficient for
application acceptance. The profile's logic bucket may include waits and
OS calls; it does not prove the game is CPU-compute-bound.

Host's sole later diagnostic is `event queue overflow`. ELF disassembly
confirms Wos_MGL_PumpInput.part.0 (0x277c4) loads the window previously returned
by mglGetWindowHandle, loads UserPort at offset 0x56, and calls Exec GetMsg
(-372) and ReplyMsg (-378) through the WarpOS gateway. The host's
PollContextEvents drains that same port when forwarding was negotiated.
These are competing consumers, not two independent queues. The legacy client
cannot consume forwarded events through the public ABI-3 dispatch table.

Fixed frontend registration to request host-event forwarding only outside the
ABI-3 library build (`!R200_FRONTEND_LIBRARY`). This leaves legacy clients as
sole UserPort consumers while retaining standalone frontend forwarding.
This fix has not yet been deployed or runtime-validated.

The 68k host also continuously polls when idle. Resource contention affecting
game logic/audio/IO is a candidate, not a proven cause. Next discriminating
test should vary idle pacing alone, preserve mailbox latency and GPU ordering,
and compare game logic/present buckets. Black-screen cause remains unresolved.
