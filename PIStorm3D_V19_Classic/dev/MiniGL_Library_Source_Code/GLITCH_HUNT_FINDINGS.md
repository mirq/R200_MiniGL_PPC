# BlitzQuake glitch hunt — hardware findings (2026-08-25)

Physical 68060/RV280, `Work:Games/BlitzQuake_68k`, windowed 320x240x16 unless
stated. Probe sources live in `examples/`: `seqprobe.c`, `visualprobe.c`,
`bisectprobe.c`, `tclprobe.c`, `blend_alpha_smoke.c`. All PPM dumps need a
byte-swap to view (fixed in `ReadSurfacePixel` as part of this work).

## Confirmed working

- Bonus-flash fill path: textured 1x1 `MGL_FLATFAN` + `GL_MODULATE` +
  `SRC_ALPHA` blending + scissor renders pixel-correct (probe Q1/Q2).
- Diffuse-alpha blended fill renders correctly (probe Q2) — the
  "diffuse alpha reaches R200 as zero" workaround comment in BlitzQuake's
  `gl_draw.c` appears stale for the current library.
- Smooth shading, provoking-vertex flat shading, additive `GL_ONE/GL_ONE`
  accumulation, depth-test-with-masked-writes: all correct **when the draw
  is the first TCL record of a submit**.

## Confirmed broken: hardware-TCL records silently lost (refined)

`tclprobe` (windowed 320x240, pixel-sampled via `mglLockBack`, raw values are
byte-swapped 565): within one submitted batch, some TCL draw records render
and others never appear — no GL error, records counted in
`MGL_PERF_DRAW_RECORDS`, Execute succeeds, the stream is NOT derailed
(records after a lost one still render).

Observed matrix:

- f0 `[fan smooth][fan smooth]` -> both render
- f1 `[fan][tris flat]` / f2 `[tris][fan]` -> tris lost either way, fan fine
- f3 `[tris][tris]` identical state (merged to one record) -> lost
- f4 `[fan][fan + GL_ONE/GL_ONE blend]` -> additive fan LOST (fans can die too)
- f5 `[tris][point size 4]` -> both lost
- f6 lone tris record in its own submit -> lost
- quad_list_smoke (default SMOOTH state) renders with pixel checks
- cull_shade_smoke "passes" vacuously: both its triangles never render, so
  all equality comparisons agree on black

So it is not submit ordering, not opcode transitions per se, not
streaming-vs-inline, not fullscreen-vs-window. Working records so far:
smooth-shaded untextured fans, quads (quad_list API), textured world-style
draws, screen-space FLAT fills. Lost: flat-shaded tri/point/line records,
an additive-blended fan (fragment-state record).

Working hypothesis: records whose state block differs from the previous
record's (shade model flip, blend fields appearing) or certain
state+opcode combinations produce packets the card ignores; fans under an
unchanged state survive. The primitive-type constant table matches Mesa's
`R200_VF_PRIM_*` exactly, so the packet type bits are correct.

BlitzQuake impact: weapon effects (nailgun tracers, explosion particles,
flashblend dlight fans) are TCL records that follow world records in the
same submits — effect loss/corruption is consistent with this pattern.

## Root cause found and fixed (2026-08-25)

`tclprobe` f13/f14 cross-check nailed it: SMOOTH triangles render, FLAT fans
fail — shade model, not primitive type, decides survival.

**The bug** (driver, `radeon3d_service.c` EmitExecuteState): flat shading was
programmed as `seControl &= ~R200_DIFFUSE_SHADE_GOURAUD`. Per Mesa's
`r200_reg.h` the SE_CNTL shade fields are two-bit encodings
(SOLID=0, FLAT=1, GOURAUD=2), so clearing GOURAUD selected **SOLID** — the
unprogrammed solid-colour path — instead of FLAT. Flat-shaded TCL draws
rasterized in an undefined solid colour (black), i.e. invisible on black
clears and corrupt over real content.

**Fix**: clear `R200_DIFFUSE_SHADE_MASK`, set `R200_DIFFUSE_SHADE_FLAT`
(+ the SOLID/FLAT/MASK definitions added to `radeon_regs.h`).

**Verified on hardware**: all tclprobe frames render (triangles, quads,
strips, polygons, six-vertex lists, additive-blend fans, flat fans); the
full phase4/5/6 + cull_shade + quad_list suites pass (cull_shade's passes
are now real pixel comparisons, not vacuous black-on-black); blend_alpha
FAILBITS dropped 1bbe -> 3be (remainder: stale raw-value expectations plus
the point-size issue below); glquake world rendering confirmed correct by
screenshot (geometry, lightmaps, weapon model, monsters, torches).

## Remaining known issue: glPointSize ignored (points render 1px)

Points rasterize at the correct position but as a single pixel regardless
of `glPointSize`. Library packs `(size*16)<<8` into TransformFlags; the
service writes `RE_POINTSIZE = size*16 | (1024<<16)`. Mesa writes
`(MaxPointSize*16)<<R200_MAXPOINTSIZE_SHIFT | size*16` (U4.4 both fields,
shift 16). Value shape looks equivalent, yet the hardware renders 1px —
needs a focused investigation (possibly the VAP/VTX output-format PSIZE
path). Quake particles are affected (tiny explosion dots).

## In-game status

The full-screen yellow wash on pickup/damage is `R_PolyBlend` ->
`Draw_AlphaFill` (cshift fill). With cshifts disabled (`+gl_cshiftpercent 0`)
the game renders perfectly. The wash intensity at 3 FPS is consistent with
legitimately accumulated damage (max 150/255) + bonus cshifts — combat-heavy
demo at low FPS stacks them. A 16bpp A/B run to rule out 32bpp alpha
inflation crashed the machine (Break mid-frame + immediate relaunch; see
AGENTS.md automation policy) — retest after power-cycle.

## Fixed along the way

- `ReadSurfacePixel` (16 bpp): `R5G6B5PC` words are little-endian on the
  surface; the big-endian load crossed channels. `glReadPixels` and
  `mglWriteShotPPM` now decode true colours.
- Driver fence tail reordered (WAIT_UNTIL, then both destination-cache
  flushes, then scratch) and `RadeonCpTestFence` now drains both
  destination caches; `RadeonCpWaitFence` adds a bounded RBBM/GUI idle
  grace. These are correctness hardening; the fence alone did not explain
  the record loss.
- Segment cursors are no longer reset per submit (monotonic write offsets,
  inline fallback when a half fills) — defensive, per the
  "fetch caches never re-read rewritten lines" quirk.

## Ruled out

- First-submission swallow: was a probe bug (zero-size fan from an
  uninitialised `lock`); fixed probe renders on the very first submit.
- Matrix/scissor state for the flash: record headers carry the full-screen
  rect; `MGL_FLAT*` bypasses matrices by design.
- Windowed-only: reproduces fullscreen.
- Streaming-only: reproduces with `STREAM_COMMITS=0`.

## Next steps

1. Dump the service-side generated CP stream for a failing batch (debug
   driver + stats block read via the bridge `inspect_memory`, which works
   on this target) and diff against a gears batch.
2. Add pixel verification to `phase6_primitives` so this class regresses
   visibly.
3. Re-verify in-game with the `-diag` build after the record-loss fix.

## Point size (FIXED 2026-08-25, library-side)

`glPointSize` has no effect: points rasterise exactly 1x1 at every size.
Reproduced deterministically by `minigl_testsuite --test raster.point-size`
(detail `px 1->1 2->1 4->1 8->1 16->1`, POINTDIAG shows count=1 box=1x1).
Verified at 32bpp windowed on RV280.

Ruled out by inspection or measurement:

- Library plumbing is correct: `Lib_glPointSize` stores the size, bumps the
  semantic serial, and `CapturePendingDrawState` packs `size * 16` into
  `RADEON3D_TRANSFORM_POINT_SIZE_MASK` (shift 8, 12-bit field, no collision
  with the flag bits below bit 8).
- Service extracts it correctly and writes `R200_RE_POINTSIZE` (0x2648) as
  `size*16 | (1024 << 16)`. Low half matches Mesa's U4.4 encoding
  (`r200PointSize`: `cmd |= Point.Size * 16.0`); the high half is the max-size
  clamp and 1024 means 64px, well above the tested sizes.
- `SameExecuteState` does compare `TransformFlags`, so a size change is not
  swallowed by the state cache.
- `R200_OUTPUT_PT_SIZE` is not set in `SE_TCL_OUTPUT_VTX_COMP_SEL`, i.e. the
  hardware is not being told to take the size from a per-vertex attribute.

Changes made from Mesa's implementation that did NOT fix it (kept because they
match Mesa and the suite shows no regression, 12/13 pass, but they are not a
fix):

- Emit the point-sprite vport-scale and attenuation vector blocks
  (`R200_VS_PNT_SPRITE_VPORT_SCALE` 0x68 with the size at index 2,
  `R200_VS_PNT_SPRITE_ATT_CONST` 0xbc), mirroring Mesa's `ptp` state atom.
  Motivated by Mesa's comment in `r200PointSize()`: "point size reg value is
  not used when calculation is active".
- Emit `R200_SE_TCL_POINT_SPRITE_CNTL` (0x22c4) as
  `R200_PS_SE_SEL_STATE | R200_PS_MULT_CONST`, which is what Mesa programs
  whenever point attenuation is off, i.e. select the setup-engine state
  register (`RE_POINTSIZE`) as the size source. Our service never wrote this
  register at all before.

Remaining hypotheses, cheapest first:

1. The pragmatic fix: expand points to screen-space quads in the library using
   `context->pointSize`, as `AppendScreenPoint` already does for the
   screen-space path (r200_geometry.c:1050), instead of relying on hardware
   point sizing at all. Deterministic and covered by the new test.
2. Register-level visibility (does the RE_POINTSIZE write actually land?)
   would settle it, but needs a debug driver; a previous DEBUG build with DOS
   hooks crashed the machine, so that route needs care.

### Resolution

Hardware point sizing was abandoned; `minigl.library` sizes points itself.
`EmitSizedPoints()` (r200_geometry.c) transforms each point on the CPU with the
same combined matrix, viewport and depth-range mapping that
`CapturePendingDrawState` writes into record header dwords 21..42, then expands
it into a screen-space quad via `AppendScreenPoint()`, the path polygon-mode
`GL_POINT` decomposition already used. Pending hardware-transformed geometry is
flushed first, because `PrepareScreenAppend`'s fast path does not compare
`screenHardwareTransform`.

Gated on `pointSize > 1.5`, so default-size points keep the cheap hardware path
and nothing changes for clients that never call `glPointSize`.

Verified on RV280, 32bpp windowed:
`raster.point-size PASS detail="px 1->1 2->2 4->4 8->8 16->16"`, with pixel
counts 1/4/16/64/256 in correctly centred boxes. Full suite 13/13.

Both driver-side attempts (point-sprite vector blocks, SE_TCL_POINT_SPRITE_CNTL)
were reverted after they proved ineffective, so the driver keeps only the
flat-shade fix and does not pay their per-state-emit CP cost.

NOTE: minigl.library stays resident between runs. After deploying a new
library, run `Avail FLUSH` (or reboot) or the old code keeps running - this
cost one debugging cycle here, where the fix appeared to do nothing.

## Streaming commit stage-44 rejection (ROOT-CAUSED 2026-08-25, fix awaiting hardware verify)

`minigl_testsuite`'s `raster.prim-shade` test (5 primitives, alternating shade
models and opcode transitions, 70 presented frames) exposed a streaming-commit
bug: `Radeon3DCommitBatch` rejects the batch at its own stage 44 —
`VertexOffsets[i] >= slot->Bytes` — with `rec=44 draws=1` (one unlit TCL
header, normal shape, bad offset table). Sometimes the reject is clean;
sometimes the malformed batch wedges the CP and the machine crashes
(two power-cycles on 2026-08-25).

Depth-independent (seen at 16 and 32 bpp). The inline Execute path
(`STREAM_COMMITS=0`) never touches this code and is stable.

Mitigation deployed and verified: `LIBS:minigl.library` rebuilt with
`STREAM_COMMITS=0` (original mitigation crc 1f27d185; later inline builds have
different CRCs as the source evolved). Suite 13/13 at 512x320x16 and x32.
This matches the long-standing comment in `FlushScreenVertices` ("use the
validated inline Execute path for production rendering").

Suite sampler fix in the same round: raw `mglLockBack` words at 16 bpp are
byte-crossed R5G6B5PC; green is split across the byte boundary and cannot be
decoded with a contiguous mask. `PixelToken` now byte-swaps 16-bit tokens, so
the runtime-calibrated channel decode sees true 565 values. The two 16 bpp
suite failures before the fix were measurement artifacts; the renders were
correct (verified by decoding the reported values by hand).

### Root cause

Two unguarded slot switches in `FlushScreenVertices` wrote vertices outside
the build slot's segment half:

1. **Mode-change submit** (`slot->commitMode != useCommit`): `useCommit` and
   its half-space check were evaluated against the OLD build slot; after
   `Submit()` flipped to the sibling, the flush committed into the sibling
   without checking ITS remaining space.
2. **Budget-overflow submit**: same pattern — `slot->commitMode = useCommit`
   was applied to the freshly landed sibling using a fit decision computed
   for the submitted slot.

With monotonic per-slot cursors (reset removed for the RV280 fetch-cache
quirk) both halves fill toward `SEGMENT_HALF_BYTES`, so the landed sibling is
regularly near-full. The overshoot then:

- writes the vertex tail past the half end — into the sibling half (trashing
  in-flight or future vertex data: the long-standing "segment-reuse
  corruption" under sustained Quake load) or past the end of the segment
  allocation itself;
- advances the cursor past `SEGMENT_HALF_BYTES`, so the NEXT offset entry
  becomes `>= segment.Bytes` and the service rejects the batch at stage 44
  (`fencestage=0x8000002C rec=44 draws=1`). A second signature
  (`fencestage=0x00000000`, GetInfo failing afterwards) is the same corruption
  reaching `BuildExecuteStream`/recovery instead of the offset check.

Why "interactive fails at start, single tests pass": the menu redraws only on
keypress (few frames, cursors near zero — silent). Pressing Return starts the
continuous preview loop, whose large text batches exhaust a 128 KiB half
within seconds at preview FPS; from then on every slot switch overshoots.
`--run-all` uses small non-text draws plus readback drains between tests, so
it usually survives (one clean reject observed mid-run in `T:mgl_out.txt`),
and short focused commit probes never reach exhaustion at all — which is why
they passed while real workloads corrupted.

### Fix (library-side, r200_geometry.c/r200_submit.c)

- `SegmentHalfFits()` helper; after EVERY mid-flush `Submit()` (mode change
  and budget paths) the landed slot's remaining space is re-checked and the
  flush degrades to the inline record shape if needed (`required`,
  `generatedRequired` recomputed).
- `RotateStreamSegment()`: when the build half cannot hold the draw, drain
  everything, allocate the replacement segment BEFORE freeing the old one
  (the service's lowest-free-index policy then alternates pool slots, i.e.
  real VRAM regions), switch, and zero both cursors. Streaming therefore
  continues indefinitely instead of silently decaying to inline once both
  halves fill. On rotation failure the old segment stays valid and the flush
  goes inline.
- `InvalidateSubmissionState()` zeroes cursors across device epochs (fresh
  epoch = fresh allocation).
- Diagnostics kept: commit-fail print now includes `slot=`, `cursor=`,
  `segId=`, `segBytes=`, `off0=`, and `DumpFailBatch()` writes the rejected
  batch (records + offset table + scalars) to `T:r200_failbatch[1-4].bin`.

Verification status: built (`bin/dispatch-r200-gcc/minigl.library`
crc 9b9eb1d9+); hardware run pending — the reproduction wedged the machine
again (RETURN into the interactive preview), requiring a cold power-cycle.

## Dense native-quad menu hang (mitigated, 2026-08-25)

The interactive test-suite menu originally redrew its 5x7 software font on
every loop iteration. Each lit font pixel was one native `GL_QUADS` primitive,
so one static menu frame submitted several thousand tiny quads. The bridge
became unreachable after 40-60 seconds; a standalone glyph-density quad probe
reproduced the same hard wedge, while a low-density quad/title/present loop
remained responsive for 100 seconds. After making the menu event-driven, the
first frame completed but a later selection redraw still wedged the device,
isolating the failure to repeated dense native-quad frames rather than input,
window-title updates, the timer, or the log file.

The suite UI now blocks on the Intuition window's message port while idle and
redraws only after selection or benchmark-length changes. `DrawText` no longer
uses native quads: contiguous lit pixels in each glyph row are coalesced into a
rectangle and emitted as two `GL_TRIANGLES`. This both reduces geometry and
keeps UI text on the stable triangle path; it does not change the library's
public `GL_QUADS` implementation or the explicit primitive correctness tests.

Verified on RV280 at 512x320x32 windowed: five menu redraws (selection and frame
length), clean Escape/context teardown, post-exit `bootstrap_smoke`, and the
full automated suite (`passed=13 failed=0 skipped=0`) all completed with the
bridge responsive.

## Depth-pitch tile alignment — vertical black bars (fixed, 2026-08-27)

`quake.world-room-walk` at 800x600 showed intermittent 32-pixel-wide vertical
black columns (observed at x=64..95 and x=768..799), pose-dependent and
repeating with the 256-frame walk cycle, identical at 16 and 32 bpp. The
offline reference render is clean, so the defect was in the back buffer.

Root cause: the R200 depth buffer is tiled. For 16-bit Z the micro-tile is a
4x4 group of z-pixels and the macro arrangement is 8x2 of those, i.e. 32
pixels wide, handled in pairs; tile addressing is a linear function of the
pitch in pixels divided by the tile width (visible in the Linux DRM's
`radeon_cp_dispatch_clear` macro-tile path). A pitch that is not a whole
number of tile pairs makes a 32-pixel column alias across the row boundary and
lose its depth writes; the stale depth then rejects everything drawn there.

`DepthStorageWidth()` now pads the depth pixel width to a multiple of 64
(800 -> 832), which is the alignment the Linux radeon DDX applies to every
tiled surface (`displayWidth = (virtualX + 63) & ~63`) — and the reason a
width like 800 never occurs there. `ValidDepthTarget()` in `Radeon9200.chip`
requires the resulting 128-byte pitch alignment and accepts a depth surface
wider than the color target.

Measured widths: 320, 512, 640, 768 and 832 clean; 800 striped. Every data
point fits the 64-pixel rule; an earlier 256-byte-pitch hypothesis was
falsified by clean fullscreen 320 (640-byte pitch) and by clean windowed 832
(1664-byte pitch). The rule has not been isolated by a controlled test — two
attempts ran inside a 1024-wide screen and only reported the undrawn region.

Verified: windowed 800x600x32, 400 frames, 0 reports / 50 glitch scans;
fullscreen 800x600x32, 800 frames, 0 / 100.

## Released texture surface in an unsubmitted batch (fixed, 2026-08-27)

Symptom: black and green textures, reproducible only from the interactive
menu or `--run-all` (never from a single `--test`, which starts with an empty
batch). `quake.lightmap-update` failed with `gl_error=89`
(`GL_INVALID_OPERATION` in this backend's numbering).

`glTexSubImage2D()` re-imports the texture at a fresh VRAM address, because
the GPU's texture cache will not drop stale lines for a reused address. The
stale surface is parked for `ReapDeferredSurfaces()` only when
`staleFence` is set, which happens on submission. But `glTexSubImage2D()`
begins with `FlushScreenVertices()`, which appends records naming that texture
into the still-unsubmitted build slot — those records carry no fence, so the
surface took the immediate-release path while they still referenced it. At
commit the service resolved the recorded handle to NULL, rejected the draw,
and threw out the whole batch; the failing flush then made the texture upload
report `GL_INVALID_OPERATION` and the lightmap never updated.

Fix: submit before the re-import when the texture is queued in the build slot,
the same `TextureQueued()`/`Submit()` barrier `SyncTextureDirty()` already
used. No measurable cost (`quake.lightmap-update` 19263 -> 18861 fps, within
run-to-run noise).

Attribution came from splitting the service's coarse commit-failure stage 22
into per-term codes (`radeon3d_service.c`), which runs only on the failure
path: `0x80060016` -> `0x8006003F` (texture validation) -> `0x80060050`
(handle resolves to NULL).

Verified: `--run-all` at 800x600x32 fullscreen and windowed, 24/24 pass, no
commit failures; per-test frame rates unchanged within +/-5%.
