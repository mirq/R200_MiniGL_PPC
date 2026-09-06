	.text
	.align 2
	.globl MGLPPCFlushDataRange
	.type MGLPPCFlushDataRange,@function
MGLPPCFlushDataRange:
	cmpwi	4,0
	beqlr
	rlwinm	5,3,0,0,26
	add	6,3,4
.flush_loop:
	dcbf	0,5
	addi	5,5,32
	cmplw	5,6
	blt	.flush_loop
	sync
	blr

	.globl MGLPPCStoreBarrier
	.type MGLPPCStoreBarrier,@function
MGLPPCStoreBarrier:
	sync
	blr

	.globl MGLPPCBlit565Swap
	.type MGLPPCBlit565Swap,@function
# void MGLPPCBlit565Swap(void *dst, const void *src, ULONG bytes)
# r3 dst (4-aligned), r4 src, r5 bytes (multiple of 32).
# Big-endian 565 source -> little-endian-word 565PC destination:
# swap the two bytes of every halfword, 32 bytes per stswi so the
# aperture access cost amortises over one instruction per 32 bytes.
MGLPPCBlit565Swap:
	stmw	14,-32(1)
	stwu	1,-64(1)
	lis	22,0xFF00
	ori	22,22,0xFF00
	lis	23,0x00FF
	ori	23,23,0x00FF
	srwi	5,5,5
	mtctr	5
.blit565_loop:
	lwz	14,0(4)
	and	24,14,22
	srwi	24,24,8
	and	25,14,23
	rlwinm	25,25,8,0,31
	or	14,24,25
	lwz	15,4(4)
	and	24,15,22
	srwi	24,24,8
	and	25,15,23
	rlwinm	25,25,8,0,31
	or	15,24,25
	lwz	16,8(4)
	and	24,16,22
	srwi	24,24,8
	and	25,16,23
	rlwinm	25,25,8,0,31
	or	16,24,25
	lwz	17,12(4)
	and	24,17,22
	srwi	24,24,8
	and	25,17,23
	rlwinm	25,25,8,0,31
	or	17,24,25
	lwz	18,16(4)
	and	24,18,22
	srwi	24,24,8
	and	25,18,23
	rlwinm	25,25,8,0,31
	or	18,24,25
	lwz	19,20(4)
	and	24,19,22
	srwi	24,24,8
	and	25,19,23
	rlwinm	25,25,8,0,31
	or	19,24,25
	lwz	20,24(4)
	and	24,20,22
	srwi	24,24,8
	and	25,20,23
	rlwinm	25,25,8,0,31
	or	20,24,25
	lwz	21,28(4)
	and	24,21,22
	srwi	24,24,8
	and	25,21,23
	rlwinm	25,25,8,0,31
	or	21,24,25
	stswi	14,3,32
	addi	4,4,32
	addi	3,3,32
	bdnz	.blit565_loop
	addi	1,1,64
	lmw	14,-32(1)
	blr

	.globl MGLPPCInvalidateLine32
	.type MGLPPCInvalidateLine32,@function
MGLPPCInvalidateLine32:
	dcbf	0,3
	sync
	blr
