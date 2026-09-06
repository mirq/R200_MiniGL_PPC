/*
 * V19-parity test for the R200 backend (see plan.md).
 *
 * Covers the APIs the V19 mglQ3 implementation actually provided and the
 * V10 R200 backend now mirrors:
 *   - glInterleavedArrays: all six V19 layouts, custom strides, state
 *     replacement, GL_INVALID_ENUM/GL_INVALID_VALUE error paths, drawing
 *     through glDrawArrays with pixel-readback verification
 *   - glHint: GL_FOG_HINT / GL_PERSPECTIVE_CORRECTION_HINT accepted
 *   - mglGetSupportedScreenModes: V19 return semantics (selected mode ID
 *     or MGL_SM_BESTMODE)
 *   - mglDrawMultitexBuffer: two-pass queued polygon flush
 *   - pre-context choosers accepted before context creation
 *
 * Self-contained: generated checker textures, no data-package dependency.
 * Run windowed at 16 or 32 bpp (fullscreen also fine):
 *   v19_parity_r200 [-depth 16|32] [--fullscreen]
 */

#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static int passCount, failCount;

#define CHECK(name, condition) \
    do { \
        if (condition) { ++passCount; } \
        else { ++failCount; \
               printf("FAIL: %s\n", name); } \
    } while (0)

#define CHECK_ERROR(name, expected) \
    CHECK(name, glGetError() == (expected))

static GLboolean CountModesCallback(MGLScreenMode *mode)
{
    (void)mode;
    return GL_FALSE;
}

static GLboolean PickAnyCallback(MGLScreenMode *mode)
{
    (void)mode;
    return GL_TRUE;
}

static MGLScreenMode firstModeCapture;

static GLboolean StoreFirstCallback(MGLScreenMode *mode)
{
    firstModeCapture = *mode;
    return GL_FALSE;
}

static void FillChecker(GLubyte *image, int width, int height,
                        GLubyte r, GLubyte g, GLubyte b)
{
    int x, y;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            GLubyte *pixel = image + (y * width + x) * 4;
            int on = ((x / 1) + (y / 1)) & 1;
            pixel[0] = on ? r : 0;
            pixel[1] = on ? g : 0;
            pixel[2] = on ? b : 0;
            pixel[3] = 255;
        }
    }
}

static void MakeTexture(GLuint name, GLubyte r, GLubyte g, GLubyte b)
{
    static GLubyte image[4 * 4 * 4];
    FillChecker(image, 4, 4, r, g, b);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* Reads several framebuffer rows and reports whether any non-clear colour
 * is present (works for 16 and 32 bpp targets). */
static int FramebufferHasContent(void)
{
    MGLLockInfo info;
    int found = 0;
    long row;
    glFinish();
    if (!mglLockBack(&info))
        return -1;
    for (row = info.height / 4; row <= info.height * 3 / 4 && !found;
         row += info.height / 4) {
        unsigned char *line = (unsigned char *)info.base_address +
                              row * (long)info.pitch;
        int x;
        if (info.depth == 32) {
            /* B8G8R8A8 memory order [B,G,R,A]; m68k is big-endian, so a
             * native ULONG read is B<<24|G<<16|R<<8|A — alpha is the LOW
             * byte. (A little-endian mask here made every correct black
             * clear read as "content" at 32 bpp.) */
            for (x = 0; x < (int)info.width && !found; ++x) {
                unsigned long pixel = ((unsigned long *)line)[x];
                if ((pixel & 0xffffff00UL) != 0)
                    found = 1;
            }
        } else if (info.depth == 16) {
            for (x = 0; x < (int)info.width && !found; ++x) {
                unsigned short pixel = ((unsigned short *)line)[x];
                if (pixel != 0)
                    found = 1;
            }
        } else if (info.depth == 8) {
            /* CLUT8: one palette index per byte; the fixed fullscreen
             * palette maps black to index 0, so any non-zero index is
             * content. */
            for (x = 0; x < (int)info.width && !found; ++x)
                if (line[x] != 0)
                    found = 1;
        }
    }
    mglUnlockDisplay();
    return found;
}

static void TestHints(void)
{
    glHint(GL_FOG_HINT, GL_NICEST);
    CHECK_ERROR("hint fog nicest", GL_NO_ERROR);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    CHECK_ERROR("hint perspective fastest", GL_NO_ERROR);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_DONT_CARE);
    CHECK_ERROR("hint perspective dontcare", GL_NO_ERROR);
    glHint(0x7fff, GL_NICEST);
    CHECK_ERROR("hint unknown target", GL_INVALID_ENUM);
    glHint(GL_FOG_HINT, 0x7fff);
    CHECK_ERROR("hint unknown mode", GL_INVALID_ENUM);
}

static void TestInterleavedErrors(void)
{
    static GLfloat buffer[64];

    glInterleavedArrays(0x7fff, 0, buffer);
    CHECK_ERROR("interleaved unknown format", GL_INVALID_ENUM);
    glInterleavedArrays(GL_V3F, -8, buffer);
    CHECK_ERROR("interleaved negative stride", GL_INVALID_VALUE);
    glInterleavedArrays(GL_V3F, 0, buffer);
    CHECK_ERROR("interleaved V3F ok", GL_NO_ERROR);
    glInterleavedArrays(GL_V3F, 0, NULL);
    CHECK_ERROR("interleaved null pointer", GL_INVALID_VALUE);
}

/*
 * Fills records 0..2 of the layout's buffer with a real triangle plus
 * valid colour/texcoord fields, honouring each format's byte offsets.
 * (The first version of this test filled one 8-float pattern regardless
 * of layout, which produced collinear or all-zero vertices for exactly
 * the layouts that failed on hardware — a test-data bug, not a backend
 * bug; see plan.md progress log.)
 */
static void TestInterleavedDraw(GLenum format, GLsizei stride,
                                const char *name)
{
    static GLfloat floatBuffer[16 * 8];
    static GLubyte byteBuffer[16 * 32];
    GLubyte *bytes = NULL;
    GLfloat *floats = NULL;
    int voff = 0, coff = -1, toff = -1, record = 0;
    int i;

    if (format == GL_C4UB_V3F || format == GL_T2F_C4UB_V3F) {
        bytes = byteBuffer;
        record = stride ? stride : (format == GL_C4UB_V3F ? 16 : 24);
    } else {
        floats = floatBuffer;
        record = stride ? stride : (format == GL_V3F ? 12 :
                                    format == GL_C3F_V3F ? 24 :
                                    format == GL_T2F_V3F ? 20 : 32);
    }
    memset(floatBuffer, 0, sizeof(floatBuffer));
    memset(byteBuffer, 0, sizeof(byteBuffer));

    switch (format) {
    case GL_V3F:          voff = 0; break;
    case GL_C4UB_V3F:     voff = 4; coff = 0; break;
    case GL_C3F_V3F:      voff = 12; coff = 0; break;
    case GL_T2F_V3F:      voff = 8; toff = 0; break;
    case GL_T2F_C4UB_V3F: voff = 12; coff = 8; toff = 0; break;
    case GL_T2F_C3F_V3F:  voff = 20; coff = 8; toff = 0; break;
    default: break;
    }

    for (i = 0; i < 3; ++i) {
        GLfloat x = (i == 0) ? -0.6f : (i == 1) ? 0.0f : 0.6f;
        GLfloat y = (i == 1) ? 0.6f : -0.5f;
        GLubyte *rec = bytes ? bytes + i * record : NULL;
        GLfloat *frec = floats ? floats + i * (record / 4) : NULL;
        if (rec) {
            if (coff >= 0) {
                rec[coff + 0] = 255; rec[coff + 1] = 128;
                rec[coff + 2] = 64;  rec[coff + 3] = 255;
            }
            if (toff >= 0) {
                GLfloat t[2] = { 0.5f, 0.5f };
                memcpy(rec + toff, t, sizeof(t));
            }
            memcpy(rec + voff, &x, sizeof(GLfloat));
            memcpy(rec + voff + 4, &y, sizeof(GLfloat));
        } else {
            if (coff >= 0) {
                frec[coff / 4 + 0] = 1.0f; frec[coff / 4 + 1] = 1.0f;
                frec[coff / 4 + 2] = 1.0f;
            }
            if (toff >= 0) {
                frec[toff / 4 + 0] = 0.5f; frec[toff / 4 + 1] = 0.5f;
            }
            frec[voff / 4 + 0] = x;
            frec[voff / 4 + 1] = y;
            frec[voff / 4 + 2] = 0.0f;
        }
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glInterleavedArrays(format, stride,
                        bytes ? (const void *)bytes : (const void *)floats);
    CHECK_ERROR(name, GL_NO_ERROR);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_NO_ERROR) {
        ++failCount;
        printf("FAIL: %s draw\n", name);
        return;
    }
    CHECK(name, FramebufferHasContent() > 0);
    glGetError();
}

static void TestMultitexBuffer(void)
{
    MakeTexture(1, 255, 0, 0);
    MakeTexture(2, 0, 255, 0);
    glGetError();

    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glBindTexture(GL_TEXTURE_2D, 2);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    glBegin(GL_POLYGON);
    glColor3f(1.0f, 1.0f, 1.0f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 0.0f);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0f, 0.0f);
    glVertex2f(-0.5f, -0.5f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 0.0f);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0f, 0.0f);
    glVertex2f(0.5f, -0.5f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 1.0f);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0f, 1.0f);
    glVertex2f(0.5f, 0.5f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 1.0f);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0f, 1.0f);
    glVertex2f(-0.5f, 0.5f);
    glEnd();
    /* Queued, not drawn yet: nothing but the clear colour. */
    CHECK("mtex queued defers draw", FramebufferHasContent() == 0);

    mglDrawMultitexBuffer(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_MODULATE);
    CHECK_ERROR("mtex flush", GL_NO_ERROR);
    CHECK("mtex flush drew", FramebufferHasContent() > 0);

    /* Disabled unit 1: polygon renders normally, flush is a no-op. */
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_POLYGON);
    glColor3f(1.0f, 1.0f, 1.0f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 0.0f);
    glVertex2f(-0.5f, -0.5f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 0.0f);
    glVertex2f(0.5f, -0.5f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 1.0f);
    glVertex2f(0.5f, 0.5f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 1.0f);
    glVertex2f(-0.5f, 0.5f);
    glEnd();
    CHECK("unit0-only poly draws directly", FramebufferHasContent() > 0);
    /* Diagnostic: blank the framebuffer, then flush. Content after the
     * flush proves the polygon was captured into the mtex queue instead
     * of drawn directly (a pending submit would already have shown up in
     * the check above). */
    glClear(GL_COLOR_BUFFER_BIT);
    if (FramebufferHasContent() == 0) {
        mglDrawMultitexBuffer(GL_ONE, GL_ONE, GL_REPLACE);
        if (FramebufferHasContent() > 0)
            printf("DIAG: polygon was captured by the mtex queue\n");
    }
    CHECK_ERROR("mtex flush after direct draw", GL_NO_ERROR);
    glActiveTextureARB(GL_TEXTURE0_ARB);
}

static void TestScreenModes(void)
{
    GLint id;
    glGetError();
    id = mglGetSupportedScreenModes(CountModesCallback);
    CHECK("screenmodes bestmode when rejected",
          (ULONG)id == MGL_SM_BESTMODE);
    id = mglGetSupportedScreenModes(PickAnyCallback);
    CHECK("screenmodes returns picked id",
          id != 0 && (ULONG)id != MGL_SM_BESTMODE);
    id = mglGetSupportedScreenModes(NULL);
    CHECK("screenmodes null callback", (ULONG)id == MGL_SM_BESTMODE);
}

int main(int argc, char **argv)
{
    void *context;
    GLint width = 320;
    GLint height = 240;
    GLint depth = 16;
    GLint buffers = 2;
    GLboolean fullscreen = GL_FALSE;
    int argument;
    GLint firstId;

    for (argument = 1; argument < argc; ++argument) {
        if (!strcmp(argv[argument], "--fullscreen")) {
            fullscreen = GL_TRUE;
        } else if (!strcmp(argv[argument], "-depth") &&
                   argument + 1 < argc) {
            depth = atoi(argv[++argument]);
        } else if (!strcmp(argv[argument], "-buffers") &&
                   argument + 1 < argc) {
            buffers = atoi(argv[++argument]);
        } else {
            printf("Usage: v19_parity_r200 [--fullscreen]"
                   " [-depth 8|16|32] [-buffers 1|2|3]\n");
            return 10;
        }
    }

    /* Pre-context choosers (V19 parity: accepted before creation). */
    MiniGLOpen();
    mglChooseMtexBufferSize(1024);
    mglChooseVertexBufferSize(1024);
    mglChooseTextureBufferSize(256);
    mglChooseNumberOfBuffers((int)buffers);
    mglChooseGuardBand(GL_FALSE);
    mglProhibitAlphaFallback(GL_FALSE);
    mglProhibitMipMapping(GL_FALSE);
    mglProposeCloseDesktop(GL_FALSE);
    glGetError();

    mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
    mglChoosePixelDepth((int)depth);
    context = mglCreateContext(0, 0, width, height);
    if (!context) {
        printf("v19_parity: context creation failed\n");
        MiniGLClose();
        return 20;
    }
    printf("v19_parity: abi=%ld vendor=%s renderer=%s\n",
           (long)MiniGLDispatch->abiVersion,
           glGetString(GL_VENDOR), glGetString(GL_RENDERER));

    /* Enumerate one mode for reporting. */
    firstId = mglGetSupportedScreenModes(StoreFirstCallback);
    printf("v19_parity: first mode %ldx%ldx%ld id=0x%08lx\n",
           (long)firstModeCapture.width, (long)firstModeCapture.height,
           (long)firstModeCapture.bit_depth, (unsigned long)firstId);

    TestScreenModes();
    TestHints();
    TestInterleavedErrors();
    TestInterleavedDraw(GL_V3F, 0, "interleaved draw V3F");
    TestInterleavedDraw(GL_C4UB_V3F, 0, "interleaved draw C4UB_V3F");
    TestInterleavedDraw(GL_C3F_V3F, 0, "interleaved draw C3F_V3F");
    TestInterleavedDraw(GL_T2F_V3F, 0, "interleaved draw T2F_V3F");
    TestInterleavedDraw(GL_T2F_C4UB_V3F, 0, "interleaved draw T2F_C4UB_V3F");
    TestInterleavedDraw(GL_T2F_C3F_V3F, 0, "interleaved draw T2F_C3F_V3F");
    TestInterleavedDraw(GL_V3F, 32, "interleaved draw V3F stride32");
    TestMultitexBuffer();

    mglDeleteContext();
    MiniGLClose();
    printf("v19_parity: %d passed, %d failed\n", passCount, failCount);
    return failCount ? 20 : 0;
}
