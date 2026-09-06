#include <stdio.h>

#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

#ifdef MINIGLPPC_SEMANTIC_PROFILE
#include <libraries/minigl_ppc_semantic_profile.h>

static ULONG ProfileTicksToMicros(ULONG ticks, ULONG rate)
{
    if (!rate) return 0;
    return (ULONG)(((unsigned long long)ticks * 1000000ULL) / rate);
}
#endif

int main(void)
{
    static const UBYTE checker[4 * 4 * 4] = {
        255, 255, 255, 255,  32, 64, 255, 255, 255, 255, 255, 255,  32, 64, 255, 255,
         32, 64, 255, 255, 255, 255, 255, 255,  32, 64, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255,  32, 64, 255, 255, 255, 255, 255, 255,  32, 64, 255, 255,
         32, 64, 255, 255, 255, 255, 255, 255,  32, 64, 255, 255, 255, 255, 255, 255
    };
    static const UBYTE rgbPadded[2 * 12] = {
        255, 32, 32,  32, 255, 32,  32, 32, 255,  0, 0, 0,
         32, 32, 255, 255, 255, 32, 255, 32, 32,   0, 0, 0
    };
    static UBYTE largeTexture[64 * 64 * 4];
    ULONG frame;
    ULONG pixelIndex;
    GLuint texture;
    UBYTE pixel[4];

    if (!MiniGLOpen()) {
        printf("PPC_SEMANTIC_LIBRARY open=fail\n");
        return 20;
    }
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(32);
    if (!mglCreateContext(0, 0, 320, 240)) {
        printf("PPC_SEMANTIC_LIBRARY context=fail\n");
        MiniGLClose();
        return 20;
    }
    if (!mglGetWindowHandle() || mglCreateContext(0, 0, 64, 64)) {
        printf("PPC_SEMANTIC_LIBRARY lifecycle=fail\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }

    glEnable(GL_DEPTH_TEST);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, checker);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 3, 2, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, rgbPadded);
    for (pixelIndex = 0; pixelIndex < 64UL * 64UL; ++pixelIndex) {
        ULONG color = ((pixelIndex & 8UL) ^ ((pixelIndex >> 6) & 8UL))
            ? 224UL : 48UL;
        largeTexture[pixelIndex * 4UL + 0UL] = (UBYTE)color;
        largeTexture[pixelIndex * 4UL + 1UL] = (UBYTE)(255UL - color);
        largeTexture[pixelIndex * 4UL + 2UL] = 160;
        largeTexture[pixelIndex * 4UL + 3UL] = 255;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, largeTexture);
    glEnable(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR) {
        printf("PPC_SEMANTIC_LIBRARY texture=fail\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.75, 0.75, 1.0, 20.0);
    for (frame = 0; frame < 120UL; ++frame) {
        glClearColor(0.02f, 0.03f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -3.0f);
        glRotatef((GLfloat)frame * 3.0f, 0.0f, 0.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glTexCoord2f(0.0f, 0.0f);
        glColor3ub(255, 64, 32);
        glVertex3f(-0.7f, -0.6f, 0.0f);
        glTexCoord2f(1.0f, 0.0f);
        glColor3ub(32, 255, 96);
        glVertex3f(0.7f, -0.6f, 0.0f);
        glTexCoord2f(0.5f, 1.0f);
        glColor3ub(64, 128, 255);
        glVertex3f(0.0f, 0.7f, 0.0f);
        glEnd();
        mglSwitchDisplay();
        {
            GLenum frameError = glGetError();
            if (frameError != GL_NO_ERROR) {
                printf("PPC_SEMANTIC_LIBRARY frame=%lu error=%lu\n",
                       (unsigned long)frame, (unsigned long)frameError);
                fflush(stdout);
                mglDeleteContext();
                MiniGLClose();
                return 20;
            }
        }
    }

    glFinish();
    if (glGetError() != GL_NO_ERROR) {
        printf("PPC_SEMANTIC_LIBRARY finish=fail\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }
    glDeleteTextures(1, &texture);
    if (glGetError() != GL_NO_ERROR) {
        printf("PPC_SEMANTIC_LIBRARY delete=fail\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (glGetError() == GL_NO_ERROR) {
        printf("PPC_SEMANTIC_LIBRARY readback=unexpected-support\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }
    if (mglLockBack(NULL) || glGetError() == GL_NO_ERROR) {
        printf("PPC_SEMANTIC_LIBRARY lockback=fail\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }

    mglDeleteContext();
    MiniGLClose();
    MiniGLClose();
#ifdef MINIGLPPC_SEMANTIC_PROFILE
    {
        const MGLPPCSemanticPlatformProfile *profile =
            R200PPCPlatformProfile();
        ULONG rate = profile->clockRate;
        printf("MGLPPC_SEMANTIC_PROFILE publishes=%lu send_us=%lu "
               "crc_us=%lu flush_us=%lu waits=%lu wait_us=%lu "
               "wait_max_us=%lu barriers=%lu barrier_us=%lu "
               "controls=%lu control_us=%lu chunks=%lu chunk_us=%lu "
               "ring_stall_us=%lu drain_us=%lu "
               "session_us=%lu tb_hz=%lu\n",
               (unsigned long)profile->publishCount,
               (unsigned long)ProfileTicksToMicros(profile->publishSendTicks, rate),
               (unsigned long)ProfileTicksToMicros(profile->publishCrcTicks, rate),
               (unsigned long)ProfileTicksToMicros(profile->publishFlushTicks, rate),
               (unsigned long)profile->replyWaitCount,
               (unsigned long)ProfileTicksToMicros(profile->replyWaitTicks, rate),
               (unsigned long)ProfileTicksToMicros(profile->replyWaitMaxTicks, rate),
               (unsigned long)profile->barrierCount,
               (unsigned long)ProfileTicksToMicros(profile->barrierTicks, rate),
               (unsigned long)profile->controlCount,
               (unsigned long)ProfileTicksToMicros(profile->controlTicks, rate),
               (unsigned long)profile->chunkCount,
               (unsigned long)ProfileTicksToMicros(profile->chunkTicks, rate),
               (unsigned long)ProfileTicksToMicros(profile->ringStallTicks, rate),
               (unsigned long)ProfileTicksToMicros(profile->drainTicks, rate),
               (unsigned long)ProfileTicksToMicros(profile->sessionTicks, rate),
               (unsigned long)rate);
        fflush(stdout);
    }
#endif
    printf("PPC_SEMANTIC_LIBRARY status=ok frames=120\n");
    return 0;
}
