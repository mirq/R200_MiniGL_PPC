# V19 Parity Implementation Plan (R200)

Goal: make the V10 R200 backend functionally aligned with what the V19
`mglQ3` implementation *actually does* (not what its headers merely
declare), scoped to the R200 backend with safe shared-library semantics.

## Scope

Included:

- `glInterleavedArrays()` — new dispatch field (ABI v5)
- `mglEmergencyCloseScreen()` — new dispatch field (ABI v5), with safe
  terminal semantics instead of V19's half-destroyed context
- `mglDrawMultitexBuffer()` — R200 implementation of the V19 two-pass
  polygon queue
- `mglGetSupportedScreenModes()` — V19 return-value semantics
- `glHint()` — V19 target acceptance (fog, perspective, W-one)
- `mglChooseMtexBufferSize()` — effective queue capacity
- Pre-context choosers wired to real behavior where feasible
- `glColorMask()` — driver-capability-gated (optional, stretch)

Excluded (documented rationale):

- `glMultiDrawArrays()`, `MGLSwitchBuffer()`, `glMGLForceNewRenderPass()`:
  no V19 implementation exists
- Direct `MGLInit()`/`MGLTerm()`: V10 lifecycle is `MiniGLOpen()`/`MiniGLClose()`
- Slope-scaled `glPolygonOffset()`: V19 stores values but never applies
  them; R200's constant-unit bias already exceeds V19 behavior
- `mglProhibitMipMapping()`: already has a real R200 effect

## Phases

### Phase 1 — Parity test program (in progress)
- `examples/v19_parity_r200.c`: interleaved arrays (6 formats, custom
  strides, error cases), multitex queue, screen-mode callback semantics,
  hint acceptance, choosers. Self-contained (generated textures, pixel
  readback); no `minigl_data.lha` dependency.
- Separate emergency-teardown executable (terminates the context).

### Phase 2 — Dispatch ABI v5
- `include/libraries/minigl_dispatch.h` (+ packaged `dev/include/` copy):
  - `MINIGL_DISPATCH_ABI_VERSION` 4 → 5
  - Append `GLInterleavedArrays` and `MGLEmergencyCloseScreen` fields
    after the v4 lighting tail (append-only; positional initializers
    elsewhere must stay valid)
  - Guarded client wrappers `MGLD_glInterleavedArrays`,
    `MGLD_mglEmergencyCloseScreen` + `#define`s
- `minigl.fd` / assembly import stubs unchanged (table-size independent)

### Phase 3 — R200 backend implementations
1. `library/r200_geometry.c`: `Lib_glInterleavedArrays()` — configure
   existing `ArrayState` structures for the six V19 layouts:
   | Format | V off | C off | T off | packed stride |
   |---|---|---|---|---|
   | GL_V3F | 0 | – | – | 12 |
   | GL_C4UB_V3F | 4 | 0 | – | 16 |
   | GL_C3F_V3F | 12 | 0 | – | 24 |
   | GL_T2F_V3F | 8 | – | 0 | 20 |
   | GL_T2F_C4UB_V3F | 12 | 8 | 0 | 24 |
   | GL_T2F_C3F_V3F | 20 | 8 | 0 | 32 |
   Reject inside-begin, negative stride (INVALID_VALUE), unknown format
   (INVALID_ENUM). Replace (not merge) enabled-state; clear compiled-array
   cache; leave normal array disabled.
2. `library/minigl_r200.c`: `Lib_mglEmergencyCloseScreen()` — safe
   terminal teardown: mark terminating, detach from dispatch visibility,
   invalidate FastPath/locked arrays, bounded retirement (no indefinite
   wait), shared teardown helpers with normal destroy, clear
   `LibraryBase->context`/`owner`/`mini_CurrentContext`; later
   `mglDeleteContext()` is a no-op.
3. `library/r200_geometry.c` + `minigl_r200_internal.h`:
   `Lib_mglDrawMultitexBuffer()` — context-owned polygon queue captured
   when unit 1 enabled + `GL_POLYGON`; flush = pass 1 unit-0 texture,
   pass 2 unit-1 texture with requested TexEnv/blend; depth-write-safe
   second pass (disable writes, equal-compatible test); full state
   save/restore; capacity from `mtexBufferSize` (rounded to 4, validated).
4. `library/r200_surface.c`: `Lib_mglGetSupportedScreenModes()` — return
   `mode.id` on `GL_TRUE`, `MGL_SM_BESTMODE` otherwise/null-callback;
   null-terminate names; keep 8/16/32 filter.
5. `library/r200_state.c`: `Lib_glHint()` — accept `GL_FOG_HINT`,
   `GL_PERSPECTIVE_CORRECTION_HINT`, `MGL_W_ONE_HINT` (advisory where no
   hardware lever); keep perf-counter hint.

### Phase 4 — Other dispatch producers
- stub: logging stubs for both new fields
- classic/pistorm3d: NULL-tail (wrapper guards must not call through
  missing entries) — same convention as v4 lighting
- `client/minigl_ppc_api.c`: sparse designated-init table → new fields
  stay NULL; guards make this safe
- classic build must still link (packaged archive lacks new symbols)

### Phase 5 — Build + regenerate
- `make -f Makefile_dispatch_r200.gcc dispatchtable` after backend edits
- Regenerated table target: 152 fields, 152 bound, 0 unsupported
- Build order: stub (sanity) → R200 (all) → classic (ABI layout check)
- `m68k-amigaos-nm -u` empty on all linked libraries

### Phase 6 — Hardware verification (physical Amiga 192.168.1.21:2345)
- Windowed 16/32; fullscreen 8/16/32
- Interleaved arrays: immediate, DrawArrays, DrawElements, locked
- Multitex: REPLACE/MODULATE/DECAL × blend set from mtex demo
- Buffers 1/2/3 fullscreen presentation
- Screen-mode enumeration (smtest port)
- Emergency teardown last in each session
- Regression: phase5/6 primitives/6 acceptance/7 lighting/phaseE
- Perf: 800x600@32 windowed gears + four-Ogre; reject regressions

### Phase 7 — Packaging + docs
- Sync `dev/include/libraries/minigl_dispatch.h` (byte-identical)
- Update counts/versions in README.md, AGENTS.md, developer docs
- Record progress in this file's Progress log

## ABI compatibility note

Clients validate `abiVersion == MINIGL_DISPATCH_ABI_VERSION` exactly.
v5 clients reject v4 libraries and vice versa — intentional break, ship
matching header + import lib + runtime together.

## Progress log

- 2026-09-05: Authorized matched-native negotiation integration started. Added
  scalar private selector 0x4d47500c (one GLint, context-independent tagged V1
  reply); host subimage capability/command require a positive checked response.
  V2 preflight replaces the image-requiring V1 preflight; V1 remains post-update.
  Public PPC ABI 3, native ABI 5, and mailbox protocol 15 remain unchanged.
  Root build outputs stay under /tmp/opencode; no hardware operations.
- 2026-09-05: Matched-native integration completed and reviewed in source and
  host-independent tests; this resolves the earlier V1-preflight/scope blocker
  below. Selector 0x4d47500c is unique in both active private bridge headers and
  writes exactly one GLint, including before context creation. Checked response:
  `(reply & 0xffffff00) == 0x54580100` and bit 0 set. Host HELLO advertises
  protocol-15 TEXTURE_SUBIMAGE_V1 only on that positive check, and command 26
  independently refuses execution without it. With a live context GetError must
  be zero; without a context native GetError's INVALID_OPERATION is expected,
  but never substitutes for the positive tag/bit. Old, unknown-version, missing-
  bit, and GL-error replies fail closed. No public ABI/version bump.
  Host uses a bounded V2 descriptor for preflight (no eager materialization),
  native TexSubImage performs the lazy backing, and V1 postquery returns its
  image-backed residency to the frontend. Protocol documentation in root
  backend_r200/include/libraries/minigl_ppc_protocol.h now specifies this matched
  behavior rather than requiring NODIRECT. The remaining restrictions are >2 MiB
  subimages, unsynchronized color-index sources, non-BGRA direct surfaces, and
  extending a direct texture's one-level mip chain. The default direct BGRA
  textures no longer have the former subimage compatibility restriction.
  Passed ASan/UBSan capability canary/null/context/old-peer HELLO tests, actual
  native V2-direct -> subimage -> V1-backed sequence, materialization failure and
  pixel/accounting tests, host/frontend actual-function tests, 100000 production
  span cases, and COMMIT/subimage mailbox timeout/late-completion regressions.
  Harnesses: /tmp/opencode/test_r200_subimage_caps.py,
  test_r200_native_materialize.py, test_r200_subimage_frontend.py (also runs host
  checks), and test_r200_subimage_mailbox.py. Production arithmetic tests remain
  embedded in the root protocol header with their standalone build invocation.
  Native prescribed dispatchtable + normal GCC build passed (152/152 bound,
  nm -u empty). Root host/frontend/WarpELF builds passed into /tmp/opencode;
  normalized ELF symbol/relocation checks passed, and the generated PPC table
  compares byte-identical with the root table (134/134 bound). Existing unrelated
  compiler warnings remain. Matched artifacts and SHA256:
  - native bin/dispatch-r200-gcc/minigl.library:
    c5783972fdc3a4a27d42cca66ddf98cb9a75ce64149fe00aee32553ddc2bd5cc
  - /tmp/opencode/minigl_ppc_host_subimage:
    4d2d19258258b86328430b89100fcede103e9031ee494435be8a249abfd75377
  - /tmp/opencode/minigl_ppc_r200_subimage.warpelf:
    28fe80696def4d1499c3c7989cb7a424af384db7e57d787822f8fc1380116bf9
  No root bin writes, native/driver deployment, or hardware access. Hardware
  acceptance remains pending for alias cache visibility, old-draw/update/new-draw
  pixels, texture sizes/formats, repeated updates and unchanged full-upload timing.

- 2026-09-05: Started lazy native backing for committed direct BGRA textures on
  first subimage. Scope: preserve the ordinary direct-upload hot path, allocate
  canonical image/dirty metadata transactionally, fence old uses before reading
  the leased CpuAddress, and reuse native subimage dirty/reimport handling.
  No hardware operations. Automatic matched-native capability detection needs
  one r200_state.c private-query case; permission requested separately.
- 2026-09-05: Native lazy-backing implementation/build completed (NOT hardware
  validated, bridge integration still pending scope approval). First nonempty
  valid subimage materializes only committed one-level BGRA direct textures.
  Image and dirty-rect allocations precede all flush/wait operations; either
  allocation failure leaves the texture object and accounting untouched.
  Successful materialization flushes queued old draws, submits texture users,
  waits their last use, rechecks epoch/lease, invalidates the clean CPU alias,
  and copies active row bytes into zero-padded canonical storage. Original
  surface stays alive until the normal subimage fresh-residency path replaces it.
  One-level dimensions/defined mask, generation/content serial and sampler state
  are preserved by materialization; no eager backing or copy added to BEGIN/COMMIT.
  Further mip definitions on direct textures remain outside this step.
  ASan/UBSan actual-function harness passed allocation rollback, mapping bounds,
  accounting, flush/submit/wait failures and epoch recovery, padded-row pixel
  preservation, subimage/reimport, and no second materialization:
  `python3 /tmp/opencode/test_r200_native_materialize.py`.
  Prescribed GCC build: dispatchtable regenerated (152/152 bound), normal library
  linked, nm -u empty; existing unrelated warnings remain. Artifact:
  `bin/dispatch-r200-gcc/minigl.library`, SHA256
  `6bac2f72d25191cbf30c421e6c79b17ab0501988232461e9a994fd48781eb16d`.
  Review found the host's current V1 preflight still rejects image-less direct
  textures before entering native subimage. It must use V2 for preflight, V1 for
  the post-update reply, and negotiate a new native private capability before
  claiming direct-subimage support. A matching native/host build is required;
  neither protocol 15 nor dispatch ABI 5 alone proves the new native behavior.
  No deployment or hardware access. Runtime review/acceptance must cover native
  alias visibility, old-draw/update/new-draw pixels, tiny/padded and full-size
  textures, repeated updates, OOM, redefine/delete, and the unchanged direct
  full-upload timing before this can be called end-to-end complete.

- 2026-09-03: Plan created. Implementation started (Phase 1).
- 2026-09-03: Phase 2 done — dispatch ABI v5 (`MINIGL_DISPATCH_ABI_VERSION 5UL`);
  `GLInterleavedArrays` + `MGLEmergencyCloseScreen` appended; guarded client
  wrappers added; packaged `dev/include` copy synced byte-identical.
- 2026-09-03: Phase 3 done — R200 backend:
  - `Lib_glInterleavedArrays()` in r200_geometry.c (six V19 layouts, state
    replacement, packed-stride handling, INVALID_ENUM/INVALID_VALUE paths,
    null-pointer refusal, normal array cleared)
  - context-owned mtex queue (`struct MtexPolygon` + lazy AllocMem, capacity
    from `mglChooseMtexBufferSize` via verts/8, V19 silent-drop on overflow,
    flush = unit-0 pass then unit-1 pass with requested blend/env,
    GL_LEQUAL + depth-write-off equal-depth safety, full state save/restore);
    capture hook in `Lib_glEnd()` for GL_POLYGON + unit-1-enabled
    (3-4 vertex polygons; larger fans take the normal path)
  - `Lib_mglEmergencyCloseScreen()` in minigl_r200.c — full safe teardown
    under the library lock, clears `mini_CurrentContext`, idempotent
  - `Lib_mglGetSupportedScreenModes()` — V19 return semantics (picked mode
    ID / MGL_SM_BESTMODE, null-callback guard)
  - `Lib_glHint()` — accepts GL_FOG_HINT / GL_PERSPECTIVE_CORRECTION_HINT
    as advisory no-ops (no Radeon3D quality lever), keeps perf-counter hint
  - minigl_r200.h: added V19 enum values (GL_V3F=204, GL_C4UB_V3F=21,
    GL_C3F_V3F=19, GL_T2F_V3F=172, GL_T2F_C4UB_V3F=171, GL_T2F_C3F_V3F=170,
    GL_DECAL=36, GL_FOG_HINT=65, GL_PERSPECTIVE_CORRECTION_HINT=130,
    MGL_SM_BESTMODE=0xFFFFFFFF)
- 2026-09-03: Phase 4 done — stub table has logging stubs + explicit 18
  NULL lighting placeholders (positional alignment); classic/pistorm3d/PPC
  leave v5 tail NULL (guarded wrappers refuse).
- 2026-09-03: Phase 5 done — `dispatchtable` regenerated:
  **152 fields, 152 bound, 0 unsupported**. Stub, classic, R200 all build
  clean (`all importlib testprograms`), `nm -u` empty on all three
  libraries. New tests `bin/v19_parity_r200_gcc`,
  `bin/v19_emergency_r200_gcc` build and link clean.
- Pending: Phase 1 hardware run, Phase 6 physical verification matrix,
  Phase 7 packaging sync (import-lib release copies) + remaining doc
  sweeps; known limitation: GL_POLYGON fans >4 vertices with unit 1
  enabled take the normal single-pass path instead of the queue.
- 2026-09-03: **Hardware session 1 (physical Amiga 192.168.1.21:2345,
  68060/RV280, windowed)** — Phases 1, 5 and the core of Phase 6 done.
  Fixes found on hardware:
  - T2F_C3F_V3F vertex pointer was `+8*sizeof(GLfloat)` (byte 32) instead
    of V19's byte 20 — garbage vertices, silent no-output. Fixed.
  - Test bugs fixed: degenerate fill data for 5 of 7 interleaved layouts;
    little-endian 32bpp readback mask (B8G8R8A8 on big-endian m68k reads
    `B<<24|G<<16|R<<8|A`, so a correct black clear reads 0x000000FF).
  Results:
  - `v19_parity`: **31/31 PASS at 16 bpp and at 32 bpp** (abi=5).
  - `v19_emergency`: **6/6 PASS at 16 bpp and 32 bpp** (idempotent no-op,
    terminal close, safe no-op delete, fresh context + render after).
  - Regression suite v5: bootstrap PASS, phase4 PASS, phase5 PASS,
    phase6 primitives PASS, phase6 acceptance PASS (deferred state,
    arrays, fastpath v2, move/resize, batch budget, async), cull/shade
    PASS, phaseE PASS, quad_list PASS, perspective precision PASS.
  - phase7 lighting "directional light changes shading (code 40)": FAILED
    **identically on the v4 baseline** (A/B with LIBS:minigl.library.
    pre-v5-parity + the old v4 client) — pre-existing, not a v5
    regression. Resolved on 2026-09-04; see hardware session 4 below.
  - blend_alpha FAILBITS=00000386: bit-identical on the v4 baseline —
    pre-existing diagnostic state.
  - Library installed as LIBS:minigl.library (CRC verified each push,
    Avail Flush after every swap); snapshots kept:
    minigl.library.pre-v5-parity (v4), minigl.library.pre-v5-fix1 (v5
    pre voff fix). Test clients live in Work:minigl_testing/ per the
    AGENTS.md deployment convention.
- 2026-09-03: **Hardware session 2 (same target, windowed + fullscreen
  matrix)**:
  - `v19_parity` now honours `--fullscreen` and `-buffers N`; probe got
    the same flags plus raw centre-pixel reporting.
  - Parity **31/31 fullscreen at 16 and 32 bpp**; initially 9 failures at
    8 bpp were the readback having no CLUT8 branch (fixed; p6acc at
    8 bpp was already passing and proved rendering worked). Parity
    **31/31 fullscreen at 8 bpp** after the fix.
  - `mglChooseNumberOfBuffers()` is now real (was store-only):
    clamps 1..3, `CreateFullscreenTarget` allocates N screen buffers,
    depth-storage avoidance count follows N, and the present rotation
    formula generalises. `fs=ok buffers=` debug now prints the real
    count. Parity 31/31 fullscreen at buffers 1/2/3.
  - **2-buffer FLIP blink bug (introduced + fixed same session)**: with
    two buffers the generalised sum rotation resolved to the just-flipped
    buffer, rendering into the displayed surface — visible blink every
    second frame (operator-confirmed). Fix: N=2 renders into the buffer
    that retires when the switch completes (the currently displayed
    buffer); safe-mask wait gates rendering. Operator confirmed: no
    blinking after the fix.
  - **Known issue (pre-existing, deferred — see TODO below): 3-buffer
    fullscreen shows a black screen** in BOTH present modes (default
    blit and MINIGL_PRESENT=FLIP) at 640x480x16 on this setup, while the
    client itself runs all frames and exits cleanly. A/B-verified on the
    pre-v5-buffers library snapshot — not a v5/numberOfBuffers
    regression. 2-buffer fullscreen is correct in both modes; 1-buffer
    tears by design (renders into the visible bitmap; diagnostic mode,
    matches MINIGL_BUFFERS=1 semantics).
  - Environment override MINIGL_PRESENT removed after testing (UnSetEnv).
  - **Gears perf baseline (windowed 800x600@32, envmapped, 300 frames,
    -benchmark)**: current v5 library **9455 fps_x1000** vs pre-session-2
    snapshot 9411 (+0.5%, within noise) — **no perf regression** from the
    v19-parity/buffers work.
  - Final regression on the shipped library: parity 31/31 (windowed 16),
    emergency 6/6. Library installed as LIBS:minigl.library
    (CRC e6f423aa/22d135dd/ba67d110 lineage; snapshots pre-v5-buffers and
    pre-v5-fix1 retained for A/B).
- 2026-09-03: **Phase 7 packaging/doc sweep done**:
  - Packaged include tree (`dev/include/`) verified byte-identical to the
    source tree (dispatch header + all mgl/ libraries/ headers).
  - Packaged import libraries (`dev/lib/libminigl.a`,
    `dev/lib/libminigl_dispatch.a`) re-synced from the current build
    (sha256-verified); stub and classic rebuilt clean against ABI v5 as a
    shared-header sanity check.
  - `developer/PORTING.md`: ABI v5 additions documented (v5 wrapper
    guarding, callable-surface list).
  - Nested `AGENTS.md`: v19 parity/emergency/probe clients added to the
    hardware-test table with their flags, plus the hardware-validated
    status summary and Work:minigl_testing deployment convention.
  - Root `bin/minigl.library` remains the packaged CLASSIC v10 library
    (ABI v5 table with NULL v5 vectors — consistent); the hardware-
    validated R200 library lives in
    `bin/dispatch-r200-gcc/minigl.library`.
- Pending: four-Ogre/Quake Phase-0 perf loop (requires the BlitzQuake
  tree build; see AGENTS.md Quake section), deferred 3-buffer fullscreen
  investigation (TODO above).
- Pending: gears + four-Ogre perf baselines, Phase 7 packaging sync,
  and the deferred 3-buffer fullscreen investigation below.
- 2026-09-03 (hardware session 3): deferred 3-buffer fullscreen black
  screen root-caused and FIXED — flip path's sync `CompletePendingFlip`
  clobbered the three-buffer rotation target (`drawingBuffer = retired`
  is correct only for N=2); fix re-applies the rotation formula after
  the sync wait. Operator-confirmed live (rotating triangle, flip mode,
  3 buffers), trace cycles drawn 1,2,0 / disp 0,1,2 with zero
  ChangeScreenBuffer failures. Regression matrix on the fix build
  (crc32 51699751, snapshot `minigl.library.v5-flipfix`): parity 31/31
  fullscreen 16bpp buffers 1/2/3, fullscreen 32bpp buffers 3, windowed
  16/32. New `v19_tri_buffers` long-run visual client added
  (`examples/v19_tri_buffers.c`). Also this session: reconstructed the
  `MGL_PERF_WORST_*` perf-header section lost in the SDK include sync
  (fresh collision-free values, worst ranges answered 0 by this
  backend's `Lib_glGetIntegerv`), rebuilt BlitzQuake against the v5
  importlib. Flip   present remains slow by design (~1-2 vblank waits per
  present); BLIT is the fast path for all buffer counts.
- 2026-09-03: `minigl_testsuite` deployed (`Work:minigl_testing/testsuite`,
  crc32 2ba46dee, built from `examples/minigl_testsuite.c`) and run
  non-interactively (`--run-all --frames 60 --depth 16`, log `T:ts.log`):
  **24/24 PASS, 0 failed, 0 skipped** on the flip-fix build — including the
  full Quake workload family. Four-Ogre perf baseline (512x320x16 windowed,
  60 measured frames): single ogre 5.05 fps, x4-static 3.86, x4-spin 3.85,
  x4-advance 3.85, world-stream (3072 verts/frame) 4.27, lightmap
  static/update 5.53/5.20, rotating cubes 5.26, simple primitives
  5.5-5.6, present.stability 10 presents 0 drift. Overall ~5.5 fps is the
  68k-client pipeline throughput at this resolution, consistent with the
  gears 9.5 fps fullscreen reference.
- 2026-09-03: **16bpp windowed slowness root-caused** (testsuite A/B):
  windowed 512x320x16 runs at a uniform ~5.5 fps floor (single triangle
  5.6 fps, cubes 5.26, x4 ogres 3.85) while the same tests at 32bpp run
  86-241 fps (arrays 240.8, triangle 227.9, cubes 86.1, single ogre
  43.7, x4 ogres 12.1, world-stream 16.9, room-walk 8.1, fog 177.7;
  suite 24/24 PASS at 32bpp too). `--profile` attribution: present
  ticks 745082 (16bpp) vs 80886 (32bpp) — 9x present cost. Cause: the
  Workbench screen is 32-bit; a 16bpp window ClipBlit forces P96's
  per-pixel depth conversion on the 68060 (~180 ms/frame), while 32bpp
  blits are same-depth fast paths. Fullscreen is unaffected (custom
  screen = context depth). Operator's historical figures (cubes ~60,
  ogres ~20, walk ~15) match the 32bpp class. Fix candidates: match the
  window surface depth to the screen depth in the windowed path, or a
  Radeon3D GPU blit for depth-mismatched window presents; meanwhile
  windowed clients should use `-depth 32` on this configuration.
- 2026-09-04: **Hardware session 4: phase7 lighting fixed** on the physical
  68060/RV280. MiniGL's submitted 133-dword record was verified correct
  (`options=0x7700`, HW TCL + normals + lighting + compact vertex, one enabled
  light). The driver configured the lighting engine but omitted
  `R200_OUTPUT_COLOR_0` from `R200_SE_TCL_OUTPUT_VTX_COMP_SEL`, so TCL's lit
  primary colour was discarded and rasterization reused the unlit input
  colour. `radeon3d_emit.c` now selects colour 0 in the lighting branch, matching
  Mesa classic R200. `litprobe` changed from `lit=ffff same_as_unlit=1` to
  `lit=79ce same_as_unlit=0`; phase7 passes directional, deferred split,
  colour material, attenuation, spotlight, normal arrays, queries and FastPath.
  The phase7 client was corrected to disable `GL_LIGHTING` for its true unlit
  comparison and to initialize/check FastPath according to its boolean return
  contract. The R200 backend also now answers `GL_MAX_LIGHTS` and rejects a NULL
  normal-array pointer as required by this backend's test contract. Installed
  artifacts: `Radeon9200.chip` CRC `07A7193D`, `Prometheus.card` CRC
  `18A453D6`, `minigl.library` CRC `41A59718`. Regression: bootstrap,
  phase4/5/6/6-acceptance, cull/shade, phaseE, quad-list, perspective and V19
  parity 31/31 pass; testsuite at 32bpp finishes 24/24 PASS, including single
  and four-Ogre workloads.

## Deferred / TODO

- ~~3-buffer fullscreen black screen~~ **ROOT-CAUSED AND FIXED 2026-09-03
  (hardware session 3).** The present path computed the three-buffer
  rotation (drawingBuffer = N(N-1)/2 - displayed - drawn) but the
  immediately following sync CompletePendingFlip() overwrote
  drawingBuffer with the retired buffer (correct for N=2, destructive
  for N=3), degenerating the rotation to a 1<->0 ping-pong in which
  buffer 2 was never used. Fix: save the pre-wait displayedBuffer and
  re-apply the formula after the sync wait (r200_surface.c, flip path).
  Verified live: flip drawn cycles 1,2,0 / disp cycles 0,1,2, every
  ChangeScreenBuffer succeeds, no cpf timeouts. Remaining 3-buffer
  notes: the flip path is inherently slow (~1-2 vblank waits per
  present); MINIGL_PRESENT=BLIT displays 3 buffers correctly and fast —
  confirmed on hardware with the new tri_buffers client. The bridge
  screenshot tool reads a stale/wrong bitmap for fullscreen custom
  screens (uniform gray) — operator eyes are the display oracle.
- **1-buffer fullscreen artifacts (inherent, operator-confirmed x2)**:
  rendering straight into the visible bitmap shows "missing horizontal
  lines" bands (render racing scanout), not classic tearing. A vblank
  pin was added to the single-buffer present (WaitTOF before each
  frame; library crc32 5dba9d41) — artifact persists; operator then
  reproduced the same banding with the gears benchmark fullscreen at
  `-buffers 1` (even stronger), confirming it is the mode's physics,
  not a client bug. Decision (operator): leave as is. True
  single-buffering cannot be artifact-free; visual clients should use
  2+ buffers. `gears_benchmark` gained a `-buffers 1|2|3` flag for such
  demonstrations (default unchanged: no call = validated default 3).
- Slope-scaled glPolygonOffset (excluded from parity scope; V19 never
  implemented it either).
- glInterleavedArrays fans >4 vertices with unit 1 enabled take the
  normal single-pass path instead of the mtex queue (documented
  limitation).
- SDK lineage reconciliation: `PERFORMANCE.md`, `OPTIMIZATION_PLAN.md` and
  `GLITCH_HUNT_FINDINGS.md` are now migrated into this tree (2026-09-03).
  Still unported from `PiStorm_SDK_v10`: the worst-frame attribution
  machinery (`WorstFrameSlot` tracking in `minigl_r200_internal.h` +
  `r200_state.c` query ranges — this backend answers 0) and the
  quake-tuned objdirs (`obj_gcc_dispatch_r200_quake[2]`, reproducible
  build artifacts). Port or drop: a design decision, not data loss —
  the SDK tree remains on disk as the source.
