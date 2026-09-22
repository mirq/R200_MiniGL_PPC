# Architecture — the WarpOS/PPC direct-R200 stack

This document describes how a GL call travels from a PPC client to the RV280
and how the pieces are layered. For the protocol wire details see
[transport-protocol.md](transport-protocol.md); for the 68k side internals see
[host-68k.md](host-68k.md).

## 1. End-to-end picture

```
 +-------------------------------------- PPC (WarpOS) ---------------------------------------+
 |  PPC client program                                                                       |
 |    #include <proto/minigl.h>                                                              |
 |    glClear(...)  -> inline wrapper in include/libraries/minigl_dispatch.h                 |
 |                    MiniGLDispatch->GLClear(*(MiniGLDispatch->currentContext), ...)        |
 |    MGLDispatchTable* --- one pointer dereference, no 68k transition                       |
 |                                                                                           |
 |  minigl_ppc.dll  (Hyperion DLL, backend_r200/library)                                     |
 |    semantic frontend: GL state, matrices, lighting, clip/transform, batching              |
 |    -> semantic records + vertex payloads in shared memory                                 |
 |       * ring:     8 x 8192-dword slots published to the host (hot path)                   |
 |       * mailbox:  160-byte synchronous channel for texture/readback queries               |
 |       * Exec port: lifecycle + control messages (Hello, Create, Destroy, Bind, ...)       |
 +------------------------------------------+------------------------------------------------+
                                            | shared memory + Exec MsgPort
                                            | (big-endian; dcbf/sync publish)
 +------------------------------------------v------------------------------------------------+
 |  68k AmigaOS                                                                              |
 |  minigl_ppc_host  (backend_r200/minigl_ppc_host.c, port MiniGLPPC.R200.Host.1)            |
 |    polls ring entries -> decodes records -> Radeon3DExecute / service calls               |
 |    owns fences, retirement, present sequencing, texture staging buffers                   |
 |    -> minigl.library (68k, direct-R200 backend)                                           |
 |         -> Radeon3D.library service (in Radeon9200.chip)                                  |
 |              -> RV280 CP ring, MMIO, VRAM (driver-owned)                                  |
 +-------------------------------------------------------------------------------------------+
```

Key property: **the PPC never calls 68k code**. A WarpOS task calling a 68k
function raises the "illegal run68k function execution" trap, so all 68k
services are reached through shared memory and Exec messages handled by a
separate 68k task.

## 2. Components and ownership

| Component | Runs on | Owns | Must never do |
|---|---|---|---|
| Client (`libminigl.a` + headers) | PPC | GL call syntax, current-context pointer | Import private context/vertex layouts |
| `minigl_ppc.dll` frontend (`backend_r200/library/`) | PPC | GL semantics, state, command generation, surface/texture bookkeeping | Touch PCI/MMIO/CP/VRAM, call 68k code |
| `hyperion_dll/` | PPC (client side and DLL side) | DLL process rendezvous, symbol/resource lookup | GL semantics |
| `minigl_ppc_host` (`backend_r200/minigl_ppc_host.c`) | 68k | Ring/mailbox service, fence/retirement, staging buffers, present sequencing | GL semantics beyond what the service needs |
| `minigl.library` (classic_68k tree) | 68k | Direct-R200 backend: record decode, submission, surfaces | Enumerate PCI / own MMIO |
| `Radeon9200.chip` (p96-driver tree) | 68k | RV280 init, display, CP, MMIO, VRAM, Radeon3D service | Let clients touch hardware directly |
| `Prometheus.card` (p96-driver tree) | 68k | PCI enumeration/claiming, address translation, DMA arena | Radeon-specific logic |

## 3. Task and context model

- **One global current context.** The DLL owns `mini_CurrentContext`; the
  dispatch table's `currentContext` field points at it. Wrappers inject
  `*(MiniGLDispatch->currentContext)` as the hidden first argument. There is no
  per-opener context and no thread-local state.
- **One client task at a time.** `MiniGLBase` is a Hyperion DLL instance
  handle. `MGLPPCClientAttach`/`MGLPPCClientDetach` (exported by the R200 DLL)
  bind transport resources to the attaching task; a normal
  `MiniGLClose()` detaches and lets the DLL free task-owned resources safely.
- **Legacy clients.** A client built against the historical Silent Hill ABI-3
  library never calls the attach/detach pair. The DLL recognizes that case
  (`R200LegacyClient`) and releases transport on the context-delete path
  instead. This compatibility path is intentional and must not be removed.
- **Host lifetime.** One host process per client run. The client's
  `mglDeleteContext`/`MiniGLClose` leads to a shutdown message; the host then
  exits. A fresh host must be started before the next client. (See
  [client-guide.md](client-guide.md).)

### 3.1 Hyperion DLL runtime (`hyperion_dll/`)

A "Hyperion DLL" is not an Amiga shared library: it is a **separate PPC
process** that the client's loader starts and communicates with over an Exec
message port named `MINIGL_PPC_PORTNAME` (`MiniGLPPC.DLL`). The client calls
into it through exported symbols, not through library vectors.

- `dllLoadLibrary(name, portname)` (client side, `hyperion_dll/dll.c`) checks a
  small table of already-opened DLLs (max 20, keyed by port name), then either
  finds the running port or launches the DLL with `SystemTags(... SYS_Asynch
  ... NP_StackSize 10000)`. The DLL's stdout is redirected to the
  `MGLPPC_LOGFILE` file if set, to a console only when `MGLPPC_VERBOSE` is set,
  and to `NIL:` otherwise (silent by default because console.device writes
  dominate frame time). It then waits for the port and sends `DLLMTYPE_Open`.
- `dllGetProcAddress(hinst, name)` sends `DLLMTYPE_SymbolQuery` and waits on a
  reply port; `dllFreeLibrary`/`dllKillLibrary` close or expunge the instance.
- The DLL side (`hyperion_dll/dllstartup.c`) creates the port, resolves its
  `DLL_ImportSymbols`, calls `DLL_Init`, and serves `Open`/`Close`/`SymbolQuery`/
  `Kill` messages. It records the caller's `StackType` (`dllClientStackType`)
  because the ABI of function calls into the DLL differs between Storm/vbcc
  (PowerOpen) and GCC (SysV) clients; the texture-upload bridge uses that to
  select the right stack frame layout.
- Every dispatch DLL exports `MGLGetDispatchTable` plus the mandatory resource
  symbols `dllFindResource`, `dllLoadResource`, `dllFreeResource`. The R200 DLL
  additionally exports the PowerOpen texture block helpers
  (`MGLTexImage2DBlock`/`MGLTexSubImage2DBlock`) and the task-ownership pair
  `MGLPPCClientAttach`/`MGLPPCClientDetach`.
- Client side, `client/minigl_ppc_open.c` wires it all together:
  `dllLoadLibrary` → `dllGetProcAddress("MGLGetDispatchTable")` → validate
  `abiVersion`/`structSize`/`currentContext` → publish `MiniGLBase` and
  `MiniGLDispatch`. `MiniGLClose` detaches and zeroes the globals *before*
  `dllFreeLibrary`, so a late call can never reach a freed DLL.

## 4. Frontend modules (`backend_r200/library/`)

The PPC DLL compiles one translation unit for the frontend
(`r200_frontend.o`, built from `minigl_r200_warpos_compile_probe.c` +
`r200_unity.c`). The unity order exists for 68060-class i-cache locality and,
on PPC, to keep hot helpers inlinable. Do not split it casually.

| Source | Responsibility |
|---|---|
| `minigl_r200.c` | `MGLInit`/`MGLTerm`, context create/destroy, current-context rule, capability checks (`REQUIRED_CAPS`), perf clock, error state |
| `r200_state.c` | Deferred draw-state snapshot (`PendingDrawState`), serial comparison/copy, matrix stack, lighting/fog/blend/alpha/scissor state, `glHint` |
| `r200_geometry.c` | `glBegin/End`, vertex append, arrays/indexed draws, interleaved arrays, multitexture queue, state-batch accumulation/expansion, serialization kernels (`MGLPPCSwap*`) |
| `r200_submit.c` | Submit slots (`MAX_RECORD_DWORDS`, `MAX_SLOT_DRAWS`), publish/wait, streaming segment rotation, recovery, fence consumption |
| `r200_surface.c` | Window/fullscreen targets, depth storage, clear/read/flush/finish, choosers, `mglSwitchDisplay` (blit/flip), lock/unlock |
| `r200_texture.c` | Texture objects, residency/dirty tracking, upload paths, eviction |
| `r200_math.c` | Inline float bit ops, 4x4 math, inverse/normal matrices, lighting matrix build, fog factor |
| `r200_cp_emit.c` | Adapter to the shared `radeon3d_emit.c` for the optional PPC CP-emitter mode |

`minigl_r200_internal.h` is the single private header: it holds
`MiniGLR200Context`, the platform services (`FrontendPlatform`), the transport
declarations and all static assertions. The public frontend API is
`minigl_r200.h`.

### 4.1 Draw path (immediate mode and arrays)

1. Attribute calls (`glColor*`, `glTexCoord*`, `glNormal*`, `glVertex*`) capture
   current-attribute values; `glVertex*` appends a screen-vertex record
   (position, color, texture coordinates, optional normal index) to the context
   vertex buffer.
2. `glEnd`/`glDrawArrays`/`glDrawElements` call `FlushScreenVertices`, which
   captures the deferred `PendingDrawState` header (only if changed) and
   serializes vertices into the current submit slot.
3. `Submit()` publishes the slot to the ring with a generation stamp; the 68k
   host picks it up.
4. Retained-mode state changes (bound texture, lighting, matrices, blend) mark
   the state dirty; the serialization step writes a compact record that lets
   the 68k backend reuse previous commit state where the Radeon3D interface
   supports it.

### 4.2 Batching

Two independent mechanisms, both bounded by the 8192-dword record limit:

- **State batch** — a shared TCL header plus up to `MAX_SLOT_DRAWS` (192)
  draw entries. When the per-draw state deviates from the shared header,
  `R200ExpandStateBatch` fans the batch out into per-draw records. The
  mismatch threshold `R200_EXPAND_MISMATCH_MIN` (default 4, build-time
  `MGLPPC_EXPAND_MIN`) avoids expanding for a handful of outliers.
- **Budget checks** (`r200_batch_budget.h`) — pure endian-neutral helpers
  `R200SlotPayloadFits` and `R200ExpandedBatchCost` decide *before* emitting
  whether a batch fits the wire limit, including the ring commit prefix and
  state-batch overhead.

Tests that lock this behaviour down: `tools/test_batch_budget.py`,
`tools/test_batch_expansion.py`, `tools/test_state_batch_expansion.py`
(extracts the real `R200ExpandStateBatch` and runs it under ASan).

### 4.3 Submission and feedback

- Two 8192-dword submit slots rotate; a slot is reusable only after its fence
  is **retired**, not merely completed.
- Vertex payloads live in four leased vertex segments that rotate with the
  slots; `glFinish` waits for real retirement, not a local flag.
- The ring control block publishes `head`, `completed`, `retired`,
  `presentSeq`, `error` and event rings back to the PPC side; the frontend
  polls these (bounded spin, then yield) and reports `STALE_GENERATION` /
  `VERIFY_FAILED` if ownership/generation checks fail.
- Fences are grouped for coalescing when the driver exposes
  `MULTI_FENCE`/`FENCE_COALESCE`; the host retires a contiguous prefix.

### 4.4 Surfaces and presentation

- Windowed: one target surface; present is a bounded blit of the drawing
  buffer (default) or flip where supported.
- Fullscreen: 1–3 screen buffers; BLIT present is the default validated path;
  FLIP requires sync and has a documented CP-emitter race.
- `glReadPixels`, `glFinish` and `mglSwitchDisplay` are the synchronization
  points; they drain or wait as required by the current mode.
- Depth storage pitch is padded (16-bit depth to a 256-byte pitch) to satisfy
  the driver's tiling rules.

## 5. Transport layer

Three channels, each chosen for a different latency/ownership profile:

| Channel | Direction | Used for | Notes |
|---|---|---|---|
| Exec `MsgPort` (`MiniGLPPC.R200.Host.1`) | PPC → 68k, reply messages | Lifecycle and control: HELLO, CREATE/DESTROY, BIND_TEXTURE, upload begin/commit, readback, VRAM probe | Protocol v15 message header; one outstanding request per channel as encoded in the command semantics |
| Ring | PPC → 68k one-way, 68k → PPC control block | Hot path: semantic draw slots, present markers, retired/completed/present sequence feedback | 64 KiB segment, 8 slots x 8192 dwords, control version 6 |
| Mailbox | PPC ↔ 68k synchronous | Low-latency queries/commands where an Exec round trip is too expensive (GET_INTEGER, texture direct begin/commit, CP surface, upload-shared gating) | 160 bytes at ring offset 4096; serial/ack; 5 s timeout poisons the session |

Registration (`MGLPPC_CMD_REGISTER_SEMANTIC_RING`) negotiates capabilities and
leases the ring, four vertex segments and (when CP_EMIT is requested) a 256 KiB
CP segment. All leases are driver-backed segments; the PPC side only writes
through the mapped addresses it is given.

## 6. Memory model and resource bounds

| Resource | Size / count | Owner |
|---|---|---|
| Ring segment | 64 KiB | host leases, PPC publishes |
| Ring slots | 8 x 8192 dwords | frontend rotates |
| Vertex segments | 4 (segment size driver-defined, ≤256 KiB each) | host leases |
| CP segment (opt-in) | 256 KiB, 8 x 32 KiB slices | host leases |
| Mailbox | 160 bytes inside ring at +4096 | both |
| Texture shared staging | ≤2 MiB per transfer; 64 KiB chunks on Exec | PPC `MEMF_PUBLIC`, host invalidates |
| Submit slots (frontend) | 2 (one active, one in flight) | frontend |
| Max draws per slot | 192 | frontend/host |
| Max record dwords | 8192 (`RADEON3D_MAX_BATCH_DWORDS`) | driver contract |

The host uses `AllocVec` staging buffers for Exec-path transfers and
`CacheClearE` to make data visible. The frontend publishes with `dcbf`+`sync`
per cache line range. Both are bounded and pool-free.

## 7. Endianness and alignment

- 68060 and WarpOS/PPC are both **big-endian**, so protocol structures are
  plain `ULONG` arrays with no conversion. This is a deliberate simplification,
  not an accident; do not introduce host-endian assumptions if the design ever
  gains a little-endian peer.
- Little-endian appears only at the GPU boundary:
  - PPC serialization kernels use `stwbrx` to write vertex/TCL words in the
    RV280's little-endian format (`minigl_ppc_serial.s`).
  - The 68k side uses word-swap sequences (`ror.w #8`/`swap`/`ror.w #8`) when
    staging CP words and swaps 565 pixels for blits
    (`MGLPPCBlit565Swap` on the PPC side for the reverse direction).
- Alignment: Radeon aperture writes must be 4-byte aligned and preferably
  32-byte based; the mailbox occupies 32-byte lines; ring control is 32-byte
  aligned. The PPC frontend uses explicit size assertions
  (`minigl_r200_warpos_compile_probe.c`) for `MGLScreenMode` (96 bytes) and
  `MGLLockInfo` (24 bytes).
- Never force 2-byte packing on the dispatch table or R200 structures. Normal
  PPC layout on both sides of the ABI is required.

## 8. Lifecycle of a frame (semantic mode)

1. Client sets state and emits geometry; frontend captures state snapshots.
2. `glEnd`/draw call serializes into a submit slot; `Submit()` publishes.
3. Host sees the ring entry, decodes it, and calls `Radeon3DExecute` through
   `minigl.library`.
4. Driver builds/submits CP packets and returns a fence.
5. Frontend `mglSwitchDisplay` requests a present; host waits out any required
   fence and blits/flips the target surface.
6. Next frame reuses slots whose fences retired. `glFinish` (or a full ring)
   blocks until retirement.

In CP-emitter mode (opt-in, `MGLPPC_CPEMIT=1`) steps 2–4 change: the PPC builds
driver-format command packets itself and the host only submits them and manages
recovery. See [reference/CP_EMITTER.md](reference/CP_EMITTER.md).

## 9. Legacy mglQ3/Warp3D path

`Makefile.dll.gcc` links `mglQ3` (the shared GL core) into a DLL whose dispatch
table binds the `mglQ3` implementations. `mglQ3/src/init.c` opens
`Warp3DPPC.library` (PPC) or `Warp3D.library` (68k) and the core drives Warp3D
directly. This path shares the `MGLDispatchTable` ABI and the client-side
open/close flow, but it is a different rendering architecture and is **not**
used by the R200 target. The classic 68k tree also builds `mglQ3` for its own
backends.

## Related documents

- [dispatch-abi.md](dispatch-abi.md) — the contract clients and DLLs must match.
- [transport-protocol.md](transport-protocol.md) — wire formats and ordering.
- [frontend-r200.md](frontend-r200.md) — frontend internals in more detail.
- [host-68k.md](host-68k.md) — 68k host internals.
- [performance.md](performance.md) — where the time actually goes.
