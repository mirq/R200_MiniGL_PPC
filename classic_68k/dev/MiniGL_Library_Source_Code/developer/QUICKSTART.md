# Quick Start

## 1. Build the GCC import library

The GCC cross-toolchain is installed under `/opt/amiga/bin` in this tree's
development environment:

```sh
PATH=/opt/amiga/bin:$PATH make -f Makefile_dispatch_r200.gcc importlib
```

The backend makefiles all write `lib/libminigl_dispatch.a`. This archive is
client-side code only: it defines `MiniGLBase`, opens and validates the shared
library, and calls the single dispatch-table LVO. Build it with flags compatible
with the application toolchain. It does not embed a rendering backend.

## 2. Add includes and linking

For GCC, add both include roots and link the import archive:

```make
MINIGL_ROOT := path/to/minigl_ppc
CPPFLAGS += -I$(MINIGL_ROOT)/include \
            -I$(MINIGL_ROOT)/third_party/warp3d/include
LDLIBS += $(MINIGL_ROOT)/lib/libminigl_dispatch.a
```

Add `-lm` if the game itself uses the C math library. Keep the application's
existing CPU, FPU, C runtime, and `-noixemul` choices consistent across all
objects.

For vbcc, compile and link these files into the application:

```text
client/minigl_base.c
client/minigl_open.c
client/minigl_dispatch_stub_vbcc.s
```

See [example/Makefile.vbcc](example/Makefile.vbcc) for exact commands.

## 3. Include the public API

```c
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
```

`proto/minigl.h` imports the GL/MGL types and the dispatch wrappers.
`clib/minigl_open_protos.h` declares `MiniGLOpen()` and `MiniGLClose()`.

## 4. Use the required lifecycle

```c
if (!MiniGLOpen()) {
    /* Missing library, old library version, or incompatible dispatch ABI. */
    return 20;
}

mglChooseWindowMode(GL_TRUE);
mglChoosePixelDepth(16);
mglChooseNumberOfBuffers(2);

if (!mglCreateContext(0, 0, 640, 480)) {
    MiniGLClose();
    return 20;
}

glViewport(0, 0, 640, 480);

/* Initialize GL state and run the game loop here. */

mglDeleteContext();
MiniGLClose();
```

Use a single cleanup path in a larger engine so partial initialization cannot
leak the context or leave the library open.

## 5. Present frames

Render to the current back buffer and present it with:

```c
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
/* draw world and UI */
mglSwitchDisplay();
```

`glFlush()` submits queued rendering. `glFinish()` additionally waits for it to
complete and should not be inserted into every frame unless synchronization is
actually required. `mglSwitchDisplay()` is the normal end-of-frame presentation
operation.

## 6. Build the example

From the repository root:

```sh
PATH=/opt/amiga/bin:$PATH make -C developer/example -f Makefile.gcc
```

The result is `developer/example/build/hello_triangle`. Copy that executable to
the Amiga and install one backend's `minigl.library` in `LIBS:`.

When replacing a library that was already opened during the current boot, run
`Avail Flush` before testing. AmigaOS may otherwise retain the old resident
copy even though the file on disk has changed.

## Diagnosing startup failures

| Symptom | Likely cause |
|---|---|
| Linker cannot find `MiniGLOpen` or `MiniGLDispatch` | Import archive/client objects are missing |
| Linker cannot find `MiniGLGetDispatchTableLVO` | Compiler-specific assembly client stub is missing |
| `MiniGLOpen()` returns false | Library absent, too old, backend initialization failed, or ABI mismatch |
| `mglCreateContext()` returns NULL | Unsupported mode/depth, hardware unavailable, or another task owns the backend |
| Crash on first GL call | GL call happened without a valid current context or an optional dispatch vector was used blindly |
| New build appears unchanged | Old resident library was not flushed |

Use `glGetString(GL_VENDOR)`, `glGetString(GL_RENDERER)`, and
`glGetString(GL_VERSION)` after context creation when recording user bug
reports.
