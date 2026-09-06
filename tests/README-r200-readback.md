# R200 Readback Validation

Command 27 uses capability bit 10 on protocol 15, without changing public ABI 3
or any message/ring/mailbox layout. Only `GL_RGBA` / `GL_UNSIGNED_BYTE` is
supported; other format/type pairs return `GL_INVALID_ENUM`. Nonempty requests
above 2 MiB return `GL_INVALID_OPERATION`, without partial output. Invalid
dimensions, NULL destinations, overflowing pack layouts and out-of-bounds
rectangles return `GL_INVALID_VALUE`. Missing capability or poisoned/abandoned
transport returns `GL_INVALID_OPERATION`. Zero-area calls do not transfer data.

Readback uses the native context's current read surface (normally the drawing
buffer), before present. Native out-of-bounds rejection is retained, not replaced
with clipping. This does not add read-buffer selection, LockBack, screenshots,
new native exports or a direct driver ABI. The host temporarily sets native pack
to alignment 1, row length/skips 0, then restores it on success and setup/readback
errors; the frontend applies application pack state. Native `glGetIntegerv` does
not expose pack fields. The host tracks them like unpack: fresh native contexts
are zero-initialized with alignment 4, and valid PixelStore replay writes update
the tracker. Temporary readback overrides do not change it. Pending/setup/native
errors take precedence over restoration errors; restoration errors are consumed
and reported if no earlier error exists.

The PPC cleans an aligned staging subrange before publication and leaves it
untouched until ack. Host checks the complete interval inside one Exec RAM
region before cache access, fences, fills with native ReadPixels, then cleans
before ack. PPC invalidates clean staging after successful ack before copying.
Failed/partial native writes never reach caller memory. Mailbox timeout reuses
the existing abandoned-request policy: no retry, no copy, no staging replacement
or free until late completion allows teardown. RAM validation is a trusted-peer
interval check, not proof of allocation ownership.

## Host-Independent Tests

Run from the repository root with a C99 compiler supporting ASan/UBSan and
Python 3. These standalone tests need no Amiga SDK or hardware and do not
rebuild production binaries. All generated binaries stay in `/tmp/opencode`;
no pre-existing scratch sources are required.

```sh
mkdir -p /tmp/opencode
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined tests/r200_texture_layout_test.c -o /tmp/opencode/r200-texture-layout-test
/tmp/opencode/r200-texture-layout-test
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined tests/r200_readback_layout.c -o /tmp/opencode/r200-readback-layout
/tmp/opencode/r200-readback-layout
python3 tests/test_r200_texture_order.py
python3 tests/test_r200_readback.py
python3 tests/test_r200_context_lifecycle.py
python3 tests/test_r200_context_lifecycle.py --regress-crc
python3 tests/test_r200_context_lifecycle.py --regress-cache
python3 tests/test_r200_context_lifecycle.py --regress-register
```

`r200_texture_layout_test.c` contains the former protocol-header test program:
source-format, rectangle and span assertions plus 100,000 boundary-biased cases
against an independent 64-bit oracle. The shared inline validators remain in
the production header; tests select its SDK-free declarations with
`MGLPPC_TEXTURE_VALIDATION_TEST`, which no longer defines a `main()`.

The three Python runners can each be run independently using the commands
above. `test_r200_texture_order.py` checks production-source publication/control
ordering and the example's pre-present pixel oracles without compiling it.
The other two runners generate and compile their own temporary C harnesses.

The readback C test exercises production 32-bit layout arithmetic and guarded pack copies,
including overlapping rows, alignments 1/2/4/8, row length, both skips, cap and
overflow boundaries. The Python runner compiles the actual frontend readback,
barrier, allocation and error helpers and the actual host command-27 handler
against deterministic stubs with ASan/UBSan. It checks cache/barrier order,
bounded RAM and payload validation, native errors, replayed nondefault pack state,
restoration on success/setup/native error and first-error preservation, allocation reuse, simulated
timeout and unchanged caller output. Non-PIE keeps test buffers addressable by
the real 32-bit protocol. Transport, GPU and caches are mocked, not emulated.

The lifecycle test runs two sequential contexts within one DLL open, separately
for ring/mailbox and Exec fallback. It uses production create/delete/close,
registration/discard, mailbox, publish and barrier functions, and the actual
inline RingPoll branch (unused streamed-commit branches are excluded). Native
texture and GPU operations are stubs; this is not a replacement for pixel tests.
It verifies fresh mailbox/ring counters, context IDs and registration epochs
advancing 1 to 2, stale lifecycle request rejection, first/repeat submissions,
and complete teardown/shutdown. No state-wide test reset occurs between the
two contexts. Registration deliberately reuses the same shared ring address.
The quiet-path test also wraps all eight payload slots twice with a dirty-host-
cache model. `--regress-cache` removes the host release clean and verifies that
stale writeback corrupts the next payload. `--regress-register` removes the
registration control-block clean and checks that an unpublished registration
is rejected by the test. Normal execution checks the PPC invalidates all new
control lines before use. These models do not prove physical cache visibility.

`--regress-crc` removes only the frontend CreateContext CRC rearm in the generated
test translation unit. This reproduces the original second-context failure:
frontend CRC zero, host first-Execute CRC nonzero, ring error=1/head=1/completed=0,
subimage barrier returning INVALID_OPERATION before command 26 is sent. Cleanup
retains the context and never reaches host shutdown; the mailbox is not poisoned.
The clear prefix is realistic: `FlushScreenVertices` submits a preceding clear
through inline Execute before a streamed state-batch draw. CRC arming must be
per registration, matching the host's reset of `semanticExecutes`, not per DLL
open. This establishes a deterministic software fault matching the reported
failure sequence; it does not claim a captured hardware CRC or a hardware rerun.

`r200_readback_abi.c` must be compiled separately with both real target SDKs.
It checks command/capability, mailbox eligibility, eight-word arguments, the
80-byte zero-payload message, 448-byte ring and 160-byte mailbox. Production
headers additionally check field offsets; the PPC DLL checks ABI-3 size 0x22c.

## Isolated Builds

```sh
make -f Makefile.dll.r200.gcc warpelf OBJDIR=/tmp/opencode/r200-readback-obj WARPELF=/tmp/opencode/minigl_ppc_r200_readback.warpelf R200_SYMBOLS=/tmp/opencode/r200-readback-symbols.txt R200_TABLE=/tmp/opencode/r200-readback-table.c
make -f Makefile.dll.r200.gcc /tmp/opencode/minigl_ppc_host_readback R200_HOST=/tmp/opencode/minigl_ppc_host_readback
make -f Makefile.import.gcc /tmp/opencode/r200-readback-examples/r200_texture_update_test_ppc.warpelf OBJDIR=/tmp/opencode/r200-readback-import BINDIR=/tmp/opencode/r200-readback-examples OUTLIB=/tmp/opencode/r200-readback-import.a DROPINLIB=/tmp/opencode/r200-readback-dropin.a
```

Build the explicit host file target, not `host`/`stack`, to avoid rebuilding the
native library. These commands do not deploy or overwrite tested bin artifacts.

## Runtime Coverage

`r200_texture_update_test` verifies first and repeated RGBA patches in two
contexts: old draw A versus new draw B, inside and outside each patch, before
present. Each verification frame uses four 1x16 reads (16 reads / 256 pixels
total), spanning texture rows 7 and 8 in GL bottom-up orientation. Expected
canonical RGBA comes from the original image and uploaded pattern, including
alpha. Pack padding/skips retain sentinels. The other five source-format updates
retain their error/order smoke checks, not pixel assertions. Direct-upload and
mailbox routing still require host counters; the public API cannot prove them.

No hardware run is implied by compilation or mocked tests. Exact rendered
values, native-driver compatibility, PCI/cache coherence and GPU ordering still
require the matched frontend/host/native stack on hardware. Readback is bounded
in bytes and test calls, not in native driver execution time.
