/* Host-independent production arithmetic checks, not native conversion/GPU ordering.
 * Build and run commands: tests/README-r200-readback.md. */
#define MGLPPC_TEXTURE_VALIDATION_TEST
#include "../backend_r200/include/libraries/minigl_ppc_protocol.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    /* Native enum values; production macro expands against each CPU's GL SDK. */
    enum { GL_ALPHA = 1, GL_RGB = 151, GL_RGBA = 155,
        GL_LUMINANCE = 100, GL_LUMINANCE_ALPHA = 103,
        GL_UNSIGNED_BYTE = 200, MGL_UBYTE_BGRA = 237, MGL_UBYTE_ARGB = 238,
        MGL_UNSIGNED_SHORT_5_6_5 = 239, MGL_UNSIGNED_SHORT_4_4_4_4 = 240 };
    ULONG row, stride, offset, bytes, seed = 1, i;
    assert(MGLPPC_CMD_TEXTURE_SUBIMAGE_SHARED == 26UL);
    assert(MGLPPC_CAP_TEXTURE_SUBIMAGE_V1 == (1UL << 9));
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(GL_RGB, GL_UNSIGNED_BYTE) == 3);
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(GL_RGBA, GL_UNSIGNED_BYTE) == 4);
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(GL_ALPHA, GL_UNSIGNED_BYTE) == 1);
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(GL_LUMINANCE, GL_UNSIGNED_BYTE) == 1);
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE) == 2);
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(MGL_UBYTE_BGRA, GL_UNSIGNED_BYTE) == 4);
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(MGL_UBYTE_ARGB, GL_UNSIGNED_BYTE) == 4);
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(GL_RGB, MGL_UNSIGNED_SHORT_5_6_5) == 2);
    assert(MGLPPC_TEXTURE_SOURCE_BYTES(GL_RGBA, MGL_UNSIGNED_SHORT_4_4_4_4) == 2);
    assert(!MGLPPC_TEXTURE_SOURCE_BYTES(GL_RGBA, MGL_UNSIGNED_SHORT_5_6_5));
    assert(!MGLPPC_TEXTURE_SOURCE_BYTES(GL_RGB, MGL_UNSIGNED_SHORT_4_4_4_4));
    assert(!MGLPPC_TEXTURE_SOURCE_BYTES(29, GL_UNSIGNED_BYTE)); /* color index */
    assert(!MGLPPC_TEXTURE_SOURCE_BYTES(GL_RGB, 0));
    assert(MGLPPCTextureSpan(3, 2, 3, 4, 5, 2, 1, 4096,
        &row, &stride, &offset, &bytes));
    assert(row == 9 && stride == 16 && offset == 35 && bytes == 18);
    assert(MGLPPCTextureSpan(3, 2, 4, 8, 1, 0, 0, 4096,
        &row, &stride, &offset, &bytes)); /* legal overlapping rows */
    assert(row == 12 && stride == 8 && bytes == 24);
    for (i = 1; i <= 8; i *= 2) {
        assert(MGLPPCTextureSpan(3, 2, 2, i, 0, 0, 0, 4096,
            &row, &stride, &offset, &bytes));
        assert(stride == ((6 + i - 1) & ~(i - 1)) && bytes == 12);
    }
    assert(MGLPPCTextureSpan(1, 1, 1, 1, 0, 0, 0, UINT32_MAX - 1,
        &row, &stride, &offset, &bytes));
    assert(!MGLPPCTextureSpan(1, 1, 1, 1, 0, 0, 0, UINT32_MAX,
        &row, &stride, &offset, &bytes));
    assert(!MGLPPCTextureSpan(1, 2, 4, 4, 0x3fffffff, 0, 0, 16,
        &row, &stride, &offset, &bytes)); /* last row + base overflow */
    assert(MGLPPCTextureSpan(1024, 512, 4, 1, 0, 0, 0, 4096,
        &row, &stride, &offset, &bytes));
    assert(bytes == MGLPPC_TEXTURE_SHARED_MAX_BYTES);
    assert(MGLPPCTextureRect(64, 32, 7, 127, 2, 15, 7, 1, 1));
    assert(MGLPPCTextureRect(64, 32, 7, 127, 6, 1, 1, 0, 0));
    assert(!MGLPPCTextureRect(64, 32, 7, 1, 2, 0, 0, 1, 1));
    assert(!MGLPPCTextureRect(64, 32, 7, 127, 7, 0, 0, 1, 1));
    assert(!MGLPPCTextureRect(64, 32, 7, 127, 0, 63, 0, 2, 1));
    assert(!MGLPPCTextureRect(64, 32, 7, 127, 0, 0xffffffffU, 0, 2, 1));
    assert(!MGLPPCTextureRect(64, 32, 7, 127, 0xffffffffU, 0, 0, 1, 1));
    /* Boundary-biased inputs checked against independent 64-bit arithmetic. */
    for (i = 0; i < 100000; ++i) {
        ULONG v[8], j, w, h, bpp, align, length, skips, pixels, address;
        uint64_t r, s, o, span, total;
        int expected, actual;
        for (j = 0; j < 8; ++j) {
            seed = seed * 1664525U + 1013904223U;
            v[j] = (i & (1U << j)) ? seed : seed % 17U;
        }
        w = v[0]; h = v[1]; bpp = v[2] % 6;
        align = (i & 1) ? (1U << (v[3] % 4)) : v[3] % 10;
        length = v[4]; skips = v[5]; pixels = v[6]; address = v[7];
        r = (uint64_t)w * bpp;
        s = ((uint64_t)(length ? length : w) * bpp + (align ? align - 1 : 0));
        if (align) s &= ~(uint64_t)(align - 1);
        /* Limit individual intermediates before products to keep the oracle
         * below uint64_t overflow even for deliberately malformed inputs. */
        expected = w && h && bpp && bpp <= 4 &&
            (align == 1 || align == 2 || align == 4 || align == 8) &&
            r <= UINT32_MAX && s <= UINT32_MAX &&
            (uint64_t)(length ? length : w) * bpp + align - 1 <= UINT32_MAX;
        if (expected) {
            total = r * h;
            o = (uint64_t)skips * s;
            expected = total <= UINT32_MAX && o <= UINT32_MAX;
            if (expected) {
                o += (uint64_t)pixels * bpp;
                expected = o <= UINT32_MAX;
            }
            if (expected) {
                span = o + (uint64_t)(h - 1) * s + r;
                expected = span <= UINT32_MAX && address <= UINT32_MAX - span;
            }
        }
        actual = MGLPPCTextureSpan(w, h, bpp, align, length, skips, pixels,
            address, &row, &stride, &offset, &bytes);
        assert(actual == expected);
        if (actual) {
            assert(row == r && stride == s && offset == o && bytes == total);
        }
    }
    puts("R200 subimage validation: format/rect/span checks + 100000 cases passed");
    return 0;
}
