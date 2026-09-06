#include <stdio.h>
#include <string.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>

static UBYTE image[16*16*4];

static void fill(UBYTE r, UBYTE g, UBYTE b, UBYTE a)
{
    ULONG i;
    for (i=0; i<16UL*16UL; ++i) {
        image[i*4]=r; image[i*4+1]=g; image[i*4+2]=b; image[i*4+3]=a;
    }
}

static void read_direct_pixel(GLint x, GLint topY, UBYTE rgba[4])
{
    glFinish();
    glReadPixels(x, 239-topY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

int main(int argc, char **argv)
{
    void *context;
    GLuint texture;
    UBYTE textured[4], textureBlend[4], solid[4], replaceBlend[4], blended[4], blueAsAlpha[4], redAsAlpha[4], glyphPixel[4];
    ULONG x,y;
    int glyphChanged=0;

    BOOL window=!(argc==2 && !strcmp(argv[1],"--fullscreen"));
    if (!MiniGLOpen()) return 20;
    mglChooseWindowMode(window ? GL_TRUE : GL_FALSE);
    mglChoosePixelDepth(32);
    context=mglCreateContext(0,0,320,240);
    if (!context) { MiniGLClose(); return 21; }

    glViewport(0,0,320,240);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glShadeModel(GL_FLAT);
    glEnable(GL_TEXTURE_2D); glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER,0.5f);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    fill(255,0,0,170);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,
                 GL_UNSIGNED_BYTE,image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0,0); glVertex2f(100,100);
    glTexCoord2f(1,0); glVertex2f(116,100);
    glTexCoord2f(1,1); glVertex2f(116,116);
    glTexCoord2f(0,1); glVertex2f(100,116);
    glEnd();
    read_direct_pixel(108,108,textured);

    fill(255,0,0,128);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,
                 GL_UNSIGNED_BYTE,image);
    glDisable(GL_ALPHA_TEST); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0,0); glVertex2f(100,100);
    glTexCoord2f(1,0); glVertex2f(116,100);
    glTexCoord2f(1,1); glVertex2f(116,116);
    glTexCoord2f(0,1); glVertex2f(100,116);
    glEnd();
    read_direct_pixel(108,108,textureBlend);
    glDisable(GL_BLEND); glEnable(GL_ALPHA_TEST);

    /* Sparse 128x128 atlas cell matching character 83 ('S'). */
    {
        static UBYTE atlas[128*128*4];
        memset(atlas,0,sizeof(atlas));
        for (y=0;y<128UL*128UL;++y)
            atlas[y*4]=atlas[y*4+1]=atlas[y*4+2]=255;
        for (y=0;y<8;++y) for (x=0;x<8;++x) {
            BOOL stroke=((y==1||y==3||y==6)&&x>=1&&x<=6)||
                        (x==1&&y>=1&&y<=3)||(x==6&&y>=3&&y<=6);
            if (stroke) atlas[((5*8+y)*128+3*8+x)*4+3]=170;
        }
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,128,128,0,GL_RGBA,
                     GL_UNSIGNED_BYTE,atlas);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(MGL_FLATFAN);
        glTexCoord2f(3/16.0f,5/16.0f); glVertex2f(100,100);
        glTexCoord2f(4/16.0f,5/16.0f); glVertex2f(108,100);
        glTexCoord2f(4/16.0f,6/16.0f); glVertex2f(108,108);
        glTexCoord2f(3/16.0f,6/16.0f); glVertex2f(100,108);
        glEnd(); glFinish();
        for (y=100;y<108;++y) for (x=100;x<108;++x) {
            read_direct_pixel((GLint)x,(GLint)y,glyphPixel);
            if (glyphPixel[0] || glyphPixel[1] || glyphPixel[2]!=255)
                glyphChanged++;
        }
    }

    glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND);
    glColor4ub(0,255,0,128);
    glBegin(MGL_FLATFAN);
    glVertex2f(120,100); glVertex2f(136,100);
    glVertex2f(136,116); glVertex2f(120,116);
    glEnd();
    read_direct_pixel(128,108,solid);

    glClear(GL_COLOR_BUFFER_BIT); glEnable(GL_BLEND);
    glBlendFunc(GL_ONE,GL_ZERO);
    glBegin(MGL_FLATFAN);
    glVertex2f(120,100); glVertex2f(136,100);
    glVertex2f(136,116); glVertex2f(120,116);
    glEnd();
    read_direct_pixel(128,108,replaceBlend);

    glClear(GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glBegin(MGL_FLATFAN);
    glVertex2f(120,100); glVertex2f(136,100);
    glVertex2f(136,116); glVertex2f(120,116);
    glEnd();
    read_direct_pixel(128,108,blended);

    glClear(GL_COLOR_BUFFER_BIT);
    glColor4ub(0,255,128,255);
    glBegin(MGL_FLATFAN);
    glVertex2f(120,100); glVertex2f(136,100);
    glVertex2f(136,116); glVertex2f(120,116);
    glEnd();
    read_direct_pixel(128,108,blueAsAlpha);

    glClear(GL_COLOR_BUFFER_BIT);
    glColor4ub(128,255,0,255);
    glBegin(MGL_FLATFAN);
    glVertex2f(120,100); glVertex2f(136,100);
    glVertex2f(136,116); glVertex2f(120,116);
    glEnd();
    read_direct_pixel(128,108,redAsAlpha);

    printf("MINIGL_PHASE32_UI mode=%s texture=%02lx%02lx%02lx%02lx glyph=%ld "
           "texblend=%02lx%02lx%02lx%02lx solid=%02lx%02lx%02lx%02lx "
           "onezero=%02lx%02lx%02lx%02lx "
           "blend=%02lx%02lx%02lx%02lx bluealpha=%02lx%02lx%02lx%02lx "
           "redalpha=%02lx%02lx%02lx%02lx error=%08lx\n",
           window ? "window" : "fullscreen",
           (ULONG)textured[0],(ULONG)textured[1],(ULONG)textured[2],
           (ULONG)textured[3],(LONG)glyphChanged,
           (ULONG)textureBlend[0],(ULONG)textureBlend[1],
           (ULONG)textureBlend[2],(ULONG)textureBlend[3],
           (ULONG)solid[0],(ULONG)solid[1],(ULONG)solid[2],(ULONG)solid[3],
           (ULONG)replaceBlend[0],(ULONG)replaceBlend[1],
           (ULONG)replaceBlend[2],(ULONG)replaceBlend[3],
           (ULONG)blended[0],(ULONG)blended[1],
           (ULONG)blended[2],(ULONG)blended[3],
           (ULONG)blueAsAlpha[0],(ULONG)blueAsAlpha[1],
           (ULONG)blueAsAlpha[2],(ULONG)blueAsAlpha[3],
           (ULONG)redAsAlpha[0],(ULONG)redAsAlpha[1],
           (ULONG)redAsAlpha[2],(ULONG)redAsAlpha[3],(ULONG)glGetError());
    glDeleteTextures(1,&texture);
    mglDeleteContext(); MiniGLClose();
    return (textured[0]>200 && textured[1]<20 && textured[2]<20 &&
            glyphChanged>0 &&
            blended[1]>80 && blended[2]>80) ? 0 : 10;
}
