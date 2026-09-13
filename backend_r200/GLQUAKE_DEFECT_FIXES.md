# MiniGL R200 Fix Session — GLQuake defect report (2026-09-13)

Branch `fix/r200-glquake-texture-defects`, based on `dd47e08`. Response to
`/home/mirek/BlitzQuake_src/MiniGL_R200_BUG_REPORT.md` with an isolated probe
(`examples/r200_defect_probe.c`, WarpOS, windowed 640x480x32 triple gadget
defaults) run against the fully CRC-verified deployed stack.

## Deployment audit (start of session)

Cold boot, `InitPPC` once, CRC of every artifact matched the validated
baselines exactly (`work:ibdraw/minigl_ppc.dll` 02605851/294768, `mglhost`
0FAA4EFA/40588 bits 00020000, `LIBS:minigl.library` 62343673/127252,
`Prometheus.card` 18A453D6, `Radeon9200.chip` CEB838A6, `precalc`
1F3401F7). The deployed stack was the current tree. Fresh local builds
reproduced both the host and the native library CRCs byte-identically,
so every reported defect was reproduced against current sources.

## Benchmark baseline

Following `how_to_benchmark.md`: fullscreen precalc gears 640x480x16,
3 buffers, sync off, 600 frames.

| Sample | Boot | Stack | FPS |
| --- | --- | --- | ---: |
| baseline1 | cold 2026-09-13 | deployed (pre-fix) | 133.333 |
| baseline2 | same boot | after Phase 1 (enum/format/error/WriteShotPPM) | 134.529 |
| baseline3 | same boot | after Phase 2 (mailbox wait + canonical staging) | 136.363 |

All `error=0`. Single samples; the median-of-three cold-boot protocol is
required for any final claim.

## Root causes and what changed (commit f16274f + follow-ups)

1. **Enum parity (the dominant correctness defect).** The frontend and
   native `minigl_r200.h` carried the *classic* client numbering; the WOS
   client header (`include/mgl/gl.h`) re-anchored `GL_TEXTURE0_ARB` to the
   real spec values (Quake2 multitexture fix), shifting its tail (-4) and
   dropping the lighting tail. Consequences measured on hardware, all
   reproduced by the probe and fixed:
   - `glBegin(MGL_FLATFAN)` -> INVALID_ENUM 0x58 (the reporter's "any draw
     fails 0x58"); MGL_FLATSTRIP likewise.
   - `glHint(MGL_W_ONE_HINT)`, `glColorTable`, `glActiveTextureARB` (real
     spec values 0x84C0/1) all returned INVALID_ENUM.
   - 565/4444 texture uploads rejected (the game's `-lm_*` tile path);
     BGRA/ARGB would have been silently misread through the
     byte-order fallback (silent channel swap).
   - The reporter's Defect 1 ("TexImage GL_RGBA -> 0x59") does *not*
     reproduce on a clean boot (TexImage GL_RGBA works, err=0). It is
     sticky-error contamination: their probe's stage S1 calls
     `mglWriteShotPPM`, which (pre-fix) set GL_INVALID_OPERATION that was
     never consumed, so S2's error check on the GL_RGBA upload read the
     leftover 0x59. Real defect underneath: the enum mismatches above.
   Fixed frontend constants to client values; `tests/test_enum_parity.py`
   guards both trees against drift (WOS pair 152 shared constants, native
   pair 191, 0 mismatches).
2. **Source-format whitelist.** The PPC frontend rejected GL_ALPHA /
   GL_LUMINANCE / GL_LUMINANCE_ALPHA (and BGRA/ARGB) source formats with
   0x58; the host byte-count table matched only RGB/RGBA/565/4444.
   Extended across frontend validation, `FrontendTextureLayout`, the
   direct-path conversion loops (unified through
   `FrontendSourceToBGRA32`, mirroring the native `ConvertPixel`
   exactly), host `TextureByteCount`, and the wire: source formats/types
   are translated to the native tree's numbering at pack time
   (`FrontendWireSourceFormat/Type`), because the host and native compile
   against the classic header. BGRA/ARGB now convert identically to
   native.
3. **Error fidelity.** `MGLPPC_STATUS_GL_ERROR` replies (native code in
   value[0]) now surface through `FrontendSetTransportError` on the
   shared TexImage path instead of being masked as 0x59 / INVALID_VALUE.
4. **mglWriteShotPPM.** Implemented in the PPC frontend over the
   command-27 readback path (banded staging, P6 header, correct
   top-to-bottom row order); dispatch remap in
   `ppc_dll/minigl_ppc_dispatch_r200.c`. Probe: files written, err 0x00,
   pulled PPM has the correct `P6 640 480 255` header.
5. **Mailbox wait pacing (the reported ~101 ms fixed TexImage cost).**
   PlatformCall profile attribution: 906,090,250 of 1,008,834,950
   lifetime ticks inside PPC controls; the host handled each control in
   ~1 ms (dbeg/dcom EClock evidence). Root cause: `PlatformMailboxWait`
   called `Delay(1)` every 256 spins -- each 68k-side sleep quantizes to
   a 50 Hz tick (~20 ms on this kernel, per HOST_LOADER_REGRESSION.md),
   adding 1-4 ticks per control. The "spinning starves the 68k host"
   comment was wrong for this architecture (separate physical CPUs).
   Replaced with the bounded-yield policy already validated for the ring
   waits (`MGLPPC_RING_YIELD_SPINS` budget, `MGLPPCYield()`).
   Measured TexImage, glFinish per iteration: ~77-94 ms -> **2-15 ms**.

## Performance attribution methodology

- Frontend: `MGLPPC_PROFILE=1` DLL build, shutdown report at
  `DH2:mglprof/dll-profile.log` (PPC_CONTROL/PPC_TEXTURE/PPC_BARRIER
  buckets). Runs the client from a directory containing the prof DLL.
- Host: `MGLPPC_PROFILE=1` 68k host (now additionally reports
  `HOST_SUBIMAGE` handler/preflight/native/bridge breakdown, added this
  session). Run from a directory containing that host.

Round 1 (raw client format staging):
`HOST_SUBIMAGE count=157 handler_us=18.68s native_texsub_us=18.57s`
(~118 ms per native TexSubImage dominated by the 68060 per-texel
`ConvertPixel` loop; preflight/bridge private queries measured ~0.07 ms).

## Phase 2: canonical staging (current state)

`FrontendStageTextureBGRA` converts client texels to canonical BGRA8888
on the PPC for both the shared TexImage path and command 26 subimages
(v15 hosts only; v14 keeps raw rows). The native `UploadTextureRegion`
takes its per-row `CopyMem` fast path for BGRA sources, so the 68k never
runs its per-texel loop for shared work. Probe re-run:

| Operation | Report (pre-fix) | Yield fix | Canonical staging |
| --- | ---: | ---: | ---: |
| TexImage 128 (RGB5_A1/RGB/RGBA) | ~102 ms | 2-3 ms | 2-3 ms |
| TexImage 256 | ~101 ms | 7 ms | ~15 ms |
| TexSubImage 256x256 full | 408-415 ms | 390 ms | **130 ms** |
| TexSubImage 128x128 patch | ~122 ms | 142 ms | **77 ms** |
| TexSubImage 128x64 lum | n/a (failed) | 46 ms | **32 ms** |
| matrix full-subimage 128 update | ~108 ms | ~11 ms | **5-6 ms** |

Validation this boot: `R200_TEXTURE_UPDATE_TEST PASS CHECKS=7127
FAILURES=0 CONTEXTS=2 UPDATES=18 FRAMES=16` (pixel-verified, including
ALPHA/LUMINANCE/LUMINANCE_ALPHA sources through the shared canonical
path), probe all-err-0, gears 136.363 FPS.

Deployed identities (recorded for rollback; backups as `.pre-enumfix`,
`.pre-yieldfix`):
- `Work:ibdraw/minigl_ppc.dll` BE2AEDBA/303812 (canonical staging)
- `Work:ibdraw/mglhost` 573F97A3/40624, bits 00020000
- `LIBS:minigl.library` unchanged 62343673; drivers unchanged.
Profiler-only diagnostics stay in `Work:ibdraw/profdir{,/pfrun}`
(prof DLL B7C62BF7/307788; prof host EE7DC3F8/48352) and are not
production artifacts.

## Known remaining work

1. **Native subimage residency churn (~19-27 ms fixed per update).**
   Every shared subimage re-imports fresh residency
   (`allocate+import+full-backing CopyMem`, R200 texture cache holds
   per-address lines; in-place rewrites verified invisible). This is
   now the largest remaining subimage component after the canonical
   staging change. Candidate surgery lives in the classic tree's
   `Lib_glTexSubImage2D`/EnsureTextureResident (surface pool with
   fenced retirement) and must respect the TEXTURE_UPLOAD_PROGRESS
   visibility gates plus phase5/7127 pixel checks.
2. Chunked (non-direct, non-shared) TexImage still ships raw client
   rows; canonicalizing it needs chunk-texel-aware splitting (>2 MiB and
   v14 fallback paths). Low priority.
3. Final performance claims need the three-independent-cold-boot
   median protocol (how_to_benchmark.md), which has not been run for
   the Phase-2 numbers above.
