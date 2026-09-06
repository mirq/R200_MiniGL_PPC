        .text
        .balign 16
        .global _MGLPPCRawAltivecCopy16
        .global _MGLPPCRawAltivecMat4Multiply
        .global _MGLPPCRawAltivecVec4TransformArray

# All pointers passed to these raw kernels are 16-byte aligned. Public C
# wrappers enforce that contract and provide scalar fallbacks.

# void MGLPPCRawAltivecCopy16(void *dst, const void *src, ULONG blocks)
_MGLPPCRawAltivecCopy16:
        cmpwi   r5,0
        beqlr
        mfspr   r11,vrsave
        li      r0,-1
        mtspr   vrsave,r0
        li      r6,0
        mtctr   r5
.copy16_loop:
        lvx     v0,r6,r4
        stvx    v0,r6,r3
        addi    r4,r4,16
        addi    r3,r3,16
        bdnz    .copy16_loop
        mtspr   vrsave,r11
        blr

# void MGLPPCRawAltivecMat4Multiply(float *out, const float *left,
#                                   const float *right)
_MGLPPCRawAltivecMat4Multiply:
        mfspr   r11,vrsave
        li      r0,-1
        mtspr   vrsave,r0
        li      r6,0
        lvx     v0,r6,r4
        addi    r4,r4,16
        lvx     v1,r6,r4
        addi    r4,r4,16
        lvx     v2,r6,r4
        addi    r4,r4,16
        lvx     v3,r6,r4
        li      r7,4
        mtctr   r7
.mat4_column:
        lvx     v4,r6,r5
        vspltw  v5,v4,0
        vspltw  v6,v4,1
        vspltw  v7,v4,2
        vspltw  v8,v4,3
        vxor    v9,v9,v9
        # vasm syntax is VD,VA,VB,VC; VMADDFP computes VA * VC + VB.
	vmaddfp v9,v0,v9,v5
	vmaddfp v9,v1,v9,v6
	vmaddfp v9,v2,v9,v7
	vmaddfp v9,v3,v9,v8
        stvx    v9,r6,r3
        addi    r5,r5,16
        addi    r3,r3,16
        bdnz    .mat4_column
        mtspr   vrsave,r11
        blr

# void MGLPPCRawAltivecVec4TransformArray(float *out, const float *matrix,
#                                          const float *in, ULONG count)
_MGLPPCRawAltivecVec4TransformArray:
        cmpwi   r6,0
        beqlr
        mfspr   r11,vrsave
        li      r0,-1
        mtspr   vrsave,r0
        li      r7,0
        lvx     v0,r7,r4
        addi    r4,r4,16
        lvx     v1,r7,r4
        addi    r4,r4,16
        lvx     v2,r7,r4
        addi    r4,r4,16
        lvx     v3,r7,r4
        mtctr   r6
.transform_loop:
        lvx     v4,r7,r5
        vspltw  v5,v4,0
        vspltw  v6,v4,1
        vspltw  v7,v4,2
        vspltw  v8,v4,3
        vxor    v9,v9,v9
	vmaddfp v9,v0,v9,v5
	vmaddfp v9,v1,v9,v6
	vmaddfp v9,v2,v9,v7
	vmaddfp v9,v3,v9,v8
        stvx    v9,r7,r3
        addi    r5,r5,16
        addi    r3,r3,16
        bdnz    .transform_loop
        mtspr   vrsave,r11
        blr
