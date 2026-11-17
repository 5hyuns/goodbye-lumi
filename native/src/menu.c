/* ============================================================
   THE TITLE, AND THE EYES OPENING

   THE GAME USED TO START WITH YOU ALREADY IN IT.  appInit built
   the ship and the very next frame was the crew quarters with
   the camera 0.35 m off the deck and the drone already coming
   down the room -- no title, no name, nothing between the
   desktop and a dark corridor.  Two things were missing and
   they are the same thing: the player has to be TOLD what they
   are about to play, and the moment they say yes has to be the
   moment the character wakes up.

   So: a black card with the name on it, a start, and a language
   -- and pressing start does not cut to the game, it OPENS THE
   PLAYER'S EYES.  S0 already had the body on the floor and the
   head rolled over; what it never had was the lids.  menuLids()
   is a heavy, four-blink wake that runs for 3.2 seconds while
   the director holds, and the story's first frame is the frame
   the eyes finish opening on.

   AND EVERY STRING IN THE GAME ANSWERS TO ONE FLAG.  menuLang()
   is read by the quest banner, the prompts, the subtitle player
   and this card.  Korean is the default because the story was
   written in Korean; English is a table beside it, never a
   substitute for it.
   ============================================================ */
#include "gl33.h"
#include "app.h"
#include "hud.h"
#include "kfont.h"
#include "menu.h"
#include "audio.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef VK_RETURN
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_SPACE  0x20
#define VK_LEFT   0x25
#define VK_UP     0x26
#define VK_RIGHT  0x27
#define VK_DOWN   0x28
#endif

/* the wake.  Four openings, three of which fall shut again -- the curve is
   the point: a clean fade-in reads as a camera, and a lid that gets most
   of the way up and then loses it reads as a person. */
#define LID_S 3.20f
static const float LID_K[][2] = {
  {0.00f,1.00f},{0.34f,0.60f},{0.58f,1.00f},
  {1.00f,0.28f},{1.24f,0.82f},
  {1.78f,0.06f},{1.94f,0.34f},
  {2.55f,0.00f},{3.20f,0.00f},
};

static struct {
  int   state;                 /* 0 title, 1 options, 2 waking, 3 gone */
  int   lang, sel;
  float t, wake;
  float glow;
  int   hold;                  /* -wake N: the lids do not move */
} M;

void menuInit(void){
  memset(&M,0,sizeof M);
  M.lang=LANG_KO;
  /* -en / -ko skip the card entirely, which is what every instrument in
     the exe wants: -shot, -auto, -bench and -scene must not sit on a
     title screen waiting for a keypress that is never coming. */
  if(platArg("-en")) M.lang=LANG_EN;
  if(platArg("-ja")) M.lang=LANG_JA;
  if(platArg("-shot")||platArg("-auto")||platArg("-bench")||platArg("-scene")
     ||platArg("-probe")||platArg("-font")||platArg("-lumi")||platArg("-vmw")
     ||platArg("-mats")||platArg("-spider")||platArg("-cam")||platArg("-wav")
     ||platArg("-prop"))
    M.state=3;
  /* -title photographs the card and -wake N the lids N tenths of a second
     into the wake, because a thing the player sees for four seconds at the
     very start of the game is exactly the thing that has to be checked
     from a picture rather than from the code. */
  if(platArg("-title")){ M.state=0; if(platArg("-options")) M.state=1; }
  { const char *p=strstr(platCmdline(),"-wake");
    if(p){ M.state=2; M.wake=(float)atoi(p+5)*0.1f; M.hold=1; } }
}

/* BACK TO THE CARD.  The ending needs somewhere to go, and the only
   correct somewhere is the frame the game started on -- same card, same
   language, lids shut.  Everything else about a new run is storyInit's. */
void menuReset(void){
  int lang=M.lang;
  memset(&M,0,sizeof M);
  M.lang=lang;
  platSetLock(0);
}
int   menuLang(void){ return M.lang; }
int   menuActive(void){ return M.state<2; }      /* the story is held */
int   menuOver(void){ return M.state>=3; }
const char *menuPick(const char *ko,const char *en,const char *ja){
  if(M.lang==LANG_EN) return en?en:ko;
  if(M.lang==LANG_JA) return ja?ja:ko;
  return ko;
}

/* 0 open, 1 shut.  Sampled off the keyframes above; before the wake
   starts the lids are simply down, which is what the title card is. */
float menuLids(void){
  if(M.state<2) return 1.f;
  if(M.state>=3) return 0.f;
  float t=M.wake;
  int n=(int)(sizeof LID_K/sizeof LID_K[0]);
  for(int i=0;i<n-1;i++){
    if(t>=LID_K[i][0]&&t<=LID_K[i+1][0]){
      float u=(t-LID_K[i][0])/(LID_K[i+1][0]-LID_K[i][0]);
      u=u*u*(3.f-2.f*u);
      return LID_K[i][1]+(LID_K[i+1][1]-LID_K[i][1])*u;
    }
  }
  return 0.f;
}

void menuStep(float dt){
  M.t+=dt;
  if(M.state==2&&!M.hold){
    M.wake+=dt;
    if(M.wake>=LID_S) M.state=3;
  }
}

static void start(void){
  if(M.state<2){ M.state=2; M.wake=0; platSetLock(1); }
}

void menuKey(int vk,int down){
  if(!down||M.state>=2) return;
  if(M.state==0){
    if(vk==VK_RETURN||vk==VK_SPACE){ auUI(UI_OK);
                                     if(M.sel==0) start(); else M.state=1; }
    else if(vk==VK_ESCAPE) platQuit();     /* the card is also the way out */
    else if(vk==VK_UP||vk==VK_DOWN||vk=='W'||vk=='S'){ M.sel^=1; auUI(UI_MOVE); }
  } else {
    if(vk==VK_ESCAPE||vk==VK_RETURN||vk==VK_SPACE){ M.state=0; auUI(UI_BACK); }
    /* THREE LANGUAGES, SO A TOGGLE IS A CYCLE.  `M.lang^=1` was right
       while there were two of them and is a silent no-op on the third:
       left and right now walk the list in opposite directions, which is
       what left and right have always promised on a row like this. */
    else if(vk==VK_LEFT||vk=='A'){ M.lang=(M.lang+LANG_N-1)%LANG_N; auUI(UI_MOVE); }
    else if(vk==VK_RIGHT||vk=='D'){ M.lang=(M.lang+1)%LANG_N; auUI(UI_MOVE); }
  }
}

/* THE CARD IS CLICKABLE, because a start button that is not is a lie.
   plat.c hands the cursor position through on every move whether it is
   locked or not, and the two hit rectangles are the two the draw below
   lays out -- one place for the geometry, and it is here. */
static float hitY0[2], hitY1[2], hitX0, hitX1;
static float curX=-1, curY=-1;
void menuCursor(float x,float y){ curX=x; curY=y; }
void menuClick(int down){
  if(!down||M.state>=2) return;
  for(int i=0;i<2;i++)
    if(curX>hitX0&&curX<hitX1&&curY>hitY0[i]&&curY<hitY1[i]){
      M.sel=i;
      if(M.state==0){ auUI(UI_OK); if(i==0) start(); else M.state=1; }
      else if(i==0){ M.lang=(M.lang+1)%LANG_N; auUI(UI_MOVE); }
      else { M.state=0; auUI(UI_BACK); }
      return;
    }
}

/* ---- the card ------------------------------------------------------- */
static const float C_TITLE[3]={0.86f,0.88f,0.90f};
static const float C_SUB  [3]={0.42f,0.79f,0.94f};
static const float C_DIMM [3]={0.36f,0.36f,0.38f};
static const float BLACK  [3]={0.f,0.f,0.f};

/* each language in its own script, in LANG_* order */
static const char *const LANGV[LANG_N]={
  "\355\225\234\352\265\255\354\226\264",                       /* 한국어 */
  "ENGLISH",
  "\346\227\245\346\234\254\350\252\236",                       /* 日本語 */
};

static void row(float cx,float y,float w,float h,int on,
                const char *ko,const char *en,const char *ja,const char *val){
  hudRect(cx-w*0.5f,y,w,h,HUD_INK,on?0.92f:0.55f);
  hudRect(cx-w*0.5f,y,3,h,HUD_AMBER,on?1.f:0.28f);
  const float *c = on?HUD_BONE:C_DIMM;
  krText(cx-w*0.5f+26,y+h*0.5f-13.f,24.f,menuPick(ko,en,ja),c,1.f);
  /* THE VALUE GOES THROUGH krText AND NOT hudText, because the only value
     on this card is a language and a language names itself in its own
     script: 한국어, ENGLISH, 日本語.  Spelling all three in the Latin
     stencil was the one place the picker could not say what it picks. */
  if(val) krText(cx+w*0.5f-krWidth(20.f,val)-26,y+h*0.5f-11.f,
                 20.f,val,on?HUD_AMBER:C_DIMM,1.f);
}

void menuDraw(void){
  float W=(float)platW, H=(float)platH;

  /* ---- the lids.  Twenty rows a side with a cosine belly, so what
     closes over the frame is an EYE and not a letterbox.  They are
     drawn for the title too: the card is what you see through shut
     eyes, which is why the card is black. */
  float lid=menuLids();
  if(lid>0.0005f){
    const int N=44;
    for(int i=0;i<N;i++){
      float u0=(float)i/N, u1=(float)(i+1)/N;
      float xa=u0*W, xb=u1*W;
      /* THE MIDDLE OF THE FRAME OPENS FIRST, which is the whole shape of
         an eye: a lid that comes down evenly is a letterbox, and one that
         keeps its corners while the centre parts is an eye.  Forty-four
         columns, because at twenty the profile reads as a stair. */
      float m=0.5f*(u0+u1);
      float belly=1.00f-0.36f*sinf(m*3.14159f);
      float d=H*0.54f*lid*belly;
      hudRect(xa,0,xb-xa+1.f,d,BLACK,1.f);
      hudRect(xa,H-d,xb-xa+1.f,d+1.f,BLACK,1.f);
    }
    /* and the last of it as a flat wash, so a half-open lid still
       darkens the middle of the frame rather than framing it */
    if(lid>0.55f) hudRect(0,0,W,H,BLACK,(lid-0.55f)/0.45f);
  }
  if(M.state>=2) return;                    /* waking: lids and nothing else */

  float cx=W*0.5f;
  float tp = W/1280.f; if(tp<0.6f) tp=0.6f; if(tp>2.2f) tp=2.2f;

  /* ---- the name.  Latin, in the 5x7 stencil the rest of the ship is
     lettered in, because the ship's own signage is Latin and a title in
     a different hand belongs to a different game. */
  { const char *t="GOODBYE LUMI";
    float px=9.5f*tp;
    float w=hudWidth(px,t);
    /* a slow breath on the L, which is her: the title says her name and
       the name is the thing that goes out at the end */
    float br=0.72f+0.28f*sinf(M.t*0.9f);
    hudText(cx-w*0.5f,H*0.26f,px,t,C_TITLE,0.92f);
    hudRect(cx-w*0.5f,H*0.26f+px*7.6f+10.f,w,2,C_SUB,br*0.7f); }
  { const char *s=menuPick("\352\263\240\353\246\275\353\220\234 \354\204\240"
                           "\353\260\225 47\352\265\254\354\227\255",
                           "DERELICT  \302\267  SECTOR 47",
                           /* 漂流船 ・ 47区画 */
                           "\346\274\202\346\265\201\350\210\271 \343\203\273"
                           " 47\345\214\272\347\224\273");
    float w=krWidth(19.f*tp,s);
    krText(cx-w*0.5f,H*0.26f+9.5f*tp*7.6f+26.f,19.f*tp,s,C_SUB,0.72f); }

  float bw=380.f*tp, bh=54.f*tp, y0=H*0.56f;
  hitX0=cx-bw*0.5f; hitX1=cx+bw*0.5f;
  for(int i=0;i<2;i++){ hitY0[i]=y0+i*(bh+12.f*tp); hitY1[i]=hitY0[i]+bh; }

  if(M.state==0){
    row(cx,hitY0[0],bw,bh,M.sel==0,"\352\262\214\354\236\204 \354\213\234"
        "\354\236\221","START",
        "\343\202\262\343\203\274\343\203\240\351\226\213\345\247\213",0);/* ゲーム開始 */
    row(cx,hitY0[1],bw,bh,M.sel==1,"\354\204\244\354\240\225","OPTIONS",
        "\350\250\255\345\256\232",0);                          /* 設定 */
    const char *h=menuPick("W/S \354\204\240\355\203\235   ENTER \355\231\225"
                           "\354\235\270","W/S SELECT   ENTER CONFIRM",
                           /* W/S 選択   ENTER 決定 */
                           "W/S \351\201\270\346\212\236   ENTER \346\261\272"
                           "\345\256\232");
    float w=krWidth(15.f*tp,h);
    krText(cx-w*0.5f,hitY0[1]+bh+26.f*tp,15.f*tp,h,C_DIMM,0.9f);
  } else {
    row(cx,hitY0[0],bw,bh,M.sel==0,"\354\226\270\354\226\264","LANGUAGE",
        "\350\250\200\350\252\236",                             /* 言語 */
        LANGV[M.lang]);
    row(cx,hitY0[1],bw,bh,M.sel==1,"\353\222\244\353\241\234","BACK",
        "\346\210\273\343\202\213",0);                          /* 戻る */
    const char *h=menuPick("A/D \353\263\200\352\262\275   ESC \353\222\244"
                           "\353\241\234","A/D CHANGE   ESC BACK",
                           /* A/D 変更   ESC 戻る */
                           "A/D \345\244\211\346\233\264   ESC \346\210\273"
                           "\343\202\213");
    float w=krWidth(15.f*tp,h);
    krText(cx-w*0.5f,hitY0[1]+bh+26.f*tp,15.f*tp,h,C_DIMM,0.9f);
  }
}
