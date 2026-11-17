/* ============================================================
   LUMI.

   The drone is the bench's, not this file's -- and as of
   props/lumi_v02/ the bench hands over a GENERATOR rather than a
   page to read numbers off.  lumi_geometry.js is the frozen
   design and the shape itself: 35 parts, 9,627 vertices, 14,772
   triangles, every transform already baked into the points.  She
   is not rebuilt here out of spheres and drums; a finished mesh
   already exists.

   So the shape comes from lumimesh.c, which is that generator
   ported statement for statement, and this file writes only what
   the port is allowed to: the renderer binding, the materials,
   and the animation constants.

   What the v6 design settles:

     -- she is BONE, not gunmetal.  #dedace under a clearcoat.  A
        dark drone in a dark ship is a shape you lose; a pale one
        is the brightest thing on the screen after her own lamp,
        which is what a companion has to be.
     -- the body is SEVEN ZONES on one ovoid, and the seams are
        vertex duplicates that break the normal.  Drawn as rings
        laid over a lathe they sat at the lathe's own radius and
        were invisible; built into the profile they are the
        hard-surface edges the whole silhouette rests on.
     -- the visor is a superellipse projected onto a sphere, not
        a plate.  Tilted 15 degrees UP -- she flies at 1.40 m and
        the eye is at 1.66, so a face angled up is a face turned
        towards you, which is most of why she reads as attentive.
        The display now sits on that sphere too.
     -- the pods are a bevelled section swept round a rounded
        rectangle, so the hole through each one is real, and the
        silhouette comes from the geometry for nothing.
     -- she does not fly on nothing.  A circular emitter under the
        hull throws a short horizontal field, and that -- not a
        rocket plume -- is what says hovering.

   She still has no face and no voice.  The design forbids both.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include "prim.h"
#include "ent.h"
#include "lumi.h"
#include "lumimesh.h"
#include "face.h"
#include "zom.h"
#include <string.h>
#include <stdlib.h>

/* The generator works at a body radius of 0.222 and hers is LUMI_R, so V()
   carries any number of the bench's -- a vertex, a display element, the
   emitter plane -- straight into this ship's scale.  It is also the whole
   of the mesh's transform: one uniform scale, applied to every group. */
#define LUMI_R      0.200f          /* a 0.40 m drone */
#define V(x)        ((x)*(LUMI_R/LM_R))
#define LUMI_FLOAT  1.40f
/* HER TRAVEL HAS TO BE WATCHABLE, AND SHE STILL MUST NOT BE LEFT BEHIND.
   One number could not be both: eight metres a second keeps station on a
   sprinting player and crosses a room in four frames, which is a cut and
   not a companion going somewhere.  So the base is a fast walk and it
   only opens up when she is actually falling behind -- 4.2 m/s inside two
   metres, rising to about 10 at six, which is a drone hurrying rather
   than a drone teleporting.  The one real teleport left is the one the
   player asked for: restore() puts her back beside them after a death. */
#define LUMI_SPEED  4.2f
#define LUMI_CATCHUP 6.0f           /* ... plus this much, by six metres out */
/* SHE FLIES TO A PANEL; SHE DOES NOT ARRIVE AT ONE.  A five-metre crossing
   to a door at travel speed is six frames.  A held position is approached
   at something you can watch her cover. */
#define LUMI_PANEL_SPEED 2.4f
#define LEAD_AHEAD  3.5f
/* SHE FLIES THE ROUTE, AND THESE ARE THE THREE NUMBERS THAT SAY WHEN.
   Inside FLY_NEAR the straight line IS the route -- she is in the same
   room as the thing and a lattice cell is coarser than the approach.
   Past it she looks FLY_LOOK ahead, and the first solid answer puts her
   on the field for FLY_STICK seconds: without that hold she noses into
   the corner, routes, clears the probe, goes straight, and noses into the
   same corner again, sixty times a second.  FLY_STICK is long enough to
   get round a doorframe and short enough that she is never following a
   corridor she has already left. */
#define FLY_NEAR    1.60f
#define FLY_LOOK    1.30f
#define FLY_STICK   0.50f
/* AND SHE FLIES DOWN THE MIDDLE OF IT.

   Knowing WHICH WAY to go and knowing WHERE IN THE CORRIDOR to be are two
   different problems, and the lattice only answers the first.  The
   shortest way through a doorway is along the jamb; the shortest way round
   a corner is across the corner.  A drone that takes either one scrapes
   the ship even when its route is correct.

   So after the heading is chosen, she probes left and right of it and
   steers toward whichever side has more room.  In an open room both probes
   run out at FLY_WIDE and cancel, so this costs her nothing; in a 2.4 m
   corridor they find the two walls and she rides the centreline; in a
   doorway they find the jambs, which is the one place in this ship where
   the middle is not a preference.  Thirteen solidAt calls a frame, and
   only while she is actually travelling. */
#define PIN_GIVE    1.20f       /* getting nowhere this long: go through it */
#define PIN_GHOST   1.50f       /* ... and stay through for this long */
#define FLY_WIDE    1.70f       /* a wall further out than this is not near */
#define FLY_PROBE   0.24f       /* ... found in steps of */
#define FLY_PUSH    1.30f       /* how hard the imbalance steers, per metre */
/* the emitter plane is LM_EMIT_Y, computed by the generator from A5 */

/* the bench's palette, straight off the material definitions in v6 */
static const float C_PAINT[3]={0.735f,0.720f,0.678f};   /* #dedace, headroom */
static const float C_BAND [3]={0.141f,0.157f,0.173f};   /* #24282c */
static const float C_STEEL[3]={0.576f,0.604f,0.627f};   /* #939aa0 */
static const float C_DARK [3]={0.051f,0.063f,0.075f};   /* #0d1013 */
static const float C_PANEL[3]={0.082f,0.098f,0.114f};   /* #15191d */
static const float C_CYAN [3]={0.384f,0.847f,1.000f};   /* #62d8ff */
static const float C_DIM  [3]={0.184f,0.498f,0.620f};   /* #2f7f9e */

/* how far to the first solid thing along a heading, out to FLY_WIDE */
static float clearTo(float x,float y,float z,float dx,float dz){
  for(float t=FLY_PROBE;t<=FLY_WIDE;t+=FLY_PROBE)
    if(solidAt(x+dx*t,y,z+dz*t)) return t;
  return FLY_WIDE;
}
/* AND A LOOK AHEAD IS A SEGMENT, NOT A POINT.  Probing one point 1.30 m
   down the heading steps straight OVER a thin bulkhead and reports clear
   air on the far side of it -- measured: she sat flush against something
   four centimetres in front of her for twenty-two seconds with the route
   never once asked for, because the only sample taken was past the thing
   stopping her.  Walk the segment, and start it at the skin. */
static int lineBlocked(float x,float y,float z,float dx,float dz,float len){
  for(float t=0.05f;t<len;t+=FLY_PROBE)
    if(solidAt(x+dx*t,y,z+dz*t)) return 1;
  return solidAt(x+dx*len,y,z+dz*len);
}

static struct {
  float x,y,z;                      /* where she is */
  float tx,ty,tz;                   /* where she wants to be */
  float px,py,pz;                   /* and where she was, for the bank */
  float yaw, pitch, roll;
  int   mode, disp, dispPrev;
  float blend;                      /* LEAD <-> HUD, one second */
  float bob, t, blink, nextBlink;
  float hp, bpm, openU, talk;
  int   count;
  float panelX,panelY,panelZ; int panelOn;
  float dockX,dockY,dockZ; int docked;
  float leadX,leadY,leadZ; int leadOn;
  float doorWait;                   /* how long a shut leaf has held her */
  float routeT;                     /* how long she stays on the lattice */
  float insideT;                    /* ... and how long she has been in a wall */
  float pinT;                       /* ... and how long she has got nowhere */
  float ghostT;                     /* she is passing through; leave her alone */
} L;
int adWouldBlock(float x,float y,float z);

/* ---- WHAT "SHE IS STUCK" MEANS AS A NUMBER -------------------------
   Three separate failures look like one -- she catches on a corner, the
   door event does not fire, she is left hanging in a wall -- and none of
   them can be told apart by watching, because all three look like a
   drone that has stopped.  So each one gets a counter, and storyStep
   prints them every two seconds.

     pin     the slide dropped BOTH axes: she is against something and
             going nowhere.  This is the corner.
     in      solidAt() says she is INSIDE the geometry.  This is the wall.
     stall   she is more than STALL_D from her target and the distance is
             not coming down.  This is the event that never fires, because
             every one of them waits on lumiReadyAt().

   A counter that is zero for a whole run is a bug that is fixed; a
   counter that is not is the one still to answer. */
#define STALL_D 0.60f
static struct {
  int   frames, pin, inside, stall, want, got, far;
  float stallT, worstStall, worstPin, pinT;
  float lastD;
} LT;

void lumiInit(void){
  memset(&L,0,sizeof L);
  memset(&LT,0,sizeof LT); LT.lastD=1e9f;
  L.mode=LM_LEAD; L.disp=LD_EYES_IDLE;
  L.hp=1; L.bpm=84; L.nextBlink=3;
  L.y=LUMI_FLOAT;
}
/* -lumi: the drone on a card, held in front of the camera so the model can
   be compared against props/lumi_v02/lumi_v6.html by looking at the two.
   While it is up the director's hold on her is released -- it is an
   instrument, and an instrument the game can move is no use.

   SHE FOLLOWS THE CAMERA, she is not parked at a coordinate.  Frozen at
   the position she was placed at during init, the framing depended on
   where the player's feet had settled by the time the shutter went, so
   -cam had to be found by sweeping pitch and the same flag photographed
   a different angle in a different room.  On the card she is 1.30 m down
   the view vector at EYE HEIGHT, facing you -- which is the bench's own
   pose, and is the same shot every time. */
static int   cardOn=0;
static float cardDist=1.30f;
void lumiCard(float dist,int disp){
  cardDist = dist>0.2f ? dist : 1.30f;
  /* the bench's own readings, so a state that draws a value has one to
     draw and the same flag photographs the same face every time */
  L.disp=disp; L.hp=0.72f; L.bpm=84.f; L.count=42; L.openU=0.62f; L.talk=0.5f;
  L.pitch=L.roll=0;
  cardOn=1;
}
void lumiMode(int m){ if(cardOn) return; L.mode=m; if(m!=LM_PANEL) L.panelOn=0; }
int  lumiModeNow(void){ return L.mode; }
void lumiDisplay(int d){ if(!cardOn) L.disp=d; }
int  lumiDisplayNow(void){ return L.disp; }
void lumiSetCount(int n){ L.count=n; }
/* AND THE CARD HOLDS HER VITALS TOO.  lumiDisplay() and lumiMode() already
   refuse the director while -lumi is up, but the health fraction did not,
   so the director wrote the running player's HP over the card's own 0.72
   every frame -- and the status ring and the health bar, which are read off
   it, photographed a different colour on every run.  A bench that will not
   hold still is not a bench. */
void lumiSetVitals(float f,float b){ if(!cardOn){ L.hp=f; L.bpm=b; } }
void lumiSetOpen(float u){ L.openU=u; }
void lumiSetTalk(float u){ L.talk=u; }
void lumiPos(float *o){ o[0]=L.x; o[1]=L.y; o[2]=L.z; }
/* every two seconds, out of storyStep's own trace: what the last two
   seconds of her flight actually did.  The three percentages are of
   frames, the two worst-cases are in seconds and are since boot. */
void lumiTrace(void){
  int f=LT.frames?LT.frames:1;
  float dx=L.tx-L.x, dy=L.ty-L.y, dz=L.tz-L.z;
  platLog("lumiv  d %.2f  pin %2d%%  in %2d%%  stall %2d%%  far %2d%% "
          "want %2d%% got %2d%%   worst pin %.1fs stall %.1fs",
          hypot3(dx,dy,dz),LT.pin*100/f,LT.inside*100/f,LT.stall*100/f,
          LT.far*100/f,LT.want*100/f,LT.got*100/f,
          LT.worstPin,LT.worstStall);
  LT.frames=LT.pin=LT.inside=LT.stall=LT.want=LT.got=LT.far=0;
}
void lumiAt(float x,float y,float z){
  L.x=L.tx=L.px=x; L.y=L.ty=L.py=y; L.z=L.tz=L.pz=z;
}
void lumiDock(float x,float y,float z){
  L.dockX=x; L.dockY=y; L.dockZ=z; L.mode=LM_DOCKED; L.docked=1; L.panelOn=0;
}
void lumiPanelAt(float x,float y,float z){
  L.panelX=x; L.panelY=y; L.panelZ=z; L.panelOn=1; L.mode=LM_PANEL;
}
/* WHAT MAKES "FOLLOW LUMI" A THING YOU CAN DO.  LEAD used to be a point
   three and a half metres down the player's own view vector, which is not
   leading -- it is a balloon on a stick, and it turns with your head.  The
   director now hands her a point ON THE ROUTE and she flies to that, so
   there is something ahead of you that is actually going somewhere. */
void lumiLeadTo(float x,float y,float z,int valid){
  L.leadX=x; L.leadY=y; L.leadZ=z; L.leadOn=valid;
}
int lumiReadyAt(float x,float z,float r){
  float dx=L.x-x, dz=L.z-z;
  return dx*dx+dz*dz < r*r;
}

void lumiStep(float dt,const float *eye,float yaw,float pitch,int combat){
  L.t+=dt;
  L.bob=0.026f*sinf(L.t*2.62f)+0.007f*sinf(L.t*3.70f);
  L.blink-=dt;
  if(L.blink<-0.12f){ L.nextBlink=3.f+(float)(rand()%300)*0.01f; L.blink=L.nextBlink; }

  if(cardOn){                               /* -lumi: the bench pose */
    float cx=sinf(yaw), cz=cosf(yaw);
    L.x=eye[0]+cx*cardDist; L.y=eye[1]; L.z=eye[2]+cz*cardDist;
    L.yaw=yaw+3.1416f; L.pitch=L.roll=0;
    return;
  }
  if(L.mode==LM_OFF) return;

  float fx=sinf(yaw), fz=cosf(yaw);
  float rx=-cosf(yaw), rz=sinf(yaw);       /* camBasis()'s right vector */
  if(L.mode==LM_DOCKED){
    L.tx=L.dockX; L.ty=L.dockY; L.tz=L.dockZ;
  } else if(L.mode==LM_PANEL&&L.panelOn){
    L.tx=L.panelX; L.ty=L.panelY; L.tz=L.panelZ;
  } else {
    /* HUD IS THE MIRROR OF THE GUN.  The weapons rest at view-space
       (+0.17,-0.18,-0.36), which puts them 42% of the way to the right
       edge and 78% of the way down; she takes the same two fractions on
       the other side.  Her face is the only health readout in the game,
       so it has to land where the eye already goes for the ammo count. */
    float hud = (L.mode==LM_HUD||combat) ? 1.f : 0.f;
    float k=dt/1.0f;                            /* the one-second blend */
    L.blend += (hud-L.blend)*(k>1?1:k*4.f);
    /* she keeps station in the CAMERA's frame, pitch included but damped:
       glued rigidly she reads as a decal, ignoring pitch entirely she
       slides off the bottom of the screen the moment you look up */
    float cp=pitch; if(cp>0.6f)cp=0.6f; if(cp<-0.6f)cp=-0.6f;
    float sp=sinf(cp), cc=cosf(cp);
    float Fx=fx*cc, Fy=sp, Fz=fz*cc;
    float Ux=-fx*sp, Uy=cc, Uz=-fz*sp;
    /* 0.95 m puts a 0.40 m drone across a fifth of the screen -- the same
       weight the gun has on the other side -- and the two offsets are that
       distance times the gun's own screen fractions, so moving her nearer
       moves her along the sightline and not across the frame.
       AND SHE HAS TO SURVIVE A CORRIDOR.  Her station is nearly a metre
       out, which in a 2.4 m passage is inside the wall the moment you face
       one, and a health bar that vanishes when you look at a bulkhead is
       not a health bar.  So the station telescopes: same screen position,
       shorter arm, bigger drone. */
    const float RX=0.463f, RY=0.352f;      /* left and down, per metre out */
    float FWD=0.95f;
    float hx,hy,hz;
    for(int a=0;a<3;a++){
      FWD = a==0?0.95f : (a==1?0.72f:0.52f);
      hx=eye[0]+Fx*FWD-rx*(FWD*RX)+Ux*(-FWD*RY);
      hy=eye[1]+Fy*FWD            +Uy*(-FWD*RY);
      hz=eye[2]+Fz*FWD-rz*(FWD*RX)+Uz*(-FWD*RY);
      if(!solidAt(hx,hy,hz)) break;
    }
    /* and LEAD is a point on the route, or -- when the lattice cannot
       answer -- the old balloon, which is still better than nothing */
    float lx,ly,lz;
    if(L.leadOn){ lx=L.leadX; ly=L.leadY; lz=L.leadZ; }
    else { lx=eye[0]+fx*LEAD_AHEAD; ly=LUMI_FLOAT; lz=eye[2]+fz*LEAD_AHEAD; }
    float b=L.blend;
    L.tx=lx+(hx-lx)*b; L.ty=ly+(hy-ly)*b; L.tz=lz+(hz-lz)*b;
  }
  /* a ghost collider: she is not allowed to be the thing that
     gets stuck on a doorframe while the player is being chased */
  float dx=L.tx-L.x, dy=L.ty-L.y, dz=L.tz-L.z;
  float d=hypot3(dx,dy,dz);
  float spd;
  if(L.mode==LM_PANEL||L.mode==LM_DOCKED) spd=LUMI_PANEL_SPEED;
  else {
    float over=(d-2.0f)*0.25f;                 /* 0 at two metres, 1 at six */
    if(over<0) over=0;
    if(over>1) over=1;
    spd=LUMI_SPEED+LUMI_CATCHUP*over;
    /* A THIRD GEAR PAST TEN METRES WAS TRIED AND REVERTED.  The four
       recalls a 300 s auto run needs are not a speed problem: raising the
       ceiling to 22 m/s left the count at four and the worst lag at 30 m,
       because she is not being outrun -- she is caught on something, and a
       drone that is stuck is stuck at any speed.  What answers it is the
       recall in story.c and, one day, a flight that slides better. */
  }
  float step=spd*dt;
  int pinned=0;
  L.px=L.x; L.py=L.y; L.pz=L.z;

  /* AND IF SHE IS ALREADY IN A WALL, SHE GETS OUT OF IT FIRST.  Rare --
     half a per cent of frames over a 300 s run -- but it is the one state
     nothing else here can leave, because every escape below is guarded on
     `!solidAt(L.x,L.y,L.z)` so that a drone which is somehow inside the
     geometry can still fly out of it.  What that guard never did was give
     her a direction, so she carried on toward a target she could not
     reach through the bulkhead she was in.  Eight headings and a lift, at
     the cost of nine solidAt calls on a frame that is already broken. */
  if(L.ghostT>0) L.ghostT-=dt;
  if(solidAt(L.x,L.y,L.z)&&L.ghostT<=0){
    L.insideT+=dt;
    if(L.insideT>0.35f){
      static const float ES[8][2]={{1,0},{-1,0},{0,1},{0,-1},
                                   {0.707f,0.707f},{-0.707f,0.707f},
                                   {0.707f,-0.707f},{-0.707f,-0.707f}};
      float out=0.55f;
      for(int i=0;i<8;i++){
        float ex=L.x+ES[i][0]*out, ez=L.z+ES[i][1]*out;
        if(!solidAt(ex,L.y,ez)){ L.x=ex; L.z=ez; break; }
      }
      if(solidAt(L.x,L.y,L.z)&&!solidAt(L.x,L.y+out,L.z)) L.y+=out;
      L.insideT=0;
    }
  } else L.insideT=0;

  if(d>1e-4f){
    float nx,ny,nz;
    /* SHE FLIES THE ROUTE, NOT THE LINE.

       Her whole flight was: aim at the target, and when the next position
       is solid drop whichever axis is blocked and keep the other.  That
       slide gets round a flat wall.  It cannot get round a CORNER, where
       both axes are blocked at once and there is nothing left to drop --
       and a ship made of corridors is made of corners.  Measured over a
       300 s -auto run she was pinned like that for 64% of her frames, 51.6
       seconds of it in one stretch, and in the whole run she opened no
       door: every door in the game waits on lumiReadyAt(), so a drone that
       cannot round a corner is a story that stops at one.

       zom.c's lattice already answers "which way from here to there" for
       every animal in the ship.  So when the line ahead is solid she asks
       it, on a field of her own solved to her own target, and flies the
       heading it gives back.  The line is still the common case and still
       costs one solidAt; the lattice is what happens at the corner. */
    float mx=dx/d, mz=dz/d;
    int routed=0;
    if(L.routeT>0) L.routeT-=dt;
    if(d>FLY_NEAR){
      zomFlyGoal(L.tx,L.ty,L.tz,dt);
      LT.far++;
      float look=d<FLY_LOOK?d:FLY_LOOK;
      if(L.routeT>0||lineBlocked(L.x,L.y,L.z,mx,mz,look)){
        float rd[2];
        LT.want++;
        if(zomFlyStep(L.x,L.y,L.z,rd)){
          mx=rd[0]; mz=rd[1]; routed=1; L.routeT=FLY_STICK; LT.got++;
        }
      }
      /* ... and now WHERE in the passage, which the lattice never said */
      { float qx=-mz, qz=mx;
        float lc=clearTo(L.x,L.y,L.z, qx, qz);
        float rc=clearTo(L.x,L.y,L.z,-qx,-qz);
        if(lc<FLY_WIDE||rc<FLY_WIDE){
          float k=(lc-rc)*0.5f*FLY_PUSH;
          if(k> 1.f) k= 1.f;
          if(k<-1.f) k=-1.f;
          mx+=qx*k; mz+=qz*k;
          float m=hypot2(mx,mz);
          if(m>1e-4f){ mx/=m; mz/=m; }
        } }
    } else L.routeT=0;
    if(routed){
      /* on the lattice she holds CRUISE height rather than the target's:
         the deck she is crossing is not the deck the target stands on,
         and a drone that dives at a doorway to a height it will need on
         the far side of it clips the lintel on the way through */
      float fy, wy=L.ty;
      if(deckNear(L.x+mx*step,L.z+mz*step,L.y-LUMI_FLOAT,&fy)) wy=fy+LUMI_FLOAT;
      float e=wy-L.y;
      nx=L.x+mx*step; nz=L.z+mz*step;
      ny=L.y+(e>step?step:(e<-step?-step:e));
    }
    else if(d<step){ nx=L.tx; ny=L.ty; nz=L.tz; }
    else{ nx=L.x+mx*step; ny=L.y+dy/d*step; nz=L.z+mz*step; }
    /* SHE IS STOPPED BY EXACTLY ONE THING, AND ONLY UNTIL IT OPENS.
       The ghost collider is what keeps her from being the thing that gets
       stuck on a doorframe, and that is worth keeping -- but a ghost that
       crosses a shut bulkhead in front of a player who is waiting at it
       is not a companion, it is a rendering mistake, and it is the one
       thing in her flight the player is guaranteed to be looking at.  So
       a CLOSED LEAF holds her, she stands on its own sensor while it runs
       (adUpdate), and she goes through the moment it is open.  Six
       seconds at the outside: a leaf that never opens -- a lock nothing
       in the ship can clear -- must not be able to strand the only light
       in the game on the far side of the level. */
    if(adWouldBlock(nx,ny,nz)&&!adWouldBlock(L.x,L.y,L.z)){
      L.doorWait+=dt;
      if(L.doorWait<6.f){ nx=L.x; ny=L.y; nz=L.z; }
    } else L.doorWait=0;
    /* AND SHE SLIDES OFF A BULKHEAD INSTEAD OF GRINDING ALONG IT.  The
       ghost collider is what keeps her out of a doorframe, and it is why
       nothing ever stopped her flying INTO one: the lead point is a node
       on the route and the line to it cuts every corner between here and
       there, so she spent the trip scraping the inside of the turn.  Drop
       whichever axis is blocked and keep the other two, which is what
       game.c's moveHorizontal does for the player and for the same
       reason.  She is still a ghost; she is just not a ghost that looks
       like it is stuck. */
    if(solidAt(nx,ny,nz)&&!solidAt(L.x,L.y,L.z)){
      float tx=nx, ty=ny, tz=nz;
      nx=L.x; ny=L.y; nz=L.z;
      if(!solidAt(tx,ny,nz)) nx=tx;
      if(!solidAt(nx,ny,tz)) nz=tz;
      if(!solidAt(nx,ty,nz)) ny=ty;
      /* nothing opened: lift, because a ceiling rib is the usual answer
         and 20 cm of climb clears most of them.

         AND THE LIFT HAS A CEILING OF ITS OWN, which it did not.  `L.y +
         0.25*dt*4` is L.y + dt: one metre a second, every frame she is
         pinned, with nothing anywhere that brings her down again -- the
         descent only happens when the target is below her AND the way to
         it is clear, and a drone pinned against a bulkhead has neither.
         Measured over a 300 s run she climbed to 3.21 m in the cargo hold
         and stayed there while the player walked 68 metres away.  That is
         also the first surface not being the deck, in a different hat:
         above 2.62 m she is over the door heads and the ship has no walls.
         Nine hundred millimetres over the point she is trying to reach is
         enough to clear what she gets caught on and keeps her under every
         door head in the ship. */
      if(nx==L.x&&nz==L.z){
        pinned=1; L.pinT+=dt;
        /* DOWN BEFORE UP.  The only thing this branch used to try was a
           lift, and a lift is the wrong instinct for a drone pinned under
           a ceiling rib: measured, it carried her from her 1.40 m cruise
           to 2.06 m in the crew quarters and left her there for the whole
           run, because coming down needs the target below her AND a clear
           path and a pinned drone has neither.  Cruise height is where
           the doorways are, so she goes back to it first and only climbs
           if that is blocked too. */
        float fy, cruise=L.ty;
        if(deckNear(L.x,L.z,L.y-LUMI_FLOAT,&fy)) cruise=fy+LUMI_FLOAT;
        if(L.y>cruise+0.10f&&!solidAt(L.x,L.y-0.30f,L.z))      ny=L.y-dt;
        else if(L.y<cruise+0.90f&&!solidAt(L.x,L.y+0.30f,L.z)) ny=L.y+dt;
        /* AND A GHOST IS NOT ALLOWED TO BE STUCK.  She is a ghost
           collider precisely so she is never the thing that gets
           caught on a doorframe, and every escape above is a courtesy:
           they keep her out of the geometry when they can.  When they
           cannot, the courtesy has to end -- she is the light, the health
           readout and the only thing that opens a door, and a run must
           never be lost to her flight.  PIN_GIVE seconds of getting
           nowhere and she goes through, and stays a ghost long enough to
           come out the far side. */
        if(L.pinT>PIN_GIVE){ nx=tx; ny=ty; nz=tz; L.ghostT=PIN_GHOST; }
      } else L.pinT=0;
    } else L.pinT=0;
    L.x=nx; L.y=ny; L.z=nz;
  }
  /* ---- and what that flight cost, as three counters -------------- */
  LT.frames++;
  if(pinned){ LT.pin++; LT.pinT+=dt;
              if(LT.pinT>LT.worstPin) LT.worstPin=LT.pinT; }
  else LT.pinT=0;
  if(solidAt(L.x,L.y,L.z)) LT.inside++;
  if(d>STALL_D&&d>LT.lastD-0.004f){
    LT.stall++; LT.stallT+=dt;
    if(LT.stallT>LT.worstStall) LT.worstStall=LT.stallT;
  } else LT.stallT=0;
  LT.lastD=d;
  /* she faces the way she is going, except in HUD where she faces you */
  float wantYaw;
  if(L.mode==LM_HUD||L.blend>0.5f) wantYaw=yaw+3.1416f;
  else if(d>0.2f)                  wantYaw=atan2f(dx,dz);
  else                             wantYaw=yaw+3.1416f;
  float dy2=wantYaw-L.yaw;
  while(dy2>3.14159f) dy2-=6.2832f;
  while(dy2<-3.14159f) dy2+=6.2832f;
  float lim=7.f*dt;
  L.yaw += dy2>lim?lim:(dy2<-lim?-lim:dy2);

  /* THE BANK IS WHAT SELLS THE HOVER.  v6 rolls her into her own sideways
     velocity and noses her at what she is chasing; without it a flying
     thing reads as a prop on a wire. */
  if(dt>1e-5f){
    float vx=(L.x-L.px)/dt, vy=(L.y-L.py)/dt, vz=(L.z-L.pz)/dt;
    float rv=vx*cosf(L.yaw)-vz*sinf(L.yaw);
    float wantRoll=-rv*0.055f;
    if(wantRoll> 0.30f) wantRoll= 0.30f;
    if(wantRoll<-0.30f) wantRoll=-0.30f;
    float wantPitch=vy*0.045f;
    if(wantPitch> 0.22f) wantPitch= 0.22f;
    if(wantPitch<-0.22f) wantPitch=-0.22f;
    float k=dt*4.5f; if(k>1)k=1;
    L.roll +=(wantRoll -L.roll )*k;
    L.pitch+=(wantPitch-L.pitch)*k;
  }
}

/* the drone's own spotlight -- one of the eight unbaked slots.  The ship's
   own lighting is 12% of nothing, so this is very nearly the only light
   the player has, which is the point. */
void lumiLight(void *fs){
  FrameState *F=(FrameState*)fs;
  if(L.mode==LM_OFF||F->dynN>=MAXD) return;
  int i=F->dynN++;
  float fx=sinf(L.yaw), fz=cosf(L.yaw);
  F->dynP[i*3]=L.x-fx*0.25f; F->dynP[i*3+1]=L.y+L.bob; F->dynP[i*3+2]=L.z-fz*0.25f;
  F->dynC[i*3]=1.00f; F->dynC[i*3+1]=0.94f; F->dynC[i*3+2]=0.84f;   /* 5600 K */
  F->dynR[i]=9.5f;
  /* and the lift field, which is cyan and is UNDER her, so a dark floor
     shows her standing on something */
  if(F->dynN<MAXD){
    int j=F->dynN++;
    F->dynP[j*3]=L.x; F->dynP[j*3+1]=L.y+L.bob+V(LM_EMIT_Y)-0.06f; F->dynP[j*3+2]=L.z;
    F->dynC[j*3]=0.16f; F->dynC[j*3+1]=0.36f; F->dynC[j*3+2]=0.44f;
    F->dynR[j]=2.1f;
  }
}

/* ---- the display is no longer solids.  It is a SCREEN: face.c draws
   visor_screen with its own program and evaluates the bench's 512x290
   canvas per pixel.  Eleven emissive boxes for a heartbeat is what a
   renderer with no texture unit does before it is given one; what stays
   here is deciding WHAT she is showing, which was always this file's. */
/* ---- her seven materials --------------------------------------------
   The design gives each one a colour, a roughness and a metalness, and the
   entity shader's MACHINED kind takes exactly those three: si IS the
   metalness, sf is the plating pitch in metres, and sp turns the plate
   grid off.

   NOTHING ON HER IS PLATED.  A plate grid is what a bare PRIM_POD needs,
   because it is an egg and an egg needs panel lines to stop reading as
   one.  The generator gives
   her seven zones, two inset bands, a flange, a housing, two swept pod
   hoops and a visor -- the silhouette does that job now, and the bench's
   own paint is a clearcoat with a roughness map and no panels on it at all.
   MACHINED still earns its place for the roughness line noise and for the
   conductor term that makes 0.96 metalness read as steel; only the grid is
   off. */
typedef struct { int grp; const float *col; float rough, metal; } Skin;
static const Skin SKIN[]={
  { LMP_PAINT , C_PAINT, 0.26f, 0.04f },   /* the bone clearcoat */
  { LMP_BAND  , C_BAND , 0.72f, 0.28f },
  { LMP_STEEL , C_STEEL, 0.26f, 0.96f },
  { LMP_DARK  , C_DARK , 0.58f, 0.40f },
  { LMP_PANEL , C_PANEL, 0.30f, 0.62f },
  /* THE DECALS ARE PRINTED ON THE PAINT, NOT BOLTED TO IT.  On the bench
     they are alpha textures over the shell and what is not ink is the
     shell; as black plates they were four dark blotches on a white drone.
     The patch takes the paint a shade down -- a printed marking, all but
     invisible -- and the ink is LMP_MARK. */
  { LMP_DECAL , C_PAINT, 0.44f, 0.04f },
};

void lumiDraw(void){
  if(L.mode==LM_OFF){ faceSet(0,0,-1,0,0,0,0,0,0); return; }
  int zi=zoneAt(L.x,L.z);
  EntFx fx=ENT_PLAIN, gx=ENT_PLAIN;
  fx.zone=gx.zone=(float)zi;
  fx.rim=0.55f; fx.ao=0.90f;
  gx.rim=0.20f; gx.ao=1.00f; gx.rough=0.25f;
  sampleLight(L.x,L.y,L.z,fx.lit,fx.ldir);
  for(int k=0;k<3;k++){
    if(fx.lit[k]<0.06f) fx.lit[k]=0.06f;          /* she lights herself */
    gx.lit[k]=fx.lit[k]; gx.ldir[k]=fx.ldir[k];
  }

  /* v6 sets body.rotation to (pitch,yaw,roll) in YXZ, so the frame is
     translate * Ry * Rx * Rz and the whole mesh hangs off it -- one
     uniform scale, because the generator baked everything else. */
  float A[12],B[12],C[12],D[12],M[12];
  mTrans34(L.x,L.y+L.bob,L.z,A);
  mRotY34(L.yaw,B);   mMul34(A,B,C);
  mRotX34(L.pitch,B); mMul34(C,B,D);
  mRotZ34(L.roll,B);  mMul34(D,B,M);
  const float S=V(1.f);

  /* ---- 1. the hull, the visor, the pods, the antenna, the back: six
     material groups, six instances, and every vertex in them is one of
     lumi_geometry.js's own. --------------------------------------- */
  for(int i=0;i<(int)(sizeof SKIN/sizeof SKIN[0]);i++){
    const Skin *k=&SKIN[i];
    fx.rough=k->rough;
    fx.sk=3; fx.sf=0.02f; fx.si=k->metal; fx.sp=1.f;   /* no plate grid */
    pushEnt(M,S,S,S,k->col[0],k->col[1],k->col[2],0,&fx,LUMI_PRIM[k->grp]);
  }
  /* LMP_SCREEN IS NOT PUSHED HERE.  face.c draws that same patch with the
     display on it -- one surface, so there is nothing to z-fight with, and
     it is unlit, which is what MeshBasicMaterial means on the bench. */
  { EntFx sc=fx;
    /* her name, cut out of the ship's own 5x7 stencil:
       dark ink on the bone, the way the bench prints it */
    sc.rough=0.52f; sc.rim=0.25f; sc.sk=3; sc.si=0.10f; sc.sf=0.02f; sc.sp=1.f;
    pushEnt(M,S,S,S,C_DARK[0]*1.5f,C_DARK[1]*1.5f,C_DARK[2]*1.5f,
            0,&sc,LUMI_PRIM[LMP_MARK]); }

  /* ---- 2. what is lit on her, and is not the face -------------------- */
  pushEnt(M,S,S,S,C_CYAN[0],C_CYAN[1],C_CYAN[2],1.8f,&gx,LUMI_PRIM[LMP_CYAN]);
  pushEnt(M,S,S,S,C_DIM[0],C_DIM[1],C_DIM[2],0.9f,&gx,LUMI_PRIM[LMP_DIM]);

  /* ---- 3. the lift field.  On the v6 bench everything circular, and it
     spreads sideways rather than down, because a plume is a rocket and a
     rocket is not what a maintenance drone has.

     beamLayer() bakes its vertical fade into VERTEX COLOUR and this
     renderer's colour is per instance, so the two layers carry the fade
     as their own emissive instead -- 0.16 and 0.11 opacity in the bench
     become the same ratio here. */
  /* THE ENTITY PASS DOES NOT BLEND.  render.c turns GL_BLEND on for the
     glass and off again, so fx.alpha only darkens -- an additive layer is
     not available, and a 26 cm disc at the bench's emissive is a solid
     white plate with a lampshade under it.  The bench's own opacities
     (0.16 and 0.11) become the ratio between three emissives instead: the
     RING carries the light, the disc is a soft floor under it, and the
     two cones are barely there.  Her cyan dynamic light does the rest. */
  { float pu=0.93f+0.07f*sinf(L.t*4.2f);
    pushEnt(M,S,S,S,C_CYAN[0]*0.80f,C_CYAN[1]*0.92f,C_CYAN[2],0.85f*pu,&gx,
            LUMI_PRIM[LMP_GLOW]);
    pushEnt(M,S,S,S,C_CYAN[0]*0.30f,C_CYAN[1]*0.42f,C_CYAN[2]*0.50f,0.26f*pu,&gx,
            LUMI_PRIM[LMP_BEAM0]);
    pushEnt(M,S,S,S,C_CYAN[0]*0.24f,C_CYAN[1]*0.34f,C_CYAN[2]*0.40f,0.17f*pu,&gx,
            LUMI_PRIM[LMP_BEAM1]); }

  /* the status ring: blue-white normal, amber warned, red once the ship
     has given up.  Four segments on the rim, not a torus -- the generator
     has no such part, because on the bench this is the emitter ring's own
     colour and the emitter ring here is shared with two LEDs. */
  { const float *rc;
    static const float BW[3]={0.42f,0.78f,1.00f};
    static const float AM[3]={1.00f,0.62f,0.14f};
    static const float RD[3]={1.00f,0.18f,0.12f};
    rc = (L.disp==LD_WARN)?AM:(L.hp<0.30f?RD:BW);
    for(int i=0;i<4;i++){
      float a=i*1.5708f+0.7854f;
      mTrans34(cosf(a)*V(0.166f),V(-0.152f),sinf(a)*V(0.166f),A);
      mMul34(M,A,C);
      pushEnt(C,V(0.048f),V(0.014f),V(0.048f),rc[0],rc[1],rc[2],1.5f,&gx,PRIM_LENS); }
  }

  /* ---- and what the visor is saying.  The frame WITHOUT the visor tilt:
     the screen patch carries that already, baked in by the generator. */
  { float blink = (L.blink<0.f&&L.blink>-0.12f) ? 0.12f : 1.f;
    if(L.disp==LD_EYES_TALK) blink*=0.72f+L.talk*0.45f;
    faceSet(M,S,L.disp,L.hp,L.bpm,L.count,
            L.disp==LD_OPEN?L.openU:L.talk,blink,L.t); }
}
