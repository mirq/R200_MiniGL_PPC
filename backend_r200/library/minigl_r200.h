#ifndef MINIGL_R200_H
#define MINIGL_R200_H

#include <exec/types.h>

/*
 * This backend is self-contained: it declares the GL types and enums it needs
 * rather than including mgl/gl.h, so that the rendering core stays independent
 * of the client SDK headers. Tell libraries/minigl_fastpath.h not to pull
 * mgl/gl.h in on top of these declarations. The typedefs below are chosen to
 * match mgl/gl.h exactly, so the dispatch thunks need no conversion.
 */
#define MINIGL_GL_TYPES_PROVIDED 1

typedef void GLvoid;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef int GLint;
typedef unsigned int GLuint;
typedef unsigned int GLboolean;
typedef long GLsizei;
typedef unsigned long GLbitfield;
typedef float GLfloat;
typedef double GLdouble;
typedef float GLclampf;
typedef double GLclampd;
typedef unsigned int GLenum;

/* Matches mgl/context.h exactly; see the note above about self-containment. */
typedef enum {
    MGLKEY_F1, MGLKEY_F2, MGLKEY_F3, MGLKEY_F4, MGLKEY_F5, MGLKEY_F6,
    MGLKEY_F7, MGLKEY_F8, MGLKEY_F9, MGLKEY_F10,
    MGLKEY_CUP, MGLKEY_CDOWN, MGLKEY_CLEFT, MGLKEY_CRIGHT
} MGLspecial;

typedef void (*KeyHandlerFn)(char key);
typedef void (*SpecialHandlerFn)(MGLspecial special_key);
typedef void (*MouseHandlerFn)(GLint x, GLint y, GLbitfield buttons);
typedef void (*IdleFn)(void);

/* Matches mgl/modes.h exactly. */
#define MGL_MAX_MODE 80

typedef struct {
    GLint id;
    GLint width, height;
    GLint bit_depth;
    char mode_name[MGL_MAX_MODE];
} MGLScreenMode;

typedef GLboolean (*MGLScreenModeCallback)(MGLScreenMode *);

#define GL_COLOR_BUFFER_BIT 0x00000001UL
#define GL_DEPTH_BUFFER_BIT 0x00000002UL
#define GL_FALSE 0U
#define GL_TRUE 1U

/* Values are fixed by the classic MiniGL public enum, not desktop OpenGL. */
#define GL_NO_ERROR 0U
#define GL_INVALID_ENUM 88U
#define GL_INVALID_OPERATION 89U
#define GL_INVALID_VALUE 90U
#define GL_OUT_OF_MEMORY 123U
#define GL_RENDERER 148U
#define GL_TRIANGLES 191U
#define GL_VENDOR 205U
#define GL_VERSION 206U
#define GL_EXTENSIONS 57U
#define GL_ALWAYS 8U
#define GL_ALPHA_TEST_REF 7U
#define GL_BYTE 18U
#define GL_COLOR_ARRAY 23U
#define GL_COLOR_CLEAR_VALUE 28U
#define GL_CURRENT_COLOR 32U
#define GL_DEPTH_CLEAR_VALUE 38U
#define GL_DEPTH_RANGE 41U
#define GL_DEPTH_TEST 43U
#define GL_DEPTH_WRITEMASK 44U
#define GL_DITHER 45U
#define GL_DONT_CARE 46U
#define GL_DOUBLE 47U
#define GL_EQUAL 56U
#define GL_FLOAT 60U
#define GL_FASTEST 58U
#define GL_FOG 61U
#define GL_FOG_COLOR 62U
#define GL_FOG_DENSITY 63U
#define GL_FOG_END 64U
#define GL_FOG_MODE 67U
#define GL_FOG_START 68U
#define GL_GEQUAL 74U
#define GL_GREATER 75U
#define GL_INT 85U
#define GL_LEQUAL 92U
#define GL_LESS 93U
#define GL_MODELVIEW 107U
#define GL_MODELVIEW_MATRIX 108U
#define GL_NEVER 111U
#define GL_NICEST 115U
#define GL_NOTEQUAL 117U
#define GL_POLYGON 139U
#define GL_PROJECTION 140U
#define GL_PROJECTION_MATRIX 141U
#define GL_QUADS 143U
#define GL_QUAD_STRIP 144U

/* Axis flags for glRotatefEXT/glRotatefEXTs; values match mgl/gl.h. */
#define GLROT_001 0x800U
#define GLROT_010 0x1000U
#define GLROT_100 0x2000U
#define GLROT_011 0x4000U
#define GLROT_101 0x8000U
#define GLROT_110 0x10000U
#define GLROT_111 0x20000U
#define GL_SCISSOR_BOX 159U
#define GL_SHORT 162U
#define GL_STACK_OVERFLOW 167U
#define GL_STACK_UNDERFLOW 168U
#define GL_VIEWPORT 212U
#define GL_TRIANGLE_FAN 192U
#define GL_TRIANGLE_STRIP 193U
#define GL_UNSIGNED_BYTE 200U
#define GL_UNSIGNED_INT 201U
#define GL_UNSIGNED_SHORT 202U
#define GL_VERTEX_ARRAY 207U
#define GL_ALPHA 1U
/* Sized aliases (numbering follows the shared mgl/gl.h enum; accepted by
 * the TexImage2D gates, widened to their unsized class on upload). */
#define GL_ALPHA8 2U
#define GL_ALPHA_TEST 5U
#define GL_BACK 10U
#define GL_BLEND 13U
#define GL_CCW 22U
#define GL_CULL_FACE 30U
#define GL_CULL_FACE_MODE 31U
#define GL_CW 35U
#define GL_DRAW_BUFFER 49U
#define GL_FLAT 59U
#define GL_FRONT 70U
#define GL_FRONT_AND_BACK 69U
#define GL_FRONT_FACE 71U
#define GL_NONE 116U
#define GL_SHADE_MODEL 161U
#define GL_SMOOTH 163U
#define GL_LINES 97U
#define GL_LINE_LOOP 98U
#define GL_LINE_STRIP 99U
#define GL_POINTS 131U
/* Polygon-mode rasterization styles (distinct from the GL_POINTS/GL_LINES
 * primitive types above); appended to the end of mgl/gl.h's enum. */
#define GL_POINT 243U
#define GL_LINE 244U
#define GL_FILL 222U
#define GL_POLYGON_OFFSET_FILL 135U
#define GL_COLOR_INDEX 29U
#define GL_COLOR_TABLE 232U
#define GL_SCISSOR_TEST 160U
#define GL_DST_ALPHA 50U
#define GL_DST_COLOR 51U
#define GL_LINEAR 94U
#define GL_LINEAR_MIPMAP_LINEAR 95U
#define GL_LINEAR_MIPMAP_NEAREST 96U
#define GL_LUMINANCE 100U
#define GL_LUMINANCE8 101U
#define GL_LUMINANCE8_ALPHA8 102U
#define GL_LUMINANCE_ALPHA 103U
#define GL_MODULATE 110U
#define GL_NEAREST 112U
#define GL_NEAREST_MIPMAP_NEAREST 113U
#define GL_NEAREST_MIPMAP_LINEAR 114U
#define GL_ONE 118U
#define GL_ONE_MINUS_DST_ALPHA 119U
#define GL_ONE_MINUS_DST_COLOR 120U
#define GL_ONE_MINUS_SRC_ALPHA 121U
#define GL_ONE_MINUS_SRC_COLOR 122U
#define GL_PACK_ALIGNMENT 124U
#define GL_PACK_ROW_LENGTH 126U
#define GL_PACK_SKIP_PIXELS 127U
#define GL_PACK_SKIP_ROWS 128U
#define GL_REPLACE 149U
#define GL_REPEAT 150U
#define GL_RGB 151U
#define GL_RGB5 152U
#define GL_RGB5_A1 153U
#define GL_RGB8 154U
#define GL_RGBA 155U
#define GL_RGBA8 156U
#define GL_SRC_ALPHA 164U
#define GL_SRC_ALPHA_SATURATE 165U
#define GL_SRC_COLOR 166U
#define GL_TEXTURE_2D 174U
#define GL_TEXTURE_COORD_ARRAY 176U
#define GL_TEXTURE_ENV 181U
#define GL_TEXTURE_ENV_MODE 183U
#define GL_TEXTURE_MAG_FILTER 184U
#define GL_TEXTURE_MIN_FILTER 185U
#define GL_TEXTURE_WRAP_S 187U
#define GL_TEXTURE_WRAP_T 188U
#define GL_TEXTURE_GEN_S 189U
#define GL_TEXTURE_GEN_T 190U
#define GL_UNPACK_ALIGNMENT 194U
#define GL_UNPACK_ROW_LENGTH 196U
#define GL_UNPACK_SKIP_PIXELS 197U
#define GL_UNPACK_SKIP_ROWS 198U
#define GL_ZERO 213U
#define GL_CLAMP 215U
#define GL_EXP 216U
#define GL_EXP2 217U
#define GL_TEXTURE_GEN_MODE 218U
#define GL_SPHERE_MAP 219U
#define GL_T 220U
#define GL_S 221U
/* Interleaved-array formats and remaining hint targets (ABI v5,
 * values from the classic MiniGL public enum, matching mgl/gl.h). */
#define GL_C3F_V3F 19U
#define GL_C4UB_V3F 21U
#define GL_DECAL 36U
#define GL_FOG_HINT 65U
#define GL_PERSPECTIVE_CORRECTION_HINT 130U
#define GL_T2F_C3F_V3F 170U
#define GL_T2F_C4UB_V3F 171U
#define GL_T2F_V3F 172U
#define GL_V3F 204U
/* Matches mgl/gl.h; V19-parity return value for
 * mglGetSupportedScreenModes when no mode is selected. */
#define MGL_SM_BESTMODE 0xFFFFFFFFUL
/*
 * Client-visible enum numbering must match include/mgl/gl.h (the header
 * every dispatch client compiles against), NOT the classic dev/include
 * copy. The WOS client header re-anchored GL_TEXTURE0_ARB/1_ARB to the
 * real GL spec values (Quake2 multitexture fix) and re-anchored
 * GL_MAX_TEXTURE_UNITS_ARB to GL_FILL+1, which shifted every tail
 * constant by -4 relative to the classic numbering below-the-line values
 * (BGRA/ARGB/565/4444/FLATFAN/...) previously used here. A mismatch is
 * not cosmetic: ValidPrimitive()/glHint/glColorTable validate raw client
 * values, and BGRA/ARGB fall into the byte-order else-branch and swap
 * channels silently. tests/test_enum_parity.py guards this contract.
 */
#define MGL_FLATFAN 227U
#define MGL_FLATSTRIP 228U
#define MGL_W_ONE_HINT 230U
#define MGL_Z_OFFSET 231U
#define MGL_PERSPECTIVE_MAPPING 322U
#define MGL_LOCK_AUTOMATIC 224U
#define MGL_LOCK_MANUAL 225U
#define MGL_LOCK_SMART 226U
#define MGL_UBYTE_BGRA 233U
#define MGL_UBYTE_ARGB 234U
#define MGL_UNSIGNED_SHORT_5_6_5 235U
#define MGL_UNSIGNED_SHORT_4_4_4_4 236U
/* Same contract for the multitexture unit selectors: dispatch clients
 * pass the real spec values (include/mgl/gl.h re-anchor), so unit -
 * GL_TEXTURE0_ARB yields the 0/1 unit index. */
#define GL_TEXTURE0_ARB 0x84C0U
#define GL_TEXTURE1_ARB 0x84C1U
/* Fixed-function lighting (ABI v4), appended to the end of mgl/gl.h's
 * enum; values must match it exactly. */
#define GL_LIGHTING 245U
#define GL_LIGHT0 246U
#define GL_LIGHT1 247U
#define GL_LIGHT2 248U
#define GL_LIGHT3 249U
#define GL_LIGHT4 250U
#define GL_LIGHT5 251U
#define GL_LIGHT6 252U
#define GL_LIGHT7 253U
#define GL_MAX_LIGHTS 254U
/* Integer queries (numbering follows the shared mgl/gl.h enum). */
#define GL_MAX_TEXTURE_SIZE 105U
#define GL_MAX_TEXTURE_UNITS_ARB 223U
#define GL_AMBIENT 255U
#define GL_DIFFUSE 256U
#define GL_SPECULAR 257U
#define GL_EMISSION 258U
#define GL_SHININESS 259U
#define GL_AMBIENT_AND_DIFFUSE 260U
#define GL_POSITION 261U
#define GL_SPOT_DIRECTION 262U
#define GL_SPOT_EXPONENT 263U
#define GL_SPOT_CUTOFF 264U
#define GL_CONSTANT_ATTENUATION 265U
#define GL_LINEAR_ATTENUATION 266U
#define GL_QUADRATIC_ATTENUATION 267U
#define GL_LIGHT_MODEL_AMBIENT 268U
#define GL_LIGHT_MODEL_LOCAL_VIEWER 269U
#define GL_LIGHT_MODEL_TWO_SIDE 270U
#define GL_COLOR_MATERIAL 271U
#define GL_COLOR_MATERIAL_FACE 272U
#define GL_COLOR_MATERIAL_PARAM 273U
#define GL_NORMALIZE 274U
#define GL_RESCALE_NORMAL 275U
#define GL_NORMAL_ARRAY 276U
#define GL_NORMAL_ARRAY_POINTER 277U
#define GL_NORMAL_ARRAY_SIZE 278U
#define GL_NORMAL_ARRAY_STRIDE 279U
#define GL_NORMAL_ARRAY_TYPE 280U
#define GL_CURRENT_NORMAL 281U

typedef struct {
    ULONG width, height, depth;
    ULONG pixel_format;
    void *base_address;
    ULONG pitch;
} MGLLockInfo;

#endif
