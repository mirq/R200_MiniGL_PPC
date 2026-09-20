# Frame-260 internal subimage diagnostic (f2976f6)

This is instrumentation, not an optimization. No renderer, wire ABI, native
library, driver, ring defaults, or texture ordering changes. The profiled
PPC wrapper and host store the first **128** events in static RAM (27,648 and
33,280 bytes respectively), then drop new records and count drops. There are
no new per-frame allocations or output calls. Captured phases include small
clock/bookkeeping costs. All timestamps/deltas are unsigned low-32-bit values.

## Build only, in the isolated worktree

```sh
make -f Makefile.dll.r200.gcc warpelf bin/frame260host \
  OBJDIR=obj/frame260-deep WARPELF=bin/frame260deep.warpelf \
  R200_HOST=bin/frame260host MGLPPC_PROFILE=1 \
  R200_EMITTER_ROOT=/tmp/opencode/q260-driver \
  R200_NATIVE_IMPORT=classic_68k/dev/lib/libminigl_dispatch.a
```

The source-tree native import archive is absent in this worktree. The command
uses its existing packaged **68k import stubs** (CRC32 `EFD4B810`), not the PPC
archive or any code from the unsafe current tree. This direct host target does
**not** build `minigl.library`. Default MOS2WOS/common compiler settings remain
in use. `frame260deep.warpelf` still needs target-side Elf2Exe2 conversion by the
session owner; it is not already a loadable DLL. No deployment is done here.

## Output and matching

- PPC: `DH2:wosbuild/q260deep/ppc_lm.txt`, opened once with `w` **after platform
  cleanup**, following context destruction, transport close and buffer release.
  The session owner must create the directory before the run. Open failure is
  nonfatal and not retried. Normal DLL close is required, not just a frame stop.
- Host: `HOST_LM*` lines on stdout once after context/device/library cleanup on
  normal host shutdown. Redirect stdout to a fresh run-specific file. A hang
  deliberately produces no partial per-event file. Existing aggregate outputs
  retain their paths; CP/batch dump calls now follow completed context teardown.

`PPC_LM_ID seq` numbers every API call; `shared_order` numbers only attempted
`TEXTURE_SUBIMAGE_SHARED` PlatformCalls. `HOST_LM_ID seq` numbers received shared
requests, including rejected envelopes. Match **order + context/name +
level/x/y/w/h**, not blindly the two `seq` fields. A pre-publication failure can
create a shared-order gap. `request_serial` is the existing transport sequence,
not an added wire field. PPC and host absolute clocks are **not synchronized**.

The historic FP frame 260 / demo 259 / server time 22.585360 is identified using
the external client log and these known normal-lighting tuples (no engine
frame/demo/server fields are available inside MiniGL):

| name | atlas | x | y | w | h | source bytes | BGRA rectangle bytes |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1027 | 3 | 0 | 53 | 128 | 22 | 2816 | 11264 |
| 1029 | 5 | 0 | 118 | 128 | 10 | 1280 | 5120 |
| 1030 | 6 | 0 | 0 | 128 | 16 | 2048 | 8192 |
| 1032 | 8 | 0 | 71 | 128 | 53 | 6784 | 27136 |

Source format/type are the original API arguments; the shared wire format is
canonical BGRA, not LUMINANCE. `bgra_rect_bytes` is the intended rectangle size,
not a claim that bytes were transmitted (especially on errors/direct paths).
The expected run remains 969 demo frames / 978 presents; this code cannot verify
those external counts and never changes `r_dynamic`.

## Timing interpretation

For each `*_PHASE` line, ticks = `(end - start) & 0xffffffff`. Unvisited phases
are omitted; zero ticks is valid. Phases partition each event's `*_TOTAL`.
Individual intervals must be shorter than one rollover (~172 s at 25 MHz PPC,
~6055 s at 709379 Hz EClock). Phase/total durations are inclusive of profiling
overhead; no overhead is silently subtracted.

PPC phases:

- `validate`: API/source/rectangle validation, before pending upload flush.
- `pending_flush`: existing pending upload flush and path eligibility check.
- `old_work_barrier`: existing `FrontendTextureBarrier`, including mailbox
  readiness, `FlushScreenVertices`, **unchanged `SubmitAndWait`**, and
  `R200PPCPlatformBarrier(FALSE)`. Not a pure GPU-only fence timer.
- `bgra_stage`: existing upload-buffer ensure/allocation and BGRA conversion.
- `cache_flush`: data-range flush of the canonical staging buffer.
- `request_invalidate`: packet construction and original pre-call mirror/cache
  invalidation; no invalidation was reordered.
- `platform_call`: whole synchronous PlatformCall, including PPC publication,
  scheduling/transport wait, **all host work**, and reply retrieval.
- `mirror`: reply validation and frontend mirror update.
- Direct paths separately show `direct_patch`, `direct_barrier`,
  `direct_unclassified` (whole direct helper, **not** a claimed mailbox-only
  split), `direct_reply`, and on fallback `direct_abort` before shared phases.
  Path 0 = unselected; 1 = shared; 2 = direct/unclassified; 3 = direct→shared.
  Result 0 = early exit, 1 = successful update, 2 = empty no-op. Sticky errors
  are peeked before/after without consuming them; result is independent of a
  previously latched error. `mailbox` describes the shared-call route only.

Normalize raw PPC ticks against the client's measured timebase (~25 MHz).
`clock_hz_legacy` is printed but **untrusted**, also for legacy CP_PHASE data.

Host phases:

- `envelope`: mailbox decode/validation (starts just after a new reqSerial is
  observed); Exec entry starts before HandleMessage. Not PPC queue latency.
- `handler_validation`, `source_validation`, then `fence_drain` around the
  **existing** DrainSemanticFence before preflight. `fences_at_drain` records
  the pending count. A zero-fence call still has bookkeeping costs.
- `preflight_prep`, `preflight` (V2 query), `bind_cache_unpack` (validation,
  bind, CacheClearE, canonical unpack setup).
- `perf_query_before` / `perf_query_after`: five supported scalar native
  GetIntegerv reads each, **outside** `native_texsub` and separately timed.
- `native_texsub`: native glTexSubImage2D only, not error retrieval/restoration.
- `restore`, `bridge_query` (V1 query and failure handling), `result`, `reply`.
  Mailbox TOTAL ends after ack cache publication; Exec TOTAL ends before
  ReplyMsg (no new waits). HANDLER is nested inside TOTAL and includes drain.

`HOST_LM_NATIVE` preserves before/after clock rate, residency/upload ticks,
upload bytes and evictions; `HOST_LM_DELTA` uses unsigned subtraction for the
four cumulative counters. `native_samples=3` means both snapshots were taken,
even if the native call failed. No extra glGetError calls are introduced:
these selectors are supported scalar reads on the frozen native stack; query
errors are not independently checked. Per-record `HOST_LM_HINT error` records
the existing native perf hint result. Clock rate should be 709379.

**Do not sum native counters, HANDLER and TOTAL, or host time and PPC
platform_call.** Those are nested/inclusive measurements; residency/upload
counters can also overlap each other. Only consecutive phase intervals within
one event are exclusive. Legacy aggregate counters keep their old scopes and
success-path limitations; use these records for error-path attribution.

## Local checks

`python3 tests/test_frame260_deep_profile.py` runs extracted production paths
and record/report helpers with ASan/UBSan and strict C warnings. It checks
shared/direct/fallback and failures, empty calls, sticky errors, query deltas,
32-bit wrap, drop bounds, short formatters and deferred output. Preprocessed
profile-off PPC/host subimage operation order is compared against HEAD.
Host classifier and texture-order tests also pass. These tests do not validate
cache coherency, GPU completion, native counter semantics on hardware, or the
actual historic event. The older host/transport aggregate-profile tests and
bind-cache extraction test are already stale at f2976f6; their identical
failures were reproduced with pristine HEAD source fixtures. Do not interpret
these extraction failures as runtime results.
