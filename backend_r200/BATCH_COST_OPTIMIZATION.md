# Batch-cost optimization candidate

This follows the diagnostic-free Quake baseline of 969 frames in 214.260 s
(4.523 FPS, 800x600x32 fullscreen). No target runs, reboots or deployment
were performed for this optimization pass.

## Corrected expansion estimate

A state batch holds one header plus draw descriptors. Expansion copies
that header, changing only each record's vertex count. For unlit records,
the service caches the identical state and matrices. Consequently the
old homogeneous portion keeps its compact generated-command bound rather
than requiring a complete state upload per draw.

`R200ExpandedBatchCost` now retains the compact cost for unlit headers.
`CanExpandStateBatch` selects this using the **old** header's LIGHTING bit;
the incoming differing draw is still charged separately. Lit records keep
the conservative full-state bound because the emitter currently repeats
43 + 39 * enabled-lights dwords on its cached per-record path.

Representative unlit, textured, 64-draw case (no depth):

| Quantity | Dwords |
| --- | ---: |
| Previous safety repair's estimate | 10,176 |
| New generated-command bound | 982 |
| Actual driver emission, shared or expanded | 722 |

The new bound includes four words for a possible content-serial invalidation.
Texgen additionally reserves its two 21-word matrix uploads; the old model
omitted these. Both are one-time costs in a homogeneous unlit batch.

The 8192-word wire limit, prefix/descriptor accounting, segment limits,
draw-table limits and pre-copy publish guard are unchanged. This does not
restore the old buffer overflow or change record/vertex structure layouts.

## What this does and does not prove

It removes false generated-budget rejections and permits the existing
expansion policy to avoid an otherwise forced submit. It does **not** reduce
722 GPU words to a smaller number, nor imply a proportional FPS gain.
Expanded records use more transport bytes and more 68k header parsing than
compact state batches. The net tradeoff against submission overhead still
requires a controlled hardware comparison before calling this policy optimal.
The expansion threshold has not been retuned, and the world renderer has
not been switched to an unvalidated submission path.

## Local verification

```
python3 tools/test_batch_budget.py
python3 tools/test_batch_expansion.py
python3 tools/test_screen_clip.py
```

- 579,794 packet/expansion boundary checks pass.
- 2,247 checks against the real Radeon emitter pass. Unlit shared-state and
  expanded forms produce byte-identical streams. The fixture covers varying
  vertex counts, depth, fog, dual textures, compact vertices, normals,
  texture generation, content serials, triangles/fans/quads and up to 180 draws.
- The full-state-estimate mutant fails 24 checks, demonstrating that the
  test detects the over-conservative behavior.
- 7,188 clipping checks pass; the old fused-interpolation mutant still fails.
- WarpOS client/DLL and 68060 client builds pass; the changed backend also
  compiles in its native 68k configuration. Existing compiler warnings remain.

Candidate ELF: `bin/qopt.warpelf` (CRC32 E098BA66).
It is not the DLL currently installed on the Amiga.
