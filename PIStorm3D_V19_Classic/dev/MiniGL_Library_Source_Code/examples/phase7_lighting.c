/*
 * Phase 7: fixed-function lighting on the Radeon9200.chip interface-11
 * hardware-TCL path.
 *
 * Runs in a 320x240 16-bit window and reads the rendered surface back with
 * mglLockBack(). Checks compare sampled pixels (lit vs unlit, lit vs lit,
 * lit vs background) so they do not depend on the surface pixel layout.
 *
 * Geometry: a camera-facing quad at z = -0.5 with normal (0,0,1), so a
 * light shining down -Z hits it at full N dot L. Off-centre quads probe
 * spot cones; depth-separated quads probe range attenuation.
 */
#include <stdio.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static MGLLockInfo lock;
static UWORD background;

#define QUAD_CX 0.0f
#define QUAD_CY 0.0f
#define LEFT_X -0.6f
#define LEFT_Y 0.0f
#define RIGHT_X 0.6f
#define RIGHT_Y 0.0f

static UWORD sample(GLfloat ndcx, GLfloat ndcy)
{
    UBYTE *base = (UBYTE *)lock.base_address;
    ULONG x = (ULONG)((ndcx + 1.0f) * 0.5f * (GLfloat)lock.width);
    ULONG y = (ULONG)((1.0f - ndcy) * 0.5f * (GLfloat)lock.height);

    if (x >= lock.width) x = lock.width - 1UL;
    if (y >= lock.height) y = lock.height - 1UL;
    return *(UWORD *)(base + y * lock.pitch + x * 2UL);
}

static int capture(void)
{
    glFlush();
    return mglLockBack(&lock) ? 0 : 1;
}

static void clear_scene(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* Camera-facing quad centred at ndc (cx, cy), normal +Z toward the eye. */
static void quad_at(GLfloat cx, GLfloat cy, GLfloat nz)
{
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.0f, nz);
    glVertex3f(cx - 0.25f, cy - 0.25f, -0.5f);
    glVertex3f(cx + 0.25f, cy - 0.25f, -0.5f);
    glVertex3f(cx + 0.25f, cy + 0.25f, -0.5f);
    glVertex3f(cx - 0.25f, cy + 0.25f, -0.5f);
    glEnd();
}

static int expect(int condition, const char *what, int code)
{
    if (condition)
        return 0;
    printf("phase7: FAILED %s (code %d)\n", what, code);
    return code;
}

int main(int argc, char **argv)
{
    UWORD unlit, litHeadlight, litSplit, litColourMaterial, litNear, litFar,
          coneIn, coneOut;
    GLint query;
    GLfloat values[4];
    int result;
    (void)argc;

    if (!MiniGLOpen()) { printf("P7 open fail\n"); return 21; }
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240)) {
        printf("P7 ctx fail\n"); MiniGLClose(); return 22;
    }

    clear_scene();
    if (capture()) { printf("P7 first capture fail\n"); result = 23; goto done; }
    if (lock.depth != 16 || !lock.base_address ||
        lock.width < 64 || lock.height < 64) {
        printf("P7 bad lock depth=%ld w=%ld h=%ld\n",
               (long)lock.depth, (long)lock.width, (long)lock.height);
        result = 24; goto done;
    }
    background = sample(LEFT_X, LEFT_Y);

    /* Query defaults before touching anything. */
    if ((result = expect(!glIsEnabled(GL_LIGHTING), "lighting off default",
                         30)))
        goto done;
    glGetIntegerv(GL_MAX_LIGHTS, &query);
    if ((result = expect(query >= 8, "max lights", 31)))
        goto done;
    glGetLightfv(GL_LIGHT3, GL_POSITION, values);
    if ((result = expect(values[0] == 0.0f && values[1] == 0.0f &&
                         values[2] == 1.0f && values[3] == 0.0f,
                         "default light position", 32))) {
        printf("P7 pos=%g %g %g %g\n",
               (double)values[0], (double)values[1],
               (double)values[2], (double)values[3]);
        goto done;
    }
    glGetMaterialfv(GL_FRONT, GL_DIFFUSE, values);
    if ((result = expect(values[0] == 0.8f && values[1] == 0.8f &&
                         values[2] == 0.8f && values[3] == 1.0f,
                         "default material diffuse", 33)))
        goto done;

    /* Unlit reference quad. */
    glColor3ub(255, 255, 255);
    clear_scene();
    quad_at(QUAD_CX, QUAD_CY, 1.0f);
    if ((result = capture())) { result = 35; goto done; }
    unlit = sample(QUAD_CX, QUAD_CY);
    if ((result = expect(unlit != background, "unlit quad drawn", 36)))
        goto done;

    /* Head-light directional: white diffuse straight onto the quad.
     * The lit pixel must differ from the unlit one and from background. */
    glEnable(GL_LIGHTING);
    if ((result = expect(glIsEnabled(GL_LIGHTING), "lighting enabled", 38)))
        goto done;
    glEnable(GL_LIGHT0);
    clear_scene();
    quad_at(QUAD_CX, QUAD_CY, 1.0f);
    if ((result = capture())) { result = 39; goto done; }
    litHeadlight = sample(QUAD_CX, QUAD_CY);
    if ((result = expect(litHeadlight != unlit &&
                         litHeadlight != background,
                         "directional light changes shading", 40)))
        goto done;

    /* Disabling lighting restores the unlit appearance exactly. */
    glDisable(GL_LIGHT0);
    glDisable(GL_LIGHTING);
    clear_scene();
    quad_at(QUAD_CX, QUAD_CY, 1.0f);
    if ((result = capture())) { result = 41; goto done; }
    if ((result = expect(sample(QUAD_CX, QUAD_CY) == unlit,
                          "lighting off restores unlit", 42)))
        goto done;
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    clear_scene();

    /*
     * Deferred-order split: draw under one light configuration, mutate the
     * light between glBegin blocks inside the same frame, draw again. The
     * two batches must keep their own lighting results after the flush.
     */
    glLightfv(GL_LIGHT1, GL_DIFFUSE, (GLfloat[]){1.0f, 1.0f, 1.0f, 1.0f});
    /* Point the shared head-light away so only geometry facing GL_LIGHT1's
     * separate direction would differ; here both quads share normals, so
     * instead give LIGHT1 zero diffuse to make its half dark when enabled. */
    glLightfv(GL_LIGHT1, GL_AMBIENT, (GLfloat[]){0.0f, 0.0f, 0.0f, 1.0f});
    glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, 180.0f);
    clear_scene();
    quad_at(LEFT_X, LEFT_Y, 1.0f);   /* captured under LIGHT0 only */
    glEnable(GL_LIGHT1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, (GLfloat[]){0.0f, 0.0f, 0.0f, 1.0f});
    glLightfv(GL_LIGHT1, GL_DIFFUSE, (GLfloat[]){1.0f, 1.0f, 1.0f, 1.0f});
    quad_at(RIGHT_X, RIGHT_Y, 1.0f); /* must capture the new state */
    if ((result = capture())) { result = 43; goto done; }
    litSplit = sample(LEFT_X, LEFT_Y);
    if ((result = expect(litSplit == litHeadlight &&
                         sample(RIGHT_X, RIGHT_Y) == litHeadlight,
                         "deferred split keeps per-batch lighting", 44)))
        goto done;
    glDisable(GL_LIGHT1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, (GLfloat[]){1.0f, 1.0f, 1.0f, 1.0f});

    /* Colour material: the current colour feeds the tracked channels. */
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glColor3ub(255, 0, 0);
    clear_scene();
    quad_at(QUAD_CX, QUAD_CY, 1.0f);
    if ((result = capture())) { result = 45; goto done; }
    litColourMaterial = sample(QUAD_CX, QUAD_CY);
    if ((result = expect(litColourMaterial != litHeadlight &&
                         litColourMaterial != background,
                         "colour material tracks current colour", 46)))
        goto done;
    glDisable(GL_COLOR_MATERIAL);
    glColor3ub(255, 255, 255);
    clear_scene();
    quad_at(QUAD_CX, QUAD_CY, 1.0f);
    if ((result = capture())) { result = 47; goto done; }
    if ((result = expect(sample(QUAD_CX, QUAD_CY) == litHeadlight,
                         "colour material off restores", 48)))
        goto done;

    /* Range attenuation: a local light sits just in front of the left
     * quad and far behind the right one; linear attenuation makes them
     * render differently. */
    glLightfv(GL_LIGHT0, GL_POSITION, (GLfloat[]){-0.6f, 0.0f, 0.4f, 1.0f});
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.0f);
    clear_scene();
    quad_at(LEFT_X, LEFT_Y, 1.0f);
    quad_at(RIGHT_X, RIGHT_Y, 1.0f);
    if ((result = capture())) { result = 50; goto done; }
    litNear = sample(LEFT_X, LEFT_Y);
    litFar = sample(RIGHT_X, RIGHT_Y);
    if ((result = expect(litNear != litFar && litNear != background,
                         "range attenuation separates distances", 51)))
        goto done;

    /* Spot cone: cutoff ~30 degrees aimed down -Z lights the centre quad
     * but not an off-axis one. */
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.0f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, (GLfloat[]){0.0f, 0.0f, 1.0f, 1.0f});
    glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION,
              (GLfloat[]){0.0f, 0.0f, -1.0f});
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 30.0f);
    clear_scene();
    quad_at(QUAD_CX, QUAD_CY, 1.0f);
    quad_at(0.7f, 0.7f, 1.0f);
    if ((result = capture())) { result = 52; goto done; }
    coneIn = sample(QUAD_CX, QUAD_CY);
    coneOut = sample(0.7f, 0.7f);
    if ((result = expect(coneIn != background && coneIn != coneOut,
                         "spot cone lights centre only", 53)))
        goto done;
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 180.0f);

    /* Normal arrays: normals pointing away kill the diffuse term, so an
     * array-drawn quad with reversed normals goes dark. */
    {
        static const GLfloat positions[4][3] = {
            {-0.25f, -0.25f, -0.5f}, {0.25f, -0.25f, -0.5f},
            {0.25f, 0.25f, -0.5f}, {-0.25f, 0.25f, -0.5f}};
        static const GLfloat normalsAway[4][3] = {
            {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f},
            {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}};
        UWORD away;

        glTranslatef(0.55f, 0.0f, 0.0f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, positions);
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, 0, normalsAway);
        clear_scene();
        glDrawArrays(GL_QUADS, 0, 4);
        if ((result = capture())) { result = 54; goto done; }
        away = sample(0.55f, 0.0f);
        if ((result = expect(away != litHeadlight && away != coneOut,
                             "normal arrays reach the shader", 55)))
            goto done;
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glLoadIdentity();
    }
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);

    query = (GLint)glGetError();
    if ((result = expect(query == GL_NO_ERROR, "no error so far", 60))) {
        printf("phase7: unexpected error=%ld\n", (long)query);
        goto done;
    }

    /* Rejected arguments must raise errors and leave state alone. */
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 200.0f);
    if ((result = expect(glGetError() == GL_INVALID_VALUE,
                         "spot exponent range", 61)))
        goto done;
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 120.0f);
    if ((result = expect(glGetError() == GL_INVALID_VALUE,
                         "spot cutoff range", 62)))
        goto done;
    glLightf((GLenum)(GL_LIGHT7 + 1), GL_SPOT_CUTOFF, 45.0f);
    if ((result = expect(glGetError() == GL_INVALID_ENUM,
                         "light index", 63)))
        goto done;
    glLightfv(GL_LIGHT0, 9999, values);
    if ((result = expect(glGetError() == GL_INVALID_ENUM,
                         "light pname", 64)))
        goto done;
    glMaterialf(GL_FRONT, GL_SHININESS, -1.0f);
    if ((result = expect(glGetError() == GL_INVALID_VALUE,
                         "shininess range", 65)))
        goto done;
    glColorMaterial((GLenum)999, GL_AMBIENT);
    if ((result = expect(glGetError() == GL_INVALID_ENUM,
                         "color material face", 66)))
        goto done;
    glNormalPointer(GL_FLOAT, 0, 0);
    if ((result = expect(glGetError() == GL_INVALID_VALUE,
                         "null normal pointer", 67)))
        goto done;
    /* A rejected call leaves the configured cutoff untouched. */
    glGetLightfv(GL_LIGHT0, GL_SPOT_CUTOFF, values);
    if ((result = expect(values[0] == 180.0f, "cutoff unchanged", 68)))
        goto done;

    /* FastPath refuses to start while lighting is on; it works again
     * once lighting is off. */
    glEnable(GL_LIGHTING);
    {
        MGLFastPath fp = MGL_FASTPATH_INITIALIZER;
        if ((result = expect(!mglBeginFastPath(GL_TRIANGLES,
                                               MGL_FASTPATH_POSITION, &fp),
                             "fastpath rejected while lit", 70)))
            goto done;
        if ((result = expect(glGetError() == GL_INVALID_OPERATION,
                             "fastpath rejection error", 71)))
            goto done;
    }
    glDisable(GL_LIGHTING);
    {
        MGLFastPath fp = MGL_FASTPATH_INITIALIZER;
        if (!mglBeginFastPath(GL_TRIANGLES, MGL_FASTPATH_POSITION, &fp)) {
            result = expect(0, "fastpath usable unlit", 72);
            goto done;
        }
        mglEndFastPath(&fp);
    }

    if ((result = expect(glGetError() == GL_NO_ERROR, "final error clean",
                         75)))
        goto done;

    result = 0;
    printf("MINIGL_PHASE7 mode=window size=%lux%lu "
           "directional=pass deferred_split=pass color_material=pass "
           "attenuation=pass spot=pass normal_arrays=pass queries=pass "
           "fastpath=pass\n", lock.width, lock.height);

done:
    mglDeleteContext();
    MiniGLClose();
    return result;
}
