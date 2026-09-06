/* Texture-upload hot-path smoke test for the R200 WarpOS stack.
 *
 * usage: r200_texture_upload_smoke [frames] [full|tiles|tiles16|stride]
 *                                  [window|fullscreen]
 *
 *   full   - one 512x512 RGBA glTexImage2D per frame drawn as a full-screen
 *            quad (game-like full-frame present upload, 1 MB/frame).
 *   tiles  - four 256x256 textures re-uploaded and drawn per frame (the
 *            game's present path is tiled; also ~1 MB/frame).
 *   tiles16 - four 256x256 RGB565 uploads (~512 KB/frame).
 *   stride - one 512x512 upload per frame read from a 576-wide source buffer
 *            through GL_UNPACK_ROW_LENGTH (row-stride handling check).
 *
 * The report separates source generation, TexImage2D calls and display
 * switching. Raw ticks use a timebase rate calibrated against GetSysTimePPC.
 * Upload timing measures CPU call duration, not GPU completion.
 *
 * The pattern is deterministic and changes every frame (blue = frame*8), and
 * the PPC frontend stubs glReadPixels, so cache-coherency verification is a
 * screenshot taken mid-run plus per-frame glGetError. Run the same binary
 * against the old host (chunked path) and the new host (shared path); the
 * PPCPROF line printed at context delete reports calls and milliseconds for
 * both.
 */

#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/minigl.h>
#include <powerpc/powerpc_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEX_FULL 512
#define TEX_TILE 256
#define SRC_WIDTH 576
#define DEFAULT_FRAMES 30

enum { MODE_FULL = 0, MODE_TILES = 1, MODE_STRIDE = 2,
       MODE_TILES16 = 3 };

/* Retry across a low-word rollover; retain all ticks even on long runs.
 * The memory clobber keeps source-buffer writes inside their timed section. */
static unsigned long long ReadTimebase(void)
{
    ULONG hi, lo, check;
    do {
        __asm__ volatile ("mftbu %0\n\tmftb %1\n\tmftbu %2"
                          : "=&r" (hi), "=&r" (lo), "=&r" (check)
                          : : "memory");
    } while (hi != check);
    return ((unsigned long long)hi << 32) | lo;
}

static void FillPattern(UBYTE *pixels, ULONG width, ULONG height,
                        ULONG stridePixels, ULONG frame, ULONG seed)
{
    ULONG x, y;

    for (y = 0; y < height; ++y) {
        UBYTE *row = pixels + y * stridePixels * 4UL;
        for (x = 0; x < width; ++x) {
            row[x * 4UL + 0] = (UBYTE)x;
            row[x * 4UL + 1] = (UBYTE)y;
            row[x * 4UL + 2] = (UBYTE)(frame * 8UL + seed);
            row[x * 4UL + 3] = 255;
        }
    }
}

/* Big-endian R5G6B5 halfwords: the exact source layout the game's
 * present path uploads via (MGL_UNSIGNED_SHORT_5_6_5,
 * GL_UNSIGNED_SHORT). */

/* The game binary passes the NATIVE header's enum values. The repo's
 * public include/mgl/gl.h diverges after GL_FILL and gives
 * MGL_UNSIGNED_SHORT_5_6_5 the value 235 there; the frontend and the
 * game both use 239/240. */
#define GAME_UNSIGNED_SHORT_5_6_5 ((GLenum)239)
#define GAME_UNSIGNED_SHORT_4_4_4_4 ((GLenum)240)

static void FillPattern565(UBYTE *pixels, ULONG width, ULONG height,
                           ULONG frame, ULONG seed)
{
    ULONG x, y;

    for (y = 0; y < height; ++y) {
        UBYTE *row = pixels + y * width * 2UL;
        for (x = 0; x < width; ++x) {
            ULONG r = ((x * 255UL / (width - 1UL)) >> 3) & 0x1fUL;
            ULONG g = ((y * 255UL / (height - 1UL)) >> 2) & 0x3fUL;
            ULONG b = (((frame * 8UL + seed) & 0xffUL) >> 3) & 0x1fUL;
            ULONG v = (r << 11) | (g << 5) | b;
            row[x * 2UL + 0] = (UBYTE)(v >> 8);
            row[x * 2UL + 1] = (UBYTE)(v & 0xffUL);
        }
    }
}

static void DrawTexturedQuad(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x0, y0, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x1, y0, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x1, y1, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x0, y1, 0.0f);
    glEnd();
}

int main(int argc, char **argv)
{
    struct timeval referenceStart, referenceEnd;
    UBYTE *pixels;
    GLuint textures[4] = { 0, 0, 0, 0 };
    GLenum error = GL_NO_ERROR;
    unsigned long long start, tb0, ticks, calibrationTicks, referenceUs;
    unsigned long long patternTicks = 0, uploadTicks = 0;
    unsigned long long presentTicks = 0, uploadBytes = 0;
    ULONG tickRate = 0;
    ULONG uploadCalls = 0, completedFrames = 0;
    ULONG textureCount, tex, bytesPerFrame;
    int mode = MODE_FULL;
    int frame;
    int frames = DEFAULT_FRAMES;
    GLboolean windowed = GL_TRUE;

    if (argc > 1) {
        frames = atoi(argv[1]);
        if (frames <= 0 || frames > 10000) {
            puts("usage: r200_texture_upload_smoke [frames] [full|tiles|tiles16|stride]");
            return 10;
        }
    }
    if (argc > 2) {
        if (strcmp(argv[2], "tiles") == 0)
            mode = MODE_TILES;
        else if (strcmp(argv[2], "tiles16") == 0)
            mode = MODE_TILES16;
        else if (strcmp(argv[2], "stride") == 0)
            mode = MODE_STRIDE;
        else if (strcmp(argv[2], "full") != 0) {
            puts("usage: r200_texture_upload_smoke [frames] [full|tiles|tiles16|stride]");
            return 10;
        }
    }
    if (argc > 3) {
        if (strcmp(argv[3], "fullscreen") == 0)
            windowed = GL_FALSE;
        else if (strcmp(argv[3], "window") != 0) {
            puts("usage: r200_texture_upload_smoke [frames] [full|tiles|tiles16|stride]");
            return 10;
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    if (mode == MODE_TILES)
        bytesPerFrame = 4UL * TEX_TILE * TEX_TILE * 4UL;
    else if (mode == MODE_TILES16)
        bytesPerFrame = 4UL * TEX_TILE * TEX_TILE * 2UL;
    else
        bytesPerFrame = TEX_FULL * TEX_FULL * 4UL;
    pixels = (UBYTE *)malloc(SRC_WIDTH * TEX_FULL * 4UL);
    if (!pixels) {
        puts("texture: allocation failed");
        return 20;
    }
    memset(pixels, 0, SRC_WIDTH * TEX_FULL * 4UL);

    puts("texture: open");
    if (!MiniGLOpen()) {
        puts("texture: open failed");
        free(pixels);
        return 20;
    }
    mglChooseWindowMode(windowed);
    mglChoosePixelDepth(32);
    mglChooseTextureBufferSize(4096);
    if (!mglCreateContext(0, 0, 640, 480)) {
        puts("texture: create failed");
        MiniGLClose();
        free(pixels);
        return 20;
    }

    glViewport(0, 0, 640, 480);
    textureCount = (mode == MODE_TILES || mode == MODE_TILES16)
        ? 4UL : 1UL;
    glGenTextures((GLsizei)textureCount, textures);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    if (mode == MODE_STRIDE)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, SRC_WIDTH);

    GetSysTimePPC(&referenceStart);
    tb0 = ReadTimebase();
    Delay(10);
    calibrationTicks = ReadTimebase() - tb0;
    GetSysTimePPC(&referenceEnd);
    SubTimePPC(&referenceEnd, &referenceStart);
    referenceUs = (unsigned long long)referenceEnd.tv_secs * 1000000ULL +
                  referenceEnd.tv_micro;
    if (referenceUs && calibrationTicks &&
        calibrationTicks <= ~0ULL / 1000000ULL) {
        unsigned long long hz = calibrationTicks * 1000000ULL / referenceUs;
        if (hz && hz <= (ULONG)~0UL)
            tickRate = (ULONG)hz;
    }
    /* Target printf cannot consume 64-bit arguments; pass separate words. */
    printf("texture: calibration ticks=0x%08lx%08lx "
           "referenceUs=0x%08lx%08lx tbHz=%lu\n",
           (ULONG)(calibrationTicks >> 32), (ULONG)calibrationTicks,
           (ULONG)(referenceUs >> 32), (ULONG)referenceUs, tickRate);
    if (!tickRate)
        puts("texture: calibration rate unavailable (zero or out of range)");

    start = ReadTimebase();
    for (frame = 0; frame < frames; ++frame) {
        for (tex = 0; tex < textureCount; ++tex) {
            GLenum uploadErr;
            ULONG size = textureCount == 4 ? TEX_TILE : TEX_FULL;

            tb0 = ReadTimebase();
            if (mode == MODE_TILES16)
                FillPattern565(pixels, size, size, (ULONG)frame, tex * 40UL);
            else
                FillPattern(pixels, size, size,
                            mode == MODE_STRIDE ? SRC_WIDTH : size,
                            (ULONG)frame, tex * 40UL);
            patternTicks += ReadTimebase() - tb0;

            if (textureCount == 4)
                glBindTexture(GL_TEXTURE_2D, textures[tex]);
            tb0 = ReadTimebase();
            if (mode == MODE_TILES16)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size,
                              0, GAME_UNSIGNED_SHORT_5_6_5,
                              GL_UNSIGNED_SHORT, pixels);
            else
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            uploadTicks += ReadTimebase() - tb0;
            ++uploadCalls;
            uploadBytes += bytesPerFrame / textureCount;
            uploadErr = glGetError();
            if (uploadErr != GL_NO_ERROR) {
                printf("texture: upERR f=%ld tex=%lu err=0x%04lx\n",
                       (LONG)frame, tex, (unsigned long)uploadErr);
                if (error == GL_NO_ERROR)
                    error = uploadErr;
            }
        }
        glClear(GL_COLOR_BUFFER_BIT);
        if (mode == MODE_TILES || mode == MODE_TILES16) {
            for (tex = 0; tex < textureCount; ++tex) {
                glBindTexture(GL_TEXTURE_2D, textures[tex]);
                DrawTexturedQuad((tex & 1UL) ? 0.0f : -1.0f,
                                 (tex & 2UL) ? 0.0f : -1.0f,
                                 (tex & 1UL) ? 1.0f : 0.0f,
                                 (tex & 2UL) ? 1.0f : 0.0f);
            }
        } else {
            DrawTexturedQuad(-1.0f, -1.0f, 1.0f, 1.0f);
        }
        tb0 = ReadTimebase();
        mglSwitchDisplay();
        presentTicks += ReadTimebase() - tb0;
        ++completedFrames;
        {
            GLenum presentErr = glGetError();
            if (error == GL_NO_ERROR)
                error = presentErr;
        }
        if (error != GL_NO_ERROR)
            break;
    }
    ticks = ReadTimebase() - start;
    printf("texture: mode=%s frames=%lu requested=%ld totalTicks=0x%08lx%08lx "
           "patternTicks=0x%08lx%08lx uploadTicks=0x%08lx%08lx "
           "presentTicks=0x%08lx%08lx calls=%lu bytes/frame=%lu "
           "uploadBytes=0x%08lx%08lx error=0x%04lx\n",
           mode == MODE_TILES ? "tiles"
               : (mode == MODE_TILES16 ? "tiles16"
                  : (mode == MODE_STRIDE ? "stride" : "full")),
           completedFrames, (LONG)frames,
           (ULONG)(ticks >> 32), (ULONG)ticks,
           (ULONG)(patternTicks >> 32), (ULONG)patternTicks,
           (ULONG)(uploadTicks >> 32), (ULONG)uploadTicks,
           (ULONG)(presentTicks >> 32), (ULONG)presentTicks,
           uploadCalls, bytesPerFrame,
           (ULONG)(uploadBytes >> 32), (ULONG)uploadBytes,
           (unsigned long)error);
    if (tickRate && uploadTicks && uploadBytes <= ~0ULL / tickRate) {
        unsigned long long byteRate = uploadBytes * tickRate / uploadTicks;
        printf("texture: uploadBytes/secHex=0x%08lx%08lx "
               "(attempted payload, CPU calls)\n",
               (ULONG)(byteRate >> 32), (ULONG)byteRate);
        if (byteRate <= (ULONG)~0UL)
            printf("texture: uploadBytes/sec=%lu\n", (ULONG)byteRate);
        else
            puts("texture: uploadBytes/sec=out_of_ULONG_range (see exact hex)");
    } else
        puts("texture: uploadBytes/sec=unavailable (invalid timing or overflow)");

    /* Hold the last presented frame for 3 s so a screenshot can verify the
     * pattern (R = x gradient, G = y gradient, B = final frame) while the
     * 68k side is idle. */
    Delay(150);
    glDeleteTextures((GLsizei)textureCount, textures);
    mglDeleteContext();
    MiniGLClose();
    free(pixels);
    puts("texture: done");
    return error == GL_NO_ERROR && completedFrames == (ULONG)frames ? 0 : 20;
}
