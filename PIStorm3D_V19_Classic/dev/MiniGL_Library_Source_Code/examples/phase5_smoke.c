#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <libraries/minigl_perf.h>

static void solid(UBYTE *image, ULONG width, ULONG height,
                  UBYTE r, UBYTE g, UBYTE b, UBYTE a)
{ ULONG i; for(i=0;i<width*height;++i) { image[i*4]=r; image[i*4+1]=g; image[i*4+2]=b; image[i*4+3]=a; } }

static LONG residencyFailIndex=-1;
static GLenum residencyError=GL_NO_ERROR;

static void checker(UBYTE *image, ULONG size)
{
    ULONG x,y;
    for(y=0;y<size;++y) for(x=0;x<size;++x) {
        BOOL odd=((x>>2)^(y>>2))&1; UBYTE *p=image+(y*size+x)*4;
        p[0]=odd?245:30; p[1]=odd?210:60; p[2]=odd?40:235;
        p[3]=(x<size/2)?255:112;
    }
}

static void quad(GLfloat l,GLfloat b,GLfloat r,GLfloat t,
                 GLfloat s0,GLfloat t0,GLfloat s1,GLfloat t1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(s0,t0); glVertex2f(l,b); glTexCoord2f(s1,t0); glVertex2f(r,b);
    glTexCoord2f(s1,t1); glVertex2f(r,t); glTexCoord2f(s0,t1); glVertex2f(l,t);
    glEnd();
}

static UWORD pixel(const MGLLockInfo *info,ULONG x,ULONG y)
{ volatile UBYTE *p=(volatile UBYTE*)info->base_address+y*info->pitch+x*2; return (UWORD)(((UWORD)p[0]<<8)|p[1]); }

static int verify_updates(GLuint texture)
{
    UBYTE image[16*16*4], green[4*4*4], transparent[16*16*4];
    MGLLockInfo info; UWORD first,updated,alpha,alphaVisible,blend,nativeBlend;
    solid(image,16,16,255,0,0,255); solid(green,4,4,0,255,0,255);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glEnable(GL_TEXTURE_2D); glColor4ub(255,255,255,255);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    quad(-0.7f,-0.7f,0.7f,0.7f,0,0,1,1); glFinish();
    if(!mglLockBack(&info)) return 0; first=pixel(&info,info.width/2,info.height/2);
    glTexSubImage2D(GL_TEXTURE_2D,0,6,6,4,4,GL_RGBA,GL_UNSIGNED_BYTE,green);
    glClear(GL_COLOR_BUFFER_BIT); quad(-0.7f,-0.7f,0.7f,0.7f,0,0,1,1); glFinish();
    if(!mglLockBack(&info)) return 0; updated=pixel(&info,info.width/2,info.height/2);
    solid(transparent,16,16,255,0,0,0);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,transparent);
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,0.5f);
    quad(-0.7f,-0.7f,0.7f,0.7f,0,0,1,1); glFinish();
    if(!mglLockBack(&info)) return 0; alpha=pixel(&info,info.width/2,info.height/2);
    solid(image,16,16,255,0,0,170);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    glClear(GL_COLOR_BUFFER_BIT);
    quad(-0.7f,-0.7f,0.7f,0.7f,0,0,1,1); glFinish();
    if(!mglLockBack(&info)) return 0;
    alphaVisible=pixel(&info,info.width/2,info.height/2);
    glDisable(GL_ALPHA_TEST);
    solid(image,16,16,255,0,0,128);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    glClear(GL_COLOR_BUFFER_BIT); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    quad(-0.7f,-0.7f,0.7f,0.7f,0,0,1,1); glFinish();
    if(!mglLockBack(&info)) return 0; blend=pixel(&info,info.width/2,info.height/2);
    glDisable(GL_BLEND); nativeBlend=(UWORD)((blend>>8)|(blend<<8));
    printf("MINIGL_PHASE5_VERIFY texture=%04lx subimage=%04lx alpha=%04lx alpha170=%04lx blend=%04lx\n",
           (unsigned long)first,(unsigned long)updated,(unsigned long)alpha,
           (unsigned long)alphaVisible,(unsigned long)blend);
    return first==0x00f8U && updated==0xe007U && alpha==0x1f00U &&
           alphaVisible==0x00f8U &&
           (nativeBlend&0xf800U) && (nativeBlend&0x001fU);
}

static int verify_deferred_updates(GLuint texture)
{
    UBYTE red[16*16*4],green[16*16*4],patch[4*4*4];
    MGLLockInfo info;
    GLint submits,copies,bytes,coalesced,deferred;
    GLint barriers,asyncSubmits,textureWaits,pollHits,executeFailures;
    GLint waitFailures;
    UWORD left,right;

    solid(red,16,16,255,0,0,255);
    solid(green,16,16,0,255,0,255);
    solid(patch,4,4,0,0,255,255);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,
                 GL_UNSIGNED_BYTE,red);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glEnable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
    glClear(GL_COLOR_BUFFER_BIT); quad(-.1f,-.1f,.1f,.1f,0,0,1,1);
    glFinish();

    glHint(MGL_PERF_COUNTERS_HINT,GL_NICEST);
    glTexSubImage2D(GL_TEXTURE_2D,0,2,2,4,4,GL_RGBA,
                    GL_UNSIGNED_BYTE,patch);
    glTexSubImage2D(GL_TEXTURE_2D,0,6,2,4,4,GL_RGBA,
                    GL_UNSIGNED_BYTE,patch);
    glGetIntegerv(MGL_PERF_SUBMITS,&submits);
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_COPIES,&copies);
    if(submits!=0 || copies!=0) return 0;
    glClear(GL_COLOR_BUFFER_BIT); quad(-.7f,-.7f,.7f,.7f,0,0,1,1);
    glFinish();
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_COPIES,&copies);
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_BYTES,&bytes);
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_COALESCED,&coalesced);
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_DEFERRED,&deferred);
    /* Updates reach the GPU through a residency re-import (the texture
     * cache never re-reads rewritten lines), so no in-place copy runs;
     * the updates are still deferred and coalesced into one re-import. */
    if(copies!=0 || bytes!=0 || deferred!=2) return 0;

    glHint(MGL_PERF_COUNTERS_HINT,GL_NICEST);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,16,16,GL_RGBA,
                    GL_UNSIGNED_BYTE,red);
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    quad(-.95f,-.7f,-.05f,.7f,0,0,1,1);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,16,16,GL_RGBA,
                    GL_UNSIGNED_BYTE,green);
    quad(.05f,-.7f,.95f,.7f,0,0,1,1);
    glFinish();
    if(!mglLockBack(&info)) return 0;
    left=pixel(&info,info.width/4,info.height/2);
    right=pixel(&info,(info.width*3)/4,info.height/2);
    glGetIntegerv(MGL_PERF_TEXTURE_UPLOAD_BARRIERS,&barriers);
    glGetIntegerv(MGL_PERF_ASYNC_SUBMITS,&asyncSubmits);
    glGetIntegerv(MGL_PERF_TEXTURE_WAITS,&textureWaits);
    glGetIntegerv(MGL_PERF_FENCE_POLL_HITS,&pollHits);
    glGetIntegerv(MGL_PERF_EXECUTE_FAILURES,&executeFailures);
    glGetIntegerv(MGL_PERF_WAIT_FAILURES,&waitFailures);
    printf("MINIGL_PHASE5_DEFERRED copies=%ld bytes=%ld coalesced=%ld deferred=%ld barriers=%ld async=%ld texwait=%ld polls=%ld left=%04lx right=%04lx\n",
           (long)copies,(long)bytes,(long)coalesced,(long)deferred,
           (long)barriers,(long)asyncSubmits,(long)textureWaits,(long)pollHits,
           (unsigned long)left,(unsigned long)right);
    glHint(MGL_PERF_COUNTERS_HINT,GL_DONT_CARE);
    /* asyncSubmits is timing-sensitive, not an ordering contract: the
     * residency engine may split the final flush around the texture
     * re-import depending on when the driver makes the previous fence
     * visible (measured async=2 on the pre-extraction driver, async=3 on
     * the radeon3d_emit driver with both old and new clients; pixels and
     * the barrier/deferred counters are identical). The ordering gates are
     * deferred==2, barriers>=1 and a live fence observation below. */
    return barriers>=1 && asyncSubmits>=2 && textureWaits+pollHits>=1 &&
           executeFailures==0 && waitFailures==0 && left==0x00f8U &&
           right==0xe007U && glGetError()==GL_NO_ERROR;
}

static int verify_flatfan(GLuint texture)
{
    UBYTE image[16*16*4]; MGLLockInfo info; UWORD center,texturedCenter;
    glViewport(17,23,64,48);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glTranslatef(1000.0f,1000.0f,0.0f);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glColor4ub(255,0,0,255);
    glBegin(MGL_FLATFAN);
    glVertex2f(280,200); glVertex2f(360,200);
    glVertex2f(360,280); glVertex2f(280,280);
    glEnd(); glFinish();
    if(!mglLockBack(&info)) return 0;
    center=pixel(&info,320,240);
    solid(image,16,16,255,0,0,170);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glEnable(GL_TEXTURE_2D); glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER,0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0,0); glVertex2f(280,200);
    glTexCoord2f(1,0); glVertex2f(360,200);
    glTexCoord2f(1,1); glVertex2f(360,280);
    glTexCoord2f(0,1); glVertex2f(280,280);
    glEnd(); glFinish();
    if(!mglLockBack(&info)) return 0;
    texturedCenter=pixel(&info,320,240);
    glViewport(0,0,640,480);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
    glColor4ub(255,255,255,255);
    printf("MINIGL_PHASE5_FLATFAN center=%04lx textured_alpha170=%04lx\n",
           (unsigned long)center,(unsigned long)texturedCenter);
    return center==0x00f8U && texturedCenter==0x00f8U;
}

static UBYTE fontAtlas[128*128*4];

static int render_font_case(GLuint texture, UBYTE alpha, GLint filter)
{
    MGLLockInfo info; ULONG x,y,index; int changed=0;
    const ULONG cellX=3,cellY=5;
    for(index=0;index<128UL*128UL;++index) {
        fontAtlas[index*4]=255; fontAtlas[index*4+1]=255;
        fontAtlas[index*4+2]=255; fontAtlas[index*4+3]=0;
    }
    for(y=0;y<8;++y) for(x=0;x<8;++x) {
        BOOL stroke=((y==1||y==3||y==6)&&x>=1&&x<=6)||
                    (x==1&&y>=1&&y<=3)||(x==6&&y>=3&&y<=6);
        if(stroke) fontAtlas[((cellY*8+y)*128+cellX*8+x)*4+3]=alpha;
    }
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,128,128,0,GL_RGBA,
                 GL_UNSIGNED_BYTE,fontAtlas);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glEnable(GL_TEXTURE_2D); glEnable(GL_ALPHA_TEST);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glAlphaFunc(GL_GREATER,0.5f);
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(cellX/16.0f,cellY/16.0f); glVertex2f(100,100);
    glTexCoord2f((cellX+1)/16.0f,cellY/16.0f); glVertex2f(108,100);
    glTexCoord2f((cellX+1)/16.0f,(cellY+1)/16.0f); glVertex2f(108,108);
    glTexCoord2f(cellX/16.0f,(cellY+1)/16.0f); glVertex2f(100,108);
    glEnd(); glFinish();
    if(!mglLockBack(&info)) return -1;
    for(y=100;y<108;++y) for(x=100;x<108;++x)
        if(pixel(&info,x,y)!=0x1f00U) changed++;
    glDisable(GL_ALPHA_TEST);
    return changed;
}

static int verify_font_filter(GLuint texture)
{
    int linear170=render_font_case(texture,170,GL_LINEAR);
    int nearest170=render_font_case(texture,170,GL_NEAREST);
    int linear255=render_font_case(texture,255,GL_LINEAR);
    printf("MINIGL_PHASE5_FONT linear170=%d nearest170=%d linear255=%d\n",
           linear170,nearest170,linear255);
    return nearest170>0 && linear255>0;
}

static int texture_stress(void)
{
    GLuint names[32],again[32]; ULONG round,index;
    for(round=0;round<12;++round) {
        glGenTextures(32,names); glDeleteTextures(32,names);
        glGenTextures(32,again);
        for(index=0;index<32;++index) if(names[index]!=again[index]) return 0;
        glDeleteTextures(32,again);
    }
    return glGetError()==GL_NO_ERROR;
}

static int residency_stress(void)
{
    GLuint names[48]; UBYTE *image; ULONG index,pixelIndex;
    image=AllocMem(512UL*512UL*4UL,MEMF_PUBLIC);
    if(!image) return 0;
    for(pixelIndex=0;pixelIndex<512UL*512UL;++pixelIndex) {
        image[pixelIndex*4]=(UBYTE)(255-(UBYTE)pixelIndex);
        image[pixelIndex*4+1]=(UBYTE)(pixelIndex>>3);
        image[pixelIndex*4+2]=(UBYTE)pixelIndex;
        image[pixelIndex*4+3]=255;
    }
    glGenTextures(48,names); glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    for(index=0;index<48;++index) {
        glBindTexture(GL_TEXTURE_2D,names[index]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,512,512,0,MGL_UBYTE_BGRA,GL_UNSIGNED_BYTE,image);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        quad(-0.98f,-0.98f,-0.9f,-0.9f,0,0,1,1); glFinish();
        residencyError=glGetError();
        if(residencyError!=GL_NO_ERROR) { residencyFailIndex=(LONG)index; FreeMem(image,512UL*512UL*4UL); return 0; }
    }
    glBindTexture(GL_TEXTURE_2D,names[0]);
    quad(-0.98f,-0.98f,-0.9f,-0.9f,0,0,1,1); glFinish();
    glDeleteTextures(48,names); FreeMem(image,512UL*512UL*4UL);
    residencyError=glGetError();
    if(residencyError!=GL_NO_ERROR) residencyFailIndex=48;
    return residencyError==GL_NO_ERROR;
}

static int conversion_stress(GLuint texture)
{
    UBYTE alpha[16],lum[16],la[32],rgb[48],rgba[64],bgra[64],argb[64];
    UBYTE padded[6*6*3];
    UWORD p565[16],p4444[16]; ULONG i;
    for(i=0;i<16;++i) {
        alpha[i]=(UBYTE)(i*16); lum[i]=(UBYTE)(255-i*12);
        la[i*2]=lum[i]; la[i*2+1]=alpha[i];
        rgb[i*3]=255; rgb[i*3+1]=(UBYTE)(i*12); rgb[i*3+2]=0;
        rgba[i*4]=0; rgba[i*4+1]=255; rgba[i*4+2]=(UBYTE)(i*12); rgba[i*4+3]=255;
        bgra[i*4]=(UBYTE)(i*12); bgra[i*4+1]=0; bgra[i*4+2]=255; bgra[i*4+3]=255;
        argb[i*4]=255; argb[i*4+1]=255; argb[i*4+2]=(UBYTE)(i*12); argb[i*4+3]=0;
        p565[i]=0xf800U; p4444[i]=0xf00fU;
    }
    for(i=0;i<6*6;++i) { padded[i*3]=255; padded[i*3+1]=128; padded[i*3+2]=0; }
#define UPLOAD(fmt,type,data) do { glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,fmt,type,data); quad(-.98f,.9f,-.9f,.98f,0,0,1,1); glFinish(); if(glGetError()!=GL_NO_ERROR) return 0; } while(0)
    glBindTexture(GL_TEXTURE_2D,texture); glEnable(GL_TEXTURE_2D);
    UPLOAD(GL_ALPHA,GL_UNSIGNED_BYTE,alpha);
    UPLOAD(GL_LUMINANCE,GL_UNSIGNED_BYTE,lum);
    UPLOAD(GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,la);
    UPLOAD(GL_RGB,GL_UNSIGNED_BYTE,rgb); UPLOAD(GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    UPLOAD(MGL_UBYTE_BGRA,GL_UNSIGNED_BYTE,bgra);
    UPLOAD(MGL_UBYTE_ARGB,GL_UNSIGNED_BYTE,argb);
    UPLOAD(GL_RGB,MGL_UNSIGNED_SHORT_5_6_5,p565);
    UPLOAD(GL_RGBA,MGL_UNSIGNED_SHORT_4_4_4_4,p4444);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,6); glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,1);
    UPLOAD(GL_RGB,GL_UNSIGNED_BYTE,padded);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0); glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
#undef UPLOAD
    return 1;
}

int main(int argc,char **argv)
{
    BOOL window=TRUE,runResidency=FALSE; void *context; GLuint texture; UBYTE levels[16*16*4];
    static const GLfloat vertices[]={-0.85f,-0.9f,-0.15f,-0.9f,-0.15f,-0.35f,-0.85f,-0.35f};
    static const GLfloat texcoords[]={0,0,3,0,3,3,0,3};
    static const GLushort indices[]={0,1,2,3};
    int frame,frames,level; ULONG size; FILE *report=NULL;
    int width=640,height=480,depth=16,arg;
    for(arg=1;arg<argc;++arg) {
        if(!strcmp(argv[arg],"--fullscreen")) window=FALSE;
        else if(!strcmp(argv[arg],"--stress")) runResidency=TRUE;
        else if(!strcmp(argv[arg],"-width")&&arg+1<argc) width=atoi(argv[++arg]);
        else if(!strcmp(argv[arg],"-height")&&arg+1<argc) height=atoi(argv[++arg]);
        else if(!strcmp(argv[arg],"-depth")&&arg+1<argc) depth=atoi(argv[++arg]);
        else return 10;
    }
    frames=window?12:80;
    if(!MiniGLOpen()) return 20; mglChooseWindowMode(window); mglChoosePixelDepth(depth);
    context=mglCreateContext(0,0,width,height); if(!context) { MiniGLClose(); return 20; }
    if(!texture_stress()) { printf("MINIGL_PHASE5 stress=fail\n"); goto fail; }
    glGenTextures(1,&texture);
    if(!verify_updates(texture)) { printf("MINIGL_PHASE5 verify=fail\n"); goto fail; }
    if(!verify_deferred_updates(texture)) { printf("MINIGL_PHASE5 deferred=fail\n"); goto fail; }
    if(!verify_flatfan(texture)) { printf("MINIGL_PHASE5 flatfan=fail\n"); goto fail; }
    if(!verify_font_filter(texture)) { printf("MINIGL_PHASE5 font=fail\n"); goto fail; }
    if(!conversion_stress(texture)) { printf("MINIGL_PHASE5 conversion=fail\n"); goto fail; }
    if(runResidency) {
        if(!residency_stress()) { printf("MINIGL_PHASE5 residency=fail index=%ld error=%lu\n",(long)residencyFailIndex,(unsigned long)residencyError); goto fail; }
        printf("MINIGL_PHASE5 residency=pass objects=48 size=512x512\n");
        report=fopen("Work:minigl-phase5-stress.txt","w");
        if(report) { fprintf(report,"MINIGL_PHASE5 residency=pass objects=48 size=512x512\n"); fclose(report); report=NULL; }
        glBindTexture(GL_TEXTURE_2D,texture);
    }
    checker(levels,16);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,levels);
    for(level=1,size=8;level<5;++level,size>>=1) {
        solid(levels,size,size,(UBYTE)(40*level),(UBYTE)(255-35*level),(UBYTE)(50*level),255);
        glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,levels);
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
    quad(-.98f,.9f,-.9f,.98f,-1,-1,2,2); glFinish();
    if(glGetError()!=GL_NO_ERROR) { printf("MINIGL_PHASE5 clamp=fail\n"); goto fail; }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    {
        static const GLenum filters[]={GL_NEAREST_MIPMAP_NEAREST,
            GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST_MIPMAP_LINEAR,
            GL_LINEAR_MIPMAP_LINEAR};
        for(level=0;level<4;++level) {
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filters[level]);
            quad(-.98f,.9f,-.9f,.98f,0,0,1,1); glFinish();
            if(glGetError()!=GL_NO_ERROR) { printf("MINIGL_PHASE5 mip_filter=fail\n"); goto fail; }
        }
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    {
        GLuint retiring; glGenTextures(1,&retiring); glBindTexture(GL_TEXTURE_2D,retiring);
        checker(levels,16);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,levels);
        quad(-.98f,.9f,-.9f,.98f,0,0,1,1);
        glDeleteTextures(1,&retiring);
        if(glGetError()!=GL_NO_ERROR) { printf("MINIGL_PHASE5 retire=fail\n"); goto fail; }
        glBindTexture(GL_TEXTURE_2D,texture);
    }
    for(frame=0;frame<frames;++frame) {
        glClearColor(0.02f,0.04f,0.08f,1); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glColor4ub(255,255,255,255); quad(-0.95f,0.05f,-0.05f,0.95f,0,0,3,3);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        glColor4ub(80,180,255,255); quad(0.05f,0.05f,0.95f,0.95f,0,0,1,1);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glColor4ub(255,255,255,160); quad(0.05f,-0.95f,0.95f,-0.05f,0,0,1,1);
        glDisable(GL_BLEND);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(2,GL_FLOAT,0,vertices); glTexCoordPointer(2,GL_FLOAT,0,texcoords);
        glDrawElements(GL_QUADS,4,GL_UNSIGNED_SHORT,indices);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
        mglSwitchDisplay();
        if(glGetError()!=GL_NO_ERROR) { printf("MINIGL_PHASE5 render=fail frame=%ld\n",(long)frame); goto fail; }
        Delay(1);
    }
    glDeleteTextures(1,&texture); mglDeleteContext(); MiniGLClose();
    printf("MINIGL_PHASE5 mode=%s image=pass subimage=pass mip=pass wrap=pass env=pass blend=pass alpha=pass stress=pass frames=%ld\n",
           window?"window":"fullscreen",(long)frames); return 0;
fail:
    if(runResidency) { report=fopen("Work:minigl-phase5-stress.txt","w"); if(report) { fprintf(report,"MINIGL_PHASE5 residency=fail index=%ld error=%lu\n",(long)residencyFailIndex,(unsigned long)residencyError); fclose(report); } }
    mglDeleteContext(); MiniGLClose(); return 20;
}
