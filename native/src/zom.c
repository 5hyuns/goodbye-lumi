/* ============================================================
   G4 + G8 + G8b + G15, ported.

   NAVIGATION is a half-metre lattice of standing surfaces linked
   where something crouched could get from one to the next, solved
   into a flow field: every cell knows which neighbour is downhill
   toward the player, so a crowd costs one array read per body per
   step instead of twenty path searches.

   THE CREATURE GRAPH is the reason this ship's enemy is a spider.
   Every node is a span -- it knows where it starts, where it ends,
   and it hands its tip to whatever attaches next -- and that is
   the whole attachment rule.  A chain, a leg and a mandible are
   the same twenty lines with different numbers.

   THE GAIT is not scheduled.  Each foot keeps a home spot under
   the body, steps when it has drifted too far from it, and may
   only leave the ground while the opposite tripod is down.  That
   last rule is the entire walk cycle: nothing sequences the legs,
   they sequence each other.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "app.h"
#include "prim.h"
#include "ent.h"
#include "fx.h"
#include "zom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int   rayTraceOne(float ox,float oy,float oz,float dx,float dy,float dz,
                  float maxT,int skip);
extern float rayHitDist;
extern int   rayHitAx, rayHitSg;
int   boxSolid(float ax0,float ay0,float az0,float ax1,float ay1,float az1);
void  sfxCreature(int kind,float x,float y,float z,float sz); /* audio.c */
void  sfxStep(float x,float y,float z,float sz);

/* ---- a cheap stream, for gameplay only ------------------------------
   Nothing here feeds a mesh, so it does not have to agree with the
   browser's Math.random the way mulberry32 in vmw.c has to. */
static unsigned int rs=0x9E3779B9u;
static float rnd(void){
  rs^=rs<<13; rs^=rs>>17; rs^=rs<<5;
  return (float)(rs&0xFFFFFF)/16777216.f;
}

/* ============================================================
   G4.  NAVIGATION
   ============================================================ */
#define NC       0.5f
#define NAV_X0  -34.0f
#define NAV_Z0   -2.0f
#define NAV_NX   128
#define NAV_NZ   242
#define NAV_CLEAR 1.52f
#define NAV_STEP  0.60f
#define NAV_DROP  3.60f

static float *nvX,*nvY,*nvZ; static int nvN;
static int   *nvCell; static unsigned char *nvCnt;
static int   *inOff,*inNode; static float *inCost;
static float navCX(int i){ return NAV_X0+(i+0.5f)*NC; }
static float navCZ(int j){ return NAV_Z0+(j+0.5f)*NC; }

static int navSolid(float x,float y,float z){ return solidAt(x,y,z); }

static void buildNav(void){
  double t0=platTime();
  int cap=1<<16, n=0;
  float *xs=(float*)malloc(cap*4),*ys=(float*)malloc(cap*4),*zs=(float*)malloc(cap*4);
  nvCell=(int*)malloc(NAV_NX*NAV_NZ*sizeof(int));
  nvCnt =(unsigned char*)calloc(NAV_NX*NAV_NZ,1);
  for(int i=0;i<NAV_NX*NAV_NZ;i++) nvCell[i]=-1;

  float hs[8];
  for(int j=0;j<NAV_NZ;j++) for(int i=0;i<NAV_NX;i++){
    float cx=navCX(i), cz=navCZ(j);
    int nh=0;
    for(int b=0;b<nBrush;b++){
      const Brush *B=BRUSHES+b;
      if(cx<B->x0||cx>B->x1||cz<B->z0||cz>B->z1) continue;
      float top=B->y1;
      if(top<-0.5f||top>7.0f) continue;
      int dup=0;
      for(int q=0;q<nh;q++) if(fabsf(hs[q]-top)<0.14f){ dup=1; break; }
      if(!dup&&nh<8) hs[nh++]=top;
    }
    if(!nh) continue;
    for(int a=1;a<nh;a++){ float v=hs[a]; int b2=a-1;
      while(b2>=0&&hs[b2]>v){ hs[b2+1]=hs[b2]; b2--; } hs[b2+1]=v; }
    int ci=j*NAV_NX+i;
    for(int q=0;q<nh&&nvCnt[ci]<3;q++){
      float h=hs[q];
      if(navSolid(cx,h+0.14f,cz)||navSolid(cx,h+0.70f,cz)||
         navSolid(cx,h+NAV_CLEAR,cz)) continue;
      if(nvCell[ci]<0) nvCell[ci]=n;
      if(n>=cap){ cap*=2; xs=(float*)realloc(xs,cap*4);
                  ys=(float*)realloc(ys,cap*4); zs=(float*)realloc(zs,cap*4); }
      xs[n]=cx; ys[n]=h; zs[n]=cz; n++; nvCnt[ci]++;
    }
  }
  nvN=n; nvX=xs; nvY=ys; nvZ=zs;

  /* ---- link.  Drops are one-way and a diagonal has to have both of its
     square neighbours clear, or a body cuts a corner through a doorpost. */
  static const int DIR[8][2]={{1,0},{-1,0},{0,1},{0,-1},
                              {1,1},{1,-1},{-1,1},{-1,-1}};
  int ecap=1<<18, nE=0;
  int *ea=(int*)malloc(ecap*4),*eb=(int*)malloc(ecap*4);
  float *ec=(float*)malloc(ecap*4);
  for(int j=0;j<NAV_NZ;j++) for(int i=0;i<NAV_NX;i++){
    int ci=j*NAV_NX+i; if(nvCell[ci]<0) continue;
    for(int a=nvCell[ci];a<nvCell[ci]+nvCnt[ci];a++){
      float ha=nvY[a], ax=nvX[a], az=nvZ[a];
      for(int d=0;d<8;d++){
        int i2=i+DIR[d][0], j2=j+DIR[d][1];
        if(i2<0||j2<0||i2>=NAV_NX||j2>=NAV_NZ) continue;
        int c2=j2*NAV_NX+i2; if(nvCell[c2]<0) continue;
        int diag=d>3;
        for(int m=nvCell[c2];m<nvCell[c2]+nvCnt[c2];m++){
          float dy=nvY[m]-ha;
          if(dy>NAV_STEP||dy<-NAV_DROP) continue;
          float base=ha>nvY[m]?ha:nvY[m];
          float mx=(ax+nvX[m])*0.5f, mz=(az+nvZ[m])*0.5f;
          if(navSolid(mx,base+0.12f,mz)||navSolid(mx,base+0.68f,mz)||
             navSolid(mx,base+1.20f,mz)) continue;
          if(diag){
            if(navSolid(nvX[m],base+0.12f,az)||navSolid(nvX[m],base+0.68f,az)||
               navSolid(nvX[m],base+1.20f,az)) continue;
            if(navSolid(ax,base+0.12f,nvZ[m])||navSolid(ax,base+0.68f,nvZ[m])||
               navSolid(ax,base+1.20f,nvZ[m])) continue;
          }
          if(nE>=ecap){ ecap*=2; ea=(int*)realloc(ea,ecap*4);
                        eb=(int*)realloc(eb,ecap*4); ec=(float*)realloc(ec,ecap*4); }
          ea[nE]=a; eb[nE]=m;
          ec[nE]=(diag?0.707f:0.5f)+(dy<0?-dy*0.9f:0)+(dy>0.25f?0.5f:0);
          nE++;
        }
      }
    }
  }
  /* CSR, reversed: the field is solved backward from the goal */
  inOff=(int*)calloc(nvN+1,sizeof(int));
  for(int e=0;e<nE;e++) inOff[eb[e]+1]++;
  for(int i=0;i<nvN;i++) inOff[i+1]+=inOff[i];
  inNode=(int*)malloc(nE*sizeof(int)); inCost=(float*)malloc(nE*4);
  int *cur=(int*)malloc((nvN+1)*sizeof(int));
  memcpy(cur,inOff,(nvN+1)*sizeof(int));
  for(int e=0;e<nE;e++){ int p=cur[eb[e]]++; inNode[p]=ea[e]; inCost[p]=ec[e]; }
  free(cur); free(ea); free(eb); free(ec);
  platLog("nav    %d nodes, %d links, %.0f ms",nvN,nE,(platTime()-t0)*1000.0);
}

static int navNodeAt(float x,float y,float z){
  int i=(int)floorf((x-NAV_X0)/NC), j=(int)floorf((z-NAV_Z0)/NC);
  if(i<0||j<0||i>=NAV_NX||j>=NAV_NZ) return -1;
  int ci=j*NAV_NX+i; if(nvCell[ci]<0) return -1;
  int best=-1; float bd=1e9f;
  for(int n=nvCell[ci];n<nvCell[ci]+nvCnt[ci];n++){
    float d=fabsf(nvY[n]-y);
    if(nvY[n]<=y+0.8f&&d<bd){ bd=d; best=n; }
  }
  return best<0?nvCell[ci]:best;
}

/* binary heap keyed on cost */
static float *hpK; static int *hpV, hpN;
static void hpPush(float k,int v){
  int i=hpN++; hpK[i]=k; hpV[i]=v;
  while(i>0){ int p=(i-1)>>1; if(hpK[p]<=hpK[i]) break;
    float tk=hpK[p]; int tv=hpV[p];
    hpK[p]=hpK[i]; hpV[p]=hpV[i]; hpK[i]=tk; hpV[i]=tv; i=p; }
}
static int hpPop(void){
  int top=hpV[0]; hpN--;
  if(hpN>0){ hpK[0]=hpK[hpN]; hpV[0]=hpV[hpN];
    int i=0;
    for(;;){ int l=i*2+1,r=l+1,s=i;
      if(l<hpN&&hpK[l]<hpK[s]) s=l;
      if(r<hpN&&hpK[r]<hpK[s]) s=r;
      if(s==i) break;
      float tk=hpK[s]; int tv=hpV[s];
      hpK[s]=hpK[i]; hpV[s]=hpV[i]; hpK[i]=tk; hpV[i]=tv; i=s; } }
  return top;
}
static float *distPly; static int *nextPly;
static float *distGoal; static int *nextGoal; static int goalNode=-2;
static float *distFly; static int *nextFly; static int flyNode=-2;
static void solveField(int goal,float *dist,int *next){
  for(int i=0;i<nvN;i++){ dist[i]=1e9f; next[i]=-1; }
  hpN=0;
  if(goal<0) return;
  dist[goal]=0; hpPush(0,goal);
  while(hpN>0){
    int u=hpPop(); float du=dist[u];
    for(int e=inOff[u];e<inOff[u+1];e++){
      int v=inNode[e];
      float nd=du+inCost[e];
      if(nd<dist[v]-1e-4f){ dist[v]=nd; next[v]=u; hpPush(nd,v); }
    }
  }
}

/* ============================================================
   G8.  THE FOUR ROLES

   One animal.  The scuttler is the previz's stalker shrunk to
   swarm size, the brooder is the same creature at half again,
   and the spitter is a paler one that has stopped walking.  The
   cocoon is the only shape that is not a spider, and that is the
   point of it: it reads as something a spider MADE.
   ============================================================ */
typedef struct {
  float hp,spd,dmg,sc,rad,hgt,mass,gib,reach,swing;
  float coat[3],flesh[3],trous[3],gore[3];
} ZType;

/* the design numbers, wearing the previz's colours */
static const ZType ZT[Z_NTYPE]={
 /* SCUTTLER  M1 */ {24, 3.20f, 8, 0.74f,0.26f,0.86f,0.55f, 60,1.35f,0.28f,
   {0.082f,0.066f,0.098f},{0.135f,0.050f,0.100f},
   {0.40f,0.36f,0.29f},{0.34f,0.05f,0.16f}},
 /* SPITTER   M2 */ {40, 1.10f,14, 0.92f,0.40f,1.00f,0.80f,110,1.60f,0.40f,
   {0.105f,0.098f,0.062f},{0.180f,0.135f,0.055f},
   {0.44f,0.40f,0.30f},{0.30f,0.26f,0.05f}},
 /* BROODER   M3 */ {180,2.60f,28, 1.62f,0.50f,1.55f,3.20f,420,2.10f,0.55f,
   {0.070f,0.056f,0.078f},{0.155f,0.045f,0.090f},
   {0.36f,0.32f,0.26f},{0.32f,0.05f,0.14f}},
 /* COCOON    M4 */ {60, 0.00f, 0, 1.05f,0.42f,1.10f,2.00f,200,0.00f,0.40f,
   {0.150f,0.128f,0.104f},{0.220f,0.130f,0.120f},
   {0.34f,0.30f,0.24f},{0.30f,0.12f,0.10f}},
};

#define MAXZ 80
typedef struct {
  int   used,id,type,enc;
  float x,y,z,yaw,vy;
  float hp,maxhp;
  int   grounded,state,swung,dead,gibbed,headGone;
  float phase,swing,flash,stagger,atkCd,litT,groanT,deadT;
  float fallZ,lean,spawnT,aggro,slow;
  float lit[3],ldir[3];
  float nudgeX,nudgeZ;
  float vx,vz,lx,lz;
  float feet[18],fA[18],fB[18],fT[6];
  float crouch,crouchT,pounceCd,backoff;
  float stuckT,sx0,sz0;        /* the watchdog: wanted to move and did not */
  int   circle;
  int   mounted;               /* spitter and cocoon do not walk */
  int   perched;               /* CEILING: it holds its height, it does not fall */
  float fireCd,aimT;           /* the spitter's three-second warning */
  float kidT; int kids;        /* the cocoon's brood */
} Zom;

static Zom Z[MAXZ];
static int zSerial=0, zKills=0;
/* AND WHAT WAS KILLED, NOT ONLY HOW MANY.  The story has one line that
   is about the size of the thing on the deck, and 'the encounter is
   over' cannot tell it whether the big one was in it. */
static int zKillT[Z_NTYPE];
/* AND WHERE IT FELL.  A body that leaves something behind is a decision
   the STORY makes -- what drops, how often, whether the run needs it --
   and this file has no business knowing any of that.  What it does know
   is that something died at a point, so it says so once, into a ring the
   story drains every frame.  Sixteen is four times the most that has ever
   died in one frame (a shotgun through a doorway takes three); a full
   ring drops the oldest, because a drop that never appears is better than
   a kill that stalls the pool. */
#define KDQ 16
static struct { float x,y,z; int type; } KDQ_[KDQ];
static int kdHead,kdTail;
static float PX=0,PY=0,PZ=0; static int pVisible=1;
static float fieldT=0;

/* the acid the spitter throws */
#define MAXBOLT 24
static struct { int used; float x,y,z,vx,vy,vz,life; } BOLT[MAXBOLT];

void zomTarget(float x,float y,float z,int vis){ PX=x; PY=y; PZ=z; pVisible=vis; }
int  zomKills(void){ return zKills; }
int  zomKillsOf(int t){ return (t>=0&&t<Z_NTYPE)? zKillT[t] : 0; }
int  zomTakeDeath(float *o){
  if(kdHead==kdTail) return 0;
  o[0]=KDQ_[kdHead].x; o[1]=KDQ_[kdHead].y; o[2]=KDQ_[kdHead].z;
  int t=KDQ_[kdHead].type;
  kdHead=(kdHead+1)%KDQ;
  return t+1;                                  /* 0 means "nothing left" */
}
void zomPos(int i,float *o){ o[0]=Z[i].x; o[1]=Z[i].y; o[2]=Z[i].z; }
void zomPerch(int i){ if(i>=0&&i<MAXZ) Z[i].perched=1; }

/* is there a route from here to the player at all, and how long */
/* the closest one that is actually up and hunting -- an unused slot keeps
   the coordinates of whatever died in it, so a trace that walks the array
   blind reports a ghost that never moves again */
int zomNearest(float x,float z,float *out){
  int best=-1; float bd=1e9f;
  for(int i=0;i<MAXZ;i++){
    if(!Z[i].used||Z[i].dead) continue;
    float d=hypot2(Z[i].x-x,Z[i].z-z);
    if(d<bd){ bd=d; best=i; }
  }
  if(best>=0&&out){ out[0]=Z[best].x; out[1]=Z[best].y; out[2]=Z[best].z; out[3]=bd; }
  return best;
}
float zomFieldAt(float x,float y,float z){
  int n=navNodeAt(x,y,z);
  return (n<0)?-1.f:distPly[n];
}
int zomLive(void){
  int n=0;
  for(int i=0;i<MAXZ;i++) if(Z[i].used&&!Z[i].dead&&Z[i].type!=Z_COCOON) n++;
  return n;
}
int zomLiveIn(int enc){
  int n=0;
  for(int i=0;i<MAXZ;i++) if(Z[i].used&&!Z[i].dead&&Z[i].enc==enc) n++;
  return n;
}
int zomCount(void){
  int n=0; for(int i=0;i<MAXZ;i++) if(Z[i].used) n++; return n;
}
void zomKillAll(void){
  for(int i=0;i<MAXZ;i++) Z[i].used=0;
  for(int i=0;i<MAXBOLT;i++) BOLT[i].used=0;
}
void zomReset(void){ zomKillAll(); zKills=0;
  kdHead=kdTail=0;
  for(int i=0;i<Z_NTYPE;i++) zKillT[i]=0; }
int zomBoltCount(void){
  int n=0; for(int i=0;i<MAXBOLT;i++) if(BOLT[i].used) n++; return n;
}

/* ---- the feet.  Six home spots under the body, and a step is only
   allowed to start when the opposite tripod is down. ----------------- */
static const float SLEGX[6]={-0.60f,0.60f,-0.70f,0.70f,-0.58f,0.58f};
static const float SLEGZ[6]={ 0.54f,0.54f, 0.04f,0.04f,-0.52f,-0.52f};
#define STRIPOD(i) ((((i)>>1)+((i)&1))&1)

static void stalkerInit(Zom *z){
  float s=ZT[z->type].sc, c=cosf(z->yaw), sn=sinf(z->yaw);
  for(int i=0;i<6;i++){
    float lx=SLEGX[i]*s, lz=SLEGZ[i]*s;
    z->feet[i*3  ]=z->x+lx*c+lz*sn;
    z->feet[i*3+1]=z->y;
    z->feet[i*3+2]=z->z-lx*sn+lz*c;
    z->fT[i]=-1;
  }
}

int zomSpawn(int type,float x,float y,float z,float yaw,int enc){
  int i=-1;
  for(int k=0;k<MAXZ;k++) if(!Z[k].used){ i=k; break; }
  if(i<0){                                  /* the oldest corpse gives way */
    float best=-1;
    for(int k=0;k<MAXZ;k++) if(Z[k].dead&&Z[k].deadT>best){ best=Z[k].deadT; i=k; }
    if(i<0) return -1;
  }
  Zom *o=Z+i;
  memset(o,0,sizeof *o);
  o->used=1; o->id=zSerial++; o->type=type; o->enc=enc;
  o->x=x; o->y=y; o->z=z;
  o->yaw = yaw<-9 ? rnd()*6.2832f : yaw;
  o->hp=o->maxhp=ZT[type].hp;
  o->phase=rnd()*6.2832f;
  o->litT=rnd()*0.2f;
  o->groanT=1.f+rnd()*4.f;
  o->spawnT=0.35f;
  o->pounceCd=0.5f+rnd()*1.1f;
  o->circle=rnd()<0.5f?-1:1;
  o->ldir[1]=1;
  o->lx=x; o->lz=z;
  o->mounted = (type==Z_SPITTER||type==Z_COCOON);
  o->fireCd=1.2f+rnd()*1.5f;
  o->kidT=6.f;
  stalkerInit(o);
  return i;
}

/* ---- collision ------------------------------------------------------ */
static int cylSolid(float x,float y,float z,float r,float h){
  return boxSolid(x-r,y+0.09f,z-r,x+r,y+h,z+r);
}
static int zMoveTry(Zom *z,float dx,float dz){
  float r=ZT[z->type].rad, h=ZT[z->type].hgt;
  int moved=0;
  if(dx!=0){
    if(!cylSolid(z->x+dx,z->y,z->z,r,h)){ z->x+=dx; moved=1; }
    else if(z->grounded&&!cylSolid(z->x+dx,z->y+0.55f,z->z,r,h)){
      z->y+=0.55f; z->x+=dx; moved=1; }
  }
  if(dz!=0){
    if(!cylSolid(z->x,z->y,z->z+dz,r,h)){ z->z+=dz; moved=1; }
    else if(z->grounded&&!cylSolid(z->x,z->y+0.55f,z->z+dz,r,h)){
      z->y+=0.55f; z->z+=dz; moved=1; }
  }
  return moved;
}
/* THE FLOW FIELD ROUTES A POINT AND THE ANIMAL IS A BOX.  buildNav decides a
   cell is standable with three point samples up its centre line, so it
   happily links a gap narrower than the body that has to walk it -- and a
   creature told to go west into a bulkhead it cannot pass just stands there
   pressing on it for the rest of the fight.  Sweeping the heading thirty
   degrees at a time until something opens is what turns that into wall
   following, which is what it looks like an animal should do anyway. */
static void zMove(Zom *z,float dx,float dz){
  if(zMoveTry(z,dx,dz)) return;
  if(hypot2(dx,dz)<1e-5f) return;
  for(int k=1;k<=4;k++){
    float a=k*0.5236f;
    for(int s=-1;s<=1;s+=2){
      float ca=cosf(a*s), sa=sinf(a*s);
      float nx=dx*ca-dz*sa, nz=dx*sa+dz*ca;
      if(zMoveTry(z,nx,nz)) return;
    }
  }
}
static int blocked(float ox,float oy,float oz,float dx,float dy,float dz,float len){
  if(len<=0.02f) return 0;
  return rayTraceOne(ox,oy,oz,dx,dy,dz,len,-1)>=0;
}

static float groundY(float x,float z,float fb,float s){
  float top=fb+0.85f*s;
  int b=rayTraceOne(x,top,z,0,-1,0,1.9f*s,-1);
  float d=rayHitDist;                      /* read it before anything traces again */
  return b<0?fb:top-d;
}

static void stepFeet(Zom *z,float dt){
  float s=ZT[z->type].sc, c=cosf(z->yaw), sn=sinf(z->yaw);
  float ivx=(z->x-z->lx)/(dt>1e-4f?dt:1e-4f);
  float ivz=(z->z-z->lz)/(dt>1e-4f?dt:1e-4f);
  z->lx=z->x; z->lz=z->z;
  float k=dt*9.f; if(k>1)k=1;
  z->vx+=(ivx-z->vx)*k; z->vz+=(ivz-z->vz)*k;
  if(!z->grounded){
    /* mid-pounce there is no floor to step on, so the legs gather under
       the body and reach for the landing instead of trailing behind it */
    float g=dt*20.f; if(g>1)g=1;
    for(int i=0;i<6;i++){
      float lx=SLEGX[i]*s, lz=SLEGZ[i]*s;
      z->fT[i]=-1;
      z->feet[i*3  ]+=(z->x+lx*c+lz*sn-z->feet[i*3  ])*g;
      z->feet[i*3+1]+=(z->y-0.06f*s-z->feet[i*3+1])*g;
      z->feet[i*3+2]+=(z->z-lx*sn+lz*c-z->feet[i*3+2])*g;
    }
    return;
  }
  float spd=hypot2(z->vx,z->vz);
  float thr=(0.22f+(spd*0.06f<0.20f?spd*0.06f:0.20f))*s;
  float lead=spd*0.10f; if(lead>0.40f) lead=0.40f;
  float stepT=0.50f/(spd+1.8f);
  if(stepT<0.07f) stepT=0.07f; if(stepT>0.20f) stepT=0.20f;

  int anyAir=0;
  for(int i=0;i<6;i++) if(z->fT[i]>=0){ anyAir=1; break; }
  if(!anyAir){
    float drift[6]; int want=-1; float worst=thr;
    for(int i=0;i<6;i++){
      float lx=SLEGX[i]*s, lz=SLEGZ[i]*s;
      float dx=z->feet[i*3]-(z->x+lx*c+lz*sn);
      float dz=z->feet[i*3+2]-(z->z-lx*sn+lz*c);
      drift[i]=hypot2(dx,dz);
      if(drift[i]>worst){ worst=drift[i]; want=STRIPOD(i); }
    }
    if(want>=0) for(int i=0;i<6;i++){
      if(STRIPOD(i)!=want||drift[i]<thr*0.30f) continue;
      float lx=SLEGX[i]*s, lz=SLEGZ[i]*s;
      float hx=z->x+lx*c+lz*sn, hz=z->z-lx*sn+lz*c;
      z->fA[i*3]=z->feet[i*3]; z->fA[i*3+1]=z->feet[i*3+1]; z->fA[i*3+2]=z->feet[i*3+2];
      float bx=hx+z->vx*lead, bz=hz+z->vz*lead;
      float by=groundY(bx,bz,z->y,s);
      z->fB[i*3]=bx; z->fB[i*3+1]=by; z->fB[i*3+2]=bz;
      z->fT[i]=0;
    }
  }
  for(int i=0;i<6;i++){
    if(z->fT[i]<0) continue;
    z->fT[i]+=dt/stepT;
    if(z->fT[i]>=1){
      z->feet[i*3]=z->fB[i*3]; z->feet[i*3+1]=z->fB[i*3+1]; z->feet[i*3+2]=z->fB[i*3+2];
      z->fT[i]=-1;
    } else {
      float u=z->fT[i], w=u*u*(3-2*u);
      z->feet[i*3  ]=z->fA[i*3  ]+(z->fB[i*3  ]-z->fA[i*3  ])*w;
      z->feet[i*3+2]=z->fA[i*3+2]+(z->fB[i*3+2]-z->fA[i*3+2])*w;
      z->feet[i*3+1]=z->fA[i*3+1]+(z->fB[i*3+1]-z->fA[i*3+1])*w
                     +sinf(u*3.1416f)*0.21f*s;
    }
  }
}

/* ---- the four hit spheres ------------------------------------------
   Low and long: the head is out in front rather than up top, so the
   shot that would kill a man walks straight over this animal. */
static void zSpheres(const Zom *z,float *out){
  float s=ZT[z->type].sc, c=cosf(z->yaw), sn=sinf(z->yaw);
  float dp=z->dead?0.30f:z->crouch*0.20f;
  static const float LX[4]={0,0,0,0};
  const float LY[4]={0.60f-dp,0.64f-dp,0.66f-dp,0.38f-dp};
  const float LZ[4]={0.60f,0.06f,-0.44f,0.00f};
  const float R [4]={0.200f,0.300f,0.340f,0.250f};
  for(int i=0;i<4;i++){
    out[i*4  ]=z->x+LX[i]*c+LZ[i]*sn;
    out[i*4+1]=z->y+LY[i]*s;
    out[i*4+2]=z->z-LX[i]*sn+LZ[i]*c;
    out[i*4+3]=R[i]*s;
  }
}
static float raySphere(float ox,float oy,float oz,float dx,float dy,float dz,
                       float cx,float cy,float cz,float r){
  float ex=cx-ox, ey=cy-oy, ez=cz-oz;
  float b=ex*dx+ey*dy+ez*dz;
  float c=ex*ex+ey*ey+ez*ez-r*r;
  if(c>0&&b<0) return -1;
  float d=b*b-c;
  if(d<0) return -1;
  float s=sqrtf(d), t=b-s;
  return t<0?(b+s):t;
}

int zomTrace(float ox,float oy,float oz,float dx,float dy,float dz,
             float range,float *tOut,int *headOut){
  int best=-1; float bt=range; int bh=0;
  float sp[16];
  for(int i=0;i<MAXZ;i++){
    if(!Z[i].used||Z[i].dead) continue;
    float ddx=Z[i].x-ox, ddz=Z[i].z-oz;
    if(ddx*ddx+ddz*ddz>(range+2)*(range+2)) continue;
    zSpheres(Z+i,sp);
    for(int k=0;k<4;k++){
      float t=raySphere(ox,oy,oz,dx,dy,dz,sp[k*4],sp[k*4+1],sp[k*4+2],sp[k*4+3]);
      if(t>0.05f&&t<bt){ bt=t; best=i; bh=(k==0); }
    }
  }
  if(best>=0){ *tOut=bt; *headOut=bh; }
  return best;
}

/* ---- gore ----------------------------------------------------------- */
static void bloodSpray(float x,float y,float z,float dx,float dy,float dz,
                       int amount,const float *gore){
  for(int i=0;i<amount;i++){
    float sp=1.2f+rnd()*5.5f;
    float jx=dx+(rnd()-0.5f)*1.15f;
    float jy=dy+(rnd()-0.5f)*1.0f+0.25f;
    float jz=dz+(rnd()-0.5f)*1.15f;
    float sz=0.045f+rnd()*0.075f;
    float lf=0.35f+rnd()*0.5f;
    int   kd=rnd()<0.35f?PK_CHUNK:PK_BLOB;
    float tint=0.7f+rnd()*0.6f;
    float spin=(rnd()-0.5f)*10.f;
    fxEmit(x,y,z,jx*sp,jy*sp,jz*sp,sz,lf,kd,
           gore[0]*tint,gore[1],gore[2],0.9f,9,1.6f,0,spin);
  }
  int mist=amount>>2; if(mist<2) mist=2;
  for(int i=0;i<mist;i++){
    float vx=(rnd()-0.5f)*0.9f, vy=(rnd()-0.5f)*0.6f+0.2f, vz=(rnd()-0.5f)*0.9f;
    float sz=0.14f+rnd()*0.18f, lf=0.5f+rnd()*0.4f;
    fxEmit(x,y,z,vx,vy,vz,sz,lf,PK_BLOB,
           gore[0]*0.55f,gore[1]*0.7f,gore[2]*0.7f,0.34f,1.5f,2.2f,0,0);
  }
}
static void bloodDecal(float x,float y,float z,float dx,float dy,float dz,
                       const float *gore){
  int b=rayTraceOne(x,y,z,dx,dy,dz,4.2f,-1);
  if(b<0) return;
  float d=rayHitDist; int ax=rayHitAx, sg=rayHitSg;
  float n[3]={0,0,0}; n[ax]=(float)sg;
  float sz=0.36f+rnd()*0.55f, al=0.62f+rnd()*0.3f;
  fxDecal(x+dx*d,y+dy*d,z+dz*d,n[0],n[1],n[2],sz,
          gore[0]*0.7f,gore[1]*0.7f,gore[2]*0.7f,al,0);
}
static void gibBurst(Zom *z,int n,float power){
  const ZType *t=ZT+z->type;
  for(int i=0;i<n;i++){
    float a=rnd()*6.28f, e=0.2f+rnd()*0.9f;
    float sp=(1.6f+rnd()*4.2f)*power;
    float sz=(0.07f+rnd()*0.13f)*t->sc;
    float ox=(rnd()-0.5f)*0.4f, oy=0.5f+rnd()*0.8f*t->sc, oz=(rnd()-0.5f)*0.4f;
    float sy=sz*(0.7f+rnd()*0.8f);
    float tint=0.8f+rnd()*0.5f, life=7.f+rnd()*4.f;
    fxDebris(z->x+ox,z->y+oy,z->z+oz,
             cosf(a)*sp,e*sp*1.5f,sinf(a)*sp, sz,sy,sz,
             t->gore[0]*tint,t->gore[1],t->gore[2], life,1,0);
  }
}
static void killZom(Zom *z,float dmg,float dx,float dz,int head,int blast){
  const ZType *t=ZT+z->type;
  z->dead=1; z->deadT=0; z->hp=0;
  z->fallZ=(rnd()-0.5f)*1.4f;
  zKills++;
  zKillT[z->type]++;
  { int n=(kdTail+1)%KDQ;
    if(n==kdHead) kdHead=(kdHead+1)%KDQ;       /* the oldest gives way */
    KDQ_[kdTail].x=z->x; KDQ_[kdTail].y=z->y; KDQ_[kdTail].z=z->z;
    KDQ_[kdTail].type=z->type; kdTail=n; }
  int gib = dmg>=t->gib || (head&&dmg>t->gib*0.45f) || blast;
  sfxCreature(2,z->x,z->y+0.7f,z->z,ZT[z->type].sc);
  if(gib){
    gibBurst(z,z->type==Z_BROODER?18:11,1.5f);
    bloodSpray(z->x,z->y+0.7f*t->sc,z->z,0,0.5f,0,26,t->gore);
    for(int i=0;i<4;i++){
      float a=(rnd()-0.5f)*2, b=-0.4f-rnd(), c=(rnd()-0.5f)*2;
      bloodDecal(z->x,z->y+0.7f,z->z,a,b,c,t->gore);
    }
    z->gibbed=1;
  } else {
    bloodSpray(z->x,z->y+0.7f*t->sc,z->z,dx*0.3f,0.4f,dz*0.3f,10,t->gore);
  }
}
void zomHurt(int i,float dmg,float px,float py,float pz,
             float dx,float dy,float dz,int head,int blast){
  Zom *z=Z+i;
  if(!z->used||z->dead) return;
  const ZType *t=ZT+z->type;
  z->aggro=7;
  z->hp-=dmg;
  z->flash=1;
  float st=z->stagger+dmg*0.0045f*(3.f/t->mass);
  z->stagger=st>0.6f?0.6f:st;
  float kb=dmg*0.010f/t->mass; if(kb>0.55f) kb=0.55f;
  z->nudgeX+=dx*kb; z->nudgeZ+=dz*kb;
  bloodSpray(px,py,pz,dx*0.6f,dy*0.6f,dz*0.6f,head?12:6,t->gore);
  if(rnd()<(head?0.9f:0.45f)) bloodDecal(px,py,pz,dx,dy,dz,t->gore);
  sfxCreature(3,px,py,pz,ZT[z->type].sc);
  if(z->hp<=0) killZom(z,dmg,dx,dz,head,blast);
}

/* ---- the spitter's acid --------------------------------------------- */
static void boltSpawn(float x,float y,float z,float vx,float vy,float vz){
  for(int i=0;i<MAXBOLT;i++) if(!BOLT[i].used){
    BOLT[i].used=1; BOLT[i].x=x; BOLT[i].y=y; BOLT[i].z=z;
    BOLT[i].vx=vx; BOLT[i].vy=vy; BOLT[i].vz=vz; BOLT[i].life=3.2f;
    return;
  }
}
static void boltStep(float dt){
  for(int i=0;i<MAXBOLT;i++){
    if(!BOLT[i].used) continue;
    BOLT[i].vy-=9.0f*dt;
    float nx=BOLT[i].x+BOLT[i].vx*dt;
    float ny=BOLT[i].y+BOLT[i].vy*dt;
    float nz=BOLT[i].z+BOLT[i].vz*dt;
    BOLT[i].life-=dt;
    int hit = solidAt(nx,ny,nz);
    float dx=nx-PX, dy=ny-(PY+0.9f), dz=nz-PZ;
    if(dx*dx+dy*dy+dz*dz<0.42f*0.42f){
      zomHurtPlayer(14,nx,ny,nz); hit=1;
    }
    if(hit||BOLT[i].life<=0){
      for(int q=0;q<7;q++){
        float a=rnd()*6.28f, e=rnd()*2.f, sp=1.f+rnd()*2.5f;
        fxEmit(nx,ny,nz,cosf(a)*sp,e,sinf(a)*sp,0.06f+rnd()*0.08f,
               0.5f,PK_BLOB,0.42f,0.62f,0.10f,0.8f,7,2,0,0);
      }
      fxAddDynLight(nx,ny,nz,0.35f,0.7f,0.12f,3.2f,0.22f);
      BOLT[i].used=0; continue;
    }
    BOLT[i].x=nx; BOLT[i].y=ny; BOLT[i].z=nz;
  }
}

/* ============================================================
   G15.  THE AI

   The previz's zombies split their attention between the player
   and a reactor they were told to eat.  There is no reactor in
   this ship: the story's animals hunt the player and nothing
   else, which is what makes holding one doorway stop working.
   ============================================================ */
/* ---- the same lattice, walked forwards ------------------------------
   The creatures follow a field solved from the player.  -auto needs one
   solved from wherever the quest is pointing, so it can walk the route
   the story lays out and prove the ship is actually traversable along it.
   It is the same solver; only the goal moves. */
/* A GOAL IS A POINT ON THE DECK PLAN AND THE DECK PLAN IS NOT THE SHIP.
   The story's door positions are the top-left corners of the plan's door
   rectangles and the quest targets are hand-written landmarks, so a goal
   lands inside a bulkhead about a third of the time -- and a field solved
   from a node inside a bulkhead reaches nothing at all, which reads
   exactly like a level that is not connected.  Snap to the nearest cell
   that has a standable node in it and the whole class of problem goes. */
static int navNear(float x,float y,float z){
  int n=navNodeAt(x,y,z);
  if(n>=0&&inOff[n+1]>inOff[n]) return n;
  int best=-1; float bd=1e9f;
  for(int r=1;r<=10;r++){
    for(int dj=-r;dj<=r;dj++) for(int di=-r;di<=r;di++){
      if(abs(di)!=r&&abs(dj)!=r) continue;         /* the ring only */
      float px=x+di*NC, pz=z+dj*NC;
      int m=navNodeAt(px,y,pz);
      if(m<0||inOff[m+1]<=inOff[m]) continue;
      float d=hypot3(nvX[m]-x,nvY[m]-y,nvZ[m]-z);
      if(d<bd){ bd=d; best=m; }
    }
    if(best>=0) return best;
  }
  return -1;
}
void zomGoal(float x,float y,float z){
  int n=navNear(x,y,z);
  if(n==goalNode) return;
  goalNode=n;
  solveField(n,distGoal,nextGoal);
}
/* ---- CAN A BODY WALK FROM a TO b -------------------------------------
   THE LATTICE IS DIRECTED AND THE OBVIOUS TEST ASKS THE WRONG WAY ROUND.
   Links are stored as INCOMING edges, so solveField from a node answers
   "what does it cost to reach THIS node from everywhere else" -- exactly
   the flow field the animals want, and the reverse of what a quest target
   needs to know.  A drop the player can fall down but not climb makes the
   two answers differ, and the escape gallery has one: a point that the
   whole ship can reach, and that can reach nothing, is exactly the pocket
   -auto spent four minutes grinding against.  This one solves the field
   the right way round and leaves it in place, since goalNode is what says
   whose field distGoal holds. */
int zomRouteExists(float fx,float fy,float fz,float tx,float ty,float tz){
  int g=navNear(tx,ty,tz);
  int a=navNodeAt(fx,fy,fz);
  if(g<0||a<0) return 0;
  solveField(g,distGoal,nextGoal);
  goalNode=g;
  return distGoal[a]>=0.f && distGoal[a]<1e8f;
}
float zomGoalDist(float x,float y,float z){
  int n=navNodeAt(x,y,z);
  return n<0?-1.f:distGoal[n];
}
int zomStepTo(float x,float y,float z,float *dir){
  if(goalNode<0) return 0;
  int n=navNodeAt(x,y,z);
  if(n<0||nextGoal[n]<0) return 0;
  int nn=nextGoal[n];
  /* look one node further where the line is clear, so the walk does not
     zig-zag from cell centre to cell centre */
  if(nextGoal[nn]>=0){
    int n2=nextGoal[nn];
    float ddx=nvX[n2]-x, ddz=nvZ[n2]-z, dd=hypot2(ddx,ddz);
    if(dd>0.1f&&!blocked(x,y+0.9f,z,ddx/dd,0,ddz/dd,dd-0.3f)) nn=n2;
  }
  float dx=nvX[nn]-x, dz=nvZ[nn]-z, d=hypot2(dx,dz);
  if(d<0.02f) return 0;
  dir[0]=dx/d; dir[1]=dz/d;
  return 1;
}
/* ---- AND A THIRD FIELD, WHICH BELONGS TO THE DRONE ------------------
   Lumi flew the straight line to her target and dropped whichever axis
   was solid.  That slide gets round a flat wall and DEADLOCKS on every
   corner in the ship: at an inside corner both axes are blocked at once
   and there is nothing left to drop.  Measured over a 300 s -auto run she
   spent 64% of her frames pinned like that -- 51.6 s of it in one stretch,
   sixteen metres behind a player who had walked on -- and opened no door
   at all, because every door event in the game waits on lumiReadyAt().

   The lattice that walks the animals answers exactly this question, and
   it is already paid for: zomStep solves one field every 0.26 s.  This is
   a second one, solved to wherever she is going.

   Hers is separate from distGoal for a reason worth writing down: that
   one is aimed at the QUEST goal (leadStep sets it) and hers is aimed at
   a door panel, a dock or a lead point, which are three different places.
   Sharing them would mean re-solving the animals' field twice a second
   and stealing it from whoever asked first.

   A goal that does not move -- a panel, the dock -- costs one solve.  A
   lead point sliding along with the player is rate-limited to FLY_HOLD,
   and a field a third of a second stale still points down the right
   corridor. */
#define FLY_HOLD 0.30f
static float flyHold=0;
void zomFlyGoal(float x,float y,float z,float dt){
  if(flyHold>0) flyHold-=dt;
  int n=navNodeAt(x,y,z);
  if(n<0||inOff[n+1]<=inOff[n]) n=navNear(x,y,z);
  if(n<0||n==flyNode) return;
  if(flyHold>0) return;
  flyHold=FLY_HOLD;
  flyNode=n;
  solveField(n,distFly,nextFly);
}
/* the same walk as zomStepTo, with two differences that are both about
   her being a flying thing rather than a walking one: the line-of-sight
   test runs at HER height instead of at a waist over the deck, and it
   looks up to three nodes ahead rather than one, because a drone with no
   feet has no reason to visit the middle of every cell on the way. */
int zomFlyStep(float x,float y,float z,float *dir){
  if(flyNode<0) return 0;
  int n=navNodeAt(x,y,z);
  if(n<0||inOff[n+1]<=inOff[n]) n=navNear(x,y,z);
  if(n<0||nextFly[n]<0) return 0;
  int nn=nextFly[n];
  for(int k=0;k<3&&nextFly[nn]>=0;k++){
    int n2=nextFly[nn];
    float ddx=nvX[n2]-x, ddz=nvZ[n2]-z, dd=hypot2(ddx,ddz);
    if(dd<=0.1f||blocked(x,y,z,ddx/dd,0,ddz/dd,dd-0.3f)) break;
    nn=n2;
  }
  float dx=nvX[nn]-x, dz=nvZ[nn]-z, d=hypot2(dx,dz);
  if(d<0.02f) return 0;
  dir[0]=dx/d; dir[1]=dz/d;
  return 1;
}

void zomDump(void){
  for(int i=0;i<MAXZ;i++){
    if(!Z[i].used) continue;
    platLog("  z%-2d M%d %s %6.2f %5.2f %6.2f  hp %3.0f  st %d  gr %d  sp %.2f",
            i,Z[i].type+1,Z[i].dead?"DEAD":"live",Z[i].x,Z[i].y,Z[i].z,
            Z[i].hp,Z[i].state,Z[i].grounded,hypot2(Z[i].vx,Z[i].vz));
  }
}
void zomStep(float dt){
  fieldT-=dt;
  if(fieldT<=0){
    fieldT=0.26f;
    solveField(navNodeAt(PX,PY,PZ),distPly,nextPly);
  }
  boltStep(dt);

  for(int i=0;i<MAXZ;i++){
    Zom *z=Z+i;
    if(!z->used) continue;
    const ZType *t=ZT+z->type;
    z->flash-=dt*5.5f; if(z->flash<0) z->flash=0;

    if(z->dead){
      z->deadT+=dt;
      if(z->gibbed||z->deadT>8.f){ z->used=0; continue; }
      z->vy-=22*dt;
      float ny=z->y+z->vy*dt;
      if(cylSolid(z->x,ny,z->z,t->rad,0.4f)) z->vy=0; else z->y=ny;
      continue;
    }
    z->spawnT-=dt;  if(z->spawnT<0) z->spawnT=0;
    z->stagger-=dt*1.9f; if(z->stagger<0) z->stagger=0;
    z->atkCd-=dt;   if(z->atkCd<0) z->atkCd=0;
    z->aggro-=dt;   if(z->aggro<0) z->aggro=0;
    z->slow-=dt;    if(z->slow<0) z->slow=0;
    z->litT-=dt;
    if(z->litT<=0){
      z->litT=0.16f+rnd()*0.12f;
      sampleLight(z->x,z->y+0.9f*t->sc,z->z,z->lit,z->ldir);
    }
    z->groanT-=dt;
    if(z->groanT<=0){ z->groanT=3.2f+rnd()*6.f; sfxCreature(0,z->x,z->y+1,z->z,ZT[z->type].sc); }

    float dTgt=hypot2(PX-z->x,PZ-z->z);

    /* ---- the cocoon: a sac that opens ------------------------------- */
    if(z->type==Z_COCOON){
      z->phase+=dt*0.9f;
      z->kidT-=dt;
      if(z->kidT<=0&&z->kids<3&&dTgt<24.f){
        z->kidT=6.f; z->kids++;
        sfxCreature(1,z->x,z->y+1,z->z,ZT[z->type].sc);
        float a=rnd()*6.28f;
        zomSpawn(Z_SCUTTLER,z->x+cosf(a)*0.6f,z->y,z->z+sinf(a)*0.6f,-10,z->enc);
      }
      continue;
    }

    /* ---- the spitter: it does not close, it aims -------------------- */
    if(z->type==Z_SPITTER){
      z->phase+=dt*1.4f;
      float want=atan2f(PX-z->x,PZ-z->z);
      float d=want-z->yaw;
      while(d>3.14159f) d-=6.2832f;
      while(d<-3.14159f) d+=6.2832f;
      float lim=2.2f*dt;
      z->yaw += d>lim?lim:(d<-lim?-lim:d);
      z->fireCd-=dt;
      float ex=PX-z->x, ey=(PY+0.9f)-(z->y+0.7f), ez=PZ-z->z;
      float L=hypot3(ex,ey,ez);
      int see = L<14.f && !blocked(z->x,z->y+0.7f,z->z,ex/L,ey/L,ez/L,L-0.4f);
      if(see&&z->fireCd<=0){
        z->aimT+=dt;                    /* three seconds of warning */
        if(z->aimT>=3.f){
          z->aimT=0; z->fireCd=2.6f+rnd()*1.6f;
          float sp=17.f;
          float lead=L/sp;
          boltSpawn(z->x,z->y+0.7f,z->z,
                    ex/L*sp, ey/L*sp+4.4f*lead, ez/L*sp);
          sfxCreature(4,z->x,z->y+0.7f,z->z,ZT[z->type].sc);
        }
      } else z->aimT=0;
      /* a perched spitter holds its height -- it came out of a ceiling
         hatch and it is clinging to the deckhead, which is by design and
         is also why it cannot simply be walked away from */
      if(!z->perched){
        z->vy-=24*dt;
        float ny=z->y+z->vy*dt;
        if(!cylSolid(z->x,ny,z->z,t->rad,t->hgt)){ z->y=ny; z->grounded=0; }
        else{ if(z->vy<0) z->grounded=1; z->vy=0; }
      } else z->grounded=1;
      stepFeet(z,dt);
      continue;
    }

    /* ---- steer ------------------------------------------------------ */
    int chase = dTgt<26.f || z->aggro>0;
    float tx=PX, tz=PZ;
    float reach=t->reach;
    float wx=0,wz=0;
    if(chase&&dTgt>reach*0.85f){
      int useDirect=0;
      if(dTgt<7.5f){
        float inv=1.f/dTgt;
        if(!blocked(z->x,z->y+0.75f,z->z,(tx-z->x)*inv,0,(tz-z->z)*inv,dTgt-0.4f))
          useDirect=1;
      }
      if(useDirect){ wx=(tx-z->x)/dTgt; wz=(tz-z->z)/dTgt; }
      else{
        int n=navNodeAt(z->x,z->y,z->z);
        if(n>=0&&nextPly[n]>=0){
          int nn=nextPly[n];
          if(nextPly[nn]>=0){                    /* look one further, to smooth */
            int n2=nextPly[nn];
            float ddx=nvX[n2]-z->x, ddz=nvZ[n2]-z->z, dd=hypot2(ddx,ddz);
            if(dd>0.1f&&!blocked(z->x,z->y+0.75f,z->z,ddx/dd,0,ddz/dd,dd-0.25f))
              nn=n2;
          }
          float ddx=nvX[nn]-z->x, ddz=nvZ[nn]-z->z, dd=hypot2(ddx,ddz);
          if(dd>0.02f){ wx=ddx/dd; wz=ddz/dd; }
        } else if(dTgt>0.1f){ wx=(tx-z->x)/dTgt; wz=(tz-z->z)/dTgt; }
      }
    }
    /* after a swipe it gives ground, curving as it goes.  This is what
       reopens the pounce window, and it is why the room never settles. */
    if(z->backoff>0){
      z->backoff-=dt;
      if(dTgt>6.5f) z->backoff=0;
      else{
        float iv=1.f/(dTgt>0.001f?dTgt:0.001f);
        float ax=(tx-z->x)*iv, az=(tz-z->z)*iv;
        wx=-ax*0.80f-az*0.60f*z->circle;
        wz=-az*0.80f+ax*0.60f*z->circle;
      }
    }
    /* separation, so a crowd spreads across a doorway instead of stacking */
    float sx=0,sz=0;
    for(int k=0;k<MAXZ;k++){
      if(k==i||!Z[k].used||Z[k].dead) continue;
      float ox=z->x-Z[k].x, oz=z->z-Z[k].z;
      float d2=ox*ox+oz*oz, rr=(t->rad+ZT[Z[k].type].rad)*1.12f;
      if(d2<rr*rr&&d2>1e-4f){
        float d=sqrtf(d2), k2=(1-d/rr)/d;
        sx+=ox*k2; sz+=oz*k2;
      }
    }
    wx+=sx*1.5f; wz+=sz*1.5f;
    float wl=hypot2(wx,wz);
    if(wl>1e-4f){ wx/=wl; wz/=wl; }

    float slowK=z->slow>0?0.42f:1.f;
    float spd=t->spd*slowK*(z->state?0.18f:1.f)*(1-z->stagger*0.8f)
              *(z->spawnT>0?0.3f:1.f);
    if(chase&&dTgt>3.5f&&!z->state) spd*=2.0f;   /* the charge */
    if(wl>1e-4f){
      /* A stalker never turns its back.  Facing comes from the target, not
         from travel, so it can withdraw and circle while still looking at
         you -- and its planted feet stop being swept around by a body
         spinning at the maximum turn rate. */
      float want = dTgt>0.25f ? atan2f(tx-z->x,tz-z->z) : atan2f(wx,wz);
      float d=want-z->yaw;
      while(d>3.14159f) d-=6.2832f;
      while(d<-3.14159f) d+=6.2832f;
      float lim=6*dt;
      z->yaw += d>lim?lim:(d<-lim?-lim:d);
      zMove(z,wx*spd*dt,wz*spd*dt);
      /* AND A FOOT COMES DOWN.  Six legs moving in a corridor made no
         sound at all, so an animal was silent until it was close enough to
         groan -- and a thing you cannot hear walking is a thing that
         teleports into the room.  The cadence is the gait's own phase, one
         tick every 2.8 radians, which is about four a second at a
         scuttler's sprint and two at a brooder's walk.  dGain inside
         sfxStep drops it past sixteen metres and the allocator drops it
         when the mix is busy, which is the whole of the budget for it. */
      { float ph0=z->phase;
        z->phase+=spd*dt*3.4f;
        if(z->grounded&&spd>0.4f&&
           (int)(z->phase*0.357f)!=(int)(ph0*0.357f))
          sfxStep(z->x,z->y+0.15f,z->z,ZT[z->type].sc); }
    } else z->phase+=dt*1.2f;

    if(z->nudgeX||z->nudgeZ){
      zMove(z,z->nudgeX*dt*10,z->nudgeZ*dt*10);
      float f=1-dt*9; if(f<0) f=0;
      z->nudgeX*=f; z->nudgeZ*=f;
      if(fabsf(z->nudgeX)<0.001f) z->nudgeX=0;
      if(fabsf(z->nudgeZ)<0.001f) z->nudgeZ=0;
    }

    z->vy-=24*dt;
    float ny=z->y+z->vy*dt;
    if(!cylSolid(z->x,ny,z->z,t->rad,t->hgt)){ z->y=ny; z->grounded=0; }
    else{
      if(z->vy<0){
        float rem=-z->vy*dt;
        while(rem>0.004f&&!cylSolid(z->x,z->y-0.01f,z->z,t->rad,t->hgt)){
          z->y-=0.01f; rem-=0.01f;
        }
        z->grounded=1;
      }
      z->vy=0;
    }
    if(z->y<-8){ z->used=0; continue; }

    /* THE WATCHDOG.  A body that wants to move and has not moved for two
       seconds is wedged -- inside a doorpost, on the lip of a grating, or
       in a brush it was spawned into.  Lifting it to the nearest standing
       node is not elegant and it is the difference between a wave that
       arrives and a wave that stands in a wall for the whole fight. */
    if(wl>1e-4f){
      if(hypot2(z->x-z->sx0,z->z-z->sz0)<0.05f){
        z->stuckT+=dt;
        if(z->stuckT>2.0f){
          z->stuckT=0;
          int n=navNodeAt(z->x,z->y+0.4f,z->z);
          if(n>=0){ z->x=nvX[n]; z->y=nvY[n]+0.05f; z->z=nvZ[n]; z->vy=0;
                    stalkerInit(z); }
          else z->used=0;
        }
      } else { z->stuckT=0; z->sx0=z->x; z->sz0=z->z; }
    } else z->stuckT=0;

    stepFeet(z,dt);
    /* the pounce.  It crouches where you can see it crouch, then clears
       the gap in one arc -- the tell is the whole point of it. */
    z->pounceCd-=dt; if(z->pounceCd<0) z->pounceCd=0;
    if(z->crouchT>0){
      z->crouchT-=dt;
      if(z->crouchT<=0){
        float dd=dTgt>0.001f?dTgt:0.001f;
        z->vy=6.0f; z->grounded=0;
        z->nudgeX+=(tx-z->x)/dd*2.2f; z->nudgeZ+=(tz-z->z)/dd*2.2f;
        z->pounceCd=3.2f+rnd()*2.2f;
        sfxCreature(1,z->x,z->y+0.7f,z->z,ZT[z->type].sc);
      }
    } else if(chase&&z->grounded&&z->pounceCd<=0&&z->spawnT<=0&&z->stagger<0.1f
              &&z->backoff<=0&&dTgt>2.8f&&dTgt<8.5f
              &&!blocked(z->x,z->y+0.8f,z->z,(tx-z->x)/dTgt,0,(tz-z->z)/dTgt,dTgt-0.4f)){
      z->crouchT=0.30f;
    }
    { float k2=dt*13; if(k2>1)k2=1;
      z->crouch+=((z->crouchT>0?1.f:0.f)-z->crouch)*k2; }

    { float tl=z->state?0.10f:(0.22f+spd*0.05f);
      float k2=dt*6; if(k2>1)k2=1;
      z->lean+=(tl-z->lean)*k2; }

    /* ---- attack ----------------------------------------------------- */
    if(z->state){
      z->swing+=dt/t->swing;
      if(z->swing>=0.45f&&!z->swung){
        z->swung=1;
        if(hypot2(tx-z->x,tz-z->z)<reach+0.45f){
          zomHurtPlayer(t->dmg,z->x,z->y+1,z->z);
          sfxCreature(3,z->x,z->y+1,z->z,ZT[z->type].sc);
        }
      }
      if(z->swing>=1){
        z->state=0; z->swing=0; z->swung=0; z->atkCd=0.45f+rnd()*0.4f;
        z->backoff=0.75f+rnd()*0.65f;
        z->circle=rnd()<0.5f?-1:1;
      }
    } else if(dTgt<reach&&z->atkCd<=0&&z->spawnT<=0){
      z->state=1; z->swing=0; z->swung=0;
      sfxCreature(1,z->x,z->y+1,z->z,ZT[z->type].sc);
    }
  }
}

/* ============================================================
   G8b.  THE CREATURE GRAPH

   Every node is a span.  cgSpan draws one; cgChain runs a
   tapering row of them with a wave on it; cgLegIK solves two
   bones to a planted foot.  Nothing below is a mesh.
   ============================================================ */
static float SEGL=0;
static void mSeg(float ax,float ay,float az,float bx,float by,float bz,float *o){
  float ux=bx-ax, uy=by-ay, uz=bz-az;
  float L=hypot3(ux,uy,uz); if(L<1e-6f) L=1e-6f;
  float inv=1.f/L; ux*=inv; uy*=inv; uz*=inv; SEGL=L;
  float rx=fabsf(uy)>0.94f?1.f:0.f, ry=fabsf(uy)>0.94f?0.f:1.f;
  float xx=ry*uz, xy=-rx*uz, xz=rx*uy-ry*ux;
  float m=hypot3(xx,xy,xz); if(m<1e-6f) m=1e-6f;
  m=1.f/m; xx*=m; xy*=m; xz*=m;
  /* X x u, not u x X: the other order gives a left-handed frame, and with
     back-face culling on that draws every span inside out. */
  float zx=xy*uz-xz*uy, zy=xz*ux-xx*uz, zz=xx*uy-xy*ux;
  o[0]=xx; o[1]=ux; o[2]=zx; o[3]=(ax+bx)*0.5f;
  o[4]=xy; o[5]=uy; o[6]=zy; o[7]=(ay+by)*0.5f;
  o[8]=xz; o[9]=uz; o[10]=zz; o[11]=(az+bz)*0.5f;
}
static void cgSpan(float ax,float ay,float az,float bx,float by,float bz,
                   float th,float r,float g,float b,float emis,
                   const EntFx *fx,int prim){
  float M[12]; mSeg(ax,ay,az,bx,by,bz,M);
  pushEnt(M,th,SEGL,th,r,g,b,emis,fx,prim);
}
/* A span with an oval section and a little overrun past its own ends.
   It is what lets a body be four overlapping masses instead of one
   sphere: nothing here is ever allowed to read as the primitive it
   came from. */
static void cgBlob(float ax,float ay,float az,float bx,float by,float bz,
                   float w,float h,float ext,float r,float g,float b,
                   const EntFx *fx,int prim){
  float M[12]; mSeg(ax,ay,az,bx,by,bz,M);
  pushEnt(M,w,SEGL*ext,h,r,g,b,0,fx,prim);
}
static float XFV[3],XFD[3];
static float *xf(const float *M,float px,float py,float pz){
  XFV[0]=M[0]*px+M[1]*py+M[2]*pz+M[3];
  XFV[1]=M[4]*px+M[5]*py+M[6]*pz+M[7];
  XFV[2]=M[8]*px+M[9]*py+M[10]*pz+M[11];
  return XFV;
}
static float *xfv(const float *M,float px,float py,float pz){
  XFD[0]=M[0]*px+M[1]*py+M[2]*pz;
  XFD[1]=M[4]*px+M[5]*py+M[6]*pz;
  XFD[2]=M[8]*px+M[9]*py+M[10]*pz;
  return XFD;
}
static float RX,RY,RZ;
static void rotAx(float vx,float vy,float vz,float kx,float ky,float kz,float a){
  float c=cosf(a), sn=sinf(a), d=(kx*vx+ky*vy+kz*vz)*(1-c);
  RX=vx*c+(ky*vz-kz*vy)*sn+kx*d;
  RY=vy*c+(kz*vx-kx*vz)*sn+ky*d;
  RZ=vz*c+(kx*vy-ky*vx)*sn+kz*d;
}
/* CHAIN.  A tapering run of links that bends at every joint and carries a
   travelling wave.  This one operator is the tail, the crest and the back
   tubes; only the numbers differ. */
static void cgChain(float x,float y,float z,float dx,float dy,float dz,
                    float ux,float uy,float uz,int n,float seg,float th,
                    float taper,float bend,float wave,float ph,float lag,
                    float r,float g,float b,const EntFx *fx,int prim,float *tip){
  float q=hypot3(dx,dy,dz); if(q<1e-6f) q=1e-6f;
  q=1.f/q; dx*=q; dy*=q; dz*=q;
  for(int i=0;i<n;i++){
    float k=(float)i/n;
    float sx=uy*dz-uz*dy, sy=uz*dx-ux*dz, sz=ux*dy-uy*dx;
    float sm=hypot3(sx,sy,sz);
    if(sm>1e-5f){
      float w=1.f/sm; sx*=w; sy*=w; sz*=w;
      rotAx(dx,dy,dz,sx,sy,sz,bend+sinf(ph-i*lag)*wave);
      dx=RX; dy=RY; dz=RZ;
    }
    rotAx(dx,dy,dz,ux,uy,uz,sinf(ph*0.63f-i*lag*1.3f)*wave*0.5f);
    dx=RX; dy=RY; dz=RZ;
    float sl=seg*(1-taper*k);
    float nx=x+dx*sl, ny=y+dy*sl, nz=z+dz*sl;
    cgSpan(x,y,z,nx,ny,nz,th*(1-taper*k),r,g,b,0,fx,prim);
    x=nx; y=ny; z=nz;
  }
  if(tip){ tip[0]=x; tip[1]=y; tip[2]=z; tip[3]=dx; tip[4]=dy; tip[5]=dz; }
}
/* LEG.  Two bones to a planted foot.  The knee is pushed along the bend
   vector, and for a spider that vector points up and outward -- that arch
   standing above the back is the entire silhouette. */
static void cgLegIK(float hx,float hy,float hz,float tx,float ty,float tz,
                    float a,float b,float ux,float uy,float uz,float th,
                    const float *col,const EntFx *fx,float *knee,float bow,
                    float *foot){
  float dx=tx-hx, dy=ty-hy, dz=tz-hz;
  float d=hypot3(dx,dy,dz); if(d<1e-6f) d=1e-6f;
  float lo=fabsf(a-b)+1e-3f, hi=(a+b)*0.999f;
  float sx=dx, sy=dy, sz=dz, sd=d;
  if(d>hi||d<lo){                     /* out of reach: solve short, draw long */
    sd=d>hi?hi:lo;
    float w=sd/d; sx=dx*w; sy=dy*w; sz=dz*w;
  }
  float inv=1.f/sd, ex=sx*inv, ey=sy*inv, ez=sz*inv;
  float along=(a*a-b*b+sd*sd)/(2*sd);
  float hh=a*a-along*along; hh=hh>0?sqrtf(hh):0;
  float dot=ux*ex+uy*ey+uz*ez;
  float px=ux-ex*dot, py=uy-ey*dot, pz=uz-ez*dot;
  float pm=hypot3(px,py,pz);
  if(pm<1e-4f){ px=0; py=1; pz=0; pm=1; }
  float w2=1.f/pm; px*=w2; py*=w2; pz*=w2;
  float kx=hx+ex*along+px*hh, ky=hy+ey*along+py*hh, kz=hz+ez*along+pz*hh;
  if(bow>0){
    /* A femur drawn as one straight span is a cylinder and reads as one.
       Two links with the midpoint pushed off the line is a bone. */
    float mx=(hx+kx)*0.5f+px*bow, my=(hy+ky)*0.5f+py*bow, mz=(hz+kz)*0.5f+pz*bow;
    cgSpan(hx,hy,hz,mx,my,mz,th*0.94f,col[0],col[1],col[2],0,fx,PRIM_SEG);
    cgSpan(mx,my,mz,kx,ky,kz,th,col[0]*0.93f,col[1]*0.93f,col[2]*0.93f,0,fx,PRIM_SEG);
  } else
    cgSpan(hx,hy,hz,kx,ky,kz,th,col[0],col[1],col[2],0,fx,PRIM_SEG);
  /* draw to the CLAMPED foot: a shin that stretches to reach reads far
     worse than a foot that lifts for a few frames */
  cgSpan(kx,ky,kz,hx+sx,hy+sy,hz+sz,th*0.78f,
         col[0]*0.82f,col[1]*0.82f,col[2]*0.82f,0,fx,PRIM_SEG);
  if(knee){ knee[0]=kx; knee[1]=ky; knee[2]=kz; }
  /* and hand back the foot it actually drew to: anything below the shin
     has to follow the shin, or it detaches exactly when a leg stretches */
  if(foot){ foot[0]=hx+sx; foot[1]=hy+sy; foot[2]=hz+sz; }
}
static void cgMaw(const float *M,int n,float rad,float len,float open,
                  float th,const float *col,const EntFx *fx,int prim){
  for(int i=0;i<n;i++){
    float a=((float)i/n)*6.2832f+0.7854f, ca=cosf(a), sa=sinf(a);
    float sp=rad*(1+open*0.75f);
    float *v=xf(M,ca*sp,sa*sp,0);
    float ax=v[0], ay=v[1], az=v[2];
    float t=rad*(0.30f-open*0.95f);
    float *w=xf(M,ca*t,sa*t,len);
    cgSpan(ax,ay,az,w[0],w[1],w[2],th,col[0],col[1],col[2],0,fx,prim);
  }
}
/* OCELLI.  A spider's eyes are two rows, not an arc: an anterior row of
   four with the median pair much the largest, and a posterior row set
   higher and wider.  A person reads that arrangement before they have
   consciously seen anything else about the animal. */
static const float EYEX[8]={-0.30f,0.30f,-0.86f,0.86f,-0.58f,0.58f,-1.12f,1.12f};
static const float EYEY[8]={ 0.00f,0.00f, 0.13f,0.13f, 0.66f,0.66f, 0.60f,0.60f};
static const float EYEK[8]={ 1.00f,1.00f, 0.62f,0.62f, 0.70f,0.70f, 0.48f,0.48f};
static void cgEyes(const float *M,int n,float rad,float sz,
                   float r,float g,float b,float emis,const EntFx *fx){
  for(int i=0;i<n;i++){
    float k=EYEK[i], A[12],B[12];
    mTrans34(EYEX[i]*rad, EYEY[i]*rad*0.62f+rad*0.12f,
             rad*(0.90f-fabsf(EYEX[i])*0.17f), A);
    mMul34(M,A,B);
    pushEnt(B,sz*k,sz*k*0.86f,sz*k,r,g,b,emis,fx,PRIM_LENS);
  }
}

/* ---- the animal ----------------------------------------------------- */
static void drawStalker(const Zom *z,const float *root,EntFx *fx,EntFx *gfx){
  const ZType *t=ZT+z->type;
  float s=t->sc;
  /* Close up this creature is about ninety instances, so it gets a real
     ladder: ribs, thorns and claws inside 10 m, back tubes, mandibles and a
     full tail inside 26 m, and past that a walking silhouette -- which is
     all six legs, because the legs ARE the silhouette. */
  float d2=(z->x-PX)*(z->x-PX)+(z->z-PZ)*(z->z-PZ);
  int lod = d2>676?2:(d2>110?1:0);
  int near_= d2<34;
  /* No two of them are the same colour.  A wave of identically tinted
     animals is the fastest way to make a good model read as an asset. */
  float vr=0.84f+fmodf(z->id*0.3730f,1.f)*0.30f;
  float vg=0.92f+fmodf(z->id*0.7113f,1.f)*0.16f;
  float co[3]={t->coat[0]*vr, t->coat[1]*vr*vg, t->coat[2]*vr*(1.06f-vg*0.10f)};
  const float *pl=t->flesh;
  float un[3]={t->trous[0]*0.46f,t->trous[1]*0.44f,t->trous[2]*0.44f};
  float ph0=z->id*0.61f;
  float dead = z->dead ? (z->deadT/0.8f>1?1:z->deadT/0.8f) : 0;
  float rear = z->state?sinf(z->swing*3.1416f):0;
  float open = z->state?sinf((z->swing*1.4f>1?1:z->swing*1.4f)*3.1416f)*0.9f:0.12f;

  float A[12],B[12],C[12];
  /* the body rides on the legs: it dips as it walks, drops to crouch,
     and rears back over the front pair to strike */
  float bodyY=(0.60f+sinf(z->phase*2)*0.020f-z->crouch*0.20f-dead*0.36f)*s;
  mTrans34(0,bodyY,0,A); mMul34(root,A,C);
  mRotX34(-0.06f-rear*0.55f+z->crouch*0.18f+dead*0.42f,A); mMul34(C,A,B);
  float body[12]; memcpy(body,B,sizeof body);

  /* ---- the shell.  A run of plates, each one's tapered front tucked
     under the flared rear lip of the plate ahead of it, which is how an
     arthropod's tergites actually sit and the reason this stopped
     reading as a pile of eggs. ------------------------------------- */
  #define SHELL(sc_,wet,band,rgh) do{ fx->sk=1; fx->sf=(sc_); fx->si=(wet); \
                                      fx->sp=(band); fx->rough=(rgh); }while(0)
  #define MEMB(ph)  do{ fx->sk=2; fx->sf=1.f; fx->si=0.34f; fx->sp=(ph); \
                        fx->rough=0.64f; }while(0)
  fx->rough=0.46f; fx->rim=0.50f; fx->ao=0.74f;

  #define MASS(M,x0,y0,z0,x1,y1,z1,w,h,ext,r,g,b,prim) do{ \
      float *p_=xf(M,(x0)*s,(y0)*s,(z0)*s); \
      float a0=p_[0],a1=p_[1],a2=p_[2]; \
      float *q_=xf(M,(x1)*s,(y1)*s,(z1)*s); \
      cgBlob(a0,a1,a2,q_[0],q_[1],q_[2],(w)*s,(h)*s,ext,r,g,b,fx,prim); }while(0)
  #define PLATE(M,x,y,zz,tilt,w,h,len,k) do{ \
      fx->sp=0; \
      mTrans34((x)*s,(y)*s,(zz)*s,A); mMul34(M,A,C); \
      mRotX34(tilt,A); mMul34(C,A,B); \
      pushEnt(B,(w)*s,(h)*s,(len)*s,co[0]*(k),co[1]*(k),co[2]*(k),0,fx,PRIM_CARA); \
    }while(0)

  SHELL(1.00f,0.38f,0.85f,0.33f);
  PLATE(body, 0, 0.050f, 0.20f,-0.06f, 0.50f,0.48f,0.42f, 1.16f);
  PLATE(body, 0, 0.075f,-0.08f, 0.04f, 0.58f,0.58f,0.42f, 1.06f);
  if(lod<2) PLATE(body,0,0.030f,-0.90f,-0.48f, 0.34f,0.34f,0.24f, 0.84f);
  PLATE(body, 0, 0.150f,-0.50f, 0.16f, 0.60f,0.62f,0.42f, 1.00f);
  PLATE(body, 0, 0.125f,-0.74f,-0.20f, 0.50f,0.50f,0.32f, 0.92f);

  /* The waist.  Every other creature in this game is one continuous mass;
     the pinch between prosoma and opisthosoma is the single silhouette cue
     that says SPIDER before anything else registers -- so it is also the
     right place for the soft part that glows. */
  MEMB(ph0);
  MASS(body, 0,0.075f,-0.30f, 0,0.085f,-0.11f, 0.22f,0.21f,1.08f,
       pl[0],pl[1],pl[2], PRIM_POD);

  if(lod<2){
    SHELL(1.00f,0.26f,0.85f,0.60f);
    MASS(body, 0,-0.14f,-0.42f, 0,-0.12f,0.10f, 0.40f,0.20f,1.26f,
         un[0]*0.46f,un[1]*0.46f,un[2]*0.46f, PRIM_SEG);
    MEMB(ph0+2.1f);
    MASS(body, 0,0.245f,-0.40f, 0,0.225f,-0.68f, 0.17f,0.13f,1.16f,
         pl[0]*1.1f,pl[1]*1.1f,pl[2]*1.1f, PRIM_POD);
    /* the tubes off its back.  Two segments each so they curve instead of
       standing up like pins; they are the cue that survives at any
       distance and in any light. */
    SHELL(1.00f,0.26f,0.85f,0.52f);
    int nt=lod==0?4:2;
    for(int i=0;i<nt;i++){
      float sd=(i&1)?1.f:-1.f; int k=i>>1;
      float *bp=xf(body, sd*0.20f*s,(0.19f-k*0.02f)*s,(0.02f-k*0.24f)*s);
      float b0=bp[0],b1=bp[1],b2=bp[2];
      float *dv=xfv(body, sd*0.34f,0.92f,-0.20f);
      float d0=dv[0],d1=dv[1],d2v=dv[2];
      float *uv=xfv(body,0,0,-1);
      cgChain(b0,b1,b2,d0,d1,d2v,uv[0],uv[1],uv[2],2,
              (0.29f-k*0.04f)*s,(0.125f-k*0.014f)*s,0.40f,0.46f,0,0,0,
              co[0]*0.95f,co[1]*0.95f,co[2]*0.95f,fx,PRIM_SEG,0);
    }
  }
  if(lod==0){
    /* ribs.  Four spans off the spine that wrap down past the widest point,
       so the body has an edge you can see against a lit wall. */
    SHELL(1.00f,0.20f,0.85f,0.37f);
    for(int i=0;i<4;i++){
      float sd=(i&1)?1.f:-1.f; int k=i>>1;
      float *r0=xf(body, sd*0.07f*s,(0.20f-k*0.03f)*s,(-0.40f-k*0.26f)*s);
      float c0=r0[0],c1=r0[1],c2=r0[2];
      float *r1=xf(body, sd*0.42f*s,(-0.10f-k*0.02f)*s,(-0.46f-k*0.28f)*s);
      cgSpan(c0,c1,c2,r1[0],r1[1],r1[2],(0.052f-k*0.006f)*s,
             co[0]*0.62f,co[1]*0.62f,co[2]*0.66f,0,fx,PRIM_HORN);
    }
    float *rp=xf(body,0,0.26f*s,0.30f*s);
    float rx=rp[0],ry=rp[1],rz=rp[2];
    float *rd=xfv(body,0,0.12f,-1);
    float rdx=rd[0],rdy=rd[1],rdz=rd[2];
    float *ru=xfv(body,1,0,0);
    SHELL(1.00f,0.38f,0.85f,0.35f);
    cgChain(rx,ry,rz,rdx,rdy,rdz,ru[0],ru[1],ru[2],3,
            0.17f*s,0.15f*s,0.44f,-0.10f,0,0,0,
            co[0]*1.2f,co[1]*1.2f,co[2]*1.2f,fx,PRIM_SEG,0);
  }

  /* ---- skull ------------------------------------------------------- */
  float head[12];
  mTrans34(0,-0.03f*s,0.44f*s,A); mMul34(body,A,C);
  mRotX34(0.26f+sinf(z->phase*2)*0.06f+rear*0.5f,A); mMul34(C,A,B);
  memcpy(head,B,sizeof head);
  SHELL(1.00f,0.42f,0.85f,0.31f);
  PLATE(head, 0,0.030f,-0.06f, 0.00f, 0.40f,0.34f,0.52f, 0.98f);
  if(lod<2){
    PLATE(head, 0,-0.035f,0.26f,-0.10f, 0.28f,0.24f,0.32f, 1.10f);
    float *cp=xf(head,0,0.11f*s,-0.26f*s);
    float cx=cp[0],cy=cp[1],cz=cp[2];
    float *cd=xfv(head,0,0.22f,-0.98f);
    float cdx=cd[0],cdy=cd[1],cdz=cd[2];
    float *cu=xfv(head,1,0,0);
    SHELL(1.00f,0.46f,0.85f,0.34f);
    cgChain(cx,cy,cz,cdx,cdy,cdz,cu[0],cu[1],cu[2],2,
            0.20f*s,0.24f*s,0.44f,0.34f,0,0,0,
            co[0]*0.90f,co[1]*0.90f,co[2]*0.90f,fx,PRIM_SEG,0);
    SHELL(1.00f,0.38f,0.70f,0.24f);
    cgMaw(head,4,0.15f*s,0.30f*s,open,0.062f*s,un,fx,PRIM_HORN);
    /* Chelicerae.  The mandibles open sideways; these two hang under them
       and swing forward on the bite, and they are what you see coming. */
    SHELL(1.00f,0.34f,0.60f,0.22f);
    for(int i=0;i<2;i++){
      float sd=i?1.f:-1.f;
      float *fp=xf(head, sd*0.11f*s,-0.09f*s,0.15f*s);
      float f0=fp[0],f1=fp[1],f2=fp[2];
      float *fv=xfv(head, sd*0.28f,-0.74f-open*0.42f,0.60f+open*0.55f);
      cgSpan(f0,f1,f2, f0+fv[0]*0.32f*s,f1+fv[1]*0.32f*s,f2+fv[2]*0.32f*s,
             0.050f*s, un[0]*0.80f,un[1]*0.80f,un[2]*0.80f,0,fx,PRIM_HORN);
    }
    /* pedipalps.  The short pair beside the mouth that everything with
       eight legs has and nothing with six does. */
    for(int i=0;i<2;i++){
      float sd=i?1.f:-1.f, tw=sinf(z->phase*2.4f+i*2.1f)*0.13f;
      float *p0=xf(head, sd*0.165f*s,-0.055f*s,0.10f*s);
      float q0=p0[0],q1=p0[1],q2=p0[2];
      float *v1=xfv(head, sd*0.58f,0.44f+tw,0.68f);
      float e1=q0+v1[0]*0.26f*s, e2=q1+v1[1]*0.26f*s, e3=q2+v1[2]*0.26f*s;
      SHELL(1.00f,0.28f,0.95f,0.60f);
      cgSpan(q0,q1,q2,e1,e2,e3,0.062f*s,
             co[0]*0.94f,co[1]*0.94f,co[2]*0.94f,0,fx,PRIM_SEG);
      float *v2=xfv(head, sd*0.28f,-0.34f-tw,0.90f);
      float f1b=e1+v2[0]*0.24f*s, f2b=e2+v2[1]*0.24f*s, f3b=e3+v2[2]*0.24f*s;
      cgSpan(e1,e2,e3,f1b,f2b,f3b,0.053f*s,
             co[0]*0.88f,co[1]*0.88f,co[2]*0.88f,0,fx,PRIM_SEG);
      if(lod==0){
        SHELL(1.00f,0.30f,0.50f,0.25f);
        float *v3=xfv(head, sd*0.10f,-0.74f,0.66f);
        cgSpan(f1b,f2b,f3b, f1b+v3[0]*0.15f*s,f2b+v3[1]*0.15f*s,f3b+v3[2]*0.15f*s,
               0.045f*s, un[0]*0.70f,un[1]*0.70f,un[2]*0.70f,0,fx,PRIM_HORN);
      }
    }
  }
  /* the lenses survive to the last tier: they are the only lit thing on
     the creature, so they are how you find it in a dark room */
  cgEyes(head, lod==0?8:(lod==1?4:2), 0.150f*s,0.050f*s,
         1.0f,0.50f,0.10f, dead?0:1.05f, gfx);

  /* ---- the tail ---------------------------------------------------- */
  { float *o=xf(body,0,0.06f*s,-0.90f*s);
    float ox=o[0],oy=o[1],oz=o[2];
    float *d=xfv(body,0,0.52f,-0.86f);
    float dx=d[0],dy=d[1],dz=d[2];
    float *u=xfv(body,1,0,0);
    float STIP[6];
    SHELL(1.00f,0.24f,0.85f,0.50f);
    cgChain(ox,oy,oz,dx,dy,dz,u[0],u[1],u[2], lod==0?7:(lod==1?5:3),
            0.22f*s,0.125f*s,0.60f, 0.17f-rear*0.5f, 0.13f,
            z->phase*1.6f+z->id, 0.85f,
            co[0],co[1],co[2],fx,PRIM_SEG,STIP);
    if(lod==0)
      cgSpan(STIP[0],STIP[1],STIP[2],
             STIP[0]+STIP[3]*0.19f*s,STIP[1]+STIP[4]*0.19f*s,
             STIP[2]+STIP[5]*0.19f*s,
             0.055f*s, 1.0f,0.46f,0.10f, dead?0:0.85f, gfx, PRIM_HORN); }

  if(lod==0){
    /* spinnerets.  Every spider has them and nothing else does, so they
       cost four instances and buy an entire taxonomic class. */
    SHELL(1.00f,0.36f,0.40f,0.66f);
    for(int i=0;i<4;i++){
      float sd=(i&1)?1.f:-1.f; int k=i>>1;
      float *sp=xf(body, sd*0.072f*s,(-0.02f-k*0.048f)*s,(-0.80f-k*0.05f)*s);
      float s0=sp[0],s1=sp[1],s2=sp[2];
      float *sv=xfv(body, sd*0.36f,-0.34f-k*0.24f,-0.88f);
      float sl=(0.15f-k*0.035f)*s;
      cgSpan(s0,s1,s2,s0+sv[0]*sl,s1+sv[1]*sl,s2+sv[2]*sl,
             (0.050f-k*0.008f)*s, pl[0]*0.95f,pl[1]*0.95f,pl[2]*0.95f,0,fx,PRIM_HORN);
    }
  }
  if(near_){
    SHELL(1.00f,0.16f,0.0f,0.44f);
    for(int i=0;i<6;i++){
      float sd=(i&1)?1.f:-1.f; int k=i>>1;
      float *bp=xf(body, sd*(0.13f+k*0.05f)*s,(0.20f-k*0.05f)*s,(-0.34f-k*0.19f)*s);
      float b0=bp[0],b1=bp[1],b2=bp[2];
      float *bv=xfv(body, sd*0.66f,0.74f,-0.52f);
      cgSpan(b0,b1,b2,b0+bv[0]*0.16f*s,b1+bv[1]*0.16f*s,b2+bv[2]*0.16f*s,
             0.0115f*s, co[0]*0.80f,co[1]*0.78f,co[2]*0.82f,0,fx,PRIM_SETA);
    }
  }

  /* ---- six legs to six planted feet -------------------------------- */
  float upper=0.64f*s, lower=0.78f*s;
  for(int i=0;i<6;i++){
    float *h=xf(body, SLEGX[i]*0.44f*s,0.03f*s,SLEGZ[i]*0.62f*s);
    float hx=h[0],hy=h[1],hz=h[2];
    float *ov=xfv(body, SLEGX[i]<0?-1.f:1.f,0,0);
    float ovx=ov[0], ovz=ov[2];
    /* the knee goes up more than out: that arch standing above the back
       is what says "not a dog" at fifty metres */
    float bx=ovx*0.46f, by=1.30f, bz=ovz*0.46f;
    float ftx=z->feet[i*3], fty=z->feet[i*3+1], ftz=z->feet[i*3+2];
    if(dead>0){                     /* a dead spider folds its legs over itself */
      float cx=z->x+(hx-z->x)*0.40f, cz=z->z+(hz-z->z)*0.40f, cy=z->y+0.50f*s;
      ftx+=(cx-ftx)*dead; fty+=(cy-fty)*dead; ftz+=(cz-ftz)*dead;
    }
    float SKNEE[3],SFOOT[3];
    SHELL(1.00f,0.20f,1.0f,0.66f);
    cgLegIK(hx,hy,hz, ftx,fty,ftz, upper,lower, bx,by,bz,
            0.134f*s, co, fx, SKNEE, lod==0?0.070f*s:0, SFOOT);
    float fx0=SFOOT[0], fy0=SFOOT[1], fz0=SFOOT[2];
    if(lod<2){
      /* the coxa has to survive to lod 1: without it the leg starts in
         mid-air at the exact distance where you can still see the hip */
      SHELL(1.00f,0.22f,0.9f,0.68f);
      float *j=xf(body, SLEGX[i]*0.28f*s,0.06f*s,SLEGZ[i]*0.48f*s);
      cgSpan(j[0],j[1],j[2],
             hx+(SKNEE[0]-hx)*0.20f, hy+(SKNEE[1]-hy)*0.20f, hz+(SKNEE[2]-hz)*0.20f,
             0.180f*s, co[0]*0.86f,co[1]*0.86f,co[2]*0.86f,0,fx,PRIM_SEG);
      /* The knee is where an arthropod stops being armour.  A sleeve from
         inside the femur to inside the tibia straddles the bend, and it is
         the only lit thing on the limb -- which is what makes six dark
         sticks read as six joints. */
      MEMB(ph0+i*0.83f);
      cgSpan(SKNEE[0]+(hx-SKNEE[0])*0.24f, SKNEE[1]+(hy-SKNEE[1])*0.24f,
             SKNEE[2]+(hz-SKNEE[2])*0.24f,
             SKNEE[0]+(fx0-SKNEE[0])*0.22f, SKNEE[1]+(fy0-SKNEE[1])*0.22f,
             SKNEE[2]+(fz0-SKNEE[2])*0.22f,
             0.158f*s, pl[0],pl[1],pl[2],0,fx,PRIM_POD);
      if(lod==0){
        float kb=0.052f*s;
        SHELL(1.00f,0.30f,0.30f,0.33f);
        cgSpan(SKNEE[0]+(hx-SKNEE[0])*0.30f+bx*kb,
               SKNEE[1]+(hy-SKNEE[1])*0.30f+by*kb,
               SKNEE[2]+(hz-SKNEE[2])*0.30f+bz*kb,
               SKNEE[0]+(fx0-SKNEE[0])*0.26f+bx*kb,
               SKNEE[1]+(fy0-SKNEE[1])*0.26f+by*kb,
               SKNEE[2]+(fz0-SKNEE[2])*0.26f+bz*kb,
               0.112f*s, co[0]*1.10f,co[1]*1.10f,co[2]*1.10f,0,fx,PRIM_CARA);
      }
    }
    if(lod==0){
      SHELL(1.00f,0.18f,0.85f,0.42f);
      float px=hx+(SKNEE[0]-hx)*0.60f, py=hy+(SKNEE[1]-hy)*0.60f,
            pz=hz+(SKNEE[2]-hz)*0.60f;
      cgSpan(px,py,pz,px+ovx*0.13f*s,py+0.21f*s,pz+ovz*0.13f*s,
             0.046f*s, co[0]*0.58f,co[1]*0.58f,co[2]*0.62f,0,fx,PRIM_HORN);
      SHELL(1.00f,0.18f,0.85f,0.34f);
      float gx=fx0-hx, gz=fz0-hz;
      float ax2=fx0+gx*0.09f, az2=fz0+gz*0.09f;
      cgSpan(fx0,fy0+0.21f*s,fz0, ax2,fy0+0.055f*s,az2,
             0.082f*s, co[0]*0.70f,co[1]*0.70f,co[2]*0.70f,0,fx,PRIM_SEG);
      /* Two tarsal claws, not one.  A hunting spider has exactly two and
         they splay to either side of the toe; at this range one peg where
         a pair should be is the tell that gives the model away. */
      float cm=hypot2(gx,gz); cm=1.f/(cm>1e-4f?cm:1e-4f);
      float cpx=-gz*cm, cpz=gx*cm;
      for(int c=0;c<2;c++){
        float sg=c?1.f:-1.f;
        cgSpan(ax2,fy0+0.055f*s,az2,
               fx0+gx*0.17f+cpx*0.052f*s*sg, fy0-0.004f*s,
               fz0+gz*0.17f+cpz*0.052f*s*sg,
               0.036f*s, co[0]*0.42f,co[1]*0.42f,co[2]*0.42f,0,fx,PRIM_HORN);
      }
    }
    if(near_){
      SHELL(1.00f,0.16f,0.0f,0.44f);
      for(int bI=0;bI<2;bI++){
        float u=0.34f+bI*0.30f, aa=bI*2.4f+i*1.31f;
        float spx=hx+(SKNEE[0]-hx)*u, spy=hy+(SKNEE[1]-hy)*u, spz=hz+(SKNEE[2]-hz)*u;
        float ca=cosf(aa), sa=sinf(aa);
        cgSpan(spx,spy,spz,
               spx+ovx*ca*0.11f*s, spy+(0.52f+0.42f*sa)*0.13f*s, spz+ovz*ca*0.11f*s,
               0.0105f*s, co[0]*0.80f,co[1]*0.78f,co[2]*0.82f,0,fx,PRIM_SETA);
      }
    }
  }
  fx->sk=0;
}

/* ---- the cocoon.  The one shape here that is not a spider, and that is
   the point of it: it has to read as something a spider MADE. -------- */
static void drawCocoon(const Zom *z,const float *root,EntFx *fx,EntFx *gfx){
  const ZType *t=ZT+z->type;
  float s=t->sc;
  float A[12],B[12],C[12];
  float br=1.f+sinf(z->phase)*0.045f;          /* it breathes */
  float wob=sinf(z->phase*0.7f)*0.05f;
  mTrans34(0,0.72f*s,0,A); mMul34(root,A,C);
  mRotZ34(wob,A); mMul34(C,A,B);
  fx->sk=2; fx->sf=1.f; fx->si=0.30f; fx->sp=z->id*0.3f; fx->rough=0.70f;
  pushEnt(B,0.46f*s*br,0.72f*s,0.44f*s*br,
          t->coat[0],t->coat[1],t->coat[2],0,fx,PRIM_POD);
  mTrans34(0,0.40f*s,0,A); mMul34(root,A,C);
  pushEnt(C,0.34f*s,0.30f*s,0.33f*s,
          t->coat[0]*0.86f,t->coat[1]*0.86f,t->coat[2]*0.86f,0,fx,PRIM_POD);
  /* the silk that holds it to the plate */
  fx->sk=1; fx->sf=1.f; fx->si=0.10f; fx->sp=0.4f; fx->rough=0.85f;
  for(int i=0;i<6;i++){
    float a=i*1.047f+z->id*0.7f;
    float x0=cosf(a)*0.30f*s, z0=sinf(a)*0.30f*s;
    cgSpan(z->x+x0,z->y+1.34f*s,z->z+z0, z->x+x0*2.4f,z->y+1.95f*s,z->z+z0*2.4f,
           0.020f*s, t->trous[0]*0.7f,t->trous[1]*0.7f,t->trous[2]*0.7f,0,fx,PRIM_SETA);
  }
  /* and the thing inside, lit -- so a cocoon reads as loaded */
  float g=z->dead?0:0.55f+sinf(z->phase*2.3f)*0.20f;
  mTrans34(0,0.66f*s,0.20f*s,A); mMul34(root,A,C);
  pushEnt(C,0.15f*s,0.20f*s,0.10f*s, 1.0f,0.46f,0.12f,g,gfx,PRIM_LENS);
  fx->sk=0;
}

void zomDraw(void){
  for(int i=0;i<MAXZ;i++){
    Zom *z=Z+i;
    if(!z->used) continue;
    float A[12],B[12],C[12];
    int zi=zoneAt(z->x,z->z);
    EntFx fx=ENT_PLAIN, gfx=ENT_PLAIN;
    fx.zone=gfx.zone=(float)zi;
    for(int k=0;k<3;k++){ fx.lit[k]=gfx.lit[k]=z->lit[k];
                          fx.ldir[k]=gfx.ldir[k]=z->ldir[k]; }
    fx.flash=gfx.flash=z->flash;
    fx.rim=0.62f; fx.ao=0.82f; fx.rough=0.82f;
    gfx.rim=0.50f; gfx.ao=0.70f; gfx.rough=0.50f;

    float roll=0, sink=0;
    if(z->dead){
      float u=z->deadT/0.55f; if(u>1) u=1;
      sink=-u*0.42f*ZT[z->type].sc;
      roll=z->fallZ*u*0.5f;
    }
    float bob=(!z->dead&&!z->state)?fabsf(sinf(z->phase))*0.035f:0;
    mTrans34(z->x,z->y+bob+sink,z->z,A);
    mRotY34(z->yaw,B); mMul34(A,B,C);
    if(roll!=0){ float D[12]; mRotZ34(roll,A); mMul34(C,A,D); memcpy(C,D,sizeof C); }
    if(z->type==Z_COCOON) drawCocoon(z,C,&fx,&gfx);
    else                  drawStalker(z,C,&fx,&gfx);
  }
  /* the acid in flight */
  for(int i=0;i<MAXBOLT;i++){
    if(!BOLT[i].used) continue;
    EntFx g=ENT_PLAIN; g.rim=0.2f; g.ao=1.f;
    g.lit[0]=g.lit[1]=g.lit[2]=0.f;
    float A[12]; mTrans34(BOLT[i].x,BOLT[i].y,BOLT[i].z,A);
    pushEnt(A,0.10f,0.13f,0.10f,0.55f,0.85f,0.16f,1.4f,&g,PRIM_LENS);
    fxAddDynLight(BOLT[i].x,BOLT[i].y,BOLT[i].z,0.20f,0.45f,0.06f,3.0f,0.05f);
  }
}

/* the story fires the tell before the body exists, so it has to be able
   to ask how big the thing arriving is without owning ZT */
float zomTypeSize(int type){
  return (type<0||type>=Z_NTYPE)?1.f:ZT[type].sc;
}

void zomInit(void){
  buildNav();
  distPly=(float*)malloc(nvN*4);
  nextPly=(int*)malloc(nvN*4);
  distGoal=(float*)malloc(nvN*4);
  nextGoal=(int*)malloc(nvN*4);
  distFly=(float*)malloc(nvN*4);
  nextFly=(int*)malloc(nvN*4);
  hpK=(float*)malloc((nvN+8)*4);
  hpV=(int*)malloc((nvN+8)*4);
  for(int i=0;i<nvN;i++){ distPly[i]=1e9f; nextPly[i]=-1;
                          distFly[i]=1e9f; nextFly[i]=-1; }
  zomReset();
}
