/* ============================================================
   AUTOMATIC BULKHEAD DOORS

   THE PLAN ALREADY DREW THEM.  deckplan.py has 34 door rects and
   shipdetail.py builds their frames --
   the head at 2.62 m, a jamb either side, a hazard strip on the
   deck -- with the comment "the leaf is dynamic and comes later".
   This is later.  DOORS is the list it left behind: {x,z,w,d,ax},
   where ax=1 means the opening runs along X and ax=0 along Z.

   WHY A LEAF THAT SHRINKS AND A NOSE THAT SLIDES.  A bi-parting
   door 1.8 m per leaf needs 1.8 m of wall pocket to vanish into,
   and there is no pocket -- the jamb is 600 mm, exactly the deck
   splay it stands on.  So the leaf's outer edge is pinned at the
   jamb and its width goes to zero.  The cheat is only invisible
   if nothing on the leaf reveals the scaling, which is why every
   feature on the BODY is HORIZONTAL -- bands, a rail, a kick
   plate -- all of which stretch along their own length and show
   nothing, and every feature that is vertical (the nose rib, the
   interlocking teeth, the lit edge) is drawn at a FIXED size and
   translated.  The eye tracks the nose, so the door reads as
   sliding into the wall.

   AND IT IS A MOTION SENSOR, NOT A BUTTON.  It opens for the
   player and for anything else alive in front of it, which is
   what keeps the pathing honest: the doors are not navigation
   blockers, so the AI walks its usual route and the door is open
   by the time it arrives.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include "ent.h"
#include "prim.h"
#include "lumi.h"
#include <string.h>
#include <stdlib.h>

void auSlide(float,float,float,int);
void auLatch(float,float,float);

#define AD_HEIGHT 2.62f   /* the clear height shipdetail.py leaves under the head */
#define AD_PERP   3.30f   /* how far off the face the sensor reaches */
#define AD_PAD    0.80f   /* ... and how far past the jamb */
#define AD_DWELL  1.10f   /* it stays open this long after you step out */
/* SHE DOES NOT GET TO SHUT A DOOR IN YOUR FACE.

   A leaf is a motion sensor and Lumi is one of the two things that stand
   on it.  She leads, so she reaches it first, it opens for her, she goes
   through -- and AD_DWELL later it shuts again, with the player still on
   the near side and the instruction on screen still saying to follow her.
   The player's own sensor will reopen it, but only after they have walked
   up to a closed bulkhead that was open a second ago, and if the beat on
   the far side was waiting on them arriving, it waits.

   So a leaf SHE triggered while she was nearer to it than the player is
   held open until the player reaches it too.  AD_HELD is the outside: a
   door she brushed past on an errand the player never follows must not
   stand open for the rest of the game. */
#define AD_HELD   30.0f
#define AD_OPEN_S 2.35f   /* 1/s -- 0.43 s to open ... */
#define AD_SHUT_S 1.45f   /*        ... and 0.69 s to close, like every servo */
#define AD_DRAW   34.0f   /* drawn within this */
#define AD_FINE   16.0f   /* detailed within this */
#define AD_SOLID  32.0f   /* and solid within this */

/* Machined steel and the three accent roles, and nothing else coloured.
   THE METAL ONES ARE AUTHORED BRIGHT ON PURPOSE.  The MACHINED kind spends
   88% of a conductor's albedo on a tinted specular and leaves the rest as
   diffuse, so 0.48 steel arrives on screen at about 0.14 and only lights up
   where it catches a lamp -- which is the entire look.  Read the second
   number as "how much of this is metal", not as a finish. */
static const float AD_STEEL[3]={0.545f,0.531f,0.500f}; static const float MET_STEEL=0.90f;
static const float AD_TRIM [3]={0.735f,0.712f,0.658f}; static const float MET_TRIM =0.95f;
static const float AD_GAP  [3]={0.115f,0.117f,0.124f}; static const float MET_GAP  =0.60f;
static const float AD_KICK [3]={0.360f,0.352f,0.334f}; static const float MET_KICK =0.90f;
static const float AD_OCHRE[3]={0.360f,0.252f,0.033f}; static const float MET_OCHRE=0.10f;
static const float AD_AMBER[3]={1.00f,0.60f,0.15f};
static const float AD_CYAN [3]={0.30f,0.86f,1.10f};
/* THE STATUS LAMP IS NOT AN ACCENT, IT IS A SIGN.  House rule 7 keeps the
   ship to ochre, amber and cyan, and the leaf's own running strip still
   obeys it -- but the eye on the head answers one question the player has
   to be able to ask from the far end of a corridor before walking it, and
   the two answers to that question are green and red everywhere a human
   has ever seen a door.  Nothing else in this ship is allowed either. */
static const float AD_GO  [3]={0.20f,1.00f,0.38f};
static const float AD_NO  [3]={1.00f,0.14f,0.10f};

typedef struct {
  float x,z; int ax;
  float hw,th,H;
  float t,e,hold; int was,moved;
  int   lock, force;              /* the story's two overrides */
  float held;                     /* she went through: hold it for the player */
  int   zi;
  float litT, lit[3], ldir[3];
} Door;

static Door *AD; static int nAD;
/* closed leaves as solid boxes -- what boxSolid and a bullet trace see */
static float ADB[128*6]; static int nADB;
static float adDynT;
static int adReady;

static void adInit(void){
  if(adReady) return;
  adReady=1;
  AD=(Door*)calloc(nDoor?nDoor:1,sizeof(Door));
  for(int i=0;i<nDoor;i++){
    DoorSlot *s=&DOORS[i];
    int ax=s->ax?1:0;
    float span=ax?s->w:s->d, th=ax?s->d:s->w;
    if(span<0.8f) continue;
    Door *D=&AD[nAD++];
    D->x=s->x; D->z=s->z; D->ax=ax;
    D->hw=span*0.5f; D->th=th; D->H=AD_HEIGHT;
    D->zi=zoneAt(s->x,s->z);
    D->litT=ihash(i,7,3)*0.31f;
    D->ldir[1]=1;
  }
  platLog("doors  %d leaves",nAD);
}

/* ---- the sensor.  A rectangle that hugs the opening, not a circle: a
   3.6 m door has to answer you at its edge as well as at its middle. */
static int adSensed(const Door *D,float px,float pz){
  float a=D->ax? px-D->x : pz-D->z;
  float p=D->ax? pz-D->z : px-D->x;
  if(a<0)a=-a; if(p<0)p=-p;
  return p<AD_PERP && a<D->hw+AD_PAD;
}

/* what the player walks into: the leaves that are still across the way */
int adBoxSolid(float ax0,float ay0,float az0,float ax1,float ay1,float az1){
  for(int i=0;i<nADB;i++){
    const float *b=ADB+i*6;
    if(ax1>b[0]&&ax0<b[3]&&ay1>b[1]&&ay0<b[4]&&az1>b[2]&&az0<b[5]) return 1;
  }
  return 0;
}

static void dynLight(FrameState *F,float x,float y,float z,
                     float r,float g,float b,float rad){
  if(F->dynN>=MAXD) return;
  int i=F->dynN++;
  F->dynP[i*3]=x; F->dynP[i*3+1]=y; F->dynP[i*3+2]=z;
  F->dynC[i*3]=r; F->dynC[i*3+1]=g; F->dynC[i*3+2]=b;
  F->dynR[i]=rad;
}

/* SHE STANDS ON THE SENSOR THE WAY YOU DO.  A door here is a motion
   sensor and not a button, and the sensor only ever had the player's feet
   on it -- but the drone is the other thing in this ship that walks the
   route, and she walks it AHEAD of you.  So she flew at leaves that were
   still shut and, being a ghost collider, went through them:
   what the player saw was a companion crossing a closed bulkhead, and an
   instruction ("follow Lumi") that the door in front of them contradicted.
   She is a second sensor now, and adLumiHeld below is the other half --
   the leaf holds her until it has actually opened. */
static float lumX,lumZ; static int lumOn;
int adWouldBlock(float x,float y,float z){
  for(int i=0;i<nADB;i++){
    const float *b=ADB+i*6;
    if(x>b[0]-0.30f&&x<b[3]+0.30f&&y>b[1]&&y<b[4]&&
       z>b[2]-0.30f&&z<b[5]+0.30f) return 1;
  }
  return 0;
}

void adUpdate(float dt,float px,float pz,FrameState *F){
  adInit();
  nADB=0;
  { float lp[3]; lumiPos(lp);
    lumX=lp[0]; lumZ=lp[2]; lumOn = lumiModeNow()!=LM_OFF; }
  if(!nAD) return;
  Door *lead=0; float leadD=1e9f;
  Door *sign=0; float signD=1e9f;
  for(int i=0;i<nAD;i++){
    Door *D=&AD[i];
    float ddx=D->x-px, ddz=D->z-pz, d2=ddx*ddx+ddz*ddz;

    /* ---- the servo -------------------------------------------------- */
    int want=0;
    float reach=AD_PERP+D->hw+4;
    float ldx=D->x-lumX, ldz=D->z-lumZ, l2=ldx*ldx+ldz*ldz;
    if(d2<reach*reach && adSensed(D,px,pz)){
      want=1; D->hold=AD_DWELL; D->held=0; }        /* you are here: let go */
    else if(lumOn && l2<reach*reach && adSensed(D,lumX,lumZ)){
      want=1; D->hold=AD_DWELL;
      /* she is on it and you are further off than she is: she holds it */
      if(d2>l2) D->held=AD_HELD;
    }
    else if(D->hold>0){ D->hold-=dt; want=1; }
    if(D->held>0){ D->held-=dt; want=1; }
    /* THE STORY OWNS EVERY DOOR IN THIS SHIP.  The player alone
       opens nothing -- Lumi has to be at the panel -- and a locked leaf
       stays shut however close you stand to it.  force is what Lumi (and
       sink stage 3, which takes door control away from her) sets. */
    if(D->lock) want=0;
    if(D->force) want=1;
    float prev=D->t;
    D->t += want? dt*AD_OPEN_S : -dt*AD_SHUT_S;
    D->t = clv(D->t,0,1);
    D->e = D->t*D->t*(3-2*D->t);
    D->moved = (D->t!=prev);
    /* the noise it makes, once per stroke -- on the EDGE, not while it runs,
       or a door two rooms away holds a saw note for the whole level */
    if(D->moved&&!D->was&&d2<44*44) auSlide(D->x,1.5f,D->z,want);
    if(!D->moved&&D->was&&D->t<=0&&d2<44*44) auLatch(D->x,1.2f,D->z);
    D->was=D->moved;
    if(D->moved && d2<leadD){ leadD=d2; lead=D; }
    if(d2<signD){ signD=d2; sign=D; }

    /* ---- what it is lit by, on its own phase ------------------------ */
    if(d2<AD_DRAW*AD_DRAW){
      D->litT-=dt;
      if(D->litT<=0){
        D->litT=0.31f;
        sampleLight(D->x,1.55f,D->z,D->lit,D->ldir);
        for(int k=0;k<3;k++) D->lit[k]*=0.72f;
      }
    }

    /* ---- and what it blocks ----------------------------------------- */
    if(D->e>0.985f || d2>AD_SOLID*AD_SOLID) continue;
    float lw=D->hw*(1-D->e);
    if(lw<0.05f) continue;
    if(nADB+2>128) continue;
    float q=D->th*0.30f;
    float *b0=ADB+nADB*6, *b1=ADB+(nADB+1)*6;
    if(D->ax){
      b0[0]=D->x-D->hw;    b0[1]=0; b0[2]=D->z-q;
      b0[3]=D->x-D->hw+lw; b0[4]=D->H; b0[5]=D->z+q;
      b1[0]=D->x+D->hw-lw; b1[1]=0; b1[2]=D->z-q;
      b1[3]=D->x+D->hw;    b1[4]=D->H; b1[5]=D->z+q;
    }else{
      b0[0]=D->x-q; b0[1]=0; b0[2]=D->z-D->hw;
      b0[3]=D->x+q; b0[4]=D->H; b0[5]=D->z-D->hw+lw;
      b1[0]=D->x-q; b1[1]=0; b1[2]=D->z+D->hw-lw;
      b1[3]=D->x+q; b1[4]=D->H; b1[5]=D->z+D->hw;
    }
    nADB+=2;
  }
  /* A LIT THING HAS TO BE A LAMP.  The strip on a moving leaf is baked into
     nothing, so the nearest door that is actually running borrows one of the
     eight unbaked slots for as long as it takes to run. */
  adDynT-=dt;
  if(lead && adDynT<=0 && leadD<20*20){
    adDynT=0.05f;
    float k=0.35f+0.65f*lead->e;
    dynLight(F,lead->x,1.62f,lead->z,
             AD_CYAN[0]*0.62f*k,AD_CYAN[1]*0.62f*k,AD_CYAN[2]*0.62f*k, 6.2f);
  }
  /* and the nearest door washes its own frame with its answer, so the
     colour is on the WALL and not only on a 26 cm strip -- one slot, the
     nearest leaf, because eight is all there are. */
  if(sign && signD<13.f*13.f){
    const float *c = sign->lock? AD_NO : AD_GO;
    dynLight(F,sign->x,2.30f,sign->z,c[0]*0.34f,c[1]*0.34f,c[2]*0.34f,4.4f);
  }
}

/* ---- what the story reaches in with ---------------------------------
   It knows a door by the point the deck plan draws it at, and this maps
   that to the leaf that is actually standing there -- so a door never has
   to be numbered twice, once in the plan and once in the story. */
int adNearest(float x,float z){
  adInit();
  int best=-1; float bd=1e9f;
  for(int i=0;i<nAD;i++){
    float dx=AD[i].x-x, dz=AD[i].z-z, d=dx*dx+dz*dz;
    if(d<bd){ bd=d; best=i; }
  }
  return (bd<9.f)?best:-1;
}
int  adCount(void){ adInit(); return nAD; }
void adDump(void){
  adInit();
  for(int i=0;i<nAD;i++)
    platLog("  leaf %2d  %7.2f %7.2f  ax %d  hw %.2f",i,AD[i].x,AD[i].z,AD[i].ax,AD[i].hw);
}
void adLock(int i,int on){ if(i>=0&&i<nAD){ AD[i].lock=on; if(on) AD[i].hold=0; } }
void adForce(int i,int on){ if(i>=0&&i<nAD){ AD[i].force=on; if(on) AD[i].lock=0; } }
int  adOpen(int i){ return (i>=0&&i<nAD)?(AD[i].e>0.92f):1; }
float adEase(int i){ return (i>=0&&i<nAD)?AD[i].e:1.f; }
void adPos(int i,float *o){ if(i>=0&&i<nAD){ o[0]=AD[i].x; o[1]=AD[i].z; } }
void adAllOpen(int on){ adInit(); for(int i=0;i<nAD;i++){ AD[i].force=on; if(on) AD[i].lock=0; } }

/* ---- a box in the door's own frame: a along the opening, p through it -- */
static void adBox(const Door *D,float a,float y,float p,
                  float sa,float sy,float sp,
                  const float *c,float em,const EntFx *fx,int prim){
  if(D->ax) pushBox(D->x+a,y,D->z+p, sa,sy,sp, c[0],c[1],c[2],em,fx,prim);
  else      pushBox(D->x+p,y,D->z+a, sp,sy,sa, c[0],c[1],c[2],em,fx,prim);
}

void adDraw(float px,float pz,float elapsed){
  for(int i=0;i<nAD;i++){
    Door *D=&AD[i];
    float ddx=D->x-px, ddz=D->z-pz, d2=ddx*ddx+ddz*ddz;
    if(d2>AD_DRAW*AD_DRAW) continue;
    int fine = d2<AD_FINE*AD_FINE;
    float H=D->H, th=D->th, hw=D->hw, e=D->e;

    /* one fx, rewritten between groups: sk 3 is MACHINED, sf the plate pitch
       in metres, si the metalness, sp 1 for a plain bar with no seams */
    EntFx fx=ENT_PLAIN;
    fx.zone=(float)D->zi; fx.rim=0.26f; fx.ao=0.56f; fx.rough=0.38f;
    fx.sk=3; fx.sf=0.30f; fx.si=MET_STEEL; fx.sp=0;
    for(int k=0;k<3;k++){ fx.lit[k]=D->lit[k]; fx.ldir[k]=D->ldir[k]; }
    #define M(ro,pi,me,pl) do{ fx.rough=(ro); fx.sf=(pi); fx.si=(me); \
                               fx.sp=(pl); }while(0)
    EntFx lampFx=ENT_PLAIN;
    lampFx.zone=(float)D->zi; lampFx.rim=0; lampFx.ao=1; lampFx.rough=0.30f;

    /* amber standing by, cyan running.  The two accent roles this needs --
       and the crossfade is snapped, because the halfway mix of the two is a
       washed-out white that is neither and reads as a blown lamp. */
    float k = e<=0.28f?0.f : (e>=0.72f?1.f:(e-0.28f)/0.44f);
    float lamp[3];
    for(int c=0;c<3;c++) lamp[c]=AD_AMBER[c]+(AD_CYAN[c]-AD_AMBER[c])*k;
    float run = D->moved? (0.70f+0.30f*sinf(elapsed*22)) : 1.0f;

    /* ---- the head.  The frame's own brush starts at 2.62 and runs to the
       ceiling, so the track hangs just under it and the sensor eye goes on
       its FACE, clear of the brush. */
    float ey=th*0.5f+0.022f;
    M(0.34f,0.24f,MET_STEEL,1);
    adBox(D,0,H-0.056f,0, hw*2-0.02f,0.112f,th*0.72f, AD_STEEL,0,&fx,PRIM_BEVEL);
    M(0.26f,0.24f,MET_TRIM,1);
    adBox(D,0,H-0.118f,0, hw*2-0.06f,0.026f,th*0.76f, AD_TRIM,0,&fx,PRIM_BEVEL);
    M(0.34f,0.16f,MET_STEEL,0);
    for(int si=0;si<2;si++){
      float s=si?1.f:-1.f;
      adBox(D,0,H+0.155f,s*ey, 0.40f,0.110f,0.044f, AD_STEEL,0,&fx,PRIM_BEVEL);
      /* green if it will let you through, red if it will not.  A locked
         leaf breathes rather than blinks: a blinking red at every one of
         thirty-four doors is a fire alarm, not a sign. */
      { const float *sc = D->lock? AD_NO : AD_GO;
        float se = D->lock ? (0.55f+0.45f*sinf(elapsed*2.1f)) : 1.0f;
        adBox(D,0,H+0.155f,s*(ey+0.014f), 0.30f,0.052f,0.028f,
              sc,1.15f*se,&lampFx,PRIM_BEVEL); }
      /* the running strip stays on the house accents, because what it is
         saying is "moving", not "open to you" */
      adBox(D,0,H+0.088f,s*(ey+0.010f), 0.20f,0.024f,0.024f,
            lamp,0.70f*run,&lampFx,PRIM_BEVEL);
    }

    if(e>0.985f) continue;
    float lw=hw*(1-e);
    if(lw<0.045f) continue;

    float LH=H-0.125f;            /* the leaf, hanging off that track */
    float bd=th*0.44f;            /* the leaf's own thickness */
    float pf=th*0.22f+0.013f;     /* and where a plate on its face sits */
    for(int si=0;si<2;si++){
      float s=si?1.f:-1.f;
      float c=s*(hw-lw*0.5f);     /* the body's centre: it stretches */
      float nz=s*(hw-lw);         /* the leading edge: it translates */

      /* the body.  Everything on it runs ACROSS the slide, so the stretch has
         nothing to show -- and the plating does not stretch either, because
         MACHINED measures itself in metres. */
      M(0.38f,0.30f,MET_STEEL,0);
      adBox(D,c,LH*0.5f,0, lw,LH,bd, AD_STEEL,0,&fx,PRIM_BEVEL);
      M(0.44f,0.15f,MET_KICK,0);
      adBox(D,c,0.105f,0, lw,0.210f,bd+0.020f, AD_KICK,0,&fx,PRIM_BEVEL);
      if(fine){
        for(int fi=0;fi<2;fi++){
          float f=fi?1.f:-1.f;
          M(0.28f,0.20f,MET_TRIM,1);
          adBox(D,c,0.910f,f*pf, lw-0.06f,0.048f,0.024f, AD_TRIM,0,&fx,PRIM_BEVEL);
          M(0.62f,0.20f,MET_GAP,1);
          for(int r=0;r<3;r++)
            adBox(D,c,0.560f+r*0.700f,f*pf, lw-0.10f,0.116f,0.022f,
                  AD_GAP,0,&fx,PRIM_BEVEL);
          if(lw>0.42f){
            M(0.66f,0.09f,MET_OCHRE,0);
            adBox(D,c,0.330f,f*pf, lw-0.16f,0.090f,0.022f, AD_OCHRE,0,&fx,PRIM_BEVEL);
          }
        }
      }
      /* the one vertical thing a stretching body is allowed: a stiffener
         measured from the NOSE, so it slides with it and never scales */
      M(0.26f,0.20f,MET_TRIM,1);
      if(fine) for(int r=0;r<2;r++){
        float q=0.42f+r*0.46f;
        if(q<lw-0.10f)
          adBox(D,nz+s*q,LH*0.5f+0.02f,0, 0.062f,LH-0.30f,bd+0.026f,
                AD_TRIM,0,&fx,PRIM_BEVEL);
      }
      /* the leading edge, at a fixed size, sliding.  The nose rib, the teeth
         that interlock with the other leaf, and the line down the joint. */
      M(0.30f,0.20f,MET_STEEL,1);
      adBox(D,nz+s*0.070f,LH*0.5f,0, 0.132f,LH,bd+0.055f, AD_STEEL,0,&fx,PRIM_BEVEL);
      if(lw>0.34f){
        float y0=si?0.905f:0.520f;
        int nteeth=si?2:3;
        for(int r=0;r<nteeth;r++)
          adBox(D,nz-s*0.085f,y0+r*0.770f,0, 0.180f,0.300f,bd+0.030f,
                AD_TRIM,0,&fx,PRIM_BEVEL);
      }
      for(int fi=0;fi<2;fi++){
        float f=fi?1.f:-1.f;
        adBox(D,nz+s*0.128f,LH*0.50f+0.06f,f*(pf+0.014f), 0.030f,LH*0.72f,0.022f,
              lamp,0.48f*run,&lampFx,PRIM_BEVEL);
      }
      /* THE ACTUATOR, AND THE ONE THING THAT IS ALLOWED TO STRETCH.  A ram
         bolted to the jamb and a rod running to the nose: the rod is the only
         part of the leaf whose length is supposed to change, so the same
         scaling that had to be hidden everywhere else is, here, the point. */
      if(fine) for(int fi=0;fi<2;fi++){
        float f=fi?1.f:-1.f;
        M(0.24f,0.12f,MET_TRIM,1);
        adBox(D,s*(hw-0.17f),LH-0.090f,f*(pf+0.030f), 0.320f,0.104f,0.086f,
              AD_TRIM,0,&fx,PRIM_BEVEL);
        M(0.18f,0.12f,MET_TRIM,1);
        adBox(D,c,LH-0.090f,f*(pf+0.026f), lw,0.046f,0.044f, AD_TRIM,0,&fx,PRIM_BEVEL);
      }
    }
    #undef M
  }
}
