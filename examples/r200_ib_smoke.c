#include <stdio.h>
#include <proto/minigl.h>

/* The DLL performs the optional startup smoke before returning a context.
 * No rendering, textures or frame loop can obscure that acknowledgment. */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("IBCLIENT open");
    if (!MiniGLOpen()) {
        puts("IBCLIENT open_failed");
        return 20;
    }
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(16);
    puts("IBCLIENT create");
    if (!mglCreateContext(0, 0, 160, 120)) {
        puts("IBCLIENT create_failed; inspect DH2:ibppc.log before retrying");
        MiniGLClose();
        return 20;
    }
    puts("IBCLIENT context_ok draws=0");
    mglDeleteContext();
    MiniGLClose();
    puts("IBCLIENT closed");
    return 0;
}
