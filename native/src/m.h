/* ============================================================
   0.  MATH  --  ported from map.html section 0, value for value.
   THE HASH IS THE CONTRACT.  Every texture, every scatter and
   every flake in this game is a function of ihash(), so if the
   C version disagrees with the JS version by one bit the whole
   ship comes out a different ship.  Math.imul is a 32-bit
   signed multiply that wraps; uint32_t wraps by definition, so
   that is what it is spelled as here.
   ============================================================ */
#ifndef M_H
#define M_H
#include <math.h>
#include <stdint.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif
#define TAU 6.283185307179586

static inline float cl(float x){ return x<0?0:(x>1?1:x); }
static inline float clv(float x,float a,float b){ return x<a?a:(x>b?b:x); }
static inline float ss(float a,float b,float x){
  float t=cl((x-a)/(b-a)); return t*t*(3-2*t);
}
static inline float mixf(float a,float b,float t){ return a+(b-a)*t; }
static inline float sgn(float x){ return x<0?-1.f:(x>0?1.f:0.f); }
static inline float hypot3(float x,float y,float z){ return sqrtf(x*x+y*y+z*z); }
static inline float hypot2(float x,float y){ return sqrtf(x*x+y*y); }

/* Math.imul */
static inline uint32_t imul(int32_t a,int32_t b){
  return (uint32_t)((int64_t)a*(int64_t)b & 0xFFFFFFFFu);
}

static inline float ihash(int32_t a,int32_t b,int32_t s){
  uint32_t h = imul(a,374761393) ^ imul(b,668265263) ^ imul(s,362437);
  h = imul((int32_t)(h ^ (h>>13)), 1274126177);
  return (float)((double)(h ^ (h>>16)) / 4294967296.0);
}

/* periodic value noise -- independent periods keep every texture
   seamlessly tileable */
static inline float pnA(float u,float v,int Px,int Py,int s){
  float X=u*Px, Y=v*Py;
  float fx=floorf(X), fy=floorf(Y);
  int xi=(int)fx, yi=(int)fy;
  float xf=X-fx, yf=Y-fy;
  float a=xf*xf*xf*(xf*(xf*6-15)+10), b=yf*yf*yf*(yf*(yf*6-15)+10);
  #define H(p,q) ihash(((( p)%Px)+Px)%Px, (((q)%Py)+Py)%Py, s)
  float p00=H(xi,yi), p10=H(xi+1,yi), p01=H(xi,yi+1), p11=H(xi+1,yi+1);
  #undef H
  float t0=p00+(p10-p00)*a, t1=p01+(p11-p01)*a;
  return t0+(t1-t0)*b;
}

static inline float fbmA(float u,float v,int oct,int Px,int Py,int s){
  float acc=0,amp=1,n=0; int px=Px,py=Py;
  for(int i=0;i<oct;i++){ acc+=pnA(u,v,px,py,s+i*97)*amp; n+=amp; amp*=0.5f; px*=2; py*=2; }
  return acc/n;
}
static inline float fbmP(float u,float v,int oct,int P,int s){ return fbmA(u,v,oct,P,P,s); }

/* periodic worley -- aggregate, flakes, cracks */
static inline float worleyP(float u,float v,int P,int s){
  float X=u*P, Y=v*P;
  int xi=(int)floorf(X), yi=(int)floorf(Y);
  float best=9;
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
    int cx=xi+dx, cy=yi+dy;
    int mx=((cx%P)+P)%P, my=((cy%P)+P)%P;
    float r1=ihash(mx,my,s), r2=ihash(mx,my,s+9161);
    float px=cx+r1, py=cy+r2;
    float d=(px-X)*(px-X)+(py-Y)*(py-Y);
    if(d<best) best=d;
  }
  return sqrtf(best);
}

/* ---- matrices: column-major float[16], as WebGL had them ---------- */
static inline void perspectiveM(float *o,float fovy,float aspect,float zn,float zf){
  float f=1.f/tanf(fovy*0.5f), nf=1.f/(zn-zf);
  o[0]=f/aspect;o[1]=0;o[2]=0;o[3]=0;
  o[4]=0;o[5]=f;o[6]=0;o[7]=0;
  o[8]=0;o[9]=0;o[10]=(zf+zn)*nf;o[11]=-1;
  o[12]=0;o[13]=0;o[14]=2*zf*zn*nf;o[15]=0;
}
static inline void lookAtM(float *o,const float *e,const float *c,const float *up){
  float fx=c[0]-e[0], fy=c[1]-e[1], fz=c[2]-e[2];
  float l=1.f/hypot3(fx,fy,fz); fx*=l; fy*=l; fz*=l;
  float sx=fy*up[2]-fz*up[1], sy=fz*up[0]-fx*up[2], sz=fx*up[1]-fy*up[0];
  l=1.f/hypot3(sx,sy,sz); sx*=l; sy*=l; sz*=l;
  float ux=sy*fz-sz*fy, uy=sz*fx-sx*fz, uz=sx*fy-sy*fx;
  o[0]=sx;o[1]=ux;o[2]=-fx;o[3]=0;
  o[4]=sy;o[5]=uy;o[6]=-fy;o[7]=0;
  o[8]=sz;o[9]=uz;o[10]=-fz;o[11]=0;
  o[12]=-(sx*e[0]+sy*e[1]+sz*e[2]);
  o[13]=-(ux*e[0]+uy*e[1]+uz*e[2]);
  o[14]=fx*e[0]+fy*e[1]+fz*e[2];
  o[15]=1;
}
static inline void mulM(float *o,const float *a,const float *b){ /* o = a*b */
  for(int c=0;c<4;c++)for(int r=0;r<4;r++){
    float s=0; for(int k=0;k<4;k++) s+=a[k*4+r]*b[c*4+k];
    o[c*4+r]=s;
  }
}
static inline void identM(float *o){
  for(int i=0;i<16;i++) o[i]=0;
  o[0]=o[5]=o[10]=o[15]=1;
}

#endif
