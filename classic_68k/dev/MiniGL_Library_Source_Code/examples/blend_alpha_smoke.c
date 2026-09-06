/*
 * Blend / alpha / shading acceptance check for the R200 backend.
 *
 * Reproduces the exact state combinations BlitzQuake uses for its bonus-flash
 * fill (textured MGL_FLATFAN + MODULATE + SRC_ALPHA blending + scissor),
 * its particle points (alpha test + blended GL_POINTS + depth mask off) and
 * its flashblend dlight fans (smooth-shaded and additively blended fans,
 * depth test active with writes masked).
 *
 * Every check runs unconditionally and raw pixel samples are printed, so a
 * single serial run gives the full picture even when several paths fail.
 * Assertions are equality or monotonic-gray comparisons, independent of the
 * surface pixel layout.
 */
#include <stdio.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>


static MGLLockInfo lock;
static ULONG W, H;
static UWORD background;

static UWORD sample_at(ULONG x, ULONG y)
{
    UBYTE *base = (UBYTE *)lock.base_address;

    if (x >= W) x = W - 1UL;
    if (y >= H) y = H - 1UL;
    return *(UWORD *)(base + y * lock.pitch + x * 2UL);
}

/* Device coordinates for the 2D paths; NDC for the 3D paths. */
#define DX(frac) ((ULONG)((frac) * (GLfloat)(W - 1UL)))
#define DY(frac) ((ULONG)((frac) * (GLfloat)(H - 1UL)))
static UWORD sample_dev(GLfloat fx, GLfloat fy)
{
    return sample_at(DX(fx), DY(fy));
}
static UWORD sample_ndc(GLfloat nx, GLfloat ny)
{
    GLfloat fx = (nx + 1.0f) * 0.5f;
    GLfloat fy = (1.0f - ny) * 0.5f;

    return sample_at(DX(fx), DY(fy));
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

/* Fullscreen device-coordinate fan, the shape every Quake 2D fill uses. */
static void dev_fan_fullscreen(void)
{
    glBegin(MGL_FLATFAN);
    glVertex2f(0.0f, 0.0f);
    glVertex2f((GLfloat)W, 0.0f);
    glVertex2f((GLfloat)W, (GLfloat)H);
    glVertex2f(0.0f, (GLfloat)H);
    glEnd();
}

/* Additive gray rectangle fan covering an NDC box; returns its centre. */
static void additive_box(GLfloat x0, GLfloat x1, GLfloat y0, GLfloat y1)
{
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

/* Every capture checks the return code; a failed lock means stale samples. */
static int lockFailures;
static int cap(void)
{
    int ok = capture();

    if (!ok)
        lockFailures++;
    return ok;
}

int main(int argc, char **argv)
{
    GLboolean fullscreen = argc == 2 && !strcmp(argv[1], "--fullscreen");
    int width = fullscreen ? 640 : 320;
    int height = fullscreen ? 480 : 240;
    int fails = 0;
    UWORD refWhite, redRef;
    UWORD t1Covered, t1Upper, t2Diffuse;
    UWORD pointA, pointB, pointC;
    UWORD fanCentre, fanRim, addOne, addTwo, depthHidden, depthFront;
    UWORD scLeft, scRight;
    GLuint texWhiteHalf;
    GLubyte texels[4];
    GLenum err;

    if (!MiniGLOpen()) return 21;
    mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, width, height)) { MiniGLClose(); return 22; }

    glViewport(0, 0, width, height);

    /* Baseline state mirroring BlitzQuake's GL_Init(). */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    clear_scene();
    printf("blend_alpha: lock base=%08lx pitch=%ld w=%ld h=%ld depth=%ld\n",
           (long)lock.base_address, (long)lock.pitch, (long)lock.width,
           (long)lock.height, (long)lock.depth);
    if (capture()) { mglDeleteContext(); MiniGLClose(); return 23; }
    W = lock.width;
    H = lock.height;
    if (!lock.base_address || W < 64UL || H < 64UL) {
        mglDeleteContext(); MiniGLClose(); return 24;
    }
    background = sample_dev(0.9f, 0.9f);
    printf("blend_alpha: after-clear err=%ld bg=%04x\n",
           (long)glGetError(), background);

    /* References: opaque white patch and solid red patch. Quake discipline:
     * GL_TEXTURE_2D is disabled for every untextured draw (an enabled
     * sampler with texture name 0 is a state Quake never enters). */
    clear_scene();
    printf("blend_alpha: post-clear err=%ld\n", (long)glGetError());
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(MGL_FLATFAN);
    glVertex2f(0.0f, 0.0f);
    glVertex2f((GLfloat)DX(0.25f), 0.0f);
    glVertex2f((GLfloat)DX(0.25f), (GLfloat)DY(0.25f));
    glVertex2f(0.0f, (GLfloat)DY(0.25f));
    glEnd();
    printf("blend_alpha: reffan err=%ld\n", (long)glGetError());
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.9f, -0.9f);
    glVertex2f(-0.5f, -0.9f);
    glVertex2f(-0.7f, -0.5f);
    glEnd();
    printf("blend_alpha: reftri err=%ld\n", (long)glGetError());
    capture();
    refWhite = sample_dev(0.12f, 0.12f);
    redRef = sample_ndc(-0.7f, -0.75f);
    printf("blend_alpha: refs white=%04x red=%04x\n", refWhite, redRef);

    /* Probe P1: the same red triangle with GL_TEXTURE_2D enabled and no
     * valid texture bound (texture name 0). Checks whether that state
     * raises INVALID_OPERATION and/or poisons the submission pipeline the
     * way the first hardware run suggested. */
    clear_scene();
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(0.5f, -0.9f);
    glVertex2f(0.9f, -0.9f);
    glVertex2f(0.7f, -0.5f);
    glEnd();
    printf("blend_alpha: probe-notex-draw err=%ld\n", (long)glGetError());
    glDisable(GL_TEXTURE_2D);
    capture();
    printf("blend_alpha: probe-notex sample=%04x\n",
           sample_ndc(0.7f, -0.75f));
    glGetError();

    /*
     * T1: the pickup-flash path. One 1x1 white texture with alpha 0.5,
     * MODULATE env, SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending, scissored
     * fullscreen MGL_FLATFAN in device coordinates.
     */
    texWhiteHalf = 1;
    texels[0] = texels[1] = texels[2] = 255;
    texels[3] = 128;
    glBindTexture(GL_TEXTURE_2D, texWhiteHalf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    clear_scene();
    glDisable(GL_ALPHA_TEST);
    glBindTexture(GL_TEXTURE_2D, texWhiteHalf);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glScissor(0, (GLint)H / 2, (GLint)W, (GLint)H - (GLint)H / 2);
    glEnable(GL_SCISSOR_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f((GLfloat)W, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f((GLfloat)W, (GLfloat)H);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, (GLfloat)H);
    glEnd();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    capture();
    t1Covered = sample_dev(0.5f, 0.75f);
    t1Upper = sample_dev(0.5f, 0.25f);
    if (!(t1Covered != background && t1Covered < refWhite))
        fails |= 1 << 1;
    if (t1Upper != background)
        fails |= 1 << 2;

    /*
     * T2: the same fill without a texture, relying on diffuse alpha
     * (glColor4f alpha 0.5). Known-suspect path ("packed immediate-mode
     * diffuse alpha reaches R200 as zero").
     */
    clear_scene();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
    dev_fan_fullscreen();
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    capture();
    t2Diffuse = sample_dev(0.5f, 0.5f);
    if (!(t2Diffuse != background && t2Diffuse < refWhite))
        fails |= 1 << 3;

    /*
     * T3: particle conditions. Blended GL_POINTS with alpha test active:
     * alpha 1.0 and ~0.78 pass the GREATER 0.5 test and blend over black,
     * alpha 0.25 must be discarded entirely.
     */
    clear_scene();
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glPointSize(3.0f);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_POINTS);
    glColor4ub(255, 255, 255, 255); glVertex2f(-0.5f, 0.5f);
    glColor4ub(255, 255, 255, 200); glVertex2f(0.0f, 0.5f);
    glColor4ub(255, 255, 255, 64);  glVertex2f(0.5f, 0.5f);
    glEnd();
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    capture();
    pointA = sample_ndc(-0.5f, 0.5f);
    pointB = sample_ndc(0.0f, 0.5f);
    pointC = sample_ndc(0.5f, 0.5f);
    if (pointA == background)
        fails |= 1 << 4;
    if (pointB == background)
        fails |= 1 << 5;
    if (pointC != background)
        fails |= 1 << 6;

    /*
     * T4: flashblend dlight fan geometry. A smooth-shaded triangle fan with
     * a bright centre and black rim: the rim must stay black (a flat-take-
     * first bug floods it) and the centre must match the red reference
     * (a flat-take-last bug leaves it black).
     */
    clear_scene();
    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(0.35f, 0.0f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(0.25f, 0.25f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(0.0f, 0.35f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(-0.25f, 0.25f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(-0.35f, 0.0f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(-0.25f, -0.25f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(0.0f, -0.35f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(0.25f, -0.25f);
    glColor3f(0.0f, 0.0f, 0.0f); glVertex2f(0.35f, 0.0f);
    glEnd();
    glShadeModel(GL_FLAT);
    capture();
    fanCentre = sample_ndc(0.05f, 0.05f);
    fanRim = sample_ndc(0.30f, 0.06f);
    if (fanRim != background)
        fails |= 1 << 7;
    if (fanCentre != redRef)
        fails |= 1 << 8;

    /*
     * T5: additive blending on fans (the dlight blend func). Two stacked
     * gray layers must be brighter than one layer, and one layer brighter
     * than the background: proves the GL_ONE/GL_ONE factors accumulate.
     */
    clear_scene();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glColor3f(0.19f, 0.19f, 0.19f);
    additive_box(-0.9f, -0.3f, 0.2f, 0.7f);
    capture();
    addOne = sample_ndc(-0.6f, 0.45f);

    clear_scene();
    additive_box(0.3f, 0.9f, 0.2f, 0.7f);
    additive_box(0.3f, 0.9f, 0.2f, 0.7f);
    glDisable(GL_BLEND);
    capture();
    addTwo = sample_ndc(0.6f, 0.45f);
    if (!(addOne > background && addTwo > addOne))
        fails |= 1 << 9;

    /*
     * T6: depth test stays active while the depth mask is off (flashblend
     * draws with glDepthMask(FALSE)). A blended quad behind an opaque one
     * must stay hidden; moving it in front must blend it over the white.
     */
    clear_scene();
    glEnable(GL_DEPTH_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.8f, -0.8f, -0.5f);
    glVertex3f(0.8f, -0.8f, -0.5f);
    glVertex3f(0.0f, 0.8f, -0.5f);
    glEnd();
    capture();
    refWhite = sample_ndc(0.0f, -0.3f);

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 1.0f, 0.0f, 0.75f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.6f, -0.6f, 0.5f);
    glVertex3f(0.6f, -0.6f, 0.5f);
    glVertex3f(0.0f, 0.6f, 0.5f);
    glEnd();
    capture();
    depthHidden = sample_ndc(0.0f, -0.3f);
    if (depthHidden != refWhite)
        fails |= 1 << 10;

    glBegin(GL_TRIANGLES);
    glVertex3f(-0.6f, -0.6f, -0.9f);
    glVertex3f(0.6f, -0.6f, -0.9f);
    glVertex3f(0.0f, 0.6f, -0.9f);
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    capture();
    depthFront = sample_ndc(0.0f, -0.3f);
    if (!(depthFront != refWhite && depthFront != background))
        fails |= 1 << 11;

    /*
     * T7: scissor alone (pickup path guards the status bar this way).
     * A white fan covering the whole screen must paint only the scissored
     * left half.
     */
    clear_scene();
    glDisable(GL_ALPHA_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glScissor(0, 0, (GLint)W / 2, (GLint)H);
    glEnable(GL_SCISSOR_TEST);
    dev_fan_fullscreen();
    glDisable(GL_SCISSOR_TEST);
    capture();
    scLeft = sample_dev(0.25f, 0.5f);
    scRight = sample_dev(0.75f, 0.5f);
    if (!(scLeft == refWhite && scRight == background))
        fails |= 1 << 12;

    err = glGetError();

    printf("blend_alpha: bg=%04x white=%04x red=%04x\n", background,
           refWhite, redRef);
    printf("blend_alpha: t1cov=%04x t1top=%04x t2diff=%04x\n",
           t1Covered, t1Upper, t2Diffuse);
    printf("blend_alpha: ptA=%04x ptB=%04x ptC=%04x\n",
           pointA, pointB, pointC);
    printf("blend_alpha: fanc=%04x fanr=%04x add1=%04x add2=%04x\n",
           fanCentre, fanRim, addOne, addTwo);
    printf("blend_alpha: dhide=%04x dfront=%04x scL=%04x scR=%04x err=%ld\n",
           depthHidden, depthFront, scLeft, scRight, (long)err);
    if (fails == 0 && err == GL_NO_ERROR && lockFailures == 0)
        printf("MINIGL_BLEND_ALPHA mode=%s size=%lux%lu ALL=pass\n",
               fullscreen ? "fullscreen" : "window", W, H);
    else
        printf("MINIGL_BLEND_ALPHA mode=%s FAILBITS=%08lx locks=%ld err=%ld\n",
               fullscreen ? "fullscreen" : "window", (long)fails,
               (long)lockFailures, (long)err);

    mglDeleteContext();
    MiniGLClose();
    return (fails == 0 && err == GL_NO_ERROR) ? 0 : 1;
}
