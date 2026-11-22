/*
 * eg_emit.c -- the MiniGL frontend's draw records (radeon3d_records.h) as
 * native Radeon HD 5450 (Evergreen, Cedar) commands.
 *
 * One batch of records becomes one self-contained indirect buffer: the
 * engine state is unknown at its start (the Picasso96 driver draws on the
 * same engine between batches), so every buffer opens with the full
 * default state and a cache flush, then emits per record only the state
 * groups that changed. Fixed function is expressed natively: vertex and
 * pixel shaders are generated per vertex layout and texture/fog
 * combination and cached in VRAM; alpha test, blending, depth, culling and
 * clipping are hardware state.
 *
 * Byte order (evergreen-p96-driver docs/04-endianness.md): command words
 * are host longs (the CP fetches the buffer with a 32-bit swap); record
 * vertices are copied as the 68k holds them and fetched with the 8IN32
 * swap; shader words and constants are written little-endian; texels are
 * the frontend's BGRA bytes, fetched without a swap.
 *
 * State code: xf86-video-ati's Evergreen accel functions (MIT), built from
 * the Evergreen.chip tree (EVERGREEN_DRIVER in the makefile).
 */
#include <exec/types.h>
#include <string.h>

#include "xati_compat.h"
#include "radeon3d.h"
#include "radeon3d_records.h"
#include "eg_emit.h"

/* small fixed-size copies and clears inline (the build has -fno-builtin;
 * the shim's memcpy/memset take the rest) */
#ifdef __m68k__
#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
#define memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define memset(d, v, n) __builtin_memset((d), (v), (n))
#endif

/* draw-record option (radeon3d_emit.h): a depth-only pass, no colour */
#ifndef RADEON3D_EXEC_SUPPRESS_COLOR_WRITE
#define RADEON3D_EXEC_SUPPRESS_COLOR_WRITE 0x80000000UL
#endif

/* --- the command buffer (an EgCp whose "ring" is the staging buffer) ----- */

static BOOL PersistOk;          /* see EgEmitBatch */
ULONG EgFastDraws, EgFullDraws;
static struct EgHw EmitHw;
static struct EgCp EmitCp;
static RADEONInfoRec Xi;
#define pScrn (&Xi)            /* the xati batch macros name it */
static struct radeon_accel_state Accel;
static ULONG EmitCapacity;
static BOOL EmitOverflow;

/* Context registers: a write to the register after the one the previous
 * SET_CONTEXT_REG packet ended with, right behind it, extends that packet
 * (runs of 3-dword packets otherwise). Forgotten at every buffer start. */
static ULONG CoalHdr = ~0UL, CoalEnd, CoalReg;

static void EgCtxReg(ULONG reg, ULONG val)
{
    if (reg >= SET_CONTEXT_REG_offset && reg < SET_CONTEXT_REG_end) {
        if (CoalHdr != ~0UL && EmitCp.Wptr == CoalEnd && reg == CoalReg + 4UL &&
            !EmitOverflow) {
            EmitCp.Ring[CoalHdr] += 1UL << 16;
        } else {
            CoalHdr = EmitCp.Wptr;
            eg_cp_out(&EmitCp, RADEON_CP_PACKET3 | (IT_SET_CONTEXT_REG << 8) | (1UL << 16));
            eg_cp_out(&EmitCp, (reg - SET_CONTEXT_REG_offset) >> 2);
        }
        eg_cp_out(&EmitCp, val);
        CoalEnd = EmitCp.Wptr;
        CoalReg = reg;
        return;
    }
    {
        RADEONInfoPtr info = &Xi;
        PACK0(reg, 1);
        E32(val);
    }
}
#undef EREG
#define EREG(reg, val) EgCtxReg((reg), (val))

/* eg_cp.c is the chip's; the library only needs the buffer bound check */
BOOL eg_cp_begin(struct EgCp *cp, ULONG n)
{
    if (cp->Wptr + n + 16 > EmitCapacity) {
        EmitOverflow = TRUE;
        cp->Wptr = 0;               /* keep writing in bounds; the batch is discarded */
        return FALSE;
    }
    return TRUE;
}

/* --- VRAM: shader cache and per-slot data streams ----------------------- */

static volatile UBYTE *VramCpu;     /* CPU address of VRAM offset 0 */
static ULONG VramGpu;               /* GPU address of VRAM offset 0 */
static ULONG ShaderOff, ShaderSize, ShaderUsed;
static ULONG StreamOff[EG_EMIT_SLOTS], StreamSize;
static ULONG StreamPos, StreamSlot;

ULONG EgEmitFailStage;
ULONG MGLPPCEmitFailStage;       /* the frontend's diagnostics read it */
/* the R200 emitter's counters, printed by the frontend's diagnostics */
ULONG R3DStatDraws, R3DStatRegCalls, R3DStatRegWrites, R3DStatFast, R3DStatFastOther[48];
ULONG EgEmitFace = 1;
/* profiling counters (printed by the frontend at context end) */
ULONG EgStatDraws, EgStatVtxDwords, EgStatConstUploads, EgStatStride[17], EgStatTcl,
      EgStatClears, EgStatTexBinds, EgStatShaderBinds, EgStatOldPrim[20], EgStatOldVerts[20];               /* PA_SU_SC_MODE_CNTL.FACE for GL front = CCW */

void EgEmitInit(APTR vramCpu, ULONG vramGpu, ULONG shaderOff, ULONG shaderSize,
                ULONG streamOff, ULONG streamSlotSize)
{
    ULONG i;

    VramCpu = (volatile UBYTE *)vramCpu;
    VramGpu = vramGpu;
    ShaderOff = shaderOff;
    ShaderSize = shaderSize;
    ShaderUsed = 0;
    for (i = 0; i < EG_EMIT_SLOTS; ++i)
        StreamOff[i] = streamOff + i * streamSlotSize;
    StreamSize = streamSlotSize;
    PersistOk = FALSE;
    EgEmitFlushShaders();
}

static ULONG SwapLe(ULONG v)
{
    return (v >> 24) | ((v >> 8) & 0xff00UL) | ((v << 8) & 0xff0000UL) | (v << 24);
}

/* Room in the current slot's stream: VRAM offset, or ~0 when full. */
static ULONG StreamAlloc(ULONG bytes, ULONG align)
{
    ULONG pos = (StreamPos + align - 1) & ~(align - 1);

    if (pos + bytes > StreamSize) {
        EmitOverflow = TRUE;
        return ~0UL;
    }
    StreamPos = pos + bytes;
    return StreamOff[StreamSlot] + pos;
}

/* Plain long stores: the bridges corrupt MOVE16 line bursts into VRAM. */
static void ToVram(ULONG offset, const ULONG *src, ULONG dwords)
{
    volatile ULONG *dst = (volatile ULONG *)(VramCpu + offset);

    while (dwords--)
        *dst++ = *src++;
}

static void ToVramLe(ULONG offset, const ULONG *src, ULONG dwords)
{
    volatile ULONG *dst = (volatile ULONG *)(VramCpu + offset);

    while (dwords--)
        *dst++ = SwapLe(*src++);
}

/* --- shader generation --------------------------------------------------- */

#define VS_POS_W4       0x0001UL    /* position has 4 components at posOff */
#define VS_W_SEPARATE   0x0002UL    /* w at wOff (clip-coordinate screen records) */
#define VS_TEX0         0x0004UL
#define VS_TEX1         0x0008UL
#define VS_FOG          0x0010UL
#define VS_STREAMS      0x0020UL    /* EG_EXEC_ARRAYS: one buffer per attribute */

struct VsKey {
    ULONG flags;
    UBYTE posOff, wOff, colOff, tex0Off, tex1Off, fogOff, stride, pad;
};

#define PS_TEX0         0x0001UL
#define PS_TEX0_REPLACE 0x0002UL
#define PS_TEX1         0x0004UL
#define PS_TEX1_REPLACE 0x0008UL
#define PS_RGB2X        0x0010UL
#define PS_ALPHA2X      0x0020UL
#define PS_FOG          0x0040UL
#define PS_FLAT         0x0080UL

struct Shader {
    ULONG key[3];
    ULONG gpu;
    ULONG gprs;
    BOOL used;
    UWORD setupN;           /* the setup's command words, once emitted */
    ULONG setup[40];
};

#define MAX_SHADERS 96
static struct Shader Shaders[MAX_SHADERS];

void EgEmitFlushShaders(void)
{
    ULONG i;

    for (i = 0; i < MAX_SHADERS; ++i)
        Shaders[i].used = FALSE;
    ShaderUsed = 0;
}

static ULONG Prog[512];
static ULONG ProgN;

static void Out(ULONG w) { if (ProgN < 512) Prog[ProgN++] = w; }

/* one ALU slot, OP2: dst.dchan = op(src0.c0, src1.c1) */
static void Alu2(ULONG op, ULONG dst, ULONG dchan, ULONG wm,
                 ULONG s0, ULONG c0, ULONG n0, ULONG s1, ULONG c1, ULONG n1,
                 ULONG last, ULONG clamp, ULONG omod, ULONG bs)
{
    Out(ALU_DWORD0(SRC0_SEL(s0), SRC0_REL(ABSOLUTE), SRC0_ELEM(c0), SRC0_NEG(n0),
                   SRC1_SEL(s1), SRC1_REL(ABSOLUTE), SRC1_ELEM(c1), SRC1_NEG(n1),
                   INDEX_MODE(SQ_INDEX_AR_X), PRED_SEL(SQ_PRED_SEL_OFF), LAST(last)));
    Out(ALU_DWORD1_OP2(SRC0_ABS(0), SRC1_ABS(0), UPDATE_EXECUTE_MASK(0),
                       UPDATE_PRED(0), WRITE_MASK(wm), OMOD(omod), ALU_INST(op),
                       BANK_SWIZZLE(bs), DST_GPR(dst), DST_REL(ABSOLUTE),
                       DST_ELEM(dchan), CLAMP(clamp)));
}

/* one ALU slot, OP3: dst.dchan = src0 * src1 + src2 (MULADD) */
static void Alu3(ULONG op, ULONG dst, ULONG dchan,
                 ULONG s0, ULONG c0, ULONG s1, ULONG c1, ULONG s2, ULONG c2,
                 ULONG n2, ULONG last, ULONG clamp)
{
    Out(ALU_DWORD0(SRC0_SEL(s0), SRC0_REL(ABSOLUTE), SRC0_ELEM(c0), SRC0_NEG(0),
                   SRC1_SEL(s1), SRC1_REL(ABSOLUTE), SRC1_ELEM(c1), SRC1_NEG(0),
                   INDEX_MODE(SQ_INDEX_AR_X), PRED_SEL(SQ_PRED_SEL_OFF), LAST(last)));
    Out(ALU_DWORD1_OP3(SRC2_SEL(s2), SRC2_REL(ABSOLUTE), SRC2_ELEM(c2), SRC2_NEG(n2),
                       ALU_INST(op), BANK_SWIZZLE(SQ_ALU_VEC_012), DST_GPR(dst),
                       DST_REL(ABSOLUTE), DST_ELEM(dchan), CLAMP(clamp)));
}

static void CfAlu(ULONG addr, ULONG count, BOOL kcache)
{
    Out(CF_ALU_DWORD0(ADDR(addr), KCACHE_BANK0(0), KCACHE_BANK1(0),
                      KCACHE_MODE0(kcache ? SQ_CF_KCACHE_LOCK_1 : SQ_CF_KCACHE_NOP)));
    Out(CF_ALU_DWORD1(KCACHE_MODE1(SQ_CF_KCACHE_NOP), KCACHE_ADDR0(0), KCACHE_ADDR1(0),
                      I_COUNT(count), ALT_CONST(0), CF_INST(SQ_CF_INST_ALU),
                      WHOLE_QUAD_MODE(0), BARRIER(1)));
}

static void CfClause(ULONG inst, ULONG addr, ULONG count)
{
    Out(CF_DWORD0(ADDR(addr), JUMPTABLE_SEL(SQ_CF_JUMPTABLE_SEL_CONST_A)));
    Out(CF_DWORD1(POP_COUNT(0), CF_CONST(0), COND(SQ_CF_COND_ACTIVE), I_COUNT(count),
                  VALID_PIXEL_MODE(0), END_OF_PROGRAM(0), CF_INST(inst),
                  WHOLE_QUAD_MODE(0), BARRIER(1)));
}

static void CfExport(ULONG type, ULONG base, ULONG gpr, ULONG burst, BOOL eop)
{
    Out(CF_ALLOC_IMP_EXP_DWORD0(ARRAY_BASE(base), TYPE(type), RW_GPR(gpr),
                                RW_REL(ABSOLUTE), INDEX_GPR(0), ELEM_SIZE(0)));
    Out(CF_ALLOC_IMP_EXP_DWORD1_SWIZ(SRC_SEL_X(SQ_SEL_X), SRC_SEL_Y(SQ_SEL_Y),
                                     SRC_SEL_Z(SQ_SEL_Z), SRC_SEL_W(SQ_SEL_W),
                                     BURST_COUNT(burst), VALID_PIXEL_MODE(0),
                                     END_OF_PROGRAM(eop), CF_INST(SQ_CF_INST_EXPORT_DONE),
                                     MARK(0), BARRIER(1)));
}

static void FetchB(ULONG dst, ULONG buffer, ULONG offset, ULONG format, ULONG bytes,
                   ULONG nfa, ULONG fca, ULONG endian,
                   ULONG sx, ULONG sy, ULONG sz, ULONG sw)
{
    Out(VTX_DWORD0(VTX_INST(SQ_VTX_INST_FETCH), FETCH_TYPE(SQ_VTX_FETCH_VERTEX_DATA),
                   FETCH_WHOLE_QUAD(0), BUFFER_ID(buffer), SRC_GPR(0), SRC_REL(ABSOLUTE),
                   SRC_SEL_X(SQ_SEL_X), MEGA_FETCH_COUNT(bytes)));
    Out(VTX_DWORD1_GPR(DST_GPR(dst), DST_REL(0), DST_SEL_X(sx), DST_SEL_Y(sy),
                       DST_SEL_Z(sz), DST_SEL_W(sw), USE_CONST_FIELDS(0),
                       DATA_FORMAT(format), NUM_FORMAT_ALL(nfa),
                       FORMAT_COMP_ALL(fca), SRF_MODE_ALL(SRF_MODE_ZERO_CLAMP_MINUS_ONE)));
    Out(VTX_DWORD2(OFFSET(offset), ENDIAN_SWAP(endian), CONST_BUF_NO_STRIDE(0),
                   MEGA_FETCH(1), ALT_CONST(0), BUFFER_INDEX_MODE(SQ_CF_INDEX_NONE)));
    Out(VTX_DWORD_PAD);
}

static void Fetch(ULONG dst, ULONG offset, ULONG format, ULONG bytes, ULONG nfa,
                  ULONG fca, ULONG sx, ULONG sy, ULONG sz, ULONG sw)
{
    FetchB(dst, 0, offset, format, bytes, nfa, fca, SQ_ENDIAN_8IN32, sx, sy, sz, sw);
}

static void Pad128(void) { while (ProgN & 3) Out(0); }

/* Vertex shader: fetch, position = c0..c3 * pos (DOT4 rows), export the
 * position and the parameters colour[, tex0][, tex1][, fog].
 * GPRs: R0 vertex index, R1 position, R2.. parameters, then the result. */
static ULONG BuildVs(const struct VsKey *k, ULONG *gprs)
{
    ULONG params = 1, fetches, pos, i, j, cfN, vtxAddr, aluAddr, p;

    if (k->flags & VS_TEX0) ++params;
    if (k->flags & VS_TEX1) ++params;
    if (k->flags & VS_FOG) ++params;
    pos = 2 + params;
    fetches = 1 + params + ((k->flags & VS_W_SEPARATE) ? 1 : 0);
    ProgN = 0;
    cfN = 4;                        /* VC, ALU, export pos, export params */
    aluAddr = cfN;                  /* in 64-bit units */
    vtxAddr = (aluAddr + 16 + 1) & ~1UL;
    CfClause(SQ_CF_INST_VC, vtxAddr, fetches);
    CfAlu(aluAddr, 16, TRUE);
    CfExport(SQ_EXPORT_POS, CF_POS0, pos, 1, FALSE);
    CfExport(SQ_EXPORT_PARAM, 0, 2, params, TRUE);
    /* four DOT4 groups: row r of c0..c3 times R1 into R(pos).r */
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
            Alu2(SQ_OP2_INST_DOT4, pos, j, j == i, ALU_SRC_GPR_BASE + 1, j, 0,
                 ALU_SRC_KCACHE0_BASE + i, j, 0, j == 3, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
    while (ProgN < vtxAddr * 2)
        Out(0);
    if (k->flags & VS_STREAMS) {
        /* EG_EXEC_ARRAYS: buffer 0 position, 1 colour, 2 and 3 the
         * texture coordinates; the formats are in posOff (EGA_FORMATS
         * low byte), wOff bit 0 = interleaved (everything in buffer 0 at
         * colOff, tex0Off, tex1Off), bit 1 = colour array there */
        ULONG size = k->posOff & EGA_POS_SIZE_MASK, col = (k->posOff >> EGA_COL_SHIFT) & 3;
        BOOL il = (k->wOff & 1) != 0;
        ULONG cb = il && (k->wOff & 2) ? 0 : 1, co = cb ? 0 : k->colOff;
        if (size == 2)
            FetchB(1, 0, 0, FMT_32_32_FLOAT, 8, SQ_NUM_FORMAT_SCALED, SQ_FORMAT_COMP_SIGNED,
                   SQ_ENDIAN_8IN32, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_0, SQ_SEL_1);
        else if (size == 4)
            FetchB(1, 0, 0, FMT_32_32_32_32_FLOAT, 16, SQ_NUM_FORMAT_SCALED, SQ_FORMAT_COMP_SIGNED,
                   SQ_ENDIAN_8IN32, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_Z, SQ_SEL_W);
        else
            FetchB(1, 0, 0, FMT_32_32_32_FLOAT, 12, SQ_NUM_FORMAT_SCALED, SQ_FORMAT_COMP_SIGNED,
                   SQ_ENDIAN_8IN32, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_Z, SQ_SEL_1);
        if (col == EGA_COL_FLOAT3)
            FetchB(2, cb, co, FMT_32_32_32_FLOAT, 12, SQ_NUM_FORMAT_SCALED, SQ_FORMAT_COMP_SIGNED,
                   SQ_ENDIAN_8IN32, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_Z, SQ_SEL_1);
        else if (col == EGA_COL_FLOAT4)
            FetchB(2, cb, co, FMT_32_32_32_32_FLOAT, 16, SQ_NUM_FORMAT_SCALED, SQ_FORMAT_COMP_SIGNED,
                   SQ_ENDIAN_8IN32, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_Z, SQ_SEL_W);
        else if (col == EGA_COL_ARGB)   /* A, R, G, B bytes: X = A */
            FetchB(2, cb, co, FMT_8_8_8_8, 4, SQ_NUM_FORMAT_NORM, SQ_FORMAT_COMP_UNSIGNED,
                   SQ_ENDIAN_NONE, SQ_SEL_Y, SQ_SEL_Z, SQ_SEL_W, SQ_SEL_X);
        else    /* R, G, B, A bytes in memory order: no swap */
            FetchB(2, cb, co, FMT_8_8_8_8, 4, SQ_NUM_FORMAT_NORM, SQ_FORMAT_COMP_UNSIGNED,
                   SQ_ENDIAN_NONE, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_Z, SQ_SEL_W);
        p = 3;
        if (k->flags & VS_TEX0)
            FetchB(p++, il ? 0 : 2, il ? k->tex0Off : 0, FMT_32_32_FLOAT, 8, SQ_NUM_FORMAT_SCALED,
                   SQ_FORMAT_COMP_SIGNED, SQ_ENDIAN_8IN32, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_0, SQ_SEL_1);
        if (k->flags & VS_TEX1)
            FetchB(p++, il ? 0 : 3, il ? k->tex1Off : 0, FMT_32_32_FLOAT, 8, SQ_NUM_FORMAT_SCALED,
                   SQ_FORMAT_COMP_SIGNED, SQ_ENDIAN_8IN32, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_0, SQ_SEL_1);
        *gprs = pos + 1;
        return ProgN;
    }
    if (k->flags & VS_POS_W4)
        Fetch(1, k->posOff, FMT_32_32_32_32_FLOAT, 16, SQ_NUM_FORMAT_SCALED,
              SQ_FORMAT_COMP_SIGNED, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_Z, SQ_SEL_W);
    else
        Fetch(1, k->posOff, FMT_32_32_32_FLOAT, 12, SQ_NUM_FORMAT_SCALED,
              SQ_FORMAT_COMP_SIGNED, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_Z,
              (k->flags & VS_W_SEPARATE) ? SQ_SEL_MASK : SQ_SEL_1);
    if (k->flags & VS_W_SEPARATE)
        Fetch(1, k->wOff, FMT_32_FLOAT, 4, SQ_NUM_FORMAT_SCALED, SQ_FORMAT_COMP_SIGNED,
              SQ_SEL_MASK, SQ_SEL_MASK, SQ_SEL_MASK, SQ_SEL_X);
    /* packed ARGB long, 8IN32: X = blue ... W = alpha */
    Fetch(2, k->colOff, FMT_8_8_8_8, 4, SQ_NUM_FORMAT_NORM, SQ_FORMAT_COMP_UNSIGNED,
          SQ_SEL_Z, SQ_SEL_Y, SQ_SEL_X, SQ_SEL_W);
    p = 3;
    if (k->flags & VS_TEX0)
        Fetch(p++, k->tex0Off, FMT_32_32_FLOAT, 8, SQ_NUM_FORMAT_SCALED,
              SQ_FORMAT_COMP_SIGNED, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_0, SQ_SEL_1);
    if (k->flags & VS_TEX1)
        Fetch(p++, k->tex1Off, FMT_32_32_FLOAT, 8, SQ_NUM_FORMAT_SCALED,
              SQ_FORMAT_COMP_SIGNED, SQ_SEL_X, SQ_SEL_Y, SQ_SEL_0, SQ_SEL_1);
    if (k->flags & VS_FOG)
        Fetch(p++, k->fogOff, FMT_32_FLOAT, 4, SQ_NUM_FORMAT_SCALED,
              SQ_FORMAT_COMP_SIGNED, SQ_SEL_X, SQ_SEL_0, SQ_SEL_0, SQ_SEL_1);
    *gprs = pos + 1;
    return ProgN;
}

/* Pixel shader: interpolate the inputs (perspective), sample, combine,
 * export. GPRs: R0 barycentrics, R1 colour, R2.. other inputs, then the
 * samples and the result. Constants: c0 = fog colour. */
static ULONG BuildPs(ULONG key, ULONG *gprs)
{
    ULONG inputs = 1, in, tc0 = 0, tc1 = 0, fog = 0, t0, t1, outg, i, s;
    ULONG interpN, texN, combN, cfN, interpAddr, texAddr, combAddr, cf;
    ULONG src;

    if (key & PS_TEX0) tc0 = 1 + inputs++;
    if (key & PS_TEX1) tc1 = 1 + inputs++;
    if (key & PS_FOG) fog = 1 + inputs++;
    t0 = 1 + inputs;
    t1 = t0 + 1;
    outg = t1 + 1;
    texN = ((key & PS_TEX0) ? 1 : 0) + ((key & PS_TEX1) ? 1 : 0);
    interpN = inputs * 8;
    /* combine: worst case 4 (tex0) + 4 (tex1) + 8 (fog) + 4 (move) */
    ProgN = 0;
    cfN = 2 + (texN ? 1 : 0) + 1;           /* interp ALU, [TEX], combine ALU, export */
    interpAddr = cfN;
    texAddr = (interpAddr + interpN + 1) & ~1UL;
    combAddr = texAddr + texN * 2;
    /* control flow, addresses patched below once the combine length is known */
    cf = ProgN;
    for (i = 0; i < cfN * 2; ++i)
        Out(0);
    /* interpolation */
    for (in = 0; in < inputs; ++in) {
        ULONG dst = 1 + in;
        if (in == 0 && (key & PS_FLAT)) {
            for (i = 0; i < 4; ++i)
                Alu2(SQ_OP2_INST_INTERP_LOAD_P0, dst, i, 1, ALU_SRC_PARAM_BASE + in, i, 0,
                     ALU_SRC_GPR_BASE + 0, 0, 0, i == 3, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
            for (i = 0; i < 4; ++i)     /* keep 8 slots per input */
                Alu2(SQ_OP2_INST_MOV, dst, i, 0, ALU_SRC_GPR_BASE + dst, i, 0,
                     ALU_SRC_GPR_BASE + dst, i, 0, i == 3, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
            continue;
        }
        for (i = 0; i < 8; ++i)
            Alu2(i < 4 ? SQ_OP2_INST_INTERP_ZW : SQ_OP2_INST_INTERP_XY, dst, i & 3,
                 i < 4 ? ((i & 3) >= 2) : ((i & 3) < 2),
                 ALU_SRC_GPR_BASE + 0, (i & 1) ? ELEM_X : ELEM_Y, 0,
                 ALU_SRC_PARAM_BASE + in, ELEM_X, 0, (i & 3) == 3, 0,
                 SQ_ALU_OMOD_OFF, SQ_ALU_VEC_210);
    }
    while (ProgN < texAddr * 2)
        Out(0);
    /* samples */
    for (s = 0; s < 2; ++s) {
        ULONG tc = s ? tc1 : tc0, dst = s ? t1 : t0;
        if (!tc)
            continue;
        Out(TEX_DWORD0(TEX_INST(SQ_TEX_INST_SAMPLE), INST_MOD(0), FETCH_WHOLE_QUAD(0),
                       RESOURCE_ID(s), SRC_GPR(tc), SRC_REL(ABSOLUTE), ALT_CONST(0),
                       RESOURCE_INDEX_MODE(SQ_CF_INDEX_NONE),
                       SAMPLER_INDEX_MODE(SQ_CF_INDEX_NONE)));
        Out(TEX_DWORD1(DST_GPR(dst), DST_REL(ABSOLUTE), DST_SEL_X(SQ_SEL_X),
                       DST_SEL_Y(SQ_SEL_Y), DST_SEL_Z(SQ_SEL_Z), DST_SEL_W(SQ_SEL_W),
                       LOD_BIAS(0), COORD_TYPE_X(TEX_NORMALIZED),
                       COORD_TYPE_Y(TEX_NORMALIZED), COORD_TYPE_Z(TEX_NORMALIZED),
                       COORD_TYPE_W(TEX_NORMALIZED)));
        Out(TEX_DWORD2(OFFSET_X(0), OFFSET_Y(0), OFFSET_Z(0), SAMPLER_ID(s),
                       SRC_SEL_X(SQ_SEL_X), SRC_SEL_Y(SQ_SEL_Y), SRC_SEL_Z(SQ_SEL_0),
                       SRC_SEL_W(SQ_SEL_1)));
        Out(TEX_DWORD_PAD);
    }
    /* combine into R(outg), one slot per group */
    combN = 0;
    src = 1;                                /* the interpolated colour */
    if (key & PS_TEX0) {
        for (i = 0; i < 4; ++i) {
            if (key & PS_TEX0_REPLACE)
                Alu2(SQ_OP2_INST_MOV, outg, i, 1, ALU_SRC_GPR_BASE + t0, i, 0,
                     ALU_SRC_GPR_BASE + t0, i, 0, 1, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
            else
                Alu2(SQ_OP2_INST_MUL, outg, i, 1, ALU_SRC_GPR_BASE + 1, i, 0,
                     ALU_SRC_GPR_BASE + t0, i, 0, 1, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
            ++combN;
        }
        src = outg;
    }
    if (key & PS_TEX1) {
        for (i = 0; i < 4; ++i) {
            if (key & PS_TEX1_REPLACE)
                Alu2(SQ_OP2_INST_MOV, outg, i, 1, ALU_SRC_GPR_BASE + t1, i, 0,
                     ALU_SRC_GPR_BASE + t1, i, 0, 1, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
            else
                Alu2(SQ_OP2_INST_MUL, outg, i, 1, ALU_SRC_GPR_BASE + src, i, 0,
                     ALU_SRC_GPR_BASE + t1, i, 0, 1, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
            ++combN;
        }
        src = outg;
    }
    if (src != outg) {
        for (i = 0; i < 4; ++i) {
            Alu2(SQ_OP2_INST_MOV, outg, i, 1, ALU_SRC_GPR_BASE + src, i, 0,
                 ALU_SRC_GPR_BASE + src, i, 0, 1, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
            ++combN;
        }
    }
    /* the texture environment's x2 scales, clamped */
    if (key & (PS_RGB2X | PS_ALPHA2X)) {
        for (i = 0; i < 4; ++i) {
            BOOL two = i < 3 ? (key & PS_RGB2X) != 0 : (key & PS_ALPHA2X) != 0;
            if (!two)
                continue;
            Alu2(SQ_OP2_INST_MOV, outg, i, 1, ALU_SRC_GPR_BASE + outg, i, 0,
                 ALU_SRC_GPR_BASE + outg, i, 0, 1, 1, SQ_ALU_OMOD_M2, SQ_ALU_VEC_012);
            ++combN;
        }
    }
    /* fog: rgb = (rgb - fogc) * f + fogc, f = 1 unfogged */
    if (key & PS_FOG) {
        for (i = 0; i < 3; ++i) {
            Alu2(SQ_OP2_INST_ADD, outg, i, 1, ALU_SRC_GPR_BASE + outg, i, 0,
                 ALU_SRC_KCACHE0_BASE + 0, i, 1, 1, 0, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
            Alu3(SQ_OP3_INST_MULADD, outg, i, ALU_SRC_GPR_BASE + outg, i,
                 ALU_SRC_GPR_BASE + fog, ELEM_X, ALU_SRC_KCACHE0_BASE + 0, i, 0, 1, 0);
            combN += 2;
        }
    }
    /* clamp for the colour buffer */
    for (i = 0; i < 4; ++i) {
        Alu2(SQ_OP2_INST_MOV, outg, i, 1, ALU_SRC_GPR_BASE + outg, i, 0,
             ALU_SRC_GPR_BASE + outg, i, 0, 1, 1, SQ_ALU_OMOD_OFF, SQ_ALU_VEC_012);
        ++combN;
    }
    /* control flow */
    {
        ULONG save = ProgN;
        ProgN = cf;
        CfAlu(interpAddr, interpN, FALSE);
        if (texN)
            CfClause(SQ_CF_INST_TC, texAddr, texN);
        CfAlu(combAddr, combN, (key & PS_FOG) != 0);
        Out(CF_ALLOC_IMP_EXP_DWORD0(ARRAY_BASE(CF_PIXEL_MRT0), TYPE(SQ_EXPORT_PIXEL),
                                    RW_GPR(outg), RW_REL(ABSOLUTE), INDEX_GPR(0),
                                    ELEM_SIZE(1)));
        Out(CF_ALLOC_IMP_EXP_DWORD1_SWIZ(SRC_SEL_X(SQ_SEL_X), SRC_SEL_Y(SQ_SEL_Y),
                                         SRC_SEL_Z(SQ_SEL_Z), SRC_SEL_W(SQ_SEL_W),
                                         BURST_COUNT(1), VALID_PIXEL_MODE(0),
                                         END_OF_PROGRAM(1), CF_INST(SQ_CF_INST_EXPORT_DONE),
                                         MARK(0), BARRIER(1)));
        ProgN = save;
    }
    *gprs = outg + 1;
    return ProgN;
}

/* A shader for key k0/k1/k2 in the cache, built and uploaded on first use;
 * its GPU address, or 0. */
static struct Shader *GetShader(ULONG k0, ULONG k1, ULONG k2, BOOL pixel)
{
    ULONG i, n, gprs = 0, off;
    struct Shader *s;

    for (i = 0; i < MAX_SHADERS; ++i) {
        s = &Shaders[i];
        if (s->used && s->key[0] == k0 && s->key[1] == k1 && s->key[2] == k2)
            return s;
    }
    for (i = 0; i < MAX_SHADERS && Shaders[i].used; ++i)
        ;
    if (i == MAX_SHADERS)
        return NULL;
    s = &Shaders[i];
    if (pixel)
        n = BuildPs(k0, &gprs);
    else {
        struct VsKey k;
        k.flags = k0;
        k.posOff = (UBYTE)(k1 >> 24); k.wOff = (UBYTE)(k1 >> 16);
        k.colOff = (UBYTE)(k1 >> 8); k.tex0Off = (UBYTE)k1;
        k.tex1Off = (UBYTE)(k2 >> 24); k.fogOff = (UBYTE)(k2 >> 16);
        k.stride = (UBYTE)(k2 >> 8); k.pad = 0;
        n = BuildVs(&k, &gprs);
    }
    Pad128();
    n = ProgN;
    off = (ShaderUsed + 255UL) & ~255UL;
    if (off + n * 4 > ShaderSize - 256 || ProgN >= 512)   /* last 256: present constants */
        return NULL;
    /* the words are little-endian already (cpu_to_le32 in the macros) */
    ToVram(ShaderOff + off, Prog, n);
    ShaderUsed = off + n * 4;
    s->key[0] = k0; s->key[1] = k1; s->key[2] = k2;
    s->setupN = 0;
    s->gpu = VramGpu + ShaderOff + off;
    s->gprs = gprs;
    s->used = TRUE;
    return s;
}

/* --- state shadows within one buffer ------------------------------------ */

struct Target { ULONG gpu, pitch, height, format; };
struct Depth { ULONG gpu, pitch, height; ULONG control; };

static struct {
    BOOL valid;
    struct Target target;
    struct Depth depth;
    ULONG cbMask, blend, alphaCtl, alphaRef, dbShader;
    ULONG suMode, clipCntl, vteCntl, vport[6], pointSize;
    ULONG scissor[4];
    struct Shader *vs, *ps;
    ULONG psInputs, flat;
    ULONG vsConst[16], vsConstGpu;
    ULONG psConst[4], psConstGpu;
    ULONG tex[2][4];        /* gpu, size, state, pitch */
} Sh;

static void Replay(const ULONG *w, ULONG n);
static ULONG CaptureNoSync(ULONG *dst, ULONG start, ULONG end);
/* Draw's fast path: the last array draw's header and descriptors */
static __inline__ BOOL SameLongs(const void *a, const void *b, ULONG longs);
static const ULONG *PrevHdr;
static BOOL DrawTexOnly;        /* the walker: an EG_EXEC_ARRAYS_TEXONLY record */     /* the record itself (valid for the walk) */
static ULONG PrevFmt, PrevOffs;
static BOOL PrevOk;

static void Reg(ULONG reg, ULONG value)
{
    RADEONInfoPtr info = &Xi;
    BEGIN_BATCH(3);
    EREG(reg, value);
    END_BATCH();
}

static void EmitTarget(const struct Target *t)
{
    RADEONInfoPtr info = &Xi;
    ULONG bpp = (t->format == RADEON3D_FORMAT_R5G6B5PC ||
                 t->format == EG_FORMAT_R5G6B5 || t->format == EG_FORMAT_RAW16) ? 2 : 4;
    ULONG pitchPx = t->pitch / bpp, h = (t->height + 7) & ~7UL, info0, fmt, endian;

    switch (t->format) {
    case EG_FORMAT_RAW16:          fmt = COLOR_5_6_5; endian = ENDIAN_NONE; break;
    case EG_FORMAT_RAW32:          fmt = COLOR_8_8_8_8; endian = ENDIAN_NONE; break;
    case RADEON3D_FORMAT_R5G6B5PC: fmt = COLOR_5_6_5; endian = ENDIAN_NONE; break;
    case EG_FORMAT_R5G6B5:         fmt = COLOR_5_6_5; endian = ENDIAN_8IN16; break;
    case EG_FORMAT_A8R8G8B8:       fmt = COLOR_8_8_8_8; endian = ENDIAN_8IN32; break;
    default:                       fmt = COLOR_8_8_8_8; endian = ENDIAN_NONE; break;
    }
    /* 565: the reversed standard order puts red in the high bits, the
     * bytes the display and Picasso96 expect (MEASURED: the standard swap
     * gave red/blue exchanged frames on a flip, the alternate one drops
     * red); 8888: ARGB (alternate) as the 2D path (eg_draw.c Target) */
    info0 = (endian << ENDIAN_shift) | (fmt << CB_COLOR0_INFO__FORMAT_shift) |
            (ARRAY_LINEAR_GENERAL << CB_COLOR0_INFO__ARRAY_MODE_shift) |
            ((t->format == EG_FORMAT_RAW32 || t->format == EG_FORMAT_RAW16 ? 0UL :
              bpp == 4 ? 1UL : 2UL) << COMP_SWAP_shift) |
            (EXPORT_4C_16BPC << SOURCE_FORMAT_shift) | BLEND_CLAMP_bit;
    BEGIN_BATCH(40);
    EREG(CB_COLOR0_BASE, t->gpu >> 8);
    EREG(CB_COLOR0_CMASK, 0);
    EREG(CB_COLOR0_FMASK, 0);
    EREG(CB_COLOR0_ATTRIB, CB_COLOR0_ATTRIB__NON_DISP_TILING_ORDER_bit);
    EREG(CB_COLOR0_INFO, info0);
    EREG(CB_COLOR0_PITCH, pitchPx / 8 - 1);
    EREG(CB_COLOR0_SLICE, pitchPx * h / 64 - 1);
    EREG(CB_COLOR0_VIEW, 0);
    EREG(CB_COLOR0_DIM, ((pitchPx - 1) << WIDTH_MAX_shift) | ((t->height - 1) << HEIGHT_MAX_shift));
    EREG(CB_COLOR0_CMASK_SLICE, 0);
    EREG(CB_COLOR0_FMASK_SLICE, 0);
    EREG(CB_COLOR_CONTROL, (0xccUL << 16) | (CB_NORMAL << CB_COLOR_CONTROL__MODE_shift));
    END_BATCH();
}

static void EmitDepth(const struct Depth *d)
{
    RADEONInfoPtr info = &Xi;
    ULONG pitchPx = d->pitch / 2, h = (d->height + 7) & ~7UL;

    BEGIN_BATCH(24);
    if (!d->gpu) {
        EREG(DB_Z_INFO, 0);
        EREG(DB_STENCIL_INFO, 0);
        EREG(DB_DEPTH_CONTROL, 0);
        END_BATCH();
        return;
    }
    EREG(DB_Z_INFO, (Z_16 << DB_Z_INFO__FORMAT_shift) |
                    (ARRAY_1D_TILED_THIN1 << DB_Z_INFO__ARRAY_MODE_shift));
    EREG(DB_STENCIL_INFO, 0);
    EREG(DB_Z_READ_BASE, d->gpu >> 8);
    EREG(DB_Z_WRITE_BASE, d->gpu >> 8);
    EREG(DB_STENCIL_READ_BASE, d->gpu >> 8);
    EREG(DB_STENCIL_WRITE_BASE, d->gpu >> 8);
    EREG(DB_DEPTH_SIZE, ((pitchPx / 8 - 1) << PITCH_TILE_MAX_shift) |
                        ((h / 8 - 1) << HEIGHT_TILE_MAX_shift));
    EREG(DB_DEPTH_SLICE, (pitchPx * h / 64 - 1) << SLICE_TILE_MAX_shift);
    EREG(DB_DEPTH_VIEW, 0);
    EREG(DB_DEPTH_CONTROL, d->control);
    END_BATCH();
}

static ULONG ConstUpload(const ULONG *values, ULONG dwords)
{
    ULONG off = StreamAlloc(256, 256);

    ++EgStatConstUploads;
    if (off == ~0UL)
        return 0;
    ToVramLe(off, values, dwords);
    return VramGpu + off;
}

static void BindShaders(struct Shader *vs, struct Shader *ps, ULONG inputs, BOOL flat)
{
    RADEONInfoPtr info = &Xi;
    shader_config_t c;
    ULONG i;

    if (Sh.valid && Sh.vs == vs && Sh.ps == ps && Sh.psInputs == inputs && Sh.flat == flat)
        return;
    ++EgStatShaderBinds;
    if (!Sh.valid || Sh.vs != vs) {
        if (vs->setupN)
            Replay(vs->setup, vs->setupN);
        else {
            ULONG start = EmitCp.Wptr, k;
            CLEAR(c);
            c.shader_addr = vs->gpu;
            c.shader_size = 512;
            c.num_gprs = vs->gprs;
            evergreen_vs_setup(&Xi, &c, RADEON_GEM_DOMAIN_VRAM);
            if (!EmitOverflow && EmitCp.Wptr - start <= 40UL) {
                vs->setupN = (UWORD)CaptureNoSync(vs->setup, start, EmitCp.Wptr);
                (void)k;
            }
        }
    }
    if (!Sh.valid || Sh.ps != ps) {
        if (ps->setupN)
            Replay(ps->setup, ps->setupN);
        else {
            ULONG start = EmitCp.Wptr, k;
            CLEAR(c);
            c.shader_addr = ps->gpu;
            c.shader_size = 512;
            c.num_gprs = ps->gprs;
            c.export_mode = 2;
            evergreen_ps_setup(&Xi, &c, RADEON_GEM_DOMAIN_VRAM);
            if (!EmitOverflow && EmitCp.Wptr - start <= 40UL) {
                ps->setupN = (UWORD)CaptureNoSync(ps->setup, start, EmitCp.Wptr);
                (void)k;
            }
        }
    }
    BEGIN_BATCH(40);
    EREG(SPI_VS_OUT_CONFIG, (inputs - 1) << VS_EXPORT_COUNT_shift);
    EREG(SPI_VS_OUT_ID_0, (0 << SEMANTIC_0_shift) | (1 << SEMANTIC_1_shift) |
                          (2 << SEMANTIC_2_shift) | (3UL << SEMANTIC_3_shift));
    for (i = 0; i < inputs; ++i)
        EREG(SPI_PS_INPUT_CNTL_0 + i * 4, (i << SEMANTIC_shift) | (1 << DEFAULT_VAL_shift) |
                                          ((i == 0 && flat) ? FLAT_SHADE_bit : 0));
    EREG(SPI_PS_IN_CONTROL_0, (inputs << NUM_INTERP_shift) | PERSP_GRADIENT_ENA_bit);
    EREG(SPI_PS_IN_CONTROL_1, 0);
    EREG(SPI_INTERP_CONTROL_0, flat ? FLAT_SHADE_ENA_bit : 0);
    EREG(SPI_BARYC_CNTL, 1 << PERSP_CENTER_ENA_shift);
    END_BATCH();
    Sh.vs = vs;
    Sh.ps = ps;
    Sh.psInputs = inputs;
    Sh.flat = flat;
}

/* Command words emitted once and replayed: a texture unit's resource and
 * sampler for (unit, address, size, state). */
#define TEX_REPLAY 256         /* direct mapped by the key's hash */
static struct { ULONG key[4]; ULONG n; ULONG w[24]; } TexReplay[TEX_REPLAY];
/* the sampler words each unit has in this buffer (0: unknown) */
static ULONG SampShadow[2][3];

/* Captured command words without SURFACE_SYNC packets (the shader
 * setups flush the shader cache, which every buffer start already does);
 * the number of words kept. */
static ULONG CaptureNoSync(ULONG *dst, ULONG start, ULONG end)
{
    ULONG i = start, n = 0;

    while (i < end) {
        ULONG h = EmitCp.Ring[i], len = 1;
        if ((h >> 30) == 3UL) {
            len = ((h >> 16) & 0x3fffUL) + 2UL;
            if (((h >> 8) & 0xffUL) == IT_SURFACE_SYNC) {
                i += len;
                continue;
            }
        }
        while (len-- && i < end)
            dst[n++] = EmitCp.Ring[i++];
    }
    return n;
}

static void Replay(const ULONG *w, ULONG n)
{
    RADEONInfoPtr info = &Xi;
    ULONG i;

    BEGIN_BATCH(n);
    for (i = 0; i < n; ++i)
        E32(w[i]);
    END_BATCH();
}

static void EmitTexture2(ULONG unit, const struct MGLPPCEmitSurface *s, ULONG offset,
                         ULONG size, ULONG state);

static void EmitTexture(ULONG unit, const struct MGLPPCEmitSurface *s, ULONG offset,
                        ULONG size, ULONG state)
{
    ULONG gpu = s->gpuAddress + offset, k3 = (unit << 30) ^ size, i, start;
    ULONG key = state & (RADEON3D_TEX_STATE_MASK | EG_TEX_FORMAT_MASK);

    if (Sh.valid && Sh.tex[unit][0] == gpu && Sh.tex[unit][1] == size &&
        Sh.tex[unit][2] == key)
        return;
    i = ((gpu >> 8) ^ (gpu >> 17) ^ (k3 * 0x9e3779b1UL >> 24) ^ (key * 0x85ebca6bUL >> 24)) &
        (TEX_REPLAY - 1UL);
    if (TexReplay[i].n && TexReplay[i].key[0] == gpu && TexReplay[i].key[1] == k3 &&
        TexReplay[i].key[2] == key) {
            /* the resource (10 words), then the sampler (5) unless the unit
             * has the same sampler already */
            if (TexReplay[i].n == 15UL && SampShadow[unit][0] &&
                SampShadow[unit][0] == TexReplay[i].w[12] &&
                SampShadow[unit][1] == TexReplay[i].w[13] &&
                SampShadow[unit][2] == TexReplay[i].w[14])
                Replay(TexReplay[i].w, 10);
            else {
                Replay(TexReplay[i].w, TexReplay[i].n);
                if (TexReplay[i].n == 15UL) {
                    SampShadow[unit][0] = TexReplay[i].w[12];
                    SampShadow[unit][1] = TexReplay[i].w[13];
                    SampShadow[unit][2] = TexReplay[i].w[14];
                }
            }
            Sh.tex[unit][0] = gpu;
            Sh.tex[unit][1] = size;
            Sh.tex[unit][2] = key;
            Sh.tex[unit][3] = TexReplay[i].key[3];
            return;
        }
    start = EmitCp.Wptr;
    EmitTexture2(unit, s, offset, size, state);
    TexReplay[i].n = 0;
    if (!EmitOverflow && EmitCp.Wptr > start && EmitCp.Wptr - start <= 24UL) {
        ULONG k;
        for (k = 0; k < EmitCp.Wptr - start; ++k)
            TexReplay[i].w[k] = EmitCp.Ring[start + k];
        TexReplay[i].key[0] = gpu;
        TexReplay[i].key[1] = k3;
        TexReplay[i].key[2] = key;
        TexReplay[i].key[3] = Sh.tex[unit][3];
        TexReplay[i].n = EmitCp.Wptr - start;
        if (TexReplay[i].n == 15UL) {
            SampShadow[unit][0] = TexReplay[i].w[12];
            SampShadow[unit][1] = TexReplay[i].w[13];
            SampShadow[unit][2] = TexReplay[i].w[14];
        } else
            SampShadow[unit][0] = 0;
    }
}

/* the texture replays name addresses: forgotten when a texture may move */
void EgEmitForgetTextures(void)
{
    ULONG i;
    for (i = 0; i < TEX_REPLAY; ++i)
        TexReplay[i].n = 0;
}

static void EmitTexture2(ULONG unit, const struct MGLPPCEmitSurface *s, ULONG offset,
                         ULONG size, ULONG state)
{
    tex_resource_t tex;
    tex_sampler_t samp;
    ULONG w = (size & 0xffffUL) + 1, h = (size >> 16) + 1, min, stride, levels;
    ULONG gpu = s->gpuAddress + offset;
    ULONG fmt = (state & EG_TEX_FORMAT_MASK) >> EG_TEX_FORMAT_SHIFT;
    ULONG bpe = fmt == EG_TEX_FORMAT_8888 ? 4 : fmt == EG_TEX_FORMAT_L8 ? 1 : 2;
    ULONG key = state & (RADEON3D_TEX_STATE_MASK | EG_TEX_FORMAT_MASK);

    /* the frontend's Evergreen mip tree (TextureLevelLayout): rows in
     * whole 256 bytes, level 1 at the first 256-byte boundary after level 0 */
    levels = ((state & RADEON3D_TEX_LEVELS_MASK) >> RADEON3D_TEX_LEVELS_SHIFT) + 1;
    stride = (w * bpe + 255UL) & ~255UL;
    if (Sh.valid && Sh.tex[unit][0] == gpu && Sh.tex[unit][1] == size &&
        Sh.tex[unit][2] == key && Sh.tex[unit][3] == stride)
        return;
    CLEAR(tex);
    tex.id = unit;
    tex.w = w;
    tex.h = h;
    tex.pitch = stride / bpe;
    tex.depth = 0;
    tex.dim = SQ_TEX_DIM_2D;
    tex.base = gpu;
    tex.mip_base = gpu + ((stride * h + 255UL) & ~255UL);
    tex.size = stride * h;
    tex.num_format_all = SQ_NUM_FORMAT_NORM;
    /* BGRA bytes, no swap: X = blue, Y = green, Z = red, W = alpha; the
     * 16-bit formats are big-endian shorts with the colour in the same
     * order from the low bits (B, G, R, A), swapped 8-in-16 */
    tex.format = fmt == EG_TEX_FORMAT_4444 ? FMT_4_4_4_4 :
                 fmt == EG_TEX_FORMAT_565 ? FMT_5_6_5 :
                 fmt == EG_TEX_FORMAT_L8 ? FMT_8 : FMT_8_8_8_8;
    tex.endian = bpe == 2 ? SQ_ENDIAN_8IN16 : SQ_ENDIAN_NONE;
    tex.dst_sel_x = SQ_SEL_Z;
    tex.dst_sel_y = SQ_SEL_Y;
    tex.dst_sel_z = SQ_SEL_X;
    tex.dst_sel_w = fmt == EG_TEX_FORMAT_565 ? SQ_SEL_1 : SQ_SEL_W;
    if (fmt == EG_TEX_FORMAT_L8) {
        tex.dst_sel_x = tex.dst_sel_y = tex.dst_sel_z = SQ_SEL_X;
        tex.dst_sel_w = SQ_SEL_1;
    }
    tex.base_level = 0;
    tex.last_level = levels - 1;
    tex.array_mode = ARRAY_LINEAR_ALIGNED;
    {
        /* evergreen_set_tex_resource's words for a linear texture, without
         * its texture-cache flush: every buffer starts with one, and the
         * texels change only between buffers */
        RADEONInfoPtr info = &Xi;
        BEGIN_BATCH(10);
        PACK0(SQ_FETCH_RESOURCE + unit * SQ_FETCH_RESOURCE_offset, 8);
        E32((SQ_TEX_DIM_2D << DIM_shift) | ((((tex.pitch + 7) >> 3) - 1) << PITCH_shift) |
            ((w - 1) << TEX_WIDTH_shift));
        E32((ARRAY_LINEAR_ALIGNED << SQ_TEX_RESOURCE_WORD1_0__ARRAY_MODE_shift) |
            ((h - 1) << TEX_HEIGHT_shift));
        E32(tex.base >> 8);
        E32(tex.mip_base >> 8);
        E32((SQ_NUM_FORMAT_NORM << SQ_TEX_RESOURCE_WORD4_0__NUM_FORMAT_ALL_shift) |
            (tex.endian << SQ_TEX_RESOURCE_WORD4_0__ENDIAN_SWAP_shift) |
            (tex.dst_sel_x << SQ_TEX_RESOURCE_WORD4_0__DST_SEL_X_shift) |
            (tex.dst_sel_y << SQ_TEX_RESOURCE_WORD4_0__DST_SEL_Y_shift) |
            (tex.dst_sel_z << SQ_TEX_RESOURCE_WORD4_0__DST_SEL_Z_shift) |
            (tex.dst_sel_w << SQ_TEX_RESOURCE_WORD4_0__DST_SEL_W_shift));
        E32(tex.last_level << LAST_LEVEL_shift);
        E32(4UL << SQ_TEX_RESOURCE_WORD6_0__TILE_SPLIT_shift);
        E32((tex.format << SQ_TEX_RESOURCE_WORD7_0__DATA_FORMAT_shift) |
            (1UL << SQ_TEX_RESOURCE_WORD7_0__NUM_BANKS_shift) |
            (SQ_TEX_VTX_VALID_TEXTURE << SQ_TEX_RESOURCE_WORD7_0__TYPE_shift));
        END_BATCH();
    }
    CLEAR(samp);
    samp.id = unit;
    samp.clamp_x = (state & RADEON3D_TEX_REPEAT_S) ? SQ_TEX_WRAP
                 : (state & RADEON3D_TEX_BORDER_S) ? SQ_TEX_CLAMP_BORDER
                 : SQ_TEX_CLAMP_LAST_TEXEL;
    samp.clamp_y = (state & RADEON3D_TEX_REPEAT_T) ? SQ_TEX_WRAP : SQ_TEX_CLAMP_LAST_TEXEL;
    samp.clamp_z = SQ_TEX_WRAP;
    samp.border_color = SQ_TEX_BORDER_COLOR_OPAQUE_WHITE;
    samp.xy_mag_filter = (state & RADEON3D_TEX_MAG_LINEAR) ? SQ_TEX_XY_FILTER_BILINEAR
                                                           : SQ_TEX_XY_FILTER_POINT;
    min = (state & RADEON3D_TEX_MIN_MASK) >> RADEON3D_TEX_MIN_SHIFT;
    samp.xy_min_filter = (min == RADEON3D_TEX_MIN_LINEAR ||
                          min == RADEON3D_TEX_MIN_LINEAR_MIPMAP_NEAREST ||
                          min == RADEON3D_TEX_MIN_LINEAR_MIPMAP_LINEAR)
                             ? SQ_TEX_XY_FILTER_BILINEAR : SQ_TEX_XY_FILTER_POINT;
    samp.z_filter = SQ_TEX_Z_FILTER_NONE;
    samp.mip_filter = (levels < 2 || min <= RADEON3D_TEX_MIN_LINEAR) ? SQ_TEX_Z_FILTER_NONE
                    : (min == RADEON3D_TEX_MIN_NEAREST_MIPMAP_LINEAR ||
                       min == RADEON3D_TEX_MIN_LINEAR_MIPMAP_LINEAR) ? SQ_TEX_Z_FILTER_LINEAR
                    : SQ_TEX_Z_FILTER_POINT;
    samp.min_lod = 0;
    samp.max_lod = (levels - 1) << 6;           /* unsigned 4.6 */
    evergreen_set_tex_sampler(&Xi, &samp);
    Sh.tex[unit][0] = gpu;
    Sh.tex[unit][1] = size;
    Sh.tex[unit][2] = key;
    Sh.tex[unit][3] = stride;
}

static ULONG VbufShadow[4][3];

static void EmitVertexBufferId(ULONG id, ULONG gpu, ULONG bytes, ULONG stride)
{
    RADEONInfoPtr info = &Xi;

    BEGIN_BATCH(10);
    PACK0(SQ_FETCH_RESOURCE + (SQ_FETCH_RESOURCE_vs + id) * SQ_FETCH_RESOURCE_offset, 8);
    E32(gpu);
    E32(bytes - 1);
    E32((stride << SQ_VTX_CONSTANT_WORD2_0__STRIDE_shift) |
        (FMT_32_32_32_32_FLOAT << SQ_VTX_CONSTANT_WORD2_0__DATA_FORMAT_shift) |
        (SQ_ENDIAN_8IN32 << SQ_VTX_CONSTANT_WORD2_0__ENDIAN_SWAP_shift));
    E32((SQ_SEL_X << SQ_VTX_CONSTANT_WORD3_0__DST_SEL_X_shift) |
        (SQ_SEL_Y << SQ_VTX_CONSTANT_WORD3_0__DST_SEL_Y_shift) |
        (SQ_SEL_Z << SQ_VTX_CONSTANT_WORD3_0__DST_SEL_Z_shift) |
        (SQ_SEL_W << SQ_VTX_CONSTANT_WORD3_0__DST_SEL_W_shift));
    E32(0);
    E32(0);
    E32(0);
    E32(SQ_TEX_VTX_VALID_BUFFER << SQ_VTX_CONSTANT_WORD7_0__TYPE_shift);
    END_BATCH();
}

static void EmitVertexBuffer(ULONG gpu, ULONG bytes, ULONG stride)
{
    EmitVertexBufferId(0, gpu, bytes, stride);
    VbufShadow[0][0] = 0;       /* written without the shadow */
}

/* a vertex buffer resource unless it is the one already set in this
 * buffer (the shadow is cleared at every buffer start) */

static void VertexBufferOnce(ULONG id, ULONG gpu, ULONG bytes, ULONG stride)
{
    if (VbufShadow[id][0] == gpu && VbufShadow[id][1] == bytes && VbufShadow[id][2] == stride)
        return;
    EmitVertexBufferId(id, gpu, bytes, stride);
    VbufShadow[id][0] = gpu;
    VbufShadow[id][1] = bytes;
    VbufShadow[id][2] = stride;
}

/* VGT_PRIMITIVE_TYPE, INDEX_TYPE: as last set in this buffer (~0 unknown) */
static ULONG PrimShadow, IndexTypeShadow;

/* VGT_INDX_OFFSET: added to every index, auto-generated ones too */
static ULONG IndexOffset;

static void SetIndexOffset(ULONG offset)
{
    if (IndexOffset != offset) {
        Reg(VGT_INDX_OFFSET, offset);
        IndexOffset = offset;
    }
}

/* SQ_VTX_BASE_VTX_LOC: added to the index of every VERTEX_DATA fetch (the
 * shared vertex buffer window, ArraysTail). 0 at every buffer's start and
 * end: the copying present and the 2D driver fetch with it 0. ~0: unknown. */
static ULONG BaseVtx;

static void SetBaseVtx(ULONG base)
{
    if (BaseVtx != base) {
        Reg(SQ_VTX_BASE_VTX_LOC, base);
        BaseVtx = base;
    }
}

/* VGT_MULTI_PRIM_IB_RESET_EN (index 0xffff): off at every buffer's start
 * and end, as the present and the 2D driver expect it */
static ULONG RestartShadow;

static void SetRestart(ULONG on)
{
    if (RestartShadow != on) {
        if (on)
            Reg(VGT_MULTI_PRIM_IB_RESET_INDX, 0xffffUL);
        Reg(VGT_MULTI_PRIM_IB_RESET_EN, on);
        RestartShadow = on;
    }
}

/* count big-endian 16- or 32-bit indices at gpu */
static void EmitDrawIndexed(ULONG prim, ULONG count, ULONG gpu, BOOL index32)
{
    RADEONInfoPtr info = &Xi;

    ULONG it = index32 ? (IT_INDEX_TYPE_SWAP_MODE(VGT_DMA_SWAP_32_BIT) | DI_INDEX_SIZE_32_BIT)
                       : (IT_INDEX_TYPE_SWAP_MODE(VGT_DMA_SWAP_16_BIT) | DI_INDEX_SIZE_16_BIT);

    BEGIN_BATCH(14);
    if (PrimShadow != prim) {
        EREG(VGT_PRIMITIVE_TYPE, prim);
        PrimShadow = prim;
    }
    if (IndexTypeShadow != it) {
        PACK3(IT_INDEX_TYPE, 1);
        E32(it);
        /* NUM_INSTANCES stays 1: every draw sets it so */
        PACK3(IT_NUM_INSTANCES, 1);
        E32(1);
        IndexTypeShadow = it;
    }
    PACK3(IT_DRAW_INDEX, 4);
    E32(gpu);
    E32(0);
    E32(count);
    E32(DI_SRC_SEL_DMA);
    END_BATCH();
}

static void EmitDrawOffset(ULONG prim, ULONG count, ULONG offset)
{
    draw_config_t d;

    SetIndexOffset(offset);
    CLEAR(d);
    d.prim_type = prim;
    d.vgt_draw_initiator = DI_SRC_SEL_AUTO_INDEX;
    d.num_instances = 1;
    d.num_indices = count;
    d.index_type = DI_INDEX_SIZE_16_BIT;
    evergreen_draw_auto(&Xi, &d);
    PrimShadow = prim;
    IndexTypeShadow = DI_INDEX_SIZE_16_BIT;
}

static void EmitDraw(ULONG prim, ULONG count)
{
    EmitDrawOffset(prim, count, 0);
}

static void EmitScissor(ULONG l, ULONG t, ULONG r, ULONG b)
{
    if (Sh.valid && Sh.scissor[0] == l && Sh.scissor[1] == t &&
        Sh.scissor[2] == r && Sh.scissor[3] == b)
        return;
    if (r <= l || b <= t)
        l = t = r = b = 0;
    evergreen_set_generic_scissor(&Xi, (int)l, (int)t, (int)r, (int)b);
    Sh.scissor[0] = l; Sh.scissor[1] = t; Sh.scissor[2] = r; Sh.scissor[3] = b;
}

static void EmitRaster(ULONG suMode, ULONG clipCntl, ULONG vte, const ULONG *vport,
                       ULONG pointSize)
{
    RADEONInfoPtr info = &Xi;
    ULONG i;

    if (!Sh.valid || Sh.suMode != suMode || Sh.clipCntl != clipCntl || Sh.vteCntl != vte) {
        BEGIN_BATCH(9);
        EREG(PA_SU_SC_MODE_CNTL, suMode);
        EREG(PA_CL_CLIP_CNTL, clipCntl);
        EREG(PA_CL_VTE_CNTL, vte);
        END_BATCH();
        Sh.suMode = suMode; Sh.clipCntl = clipCntl; Sh.vteCntl = vte;
    }
    if (vport && (!Sh.valid || !SameLongs(Sh.vport, vport, sizeof(Sh.vport) / 4))) {
        BEGIN_BATCH(8);
        PACK0(PA_CL_VPORT_XSCALE_0, 6);
        for (i = 0; i < 6; ++i)
            E32(vport[i]);
        END_BATCH();
        memcpy(Sh.vport, vport, sizeof(Sh.vport));
    }
    if (!Sh.valid || Sh.pointSize != pointSize) {
        BEGIN_BATCH(6);
        /* 12.4 fixed point, half the size */
        EREG(PA_SU_POINT_SIZE, ((pointSize / 2) << PA_SU_POINT_SIZE__WIDTH_shift) | (pointSize / 2));
        EREG(PA_SU_POINT_MINMAX, 0xffffUL << PA_SU_POINT_MINMAX__MAX_SIZE_shift);
        END_BATCH();
        Sh.pointSize = pointSize;
    }
}

static ULONG FloatOf(ULONG n, ULONG d)
{
    /* n/d as IEEE single bits without the FPU in the emitter's hot path */
    float f = (float)n / (float)d;
    ULONG b;
    memcpy(&b, &f, 4);
    return b;
}

static const UBYTE DepthFunc[8] = {
    FRAG_LESS, FRAG_LEQUAL, FRAG_EQUAL, FRAG_GEQUAL,
    FRAG_GREATER, FRAG_NOTEQUAL, FRAG_NEVER, FRAG_ALWAYS
};

static const UBYTE BlendFactor[11] = {
    BLEND_ZERO, BLEND_ONE, BLEND_SRC_COLOR, BLEND_ONE_MINUS_SRC_COLOR,
    BLEND_DST_COLOR, BLEND_ONE_MINUS_DST_COLOR, BLEND_SRC_ALPHA,
    BLEND_ONE_MINUS_SRC_ALPHA, BLEND_DST_ALPHA, BLEND_ONE_MINUS_DST_ALPHA,
    BLEND_SRC_ALPHA_SATURATE
};

static const UBYTE PrimType[10] = {
    0, 0, DI_PT_TRILIST, DI_PT_TRISTRIP, DI_PT_TRIFAN, DI_PT_QUADLIST,
    DI_PT_POINTLIST, DI_PT_LINELIST, DI_PT_LINESTRIP, DI_PT_LINELOOP
};

/* Fragment state shared by draws and clears. */
static void EmitFragment(ULONG cbMask, ULONG blend, ULONG alphaCtl, ULONG alphaRef)
{
    RADEONInfoPtr info = &Xi;
    ULONG dbShader = (EARLY_Z_THEN_LATE_Z << Z_ORDER_shift) | DUAL_EXPORT_ENABLE_bit;

    if (alphaCtl)
        dbShader = (LATE_Z << Z_ORDER_shift) | DUAL_EXPORT_ENABLE_bit;
    if (!Sh.valid || Sh.cbMask != cbMask) {
        Reg(CB_TARGET_MASK, cbMask);
        Sh.cbMask = cbMask;
    }
    if (!Sh.valid || Sh.blend != blend) {
        Reg(CB_BLEND0_CONTROL, blend);
        Sh.blend = blend;
    }
    if (!Sh.valid || Sh.alphaCtl != alphaCtl || Sh.alphaRef != alphaRef) {
        BEGIN_BATCH(6);
        EREG(SX_ALPHA_TEST_CONTROL, alphaCtl);
        EREG(SX_ALPHA_REF, alphaRef);
        END_BATCH();
        Sh.alphaCtl = alphaCtl;
        Sh.alphaRef = alphaRef;
    }
    if (!Sh.valid || Sh.dbShader != dbShader) {
        Reg(DB_SHADER_CONTROL, dbShader);
        Sh.dbShader = dbShader;
    }
}

/* longword compare (the inlined memcmp goes byte by byte) */
static __inline__ BOOL SameLongs(const void *a, const void *b, ULONG longs)
{
    const ULONG *x = (const ULONG *)a, *y = (const ULONG *)b;

    while (longs--)
        if (*x++ != *y++)
            return FALSE;
    return TRUE;
}

/* Handle to surface, remembered for one batch (EgEmitBatch clears it):
 * every draw resolves its target, depth and textures. */
#define RC_SLOTS 64
static ULONG RcHandle[RC_SLOTS];
static struct MGLPPCEmitSurface RcSurf[RC_SLOTS];

static BOOL Resolve(MGLPPCEmitResolve resolve, void *user, ULONG handle,
                    struct MGLPPCEmitSurface *out)
{
    ULONG i = ((handle * 0x9e3779b1UL) >> 26) & (RC_SLOTS - 1UL);

    if (handle && RcHandle[i] == handle) {
        *out = RcSurf[i];
        return TRUE;
    }
    if (!resolve(user, handle, out))
        return FALSE;
    RcHandle[i] = handle;
    RcSurf[i] = *out;
    return TRUE;
}

static BOOL SetTargets(ULONG colorHandle, ULONG depthHandle, ULONG depthControl,
                       MGLPPCEmitResolve resolve, void *user,
                       struct MGLPPCEmitSurface *colorOut)
{
    struct MGLPPCEmitSurface c, d;
    struct Target t;
    struct Depth z;

    if (!colorHandle || !Resolve(resolve, user, colorHandle, &c) || !c.pitch) {
        EgEmitFailStage = 0x2001;
        return FALSE;
    }
    if (colorOut)
        *colorOut = c;
    t.gpu = c.gpuAddress;
    t.pitch = c.pitch;
    t.height = c.height;
    t.format = c.format & 0xffUL;
    if (!Sh.valid || !SameLongs(&Sh.target, &t, sizeof(t) / 4)) {
        EmitTarget(&t);
        Sh.target = t;
    }
    memset(&z, 0, sizeof(z));
    if (depthHandle) {
        if (!Resolve(resolve, user, depthHandle, &d)) {
            EgEmitFailStage = 0x2002;
            return FALSE;
        }
        z.gpu = d.gpuAddress;
        z.pitch = d.pitch;
        z.height = d.height;
        z.control = depthControl;
    }
    if (!Sh.valid || !SameLongs(&Sh.depth, &z, 3)) {
        EmitDepth(&z);
        Sh.depth = z;
    } else if (Sh.depth.control != z.control) {
        /* the same surface: the control alone */
        if (z.gpu)
            Reg(DB_DEPTH_CONTROL, z.control);
        Sh.depth.control = z.control;
    }
    return TRUE;
}

static const ULONG Identity[16] = {
    0x3f800000UL, 0, 0, 0,  0, 0x3f800000UL, 0, 0,
    0, 0, 0x3f800000UL, 0,  0, 0, 0, 0x3f800000UL
};

static BOOL BindVsConst(const ULONG *rows)
{
    RADEONInfoPtr info = &Xi;
    ULONG gpu;

    if (Sh.valid && Sh.vsConstGpu && SameLongs(Sh.vsConst, rows, 16))
        return TRUE;
    gpu = ConstUpload(rows, 16);
    if (!gpu)
        return FALSE;
    BEGIN_BATCH(6);
    EREG(SQ_ALU_CONST_BUFFER_SIZE_VS_0, 1);
    EREG(SQ_ALU_CONST_CACHE_VS_0, gpu >> 8);
    END_BATCH();
    memcpy(Sh.vsConst, rows, 64);
    Sh.vsConstGpu = gpu;
    return TRUE;
}

static BOOL BindPsConst(const ULONG *c)
{
    RADEONInfoPtr info = &Xi;
    ULONG gpu;

    if (Sh.valid && Sh.psConstGpu && SameLongs(Sh.psConst, c, 4))
        return TRUE;
    gpu = ConstUpload(c, 4);
    if (!gpu)
        return FALSE;
    BEGIN_BATCH(6);
    EREG(SQ_ALU_CONST_BUFFER_SIZE_PS_0, 1);
    EREG(SQ_ALU_CONST_CACHE_PS_0, gpu >> 8);
    END_BATCH();
    memcpy(Sh.psConst, c, 16);
    Sh.psConstGpu = gpu;
    return TRUE;
}

#if R200_PERF_TIMING
ULONG PerfClock(void);
extern ULONG EgT[16], EgN[16];
#define EGE_BEGIN() ULONG ege0_ = PerfClock()
#define EGE_END(k) do { EgT[k] += PerfClock() - ege0_; EgN[k]++; } while (0)
#else
#define EGE_BEGIN() do { } while (0)
#define EGE_END(k) do { } while (0)
#endif

/* Vertices into the stream; returns the GPU address or 0. */
static ULONG UploadVertices(const ULONG *v, ULONG dwords)
{
    ULONG off = StreamAlloc(dwords * 4, 256);
    EGE_BEGIN();

    if (off == ~0UL)
        return 0;
    ToVram(off, v, dwords);
    EGE_END(11);
    return VramGpu + off;
}

static BOOL Clear(const ULONG *r, MGLPPCEmitResolve resolve, void *user)
{
    struct MGLPPCEmitSurface c;
    ULONG mask = r[4], depthHandle = (mask & RADEON3D_CLEAR_DEPTH) ? r[3] : 0;
    ULONG v[3 * 6], gpu, i, x[3], y[3];
    struct VsKey k;
    struct Shader *vs, *ps;
    ULONG psc[4] = {0, 0, 0, 0};

    ++EgStatClears;
    PrevOk = FALSE;
    if (!SetTargets(r[2], depthHandle,
                    depthHandle ? (Z_ENABLE_bit | Z_WRITE_ENABLE_bit | (FRAG_ALWAYS << ZFUNC_shift)) : 0,
                    resolve, user, &c))
        return FALSE;
    EmitFragment((mask & RADEON3D_CLEAR_COLOR) ? 0xfUL : 0, 0, 0, 0);
    EmitScissor(r[7], r[8], r[9], r[10]);
    EmitRaster(0, CLIP_DISABLE_bit, VTX_XY_FMT_bit | VTX_Z_FMT_bit, NULL, 16);
    /* a RECTLIST over the rectangle in the basic screen layout:
     * x, y, z, s, t, colour */
    x[0] = r[7]; y[0] = r[8];
    x[1] = r[9]; y[1] = r[8];
    x[2] = r[7]; y[2] = r[10];
    for (i = 0; i < 3; ++i) {
        v[i * 6 + 0] = FloatOf(x[i], 1);
        v[i * 6 + 1] = FloatOf(y[i], 1);
        v[i * 6 + 2] = r[6];
        v[i * 6 + 3] = 0;
        v[i * 6 + 4] = 0;
        v[i * 6 + 5] = r[5];
    }
    memset(&k, 0, sizeof(k));
    k.flags = 0; k.posOff = 0; k.colOff = 20; k.stride = 24;
    vs = GetShader(k.flags, ((ULONG)k.posOff << 24) | ((ULONG)k.wOff << 16) |
                   ((ULONG)k.colOff << 8) | k.tex0Off,
                   ((ULONG)k.tex1Off << 24) | ((ULONG)k.fogOff << 16) | ((ULONG)k.stride << 8), FALSE);
    ps = GetShader(0, 0, 0, TRUE);
    if (!vs || !ps) {
        EgEmitFailStage = 0x2003;
        return FALSE;
    }
    BindShaders(vs, ps, 1, FALSE);
    if (!BindVsConst(Identity) || !BindPsConst(psc))
        return FALSE;
    gpu = UploadVertices(v, 18);
    if (!gpu)
        return FALSE;
    EmitVertexBuffer(gpu, 72, 24);
    EmitDraw(DI_PT_RECTLIST, 3);
    Sh.valid = TRUE;
    return TRUE;
}

/* An EG_EXEC_ARRAYS draw's vertex buffers and the draw itself. */
static void ArraysTail(const ULONG *ga, ULONG count, BOOL textured, BOOL textured1)
{
        ULONG n = ga[EGA_VERTS], fmt = ga[EGA_FORMATS], col = fmt >> EGA_COL_SHIFT;
    ULONG posBytes = (fmt & EGA_POS_SIZE_MASK) * 4;
    ULONG colBytes = (col & 3) == EGA_COL_FLOAT4 ? 16 : (col & 3) == EGA_COL_FLOAT3 ? 12 : 4;
    ULONG window = 0;
#define EGA_BYTES(st, elem) ((st) ? (st) * n : (elem))
    if (fmt & EGA_INTERLEAVED) {
        /* one buffer, the attributes at their offsets. Its base: the
         * address congruent to the data's modulo the stride at the start of
         * its 16 MB block, the data's first record selected by
         * VGT_INDX_OFFSET -- draws of one stride share the resource. */
        ULONG st = ga[EGA_POS_STRIDE], gpu = ga[EGA_POS_GPU];
        /* indexed draws only: with auto-generated indices the base did
         * not take (MEASURED: raster.point-size, the first point drawn 4
         * pixels wide) */
        if (st >= 4UL && !(st & 3UL) && ga[EGA_INDEX_TYPE] != EGA_INDEX_NONE) {
            ULONG b0 = gpu & ~0xffffffUL;
            window = (gpu - b0) / st;
            VertexBufferOnce(0, gpu - window * st, 0x2000000UL, st);
        } else
            VertexBufferOnce(0, gpu, st * n, st);
        if (!(fmt & EGA_COL_ARRAY))
            VertexBufferOnce(1, ga[EGA_COL_GPU], colBytes, 0);
    } else {
        VertexBufferOnce(0, ga[EGA_POS_GPU], EGA_BYTES(ga[EGA_POS_STRIDE], posBytes),
                         ga[EGA_POS_STRIDE]);
        VertexBufferOnce(1, ga[EGA_COL_GPU], EGA_BYTES(ga[EGA_COL_STRIDE], colBytes),
                         ga[EGA_COL_STRIDE]);
        if (textured)
            VertexBufferOnce(2, ga[EGA_TEX0_GPU], EGA_BYTES(ga[EGA_TEX0_STRIDE], 8),
                             ga[EGA_TEX0_STRIDE]);
        if (textured1)
            VertexBufferOnce(3, ga[EGA_TEX1_GPU], EGA_BYTES(ga[EGA_TEX1_STRIDE], 8),
                             ga[EGA_TEX1_STRIDE]);
    }
#undef EGA_BYTES
    ++EgStatDraws;
    /* the indices' rebase too: VGT_INDX_OFFSET does not reach the vertex
     * fetch (MEASURED: a windowed draw through it fetched nothing) */
    if (ga[EGA_INDEX_TYPE] == EGA_INDEX_NONE) {
        SetBaseVtx(window);
        EmitDraw(PrimType[ga[EGA_PRIM] % 10], count);
    } else {
        SetBaseVtx(window + ga[EGA_INDEX_OFFSET]);
        SetIndexOffset(0);
        SetRestart((fmt & EGA_RESTART) ? 1UL : 0UL);
        EmitDrawIndexed(PrimType[ga[EGA_PRIM] % 10], count, ga[EGA_INDEX_GPU],
                        ga[EGA_INDEX_TYPE] == EGA_INDEX_32);
    }
}

/* The last array draw's header (dwords 2..43) and descriptors, to skip
 * every state group but the textures when only they changed (consecutive
 * world chunks). Cleared at every buffer start and by any other record. */
/* header dwords a texture-only change touches (bindings, sizes, offsets,
 * count): compared apart */
static const UBYTE TexWord[44] = {
    0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
#define PS_TEX_BITS (RADEON3D_TEX_MODULATE | RADEON3D_TEX_SCALE_2X | RADEON3D_TEX_ALPHA_SCALE_2X)

static BOOL Draw(const ULONG *r, ULONG length, ULONG primitive,
                 MGLPPCEmitResolve resolve, void *user, const ULONG *ga)
{
    struct MGLPPCEmitSurface c, t0s, t1s;
    ULONG options = r[5], headerDwords, stride, count = r[10], depthControl = 0;
    BOOL fragment = (options & RADEON3D_DRAW_FRAGMENT_STATE) != 0;
    BOOL extended = (options & RADEON3D_DRAW_EXTENDED_VERTEX) != 0;
    BOOL tcl = (options & RADEON3D_DRAW_HW_TCL) != 0;
    BOOL normals = (options & (RADEON3D_DRAW_NORMALS | RADEON3D_DRAW_LIGHTING)) != 0;
    BOOL compact = (options & RADEON3D_DRAW_COMPACT_VERTEX) != 0;
    BOOL textured = (options & RADEON3D_DRAW_TEXTURED) != 0;
    ULONG vertexState = extended ? r[19] : 0, fragmentState = fragment ? r[14] : 0;
    BOOL textured1 = extended && (vertexState & RADEON3D_VERTEX_TEXTURE1);
    BOOL fog = extended && (vertexState & RADEON3D_VERTEX_FOG);
    BOOL clipCoords = extended && (vertexState & RADEON3D_VERTEX_CLIP_COORDINATES);
    ULONG transform = tcl ? r[43] : 0, suMode, clipCntl, vte, vport[6], rows[16];
    ULONG psKey = 0, blend = 0, alphaCtl = 0, alphaRef = 0, gpu, i, inputs = 1;
    struct VsKey k;
    struct Shader *vs, *ps;
    const ULONG *vertices;
    ULONG psc[4];

    if (options & (RADEON3D_DRAW_TEXGEN | RADEON3D_DRAW_LIGHTING | RADEON3D_DRAW_PALETTE)) {
        EgEmitFailStage = 0x2101;   /* not yet: lighting, texgen, palette */
        return FALSE;
    }
    if (ga && PrevOk && Sh.valid && length >= RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS + EG_ARRAYS_DWORDS) {
        /* the previous array draw's state but for the textures */
        const ULONG *g = r + RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS;
        ULONG i;
        BOOL same = g[EGA_FORMATS] == PrevFmt && g[EGA_OFFSETS] == PrevOffs &&
                    !((r[13] ^ PrevHdr[13]) & (PS_TEX_BITS | EG_TEX_FORMAT_MASK)) &&
                    !((r[18] ^ PrevHdr[18]) & (PS_TEX_BITS | EG_TEX_FORMAT_MASK));
        if (!DrawTexOnly)
            for (i = 2; same && i < 44UL; ++i)
                if (!TexWord[i] && r[i] != PrevHdr[i])
                    same = FALSE;
        if (same) {
            if (textured) {
                if (!r[4] || !Resolve(resolve, user, r[4], &t0s)) {
                    EgEmitFailStage = 0x2104;
                    return FALSE;
                }
                EmitTexture(0, &t0s, r[11], r[12], r[13]);
            }
            if (textured1) {
                if (!r[15] || !Resolve(resolve, user, r[15], &t1s)) {
                    EgEmitFailStage = 0x2105;
                    return FALSE;
                }
                EmitTexture(1, &t1s, r[16], r[17], r[18]);
            }
            ArraysTail(g, g[EGA_COUNT], textured, textured1);
            PrevHdr = r;
            EgFastDraws++;
            return TRUE;
        }
    }
    headerDwords = tcl ? RADEON3D_EXEC_DRAW_HW_TCL_HEADER_DWORDS
                 : extended ? RADEON3D_EXEC_DRAW_EXTENDED_HEADER_DWORDS
                 : fragment ? RADEON3D_EXEC_DRAW_FRAGMENT_HEADER_DWORDS
                 : RADEON3D_EXEC_DRAW_HEADER_DWORDS;
    if (tcl && normals)
        headerDwords += RADEON3D_EXEC_NORMAL_MATRICES_DWORDS;
    /* the vertex layout (radeon3d_records.h, r200_geometry.c) */
    memset(&k, 0, sizeof(k));
    if (tcl) {
        if (normals) {
            stride = compact ? 10 : 13;
            k.flags = VS_POS_W4; k.posOff = 0; k.colOff = 28;
            if (textured) { k.flags |= VS_TEX0; k.tex0Off = 32; }
            if (!compact && textured1) { k.flags |= VS_TEX1; k.tex1Off = 40; }
            if (!compact && fog) { k.flags |= VS_FOG; k.fogOff = 48; }
        } else if (compact && (options & RADEON3D_DRAW_NO_W)) {
            ULONG o = (options & RADEON3D_DRAW_NO_Z) ? 8 : 12;
            if (options & RADEON3D_DRAW_NO_Z) {
                EgEmitFailStage = 0x2102;
                return FALSE;
            }
            stride = (o + 4 + (textured ? 8 : 0)) / 4;
            k.flags = 0; k.posOff = 0; k.colOff = (UBYTE)o;
            if (textured) { k.flags |= VS_TEX0; k.tex0Off = (UBYTE)(o + 4); }
        } else if (compact) {
            stride = textured1 ? 9 : 7;
            k.flags = VS_POS_W4; k.posOff = 0; k.colOff = 16;
            if (textured) { k.flags |= VS_TEX0; k.tex0Off = 20; }
            if (textured1) { k.flags |= VS_TEX1; k.tex1Off = 28; }
        } else {
            stride = RADEON3D_EXEC_HW_TCL_VERTEX_DWORDS;
            k.flags = VS_POS_W4; k.posOff = 0; k.colOff = 16;
            if (textured) { k.flags |= VS_TEX0; k.tex0Off = 20; }
            if (textured1) { k.flags |= VS_TEX1; k.tex1Off = 28; }
            if (fog) { k.flags |= VS_FOG; k.fogOff = 36; }
        }
    } else {
        stride = extended ? RADEON3D_EXEC_EXTENDED_VERTEX_DWORDS : RADEON3D_EXEC_VERTEX_DWORDS;
        k.flags = 0; k.posOff = 0; k.colOff = 20;
        if (textured) { k.flags |= VS_TEX0; k.tex0Off = 12; }
        if (textured1) { k.flags |= VS_TEX1; k.tex1Off = 24; }
        if (fog) { k.flags |= VS_FOG; k.fogOff = 32; }
        if (clipCoords) { k.flags |= VS_W_SEPARATE; k.wOff = 32; }
    }
    k.stride = (UBYTE)(stride * 4);
    if (ga) {
        /* the client's arrays: the descriptors follow the header */
        if (!tcl || normals || fog || length < headerDwords + EG_ARRAYS_DWORDS) {
            EgEmitFailStage = 0x2107;
            return FALSE;
        }
        ga = r + headerDwords;
        count = ga[EGA_COUNT];
        memset(&k, 0, sizeof(k));
        k.flags = VS_STREAMS | (textured ? VS_TEX0 : 0) | (textured1 ? VS_TEX1 : 0);
        k.posOff = (UBYTE)ga[EGA_FORMATS];
        if (ga[EGA_FORMATS] & EGA_INTERLEAVED) {
            k.wOff = (UBYTE)(1 | ((ga[EGA_FORMATS] & EGA_COL_ARRAY) ? 2 : 0));
            k.colOff = (UBYTE)ga[EGA_OFFSETS];
            k.tex0Off = textured ? (UBYTE)(ga[EGA_OFFSETS] >> 8) : 0;
            k.tex1Off = textured1 ? (UBYTE)(ga[EGA_OFFSETS] >> 16) : 0;
        }
        stride = 0;
    } else if (length < headerDwords + count * stride) {
        EgEmitFailStage = 0x2103;
        return FALSE;
    }
    if (count == 0)
        return TRUE;
    vertices = r + headerDwords;
    /* depth */
    if (r[3]) {
        depthControl = Z_ENABLE_bit | (DepthFunc[(options & RADEON3D_DRAW_DEPTH_FUNC_MASK) >>
                                                 RADEON3D_DRAW_DEPTH_FUNC_SHIFT] << ZFUNC_shift);
        if (options & RADEON3D_DRAW_DEPTH_WRITE)
            depthControl |= Z_WRITE_ENABLE_bit;
    }
    if (!SetTargets(r[2], r[3], depthControl, resolve, user, &c))
        return FALSE;
    /* fragment state */
    if (fragment && (fragmentState & RADEON3D_FRAGMENT_ALPHA_TEST)) {
        ULONG f = (fragmentState & RADEON3D_FRAGMENT_ALPHA_FUNC_MASK) >> RADEON3D_FRAGMENT_ALPHA_FUNC_SHIFT;
        alphaCtl = ALPHA_TEST_ENABLE_bit | ((ULONG)DepthFunc[f] << ALPHA_FUNC_shift);
        alphaRef = FloatOf((fragmentState & RADEON3D_FRAGMENT_ALPHA_REF_MASK) >>
                           RADEON3D_FRAGMENT_ALPHA_REF_SHIFT, 255);
    }
    if (fragment && (fragmentState & RADEON3D_FRAGMENT_BLEND)) {
        ULONG sf = BlendFactor[((fragmentState & RADEON3D_FRAGMENT_SRC_MASK) >> RADEON3D_FRAGMENT_SRC_SHIFT) % 11];
        ULONG df = BlendFactor[((fragmentState & RADEON3D_FRAGMENT_DST_MASK) >> RADEON3D_FRAGMENT_DST_SHIFT) % 11];
        ULONG fn = (fragmentState & RADEON3D_FRAGMENT_BLEND_SUB) ? COMB_SRC_MINUS_DST
                 : (fragmentState & RADEON3D_FRAGMENT_BLEND_RSUB) ? COMB_DST_MINUS_SRC
                 : COMB_DST_PLUS_SRC;
        blend = CB_BLEND0_CONTROL__ENABLE_bit |
                (sf << COLOR_SRCBLEND_shift) | (fn << COLOR_COMB_FCN_shift) | (df << COLOR_DESTBLEND_shift) |
                (sf << ALPHA_SRCBLEND_shift) | (fn << ALPHA_COMB_FCN_shift) | (df << ALPHA_DESTBLEND_shift);
    } else if (!fragment && (options & RADEON3D_DRAW_ALPHA_BLEND)) {
        blend = CB_BLEND0_CONTROL__ENABLE_bit |
                (BLEND_SRC_ALPHA << COLOR_SRCBLEND_shift) | (BLEND_ONE_MINUS_SRC_ALPHA << COLOR_DESTBLEND_shift) |
                (BLEND_SRC_ALPHA << ALPHA_SRCBLEND_shift) | (BLEND_ONE_MINUS_SRC_ALPHA << ALPHA_DESTBLEND_shift);
    }
    EmitFragment((options & RADEON3D_EXEC_SUPPRESS_COLOR_WRITE) ? 0 : 0xfUL, blend, alphaCtl, alphaRef);
    EmitScissor(r[6], r[7], r[8], r[9]);
    /* textures */
    if (textured) {
        ULONG state = fragment ? r[13] : (RADEON3D_TEX_MODULATE |
                      ((options & RADEON3D_DRAW_BILINEAR) ? (RADEON3D_TEX_MAG_LINEAR |
                       (RADEON3D_TEX_MIN_LINEAR << RADEON3D_TEX_MIN_SHIFT)) : 0));
        if (!r[4] || !Resolve(resolve, user, r[4], &t0s)) {
            EgEmitFailStage = 0x2104;
            return FALSE;
        }
        EmitTexture(0, &t0s, fragment ? r[11] : 0, fragment ? r[12] : 0, state);
        psKey |= PS_TEX0;
        if (!(state & RADEON3D_TEX_MODULATE)) psKey |= PS_TEX0_REPLACE;
        if (state & RADEON3D_TEX_SCALE_2X) psKey |= PS_RGB2X;
        if (state & RADEON3D_TEX_ALPHA_SCALE_2X) psKey |= PS_ALPHA2X;
        ++inputs;
    }
    if (textured1) {
        ULONG state = r[18];
        if (!r[15] || !Resolve(resolve, user, r[15], &t1s)) {
            EgEmitFailStage = 0x2105;
            return FALSE;
        }
        EmitTexture(1, &t1s, r[16], r[17], state);
        psKey |= PS_TEX1;
        if (!(state & RADEON3D_TEX_MODULATE)) psKey |= PS_TEX1_REPLACE;
        if (state & RADEON3D_TEX_SCALE_2X) psKey |= PS_RGB2X;
        if (state & RADEON3D_TEX_ALPHA_SCALE_2X) psKey |= PS_ALPHA2X;
        ++inputs;
    }
    if (fog) {
        psKey |= PS_FOG;
        ++inputs;
    }
    if (transform & RADEON3D_TRANSFORM_FLAT_SHADE)
        psKey |= PS_FLAT;
    /* raster and transform */
    suMode = PROVOKING_VTX_LAST_bit;
    if (tcl) {
        if (transform & RADEON3D_TRANSFORM_CULL_FRONT) suMode |= CULL_FRONT_bit;
        if (transform & RADEON3D_TRANSFORM_CULL_BACK) suMode |= CULL_BACK_bit;
        /* GL's front face with the viewport's Y flip (EgEmitFace) */
        if (((transform & RADEON3D_TRANSFORM_FRONT_CCW) != 0) != (EgEmitFace != 0))
            suMode |= FACE_bit;
        clipCntl = 0;
        vte = VPORT_X_SCALE_ENA_bit | VPORT_X_OFFSET_ENA_bit | VPORT_Y_SCALE_ENA_bit |
              VPORT_Y_OFFSET_ENA_bit | VPORT_Z_SCALE_ENA_bit | VPORT_Z_OFFSET_ENA_bit |
              VTX_W0_FMT_bit;
        for (i = 0; i < 6; ++i)
            vport[i] = r[37 + i];
        /* column-major MVP (dwords 21..36) as rows */
        for (i = 0; i < 4; ++i) {
            rows[i * 4 + 0] = r[21 + i];
            rows[i * 4 + 1] = r[25 + i];
            rows[i * 4 + 2] = r[29 + i];
            rows[i * 4 + 3] = r[33 + i];
        }
        EmitRaster(suMode, clipCntl, vte, vport,
                   (transform & RADEON3D_TRANSFORM_POINT_SIZE_MASK) >> RADEON3D_TRANSFORM_POINT_SIZE_SHIFT);
    } else if (clipCoords) {
        /* clip coordinates over the whole target (r200_geometry.c) */
        vport[0] = FloatOf(c.width, 2);  vport[1] = FloatOf(c.width, 2);
        vport[2] = FloatOf(c.height, 2) | 0x80000000UL; vport[3] = FloatOf(c.height, 2);
        vport[4] = FloatOf(1, 2); vport[5] = FloatOf(1, 2);
        memcpy(rows, Identity, sizeof(rows));
        vte = VPORT_X_SCALE_ENA_bit | VPORT_X_OFFSET_ENA_bit | VPORT_Y_SCALE_ENA_bit |
              VPORT_Y_OFFSET_ENA_bit | VPORT_Z_SCALE_ENA_bit | VPORT_Z_OFFSET_ENA_bit |
              VTX_W0_FMT_bit;
        EmitRaster(suMode, 0, vte, vport, 16);
    } else {
        memcpy(rows, Identity, sizeof(rows));
        EmitRaster(suMode, CLIP_DISABLE_bit, VTX_XY_FMT_bit | VTX_Z_FMT_bit, NULL, 16);
    }
    if (primitive == RADEON3D_EXEC_DRAW_POINTS && !tcl)
        ;   /* screen points are expanded by the frontend */
    /* shaders and constants */
    vs = GetShader(k.flags, ((ULONG)k.posOff << 24) | ((ULONG)k.wOff << 16) |
                   ((ULONG)k.colOff << 8) | k.tex0Off,
                   ((ULONG)k.tex1Off << 24) | ((ULONG)k.fogOff << 16) | ((ULONG)k.stride << 8), FALSE);
    ps = GetShader(psKey, 0, 0, TRUE);
    if (!vs || !ps) {
        EgEmitFailStage = 0x2106;
        return FALSE;
    }
    BindShaders(vs, ps, inputs, (psKey & PS_FLAT) != 0);
    if (!BindVsConst(rows))
        return FALSE;
    {
        ULONG fc = extended ? r[20] : 0;
        psc[0] = FloatOf((fc >> 16) & 255, 255);
        psc[1] = FloatOf((fc >> 8) & 255, 255);
        psc[2] = FloatOf(fc & 255, 255);
        psc[3] = 0x3f800000UL;
        if (!BindPsConst(psc))
            return FALSE;
    }
    if (ga) {
        EgFullDraws++;
        ArraysTail(ga, count, textured, textured1);
        PrevHdr = r;
        PrevFmt = ga[EGA_FORMATS];
        PrevOffs = ga[EGA_OFFSETS];
        PrevOk = TRUE;
        Sh.valid = TRUE;
        return TRUE;
    }
    PrevOk = FALSE;
    gpu = UploadVertices(vertices, count * stride);
    if (!gpu)
        return FALSE;
    EgStatOldPrim[(primitive % 10) + (tcl ? 0 : 10)]++;
    EgStatOldVerts[(primitive % 10) + (tcl ? 0 : 10)] += count;
    ++EgStatDraws;
    EgStatVtxDwords += count * stride;
    ++EgStatStride[stride > 16 ? 16 : stride];
    if (tcl) ++EgStatTcl;
    SetBaseVtx(0);
    EmitVertexBuffer(gpu, count * stride * 4, stride * 4);
    EmitDraw(PrimType[primitive], count);
    Sh.valid = TRUE;
    return TRUE;
}

/* --- one batch ----------------------------------------------------------- */

static void BeginBuffer(ULONG *words, ULONG capacity, BOOL persist);
static ULONG EndBuffer(ULONG *words);

/* The engine's state as the last buffer left it, if nothing else ran on the
 * engine since (EgEngineOurs, eg_device.c): a buffer then starts without
 * the default state and keeps the state shadows. EgEmitPersisted tells the
 * dispatch to verify that (else the batch is emitted again in full). */
ULONG EgEmitPersisted;
BOOL EgEmitForceFull;
BOOL EgEngineOurs(void);

ULONG EgEmitBatch(ULONG *words, ULONG capacity, ULONG slot,
                  const ULONG *records, ULONG recordDwords,
                  MGLPPCEmitResolve resolve, void *user)
{
    ULONG walk = 0;

    EgEmitFailStage = 0;
    memset(RcHandle, 0, sizeof(RcHandle));
    EgEmitPersisted = PersistOk && !EgEmitForceFull && EgEngineOurs();
    EgEmitForceFull = FALSE;
    PersistOk = FALSE;      /* until this buffer is complete */
    {
    EGE_BEGIN();
    BeginBuffer(words, capacity, EgEmitPersisted);
    EGE_END(14);
    }
    StreamSlot = slot % EG_EMIT_SLOTS;
    StreamPos = 0;
    while (walk + 2 <= recordDwords) {
        ULONG op = records[walk], length = records[walk + 1];
        BOOL ok;

        if (length < 2 || length > recordDwords - walk) {
            EgEmitFailStage = 0x2201;
            return 0;
        }
        EGE_BEGIN();
        if (op == RADEON3D_EXEC_CLEAR)
            ok = length == RADEON3D_EXEC_CLEAR_DWORDS && Clear(records + walk, resolve, user);
        else if (op >= RADEON3D_EXEC_DRAW_TRIANGLES && op <= RADEON3D_EXEC_DRAW_LINE_LOOP)
            ok = Draw(records + walk, length, op, resolve, user, NULL);
        else if (op == EG_EXEC_ARRAYS || op == EG_EXEC_ARRAYS_TEXONLY) {
            DrawTexOnly = op == EG_EXEC_ARRAYS_TEXONLY;
            ok = Draw(records + walk, length, RADEON3D_EXEC_DRAW_TRIANGLES, resolve, user,
                      records + walk);
            DrawTexOnly = FALSE;
        }
        else {
            EgEmitFailStage = 0x2202;   /* REUSE_* records: commit mode only */
            ok = FALSE;
        }
        EGE_END(op == EG_EXEC_ARRAYS || op == EG_EXEC_ARRAYS_TEXONLY ? 12 : 13);
        if (!ok) {
            if (!EgEmitFailStage)
                EgEmitFailStage = 0x2203;
            return 0;
        }
        if (EmitOverflow) {
            EgEmitFailStage = 0x2204;
            return 0;
        }
        walk += length;
    }
    SetBaseVtx(0);          /* as the present and the 2D driver expect it */
    SetRestart(0);
    {
        ULONG n = EndBuffer(words);
        PersistOk = n != 0;
        return n;
    }
}

/* A fresh buffer: the engine's default state, then a flush of everything
 * the CPU wrote before it (textures, vertices, constants, shaders). */
static void BeginBuffer(ULONG *words, ULONG capacity, BOOL persist)
{
    RADEONInfoPtr info = &Xi;

    CoalHdr = ~0UL;         /* no packet to extend in a new buffer */

    if (persist) {
        /* the engine holds the last buffer's state: keep the shadows, but
         * nothing that points into a stream or ring part (reused) */
        EmitOverflow = FALSE;
        EmitCp.Ring = (volatile ULONG *)words;
        EmitCp.Wptr = 0;
        EmitCapacity = capacity;
        Sh.vsConstGpu = 0;
        Sh.psConstGpu = 0;
        PrevOk = FALSE;
        memset(VbufShadow, 0, sizeof(VbufShadow));
        BEGIN_BATCH(5);
        PACK3(IT_SURFACE_SYNC, 4);
        E32(TC_ACTION_ENA_bit | SH_ACTION_ENA_bit);
        E32(0xffffffffUL);
        E32(0);
        E32(10);
        END_BATCH();
        return;
    }
    EmitOverflow = FALSE;
    memset(&EmitHw, 0, sizeof(EmitHw));
    memset(&EmitCp, 0, sizeof(EmitCp));
    memset(&Accel, 0, sizeof(Accel));
    memset(&Xi, 0, sizeof(Xi));
    memset(&Sh, 0, sizeof(Sh));
    EmitCp.Hw = &EmitHw;
    EmitCp.Ring = (volatile ULONG *)words;
    EmitCp.RingDw = 0x40000000UL;      /* no wrap: eg_cp_begin bounds it */
    EmitCapacity = capacity;
    Xi.cs = &EmitCp;
    Xi.accel_state = &Accel;
    Xi.ChipFamily = CHIP_FAMILY_CEDAR;
    Xi.num_banks = 4;

    /* The default state rewrites the SQ's GPR and thread resource
     * configuration, which may change only while no shader runs: the
     * previous buffer's draws can still be in flight (MEASURED: a GPU hang
     * once batches got heavy enough to overlap). Drain the 3D pipe first. */
    BEGIN_BATCH(8);
    PACK3(IT_EVENT_WRITE, 1);
    E32(PS_PARTIAL_FLUSH | (4UL << 8));
    PACK3(IT_EVENT_WRITE, 1);
    E32(VS_PARTIAL_FLUSH | (4UL << 8));
    EREG(WAIT_UNTIL, WAIT_3D_IDLE_bit | WAIT_3D_IDLECLEAN_bit);
    END_BATCH();
    evergreen_set_default_state(&Xi);
    BaseVtx = ~0UL;
    SetBaseVtx(0);
    RestartShadow = 0;      /* the default state */
    IndexOffset = 0;
    PrimShadow = IndexTypeShadow = ~0UL;
    PrevOk = FALSE;
    memset(SampShadow, 0, sizeof(SampShadow));
    memset(VbufShadow, 0, sizeof(VbufShadow));
    /* everything the CPU wrote before this buffer: textures, vertices,
     * constants, shaders */
    BEGIN_BATCH(5);
    PACK3(IT_SURFACE_SYNC, 4);
    E32(TC_ACTION_ENA_bit | SH_ACTION_ENA_bit);
    E32(0xffffffffUL);
    E32(0);
    E32(10);
    END_BATCH();
    evergreen_set_screen_scissor(&Xi, 0, 0, 8192, 8192);
    evergreen_set_window_scissor(&Xi, 0, 0, 8192, 8192);
}

static ULONG EndBuffer(ULONG *words)
{
    if (EmitOverflow) {
        EgEmitFailStage = 0x2204;
        return 0;
    }
    /* the CP wants an even-length buffer as the classic path padded it */
    if (EmitCp.Wptr & 1)
        words[EmitCp.Wptr++] = 0x80000000UL;
    return EmitCp.Wptr;
}

/* --- present: the rendered buffer onto the visible one ------------------- */

/* SURFACE_SYNC on every colour buffer: the CP waits until their writes
 * reached memory (and invalidates the texture cache behind them). */
static void SyncColorBuffers(void)
{
    RADEONInfoPtr info = &Xi;

    BEGIN_BATCH(5);
    PACK3(IT_SURFACE_SYNC, 4);
    E32(CB_ACTION_ENA_bit | (0xffUL << 6) | TC_ACTION_ENA_bit);
    E32(0xffffffffUL);
    E32(0);
    E32(10);
    END_BATCH();
}

/* A bit-exact copy with the 3D engine (no format conversion on either
 * side, as the 2D driver's copies: docs/04-endianness.md), optionally after
 * the CP waits for the CRTC's vertical blank. vertCpu/vertGpu: 18 dwords
 * of VRAM for the rectangle's vertices, owned by the caller. */
ULONG EgEmitPresent(ULONG *words, ULONG capacity, APTR vertCpu, ULONG vertGpu,
                    ULONG srcGpu, ULONG dstGpu, ULONG pitchBytes, ULONG format,
                    ULONG width, ULONG height, BOOL waitVblank)
{
    ULONG bpp = (format == RADEON3D_FORMAT_R5G6B5PC || format == EG_FORMAT_R5G6B5) ? 2 : 4;
    BOOL be = format == EG_FORMAT_R5G6B5 || format == EG_FORMAT_A8R8G8B8;
    RADEONInfoPtr info = &Xi;
    struct Target t;
    struct Depth z;
    struct VsKey k;
    struct Shader *vs, *ps;
    tex_resource_t tex;
    tex_sampler_t samp;
    ULONG v[18], i, x[3], y[3], s3[3], t3[3], psc[4] = {0, 0, 0, 0};
    volatile ULONG *dst = (volatile ULONG *)vertCpu;

    EgEmitFailStage = 0;
    BeginBuffer(words, capacity, FALSE);
    PersistOk = FALSE;      /* the copy leaves its own state */
    StreamSlot = 0;
    StreamPos = StreamSize;                 /* no stream: the constants below need none */
    /* the frame must be in memory before it is read as a texture: its
     * colour-buffer writes are flushed asynchronously at the previous
     * buffer's end, so wait for every colour buffer here (stale 8x8 tiles
     * otherwise) */
    SyncColorBuffers();
    memset(&t, 0, sizeof(t));
    t.gpu = dstGpu;
    t.pitch = pitchBytes;
    t.height = height;
    /* the same convention as the draws into this format (and as the 2D
     * driver's copies): the texture fetch undoes the colour buffer's swap */
    t.format = format;
    EmitTarget(&t);
    memset(&z, 0, sizeof(z));
    EmitDepth(&z);
    Sh.valid = FALSE;
    EmitFragment(0xfUL, 0, 0, 0);
    Sh.valid = TRUE;
    Sh.target = t;
    Sh.depth = z;
    EmitScissor(0, 0, width, height);
    Sh.valid = FALSE;
    EmitRaster(0, CLIP_DISABLE_bit, VTX_XY_FMT_bit | VTX_Z_FMT_bit, NULL, 16);
    Sh.valid = TRUE;
    CLEAR(tex);
    tex.id = 0;
    tex.w = pitchBytes / bpp;
    tex.h = height;
    tex.pitch = pitchBytes / bpp;
    tex.dim = SQ_TEX_DIM_2D;
    tex.base = srcGpu;
    tex.mip_base = srcGpu;
    tex.size = pitchBytes * height;
    tex.format = bpp == 2 ? FMT_5_6_5 : FMT_8_8_8_8;
    tex.num_format_all = SQ_NUM_FORMAT_NORM;
    tex.endian = !be ? SQ_ENDIAN_NONE : bpp == 2 ? SQ_ENDIAN_8IN16 : SQ_ENDIAN_8IN32;
    tex.dst_sel_x = SQ_SEL_Z;
    tex.dst_sel_y = SQ_SEL_Y;
    tex.dst_sel_z = SQ_SEL_X;
    tex.dst_sel_w = bpp == 2 ? SQ_SEL_1 : SQ_SEL_W;
    tex.array_mode = ARRAY_LINEAR_GENERAL;
    evergreen_set_tex_resource(&Xi, &tex, RADEON_GEM_DOMAIN_VRAM);
    CLEAR(samp);
    samp.id = 0;
    samp.clamp_x = samp.clamp_y = SQ_TEX_CLAMP_LAST_TEXEL;
    samp.clamp_z = SQ_TEX_WRAP;
    samp.xy_mag_filter = samp.xy_min_filter = SQ_TEX_XY_FILTER_POINT;
    evergreen_set_tex_sampler(&Xi, &samp);
    memset(&k, 0, sizeof(k));
    k.flags = VS_TEX0; k.posOff = 0; k.tex0Off = 12; k.colOff = 20; k.stride = 24;
    vs = GetShader(k.flags, ((ULONG)k.posOff << 24) | ((ULONG)k.wOff << 16) |
                   ((ULONG)k.colOff << 8) | k.tex0Off,
                   ((ULONG)k.tex1Off << 24) | ((ULONG)k.fogOff << 16) | ((ULONG)k.stride << 8), FALSE);
    ps = GetShader(PS_TEX0 | PS_TEX0_REPLACE, 0, 0, TRUE);
    if (!vs || !ps) {
        EgEmitFailStage = 0x2401;
        return 0;
    }
    Sh.valid = FALSE;
    BindShaders(vs, ps, 2, FALSE);
    Sh.valid = TRUE;
    {
        /* constants without the stream: the identity and zero live in the
         * shader cache's last 256 bytes, written once */
        ULONG off = ShaderOff + ShaderSize - 256;
        ToVramLe(off, Identity, 16);
        ToVramLe(off + 64, psc, 4);
        BEGIN_BATCH(12);
        EREG(SQ_ALU_CONST_BUFFER_SIZE_VS_0, 1);
        EREG(SQ_ALU_CONST_CACHE_VS_0, (VramGpu + off) >> 8);
        EREG(SQ_ALU_CONST_BUFFER_SIZE_PS_0, 1);
        EREG(SQ_ALU_CONST_CACHE_PS_0, (VramGpu + off + 64) >> 8);
        END_BATCH();
    }
    /* RECTLIST: top-left, top-right, bottom-left (pixels; s, t 0..1) */
    x[0] = 0; y[0] = 0; s3[0] = 0; t3[0] = 0;
    x[1] = width; y[1] = 0; s3[1] = 1; t3[1] = 0;
    x[2] = 0; y[2] = height; s3[2] = 0; t3[2] = 1;
    for (i = 0; i < 3; ++i) {
        v[i * 6 + 0] = FloatOf(x[i], 1);
        v[i * 6 + 1] = FloatOf(y[i], 1);
        v[i * 6 + 2] = 0;
        v[i * 6 + 3] = s3[i] ? FloatOf(width, pitchBytes / bpp) : 0;
        v[i * 6 + 4] = t3[i] ? 0x3f800000UL : 0;
        v[i * 6 + 5] = 0xffffffffUL;
    }
    for (i = 0; i < 18; ++i)
        dst[i] = v[i];
    EmitVertexBuffer(vertGpu, 72, 24);
    if (waitVblank) {
        /* WAIT_REG_MEM: CRTC0 status, V_BLANK set */
        BEGIN_BATCH(7);
        PACK3(0x3C, 6);                     /* WAIT_REG_MEM */
        E32(3);                             /* function equal, register space */
        E32(0x6e8cUL >> 2);
        E32(0);
        E32(1);                             /* reference */
        E32(1);                             /* mask */
        E32(10);                            /* poll interval */
        END_BATCH();
    }
    EmitDraw(DI_PT_RECTLIST, 3);
    /* and the copy must be done before the next frame draws into its
     * source */
    SyncColorBuffers();
    return EndBuffer(words);
}
