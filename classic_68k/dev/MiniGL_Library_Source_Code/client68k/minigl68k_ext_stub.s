| minigl68k.library LVO -36: const MGL68KExt *MiniGL68KGetExt(void)
	.text
	.even
	.extern _MiniGLBase
	.extern _MiniGL68KExtOK
	.globl _MiniGL68KGetExt
_MiniGL68KGetExt:
	move.l a6,-(sp)
	tst.l _MiniGL68KExtOK
	beq.s .Lno_ext
	move.l _MiniGLBase,a6
	tst.l a6
	beq.s .Lno_ext
	jsr -36(a6)
	move.l (sp)+,a6
	rts
.Lno_ext:
	move.l (sp)+,a6
	moveq #0,d0
	rts
