# Backend Notes

Applications use the same headers and `libminigl_dispatch.a` with every native
68k backend. Select the renderer by installing exactly one implementation as
`LIBS:minigl.library`.

## Backend flags

After `MiniGLOpen()`, `MiniGLDispatch->backendFlags` contains one of:

| Flag | Backend |
|---|---|
| `MINIGL_BACKEND_FLAG_STUB` | No-op development/test backend |
| `MINIGL_BACKEND_FLAG_CLASSIC` | Classic MiniGL hardware path |
| `MINIGL_BACKEND_FLAG_PISTORM3D` | PiStorm3D path |
| `MINIGL_BACKEND_FLAG_R200` | Radeon R200 path through `Radeon9200.chip` |

Prefer feature checks and renderer settings over backend-specific rendering
branches. Flags are most useful for diagnostics and narrowly scoped
workarounds.

## Stub

The stub backend is useful for validating startup, dispatch, and shutdown when
graphics hardware is unavailable. It does not validate rendered pixels or
hardware timing.

## Classic

The classic backend targets the established classic MiniGL/Warp3D path.
Feature and mode availability depends on the installed classic graphics stack
and hardware. Test the exact machines listed by the game as supported.

## PiStorm3D

The PiStorm3D backend uses its own rendering implementation. A public 32-bit
pixel-depth request is translated by the client wrapper to its historical
24-bit request. Use the public `mglChoosePixelDepth(32)` call rather than adding
a PiStorm-specific setting to game code.

## R200

The R200 backend requires physical Radeon R200-class hardware and the matching
`Radeon9200.chip` semantic service. It has no emulator path.

- Supported pixel-depth requests are 8, 16, and 32.
- 8-bit is fullscreen-only; portable ports should default to 16 or 32.
- Only one context-owning task is supported at a time.
- Fixed-function lighting is implemented. Two-sided lighting is accepted but
  rendered with front material semantics because the service has no back-face
  lighting block.
- `mglDrawMultitexBuffer()` is unsupported and reports invalid operation.
- FastPath v2 has no normal attribute, so beginning FastPath while lighting is
  enabled is rejected.
- Texture mutation, clear, readback, resize, and presentation are ordering
  barriers. Avoid placing them between small batches unnecessarily.

An R200 deployment also needs the user's normal Picasso96/graphics/intuition
environment and the Radeon service. Backend initialization can therefore make
`MiniGLOpen()` fail even when the `minigl.library` file exists.

## Shipping policy

State which backend(s), CPU/FPU, display depths, and window modes were actually
tested. Do not infer hardware support from a successful cross build. If a game
ships multiple backend library files, its installer must choose one and install
it under the single name `minigl.library`; do not put multiple implementations
in `LIBS:` under ambiguous names and expect automatic selection.
