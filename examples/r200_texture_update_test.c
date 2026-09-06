/* Bounded public-dispatch smoke for the NEW command-26 subimage stack.
 * Build: make -f Makefile.import.gcc bin/examples/r200_texture_update_test_ppc.warpelf
 * Run with no arguments, on a matched frontend/host/native candidate stack.
 * No transport, private context layouts, native queries or guessed addresses.
 *
 * A 16x16 RGBA image is direct-upload eligible, then its first subimage
 * exercises lazy materialization IF direct upload was negotiated/succeeded.
 * Public ReadPixels verifies full redefinition and first/repeat RGBA updates:
 * old A/new B, inside/outside patches, before present, including pack guards.
 * Direct vs shared fallback and mailbox routing still need host counters.
 * Requires command-27 readback capability; LockBack remains unavailable.
 *
 * Only the common unsigned-byte RGB/RGBA/ALPHA/LUMINANCE/LUMINANCE_ALPHA
 * source enums are used. Public MGL BGRA/ARGB/565/4444 values differ from
 * native headers; do not silently hardcode native enum numbers here.
 * Fixed work: two contexts, eight A/update/B frames each, no sleeps/retries.
 * This bounds test work, not a wedged driver's blocking call duration.
 */

#include <proto/minigl.h>
#include <stdio.h>
#include <string.h>

#define TEX_SIZE 16

static ULONG checks, failures, updates, frames, contexts;

static int Check(const char *label, ULONG actual, ULONG expected)
{
    ++checks;
    if (actual == expected)
        return 1;
    ++failures;
    printf("FAIL %s ACTUAL=0x%08lx EXPECTED=0x%08lx\n",
           label, actual, expected);
    return 0;
}

static int ErrorIs(const char *label, GLenum expected)
{
    int ok = Check(label, (ULONG)glGetError(), (ULONG)expected);
    /* Also assert that fetching the sticky error clears it. */
    return Check("ERROR CLEARED", (ULONG)glGetError(), GL_NO_ERROR) && ok;
}

static void DrawPanel(GLfloat left, GLfloat right)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(left, -1, 0);
    glTexCoord2f(1, 0); glVertex3f(right, -1, 0);
    glTexCoord2f(1, 1); glVertex3f(right, 1, 0);
    glTexCoord2f(0, 1); glVertex3f(left, 1, 0);
    glEnd();
}

static int VerifyPanel(const char *label, GLint left, const UBYTE *expected)
{
    UBYTE packed[280], reference[280];
    ULONG sample, row, index, texX, texY;
    int ok = 1;
    /* At 320x240 each half-panel texel spans 10x15 screen pixels.
     * GL y and texture t both increase upward. Read through the boundary
     * between patch rows 7 and 8, away from any triangle/panel edges. */
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 3);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
    for (sample = 0; sample < 2; ++sample) {
        texX = sample ? 2 : 6;
        memset(packed, 0xa5, sizeof(packed));
        memset(reference, 0xa5, sizeof(reference));
        for (row = 0; row < 16; ++row) {
            texY = (112 + row) * TEX_SIZE / 240;
            memcpy(reference + 20 + row * 16,
                   expected + (texY * TEX_SIZE + texX) * 4, 4);
        }
        glReadPixels(left + texX * 10 + 5, 112, 1, 16,
                     GL_RGBA, GL_UNSIGNED_BYTE, packed);
        if (!ErrorIs(label, GL_NO_ERROR)) { ok = 0; break; }
        for (index = 0; index < sizeof(packed); ++index)
            if (!Check(label, packed[index], reference[index])) {
                printf("PIXEL PANEL_X=%ld SAMPLE=%lu BYTE=%lu\n",
                       (LONG)left, sample, index);
                ok = 0;
                break;
            }
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    return ErrorIs("RESET PACK", GL_NO_ERROR) && ok;
}

int main(void)
{
    static const struct {
        const char *label;
        GLenum format;
        ULONG bpp;
        GLint alignment;
    } sources[] = {
        { "RGBA ALIGN1", GL_RGBA, 4, 1 },
        { "RGB ALIGN2 PADDED", GL_RGB, 3, 2 },
        { "ALPHA ALIGN4 PADDED", GL_ALPHA, 1, 4 },
        { "LUMINANCE ALIGN8 PADDED", GL_LUMINANCE, 1, 8 },
        { "LUMINANCE_ALPHA ALIGN8 PADDED", GL_LUMINANCE_ALPHA, 2, 8 }
    };
    static const struct {
        const char *label;
        GLenum target;
        GLint level, x, y;
        GLsizei w, h;
        GLenum format, type, error;
        int nullSource;
    } invalid[] = {
        { "BAD TARGET", GL_RGB, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_ENUM, 0 },
        { "INDEXED SOURCE", GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, GL_INVALID_ENUM, 0 },
        { "BAD TYPE", GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, GL_INVALID_ENUM, 0 },
        { "NEGATIVE LEVEL", GL_TEXTURE_2D, -1, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "UNDEFINED MIP", GL_TEXTURE_2D, 1, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "NEGATIVE X", GL_TEXTURE_2D, 0, -1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "NEGATIVE Y", GL_TEXTURE_2D, 0, 0, -1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "NEGATIVE WIDTH", GL_TEXTURE_2D, 0, 0, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "NEGATIVE HEIGHT", GL_TEXTURE_2D, 0, 0, 0, 1, -1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "RIGHT OVERRUN", GL_TEXTURE_2D, 0, 15, 0, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "BOTTOM OVERRUN", GL_TEXTURE_2D, 0, 0, 15, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "HUGE X", GL_TEXTURE_2D, 0, 0x7fffffff, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "HUGE WIDTH", GL_TEXTURE_2D, 0, 1, 0, 0x7fffffff, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 0 },
        { "NULL NONEMPTY", GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 1 },
        { "ZERO WIDTH AT EDGE", GL_TEXTURE_2D, 0, 16, 0, 0, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_NO_ERROR, 1 },
        { "ZERO HEIGHT AT EDGE", GL_TEXTURE_2D, 0, 0, 16, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, GL_NO_ERROR, 1 },
        { "ZERO OUTSIDE BOUNDS", GL_TEXTURE_2D, 0, 17, 0, 0, 1, GL_RGBA, GL_UNSIGNED_BYTE, GL_INVALID_VALUE, 1 }
    };
    UBYTE image[TEX_SIZE * TEX_SIZE * 4], before[sizeof(image)], source[128], pixel[4];
    MGLLockInfo lock;
    GLuint name = 0;
    ULONG pass, step, i, x, y, c, stride, offset;
    GLenum format;
    int opened = 0, live = 0;

#define REQUIRE_ERROR(label, expected) \
    do { if (!ErrorIs(label, expected)) goto cleanup; } while (0)

    setvbuf(stdout, NULL, _IONBF, 0);
    memset(source, 0xa5, sizeof(source));
    puts("R200_TEXTURE_UPDATE_TEST BEGIN RGBA PIXEL + GL ERROR/ORDER CHECKS");
    puts("DIRECT_PATH_AND_MAILBOX_ROUTING_UNVERIFIED: CHECK HOST COUNTERS SEPARATELY");
    if (!Check("MINIGL OPEN", MiniGLOpen() != 0, 1)) goto cleanup;
    opened = 1;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(32);
    mglChooseTextureBufferSize(4096);

    for (pass = 0; pass < 2; ++pass) {
        if (!Check("CREATE CONTEXT", mglCreateContext(0, 0, 320, 240) != 0, 1))
            goto cleanup;
        live = 1;
        ++contexts;
        REQUIRE_ERROR("FRESH CONTEXT", GL_NO_ERROR);
        if (!pass) {
            glGenTextures(1, &name);
            REQUIRE_ERROR("GEN NAME", GL_NO_ERROR);
            if (!Check("NONZERO NAME", name != 0, 1)) goto cleanup;
        }
        printf("CONTEXT=%lu TEXTURE_NAME=%lu\n", pass + 1UL, (ULONG)name);
        glBindTexture(GL_TEXTURE_2D, name);
        REQUIRE_ERROR("BIND SAME NAME", GL_NO_ERROR);
        /* The name from the destroyed context must not retain an image. */
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, source);
        REQUIRE_ERROR("FRESH NAME HAS NO IMAGE", GL_INVALID_OPERATION);

        glViewport(0, 0, 320, 240);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        REQUIRE_ERROR("SETUP", GL_NO_ERROR);
        for (y = 0; y < TEX_SIZE; ++y)
            for (x = 0; x < TEX_SIZE; ++x) {
                i = (y * TEX_SIZE + x) * 4;
                image[i] = (UBYTE)(x * 16);
                image[i + 1] = (UBYTE)(y * 16);
                image[i + 2] = (UBYTE)(32 + pass * 128);
                image[i + 3] = 255;
            }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        REQUIRE_ERROR("BASELINE RGBA FULL UPLOAD", GL_NO_ERROR);

        /* Preserve the original source as A's oracle, never a readback of A.
         * Same-shape level-zero redefinition is direct-upload eligible. */
        memcpy(before, image, sizeof(image));
        glClear(GL_COLOR_BUFFER_BIT);
        DrawPanel(-1, 0);
        REQUIRE_ERROR("FULL DRAW A QUEUED", GL_NO_ERROR);
        for (y = 0; y < TEX_SIZE; ++y)
            for (x = 0; x < TEX_SIZE; ++x) {
                i = (y * TEX_SIZE + x) * 4;
                image[i] = (UBYTE)(240 - x * 16);
                image[i + 1] = (UBYTE)(240 - y * 16);
                image[i + 2] = (UBYTE)(224 - pass * 128);
                image[i + 3] = 255;
            }
        /* No Flush/Finish/readback/present between old A and TexImage. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        REQUIRE_ERROR("FULL SAME SHAPE REDEFINITION", GL_NO_ERROR);
        ++updates;
        DrawPanel(0, 1);
        REQUIRE_ERROR("FULL DRAW B", GL_NO_ERROR);
        if (!VerifyPanel("A BEFORE FULL REDEFINITION", 0, before) ||
            !VerifyPanel("B AFTER FULL REDEFINITION", 160, image)) goto cleanup;
        mglSwitchDisplay();
        REQUIRE_ERROR("PRESENT FULL A/B", GL_NO_ERROR);
        ++frames;
        printf("SMOKE CONTEXT=%lu FULL SAME SHAPE PIXEL ORDER\n", pass + 1UL);

        /* First and repeat are tight RGBA. Then vary padded source rows,
         * both skips and alignment, keeping all reads inside source[128]. */
        for (step = 0; step < 7; ++step) {
            const char *label;
            ULONG bpp;
            GLint alignment;
            if (step < 2) {
                label = step ? "REPEAT PARTIAL" : "FIRST LAZY SUBIMAGE";
                format = GL_RGBA; bpp = 4; alignment = 4;
                stride = 12; offset = 0;
            } else {
                i = step - 2;
                label = sources[i].label;
                format = sources[i].format;
                bpp = sources[i].bpp;
                alignment = sources[i].alignment;
                stride = (5 * bpp + alignment - 1) & ~((ULONG)alignment - 1);
                offset = stride + bpp;
            }
            memset(source, 0xa5, sizeof(source));
            for (y = 0; y < 2; ++y)
                for (x = 0; x < 3; ++x)
                    for (c = 0; c < bpp; ++c)
                        source[offset + y * stride + x * bpp + c] =
                            (UBYTE)(17 + pass * 53 + step * 23 + y * 31 + x * 7 + c * 41);
            glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, step < 2 ? 0 : 5);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, step < 2 ? 0 : 1);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, step < 2 ? 0 : 1);
            REQUIRE_ERROR("UNPACK SETUP", GL_NO_ERROR);
            glClear(GL_COLOR_BUFFER_BIT);
            if (step < 2) memcpy(before, image, sizeof(image));
            DrawPanel(-1, 0);
            REQUIRE_ERROR("DRAW A QUEUED", GL_NO_ERROR);
            /* No Finish/Flush/present between A and update: the update
             * itself must publish and fence old texture snapshots. */
            glTexSubImage2D(GL_TEXTURE_2D, 0, 6, 7, 3, 2,
                            format, GL_UNSIGNED_BYTE, source);
            REQUIRE_ERROR(label, GL_NO_ERROR);
            ++updates;
            if (step < 2)
                for (y = 0; y < 2; ++y)
                    memcpy(image + ((7 + y) * TEX_SIZE + 6) * 4,
                           source + y * stride, 12);
            memset(source, 0x5a, sizeof(source));
            DrawPanel(0, 1);
            REQUIRE_ERROR("DRAW B", GL_NO_ERROR);
            if (step < 2 &&
                (!VerifyPanel("A BEFORE UPDATE", 0, before) ||
                 !VerifyPanel("B AFTER UPDATE", 160, image))) goto cleanup;
            mglSwitchDisplay();
            REQUIRE_ERROR("PRESENT A/B", GL_NO_ERROR);
            ++frames;
            printf("SMOKE CONTEXT=%lu STEP=%lu %s\n", pass + 1UL, step, label);
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        REQUIRE_ERROR("RESET UNPACK", GL_NO_ERROR);
        for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            glTexSubImage2D(invalid[i].target, invalid[i].level,
                invalid[i].x, invalid[i].y, invalid[i].w, invalid[i].h,
                invalid[i].format, invalid[i].type,
                invalid[i].nullSource ? NULL : source);
            REQUIRE_ERROR(invalid[i].label, invalid[i].error);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
        REQUIRE_ERROR("BAD ALIGNMENT", GL_INVALID_VALUE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, -1);
        REQUIRE_ERROR("NEGATIVE ROW LENGTH", GL_INVALID_VALUE);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, -1);
        REQUIRE_ERROR("NEGATIVE SKIP ROWS", GL_INVALID_VALUE);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, -1);
        REQUIRE_ERROR("NEGATIVE SKIP PIXELS", GL_INVALID_VALUE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0x7fffffff);
        REQUIRE_ERROR("SET OVERFLOWING ROW LENGTH", GL_NO_ERROR);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 2,
                        GL_RGBA, GL_UNSIGNED_BYTE, source);
        REQUIRE_ERROR("SOURCE SPAN OVERFLOW", GL_INVALID_VALUE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        REQUIRE_ERROR("RESTORE ROW LENGTH", GL_NO_ERROR);
        glBindTexture(GL_TEXTURE_2D, 0);
        REQUIRE_ERROR("UNBIND", GL_NO_ERROR);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, source);
        REQUIRE_ERROR("NO TEXTURE", GL_INVALID_OPERATION);
        glBindTexture(GL_TEXTURE_2D, name);
        REQUIRE_ERROR("REBIND", GL_NO_ERROR);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 15, 15, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, source);
        REQUIRE_ERROR("VALID EDGE UPDATE AFTER ERRORS", GL_NO_ERROR);
        ++updates;
        DrawPanel(-1, 1);
        glFinish();
        REQUIRE_ERROR("DRAW AFTER ERRORS AND FINISH", GL_NO_ERROR);

        memset(&lock, 0, sizeof(lock));
        if (!Check("LOCKBACK UNAVAILABLE", mglLockBack(&lock), GL_FALSE))
            goto cleanup;
        REQUIRE_ERROR("LOCKBACK REJECTION", GL_INVALID_OPERATION);
        memset(pixel, 0xa5, sizeof(pixel));
        glReadPixels(0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
        REQUIRE_ERROR("READPIXELS UNSUPPORTED FORMAT", GL_INVALID_ENUM);
        glReadPixels(-1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        REQUIRE_ERROR("READPIXELS NATIVE BOUNDS", GL_INVALID_VALUE);
        glReadPixels(0, 0, 1024, 513, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        REQUIRE_ERROR("READPIXELS OVER CAP", GL_INVALID_OPERATION);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0x7fffffff);
        glReadPixels(0, 0, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        REQUIRE_ERROR("READPIXELS PACK OVERFLOW", GL_INVALID_VALUE);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        for (i = 0; i < sizeof(pixel); ++i)
            if (!Check("FAILED READBACK LEAVES OUTPUT", pixel[i], 0xa5)) goto cleanup;
        /* Deliberately leave the texture alive for context-owned cleanup. */
        mglDeleteContext();
        live = 0;
    }
    Check("CONTEXT COUNT", contexts, 2);
    Check("UPDATE COUNT", updates, 18);
    Check("FRAME COUNT", frames, 16);

cleanup:
    if (live) mglDeleteContext();
    if (opened) MiniGLClose();
    printf("R200_TEXTURE_UPDATE_TEST %s CHECKS=%lu FAILURES=%lu CONTEXTS=%lu UPDATES=%lu FRAMES=%lu RGBA_FULL_FIRST_REPEAT_PIXEL_CHECKS\n",
           failures ? "FAIL" : "PASS", checks, failures,
           contexts, updates, frames);
    return failures ? 20 : 0;
}
