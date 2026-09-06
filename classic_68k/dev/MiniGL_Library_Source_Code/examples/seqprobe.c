/*
 * Minimal sequencing probe: why does early geometry not reach the surface
 * that mglLockBack() hands out? One windowed context, four stages, each
 * preceded by a clear. Samples the same pixel after every stage.
 */
#include <stdio.h>
#include <libraries/minigl_perf.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static MGLLockInfo lock;

static UWORD sample(void)
{
    UBYTE *base = (UBYTE *)lock.base_address;
    ULONG x = lock.width / 4UL;
    ULONG y = lock.height / 4UL;

    return *(UWORD *)(base + y * lock.pitch + x * 2UL);
}

static void stage(const char *name)
{
    GLint submits, waits, polls, drains;

    glFlush();
    if (!mglLockBack(&lock)) {
        printf("seqprobe: %s LOCK FAILED err=%ld\n", name,
               (long)glGetError());
        return;
    }
    glGetIntegerv(MGL_PERF_SUBMITS, &submits);
    glGetIntegerv(MGL_PERF_FENCE_WAITS, &waits);
    glGetIntegerv(MGL_PERF_FENCE_POLL_HITS, &polls);
    glGetIntegerv(MGL_PERF_DRAIN_WAITS, &drains);
    printf("seqprobe: %s sample=%04x submits=%ld fwaits=%ld polls=%ld "
           "drains=%ld err=%ld\n", name, sample(), (long)submits,
           (long)waits, (long)polls, (long)drains, (long)glGetError());
}

static void busy_wait(volatile ULONG iterations)
{
    while (iterations--) ;
}

static void white_fan(void)
{
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(MGL_FLATFAN);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(160.0f, 0.0f);
    glVertex2f(160.0f, 120.0f);
    glVertex2f(0.0f, 120.0f);
    glEnd();
}

int main(void)
{
    GLubyte texels[4] = {255, 255, 255, 255};

    if (!MiniGLOpen()) return 21;
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240)) { MiniGLClose(); return 22; }
    glViewport(0, 0, 320, 240);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glHint(MGL_PERF_COUNTERS_HINT, GL_NICEST);

    /* S1: very first geometry submission. */
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    stage("s1-first");
    stage("s1e-relock1");
    stage("s1e-relock2");
    {
        /* Latency curve: re-read the already-locked surface over ~5 s with
         * no new submits, no new locks. Prints the sample as it changes. */
        int step;
        UWORD last = sample();
        for (step = 0; step < 20; ++step) {
            UWORD now;
            busy_wait(2000000UL);
            now = sample();
            if (now != last) {
                printf("seqprobe: s1f-t%d sample=%04x (was %04x)\n",
                       step, now, last);
                last = now;
            }
        }
        printf("seqprobe: s1f-final sample=%04x\n", last);
    }

    /* S1b: same, but give the GPU half a second before reading. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    glFlush();
    busy_wait(4000000UL);
    stage("s1b-delayed");

    /* S1c: time only, no new submit between draw and read. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    glFlush();
    busy_wait(4000000UL);
    stage("s1c-timeonly");

    /* S1d: lock twice back to back, no new work. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    glFlush();
    stage("s1d-lock1");
    stage("s1d-lock2");

    /* S2: identical second attempt without anything in between. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    stage("s2-second");

    /* S3: present via SwitchDisplay before locking. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    mglSwitchDisplay();
    stage("s3-switched");

    /* S4: dummy texture upload first, then draw. */
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    stage("s4-textured");

    /* S5: two draws in one frame. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    white_fan();
    stage("s5-twice");

    /* S6: warm-up flush first (a throwaway offscreen fan), then the real
     * draw. If s6 works where s1 failed, exactly one flush is swallowed. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor4f(1, 1, 1, 1);
    glBegin(MGL_FLATFAN);
    glVertex2f(-10.0f, -10.0f);
    glVertex2f(-8.0f, -10.0f);
    glVertex2f(-8.0f, -8.0f);
    glEnd();
    glFlush();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    stage("s6-warmup");

    /* S7: fresh context, SwitchDisplay before any drawing. */
    mglDeleteContext();
    if (!mglCreateContext(0, 0, 320, 240)) { MiniGLClose(); return 26; }
    glViewport(0, 0, 320, 240);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    mglSwitchDisplay();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    white_fan();
    stage("s7-preswitch");

    mglDeleteContext();
    MiniGLClose();
    return 0;
}
