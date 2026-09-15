# ABI bridge for the two texture uploads.
#
# glTexImage2D/glTexSubImage2D are the only MGLDispatchTable entries
# with more than eight arguments. The dix table entry receives
#
#   (context, target, level, internalformat, width, height, border,
#    format, type, pixels)
#
# which fills r3..r10 with context..format and leaves type/pixels on the
# stack. PowerOpen (vbcc/StormC PPC) clients place those two stack
# arguments at 56(r1)/60(r1); SysV clients (GCC, this DLL) at
# 8(r1)/12(r1). The frontend entry points take no context, so these
# trampolines also drop context and shift the register arguments down.
#
# Layout selection: dllClientStackType is used when the client reported
# an explicit PPC stack type; otherwise the GL type enum (200=UBYTE,
# 235=565, 236=4444) identifies the slot. Callers of either ABI are
# accepted and forwarded in the SysV layout.

	.text
	.align 2
	.globl R200PowerTexImage2D
	.type R200PowerTexImage2D,@function
R200PowerTexImage2D:
	mflr	0
	mr	11,1
	stwu	1,-48(1)
	stw	0,52(1)
	lis	0,dllClientStackType@ha
	lwz	0,dllClientStackType@l(0)
	cmplwi	0,32			# DLLSTACK_SYSV
	beq	1f
	cmplwi	0,16			# DLLSTACK_POWEROPEN
	beq	2f
	lwz	0,8(11)
	cmplwi	0,200
	beq	1f
	cmplwi	0,235
	beq	1f
	cmplwi	0,236
	beq	1f
	lwz	0,56(11)
	cmplwi	0,200
	beq	2f
	cmplwi	0,235
	beq	2f
	cmplwi	0,236
	beq	2f
1:	lwz	0,8(11)			# SysV: type, pixels
	lwz	12,12(11)
	b	3f
2:	lwz	0,56(11)		# PowerOpen: type, pixels
	lwz	12,60(11)
3:	stw	12,8(1)			# 9th frontend argument
	mr	3,4			# drop context, shift target..format
	mr	4,5
	mr	5,6
	mr	6,7
	mr	7,8
	mr	8,9
	mr	9,10
	mr	10,0			# type
	bl	R200PPCDispatchTexImage2D
	lwz	0,52(1)
	addi	1,1,48
	mtlr	0
	blr
	.size	R200PowerTexImage2D,.-R200PowerTexImage2D

	.globl R200PowerTexSubImage2D
	.type	R200PowerTexSubImage2D,@function
R200PowerTexSubImage2D:
	mflr	0
	mr	11,1
	stwu	1,-48(1)
	stw	0,52(1)
	lis	0,dllClientStackType@ha
	lwz	0,dllClientStackType@l(0)
	cmplwi	0,32			# DLLSTACK_SYSV
	beq	1f
	cmplwi	0,16			# DLLSTACK_POWEROPEN
	beq	2f
	lwz	0,8(11)
	cmplwi	0,200
	beq	1f
	cmplwi	0,235
	beq	1f
	cmplwi	0,236
	beq	1f
	lwz	0,56(11)
	cmplwi	0,200
	beq	2f
	cmplwi	0,235
	beq	2f
	cmplwi	0,236
	beq	2f
1:	lwz	0,8(11)
	lwz	12,12(11)
	b	3f
2:	lwz	0,56(11)
	lwz	12,60(11)
3:	stw	12,8(1)
	mr	3,4
	mr	4,5
	mr	5,6
	mr	6,7
	mr	7,8
	mr	8,9
	mr	9,10
	mr	10,0
	bl	R200PPCDispatchTexSubImage2D
	lwz	0,52(1)
	addi	1,1,48
	mtlr	0
	blr
	.size	R200PowerTexSubImage2D,.-R200PowerTexSubImage2D
