# Plan: bring the V19-based WarpOS/R200 MiniGL to V29.1 feature parity

Status (2026-10-21, branch `v29-parity`, uncommitted): **P0-P5 implemented
and building; no hardware validation yet.** See "Implementation status" at
the end.

This is a working plan, not documentation. Under `AGENTS.md`, each phase
moves its durable facts into `docs/` in the same change set. When the work
is complete, delete this file; git history keeps it.

Reference tree: `~/MiniGL_Library_68k` (upstream, HEAD `d950a3a`
"Update to V29.1 Part 4").
**Authoritative 29.1 headers are `libraryroot/include/`.** The top-level
`~/MiniGL_Library_68k/include/` is a stale V14/V25 copy (`MINIGL_VERSION 14`,
158 fields) and must not be used as the reference.

---

## 0. Decisions already taken (user, 2026-10-21)

| # | Decision |
|---|---|
| D1 | **Match the 29.1 dispatch layout exactly**: slots 1–205 in 29.1 order, `MINIGL_DISPATCH_ABI_VERSION 5`, OpenGL token values in `mgl/gl.h`. Our private extensions are appended **after** slot 205. |
| D2 | Scope: **the whole API surface, plus real implementations of core GL** (blend equation/separate, lighting/material, normals, display lists, texenv colour, copy-tex, line width, GLU mipmaps/quadrics). GLUT and the GL 1.1 odds and ends start as documented fallbacks or a thin layer, then get implemented in phases. |
| D4 | `glqfinal` and ioq3 are **rebuilt against ABI 5 later**, as a follow-up after the new DLL is stable. Breaking them in the meantime is accepted, and no legacy ABI-3 table is needed for them. |
| D3 | `MGLCreateContextFromWindow` / `FromBitMap`: **study the Classic implementation and implement it for R200 where that is possible**. Where the target cannot be rendered to directly, use a documented fallback (failure or a copy path). This lifts the `AGENTS.md` prohibition, and `AGENTS.md` must be updated in the same change. |

Consequences of D1 that the user accepted when choosing it:
- Every existing ABI-3 PPC client is refused by the equality check in
  `MiniGLOpen` until it is rebuilt. That includes `glqfinal`, ioq3, the
  `bin/examples/*`, and possibly `SilentHill.elf` (see open question O1).
- FastPath moves from slots 135–137 to slots 206–208.

---

## 1. Gap analysis (measured from the headers, not estimated)

Field *i* sits at offset `20 + (i-1)*4`, after the 20-byte header
(`abiVersion, structSize, backendFlags, reserved, currentContext`).
Slots 1–132 (through `GLMultiDrawArrays`) are **name-for-name identical** in
both trees (checked by diff).

| | This repo (V19, ABI 3) | Upstream 29.1 (ABI 5) |
|---|---|---|
| Function slots | 137 | 205 |
| `sizeof(MGLDispatchTable)` | `0x238` | `0x348` |
| Slots 133–137 | `MGLEmergencyCloseScreen`, `MGLForceNewRenderPass`, `mglBeginFastPath`, `mglEndFastPath`, `mglGetFastPathCapabilities` | `GLBlendEquation`, `GLBlendFuncSeparate`, `GLIsTexture`, `mglChooseZBufferDepth`, `GLLineWidth` |
| GL token values | auto-numbered `enum` (for example `GL_TRIANGLES` = 191) | real OpenGL values (`GL_TRIANGLES 0x0004`) |
| `MINIGL_VERSION` | 19 | 29 (library 29.1) |
| FastPath | present | removed upstream |

### 1.1 Target layout

| Slot | Offset | Field | R200 plan (phase) |
|---|---|---|---|
| 133 | 0x224 | GLBlendEquation | real, needs a driver record bit (P6) |
| 134 | 0x228 | GLBlendFuncSeparate | real, needs a driver record field (P6) |
| 135 | 0x22C | GLIsTexture | real, PPC only (P4) |
| 136 | 0x230 | mglChooseZBufferDepth | 16 now; 24 after host/library work (P6) |
| 137 | 0x234 | GLLineWidth | real, PPC only (P4) |
| 138 | 0x238 | GLTexGenfv | real; object/eye-linear planes (P6) |
| 139–140 | 0x23C–0x240 | GLCopyTexImage2D, GLCopyTexSubImage2D | readback-and-upload first, GPU copy later (P6) |
| 141 | 0x244 | GLAreTexturesResident | real (residency query) (P4) |
| 142–144 | 0x248–0x250 | GLEdgeFlag, GLEdgeFlagPointer, GLEdgeFlagv | state, honoured in GL_LINE polygon mode (P4) |
| 145 | 0x254 | GLGetDoublev | wraps GetFloatv (P4) |
| 146 | 0x258 | GLGetPointerv | real, array pointers (P4) |
| 147–149 | 0x25C–0x264 | GLIndexi, GLIndexiv, GLIndexPointer | documented fallback: no colour-index mode (P3) |
| 150 | 0x268 | GLPrioritizeTextures | stored hint only (P4) |
| 151 | 0x26C | GLReadBuffer | real, selects front/back for ReadPixels and CopyTex (P4) |
| 152 | 0x270 | MGLCreateContextFromWindow | real where possible, otherwise NULL (P7) |
| 153 | 0x274 | MGLCreateContextFromBitMap | real where possible, otherwise NULL (P7) |
| 154–161 | 0x278–0x294 | GLLightf/fv, GLMaterialf/fv, GLLightModelf/fv, GLGetLightfv, GLGetMaterialfv | **already in the frontend**, needs exposing (P4) |
| 162–167 | 0x298–0x2AC | GLGenLists, GLDeleteLists, GLIsList, GLNewList, GLEndList, GLCallList | new generic layer (P5) |
| 168–178 | 0x2B0–0x2D8 | GLUBuild2DMipmaps, GLUErrorString, GLU quadric API (New/Delete/Normals/Texture/DrawStyle/Orientation/Cylinder/Sphere/Disk) | new generic layer (P5) |
| 179–200 | 0x2DC–0x330 | the 22 GLUT entries | new generic layer over MGLMainLoop (P5) |
| 201 | 0x334 | GLNormalPointer | **already in the frontend** (P4) |
| 202 | 0x338 | GLUQuadricCallback | generic layer (P5) |
| 203 | 0x33C | GLColorMaterial | **already in the frontend** (P4) |
| 204 | 0x340 | GLTexEnvfv | needs GL_TEXTURE_ENV_COLOR; the frontend currently truncates to int (P4/P6) |
| 205 | 0x344 | GLNormal3fv | thin wrapper over Lib_glNormal3f (P4) |
| 206–208 | 0x348–0x350 | mglBeginFastPath, mglEndFastPath, mglGetFastPathCapabilities | **our extension**, moved here |

New `sizeof(MGLDispatchTable)` = **`0x354`** (852 bytes).
`MGLEmergencyCloseScreen` and `MGLForceNewRenderPass` were no-op slots and
upstream removed them, so they lose their slots. Their client macros remain
as inline no-ops to keep source compatibility.

### 1.2 What 29.1 really implements, and why we cannot port it

In upstream `libraryroot/library/dispatch_table_classic.c` nearly every new
slot is a `ClassicStub_*`, either a no-op or a `ClassicUnsupported()` that
sets an error. The real implementations live in the PiStorm3D V3D backend,
and its source is **not** in the reference tree. For R200, every
"real" item in this plan is new work. The 29.1 source contributes the
header/ABI shape, the documented semantics, the Classic Warp3D
FromWindow/FromBitMap code (§P7), and the mglQ3 bug fixes (§P2.3).

### 1.3 Frontend facts that set the cost

- `backend_r200/library/r200_state.c` already has `Lib_glLightf/fv/i/iv`,
  `Lib_glMaterial*`, `Lib_glLightModel*`, `Lib_glGetLight*`,
  `Lib_glGetMaterial*`, `Lib_glColorMaterial`, and
  `Lib_glNormalPointer`/`Lib_glNormal3f`. Driver caps include
  `HW_LIGHTING` and `HW_NORMALS`. **These are reachable through the 68k
  table but not through the PPC ABI.** Exposing them costs little; correctness
  still needs hardware validation.
- Blend reaches the hardware through `RADEON3D_FRAGMENT_*` bits
  (`radeon3d_records.h`). `FRAGMENT_STATE_MASK` is `0x01ffffff`, so bits
  25–31 are free. The emitter (`third_party/radeon3d/emit/src/radeon3d_emit.c`,
  around lines 908, 1115 and 1878) builds `R200_RB3D_BLENDCNTL`. This emitter
  is shared by the PPC `CP_EMIT` path and the host.
- Lines and polygon-mode edges are screen-space quads with a fixed width
  (`AppendScreenSegment`, `±0.5`). That makes `glLineWidth` a PPC-only change.
- The records already define `RADEON3D_TEXGEN_MODE_OBJECT_LINEAR`.
  `Lib_glTexGeni` currently accepts only `GL_SPHERE_MAP`.
- **GL enum values cross the PPC↔68k wire.** The host is compiled against
  the 68k library's `minigl_r200.h` (`GL_TRIANGLES 191U`). Enums carried by
  `GET_INTEGER`, texture format/type and error codes therefore use the
  internal numbering today. See P2.2.
- The window is created by the **68k** `minigl.library` during
  `CREATE_CONTEXT`. The host polls that window's `UserPort` for
  `HOST_EVENTS` (`PollContextEvents`).

---

## 2. Phases

Each phase ends at a buildable checkpoint, and its docs are updated in the
same commit. Hardware validation follows the `AGENTS.md` rules (cold boot,
CRC32 verification, serialized bridge, `InitPPC` once). Effort marks are
**estimates**: S ≈ under a day, M ≈ a few days, L ≈ a week or more.

### P0 — Baseline and tooling (S)

1. Record the current known-good stack: commit, DLL/host/library/driver-pair
   CRCs, and gears/Q3 FPS medians from `docs/performance.md`. This is the
   reference for regressions.
2. Add `tools/check_dispatch_layout.py` (host-side, no hardware). It parses
   our `include/libraries/minigl_dispatch.h` and upstream
   `libraryroot/include/libraries/minigl_dispatch.h`, then asserts that
   slots 1–205 match by name and order, that 206–208 are the FastPath trio,
   and that ABI = 5. Run it from the R200 makefile like the host classifier
   check.
3. Extend `tools/generate_dispatch_r200.py` so it takes the slot list from
   the header instead of a hand-kept list. Rename the output from
   `generated/dispatch_table_abi3.c` to `dispatch_table_abi5.c`.

Acceptance: the script passes against upstream and fails against the
current header (which proves it detects the conflict).

### P1 — Public headers and ABI 5 (M)

Files: `include/libraries/minigl_dispatch.h`, `include/libraries/minigl.h`,
`include/mgl/gl.h`, `include/mgl/glut.h` (new), `include/mgl/mglmacros.h`,
`include/mgl/minigl.h`, `include/mgl/context.h`,
`include/libraries/minigl_fastpath.h`, `client/minigl_ppc_open.c`.

1. Take the struct, wrappers, GLU/GLUT prototypes and `extern "C"` guards
   from upstream. Keep the PPC-specific parts: `MINIGL_BACKEND_FLAG_PPC/R200`,
   the `MINIGL_PPC_DLLNAME`/port names, the `dllGetProcAddress` notes and
   the FastPath header. Make a targeted merge, **not** a bulk sync. Per
   `AGENTS.md`, `include/mgl/` and `mglQ3/include/mgl/` stay distinct.
2. `gl.h`: replace the auto-numbered `enum` with upstream's `#define`
   OpenGL values. Remove the `GL_TEXTURE0_ARB` special-case comment, which
   is now moot.
3. Set `MINIGL_DISPATCH_ABI_VERSION 5`, `MINIGL_VERSION 29`, and add the
   `structSize` static assert at `0x354`.
4. Keep `MiniGLOpen`'s equality check on the ABI and `>=` on `structSize`,
   matching upstream `minigl_open.c`.
5. Keep `mglEmergencyCloseScreen()` and `glMGLForceNewRenderPass()` as
   source-level no-op macros.

Acceptance: `make -f Makefile.import.gcc` builds the client archives, and
`lib/libminigl.a` is still byte-identical to `lib/libminigl_dispatch_ppc.a`.
The layout check passes.

### P2 — Token migration (M–L, highest regression risk)

#### P2.1 R200 frontend

`backend_r200/library/minigl_r200.h` defines its own GL values (191-style).
Switch it to OpenGL values. That also aligns it with Mesa r200, which
`AGENTS.md` makes the reference.

- Audit every place that assumes small contiguous enum values: arrays
  indexed by `enum - GL_X`, range checks (`unit - GL_TEXTURE0_ARB`,
  `GL_LIGHT0 + n`), bit packing of enums into record fields, and lookup
  tables. `grep -n "\- *GL_\|GL_[A-Z0-9_]* *+\|\[GL_"` across
  `backend_r200/library` is the starting list.
- OpenGL values are sparse, so `switch` statements get larger. The build uses
  `-fno-jump-tables`, so they become compare chains. Re-benchmark the hot
  paths (`glBegin` mode, `glEnable`, `glBindTexture`, `glTexEnv`) before and
  after.
- `GL_R200_PRIVATE_*`, `GL_PERF_*`, `GL_LM_CAPACITY`, `MGL_*` and other
  private tokens keep values that cannot collide; choose them from a
  documented private range.

#### P2.2 Wire translation (keeps host and 68k library unchanged for now)

Add one small table in the PPC platform layer
(`minigl_r200_warpos_compile_probe.c` / transport) that maps the few enums
crossing the wire (`GET_INTEGER` names, texture format/type, error codes,
`GL_UBYTE_ARGB` and similar) between OpenGL values and the 68k library's
numbering. Protocol 16 stays unchanged. Move the wire to OpenGL values only
when P8 migrates the 68k library, and bump the protocol to 17 when that
happens.

#### P2.3 Bundled mglQ3 backend (`mglQ3/`, the Warp3D variant of the DLL)

Merge upstream 29.1 mglQ3 changes file by file, not by overwrite. Our tree
is the WarpOS build, so `sysinc.h` and the 68k-only startup/inline changes
must not be copied blindly. Measured diffs (whitespace-insensitive) and their
content:

- `texture.c`: QuarkTex-safe colour pointer for `W3D_SetTexEnv`/`SetWrapMode`,
  removal of the `CurTexEnv` early-out after a bind, and the
  `GL_TEXTURE0_ARB` range check. About 25 lines once line endings are
  ignored. Most of the apparent 3811-line diff is CRLF.
- `others.c`: `ConfiguredMaxTextureSize` (an env override), both blend
  factors always submitted, the `(DST_COLOR, ZERO)` equivalence, and
  ReadPixels finishing before it reads the real back bitmap.
- `vertexarray.c`, `vertexelements.c`, `vertexbuffer.c`: a clipped-triangle
  flush before scratch reuse, a seven-plane append bound, removed SDK modes
  that must not reach the default fan path, and NULL-vector guards.
- `draw.c`: device-coordinate flat path removed.
- `context.c`: `MGLCreateContextFromWindow` and `MGLCreateContextFromBitMap`
  over Warp3D (P7 reuses these directly for the mglQ3 DLL).
- `matrix.c`: 10 lines.
- Headers `gl.h`, `mglmacros.h`, `minigl.h`, `glut.h`: already OpenGL
  values upstream. These are the mglQ3 build's own headers.

Also the stub DLL (`ppc_dll/minigl_ppc_dispatch_stub.c`): convert it to the
new layout with logging stubs, as upstream `dispatch_table_stub.c` does.

Acceptance: all three DLL variants build. Gears, the texture probes and Q3
render identically on hardware compared with the P0 baseline (screenshots),
and FPS stays within run-to-run noise (three cold-boot runs, median).

### P3 — Wire every slot (S–M)

Fill all 208 slots in all three initializers (R200 generated, mglQ3
`ppc_dll/minigl_ppc_dispatch.c`, stub). Where P4–P7 have not landed yet,
use explicit documented fallbacks that match upstream Classic semantics:
a no-op or `GL_INVALID_OPERATION` through `LibUnsupported()`, `NULL` from
FromWindow/FromBitMap, `GL_FALSE` from `IsList`/`IsTexture`. No slot may be
NULL. `GLIndex*` stay fallbacks permanently, because there is no
colour-index mode.

Acceptance: the layout check passes, a PPC test client calls every slot once
without a crash (`bin/examples/abi5_smoke`), and `glGetError` returns the
expected codes.

### P4 — PPC-only real implementations, no protocol change (M)

| Feature | Work |
|---|---|
| Lighting/material/light-model (154–161), ColorMaterial (203), NormalPointer (201), Normal3fv (205) | Thunks to the existing `Lib_*`. Verify the arithmetic against Mesa 7.11.2 `t_vb_light.c` and `r200_state.c`. A probe compares lit vertex colours with Mesa-derived expectations, not hand-derived ones. |
| GLIsTexture, AreTexturesResident, PrioritizeTextures | Texture-object table lookup. Residency comes from the host's resident state, which is already tracked for direct-park. Priority is stored and unused. |
| GLGetDoublev, GLGetPointerv | Wrap `Lib_glGetFloatv`, and return the vertex/colour/texcoord/normal/edge-flag array pointers. |
| GLEdgeFlag / v / Pointer | Current-flag and array state, honoured in `AppendScreenTriangleEdges` for `GL_LINE` polygon mode. |
| GLLineWidth | Width state with clamping, used by `AppendScreenSegment` and the `GL_LINES`/`LINE_STRIP` paths. Expose `GL_LINE_WIDTH` and `GL_LINE_WIDTH_RANGE` through Get*. |
| GLReadBuffer | `GL_FRONT`/`GL_BACK` state that `Lib_glReadPixels` and CopyTex consume. |
| GLTexEnvfv | Proper float path. `GL_TEXTURE_ENV_COLOR` is stored here and becomes visible in P6. |
| mglChooseZBufferDepth | Accept 16; store 24/32 for P6 and report the effective depth through `GL_DEPTH_BITS`. |

Acceptance: hardware probes for lighting (compared with Mesa), line width,
edge flags, and Get* round trips. Gears FPS unchanged, because lighting is
off on the hot path.

### P5 — Backend-agnostic layers: display lists, GLU, GLUT (L)

Put these in new shared code, for example `ppc_common/`, written only
against the dispatch table's own `R200_*`/`Lib_*` entry points. That way
the R200, mglQ3 and stub DLLs all link the same objects.

**Display lists (162–167).**
- Recording approach: the table is positional, and clients re-read
  `MiniGLDispatch->X` on every call. On `glNewList`, the DLL swaps the
  compilable slots in its own (writable) table for recording thunks, and
  restores them on `glEndList`. This costs nothing on the hot path outside
  compilation. `GL_COMPILE_AND_EXECUTE` thunks record and then forward.
- Follow the GL 1.1 rules for what is not compiled: Gen/Delete/IsList, every
  Get*, ReadPixels, PixelStore, Flush/Finish, client state and array
  pointers. Draw calls from arrays are dereferenced into immediate vertices
  at compile time. Texture uploads inside lists copy their data.
- Storage: chunked command buffers in DLL-owned memory, freed by
  DeleteLists and at context destroy. Nested `glCallList` has a depth limit
  of 64 (`GL_MAX_LIST_NESTING`).
- Later optimization: compile a vertex-only list into a cached vertex
  segment for the R200 path. Measure first.

**GLU (168–178, 202).**
- `gluBuild2DMipmaps`: scale to a power of two, box-filter each level,
  and call `TexImage2D` per level. Respect the frontend's mip limits.
- `gluErrorString`: a string table.
- Quadrics: port the SGI libGLU `quad.c` (SGI Free License B; check
  compatibility with the Hyperion MiniGL license before importing). Its
  output goes through the dispatch immediate mode, or through display lists
  if one is compiling.
- `GLUquadricObj` layout must match upstream `mgl/gl.h`, because clients
  may allocate it opaquely only. Verify this.

**GLUT (179–200).**
- Callbacks are PPC→PPC, so they are safe here, unlike in a 68k library
  calling back into PPC.
- Build on the existing `MGLCreateContext`, `MGLMainLoop`, `MGLKeyFunc`,
  `MGLIdleFunc` and `MGLSwitchDisplay`. `glutCreateWindow` maps to
  `mglChooseWindowMode(TRUE)` + `mglCreateContext`. Game mode parses
  `"WxH:bpp"` into `mglChoosePixelDepth` and a fullscreen context.
- `glutGet` uses the upstream `glut.h` enum set, including
  `GLUT_WINDOW_CURSOR`.
- `PostRedisplay` sets a flag that the main loop services. `Reshape` is
  called once at create, because the window size is fixed while
  `MGLResizeContext` stays a no-op.
- Solid shapes: the cube is built in, sphere and cone use quadrics, and the
  torus and dodecahedron come from freeglut geometry (MIT/X licence).
- Input comes through the existing `HOST_EVENTS` ring.

Acceptance:
- PPC builds of the upstream examples that use lists, GLU and GLUT (port
  `libraryroot/examples/` where applicable, plus a glxsglut-style test).
- A display-list stress test: 1000 lists, nesting, delete during use.
- A memory check after context destroy (`Avail` before and after).

### P6 — Real features that need the driver or protocol (L)

Each change to the driver or service follows the p96-driver rules: matched
pair install, `.previous` backups, cold reboot, an interface bump in
`docs/02-service-abi-reference.md`, and reconciled consumer headers under
`third_party/radeon3d/include/`.

1. **BlendEquation / BlendFuncSeparate.**
   - Use fragment bits 25–27 for the colour equation
     (ADD/SUBTRACT/REVERSE_SUBTRACT/MIN/MAX).
   - Separate alpha factors and equation do not fit, so they need a new
     record word. The emitter writes `R200_RB3D_BLENDCNTL` plus
     `R200_RB3D_ABLENDCNTL`/`CBLENDCNTL` with `R200_SEPARATE_ALPHA_ENABLE`.
     Check against Mesa `r200_state.c` (`r200_set_blend_state`). MIN/MAX
     ignore the factors.
   - Gate on a new Radeon3D capability bit, and fall back to ADD with
     `GL_INVALID_OPERATION` when it is absent.
   - Confirm the service's register-pattern validation
     (`radeon3d_service.c` `MatchRegister` sequences) accepts the new
     values.
2. **TexGenfv object/eye linear.** `RADEON3D_TEXGEN_MODE_OBJECT_LINEAR`
   exists. Check whether the plane coefficients are carried. Add eye-linear
   (with the planes transformed by the inverse modelview at specification
   time, as `t_vb_texgen.c` does). Validate against Mesa.
3. **GL_TEXTURE_ENV_COLOR.** Add `PP_TFACTOR_n` to the texture-stage
   record, needed for `GL_BLEND` texenv.
4. **CopyTexImage2D / CopyTexSubImage2D.**
   - Stage A: flush, then `READ_PIXELS_SHARED` (existing cap bit 10) from
     the `glReadBuffer` target, then the existing TexImage/TexSubImage
     upload path. Correct but slow. Document the measured cost in
     `docs/textures.md`.
   - Stage B, only if a client needs it: a GPU colour-buffer→texture copy
     as a new service operation.
5. **24-bit depth.** Pass the chosen depth in `CREATE_CONTEXT`
   `value[]`, and have the 68k library allocate Z24 (R200 supports a 24-bit
   Z buffer with 8-bit stencil). Bump the protocol to 17 if a value word
   changes meaning.

Acceptance: a hardware probe per feature against Mesa-derived
expectations, the regress script (`tools/amiga-regress.script`) extended,
and no FPS regression on gears/Q3.

### P7 — `MGLCreateContextFromWindow` / `FromBitMap` (M–L)

Classic 29.1 (`mglQ3/src/context.c`, around lines 1789–1870):
- FromWindow calls `vid_OpenExistingWindow(context, window, &w, &h)`, then
  `MGLInitContext` and a viewport of the window size. The caller owns the
  window, and it must outlive the context.
- FromBitMap requires a CGX bitmap (`CYBRMATTR_ISCYBERGFX`) with
  `BPPIX == 4`. It creates a Warp3D context on that bitmap with
  `NumBuffers = 0` and no presentation: the application presents and owns
  the bitmap's lifetime.

R200 design:
- The 68k library (`classic_68k/.../library/r200_surface.c`) already
  imports a window's bitmap with `Radeon3DImportBitMap` in window mode
  (around line 935). Add `MGLCreateContextFromWindow`: validate the
  caller's `Window->RPort->BitMap`, which must be a P96 bitmap on the
  Radeon board (validated through `p96GetBitMapAttr`/board match, never by
  touching PCI). Then reuse the window-mode back-buffer and blit-present
  path.
- `FromBitMap`: import the bitmap directly when it is in Radeon VRAM and in
  a `COLOR_TARGET_FORMATS` format (RGB565 or ARGB8888). Swap means finish
  only, with no flip.
- Fallback: when the bitmap is not on the board (planar, fast RAM, another
  card), either render offscreen and CPU-copy at swap, or return NULL.
  Decide after measuring; the default is NULL with `GL_INVALID_OPERATION`.
- Protocol: `CREATE_CONTEXT` gets a flag plus the window/bitmap pointer in
  `value[]`. That is protocol 17 with capability bit 19 `EXTERNAL_TARGET`.
  A DLL talking to an older host returns NULL.
- **Host events:** for an external window, the host must **not** `GetMsg`
  the application's `UserPort`, because the application owns its IDCMP.
  Disable `PollContextEvents` for such contexts. `MGLGetWindowHandle`
  returns the caller's window.
- Lifetime: closing the window before `mglDeleteContext` is a client error.
  Document it, and make the host tolerate it during teardown (no access to
  the window after DESTROY).
- The mglQ3 DLL variant reuses upstream's Warp3D code as is.
- Update `AGENTS.md`: replace "Do not add
  `FromWindow`/`CreateContextFromWindow`" with the new contract and its
  limits.

Acceptance: an SDL-style test that opens its own window on the Radeon
Workbench screen, a bitmap test with application presentation, and a
rejection test with a non-board bitmap. Windowed presentation must still
work after a cold boot (see the warm-reboot caveat in `AGENTS.md`).

### P8 — Classic 68k `minigl.library` migration (L, separate track)

`classic_68k/dev/MiniGL_Library_Source_Code` has its **own private tail**:
`StubPolygonOffset`, the Direct FastPath trio, lighting from slot 133,
`GLInterleavedArrays` and `MGLEmergencyCloseScreen` at the end. That is
incompatible with 29.1 as well, so 68k clients built against 29.1 headers
would crash on it.

- Migrate the four `library/dispatch_table_*.c`, `include/`, and the
  packaged `dev/include/` copy to the 29.1 layout and tokens, as `AGENTS.md`
  requires for dispatch changes.
- The R200 implementations from P4–P7 come along: the frontend logic is
  shared code (the 68k `r200_*.c` files are the same lineage).
- Once the 68k library uses OpenGL values, retire the P2.2 wire translation
  and set the protocol to 17/18.
- Rebuild the host against the new library, and keep the Sonnet LoadSeg
  classifier check.

### P9 — Documentation, AGENTS.md, benchmarks (S per phase, mandatory)

| Change | Document |
|---|---|
| Layout, ABI 5, slots 133–208, removed slots | `docs/dispatch-abi.md` (rewrite §1–§2, keep the FastPath regression note with the new offsets) |
| Protocol 17, `EXTERNAL_TARGET`, depth value, wire translation | `docs/transport-protocol.md` |
| Host external-window event rule, Z24 | `docs/host-68k.md` |
| Frontend: tokens, line width, edge flags, display-list swap, GLU/GLUT layer | `docs/frontend-r200.md` |
| CopyTex costs, TexEnv colour | `docs/textures.md` |
| New sources, generator rename, layout check | `docs/building.md` |
| Measured before/after FPS for P2, P4, P6 | `docs/performance.md` |
| New probes and examples | `docs/testing.md`, `tools/amiga-regress.script` |
| Client rebuild requirement (ABI 5), GLUT usage, FromWindow lifetime rules | `docs/client-guide.md`, `docs/troubleshooting.md` |
| ABI 3 → 5 rule, FromWindow rule, slot list | `AGENTS.md` |

Benchmarks follow `AGENTS.md`: change one variable at a time, run three
cold boots, report the median and every sample, use `-frames N`, and keep
diagnostics off.

---

## 3. Order and checkpoints

```
P0 → P1 → P2 → P3 ─┬→ P4 → P6
                    ├→ P5
                    └→ P7 (needs protocol 17 from P6.5, or its own bump)
P8 after P4–P7 are stable on PPC.  P9 inside every phase.
```

The first useful milestone is **P0–P3**: a 29.1-shaped ABI-5 DLL whose old
features render as before and whose new slots are safe fallbacks. The second
is **P4**, which turns on lighting through the PPC ABI.

---

## 4. Risks

| Risk | Mitigation |
|---|---|
| Existing ABI-3 binaries refused (glqfinal, ioq3, examples, possibly SilentHill.elf) | `bin/examples` are rebuilt in P1/P3. `glqfinal` and ioq3 are a later follow-up (D4): rebuild with `startupwos.o` first on the link line. Rebuild list in `docs/client-guide.md`. O1 covers source-less binaries. |
| Token migration breaks enum arithmetic silently | P2 audit grep, Mesa-checked probes, and screenshot comparison with the P0 baseline before any feature work. |
| Hot-path slowdown from sparse switches | Measure in P2; reorder hot cases or use small range checks. |
| Shared emitter changes break the host path or the PPC `CP_EMIT` path | Cap-gated, both paths tested, matched driver pair with `.previous` backup. |
| External window IDCMP conflict | Host polling disabled for external contexts (P7). |
| Third-party code licences (SGI GLU, freeglut) | Check before import; otherwise write fresh implementations. |
| Writable dispatch table for list recording | The table currently lives in `.data` or `.rodata`. Make it writable; keep `currentContext` semantics; one context at a time (already global). |

## 5. Open questions (to settle before or during the phase noted)

- **O1 — resolved 2026-10-21:** `SilentHill.elf`'s own `MiniGLOpen` (at `.text+0x3a718c`) requires `abiVersion == 3` (`cmpwi r9,3; bne fail`) and `structSize > 547`, so the ABI-5 DLL refuses it and the game falls back to its software renderer. It loads `minigl_ppc.dll` from its own directory, so it can keep an ABI-3 R200 DLL (built from the pre-ABI-5 tree; protocol 16 is unchanged, so it pairs with the current host) next to the ELF. Original text:
  **O1 (before P1, narrowed by D4):** `glqfinal` and ioq3 will be rebuilt,
  so this only matters for binaries without source, mainly `SilentHill.elf`.
  Keep existing ABI-3 binaries working? Old binaries
  look up `MGLGetDispatchTable` by name and expect ABI 3. Clients built for
  upstream **MiniGLWOS** (released 8 Oct 2026, source not in the reference
  tree) probably look up the same name and expect ABI 5. One name cannot
  serve both, so a legacy table helps only if upstream WOS clients use a
  different symbol. Otherwise the only way to keep old binaries is a
  separately named legacy DLL. First find out which symbol upstream
  MiniGLWOS clients resolve, and whether `SilentHill.elf` can be rebuilt; if
  it cannot, this decision matters.
- **O2 (P2.2):** Wire translation now versus migrating the 68k library at
  the same time. The plan says translate now and migrate in P8.
- **O3 (P7):** Non-board bitmap fallback: CPU copy or NULL. The plan
  defaults to NULL until measured.
- **O4 (P5):** Whether GLUT must support more than one window. Upstream has
  one global context, so the plan assumes a single window.

---

## 6. Implementation status (2026-10-21)

Everything below builds (R200 DLL, mglQ3 DLL, stub DLL, import archives, all
examples); host-side tests match HEAD. **Nothing has run on the hardware.**

| Phase | State |
|---|---|
| P0 | done: `tools/check_dispatch_layout.py` + vendored reference, run by the R200 build |
| P1 | done: ABI 5, 0x354 table, upstream wrappers + `mgl/glut.h`, `MGLResizeContext` returns `GLboolean` (a prefix difference found by the checker) |
| P2 | done: OpenGL token values in client header and R200 frontend (358 shared tokens, parity test), generated wire translation (`r200_wire_enum.h`, `WireTranslateRequest`, error map), upstream 29.1 mglQ3 core with WOS deltas |
| P3 | done: every slot bound in all three DLLs; R200 has 3 explicit fallbacks left (`glTexGenfv`, FromWindow, FromBitMap) |
| P4 | done: lighting/material/normal pointer exposed, IsTexture/Resident/Prioritize, GetPointerv, GetDoublev, EdgeFlag state, ReadBuffer state, LineWidth (CPU-expanded wide lines), TexEnvfv colour stored, ZBufferDepth, new queries |
| P5 | done: display lists (table-swap recording, generated thunks, array-draw expansion on R200), GLU quadrics + Build2DMipmaps, GLUT |
| P6 | partial: CopyTex stage A done (R200 + generic); BlendEquation/Separate partial; TexGenfv planes, TexEnv colour consumption, Z24 not started (driver/record work) |
| P7 | mglQ3 DLL: done (upstream Warp3D code). R200: not started (68k library + protocol 17) |
| P8 | not started |
| P9 | docs updated for everything above |

New findings during implementation:

- The classic 68k R200 `minigl.library` already calls its private layout
  "ABI 5" (`examples/r200_run68k_probe.h`, protocol header comments). That
  collides with upstream's ABI 5: a 68k client built against upstream 29.1
  headers would pass the equality check against it and call the wrong slots.
  P8 must either adopt the upstream layout or move off the number 5.
- PowerOpen (vbcc) ABI: `glCopyTexSubImage2D` (9th argument `height`) and the
  GLU quadric / `glutSolid*` calls (doubles mixed with ints) need bridge
  trampolines like `glTexImage2D`'s before vbcc clients may use them.
- `glReadBuffer(GL_FRONT)` is stored on the PPC side, but PPC readback is
  served by the host's context, which reads its current target: honouring
  it needs a protocol word.
- Pre-existing, unrelated: `bin/examples/r200_mapping_bench_ppc.warpelf`
  does not relink from source (`minigl_ppc_transport.c` needs `MGLPPCWatch*`,
  which only the DLL defines); a failed relink deletes the tracked binary.

Hardware, 2026-10-21 (one cold boot, interleaved A/B, see
`docs/performance.md` section 2): Quake demo3 800x600x16 61.374 -> 61.305
and Quake 3 `four` 1024x768x16 48.7 -> 49.0 (medians of three), all runs
failed=0 with identical present counts: ABI 5 works end to end for both
games (wire translation, token values) at no measurable cost.

Hardware validation still needed before merge (AGENTS.md procedure): `abi5_smoke`
(window and fullscreen), `glut_shapes`, the regression script, gears/Quake
FPS against the pre-ABI-5 baseline (token switch codegen), lighting
screenshots, a texture upload + readback round trip (wire translation), and
`glqfinal`/ioq3 after their rebuild (D4).
