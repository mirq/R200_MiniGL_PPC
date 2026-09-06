#include <exec/types.h>
#include <stdio.h>

#include <proto/minigl.h>

int main(void)
{
    static const GLubyte texturePixels[12] = {
        255, 0, 0, 0, 255, 0,
        0, 0, 255, 255, 255, 255
    };
    GLuint texture;
    if (!MiniGLOpen()) {
        printf("FAIL: cannot initialize PPC MiniGL bridge API\n");
        return 20;
    }
    mglChooseWindowMode(GL_TRUE);
    mglChoosePixelDepth(32);
    mglChooseVertexBufferSize(1000);
    if (!mglCreateContext(0, 0, 320, 240)) {
        printf("FAIL: PPC MiniGL API context creation failed\n");
        MiniGLClose();
        return 20;
    }

    glViewport(0, 0, 320, 240);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.75, 0.75, 1.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -2.0f);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, texturePixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.03f, 0.05f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glColor3f(1.0f, 0.2f, 0.1f);
    glVertex3f(-0.7f, -0.6f, 0.0f);
    glColor3f(0.2f, 1.0f, 0.2f);
    glVertex3f(0.7f, -0.6f, 0.0f);
    glColor3f(0.2f, 0.4f, 1.0f);
    glVertex3f(0.0f, 0.7f, 0.0f);
    glEnd();
    mglSwitchDisplay();

    if (glGetError() != GL_NO_ERROR) {
        printf("FAIL: PPC MiniGL standard API reported a GL error\n");
        mglDeleteContext();
        MiniGLClose();
        return 20;
    }
    glDeleteTextures(1, &texture);
    mglDeleteContext();
    MiniGLClose();
    printf("OK: PPC MiniGL standard API smoke completed\n");
    return 0;
}
