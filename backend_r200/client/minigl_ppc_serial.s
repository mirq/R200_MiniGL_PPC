# PPC serialization kernels for the R200 PPC frontend.
#
# The m68k build of r200_geometry.c serializes streaming vertices with
# movem block copies; the PPC build historically fell through to the
# generic per-field loop with an out-of-line FloatBits call per float.
# These kernels restore the same treatment: each flush is one
# byte-reversing block copy from the ScreenVertex array into the VRAM
# segment half, with disabled texture/fog attributes collapsed by AND
# masks (0x00000000 or 0xffffffff, symmetric under byte reversal).
#
# Byte reversal is done in registers (rlwinm/rlwimi) followed by plain
# stw stores, NOT stwbrx: measured on the reference 7410/Prometheus
# machine (p96-driver docs/10, tools/phase4), stwbrx to the VRAM aperture
# costs ~60 ns/dword while register-swapped plain stores reach ~42 ns/dw
# (write-gathering covers adjacent normal stores; byte-reversed stores
# are not gathered). r0 is used as the swap scratch; it is volatile in
# the SysV ABI and unused by these kernels.
#
# ScreenVertex layout (13 dwords): x,y,z,rhw,nx,ny,nz,color,s,t,s1,t1,fog
# Record layouts (little-endian words, what the VAP fetches):
#   lit full    13 dwords: 0..12 in place,      d8,d9<-m0 d10,d11<-m1 d12<-mf
#   lit compact 10 dwords: 0..9  in place,      d8,d9<-m0
#   TCL full    10 dwords: 0,1,2,3,7,8,9,10,11,12 (color moves 7->4)
#   TCL compact  7 dwords: 0,1,2,3,7,8,9        (color moves 7->4)
#
# Source stride is always 13 dwords (52 bytes); destination stride is the
# record vertexDwords. All pointers are word-aligned.
#
# r3=destination r4=source r5=count r6=unit0Mask r7=unit1Mask r8=fogMask
# Scratch: r9 (destination cursor), r10 (source cursor), r3-r5 after the
# setup (freed once loaded into r9/r10/ctr), r11-r12, r14-r21 (saved).

        .text
        .balign 16
        .global MGLPPCSwapLit13
        .global MGLPPCSwapLit10
        .global MGLPPCSwapTCL10
        .global MGLPPCSwapTCL7

# void MGLPPCSwapLit13(ULONG *dst, const ULONG *src, ULONG count,
#                      ULONG unit0Mask, ULONG unit1Mask, ULONG fogMask)
MGLPPCSwapLit13:
        cmpwi   r5,0
        beqlr
        stwu    r1,-96(r1)
        stmw    r14,8(r1)
        mr      r9,r3
        mr      r10,r4
        mtctr   r5
.l13:
        lwz     r11,0(r10)
        lwz     r12,4(r10)
        lwz     r3,8(r10)
        lwz     r4,12(r10)
        lwz     r5,16(r10)
        lwz     r14,20(r10)
        lwz     r15,24(r10)
        lwz     r16,28(r10)
        lwz     r17,32(r10)
        and     r17,r17,r6
        lwz     r18,36(r10)
        and     r18,r18,r6
        lwz     r19,40(r10)
        and     r19,r19,r7
        lwz     r20,44(r10)
        and     r20,r20,r7
        lwz     r21,48(r10)
        and     r21,r21,r8
        rlwinm  r0,r11,8,0,31
        rlwimi  r0,r11,24,0,7
        rlwimi  r0,r11,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r12,8,0,31
        rlwimi  r0,r12,24,0,7
        rlwimi  r0,r12,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r3,8,0,31
        rlwimi  r0,r3,24,0,7
        rlwimi  r0,r3,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r4,8,0,31
        rlwimi  r0,r4,24,0,7
        rlwimi  r0,r4,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r5,8,0,31
        rlwimi  r0,r5,24,0,7
        rlwimi  r0,r5,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r14,8,0,31
        rlwimi  r0,r14,24,0,7
        rlwimi  r0,r14,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r15,8,0,31
        rlwimi  r0,r15,24,0,7
        rlwimi  r0,r15,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r16,8,0,31
        rlwimi  r0,r16,24,0,7
        rlwimi  r0,r16,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r17,8,0,31
        rlwimi  r0,r17,24,0,7
        rlwimi  r0,r17,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r18,8,0,31
        rlwimi  r0,r18,24,0,7
        rlwimi  r0,r18,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r19,8,0,31
        rlwimi  r0,r19,24,0,7
        rlwimi  r0,r19,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r20,8,0,31
        rlwimi  r0,r20,24,0,7
        rlwimi  r0,r20,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r21,8,0,31
        rlwimi  r0,r21,24,0,7
        rlwimi  r0,r21,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        addi    r10,r10,52
        bdnz    .l13
        lmw     r14,8(r1)
        addic   r1,r1,96
        blr

# void MGLPPCSwapLit10(ULONG *dst, const ULONG *src, ULONG count,
#                      ULONG unit0Mask)
MGLPPCSwapLit10:
        cmpwi   r5,0
        beqlr
        stwu    r1,-96(r1)
        stmw    r14,8(r1)
        mr      r9,r3
        mr      r10,r4
        mtctr   r5
.l10:
        lwz     r11,0(r10)
        lwz     r12,4(r10)
        lwz     r3,8(r10)
        lwz     r4,12(r10)
        lwz     r5,16(r10)
        lwz     r14,20(r10)
        lwz     r15,24(r10)
        lwz     r16,28(r10)
        lwz     r17,32(r10)
        and     r17,r17,r6
        lwz     r18,36(r10)
        and     r18,r18,r6
        rlwinm  r0,r11,8,0,31
        rlwimi  r0,r11,24,0,7
        rlwimi  r0,r11,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r12,8,0,31
        rlwimi  r0,r12,24,0,7
        rlwimi  r0,r12,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r3,8,0,31
        rlwimi  r0,r3,24,0,7
        rlwimi  r0,r3,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r4,8,0,31
        rlwimi  r0,r4,24,0,7
        rlwimi  r0,r4,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r5,8,0,31
        rlwimi  r0,r5,24,0,7
        rlwimi  r0,r5,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r14,8,0,31
        rlwimi  r0,r14,24,0,7
        rlwimi  r0,r14,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r15,8,0,31
        rlwimi  r0,r15,24,0,7
        rlwimi  r0,r15,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r16,8,0,31
        rlwimi  r0,r16,24,0,7
        rlwimi  r0,r16,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r17,8,0,31
        rlwimi  r0,r17,24,0,7
        rlwimi  r0,r17,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r18,8,0,31
        rlwimi  r0,r18,24,0,7
        rlwimi  r0,r18,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        addi    r10,r10,52
        bdnz    .l10
        lmw     r14,8(r1)
        addic   r1,r1,96
        blr

# void MGLPPCSwapTCL10(ULONG *dst, const ULONG *src, ULONG count,
#                      ULONG unit0Mask, ULONG unit1Mask, ULONG fogMask)
# Plain TCL: the record colour dword comes from source lane 7; the
# object-space normal lanes (4,5,6) are not fetched at all.
MGLPPCSwapTCL10:
        cmpwi   r5,0
        beqlr
        stwu    r1,-96(r1)
        stmw    r14,8(r1)
        mr      r9,r3
        mr      r10,r4
        mtctr   r5
.tcl10:
        lwz     r11,0(r10)
        lwz     r12,4(r10)
        lwz     r3,8(r10)
        lwz     r4,12(r10)
        lwz     r16,28(r10)
        lwz     r17,32(r10)
        and     r17,r17,r6
        lwz     r18,36(r10)
        and     r18,r18,r6
        lwz     r19,40(r10)
        and     r19,r19,r7
        lwz     r20,44(r10)
        and     r20,r20,r7
        lwz     r21,48(r10)
        and     r21,r21,r8
        rlwinm  r0,r11,8,0,31
        rlwimi  r0,r11,24,0,7
        rlwimi  r0,r11,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r12,8,0,31
        rlwimi  r0,r12,24,0,7
        rlwimi  r0,r12,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r3,8,0,31
        rlwimi  r0,r3,24,0,7
        rlwimi  r0,r3,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r4,8,0,31
        rlwimi  r0,r4,24,0,7
        rlwimi  r0,r4,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r16,8,0,31
        rlwimi  r0,r16,24,0,7
        rlwimi  r0,r16,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r17,8,0,31
        rlwimi  r0,r17,24,0,7
        rlwimi  r0,r17,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r18,8,0,31
        rlwimi  r0,r18,24,0,7
        rlwimi  r0,r18,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r19,8,0,31
        rlwimi  r0,r19,24,0,7
        rlwimi  r0,r19,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r20,8,0,31
        rlwimi  r0,r20,24,0,7
        rlwimi  r0,r20,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r21,8,0,31
        rlwimi  r0,r21,24,0,7
        rlwimi  r0,r21,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        addi    r10,r10,52
        bdnz    .tcl10
        lmw     r14,8(r1)
        addic   r1,r1,96
        blr

# void MGLPPCSwapTCL7(ULONG *dst, const ULONG *src, ULONG count,
#                     ULONG unit0Mask)
MGLPPCSwapTCL7:
        cmpwi   r5,0
        beqlr
        stwu    r1,-96(r1)
        stmw    r14,8(r1)
        mr      r9,r3
        mr      r10,r4
        mtctr   r5
.tcl7:
        lwz     r11,0(r10)
        lwz     r12,4(r10)
        lwz     r3,8(r10)
        lwz     r4,12(r10)
        lwz     r16,28(r10)
        lwz     r17,32(r10)
        and     r17,r17,r6
        lwz     r18,36(r10)
        and     r18,r18,r6
        rlwinm  r0,r11,8,0,31
        rlwimi  r0,r11,24,0,7
        rlwimi  r0,r11,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r12,8,0,31
        rlwimi  r0,r12,24,0,7
        rlwimi  r0,r12,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r3,8,0,31
        rlwimi  r0,r3,24,0,7
        rlwimi  r0,r3,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r4,8,0,31
        rlwimi  r0,r4,24,0,7
        rlwimi  r0,r4,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r16,8,0,31
        rlwimi  r0,r16,24,0,7
        rlwimi  r0,r16,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r17,8,0,31
        rlwimi  r0,r17,24,0,7
        rlwimi  r0,r17,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        rlwinm  r0,r18,8,0,31
        rlwimi  r0,r18,24,0,7
        rlwimi  r0,r18,24,16,23
        stw     r0,0(r9)
        addi    r9,r9,4
        addi    r10,r10,52
        bdnz    .tcl7
        lmw     r14,8(r1)
        addic   r1,r1,96
        blr
