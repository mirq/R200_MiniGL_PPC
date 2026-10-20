/* Q3 menu-background replica (WarpOS PPC, public dispatch API only).
 *
 * Reproduces ioq3's Amiga windowed-menu pipeline 1:1 so the Q3
 * black-screen can be bisected OUTSIDE the game:
 *   - context setup exactly like amiga_glimp.c GLimp_Init:
 *     ChoosePixelDepth/WindowMode(TRUE)/NumberOfBuffers(2)/GuardBand(off)/
 *     VertexBufferSize(4096)/CreateContext
 *   - GL defaults exactly like GL_SetDefaultState: TEXTURE_2D on,
 *     TexEnv MODULATE, ShadeModel SMOOTH, DepthFunc LEQUAL,
 *     VERTEX_ARRAY client state always enabled, DepthMask TRUE,
 *     DEPTH_TEST off, SCISSOR_TEST ENABLED, cull off, blend off
 *   - texture exactly like Upload32 with r_texturemode
 *     GL_LINEAR_MIPMAP_NEAREST: full software mip chain, MIN
 *     LINEAR_MIPMAP_NEAREST, MAG LINEAR, wrap REPEAT
 *   - per frame exactly like RB_SetGL2D + RB_StretchPic +
 *     R_DrawElements: viewport+scissor(0,0,W,H), ortho(0,W,H,0,0,1),
 *     blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA, 4-vertex pixel-space quad,
 *     6 indexes {3,0,2, 2,0,1}, glDrawElements(GL_TRIANGLES,
 *     GL_UNSIGNED_SHORT), optional CVA lock/unlock (--cva), present
 *     via mglSwitchDisplay every frame
 *
 * The image is a 256x256 RGBA raw file (DH2:wosbuild/q3bg.rgba) with a
 * green/magenta frame baked in, so a correct present is unmistakable.
 *
 * Build: manual link line identical to texfmt_probe (startupwos.o
 * FIRST, then the object, then the DLL-name override object, then the
 * import archive). Deploy as q3bg (<=16 chars).
 */

#include <proto/minigl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMG_W 256
#define IMG_H 256
#define IMG_BYTES (IMG_W * IMG_H * 4)

static int g_winW = 320, g_winH = 240;   /* r_mode 0 */
static int g_depth = 32;                 /* r_colorbits */
static int g_cva = 0;                    /* Q3 compiled vertex arrays */
static int g_frames = 600;               /* menu-loop frames */
static int g_sync = -1;                  /* -1 = never call mglEnableSync;
                                          * Q3 calls mglEnableSync(FALSE) at
                                          * init ("sync disabled") when
                                          * r_finish is 0 -- its default */
static int g_drawbuf = 0;                /* 1 = glDrawBuffer(GL_BACK) like
                                          * Q3's GL_SetDefaultState */
static int g_input = 0;                  /* 1 = Q3's input-window attach:
                                          * mglGetInputWindowHandle,
                                          * ModifyIDCMP, REPORTMOUSE, and
                                          * a per-frame IDCMP drain */
static int g_stucktest = 0;              /* 1 = present a solid BLUE frame
                                          * first (2 s), then the image:
                                          * if the window stays blue, later
                                          * mglSwitchDisplay calls never
                                          * update it (frozen-first-present) */
static struct Window *g_win;             /* MiniGL input window (Q3 keeps
                                          * this for the per-frame pump) */
struct IntuitionBase *IntuitionBase;     /* definition (proto declares it
                                          * extern); opened for ModifyIDCMP */

static void LogLine(const char *line)
{
    FILE *f = fopen("DH2:wosbuild/q3bg.log", "a");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

/* Box-filter mip chain, exactly the shape Upload32 feeds glTexImage2D */
static GLboolean BuildMips(const UBYTE *img)
{
    static UBYTE work[IMG_W * IMG_H * 4];
    static UBYTE dst[IMG_W * IMG_H * 4];
    int w = IMG_W, h = IMG_H, level = 0;

    memcpy(work, img, sizeof(work));
    for (;;) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, work);
        if (glGetError() != GL_NO_ERROR)
            return GL_FALSE;
        if (w == 1 && h == 1)
            break;
        {
            int dw = w > 1 ? w / 2 : 1;
            int dh = h > 1 ? h / 2 : 1;
            int x, y, c;

            for (y = 0; y < dh; ++y)
                for (x = 0; x < dw; ++x)
                    for (c = 0; c < 4; ++c) {
                        int x0 = x * 2 < w ? x * 2 : w - 1;
                        int x1 = x * 2 + 1 < w ? x * 2 + 1 : w - 1;
                        int y0 = y * 2 < h ? y * 2 : h - 1;
                        int y1 = y * 2 + 1 < h ? y * 2 + 1 : h - 1;

                        dst[((y * dw) + x) * 4 + c] =
                            (UBYTE)((work[((y0 * w) + x0) * 4 + c] +
                                     work[((y0 * w) + x1) * 4 + c] +
                                     work[((y1 * w) + x0) * 4 + c] +
                                     work[((y1 * w) + x1) * 4 + c]) / 4);
                    }
            memcpy(work, dst, (size_t)dw * dh * 4);
        }
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        ++level;
    }
    return GL_TRUE;
}

int main(int argc, char **argv)
{
    static UBYTE img[IMG_BYTES];
    static float verts[4][3];
    static float tuv[4][2];
    static UBYTE cols[4][4];
    static const GLushort idx[6] = { 3, 0, 2, 2, 0, 1 };
    GLuint name = 0;
    int opened = 0, live = 0;
    int i, f;
    FILE *fimg;
    char line[96];

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--width") && i + 1 < argc)
            g_winW = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc)
            g_winH = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--depth") && i + 1 < argc)
            g_depth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cva"))
            g_cva = 1;
        else if (!strcmp(argv[i], "--sync") && i + 1 < argc)
            g_sync = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--drawbuf"))
            g_drawbuf = 1;
        else if (!strcmp(argv[i], "--input"))
            g_input = 1;
        else if (!strcmp(argv[i], "--stucktest"))
            g_stucktest = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            g_frames = atoi(argv[++i]);
        else {
            printf("usage: %s [--width W] [--height H] [--depth D] "
                   "[--cva] [--frames N]\n", argv[0]);
            return 10;
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    LogLine("BEGIN");
    printf("Q3BG BEGIN %dx%dx%d cva=%d frames=%d\n",
           g_winW, g_winH, g_depth, g_cva, g_frames);

    fimg = fopen("DH2:wosbuild/q3bg.rgba", "rb");
    if (!fimg || fread(img, 1, IMG_BYTES, fimg) != IMG_BYTES) {
        LogLine("image-FAIL");
        puts("Q3BG FAIL: cannot read DH2:wosbuild/q3bg.rgba");
        if (fimg) fclose(fimg);
        return 20;
    }
    fclose(fimg);

    /* ---- context setup: amiga_glimp.c GLimp_Init order ---- */
    if (MiniGLOpen() == 0) {
        LogLine("open-FAIL");
        puts("Q3BG FAIL: MiniGLOpen");
        return 20;
    }
    opened = 1;
    LogLine("open-ok");
    mglChoosePixelDepth(g_depth);
    mglChooseWindowMode(GL_TRUE);        /* windowed forces 2 buffers */
    mglChooseNumberOfBuffers(2);
    mglChooseGuardBand(GL_FALSE);
    mglChooseVertexBufferSize(4096);
    if (mglCreateContext(0, 0, g_winW, g_winH) == 0) {
        LogLine("context-FAIL");
        puts("Q3BG FAIL: mglCreateContext");
        goto cleanup;
    }
    live = 1;
    LogLine("context-ok");
    printf("Q3BG context ok %dx%dx%d buffers=2\n", g_winW, g_winH, g_depth);
    if (g_sync >= 0) {
        mglEnableSync(g_sync ? GL_TRUE : GL_FALSE);
        printf("Q3BG mglEnableSync(%d)\n", g_sync);
        sprintf(line, "enable-sync=%d", g_sync);
        LogLine(line);
    }
    if (g_drawbuf) {
        glDrawBuffer(GL_BACK);   /* Q3's GL_SetDefaultState, combined with
                                  * the windowed 2-buffer context above */
        puts("Q3BG glDrawBuffer(BACK)");
        LogLine("drawbuffer=back");
    }
    if (g_input) {
        /* Q3's amiga_glimp.c input attach, 1:1 */
        struct Window *win;

        IntuitionBase = (struct IntuitionBase *)
            OpenLibrary("intuition.library", 39);
        win = mglGetInputWindowHandle();
        if (!win)
            win = mglGetWindowHandle();
        if (!win || !IntuitionBase) {
            LogLine("inputwindow-FAIL");
            puts("Q3BG FAIL: MiniGL returned no input window");
            goto cleanup;
        }
        ModifyIDCMP(win, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS |
                         IDCMP_MOUSEMOVE | IDCMP_DELTAMOVE);
        win->Flags |= WFLG_REPORTMOUSE;
        g_win = win;
        puts("Q3BG input window attached");
        LogLine("input-ok");
    }

    /* ---- GL_SetDefaultState replica ---- */
    glClearDepth(1.0f);
    glCullFace(GL_FRONT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glShadeModel(GL_SMOOTH);
    glDepthFunc(GL_LEQUAL);
    glEnableClientState(GL_VERTEX_ARRAY);   /* always on in Q3 */
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);              /* Q3 default: scissor ON */
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    /* ---- texture: Upload32 replica (full mip chain, Q3 filters) --- */
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!BuildMips(img)) {
        LogLine("mips-FAIL");
        puts("Q3BG FAIL: mip chain");
        goto cleanup;
    }
    LogLine("mips-ok");

    verts[0][0] = 0.0f;          verts[0][1] = 0.0f;
    verts[1][0] = (float)g_winW; verts[1][1] = 0.0f;
    verts[2][0] = (float)g_winW; verts[2][1] = (float)g_winH;
    verts[3][0] = 0.0f;          verts[3][1] = (float)g_winH;
    verts[0][2] = verts[1][2] = verts[2][2] = verts[3][2] = 0.0f;
    tuv[0][0] = 0.0f; tuv[0][1] = 0.0f;
    tuv[1][0] = 1.0f; tuv[1][1] = 0.0f;
    tuv[2][0] = 1.0f; tuv[2][1] = 1.0f;
    tuv[3][0] = 0.0f; tuv[3][1] = 1.0f;
    for (i = 0; i < 4; ++i) {
        cols[i][0] = 255; cols[i][1] = 255;
        cols[i][2] = 255; cols[i][3] = 255;
    }

    /* ---- menu loop: RB_SetGL2D + RB_StretchPic + R_DrawElements ---- */
    if (g_stucktest) {
        /* frame 0: solid BLUE, presented alone for 2 s. Every later
         * frame is the image. If the window never shows the image,
         * windowed mglSwitchDisplay only takes effect once. */
        glClearColor(0.0f, 0.0f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        mglSwitchDisplay();
        puts("Q3BG stucktest: BLUE presented, holding 2s");
        Delay(120);
    }
    for (f = 0; f < g_frames; ++f) {
        if (g_input) {
            /* Q3's Q3_PPC_DIRECT_EVENTPUMP: drain + reply the window's
             * IDCMP messages every frame */
            for (;;) {
                struct IntuiMessage *msg =
                    (struct IntuiMessage *)GetMsg(g_win->UserPort);
                if (!msg)
                    break;
                ReplyMsg((struct Message *)msg);
            }
        }
        glViewport(0, 0, g_winW, g_winH);
        glScissor(0, 0, g_winW, g_winH);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (GLdouble)g_winW, (GLdouble)g_winH, 0.0, 0.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, name);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glVertexPointer(3, GL_FLOAT, 0, verts);
        glTexCoordPointer(2, GL_FLOAT, 0, tuv);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, cols);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        if (g_cva)
            glLockArrays(0, 4);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
        if (g_cva)
            glUnlockArrays();
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        mglSwitchDisplay();
        if (f == 0) {
            LogLine("frame0-presented");
            puts("Q3BG frame 0 presented");
        }
    }
    sprintf(line, "loop-done frames=%d", g_frames);
    LogLine(line);
    printf("Q3BG loop done frames=%d, holding present 30s\n", g_frames);
    Delay(1800);   /* hold the final present for the operator/screenshot */

cleanup:
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    if (live)
        mglDeleteContext();
    if (opened)
        MiniGLClose();
    LogLine("cleanup-done");
    printf("Q3BG DONE\n");
    return 0;
}
