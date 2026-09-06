        .text
        .balign 16
        .global _MGLPPCPciWriteBarrier

_MGLPPCPciWriteBarrier:
        eieio
        sync
        blr
