        .text
        .balign 16
        .global _MGLPPCAltivecLoadState
        .global _MGLPPCAltivecStoreState
        .global _MGLPPCAltivecAdd4
        .global _MGLPPCAltivecGetVRSAVE
        .global _MGLPPCAltivecSetVRSAVE

_MGLPPCAltivecGetVRSAVE:
        mfspr   r3,vrsave
        blr

_MGLPPCAltivecSetVRSAVE:
        mtspr   vrsave,r3
        blr

# void MGLPPCAltivecLoadState(const ULONG *state)
_MGLPPCAltivecLoadState:
        li      r4,-1
        mtspr   vrsave,r4
        li      r4,0
        lvx     v20,r4,r3
        addi    r3,r3,16
        lvx     v21,r4,r3
        addi    r3,r3,16
        lvx     v22,r4,r3
        addi    r3,r3,16
        lvx     v23,r4,r3
        addi    r3,r3,16
        lvx     v24,r4,r3
        addi    r3,r3,16
        lvx     v25,r4,r3
        addi    r3,r3,16
        lvx     v26,r4,r3
        addi    r3,r3,16
        lvx     v27,r4,r3
        addi    r3,r3,16
        lvx     v28,r4,r3
        addi    r3,r3,16
        lvx     v29,r4,r3
        addi    r3,r3,16
        lvx     v30,r4,r3
        addi    r3,r3,16
        lvx     v31,r4,r3
        blr

# void MGLPPCAltivecStoreState(ULONG *state)
_MGLPPCAltivecStoreState:
        li      r4,0
        stvx    v20,r4,r3
        addi    r3,r3,16
        stvx    v21,r4,r3
        addi    r3,r3,16
        stvx    v22,r4,r3
        addi    r3,r3,16
        stvx    v23,r4,r3
        addi    r3,r3,16
        stvx    v24,r4,r3
        addi    r3,r3,16
        stvx    v25,r4,r3
        addi    r3,r3,16
        stvx    v26,r4,r3
        addi    r3,r3,16
        stvx    v27,r4,r3
        addi    r3,r3,16
        stvx    v28,r4,r3
        addi    r3,r3,16
        stvx    v29,r4,r3
        addi    r3,r3,16
        stvx    v30,r4,r3
        addi    r3,r3,16
        stvx    v31,r4,r3
        blr

# void MGLPPCAltivecAdd4(float *out, const float *a, const float *b)
_MGLPPCAltivecAdd4:
        li      r6,0
        lvx     v0,r6,r4
        lvx     v1,r6,r5
        vaddfp  v2,v0,v1
        stvx    v2,r6,r3
        blr
