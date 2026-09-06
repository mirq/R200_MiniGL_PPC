# Classic 68k V19 MiniGL driver

This tree contains the classic 68k side of the V19 MiniGL driver: the
`minigl.library` shared-library sources (dispatch tables, wrapper, client
import stubs, backends), the directly linked `mglQ3` backend, and the demo
and test programs used during development.

It targets AmigaOS 3.x with RTG. The active backend for physical-hardware
work is the R200 backend (`dev/MiniGL_Library_Source_Code/`, freestanding
68060/68881), which drives the Radeon 9200 (RV280) in a Prometheus PCI
adapter through the bounded `Radeon3D.library` service owned by
`Picasso96/Radeon9200.chip`. See the repository-root `AGENTS.md` for the
full build, deployment and benchmarking rules.

## Layout

- `dev/MiniGL_Library_Source_Code/` — 68k `minigl.library`: dispatch
  tables and backends (classic, stub, dispatch, R200), wrapper, import
  stubs, examples. See its own `AGENTS.md` for details.
- `mglQ3/` — directly linked MiniGL archive (`make -C mglQ3 -f makefile`
  → `libmgl-q3.a`).
- `demos/`, `bin/` — packaged release copies and build outputs; the
  makefiles do not update them.

## Provenance and license

This tree descends from MiniGL V1.2 (Hyperion MiniGL Open Source License)
and incorporates the libminiglQ3 changes by CowCat under the same license.
The historical V3D/PiStorm3D product this tree was derived from also
contained third-party V3D code (Macoy Madson, Broadcom, VMware, Mesa);
any retained V3D-derived files keep their original copyright notices and
license terms, which must be preserved on redistribution.

This software is provided without warranty, to the extent permitted by
applicable law.
