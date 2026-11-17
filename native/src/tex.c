/* ============================================================
   4.  MATERIAL COMPILER
       Each material writes six values per texel:
       r, g, b, height, roughness, emissive.
       Height becomes a tangent-space normal map by Sobel.

   Texture operator machine.
   Materials are not written as functions -- they are short
   programs over a shared operator library, so adding a material
   costs a recipe, not code.  The 847 instructions that make the
   ship's forty surfaces are data (plan_data.h); the 27
   operators they call are this file.

   ONE REGISTER SET PER WORKER.  The browser compiled the layers
   one at a time on the main thread because it had no choice.
   Nothing in a recipe reads another material, so the forty are
   independent -- given private registers they compile on forty
   cores at once, and 9 MB of scratch per worker is a cheap price
   for the whole pass fitting inside a frame.
   ============================================================ */
#include "ship.h"
#include "app.h"
#include <stdlib.h>
#include <string.h>
#include "plan_data.h"

unsigned char *ALB_DATA, *NRM_DATA;

/* What the bounce needs to know about a surface it just hit: the average of
   its albedo, and the average of albedo*emissive.  Both fall out of the
   compile for free, and neither can be guessed -- a material's mean colour is
   not its base colour once the recipe has finished with it. */
float MAT_ALBEDO[TL][3], MAT_EMIS[TL][3];

float MAT_METAL[TL];
int   MAT_SURF[TL];
static float MAT_BUMP[TL];

enum { OP_FLAT=0, OP_PERLIN, OP_CELL, OP_GRAD, OP_RECTS, OP_GLOW, OP_SCAN,
       OP_STRIPE, OP_BLUR, OP_DISTORT, OP_ADD, OP_SUB, OP_MUL, OP_BLEND,
       OP_MASK, OP_LEVELS, OP_HSCB, OP_COLORIZE, OP_CAVITY, OP_NORMALS,
       OP_COPY, OP_MULC, OP_MIXC, OP_SHARP, OP_SOFTBOX, OP_WARP, OP_REPEAT };

#define NREG 9
#define NPIX (TN*TN)
#define RSZ  (NPIX*4)

typedef struct { float *r[NREG]; float *tmp, *cp; } Regs;

/* two-point cellular distance, periodic */
static void cell2(float u,float v,int P,int s,float *out){
  float X=u*P, Y=v*P;
  int xi=(int)floorf(X), yi=(int)floorf(Y);
  float f1=9,f2=9,id=0;
  for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){
    int cx=xi+dx, cy=yi+dy;
    int wx=((cx%P)+P)%P, wy=((cy%P)+P)%P;
    float r1=ihash(wx,wy,s), r2=ihash(wx,wy,s+9161);
    float px=cx+r1, py=cy+r2;
    float d=hypot2(px-X,py-Y);
    if(d<f1){ f2=f1; f1=d; id=ihash(wx,wy,s+7717); }
    else if(d<f2) f2=d;
  }
  out[0]=f1; out[1]=f2; out[2]=id;
}

/* ---- WARP: the register seen through a rotation, a scale and a shift ----
   Every output texel asks where it came from: undo the shift, undo the
   scale, undo the turn, and read the source there.  The source is a torus,
   so a read past an edge comes back in on the far side and a warped tile
   still tiles.  Bilinear, because a nearest-texel read of a turned pattern
   stair-steps every edge in it. */
static void tapWrapped(const float *src,float u,float v,float *out){
  const int N=TN;
  u-=floorf(u); v-=floorf(v);
  float fx=u*N-0.5f, fy=v*N-0.5f;
  float bx=floorf(fx), by=floorf(fy);
  float wx=fx-bx, wy=fy-by;
  int x0=(((int)bx%N)+N)%N, y0=(((int)by%N)+N)%N;
  int x1=(x0+1)%N, y1=(y0+1)%N;
  const float *a=src+(y0*N+x0)*4, *b=src+(y0*N+x1)*4;
  const float *c=src+(y1*N+x0)*4, *d=src+(y1*N+x1)*4;
  for(int k=0;k<3;k++){
    float top=a[k]+(b[k]-a[k])*wx, bot=c[k]+(d[k]-c[k])*wx;
    out[k]=top+(bot-top)*wy;
  }
}

static void warp(Regs *R,int d,float turns,float sx,float sy,float tx,float ty){
  const int N=TN;
  float *B=R->r[d], *src=R->cp;
  float th=turns*6.2831853f, ct=cosf(th), st=sinf(th);
  float isx=1.f/(sx!=0?sx:1.f), isy=1.f/(sy!=0?sy:1.f);
  memcpy(src,B,sizeof(float)*RSZ);
  for(int y=0;y<N;y++){
    float py=(y+0.5f)/N-0.5f;
    for(int x=0;x<N;x++){
      float px=(x+0.5f)/N-0.5f;
      float u=( px*ct+py*st)*isx+tx;
      float v=(-px*st+py*ct)*isy+ty;
      tapWrapped(src,u,v,B+(y*N+x)*4);
    }
  }
}

/* p[0] = word count including itself, p[1] = op, p[2] = dst, p[3..] = args.
   JS ins[j] is p[j+1], and "ins[j] === undefined" is "j+1 >= n". */
#define A(j)   (p[(j)+1])
#define HAS(j) ((j)+1 < n)

static void runOp(Regs *R,const float *p,int n){
  const int N=TN;
  int op=(int)p[1], d=(int)p[2];
  float *B=R->r[d];
  switch(op){

  case OP_FLAT:{
    float r=A(2),g=A(3),b=A(4);
    for(int i=0;i<RSZ;i+=4){ B[i]=r;B[i+1]=g;B[i+2]=b;B[i+3]=1; }
    break; }

  case OP_PERLIN:{  /* freq, oct, falloff, mode(0 fbm/1 ridged/2 sine), seed, amp, bias */
    int freq=(int)A(2), oct=(int)A(3);
    float fall=A(4); int mode=(int)A(5), seed=(int)A(6);
    float amp = HAS(7)? A(7) : 1.f;
    float bias= HAS(8)? A(8) : 0.f;
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      float u=(x+0.5f)/N, v=(y+0.5f)/N;
      float a=0,ampl=1,nrm=0; int f=freq;
      for(int o=0;o<oct;o++){
        float nn=pnA(u,v,f,f,seed+o*97);
        if(mode==1) nn=fabsf(nn*2-1);
        else if(mode==2) nn=0.5f+0.5f*sinf(nn*12.566371f);
        a+=nn*ampl; nrm+=ampl; ampl*=fall; f*=2;
      }
      float val=(a/nrm)*amp+bias;
      int i=(y*N+x)*4; B[i]=B[i+1]=B[i+2]=val; B[i+3]=1;
    }
    break; }

  case OP_CELL:{    /* count, mode(0 F1/1 F2-F1/2 id), amp, gamma, seed */
    int cnt=(int)A(2), mode=(int)A(3);
    float amp=A(4), gam=A(5); int seed=(int)A(6);
    float o3[3];
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      cell2((x+0.5f)/N,(y+0.5f)/N,cnt,seed,o3);
      float val = mode==0? o3[0] : (mode==1? (o3[1]-o3[0]) : o3[2]);
      val=powf(cl(val*amp),gam);
      int i=(y*N+x)*4; B[i]=B[i+1]=B[i+2]=val; B[i+3]=1;
    }
    break; }

  case OP_GRAD:{    /* mode(0 vert/1 horiz/2 radial), c0 rgb, c1 rgb */
    int m=(int)A(2);
    float r0=A(3),g0=A(4),b0=A(5), r1=A(6),g1=A(7),b1=A(8);
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      float u=(x+0.5f)/N, v=(y+0.5f)/N;
      float t = m==0? v : (m==1? u : cl(hypot2(u-0.5f,v-0.5f)*2));
      int i=(y*N+x)*4;
      B[i]=mixf(r0,r1,t); B[i+1]=mixf(g0,g1,t); B[i+2]=mixf(b0,b1,t); B[i+3]=1;
    }
    break; }

  case OP_RECTS:{   /* nx, ny, gap, bevel, jitter, seed -> beveled plate field */
    float nx=A(2), ny=A(3), gap=A(4), bev=A(5), jit=A(6);
    int seed=(int)A(7);
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      float u=(x+0.5f)/N, v=(y+0.5f)/N;
      float tx=fmodf(u*nx,1.f), ty=fmodf(v*ny,1.f);
      int ix=(int)floorf(u*nx), iy=(int)floorf(v*ny);
      float ex=tx<1-tx?tx:1-tx, ey=ty<1-ty?ty:1-ty;
      float e=ex<ey?ex:ey;
      float val = e<gap ? 0 : ss(gap,gap+bev,e);
      float j=(ihash(ix,iy,seed)-0.5f)*jit;
      int i=(y*N+x)*4; float o=cl(val+j*val);
      B[i]=B[i+1]=B[i+2]=o; B[i+3]=1;
    }
    break; }

  case OP_GLOW:{    /* cx, cy, rx, ry, sharp, r, g, b, alpha (drawn over) */
    float cx=A(2),cy=A(3),rx=A(4),ry=A(5),sh=A(6),
          r=A(7),g=A(8),b=A(9),al=A(10);
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      float u=(x+0.5f)/N, v=(y+0.5f)/N;
      float dx=fabsf(u-cx)/rx, dy=fabsf(v-cy)/ry;
      float k=(1-ss(sh,1,dx))*(1-ss(sh,1,dy))*al;
      if(k<=0) continue;
      int i=(y*N+x)*4;
      B[i]=mixf(B[i],r,k); B[i+1]=mixf(B[i+1],g,k); B[i+2]=mixf(B[i+2],b,k);
    }
    break; }

  case OP_SCAN:{    /* count, duty, soft, amp  (bands along v) */
    float cnt=A(2), duty=A(3), soft=A(4), amp=A(5);
    for(int y=0;y<N;y++){
      float v=(y+0.5f)/N, t=fmodf(v*cnt,1.f);
      float mn=t<1-t?t:1-t;
      float k=(1-ss(duty,duty+soft,mn))*amp;
      for(int x=0;x<N;x++){ int i=(y*N+x)*4; B[i]=B[i+1]=B[i+2]=k; B[i+3]=1; }
    }
    break; }

  case OP_STRIPE:{  /* count, soft (diagonal) */
    float cnt=A(2), soft=A(3);
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      float u=(x+0.5f)/N, v=(y+0.5f)/N;
      float k=ss(-soft,soft,sinf((u+v)*3.14159265f*cnt));
      int i=(y*N+x)*4; B[i]=B[i+1]=B[i+2]=k; B[i+3]=1;
    }
    break; }

  case OP_BLUR:{    /* rx, ry (texels) -- separable box, wraps for tiling */
    int rx=(int)A(2), ry=(int)A(3);
    float *tmp=R->tmp;
    for(int pass=0;pass<2;pass++){
      int r = pass==0?rx:ry, horiz = pass==0;
      const float *src = pass==0? B : tmp;
      float *dst       = pass==0? tmp : B;
      if(r<=0){ memcpy(dst,src,sizeof(float)*RSZ); continue; }
      float w=1.f/(2*r+1);
      for(int y=0;y<N;y++) for(int x=0;x<N;x++){
        float a=0,b2=0,c=0;
        for(int k=-r;k<=r;k++){
          int sx=horiz?(((x+k)%N)+N)%N:x, sy=horiz?y:(((y+k)%N)+N)%N;
          int i=(sy*N+sx)*4; a+=src[i]; b2+=src[i+1]; c+=src[i+2];
        }
        int i=(y*N+x)*4; dst[i]=a*w; dst[i+1]=b2*w; dst[i+2]=c*w; dst[i+3]=1;
      }
    }
    break; }

  case OP_DISTORT:{ /* src, amount */
    const float *s=R->r[(int)A(2)];
    float amt=A(3);
    memcpy(R->cp,B,sizeof(float)*RSZ);
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      int i=(y*N+x)*4;
      int ox=(int)lrintf((s[i]-0.5f)*amt*N), oy=(int)lrintf((s[i+1]-0.5f)*amt*N);
      int sx=(((x+ox)%N)+N)%N, sy=(((y+oy)%N)+N)%N, j=(sy*N+sx)*4;
      B[i]=R->cp[j]; B[i+1]=R->cp[j+1]; B[i+2]=R->cp[j+2];
    }
    break; }

  case OP_ADD: case OP_SUB: case OP_MUL:{
    const float *s=R->r[(int)A(2)];
    float k = HAS(3)? A(3) : 1.f;
    for(int i=0;i<RSZ;i+=4) for(int c=0;c<3;c++){
      float v=s[i+c]*k;
      B[i+c] = op==OP_ADD? B[i+c]+v : (op==OP_SUB? B[i+c]-v : B[i+c]*v);
    }
    break; }

  case OP_BLEND:{
    const float *a=R->r[(int)A(2)], *b=R->r[(int)A(3)];
    float t=A(4);
    for(int i=0;i<RSZ;i+=4) for(int c=0;c<3;c++) B[i+c]=mixf(a[i+c],b[i+c],t);
    break; }

  case OP_MASK:{
    const float *a=R->r[(int)A(2)], *b=R->r[(int)A(3)], *m=R->r[(int)A(4)];
    for(int i=0;i<RSZ;i+=4){ float t=cl(m[i]);
      for(int c=0;c<3;c++) B[i+c]=mixf(a[i+c],b[i+c],t); }
    break; }

  case OP_LEVELS:{
    float lo=A(2), hi=A(3), gam=A(4);
    float sp=1.f/(hi-lo>1e-5f?hi-lo:1e-5f);
    for(int i=0;i<RSZ;i+=4) for(int c=0;c<3;c++)
      B[i+c]=powf(cl((B[i+c]-lo)*sp),gam);
    break; }

  case OP_HSCB:{
    float hue=A(2), sat=A(3), con=A(4), bri=A(5);
    for(int i=0;i<RSZ;i+=4){
      float r=B[i], g=B[i+1], b=B[i+2];
      if(hue!=0){
        float c=cosf(hue*6.2831853f), s2=sinf(hue*6.2831853f);
        float nr=r*(0.299f+0.701f*c+0.168f*s2)+g*(0.587f-0.587f*c+0.330f*s2)+b*(0.114f-0.114f*c-0.497f*s2);
        float ng=r*(0.299f-0.299f*c-0.328f*s2)+g*(0.587f+0.413f*c+0.035f*s2)+b*(0.114f-0.114f*c+0.292f*s2);
        float nb=r*(0.299f-0.300f*c+1.250f*s2)+g*(0.587f-0.588f*c-1.050f*s2)+b*(0.114f+0.886f*c-0.203f*s2);
        r=nr; g=ng; b=nb;
      }
      float l=r*0.299f+g*0.587f+b*0.114f;
      r=mixf(l,r,sat); g=mixf(l,g,sat); b=mixf(l,b,sat);
      B[i]  =(r-0.5f)*con+0.5f+bri;
      B[i+1]=(g-0.5f)*con+0.5f+bri;
      B[i+2]=(b-0.5f)*con+0.5f+bri;
    }
    break; }

  case OP_COLORIZE:{ /* src, three rgb stops across the source's luminance */
    const float *s=R->r[(int)A(2)];
    float c0[3]={A(3),A(4),A(5)}, c1[3]={A(6),A(7),A(8)}, c2[3]={A(9),A(10),A(11)};
    for(int i=0;i<RSZ;i+=4){
      float t=cl(s[i]);
      if(t<0.5f){ float k=t*2;
        for(int c=0;c<3;c++) B[i+c]=mixf(c0[c],c1[c],k); }
      else      { float k=(t-0.5f)*2;
        for(int c=0;c<3;c++) B[i+c]=mixf(c1[c],c2[c],k); }
      B[i+3]=1;
    }
    break; }

  case OP_CAVITY:{ /* hsrc, radius, gain, strength
     View-independent recess darkening.  No light direction, no highlight:
     a highlight belongs to the renderer, grime in the pits belongs here. */
    const float *Hb=R->r[(int)A(2)];
    int rad=(int)A(3); float gain=A(4), st=A(5);
    float w=1.f/((rad*2+1)*(rad*2+1));
    memcpy(R->cp,B,sizeof(float)*RSZ);
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      float acc=0;
      for(int dy=-rad;dy<=rad;dy++) for(int dx=-rad;dx<=rad;dx++){
        int a=x+dx, b=y+dy;
        acc+=Hb[((((b%N)+N)%N)*N+(((a%N)+N)%N))*4];
      }
      float here=Hb[(y*N+x)*4];
      float k=1-cl((acc*w-here)*gain)*st;
      int i=(y*N+x)*4;
      B[i]*=k; B[i+1]*=k; B[i+2]*=k;
    }
    break; }

  case OP_NORMALS:{
    const float *Hb=R->r[(int)A(2)];
    float st=A(3);
    #define AT(a,b) Hb[((((b)%N)+N)%N)*N*4+((((a)%N)+N)%N)*4]
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      float gx=-(AT(x+1,y)-AT(x-1,y))*st, gy=-(AT(x,y+1)-AT(x,y-1))*st;
      float il=1.f/hypot3(gx,gy,1); int i=(y*N+x)*4;
      B[i]=gx*il*0.5f+0.5f; B[i+1]=gy*il*0.5f+0.5f; B[i+2]=il*0.5f+0.5f; B[i+3]=1;
    }
    #undef AT
    break; }

  case OP_COPY: memcpy(B,R->r[(int)A(2)],sizeof(float)*RSZ); break;

  /* ---- SOFT BOX: a rectangle with a feathered rim --------------------
     args  cx cy | fx fy | hx hy | r g b | opacity=1 | curve=0.5
     Inside the half-size (hx,hy) is solid colour; beyond it the colour
     fades out over (fx,fy), measured as an ellipse so the corners round
     off instead of meeting in a cross.  The fade is 1-q^(0.5/curve), q the
     squared reach into the rim: curve 0.5 is a straight ramp, smaller is a
     crisp edge, larger a glow.  The axes are separable, so the reach along
     each is worked out once per column and once per row, not per texel.
     A plate is a big box, a rivet a small one, a seam a thin one. */
  case OP_SOFTBOX:{
    float c[2]={A(2),A(3)}, f[2], h[2]={A(6),A(7)};
    f[0]=A(4)>1e-5f?A(4):1e-5f; f[1]=A(5)>1e-5f?A(5):1e-5f;
    float col[3]={A(8),A(9),A(10)};
    float op    = HAS(11)? A(11) : 1.f;
    float curve = HAS(12)? A(12) : 0.5f;
    float k=0.5f/(curve>1e-5f?curve:1e-5f);
    float *q=R->tmp, *qx=q, *qy=q+N;          /* squared reach, per column / row */
    for(int axis=0;axis<2;axis++){
      float *out=axis? qy : qx;
      for(int t=0;t<N;t++){
        float a=fabsf((t+0.5f)/N-c[axis]);
        if(1-a<a) a=1-a;                      /* the nearer copy on the torus */
        a-=h[axis]; if(a<0) a=0;
        a/=f[axis]; out[t]=a*a;
      }
    }
    for(int y=0;y<N;y++){
      if(qy[y]>=1) continue;
      for(int x=0;x<N;x++){
        float r2=qx[x]+qy[y];
        if(r2>=1) continue;
        float w=(1-powf(r2,k))*op;
        if(w<=0) continue;
        float *t=B+(y*N+x)*4;
        for(int ch=0;ch<3;ch++) t[ch]=mixf(t[ch],col[ch],w);
      }
    }
    break; }

  case OP_WARP:
    warp(R,d,A(2), HAS(3)?A(3):1, HAS(4)?A(4):1,
         HAS(5)?A(5):0.5f, HAS(6)?A(6):0.5f);
    break;

  /* ---- REPEAT: 2^n copies by doubling ---------------------------------
     args  turn | sx sy | tx ty | how=add | n=1 | fade=1
     Laying the register over itself shifted by one step gives two copies;
     doing it again shifted by two steps gives four, then eight.  n passes,
     2^n copies, evenly spaced, for n warps rather than 2^n.  (tx,ty) is
     where the LAST copy's centre lands, so one step is that offset over
     2^n-1, and pass p moves by 2^p steps; the scale compounds the same
     way.  Each pass may dim the copy it lays (fade, squared every pass),
     and `how` picks the blend: 0 add, 1 subtract, 2 multiply, 3 lighter. */
  case OP_REPEAT:{
    int how = HAS(7)? (int)A(7) : 0;
    int passes = HAS(8)? (int)A(8) : 0; if(passes<1) passes=1;
    float copies=(float)(1<<passes), steps=copies-1;
    float turn  = A(2)/copies;
    float stepx = (HAS(5)? A(5)-0.5f : 0.f)/steps;
    float stepy = (HAS(6)? A(6)-0.5f : 0.f)/steps;
    float gx = powf(HAS(3)&&A(3)!=0? A(3) : 1.f, 1.f/steps);
    float gy = powf(HAS(4)&&A(4)!=0? A(4) : 1.f, 1.f/steps);
    float dim = HAS(9)? A(9) : 1.f;
    float *keep=R->tmp;
    for(int p=0, span=1; p<passes; p++, span*=2){
      memcpy(keep,B,sizeof(float)*RSZ);
      if(dim!=1) for(int i=0;i<RSZ;i+=4){ B[i]*=dim; B[i+1]*=dim; B[i+2]*=dim; }
      warp(R,d,turn,gx,gy,0.5f+stepx*span,0.5f+stepy*span);
      for(int i=0;i<RSZ;i+=4) for(int ch=0;ch<3;ch++){
        float a=keep[i+ch], b=B[i+ch];
        B[i+ch] = how==1? a-b : how==2? a*b : how==3? (a>b?a:b) : a+b;
      }
      turn*=2; gx*=gx; gy*=gy; dim*=dim;
    }
    break; }

  case OP_MULC:{
    float r=A(2),g=A(3),b=A(4);
    for(int i=0;i<RSZ;i+=4){ B[i]*=r; B[i+1]*=g; B[i+2]*=b; }
    break; }

  case OP_MIXC:{
    const float *m=R->r[(int)A(2)];
    float r=A(3),g=A(4),b=A(5), amt=A(6);
    for(int i=0;i<RSZ;i+=4){ float t=cl(m[i])*amt;
      B[i]=mixf(B[i],r,t); B[i+1]=mixf(B[i+1],g,t); B[i+2]=mixf(B[i+2],b,t); }
    break; }

  case OP_SHARP:{
    float amt=A(2);
    memcpy(R->cp,B,sizeof(float)*RSZ);
    const float *cp=R->cp;
    for(int y=0;y<N;y++) for(int x=0;x<N;x++){
      int i=(y*N+x)*4;
      for(int c=0;c<3;c++){
        float l =cp[(y*N+((x-1+N)%N))*4+c], r=cp[(y*N+((x+1)%N))*4+c];
        float u =cp[(((y-1+N)%N)*N+x)*4+c], dn=cp[(((y+1)%N)*N+x)*4+c];
        B[i+c]=cp[i+c]+(cp[i+c]*4-l-r-u-dn)*amt;
      }
    }
    break; }
  }
}
#undef A
#undef HAS

/* ---- one material --------------------------------------------------- */
static void compileLayer(Regs *R,int layer){
  const int N=TN;
  size_t off=(size_t)layer*N*N*4;
  for(int i=0;i<NREG;i++) memset(R->r[i],0,sizeof(float)*RSZ);
  for(int w=RECIPE_OFF[layer]; w<RECIPE_OFF[layer+1]; ){
    int n=(int)RECIPE_OPS[w];
    runOp(R,RECIPE_OPS+w,n);
    w+=n;
  }
  const float *A=R->r[0], *H=R->r[1], *Rg=R->r[2], *E=R->r[3], *MT=R->r[8];
  double ar=0,ag=0,ab=0,er=0,eg=0,eb=0;
  for(int i=0;i<N*N;i++){
    size_t o=off+(size_t)i*4; int j=i*4;
    float cr=cl(A[j]), cg=cl(A[j+1]), cb=cl(A[j+2]), ce=cl(E[j]);
    ALB_DATA[o  ]=(unsigned char)(cr*255.f);
    ALB_DATA[o+1]=(unsigned char)(cg*255.f);
    ALB_DATA[o+2]=(unsigned char)(cb*255.f);
    ALB_DATA[o+3]=(unsigned char)(cl(Rg[j])*255.f);
    NRM_DATA[o+3]=(unsigned char)(ce*255.f);
    ar+=cr; ag+=cg; ab+=cb; er+=cr*ce; eg+=cg*ce; eb+=cb*ce;
  }
  double iv=1.0/((double)N*N);
  MAT_ALBEDO[layer][0]=(float)(ar*iv);
  MAT_ALBEDO[layer][1]=(float)(ag*iv);
  MAT_ALBEDO[layer][2]=(float)(ab*iv);
  MAT_EMIS[layer][0]=(float)(er*iv*EMIS_GAIN);
  MAT_EMIS[layer][1]=(float)(eg*iv*EMIS_GAIN);
  MAT_EMIS[layer][2]=(float)(eb*iv*EMIS_GAIN);

  /* the height field becomes a tangent-space normal by Sobel, and blue --
     which a unit vector does not need, since z falls out of x and y -- is
     handed to the metal mask instead */
  float st=MAT_BUMP[layer];
  #define AT(a,b) H[((((b)%N)+N)%N)*N*4+((((a)%N)+N)%N)*4]
  for(int y=0;y<N;y++) for(int x=0;x<N;x++){
    float gx=-(AT(x+1,y)-AT(x-1,y))*st, gy=-(AT(x,y+1)-AT(x,y-1))*st;
    float il=1.f/hypot3(gx,gy,1);
    size_t o=off+(size_t)(y*N+x)*4;
    NRM_DATA[o  ]=(unsigned char)((gx*il*0.5f+0.5f)*255.f);
    NRM_DATA[o+1]=(unsigned char)((gy*il*0.5f+0.5f)*255.f);
    NRM_DATA[o+2]=(unsigned char)(cl(MT[(y*N+x)*4])*255.f);
  }
  #undef AT
}

/* ---- the parallel pass ---------------------------------------------- */
static Regs *POOL; static int nPool;
static void texChunk(int i,void *ctx){
  (void)ctx;
  compileLayer(&POOL[i],i);
}

void texCompileLayer(int layer){ compileLayer(&POOL[layer],layer); }

void texCompileAll(void){
  double t0=platTime();
  ALB_DATA=(unsigned char*)malloc((size_t)TN*TN*4*TL);
  NRM_DATA=(unsigned char*)malloc((size_t)TN*TN*4*TL);
  for(int i=0;i<TL;i++){
    MAT_METAL[i]=MAT_METAL_T[i];
    MAT_SURF[i] =MAT_SURF_T[i];
    MAT_BUMP[i] =MAT_BUMP_T[i];
  }
  /* a register set per LAYER, not per core: the layer index is the job
     index, so nothing has to be handed out and nothing is shared */
  nPool=TL;
  POOL=(Regs*)calloc(nPool,sizeof(Regs));
  for(int i=0;i<nPool;i++){
    for(int r=0;r<NREG;r++) POOL[i].r[r]=(float*)malloc(sizeof(float)*RSZ);
    POOL[i].tmp=(float*)malloc(sizeof(float)*RSZ);
    POOL[i].cp =(float*)malloc(sizeof(float)*RSZ);
  }
  platParallel(texChunk,TL,0);
  for(int i=0;i<nPool;i++){
    for(int r=0;r<NREG;r++) free(POOL[i].r[r]);
    free(POOL[i].tmp); free(POOL[i].cp);
  }
  free(POOL); POOL=0;
  if(platArg("-mats"))
    for(int i=0;i<TL;i++)
      platLog("mat%-3d %-20s surf %d  emis %.4f,%.4f,%.4f  alb %.4f,%.4f,%.4f",
              i,PLAN_MAT_NAME[i],MAT_SURF[i],
              MAT_EMIS[i][0],MAT_EMIS[i][1],MAT_EMIS[i][2],
              MAT_ALBEDO[i][0],MAT_ALBEDO[i][1],MAT_ALBEDO[i][2]);
  platLog("tex    %d materials  %d operators  %d KB  %.0f ms",
          TL, RECIPE_TOTAL_OPS, (int)((size_t)TN*TN*4*TL*2/1024),
          (platTime()-t0)*1000.0);
}
