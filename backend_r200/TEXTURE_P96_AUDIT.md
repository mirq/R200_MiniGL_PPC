# Texture P96 Audit And Retirement Fix

2026-09-07. Based on `abac846`. The unsuccessful native flip-wait prototype
remains reverted in production and inactive in `experiments/native-flip-wait/`.
The installed library was verified as the restored DC041695 baseline before
this separate texture-retirement change was tested.

## Findings

### Clean draws do not repeatedly upload textures

The fixed gears client binds/uploads `chrome.ppm` once during setup, before
timing. Its frame loop changes matrices/environment state, emits geometry,
and presents; it does not call TexImage or TexSubImage per frame.

The PPC mirror accepts the returned surface/content generation, clears its
dirty mask, and reuses that residency for subsequent draws. On the native
side, `EnsureTextureResident` and `SyncTextureDirty` return immediately for
clean resident textures. No repeated P96 allocation/import was found on this
steady-state texture path. Per-frame submission and P96 presentation are
separate costs.

### Explicit mutations are expensive

Source-level operation counts on a successful path:

| Operation | New P96 Bitmap / Import | Main Data Work |
| --- | --- | --- |
| Clean resident draw | 0 / 0 | Reuse existing residency |
| Native texture bind | 0 / 0 | Logical state change |
| Ordinary full image definition | Deferred until residency is needed | New CPU backing, then full backing upload |
| PPC direct BEGIN | 1 / 1 per call | Lease mapped BGRA storage |
| PPC direct COMMIT | 0 / 0 | Commit already-written mapped pixels |
| Nonempty subimage of resident texture | 1 / 1 per call | Update shadow region, then copy the whole backing into fresh residency |
| First subimage of a direct BGRA texture | As above | Also materialize its CPU shadow once |

The direct PPC path converts RGB565/RGBA/RGB into the native BGRA mapping;
RGB565 source therefore becomes four-byte GPU texels. Successful direct
COMMIT uses `srcAddress=0`, avoiding another host-side source-pixel copy.
Shared/chunked fallbacks add staging/reassembly work, but are not the normal
successful direct path. Shared staging is retained and only grows as needed.

The native resident-subimage path eagerly calls `EnsureTextureResident` before
returning. Several small updates without an intervening draw therefore still
create several fresh bitmap/import/full-copy realizations. Existing COPY/BYTES
counters primarily describe `SyncTextureDirty`, not these full realizations;
zero counters are not proof that P96 allocation/copy work was avoided. The
phase5 comment describing coalescing does not establish that allocation behavior.

Additional bounded candidates, not changed in this fix:

- PPC redundant binds still perform a synchronous host command before the
  local same-binding shortcut. Native binding itself does not allocate P96
  storage. This is not part of gears' steady frame loop.
- Texture allocations request `BMF_CLEAR` even though uploaded storage is
  subsequently overwritten. Removing clearing requires proving all sampled
  bytes/padding are initialized; do not remove it from shared color/depth helpers.
- Direct textures deliberately use P96 rather than the service aux pool.
  The aux pool is only 4 MiB / eight surfaces and is shared with depth. The
  current direct COMMIT also expects a P96 bitmap, so changing an allocator
  preference alone is not a valid replacement.

## Concrete Retirement Defect

`ReapDeferredSurfaces` polled each parked node's historical fence. The service
implementation's `IsSessionFence` accepts only the device's current `LastFence`.
After F2 is submitted, testing parked F1 returns false even when F1 is finished.
`Radeon3DReleaseSurface` also resets `LastFence` to zero, so releasing the first
of several nodes carrying F2 can strand the rest. The native reaper could retain
P96 bitmaps, nodes and service handles despite a completed present-time drain.
That can increase VRAM pressure, eviction/re-upload cost, and per-present list
work. No live VRAM leak size or FPS penalty was measured in this session.

The analogous segment reaper used the same invalid historical-token polling.
Surface release could invalidate its token before that reaper ran. Both
reapers also attempted to release old-epoch handles/IDs against a new device.

### Fix

- Surface reaping uses native submit-slot ownership rather than old public
  tokens. It retains all current-epoch surfaces while any current native slot
  remains in flight: releasing even an unrelated surface would otherwise
  drain/reset the service's latest fence underneath that slot.
- After the existing successful drain, all eligible nodes can be released,
  including multiple nodes with the same fence. Service release still precedes
  P96 bitmap free and CPU-node free.
- Segment reaping retains a segment while its matching epoch/fence slot is
  live. A gone slot means retirement or device invalidation. FreeSegment does
  not reset the latest token or implicitly wait for unrelated work.
- Closed-device/old-epoch cleanup frees retained P96 bitmap/node ownership
  without sending dead surface handles or segment IDs to a new device.
- Recovery reaps deferred surfaces immediately after closing/nulling the old
  device, before opening a replacement.

The small shared changes were made in both the native and PPC source copies;
no public ABI or service change was made. PPC ring-slot consumption is NOT used
as GPU retirement: normal PPC texture ownership/reaping lives on the native
host, whose PRESENT path drains its semantic fence first. The PPC pool still
uses its existing host-owned GPU-retired watermark.

No upload barriers, texture-update storage policy, native flip waits, or driver
code were changed. Clean texture usage is still allocation-free after setup.

## Validation

`tests/test_r200_retirement.py` compiles actual reapers from both trees under
ASan/UBSan with async rotation on/off. It covers obsolete and shared fences,
pending-slot retention, zero fences, old epochs, closed devices, release order,
compaction and idempotence. Historical-polling mutations reproduce stranded
surfaces/segments; the current code passes. Recovery is source-order checked.
Independent code review found no introduced premature release in the current
native/PPC call paths. These tests mock the GPU and cache behavior.

Native and PPC builds passed their existing dispatch checks (152 native fields,
134 PPC fields) and symbol/link/relocation checks. Pre-existing compile warnings
remain; the new logic added none. Other lifecycle, readback and texture-order
CPU tests pass.

Physical tests, current healthy session rather than independent cold boots:

- Original DC041695 library: native phase5 image/subimage/deferred-order/mip/
  wrap/environment/blend/alpha tests passed.
- New library: same native phase5 tests passed with identical verified pixels.
- New library: phase5 `--stress` passed, including 48 textures at 512x512.
- New library with unchanged PPC DLL/host: `r200_texture_update_test` passed
  7,127 checks, zero failures, two contexts, 18 updates and 16 frames. The RGBA
  full/first/repeat paths are pixel checked; other format paths retain error/
  ordering checks. Host confirmed 50 execute batches and 16 presents.
- Bootstrap passed before/after deployment and after the final PPC test; all
  clients/hosts exited cleanly and temporary environment overrides were removed.

This validates rendering compatibility, not the exact number of P96 allocations
or a performance gain. Gears was not used to claim a texture speedup because it
does not stream textures in its measured loop.

## Gears Regression On The Changed Library

Follow-up on the installed retirement build `2F05868E`, verified before and
after testing. The unused resident library was flushed before bootstrap and
the series. Same 640x480x32 clients, host priority -1, 1,000,000-byte shell
stack, and two buffers for fullscreen. Pacing/profiling and live output were
off; only the complete fixed-frame run and final drain were timed.

| Log Prefix | Mode | Variant | Frames | Ticks (50 Hz) | Seconds | FPS |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| gbn | Fullscreen BLIT, no sync | Normal | 3000 | 3208 | 64.16 | 46.758 |
| gbp | Fullscreen BLIT, no sync | Precalc | 3000 | 3212 | 64.24 | 46.699 |
| gwn | Windowed | Normal | 1500 | 1691 | 33.82 | 44.352 |
| gwp | Windowed | Precalc | 1500 | 1695 | 33.90 | 44.247 |
| gfn | Fullscreen FLIP, sync on | Normal | 600 | 1477 | 29.54 | 20.311 |
| gfp | Fullscreen FLIP, sync on | Precalc | 600 | 1466 | 29.32 | 20.463 |

One sample per configuration in the same existing initialized session; these
are not independent cold-boot medians. BLIT runs used the same bounded async
capture pattern to avoid the tool's long-call timeout. Every run had `error=0`,
matching host present counts, and clean client/DLL/host exits. Total: 10,200
frames and 34,526 execute batches. Bootstrap passed before and after the
series. No new InitPPC, build, installation or driver change occurred during
these runs. Temporary environment settings were removed afterward.

Verified inputs: DLL `31878AB6`, quiet host `98DE2066`, normal client
`65B0B783`, precalc client `1F3401F7`, Radeon9200.chip `07A7193D`, and
Prometheus.card `18A453D6`. Raw `<prefix>-client.log` / `<prefix>-host.log`
files remain in `DH2:texretire`. Combined `gears-runs.log` (710 bytes) and
`gears-hosts.log` (2624 bytes) were CRC-verified on download into
`/tmp/opencode/texture-retire/`.

Results remain close to the previous roughly 46.6 FPS BLIT, 44 FPS windowed,
and 20-21 FPS FLIP observations. There is no obvious throughput regression or
material normal/precalc difference in these samples. This is a compatibility/
regression check, not a measured streaming-texture speedup; no old-library
control or per-frame gap measurement was taken in this follow-up.

## Deployment And Provenance

Only the native retirement fix was installed for the hardware tests. The newly
built PPC mirror was build-checked but not substituted into the tested DLL.
The previously accepted PPC wait-order fix remains in the unchanged DLL.
The native wait experiment is still absent.

| Artifact | Bytes | CRC32 |
| --- | ---: | --- |
| Original / `DH2:texretire/native-base` | 125896 | DC041695 |
| Installed retirement-fix `LIBS:minigl.library` | 126096 | 2F05868E |
| Native `phase5` test | 27664 | 594248A0 |
| Native deployment `test.zip` | 71074 | D124DC84 |
| PPC `updates.elf` | 122062 | 3604C447 |
| Converted PPC `updates` | 97248 | F829BE0E |
| PPC deployment `updates.zip` | 53386 | 42E67659 |

ZIP transfers/extracted artifacts and installed library CRCs were verified.
`Avail Flush` was run and the old resident library was confirmed absent before
testing. Host `DH2:gearsstut/mglhost_pacing0` and DLL remain the prior quiet,
priority-safe setup; the Picasso96 driver pair was not changed. Existing
powerpc.library 17.15 initialization was reused; InitPPC was not repeated.

Logs remain in `DH2:texretire`: `base-p5.log`, `new-p5.log`, `stress-p5.log`,
`ppc-test.log`, `ppc-host.log`. New phase5/stress/PPC logs were CRC-verified on
download into `/tmp/opencode/texture-retire/`. Isolated builds used that same
host directory, preserving the user's pre-existing bin/obj and Makefile changes.

## Next Texture Work

First measure the actual streaming client's allocation/import/copy counts and
time separately from presentation. Then consider coalescing unsampled subimage
updates into one fresh realization. Preserve old snapshots and the old allocation
until replacement storage exists; do not restore the unsafe byte-equality skip,
blindly reuse sampled addresses, or remove required cache/GPU dependencies.

## Driver Working-Memory Follow-Up

The 2026-09-07 follow-up found a separate steady-path problem in the unchanged
driver: its private ExecuteTrusted, ExecuteGenerated and ExecuteEmitter buffers
were allocated in PPC RAM while being processed by the 68k. Live session
inspection and a CPU-only placement comparison established the issue; it is
not repeated texture uploading in gears. The standalone emitter was about
3.5-5 times slower for representative textured batches with its fixture in
PPC RAM, with matching final command checksums. This is not a measured gears
speedup or proof of the historical approximately 100 FPS configuration.

`/home/mirek/p96-driver/EXECUTE_MEMORY.md` records attribution, the allocator
hook, correction, tests, artifact identities and the completed reboot matrix.
The first candidate `209446AE` was ineffective: the PPC-task AllocMem hook adds
a PPC requirement to PUBLIC/FAST requests, making its LOCAL+PUBLIC+FAST
preference fail and fall back to PPC RAM. The installed correction `23ACBAEC`
requests LOCAL alone, verifies FAST with TypeOfMem (rejecting/freeing Chip RAM),
and retains the old public-allocation fallback. All three working pointers were
then confirmed in Amiga-local RAM; no GPU visibility, surface, vertex-segment,
CPU-flag, DLL or native-library changes were mixed into this driver comparison.

Three independently bridge-cold-rebooted runs per build, 600 frames each at
640x480x32 BLIT/no-sync/two buffers/host priority -1:

| Driver | Samples (FPS) | Median |
| --- | --- | ---: |
| Original 07A7193D | 44.510, 44.313, 44.313 | 44.313 |
| Corrected 23ACBAEC | 54.446, 54.844, 54.644 | 54.644 |

The median improves **23.3%**. Every run had zero errors and matching 2,031
executions / 600 presents. These were bridge cold reboots, not operator power
cycles. Native texture stress/phase6 primitives/acceptance and the 7,127-check
PPC update test passed. Normal fullscreen and precalc windowed regression runs
also passed. Approximately 100 FPS remains unreproduced; no all-stalls-fixed
claim is made. The PPC bind cache remains separate and undeployed.

Active pair: `23ACBAEC` / unchanged `18A453D6`; both `.previous` recovery files
hold original `07A7193D` / `18A453D6`. With explicit user permission, the current
working display modes were preserved byte-for-byte in the `.9200` settings
profile and the monitor ToolType corrected; originals are backed up under
`DH2:drvperf`. Full provenance and raw log locations are in EXECUTE_MEMORY.md.

## Subsequent CP And Texture-Matrix Fixes

The next isolated driver work fixed a texture-only fast path that skipped
changed matrices in multi-record Execute/CommitBatch submissions. Hardware RGB
controls reproduced two stale-transform failures on `23ACBAEC`; corrected
`6B860EE0` passes them and the native/PPC rendering regressions.

A separate fused CP copy change then removed the eight-word stack staging pass,
without removing fences, posted-write readbacks or ring bounds. Against the
texture-fixed baseline, three independently bridge-rebooted runs per build gave:

| CP Kernel | Samples (FPS) | Median |
| --- | --- | ---: |
| Old, 6B860EE0 | 53.956, 54.446, 54.844 | 54.446 |
| Fused, 9768419A | 57.034, 57.471, 57.251 | 57.251 |

Same 600-frame, 640x480x32 BLIT/two-buffer/no-sync/priority -1 workload; all
counts match and errors are zero. Median gain is **5.2%**; CP submit elapsed
falls from 2.582 to 1.152 ms/frame. The two changes were staged separately.

The active pair is now `9768419A` / `18A453D6`, with texture-fixed
`6B860EE0` / `18A453D6` in both `.previous` recovery files. Native library
`2F05868E`, DLL `31878AB6` and host `98DE2066` are unchanged; the separate
PPC bind-cache candidate remains undeployed. Native texture stress, phase6,
phase7 and the 7,127-check PPC update test pass. Full evidence and test/build
recipes are in `/home/mirek/p96-driver/CP_TEXTURE_FIXES.md`; logs are under
`DH2:cptex`. These results do not establish approximately 100 FPS or removal of
all frame stalls.
