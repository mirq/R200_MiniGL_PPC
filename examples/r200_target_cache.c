/* Hardware readback probe: queue several frames before reading the live target. */
#include <proto/minigl.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "window";
    int flip = !strcmp(mode, "flip");
    int window = !strcmp(mode, "window");
    int context, group, frame, sequence, checks = 0, failures = 0;
    int result = 20, live = 0;
    static const unsigned char colors[3][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}
    };

    if (argc > 2 || (!window && !flip && strcmp(mode, "blit"))) {
        puts("usage: r200_target_cache [window|blit|flip]");
        return 20;
    }
    if (!MiniGLOpen())
        return 20;
    for (context = 0; context < 2; ++context) {
        mglChooseWindowMode(window ? GL_TRUE : GL_FALSE);
        mglChoosePixelDepth(32);
        mglChooseNumberOfBuffers(2);
        if (!mglCreateContext(0, 0, 640, 480))
            goto out;
        live = 1;
        mglEnableSync(flip ? GL_TRUE : GL_FALSE);
        glViewport(0, 0, 640, 480);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        sequence = 0;
        for (group = 0; group < 3; ++group) {
            GLubyte pixel[4] = {0};
            GLenum error;
            int expected;
            for (frame = 0; frame < 16; ++frame, ++sequence) {
                const unsigned char *color = colors[sequence % 3];
                glClearColor(color[0] / 255.0f, color[1] / 255.0f,
                             color[2] / 255.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                /* A separate draw entry must share the clear's target. Keep
                 * it away from the sample at the centre of the screen. */
                glColor3f(1.0f, 1.0f, 1.0f);
                glBegin(GL_TRIANGLES);
                glVertex3f(-0.9f, -0.9f, 0.0f);
                glVertex3f(-0.7f, -0.9f, 0.0f);
                glVertex3f(-0.9f, -0.7f, 0.0f);
                glEnd();
                mglSwitchDisplay();
            }
            glFinish();
            glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            error = glGetError();
            /* Synchronous double-buffer FLIP leaves the previous frame in
             * the new back buffer; BLIT/window keep the last drawn target. */
            expected = (sequence - (flip ? 2 : 1)) % 3;
            ++checks;
            if (error != GL_NO_ERROR ||
                pixel[0] != colors[expected][0] ||
                pixel[1] != colors[expected][1] ||
                pixel[2] != colors[expected][2])
                ++failures;
            printf("TARGET_CACHE mode=%s context=%d frames=%d "
                   "rgb=%02x%02x%02x expected=%02x%02x%02x error=%lu\n",
                   mode, context + 1, sequence,
                   pixel[0], pixel[1], pixel[2], colors[expected][0],
                   colors[expected][1], colors[expected][2],
                   (unsigned long)error);
            if (error != GL_NO_ERROR)
                goto out;
        }
        mglDeleteContext();
        live = 0;
    }
    result = failures ? 5 : 0;
out:
    if (live)
        mglDeleteContext();
    MiniGLClose();
    printf("TARGET_CACHE result=%d checks=%d failures=%d\n",
           result, checks, failures);
    return result;
}
