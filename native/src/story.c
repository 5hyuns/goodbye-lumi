/* ============================================================
   THE DIRECTOR.

   The story is not written as a script: it is five systems and
   a table that drives them.  So this file is flags, an event bus, trigger volumes, a
   subtitle player and a spawn director -- and every line, every
   wave and every room rectangle comes out of story_data.h, which
   tools/storypack.py wrote from the JSON in story/.

   What stays as code here is the part that really is logic: the
   order the opening happens in, what a checkpoint restores, how
   a wave decides it is finished, and the ninety seconds at the
   end where the drone is gone and the ship is open.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "app.h"
#include "prim.h"
#include "ent.h"
#include "hud.h"
#include "kfont.h"
#include "fx.h"
#include "zom.h"
#include "lumi.h"
#include "vmw.h"
#include "story.h"
#include "menu.h"
#include "props.h"
#include "audio.h"
#include "story_data.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* gameplay randomness only -- nothing here feeds a mesh */
static unsigned int srs=0x2545F491u;
static float rnd(void){
  srs^=srs<<13; srs^=srs>>17; srs^=srs<<5;
  return (float)(srs&0xFFFFFF)/16777216.f;
}

/* ---- what the rest of the exe lends us ------------------------------ */
float gamePlayerHP(void);
void  gameSetHP(float v);
void  gamePlayerPos(float *o);
void  gamePlayerEye(float *o);
float gamePlayerYaw(void);
float gamePlayerPitch(void);
void  gameTeleport(float x,float y,float z,float yaw);
void  gamePitch(float p);
int   rayTraceOne(float,float,float,float,float,float,float,int);
extern float rayHitDist;
int   adNearest(float x,float z);
int   adCount(void);
float zomGoalDist(float x,float y,float z);
void  adLock(int i,int on);
void  adForce(int i,int on);
int   adOpen(int i);
void  adPos(int i,float *o);
void  adAllOpen(int on);
void  auBeep(float pitch,int warn);
void  auHeart(float strength);
void  auAlarmHit(float strength);
void  auHullGroan(void);
void  sfxCreature(int kind,float x,float y,float z,float sz);
void  vmwGive(int slot);
int   vmwOwns(int slot);
int   vmwArmed(void);
void  vmwAddAmmo(int slot,int n);
void  vmwDisarmAll(void);
void  vmwSnapshot(int *o);
void  vmwRestore(const int *o);
void  lumiDock(float x,float y,float z);

/* ============================================================
   1.  FLAGS
   ============================================================ */
static struct {
  int   introDone, galleryBriefed, shortcutOpen, finalDefense, epilogue;
  int   packState[4];              /* 1..3: 0 not found, 1 carried, 2 in */
  int   pack3Cracked;
  float slot[7];                   /* 1..6, per cent */
  int   sinkStage;
  float timerLaunch;
  int   carrying;                  /* which pack is in hand, 0 for none */
  int   boarded, dead;
} F;

static int flagOn(const char *n){
  if(!n||!*n) return 1;
  if(!strcmp(n,"has_pistol"))   return vmwOwns(1);
  if(!strcmp(n,"has_shotgun"))  return vmwOwns(2);
  if(!strcmp(n,"has_smg"))      return vmwOwns(3);
  if(!strcmp(n,"gallery_briefed")) return F.galleryBriefed;
  if(!strcmp(n,"shortcut_open"))return F.shortcutOpen;
  if(!strcmp(n,"final_defense"))return F.finalDefense;
  if(!strcmp(n,"pack1_installed")) return F.packState[1]==2;
  if(!strcmp(n,"pack2_installed")) return F.packState[2]==2;
  if(!strcmp(n,"pack3_installed")) return F.packState[3]==2;
  if(!strcmp(n,"pack3_cracked"))return F.pack3Cracked;
  return 0;
}

/* ============================================================
   2.  THE EVENT BUS

   Events are strings because there are ninety of them and they
   are written down in three JSON files; interning them into an
   enum would mean maintaining the enum by hand next to the data
   that already names them.  They are queued rather than
   dispatched inline, so a handler that emits is not a handler
   that recurses.
   ============================================================ */
#define EVQ 96
#define EVLEN 56                              /* "enter_R-QTR first" and room */
static char  EVQ_[EVQ][EVLEN];
static int   evHead,evTail;
static void  dispatch(const char *ev);

void storyEvent(const char *ev){
  int n=(evTail+1)%EVQ;
  if(n==evHead){
    /* AND IT SAYS SO.  Ninety-six is more than any frame has ever queued
       -- pump() drains the whole ring every frame -- but a silent drop
       here is a scene that stops with no reason anywhere, which is the
       one failure this file must never have. */
    platLog("story  EVENT QUEUE FULL -- dropped %s",ev);
    return;
  }
  int k=0; while(ev[k]&&k<EVLEN-1){ EVQ_[evTail][k]=ev[k]; k++; }
  EVQ_[evTail][k]=0;
  evTail=n;
}
static int storyTrace=0;
void storySetTrace(int on){ storyTrace=on; }
static void pump(void){
  int guard=0;
  while(evHead!=evTail&&guard++<256){
    char e[EVLEN]; strcpy(e,EVQ_[evHead]);
    evHead=(evHead+1)%EVQ;
    if(storyTrace&&strncmp(e,"dialogue_",9)) platLog("event  %s",e);
    dispatch(e);
  }
}

/* ---- the named points, in the order storypack wrote them ------------ */
static float LP[SP_N][2];
enum { P_START=0,P_LUMI_A,P_LUMI_B,P_LUMI_C,P_CONSOLE,P_DOCK,P_CAPSULE,
       P_DUCT,P_DUCT_LAND,P_TERMINAL };

/* ============================================================
   3.  TRIGGER VOLUMES
   ============================================================ */
static unsigned char volIn[SV_N], volSeen[SV_N];
static int  volNow=-1;                        /* the smallest one you are in */

/* HYSTERESIS, or the boundary shouts.  A player walking a doorway sits on
   the edge of two volumes and crosses it a dozen times a second; without a
   margin every one of those is an enter and an exit on the bus. */
#define VOL_HYST 0.45f
static void volumes(float px,float pz){
  int best=-1; float bestA=1e18f;
  for(int i=0;i<SV_N;i++){
    const float *r=SV_RECT+i*4;
    float m = volIn[i]? VOL_HYST : -0.f;
    int in = px>=r[0]-m&&px<=r[2]+m&&pz>=r[1]-m&&pz<=r[3]+m;
    if(in){
      float a=(r[2]-r[0])*(r[3]-r[1]);
      if(a<bestA){ bestA=a; best=i; }
    }
    if(in&&!volIn[i]){
      char b[52];
      volIn[i]=1;
      _snprintf(b,sizeof b,"enter_%s",SV_ID[i]); storyEvent(b);
      if(!volSeen[i]){
        volSeen[i]=1;
        _snprintf(b,sizeof b,"enter_%s first",SV_ID[i]); storyEvent(b);
      }
    } else if(!in&&volIn[i]){
      char b[52];
      volIn[i]=0;
      _snprintf(b,sizeof b,"exit_%s",SV_ID[i]); storyEvent(b);
    }
  }
  volNow=best;

  /* ---- the two volumes that are not rectangles on the room list -------
     E-01 waits until you are past halfway down the tutorial spine with a
     gun in your hand (spawns.json: "event:E01_zone && flag_has_pistol"),
     and the duct is a hole in the cargo floor rather than a room. */
  { static int e01=0, ductSeen=0;
    static int c03=-2;
    if(c03==-2){ c03=-1;
      for(int i=0;i<SV_N;i++) if(!strcmp(SV_ID[i],"C-03")){ c03=i; break; } }
    if(!e01&&c03>=0&&volIn[c03]&&pz>(310.f-130.f)*0.145f){
      e01=1; storyEvent("E01_zone");
    }
    float dx=px-LP[P_DUCT][0], dz=pz-LP[P_DUCT][1];
    int nearDuct = dx*dx+dz*dz < 6.25f;
    if(nearDuct&&!ductSeen){ ductSeen=1; storyEvent("enter_duct"); }
    if(!nearDuct) ductSeen=0;
  }
}

/* ============================================================
   4.  THE SUBTITLE PLAYER

   Rules straight out of dialogue.json: nine characters a second
   plus six tenths of tail, one beep a character, never during a
   fight unless the line is one of the five the finale needs, and
   the drone's display follows the line rather than the line
   following the display.
   ============================================================ */
static int   said[SL_N];
static int   dlgCur=-1, dlgQ[32], dlgQN;
static float dlgT, dlgLen, dlgBeep, restoreT;
static int   dlgBark=-1;
static float silence;
static char  barkTxt[160];

static float lineLen(const char *s){
  return krCells(s)/(float)SL_CPS + SL_TAIL;
}
/* ---- ONE FLAG, AND EVERY STRING IN THE STORY ANSWERS TO IT.  English is
   a table beside the Korean rather than a replacement for it, so the whole
   of the localisation is these three functions and menuPick(); a line
   whose translation is missing falls back to the original, because an
   untranslated line is a small problem and a blank one is not. */
/* AND A LINE IS NAMED, NOT NUMBERED.  said[37] meant "L-038 has played"
   and said[46] meant "the epilogue has", which was true until the epilogue
   grew two lines and everything after L-047 moved down two slots.  A
   literal index into a generated table is a bug with a delay on it. */
static int saidId(const char *id){
  for(int i=0;i<SL_N;i++) if(!strcmp(SL_ID[i],id)) return said[i];
  return 0;
}
static const char *lineTxt(int i){
  if(menuLang()==LANG_EN) return SL_TEXT_EN[i];
  if(menuLang()==LANG_JA) return SL_TEXT_JA[i];
  return SL_TEXT[i];
}
static const char *barkTxtOf(int i){
  if(menuLang()==LANG_EN) return SB_TEXT_EN[i];
  if(menuLang()==LANG_JA) return SB_TEXT_JA[i];
  return SB_TEXT[i];
}
static const char *questTxt(int q){
  if(menuLang()==LANG_EN) return SQ_TEXT_EN[q];
  if(menuLang()==LANG_JA) return SQ_TEXT_JA[q];
  return SQ_TEXT[q];
}

static void queueLine(int i){
  if(i<0||i>=SL_N||said[i]) return;
  if(dlgQN>=32) return;
  for(int k=0;k<dlgQN;k++) if(dlgQ[k]==i) return;
  dlgQ[dlgQN++]=i;
}
static void startLine(int i){
  said[i]=1; dlgCur=i; dlgT=0; dlgLen=lineLen(lineTxt(i)); dlgBeep=0;
  dlgBark=-1;
  lumiDisplay(SL_D0[i]);
  silence=0;
  char b[52]; _snprintf(b,sizeof b,"dialogue_start:%s",SL_ID[i]);
  storyEvent(b);
  if(storyTrace) platLog("say    %s  %.1fs",SL_ID[i],dlgLen);
}
static void bark(void){
  int n=(int)(rnd()*SB_N);
  static int used[SB_N];
  int all=1; for(int i=0;i<SB_N;i++) if(!used[i]) all=0;
  if(all) memset(used,0,sizeof used);
  for(int k=0;k<SB_N;k++){ int i=(n+k)%SB_N; if(!used[i]){ n=i; break; } }
  used[n]=1;
  strncpy(barkTxt,barkTxtOf(n),sizeof barkTxt-1);
  dlgBark=n; dlgCur=-1; dlgT=0; dlgLen=lineLen(barkTxt); dlgBeep=0;
  lumiDisplay(LD_EYES_TALK);
}
static const char *dlgText(void){
  if(dlgBark>=0) return barkTxt;
  return dlgCur>=0?lineTxt(dlgCur):0;
}

/* ============================================================
   5.  THE SPAWN DIRECTOR
   ============================================================ */
enum { M_VENT=0,M_CRACK,M_CRATE,M_COCOON,M_CEILING,M_WALLBREAK,M_CHASE };

static struct { int fired,done,wave; float t; } EN[SE_N];
/* THE QUEUE HAS TO OUTLAST THE CAP.  A spawn the cap turns away keeps its
   slot and retries every half second, and E-12 fires another 8-14 rows on
   the clock regardless -- so a player who kills slowly backs the queue up,
   and pend() below simply RETURNS when every slot is held.  Whole waves
   were being dropped with nothing logged anywhere.  136 bodies is the
   entire schedule; 192 slots cannot be filled by it. */
#define MAXPEND 192
static struct { int used,type,method,enc; float t,x,z; } PEND[MAXPEND];

/* THE DECK IS FOUND FROM UNDER THE CEILING, NOT FROM ABOVE IT.  A trace
   that starts at seven metres is above every corridor in this ship (they
   are 3.9 m) and finds the ceiling, which is how the first wave of the
   final defence came out standing on the deckhead.  2.9 m is under every
   ceiling here and over every floor. */
static float floorAt(float x,float z,float fallback){
  int b=rayTraceOne(x,2.9f,z,0,-1,0,4.2f,-1);
  float d=rayHitDist;
  return b<0?fallback:2.9f-d;
}

/* deckNear()/deckAt() are level.c's: what a person would be standing
   on here, which is not the first thing the trace hits.  See there. */

/* A FIGHT IS A FIGHT YOU ARE IN.  "Combat" was zomLive() -- is anything at
   all alive anywhere in this ship -- and three systems hang off it: the
   subtitle player holds every non-exception line, Lumi abandons the route
   for your shoulder, and her face goes to the HUD station.  Two scuttlers
   left standing thirty metres behind you therefore stop the story dead:
   -auto reached the gallery with two of E-01's alive at 31.3 m, the
   briefing never played, dialogue_end:L-020 never fired, and with it
   gallery_briefed -- so D-22 stayed locked and the run ended there with
   the banner still asking for something that could not be done.  The room
   you are standing in is what decides. */
#define COMBAT_R 18.0f
static int inFight(void){
  float p[3]; gamePlayerPos(p);
  float b[4]={0,0,0,-1};
  zomNearest(p[0],p[2],b);
  return b[3]>=0.f&&b[3]<COMBAT_R;
}
/* Nothing appears in front of you inside twelve metres.  If the
   written point is in view and close, it is moved around the player until
   it is not -- which is cheaper than a visibility graph and reads the same.

   AND THE DECK GETS A VOTE.  The old push rotated sixty degrees at a time
   and handed whatever it landed on to freeSpot.  The rotation is blind to
   geometry, and in the escape gallery -- a 25 m hall with a bulkhead down
   its whole east side -- what it lands on is as often wall as deck.
   freeSpot then failed and directorStep DELETED the body: no log line, no
   retry, and the wave the player was waiting for was simply smaller.  Every
   candidate is now tested for standable deck BEFORE it is accepted, at
   thirty degrees a step, and if the whole ring is bulkhead the body goes on
   a wider ring behind the player rather than into the bin. */
static int freeSpot(float *x,float *z,float *y);
#define VIEW_DOT 0.36f                          /* cos of the "in front" cone */
static int placeSpawn(float *x,float *z,float *y){
  float p[3]; gamePlayerPos(p);
  float yaw=gamePlayerYaw();
  float fx=sinf(yaw), fz=cosf(yaw);
  float dx=*x-p[0], dz=*z-p[2];
  float d=hypot2(dx,dz);
  if(d>12.f) return freeSpot(x,z,y);            /* far enough off: as written */
  if(d<0.4f){ dx=2.f; dz=0.f; d=2.f; }
  for(int k=0;k<12;k++){
    float a=k*0.5236f;                          /* thirty degrees a step */
    float ca=cosf(a), sa=sinf(a);
    float nx=dx*ca-dz*sa, nz=dx*sa+dz*ca;
    if((nx*fx+nz*fz)/d>=VIEW_DOT) continue;     /* still out in front of you */
    float tx=p[0]+nx, tz=p[2]+nz;
    if(freeSpot(&tx,&tz,y)){ *x=tx; *z=tz; return 1; }
  }
  /* the whole ring at that radius is bulkhead: stand it further off */
  for(int r=0;r<3;r++){
    float rad=8.f+r*4.f;
    for(int k=0;k<12;k++){
      float a=k*0.5236f;
      float bx=-fx*rad, bz=-fz*rad;             /* start behind you, and sweep */
      float ca=cosf(a), sa=sinf(a);
      float nx=bx*ca-bz*sa, nz=bx*sa+bz*ca;
      if((nx*fx+nz*fz)/rad>=VIEW_DOT) continue;
      float tx=p[0]+nx, tz=p[2]+nz;
      if(freeSpot(&tx,&tz,y)){ *x=tx; *z=tz; return 1; }
    }
  }
  return 0;
}
/* A WRITTEN SPAWN POINT IS A POINT ON A DRAWING, and the drawing does not
   know where this ship's walls ended up.  Six of the fifty-eight land inside
   one -- E-12's entry A is exactly on the gallery's east bulkhead -- and a
   body that spawns inside a brush is grounded, blocked on every axis and
   frozen there for the rest of the game.  So the point is a suggestion: the
   nearest standing spot to it is where the animal actually arrives. */
static int freeSpot(float *x,float *z,float *y){
  for(int r=0;r<7;r++){
    int steps = r?8:1;
    for(int a=0;a<steps;a++){
      float ang=a*0.7854f+r*0.41f, rad=r*0.85f;
      float tx=*x+cosf(ang)*rad, tz=*z+sinf(ang)*rad;
      float ty;
      if(!deckAt(tx,tz,&ty)) continue;
      *x=tx; *z=tz; *y=ty; return 1;
    }
  }
  return 0;
}
static void pend(int type,int method,int enc,float t,float x,float z){
  for(int i=0;i<MAXPEND;i++) if(!PEND[i].used){
    PEND[i].used=1; PEND[i].type=type; PEND[i].method=method;
    PEND[i].enc=enc; PEND[i].t=t; PEND[i].x=x; PEND[i].z=z;
    return;
  }
}
static void fireWave(int e,int w){
  int u0=SW_U0[w], un=SW_UN[w];
  for(int u=u0;u<u0+un;u++){
    int type=SU_TYPE[u]-1;                      /* M1..M4 -> 0..3 */
    if(type<0||type>=Z_NTYPE) type=0;
    int meth=SU_METHOD[u];
    float lead = meth==M_VENT?1.2f : meth==M_CRACK?0.8f
               : meth==M_WALLBREAK?SU_LEAD[u] : 0.f;
    for(int n=0;n<SU_MANY[u];n++){
      float x=SU_POS[u*2], z=SU_POS[u*2+1];
      float a=rnd()*6.2832f;
      x+=cosf(a)*0.9f*n; z+=sinf(a)*0.9f*n;     /* they do not stack */
      if(meth==M_CHASE){
        float p[3]; gamePlayerPos(p);
        float yaw=gamePlayerYaw();
        x=p[0]-sinf(yaw)*15.f; z=p[2]-cosf(yaw)*15.f;
      }
      pend(type,meth,e,lead+n*0.18f,x,z);
      /* the tell, ahead of the body: a hatch bursting or a wall cracking,
         which is the whole reason for the lead time */
      if(n==0&&lead>0) sfxCreature(1,x,1.4f,z,zomTypeSize(type));
    }
  }
}
/* A TRIGGER THAT ARRIVES TOO EARLY IS NOT A TRIGGER THAT IS GONE.

   Three encounters carry a condition as well as a trigger, and two of the
   three are triggered by something that only ever happens ONCE -- E-01 by
   a latch in the C-03 spine, E-02 by `enter_R-LBY first`.  Both want
   has_pistol, and the pistol is an E press the player can simply not
   make: walk past W1, cross the zone, and the first two fights of the
   game are gone for the rest of the run with nothing logged anywhere.
   (E-09, the third, is triggered by the repeatable `enter_C-04` and
   retries by itself -- the pattern was already here, on one row of three.)

   So a trigger whose condition is not met yet ARMS the encounter instead
   of being spent, and the arming is re-checked on every event.  The flag
   these wait on turns during an event -- vmwGive runs before takeItem
   fires pickup_W1 -- so the re-check lands on the same dispatch. */
static unsigned char encArmed[SE_N];

static void encStart(int e){
  if(EN[e].fired) return;
  EN[e].fired=1; EN[e].wave=0; EN[e].t=0;
  if(SE_WN[e]>0&&SW_T[SE_W0[e]]<=0.001f){ fireWave(e,SE_W0[e]); EN[e].wave=1; }
}
static void encClear(int e){
  char b[52];
  EN[e].done=1;
  _snprintf(b,sizeof b,"clear_%s",SE_ID[e]);
  storyEvent(b);
}

/* ============================================================
   6.  ITEMS
   ============================================================ */
static struct { int live,taken; float x,y,z,respawn,bob; } IT[SI_N];
static int itemBy(const char *id){
  for(int i=0;i<SI_N;i++) if(!strcmp(SI_ID[i],id)) return i;
  return -1;
}

/* ---- CAN THE PLAYER ACTUALLY GET THERE ------------------------------
   The deck plan is not the ship, and this is the one place that cannot
   be shrugged off.  Standable is not the same as reachable: build_ship.py snaps what
   it builds to the half-module grid, and that leaves pockets of real deck
   the nav lattice never joined to the rest of the ship.  A quest target
   in one of those is a run that ends there -- the compass points at it,
   the ring stands on it, Lumi flies at it, and -auto ground against the
   escape gallery's south wall for four minutes trying to walk to a
   shotgun nine metres away across a floor with no route to it.

   navFromStart solves the field once, at boot, from the player's own
   start, so "reachable" here means reachable FROM THE BEGINNING OF THE
   GAME rather than from wherever the asker happens to be standing. */
static int navReady=0;
static void navFromStart(void){
  if(navReady) return;
  zomGoal(SP_POS[P_START*2],0.f,SP_POS[P_START*2+1]);
  navReady=1;
}
static int reachable(float x,float z,float y){
  float d=zomGoalDist(x,y+0.25f,z);
  return d>=0.f && d<1e8f;
}

/* AN ITEM HAS TO BE SOMEWHERE A PLAYER CAN REACH, and reach is a sphere
   in three dimensions: itemStep takes the nearest thing within 2.4 m of
   the player's FEET.  An item that settles on a ceiling pipe at 2.7 m is
   therefore not merely hard to see, it cannot be picked up at all -- no
   approach brings the feet inside the sphere.  W3, the rotary, was one of
   those, and so was an energy pack.

   So the written point is a suggestion, exactly as a spawn point is
   (freeSpot): the nearest
   standable deck to it is where the thing actually lies.  The one item
   with a deck written into the table -- pack 3, up on the cargo hold's
   platform, and that height is the whole reason the duct sequence exists
   -- keeps the height it was given. */
/* THE OTHER DIRECTION, and on this lattice it is a different question.
   reachable() above solves the field FROM the start, so what it measures
   is the cost of walking from the point TO the start -- which is the same
   number on the main deck, where every link is two-way, and exactly the
   wrong one for a platform.  A platform is the one shape in this ship you
   can leave by stepping off and cannot re-enter the same way, so it is
   precisely where the two answers come apart: the nav lattice is directed.
   routeTo() below asks it the right way round; it re-solves the field from
   its OWN goal to do so, which leaves reachable()'s start field gone, so
   the next asker is told to rebuild it. */
static int routeTo(float x,float z,float y);
static int routeUpTo(float x,float z,float y){
  int ok=routeTo(x,z,y);
  navReady=0;
  return ok;
}

static void settleItem(int i){
  float x=SI_POS[i*3], z=SI_POS[i*3+1], deck=SI_POS[i*3+2];
  float y;
  IT[i].x=x; IT[i].z=z;
  /* A WRITTEN DECK IS A HEIGHT ON THE DRAWING, NOT A FLOOR.  Pack 3 is
     the one item with a deck in the table -- 3.15 m, up on the cargo
     hold's platform, and that height is the whole reason the duct
     sequence exists -- and taking the number literally hung it in mid
     air, because build_ship.py snaps the platform it actually built to
     the half-module grid and 3.15 is not on it.  deckNear walks the
     column and answers with the real surface nearest the height asked
     for, which is the same intent and lands on something. */
  if(deck>0.1f){
    /* and it has to be a surface NEAR the height asked for, not merely
       the answer deckNear gives for that column: at pack 3's own point
       there is nothing at 3.15 at all, and taking the column's floor
       silently turns "up on the platform" into "on the deck" with no
       error.  Within 1.2 m counts as the platform; past that the item
       falls through to the ordinary settle below, on real floor. */
    /* AND IT HAS TO HAVE A ROUTE UP, which this path never asked.  It did
       not have to while deckNear began its column at 2.90 m: it could not
       return a platform at all then, so the branch was dead and every run
       fell through to the floor.  With the column started above the deck
       being asked about the branch is live, and pack 3 is the THIRD energy
       pack -- a platform with no way onto it is not a lost beat, it is a
       run that cannot be finished.  No route, no promotion: fall back to
       the floor. */
    int nr=0; float nry=0;
    for(int r=0;r<7;r++){
      int steps=r?8:1;
      for(int a=0;a<steps;a++){
        float ang=a*0.7854f+r*0.31f;
        float tx=x+cosf(ang)*r*0.55f, tz=z+sinf(ang)*r*0.55f;
        if(!deckNear(tx,tz,deck,&y)) continue;
        if(fabsf(y-deck)>1.20f) continue;
        if(!routeUpTo(tx,tz,y)){ if(!nr){ nr=1; nry=y; } continue; }
        IT[i].x=tx; IT[i].z=tz; IT[i].y=y;
        platLog("item   %-10s written deck %.2f -> real %.2f at %.2f m",
                SI_ID[i],deck,y,r*0.55f);
        return;
      }
    }
    if(nr)
      platLog("item   %-10s written deck %.2f is real at %.2f and has NO "
              "ROUTE UP -- settling on the floor",SI_ID[i],deck,nry);
    else
      platLog("item   %-10s NOTHING within 1.2 m of its written deck %.2f "
              "-- settling on the floor",SI_ID[i],deck);
  }
  /* THREE PASSES, AND EACH GIVES UP ONE THING.  The cargo hold is a stack
     of container tops and every one of them is standable, so "the nearest
     standable surface" alone put the rotary 2.4 m up on a container --
     better than the pipe it had been on and still not a thing you can
     walk to.  Pass one wants the main deck AND a route to it from the
     player's start; pass two drops the route; pass three drops the deck,
     so an item written over a platform is placed rather than lost. */
  navFromStart();
  for(int pass=0;pass<3;pass++)
    for(int r=0;r<10;r++){
      int steps = r?12:1;
      for(int a=0;a<steps;a++){
        float ang=a*0.5236f+r*0.37f, rad=r*0.62f;
        float tx=x+cosf(ang)*rad, tz=z+sinf(ang)*rad;
        if(!deckAt(tx,tz,&y)) continue;
        if(pass<2&&fabsf(y)>1.20f) continue;
        if(pass<1&&!reachable(tx,tz,y)) continue;
        IT[i].x=tx; IT[i].z=tz; IT[i].y=y;
        if(r||pass) platLog("item   %-10s moved %.2f m -> y %.2f%s",
                            SI_ID[i],rad,y,
                            pass==1?"  (NO ROUTE)":(pass==2?"  (OFF DECK)":""));
        return;
      }
    }
  IT[i].y=0.f;
  platLog("item   %-10s NO DECK within 5.6 m of its written point",SI_ID[i]);
}

/* ---- and the same question, asked the OTHER WAY ROUND, of every place
   the story sends the player -------------------------------------------
   reachable() above answers "can a body walk from here to the start",
   which is what a cheap pre-filter can afford and is not the question.
   The question is "can the player walk from the start to HERE", and on a
   directed lattice those differ -- see zomRouteExists.  Every quest
   target, and the three landmarks the finale steers by, are solved that
   way once at boot.  It costs one field solve each and it is the whole
   difference between an objective and a dead end.

   A pickup objective is answered by the ITEM: settleItem has already
   found it a deck, so the ring, the needle and the drone stand on that
   rather than on the point it was written at -- and if the item itself
   turns out to be off the route, the item moves with the marker, because
   a marker standing somewhere the thing is not is worse than either. */
static float QT[SQ_N*2];
static int routeTo(float x,float z,float y){
  return zomRouteExists(SP_POS[P_START*2],0.4f,SP_POS[P_START*2+1],x,y+0.25f,z);
}
/* the nearest point to (x,z) the player can actually walk to, or 0 */
static int onRoute(float *x,float *z,float *y,float step,int rings){
  if(deckAt(*x,*z,y)&&routeTo(*x,*z,*y)) return 1;
  for(int r=1;r<=rings;r++)
    for(int a=0;a<10;a++){
      float ang=a*0.6283f+r*0.31f, rad=r*step;
      float tx=*x+cosf(ang)*rad, tz=*z+sinf(ang)*rad;
      float ty;
      if(!deckAt(tx,tz,&ty)) continue;
      if(fabsf(ty)>1.20f) continue;
      if(!routeTo(tx,tz,ty)) continue;
      *x=tx; *z=tz; *y=ty; return 1+r;
    }
  return 0;
}
/* ---- MOUNTING A FIXTURE ON A BULKHEAD -------------------------------
   The nearest of the four cardinals that answers solid, returned as the
   yaw a prop takes to have its BACK to it -- props.c's hasWall() fixes
   that convention: -Z in the prop's frame is its back, so the front is
   (sin yaw, cos yaw) and facing away from a wall in direction D means
   yaw = atan2(-D). */
#define FIX_REACH 6.00f    /* a plate further off than this is not a wall */
#define FIX_BACK  0.06f    /* how close the fixture's back sits to it */
#define FIX_STAND 1.35f    /* and how far in front of its face you stand */
static int wallNear(float x,float z,float y,float *outYaw,float *outGap){
  static const float DIR[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
  int best=-1; float bd=1e9f;
  for(int i=0;i<4;i++)
    for(float t=0.40f;t<FIX_REACH;t+=0.12f)
      if(solidAt(x+DIR[i][0]*t,y+0.90f,z+DIR[i][1]*t)){
        if(t<bd){ bd=t; best=i; }
        break;
      }
  if(best<0) return 0;
  *outYaw=atan2f(-DIR[best][0],-DIR[best][1]);
  *outGap=bd;
  return 1;
}
/* point the objective named `on` at a place, after snapQuests has run --
   used when the thing the objective is ABOUT has moved since.

   IT GOES THROUGH onRoute LIKE EVERY OTHER TARGET.  A ring the lattice
   cannot reach is a run that stops, and 1.35 m in front of a fixture is
   exactly the kind of spot that can land inside the plinth the fixture is
   standing on.  Same test snapQuests makes, and it says so when it fails
   rather than leaving a marker nobody can stand in. */
static void questAim(const char *on,float x,float z){
  for(int q=0;q<SQ_N;q++){
    if(strcmp(SQ_ON[q],on)) continue;
    float tx=x, tz=z, ty=0;
    if(!onRoute(&tx,&tz,&ty,0.75f,9)){
      platLog("quest  %-22s re-aim has NO ROUTE at %.2f %.2f",on,x,z);
      return;
    }
    QT[q*2]=tx; QT[q*2+1]=tz;
    return;
  }
}

static void snapQuests(void){
  for(int q=0;q<SQ_N;q++){
    float x=SQ_TARGET[q*2], z=SQ_TARGET[q*2+1], y=0;
    int it=-1;
    if(q+1<SQ_N&&!strncmp(SQ_ON[q+1],"pickup_",7)){
      it=itemBy(SQ_ON[q+1]+7);
      if(it>=0){ x=IT[it].x; z=IT[it].z; }
    }
    int r=onRoute(&x,&z,&y,0.75f,9);
    QT[q*2]=x; QT[q*2+1]=z;
    if(r>1){
      platLog("quest  %-22s target moved %.2f m onto the route",
              SQ_ON[q],(r-1)*0.75f);
      if(it>=0){ IT[it].x=x; IT[it].z=z; IT[it].y=y;   /* and so does the thing */
                 platLog("item   %-10s follows its objective",SI_ID[it]); }
    } else if(!r)
      platLog("quest  %-22s target has NO ROUTE within 6.8 m",SQ_ON[q]);
  }
}

/* ============================================================
   7.  DOORS, THE QUEST BANNER, AND THE REST OF THE STATE
   ============================================================ */
static int   dIx[SD_N];
static unsigned char dOpened[SD_N];   /* she only announces a door once */
static int   questOn=-1; static float questFlash;
static unsigned char arrived[SQ_N];    /* the goal ring, fired once each */
static float camY=1.f, camRoll=0, sinkRoll=0;
static int   lockLook=1, lockMove=1;
static float bpm=118.f, heartT=0, alarmT=0, groanT=20.f;
static float dockX,dockZ,consoleX,consoleZ,capX,capZ;
static float openU=0, openTarget=-1; static int openDoor=-1;
static float installT=0;
static float sinkMix=0;
static char  failMsg[64];
static int   overT=0; static float creditT=0;
static int   endMenu=0;            /* the ending offers the title card */
static float endX0,endY0,endX1,endY1;
static float damageT=0;

/* the checkpoint.  Health and ammunition come back as they were when it
   was taken, which is also the only way the final defence is
   survivable on the third try. */
static struct {
  int   valid;
  float x,y,z,yaw,hp;
  int   guns[10];
  int   packState[4], sink, briefed, shortcut, cracked;
  float slot[7];
  /* THE THREE THINGS THAT WERE NOT IN HERE, AND THE DEAD END THEY MADE.
     restore() dropped whatever pack you were holding and set its state
     back to "not found" -- but the pack is an ITEM, and the item was
     still marked taken, so it was not in the world either.  The banner
     then read "return to the gallery console" for the rest of the run
     with nothing to install and nothing to go and fetch, and -auto sat
     on the console for 470 of its 700 seconds proving it.  A checkpoint
     that does not restore the world's items, the objective and the thing
     in your hands is not a checkpoint. */
  int   carrying, quest;
  unsigned char taken[SI_N], live[SI_N];
  char  id[8];
} CP;
static void checkpoint(const char *id){
  float p[3]; gamePlayerPos(p);
  CP.valid=1; CP.x=p[0]; CP.y=p[1]; CP.z=p[2]; CP.yaw=gamePlayerYaw();
  CP.hp=gamePlayerHP();
  vmwSnapshot(CP.guns);
  memcpy(CP.packState,F.packState,sizeof CP.packState);
  memcpy(CP.slot,F.slot,sizeof CP.slot);
  CP.sink=F.sinkStage; CP.briefed=F.galleryBriefed;
  CP.shortcut=F.shortcutOpen; CP.cracked=F.pack3Cracked;
  CP.carrying=F.carrying; CP.quest=questOn;
  for(int i=0;i<SI_N;i++){ CP.taken[i]=(unsigned char)IT[i].taken;
                           CP.live[i]=(unsigned char)IT[i].live; }
  strncpy(CP.id,id,7); CP.id[7]=0;
}
/* the dock errand, up here because doorStep has to see it too */
static int   dockMoving=0;
static float dockT=0.f;

static void doorApply(void);
static void restore(void){
  if(!CP.valid) return;
  F.dead=0; failMsg[0]=0;
  gameTeleport(CP.x,CP.y,CP.z,CP.yaw);
  gameSetHP(CP.hp<25.f?25.f:CP.hp);
  /* SHE COMES WITH YOU, AND THIS IS THE ONLY PLACE SHE IS ALLOWED TO JUMP.
     Everywhere else she flies at a speed the player can watch; after a
     death the checkpoint is two rooms back and watching her cover that is
     twenty seconds of nothing. */
  lumiAt(CP.x+sinf(CP.yaw)*1.4f,CP.y+1.40f,CP.z+cosf(CP.yaw)*1.4f);
  vmwRestore(CP.guns);
  memcpy(F.packState,CP.packState,sizeof F.packState);
  memcpy(F.slot,CP.slot,sizeof F.slot);
  F.sinkStage=CP.sink; F.galleryBriefed=CP.briefed;
  F.shortcutOpen=CP.shortcut; F.pack3Cracked=CP.cracked;
  /* the world as it stood at the checkpoint: what you were holding, what
     was still lying about, and what the banner was asking for */
  F.carrying=CP.carrying;
  for(int i=0;i<SI_N;i++){ IT[i].taken=CP.taken[i]; IT[i].live=CP.live[i];
                           IT[i].respawn=0; }
  /* AND THE OBJECTIVE ROLLS BACK WITH THEM.  quest() only ever advances,
     which is right for a room you walk back through and wrong for a death:
     an objective ahead of the world it describes is the same dead end by
     another road. */
  if(CP.quest<questOn){
    questOn=CP.quest; questFlash=1.5f;
    for(int q=0;q<SQ_N;q++) if(q>=questOn) arrived[q]=0;
  }
  zomReset();
  for(int i=0;i<MAXPEND;i++) PEND[i].used=0;
  /* the encounters in the room you died in are armed again; the ones you
     already finished are not */
  for(int e=0;e<SE_N;e++) if(!EN[e].done) EN[e].fired=0;
  if(F.finalDefense){                  /* S10 restarts whole, boxes full */
    F.timerLaunch=90.f;
    for(int i=0;i<SI_N;i++) if(SI_KIND[i]==4){ IT[i].live=1; IT[i].taken=0; }
    for(int e=0;e<SE_N;e++) if(!strcmp(SE_ID[e],"E-12")){
      EN[e].fired=0; EN[e].wave=0; EN[e].t=0; encStart(e);
    }
  }
  doorApply();
}

/* ---- the doors the story owns -------------------------------------- */
static void doorApply(void){
  for(int i=0;i<SD_N;i++){
    if(dIx[i]<0) continue;
    int lock=0, force=0;
    switch(SD_LOCK[i]){
    case 0: lock=0; break;                       /* ordinary automatic door */
    case 1: lock=1; break;                       /* Lumi has to open it */
    case 2: lock=!flagOn(SD_COND[i]); break;     /* held until a flag turns */
    case 3: lock=!F.shortcutOpen; force=F.shortcutOpen; break;   /* SD-01 */
    }
    /* A DOOR SHE HAS ALREADY OPENED IS OPEN, AND STAYS OPEN.  This
       function re-derives every lock from the table, and it runs again on
       every flag that turns -- so the gallery door Lumi opened on the way
       IN was locked again by the briefing that fires the moment you get
       there.  doorStep skips a door it has already opened, so nothing left
       in the ship could open it a second time: a locked door across the
       only way out of the gallery, with the drone drifting through it. */
    if(dOpened[i]){ lock=0; force=1; }
    if(F.sinkStage>=3){ lock=0; force=1; }       /* she has lost door control */
    adLock(dIx[i],lock);
    adForce(dIx[i],force);
  }
}

/* ============================================================
   8.  SCENES

   S0 is a timeline because an opening is a timeline.  Everything
   after it is the systems above, wired by the table.
   ============================================================ */
static int   sc=0;                 /* the opening's step */
static float scT=0;
static float standT=0;

/* A QUEST ONLY EVER ADVANCES.  The banner is an ordered list of where the
   story is up to, and half its triggers are room entries -- so walking back
   through the central lobby, which this route does six times, was setting
   the objective back to the one before it and pointing the compass at a
   room the player had already been sent away from. */
static void quest(int q){
  if(q<=questOn) return;
  questOn=q; questFlash=1.5f;
}
/* AND -scene NAMES ITS OBJECTIVE RATHER THAN COUNTING TO IT.  The banner
   is an ordered list, so inserting a step -- the pistol became one --
   renumbers everything below it, and a literal index in here then stands
   the story up on the wrong objective with no error anywhere. */
static int questBy(const char *on){
  for(int q=0;q<SQ_N;q++) if(!strcmp(SQ_ON[q],on)) return q;
  return -1;
}
static void sink(int stage){
  if(stage<=F.sinkStage) return;
  F.sinkStage=stage;
  sinkRoll = stage==1?0.0436f : stage==2?0.0820f : 0.1309f;  /* 2.5/4.7/7.5 deg */
  doorApply();
  auAlarmHit(1.f);
}
static void installPack(int n){
  char b[32];
  F.packState[n]=2; F.carrying=0;
  F.slot[3+n] = (n==3&&F.pack3Cracked) ? 51.f : 100.f;
  _snprintf(b,sizeof b,"install_slot%d",3+n);
  storyEvent(b);
}

/* ============================================================
   9.  THE BUS ITSELF
   ============================================================ */
static void doClear(const char *acts){
  /* on_clear is a list: say:L-013, spawn_item:E06-BOX, open:duct */
  char buf[128]; strncpy(buf,acts,127); buf[127]=0;
  char *p=buf;
  while(*p){
    char *e=strchr(p,';'); if(e) *e=0;
    if(!strncmp(p,"say:",4)){
      for(int i=0;i<SL_N;i++) if(!strcmp(SL_ID[i],p+4)) queueLine(i);
    } else if(!strncmp(p,"spawn_item:",11)){
      int i=itemBy(p+11);
      if(i>=0){ IT[i].live=1; IT[i].taken=0; }
    } else if(!strncmp(p,"open:",5)){
      /* the duct in the cargo hold: the way down to the engine roof */
    }
    if(!e) break;
    p=e+1;
  }
}

static void dispatch(const char *ev){
  char b[64];

  /* --- dialogue ---------------------------------------------------- */
  for(int i=0;i<SL_N;i++)
    if(SL_TRIG[i][0]&&!said[i]&&!strcmp(SL_TRIG[i],ev)) queueLine(i);

  /* --- encounters --------------------------------------------------- */
  for(int e=0;e<SE_N;e++){
    if(EN[e].fired) continue;
    const char *t=SE_TRIG[e];
    if(!t[0]){                       /* triggered by a flag turning on */
      _snprintf(b,sizeof b,"flag_%s",SE_COND[e]);
      if(strcmp(b,ev)) continue;
    } else if(strcmp(t,ev)) continue;
    else if(!flagOn(SE_COND[e])){ encArmed[e]=1; continue; }
    encStart(e);
  }
  /* ... and the ones whose moment came before their condition did */
  for(int e=0;e<SE_N;e++)
    if(encArmed[e]&&!EN[e].fired&&flagOn(SE_COND[e])){
      encArmed[e]=0;
      if(storyTrace) platLog("event  %s armed earlier, starting now",SE_ID[e]);
      encStart(e);
    }

  /* --- quests ------------------------------------------------------- */
  for(int q=0;q<SQ_N;q++) if(!strcmp(SQ_ON[q],ev)) quest(q);

  /* --- items that appear when something happens ---------------------- */
  for(int i=0;i<SI_N;i++)
    if(SI_BY[i][0]&&!strcmp(SI_BY[i],ev)&&!IT[i].taken){ IT[i].live=1; }

  /* --- and the scenes ----------------------------------------------- */
  if(!strncmp(ev,"clear_",6)){
    for(int e=0;e<SE_N;e++)
      if(!strcmp(SE_ID[e],ev+6)&&SE_CLEAR[e][0]) doClear(SE_CLEAR[e]);
  }
  /* THE BRIEFING IS OVER WHEN THE BRIEFING IS OVER.  scenes.json sets
     flag_gallery_briefed at the end of S3 and nothing in the data says
     when that is; it is the moment L-020 stops talking, which is the line
     that names the first destination. */
  if(!strcmp(ev,"dialogue_end:L-020")) storyEvent("gallery_briefed");
  if(!strcmp(ev,"dialogue_end:L-041")) storyEvent("lumi_dock_arrive");
  /* THE TWO THINGS THE PLAYER CANNOT WORK OUT ALONE.  The torch is on a
     key nothing on screen mentions, and the ship is dark enough that not
     knowing is indistinguishable from the game being broken; the compass
     and the ring are new and want naming once.  Both hang off the end of
     a line rather than off the event that line answers, so they arrive in
     a gap instead of on top of her. */
  if(!strcmp(ev,"dialogue_end:L-008")) storyEvent("hint_light");
  if(!strcmp(ev,"dialogue_end:L-009")) storyEvent("hint_marker");
  if(!strcmp(ev,"gallery_briefed")){
    F.galleryBriefed=1; doorApply(); checkpoint("CP-2");
  }
  /* A PACK IN HAND IS WORTH A CHECKPOINT.  The three long carries are the
     only stretch of this game where dying costs more than the fight: with
     the item state restored a death sends you back for the pack, and one
     checkpoint at the pickup turns that into the walk you already made. */
  if(!strncmp(ev,"pickup_PACK-",12)) checkpoint("CP-PK");
  if(!strcmp(ev,"enter_R-ENG first")) checkpoint("CP-3");
  if(!strcmp(ev,"enter_R-MNT first")) checkpoint("CP-5");
  if(!strcmp(ev,"enter_R-CGO first")) checkpoint("CP-7");
  if(!strcmp(ev,"install_slot4")){ sink(1); doorApply(); checkpoint("CP-4"); }
  if(!strcmp(ev,"install_slot5")){ sink(2); doorApply(); checkpoint("CP-6"); }
  if(!strcmp(ev,"install_slot6")){
    /* the core is cracked and stops at 51%, which is the turn the whole
       story has been walking toward.  Nothing else happens here: she
       works it out on her own, in L-036 and L-037. */
    F.slot[6]=51.f;
  }
  if(!strcmp(ev,"pickup_PACK-2")){
    F.shortcutOpen=1; doorApply();
  }
  if(!strcmp(ev,"duct_landing")){
    F.pack3Cracked=1;
    sfxCreature(2,0,0,0,1.f);
    checkpoint("CP-8");
  }
  if(!strcmp(ev,"lumi_dock_arrive")){
    sink(3); F.finalDefense=1; F.timerLaunch=90.f;
    lumiDock(dockX,1.30f,dockZ);
    adAllOpen(1);
    checkpoint("CP-9");
    storyEvent("flag_final_defense");
    storyEvent("timer_start");
  }
  if(!strcmp(ev,"door_opened_D-01")){
    /* the opening is over the moment the bunkroom door is open, however it
       got opened -- Lumi's own five-metre rule beats the scripted step to
       the panel, and the scene must not sit waiting for a step that has
       already happened */
    checkpoint("CP-1");
    F.introDone=1; sc=9; openDoor=-1; openTarget=-1;
    if(lumiModeNow()==LM_PANEL) lumiMode(LM_LEAD);
  }
}

/* ============================================================
   10.  BOOT
   ============================================================ */
static int freeCam=0;
/* -cam puts the camera somewhere on purpose; the opening must not then
   drag the player back to the crew quarters */
void storyFreeCam(int on){ freeCam=on; }

static void storyResetLate(void);   /* the run state declared below here */
void storyInit(void){
  memset(&F,0,sizeof F);
  /* IT RUNS TWICE NOW.  "back to title" re-enters here rather than
     re-booting the exe, so everything this function APPENDS to has to be
     emptied first -- the story's keep-clear points and its two fixtures
     are both appends into props.c, and a second run without this walks
     the fixture array off its end. */
  propStoryReset();
  overT=0; creditT=0; endMenu=0; questOn=-1; sinkMix=0; sinkRoll=0;
  camY=1.f; camRoll=0; installT=0; damageT=0;
  openDoor=-1; openTarget=-1; openU=0;
  CP.valid=0;
  storyResetLate();
  memset(arrived,0,sizeof arrived);
  zomReset();
  F.slot[1]=F.slot[2]=F.slot[3]=100.f;
  F.epilogue=1;
  memset(said,0,sizeof said);
  memset(EN,0,sizeof EN);
  memset(encArmed,0,sizeof encArmed);
  memset(volIn,0,sizeof volIn); memset(volSeen,0,sizeof volSeen);
  memset(dOpened,0,sizeof dOpened);
  evHead=evTail=0; dlgQN=0; dlgCur=-1; dlgBark=-1;

  /* THE STORY NAMES A DOOR BY A POINT ON THE DRAWING AND THE SHIP BUILT IT
     SOMEWHERE ELSE.  The table's positions are the top-left corners of the
     plan's door rectangles, and build_ship widens every passage and snaps
     the leaf it stands up to the 0.6 m half-module grid -- so a leaf can sit
     three metres from the coordinate that names it, and half of them missed
     a plain nearest-within-three-metres lookup.  What is reliable is that
     there are exactly as many leaves as doors: assign each story door the
     nearest leaf NOT ALREADY TAKEN, and a systematic offset stops mattering
     because every door is offset the same way. */
  { int used[128]; int nL=adCount(); float worst=0;
    for(int k=0;k<nL&&k<128;k++) used[k]=0;
    for(int i=0;i<SD_N;i++){
      dIx[i]=-1;
      if(SD_LOCK[i]==3) continue;              /* SD-01 is a breach, not a leaf */
      int best=-1; float bd=1e9f;
      for(int k=0;k<nL&&k<128;k++){
        if(used[k]) continue;
        float dp[2]; adPos(k,dp);
        float d=hypot2(dp[0]-SD_POS[i*2],dp[1]-SD_POS[i*2+1]);
        if(d<bd){ bd=d; best=k; }
      }
      if(best>=0&&bd<8.f){ dIx[i]=best; used[best]=1; if(bd>worst) worst=bd; }
    }
    int miss=0;
    for(int i=0;i<SD_N;i++) if(dIx[i]<0&&SD_LOCK[i]!=3) miss++;
    platLog("story  %d doors bound, %d unmatched, worst %.2f m",SD_N-miss-1,miss,worst);
  }
  for(int i=0;i<SI_N;i++){
    settleItem(i);
    IT[i].live = SI_BY[i][0]?0:1;
    IT[i].taken=0;
    IT[i].bob=(float)i*0.7f;
    /* AND THE TWO KINDS THE RUN CANNOT BE FINISHED WITHOUT SAY WHERE THEY
       LANDED, every time, not only when they moved.  A weapon or a pack
       that settled on a container top is a run that stops there, and the
       old log was silent about exactly the case that costs the most --
       the one where the FIRST candidate was already wrong. */
    if(SI_KIND[i]==1||SI_KIND[i]==3)
      platLog("item   %-10s %s rests at %.2f m  (%.2f %.2f)",SI_ID[i],
              SI_KIND[i]==1?"weapon":"pack  ",IT[i].y,IT[i].x,IT[i].z);
  }
  snapQuests();
  for(int i=0;i<SP_N;i++){ LP[i][0]=SP_POS[i*2]; LP[i][1]=SP_POS[i*2+1]; }
  /* THE CAPSULE IS THE ONE POINT THE GAME CANNOT BE FINISHED WITHOUT.
     It is a landmark, which means it is a point on a drawing, and the
     drawing is not the ship: the boarding radius, the ring on the deck
     and where the ninety seconds want you standing are all this one
     number.  So the three the finale steers by are solved the same way
     the objectives are. */
  { static const int LSNAP[3]={P_DOCK,P_CONSOLE,P_CAPSULE};
    static const char *LN[3]={"dock","console","capsule"};
    for(int k=0;k<3;k++){
      float x=LP[LSNAP[k]][0], z=LP[LSNAP[k]][1], y=0;
      int r=onRoute(&x,&z,&y,0.70f,9);
      if(r>1){ LP[LSNAP[k]][0]=x; LP[LSNAP[k]][1]=z;
               platLog("point  %-8s moved %.2f m onto the route",
                       LN[k],(r-1)*0.70f); }
      else if(!r) platLog("point  %-8s has NO ROUTE within 6.3 m",LN[k]);
    } }
  dockX=LP[P_DOCK][0];       dockZ=LP[P_DOCK][1];
  consoleX=LP[P_CONSOLE][0]; consoleZ=LP[P_CONSOLE][1];
  capX=LP[P_CAPSULE][0];     capZ=LP[P_CAPSULE][1];
  /* NOTHING STANDS ON A THING THE STORY NEEDS.  The prop placer runs after
     this and refuses any spot within two metres of a point handed to it
     here -- every pickup, every landmark, every quest ring.  A crate over
     the pistol is the same bug as a pistol on a ceiling pipe, arriving by
     a different road. */
  for(int i=0;i<SI_N;i++) propKeepClear(IT[i].x,IT[i].z);
  for(int i=0;i<SP_N;i++) propKeepClear(LP[i][0],LP[i][1]);
  for(int q=0;q<SQ_N;q++) propKeepClear(QT[q*2],QT[q*2+1]);

  /* ---- THE TWO OBJECTS THE LAST ACT IS ABOUT, AND THEY STAND ON A WALL
     The six-slot console and the capsule are the bench's own Energy Pack
     Interface (#34) and Escape Capsule Hatch (#35).  Stood at their
     written coordinates, both are in the MIDDLE of a nine-metre room, the
     interface with its back to open floor and its face pointed at the far
     bulkhead, and the marker the player walks to is eight metres north of
     either of them.  What that reads as is three separate things in an
     empty hall, none of which is obviously the one the objective means.

     A fixture belongs on a plate.  So both are mounted on the nearest
     bulkhead, backs to it, facing into the room -- and the POINT each one
     marks is moved to FIX_STAND in front of its own face, which is the
     only place a player can read the thing they are standing at.

     They share one plate and are separated along it by their own two
     widths, because two fixtures solved onto the same wall independently
     land on top of each other.  Neither is ever solid: the player has to
     be able to stand on the boarding point. */
  { float y0;
    float yaw=1.5708f, gap=0;
    if(deckAt(capX,capZ,&y0)&&wallNear(capX,capZ,y0,&yaw,&gap)){
      float fx=sinf(yaw), fz=cosf(yaw);            /* out of the plate */
      float y;
      /* the hatch: its back on the plate, the boarding point in front */
      float hd=propDepth(PROP_CAPSULE)*0.5f+FIX_BACK;
      float hx=capX-fx*(gap-hd), hz=capZ-fz*(gap-hd);
      if(deckAt(hx,hz,&y)) propAddFixed(PROP_CAPSULE,hx,y,hz,yaw);
      capX=hx+fx*FIX_STAND; capZ=hz+fz*FIX_STAND;
      /* the six slots: the same plate, and far enough along it that the
         two do not share a footprint -- on whichever side the console
         point was already on, so the room's traffic is not turned round */
      float ax=-fz, az=fx;
      float sep=(propWidth(PROP_CAPSULE)+propWidth(PROP_PACK_SLOTS))*0.5f+0.70f;
      float sgn=((consoleX-hx)*ax+(consoleZ-hz)*az)<0.f?-1.f:1.f;
      float sd=propDepth(PROP_PACK_SLOTS)*0.5f+FIX_BACK;
      float sx=hx+fx*(sd-hd)+ax*sgn*sep, sz=hz+fz*(sd-hd)+az*sgn*sep;
      if(deckAt(sx,sz,&y)){
        propAddFixed(PROP_PACK_SLOTS,sx,y,sz,yaw);
        consoleX=sx+fx*FIX_STAND; consoleZ=sz+fz*FIX_STAND;
      }
      LP[P_CAPSULE][0]=capX;     LP[P_CAPSULE][1]=capZ;
      LP[P_CONSOLE][0]=consoleX; LP[P_CONSOLE][1]=consoleZ;
      platLog("point  capsule  now %.2f %.2f   console now %.2f %.2f",
              capX,capZ,consoleX,consoleZ);
      /* AND THE OBJECTIVES FOLLOW THE THING THEY NAME.  snapQuests ran on
         the packed coordinates, which were the old middle-of-the-room
         ones; a ring that stays there while the fixture moves is the same
         bug the fixture had, wearing the marker's hat. */
      questAim("enter_R-LBY first",capX,capZ);     /* REACH THE GALLERY */
      questAim("flag_final_defense",capX,capZ);    /* HOLD UNTIL YOU BOARD */
      questAim("pickup_PACK-1",consoleX,consoleZ); /* RETURN TO THE CONSOLE */
      questAim("pickup_PACK-2",consoleX,consoleZ);
      questAim("pickup_PACK-3",consoleX,consoleZ);
    } else {
      float y;                                     /* no plate: as before */
      if(deckAt(consoleX+0.95f,consoleZ,&y))
        propAddFixed(PROP_PACK_SLOTS,consoleX+0.95f,y,consoleZ,-1.5708f);
      if(deckAt(capX,capZ,&y))
        propAddFixed(PROP_CAPSULE,capX,y,capZ,1.5708f);
    } }
  vmwDisarmAll();
  gameSetHP(10.f);                     /* you start at ten */
  lumiInit();
  doorApply();

  /* ---- S0.  You are on the floor of the crew quarters, and the first
     thing in the ship that moves is not hostile. */
  /* You are on your back with your head toward the bow, which is where
     she comes from -- the first thing that moves in this ship has to
     arrive INSIDE the frame or the opening is a shot of a ceiling. */
  if(!freeCam){ gameTeleport(LP[P_START][0],0.f,LP[P_START][1],3.05f);
                gamePitch(0.26f); }
  else { F.introDone=1; sc=9; lockLook=0; lockMove=0; camY=1.f; camRoll=0;
         for(int i=0;i<SL_N;i++) said[i]=1; }
  lumiAt(LP[P_LUMI_A][0],1.55f,LP[P_LUMI_A][1]);
  lumiPanelAt(LP[P_LUMI_A][0],1.55f,LP[P_LUMI_A][1]);
  lumiDisplay(LD_EYES_IDLE);
  /* AND THE OPENING'S POSE MUST NOT FOLLOW -cam OUT OF THE ROOM.  These
     four lines used to run unguarded, so every instrument shot was taken
     from an eye 0.58 m off the deck with a 0.31 rad roll on it -- the
     pose of a body lying on the floor of the crew quarters, applied to a
     camera the caller had put somewhere else on purpose.  Every -shot in
     the tree was framed against that and none of them said so. */
  if(!freeCam){
    sc=0; scT=0; camY=0.35f; camRoll=0.314f; lockLook=1; lockMove=1;
  }
  int hk=itemBy("HK-00"); if(hk>=0) IT[hk].live=1;
  platLog("story  %d volumes, %d doors, %d items, %d lines, %d encounters",
          SV_N,SD_N,SI_N,SL_N,SE_N);
}

/* ---- THE ROUTES, WALKED, WITH THE FURNITURE IN --------------------
   props.c's placer is entirely local -- this spot has a deck, a wall
   behind it, a metre and a quarter in front, no leaf within three -- and
   every one of those can be true of a crate that still sits in the one
   gap the route goes through.  A room is not a corridor and the placer
   cannot see a route.  This one can: it solves the field to each
   objective and each landmark in turn, walks it from the player's own
   start, and deletes anything standing on the walk.  Two hundred props
   were placed; the handful this takes out will not be missed, and the
   alternative is a run that ends at a locker. */
void storyClearRoutes(void){
  float sx=SP_POS[P_START*2], sz=SP_POS[P_START*2+1];
  int gone=0;
  for(int q=0;q<SQ_N+3;q++){
    float gx,gz;
    if(q<SQ_N){ gx=QT[q*2]; gz=QT[q*2+1]; }
    else if(q==SQ_N)   { gx=dockX;    gz=dockZ; }
    else if(q==SQ_N+1) { gx=consoleX; gz=consoleZ; }
    else               { gx=capX;     gz=capZ; }
    zomGoal(gx,0.f,gz);
    float x=sx, z=sz, y=0, dir[2];
    for(int step=0;step<900;step++){
      if(!deckAt(x,z,&y)) break;
      gone+=propClearAt(x,y,z,0.46f);
      if(hypot2(gx-x,gz-z)<1.0f) break;
      if(!zomStepTo(x,y+0.2f,z,dir)) break;
      x+=dir[0]*0.45f; z+=dir[1]*0.45f;
    }
    /* and the objective itself has to be walkable up to and stood on */
    gone+=propClearAt(gx,y,gz,1.30f);
  }
  /* every pickup gets the same clearance, because reaching one is a route
     even when it is not an objective */
  for(int i=0;i<SI_N;i++) gone+=propClearAt(IT[i].x,IT[i].y,IT[i].z,1.05f);
  platLog("props  %d removed from the story's routes",gone);
}

/* ---- -probe x0,y0,x1,y1 (PLAN units) ---------------------------------
   An ASCII map of what is solid at knee height over a rectangle of the deck
   plan, printed and then exit.  Every story coordinate in story_data.h is a
   point on a drawing, and the drawing is not the ship: the plan says the
   escape gallery is 58 units wide and what got built is narrower, which is
   how the final defence ended up spawning four animals inside a bulkhead.
   This is the cheapest possible answer to "is that point standable".  */
void storyProbe(const char *arg){
  float a[6]={140,410,210,650,-1,-1};
  int na=sscanf(arg,"%f,%f,%f,%f,%f,%f",a,a+1,a+2,a+3,a+4,a+5);
  int reach = na>=6;
  if(reach){
    /* the second question, and the one that actually matters: not "is this
       cell open" but "can a body get from this cell to there".  The lattice
       already knows; this only prints what it knows. */
    zomGoal((a[4]-340.f)*0.145f,0.f,(a[5]-130.f)*0.145f);
    platLog("probe  plan x %.0f..%.0f  y %.0f..%.0f   reachability to %.0f,%.0f"
            "   0-9 = distance band, 'x' = standable but NO ROUTE, ' ' = no deck",
            a[0],a[2],a[1],a[3],a[4],a[5]);
  } else
    platLog("probe  plan x %.0f..%.0f  y %.0f..%.0f   '.' open  '#' solid  "
            "'o' standable  'P' prop",a[0],a[2],a[1],a[3]);
  char line[200];
  for(float py=a[1];py<=a[3];py+=2.f){
    int n=0;
    for(float px=a[0];px<=a[2]&&n<190;px+=2.f){
      float wx=(px-340.f)*0.145f, wz=(py-130.f)*0.145f;
      float fy=floorAt(wx,wz,-99.f);
      char c;
      if(fy<-90.f) c=' ';
      else if(solidAt(wx,fy+0.35f,wz)||solidAt(wx,fy+0.85f,wz)) c='#';
      /* AND WHAT THE FURNITURE TAKES UP.  props.c places 200-odd solids
         that the brush grid has never heard of, and "not in the way" is a
         claim about a corridor, which is a thing this map can actually
         answer.  'P' is deck a player could
         stand on with a prop standing on it. */
      else if(propBoxSolid(wx-0.28f,fy+0.10f,wz-0.28f,
                           wx+0.28f,fy+1.10f,wz+0.28f)) c='P';
      else if(reach){
        float d=zomGoalDist(wx,fy+0.2f,wz);
        c = d<0?'x' : (d>1e8f?'x' : (char)('0'+(int)(d/14.f)%10));
      }
      else c = (fy>-0.6f&&fy<0.6f)?'.':'o';
      line[n++]=c;
    }
    line[n]=0;
    platLog("%4.0f %s",py,line);
  }
}

/* ---- -scene N.  A `goto` console command in one flag: stand the story up at the head of a scene with
   everything that scene assumes already true.  It is the only way to get
   a photograph of the ending without playing to it. -------------------- */
/* what -scene does instead of walking there and pressing E: the item is
   gone from the world AND the objective it was gating has moved on */
static void takeQuiet(const char *id){
  int i=itemBy(id);
  if(i<0) return;
  char b[52];
  IT[i].taken=1; IT[i].live=0;
  _snprintf(b,sizeof b,"pickup_%s",SI_ID[i]);
  int q=questBy(b);
  if(q>=0) quest(q);
}
static void sceneMoveLumi(void);            /* below, beside storyInteract */
void storyScene(int n){
  if(n<=0) return;
  F.introDone=1; sc=9; lockLook=0; lockMove=0; camY=1.f; camRoll=0;
  lumiMode(LM_LEAD); lumiDisplay(LD_VITALS);
  gameSetHP(100.f);
  for(int i=0;i<SL_N;i++) said[i]=1;         /* no backlog of old lines */
  takeQuiet("HK-00");
  /* A GUN HANDED OUT IS A GUN OFF THE DECK.  The weapon pickups gate the
     objective after them now, so a scene that arms you without clearing
     the item leaves the banner asking for something already in your
     hands, with its ring still lit in a room you have left. */
  if(n>=1){ vmwGive(1); takeQuiet("W1"); }
  if(n>=3){ F.galleryBriefed=1; quest(questBy("enter_R-LBY first")); }
  if(n>=4){ vmwGive(2); takeQuiet("W2");
            int q=questBy("enter_R-LBY first");
            gameTeleport(QT[q*2],0.f,QT[q*2+1]-4.f,0.f); }
  if(n>=5){ F.packState[1]=2; F.slot[4]=100; sink(1); }
  if(n>=7){ F.packState[2]=2; F.slot[5]=100; F.shortcutOpen=1; sink(2);
            vmwGive(3); takeQuiet("W3"); }
  if(n>=8){ gameTeleport(LP[P_DUCT_LAND][0],0.f,LP[P_DUCT_LAND][1],0.f); }
  if(n>=9){ F.packState[3]=2; F.pack3Cracked=1; F.slot[6]=51;
            quest(questBy("pickup_PACK-3"));
            gameTeleport(consoleX,0.f,consoleZ+2.f,3.14f); }
  if(n>=10){
    sink(3); F.finalDefense=1; F.timerLaunch=90.f;
    lumiDock(dockX,1.30f,dockZ); lumiMode(LM_OFF); lumiDisplay(LD_OFF);
    for(int i=0;i<SI_N;i++) if(SI_KIND[i]==4){ IT[i].live=1; IT[i].taken=0; }
    for(int e=0;e<SE_N;e++) if(!strcmp(SE_ID[e],"E-12")) encStart(e);
    quest(questBy("pickup_PACK-3"));
  }
  if(n>=11){
    /* THE ENDING, PHOTOGRAPHED.  The capsule has taken you and the credits
       are running, which is the one screen in the game that cannot be
       reached by any instrument short of surviving the ninety seconds.
       Her last three lines are un-said again: the loader marks the whole
       script as played, and those three are the only ones in it that are
       supposed to come AFTER the ending. */
    F.boarded=1; creditT=0; zomKillAll();
    for(int i=0;i<SL_N;i++)
      if(!strncmp(SL_ID[i],"L-047",5)) said[i]=0;
  }
  /* AND THE FIVE THE ENDING IS MADE OF, for -scene 10.  storyScene marks
     the whole script played so an old backlog cannot arrive on top of the
     scene; that also silenced the only lines the last act produces, so
     `-scene 10 -auto` ran the ending with no dialogue in it at all and
     could not have caught the line that was arriving in the wrong place.
     An instrument that cannot fail is not an instrument. */
  if(n>=10)
    for(int i=0;i<SL_N;i++)
      if(!strncmp(SL_ID[i],"L-04",4)&&SL_ID[i][4]>='1') said[i]=0;
  for(int i=0;i<SV_N;i++) volSeen[i]=1;      /* no "first" barrage on arrival */
  sceneMoveLumi();                           /* ... and she comes with you */
  doorApply();
  checkpoint("SCENE");
}

/* ============================================================
   11.  THE FRAME
   ============================================================ */
static void openingStep(float dt);
static void dialogueStep(float dt);
static void directorStep(float dt);
static void itemStep(float dt);
static void doorStep(float dt);
static void finaleStep(float dt);
static void leadStep(float dt);
static void goalStep(float dt);
static void dropStep(float dt);
static void dropTrace(void);
static void beginOpen(int i,const float *dp,const float *p);
static int  dlgBusy(void);
#define DOOR_ASK  4.20f            /* how near the PLAYER has to be to ask */
#define DOOR_STANDOFF 1.10f        /* how far off the leaf she works, metres */
#define DOOR_OPEN_S   1.60f        /* and how long the panel takes once there */
#define DOOR_GIVEUP_S 7.0f         /* the player walked off; let her go */
static float openPX,openPZ,openWait;
/* the point she holds while the opening waits for you -- see openingStep
   case 8, which is the one place a door is asked for before doorStep owns
   them all */
static float waitX,waitZ;

void storyStep(float dt){
  float p[3],eye[3];
  gamePlayerPos(p); gamePlayerEye(eye);
  float hp=gamePlayerHP();

  if(storyTrace){
    /* one line every two seconds: how many are up, how far the nearest one
       has got, and whether the flow field can reach the player at all --
       which is the difference between a wave that is coming and a wave
       that is standing in a wall */
    static float tt=0; tt+=dt;
    if(tt>2.f){ tt=0;
      float b2[4]={0,0,0,-1};
      zomNearest(p[0],p[2],b2);
      float lp[3]; lumiPos(lp);
      platLog("live   %d  nearest %.1f m  field %.1f  hp %.0f  sink %d  t %.0f",
              zomLive(),b2[3],zomFieldAt(b2[0],b2[1],b2[2]),hp,
              F.sinkStage,F.timerLaunch);
      platLog("lumi   mode %d disp %d at %.2f %.2f %.2f   you %.2f %.2f %.2f  q %d",
              lumiModeNow(),lumiDisplayNow(),lp[0],lp[1],lp[2],p[0],p[1],p[2],questOn);
      lumiTrace();
      /* THE SIX BANKS, so a flicker in patches is a thing that can be read
         rather than a thing that has to be watched for.  0 and 1 go out
         with the sink, 2 is the alarm, and 3-5 are the broken fixtures on
         three clocks -- if those three ever print one number for a whole
         run they are not independent and the flicker is one bank wearing
         three names. */
      /* and what the bodies have left lying about -- the last stand's
         whole economy, and the one part of it a screenshot of an empty
         deck cannot show */
      dropTrace();
      platLog("style  bank %.2f %.2f  alarm %.2f  flick %.2f %.2f %.2f"
              "  last %.2f",
              STY_C[0],STY_C[3],STY_C[6],STY_C[9],STY_C[12],STY_C[15],
              STY_C[18]);
    }
  }
  pump();
  if(!lockMove) volumes(p[0],p[2]);
  zomTarget(p[0],p[1],p[2],1);

  openingStep(dt);
  dialogueStep(dt);
  directorStep(dt);
  itemStep(dt);
  dropStep(dt);
  doorStep(dt);
  finaleStep(dt);
  goalStep(dt);

  /* ---- the pulse.  78-92 at rest, 120-165 in a fight, 170 and
     up under thirty per cent -- and the heartbeat SFX is the same clock,
     so the readout and the sound cannot drift apart. */
  { int fight=zomLive()>0;
    float want = hp<30.f ? 172.f : (fight?126.f+ (100.f-hp)*0.38f : 84.f);
    float k=dt*0.5f; if(k>1)k=1;
    bpm+=(want-bpm)*k;
    heartT-=dt;
    if(heartT<=0){ heartT=60.f/bpm; auHeart(hp<30.f?1.f:(fight?0.6f:0.25f)); } }

  /* ---- her display, when she is not talking ------------------------ */
  if(dlgCur<0&&dlgBark<0&&restoreT<=0&&lumiModeNow()!=LM_OFF
     &&lumiDisplayNow()!=LD_OPEN&&lumiDisplayNow()!=LD_COUNT){
    /* under thirty per cent the vitals give way to a warning every other
       two seconds, which is how you learn to read her */
    if(hp<30.f && fmodf((float)platTime(),4.f)<2.f) lumiDisplay(LD_WARN);
    else lumiDisplay(LD_VITALS);
  }
  lumiSetVitals(hp/100.f,bpm);

  int combat=inFight();
  leadStep(dt);
  lumiStep(dt,eye,gamePlayerYaw(),gamePlayerPitch(),combat&&!lockMove);

  /* ---- the ship, coming apart -------------------------------------- */
  if(F.sinkStage>=1){
    alarmT-=dt;
    if(alarmT<=0){ alarmT=F.sinkStage>=3?1.6f:2.6f; auAlarmHit(0.6f+F.sinkStage*0.16f); }
  }
  groanT-=dt;
  if(groanT<=0){ groanT=12.f+rnd()*28.f; auHullGroan(); }
  { float want=F.sinkStage?1.f:0.f, k=dt*0.4f; if(k>1)k=1;
    sinkMix+=(want-sinkMix)*k; }
  { float k=dt*1.1f; if(k>1)k=1;
    if(!lockMove) camRoll+=(sinkRoll-camRoll)*k; }
  damageT-=dt*1.4f; if(damageT<0) damageT=0;

  /* ---- dying ------------------------------------------------------- */
  if(hp<=0&&!F.dead){
    F.dead=1;
    strcpy(failMsg,"RESTORING");
  }
  if(F.dead){
    scT+=dt;
    if(scT>2.0f){ scT=0; restore(); }
  }
  pump();
}

/* ---- S0 ------------------------------------------------------------- */
/* how long she will hold the door waiting for a line that has not come.
   L-049 cannot fail to arrive -- reaching D-01 is further than the two
   metres that fire L-008 -- but the opening is the one stretch of this
   game with no way out of itself, so it gets a giveup like everything
   else here does. */
#define HINT_WAIT 16.0f
static void openingStep(float dt){
  if(F.introDone) return;
  scT+=dt;
  switch(sc){
  case 0:                          /* she comes down the room and stops */
    if(scT>1.2f){ lumiPanelAt(LP[P_LUMI_B][0],1.50f,LP[P_LUMI_B][1]); sc=1; scT=0; }
    break;
  case 1:
    if(scT>1.0f){ lumiPanelAt(LP[P_LUMI_C][0],1.15f,LP[P_LUMI_C][1]+0.62f);
                  sc=2; scT=0; }
    break;
  case 2:                          /* the opening sequence, exactly:
                                      idle, then two seconds of a pulse she
                                      has just found, then idle, then talk */
    if(scT>0.9f&&lumiDisplayNow()==LD_EYES_IDLE) lumiDisplay(LD_HEARTBEAT);
    if(scT>2.9f){ lumiDisplay(LD_EYES_IDLE); storyEvent("intro_recognize");
                  sc=3; scT=0; }
    break;
  case 3:                          /* L-001..L-003 chain */
    if(dlgCur<0&&dlgQN==0&&said[2]){ lockLook=0; storyEvent("intro_controls_unlocked");
                                     sc=4; scT=0; }
    break;
  case 4:                          /* L-004, then get up */
    if(dlgCur<0&&dlgQN==0){ sc=5; scT=0; }
    break;
  case 5: {                        /* two and a half seconds to your feet */
    standT+=dt/2.5f;
    float u=standT>1?1:standT;
    camY=0.35f+(1.00f-0.35f)*u*u*(3-2*u);
    camRoll=0.314f*(1.f-u);
    if(standT>=1){ camY=1.f; camRoll=0; lockMove=0;
                   storyEvent("intro_standup_done"); sc=6; scT=0; }
    break; }
  case 6:
    if(scT>0.6f){ lumiMode(LM_HUD); storyEvent("lumi_hud_attached"); sc=7; scT=0; }
    break;
  case 7: {                        /* the kit, then the door */
    float p[3]; gamePlayerPos(p);
    /* ONCE, not every frame: this is a distance FROM the bunk, not a
       distance travelled, and adding it up made the whole ship shout the
       same line sixty times a second */
    static int moved=0;
    if(!moved&&hypot2(p[0]-LP[P_START][0],p[2]-LP[P_START][1])>2.0f){
      moved=1; storyEvent("intro_move_start");
    }
    if(gamePlayerHP()>50.f&&scT>1.0f){ sc=8; scT=0; }
    break; }
  case 8: {                        /* SHE WAITS AT THE DOOR.

       She used to fly to the panel the frame the medkit was picked up,
       from wherever the player happened to be standing, and leave through
       it.  The fix for that was to make her wait for the player to arrive
       -- but waiting was all it was: she stayed in LM_HUD and leadStep,
       which runs every frame of the opening too, took one look at the
       first objective, found it on the far side of a shut D-01, flew at
       it, pinned on the leaf and after PIN_GIVE went THROUGH it -- so she
       passed through the wall and left without opening the door -- and
       then a seven-second DOOR_GIVEUP later she had abandoned the errand
       as well, so the
       first door of the game never opened at all.

       So the opening puts her ON the leaf, on YOUR side of it, and holds
       her there.  LM_PANEL is what does the holding: leadStep returns at
       its first line for any mode that is not LEAD or HUD, which is the
       same latch doorStep already relies on. */
    if(dIx[0]<0){ sc=9; scT=0; break; }
    float dp[2]; adPos(dIx[0],dp);
    float pp[3]; gamePlayerPos(pp);
    if(openDoor<0){
      /* recomputed while she waits, so the standoff is still YOUR side of
         the leaf after you have walked round it */
      float wdx=pp[0]-dp[0], wdz=pp[2]-dp[1];
      float wd=hypot2(wdx,wdz);
      if(wd<0.5f){ wdx=0.f; wdz=-1.f; wd=1.f; }
      float wx=dp[0]+wdx/wd*DOOR_STANDOFF, wz=dp[1]+wdz/wd*DOOR_STANDOFF;
      if(lumiModeNow()!=LM_PANEL||hypot2(wx-waitX,wz-waitZ)>0.60f){
        waitX=wx; waitZ=wz;
        lumiPanelAt(wx,1.35f,wz);
        lumiDisplay(LD_VITALS);
      }
      /* AND THE TORCH GETS NAMED BEFORE THE DOOR OPENS.  L-008 fires on
         your first two metres and its END is what produces hint_light ->
         L-048/L-049, the only place in the game that says which key the
         flashlight is on.  Opening D-01 first put that line at your back
         in a corridor, walking. */
      if(!saidId("L-049")&&scT<HINT_WAIT) break;
      if(dlgBusy()) break;
      if(hypot2(dp[0]-pp[0],dp[1]-pp[2])>DOOR_ASK) break;
      beginOpen(0,dp,pp);
    }
    break; }
  case 9:
    F.introDone=1;
    lumiMode(LM_LEAD);
    break;
  }
}

/* ---- the subtitle player -------------------------------------------- */
#define DLG_HOLD_MAX 12.f          /* the longest a fight may sit on a line */
static float dlgHold=0;
static void dialogueStep(float dt){
  int combat=inFight();
  if(restoreT>0){
    restoreT-=dt;
    if(restoreT<=0&&lumiDisplayNow()!=LD_OPEN) lumiDisplay(LD_VITALS);
  }
  if(dlgCur>=0||dlgBark>=0){
    const char *tx=dlgText();
    dlgT+=dt;
    /* one beep a character, and the last one falls */
    dlgBeep-=dt;
    if(dlgBeep<=0&&dlgT<dlgLen-SL_TAIL){
      dlgBeep=1.f/SL_CPS;
      float u=dlgT/(dlgLen-SL_TAIL);
      int warn = dlgCur>=0 && (SL_D0[dlgCur]==LD_WARN||SL_D1[dlgCur]==LD_WARN);
      auBeep(0.94f+0.12f*sinf(dlgT*9.f)+(u>0.92f?-0.18f:0.f),warn);
    }
    lumiSetTalk(0.5f+0.5f*sinf(dlgT*17.f));
    /* "A>B" -- she starts on one face and moves to the other halfway */
    if(dlgCur>=0&&SL_D0[dlgCur]!=SL_D1[dlgCur]&&dlgT>dlgLen*0.45f)
      lumiDisplay(SL_D1[dlgCur]);
    if(dlgT>=dlgLen){
      if(dlgCur>=0){
        char b[52]; _snprintf(b,sizeof b,"dialogue_end:%s",SL_ID[dlgCur]);
        storyEvent(b);
        /* a chain line follows immediately, with no trigger of its own */
        int nx=dlgCur+1;
        if(nx<SL_N&&!SL_TRIG[nx][0]&&!said[nx]) queueLine(nx);
      }
      dlgCur=-1; dlgBark=-1; restoreT=0.5f; silence=0;
      lumiSetTalk(0);
    }
    (void)tx;
    return;
  }
  /* nothing playing: take the next line off the queue, unless the room is
     busy.  Every line is held during a fight except the five the
     final defence needs, because you cannot read a subtitle and aim. */
  /* AND THE HOLD IS NOT ALLOWED TO BE FOREVER.  Holding a line during a
     fight is right; holding it UNTIL THE FIGHT ENDS is a bet that the
     fight ends, and an animal that has got itself wedged never ends one.
     Two of E-01's sat at 10.7 m from the player for seven hundred seconds
     -- the distance never moved a centimetre -- and took the gallery
     briefing with them; the briefing is what sets gallery_briefed, which
     is what unlocks D-22, so the run stopped on a line that was never
     going to play.  After twelve seconds the queue moves anyway: a
     subtitle over a fight is worse than a subtitle, and better than a
     story that has stopped. */
  if(dlgQN>0){
    int i=dlgQ[0];
    /* AND IN THE LAST NINETY SECONDS, HOLDING IS NOT ENOUGH.  DLG_HOLD_MAX
       exists so a wedged animal cannot stop the story; in the finale it is
       the wrong end of the trade -- the fight NEVER stops, so every line
       still in the queue arrives twelve seconds later on top of the five
       that are counting the launch down.  What the player got was the
       countdown interrupted by a line about the mess hall.  The five lines
       the ending needs are the five exceptions; everything else is a beat
       whose moment has gone, and a beat whose moment has gone is dropped,
       not deferred. */
    if(F.finalDefense&&!F.boarded&&!SL_EXC[i]){
      said[i]=1;
      for(int k=1;k<dlgQN;k++) dlgQ[k-1]=dlgQ[k];
      dlgQN--; dlgHold=0;
      return;
    }
    int held = combat && !SL_EXC[i];
    if(held) dlgHold+=dt; else dlgHold=0;
    if(!held||dlgHold>DLG_HOLD_MAX){
      for(int k=1;k<dlgQN;k++) dlgQ[k-1]=dlgQ[k];
      dlgQN--; dlgHold=0;
      startLine(i);
      return;
    }
  } else dlgHold=0;
  silence+=dt;
  if(silence>SL_IDLE&&!combat&&F.introDone&&!F.finalDefense){ silence=0; bark(); }
}

/* ---- the waves ------------------------------------------------------ */
/* THE ADULT IS SOMETHING YOU KILLED, NOT SOMETHING THE ROOM ANNOUNCES.
   L-027 -- "대형 개체입니다.  성체로 추정됩니다" -- hung off clear_E-07,
   which is the mess hall's whole encounter: three cocoons and one brooder
   that comes through the west wall three seconds later.  The line
   therefore played over an empty room, in the present tense, about a
   thing that was already dead -- and if the player left before the
   brooder arrived it played about a thing they had never seen.  It is a
   brooder dying that it is about, so a brooder dying is what fires it,
   wherever in the ship that happens, and the text is past tense now. */
static int broodKilled;
static void adultStep(void){
  int n=zomKillsOf(Z_BROODER);
  /* ... AND NOT DURING THE LAST STAND.  E-12 sends five brooders at a
     player who is reading a countdown, so a player who had not met one
     before the gallery got "방금 그거, 성체였습니다" on top of L-044.
     It is a beat about the first one, and by S10 there is no first one
     left to be about: the kills still count, the line simply does not
     get to interrupt the ending. */
  if(n>broodKilled){
    broodKilled=n;
    if(!F.finalDefense) storyEvent("killed_adult");
  }
}

/* what the last stand refuses to fall below, and how late it stops
   re-arming itself -- see the wave block below for both */
#define FINAL_FLOOR   14          /* the fewest E-12 leaves standing */
#define FINAL_FLOOR_CD 3.0f       /* ... and how often it may say so */
#define FINAL_WRAP_AT 8.0f        /* it stops re-arming itself this late */

/* THE FLOOR NEEDS A CLOCK, NOT A HEADCOUNT OF THE QUEUE.  Two wrong
   answers were measured before this one.  Reading zomLiveIn() alone
   cascades: every row of E-12 spends its lead time in PEND before it is
   anything at all, so the floor saw an empty gallery, pulled the next wave
   in, and did it again on the very next frame -- all thirteen waves queued
   inside one second, nine schedule wraps, and the live count pinned at the
   cap from t=0, which is not pressure, it is a fight with no shape.
   Adding pendIn() to the test fixes the cascade and gives the pacing
   straight back to the schedule, which is what was too thin in the first
   place: live mean fell to 9.9 with a max of 24.  So the floor reads the
   LIVING -- that is what "the hall is thin" means -- and a cooldown, not
   the queue, is what stops it emptying the schedule in one frame. */
static float floorCd=0.f;
static int pendIn(int e){
  int n=0;
  for(int i=0;i<MAXPEND;i++) if(PEND[i].used&&PEND[i].enc==e) n++;
  return n;
}
static void directorStep(float dt){
  adultStep();
  if(floorCd>0.f) floorCd-=dt;
  if(F.boarded) return;              /* the credits are not a fight */
  for(int i=0;i<MAXPEND;i++){
    if(!PEND[i].used) continue;
    PEND[i].t-=dt;
    if(PEND[i].t>0) continue;
    PEND[i].used=0;
    /* THE CAP IS A DIFFICULTY DIAL, NOT A BUDGET.  Twenty is what the
       walk through the ship wants -- a corridor with twenty animals in it
       is already more than a corridor can hold.  The last stand is the one
       fight the player is meant to be swamped by, and twenty there means
       E-12's 136 bodies arrive as a queue: measured, the gallery sat empty
       for eight seconds at a stretch with twelve alive and none of them
       within twenty metres, which reads as the gallery being empty.  The
       pool is MAXZ 80 and a corpse clears in eight seconds. */
    /* AND IT COUNTS THE LIVING.  zomCount() is every USED slot and a corpse
       holds its slot for eight seconds (zom.c, deadT>8.f), so a player
       killing five a second spent forty of the forty-four on dead bodies
       and the director stopped sending any -- the fight got QUIETER the
       better you played, which is the wrong way round for the one fight in
       the game that is about being swamped.  zomLive() is the number this
       cap was always meant to be; the pool is MAXZ 80 and zomSpawn gives
       the oldest corpse away when it runs out. */
    /* AND THE CORRIDOR KEEPS THE OLD COUNT.  Every fight in the ship was
       tuned against zomCount(), corpses included, so switching the whole
       game to the living number would be a difficulty change everywhere.
       The last stand is the one place the bug was doing visible harm. */
    int cap  = F.finalDefense ? 44 : 20;
    int here = F.finalDefense ? zomLive() : zomCount();
    if(here>=cap) { PEND[i].used=1; PEND[i].t=0.5f; continue; }
    float x=PEND[i].x, z=PEND[i].z;
    float y=0;
    if(!placeSpawn(&x,&z,&y)){
      if(storyTrace) platLog("spawn  NO SPOT  M%d near %.1f %.1f",
                             PEND[i].type+1,PEND[i].x,PEND[i].z);
      PEND[i].used=0; continue;
    }
    int meth=PEND[i].method;
    /* a hatch drop only starts up there if up there is empty */
    if((meth==M_VENT||meth==M_CEILING)&&!solidAt(x,y+2.6f,z)) y+=2.6f;
    int k=zomSpawn(PEND[i].type,x,y,z,-10,PEND[i].enc);
    if(storyTrace) platLog("spawn  M%d at %.1f %.1f %.1f  (enc %s)",
                           PEND[i].type+1,x,y,z,
                           PEND[i].enc>=0?SE_ID[PEND[i].enc]:"-");
    if(k>=0){
      if(meth==M_CEILING) zomPerch(k);
      sfxCreature(1,x,y,z,zomTypeSize(PEND[i].type));
      for(int q=0;q<8;q++){
        float a=rnd()*6.28f, sp=0.8f+rnd()*2.2f;
        fxEmit(x,y+0.4f,z,cosf(a)*sp,rnd()*1.6f,sinf(a)*sp,
               0.05f+rnd()*0.07f,0.5f,PK_SMOKE,0.26f,0.24f,0.22f,0.5f,2,2,0,0);
      }
    }
  }
  for(int e=0;e<SE_N;e++){
    if(!EN[e].fired||EN[e].done) continue;
    EN[e].t+=dt;
    int w0=SE_W0[e], wn=SE_WN[e];
    if(EN[e].wave<wn){
      int w=w0+EN[e].wave;
      int ready = EN[e].t>=SW_T[w];
      if(!ready&&SW_ORLE[w]>=0&&zomLiveIn(e)<=SW_ORLE[w]) ready=1;
      /* THE LAST STAND HAS A FLOOR OF ITS OWN.  Every other encounter in
         the ship is paced by its clock and arriving early is a fault there.
         E-12 is the one fight the player is meant to be swamped by, and its
         waves are 6-8 s apart: clear fourteen in four seconds and the
         gallery is empty for the other four.  Below FINAL_FLOOR the next
         wave stops waiting for its cue. */
      int floored=0;
      if(!ready&&F.finalDefense&&!F.boarded&&floorCd<=0.f
         &&zomLiveIn(e)<=FINAL_FLOOR){ ready=1; floored=1; }
      if(ready){ fireWave(e,w); EN[e].wave++; if(floored) floorCd=FINAL_FLOOR_CD; }
      continue;
    }
    /* ... AND IT DOES NOT RUN OUT OF SCHEDULE.  Thirteen waves is 136
       bodies over eighty seconds, which a player holding the ring with the
       rotary cannon empties well before the launch clock does -- and the
       ending then plays out quiet, which is the exact opposite of what the
       ninety seconds are for.  While the clock is still running the
       schedule WRAPS to its own second half, where the heavy waves are. */
    if(F.finalDefense&&!F.boarded&&F.timerLaunch>FINAL_WRAP_AT&&wn>1){
      EN[e].wave = wn/2;
      EN[e].t    = SW_T[w0+wn/2];
      if(storyTrace) platLog("wave   %s schedule wrapped to %d",SE_ID[e],wn/2);
      continue;
    }
    /* every wave is out and nothing of ours is standing */
    if(!pendIn(e)&&zomLiveIn(e)==0) encClear(e);
  }
}

/* ---- items ---------------------------------------------------------- */
static float itemDist(int i,const float *p){
  float dx=IT[i].x-p[0], dz=IT[i].z-p[2], dy=IT[i].y-p[1];
  return hypot3(dx,dy,dz);
}
static int nearItem=-1;
static void itemStep(float dt){
  float p[3]; gamePlayerPos(p);
  nearItem=-1;
  float best=2.4f;
  for(int i=0;i<SI_N;i++){
    IT[i].bob+=dt;
    if(IT[i].respawn>0){
      IT[i].respawn-=dt;
      if(IT[i].respawn<=0){ IT[i].live=1; IT[i].taken=0; }
    }
    if(!IT[i].live||IT[i].taken) continue;
    float d=itemDist(i,p);
    if(d<best){ best=d; nearItem=i; }
    /* "near_X" is its own event: the drone comments on the locker before
       you have it, which is what makes the locker worth walking to */
    if(d<3.2f){
      char b[52]; _snprintf(b,sizeof b,"near_%s",SI_ID[i]);
      static char lastNear[52];
      if(strcmp(lastNear,b)){ strcpy(lastNear,b); storyEvent(b); }
    }
  }
}
/* ---- WHAT A BODY LEAVES BEHIND --------------------------------------
   The written items are a table: twenty-four points on a drawing, each one
   placed once and taken once.  That is the right shape for a ship you walk
   through and the wrong one for a room you hold, where the whole economy is
   what the last ninety seconds hand back -- E-12 alone now sends 136 bodies
   at a player standing on two respawning supply boxes.

   So a kill leaves something, on a counter rather than on a die: every
   DROP_EVERY-th body, plus any body that dies while the player is under
   DROP_HURT per cent, which is the only time a health kit is the thing you
   need.  A counter cannot roll six ammo boxes in a row when you are at
   nine health, and the player can feel the rhythm of it inside one fight.

   They are picked up by WALKING OVER them.  The written items ask for an E
   press because walking past one by accident is worse than stopping for
   it; a box that dropped where the animal fell is the opposite case, and
   an E prompt in the middle of a last stand is a thing to fumble.

   They expire.  Twenty-four slots is four seconds of E-12 at full tilt and
   the pool has to come back; DROP_LIFE is long enough to fight your way to
   one and short enough that the deck does not become a carpet. */
#define MAXDROP    24
#define DROP_EVERY 3
#define DROP_HURT  55.f
#define DROP_LIFE  40.f
#define DROP_R     1.15f
static struct { int used,kind; float x,y,z,t,bob; } DROP[MAXDROP];
static int dropCount;
static void addDrop(int kind,float x,float y,float z){
  int i=-1;
  for(int k=0;k<MAXDROP;k++) if(!DROP[k].used){ i=k; break; }
  if(i<0){                                     /* the oldest gives way */
    float worst=-1.f;
    for(int k=0;k<MAXDROP;k++) if(DROP[k].t>worst){ worst=DROP[k].t; i=k; }
  }
  DROP[i].used=1; DROP[i].kind=kind; DROP[i].t=0; DROP[i].bob=rnd()*6.f;
  DROP[i].x=x; DROP[i].z=z;
  /* ON THE DECK, NOT WHERE THE ANIMAL'S CHEST WAS.  deckNear walks the
     whole column and keeps only a surface with room to stand on it --
     the first surface is not the deck; a downward trace from 1.4 m
     in this ship finds a rib as often as a floor. */
  float dy;
  DROP[i].y = deckNear(x,z,y,&dy) ? dy+0.02f : y;
}
static void dropStep(float dt){
  float p[3]; gamePlayerPos(p);
  float hp=gamePlayerHP();
  float d[3]; int t;
  while((t=zomTakeDeath(d))){
    if(t-1==Z_COCOON) continue;                /* a husk is not a kill */
    dropCount++;
    int want = (dropCount%DROP_EVERY)==0;
    int hurt = hp<DROP_HURT;
    if(!want&&!hurt) continue;
    /* a brooder is worth the walk to it */
    int kind = hurt ? 0 : ((t-1==Z_BROODER||(dropCount%(DROP_EVERY*2))==0)?0:1);
    addDrop(kind,d[0],d[1]+0.2f,d[2]);
  }
  for(int i=0;i<MAXDROP;i++){
    if(!DROP[i].used) continue;
    DROP[i].t+=dt; DROP[i].bob+=dt;
    if(DROP[i].t>DROP_LIFE){ DROP[i].used=0; continue; }
    float dx=DROP[i].x-p[0], dz=DROP[i].z-p[2], dy=DROP[i].y-p[1];
    if(dx*dx+dz*dz>DROP_R*DROP_R||dy<-1.2f||dy>2.2f) continue;
    if(DROP[i].kind==0){
      if(hp>=100.f) continue;                  /* leave it where it is */
      gameSetHP(hp+25.f); hp=gamePlayerHP();
    } else {
      int any=0;
      for(int w=1;w<=3;w++) if(vmwOwns(w)){ vmwAddAmmo(w,w==1?30:(w==2?14:120)); any=1; }
      if(!any) continue;
    }
    DROP[i].used=0;
    /* A DROP IS A THING ON THE DECK, not the drone speaking.  This was
       auBeep(1.35) -- LUMI's own voice, a third up -- so the ship
       confirmed a pickup in the same sound it uses for dialogue. */
    auPickup(DROP[i].kind==0?0:2);
  }
}
static void dropTrace(void){
  int nd=0,nh=0;
  for(int i=0;i<MAXDROP;i++) if(DROP[i].used){ nd++; if(!DROP[i].kind) nh++; }
  platLog("drops  %d on the deck (%d heal / %d ammo)  %d bodies counted",
          nd,nh,nd-nh,dropCount);
}
static void takeItem(int i){
  char b[52];
  IT[i].taken=1; IT[i].live=0;
  /* ... AND THE ITEMS THEMSELVES WERE SILENT.  The drops at least beeped;
     pressing E on a rifle, a medkit or an energy pack made no sound at
     all, so the only confirmation the press had registered was the HUD.
     SI_KIND is auPickup's own argument, so there is nothing to translate. */
  auPickup(SI_KIND[i]);
  switch(SI_KIND[i]){
  case 0:                                    /* a health kit */
    gameSetHP(gamePlayerHP()+SI_AMT[i]);
    break;
  case 1:                                    /* a weapon on its rack */
    vmwGive(SI_AMT[i]);
    break;
  case 2:                                    /* an ammunition cache */
    for(int s=1;s<=3;s++) if(vmwOwns(s)) vmwAddAmmo(s,s==1?36:(s==2?16:90));
    break;
  case 3:                                    /* an energy pack */
    F.packState[SI_AMT[i]]=1; F.carrying=SI_AMT[i];
    break;
  case 4:                                    /* the respawn boxes of S10 */
    gameSetHP(gamePlayerHP()+25);
    for(int s=1;s<=3;s++) if(vmwOwns(s)) vmwAddAmmo(s,s==1?24:(s==2?12:60));
    IT[i].respawn=30.f;
    break;
  }
  _snprintf(b,sizeof b,"pickup_%s",SI_ID[i]);
  storyEvent(b);
}

/* ============================================================
   11b. WHERE SHE IS LEADING, AND THE RING ON THE FLOOR

   TWO INSTRUCTIONS THAT DISAGREE ARE WORSE THAN ONE.  The banner
   names a place, the compass points at it, and Lumi -- who is
   also the health bar -- used to hover three and a half metres
   down the player's own view vector, which is not a direction at
   all: it turns with your head, so "follow Lumi" was an order
   that could not be obeyed and the route stopped there.

   All three now say the same thing.  She flies to a point on the
   route the nav lattice actually solved; the needle points at the
   objective; and the objective has a lit ring standing on it, so
   arriving is something you can SEE happen rather than a volume
   rectangle you may or may not have clipped.  That last one is
   the important one -- a plan rectangle that does not cover the
   floor that got built is a dead run, and the ring is the
   backstop that cannot miss.
   ============================================================ */
#define GOAL_R    1.70f            /* the trigger cylinder, in metres */
static float goalT=0;              /* how long you have stood in it */

/* IS SHE IN THE MIDDLE OF SAYING SOMETHING.  Two systems used to run off
   the same clock and ignore each other: the subtitle player takes about
   four seconds a line and the director takes none, so picking up the
   pistol fired her line and her door and her next objective in the same
   frame -- she was two rooms away with the panel already open by the time
   the sentence the player was reading had finished.  A line in flight or
   waiting in the queue now shortens her leash and stops her opening
   anything, which is the whole of the fix: the game waits for the words,
   rather than the words racing the game. */
static int dlgBusy(void){ return dlgCur>=0||dlgBark>=0||dlgQN>0; }

/* the DESTINATION, which is not storyAutoTarget(): that one diverts to
   whatever item is under your feet, and a marker that moves is not a
   marker. */
static int goalPoint(float *out){
  if(F.finalDefense){ out[0]=capX; out[1]=capZ; return 1; }
  if(F.carrying){ out[0]=consoleX; out[1]=consoleZ; return 1; }
  if(questOn<0) return 0;
  out[0]=QT[questOn*2]; out[1]=QT[questOn*2+1];
  return 1;
}

/* WHEN THE OBJECTIVE IS A THING AND NOT A PLACE.  The ring on the deck
   marks where to walk; if what is standing there is a pickup, she goes and
   hangs in the ring over it rather than coming back to your shoulder the
   moment you are within six metres.  A ring is a place -- a ring with the
   only moving light in the ship hovering in it is an instruction, and it
   is the same instruction the banner and the compass are giving. */
#define HOVER_R 11.0f              /* how near before she leaves off leading */
static int goalItem(const float *g){
  if(F.carrying||F.finalDefense) return -1;
  for(int i=0;i<SI_N;i++){
    if(!IT[i].live||IT[i].taken) continue;
    if(fabsf(IT[i].x-g[0])<0.8f&&fabsf(IT[i].z-g[1])<0.8f) return i;
  }
  return -1;
}

static void leadStep(float dt){
  int m=lumiModeNow();
  if(m!=LM_LEAD&&m!=LM_HUD){ lumiLeadTo(0,0,0,0); return; }
  float p[3]; gamePlayerPos(p);
  float g[2];
  int have=goalPoint(g);
  float away = have? hypot2(g[0]-p[0],g[1]-p[2]) : 0.f;   /* not `far`: windows.h owns that */

  /* she stands over the pickup, and stays there until it is gone */
  if(have&&!lockMove&&!inFight()&&away<HOVER_R){
    int gi=goalItem(g);
    if(gi>=0){
      lumiMode(LM_LEAD);
      lumiLeadTo(IT[gi].x,IT[gi].y+1.30f,IT[gi].z,1);
      return;
    }
  }

  /* SHE LEADS WHEN THERE IS SOMEWHERE TO LEAD TO, and comes to your
     shoulder when there is not: in a fight, on arrival, or while the
     opening still owns your feet.  Hysteresis, because a companion that
     flickers between two stations reads as broken -- but the thresholds
     are 3 and 5 rather than 4.5 and 7, because a goal six metres away
     through a wall is still a goal you cannot find. */
  /* AND THE OPENING OWNS HER UNTIL IT IS OVER.  Before introDone the
     first objective is already set and it is on the far side of a shut
     D-01, so leading at it means flying at a leaf and, 1.2 s later,
     through it.  She still hovers over the medkit -- that branch is
     above this one and returns -- and she still comes to the shoulder;
     what she does not do is set off. */
  int lead;
  if(!have||lockMove||inFight()||!F.introDone) lead=0;
  else if(m==LM_LEAD)                lead = away>3.0f;
  else                               lead = away>5.0f;

  /* AND SHE IS NEVER FURTHER AWAY THAN SHE CAN BE FOLLOWED.

     Leading is a thing done in front of somebody.  Once she is far enough
     back that the player cannot see her she is not a guide, she is a lost
     object -- and the 300 s run found exactly that: pinned against a
     bulkhead in the cargo hold she fell 68 metres behind and stayed there,
     which costs the player the compass, the health readout AND most of the
     light in the game at once.  So: past LEAD_LOST she stops leading and
     comes to the shoulder, which is a target she can always reach because
     it is defined in the camera's frame rather than on the deck.  Past
     LEAD_GONE for LOST_T seconds she is simply put there -- the same
     admission restore() already makes after a death, for the same reason.
     Her flight is not allowed to be the thing that ends a run. */
  #define LEAD_LOST 14.0f
  #define LEAD_GONE 24.0f
  #define LOST_T     3.0f
  { float lp[3]; lumiPos(lp);
    float lag=hypot2(lp[0]-p[0],lp[2]-p[2]);
    static float lostT=0;
    if(lag>LEAD_LOST) lead=0;
    if(lag>LEAD_GONE){
      lostT+=dt;
      if(lostT>LOST_T){
        lostT=0;
        float e[3]; gamePlayerEye(e);
        lumiAt(e[0],e[1]-0.30f,e[2]);
        platLog("lumi   RECOVERED -- %.1f m behind for %.1f s",lag,LOST_T);
      }
    } else lostT=0; }

  lumiMode(lead?LM_LEAD:LM_HUD);
  if(!lead){ lumiLeadTo(0,0,0,0); return; }

  /* HOW FAR AHEAD IS NOT A DISTANCE, IT IS A DECISION.

     A fixed leash of 5.5 m takes her through the far door before the
     player has cleared the near one.  3.5 m has the opposite failure: in
     the lobby, in the gallery, anywhere the route is a choice between five
     doors, three and a half metres is still the middle of the room.  She
     leads and points at nothing, and the player cannot tell which door to
     take.

     Both are the same mistake -- picking a NUMBER for a thing that is not
     a number.  What the player needs to be shown is the next place the
     route stops being obvious: the doorway it takes, or the corner it
     turns.  So she walks the route forward and stops at the first of

       - a door leaf within LEAD_DOOR of the route  -- the whole answer in
         a room with five of them, and a place she can hold rather than
         drift past, since she is a ghost collider and never blocks it;
       - a turn of more than LEAD_TURN off the first leg -- the corner, so
         the player sees her take it rather than finding her gone;
       - LEAD_MAX, so a straight corridor does not carry her out of sight.

     In a passage with no choice in it the first two never fire and she
     rides at LEAD_MAX, which is where a companion should be when there is
     nothing to decide.  zomStepTo already looks one node past the next
     where the line is clear, so this is the animals' own walk run
     forwards from the player. */
  #define LEAD_MIN   2.6f     /* a door nearer than this is one you are in */
  #define LEAD_MAX   9.0f
  #define LEAD_TALK  6.0f     /* while she is talking, stay where she can be
                                 heard -- but keep leading */
  #define LEAD_DOOR  1.40f    /* how near a leaf counts as standing in it */
  #define LEAD_TURN  0.62f    /* ~36 degrees off the first leg is a corner */
  float capD = dlgBusy() ? LEAD_TALK : LEAD_MAX;
  zomGoal(g[0],0.f,g[1]);

  int nL=adCount();
  float x=p[0], z=p[2], dir[2];
  float f0x=0,f0z=0;                       /* the first leg, for the turn */
  float bx=0,bz=0; int haveBest=0;         /* the decision point, if found */
  int steps=0;
  while(steps<14){
    if(!zomStepTo(x,p[1],z,dir)) break;
    if(!steps){ f0x=dir[0]; f0z=dir[1]; }
    x+=dir[0]*0.8f; z+=dir[1]*0.8f;
    steps++;
    float away2=hypot2(x-p[0],z-p[2]);
    if(away2>capD) break;
    if(away2<LEAD_MIN) continue;           /* too near to be information */
    /* a leaf on the route -- the strongest signal there is */
    for(int k=0;k<nL;k++){
      float dp[2]; adPos(k,dp);
      if(hypot2(dp[0]-x,dp[1]-z)<LEAD_DOOR){ bx=x; bz=z; haveBest=1; break; }
    }
    if(haveBest) break;
    /* ... or the corner, one step PAST the turn so she is around it */
    if(f0x*dir[0]+f0z*dir[1] < 1.f-LEAD_TURN){ bx=x; bz=z; haveBest=1; break; }
  }
  if(steps<2){ lumiLeadTo(0,0,0,0); return; }   /* off the lattice: balloon */
  if(!haveBest){ bx=x; bz=z; }
  float dx=bx-p[0], dz=bz-p[2], d=hypot2(dx,dz);
  if(d>capD){ bx=p[0]+dx/d*capD; bz=p[2]+dz/d*capD; }
  float fy; if(!deckNear(bx,bz,p[1],&fy)) fy=p[1];
  lumiLeadTo(bx,fy+1.45f,bz,1);
}

/* ---- the trigger cylinder ------------------------------------------
   Standing in the ring for half a second fires the event the quest was
   waiting for.  It only ever fires the event a MOVEMENT objective was
   waiting for -- a pack you have not picked up is still a pack you have
   not picked up -- so the packer leaves SQ_ARRIVE empty for the rest. */
/* AND IF THE RING IS THE ONLY WAY IN, IT HAD BETTER NOT BE THE ONLY WAY IN.
   Moving a beat off a volume rectangle and onto the ring is the right
   trade -- a rectangle from the plan may not cover the floor that got
   built, and the ring is drawn where the player can see it -- but it
   trades a beat that fires too early for one that can fail to fire at
   all, and this ship has exactly one of those already: the last act,
   which needed DOCK_GIVEUP_S for the same reason.  So a player who is
   demonstrably IN THE ROOM and has not found the cylinder gets the beat
   anyway.  GOAL_NEAR is a room, not a corridor; GOAL_GIVEUP is long
   enough that nobody who is walking to the marker ever sees it. */
#define GOAL_NEAR   12.0f
#define GOAL_GIVEUP 40.0f
static float goalNearT=0;
static int   goalNearQ=-1;
static void goalStep(float dt){
  if(questOn<0||F.boarded||lockMove) return;
  if(!SQ_ARRIVE[questOn][0]||arrived[questOn]){ goalT=0; return; }
  float p[3]; gamePlayerPos(p);
  float g[2]; if(!goalPoint(g)){ goalT=0; return; }
  float away=hypot2(g[0]-p[0],g[1]-p[2]);
  if(questOn!=goalNearQ){ goalNearQ=questOn; goalNearT=0; }
  if(away<GOAL_NEAR&&!inFight()) goalNearT+=dt; else goalNearT=0;
  if(goalNearT>GOAL_GIVEUP){
    arrived[questOn]=1; goalT=0; goalNearT=0;
    platLog("goal   %s given at %.1f m -- %.0f s in the room without the ring",
            SQ_ARRIVE[questOn],away,GOAL_GIVEUP);
    storyEvent(SQ_ARRIVE[questOn]);
    return;
  }
  if(away>GOAL_R){ goalT=0; return; }
  goalT+=dt;
  if(goalT>0.5f){
    arrived[questOn]=1; goalT=0;
    storyEvent(SQ_ARRIVE[questOn]);
  }
}

/* and what it looks like: a dashed ring on the deck with six short light
   columns standing on it.  Amber while it is the thing to walk to, cyan
   for the half second it takes to register that you did. */
static void drawGoalRing(void){
  if(questOn<0||F.boarded||lockMove) return;
  float p[3]; gamePlayerPos(p);
  float g[2]; if(!goalPoint(g)) return;
  float dist=hypot2(g[0]-p[0],g[1]-p[2]);
  if(dist>40.f) return;
  /* THE MARKER HAS TO LAND ON THE FLOOR THAT GOT BUILT.  When the objective
     is a pickup, the item has already resolved its own deck height and the
     ring belongs under it exactly; otherwise the written point is a point
     on a drawing and can miss the deck, and a marker that silently declines
     to draw is worse than one standing half a metre off. */
  int gi=goalItem(g);
  float fy;
  if(gi>=0){ g[0]=IT[gi].x; g[1]=IT[gi].z; fy=IT[gi].y; }
  else {
    fy=-99.f;
    if(!deckNear(g[0],g[1],p[1],&fy)) fy=-99.f;
    for(int r=1;r<4&&fy<-90.f;r++)
      for(int a=0;a<8&&fy<-90.f;a++){
        float ang=a*0.7854f;
        float tx=g[0]+cosf(ang)*r*0.6f, tz=g[1]+sinf(ang)*r*0.6f;
        float ty;
        if(deckNear(tx,tz,p[1],&ty)){ g[0]=tx; g[1]=tz; fy=ty; }
      }
    if(fy<-90.f) return;
  }
  int zi=zoneAt(g[0],g[1]);
  EntFx gx=ENT_PLAIN;
  gx.zone=(float)zi; gx.rim=0.f; gx.ao=1.f; gx.rough=0.30f;
  gx.lit[0]=gx.lit[1]=gx.lit[2]=0.f; gx.ldir[1]=1.f;
  float t=(float)platTime();
  int in = goalT>0.f;
  static const float AMB[3]={1.00f,0.66f,0.16f};
  static const float CYA[3]={0.30f,0.86f,1.00f};
  const float *c = in?CYA:AMB;
  float pulse=0.72f+0.28f*sinf(t*3.1f);
  float A[12],B[12],C[12];
  /* the ring: sixteen tangential dashes lying flat on the deck, every
     other one bright.  It marks a place to stand, so it stays down there. */
  for(int i=0;i<16;i++){
    float a=i*0.39270f+t*0.20f;
    /* 85 mm, not 28: fy is the top of the BRUSH and the plate the player
       sees is a kit part standing on it, so a dash at brush height is
       inside the floor and draws nothing */
    mTrans34(g[0]+cosf(a)*GOAL_R,fy+0.085f,g[1]+sinf(a)*GOAL_R,A);
    mRotY34(-(a+1.5708f),B); mMul34(A,B,C);
    pushEnt(C,0.34f,0.030f,0.055f,c[0],c[1],c[2],
            ((i&1)?2.0f:0.7f)*pulse,&gx,PRIM_BEVEL);
  }
  /* AND FOUR SHORT POSTS, NOT SIX TALL ONES.  1.9 m columns on it read as
     a cage in the middle of the room -- the marker louder than the ship.  Knee height, two
     segments, the upper one a third as bright: it says "here" and then
     gets out of the way of the thing it is standing in front of. */
  for(int i=0;i<4;i++){
    float a=i*1.5708f-t*0.30f;
    float cx=g[0]+cosf(a)*GOAL_R, cz=g[1]+sinf(a)*GOAL_R;
    for(int k=0;k<2;k++){
      float em=(k?0.42f:1.5f)*pulse;
      mTrans34(cx,fy+0.20f+k*0.40f,cz,A);
      pushEnt(A,0.040f,0.38f,0.040f,c[0],c[1],c[2],em,&gx,PRIM_BEVEL);
    }
  }
  fxAddDynLight(g[0],fy+0.7f,g[1],c[0]*0.24f,c[1]*0.24f,c[2]*0.24f,4.2f,0.06f);
}

/* ---- the doors Lumi opens -------------------------------------------
   THE DOOR OPENS BECAUSE SHE ARRIVED.  Sent at the door's own centre --
   which is inside the leaf -- with the gauge started on the same frame,
   five metres out, she darts at the frame, the gauge runs whether she gets
   there or not, and the door comes open somewhere behind her: a cut, not
   a companion doing a job.  So she stops short of
   the leaf ON THE PLAYER'S SIDE, flies there at a speed you can watch,
   and the gauge does not start until she is standing at it. */
static void beginOpen(int i,const float *dp,const float *p){
  float dx=p[0]-dp[0], dz=p[2]-dp[1];
  float d=hypot2(dx,dz);
  if(d<0.5f){ dx=0.f; dz=-1.f; d=1.f; }        /* you are IN the doorway */
  openPX=dp[0]+dx/d*DOOR_STANDOFF;
  openPZ=dp[1]+dz/d*DOOR_STANDOFF;
  lumiPanelAt(openPX,1.35f,openPZ);
  /* the vitals stay up while she crosses -- they come off her face only
     for the OPEN itself, and the OPEN has not started yet */
  openDoor=i; openU=0; openTarget=-1; openWait=0;
}
static void doorStep(float dt){
  float p[3]; gamePlayerPos(p);
  /* SHE IS ON AN ERRAND THE LAST ACT DEPENDS ON.  See finaleStep: a door
     offered during the dock trip diverts her and hands her back in LEAD,
     and the trip is not re-issued by anything the player can do. */
  if(dockMoving) return;
  /* she is on her way to the panel and the gauge has not started */
  if(openDoor>=0&&openTarget<0){
    openWait+=dt;
    if(lumiReadyAt(openPX,openPZ,0.85f)){
      openTarget=DOOR_OPEN_S; openU=0; openWait=0;
      lumiDisplay(LD_OPEN); auBeep(0.55f,0);
    } else if(openWait>DOOR_GIVEUP_S){
      openWait=0; openDoor=-1;
      lumiMode(LM_LEAD); lumiDisplay(LD_VITALS);
    }
    return;
  }
  if(openTarget>0){
    openU+=dt/openTarget;
    lumiSetOpen(openU);
    if(openU>=1){
      char b[52];
      openU=0; openTarget=-1;
      if(openDoor>=0&&dIx[openDoor]>=0) adForce(dIx[openDoor],1);
      /* the leaf stays forced from here on, so this fires once -- and the
         checkpoint hanging off door_opened_D-01 fires once with it */
      if(openDoor>=0&&!dOpened[openDoor]){
        dOpened[openDoor]=1;
        _snprintf(b,sizeof b,"door_opened_%s",SD_ID[openDoor]);
        storyEvent(b);
      }
      openDoor=-1;
      lumiMode(LM_LEAD);
      lumiDisplay(LD_VITALS);
    }
    return;
  }
  if(F.sinkStage>=3||lockMove) return;
  /* THE PLAYER OPENS THE DOOR, THROUGH HER.  She works the panel; the
     question is who decides WHEN.  If it is "whoever gets there first",
     it is always her, because she leads: she reaches the locked leaf, her
     own distance satisfies the second clause, the panel runs, and the
     player arrives at an open door in an empty corridor having done
     nothing.  Repeated 34 times that is not a companion, it is a cutscene
     with walking in it.

     A door waits for YOU.  Reach it -- which is what a checkpoint is
     here, the objective being a place you stand -- and she comes and
     works the panel while you watch.  Then the next one: reach the
     checkpoint, one door opens, follow her.
     And she will not start one over a line she has not finished saying. */
  if(dlgBusy()) return;
  for(int i=0;i<SD_N;i++){
    if(dIx[i]<0) continue;
    if(SD_LOCK[i]!=1) continue;
    if(dOpened[i]||adOpen(dIx[i])) continue;
    float dp[2]; adPos(dIx[i],dp);
    float dx=dp[0]-p[0], dz=dp[1]-p[2];
    if(dx*dx+dz*dz>DOOR_ASK*DOOR_ASK) continue;
    beginOpen(i,dp,p);
    return;
  }
}

/* how near the capsule counts as being at its hatch.  It was sqrt(12) --
   3.46 m -- which is smaller than the goal ring standing on the same
   point, so the marker said "here" over ground the hatch would not
   answer from. */
#define CAP_R  4.60f
#define CAP_AT 25.0f              /* and boarding opens this late */

/* ---- the console, the dock, and the ninety seconds ------------------ */
/* BOARDING IS THE END OF THE LAST STAND, NOT A THING THAT HAPPENS DURING
   IT.  The hatch opens at T-25 and both boarding paths did the same three
   lines -- set the flag, zero the credit clock, kill what was standing --
   and then left the ninety seconds RUNNING underneath the credits.
   Measured, boarding at T-22: the timer ticked on to fire timer_20, which
   queues L-044 ("20... 19... 탑승하세요. 지금"), and that line -- a combat
   exception, so nothing holds it -- played over the black screen at a
   player who was already aboard, in front of L-045.  E-12 also kept
   spawning into an empty gallery for another twenty-two seconds.

   So the hatch shutting stops the clock, the director, the queue and the
   line that was mid-sentence when it shut.  Everything after this point
   is the ending, and the ending has five lines of its own. */
static void boardCapsule(void){
  F.boarded=1; creditT=0;
  zomKillAll();
  for(int i=0;i<MAXPEND;i++) PEND[i].used=0;
  for(int e=0;e<SE_N;e++) if(!strcmp(SE_ID[e],"E-12")) EN[e].done=1;
  memset(DROP,0,sizeof DROP);
  /* whatever she was in the middle of saying, she is not saying it now */
  dlgQN=0; dlgCur=-1; dlgBark=-1; dlgT=0; dlgHold=0; lumiSetTalk(0);
  storyEvent("capsule_hatch_closed");
}
static void finaleStep(float dt){
  float p[3]; gamePlayerPos(p);
  /* the slot console.  Standing at it with a pack in hand charges a slot,
     and the third one stops at 51 per cent. */
  if(F.carrying&&!F.finalDefense){
    float dx=p[0]-consoleX, dz=p[2]-consoleZ;
    if(dx*dx+dz*dz<9.f){
      installT+=dt;
      if(installT>=3.f){ installT=0; installPack(F.carrying); }
    } else installT=0;
  } else installT=0;

  /* she works out what her own core is for, and goes and docks it.

     AND THE WHOLE LAST ACT HANGS OFF HER ARRIVING.  lumi_dock_arrive is
     what fires sink 3, the ninety seconds, E-12 and the capsule; nothing
     else produces it.  It used to be issued ONCE -- one lumiPanelAt, one
     latched `moving` -- with no way back if she did not get there, and
     there are two ways she does not:

       doorStep() takes her.  It only stops itself at sink 3 and the dock
       trip runs at sink 2, so the player walking within DOOR_ASK of any
       unopened panel door sends her to work it, and it hands her back in
       LM_LEAD.  Nothing re-issued the errand.
       She gets caught.  Measured, in the run that found this.

     Either one stops the game at S9 for ever.  So the errand is re-issued
     every frame she is not on it, doorStep is told to leave her alone
     while she is, and past DOCK_GIVEUP_S she is simply put on the dock --
     the same admission restore() and the door's own DOOR_GIVEUP_S make.
     A companion's flight is not allowed to be the end of a run. */
  #define DOCK_GIVEUP_S 25.0f
  if(F.packState[3]==2&&!F.finalDefense&&lumiModeNow()!=LM_DOCKED){
    if(!dockMoving&&dlgCur<0&&dlgQN==0&&saidId("L-038")){
      dockMoving=1; dockT=0.f;
      storyEvent("lumi_dock_move_start");
      lumiPanelAt(dockX,1.30f,dockZ);
    }
    if(dockMoving){
      dockT+=dt;
      if(lumiModeNow()!=LM_PANEL) lumiPanelAt(dockX,1.30f,dockZ);
      int late = dockT>DOCK_GIVEUP_S;
      if(lumiReadyAt(dockX,dockZ,0.6f)||late){
        if(late){
          platLog("story  the dock trip did not land in %.0f s -- placing her",
                  dockT);
          lumiAt(dockX,1.30f,dockZ);
        }
        dockMoving=0;
        storyEvent("lumi_dock_arrive");
      }
    }
  }
  if(!F.finalDefense) return;

  /* ---- S10.  Her light, her readout and her door control are gone at
     the same moment, and that loss IS the difficulty here. */
  if(lumiModeNow()!=LM_OFF&&F.timerLaunch<89.4f){
    lumiMode(LM_OFF); lumiDisplay(LD_OFF);
  }
  /* ... and the clock only runs while there is still something to count
     down TO.  See boardCapsule: past the hatch it is the credits' clock,
     not the launch's. */
  float was=F.timerLaunch;
  if(!F.boarded){
    F.timerLaunch-=dt;
    if(F.timerLaunch<0) F.timerLaunch=0;
    if(was>45.f&&F.timerLaunch<=45.f) storyEvent("timer_45");
    if(was>20.f&&F.timerLaunch<=20.f) storyEvent("timer_20");
  }
  /* SURVIVING THE NINETY SECONDS HAS TO BE WHAT WINNING LOOKS LIKE.
     Boarding was an E press inside 3.5 m of the capsule in the last
     twenty-five seconds, with no prompt anywhere on the overlay saying
     so -- so a player who did the hard part, held the gallery for the
     whole ninety, and was standing in the lit ring when it ran out got
     "THE CAPSULE LEFT WITHOUT YOU".  The defence is the difficulty; the
     door of the thing you are defending is not.  Stand in it when the
     clock reaches zero and it takes you. */
  if(F.timerLaunch<=0&&!F.boarded&&!F.dead){
    float dx=p[0]-capX, dz=p[2]-capZ;
    if(dx*dx+dz*dz<CAP_R*CAP_R) boardCapsule();
    else { F.dead=1; strcpy(failMsg,"THE CAPSULE LEFT WITHOUT YOU"); }
  }
  if(F.boarded){
    creditT+=dt;
    /* the launch, then the card, then her last transmission -- and the
       transmission was being SPOKEN and never shown, because the subtitle
       drew itself only while !F.boarded and the credits are the one
       screen it matters on.  Fourteen seconds was also most of a minute
       of black before anything happened; five and a half is long enough
       for the launch to land and short enough that the player is still
       watching.  The ending is a bookend, and the bookend has to be
       legible. */
    if(creditT>8.f) overT=1;
    if(F.epilogue&&creditT>5.5f&&!saidId("L-047")) storyEvent("epilogue");
    /* AND THEN A WAY BACK TO THE BEGINNING.  Reaching the end of this
       game used to leave the player on a black screen with a Korean line
       on it and no key that did anything -- alt-F4 as an ending. */
    int done = !dlgBusy() && (!F.epilogue || saidId("L-047B"));
    if(creditT>5.0f && (done || creditT>34.f)){
      if(!endMenu){ endMenu=1; platSetLock(0); }
    }
  }
}

/* ============================================================
   12.  WHAT THE PLAYER PRESSES
   ============================================================ */
/* AND SHE GOES WHERE IT SENDS YOU.  -scene teleports the player and left
   her wherever boot had put her, which for -scene 9 is the crew quarters:
   forty metres of ship between her and the dock the act is waiting on, so
   the one flag that names that act could not test it.  Docked and OFF are
   the two states the scene itself chose; everything else follows you. */
static void sceneMoveLumi(void){
  int m=lumiModeNow();
  if(m==LM_DOCKED||m==LM_OFF) return;
  float e[3]; gamePlayerEye(e);
  lumiAt(e[0],e[1]-0.30f,e[2]);
}

void storyInteract(void){
  float p[3]; gamePlayerPos(p);
  /* THE HATCH COMES FIRST WHEN IT IS OPEN.  Two of the respawn boxes
     stand in the gallery within a few metres of the capsule, and items
     were tested before anything else -- so at T-20, with a box at your
     feet, E took the box and the capsule was never offered. */
  if(F.finalDefense&&F.timerLaunch<=CAP_AT&&!F.boarded){
    float dx=p[0]-capX, dz=p[2]-capZ;
    if(dx*dx+dz*dz<CAP_R*CAP_R){ boardCapsule(); return; }
  }
  if(nearItem>=0){ takeItem(nearItem); return; }
  /* the duct.  No long fall: it is the drop
     itself, onto the engine room's roof, and what it does to the core in
     your hands is the turn the last act runs on. */
  { float dx=p[0]-LP[P_DUCT][0], dz=p[2]-LP[P_DUCT][1];
    if(dx*dx+dz*dz<6.25f&&F.carrying==3&&!F.pack3Cracked){
      gameTeleport(LP[P_DUCT_LAND][0],
                   floorAt(LP[P_DUCT_LAND][0],LP[P_DUCT_LAND][1],0.f)+0.2f,
                   LP[P_DUCT_LAND][1],gamePlayerYaw());
      storyEvent("duct_landing");
      return;
    } }
  { float dx=p[0]-LP[P_TERMINAL][0], dz=p[2]-LP[P_TERMINAL][1];
    if(dx*dx+dz*dz<9.f){ storyEvent("use_info_terminal"); return; } }
}

/* ============================================================
   13.  WHAT THE FRAME ASKS
   ============================================================ */
/* what -auto steers by: the quest marker, or the nearest thing worth
   picking up if one is within reach */
int storyAutoTarget(float *out){
  if(nearItem>=0){ out[0]=IT[nearItem].x; out[1]=IT[nearItem].z; return 2; }
  if(F.carrying){ out[0]=consoleX; out[1]=consoleZ; return 3; }
  if(F.finalDefense){ out[0]=capX; out[1]=capZ; return 4; }
  if(questOn<0) return 0;
  out[0]=QT[questOn*2]; out[1]=QT[questOn*2+1];
  return 1;
}
int storyAutoInteract(void){ return nearItem>=0; }

int   storyLockLook(void){ return lockLook; }
int   storyLockMove(void){ return lockMove||F.boarded; }
float storyCamY(void){ return camY; }
float storyCamRoll(void){ return camRoll; }
/* the two pieces of run state that live below storyInit and still have to
   be cleared by it -- see the note there about being called twice */
static void storyResetLate(void){ goalT=0; nearItem=-1;
  memset(DROP,0,sizeof DROP); dropCount=0;
  /* not 0: a restart from the ending re-enters with the run's kills
     still counted unless zomReset ran first, and a phantom edge here
     would play the line over the bunkroom */
  broodKilled=zomKillsOf(Z_BROODER); }
int   storyOver(void){ return overT; }
int   storyEndMenu(void){ return endMenu; }
int   storyEndHit(float x,float y){
  return endMenu && x>endX0 && x<endX1 && y>endY0 && y<endY1;
}
const char *storyFail(void){ return failMsg[0]?failMsg:0; }

/* uphill is slower and downhill is faster once the deck has gone over --
   movement on the tilt, which is what makes the list felt */
float storySpeedScale(float mx,float mz){
  if(!F.sinkStage) return 1.f;
  (void)mz;
  float lean=sinkRoll/0.1309f;
  return 1.f + (mx>0? -0.12f : 0.08f)*lean;
}

/* ---- THE SHIP LOSING ITS LIGHTING ----------------------------------
   "The whole ship goes to red emergency lighting" done as a post-process
   grade -- luminance kept, hue driven to red, the entire frame -- reads
   as a filter over a photograph.  A hull
   with three of six cores gone does not turn red, it goes DARK, a bank
   at a time, and the fixtures that were already failing before any of
   this started keep failing louder.

   So bake.c splits the lamps into four switchable banks and this decides
   what the four are worth, every frame:

     0  out at sink 1        -- the first thing the player notices is
                                that a corridor they walked down lit is
                                not lit any more
     1  out at sink 2
     2  white until sink 2, then a red blink -- the ONE red thing, and it
                                is a light in a room rather than a colour
                                over the screen
     3  a stutter, from the first frame of the game, because a derelict
                                has broken fixtures in it before the
                                player arrives

   Banks 0 and 1 fade over about two seconds rather than snapping: a
   third of a corridor's light vanishing between two frames reads as a
   rendering fault, and the same third dying over two seconds reads as a
   breaker letting go. */
static float styLev[STY_N]={1.f,1.f,1.f,1.f,1.f,1.f,1.f};
static const float STY_RED[3]={1.42f,0.15f,0.10f};

/* mostly on, with short dropouts on a hashed schedule -- a tube striking
   and failing, not a sine wave */
static float stutter(float t,int salt,float deep,float rate){
  float u=t*rate;
  int i=(int)u; float f=u-(float)i;
  float h=ihash(i*13+salt,7,29);
  if(h>=0.30f) return 1.f;                 /* most beats it simply burns */
  /* A FAILING TUBE RESTRIKES; IT DOES NOT SWITCH.  What was here was one
     dropout per beat with a fixed shape -- a square wave, and a square
     wave in a light reads as the renderer stuttering rather than as the
     fitting.  A tube that has lost a cathode drops out and then the
     ballast tries again three to six times inside the same beat, each try
     its own length, and the one that holds ends the beat lit.  Two things
     come out of that and both are what the eye actually recognises: the
     rhythm inside a dropout is irregular, and a tube that has just struck
     is BRIGHTER than one that has been burning, because the gas is cold
     and the ballast is still pushing.  That overshoot is the flash. */
  if(h<0.055f) return 1.f-deep;            /* now and then it just stays out */
  float g=ihash(i*13+salt,11,5);
  int n=3+(int)(g*4.f);                    /* three to six tries */
  float s=f*(float)n;
  int k=(int)s;
  int on=ihash(i*57+salt+k*3,3,19)<0.42f;
  if(!on) return 1.f-deep;
  float lvl=1.f;
  if(k>0 && s-(float)k<0.25f) lvl=1.24f;   /* the strike, cold and hard */
  return lvl;
}

static void lampStyles(float t){
  int st=F.sinkStage;
  /* its own clock: storyFrame runs from the render side and is not handed
     a dt, and the ease below is the only thing here that needs one */
  static float last=-1.f;
  float dt = last<0.f ? 0.016f : t-last;
  last=t;
  if(dt<0.f||dt>0.25f) dt=0.016f;
  float want[STY_N];
  want[0] = st>=1 ? 0.f : 1.f;
  want[1] = st>=2 ? 0.f : 1.f;
  want[2] = 1.f;
  want[3] = want[4] = want[5] = 1.f;    /* a broken tube still has power */
  /* and the last breaker.  Not st>=3 alone: the credits run at sink 3
     too, and the launch is the one moment the gallery may be lit. */
  want[6] = (F.finalDefense&&!F.boarded) ? 0.f : 1.f;
  float k=dt*0.55f; if(k>1)k=1;
  for(int g=0;g<STY_N;g++) styLev[g]+=(want[g]-styLev[g])*k;

  for(int g=0;g<STY_N;g++)
    for(int c=0;c<3;c++) STY_C[g*3+c]=styLev[g];

  /* the alarm bank.  Before stage two it is an ordinary lamp; after it,
     it is the only red in the ship and it is a real one. */
  if(st>=2){
    float period = st>=3 ? 0.95f : 1.75f;
    float on     = st>=3 ? 0.34f : 0.62f;
    float b = fmodf(t,period)<on ? 1.f : 0.06f;
    /* AND IN THE LAST STAND IT IS NOT A ROTATING BEACON ANY MORE.  A
       beacon on a period you can count is something you stop seeing after
       ten seconds, and the ninety seconds are the one stretch of this game
       that is supposed to be impossible to read calmly.  Same fixtures,
       same colour: a fast double-strike on a hashed schedule, so the room
       goes black between the pairs and no two beats are the same length.
       0.55 s means roughly one and a half of them a second, which is the
       rate a real alarm circuit under load actually runs at. */
    if(F.finalDefense&&!F.boarded){
      float u=t*(1.f/0.55f);
      int i=(int)u; float f=u-(float)i;
      float g=ihash(i*29+5,13,3);
      float w=0.20f+g*0.16f;                   /* the strike, and its echo */
      b = (f<w||(f>w*1.9f&&f<w*2.7f)) ? 1.30f : 0.02f;
    }
    for(int c=0;c<3;c++) STY_C[6+c]=STY_RED[c]*b;
  }
  /* AND THE ONES THAT WERE ALREADY BROKEN, ON THREE CLOCKS.  One bank
     meant every broken fixture in the ship struck and failed on the same
     frame, forty metres apart -- which no two tubes have ever done, and
     which reads as the renderer stuttering rather than as the ship.  Three
     salts and three rates, so a corridor behind you goes while the one
     you are in holds, and neither of them is on your clock. */
  { float deep = st>=2 ? 0.95f : (st>=1 ? 0.90f : 0.82f);
    static const int   SALT[3]={3,17,41};
    static const float RATE[3]={3.10f,2.15f,4.35f};
    for(int g=3;g<6;g++){                /* 3,4,5 -- SIX is the breaker */
      float sv=stutter(t,SALT[g-3],deep,RATE[g-3]);
      for(int c=0;c<3;c++) STY_C[g*3+c]=styLev[g]*sv;
    } }
}

void storyFrame(void *fs){
  FrameState *F2=(FrameState*)fs;
  lumiLight(F2);
  lampStyles(F2->time);
  /* THE SHIP GETS DARKER, NOT REDDER.  What is left here is the two
     things a bank of lamps cannot say by itself: the floor under the
     ambient drops with the power, and a hull that is coming apart has
     more in the air than one that is not. */
  if(sinkMix>0.01f){
    for(int i=0;i<3;i++) F2->minLight[i]*=1.f-0.50f*sinkMix;
    F2->fogDensity*=1.f+0.16f*sinkMix*(float)F.sinkStage;
  }
  /* ---- and the last ninety seconds are darker than any of it.
     Everything above is the ship losing banks, which is a slope; this is
     the bottom of it.  Her lamp has gone out (finaleStep puts her at
     LM_OFF), the ambient floor drops to a third, and what is left of it
     is the colour of the beacons rather than of sodium -- so between two
     strikes the room is not grey, it is nearly black, and the strike is
     the only thing that shows you the deck.  The torch is the answer to
     it and that is the point.  The floor BREATHES with the alarm rather
     than sitting still, because a constant floor under a strobing lamp
     reads as two separate effects. */
  if(F.finalDefense&&!F.boarded){
    float b=STY_C[6]/(STY_RED[0]>0.f?STY_RED[0]:1.f);  /* 0..1.3, the beacon */
    static const float EMR[3]={1.30f,0.42f,0.34f};
    for(int i=0;i<3;i++){
      float lv=F2->minLight[i]*(0.30f+0.34f*b);
      F2->minLight[i]=lv*EMR[i];
      F2->fogCol[i]*=0.55f*(1.f+0.9f*b*EMR[i]);
    }
    F2->fogDensity*=1.22f;
  }
}

/* ============================================================
   14.  THE OVERLAY

   Quest banner, compass, subtitle, prompt, timer.  The health
   is deliberately absent: it is on the drone's face, and when
   she is gone there is no number anywhere.
   ============================================================ */
static void drawItems(void);

/* HER VOICE, WHEREVER IT IS BEING HEARD.  On the ending screen there is
   nothing else on the frame, so the box goes in the middle and lighter --
   and it is drawn after the credits rather than under them. */
static void drawSubtitle(float W,float H){
  const char *tx=dlgText();
  if(!tx) return;
  int off[3],len[3];
  int n=krWrap(tx,(float)SL_WRAP,2,off,len);
  float px=21.f, bw=0;
  for(int i=0;i<n;i++){
    char line[220]; int L=len[i]>219?219:len[i];
    memcpy(line,tx+off[i],L); line[L]=0;
    float w=krWidth(px,line);
    if(w>bw) bw=w;
  }
  float bx=W*0.5f-bw*0.5f-90, by=F.boarded? H*0.58f : H-136.f;
  hudRect(bx,by,bw+108,n*30.f+22,HUD_INK,F.boarded?0.72f:0.80f);
  hudText(bx+14,by+13,2.0f,"LUMI",HUD_TEAL,1.f);
  for(int i=0;i<n;i++){
    char line[220]; int L=len[i]>219?219:len[i];
    memcpy(line,tx+off[i],L); line[L]=0;
    krText(bx+84,by+10+i*30.f,px,line,HUD_BONE,1.f);
  }
}

void storyHUD(void){
  char b[192];
  float p[3]; gamePlayerPos(p);
  float W=(float)platW, H=(float)platH;

  /* ---- the quest banner, top left ---------------------------------- */
  if(questOn>=0&&!F.boarded){
    float a=0.80f+ (questFlash>0?0.20f*questFlash/1.5f:0);
    const char *q=questTxt(questOn);
    float w=krWidth(19.f,q)+58;
    hudRect(14,78,w,38,HUD_INK,a*0.9f);
    hudRect(14,78,4,38,HUD_AMBER,questFlash>0?1.f:0.7f);
    krText(30,86,19.f,q,questFlash>0?HUD_AMBER:HUD_BONE,1.f);
  }

  /* ---- the compass, top centre ------------------------------------- */
  if(questOn>=0&&!F.boarded){
    float yaw=gamePlayerYaw();
    float cx=W*0.5f, y=18;
    hudRect(cx-150,y,300,22,HUD_INK,0.55f);
    for(int i=0;i<24;i++){
      float a=i*0.2618f;
      float d=a-yaw;
      while(d>3.14159f) d-=6.2832f;
      while(d<-3.14159f) d+=6.2832f;
      if(fabsf(d)>1.05f) continue;
      float x=cx+d*140.f;
      int major=(i%6)==0;
      hudRect(x-1,y+(major?4:9),2,major?14:8,major?HUD_BONE:HUD_DIM,0.85f);
    }
    float tx=QT[questOn*2], tz=QT[questOn*2+1];
    float d=atan2f(tx-p[0],tz-p[2])-yaw;
    while(d>3.14159f) d-=6.2832f;
    while(d<-3.14159f) d+=6.2832f;
    if(fabsf(d)<1.05f){
      float x=cx+d*140.f;
      hudRect(x-5,y+3,10,10,HUD_AMBER,1.f);
    } else {
      float x = d>0?cx+146:cx-152;
      hudRect(x,y+6,6,10,HUD_AMBER,0.7f);
    }
    _snprintf(b,sizeof b,"%d M",(int)hypot2(tx-p[0],tz-p[2]));
    hudText(cx-hudWidth(1.6f,b)*0.5f,y+26,1.6f,b,HUD_DIM,1.f);
  }

  /* ---- the countdown -----------------------------------------------
     TWO NUMBERS COUNTING DOWN ON ONE SCREEN HAVE TO SAY WHICH IS WHICH.
     This one is the launch and the one below is the hatch, and both were
     bare digits -- so standing at the capsule at T-40 the player read
     "40" at the top and "15" in the middle and had no way at all to tell
     that one was the ship leaving and the other was the door opening.
     They are labelled now, in the language the rest of the overlay is in,
     and the labels are what differ rather than the size. */
  if(F.finalDefense&&!F.boarded){
    _snprintf(b,sizeof b,"%d",(int)(F.timerLaunch+0.999f));
    int crit=F.timerLaunch<20.f;
    float s=crit?5.4f:4.2f;
    const float *c=crit?HUD_CRIT:HUD_AMBER;
    float a=crit?(fmodf((float)platTime()*4,1.f)<0.5f?1.f:0.55f):1.f;
    const char *k=menuPick("\354\202\254\354\266\234\352\271\214\354\247\200",  /* 사출까지 */
                           "LAUNCH IN",
                           /* 射出まで */
                           "\345\260\204\345\207\272\343\201\276\343\201\247");
    float nw=hudWidth(s,b), kw=krWidth(17.f,k);
    float x0=W*0.5f-(nw+kw+14)*0.5f;
    krText(x0,60+(s*8.f-17.f)*0.5f+4,17.f,k,HUD_DIM,a);
    hudText(x0+kw+14,60,s,b,c,a);
  }

  /* ---- the capsule prompt.  The one action in the game that ends it,
     and until now the only one with nothing on screen asking for it. */
  int atHatch=0;
  if(F.finalDefense&&!F.boarded){
    float dx=p[0]-capX, dz=p[2]-capZ;
    float d2=dx*dx+dz*dz;
    if(d2<CAP_R*CAP_R){
      atHatch=1;
      if(F.timerLaunch<=CAP_AT){
        const char *k=menuPick("\355\203\221\354\212\271","BOARD",     /* 탑승 */
                               "\346\220\255\344\271\227");            /* 搭乗 */
        float w=krWidth(22.f,k)+96;
        float bl=0.55f+0.45f*(fmodf((float)platTime()*2.2f,1.f)<0.5f?1.f:0.f);
        hudRect(W*0.5f-w*0.5f,H*0.54f,w,36,HUD_INK,0.85f);
        hudText(W*0.5f-w*0.5f+15,H*0.54f+10,2.4f,"E",HUD_AMBER,bl);
        krText(W*0.5f-w*0.5f+46,H*0.54f+7,22.f,k,HUD_BONE,1.f);
      } else {
        /* ... and the other one.  Not the ship leaving: the hatch of the
           thing you are standing on, which does not unlock until T-25. */
        _snprintf(b,sizeof b,"%d",(int)(F.timerLaunch-CAP_AT+0.999f));
        const char *k=menuPick("\355\225\264\354\271\230\040\352\260\234\353\260\251\352\271\214\354\247\200", /* 해치 개방까지 */
                               "HATCH OPENS IN",
                               /* ハッチ開放まで */
                               "\343\203\217\343\203\203\343\203\201\351\226"
                               "\213\346\224\276\343\201\276\343\201\247");
        float kw=krWidth(17.f,k), w=kw+78;
        hudRect(W*0.5f-w*0.5f,H*0.54f,w,30,HUD_INK,0.7f);
        krText(W*0.5f-w*0.5f+12,H*0.54f+7,17.f,k,HUD_DIM,1.f);
        hudText(W*0.5f-w*0.5f+kw+22,H*0.54f+8,1.9f,b,HUD_BONE,1.f);
      }
    }
  }

  /* ---- the interaction prompt -------------------------------------- */
  if(nearItem>=0&&!F.boarded&&!(atHatch&&F.timerLaunch<=CAP_AT)){
    static const char *K[5]={"\355\232\214\353\263\265",      /* 회복 */
                             "\355\232\215\353\223\235",      /* 획득 */
                             "\355\203\204\354\225\275",      /* 탄약 */
                             "\354\227\220\353\204\210\354\247\200\355\214\251", /* 에너지팩 */
                             "\353\263\264\352\270\211\354\203\201\354\236\220"};/* 보급상자 */
    static const char *KE[5]={"HEAL","TAKE","AMMO",
                              "ENERGY CORE","SUPPLIES"};
    static const char *KJ[5]={"\345\233\236\345\276\251",       /* 回復 */
                              "\345\217\226\345\276\227",       /* 取得 */
                              "\345\274\276\350\226\254",       /* 弾薬 */
                              /* エネルギーパック */
                              "\343\202\250\343\203\215\343\203\253\343\202"
                              "\256\343\203\274\343\203\221\343\203\203\343"
                              "\202\257",
                              "\350\243\234\347\265\246\347\256\261"};/* 補給箱 */
    const char *k=menuPick(K[SI_KIND[nearItem]],KE[SI_KIND[nearItem]],
                           KJ[SI_KIND[nearItem]]);
    float w=krWidth(20.f,k)+92;
    hudRect(W*0.5f-w*0.5f,H*0.60f,w,32,HUD_INK,0.78f);
    hudText(W*0.5f-w*0.5f+14,H*0.60f+9,2.2f,"E",HUD_AMBER,1.f);
    krText(W*0.5f-w*0.5f+42,H*0.60f+6,20.f,k,HUD_BONE,1.f);
  }
  if(F.carrying&&installT>0){
    float w=260;
    hudRect(W*0.5f-w*0.5f,H*0.66f,w,14,HUD_DIM,0.35f);
    hudRect(W*0.5f-w*0.5f,H*0.66f,w*(installT/3.f),14,HUD_AMBER,0.95f);
  }

  if(!F.boarded) drawSubtitle(W,H);

  /* ---- being hit ---------------------------------------------------- */
  if(damageT>0.01f){
    float a=damageT*0.42f;
    hudRect(0,0,W,26,HUD_CRIT,a); hudRect(0,H-26,W,26,HUD_CRIT,a);
    hudRect(0,0,26,H,HUD_CRIT,a); hudRect(W-26,0,26,H,HUD_CRIT,a);
  }
  /* and when she is gone, there is no number anywhere -- so the edge of
     the screen is the only thing left that knows */
  if(lumiModeNow()==LM_OFF){
    float hp=gamePlayerHP();
    if(hp<70.f){
      float a=(1.f-hp/70.f)*0.34f;
      hudRect(0,0,W,40,HUD_CRIT,a); hudRect(0,H-40,W,40,HUD_CRIT,a);
      hudRect(0,0,40,H,HUD_CRIT,a); hudRect(W-40,0,40,H,HUD_CRIT,a);
    }
  }

  if(failMsg[0]){
    hudRect(0,H*0.42f,W,64,HUD_INK,0.86f);
    hudText(W*0.5f-hudWidth(3.4f,failMsg)*0.5f,H*0.42f+22,3.4f,failMsg,HUD_CRIT,1.f);
  }
  if(F.boarded){
    float a=creditT<1.f?creditT:1.f;
    /* THE CREDITS PAINT THE WHOLE FRAME, so anything that has to be read
       ON them is drawn after -- her last three lines were being laid down
       first and then covered by a 94% black rectangle. */
    hudRect(0,0,W,H,HUD_INK,a*0.94f);
    if(creditT>1.5f){
      /* THE CARD NAMES WHAT THE PLAYER JUST LOST, NOT WHERE IT
         HAPPENED.  The ship is the setting and the setting is over;
         she is the one who stayed, and the title card already said
         GOODBYE LUMI on the way in.  Latin in the 5x7 stencil for
         all three languages, the way that card is. */
      const char *T="THANK YOU LUMI";
      /* and it is fourteen cells wide rather than eight, so the size
         comes off the frame: 6 px a cell needs 504 of it. */
      float tp=W/1280.f; if(tp>1.f) tp=1.f;
      float px=6.f*tp;
      hudText(W*0.5f-hudWidth(px,T)*0.5f,H*0.40f,px,T,HUD_BONE,
              (creditT-1.5f)>1?1.f:(creditT-1.5f));
      /* THE LAST LINE IN THE GAME WAS THE ONE LINE NOT ON menuPick.  It
         was spelled twice inline -- once to measure and once to draw --
         so it read as Korean in every language.  One name, one pick. */
      if(creditT>3.f){
        const char *rem=menuPick("\353\243\250\353\257\270\353\245\274 "     /* 루미를 기억하세요 */
                                 "\352\270\260\354\226\265\355\225\230\354\204\270\354\232\224",
                                 "REMEMBER LUMI",
                                 /* ルミを覚えていて */
                                 "\343\203\253\343\203\237\343\202\222\350\246"
                                 "\232\343\201\210\343\201\246\343\201\204\343"
                                 "\201\246");
        krText(W*0.5f-krWidth(20.f,rem)*0.5f,H*0.40f+70,20.f,rem,
               HUD_DIM,(creditT-3.f)>1?1.f:(creditT-3.f));
      }
    }
    /* ---- and the way back.  Same shape as the title card's rows, because
       it is the same button: it puts that card back up. */
    if(endMenu){
      const char *lb=menuPick("\354\262\230\354\235\214\354\234\274\353\241\234",
                              "BACK TO TITLE",
                              /* タイトルへ */
                              "\343\202\277\343\202\244\343\203\210\343\203"
                              "\253\343\201\270");
      float bw2=360.f, bh2=54.f;
      endX0=W*0.5f-bw2*0.5f; endY0=H*0.78f;
      endX1=endX0+bw2;       endY1=endY0+bh2;
      hudRect(endX0,endY0,bw2,bh2,HUD_INK,0.92f);
      hudRect(endX0,endY0,3,bh2,HUD_AMBER,1.f);
      krText(endX0+30,endY0+bh2*0.5f-13.f,24.f,lb,HUD_BONE,1.f);
      const char *hint=menuPick("ENTER \353\230\220\353\212\224 \355\201\264\353\246\255",
                                "ENTER OR CLICK",
                                /* ENTER またはクリック */
                                "ENTER \343\201\276\343\201\237\343\201\257"
                                "\343\202\257\343\203\252\343\203\203\343\202"
                                "\257");
      krText(W*0.5f-krWidth(16.f,hint)*0.5f,endY1+14.f,16.f,hint,HUD_DIM,0.7f);
    }
    drawSubtitle(W,H);
  }
  drawItems();
}

/* the items themselves are entities, pushed in the world pass; this only
   marks the one you can reach */
static void drawItems(void){}

/* ---- the world half of the items ------------------------------------ */
static void drawDrops(void);
void storyDrawWorld(void){
  drawGoalRing();
  static const float HPC[3]={0.30f,0.90f,0.55f};
  drawDrops();
  static const float WEPC[3]={0.95f,0.72f,0.22f};
  static const float AMC[3]={0.85f,0.80f,0.55f};
  static const float PKC[3]={0.35f,0.85f,1.00f};
  for(int i=0;i<SI_N;i++){
    if(!IT[i].live||IT[i].taken) continue;
    int k=SI_KIND[i];
    const float *c = k==0?HPC : k==1?WEPC : k==3?PKC : AMC;
    EntFx fx=ENT_PLAIN, gx=ENT_PLAIN;
    int zi=zoneAt(IT[i].x,IT[i].z);
    fx.zone=gx.zone=(float)zi;
    fx.rim=0.6f; fx.ao=0.9f; fx.rough=0.34f; fx.sk=3; fx.sf=1.f; fx.si=0.5f;
    gx.rim=0.2f; gx.ao=1.f;
    sampleLight(IT[i].x,IT[i].y+0.4f,IT[i].z,fx.lit,fx.ldir);
    for(int q=0;q<3;q++){ gx.lit[q]=fx.lit[q]; gx.ldir[q]=fx.ldir[q]; }
    float bob=sinf(IT[i].bob*1.6f)*0.035f;
    float A[12],B[12],C[12];
    mTrans34(IT[i].x,IT[i].y+0.30f+bob,IT[i].z,A);
    mRotY34(IT[i].bob*0.6f,B); mMul34(A,B,C);
    /* A WEAPON IS ITS OWN SILHOUETTE.  The crate-and-bar reading is what
       makes a health kit legible across a dark room, and it is what made
       the three guns unreadable: the player is being sent the length of
       the ship for a shape they already know, and a box does not have it.
       vmwDrawPickup draws the real mesh, turning, in storyDrawGuns; what
       is left here is the deck under it -- a low lit disc, so the thing
       hanging in the air is standing on something. */
    if(k==1){
      /* NINE CENTIMETRES OFF THE TRACED SURFACE, NOT TWO.  deckAt answers
         with the top of the BRUSH, and what the player sees standing there
         is a kit part -- FLOOR 1..4 are real meshes lying on that brush,
         so the visible deck is several centimetres higher than the number
         the trace returns.  A marker laid at brush height is inside the
         plate and draws nothing at all, which is exactly what the first
         pass did. */
      for(int q=0;q<10;q++){
        float a=q*0.6283f;
        mTrans34(IT[i].x+cosf(a)*0.38f,IT[i].y+0.09f,IT[i].z+sinf(a)*0.38f,A);
        mRotY34(-a,B); mMul34(A,B,C);
        pushEnt(C,0.13f,0.020f,0.050f,c[0],c[1],c[2],
                (q&1)?0.45f:1.15f,&gx,PRIM_BEVEL);
      }
      fxAddDynLight(IT[i].x,IT[i].y+0.55f,IT[i].z,
                    c[0]*0.42f,c[1]*0.42f,c[2]*0.42f,3.4f,0.05f);
      continue;
    }
    /* THE SUPPLIES ARE THE BENCH'S TOO.  A weapon has been drawn with its
       real mesh since vmwDrawPickup existed, and every other pickup was a
       bevelled crate with three bars through it -- an invention standing
       in for two props the kit already has.  Supply Crate is #1 and Energy
       Core is #33; what stays hand-made is only the CROSS, because the
       bench's crate does not say "medical" and that is the one pickup the
       player has to find in a hurry.  The bars are now laid ON the mesh
       rather than driven through a box. */
    float bob2=bob;
    if(k==3){
      /* the core: small, upright, and the only pickup that is its own
         light source.  1.7x the bench's 0.39 m so it reads across a hold. */
      float base=IT[i].y+0.12f+bob2;
      propDrawAt(PROP_ENERGY_CORE,IT[i].x,base,IT[i].z,IT[i].bob*0.6f,1.70f,1.f);
      float T[12];
      mTrans34(IT[i].x,base+0.39f*1.70f*0.5f,IT[i].z,T);
      pushEnt(T,0.15f,0.15f,0.15f,c[0],c[1],c[2],1.5f,&gx,PRIM_LENS);
    } else {
      float sc = (k==2) ? 0.72f : 0.92f;             /* ammo is the small one */
      propDrawAt(PROP_SUPPLY_CRATE,IT[i].x,IT[i].y+bob2,IT[i].z,
                 IT[i].bob*0.35f,sc,1.f);
      float s=0.24f*sc, ty=IT[i].y+0.49f*sc+bob2+0.03f;
      float T[12],R[12],M2[12];
      mTrans34(IT[i].x,ty,IT[i].z,T);
      mRotY34(IT[i].bob*0.35f,R); mMul34(T,R,M2);
      if(k==0||k==4){
        /* the one symbol that needs no legend and survives at 20 metres */
        pushEnt(M2,s*1.30f,s*0.20f,s*0.20f,c[0],c[1],c[2],2.2f,&gx,PRIM_BEVEL);
        pushEnt(M2,s*0.20f,s*0.20f,s*1.30f,c[0],c[1],c[2],2.2f,&gx,PRIM_BEVEL);
      } else
        pushEnt(M2,s*0.34f,s*0.30f,s*0.34f,c[0],c[1],c[2],1.8f,&gx,PRIM_LENS);
    }
    fxAddDynLight(IT[i].x,IT[i].y+0.45f,IT[i].z,c[0]*0.25f,c[1]*0.25f,c[2]*0.25f,
                  2.6f,0.05f);
  }
}
/* ---- and the things the bodies left.  Smaller than a written pickup and
   lit harder: it has to be findable in a red-lit room from across the
   gallery, and it has to be obviously not one of the twenty-four. */
static void drawDrops(void){
  static const float HPC[3]={0.30f,0.95f,0.55f};
  static const float AMC[3]={0.95f,0.82f,0.34f};
  for(int i=0;i<MAXDROP;i++){
    if(!DROP[i].used) continue;
    const float *c = DROP[i].kind==0?HPC:AMC;
    /* THE LAST FOUR SECONDS ARE A WARNING, NOT A SURPRISE.  A box that
       vanishes while you are running at it is worse than one that was
       never there; it blinks out. */
    float left=DROP_LIFE-DROP[i].t;
    if(left<4.f&&fmodf(left*3.f,1.f)<0.42f) continue;
    EntFx gx=ENT_PLAIN;
    gx.zone=(float)zoneAt(DROP[i].x,DROP[i].z);
    gx.rim=0.35f; gx.ao=1.f;
    sampleLight(DROP[i].x,DROP[i].y+0.3f,DROP[i].z,gx.lit,gx.ldir);
    float bob=sinf(DROP[i].bob*2.1f)*0.03f;
    float y=DROP[i].y+0.10f+bob;
    propDrawAt(PROP_SUPPLY_CRATE,DROP[i].x,y,DROP[i].z,DROP[i].bob*0.8f,0.52f,1.f);
    float sc=0.24f*0.52f, T[12],R[12],M2[12];
    mTrans34(DROP[i].x,y+0.49f*0.52f+0.03f,DROP[i].z,T);
    mRotY34(DROP[i].bob*0.8f,R); mMul34(T,R,M2);
    if(DROP[i].kind==0){
      pushEnt(M2,sc*1.30f,sc*0.20f,sc*0.20f,c[0],c[1],c[2],3.0f,&gx,PRIM_BEVEL);
      pushEnt(M2,sc*0.20f,sc*0.20f,sc*1.30f,c[0],c[1],c[2],3.0f,&gx,PRIM_BEVEL);
    } else
      pushEnt(M2,sc*0.40f,sc*0.34f,sc*0.40f,c[0],c[1],c[2],2.6f,&gx,PRIM_LENS);
    fxAddDynLight(DROP[i].x,y+0.35f,DROP[i].z,
                  c[0]*0.30f,c[1]*0.30f,c[2]*0.30f,2.2f,0.05f);
  }
}
/* the three guns, drawn with the viewmodel's own program because it is the
   only thing that knows how to light them.  It runs where the shells run
   -- after the world and before the viewmodel clears the depth buffer --
   because it is a world object and has to be occluded by the ship. */
void storyDrawGuns(const void *frameState,const float *proj,const float *view){
  const FrameState *F=(const FrameState*)frameState;
  float p[3]; gamePlayerPos(p);
  for(int i=0;i<SI_N;i++){
    if(!IT[i].live||IT[i].taken||SI_KIND[i]!=1) continue;
    float dx=IT[i].x-p[0], dz=IT[i].z-p[2];
    if(dx*dx+dz*dz>46.f*46.f) continue;
    float t=IT[i].bob;
    vmwDrawPickup(F,proj,view,SI_AMT[i],
                  IT[i].x, IT[i].y+0.62f+sinf(t*1.6f)*0.045f, IT[i].z,
                  t*0.85f, 1.35f);
  }
}
void storyDamaged(void){ damageT=1.f; }
