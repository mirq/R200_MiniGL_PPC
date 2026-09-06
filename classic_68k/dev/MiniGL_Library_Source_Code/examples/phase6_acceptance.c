#include <exec/libraries.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/radeon3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility/tagitem.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <libraries/minigl_perf.h>

struct IntuitionBase *IntuitionBase;
struct Library *Radeon9200Base;
static BOOL contextLossTested;

static int expect_error(GLenum expected, int code)
{
    GLenum actual = glGetError();
    if (actual != expected) {
        printf("phase6 acceptance: error %u, expected %u\n",
               (unsigned)actual, (unsigned)expected);
        return code;
    }
    return 0;
}

static void advance_fast_vertex(MGLFastPath *fp)
{
    UBYTE *vertex;

    ++fp->vertexCount;
    vertex = fp->vertexBuffer + fp->vertexCount * fp->vertexStride;
    fp->currentVertex = vertex;
    fp->x = (GLfloat *)(vertex + fp->offsetX);
    fp->y = (GLfloat *)(vertex + fp->offsetY);
    fp->z = (GLfloat *)(vertex + fp->offsetZ);
    fp->w = (GLfloat *)(vertex + fp->offsetW);
    fp->r = (GLfloat *)(vertex + fp->offsetR);
    fp->g = (GLfloat *)(vertex + fp->offsetG);
    fp->b = (GLfloat *)(vertex + fp->offsetB);
    fp->a = (GLfloat *)(vertex + fp->offsetA);
    fp->u0 = (GLfloat *)(vertex + fp->offsetU0);
    fp->v0 = (GLfloat *)(vertex + fp->offsetV0);
    fp->u1 = (GLfloat *)(vertex + fp->offsetU1);
    fp->v1 = (GLfloat *)(vertex + fp->offsetV1);
    fp->q = (GLfloat *)(vertex + fp->offsetQ);
}

static int test_arrays(void)
{
    GLfloat positions[6][3] = {
        {-0.8f,-0.7f,0.0f}, {-0.2f,-0.7f,0.0f}, {-0.5f,-0.1f,0.0f},
        { 0.2f,-0.7f,0.0f}, { 0.8f,-0.7f,0.0f}, { 0.5f,-0.1f,0.0f}
    };
    GLubyte colors[6][4] = {
        {255,0,0,255}, {0,255,0,255}, {0,0,255,255},
        {255,255,0,255}, {0,255,255,255}, {255,0,255,255}
    };
    GLushort elements[3] = {2,3,4};
    int result;

    glVertexPointer(3, GL_FLOAT, 0, positions);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glLockArrays(1, 4);
    if ((result = expect_error(GL_NO_ERROR, 31))) return result;
    positions[2][0] = 20.0f;
    glDrawArrays(GL_TRIANGLES, 1, 3);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, elements);
    glBegin(GL_TRIANGLES);
    glArrayElement(1); glArrayElement(2); glArrayElement(3);
    glEnd();
    if ((result = expect_error(GL_NO_ERROR, 32))) return result;
    glLockArrays(0, 1);
    if ((result = expect_error(GL_INVALID_OPERATION, 33))) return result;
    glUnlockArrays();
    if ((result = expect_error(GL_NO_ERROR, 34))) return result;
    glUnlockArrays();
    if ((result = expect_error(GL_INVALID_OPERATION, 35))) return result;
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    return 0;
}

static int test_fastpath(void)
{
    const ULONG supported = MGL_FASTPATH_POSITION | MGL_FASTPATH_COLOR |
                            MGL_FASTPATH_TEXCOORD0 |
                            MGL_FASTPATH_TEXCOORD1 |
                            MGL_FASTPATH_TEXCOORD_Q;
    MGLFastPath fp = MGL_FASTPATH_INITIALIZER;
    int result;

    if (mglGetFastPathCapabilities() != supported) return 40;
    if (mglBeginFastPath(GL_TRIANGLES, MGL_FASTPATH_COLOR, &fp)) return 41;
    if ((result = expect_error(GL_INVALID_VALUE, 42))) return result;
    fp.size = sizeof(fp);
    if (mglBeginFastPath(GL_TRIANGLES,
                         MGL_FASTPATH_POSITION | MGL_FASTPATH_NORMAL,
                         &fp)) return 43;
    if ((result = expect_error(GL_INVALID_ENUM, 44))) return result;
    fp.size = sizeof(fp);
    if (!mglBeginFastPath(GL_TRIANGLES, supported, &fp)) return 45;
    if (fp.flags != MGL_FASTPATH_FLAG_DIRECT_BUFFER || !fp.vertexBuffer ||
        fp.vertexStride != 13 * sizeof(GLfloat) || fp.vertexCapacity != 4096 ||
        fp.firstVertex != 0 || fp.vertexCount != 0 ||
        fp.offsetX != 0 || fp.offsetW != 3 * sizeof(GLfloat) ||
        fp.offsetR != 4 * sizeof(GLfloat) ||
        fp.offsetU0 != 8 * sizeof(GLfloat) ||
        fp.offsetU1 != 10 * sizeof(GLfloat) ||
        fp.offsetQ != 12 * sizeof(GLfloat) || fp.x == NULL || fp.q == NULL)
        return 46;
    *fp.x=-0.5f; *fp.y=0.1f; *fp.z=0.0f; *fp.w=1.0f;
    *fp.r=1.0f; *fp.g=0.0f; *fp.b=0.0f; *fp.a=1.0f;
    *fp.u0=0.0f; *fp.v0=0.0f; *fp.u1=0.0f; *fp.v1=0.0f; *fp.q=2.0f;
    fp.currentQ=2.0f;
    advance_fast_vertex(&fp);
    *fp.x=0.5f; *fp.y=0.1f; *fp.z=0.0f; *fp.w=1.0f;
    *fp.r=0.0f; *fp.g=1.0f; *fp.b=0.0f; *fp.a=1.0f;
    *fp.u0=2.0f; *fp.v0=0.0f; *fp.u1=2.0f; *fp.v1=0.0f; *fp.q=2.0f;
    advance_fast_vertex(&fp);
    *fp.x=0.0f; *fp.y=0.8f; *fp.z=0.0f; *fp.w=1.0f;
    *fp.r=0.0f; *fp.g=0.0f; *fp.b=1.0f; *fp.a=1.0f;
    *fp.u0=1.0f; *fp.v0=2.0f; *fp.u1=1.0f; *fp.v1=2.0f; *fp.q=2.0f;
    advance_fast_vertex(&fp);
    mglEndFastPath(&fp);
    if (fp.privateData || fp.currentVertex) return 47;
    if ((result = expect_error(GL_NO_ERROR, 48))) return result;
    glBegin(GL_TRIANGLES);
    fp.size = sizeof(fp);
    if (mglBeginFastPath(GL_TRIANGLES, MGL_FASTPATH_POSITION, &fp)) return 49;
    if ((result = expect_error(GL_INVALID_OPERATION, 50))) return result;
    glVertex2f(-0.1f,-0.1f); glVertex2f(0.1f,-0.1f);
    glVertex2f(0.0f,0.1f); glEnd();
    return expect_error(GL_NO_ERROR, 51);
}

static int test_window_resize(void)
{
    struct Window *window = (struct Window *)mglGetWindowHandle();
    struct Window *overlap;
    struct TagItem tags[] = {
        {WA_Left, 40}, {WA_Top, 30}, {WA_Width, 180}, {WA_Height, 120},
        {WA_Title, (ULONG)"Phase6 overlap"}, {WA_DragBar, TRUE},
        {WA_DepthGadget, TRUE}, {TAG_DONE, 0}
    };
    MGLLockInfo lock;
    LONG oldLeft, oldTop, moveX, moveY;
    int result;

    if (!window || !IntuitionBase) return 60;
    oldLeft = window->LeftEdge;
    oldTop = window->TopEdge;
    tags[0].ti_Data = oldLeft + 10;
    tags[1].ti_Data = oldTop + 10;
    overlap = OpenWindowTagList(NULL, tags);
    if (!overlap) return 61;
    moveX = oldLeft > 5 ? -5 : 5;
    moveY = oldTop > 5 ? -5 : 5;
    MoveWindow(window, moveX, moveY);
    Delay(2);
    if (window->LeftEdge >= overlap->LeftEdge + overlap->Width ||
        overlap->LeftEdge >= window->LeftEdge + window->Width ||
        window->TopEdge >= overlap->TopEdge + overlap->Height ||
        overlap->TopEdge >= window->TopEdge + window->Height) {
        CloseWindow(overlap); return 63;
    }
    mglResizeContext(400, 300);
    {
        ULONG attempts;
        for (attempts = 0; attempts < 25UL; ++attempts) {
            if (window->Width-window->BorderLeft-window->BorderRight == 400 &&
                window->Height-window->BorderTop-window->BorderBottom == 300)
                break;
            Delay(1);
        }
    }
    if ((result = expect_error(GL_NO_ERROR, 64))) {
        CloseWindow(overlap); return result;
    }
    {
        BOOL locked=mglLockBack(&lock);
        printf("PHASE6_RESIZE inner=%ldx%ld lock=%ld size=%lux%lu\n",
               (long)(window->Width-window->BorderLeft-window->BorderRight),
               (long)(window->Height-window->BorderTop-window->BorderBottom),
               (long)locked,locked?lock.width:0,locked?lock.height:0);
    if (!locked ||
        window->Width - window->BorderLeft - window->BorderRight != 400 ||
        window->Height - window->BorderTop - window->BorderBottom != 300 ||
        lock.width != 400 || lock.height != 300) {
        CloseWindow(overlap); return 65;
    }
    }
    mglResizeContext(63, 300);
    if ((result = expect_error(GL_INVALID_VALUE, 66))) {
        CloseWindow(overlap); return result;
    }
    glBegin(GL_TRIANGLES);
    mglResizeContext(320, 240);
    if ((result = expect_error(GL_INVALID_OPERATION, 67))) {
        glEnd(); CloseWindow(overlap); return result;
    }
    mglSwitchDisplay();
    if ((result = expect_error(GL_INVALID_OPERATION, 140))) {
        glEnd(); CloseWindow(overlap); return result;
    }
    glViewport(0,0,320,240);
    if ((result = expect_error(GL_INVALID_OPERATION, 141))) {
        glEnd(); CloseWindow(overlap); return result;
    }
    glClearColor(1.0f,0.0f,1.0f,1.0f);
    if ((result = expect_error(GL_INVALID_OPERATION, 142))) {
        glEnd(); CloseWindow(overlap); return result;
    }
    glVertex2f(-0.1f,-0.1f); glVertex2f(0.1f,-0.1f);
    glVertex2f(0.0f,0.1f); glEnd();
    CloseWindow(overlap);
    return expect_error(GL_NO_ERROR, 68);
}

static int test_context_loss(void)
{
    struct Radeon3DInfo info;
    struct Radeon3DDevice *device;
    UBYTE before[4*4*4],after[4*4*4];
    GLuint texture;
    ULONG index;
    int result;
    glFlush();
    if ((result=expect_error(GL_NO_ERROR,80))) return result;
    Radeon9200Base=OpenLibrary("Radeon9200.chip",RADEON3D_LIBRARY_VERSION);
    if (!Radeon9200Base) return 81;
    info.Size=sizeof(info);
    device=Radeon3DOpen(RADEON3D_IFACE_VERSION,&info);
    if (!device) {
        CloseLibrary(Radeon9200Base); Radeon9200Base=NULL; return 82;
    }
    if (!(info.Caps&RADEON3D_CAP_TEST_INVALIDATE)) {
        if (Radeon3DInvalidateForTest(device)) {
            Radeon3DClose(device);
            CloseLibrary(Radeon9200Base); Radeon9200Base=NULL; return 82;
        }
        Radeon3DClose(device);
        CloseLibrary(Radeon9200Base); Radeon9200Base=NULL; return 0;
    }
    contextLossTested=TRUE;
    for (index=0;index<16;++index) {
        before[index*4]=255; before[index*4+1]=0;
        before[index*4+2]=0; before[index*4+3]=255;
        after[index*4]=0; after[index*4+1]=255;
        after[index*4+2]=0; after[index*4+3]=255;
    }
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,
                 GL_UNSIGNED_BYTE,before);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0,0); glVertex2f(-.2f,-.2f);
    glTexCoord2f(1,0); glVertex2f(.2f,-.2f);
    glTexCoord2f(.5f,1); glVertex2f(0,.2f);
    glEnd(); glFinish();
    if ((result=expect_error(GL_NO_ERROR,85))) return result;
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0,0); glVertex2f(-.2f,-.2f);
    glTexCoord2f(1,0); glVertex2f(.2f,-.2f);
    glTexCoord2f(.5f,1); glVertex2f(0,.2f);
    glEnd(); glFlush();
    if ((result=expect_error(GL_NO_ERROR,86))) return result;
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,
                    GL_UNSIGNED_BYTE,after);
    if (!Radeon3DInvalidateForTest(device)) {
        if (device) Radeon3DClose(device);
        CloseLibrary(Radeon9200Base); Radeon9200Base=NULL; return 82;
    }
    Radeon3DClose(device); CloseLibrary(Radeon9200Base); Radeon9200Base=NULL;
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0,0); glVertex2f(-.2f,-.2f);
    glTexCoord2f(1,0); glVertex2f(.2f,-.2f);
    glTexCoord2f(.5f,1); glVertex2f(0,.2f);
    glEnd(); mglSwitchDisplay();
    if ((result=expect_error(GL_INVALID_OPERATION,83))) return result;
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0,0); glVertex2f(-.2f,-.2f);
    glTexCoord2f(1,0); glVertex2f(.2f,-.2f);
    glTexCoord2f(.5f,1); glVertex2f(0,.2f);
    glEnd(); mglSwitchDisplay();
    result=expect_error(GL_NO_ERROR,84);
    glDeleteTextures(1,&texture); glDisable(GL_TEXTURE_2D);
    return result;
}

static int test_batch_budget(void)
{
    ULONG index;

    for (index = 0; index < 100UL; ++index) {
        glClearColor((GLfloat)(index & 1UL), 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glFlush();
    return expect_error(GL_NO_ERROR, 75);
}

static int test_async_submission(ULONG depth)
{
    MGLLockInfo lock;
    volatile UBYTE *pixels;
    ULONG sample = 0, byteCount, offset, byteIndex;
    GLint asyncSubmits, fenceWaits, pollHits, bufferWaits, textureWaits;
    GLint drainWaits;
    GLint executeFailures, waitFailures;
    GLenum error;
    ULONG index;

    /* Start with no inherited fence. The first glFlush must execute into one
     * slot and return without calling WaitFence. */
    glFinish();
    if (glGetError() != GL_NO_ERROR) return 120;
    glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    glGetIntegerv(MGL_PERF_ASYNC_SUBMITS, &asyncSubmits);
    glGetIntegerv(MGL_PERF_FENCE_WAITS, &fenceWaits);
    if (glGetError() != GL_NO_ERROR || asyncSubmits != 1 || fenceWaits != 0)
        return 121;

    /* Reuse both immutable record slots repeatedly. LockBack is the final
     * readback completion boundary and must observe the last (green) clear. */
    for (index = 0; index < 64UL; ++index) {
        glClearColor(0.0f, (index & 1UL) ? 1.0f : 0.0f,
                     (index & 1UL) ? 0.0f : 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glFlush();
    }
    if (!mglLockBack(&lock)) return 122;
    pixels = (volatile UBYTE *)lock.base_address;
    byteCount = depth >> 3;
    offset = 120UL * lock.pitch + 160UL * byteCount;
    for (byteIndex = 0; byteIndex < byteCount; ++byteIndex)
        sample = (sample << 8) | pixels[offset + byteIndex];
    glGetIntegerv(MGL_PERF_ASYNC_SUBMITS, &asyncSubmits);
    glGetIntegerv(MGL_PERF_FENCE_WAITS, &fenceWaits);
    glGetIntegerv(MGL_PERF_FENCE_POLL_HITS, &pollHits);
    glGetIntegerv(MGL_PERF_BUFFER_WAITS, &bufferWaits);
    glGetIntegerv(MGL_PERF_TEXTURE_WAITS, &textureWaits);
    glGetIntegerv(MGL_PERF_DRAIN_WAITS, &drainWaits);
    glGetIntegerv(MGL_PERF_EXECUTE_FAILURES, &executeFailures);
    glGetIntegerv(MGL_PERF_WAIT_FAILURES, &waitFailures);
    error = glGetError();
    printf("MINIGL_ASYNC submits=%ld waits=%ld polls=%ld buffer=%ld "
           "texture=%ld drain=%ld execfail=%ld waitfail=%ld error=%lu "
           "sample=%08lx\n",
           (long)asyncSubmits, (long)fenceWaits, (long)pollHits,
           (long)bufferWaits, (long)textureWaits, (long)drainWaits,
           (long)executeFailures, (long)waitFailures, (ULONG)error, sample);
    glHint(MGL_PERF_COUNTERS_HINT, GL_DONT_CARE);
    if (error != GL_NO_ERROR || glGetError() != GL_NO_ERROR ||
        asyncSubmits != 65 ||
        executeFailures != 0 || waitFailures != 0 ||
        textureWaits != 0 ||
        fenceWaits != bufferWaits + textureWaits + drainWaits ||
        fenceWaits + pollHits == 0 ||
        (depth == 16UL && sample != 0xe007UL) ||
        (depth == 32UL && sample != 0x00ff00ffUL))
        return 122;
    return 0;
}

static int test_texgen_state(void)
{
    glNormal3f(0.0f,0.0f,1.0f);
    glTexGeni(GL_S,GL_TEXTURE_GEN_MODE,GL_SPHERE_MAP);
    glTexGeni(GL_T,GL_TEXTURE_GEN_MODE,GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glHint(MGL_W_ONE_HINT,GL_FASTEST);
    mglLockMode(MGL_LOCK_MANUAL);
    if (!mglLockDisplay() || !glIsEnabled(GL_TEXTURE_GEN_S) ||
        !glIsEnabled(GL_TEXTURE_GEN_T))
        return 110;
    mglUnlockDisplay();
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    if (glIsEnabled(GL_TEXTURE_GEN_S) || glIsEnabled(GL_TEXTURE_GEN_T))
        return 111;
    return expect_error(GL_NO_ERROR,112);
}

static int test_deferred_state(ULONG depth)
{
    static const GLfloat leftPositions[3][3] = {
        {-0.9f,-0.8f,0.0f}, {-0.2f,-0.8f,0.0f}, {-0.55f,0.2f,0.0f}
    };
    static const GLfloat centerPositions[3][3] = {
        {-0.8f,-0.8f,0.0f}, {0.8f,-0.8f,0.0f}, {0.0f,0.8f,0.0f}
    };
    static const GLubyte red[3][4] = {
        {255,0,0,255}, {255,0,0,255}, {255,0,0,255}
    };
    static const GLubyte green[3][4] = {
        {0,255,0,255}, {0,255,0,255}, {0,255,0,255}
    };
    static const GLubyte blue[3][4] = {
        {0,0,255,128}, {0,0,255,128}, {0,0,255,128}
    };
    MGLLockInfo lock;
    volatile UBYTE *pixels;
    ULONG byteIndex,byteCount,offset,beforeRaw=0,afterRaw=0;
    GLint records, submitted, stateCalls, stateNoops, splits, merges;
    GLint snapshotBuilds, snapshotFastHits;

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glClearColor(0.0f,0.0f,0.0f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    if (glGetError()!=GL_NO_ERROR) return 130;
    glHint(MGL_PERF_COUNTERS_HINT,GL_NICEST);
    glVertexPointer(3,GL_FLOAT,0,leftPositions);
    glColorPointer(4,GL_UNSIGNED_BYTE,0,red);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDrawArrays(GL_TRIANGLES,0,3);

    /* These validated calls are redundant, and BlendFunc is frontend-only
     * while blending remains disabled. None may serialize pending geometry. */
    glBindTexture(GL_TEXTURE_2D,0);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glVertexPointer(3,GL_FLOAT,0,centerPositions);
    glColorPointer(4,GL_UNSIGNED_BYTE,0,green);
    glDrawArrays(GL_TRIANGLES,0,3);
    glDrawArrays(GL_TRIANGLES,0,3);
    glGetIntegerv(MGL_PERF_DRAW_RECORDS,&records);
    glGetIntegerv(MGL_PERF_DEFERRED_SPLITS,&splits);
    {
        GLenum error=glGetError();
        if (error!=GL_NO_ERROR || records!=0 || splits!=0) {
            printf("MINIGL_DEFERRED_PRE records=%ld splits=%ld error=%lu\n",
                   (long)records,(long)splits,(ULONG)error);
            return 131;
        }
    }

    /* A real serialized-header change remains deferred until the next draw;
     * that append must split the earlier batch, including the two identical
     * center array draws, exactly once. */
    glEnable(GL_BLEND);
    glGetIntegerv(MGL_PERF_DRAW_RECORDS,&records);
    if (records!=0) return 132;
    glColorPointer(4,GL_UNSIGNED_BYTE,0,blue);
    glDrawArrays(GL_TRIANGLES,0,3);
    glGetIntegerv(MGL_PERF_DRAW_RECORDS,&records);
    glGetIntegerv(MGL_PERF_DEFERRED_SPLITS,&splits);
    if (records!=1 || splits!=1) return 133;
    glFinish();
    glGetIntegerv(MGL_PERF_DRAW_RECORDS,&records);
    glGetIntegerv(MGL_PERF_SUBMITTED_VERTICES,&submitted);
    glGetIntegerv(MGL_PERF_STATE_CALLS,&stateCalls);
    glGetIntegerv(MGL_PERF_STATE_NOOPS,&stateNoops);
    glGetIntegerv(MGL_PERF_DRAW_MERGES,&merges);
    glGetIntegerv(MGL_PERF_SNAPSHOT_BUILDS,&snapshotBuilds);
    glGetIntegerv(MGL_PERF_SNAPSHOT_FAST_HITS,&snapshotFastHits);
    printf("MINIGL_DEFERRED records=%ld vertices=%ld state=%ld noops=%ld "
           "splits=%ld raw_merges=%ld snapshot_builds=%ld "
           "snapshot_fast_hits=%ld\n",
           (long)records,(long)submitted,(long)stateCalls,(long)stateNoops,
           (long)splits,(long)merges,(long)snapshotBuilds,
           (long)snapshotFastHits);
    if (glGetError()!=GL_NO_ERROR || records!=2 || submitted!=12 ||
        stateNoops<4 || splits!=1 || snapshotBuilds!=3 ||
        snapshotFastHits<1)
        return 134;
    glDisable(GL_BLEND);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glHint(MGL_PERF_COUNTERS_HINT,GL_DONT_CARE);
    glClearColor(0.0f,0.0f,0.0f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(0.0f,1.0f,0.0f,1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f,-0.8f); glVertex2f(0.8f,-0.8f);
    glVertex2f(0.0f,0.8f);
    glEnd();
    glFinish();
    if (!mglLockBack(&lock)) return 135;
    pixels=(volatile UBYTE *)lock.base_address;
    byteCount=depth>>3;
    offset=120UL*lock.pitch+160UL*byteCount;
    for (byteIndex=0;byteIndex<byteCount;++byteIndex)
        beforeRaw=(beforeRaw<<8)|pixels[offset+byteIndex];
    glClearColor(0.0f,0.0f,1.0f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    if (!mglLockBack(&lock)) return 135;
    pixels=(volatile UBYTE *)lock.base_address;
    for (byteIndex=0;byteIndex<byteCount;++byteIndex)
        afterRaw=(afterRaw<<8)|pixels[offset+byteIndex];
    printf("MINIGL_DEFERRED_ORDER before=%08lx after=%08lx\n",
           beforeRaw,afterRaw);
    if ((depth==16UL && (beforeRaw!=0xe007UL || afterRaw!=0x1f00UL)) ||
        (depth==32UL && (beforeRaw!=0x00ff00ffUL ||
                         afterRaw!=0xff0000ffUL)))
        return 135;
    return expect_error(GL_NO_ERROR,135);
}

static int test_target_depth(ULONG depth)
{
    MGLLockInfo lock;
    volatile UBYTE *pixels;
    ULONG offset, sample = 0, index;
    ULONG expectedFormat = depth == 8UL ? RADEON3D_FORMAT_CLUT8 :
                           depth == 16UL ? RADEON3D_FORMAT_R5G6B5PC :
                                           RADEON3D_FORMAT_B8G8R8A8;

    glClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    if (glGetError() != GL_NO_ERROR || !mglLockBack(&lock) ||
        lock.depth != depth || lock.pixel_format != expectedFormat)
        return 90;
    pixels = (volatile UBYTE *)lock.base_address;
    offset = 120UL * lock.pitch + 160UL * (depth >> 3);
    for (index = 0; index < (depth >> 3); ++index)
        sample = (sample << 8) | pixels[offset + index];
    printf("MINIGL_TARGET depth=%lu format=%lu pitch=%lu sample=%08lx\n",
           lock.depth, lock.pixel_format, lock.pitch, sample);
    if ((depth == 8UL &&
         ((((sample >> 5) & 7UL) < 1UL ||
           ((sample >> 5) & 7UL) > 2UL) ||
          (((sample >> 2) & 7UL) < 2UL ||
           ((sample >> 2) & 7UL) > 3UL) ||
          ((sample & 3UL) < 2UL))) ||
        (depth == 16UL && sample != 0x3933UL) ||
        (depth == 32UL && sample != 0xcc6633ffUL))
        return 91;
    mglSwitchDisplay();
    if (glGetError() != GL_NO_ERROR)
        return 92;
    return 0;
}

int main(int argc, char **argv)
{
    void *context;
    ULONG caps;
    int result = 0;
    BOOL fullscreen = FALSE;
    ULONG depth = 16UL;
    int width = 320, height = 240;
    int argument;

    for (argument = 1; argument < argc; ++argument) {
        if (!strcmp(argv[argument], "--fullscreen"))
            fullscreen = TRUE;
        else if (!strcmp(argv[argument], "--depth") &&
                 argument + 1 < argc)
            depth = (ULONG)atoi(argv[++argument]);
        else if (!strcmp(argv[argument], "-width") &&
                 argument + 1 < argc)
            width = atoi(argv[++argument]);
        else if (!strcmp(argv[argument], "-height") &&
                 argument + 1 < argc)
            height = atoi(argv[++argument]);
        else
            return 19;
    }
    if ((depth != 8UL && depth != 16UL && depth != 32UL) ||
        (depth == 8UL && !fullscreen))
        return 19;

    if (!MiniGLOpen()) return 20;
    caps = mglGetFastPathCapabilities();
    if (caps != (MGL_FASTPATH_POSITION | MGL_FASTPATH_COLOR |
                 MGL_FASTPATH_TEXCOORD0 | MGL_FASTPATH_TEXCOORD1 |
                 MGL_FASTPATH_TEXCOORD_Q)) {
        MiniGLClose(); return 21;
    }
    mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
    mglChoosePixelDepth((int)depth);
    context = mglCreateContext(0, 0, width, height);
    if (!context) { MiniGLClose(); return 22; }
    result = test_target_depth(depth);
    if (!result) result = test_deferred_state(depth);
    if (!result) result = test_texgen_state();
    if (fullscreen) {
        if (!result) {
            mglResizeContext(width, height);
            result = expect_error(GL_INVALID_OPERATION, 70);
        }
    } else {
        IntuitionBase = (struct IntuitionBase *)
                            OpenLibrary("intuition.library", 39);
        if (!IntuitionBase) result = 23;
        if (!result) result = test_arrays();
        if (!result) result = test_fastpath();
        if (!result) result = test_window_resize();
        if (!result) result = test_batch_budget();
        if (!result) result = test_async_submission(depth);
        if (!result) result = test_context_loss();
    }
    mglDeleteContext();
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    MiniGLClose();
    if (!result && fullscreen)
        printf("MINIGL_PHASE6_ACCEPT depth=%lu fullscreen_resize=rejected\n",
               depth);
    else if (!result)
        printf("MINIGL_PHASE6_ACCEPT depth=%lu deferred_state=pass arrays=pass fastpath=v2 "
                "window=move,overlap resize=pass batch_budget=pass "
                "async=pass context_loss=%s fullscreen=not-run\n",
               depth, contextLossTested ? "recovered" : "diagnostic-only");
    return result;
}
