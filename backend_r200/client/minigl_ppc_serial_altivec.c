/* PPC vertex serialization staging for the R200 frontend.
 *
 * The scalar MGLPPCSwap* kernels in minigl_ppc_serial.s were written to
 * byte-reverse each vertex straight into the VRAM aperture. Measured on
 * the physical 7410 that costs about 27 cycles per 4-byte store: the
 * aperture is a posted-write bridge mapping and every store is its own
 * transaction. This unit keeps those kernels byte-for-byte but points
 * them at a 16-byte-aligned cacheable scratch buffer, then moves the
 * result to the real destination with 16-byte stvx blocks
 * (MGLPPCVecCopyUnaligned), cutting the aperture transaction count about
 * 4x. Output bytes are identical by construction.
 *
 * AltiVec is never mandatory: the CPU is probed once through WarpOS
 * GetInfo, the scalar kernels stay linked and selected when no G4-class
 * CPU is present, and MGLPPC_NOALTIVEC=1 forces the scalar path for A/B.
 *
 * MGLPPC_ALTIVEC_VERIFY=1 (and MGLPPC_VERBOSE=1 for the output) reads the
 * written bytes back from the aperture and compares them with the scratch
 * for the first flushes and periodically afterwards.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/dos.h>
#include <utility/tagitem.h>
#include <powerpc/powerpc.h>
#include <powerpc/powerpc_protos.h>

/* Scalar kernels, minigl_ppc_serial.s. Source stride is always 13 dwords;
 * the destination stride is the record's vertexDwords. */
extern void MGLPPCSwapLit13(ULONG *destination, const ULONG *source,
                            ULONG count, ULONG unit0Mask, ULONG unit1Mask,
                            ULONG fogMask);
extern void MGLPPCSwapLit10(ULONG *destination, const ULONG *source,
                            ULONG count, ULONG unit0Mask);
extern void MGLPPCSwapTCL10(ULONG *destination, const ULONG *source,
                            ULONG count, ULONG unit0Mask, ULONG unit1Mask,
                            ULONG fogMask);
extern void MGLPPCSwapTCL7(ULONG *destination, const ULONG *source,
                           ULONG count, ULONG unit0Mask);

/* minigl_ppc_serial_altivec.s. Destination must be 4-byte aligned, source
 * 16-byte aligned, bytes a multiple of 4; the source needs 32 bytes of
 * readable padding behind it. */
extern void MGLPPCVecCopyUnaligned(APTR destination, const void *source,
                                   ULONG bytes);

/* Console diagnostics (verbose-gated in minigl_r200_warpos_compile_probe.c). */
extern void R200PPCConsolePrintf(const char *format, ...);

/* The DLL's own stdout is a separate libc instance and never reaches the
 * client's redirected output, so probe/verify results go to a small file.
 * Only written when the probe or verify mode runs. */
#define MGLPPC_ALTIVEC_LOG "DH2:mglppc_av.txt"

extern struct DosLibrary *DOSBase;

static void AltiVecLog(const char *label, ULONG value)
{
    char buffer[96];
    ULONG length = 0;
    ULONG shift;
    BPTR file;

    while (*label && length < 80UL)
        buffer[length++] = *label++;
    buffer[length++] = '=';
    for (shift = 28; shift < 32; shift -= 4) {
        ULONG nibble = (value >> shift) & 15UL;
        buffer[length++] = (char)(nibble < 10UL ? '0' + nibble
                                               : 'A' + nibble - 10UL);
    }
    buffer[length++] = '\n';
    file = Open((STRPTR)MGLPPC_ALTIVEC_LOG, MODE_OLDFILE);
    if (!file)
        return;
    Seek(file, 0, OFFSET_END);
    (void)Write(file, buffer, length);
    Close(file);
}

static void AltiVecLogReset(const char *label, ULONG value)
{
    BPTR file = Open((STRPTR)MGLPPC_ALTIVEC_LOG, MODE_NEWFILE);
    if (file)
        Close(file);
    AltiVecLog(label, value);
}

/* The scratch holds one chunk; the last shifted block over-reads a full
 * quadword, hence the padding. 8 KiB keeps a whole gears flush (about 110
 * vertices) in one chunk. */
#define MGLPPC_VERTEX_SCRATCH_BYTES 8192UL
#define MGLPPC_VERTEX_SCRATCH_PAD   32UL
#define MGLPPC_VERTEX_VERIFY_FIRST  64UL

/* Self-test buffers: the copy kernel is exercised once against a cacheable
 * destination at every destination alignment and several lengths. This
 * isolates kernel correctness from the VRAM aperture and the geometry
 * path. */
#define MGLPPC_COPY_TEST_BYTES 1024UL

/* The WarpOS loader does not guarantee section alignment (the same reason
 * the original .rodata shift table was wrong), so every buffer the vector
 * kernel reads or writes gets a runtime 16-byte-aligned pointer. The +15
 * slack covers the worst-case raw address. */
static UBYTE VertexScratchRaw[MGLPPC_VERTEX_SCRATCH_BYTES +
                              MGLPPC_VERTEX_SCRATCH_PAD + 16UL]
    __attribute__((aligned(16)));
static UBYTE CopyTestSourceRaw[MGLPPC_COPY_TEST_BYTES + 32UL + 16UL]
    __attribute__((aligned(16)));
static UBYTE CopyTestDestinationRaw[MGLPPC_COPY_TEST_BYTES + 64UL + 16UL]
    __attribute__((aligned(16)));
static UBYTE CopyReplayDestinationRaw[MGLPPC_VERTEX_SCRATCH_BYTES + 64UL +
                                      16UL]
    __attribute__((aligned(16)));

static UBYTE *VertexScratch;
static UBYTE *CopyTestSource;
static UBYTE *CopyTestDestination;
static UBYTE *CopyReplayDestination;
static BOOL MGLPPCAltiVecBuffersReady;
static BOOL MGLPPCAltiVecSelfTestPassed;

static void AltiVecAlignBuffers(void)
{
    if (MGLPPCAltiVecBuffersReady)
        return;
    MGLPPCAltiVecBuffersReady = TRUE;
    VertexScratch = (UBYTE *)(((ULONG)VertexScratchRaw + 15UL) & ~15UL);
    CopyTestSource = (UBYTE *)(((ULONG)CopyTestSourceRaw + 15UL) & ~15UL);
    CopyTestDestination =
        (UBYTE *)(((ULONG)CopyTestDestinationRaw + 15UL) & ~15UL);
    CopyReplayDestination =
        (UBYTE *)(((ULONG)CopyReplayDestinationRaw + 15UL) & ~15UL);
}

BOOL MGLPPCAltiVecVertexEnabled = FALSE;

static BOOL MGLPPCAltiVecProbed = FALSE;
static BOOL MGLPPCAltiVecVerify = FALSE;
static ULONG MGLPPCAltiVecVerifyFlushes;
static ULONG MGLPPCAltiVecVerifyFailures;
static BOOL MGLPPCAltiVecVerifyDone;
#define MGLPPC_VERIFY_MAX_FAILURES 8UL

/* One-shot kernel correctness test against a cacheable destination:
 * every destination alignment and lengths from 4 to 1 KiB. */
static BOOL AltiVecCopySelfTest(void)
{
    ULONG index, alignment, length;

    MGLPPCAltiVecSelfTestPassed = FALSE;
    for (index = 0; index < MGLPPC_COPY_TEST_BYTES + 32UL; ++index)
        CopyTestSource[index] = (UBYTE)(index * 7UL + 3UL);
    for (alignment = 0; alignment < 16UL; alignment += 4UL) {
        for (length = 4; length <= MGLPPC_COPY_TEST_BYTES; length += 4UL) {
            for (index = 0; index < MGLPPC_COPY_TEST_BYTES + 64UL; ++index)
                CopyTestDestination[index] = 0xA5;
            MGLPPCVecCopyUnaligned(CopyTestDestination + alignment,
                                   CopyTestSource, length);
            for (index = 0; index < length; ++index) {
                if (CopyTestDestination[alignment + index] !=
                    CopyTestSource[index]) {
                    AltiVecLog("SELFTEST_FAIL_ALIGN", alignment);
                    AltiVecLog("SELFTEST_FAIL_LEN", length);
                    AltiVecLog("SELFTEST_FAIL_OFFSET", index);
                    AltiVecLog("SELFTEST_FAIL_WANT",
                               CopyTestSource[index]);
                    AltiVecLog("SELFTEST_FAIL_GOT",
                               CopyTestDestination[alignment + index]);
                    return FALSE;
                }
            }
            if (CopyTestDestination[alignment + length] != 0xA5) {
                AltiVecLog("SELFTEST_FAIL_TAIL_ALIGN", alignment);
                AltiVecLog("SELFTEST_FAIL_TAIL_LEN", length);
                return FALSE;
            }
            if (alignment > 0 && CopyTestDestination[alignment - 1] != 0xA5) {
                AltiVecLog("SELFTEST_FAIL_HEAD_ALIGN", alignment);
                AltiVecLog("SELFTEST_FAIL_HEAD_LEN", length);
                return FALSE;
            }
        }
    }
    MGLPPCAltiVecSelfTestPassed = TRUE;
    return TRUE;
}

/* True when the value is a G4-class AltiVec CPU.
 *
 * powerpc.library 17.15 GetInfo(GETINFO_CPU) does not return the CPUB_*
 * enumeration documented by the WarpOS 14.1 headers; its handler writes
 * one of three CPU-class flag words. The validated MPC7410 returns
 * 0x00C00000 (bit 22 set); the related 0x00400000 case also sets it while
 * 0x00200000 (the lower class) does not. Accept the documented enum too
 * in case another library version follows the header. Only the 7410 has
 * been validated on hardware; MGLPPC_NOALTIVEC is the safety valve and
 * MGLPPC_ALTIVEC forces the vector path on a known machine. */
static BOOL AltiVecCpuValue(ULONG cpu)
{
    if (cpu == CPUB_G4 || cpu == CPUB_7410 || cpu == CPUB_7441)
        return TRUE;
    return (cpu & 0x00400000UL) != 0UL;
}

void MGLPPCAltiVecVertexProbe(void)
{
    UBYTE flag[8];
    struct TagItem tags[2];
    ULONG cpu = 0;
    ULONG pvr = 0;
    ULONG pointerCpu = 0xDEADBEEFUL;
    BOOL forced = FALSE;
    BOOL overridden = FALSE;
    BOOL verify = FALSE;

    if (MGLPPCAltiVecProbed)
        return;
    MGLPPCAltiVecProbed = TRUE;
    MGLPPCAltiVecVertexEnabled = FALSE;

    flag[0] = 0;
    if (GetVar((STRPTR)"MGLPPC_NOALTIVEC", (STRPTR)flag,
               (LONG)sizeof(flag), GVF_GLOBAL_ONLY) > 0 && flag[0])
        forced = TRUE;
    flag[0] = 0;
    if (!forced &&
        GetVar((STRPTR)"MGLPPC_ALTIVEC", (STRPTR)flag,
               (LONG)sizeof(flag), GVF_GLOBAL_ONLY) > 0 && flag[0])
        overridden = TRUE;

    if (!forced) {
        /* Variant 1: the value is returned in ti_Data. */
        tags[0].ti_Tag = GETINFO_CPU;
        tags[0].ti_Data = 0;
        tags[1].ti_Tag = TAG_DONE;
        tags[1].ti_Data = 0;
        (void)GetInfo(tags);
        cpu = tags[0].ti_Data;

        /* Variant 2: some library versions write through ti_Data. */
        if (!AltiVecCpuValue(cpu)) {
            tags[0].ti_Tag = GETINFO_CPU;
            tags[0].ti_Data = (ULONG)&pointerCpu;
            tags[1].ti_Tag = TAG_DONE;
            tags[1].ti_Data = 0;
            (void)GetInfo(tags);
        }

        tags[0].ti_Tag = GETINFO_PVR;
        tags[0].ti_Data = 0;
        tags[1].ti_Tag = TAG_DONE;
        tags[1].ti_Data = 0;
        (void)GetInfo(tags);
        pvr = tags[0].ti_Data;

        if (AltiVecCpuValue(cpu) || AltiVecCpuValue(pointerCpu))
            MGLPPCAltiVecVertexEnabled = TRUE;
    }
    if (overridden)
        MGLPPCAltiVecVertexEnabled = TRUE;

    if (MGLPPCAltiVecVertexEnabled) {
        flag[0] = 0;
        if (GetVar((STRPTR)"MGLPPC_ALTIVEC_VERIFY", (STRPTR)flag,
                   (LONG)sizeof(flag), GVF_GLOBAL_ONLY) > 0 && flag[0]) {
            MGLPPCAltiVecVerify = TRUE;
            verify = TRUE;
        }
    }
    AltiVecLogReset("PROBE_CPU", cpu);
    AltiVecLog("PROBE_POINTER_CPU", pointerCpu);
    AltiVecLog("PROBE_PVR", pvr);
    AltiVecLog("PROBE_BASE", (ULONG)PowerPCBase);
    AltiVecLog("PROBE_FORCED", forced ? 1UL : 0UL);
    AltiVecLog("PROBE_OVERRIDE", overridden ? 1UL : 0UL);
    AltiVecLog("PROBE_VERIFY", verify ? 1UL : 0UL);
    if (MGLPPCAltiVecVertexEnabled) {
        AltiVecAlignBuffers();
        AltiVecLog("BUFFER_ALIGN", (ULONG)VertexScratch & 15UL);
        AltiVecLog("SELFTEST", AltiVecCopySelfTest() ? 1UL : 0UL);
        if (!MGLPPCAltiVecSelfTestPassed) {
            /* A misaligned or otherwise unusable vector path must never
             * corrupt records: fall back to the scalar kernels. */
            MGLPPCAltiVecVertexEnabled = FALSE;
            AltiVecLog("SELFTEST_FALLBACK", 1UL);
        }
    }
    AltiVecLog("PROBE_ENABLED", MGLPPCAltiVecVertexEnabled ? 1UL : 0UL);
}

/* Copy one staged chunk to the aperture. In verify mode the same copy is
 * replayed into a cacheable buffer at the same alignment (isolating the
 * kernel from the aperture), then the aperture bytes are read back after a
 * sync and compared. */
static void VectorCopyChunk(UBYTE *destination, const UBYTE *source,
                            ULONG bytes)
{
    ULONG offset;

    MGLPPCVecCopyUnaligned(destination, source, bytes);
    if (!MGLPPCAltiVecVerify || MGLPPCAltiVecVerifyDone)
        return;
    ++MGLPPCAltiVecVerifyFlushes;
    if (MGLPPCAltiVecVerifyFlushes > MGLPPC_VERTEX_VERIFY_FIRST &&
        (MGLPPCAltiVecVerifyFlushes & 255UL) != 0UL)
        return;

    /* 1. Cacheable replay at the same destination alignment. */
    if (bytes <= MGLPPC_VERTEX_SCRATCH_BYTES) {
        UBYTE *replay = CopyReplayDestination +
                        ((ULONG)destination & 15UL);
        BOOL cacheOk = TRUE;
        MGLPPCVecCopyUnaligned(replay, source, bytes);
        for (offset = 0; offset < bytes; offset += 4UL) {
            if (*(const ULONG *)(replay + offset) !=
                *(const ULONG *)(source + offset)) {
                cacheOk = FALSE;
                if (MGLPPCAltiVecVerifyFailures < MGLPPC_VERIFY_MAX_FAILURES) {
                    ++MGLPPCAltiVecVerifyFailures;
                    AltiVecLog("VERIFY_CACHE_FAIL_FLUSH",
                               MGLPPCAltiVecVerifyFlushes);
                    AltiVecLog("VERIFY_CACHE_FAIL_OFFSET", offset);
                    AltiVecLog("VERIFY_CACHE_FAIL_WANT",
                               *(const ULONG *)(source + offset));
                    AltiVecLog("VERIFY_CACHE_FAIL_GOT",
                               *(const ULONG *)(replay + offset));
                }
                break;
            }
        }
        if (cacheOk && MGLPPCAltiVecVerifyFlushes == 1UL)
            AltiVecLog("VERIFY_CACHE_OK", bytes);
    }

    /* 2. Aperture read-back after ordering. */
    __asm__ __volatile__ ("sync" ::: "memory");
    for (offset = 0; offset < bytes; offset += 4UL) {
        ULONG expected = *(const ULONG *)(source + offset);
        ULONG actual = *(volatile ULONG *)(destination + offset);
        if (actual != expected) {
            if (MGLPPCAltiVecVerifyFailures < MGLPPC_VERIFY_MAX_FAILURES) {
                ULONG window = offset & ~15UL;
                ++MGLPPCAltiVecVerifyFailures;
                AltiVecLog("VERIFY_FAIL_FLUSH", MGLPPCAltiVecVerifyFlushes);
                AltiVecLog("VERIFY_FAIL_OFFSET", offset);
                AltiVecLog("VERIFY_FAIL_WANT", expected);
                AltiVecLog("VERIFY_FAIL_GOT", actual);
                AltiVecLog("VERIFY_FAIL_SRC0",
                           *(const ULONG *)(source + window));
                AltiVecLog("VERIFY_FAIL_DST0",
                           *(volatile ULONG *)(destination + window));
                AltiVecLog("VERIFY_FAIL_DST1",
                           *(volatile ULONG *)(destination + window + 4UL));
                AltiVecLog("VERIFY_FAIL_DST2",
                           *(volatile ULONG *)(destination + window + 8UL));
                AltiVecLog("VERIFY_FAIL_DST3",
                           *(volatile ULONG *)(destination + window + 12UL));
            }
            if (MGLPPCAltiVecVerifyFailures >= MGLPPC_VERIFY_MAX_FAILURES)
                MGLPPCAltiVecVerifyDone = TRUE;
        }
    }
    if (MGLPPCAltiVecVerifyFailures == 0UL &&
        (MGLPPCAltiVecVerifyFlushes == 1UL ||
         (MGLPPCAltiVecVerifyFlushes & 1023UL) == 0UL)) {
        AltiVecLog("VERIFY_OK_FLUSH", MGLPPCAltiVecVerifyFlushes);
        AltiVecLog("VERIFY_OK_BYTES", bytes);
    }
}

void MGLPPCSwapLit13Vector(ULONG *destination, const ULONG *source,
                           ULONG count, ULONG unit0Mask, ULONG unit1Mask,
                           ULONG fogMask)
{
    const ULONG chunk = MGLPPC_VERTEX_SCRATCH_BYTES / (13UL * 4UL);

    while (count) {
        ULONG n = count < chunk ? count : chunk;
        ULONG bytes = n * 13UL * 4UL;
        MGLPPCSwapLit13((ULONG *)VertexScratch, source, n,
                        unit0Mask, unit1Mask, fogMask);
        VectorCopyChunk((UBYTE *)destination, VertexScratch, bytes);
        destination += n * 13UL;
        source += n * 13UL;
        count -= n;
    }
}

void MGLPPCSwapLit10Vector(ULONG *destination, const ULONG *source,
                           ULONG count, ULONG unit0Mask)
{
    const ULONG chunk = MGLPPC_VERTEX_SCRATCH_BYTES / (10UL * 4UL);

    while (count) {
        ULONG n = count < chunk ? count : chunk;
        ULONG bytes = n * 10UL * 4UL;
        MGLPPCSwapLit10((ULONG *)VertexScratch, source, n, unit0Mask);
        VectorCopyChunk((UBYTE *)destination, VertexScratch, bytes);
        destination += n * 10UL;
        source += n * 13UL;
        count -= n;
    }
}

void MGLPPCSwapTCL10Vector(ULONG *destination, const ULONG *source,
                           ULONG count, ULONG unit0Mask, ULONG unit1Mask,
                           ULONG fogMask)
{
    const ULONG chunk = MGLPPC_VERTEX_SCRATCH_BYTES / (10UL * 4UL);

    while (count) {
        ULONG n = count < chunk ? count : chunk;
        ULONG bytes = n * 10UL * 4UL;
        MGLPPCSwapTCL10((ULONG *)VertexScratch, source, n,
                        unit0Mask, unit1Mask, fogMask);
        VectorCopyChunk((UBYTE *)destination, VertexScratch, bytes);
        destination += n * 10UL;
        source += n * 13UL;
        count -= n;
    }
}

void MGLPPCSwapTCL7Vector(ULONG *destination, const ULONG *source,
                          ULONG count, ULONG unit0Mask)
{
    const ULONG chunk = MGLPPC_VERTEX_SCRATCH_BYTES / (7UL * 4UL);

    while (count) {
        ULONG n = count < chunk ? count : chunk;
        ULONG bytes = n * 7UL * 4UL;
        MGLPPCSwapTCL7((ULONG *)VertexScratch, source, n, unit0Mask);
        VectorCopyChunk((UBYTE *)destination, VertexScratch, bytes);
        destination += n * 7UL;
        source += n * 13UL;
        count -= n;
    }
}
