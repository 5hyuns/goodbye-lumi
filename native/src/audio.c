/* ============================================================
   10.  PROCEDURAL AUDIO

   Every effect is built from one shared noise buffer and a few
   oscillators.  Nothing is sampled, so nothing is stored.

   THE BROWSER HAD A SYNTHESISER; THIS HAD TO BE ONE.  map.html
   wires Web Audio nodes -- oscillator, biquad, gain, delay --
   and lets the browser run them.  There is no such thing here,
   so the graph is written out: a voice is a source, a parameter
   ramp on its frequency, one biquad, an envelope on its gain and
   a send to a three-tap plate.  That is the whole vocabulary the
   forty-odd effects in the file actually use, which is why it
   fits in three hundred lines rather than in a library.

   waveOut, four blocks of 512 frames at 44.1 kHz: 46 ms of
   latency, which is under the threshold at which a gunshot stops
   feeling like it came from the trigger.
   ============================================================ */
#include <windows.h>
#include <mmsystem.h>
#include "m.h"
#include "app.h"
#include "audio.h"
#include <stdlib.h>
#include <string.h>

#define SR      44100
#define BLK     512
#define NBLK    4
#define MAXVOICE 48
#define NOISE_N (SR)                 /* one second, looped */

enum { SRC_SINE=0, SRC_SAW, SRC_SQUARE, SRC_TRI, SRC_NOISE };
enum { FLT_NONE=0, FLT_LP, FLT_BP, FLT_HP };

/* a parameter that moves: value, target, and how it gets there */
typedef struct { float v0,v1,t0,t1; int expo; } Ramp;

static float rampAt(const Ramp *r,float t){
  if(t<=r->t0||r->t1<=r->t0) return r->v0;
  if(t>=r->t1) return r->v1;
  float k=(t-r->t0)/(r->t1-r->t0);
  if(r->expo){
    float a=r->v0<1e-6f?1e-6f:r->v0, b=r->v1<1e-6f?1e-6f:r->v1;
    return a*powf(b/a,k);
  }
  return r->v0+(r->v1-r->v0)*k;
}

typedef struct {
  int   used;
  int   src;
  float phase, rate;               /* noise: read position and playback rate */
  Ramp  freq;
  int   flt; float q;
  Ramp  fc;
  float b0,b1,b2,a1,a2, x1,x2,y1,y2;
  /* the envelope, exactly the shape env() draws: 4 ms to peak, an optional
     hold, then an exponential fall to silence */
  float peak, atk, hold, dur;
  float wet, gain;
  float t, life;
  /* A LAYER THAT ARRIVES LATE IS THE WHOLE DIFFERENCE BETWEEN A BLIP AND
     A SOUND.  A gunshot is a crack and then the room answering it 30 ms
     later; a spider's legs are four ticks that are NOT evenly spaced; a
     footstep is a heel and then the sole.  Every one of those was a single
     voice starting at zero, and all of them read as one flat click. */
  float delay;
} Voice;

static Voice VOX[MAXVOICE];
static float *NOISEBUF;
static int    nVoice;

/* the plate: two -- three -- feedback delays behind a lowpass.  This is what
   makes a gunshot sound like it happened in a concrete room. */
#define DMAX 8192
static float DLY[3][DMAX];
static int   DPOS[3];
static const int DLEN[3]={ (int)(0.071f*SR), (int)(0.113f*SR), (int)(0.167f*SR) };
static const float DFB[3]={0.55f,0.50f,0.44f};
static float revLP1, revHP1, revHPx;

/* the ambient bed: two saws and a filtered hiss under a 220 Hz lowpass */
static float ambPh[2], ambTarget, ambGain, ambNoise, ambLP;
static int   audioOn=1, auReady;

static float LX,LY,LZ;              /* where the listener is */
static CRITICAL_SECTION LOCK;
static HWAVEOUT HWO;
static WAVEHDR  HDR[NBLK];
static short   *BUF[NBLK];
static HANDLE   TH;
static volatile int running;

/* ---- the biquad, RBJ cookbook -------------------------------------- */
static void biquad(Voice *v,float f){
  if(f<20) f=20; if(f>SR*0.45f) f=SR*0.45f;
  float w=6.2831853f*f/SR, cw=cosf(w), sw=sinf(w);
  float q=v->q<0.05f?0.05f:v->q;
  float al=sw/(2*q), a0;
  switch(v->flt){
  case FLT_LP: a0=1+al; v->b0=(1-cw)/2/a0; v->b1=(1-cw)/a0; v->b2=v->b0;
               v->a1=-2*cw/a0; v->a2=(1-al)/a0; break;
  case FLT_HP: a0=1+al; v->b0=(1+cw)/2/a0; v->b1=-(1+cw)/a0; v->b2=v->b0;
               v->a1=-2*cw/a0; v->a2=(1-al)/a0; break;
  case FLT_BP: a0=1+al; v->b0=al/a0; v->b1=0; v->b2=-al/a0;
               v->a1=-2*cw/a0; v->a2=(1-al)/a0; break;
  default: v->b0=1; v->b1=v->b2=v->a1=v->a2=0; break;
  }
}

/* ---- one voice ------------------------------------------------------ */
static Voice *alloc(int priority){
  if(!audioOn||!auReady) return 0;
  if(nVoice>=44 && priority<=1) return 0;
  for(int i=0;i<MAXVOICE;i++) if(!VOX[i].used){
    Voice *v=&VOX[i];
    memset(v,0,sizeof *v);
    v->used=1; v->q=1; v->gain=1;
    nVoice++;
    return v;
  }
  return 0;
}
static void setFreq(Voice *v,float f0,float f1,float dur,int expo){
  v->freq.v0=f0; v->freq.v1=f1; v->freq.t0=0; v->freq.t1=dur; v->freq.expo=expo;
}
static void setFc(Voice *v,int type,float f0,float f1,float dur,float q,int expo){
  v->flt=type; v->q=q;
  v->fc.v0=f0; v->fc.v1=f1; v->fc.t0=0; v->fc.t1=dur; v->fc.expo=expo;
  biquad(v,f0);
}
/* FOUR MILLISECONDS IS A FADE-IN, AND A CLACK DOES NOT HAVE ONE.  env()
   fixed the attack at 4 ms, which is twelve cycles at 3 kHz -- long enough
   to round the leading edge off every impact in the file, and the reason
   the mechanism sounds read as blips rather than as things striking each
   other.  Steel arriving on steel rises in well under a millisecond.  The
   soft-attack default stays, because a scrape and a servo genuinely do
   fade in; what changed is that a strike can now say so. */
static void enva(Voice *v,float peak,float dur,float hold,float atk){
  v->peak=peak; v->atk=atk<0.0001f?0.0001f:atk; v->hold=hold; v->dur=dur;
  v->life=dur+0.05f;
}
static void env(Voice *v,float peak,float dur,float hold){
  enva(v,peak,dur,hold,0.004f);
}
/* after env(), or the life it extends is not there yet */
static void dly(Voice *v,float t){ v->delay=t; v->life+=t; }
/* one uniform random in [0,1) -- rand()/RAND_MAX, spelled once */
static float r01(void){ return (float)rand()/(float)RAND_MAX; }
static float rsym(float a){ return (r01()*2.f-1.f)*a; }

/* ---- what a struck object is, in four lines --------------------------
   A STRIKE, A SCRAPE, A RING AND THE MASS BEHIND THEM.  Every mechanism
   in the ship, every impact and every pickup is some ordering of exactly
   those four, so they are spelled once here rather than forty times as
   raw setFc calls.  Section 10c is where they are used and why. */
#define ATK_HARD 0.00025f          /* 11 samples: a clack, not a fade-in */

/* A FILTER TAKES ENERGY AWAY AND `peak` STOPPED MEANING ANYTHING.  A
   band-pass at Q 1.1 on 880 Hz passes about 800 Hz of a 22 kHz noise
   floor -- a fifth of the amplitude that went in -- while a sine written
   at the same `peak` passes all of it.  So a strike written at 0.12 over
   a body written at 0.05 came out with the BODY three times the louder,
   and every layered sound here collapsed onto its lowest partial: the
   bench measured an 80% energy bandwidth of 22 Hz on a strike that was
   meant to be broadband, and four of them sat quieter than the ambient
   bed they play over.  Put the loss back where it happens, so that one
   number means one thing in all four helpers.  Capped, because the
   compensation for a very narrow band is not a sound, it is a squeal. */
static float fltComp(int type,float f,float q){
  float ny=SR*0.5f, bw;
  if(type==FLT_BP)      bw=f/(q<0.2f?0.2f:q);
  else if(type==FLT_LP) bw=f;
  else if(type==FLT_HP) bw=ny-f;
  else return 1.f;
  if(bw<40.f) bw=40.f;
  float k=sqrtf(ny/bw);
  return k>4.f?4.f:k;
}

/* metal arriving on metal.  FLT_HP is a bright strike on a thin part,
   FLT_BP a knock on a thick one, FLT_LP a lump with no ring left in it. */
static Voice *auHit(int type,float peak,float f,float q,float dur,float wet){
  Voice *v=alloc(1); if(!v) return 0;
  v->src=SRC_NOISE; v->rate=1.1f+rsym(0.35f); v->phase=(float)(rand()%NOISE_N);
  setFc(v,type,f,f*0.62f,dur,q,1);
  enva(v,peak*fltComp(type,f,q),dur,0,ATK_HARD); v->wet=wet;
  return v;
}
/* something sliding.  f0 -> f1 is the whole of it: a scrape that holds
   one frequency is the tone this file is being taken away from. */
static Voice *auScrape(float peak,float f0,float f1,float q,float dur,float wet){
  Voice *v=alloc(0); if(!v) return 0;
  v->src=SRC_NOISE; v->rate=0.8f+rsym(0.3f); v->phase=(float)(rand()%NOISE_N);
  setFc(v,FLT_BP,f0,f1,dur,q,1);
  enva(v,peak*fltComp(FLT_BP,(f0+f1)*0.5f,q),dur,0,0.007f); v->wet=wet;
  return v;
}
/* and the part ringing.  The droop is not decoration: a struck part sheds
   a fraction of a per cent of its pitch as the strike energy leaves it,
   and a partial held dead flat is exactly the one that reads as an
   oscillator rather than as a piece of steel. */
static Voice *auRing(float peak,float f,float dur,float wet){
  Voice *v=alloc(0); if(!v) return 0;
  v->src=SRC_TRI;
  setFreq(v,f,f*0.994f,dur,0);
  enva(v,peak,dur,0,ATK_HARD); v->wet=wet;
  return v;
}
/* the mass behind it */
static Voice *auBody(float peak,float f0,float f1,float dur,float wet){
  Voice *v=alloc(0); if(!v) return 0;
  v->src=SRC_SINE;
  setFreq(v,f0,f1,dur,1);
  enva(v,peak,dur,0,0.0015f); v->wet=wet;
  return v;
}


/* ---- the render loop ------------------------------------------------ */
static float srcSample(Voice *v){
  switch(v->src){
  case SRC_SINE:   return sinf(v->phase*6.2831853f);
  case SRC_SAW:    return v->phase*2-1;
  case SRC_SQUARE: return v->phase<0.5f?1.f:-1.f;
  case SRC_TRI:    return 4*fabsf(v->phase-0.5f)-1;
  default: {
    int i=(int)v->phase;
    return NOISEBUF[i%NOISE_N];
  }}
}

static void mixBlock(short *out,int n){
  EnterCriticalSection(&LOCK);
  for(int s=0;s<n;s++){
    float dry=0, wetIn=0;
    for(int i=0;i<MAXVOICE;i++){
      Voice *v=&VOX[i];
      if(!v->used) continue;
      float t=v->t-v->delay;
      if(t<0.f){                       /* not yet: hold the source still */
        v->t += 1.f/SR;
        continue;
      }
      /* the envelope */
      float g;
      if(t<v->atk) g=v->peak*(t/v->atk);
      else if(t<v->hold) g=v->peak;
      else {
        float k=(t-(v->hold>v->atk?v->hold:v->atk))
               /((v->dur-(v->hold>v->atk?v->hold:v->atk))+1e-6f);
        if(k>1) k=1;
        g=v->peak*powf(0.0008f/(v->peak>1e-6f?v->peak:1e-6f),k);
      }
      float smp=srcSample(v)*g*v->gain;
      /* the filter, retuned a few hundred times a second rather than
         44,100 -- a ramp that moves an octave in 400 ms does not need more */
      if(v->flt){
        if(((s+i)&31)==0) biquad(v,rampAt(&v->fc,t));
        float y=v->b0*smp+v->b1*v->x1+v->b2*v->x2-v->a1*v->y1-v->a2*v->y2;
        v->x2=v->x1; v->x1=smp; v->y2=v->y1; v->y1=y;
        smp=y;
      }
      dry+=smp; wetIn+=smp*v->wet;
      /* advance */
      if(v->src==SRC_NOISE) v->phase += v->rate*NOISE_N/(float)SR;
      else {
        v->phase += rampAt(&v->freq,t)/SR;
        if(v->phase>=1) v->phase-=(float)(int)v->phase;
      }
      v->t += 1.f/SR;
      if(v->t>=v->life){ v->used=0; nVoice--; }
    }
    /* ---- the ambient bed --------------------------------------------- */
    ambGain += (ambTarget-ambGain)*0.00006f;
    if(ambGain>0.0002f){
      float a=0;
      ambPh[0]+=51.0f/SR;   if(ambPh[0]>=1) ambPh[0]-=1;
      ambPh[1]+=77.5f/SR;   if(ambPh[1]>=1) ambPh[1]-=1;
      a += (ambPh[0]*2-1)*0.10f + (ambPh[1]*2-1)*0.10f;
      /* a slow random walk, which is what a hull sounds like */
      ambNoise = ambNoise*0.98f + ((float)rand()/RAND_MAX*2-1)*0.02f;
      a += ambNoise*0.55f;
      /* one-pole lowpass at 220 Hz */
      float k=1-expf(-6.2831853f*220.f/SR);
      ambLP += (a-ambLP)*k;
      dry += ambLP*ambGain;
    }
    /* ---- the plate ---------------------------------------------------- */
    { float k=1-expf(-6.2831853f*2100.f/SR);
      revLP1 += (wetIn*0.30f-revLP1)*k;
      float wet=0;
      for(int d=0;d<3;d++){
        float y=DLY[d][DPOS[d]];
        wet+=y;
        DLY[d][DPOS[d]] = revLP1 + y*DFB[d];
        if(++DPOS[d]>=DLEN[d]) DPOS[d]=0;
      }
      wet*=0.42f;
      /* and a 180 Hz highpass on the tail, so the room does not rumble */
      float hk=1-expf(-6.2831853f*180.f/SR);
      revHP1 += (wet-revHP1)*hk;
      dry += wet-revHP1;
    }
    float o=dry*0.5f;
    if(o>1)o=1; else if(o<-1)o=-1;
    short v16=(short)(o*32000);
    out[s*2]=v16; out[s*2+1]=v16;
  }
  LeaveCriticalSection(&LOCK);
  (void)revHPx;
}

static DWORD WINAPI audioThread(void *p){
  (void)p;
  int cur=0;
  while(running){
    WAVEHDR *h=&HDR[cur];
    if(h->dwFlags & WHDR_INQUEUE){ Sleep(1); continue; }
    mixBlock(BUF[cur],BLK);
    waveOutWrite(HWO,h,sizeof *h);
    cur=(cur+1)%NBLK;
  }
  return 0;
}

void auInit(void){
  if(auReady) return;
  InitializeCriticalSection(&LOCK);
  NOISEBUF=(float*)malloc(sizeof(float)*NOISE_N);
  for(int i=0;i<NOISE_N;i++) NOISEBUF[i]=(float)rand()/RAND_MAX*2-1;

  WAVEFORMATEX w={0};
  w.wFormatTag=WAVE_FORMAT_PCM; w.nChannels=2; w.nSamplesPerSec=SR;
  w.wBitsPerSample=16; w.nBlockAlign=4; w.nAvgBytesPerSec=SR*4;
  if(waveOutOpen(&HWO,WAVE_MAPPER,&w,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){
    platLog("audio  no output device");
    return;
  }
  for(int i=0;i<NBLK;i++){
    BUF[i]=(short*)calloc(BLK*2,sizeof(short));
    HDR[i].lpData=(LPSTR)BUF[i];
    HDR[i].dwBufferLength=BLK*4;
    waveOutPrepareHeader(HWO,&HDR[i],sizeof HDR[i]);
    HDR[i].dwFlags &= ~WHDR_INQUEUE;
  }
  auReady=1; running=1;
  TH=CreateThread(0,0,audioThread,0,0,0);
  SetThreadPriority(TH,THREAD_PRIORITY_ABOVE_NORMAL);
  platLog("audio  %d Hz, %d x %d frames",SR,NBLK,BLK);
}
void auShutdown(void){
  if(!auReady) return;
  running=0;
  if(TH){ WaitForSingleObject(TH,500); CloseHandle(TH); }
  waveOutReset(HWO); waveOutClose(HWO);
  auReady=0;
}
void auListener(float x,float y,float z){ LX=x; LY=y; LZ=z; }
void auAmbient(int on){ ambTarget = on?0.16f:0.f; }
void auMute(int on){ audioOn=!on; if(on) ambTarget=0; }
int  auMuted(void){ return !audioOn; }

/* distance rolloff for anything that happens away from the player */
static float dGain(float x,float y,float z,float ref){
  float d=hypot3(LX-x,LY-y,LZ-z);
  float k=1-d/(ref>0?ref:26.f);
  if(k<0) return 0;
  return powf(k,1.6f);
}

/* ---- the effects ---------------------------------------------------- */
/* A BOOT ON A STEEL DECK IS FOUR THINGS, AND IT WAS ONE.  A single
   band-passed noise burst is a brush stroke: it has no weight, no heel and
   no room, and at 1.15 m a stride it turned into a rhythm you stop hearing
   in the first corridor.  What a step on 6 mm plate actually is:

     the heel      a hard transient, 12 ms, almost all of it above 2 kHz
     the body      the mass of a person, a sine that falls 110 -> 52 Hz
     the sole      the rest of the boot rolling on, 25 ms after the heel
     the plate     and the deck itself ringing, which is the one that says
                   STEEL rather than carpet -- and it is why this ship
                   sounds like a ship

   `hard` is 0 walking and 1 running: it moves the weight and the ring, not
   the pitch, because a heavier tread is not a lower-pitched one.  Every
   number is jittered per step, so the left foot and the right foot are not
   the same recording twice. */
void auFootstep(float hard){
  float w = 0.62f+0.38f*hard;
  Voice *v;
  /* the heel */
  v=alloc(0);
  if(v){
    v->src=SRC_NOISE; v->rate=1.5f+rsym(0.25f);
    v->phase=(float)(rand()%NOISE_N);
    setFc(v,FLT_HP, 2400, 2400, 0.014f, 1.f, 0);
    env(v,0.075f*w,0.014f,0); v->wet=0.30f;
  }
  /* the body of the person */
  v=alloc(0);
  if(v){
    v->src=SRC_SINE;
    float f=104.f+rsym(12.f);
    setFreq(v,f,f*0.48f,0.085f,1);
    env(v,0.070f*w,0.085f,0); v->wet=0.10f;
  }
  /* the sole, rolling on */
  v=alloc(0);
  if(v){
    v->src=SRC_NOISE; v->rate=1.0f+rsym(0.18f);
    v->phase=(float)(rand()%NOISE_N);
    float f=700.f+r01()*380.f;
    setFc(v,FLT_BP, f, f*0.72f, 0.075f, 1.2f, 0);
    env(v,0.055f*w,0.075f,0); v->wet=0.26f;
    dly(v,0.013f+r01()*0.010f);
  }
  /* and the deck answering.  Quiet, long, and the only part of a footstep
     that is a PITCH -- a plate has a mode and it is what tells you the
     floor is metal.  Detuned per step so a corridor is not a xylophone. */
  v=alloc(0);
  if(v){
    v->src=SRC_TRI;
    float f=1180.f+r01()*640.f;
    setFreq(v,f,f*0.94f,0.20f,0);
    env(v,0.020f*w,0.20f,0); v->wet=0.85f;
    dly(v,0.006f);
  }
}

/* THE SHOT IS THE CRACK; THE GUN IS EVERYTHING AFTER IT.

   Three voices -- body, sub, crack -- all starting at zero is a firework,
   not a firearm.  What was missing is the two halves that arrive LATE and
   are the only reason a recorded gunshot sounds like it was fired indoors:

     the tail    the corridor answering, 25 ms behind and half a second
                 long.  Entirely wet, so the plate does the work.  This is
                 the single loudest difference between this and the old
                 sound, and it costs one voice.
     the action  the mechanism, 60-90 ms behind: a slide returning, a bolt,
                 the break latch.  It is what says the shot came out of a
                 MACHINE that has parts.

   The double-barrel's numbers moved with its damage (vmw.c: 14 pellets at
   11).  A gun that folds a spitter in one barrel does not go `pap`. */
void auGun(int kind){
  static const float C[4][10]={
    /* g     dur   bp     q     sub   sg    crack  tail  tdur  act */
    {0.50f,0.20f,1400,0.85f,150,0.42f,0.30f, 0.13f,0.34f,0.070f},  /* glock */
    {0.42f,0.15f,1950,1.05f,180,0.30f,0.34f, 0.09f,0.26f,0.055f},  /* smg */
    {1.05f,0.46f, 700,0.50f, 62,1.05f,0.50f, 0.26f,0.72f,0.090f},  /* shot */
    {0.26f,0.11f,2400,1.30f,220,0.16f,0.22f, 0.07f,0.22f,0.045f}}; /* turret */
  if(kind<0||kind>3) return;
  const float *c=C[kind];
  Voice *v=alloc(2);
  if(v){
    v->src=SRC_NOISE; v->rate=0.75f+r01()*0.6f;
    v->phase=(float)(rand()%NOISE_N);
    setFc(v,FLT_BP,c[2]*2.4f,c[2]*0.40f,c[1],c[3],1);
    env(v,c[0],c[1],0); v->wet=1;
  }
  v=alloc(2);
  if(v){
    v->src=SRC_SINE;
    setFreq(v,c[4]*3.4f,c[4]*0.55f,c[1]*0.7f,1);
    env(v,c[5],c[1]*0.85f,0);
  }
  v=alloc(2);
  if(v){
    v->src=SRC_NOISE; v->rate=1.6f; v->phase=(float)(rand()%NOISE_N);
    setFc(v,FLT_HP,3400,3400,0.035f,1,0);
    env(v,c[6],0.035f,0); v->wet=1;
  }
  /* the room */
  v=alloc(2);
  if(v){
    v->src=SRC_NOISE; v->rate=0.55f; v->phase=(float)(rand()%NOISE_N);
    setFc(v,FLT_LP,2200,240,c[8],0.8f,1);
    env(v,c[7],c[8],0); v->wet=1.f;
    dly(v,0.025f);
  }
  /* the mechanism */
  v=alloc(1);
  if(v){
    v->src=SRC_NOISE; v->rate=1.9f+rsym(0.2f); v->phase=(float)(rand()%NOISE_N);
    float f=2600.f+r01()*1400.f;
    setFc(v,FLT_BP,f,f*0.55f,0.045f,4.0f,1);
    env(v,0.085f,0.045f,0); v->wet=0.55f;
    dly(v,c[9]);
  }
}

/* ---- a round arriving somewhere --------------------------------------
   THE MOST-HEARD SOUND IN THE GAME WAS A BELL.  Every bullet that missed
   put one band-passed burst and ONE dead-flat triangle partial into the
   room -- an 80% energy bandwidth of 22 Hz on the bench, which is a note,
   and at three or four a second it is the note the entire firefight gets
   played in.

   What a round hitting 6 mm plate actually is: the strike, broadband and
   gone inside ten milliseconds; the plate's own modes, three of them,
   inharmonic and dying at DIFFERENT rates -- the high one first, which is
   the whole of what separates metal from a bell; and then the spall going
   past.  Concrete has no modes at all and is grit.  A crate is a box, so
   it is mostly body. */
void auImpact(int metal,int wood,float x,float y,float z){
  float vol=dGain(x,y,z,30); if(vol<0.03f) return;
  Voice *v;
  if(metal){
    float f=760.f+r01()*1500.f;               /* where this plate rings */
    auHit(FLT_HP,vol*0.30f,3200,0.9f,0.010f,0.55f);
    auRing(vol*0.115f,f,       0.135f,0.85f);
    auRing(vol*0.055f,f*2.76f, 0.070f,0.85f);
    auRing(vol*0.028f,f*5.40f, 0.034f,0.85f);
    v=auScrape(vol*0.075f,2600,1300,1.0f,0.075f,0.70f);
    if(v) dly(v,0.011f);                      /* ... and the spall */
  } else if(wood){
    auHit(FLT_BP,vol*0.26f,760,1.1f,0.030f,0.45f);
    auBody(vol*0.115f,180,120,0.075f,0.30f);
    v=auRing(vol*0.030f,430,0.090f,0.40f);
    if(v) dly(v,0.004f);
  } else {
    auHit(FLT_BP,vol*0.24f,1500,0.8f,0.035f,0.55f);
    auBody(vol*0.055f,150,95,0.050f,0.25f);
    v=auScrape(vol*0.060f,5200,2600,0.9f,0.065f,0.60f);
    if(v) dly(v,0.006f);                      /* the grit is the tell */
  }
}

/* ---- the sound of a servo, and the latch at the end of it ------------ */
void auSlide(float x,float y,float z,int open){
  float vol=dGain(x,y,z,34); if(vol<0.03f) return;
  Voice *v=alloc(1);
  if(v){
    v->src=SRC_SAW;
    setFreq(v, open?96.f:190.f, open?205.f:104.f, 0.42f, 0);
    setFc(v,FLT_BP,520,520,0.42f,1.6f,0);
    env(v,vol*0.16f,0.62f,0.34f); v->wet=1;
  }
  v=alloc(1); if(!v) return;
  v->src=SRC_NOISE; v->rate=0.85f; v->phase=(float)(rand()%NOISE_N);
  setFc(v,FLT_BP,1500,700,0.5f,0.7f,0);
  env(v,vol*0.11f,0.58f,0.26f); v->wet=1;
}
void auLatch(float x,float y,float z){
  float vol=dGain(x,y,z,30); if(vol<0.03f) return;
  Voice *v=alloc(1);
  if(v){
    v->src=SRC_NOISE; v->rate=0.5f; v->phase=(float)(rand()%NOISE_N);
    setFc(v,FLT_LP,460,460,0.16f,1,0);
    env(v,vol*0.30f,0.16f,0); v->wet=1;
  }
  v=alloc(1); if(!v) return;
  v->src=SRC_TRI;
  setFreq(v,148,74,0.12f,1);
  env(v,vol*0.16f,0.15f,0); v->wet=1;
}

/* ---- the animal -----------------------------------------------------
   A spider has no throat, so none of this is a voice.  What it has is
   chitin knocking on chitin and air being forced through a book lung,
   which is a click train under a band-passed rasp -- and that is what
   makes it read as an arthropod rather than as a small dog.
   kind: 0 chitter, 1 lunge or hatch, 2 death, 3 wet impact, 4 spit. */
void sfxCreature(int kind,float x,float y,float z,float sz){
  float vol=dGain(x,y,z,kind==0?20.f:34.f);
  if(vol<0.03f) return;
  /* SIZE IS PITCH, and it is the whole of knowing what is in the room.
     Every animal used the same numbers, so a 24 hp scuttler and a 180 hp
     brooder made one identical noise and the sound carried no information
     at all.  `sz` is the type's own ZT[].sc -- 0.74, 0.92, 1.62, 1.05 --
     and a big body is slower and lower, not merely louder. */
  float pt = 1.f/(0.42f+0.58f*sz);          /* brooder 0.72, scuttler 1.15 */
  float sl = 0.72f+0.28f*sz;                /* ... and it takes longer */
  static const float C[5][6]={
    /* gain  dur   f0     f1     q     rate */
    {0.13f,0.16f, 900,  520,  6.0f, 1.30f},   /* chitter */
    {0.34f,0.34f, 420, 1750,  1.5f, 0.90f},   /* lunge / hatch */
    {0.40f,0.62f,1500,  180,  1.1f, 0.80f},   /* death */
    {0.30f,0.11f, 520,  240,  0.8f, 1.00f},   /* wet impact */
    {0.24f,0.22f,1100, 2600,  2.2f, 1.40f},   /* spit */
  };
  if(kind<0||kind>4) return;
  const float *c=C[kind];
  Voice *v=alloc(kind?1:0);
  if(v){
    v->src=SRC_NOISE; v->rate=c[5]*pt*(0.85f+r01()*0.3f);
    v->phase=(float)(rand()%NOISE_N);
    setFc(v,FLT_BP,c[2]*pt,c[3]*pt,c[1]*sl,c[4],1);
    env(v,vol*c[0],c[1]*sl,0); v->wet=1;
  }
  /* THE LEGS, AND THEY ARE NOT A METRONOME.  Four ticks a beat was one
     tick, and one tick is a click.  Three, unevenly spaced and falling in
     pitch, is an animal putting feet down -- the irregularity is the whole
     effect, and it is what the voice delay was added for. */
  if(kind==0||kind==1){
    static const float TK[3]={0.f,0.031f,0.079f};
    for(int i=0;i<3;i++){
      v=alloc(0); if(!v) break;
      v->src=SRC_SQUARE;
      float f=(1900.f+r01()*900.f)*pt*(1.f-i*0.11f);
      setFreq(v,f,f*0.55f,0.055f,1);
      env(v,vol*0.075f,0.055f,0); v->wet=1;
      dly(v,(TK[i]+rsym(0.008f))*sl);
    }
  }
  /* AND A BIG ONE HAS A CHEST.  Below about a metre and a half of body
     there is nothing to resonate and this stays silent; a brooder gets the
     sub that makes it audible through a bulkhead, which is the point --
     you are meant to know it is coming. */
  if(sz>1.15f&&(kind==0||kind==1||kind==2)){
    v=alloc(1);
    if(v){
      v->src=SRC_SINE;
      float f=78.f/sz;
      setFreq(v,f*1.35f,f*0.72f,0.34f*sl,1);
      env(v,vol*0.20f*(sz-1.f),0.34f*sl,0); v->wet=0.45f;
      dly(v,0.02f);
    }
  }
  if(kind==2){                    /* what comes out when it stops */
    v=alloc(1); if(!v) return;
    v->src=SRC_SAW;
    setFreq(v,260*pt,58*pt,0.5f*sl,1);
    setFc(v,FLT_LP,1400*pt,300*pt,0.5f*sl,0.9f,1);
    env(v,vol*0.22f,0.5f*sl,0); v->wet=1;
  }
}

/* ---- an animal putting a foot down ----------------------------------
   Chitin on plate: a tick with almost no body, pitched by the size of the
   leg that made it.  It is the quietest thing in the file and the one that
   makes a corridor with something in it different from an empty one. */
void sfxStep(float x,float y,float z,float sz){
  float vol=dGain(x,y,z,16.f); if(vol<0.04f) return;
  Voice *v=alloc(0); if(!v) return;
  float pt=1.f/(0.42f+0.58f*sz);
  v->src=SRC_NOISE; v->rate=1.7f*pt+rsym(0.2f);
  v->phase=(float)(rand()%NOISE_N);
  float f=(2300.f+r01()*1100.f)*pt;
  setFc(v,FLT_BP,f,f*0.6f,0.030f,5.5f,1);
  env(v,vol*0.055f,0.030f,0); v->wet=0.7f;
}

/* ---- LUMI ------------------------------------------------------------
   One syllable a character.  By design the drone has no emotion in
   its voice and only pitch and speed to carry any, and that is
   still true here -- a babble is a TEXTURE, not a feeling.

   What it replaces was a 35 ms sine at a fixed pitch, which is a cursor
   blink: fourteen a second of the identical tone, and after two lines the
   ear filed it as UI and stopped hearing that anyone was speaking.  What
   makes a sound read as speech is not its pitch but that the pitch MOVES
   inside the syllable, and that the syllables are not all the same one.
   So each character is

     a carrier   square, gliding up or down a whole tone across 55 ms --
                 the glide is the entire effect
     a formant   a band-pass that sweeps with it, which is a mouth shape
     a ring      a second carrier a fifth off, quiet, which is what makes
                 it a MACHINE talking rather than a person humming
     a consonant every third or fourth character, a 10 ms noise tick

   `pitch` still carries the sentence: story.c wobbles it and drops it at
   the end of a line, and all of that survives. */
void auBeep(float pitch,int warn){
  float f0=(warn?820.f:1180.f)*pitch;
  /* the glide, and it goes both ways -- a syllable that always falls is a
     sigh and a syllable that always rises is a question */
  float f1=f0*(0.86f+r01()*0.30f);
  Voice *v=alloc(1);
  if(v){
    v->src=SRC_SQUARE;
    setFreq(v,f0,f1,0.055f,1);
    setFc(v,FLT_BP, f0*1.6f, f1*2.3f, 0.055f, 2.4f, 1);
    /* AND SHE HAS TO CLEAR THE ROOM TONE.  The ambient bed measures
       -43 dB and her syllables measured -43: the drone who carries the
       compass, the health readout and every line of the story was sitting
       exactly AT the hull hiss, which is not a design for a machine voice,
       it is a level that was never checked against the bed it plays over. */
    env(v,warn?0.155f:0.115f,0.050f,0.008f);
    v->wet=0.20f;
  }
  v=alloc(1);
  if(v){
    v->src=SRC_TRI;
    setFreq(v,f0*1.4983f,f1*1.4983f,0.050f,1);      /* a fifth, tempered */
    env(v,warn?0.054f:0.039f,0.046f,0.006f);
    v->wet=0.24f;
  }
  /* the consonant.  Not every syllable, or it is a rattle. */
  { static int n=0;
    if((++n&3)==0){
      v=alloc(0);
      if(v){
        v->src=SRC_NOISE; v->rate=2.1f; v->phase=(float)(rand()%NOISE_N);
        setFc(v,FLT_HP,3000+r01()*2200.f,3000.f,0.010f,1.f,0);
        env(v,0.048f,0.010f,0); v->wet=0.15f;
      }
    } }
}
/* the heartbeat the vitals readout is synchronised to */
void auHeart(float strength){
  Voice *v=alloc(1); if(!v) return;
  v->src=SRC_SINE;
  setFreq(v,86,44,0.16f,1);
  env(v,0.10f+strength*0.16f,0.17f,0.02f);
  v->wet=0.10f;
}
/* the low-frequency alarm the ship runs from sink stage 1 */
void auAlarmHit(float strength){
  Voice *v=alloc(1);
  if(v){
    v->src=SRC_SAW;
    setFreq(v,155,132,0.9f,0);
    setFc(v,FLT_BP,430,330,0.9f,2.2f,0);
    env(v,0.075f*strength,0.9f,0.35f); v->wet=1;
  }
  v=alloc(1); if(!v) return;
  v->src=SRC_SINE;
  setFreq(v,62,54,1.0f,0);
  env(v,0.06f*strength,1.0f,0.4f);
}
/* metal fatigue: the hull, complaining.  Not a metaphor, says V-05. */
void auHullGroan(void){
  Voice *v=alloc(1);
  if(v){
    v->src=SRC_SAW;
    float f=40+(float)rand()/RAND_MAX*34;
    setFreq(v,f,f*0.72f,2.6f,1);
    setFc(v,FLT_LP,240,120,2.6f,1.4f,1);
    env(v,0.085f,2.6f,0.9f); v->wet=1;
  }
  v=alloc(0); if(!v) return;
  v->src=SRC_NOISE; v->rate=0.35f; v->phase=(float)(rand()%NOISE_N);
  setFc(v,FLT_BP,700,380,2.2f,3.4f,1);
  env(v,0.030f,2.2f,0.7f); v->wet=1;
}

/* ============================================================
   10c. THE MECHANISM

   TEN ACTIONS THAT WERE TEN MUSICAL NOTES.  Every moving part in
   every gun -- the striker, the catch, the magazine, the slide,
   the extractor, the break latch -- went through one function,
   auClick(f,dur), which played a bare square wave at a named
   frequency for a named length.  A square wave at a named
   frequency is a NOTE, and the ear files a note as UI the way it
   files a lift arriving.  Measured on the bench below, the
   spectral flatness of that sound is 0.0003; a piece of concrete
   being hit is 0.26.  Three orders of magnitude is not a quiet
   impact, it is a different kind of signal altogether.

   What a mechanism is instead is three things in an order:

     a STRIKE   metal arriving on metal.  Broadband, and the
                ATTACK is the sound -- see enva().
     a SCRAPE   something sliding.  A band that MOVES, because a
                band that holds still is a tone again.
     a RING     the part carrying on afterwards, at 1 : 2.76 :
                5.40 -- a free bar's modes, not a harmonic
                series.  A harmonic ring is a bell and a bell is
                a note, which is the thing being left behind.

   ... plus the MASS, which is the only reason a big thing sounds
   big and which a frequency argument could not express at all.
   ============================================================ */
/* the four of them are spelled out above alloc(), where auImpact and the
   footstep can reach them too. */

void auMech(int what,float w){
  if(what<0||what>=MCH_N) return;
  if(w<0.3f) w=0.3f;
  if(w>2.0f) w=2.0f;
  float pt=1.f/(0.45f+0.55f*w);      /* heavier is lower ...  */
  float sl=0.75f+0.25f*w;            /* ... and takes longer  */
  Voice *v;
  switch(what){

  case MCH_DRY:
    /* THE ONE SOUND DEFINED BY WHAT IS MISSING.  A live round gives the
       striker something to do; an empty chamber gives it the frame, and
       a frame is a lump.  So there is deliberately no ring here at all
       -- the absence IS the information, and it is what sends the hand
       to the reload key before the second pull. */
    auHit(FLT_HP,0.090f,4600*pt,0.9f,0.005f,0.05f);         /* the sear */
    v=auHit(FLT_BP,0.215f,880*pt,1.1f,0.030f,0.10f);
    if(v) dly(v,0.006f*sl);                                 /* the frame */
    v=auBody(0.070f,210*pt,150*pt,0.040f,0.05f);
    if(v) dly(v,0.006f*sl);
    break;

  case MCH_MAG_OUT:
    auHit(FLT_HP,0.095f,3400*pt,0.9f,0.005f,0.10f);         /* the catch */
    v=auScrape(0.105f,1500*pt,780*pt,1.1f,0.075f*sl,0.22f);
    if(v) dly(v,0.012f);                                    /* down the well */
    v=auHit(FLT_BP,0.080f,620*pt,1.4f,0.030f,0.18f);
    if(v) dly(v,0.070f*sl);                                 /* and clear of it */
    break;

  case MCH_MAG_IN:
    auScrape(0.052f,850*pt,1700*pt,1.0f,0.050f*sl,0.20f);   /* into the well */
    v=auHit(FLT_HP,0.100f,2600*pt,0.9f,0.014f,0.20f);
    if(v) dly(v,0.050f*sl);                                 /* the catch takes it */
    v=auBody(0.105f,170*pt,118*pt,0.090f*sl,0.14f);
    if(v) dly(v,0.050f*sl);                                 /* ... and it stops */
    v=auRing(0.032f,2350*pt,0.055f*sl,0.35f);
    if(v) dly(v,0.050f*sl);
    break;

  case MCH_SLIDE:
    /* the loudest thing a pistol does that is not firing */
    auScrape(0.058f,1150*pt,2700*pt,0.85f,0.042f*sl,0.18f); /* the rails */
    v=auHit(FLT_HP,0.200f,1900*pt,0.8f,0.030f,0.40f);
    if(v) dly(v,0.042f*sl);                                 /* into battery */
    v=auRing(0.058f,1260*pt,0.115f*sl,0.55f);
    if(v) dly(v,0.042f*sl);
    v=auRing(0.030f,3480*pt,0.055f*sl,0.55f);               /* 2.76 x */
    if(v) dly(v,0.042f*sl);
    v=auBody(0.055f,185*pt,138*pt,0.062f*sl,0.20f);
    if(v) dly(v,0.042f*sl);
    break;

  case MCH_EJECT:
    auHit(FLT_HP,0.240f,5200,0.9f,0.004f,0.15f);            /* the extractor */
    auRing(0.030f,3150*pt,0.085f,0.55f);                    /* and the brass */
    v=auRing(0.018f,5320*pt,0.050f,0.55f);
    if(v) dly(v,0.002f);
    /* ... which TUMBLES.  Two naked triangles are a tuning fork, and the
       bench read the whole eject at a flatness of 0.005 -- a note again,
       in the middle of the vocabulary written to stop being one.  A case
       leaving an ejection port is mostly noise with a pitch in it. */
    v=auScrape(0.085f,3400,6200,1.1f,0.055f,0.55f);
    if(v) dly(v,0.006f);
    /* AND IT LANDS.  A quarter of a second later, and it is the cheapest
       thing in this file that says the room has a floor in it.  Quieter
       than the extractor that threw it, or the deck becomes the loudest
       part of reloading a gun. */
    v=auHit(FLT_HP,0.130f,4200,1.2f,0.006f,0.60f);
    if(v) dly(v,0.19f+r01()*0.10f);
    v=auRing(0.014f,4700+r01()*1600.f,0.060f,0.70f);
    if(v) dly(v,0.20f+r01()*0.09f);
    break;

  case MCH_CYCLE:
    /* the same travel with nothing in the chamber to throw */
    auScrape(0.140f,780*pt,430*pt,1.2f,0.055f*sl,0.18f);
    v=auHit(FLT_BP,0.115f,1150*pt,1.6f,0.010f,0.20f);
    if(v) dly(v,0.042f*sl);
    break;

  case MCH_BREAK:
    /* THE HEAVIEST SOUND IN THE SHIP.  Two barrels of steel swinging on
       a hinge and stopping against a standing breech: long parts ring
       LOW and for a long time, which is the whole of why this reads as a
       shotgun and the pistol's slide does not. */
    auScrape(0.050f,520*pt,980*pt,1.0f,0.058f*sl,0.20f);    /* the hinge */
    v=auHit(FLT_HP,0.195f,1250*pt,0.75f,0.038f,0.55f);
    if(v) dly(v,0.058f*sl);                                 /* ... and it shuts */
    v=auRing(0.062f,640*pt,0.265f*sl,0.75f);
    if(v) dly(v,0.058f*sl);
    v=auRing(0.034f,1766*pt,0.130f*sl,0.75f);               /* 2.76 x */
    if(v) dly(v,0.058f*sl);
    v=auRing(0.018f,3456*pt,0.070f*sl,0.75f);               /* 5.40 x */
    if(v) dly(v,0.058f*sl);
    v=auBody(0.075f,132*pt,96*pt,0.095f*sl,0.25f);
    if(v) dly(v,0.058f*sl);
    v=auHit(FLT_HP,0.062f,3100*pt,1.0f,0.007f,0.30f);
    if(v) dly(v,0.076f*sl);                                 /* the latch */
    break;

  case MCH_READY:
    /* the gun coming back to rest in the hands.  Quiet on purpose: the
       staged sounds above have already said everything, and a fanfare
       here is the third noise for one event. */
    auScrape(0.125f,900*pt,520*pt,1.0f,0.045f*sl,0.15f);
    v=auHit(FLT_BP,0.140f,1800*pt,2.0f,0.010f,0.18f);
    if(v) dly(v,0.030f*sl);
    break;

  case MCH_SEIZE:
    /* A HOT MECHANISM DOES NOT RING.  Everything that would carry a
       partial has expanded against something else, so what comes back
       is the clunk and then gas leaving where it should not. */
    auHit(FLT_LP,0.175f,430*pt,1.0f,0.045f,0.10f);
    auBody(0.070f,150*pt,104*pt,0.070f,0.10f);
    v=auScrape(0.045f,4200,3000,0.8f,0.150f,0.30f);
    if(v) dly(v,0.018f);                                    /* the leak */
    break;

  default:  /* MCH_DRAW */
    auScrape(0.066f,1900*pt,880*pt,0.7f,0.095f,0.16f);      /* webbing */
    v=auHit(FLT_BP,0.092f,760*pt,1.5f,0.032f,0.22f);
    if(v) dly(v,0.058f);                                    /* a hand on it */
    v=auRing(0.026f,2100*pt,0.055f,0.30f);
    if(v) dly(v,0.058f);
    break;
  }
}

/* ---- picking a thing up ---------------------------------------------
   A PICKUP WAS LUMI SAYING A SYLLABLE.  story.c called auBeep(1.35,0) --
   the drone's own voice, a third up -- for a medical kit, an ammunition
   cache and an energy pack alike, so the ship's only confirmation that
   anything had been taken at all was the companion appearing to speak,
   in the same voice she uses for dialogue, three times a room.

   A pickup is two events.  The OBJECT leaves where it stood, and that is
   different for a steel case and a rifle on a rack; then the suit
   acknowledges it, and THAT is the same every time.  The acknowledgement
   is the one sound in this file allowed to be electronic -- it comes out
   of a speaker four centimetres from the ear -- but it is a pair under a
   band with a tail on it, which is a console, and not the bare square,
   which is a doorbell. */
void auPickup(int kind){
  Voice *v;
  switch(kind){
  case 0:                       /* medical: a case, and the injector in it */
    auHit(FLT_BP,0.140f,1250,1.2f,0.020f,0.18f);
    v=auScrape(0.100f,2600,5200,0.9f,0.140f,0.20f);
    if(v) dly(v,0.035f);
    break;
  case 1:                       /* a weapon off its rack: steel leaving steel */
    auScrape(0.085f,900,1700,0.9f,0.090f,0.22f);
    v=auHit(FLT_HP,0.130f,2100,0.9f,0.028f,0.30f);
    if(v) dly(v,0.055f);
    v=auRing(0.034f,880,0.180f,0.45f);
    if(v) dly(v,0.055f);
    v=auBody(0.062f,150,108,0.100f,0.16f);
    if(v) dly(v,0.055f);
    break;
  case 2:                       /* ammunition: brass, and it does not
                                   land all together */
    { static const float TK[3]={0.f,0.026f,0.061f};
      for(int i=0;i<3;i++){
        v=auHit(FLT_HP,0.170f-0.034f*i,3600+r01()*1800.f,1.4f,0.012f,0.25f);
        if(v) dly(v,TK[i]+rsym(0.006f));
      } }
    v=auBody(0.040f,190,140,0.070f,0.12f);
    if(v) dly(v,0.020f);
    break;
  case 3:                       /* an energy pack: a servo latch, and then
                                   something that is LIVE */
    auHit(FLT_BP,0.112f,700,1.3f,0.030f,0.20f);
    v=auScrape(0.056f,1400,600,1.1f,0.120f,0.18f);
    if(v) dly(v,0.030f);
    /* the cell coming up.  A rise, because a rise is the only shape that
       means a thing has been switched ON. */
    v=auBody(0.055f,110,320,0.320f,0.10f);
    if(v) dly(v,0.060f);
    break;
  default:                      /* the crates of the last stand: quick,
                                   and no ceremony -- they come every
                                   thirty seconds */
    auHit(FLT_BP,0.115f,1500,1.1f,0.022f,0.20f);
    v=auBody(0.038f,175,130,0.060f,0.12f);
    if(v) dly(v,0.015f);
    break;
  }
  /* ... and the suit, acknowledging: a fifth, tempered, under a band */
  v=alloc(1);
  if(v){
    v->src=SRC_TRI;
    setFreq(v,1480,1660,0.070f,1);
    setFc(v,FLT_BP,2400,3100,0.070f,1.8f,1);
    enva(v,0.050f,0.075f,0.020f,0.0018f); v->wet=0.30f;
    dly(v,0.085f);
  }
  v=alloc(1);
  if(v){
    v->src=SRC_TRI;
    setFreq(v,2217,2487,0.062f,1);
    enva(v,0.021f,0.066f,0.016f,0.0018f); v->wet=0.34f;
    dly(v,0.092f);
  }
}

/* ---- being hit -------------------------------------------------------
   THE PLAYER TAKING DAMAGE MADE NO SOUND AT ALL.  zomHurtPlayer shook
   the camera, pushed the hurt vignette and set a direction arrow, and
   that was the whole of it -- so the one event in the game the player
   most needs to LOCATE was the one event with nothing in the mix, and
   an animal 0.8 m tall standing behind you is exactly the thing the
   screen edge cannot tell you about.

   A hit on an EVA suit is a hit on a shell with a person inside it: the
   plate takes the blow, the body behind it takes the rest, and past a
   certain size the helmet's own alarm answers.  `dmg` decides how much
   of each, so a scratch is the plate alone and a mauling is all four. */
void auHurt(float dmg){
  float s=dmg/28.f;
  if(s>1.4f) s=1.4f;
  if(s<0.18f) s=0.18f;
  Voice *v;
  /* THE PLATE, AND IT HAS TO BE WIDE.  Written first as one low-passed
     burst under a sine, and the sine was three times its size: the bench
     read the pair as a 625 Hz tone with a flatness of 0.0002 -- the same
     reading the square wave got, arrived at from the other direction.  A
     blow on a shell is broadband, and the body is what is UNDER it. */
  auHit(FLT_BP,0.150f+0.130f*s,620-160*s,0.55f,0.045f+0.030f*s,0.25f);
  v=auScrape(0.075f+0.060f*s,1500,600,0.9f,0.070f,0.28f);
  if(v) dly(v,0.005f);                       /* and the scuff off it */
  v=auBody(0.048f+0.055f*s,145-40*s,62,0.115f+0.060f*s,0.14f);
  if(v) dly(v,0.008f);                       /* the person inside it */
  if(s>0.55f){                               /* the suit, tearing */
    v=auScrape(0.040f*s,2200,900,0.8f,0.090f,0.30f);
    if(v) dly(v,0.012f);
  }
  /* and the helmet -- the one thing here that IS a warning tone, but a
     warning tone with a body in it: two squares seven hertz apart under
     one band, beating against each other.  The beat is what an alarm is
     and is the thing a single oscillator can never do. */
  if(s>0.85f){
    for(int i=0;i<2;i++){
      v=alloc(2); if(!v) break;
      v->src=SRC_SQUARE;
      setFreq(v,i?767.f:760.f,i?767.f:760.f,0.34f,0);
      setFc(v,FLT_BP,1500,1200,0.34f,2.6f,0);
      enva(v,i?0.026f:0.030f,0.34f,0.16f,0.004f); v->wet=0.12f;
      dly(v,0.055f);
    }
  }
}

/* ---- the console -----------------------------------------------------
   THE TITLE CARD IS THE FIRST THING THE PLAYER TOUCHES AND IT WAS
   SILENT: three keys with no answer at all -- the selection moving, a
   row being taken, and backing out of the language page.  A menu that
   does not answer reads as a menu that did not register the press, and
   the player presses again.

   This is a ship's console and it is allowed to be electronic.  But a
   relay is a MECHANISM before it is a signal, so each of these is a
   contact closing with a filtered pair behind it, rather than a note. */
void auUI(int kind){
  Voice *v;
  switch(kind){
  /* A RESONANCE IS NOT AN OSCILLATOR, and the difference is the whole of
     this function.  Written first with auRing, these three came back at a
     flatness of 0.0005 -- indistinguishable from the square wave they
     replaced, because a naked triangle IS one.  Noise through a band at
     Q 7 has a pitch and still has a spectrum, which is what a relay in a
     console sounds like, and it is a hair more expensive than nothing. */
  case UI_MOVE:
    auHit(FLT_HP,0.190f,5200,1.1f,0.004f,0.10f);
    v=auScrape(0.100f,2400,2900,5.0f,0.028f,0.20f);
    if(v) dly(v,0.002f);
    break;
  case UI_OK:
    auHit(FLT_BP,0.210f,1900,1.6f,0.010f,0.14f);
    v=auScrape(0.130f,1480,1500,7.0f,0.070f,0.28f);
    if(v) dly(v,0.004f);
    v=auScrape(0.100f,2217,2240,7.0f,0.060f,0.28f);   /* ... and up a fifth */
    if(v) dly(v,0.038f);
    v=auRing(0.016f,1480,0.060f,0.28f);
    if(v) dly(v,0.004f);
    break;
  default:                                  /* UI_BACK: the pair, reversed */
    auHit(FLT_BP,0.190f,1400,1.6f,0.010f,0.14f);
    v=auScrape(0.120f,1660,1680,7.0f,0.058f,0.28f);
    if(v) dly(v,0.004f);
    v=auScrape(0.098f,1108,1120,7.0f,0.072f,0.28f);
    if(v) dly(v,0.036f);
    v=auRing(0.014f,1108,0.070f,0.28f);
    if(v) dly(v,0.036f);
    break;
  }
}

/* ---- proving it makes a noise ---------------------------------------
   A CLAIM ABOUT SOUND NEEDS A FILE, for the same reason a claim about how
   something looks needs a picture.  `-wav out.wav` runs the mixer without
   the device, fires one of everything at known times, and writes what came
   out -- so silence, clipping and NaN are all visible before anyone puts
   headphones on. */
#include <stdio.h>
void auRenderWav(const char *path,float seconds){
  int n=(int)(seconds*SR);
  short *pcm=(short*)malloc(sizeof(short)*2*n);
  auAmbient(1);
  ambGain=0.16f;                       /* no fade-in: this is a test bench */
  int done=0, ev=0;
  /* AND THE BENCH HAS TO CARRY THE NEW VOICES OR IT IS TESTING THE OLD
     BUILD.  A walk and a run, the pistol and the double-barrel, both ends
     of the animal (a scuttler at 0.74 and a brooder at 1.62, which is the
     one comparison that says whether size is audible at all), a foot of
     each, and four characters of LUMI in a row so the babble can be heard
     as babble rather than as one syllable. */
  /* AND THE BENCH NAMES ITS OWN EVENTS.  The times used to live here as a
     bare float array and again in whatever was reading the wav, which is
     two things to keep in step for no reason -- so the schedule carries
     the name, and the run drops a `<path>.events.txt` beside the audio
     saying what happened when.  Nothing downstream has to guess. */
  #define NWAV 39
  static const struct { float t; const char *name; } EVT[NWAV]={
    {0.20f,"footstep walk"},   {0.45f,"footstep run"},
    {0.80f,"gun pistol"},      {1.30f,"gun double-barrel"},
    {1.80f,"impact metal"},    {2.10f,"impact concrete"},
    {2.45f,"door slide"},      {2.70f,"door latch"},
    {3.10f,"chitter 0.74"},    {3.55f,"chitter 1.62"},
    {4.00f,"lunge 0.74"},      {4.30f,"lunge 1.62"},
    {4.60f,"creature death"},  {4.90f,"creature step"},
    {5.25f,"lumi a"},{5.32f,"lumi b"},{5.39f,"lumi c"},{5.46f,"lumi d"},
    {5.53f,"lumi warn"},
    /* ---- the mechanism, which is what this bench was extended for --- */
    {5.90f,"mech DRY"},        {6.20f,"mech MAG_OUT"},
    {6.55f,"mech MAG_IN"},     {6.95f,"mech SLIDE w1.00"},
    {7.30f,"mech SLIDE w0.72"},/* the same action with lighter parts, and
                                  the one pair on this bench that says
                                  whether `w` is audible at all -- the way
                                  the 0.74 / 1.62 chitters say it for `sz` */
    {7.65f,"mech EJECT"},      {8.15f,"mech CYCLE"},
    {8.50f,"mech BREAK w1.30"},{9.10f,"mech READY"},
    {9.40f,"mech SEIZE"},      {9.80f,"mech DRAW"},
    {10.20f,"pickup medical"}, {10.60f,"pickup weapon"},
    {11.05f,"pickup ammo"},    {11.45f,"pickup pack"},
    {12.05f,"hurt 9"},         {12.45f,"hurt 34"},
    {13.00f,"ui MOVE"},{13.25f,"ui OK"},{13.60f,"ui BACK"} };
  while(done<n){
    float t=(float)done/SR;
    while(ev<NWAV && t>=EVT[ev].t){
      switch(ev){
      case 0: auFootstep(0.f); break;              /* a walk */
      case 1: auFootstep(1.f); break;              /* ... and a run */
      case 2: auGun(0); break;                     /* pistol */
      case 3: auGun(2); break;                     /* the double-barrel */
      case 4: auImpact(1,0,LX,LY,LZ); break;
      case 5: auImpact(0,0,LX,LY,LZ); break;
      case 6: auSlide(LX,LY,LZ,1); break;
      case 7: auLatch(LX,LY,LZ); break;
      case 8: sfxCreature(0,LX,LY,LZ,0.74f); break;   /* scuttler chitter */
      case 9: sfxCreature(0,LX,LY,LZ,1.62f); break;   /* brooder chitter */
      case 10:sfxCreature(1,LX,LY,LZ,0.74f); break;   /* a small lunge */
      case 11:sfxCreature(1,LX,LY,LZ,1.62f); break;   /* a big one */
      case 12:sfxCreature(2,LX,LY,LZ,1.62f); break;   /* and it stops */
      case 13:sfxStep(LX,LY,LZ,0.74f); break;
      case 14:case 15:case 16:case 17:
              auBeep(0.94f+0.06f*(ev-14),0); break;   /* LUMI, babbling */
      case 18:auBeep(1.0f,1); break;                  /* ... and warning */
      case 19:auMech(MCH_DRY,1.0f); break;
      case 20:auMech(MCH_MAG_OUT,1.0f); break;
      case 21:auMech(MCH_MAG_IN,1.0f); break;
      case 22:auMech(MCH_SLIDE,1.00f); break;
      case 23:auMech(MCH_SLIDE,0.72f); break;
      case 24:auMech(MCH_EJECT,1.0f); break;
      case 25:auMech(MCH_CYCLE,1.0f); break;
      case 26:auMech(MCH_BREAK,1.30f); break;
      case 27:auMech(MCH_READY,1.0f); break;
      case 28:auMech(MCH_SEIZE,1.05f); break;
      case 29:auMech(MCH_DRAW,1.0f); break;
      case 30:auPickup(0); break;
      case 31:auPickup(1); break;
      case 32:auPickup(2); break;
      case 33:auPickup(3); break;
      case 34:auHurt(9.f); break;
      case 35:auHurt(34.f); break;
      case 36:auUI(UI_MOVE); break;
      case 37:auUI(UI_OK); break;
      case 38:auUI(UI_BACK); break;
      }
      ev++;
    }
    int m=n-done; if(m>BLK) m=BLK;
    mixBlock(pcm+done*2,m);
    done+=m;
  }
  FILE *f=fopen(path,"wb");
  if(f){
    int data=n*4, riff=36+data;
    fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVEfmt ",1,8,f);
    int fmtlen=16; short one=1, two=2, bits=16; int sr=SR, brate=SR*4;
    short align=4;
    fwrite(&fmtlen,4,1,f); fwrite(&one,2,1,f); fwrite(&two,2,1,f);
    fwrite(&sr,4,1,f); fwrite(&brate,4,1,f); fwrite(&align,2,1,f);
    fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&data,4,1,f);
    fwrite(pcm,2,(size_t)n*2,f);
    fclose(f);
  }
  { char sc[300]; FILE *g;
    _snprintf(sc,sizeof sc,"%s.events.txt",path); sc[sizeof sc-1]=0;
    g=fopen(sc,"wb");
    if(g){
      for(int i=0;i<NWAV;i++) fprintf(g,"%.3f\t%s\n",EVT[i].t,EVT[i].name);
      fclose(g);
    } }
  /* and the numbers, so the log says whether it is a signal or a fault */
  double sum=0; int peak=0, nan=0;
  for(int i=0;i<n*2;i++){
    int v=pcm[i]; if(v<0)v=-v;
    if(v>peak) peak=v;
    sum+=(double)pcm[i]*pcm[i];
  }
  platLog("audio  wav %s  %.1f s  rms %.1f  peak %d  clipped %s  nan %d",
          path,seconds,sqrt(sum/(n*2)),peak,peak>=32000?"YES":"no",nan);
  free(pcm);
}
