/*
 * Unity translation unit for the R200 backend.
 *
 * The backend sources are kept as separate modules for readability, but
 * they are compiled together into this one object: the m68k toolchain has
 * no usable LTO, and splitting them cost measurable frontend/serialization
 * time to lost cross-module inlining (gears fullscreen 16bpp dropped
 * 5.77 -> 5.39 FPS). Keep every module listed here exactly once.
 *
 * Order matters for i-cache placement on the direct-mapped 68060: hot
 * geometry/submit/math code goes first, cold lifecycle/texstate/surface
 * and the large lighting block in r200_state.c go last, so hot loops do
 * not fight cold ones for cache sets.
 */

#include "minigl_r200.c"
#include "r200_geometry.c"
#include "r200_submit.c"
#include "r200_math.c"
#include "r200_texture.c"
#include "r200_surface.c"
#include "r200_state.c"
