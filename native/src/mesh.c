/* The mesh machine.  See mesh.h for what it is for.

   EVERY OPERATOR HERE IS A PORT, NOT A REWRITE.  The kit's twenty-one parts
   are recipes over these calls and were tuned against the JS versions'
   exact behaviour -- including the parts that look like accidents, such as
   mSplit throwing its creases away and re-adding only the caller's.  A
   "better" subdivision here changes every part in the ship. */
#include "mesh.h"
#include <stdlib.h>
#include <string.h>

#define EK 1000003LL
static int meshTriOK(const Mesh*,const float*,int,int,int,int);

static long long ekey(int a,int b){
  return a<b ? (long long)a*EK+b : (long long)b*EK+a;
}

/* ---- growable bits -------------------------------------------------- */
static void needV(Mesh *m,int n){
  if(m->nV*3+n*3>m->capV){
    while(m->capV<m->nV*3+n*3) m->capV=m->capV?m->capV*2:192;
    m->V=(float*)realloc(m->V,sizeof(float)*m->capV);
  }
}
static void needF(Mesh *m,int n){
  if(m->nF+n>m->capF){
    while(m->capF<m->nF+n) m->capF=m->capF?m->capF*2:64;
    m->F=(MFace*)realloc(m->F,sizeof(MFace)*m->capF);
  }
}
static void needI(Mesh *m,int n){
  if(m->nFI+n>m->capFI){
    while(m->capFI<m->nFI+n) m->capFI=m->capFI?m->capFI*2:256;
    m->FI=(int*)realloc(m->FI,sizeof(int)*m->capFI);
  }
}

int mAddVert(Mesh *m,float x,float y,float z){
  needV(m,1);
  m->V[m->nV*3]=x; m->V[m->nV*3+1]=y; m->V[m->nV*3+2]=z;
  return m->nV++;
}
void mAddFace(Mesh *m,const int *idx,int n,int sel){
  needF(m,1); needI(m,n);
  MFace *f=&m->F[m->nF++];
  f->off=m->nFI; f->n=n; f->s=sel;
  for(int i=0;i<n;i++) m->FI[m->nFI++]=idx[i];
}

/* ---- the crease set: open addressing over int64 keys ---------------- */
static void csGrow(Mesh *m,int want){
  int cap=m->capC?m->capC:64;
  while(cap < want*2) cap*=2;
  if(cap==m->capC) return;
  long long *old=m->C; int oc=m->capC;
  m->C=(long long*)calloc(cap,sizeof(long long));
  m->capC=cap;
  if(old){
    for(int i=0;i<oc;i++) if(old[i]){
      unsigned h=(unsigned)(old[i]*2654435761u)&(cap-1);
      while(m->C[h]) h=(h+1)&(cap-1);
      m->C[h]=old[i];
    }
    free(old);
  }
}
static void csAdd(Mesh *m,long long k){
  if(!k) return;
  csGrow(m,m->nC+1);
  unsigned h=(unsigned)(k*2654435761u)&(m->capC-1);
  while(m->C[h]){ if(m->C[h]==k) return; h=(h+1)&(m->capC-1); }
  m->C[h]=k; m->nC++;
}
static int csHas(const Mesh *m,long long k){
  if(!m->capC) return 0;
  unsigned h=(unsigned)(k*2654435761u)&(m->capC-1);
  while(m->C[h]){ if(m->C[h]==k) return 1; h=(h+1)&(m->capC-1); }
  return 0;
}
/* deletion by rebuild: the only caller is mUncreaseBox, once per part */
static void csClear(Mesh *m){
  if(m->C) memset(m->C,0,sizeof(long long)*m->capC);
  m->nC=0;
}

/* ---- the edge map --------------------------------------------------- */
typedef struct { long long k; int a,b,f0,f1,nf,pt; } Edge;
typedef struct { Edge *e; int cap,n; int *slot; } EdgeMap;

static void emInit(EdgeMap *E,int guess){
  E->cap=64; while(E->cap<guess*2) E->cap*=2;
  E->slot=(int*)malloc(sizeof(int)*E->cap);
  for(int i=0;i<E->cap;i++) E->slot[i]=-1;
  E->e=(Edge*)malloc(sizeof(Edge)*(guess+16));
  E->n=0;
}
static void emFree(EdgeMap *E){ free(E->e); free(E->slot); }
static Edge *emGet(EdgeMap *E,long long k){
  unsigned h=(unsigned)(k*2654435761u)&(E->cap-1);
  while(E->slot[h]>=0){
    if(E->e[E->slot[h]].k==k) return &E->e[E->slot[h]];
    h=(h+1)&(E->cap-1);
  }
  return 0;
}
static Edge *emPut(EdgeMap *E,long long k,int a,int b){
  unsigned h=(unsigned)(k*2654435761u)&(E->cap-1);
  while(E->slot[h]>=0){
    if(E->e[E->slot[h]].k==k) return &E->e[E->slot[h]];
    h=(h+1)&(E->cap-1);
  }
  E->slot[h]=E->n;
  Edge *e=&E->e[E->n++];
  e->k=k; e->a=a<b?a:b; e->b=a<b?b:a; e->f0=e->f1=-1; e->nf=0; e->pt=-1;
  return e;
}
static void buildEdges(const Mesh *m,EdgeMap *E){
  int guess=0;
  for(int fi=0;fi<m->nF;fi++) guess+=m->F[fi].n;
  emInit(E,guess);
  for(int fi=0;fi<m->nF;fi++){
    const MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++){
      int a=MFV(m,f,i), b=MFV(m,f,(i+1)%f->n);
      Edge *e=emPut(E,ekey(a,b),a,b);
      if(e->nf==0) e->f0=fi; else if(e->nf==1) e->f1=fi;
      e->nf++;
    }
  }
}

/* ---- lifecycle ------------------------------------------------------ */
Mesh *mNew(void){ return (Mesh*)calloc(1,sizeof(Mesh)); }
void mFree(Mesh *m){ if(!m) return; free(m->V); free(m->F); free(m->FI); free(m->C); free(m); }
Mesh *mClone(const Mesh *m){
  Mesh *o=mNew();
  o->nV=m->nV; o->capV=m->nV*3;
  o->V=(float*)malloc(sizeof(float)*(o->capV?o->capV:1));
  memcpy(o->V,m->V,sizeof(float)*m->nV*3);
  o->nF=m->nF; o->capF=m->nF?m->nF:1;
  o->F=(MFace*)malloc(sizeof(MFace)*o->capF);
  memcpy(o->F,m->F,sizeof(MFace)*m->nF);
  o->nFI=m->nFI; o->capFI=m->nFI?m->nFI:1;
  o->FI=(int*)malloc(sizeof(int)*o->capFI);
  memcpy(o->FI,m->FI,sizeof(int)*m->nFI);
  for(int i=0;i<m->capC;i++) if(m->C[i]) csAdd(o,m->C[i]);
  return o;
}

/* ---- inspection ----------------------------------------------------- */
void mFaceNormal(const Mesh *m,const MFace *f,float *out){   /* Newell */
  float nx=0,ny=0,nz=0;
  for(int i=0;i<f->n;i++){
    int a=MFV(m,f,i)*3, b=MFV(m,f,(i+1)%f->n)*3;
    nx+=(m->V[a+1]-m->V[b+1])*(m->V[a+2]+m->V[b+2]);
    ny+=(m->V[a+2]-m->V[b+2])*(m->V[a  ]+m->V[b  ]);
    nz+=(m->V[a  ]-m->V[b  ])*(m->V[a+1]+m->V[b+1]);
  }
  float L=hypot3(nx,ny,nz); if(L<1e-20f) L=1;
  out[0]=nx/L; out[1]=ny/L; out[2]=nz/L;
}
void mFaceCentre(const Mesh *m,const MFace *f,float *out){
  float x=0,y=0,z=0;
  for(int i=0;i<f->n;i++){
    int v=MFV(m,f,i)*3;
    x+=m->V[v]; y+=m->V[v+1]; z+=m->V[v+2];
  }
  out[0]=x/f->n; out[1]=y/f->n; out[2]=z/f->n;
}
static void faceReverse(Mesh *m,MFace *f){
  for(int i=0,j=f->n-1;i<j;i++,j--){
    int t=m->FI[f->off+i]; m->FI[f->off+i]=m->FI[f->off+j]; m->FI[f->off+j]=t;
  }
}

/* ---- primitives ----------------------------------------------------- */
void mFixWinding(Mesh *m){               /* for convex starts: face out */
  float n[3],c[3];
  for(int fi=0;fi<m->nF;fi++){
    MFace *f=&m->F[fi];
    mFaceNormal(m,f,n); mFaceCentre(m,f,c);
    if(n[0]*c[0]+n[1]*c[1]+n[2]*c[2]<0) faceReverse(m,f);
  }
}
void mAddMesh(Mesh *a,const Mesh *b){
  int off=a->nV;
  for(int i=0;i<b->nV;i++) mAddVert(a,b->V[i*3],b->V[i*3+1],b->V[i*3+2]);
  for(int fi=0;fi<b->nF;fi++){
    const MFace *f=&b->F[fi];
    needF(a,1); needI(a,f->n);
    MFace *g=&a->F[a->nF++];
    g->off=a->nFI; g->n=f->n; g->s=0;
    for(int i=0;i<f->n;i++) a->FI[a->nFI++]=b->FI[f->off+i]+off;
  }
}

void mSpin(Mesh *m,float ang){
  float c=cosf(ang), s=sinf(ang);
  for(int i=0;i<m->nV;i++){
    float x=m->V[i*3]*c-m->V[i*3+2]*s, z=m->V[i*3]*s+m->V[i*3+2]*c;
    m->V[i*3]=x; m->V[i*3+2]=z;
  }
}
void mTilt(Mesh *m,float ang){           /* about X: +Y leans toward +Z */
  float c=cosf(ang), s=sinf(ang);
  for(int i=0;i<m->nV;i++){
    float y=m->V[i*3+1]*c-m->V[i*3+2]*s, z=m->V[i*3+1]*s+m->V[i*3+2]*c;
    m->V[i*3+1]=y; m->V[i*3+2]=z;
  }
}
void mRoll(Mesh *m,float ang){           /* about Z: stands a Y axis up along X */
  float c=cosf(ang), s=sinf(ang);
  for(int i=0;i<m->nV;i++){
    float x=m->V[i*3]*c-m->V[i*3+1]*s, y=m->V[i*3]*s+m->V[i*3+1]*c;
    m->V[i*3]=x; m->V[i*3+1]=y;
  }
}

/* Mesh_Torus + their rot .25.  Built in XY, not XZ: their chain rotates it a
   quarter turn, and without that the passage ring is a hoop lying flat on the
   deck at head height. */
Mesh *mTorus(int seg,int slices,float r1,float r2){
  Mesh *m=mNew();
  for(int i=0;i<seg;i++){
    float a=(float)i/seg*6.2831853f, ca=cosf(a), sa=sinf(a);
    for(int j=0;j<slices;j++){
      float b=(float)j/slices*6.2831853f;
      float rr=r1+cosf(b)*r2;
      mAddVert(m,ca*rr,sa*rr,sinf(b)*r2);
    }
  }
  for(int i=0;i<seg;i++){
    int i2=(i+1)%seg;
    for(int j=0;j<slices;j++){
      int j2=(j+1)%slices;
      int q[4]={i*slices+j, i2*slices+j, i2*slices+j2, i*slices+j2};
      mAddFace(m,q,4,0);
    }
  }
  mFixWinding(m);
  return m;
}

Mesh *mCube(void){
  Mesh *m=mNew();
  for(int sz=-1;sz<=1;sz+=2) for(int sy=-1;sy<=1;sy+=2) for(int sx=-1;sx<=1;sx+=2)
    mAddVert(m,sx*0.5f,sy*0.5f,sz*0.5f);
  /* vertex index = sx + sy*2 + sz*4 with the -1 case first */
  static const int Q[6][4]={{0,1,3,2},{4,5,7,6},{0,1,5,4},{2,3,7,6},{0,2,6,4},{1,3,7,5}};
  for(int i=0;i<6;i++) mAddFace(m,Q[i],4,0);
  mFixWinding(m);
  return m;
}
Mesh *mCylinder(int seg,int capped){
  Mesh *m=mNew();
  for(int s=0;s<2;s++){
    float y = s? 0.5f : -0.5f;
    for(int i=0;i<seg;i++){
      float t=(float)i/seg*6.2831853f;
      mAddVert(m,cosf(t)*0.5f,y,sinf(t)*0.5f);
    }
  }
  for(int i=0;i<seg;i++){
    int j=(i+1)%seg;
    int q[4]={i,j,seg+j,seg+i};
    mAddFace(m,q,4,0);
  }
  if(capped){
    int *lo=(int*)malloc(sizeof(int)*seg), *hi=(int*)malloc(sizeof(int)*seg);
    for(int i=0;i<seg;i++){ lo[seg-1-i]=i; hi[i]=seg+i; }   /* lo is reversed */
    mAddFace(m,lo,seg,0);
    mAddFace(m,hi,seg,0);
    free(lo); free(hi);
  }
  mFixWinding(m);
  return m;
}
Mesh *mGrid(int nx,int nz){
  Mesh *m=mNew();
  for(int j=0;j<=nz;j++) for(int i=0;i<=nx;i++)
    mAddVert(m,-0.5f+(float)i/nx,0,-0.5f+(float)j/nz);
  for(int j=0;j<nz;j++) for(int i=0;i<nx;i++){
    int q[4]={ j*(nx+1)+i, (j+1)*(nx+1)+i, (j+1)*(nx+1)+i+1, j*(nx+1)+i+1 };
    mAddFace(m,q,4,0);
  }
  return m;
}

/* ---- selection ------------------------------------------------------ */
void mSelAll(Mesh *m,int v){ for(int i=0;i<m->nF;i++) m->F[i].s=v; }
void mSelNone(Mesh *m){ mSelAll(m,0); }
void mSelInvert(Mesh *m){ for(int i=0;i<m->nF;i++) m->F[i].s=m->F[i].s?0:1; }

static int selCombine(int had,int hit,int mode){
  switch(mode){
  case MSEL_SUB: return (had&&!hit)?1:0;
  case MSEL_ADD: return (had||hit)?1:0;
  case MSEL_AND: return (had&&hit)?1:0;
  default:       return hit?1:0;
  }
}
void mSelBox(Mesh *m,float x0,float y0,float z0,float x1,float y1,float z1,int mode){
  float c[3];
  for(int i=0;i<m->nF;i++){
    mFaceCentre(m,&m->F[i],c);
    int inside = c[0]>=x0&&c[0]<=x1&&c[1]>=y0&&c[1]<=y1&&c[2]>=z0&&c[2]<=z1;
    m->F[i].s=selCombine(m->F[i].s,inside,mode);
  }
}
void mSelNormal(Mesh *m,float nx,float ny,float nz,float tolDeg,int mode){
  float n[3], L=hypot3(nx,ny,nz); if(L<1e-20f) L=1;
  nx/=L; ny/=L; nz/=L;
  float lim=cosf(tolDeg*3.14159265f/180.f);
  for(int i=0;i<m->nF;i++){
    mFaceNormal(m,&m->F[i],n);
    int hit=(n[0]*nx+n[1]*ny+n[2]*nz)>=lim;
    /* the JS only ever passed 'add' or nothing here */
    m->F[i].s = mode==MSEL_ADD ? ((m->F[i].s||hit)?1:0) : (hit?1:0);
  }
}
void mSelGrow(Mesh *m){                    /* spread across shared edges */
  EdgeMap E; buildEdges(m,&E);
  unsigned char *add=(unsigned char*)calloc(m->nF,1);
  for(int i=0;i<E.n;i++){
    Edge *e=&E.e[i];
    if(e->nf<2) continue;
    if(m->F[e->f0].s||m->F[e->f1].s){ add[e->f0]=1; add[e->f1]=1; }
  }
  for(int i=0;i<m->nF;i++) if(add[i]) m->F[i].s=1;
  free(add); emFree(&E);
}
void mSelEvery(Mesh *m,int n,int off){     /* a cheap deterministic scatter */
  for(int i=0;i<m->nF;i++) m->F[i].s=(((i+off)%n)==0)?1:0;
}

/* ---- creases -------------------------------------------------------- */
void mCreaseAngle(Mesh *m,float deg,int keep){
  if(!keep) csClear(m);
  EdgeMap E; buildEdges(m,&E);
  float n0[3],n1[3];
  float lim=cosf(deg*3.14159265f/180.f);
  for(int i=0;i<E.n;i++){
    Edge *e=&E.e[i];
    if(e->nf<2){ csAdd(m,e->k); continue; }      /* borders stay hard */
    mFaceNormal(m,&m->F[e->f0],n0);
    mFaceNormal(m,&m->F[e->f1],n1);
    if(n0[0]*n1[0]+n0[1]*n1[1]+n0[2]*n1[2] < lim) csAdd(m,e->k);
  }
  emFree(&E);
}
void mUncreaseBox(Mesh *m,float x0,float y0,float z0,float x1,float y1,float z1){
  EdgeMap E; buildEdges(m,&E);
  /* rebuild the set without the keys inside the box */
  long long *keep=(long long*)malloc(sizeof(long long)*(m->nC+1));
  int nk=0;
  for(int i=0;i<m->capC;i++){
    long long k=m->C[i]; if(!k) continue;
    Edge *e=emGet(&E,k);
    int drop=0;
    if(e){
      float mx=(m->V[e->a*3]+m->V[e->b*3])/2;
      float my=(m->V[e->a*3+1]+m->V[e->b*3+1])/2;
      float mz=(m->V[e->a*3+2]+m->V[e->b*3+2])/2;
      drop = mx>=x0&&mx<=x1&&my>=y0&&my<=y1&&mz>=z0&&mz<=z1;
    }
    if(!drop) keep[nk++]=k;
  }
  csClear(m);
  for(int i=0;i<nk;i++) csAdd(m,keep[i]);
  free(keep); emFree(&E);
}

/* ---- extrude --------------------------------------------------------
   Region extrude: the whole selection lifts as one skin and only its outer
   boundary grows walls, so adjacent selected faces stay welded. */
void mExtrude(Mesh *m,float dist,float inset){
  int nsel=0;
  for(int i=0;i<m->nF;i++) if(m->F[i].s) nsel++;
  if(!nsel) return;
  int nv=m->nV;
  float *dir=(float*)calloc(nv*3,sizeof(float));
  unsigned char *used=(unsigned char*)calloc(nv,1);
  float n[3],c[3];
  for(int fi=0;fi<m->nF;fi++){
    if(!m->F[fi].s) continue;
    MFace *f=&m->F[fi];
    mFaceNormal(m,f,n);
    for(int i=0;i<f->n;i++){
      int vi=MFV(m,f,i);
      dir[vi*3]+=n[0]; dir[vi*3+1]+=n[1]; dir[vi*3+2]+=n[2]; used[vi]=1;
    }
  }
  int *dup=(int*)malloc(sizeof(int)*nv);
  for(int i=0;i<nv;i++) dup[i]=-1;
  for(int vi=0;vi<nv;vi++){
    if(!used[vi]) continue;
    float L=hypot3(dir[vi*3],dir[vi*3+1],dir[vi*3+2]); if(L<1e-20f) L=1;
    dup[vi]=mAddVert(m, m->V[vi*3]  +dir[vi*3]  /L*dist,
                        m->V[vi*3+1]+dir[vi*3+1]/L*dist,
                        m->V[vi*3+2]+dir[vi*3+2]/L*dist);
  }
  if(inset!=0){                           /* pull the new rim in per face */
    float *pull=(float*)calloc(nv*3,sizeof(float));
    float *cnt =(float*)calloc(nv,sizeof(float));
    for(int fi=0;fi<m->nF;fi++){
      if(!m->F[fi].s) continue;
      MFace *f=&m->F[fi];
      mFaceCentre(m,f,c);
      for(int i=0;i<f->n;i++){
        int vi=MFV(m,f,i), d=dup[vi];
        if(d<0) continue;
        pull[vi*3  ]+=c[0]-m->V[d*3];
        pull[vi*3+1]+=c[1]-m->V[d*3+1];
        pull[vi*3+2]+=c[2]-m->V[d*3+2];
        cnt[vi]++;
      }
    }
    for(int vi=0;vi<nv;vi++){
      if(!cnt[vi]) continue;
      int d=dup[vi]*3; float k=inset/cnt[vi];
      m->V[d  ]+=pull[vi*3  ]*k;
      m->V[d+1]+=pull[vi*3+1]*k;
      m->V[d+2]+=pull[vi*3+2]*k;
    }
    free(pull); free(cnt);
  }
  /* walls on the boundary of the selection only */
  EdgeMap E; buildEdges(m,&E);
  int wcap=E.n, wn=0;
  int (*walls)[4]=(int(*)[4])malloc(sizeof(int)*4*(wcap+1));
  for(int i=0;i<E.n;i++){
    Edge *e=&E.e[i];
    int s0 = e->f0>=0 ? m->F[e->f0].s : 0;
    int s1 = e->nf>1&&e->f1>=0 ? m->F[e->f1].s : 0;
    if(e->nf>1 && s0 && s1) continue;
    if(!s0 && !s1) continue;
    int fi = s0? e->f0 : e->f1;
    MFace *f=&m->F[fi];
    int a=-1,b=-1;
    for(int j=0;j<f->n;j++){
      int p=MFV(m,f,j), q=MFV(m,f,(j+1)%f->n);
      if(ekey(p,q)==e->k){ a=p; b=q; break; }
    }
    if(a<0||dup[a]<0||dup[b]<0) continue;
    if(dist>=0){ walls[wn][0]=a; walls[wn][1]=dup[a]; walls[wn][2]=dup[b]; walls[wn][3]=b; }
    else       { walls[wn][0]=a; walls[wn][1]=b; walls[wn][2]=dup[b]; walls[wn][3]=dup[a]; }
    wn++;
  }
  emFree(&E);
  for(int fi=0;fi<m->nF;fi++){
    if(!m->F[fi].s) continue;
    MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++){
      int vi=MFV(m,f,i);
      if(dup[vi]>=0) m->FI[f->off+i]=dup[vi];
    }
  }
  for(int i=0;i<wn;i++) mAddFace(m,walls[i],4,0);
  free(walls); free(dir); free(used); free(dup);
}

/* Per-face inset: each selected face keeps its outline and grows a border
   ring, leaving a smaller face inside that stays selected.  Extruding that
   inward is a recessed panel; outward is a boss. */
void mInsetFaces(Mesh *m,float t){
  int base=m->nF;
  for(int fi=0;fi<base;fi++){
    if(!m->F[fi].s) continue;
    MFace *f=&m->F[fi];
    int k=f->n;
    int *v=(int*)malloc(sizeof(int)*k);
    for(int i=0;i<k;i++) v[i]=MFV(m,f,i);
    float cx=0,cy=0,cz=0;
    for(int i=0;i<k;i++){ int o=v[i]*3; cx+=m->V[o]; cy+=m->V[o+1]; cz+=m->V[o+2]; }
    cx/=k; cy/=k; cz/=k;
    int *inner=(int*)malloc(sizeof(int)*k);
    for(int i=0;i<k;i++){
      int o=v[i]*3;
      inner[i]=mAddVert(m, m->V[o]  +(cx-m->V[o]  )*t,
                           m->V[o+1]+(cy-m->V[o+1])*t,
                           m->V[o+2]+(cz-m->V[o+2])*t);
    }
    for(int i=0;i<k;i++){
      int q[4]={v[i],v[(i+1)%k],inner[(i+1)%k],inner[i]};
      mAddFace(m,q,4,0);
    }
    /* mAddFace may have reallocated F; take the pointer again */
    f=&m->F[fi];
    for(int i=0;i<k;i++) m->FI[f->off+i]=inner[i];
    free(v); free(inner);
  }
}

/* ---- deformers ------------------------------------------------------ */
void mDeform(Mesh *m,void (*fn)(float*,void*),void *ctx){
  float p[3];
  for(int vi=0;vi<m->nV;vi++){
    p[0]=m->V[vi*3]; p[1]=m->V[vi*3+1]; p[2]=m->V[vi*3+2];
    fn(p,ctx);
    m->V[vi*3]=p[0]; m->V[vi*3+1]=p[1]; m->V[vi*3+2]=p[2];
  }
}
void mTransformSel(Mesh *m,void (*fn)(float*,void*),void *ctx){
  unsigned char *touched=(unsigned char*)calloc(m->nV,1);
  for(int fi=0;fi<m->nF;fi++) if(m->F[fi].s){
    MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++) touched[MFV(m,f,i)]=1;
  }
  float p[3];
  for(int vi=0;vi<m->nV;vi++){
    if(!touched[vi]) continue;
    p[0]=m->V[vi*3]; p[1]=m->V[vi*3+1]; p[2]=m->V[vi*3+2];
    fn(p,ctx);
    m->V[vi*3]=p[0]; m->V[vi*3+1]=p[1]; m->V[vi*3+2]=p[2];
  }
  free(touched);
}
void mScale(Mesh *m,float sx,float sy,float sz){
  for(int i=0;i<m->nV;i++){ m->V[i*3]*=sx; m->V[i*3+1]*=sy; m->V[i*3+2]*=sz; }
}
void mTranslate(Mesh *m,float tx,float ty,float tz){
  for(int i=0;i<m->nV;i++){ m->V[i*3]+=tx; m->V[i*3+1]+=ty; m->V[i*3+2]+=tz; }
}

/* Make the whole surface agree with itself, then point it outward.  Two
   faces sharing an edge are consistent when they walk that edge in opposite
   directions; flood-fill on that rule, then use the signed volume to decide
   which way round the result is.  With this at the end of a recipe, no
   single operator has to get its winding right on its own. */
void mOrient(Mesh *m){
  if(!m->nF) return;
  EdgeMap E; buildEdges(m,&E);
  /* every face that touches an edge, so the flood fill can cross n-gons */
  int *cnt=(int*)calloc(E.n,sizeof(int));
  for(int fi=0;fi<m->nF;fi++){
    MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++){
      Edge *e=emGet(&E,ekey(MFV(m,f,i),MFV(m,f,(i+1)%f->n)));
      cnt[(int)(e-E.e)]++;
    }
  }
  int *off=(int*)malloc(sizeof(int)*(E.n+1));
  off[0]=0;
  for(int i=0;i<E.n;i++) off[i+1]=off[i]+cnt[i];
  int *adj=(int*)malloc(sizeof(int)*(off[E.n]?off[E.n]:1));
  int *fill=(int*)calloc(E.n,sizeof(int));
  for(int fi=0;fi<m->nF;fi++){
    MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++){
      int ei=(int)(emGet(&E,ekey(MFV(m,f,i),MFV(m,f,(i+1)%f->n)))-E.e);
      adj[off[ei]+fill[ei]++]=fi;
    }
  }
  unsigned char *seen=(unsigned char*)calloc(m->nF,1);
  int *stack=(int*)malloc(sizeof(int)*m->nF);
  int sp=0;
  stack[sp++]=0; seen[0]=1;
  while(sp){
    int fi=stack[--sp];
    MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++){
      int a=MFV(m,f,i), b=MFV(m,f,(i+1)%f->n);
      long long k=ekey(a,b);
      int ei=(int)(emGet(&E,k)-E.e);
      int dfi = a<b ? 1 : -1;
      for(int j=off[ei];j<off[ei+1];j++){
        int gj=adj[j];
        if(gj==fi||seen[gj]) continue;
        seen[gj]=1;
        /* how gj walks this edge */
        MFace *g=&m->F[gj];
        int dgj=0;
        for(int q=0;q<g->n;q++){
          int p0=MFV(m,g,q), p1=MFV(m,g,(q+1)%g->n);
          if(ekey(p0,p1)==k){ dgj = p0<p1 ? 1 : -1; break; }
        }
        if(dfi==dgj) faceReverse(m,g);
        stack[sp++]=gj;
      }
    }
  }
  /* signed volume decides inside from outside */
  double vol=0;
  for(int fi=0;fi<m->nF;fi++){
    MFace *f=&m->F[fi];
    for(int i=1;i+1<f->n;i++){
      int a=MFV(m,f,0)*3, b=MFV(m,f,i)*3, c=MFV(m,f,i+1)*3;
      vol+= (double)m->V[a  ]*(m->V[b+1]*m->V[c+2]-m->V[b+2]*m->V[c+1])
          - (double)m->V[a+1]*(m->V[b  ]*m->V[c+2]-m->V[b+2]*m->V[c  ])
          + (double)m->V[a+2]*(m->V[b  ]*m->V[c+1]-m->V[b+1]*m->V[c  ]);
    }
  }
  if(vol<0) for(int fi=0;fi<m->nF;fi++) faceReverse(m,&m->F[fi]);
  free(seen); free(stack); free(cnt); free(off); free(adj); free(fill);
  emFree(&E);
}

/* ---- Catmull-Clark, with creases ------------------------------------ */
static Mesh *ccOnce(Mesh *m){
  int nv=m->nV;
  Mesh *out=mNew();
  int *fp=(int*)malloc(sizeof(int)*(m->nF?m->nF:1));
  float c[3];
  for(int fi=0;fi<m->nF;fi++){
    mFaceCentre(m,&m->F[fi],c);
    fp[fi]=mAddVert(out,c[0],c[1],c[2]);
  }
  EdgeMap E; buildEdges(m,&E);
  for(int i=0;i<E.n;i++){
    Edge *e=&E.e[i];
    int a=e->a*3, b=e->b*3;
    int hard = csHas(m,e->k) || e->nf<2;
    float x,y,z;
    if(hard){
      x=(m->V[a]+m->V[b])/2; y=(m->V[a+1]+m->V[b+1])/2; z=(m->V[a+2]+m->V[b+2])/2;
    } else {
      int f0=fp[e->f0]*3, f1=fp[e->f1]*3;
      x=(m->V[a]+m->V[b]+out->V[f0]+out->V[f1])/4;
      y=(m->V[a+1]+m->V[b+1]+out->V[f0+1]+out->V[f1+1])/4;
      z=(m->V[a+2]+m->V[b+2]+out->V[f0+2]+out->V[f1+2])/4;
    }
    e->pt=mAddVert(out,x,y,z);
  }
  /* per-vertex gathering */
  int *vfCnt=(int*)calloc(nv,sizeof(int));
  int *veCnt=(int*)calloc(nv,sizeof(int));
  for(int fi=0;fi<m->nF;fi++){
    MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++) vfCnt[MFV(m,f,i)]++;
  }
  for(int i=0;i<E.n;i++){
    Edge *e=&E.e[i];
    veCnt[e->a]++; veCnt[e->b]++;
  }
  /* the two accumulators the smooth rule needs, and the crease neighbours */
  float *fAcc=(float*)calloc(nv*3,sizeof(float));
  float *rAcc=(float*)calloc(nv*3,sizeof(float));
  int   *hFar=(int*)malloc(sizeof(int)*nv*2);
  int   *hN  =(int*)calloc(nv,sizeof(int));
  for(int fi=0;fi<m->nF;fi++){
    MFace *f=&m->F[fi];
    int o=fp[fi]*3;
    for(int i=0;i<f->n;i++){
      int vi=MFV(m,f,i);
      fAcc[vi*3]+=out->V[o]; fAcc[vi*3+1]+=out->V[o+1]; fAcc[vi*3+2]+=out->V[o+2];
    }
  }
  for(int i=0;i<E.n;i++){
    Edge *e=&E.e[i];
    float mx=(m->V[e->a*3]  +m->V[e->b*3]  )/2;
    float my=(m->V[e->a*3+1]+m->V[e->b*3+1])/2;
    float mz=(m->V[e->a*3+2]+m->V[e->b*3+2])/2;
    rAcc[e->a*3]+=mx; rAcc[e->a*3+1]+=my; rAcc[e->a*3+2]+=mz;
    rAcc[e->b*3]+=mx; rAcc[e->b*3+1]+=my; rAcc[e->b*3+2]+=mz;
    if(csHas(m,e->k)||e->nf<2){
      if(hN[e->a]<2) hFar[e->a*2+hN[e->a]]=e->b;
      hN[e->a]++;
      if(hN[e->b]<2) hFar[e->b*2+hN[e->b]]=e->a;
      hN[e->b]++;
    }
  }
  int *vp=(int*)malloc(sizeof(int)*(nv?nv:1));
  for(int vi=0;vi<nv;vi++){
    float P[3]={m->V[vi*3],m->V[vi*3+1],m->V[vi*3+2]};
    float x,y,z;
    if(hN[vi]>2||vfCnt[vi]==0){              /* corner: pinned */
      x=P[0]; y=P[1]; z=P[2];
    } else if(hN[vi]==2){                    /* along a crease */
      int f0=hFar[vi*2], f1=hFar[vi*2+1];
      x=(P[0]*6+m->V[f0*3]  +m->V[f1*3]  )/8;
      y=(P[1]*6+m->V[f0*3+1]+m->V[f1*3+1])/8;
      z=(P[2]*6+m->V[f0*3+2]+m->V[f1*3+2])/8;
    } else {                                 /* smooth interior */
      int n=vfCnt[vi], ne=veCnt[vi];
      float fx=fAcc[vi*3]/n, fy=fAcc[vi*3+1]/n, fz=fAcc[vi*3+2]/n;
      float rx=rAcc[vi*3]/ne, ry=rAcc[vi*3+1]/ne, rz=rAcc[vi*3+2]/ne;
      x=(fx+2*rx+(n-3)*P[0])/n;
      y=(fy+2*ry+(n-3)*P[1])/n;
      z=(fz+2*rz+(n-3)*P[2])/n;
    }
    vp[vi]=mAddVert(out,x,y,z);
  }
  /* rebuild: one quad per corner */
  for(int fi=0;fi<m->nF;fi++){
    MFace *f=&m->F[fi];
    int k=f->n, sel=f->s;
    for(int i=0;i<k;i++){
      int prev=MFV(m,f,(i+k-1)%k), cur=MFV(m,f,i), next=MFV(m,f,(i+1)%k);
      int q[4]={ vp[cur],
                 emGet(&E,ekey(cur,next))->pt,
                 fp[fi],
                 emGet(&E,ekey(prev,cur))->pt };
      mAddFace(out,q,4,sel);
    }
  }
  /* a creased edge stays creased in both halves */
  for(int i=0;i<E.n;i++){
    Edge *e=&E.e[i];
    if(!(csHas(m,e->k)||e->nf<2)) continue;
    csAdd(out,ekey(vp[e->a],e->pt));
    csAdd(out,ekey(e->pt,vp[e->b]));
  }
  free(fp); free(vfCnt); free(veCnt);
  free(fAcc); free(rAcc); free(hFar); free(hN); free(vp);
  emFree(&E);
  mFree(m);
  return out;
}
Mesh *mSubdivide(Mesh *m,int times){
  for(int i=0;i<(times?times:1);i++) m=ccOnce(m);
  return m;
}
/* Raise resolution without moving anything: with every edge creased, the
   subdivision rules collapse to midpoints and centroids, so this is a pure
   quad split.  Region selection needs it before it can pick out a grip or a
   panel, because a selection works on whole faces. */
Mesh *mSplit(Mesh *m,int times){
  for(int it=0;it<(times?times:1);it++){
    int nkeep=m->nC;
    long long *keep=(long long*)malloc(sizeof(long long)*(nkeep+1));
    int nk=0;
    for(int i=0;i<m->capC;i++) if(m->C[i]) keep[nk++]=m->C[i];
    EdgeMap E; buildEdges(m,&E);
    for(int i=0;i<E.n;i++) csAdd(m,E.e[i].k);
    emFree(&E);
    m=ccOnce(m);
    csClear(m);
    /* put back only the creases the caller had asked for -- approximate,
       but recipes crease by angle afterwards anyway */
    for(int i=0;i<nk;i++) csAdd(m,keep[i]);
    free(keep);
  }
  return m;
}

/* ---- output --------------------------------------------------------- */
void mBounds(const Mesh *m,float *b){
  if(!m->nV){ b[0]=b[1]=b[2]=0; b[3]=b[4]=b[5]=1; return; }
  b[0]=b[1]=b[2]=1e9f; b[3]=b[4]=b[5]=-1e9f;
  for(int i=0;i<m->nV;i++) for(int k=0;k<3;k++){
    float v=m->V[i*3+k];
    if(v<b[k]) b[k]=v;
    if(v>b[k+3]) b[k+3]=v;
  }
}
static void bounds(const Mesh *m,float *lo,float *hi){
  lo[0]=lo[1]=lo[2]=1e9f; hi[0]=hi[1]=hi[2]=-1e9f;
  for(int i=0;i<m->nV;i++) for(int k=0;k<3;k++){
    float v=m->V[i*3+k];
    if(v<lo[k]) lo[k]=v;
    if(v>hi[k]) hi[k]=v;
  }
}
void mFitUniform(Mesh *m){
  float lo[3],hi[3]; bounds(m,lo,hi);
  float e=hi[0]-lo[0];
  if(hi[1]-lo[1]>e) e=hi[1]-lo[1];
  if(hi[2]-lo[2]>e) e=hi[2]-lo[2];
  if(e<1e-6f) e=1e-6f;
  float k=1.f/e;
  for(int i=0;i<m->nV;i++) for(int c=0;c<3;c++)
    m->V[i*3+c]=(m->V[i*3+c]-(lo[c]+hi[c])/2)*k;
}
void mFitBox(Mesh *m,float pad){
  float lo[3],hi[3]; bounds(m,lo,hi);
  float k=1.f-pad;
  float s[3];
  for(int c=0;c<3;c++){
    float e=hi[c]-lo[c]; if(e<1e-6f) e=1e-6f;
    s[c]=k/e;
  }
  for(int i=0;i<m->nV;i++) for(int c=0;c<3;c++)
    m->V[i*3+c]=(m->V[i*3+c]-(lo[c]+hi[c])/2)*s[c];
}

/* triangulate and shade: a corner takes the average of the faces around it
   that lie within `angle`, so a subdivided surface is smooth but a creased
   ridge still catches a hard highlight */
void mEmit(const Mesh *m,float angleDeg,float **out,int *n,int *cap){
  int nv=m->nV;
  float *fn=(float*)malloc(sizeof(float)*(m->nF?m->nF:1)*3);
  for(int fi=0;fi<m->nF;fi++) mFaceNormal(m,&m->F[fi],fn+fi*3);

  int *vfCnt=(int*)calloc(nv+1,sizeof(int));
  for(int fi=0;fi<m->nF;fi++){
    const MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++) vfCnt[MFV(m,f,i)]++;
  }
  int *voff=(int*)malloc(sizeof(int)*(nv+1));
  voff[0]=0;
  for(int i=0;i<nv;i++) voff[i+1]=voff[i]+vfCnt[i];
  int *vadj=(int*)malloc(sizeof(int)*(voff[nv]?voff[nv]:1));
  int *fill=(int*)calloc(nv,sizeof(int));
  for(int fi=0;fi<m->nF;fi++){
    const MFace *f=&m->F[fi];
    for(int i=0;i<f->n;i++){ int vi=MFV(m,f,i); vadj[voff[vi]+fill[vi]++]=fi; }
  }
  float lim=cosf(angleDeg*3.14159265f/180.f);

  #define GROW(k) do{ if(*n+(k)>*cap){ while(*cap<*n+(k)) *cap=*cap?*cap*2:4096; \
                      *out=(float*)realloc(*out,sizeof(float)*(*cap)); } }while(0)

  for(int fi=0;fi<m->nF;fi++){
    const MFace *f=&m->F[fi];
    int k=f->n;
    /* the 0-2 diagonal is wrong for a concave quad; 1-3 is the other choice */
    int tri[16][3]; int nt=0;
    if(k==4){
      int v0=MFV(m,f,0),v1=MFV(m,f,1),v2=MFV(m,f,2),v3=MFV(m,f,3);
      int d02 = meshTriOK(m,fn,v0,v1,v2,fi)&&meshTriOK(m,fn,v0,v2,v3,fi);
      if(d02){ tri[nt][0]=v0;tri[nt][1]=v1;tri[nt][2]=v2;nt++;
               tri[nt][0]=v0;tri[nt][1]=v2;tri[nt][2]=v3;nt++; }
      else   { tri[nt][0]=v1;tri[nt][1]=v2;tri[nt][2]=v3;nt++;
               tri[nt][0]=v1;tri[nt][1]=v3;tri[nt][2]=v0;nt++; }
    } else {
      for(int i=1;i+1<k && nt<16;i++){
        tri[nt][0]=MFV(m,f,0); tri[nt][1]=MFV(m,f,i); tri[nt][2]=MFV(m,f,i+1); nt++;
      }
    }
    for(int t=0;t<nt;t++){
      int a=tri[t][0],b=tri[t][1],c=tri[t][2];
      if(!meshTriOK(m,fn,a,b,c,fi)){ int q=b; b=c; c=q; }
      int vs[3]={a,b,c};
      GROW(18);
      for(int q=0;q<3;q++){
        int vi=vs[q];
        float x=0,y=0,z=0;
        float ax=fn[fi*3],ay=fn[fi*3+1],az=fn[fi*3+2];
        for(int j=voff[vi];j<voff[vi+1];j++){
          int g=vadj[j];
          float bx=fn[g*3],by=fn[g*3+1],bz=fn[g*3+2];
          if(ax*bx+ay*by+az*bz>=lim){ x+=bx; y+=by; z+=bz; }
        }
        float L=hypot3(x,y,z);
        if(L<1e-9f){ x=ax; y=ay; z=az; } else { x/=L; y/=L; z/=L; }
        float *o=*out+*n; *n+=6;
        o[0]=m->V[vi*3]; o[1]=m->V[vi*3+1]; o[2]=m->V[vi*3+2];
        o[3]=x; o[4]=y; o[5]=z;
      }
    }
  }
  #undef GROW
  free(fn); free(vfCnt); free(voff); free(vadj); free(fill);
}

static int meshTriOK(const Mesh *m,const float *fn,int a,int b,int c,int fi){
  float e1x=m->V[b*3]-m->V[a*3], e1y=m->V[b*3+1]-m->V[a*3+1], e1z=m->V[b*3+2]-m->V[a*3+2];
  float e2x=m->V[c*3]-m->V[a*3], e2y=m->V[c*3+1]-m->V[a*3+1], e2z=m->V[c*3+2]-m->V[a*3+2];
  float cx=e1y*e2z-e1z*e2y, cy=e1z*e2x-e1x*e2z, cz=e1x*e2y-e1y*e2x;
  return cx*fn[fi*3]+cy*fn[fi*3+1]+cz*fn[fi*3+2] > 0;
}
