/* ============================================================
   LUMI'S SHAPE.  See lumimesh.h.

   This is props/lumi_v02/lumi_geometry.js ported statement for
   statement.  The JS is 601 lines of array pushes and Math
   calls with no dependency on three.js or on a browser, which
   is why a C port changes three things and nothing else:
   dynamic arrays become fixed buffers, textures become shader
   work, and materials become uniforms.  The numbers do not
   change.  The shape is frozen: this is a port, not a redesign.

   Two departures:

     -- NO UVs.  The entity shader has no texture unit; it
        derives its plating from the vertex's position in the
        part's own metres (vObjW).  A uv per vertex would be
        77 KB of buffer that nothing reads.
     -- THE DECAL TEXT IS GEOMETRY.  The canvas font has to
        become bitmap glyphs.  "LUMI" is cut out of hud.c's
        own 5x7 stencil -- the one the ship's placards are cut
        with -- and laid on the front decal's sphere as one more
        part.  It is NOT counted in the generator's checksum, because it
        is not one of the generator's 35.

   Everything here works in double, the way the JS does, so that
   the degenerate-ring test in lathe() -- which decides whether a
   duplicated hard-edge ring closed on itself -- lands on the same
   side in both languages.  The JS compares with ===; this
   compares against 1e-9, which is the same answer for a profile
   whose smallest real step is 6 mm, and does not have to be
   argued about under -ffast-math.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include "prim.h"
#include "lumimesh.h"
#include "hud.h"
#include <stdlib.h>
#include <string.h>

/* ---- PARAMS.  lumi_geometry.js's P, name for name. ------------------ */
#define P_R           0.222
#define P_YS          0.97
#define P_BAND_INSET  0.0085
#define P_A1          0.44
#define P_A2          0.55
#define P_A3          2.02
#define P_A4          2.13
#define P_A5          2.34
#define P_SEG         64

#define V_TILT       -0.26
#define V_A           0.134
#define V_B           0.074
#define V_EXPO        3.2
#define V_RBEZEL      0.2268
#define V_RRIM        0.2242
#define V_RSCREEN     0.2236

#define D_RSURF       0.2230
#define D_FRONT_TILT  0.258
#define D_FRONT_A     0.080
#define D_FRONT_B     0.036
#define D_BACK_TILT  -0.08
#define D_BACK_A      0.082
#define D_BACK_B      0.041
#define D_SIDE_YAW    0.72
#define D_SIDE_A      0.040
#define D_SIDE_B      0.040

#define POD_X         0.158
#define POD_Y         0.010
#define POD_Z         0.004
#define POD_OUTW      0.116
#define POD_OUTH      0.150
#define POD_OUTR      0.055
#define POD_INW       0.078
#define POD_INH       0.110
#define POD_INR       0.038
#define POD_DEPTH     0.056
#define POD_BEVEL     0.005
#define POD_CAPR0     0.044
#define POD_CAPR1     0.046
#define POD_CAPLEN    0.032
#define POD_CAPZ      0.042
#define POD_RINGR     0.046
#define POD_RINGT     0.005
#define POD_RINGZ     0.056
#define POD_HOLER     0.0095
#define POD_HOLERING  0.021
#define POD_HOLEZ     0.060
#define POD_HOLEPH    0.78

#define AN_R0         0.0035
#define AN_R1         0.0055
#define AN_LEN        0.092
#define AN_TILT       0.16
#define AN_TIPR       0.006

#define EM_RINGR      0.104
#define EM_RINGT      0.008
#define EM_RINGDY     0.004
#define EM_DISCR      0.132
#define EM_DISCDY    -0.002
#define BEAM_H        0.085

#define PI2           6.283185307179586
#define PIH           1.5707963267948966

float LM_EMIT_Y;
int   LUMI_PRIM[LMP_N];

/* ============================================================
   the mesh accumulator -- Mesh() in the JS
   ============================================================ */
typedef struct { double *p,*n; int *ix; int nv,cv,ni,ci; } LM;

static void lmRoom(LM *m,int addV,int addI){
  if(m->nv+addV>m->cv){
    while(m->cv<m->nv+addV) m->cv = m->cv? m->cv*2 : 256;
    m->p=(double*)realloc(m->p,sizeof(double)*(size_t)m->cv*3);
    m->n=(double*)realloc(m->n,sizeof(double)*(size_t)m->cv*3);
  }
  if(m->ni+addI>m->ci){
    while(m->ci<m->ni+addI) m->ci = m->ci? m->ci*2 : 512;
    m->ix=(int*)realloc(m->ix,sizeof(int)*(size_t)m->ci);
  }
}
static int lmVert(LM *m,double x,double y,double z,
                        double nx,double ny,double nz){
  lmRoom(m,1,0);
  m->p[m->nv*3]=x; m->p[m->nv*3+1]=y; m->p[m->nv*3+2]=z;
  m->n[m->nv*3]=nx; m->n[m->nv*3+1]=ny; m->n[m->nv*3+2]=nz;
  return m->nv++;
}
static void lmTri(LM *m,int a,int b,int c){
  lmRoom(m,0,3);
  m->ix[m->ni++]=a; m->ix[m->ni++]=b; m->ix[m->ni++]=c;
}
static void lmQuad(LM *m,int a,int b,int c,int d){ lmTri(m,a,b,c); lmTri(m,a,c,d); }
static void lmFree(LM *m){ free(m->p); free(m->n); free(m->ix); memset(m,0,sizeof *m); }

/* area-weighted vertex normals, exactly recomputeNormals() */
static void lmNorms(LM *m){
  for(int i=0;i<m->nv*3;i++) m->n[i]=0;
  for(int i=0;i+2<m->ni;i+=3){
    int a=m->ix[i]*3, b=m->ix[i+1]*3, c=m->ix[i+2]*3;
    double ux=m->p[b]-m->p[a], uy=m->p[b+1]-m->p[a+1], uz=m->p[b+2]-m->p[a+2];
    double vx=m->p[c]-m->p[a], vy=m->p[c+1]-m->p[a+1], vz=m->p[c+2]-m->p[a+2];
    double nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
    m->n[a]+=nx; m->n[a+1]+=ny; m->n[a+2]+=nz;
    m->n[b]+=nx; m->n[b+1]+=ny; m->n[b+2]+=nz;
    m->n[c]+=nx; m->n[c+1]+=ny; m->n[c+2]+=nz;
  }
  for(int i=0;i<m->nv;i++){
    int k=i*3;
    double l=sqrt(m->n[k]*m->n[k]+m->n[k+1]*m->n[k+1]+m->n[k+2]*m->n[k+2]);
    if(l>1e-9){ m->n[k]/=l; m->n[k+1]/=l; m->n[k+2]/=l; }
  }
}
/* one mesh poured into another, which is how the JS builds pod_holes and
   back_leds out of a handful of little ones */
static void lmMerge(LM *dst,LM *src){
  int base=dst->nv;
  lmRoom(dst,src->nv,src->ni);
  memcpy(dst->p+(size_t)dst->nv*3,src->p,sizeof(double)*(size_t)src->nv*3);
  memcpy(dst->n+(size_t)dst->nv*3,src->n,sizeof(double)*(size_t)src->nv*3);
  dst->nv+=src->nv;
  for(int i=0;i<src->ni;i++) dst->ix[dst->ni++]=src->ix[i]+base;
  lmFree(src);
}

/* ============================================================
   the transforms, baked into the points -- rotX/rotY/mul3/bake
   ============================================================ */
static void rot3X(double t,double *M){ double c=cos(t),s=sin(t);
  M[0]=1;M[1]=0;M[2]=0; M[3]=0;M[4]=c;M[5]=-s; M[6]=0;M[7]=s;M[8]=c; }
static void rot3Y(double t,double *M){ double c=cos(t),s=sin(t);
  M[0]=c;M[1]=0;M[2]=s; M[3]=0;M[4]=1;M[5]=0; M[6]=-s;M[7]=0;M[8]=c; }
static void mul3(const double *A,const double *B,double *C){
  for(int r=0;r<3;r++) for(int c=0;c<3;c++)
    C[r*3+c]=A[r*3]*B[c]+A[r*3+1]*B[3+c]+A[r*3+2]*B[6+c];
}
/* the basis whose +Z is dir -- what stands a little box off the hull along
   its own radius, so an LED sits ON the shell rather than through it */
static void basisTowards(const double *dir,double *M){
  double l=sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
  double z[3]={dir[0]/l,dir[1]/l,dir[2]/l};
  double up[3]; up[0]=0; up[1]=1; up[2]=0;
  if(fabs(z[1])>0.999){ up[0]=0; up[1]=0; up[2]=1; }
  double x[3]={up[1]*z[2]-up[2]*z[1], up[2]*z[0]-up[0]*z[2], up[0]*z[1]-up[1]*z[0]};
  double lx=sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]);
  x[0]/=lx; x[1]/=lx; x[2]/=lx;
  double y[3]={z[1]*x[2]-z[2]*x[1], z[2]*x[0]-z[0]*x[2], z[0]*x[1]-z[1]*x[0]};
  M[0]=x[0];M[1]=y[0];M[2]=z[0];
  M[3]=x[1];M[4]=y[1];M[5]=z[1];
  M[6]=x[2];M[7]=y[2];M[8]=z[2];
}
static void lmBake(LM *m,const double *M,const double *T){
  for(int i=0;i<m->nv;i++){
    int k=i*3;
    if(M){
      double x=m->p[k],y=m->p[k+1],z=m->p[k+2];
      m->p[k]  =M[0]*x+M[1]*y+M[2]*z;
      m->p[k+1]=M[3]*x+M[4]*y+M[5]*z;
      m->p[k+2]=M[6]*x+M[7]*y+M[8]*z;
      double a=m->n[k],b=m->n[k+1],c=m->n[k+2];
      m->n[k]  =M[0]*a+M[1]*b+M[2]*c;
      m->n[k+1]=M[3]*a+M[4]*b+M[5]*c;
      m->n[k+2]=M[6]*a+M[7]*b+M[8]*c;
    }
    if(T){ m->p[k]+=T[0]; m->p[k+1]+=T[1]; m->p[k+2]+=T[2]; }
  }
}

/* ============================================================
   the primitives
   ============================================================ */
static void spherePt(double a,double inset,double *o){
  o[0]=P_R*sin(a)-inset; o[1]=P_R*P_YS*cos(a);
}

/* A LATHE WHOSE SEAMS ARE VERTEX DUPLICATES.  hard=1 on an interior ring
   copies it, and the copy's normals are computed from the OTHER side --
   which is the whole of how seven zones on one ovoid read as seven
   machined zones and not as a smooth egg: take the duplication out and
   you have a boiled egg. */
static void lathe(const double *prof,int np,int seg,LM *m){
  double R[64*2]; int nr=0;
  for(int i=0;i<np;i++){
    R[nr*2]=prof[i*3]; R[nr*2+1]=prof[i*3+1]; nr++;
    if(prof[i*3+2]!=0.0&&i>0&&i<np-1){ R[nr*2]=prof[i*3]; R[nr*2+1]=prof[i*3+1]; nr++; }
  }
  memset(m,0,sizeof *m);
  for(int i=0;i<nr;i++){
    double r=R[i*2], y=R[i*2+1];
    for(int j=0;j<=seg;j++){
      double t=PI2*((double)j/seg-0.5);
      lmVert(m,r*sin(t),y,r*cos(t),0,0,0);
    }
  }
  int row=seg+1;
  for(int i=0;i<nr-1;i++){
    if(fabs(R[i*2]-R[(i+1)*2])<1e-9&&fabs(R[i*2+1]-R[(i+1)*2+1])<1e-9) continue;
    for(int j=0;j<seg;j++){
      int a=i*row+j;
      lmQuad(m,a,a+row,a+row+1,a+1);
    }
  }
  lmNorms(m);
}

/* NOT m.h's sgn().  The JS's returns +1 at zero and m.h's returns 0, and
   at zero this is the superellipse's own axis point -- 0 there collapses
   four vertices of every patch onto its centre. */
static double lsgn(double x){ return x<0?-1.0:1.0; }

/* A SUPERELLIPSE PROJECTED ONTO A SPHERE.  The visor is not a plane.  Made
   flat it either buries its middle in the hull or lifts its corners off
   it.  f is the radial parameter: 0 is
   the centre, 1 the outline, and >1 walks outside it for a bezel. */
static void spherePatch(double Rp,double A,double B,double expo,
                        int aSeg,int rSeg,double f0,double f1,LM *m){
  memset(m,0,sizeof *m);
  for(int j=0;j<=rSeg;j++){
    double f=f0+(f1-f0)*((double)j/rSeg);
    for(int i=0;i<=aSeg;i++){
      double th=PI2*i/aSeg, c=cos(th), s=sin(th);
      double px=A*lsgn(c)*pow(fabs(c),2.0/expo)*f;
      double py=B*lsgn(s)*pow(fabs(s),2.0/expo)*f;
      double l=sqrt(px*px+py*py+Rp*Rp), k=Rp/l;
      lmVert(m,px*k,py*k,Rp*k,0,0,0);
    }
  }
  int row=aSeg+1;
  for(int j=0;j<rSeg;j++) for(int i=0;i<aSeg;i++){
    int a=j*row+i;
    lmQuad(m,a,a+row,a+row+1,a+1);
  }
  lmNorms(m);
}

static void mkBox(double w,double h,double d,LM *m){
  memset(m,0,sizeof *m);
  double x=w/2,y=h/2,z=d/2;
  static const int F[6][4][3]={
    {{ 1,-1,-1},{ 1,-1, 1},{ 1, 1, 1},{ 1, 1,-1}},
    {{-1,-1, 1},{-1,-1,-1},{-1, 1,-1},{-1, 1, 1}},
    {{-1, 1,-1},{ 1, 1,-1},{ 1, 1, 1},{-1, 1, 1}},
    {{-1,-1, 1},{ 1,-1, 1},{ 1,-1,-1},{-1,-1,-1}},
    {{-1,-1, 1},{-1, 1, 1},{ 1, 1, 1},{ 1,-1, 1}},
    {{ 1,-1,-1},{ 1, 1,-1},{-1, 1,-1},{-1,-1,-1}}};
  static const double N[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  for(int f=0;f<6;f++){
    int base=m->nv;
    for(int k=0;k<4;k++)
      lmVert(m,F[f][k][0]*x,F[f][k][1]*y,F[f][k][2]*z,N[f][0],N[f][1],N[f][2]);
    lmQuad(m,base,base+1,base+2,base+3);
  }
}

static void mkCyl(double rTop,double rBot,double h,int seg,int open,LM *m){
  memset(m,0,sizeof *m);
  double slope=(rBot-rTop)/h;
  double nl=sqrt(1.0+slope*slope);
  for(int ring=0;ring<2;ring++){
    double r=ring?rBot:rTop, y=ring?-h/2:h/2;
    for(int i=0;i<=seg;i++){
      double t=PI2*i/seg, cx=cos(t), cz=sin(t);
      lmVert(m,r*cx,y,r*cz,cx/nl,slope/nl,cz/nl);
    }
  }
  int row=seg+1;
  for(int i=0;i<seg;i++) lmQuad(m,i,i+row,i+row+1,i+1);
  if(!open){
    double capR[2]={rTop,rBot}, capY[2]={h/2,-h/2}, capN[2]={1,-1};
    for(int e=0;e<2;e++){
      if(capR[e]<=1e-6) continue;
      int base=m->nv;
      int c=lmVert(m,0,capY[e],0,0,capN[e],0);
      for(int i=0;i<=seg;i++){
        double t=PI2*i/seg;
        lmVert(m,capR[e]*cos(t),capY[e],capR[e]*sin(t),0,capN[e],0);
      }
      for(int i=0;i<seg;i++){
        if(capN[e]>0) lmTri(m,c,base+1+i,base+2+i);
        else          lmTri(m,c,base+2+i,base+1+i);
      }
    }
  }
}

static void mkCircle(double r,int seg,LM *m){
  memset(m,0,sizeof *m);
  int c=lmVert(m,0,0,0,0,0,1);
  for(int i=0;i<=seg;i++){
    double t=PI2*i/seg;
    lmVert(m,r*cos(t),r*sin(t),0,0,0,1);
  }
  for(int i=0;i<seg;i++) lmTri(m,c,1+i,2+i);
}

static void mkTorus(double R0,double tube,int radSeg,int tubSeg,LM *m){
  memset(m,0,sizeof *m);
  for(int i=0;i<=radSeg;i++){
    double u=PI2*i/radSeg, cu=cos(u), su=sin(u);
    for(int j=0;j<=tubSeg;j++){
      double v=PI2*j/tubSeg, cv=cos(v), sv=sin(v);
      double r=R0+tube*cv;
      lmVert(m,r*cu,r*su,tube*sv,cv*cu,cv*su,sv);
    }
  }
  int row=tubSeg+1;
  for(int i=0;i<radSeg;i++) for(int j=0;j<tubSeg;j++){
    int a=i*row+j;
    lmQuad(m,a,a+row,a+row+1,a+1);
  }
}

static void mkSphere(double r,int segU,int segV,LM *m){
  memset(m,0,sizeof *m);
  for(int j=0;j<=segV;j++){
    double a=3.141592653589793*j/segV, sa=sin(a), ca=cos(a);
    for(int i=0;i<=segU;i++){
      double t=PI2*i/segU;
      double nx=sa*sin(t), ny=ca, nz=sa*cos(t);
      lmVert(m,r*nx,r*ny,r*nz,nx,ny,nz);
    }
  }
  int row=segU+1;
  for(int j=0;j<segV;j++) for(int i=0;i<segU;i++){
    int a=j*row+i;
    lmQuad(m,a,a+row,a+row+1,a+1);
  }
}

/* THE POD HOOP IS A SWEEP, NOT A RING.  A bevelled section carried round a
   rounded rectangle -- which is why the hole through it reads as machined
   rather than drilled.  seg is quartered, so 48 is twelve to a corner. */
static int roundRectPath(double w,double h,double r,int seg,double *out){
  double x=w/2-r, y=h/2-r;
  double cx[4]={ x,-x,-x, x}, cy[4]={ y, y,-y,-y};
  double a0[4]={0, PIH, 3.141592653589793, 3.141592653589793*1.5};
  int quarter=(int)(seg/4.0+0.5); if(quarter<2) quarter=2;
  int n=0;
  for(int c=0;c<4;c++) for(int i=0;i<quarter;i++){
    double a=a0[c]+PIH*i/quarter;
    out[n*2]=cx[c]+r*cos(a); out[n*2+1]=cy[c]+r*sin(a); n++;
  }
  return n;
}
static void sweepClosed(const double *path,int n,const double *sec,int s,LM *m){
  memset(m,0,sizeof *m);
  for(int i=0;i<n;i++){
    const double *p=path+i*2;
    const double *pn=path+((i+1)%n)*2;
    const double *pp=path+((i-1+n)%n)*2;
    double tx=pn[0]-pp[0], ty=pn[1]-pp[1];
    double tl=sqrt(tx*tx+ty*ty); if(tl==0) tl=1;
    double ox=ty/tl, oy=-tx/tl;
    if(p[0]*ox+p[1]*oy<0){ ox=-ox; oy=-oy; }
    for(int j=0;j<s;j++)
      lmVert(m,p[0]+ox*sec[j*2],p[1]+oy*sec[j*2],sec[j*2+1],0,0,0);
  }
  for(int i=0;i<n;i++) for(int j=0;j<s;j++){
    int a=i*s+j,            b=((i+1)%n)*s+j;
    int a2=i*s+(j+1)%s,     b2=((i+1)%n)*s+(j+1)%s;
    lmQuad(m,a,b,b2,a2);
  }
  lmNorms(m);
}

/* ============================================================
   the material groups.  A group is one primitive range, drawn as
   one instance, so every part in it has to want the same colour.
   ============================================================ */
typedef struct { float *v; int nv,cap; } Soup;
static Soup G[LMP_N];
static int ckParts,ckVerts,ckTris;

static void soupAdd(Soup *s,const LM *m){
  int add=m->ni;                            /* one soup vertex per index */
  if(s->nv+add>s->cap){
    while(s->cap<s->nv+add) s->cap = s->cap? s->cap*2 : 4096;
    s->v=(float*)realloc(s->v,sizeof(float)*(size_t)s->cap*6);
  }
  for(int i=0;i<m->ni;i++){
    int k=m->ix[i]*3;
    float *o=s->v+(size_t)(s->nv+i)*6;
    o[0]=(float)m->p[k]; o[1]=(float)m->p[k+1]; o[2]=(float)m->p[k+2];
    o[3]=(float)m->n[k]; o[4]=(float)m->n[k+1]; o[5]=(float)m->n[k+2];
  }
  s->nv+=add;
}
/* A COUNT IS NOT ENOUGH EVIDENCE.  vmwcheck.js exists because a mesh can
   have exactly the right number of vertices and still be the wrong mesh --
   one transposed matrix, one sign, and the count does not move.  So every
   part also reports its bounding box, and `playlumi.exe -lumimesh` prints
   the table that `node tools/lumicheck.js` prints from the generator. */
static int  dumpOn;
static void report(const char *name,const LM *m){
  double lo[3]={1e9,1e9,1e9}, hi[3]={-1e9,-1e9,-1e9};
  for(int i=0;i<m->nv;i++) for(int k=0;k<3;k++){
    double v=m->p[i*3+k];
    if(v<lo[k]) lo[k]=v;
    if(v>hi[k]) hi[k]=v;
  }
  platLog("%-16s %5d %5d  %8.4f %8.4f %8.4f  %8.4f %8.4f %8.4f",
          name,m->nv,m->ni/3,lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]);
}
/* one of the generator's 35: it goes into a group AND onto the checksum */
static void emit(int g,const char *name,LM *m){
  ckParts++; ckVerts+=m->nv; ckTris+=m->ni/3;
  if(dumpOn) report(name,m);
  soupAdd(&G[g],m);
  lmFree(m);
}
/* and one that is not: the lettering the port substitutes for canvas text */
static void emitExtra(int g,const char *name,LM *m){
  if(dumpOn) report(name,m);
  soupAdd(&G[g],m); lmFree(m);
}

/* ---- the seven body zones ------------------------------------------- */
static int arcProf(double a0,double a1,int n,double inset,double *p){
  for(int i=0;i<=n;i++){
    double a=a0+(a1-a0)*i/n, q[2];
    spherePt(a,inset,q);
    p[i*3]=q[0]; p[i*3+1]=q[1]; p[i*3+2]=0;
  }
  return n+1;
}
static void buildZones(void){
  double p[64*3]; int n; LM m; double q[2],qi[2],e[2];
  const double D=P_BAND_INSET;

  n=arcProf(0,P_A1,12,0,p);
  spherePt(P_A1,0,q); spherePt(P_A1,D,qi);
  p[n*3]=q[0];  p[n*3+1]=q[1]; p[n*3+2]=1; n++;
  p[n*3]=qi[0]; p[n*3+1]=q[1]; p[n*3+2]=1; n++;
  lathe(p,n,P_SEG,&m); emit(LMP_PAINT,"shell_cap",&m);

  n=arcProf(P_A1,P_A2,4,D,p);
  p[2]=1; p[(n-1)*3+2]=1;
  spherePt(P_A2,0,q);
  p[n*3]=q[0]; p[n*3+1]=q[1]; p[n*3+2]=1; n++;
  lathe(p,n,P_SEG,&m); emit(LMP_BAND,"band_upper",&m);

  n=arcProf(P_A2,P_A3,26,0,p);
  p[2]=1;
  spherePt(P_A3,0,q); spherePt(P_A3,D,qi);
  p[n*3]=q[0];  p[n*3+1]=q[1]; p[n*3+2]=1; n++;
  p[n*3]=qi[0]; p[n*3+1]=q[1]; p[n*3+2]=1; n++;
  lathe(p,n,P_SEG,&m); emit(LMP_PAINT,"shell_main",&m);

  n=arcProf(P_A3,P_A4,4,D,p);
  p[2]=1; p[(n-1)*3+2]=1;
  spherePt(P_A4,0,q);
  p[n*3]=q[0]; p[n*3+1]=q[1]; p[n*3+2]=1; n++;
  lathe(p,n,P_SEG,&m); emit(LMP_BAND,"band_lower",&m);

  n=arcProf(P_A4,P_A5,8,0,p);
  p[2]=1;
  lathe(p,n,P_SEG,&m); emit(LMP_PAINT,"shell_lower",&m);

  spherePt(P_A5,0,e);
  { double f[4*3]={ e[0],e[1],1,  e[0]+0.007,e[1]-0.008,1,
                    e[0]+0.003,e[1]-0.020,1,  0.136,e[1]-0.027,1 };
    lathe(f,4,P_SEG,&m); emit(LMP_STEEL,"flange",&m); }

  { double h[5*3]={ 0.136,e[1]-0.027,1,  0.131,e[1]-0.038,1,
                    0.118,e[1]-0.045,1,  0.118,e[1]-0.055,1,
                    0,    e[1]-0.057,0 };
    lathe(h,5,P_SEG,&m); emit(LMP_DARK,"emitter_housing",&m); }
}

/* ---- the name.  The canvas font becomes bitmap glyphs. --------------
   The bench prints LUMI and MAINTENANCE UNIT as ink ON the white shell --
   the decal patches carry an alpha texture and are otherwise the paint.
   There is no texture here, so the ink is the geometry: one tile per lit
   cell of hud.c's own 5x7 stencil, projected onto the decal's sphere.  The
   same stencil the ship's placards are cut with, which is the point.

   The small line is 1.1 mm a cell and will not resolve past a metre or
   two.  That is what small print does; it is the line being there that
   the shell reads, and she is a metre from the player's face in HUD. */
static void markText(LM *out,const char *txt,double cell,double cy){
  int n=0; for(const char *q=txt;*q;q++) n++;
  double w=(n*6-1)*cell;
  double x0=-w*0.5, y0=cy-3.5*cell;
  const double R=D_RSURF+0.0011;             /* proud of the decal patch */
  for(int g=0;g<n;g++){
    const unsigned char *bits=hudGlyph(txt[g]);
    for(int r=0;r<7;r++) for(int c=0;c<5;c++){
      if(!(bits[r]&(0x10>>c))) continue;
      double px=x0+(g*6+c)*cell+cell*0.5;
      double py=y0+(6-r)*cell+cell*0.5;
      double l=sqrt(px*px+py*py+R*R), k=R/l;
      double dir[3]={px*k,py*k,R*k}, B[9];
      basisTowards(dir,B);
      LM t; mkBox(cell*0.94,cell*0.94,0.0016,&t);
      lmBake(&t,B,dir);
      lmMerge(out,&t);
    }
  }
}
static void buildMark(void){
  LM out; memset(&out,0,sizeof out);
  markText(&out,"LUMI",0.00420, 0.0060);
  markText(&out,"MAINTENANCE UNIT",0.00115,-0.0160);
  double MR[9]; rot3X(D_FRONT_TILT,MR);
  lmBake(&out,MR,0);
  emitExtra(LMP_MARK,"mark_lumi",&out);
}

/* ============================================================
   buildLumi()
   ============================================================ */
static void buildLumi(void){
  LM m; double M[9],M2[9],T[3];

  buildZones();

  /* --- the visor.  Three shells at three radii, tilted as a group. --- */
  { double MV[9]; rot3X(V_TILT,MV);
    spherePatch(V_RBEZEL,V_A,V_B,V_EXPO,72,3,1.00,1.19,&m);
    lmBake(&m,MV,0); emit(LMP_PANEL,"visor_bezel",&m);
    spherePatch(V_RRIM,V_A,V_B,V_EXPO,72,2,0.99,1.02,&m);
    lmBake(&m,MV,0); emit(LMP_DARK,"visor_rim",&m);
    spherePatch(V_RSCREEN,V_A,V_B,V_EXPO,64,8,0.002,1.0,&m);
    lmBake(&m,MV,0); emit(LMP_SCREEN,"visor_screen",&m); }

  /* --- the decals.  Same patch machine, 0.5 mm off the shell. -------- */
  spherePatch(D_RSURF,D_FRONT_A,D_FRONT_B,2.0,40,4,0.002,1.0,&m);
  rot3X(D_FRONT_TILT,M); lmBake(&m,M,0); emit(LMP_DECAL,"decal_front",&m);
  spherePatch(D_RSURF,D_BACK_A,D_BACK_B,2.0,40,4,0.002,1.0,&m);
  rot3Y(3.141592653589793,M); rot3X(D_BACK_TILT,M2);
  { double MB[9]; mul3(M,M2,MB); lmBake(&m,MB,0); emit(LMP_DECAL,"decal_back",&m); }
  for(int s=-1;s<=1;s+=2){
    spherePatch(D_RSURF,D_SIDE_A,D_SIDE_B,2.0,24,4,0.002,1.0,&m);
    rot3Y(s*D_SIDE_YAW,M); lmBake(&m,M,0);
    emit(LMP_DECAL,s<0?"decal_side_l":"decal_side_r",&m);
  }

  /* --- the two LEDs on the front of the hull ------------------------ */
  { double d[3]={0,0.66,0.75}, dl=sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    T[0]=d[0]/dl*0.2245; T[1]=d[1]/dl*0.2245; T[2]=d[2]/dl*0.2245;
    basisTowards(d,M);
    mkBox(0.020,0.010,0.006,&m); lmBake(&m,M,T); emit(LMP_CYAN,"led_top",&m); }
  { double d[3]={0.44,-0.44,0.78}, dl=sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    T[0]=d[0]/dl*0.2238; T[1]=d[1]/dl*0.2238; T[2]=d[2]/dl*0.2238;
    basisTowards(d,M);
    mkCircle(0.010,16,&m); lmBake(&m,M,T); emit(LMP_CYAN,"led_dot",&m); }

  /* --- the side pods.  Five parts each, and they are the silhouette. - */
  for(int s=-1;s<=1;s+=2){
    double MP[9]; rot3Y(s*PIH,MP);
    double TP[3]={s*POD_X,POD_Y,POD_Z};
    double cw=(POD_OUTW+POD_INW)/2, ch=(POD_OUTH+POD_INH)/2;
    double cr=(POD_OUTR+POD_INR)/2;
    double half=(POD_OUTW-POD_INW)/4+(POD_OUTH-POD_INH)/4;
    double bv=POD_BEVEL, dz=POD_DEPTH;
    double sec[8*2]={ -half,bv,  -half,dz-bv,  -half+bv,dz,  half-bv,dz,
                       half,dz-bv,  half,bv,   half-bv,0,   -half+bv,0 };
    double path[64*2];
    int np=roundRectPath(cw,ch,cr,48,path);
    sweepClosed(path,np,sec,8,&m); lmBake(&m,MP,TP);
    emit(LMP_PANEL,s<0?"pod_hoop_l":"pod_hoop_r",&m);

    mkCyl(POD_CAPR0,POD_CAPR1,POD_CAPLEN,22,0,&m);
    rot3X(PIH,M); T[0]=0; T[1]=0; T[2]=POD_CAPZ;
    lmBake(&m,M,T); lmBake(&m,MP,TP);
    emit(LMP_DARK,s<0?"pod_cap_l":"pod_cap_r",&m);

    mkTorus(POD_RINGR,POD_RINGT,26,6,&m);
    T[0]=0; T[1]=0; T[2]=POD_RINGZ;
    lmBake(&m,0,T); lmBake(&m,MP,TP);
    emit(LMP_STEEL,s<0?"pod_ring_l":"pod_ring_r",&m);

    { LM holes; memset(&holes,0,sizeof holes);
      double hx[5],hy[5];
      for(int k=0;k<4;k++){
        double a=k/4.0*PI2+POD_HOLEPH;
        hx[k]=POD_HOLERING*cos(a); hy[k]=POD_HOLERING*sin(a);
      }
      hx[4]=0; hy[4]=0;
      for(int k=0;k<5;k++){
        LM h; mkCircle(POD_HOLER,12,&h);
        T[0]=hx[k]; T[1]=hy[k]; T[2]=POD_HOLEZ;
        lmBake(&h,0,T);
        lmMerge(&holes,&h);
      }
      lmBake(&holes,MP,TP);
      emit(LMP_DARK,s<0?"pod_holes_l":"pod_holes_r",&holes); }

    mkBox(0.004,0.032,0.003,&m);
    T[0]=0; T[1]=0.064; T[2]=0.032;
    lmBake(&m,0,T); lmBake(&m,MP,TP);
    emit(LMP_DIM,s<0?"pod_slit_l":"pod_slit_r",&m);
  }

  /* --- the back: five running lights, the service slot, its grill ---- */
  { LM leds; memset(&leds,0,sizeof leds);
    for(int k=0;k<5;k++){
      double d[3]={-0.22+k*0.11,-0.20,-0.95};
      double dn=sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
      LM b; mkBox(0.014,0.009,0.005,&b);
      basisTowards(d,M);
      T[0]=d[0]/dn*0.2248; T[1]=d[1]/dn*0.2248; T[2]=d[2]/dn*0.2248;
      lmBake(&b,M,T);
      lmMerge(&leds,&b);
    }
    emit(LMP_DIM,"back_leds",&leds); }
  { double d[3]={0,-0.44,-0.90}, dn=sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    double sp[3]={d[0]/dn*0.222,d[1]/dn*0.222,d[2]/dn*0.222};
    basisTowards(d,M);
    mkBox(0.090,0.028,0.012,&m); lmBake(&m,M,sp); emit(LMP_DARK,"back_slot",&m);
    T[0]=sp[0]*1.02; T[1]=sp[1]*1.02; T[2]=sp[2]*1.02;
    mkBox(0.076,0.004,0.004,&m); lmBake(&m,M,T); emit(LMP_PANEL,"back_grill",&m); }

  /* --- the antenna --------------------------------------------------- */
  mkCyl(AN_R0,AN_R1,AN_LEN,10,0,&m);
  rot3X(AN_TILT,M); T[0]=0; T[1]=0.252; T[2]=-0.020;
  lmBake(&m,M,T); emit(LMP_PANEL,"antenna",&m);
  mkSphere(AN_TIPR,10,8,&m);
  T[0]=0; T[1]=0.297; T[2]=-0.027;
  lmBake(&m,0,T); emit(LMP_STEEL,"antenna_tip",&m);

  /* --- the lift emitter.  A ring, a disc, and two diffusion layers. -- */
  mkTorus(EM_RINGR,EM_RINGT,44,8,&m);
  rot3X(PIH,M); T[0]=0; T[1]=LM_EMIT_Y+EM_RINGDY; T[2]=0;
  lmBake(&m,M,T); emit(LMP_CYAN,"emit_ring",&m);
  mkCircle(EM_DISCR,44,&m);
  T[1]=LM_EMIT_Y+EM_DISCDY;
  lmBake(&m,M,T); emit(LMP_GLOW,"emit_disc",&m);
  { const double rT[2]={0.086,0.100}, rB[2]={0.118,0.150};
    for(int i=0;i<2;i++){
      /* beamLayer() bakes a vertical fade into VERTEX COLOUR, and this
         renderer's colour is per instance.  The geometry is the JS's; the
         fade is the two layers' own emissive, set in lumi.c. */
      mkCyl(rT[i],rB[i],BEAM_H,36,1,&m);
      T[0]=0; T[1]=LM_EMIT_Y-BEAM_H/2; T[2]=0;
      lmBake(&m,0,T);
      emit(i?LMP_BEAM1:LMP_BEAM0,i?"beam_1":"beam_0",&m);
    } }

  buildMark();
}

/* ---- the same patch again, with the uv the generator computes ------
   face.c draws LUMI's display ON the screen patch, the way the bench puts
   a 512x290 canvas on it, so it needs the surface's own uv -- and the
   entity buffer carries position and normal and nothing else.

   The uv is not stored; it is RECOVERED.  spherePatch projects a point by
   scaling (px,py,Rp) onto the sphere, so the projected z is Rp*k and
   px = p.x*Rp/p.z exactly.  That is the same number the generator wrote
   into its uv array, arrived at from the vertex rather than kept beside
   it -- one less thing that can drift out of step with the shape. */
int lumiMeshScreen(float **out){
  LM m;
  spherePatch(V_RSCREEN,V_A,V_B,V_EXPO,64,8,0.002,1.0,&m);
  float *v=(float*)malloc(sizeof(float)*(size_t)m.ni*5);
  double MV[9]; rot3X(V_TILT,MV);
  for(int i=0;i<m.ni;i++){
    int k=m.ix[i]*3;
    double x=m.p[k], y=m.p[k+1], z=m.p[k+2];
    double px=x*V_RSCREEN/z, py=y*V_RSCREEN/z;
    float *o=v+(size_t)i*5;
    o[0]=(float)(MV[0]*x+MV[1]*y+MV[2]*z);
    o[1]=(float)(MV[3]*x+MV[4]*y+MV[5]*z);
    o[2]=(float)(MV[6]*x+MV[7]*y+MV[8]*z);
    o[3]=(float)(0.5+px/(2*V_A));
    o[4]=(float)(0.5+py/(2*V_B));
  }
  int n=m.ni;
  lmFree(&m);
  *out=v;
  return n;
}

void lumiMeshCheck(int *parts,int *verts,int *tris){
  if(parts)*parts=ckParts; if(verts)*verts=ckVerts; if(tris)*tris=ckTris;
}

void lumiMeshInit(void){
  double t0=platTime(), e[2];
  dumpOn = strstr(platCmdline(),"-lumimesh")!=0;
  if(dumpOn) platLog("%-16s %5s %5s  %8s %8s %8s  %8s %8s %8s",
                     "part","verts","tris","minx","miny","minz",
                     "maxx","maxy","maxz");
  spherePt(P_A5,0,e);
  LM_EMIT_Y=(float)(e[1]-0.055);
  ckParts=ckVerts=ckTris=0;
  memset(G,0,sizeof G);
  buildLumi();
  int soup=0;
  for(int i=0;i<LMP_N;i++){
    LUMI_PRIM[i]=primAdd(G[i].v,G[i].nv);
    soup+=G[i].nv;
    free(G[i].v); G[i].v=0; G[i].nv=G[i].cap=0;
  }
  /* The generator's own checksum.  If these three are not 35 / 9627 / 14772
     the port built a different robot, and the log says so rather than
     leaving it to be looked at and judged close enough. */
  platLog("lumi   %d parts  %d verts  %d tris  (expect 35/9627/14772)  "
          "%d groups  %d soup  %.0f ms",
          ckParts,ckVerts,ckTris,LMP_N,soup,(platTime()-t0)*1000.0);
}
