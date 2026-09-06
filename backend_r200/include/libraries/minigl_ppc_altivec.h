#ifndef LIBRARIES_MINIGL_PPC_ALTIVEC_H
#define LIBRARIES_MINIGL_PPC_ALTIVEC_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * useAltiVec may only be TRUE after the caller has established that the
 * current PPC and WarpOS installation support and preserve vector state.
 * Float operands must be at least 4-byte aligned. The public entry points use
 * their scalar implementation unless every vector operand is 16-byte aligned.
 * Copy uses memcpy overlap rules; vec4 output may equal input but must not
 * otherwise overlap it or alias matrix.
 */
void MGLPPCVectorCopy(void *destination, const void *source, ULONG bytes,
                      BOOL useAltiVec);
void MGLPPCMat4Multiply(float *output, const float *left,
                         const float *right, BOOL useAltiVec);
void MGLPPCVec4TransformArray(float *output, const float *matrix,
                               const float *input, ULONG vectorCount,
                               BOOL useAltiVec);

typedef struct MGLPPCAltivecStats {
    ULONG vectorCopies;
    ULONG scalarCopies;
} MGLPPCAltivecStats;

void MGLPPCAltivecResetStats(void);
void MGLPPCAltivecReadStats(MGLPPCAltivecStats *stats);

/* Scalar references are exported for validation and non-vector clients. */
void MGLPPCMat4MultiplyScalar(float *output, const float *left,
                              const float *right);
void MGLPPCVec4TransformArrayScalar(float *output, const float *matrix,
                                    const float *input, ULONG vectorCount);

#ifdef __cplusplus
}
#endif

#endif
