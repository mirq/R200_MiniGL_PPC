#include <stdio.h>
#include <proto/minigl.h>

int main(void)
{
    void *ctx;
    const GLubyte *renderer;

    setvbuf(stdout, NULL, _IONBF, 0);

    puts("Opening minigl.library...");
    if (!MiniGLOpen()) {
        puts("Could not open minigl.library v4");
        return 20;
    }
    puts("MiniGL DLL opened.");

    mglChooseVertexBufferSize(1000);
    puts("Creating MiniGL context...");
    ctx = mglCreateContext(0, 0, 640, 480);
    if (!ctx) {
        puts("Could not create MiniGL context");
        MiniGLClose();
        return 20;
    }
    puts("MiniGL context created.");

    renderer = glGetString(GL_RENDERER);
    printf("Renderer: %s\n", renderer ? (const char *)renderer : "<NULL>");

    glViewport(0, 0, 640, 480);
    glClearColor(0.1f, 0.2f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    puts("Switching display...");
    mglSwitchDisplay();
    puts("Display switched.");

    puts("Deleting MiniGL context...");
    mglDeleteContext();
    MiniGLClose();
    puts("Done.");
    return 0;
}
