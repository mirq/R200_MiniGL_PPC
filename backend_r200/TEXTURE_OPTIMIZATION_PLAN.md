# Texture Engine Optimization Plan

Prepared 2026-09-07. Planning only: this document does not enable any new
upload, residency, protocol, or driver behavior.

## Objective

Reduce texture-update latency and P96 allocation/import/copy work while
preserving exact image contents, GL ordering, bounded memory use, and the
PPC/68k/GPU ownership contract. Optimize the actual streaming workload, not
static gears FPS. Gears remains a useful no-upload regression/control.

Priority order: establish allocation accounting, take isolated low-risk wins,
coalesce unsampled mutations, then optimize the remaining measured bottleneck.
Do not begin with a wholesale P96 replacement or reusable VRAM pool.

## Baseline And Invariants

- Preserve accepted PPC wait-ordering fix `abac846` and the quiet fixed-frame
  benchmark. The rejected native flip-wait prototype stays inactive.
- Checkpoint the currently uncommitted retirement fix before further engine
  work. Last verified native library: `2F05868E`; rollback: `DC041695` at
  `DH2:texretire/native-base`. Its CPU regressions, native phase5/stress, and
  PPC 7,127-check update test are the starting correctness baseline.
- Keep public PPC dispatch ABI 3 and native ABI 5 unchanged. New private
  behavior must be negotiated against both host and native backend support.
- Keep all GL semantics and texture conversion on the existing paths until
  their replacement passes pixel tests. No Warp3D/Warp3DPPC R200 route exists.
- Radeon9200.chip remains the owner of MMIO, CP submission, reset and private
  VRAM. PPC code writes only explicitly granted, bounded mappings.
- Ring-slot consumption is not GPU retirement. Do not poll obsolete public
  fence tokens or reuse an address merely because its previous handle changed.
- Preserve current image/subimage size, format, mip, palette and PPC texture-
  unit restrictions unless a separately tested phase explicitly extends them.
- Use two fullscreen buffers. Hold host priority, display mode/depth, sync,
  driver pair and compiler fixed during each A/B comparison. Use the verified
  priority-safe host when selecting -1; older hosts parse that setting unsafely.

## Phase 0: Measure The Texture Work

Add opt-in, in-memory accounting with one end-of-run report. Normal builds
must retain no new live logging, per-vertex timing or per-frame file writes.
Do not disable clocks required for mailbox deadlines.

Count actual operations, not inferred dirty-copy counters:

| Bucket | Required Measurements |
| --- | --- |
| API/workload | Full/subimage calls, formats/sizes, repeated binds, updates per texture between sampling/resolve boundaries |
| Allocation | P96 allocation/free calls, friend-bitmap cache misses, service imports/releases, failures/eviction retries |
| Copies | Source bytes, canonical shadow bytes, materialization readback bytes, full-realization bytes, aperture bytes, clear/padding bytes |
| Transport | Mailbox/Exec/chunk/resolve counts, control wait and barrier time, timeouts/fallbacks |
| Resources | Live/deferred surfaces and bytes, high-water marks, retirement progress, staging capacity |
| Outcome | Whole-run time, texture-stage time and tail latency; presentation reported separately |

Existing COPY/BYTES counters do not include every residency re-import/full
copy. A zero dirty-copy counter must not be described as zero upload work.
Distinguish source payload throughput from actual BGRA/aperture traffic.

Use a bounded workload matrix:

| Workload | Purpose |
| --- | --- |
| One upload, many clean draws | No new allocations/imports after setup |
| Repeated identical binds | Control-call overhead without texture mutation |
| Same-size full upload then draw, repeated | Full-frame/tile streaming path |
| 1, 4, 8 and 16 subimages, then one draw | Coalescing opportunity and update-density threshold |
| Overlapping and disjoint patches | Correct patch ordering and untouched pixels |
| Draw A, update, draw B | Old/new GPU versions must remain distinct |
| Alternating textures, deletion/redefinition, memory pressure | Resource bounds and retirement |

Start with existing `r200_texture_upload_smoke` and `r200_texture_update_test`
plus a focused native allocation-count fixture. Match the streaming client's
actual sizes/formats where available. Validate the real shipped enum values
across headers/transport, especially packed formats; do not bulk-sync headers.

**Gate:** explain the measured cost split and updates-per-sampling distribution
before selecting the main optimization. Keep setup/warmup outside timing and
drain pending work before the start and after the final measured frame.

Separate API acceptance latency from realization-inclusive cost. The existing
upload smoke's TexImage timer alone is insufficient for deferred work: time
update groups through their first sampling draw/resolve, and include all
update/draw/present work plus the final drain in end-to-end throughput. If a
serialized GPU-complete group-latency fixture uses glFinish per group, label
it separately and use the same synchronization in both variants; do not add
those drains to the ordinary pipeline benchmark. Tail statistics use successful
measured update-to-first-use groups after warmup, bucketed by workload shape.
Report failed/timeout groups and their elapsed time separately, not as fast or
silently discarded samples. Lower API-call time alone is not an acceptance gate.

### Correctness Prerequisites

Before enabling deferred replacement, close these specific integration gaps:

- Audit `AppendResolvedTclVertex` / indexed draw fast paths in both geometry
  copies. They can append to a nonempty same-primitive buffer without rechecking
  semantic serial or calling `PrepareScreenAppend`. Add a sampling-state check
  at the draw boundary, not per-vertex instrumentation or unconditional resolve.
- Define lifetime protection for exported PPC residency handles. Native LRU
  eviction must not invalidate a clean PPC mirror or an unpublished snapshot.
  Initially, pin exported current residencies and fail boundedly with OOM when
  no safe eviction exists. Explicit invalidation/release acknowledgements may
  replace that conservative policy later; GPU completion alone is insufficient
  because the PPC can sample its clean mirror again in a later frame.
- Restrict generic host `GET_INTEGER` forwarding to genuinely scalar, read-only
  selectors. It currently uses a one-GLint destination and cannot carry
  structure-writing, multi-value or mutating private queries. New descriptors
  require dedicated bounded handlers, not another arbitrary scalar forward.

Do not proceed to Phases 2-3 until tests cover those boundaries. These are
prerequisites for correct coalescing, not optional performance refinements.

## Phase 1: Isolated Low-Risk Reductions

### Redundant Binds

In `R200PPCDispatchBindTexture`, perform the same-binding shortcut before the
host round trip, but only after target/name/unit/begin-end validation and
transport-state checks. Require a known synchronized native binding mirror;
invalidate that knowledge on ambiguous failure, context changes and any
handler that cannot restore binding. Do not conceal a poisoned/abandoned
mailbox or skip required implicit texture creation.

**Gate:** repeated valid binds produce zero additional host commands, while
invalid calls, changed bindings, delete/rebind and failure recovery preserve
errors/state. This is a control optimization, not P96 allocation removal.

### Texture-Specific Clearing

Introduce an allocation policy for completely initialized texture storage,
not a global removal of `BMF_CLEAR`. Keep render-target, depth and friend-
bitmap initialization unchanged. Prove coverage of every GPU-readable byte,
including row/mip padding and required cache-line initialization. Compare
explicit padding initialization against the P96 clear instead of assuming it
is cheaper. Preserve the producer cache handoff before direct publication.

**Gate:** exact pixels/readback unchanged; no undefined sampled bytes; measured
allocation/clear time improves. Implement and measure separately from bind
changes. Move coalescing ahead of this phase if Phase 0 identifies large bursts.

## Phase 2: Native Deferred Replacement

Coalesce changes in owned CPU backing and create one fresh residency only
when current content must be sampled or explicitly resolved.

Logical state model, not a requirement for a general state-machine framework:

```text
current CPU generation g / current GPU residency g
  -> update CPU backing; pending generation g+1; retain old GPU residency g
  -> further unsampled updates; pending generation g+n; no GPU allocation
  -> allocate/import/fill temporary replacement; publish residency g+n
  -> retire old residency using the existing ownership/epoch rules
```

Implementation requirements:

- Preserve existing flush/submit dependencies before changing content that
  pending geometry may still need. Consume caller bytes before returning.
- Add an explicit replacement-pending distinction. A dirty resident texture
  currently passes `EnsureTextureResident`'s fast test and can reach in-place
  `SyncTextureDirty`; that must not happen for deferred replacement.
- Bump semantic state at logical mutation so snapshot-cache hits cannot
  bypass resolution. New captures may never name old residency as new content.
- Cover the indexed append shortcut explicitly; a serial bump without a
  corresponding draw-boundary check does not force recapture or resolution.
- Allocate/import/fill a temporary replacement while the old allocation is
  still held. Publish handle, generation and content serial only after success.
- Keep direct full uploads shadow-free. The first nonempty subimage may
  materialize one shadow; subsequent subimages must not repeat that readback.
- Keep legacy V1/V2 texture queries as current-residency queries: they force
  resolution. Native draw capture is another resolution boundary.
- Resolve only textures actually needed. Bind alone need not resolve;
  deletion, unused-texture teardown and full redefinition should not realize
  an obsolete pending version unnecessarily.

**Failure policy:** validation/staging failure leaves previous content intact.
Once a valid update has been accepted into owned backing, a later resolution
failure leaves pending content and old ownership available for retry, but
reports an error and never renders stale residency as current. The deferred
allocation error occurs at the resolution boundary; document this explicitly.
Do not promise full rollback of already-accepted CPU mutations without an
actual undo mechanism. Old and new allocations remain bounded under pressure.

**Structural gate:** for a pre-resident texture, eight patches with no
sampling/query between them cause zero replacement allocations during updates
and exactly one successful replacement at resolution, rather than eight.
Full realization traffic drops from roughly eight backing copies to one;
the necessary per-patch source-to-shadow copies remain.

## Phase 3: Coalescing Across PPC And 68k

Native-only deferral is insufficient for PPC clients. The current host's V2
preflight AND V1 post-update query force residency. The current command-26
reply also promises a valid current GPU handle, which the PPC immediately
marks resident. Do not reinterpret that response.

Add a small, opt-in private contract for three separate operations:

1. Metadata validation without allocation, copying or residency resolution.
2. Accept a deferred update into native-owned backing and acknowledge its
   logical generation. No current-GPU-handle promise in this acknowledgement.
3. Resolve pending content and return a validated current residency tuple.

PPC requirements:

- After deferred acceptance, record pending logical content and invalidate
  sampling snapshot eligibility. Resolve before any record captures that
  texture's handle; install the returned generation/epoch only on success.
- Publish/consume prior pending geometry before an allocation-capable resolve.
  Current capture can occur before flushing the preceding geometry, so placing
  allocation inside capture without restructuring that ordering is unsafe.
  Avoid recursive resolution while flushing the previous immutable snapshot.
- Keep every exported current residency protected against native LRU eviction
  until the PPC explicitly drops/invalidates its mirror or the owning texture/
  context is replaced or deleted through an ordered operation. Retire old GPU
  versions separately. Start with a bounded no-eviction/OOM policy for protected
  exports, and measure its memory limits before enabling it broadly.
- Keep staging immutable until acknowledgement. Native must consume it before
  replying; never retain caller pointers or a pointer into reusable staging.
- Preserve existing ring-consumption and host GPU barriers initially. This
  phase saves allocations/imports/full copies, not necessarily command counts
  or all waits. Do not combine it with synchronization removal.
- A lost/ambiguous acknowledgement retains the existing poison/pinning policy:
  no automatic retry through Exec, legacy update, or another generation.

Negotiate capability only when the host and native backend support the whole
contract. Use new private operations/descriptor versions and a free capability
bit after inventory; do not silently repurpose existing fields. If existing
layout/semantics must change, version them explicitly.
Structure-writing and mutating selectors must also be rejected by the generic
scalar GET_INTEGER route; test that rejection neither overwrites its scalar
destination nor performs a mutation.

**Compatibility gate:** old DLL/new host, new DLL/old host, and new host/old
native backend retain today's eager path or existing unsupported behavior.
Legacy V1/V2 queries still resolve. The new path is selected before mutation,
not after a failed or ambiguously completed command. Public dispatch stays put.

**Gate:** the native eight-patches/one-realization result must also hold through
the real PPC bridge, including its preflight. Exact pixels, old/new draws,
source reuse, query behavior, mirror generations and cleanup must pass.
Include resolving pending B under pressure while A has buffered/unpublished
records, then sampling A again from a clean mirror. B must not evict A behind
the PPC's back or cause old A geometry to use B's residency.

## Phase 4: Optimize The Remaining Measured Cost

Re-measure after coalescing. Do not implement every option below automatically.

### Control And Barrier Cost

If control latency dominates, evaluate bounded batching of owned update data
and resource-specific dependencies as separate changes. Retain per-call source
consumption and error semantics. A ready ring slot is not proof a sampled
texture is writable. Never use historical public fence polling for retirement.
The current grow-only PPC staging allocation already exists; adding another
staging layer is not itself an optimization.

### Source Conversion And Packed Storage

If aperture bytes/conversion dominate, separately investigate native RGB565
storage for opaque RGB565 streaming. The private BEGIN descriptor already has
a format request, but the current PPC path deliberately requests/requires
BGRA32 after a recorded RGB565 import failure. This is not a one-line toggle.
Resolve that format/import gate, endianness, filtering/alpha behavior, mip
layout and later subimage materialization first. Compare arithmetic with Mesa.
Only vectorize packing/conversion kernels after their measured cost warrants it.

### Reusable Driver-Managed Leases

Last, consider a bounded pool keyed by shape/format/epoch if allocation still
dominates. Reusing a previously sampled address requires BOTH GPU-reader
retirement and a physically validated texture-cache visibility mechanism.
Fences alone do not establish cache invalidation. The driver must grant reuse;
the PPC must not invent MMIO/CP flushes or inspect opaque native handles.

Do not replace P96 allocation wholesale with the current aux pool: it is only
4 MiB/eight surfaces and shares capacity with depth. Include budget, exhaustion,
fallback, reset and release rules. A P96-backed reusable pool may be preferable
to aux storage, but neither is enabled without the same cache/reuse proof.

## Correctness And Acceptance Gates

- Validate clean draws, all supported source formats/unpack layouts, mip
  boundaries, overlapping patches, zero/invalid updates and untouched texels.
- Exercise first/repeated direct-texture materialization and immediate reuse
  or freeing of application source buffers after the call returns.
- Test pending CPU geometry, built records and in-flight GPU draws across
  update/resolve/delete/redefine; include cached snapshots and native two-unit
  use without claiming new PPC multitexture support.
- Include pending B -> indexed draw with A -> bind B -> another indexed draw
  below buffer capacity and without changing primitive type. The second draw
  must resolve/capture B rather than append under A's cached snapshot.
- Exercise native eviction pressure against exported PPC mirrors, including
  later clean reuse of A after resolving B. Test protected-residency OOM and
  explicit lease/mirror invalidation, not only GPU fence completion.
- Inject shadow, bitmap, import and retirement-node allocation failures,
  submission failures, epoch changes and lost/late replies. No early free,
  stale-generation sampling, false current mirror, double release or leak.
- Verify bounded live/deferred/staging memory and return to baseline after
  drains and repeated context teardown/recreation.
- Extend `test_r200_retirement.py`, texture-order/lifecycle/readback tests and
  `r200_texture_update_test`; run native phase5/stress and phase6 acceptance.
- Cross-build affected trees with the same GCC versions/flags. Regenerate
  positional dispatch tables when required and retain converter checks.
- Use diagnostic counters only in separate attribution runs. Performance runs
  retain whole-run timing, final drain, no live logs, and fixed workload/mode.
- For acceptance, use three independent physical cold-power-cycle runs per
  candidate/control, reporting every sample and the median. Software reboot
  trials remain separately labelled. Follow serialized bridge operations,
  short target filenames, CRC checks, InitPPC-once and rollback/Avail Flush rules.

Accept a performance phase only after correctness passes and a reproducible
texture-stage or tail-latency improvement is shown without a material clean-
draw/memory regression. Structural call reductions are required evidence, not
a substitute for hardware throughput. Do not claim a target FPS before measuring.

## Execution Scope

Primary files:

- `backend_r200/library/minigl_r200_warpos_compile_probe.c`: transport, mirror,
  source staging, direct upload and deferred acceptance/resolution routing.
- `backend_r200/library/r200_texture.c` and native `library/r200_texture.c`:
  residency, dirty content, replacement publication and retirement.
- Both `library/r200_state.c` / `r200_geometry.c`: semantic serials and sampling
  capture boundaries; do not bulk-copy the different frontend trees.
- `backend_r200/minigl_ppc_host.c`: validation, preflight, native operations and
  response semantics.
- `backend_r200/include/libraries/minigl_{ppc_protocol,r200_bridge}.h` and
  matching native headers: negotiated private contracts and layout assertions.
- `/home/mirek/p96-driver`: only for a measured format/reuse/service bottleneck;
  preserve the matched card/chip ownership and deployment protocol.

Recommended first implementation slice: checkpoint retirement, add trustworthy
allocation/copy accounting and workload probes, then remove redundant binds.
The principal engine change is Phases 2-3: one fresh realization per group of
unsampled subimages, with native and PPC contracts implemented in order.
