/* ============================================================
   8/9/12/13.  PLAYER, INPUT, BUILD, FRAME

   The boot order is the browser's boot order, and it matters:
   materials, then the ambient cubes they feed, then the kit
   (which adds brushes), then the bake, then the mesh that reads
   the bake.  Meshing before the kit has placed its emitters
   gives a ship lit by nothing.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "app.h"
#include "prim.h"
#include "ent.h"
#include "hud.h"
#include "kfont.h"
#include "vmw.h"
#include "fx.h"
#include "zom.h"
#include "lumi.h"
#include "lumimesh.h"
#include "face.h"
#include "story.h"
#include "menu.h"
#include "props.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int FASTLIGHT=0;
static long EXE_BYTES=0;

void renderSetCubes(const float *c);
void adUpdate(float dt,float px,float pz,FrameState *F);
static int propCardN=-1;                 /* -prop N: see appFrame */
void auInit(void); void auShutdown(void); void auAmbient(int);
void auListener(float,float,float); void auFootstep(float hard);
void auMute(int); int auMuted(void);
void adDraw(float px,float pz,float elapsed);
int  atmosInit(void);
void atmosDraw(const FrameState *F);
void levelGridBuild(void);
int  boxSolid(float ax0,float ay0,float az0,float ax1,float ay1,float az1);
int  shotGrab(const char *path);
int  probeDumpPNG(const char *path);

/* -shot [path]        one frame, photographed, then exit
   -cam x,y,z,yaw,pitch  stand somewhere specific first
   Together they are the native answer to tools/shot.js. */
static int   shotMode=0, shotFrame=0, benchN=0, shotGun=0, shotFire=0;
static int   shotHold=0;             /* -hold N: frames to wait first */
static int   fontTest=0;             /* -font: the typeface, on a card */
static int   spiderN=0;              /* -spider N: N of them, in front of you */
static int   sceneN=0;               /* -scene N: start at the head of a scene */
static int   devHud=0;               /* -dev: the instruments, over the game */
static int   autoPlay=0;             /* -auto N: play it, for N seconds */
static const char *probeArg=0;       /* -probe: the deck, as an ASCII map */
static char  shotPath[260]="shot.png";
static int   probeMode=0;
static char  probePath[260]="probe.png";

/* ---- the player ---------------------------------------------------- */
#define PR        0.32f
#define H_STAND   1.80f
#define H_CROUCH  1.15f
#define EYE_STAND 1.66f
#define EYE_CROUCH 1.02f
#define GRAV      22.0f
#define JUMPV     6.6f
#define STEPUP    0.46f
#define FOOT_EPS  0.09f
#define SENS      0.0022f

static struct {
  float x,y,z, vy, yaw, pitch, bob;
  int grounded, crouch;
} P = { -21.f, 0.f, 13.8f, 0,0,0,0, 0,0 };

static float curH=H_STAND, curEye=EYE_STAND;
static float stepPhase=0, moveSpeedNow=0, shakeAmp=0;
/* THE STAIR OFFSET.  The body teleports onto a lip; the eye does not.
   stepOff is how far the eye is still behind the body, in metres, and it
   is paid off exponentially -- which is the whole difference between
   walking a deck with grating and door sills on it and pogoing over it. */
static float stepOff=0;
#define STEP_MAX  0.50f
static float playerHP=100.f, hurtT=0, hurtDirX=0, hurtDirZ=0;
static int   godMode=0;

float gamePlayerHP(void){ return playerHP; }
void  gameSetHP(float v){ playerHP=v<0?0:(v>100?100:v); }
void  gamePlayerPos(float *o){ o[0]=P.x; o[1]=P.y; o[2]=P.z; }
void  gamePlayerEye(float *o){ o[0]=P.x; o[1]=P.y+curEye+P.bob+stepOff; o[2]=P.z; }
float gamePlayerYaw(void){ return P.yaw; }
float gamePlayerPitch(void){ return P.pitch; }
void  gameTeleport(float x,float y,float z,float yaw){
  P.x=x; P.y=y; P.z=z; P.yaw=yaw; P.vy=0; stepOff=0;
}
void  gamePitch(float p){ P.pitch=p; }
void zomHurtPlayer(float dmg,float sx,float sy,float sz){
  (void)sy;
  if(godMode) return;
  storyDamaged();
  auHurt(dmg);                  /* it was the one event with nothing in the mix */
  playerHP-=dmg;
  if(playerHP<0) playerHP=0;
  hurtT=1.f;
  float dx=sx-P.x, dz=sz-P.z, l=hypot2(dx,dz);
  if(l>1e-3f){ hurtDirX=dx/l; hurtDirZ=dz/l; }
  shakeAmp+=0.8f;
}
static unsigned char keys[256];
static int noclip=0;

static int collides(float x,float y,float z,float h){
  if(noclip) return 0;
  return boxSolid(x-PR,y+FOOT_EPS,z-PR,x+PR,y+h,z+PR);
}
/* THE FULL 46 CM WAS BEING SPENT ON A 4 CM LIP.  A door sill, the edge of
   a grating, a kit part's plinth: every one of them lifted the player the
   whole step height and gravity then dropped him back over the next three
   frames, which is the bob that was being felt on ground that is flat.
   Find the SMALLEST rise that clears, settle the body back down onto what
   it stepped onto so gravity has nothing left to undo, and hand the eye
   the difference to pay off smoothly. */
static int tryStep(float nx,float nz){
  static const float R[6]={0.06f,0.12f,0.20f,0.28f,0.37f,STEPUP};
  for(int i=0;i<6;i++){
    if(collides(nx,P.y+R[i],nz,curH)) continue;
    float y0=P.y, y=P.y+R[i], rem=R[i];
    while(rem>0.005f){
      float st=rem<0.01f?rem:0.01f;
      if(collides(nx,y-st,nz,curH)) break;
      y-=st; rem-=st;
    }
    P.y=y; P.x=nx; P.z=nz;
    stepOff-=(y-y0);
    if(stepOff<-STEP_MAX) stepOff=-STEP_MAX;
    return 1;
  }
  return 0;
}
static void moveHorizontal(float dx,float dz){
  if(dx!=0){
    if(!collides(P.x+dx,P.y,P.z,curH)) P.x+=dx;
    else if(P.grounded) tryStep(P.x+dx,P.z);
  }
  if(dz!=0){
    if(!collides(P.x,P.y,P.z+dz,curH)) P.z+=dz;
    else if(P.grounded) tryStep(P.x,P.z+dz);
  }
}

static void simulate(float dt){
  int wantCrouch = keys['C']||keys[VK_CONTROL];
  if(wantCrouch!=P.crouch){
    if(!wantCrouch){ if(!collides(P.x,P.y,P.z,H_STAND)) P.crouch=0; }
    else P.crouch=1;
  }
  float tH=P.crouch?H_CROUCH:H_STAND, tE=P.crouch?EYE_CROUCH:EYE_STAND;
  float k=dt*12.f; if(k>1)k=1;
  curH+=(tH-curH)*k; curEye+=(tE-curEye)*k;

  float fx=sinf(P.yaw), fz=cosf(P.yaw);
  float rx=-cosf(P.yaw), rz=sinf(P.yaw);
  float mx=0,mz=0;
  if(keys['W']||keys[VK_UP])    { mx+=fx; mz+=fz; }
  if(keys['S']||keys[VK_DOWN])  { mx-=fx; mz-=fz; }
  if(keys['D']||keys[VK_RIGHT]) { mx+=rx; mz+=rz; }
  if(keys['A']||keys[VK_LEFT])  { mx-=rx; mz-=rz; }
  float len=hypot2(mx,mz);
  int running=keys[VK_SHIFT];
  float speed = P.crouch?2.1f:(running?6.6f:4.2f);   /* m/s */
  if(storyLockMove()) speed=0;
  speed *= storySpeedScale(mx,mz);
  if(len>0){
    mx=mx/len*speed*dt; mz=mz/len*speed*dt;
    if(noclip){
      float pitchLift = keys['W']||keys[VK_UP] ? -sinf(P.pitch) :
                        (keys['S']||keys[VK_DOWN] ? sinf(P.pitch) : 0);
      P.x+=mx; P.z+=mz; P.y+=pitchLift*speed*dt;
    } else {
      moveHorizontal(mx,mz);
      if(P.grounded){
        stepPhase+=speed*dt;
        P.bob=sinf(stepPhase*2.6f)*(running?0.055f:0.035f);
        if(stepPhase>1.15f){ stepPhase=0; auFootstep(running?1.f:0.f); }
      }
    }
  } else P.bob*=0.88f;

  if(noclip){
    if(keys[VK_SPACE]) P.y+=speed*dt;
    if(keys['C'])      P.y-=speed*dt;
    P.vy=0; P.grounded=0;
    return;
  }

  if(keys[VK_SPACE]&&P.grounded&&!P.crouch){ P.vy=JUMPV; P.grounded=0; }
  P.vy-=GRAV*dt;
  float dy=P.vy*dt;
  int wasAir=!P.grounded;
  float yBefore=P.y;
  if(dy!=0){
    if(!collides(P.x,P.y+dy,P.z,curH)){ P.y+=dy; P.grounded=0; }
    else{
      float dir=sgn(dy), rem=fabsf(dy);
      while(rem>0.001f){
        float s=rem<0.01f?rem:0.01f;
        if(collides(P.x,P.y+dir*s,P.z,curH)) break;
        P.y+=dir*s; rem-=s;
      }
      if(dy<0) P.grounded=1;
      P.vy=0;
    }
  }
  /* and the same courtesy on the way down: stepping OFF a lip drops the
     body in one frame, so the eye is lent the drop and gives it back */
  if(P.grounded&&wasAir){
    float d=yBefore-P.y;
    if(d>0.f&&d<0.7f){ stepOff+=d; if(stepOff>STEP_MAX) stepOff=STEP_MAX; }
  }
  { float k=dt*11.f; if(k>1)k=1;
    stepOff-=stepOff*k;
    if(stepOff<0.0006f&&stepOff>-0.0006f) stepOff=0; }
  if(P.y<-40){ P.x=-21; P.y=0; P.z=13.8f; P.vy=0; stepOff=0; }
}

/* ---- boot ---------------------------------------------------------- */
/* ---- STARTING OVER ---------------------------------------------------
   Not a reboot: the ship, the bake, the meshes and the nav lattice are
   the same eight seconds of work they were at launch and none of it
   depends on the run.  What a new game is, is the story's own state --
   which is why storyInit had to become something that can be called
   twice. */
void vmwDisarmAll(void);
void appRestart(void){
  zomReset();
  fxReset();
  vmwDisarmAll();
  storyInit();
  menuReset();
  platLog("story  restarted from the ending");
}

int appInit(void){
  FASTLIGHT = platArg("-fast");
  /* THE BUDGET ON SCREEN IS THE EXE'S OWN.  The contest measures the
     executable and its runtime against 1,474,560 bytes, so the only figure
     worth showing is the size of the file that is running. */
  { char self[MAX_PATH];
    if(GetModuleFileNameA(0,self,MAX_PATH)){
      HANDLE h=CreateFileA(self,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
      if(h!=INVALID_HANDLE_VALUE){ EXE_BYTES=(long)GetFileSize(h,0); CloseHandle(h); }
    } }
  {
    const char *cl=platCmdline(), *p;
    if((p=strstr(cl,"-shot"))){
      shotMode=1;
      p+=5; while(*p==' ') p++;
      if(*p && *p!='-'){ int i=0; while(*p&&*p!=' '&&i<255) shotPath[i++]=*p++;
                         shotPath[i]=0; }
    }
    if((p=strstr(cl,"-debug"))) R_DEBUG=atoi(p+6);
    if((p=strstr(cl,"-gun")))   shotGun=atoi(p+4);
    shotFire=platArg("-fire");
    if(platArg("-flash")) R_FLASH=1;
    /* TWO FLAGS ARE SPELLED -probe AND THEY ARE NOT THE SAME INSTRUMENT.
       `-probe atlas.png` writes the reflection probe atlas; `-probe
       145,420,205,650` prints the deck as an ASCII map.  Merged in from two
       branches they collided on the first six characters, and asking for
       the deck map silently wrote a 1.6 MB PNG called "145,420,205,650".
       A filename is what ends in .png; everything else is the deck. */
    if((p=strstr(cl,"-probe"))){
      const char *q=p+6; while(*q==' ') q++;
      { int i=0; char buf[256];
        while(q[i]&&q[i]!=' '&&i<255){ buf[i]=q[i]; i++; }
        buf[i]=0;
        if(i>=4&&!strcmp(buf+i-4,".png")){ probeMode=1; strcpy(probePath,buf); }
        else if(!i||buf[0]=='-') probeMode=1;    /* bare -probe: the atlas */
      }
    }
    if((p=strstr(cl,"-bench"))) benchN=atoi(p+6)?atoi(p+6):600;
    if((p=strstr(cl,"-hold")))  shotHold=atoi(p+5);
    fontTest=platArg("-font");
    if((p=strstr(cl,"-spider"))) spiderN=atoi(p+7)?atoi(p+7):1;
    if((p=strstr(cl,"-scene")))  sceneN=atoi(p+6);
    devHud=platArg("-dev");
    if((p=strstr(cl,"-auto"))) autoPlay=atoi(p+5)?atoi(p+5):240;
    /* -auto IS A ROUTE INSTRUMENT, NOT A DIFFICULTY ONE.  What it is there
       to answer is whether a player can get from the crew quarters to the
       capsule through 35 doors; a robot that dies to the second ambush
       answers a different question badly, and the restore it triggers puts
       the state it was measuring back to a checkpoint.  So it walks the
       route unkillable unless -mortal says otherwise, and -god offers the
       same thing to a human who is testing a room rather than a fight. */
    if(platArg("-god")||(autoPlay&&!platArg("-mortal"))) godMode=1;
    if((p=strstr(cl,"-cam"))){
      float a[5]={0,0,0,0,0};
      if(sscanf(p+4,"%f,%f,%f,%f,%f",a,a+1,a+2,a+3,a+4)>=3){
        P.x=a[0]; P.y=a[1]; P.z=a[2]; P.yaw=a[3]; P.pitch=a[4];
        noclip=1;
      }
    }
  }

  /* THE KIT RUNS BEFORE THE BAKE GRID IS BUILT, and that ordering is the
     whole reason an emitter works.  A part's triangles are invisible to the
     radiosity gather -- it ray-casts the BRUSH set and reads the material off
     whatever it hits -- so a lit channel declares a real brush lying in its
     modelled recess.  Build the ray grid first and those brushes are not in
     it, and every part in the ship glows while lighting nothing. */
  levelInit();
  texCompileAll();                 /* MAT_ALBEDO/MAT_EMIS, which the bounce reads */
  primInit();                      /* the instanced solids */
  lumiMeshInit();                  /* ... and LUMI, as thirteen more */
  /* AND THE PROP MESHES, WHICH ARE 235 MORE.  This has to be here and not
     in propsInit: entInit uploads PRIM_V to the GPU exactly once, so a
     range appended after it is a draw of somebody else's vertices.  The
     PLACEMENT still runs last, because it needs the doors and the items. */
  propMeshInit();
  kitBuildModules();               /* adds clip and emitter brushes */
  bakeInit();                      /* ray grid over the finished brush set */
  solveCubes(FASTLIGHT?1:3);
  solveProbes();                   /* the specular half of the same solution */
  /* AND THEN A THIRD OFF THE DIFFUSE AMBIENT, WHICH IS WHERE THE DARK
     ACTUALLY COMES FROM.  Doom 3 had no ambient term at all: a surface no
     lamp could see was black, and that is why a lamp in it was an event.
     This ship cannot go that far -- it has no per-light shadows, so the
     ambient cube is the only thing standing between an unlit corner and a
     hole in the screen -- but 1.0 of it was set when the cube was carrying
     the whole mid-tone, and the sixteen analytic lamps carry that now.
     0.70 is what puts the dark BETWEEN two lamps back.

     AFTER solveProbes, deliberately: the probe is traced radiance and it
     is what every reflection in the ship is made of, so it keeps the full
     bounce.  Only the diffuse floor comes down.  A dark room with live
     reflections in it is the look; a dark room with dead metal is not. */
  for(int i=0;i<CUBE_COUNT*18;i++) CUBES[i]*=0.70f;
  if(probeMode){ probeDumpPNG(probePath); platQuit(); }
  levelGridBuild();                /* the player's own collision grid */
  buildMesh();

  if(!renderInit()) return 0;
  if(!entInit(CUBE_COUNT)) return 0;
  if(!hudInit()) return 0;
  krInit();                        /* the glyph sheet, over hudInit's placeholder */
  if(!faceInit()) return 0;
  if(!atmosInit()) return 0;
  if(!vmwInit()) return 0;
  if(!fxInit()) return 0;
  renderSetCubes(CUBES);
  /* renderUpload() USED TO BE HERE and now runs below propsInit: the
     contact-shadow pass writes the light channels of the scene vertices
     after the props are down, and uploading before that would send the
     lightmap the props are not in.  Nothing between here and there draws. */

  zomInit();                       /* the nav lattice, over the finished brushes */
  storySetTrace(platArg("-log"));
  { const char *pc=strstr(platCmdline(),"-prop");
    if(pc&&strncmp(pc,"-probe",6)!=0) propCardN=atoi(pc+5); }
  storyFreeCam(noclip);
  storyInit();
  if(sceneN) storyScene(sceneN);
  /* -gun N AFTER storyInit, NOT BEFORE IT.  storyInit() is where a new run
     empties your hands (vmwDisarmAll), so a gun forced during appInit was
     taken away again eleven lines later: OWNED all zero, and vmwTryFire's
     first line is `if(!OWNED[curW]) return`.  Every `-gun N -fire` shot
     ever taken photographed a gun that had not fired -- byte-identical to
     the same frame without -fire, apart from the grain. */
  if(shotGun) vmwForce(shotGun);
  { const char *q=strstr(platCmdline(),"-probe");
    if(q){ probeArg=q+6; } }
  if(spiderN){
    /* -spider N: N of them, a few metres ahead, so the animal can be
       photographed without playing to the room that spawns it */
    float fx2=sinf(P.yaw), fz2=cosf(P.yaw);
    for(int i=0;i<spiderN;i++){
      float off=(i-(spiderN-1)*0.5f)*1.9f;
      zomSpawn(i==spiderN-1&&spiderN>2?Z_BROODER:Z_SCUTTLER,
               P.x+fx2*6.5f-fz2*off, P.y, P.z+fz2*6.5f+fx2*off,
               P.yaw+3.1416f, -1);
    }
  }

  /* -lumi N: the bench.  N is the display state; -lumimesh is a different
     flag and must not be mistaken for it, which strstr would happily do. */
  /* AND THE BENCH HAS TO STAND UP FIRST.  The opening puts the player on
     the deck -- storyCamY() 0.35 and a 0.31 rad roll -- and the card aims
     at gamePlayerEye(), which reports a STANDING eye because it does not
     apply that scale.  So she photographed half a metre above the frame
     against a horizon that was not level, and -cam had to be found by
     sweeping.  -lumi means the intro is over, the same way every other
     instrument flag means the title card is. */
  for(const char *p=strstr(platCmdline(),"-lumi"); p; p=strstr(p+5,"-lumi"))
    if(strncmp(p,"-lumimesh",9)!=0){
      /* a digit means that state, INCLUDING zero -- atoi's 0 for "no
         number given" and LD_EYES_IDLE are the same answer otherwise, and
         her idle face was the one state the bench could not photograph */
      const char *q=p+5; while(*q==' ') q++;
      int disp=(*q>='0'&&*q<='9')?atoi(q):LD_VITALS;
      if(!sceneN) storyScene(1);
      lumiCard(1.30f,disp); break; }

  /* THE PROPS GO IN LAST, and the order is the whole of their safety: the
     doors have to exist for the placer to keep three metres off every
     leaf, and the items have to have settled for it to keep clear of the
     things the player has to be able to walk up to and press E on. */
  propsInit();
  storyClearRoutes();                /* ... and then nothing stands on a route */
  propShadowBake();                  /* ... and then they darken what they stand on */
  renderUpload();                    /* ... and only now does the GPU see it */
  menuInit();
  auInit();
  auAmbient(1);
  { const char *p=strstr(platCmdline(),"-wav");
    if(p){ char path[260]="mix.wav"; p+=4; while(*p==' ')p++;
           if(*p&&*p!='-'){ int i=0; while(*p&&*p!=' '&&i<255) path[i++]=*p++;
                            path[i]=0; }
           void auRenderWav(const char*,float);
           auRenderWav(path,14.2f);      /* the bench runs to 13.6 s now */
           /* AND THEN IT STOPS, which it did not.  -wav wrote the file and
              fell through into the game, so the one instrument whose whole
              output is a file sat there afterwards holding a window open
              and playing the ship at whoever ran it.  Every other
              instrument in game.c quits when it has its answer. */
           platQuit(); } }
  platLog("boot   ok");
  return 1;
}

void appResize(int w,int h){ (void)w;(void)h; }

void appKey(int vk,int down){
  if(vk<0||vk>255) return;
  keys[vk]=(unsigned char)down;
  /* WHILE THE CARD IS UP IT OWNS THE KEYBOARD.  Every one of the game's
     own keys -- the torch, noclip, the weapon slots, E -- would otherwise
     fire behind a black screen at a player who has not started yet. */
  if(menuActive()){ menuKey(vk,down); return; }
  if(!down) return;
  /* THE END OF THE GAME OWNS THE KEYBOARD THE WAY THE CARD DOES.  Without
     this the only key that did anything on the credits screen was Escape,
     and what Escape does is quit. */
  if(storyEndMenu()){
    if(vk==VK_RETURN||vk==VK_SPACE||vk=='E') appRestart();
    else if(vk==VK_ESCAPE) platQuit();
    return;
  }
  switch(vk){
  case VK_ESCAPE: if(platLocked) platSetLock(0); else platQuit(); break;
  case 'F': R_FLASH=!R_FLASH; break;
  case 0x54: R_DEBUG=(R_DEBUG+1)%10; break;  /* T -- 8 is the IBL term,
                                                9 the raw probe */
  case 'N': noclip=!noclip; break;
  case 'M': auMute(!auMuted()); auAmbient(!auMuted()); break;
  case 'R': vmwReload(); break;
  case 'E': storyInteract(); break;
  case 'G': godMode=!godMode; break;
  case '1': case '2': case '3': vmwSelect(vk-'0'); break;
  }
}
void appChar(int ch){ (void)ch; }

/* the cursor is tracked whether the card is up or not: the ending has a
   button too, and it is not the card's */
static float curMX=-1, curMY=-1;
void appCursor(float x,float y){ curMX=x; curMY=y; menuCursor(x,y); }
void appMouse(float dx,float dy){
  if(menuActive()) return;
  if(storyLockLook()) return;        /* S0: you cannot look until she says so */
  P.yaw   -= dx*SENS;
  P.pitch  = clv(P.pitch - dy*SENS, -1.45f, 1.45f);
  vmwLook(dx,dy);                     /* the viewmodel lags the turn */
}
void appButton(int btn,int down){
  if(menuActive()){ if(btn==0) menuClick(down); return; }
  if(storyEndMenu()){
    if(btn==0&&down&&storyEndHit(curMX,curMY)) appRestart();
    return;
  }
  if(btn==0&&!platLocked){ if(down) platSetLock(1); return; }
  if(btn==0) vmwTrigger(down);
}
void appWheel(float d){ (void)d; }

/* ---- the overlay ---------------------------------------------------
   The previz put this in DOM and canvas2d.  What it says is the same:
   where you are, what the ship cost, and how much of the floppy is left. */
static double fpsShown=0;

static void drawHud(double t,double dt){
  (void)dt;
  hudBegin();
  char b[192];
  int z=zoneAt(P.x,P.z);

  /* ---- the crosshair: four ticks, never a dot ----------------------- */
  if(vmwArmed()){ float cx=platW*0.5f, cy=platH*0.5f;
    hudRect(cx-11,cy-1,7,2,HUD_TEAL,0.85f);
    hudRect(cx+4, cy-1,7,2,HUD_TEAL,0.85f);
    hudRect(cx-1, cy-11,2,7,HUD_TEAL,0.85f);
    hudRect(cx-1, cy+4, 2,7,HUD_TEAL,0.85f); }

  /* ---- where you are ------------------------------------------------ */
  { const char *n = z<nZone? ZONES[z].n : "OUTSIDE THE HULL";
    const char *s = z<nZone? ZONES[z].s : "no deck";
    hudRect(14,14, hudWidth(3.f,n)+24, 54, HUD_INK, 0.80f);
    hudText(26,26,3.f,n,HUD_BONE,1.f);
    _snprintf(b,sizeof b,"%s",s);
    for(char *q=b;*q;q++) if(*q>='a'&&*q<='z') *q-=32;
    hudText(26,50,1.6f,b,HUD_AMBER,1.f); }

  /* ---- what it cost -------------------------------------------------
     These two panels are instruments, not HUD.  There is a game in front
     of them now, so they answer to -dev; the numbers they carry are still
     the ones a claim about this build has to cite. */
  if(devHud){ float x=platW-14.f, y=14.f;
    static const char *K[6]={"FPS","TRIANGLES","BRUSHES","MATERIALS",
                             "LAMPS","MODULES"};
    char V[6][32];
    _snprintf(V[0],32,"%.0f",fpsShown);
    _snprintf(V[1],32,"%d",nMeshVert/3);
    _snprintf(V[2],32,"%d",nBrush);
    _snprintf(V[3],32,"%d",TL);
    _snprintf(V[4],32,"%d",nLight);
    _snprintf(V[5],32,"%d",KIT_PLACED);
    float w=190;
    hudRect(x-w,y, w, 6*18.f+16, HUD_INK, 0.80f);
    for(int i=0;i<6;i++){
      float ry=y+12+i*18.f;
      hudText(x-w+12,ry,1.6f,K[i],HUD_DIM,1.f);
      hudText(x-12-hudWidth(1.8f,V[i]),ry-1,1.8f,V[i],HUD_TEAL,1.f);
    } }

  /* ---- and how much of the floppy is left ---------------------------
     The contest is 1,474,560 bytes with the runtime counted in, so the
     only budget worth showing on screen is the exe's own. */
  if(devHud){ float w=380, x=14, y=platH-66.f;
    hudRect(x,y,w,52,HUD_INK,0.80f);
    hudText(x+12,y+10,1.6f,"1.44 MB BUDGET",HUD_BONE,1.f);
    _snprintf(b,sizeof b,"%d / 1474560",(int)EXE_BYTES);
    hudText(x+w-12-hudWidth(1.4f,b),y+11,1.4f,b,HUD_DIM,1.f);
    float f=(float)EXE_BYTES/1474560.f;
    hudRect(x+12,y+30,w-24,10,HUD_DIM,0.35f);
    hudRect(x+12,y+30,(w-24)*f,10,HUD_AMBER,0.95f); }

  /* ---- what is in your hands ---------------------------------------
     Name, rounds and heat.  The counter goes amber when the magazine is
     the last one and red on OVERHEAT, because the two ways this gun stops
     firing want to be told apart at a glance. */
  if(vmwArmed()){ float w=250, x=platW-14.f-w, y=platH-66.f;
    hudRect(x,y,w,52,HUD_INK,0.80f);
    hudText(x+12,y+9,1.8f,vmwName(),HUD_BONE,1.f);
    if(vmwOver())      _snprintf(b,sizeof b,"OVERHEAT");
    else if(vmwReloading()) _snprintf(b,sizeof b,"RELOADING");
    else if(vmwUsesMag())   _snprintf(b,sizeof b,"%d / %d",vmwMag(),vmwReserve());
    else                    _snprintf(b,sizeof b,"%d",vmwReserve());
    const float *col = vmwOver() ? HUD_AMBER
                     : (vmwUsesMag()&&vmwMag()==0) ? HUD_AMBER : HUD_TEAL;
    hudText(x+w-12-hudWidth(2.6f,b),y+28,2.6f,b,col,1.f);
    float h=vmwHeat();
    if(h>0.02f){
      hudRect(x+12,y+34,110,6,HUD_DIM,0.35f);
      hudRect(x+12,y+34,110*h,6,vmwOver()?HUD_AMBER:HUD_TEAL,0.95f);
    } }

  /* -font: the Hangul machine on a card, so a stroke that is wrong can be
     found by looking rather than by playing to the line that uses it --
     and, under it, the Japanese sheet, for the same reason and one more:
     a glyph tools/jafont.py never rasterised draws as a hollow box, so
     this card is also where a forgotten run of that tool confesses. */
  if(fontTest){
    static const char *J[]={
      /* あいうえお かきくけこ さしすせそ たちつてと */
      "\343\201\202\343\201\204\343\201\206\343\201\210\343\201\212 \343\201\213\343\201\215\343\201\217\343\201\221\343\201\223 \343\201\225\343\201\227\343\201\231\343\201\233\343\201\235 \343\201\237\343\201\241\343\201\244\343\201\246\343\201\250",
      /* アイウエオ ガギグゲゴ パピプペポ ャュョッー */
      "\343\202\242\343\202\244\343\202\246\343\202\250\343\202\252 \343\202\254\343\202\256\343\202\260\343\202\262\343\202\264 \343\203\221\343\203\224\343\203\227\343\203\232\343\203\235 \343\203\243\343\203\245\343\203\247\343\203\203\343\203\274",
      /* ルミ: 私の任務はあなたを脱出させることです。 */
      "\343\203\253\343\203\237: \347\247\201\343\201\256\344\273\273\345\213\231\343\201\257\343\201\202\343\201\252\343\201\237\343\202\222\350\204\261\345\207\272\343\201\225\343\201\233\343\202\213\343\201\223\343\201\250\343\201\247\343\201\231\343\200\202",
      /* 射出ベイ1番…空です。2番…空です。3番、4番、5番…6番。 */
      "\345\260\204\345\207\272\343\203\231\343\202\2441\347\225\252\342\200\246\347\251\272\343\201\247\343\201\231\343\200\2022\347\225\252\342\200\246\347\251\272\343\201\247\343\201\231\343\200\2023\347\225\252\343\200\2014\347\225\252\343\200\2015\347\225\252\342\200\2466\347\225\252\343\200\202",
    };
    static const char *S[]={
      "\352\260\200\353\202\230\353\213\244\353\235\274\353\247\210\353\260\224\354\202\254\354\225\204\354\236\220\354\260\250\354\271\264\355\203\200\355\214\214\355\225\230",
      "\352\263\275\354\214\215\355\213\210\353\262\237\354\240\254\354\235\212\355\225\245\354\227\206\354\230\263\352\260\222",
      "\352\265\254\354\266\225\352\270\260\354\230\254\353\246\204\354\231\200\354\233\214\354\234\204\354\235\230\355\235\254",
      "\353\243\250\353\257\270: \354\240\200\354\235\230 \354\236\204\353\254\264\353\212\224 \353\213\271\354\213\240\354\235\204 \355\203\210\354\266\234\354\213\234\355\202\244\353\212\224 \352\262\203\354\236\205\353\213\210\353\213\244.",
      "\354\212\254\353\241\257 6\353\262\210... \354\266\251\354\240\204\354\234\250 51%\354\227\220\354\204\234 \354\240\225\354\247\200. WARN",
    };
    float cy=platH*0.14f;
    hudRect(60,cy,platW-120.f,520,HUD_INK,0.92f);
    for(int i=0;i<5;i++)
      krText(90,cy+24+i*52.f,i<3?38.f:26.f,S[i],
             i==4?HUD_AMBER:HUD_BONE,1.f);
    hudRect(90,cy+292,platW-180.f,1,HUD_DIM,0.5f);
    for(int i=0;i<4;i++)
      krText(90,cy+312+i*50.f,i<2?34.f:26.f,J[i],
             i==3?HUD_AMBER:HUD_BONE,1.f);
  }

  /* THE STORY'S OVERLAY IS PART OF THIS BATCH.  hudEnd() uploads and draws
     what has been queued and then the queue is gone, so anything the
     director wants on the screen has to be queued before it. */
  storyHUD();
  /* and the card and the lids go over all of it -- queued last so they
     are on top, and still before hudEnd(), which is where the batch ends */
  menuDraw();

  hudEnd();

  static double tick=0; static int frames=0;
  frames++;
  if(t-tick>0.35){ fpsShown=frames/(t-tick); tick=t; frames=0; }
}

/* The basis the weapon reasons in.  It is the camera's, not the player's:
   a gun held in view space needs right/up/forward, and the pitch is in
   them.  lookAtM builds the same three vectors, so they are spelled the
   way it spells them -- right = normalize(cross(forward,worldUp)). */
static void camBasis(VmwCtx *C,float yaw,float pitch){
  float cp=cosf(pitch), sp=sinf(pitch), sy=sinf(yaw), cy=cosf(yaw);
  C->eye[0]=P.x; C->eye[1]=P.y+curEye+P.bob+stepOff; C->eye[2]=P.z;
  C->camF[0]=sy*cp; C->camF[1]=sp;  C->camF[2]=cy*cp;
  C->camR[0]=-cy;   C->camR[1]=0;   C->camR[2]=sy;
  C->camU[0]=-sy*sp;C->camU[1]=cp;  C->camU[2]=-cy*sp;
}


/* ---- -auto N -------------------------------------------------------
   THE ROUTE HAS TO BE WALKABLE AND THE ONLY PROOF IS WALKING IT.  The
   director can be verified event by event on a table, but whether a
   player can actually get from the crew quarters to the escape gallery
   through 34 doors and 19 corridors is a property of the built ship, and
   the only instrument that can answer it is a player.

   So this is one: it asks the story where it is pointing, asks the nav
   lattice for the next node toward it, walks, shoots whatever gets inside
   nine metres, and presses E on anything it is standing on.  It reports
   what it reached and what it never did.
   ------------------------------------------------------------------- */
static float autoT=0, autoLog=0, autoStuck=0, autoSX=0, autoSZ=0;
static int   autoFireT=0;

static void autopilot(float dt){
  autoT+=(float)dt;
  if(storyLockMove()) return;

  float tgt[2]; int kind=storyAutoTarget(tgt);
  if(kind){
    zomGoal(tgt[0],0.f,tgt[1]);
    float dir[2];
    if(!zomStepTo(P.x,P.y,P.z,dir)){
      float dx=tgt[0]-P.x, dz=tgt[1]-P.z, d=hypot2(dx,dz);
      if(d<1e-3f) d=1;
      dir[0]=dx/d; dir[1]=dz/d;
    }
    float want=atan2f(dir[0],dir[1]);
    /* turn toward the route, but let the guns point at what is closest */
    float bp[4]={0,0,0,-1};
    int z=zomNearest(P.x,P.z,bp);
    if(z>=0&&bp[3]<9.f) want=atan2f(bp[0]-P.x,bp[2]-P.z);
    float d=want-P.yaw;
    while(d>3.14159f) d-=6.2832f;
    while(d<-3.14159f) d+=6.2832f;
    float lim=3.4f*(float)dt;
    P.yaw += d>lim?lim:(d<-lim?-lim:d);
    /* AIM DOWN.  This animal stands 0.8 m at the shoulder with its head out
       in FRONT rather than up top -- the whole reason it was designed that
       way -- so a robot that holds the horizon empties three hundred rounds
       over the top of a room full of them and reports nothing killed. */
    float wantPitch=0.f;
    if(z>=0&&bp[3]<12.f){
      float eh=P.y+curEye;
      wantPitch=atan2f((bp[1]+0.45f)-eh, bp[3]>0.2f?bp[3]:0.2f);
    }
    P.pitch += (wantPitch-P.pitch)*(float)dt*6.f;

    /* WALK ALONG THE ROUTE, NOT ALONG THE CAMERA.  The head turns at three
       radians a second and the feet do not have to wait for it -- which is
       also how a person walks through a door while looking at something
       else, and it is the difference between this reaching the gallery and
       standing in the bunkroom doorway for four minutes. */
    if(!(z>=0&&bp[3]<3.2f)){
      float sp=4.2f*(float)dt;
      float mx=dir[0]*sp, mz=dir[1]*sp;
      float ox=P.x, oz=P.z;
      moveHorizontal(mx,mz);
      if(hypot2(P.x-ox,P.z-oz)<sp*0.3f){
        /* Wedged on a doorframe.  A sidestep that alternates every second
           just undoes itself, so it commits to one side for a while and
           only tries the other if that side is no good either. */
        static float side=1.f, sideT=0;
        sideT+=(float)dt;
        if(sideT>1.6f){ sideT=0; side=-side; }
        moveHorizontal(-dir[1]*sp*side, dir[0]*sp*side);
      }
    }
    if(z>=0&&bp[3]<9.f&&vmwArmed()){
      /* HOLD THE TRIGGER, DON'T TAP IT.  The rotary cannon will not fire
         until its barrels are up to speed, so a three-frame pulse spins it
         and stops -- a hundred seconds of that killed nothing at all.  Hold
         it down, and let go for three frames in forty so a semi-automatic
         can cycle. */
      autoFireT++;
      vmwTrigger((autoFireT%40)>=3);
    } else vmwTrigger(0);
  }
  if(storyAutoInteract()) storyInteract();

  /* progress, once a second, and a stall report if it stops making any */
  autoLog+=(float)dt;
  if(autoLog>1.f){
    autoLog=0;
    if(hypot2(P.x-autoSX,P.z-autoSZ)<0.6f) autoStuck+=1.f; else autoStuck=0;
    /* Ten seconds without progress is not a player being bad at doors, it
       is a route that does not exist.  Say so, then step onto the lattice
       node the field wants and carry on, so one bad doorway does not hide
       everything downstream of it. */
    if(autoStuck>=10.f){
      float dir[2];
      platLog("auto   STALL at %.2f %.2f -- stepping onto the route",P.x,P.z);
      if(zomStepTo(P.x,P.y,P.z,dir)){ P.x+=dir[0]*1.2f; P.z+=dir[1]*1.2f; }
      autoStuck=0;
    }
    autoSX=P.x; autoSZ=P.z;
    platLog("auto   t %5.1f  at %6.2f %6.2f  target %d (%6.2f %6.2f)  "
            "hp %3.0f  enemies %d%s",
            autoT,P.x,P.z,kind,tgt[0],tgt[1],gamePlayerHP(),zomLive(),
            autoStuck>6.f?"  STALLED":"");
  }
  if(autoT>(float)autoPlay){
    platLog("auto   finished at %.1f s, %d killed",autoT,zomKills());
    platQuit();
  }
}

/* ---- frame --------------------------------------------------------- */
void appFrame(double t,double dt){
  float px=P.x, pz=P.z;
  menuStep((float)dt);
  /* NOTHING MOVES BEHIND THE CARD.  The world is still built, lit and
     drawn every frame -- the driver's first upload and the shader cache
     want somewhere to happen, and they may as well happen here -- but the
     player is not simulated and the director is not stepped, so the story
     starts on the frame the eyes finish opening rather than four seconds
     into a title screen. */
  if(!menuActive()) simulate((float)dt);
  moveSpeedNow = hypot2(P.x-px,P.z-pz)/(dt>1e-4?(float)dt:1e-4f);

  /* THE WEAPON STEPS BEFORE THE CAMERA IS BUILT, because recoil is a move
     of the view and not a decoration on it: the shot climbs the pitch, and
     the frame that follows has to be aimed where the climb left it. */
  static VmwCtx C;
  camBasis(&C,P.yaw,P.pitch);
  C.feetY=P.y; C.stepPhase=stepPhase; C.moveSpeed=moveSpeedNow;
  C.elapsed=(float)t; C.aspect=(float)platW/(float)platH;
  C.grounded=P.grounded; C.crouch=P.crouch; C.flashlight=R_FLASH;
  C.pitch=P.pitch; C.yaw=P.yaw;
  vmwStep(&C,(float)dt,(float)dt);
  P.pitch=clv(C.pitch,-1.45f,1.45f); P.yaw=C.yaw;
  shakeAmp += C.shake; if(shakeAmp>3.2f) shakeAmp=3.2f;
  shakeAmp -= (float)dt*6.5f; if(shakeAmp<0) shakeAmp=0;
  camBasis(&C,P.yaw,P.pitch);

  float kx=0,ky=0;
  if(shakeAmp>0.001f){
    float a=shakeAmp*shakeAmp*0.010f;
    kx=((float)rand()/RAND_MAX-0.5f)*a; ky=((float)rand()/RAND_MAX-0.5f)*a;
  }
  float yaw=P.yaw+kx, pit=clv(P.pitch+ky,-1.5f,1.5f);
  float eye[3]={P.x, P.y+(curEye+P.bob)*storyCamY()+stepOff, P.z};
  float cp=cosf(pit), sp=sinf(pit);
  float dir[3]={ sinf(yaw)*cp, sp, cosf(yaw)*cp };
  float ctr[3]={eye[0]+dir[0], eye[1]+dir[1], eye[2]+dir[2]};
  /* THE DECK GOES OVER, so the camera's up does.  The list is applied
     to the camera rather than to the world: rolling 6,050 brushes would
     cost the whole bake, and a horizon that is not level is the entire
     read anyway. */
  float rl=storyCamRoll();
  float up[3]={ -sinf(rl)*cosf(yaw)*0+sinf(rl)*cosf(yaw), cosf(rl), sinf(rl)*sinf(yaw) };
  { float rx=-cosf(yaw), rz=sinf(yaw);
    up[0]=rx*sinf(rl); up[1]=cosf(rl); up[2]=rz*sinf(rl); }
  static FrameState F;
  perspectiveM(F.proj, 1.134464f, (float)platW/(float)platH, 0.05f, 400.f);
  lookAtM(F.view, eye, ctr, up);
  for(int k=0;k<3;k++){ F.camPos[k]=eye[k]; F.camDir[k]=dir[k]; }
  /* THE FLOOR UNDER THE AMBIENT IS NOT NEUTRAL ANY MORE.  0.046 grey is
     the colour of nothing, and it washed out the one thing the palette is
     built on: shiplights.py hangs amber (1.00,0.52,0.14) over every
     piece of machinery in the ship, and a neutral floor under it turns
     that into a grey room with orange dots in it.  Same luminance, pushed
     to sodium, so the amber lamps read as the colour the room already is
     and the cyan accents stay the only cold thing in it. */
  /* AND LESS OF IT, BECAUSE THE LAMPS DO THE WORK NOW.  0.058 was set when
     the only specular in the ship was one blurred lobe off the lightmap
     and the ambient floor was carrying the whole mid-tone; sixteen of the
     ship's own lamps now put real light and real highlights on what is
     near them, so the floor's job is only to keep an unlit corner from
     being pure black.  0.038 is a third off it, and what a third off it
     buys is the thing Doom 3 is actually made of: the dark between two
     lamps is DARK, so the pool under each one is a place rather than a
     brighter part of an evenly grey corridor.  It is the same sodium; it
     is not a colour change. */
  F.minLight[0]=0.038f; F.minLight[1]=0.027f; F.minLight[2]=0.019f;
  /* A LITTLE BLUE IN THE AIR.  The fog was warm -- 0.030/0.026/0.022, a
     shade of the lamps -- so distance in this ship read as haze from the
     same tungsten that lit the near field, and everything flattened into
     one amber.  A cold far field is what gives a warm lamp something to
     be warm against, and it is most of what depth costs. */
  /* AND A LITTLE MORE AIR IN IT.  0.0155 with a blue fog is a clean
     vacuum: the far end of a corridor was as sharp as the near end, which
     is the one thing a derelict is not.  0.021 puts a third of the way to
     the fog colour at twenty metres and half at forty -- enough to lose a
     doorway in, not enough to lose a spider in -- and the colour follows
     the ambient off blue, because the fog IS the ambient once it is more
     than a few metres of it. */
  /* the fog has to come down with the floor it is made of, or the far end
     of a corridor is brighter than the near end of it */
  F.fogCol[0]=0.021f; F.fogCol[1]=0.017f; F.fogCol[2]=0.015f;
  F.fogDensity=0.0210f; F.time=(float)t; F.style=1.0f;
  F.flash=R_FLASH; F.dynN=0; F.vmFlash=0;

  storyFrame(&F);                    /* her lamp, and the emergency lighting */
  auListener(eye[0],eye[1],eye[2]);
  fxStep((float)dt);
  if(!menuActive()) storyStep((float)dt);
  if(autoPlay) autopilot((float)dt);
  zomTarget(P.x,P.y,P.z,1);
  zomStep((float)dt);
  hurtT-=(float)dt*1.6f; if(hurtT<0) hurtT=0;
  entReset();
  zomDraw();                         /* the animals, into the entity buffer */
/* -prop N: ONE PROP ON A CARD, 1.60 m down the view vector at eye
   height and turning, so the exe's copy can be held beside the
   bench's own propview.html and looked at.  The same reason -lumi
   and -vmw exist, and a sharper one here: the geometry was already
   checked by number (19,094 faces, both sides) and it was the
   SURFACE that had never been compared at all. */
  if(propCardN>=0){
    float pe[3]; gamePlayerEye(pe);
    float pfx=sinf(P.yaw), pfz=cosf(P.yaw);
    float ph=propHeight(propCardN), ps=propSpan(propCardN);
    /* FRAMED BY THE PROP, NOT BY A CONSTANT.  A cargo container is
       2.47 m long and a wall junction box is 0.22: one distance
       photographs one of them and fills the frame with the other.
       2.1 spans puts any of the 37 in the same rectangle. */
    float pd=1.05f+ps*2.10f;
    float px2=pe[0]+pfx*pd, pz2=pe[2]+pfz*pd;
    /* AND LIT, because the bench lights its props with a 3.6 key and
       this ship lights a corner with eight per cent of a lamp: a card
       photographed in the dark compares two darknesses. */
    propCardLight(1.55f);
    propDrawAt(propCardN, px2, pe[1]-ph*0.55f, pz2,
               (float)t*0.5f, 1.f, 1.f);
  } else
  propsDraw(P.x,P.z);                /* and the furniture */
  lumiDraw();
  storyDrawWorld();
  adUpdate((float)dt,P.x,P.z,&F);
  adDraw(P.x,P.z,(float)t);
  fxPushDebris();                    /* loose boxes, into the entity buffer */
  fxLights(&F);                      /* muzzle and sparks, into eight slots */

  /* The order the browser draws in, and every step of it earns its place:
     the glass has to sit behind the brass and in front of the world, and
     the viewmodel comes last because it clears the depth buffer to own its
     own range -- anything drawn after it would test against nothing. */
  renderBeginScene(&F);
  entDraw(&F,0);
  faceDraw(&F);                      /* her display, on its own patch */
  renderGlass();
  fxDrawDecals(&F,(float)dt);        /* laid on what they landed on */
  atmosDraw(&F);
  vmwDrawShells(&F,F.proj,F.view);
  storyDrawGuns(&F,F.proj,F.view);   /* a weapon on the deck is the real gun */
  fxDrawParticles(&F);
  vmwDraw(&C);
  renderEndScene(&F);
  drawHud(t,dt);

  /* THE PROBE HAS TO RUN AFTER THE DOORS HAVE MOVED.  A leaf is an entity
     and it animates over two seconds; printed at boot every doorway in the
     ship reads as a wall, which is the opposite of the question being
     asked. */
  if(probeArg){
    static int pf=0;
    if(++pf>90){ storyProbe(probeArg); platQuit(); }
  }
  if(benchN){
    /* A FRAME RATE IS MEASURED WITH VSYNC OFF, or it is a measurement of the
       monitor.  The first thirty frames are thrown away: they carry the
       shader cache, the first upload of a 217 MB buffer and the driver's own
       warm-up, none of which happen again. */
    static int bf=0; static double b0=0;
    bf++;
    if(bf==30) b0=t;
    if(bf>=30+benchN){
      double el=t-b0;
      platLog("bench  %d frames in %.2f s = %.1f fps  (%.2f ms)  %d tris",
              benchN, el, benchN/el, el*1000.0/benchN, nMeshVert/3);
      platQuit();
    }
  }
  if(shotFire){                          /* -fire: one round, so the flash is
                                            in the frame the shutter takes */
    if(shotFrame==1) vmwTrigger(1);
    if(shotFrame==2) vmwTrigger(0);
  }
  /* -hold N holds the shutter N frames longer.  The flash of a shot is
     gone in a fifteenth of a second and what it leaves -- the crater, the
     sparks arcing off the plate, the smoke -- is what the frame after it
     is for; three frames is the muzzle and nothing else. */
  if(shotMode && ++shotFrame>=3+shotHold){
    glFinish();
    shotGrab(shotPath);
    platQuit();
  }

  static double tick=0; static int frames=0;
  frames++;
  if(t-tick>0.5){
    char b[192];
    int z=zoneAt(P.x,P.z);
    _snprintf(b,sizeof b,"Play Lumi  |  %.0f fps  |  %s  |  %.1f %.1f %.1f%s",
              frames/(t-tick), z<nZone?ZONES[z].n:"OUTSIDE",
              P.x,P.y,P.z, noclip?"  [noclip]":"");
    platTitle(b);
    tick=t; frames=0;
  }
}
