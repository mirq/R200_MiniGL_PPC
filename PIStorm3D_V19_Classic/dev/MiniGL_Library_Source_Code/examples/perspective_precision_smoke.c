#include <stdio.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/minigl.h>
#include <clib/minigl_open_protos.h>
#include <libraries/minigl_perf.h>

#define WIDTH 640
#define HEIGHT 480
#define TEX_SIZE 8

typedef struct { float x,y,z,w,s,t; } RefVertex;

static UBYTE checker[TEX_SIZE*TEX_SIZE*4];
static UBYTE white[TEX_SIZE*TEX_SIZE*4];
static UBYTE hud[TEX_SIZE*TEX_SIZE*4];

static int texel_index(float coordinate)
{
    int index=0;
    while (index+1<TEX_SIZE &&
           coordinate >= (float)(index+1)/(float)TEX_SIZE)
        ++index;
    return index;
}

static void make_images(void)
{
    ULONG x,y;
    for (y=0;y<TEX_SIZE;++y) for (x=0;x<TEX_SIZE;++x) {
        ULONG p=(y*TEX_SIZE+x)*4;
        checker[p]=(UBYTE)(16+x*29);
        checker[p+1]=(UBYTE)(16+y*29);
        checker[p+2]=(UBYTE)(24+((x^y)&7)*27);
        checker[p+3]=255;
        white[p]=white[p+1]=white[p+2]=white[p+3]=255;
        hud[p]=255; hud[p+1]=32; hud[p+2]=192; hud[p+3]=255;
    }
}

static void upload(GLuint name,const UBYTE *pixels)
{
    glBindTexture(GL_TEXTURE_2D,name);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,TEX_SIZE,TEX_SIZE,0,
                 GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
}

/* Reference for the exact frustum used below. Clipping and attribute
 * interpolation happen in homogeneous space; projection happens afterwards. */
static RefVertex transform(float x,float y,float z,float s,float t)
{
    RefVertex v;
    v.x=x; v.y=y*(4.0f/3.0f);
    v.z=-(101.0f/99.0f)*z-(200.0f/99.0f); v.w=-z;
    v.s=s; v.t=t;
    return v;
}

static float distance_to(const RefVertex *v,int plane)
{
    switch (plane) {
    case 0: return v->x+v->w; case 1: return v->w-v->x;
    case 2: return v->y+v->w; case 3: return v->w-v->y;
    case 4: return v->z+v->w; default: return v->w-v->z;
    }
}

static RefVertex mix_vertex(const RefVertex *a,const RefVertex *b,float q)
{
    RefVertex o;
#define MIX(f) o.f=a->f+(b->f-a->f)*q
    MIX(x); MIX(y); MIX(z); MIX(w); MIX(s); MIX(t);
#undef MIX
    return o;
}

static int clip_polygon(RefVertex *v,int count)
{
    RefVertex temporary[16];
    int plane;
    for (plane=0;plane<6 && count; ++plane) {
        int i,n=0;
        RefVertex previous=v[count-1];
        float pd=distance_to(&previous,plane);
        for (i=0;i<count;++i) {
            RefVertex current=v[i];
            float cd=distance_to(&current,plane);
            int pi=pd>=0.0f,ci=cd>=0.0f;
            if (pi!=ci)
                temporary[n++]=mix_vertex(&previous,&current,pd/(pd-cd));
            if (ci) temporary[n++]=current;
            previous=current; pd=cd;
        }
        count=n;
        for (i=0;i<count;++i) v[i]=temporary[i];
    }
    return count;
}

static int expected_at(const RefVertex *polygon,int count,int px,int py,
                       float scale,UBYTE rgba[4])
{
    int tri;
    float sampleX=(float)px+0.5f,sampleY=(float)py+0.5f;
    for (tri=1;tri+1<count;++tri) {
        const RefVertex *v[3]={&polygon[0],&polygon[tri],&polygon[tri+1]};
        float x[3],y[3],area,a,b,c,iw,s,t;
        int i,tx,ty;
        for (i=0;i<3;++i) {
            x[i]=(v[i]->x/v[i]->w+1.0f)*(WIDTH*0.5f);
            y[i]=(v[i]->y/v[i]->w+1.0f)*(HEIGHT*0.5f);
        }
        area=(x[1]-x[0])*(y[2]-y[0])-(x[2]-x[0])*(y[1]-y[0]);
        if (area==0.0f) continue;
        a=((x[1]-sampleX)*(y[2]-sampleY)-
           (x[2]-sampleX)*(y[1]-sampleY))/area;
        b=((x[2]-sampleX)*(y[0]-sampleY)-
           (x[0]-sampleX)*(y[2]-sampleY))/area;
        c=1.0f-a-b;
        if (a < -0.0001f || b < -0.0001f || c < -0.0001f) continue;
        iw=a/v[0]->w+b/v[1]->w+c/v[2]->w;
        s=(a*v[0]->s/v[0]->w+b*v[1]->s/v[1]->w+
           c*v[2]->s/v[2]->w)/iw;
        t=(a*v[0]->t/v[0]->w+b*v[1]->t/v[1]->w+
           c*v[2]->t/v[2]->w)/iw;
        s*=scale; t*=scale;
        s-=((long)s); if (s<0.0f) s+=1.0f;
        t-=((long)t); if (t<0.0f) t+=1.0f;
        /* Avoid depending on the 68881 FPCR rounding mode for float-to-int. */
        tx=texel_index(s); ty=texel_index(t);
        if (tx>=TEX_SIZE) tx=0; if (ty>=TEX_SIZE) ty=0;
        i=(ty*TEX_SIZE+tx)*4;
        rgba[0]=checker[i]; rgba[1]=checker[i+1];
        rgba[2]=checker[i+2]; rgba[3]=255;
        return 1;
    }
    return 0;
}

static int expected_quad_at(const RefVertex quad[4],int px,int py,float scale,
                            UBYTE rgba[4])
{
    static const UBYTE triangles[2][3]={{0,1,2},{0,2,3}};
    RefVertex polygon[16];
    int triangle,i,count;
    for (triangle=0;triangle<2;++triangle) {
        for (i=0;i<3;++i) polygon[i]=quad[triangles[triangle][i]];
        count=clip_polygon(polygon,3);
        if (expected_at(polygon,count,px,py,scale,rgba)) return 1;
    }
    return 0;
}

static void vertex(float x,float y,float z,float s,float t,int multitex)
{
    if (multitex) {
        glMultiTexCoord2fARB(GL_TEXTURE0_ARB,s,t);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB,s,t);
    } else glTexCoord2f(s,t);
    glVertex3f(x,y,z);
}

static void draw_quad(const float p[4][3],float scale,int multitex)
{
    static const float st[4][2]={{0,0},{1,0},{1,1},{0,1}};
    static const UBYTE order[6]={0,1,2,0,2,3};
    int i;
    glBegin(GL_TRIANGLES);
    for (i=0;i<6;++i) {
        int n=order[i];
        vertex(p[n][0],p[n][1],p[n][2],st[n][0]*scale,
               st[n][1]*scale,multitex);
    }
    glEnd();
}

static int verify_samples(const float p[4][3],float scale,int multitex,
                          const int samples[][2],int sampleCount,
                          const char *name,int baseCode)
{
    RefVertex quad[4];
    UBYTE got[4],expected[4];
    int i;
    for (i=0;i<4;++i) {
        static const float st[4][2]={{0,0},{1,0},{1,1},{0,1}};
        quad[i]=transform(p[i][0],p[i][1],p[i][2],st[i][0],st[i][1]);
    }
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    draw_quad(p,scale,multitex); glFinish();
    for (i=0;i<sampleCount;++i) {
        if (!expected_quad_at(quad,samples[i][0],samples[i][1],scale,
                              expected)) return baseCode+8+i;
        glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,
                     GL_UNSIGNED_BYTE,got);
        if (got[0]!=expected[0] || got[1]!=expected[1] ||
            got[2]!=expected[2]) {
            printf(" stage=%s sample%d=%02lx%02lx%02lx expected=%02lx%02lx%02lx",
                   name,i,(ULONG)got[0],(ULONG)got[1],(ULONG)got[2],
                   (ULONG)expected[0],(ULONG)expected[1],(ULONG)expected[2]);
            return baseCode+i;
        }
    }
    return 0;
}

static void draw_hud(GLuint texture)
{
    glActiveTextureARB(GL_TEXTURE1_ARB); glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB); glBindTexture(GL_TEXTURE_2D,texture);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(MGL_FLATFAN);
    glTexCoord2f(0,0); glVertex3f(16,80,0);
    glTexCoord2f(1,0); glVertex3f(80,80,0);
    glTexCoord2f(1,1); glVertex3f(80,16,0);
    glTexCoord2f(0,1); glVertex3f(16,16,0);
    glEnd();
}

static int verify_hud(GLuint texture)
{
    UBYTE pixel[4];
    draw_hud(texture);
    glFinish();
    glReadPixels(48,431,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    return pixel[0]==255 && pixel[1]==32 && pixel[2]==192 ? 0 : 140;
}

int main(int argc,char **argv)
{
    static const float regular[4][3]={{-1,-0.9f,-2},{4,-3.6f,-8},
                                      {4,3.6f,-8},{-1,0.9f,-2}};
    static const float nearClipped[4][3]={{-0.8f,-0.4f,-0.5f},{4,-3.6f,-8},
                                          {4,3.6f,-8},{-0.8f,0.4f,-0.5f}};
    static const float edgeClipped[4][3]={{-6,-0.9f,-2},{4,-3.6f,-8},
                                          {4,3.6f,-8},{-6,0.9f,-2}};
    /* A world-space floor made from known square cells. It is deliberately
     * shallow and extends beyond both screen edges near the viewer. */
    static const float floorPlane[4][3]={{-6,-1.2f,-2},{6,-1.2f,-2},
                                         {6,-1.2f,-14},{-6,-1.2f,-14}};
    static const int regularSamples[][2]={{240,180},{320,240},{400,300}};
    static const int nearSamples[][2]={{240,200},{320,240},{400,280}};
    static const int edgeSamples[][2]={{40,180},{240,240},{440,300}};
    static const int floorSamples[][2]={{320,100},{320,160},{250,190}};
    void *context=NULL;
    GLuint textures[3];
    GLint execFailures=0,waitFailures=0,clamps=0,drops[5]={0,0,0,0,0};
    int fullscreen=0,soak=0,arg;
    int result=0;
    GLenum error;

    for (arg=1;arg<argc;++arg) {
        if (!strcmp(argv[arg],"--fullscreen")) fullscreen=1;
        else if (!strcmp(argv[arg],"--soak")) soak=1;
    }
    if (!MiniGLOpen()) return 20;
    mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
    mglChoosePixelDepth(32);
    context=mglCreateContext(0,0,WIDTH,HEIGHT);
    if (!context) { MiniGLClose(); return 21; }
    glHint(MGL_PERF_COUNTERS_HINT,GL_NICEST);
    glViewport(0,0,WIDTH,HEIGHT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-1,1,-0.75,0.75,1,100);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST); glClearColor(0,0,0,1);
    make_images(); glGenTextures(3,textures);
    glActiveTextureARB(GL_TEXTURE0_ARB); glEnable(GL_TEXTURE_2D);
    upload(textures[0],checker);
    glActiveTextureARB(GL_TEXTURE1_ARB); upload(textures[1],white);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);

    result=verify_samples(regular,1.0f,0,regularSamples,3,"single",100);
    if (!result) result=verify_samples(regular,4.0f,0,regularSamples,3,
                                       "repeat",110);
    if (!result) result=verify_samples(floorPlane,8.0f,0,floorSamples,3,
                                       "floor",120);
    glActiveTextureARB(GL_TEXTURE1_ARB); glEnable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    if (!result) result=verify_samples(regular,1.0f,1,regularSamples,3,
                                       "multitex",130);
    if (!result) result=verify_samples(nearClipped,2.0f,1,nearSamples,3,
                                       "nearclip",140);
    if (!result) result=verify_samples(edgeClipped,2.0f,1,edgeSamples,3,
                                       "edgeclip",150);
    glActiveTextureARB(GL_TEXTURE1_ARB); glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB); upload(textures[2],hud);
    if (!result) result=verify_hud(textures[2]);
    if (!result && soak) {
        draw_hud(textures[2]);
        mglSwitchDisplay();
        Delay(3000);
    }

    glGetIntegerv(MGL_PERF_EXECUTE_FAILURES,&execFailures);
    glGetIntegerv(MGL_PERF_WAIT_FAILURES,&waitFailures);
    glGetIntegerv(MGL_PERF_RHW_CLAMPS,&clamps);
    glGetIntegerv(MGL_PERF_DROP_DRAW_RECT,&drops[0]);
    glGetIntegerv(MGL_PERF_DROP_TEXTURE0,&drops[1]);
    glGetIntegerv(MGL_PERF_DROP_TEXTURE0_MIPS,&drops[2]);
    glGetIntegerv(MGL_PERF_DROP_TEXTURE1,&drops[3]);
    glGetIntegerv(MGL_PERF_DROP_TEXTURE1_MIPS,&drops[4]);
    if (!result && execFailures) result=150;
    if (!result && waitFailures) result=151;
    if (!result && clamps) result=152;
    if (!result && (drops[0]||drops[1]||drops[2]||drops[3]||drops[4]))
        result=153;
    error=glGetError();
    if (!result && error!=GL_NO_ERROR) result=154;
    printf("MINIGL_PERSPECTIVE_PRECISION mode=%s result=%d execfail=%ld "
           "waitfail=%ld clamps=%ld drops=%ld,%ld,%ld,%ld,%ld error=%08lx\n",
           fullscreen?"fullscreen":"window",result,(LONG)execFailures,
           (LONG)waitFailures,(LONG)clamps,(LONG)drops[0],(LONG)drops[1],
           (LONG)drops[2],(LONG)drops[3],(LONG)drops[4],(ULONG)error);
    glDeleteTextures(3,textures); mglDeleteContext(); MiniGLClose();
    return result;
}
