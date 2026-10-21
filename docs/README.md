# MiniGL WarpOS/PPC — developer documentation

API level 29 / dispatch ABI 5 (upstream minigl.library 29.1 parity; the tree
began as MiniGL V19).

This directory is the single source of developer documentation for the
WarpOS/PPC MiniGL implementation in this repository. It is **PPC-first**: the
primary subject is the WarpOS `minigl_ppc.dll` direct-R200 stack
(`minigl_ppc.dll` + `minigl_ppc_host` + 68k `minigl.library` +
`Radeon9200.chip`). The legacy `mglQ3`/Warp3D DLL and the classic 68k tree are
documented only where they matter to the PPC stack.

Target hardware: Amiga 4000 (68060) + Prometheus PCI + Radeon 9200 (RV280) +
PMC PPC card (MPC7410) running WarpOS. This is a physical machine, never an
emulator.

## Reading order

| # | Document | Read it for |
|---|---|---|
| 1 | [overview.md](overview.md) | What MiniGL is, what this repo ships, the two runtime paths, repo map |
| 2 | [architecture.md](architecture.md) | End-to-end data flow, components, tasks, memory, submission lifecycle |
| 3 | [building.md](building.md) | Toolchains, every makefile and target, ET_REL contract, packaging |
| 4 | [dispatch-abi.md](dispatch-abi.md) | `MGLDispatchTable` ABI 5 (upstream 29.1 layout), per-slot status, the shared `ppc_common` layer (display lists, GLU, GLUT), FastPath, upgrade rules |
| 5 | [client-guide.md](client-guide.md) | Writing and running PPC clients, runtime requirements, deployment |
| 5a | [client-porting.md](client-porting.md) | For game developers: building a client against MiniGL 29 / ABI 5, migrating V19 ports, PowerOpen notes, Silent Hill example |
| 6 | [frontend-r200.md](frontend-r200.md) | PPC semantic frontend internals: state, batching, submission, surfaces |
| 7 | [transport-protocol.md](transport-protocol.md) | Exec port, ring, mailbox, protocol v15, fences, memory ordering |
| 8 | [host-68k.md](host-68k.md) | The 68k host `minigl_ppc_host`: loop, handlers, segments, env vars |
| 9 | [textures.md](textures.md) | Texture paths, formats, residency, P96 allocation contract, costs |
| 10 | [performance.md](performance.md) | Measured FPS, frame budgets, cycle-level costs, regressions, profiling |
| 11 | [benchmarking.md](benchmarking.md) | The hardware-validated benchmark procedure (cold boot → medians) |
| 12 | [testing.md](testing.md) | Host-side test suite, probes, generated regression scripts |
| 13 | [troubleshooting.md](troubleshooting.md) | Symptoms, causes, recovery, deployment rules learned the hard way |
| 14 | [run68k-direct-calls.md](run68k-direct-calls.md) | Measured PPC→68k `Run68K` direct calls: mechanism, costs, limits, probes |

Deep, still-authoritative specifications and evidence live in `reference/`:

- [reference/MAILBOX_V1.md](reference/MAILBOX_V1.md) — control mailbox contract.
- [reference/CP_EMITTER.md](reference/CP_EMITTER.md) — PPC-side CP emitter.
- [reference/HOST_LOADER_REGRESSION.md](reference/HOST_LOADER_REGRESSION.md) —
  Sonnet loader misclassification and the yield/pacing regression evidence.

## Documentation maintenance (mandatory)

`docs/` must describe the code as it is **now**. Per `AGENTS.md`:

- Any change that is committed and pushed to the remote must update the
  affected documents in the same change set. A doc update is part of the
  change, not a follow-up task.
- New files must be added to this index.
- Moving or deleting a document requires updating every reference to it
  (grep the whole repo, including `classic_68k/` and comments in sources).
- Historical session notes, plans and "findings" files are not documentation:
  fold their durable facts into the relevant document here and delete them.
  Git history preserves the rest.
- Performance changes must update [performance.md](performance.md) with
  measured numbers **and their conditions** (CPU/board, stack CRCs, mode,
  depth, buffers, sync, cold/warm, backend mode). Never mix measurements from
  different stacks or boot states in one comparison.
- Do not invent cycle counts. Every cycle/timing figure in the docs is
  measured or derived from a measurement, and is labelled as such.

## Conventions used in these documents

- **Measured** — a number read from a clock/counter on the physical machine.
- **Derived** — arithmetic on measured numbers, with the derivation shown.
- **Estimated** — an expectation that has not been hardware-validated; never
  used as a gate.
- File paths are relative to the repository root unless stated otherwise.
- `backend_r200/…` is the PPC DLL side of the direct-R200 stack;
  `classic_68k/dev/MiniGL_Library_Source_Code/` owns the 68k
  `minigl.library` and its own `AGENTS.md`.
- Timebase values are session-specific; always re-read them, do not assume
  the documented value for a new boot.
- Diagrams are [Mermaid](https://mermaid.js.org/) code blocks and render on
  GitHub/GitLab and in editors with Mermaid support. Keep them valid: every
  diagram in `docs/` has been rendered with `mmdc` before commit.
