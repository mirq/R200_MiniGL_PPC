/*
 * GLUT for every PPC DLL variant (upstream minigl.library 29.1 slots
 * 179..200): a single-window GLUT on top of the MiniGL context and main
 * loop, written against the GLUT 3 specification.
 *
 * The callbacks are client PPC code called from the DLL's PPC main loop, so
 * unlike a 68k library calling into PPC there is no CPU switch involved.
 * glutMainLoop installs an idle adapter in MGLMainLoop: the reshape callback
 * runs once before the first display (GLUT 3 7.1), the display callback
 * whenever a redisplay is pending (initially, and after glutPostRedisplay),
 * then the client's idle callback. Keys arrive through mglKeyFunc with no
 * pointer position, so x and y are 0. One window: MiniGL has one current
 * context, and the window size is fixed (mglResizeContext is a no-op), so
 * the reshape callback only ever sees the creation size.
 */
#define MINIGL_LIBRARY_BUILD 1
#include "minigl_ppc_generic.h"
#include <math.h>

#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#ifdef WARPUP
#include <powerpc/powerpc_protos.h>
#pragma pack(pop)
#endif

#define T (*MGLPPCSelfTable)
#define CTX (*MGLPPCSelfTable->currentContext)

static struct {
    int initWidth, initHeight, initX, initY;
    unsigned int displayMode;
    int width, height;          /* of the open window or game mode */
    int gameWidth, gameHeight, gameDepth, gameRate;
    BOOL gameActive, reshapePending, redisplayPending;
    void (*display)(void);
    void (*idle)(void);
    void (*keyboard)(unsigned char key, int x, int y);
    void (*reshape)(int width, int height);
    struct timeval start;
} Glut = { 300, 300, -1, -1, GLUT_RGBA | GLUT_SINGLE, 0, 0,
           640, 480, 16, 0, FALSE, FALSE, FALSE,
           0, 0, 0, 0, { 0, 0 } };

static void GlutNow(struct timeval *now)
{
#ifdef WARPUP
    GetSysTimePPC(now);
#else
    now->tv_secs = now->tv_micro = 0;
#endif
}

void MGLGeneric_GLUTInit(int *argcp, char **argv)
{
    (void)argcp; (void)argv;    /* no X11 command-line options to strip */
    GlutNow(&Glut.start);
}

void MGLGeneric_GLUTInitDisplayMode(unsigned int mode) { Glut.displayMode = mode; }

void MGLGeneric_GLUTInitWindowSize(int width, int height)
{
    if (width > 0 && height > 0) {
        Glut.initWidth = width;
        Glut.initHeight = height;
    }
}

void MGLGeneric_GLUTInitWindowPosition(int x, int y)
{
    Glut.initX = x;
    Glut.initY = y;
}

/* Opens the one context and makes it current for every gl* wrapper. */
static int GlutOpen(GLboolean window, int x, int y, int width, int height)
{
    void *context;
    if (CTX)
        return 0;               /* one window */
    T.mglChooseWindowMode(window);
    if (!window)
        T.mglChoosePixelDepth(Glut.gameDepth);
    context = T.MGLCreateContext(x < 0 ? 0 : x, y < 0 ? 0 : y, width, height);
    if (!context)
        return 0;
    CTX = (GLcontext)context;
    Glut.width = width;
    Glut.height = height;
    Glut.reshapePending = TRUE;
    Glut.redisplayPending = TRUE;
    return 1;
}

int MGLGeneric_GLUTCreateWindow(const char *title)
{
    (void)title;                /* MiniGL windows carry no title */
    return GlutOpen(GL_TRUE, Glut.initX, Glut.initY, Glut.initWidth,
                    Glut.initHeight);
}

static void GlutIdle(void)
{
    if (Glut.reshapePending) {
        Glut.reshapePending = FALSE;
        if (Glut.reshape)
            Glut.reshape(Glut.width, Glut.height);
        else if (CTX)
            T.GLViewport(CTX, 0, 0, Glut.width, Glut.height);
    }
    if (Glut.redisplayPending && Glut.display) {
        Glut.redisplayPending = FALSE;
        Glut.display();
    }
    if (Glut.idle)
        Glut.idle();
}

static void GlutKey(char key)
{
    if (Glut.keyboard)
        Glut.keyboard((unsigned char)key, 0, 0);
}

void MGLGeneric_GLUTMainLoop(void)
{
    GLcontext context = CTX;
    if (!context)
        return;
    T.MGLIdleFunc(context, GlutIdle);
    T.MGLKeyFunc(context, (KeyHandlerFn)GlutKey);
    /* Returns when the client calls glutLeaveMainLoop (mglExit) or the
     * MiniGL loop ends; GLUT 3 says it never returns, freeglut that it may. */
    T.MGLMainLoop(context);
}

void MGLGeneric_GLUTDisplayFunc(void (*func)(void)) { Glut.display = func; }
void MGLGeneric_GLUTIdleFunc(void (*func)(void)) { Glut.idle = func; }
void MGLGeneric_GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y)) { Glut.keyboard = func; }
void MGLGeneric_GLUTReshapeFunc(void (*func)(int width, int height))
{
    Glut.reshape = func;
    if (CTX)
        Glut.reshapePending = TRUE;
}

void MGLGeneric_GLUTSwapBuffers(void)
{
    if (CTX)
        T.MGLSwitchDisplay(CTX);
}

void MGLGeneric_GLUTPostRedisplay(void) { Glut.redisplayPending = TRUE; }

int MGLGeneric_GLUTGet(GLenum state)
{
    struct timeval now;
    switch (state) {
    case GLUT_WINDOW_X: return Glut.initX < 0 ? 0 : Glut.initX;
    case GLUT_WINDOW_Y: return Glut.initY < 0 ? 0 : Glut.initY;
    case GLUT_WINDOW_WIDTH: return CTX ? Glut.width : 0;
    case GLUT_WINDOW_HEIGHT: return CTX ? Glut.height : 0;
    case GLUT_WINDOW_DOUBLEBUFFER: return (Glut.displayMode & GLUT_DOUBLE) != 0;
    case GLUT_WINDOW_RGBA: return 1;
    case GLUT_WINDOW_DEPTH_SIZE: return 16;
    case GLUT_WINDOW_PARENT: case GLUT_WINDOW_NUM_CHILDREN: return 0;
    case GLUT_WINDOW_CURSOR: return GLUT_CURSOR_INHERIT;
    case GLUT_SCREEN_WIDTH: return CTX ? Glut.width : Glut.initWidth;
    case GLUT_SCREEN_HEIGHT: return CTX ? Glut.height : Glut.initHeight;
    case GLUT_INIT_WINDOW_X: return Glut.initX;
    case GLUT_INIT_WINDOW_Y: return Glut.initY;
    case GLUT_INIT_WINDOW_WIDTH: return Glut.initWidth;
    case GLUT_INIT_WINDOW_HEIGHT: return Glut.initHeight;
    case GLUT_INIT_DISPLAY_MODE: return (int)Glut.displayMode;
    case GLUT_DISPLAY_MODE_POSSIBLE:
        return (Glut.displayMode & (GLUT_INDEX | GLUT_STEREO |
                                    GLUT_MULTISAMPLE | GLUT_ACCUM)) == 0;
    case GLUT_ELAPSED_TIME:
        GlutNow(&now);
        return (int)((now.tv_secs - Glut.start.tv_secs) * 1000UL +
                     (now.tv_micro / 1000UL) - (Glut.start.tv_micro / 1000UL));
    default:
        return 0;
    }
}

/* "WxH:bpp@hz", every part optional (GLUT 3 game mode string). */
void MGLGeneric_GLUTGameModeString(const char *string)
{
    int value = 0, field = 0;
    const char *p;
    if (!string)
        return;
    for (p = string; ; ++p) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            continue;
        }
        if (value) {
            if (field == 0) Glut.gameWidth = value;
            else if (field == 1) Glut.gameHeight = value;
            else if (field == 2) Glut.gameDepth = value;
            else if (field == 3) Glut.gameRate = value;
        }
        value = 0;
        if (*p == 'x') field = 1;
        else if (*p == ':') field = 2;
        else if (*p == '@') field = 3;
        else if (!*p) break;
    }
}

int MGLGeneric_GLUTEnterGameMode(void)
{
    if (Glut.gameActive)
        return 1;
    if (!GlutOpen(GL_FALSE, 0, 0, Glut.gameWidth, Glut.gameHeight))
        return 0;
    Glut.gameActive = TRUE;
    return 1;
}

void MGLGeneric_GLUTLeaveGameMode(void)
{
    if (!Glut.gameActive || !CTX)
        return;
    T.MGLDeleteContext(CTX);
    CTX = (GLcontext)0;
    Glut.gameActive = FALSE;
}

int MGLGeneric_GLUTGameModeGet(GLenum query)
{
    switch (query) {
    case GLUT_GAME_MODE_ACTIVE: return Glut.gameActive;
    case GLUT_GAME_MODE_POSSIBLE: return 1;
    case GLUT_GAME_MODE_WIDTH: return Glut.gameWidth;
    case GLUT_GAME_MODE_HEIGHT: return Glut.gameHeight;
    case GLUT_GAME_MODE_PIXEL_DEPTH: return Glut.gameDepth;
    case GLUT_GAME_MODE_REFRESH_RATE: return Glut.gameRate;
    case GLUT_GAME_MODE_DISPLAY_CHANGED: return Glut.gameActive;
    default: return 0;
    }
}

/* ---- solid shapes ----------------------------------------------------- */

static GLUquadricObj *GlutQuadric(void)
{
    static GLUquadricObj *quadric;
    if (!quadric)
        quadric = MGLGeneric_GLUNewQuadric();   /* smooth, filled, outside */
    return quadric;
}

void MGLGeneric_GLUTSolidCube(GLdouble size)
{
    static const GLfloat n[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
        { 0, 0, -1 } };
    /* Corners of each face, counter-clockwise seen from outside. */
    static const signed char f[6][4][3] = {
        { { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { 1, -1, 1 } },
        { { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, -1 } },
        { { -1, 1, -1 }, { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 } },
        { { -1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 } },
        { { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
        { { -1, 1, -1 }, { 1, 1, -1 }, { 1, -1, -1 }, { -1, -1, -1 } } };
    GLcontext c = CTX;
    GLfloat h = (GLfloat)(size * 0.5);
    int i, k;
    if (!c) return;
    T.GLBegin(c, GL_QUADS);
    for (i = 0; i < 6; ++i) {
        T.GLNormal3f(c, n[i][0], n[i][1], n[i][2]);
        for (k = 0; k < 4; ++k)
            T.GLVertex4f(c, h * f[i][k][0], h * f[i][k][1], h * f[i][k][2], 1.0f);
    }
    T.GLEnd(c);
}

void MGLGeneric_GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks)
{
    GLUquadricObj *q = GlutQuadric();
    if (q)
        MGLGeneric_GLUSphere(q, radius, slices, stacks);
}

/* Base disk at z = 0 facing -z, apex at z = height (GLUT 3 11.6). */
void MGLGeneric_GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks)
{
    GLUquadricObj *q = GlutQuadric();
    if (!q) return;
    MGLGeneric_GLUCylinder(q, base, 0.0, height, slices, stacks);
    MGLGeneric_GLUQuadricOrientation(q, GLU_INSIDE);
    MGLGeneric_GLUDisk(q, 0.0, base, slices, 1);
    MGLGeneric_GLUQuadricOrientation(q, GLU_OUTSIDE);
}

/* Around the z axis; the tube circles the ring (GLUT 3 11.7). */
void MGLGeneric_GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings)
{
    GLcontext c = CTX;
    int i, j;
    if (!c || sides < 3 || rings < 3) return;
    for (i = 0; i < rings; ++i) {
        GLdouble t0 = 2.0 * 3.14159265358979 * i / rings;
        GLdouble t1 = 2.0 * 3.14159265358979 * (i + 1) / rings;
        T.GLBegin(c, GL_QUAD_STRIP);
        for (j = 0; j <= sides; ++j) {
            GLdouble p = 2.0 * 3.14159265358979 * (j == sides ? 0 : j) / sides;
            GLdouble cp = cos(p), sp = sin(p);
            GLdouble r = outerRadius + innerRadius * cp;
            /* Ring t0 then t1 per column: +theta then +phi is
             * counter-clockwise seen from outside the tube. */
            T.GLNormal3f(c, (GLfloat)(cos(t0) * cp), (GLfloat)(sin(t0) * cp), (GLfloat)sp);
            T.GLVertex4f(c, (GLfloat)(cos(t0) * r), (GLfloat)(sin(t0) * r),
                         (GLfloat)(innerRadius * sp), 1.0f);
            T.GLNormal3f(c, (GLfloat)(cos(t1) * cp), (GLfloat)(sin(t1) * cp), (GLfloat)sp);
            T.GLVertex4f(c, (GLfloat)(cos(t1) * r), (GLfloat)(sin(t1) * r),
                         (GLfloat)(innerRadius * sp), 1.0f);
        }
        T.GLEnd(c);
    }
}

/*
 * Regular dodecahedron with circumradius sqrt(3) (GLUT 3 11.8): vertices
 * (+-1, +-1, +-1) and the cyclic permutations of (0, +-1/phi, +-phi). Each
 * face is the five vertices nearest one icosahedron direction (the cyclic
 * permutations of (0, +-1, +-phi)), sorted counter-clockwise around it.
 */
void MGLGeneric_GLUTSolidDodecahedron(void)
{
    static GLfloat v[20][3], n[12][3];
    static int face[12][5], ready;
    GLcontext c = CTX;
    int i, k;
    if (!c) return;
    if (!ready) {
        const GLfloat phi = 1.6180339887f, iphi = 0.6180339887f;
        int count = 0;
        for (i = 0; i < 8; ++i) {
            v[count][0] = (i & 1) ? 1.0f : -1.0f;
            v[count][1] = (i & 2) ? 1.0f : -1.0f;
            v[count][2] = (i & 4) ? 1.0f : -1.0f;
            ++count;
        }
        for (i = 0; i < 4; ++i) {
            GLfloat a = (i & 1) ? iphi : -iphi, b = (i & 2) ? phi : -phi;
            v[count][0] = 0; v[count][1] = a; v[count][2] = b; ++count;
            v[count][0] = a; v[count][1] = b; v[count][2] = 0; ++count;
            v[count][0] = b; v[count][1] = 0; v[count][2] = a; ++count;
        }
        for (i = 0, count = 0; i < 4; ++i) {
            GLfloat a = (i & 1) ? 1.0f : -1.0f, b = (i & 2) ? phi : -phi;
            GLfloat len = (GLfloat)sqrt(1.0 + phi * phi);
            n[count][0] = 0; n[count][1] = a / len; n[count][2] = b / len; ++count;
            n[count][0] = a / len; n[count][1] = b / len; n[count][2] = 0; ++count;
            n[count][0] = b / len; n[count][1] = 0; n[count][2] = a / len; ++count;
        }
        for (i = 0; i < 12; ++i) {
            GLfloat d[20], u[3], w[3], ang[5];
            int sel[5], s, j;
            /* The five vertices with the largest projection on n[i]. */
            for (j = 0; j < 20; ++j)
                d[j] = v[j][0] * n[i][0] + v[j][1] * n[i][1] + v[j][2] * n[i][2];
            for (s = 0; s < 5; ++s) {
                int best = -1;
                for (j = 0; j < 20; ++j) {
                    int used = 0, t;
                    for (t = 0; t < s; ++t) used |= sel[t] == j;
                    if (!used && (best < 0 || d[j] > d[best])) best = j;
                }
                sel[s] = best;
            }
            /* Basis (u, w) of the face plane with u x w = n. */
            u[0] = v[sel[0]][0] - d[sel[0]] * n[i][0];
            u[1] = v[sel[0]][1] - d[sel[0]] * n[i][1];
            u[2] = v[sel[0]][2] - d[sel[0]] * n[i][2];
            w[0] = n[i][1] * u[2] - n[i][2] * u[1];
            w[1] = n[i][2] * u[0] - n[i][0] * u[2];
            w[2] = n[i][0] * u[1] - n[i][1] * u[0];
            for (s = 0; s < 5; ++s) {
                const GLfloat *p = v[sel[s]];
                ang[s] = (GLfloat)atan2(p[0] * w[0] + p[1] * w[1] + p[2] * w[2],
                                        p[0] * u[0] + p[1] * u[1] + p[2] * u[2]);
            }
            for (s = 0; s < 5; ++s) {
                int best = s, t;
                for (t = s + 1; t < 5; ++t) if (ang[t] < ang[best]) best = t;
                { GLfloat ta = ang[s]; ang[s] = ang[best]; ang[best] = ta; }
                { int ts = sel[s]; sel[s] = sel[best]; sel[best] = ts; }
                face[i][s] = sel[s];
            }
        }
        ready = 1;
    }
    for (i = 0; i < 12; ++i) {
        T.GLBegin(c, GL_POLYGON);
        T.GLNormal3f(c, n[i][0], n[i][1], n[i][2]);
        for (k = 0; k < 5; ++k)
            T.GLVertex4f(c, v[face[i][k]][0], v[face[i][k]][1], v[face[i][k]][2], 1.0f);
        T.GLEnd(c);
    }
}
