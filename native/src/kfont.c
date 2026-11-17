/* ============================================================
   HANGUL, DRAWN RATHER THAN STORED.

   The story is in Korean and the overlay had a 48-glyph Latin
   stencil, so the subtitles had nowhere to go.  A bitmap font
   was never an option: 2,350 syllables at any legible size is
   more bytes than the whole ship.

   But Hangul is not 11,172 pictures, it is 24 letters and a
   layout rule -- so this file stores the letters and applies the
   rule.  Fourteen consonant shapes, fourteen vowel parts, each
   one a handful of strokes in its own unit box; a syllable is
   decomposed into initial, medial and final, and each part is
   scaled into the box the medial's orientation asks for.  The
   whole typeface is 119 strokes, five bytes each.

   Every stroke is drawn with the same quad batcher the rest of
   the overlay uses, so a Korean subtitle costs rectangles and
   not a texture.
   ============================================================ */
#include "hud.h"
#include "kfont.h"
#include "jafont_data.h"
#include <math.h>
#include <stdlib.h>

/* a stroke in a jamo's own 0..1 box, in hundredths, y down */
enum { HB=0, VB, DG, RG };
typedef struct { unsigned char k,x0,y0,x1,y1; } Stk;

/* ---- the fourteen consonants -------------------------------------- */
static const Stk CS[]={
 /* 0 GIYEOK   */ {HB, 6,10,94,10},{VB,86,10,86,96},
 /* 1 NIEUN    */ {VB,14, 6,14,90},{HB,14,88,94,88},
 /* 2 DIGEUT   */ {HB,10, 8,92, 8},{VB,14, 8,14,90},{HB,10,88,92,88},
 /* 3 RIEUL    */ {HB,10, 6,90, 6},{VB,84, 6,84,46},{HB,10,46,90,46},
                  {VB,14,46,14,90},{HB,10,88,90,88},
 /* 4 MIEUM    */ {HB,10, 8,90, 8},{HB,10,90,90,90},{VB,13, 8,13,90},
                  {VB,87, 8,87,90},
 /* 5 BIEUP    */ {VB,13, 6,13,92},{VB,87, 6,87,92},{HB,13,52,87,52},
                  {HB,13,90,87,90},
 /* 6 SIOT     */ {DG,50, 6,10,94},{DG,50, 6,90,94},
 /* 7 IEUNG    */ {RG, 8, 8,92,92},
 /* 8 JIEUJ    */ {HB, 8,10,92,10},{DG,50,14,10,94},{DG,50,14,90,94},
 /* 9 CHIEUCH  */ {VB,50, 0,50,12},{HB, 8,24,92,24},{DG,50,28,12,96},
                  {DG,50,28,88,96},
 /*10 KIEUK    */ {HB, 6,10,94,10},{VB,86,10,86,96},{HB,32,52,90,52},
 /*11 TIEUT    */ {HB,10, 8,92, 8},{VB,14, 8,14,90},{HB,10,88,92,88},
                  {HB,14,48,88,48},
 /*12 PIEUP    */ {HB, 6,14,94,14},{VB,28,14,28,84},{VB,72,14,72,84},
                  {HB, 6,84,94,84},
 /*13 HIEUH    */ {VB,50, 0,50,10},{HB,16,20,84,20},{RG,18,34,82,94},
};
static const unsigned char COFF[15]={0,2,4,7,12,16,20,22,23,26,30,33,37,41,44};

/* ---- the nine upright vowel parts, in a tall box on the right ------ */
static const Stk VS[]={
 /* 0 I   */ {VB,45, 0,45,100},
 /* 1 A   */ {VB,42, 0,42,100},{HB,42,52,96,52},
 /* 2 YA  */ {VB,42, 0,42,100},{HB,42,36,96,36},{HB,42,68,96,68},
 /* 3 EO  */ {VB,58, 0,58,100},{HB, 4,52,58,52},
 /* 4 YEO */ {VB,58, 0,58,100},{HB, 4,36,58,36},{HB, 4,68,58,68},
 /* 5 AE  */ {VB,28, 0,28,100},{HB,28,52,66,52},{VB,76, 0,76,100},
 /* 6 YAE */ {VB,28, 0,28,100},{HB,28,36,66,36},{HB,28,68,66,68},
             {VB,76, 0,76,100},
 /* 7 E   */ {VB,44, 0,44,100},{HB, 6,52,44,52},{VB,84, 0,84,100},
 /* 8 YE  */ {VB,44, 0,44,100},{HB, 6,36,44,36},{HB, 6,68,44,68},
             {VB,84, 0,84,100},
};
static const unsigned char VOFF[10]={0,1,3,6,8,11,14,18,21,25};

/* ---- the five lying vowel parts, in a wide box underneath ---------- */
static const Stk HS[]={
 /* 0 O  */ {VB,50, 0,50,58},{HB, 4,60,96,60},
 /* 1 YO */ {VB,33, 0,33,58},{VB,67, 0,67,58},{HB, 4,60,96,60},
 /* 2 U  */ {HB, 4,24,96,24},{VB,50,24,50,92},
 /* 3 YU */ {HB, 4,24,96,24},{VB,33,24,33,92},{VB,67,24,67,92},
 /* 4 EU */ {HB, 4,50,96,50},
};
static const unsigned char HOFF[6]={0,2,5,7,10,11};

/* ---- the three jamo alphabets, as indices into the shapes above ----
   A doubled initial is the same consonant twice side by side, and a
   cluster final is two different ones -- so nineteen initials and
   twenty-eight finals cost two small tables instead of 47 shapes. */
static const unsigned char CHO[19*2]={
  0,0, 0,1, 1,0, 2,0, 2,1, 3,0, 4,0, 5,0, 5,1, 6,0,
  6,1, 7,0, 8,0, 8,1, 9,0,10,0,11,0,12,0,13,0 };
static const unsigned char JONG[28*2]={
  255,255, 0,255, 0,0, 0,6, 1,255, 1,8, 1,13, 2,255,
  3,255, 3,0, 3,4, 3,5, 3,6, 3,11, 3,12, 3,13,
  4,255, 5,255, 5,6, 6,255, 6,6, 7,255, 8,255,
  9,255, 10,255, 11,255, 12,255, 13,255 };
/* medial -> (upright part, lying part); 255 for absent */
static const unsigned char JUNG[21*2]={
  1,255, 5,255, 2,255, 6,255, 3,255, 7,255, 4,255, 8,255,
  255,0, 1,0, 5,0, 0,0, 255,1, 255,2, 3,2, 7,2, 0,2,
  255,3, 255,4, 0,4, 0,255 };

/* ---- putting a stroke on the screen -------------------------------- */
static void strokes(const Stk *s,int n,float bx,float by,float bw,float bh,
                    float th,const float *c,float a){
  for(int i=0;i<n;i++){
    float x0=bx+s[i].x0*0.01f*bw, y0=by+s[i].y0*0.01f*bh;
    float x1=bx+s[i].x1*0.01f*bw, y1=by+s[i].y1*0.01f*bh;
    switch(s[i].k){
    case HB: hudRect(x0,y0-th*0.5f,x1-x0,th,c,a); break;
    case VB: hudRect(x0-th*0.5f,y0,th,y1-y0,c,a); break;
    case DG: {
      /* a diagonal is a staircase of squares -- at subtitle sizes the
         difference between that and a real one is under a pixel */
      float dx=x1-x0, dy=y1-y0, L=sqrtf(dx*dx+dy*dy);
      int n2=(int)(L/(th*0.55f))+1;
      for(int k=0;k<=n2;k++){
        float u=(float)k/n2;
        hudRect(x0+dx*u-th*0.5f,y0+dy*u-th*0.5f,th,th,c,a);
      }
      break; }
    case RG: {
      /* ieung and the bowl of hieuh.  Twelve squares round an ellipse
         reads as a circle and a square ring does not, which matters:
         it is the only round letter in the alphabet. */
      float cx=(x0+x1)*0.5f, cy=(y0+y1)*0.5f;
      float rx=(x1-x0)*0.5f-th*0.5f, ry=(y1-y0)*0.5f-th*0.5f;
      for(int k=0;k<12;k++){
        float ang=k*0.5235988f;
        hudRect(cx+cosf(ang)*rx-th*0.5f,cy+sinf(ang)*ry-th*0.5f,th,th,c,a);
      }
      break; }
    }
  }
}
static void cons(int ci,float bx,float by,float bw,float bh,float th,
                 const float *c,float a){
  if(ci>13) return;
  strokes(CS+COFF[ci],COFF[ci+1]-COFF[ci],bx,by,bw,bh,th,c,a);
}

/* ---- one syllable -------------------------------------------------- */
static void syllable(int code,float x,float y,float px,const float *c,float a){
  int s=code-0xAC00;
  int cho=s/588, jung=(s%588)/28, jong=s%28;
  int vp=JUNG[jung*2], hp=JUNG[jung*2+1];
  float th=px*0.088f; if(th<1.f) th=1.f;
  float bot = jong ? 0.70f : 1.00f;

  float cx,cy,cw,ch;
  if(vp!=255&&hp!=255){ cx=0.03f; cy=0.02f; cw=0.50f; ch=bot*0.58f; }
  else if(hp!=255)    { cx=0.14f; cy=0.02f; cw=0.72f; ch=bot*0.60f; }
  else                { cx=0.02f; cy=0.03f; cw=0.54f; ch=bot*0.94f; }

  /* the initial, twice over if it is a tense one */
  { int ic=CHO[cho*2], dbl=CHO[cho*2+1];
    if(dbl){
      cons(ic,x+cx*px,y+cy*px,cw*0.48f*px,ch*px,th,c,a);
      cons(ic,x+(cx+cw*0.52f)*px,y+cy*px,cw*0.48f*px,ch*px,th,c,a);
    } else cons(ic,x+cx*px,y+cy*px,cw*px,ch*px,th,c,a); }

  if(vp!=255){
    float vx = (hp!=255)?0.56f:0.58f, vw=(hp!=255)?0.44f:0.42f;
    float vh = (hp!=255)?bot*0.74f:bot;
    strokes(VS+VOFF[vp],VOFF[vp+1]-VOFF[vp],
            x+vx*px,y,vw*px,vh*px,th,c,a);
  }
  if(hp!=255){
    float hy = (vp!=255)?bot*0.66f:bot*0.64f;
    float hw = (vp!=255)?0.94f:1.00f;
    strokes(HS+HOFF[hp],HOFF[hp+1]-HOFF[hp],
            x,y+hy*px,hw*px,(bot-hy)*px,th,c,a);
  }
  if(jong){
    int a1=JONG[jong*2], a2=JONG[jong*2+1];
    /* a final consonant has a quarter of the cell and has to stay a
       letter in it: thinner strokes, and the top half gives up 30%
       rather than 28, which is the whole difference between a legible
       batchim and a smear */
    float jt=th*0.90f;
    if(a2!=255){
      cons(a1,x+0.05f*px,y+0.725f*px,0.43f*px,0.265f*px,jt,c,a);
      cons(a2,x+0.52f*px,y+0.725f*px,0.43f*px,0.265f*px,jt,c,a);
    } else cons(a1,x+0.17f*px,y+0.725f*px,0.66f*px,0.265f*px,jt,c,a);
  }
}

/* ============================================================
   AND THE ONE ALPHABET THAT COULD NOT BE DRAWN.

   Everything above is here because Hangul has a rule: 24 letters
   and a layout, and 11,172 syllables fall out of them.  Japanese
   does not.  Kana would go the same way as the jamo -- 92 shapes
   and two marks -- but the story is written the way Japanese is
   written, with kanji, and there is no rule that assembles one
   out of pieces small enough to store.  Hand-cut strokes for
   three hundred of them is not a typeface, it is three hundred
   drawings, and they would look like it.

   So Japanese is the exception, and it is bounded the way this
   build bounds everything: tools/jafont.py rasterises the
   glyphs THE STORY ACTUALLY USES, once, at JF_CELL dots, and
   jafont_data.h is that set and nothing else.  A font is
   megabytes; the ship's own vocabulary is about thirty
   kilobytes, against the five hundred the exe has spare.

   Re-run tools/jafont.py after any change to Japanese text --
   the same standing obligation storypack.py has.  A character
   with no glyph draws as a hollow box, so a forgotten run is
   visible on the first screen that shows it rather than being a
   blank nobody notices.
   ============================================================ */
static unsigned char *SHEET;
static int shW, shH;

/* JF_CP is emitted sorted, so this is a binary search and not a scan */
static int jaIndex(int cp){
#if JF_N > 0
  int lo=0, hi=JF_N-1;
  while(lo<=hi){
    int m=(lo+hi)>>1, v=JF_CP[m];
    if(v==cp) return m;
    if(v<cp) lo=m+1; else hi=m-1;
  }
#else
  (void)cp;
#endif
  return -1;
}

/* Full-width: one em of advance, and two cells to the dialogue clock.
   Hangul and the CJK blocks both, because the question every caller is
   really asking is "does this character take a whole cell". */
static int wideCp(int cp){
  if(cp>=0xAC00&&cp<=0xD7A3) return 1;          /* Hangul syllables */
  if(cp>=0x3000&&cp<=0x30FF) return 1;          /* CJK punctuation, kana */
  if(cp>=0x4E00&&cp<=0x9FFF) return 1;          /* kanji */
  if(cp>=0xFF01&&cp<=0xFF60) return 1;          /* fullwidth forms */
  return 0;
}
static int isJa(int cp){
  return cp!=0x3000 && wideCp(cp) && !(cp>=0xAC00&&cp<=0xD7A3);
}

void krInit(void){
  shW = JF_COLS*JF_CELL;
  /* two rows of white under the glyphs.  hudRect samples the middle of the
     last one, so every quad in the overlay -- textured or not -- comes out
     of one sheet, one vertex format and one draw call. */
  shH = JF_ROWS*JF_CELL + 2;
  SHEET=(unsigned char*)calloc((size_t)shW*shH,1);
  if(!SHEET) return;
#if JF_N > 0
  { const int stride=(JF_CELL+7)/8;
    for(int g=0;g<JF_N;g++){
      int cx=(g%JF_COLS)*JF_CELL, cy=(g/JF_COLS)*JF_CELL;
      const unsigned char *b=JF_BITS+(size_t)g*stride*JF_CELL;
      for(int y=0;y<JF_CELL;y++)
        for(int x=0;x<JF_CELL;x++)
          if(b[y*stride+(x>>3)] & (0x80>>(x&7)))
            SHEET[(size_t)(cy+y)*shW+cx+x]=255;
    } }
#endif
  for(int y=shH-2;y<shH;y++)
    for(int x=0;x<shW;x++) SHEET[(size_t)y*shW+x]=255;
  hudGlyphSheet(SHEET,shW,shH);
}

/* one full-width glyph, in the em box the syllable machine also uses */
static void jaGlyph(int gi,float x,float y,float px,const float *c,float a){
  if(gi<0){
    /* NO GLYPH IS DRAWN AS A BOX AND NOT AS A GAP.  A missing character
       that renders as nothing reads as a translation with a hole in it;
       a box reads as "run tools/jafont.py", which is what it means. */
    float t=px*0.06f; if(t<1.f) t=1.f;
    float m=px*0.12f, s=px-m*2.f;
    hudRect(x+m,y+m,s,t,c,a);          hudRect(x+m,y+m+s-t,s,t,c,a);
    hudRect(x+m,y+m,t,s,c,a);          hudRect(x+m+s-t,y+m,t,s,c,a);
    return;
  }
  float u0=(float)((gi%JF_COLS)*JF_CELL)/(float)shW;
  float v0=(float)((gi/JF_COLS)*JF_CELL)/(float)shH;
  float u1=u0+(float)JF_CELL/(float)shW;
  float v1=v0+(float)JF_CELL/(float)shH;
  hudTexRect(x,y,px,px,u0,v0,u1,v1,c,a);
}

/* ---- UTF-8 --------------------------------------------------------- */
static int utf8(const char *s,int *adv){
  const unsigned char *p=(const unsigned char*)s;
  if(p[0]<0x80){ *adv=1; return p[0]; }
  if((p[0]&0xE0)==0xC0&&p[1]){ *adv=2; return ((p[0]&0x1F)<<6)|(p[1]&0x3F); }
  if((p[0]&0xF0)==0xE0&&p[1]&&p[2]){ *adv=3;
    return ((p[0]&0x0F)<<12)|((p[1]&0x3F)<<6)|(p[2]&0x3F); }
  if((p[0]&0xF8)==0xF0&&p[1]&&p[2]&&p[3]){ *adv=4;
    return ((p[0]&0x07)<<18)|((p[1]&0x3F)<<12)|((p[2]&0x3F)<<6)|(p[3]&0x3F); }
  *adv=1; return '?';
}
/* everything that is not a syllable falls back to a Latin cell */
#define ASC_ADV (6.f/9.f)   /* the 5x7 cell advances six units of px/9 */
static int latin(int cp,char *out){
  if(cp<128){ out[0]=(char)cp; out[1]=0; return 1; }
  switch(cp){
  case 0x2026: out[0]=out[1]=out[2]='.'; out[3]=0; return 3;  /* … */
  case 0x00B7: case 0x2022: out[0]='.'; out[1]=0; return 1;
  case 0x2018: case 0x2019: out[0]='\''; out[1]=0; return 1;
  case 0x201C: case 0x201D: out[0]='"'; out[1]=0; return 1;
  }
  out[0]='.'; out[1]=0; return 1;
}

float krText(float x,float y,float px,const char *s,const float *c,float a){
  float unit=px/9.f;
  while(*s){
    int n, cp=utf8(s,&n); s+=n;
    if(cp>=0xAC00&&cp<=0xD7A3){ syllable(cp,x,y,px,c,a); x+=px; }
    else if(cp==0x3000){ x+=px; }              /* the ideographic space */
    else if(isJa(cp)){ jaGlyph(jaIndex(cp),x,y,px,c,a); x+=px; }
    else{
      char b[8]; int k=latin(cp,b); (void)k;
      x=hudText(x,y+px*0.16f,unit,b,c,a);
    }
  }
  return x;
}
float krWidth(float px,const char *s){
  float w=0;
  while(*s){
    int n, cp=utf8(s,&n); s+=n;
    if(wideCp(cp)) w+=px;
    else{ char b[8]; int k=latin(cp,b); w+=ASC_ADV*px*k; }
  }
  return w;
}
int krCells(const char *s){
  int n2=0;
  while(*s){ int n, cp=utf8(s,&n); s+=n;
    n2 += wideCp(cp) ? 2 : 1; }
  return (n2+1)/2;
}

/* JAPANESE FORBIDS A FEW BREAKS THAT KOREAN DOES NOT.  A line may not
   OPEN with a full stop, a comma, a closing bracket or a long mark -- 禁則
   処理, and the one rule of it that a reader notices immediately.  Pushing
   the break one character earlier is the whole implementation. */
static int noStart(int cp){
  return cp==0x3001||cp==0x3002||cp==0x30FC||cp==0xFF01||cp==0xFF1F
       ||cp==0x300D||cp==0x300F||cp==0xFF09||cp==0x3015||cp==0xFF0C
       ||cp==0xFF0E;
}

int krWrap(const char *s,float cells,int maxLines,int *off,int *len){
  int line=0, start=0, i=0, lastSp=-1;
  float w=0;
  while(s[i]&&line<maxLines){
    int n, cp=utf8(s+i,&n);
    char tb[8];
    float adv = wideCp(cp) ? 1.f : ASC_ADV*latin(cp,tb);
    if(cp==' ') lastSp=i;
    if(w+adv>cells&&i>start){
      /* Korean has no spaces inside a word, so a break that insists on
         one either overflows the bar or leaves half of it empty.  Break
         at the last space if it is close enough to the end to be a word
         boundary rather than a paragraph, and mid-run otherwise. */
      int cut = (lastSp>start && (float)(i-lastSp)<cells*1.2f) ? lastSp : i;
      /* ... and never so that the next line begins with a mark that cannot
         begin one.  Walk back over a run of them, not just the first. */
      { int guard=0;
        while(cut>start+1 && guard++<4){
          int m, c2=utf8(s+cut,&m);
          if(!noStart(c2)) break;
          do { cut--; } while(cut>start && ((unsigned char)s[cut]&0xC0)==0x80);
        } }
      off[line]=start; len[line]=cut-start;
      line++;
      start = cut + (cut==lastSp?1:0);
      i=start; w=0; lastSp=-1;
      continue;
    }
    w+=adv; i+=n;
  }
  if(line<maxLines&&s[start]){ off[line]=start; len[line]=i-start; line++; }
  return line;
}
