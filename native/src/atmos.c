/* ============================================================
   ATMOSPHERE.  Two passes, both additive, both drawn after the
   world and before the overlay: the dust that gives a corridor a
   volume, and the corona that gives a lamp a size.

   Neither is lit by the bake -- they are the two things in the
   frame that are IN the air rather than on a surface, so they
   ask the lamps directly.  There are more lamps in the ship than
   the dust shader carries, so the slots go to the ones nearest
   the camera and are refilled as it moves.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "app.h"
#include "ent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GLuint glMakeProgram(const char *vs,const char *fs,const char *what);

#define MAXL 26

static GLuint pCorona,pDust, coronaVAO,coronaVB, dustVAO,dustVB;
static int coronaCount, dustCount;
static struct { GLint uProj,uView,uGain,uStyleC; } UCo;
static struct { GLint uProj,uView,uCamPos,uLightPos,uLightCol,uLightRange,
                      uLightCount,uTime,uScale; } UDu;
static float LP[MAXL*3], LC[MAXL*3], LR[MAXL];
static int   nNear;

static const char *CORONA_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aCenter;\n"
"layout(location=1) in vec2 aCorner;\n"
"layout(location=2) in vec3 aColor;\n"
"layout(location=3) in float aRadius;\n"
"layout(location=4) in float aStyle;\n"
"uniform mat4 uProj,uView;\n"
/* the same four banks the world shader takes; aStyle is the bank
   index plus one, and zero means the lamp is steady */
"uniform vec3 uStyleC[7];\n"
"out vec2 vC; out vec3 vCol; out float vGain;\n"
"void main(){\n"
"  vec3 right=vec3(uView[0][0],uView[1][0],uView[2][0]);\n"
"  vec3 up   =vec3(uView[0][1],uView[1][1],uView[2][1]);\n"
"  int sg=int(aStyle+0.5)-1;\n"
"  vC=aCorner; vGain=1.0;\n"
"  vCol = sg>=0 ? aColor*uStyleC[sg] : aColor;\n"
"  gl_Position=uProj*uView*vec4(aCenter+(right*aCorner.x+up*aCorner.y)*aRadius,1.0);\n"
"}\n";
static const char *CORONA_FS =
"#version 330 core\n"
"in vec2 vC; in vec3 vCol; in float vGain;\n"
"uniform float uGain;\n"
"out vec4 outColor;\n"
"void main(){\n"
"  float d=length(vC);\n"
"  if(d>1.0) discard;\n"
"  outColor=vec4(vCol*pow(1.0-d,3.0)*uGain*vGain,1.0);\n"
"}\n";

/* %d: MAXL x4 */
static const char *DUST_VS_FMT =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec2 aSeed;\n"
"uniform mat4 uProj,uView;\n"
"uniform vec3 uCamPos;\n"
"uniform vec3 uLightPos[%d];\n"
"uniform vec3 uLightCol[%d];\n"
"uniform float uLightRange[%d];\n"
"uniform int uLightCount;\n"
"uniform float uTime,uScale;\n"
"out vec3 vCol;\n"
"void main(){\n"
"  float ph=aSeed.x;\n"
"  vec3 p=aPos+vec3(sin(uTime*0.29+ph)*0.42,\n"
"                   sin(uTime*0.17+ph*1.7)*0.26,\n"
"                   cos(uTime*0.23+ph*1.3)*0.42);\n"
"  vec3 lit=vec3(0.0);\n"
"  for(int i=0;i<%d;i++){\n"
"    if(i>=uLightCount) break;\n"
"    float dd=length(uLightPos[i]-p);\n"
"    float att=max(0.0,1.0-dd/(uLightRange[i]*0.85));\n"
"    lit+=uLightCol[i]*att*att;\n"
"  }\n"
"  vCol=lit;\n"
"  gl_PointSize=clamp(uScale*aSeed.y/max(length(uCamPos-p),0.5),1.0,7.0);\n"
"  gl_Position=uProj*uView*vec4(p,1.0);\n"
"}\n";
static const char *DUST_FS =
"#version 330 core\n"
"in vec3 vCol; out vec4 outColor;\n"
"void main(){\n"
"  float d=length(gl_PointCoord-0.5)*2.0;\n"
"  if(d>1.0) discard;\n"
"  outColor=vec4(vCol*pow(1.0-d,2.0)*0.30,1.0);\n"
"}\n";

/* airborne dust, scattered through the open volumes and rejected inside
   solids.  The volumes are the corridors the route actually runs down --
   filling the whole hull would put motes inside every wall. */
static const float DUST_VOL[5][6]={
  {-6.0f,-1.2f,0.2f,3.2f,18.0f,42.0f},
  {-22.8f,-1.2f,0.2f,3.2f,21.6f,26.4f},
  {-22.8f,-18.0f,0.2f,3.2f,12.0f,27.6f},
  {-7.2f,0.0f,0.2f,3.2f,20.4f,27.6f},
  {-8.4f,-3.6f,0.2f,3.2f,58.8f,78.0f}};

int atmosInit(void){
  pCorona=glMakeProgram(CORONA_VS,CORONA_FS,"corona");
  if(!pCorona) return 0;
  UCo.uProj =glGetUniformLocation(pCorona,"uProj");
  UCo.uView =glGetUniformLocation(pCorona,"uView");
  UCo.uGain =glGetUniformLocation(pCorona,"uGain");
  UCo.uStyleC=glGetUniformLocation(pCorona,"uStyleC");

  char *vs=(char*)malloc(4096);
  _snprintf(vs,4096,DUST_VS_FMT,MAXL,MAXL,MAXL,MAXL);
  pDust=glMakeProgram(vs,DUST_FS,"dust");
  free(vs);
  if(!pDust) return 0;
#define U(n) UDu.n=glGetUniformLocation(pDust,#n)
  U(uProj);U(uView);U(uCamPos);U(uLightPos);U(uLightCol);U(uLightRange);
  U(uLightCount);U(uTime);U(uScale);
#undef U

  /* ---- the coronas: two triangles a lamp, billboarded in the shader --- */
  { int cap=nLight*6*10;
    float *V=(float*)malloc(sizeof(float)*cap);
    int n=0;
    static const float C[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    for(int i=0;i<nLight;i++){
      Light *L=&LIGHTS[i];
      if(L->r<6) continue;                    /* a strip is not a lamp */
      float rad=L->r*0.20f; if(rad>2.2f) rad=2.2f;
      for(int c=0;c<6;c++){
        float *o=V+n; n+=10;
        o[0]=L->p[0]; o[1]=L->p[1]; o[2]=L->p[2];
        o[3]=C[c][0]; o[4]=C[c][1];
        o[5]=L->c[0]; o[6]=L->c[1]; o[7]=L->c[2];
        /* WHICH BANK THIS LAMP IS IN, plus one.  It was hardcoded to 0
           because nothing in the ship switched; now the corona goes
           dark, or red, with the light it is the halo of. */
        o[8]=rad; o[9]=(float)(lampStyle(i)+1);
      }
    }
    coronaCount=n/10;
    glGenVertexArrays(1,&coronaVAO); glBindVertexArray(coronaVAO);
    glGenBuffers(1,&coronaVB); glBindBuffer(GL_ARRAY_BUFFER,coronaVB);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)n*4,V,GL_STATIC_DRAW);
    const int S=40;
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,S,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,S,(void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,S,(void*)20);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,S,(void*)32);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,S,(void*)36);
    glBindVertexArray(0);
    free(V); }

  /* ---- the dust ------------------------------------------------------ */
  { float total=0;
    for(int i=0;i<5;i++){
      const float *v=DUST_VOL[i];
      total+=(v[1]-v[0])*(v[3]-v[2])*(v[5]-v[4]);
    }
    float *V=(float*)malloc(sizeof(float)*1800*5);
    int n=0;
    unsigned seed=1337;
    #define RND() (seed=seed*1664525u+1013904223u, seed/4294967296.0f)
    for(int i=0;i<5;i++){
      const float *v=DUST_VOL[i];
      float vol=(v[1]-v[0])*(v[3]-v[2])*(v[5]-v[4]);
      int want=(int)(1500*vol/total+0.5f);
      for(int k=0,guard=0; k<want && guard<want*12; guard++){
        float x=v[0]+RND()*(v[1]-v[0]);
        float y=v[2]+RND()*(v[3]-v[2]);
        float z=v[4]+RND()*(v[5]-v[4]);
        if(solidAt(x,y,z)) continue;
        float *o=V+n; n+=5;
        o[0]=x; o[1]=y; o[2]=z; o[3]=RND()*6.283f; o[4]=0.5f+RND()*1.6f;
        k++;
      }
    }
    #undef RND
    dustCount=n/5;
    glGenVertexArrays(1,&dustVAO); glBindVertexArray(dustVAO);
    glGenBuffers(1,&dustVB); glBindBuffer(GL_ARRAY_BUFFER,dustVB);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)n*4,V,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,20,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,20,(void*)12);
    glBindVertexArray(0);
    free(V); }

  platLog("atmos  %d corona quads  %d dust motes",coronaCount/6,dustCount);
  return 1;
}

/* The dust shader carries MAXL lights.  A ship this size has ten times that,
   so the slots go to the lamps nearest the camera and are refilled as it
   moves -- a partial selection, not a sort, because only the count matters. */
static void nearestLamps(const float *eye){
  float bestD[MAXL]; int bestI[MAXL];
  nNear=0;
  for(int i=0;i<nLight;i++){
    Light *L=&LIGHTS[i];
    float dx=L->p[0]-eye[0], dy=L->p[1]-eye[1], dz=L->p[2]-eye[2];
    float d=dx*dx+dy*dy+dz*dz;
    if(nNear<MAXL){
      bestD[nNear]=d; bestI[nNear]=i; nNear++;
    } else {
      int worst=0;
      for(int k=1;k<MAXL;k++) if(bestD[k]>bestD[worst]) worst=k;
      if(d<bestD[worst]){ bestD[worst]=d; bestI[worst]=i; }
    }
  }
  for(int k=0;k<nNear;k++){
    Light *L=&LIGHTS[bestI[k]];
    LP[k*3]=L->p[0]; LP[k*3+1]=L->p[1]; LP[k*3+2]=L->p[2];
    LC[k*3]=L->c[0]; LC[k*3+1]=L->c[1]; LC[k*3+2]=L->c[2];
    LR[k]=L->r;
  }
}

void atmosDraw(const FrameState *F){
  if(R_DEBUG) return;
  nearestLamps(F->camPos);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE);      /* both passes are pure addition */
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_PROGRAM_POINT_SIZE);

  glUseProgram(pDust);
  glUniformMatrix4fv(UDu.uProj,1,GL_FALSE,F->proj);
  glUniformMatrix4fv(UDu.uView,1,GL_FALSE,F->view);
  glUniform3fv(UDu.uCamPos,1,F->camPos);
  glUniform3fv(UDu.uLightPos,MAXL,LP);
  glUniform3fv(UDu.uLightCol,MAXL,LC);
  glUniform1fv(UDu.uLightRange,MAXL,LR);
  glUniform1i(UDu.uLightCount,nNear);
  glUniform1f(UDu.uTime,F->time);
  glUniform1f(UDu.uScale,platH*0.010f);
  glBindVertexArray(dustVAO);
  glDrawArrays(GL_POINTS,0,dustCount);

  glUseProgram(pCorona);
  glUniformMatrix4fv(UCo.uProj,1,GL_FALSE,F->proj);
  glUniformMatrix4fv(UCo.uView,1,GL_FALSE,F->view);
  glUniform1f(UCo.uGain,0.55f);
  glUniform3fv(UCo.uStyleC,STY_N,STY_C);
  glBindVertexArray(coronaVAO);
  glDrawArrays(GL_TRIANGLES,0,coronaCount);

  glBindVertexArray(0);
  glDisable(GL_PROGRAM_POINT_SIZE);
  glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}
