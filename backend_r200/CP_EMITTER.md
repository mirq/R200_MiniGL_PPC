# PPC CP Emitter

Opt-in: `SetEnv MGLPPC_CPEMIT 1` before starting the client. Ordinary rendering
is unchanged when unset. This is separate from `MGLPPC_IBSMOKE`; do not enable
both. Currently limited to windowed contexts with stable render targets.

## Ownership

The PPC DLL links the same `src/radeon3d_emit.c` as the chip, using
`R200_EMITTER_ROOT` (default sibling `../p96-driver`). The adapter supports
inline semantic records, streamed commits and homogeneous state batches.
It resets emitter state per submission, not per draw, and does not change the
driver's register programming. Vertices retain the existing streaming path.

Protocol v15 remains unchanged; CP_EMIT is explicitly requested and echoed at
registration. It requires live INDIRECT_DISPATCH and INDIRECT_RENDER caps;
an opted-in client rejects missing support instead of silently benchmarking a
semantic fallback. The host leases one 256 KiB CP segment and four vertex
segments from the context's existing device. Vertex reply tuples include the
driver's GPU address. No second device or direct PPC MMIO/CP-ring access exists.

Each of eight FIFO positions owns a separate 32 KiB CP slice. The PPC stages
host-native words, pads odd lengths with PACKET2 (classic Linux Radeon indirect
dispatch requires an even dword count), then swaps once and flushes before
publishing `[byteOffset, dwordCount, vertexSegmentMask]`. A slice is reused only
after **retired**, not **completed**. The host retires its previous fence before
dispatching another, retains vertex-use masks, and performs ordinary presents.
`CP_RUN dispatches=... dwords=... failed=...` is printed once at shutdown and
distinguishes real CP submissions from the old startup smoke.

Native private query `0x4d475013` returns only context-owned, live surface
descriptors after service-generation checks, without recovery or residency
changes. The PPC caches 16 descriptors, invalidated on resource/lifecycle
commands; binding alone does not replace residency. There is no per-frame
metadata query for gears' unchanged textures. Generic scalar GET_INTEGER
rejects the private descriptor namespace to prevent descriptor writes into a
four-byte destination.

CP failures retain storage and reject further work. Host teardown rejects
unconsumed entries, failed retirement and terminal CP errors. `glFinish` waits
for actual CP retirement, including a final unpresented clear/inline draw.
These are trusted packets: extent validation does not sandbox their register
or VRAM references.

## Validation Status

Implemented and cross-built on 2026-09-10. Hardware triangle readback and all
7,127 texture-update checks passed on the CP path, with positive CP submission
counts and normal teardown. The subsequent loader/pacing investigation is in
`HOST_LOADER_REGRESSION.md`; it supersedes the original performance assumptions.
Local checks: all 11 Python test scripts and six regression modes pass. The
real emitter adapter has 15 ASan/UBSan tests (including 4,000 mutations), the
surface bridge has nine, and the driver dispatch harness has 79 cases. The
host, adapter, shared PPC emitter and chip compile without new warnings;
pre-existing native/frontend and rebuilt-card warnings remain.

Quiet FPS uses `GEARS_RUN` with `MGLPPC_PROFILE=0`, client/host traces compiled
out, and verbose/upload/IB-smoke diagnostics unset. Existing optional fine-grain
transport/host profile subdivisions do not yet account for CP-specific
emit/wait/submit work; do not use those buckets to attribute this path. Wall
time and shutdown CP counts remain the benchmark evidence.

## Build And Baseline

The corresponding driver implementation is committed in `p96-driver` at
`6fd98ef` (interface 18). `R200_EMITTER_ROOT` must resolve to that compatible
driver tree; the shared emitter is compiled from its source, not an old object.

Initial artifacts below are from the 2026-09-10 bring-up. For the final
classified-host and bounded-polling artifacts/results, use
`HOST_LOADER_REGRESSION.md` (current quiet DLL CRC 02605851).

Initial isolated artifacts: `/tmp/opencode/ibdraw`; target staging: `DH2:ibdraw`.

| Artifact | Target CRC32 | Bytes |
| --- | --- | ---: |
| Chip | `CEB838A6` | 77940 |
| Card, preserved known-good baseline | `18A453D6` | 7796 |
| Native MiniGL | `62343673` | 127252 |
| Host | `F98CB3DF` | 40564 |
| PPC WarpELF | `953151AF` | 361421 |
| Target-converted DLL | `77239FB3` | 295680 |
| Triangle with pixel checks | `C6F60689` | 89456 |
| Texture-update acceptance client | `F829BE0E` | 97248 |

The freshly rebuilt card (`441CBE1C`) is archived as `DH2:ibdraw/cardbuild`,
not selected for deployment: preserve the known-good card binary rather than
introducing a second artifact change. Both components are still replaced as a
matched pair, with both active files backed up before installation.

The unchanged semantic stack produced **52.264 FPS** in one operator-rebooted
600-frame run, 800x600x32 windowed, envmap, sync off, ordinary (not precalculated)
geometry, error zero. Host priority -1; 2,031 executes and 600 presents. It is a
single-sample reference, not a three-cold-run median. Logs are preserved under
`/tmp/opencode/ibdraw/bench/{sample1,host1,health1}.log`; original matched
artifacts remain in `DH2:ibsafe` and the baseline client in `DH2:ibbench`.
