# MiniGL Test Suite

`minigl_testsuite` is a backend-neutral interactive correctness and performance
runner for the public MiniGL dispatch API. It does not call Radeon, PiStorm3D,
or Warp3D hardware services directly.

## Build

Build the import library and suite with the Makefile for the MiniGL library that
will be installed on the Amiga:

```text
PATH=/opt/amiga/bin:$PATH make -f Makefile_dispatch_r200.gcc testsuite
PATH=/opt/amiga/bin:$PATH make -f Makefile_dispatch.gcc testsuite
PATH=/opt/amiga/bin:$PATH make -f Makefile_dispatch_classic.gcc testsuite
PATH=/opt/amiga/bin:$PATH make -f Makefile_dispatch_stub.gcc testsuite
```

The corresponding binaries are written to `bin/`.

Every `testsuite` target also creates an easy-to-deploy pair:

```text
bin/MiniGLTestSuite
bin/StartMiniGLTests
```

Install both files in `Work:MiniGLTestSuite/`. `StartMiniGLTests` gives the
program a 64 KiB stack and starts a 512x320x16 window. It can be run with
`Execute Work:MiniGLTestSuite/StartMiniGLTests` or from Workbench with
`C:IconX` as its default tool. `MiniGLTestSuite` can also be started directly
and needs no arguments.

## Interactive Use

Run the binary without arguments. The menu shows what the selected test does
and what correct output should look like.

Controls:

- Up/Down, `W`/`S`, or `K`/`J`: select a test
- Left/Right or `A`/`D`: choose 30, 60, 120, 300, or 600 measured frames
- Return: show the animated preview
- Return from preview: start the uninstrumented FPS measurement
- Escape: return to the previous screen
- `Q` or the window close gadget: exit cleanly

The interactive sequence is:

1. Preview the scene with its purpose and expected appearance.
2. Press Return to run warm-up and measured frames without an overlay.
3. Inspect the automatic `PASS`, `FAIL`, or `SKIP`, measured FPS, and reason.

The live preview and benchmark show FPS in the native Intuition window title,
not in rendered MiniGL geometry. The explanatory text is shown on a separate
screen before the clean preview. Window-title update time is removed from the
final benchmark duration. The menu remembers the last measured FPS for each
test.

## Automated Use

```text
minigl_testsuite --list
minigl_testsuite --run-all --window --width 512 --height 320 --depth 16
minigl_testsuite --test 3d.rotating-cubes --frames 300
minigl_testsuite --test quake.alias-ogre --frames 300 --profile
```

Results are printed as `MINIGL_TEST` records and appended to
`T:minigl-testsuite.log`. `--run-all` exits with status 1 if any test fails.

On the R200 backend, `--profile` also emits a `MINIGL_PERF` record for each
benchmarked test. It breaks time down into frontend, serialization, Execute,
wait, presentation, texture upload, and residency ticks, and reports geometry,
submission, batching, texture-barrier, and failure counters. The output includes
the backend clock rate so ticks can be converted without assuming a timer rate.
Profiling is opt-in because collecting the counters adds overhead; without
`--profile`, FPS remains directly comparable with earlier suite runs.

## Current Coverage

- Dispatch ABI version and table size
- Immediate-mode triangle rendering
- `GL_LESS` depth ordering
- source-alpha blending
- modelview translation
- vertex arrays with `glDrawArrays`
- perspective, modelview transforms, depth, and colored faces using five
  animated 3D cubes
- a 64-triangle presentation benchmark
- optional ABI-v4 lighting-vector availability
- all filled primitive types in flat and smooth shade modes
- requested versus measured point sizes
- Quake texture-alpha fills and repeated-presentation stability
- a rotating Ogre mesh extracted from the original Quake model, using the
  strided alias-array layout and alternating float/integer vertex paths
- four simultaneously visible Ogre instances at mixed depths, benchmarked as
  fixed, independently spinning, and advancing/receding scenes using float arrays
- sustained merged world geometry that exhausts streaming segments
- separate static and dynamically replaced lightmaps followed by Quake's
  `GL_ZERO`/`GL_SRC_COLOR` pass, allowing residency cost to be isolated
- hardware-transformed world geometry mixed with sized blended particles
- an effect-free FPS-style walkthrough of the real Quake `e1m1` entrance hall:
  BSP world geometry plus door/button brush models in texture-chained
  batches, walked out and back along the corridor on a deterministic camera
  loop, verified against baked block means at four patrol poses
- near/middle/far linear fog interpolation
- the exact device-coordinate `MGL_FLATFAN` pickup-flash blend/scissor recipe

By default, each rendering test uses 10 warm-up and 60 measured frames. The
measured count is configurable from the interactive menu or with `--frames N`.
FPS covers drawing and `mglSwitchDisplay()`. Pixel verification happens after
measurement and is not included in the FPS result.

The generated `examples/quake_ogre_model.h` can be refreshed from a locally
installed Quake PAK with:

```text
python3 tools/extract_quake_mdl.py /path/to/PAK0.PAK progs/ogre.mdl examples/quake_ogre_model.h
```

The `quake.world-room-walk` room data (`examples/quake_room_model.h`) is
regenerated the same way from a Quake PAK containing `maps/e1m1.bsp`:

```text
python3 tools/extract_quake_bsp_room.py /path/to/PAK0.PAK --map maps/e1m1.bsp examples/quake_room_model.h
```

Existing phase 4/5/6/7 acceptance checks remain standalone programs until their
remaining mechanisms are moved into descriptor-based suite tests.
