/* ============================================================
   PROCEDURAL WEAPONS  --  map.html 14661..15989, ported whole.

   THE GUN IS ITS OWN RENDERER.  It shares the window, the clock
   and the room's ambient cube with the ship and nothing else:
   its own mesh builder (loft / lathe / chamferBox, 50-degree
   smoothing groups, a 13-float interleave), its own 512^2
   albedo/normal/RMA set generated on the GPU in one MRT pass,
   and its own PBR shader rendered in VIEW SPACE -- uView is
   identity and uModel puts the gun in front of a camera that
   sits at the origin.

   Three weapons on three actions: a pistol whose slide cycles
   and locks back on empty, a rotary cannon that spins up and
   overheats, and a break-action double-barrel.  Each mesh is
   split stat / spin / hands / hmov / magm, so the whole
   animation costs one transform per part and no skinning.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "app.h"
#include "ent.h"
#include "vmw.h"
#include "fx.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GLuint glMakeProgram(const char *vs,const char *fs,const char *what);
int    rayTraceOne(float ox,float oy,float oz,float dx,float dy,float dz,
                   float maxT,int skip);
void   auGun(int kind);
void   auImpact(int metal,int wood,float x,float y,float z);
extern float rayHitDist;               /* how far the last trace got */
int    zomTrace(float,float,float,float,float,float,float,float*,int*);
void   zomHurt(int,float,float,float,float,float,float,float,int,int);
extern int   rayHitAx, rayHitSg;       /* ... and which face it came in on */

/* ---------------------------------------------------------------- math
   Column-major float[16], the way WebGL had them, returned by value so
   the call sites read like the JS they came from. */
typedef struct { float m[16]; } M4;

static M4 mI(void){ M4 o; identM(o.m); return o; }
static M4 mMul2(M4 a,M4 b){ M4 o; mulM(o.m,a.m,b.m); return o; }
static M4 mT(float x,float y,float z){ M4 m=mI(); m.m[12]=x;m.m[13]=y;m.m[14]=z; return m; }
static M4 mRX(float a){ float c=cosf(a),s=sinf(a); M4 m=mI();
  m.m[5]=c;m.m[6]=s;m.m[9]=-s;m.m[10]=c; return m; }
static M4 mRY(float a){ float c=cosf(a),s=sinf(a); M4 m=mI();
  m.m[0]=c;m.m[2]=-s;m.m[8]=s;m.m[10]=c; return m; }
static M4 mRZ(float a){ float c=cosf(a),s=sinf(a); M4 m=mI();
  m.m[0]=c;m.m[1]=s;m.m[4]=-s;m.m[5]=c; return m; }
static M4 mS(float x,float y,float z){ M4 m=mI(); m.m[0]=x;m.m[5]=y;m.m[10]=z; return m; }
static M4 mPersp(float f,float a,float n,float fa){
  M4 o; float t=1.f/tanf(f*0.5f);
  memset(o.m,0,sizeof o.m);
  o.m[0]=t/a; o.m[5]=t; o.m[10]=(fa+n)/(n-fa); o.m[11]=-1; o.m[14]=2*fa*n/(n-fa);
  return o;
}
static void m3of(const M4 *m,float *o){
  o[0]=m->m[0];o[1]=m->m[1];o[2]=m->m[2];
  o[3]=m->m[4];o[4]=m->m[5];o[5]=m->m[6];
  o[6]=m->m[8];o[7]=m->m[9];o[8]=m->m[10];
}
static void xfp(const M4 *m,const float *p,float *o){
  float x=p[0],y=p[1],z=p[2];
  o[0]=m->m[0]*x+m->m[4]*y+m->m[8]*z+m->m[12];
  o[1]=m->m[1]*x+m->m[5]*y+m->m[9]*z+m->m[13];
  o[2]=m->m[2]*x+m->m[6]*y+m->m[10]*z+m->m[14];
}
static void rot3(const M4 *M,const float *v,float *o){
  o[0]=M->m[0]*v[0]+M->m[4]*v[1]+M->m[8]*v[2];
  o[1]=M->m[1]*v[0]+M->m[5]*v[1]+M->m[9]*v[2];
  o[2]=M->m[2]*v[0]+M->m[6]*v[1]+M->m[10]*v[2];
}

/* mulberry32.  THE MESH IS A FUNCTION OF THIS, so it is spelled as
   uint32 arithmetic: Math.imul wraps, and so does uint32_t. */
static uint32_t RS;
static void seedRng(uint32_t s){ RS=s; }
static float rng(void){
  RS += 0x6D2B79F5u;
  uint32_t a=RS;
  uint32_t t=(a ^ (a>>15)) * (1u|a);
  t = ((t + ((t ^ (t>>7)) * (61u|t))) ^ t);
  return (float)((double)(t ^ (t>>14)) / 4294967296.0);
}
/* the runtime's own dice -- nothing downstream of these has to reproduce */
static float rnd01(void){ return (float)rand()/(float)RAND_MAX; }
static float rnd(float a){ return (rnd01()-0.5f)*2.f*a; }
static void lin(unsigned hex,float *o){
  o[0]=powf((float)((hex>>16)&255)/255.f,2.2f);
  o[1]=powf((float)((hex>>8)&255)/255.f,2.2f);
  o[2]=powf((float)(hex&255)/255.f,2.2f);
}

/* ------------------------------------------- mesh builder (face soup)
   One triangle is 9 position floats, 6 uv floats and one material byte.
   Corners are welded later, by position, in finalize(). */
typedef struct { float *v,*t; unsigned char *m; int n,cap; } Bld;

static M4 CM; static M4 XSTACK[16]; static int XSP;
static void push(M4 m){ XSTACK[XSP++]=CM; CM=mMul2(CM,m); }
static void pop(void){ CM=XSTACK[--XSP]; }

static void newB(Bld *B){ memset(B,0,sizeof *B); }
static void freeB(Bld *B){ free(B->v); free(B->t); free(B->m); memset(B,0,sizeof *B); }
static void bGrow(Bld *B){
  if(B->n<B->cap) return;
  B->cap = B->cap ? B->cap*2 : 512;
  B->v=(float*)realloc(B->v,(size_t)B->cap*9*sizeof(float));
  B->t=(float*)realloc(B->t,(size_t)B->cap*6*sizeof(float));
  B->m=(unsigned char*)realloc(B->m,(size_t)B->cap);
}
static void tri(Bld *B,const float *a,const float *b,const float *c,
                const float *ta,const float *tb,const float *tc,int mat){
  bGrow(B);
  float *v=B->v+B->n*9, *t=B->t+B->n*6;
  xfp(&CM,a,v); xfp(&CM,b,v+3); xfp(&CM,c,v+6);
  t[0]=ta[0];t[1]=ta[1]; t[2]=tb[0];t[3]=tb[1]; t[4]=tc[0];t[5]=tc[1];
  B->m[B->n]=(unsigned char)mat;
  B->n++;
}
static void quad(Bld *B,const float *a,const float *b,const float *c,const float *d,
                 const float *ta,const float *tb,const float *tc,const float *td,
                 int mat,int flip){
  if(flip){ tri(B,a,d,c,ta,td,tc,mat); tri(B,a,c,b,ta,tc,tb,mat); }
  else    { tri(B,a,b,c,ta,tb,tc,mat); tri(B,a,c,d,ta,tc,td,mat); }
}

#define RMAX 40
typedef struct { float p[RMAX][3]; int n; } Ring;

static void capRing(Bld *B,const Ring *r,int mat,float ts,int flip){
  int n=r->n; float cx=0,cy=0,cz=0;
  for(int i=0;i<n;i++){ cx+=r->p[i][0]; cy+=r->p[i][1]; cz+=r->p[i][2]; }
  cx/=n; cy/=n; cz/=n;
  float C[3]={cx,cy,cz}, uc[2]={cx*ts,cy*ts};
  for(int i=0;i<n;i++){
    const float *a=r->p[i], *b=r->p[(i+1)%n];
    float ua[2]={a[0]*ts,a[1]*ts}, ub[2]={b[0]*ts,b[1]*ts};
    if(flip) tri(B,C,b,a,uc,ub,ua,mat);
    else     tri(B,C,a,b,uc,ua,ub,mat);
  }
}
static void loftO(Bld *B,const Ring *r,int R,int mat,float ts,
                  int flip,int capS,int capE){
  int n=r[0].n;
  float us[RMAX+1], vs[16];
  us[0]=0;
  for(int i=1;i<=n;i++){
    const float *a=r[0].p[(i-1)%n], *b=r[0].p[i%n];
    us[i]=us[i-1]+hypot3(b[0]-a[0],b[1]-a[1],b[2]-a[2]);
  }
  vs[0]=0;
  for(int k=1;k<R;k++){
    float s=0;
    for(int i=0;i<n;i++){
      const float *a=r[k-1].p[i], *b=r[k].p[i];
      s+=hypot3(b[0]-a[0],b[1]-a[1],b[2]-a[2]);
    }
    float d=s/n; if(d<1e-4f) d=1e-4f;
    vs[k]=vs[k-1]+d;
  }
  for(int k=0;k<R-1;k++) for(int i=0;i<n;i++){
    int j=(i+1)%n;
    float ta[2]={us[i]*ts,vs[k]*ts},     tb[2]={us[i+1]*ts,vs[k]*ts};
    float tc[2]={us[i+1]*ts,vs[k+1]*ts}, td[2]={us[i]*ts,vs[k+1]*ts};
    quad(B,r[k].p[i],r[k].p[j],r[k+1].p[j],r[k+1].p[i],ta,tb,tc,td,mat,flip);
  }
  if(capS) capRing(B,&r[0],mat,ts,!flip);
  if(capE) capRing(B,&r[R-1],mat,ts,flip);
}
static void loft(Bld *B,const Ring *r,int R,int mat,float ts){
  loftO(B,r,R,mat,ts,0,0,0);
}
static Ring resample(Ring in,int n){
  int m=in.n; float cum[RMAX+1]; Ring o; o.n=n;
  cum[0]=0;
  for(int i=1;i<=m;i++){
    const float *a=in.p[(i-1)%m], *b=in.p[i%m];
    cum[i]=cum[i-1]+hypot3(b[0]-a[0],b[1]-a[1],b[2]-a[2]);
  }
  float total=cum[m]; int seg=0;
  for(int i=0;i<n;i++){
    float d=total*i/n;
    while(seg<m-1&&cum[seg+1]<d) seg++;
    float den=cum[seg+1]-cum[seg]; if(den<1e-9f) den=1e-9f;
    float t=(d-cum[seg])/den;
    const float *a=in.p[seg%m], *b=in.p[(seg+1)%m];
    for(int k=0;k<3;k++) o.p[i][k]=a[k]+(b[k]-a[k])*t;
  }
  return o;
}
static Ring ringCircle(float cx,float cy,float z,float r,int seg){
  Ring o; o.n=seg;
  for(int i=0;i<seg;i++){
    float a=(float)i/(float)seg*(float)TAU;
    o.p[i][0]=cx+cosf(a)*r; o.p[i][1]=cy+sinf(a)*r; o.p[i][2]=z;
  }
  return o;
}
static Ring ringTri(float cx,float cy,float z,float R,float c){
  Ring o; o.n=6; float V[3][2];
  for(int i=0;i<3;i++){
    float a=(float)PI/2.f+(float)i*2.f*(float)PI/3.f;
    V[i][0]=cx+cosf(a)*R; V[i][1]=cy+sinf(a)*R;
  }
  for(int i=0;i<3;i++){
    const float *v=V[i], *pv=V[(i+2)%3], *nv=V[(i+1)%3];
    float l1=hypot2(pv[0]-v[0],pv[1]-v[1]), l2=hypot2(nv[0]-v[0],nv[1]-v[1]);
    float k1=(c<l1*0.45f?c:l1*0.45f)/l1, k2=(c<l2*0.45f?c:l2*0.45f)/l2;
    o.p[i*2][0]  =v[0]+(pv[0]-v[0])*k1; o.p[i*2][1]  =v[1]+(pv[1]-v[1])*k1; o.p[i*2][2]  =z;
    o.p[i*2+1][0]=v[0]+(nv[0]-v[0])*k2; o.p[i*2+1][1]=v[1]+(nv[1]-v[1])*k2; o.p[i*2+1][2]=z;
  }
  return o;
}
static Ring ringRect(float cx,float cy,float z,float sx,float sy,float c){
  float hx=sx/2>1e-4f?sx/2:1e-4f, hy=sy/2>1e-4f?sy/2:1e-4f;
  if(c>hx*0.9f) c=hx*0.9f;
  if(c>hy*0.9f) c=hy*0.9f;
  Ring o; o.n=8;
  /* the eight corners of a chamfered rectangle, in order, exactly as the
     JS literal spelled them out */
  const float X[8]={-1, 1, 1, 1, 1,-1,-1,-1};
  const float Y[8]={-1,-1,-1, 1, 1, 1, 1,-1};
  const float EX[8]={ c,-c, 0, 0,-c, c, 0, 0};
  const float EY[8]={ 0, 0, c,-c, 0, 0,-c, c};
  for(int i=0;i<8;i++){
    o.p[i][0]=cx+X[i]*hx+EX[i];
    o.p[i][1]=cy+Y[i]*hy+EY[i];
    o.p[i][2]=z;
  }
  return o;
}
static void chamferBox(Bld *B,float cx,float cy,float cz,
                       float sx,float sy,float sz,float c,int mat,float ts){
  float hz=sz/2;
  Ring r[4]={ ringRect(cx,cy,cz-hz,    sx-2*c,sy-2*c,c*0.6f),
              ringRect(cx,cy,cz-hz+c,  sx,    sy,    c),
              ringRect(cx,cy,cz+hz-c,  sx,    sy,    c),
              ringRect(cx,cy,cz+hz,    sx-2*c,sy-2*c,c*0.6f) };
  loftO(B,r,4,mat,ts,0,1,1);
}
/* a lathe profile point: radius, height, and -1 for "the lathe's material" */
typedef struct { float r,z; int m; } Prof;
static void lathe(Bld *B,float cx,float cy,const Prof *p,int np,int seg,
                  int mat,float ts){
  for(int i=0;i<np-1;i++){
    int m = p[i].m>=0 ? p[i].m : mat;
    Ring r[2]={ ringCircle(cx,cy,p[i].z,  p[i].r,  seg),
                ringCircle(cx,cy,p[i+1].z,p[i+1].r,seg) };
    loft(B,r,2,m,ts);
  }
}

/* -------- face normals/tangents, then 50-degree smoothing groups ------
   A CORNER IS WELDED BY POSITION, NOT BY INDEX.  Two triangles meeting at
   a point share a normal only if their faces are within 50 degrees of one
   another; past that the crease stays a crease.  The bucket chain below
   reproduces the JS Map exactly -- one entry per corner, so a face joins
   a group once for every corner it puts there. */
static int FB_FACE, FB_CORNER;   /* corners with no tangent of their own */

static float *finalize(Bld *B,int *nvOut){
  FB_FACE=FB_CORNER=0;
  int nt=B->n;
  *nvOut = nt*3;
  if(!nt) return NULL;
  /* THE FACE FRAME IS COMPUTED IN DOUBLE, and that is not fussiness.  The
     handedness of every corner is one sign test -- cross(N,T) against the
     face bitangent -- and on the flat disc that closes a lathe the two are
     very nearly perpendicular, so in float the test is a coin toss and
     dozens of corners come out mirrored against the previz.  Doubles cost
     three transient arrays and about ten milliseconds of boot. */
  double *fN =(double*)malloc((size_t)nt*3*sizeof(double));
  double *fT =(double*)malloc((size_t)nt*3*sizeof(double));
  double *fBt=(double*)malloc((size_t)nt*3*sizeof(double));
  for(int f=0;f<nt;f++){
    const float *V=B->v+f*9; const float *U=B->t+f*6;
    double e1[3]={V[3]-V[0],V[4]-V[1],V[5]-V[2]};
    double e2[3]={V[6]-V[0],V[7]-V[1],V[8]-V[2]};
    double nx=e1[1]*e2[2]-e1[2]*e2[1];
    double ny=e1[2]*e2[0]-e1[0]*e2[2];
    double nz=e1[0]*e2[1]-e1[1]*e2[0];
    double l=sqrt(nx*nx+ny*ny+nz*nz); if(l==0) l=1;
    fN[f*3]=nx/l; fN[f*3+1]=ny/l; fN[f*3+2]=nz/l;
    double d1[2]={U[2]-U[0],U[3]-U[1]}, d2[2]={U[4]-U[0],U[5]-U[1]};
    double r=d1[0]*d2[1]-d2[0]*d1[1];
    double tx,ty,tz,bx,by,bz;
    if(fabs(r)<1e-12){
      FB_FACE++;
      double ax[3]={0,0,0};
      if(fabs(fN[f*3])<0.9) ax[0]=1; else ax[1]=1;
      tx=ax[1]*fN[f*3+2]-ax[2]*fN[f*3+1];
      ty=ax[2]*fN[f*3]  -ax[0]*fN[f*3+2];
      tz=ax[0]*fN[f*3+1]-ax[1]*fN[f*3];
      bx=fN[f*3+1]*tz-fN[f*3+2]*ty;
      by=fN[f*3+2]*tx-fN[f*3]*tz;
      bz=fN[f*3]*ty-fN[f*3+1]*tx;
    } else {
      r=1.0/r;
      tx=(e1[0]*d2[1]-e2[0]*d1[1])*r;
      ty=(e1[1]*d2[1]-e2[1]*d1[1])*r;
      tz=(e1[2]*d2[1]-e2[2]*d1[1])*r;
      bx=(e2[0]*d1[0]-e1[0]*d2[0])*r;
      by=(e2[1]*d1[0]-e1[1]*d2[0])*r;
      bz=(e2[2]*d1[0]-e1[2]*d2[0])*r;
    }
    double tl=sqrt(tx*tx+ty*ty+tz*tz); if(tl==0) tl=1;
    fT[f*3]=tx/tl; fT[f*3+1]=ty/tl; fT[f*3+2]=tz/tl;
    double bl=sqrt(bx*bx+by*by+bz*bz); if(bl==0) bl=1;
    fBt[f*3]=bx/bl; fBt[f*3+1]=by/bl; fBt[f*3+2]=bz/bl;
  }

  size_t N=(size_t)nt*3;
  int *K=(int*)malloc(N*3*sizeof(int));
  for(size_t c=0;c<N;c++){
    const float *p=B->v+c*3;
    K[c*3]  =(int)lrintf(p[0]*4096.f);
    K[c*3+1]=(int)lrintf(p[1]*4096.f);
    K[c*3+2]=(int)lrintf(p[2]*4096.f);
  }
  size_t hs=1; while(hs < N*2) hs<<=1;
  int *head=(int*)malloc(hs*sizeof(int));
  int *nxt =(int*)malloc(N*sizeof(int));
  for(size_t i=0;i<hs;i++) head[i]=-1;
  for(size_t c=0;c<N;c++){
    unsigned h=(unsigned)(K[c*3]*73856093) ^ (unsigned)(K[c*3+1]*19349663)
             ^ (unsigned)(K[c*3+2]*83492791);
    h&=(unsigned)(hs-1);
    nxt[c]=head[h]; head[h]=(int)c;
  }

  const double COS=0.64;                    /* 50 degrees */
  float *out=(float*)malloc(N*13*sizeof(float));
  for(size_t c=0;c<N;c++){
    int f=(int)(c/3), k=(int)(c%3);
    const float *P=B->v+c*3; const float *U=B->t+f*6+k*2;
    unsigned h=(unsigned)(K[c*3]*73856093) ^ (unsigned)(K[c*3+1]*19349663)
             ^ (unsigned)(K[c*3+2]*83492791);
    h&=(unsigned)(hs-1);
    double nx=0,ny=0,nz=0,tx=0,ty=0,tz=0;
    for(int e=head[h]; e>=0; e=nxt[e]){
      if(K[e*3]!=K[c*3]||K[e*3+1]!=K[c*3+1]||K[e*3+2]!=K[c*3+2]) continue;
      int g=e/3;
      double d=fN[g*3]*fN[f*3]+fN[g*3+1]*fN[f*3+1]+fN[g*3+2]*fN[f*3+2];
      if(d>COS){
        nx+=fN[g*3]; ny+=fN[g*3+1]; nz+=fN[g*3+2];
        tx+=fT[g*3]; ty+=fT[g*3+1]; tz+=fT[g*3+2];
      }
    }
    double l=sqrt(nx*nx+ny*ny+nz*nz); if(l==0) l=1;
    nx/=l; ny/=l; nz/=l;
    double dt=tx*nx+ty*ny+tz*nz;
    tx-=nx*dt; ty-=ny*dt; tz-=nz*dt;
    l=sqrt(tx*tx+ty*ty+tz*tz);
    if(l<1e-6){
      FB_CORNER++;
      double ax[3]={0,0,0};
      if(fabs(nx)<0.9) ax[0]=1; else ax[1]=1;
      tx=ax[1]*nz-ax[2]*ny; ty=ax[2]*nx-ax[0]*nz; tz=ax[0]*ny-ax[1]*nx;
      l=sqrt(tx*tx+ty*ty+tz*tz); if(l==0) l=1;
    }
    tx/=l; ty/=l; tz/=l;
    double ccx=ny*tz-nz*ty, ccy=nz*tx-nx*tz, ccz=nx*ty-ny*tx;
    float w=(ccx*fBt[f*3]+ccy*fBt[f*3+1]+ccz*fBt[f*3+2])<0?-1.f:1.f;
    float *q=out+c*13;
    q[0]=P[0]; q[1]=P[1]; q[2]=P[2];
    q[3]=(float)nx; q[4]=(float)ny; q[5]=(float)nz;
    q[6]=U[0]; q[7]=U[1];
    q[8]=(float)tx; q[9]=(float)ty; q[10]=(float)tz; q[11]=w;
    q[12]=(float)B->m[f];
  }
  free(K); free(head); free(nxt); free(fN); free(fT); free(fBt);
  return out;
}

/* --------------------------------------------------- gun assemblies
   Texel scale is in metres, so a hand and a receiver carry grain at the
   same size: TSG for the small parts, TSB for the big flat ones. */
#define TSG 3.2f
#define TSB 7.2f
#define SEG 20

typedef struct { float *v; int nv, fbF, fbC; } Raw;
typedef struct { Raw stat,spin,hands,hmov,magm; } RawGun;

static Raw done(Bld *B){
  Raw r; r.v=finalize(B,&r.nv); r.fbF=FB_FACE; r.fbC=FB_CORNER;
  freeB(B); return r;
}
static void beginBuild(void){ CM=mI(); XSP=0; }

/* THE PROFILE IS THE MUZZLE.  Ten points shape the flash hider and the
   step behind it; the loop then walks the barrel in five segments, each
   with a cooling collar at a random fraction of its length, so no two of
   the three barrels read as the same extrusion. */
static int barrelProfile(float r,float z0,float z1,Prof *p){
  int k=0;
  p[k].r=0;       p[k].z=z0+0.055f; p[k++].m=5;
  p[k].r=r*0.44f; p[k].z=z0+0.055f; p[k++].m=5;
  p[k].r=r*0.44f; p[k].z=z0;        p[k++].m=5;
  p[k].r=r*1.34f; p[k].z=z0;        p[k++].m=2;
  p[k].r=r*1.42f; p[k].z=z0+0.007f; p[k++].m=2;
  p[k].r=r*1.42f; p[k].z=z0+0.030f; p[k++].m=2;
  p[k].r=r*1.34f; p[k].z=z0+0.037f; p[k++].m=1;
  p[k].r=r*1.06f; p[k].z=z0+0.037f; p[k++].m=1;
  p[k].r=r*1.06f; p[k].z=z0+0.052f; p[k++].m=1;
  p[k].r=r;       p[k].z=z0+0.060f; p[k++].m=1;
  float z=z0+0.060f;
  const int n=5; float seg=(z1-0.055f-z)/n;
  for(int i=0;i<n;i++){
    float g=0.55f+rng()*0.25f;
    p[k].r=r;       p[k].z=z+seg*g;         p[k++].m=1;
    p[k].r=r*1.18f; p[k].z=z+seg*(g+0.06f); p[k++].m=1;
    p[k].r=r*1.18f; p[k].z=z+seg*(g+0.22f); p[k++].m=1;
    p[k].r=r;       p[k].z=z+seg*(g+0.28f); p[k++].m=1;
    z+=seg;
  }
  p[k].r=r;       p[k].z=z1-0.055f; p[k++].m=1;
  p[k].r=r*1.30f; p[k].z=z1-0.048f; p[k++].m=1;
  p[k].r=r*1.30f; p[k].z=z1;        p[k++].m=1;
  p[k].r=0;       p[k].z=z1;        p[k++].m=1;
  return k;
}

/* The hands are their own mesh so they can ride the frame or the moving
   half.  Four fingers, a thumb pad and a wrist -- no more, because a
   viewmodel hand is only ever seen wrapped around something. */
static void handAt(Bld *TG,float x,float y,float z,float rot,float scale){
  push(mMul2(mMul2(mT(x,y,z),mRZ(rot)),mRX(0.15f)));
  chamferBox(TG,0,-0.055f*scale,0.02f,0.085f*scale,0.105f*scale,0.115f*scale,0.014f,6,TSG);
  chamferBox(TG,0,-0.130f*scale,0.05f,0.078f*scale,0.075f*scale,0.150f*scale,0.012f,6,TSG);
  for(int i=0;i<4;i++)
    chamferBox(TG,0,0.020f*scale,-0.030f+i*0.028f,0.090f*scale,0.026f*scale,0.024f,0.006f,6,TSG*1.6f);
  chamferBox(TG,-0.046f*scale,-0.006f*scale,-0.010f,0.020f,0.030f,0.030f,0.006f,6,TSG*1.6f);
  pop();
}

/* ---- rotary cannon ------------------------------------------------- */
static void clampTri(Bld *P,float z,float th,int accent,
                     float CRI,float CRO,float CCI,float CCO){
  float bev = 0.014f < th*0.35f ? 0.014f : th*0.35f;
  float z0=z-th/2, z1=z+th/2;
  Ring outer[4]={ ringTri(0,0,z0,     CRO-bev,CCO), ringTri(0,0,z0+bev,CRO,CCO),
                  ringTri(0,0,z1-bev, CRO,    CCO), ringTri(0,0,z1,    CRO-bev,CCO) };
  Ring inner[4]={ ringTri(0,0,z0,     CRI+bev,CCI), ringTri(0,0,z0+bev,CRI,CCI),
                  ringTri(0,0,z1-bev, CRI,    CCI), ringTri(0,0,z1,    CRI+bev,CCI) };
  int M=accent?2:0;
  loft(P,outer,4,M,TSG);
  loftO(P,inner,4,0,TSG,1,0,0);
  { Ring e[2]={inner[0],outer[0]}; loft(P,e,2,M,TSG); }
  { Ring e[2]={outer[3],inner[3]}; loft(P,e,2,M,TSG); }
}

static RawGun buildRotary(uint32_t seed){
  seedRng(seed); beginBuild();
  Bld S,P,Hd; newB(&S); newB(&P); newB(&Hd);
  const float BR=0.023f, OFF=0.036f, Z0=-0.68f, Z1=-0.055f;
  const float RBMAX=BR*1.18f, CLR=0.004f;
  const float CCI=0.036f, CCO=0.051f;
  float a1=OFF+2.0f*(RBMAX+CLR), a2=OFF+RBMAX+CLR+0.866f*CCI;
  const float CRI=a1>a2?a1:a2, CRO=CRI+0.020f;

  Prof pr[40];
  for(int i=0;i<3;i++){
    float a=(float)PI/2.f+(float)i*2.f*(float)PI/3.f;
    int np=barrelProfile(BR,Z0,Z1,pr);
    lathe(&P,cosf(a)*OFF,sinf(a)*OFF,pr,np,16,1,TSG*1.6f);
  }
  clampTri(&P,-0.545f,0.040f,0,CRI,CRO,CCI,CCO);
  clampTri(&P,-0.300f,0.036f,1,CRI,CRO,CCI,CCO);
  { Prof p[6]={{0,-0.118f,-1},{0.052f,-0.118f,-1},{0.072f,-0.106f,-1},
               {0.072f,-0.062f,-1},{0.058f,-0.055f,-1},{0,-0.055f,-1}};
    lathe(&P,0,0,p,6,SEG,0,TSG); }
  for(int i=0;i<6;i++){
    float a=(float)PI/2.f+(float)i/6.f*(float)TAU;
    push(mMul2(mRZ(a),mT(0.047f,0,-0.118f)));
    chamferBox(&P,0,0,0,0.016f,0.024f,0.014f,0.004f,rng()>0.65f?2:0,TSG*2);
    pop();
  }
  { Ring r[4]={ ringCircle(0,0,-0.060f,0.070f,SEG), ringCircle(0,0,-0.060f,0.078f,SEG),
                ringCircle(0,0, 0.030f,0.092f,SEG), ringCircle(0,0, 0.030f,0.082f,SEG) };
    loft(&S,r,4,0,TSB); }
  for(int i=0;i<7;i++){
    float a=-0.9f+i*0.30f;
    push(mMul2(mRZ(a),mT(0,0.085f,-0.014f)));
    chamferBox(&S,0,0,0,0.030f,0.012f,0.052f,0.003f,0,TSG*2);
    pop();
  }
  { Ring r[2]={ ringCircle(0,0,0.030f,0.092f,SEG),
                resample(ringRect(0,0,0.052f,0.176f,0.156f,0.020f),SEG) };
    loft(&S,r,2,0,TSB); }
  chamferBox(&S,0,0,0.185f,0.176f,0.156f,0.270f,0.018f,0,TSB);
  chamferBox(&S,-0.090f,0.005f,0.190f,0.010f,0.116f,0.200f,0.004f,0,TSB);
  chamferBox(&S, 0.090f,0.005f,0.190f,0.010f,0.116f,0.200f,0.004f,0,TSB);
  chamferBox(&S,0,0.090f,0.170f,0.050f,0.024f,0.230f,0.006f,0,TSB);
  for(int i=0;i<6;i++)
    chamferBox(&S,0,0.104f,0.075f+i*0.038f,0.038f,0.010f,0.020f,0.003f,0,TSG*2);
  chamferBox(&S,0,0,0.072f,0.182f,0.162f,0.030f,0.008f,8,TSG);
  push(mT(-0.064f,0.098f,0.074f));
  chamferBox(&S,0,0,-0.013f,0.050f,0.034f,0.022f,0.005f,0,TSB);
  chamferBox(&S,0,-0.026f,-0.018f,0.024f,0.020f,0.014f,0.004f,0,TSG);
  { float a[3]={-0.020f,-0.012f,0.0012f}, b[3]={0.020f,-0.012f,0.0012f};
    float c[3]={ 0.020f, 0.012f,0.0012f}, d[3]={-0.020f,0.012f,0.0012f};
    float t0[2]={0,0},t1[2]={1,0},t2[2]={1,1},t3[2]={0,1};
    quad(&S,a,b,c,d,t0,t1,t2,t3,9,0); }
  pop();
  chamferBox(&S,-0.096f,0.030f,0.200f,0.005f,0.014f,0.120f,0.002f,4,TSG);
  chamferBox(&S, 0.096f,0.030f,0.200f,0.005f,0.014f,0.120f,0.002f,4,TSG);
  /* C DOES NOT ORDER ITS ARGUMENTS AND JS DOES.  Every one of these draws
     from the same mulberry32 stream, so folding them into one argument
     list hands gcc the right to consume them right to left -- a different
     greeble on every panel, and a mesh that no longer matches the previz.
     One call per statement, left to right, is the whole fix. */
  for(int i=0;i<14;i++){
    float side=rng()<0.5f?-1.f:1.f;
    float sx=0.006f+rng()*0.008f;
    float sy=0.012f+rng()*0.040f;
    float sz=0.020f+rng()*0.060f;
    float cy=-0.045f+rng()*0.09f;
    float cz=0.085f+rng()*0.20f;
    int   mt=rng()<0.18f?2:0;
    chamferBox(&S,side*0.096f,cy,cz,sx,sy,sz,0.0025f,mt,TSG*2);
  }
  for(int i=0;i<6;i++){
    float cx=-0.05f+rng()*0.10f;
    float cz=0.090f+rng()*0.20f;
    float sx=0.010f+rng()*0.02f;
    float sz=0.014f+rng()*0.03f;
    chamferBox(&S,cx,0.081f,cz,sx,0.008f,sz,0.002f,0,TSG*2);
  }
  chamferBox(&S,0.128f,-0.040f,0.235f,0.100f,0.130f,0.150f,0.012f,0,TSB);
  chamferBox(&S,0.128f,-0.040f,0.162f,0.084f,0.110f,0.014f,0.005f,8,TSG);
  for(int i=0;i<7;i++){
    float t=(float)i/6.f;
    push(mMul2(mT(0.075f+0.045f*t,-0.088f+0.024f*sinf(t*3.1f),0.150f-0.055f*t),
               mRZ(-0.35f+0.5f*t)));
    chamferBox(&S,0,0,0,0.014f,0.026f,0.016f,0.003f,(i%2)?2:0,TSG*2);
    pop();
  }
  chamferBox(&S,0,-0.072f,-0.290f,0.086f,0.048f,0.170f,0.010f,0,TSB);
  for(int i=0;i<5;i++)
    chamferBox(&S,0,-0.098f,-0.355f+i*0.033f,0.062f,0.010f,0.016f,0.003f,0,TSG*2);
  push(mMul2(mT(0,-0.140f,0.240f),mRX(0.22f)));
  chamferBox(&S,0,0,0,0.046f,0.135f,0.062f,0.012f,3,TSG*2);
  pop();
  chamferBox(&S,0,-0.078f,0.240f,0.056f,0.030f,0.076f,0.010f,0,TSB);
  handAt(&Hd,0.0f,-0.150f,0.245f,0.0f,1.0f);
  handAt(&Hd,0.0f,-0.128f,-0.300f,0.0f,0.92f);

  RawGun g;
  g.stat=done(&S); g.spin=done(&P); g.hands=done(&Hd);
  { Bld a,b; newB(&a); newB(&b); g.hmov=done(&a); g.magm=done(&b); }
  return g;
}

/* ---- pistol -------------------------------------------------------- */
static RawGun buildPistol(void){
  beginBuild();
  Bld S,P,Hd,Mg; newB(&S); newB(&P); newB(&Hd); newB(&Mg);
  const float T=6.0f;
  chamferBox(&P,0,0.012f,-0.045f,0.046f,0.042f,0.250f,0.007f,14,T);
  chamferBox(&P,0,0.036f,-0.045f,0.030f,0.010f,0.230f,0.004f,14,T);
  for(int i=0;i<5;i++)
    chamferBox(&P, 0.024f,0.014f,-0.130f+i*0.019f,0.006f,0.024f,0.010f,0.002f,14,T*1.6f);
  for(int i=0;i<5;i++)
    chamferBox(&P,-0.024f,0.014f,-0.130f+i*0.019f,0.006f,0.024f,0.010f,0.002f,14,T*1.6f);
  chamferBox(&P,0.024f,0.006f,0.030f,0.006f,0.020f,0.070f,0.002f,5,T);
  chamferBox(&P,0,0.040f,-0.158f,0.010f,0.014f,0.016f,0.003f,14,T*1.6f);
  chamferBox(&P,0,0.040f,0.062f,0.024f,0.012f,0.012f,0.003f,14,T*1.6f);
  for(int i=0;i<4;i++){
    chamferBox(&P, 0.024f,0.012f,0.036f+i*0.010f,0.005f,0.030f,0.005f,0.0015f,14,T*2);
    chamferBox(&P,-0.024f,0.012f,0.036f+i*0.010f,0.005f,0.030f,0.005f,0.0015f,14,T*2);
  }
  { Prof p[7]={{0,-0.166f,5},{0.0090f,-0.166f,5},{0.0090f,-0.176f,5},
               {0.0150f,-0.176f,12},{0.0160f,-0.170f,12},{0.0160f,-0.150f,12},
               {0,-0.150f,12}};
    lathe(&P,0,0.012f,p,7,16,12,40); }
  chamferBox(&S,0,-0.026f,-0.030f,0.048f,0.038f,0.250f,0.007f,13,T);
  chamferBox(&S,0,-0.040f,-0.098f,0.044f,0.020f,0.100f,0.005f,13,T);
  chamferBox(&S,0,-0.088f,-0.060f,0.042f,0.098f,0.108f,0.009f,13,T);
  chamferBox(&S, 0.022f,-0.086f,-0.060f,0.004f,0.070f,0.086f,0.003f,8,T);
  chamferBox(&S,-0.022f,-0.086f,-0.060f,0.004f,0.070f,0.086f,0.003f,8,T);
  chamferBox(&S,0,-0.140f,-0.060f,0.046f,0.014f,0.104f,0.005f,13,T);
  chamferBox(&S,0,-0.070f,0.012f,0.030f,0.012f,0.062f,0.005f,13,T);
  chamferBox(&S,0,-0.052f,0.006f,0.010f,0.030f,0.010f,0.003f,14,T*1.6f);
  push(mMul2(mT(0,-0.098f,0.066f),mRX(0.30f)));
  chamferBox(&S,0,0,0,0.040f,0.118f,0.052f,0.010f,13,T);
  for(int i=0;i<6;i++)
    chamferBox(&S,0,-0.048f+i*0.019f,-0.028f,0.034f,0.008f,0.006f,0.002f,3,T*2);
  pop();
  chamferBox(&S,0,-0.020f,0.098f,0.044f,0.048f,0.014f,0.006f,13,T);
  /* the ammo counter: a bezel of four thin bars and the screen quad it
     frames, material 9, which the shader draws as seven segments */
  { const float Z=0.1062f, x0=-0.0155f, x1=0.0155f, y0=-0.0325f, y1=-0.0075f;
    chamferBox(&S,0,y0-0.004f,Z-0.0016f,0.040f,0.005f,0.005f,0.0015f,14,T*2);
    chamferBox(&S,0,y1+0.004f,Z-0.0016f,0.040f,0.005f,0.005f,0.0015f,14,T*2);
    chamferBox(&S,x0-0.004f,-0.020f,Z-0.0016f,0.005f,0.034f,0.005f,0.0015f,14,T*2);
    chamferBox(&S,x1+0.004f,-0.020f,Z-0.0016f,0.005f,0.034f,0.005f,0.0015f,14,T*2);
    float a[3]={x0,y0,Z}, b[3]={x1,y0,Z}, c[3]={x1,y1,Z}, d[3]={x0,y1,Z};
    float t0[2]={0,0},t1[2]={1,0},t2[2]={1,1},t3[2]={0,1};
    quad(&S,a,b,c,d,t0,t1,t2,t3,9,0); }
  chamferBox(&Mg,0,-0.106f,-0.060f,0.034f,0.092f,0.094f,0.007f,13,T);
  chamferBox(&Mg,0,-0.156f,-0.060f,0.038f,0.014f,0.098f,0.005f,14,T);
  for(int i=0;i<4;i++){
    Prof p[4]={{0,-0.020f,-1},{0.0125f,-0.014f,-1},{0.0135f,0.014f,-1},{0,0.020f,-1}};
    lathe(&Mg,0,-0.075f-i*0.021f,p,4,12,10,26);
  }
  handAt(&Hd,0,-0.112f,0.072f,0,0.86f);

  RawGun g;
  g.stat=done(&S); g.spin=done(&P); g.hands=done(&Hd); g.magm=done(&Mg);
  { Bld a; newB(&a); g.hmov=done(&a); }
  return g;
}

/* ---- double-barrel, break action ----------------------------------- */
static RawGun buildShotgun(void){
  beginBuild();
  Bld S,P,Hd,Hm; newB(&S); newB(&P); newB(&Hd); newB(&Hm);
  const float BX=0.032f, Z0=-0.66f, ZB=-0.030f;
  for(int s=0;s<2;s++){
    float sx = s?BX:-BX;
    Prof p[13]={{0,Z0+0.050f,5},{0.0165f,Z0+0.050f,5},{0.0165f,Z0,5},
                {0.0252f,Z0,12},{0.0266f,Z0+0.009f,12},{0.0266f,Z0+0.026f,12},
                {0.0232f,Z0+0.036f,12},{0.0214f,Z0+0.036f,12},
                {0.0214f,-0.120f,12},{0.0244f,-0.112f,12},{0.0244f,ZB-0.020f,12},
                {0.0222f,ZB,12},{0,ZB,12}};
    lathe(&P,sx,0,p,13,22,12,TSG*1.6f);
  }
  chamferBox(&P,0,0,-0.350f,0.020f,0.032f,0.560f,0.005f,0,TSB);
  chamferBox(&P,0,0.026f,Z0+0.048f,0.014f,0.014f,0.022f,0.004f,0,TSG);
  chamferBox(&P,0,0,ZB-0.030f,0.104f,0.062f,0.062f,0.011f,0,TSB);
  for(int s=0;s<2;s++){
    float sx = s?BX:-BX;
    Prof p[3]={{0,ZB-0.001f,5},{0.0170f,ZB-0.001f,5},{0.0170f,ZB-0.030f,5}};
    lathe(&P,sx,0,p,3,14,5,40);
  }
  chamferBox(&P,0,-0.040f,-0.300f,0.098f,0.058f,0.205f,0.015f,0,TSB);
  for(int i=0;i<6;i++)
    chamferBox(&P,0,-0.070f,-0.378f+i*0.031f,0.088f,0.010f,0.017f,0.004f,0,TSG*2);
  chamferBox(&P,0,-0.040f,-0.201f,0.104f,0.064f,0.014f,0.005f,0,TSG);
  chamferBox(&P,0,-0.040f,-0.399f,0.104f,0.064f,0.012f,0.005f,0,TSG);
  /* the receiver is one loft down eight stations, so the stock swells and
     tapers instead of stepping */
  { const float RG[8][5]={{-0.030f,0.106f,0.066f,0.022f,-0.002f},
                          { 0.004f,0.112f,0.100f,0.032f,-0.004f},
                          { 0.050f,0.114f,0.122f,0.040f,-0.006f},
                          { 0.118f,0.108f,0.118f,0.040f,-0.009f},
                          { 0.182f,0.096f,0.108f,0.037f,-0.012f},
                          { 0.242f,0.084f,0.102f,0.034f,-0.015f},
                          { 0.298f,0.076f,0.100f,0.032f,-0.018f},
                          { 0.348f,0.068f,0.094f,0.028f,-0.021f}};
    Ring r[8];
    for(int i=0;i<8;i++) r[i]=ringRect(0,RG[i][4],RG[i][0],RG[i][1],RG[i][2],RG[i][3]);
    loftO(&S,r,8,0,TSB,0,1,1); }
  chamferBox(&S,0,0.0475f,-0.022f,0.028f,0.021f,0.044f,0.007f,0,TSG);
  chamferBox(&S,0,-0.004f,-0.012f,0.116f,0.106f,0.026f,0.010f,8,TSG);
  for(int k=0;k<2;k++){
    float sd = k?1.f:-1.f;
    chamferBox(&S,sd*0.048f,-0.008f,0.208f,0.008f,0.074f,0.110f,0.007f,0,TSB);
    chamferBox(&S,sd*0.054f,-0.022f,0.078f,0.006f,0.020f,0.124f,0.003f,0,TSG*2);
    for(int i=0;i<3;i++)
      chamferBox(&S,sd*0.048f,0.012f,0.152f+i*0.040f,0.006f,0.011f,0.013f,0.002f,0,TSG*2);
    chamferBox(&S,sd*0.049f,-0.002f,0.196f,0.005f,0.011f,0.082f,0.002f,4,TSG);
  }
  chamferBox(&S,0,0.0505f,0.030f,0.050f,0.015f,0.034f,0.006f,0,TSB);
  { const float Y=0.0582f, x0=-0.0205f, x1=0.0205f, z0=0.019f, z1=0.041f;
    chamferBox(&S,0,Y-0.0035f,z0-0.004f,0.050f,0.006f,0.005f,0.0015f,0,TSG*2);
    chamferBox(&S,0,Y-0.0035f,z1+0.004f,0.050f,0.006f,0.005f,0.0015f,0,TSG*2);
    chamferBox(&S,x0-0.004f,Y-0.0035f,0.030f,0.005f,0.006f,0.032f,0.0015f,0,TSG*2);
    chamferBox(&S,x1+0.004f,Y-0.0035f,0.030f,0.005f,0.006f,0.032f,0.0015f,0,TSG*2);
    float a[3]={x0,Y,z1}, b[3]={x1,Y,z1}, c[3]={x1,Y,z0}, d[3]={x0,Y,z0};
    float t0[2]={0,0},t1[2]={1,0},t2[2]={1,1},t3[2]={0,1};
    quad(&S,a,b,c,d,t0,t1,t2,t3,9,0); }
  chamferBox(&S,0,-0.068f,0.124f,0.046f,0.026f,0.112f,0.010f,0,TSB);
  chamferBox(&S,0,-0.056f,0.106f,0.008f,0.030f,0.015f,0.003f,0,TSG*2);
  chamferBox(&S,0,-0.056f,0.134f,0.008f,0.030f,0.015f,0.003f,0,TSG*2);
  push(mMul2(mT(0,-0.120f,0.222f),mRX(0.32f)));
  chamferBox(&S,0,0,0,0.046f,0.130f,0.060f,0.013f,3,TSG*2);
  pop();
  handAt(&Hd,0,-0.130f,0.228f,0,1.0f);
  handAt(&Hm,0,-0.098f,-0.298f,0,0.94f);

  RawGun g;
  g.stat=done(&S); g.spin=done(&P); g.hands=done(&Hd); g.hmov=done(&Hm);
  { Bld a; newB(&a); g.magm=done(&a); }
  return g;
}

static Raw buildShell(void){
  beginBuild();
  Bld B; newB(&B);
  Prof p[7]={{0.0086f,-0.025f,-1},{0.0114f,-0.025f,-1},{0.0114f,0.0132f,-1},
             {0.0134f,0.0166f,-1},{0.0134f,0.0223f,-1},{0.0100f,0.0252f,-1},
             {0,0.0252f,-1}};
  lathe(&B,0,0,p,7,12,10,28);
  return done(&B);
}
static Raw buildShotshell(void){
  beginBuild();
  Bld B; newB(&B);
  Prof p[7]={{0.0112f,-0.028f,11},{0.0138f,-0.028f,11},{0.0138f,0.008f,10},
             {0.0154f,0.012f,10},{0.0154f,0.020f,10},{0.0116f,0.025f,10},
             {0,0.025f,10}};
  lathe(&B,0,0,p,7,14,11,26);
  return done(&B);
}
/* Muzzle flash: one quad, drawn twice, always facing the camera (mat 20).
   A rigid star of crossed blades reads as the same cage on every shot, and
   a cone down the bore is worse -- from behind the gun you look along its
   axis, so it projects as a filled disc with a hard silhouette.  A card has
   no shape of its own; the petals are cut in the shader from uMuzS, which
   is redrawn every shot. */
static Raw buildFlashCard(void){
  beginBuild();
  Bld B; newB(&B);
  float a[3]={-1,-1,0}, b[3]={1,-1,0}, c[3]={1,1,0}, d[3]={-1,1,0};
  float t0[2]={0,0},t1[2]={1,0},t2[2]={1,1},t3[2]={0,1};
  quad(&B,a,b,c,d,t0,t1,t2,t3,20,0);
  return done(&B);
}

/* ------------------------------------------------- procedural textures
   and the viewmodel shader.  Both are the browser's own GLSL, moved from
   `#version 300 es` to `#version 330 core` -- the precision qualifiers go
   because desktop GLSL has none, and nothing else changed, because nothing
   else needed to.  The viewmodel is rendered in VIEW SPACE: uView is the
   identity and uModel puts the gun in front of a camera at the origin,
   which is why every light in it is a view-space vector. */

static const char *GEN_VS =
"#version 330 core\n"
"void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}\n";

static const char *GEN_FS =
"#version 330 core\n"
"uniform vec2 uSeed; uniform int uMode; uniform float uRes;\n"
"uniform float uFlat,uMicro,uCells,uRustAmt,uRghBase,uRghVar;\n"
"uniform vec3 uSteel,uRust,uWorn;\n"
"layout(location=0) out vec4 oAlb;\n"
"layout(location=1) out vec4 oNrm;\n"
"layout(location=2) out vec4 oRMA;\n"
"\n"
"float hash21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}\n"
"float vnoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);\n"
"  float a=hash21(i),b=hash21(i+vec2(1,0)),c=hash21(i+vec2(0,1)),d=hash21(i+vec2(1,1));\n"
"  return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);}\n"
"float fbm(vec2 p){float s=0.0,a=0.5;for(int i=0;i<5;i++){s+=a*vnoise(p);p=p*2.03+7.1;a*=0.5;}return s;}\n"
"float sdBox(vec2 p,vec2 b){vec2 d=abs(p)-b;return length(max(d,0.0))+min(max(d.x,d.y),0.0);}\n"
"\n"
"float hPlate(vec2 uv){\n"
"  vec2 P=uv*uCells; vec2 id=floor(P), f=fract(P)-0.5;\n"
"  float r=hash21(id+uSeed);\n"
"  float h=0.0;\n"
"  float d=sdBox(f,vec2(0.445-0.06*step(0.72,r)));\n"
"  h+=smoothstep(0.022,-0.028,d)*(0.52+0.26*step(0.5,r));\n"
"  if(r>0.36){\n"
"    vec2 bp=abs(f)-vec2(0.345);\n"
"    float db=length(bp);\n"
"    h+=smoothstep(0.046,0.020,db)*0.34;\n"
"    h-=smoothstep(0.020,0.008,db)*0.22;\n"
"  }\n"
"  if(r<0.24){\n"
"    vec2 vp=f; vp.y=fract(vp.y*6.0+0.5)-0.5;\n"
"    float dv=sdBox(vec2(vp.x,vp.y/6.0),vec2(0.27,0.030));\n"
"    h-=smoothstep(0.012,-0.010,dv)*0.55;\n"
"  }\n"
"  if(r>0.55&&r<0.72){\n"
"    float ds=sdBox(f-vec2(0.0,0.30),vec2(0.30,0.055));\n"
"    h+=smoothstep(0.014,-0.012,ds)*0.30;\n"
"  }\n"
"  h+=(fbm(uv*90.0)-0.5)*0.075*uMicro;\n"
"  h+=(fbm(uv*260.0)-0.5)*0.035*uMicro;\n"
"  return h;\n"
"}\n"
"float hMach(vec2 uv){\n"
"  float h=0.0;\n"
"  float g=abs(fract(uv.y*22.0)-0.5)*2.0;\n"
"  h+=smoothstep(0.22,0.78,g)*0.26;\n"
"  float b=floor(uv.y*5.0);\n"
"  float rb=hash21(vec2(b,3.0)+uSeed);\n"
"  if(rb>0.58){\n"
"    vec2 k=uv*vec2(52.0,52.0);\n"
"    float kn=abs(fract(k.x+k.y)-0.5)+abs(fract(k.x-k.y)-0.5);\n"
"    h+=(0.5-kn)*0.38;\n"
"  }\n"
"  if(rb<0.20){ float d=abs(fract(uv.y*5.0)-0.5); h-=smoothstep(0.42,0.18,d)*0.30; }\n"
"  h+=(fbm(uv*vec2(70.0,140.0))-0.5)*0.09*uMicro;\n"
"  h+=(fbm(uv*vec2(220.0,420.0))-0.5)*0.04*uMicro;\n"
"  return h;\n"
"}\n"
"float hHard(vec2 uv){\n"
"  vec2 P=uv*2.0; vec2 id=floor(P), f=fract(P)-0.5;\n"
"  float r=hash21(id+uSeed*1.7);\n"
"  float h=0.0;\n"
"  h+=smoothstep(0.055,-0.020,sdBox(f,vec2(0.425,0.405)))*0.55;\n"
"  if(r>0.30&&abs(f.x)<0.36){\n"
"    float xr=fract(f.x*5.0+0.5)-0.5;\n"
"    float dl=sdBox(vec2(xr*0.2,f.y-0.17),vec2(0.034,0.125));\n"
"    h-=smoothstep(0.014,-0.008,dl)*0.72;\n"
"  }\n"
"  if(r<0.62){\n"
"    for(int i=0;i<2;i++){\n"
"      vec2 sp=f-vec2(i==0?-0.30:0.30,-0.29);\n"
"      h+=smoothstep(0.018,-0.010,length(sp)-0.082)*0.30;\n"
"      h-=smoothstep(0.010,-0.006,sdBox(sp,vec2(0.072,0.015)))*0.36;\n"
"    }\n"
"  }\n"
"  if(r>0.72){\n"
"    h-=smoothstep(0.012,-0.006,sdBox(f-vec2(0.0,-0.05),vec2(0.34,0.014)))*0.30;\n"
"  }\n"
"  h+=(fbm(uv*70.0)-0.5)*0.055*uMicro;\n"
"  h+=(fbm(uv*200.0)-0.5)*0.030*uMicro;\n"
"  return h;\n"
"}\n"
"float H(vec2 uv){ return uMode==0 ? hPlate(uv) : (uMode==1 ? hMach(uv) : hHard(uv)); }\n"
"void main(){\n"
"  vec2 uv=gl_FragCoord.xy/uRes;\n"
"  float e=1.0/uRes;\n"
"  float h =H(uv);\n"
"  float hx=H(uv+vec2(e,0.0));\n"
"  float hy=H(uv+vec2(0.0,e));\n"
"  float gx=(hx-h)/e, gy=(hy-h)/e;\n"
"  vec3 n=normalize(vec3(-gx,-gy,uFlat));\n"
"  float hw=(H(uv+vec2(4.0*e,0.0))+H(uv-vec2(4.0*e,0.0))\n"
"           +H(uv+vec2(0.0,4.0*e))+H(uv-vec2(0.0,4.0*e)))*0.25;\n"
"  float ao=clamp(1.0-(hw-h)*1.9,0.25,1.0);\n"
"  float grime=fbm(uv*6.0+uSeed*0.3);\n"
"  float ox=smoothstep(0.56,0.88,grime);\n"
"  vec3 alb=mix(uSteel,uRust,ox*uRustAmt);\n"
"  alb=mix(alb,uWorn,smoothstep(0.46,0.92,h)*0.40);\n"
"  alb*=0.66+0.44*ao;\n"
"  float rgh=uRghBase+uRghVar*ox-0.12*smoothstep(0.5,0.95,h)+0.07*(1.0-ao);\n"
"  rgh=clamp(rgh,0.05,0.92);\n"
"  oAlb=vec4(alb,1.0);\n"
"  oNrm=vec4(n*0.5+0.5,1.0);\n"
"  oRMA=vec4(rgh,1.0,ao,h*0.5+0.5);\n"
"}\n";

static const char *VM_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec4 aTan;\n"
"layout(location=4) in float aMat;\n"
"uniform mat4 uProj,uModel; uniform mat3 uNM;\n"
"out vec3 vP; out vec3 vN; out vec2 vUV; out vec3 vT; out float vW; flat out float vM;\n"
"void main(){\n"
"  vec4 wp=uModel*vec4(aPos,1.0);\n"
"  vP=wp.xyz; vN=normalize(uNM*aNrm); vT=normalize(uNM*aTan.xyz); vW=aTan.w;\n"
"  vUV=aUV; vM=aMat;\n"
"  gl_Position=uProj*wp;\n"
"}\n";

static const char *VM_FS =
"#version 330 core\n"
"in vec3 vP; in vec3 vN; in vec2 vUV; in vec3 vT; in float vW; flat in float vM;\n"
"out vec4 o;\n"
"uniform sampler2D tA0,tA1,tA2,tB0,tB1,tB2,tC0,tC1,tC2;\n"
"uniform vec3 uAmb;            // sampled room light at the player\n"
"uniform vec3 uLDir;           // its dominant direction, view space\n"
"uniform mat3 uV2W;            // view -> world rotation (for the env probe)\n"
"uniform vec3 uMuzP,uMuzC;     // muzzle point light, view space\n"
"uniform float uFlashI;        // player flashlight (cone down -Z from origin)\n"
"uniform float uMuzI;          // flash intensity (mats 20, 21)\n"
"uniform float uMuzS;          // flash shape seed, redrawn every shot\n"
"uniform float uMuzG;          // 1 = the soft bloom pass, 0 = the burst itself\n"
"uniform float uGain;          // ref-tuned constants -> engine light units\n"
"uniform float uTime; uniform int uAmmo; uniform float uHeat; uniform vec3 uAccent,uPaint;\n"
"const vec4 SEGS[7]=vec4[7](\n"
"  vec4(0.15,0.92,0.85,0.92), vec4(0.90,0.87,0.90,0.55), vec4(0.90,0.45,0.90,0.13),\n"
"  vec4(0.15,0.08,0.85,0.08), vec4(0.10,0.13,0.10,0.45), vec4(0.10,0.55,0.10,0.87),\n"
"  vec4(0.15,0.50,0.85,0.50));\n"
"const int DMASK[10]=int[10](63,6,91,79,102,109,125,7,127,111);\n"
"float sdSeg(vec2 p,vec2 a,vec2 b){vec2 pa=p-a,ba=b-a;\n"
"  float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0);return length(pa-ba*h);}\n"
"vec3 F_S(vec3 f0,float u){ float x=clamp(1.0-u,0.0,1.0);\n"
"  float x2=x*x; return f0+(1.0-f0)*(x2*x2*x); }\n"
"float D_GGX(float NoH,float a){float a2=a*a;float d=NoH*NoH*(a2-1.0)+1.0;return a2/(3.14159265*d*d);}\n"
"float V_S(float NoV,float NoL,float a){float a2=a*a;\n"
"  float gv=NoL*sqrt(NoV*NoV*(1.0-a2)+a2), gg=NoV*sqrt(NoL*NoL*(1.0-a2)+a2);\n"
"  return 0.5/max(gv+gg,1e-5);}\n"
/* THE FOUR-CONSTANT FIT IS GONE.  It was standing in for the split sum's
   DFG term, and against the real integral it is off by 0.18 on the scale
   -- a sixth of the reflection, missing where a polished barrel is
   supposed to be brightest.  render.c integrates the table at load and
   lends it here, so the gun and the wall answer the same question the
   same way. */
"uniform sampler2D uDFG;\n"
"/* the current room's ambient cube, in engine light units -- the same probe\n"
"   the world shader reflects, so gun metal and wall metal share one room */\n"
"uniform vec3 uCube[6];\n"
"vec3 ambW(vec3 d){\n"
"  vec3 s=d*d;\n"
"  return s.x*(d.x>0.0?uCube[0]:uCube[1])\n"
"       + s.y*(d.y>0.0?uCube[2]:uCube[3])\n"
"       + s.z*(d.z>0.0?uCube[4]:uCube[5]);}\n"
/* AND THE ROOM ITSELF, not just its average.  uPBase names the layer the
   zone's probe starts at, uPN its inner edge and uPLev the last roughness
   level; bake.c traced them, render.c uploaded them, and this is the same
   lookup the world shader does -- if the two ever disagree the gun stops
   belonging to the corridor it is being carried down. */
"uniform sampler2DArray uProbe;\n"
"uniform float uPN,uPLev,uPBase;\n"
"vec2 octEnc(vec3 d){\n"
"  d/=(abs(d.x)+abs(d.y)+abs(d.z));\n"
"  vec2 p=vec2(d.x,d.z);\n"
"  if(d.y<0.0) p=vec2((1.0-abs(p.y))*(p.x>=0.0?1.0:-1.0),\n"
"                     (1.0-abs(p.x))*(p.y>=0.0?1.0:-1.0));\n"
"  return p;}\n"
"vec3 probeAt(vec3 R,float rough){\n"
"  vec2 t=((octEnc(R)*0.5+0.5)*uPN+1.0)/(uPN+2.0);\n"
"  float lv=clamp(rough,0.0,1.0)*uPLev;\n"
"  float l0=floor(lv);\n"
"  vec3 a=texture(uProbe,vec3(t,uPBase+l0)).rgb;\n"
"  vec3 b=texture(uProbe,vec3(t,uPBase+min(l0+1.0,uPLev))).rgb;\n"
"  return mix(a,b,lv-l0);}\n"
"/* value noise for the flash.  Sampled on the unit circle so the angular\n"
"   bands close on themselves -- a seam would be its own kind of regularity. */\n"
"float h21(vec2 p){p=fract(p*vec2(127.1,311.7));p+=dot(p,p+34.53);return fract(p.x*p.y);}\n"
"float vn2(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);\n"
"  return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),\n"
"             mix(h21(i+vec2(0,1)),h21(i+vec2(1,1)),f.x),f.y);}\n"
"float fb2(vec2 p){float s=0.0,a=0.5;\n"
"  for(int i=0;i<4;i++){s+=a*vn2(p);p=p*2.07+3.7;a*=0.5;}return s;}\n"
"vec3 shade(vec3 N,vec3 V,vec3 L,vec3 rad,vec3 alb,float met,float rgh){\n"
"  vec3 H=normalize(V+L);\n"
"  float NoL=max(dot(N,L),0.0), NoV=max(dot(N,V),1e-4);\n"
"  float NoH=max(dot(N,H),0.0), VoH=max(dot(V,H),0.0);\n"
"  float a=max(rgh*rgh,0.008);\n"
"  vec3 f0=mix(vec3(0.04),alb,met);\n"
"  vec3 F=F_S(f0,VoH);\n"
"  vec3 spec=F*D_GGX(NoH,a)*V_S(NoV,NoL,a);\n"
"  vec3 kd=(1.0-F)*(1.0-met);\n"
"  return (kd*alb/3.14159265+spec)*rad*NoL;\n"
"}\n"
"void main(){\n"
"  int m=int(vM+0.5);\n"
"  if(m==20){                     // flash card, camera-facing, unlit additive\n"
"    vec2 p=vUV*2.0-1.0;\n"
"    float r=length(p);\n"
"    if(r>1.0){ o=vec4(0.0); return; }\n"
"    /* The bloom pass: a round glow with no structure, drawn without the\n"
"       depth test.  A flash this bright bleeds over the barrel in any real\n"
"       camera -- what looked pasted on before was the petals doing it. */\n"
"    if(uMuzG>0.5){\n"
"      float g=pow(1.0-r,2.6);\n"
"      o=vec4(vec3(1.0,0.55,0.18)*g*uMuzI,1.0); return;\n"
"    }\n"
"    vec2 dir=p/max(r,1e-4);      // on the unit circle: the bands have no seam\n"
"    // a few directions carry most of the fire -- pow() turns the soft noise\n"
"    // into spikes, which is what stops the burst reading as a disc\n"
"    float a1=fb2(dir*2.60+vec2(uMuzS,uMuzS*1.7));\n"
"    float a2=fb2(dir*6.10-vec2(uMuzS*2.1,uMuzS));\n"
"    float lobe=0.26+0.60*pow(clamp(a1*1.18,0.0,1.0),2.0)+0.16*a2;\n"
"    float e=r/max(lobe,1e-3);                      // 0 centre .. 1 petal tip\n"
"    float body=smoothstep(1.0,0.45,e)*(1.0-min(e,1.0));\n"
"    body*=0.35+0.65*fb2(dir*12.0+vec2(uMuzS*3.3,e*4.0));   // ragged rim\n"
"    float core=exp(-r*r*26.0);   // the heart is a fixed size, not a petal\n"
"    // saturated, or the tonemap washes the whole burst to grey\n"
"    vec3 c=mix(vec3(1.0,0.58,0.14),vec3(0.95,0.13,0.02),clamp(e,0.0,1.0));\n"
"    c=mix(c,vec3(1.0,0.96,0.86),core);\n"
"    o=vec4(c*(body*2.2+core*2.4)*uMuzI,1.0); return;\n"
"  }\n"
"  vec3 aA=texture(tA0,vUV).rgb, nA=texture(tA1,vUV).xyz*2.0-1.0; vec4 rA=texture(tA2,vUV);\n"
"  vec3 aB=texture(tB0,vUV).rgb, nB=texture(tB1,vUV).xyz*2.0-1.0; vec4 rB=texture(tB2,vUV);\n"
"  vec3 aC=texture(tC0,vUV).rgb, nC=texture(tC1,vUV).xyz*2.0-1.0; vec4 rC=texture(tC2,vUV);\n"
"  vec3 alb; vec3 nT; float rgh,met,ao; vec3 emis=vec3(0.0);\n"
"  if(m==1){ alb=aB; nT=nB; rgh=rB.x; met=0.78; ao=rB.z; }\n"
"  else    { alb=aA; nT=nA; rgh=rA.x; met=0.78; ao=rA.z; }\n"
"  if(m==2){\n"
"    alb=mix(uAccent,uAccent*0.23,1.0-rA.z)*(0.6+0.8*rA.z);\n"
"    met=0.0; rgh=clamp(0.36+0.35*(1.0-rA.z),0.2,0.85); nT=nA;\n"
"  } else if(m==3){\n"
"    alb=vec3(0.016,0.017,0.019); met=0.0; rgh=0.86; nT=nA*vec3(1.6,1.6,1.0);\n"
"  } else if(m==4){\n"
"    alb=vec3(0.02); met=0.0; rgh=0.35;\n"
"    float fl=0.86+0.14*sin(uTime*11.0+vP.z*7.0);\n"
"    emis=vec3(0.16,0.86,1.0)*2.6*fl;\n"
"  } else if(m==5){\n"
"    alb=vec3(0.006); met=1.0; rgh=0.55; ao=0.25;\n"
"  } else if(m==13){\n"
"    alb=mix(uPaint,uPaint*0.26,1.0-rC.z)*(0.62+0.76*rC.z);\n"
"    alb=mix(alb,vec3(0.155,0.150,0.140),smoothstep(0.60,0.95,rC.w)*0.34);\n"
"    met=0.06; rgh=clamp(0.46+0.28*(1.0-rC.z),0.26,0.90); nT=nC;\n"
"  } else if(m==14){\n"
"    alb=aC*1.30; met=0.80; ao=rC.z;\n"
"    rgh=clamp(rC.x*0.72,0.10,0.55); nT=nC;\n"
"  } else if(m==12){\n"
"    alb=aB*1.10; met=1.0; ao=rB.z;\n"
"    rgh=clamp(rB.x*0.42+0.04,0.05,0.34);\n"
"    nT=normalize(mix(vec3(0.0,0.0,1.0),nB,0.16));\n"
"  } else if(m==11){\n"
"    alb=mix(vec3(0.44,0.055,0.030),vec3(0.15,0.020,0.012),1.0-rB.z);\n"
"    met=0.0; rgh=clamp(0.32+0.26*(1.0-rB.z),0.18,0.80); nT=nB;\n"
"  } else if(m==10){\n"
"    alb=mix(vec3(0.60,0.40,0.13),vec3(0.26,0.16,0.05),1.0-rB.z);\n"
"    met=1.0; rgh=clamp(0.16+0.34*(1.0-rB.z),0.10,0.62); nT=nB;\n"
"  } else if(m==8){\n"
"    float tt=fract((vUV.x+vUV.y)*3.2);\n"
"    float w=max(fwidth(tt)*1.2,0.012);\n"
"    float st=smoothstep(0.5-w,0.5+w,tt)*(1.0-smoothstep(1.0-w,1.0,tt));\n"
"    vec3 yl=vec3(0.52,0.34,0.020), bk=vec3(0.012,0.012,0.013);\n"
"    alb=mix(bk,yl,st);\n"
"    alb=mix(alb,vec3(0.16,0.15,0.13),smoothstep(0.55,0.95,rA.w)*0.35);\n"
"    alb*=0.55+0.55*rA.z;\n"
"    met=0.0; rgh=clamp(0.44+0.30*(1.0-rA.z),0.25,0.9); nT=nA;\n"
"  } else if(m==6){\n"
"    alb=mix(vec3(0.030,0.033,0.028),vec3(0.075,0.070,0.055),rA.z)*0.9;\n"
"    met=0.12; rgh=clamp(0.52+0.25*(1.0-rA.z),0.3,0.9); nT=nA;\n"
"  }\n"
"  /* barrel heat: red must saturate while green/blue stay on the linear\n"
"     part of the tonemap curve, or the glow turns white */\n"
"  if((m==1||m==12)&&uHeat>0.24){\n"
"    float h=clamp((uHeat-0.24)/0.76,0.0,1.0);\n"
"    float v=0.68+0.32*rB.w;\n"
"    float fl=0.90+0.10*sin(uTime*23.0+vUV.y*9.0);\n"
"    emis+=mix(vec3(1.0,0.035,0.004),vec3(1.0,0.095,0.020),h)*pow(h,1.6)*2.5*v*fl;\n"
"    rgh=clamp(rgh*0.85,0.08,0.90);\n"
"  }\n"
"  vec3 N0=normalize(vN);\n"
"  vec3 T=normalize(vT-N0*dot(N0,vT));\n"
"  vec3 B=cross(N0,T)*vW;\n"
"  vec3 N=normalize(mat3(T,B,N0)*normalize(nT));\n"
"  /* specular AA: where the bump normals churn within a pixel, GGX highlights\n"
"     shrink below pixel size and sparkle -- widen roughness to match */\n"
"  { vec3 du=dFdx(N),dv=dFdy(N);\n"
"    float varN=0.25*(dot(du,du)+dot(dv,dv));\n"
"    rgh=sqrt(min(rgh*rgh+min(2.0*varN,0.18),1.0)); }\n"
"  vec3 V=normalize(-vP);\n"
"  if(m==9){                                // digital ammo counter\n"
"    vec2 p=vUV;\n"
"    vec3 c=vec3(0.008,0.011,0.013);\n"
"    if(p.x>0.035&&p.x<0.965&&p.y>0.07&&p.y<0.93){\n"
"      float lit=0.0,gh=0.0;\n"
"      for(int k=0;k<3;k++){\n"
"        vec2 q=vec2((p.x-(0.09+float(k)*0.28))/0.24,(p.y-0.16)/0.68);\n"
"        if(q.x<-0.25||q.x>1.25) continue;\n"
"        int dv = k==0?100:(k==1?10:1);\n"
"        int mk = uAmmo<0 ? 64 : DMASK[(uAmmo/dv)%10];\n"
"        for(int i=0;i<7;i++){\n"
"          float dd=sdSeg(q,SEGS[i].xy,SEGS[i].zw)-0.052;\n"
"          float a=smoothstep(0.022,-0.012,dd);\n"
"          gh+=a*0.05;\n"
"          if(((mk>>i)&1)==1) lit+=a;\n"
"        }\n"
"      }\n"
"      vec3 on=vec3(0.18,1.0,0.82);\n"
"      float bl=uAmmo<0?(0.35+0.65*step(0.0,sin(uTime*18.0))):1.0;\n"
"      c+=on*(min(lit,1.0)*3.0+gh*0.6)*bl;\n"
"      c*=0.90+0.10*sin(p.y*180.0);\n"
"    }\n"
"    c+=ambW(uV2W*reflect(-V,normalize(vN)))*0.12;\n"
"    o=vec4(c*uGain*2.2,1.0); return;\n"
"  }\n"
"  if(dot(N,V)<0.0) N=normalize(N-V*dot(N,V)*1.2);\n"
"\n"
"  float lum=dot(uAmb,vec3(0.35,0.5,0.15));\n"
"  vec3 col=vec3(0.0);\n"
"  // key: the room's dominant light\n"
"  col+=shade(N,V,normalize(uLDir),uAmb*2.8,alb,met,rgh);\n"
"  // fill from up-left of the camera, rim from the right (scaled to the room)\n"
"  col+=shade(N,V,normalize(vec3(-0.35,0.55,0.75)),uAmb*0.9+vec3(0.008),alb,met,rgh);\n"
"  col+=shade(N,V,normalize(vec3(0.80,0.25,-0.40)),uAmb*0.55,alb,met,rgh);\n"
"  { vec3 d=uMuzP-vP; float dist=length(d);\n"
"    vec3 rad=uMuzC/(1.0+3.2*dist*dist);\n"
"    col+=shade(N,V,d/max(dist,1e-4),rad,alb,met,rgh); }\n"
"  if(uFlashI>0.0){\n"
"    vec3 d=-vP; float dist=length(d); vec3 L=d/max(dist,1e-4);\n"
"    float cone=smoothstep(0.72,0.92,dot(-L,vec3(0.0,0.0,-1.0)));\n"
"    vec3 rad=vec3(1.0,0.94,0.86)*uFlashI*cone/(1.0+0.18*dist*dist);\n"
"    col+=shade(N,V,L,rad,alb,met,rgh);\n"
"  }\n"
"  float NoV=max(dot(N,V),1e-4);\n"
"  vec3 R=reflect(-V,N);\n"
"  vec3 f0=mix(vec3(0.04),alb,met);\n"
/* the same three terms the world shader uses, in the same order: the
   prefiltered radiance, the integrated DFG, and the energy the split sum
   drops on the second bounce.  The two hand-tuned boosts that used to sit
   here (x2 for metal, x3 for gloss) were compensating for a probe that had
   no detail in it; it has detail now. */
"  vec2 dfg=texture(uDFG,vec2(NoV,rgh)).rg;\n"
"  vec3 FssEss=f0*dfg.x+dfg.y;\n"
"  float Ems=1.0-(dfg.x+dfg.y);\n"
"  vec3 Favg=f0+(1.0-f0)*0.047619;\n"
"  vec3 Fms=FssEss*Favg/(1.0-Ems*Favg);\n"
"  col+=(probeAt(uV2W*R,rgh)*FssEss+(Fms*Ems)*ambW(uV2W*N))*ao;\n"
"  col+=alb*(1.0-met)*(uAmb*0.55+ambW(uV2W*N)*0.45)*ao;\n"
"  col+=emis*uGain*1.6;\n"
"  o=vec4(col,1.0);\n"
"}\n";


/* --------------------------------------------------------------- GL */
#define RESW 512

/* the ref-tuned material parameters, one set behind all three guns */
static const struct { float str,micro,cells,rust,rgh,rghv;
                      unsigned steel,rustc,worn,accent,paint; } TP =
  { 1.0f, 0.2f, 4.0f, 0.52f, 0.23f, 0.25f,
    0x55575a, 0x5d4b3c, 0x838588, 0xd47e31, 0x4c5533 };

static GLuint TEXW[3][3];
static GLuint genProg, genFBO, genVAO, vmProg;
static struct {
  GLint uProj,uModel,uNM,uAmb,uLDir,uV2W,uCube,uMuzP,uMuzC,uFlashI,uMuzI,
        uMuzS,uMuzG,uGain,uTime,uAmmo,uHeat,uAccent,uPaint,
        uProbe,uDFG,uPN,uPLev,uPBase;
  GLint tex[9];
} UW;

typedef struct { GLuint vao,vb; int count; } VMesh;
typedef struct { VMesh stat,spin,hands,hmov,magm; } Gun;
static Gun WMESH[3];
static VMesh mShell,mShot,mCard;

static GLuint makeTex(void){
  GLuint t; glGenTextures(1,&t);
  glBindTexture(GL_TEXTURE_2D,t);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,RESW,RESW,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  return t;
}

/* Three surfaces -- plate, machined, hard-edged -- each written as albedo,
   normal and RMA in ONE pass over a full-screen triangle.  The height field
   is evaluated five times per pixel: the value, two neighbours for the
   gradient and a wide four-tap for the cavity term.  Doing that on the CPU
   at 512^2 x 3 modes x 5 taps would cost more than the ship's whole
   material compile, and the answer is a texture either way. */
static void generateTextures(uint32_t seed){
  seedRng(seed);
  GLfloat mx=0;
  glBindVertexArray(genVAO);
  glBindFramebuffer(GL_FRAMEBUFFER,genFBO);
  glViewport(0,0,RESW,RESW);
  glUseProgram(genProg);
  glDisable(GL_DEPTH_TEST);
  #define GU(k) glGetUniformLocation(genProg,k)
  glUniform1f(GU("uFlat"),52.0f/TP.str);
  glUniform1f(GU("uMicro"),TP.micro);
  glUniform1f(GU("uCells"),TP.cells);
  glUniform1f(GU("uRustAmt"),TP.rust);
  glUniform1f(GU("uRghBase"),TP.rgh);
  glUniform1f(GU("uRghVar"),TP.rghv);
  { float c[3];
    lin(TP.steel,c); glUniform3fv(GU("uSteel"),1,c);
    lin(TP.rustc,c); glUniform3fv(GU("uRust"),1,c);
    lin(TP.worn,c);  glUniform3fv(GU("uWorn"),1,c); }
  glGetFloatv(GL_MAX_TEX_ANISO,&mx);
  for(int mode=0;mode<3;mode++){
    static const GLenum AT[3]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,
                               GL_COLOR_ATTACHMENT2};
    for(int i=0;i<3;i++)
      glFramebufferTexture2D(GL_FRAMEBUFFER,AT[i],GL_TEXTURE_2D,TEXW[mode][i],0);
    glDrawBuffers(3,AT);
    glUniform2f(GU("uSeed"),rng()*40.f,rng()*40.f);
    glUniform1i(GU("uMode"),mode);
    glUniform1f(GU("uRes"),(float)RESW);
    glDrawArrays(GL_TRIANGLES,0,3);
    for(int i=0;i<3;i++){
      glBindTexture(GL_TEXTURE_2D,TEXW[mode][i]);
      glGenerateMipmap(GL_TEXTURE_2D);
      if(mx>1) glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY,
                               mx<8.f?mx:8.f);
    }
  }
  #undef GU
  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glEnable(GL_DEPTH_TEST);
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(0);
}

/* pos(3) nrm(3) uv(2) tan(4) mat(1) -- 13 floats, one interleaved buffer */
static VMesh upload(Raw r){
  VMesh m; m.count=r.nv;
  glGenVertexArrays(1,&m.vao); glBindVertexArray(m.vao);
  glGenBuffers(1,&m.vb); glBindBuffer(GL_ARRAY_BUFFER,m.vb);
  glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)r.nv*13*sizeof(float),r.v,GL_STATIC_DRAW);
  const GLsizei S=13*4;
  const int size[5]={3,3,2,4,1}, off[5]={0,12,24,32,48};
  for(int i=0;i<5;i++){
    glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer((GLuint)i,size[i],GL_FLOAT,GL_FALSE,S,
                          (const void*)(size_t)off[i]);
  }
  glBindVertexArray(0);
  free(r.v);
  return m;
}
/* -vmw: the mesh, as five numbers per part.

   THE CHECK IS THE SAME CHECK -mats MAKES.  The C mesh builder and the JS
   one must agree, and the cheap way to know is to sum every channel of
   every vertex on both sides -- node tools/vmwcheck.js prints the same
   table from map.html's own VMW section.  A wrong ring order, a dropped
   cap, a sign flip in the tangent or one bit of drift in mulberry32 all
   move one of these five numbers. */
static int DUMP=0;
static void dumpRaw(const char *name,Raw r){
  if(!DUMP) return;
  double sp=0,sn=0,su=0,st=0,sw=0,sm=0;
  for(int i=0;i<r.nv;i++){
    const float *q=r.v+(size_t)i*13;
    sp+=q[0]+q[1]+q[2];
    sn+=q[3]+q[4]+q[5];
    su+=q[6]+q[7];
    st+=q[8]+q[9]+q[10];
    sw+=q[11];
    sm+=q[12];
  }
  platLog("vmw  %-16s %6d  pos %12.4f  nrm %12.4f  uv %14.4f  tan %12.4f"
          "  hand %7.0f  mat %8.0f  fb %d/%d",
          name,r.nv,sp,sn,su,st,sw,sm,r.fbF,r.fbC);
}
static Gun uploadGun(const char *n,RawGun g){
  Gun o;
  char b[64];
  #define PART(f) do{ _snprintf(b,sizeof b,"%s.%s",n,#f); dumpRaw(b,g.f);                       o.f=upload(g.f); }while(0)
  PART(stat); PART(spin); PART(hands); PART(hmov); PART(magm);
  #undef PART
  return o;
}

/* ------------------------------------------- state + the weapon table */
typedef struct {
  const char *name;
  int   kind, wmesh, sfx;            /* sfx picks auGun's voice */
  float rate; int mag; float reload, reldly;
  int   pellets; float spread;
  float kickA,kickR,shake,vrec,vpush,vdec;
  float vm[3], muz, twin;
  float ejP[3], ejV[3]; int shell, ejShot;
  float heatAdd, mfl, flash, smoke;
  int   autoFire, spinUp;
  /* the muzzle's WORLD light: colour, then radius in metres.  It is not
     the flash quad and it is not uMuzC -- those two light the gun, and a
     gun lit against a room that stayed dark reads as a shot that lights
     nothing.  Measured, firing into the bunkroom and differencing the
     frame against the same frame unfired: the double-barrel washed the
     whole picture (+68 mean luminance, +20 at the frame edges) and the
     pistol lit a patch on the wall it was pointed at and nothing else
     (+130 in the two centre blocks, 0.0 at every edge) -- because 2.4 x
     mfl 1.3 over 7.5 m is a quarter of the shotgun's reach with a fifth
     of its energy.  The pistol and the rotary are brought up to where
     their own flash is an event in the room; the shotgun keeps its
     numbers, being already the one that worked. */
  float light[4];
  int   reserve;
  /* WHAT THE RACK HANDS OVER, which is not the same as what the gun can
     carry.  The first two are half a reserve, the way every pickup in the
     game has worked; the rotary is handed its whole belt.  L-033 calls it
     the answer to what is coming and it burns a round every 0.014 s --
     71 a second -- so half of 600 was four seconds of held trigger, and
     the gun the story promises the last stand on was empty before the
     first wave of it had crossed the hall. */
  int   giveN;
} Wep;

/* Damage, per round.  The double-barrel throws its pellets
   rather than one round, so its figure is PER PELLET.

   AND IT IS THE POWERFUL ONE, which the first numbers did not say.  Twelve
   at 7 is 84 into a body at contact -- less than half a brooder (180) for
   a two-round magazine and a 1.3 s reload, so the gun you walk the length
   of the ship for was strictly worse than standing still with the rotary.
   Fourteen at 11 is 154: a spitter (40) or a cocoon (60) folds to one
   barrel, a brooder to both, and a scuttler (24) dies to any three pellets
   of the fourteen.  The spread pays for it -- see the table below. */
static const float WDMG[3]={18.f, 11.f, 12.f};

/* The ref's three weapons on the engine's three gun slots.  The numbers
   are the ref's, not the engine's: rate, recoil and heat were tuned
   against these meshes, and the old table gives a pistol that cycles
   faster than its own slide can travel. */
static const Wep WEP[3]={
  { "PISTOL", 2,0,0,
    0.16f, 12, 1.35f, 0.10f,
    1, 0.008f,
    0.020f,0.008f,0.30f,1.6f,0.040f,8.0f,
    {0.15f,-0.145f,-0.42f}, -0.19f, 0.f,
    {0.028f,0.018f,0.030f},{1.9f,2.6f,-0.6f}, 0, 1,
    0.006f, 1.3f, 0.15f, 0.05f,
    0, 0,
    {1.9f,1.56f,0.86f,11.0f}, 340, 170 },
  /* THE PATTERN IS WHAT MAKES IT A SHOTGUN AND NOT A RIFLE THAT HURTS.
     0.062 is a 3.5-degree half-angle -- 12 cm across at two metres, which
     is a slug you have to aim, and it made the damage above feel like a
     bad rifle.  0.100 is 5.7 degrees, 40 cm at two metres and 1.0 m at
     five: everything in the room at contact, and most of the load in the
     air past six.  There is no range falloff on damage anywhere in this
     file, so the pattern IS the range limit, and widening it is what buys
     the 154 back. */
  { "DOUBLE-BARREL", 1,2,2,
    0.30f, 2, 1.30f, 0.55f,
    14, 0.100f,
    0.090f,0.026f,1.15f,4.6f,0.070f,5.0f,
    {0.19f,-0.21f,-0.31f}, -0.68f, 0.032f,
    {0.032f,0.010f,-0.050f},{0.35f,3.2f,-0.7f}, 1, 0,
    0.030f, 3.2f, 0.52f, 0.24f,
    0, 0,
    {1.0f,0.72f,0.34f,14.f}, 88, 44 },
  { "ROTARY CANNON", 0,1,1,
    0.055f, 0, 0.f, 0.f,
    1, 0.014f,
    0.0090f,0.0050f,0.10f,1.0f,0.034f,9.0f,
    {0.20f,-0.20f,-0.34f}, -0.70f, 0.f,
    {0.080f,0.082f,0.140f},{0.75f,3.9f,-2.6f}, 0, 1,
    0.0105f, 1.0f, 0.21f, 0.055f,
    1, 1,
    {1.9f,1.62f,0.95f,10.0f}, 999, 999 }
};

#define COOL_IDLE 0.17f
#define COOL_FIRE 0.02f
#define RESET_AT  0.45f
#define NSHELL    35

/* The break action, as one curve: snap open past the stop, settle, hold
   while the shells are swapped, close, and one small bounce on the latch. */
static float brkCurve(float u){
  if(u<0.16f){ float t=u/0.16f;        return 1.06f*(1.f-powf(1.f-t,3.f)); }
  if(u<0.26f){ float t=(u-0.16f)/0.10f;return 1.06f-0.06f*(t*t*(3.f-2.f*t)); }
  if(u<0.52f) return 1.0f;
  if(u<0.68f){ float t=(u-0.52f)/0.16f;return 1.0f-t*t*t; }
  if(u<0.82f){ float t=(u-0.68f)/0.14f;return 0.11f*sinf(t*(float)PI)*(1.f-t); }
  return 0.f;
}

static struct {
  /* HEAT IS PER GUN, NOT PER PLAYER.  One float for all three meant a
     rotary run up to overheat handed its glowing barrel to whatever was
     drawn next -- swap to the double-barrel and it comes out of the
     holster orange, with the OVERHEAT lock on a gun that has fired
     twice.  Three slots; all three cool every frame, because a holstered
     barrel is still losing heat. */
  float heat[3]; int over[3];
  float spin,spinV,recoil,jolt,climb,idle;
  float muzzle,flashRoll,flashSeed,slideZ,magDrop,brk,rlp,relU;
  int   spent,shotSide;
  float dryT,clickT,prevReload;
  int   ejd,ldd,snB;
  struct { float p[3],v[3],r[3],w[3],t; int s; } sh[NSHELL];
  int   nsh;
  M4    gunM,gunW;
  float gain,flash;
} A;

static struct { float kick,kickV,rot,rotV,swayX,swayY,tgtX,tgtY;
                float bob,draw,flash,flashRoll;
                float lit[3],ldir[3]; } VM;

static int   curW=0, wantW=0, holdFire=0, trigPend=0;

/* WHAT IS ACTUALLY MOVING, for auMech.  A service pistol's slide is 1.00
   by definition; the double-barrel swings two barrels and a standing
   breech and is the heaviest mechanism in the ship; the rotary's carrier
   sits between them.  It is the same argument sfxCreature takes as `sz`
   and it does the same thing -- lower and slower, not merely louder. */
static float mechW(void){
  static const float MW[3]={1.00f,1.30f,1.05f};
  int i=curW<0?0:(curW>2?2:curW);
  return MW[i];
}

/* -gun 3 only: the barrels are already turning.  Setting A.spinV once is
   not enough -- the spin block below drives it back to zero whenever the
   trigger is up, and the first frames after boot carry a fat dt, so it
   was under vmwTryFire's 26 rad/s gate by the time -fire pulled.  The
   flag holds the target up instead of the value. */
static int   forceSpin=0;
static float fireT=0, reloadT=0, litT=0, pendShake=0;
static int   AMMO[3], MAGS[3];
/* THE PLAYER STARTS THE STORY WITH EMPTY HANDS.  The story gives the
   pistol at the junction locker, the shotgun in stores and the machine
   gun at the cargo door, and the difficulty curve is bolted to that
   order -- so ownership is a flag the story sets, not a given. */
static int   OWNED[3]={1,1,1};
static float muzW[3];

/* THE MUZZLE LIGHT IS NOT THE GUN OWN PROPERTY ANY MORE.  It used to be a
   four-slot pool here, because the muzzle was the only thing in the game
   that made light of its own; a spark off steel is a second source, so the
   pool now lives in fx.c where both can reach it. */

/* THE ROOM LIGHTS THE GUN.  ent.c already samples the lamps for moving
   things, shadowed, and that is exactly the key a viewmodel wants -- it is
   why a pistol carried into the amber bay comes out amber.  257 lamps with
   a shadow ray each is not a per-frame cost, so it is resampled ten times a
   second, exactly as the browser did.  (sampleLight lives in ent.c.) */

/* the camera in vmToWorld's convention: x right, y up, -z forward */
static M4 camMat(const VmwCtx *C){
  M4 m;
  m.m[0]= C->camR[0]; m.m[1]= C->camR[1]; m.m[2]= C->camR[2]; m.m[3]=0;
  m.m[4]= C->camU[0]; m.m[5]= C->camU[1]; m.m[6]= C->camU[2]; m.m[7]=0;
  m.m[8]=-C->camF[0]; m.m[9]=-C->camF[1]; m.m[10]=-C->camF[2]; m.m[11]=0;
  m.m[12]=C->eye[0];  m.m[13]=C->eye[1];  m.m[14]=C->eye[2];  m.m[15]=1;
  return m;
}

static void ejectShell3D(const Wep *w,const float *po,const float *ve){
  if(A.nsh>=NSHELL){
    memmove(A.sh,A.sh+1,sizeof(A.sh[0])*(NSHELL-1));
    A.nsh=NSHELL-1;
  }
  const float *EP = po?po:w->ejP;
  const float *EV = ve?ve:w->ejV;
  float v0[3]={ (EP[0]<0?-EV[0]:EV[0])+rnd(0.35f),
                EV[1]+rnd01()*0.7f,
                EV[2]+rnd(0.5f) };
  int i=A.nsh++;
  xfp(&A.gunW,EP,A.sh[i].p);
  rot3(&A.gunW,v0,A.sh[i].v);
  for(int k=0;k<3;k++){ A.sh[i].r[k]=rnd01()*6.f; A.sh[i].w[k]=rnd(20.f); }
  A.sh[i].t=0; A.sh[i].s=w->shell;
}

/* One shot: recoil, heat, muzzle light, brass, sparks, smoke, and the
   rounds themselves. */
static void fireVMW(const VmwCtx *C,const Wep *w,float *pitch,float *yaw){
  A.heat[curW] = A.heat[curW]+w->heatAdd < 1.f ? A.heat[curW]+w->heatAdd : 1.f;
  if(A.heat[curW]>=1.f) A.over[curW]=1;
  A.muzzle=1; A.flashRoll=rnd01()*6.2832f; A.flashSeed=rnd01()*64.f;
  A.recoil=w->vrec;
  A.shotSide^=1;
  float k=w->kickA+rnd01()*w->kickR;
  *pitch = *pitch+k < 1.45f ? *pitch+k : 1.45f;
  *yaw  += rnd(w->kickA*0.6f);
  A.climb+=k*0.65f;
  pendShake+=w->shake;
  VM.flash=1; VM.flashRoll=A.flashRoll;
  A.gunW=mMul2(camMat(C),A.gunM);
  float mx = w->twin!=0 ? (A.shotSide?w->twin:-w->twin) : 0.f;
  float q[3], src[3]={mx,0,w->muz};
  xfp(&A.gunW,src,q);
  muzW[0]=q[0]; muzW[1]=q[1]; muzW[2]=q[2];
  fxAddDynLight(muzW[0],muzW[1],muzW[2],
                w->light[0]*2.4f*w->mfl, w->light[1]*2.4f*w->mfl,
                w->light[2]*2.4f*w->mfl, w->light[3], 0.075f);

  /* ---- what leaves the barrel besides the round.  ONE CALL PER STATEMENT:
     C does not order its arguments, so a folded list of rnd() calls comes
     out backwards under gcc and the cone is not the cone the previz has. */
  { int nspark = w->pellets>1 ? 9 : 4;
    for(int i=0;i<nspark;i++){
      float a=rnd01()*6.28f, r=rnd01()*0.5f;
      float ca=cosf(a)*r, sa=sinf(a)*r;
      float f0=5.f+rnd01()*7.f, f1=5.f+rnd01()*7.f, f2=5.f+rnd01()*7.f;
      float lf=0.12f+rnd01()*0.1f;
      fxEmit(muzW[0],muzW[1],muzW[2],
             C->camF[0]*f0+C->camR[0]*ca+C->camU[0]*sa,
             C->camF[1]*f1+C->camU[1]*sa,
             C->camF[2]*f2+C->camR[2]*ca+C->camU[2]*sa,
             0.045f,lf,PK_SPARK, 1.0f,0.80f,0.42f,1.0f, 3.f,3.5f,1,0);
    } }
  if(w->mag>0||((AMMO[curW]&3)==0))
    fxEmit(muzW[0]+C->camF[0]*0.3f,muzW[1]+C->camF[1]*0.3f,
           muzW[2]+C->camF[2]*0.3f,
           C->camF[0]*1.2f,C->camF[1]*1.2f+0.4f,C->camF[2]*1.2f,
           w->smoke>0.1f?0.30f:0.18f, 0.55f,PK_SMOKE,
           0.44f,0.42f,0.40f,0.32f, -0.3f,2.0f,0,0);

  if(w->ejShot) ejectShell3D(w,NULL,NULL);
  else A.spent = A.spent+1 < w->mag ? A.spent+1 : w->mag;

  /* ---- the rounds.  THE DOUBLE-BARREL IS TWELVE RAYS, NOT ONE, and the
     spread is taken in the CAMERA PLANE so it stays a circle whichever way
     you are facing -- taken in yaw and pitch it would flatten as you look
     up.  It opens when you are moving or in the air and closes when you
     crouch.  There are still no bodies in the ship, so what a pellet buys
     is the crater, the sparks and the sound of what it hit; when the
     zombies land this is the trace they will be tested against unchanged. */
  { float ms=C->moveSpeed*0.16f; if(ms>1.4f) ms=1.4f;
    float sprMul=(C->crouch?0.55f:1.f)*(C->grounded?1.f:2.0f)*(1.f+ms);
    for(int i=0;i<w->pellets;i++){
      float sprd=w->spread*sprMul;
      float a=rnd01()*6.28f, r=sqrtf(rnd01())*sprd;
      float ca=cosf(a)*r, sa=sinf(a)*r;
      float dx=C->camF[0]+C->camR[0]*ca+C->camU[0]*sa;
      float dy=C->camF[1]+C->camR[1]*ca+C->camU[1]*sa;
      float dz=C->camF[2]+C->camR[2]*ca+C->camU[2]*sa;
      float m=1.f/hypot3(dx,dy,dz); dx*=m; dy*=m; dz*=m;
      int b=rayTraceOne(C->eye[0],C->eye[1],C->eye[2],dx,dy,dz,90.f,-1);
      /* read the hit BEFORE anything else traces: fxSurfaceHit samples the
         lamps for the crater and overwrites all three of these */
      float d=rayHitDist; int ax=rayHitAx, sg=rayHitSg;
      float wall = b<0 ? 1e9f : d;
      /* THE BODY IS TESTED BEFORE THE WALL BEHIND IT.  Four spheres a
         creature, and the first one is the cranium -- the weak
         point, worth two and a half times a body shot. */
      float zt; int zhead;
      int zi=zomTrace(C->eye[0],C->eye[1],C->eye[2],dx,dy,dz,90.f,&zt,&zhead);
      if(zi>=0&&zt<wall){
        float hp[3]={C->eye[0]+dx*zt, C->eye[1]+dy*zt, C->eye[2]+dz*zt};
        zomHurt(zi,WDMG[curW]*(zhead?2.5f:1.f),hp[0],hp[1],hp[2],dx,dy,dz,zhead,0);
        if(i==0&&rnd01()<(w->kind==0?0.5f:0.7f)) fxTracer(muzW,hp);
        continue;
      }
      if(b<0) continue;
      float hp[3]={C->eye[0]+dx*d, C->eye[1]+dy*d, C->eye[2]+dz*d};
      float n[3]={ax==0?(float)sg:0.f, ax==1?(float)sg:0.f, ax==2?(float)sg:0.f};
      fxSurfaceHit(b,hp[0],hp[1],hp[2],n[0],n[1],n[2]);
      if(i==0&&rnd01()<(w->kind==0?0.5f:0.7f)) fxTracer(muzW,hp);
    } }
}

static void vmwTryFire(VmwCtx *C);

/* per-frame weapon dynamics -- updateWeapons and stepVMW, in that order */
void vmwStep(VmwCtx *C,float dt,float rdt){
  const Wep *w;
  (void)dt;
  C->shake=0;

  /* ---- the engine half: draw, timers, sway, sampled light ---------- */
  if(wantW!=curW&&VM.draw<=0.02f) curW=wantW;
  { float target=(wantW==curW)?1.f:0.f, k=rdt*11.f; if(k>1)k=1;
    VM.draw+=(target-VM.draw)*k; }
  if(VM.draw<0.02f&&wantW!=curW) curW=wantW;
  w=&WEP[curW];

  fireT-=rdt; if(fireT<0) fireT=0;
  if(reloadT>0){
    reloadT-=rdt;
    if(reloadT<=0){                              /* finishReload */
      int need=w->mag-MAGS[curW], take=need<AMMO[curW]?need:AMMO[curW];
      MAGS[curW]+=take; AMMO[curW]-=take;
      auMech(MCH_READY,mechW());
      reloadT=0;
    }
  }
  VM.flash=VM.flash-rdt*15.f>0?VM.flash-rdt*15.f:0;

  if(trigPend){ trigPend=0; vmwTryFire(C); }
  if(holdFire&&w->autoFire) vmwTryFire(C);
  w=&WEP[curW];

  VM.kickV-=VM.kick*90.f*rdt; VM.kickV*=(1-rdt*13.f>0?1-rdt*13.f:0); VM.kick+=VM.kickV*rdt;
  VM.rotV -=VM.rot *80.f*rdt; VM.rotV *=(1-rdt*12.f>0?1-rdt*12.f:0); VM.rot +=VM.rotV *rdt;
  VM.swayX+=(VM.tgtX-VM.swayX)*(rdt*9.f<1.f?rdt*9.f:1.f); VM.tgtX*=(1-rdt*7.f>0?1-rdt*7.f:0);
  VM.swayY+=(VM.tgtY-VM.swayY)*(rdt*9.f<1.f?rdt*9.f:1.f); VM.tgtY*=(1-rdt*7.f>0?1-rdt*7.f:0);
  { float t=(C->moveSpeed>0.4f&&C->grounded)?1.f:0.f, k=rdt*7.f; if(k>1)k=1;
    VM.bob+=(t-VM.bob)*k; }
  litT-=rdt;
  if(litT<=0){
    litT=0.1f;
    sampleLight(C->eye[0],C->eye[1],C->eye[2],VM.lit,VM.ldir);
    /* bias the key toward the camera so the gun never goes fully black */
    VM.lit[0]+=0.06f; VM.lit[1]+=0.062f; VM.lit[2]+=0.07f;
  }

  /* ---- heat and overheat.  Each gun feeds its OWN slot; only the
     cannon moves it far.  The held trigger slows the cooling of the gun
     it is held on and of nothing else -- the other two are in a holster. */
  for(int i=0;i<3;i++){
    float cool=(i==curW&&holdFire&&!A.over[i])?COOL_FIRE:COOL_IDLE;
    A.heat[i]-=rdt*cool;
    if(A.heat[i]<0) A.heat[i]=0;
    if(A.over[i]&&A.heat[i]<RESET_AT) A.over[i]=0;
  }

  /* ---- barrel spin-up --------------------------------------------- */
  { int hasAmmo = w->mag>0 ? MAGS[curW]>0 : AMMO[curW]>0;
    int wantSpin = w->spinUp&&(holdFire||forceSpin)&&hasAmmo&&!A.over[curW]&&reloadT<=0;
    float target=wantSpin?36.f:0.f, k=rdt*(wantSpin?2.6f:3.4f);
    if(k>1) k=1;
    A.spinV+=(target-A.spinV)*k;
    A.spin+=A.spinV*rdt;
    if(A.spin>62.83f) A.spin-=62.83f; }

  /* ---- reload staging: eject, load, snap back --------------------- */
  if(reloadT>0&&w->mag>0){
    if(A.prevReload<=0){ A.ejd=0; A.ldd=0; A.snB=0; }
    A.relU=1.f-reloadT/w->reload;
    { float a=A.relU/0.10f, b=(1.f-A.relU)/0.14f;
      A.rlp=(a<1?a:1)*(b<1?b:1); }
    if(w->kind==1) A.brk=brkCurve(A.relU);
    { float ejAt = w->kind==2?0.18f:0.12f;
      if(!A.ejd&&A.relU>ejAt){
        A.ejd=1;
        if(A.spent>0){
          if(A.jolt<0.60f) A.jolt=0.60f;
          for(int i=0;i<A.spent;i++){
            float po[3]={(i?1.f:-1.f)*0.032f,0.012f,-0.048f};
            float ve[3]={0.40f,4.6f,-1.0f};
            ejectShell3D(w,po,ve);
          }
          A.spent=0;
          auMech(MCH_EJECT,mechW());
        } else { if(A.jolt<0.38f) A.jolt=0.38f; auMech(MCH_CYCLE,mechW()); }
      } }
    { float ldAt = w->kind==2?0.76f:0.42f;
      if(!A.ldd&&A.relU>ldAt){
        A.ldd=1;
        if(w->kind==2&&A.jolt<0.70f) A.jolt=0.70f;
        auMech(MCH_MAG_IN,mechW());
      } }
    if(w->kind==2&&!A.snB&&A.relU>0.88f){
      A.snB=1;
      if(A.jolt<0.85f) A.jolt=0.85f;
      if(A.recoil<0.5f) A.recoil=0.5f;
      auMech(MCH_SLIDE,mechW());          /* kind 2 is the pistol */
    }
    if(w->kind!=2&&!A.snB&&A.relU>0.68f){
      A.snB=1; A.jolt=1.f;
      if(A.recoil<0.85f) A.recoil=0.85f;
      auMech(MCH_BREAK,mechW());          /* ... and this is the barrels */
      /* the breech coming shut throws its own sparks */
      A.gunW=mMul2(camMat(C),A.gunM);
      { float src[3]={0,0.015f,-0.048f}, bp[3];
        xfp(&A.gunW,src,bp);
        for(int i=0;i<6;i++){
          float vx=rnd(1.6f), vy=0.4f+rnd01()*1.1f, vz=rnd(1.6f);
          float lf=0.12f+rnd01()*0.12f;
          fxEmit(bp[0],bp[1],bp[2],vx,vy,vz,0.011f,lf,PK_SPARK,
                 1.0f,0.80f,0.46f,0.9f,8.f,2.4f,1,0);
        } }
    }
  } else {
    A.relU=0;
    A.rlp=A.rlp-rdt*7.f>0?A.rlp-rdt*7.f:0;
    A.brk = w->kind==1 ? A.brk*(1-rdt*10.f>0?1-rdt*10.f:0) : 0;
  }
  A.prevReload=reloadT;

  /* ---- the pistol slide: 35% of the cycle back, 65% forward, and it
     stays back when the magazine is out ------------------------------ */
  if(w->kind==2){
    int empty = MAGS[curW]<=0 && (reloadT<=0||A.relU<0.86f);
    float c=0;
    if(fireT>0){
      float u=1.f-fireT/w->rate;
      c = u<0.35f ? u/0.35f : 1.f-(u-0.35f)/0.65f;
      if(c<0) c=0;
    }
    A.slideZ=(empty?1.f:c)*0.046f;
    A.magDrop = reloadT<=0 ? 0
      : A.relU<0.22f ? 0.30f*(A.relU/0.22f)
      : A.relU<0.56f ? 0.30f
      : A.relU<0.80f ? 0.30f*(1.f-(A.relU-0.56f)/0.24f) : 0;
  } else { A.slideZ=0; A.magDrop=0; }

  /* delayed auto-reload after the last round (reldly keeps it off the
     recoil, so the gun finishes kicking before the hands move) */
  if(A.dryT>0){
    A.dryT-=rdt; if(A.dryT<0) A.dryT=0;
    if(A.dryT<=0&&w->mag>0&&MAGS[curW]<=0&&AMMO[curW]>0&&reloadT<=0) vmwReload();
  }
  /* recoil recovery: only 65% of the climb comes back -- fireVMW banked
     the rest, which is what makes a burst walk up the wall */
  if(!holdFire&&A.climb>0){
    float r=A.climb<rdt*0.95f?A.climb:rdt*0.95f;
    C->pitch-=r; A.climb-=r;
  }
  A.muzzle=A.muzzle-rdt*16.f>0?A.muzzle-rdt*16.f:0;
  A.recoil=A.recoil-rdt*w->vdec>0?A.recoil-rdt*w->vdec:0;
  A.jolt  =A.jolt  -rdt*13.f  >0?A.jolt  -rdt*13.f  :0;
  /* idle sway lives while standing still and dies fast on any action */
  { int act=holdFire||fireT>0||reloadT>0||C->moveSpeed>0.4f;
    A.idle=cl(A.idle+(act?-rdt*4.0f:rdt*1.2f)); }
  /* the overheated trigger: a dry clunk, no round, and chamber sparks */
  if(A.over[curW]&&holdFire){
    A.clickT-=rdt;
    if(A.clickT<=0){
      A.clickT=0.13f;
      if(A.recoil<0.22f) A.recoil=0.22f;
      auMech(MCH_SEIZE,mechW());
      A.gunW=mMul2(camMat(C),A.gunM);
      { float src[3]={0,0.02f,-0.10f}, bp[3];
        xfp(&A.gunW,src,bp);
        for(int i=0;i<3;i++){
          float vx=rnd(1.2f), vy=0.5f+rnd01()*0.9f, vz=rnd(1.2f);
          float lf=0.14f+rnd01()*0.12f;
          fxEmit(bp[0],bp[1],bp[2],vx,vy,vz,0.010f,lf,PK_SPARK,
                 1.0f,0.74f,0.34f,0.85f,7.f,2.f,1,0);
        } }
    }
  }
  /* and a hot housing leaks smoke, which is the only tell that the cannon
     is about to stop working */
  if(A.heat[curW]>RESET_AT&&rnd01()<rdt*A.heat[curW]*16.f){
    A.gunW=mMul2(camMat(C),A.gunM);
    { float src[3], hp[3];
      src[0]=rnd(0.06f); src[1]=0.09f; src[2]=-0.10f+rnd(0.06f);
      xfp(&A.gunW,src,hp);
      float vx=rnd(0.25f), vy=0.55f+rnd01()*0.5f, vz=rnd(0.25f);
      float lf=0.6f+rnd01()*0.5f;
      fxEmit(hp[0],hp[1],hp[2],vx,vy,vz,0.06f,lf,PK_SMOKE,
             0.36f,0.33f,0.32f,0.26f*A.heat[curW],-0.5f,2.0f,0,0); }
  }
  /* spent casings: gravity, one floor bounce, and tumbling */
  { float floorY=C->feetY+0.012f;
    for(int i=A.nsh-1;i>=0;i--){
      A.sh[i].t+=rdt;
      if(A.sh[i].t>4.2f){
        memmove(A.sh+i,A.sh+i+1,sizeof(A.sh[0])*(A.nsh-i-1));
        A.nsh--; continue;
      }
      A.sh[i].v[1]-=12.f*rdt;
      for(int k=0;k<3;k++){
        A.sh[i].p[k]+=A.sh[i].v[k]*rdt;
        A.sh[i].r[k]+=A.sh[i].w[k]*rdt;
      }
      if(A.sh[i].p[1]<floorY&&A.sh[i].v[1]<0){
        A.sh[i].p[1]=floorY; A.sh[i].v[1]*=-0.34f;
        A.sh[i].v[0]*=0.62f; A.sh[i].v[2]*=0.62f;
        for(int k=0;k<3;k++) A.sh[i].w[k]*=0.5f;
      }
    }
  }
  C->shake=pendShake; pendShake=0;
}

/* the trigger.  Ammo bookkeeping here, the gun's own reaction in fireVMW. */
static void vmwTryFire(VmwCtx *C){
  const Wep *w=&WEP[curW];
  if(!OWNED[curW]) return;
  if(fireT>0||reloadT>0||VM.draw<0.6f) return;
  if(A.over[curW]) return;                 /* the trigger just clunks */
  if(w->spinUp&&A.spinV<26.f) return;      /* barrels still spinning up */
  if(w->mag>0){
    if(MAGS[curW]<=0){
      auMech(MCH_DRY,mechW()); fireT=0.22f;
      if(AMMO[curW]>0&&A.dryT<0.05f) A.dryT=0.05f;
      return;
    }
    MAGS[curW]--;
    if(MAGS[curW]<=0&&AMMO[curW]>0) A.dryT=w->reldly>0?w->reldly:0.15f;
  } else {
    if(AMMO[curW]<=0){ auMech(MCH_DRY,mechW()); fireT=0.22f; return; }
    AMMO[curW]--;
  }
  fireT=w->rate;
  auGun(w->sfx);
  fireVMW(C,w,&C->pitch,&C->yaw);
}

/* AND THE GUN COMING UP MADE NO SOUND, which is the one action the
   player takes that changes everything about the next ten seconds.  Only
   on a real change of slot: pressing 2 while holding 2 is not a draw. */
void vmwSelect(int slot){
  if(slot<1||slot>3||!OWNED[slot-1]) return;
  if(wantW==slot-1) return;
  wantW=slot-1;
  { static const float MW[3]={1.00f,1.30f,1.05f};
    auMech(MCH_DRAW,MW[slot-1]); }
}
void vmwGive(int slot){
  if(slot<1||slot>3) return;
  /* A GUN PICKED UP TWICE DOES NOT GO BACKWARDS.  The rack is one-shot,
     but a checkpoint restore can put the pickup back, and SETTING rather
     than keeping the larger turned a full belt into a fresh one. */
  int had = OWNED[slot-1] ? AMMO[slot-1] : 0;
  OWNED[slot-1]=1;
  AMMO[slot-1]=WEP[slot-1].giveN;
  if(had>AMMO[slot-1]) AMMO[slot-1]=had;
  MAGS[slot-1]=WEP[slot-1].mag;
  wantW=slot-1;
}
int  vmwOwns(int slot){ return (slot>=1&&slot<=3)&&OWNED[slot-1]; }
int  vmwArmed(void){ return OWNED[0]||OWNED[1]||OWNED[2]; }
void vmwAddAmmo(int slot,int n){
  if(slot<1||slot>3) return;
  int m=WEP[slot-1].reserve;
  AMMO[slot-1]+=n;
  if(AMMO[slot-1]>m) AMMO[slot-1]=m;
}
void vmwDisarmAll(void){
  for(int i=0;i<3;i++){ OWNED[i]=0; AMMO[i]=0; MAGS[i]=0; }
}
/* the checkpoint takes a copy of everything the guns hold */
void vmwSnapshot(int *o){
  for(int i=0;i<3;i++){ o[i]=OWNED[i]; o[3+i]=AMMO[i]; o[6+i]=MAGS[i]; }
  o[9]=curW;
}
void vmwRestore(const int *o){
  for(int i=0;i<3;i++){ OWNED[i]=o[i]; AMMO[i]=o[3+i]; MAGS[i]=o[6+i]; }
  if(OWNED[o[9]]) wantW=curW=o[9];
}
/* -gun N: no draw animation, the gun is simply already up.  The
   screenshot path has three frames to work with and the swap takes
   thirty. */
void vmwForce(int slot){
  if(slot<1||slot>3) return;
  /* AND LOADED.  Owning a gun is not the same as having anything in it,
     and vmwTryFire answers an empty magazine with a dry click rather than
     a shot -- so -fire produced a click and no flash at all. */
  OWNED[slot-1]=1;
  AMMO[slot-1]=WEP[slot-1].reserve/2;
  MAGS[slot-1]=WEP[slot-1].mag;
  /* AND ALREADY TURNING, if it is the one that has to turn.  vmwTryFire
     refuses a rotary under 26 rad/s and the barrels take about a second
     to get there off a standing start; -fire has three frames.  So
     `-gun 3 -fire` photographed a cannon that had not fired -- the frame
     came back identical to the same frame without -fire. */
  forceSpin = WEP[slot-1].spinUp;
  A.spinV=36.f;
  curW=wantW=slot-1; VM.draw=1;
}
void vmwTrigger(int down){
  holdFire=down;
  if(down) trigPend=1;
}
void vmwReload(void){
  const Wep *w=&WEP[curW];
  if(reloadT>0||w->mag<=0) return;
  if(MAGS[curW]>=w->mag||AMMO[curW]<=0) return;
  reloadT=w->reload;
  auMech(MCH_MAG_OUT,mechW());
}
void vmwLook(float dx,float dy){
  VM.tgtX=clv(VM.tgtX-dx*0.010f,-1.4f,1.4f);
  VM.tgtY=clv(VM.tgtY+dy*0.008f,-1.4f,1.4f);
}
const char *vmwName(void){ return WEP[curW].name; }
int   vmwMag(void){ return MAGS[curW]; }
int   vmwReserve(void){ return AMMO[curW]; }
int   vmwUsesMag(void){ return WEP[curW].mag>0; }
float vmwHeat(void){ return A.heat[curW]; }
int   vmwOver(void){ return A.over[curW]; }
int   vmwReloading(void){ return reloadT>0; }

/* --------------------------------------------------------------- draw */
static void bindCommon(const VmwCtx *C){
  glUseProgram(vmProg);
  for(int i=0;i<9;i++){
    glActiveTexture(GL_TEXTURE0+i);
    glBindTexture(GL_TEXTURE_2D,TEXW[i/3][i%3]);
    glUniform1i(UW.tex[i],i);
  }
  glUniform3fv(UW.uAmb,1,VM.lit);
  { int z=zoneAt(C->eye[0],C->eye[2]);
    if(z<0||z>=CUBE_COUNT) z=0;
    glUniform3fv(UW.uCube,6,CUBES+(size_t)z*18);
    /* the world's own probe and DFG table, borrowed at units 9 and 10 --
       the nine weapon textures already hold 0..8 */
    glActiveTexture(GL_TEXTURE0+9);
    glBindTexture(GL_TEXTURE_2D_ARRAY,renderProbeTex());
    glUniform1i(UW.uProbe,9);
    glActiveTexture(GL_TEXTURE0+10);
    glBindTexture(GL_TEXTURE_2D,renderDFGTex());
    glUniform1i(UW.uDFG,10);
    glUniform1f(UW.uPN,(float)PROBE_N);
    glUniform1f(UW.uPLev,(float)(PROBE_LEV-1));
    glUniform1f(UW.uPBase,(float)(z*PROBE_LEV));
    glActiveTexture(GL_TEXTURE0); }
  /* the sampled dominant light direction, world -> view */
  { const float *d=VM.ldir;
    glUniform3f(UW.uLDir,
      d[0]*C->camR[0]+d[1]*C->camR[1]+d[2]*C->camR[2],
      d[0]*C->camU[0]+d[1]*C->camU[1]+d[2]*C->camU[2],
      -(d[0]*C->camF[0]+d[1]*C->camF[1]+d[2]*C->camF[2])); }
  { float v2w[9]={ C->camR[0],C->camR[1],C->camR[2],
                   C->camU[0],C->camU[1],C->camU[2],
                  -C->camF[0],-C->camF[1],-C->camF[2] };
    glUniformMatrix3fv(UW.uV2W,1,GL_FALSE,v2w); }
  glUniform1f(UW.uGain,A.gain);
  glUniform1f(UW.uTime,C->elapsed);
  glUniform1f(UW.uFlashI,C->flashlight?0.9f:0.0f);
  { float c[3];
    lin(TP.accent,c); glUniform3fv(UW.uAccent,1,c);
    lin(TP.paint,c);  glUniform3fv(UW.uPaint,1,c); }
}
static void drawMesh(const VMesh *m,const M4 *M){
  if(!m->count) return;
  float nm[9]; m3of(M,nm);
  glUniformMatrix4fv(UW.uModel,1,GL_FALSE,M->m);
  glUniformMatrix3fv(UW.uNM,1,GL_FALSE,nm);
  glBindVertexArray(m->vao);
  glDrawArrays(GL_TRIANGLES,0,m->count);
}

void vmwDraw(const VmwCtx *C){
  const Wep *w=&WEP[curW];
  const Gun *G=&WMESH[w->wmesh];
  if(VM.draw<=0.02f) return;
  if(!OWNED[curW]) return;                 /* nothing in your hands yet */

  /* the gun matrix in view space: rest offset, bob, sway, recoil, jolt,
     the reload dip, and the idle drift that never quite repeats */
  float rw = A.over[curW] ? 0.10f+0.06f*sinf(C->elapsed*30.0f) : 0.f;
  float bobx= sinf(C->stepPhase*2.6f)*0.024f*VM.bob;
  float boby=-fabsf(cosf(C->stepPhase*2.6f))*0.021f*VM.bob;
  float t=C->elapsed, idle=A.idle;
  float iX =(sinf(t*0.91f)*0.0055f+sinf(t*1.73f+1.2f)*0.0021f)*idle;
  float iY =(sinf(t*1.27f+0.7f)*0.0042f+sinf(t*2.61f)*0.0015f)*idle;
  float iZ = sinf(t*0.63f+2.1f)*0.0030f*idle;
  float iRX=(sinf(t*1.09f+1.4f)*0.0105f+sinf(t*2.33f)*0.0035f)*idle;
  float iRY= sinf(t*0.77f)*0.0130f*idle;
  float iRZ= sinf(t*0.85f+0.5f)*0.0150f*idle;
  M4 gunM=mMul2(mMul2(mMul2(
    mT(w->vm[0]+bobx+VM.swayX*0.05f-0.03f*rw-0.012f*A.rlp+iX,
       w->vm[1]+boby+VM.swayY*0.05f-0.085f*rw+A.recoil*0.012f-0.040f*A.rlp
              +A.jolt*0.016f+iY-(1.f-VM.draw)*0.66f,
       w->vm[2]+A.recoil*w->vpush+0.04f*rw+0.026f*A.rlp+iZ),
    mRY(-0.055f+0.10f*A.rlp+VM.swayX*0.15f+iRY)),
    mRX(0.02f+A.recoil*0.055f-0.50f*rw-0.20f*A.rlp+A.jolt*0.13f+iRX)),
    mRZ(0.35f*rw+0.20f*A.rlp+iRZ));
  A.gunM=gunM;
  A.gunW=mMul2(camMat(C),gunM);
  M4 spinM = w->kind==0 ? mMul2(gunM,mRZ(A.spin))
           : w->kind==2 ? mMul2(gunM,mT(0,0,A.slideZ))
           : mMul2(gunM,mMul2(mMul2(mT(0,-0.045f,-0.015f),mRX(-A.brk*0.78f)),
                              mT(0,0.045f,0.015f)));
  M4 magM = mMul2(gunM,mT(0,-A.magDrop,0));

  /* A GUEST DRAW INHERITS THE HOST'S GL STATE, so this pass states its
     own: depth on and writing, no blend, and no culling -- the meshes are
     lofted from both sides and a chamfer ring can wind either way. */
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);

  bindCommon(C);
  M4 proj=mPersp(1.19f,C->aspect,0.012f,30.f);
  glUniformMatrix4fv(UW.uProj,1,GL_FALSE,proj.m);
  float mx = w->twin!=0 ? (A.shotSide?w->twin:-w->twin) : 0.f;
  float mq[3], src[3]={mx,0,w->muz};
  xfp(&gunM,src,mq);
  glUniform3fv(UW.uMuzP,1,mq);
  { float mi=A.muzzle*(0.7f+0.3f*rnd01());
    glUniform3f(UW.uMuzC,26.f*mi*w->mfl*A.gain,
                         17.f*mi*w->mfl*A.gain,
                          7.f*mi*w->mfl*A.gain); }
  glUniform1i(UW.uAmmo,(A.over[curW]||reloadT>0)?-1:
              (w->mag>0?MAGS[curW]:(AMMO[curW]<999?AMMO[curW]:999)));
  glUniform1f(UW.uHeat,A.heat[curW]);
  glUniform1f(UW.uMuzI,0.0f);

  /* THE GUN OWNS ITS OWN DEPTH RANGE.  Clearing depth is what stops a
     bulkhead three centimetres from the muzzle cutting the barrel off. */
  glClear(GL_DEPTH_BUFFER_BIT);
  drawMesh(&G->stat,&gunM);
  drawMesh(&G->spin,&spinM);
  drawMesh(&G->magm,&magM);
  drawMesh(&G->hands,&gunM);
  drawMesh(&G->hmov,&spinM);

  /* The flash keeps the depth test, so the barrel it leaves occludes it --
     drawn on top it read as a decal pasted over the gun. */
  if(A.muzzle>0.02f){
    float f=A.muzzle*A.muzzle;
    float S=w->flash*A.flash;
    float I=f*7.0f*A.gain*3.0f;
    float bd[3], fwd[3]={0,0,-1};
    rot3(&gunM,fwd,bd);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE);
    glDepthMask(GL_FALSE);
    #define CARD(off,sz,roll) do{ \
      M4 m_=mMul2(mMul2(mT(mq[0]+bd[0]*(off),mq[1]+bd[1]*(off), \
                           mq[2]+bd[2]*(off)),mRZ(roll)),mS(sz,sz,sz)); \
      drawMesh(&mCard,&m_); }while(0)
    /* two seeds: a wide ragged burst and a tight heart inside it.  Both
       face the camera, so there is no silhouette to recognise and no edge
       to catch as the gun swings. */
    glUniform1f(UW.uMuzG,0); glUniform1f(UW.uMuzI,I);
    glUniform1f(UW.uMuzS,A.flashSeed);
    CARD(S*0.55f,S*(1.15f+0.50f*f),A.flashRoll);
    glUniform1f(UW.uMuzS,A.flashSeed+11.7f);
    CARD(S*0.28f,S*(0.52f+0.22f*f),A.flashRoll+2.2f);
    /* the bloom it throws, which does cross the gun */
    glUniform1f(UW.uMuzG,1); glUniform1f(UW.uMuzI,I*0.30f);
    glDisable(GL_DEPTH_TEST);
    CARD(S*0.40f,S*(1.9f+0.7f*f),0);
    glEnable(GL_DEPTH_TEST);
    #undef CARD
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  }
  glEnable(GL_CULL_FACE);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
}

/* World-space casings, drawn in the world pass -- before the viewmodel's
   own depth clear -- by composing view * world so the shader can stay in
   view space.  Brass is solid, so it takes the depth mask back up for its
   own draw and hands the state back the way it found it. */
void vmwDrawShells(const FrameState *F,const float *proj,const float *view){
  if(!A.nsh) return;
  VmwCtx C;
  memset(&C,0,sizeof C);
  C.camR[0]= view[0]; C.camR[1]= view[4]; C.camR[2]= view[8];
  C.camU[0]= view[1]; C.camU[1]= view[5]; C.camU[2]= view[9];
  C.camF[0]=-view[2]; C.camF[1]=-view[6]; C.camF[2]=-view[10];
  for(int k=0;k<3;k++) C.eye[k]=F->camPos[k];
  C.elapsed=F->time; C.flashlight=F->flash;

  glDisable(GL_BLEND); glDepthMask(GL_TRUE);
  bindCommon(&C);
  glUniformMatrix4fv(UW.uProj,1,GL_FALSE,proj);
  glUniform3f(UW.uMuzP,0,0,-9);
  glUniform3f(UW.uMuzC,0,0,0);
  glUniform1i(UW.uAmmo,0);
  glUniform1f(UW.uHeat,0);
  glUniform1f(UW.uMuzI,0);
  glDisable(GL_CULL_FACE);
  M4 V; memcpy(V.m,view,sizeof V.m);
  for(int i=0;i<A.nsh;i++){
    M4 M=mMul2(V,mMul2(mMul2(mMul2(
        mT(A.sh[i].p[0],A.sh[i].p[1],A.sh[i].p[2]),
        mRY(A.sh[i].r[1])),mRX(A.sh[i].r[0])),mRZ(A.sh[i].r[2])));
    drawMesh(A.sh[i].s?&mShot:&mShell,&M);
  }
  glEnable(GL_CULL_FACE);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
}

/* THE PICKUP IS THE GUN, NOT A BOX WITH A LENS ON IT.  Every item in the
   ship draws as a 26 cm crate with a coloured bar driven through it, which
   is right for a health kit and an ammunition cache -- one silhouette, one
   symbol, legible at twenty metres -- and exactly wrong for the three
   weapons, because the thing the player is being sent across a level to
   find IS the gun's own outline.  The meshes are built and textured
   already; they had only ever been drawn in view space.

   This is vmwDrawShells' pass with a different mesh: the frame's own
   projection into uProj, a WORLD matrix premultiplied by the view into
   uModel, and the same nine textures bound.  No hands: the hands belong
   to the man holding it, and nobody is holding this one. */
void vmwDrawPickup(const FrameState *F,const float *proj,const float *view,
                   int slot,float x,float y,float z,float spin,float scale){
  if(slot<1||slot>3) return;
  const Gun *G=&WMESH[WEP[slot-1].wmesh];
  VmwCtx C;
  memset(&C,0,sizeof C);
  C.camR[0]= view[0]; C.camR[1]= view[4]; C.camR[2]= view[8];
  C.camU[0]= view[1]; C.camU[1]= view[5]; C.camU[2]= view[9];
  C.camF[0]=-view[2]; C.camF[1]=-view[6]; C.camF[2]=-view[10];
  for(int k=0;k<3;k++) C.eye[k]=F->camPos[k];
  C.elapsed=F->time; C.flashlight=F->flash;

  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  bindCommon(&C);
  glUniformMatrix4fv(UW.uProj,1,GL_FALSE,proj);
  glUniform3f(UW.uMuzP,0,0,-9);
  glUniform3f(UW.uMuzC,0,0,0);
  glUniform1i(UW.uAmmo,0);
  glUniform1f(UW.uHeat,0);
  glUniform1f(UW.uMuzI,0);
  M4 V; memcpy(V.m,view,sizeof V.m);
  /* the mesh lies down its own -Z with +Y up, which is the viewmodel's
     frame; nosed down 18 degrees and turned about Y it reads as a weapon
     hanging on a plinth rather than a prop lying on the deck */
  M4 M=mMul2(V,mMul2(mMul2(mMul2(mT(x,y,z),mRY(spin)),mRX(-0.32f)),
                     mS(scale,scale,scale)));
  drawMesh(&G->stat,&M);
  drawMesh(&G->spin,&M);
  drawMesh(&G->magm,&M);
  glEnable(GL_CULL_FACE);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
}

/* --------------------------------------------------------------- boot */
int vmwInit(void){
  double t0=platTime();
  static const char *TEXN[9]={"tA0","tA1","tA2","tB0","tB1","tB2",
                              "tC0","tC1","tC2"};
  genProg=glMakeProgram(GEN_VS,GEN_FS,"weapon textures");
  if(!genProg) return 0;
  vmProg =glMakeProgram(VM_VS,VM_FS,"viewmodel");
  if(!vmProg) return 0;
#define U(n) UW.n=glGetUniformLocation(vmProg,#n)
  U(uProj);U(uModel);U(uNM);U(uAmb);U(uLDir);U(uV2W);U(uCube);U(uMuzP);
  U(uMuzC);U(uFlashI);U(uMuzI);U(uMuzS);U(uMuzG);U(uGain);U(uTime);U(uAmmo);
  U(uHeat);U(uAccent);U(uPaint);
  U(uProbe);U(uDFG);U(uPN);U(uPLev);U(uPBase);
#undef U
  for(int i=0;i<9;i++) UW.tex[i]=glGetUniformLocation(vmProg,TEXN[i]);

  for(int m=0;m<3;m++) for(int i=0;i<3;i++) TEXW[m][i]=makeTex();
  glGenFramebuffers(1,&genFBO);
  glGenVertexArrays(1,&genVAO);
  generateTextures(1337);

  DUMP=platArg("-vmw");
  WMESH[0]=uploadGun("pistol", buildPistol());
  WMESH[1]=uploadGun("rotary", buildRotary(7));
  WMESH[2]=uploadGun("shotgun",buildShotgun());
  { Raw r;
    r=buildShell();     dumpRaw("shell",r);     mShell=upload(r);
    r=buildShotshell(); dumpRaw("shotshell",r); mShot =upload(r);
    r=buildFlashCard(); dumpRaw("flashcard",r); mCard =upload(r); }

  memset(&A,0,sizeof A);
  A.gain=0.34f; A.flash=1.0f;
  A.gunM=mI(); A.gunW=mI();
  memset(&VM,0,sizeof VM);
  VM.draw=1; VM.ldir[0]=0.2f; VM.ldir[1]=0.8f; VM.ldir[2]=0.3f;
  for(int i=0;i<3;i++){ AMMO[i]=WEP[i].reserve; MAGS[i]=WEP[i].mag; }
  curW=wantW=0;

  int tv=0;
  for(int i=0;i<3;i++)
    tv+=WMESH[i].stat.count+WMESH[i].spin.count+WMESH[i].hands.count
       +WMESH[i].hmov.count+WMESH[i].magm.count;
  platLog("vmw    3 guns  %d triangles  9 textures at %d^2  %.0f ms",
          tv/3, RESW, (platTime()-t0)*1000.0);
  return 1;
}
