/*
 * GLUT smoke test for the ABI-5 DLLs: one window, the GLUT solid shapes
 * (cube, sphere, cone, torus, dodecahedron -- the last four exercise the GLU
 * quadrics and polygon paths) under one light, the static ones compiled into
 * a display list, rotating until -frames N frames have been shown.
 *
 * usage: glut_shapes [-frames N]      (default 300)
 * Prints "GLUT_RUN frames=N ms=M error=0x...." and exits through
 * glutLeaveMainLoop.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/minigl.h>

static int frames, limit = 300, startMs;
static GLuint shapesList;
static GLfloat angle;

static void Reshape(int width, int height)
{
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (GLfloat)width / (GLfloat)(height ? height : 1),
                   1.0f, 50.0f);
    glMatrixMode(GL_MODELVIEW);
}

static void Display(void)
{
    static const GLfloat position[4] = { 2.0f, 4.0f, 6.0f, 0.0f };
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -9.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glRotatef(angle, 0.3f, 1.0f, 0.0f);
    glCallList(shapesList);
    glPushMatrix();
    glTranslatef(0.0f, -1.8f, 0.0f);
    glRotatef(-angle * 2.0f, 0.0f, 1.0f, 0.0f);
    glColor4f(0.9f, 0.8f, 0.2f, 1.0f);
    glutSolidTorus(0.25, 0.8, 12, 24);
    glPopMatrix();
    glutSwapBuffers();
}

static void Idle(void)
{
    angle += 2.0f;
    if (++frames >= limit) {
        int ms = glutGet(GLUT_ELAPSED_TIME) - startMs;
        printf("GLUT_RUN frames=%d ms=%d error=0x%04x\n", frames, ms,
               (unsigned)glGetError());
        glutLeaveMainLoop();
        return;
    }
    glutPostRedisplay();
}

static void Keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 27 || key == 'q')
        glutLeaveMainLoop();
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "-frames") == 0)
            limit = atoi(argv[++i]);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!MiniGLOpen()) {
        puts("glut_shapes: open failed");
        return 20;
    }
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(640, 480);
    if (!glutCreateWindow("glut_shapes")) {
        puts("glut_shapes: create failed");
        MiniGLClose();
        return 20;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    shapesList = glGenLists(1);
    glNewList(shapesList, GL_COMPILE);
    glPushMatrix();
    glTranslatef(-2.2f, 0.8f, 0.0f);
    glColor4f(0.8f, 0.2f, 0.2f, 1.0f);
    glutSolidCube(1.2);
    glTranslatef(2.2f, 0.0f, 0.0f);
    glColor4f(0.2f, 0.8f, 0.3f, 1.0f);
    glutSolidSphere(0.75, 20, 14);
    glTranslatef(2.2f, -0.6f, 0.0f);
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    glColor4f(0.3f, 0.4f, 0.9f, 1.0f);
    glutSolidCone(0.7, 1.4, 16, 4);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -2.5f);
    glScalef(0.5f, 0.5f, 0.5f);
    glColor4f(0.8f, 0.5f, 0.9f, 1.0f);
    glutSolidDodecahedron();
    glPopMatrix();
    glEndList();

    glutDisplayFunc(Display);
    glutReshapeFunc(Reshape);
    glutIdleFunc(Idle);
    glutKeyboardFunc(Keyboard);
    startMs = glutGet(GLUT_ELAPSED_TIME);
    glutMainLoop();

    glDeleteLists(shapesList, 1);
    if (*MiniGLDispatch->currentContext)
        mglDeleteContext();
    MiniGLClose();
    return 0;
}
