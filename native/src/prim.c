/* ============================================================
   PRIMITIVES.  The mesh side is the same idea as the
   texture side: a handful of solids and a few operators over
   them.  These are generated at load into one buffer and drawn
   by RANGE, so instancing stays exactly as cheap as it was with
   a bare cube -- but every edge now has a chamfer for a
   highlight to run along, which is most of what separates
   "modelled" from "blocks".

   Each primitive fills the unit cube, so a caller's size
   arguments keep working whichever one it asks for.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include "mesh.h"
#include "prim.h"
#include <stdlib.h>
#include <string.h>

float *PRIM_V; int PRIM_VN;              /* 6 floats a vertex: pos, nrm */
/* ---- AND A SECOND BUFFER, FOR THE ONE THING A POSITION CANNOT SAY.

   props.c carries the bench's own regions -- a face knows which of eleven
   height functions draws it, at what seed, with what two parameters, and
   how big it is in metres.  All of that is per FACE, so it cannot ride on
   the instance (a prop is one instance per material group and a group
   holds many regions) and it cannot be recovered from the position.  It
   has to be per vertex.

   It is a PARALLEL buffer rather than a wider PRIM_V because every solid
   in this file writes six floats a vertex in about forty places, and none
   of them has a region: widening the vertex would mean editing all forty
   to write eight zeroes.  This way prim.c's builders are untouched, the
   extra attributes are zero everywhere they are meaningless, and the
   shader skips the whole block on kind < 0.
     PRIM_R[v*8+0..3]  u, v, ww, wh   -- region-local metres, region size
     PRIM_R[v*8+4..7]  kind, seed, params.x, params.y                    */
float *PRIM_R;
int PRIM_OFF[PRIM_N_MAX], PRIM_CNT[PRIM_N_MAX];
int PRIM_N;

static int cap;
static void need(int n){
  if(PRIM_VN+n>cap){
    int old=cap;
    while(cap<PRIM_VN+n) cap = cap? cap*2 : (1<<16);
    PRIM_V=(float*)realloc(PRIM_V,sizeof(float)*cap);
    PRIM_R=(float*)realloc(PRIM_R,sizeof(float)*(size_t)(cap/6)*8);
    /* NOT a plain zero-fill of the whole thing: the ranges already written
       carry regions and a realloc must not erase them.  Only the new tail.
       A kind of -1 is "no region", which is what every solid in this file
       and every one of LUMI's thirteen groups means. */
    for(int i=old/6;i<cap/6;i++){
      float *r=PRIM_R+(size_t)i*8;
      r[0]=r[1]=r[2]=r[3]=0.f;
      r[4]=-1.f; r[5]=r[6]=r[7]=0.f;
    }
  }
}
static void nrm3(float x,float y,float z,float *o){
  float m=hypot3(x,y,z); if(m<1e-6f) m=1e-6f;
  o[0]=x/m; o[1]=y/m; o[2]=z/m;
}
/* wind counter-clockwise as seen from outside, whatever order came in */
static void tri(const float *p,const float *q,const float *r,
                const float *np,const float *nq,const float *nr){
  float e1[3]={q[0]-p[0],q[1]-p[1],q[2]-p[2]};
  float e2[3]={r[0]-p[0],r[1]-p[1],r[2]-p[2]};
  float c[3]={e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2],
              e1[0]*e2[1]-e1[1]*e2[0]};
  int flip = c[0]*(np[0]+nq[0]+nr[0])+c[1]*(np[1]+nq[1]+nr[1])
            +c[2]*(np[2]+nq[2]+nr[2]) < 0;
  const float *P[3]={p, flip?r:q, flip?q:r};
  const float *N[3]={np, flip?nr:nq, flip?nq:nr};
  need(18);
  for(int i=0;i<3;i++){
    float *o=PRIM_V+PRIM_VN; PRIM_VN+=6;
    o[0]=P[i][0]; o[1]=P[i][1]; o[2]=P[i][2];
    o[3]=N[i][0]; o[4]=N[i][1]; o[5]=N[i][2];
  }
}
static void quad(const float *p,const float *q,const float *r,const float *t,
                 const float *np,const float *nq,const float *nr,const float *nt){
  tri(p,q,r,np,nq,nr); tri(p,r,t,np,nr,nt);
}
static void beginPrim(void){ PRIM_OFF[PRIM_N]=PRIM_VN/6; }
static void endPrim(void){
  PRIM_CNT[PRIM_N]=PRIM_VN/6-PRIM_OFF[PRIM_N];
  PRIM_N++;
}

/* ---- a lathe, for everything that is round about Y ------------------ */
typedef float (*Prof)(float v,const void *ctx);
static void lathe(Prof prof,const void *ctx,int seg,int rings,int capLo,int capHi){
  typedef struct { float y,rad,dr; } Ring;
  Ring *R=(Ring*)malloc(sizeof(Ring)*(rings+1));
  for(int r=0;r<=rings;r++){
    float v=(float)r/rings;
    float dv=1.f/(rings*8);
    float hi=v+dv>1?1:v+dv, lo=v-dv<0?0:v-dv;
    R[r].y=-0.5f+v;
    R[r].rad=prof(v,ctx);
    R[r].dr=(prof(hi,ctx)-prof(lo,ctx))/(2*dv);
  }
  for(int r=0;r<rings;r++){
    Ring A=R[r], B=R[r+1];
    for(int s=0;s<seg;s++){
      float t0=(float)s/seg*6.2831853f, t1=(float)(s+1)/seg*6.2831853f;
      float c0=cosf(t0), s0=sinf(t0), c1=cosf(t1), s1=sinf(t1);
      float pA0[3]={c0*A.rad,A.y,s0*A.rad}, pA1[3]={c1*A.rad,A.y,s1*A.rad};
      float pB0[3]={c0*B.rad,B.y,s0*B.rad}, pB1[3]={c1*B.rad,B.y,s1*B.rad};
      float nA0[3],nA1[3],nB0[3],nB1[3];
      nrm3(c0,-A.dr,s0,nA0); nrm3(c1,-A.dr,s1,nA1);
      nrm3(c0,-B.dr,s0,nB0); nrm3(c1,-B.dr,s1,nB1);
      if(A.rad<1e-5f)      tri(pA0,pB0,pB1,nA0,nB0,nB1);
      else if(B.rad<1e-5f) tri(pA0,pA1,pB0,nA0,nA1,nB0);
      else                 quad(pA0,pA1,pB1,pB0,nA0,nA1,nB1,nB0);
    }
  }
  for(int k=0;k<2;k++){
    int doCap = k? capHi : capLo;
    float y = k? 0.5f : -0.5f;
    float rad = prof(k?1.f:0.f,ctx);
    float ny = k? 1.f : -1.f;
    if(!doCap||rad<1e-5f) continue;
    float n[3]={0,ny,0}, o[3]={0,y,0};
    for(int s=0;s<seg;s++){
      float t0=(float)s/seg*6.2831853f, t1=(float)(s+1)/seg*6.2831853f;
      float p0[3]={cosf(t0)*rad,y,sinf(t0)*rad};
      float p1[3]={cosf(t1)*rad,y,sinf(t1)*rad};
      tri(o,p0,p1,n,n,n);
    }
  }
  free(R);
}

/* ---- the profiles --------------------------------------------------- */
static float pfCap(float v,const void *c){
  (void)c; const float f=0.30f;
  if(v<f){ float t=(f-v)/f; return 0.5f*sqrtf(1-t*t>0?1-t*t:0); }
  if(v>1-f){ float t=(v-(1-f))/f; return 0.5f*sqrtf(1-t*t>0?1-t*t:0); }
  return 0.5f;
}
static float pfDome(float v,const void *c){
  (void)c; return 0.5f*sqrtf(1-v*v>0?1-v*v:0);
}
/* POD.  The creature's soft masses: a capsule with a shallow barrel to it
   and twice the sides, because a waist seen from a metre away is where
   facets show first.  Normalised so the crown of the barrel is exactly the
   unit cube's wall and the ends draw in. */
static float pfPod(float v,const void *c){
  (void)c; const float f=0.34f;
  float bar=(1.0f+0.065f*sinf(v*3.14159265f))/1.065f;
  if(v<f){ float t=(f-v)/f; return 0.5f*bar*sqrtf(1-t*t>0?1-t*t:0); }
  if(v>1-f){ float t=(v-(1-f))/f; return 0.5f*bar*sqrtf(1-t*t>0?1-t*t:0); }
  return 0.5f*bar;
}
static float pfLens(float v,const void *c){
  (void)c; float k=1-v*v; if(k<0)k=0; return 0.5f*powf(k,0.42f);
}
static float pfSeta(float v,const void *c){
  (void)c; float k=1-v; if(k<0)k=0;
  return 0.5f*powf(k,0.72f)*(1-0.34f*ss(0,0.22f,v));
}
/* SEG.  A limb bone or a tail link, carrying the same rear lip the carapace
   plates have, so a run of them shows steps rather than joints that dissolve
   into one another.  The ring count is chosen so the samples land exactly on
   the lip's edges and it stays a step instead of smearing. */
static float pfSeg(float v,const void *c){
  (void)c;
  if(v<0.084f) return mixf(0.30f,0.50f,ss(0,0.084f,v));
  if(v<0.168f) return 0.50f;
  if(v<0.252f) return mixf(0.50f,0.40f,ss(0.168f,0.252f,v));
  if(v<0.60f)  return mixf(0.40f,0.455f,ss(0.252f,0.60f,v));
  return mixf(0.455f,0.19f,ss(0.60f,1,v));
}
/* HORN.  A claw, a fang, a spine: it leaves its base on a flange and ends in
   a point.  A capsule ends in a hemisphere, which is the entire reason every
   spike drawn with one reads as a sausage. */
static float pfHorn(float v,const void *c){
  (void)c;
  if(v<0.11f) return mixf(0.50f,0.30f,ss(0,0.11f,v));
  return 0.30f*powf(1-ss(0.11f,1,v),0.58f);
}
static float pfCyl(float v,const void *c){ (void)v;(void)c; return 0.5f; }

/* ---- the sculpted ones ---------------------------------------------- */
static void deform_drum(float *p,void *c){ (void)c; p[1]*=1.52f; }
static Mesh *mkDrum(void){
  Mesh *m=mCylinder(12,1);
  mDeform(m,deform_drum,0);
  mSelNone(m); mSelNormal(m,0,1,0,20,MSEL_SET);  mExtrude(m,0.02f,0.14f); /* lid rim */
  mSelNone(m); mSelNormal(m,0,-1,0,20,MSEL_SET); mExtrude(m,0.02f,0.14f);
  mCreaseAngle(m,30,0);
  m=mSubdivide(m,1);
  mFitUniform(m);
  return m;
}
static Mesh *mkCrate(void){
  Mesh *m=mCube();
  mSelAll(m,1);
  mInsetFaces(m,0.17f);                          /* a rim around each face */
  mExtrude(m,-0.05f,0);                          /* and the panel sinks in */
  mCreaseAngle(m,30,0);
  m=mSubdivide(m,1);
  mFitUniform(m);
  return m;
}
/* CARAPACE PLATE.  A cube taken through the mesh machine until nothing of
   the cube is left: the square cross-section is rounded, a keel is raised
   down the spine, the front tapers, and the rear flares into a lip.

   The lip is the whole point.  A run of these overlaps at the tuck behind
   each lip, so what you see between two segments is a step -- and a step
   reads as a joint.  Two ellipsoids crossing never will. */
static void deform_cara(float *p,void *c){
  (void)c;
  float u=p[2]+0.5f;                             /* 0 at the rear, 1 forward */
  /* round the square cross-section: a point on the unit square at angle a
     has radius 0.5/max(|cos|,|sin|), so multiplying by that max gives a
     circle */
  float a=atan2f(p[1],p[0]);
  float ca=fabsf(cosf(a)), sa=fabsf(sinf(a));
  float k=ca>sa?ca:sa;
  p[0]*=k; p[1]*=k;
  float e=(u-0.13f)/0.10f;
  float lip=1+0.26f*expf(-e*e);
  float taper=1.02f-0.34f*ss(0.44f,1.0f,u);
  p[0]*=lip*taper; p[1]*=lip*taper;
  if(p[1]>0){                                    /* the keel */
    float cc=1-(fabsf(p[0])*3.1f<1?fabsf(p[0])*3.1f:1);
    p[1]+=0.17f*cc*cc*taper;
  }
}
static Mesh *mkCara(void){
  Mesh *m=mCube();
  m=mSplit(m,3);                                 /* 8x8 a face */
  mDeform(m,deform_cara,0);
  mCreaseAngle(m,52,0);
  mFitBox(m,0);
  return m;
}

static void sculpt(Mesh *(*recipe)(void),float angle){
  Mesh *m=recipe();
  mOrient(m);
  mEmit(m,angle,&PRIM_V,&PRIM_VN,&cap);
  mFree(m);
}

void primInit(void){
  double t0=platTime();
  PRIM_N=0; PRIM_VN=0;

  /* ---- the plain box, still right for panels, doors and crates ---- */
  beginPrim();
  { static const float F[6][3][3]={
      {{0,0,1},{1,0,0},{0,1,0}},{{0,0,-1},{-1,0,0},{0,1,0}},
      {{1,0,0},{0,0,-1},{0,1,0}},{{-1,0,0},{0,0,1},{0,1,0}},
      {{0,1,0},{1,0,0},{0,0,-1}},{{0,-1,0},{1,0,0},{0,0,1}}};
    for(int f=0;f<6;f++){
      const float *n=F[f][0], *u=F[f][1], *v=F[f][2];
      float c[3]={n[0]*0.5f,n[1]*0.5f,n[2]*0.5f};
      float p[4][3];
      static const int S[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
      for(int i=0;i<4;i++) for(int k=0;k<3;k++)
        p[i][k]=c[k]+u[k]*S[i][0]*0.5f+v[k]*S[i][1]*0.5f;
      quad(p[0],p[1],p[2],p[3],n,n,n,n);
    } }
  endPrim();                                     /* PRIM_BOX */

  /* ---- the chamfered box.  Eight corners pulled in by b on every axis,
     then the faces, the twelve edge strips and the eight corner triangles
     that join them. */
  beginPrim();
  { const float b=0.115f, h=0.5f, i=h-b;
    for(int ax=0;ax<3;ax++) for(int k=0;k<2;k++){
      float sg=k?-1.f:1.f;
      float n[3]={0,0,0}; n[ax]=sg;
      float p[4][3];
      static const int S[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
      for(int q=0;q<4;q++){
        p[q][0]=p[q][1]=p[q][2]=0;
        p[q][ax]=h*sg;
        p[q][(ax+1)%3]=i*S[q][0]*(sg>0?1:-1);
        p[q][(ax+2)%3]=i*S[q][1];
      }
      quad(p[0],p[1],p[2],p[3],n,n,n,n);
    }
    for(int a1=0;a1<3;a1++) for(int a2=a1+1;a2<3;a2++){
      int a3=3-a1-a2;
      for(int i1=0;i1<2;i1++) for(int i2=0;i2<2;i2++){
        float s1=i1?-1.f:1.f, s2=i2?-1.f:1.f;
        float n[3];
        nrm3((a1==0?s1:a2==0?s2:0),(a1==1?s1:a2==1?s2:0),(a1==2?s1:a2==2?s2:0),n);
        float p[4][3];
        static const int T[4][2]={{-1,1},{1,1},{1,2},{-1,2}};
        for(int q=0;q<4;q++){
          p[q][0]=p[q][1]=p[q][2]=0;
          p[q][a1]=(T[q][1]==1?h:i)*s1;
          p[q][a2]=(T[q][1]==1?i:h)*s2;
          p[q][a3]=T[q][0]*i;
        }
        quad(p[0],p[1],p[2],p[3],n,n,n,n);
      }
    }
    for(int ix=0;ix<2;ix++) for(int iy=0;iy<2;iy++) for(int iz=0;iz<2;iz++){
      float sx=ix?-1.f:1.f, sy=iy?-1.f:1.f, sz=iz?-1.f:1.f;
      float n[3]; nrm3(sx,sy,sz,n);
      float p[3]={h*sx,i*sy,i*sz}, q[3]={i*sx,h*sy,i*sz}, r[3]={i*sx,i*sy,h*sz};
      tri(p,q,r,n,n,n);
    } }
  endPrim();                                     /* PRIM_BEVEL */

  beginPrim(); lathe(pfCap ,0, 8, 8,0,0); endPrim();   /* CAP  -- a limb */
  beginPrim(); lathe(pfDome,0,10, 5,1,0); endPrim();   /* DOME */
  beginPrim(); lathe(pfPod ,0,14,12,0,0); endPrim();   /* POD */
  beginPrim(); lathe(pfLens,0,12, 8,1,0); endPrim();   /* LENS -- an eye */
  beginPrim(); lathe(pfSeta,0, 5, 5,1,0); endPrim();   /* SETA -- a bristle */
  beginPrim(); lathe(pfSeg ,0,14,12,1,1); endPrim();   /* SEG */
  beginPrim(); lathe(pfHorn,0,12,12,1,0); endPrim();   /* HORN -- a claw */
  beginPrim(); lathe(pfCyl ,0,10, 1,1,1); endPrim();   /* CYL */

  beginPrim(); sculpt(mkDrum ,40); endPrim();
  beginPrim(); sculpt(mkCrate,40); endPrim();
  beginPrim(); sculpt(mkCara ,46); endPrim();

  platLog("prim   %d solids  %d verts  %.0f ms",
          PRIM_N,PRIM_VN/6,(platTime()-t0)*1000.0);
}

/* ---- a solid built elsewhere.  See prim.h. -------------------------- */
int primAdd(const float *v,int nv){ return primAddR(v,0,nv); }

/* ... and the same solid WITH the bench's region on every vertex.  r8 may
   be null, which is what primAdd above is. */
int primAddR(const float *v,const float *r8,int nv){
  if(PRIM_N>=PRIM_N_MAX||nv<=0) return PRIM_BEVEL;
  beginPrim();
  need(nv*6);
  memcpy(PRIM_V+PRIM_VN,v,sizeof(float)*(size_t)nv*6);
  if(r8) memcpy(PRIM_R+(size_t)(PRIM_VN/6)*8,r8,sizeof(float)*(size_t)nv*8);
  PRIM_VN+=nv*6;
  endPrim();
  return PRIM_N-1;
}
