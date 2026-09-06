# MiniGL Developer Guide

This folder is for game authors and port maintainers using `minigl.library` as
an application dependency. It describes the public dispatch-table API; backend
and library implementation details are intentionally kept out of application
code.

## Start here

1. Read [QUICKSTART.md](QUICKSTART.md) for the required headers, link library,
   startup order, and deployment files.
2. Build [example/hello_triangle.c](example/hello_triangle.c) with the supplied
   GCC or vbcc makefile.
3. Use [PORTING.md](PORTING.md) when adapting an existing OpenGL game or engine.
4. Check [BACKENDS.md](BACKENDS.md) before relying on optional features or a
   specific display mode.

## Public SDK components

Applications need these parts of the repository:

| Path | Purpose |
|---|---|
| `include/` | MiniGL GL/MGL declarations and dispatch wrappers |
| `third_party/warp3d/include/` | Header-only transitive dependency of the public MiniGL headers |
| `lib/libminigl_dispatch.a` | GCC client import library |
| `client/minigl_base.c` | vbcc client object source |
| `client/minigl_open.c` | vbcc open/ABI-validation source |
| `client/minigl_dispatch_stub_vbcc.s` | vbcc single-LVO assembly stub |
| `bin/dispatch-*/minigl.library` | Runtime library; choose one backend |

Do not include files from `library/` in a game. Do not call backend `GL*` or
`Lib_*` functions directly. Public `gl*` and `mgl*` calls are wrappers around
the `MGLDispatchTable` returned by the library.

## ABI summary

- The application opens the library through `MiniGLOpen()`.
- `MiniGLOpen()` opens at the version in `MINIGL_VERSION`, retrieves the table,
  and validates `MINIGL_DISPATCH_ABI_VERSION` and its structure size.
- The current dispatch ABI is defined by
  `include/libraries/minigl_dispatch.h`; do not hardcode its number.
- There is one custom library vector, used internally by the client import
  code. Games should never call that LVO themselves.
- `mglCreateContext()` sets the dispatch table's current context. Do not assign
  the context slot manually.
- `mglDeleteContext()` clears and destroys the current context.

This dispatch ABI is not compatible with older experimental MiniGL libraries
that exposed one stack/register LVO per function. Ship a matching current
`minigl.library` with the game or state it as an installation requirement.

## Redistribution layout

A typical game archive contains:

```text
MyGame/
  MyGame
  MyGame.info
  data/...
Libs/
  minigl.library
```

The exact installer layout is up to the game. At runtime the selected library
must be discoverable as `LIBS:minigl.library`. The static client import library
and C headers are build-time files and should not be shipped to players.

## Important rules

- Call no `gl*` or context-dependent `mgl*` function before both
  `MiniGLOpen()` and successful context creation.
- Select window mode, pixel depth, buffer count, and buffer sizes before
  creating the context.
- Pair successful context creation with `mglDeleteContext()`, then call
  `MiniGLClose()` on every exit path.
- Keep all rendering and context ownership in one Amiga task unless a backend
  explicitly documents stronger support.
- Include the SDK's MiniGL headers instead of mixing them with another OpenGL
  implementation's headers in the same translation unit.
- Do not define `MINIGL_LIBRARY_BUILD` in application code. It disables the
  public client wrappers.
