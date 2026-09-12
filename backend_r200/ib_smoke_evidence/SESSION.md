# PPC Indirect Smoke, 2026-09-10

## Verified Result

A PPC/WarpOS client authored an eight-dword CP-native stream in the leased
ring segment. The 68k host verified the descriptor and bytes, dispatched it
through the native MiniGL bridge, waited for Radeon3D fence **101**, and
published completion. The PPC observed `ack=1 mode=gpu`; the client and host
closed normally. This is the fixed startup-smoke path, not a general rendering
command emitter or a performance result.

Order in this session:

1. Native `bootstrap -depth 16`: pass, 20 frames.
2. New PPC `ibclient`, smoke disabled: context created and deleted, no draws.
3. New PPC `ibclient`, dry smoke: exact payload `[8192,8]`, FIFO slot 0,
   generation 1, expected CP-native words verified, **zero IB GPU submissions**.
   Host synthetic acknowledgment and PPC acknowledgment both recorded.
4. New PPC `ibclient`, real smoke: same bytes, fence 101 submitted and retired;
   both CPUs recorded acknowledgment before context deletion.
5. Native `bootstrap -depth 16`: pass, 20 frames after the real smoke.
6. `Status` and the public port list: no lingering host, DLL, or client.

No reset, breakpoint, MMIO poke, driver replacement, or repeated `InitPPC`
was used in this session. The last confirmed `InitPPC` from the conversation
already applied to this boot; `MasterControl` was present before testing.
These are same-boot functional checks, not independent cold-boot benchmark runs.

## Identity

Physical target: `192.168.1.21:2345`, accessed only through `amiga-mcp` TCP.
68060/PAL; PPC MPC7410 and Prometheus/RV280 per the repository hardware baseline
(compiler target remains 604e/SysV/big-endian). VRAM/ROM and monitor ToolTypes
were not independently re-inventoried in this session and were not changed.

MiniGL commit: `6673684d9e0b6c2e1302dc7b0735e9854c9be200`, dirty before and after.
Driver commit: `98192a217cf4fb7366c6937cb4817247f7cd9706`, dirty from earlier
work; no driver-tree edits or driver rebuilds in this continuation.

| Artifact | CRC32 | Bytes | Location |
| --- | --- | ---: | --- |
| Unchanged chip | `5341F8BF` | 77640 | `LIBS:Picasso96/Radeon9200.chip` |
| Unchanged card | `18A453D6` | 7796 | `LIBS:Picasso96/Prometheus.card` |
| Native MiniGL | `4C4D4A7F` | 126504 | `LIBS:minigl.library` |
| Previous native MiniGL | `D85B04B5` | 126448 | `DH2:ibsafe/libprev` |
| Host | `8703E6E4` | 38904 | `DH2:ibsafe/mglhost` |
| PPC DLL WarpELF | `D961306A` | 326694 | `DH2:ibsafe/mgl.elf` |
| Target-converted PPC DLL | `91E639EE` | 262004 | `DH2:ibsafe/minigl_ppc.dll` |
| Test client WarpELF | `4C318AEC` | 50470 | `DH2:ibsafe/ibclient.elf` |
| Target-converted test client | `C5A16679` | 38732 | `DH2:ibsafe/ibclient` |
| Host launch script | `A8A171A2` | 62 | `DH2:ibsafe/hostrun` |

Every transfer was followed by a destination CRC check. DLL/client conversion
used `DH2:wosbuild/Elf2Exe2` after verifying both input ELFs; output CRCs above
identify the converted artifacts. `Avail Flush` followed the native-library
replacement. The existing driver-pair backups were not overwritten.

Isolated host builds are under `/tmp/opencode/ibsafe`. Both native and PPC
builds completed, with no undefined-symbol/COMMON/relocation validation failure.
The new host and client compiled without warnings. Existing backend warnings
(alignment checks, signedness, const qualifiers, indentation and dead probe
code) remain; this is not a warning-free-stack claim.

## Corrections

- A normal `MiniGLClose` sends `SHUTDOWN`; the host must be started anew for
  each client run. Missing-host failures on later launches are not evidence of
  a defective second DLL load or scheduler starvation.
- `HELLO` now advertises negotiation only. Ring registration probes the live
  context and echoes indirect support and dry/GPU mode before PPC publication.
- Segment ID zero is valid. The private probe uses an explicit flag rather
  than reserving ID zero; dispatch/free both accept a context-owned zero lease.
- The publisher snapshots an empty FIFO, writes and flushes the two payload
  dwords, publishes metadata, then advances the head. It does not infer buffer
  ownership from an old entry's slot field.
- Dry and real consumers use common completion publication. The previous
  `continue` branches skipped `control->completed`, stranding a waiting client.
- The PPC waits for its own acknowledgment before permitting ordinary GL work.
  Errors/deadlines are terminal for this diagnostic: no redispatch or shared
  storage reuse. This deliberately retains allocations on failure until reset.
- One-shot traces use per-line open/write/close on `DH2:ibppc.log` and
  `DH2:ibhost.log`, independent of verbose console output and frame tracing.

The earlier cache-partition, endian-control, pure-no-op, starvation and
boot-priming explanations were not isolated by this run. Do not infer a new
hardware size limit or performance improvement from the smoke results.
Extent validation of a trusted IB does **not** sandbox the addresses or
registers contained in its packets.

## Reproduction

Use the matched artifacts in `DH2:ibsafe`, not older copies under `Work:`.
At a genuinely fresh boot run `C:InitPPC` once, before any PPC application or
converter, and record that fact. Never use another `InitPPC` to recover a
wedged task. A gray-screen/hard-hang requires an operator cold power cycle.

`DH2:ibsafe/hostrun` contains:

```text
FailAt 21
Stack 65536
DH2:ibsafe/mglhost >DH2:ibsafe/host.log
```

With no host/DLL/client left running, use a multiline AmigaDOS script:

```text
FailAt 21
SetEnv MGLPPC_HOSTPRI -1
SetEnv MGLPPC_IBSMOKE 1
SetEnv MGLPPC_IBDRY 1
UnSetEnv MGLPPC_VERBOSE
UnSetEnv MGLPPC_UPLOAD_TRACE
Run >NIL: Execute DH2:ibsafe/hostrun
```

Verify `MiniGLPPC.R200.Host.1` exists, then run:

```text
FailAt 21
Stack 1000000
CD DH2:ibsafe
ibclient >dry.log
```

Only after both dry acknowledgments and clean teardown, start a **fresh host**
with `MGLPPC_IBDRY=0` and run the same client to `real.log`. The independent
host script is necessary to capture the host's output, rather than only the
`Run` command's `[CLI n]` banner. Multiline scripts preserve the working
directory; do not rely on shell syntax borrowed from Unix.

After this session, `MGLPPC_IBSMOKE`, `MGLPPC_IBDRY`, `MGLPPC_HOSTPRI`,
`MGLPPC_VERBOSE`, and `MGLPPC_UPLOAD_TRACE` were unset. PPC remains initialized
for the current boot. No follow-up GPU workload is automatically launched.

## Software Checks

`tests/test_r200_context_lifecycle.py` extracts the production publisher,
consumer, native private-query case and lifecycle code into an ASan/UBSan
harness. It covers dry/real completion, zero-valued segment IDs, stale entry
selectors, capability refusal, missing host, invalid bytes, wrong epoch,
failed retirement, bounded waits and retained storage. These are mocked
CPU/protocol tests, not a GPU simulator.

The baseline and `--regress-crc`, `--regress-cache`, `--regress-register`,
`--regress-waits`, and `--regress-ib-ack` checks passed. The last deliberately
removes shared completion publication and reproduces the smoke timeout.

## Next Boundary

The current transport accepts only one fixed eight-dword startup smoke.
Moving real draw/state emission to the PPC still requires shared emitter
code, validated surface/texture metadata, IB-buffer ownership and retirement,
generation invalidation, and draw-by-draw/visual comparison with the native
path. The present result does not establish those properties.
