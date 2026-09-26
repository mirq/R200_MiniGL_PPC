# Testing

There is no CI and no host-side end-to-end test of the rendered result. The
test strategy is two-layered:

1. **Host-independent tests** on Linux: Python harnesses that compile and run
   the *real* production functions against deterministic stubs, plus C probes
   compiled with ASan/UBSan. These catch protocol, layout, ordering and
   lifetime defects without hardware.
2. **Hardware tests** on the physical A4000: the `minigl_testsuite` client and
   the generated regression script. These are the only tests that prove pixels,
   driver compatibility and GPU ordering.

A mock passing does not mean the feature works on hardware. Compilation never
implies validation.

## 1. Host-independent test suite

Run from the repository root with a C99 compiler supporting ASan/UBSan and
Python 3. Binaries go to `/tmp/opencode`; nothing here rebuilds or overwrites
production artifacts.

```sh
mkdir -p /tmp/opencode
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
   tests/r200_texture_layout_test.c -o /tmp/opencode/r200-texture-layout-test
/tmp/opencode/r200-texture-layout-test

cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
   tests/r200_readback_layout.c -o /tmp/opencode/r200-readback-layout
/tmp/opencode/r200-readback-layout

python3 tests/test_r200_texture_order.py
python3 tests/test_r200_readback.py
python3 tests/test_r200_context_lifecycle.py
python3 tests/test_r200_context_lifecycle.py --regress-crc
python3 tests/test_r200_context_lifecycle.py --regress-cache
python3 tests/test_r200_context_lifecycle.py --regress-register
python3 tests/test_r200_context_lifecycle.py --regress-waits
python3 tests/test_r200_host_pacing.py
python3 tests/test_r200_retirement.py -v
python3 tests/test_r200_retirement.py --regress-fences -v
```

What the main runners cover:

- `r200_texture_layout_test.c` — the former protocol-header test: source-format,
  rectangle and span assertions plus 100,000 boundary-biased cases against an
  independent 64-bit oracle. It selects the SDK-free declarations with
  `MGLPPC_TEXTURE_VALIDATION_TEST`; the production inline validators remain in
  the header.
- `r200_readback_layout.c` — 32-bit layout arithmetic and guarded pack copies:
  overlapping rows, alignments 1/2/4/8, row length, both skips, cap and
  overflow boundaries.
- `test_r200_readback.py` — compiles the real frontend readback/barrier/
  allocation/error helpers and the real host command-27 handler against stubs
  under ASan/UBSan; checks cache/barrier order, bounded RAM and payload
  validation, native errors, replayed pack state, restoration paths,
  first-error preservation, allocation reuse and simulated timeout.
- `test_r200_context_lifecycle.py` — two sequential contexts in one DLL open,
  once ring/mailbox and once Exec fallback, using production create/delete/
  close/registration/publish/barrier code and the inline RingPoll branch.
  Verifies fresh counters, epochs 1→2, stale request rejection, first/repeat
  submissions, teardown/shutdown, and cache-order models:
  - `--regress-crc` reproduces the second-context CRC failure (frontend CRC
    zero, ring error=1) by removing the CreateContext CRC rearm.
  - `--regress-cache` removes the host release clean and shows stale writeback
    corrupts the next payload.
  - `--regress-register` removes the registration control-block clean and
    shows an unpublished registration is rejected.
  - `--regress-waits` disables readiness rechecks and reproduces the extra
    `Delay(1)` tick sleep.
- `test_r200_texture_order.py` — checks production-source publication/control
  ordering and the example's pre-present pixel oracles without compiling it.
- `test_r200_host_pacing.py` — pacing histogram bins, clock wrap, top-gap
  ordering, and the disabled/no-timer paths.
- `test_r200_retirement.py` — fence/retirement semantics;
  `--regress-fences` exercises the fault-path behaviour.

More Python suites (each compiles or parses production code):

| Test | Covers |
|---|---|
| `tests/test_r200_transport_profile.py` | ring/mailbox/serial protocol v15 accounting |
| `tests/test_r200_host_profile.py` | host profile counters |
| `tests/test_r200_host_priority.py` | priority handling |
| `tests/test_r200_surface_bridge.py` | surface bridge/leases |
| `tests/test_r200_bind_cache.py` | bind/cache behaviour |
| `tests/test_r200_cp_emit.py` | CP emitter accounting |
| `tests/test_r200_cp_surface_cache.py` | CP surface-descriptor cache |
| `tests/test_r200_fence_group.py` | fence coalescing |
| `tests/test_r200_texture_order.py` | texture ordering |
| `tests/test_frame260_deep_profile.py` | frame-260 deep profiler paths |
| `tests/test_enum_parity.py` | GL enum parity between the frontend header and `include/mgl/gl.h` |
| `tests/test_68k_host_classifier.py` | the Sonnet classifier ban list |
| `tools/test_batch_budget.py` | `r200_batch_budget.h` fits/cost tables |
| `tools/test_batch_expansion.py` | expansion budgets vs the real driver emitter |
| `tools/test_state_batch_expansion.py` | extracted `R200ExpandStateBatch` under ASan (record count, header copy, offsets, purity) |
| `tools/test_screen_clip.py` | `ScreenPlaneDistance` extraction + clipper tests |

Known pre-existing failures (as of the 2026-09-17 handover): the lifecycle
suite's compile errors (`HostRingMailboxActive/RingAcc/mismatchSubmits/
AltiVec`) and the enum/bind/host-profile/transport tests failing identically at
HEAD. Do not treat these as regressions caused by your change; compare against
HEAD.

## 2. Readback contract (command 27)

The readback tests encode the contract; keep it when touching readback:

- Command 27 uses capability bit 10 on protocol 15 without changing ABI 3 or
  any wire layout.
- Only `GL_RGBA` + `GL_UNSIGNED_BYTE`; other pairs → `GL_INVALID_ENUM`.
- Nonempty requests > 2 MiB → `GL_INVALID_OPERATION` (no partial output);
  invalid dimensions/NULL/overflow/out-of-bounds → `GL_INVALID_VALUE`; missing
  capability or poisoned transport → `GL_INVALID_OPERATION`; zero-area calls
  transfer nothing.
- The native context's current read surface is used, before present;
  out-of-bounds rejection is native (not clipped).
- The host temporarily forces native pack to alignment 1 / row length and skips
  0, then restores it; the frontend applies application pack state.
- Cache protocol: PPC cleans an aligned staging subrange before publication;
  host validates the interval in one Exec RAM region, fences, fills with native
  `ReadPixels`, cleans before ack; PPC invalidates clean staging after a
  successful ack. Failed/partial native writes never reach caller memory.
- Mailbox timeout follows the abandoned-request policy: no retry, no copy, no
  staging replacement/free until late completion allows teardown.

`tests/r200_readback_abi.c` must be compiled separately with both real target
SDKs; it checks the command/capability, mailbox eligibility, eight-word
arguments, the 80-byte zero-payload message, the ring control size and the
160-byte mailbox. The current PPC DLL asserts ABI-3 size **0x238**.

## 3. Hardware test suite

`examples/minigl_testsuite.c` builds to `bin/examples/minigl_testsuite_ppc.warpelf`.
It contains system/dispatch checks, primitive and depth/raster tests, matrices,
arrays, cubes, benchmarks, lighting, texture-format cases and 13 Quake-derived
cases (alias ogre, world stream, lightmap, particles, room walk, fog, pickup
flash, console, present stability). `tools/gen_amiga_regress.py` generates the
AmigaDOS regression script (34 cases, per-test fresh host, logs under
`Work:ibdraw/regress/`, orphan cleanup via `hoststop --orphan`); the generated
script is checked in as `tools/amiga-regress.script`.

Known broken validator (2026-09-26): `quake.world-room-walk` fails pixel
equality with `detail="room f128 want 59,44,25 got 34,29,15"` while the hall
actually renders correctly. The mismatch is byte-identical on the clean
`13e3dba` reference DLL and on the fixes build, and the operator confirmed
the on-screen rendering; the baked offline reference pixels, not the driver
path, are wrong. Treat that one case as a false failure until its expected
values are regenerated.

The Quake console assets are extracted from retail data with
`python3 tools/extract_quake_console.py [PAK0.PAK] [out.h]` → `examples/quake_console.h`.

Runtime acceptance notes:

- `examples/r200_texture_update_test.c` verifies first and repeated RGBA
  patches in two contexts (old draw A vs new draw B, inside/outside each patch,
  before present) with four 1x16 reads per frame across texture rows 7–8.
  Expected pixels come from the original image plus uploaded pattern, including
  alpha. Other source formats only get error/order smoke checks.
- Direct-upload and mailbox routing still require host counters; the public API
  cannot prove them.
- Hardware validation requires the matched frontend/host/native stack. Readback
  is bounded in bytes and test calls, not in native driver execution time.

## 4. Frame-260 deep subimage profiler

Instrumentation only (no optimization or wire changes). The profiled PPC
wrapper and host keep the first **128** events in static RAM (27,648 and 33,280
bytes), then drop and count. No per-frame allocations or output calls.

Build (isolated paths; must not overwrite production binaries):

```sh
make -f Makefile.dll.r200.gcc warpelf bin/frame260host \
  OBJDIR=obj/frame260-deep WARPELF=bin/frame260deep.warpelf \
  R200_HOST=bin/frame260host MGLPPC_PROFILE=1 \
  R200_EMITTER_ROOT=/path/to/p96-driver \
  R200_NATIVE_IMPORT=classic_68k/dev/lib/libminigl_dispatch.a
```

Outputs: PPC `DH2:wosbuild/q260deep/ppc_lm.txt` (opened once after platform
cleanup; create the directory first) and `HOST_LM*` lines on host stdout at
normal shutdown. Match by order + context/name + level/x/y/w/h — PPC and host
clocks are not synchronized, and a pre-publication failure can create a
sequence gap. `python3 tests/test_frame260_deep_profile.py` runs extracted
production paths for the phase bookkeeping.

## 5. PPC→68k direct-call probes

Two hardware probes answer "can a native PPC task call the 68k
`minigl.library` directly, and what does it cost?" without the 68k host or
`minigl_ppc.dll`. They are built by `Makefile.import.gcc` (in the normal
`testprograms` set) and need only `C:InitPPC` once per boot.

| Probe | Build target | Purpose |
|---|---|---|
| `examples/r200_run68k_bridge_probe.c` | `bin/examples/r200_run68k_bridge_probe_ppc.warpelf` | `Run68K` sanity vs LP stubs, `minigl.library` LVO −30, dispatch-table header, `PP_Stack` entries, timing matrix |
| `examples/r200_run68k_context_probe.c` | `bin/examples/r200_run68k_context_probe_ppc.warpelf` | create/query/clear/draw/present/delete a real 68k context via direct calls |

Options: `r68ctx [-nopresent] [-fs] [-hold N] [-n N]`; `r68bridge
[-n N] [-lowlevel]` plus the dangerous, opt-in `-waitfor` (known to wedge
the task — cold reboot), `-async` and `-raw` stages, which run only after
all results are written.

Evidence files: `Work:wosbuild/run68k_trace.log` + `run68k_result.txt`,
`Work:wosbuild/run68k_ctx_trace.log` + `run68k_ctx_result.txt` (one
open/append/close per line, so the last line names a wedge point).

Results, constraints and measured costs: see
[run68k-direct-calls.md](run68k-direct-calls.md). Do not treat these probes
as production paths; they exist to keep the evaluation reproducible and to
catch regressions in `Run68K`, `PP_Stack` and the direct context lifecycle.

## 6. Rules

- Compilation or mocked tests are not hardware validation; say so in reports.
- Compare against HEAD before calling a suite failure new.
- Never leave test binaries in `bin/` or `lib/` unless they are canonical
  build outputs; use isolated `OBJDIR`/`BINDIR`/`OUTLIB` paths.
- The lifecycle/drift assertions expect the protocol header copies in
  `classic_68k/dev/...` to stay byte-identical; if you edit one, sync all.
- After touching a protocol/format path, run the matching host-independent
  test and then the hardware suite on the machine.
