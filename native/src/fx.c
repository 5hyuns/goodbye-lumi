/* ============================================================
   PARTICLES, DECALS, DEBRIS.  Section G5 of map.html, and the
   two shaders from its GL section that draw them.

   THE CRATER IS THE HARD ONE.  A bullet hole is not a texture
   here -- the fragment shader carries a height profile (floor,
   wall, lip) and takes its normal from the gradient of that
   profile, so the mark is lit by the room it is in and turns
   with the eye.  It also arrives hot and cools over a second and
   a half, which is why the decal instance carries the light that
   was on that wall and its own age.

   Everything is written PREMULTIPLIED, so one blend mode covers
   both the additive spark and the alpha-blended blood.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "torch.h"
#include "app.h"
#include "ent.h"
#include "prim.h"
#include "fx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

GLuint glMakeProgram(const char *vs,const char *fs,const char *what);
void   auImpact(int metal,int wood,float x,float y,float z);

static float rnd01(void){ return (float)rand()/(float)RAND_MAX; }
static float rnd(float a){ return (rnd01()-0.5f)*2.f*a; }

#define MAX_PART     1400
#define PART_STRIDE    12
#define MAX_DECAL     220
#define DECAL_STRIDE   20
#define MAX_HOLE       56     /* holes kept before the oldest is told to close */
#define MAX_DEBRIS     72
#define MAXDYN         16     /* the pool; the brightest MAXD of it are drawn */

typedef struct {
  float x,y,z, vx,vy,vz;
  float s0, t, life;
  int   kind;
  float r,g,b,a0, grav,drag, rot,spin;
  int   add;
} Part;

typedef struct {
  float x,y,z, nx,ny,nz, s;
  float r,g,b,a0, t,life,fw, rot;
  int   kind;
  float lit[3], ldir[3];
} Decal;

typedef struct {
  float x,y,z, vx,vy,vz, sx,sy,sz, r,g,b;
  float life,t, rx,ry,rz, wx,wy,wz, trail;
  int   rest,bleed,ring;
  float lit[3], ldir[3];
} Debris;

static Part   PARTS [MAX_PART];   static int nPart;
static Decal  DECALS[MAX_DECAL];  static int nDecal;
static Debris DEBRIS[MAX_DEBRIS]; static int nDeb;

static float partData [MAX_PART *PART_STRIDE];
static float decalData[MAX_DECAL*DECAL_STRIDE];

static GLuint pPart,pDecal, partVAO,partInst, decalVAO,decalInst, quadVB;
static struct { GLint uProj,uView,uRight,uUp; } UP;
static struct { GLint uProj,uView,uCamPos,uCamDir,uFlash,uFogDensity,
                      uFogCol,uAmb; } UD;

/* ============================================================
   the dynamic light pool

   It used to be four slots inside vmw.c, because the muzzle was
   the only thing in the game that made light of its own.  A
   spark off steel is a second source and an explosion will be a
   third, so the pool lives where all of them can reach it.
   ============================================================ */
static struct { float p[3],c[3],rad,life,t; } DYN[MAXDYN];
static int nDyn;

/* A FULL POOL IS THE NORMAL STATE OF IT, AND SLOT 0 IS NOT THE ONE TO
   LOSE.

   `int i = nDyn<MAXDYN ? nDyn++ : 0` looks like a ring buffer and is not
   one: once the pool is full EVERY caller writes index 0, so the last
   light added in a frame is the only one that survives to fxLights and
   whatever was there before it is gone.  Two things then made that fatal
   rather than untidy.

   The pool is full essentially always.  story.c re-adds the glow under
   every visible item once a FRAME with a 0.06 s life, so each glow sits
   in the pool as three or four copies of itself and sixteen slots are
   gone within a second of boot -- measured at frame three of a new game:
   nDyn 16, and every entry a 0.25-bright pickup disc.

   And the muzzle is added EARLY.  vmwStep runs at the top of the frame
   and storyDrawWorld near the bottom, so the gunshot went into slot 0 and
   the next pickup disc overwrote it, every shot, for the whole game.
   So the room did not light up when a gun fired: the flash quad and the
   viewmodel's own uMuzC were doing all the work and the room never saw
   the shot at all.

   So: a light at the same place as one already in the pool REPLACES it,
   which is what a per-frame glow actually wants and stops it breeding;
   and a full pool gives up its WEAKEST entry, by the same weight
   fxLights ranks with, or refuses the newcomer if the newcomer is the
   weakest thing on offer.  A gunshot is two orders of magnitude brighter
   than a pickup disc, so it can no longer be the one that is dropped. */
static float dynWeight(int i){
  float a=1.f-DYN[i].t/DYN[i].life; if(a<0) a=0;
  return a*(DYN[i].c[0]+DYN[i].c[1]+DYN[i].c[2])*DYN[i].rad;
}
void fxAddDynLight(float x,float y,float z,float r,float g,float b,
                   float rad,float life){
  int i=-1;
  /* the same lamp again, one frame later */
  for(int k=0;k<nDyn;k++){
    float dx=DYN[k].p[0]-x, dy=DYN[k].p[1]-y, dz=DYN[k].p[2]-z;
    if(dx*dx+dy*dy+dz*dz<0.0625f && DYN[k].rad==rad){ i=k; break; }
  }
  if(i<0){
    if(nDyn<MAXDYN) i=nDyn++;
    else{
      float worst=1e30f;
      for(int k=0;k<nDyn;k++){ float w=dynWeight(k); if(w<worst){ worst=w; i=k; } }
      if((r+g+b)*rad < worst) return;      /* nothing in there is worth it */
    }
  }
  DYN[i].p[0]=x; DYN[i].p[1]=y; DYN[i].p[2]=z;
  DYN[i].c[0]=r; DYN[i].c[1]=g; DYN[i].c[2]=b;
  DYN[i].rad=rad; DYN[i].life=life; DYN[i].t=0;
}

/* BRIGHTEST FIRST, so the eight slots the shader carries go to what the eye
   will actually notice.  A selection rather than a sort: only the eight that
   survive it are ever read, and the pool is sixteen long. */
void fxLights(FrameState *F){
  int used[MAXDYN]; memset(used,0,sizeof used);
  while(F->dynN<MAXD){
    int best=-1; float bestW=0;
    for(int i=0;i<nDyn;i++){
      if(used[i]) continue;
      float k=1.f-DYN[i].t/DYN[i].life;
      if(k<=0) continue;
      float w=k*(DYN[i].c[0]+DYN[i].c[1]+DYN[i].c[2])*DYN[i].rad;
      if(w>bestW){ bestW=w; best=i; }
    }
    if(best<0) break;
    used[best]=1;
    float k=1.f-DYN[best].t/DYN[best].life, f=k*k;
    int j=F->dynN++;
    for(int c=0;c<3;c++){
      F->dynP[j*3+c]=DYN[best].p[c];
      F->dynC[j*3+c]=DYN[best].c[c]*f;
    }
    F->dynR[j]=DYN[best].rad;
  }
}

/* ============================================================
   particles
   ============================================================ */
void fxEmit(float x,float y,float z,float vx,float vy,float vz,
            float size,float life,int kind,
            float r,float g,float b,float a,
            float grav,float drag,int add,float spin){
  if(nPart>=MAX_PART){                          /* the oldest gives way */
    memmove(PARTS,PARTS+1,sizeof(Part)*(MAX_PART-1));
    nPart=MAX_PART-1;
  }
  Part *p=&PARTS[nPart++];
  p->x=x; p->y=y; p->z=z; p->vx=vx; p->vy=vy; p->vz=vz;
  p->s0=size; p->t=0; p->life=life; p->kind=kind;
  p->r=r; p->g=g; p->b=b; p->a0=a;
  p->grav=grav; p->drag=drag; p->add=add;
  p->rot=rnd01()*6.28f; p->spin=spin;
}

static void updateParticles(float dt){
  for(int i=nPart-1;i>=0;i--){
    Part *p=&PARTS[i];
    p->t+=dt;
    if(p->t>=p->life){
      memmove(PARTS+i,PARTS+i+1,sizeof(Part)*(nPart-i-1));
      nPart--; continue;
    }
    float k=1.f-(p->drag*dt<1.f?p->drag*dt:1.f);
    p->vx*=k; p->vz*=k; p->vy=p->vy*k-p->grav*dt;
    p->x+=p->vx*dt; p->y+=p->vy*dt; p->z+=p->vz*dt;
    p->rot+=p->spin*dt;
  }
}

static int fillParticles(void){
  int n=0;
  for(int i=0;i<nPart&&n<MAX_PART;i++){
    Part *p=&PARTS[i];
    float u=p->t/p->life, *d=partData+n*PART_STRIDE;
    float a=p->a0*(1-u), size=p->s0;
    if     (p->kind==PK_SMOKE){ size=p->s0*(1+u*2.4f);     a=p->a0*(1-u)*(1-u); }
    else if(p->kind==PK_RING ){ size=p->s0*(0.25f+u*1.9f); a=p->a0*(1-u)*(1-u); }
    else if(p->kind==PK_SPARK){ size=p->s0*(1-u*0.6f);     a=p->a0*(1-u)*(1-u); }
    else if(p->kind==PK_BLOB ){ size=p->s0*(1+u*0.55f); }
    d[0]=p->x; d[1]=p->y; d[2]=p->z; d[3]=size;
    d[4]=p->r*a; d[5]=p->g*a; d[6]=p->b*a; d[7]=p->add?0.f:a;
    d[8]=(float)p->kind; d[9]=p->rot;
    d[10]=fmodf(p->x*7.3f+p->z*3.1f,10.f); d[11]=1.f;   /* seed, stretch */
    n++;
  }
  return n;
}

/* ============================================================
   decals
   ============================================================ */
void fxDecal(float x,float y,float z,float nx,float ny,float nz,
             float size,float r,float g,float b,float a,int kind){
  if(nDecal>=MAX_DECAL){
    memmove(DECALS,DECALS+1,sizeof(Decal)*(MAX_DECAL-1));
    nDecal=MAX_DECAL-1;
  }
  Decal *D=&DECALS[nDecal++];
  D->x=x; D->y=y; D->z=z; D->nx=nx; D->ny=ny; D->nz=nz; D->s=size;
  D->r=r; D->g=g; D->b=b; D->a0=a;
  D->t=0; D->life=150.f; D->fw=14.f; D->rot=rnd01()*6.28f; D->kind=kind;
  D->lit[0]=D->lit[1]=D->lit[2]=0;
  D->ldir[0]=0; D->ldir[1]=1; D->ldir[2]=0;
  if(kind==1){
    /* what was actually on that wall when the round arrived, sampled once
       with the same shadowed tracer the lightmap used.  A crater lit by a
       guess is a crater that floats. */
    sampleLight(x+nx*0.06f,y+ny*0.06f,z+nz*0.06f,D->lit,D->ldir);
    D->life=110.f;
    /* A CAP THAT FADES RATHER THAN POPS.  Past MAX_HOLE the oldest hole is
       told to close over three quarters of a second; dropping it out from
       under the player is the one thing that would be noticed. */
    int holes=0;
    for(int i=0;i<nDecal;i++) if(DECALS[i].kind==1) holes++;
    if(holes>=MAX_HOLE){
      for(int i=0;i<nDecal;i++){
        Decal *O=&DECALS[i];
        if(O->kind==1&&O->life-O->t>0.75f){ O->life=O->t+0.75f; O->fw=0.75f; break; }
      }
    }
  }
}

static int fillDecals(float dt){
  int n=0;
  for(int i=nDecal-1;i>=0;i--){
    DECALS[i].t+=dt;
    if(DECALS[i].t>DECALS[i].life){
      memmove(DECALS+i,DECALS+i+1,sizeof(Decal)*(nDecal-i-1));
      nDecal--;
    }
  }
  for(int i=0;i<nDecal&&n<MAX_DECAL;i++){
    Decal *D=&DECALS[i];
    float *d=decalData+n*DECAL_STRIDE;
    float fade = D->t>D->life-D->fw ? (D->life-D->t)/D->fw : 1.f;
    if(fade<0) fade=0;
    d[0]=D->x; d[1]=D->y; d[2]=D->z; d[3]=D->s;
    d[4]=D->nx; d[5]=D->ny; d[6]=D->nz; d[7]=D->rot;
    d[8]=D->r; d[9]=D->g; d[10]=D->b; d[11]=D->a0*fade;
    d[12]=D->lit[0]; d[13]=D->lit[1]; d[14]=D->lit[2]; d[15]=D->t;
    d[16]=D->ldir[0]; d[17]=D->ldir[1]; d[18]=D->ldir[2]; d[19]=(float)D->kind;
    n++;
  }
  return n;
}

/* ============================================================
   debris -- loose boxes with a bit of physics: gibs, wreckage
   ============================================================ */
void fxDebris(float x,float y,float z,float vx,float vy,float vz,
              float sx,float sy,float sz,float r,float g,float b,
              float life,int bleed,int ring){
  if(nDeb>=MAX_DEBRIS){
    memmove(DEBRIS,DEBRIS+1,sizeof(Debris)*(MAX_DEBRIS-1));
    nDeb=MAX_DEBRIS-1;
  }
  Debris *d=&DEBRIS[nDeb++];
  memset(d,0,sizeof *d);
  d->x=x; d->y=y; d->z=z; d->vx=vx; d->vy=vy; d->vz=vz;
  d->sx=sx; d->sy=sy; d->sz=sz; d->r=r; d->g=g; d->b=b;
  d->life=life; d->bleed=bleed; d->ring=ring;
  d->rx=rnd01()*6.28f; d->ry=rnd01()*6.28f; d->rz=rnd01()*6.28f;
  d->wx=rnd(8.f); d->wy=rnd(8.f); d->wz=rnd(8.f);
  d->ldir[1]=1;
}

static void updateDebris(float dt){
  for(int i=nDeb-1;i>=0;i--){
    Debris *d=&DEBRIS[i];
    d->t+=dt;
    if(d->t>=d->life){
      memmove(DEBRIS+i,DEBRIS+i+1,sizeof(Debris)*(nDeb-i-1));
      nDeb--; continue;
    }
    if(d->rest) continue;
    d->vy-=24.f*dt;
    float nx=d->x+d->vx*dt, ny=d->y+d->vy*dt, nz=d->z+d->vz*dt;
    if(solidAt(nx,ny+0.02f,nz)||solidAt(nx,ny+d->sy*0.5f,nz)){
      /* find which way we came in and bounce off that */
      if(!solidAt(d->x,ny+0.02f,d->z)){ d->x=nx; d->z=nz; d->vy*=-0.32f; d->y=ny; }
      else{
        d->vy*=-0.30f;
        if(solidAt(nx,d->y+0.05f,d->z)) d->vx*=-0.4f;
        if(solidAt(d->x,d->y+0.05f,nz)) d->vz*=-0.4f;
        if(ny>d->y) d->y=ny;
      }
      d->vx*=0.62f; d->vz*=0.62f;
      if(fabsf(d->vy)<0.55f&&hypot2(d->vx,d->vz)<0.5f){
        d->rest=1; d->vx=d->vy=d->vz=0; d->wx=d->wy=d->wz=0;
        if(d->bleed) fxDecal(d->x,d->y-d->sy*0.4f,d->z,0,1,0,
                             0.42f+rnd01()*0.4f,0.34f,0.03f,0.03f,0.82f,0);
      }
    } else { d->x=nx; d->y=ny; d->z=nz; }
    d->rx+=d->wx*dt; d->ry+=d->wy*dt; d->rz+=d->wz*dt;
    if(d->bleed){
      d->trail+=dt;
      if(d->trail>0.045f){ d->trail=0;
        fxEmit(d->x,d->y,d->z,0,0,0,0.06f+rnd01()*0.05f,0.45f,PK_BLOB,
               0.42f,0.05f,0.05f,0.75f,3,3,0,0); }
    }
  }
}

void fxPushDebris(void){
  for(int i=0;i<nDeb;i++){
    Debris *d=&DEBRIS[i];
    float A[12],B[12],C[12],D[12];
    mTrans34(d->x,d->y,d->z,A); mRotY34(d->ry,B); mMul34(A,B,C);
    mRotX34(d->rx,B); mMul34(C,B,D);
    mRotZ34(d->rz,B); mMul34(D,B,C);
    float u=d->t/d->life, al = u>0.85f ? (1-u)/0.15f : 1.f;
    EntFx fx=ENT_PLAIN;
    fx.alpha=al; fx.zone=(float)zoneAt(d->x,d->z);
    fx.rim=0.35f; fx.ao=0.8f; fx.rough=d->ring?0.28f:0.75f;
    for(int k=0;k<3;k++){ fx.lit[k]=d->lit[k]; fx.ldir[k]=d->ldir[k]; }
    pushEnt(C,d->sx,d->sy,d->sz,d->r,d->g,d->b,d->ring?0.05f:0.f,
            &fx, d->ring?PRIM_CYL:PRIM_BEVEL);
  }
}

/* ============================================================
   what one round does to a wall
   ============================================================ */
void fxSurfaceHit(int bi,float x,float y,float z,float nx,float ny,float nz){
  int mat = bi>=0 ? BRUSHES[bi].t : T_PANEL;
  /* THE TWO LISTS DIFFER BY ONE MATERIAL, and are copied as they stand:
     the ear calls a VENT metal, the eye calls a LAMP metal. */
  int rings = (mat==T_PIPE||mat==T_PANEL||mat==T_RUST||mat==T_GRATE||mat==T_VENT);
  int metal = (mat==T_PIPE||mat==T_PANEL||mat==T_RUST||mat==T_GRATE||mat==T_LAMP);
  auImpact(rings, mat==T_CRATE, x,y,z);
  for(int i=0;i<(metal?9:6);i++){
    float sp=1.5f+rnd01()*5.f;
    float jx=nx+rnd(0.7f);
    float jy=ny+rnd(0.7f)+0.2f;
    float jz=nz+rnd(0.7f);
    fxEmit(x+nx*0.02f,y+ny*0.02f,z+nz*0.02f, jx*sp,jy*sp,jz*sp,
           metal?0.030f:0.045f, metal?0.28f:0.5f, metal?PK_SPARK:PK_BLOB,
           metal?1.0f:0.42f, metal?0.72f:0.40f, metal?0.30f:0.36f,
           metal?1.0f:0.5f, metal?14.f:3.f, 1.6f, metal?1:0, 0);
  }
  fxEmit(x+nx*0.05f,y+ny*0.05f,z+nz*0.05f,nx*0.5f,ny*0.5f+0.3f,nz*0.5f,
         0.10f,0.42f,PK_SMOKE,0.42f,0.40f,0.37f,0.35f,0.4f,2.4f,0,0);
  /* A HOLE IS NOT ONE SIZE.  Square on, a round leaves a neat 90 mm crater;
     arriving at a slant it tears a gouge twice that, and the spread between
     the two is most of what a wall full of them reads as.  Skewed small, so
     the big ones stay events. */
  if(mat!=T_LAMP)
    fxDecal(x,y,z,nx,ny,nz, 0.045f+powf(rnd01(),2.3f)*0.145f,
            0.046f,0.041f,0.036f, 0.94f, 1);
  if(metal) fxAddDynLight(x+nx*0.1f,y+ny*0.1f,z+nz*0.1f,
                          0.9f,0.6f,0.25f, 2.4f, 0.07f);
}

void fxTracer(const float *a,const float *b){
  float dx=b[0]-a[0], dy=b[1]-a[1], dz=b[2]-a[2];
  float L=hypot3(dx,dy,dz);
  int n=(int)(L*0.35f); if(n<2) n=2; if(n>9) n=9;
  for(int i=0;i<n;i++){
    float u=(i+0.5f)/n;
    fxEmit(a[0]+dx*u,a[1]+dy*u,a[2]+dz*u,0,0,0,0.030f+u*0.012f,0.055f,
           PK_SPARK,1.0f,0.86f,0.55f,0.85f,0,0.1f,1,0);
  }
}

void fxStep(float dt){
  updateParticles(dt);
  updateDebris(dt);
  for(int i=0;i<nDyn;i++) DYN[i].t+=dt;
  { int k=0;
    for(int i=0;i<nDyn;i++) if(DYN[i].t<DYN[i].life) DYN[k++]=DYN[i];
    nDyn=k; }
}

void fxReset(void){ nPart=0; nDecal=0; nDeb=0; nDyn=0; }

/* ============================================================
   THE TWO SHADERS

   Both are instanced camera-facing quads over the same six
   vertices; the particle carries twelve floats an instance and
   the decal twenty, because the crater has to remember the light
   that was on the wall, its own age and its kind besides where
   it is.
   ============================================================ */
static const char *PART_VS =
"#version 330 core\n"
"layout(location=0) in vec2 aQuad;\n"
"layout(location=1) in vec4 aP;\n"     /* xyz world, w size */
"layout(location=2) in vec4 aC;\n"     /* rgb premultiplied, a coverage */
"layout(location=3) in vec4 aK;\n"     /* x kind, y rotation, z seed, w stretch */
"uniform mat4 uProj,uView;\n"
"uniform vec3 uRight,uUp;\n"
"out vec2 vUV; out vec4 vC; out vec3 vK;\n"
"void main(){\n"
"  float c=cos(aK.y), sn=sin(aK.y);\n"
"  vec2 q=vec2(aQuad.x*c-aQuad.y*sn, aQuad.x*sn+aQuad.y*c);\n"
"  q.y*=aK.w;\n"
"  vec3 wp=aP.xyz+(uRight*q.x+uUp*q.y)*aP.w;\n"
"  vUV=aQuad; vC=aC; vK=vec3(aK.x,aK.z,aP.w);\n"
"  gl_Position=uProj*uView*vec4(wp,1.0);\n"
"}\n";

static const char *PART_FS =
"#version 330 core\n"
"in vec2 vUV; in vec4 vC; in vec3 vK;\n"
"out vec4 outColor;\n"
"float h21(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }\n"
"void main(){\n"
"  float r=length(vUV);\n"
"  int kind=int(vK.x+0.5);\n"
"  float a;\n"
"  if(kind==0){\n"                     /* soft blob -- blood mist, dust */
"    a=1.0-smoothstep(0.20,1.0,r); a*=a;\n"
"  } else if(kind==1){\n"              /* spark -- hot core, quick falloff */
"    a=exp(-r*r*7.0)+exp(-r*8.0)*0.35;\n"
"  } else if(kind==2){\n"              /* smoke puff, broken up by noise */
"    float n=0.0, amp=0.55; vec2 uv=vUV*1.7+vK.y;\n"
"    for(int i=0;i<3;i++){ n+=h21(floor(uv*4.0))*amp; uv*=2.03; amp*=0.5; }\n"
"    a=(1.0-smoothstep(0.15,1.0,r))*(0.45+n*0.75);\n"
"  } else if(kind==3){\n"              /* shockwave ring */
"    a=exp(-pow(abs(r-0.78)*7.0,2.0));\n"
"  } else {\n"                         /* gore chunk / wet fleck */
"    float w=0.55+0.45*sin(vK.y*13.0+vUV.x*4.0);\n"
"    a=1.0-smoothstep(0.30*w,0.95,r);\n"
"  }\n"
"  a=clamp(a,0.0,1.0);\n"
"  outColor=vec4(vC.rgb*a,vC.a*a);\n"
"}\n";

static const char *DECAL_VS =
"#version 330 core\n"
"layout(location=0) in vec2 aQuad;\n"
"layout(location=1) in vec4 aP;\n"     /* xyz centre, w size */
"layout(location=2) in vec4 aN;\n"     /* xyz surface normal, w rotation */
"layout(location=3) in vec4 aC;\n"     /* rgb colour, a strength */
"layout(location=4) in vec4 aL;\n"     /* rgb the light that was on it, a age */
"layout(location=5) in vec4 aD;\n"     /* xyz that light's direction, w kind */
"uniform mat4 uProj,uView;\n"
"uniform vec3 uCamPos;\n"
"out vec2 vUV; out vec4 vC,vL; out vec3 vN,vTX,vTY,vP,vLD;\n"
"out float vSeed,vKind;\n"
"void main(){\n"
"  vec3 n=normalize(aN.xyz);\n"
"  vec3 t=abs(n.y)>0.9?vec3(1,0,0):vec3(0,1,0);\n"
"  vec3 tx=normalize(cross(t,n)), ty=cross(n,tx);\n"
"  float c=cos(aN.w), s=sin(aN.w);\n"
"  vec2 q=vec2(aQuad.x*c-aQuad.y*s, aQuad.x*s+aQuad.y*c);\n"
/* TWO SEPARATE PROBLEMS, TWO SEPARATE FIXES.  The plate does want to stand
   slightly proud of the wall -- 10 mm, so its lip has somewhere to be -- but
   no world offset can cure the shimmer, because the fight is in the DEPTH
   BUFFER and its resolution collapses with range: 14 mm is a mile at 2 m and
   inside one step at 30 m.  So the raise stays constant and the depth is
   biased in clip space instead, which is a fixed number of buffer steps at
   every distance and moves nothing on screen. */
"  vec3 wp=aP.xyz+n*0.010+(tx*q.x+ty*q.y)*aP.w;\n"
"  vTX=tx*c+ty*s; vTY=ty*c-tx*s;\n"
"  vUV=aQuad; vC=aC; vL=aL; vLD=aD.xyz; vKind=aD.w; vSeed=aN.w;\n"
"  vN=n; vP=wp;\n"
"  gl_Position=uProj*uView*vec4(wp,1.0);\n"
"  gl_Position.z-=0.0022*gl_Position.w;\n"
"}\n";

static const char *DECAL_FS =
"#version 330 core\n"
"in vec2 vUV; in vec4 vC,vL; in vec3 vN,vTX,vTY,vP,vLD;\n"
"in float vSeed,vKind;\n"
"uniform vec3 uCamPos,uCamDir,uFogCol,uAmb;\n"
"uniform float uFlash,uFogDensity;\n"
/* how deep the crater is, as a fraction of its own radius.  0.38 puts the
   steepest part of the wall near 45 degrees, which is what a jacketed round
   leaves in plate; past about 0.5 it stops being a hole and starts being a
   funnel. */
"const float DENT=0.38;\n"
"out vec4 outColor;\n"
"float h21(vec2 p){ return fract(sin(dot(p,vec2(269.5,183.3)))*43758.5453); }\n"
TORCH_GLSL
/* THE PROFILE IS THE WHOLE PART.  Height above the plate, in quad units: a
   floor punched below it, a wall, and a lip of metal pushed back out around
   the rim.  Everything else here is read off this one function. */
"float crater(float a){\n"
"  float flr=1.0-smoothstep(0.04,0.40,a);\n"
"  float lip=smoothstep(0.28,0.45,a)*(1.0-smoothstep(0.45,0.68,a));\n"
"  return lip*0.42-flr;\n"
"}\n"
"void main(){\n"
"  float r=length(vUV);\n"
"  vec3 col; float a;\n"
"  if(vKind>0.5){\n"
/* the rim is torn, not turned -- but SMOOTHLY torn.  Hashing the radius per
   angular sector made a cog, and worse, made the hot core a starburst,
   because everything downstream is measured in r/wob. */
"    float ang=atan(vUV.y,vUV.x);\n"
"    float wob=1.0+0.10*sin(ang*3.0+vSeed*6.3)+0.055*sin(ang*5.0-vSeed*3.1);\n"
"    float e=0.022;\n"
"    float h0=crater(r/wob);\n"
"    float hx=crater(length(vUV+vec2(e,0.0))/wob);\n"
"    float hy=crater(length(vUV+vec2(0.0,e))/wob);\n"
"    vec3 N=normalize(vN-(vTX*(hx-h0)+vTY*(hy-h0))*(DENT/e));\n"
"    float aa=r/wob;\n"
"    float flr=1.0-smoothstep(0.04,0.40,aa);\n"
"    float lip=smoothstep(0.28,0.45,aa)*(1.0-smoothstep(0.45,0.68,aa));\n"
/* torn metal on the lip, scorch on the floor, soot fading out past it */
"    vec3 alb=mix(vC.rgb,vec3(0.150,0.143,0.134),clamp(lip*1.4,0.0,1.0));\n"
"    alb*=1.0-0.55*flr;\n"
"    vec3 V=normalize(uCamPos-vP);\n"
"    vec3 L=normalize(vLD+vec3(1e-5));\n"
"    float nl=max(dot(N,L),0.0);\n"
"    vec3 lit=vL.rgb*(nl*0.94+0.06)+uAmb;\n"
/* the crater has to be lit by the SAME beam the plate around it is -- a
   cookie on the wall and a plain cone in the hole is worse than neither */
"    if(uFlash>0.5){\n"
"      vec4 tb=torchBeam(vP,uCamPos,uCamDir);\n"
"      if(tb.a>0.0){\n"
"        vec3 Lf=normalize(uCamPos-vP);\n"
"        lit+=tb.rgb*(max(dot(N,Lf),0.0)*0.94+0.06);\n"
"        L=normalize(L+Lf*tb.a*2.0);\n"
"      }\n"
"    }\n"
"    float sp=pow(max(dot(N,normalize(L+V)),0.0),40.0)*lip*1.5;\n"
"    col=alb*lit+lit*sp;\n"
/* AND IT ARRIVES HOT.  White at the instant of the hit, down through yellow
   and orange into a deep red, gone in about a second and a half.  Only the
   floor of the crater glows; the lip is already cold because it is the metal
   that got out of the way. */
"    float heat=exp(-vL.a*2.3);\n"
"    float core=(1.0-smoothstep(0.02,0.30,r))*heat;\n"
"    col+=vec3(1.30,0.30,0.045)*core*core\n"
"        +vec3(1.45,1.15,0.62)*pow(core,4.0)*1.4;\n"
/* COVERAGE, AND THIS IS WHAT MADE IT A STICKER.  One opaque disc out to 74%
   of the quad is a black circle painted on the wall, whatever is drawn inside
   it.  The hole itself is opaque; everything past its lip is SOOT, which is a
   veil that thins to nothing -- so the mark ends by fading into the plate
   instead of by having an edge. */
"    float body=1.0-smoothstep(0.62,0.82,aa);\n"
"    float soot=(1.0-smoothstep(0.10,1.00,r));\n"
"    a=max(body,soot*soot*0.55)*vC.a;\n"
"  }else{\n"
/* the old splat, unchanged: a pool, six satellites and a drip */
"    float d=r*1.22;\n"
"    float m=1.0-smoothstep(0.42,0.80,d);\n"
"    for(int i=0;i<6;i++){\n"
"      float fi=float(i);\n"
"      float ang2=h21(vec2(vSeed,fi))*6.2831;\n"
"      float rr=0.30+h21(vec2(fi,vSeed))*0.62;\n"
"      float sz=0.10+h21(vec2(vSeed*1.7,fi*3.1))*0.20;\n"
"      vec2 c=vec2(cos(ang2),sin(ang2))*rr;\n"
"      m=max(m,(1.0-smoothstep(sz*0.45,sz,length(vUV-c)))*0.94);\n"
"    }\n"
"    float dr=1.0-smoothstep(0.05,0.13,abs(vUV.x-(h21(vec2(vSeed,9.0))-0.5)*0.7));\n"
"    m=max(m,dr*(1.0-smoothstep(0.1,0.95,vUV.y))*step(0.0,vUV.y)*0.55);\n"
"    a=clamp(m,0.0,1.0)*vC.a;\n"
"    col=vC.rgb*(0.62+0.38*(1.0-smoothstep(0.0,0.55,d)));\n"
"  }\n"
"  if(a<0.01) discard;\n"
"  float fog=1.0-exp(-length(uCamPos-vP)*uFogDensity);\n"
"  col=mix(col,uFogCol,clamp(fog,0.0,1.0));\n"
"  outColor=vec4(max(col,0.0)*a,a);\n"
"}\n";

int fxInit(void){
  static const float QUAD_V[12]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};

  pPart =glMakeProgram(PART_VS ,PART_FS ,"part");  if(!pPart)  return 0;
  pDecal=glMakeProgram(DECAL_VS,DECAL_FS,"decal"); if(!pDecal) return 0;
#define U(p,s,n) s.n=glGetUniformLocation(p,#n)
  U(pPart,UP,uProj); U(pPart,UP,uView); U(pPart,UP,uRight); U(pPart,UP,uUp);
  U(pDecal,UD,uProj); U(pDecal,UD,uView); U(pDecal,UD,uCamPos);
  U(pDecal,UD,uCamDir); U(pDecal,UD,uFlash); U(pDecal,UD,uFogDensity);
  U(pDecal,UD,uFogCol); U(pDecal,UD,uAmb);
#undef U

  glGenBuffers(1,&quadVB);
  glBindBuffer(GL_ARRAY_BUFFER,quadVB);
  glBufferData(GL_ARRAY_BUFFER,sizeof QUAD_V,QUAD_V,GL_STATIC_DRAW);

  GLuint vao[2]={0,0}, inst[2]={0,0};
  const int nv[2]={3,5};
  const GLsizeiptr bytes[2]={sizeof partData,sizeof decalData};
  glGenVertexArrays(2,vao);
  glGenBuffers(2,inst);
  for(int k=0;k<2;k++){
    glBindVertexArray(vao[k]);
    glBindBuffer(GL_ARRAY_BUFFER,quadVB);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,(void*)0);
    glBindBuffer(GL_ARRAY_BUFFER,inst[k]);
    glBufferData(GL_ARRAY_BUFFER,bytes[k],NULL,GL_DYNAMIC_DRAW);
    for(int i=0;i<nv[k];i++){
      glEnableVertexAttribArray(1+i);
      glVertexAttribPointer(1+i,4,GL_FLOAT,GL_FALSE,nv[k]*16,
                            (void*)(size_t)(i*16));
      glVertexAttribDivisor(1+i,1);
    }
    glBindVertexArray(0);
  }
  partVAO =vao[0];  partInst =inst[0];
  decalVAO=vao[1];  decalInst=inst[1];

  platLog("fx     %d particle slots  %d decal slots  %d debris",
          MAX_PART,MAX_DECAL,MAX_DEBRIS);
  return 1;
}

/* ---- the two passes -------------------------------------------------- */
void fxDrawDecals(const FrameState *F,float dt){
  if(R_DEBUG) return;
  int n=fillDecals(dt);
  if(!n) return;
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);   /* everything premultiplied */
  glDepthMask(GL_FALSE);
  glUseProgram(pDecal);
  glUniformMatrix4fv(UD.uProj,1,GL_FALSE,F->proj);
  glUniformMatrix4fv(UD.uView,1,GL_FALSE,F->view);
  glUniform3fv(UD.uCamPos,1,F->camPos);
  glUniform3fv(UD.uCamDir,1,F->camDir);
  glUniform1f(UD.uFlash,F->flash?1.f:0.f);
  glUniform1f(UD.uFogDensity,F->fogDensity);
  glUniform3fv(UD.uFogCol,1,F->fogCol);
  glUniform3f(UD.uAmb,F->minLight[0]*0.9f,F->minLight[1]*0.9f,
                      F->minLight[2]*0.9f);
  glBindBuffer(GL_ARRAY_BUFFER,decalInst);
  glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)n*DECAL_STRIDE*4,decalData);
  glBindVertexArray(decalVAO);
  glDrawArraysInstanced(GL_TRIANGLES,0,6,n);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void fxDrawParticles(const FrameState *F){
  if(R_DEBUG) return;
  int n=fillParticles();
  if(!n) return;
  /* the billboard basis, straight off the view matrix -- the same two rows
     the corona shader reads for itself */
  float right[3]={F->view[0],F->view[4],F->view[8]};
  float up   [3]={F->view[1],F->view[5],F->view[9]};
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glUseProgram(pPart);
  glUniformMatrix4fv(UP.uProj,1,GL_FALSE,F->proj);
  glUniformMatrix4fv(UP.uView,1,GL_FALSE,F->view);
  glUniform3fv(UP.uRight,1,right);
  glUniform3fv(UP.uUp,1,up);
  glBindBuffer(GL_ARRAY_BUFFER,partInst);
  glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)n*PART_STRIDE*4,partData);
  glBindVertexArray(partVAO);
  glDrawArraysInstanced(GL_TRIANGLES,0,6,n);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}
