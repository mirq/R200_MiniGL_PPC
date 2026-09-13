# Deferred texture-bind sync: findings (2026-09-13)

Goal: remove the ~20-33 per-frame `MGLPPC_CMD_BIND_TEXTURE` mailbox
round-trips (room-walk's dominant control-message cost, ~2.3 ms of 68k time
each). Texture state already travels in every draw record header
(`r200_state.c`: header[4] surface token, [12] size, [13] texstate), so the
mailbox bind looked redundant for rendering.

## What was tried on hardware (A4000/MPC7410, Radeon 9200)

Baseline DLL `6C74A72D`. All runs fullscreen 640x480x16, fresh verified host,
suite `mgltestsuite` `F9B0C429`.

| Variant | DLL CRC | blend.texture-alpha | lightmap-update | quake.alias-ogre | gears |
| --- | --- | --- | --- | --- | --- |
| elision (no per-render bind mailbox, flush before uploads) | `E0C69055` | PASS | PASS | **FAIL x2** (submitted=0, no ogre rendered, capture failed) | 135.1 |
| diagnostic (legacy per-bind mailbox + flush kept) | `E148C36B`->exe | PASS | PASS | **PASS 30f + 300f** | n/a |
| baseline | `6C74A72D` | PASS (boot1) | PASS (boot1) | PASS (visible on screen) | 135.1/132.7 |

The elision failure signature: vertices are input (`input=29340`) but no
records are built (`submitted=0 records=0 fetches=30` vs `fetches=29340`
healthy) — the vertex-array draw dies in the state-header build
(`dropTexture0` class), before per-vertex fetch. Immediate-mode textured
draws (blend, lightmap) work fine without the bind mailbox; only the
`glDrawElements`/alias-array path breaks.

The host's `CMD_BIND_TEXTURE` handler only calls 68k `glBindTexture` and
updates its shadow. Net host command streams look equivalent between the
variants (one BIND before the upload either way — at bind time vs at upload
time). The remaining difference is ordering relative to
`FrontendTextureBarrier` (FlushScreenVertices + SubmitAndWait + platform
barrier) inside `R200PPCDispatchTexImage2D`. The 68k-side consumer that
requires the earlier bind is NOT yet identified; a verbose-trace run
(`MGLPPC_VERBOSE=1`, host stdout) reproducing the ogre elision failure is the
next diagnostic step.

## Current state

- Active deployed DLL restored to known-good `6C74A72D`
  (`minigl_ppc.dll.pre-binddef` backup retained). Gears 132.7 fps, error=0.
- Diagnostic build staged as `DH2:wosbuild/mglbind2.dll` (no perf gain over
  baseline; identical behavior plus a redundant pre-upload bind sync).
- Source keeps the diagnostic variant (`R200PPCDispatchBindTexture` marked
  DIAGNOSTIC) plus the deferred-sync infrastructure: `hostShadowTexture` /
  `hostShadowDirty` tracking, reset at context create, delete-textures dirty
  flag, `HostTextureShadowSync()` flushed before `TEXTURE_UPLOAD_SHARED` and
  `TEXTURE_UPLOAD_BEGIN`. The infrastructure is the foundation for the
  bind-table design below.

## Conclusion for improvement 1

Texture binds cannot be silently elided: something in the 68k
vertex-array/commit path consumes the bind stream. The fix must still DELIVER
the bind to the host, just not as a synchronous mailbox round-trip:

1. **Ring-entry bind (next step):** a small `MGLPPC_SEMANTIC_FLAG_BIND`
   ring entry `{target, name}` posted fire-and-forget (publish ~11 us)
   instead of the mailbox round-trip (~2.3 ms). In-order ring processing
   guarantees the host applies it before the following commit entry.
2. **Per-slot bind table (v16):** groups with a texture name in the commit
   trailer, host calls `glBindTexture` between record groups. Subsumes
   improvement 2 (submit merging across texture changes).

Both require a matched DLL+host deployment; neither changes the driver.
