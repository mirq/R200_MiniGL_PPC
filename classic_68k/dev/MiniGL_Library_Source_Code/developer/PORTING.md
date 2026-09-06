# Porting Existing Games

MiniGL implements a fixed-function OpenGL subset plus Amiga-specific MGL
context and presentation calls. Treat it as a target renderer, not as a drop-in
desktop OpenGL SDK with every symbol available.

## Recommended porting sequence

1. Add a dedicated `MINIGL` renderer/build option instead of changing every
   platform's OpenGL path.
2. Replace platform GL headers with `proto/minigl.h` for that target.
3. Link `libminigl_dispatch.a` and add `MiniGLOpen()`/`MiniGLClose()` to the
   renderer lifecycle.
4. Replace window/context creation and buffer swapping with the MGL lifecycle.
5. Compile once to inventory missing constants and functions.
6. Implement small renderer-side fallbacks for unavailable desktop GL calls.
7. Establish a simple untextured scene, then add textures, arrays, blending,
   multitexture, fog, and lighting one feature at a time.
8. Test at 16 and 32-bit depth, windowed and fullscreen, on every backend the
   game claims to support.

## Context conversion

Desktop ports often hide these operations behind SDL, GLUT, GLX, or a custom
video layer:

| Existing operation | MiniGL operation |
|---|---|
| Load/open GL implementation | `MiniGLOpen()` |
| Select video attributes | `mglChoose*()` calls before creation |
| Create window and GL context | `mglCreateContext()` or `mglCreateContextFromID()` |
| Set drawable size | `glViewport()`; use `mglResizeContext()` after resize |
| Swap buffers | `mglSwitchDisplay()` |
| Destroy context | `mglDeleteContext()` |
| Unload implementation | `MiniGLClose()` |

Do not cache or manipulate `GLcontext`. Public wrappers obtain the current
context from the dispatch table automatically.

## Selecting a mode

Configure these before `mglCreateContext()`:

```c
mglChooseWindowMode(windowed ? GL_TRUE : GL_FALSE);
mglChoosePixelDepth(depth);
mglChooseNumberOfBuffers(buffer_count);
mglChooseVertexBufferSize(vertex_capacity);
mglChooseTextureBufferSize(texture_bytes);
```

Not every backend uses every hint, but moving them after context creation is a
programming error. Prefer 16 or 32-bit depth for portable game settings. The
dispatch wrapper translates a request for 32-bit depth to the PiStorm3D
backend's historical 24-bit request; applications should still expose it to
users as a 32-bit color option.

## API coverage strategy

The authoritative callable surface is `MGLDispatchTable` in
`include/libraries/minigl_dispatch.h`. It includes the commonly used fixed
function groups:

- immediate-mode primitives, colors, normals, and texture coordinates
- model-view/projection matrices, viewport, depth range, and scissor
- depth, alpha, blend, cull, shade, fog, and polygon state
- 2D texture creation, updates, parameters, and two-unit multitexture calls
- vertex, color, normal, and texture-coordinate arrays
- indexed/non-indexed drawing and compiled array locking
- fixed-function lights and materials in dispatch ABI v4
- interleaved vertex arrays and emergency screen teardown in dispatch ABI v5
- readback, back-buffer locking, context resize, callbacks, and screenshots
- MiniGL FastPath capability/query/submit calls

Header declarations alone are not proof that a function is in the dispatch
ABI. Some declarations are inherited from historical MiniGL headers. If a
symbol has no wrapper in `minigl_dispatch.h`, it is not callable through this
library ABI.

Some ABI fields can be NULL on a backend. The lighting wrappers and the ABI
v5 wrappers (`glInterleavedArrays`, `mglEmergencyCloseScreen`) guard missing
vectors, but most older wrappers assume their field exists. If a port treats a
feature as optional, inspect its field after `MiniGLOpen()` before enabling the
feature. Backend identity is available through `MiniGLDispatch->backendFlags`.

```c
if (MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_R200) {
    /* R200-specific setting or diagnostic, if genuinely needed. */
}
```

Keep backend checks at the renderer boundary. Normal draw code should remain
backend-neutral.

## Common desktop GL differences

- MiniGL is fixed-function. Shader objects, VBOs, FBOs, VAOs, and modern GLSL
  pipelines are outside this API.
- Do not assume arbitrary texture units. The API exposes the historical
  `GL_TEXTURE0_ARB`/`GL_TEXTURE1_ARB` multitexture path.
- Do not resolve functions with desktop mechanisms such as `glXGetProcAddress`
  or SDL GL proc lookup. Dispatch is established by `MiniGLOpen()`.
- Use only constants and entry points present in the SDK headers and dispatch
  table.
- Keep client arrays valid until the corresponding draw has consumed them.
  Do not modify locked compiled arrays before `glUnlockArrays()`.
- Prefer `GL_UNSIGNED_SHORT` indices and conservative, naturally aligned data
  layouts on 68k targets.
- Avoid a `glFinish()` after every draw or frame. It removes asynchronous work
  and can dominate slower CPUs.
- Batch state-compatible geometry and avoid texture upload/subimage operations
  in the hot draw loop.

## Coordinate and UI paths

Normal `GL_TRIANGLES`, strips, fans, lines, and points use the current matrix
state. MiniGL also has `MGL_FLAT*` primitives used by established ports for
pre-projected screen-space UI geometry. Do not substitute an `MGL_FLAT*`
primitive for ordinary object-space geometry without understanding that
contract.

## Input and event loops

A port can either use the MGL callback/main-loop API (`mglKeyFunc()`,
`mglSpecialFunc()`, `mglMouseFunc()`, `mglIdleFunc()`, `mglMainLoop()`) or keep
its engine's existing Amiga event loop. `mglGetWindowHandle()` and
`mglGetInputWindowHandle()` expose backend window handles for integration.
Avoid consuming the same Intuition messages from two independent loops.

## Cleanup pattern

```c
int Renderer_Run(void)
{
    int opened = 0;
    int context_created = 0;
    int result = 20;

    if (!MiniGLOpen())
        goto cleanup;
    opened = 1;

    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 640, 480))
        goto cleanup;
    context_created = 1;

    result = RunGameLoop();

cleanup:
    if (context_created)
        mglDeleteContext();
    if (opened)
        MiniGLClose();
    return result;
}
```

On AmigaDOS, return code 20 is a conventional hard failure. Preserve the
game's own return-code policy if it already has one.

## Port validation checklist

- Missing library fails cleanly without calling GL.
- Failed context creation closes the library.
- Normal quit destroys the context before closing the library.
- Window resize calls `mglResizeContext()` and updates `glViewport()`.
- Windowed/fullscreen and depth preferences are applied before creation.
- Renderer/vendor/version strings are included in diagnostics.
- Texture uploads are correct for channel order, unpack alignment, and alpha.
- Depth test, alpha test, blending, culling, fog, and lighting are each tested
  independently before combining them.
- Indexed and non-indexed meshes render correctly.
- UI geometry, clipping/scissor, and readback are tested separately.
- No unconditional `glFinish()` remains in the normal frame path.
- A physical-machine test is performed for hardware backends; a clean cross
  build alone is not runtime validation.
