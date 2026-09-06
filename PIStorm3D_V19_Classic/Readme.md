# PiStorm3D

PiStorm3D is a hardware-accelerated MiniGL implementation for classic Amiga systems using the Raspberry Pi VideoCore V3D GPU through PiStorm systems based on Raspberry Pi 4 / CM4 / 4B hardware.

The project implements a substantial subset of the classic Amiga MiniGL API and translates rendering directly to the V3D hardware.

It is primarily intended for accelerating existing and newly ported 68k OpenGL/MiniGL software on PiStorm-equipped Amigas.

This release contains the current MiniGLV3D sources, the V3D backend, GCC/vbcc build support and a collection of test and demo programs used during development.

## Status

PiStorm3D is based on MiniGL V1.2 and implements its API together with additional MiniGLQ3 and PiStorm3D extensions.

MiniGL V1.2 operates under the Hyperion MiniGL Open Source License. The complete license is included as a separate file with this distribution.

PiStorm3D also incorporates the libminiglQ3 changes by CowCat, which operate under the same MiniGL license.

PiStorm3D is included in two forms:

* A static linker library for creating executables which run directly on PiStorm systems using Raspberry Pi 4 / CM4 / 4B hardware.
* An implementation based on the new `minigl.library` 3D standard. This allows software to run both on PiStorm Pi4/CM4/4B systems and on other supported hardware by installing a suitable `minigl.library` implementation for the target hardware.

A `minigl.library` implementation for classic MiniGL-compatible hardware is available on Aminet.

The development source tree also includes the V10 R200 backend for Radeon 9200 hardware through `Radeon9200.chip`.

minigl.library is distributed under the Hyperion MiniGL Open Source License, except for incorporated third-party components which remain under their respective original licenses.

This includes V3D-related code derived from or based on work by Macoy Madson, Broadcom, VMware and Mesa. Those components retain their original copyright notices and license terms.0

## Requirements

PiStorm3D requires:

* A compatible PiStorm setup with access to the Raspberry Pi VideoCore V3D GPU using Raspberry Pi 4 / CM4 / 4B hardware.
* AmigaOS 3.x
* RTG graphics
* `cybergraphics.library` or Picasso96

PiStorm3 is not supported because it uses a different 3D GPU architecture.

## MiniGL API

PiStorm3D follows the classic MiniGL programming model.

Typical initialization for the statically linked PiStorm3D version is:

```c
if (!MGLInit())
    return 1;

mglChoosePixelDepth(16);
mglChooseWindowMode(GL_FALSE);

if (!mglCreateContext(0, 0, 640, 480))
{
    MGLTerm();
    return 1;
}

/* OpenGL/MiniGL rendering */

mglDeleteContext();
MGLTerm();
```

Applications using the shared `minigl.library` use `MiniGLOpen()` and `MiniGLClose()` instead of calling `MGLInit()` and `MGLTerm()` directly.

When adapting an existing MiniGL application to `minigl.library`, the following changes are normally required:

* Replace `MGLInit()` with `MiniGLOpen()`.
* Replace `MGLTerm()` with `MiniGLClose()`.
* Replace MiniGL includes such as `mgl/gl.h` with:

```c
#include <proto/minigl.h>
```

* Link against `libminigl.a` instead of `libmgl.a`.

`MiniGLOpen()` and `MiniGLClose()` also take care of opening and closing the shared `minigl.library`.

Alternatively, SDL1, SDL2 and SDL3 versions are available which use `minigl.library` for OpenGL/MiniGL support.

## Implemented Extensions and Additional Functions

Among the MiniGL/OpenGL functions supported by the current version are:

* `glActiveTextureARB()`
* `glClientActiveTextureARB()`
* `glInterleavedArrays()`
* `glMultiDrawArrays()`
* `glPolygonOffset()`
* `glColorMask()`
* multitexturing
* vertex arrays
* indexed rendering
* fog
* alpha testing
* blending
* texture environment modes
* clipping
* depth buffering

PiStorm3D also contains several implementation-specific MiniGL functions used by ports and test programs, including:

```c
mglEmergencyCloseScreen()
glMGLForceNewRenderPass()
```

`glMGLForceNewRenderPass()` exists for cases where a new V3D render pass must explicitly be started between rendering operations.

## Building the GCC Static Library

Run:

```text
build_lib_gcc_nolog.sh
```

This builds the static GCC PiStorm3D MiniGL library without diagnostic logging.

The build scripts use paths relative to the PiStorm3D source tree and therefore do not require hard-coded local source paths.

A logging-enabled GCC build script is also included for debugging purposes.

## Building minigl.library

The V10 `minigl.library` implementation is in `dev/MiniGL_Library_Source_Code/`.
It includes PiStorm3D, classic, stub, R200, and MiniGLPPC/WarpOS build paths.

Use the Makefile contained in the corresponding `minigl.library` source directory.

Applications using the shared library should link against the provided `libminigl.a` import library.
Application and game-port integration guidance starts at
`dev/MiniGL_Library_Source_Code/developer/README.md`.

## Demos and Tests

The `demos/` directory contains a collection of programs used to test individual parts of the implementation.

The demos cover areas such as:

* basic rendering
* texture mapping
* depth buffering
* blending
* alpha testing
* fog
* vertex arrays
* indexed rendering
* multitexturing
* clipping
* polygon offset
* windowed rendering
* regression and stress testing

Some demos require the MiniGL data package from Aminet:

https://aminet.net/dev/c/minigl_data.lha

## Credits

PiStorm3D was conceived and implemented by Dennis Boon.

`minigl.library` was conceived and implemented by Steffen "MagicSN" Haeuser.

We would like to take this opportunity to thank everyone involved in the project:

* Claude Schwarz and Michal Schulz for many hints and information regarding PiStorm and Raspberry Pi hardware.
* paraj for information about VideoCore 3D hardware.
* Macoy Madson and Mike, the Wandering Hacker, for extensive information, documentation and examples regarding programming of the VideoCore VI V3D GPU.
* Hans-Jörg Frieden, Thomas Frieden and Christian "Surgeon" Michael for developing MiniGL V1.2.
* CowCat for the MiniGLQ3 changes.
* Artur "arczi" Jarosik for supporting PiStorm3D with his games, including during the early development stages.
* flype, Mr-Z, mbo77, vitosky70 and Justin Webb for beta testing.
* Ben Hermans and Justin Webb for their initial substantial donations, which helped the crowdfunding campaign take off.
* Everybody who contributed financially to PiStorm3D.
* Everybody who tested, reported bugs, provided information or otherwise helped the project.

## License

PiStorm3D is based on MiniGL V1.2.

The MiniGL-derived portions of PiStorm3D, including our modifications and extensions and the incorporated MiniGLQ3 changes where applicable, are distributed under the Hyperion MiniGL Open Source License.

The complete MiniGL license is included as a separate file with this distribution.

PiStorm3D also contains third-party V3D code and headers which are not covered by the MiniGL license.

These components retain their original copyright notices and license terms.

In particular, parts of the V3D implementation contain or are derived from code by Macoy Madson, Broadcom and VMware, including code originating from or based on the Mesa V3D implementation.

The corresponding copyright notices and license terms are contained in the respective source files and must be retained when redistributing those components or substantial portions of them.

Other third-party components included in the source tree remain subject to their respective licenses and copyright notices.

## Disclaimer

PiStorm3D is provided without warranty, to the extent permitted by applicable law.

The authors and contributors accept no liability for loss of data, software malfunction, system instability, hardware damage or other direct or indirect damages resulting from the use or inability to use PiStorm3D, except where such liability cannot legally be excluded.

The warranty and liability provisions contained in the Hyperion MiniGL Open Source License and in the licenses of third-party components remain applicable to their respective parts of the software.
