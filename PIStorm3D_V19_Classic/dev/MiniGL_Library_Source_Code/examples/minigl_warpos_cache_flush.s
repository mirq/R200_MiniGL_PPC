	.text
	.align 2
	.globl _MGLPPCFlushDataRange
	.type _MGLPPCFlushDataRange,@function
_MGLPPCFlushDataRange:
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

	.globl _MGLPPCStoreBarrier
	.type _MGLPPCStoreBarrier,@function
_MGLPPCStoreBarrier:
	sync
	blr

	.globl _MGLPPCInvalidateLine32
	.type _MGLPPCInvalidateLine32,@function
_MGLPPCInvalidateLine32:
	dcbf	0,3
	sync
	blr
