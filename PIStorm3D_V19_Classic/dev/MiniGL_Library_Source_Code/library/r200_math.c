#include "minigl_r200_internal.h"
#include <math.h>
#if defined(R200_PPC_ALTIVEC)
#include <libraries/minigl_ppc_altivec.h>
#endif

/* FloatBits lives as a static inline in minigl_r200_internal.h; it is on
 * the per-vertex serialization path and must never be an out-of-line
 * call. */

GLfloat RoundFloat32(GLfloat value)
{
   volatile GLfloat rounded = value;
   return rounded;
}

ULONG BeginSemanticFloatMode(void)
{
#if defined(__GNUC__) && defined(__m68k__)
   ULONG previous, semantic;

   __asm__ volatile ("fmove.l %%fpcr,%0" : "=d" (previous));
   semantic = (previous & ~0xf0UL) | 0x40UL;
   __asm__ volatile ("fmove.l %0,%%fpcr" : : "d" (semantic));
   return previous;
#else
   return 0;
#endif
}

void EndSemanticFloatMode(ULONG previousMode)
{
#if defined(__GNUC__) && defined(__m68k__)
   __asm__ volatile ("fmove.l %0,%%fpcr" : : "d" (previousMode));
#else
   (void)previousMode;
#endif
}

BOOL FloatFinite(GLfloat value)
{
   return (FloatBits(value) & 0x7f800000UL) != 0x7f800000UL;
}


GLubyte ColorByte(GLfloat value)
{
   GLfloat scaled;

   if (value <= 0.0f)
       return 0;
   if (value >= 1.0f)
       return 255;

   scaled = value * 255.0f + 0.5f;

#if defined(__GNUC__) && defined(__HAVE_68881__)
   /*
    * The "+ 0.5f then truncate" idiom needs a truncating float-to-integer
    * conversion. GCC compiles the cast below to "fmove.w fpN,dN", which
    * converts using the current FPCR rounding mode -- round-to-nearest by
    * default -- instead of the truncation C specifies. glClearColor(0.2f)
    * then packs 52 rather than 51, because 0.2f * 255 + 0.5 is 51.5000008
    * and rounds up. vbcc's conversion truncates, so this diverged only
    * after the backend moved to GCC, and only at 32bpp: R5G6B5 shifts the
    * offending bit away, which is why every depth-16 acceptance token
    * still matched.
    *
    * FINTRZ is the FPU's own round-to-zero, so the truncation stays on the
    * 68881/68060 rather than falling back to any software path.
    */
   __asm__ ("fintrz%.x %1,%0" : "=f" (scaled) : "f" (scaled));
#endif

   return (GLubyte)scaled;
}

ULONG PackColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
   return ((ULONG)ColorByte(alpha) << 24) |
          ((ULONG)ColorByte(red) << 16) |
          ((ULONG)ColorByte(green) << 8) |
          (ULONG)ColorByte(blue);
}

GLfloat ReciprocalSqrt(GLfloat value)
{
#if R200_RSQRT_USE_FPU_SQRT
   GLfloat root;

   if (value <= 0.0f)
       return 0.0f;
   /* Keep the square root in an FPU register. The following C division is
    * emitted as an FPU divide by the 68060-targeted compiler, avoiding the
    * integer bit shuffle and Newton refinement used by the portable path. */
   __asm__ volatile ("fsqrt%.x %1,%0" : "=f" (root) : "f" (value));
   return 1.0f / root;
#else
   union {
       GLfloat f;
       ULONG u;
   } estimate;
#if R200_RSQRT_NEWTON_STEPS > 0
   GLfloat half, product, correction;
#endif

   if (value <= 0.0f)
       return 0.0f;
   estimate.f = value;
   estimate.u = 0x5f375a86UL - (estimate.u >> 1);
#if R200_RSQRT_NEWTON_STEPS > 0
   /* 68881 temporaries retain extended precision while PPC operations round
    * to binary32. Force the Newton steps through binary32 so values embedded
    * in semantic records are identical on both frontend CPUs. */
   half = RoundFloat32(value * 0.5f);
   product = RoundFloat32(half * estimate.f);
   product = RoundFloat32(product * estimate.f);
   correction = RoundFloat32(1.5f - product);
   estimate.f = RoundFloat32(estimate.f * correction);
#endif
   return estimate.f;
#endif
}



void Identity(GLfloat *matrix)
{
   ULONG index;
   for (index = 0; index < 16; ++index)
       matrix[index] = 0.0f;
   matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

void CopyMatrix(GLfloat *destination, const GLfloat *source)
{
#if defined(R200_PPC_ALTIVEC)
   /* The wrapper uses AltiVec only for 16-byte-aligned context/stack
    * matrices; arbitrary application matrices retain the scalar path. */
   MGLPPCVectorCopy(destination, source, 16UL * sizeof(GLfloat), TRUE);
#else
   ULONG index;
   for (index = 0; index < 16; ++index)
       destination[index] = source[index];
#endif
}

void MultiplyMatrix(GLfloat *output, const GLfloat *left,
                          const GLfloat *right)
{
   GLfloat result[16];
   ULONG column, row, index;
   for (column = 0; column < 4; ++column) {
       for (row = 0; row < 4; ++row) {
           GLfloat value = 0.0f;
           for (index = 0; index < 4; ++index)
               value += left[index * 4 + row] *
                        right[column * 4 + index];
           result[column * 4 + row] = value;
       }
   }
   CopyMatrix(output, result);
}

GLfloat *CurrentMatrix(struct MiniGLR200Context *context)
{
   return context->matrixMode == GL_PROJECTION
              ? context->projection[context->projectionDepth]
              : context->modelview[context->modelviewDepth];
}

void InvalidateSphereMapCache(struct MiniGLR200Context *context)
{
#if R200_SPHEREMAP_CACHE
   if (++context->sphereMapGeneration == 0)
       context->sphereMapGeneration = 1;
#else
   (void)context;
#endif
}

void InvalidateNormalMatrix(struct MiniGLR200Context *context)
{
   /* The modelview/projection product is baked into every hardware-TCL
    * record header (dwords 21..36), so a matrix change is serialized header
    * state: it must split any still-open deferred batch that was captured
    * under the old matrix. */
   context->combinedMatrixValid = FALSE;
   context->inverseModelviewValid = FALSE;
   BumpSemanticStateSerial(context);
   if (context->matrixMode == GL_MODELVIEW)
       context->normalMatrixValid = FALSE;
#if R200_NORMAL_TRANSFORM_CACHE
   context->sphereNormalValid = FALSE;
#endif
   InvalidateSphereMapCache(context);
}

void MultCurrentMatrix(struct MiniGLR200Context *context,
                             const GLfloat *matrix)
{
   GLfloat result[16];
   GLfloat *current = CurrentMatrix(context);
   MultiplyMatrix(result, current, matrix);
   CopyMatrix(current, result);
   InvalidateNormalMatrix(context);
}

void BuildNormalMatrix(struct MiniGLR200Context *context)
{
   const GLfloat *m = context->modelview[context->modelviewDepth];
   GLfloat a00=m[0], a01=m[4], a02=m[8];
   GLfloat a10=m[1], a11=m[5], a12=m[9];
   GLfloat a20=m[2], a21=m[6], a22=m[10];
   GLfloat det = a00*(a11*a22-a12*a21) -
                 a01*(a10*a22-a12*a20) +
                 a02*(a10*a21-a11*a20);
   GLfloat inverse;

   if (det == 0.0f) {
       context->normalMatrix[0]=1.0f; context->normalMatrix[1]=0.0f;
       context->normalMatrix[2]=0.0f; context->normalMatrix[3]=0.0f;
       context->normalMatrix[4]=1.0f; context->normalMatrix[5]=0.0f;
       context->normalMatrix[6]=0.0f; context->normalMatrix[7]=0.0f;
       context->normalMatrix[8]=1.0f;
   } else {
       inverse = 1.0f / det;
       context->normalMatrix[0]=(a11*a22-a12*a21)*inverse;
       context->normalMatrix[1]=(a12*a20-a10*a22)*inverse;
       context->normalMatrix[2]=(a10*a21-a11*a20)*inverse;
       context->normalMatrix[3]=(a02*a21-a01*a22)*inverse;
       context->normalMatrix[4]=(a00*a22-a02*a20)*inverse;
       context->normalMatrix[5]=(a01*a20-a00*a21)*inverse;
       context->normalMatrix[6]=(a01*a12-a02*a11)*inverse;
       context->normalMatrix[7]=(a02*a10-a00*a12)*inverse;
       context->normalMatrix[8]=(a00*a11-a01*a10)*inverse;
   }
   context->normalMatrixValid = TRUE;
}

/* General 4x4 inverse via the adjugate, column-major like the matrices
 * themselves. Returns FALSE for a singular input (output then holds the
 * identity so downstream code stays finite). */
BOOL Mat4Inverse(const GLfloat *input, GLfloat *output)
{
   GLfloat a00=input[0], a01=input[1], a02=input[2], a03=input[3];
   GLfloat a10=input[4], a11=input[5], a12=input[6], a13=input[7];
   GLfloat a20=input[8], a21=input[9], a22=input[10], a23=input[11];
   GLfloat a30=input[12], a31=input[13], a32=input[14], a33=input[15];
   GLfloat b00=a00*a11-a01*a10, b01=a00*a12-a02*a10,
           b02=a00*a13-a03*a10, b03=a01*a12-a02*a11,
           b04=a01*a13-a03*a11, b05=a02*a13-a03*a12,
           b06=a20*a31-a21*a30, b07=a20*a32-a22*a30,
           b08=a20*a33-a23*a30, b09=a21*a32-a22*a31,
           b10=a21*a33-a23*a31, b11=a22*a33-a23*a32;
   GLfloat determinant = b00*b11 - b01*b10 + b02*b09 + b03*b08 -
                         b04*b07 + b05*b06;
   GLfloat inverse;

   if (determinant == 0.0f || !FloatFinite(determinant)) {
       Identity(output);
       return FALSE;
   }
   inverse = RoundFloat32(1.0f / determinant);
   output[0]  = (a11*b11 - a12*b10 + a13*b09) * inverse;
   output[1]  = (a02*b10 - a01*b11 - a03*b09) * inverse;
   output[2]  = (a31*b05 - a32*b04 + a33*b03) * inverse;
   output[3]  = (a22*b04 - a21*b05 - a23*b03) * inverse;
   output[4]  = (a12*b08 - a10*b11 - a13*b07) * inverse;
   output[5]  = (a00*b11 - a02*b08 + a03*b07) * inverse;
   output[6]  = (a32*b02 - a30*b05 - a33*b01) * inverse;
   output[7]  = (a20*b05 - a22*b02 + a23*b01) * inverse;
   output[8]  = (a10*b10 - a11*b08 + a13*b06) * inverse;
   output[9]  = (a01*b08 - a00*b10 - a03*b06) * inverse;
   output[10] = (a30*b04 - a31*b02 + a33*b00) * inverse;
   output[11] = (a21*b02 - a20*b04 - a23*b00) * inverse;
   output[12] = (a11*b07 - a10*b09 - a12*b06) * inverse;
   output[13] = (a00*b09 - a01*b07 + a02*b06) * inverse;
   output[14] = (a31*b01 - a30*b03 - a32*b00) * inverse;
   output[15] = (a20*b03 - a21*b01 + a22*b00) * inverse;
   return TRUE;
}

/* Derived lighting matrices, following Mesa classic: the model-view and its
 * full inverse go into the lit-TCL header, _EyeZDir is the model-view
 * rotation of (0,0,1), and the rescale factor is Mesa's _ModelViewInvScale
 * (1/sqrt of the inverse's z-row length squared). */
void BuildLightingMatrices(struct MiniGLR200Context *context)
{
   const GLfloat *m = context->modelview[context->modelviewDepth];
   const GLfloat *inverse;
   GLfloat rowZSquared;

   if (!context->inverseModelviewValid) {
       if (!Mat4Inverse(m, context->inverseModelview))
           context->inverseModelview[15] = 1.0f; /* identity already set */
       context->eyeZDir[0] =
           m[0]*0.0f + m[4]*0.0f + m[8]*1.0f;
       context->eyeZDir[1] =
           m[1]*0.0f + m[5]*0.0f + m[9]*1.0f;
       context->eyeZDir[2] =
           m[2]*0.0f + m[6]*0.0f + m[10]*1.0f;
       {
           GLfloat length = ReciprocalSqrt(
               context->eyeZDir[0]*context->eyeZDir[0] +
               context->eyeZDir[1]*context->eyeZDir[1] +
               context->eyeZDir[2]*context->eyeZDir[2]);
           if (FloatFinite(length) && length > 0.0f) {
               context->eyeZDir[0] *= length;
               context->eyeZDir[1] *= length;
               context->eyeZDir[2] *= length;
           } else {
               context->eyeZDir[0] = 0.0f;
               context->eyeZDir[1] = 0.0f;
               context->eyeZDir[2] = 1.0f;
           }
       }
       inverse = context->inverseModelview;
        rowZSquared = RoundFloat32(inverse[2]*inverse[2]);
        rowZSquared = RoundFloat32(
            rowZSquared + RoundFloat32(inverse[6]*inverse[6]));
        rowZSquared = RoundFloat32(
            rowZSquared + RoundFloat32(inverse[10]*inverse[10]));
       context->modelviewInvScale =
           rowZSquared < 1e-12f ? 1.0f : ReciprocalSqrt(rowZSquared);
       if (!FloatFinite(context->modelviewInvScale) ||
           context->modelviewInvScale <= 0.0f)
           context->modelviewInvScale = 1.0f;
       context->inverseModelviewValid = TRUE;
   }
}

GLfloat FogFactor(const struct MiniGLR200Context *context,
                        GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   const GLfloat *modelview;
   GLfloat eyeZ, distance, fog;
   if (!context->fogEnabled)
       return 0.0f;
   modelview = context->modelview[context->modelviewDepth];
   eyeZ = modelview[2] * x + modelview[6] * y +
          modelview[10] * z + modelview[14] * w;
   distance = eyeZ < 0.0f ? -eyeZ : eyeZ;
   if (context->fogMode == GL_LINEAR) {
       GLfloat difference = context->fogEnd - context->fogStart;
       fog = difference != 0.0f
                 ? (context->fogEnd - distance) / difference
                 : (distance < context->fogEnd ? 1.0f : 0.0f);
   } else if (context->fogMode == GL_EXP2) {
       GLfloat amount = context->fogDensity * distance;
       fog = (GLfloat)exp(-(double)(amount * amount));
   } else {
       fog = (GLfloat)exp(-(double)(context->fogDensity * distance));
   }
   if (fog < 0.0f) fog = 0.0f;
   if (fog > 1.0f) fog = 1.0f;
   /* Explicit R200 vertex fog uses OpenGL's scene factor: 1 keeps the
    * fragment colour and 0 selects the fog colour. */
   return fog;
}
