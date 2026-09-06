/* Red-fill channel-order probe: the pickup-flash fill path with a RED
 * diffuse color. Gray/white probes cannot catch an R/G swap; red can.
 * Also measures the cost of cycling alpha levels (1x1 texture churn).
 * Run windowed 320x240x16. */
#include <stdio.h>
#include <string.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <libraries/minigl_perf.h>

static unsigned texname;

static void MakeFillTex(unsigned alpha8)
{
    unsigned char pixel[4];
    pixel[0] = pixel[1] = pixel[2] = 255;
    pixel[3] = alpha8;
    glBindTexture(GL_TEXTURE_2D, ++texname);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* Exact Draw_AlphaFill recipe with configurable diffuse colour */
static void AlphaFill(float r, float g, float b,
                      int x, int y, int w, int h)
{
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, 1);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(1, 0); glVertex2f(x + w, y);
    glTexCoord2f(1, 1); glVertex2f(x + w, y + h);
    glTexCoord2f(0, 1); glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glColor4f(1, 1, 1, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

static MGLLockInfo li;
static unsigned short Sample(int x, int y)
{
    const unsigned char *base = (const unsigned char *)li.base_address;
    return *(const unsigned short *)(base + (unsigned long)y * li.pitch +
                                     (unsigned long)x * 2UL);
}

int main(int argc, char **argv)
{
    GLint submits0, records0;
    int W = 320, H = 240;
    if (!MiniGLOpen()) return 21;
    if (argc == 2 && !strcmp(argv[1], "--fullscreen")) {
        mglChooseWindowMode(GL_FALSE);
        W = 640; H = 480;
    } else {
        mglChooseWindowMode(GL_TRUE);
    }
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, W, H)) { MiniGLClose(); return 22; }
    glViewport(0, 0, 320, 240);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
    glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFinish();

    /* 1: untextured diffuse red FLATFAN (screen-space) */
    glColor4f(1, 0, 0, 1);
    glBegin(MGL_FLATFAN);
    glVertex2f(0, 0); glVertex2f((float)W, 0);
    glVertex2f((float)W, (float)H); glVertex2f(0, (float)H);
    glEnd();
    glColor4f(1, 1, 1, 1);
    glFinish();
    mglLockBack(&li);
    printf("redfill[%s]: diffuse-red=%04x (F800=red)\n",
           argc == 2 ? "full" : "win", Sample(W / 2, H / 2));
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    /* 2: textured alpha fill, red diffuse, alpha ~0.5 (exact game recipe) */
    MakeFillTex(128);
    AlphaFill(1, 0, 0, 0, 0, W, H);
    glFinish();
    printf("redfill[%s]: texfill-red-a50=%04x (7C00=half red, F800=OPAQUE)\n",
           argc == 2 ? "full" : "win", Sample(W / 2, H / 2));
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    /* 3: textured alpha fill, bonus-flash yellow diffuse */
    AlphaFill(0.84f, 0.73f, 0.24f, 0, 0, W, H);
    glFinish();
    printf("redfill[%s]: texfill-yellow=%04x\n",
           argc == 2 ? "full" : "win", Sample(W / 2, H / 2));
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    /* 3b: DIFFUSE-alpha fill (no texture): the Q2-verified path */
    {
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 0, 0, 0.5f);
        glBegin(MGL_FLATFAN);
        glVertex2f(0, 0); glVertex2f((float)W, 0);
        glVertex2f((float)W, (float)H); glVertex2f(0, (float)H);
        glEnd();
        glDisable(GL_BLEND);
        glColor4f(1, 1, 1, 1);
        glFinish();
        printf("redfill[%s]: diff-alpha-red=%04x (7C00=half, F800=OPAQUE)\n",
               argc == 2 ? "full" : "win", Sample(W / 2, H / 2));
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
    }

    /* 4: cost of cycling alpha levels like a decaying flash: 48 fills,
     * each a new alpha level (new 1x1 texture), over a textured scene */
    {
        int i;
        for (i = 0; i < 48; ++i) {
            MakeFillTex(17 * (i % 15 + 1));
            AlphaFill(1, 0, 0, 0, 0, W, H);
        }
        glFinish();
        glGetIntegerv(MGL_PERF_SUBMITS, &submits0);
        printf("redfill: churn done err=%lx submits+=%ld\n",
               (unsigned long)glGetError(), (long)submits0);
    }
    {
        GLint execT = 0, waitT = 0, texT = 0, submitsT = 0, recordsT = 0;
        glGetIntegerv(MGL_PERF_EXECUTE_TICKS, &execT);
        glGetIntegerv(MGL_PERF_WAIT_TICKS, &waitT);
        glGetIntegerv(MGL_PERF_TEXTURE_RESIDENCY_TICKS, &texT);
        glGetIntegerv(MGL_PERF_SUBMITS, &submitsT);
        glGetIntegerv(MGL_PERF_DRAW_RECORDS, &recordsT);
        printf("redfill: counters exec=%ld wait=%ld tex=%ld submits=%ld "
               "records=%ld\n", argc == 2 ? "full" : "win", (long)execT,
               (long)waitT, (long)texT, (long)submitsT, (long)recordsT);
    }

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
