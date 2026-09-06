#include <dos/dos.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static void vertex(GLubyte r,GLubyte g,GLubyte b,GLfloat x,GLfloat y)
{
    glColor3ub(r,g,b); glVertex2f(x,y);
}

static void solid(GLubyte *image, GLubyte r, GLubyte g, GLubyte b)
{
    int i;
    for (i=0;i<8*8;++i) {
        image[i*4]=r; image[i*4+1]=g; image[i*4+2]=b; image[i*4+3]=255;
    }
}

static void multitex_quad(void)
{
    GLfloat last[2]={0.0f,1.0f};
    glBegin(GL_QUADS);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,0,0);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB,0,0); glVertex3f(-.65f,-.65f,-.15f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,1,0);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB,2,0); glVertex3f(.65f,-.65f,-.35f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,1,1);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB,2,2); glVertex3f(.65f,.35f,-.8f);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,0,1);
    glMultiTexCoord2fvARB(GL_TEXTURE1_ARB,last); glVertex3f(-.65f,.35f,-.55f);
    glEnd();
}

int main(int argc,char **argv)
{
    int frame,frameCount; GLuint textures[2]; GLubyte image[8*8*4];
    GLboolean fullscreen=GL_FALSE;
    GLfloat fogColor[4]={0.12f,0.18f,0.3f,1.0f};
    int width=640,height=480,depth=16,arg;
    for(arg=1;arg<argc;++arg) {
        if(!strcmp(argv[arg],"--fullscreen")) fullscreen=GL_TRUE;
        else if(!strcmp(argv[arg],"-width")&&arg+1<argc) width=atoi(argv[++arg]);
        else if(!strcmp(argv[arg],"-height")&&arg+1<argc) height=atoi(argv[++arg]);
        else if(!strcmp(argv[arg],"-depth")&&arg+1<argc) depth=atoi(argv[++arg]);
        else return 10;
    }
    if (!MiniGLOpen()) return 21;
    mglChooseWindowMode(fullscreen?GL_FALSE:GL_TRUE); mglChoosePixelDepth(depth);
    if (!mglCreateContext(0,0,width,height)) { MiniGLClose(); return 22; }
    glGenTextures(2,textures);
    glActiveTextureARB(GL_TEXTURE0_ARB); glBindTexture(GL_TEXTURE_2D,textures[0]);
    solid(image,255,180,80);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE); glEnable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE1_ARB); glBindTexture(GL_TEXTURE_2D,textures[1]);
    solid(image,80,180,255);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glEnable(GL_TEXTURE_2D);
    if (!glIsEnabled(GL_TEXTURE_2D)) return 24;
    glActiveTextureARB(GL_TEXTURE0_ARB);
    if (!glIsEnabled(GL_TEXTURE_2D)) return 25;
    glFogfv(GL_FOG_COLOR,fogColor); glFogf(GL_FOG_START,0.0f);
    glFogf(GL_FOG_END,1.0f); glFogf(GL_FOG_DENSITY,1.35f); glEnable(GL_FOG);
    frameCount=fullscreen?80:20;
    for (frame=0;frame<frameCount;++frame) {
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.02f,0.03f,0.06f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
        glScissor(80,60,480,360); glEnable(GL_SCISSOR_TEST);
        glClearColor(0.08f,0.12f,0.18f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
        if (frame%3==0) glFogi(GL_FOG_MODE,GL_LINEAR);
        else if (frame%3==1) glFogi(GL_FOG_MODE,GL_EXP);
        else glFogi(GL_FOG_MODE,GL_EXP2);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,
                  (frame&1)?GL_REPLACE:GL_MODULATE);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glColor3ub(255,255,255); multitex_quad();
        glPointSize(12.0f);
        if (glGetError()!=GL_NO_ERROR) { mglDeleteContext(); MiniGLClose(); return 30; }
        glBegin(GL_POINTS);
        vertex(255,64,64,-0.75f,0.55f); vertex(64,255,96,0.0f,0.55f);
        vertex(64,128,255,0.75f,0.55f); glEnd();
        if (glGetError()!=GL_NO_ERROR) { mglDeleteContext(); MiniGLClose(); return 31; }
        glBegin(GL_LINES);
        vertex(255,255,64,-0.9f,0.1f); vertex(255,64,255,0.9f,0.1f); glEnd();
        if (glGetError()!=GL_NO_ERROR) { mglDeleteContext(); MiniGLClose(); return 32; }
        glBegin(GL_LINE_STRIP);
        vertex(64,255,255,-0.8f,-0.2f); vertex(255,255,255,-0.2f,-0.65f);
        vertex(255,128,32,0.35f,-0.2f); vertex(128,64,255,0.8f,-0.65f); glEnd();
        if (glGetError()!=GL_NO_ERROR) { mglDeleteContext(); MiniGLClose(); return 33; }
        glBegin(GL_LINE_LOOP);
        vertex(255,80,80,-1.2f,-0.8f); vertex(80,255,80,0.0f,0.85f);
        vertex(80,80,255,1.2f,-0.8f); glEnd();
        if (glGetError()!=GL_NO_ERROR) { mglDeleteContext(); MiniGLClose(); return 34; }
        mglSwitchDisplay();
        if (glGetError()!=GL_NO_ERROR) { mglDeleteContext(); MiniGLClose(); return 23; }
        Delay(1);
    }
    glDeleteTextures(2,textures);
    mglDeleteContext(); MiniGLClose();
    printf("MINIGL_PHASE6 mode=%s fog=linear,exp,exp2 multitex=pass env1=modulate,replace scissor=pass points=pass lines=pass frames=%ld\n",
           fullscreen?"fullscreen":"window",(long)frameCount);
    return 0;
}
