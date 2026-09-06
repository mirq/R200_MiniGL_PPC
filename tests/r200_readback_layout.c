/* cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined
 * tests/r200_readback_layout.c -o /tmp/opencode/r200-readback-layout */
#define MGLPPC_TEXTURE_VALIDATION_TEST
#include "../backend_r200/include/libraries/minigl_ppc_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ULONG row, stride, offset, bytes, align, length, skips, pixels, h, w, y;
    unsigned char source[256], dest[4096], expected[4096];
    assert(MGLPPC_CMD_READ_PIXELS_SHARED == 27);
    assert(MGLPPC_CAP_READ_PIXELS_V1 == (1UL << 10));
    for (y = 0; y < sizeof(source); ++y) source[y] = (unsigned char)y;
    for (align = 1; align <= 8; align *= 2)
    for (length = 0; length <= 5; ++length)
    for (skips = 0; skips <= 2; ++skips)
    for (pixels = 0; pixels <= 2; ++pixels)
    for (w = 1; w <= 4; ++w)
    for (h = 1; h <= 4; ++h) {
        size_t s = (((length ? length : w) * 4 + align - 1) / align) * align;
        size_t o = skips * s + pixels * 4;
        assert(MGLPPCTextureSpan(w, h, 4, align, length, skips, pixels, 4096,
                                &row, &stride, &offset, &bytes));
        assert(row == w * 4 && stride == s && offset == o && bytes == w * h * 4);
        memset(dest, 0xa5, sizeof(dest));
        memset(expected, 0xa5, sizeof(expected));
        for (y = 0; y < h; ++y) {
            size_t x;
            memcpy(dest + offset + y * stride, source + y * row, row);
            for (x = 0; x < w * 4; ++x)
                expected[o + y * s + x] = source[y * w * 4 + x];
        }
        assert(!memcmp(dest, expected, sizeof(dest)));
    }
    assert(MGLPPCTextureSpan(1024, 512, 4, 1, 0, 0, 0, 4096,
                            &row, &stride, &offset, &bytes));
    assert(bytes == MGLPPC_READ_PIXELS_MAX_BYTES);
    assert(MGLPPCTextureSpan(1024, 513, 4, 1, 0, 0, 0, 4096,
                            &row, &stride, &offset, &bytes));
    assert(bytes > MGLPPC_READ_PIXELS_MAX_BYTES);
    assert(!MGLPPCTextureSpan(1, 2, 4, 4, 0x7fffffff, 0, 0, 4096,
                             &row, &stride, &offset, &bytes));
    assert(!MGLPPCTextureSpan(1, 1, 4, 4, 0, 0, 0, UINT32_MAX - 3,
                             &row, &stride, &offset, &bytes));
    assert(!MGLPPCTextureSpan(1, 2, 4, 8, 1, UINT32_MAX, 0, 4096,
                             &row, &stride, &offset, &bytes));
    assert(!MGLPPCTextureSpan(1, 2, 4, 8, 1, 0, UINT32_MAX, 4096,
                             &row, &stride, &offset, &bytes));
    puts("R200 readback layout: 3456 guarded pack cases and cap/overflow checks passed");
    return 0;
}
