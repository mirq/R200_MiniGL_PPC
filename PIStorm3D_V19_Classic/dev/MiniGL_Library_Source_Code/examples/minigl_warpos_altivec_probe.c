#include <exec/memory.h>
#include <exec/types.h>
#include <libraries/minigl_ppc_altivec.h>
#include <stdio.h>
#include <string.h>

#define STATE_WORDS 48UL
#define SWITCH_ITERATIONS 200UL
#define VECTOR_COUNT 256UL
#define BENCH_ITERATIONS 2000UL

struct PPCBase;
extern struct PPCBase *PowerPCBase;

APTR __AllocVecPPC(void *, ULONG, ULONG, ULONG) =
    "\tlwz\tr0,-322(r3)\n\tmtlr\tr0\n\tblrl";
LONG __FreeVecPPC(void *, APTR) =
    "\tlwz\tr0,-328(r3)\n\tmtlr\tr0\n\tblrl";
ULONG __WaitTime(void *, ULONG, ULONG) =
    "\tlwz\tr0,-550(r3)\n\tmtlr\tr0\n\tblrl";

#define AllocVecPPC(size, flags, align) \
    __AllocVecPPC(PowerPCBase, (size), (flags), (align))
#define FreeVecPPC(memory) __FreeVecPPC(PowerPCBase, (memory))
#define WaitTime(signals, time) __WaitTime(PowerPCBase, (signals), (time))

extern void MGLPPCAltivecLoadState(const ULONG *state);
extern void MGLPPCAltivecStoreState(ULONG *state);
extern void MGLPPCAltivecAdd4(float *out, const float *a, const float *b);
extern ULONG MGLPPCAltivecGetVRSAVE(void);
extern void MGLPPCAltivecSetVRSAVE(ULONG value);

static ULONG RawClock(void) = "\tmftb\tr3";

static float Absolute(float value)
{
    return value < 0.0f ? -value : value;
}

static BOOL TestArithmetic(float *a, float *b, float *out)
{
    static const float expected[4] = {6.0f, 8.0f, 10.0f, 12.0f};
    ULONG index;

    a[0] = 1.0f; a[1] = 2.0f; a[2] = 3.0f; a[3] = 4.0f;
    b[0] = 5.0f; b[1] = 6.0f; b[2] = 7.0f; b[3] = 8.0f;
    MGLPPCAltivecAdd4(out, a, b);
    for (index = 0; index < 4; ++index) {
        if (out[index] != expected[index])
            return FALSE;
    }
    return TRUE;
}

static BOOL TestProductionKernels(float *memory)
{
    float *left = memory;
    float *right = left + 16;
    float *scalarMatrix = right + 16;
    float *vectorMatrix = scalarMatrix + 16;
    float *input = vectorMatrix + 16;
    float *scalarVectors = input + VECTOR_COUNT * 4;
    float *vectorVectors = scalarVectors + VECTOR_COUNT * 4;
    float unalignedInput[17];
    float unalignedOutput[17];
    ULONG index;

    for (index = 0; index < 16; ++index) {
        left[index] = (float)((LONG)(index % 7) - 3) * 0.25f;
        right[index] = (float)((LONG)(index % 5) - 2) * 0.375f;
    }
    for (index = 0; index < VECTOR_COUNT * 4; ++index)
        input[index] = (float)((LONG)(index % 31) - 15) * 0.0625f;

    MGLPPCMat4MultiplyScalar(scalarMatrix, left, right);
    MGLPPCMat4Multiply(vectorMatrix, left, right, TRUE);
    for (index = 0; index < 16; ++index) {
        if (Absolute(scalarMatrix[index] - vectorMatrix[index]) > 0.00001f) {
            printf("ALTIVEC_KERNEL_FAIL kernel=mat4 index=%lu "
                   "scalar=%08lx vector=%08lx\n",
                   (unsigned long)index,
                   (unsigned long)((ULONG *)scalarMatrix)[index],
                   (unsigned long)((ULONG *)vectorMatrix)[index]);
            return FALSE;
        }
    }

    MGLPPCVec4TransformArrayScalar(scalarVectors, left, input, VECTOR_COUNT);
    MGLPPCVec4TransformArray(vectorVectors, left, input, VECTOR_COUNT, TRUE);
    for (index = 0; index < VECTOR_COUNT * 4; ++index) {
        if (Absolute(scalarVectors[index] - vectorVectors[index]) > 0.00001f) {
            printf("ALTIVEC_KERNEL_FAIL kernel=transform index=%lu "
                   "scalar=%08lx vector=%08lx\n",
                   (unsigned long)index,
                   (unsigned long)((ULONG *)scalarVectors)[index],
                   (unsigned long)((ULONG *)vectorVectors)[index]);
            return FALSE;
        }
    }

    MGLPPCMat4MultiplyScalar(scalarVectors + 1, left + 1, right + 1);
    MGLPPCMat4Multiply(vectorVectors + 1, left + 1, right + 1, TRUE);
    for (index = 0; index < 16; ++index) {
        if (Absolute(scalarVectors[index + 1] -
                     vectorVectors[index + 1]) > 0.00001f) {
            printf("ALTIVEC_KERNEL_FAIL kernel=mat4_fallback index=%lu\n",
                   (unsigned long)index);
            return FALSE;
        }
    }
    MGLPPCVec4TransformArrayScalar(scalarVectors + 1, left + 1,
                                   input + 1, 8UL);
    MGLPPCVec4TransformArray(vectorVectors + 1, left + 1, input + 1,
                             8UL, TRUE);
    for (index = 0; index < 8UL * 4UL; ++index) {
        if (Absolute(scalarVectors[index + 1] -
                     vectorVectors[index + 1]) > 0.00001f) {
            printf("ALTIVEC_KERNEL_FAIL kernel=transform_fallback index=%lu\n",
                   (unsigned long)index);
            return FALSE;
        }
    }

    memset(vectorVectors, 0, VECTOR_COUNT * 4 * sizeof(float));
    MGLPPCVectorCopy(vectorVectors, input,
                     VECTOR_COUNT * 4 * sizeof(float), TRUE);
    if (memcmp(vectorVectors, input,
                VECTOR_COUNT * 4 * sizeof(float)) != 0) {
        for (index = 0; index < VECTOR_COUNT * 4; ++index)
            if (((ULONG *)vectorVectors)[index] != ((ULONG *)input)[index])
                break;
        printf("ALTIVEC_KERNEL_FAIL kernel=copy index=%lu expected=%08lx "
               "actual=%08lx\n",
               (unsigned long)index,
               (unsigned long)((ULONG *)input)[index],
               (unsigned long)((ULONG *)vectorVectors)[index]);
        return FALSE;
    }

    /* The public API must fall back rather than issuing an aligned lvx/stvx
     * against an arbitrary caller buffer. */
    for (index = 0; index < 17; ++index)
        unalignedInput[index] = (float)index * 0.125f;
    memset(unalignedOutput, 0, sizeof(unalignedOutput));
    MGLPPCVectorCopy(unalignedOutput + 1, unalignedInput + 1,
                     16UL * sizeof(float), TRUE);
    if (memcmp(unalignedOutput + 1, unalignedInput + 1,
                16UL * sizeof(float)) != 0) {
        printf("ALTIVEC_KERNEL_FAIL kernel=copy_unaligned_pointer\n");
        return FALSE;
    }
    memset(unalignedOutput, 0, sizeof(unalignedOutput));
    MGLPPCVectorCopy(unalignedOutput, unalignedInput,
                      15UL * sizeof(float), TRUE);
    if (memcmp(unalignedOutput, unalignedInput,
               15UL * sizeof(float)) != 0) {
        printf("ALTIVEC_KERNEL_FAIL kernel=copy_unaligned_size\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL TestVRSAVEPreservation(float *memory)
{
    ULONG previous = MGLPPCAltivecGetVRSAVE();
    ULONG expected = 0x0055aa00UL;
    BOOL passed;

    MGLPPCAltivecSetVRSAVE(expected);
    MGLPPCVectorCopy(memory + 32, memory, 64UL, TRUE);
    MGLPPCMat4Multiply(memory + 48, memory, memory + 16, TRUE);
    MGLPPCVec4TransformArray(memory + 64, memory, memory + 80, 4UL, TRUE);
    passed = MGLPPCAltivecGetVRSAVE() == expected;
    MGLPPCAltivecSetVRSAVE(previous);
    return passed;
}

static void BenchmarkProductionKernels(float *memory)
{
    float *left = memory;
    float *right = left + 16;
    float *matrixOut = right + 16;
    float *input = matrixOut + 48;
    float *output = input + VECTOR_COUNT * 4;
    ULONG scalarStart, scalarTicks, vectorStart, vectorTicks, iteration;

    scalarStart = RawClock();
    for (iteration = 0; iteration < BENCH_ITERATIONS; ++iteration)
        MGLPPCMat4Multiply(matrixOut, left, right, FALSE);
    scalarTicks = RawClock() - scalarStart;
    vectorStart = RawClock();
    for (iteration = 0; iteration < BENCH_ITERATIONS; ++iteration)
        MGLPPCMat4Multiply(matrixOut, left, right, TRUE);
    vectorTicks = RawClock() - vectorStart;
    printf("ALTIVEC_BENCH kernel=mat4 iterations=%lu scalar_ticks=%lu "
           "vector_ticks=%lu\n", (unsigned long)BENCH_ITERATIONS,
           (unsigned long)scalarTicks, (unsigned long)vectorTicks);

    scalarStart = RawClock();
    for (iteration = 0; iteration < BENCH_ITERATIONS / 8; ++iteration)
        MGLPPCVec4TransformArray(output, left, input, VECTOR_COUNT, FALSE);
    scalarTicks = RawClock() - scalarStart;
    vectorStart = RawClock();
    for (iteration = 0; iteration < BENCH_ITERATIONS / 8; ++iteration)
        MGLPPCVec4TransformArray(output, left, input, VECTOR_COUNT, TRUE);
    vectorTicks = RawClock() - vectorStart;
    printf("ALTIVEC_BENCH kernel=transform vectors=%lu iterations=%lu "
           "scalar_ticks=%lu vector_ticks=%lu\n",
           (unsigned long)VECTOR_COUNT,
           (unsigned long)(BENCH_ITERATIONS / 8),
           (unsigned long)scalarTicks, (unsigned long)vectorTicks);
}

int main(int argc, char **argv)
{
    ULONG *expected = NULL;
    ULONG *observed = NULL;
    float *arithmetic = NULL;
    float *kernelMemory = NULL;
    ULONG salt = (argc > 1 && argv[1][0] == 'B') ?
                 0xb7410000UL : 0xa7410000UL;
    ULONG iteration;
    ULONG index;
    int result = 20;

    expected = AllocVecPPC(STATE_WORDS * sizeof(ULONG),
                           MEMF_PUBLIC | MEMF_CLEAR, 16);
    observed = AllocVecPPC(STATE_WORDS * sizeof(ULONG),
                           MEMF_PUBLIC | MEMF_CLEAR, 16);
    arithmetic = AllocVecPPC(12UL * sizeof(float),
                              MEMF_PUBLIC | MEMF_CLEAR, 16);
    kernelMemory = AllocVecPPC((64UL + VECTOR_COUNT * 12UL) * sizeof(float),
                               MEMF_PUBLIC | MEMF_CLEAR, 16);
    if (!expected || !observed || !arithmetic || !kernelMemory) {
        printf("ALTIVEC_PROBE status=alloc_failed\n");
        goto done;
    }
    if (!TestArithmetic(arithmetic, arithmetic + 4, arithmetic + 8)) {
        printf("ALTIVEC_PROBE status=arithmetic_failed\n");
        goto done;
    }
    if (!TestProductionKernels(kernelMemory)) {
        printf("ALTIVEC_PROBE status=kernel_validation_failed\n");
        goto done;
    }
    if (!TestVRSAVEPreservation(kernelMemory)) {
        printf("ALTIVEC_PROBE status=vrsave_corrupt\n");
        goto done;
    }
    for (index = 0; index < STATE_WORDS; ++index)
        expected[index] = salt ^ (index * 0x01010101UL);
    for (iteration = 0; iteration < SWITCH_ITERATIONS; ++iteration) {
        MGLPPCAltivecLoadState(expected);
        (void)WaitTime(0, 1UL);
        MGLPPCAltivecStoreState(observed);
        if (memcmp(expected, observed, STATE_WORDS * sizeof(ULONG)) != 0) {
            for (index = 0; index < STATE_WORDS; ++index) {
                if (expected[index] != observed[index])
                    break;
            }
            printf("ALTIVEC_PROBE status=state_corrupt id=%c iteration=%lu "
                   "word=%lu expected=%08lx observed=%08lx\n",
                   (argc > 1) ? argv[1][0] : 'A',
                   (unsigned long)iteration, (unsigned long)index,
                   (unsigned long)expected[index],
                   (unsigned long)observed[index]);
            goto done;
        }
    }
    printf("ALTIVEC_PROBE status=ok id=%c iterations=%lu\n",
           (argc > 1) ? argv[1][0] : 'A',
           (unsigned long)SWITCH_ITERATIONS);
    BenchmarkProductionKernels(kernelMemory);
    result = 0;

done:
    if (kernelMemory) FreeVecPPC(kernelMemory);
    if (arithmetic) FreeVecPPC(arithmetic);
    if (observed) FreeVecPPC(observed);
    if (expected) FreeVecPPC(expected);
    return result;
}
