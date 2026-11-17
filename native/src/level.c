/* ============================================================
   2.  LEVEL

   The plan arrives as a table (see tools/nativepack.py) and is
   copied into arrays that can still GROW, because the kit adds
   to them: every part stamps its own clip boxes and, where it
   is lit, a real emitter brush.  A part that skipped that would
   glow in the shader and light nothing at all.

   solidAt() is asked once per player step and several thousand
   times per bake ray, so the brushes go into a flat 2 m grid on
   x/z.  A linear scan of 3,338 boxes measured 40x slower.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include <stdlib.h>
#include <string.h>
#include "plan_data.h"

Brush *BRUSHES; int nBrush;  static int capBrush;
Face  *FACES;   int nFace;   static int capFace;
Light *LIGHTS;  int nLight;
Zone  *ZONES;   int nZone;
DoorSlot *DOORS; int nDoor;
const float *VOID_CUBE = PLAN_VOID_CUBE;

float *TRIS; int nTriVert; static int capTri;

/* ---- the solid grid ------------------------------------------------ */
#define GC 2.0f                          /* metres per cell */
static float gx0,gz0; static int gnx,gnz;
static int *gHead;                       /* -1 terminated chains */
static int *gNext;
static int gridBuilt;

static void gridBuild(void){
  float x0=1e9f,z0=1e9f,x1=-1e9f,z1=-1e9f;
  for(int i=0;i<nBrush;i++){
    Brush *b=&BRUSHES[i];
    if(b->x0<x0)x0=b->x0; if(b->z0<z0)z0=b->z0;
    if(b->x1>x1)x1=b->x1; if(b->z1>z1)z1=b->z1;
  }
  gx0=x0-1; gz0=z0-1;
  gnx=(int)((x1-gx0)/GC)+3; gnz=(int)((z1-gz0)/GC)+3;
  free(gHead); free(gNext);
  gHead=(int*)malloc(sizeof(int)*gnx*gnz);
  for(int i=0;i<gnx*gnz;i++) gHead[i]=-1;
  /* a brush spans cells, so the chain node count is not nBrush */
  int cnt=0;
  for(int i=0;i<nBrush;i++){
    Brush *b=&BRUSHES[i];
    int cx0=(int)((b->x0-gx0)/GC), cx1=(int)((b->x1-gx0)/GC);
    int cz0=(int)((b->z0-gz0)/GC), cz1=(int)((b->z1-gz0)/GC);
    cnt += (cx1-cx0+1)*(cz1-cz0+1);
  }
  gNext=(int*)malloc(sizeof(int)*cnt*2);   /* pairs: brush, next */
  int w=0;
  for(int i=0;i<nBrush;i++){
    Brush *b=&BRUSHES[i];
    int cx0=(int)((b->x0-gx0)/GC), cx1=(int)((b->x1-gx0)/GC);
    int cz0=(int)((b->z0-gz0)/GC), cz1=(int)((b->z1-gz0)/GC);
    if(cx0<0)cx0=0; if(cz0<0)cz0=0;
    if(cx1>=gnx)cx1=gnx-1; if(cz1>=gnz)cz1=gnz-1;
    for(int cz=cz0;cz<=cz1;cz++) for(int cx=cx0;cx<=cx1;cx++){
      int c=cz*gnx+cx;
      gNext[w*2]=i; gNext[w*2+1]=gHead[c]; gHead[c]=w; w++;
    }
  }
  gridBuilt=1;
}

int solidAt(float x,float y,float z){
  if(!gridBuilt) return 0;
  int cx=(int)((x-gx0)/GC), cz=(int)((z-gz0)/GC);
  if(cx<0||cz<0||cx>=gnx||cz>=gnz) return 0;
  for(int n=gHead[cz*gnx+cx]; n>=0; n=gNext[n*2+1]){
    Brush *b=&BRUSHES[gNext[n*2]];
    if(x>=b->x0&&x<=b->x1&&y>=b->y0&&y<=b->y1&&z>=b->z0&&z<=b->z1) return 1;
  }
  return 0;
}
void levelGridBuild(void){ gridBuild(); }

/* ---- growth -------------------------------------------------------- */
int addBrush(float x0,float y0,float z0,float x1,float y1,float z1,
             int t,int clip){
  if(nBrush==capBrush){
    capBrush = capBrush? capBrush*2 : 4096;
    BRUSHES=(Brush*)realloc(BRUSHES,sizeof(Brush)*capBrush);
  }
  Brush *b=&BRUSHES[nBrush++];
  b->x0=x0<x1?x0:x1; b->y0=y0<y1?y0:y1; b->z0=z0<z1?z0:z1;
  b->x1=x0<x1?x1:x0; b->y1=y0<y1?y1:y0; b->z1=z0<z1?z1:z0;
  b->t=(unsigned char)t; b->clip=(unsigned char)(clip?1:0);
  gridBuilt=0;
  return nBrush-1;
}

void addQuad(const float *c0,const float *c1,const float *c2,const float *c3,
             int t,float uo,float wo){
  if(nFace==capFace){
    capFace = capFace? capFace*2 : 1024;
    FACES=(Face*)realloc(FACES,sizeof(Face)*capFace);
  }
  Face *f=&FACES[nFace++];
  for(int k=0;k<3;k++){
    f->c[0][k]=c0[k]; f->c[1][k]=c1[k]; f->c[2][k]=c2[k]; f->c[3][k]=c3[k];
  }
  f->uo=uo; f->wo=wo; f->t=(unsigned char)t;
}

void trisReserve(int verts){
  int need=(nTriVert+verts)*9;
  if(need>capTri){
    while(capTri<need) capTri = capTri? capTri*2 : (1<<20);
    TRIS=(float*)realloc(TRIS,sizeof(float)*capTri);
  }
}
void trisPush(const float *v9){
  trisReserve(1);
  memcpy(TRIS+(size_t)nTriVert*9, v9, sizeof(float)*9);
  nTriVert++;
}

int zoneAt(float x,float z){
  for(int i=0;i<nZone;i++){
    Zone *Z=&ZONES[i];
    if(x>=Z->x0&&x<=Z->x1&&z>=Z->z0&&z<=Z->z1) return i;
  }
  return nZone;
}

/* ---- load ---------------------------------------------------------- */
void levelInit(void){
  capBrush = PLAN_NBRUSH + 8192;              /* the kit's clip and emit boxes */
  BRUSHES=(Brush*)malloc(sizeof(Brush)*capBrush);
  for(int i=0;i<PLAN_NBRUSH;i++){
    const float *p=PLAN_BRUSH+i*6;
    Brush *b=&BRUSHES[i];
    b->x0=p[0]; b->y0=p[1]; b->z0=p[2];
    b->x1=p[3]; b->y1=p[4]; b->z1=p[5];
    b->t=PLAN_BRUSH_T[i]; b->clip=PLAN_BRUSH_C[i];
  }
  nBrush=PLAN_NBRUSH;

  capFace = PLAN_NFACE + 512;
  FACES=(Face*)malloc(sizeof(Face)*capFace);
  for(int i=0;i<PLAN_NFACE;i++){
    const float *p=PLAN_FACE+i*14;
    Face *f=&FACES[i];
    for(int c=0;c<4;c++) for(int k=0;k<3;k++) f->c[c][k]=p[c*3+k];
    f->uo=p[12]; f->wo=p[13]; f->t=PLAN_FACE_T[i];
  }
  nFace=PLAN_NFACE;

  nLight=PLAN_NLIGHT;
  LIGHTS=(Light*)malloc(sizeof(Light)*(nLight+64));
  for(int i=0;i<nLight;i++){
    const float *p=PLAN_LIGHT+i*7;
    Light *L=&LIGHTS[i];
    L->p[0]=p[0]; L->p[1]=p[1]; L->p[2]=p[2];
    L->c[0]=p[3]; L->c[1]=p[4]; L->c[2]=p[5];
    L->r=p[6]; L->noFix=PLAN_LIGHT_FIX[i];
  }

  nZone=PLAN_NZONE;
  ZONES=(Zone*)malloc(sizeof(Zone)*nZone);
  for(int i=0;i<nZone;i++){
    const float *p=PLAN_ZONE+i*7;
    Zone *Z=&ZONES[i];
    Z->n=PLAN_ZONE_N[i]; Z->s=PLAN_ZONE_S[i];
    Z->x0=p[0]; Z->x1=p[1]; Z->z0=p[2]; Z->z1=p[3];
    Z->c[0]=p[4]; Z->c[1]=p[5]; Z->c[2]=p[6];
  }

  nDoor=PLAN_NDOOR;
  DOORS=(DoorSlot*)malloc(sizeof(DoorSlot)*(nDoor?nDoor:1));
  for(int i=0;i<nDoor;i++){
    const float *p=PLAN_DOOR+i*4;
    DOORS[i].x=p[0]; DOORS[i].z=p[1];
    DOORS[i].w=p[2]; DOORS[i].d=p[3]; DOORS[i].ax=PLAN_DOOR_AX[i];
  }

  platLog("level  %d brushes  %d faces  %d lamps  %d zones  %d doors",
          nBrush,nFace,nLight,nZone,nDoor);
}

/* ---- WHERE THE DECK IS, WHICH IS NOT THE FIRST THING UNDER THE CEILING
   Four systems need to answer "what would a person be standing on here":
   an item settles on it, the goal ring lies on it, Lumi flies to it plus
   1.45 m and a prop stands on it.  All four used to trace down from 2.9 m
   -- under every ceiling in this ship -- and take the first hit, and what
   that hits over a good deal of the ship is a rib ring or a pipe run at
   2.4-2.8 m.  One trace, four bugs: a rotary cannon 2.7 m up outside its
   own pickup sphere, a marker hanging in the air, and the drone rising
   OVER a 2.62 m door head to cross the ship through the ceiling.

   deckNear walks the whole column instead of stopping at the first thing
   in it, keeps only the surfaces a person could stand on -- 1.55 m of
   clear space above -- and returns the one nearest the deck the caller
   asked about.  A container top in the cargo hold IS standable, which is
   why the caller has to say which deck it means. */
int rayTraceOne(float,float,float,float,float,float,float,int);
extern float rayHitDist;

int deckNear(float x,float z,float want,float *y){
  /* AND THE COLUMN HAS TO START ABOVE THE DECK BEING ASKED ABOUT.  2.90 is
     over every main-deck surface in the ship, and for the callers that mean
     the main deck that is the whole question.  It is not over the cargo
     hold's platform: shipdetail.py lays that grating at 3.15 and its top
     face is 3.37, so a column that starts at 2.90 and walks DOWN can never
     hold it.  Pack 3 -- the one item with a deck written in its row, and
     the reason the platform is there at all -- reported "nothing within
     1.2 m of its written deck 3.15" and settled on the floor, which reads
     as a platform that was never built rather than as a trace that began
     underneath one.  At want 0 this is 2.90 m and 4.2 m exactly, so every
     caller that asks about the main deck gets the answer it always got. */
  float top=2.90f;
  if(want+0.95f>top) top=want+0.95f;
  const float len=top+1.30f;          /* 2.90 + 1.30 = the 4.2 this was */
  int   ok=0;  float oy=0, od=1e9f;
  int   lax=0; float ly=0, ld=1e9f;
  for(int k=0;k<6;k++){
    int b=rayTraceOne(x,top,z,0,-1,0,len,-1);
    if(b<0) break;
    float fy=top-rayHitDist;         /* three globals: read it before solidAt */
    if(!solidAt(x,fy+0.30f,z)&&!solidAt(x,fy+0.95f,z)){
      float d=fy-want; if(d<0) d=-d;
      if(!solidAt(x,fy+1.55f,z)){ if(d<od){ od=d; oy=fy; ok=1; } }
      else if(d<ld){ ld=d; ly=fy; lax=1; }
    }
    top=fy-0.10f;
    if(top<-3.f) break;
  }
  if(ok){ *y=oy; return 1; }
  if(lax){ *y=ly; return 1; }
  return 0;
}
/* the main deck, which is what everything written on the plan means */
int deckAt(float x,float z,float *y){ return deckNear(x,z,0.f,y); }

/* Overlap, not containment: the player is a box and so is everything it can
   walk into.  Same grid, but a span of cells rather than one -- and then the
   doors, whose leaves are not in it because they move. */
int adBoxSolid(float,float,float,float,float,float);
int propBoxSolid(float,float,float,float,float,float);

int boxSolid(float ax0,float ay0,float az0,float ax1,float ay1,float az1){
  if(adBoxSolid(ax0,ay0,az0,ax1,ay1,az1)) return 1;
  /* AND THE FURNITURE, which is not in the grid either: the grid was built
     from the brush set and handed to the bake long before a single crate
     stood anywhere.  Props are placed against walls and never within three
     metres of a leaf (props.c), so this can only ever stop you walking
     THROUGH one -- it can never be what closes a route. */
  if(propBoxSolid(ax0,ay0,az0,ax1,ay1,az1)) return 1;
  if(!gridBuilt) return 0;
  int i0=(int)((ax0-gx0)/GC), i1=(int)((ax1-gx0)/GC);
  int j0=(int)((az0-gz0)/GC), j1=(int)((az1-gz0)/GC);
  if(i0<0)i0=0; if(j0<0)j0=0;
  if(i1>=gnx)i1=gnx-1; if(j1>=gnz)j1=gnz-1;
  for(int j=j0;j<=j1;j++) for(int i=i0;i<=i1;i++)
    for(int n=gHead[j*gnx+i]; n>=0; n=gNext[n*2+1]){
      Brush *b=&BRUSHES[gNext[n*2]];
      if(ax1>b->x0&&ax0<b->x1&&ay1>b->y0&&ay0<b->y1&&az1>b->z0&&az0<b->z1)
        return 1;
    }
  return 0;
}
