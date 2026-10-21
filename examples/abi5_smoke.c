/*
 * ABI 5 smoke test: proves the DLL hands out an upstream-29.1-shaped table and
 * that every slot appended after GLMultiDrawArrays (133..205, plus the
 * FastPath trio) is callable without a crash.
 *
 * It checks the table header and that no slot is NULL, calls each new entry
 * point once with harmless arguments, printing glGetError() after each so a
 * backend's fallbacks can be compared with the documented ones, and checks
 * the behaviour every PPC backend must have: display lists compile without
 * executing under GL_COMPILE and replay on glCallList, GLU quadrics exist,
 * glGetDoublev agrees with glGetFloatv. It does not judge rendering; GLUT's
 * main loop and glutInit (which registers an atexit handler) are not called.
 *
 * usage: abi5_smoke [window|fullscreen]
 */
#include <stdio.h>
#include <string.h>
#include <proto/minigl.h>

static int failures;

static void report(const char *name)
{
    GLenum error = glGetError();
    printf("abi5: %-28s error=0x%04lx\n", name, (unsigned long)error);
}

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("abi5: FAIL %s\n", what);
        ++failures;
    }
}

int main(int argc, char **argv)
{
    const MGLDispatchTable *table;
    void * const *slot;
    ULONG count, i, nulls = 0;
    GLuint texture = 0;
    GLboolean resident = GL_FALSE;
    GLclampf priority = 1.0f;
    GLdouble viewport[4] = { -1.0, -1.0, -1.0, -1.0 };
    GLvoid *pointer = (GLvoid *)1;
    GLfloat value[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    GLfloat normal[3] = { 0.0f, 0.0f, 1.0f };
    GLint first[1] = { 0 }, index = 0;
    GLsizei drawn[1] = { 0 };
    GLboolean edge = GL_TRUE;
    GLUquadricObj *quadric;
    GLuint list;
    GLboolean windowed = GL_TRUE;

    if (argc > 1 && strcmp(argv[1], "fullscreen") == 0)
        windowed = GL_FALSE;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!MiniGLOpen()) {
        puts("abi5: open failed (an ABI-3 DLL refuses this client)");
        return 20;
    }

    table = MiniGLDispatch;
    printf("abi5: abiVersion=%lu structSize=0x%lx client=0x%lx flags=0x%lx\n",
           (unsigned long)table->abiVersion, (unsigned long)table->structSize,
           (unsigned long)sizeof(MGLDispatchTable),
           (unsigned long)table->backendFlags);
    check(table->abiVersion == 5, "abiVersion == 5");
    check(table->structSize >= sizeof(MGLDispatchTable), "structSize");

    /* Every function slot follows the five header words; none may be NULL. */
    slot = (void * const *)&table->GLActiveTextureARB;
    count = (sizeof(MGLDispatchTable) - 20) / sizeof(void *);
    for (i = 0; i < count; ++i)
        if (!slot[i]) {
            printf("abi5: FAIL slot %lu is NULL\n", (unsigned long)(i + 1));
            ++nulls;
        }
    printf("abi5: %lu slots, %lu NULL\n", (unsigned long)count,
           (unsigned long)nulls);
    failures += (int)nulls;

    check(mglCreateContextFromWindow(NULL) == NULL, "FromWindow(NULL) refused");
    check(mglCreateContextFromBitMap(NULL) == NULL, "FromBitMap(NULL) refused");
    mglChooseZBufferDepth(16);

    mglChooseWindowMode(windowed);
    if (!mglCreateContext(0, 0, 640, 480)) {
        puts("abi5: create failed");
        MiniGLClose();
        return 20;
    }
    report("create");

    glBlendEquation(GL_FUNC_ADD);                report("glBlendEquation");
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
                                                 report("glBlendFuncSeparate");
    glGenTextures(1, &texture);
    check(glIsTexture(0) == GL_FALSE, "glIsTexture(0) is false");
                                                 report("glIsTexture");
    glLineWidth(1.0f);                           report("glLineWidth");
    glTexGenfv(GL_S, GL_TEXTURE_GEN_MODE, value); report("glTexGenfv");
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0);
                                                 report("glCopyTexImage2D");
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 8, 8);
                                                 report("glCopyTexSubImage2D");
    glAreTexturesResident(1, &texture, &resident);
                                                 report("glAreTexturesResident");
    glEdgeFlag(GL_TRUE);                         report("glEdgeFlag");
    glEdgeFlagv(&edge);                          report("glEdgeFlagv");
    glEdgeFlagPointer(0, &edge);                 report("glEdgeFlagPointer");
    glGetDoublev(GL_VIEWPORT, viewport);         report("glGetDoublev");
    printf("abi5: viewport %g %g %g %g\n", viewport[0], viewport[1],
           viewport[2], viewport[3]);
    check(viewport[2] == 640.0 && viewport[3] == 480.0,
          "glGetDoublev(GL_VIEWPORT) is the 640x480 viewport");
    glGetPointerv(GL_VERTEX_ARRAY, &pointer);   report("glGetPointerv");
    glIndexi(0);                                 report("glIndexi");
    glIndexiv(&index);                           report("glIndexiv");
    glIndexPointer(GL_FLOAT, 0, value);          report("glIndexPointer");
    glPrioritizeTextures(1, &texture, &priority);
                                                 report("glPrioritizeTextures");
    glReadBuffer(GL_BACK);                       report("glReadBuffer");

    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 0.0f); report("glLightf");
    glLightfv(GL_LIGHT0, GL_DIFFUSE, value);     report("glLightfv");
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 8.0f);
                                                 report("glMaterialf");
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, value);
                                                 report("glMaterialfv");
    glLightModelf(GL_LIGHT_MODEL_TWO_SIDE, 0.0f); report("glLightModelf");
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, value);
                                                 report("glLightModelfv");
    glGetLightfv(GL_LIGHT0, GL_DIFFUSE, value);  report("glGetLightfv");
    glGetMaterialfv(GL_FRONT, GL_AMBIENT, value); report("glGetMaterialfv");
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
                                                 report("glColorMaterial");
    glNormalPointer(GL_FLOAT, 0, normal);        report("glNormalPointer");
    glNormal3fv(normal);                         report("glNormal3fv");
    glNormal3fv(NULL);                           report("glNormal3fv(NULL)");
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, value);
                                                 report("glTexEnvfv");

    list = glGenLists(1);                        report("glGenLists");
    check(list != 0, "glGenLists(1) returns a name");
    check(glIsList(0) == GL_FALSE, "glIsList(0) is false");
    check(!list || glIsList(list) == GL_TRUE, "glIsList(generated)");
                                                 report("glIsList");
    if (list) {
        GLfloat matrix[16];
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glNewList(list, GL_COMPILE);             report("glNewList");
        glTranslatef(5.0f, 0.0f, 0.0f);
        glBegin(GL_TRIANGLES);
        glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
        glVertex3f(0.0f, 0.0f, 0.0f);
        glVertex3f(0.0f, 0.1f, 0.0f);
        glVertex3f(0.1f, 0.0f, 0.0f);
        glEnd();
        glEndList();                             report("glEndList");
        glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
        check(matrix[12] == 0.0f, "GL_COMPILE did not execute");
        glCallList(list);                        report("glCallList");
        glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
        check(matrix[12] == 5.0f, "glCallList replayed the translate");
        glLoadIdentity();
        glDeleteLists(list, 1);                  report("glDeleteLists");
        check(glIsList(list) == GL_FALSE, "deleted list is gone");
    }

    printf("abi5: gluErrorString(GLU_INVALID_ENUM) = %s\n",
           (const char *)gluErrorString(GLU_INVALID_ENUM));
    quadric = gluNewQuadric();
    printf("abi5: gluNewQuadric = %p\n", (void *)quadric);
    check(quadric != NULL, "gluNewQuadric");
    if (quadric) {
        gluQuadricNormals(quadric, GLU_SMOOTH);
        gluQuadricTexture(quadric, GL_FALSE);
        gluQuadricDrawStyle(quadric, GLU_FILL);
        gluQuadricOrientation(quadric, GLU_OUTSIDE);
        gluSphere(quadric, 0.5, 8, 8);
        gluCylinder(quadric, 0.5, 0.25, 1.0, 8, 2);
        gluDisk(quadric, 0.1, 0.5, 8, 1);
        gluDeleteQuadric(quadric);
    }
    report("glu quadrics");
    printf("abi5: gluBuild2DMipmaps on NULL data = %ld\n",
           (long)gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, 0, 0, GL_RGBA,
                                   GL_UNSIGNED_BYTE, NULL));
    printf("abi5: glutGet(GLUT_WINDOW_WIDTH) = %d\n", glutGet(GLUT_WINDOW_WIDTH));

    glMultiDrawArrays(GL_TRIANGLES, first, drawn, 1); report("glMultiDrawArrays");
    printf("abi5: mglGetFastPathCapabilities = 0x%lx\n",
           (unsigned long)mglGetFastPathCapabilities());

    glDeleteTextures(1, &texture);
    mglDeleteContext();
    MiniGLClose();
    printf("abi5: done, %d failure(s)\n", failures);
    return failures ? 20 : 0;
}
