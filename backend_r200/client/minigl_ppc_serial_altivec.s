# PPC AltiVec staging copy for the R200 vertex serializers.
#
# The scalar MGLPPCSwap* kernels are unchanged; the C wrappers point them
# at a 16-byte-aligned cacheable scratch buffer instead of the VRAM
# aperture, then this kernel moves that byte-exact stream to the
# destination with 16-byte stvx blocks. The aperture only accepts 4-byte
# aligned addresses (record strides are vertexDwords*4), so the copy uses
# a scalar head/tail and a vperm-shifted middle; the source scratch is
# always 16-byte aligned.
#
# void MGLPPCVecCopyUnaligned(APTR dst, const APTR src, ULONG bytes)
#   dst  : 4-byte aligned
#   src  : 16-byte aligned
#   bytes: multiple of 4
#
# The caller guarantees at least 32 bytes of readable padding behind the
# source, because the last shifted block loads a full quadword.
#
# Volatile state only: r0, r3-r12, ctr, v0-v4. VRSAVE is preserved.

        .text
        .balign 16
        .global MGLPPCVecCopyUnaligned

MGLPPCVecCopyUnaligned:
        cmpwi   r5,0
        beqlr

        # Scalar head: advance the destination to a 16-byte boundary.
        andi.   r6,r3,15
        beq     .Lmid_head_zero
        subfic  r7,r6,16                # head bytes (4..12)
        cmplw   r7,r5
        ble     .Lhead_size
        mr      r7,r5                   # copy everything scalar
.Lhead_size:
        srwi    r8,r7,2
        mr      r10,r4
        mtctr   r8
.Lhead_loop:
        lwz     r9,0(r10)
        stw     r9,0(r3)
        addi    r10,r10,4
        addi    r3,r3,4
        bdnz    .Lhead_loop
        subf    r5,r7,r5
        cmpwi   r5,0
        beqlr
        mr      r6,r7                   # source shift equals the head
        b       .Lmid

.Lmid_head_zero:
        li      r6,0                    # aligned source and destination

.Lmid:
        cmpwi   r6,0
        bne     .Lmid_shift

        # ---- aligned stream: plain lvx/stvx blocks
        srwi    r8,r5,4
        cmpwi   r8,0
        beq     .Ltail
        mfspr   r11,256                 # VRSAVE
        li      r0,-1
        mtspr   256,r0
        li      r7,0
        mtctr   r8
.Lcopy_aligned:
        lvx     v0,r7,r4
        stvx    v0,r7,r3
        addi    r7,r7,16
        bdnz    .Lcopy_aligned
        mtspr   256,r11
        slwi    r8,r8,4
        add     r4,r4,r8                # tail source
        add     r3,r3,r8                # tail destination
        subf    r5,r8,r5
        b       .Ltail

        # ---- shifted stream: carry the previous quadword through vperm
.Lmid_shift:
        cmplw   r5,r6
        bgt     .Lshift_try
        add     r4,r4,r6                # no vector blocks: skip the head
        b       .Ltail
.Lshift_try:
        subf    r8,r6,r5
        srwi    r8,r8,4                 # blocks that fit without overrun
        cmpwi   r8,0
        bne     .Lshift_blocks
        add     r4,r4,r6
        b       .Ltail
.Lshift_blocks:
        mfspr   r11,256
        li      r0,-1
        mtspr   256,r0
        # The byte-shift permute control comes from lvsl on the unaligned
        # source address: lvsl(base+shift) yields {shift, shift+1, ...,
        # shift+15}, which is exactly the merge control for a load-side
        # vperm(W(i), W(i+1), control). It needs no lookup table, and a
        # static table's runtime alignment cannot be guaranteed by the
        # WarpOS loader (lvx ignores the low address bits, which silently
        # shifted the control and the record data).
        add     r12,r4,r6               # base + shift
        lvsl    v4,0,r12                # vperm(W(i), W(i+1), v4)
        li      r7,0
        lvx     v0,0,r4                 # W0
        mtctr   r8
.Lcopy_shift:
        addi    r7,r7,16
        lvx     v1,r7,r4                # W(i+1)
        vperm   v2,v0,v1,v4
        stvx    v2,0,r3
        vor     v0,v1,v1
        addi    r3,r3,16
        bdnz    .Lcopy_shift
        mtspr   256,r11
        slwi    r8,r8,4
        add     r4,r4,r6                # base + shift
        add     r4,r4,r8                # tail source
        subf    r5,r8,r5
        # fall through

.Ltail:
        srwi    r8,r5,2
        cmpwi   r8,0
        beqlr
        mtctr   r8
.Ltail_loop:
        lwz     r9,0(r4)
        stw     r9,0(r3)
        addi    r4,r4,4
        addi    r3,r3,4
        bdnz    .Ltail_loop
        blr
