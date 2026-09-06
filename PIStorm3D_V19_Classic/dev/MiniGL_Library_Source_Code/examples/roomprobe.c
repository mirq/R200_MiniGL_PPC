/*
 * Bisecting probe for the quake.world-room-walk suite test.
 *
 * Exercises each mechanism the room test uses, one at a time, reporting
 * glGetError after every step so the first call that raises the backend's
 * GL_INVALID_OPERATION (89) is identifiable on hardware.
 *
 * Build/flags: same as the r200 testsuite target.
 */
#include <exec/libraries.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

#include "quake_room_model.h"

static GLuint g_tex[QUAKE_ROOM_TEXTURE_COUNT];

static GLenum Step(const char *what)
{
    GLenum error = glGetError();
    printf("STEP %-38s error=%ld\n", what, (long)error);
    return error;
}

static void UploadTexture(GLint index)
{
    glBindTexture(GL_TEXTURE_2D, g_tex[index]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 g_quakeRoomTexSize[index][0], g_quakeRoomTexSize[index][1],
                 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 g_quakeRoomTexels + g_quakeRoomTexOffset[index]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

static void TexturedQuad(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-0.8f, -0.8f);
    glTexCoord2f(4.0f, 0.0f); glVertex2f(0.8f, -0.8f);
    glTexCoord2f(4.0f, 4.0f); glVertex2f(0.8f, 0.8f);
    glTexCoord2f(0.0f, 4.0f); glVertex2f(-0.8f, 0.8f);
    glEnd();
}

static void RoomViewMatrix(const GLfloat *pose)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_W,
              QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_W,
              -QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_H,
              QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_H,
              QUAKE_ROOM_PROJ_NEAR, QUAKE_ROOM_PROJ_FAR);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(90.0f - pose[3], 0.0f, 0.0f, 1.0f);
    glTranslatef(-pose[0], -pose[1], -pose[2]);
}

/* Exact copy of the suite's ResetGLState(). */
static GLint g_width = 800, g_height = 600;

static void DrainErrors(void)
{
    int count;
    for (count = 0; count < 16 && glGetError() != GL_NO_ERROR; ++count)
        ;
}

static void SuiteResetGLState(void)
{
    DrainErrors();
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDepthRange(0.0, 1.0);
    glShadeModel(GL_SMOOTH);
    glViewport(0, 0, g_width, g_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    DrainErrors();
}

int main(int argc, char **argv)
{
    GLint width = 800, height = 600, depth = 32;
    void *context;
    GLint index;
    ULONG frame;

    if (argc > 1) width = atoi(argv[1]);
    if (argc > 2) height = atoi(argv[2]);
    if (argc > 3) depth = atoi(argv[3]);

    if (!MiniGLOpen()) {
        printf("roomprobe: cannot open minigl.library\n");
        return 20;
    }
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth((int)depth);
    context = mglCreateContext(0, 0, width, height);
    if (!context) {
        printf("roomprobe: context creation failed\n");
        MiniGLClose();
        return 20;
    }
    printf("roomprobe: %s / %s / %s\n", glGetString(GL_VENDOR),
           glGetString(GL_RENDERER), glGetString(GL_VERSION));

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    mglSwitchDisplay();
    if (Step("1 clear+present")) goto fail;

    glGenTextures(QUAKE_ROOM_TEXTURE_COUNT, g_tex);
    Step("2 glGenTextures x17");

    /* Square 64x64 first: the proven shape. */
    UploadTexture(0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3ub(255, 255, 255);
    TexturedQuad();
    mglSwitchDisplay();
    if (Step("3 tex64x64 quad+present")) goto fail;

    /* Every distinct texture shape in the room set. */
    for (index = 1; index < QUAKE_ROOM_TEXTURE_COUNT; ++index) {
        char label[64];
        UploadTexture(index);
        TexturedQuad();
        sprintf(label, "4 tex %ldx%ld quad",
                (long)g_quakeRoomTexSize[index][0],
                (long)g_quakeRoomTexSize[index][1]);
        if (Step(label)) goto fail;
    }

    /* Vertex arrays exactly as the room test sets them up. */
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 5 * (GLint)sizeof(GLfloat),
                    &g_quakeRoomVertices[0][0]);
    glTexCoordPointer(2, GL_FLOAT, 5 * (GLint)sizeof(GLfloat),
                      &g_quakeRoomVertices[0][3]);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    RoomViewMatrix(g_quakeRoomPath[0]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* One small batch, base-relative pointer. */
    glBindTexture(GL_TEXTURE_2D, g_tex[g_quakeRoomBatches[0][0]]);
    glDrawElements(GL_TRIANGLES, g_quakeRoomBatches[0][2],
                   GL_UNSIGNED_SHORT, g_quakeRoomIndices);
    if (Step("5 drawElements batch0 base")) goto fail;

    /* Same batch through an offset pointer, as the suite does. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, g_quakeRoomBatches[0][2],
                   GL_UNSIGNED_SHORT,
                   g_quakeRoomIndices + g_quakeRoomBatches[0][1]);
    if (Step("6 drawElements batch0 offset")) goto fail;

    /* All 17 batches, one frame. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (index = 0; index < QUAKE_ROOM_BATCH_COUNT; ++index) {
        glBindTexture(GL_TEXTURE_2D, g_tex[g_quakeRoomBatches[index][0]]);
        glDrawElements(GL_TRIANGLES, g_quakeRoomBatches[index][2],
                       GL_UNSIGNED_SHORT,
                       g_quakeRoomIndices + g_quakeRoomBatches[index][1]);
    }
    mglSwitchDisplay();
    if (Step("7 full room frame+present")) goto fail;

    /* Walk 90 patrol frames with per-frame present, but preceded by the
     * suite's exact ResetGLState() like RunTest does, with per-call error
     * bisecting of the draw latch. */
    SuiteResetGLState();
    printf("STEP 8a after SuiteResetGLState      error=%ld\n",
           (long)glGetError());
    glEnable(GL_DEPTH_TEST);
    printf("STEP 8b glEnable DEPTH_TEST          error=%ld\n",
           (long)glGetError());
    glDepthFunc(GL_LESS);
    printf("STEP 8c glDepthFunc                  error=%ld\n",
           (long)glGetError());
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    printf("STEP 8d cull FRONT                   error=%ld\n",
           (long)glGetError());
    glColor3ub(255, 255, 255);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    printf("STEP 8e texture enable+modulate      error=%ld\n",
           (long)glGetError());
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 5 * (GLint)sizeof(GLfloat),
                    &g_quakeRoomVertices[0][0]);
    glTexCoordPointer(2, GL_FLOAT, 5 * (GLint)sizeof(GLfloat),
                      &g_quakeRoomVertices[0][3]);
    printf("STEP 8f arrays+pointers              error=%ld\n",
           (long)glGetError());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_W,
              QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_W,
              -QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_H,
              QUAKE_ROOM_PROJ_NEAR * QUAKE_ROOM_PROJ_HALF_H,
              QUAKE_ROOM_PROJ_NEAR, QUAKE_ROOM_PROJ_FAR);
    glMatrixMode(GL_MODELVIEW);
    printf("STEP 8g glFrustum                    error=%ld\n",
           (long)glGetError());
    for (frame = 0; frame < 90; ++frame) {
        RoomViewMatrix(g_quakeRoomPath[frame % QUAKE_ROOM_LOOP_FRAMES]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (index = 0; index < QUAKE_ROOM_BATCH_COUNT; ++index) {
            glBindTexture(GL_TEXTURE_2D, g_tex[g_quakeRoomBatches[index][0]]);
            glDrawElements(GL_TRIANGLES, g_quakeRoomBatches[index][2],
                           GL_UNSIGNED_SHORT,
                           g_quakeRoomIndices + g_quakeRoomBatches[index][1]);
        }
        mglSwitchDisplay();
        if (glGetError() != GL_NO_ERROR) {
            printf("STEP 8 walk error at frame %lu\n",
                   (unsigned long)frame);
            goto fail;
        }
    }
    printf("STEP 8 walk 90 frames               error=0\n");
    printf("roomprobe: PASS\n");
    mglDeleteContext();
    MiniGLClose();
    return 0;

fail:
    printf("roomprobe: FAIL\n");
    mglDeleteContext();
    MiniGLClose();
    return 20;
}
