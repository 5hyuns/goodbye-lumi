/* ============================================================
   LUMI'S DISPLAY.  See face.h.

   The bench's canvas is 512 x 290 and every state is a handful
   of fillRect / stroke calls with shadowBlur 26 under them, then
   `end()` lays a scanline every third row.  All of that ports to
   a fragment shader almost literally: a rect is a box SDF, a
   stroke is a segment SDF, and shadowBlur is what the scene's
   own HDR bloom does for free once the ink is emissive.

   The one thing a shader will not do is know what a letter is,
   so text is placed on the CPU: hud.c's 5x7 stencil, two ints a
   glyph, at most 24 glyphs a frame.  The shader draws lit cells.

   Coordinates below are CANVAS pixels, origin top-left, because
   that is the frame every number in lumi_v6.html's section 8 is
   written in and a translated layout is a layout you cannot
   diff against the thing it came from.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "app.h"
#include "ent.h"
#include "lumi.h"
#include "lumimesh.h"
#include "hud.h"
#include "face.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

GLuint glMakeProgram(const char *vs,const char *fs,const char *what);

#define MAXG 24                       /* glyphs a frame */

static GLuint pFace, faceVAO, faceVB;
static int    faceN;
static struct { GLint uProj,uView,uM0,uM1,uM2,uTime,uState,uVal,uCount,
                      uGN,uGP,uGB; } UF;

static struct {
  float M[12], s, hp, bpm, gauge, blink, t;
  int   disp, count, on;
} F;

/* ---- the glyph list ------------------------------------------------- */
static float GP[MAXG*4], GB[MAXG*4];
static int   nGly;

/* col: 0 cyan, 1 red, 2 amber, 3 dim */
static void gPut(int ch,float x,float y,float cell,float a,int col){
  if(nGly>=MAXG) return;
  const unsigned char *r=hudGlyph(ch);
  float *p=GP+nGly*4, *b=GB+nGly*4;
  p[0]=x; p[1]=y; p[2]=cell; p[3]=a;
  b[0]=(float)(r[0]|(r[1]<<5)|(r[2]<<10)|(r[3]<<15));
  b[1]=(float)(r[4]|(r[5]<<5)|(r[6]<<10));
  b[2]=(float)col; b[3]=0;
  nGly++;
}
/* the stencil is 5 cells wide and the gap is the sixth, the way markText
   and the ship's placards space it */
static float gWide(const char *s,float cell){
  int n=0; for(const char *q=s;*q;q++) n++;
  return n? (n*6-1)*cell : 0;
}
static void gText(const char *s,float x,float y,float cell,float a,int col){
  for(int i=0;s[i];i++) gPut(s[i],x+i*6*cell,y,cell,a,col);
}
static void gMid(const char *s,float cx,float y,float cell,float a,int col){
  gText(s,cx-gWide(s,cell)*0.5f,y,cell,a,col);
}

/* ============================================================
   the shaders
   ============================================================ */
static const char *FACE_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
"uniform mat4 uProj,uView;\n"
"uniform vec4 uM0,uM1,uM2;\n"      /* her body frame, scale folded in */
"out vec2 vUV;\n"
"void main(){\n"
"  vec4 p4=vec4(aPos,1.0);\n"
"  vec3 wp=vec3(dot(uM0,p4),dot(uM1,p4),dot(uM2,p4));\n"
"  vUV=aUV;\n"
"  gl_Position=uProj*uView*vec4(wp,1.0);\n"
"}\n";

static const char *FACE_FS =
"#version 330 core\n"
"in vec2 vUV;\n"
"out vec4 outColor;\n"
"uniform float uTime;\n"
"uniform int   uState,uCount,uGN;\n"
"uniform vec4  uVal;\n"                 /* hp, bpm, gauge, blink */
"uniform vec4  uGP[24];\n"              /* x, y, cell, alpha */
"uniform vec4  uGB[24];\n"              /* rows0-3, rows4-6, colour, - */
"const float DW=512.0, DH=290.0, CX=256.0, CY=145.0;\n"
/* the bench's own two colours, and the two the ship adds */
"const vec3 CYA=vec3(0.412,0.859,1.000);\n"
"const vec3 RED=vec3(1.000,0.353,0.290);\n"
"const vec3 AMB=vec3(1.000,0.660,0.160);\n"
"const vec3 DIM=vec3(0.360,0.420,0.470);\n"
"vec3 pal(int i){ return i==1?RED:(i==2?AMB:(i==3?DIM:CYA)); }\n"
"float sdBox(vec2 p,vec2 b){ vec2 d=abs(p)-b;\n"
"  return length(max(d,vec2(0.0)))+min(max(d.x,d.y),0.0); }\n"
"float sdSeg(vec2 p,vec2 a,vec2 b){ vec2 pa=p-a,ba=b-a;\n"
"  float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0); return length(pa-ba*h); }\n"
/* ONE FEATHER FOR THE WHOLE FRAGMENT.  fwidth in a loop is undefined the
   moment the loop is not uniform, so the pixel's own size in canvas units
   is measured once at the top of main and handed down. */
"float ink(float d,float aa){ return 1.0-smoothstep(-aa,aa,d); }\n"

/* the ECG, plateau for plateau off DISPLAYS.pulse: five steps inside 12%
   of the sweep and a flat line for the other 88% */
"float ecg(float x,float ph){\n"
"  float d=fract(x-ph+1.0);\n"
"  if(d>0.44&&d<0.47) return  30.0;\n"
"  if(d>=0.47&&d<0.50) return -98.0;\n"
"  if(d>=0.50&&d<0.53) return  56.0;\n"
"  if(d>=0.53&&d<0.56) return -10.0;\n"
"  return 0.0;\n"
"}\n"
/* A STEP FUNCTION HAS NO DISTANCE FIELD, so the trace is sampled the way
   the canvas polyline was -- ten points either side of the pixel, 3.4 px
   apart, which is the bench's own 140 samples across 472 px -- and the
   answer is the distance to the nearest of those segments.  The vertical
   jumps come out as steep segments, which is exactly what lineTo drew. */
"float trace(vec2 p,float x0,float w,float cy,float amp,float ph){\n"
"  float d=1e9, sx=p.x-17.0;\n"
"  vec2 a=vec2(sx,cy+ecg((sx-x0)/w,ph)*amp);\n"
"  for(int i=1;i<=10;i++){\n"
"    float bx=sx+3.4*float(i);\n"
"    vec2 b=vec2(bx,cy+ecg((bx-x0)/w,ph)*amp);\n"
"    d=min(d,sdSeg(p,a,b)); a=b;\n"
"  }\n"
"  return d;\n"
"}\n"
/* one stencil cell is a box; the 3x3 walk is so a stroke that crosses a
   cell boundary antialiases on both sides instead of one */
"float glyph(vec2 p,int i,float aa){\n"
"  vec4 g=uGP[i];\n"
"  vec2 q=(p-g.xy)/g.z;\n"
"  if(q.x<-1.0||q.x>6.0||q.y<-1.0||q.y>8.0) return 0.0;\n"
"  int bx=int(uGB[i].x), by=int(uGB[i].y);\n"
"  int c0=int(floor(q.x)), r0=int(floor(q.y));\n"
"  float k=0.0;\n"
"  for(int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++){\n"
"    int c=c0+dc, r=r0+dr;\n"
"    if(c<0||c>4||r<0||r>6) continue;\n"
"    int row=(r<4)?(bx>>(r*5)):(by>>((r-4)*5));\n"
"    if((row&(16>>c))==0) continue;\n"
"    vec2 cc=g.xy+(vec2(float(c),float(r))+0.5)*g.z;\n"
"    k=max(k,ink(sdBox(p-cc,vec2(g.z*0.5)),aa));\n"
"  }\n"
"  return k;\n"
"}\n"
"float eyes(vec2 p,float w,float h,float aa){\n"
"  return max(ink(sdBox(p-vec2(CX-82.0,CY),vec2(w*0.5,h*0.5)),aa),\n"
"             ink(sdBox(p-vec2(CX+82.0,CY),vec2(w*0.5,h*0.5)),aa));\n"
"}\n"
"void put(inout vec3 c,float k,vec3 col){ c=max(c,col*k); }\n"

"void main(){\n"
"  vec2 p=vec2(vUV.x*DW,(1.0-vUV.y)*DH);\n"
"  float aa=max(length(fwidth(p))*0.6,0.5);\n"
"  float t=uTime;\n"
"  vec3 c=vec3(0.0);\n"
"  float hp=uVal.x, bpm=max(uVal.y,30.0), gauge=uVal.z, blink=uVal.w;\n"
"  vec3 hc = hp<0.30 ? RED : (hp<0.60 ? AMB : CYA);\n"

"  if(uState==0||uState==3){\n"                       /* EYES idle / talk */
"    put(c,eyes(p,64.0,max(64.0*blink,5.0),aa),CYA);\n"
"  } else if(uState==10){\n"                          /* OFF -- the bench's
                                                         'dying': dim, small */
"    put(c,eyes(p,40.0,40.0,aa)*0.26,CYA);\n"
"  } else if(uState==1){\n"                           /* HEARTBEAT */
"    float ph=fract(t*bpm/60.0);\n"
"    put(c,ink(trace(p,20.0,DW-40.0,CY,1.0,ph)-4.0,aa),RED);\n"
"  } else if(uState==2){\n"                           /* VITALS */
     /* THE GRAPH IS THE POINT OF THIS FACE.  A number says what your health
        is; a trace says you still have one.  The strip runs the whole width
        at a third of the spike height, the rate is the real bpm, and the
        colour is the health's -- so the top of her face goes amber and then
        red before you have finished reading the digits. */
"    float ph=fract(t*bpm/60.0);\n"
"    put(c,ink(abs(p.y-62.0)-0.9,aa)*0.16,hc);\n"     /* the baseline */
"    put(c,ink(trace(p,16.0,DW-32.0,62.0,0.42,ph)-3.0,aa),hc);\n"
"    put(c,ink(abs(p.y-104.0)-0.7,aa)*0.20,DIM);\n"   /* the strip's floor */
     /* the pulse dot, on the beat, beside the rate */
"    float beat=fract(t*bpm/60.0);\n"
"    float pu=beat<0.12?1.0:(beat<0.26?0.45:0.10);\n"
"    put(c,ink(length(p-vec2(300.0,152.0))-13.0,aa)*(0.18+pu*0.82),RED);\n"
     /* the ten-segment bar, which is the reading you can take at a glance */
"    int lit=int(hp*10.0+0.5);\n"
"    for(int i=0;i<10;i++){\n"
"      float x=26.0+float(i)*46.0;\n"
"      float k=ink(sdBox(p-vec2(x+18.0,249.0),vec2(18.0,13.0)),aa);\n"
"      put(c,k*(i<lit?1.0:0.13),i<lit?hc:DIM);\n"
"    }\n"
"  } else if(uState==4){\n"                           /* SCAN */
"    put(c,ink(abs(p.y-(CY+62.0+sin(t*3.0)*34.0))-2.0,aa)*0.34,CYA);\n"
"    put(c,ink(sdBox(p-vec2(CX,CY-4.0),vec2(150.0,44.0))-2.0,aa)*0.10,CYA);\n"
"  } else if(uState==5){\n"                           /* OPEN, with a gauge */
"    float o=sin(t*4.0)*12.0;\n"
"    vec2 l=vec2(CX-176.0-o,CY-30.0), r=vec2(CX+176.0+o,CY-30.0);\n"
"    put(c,ink(min(min(sdSeg(p,l+vec2(-22.0,0.0),l+vec2(22.0,-36.0)),\n"
"                      sdSeg(p,l+vec2(-22.0,0.0),l+vec2(22.0, 36.0))),\n"
"                  sdSeg(p,l+vec2(22.0,-36.0),l+vec2(22.0,36.0)))-5.0,aa),AMB);\n"
"    put(c,ink(min(min(sdSeg(p,r+vec2( 22.0,0.0),r+vec2(-22.0,-36.0)),\n"
"                      sdSeg(p,r+vec2( 22.0,0.0),r+vec2(-22.0, 36.0))),\n"
"                  sdSeg(p,r+vec2(-22.0,-36.0),r+vec2(-22.0,36.0)))-5.0,aa),AMB);\n"
"    for(int i=0;i<8;i++){\n"
"      float x=52.0+float(i)*52.0;\n"
"      float k=ink(sdBox(p-vec2(x+20.0,232.0),vec2(20.0,15.0)),aa);\n"
"      put(c,k*(float(i)<gauge*8.0?1.0:0.12),AMB);\n"
"    }\n"
"  } else if(uState==7){\n"                           /* WARN */
"    float f=0.45+0.55*abs(sin(t*4.0));\n"
"    float d=min(min(sdSeg(p,vec2(CX,CY-96.0),vec2(CX+98.0,CY+76.0)),\n"
"                    sdSeg(p,vec2(CX+98.0,CY+76.0),vec2(CX-98.0,CY+76.0))),\n"
"                    sdSeg(p,vec2(CX-98.0,CY+76.0),vec2(CX,CY-96.0)));\n"
"    put(c,ink(d-6.0,aa)*f,RED);\n"
"    put(c,ink(sdBox(p-vec2(CX,CY-1.0),vec2(8.0,29.0)),aa)*f,RED);\n"
"    put(c,ink(sdBox(p-vec2(CX,CY+49.0),vec2(8.0,8.0)),aa)*f,RED);\n"
"  } else if(uState==8||uState==9){\n"                /* she is elsewhere */
"    int k=int(mod(t*3.0,3.0));\n"
"    for(int i=0;i<3;i++)\n"
"      put(c,ink(sdBox(p-vec2(CX-82.0+float(i)*82.0,CY),vec2(22.0,22.0)),aa)\n"
"            *(i==k?1.0:0.24),CYA);\n"
"  }\n"

/* the text, whatever the state put in the list */
"  for(int i=0;i<uGN;i++){\n"
"    vec4 g=uGP[i];\n"
"    put(c,glyph(p,i,aa)*g.w,pal(int(uGB[i].z)));\n"
"  }\n"

/* and end(): a black line every third row, 20% -- the thing that says
   SCREEN rather than PAINT, and the reason the face survives being
   photographed from 30 cm away */
"  float scan=(mod(floor(p.y),3.0)<1.0)?0.80:1.0;\n"
"  vec3 bg=vec3(0.020,0.035,0.046);\n"
"  outColor=vec4((bg+c*2.6)*scan,1.0);\n"
"}\n";

/* ============================================================
   plumbing
   ============================================================ */
int faceInit(void){
  pFace=glMakeProgram(FACE_VS,FACE_FS,"face");
  if(!pFace) return 0;
#define U(n) UF.n=glGetUniformLocation(pFace,#n)
  U(uProj);U(uView);U(uM0);U(uM1);U(uM2);U(uTime);U(uState);U(uVal);
  U(uCount);U(uGN);U(uGP);U(uGB);
#undef U

  float *v; faceN=lumiMeshScreen(&v);
  glGenVertexArrays(1,&faceVAO); glBindVertexArray(faceVAO);
  glGenBuffers(1,&faceVB); glBindBuffer(GL_ARRAY_BUFFER,faceVB);
  glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)faceN*20,v,GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,20,(void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,20,(void*)12);
  glBindVertexArray(0);
  free(v);
  platLog("face   %d verts, 512x290 in a shader",faceN);
  return 1;
}

void faceSet(const float *M,float s,int disp,
             float hp,float bpm,int count,float gauge,float blink,float t){
  if(disp<0){ F.on=0; return; }
  /* fold her scale into the frame, exactly the way pushEnt does */
  for(int r=0;r<3;r++){
    F.M[r*4]=M[r*4]*s; F.M[r*4+1]=M[r*4+1]*s;
    F.M[r*4+2]=M[r*4+2]*s; F.M[r*4+3]=M[r*4+3];
  }
  F.s=s; F.disp=disp; F.hp=hp; F.bpm=bpm; F.count=count;
  F.gauge=gauge; F.blink=blink; F.t=t; F.on=1;
}

/* what the state wants written on it.  The shader draws cells; deciding
   that 0.72 reads "72%" is this side's job. */
static void layout(void){
  char b[16];
  nGly=0;
  switch(F.disp){
  case LD_VITALS: {
    int pct=(int)(F.hp*100.f+0.5f); if(pct>99) pct=99; if(pct<0) pct=0;
    int col=F.hp<0.30f?1:(F.hp<0.60f?2:0);
    _snprintf(b,sizeof b,"%d%%",pct);
    gText(b,26.f,126.f,10.f,1.f,col);
    int bp=(int)(F.bpm+0.5f); if(bp>199) bp=199; if(bp<0) bp=0;
    _snprintf(b,sizeof b,"%d",bp);
    gText(b,330.f,124.f,7.f,1.f,0);
    gText("BPM",332.f,180.f,3.6f,0.55f,3);
    break; }
  case LD_COUNT: {
    int n=F.count<0?0:(F.count>99?99:F.count);
    _snprintf(b,sizeof b,"%d",n);
    /* the bench flashes the countdown on the second */
    float a=(fmodf(F.t,1.f)<0.16f)?1.f:0.68f;
    gMid(b,256.f,60.f,24.f,a,2);
    break; }
  case LD_SCAN:
    gMid("SCAN",256.f,116.f,11.f,1.f,0);
    break;
  case LD_OPEN:
    gMid("OPEN",256.f,86.f,12.f,1.f,2);
    break;
  case LD_SPEAKER: gMid("RX",256.f,216.f,5.f,0.5f,3); break;
  case LD_RADIO:   gMid("TX",256.f,216.f,5.f,0.5f,3); break;
  default: break;
  }
}

void faceDraw(const FrameState *Fr){
  if(!F.on) return;
  layout();

  glUseProgram(pFace);
  glUniformMatrix4fv(UF.uProj,1,GL_FALSE,Fr->proj);
  glUniformMatrix4fv(UF.uView,1,GL_FALSE,Fr->view);
  glUniform4fv(UF.uM0,1,F.M);
  glUniform4fv(UF.uM1,1,F.M+4);
  glUniform4fv(UF.uM2,1,F.M+8);
  glUniform1f(UF.uTime,F.t);
  glUniform1i(UF.uState,F.disp);
  glUniform1i(UF.uCount,F.count);
  { float val[4]={F.hp,F.bpm,F.gauge,F.blink};
    glUniform4fv(UF.uVal,1,val); }
  glUniform1i(UF.uGN,nGly);
  if(nGly){
    glUniform4fv(UF.uGP,nGly,GP);
    glUniform4fv(UF.uGB,nGly,GB);
  }
  /* A GUEST DRAW INHERITS THE HOST'S GL STATE, and the pass before this one
     is the entity pass, which leaves blending off and depth on -- but say
     so anyway, because what runs before it is not this file's to promise. */
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
  glBindVertexArray(faceVAO);
  glDrawArrays(GL_TRIANGLES,0,faceN);
  glBindVertexArray(0);
}
