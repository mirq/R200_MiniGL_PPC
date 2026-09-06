#include <stdio.h>
#include <string.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <libraries/minigl_perf.h>

static UBYTE baseImage[4*4*4];
static UBYTE lightImage[4*4*4];
static UBYTE mipImage[8*8*4];

static void fill_level(UBYTE *image,ULONG width,ULONG height,
                       UBYTE r,UBYTE g,UBYTE b)
{
    ULONG i;
    for (i=0;i<width*height;++i) {
        image[i*4]=r; image[i*4+1]=g; image[i*4+2]=b; image[i*4+3]=255;
    }
}

static void fill_base_pattern(UBYTE *image)
{
    static const UBYTE colors[8][3] = {
        {255,0,0},{0,255,0},{0,0,255},{255,255,255},
        {255,255,0},{0,255,255},{255,0,255},{64,64,64}
    };
    ULONG x,y;
    for (y=0;y<8;++y) for (x=0;x<8;++x) {
        ULONG p=(y*8+x)*4;
        image[p]=colors[x][0]; image[p+1]=colors[x][1];
        image[p+2]=colors[x][2]; image[p+3]=255;
    }
}

static void make_textures(void)
{
    ULONG x,y;
    for (y=0;y<4;++y) for (x=0;x<4;++x) {
        ULONG p=(y*4+x)*4;
        static const UBYTE colors[4][3] = {
            {255,0,0},{0,255,0},{0,0,255},{255,255,255}
        };
        baseImage[p]=colors[x][0]; baseImage[p+1]=colors[x][1];
        baseImage[p+2]=colors[x][2];
        baseImage[p+3]=255;
        lightImage[p]=lightImage[p+1]=lightImage[p+2]=192;
        lightImage[p+3]=255;
    }
}

static void vertex(GLfloat x,GLfloat y,GLfloat z,GLfloat s,GLfloat t)
{
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,s,t);
    glMultiTexCoord2fARB(GL_TEXTURE1_ARB,s,t);
    glVertex3f(x,y,z);
}

int main(void)
{
    void *context;
    GLuint texture[2];
    GLint failures=0,waitFailures=0,clamps=0;
    UBYTE sample[4][4],repeat[4][4],mipBase[4],mipNear[4],mipFar[4];
    ULONG i;

    if (!MiniGLOpen()) return 20;
    mglChooseWindowMode(GL_FALSE);
    mglChoosePixelDepth(32);
    context=mglCreateContext(0,0,640,480);
    if (!context) { MiniGLClose(); return 21; }
    glHint(MGL_PERF_COUNTERS_HINT,GL_NICEST);
    glViewport(0,0,640,480);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-1,1,-0.75,0.75,1,100);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    make_textures(); glGenTextures(2,texture);

    glActiveTextureARB(GL_TEXTURE0_ARB); glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,texture[0]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,
                 baseImage);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glActiveTextureARB(GL_TEXTURE1_ARB); glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,texture[1]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,
                 lightImage);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);

    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    /* A screen-aligned rectangle whose right edge is four times farther
     * away. Correct perspective maps center X to s=0.2, not affine s=0.5. */
    glBegin(GL_TRIANGLES);
    vertex(-1,-0.75f,-2,0,0); vertex(4,-3,-8,1,0);
    vertex(4,3,-8,1,1);
    vertex(-1,-0.75f,-2,0,0); vertex(4,3,-8,1,1);
    vertex(-1,0.75f,-2,0,1);
    glEnd();
    glFinish();
    for (i=0;i<4;++i)
        glReadPixels(200+(GLint)i*80,240,1,1,GL_RGBA,GL_UNSIGNED_BYTE,
                     sample[i]);

    /* Distinct solid colors per mip level expose both LOD selection and
     * physical level offsets. */
    glActiveTextureARB(GL_TEXTURE1_ARB); glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB); glBindTexture(GL_TEXTURE_2D,texture[0]);
    fill_base_pattern(mipImage);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,mipImage);
    fill_level(mipImage,4,4,0,255,0);
    glTexImage2D(GL_TEXTURE_2D,1,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,mipImage);
    fill_level(mipImage,2,2,0,0,255);
    glTexImage2D(GL_TEXTURE_2D,2,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,mipImage);
    fill_level(mipImage,1,1,255,255,255);
    glTexImage2D(GL_TEXTURE_2D,3,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,mipImage);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    vertex(-1.4f,-0.7f,-3,0,0); vertex(-0.1f,-0.7f,-3,1,0);
    vertex(-0.1f,0.7f,-3,1,1);
    vertex(-1.4f,-0.7f,-3,0,0); vertex(-0.1f,0.7f,-3,1,1);
    vertex(-1.4f,0.7f,-3,0,1);
    vertex(0.1f,-0.7f,-3,0,0); vertex(1.4f,-0.7f,-3,5,0);
    vertex(1.4f,0.7f,-3,5,5);
    vertex(0.1f,-0.7f,-3,0,0); vertex(1.4f,0.7f,-3,5,5);
    vertex(0.1f,0.7f,-3,0,5);
    glEnd(); glFinish();
    glReadPixels(240,240,1,1,GL_RGBA,GL_UNSIGNED_BYTE,mipBase);
    for (i=0;i<4;++i)
        glReadPixels(340+(GLint)i*25,240,1,1,GL_RGBA,GL_UNSIGNED_BYTE,
                     repeat[i]);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    vertex(-1.4f,-0.7f,-3,0,0); vertex(-0.1f,-0.7f,-3,1,0);
    vertex(-0.1f,0.7f,-3,1,1);
    vertex(-1.4f,-0.7f,-3,0,0); vertex(-0.1f,0.7f,-3,1,1);
    vertex(-1.4f,0.7f,-3,0,1);
    vertex(0.1f,-0.7f,-3,0,0); vertex(1.4f,-0.7f,-3,5,0);
    vertex(1.4f,0.7f,-3,5,5);
    vertex(0.1f,-0.7f,-3,0,0); vertex(1.4f,0.7f,-3,5,5);
    vertex(0.1f,0.7f,-3,0,5);
    glEnd(); glFinish();
    glReadPixels(240,240,1,1,GL_RGBA,GL_UNSIGNED_BYTE,mipNear);
    glReadPixels(400,240,1,1,GL_RGBA,GL_UNSIGNED_BYTE,mipFar);
    for (i=0;i<4;++i)
        glReadPixels(340+(GLint)i*25,240,1,1,GL_RGBA,GL_UNSIGNED_BYTE,
                     repeat[i]);
    glGetIntegerv(MGL_PERF_EXECUTE_FAILURES,&failures);
    glGetIntegerv(MGL_PERF_WAIT_FAILURES,&waitFailures);
    glGetIntegerv(MGL_PERF_RHW_CLAMPS,&clamps);
    printf("PERSPECTIVE_SMOKE p0=%02lx%02lx%02lx p1=%02lx%02lx%02lx "
           "p2=%02lx%02lx%02lx p3=%02lx%02lx%02lx "
            "repeat=%02lx%02lx%02lx,%02lx%02lx%02lx,"
            "%02lx%02lx%02lx,%02lx%02lx%02lx "
            "mipbase=%02lx%02lx%02lx mipnear=%02lx%02lx%02lx "
           "mipfar=%02lx%02lx%02lx execfail=%ld "
           "waitfail=%ld clamps=%ld error=%08lx\n",
           (ULONG)sample[0][0],(ULONG)sample[0][1],(ULONG)sample[0][2],
           (ULONG)sample[1][0],(ULONG)sample[1][1],(ULONG)sample[1][2],
            (ULONG)sample[2][0],(ULONG)sample[2][1],(ULONG)sample[2][2],
            (ULONG)sample[3][0],(ULONG)sample[3][1],(ULONG)sample[3][2],
            (ULONG)repeat[0][0],(ULONG)repeat[0][1],(ULONG)repeat[0][2],
            (ULONG)repeat[1][0],(ULONG)repeat[1][1],(ULONG)repeat[1][2],
            (ULONG)repeat[2][0],(ULONG)repeat[2][1],(ULONG)repeat[2][2],
            (ULONG)repeat[3][0],(ULONG)repeat[3][1],(ULONG)repeat[3][2],
            (ULONG)mipBase[0],(ULONG)mipBase[1],(ULONG)mipBase[2],
           (ULONG)mipNear[0],(ULONG)mipNear[1],(ULONG)mipNear[2],
           (ULONG)mipFar[0],(ULONG)mipFar[1],(ULONG)mipFar[2],
           (LONG)failures,(LONG)waitFailures,(LONG)clamps,
           (ULONG)glGetError());
    glDeleteTextures(2,texture); mglDeleteContext(); MiniGLClose();
    return failures || waitFailures ? 10 : 0;
}
