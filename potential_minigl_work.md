# Potential MiniGL work (from the BlitzQuake vbcc bring-up, Sep 2026)

Game: `glquakeWOS_dll` (vbcc WarpOS dispatch client) in
`~/BlitzQuake_src_vbcc/build_wos_vbcc/`, running against the R200
semantic backend. Findings below are contract gaps between what classic
MiniGL clients send and what the R200 frontend/host accept.

## 1. Lightmap upload pairing rejected (INVALID_ENUM 0x58) — game-side remap applied, verify visually

Classic MiniGL takes packed 16-bit lightmaps as
`(format=4_4_4_4 / 5_6_5, type=UNSIGNED_BYTE)` — that is what BlitzQuake
sends in 4 places in `gl_rsurf.c` (lightmap alloc `glTexImage2D` + 3×
`glTexSubImage2D`, plus it uses the format enum as the texsub `format`).

The R200 frontend (`FrontendTextureLayout` /
`FrontendSourceToBGRA32` area) only takes 16-bit as
`(format=GL_RGBA, type=4_4_4_4)` / `(format=GL_RGB, type=5_6_5)` and
returns 0 (→ `GL_INVALID_ENUM`, 0x58 in MiniGL numbering) otherwise.
Result: every lightmap alloc/subimage failed → white/unlit world.

Game-side remap applied (`LM_UploadFormat()` / `LM_UploadType()` in
`glquake.h`, classic behavior preserved for non-dispatch builds). Data
layout on the wire is unchanged (still packed 16-bit, `lightmap_bytes=2`).

Backend-side alternative (2 lines): accept the classic pairing in
`FrontendTextureLayout`, i.e. treat `(4_4_4_4, UNSIGNED_BYTE)` as native
16-bit. Would fix all classic clients at once.

## 2. Texture uploads confirmed covered by f16274f

Game sends `(internalformat ∈ {3, 4}, format=GL_RGBA,
type=UNSIGNED_BYTE)` — all accepted (`FrontendSourceToBGRA32` handles
`GL_RGBA`; `Lib_glTexImage2D` accepts 3/4). No game change needed.

## 3. INVALID_OPERATION (0x59) during gameplay unattributed

After the lightmap remap, watch the per-frame `GL error` drain: if
0x59 persists, candidates are `FlushScreenVertices` failing inside
uploads, `ReleaseTextureStorage`, or texsub racing an open batch.
Needs backend-side attribution (which call site sets it) or game-side
per-call checks at the 4 upload sites.

## 4. Enum parity across the protocol

Game sends raw client values; tail constants differ between
`include/mgl/gl.h` (dispatch: BGRA≈233/235…) and the classic numbering
the host uses (BGRA=237 … 5_6_5=239, 4_4_4_4=240).
`FrontendWireSourceFormat/Type` translates the four tail constants —
verify the lightmap texsub path (now `type=4_4_4_4`) routes through it,
or the host sees the wrong type value.

## 5. Small protocol nits noticed while integrating

- `mglResizeContext` is a documented no-op; the game calls it on
  `gl_width` change (harmless, but a `GL_INVALID_OPERATION` with a
  hint, or honoring it, would beat silence).
- `mglWriteShotPPM` is a silent no-op (game screenshots don't work).
- `glClientActiveTextureARB` is a no-op (game's fake-multitexture path
  uses it; single-pass still renders, multitexture detail may be off).
