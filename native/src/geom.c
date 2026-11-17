/* ============================================================
   6.  GEOMETRY  --  pos(3) normal(3) uv(2) tex(1) ao(1)
                     lightmap(3x3) style(3) cubemap(1)

   Every brush face is a parallelogram, so one routine meshes
   brush sides, the generated cut faces and nothing else.  The
   face is diced at AO_STEP because the lightmap lives PER
   VERTEX, not in a texture: a 3.5 m wall with four corners
   would be four samples of it.

   THE LATTICE IS WALKED ONCE, NOT PER TRIANGLE.  The browser
   asked the bake for every corner of every quad and leaned on a
   hash map to collapse the four-fold repeat.  Here each face
   plans its (n1+1)x(n2+1) grid up front, the whole level's worth
   of corners is baked in one parallel pass, and only then are
   triangles emitted against them.  Same samples, no map, and the
   bake becomes trivially threadable.

   THE UV FRAME COMES FROM THE NORMAL, NOT FROM THE AXES.
   Projecting a part from world axes flips wherever a smooth
   normal crosses 45 degrees, which is what tore the rings apart;
   parts arrive already unwrapped and carry their own u,w.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include <stdlib.h>
#include <string.h>

void bakeSamples(const float *pts,const float *nrms,const int *bis,
                 BakeSample *out,int n);

float *MESHV; int nMeshVert;
int MESH_SPLIT[2];

void uvFrame(const float *n,float *T,float *B){
  if(fabsf(n[1])>0.999f){ T[0]=1; T[1]=0; T[2]=0; }
  else { float L=hypot2(n[2],n[0]); T[0]=n[2]/L; T[1]=0; T[2]=-n[0]/L; }
  B[0]=n[1]*T[2]-n[2]*T[1];
  B[1]=n[2]*T[0]-n[0]*T[2];
  B[2]=n[0]*T[1]-n[1]*T[0];
}

/* ---- a planned face ------------------------------------------------- */
typedef struct {
  float c0[3],e1[3],e2[3],n[3];
  float uo,wo;
  int   t,bi,n1,n2,base;          /* base: first sample index */
} Patch;

static Patch *PA; static int nPa, capPa;
static Patch *patchNew(void){
  if(nPa==capPa){ capPa=capPa?capPa*2:8192;
                  PA=(Patch*)realloc(PA,sizeof(Patch)*capPa); }
  return &PA[nPa++];
}

static void plan(const float c[4][3],const float *n,int t,int bi,
                 float uo,float wo,float aoStep){
  Patch *P=patchNew();
  for(int k=0;k<3;k++){
    P->c0[k]=c[0][k];
    P->e1[k]=c[1][k]-c[0][k];
    P->e2[k]=c[3][k]-c[0][k];
    P->n[k]=n[k];
  }
  int n1=(int)(hypot3(P->e1[0],P->e1[1],P->e1[2])/aoStep+0.5f);
  int n2=(int)(hypot3(P->e2[0],P->e2[1],P->e2[2])/aoStep+0.5f);
  P->n1 = n1<1?1:(n1>24?24:n1);
  P->n2 = n2<1?1:(n2>24?24:n2);
  P->t=t; P->bi=bi; P->uo=uo; P->wo=wo;
}

/* ---- the growing vertex buffer -------------------------------------- */
static float *V; static int nV, capV;
static void put(const float *p,const float *n,float u,float w,float t,
                const BakeSample *L,float cube){
  if(nV+VSTRIDE>capV){
    while(capV<nV+VSTRIDE) capV = capV? capV*2 : (1<<22);
    V=(float*)realloc(V,sizeof(float)*capV);
  }
  float *o=V+nV; nV+=VSTRIDE;
  o[0]=p[0]; o[1]=p[1]; o[2]=p[2];
  o[3]=n[0]; o[4]=n[1]; o[5]=n[2];
  o[6]=u; o[7]=w; o[8]=t; o[9]=L->ao;
  for(int k=0;k<9;k++) o[10+k]=L->lm[k];
  o[19]=L->sty[0]; o[20]=L->sty[1]; o[21]=L->sty[2];
  o[22]=cube;
  o[23]=L->styG;              /* which bank, and whether it owns the glow */
}

void buildMesh(void){
  double t0=platTime();
  float aoStep = FASTLIGHT ? AO_STEP*3.f : AO_STEP;
  nV=0; nPa=0;

  /* ---- 1. plan every face -------------------------------------------- */
  for(int bi=0;bi<nBrush;bi++){
    Brush *b=&BRUSHES[bi];
    if(b->clip) continue;                /* clip only: it stops you, undrawn */
    float x0=b->x0,y0=b->y0,z0=b->z0,x1=b->x1,y1=b->y1,z1=b->z1;
    static const float N[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    float C[6][4][3]={
      {{x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1}},
      {{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}},
      {{x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0}},
      {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}},
      {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}},
      {{x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0}}};
    for(int f=0;f<6;f++) plan(C[f],N[f],b->t,bi,0,0,aoStep);
  }
  /* the generated faces: chamfers, corner cuts, sloped soffits */
  for(int fi=0;fi<nFace;fi++){
    Face *f=&FACES[fi];
    float ax=f->c[1][0]-f->c[0][0], ay=f->c[1][1]-f->c[0][1], az=f->c[1][2]-f->c[0][2];
    float bx=f->c[3][0]-f->c[0][0], by=f->c[3][1]-f->c[0][1], bz=f->c[3][2]-f->c[0][2];
    float n[3]={ay*bz-az*by, az*bx-ax*bz, ax*by-ay*bx};
    float L=hypot3(n[0],n[1],n[2]); if(L<1e-9f) L=1;
    n[0]/=L; n[1]/=L; n[2]/=L;
    plan(f->c,n,f->t,-1,f->uo,f->wo,aoStep);
  }

  /* ---- 2. lay out the sample list ------------------------------------ */
  int nS=0;
  for(int i=0;i<nPa;i++){
    PA[i].base=nS;
    nS += (PA[i].n1+1)*(PA[i].n2+1);
  }
  int kitBase=nS;

  /* ---- the kit's vertices, deduplicated ------------------------------
     A PART VERTEX IS SHARED BY FOUR TRIANGLES AND IS ONE SAMPLE.  mEmit
     hands back a flat vertex stream, so a quad-meshed part sends the same
     corner -- same position, same averaged normal -- three or four times,
     and baking each copy costs twelve shadow rays to reproduce a number
     already known.  The browser collapsed this with its hash map keyed on
     position-to-2dp, normal and brush; here it is the same key, applied
     only where the repeats actually are.  2.18 M vertices become 0.6 M
     samples, and the bake divides by the same factor. */
  int *kitMap=(int*)malloc(sizeof(int)*(size_t)(nTriVert?nTriVert:1));
  int nKitS=0;
  {
    int cap=64; while(cap < nTriVert*2) cap*=2;
    int *slot=(int*)malloc(sizeof(int)*cap);
    for(int i=0;i<cap;i++) slot[i]=-1;
    int *key=(int*)malloc(sizeof(int)*6*(size_t)(nTriVert?nTriVert:1));
    for(int i=0;i<nTriVert;i++){
      const float *v=TRIS+(size_t)i*9;
      int q[6]={ (int)lrintf(v[0]*100), (int)lrintf(v[1]*100), (int)lrintf(v[2]*100),
                 (int)lrintf(v[3]*1000),(int)lrintf(v[4]*1000),(int)lrintf(v[5]*1000) };
      unsigned h=2166136261u;
      for(int k=0;k<6;k++){ h^=(unsigned)q[k]; h*=16777619u; }
      h&=(unsigned)(cap-1);
      int found=-1;
      while(slot[h]>=0){
        const int *o=key+slot[h]*6;
        if(o[0]==q[0]&&o[1]==q[1]&&o[2]==q[2]&&
           o[3]==q[3]&&o[4]==q[4]&&o[5]==q[5]){ found=slot[h]; break; }
        h=(h+1)&(unsigned)(cap-1);
      }
      if(found<0){
        found=nKitS++;
        for(int k=0;k<6;k++) key[found*6+k]=q[k];
        slot[h]=found;
      }
      kitMap[i]=found;
    }
    free(slot); free(key);
  }
  nS += nKitS;

  float *sp=(float*)malloc(sizeof(float)*(size_t)nS*3);
  float *sn=(float*)malloc(sizeof(float)*(size_t)nS*3);
  int   *sb=(int*)  malloc(sizeof(int)  *(size_t)nS);
  for(int i=0;i<nPa;i++){
    Patch *P=&PA[i];
    int w=P->base;
    for(int a=0;a<=P->n1;a++) for(int b2=0;b2<=P->n2;b2++){
      float fa=(float)a/P->n1, fb=(float)b2/P->n2;
      for(int k=0;k<3;k++){
        sp[w*3+k]=P->c0[k]+P->e1[k]*fa+P->e2[k]*fb;
        sn[w*3+k]=P->n[k];
      }
      sb[w]=P->bi;
      w++;
    }
  }
  for(int i=0;i<nTriVert;i++){
    const float *s=TRIS+(size_t)i*9;
    int w=kitBase+kitMap[i];
    for(int k=0;k<3;k++){ sp[w*3+k]=s[k]; sn[w*3+k]=s[3+k]; }
    sb[w]=-1;
  }

  /* ---- 3. one parallel bake over the lot ----------------------------- */
  BakeSample *BS=(BakeSample*)malloc(sizeof(BakeSample)*(size_t)nS);
  bakeSamples(sp,sn,sb,BS,nS);

  { /* a bake that lights nothing looks exactly like a bake that ran; the
       only way to tell them apart is to print what came back */
    double sum=0, aos=0; float mx=0; int lit=0;
    for(int i=0;i<nS;i++){
      float m=0;
      for(int k=0;k<9;k++){ float v=BS[i].lm[k]; sum+=v; if(v>m) m=v; }
      if(m>0.02f) lit++;
      if(m>mx) mx=m;
      aos+=BS[i].ao;
    }
    platLog("bake   lm mean %.4f  max %.3f  lit %d/%d (%.1f%%)  ao mean %.3f",
            sum/(nS*9.0), mx, lit, nS, 100.0*lit/nS, aos/nS);
    platLog("bake   %d part verts -> %d samples (%.1fx)",
            nTriVert, nKitS, nTriVert/(double)(nKitS?nKitS:1));
  }

  /* ---- 4. emit ------------------------------------------------------- */
  for(int i=0;i<nPa;i++){
    Patch *P=&PA[i];
    float T[3],B[3]; uvFrame(P->n,T,B);
    int st=P->n2+1;
    for(int a=0;a<P->n1;a++) for(int b2=0;b2<P->n2;b2++){
      int idx[4]={ P->base+ a   *st+b2,
                   P->base+(a+1)*st+b2,
                   P->base+(a+1)*st+b2+1,
                   P->base+ a   *st+b2+1 };
      static const int order[6]={0,1,2,0,2,3};
      for(int oi=0;oi<6;oi++){
        int s=idx[order[oi]];
        const float *p=sp+s*3;
        float u=(p[0]*T[0]+p[1]*T[1]+p[2]*T[2]+P->uo)*TEX_SCALE;
        float w=(p[0]*B[0]+p[1]*B[1]+p[2]*B[2]+P->wo)*TEX_SCALE;
        put(p,P->n,u,w,(float)P->t,&BS[s],(float)zoneAt(p[0],p[2]));
      }
    }
  }
  for(int i=0;i<nTriVert;i++){
    const float *s=TRIS+(size_t)i*9;
    put(s,s+3,s[6]*TEX_SCALE,s[7]*TEX_SCALE,s[8],&BS[kitBase+kitMap[i]],
        (float)zoneAt(s[0],s[2]));
  }

  free(sp); free(sn); free(sb); free(BS); free(kitMap);

  /* ---- 5. sorted, so the blended surfaces become draw RANGES ---------
     Three passes over the whole level buffer would cost three times the
     vertex work for a couple of hundred triangles of glass.  A surface class
     never varies within a triangle, so one stable partition here -- room,
     then sprite, then glass -- makes it three draws over one buffer, in the
     order blending needs. */
  int nvert=nV/VSTRIDE, ntri=nvert/3;
  unsigned char *bin=(unsigned char*)malloc((size_t)ntri);
  int cnt[3]={0,0,0};
  for(int tr=0;tr<ntri;tr++){
    int q=MAT_SURF[(int)(V[(size_t)tr*3*VSTRIDE+8]+0.5f)];
    int c = q==SURF_GLASS?2 : (q==SURF_SPRITE?1:0);
    bin[tr]=(unsigned char)c; cnt[c]++;
  }
  free(MESHV);
  MESHV=(float*)malloc(sizeof(float)*(size_t)nV);
  int w[3]={0,cnt[0],cnt[0]+cnt[1]};
  for(int tr=0;tr<ntri;tr++){
    int c=bin[tr];
    memcpy(MESHV+(size_t)w[c]*3*VSTRIDE, V+(size_t)tr*3*VSTRIDE,
           sizeof(float)*3*VSTRIDE);
    w[c]++;
  }
  free(bin);
  MESH_SPLIT[0]=cnt[0]*3;
  MESH_SPLIT[1]=(cnt[0]+cnt[1])*3;
  nMeshVert=nvert;

  platLog("mesh   %d tris  %.1f MB  (%d room / %d sprite / %d glass)  %.0f ms",
          ntri, nV*4.0/1048576.0, cnt[0],cnt[1],cnt[2],
          (platTime()-t0)*1000.0);
}
