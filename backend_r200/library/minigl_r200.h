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
#define GL_NO_ERROR 0x0000U
#define GL_INVALID_ENUM 0x0500U
#define GL_INVALID_OPERATION 0x0502U
#define GL_INVALID_VALUE 0x0501U
#define GL_OUT_OF_MEMORY 0x0505U
#define GL_RENDERER 0x1F01U
#define GL_TRIANGLES 0x0004U
#define GL_VENDOR 0x1F00U
#define GL_VERSION 0x1F02U
#define GL_EXTENSIONS 0x1F03U
#define GL_ALWAYS 0x0207U
#define GL_ALPHA_TEST_REF 0x0BC2U
#define GL_BYTE 0x1400U
#define GL_COLOR_ARRAY 0x8076U
#define GL_COLOR_CLEAR_VALUE 0x0C22U
#define GL_CURRENT_COLOR 0x0B00U
#define GL_DEPTH_CLEAR_VALUE 0x0B73U
#define GL_DEPTH_RANGE 0x0B70U
#define GL_DEPTH_TEST 0x0B71U
#define GL_DEPTH_WRITEMASK 0x0B72U
#define GL_DITHER 0x0BD0U
#define GL_DONT_CARE 0x1100U
#define GL_DOUBLE 0x140AU
#define GL_EQUAL 0x0202U
#define GL_FLOAT 0x1406U
#define GL_FASTEST 0x1101U
#define GL_FOG 0x0B60U
#define GL_FOG_COLOR 0x0B66U
#define GL_FOG_DENSITY 0x0B62U
#define GL_FOG_END 0x0B64U
#define GL_FOG_MODE 0x0B65U
#define GL_FOG_START 0x0B63U
#define GL_GEQUAL 0x0206U
#define GL_GREATER 0x0204U
#define GL_INT 0x1404U
#define GL_LEQUAL 0x0203U
#define GL_LESS 0x0201U
#define GL_MODELVIEW 0x1700U
#define GL_MODELVIEW_MATRIX 0x0BA6U
#define GL_NEVER 0x0200U
#define GL_NICEST 0x1102U
#define GL_NOTEQUAL 0x0205U
#define GL_POLYGON 0x0009U
#define GL_PROJECTION 0x1701U
#define GL_PROJECTION_MATRIX 0x0BA7U
#define GL_QUADS 0x0007U
#define GL_QUAD_STRIP 0x0008U

/* Axis flags for glRotatefEXT/glRotatefEXTs; values match mgl/gl.h. */
#define GLROT_001 0x800U
#define GLROT_010 0x1000U
#define GLROT_100 0x2000U
#define GLROT_011 0x4000U
#define GLROT_101 0x8000U
#define GLROT_110 0x10000U
#define GLROT_111 0x20000U
#define GL_SCISSOR_BOX 0x0C10U
#define GL_SHORT 0x1402U
#define GL_STACK_OVERFLOW 0x0503U
#define GL_STACK_UNDERFLOW 0x0504U
#define GL_VIEWPORT 0x0BA2U
#define GL_TRIANGLE_FAN 0x0006U
#define GL_TRIANGLE_STRIP 0x0005U
#define GL_UNSIGNED_BYTE 0x1401U
#define GL_UNSIGNED_INT 0x1405U
#define GL_UNSIGNED_SHORT 0x1403U
#define GL_VERTEX_ARRAY 0x8074U
#define GL_ALPHA 0x1906U
/* Sized aliases (numbering follows the shared mgl/gl.h enum; accepted by
 * the TexImage2D gates, widened to their unsized class on upload). */
#define GL_ALPHA8 0x803CU
#define GL_ALPHA_TEST 0x0BC0U
#define GL_BACK 0x0405U
#define GL_BLEND 0x0BE2U
#define GL_CCW 0x0901U
#define GL_CULL_FACE 0x0B44U
#define GL_CULL_FACE_MODE 0x0B45U
#define GL_CW 0x0900U
#define GL_DRAW_BUFFER 0x0C01U
#define GL_FLAT 0x1D00U
#define GL_FRONT 0x0404U
#define GL_FRONT_AND_BACK 0x0408U
#define GL_FRONT_FACE 0x0B46U
#define GL_NONE 0x0000U
#define GL_SHADE_MODEL 0x0B54U
#define GL_SMOOTH 0x1D01U
#define GL_LINES 0x0001U
#define GL_LINE_LOOP 0x0002U
#define GL_LINE_STRIP 0x0003U
#define GL_POINTS 0x0000U
/* Polygon-mode rasterization styles (distinct from the GL_POINTS/GL_LINES
 * primitive types above); appended to the end of mgl/gl.h's enum. */
#define GL_POINT 0x1B00U
#define GL_LINE 0x1B01U
#define GL_FILL 0x1B02U
#define GL_POLYGON_OFFSET_FILL 0x8037U
#define GL_COLOR_INDEX 0x1900U
#define GL_COLOR_TABLE 0x80D0U
#define GL_SCISSOR_TEST 0x0C11U
#define GL_DST_ALPHA 0x0304U
#define GL_DST_COLOR 0x0306U
#define GL_LINEAR 0x2601U
#define GL_LINEAR_MIPMAP_LINEAR 0x2703U
#define GL_LINEAR_MIPMAP_NEAREST 0x2701U
#define GL_LUMINANCE 0x1909U
#define GL_LUMINANCE8 0x8040U
#define GL_LUMINANCE8_ALPHA8 0x8045U
#define GL_LUMINANCE_ALPHA 0x190AU
#define GL_MODULATE 0x2100U
#define GL_NEAREST 0x2600U
#define GL_NEAREST_MIPMAP_NEAREST 0x2700U
#define GL_NEAREST_MIPMAP_LINEAR 0x2702U
#define GL_ONE 0x0001U
#define GL_ONE_MINUS_DST_ALPHA 0x0305U
#define GL_ONE_MINUS_DST_COLOR 0x0307U
#define GL_ONE_MINUS_SRC_ALPHA 0x0303U
#define GL_ONE_MINUS_SRC_COLOR 0x0301U
#define GL_PACK_ALIGNMENT 0x0D05U
#define GL_PACK_ROW_LENGTH 0x0D02U
#define GL_PACK_SKIP_PIXELS 0x0D04U
#define GL_PACK_SKIP_ROWS 0x0D03U
#define GL_REPLACE 0x1E01U
#define GL_REPEAT 0x2901U
#define GL_RGB 0x1907U
#define GL_RGB5 0x8050U
#define GL_RGB5_A1 0x8057U
#define GL_RGB8 0x8051U
#define GL_RGBA 0x1908U
#define GL_RGBA8 0x8058U
#define GL_SRC_ALPHA 0x0302U
#define GL_SRC_ALPHA_SATURATE 0x0308U
#define GL_SRC_COLOR 0x0300U
#define GL_TEXTURE_2D 0x0DE1U
#define GL_TEXTURE_COORD_ARRAY 0x8078U
#define GL_TEXTURE_ENV 0x2300U
#define GL_TEXTURE_ENV_MODE 0x2200U
#define GL_TEXTURE_MAG_FILTER 0x2800U
#define GL_TEXTURE_MIN_FILTER 0x2801U
#define GL_TEXTURE_WRAP_S 0x2802U
#define GL_TEXTURE_WRAP_T 0x2803U
#define GL_TEXTURE_GEN_S 0x0C60U
#define GL_TEXTURE_GEN_T 0x0C61U
#define GL_UNPACK_ALIGNMENT 0x0CF5U
#define GL_UNPACK_ROW_LENGTH 0x0CF2U
#define GL_UNPACK_SKIP_PIXELS 0x0CF4U
#define GL_UNPACK_SKIP_ROWS 0x0CF3U
#define GL_ZERO 0x0000U
#define GL_CLAMP 0x2900U
#define GL_EXP 0x0800U
#define GL_EXP2 0x0801U
#define GL_TEXTURE_GEN_MODE 0x2500U
#define GL_SPHERE_MAP 0x2402U
#define GL_T 0x2001U
#define GL_S 0x2000U
/* Interleaved-array formats and remaining hint targets (ABI v5,
 * values from the classic MiniGL public enum, matching mgl/gl.h). */
#define GL_C3F_V3F 0x2A24U
#define GL_C4UB_V3F 0x2A23U
#define GL_DECAL 0x2101U
#define GL_FOG_HINT 0x0C54U
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50U
#define GL_T2F_C3F_V3F 0x2A2AU
#define GL_T2F_C4UB_V3F 0x2A29U
#define GL_T2F_V3F 0x2A27U
#define GL_V3F 0x2A21U
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
#define MGL_FLATFAN 0x7013U
#define MGL_FLATSTRIP 0x7014U
#define MGL_W_ONE_HINT 0x7016U
#define MGL_Z_OFFSET 0x7017U
#define MGL_PERSPECTIVE_MAPPING 0x7015U
#define MGL_LOCK_AUTOMATIC 0x7010U
#define MGL_LOCK_MANUAL 0x7011U
#define MGL_LOCK_SMART 0x7012U
#define MGL_UBYTE_BGRA 0x7018U
#define MGL_UBYTE_ARGB 0x7019U
#define MGL_UNSIGNED_SHORT_5_6_5 0x701AU
#define MGL_UNSIGNED_SHORT_4_4_4_4 0x701BU
/* Same contract for the multitexture unit selectors: dispatch clients
 * pass the real spec values (include/mgl/gl.h re-anchor), so unit -
 * GL_TEXTURE0_ARB yields the 0/1 unit index. */
#define GL_TEXTURE0_ARB 0x84C0U
#define GL_TEXTURE1_ARB 0x84C1U
/* Fixed-function lighting (ABI v4), appended to the end of mgl/gl.h's
 * enum; values must match it exactly. */
#define GL_LIGHTING 0x0B50U
#define GL_LIGHT0 0x4000U
#define GL_LIGHT1 0x4001U
#define GL_LIGHT2 0x4002U
#define GL_LIGHT3 0x4003U
#define GL_LIGHT4 0x4004U
#define GL_LIGHT5 0x4005U
#define GL_LIGHT6 0x4006U
#define GL_LIGHT7 0x4007U
#define GL_MAX_LIGHTS 0x0D31U
/* Integer queries (numbering follows the shared mgl/gl.h enum). */
#define GL_MAX_TEXTURE_SIZE 0x0D33U
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2U
#define GL_AMBIENT 0x1200U
#define GL_DIFFUSE 0x1201U
#define GL_SPECULAR 0x1202U
#define GL_EMISSION 0x1600U
#define GL_SHININESS 0x1601U
#define GL_AMBIENT_AND_DIFFUSE 0x1602U
#define GL_POSITION 0x1203U
#define GL_SPOT_DIRECTION 0x1204U
#define GL_SPOT_EXPONENT 0x1205U
#define GL_SPOT_CUTOFF 0x1206U
#define GL_CONSTANT_ATTENUATION 0x1207U
#define GL_LINEAR_ATTENUATION 0x1208U
#define GL_QUADRATIC_ATTENUATION 0x1209U
#define GL_LIGHT_MODEL_AMBIENT 0x0B53U
#define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51U
#define GL_LIGHT_MODEL_TWO_SIDE 0x0B52U
#define GL_COLOR_MATERIAL 0x0B57U
#define GL_COLOR_MATERIAL_FACE 0x0B55U
#define GL_COLOR_MATERIAL_PARAM 0x0B56U
#define GL_NORMALIZE 0x0BA1U
#define GL_RESCALE_NORMAL 0x803AU
#define GL_NORMAL_ARRAY 0x8075U
#define GL_NORMAL_ARRAY_POINTER 0x808FU
#define GL_NORMAL_ARRAY_SIZE 0x7F300001U
#define GL_NORMAL_ARRAY_STRIDE 0x807FU
#define GL_NORMAL_ARRAY_TYPE 0x807EU
#define GL_CURRENT_NORMAL 0x0B02U

typedef struct {
    ULONG width, height, depth;
    ULONG pixel_format;
    void *base_address;
    ULONG pitch;
} MGLLockInfo;


/*
 * V29 parity: the rest of the ABI-5 client vocabulary (include/mgl/gl.h),
 * so every name a dispatch client can pass has the same value here.
 * tests/test_enum_parity.py checks the whole set.
 */
#define GL_ALPHA_BITS 0x0D55U
#define GL_ALPHA_SCALE 0x0D1CU
#define GL_ALPHA_TEST_FUNC 0x0BC1U
#define GL_AUX_BUFFERS 0x0C00U
#define GL_BACK_LEFT 0x0402U
#define GL_BACK_RIGHT 0x0403U
#define GL_BLEND_DST 0x0BE0U
#define GL_BLEND_DST_ALPHA 0x80CAU
#define GL_BLEND_DST_RGB 0x80C8U
#define GL_BLEND_SRC 0x0BE1U
#define GL_BLEND_SRC_ALPHA 0x80CBU
#define GL_BLEND_SRC_RGB 0x80C9U
#define GL_BLUE 0x1905U
#define GL_BLUE_BITS 0x0D54U
#define GL_C4UB_V2F 0x2A22U
#define GL_COLOR_ARRAY_POINTER 0x8090U
#define GL_COLOR_ARRAY_SIZE 0x8081U
#define GL_COLOR_ARRAY_STRIDE 0x8083U
#define GL_COLOR_ARRAY_TYPE 0x8082U
#define GL_CURRENT_INDEX 0x0B01U
#define GL_CURRENT_TEXTURE_COORDS 0x0B03U
#define GL_DEPTH_BITS 0x0D56U
#define GL_DEPTH_COMPONENT 0x1902U
#define GL_DEPTH_FUNC 0x0B74U
#define GL_DEPTH_SCALE 0x0D1EU
#define GL_DOUBLEBUFFER 0x0C32U
#define GL_EDGE_FLAG 0x0B43U
#define GL_EDGE_FLAG_ARRAY 0x8079U
#define GL_EDGE_FLAG_ARRAY_POINTER 0x8093U
#define GL_EDGE_FLAG_ARRAY_STRIDE 0x808CU
#define GL_FOG_INDEX 0x0B61U
#define GL_FRONT_LEFT 0x0400U
#define GL_FRONT_RIGHT 0x0401U
#define GL_GREEN 0x1904U
#define GL_GREEN_BITS 0x0D53U
#define GL_INDEX_ARRAY 0x8077U
#define GL_INDEX_ARRAY_POINTER 0x8091U
#define GL_INDEX_ARRAY_STRIDE 0x8086U
#define GL_INDEX_ARRAY_TYPE 0x8085U
#define GL_INDEX_BITS 0x0D51U
#define GL_INDEX_CLEAR_VALUE 0x0C20U
#define GL_INDEX_MODE 0x0C30U
#define GL_INTENSITY 0x8049U
#define GL_INTENSITY8 0x804BU
#define GL_LEFT 0x0406U
#define GL_MATRIX_MODE 0x0BA0U
#define GL_MAX_VIEWPORT_DIMS 0x0D3AU
#define GL_MODELVIEW_STACK_DEPTH 0x0BA3U
#define GL_ADD 0x0104U
#define GL_PACK_LSB_FIRST 0x0D01U
#define GL_PACK_SWAP_BYTES 0x0D00U
#define GL_POINT_SMOOTH_HINT 0x0C51U
#define GL_LINE_SMOOTH_HINT 0x0C52U
#define GL_POLYGON_SMOOTH_HINT 0x0C53U
#define GL_POLYGON_MODE 0x0B40U
#define GL_POLYGON_OFFSET 0x7001U
#define GL_POLYGON_OFFSET_FACTOR 0x8038U
#define GL_POLYGON_OFFSET_LINE 0x2A02U
#define GL_POLYGON_OFFSET_POINT 0x2A01U
#define GL_POLYGON_OFFSET_UNITS 0x2A00U
#define GL_PROJECTION_STACK_DEPTH 0x0BA4U
#define GL_READ_BUFFER 0x0C02U
#define GL_RED 0x1903U
#define GL_RED_BITS 0x0D52U
#define GL_RGBA_MODE 0x0C31U
#define GL_RIGHT 0x0407U
#define GL_STEREO 0x0C33U
#define GL_TABLE_TOO_LARGE 0x8031U
#define GL_TEXTURE_2D_BINDING 0x8069U
#define GL_TEXTURE_COORD_ARRAY_SIZE 0x8088U
#define GL_TEXTURE_COORD_ARRAY_STRIDE 0x808AU
#define GL_TEXTURE_COORD_ARRAY_TYPE 0x8089U
#define GL_TEXTURE_DOOR_ARRAY_POINTER 0x8092U
/* The correct spelling; upstream keeps the historical typo as the primary. */
#define GL_TEXTURE_COORD_ARRAY_POINTER GL_TEXTURE_DOOR_ARRAY_POINTER
#define GL_TEXTURE_ENV_COLOR 0x2201U
#define GL_TEXTURE_PRIORITY 0x8066U
#define GL_UNPACK_LSB_FIRST 0x0CF1U
#define GL_UNPACK_SWAP_BYTES 0x0CF0U
#define GL_V2F 0x2A20U
#define GL_VERTEX_ARRAY_POINTER 0x808EU
#define GL_VERTEX_ARRAY_SIZE 0x807AU
#define GL_VERTEX_ARRAY_STRIDE 0x807CU
#define GL_VERTEX_ARRAY_TYPE 0x807BU
#define GL_POINT_SMOOTH 0x0B10U
#define GL_TEXTURE2_ARB 0x84C2U
#define GL_TEXTURE3_ARB 0x84C3U
#define GL_TEXTURE0 0x84C0U
#define GL_TEXTURE1 0x84C1U
#define GL_TEXTURE2 0x84C2U
#define GL_TEXTURE3 0x84C3U
#define MGL_FIXPOINTTRANS_HINT 0x701CU
#define MGL_ARRAY_TRANSFORMATIONS 0x701DU
#define GL_COLOR_WRITEMASK 0x0C23U
#define GL_LINE_WIDTH 0x0B21U
#define GL_EYE_LINEAR 0x2400U
#define GL_OBJECT_LINEAR 0x2401U
#define GL_OBJECT_PLANE 0x2501U
#define GL_EYE_PLANE 0x2502U
#define GL_TEXTURE 0x1702U
#define GL_TEXTURE_MATRIX 0x0BA8U
#define GL_TEXTURE_STACK_DEPTH 0x0BA5U
#define GL_POINT_SIZE 0x0B11U
#define GL_LINE_SMOOTH 0x0B20U
#define GL_LINE_STIPPLE 0x0B24U
#define GL_POLYGON_SMOOTH 0x0B41U
#define GL_POLYGON_STIPPLE 0x0B42U
#define GL_COLOR_MATERIAL_PARAMETER 0x0B56U
#define GL_STENCIL_TEST 0x0B90U
#define GL_LOGIC_OP 0x0BF1U
#define GL_INDEX_LOGIC_OP 0x0BF1U
#define GL_COLOR_LOGIC_OP 0x0BF2U
#define GL_TEXTURE_GEN_R 0x0C62U
#define GL_TEXTURE_GEN_Q 0x0C63U
#define GL_AUTO_NORMAL 0x0D80U
#define GL_TEXTURE_1D 0x0DE0U
#define GL_CLIP_PLANE0 0x3000U
#define GL_CLIP_PLANE1 0x3001U
#define GL_CLIP_PLANE2 0x3002U
#define GL_CLIP_PLANE3 0x3003U
#define GL_CLIP_PLANE4 0x3004U
#define GL_CLIP_PLANE5 0x3005U
#define GL_MAP1_COLOR_4 0x0D90U
#define GL_MAP1_INDEX 0x0D91U
#define GL_MAP1_NORMAL 0x0D92U
#define GL_MAP1_TEXTURE_COORD_1 0x0D93U
#define GL_MAP1_TEXTURE_COORD_2 0x0D94U
#define GL_MAP1_TEXTURE_COORD_3 0x0D95U
#define GL_MAP1_TEXTURE_COORD_4 0x0D96U
#define GL_MAP1_VERTEX_3 0x0D97U
#define GL_MAP1_VERTEX_4 0x0D98U
#define GL_MAP2_COLOR_4 0x0DB0U
#define GL_MAP2_INDEX 0x0DB1U
#define GL_MAP2_NORMAL 0x0DB2U
#define GL_MAP2_TEXTURE_COORD_1 0x0DB3U
#define GL_MAP2_TEXTURE_COORD_2 0x0DB4U
#define GL_MAP2_TEXTURE_COORD_3 0x0DB5U
#define GL_MAP2_TEXTURE_COORD_4 0x0DB6U
#define GL_MAP2_VERTEX_3 0x0DB7U
#define GL_MAP2_VERTEX_4 0x0DB8U
#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FBU
#define GL_FUNC_ADD 0x8006U
#define GL_MIN 0x8007U
#define GL_MAX 0x8008U
#define GL_BLEND_EQUATION 0x8009U
#define GL_FUNC_SUBTRACT 0x800AU
#define GL_FUNC_REVERSE_SUBTRACT 0x800BU
#define MGL_ZBUFFER_32F 0x0020U
#define MGL_ZBUFFER_16 0x0010U
#define GL_COMPILE 0x1300U
#define GL_COMPILE_AND_EXECUTE 0x1301U
#define GL_LIST_BASE 0x0B32U
#define GL_LIST_INDEX 0x0B33U
#define GL_LIST_MODE 0x0B30U

#endif
