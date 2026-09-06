#include <libraries/minigl_ppc_altivec.h>

#include <string.h>

extern void MGLPPCRawAltivecCopy16(void *destination, const void *source,
                                    ULONG blocks);
extern void MGLPPCRawAltivecMat4Multiply(float *output, const float *left,
                                         const float *right);
extern void MGLPPCRawAltivecVec4TransformArray(float *output,
                                                const float *matrix,
                                                const float *input,
                                                ULONG vectorCount);

static MGLPPCAltivecStats AltivecStats;

void MGLPPCAltivecResetStats(void)
{
    memset(&AltivecStats, 0, sizeof(AltivecStats));
}

void MGLPPCAltivecReadStats(MGLPPCAltivecStats *stats)
{
    if (stats) *stats = AltivecStats;
}

void MGLPPCVectorCopy(void *destination, const void *source, ULONG bytes,
                      BOOL useAltiVec)
{
    if (useAltiVec && bytes >= 16UL && !(bytes & 15UL) &&
        !(((ULONG)destination | (ULONG)source) & 15UL)) {
        AltivecStats.vectorCopies++;
        MGLPPCRawAltivecCopy16(destination, source, bytes >> 4);
        return;
    }
    AltivecStats.scalarCopies++;
    memcpy(destination, source, bytes);
}

void MGLPPCMat4MultiplyScalar(float *output, const float *left,
                              const float *right)
{
    float result[16];
    ULONG column;

    for (column = 0; column < 4; ++column) {
        const float *value = right + column * 4;
        float *destination = result + column * 4;

        destination[0] = left[0] * value[0] + left[4] * value[1] +
                         left[8] * value[2] + left[12] * value[3];
        destination[1] = left[1] * value[0] + left[5] * value[1] +
                         left[9] * value[2] + left[13] * value[3];
        destination[2] = left[2] * value[0] + left[6] * value[1] +
                         left[10] * value[2] + left[14] * value[3];
        destination[3] = left[3] * value[0] + left[7] * value[1] +
                         left[11] * value[2] + left[15] * value[3];
    }
    memcpy(output, result, sizeof(result));
}

void MGLPPCMat4Multiply(float *output, const float *left,
                        const float *right, BOOL useAltiVec)
{
    if (useAltiVec &&
        !(((ULONG)output | (ULONG)left | (ULONG)right) & 15UL)) {
        MGLPPCRawAltivecMat4Multiply(output, left, right);
        return;
    }
    MGLPPCMat4MultiplyScalar(output, left, right);
}

void MGLPPCVec4TransformArrayScalar(float *output, const float *matrix,
                                    const float *input, ULONG vectorCount)
{
    ULONG vector, row;

    for (vector = 0; vector < vectorCount; ++vector) {
        const float *value = input + vector * 4;
        float result[4];

        for (row = 0; row < 4; ++row) {
            result[row] = matrix[row] * value[0] +
                          matrix[4 + row] * value[1] +
                          matrix[8 + row] * value[2] +
                          matrix[12 + row] * value[3];
        }
        output[vector * 4] = result[0];
        output[vector * 4 + 1] = result[1];
        output[vector * 4 + 2] = result[2];
        output[vector * 4 + 3] = result[3];
    }
}

void MGLPPCVec4TransformArray(float *output, const float *matrix,
                              const float *input, ULONG vectorCount,
                              BOOL useAltiVec)
{
    if (useAltiVec && vectorCount &&
        !(((ULONG)output | (ULONG)matrix | (ULONG)input) & 15UL)) {
        MGLPPCRawAltivecVec4TransformArray(output, matrix, input,
                                           vectorCount);
        return;
    }
    MGLPPCVec4TransformArrayScalar(output, matrix, input, vectorCount);
}
