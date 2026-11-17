/* ============================================================
   PROPS.  WHAT MAKES A ROOM A ROOM.

   THE MESHES ARE THE BENCH'S.  props/props_v01/ modelled 37 hero
   props.  The bench's surface is one packed atlas per prop and
   this build cannot afford a second texture pipeline, but that
   is no reason to approximate the geometry as assemblies of
   instanced solids: propkit.c is the
   bench's BUILDERS ported call for call, and props_gen.c is the
   37 props as 703 recorded calls into them.  19,094 faces, the
   same number the bench counts.

   So the geometry is not approximated.  What is dropped is
   only the atlas: a region's kind, tint, paint and accent become
   one of this engine's own MACHINED materials rather than a
   1024^2 bake, faces are grouped by that material, and each
   group is handed to prim.c as an instanced range -- the same
   arrangement lumimesh.c uses for LUMI, and for the same reason.
   A prop costs one draw per material group and nothing per
   instance.

   AND THE PLACEMENT IS SOLVED, NOT WRITTEN.  A hundred
   hand-typed coordinates on a drawing that is not the ship
   (build_ship.py widens and snaps the plan) is a hundred chances to
   stand a locker in a doorway.  Each room asks for so many of
   a kind; the placer walks a grid over the room and takes the
   spots that pass every test -- a standable deck, a footprint
   clear of the geometry, a wall behind it if the kind wants
   one, three metres from every door leaf, clear of the items
   and the landmarks the story steers by, and clear of the
   other props.  A kind that cannot find a legal spot simply
   places fewer, which is the correct failure.

   THE CORRIDORS GET NOTHING.  They used to get junction boxes,
   suppression stations and EVA lockers on the grounds that
   those are wall furniture and a wall is not a route.  In a
   2.4 m passage that is not true often enough, so props stay
   out of corridors and a corridor is now empty.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include "ent.h"
#include "prim.h"
#include "props.h"
#include "propkit.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* the plan, in the units the plan is drawn in: metres per plan unit, origin */
#define S_    0.145f
#define OX_   340.f
#define OZ_   130.f
static float wx(float px){ return (px-OX_)*S_; }
static float wz(float pz){ return (pz-OZ_)*S_; }

int   adCount(void);
void  adPos(int i,float *o);

/* ============================================================
   1.  THE BENCH'S REGIONS, AS THIS SHIP'S MATERIALS

   propview.html bakes a region with an eleven-kind height
   function, a tint, a paint flag and an accent role.  Height
   and grime are what this port cannot carry; the other three
   are exactly what an EntFx already says.  So a region becomes
   (albedo, roughness, metalness, emissive) and nothing else,
   and faces that agree on those four share a range.

   THE PER-REGION TONE JITTER IS DELIBERATELY DROPPED.  The
   bench drifts every region's brightness by +/-7% off its seed
   so no two plates read identical; here that would turn 70
   regions into 70 material groups, which is 70 draw calls for
   a difference the eye cannot find in a dark corridor.
   ============================================================ */
static const float ACC_OCHRE[3]={0.900f,0.330f,0.050f};
static const float ACC_AMBER[3]={1.000f,0.620f,0.190f};
static const float ACC_ALARM[3]={1.000f,0.105f,0.055f};
static const float ACC_LIVEC[3]={0.240f,0.800f,0.940f};
static const float ACC_LIVEG[3]={0.260f,0.870f,0.400f};
/* untinted is bare machined alloy.  Authored bright for the same reason
   the doors are: MACHINED spends most of a conductor's albedo on a tinted
   specular, so 0.55 steel arrives on screen near 0.15. */
static const float MAT_STEEL[3]={0.545f,0.531f,0.500f};
/* and a painted tint is authored 1.4x the bench's, because the bench lights
   its props with a 3.6-intensity key and this ship's corridors are lit by a
   torch and eight per cent of a lamp. */
#define TINT_GAIN 1.40f

typedef struct { float col[3], rough, metal, emis; } PMat;

static const float *accentOf(int a){
  switch(a){
  case PK_A_OCHRE:  return ACC_OCHRE;
  case PK_A_AMBER:  return ACC_AMBER;
  case PK_A_LIVE_C: return ACC_LIVEC;
  case PK_A_LIVE_G: return ACC_LIVEG;
  case PK_A_ALARM:  return ACC_ALARM;
  }
  return ACC_AMBER;
}
static void regMat(const PkRegion *R,PMat *m){
  float k;
  if(R->tint[0]>=0.f){
    for(int i=0;i<3;i++){ k=R->tint[i]*TINT_GAIN; m->col[i]=k>1.f?1.f:k; }
  } else memcpy(m->col,MAT_STEEL,sizeof m->col);
  m->emis=0.f;
  if(R->paint){ m->metal=0.05f; m->rough=0.52f; }
  else        { m->metal=0.90f; m->rough=0.34f; }
  switch(R->kind){
  case PK_HAZARD: {          /* a livery stripe: orange, and it is not steel */
    const float *a=accentOf(R->accent?R->accent:PK_A_OCHRE);
    for(int i=0;i<3;i++) m->col[i]=a[i]*0.88f;
    m->metal=0.05f; m->rough=0.48f; break; }
  /* AND THE EMISSIVES ARE A THIRD OF THE BENCH'S NUMBER.  propview adds
     `emissive` to a tone-mapped render lit by a 3.6-intensity key; ent.c
     adds `alb*vEmis*2.4` to a scene whose mid-tone is a torch beam, so
     the bench's 1.9 for a readout arrives as a white hole with no screen
     in it.  0.5 keeps the glow and keeps the glyphs. */
  case PK_SCREEN: {          /* a readout, and the only thing on a prop that
                                is genuinely a light source */
    const float *a=accentOf(R->accent?R->accent:PK_A_AMBER);
    for(int i=0;i<3;i++) m->col[i]=a[i]*0.55f;
    m->emis=0.52f; m->metal=0.f; m->rough=0.18f; break; }
  case PK_SEAM:
    if(R->accent){         /* a recessed run of light, not a painted line */
      const float *a=accentOf(R->accent);
      for(int i=0;i<3;i++) m->col[i]=a[i]*0.45f+0.30f;
      m->emis=0.70f; m->metal=0.f; m->rough=0.16f;
    }
    break;
  case PK_PERF:  m->rough+=0.06f; break;
  case PK_BUTTONS: m->rough=0.40f; break;
  }
}

static float vlen3(const float *a,const float *b){
  float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];
  return sqrtf(x*x+y*y+z*z);
}

/* ---- the ranges.  Up to eight material groups per prop; a ninth falls
   into the first, which costs a wrong colour on a few square centimetres
   and never costs a missing face. */
#define PMAT_MAX 8
static struct {
  int   grp[PMAT_MAX], prim[PMAT_MAX];   /* the material key, and its range */
  PMat  mat[PMAT_MAX];
  int   nGrp;
  float w,h,d;                            /* the AABB, in metres */
  float cx,cz,y0;                         /* ... and where its origin sits in it */
} PMESH[37];

/* the key a face is grouped by: colour to 1/64, the rest coarser still */
static int matKey(const PMat *m){
  int r=(int)(m->col[0]*63.f+0.5f), g=(int)(m->col[1]*63.f+0.5f);
  int b=(int)(m->col[2]*63.f+0.5f);
  int ro=(int)(m->rough*31.f+0.5f), me=(int)(m->metal*15.f+0.5f);
  int em=(int)(m->emis*7.f+0.5f);
  return ((((((r*64+g)*64+b)*32+ro)*16+me)*8+em));
}

/* one quad, wound so the face normal comes out of the front -- and every
   vertex carries the bench's region with it.

   THE REGION IS A 2D FRAME ON THE FACE, and pkFace4 fixes what it is:
   w is the length of p0->p1, h the length of p0->p3, and the height
   functions read a coordinate CENTRED on the face (every one of them
   starts `hx=ww*0.5` and takes abs(p)).  So the four corners are exactly
   (-w/2,-h/2), (+w/2,-h/2), (+w/2,+h/2), (-w/2,+h/2), which is the same
   rect the bench packs into its atlas.  Carry that and the fragment
   shader can evaluate H0() itself with no atlas anywhere. */
static float *MV, *MR; static int nMV, nMR, capMV, capMR;
static void mvTri(const float *a,const float *b,const float *c,
                  const float *na,const float *nb,const float *nc,
                  const float *ua,const float *ub,const float *uc,
                  const float *reg){
  if(nMV+18>capMV){ capMV=capMV?capMV*2:(1<<16);
                    while(capMV<nMV+18) capMV*=2;
                    MV=(float*)realloc(MV,sizeof(float)*capMV); }
  if(nMR+24>capMR){ capMR=capMR?capMR*2:(1<<16);
                    while(capMR<nMR+24) capMR*=2;
                    MR=(float*)realloc(MR,sizeof(float)*capMR); }
  const float *P[3]={a,b,c}, *N[3]={na,nb,nc}, *U[3]={ua,ub,uc};
  float e1[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
  float e2[3]={c[0]-a[0],c[1]-a[1],c[2]-a[2]};
  float x=e1[1]*e2[2]-e1[2]*e2[1], y=e1[2]*e2[0]-e1[0]*e2[2],
        z=e1[0]*e2[1]-e1[1]*e2[0];
  if(x*(na[0]+nb[0]+nc[0])+y*(na[1]+nb[1]+nc[1])+z*(na[2]+nb[2]+nc[2])<0.f){
    const float *t=P[1]; P[1]=P[2]; P[2]=t;
    const float *u=N[1]; N[1]=N[2]; N[2]=u;
    const float *q=U[1]; U[1]=U[2]; U[2]=q;
  }
  for(int i=0;i<3;i++){
    float *o=MV+nMV; nMV+=6;
    o[0]=P[i][0]; o[1]=P[i][1]; o[2]=P[i][2];
    o[3]=N[i][0]; o[4]=N[i][1]; o[5]=N[i][2];
    float *r=MR+nMR; nMR+=8;
    r[0]=U[i][0]; r[1]=U[i][1];          /* region-local metres */
    r[2]=reg[0];  r[3]=reg[1];           /* the region's own w,h */
    r[4]=reg[2];  r[5]=reg[3];           /* kind, seed */
    r[6]=reg[4];  r[7]=reg[5];           /* params x,y */
  }
}

void propMeshInit(void){
  int tf=0,tv=0,tg=0;
  for(int p=0;p<37;p++){
    pkBuildProp(p);
    float lo[3]={1e9f,1e9f,1e9f}, hi[3]={-1e9f,-1e9f,-1e9f};
    for(int f=0;f<pk.nFace;f++)
      for(int c=0;c<4;c++)
        for(int k=0;k<3;k++){
          float v=pk.face[f].p[c][k];
          if(v<lo[k]) lo[k]=v;
          if(v>hi[k]) hi[k]=v;
        }
    PMESH[p].w=hi[0]-lo[0]; PMESH[p].h=hi[1]-lo[1]; PMESH[p].d=hi[2]-lo[2];
    PMESH[p].cx=(hi[0]+lo[0])*0.5f; PMESH[p].cz=(hi[2]+lo[2])*0.5f;
    PMESH[p].y0=lo[1];
    /* a region's material, once, so the face loop below is a lookup */
    static PMat rm[PK_MAX_REGIONS]; static int rk[PK_MAX_REGIONS];
    for(int r=0;r<pk.nReg;r++){ regMat(&pk.reg[r],&rm[r]); rk[r]=matKey(&rm[r]); }
    PMESH[p].nGrp=0;
    for(int r=0;r<pk.nReg;r++){
      int seen=0;
      for(int g=0;g<PMESH[p].nGrp;g++) if(PMESH[p].grp[g]==rk[r]){ seen=1; break; }
      if(seen||PMESH[p].nGrp>=PMAT_MAX) continue;
      PMESH[p].grp[PMESH[p].nGrp]=rk[r];
      PMESH[p].mat[PMESH[p].nGrp]=rm[r];
      PMESH[p].nGrp++;
    }
    for(int g=0;g<PMESH[p].nGrp;g++){
      nMV=0;
      nMR=0;
      for(int f=0;f<pk.nFace;f++){
        const PkFace *F=&pk.face[f];
        int key=rk[F->ri], gi=0;
        for(int q=0;q<PMESH[p].nGrp;q++) if(PMESH[p].grp[q]==key){ gi=q; break; }
        if(gi!=g) continue;
        const float *n0,*n1,*n2,*n3;
        if(F->hasNv){ n0=F->nv[0]; n1=F->nv[1]; n2=F->nv[2]; n3=F->nv[3]; }
        else          n0=n1=n2=n3=F->n;
        /* the face's OWN size, which is the region's except where the
           region is a tile pooled across many faces -- the shader wraps,
           so both cases are one path */
        const PkRegion *R=&pk.reg[F->ri];
        float fw=0.5f*(vlen3(F->p[1],F->p[0])+vlen3(F->p[2],F->p[3]));
        float fh=0.5f*(vlen3(F->p[3],F->p[0])+vlen3(F->p[2],F->p[1]));
        float uv[4][2]={{-fw*0.5f,-fh*0.5f},{ fw*0.5f,-fh*0.5f},
                        { fw*0.5f, fh*0.5f},{-fw*0.5f, fh*0.5f}};
        float reg[6]={R->w,R->h,(float)R->kind,R->seed,
                      R->params[0],R->params[1]};
        mvTri(F->p[0],F->p[1],F->p[2],n0,n1,n2,uv[0],uv[1],uv[2],reg);
        mvTri(F->p[0],F->p[2],F->p[3],n0,n2,n3,uv[0],uv[2],uv[3],reg);
      }
      PMESH[p].prim[g]=primAddR(MV,MR,nMV/6);
      tv+=nMV/6;
    }
    tf+=pk.nFace; tg+=PMESH[p].nGrp;
  }
  free(MV); MV=0; capMV=0; nMV=0;
  free(MR); MR=0; capMR=0; nMR=0;
  platLog("props  %d meshes  %d faces  %d verts  %d ranges",37,tf,tv,tg);
}

/* ============================================================
   2.  WHAT A ROOM ASKS FOR, BY NAME

   The placer speaks in ROOM KINDS (RK_) and the bench speaks in indices
   into PK_NAME; this is the whole of the translation, plus the
   two things the mesh cannot say -- whether the kind wants a
   bulkhead behind it, and whether it may be stacked on itself.
   ============================================================ */
enum { RK_CRATE=0, RK_DRUM, RK_PALLET, RK_CONTAINER, RK_SHELF, RK_LOCKER,
       RK_BENCH, RK_TABLE, RK_DISPENSER, RK_TERMINAL, RK_BUNK, RK_FOOTLOCKER,
       RK_MEDBED, RK_CRYO, RK_CABINET, RK_SPECIMEN, RK_VALVE, RK_GENERATOR,
       RK_COOLANT, RK_FUELCELL, RK_DATARACK, RK_GASRACK, RK_JBOX,
       RK_LADDER, RK_FIRE, RK_EVA, RK_MONITOR, RK_PEDESTAL, RK_DRONEDOCK,
       RK_N };

typedef struct { unsigned char p, wall, stack; } Kind;
static const Kind KIND[RK_N]={
  { 1,0,1},  /* Supply Crate        */  {15,0,0},  /* Storage Drum        */
  {16,0,0},  /* Loaded Pallet       */  { 0,0,1},  /* Cargo Container     */
  {14,1,0},  /* Cargo Shelf         */  { 3,1,0},  /* Storage Locker      */
  { 6,0,0},  /* Crew Bench          */  { 9,0,0},  /* Mess Table          */
  {10,1,0},  /* Ration Dispenser    */  { 2,1,0},  /* Console Terminal    */
  { 7,1,0},  /* Bunk Bed            */  { 8,1,0},  /* Foot Locker         */
  {11,0,0},  /* Med Bed             */  {17,1,0},  /* Cryo Pod            */
  { 5,1,0},  /* Medbay Cabinet      */  {25,0,0},  /* Specimen Tank       */
  { 4,1,0},  /* Reactor Valve Stack */  {12,0,0},  /* Auxiliary Generator */
  {13,1,0},  /* Coolant Tank        */  {31,1,0},  /* Fuel Cell Stack     */
  {19,1,0},  /* Data Core Rack      */  {21,1,0},  /* Gas Cylinder Rack   */
  {24,1,0},  /* Wall Junction Box   */  {28,1,0},  /* Access Ladder       */
  {26,1,0},  /* Suppression Station */  {23,1,0},  /* EVA Suit Locker     */
  {29,1,0},  /* Monitor Bank        */  {30,0,0},  /* Control Pedestal    */
  {32,1,0},  /* Drone Dock          */
};
static float kW(int k){ return PMESH[KIND[k].p].w; }
static float kH(int k){ return PMESH[KIND[k].p].h; }
static float kD(int k){ return PMESH[KIND[k].p].d; }

typedef struct { float x,y,z,yaw; unsigned char k; short prop; int zi; } Prop;
#define MAXPROP 460
static Prop PR[MAXPROP]; static int nPR;
/* THE STORY'S OWN FIXTURES LIVE IN THEIR OWN ARRAY.  PR and PB are index
   parallel and propClearAt deletes by swapping the last one down; a prop
   that has no collision box and must never be deleted cannot be in that
   pairing at all without making both facts conditional. */
static Prop FIXED[8]; static int nFIX;
/* the boxes the player walks into.  Kept apart from the brush grid, which
   was built and handed to the bake long before any of this existed. */
static float PB[MAXPROP*6]; static int nPB;

/* ============================================================
   3.  THE PLACER

   Props must not block the route, and that is a
   property of the SPOT, not of the model.  So a spot has to
   pass all of it before anything stands on it.
   ============================================================ */
#define DOOR_CLEAR 3.10f       /* nothing within this of a leaf, ever */
#define KEEP_CLEAR 1.90f       /* nor of a point the story steers by */

static float KX[40],KZ[40]; static int nK;        /* the story's own points */
void propKeepClear(float x,float z){ if(nK<40){ KX[nK]=x; KZ[nK]=z; nK++; } }
/* storyInit is re-entrant now (the ending goes back to the title card) and
   both of the things it hands this file are APPENDS.  This empties them. */
void propStoryReset(void){ nK=0; nFIX=0; }

static int boxFree(float x,float y,float z,float w,float h,float d,float yaw){
  /* the footprint, at three heights, with the corners walked in the prop's
     own frame -- a locker turned 90 degrees is a different footprint */
  float c=cosf(yaw), s=sinf(yaw);
  for(int i=0;i<9;i++){
    float u=((i%3)-1)*w*0.5f, v=((i/3)-1)*d*0.5f;
    float px=x+u*c+v*s, pz=z-u*s+v*c;
    float fy;
    if(!deckAt(px,pz,&fy)) return 0;
    if(fy-y>0.25f||y-fy>0.25f) return 0;          /* not the same deck */
    if(solidAt(px,y+0.20f,pz)) return 0;
    if(solidAt(px,y+h*0.5f,pz)) return 0;
    if(h>1.2f&&solidAt(px,y+h*0.92f,pz)) return 0;
  }
  return 1;
}
/* is there a bulkhead within reach BEHIND it -- what makes a wall prop a
   wall prop rather than a thing standing in the middle of the room */
static int hasWall(float x,float y,float z,float d,float yaw){
  float c=cosf(yaw), s=sinf(yaw);
  for(float t=d*0.5f+0.05f;t<d*0.5f+0.70f;t+=0.12f){
    /* -Z in the prop's frame is its back */
    float px=x - t*s, pz=z - t*c;
    if(solidAt(px,y+0.9f,pz)) return 1;
  }
  return 0;
}
/* and the other half of it: the side that faces the room has to stay walkable */
static int frontClear(float x,float y,float z,float d,float yaw,float want){
  float c=cosf(yaw), s=sinf(yaw);
  for(float t=d*0.5f+0.15f;t<d*0.5f+want;t+=0.22f){
    float px=x+t*s, pz=z+t*c;
    float fy;
    if(!deckAt(px,pz,&fy)) return 0;
    if(solidAt(px,y+0.35f,pz)||solidAt(px,y+1.10f,pz)) return 0;
  }
  return 1;
}
static int tooNear(float x,float z){
  for(int i=0;i<nK;i++){
    float dx=KX[i]-x, dz=KZ[i]-z;
    if(dx*dx+dz*dz<KEEP_CLEAR*KEEP_CLEAR) return 1;
  }
  int nd=adCount();
  for(int i=0;i<nd;i++){
    float dp[2]; adPos(i,dp);
    float dx=dp[0]-x, dz=dp[1]-z;
    if(dx*dx+dz*dz<DOOR_CLEAR*DOOR_CLEAR) return 1;
  }
  return 0;
}
static int propNear(float x,float z,float r){
  for(int i=0;i<nFIX;i++){
    float dx=FIXED[i].x-x, dz=FIXED[i].z-z;
    float w=PMESH[FIXED[i].prop].w, d=PMESH[FIXED[i].prop].d;
    float rr=r+(w>d?w:d)*0.5f;
    if(dx*dx+dz*dz<rr*rr) return 1;
  }
  for(int i=0;i<nPR;i++){
    float dx=PR[i].x-x, dz=PR[i].z-z;
    float w=PMESH[PR[i].prop].w, d=PMESH[PR[i].prop].d;
    float rr=r+(w>d?w:d)*0.5f;
    if(dx*dx+dz*dz<rr*rr) return 1;
  }
  return 0;
}

static int place(int k,float x,float z,float yaw){
  float y;
  float w=kW(k), h=kH(k), d=kD(k);
  if(nPR>=MAXPROP) return 0;
  if(!deckAt(x,z,&y)) return 0;
  if(fabsf(y)>1.30f) return 0;
  if(tooNear(x,z)) return 0;
  if(propNear(x,z,(w>d?w:d)*0.5f+0.35f)) return 0;
  if(!boxFree(x,y,z,w,h,d,yaw)) return 0;
  if(KIND[k].wall && !hasWall(x,y,z,d,yaw)) return 0;
  /* THE ONE TEST THAT MATTERS MOST.  Whatever else is true of a spot, the
     room in front of it has to stay walkable -- 1.25 m, which is two
     shoulders, and 1.70 m for anything tall enough to hide a doorway. */
  if(!frontClear(x,y,z,d,yaw,h>1.5f?1.70f:1.25f)) return 0;
  Prop *P=&PR[nPR++];
  P->x=x; P->y=y; P->z=z; P->yaw=yaw; P->k=(unsigned char)k;
  P->prop=(short)KIND[k].p; P->zi=zoneAt(x,z);
  if(nPB<MAXPROP){
    float *b=PB+nPB*6;
    float ex=(fabsf(cosf(yaw))*w+fabsf(sinf(yaw))*d)*0.5f;
    float ez=(fabsf(sinf(yaw))*w+fabsf(cosf(yaw))*d)*0.5f;
    /* 90% of the footprint: a solid that reaches exactly to the visible
       edge catches the player on a corner they can see daylight past */
    b[0]=x-ex*0.90f; b[1]=y; b[2]=z-ez*0.90f;
    b[3]=x+ex*0.90f; b[4]=y+h*0.94f; b[5]=z+ez*0.90f;
    nPB++;
  }
  return 1;
}

/* THE STORY'S OWN FIXTURES.  The gallery console, the capsule hatch and
   the bay platform are not furniture the placer may or may not find room
   for -- they are the three objects the last act is about, they stand
   where the director says and nowhere else, and the route walk must not
   delete them.  They carry no collision box: the player has to be able to
   stand ON the boarding point, and a prop that blocks the thing it marks
   is worse than no prop. */
void propAddFixed(int prop,float x,float y,float z,float yaw){
  if(nFIX>=8||prop<0||prop>=37) return;
  Prop *P=&FIXED[nFIX++];
  P->x=x; P->y=y; P->z=z; P->yaw=yaw; P->k=0;
  P->prop=(short)prop; P->zi=zoneAt(x,z);
  platLog("props  fixture %-22s at %.2f %.2f %.2f  yaw %+.2f  %.2f x %.2f",
          PK_NAME[prop],x,y,z,yaw,PMESH[prop].w,PMESH[prop].d);
}

/* ---- a room, and what it asks for ----------------------------------
   The plan rectangles, in plan units, and then a list of (kind, count).
   The placer walks a 0.6 m grid inset from the walls, tries the four
   cardinal yaws at each spot, and takes what passes -- so the count is a
   REQUEST.  A store that can only fit nine of the twelve shelves it asked
   for gets nine, and nothing stands in a doorway to make up the number. */
/* the placer's candidate cells for one kind in one room.  The engine
   room is 310 x 90 plan units -- 45 x 13 m -- which is 1,530 cells at
   0.62 m, so this is a cap rather than a bound; past it the far end of
   the longest room is not considered, which costs nothing a player can
   see and keeps the selection O(want x CAND x placed). */
#define CAND 2048
typedef struct { int k,n; } Ask;
typedef struct { float x0,y0,x1,y1; const Ask *a; int na; } Room;

static const Ask A_QTR[]={{RK_BUNK,4},{RK_FOOTLOCKER,4},{RK_JBOX,3},
                          {RK_LOCKER,2},{RK_DRUM,2},{RK_CRATE,3}};
static const Ask A_MNT[]={{RK_SHELF,6},{RK_LOCKER,4},{RK_CRATE,7},
                          {RK_DRUM,4},{RK_JBOX,3},{RK_LADDER,1},
                          {RK_GASRACK,2},{RK_EVA,2}};
static const Ask A_MSS[]={{RK_TABLE,4},{RK_BENCH,5},{RK_DISPENSER,2},
                          {RK_CRATE,4},{RK_DRUM,3},{RK_JBOX,2}};
static const Ask A_LBY[]={{RK_TERMINAL,2},{RK_BENCH,4},{RK_FIRE,3},
                          {RK_JBOX,4},{RK_CRATE,3},{RK_EVA,2},
                          {RK_MONITOR,2}};
static const Ask A_GAL[]={{RK_MONITOR,3},{RK_PEDESTAL,2},{RK_FIRE,2},
                          {RK_JBOX,3},{RK_CRATE,3},{RK_EVA,2}};
static const Ask A_MED[]={{RK_CRYO,4},{RK_MEDBED,2},{RK_CABINET,3},
                          {RK_SPECIMEN,2},{RK_JBOX,2}};
static const Ask A_SUP[]={{RK_SHELF,8},{RK_LOCKER,3},{RK_CRATE,8},
                          {RK_DRUM,4},{RK_LADDER,1},{RK_FIRE,1}};
static const Ask A_CGO[]={{RK_CONTAINER,7},{RK_PALLET,5},{RK_SHELF,4},
                          {RK_DRUM,6},{RK_GASRACK,3},{RK_LADDER,2},
                          {RK_CRATE,6}};
static const Ask A_ENG[]={{RK_VALVE,5},{RK_GENERATOR,3},{RK_COOLANT,4},
                          {RK_FUELCELL,4},{RK_DATARACK,3},{RK_GASRACK,3},
                          {RK_DRONEDOCK,1},{RK_LADDER,2},{RK_JBOX,5},
                          {RK_DRUM,4},{RK_CRATE,4}};

#define RM(x0,y0,x1,y1,a) {x0,y0,x1,y1,a,(int)(sizeof a/sizeof a[0])}
static const Room ROOMS[]={
  RM(160,140,260,215,A_QTR),   RM(430,130,530,240,A_MNT),
  RM(420,300,530,370,A_MSS),   RM(270,400,410,530,A_LBY),
  RM(145,420,203,640,A_GAL),   RM(455,430,535,495,A_MED),
  RM(225,640,315,705,A_SUP),   RM(420,690,530,830,A_CGO),
  RM(185,840,495,930,A_ENG),
};
#define NROOM ((int)(sizeof ROOMS/sizeof ROOMS[0]))

static int fill(float px0,float pz0,float px1,float pz1,
                const Ask *ask,int na,int seed){
  float x0=wx(px0), z0=wz(pz0), x1=wx(px1), z1=wz(pz1);
  int put=0;
  for(int i=0;i<na;i++){
    int want=ask[i].n, got=0;
    int k=ask[i].k;
    float w=kW(k), d=kD(k), h=kH(k);
    /* the walk is offset per kind so two kinds do not queue for the same
       cell, and it is inset by the prop's own half-depth plus a hand */
    float in = (w>d?w:d)*0.5f+0.28f;
    float ox = 0.31f*(float)i, oz = 0.17f*(float)i;
    /* SCAN ORDER IS NOT A LAYOUT.  Taking the first `want` cells that pass,
       row by row from a corner, puts four bunks in a line along one wall
       and leaves the other three quarters of the room empty -- and nothing
       in place() has an opinion about it, because every test in there is
       local: the deck, the footprint, the wall behind, the room in front.
       They are all true of the cell beside the last one too.

       So collect the legal cells first and take them FARTHEST-POINT: each
       pick is the candidate furthest from everything of its own kind
       already down.  Same tests, same counts, and a room that reads as
       furnished rather than as stacked against one wall.  A cell that
       fails placement at all four yaws is simply dropped and the next
       furthest tried, so the count is still a request rather than a
       promise. */
    static float cx[CAND], cz[CAND];
    int nc=0;
    for(float z=z0+in+oz; z<z1-in; z+=0.62f)
      for(float x=x0+in+ox; x<x1-in; x+=0.62f)
        if(nc<CAND){ cx[nc]=x; cz[nc]=z; nc++; }
    int base=nPR;
    while(got<want){
      int best=-1; float bestd=-1.f;
      for(int c=0;c<nc;c++){
        if(cx[c]<-1e8f) continue;                    /* already taken */
        float md=1e9f;
        for(int q=base;q<nPR;q++){
          float dx=PR[q].x-cx[c], dz=PR[q].z-cz[c];
          float d2=dx*dx+dz*dz;
          if(d2<md) md=d2;
        }
        if(md>bestd){ bestd=md; best=c; }
      }
      if(best<0) break;
      float X=cx[best], Z=cz[best];
      cx[best]=-1e9f;
      /* a wall prop faces the middle of the room; a free-standing one
         takes whatever angle its own hash gives it, because a hall of
         furniture all square to the grid reads as a spreadsheet */
      int ok=0;
      for(int t=0;t<4&&!ok;t++){
        float yaw;
        if(KIND[k].wall) yaw=t*1.5708f;
        else yaw = t*1.5708f + (ihash(seed+got*7+t,3,11)-0.5f)*0.55f;
        if(place(k,X,Z,yaw)){ ok=1; got++; put++; }
      }
    }
    /* AND THE ONES THAT STACK, STACK.  The containers are stacked up to
       three high as a stair -- the cargo hold's whole shape is a container
       stair, and a tier is placed on a tier rather than on the deck. */
    if(KIND[k].stack&&got>1){
      int base=nPR-got;
      for(int t=0;t<got-1&&nPR<MAXPROP;t++){
        if(ihash(seed+t*13,5,7)<0.45f) continue;
        Prop *b=&PR[base+t];
        Prop *P=&PR[nPR++];
        *P=*b; P->y=b->y+h;
        P->yaw=b->yaw+(ihash(seed+t,9,3)-0.5f)*0.12f;
        put++;
      }
    }
  }
  return put;
}

void propsInit(void){
  int put=0;
  for(int r=0;r<NROOM;r++){
    const Room *R=&ROOMS[r];
    put+=fill(R->x0,R->y0,R->x1,R->y1,R->a,R->na,r*97+11);
  }
  platLog("props  %d placed  (%d solid boxes, no corridor furniture)",put,nPB);
}

/* what the player walks into.  Same shape as adBoxSolid, and level.c's
   boxSolid asks both. */
int propBoxSolid(float ax0,float ay0,float az0,float ax1,float ay1,float az1){
  for(int i=0;i<nPB;i++){
    const float *b=PB+i*6;
    if(ax1>b[0]&&ax0<b[3]&&ay1>b[1]&&ay0<b[4]&&az1>b[2]&&az0<b[5]) return 1;
  }
  return 0;
}

/* ---- AND THEN THE ROUTES ARE WALKED, AND ANYTHING STANDING ON ONE GOES
   Every test in the placer is local: this spot has a deck, a wall behind
   it, a metre and a quarter in front, no door within three.  All of that
   can be true of a crate that still happens to sit in the one gap the
   route goes through, because a room is not a corridor and the placer
   cannot see a route.  storyClearRoutes walks the nav lattice from the
   player's start to every objective and every landmark and calls this at
   each step; a prop that overlaps the walking cylinder is deleted rather
   than nudged, because the room has two hundred of them and it will not
   be missed.  This is what "props must not block the route" means as a test. */
int propClearAt(float x,float y,float z,float r){
  int gone=0;
  for(int i=0;i<nPR;i++){
    if((unsigned)i>=(unsigned)nPB) break;
    const float *b=PB+i*6;
    if(x+r>b[0]&&x-r<b[3]&&y+1.70f>b[1]&&y<b[4]&&z+r>b[2]&&z-r<b[5]){
      nPR--; nPB--;
      PR[i]=PR[nPR];
      memcpy(PB+i*6,PB+nPB*6,sizeof(float)*6);
      i--; gone++;
    }
  }
  return gone;
}

/* ============================================================
   4.  DRAWING

   One transform per prop, one instance per material group, and
   ONE LIGHT SAMPLE for the whole object: sampleLight fires a
   shadow ray per lamp and a prop is up to eight instances, all
   of them inside two metres of each other.
   ============================================================ */
/* -prop N puts one of these on a card to be compared with the bench,
   and the bench lights its props with a 3.6 key while this ship lights
   a corner with eight per cent of a lamp.  A card photographed in the
   dark compares two darknesses, so the card carries the bench's key
   instead of the room's.  Zero in the game, and the game never sets
   it. */
static float cardKey=0.f;
void propCardLight(float k){ cardKey=k; }

static void drawMesh(int prop,const float *M,int zi,float emisK,float alpha){
  EntFx fx=ENT_PLAIN;
  fx.zone=(float)zi; fx.alpha=alpha;
  float lit[3],ldir[3];
  sampleLight(M[3],M[7]+PMESH[prop].h*0.55f,M[11],lit,ldir);
  if(cardKey>0.f){
    lit[0]=lit[1]=cardKey; lit[2]=cardKey*0.97f;
    ldir[0]=-0.42f; ldir[1]=0.72f; ldir[2]=-0.55f;
    float m=1.f/hypot3(ldir[0],ldir[1],ldir[2]);
    ldir[0]*=m; ldir[1]*=m; ldir[2]*=m;
  }
  for(int g=0;g<PMESH[prop].nGrp;g++){
    const PMat *m=&PMESH[prop].mat[g];
    /* sk 3 is MACHINED and sp 1 turns its plate grid OFF -- the bench's
       geometry already has every panel line it is entitled to, and a
       procedural grid on top of a modelled one is two millworks. */
    fx.rough=m->rough; fx.sk=3.f; fx.sf=0.02f; fx.si=m->metal; fx.sp=1.f;
    fx.rim = m->emis>0.01f?0.10f:0.26f;
    fx.ao  = m->emis>0.01f?1.00f:0.72f;
    for(int k=0;k<3;k++){
      fx.lit[k]=lit[k]; fx.ldir[k]=ldir[k];
      if(m->emis>0.01f&&fx.lit[k]<0.05f) fx.lit[k]=0.05f;
    }
    pushEnt(M,1.f,1.f,1.f,m->col[0],m->col[1],m->col[2],
            m->emis*emisK,&fx,PMESH[prop].prim[g]);
  }
}

/* the same object, anywhere, at any size -- what the pickups are drawn
   with.  A supply crate lying in a corridor and a supply crate in a store
   are the same mesh, which is the entire point of having the bench's. */
void propDrawAt(int prop,float x,float y,float z,float yaw,float scale,
                float emisK){
  if(prop<0||prop>=37) return;
  float A[12],B[12],C[12],M[12];
  mTrans34(x,y,z,A);
  mRotY34(yaw,B); mMul34(A,B,C);
  /* the scale goes in BETWEEN the rotation and the recentring, which is
     the only order that leaves the shrunk prop standing on the point it
     was handed rather than a scaled distance away from it */
  for(int r=0;r<3;r++) for(int c=0;c<3;c++) C[r*4+c]*=scale;
  /* the prop's own origin is not its middle; put its footprint's centre
     on the point it was handed and its feet on the deck */
  mTrans34(-PMESH[prop].cx,-PMESH[prop].y0,-PMESH[prop].cz,B);
  mMul34(C,B,M);
  drawMesh(prop,M,zoneAt(x,z),emisK,1.f);
}
float propHeight(int prop){ return (prop<0||prop>=37)?0.f:PMESH[prop].h; }
float propWidth (int prop){ return (prop<0||prop>=37)?0.f:PMESH[prop].w; }
float propDepth (int prop){ return (prop<0||prop>=37)?0.f:PMESH[prop].d; }
/* the longest side, which is what frames it on the -prop card */
float propSpan(int prop){
  if(prop<0||prop>=37) return 1.f;
  float a=PMESH[prop].w, b=PMESH[prop].h, c=PMESH[prop].d;
  float m=a>b?a:b; return m>c?m:c;
}

#define PROP_DRAW 38.0f
static void drawProp(const Prop *P){
  int prop=P->prop;
  float A[12],B[12],C[12],M[12];
  mTrans34(P->x,P->y,P->z,A);
  mRotY34(P->yaw,B); mMul34(A,B,C);
  mTrans34(-PMESH[prop].cx,-PMESH[prop].y0,-PMESH[prop].cz,B);
  mMul34(C,B,M);
  drawMesh(prop,M,P->zi,1.f,1.f);
}
/* ============================================================
   5.  THE SHADOW A PROP LEAVES ON THE FLOOR IT STANDS ON

   The lightmap is solved before any prop exists and it has to
   be: the placer asks where the doors and the items ended up,
   and those are not settled until long after buildMesh().  So a
   container was lit correctly and the deck under it was lit as
   though the container were not there -- every object in the
   ship floating a centimetre off a floor it did not darken,
   which is exactly the thing that reads as "no shadows".

   Baking them properly means running the whole radiosity solve
   again with 185 more occluders, four and a half seconds the
   boot does not have twice.  What it costs to fake is one pass
   over the scene vertices AFTER the props are down, scaling the
   light channels by how much of the sky each vertex has lost to
   the boxes near it -- which is ambient occlusion, and ambient
   occlusion is what a contact shadow IS.

   It writes MESHV[9..21]: the AO term, the three radiosity
   directions and the switchable bank.  Not the position, not
   the normal, not the material -- so a re-upload is the whole
   of the plumbing, and renderUpload() moved below propsInit()
   rather than being called twice.

   2.7 M vertices against 185 boxes is 500 M tests, so the boxes
   go in a 2.5 m grid first and a vertex looks at the nine cells
   around it.  Measured at the end of this function. */
#define SH_R    1.85f          /* a box stops mattering past this */
#define SH_MAX  0.78f          /* ... and takes at most this much light */
#define SH_CELL 2.5f

void propShadowBake(void){
  if(nPB<=0||nMeshVert<=0) return;
  double t0=platTime();
  float x0=1e9f,z0=1e9f,x1=-1e9f,z1=-1e9f;
  for(int i=0;i<nPB;i++){
    const float *b=PB+i*6;
    if(b[0]<x0)x0=b[0]; if(b[2]<z0)z0=b[2];
    if(b[3]>x1)x1=b[3]; if(b[5]>z1)z1=b[5];
  }
  x0-=SH_R+1.f; z0-=SH_R+1.f; x1+=SH_R+1.f; z1+=SH_R+1.f;
  int nx=(int)((x1-x0)/SH_CELL)+1, nz=(int)((z1-z0)/SH_CELL)+1;
  if(nx<1||nz<1||nx>4096||nz>4096) return;
  int *head=(int*)malloc(sizeof(int)*nx*nz);
  int *next=(int*)malloc(sizeof(int)*nPB*64);
  int *item=(int*)malloc(sizeof(int)*nPB*64);
  if(!head||!next||!item){ free(head);free(next);free(item); return; }
  for(int i=0;i<nx*nz;i++) head[i]=-1;
  int nw=0, cap=nPB*64;
  for(int i=0;i<nPB;i++){
    const float *b=PB+i*6;
    int cx0=(int)((b[0]-SH_R-x0)/SH_CELL), cx1=(int)((b[3]+SH_R-x0)/SH_CELL);
    int cz0=(int)((b[2]-SH_R-z0)/SH_CELL), cz1=(int)((b[5]+SH_R-z0)/SH_CELL);
    if(cx0<0)cx0=0; if(cz0<0)cz0=0;
    if(cx1>=nx)cx1=nx-1; if(cz1>=nz)cz1=nz-1;
    for(int cz=cz0;cz<=cz1&&nw<cap;cz++)
      for(int cx=cx0;cx<=cx1&&nw<cap;cx++){
        int c=cz*nx+cx;
        item[nw]=i; next[nw]=head[c]; head[c]=nw; nw++;
      }
  }
  int touched=0;
  for(int v=0;v<nMeshVert;v++){
    float *o=MESHV+(size_t)v*VSTRIDE;
    float px=o[0], py=o[1], pz=o[2];
    int cx=(int)((px-x0)/SH_CELL), cz=(int)((pz-z0)/SH_CELL);
    if(cx<0||cz<0||cx>=nx||cz>=nz) continue;
    int c=cz*nx+cx;
    if(head[c]<0) continue;
    float occ=0.f;
    for(int e=head[c];e>=0;e=next[e]){
      const float *b=PB+item[e]*6;
      /* distance from the point to the box, zero inside it */
      float dx = px<b[0] ? b[0]-px : (px>b[3] ? px-b[3] : 0.f);
      float dy = py<b[1] ? b[1]-py : (py>b[4] ? py-b[4] : 0.f);
      float dz = pz<b[2] ? b[2]-pz : (pz>b[5] ? pz-b[5] : 0.f);
      float d=hypot3(dx,dy,dz);
      if(d>=SH_R) continue;
      float k=1.f-d/SH_R; k*=k;
      /* AND THE SURFACE HAS TO BE ABLE TO SEE IT.  Without this the
         far side of a bulkhead darkens because the container on the
         other side of it is 30 cm away, which is a shadow through a
         wall -- the one artefact that would make this worse than no
         shadow at all. */
      float mx=(b[0]+b[3])*0.5f-px, my=(b[1]+b[4])*0.5f-py,
            mz=(b[2]+b[5])*0.5f-pz;
      float ml=hypot3(mx,my,mz); if(ml<1e-4f) ml=1e-4f;
      float f=(mx*o[3]+my*o[4]+mz*o[5])/ml;
      if(f<=0.f) continue;
      k*=f;
      occ = occ + k*(1.f-occ);           /* many boxes, one shadow */
    }
    if(occ<=0.002f) continue;
    float sc=1.f-SH_MAX*occ;
    for(int k=9;k<=21;k++) o[k]*=sc;     /* ao, the three bands, the bank */
    touched++;
  }
  free(head); free(next); free(item);
  platLog("shadow %d props darkened %d of %d verts  %.0f ms",
          nPB,touched,nMeshVert,(platTime()-t0)*1000.0);
}

void propsDraw(float px,float pz){
  for(int i=0;i<nFIX;i++){
    float dx=FIXED[i].x-px, dz=FIXED[i].z-pz;
    if(dx*dx+dz*dz>PROP_DRAW*PROP_DRAW*2.f) continue;   /* they are big */
    drawProp(&FIXED[i]);
  }
  for(int i=0;i<nPR;i++){
    Prop *P=&PR[i];
    float dx=P->x-px, dz=P->z-pz, d2=dx*dx+dz*dz;
    if(d2>PROP_DRAW*PROP_DRAW) continue;
    drawProp(P);
  }
}
