/* ============================================================
   5.  RADIOSITY BAKE  --  VRAD, at load, for zero bytes.

   Static light is shadow-traced against the brush set, then
   bounced once off whatever it lands on, carrying that surface's
   colour with it.  The answer is stored in Valve's three-vector
   basis, so the per-pixel normal map still steers the baked
   light: radiosity normal mapping, the thing that made Source
   rooms look like rooms.

   THE CACHE IS GONE, AND NOTHING WAS LOST.  The browser keyed
   every sample by position, normal AND brush index -- so two
   brushes never shared one, and the only hits were the corners
   a face shares with itself.  geom.c now walks each face's
   lattice once and hands the corners over as a flat array, which
   removes the same redundancy with no hash map, and leaves the
   whole bake as one embarrassingly parallel loop.  16 threads
   took the browser's 95 seconds to under two.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include <stdlib.h>
#include <string.h>

float *CUBES; int CUBE_COUNT;

/* ?fast trades the radiosity solution for a rough one: a third of the
   hemisphere rays, a third of them gathering bounce instead of half, and one
   cubemap pass instead of three.  Sample count is an area, so the wider
   lightmap spacing in geom.c is the bulk of it.  No other code path changes,
   so a fast load and a full load are the same build. */
static int AO_RAYS;
#define AO_LEN      2.2f      /* contact-shadow reach */
#define GATHER_LEN  15.0f     /* how far a bounce is allowed to travel */
#define BOUNCE      0.66f     /* reflectivity scale, VRAD's -bounce in one number */
static int GATHER_STEP;

static float AO_DIRS[16][3];

/* Valve's Half-Life 2 basis: three unit vectors 54.7 deg off the normal */
static const float S6=0.40824829f, S2=0.70710678f, S3=0.57735027f;
static float HL2[3][3];
/* recombining the three costs a factor of sqrt3; fold it in so a light
   straight down the normal still reads as exactly N.L */
#define BASIS_GAIN 1.7320508f

/* ---- the brush grid a ray walks ------------------------------------ */
#define GCELL 4.0f
static float ggx0,ggy0,ggz0;
static int   ggnx,ggny,ggnz;
static int  *cellHead, *cellNext;      /* cellNext holds (brush,next) pairs */

static int cix(float v){ int i=(int)((v-ggx0)/GCELL); return i<0?0:(i>=ggnx?ggnx-1:i); }
static int ciy(float v){ int i=(int)((v-ggy0)/GCELL); return i<0?0:(i>=ggny?ggny-1:i); }
static int ciz(float v){ int i=(int)((v-ggz0)/GCELL); return i<0?0:(i>=ggnz?ggnz-1:i); }

static void gridBuild(void){
  float x0=1e9f,y0=1e9f,z0=1e9f,x1=-1e9f,y1=-1e9f,z1=-1e9f;
  for(int i=0;i<nBrush;i++){
    Brush *b=&BRUSHES[i];
    if(b->x0<x0)x0=b->x0; if(b->y0<y0)y0=b->y0; if(b->z0<z0)z0=b->z0;
    if(b->x1>x1)x1=b->x1; if(b->y1>y1)y1=b->y1; if(b->z1>z1)z1=b->z1;
  }
  ggx0=x0-1; ggy0=y0-1; ggz0=z0-1;
  ggnx=(int)((x1-ggx0+2)/GCELL)+1; if(ggnx<1)ggnx=1;
  ggny=(int)((y1-ggy0+2)/GCELL)+1; if(ggny<1)ggny=1;
  ggnz=(int)((z1-ggz0+2)/GCELL)+1; if(ggnz<1)ggnz=1;

  int nc=ggnx*ggny*ggnz;
  free(cellHead); free(cellNext);
  cellHead=(int*)malloc(sizeof(int)*nc);
  for(int i=0;i<nc;i++) cellHead[i]=-1;
  long long total=0;
  for(int b=0;b<nBrush;b++){
    Brush *B=&BRUSHES[b];
    total += (long long)(ciz(B->z1)-ciz(B->z0)+1)*(ciy(B->y1)-ciy(B->y0)+1)
                       *(cix(B->x1)-cix(B->x0)+1);
  }
  cellNext=(int*)malloc(sizeof(int)*(size_t)total*2);
  int w=0;
  for(int b=0;b<nBrush;b++){
    Brush *B=&BRUSHES[b];
    for(int z=ciz(B->z0);z<=ciz(B->z1);z++)
      for(int y=ciy(B->y0);y<=ciy(B->y1);y++)
        for(int x=cix(B->x0);x<=cix(B->x1);x++){
          int c=(z*ggny+y)*ggnx+x;
          cellNext[w*2]=b; cellNext[w*2+1]=cellHead[c]; cellHead[c]=w; w++;
        }
  }
  platLog("bake   grid %dx%dx%d  %lld refs",ggnx,ggny,ggnz,total);
}

/* ---- the tracer ----------------------------------------------------- */
/* Per-thread scratch: the stamp array that stops a brush spanning four cells
   from being tested four times.  Shared state here would be a data race and
   a correctness bug, not just a slow path. */
typedef struct { int *stamp; int mark; float hitT; int hitAx, hitSg; } Tracer;

static float traceBrush(const Brush *B,float ox,float oy,float oz,
                        float dx,float dy,float dz,float maxT,
                        int *outAx,int *outSg){
  float t0=0.02f, t1=maxT; int ax=2, sg=1;
  const float lo[3]={B->x0,B->y0,B->z0}, hi[3]={B->x1,B->y1,B->z1};
  const float o[3]={ox,oy,oz}, d[3]={dx,dy,dz};
  for(int a=0;a<3;a++){
    if(fabsf(d[a])<1e-9f){ if(o[a]<lo[a]||o[a]>hi[a]) return -1; continue; }
    float nr=(lo[a]-o[a])/d[a], fr=(hi[a]-o[a])/d[a];
    int sN = d[a]>0 ? -1 : 1;
    if(nr>fr){ float t=nr; nr=fr; fr=t; }
    if(nr>t0){ t0=nr; ax=a; sg=sN; }
    if(fr<t1) t1=fr;
    if(t0>t1) return -1;
  }
  *outAx=ax; *outSg=sg;
  return t0;
}

/* nearest hit along the ray; -1 for a clear line */
static int rayTrace(Tracer *T,float ox,float oy,float oz,
                    float dx,float dy,float dz,float maxT,int skip){
  float ex=ox+dx*maxT, ey=oy+dy*maxT, ez=oz+dz*maxT;
  int X0=cix(ox<ex?ox:ex), X1=cix(ox>ex?ox:ex);
  int Y0=ciy(oy<ey?oy:ey), Y1=ciy(oy>ey?oy:ey);
  int Z0=ciz(oz<ez?oz:ez), Z1=ciz(oz>ez?oz:ez);
  T->mark++;
  float bt=maxT; int bb=-1, bax=2, bsg=1;
  for(int z=Z0;z<=Z1;z++) for(int y=Y0;y<=Y1;y++) for(int x=X0;x<=X1;x++){
    for(int n=cellHead[(z*ggny+y)*ggnx+x]; n>=0; n=cellNext[n*2+1]){
      int b=cellNext[n*2];
      if(b==skip||T->stamp[b]==T->mark) continue;
      T->stamp[b]=T->mark;
      int ax,sg;
      float t=traceBrush(&BRUSHES[b],ox,oy,oz,dx,dy,dz,bt,&ax,&sg);
      if(t>=0){ bt=t; bb=b; bax=ax; bsg=sg; }
    }
  }
  T->hitT=bt; T->hitAx=bax; T->hitSg=bsg;
  return bb;
}
static int blocked(Tracer *T,float ox,float oy,float oz,
                   float dx,float dy,float dz,float maxT,int skip){
  return rayTrace(T,ox,oy,oz,dx,dy,dz,maxT,skip)>=0;
}

/* ============================================================
   WHICH LAMPS THE SHIP IS ABOUT TO LOSE

   An earlier version turned the whole frame red from sink stage one, as a
   post-process grade -- luminance kept, hue driven to red.  It
   was cheap and it read as a filter over a photograph rather
   than as a ship losing power, which is what it is.

   What actually happens to a hull with three of six cores gone
   is that BANKS GO OUT.  So a share of the 257 lamps are
   assigned, at bake time, to one of four switchable groups, and
   their light is baked into the style channel instead of the
   lightmap.  Turning a group off then costs one uniform and
   takes real light out of real rooms -- the corridor goes dark
   because its lamps went dark, and the two of them that are on
   the flicker group were already stuttering before anything
   went wrong.

     0  the first bank to go      -- out at sink 1
     1  the second               -- out at sink 2
     2  the alarm bank           -- white until sink 2, then a
                                    red blink, which is the one
                                    thing left saying the ship
                                    still has an opinion
   3-5  the broken fixtures      -- stuttering from the first
                                    frame of the game, because a
                                    derelict has them already, and
                                    on THREE separate clocks so the
                                    ship flickers in places rather
                                    than blinking as one room

   The split is by hash of the lamp's own index and position, so
   it is stable across runs and does not care what order the
   plan lists them in.  53% stay steady: past about half, the
   ship stops being dark and starts being unplayable. */
float STY_C[STY_N*3]={1,1,1, 1,1,1, 1,1,1, 1,1,1, 1,1,1, 1,1,1, 1,1,1};
int lampStyle(int i){
  if(i<0||i>=nLight) return -1;
  const Light *L=&LIGHTS[i];
  /* the position is in the hash so two lamps the plan happens to list
     next to each other do not land in the same bank and blink together */
  float h=ihash(i*7+1,(int)(L->p[0]*3.f),(int)(L->p[2]*3.f));
  /* AND THE ALARM BANK IS A REAL BANK NOW.  Bank 2 was four per cent of
     the styled lamps -- about ten fixtures in a ship of 257 -- so the one
     red thing the sink brings was, in most rooms, nothing at all, while
     L-025 promised "전 구역 적색 조명".  Ten per cent is roughly
     twenty-six beacons: one in most compartments and two down a long
     spine, which is what a real alarm circuit looks like.  The six points
     come out of the UNSTYLED half rather than off banks 0/1 or the broken
     ones, so nothing else in the ship changes: the two that go dark with
     the sink keep 17 and 13 per cent, and the three broken banks keep
     their sixteen. */
  /* AND A FIFTH OF THE SHIP IS ON THE LAST BREAKER.  Bank 6 burns for the
     whole game and goes out at sink 3, which is the ninety seconds and
     nothing else.  It is taken out of the steady share -- 44 to 22 per
     cent -- because a lamp that never changes is the one thing a dark
     scene cannot argue with: grading the ambient floor down does not
     touch a ceiling strip, and it was the strips that kept the gallery
     looking like a lit room during its own last stand. */
  if(h<0.22f) return -1;
  if(h<0.44f) return 6;
  if(h<0.61f) return 0;
  if(h<0.74f) return 1;
  if(h<0.84f) return 2;
  /* and the broken ones, split three ways.  16% of 257 lamps is about
     forty-two fixtures stuttering somewhere, against the seven per cent
     that was one bank of eighteen going at once.  It was 13% and a
     derelict wants to have more than that wrong with it -- but the ceiling
     is real: past about half the ship the flicker stops being a place you
     are walking through and starts being a strobe. */
  if(h<0.895f) return 3;
  if(h<0.950f) return 4;
  return 5;
}

/* irradiance at a point from the static lamps, shadowed against the level */
static void directAt(Tracer *T,float px,float py,float pz,
                     float nx,float ny,float nz,int skip,float *out){
  out[0]=out[1]=out[2]=0;
  for(int i=0;i<nLight;i++){
    Light *L=&LIGHTS[i];
    float dx=L->p[0]-px, dy=L->p[1]-py, dz=L->p[2]-pz;
    float dist=hypot3(dx,dy,dz);
    if(dist>=L->r||dist<1e-4f) continue;
    float inv=1.f/dist; dx*=inv; dy*=inv; dz*=inv;
    float ndl=nx*dx+ny*dy+nz*dz;
    if(ndl<=0) continue;
    float att=1.f-dist/L->r; att*=att;
    float k=ndl*att;
    if(k<0.003f) continue;
    if(blocked(T,px,py,pz,dx,dy,dz,dist-0.06f,skip)) continue;
    /* THE BOUNCE IS BAKED ONCE AND CANNOT SWITCH.  A styled lamp still
       lights this gather at full strength; what the style takes away is
       its DIRECT term, which is the one the eye reads. */
    out[0]+=L->c[0]*k; out[1]+=L->c[1]*k; out[2]+=L->c[2]*k;
  }
}

/* Valve's ambient cube, evaluated the way Source lights props: square the
   normal and take the three faces it points at. */
static int cubesReady=0;
static void cubeAt(int zi,float nx,float ny,float nz,float *out){
  const float *C=CUBES+(size_t)zi*18;
  float sx=nx*nx, sy=ny*ny, sz=nz*nz;
  const float *ix=C+(nx>0?0:3), *iy=C+(ny>0?6:9), *iz=C+(nz>0?12:15);
  for(int k=0;k<3;k++) out[k]=sx*ix[k]+sy*iy[k]+sz*iz[k];
}

/* what a surface sends back into the room: reflected light, its own glow,
   and on the second pass whatever the room has already bounced onto it */
extern float MAT_ALBEDO[TL][3], MAT_EMIS[TL][3];
static void radianceAt(Tracer *T,float px,float py,float pz,
                       float nx,float ny,float nz,int brush,float *out){
  int t=BRUSHES[brush].t;
  const float *alb=MAT_ALBEDO[t], *em=MAT_EMIS[t];
  float L[3];
  directAt(T,px+nx*0.03f,py+ny*0.03f,pz+nz*0.03f,nx,ny,nz,brush,L);
  for(int k=0;k<3;k++) out[k]=alb[k]*L[k]*BOUNCE+em[k];
  if(cubesReady){
    float C[3];
    cubeAt(zoneAt(px,pz),nx,ny,nz,C);
    for(int k=0;k<3;k++) out[k]+=alb[k]*C[k]*BOUNCE;
  }
}

/* ---- one sample ------------------------------------------------------ */
static void bakePoint(Tracer *T,const float *p,const float *n,int bi,
                      BakeSample *out){
  float Tg[3],Bt[3];
  uvFrame(n,Tg,Bt);
  float sx=p[0]+n[0]*0.05f, sy=p[1]+n[1]*0.05f, sz=p[2]+n[2]*0.05f;
  float lm[9]={0,0,0,0,0,0,0,0,0};

  /* ---- direct, one shadow ray per lamp ------------------------------ */
  float sg[STY_N][9]={{0}};
  float nearSty=1e9f; int nearGrp=-1;
  for(int li=0;li<nLight;li++){
    Light *L=&LIGHTS[li];
    float dx=L->p[0]-sx, dy=L->p[1]-sy, dz=L->p[2]-sz;
    float dist=hypot3(dx,dy,dz);
    int grp=lampStyle(li);
    /* A LAMP FIXTURE'S OWN GLOW BELONGS TO ITS LAMP.  The nearest styled
       light within a metre owns this face's emissive: that is what makes
       a switched-off strip a dark strip rather than a bright strip in an
       unlit corridor. */
    if(grp>=0&&dist<1.0f&&dist<nearSty){ nearSty=dist; nearGrp=grp; }
    if(dist>=L->r||dist<1e-4f) continue;
    float inv=1.f/dist; dx*=inv; dy*=inv; dz*=inv;
    float ndl=n[0]*dx+n[1]*dy+n[2]*dz;
    if(ndl<=0) continue;
    float att=1.f-dist/L->r; att*=att;
    if(ndl*att<0.003f) continue;
    if(blocked(T,sx,sy,sz,dx,dy,dz,dist-0.06f,bi)) continue;
    float l0=dx*Tg[0]+dy*Tg[1]+dz*Tg[2];
    float l1=dx*Bt[0]+dy*Bt[1]+dz*Bt[2];
    float l2=ndl;
    float *dst = grp<0 ? lm : sg[grp];
    for(int i=0;i<3;i++){
      float w=l0*HL2[i][0]+l1*HL2[i][1]+l2*HL2[i][2];
      if(w<=0) continue;
      float g=w*att*BASIS_GAIN;
      dst[i*3]+=L->c[0]*g; dst[i*3+1]+=L->c[1]*g; dst[i*3+2]+=L->c[2]*g;
    }
  }
  /* ONE TRIPLE, SO ONE GROUP.  Whichever bank puts the most light on this
     sample keeps it; the rest is folded into the steady lightmap, which
     is wrong by however much the loser was worth and is invisible next to
     being wrong by a whole missing attribute. */
  { int best=-1; float bl=0;
    for(int g=0;g<STY_N;g++){
      float l=sg[g][0]+sg[g][1]+sg[g][2]+sg[g][3]+sg[g][4]+sg[g][5]
             +sg[g][6]+sg[g][7]+sg[g][8];
      if(l>bl){ bl=l; best=g; }
    }
    for(int g=0;g<STY_N;g++){
      if(g==best) continue;
      for(int k=0;k<9;k++) lm[k]+=sg[g][k];
    }
    int owner = nearGrp>=0 ? nearGrp : best;
    /* AND IT COLLAPSES TO ONE COLOUR, NOT A BASIS.  lm is nine floats --
       three directions by three channels -- and sty is three, because the
       shader adds it flat: `baked + vSty*style`.  A switchable bank does
       not get radiosity normal mapping, it gets its mean irradiance, and
       what that costs is the highlight rolling across a normal map on
       light that is about to be switched off anyway. */
    for(int k=0;k<3;k++)
      out->sty[k] = best<0 ? 0.f
                  : (sg[best][k]+sg[best][3+k]+sg[best][6+k])*(1.f/3.f);
    out->styG = owner>=0 ? (float)(owner+1) : 0.f;
    if(nearGrp>=0) out->styG += 8.f;         /* ... and it owns the emissive */
  }

  /* ---- occlusion, and one bounce over the same hemisphere ------------ */
  int hits=0;
  float bw[3]={0,0,0}, bn[9]={0,0,0,0,0,0,0,0,0};
  for(int r=0;r<AO_RAYS;r++){
    const float *dd=AO_DIRS[r];
    float dx=Tg[0]*dd[0]+Bt[0]*dd[1]+n[0]*dd[2];
    float dy=Tg[1]*dd[0]+Bt[1]*dd[1]+n[1]*dd[2];
    float dz=Tg[2]*dd[0]+Bt[2]*dd[1]+n[2]*dd[2];
    int gather=(r%GATHER_STEP)==0;
    int b=rayTrace(T,sx,sy,sz,dx,dy,dz,gather?GATHER_LEN:AO_LEN,bi);
    if(b>=0&&T->hitT<AO_LEN) hits++;
    if(!gather) continue;
    float w0=dd[0]*HL2[0][0]+dd[1]*HL2[0][1]+dd[2]*HL2[0][2];
    float w1=dd[0]*HL2[1][0]+dd[1]*HL2[1][1]+dd[2]*HL2[1][2];
    float w2=dd[0]*HL2[2][0]+dd[1]*HL2[2][1]+dd[2]*HL2[2][2];
    if(w0<0)w0=0; if(w1<0)w1=0; if(w2<0)w2=0;
    bw[0]+=w0; bw[1]+=w1; bw[2]+=w2;
    if(b<0) continue;                  /* open space carries nothing indoors */
    float hn[3]={0,0,0};
    hn[T->hitAx]=(float)T->hitSg;
    float R[3];
    float ht=T->hitT;
    radianceAt(T,sx+dx*ht,sy+dy*ht,sz+dz*ht,hn[0],hn[1],hn[2],b,R);
    for(int k=0;k<3;k++){
      bn[k]  +=R[k]*w0; bn[3+k]+=R[k]*w1; bn[6+k]+=R[k]*w2;
    }
  }
  /* indirect is a weighted average radiance, so an evenly glowing room reads
     back at exactly its own brightness */
  for(int i=0;i<3;i++){
    float wn = bw[i]>1e-6f ? 1.f/bw[i] : 0.f;
    for(int k=0;k<3;k++) lm[i*3+k]+=bn[i*3+k]*wn;
  }
  for(int k=0;k<9;k++) out->lm[k]=lm[k];
  float ao=1.f-((float)hits/AO_RAYS)*0.82f;
  out->ao = ao<0.22f ? 0.22f : ao;
}

/* ---- the parallel front door --------------------------------------- */
typedef struct {
  const float *pts;      /* 3 floats per sample */
  const float *nrms;
  const int   *bis;
  BakeSample  *out;
  int          n;
  Tracer      *tracers;  /* one per worker slot */
  int          chunk;
} Job;

static Job JOB;
static void bakeChunk(int i,void *ctx){
  Job *J=(Job*)ctx;
  Tracer *T=&J->tracers[i];          /* one tracer per chunk: no sharing, no race */
  int a=i*J->chunk, b=a+J->chunk;
  if(b>J->n) b=J->n;
  for(int s=a;s<b;s++)
    bakePoint(T,J->pts+s*3,J->nrms+s*3,J->bis[s],&J->out[s]);
}

/* The stamp array is per-tracer because it is written on every ray: sharing
   one across threads would not merely be slow, it would silently drop
   occluders and light the ship through its own walls. */
void bakeSamples(const float *pts,const float *nrms,const int *bis,
                 BakeSample *out,int n){
  if(n<=0) return;
  double t0=platTime();
  int nw=platCores(); if(nw<1) nw=1;
  /* eight chunks per core, so one slow region cannot strand the rest */
  int chunks=nw*8;
  int chunk=(n+chunks-1)/chunks; if(chunk<1) chunk=1;
  chunks=(n+chunk-1)/chunk;

  Tracer *tr=(Tracer*)calloc(chunks,sizeof(Tracer));
  for(int i=0;i<chunks;i++) tr[i].stamp=(int*)calloc(nBrush,sizeof(int));

  JOB.pts=pts; JOB.nrms=nrms; JOB.bis=bis; JOB.out=out; JOB.n=n;
  JOB.tracers=tr; JOB.chunk=chunk;
  platParallel(bakeChunk,chunks,&JOB);

  for(int i=0;i<chunks;i++) free(tr[i].stamp);
  free(tr);
  platLog("bake   %d samples  %d rays each  %d threads  %.0f ms",
          n,AO_RAYS,nw,(platTime()-t0)*1000.0);
}

/* ---- env_cubemap ---------------------------------------------------- */
/* One ambient cube per room, built from the same solution.  Six directions
   is what Source stores for prop lighting; here it doubles as the specular
   environment, which is why a steel panel in the amber storage bay reflects
   amber and the same panel in the control room reflects blue. */
static const float CUBE_DIRS[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

static void ambientCube(Tracer *T,const float *pt,float *out){
  for(int f=0;f<6;f++){
    const float *nd=CUBE_DIRS[f];
    float Tg[3],Bt[3];
    uvFrame(nd,Tg,Bt);
    float r=0,g=0,b=0;
    for(int i=0;i<AO_RAYS;i++){
      const float *dd=AO_DIRS[i];
      float dx=Tg[0]*dd[0]+Bt[0]*dd[1]+nd[0]*dd[2];
      float dy=Tg[1]*dd[0]+Bt[1]*dd[1]+nd[1]*dd[2];
      float dz=Tg[2]*dd[0]+Bt[2]*dd[1]+nd[2]*dd[2];
      int hb=rayTrace(T,pt[0],pt[1],pt[2],dx,dy,dz,45.f,-1);
      if(hb<0) continue;
      float hn[3]={0,0,0}; hn[T->hitAx]=(float)T->hitSg;
      float R[3], ht=T->hitT;
      radianceAt(T,pt[0]+dx*ht,pt[1]+dy*ht,pt[2]+dz*ht,hn[0],hn[1],hn[2],hb,R);
      r+=R[0]; g+=R[1]; b+=R[2];
    }
    float w=1.f/AO_RAYS;
    out[f*3]=r*w; out[f*3+1]=g*w; out[f*3+2]=b*w;
  }
}

typedef struct { float *dst; Tracer *tr; } CubeJob;
static CubeJob CJ;
static void cubeChunk(int i,void *ctx){
  CubeJob *J=(CubeJob*)ctx;
  const float *c = (i<nZone) ? ZONES[i].c : VOID_CUBE;
  ambientCube(&J->tr[i], c, J->dst+(size_t)i*18);
}

void solveCubes(int passes){
  double t0=platTime();
  int n=CUBE_COUNT;
  float *tmp=(float*)malloc(sizeof(float)*(size_t)n*18);
  Tracer *tr=(Tracer*)calloc(n,sizeof(Tracer));
  for(int i=0;i<n;i++) tr[i].stamp=(int*)calloc(nBrush,sizeof(int));
  CJ.dst=tmp; CJ.tr=tr;
  /* One pass solves direct light only; feeding it back in and solving again
     carries the first bounce, which is where a room's colour comes from. */
  for(int p=0;p<passes;p++){
    platParallel(cubeChunk,n,&CJ);
    memcpy(CUBES,tmp,sizeof(float)*(size_t)n*18);
    cubesReady=1;
  }
  for(int i=0;i<n;i++) free(tr[i].stamp);
  free(tr); free(tmp);
  platLog("cubes  %d probes x %d passes  %.0f ms",n,passes,(platTime()-t0)*1000.0);
}

void bakeInit(void){
  AO_RAYS     = FASTLIGHT?4:12;
  GATHER_STEP = FASTLIGHT?3:2;
  for(int i=0;i<AO_RAYS;i++){
    float a=i*2.399963229f;
    float r=sqrtf(((float)i+0.5f)/AO_RAYS)*0.97f;    /* never exactly tangent */
    AO_DIRS[i][0]=cosf(a)*r;
    AO_DIRS[i][1]=sinf(a)*r;
    AO_DIRS[i][2]=sqrtf(1.f-r*r > 0 ? 1.f-r*r : 0.f);
  }
  HL2[0][0]=-S6; HL2[0][1]=-S2; HL2[0][2]=S3;
  HL2[1][0]=-S6; HL2[1][1]= S2; HL2[1][2]=S3;
  HL2[2][0]=0.81649658f; HL2[2][1]=0; HL2[2][2]=S3;

  CUBE_COUNT = nZone+1;
  free(CUBES);
  CUBES=(float*)calloc((size_t)CUBE_COUNT*18,sizeof(float));
  cubesReady=0;
  gridBuild();
}

/* One ray, for the game rather than the bake: a moving thing asks whether a
   lamp can see it, a few dozen times a frame.  Its own tracer, because the
   bake's are freed the moment the level is meshed. */
float rayHitDist;                    /* how far the last rayTraceOne got */
int   rayHitAx, rayHitSg;            /* ... and which face of the box it hit:
                                        n = (ax==0?sg:0, ax==1?sg:0, ...),
                                        which is the whole normal a crater
                                        needs off an axis-aligned brush */
int rayTraceOne(float ox,float oy,float oz,float dx,float dy,float dz,
                float maxT,int skip){
  static Tracer T; static int inited;
  if(!inited){ T.stamp=(int*)calloc(nBrush,sizeof(int)); inited=1; }
  int b=rayTrace(&T,ox,oy,oz,dx,dy,dz,maxT,skip);
  rayHitDist=T.hitT; rayHitAx=T.hitAx; rayHitSg=T.hitSg;
  return b;
}

/* the single-sample entry the mesher used before it batched; kept because
   props and entities still ask for one point at a time */
void bakeAt(const float *p,const float *n,int bi,BakeSample *out){
  static Tracer T; static int inited;
  if(!inited){ T.stamp=(int*)calloc(nBrush,sizeof(int)); inited=1; }
  bakePoint(&T,p,n,bi,out);
}

/* ---- reflection probes ---------------------------------------------- */
/* THE AMBIENT CUBE IS AN IRRADIANCE, AND A REFLECTION IS NOT ONE.  Six
   axis values reconstruct how much light arrives at a surface, which is all
   a diffuse term ever needed; asked for reflect(-V,N) it answers the same
   smear from every angle, so a steel panel showed a wash rather than the
   lamp that is actually in front of it.  The cube stays -- it is still the
   right thing for diffuse -- and the specular side now reads a real
   radiance map traced per zone.

   OCTAHEDRAL, NOT A CUBEMAP, because GL 3.3 core has no cubemap arrays
   (ARB_texture_cube_map_array is 4.0) and one sampler2DArray holds every
   zone at every roughness with no extension at all.

   THE BORDER RING IS TRACED, NOT MIRRORED.  The octahedral decode extends
   continuously past the [-1,1] square -- a texel one step outside the edge
   names a real direction, the mirrored one -- so the ring costs 4% more
   rays and buys a seam that bilinear crosses without a step.  Reconstructing
   a fast-varying field over 400 k directions, worst error is 0.0148 with the
   ring and 0.0268 clamped to the edge, and the clamped worst case lands on
   v=-1.000: on the seam itself.  Copying an index instead would have been
   wrong as well as longer -- the mirror of a border texel misses the inner
   texel centre by half a texel, measured, so there is no permutation to
   copy. */
int    PROBE_N, PROBE_E;
float *PROBES;

static void octDecode(float u,float v,float *d){
  float x=u, z=v, y=1.f-fabsf(u)-fabsf(v);
  if(y<0){ float t=-y; x += x>=0?-t:t; z += z>=0?-t:t; }
  float l=1.f/hypot3(x,y,z);
  d[0]=x*l; d[1]=y*l; d[2]=z*l;
}
/* array index -> the [-1,1] coordinate of that texel's centre, border at
   index 0 and PROBE_N+1 landing just outside */
static float octUV(int i){ return ((i-0.5f)/PROBE_N)*2.f-1.f; }

/* one probe, traced: the radiance arriving from every direction at a point */
static void probeTrace(Tracer *T,const float *pt,float *dst){
  for(int j=0;j<PROBE_E;j++)
    for(int i=0;i<PROBE_E;i++){
      float d[3]; octDecode(octUV(i),octUV(j),d);
      float *o=dst+((size_t)j*PROBE_E+i)*3;
      o[0]=o[1]=o[2]=0;
      int hb=rayTrace(T,pt[0],pt[1],pt[2],d[0],d[1],d[2],45.f,-1);
      if(hb<0) continue;
      float hn[3]={0,0,0}; hn[T->hitAx]=(float)T->hitSg;
      float ht=T->hitT;
      radianceAt(T,pt[0]+d[0]*ht,pt[1]+d[1]*ht,pt[2]+d[2]*ht,
                 hn[0],hn[1],hn[2],hb,o);
    }
}

/* box-downsample the traced map's INTERIOR to an S x S grid, which is what
   the wide lobes are convolved from.  A lobe that spans thirty degrees does
   not need a 1.85-degree source, and the cost of the convolution is the
   product of the two resolutions. */
static void probeShrink(const float *src,int S,float *dst){
  int step=PROBE_N/S;
  for(int j=0;j<S;j++)
    for(int i=0;i<S;i++){
      float a[3]={0,0,0};
      for(int y=0;y<step;y++)
        for(int x=0;x<step;x++){
          const float *s=src+((size_t)(1+j*step+y)*PROBE_E+(1+i*step+x))*3;
          a[0]+=s[0]; a[1]+=s[1]; a[2]+=s[2];
        }
      float w=1.f/(step*step);
      float *o=dst+((size_t)j*S+i)*3;
      o[0]=a[0]*w; o[1]=a[1]*w; o[2]=a[2]*w;
    }
}

/* the split-sum prefilter: the GGX lobe around R, integrated over the whole
   sphere rather than importance-sampled.  At these resolutions brute force
   is both cheaper and exact -- there is no sample count to get wrong, and
   no seam, because every source texel is visited whatever R is.  On a
   synthetic sky holding one 40x lamp against a 0.2 ground, the six levels
   come out peak 40.2 / 1.77 / 0.573 / 0.313 / 0.257 / 0.239 with the mean
   flat at 0.209 to four figures: the lobe widens and the energy stays. */
static void probeConvolve(const float *src,int S,float alpha,float *dst){
  float *dir=(float*)malloc(sizeof(float)*(size_t)S*S*3);
  for(int j=0;j<S;j++)
    for(int i=0;i<S;i++){
      float u=((i+0.5f)/S)*2.f-1.f, v=((j+0.5f)/S)*2.f-1.f;
      octDecode(u,v,dir+((size_t)j*S+i)*3);
    }
  float a2=alpha*alpha;
  for(int t=0;t<PROBE_E*PROBE_E;t++){
    float R[3]; octDecode(octUV(t%PROBE_E),octUV(t/PROBE_E),R);
    float acc[3]={0,0,0}, wsum=0;
    for(int s=0;s<S*S;s++){
      const float *d=dir+(size_t)s*3;
      float nd=R[0]*d[0]+R[1]*d[1]+R[2]*d[2];
      if(nd<=0) continue;
      /* H = normalize(R+d); with |R|=|d|=1, dot(R,H) has a closed form */
      float noh=sqrtf((1.f+nd)*0.5f);
      float den=noh*noh*(a2-1.f)+1.f;
      float D=a2/(den*den);
      float w=D*nd;
      const float *L=src+(size_t)s*3;
      acc[0]+=L[0]*w; acc[1]+=L[1]*w; acc[2]+=L[2]*w;
      wsum+=w;
    }
    float *o=dst+(size_t)t*3;
    float iw = wsum>1e-9f ? 1.f/wsum : 0.f;
    o[0]=acc[0]*iw; o[1]=acc[1]*iw; o[2]=acc[2]*iw;
  }
  free(dir);
}

typedef struct { Tracer *tr; } ProbeJob;
static ProbeJob PJ;
static void probeChunk(int zi,void *ctx){
  ProbeJob *J=(ProbeJob*)ctx;
  const float *c = (zi<nZone) ? ZONES[zi].c : VOID_CUBE;
  size_t lay=(size_t)PROBE_E*PROBE_E*3;
  float *base=PROBES+(size_t)zi*PROBE_LEV*lay;
  probeTrace(&J->tr[zi],c,base);

  float *shr=(float*)malloc(sizeof(float)*(size_t)PROBE_N*PROBE_N*3);
  for(int l=1;l<PROBE_LEV;l++){
    int S=PROBE_N>>l; if(S<8) S=8;
    float rough=(float)l/(PROBE_LEV-1);
    /* A LOBE NARROWER THAN A SOURCE TEXEL IS NOT A SHARPER REFLECTION, it
       is an undersampled one.  Floor the lobe at the source's own angular
       step (2.06/S radians, measured off the decode) so level 1 reads as a
       soft version of level 0 rather than a blocky one. */
    float alpha=rough*rough, floorA=2.2f/S;
    if(alpha<floorA) alpha=floorA;
    probeShrink(base,S,shr);
    probeConvolve(shr,S,alpha,base+(size_t)l*lay);
  }
  free(shr);
}

void solveProbes(void){
  double t0=platTime();
  PROBE_N = FASTLIGHT?32:64;
  PROBE_E = PROBE_N+2;
  size_t lay=(size_t)PROBE_E*PROBE_E*3;
  free(PROBES);
  PROBES=(float*)calloc((size_t)CUBE_COUNT*PROBE_LEV*lay,sizeof(float));
  Tracer *tr=(Tracer*)calloc(CUBE_COUNT,sizeof(Tracer));
  for(int i=0;i<CUBE_COUNT;i++) tr[i].stamp=(int*)calloc(nBrush,sizeof(int));
  PJ.tr=tr;
  platParallel(probeChunk,CUBE_COUNT,&PJ);
  for(int i=0;i<CUBE_COUNT;i++) free(tr[i].stamp);
  free(tr);
  platLog("probe  %d zones x %d levels at %d^2  %.1f MB  %.0f ms",
          CUBE_COUNT,PROBE_LEV,PROBE_E,
          CUBE_COUNT*PROBE_LEV*lay*sizeof(float)/1048576.0,
          (platTime()-t0)*1000.0);
}

/* ---- the probes, photographed ---------------------------------------
   A PROBE THAT IS SILENTLY BLACK LOOKS EXACTLY LIKE A MATERIAL THAT IS
   SILENTLY MATT, so the atlas is a switch rather than a debugging session:
   `playlumi.exe -probe atlas.png` lays every zone out as a column and every
   roughness level as a row.  A column that is dark is a zone with no light
   in it; a row that is not blurrier than the one above it is a prefilter
   that did not run. */
int shotWritePNG(const char *path,int w,int h,const unsigned char *rgb);

int probeDumpPNG(const char *path){
  int cw=PROBE_E, W=CUBE_COUNT*cw, H=PROBE_LEV*cw;
  unsigned char *px=(unsigned char*)malloc((size_t)W*H*3);
  size_t lay=(size_t)PROBE_E*PROBE_E*3;
  for(int l=0;l<PROBE_LEV;l++)
    for(int z=0;z<CUBE_COUNT;z++){
      const float *src=PROBES+((size_t)z*PROBE_LEV+l)*lay;
      for(int j=0;j<cw;j++)
        for(int i=0;i<cw;i++){
          const float *s=src+((size_t)j*cw+i)*3;
          /* the maps are HDR; Reinhard and a gamma so a lamp does not eat
             the rest of the picture */
          int X=z*cw+i, Y=l*cw+j;
          unsigned char *o=px+((size_t)(H-1-Y)*W+X)*3;   /* the writer flips */
          for(int k=0;k<3;k++){
            float v=s[k]/(1.f+s[k]);
            v=powf(v<0?0:v,1.f/2.2f)*255.f+0.5f;
            o[k]=(unsigned char)(v>255?255:(v<0?0:v));
          }
        }
    }
  int ok=shotWritePNG(path,W,H,px);
  free(px);
  platLog("probe  %s  %dx%d  %s",path,W,H,ok?"ok":"FAILED");
  return ok;
}
