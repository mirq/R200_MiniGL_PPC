/*
 * r200_defect_probe.c - isolated reproduction probe for the GLQuake
 * MiniGL R200 defect report (MiniGL_R200_BUG_REPORT.md).
 *
 * Stages:
 *   P1  mglWriteShotPPM baseline (error state + file existence)
 *   F   internalformat x source-format matrix, raw GL error per call,
 *       plus texsub + textured draw on each row (report draw-side errors)
 *   T   glTexImage2D timing, glFinish per iteration (fixed-cost check)
 *   S   glTexSubImage2D timing, glFinish per iteration (per-update cost)
 *   P2  mglWriteShotPPM after textured rendering
 *
 * Errors print as raw backend numbers: GL_INVALID_ENUM = 88 = 0x58,
 * GL_INVALID_OPERATION = 89 = 0x59 (mgl/gl.h enum numbering).
 * Diagnostic markers go to RAM:defprobe_marker so a wedged run still
 * leaves its stage on disk. Timing uses DateStamp (50 Hz ticks).
 *
 * Build: make -f Makefile.import.gcc bin/examples/r200_defect_probe_ppc.warpelf
 * Convert on target: DH2:wosbuild/Elf2Exe2 <in> <out>
 * Run with no arguments. All output is stdout; redirect per session policy.
 */

#include <proto/minigl.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

#define MARK_PATH "RAM:defprobe_marker"

static void Mark(const char *s)
{
    BPTR f = Open(MARK_PATH, MODE_OLDFILE);
    if (!f)
        f = Open(MARK_PATH, MODE_NEWFILE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, (STRPTR)s, (LONG)strlen(s));
        Close(f);
    }
}

static double NowDS(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (double)ds.ds_Days * 86400.0 +
           (double)ds.ds_Minute * 60.0 +
           (double)ds.ds_Tick / 50.0;
}

static const char *ErrName(GLenum e)
{
    switch (e) {
    case GL_NO_ERROR: return "NO_ERROR";
    case 88: return "INVALID_ENUM";
    case 89: return "INVALID_OPERATION";
    case 87: return "INVALID_VALUE";
    case 90: return "OUT_OF_MEMORY";
    default: return "?";
    }
}

static void Rep(const char *tag, int n, double t0, double t1)
{
    printf("%-24s %4d it  %8.1f ms total  %8.2f ms/it\n",
           tag, n, (t1 - t0) * 1000.0, (t1 - t0) * 1000.0 / n);
    fflush(stdout);
}

static int FileExists(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return 1;
    }
    return 0;
}

/* 128x128 RGBA: opaque checker with an alpha border band, so both
 * alpha-blended text rendering and plain sampling are exercised. */
static unsigned int rgba128[128 * 128];
/* 256x256 RGBA gradient (world-texture shape). */
static unsigned int rgba256[256 * 256];
/* 128x128 and 128x64 luminance gradients (lightmap shapes). */
static unsigned char lum128[128 * 128];
static unsigned char lumsub[128 * 64];
/* 1bpp-ish alpha pattern. */
static unsigned char alpha128[128 * 128];
static unsigned char lumalpha128[128 * 128 * 2];

static void BuildPatterns(void)
{
    unsigned int x, y, i;
    for (y = 0; y < 128; ++y)
        for (x = 0; x < 128; ++x) {
            int on = ((x ^ y) & 8) || x < 4 || y < 4;
            rgba128[y * 128 + x] = on ? 0xFFFFFFFFu : 0x00000000u;
        }
    for (i = 0; i < 256 * 256; ++i)
        rgba256[i] = 0xFF000000u | ((i & 0xFFu) << 16) |
                     (((i >> 2) & 0xFFu) << 8) | ((i >> 3) & 0xFFu);
    for (y = 0; y < 128; ++y)
        for (x = 0; x < 128; ++x)
            lum128[y * 128 + x] = (unsigned char)((x * 2) ^ (y * 2));
    for (i = 0; i < sizeof(lumsub); ++i)
        lumsub[i] = (unsigned char)(i * 3 + 5);
    for (i = 0; i < 128 * 128; ++i)
        alpha128[i] = (unsigned char)((i & 16) ? 255 : 0);
    for (y = 0; y < 128; ++y)
        for (x = 0; x < 128; ++x) {
            lumalpha128[(y * 128 + x) * 2] =
                (unsigned char)((x + y) & 0xFF);
            lumalpha128[(y * 128 + x) * 2 + 1] =
                (unsigned char)(255 - ((x + y) & 0xFF));
        }
}

static void DrawTexturedQuad(float x0, float y0, float x1, float y1)
{
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x1, y0);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x1, y1);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x0, y1);
    glEnd();
}

/* One matrix row: bind (fresh name), upload 128x128, report the TexImage
 * error; when the upload succeeded, a full TexSubImage, a textured draw,
 * and 10 timed TexSubImage+glFinish iterations. */
static void MatrixRow(const char *label, GLuint name, GLenum internalFmt,
                      GLenum format, const void *pixels)
{
    GLenum e;
    double a, b;
    int i;

    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, 128, 128, 0,
                 format, GL_UNSIGNED_BYTE, pixels);
    e = glGetError();
    printf("FMT %-14s teximg err=0x%02lx (%s)\n",
           label, (unsigned long)e, ErrName(e));
    fflush(stdout);
    if (e)
        return;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128,
                    format, GL_UNSIGNED_BYTE, pixels);
    e = glGetError();
    printf("FMT %-14s texsub err=0x%02lx (%s)\n",
           label, (unsigned long)e, ErrName(e));
    fflush(stdout);
    glClear(GL_COLOR_BUFFER_BIT);
    DrawTexturedQuad(16.0f, 16.0f, 400.0f, 400.0f);
    glFinish();
    e = glGetError();
    printf("FMT %-14s draw   err=0x%02lx (%s)\n",
           label, (unsigned long)e, ErrName(e));
    fflush(stdout);
    a = NowDS();
    for (i = 0; i < 10; ++i) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128,
                        format, GL_UNSIGNED_BYTE, pixels);
        glFinish();
    }
    b = NowDS();
    Rep(label, 10, a, b);
}

struct TimingFormat {
    const char *label;
    GLenum internalFmt;
    GLenum format;
    const void *pixels;
    GLsizei size;
};

static void TexImageTiming(const struct TimingFormat *f, int iterations)
{
    double a, b;
    GLenum e;

    glTexImage2D(GL_TEXTURE_2D, 0, f->internalFmt, f->size, f->size, 0,
                 f->format, GL_UNSIGNED_BYTE, f->pixels);
    e = glGetError();
    if (e) {
        printf("TIM %-24s SKIPPED teximg err=0x%02lx (%s)\n",
               f->label, (unsigned long)e, ErrName(e));
        fflush(stdout);
        return;
    }
    a = NowDS();
    {
        int i;
        for (i = 0; i < iterations; ++i) {
            glTexImage2D(GL_TEXTURE_2D, 0, f->internalFmt, f->size,
                         f->size, 0, f->format, GL_UNSIGNED_BYTE,
                         f->pixels);
            glFinish();
        }
    }
    b = NowDS();
    Rep(f->label, iterations, a, b);
    e = glGetError();
    if (e)
        printf("TIM %-24s loop err=0x%02lx (%s)\n",
               f->label, (unsigned long)e, ErrName(e));
    fflush(stdout);
}

struct SubTiming {
    const char *label;
    GLint x, y;
    GLsizei w, h;
    GLenum format;
    const void *pixels;
};

static void TexSubTiming(const struct SubTiming *s, int iterations)
{
    double a, b;
    GLenum e;
    int i;

    a = NowDS();
    for (i = 0; i < iterations; ++i) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, s->x, s->y, s->w, s->h,
                        s->format, GL_UNSIGNED_BYTE, s->pixels);
        glFinish();
    }
    b = NowDS();
    Rep(s->label, iterations, a, b);
    e = glGetError();
    if (e)
        printf("SUB %-24s loop err=0x%02lx (%s)\n",
               s->label, (unsigned long)e, ErrName(e));
    fflush(stdout);
}

int main(void)
{
    GLuint names[8];
    unsigned int i;
    GLenum e;

    setvbuf(stdout, NULL, _IONBF, 0);
    DeleteFile(MARK_PATH);
    Mark("m0 main\n");
    printf("R200_DEFECT_PROBE BEGIN (errors: 88=INVALID_ENUM "
           "89=INVALID_OPERATION 87=INVALID_VALUE)\n");

    BuildPatterns();

    if (!MiniGLOpen()) {
        Mark("m1 MiniGLOpen FAILED\n");
        printf("MiniGLOpen failed\n");
        return 20;
    }
    Mark("m1 open ok\n");
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(32);
    if (!mglCreateContext(0, 0, 640, 480)) {
        Mark("m2 CreateContext FAILED\n");
        printf("CreateContext failed\n");
        MiniGLClose();
        return 20;
    }
    Mark("m2 context ok\n");
    printf("context ok (windowed 640x480x32)\n");
    glViewport(0, 0, 640, 480);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 640, 480, 0, -999, 999);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0, 0, 0, 1);
    printf("setup err=0x%02lx\n",
           (unsigned long)glGetError());

    /* ---- P1: screenshot baseline ---- */
    Mark("P1 shot baseline\n");
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    printf("P1 err-before-shot=0x%02lx\n",
           (unsigned long)glGetError());
    mglWriteShotPPM("RAM:defprobe_p1.ppm");
    printf("P1 err-after-shot=0x%02lx file=%d\n",
           (unsigned long)glGetError(),
           FileExists("RAM:defprobe_p1.ppm"));

    /* ---- F: internalformat x format matrix ---- */
    Mark("F matrix\n");
    glGenTextures(8, names);
    printf("F gen err=0x%02lx names=%lu..%lu\n",
           (unsigned long)glGetError(),
           (unsigned long)names[0], (unsigned long)names[7]);
    glEnable(GL_TEXTURE_2D);
    MatrixRow("RGBA/RGBA", names[0], GL_RGBA, GL_RGBA, rgba128);
    MatrixRow("RGB/RGBA", names[1], GL_RGB, GL_RGBA, rgba128);
    MatrixRow("RGB5_A1/RGBA", names[2], GL_RGB5_A1, GL_RGBA, rgba128);
    MatrixRow("RGB/RGB", names[3], GL_RGB, GL_RGB, rgba128);
    MatrixRow("LUM/LUM", names[4], GL_LUMINANCE, GL_LUMINANCE, lum128);
    MatrixRow("ALPHA/ALPHA", names[5], GL_ALPHA, GL_ALPHA, alpha128);
    MatrixRow("LUMA/LUMA", names[6], GL_LUMINANCE_ALPHA,
              GL_LUMINANCE_ALPHA, lumalpha128);
    /* Reporter misuse case: a type constant passed as internalformat. */
    MatrixRow("565/565-misuse", names[7], MGL_UNSIGNED_SHORT_5_6_5,
              GL_RGB, rgba128);
    /* Game idiom: sequential ids without glGenTextures (font texture). */
    glBindTexture(GL_TEXTURE_2D, 61);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba128);
    e = glGetError();
    printf("FMT seq-id-61      teximg err=0x%02lx (%s)\n",
           (unsigned long)e, ErrName(e));

    /* ---- T: TexImage fixed-cost timing ---- */
    Mark("T teximage timing\n");
    {
        static const struct TimingFormat timings[] = {
            { "teximg RGB5_A1 128", GL_RGB5_A1, GL_RGBA,
              rgba128, 128 },
            { "teximg RGB     128", GL_RGB, GL_RGBA, rgba128, 128 },
            { "teximg RGBA    128", GL_RGBA, GL_RGBA, rgba128, 128 },
            { "teximg RGB     256", GL_RGB, GL_RGBA, rgba256, 256 }
        };
        for (i = 0; i < 4; ++i) {
            glBindTexture(GL_TEXTURE_2D, 70 + (GLuint)i);
            TexImageTiming(&timings[i], 20);
        }
    }

    /* ---- S: TexSubImage per-update cost ---- */
    Mark("S texsubimage timing\n");
    {
        static const struct SubTiming subs[] = {
            { "texsub 256 full", 0, 0, 256, 256, GL_RGBA, rgba256 },
            { "texsub 256 128x128", 0, 0, 128, 128, GL_RGBA, rgba128 },
            { "texsub 256 128x64", 0, 0, 128, 64, GL_RGBA, rgba128 }
        };
        glBindTexture(GL_TEXTURE_2D, 70 + 3); /* the 256 RGB texture */
        printf("S bind err=0x%02lx\n", (unsigned long)glGetError());
        for (i = 0; i < 3; ++i)
            TexSubTiming(&subs[i], 20);
        /* Luminance lightmap update on the LUM texture from stage F. */
        glBindTexture(GL_TEXTURE_2D, names[4]);
        printf("S bind lum err=0x%02lx\n",
               (unsigned long)glGetError());
        {
            const struct SubTiming lumSub = {
                "texsub lum 128x64", 0, 0, 128, 64,
                GL_LUMINANCE, lumsub
            };
            TexSubTiming(&lumSub, 20);
        }
    }

    /* ---- P2: screenshot after textured rendering ---- */
    Mark("P2 shot final\n");
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, names[0]);
    DrawTexturedQuad(32.0f, 32.0f, 608.0f, 448.0f);
    glFinish();
    printf("P2 err-before-shot=0x%02lx\n",
           (unsigned long)glGetError());
    mglWriteShotPPM("RAM:defprobe_p2.ppm");
    printf("P2 err-after-shot=0x%02lx file=%d\n",
           (unsigned long)glGetError(),
           FileExists("RAM:defprobe_p2.ppm"));

    printf("R200_DEFECT_PROBE DONE\n");
    Mark("m9 done\n");
    mglDeleteContext();
    MiniGLClose();
    return 0;
}
