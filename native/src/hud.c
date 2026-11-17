/* ============================================================
   11.  HUD  --  the overlay, and the font it is written in.

   The browser drew this half in canvas2d and half in DOM, and
   neither exists here.  What replaced them is one immediate-mode
   quad batcher and one procedural font: a 5x7 cell per glyph,
   seven bytes each, five bits used -- 51 characters for 357
   bytes, which is less than one PNG's header and is the only
   kind of typeface this budget can afford.

   Everything is drawn in pixels with the origin top-left, so a
   layout reads the way the CSS did.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "app.h"
#include "hud.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GLuint glMakeProgram(const char *vs,const char *fs,const char *what);

/* the previz's own :root, so the exe and the browser agree about what
   colour a readout is */
const float HUD_BONE [3]={0.902f,0.890f,0.855f};
const float HUD_TEAL [3]={0.310f,0.839f,0.627f};
const float HUD_AMBER[3]={0.910f,0.627f,0.125f};
const float HUD_DIM  [3]={0.471f,0.529f,0.498f};
const float HUD_INK  [3]={0.039f,0.047f,0.055f};
const float HUD_CRIT [3]={0.886f,0.337f,0.290f};

/* A SUBTITLE IS NOW EIGHTY GLYPHS AND NOT TWENTY.  The 5x7 stencil drew a
   letter as up to seven runs -- so a line of English is a few hundred quads
   -- and a Japanese line is one quad per character.  4096 was never near
   the limit and still is not; what moved is the vertex, which carries uv. */
#define MAXQUAD 4096
#define QSTRIDE 8                      /* x,y,u,v,r,g,b,a per vertex */
static float *QV; static int nQV;
static GLuint pHud, hudVAO, hudVB, glyphTex;
static GLint uRes, uGlyph;
/* where hudRect samples: the middle of the all-white row jafont's sheet
   carries under the glyphs, so an untextured quad costs no branch */
static float whiteU=0.5f, whiteV=1.0f;

/* ---- the font.  Five bits a row, seven rows, one byte a row ---------
   Drawn on a 5x7 cell the way a stencil is: no curves, no descenders, and
   every stroke one unit wide -- which is what the ship's own placards are
   cut with, so the HUD and the world agree about what lettering is. */
static const unsigned char FONT[51][7]={
 {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
 {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
 {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
 {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
 {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
 {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
 {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
 {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
 {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
 {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
 {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
 {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
 {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
 {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
 {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
 {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
 {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
 {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
 {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
 {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
 {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
 {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
 {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
 {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
 {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
 {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
 {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
 {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
 {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
 {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
 {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
 {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
 {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
 {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
 {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
 {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, /* : */
 {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, /* , */
 {0x00,0x00,0x00,0x00,0x00,0x00,0x04}, /* . */
 {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
 {0x00,0x00,0x04,0x0E,0x04,0x00,0x00}, /* + */
 {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* _ (a space that is not one) */
 {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
 {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
 {0x0A,0x1F,0x0A,0x0A,0x0A,0x1F,0x0A}, /* # */
 {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, /* / */
 {0x11,0x02,0x02,0x04,0x08,0x08,0x11}, /* % -- a charge readout needs one */
 {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
 {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* ? */
 {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* the middle dot the zone subs use */
};
static int glyph(int c){
  if(c>='a'&&c<='z') c-=32;
  if(c==' ') return 0;
  if(c>='A'&&c<='Z') return 1+(c-'A');
  if(c>='0'&&c<='9') return 27+(c-'0');
  switch(c){
  case ':': return 37; case ',': return 38; case '.': return 39;
  case '-': return 40; case '+': return 41; case '_': return 42;
  case '(': return 43; case ')': return 44; case '#': return 45;
  case '/': return 46; case '%': return 47; case '!': return 48;
  case '?': return 49;
  }
  return 0;
}
const unsigned char *hudGlyph(int c){ return FONT[glyph(c)]; }


/* ---- the batcher ---------------------------------------------------- */
static void vtx(float x,float y,float u,float v,const float *c,float a){
  if(nQV+QSTRIDE>MAXQUAD*6*QSTRIDE) return;
  float *o=QV+nQV; nQV+=QSTRIDE;
  o[0]=x; o[1]=y; o[2]=u; o[3]=v;
  o[4]=c[0]; o[5]=c[1]; o[6]=c[2]; o[7]=a;
}
void hudTexRect(float x,float y,float w,float h,
                float u0,float v0,float u1,float v1,const float *c,float a){
  vtx(x,    y,    u0,v0,c,a); vtx(x+w,y,    u1,v0,c,a);
  vtx(x+w,  y+h,  u1,v1,c,a);
  vtx(x,    y,    u0,v0,c,a); vtx(x+w,y+h,  u1,v1,c,a);
  vtx(x,    y+h,  u0,v1,c,a);
}
void hudRect(float x,float y,float w,float h,const float *c,float a){
  hudTexRect(x,y,w,h,whiteU,whiteV,whiteU,whiteV,c,a);
}
/* px is the pixel size of one font unit; a glyph is 5x7 of them */
float hudText(float x,float y,float px,const char *s,const float *c,float a){
  float x0=x;
  for(;*s;s++){
    if(*s=='\n'){ y+=px*10; x=x0; continue; }
    const unsigned char *g=FONT[glyph((unsigned char)*s)];
    for(int r=0;r<7;r++){
      unsigned char row=g[r];
      int run=0;
      for(int b=0;b<6;b++){
        int on = b<5 && (row&(0x10>>b));
        if(on) run++;
        else if(run){ hudRect(x+(b-run)*px,y+r*px,run*px,px,c,a); run=0; }
      }
    }
    x+=px*6;
  }
  return x;
}
float hudWidth(float px,const char *s){
  float w=0, m=0;
  for(;*s;s++){ if(*s=='\n'){ if(w>m)m=w; w=0; } else w+=px*6; }
  return w>m?w:m;
}

static const char *HUD_VS =
"#version 330 core\n"
"layout(location=0) in vec2 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
"layout(location=2) in vec4 aCol;\n"
"uniform vec2 uRes;\n"
"out vec4 vCol; out vec2 vUV;\n"
"void main(){\n"
"  vCol=aCol; vUV=aUV;\n"
"  vec2 p=vec2(aPos.x/uRes.x*2.0-1.0, 1.0-aPos.y/uRes.y*2.0);\n"
"  gl_Position=vec4(p,0.0,1.0);\n"
"}\n";
static const char *HUD_FS =
"#version 330 core\n"
"in vec4 vCol; in vec2 vUV;\n"
"uniform sampler2D uGlyph;\n"
"out vec4 outColor;\n"
"void main(){\n"
"  float m=texture(uGlyph,vUV).r;\n"
"  float a=vCol.a*m;\n"
"  outColor=vec4(vCol.rgb*a,a);\n"
"}\n";

/* THE SHEET HAS TO EXIST BEFORE THE FIRST QUAD, not before the first
   GLYPH: hudRect samples it too.  So hudInit lays down a 1x1 white one and
   kfont.c replaces it when it has read jafont_data.h -- and if the build
   carries no Japanese at all, the placeholder is what the whole overlay
   runs on and nothing anywhere has to know. */
static void makeSheet(const unsigned char *r8,int w,int h){
  if(!glyphTex) glGenTextures(1,&glyphTex);
  glBindTexture(GL_TEXTURE_2D,glyphTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexImage2D(GL_TEXTURE_2D,0,GL_R8,w,h,0,GL_RED,GL_UNSIGNED_BYTE,r8);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}
void hudGlyphSheet(const unsigned char *r8,int w,int h){
  makeSheet(r8,w,h);
  /* the white row is the last one the packer wrote; sample its middle, far
     enough from any glyph that GL_LINEAR cannot reach one */
  whiteU=0.5f; whiteV=1.0f-0.5f/(float)h;
}

int hudInit(void){
  QV=(float*)malloc(sizeof(float)*MAXQUAD*6*QSTRIDE);
  pHud=glMakeProgram(HUD_VS,HUD_FS,"hud");
  if(!pHud) return 0;
  uRes=glGetUniformLocation(pHud,"uRes");
  uGlyph=glGetUniformLocation(pHud,"uGlyph");
  { static const unsigned char one[1]={255}; makeSheet(one,1,1); }
  whiteU=whiteV=0.5f;
  glGenVertexArrays(1,&hudVAO);
  glBindVertexArray(hudVAO);
  glGenBuffers(1,&hudVB);
  glBindBuffer(GL_ARRAY_BUFFER,hudVB);
  glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)MAXQUAD*6*QSTRIDE*4,0,GL_DYNAMIC_DRAW);
  { const int S=QSTRIDE*4;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,S,(const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,S,(const void*)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,S,(const void*)16); }
  glBindVertexArray(0);
  return 1;
}

void hudBegin(void){ nQV=0; }

void hudEnd(void){
  if(!nQV) return;
  /* A GUEST DRAW INHERITS THE HOST'S GL STATE.  The scene pass leaves
     back-face culling on with a counter-clockwise front, and these quads are
     wound for a y-DOWN screen -- so every one of them is back-facing and the
     whole overlay is culled, silently, with no GL error to show for it. */
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(pHud);
  glUniform2f(uRes,(float)platW,(float)platH);
  glUniform1i(uGlyph,0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D,glyphTex);
  glBindVertexArray(hudVAO);
  glBindBuffer(GL_ARRAY_BUFFER,hudVB);
  glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)nQV*4,QV);
  glDrawArrays(GL_TRIANGLES,0,nQV/QSTRIDE);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}
