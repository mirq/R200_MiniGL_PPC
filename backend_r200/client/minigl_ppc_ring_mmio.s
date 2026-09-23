# PPC ring-writer MMIO primitives for the interface-20 engine lease.
#
# Shapes copied verbatim from the hardware-validated p96-driver phase-0
# helpers (tools/phase0/phase0ppc.s) and the proven MiniGL WarpOS barriers
# (minigl_warpos_cache_flush.s). All Radeon registers are little-endian;
# the PPC is big-endian, so every register access goes through
# lwbrx/stwbrx (load/store word byte-reversed). The same stwbrx
# instruction is what a direct ring writer uses to publish CP dwords
# without a separate swap pass.
#
# See docs/09 (p96-driver) for the measured timings: posted MMIO store
# 239 ns, committed (write+readback) ~0.5 us; PPC loads of the ring
# window are cache-served, so host-owned dwords are read through
# dcbf-invalidate (MGLPPCInvalidateLine32) before lwz.

	.text
	.align 2

# ULONG R200PPCMmioRead32(volatile void *address)
# Load word byte-reversed (little-endian register), eieio to keep the
# access ordered against following I/O.
	.globl R200PPCMmioRead32
	.type R200PPCMmioRead32,@function
R200PPCMmioRead32:
	lwbrx	3,0,3
	eieio
	blr

# void R200PPCMmioWrite32(volatile void *address, ULONG value)
# Posted store, no readback: the pure posting cost.
	.globl R200PPCMmioWrite32
	.type R200PPCMmioWrite32,@function
R200PPCMmioWrite32:
	stwbrx	4,0,3
	eieio
	blr

# ULONG R200PPCMmioWriteCommit(volatile void *address, ULONG value)
# Posted store plus readback of the same register: the committed shape
# the driver pays before publishing a ring pointer (phase-3 gate).
	.globl R200PPCMmioWriteCommit
	.type R200PPCMmioWriteCommit,@function
R200PPCMmioWriteCommit:
	stwbrx	4,0,3
	eieio
	lwbrx	3,0,3
	eieio
	blr

# void R200PPCCopyRingBr(volatile ULONG *destination, const ULONG *source,
#                        ULONG dwords)
# Load source dwords and store them byte-reversed: the little-endian
# ring-dword burst a direct ring writer uses (a ring dword must arrive
# on the bus in the little-endian shape the CP expects). Ends with eieio
# so the burst is ordered against the following MMIO readback/WPTR kick.
	.globl R200PPCCopyRingBr
	.type R200PPCCopyRingBr,@function
R200PPCCopyRingBr:
	mtctr	5
.copy_br:
	lwz	6,0(4)
	stwbrx	6,0,3
	addi	4,4,4
	addi	3,3,4
	bdnz	.copy_br
	eieio
	blr

# void R200PPCWriteBarrier(void)
# eieio + sync: orders all prior stores against following device access.
	.globl R200PPCWriteBarrier
	.type R200PPCWriteBarrier,@function
R200PPCWriteBarrier:
	eieio
	sync
	blr
