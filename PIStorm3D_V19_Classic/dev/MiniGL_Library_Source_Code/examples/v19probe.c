/*
 * Focused hardware probe for the two remaining v19_parity findings:
 *   A) a unit-0-only GL_POLYGON drawn after mglDrawMultitexBuffer usage
 *      is captured by the mtex queue instead of drawn directly
 *   B) GL_T2F_C3F_V3F interleaved draw produces no visible output
 *
 * Each stage prints CONTENT=yes/no plus any GL error so a single run on
 * the physical Amiga isolates both.
 */

#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static void Report(const char *stage, int content)
{
    GLenum error = glGetError();
    printf("%-34s content=%s error=%ld\n", stage,
           content > 0 ? "yes" : (content < 0 ? "LOCKFAIL" : "no"),
           (long)error);
}

static void MakeTexture(GLuint name, GLubyte r, GLubyte g, GLubyte b)
{
    GLubyte image[4 * 4 * 4];
    int i;
    for (i = 0; i < 4 * 4; ++i) {
        image[i * 4 + 0] = r;
        image[i * 4 + 1] = g;
        image[i * 4 + 2] = b;
        image[i * 4 + 3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, name);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void DrawQuad(GLenum primitive)
{
    glBegin(primitive);
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
}

static int Content(void)
{
    MGLLockInfo info;
    long row;
    int found = 0;
    glFinish();
    if (!mglLockBack(&info))
        return -1;
    for (row = info.height / 4; row <= info.height * 3 / 4 && !found;
         row += info.height / 4) {
        unsigned char *line = (unsigned char *)info.base_address +
                              row * (long)info.pitch;
        int x;
        if (info.depth == 32) {
            /* B8G8R8A8 on big-endian m68k: alpha is the LOW byte. */
            for (x = 0; x < (int)info.width && !found; ++x)
                if ((((unsigned long *)line)[x] & 0xffffff00UL) != 0)
                    found = 1;
        } else if (info.depth == 16) {
            for (x = 0; x < (int)info.width && !found; ++x)
                if (((unsigned short *)line)[x] != 0)
                    found = 1;
        } else if (info.depth == 8) {
            for (x = 0; x < (int)info.width && !found; ++x)
                if (line[x] != 0)
                    found = 1;
        }
    }
    mglUnlockDisplay();
    return found;
}

/* Prints the raw centre pixel so a stale-frame readback can be told apart
 * from a mangled clear colour. */
static void CentrePixel(const char *stage)
{
    MGLLockInfo info;
    unsigned char *line;
    glFinish();
    if (!mglLockBack(&info))
        return;
    line = (unsigned char *)info.base_address +
           (long)(info.height / 2) * (long)info.pitch;
    if (info.depth == 32)
        printf("%-24s centre=0x%08lx\n", stage,
               (unsigned long)((unsigned long *)line)[info.width / 2]);
    else if (info.depth == 16)
        printf("%-24s centre=0x%04x\n", stage,
               (unsigned)((unsigned short *)line)[info.width / 2]);
    else
        printf("%-24s centre=0x%02x\n", stage,
               (unsigned)line[info.width / 2]);
    mglUnlockDisplay();
}

static void Clear(void)
{
    glClear(GL_COLOR_BUFFER_BIT);
    glGetError();
}

int main(int argc, char **argv)
{
    static GLfloat t2fc3f[3 * 8];
    void *context;
    int depth = 16;
    int fullscreen = 0;
    int argument;

    for (argument = 1; argument < argc; ++argument) {
        if (!strcmp(argv[argument], "-depth") && argument + 1 < argc)
            depth = atoi(argv[++argument]);
        else if (!strcmp(argv[argument], "--fullscreen"))
            fullscreen = 1;
    }

    if (!MiniGLOpen()) {
        printf("probe: cannot open minigl.library v5\n");
        return 20;
    }
    mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
    mglChoosePixelDepth(depth);
    context = mglCreateContext(0, 0, 320, 240);
    if (!context) {
        printf("probe: context creation failed\n");
        MiniGLClose();
        return 20;
    }
    MakeTexture(1, 255, 0, 0);
    MakeTexture(2, 0, 255, 0);
    glGetError();

    printf("--- stage A: capture-after-flush ---\n");

    /* A0: bare clear-only submit (no geometry since open). */
    Clear();
    CentrePixel("A0 clear-only pixel");
    Report("A0 clear-only submit", Content());

    /* A1 baseline: unit0-only polygon on a fresh context. */
    Clear();
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    DrawQuad(GL_POLYGON);
    Report("A1 fresh unit0-only polygon", Content());

    /* A2: full mtex setup + queue + flush (as in the parity test). */
    Clear();
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glBindTexture(GL_TEXTURE_2D, 2);
    glEnable(GL_TEXTURE_2D);
    DrawQuad(GL_POLYGON);
    Report("A2 quad queued (expect no)", Content());
    mglDrawMultitexBuffer(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_MODULATE);
    Report("A2 quad flushed (expect yes)", Content());

    /* A3: disable unit 1, draw unit0-only polygon. */
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glGetError();
    Clear();
    DrawQuad(GL_POLYGON);
    Report("A3 after-flush polygon", Content());
    mglDrawMultitexBuffer(GL_ONE, GL_ONE, GL_REPLACE);
    Report("A3 flush-after (captured?)", Content());

    /* A4: same but GL_TRIANGLES instead of GL_POLYGON. */
    Clear();
    DrawQuad(GL_TRIANGLES);
    Report("A4 after-flush triangles", Content());

    /* A5: fresh-enable sanity — re-enable unit1, disable again, draw. */
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_2D);
    glGetError();
    Clear();
    DrawQuad(GL_POLYGON);
    Report("A5 re-disable polygon", Content());
    mglDrawMultitexBuffer(GL_ONE, GL_ONE, GL_REPLACE);
    Report("A5 flush-after (captured?)", Content());

    printf("--- stage B: T2F_C3F_V3F ---\n");

    glDisable(GL_TEXTURE_2D);
    glGetError();
    Clear();
    /* [S T][R G B][X Y Z], stride 32 */
    t2fc3f[0] = 0.5f; t2fc3f[1] = 0.5f;             /* tex 0 */
    t2fc3f[2] = 1.0f; t2fc3f[3] = 1.0f; t2fc3f[4] = 1.0f;  /* color 0 */
    t2fc3f[5] = -0.6f; t2fc3f[6] = -0.5f; t2fc3f[7] = 0.0f; /* vertex 0 */
    t2fc3f[8 + 0] = 0.5f; t2fc3f[8 + 1] = 0.5f;
    t2fc3f[8 + 2] = 1.0f; t2fc3f[8 + 3] = 1.0f; t2fc3f[8 + 4] = 1.0f;
    t2fc3f[8 + 5] = 0.0f; t2fc3f[8 + 6] = 0.6f; t2fc3f[8 + 7] = 0.0f;
    t2fc3f[16 + 0] = 0.5f; t2fc3f[16 + 1] = 0.5f;
    t2fc3f[16 + 2] = 1.0f; t2fc3f[16 + 3] = 1.0f; t2fc3f[16 + 4] = 1.0f;
    t2fc3f[16 + 5] = 0.6f; t2fc3f[16 + 6] = -0.5f; t2fc3f[16 + 7] = 0.0f;

    glInterleavedArrays(GL_T2F_C3F_V3F, 0, t2fc3f);
    Report("B1 interleaved setup", 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    Report("B1 T2F_C3F_V3F draw", Content());
    mglDrawMultitexBuffer(GL_ONE, GL_ONE, GL_REPLACE);
    Report("B1 flush-after (captured?)", Content());

    /* B2: same data through separate pointer calls. */
    Clear();
    glInterleavedArrays(GL_V3F, 0, &t2fc3f[5]);
    glGetError();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    Report("B2 vertex-only draw", Content());

    /* B3: glBegin immediate with same triangle shape. */
    Clear();
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 1.0f, 1.0f);
    glVertex2f(-0.6f, -0.5f);
    glVertex2f(0.0f, 0.6f);
    glVertex2f(0.6f, -0.5f);
    glEnd();
    Report("B3 immediate triangle", Content());

    mglDeleteContext();
    MiniGLClose();
    printf("probe: done\n");
    return 0;
}
