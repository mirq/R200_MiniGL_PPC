/* Public ABI-5 PPC DLL bind benchmark: [count=256] [same|alternate].
 * Build: make -f Makefile.import.gcc bin/examples/r200_bind_bench_ppc.warpelf
 * Requires a matched R200 stack with public RGBA ReadPixels support.
 * Baseline sends N binds in either mode; redundant-bind elision should send
 * none for same, N for alternate. This client does not measure transport counts.
 * DateStamp ticks are 20ms; zero is sub-tick, not an infinite rate.
 * BIND_RUN ticks=-1 means setup failed before timing.
 */
#include <proto/minigl.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    static const GLubyte colors[2][4] = {
        {255, 0, 0, 255}, {0, 255, 0, 255}
    };
    GLuint textures[2] = {0, 0};
    GLubyte image[16], pixel[4] = {0, 0, 0, 0};
    const GLubyte *expected;
    struct DateStamp start, end;
    LONG count = 256, ticks = -1, i, j;
    GLenum error = GL_NO_ERROR;
    int alternate = 0, opened = 0, live = 0, pixelpass = 0;
    char *rest;

    if (argc > 3) goto badargs;
    if (argc > 1) {
        count = strtol(argv[1], &rest, 10);
        if (argv[1][0] < '0' || argv[1][0] > '9' || *rest ||
            count < 1 || count > 4096) goto badargs;
    }
    if (argc > 2) {
        if (!strcmp(argv[2], "alternate")) alternate = 1;
        else if (strcmp(argv[2], "same")) goto badargs;
    }

    if (!MiniGLOpen()) {
        puts("BIND_SETUP MiniGLOpen failed");
        goto cleanup;
    }
    opened = 1;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(32);
    mglChooseTextureBufferSize(4096);
    if (!mglCreateContext(0, 0, 320, 240)) {
        puts("BIND_SETUP windowed 320x240x32 context failed");
        goto cleanup;
    }
    live = 1;
    glGenTextures(2, textures);
    error = glGetError();
    if (error != GL_NO_ERROR || !textures[0] || !textures[1] ||
        textures[0] == textures[1]) {
        puts("BIND_SETUP texture creation failed");
        goto cleanup;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 4; ++j) memcpy(image + j * 4, colors[i], 4);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, image);
    }
    glViewport(0, 0, 320, 240);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);
    glClearColor(0, 0, 0, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    /* Prime red and drain all setup. Alternate's first bind must change it. */
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glFinish();
    error = glGetError();
    if (error != GL_NO_ERROR) {
        puts("BIND_SETUP GL setup/drain failed");
        goto cleanup;
    }

    DateStamp(&start);
    for (i = 0; i < count; ++i)
        glBindTexture(GL_TEXTURE_2D, textures[alternate ? ((i + 1) & 1) : 0]);
    glFinish();
    DateStamp(&end);
    ticks = ((end.ds_Days - start.ds_Days) * 1440L +
             end.ds_Minute - start.ds_Minute) * 60L * TICKS_PER_SECOND +
            end.ds_Tick - start.ds_Tick;

    /* No rebind: same ends red; alternate ends green for odd N, red for even. */
    expected = colors[alternate ? (count & 1) : 0];
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-1, -1, 0);
    glTexCoord2f(1, 0); glVertex3f(1, -1, 0);
    glTexCoord2f(1, 1); glVertex3f(1, 1, 0);
    glTexCoord2f(0, 1); glVertex3f(-1, 1, 0);
    glEnd();
    glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    error = glGetError();
    pixelpass = memcmp(pixel, expected, sizeof(pixel)) == 0;
    if (!pixelpass)
        printf("BIND_PIXEL actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
               (unsigned)pixel[0], (unsigned)pixel[1],
               (unsigned)pixel[2], (unsigned)pixel[3],
               (unsigned)expected[0], (unsigned)expected[1],
               (unsigned)expected[2], (unsigned)expected[3]);

cleanup:
    if (live) {
        glDeleteTextures(2, textures);
        mglDeleteContext();
    }
    if (opened) MiniGLClose();
    printf("BIND_RUN mode=%s count=%ld ticks=%ld error=0x%04lx pixelpass=%d\n",
           alternate ? "alternate" : "same", count, ticks, (ULONG)error, pixelpass);
    if (ticks == 0) puts("BIND_TIME ticks=0 means <20ms; no rate inferred.");
    return ticks >= 0 && error == GL_NO_ERROR && pixelpass ? 0 : 20;

badargs:
    puts("Usage: r200_bind_bench [count:1..4096 (default 256)] [same|alternate]");
    return 20;
}
