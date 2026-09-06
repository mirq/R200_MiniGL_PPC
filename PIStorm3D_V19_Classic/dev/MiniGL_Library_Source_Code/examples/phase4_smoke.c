#include <dos/dos.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

#define PI 3.14159265358979323846

static void draw_gear(GLfloat inner, GLfloat outer, int teeth,
                      GLubyte red, GLubyte green, GLubyte blue)
{
    int tooth;
    glColor3ub(red, green, blue);
    glBegin(GL_QUADS);
    for (tooth = 0; tooth < teeth; ++tooth) {
        GLfloat a0 = (GLfloat)(2.0 * PI * tooth / teeth);
        GLfloat a1 = (GLfloat)(2.0 * PI * (tooth + 0.35) / teeth);
        GLfloat a2 = (GLfloat)(2.0 * PI * (tooth + 0.65) / teeth);
        GLfloat a3 = (GLfloat)(2.0 * PI * (tooth + 1.0) / teeth);
        GLfloat root = outer * 0.78f;
        glVertex3f(inner*(GLfloat)cos(a0), inner*(GLfloat)sin(a0), 0.0f);
        glVertex3f(root*(GLfloat)cos(a0), root*(GLfloat)sin(a0), 0.0f);
        glVertex3f(outer*(GLfloat)cos(a1), outer*(GLfloat)sin(a1), 0.0f);
        glVertex3f(inner*(GLfloat)cos(a1), inner*(GLfloat)sin(a1), 0.0f);
        glVertex3f(inner*(GLfloat)cos(a1), inner*(GLfloat)sin(a1), 0.0f);
        glVertex3f(outer*(GLfloat)cos(a1), outer*(GLfloat)sin(a1), 0.0f);
        glVertex3f(outer*(GLfloat)cos(a2), outer*(GLfloat)sin(a2), 0.0f);
        glVertex3f(inner*(GLfloat)cos(a2), inner*(GLfloat)sin(a2), 0.0f);
        glVertex3f(inner*(GLfloat)cos(a2), inner*(GLfloat)sin(a2), 0.0f);
        glVertex3f(outer*(GLfloat)cos(a2), outer*(GLfloat)sin(a2), 0.0f);
        glVertex3f(root*(GLfloat)cos(a3), root*(GLfloat)sin(a3), 0.0f);
        glVertex3f(inner*(GLfloat)cos(a3), inner*(GLfloat)sin(a3), 0.0f);
    }
    glEnd();
}

static void draw_array_fixtures(void)
{
    static const GLfloat triangles[] = {
        -3.8f,-2.6f,0.5f, -2.8f,-2.6f,0.5f, -3.3f,-1.8f,0.5f,
         2.8f,-2.6f,0.5f,  3.8f,-2.6f,0.5f,  3.3f,-1.8f,0.5f
    };
    static const GLubyte colors[] = {
        255,80,80, 255,220,40, 255,80,200,
        40,180,255, 80,255,120, 180,80,255
    };
    static const GLfloat quad[] = {
        -0.8f,-3.0f,1.0f, 0.8f,-3.0f,1.0f,
         0.8f,-2.4f,1.0f,-0.8f,-2.4f,1.0f
    };
    static const GLubyte white[] = {
        220,220,220, 220,220,220, 220,220,220, 220,220,220
    };
    static const GLushort indices[] = {0,1,2,3};
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3,GL_FLOAT,0,triangles);
    glColorPointer(3,GL_UNSIGNED_BYTE,0,colors);
    glDrawArrays(GL_TRIANGLES,0,6);
    glVertexPointer(3,GL_FLOAT,0,quad);
    glColorPointer(3,GL_UNSIGNED_BYTE,0,white);
    glDrawElements(GL_QUADS,4,GL_UNSIGNED_SHORT,indices);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static int check_depth_functions(void)
{
    static const GLenum functions[] = {
        GL_NEVER,GL_LESS,GL_EQUAL,GL_LEQUAL,GL_GREATER,GL_NOTEQUAL,
        GL_GEQUAL,GL_ALWAYS
    };
    int index;
    glColor3ub(255,255,255);
    for (index=0; index<8; ++index) {
        glDepthFunc(functions[index]);
        glBegin(GL_TRIANGLES);
        glVertex3f(-0.95f,-0.95f,0); glVertex3f(-0.85f,-0.95f,0);
        glVertex3f(-0.9f,-0.85f,0); glEnd();
        glFinish();
        if (glGetError()!=GL_NO_ERROR) {
            printf("MINIGL_PHASE4 depth_func_index=%ld rejected\n",(long)index);
            return 0;
        }
    }
    glDepthFunc(GL_LESS);
    return glGetError() == GL_NO_ERROR;
}

static UWORD read_pixel(const MGLLockInfo *info, ULONG x, ULONG y)
{
    volatile UBYTE *pixel=(volatile UBYTE *)info->base_address+y*info->pitch+x*2;
    return (UWORD)(((UWORD)pixel[0]<<8)|pixel[1]);
}

static int verify_depth(void)
{
    MGLLockInfo info;
    UWORD center, transformed, clipped;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDepthFunc(GL_LESS); glClearColor(0,0,0,1); glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glColor3ub(255,0,0); glBegin(GL_TRIANGLES);
    glVertex3f(-0.6f,-0.6f,-0.5f); glVertex3f(0.6f,-0.6f,-0.5f);
    glVertex3f(0,0.6f,-0.5f); glEnd();
    glColor3ub(0,255,0); glBegin(GL_TRIANGLES);
    glVertex3f(-0.6f,-0.6f,0.5f); glVertex3f(0.6f,-0.6f,0.5f);
    glVertex3f(0,0.6f,0.5f); glEnd();
    glDisable(GL_DEPTH_TEST);
    glLoadIdentity(); glTranslatef(0.5f,0,0);
    glColor3ub(0,0,255); glBegin(GL_TRIANGLES);
    glVertex3f(-0.2f,-0.2f,0); glVertex3f(0.2f,-0.2f,0);
    glVertex3f(0,0.2f,0); glEnd();
    glLoadIdentity(); glColor3ub(255,0,255); glBegin(GL_TRIANGLES);
    glVertex3f(-2.0f,0.6f,0); glVertex3f(0.3f,0.6f,0);
    glVertex3f(0.3f,0.9f,0); glEnd();
    glEnable(GL_DEPTH_TEST);
    glFinish();
    if (!mglLockBack(&info)) return 0;
    center=read_pixel(&info,info.width/2,info.height/2);
    transformed=read_pixel(&info,info.width*3/4,info.height/2);
    clipped=read_pixel(&info,1,80);
    printf("MINIGL_PHASE4_VERIFY depth=%04lx transformed=%04lx clipped=%04lx\n",
           (unsigned long)center,(unsigned long)transformed,
           (unsigned long)clipped);
    return center==0x00f8U && transformed==0x1f00U && clipped==0x1ff8U;
}

static int verify_matrix_errors(void)
{
    int index;
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glPopMatrix();
    if (glGetError()!=GL_STACK_UNDERFLOW) return 0;
    for (index=0; index<5; ++index) glPushMatrix();
    glPushMatrix();
    if (glGetError()!=GL_STACK_OVERFLOW) return 0;
    for (index=0; index<5; ++index) glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    return glGetError()==GL_NO_ERROR;
}

int main(int argc, char **argv)
{
    GLboolean window = GL_TRUE;
    int frame, frames;
    void *context;
    if (argc==2 && !strcmp(argv[1],"--fullscreen")) window=GL_FALSE;
    else if (argc!=1) { printf("Usage: minigl_phase4 [--fullscreen]\n"); return 10; }
    frames = window ? 12 : 80;
    if (!MiniGLOpen()) return 20;
    mglChooseWindowMode(window); mglChoosePixelDepth(16);
    context=mglCreateContext(0,0,640,480);
    if (!context) { printf("MINIGL_PHASE4 context=fail\n"); MiniGLClose(); return 20; }
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glClearDepth(1.0);
    if (!check_depth_functions()) {
        printf("MINIGL_PHASE4 depth_funcs=fail\n");
        mglDeleteContext(); MiniGLClose(); return 20;
    }
    if (!verify_depth()) {
        printf("MINIGL_PHASE4 depth_verify=fail\n");
        mglDeleteContext(); MiniGLClose(); return 20;
    }
    if (!verify_matrix_errors()) {
        printf("MINIGL_PHASE4 matrix_stack=fail\n");
        mglDeleteContext(); MiniGLClose(); return 20;
    }
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(48.0f,1.3333333f,1.0f,30.0f);
    for (frame=0; frame<frames; ++frame) {
        glClearColor(0.025f,0.04f,0.07f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        gluLookAt(0,0,11,0,0,0,0,1,0);
        glPushMatrix(); glTranslatef(-2.3f,0.5f,1.0f);
        glRotatef((GLfloat)frame*2.0f,0,0,1);
        draw_gear(0.55f,2.0f,16,235,70,45); glPopMatrix();
        glPushMatrix(); glTranslatef(2.2f,0.7f,0.0f);
        glRotatef((GLfloat)frame*-2.7f,0,0,1);
        draw_gear(0.45f,1.55f,12,55,190,95); glPopMatrix();
        glPushMatrix(); glTranslatef(0.6f,-2.1f,2.0f);
        glRotatef((GLfloat)frame*3.2f,0,0,1);
        draw_gear(0.35f,1.2f,10,55,120,245); glPopMatrix();
        draw_array_fixtures();
        glColor3ub(255,40,220); glBegin(GL_TRIANGLES);
        glVertex3f(-8.0f,2.8f,0.0f); glVertex3f(-2.0f,3.5f,0.0f);
        glVertex3f(-2.0f,1.9f,0.0f); glEnd();
        mglSwitchDisplay();
        if (glGetError()!=GL_NO_ERROR) {
            printf("MINIGL_PHASE4 render=fail frame=%ld\n",(long)frame);
            mglDeleteContext(); MiniGLClose(); return 20;
        }
        Delay(1);
    }
    mglDeleteContext(); MiniGLClose();
    printf("MINIGL_PHASE4 mode=%s transforms=pass clipping=pass arrays=pass indexed=pass depth=pass frames=%ld\n",
           window?"window":"fullscreen",(long)frames);
    return 0;
}
