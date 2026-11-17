/* propkit.c -- see propkit.h for why this is a port of the BUILDERS
   and not of their output.  Every function below is the same
   arithmetic as its counterpart in propkit.js, in the same order,
   with the same constants, so the vertices come out bit-comparable
   to within float32 rounding.  Where the JS relied on a language
   feature C does not have (object literals, closures over a build
   context, string-keyed pooling) the mechanism is different and the
   RESULT is not; those are the only places worth reading closely. */
#include "propkit.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

PkBuild pk;

const float PK_AX[3]={1,0,0}, PK_AY[3]={0,1,0}, PK_AZ[3]={0,0,1};
const float PK_NX[3]={-1,0,0}, PK_NY[3]={0,-1,0}, PK_NZ[3]={0,0,-1};

/* ---- vectors ---------------------------------------------------- */
static void vcp(float*o,const float*a){o[0]=a[0];o[1]=a[1];o[2]=a[2];}
static void vadd(float*o,const float*a,const float*b){o[0]=a[0]+b[0];o[1]=a[1]+b[1];o[2]=a[2]+b[2];}
static void vsub(float*o,const float*a,const float*b){o[0]=a[0]-b[0];o[1]=a[1]-b[1];o[2]=a[2]-b[2];}
static void vscl(float*o,const float*a,float s){o[0]=a[0]*s;o[1]=a[1]*s;o[2]=a[2]*s;}
static void vma (float*o,const float*a,const float*b,float s){o[0]=a[0]+b[0]*s;o[1]=a[1]+b[1]*s;o[2]=a[2]+b[2]*s;}
static float vdot(const float*a,const float*b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static float vlen(const float*a){return sqrtf(vdot(a,a));}
static void vcrs(float*o,const float*a,const float*b){
  float x=a[1]*b[2]-a[2]*b[1], y=a[2]*b[0]-a[0]*b[2], z=a[0]*b[1]-a[1]*b[0];
  o[0]=x;o[1]=y;o[2]=z;}
static void vnrm(float*o,const float*a){float l=vlen(a); if(l<1e-9f)l=1; vscl(o,a,1.0f/l);}
static void quadN(float*o,const float*p0,const float*p1,const float*p3){
  float u[3],v[3],c[3]; vsub(u,p1,p0); vsub(v,p3,p0); vcrs(c,u,v); vnrm(o,c);}

/* ---- build context ---------------------------------------------- */
PkOpts pkOpts(void){
  PkOpts o; memset(&o,0,sizeof o);
  o.kind=-1; o.seed=0; o.accent=PK_A_NONE; o.tint=0;
  o.share=1; o.tile=0; o.chamfer=-1; o.nv=0; o.skip=0;
  return o;
}
void pkBegin(void){ pk.nFace=0; pk.nReg=0; pk.nSock=0; pk.mitre=-1; }

void pkSocket(const char*name,const float p[3],const float d[3]){
  if(pk.nSock>=PK_MAX_SOCKETS) return;
  PkSocket*s=&pk.sock[pk.nSock++];
  int i=0; for(;name[i]&&i<15;i++) s->name[i]=name[i];
  s->name[i]=0;
  vcp(s->p,p);
  if(d) vnrm(s->d,d); else { s->d[0]=0;s->d[1]=0;s->d[2]=1; }
}

/* The JS pooled regions on a STRING key.  A 32-bit hash of the same
   fields does the same job here: a collision would merge two regions
   that look alike, which is what pooling is for anyway, and at a few
   hundred regions per prop the odds are nil. */
static int hashf(int h,float v){ union{float f;unsigned u;}c; c.f=v; return (int)((unsigned)h*16777619u ^ c.u); }
static int hashi(int h,int v){ return (int)((unsigned)h*16777619u ^ (unsigned)v); }

static int optKey(int kind,float w,float h,const PkOpts*o,int sized){
  int k=(int)2166136261u;
  k=hashi(k,kind);
  if(sized){ k=hashi(k,(int)floorf(w*500+0.5f)); k=hashi(k,(int)floorf(h*500+0.5f)); }
  k=hashf(k,o->seed?o->seed:1);
  k=hashi(k,o->accent);
  if(sized){ k=hashi(k,o->hasParams?1:0);
             if(o->hasParams) for(int i=0;i<4;i++) k=hashf(k,o->params[i]); }
  if(o->tint) for(int i=0;i<3;i++) k=hashf(k,o->tint[i]);
  k=hashi(k,o->paint?1:0);
  if(sized) k=hashi(k,o->rim?1:0);
  return k;
}

static int addRegion(float w,float h,int kind,const PkOpts*o){
  if(pk.nReg>=PK_MAX_REGIONS) return pk.nReg-1;
  int ri=pk.nReg++;
  PkRegion*R=&pk.reg[ri];
  R->w = w<0.01f?0.01f:w;
  R->h = h<0.01f?0.01f:h;
  R->kind=(unsigned char)kind;
  R->seed = o->seed?o->seed:1.0f;
  for(int i=0;i<4;i++) R->params[i]=o->params[i];
  R->accent=(unsigned char)o->accent;
  if(o->tint){ R->tint[0]=o->tint[0];R->tint[1]=o->tint[1];R->tint[2]=o->tint[2]; }
  else       { R->tint[0]=-1;R->tint[1]=-1;R->tint[2]=-1; }
  R->paint=(unsigned char)(o->paint?1:0);
  R->rim  =(unsigned char)(o->rim?1:0);
  pk.shareKey[ri]=0; pk.tileKey[ri]=0;
  return ri;
}
static int sharedRegion(float w,float h,int kind,const PkOpts*o){
  int key=optKey(kind,w,h,o,1);
  for(int i=0;i<pk.nReg;i++) if(pk.shareKey[i]==key) return i;
  int ri=addRegion(w,h,kind,o);
  pk.shareKey[ri]=key;
  return ri;
}
static int tileRegion(int kind,const PkOpts*o){
  int key=optKey(kind,0,0,o,0);
  for(int i=0;i<pk.nReg;i++) if(pk.tileKey[i]==key) return i;
  int ri=addRegion(0.06f,0.06f,kind,o);
  pk.tileKey[ri]=key;
  return ri;
}
static int mitreRegion(void){
  if(pk.mitre<0){ PkOpts o=pkOpts(); o.seed=5; pk.mitre=addRegion(0.05f,0.05f,PK_PLAIN,&o); }
  return pk.mitre;
}

static void pushFace(const float*p0,const float*p1,const float*p2,const float*p3,
                     const float*n,const float (*nv)[3],int ri){
  if(pk.nFace>=PK_MAX_FACES) return;
  PkFace*F=&pk.face[pk.nFace++];
  vcp(F->p[0],p0); vcp(F->p[1],p1); vcp(F->p[2],p2); vcp(F->p[3],p3);
  vcp(F->n,n);
  F->hasNv = nv?1:0;
  if(nv) for(int i=0;i<4;i++) vcp(F->nv[i],nv[i]);
  F->ri=ri;
}

static void addFace(const float*p0,const float*p1,const float*p2,const float*p3,
                    const float*n,float w,float h,int kind,const PkOpts*o){
  int ri=addRegion(w,h,kind,o);
  pushFace(p0,p1,p2,p3,n,0,ri);
}

void pkFace4(const float p0[3],const float p1[3],const float p2[3],
             const float p3[3],const PkOpts*o){
  PkOpts d; if(!o){ d=pkOpts(); o=&d; }
  if(o->skip) return;
  float n[3]; quadN(n,p0,p1,p3);
  float a[3],b[3],c[3],e[3];
  vsub(a,p1,p0); vsub(b,p2,p3); vsub(c,p3,p0); vsub(e,p2,p1);
  float w=(vlen(a)+vlen(b))*0.5f, h=(vlen(c)+vlen(e))*0.5f;
  int kind = o->kind<0 ? PK_PLAIN : o->kind;
  int ri = o->tile ? tileRegion(kind,o)
         : (!o->share ? addRegion(w,h,kind,o) : sharedRegion(w,h,kind,o));
  pushFace(p0,p1,p2,p3,n,o->nv,ri);
}

static void addMitreFace(const float*p0,const float*p1,const float*p2,
                         const float*p3,const float*N){
  pushFace(p0,p1,p2,p3,N,0,mitreRegion());
}

/* ---- rings ------------------------------------------------------- */
void pkRectRing(float cx,float cz,float hw,float hd,float y,float out[4][3]){
  out[0][0]=cx+hw; out[0][1]=y; out[0][2]=cz+hd;
  out[1][0]=cx+hw; out[1][1]=y; out[1][2]=cz-hd;
  out[2][0]=cx-hw; out[2][1]=y; out[2][2]=cz-hd;
  out[3][0]=cx-hw; out[3][1]=y; out[3][2]=cz+hd;
}
void pkOctFoot(float cx,float cz,float hw,float hd,float c,float y,float out[8][3]){
  if(c>hw*0.9f) c=hw*0.9f;
  if(c>hd*0.9f) c=hd*0.9f;
  const float X[8]={cx+hw,cx+hw,cx+hw-c,cx-hw+c,cx-hw,cx-hw,cx-hw+c,cx+hw-c};
  const float Z[8]={cz+hd-c,cz-hd+c,cz-hd,cz-hd,cz-hd+c,cz+hd-c,cz+hd,cz+hd};
  for(int i=0;i<8;i++){ out[i][0]=X[i]; out[i][1]=y; out[i][2]=Z[i]; }
}
int pkRingAt(const float c[3],const float U[3],const float V[3],
             float r,int n,float phase,float out[][3]){
  for(int k=0;k<n;k++){
    float t=phase+k*2.0f*(float)M_PI/n;
    float a[3],b[3];
    vscl(a,U,cosf(t)*r); vscl(b,V,sinf(t)*r);
    vadd(out[k],a,b); vadd(out[k],out[k],c);
  }
  return n;
}
void pkRevRing(const float in[][3],int n,float out[][3]){
  for(int i=0;i<n;i++) vcp(out[i],in[n-1-i]);
}
void pkInsetRing(const float R[4][3],float c,float out[4][3]){
  for(int i=0;i<4;i++){
    const float*P=R[i],*Q=R[(i+1)%4],*S=R[(i+3)%4];
    float dq[3],ds[3]; vsub(dq,Q,P); vsub(ds,S,P);
    float lq=vlen(dq),ls=vlen(ds);
    if(lq==0) lq=1;
    if(ls==0) ls=1;
    float a=(c<lq*0.45f?c:lq*0.45f)/lq, b=(c<ls*0.45f?c:ls*0.45f)/ls;
    float t[3],u[3];
    vscl(t,dq,a); vscl(u,ds,b);
    vadd(out[i],t,u); vadd(out[i],out[i],P);
  }
}
int pkCutRing(const float R[4][3],float c,float out[8][3]){
  int m=0;
  for(int i=0;i<4;i++){
    const float*P=R[i],*Q=R[(i+1)%4];
    float d[3]; vsub(d,Q,P);
    float L=vlen(d); if(L==0)L=1;
    float cc=c<L*0.45f?c:L*0.45f;
    float u[3]; vscl(u,d,1.0f/L);
    vma(out[m++],P,u,cc);
    vma(out[m++],Q,u,-cc);
  }
  return m;
}

/* roundRing -- corner fillets as quadratic Beziers whose end tangents
   ARE the two edge directions, so the curve meets each flat face C1
   and a highlight rolls off the flat onto the round.  The plane comes
   from the quad, not a hardcoded up axis: a ring standing vertical
   (rrFrame on a wall) collapses the cross product otherwise. */
int pkRoundRing(const float R[4][3],float r,int seg,
                float outP[][3],float outN[][3],int card[4]){
  float dir[4][3],cut[4],eN[4][3],ctr[3]={0,0,0},pn[3],e1[3],e2[3];
  for(int i=0;i<4;i++){
    float d[3]; vsub(d,R[(i+1)%4],R[i]);
    float L=vlen(d); if(L==0)L=1;
    vscl(dir[i],d,1.0f/L);
    cut[i]=r<L*0.45f?r:L*0.45f;
    vadd(ctr,ctr,R[i]);
  }
  vscl(ctr,ctr,0.25f);
  vsub(e1,R[1],R[0]); vsub(e2,R[3],R[0]); vcrs(pn,e1,e2);
  if(vlen(pn)<1e-9f){ pn[0]=0;pn[1]=1;pn[2]=0; }
  vnrm(pn,pn);
  for(int i=0;i<4;i++){
    float t[3]; vcrs(t,dir[i],pn); vnrm(eN[i],t);
    float mid[3],a[3],b[3];
    vadd(mid,R[i],R[(i+1)%4]); vscl(mid,mid,0.5f);
    vma(a,mid,eN[i],0.01f);
    vsub(a,a,ctr); vsub(b,mid,ctr);
    if(vlen(a)<vlen(b)) vscl(eN[i],eN[i],-1.0f);
  }
  int m=0;
  for(int i=0;i<4;i++){
    int prev=(i+3)%4;
    const float*V=R[i];
    float A[3],B[3];
    vma(A,V,dir[prev],-cut[prev]);
    vma(B,V,dir[i], cut[i]);
    for(int s=0;s<=seg;s++){
      float t=seg==0?0:(float)s/seg, u=1-t;
      float x[3],y[3],z[3];
      vscl(x,A,u*u); vscl(y,V,2*u*t); vscl(z,B,t*t);
      vadd(outP[m],x,y); vadd(outP[m],outP[m],z);
      vscl(x,eN[prev],u); vscl(y,eN[i],t);
      vadd(z,x,y); vnrm(outN[m],z);
      m++;
    }
    card[i]=m-1;
  }
  return m;
}

/* ---- hullRing ---------------------------------------------------- */
static void ringFaceNormals(const float A[][3],const float B[][3],int n,float out[][3]){
  for(int k=0;k<n;k++){
    int k1=(k+1)%n;
    quadN(out[k],A[k],A[k1],B[k]);
  }
}
void pkHullRingF(const float A[][3],const float B[][3],int n,
                 const PkOpts*(*optFn)(int,void*),void*ud,
                 int smooth,PkNvFn nvFn,void*nvUd){
  static float fns[PK_MAX_RING][3];
  if(smooth!=PK_FLAT) ringFaceNormals(A,B,n,fns);
  for(int k=0;k<n;k++){
    const PkOpts*o=optFn(k,ud);
    if(!o||o->skip) continue;
    int k1=(k+1)%n, kp=(k+n-1)%n;
    float nv[4][3]; int used=0;
    if(smooth==PK_SMOOTH){
      float a[3],b[3];
      vadd(a,fns[kp],fns[k]); vnrm(a,a);
      vadd(b,fns[k],fns[k1]); vnrm(b,b);
      vcp(nv[0],a); vcp(nv[1],b); vcp(nv[2],b); vcp(nv[3],a);
      used=1;
    }else if(smooth==PK_BEVELMODE){
      if(k&1){ vcp(nv[0],fns[kp]); vcp(nv[1],fns[k1]); vcp(nv[2],fns[k1]); vcp(nv[3],fns[kp]); used=1; }
    }else if(smooth==PK_NVFN&&nvFn){
      nvFn(k,(const float(*)[3])fns,n,nv,&used,nvUd);
    }
    PkOpts t=*o;
    if(used) t.nv=(const float(*)[3])nv; else t.nv=0;
    pkFace4(A[k],A[k1],B[k1],B[k],&t);
  }
}
static const PkOpts*constOpt(int k,void*ud){ (void)k; return (const PkOpts*)ud; }
typedef struct { const PkOpts*even; const PkOpts*odd; } AltUD;
static const PkOpts*altOpt(int k,void*ud){
  AltUD*A=(AltUD*)ud; return (k&1)?A->odd:A->even; }
void pkHullRing(const float A[][3],const float B[][3],int n,const PkOpts*o,int smooth){
  pkHullRingF(A,B,n,constOpt,(void*)o,smooth,0,0);
}

void pkCapPoly(const float ring[][3],int n,const PkOpts*o){
  if(n==4){ pkFace4(ring[0],ring[1],ring[2],ring[3],o); return; }
  float C[3]={0,0,0};
  for(int i=0;i<n;i++) vadd(C,C,ring[i]);
  vscl(C,C,1.0f/n);
  for(int k=0;k<n;k+=2)
    pkFace4(ring[k],ring[(k+1)%n],ring[(k+2)%n],C,o);
}

void pkCapRound(const float R[4][3],float r,int seg,const PkOpts*cap,const PkOpts*trim){
  float I[4][3]; pkInsetRing(R,r,I);
  pkFace4(I[0],I[1],I[2],I[3],cap);
  float O[PK_MAX_RING][3],ON[PK_MAX_RING][3]; int card[4];
  int n=pkRoundRing(R,r,seg,O,ON,card);
  PkOpts def=pkOpts(); def.kind=PK_PLAIN; def.seed=8; def.tile=1;
  const PkOpts*to=trim?trim:&def;
  int per=seg+1;
  for(int i=0;i<4;i++){
    int base=i*per;
    for(int s=0;s<seg;s++)
      pkFace4(O[base+s],O[base+s+1],I[i],I[i],to);
    pkFace4(O[base+seg],O[((i+1)*per)%n],I[(i+1)%4],I[i],to);
  }
}

/* ---- panel / box -------------------------------------------------- */
void pkPanel(const float O[3],const float U[3],const float V[3],
             const float N[3],float w,float h,const PkOpts*o){
  PkOpts d; if(!o){ d=pkOpts(); o=&d; }
  if(o->skip) return;
  float c = o->chamfer<0 ? 0.014f : o->chamfer;
  PkOpts ro=*o; ro.rim=1;
  int kind = o->kind<0 ? PK_PANEL : o->kind;
  float p0[3],p1[3],p2[3],p3[3];
  vcp(p0,O); vma(p1,O,U,w); vma(p2,p1,V,h); vma(p3,O,V,h);
  if(c<=0.0001f){ addFace(p0,p1,p2,p3,N,w,h,kind,&ro); return; }
  float cw=c<w*0.32f?c:w*0.32f, ch=c<h*0.32f?c:h*0.32f;
  float bk=(cw+ch)*0.5f;
  float q0[3],q1[3],q2[3],q3[3],t[3];
  vma(t,O,U,cw);      vma(t,t,V,ch);      vma(q0,t,N,-bk);
  vma(t,O,U,w-cw);    vma(t,t,V,ch);      vma(q1,t,N,-bk);
  vma(t,O,U,w-cw);    vma(t,t,V,h-ch);    vma(q2,t,N,-bk);
  vma(t,O,U,cw);      vma(t,t,V,h-ch);    vma(q3,t,N,-bk);
  addFace(q0,q1,q2,q3,N,w-2*cw,h-2*ch,kind,&ro);
  addMitreFace(p0,p1,q1,q0,N);
  addMitreFace(p1,p2,q2,q1,N);
  addMitreFace(p2,p3,q3,q2,N);
  addMitreFace(p3,p0,q0,q3,N);
}

void pkBoxSides(float cx,float cy,float cz,float w,float h,float d,
                const PkOpts*px,const PkOpts*nx,const PkOpts*py,
                const PkOpts*ny,const PkOpts*pz,const PkOpts*nz){
  float hw=w/2,hh=h/2,hd=d/2,O[3];
  if(px){ O[0]=cx+hw;O[1]=cy-hh;O[2]=cz+hd; pkPanel(O,PK_NZ,PK_AY,PK_AX,d,h,px); }
  if(nx){ O[0]=cx-hw;O[1]=cy-hh;O[2]=cz-hd; pkPanel(O,PK_AZ,PK_AY,PK_NX,d,h,nx); }
  if(py){ O[0]=cx-hw;O[1]=cy+hh;O[2]=cz+hd; pkPanel(O,PK_AX,PK_NZ,PK_AY,w,d,py); }
  if(ny){ O[0]=cx-hw;O[1]=cy-hh;O[2]=cz-hd; pkPanel(O,PK_AX,PK_AZ,PK_NY,w,d,ny); }
  if(pz){ O[0]=cx-hw;O[1]=cy-hh;O[2]=cz+hd; pkPanel(O,PK_AX,PK_AY,PK_AZ,w,h,pz); }
  if(nz){ O[0]=cx+hw;O[1]=cy-hh;O[2]=cz-hd; pkPanel(O,PK_NX,PK_AY,PK_NZ,w,h,nz); }
}
void pkPlainBox(float cx,float cy,float cz,float w,float h,float d,
                float seed,const float*tint,int paint){
  PkOpts o=pkOpts();
  o.kind=PK_PLAIN; o.seed=seed?seed:7; o.chamfer=0; o.tint=tint; o.paint=paint;
  pkBoxSides(cx,cy,cz,w,h,d,&o,&o,&o,&o,&o,&o);
}

/* ---- loft --------------------------------------------------------- */
PkLoft pkLoft0(void){
  PkLoft S; memset(&S,0,sizeof S);
  S.cut=-1; S.topRound=-1; S.topCut=0; S.seg=-1; S.topSeg=-1;
  return S;
}

typedef struct { const PkOpts*card[4]; const PkOpts*diag; const int*isCard; int nPts; } SideUD;
static const PkOpts*sideOpt(int k,void*ud){
  SideUD*S=(SideUD*)ud;
  int ci=S->isCard[k];
  if(ci>=0) return S->card[ci];
  return S->diag;
}
typedef struct { const float (*n)[3]; int nPts; } SideNvUD;
static void sideNv(int k,const float(*fn)[3],int n,float out[4][3],int*used,void*ud){
  (void)fn;(void)n;
  SideNvUD*S=(SideNvUD*)ud;
  int k1=(k+1)%S->nPts;
  vcp(out[0],S->n[k]); vcp(out[1],S->n[k1]);
  vcp(out[2],S->n[k1]); vcp(out[3],S->n[k]);
  *used=1;
}
typedef struct { const float (*prev)[3]; const float (*cur)[3]; int m; } BandUD;
static void bandNv(int k,const float(*fn)[3],int n,float out[4][3],int*used,void*ud){
  (void)fn;(void)n;
  BandUD*B=(BandUD*)ud;
  int k1=(k+1)%B->m;
  vcp(out[0],B->prev[k]); vcp(out[1],B->prev[k1]);
  vcp(out[2],B->cur[k1]); vcp(out[3],B->cur[k]);
  *used=1;
}

void pkLoft(const float B[4][3],const float T[4][3],const PkLoft*S0){
  PkLoft d=pkLoft0(); const PkLoft*S=S0?S0:&d;
  int facet=S->facet;
  int seg = facet?1:(S->seg<0?PK_ROUND_SEG:S->seg);
  float r = S->cut<0?PK_BEVEL:(S->cut<0?0:S->cut);
  if(S->cut>=0) r=S->cut;
  float tr = S->topRound<0 ? (S->topCut>0?S->topCut:0) : S->topRound;
  if(tr<0) tr=0;
  int trSeg = facet?1:(S->topSeg<0?PK_ROUND_SEG:S->topSeg);
  if(tr<=0 && S->py && !facet && r>0){ tr = PK_CAP_ROLL<r?PK_CAP_ROLL:r; trSeg=2; }

  float up[3],e1[3],e2[3];
  vsub(e1,T[1],T[0]); vsub(e2,T[3],T[0]); vcrs(up,e1,e2); vnrm(up,up);
  float Ts[4][3];
  if(tr>0 && S->py) for(int i=0;i<4;i++) vma(Ts[i],T[i],up,-tr);
  else              for(int i=0;i<4;i++) vcp(Ts[i],T[i]);

  PkOpts diagDef=pkOpts(); diagDef.kind=PK_PLAIN; diagDef.seed=6; diagDef.tile=1;
  const PkOpts*diag=S->diag?S->diag:&diagDef;

  /* own(): the JS forced share:false on the four cardinal faces so a
     mass's own sides never pool with another mass's. */
  PkOpts ownBuf[4]; const PkOpts*card[4];
  const PkOpts*src[4]={S->px,S->nz,S->nx,S->pz};
  for(int i=0;i<4;i++){
    if(!src[i]){ card[i]=0; continue; }
    ownBuf[i]=*src[i]; ownBuf[i].share=0; card[i]=&ownBuf[i];
  }

  static float A[PK_MAX_RING][3],Bt[PK_MAX_RING][3],AN[PK_MAX_RING][3];
  static int isCard[PK_MAX_RING];
  int nPts, cardIdx[4];
  int haveN=0;
  if(r>0){
    float tn[PK_MAX_RING][3]; int c2[4];
    nPts=pkRoundRing(B,r,seg,A,AN,cardIdx);
    pkRoundRing(Ts,r,seg,Bt,tn,c2);
    haveN=1;
  }else{
    for(int i=0;i<4;i++){ vcp(A[i],B[i]); vcp(Bt[i],Ts[i]); cardIdx[i]=i; }
    nPts=4;
  }
  for(int i=0;i<nPts;i++) isCard[i]=-1;
  for(int i=0;i<4;i++) isCard[cardIdx[i]]=i;

  SideUD su={{card[0],card[1],card[2],card[3]},diag,isCard,nPts};
  SideNvUD sn={(const float(*)[3])AN,nPts};
  pkHullRingF((const float(*)[3])A,(const float(*)[3])Bt,nPts,sideOpt,&su,
              haveN?PK_NVFN:PK_FLAT, haveN?sideNv:0, &sn);

  float cap4[4][3]; float capR=r;
  for(int i=0;i<4;i++) vcp(cap4[i],Ts[i]);

  if(tr>0 && S->py){
    PkOpts topDef=pkOpts(); topDef.kind=PK_PLAIN; topDef.seed=8; topDef.tile=1;
    const PkOpts*topO=S->topOpts?S->topOpts:&topDef;
    const PkOpts*topAlt=S->topOptsAlt;
    static float prev[PK_MAX_RING][3],prevN[PK_MAX_RING][3];
    static float cur [PK_MAX_RING][3],curN [PK_MAX_RING][3];
    int m=nPts;
    for(int i=0;i<nPts;i++){ vcp(prev[i],Bt[i]); if(haveN) vcp(prevN[i],AN[i]); }
    /* the JS gates the crown band's smooth normals on nothing but
       having them (r>0); an earlier draft had a bandSmooth flag and
       porting it flat-shaded every default cap roll in the kit. */
    for(int s=1;s<=trSeg;s++){
      float t=(float)s/trSeg, ang=t*(float)M_PI*0.5f;
      float inset=tr*(1-cosf(ang)), rise=tr*sinf(ang);
      float ringQ[4][3];
      pkInsetRing(T,inset,ringQ);
      for(int i=0;i<4;i++) vma(ringQ[i],ringQ[i],up,-tr+rise);
      float rr=r-inset*0.5f; if(rr<0.002f) rr=0.002f;
      int cn; int c3[4];
      if(r>0) cn=pkRoundRing(ringQ,rr,seg,cur,curN,c3);
      else { for(int i=0;i<4;i++) vcp(cur[i],ringQ[i]); cn=4; }
      if(haveN)
        for(int i=0;i<cn;i++){
          float a[3],b[3];
          vscl(a,curN[i],cosf(ang)); vscl(b,up,sinf(ang));
          vadd(curN[i],a,b); vnrm(curN[i],curN[i]);
        }
      BandUD bu={(const float(*)[3])prevN,(const float(*)[3])curN,m};
      AltUD au={topO,topAlt};
      pkHullRingF((const float(*)[3])prev,(const float(*)[3])cur,m,
                  topAlt?altOpt:constOpt, topAlt?(void*)&au:(void*)topO,
                  haveN?PK_NVFN:PK_FLAT, haveN?bandNv:0,&bu);
      for(int i=0;i<cn;i++){ vcp(prev[i],cur[i]); if(haveN) vcp(prevN[i],curN[i]); }
      m=cn;
    }
    pkInsetRing(T,tr,cap4);
    capR=r-tr*0.5f; if(capR<0.002f) capR=0.002f;
  }
  if(S->py){
    if(capR>0) pkCapRound(cap4,capR,seg,S->py,S->trim);
    else pkFace4(cap4[0],cap4[1],cap4[2],cap4[3],S->py);
  }
  if(S->ny){
    if(r>0){ float rv[4][3]; pkRevRing(B,4,rv); pkCapRound(rv,r,seg,S->ny,S->trim); }
    else pkFace4(B[3],B[2],B[1],B[0],S->ny);
  }
}

void pkOctBox(float cx,float cy,float cz,float w,float h,float d,
              float cut,const PkLoft*S0){
  PkLoft S=S0?*S0:pkLoft0();
  int facet=S.facet || (S.topOptsAlt!=0);   /* a textured crown band is a design chamfer */
  float c = facet?cut:(S.cut>=0?S.cut:PK_BEVEL);
  S.cut=c; S.facet=facet;
  float B[4][3],T[4][3];
  pkRectRing(cx,cz,w/2,d/2,cy-h/2,B);
  pkRectRing(cx,cz,w/2,d/2,cy+h/2,T);
  pkLoft(B,T,&S);
}
void pkHull8(const float B[4][3],const float T[4][3],const PkLoft*S0){
  PkLoft S=S0?*S0:pkLoft0();
  if(S.cut<0) S.cut=PK_BEVEL;
  pkLoft(B,T,&S);
}

/* ---- tube --------------------------------------------------------- */
PkTube pkTube0(void){ PkTube t; memset(&t,0,sizeof t); t.phase=-1; t.capRoll=-1; return t; }

void pkTubeDraw(const float p0[3],const float p1[3],float r,const PkTube*O0){
  PkTube d=pkTube0(); const PkTube*O=O0?O0:&d;
  int n = O->n ? O->n : (r>0.16f?16:(r>0.06f?12:8));
  float dir[3],t[3];
  vsub(t,p1,p0); vnrm(dir,t);
  float U[3],V[3],up[3]={0,1,0},xx[3]={1,0,0};
  vcrs(U,up,dir);
  if(vlen(U)<0.01f) vcrs(U,xx,dir);
  vnrm(U,U); vcrs(V,dir,U);
  float phase = O->phase<0 ? (float)M_PI/n : O->phase;
  static float A[PK_MAX_RING][3],B[PK_MAX_RING][3];
  pkRingAt(p0,U,V,r,n,phase,A);
  pkRingAt(p1,U,V,r,n,phase,B);
  PkOpts fdef=pkOpts(); fdef.kind=PK_PLAIN; fdef.seed=9;
  const PkOpts*fo=O->face?O->face:&fdef;
  pkHullRing((const float(*)[3])A,(const float(*)[3])B,n,fo,PK_SMOOTH);

  PkOpts cdef=pkOpts(); cdef.kind=PK_PLAIN; cdef.seed=10;   /* NOT tiled: only the collar is */
  const PkOpts*co=O->capOpts?O->capOpts:&cdef;
  float roll = O->capRoll<0 ? (r*0.28f<0.010f?r*0.28f:0.010f) : O->capRoll;

  /* rimCap: rings built by pkRingAt always wind one way, so a cap on a
     REVERSED ring has to reverse each roll ring too or the strip
     between them pairs mismatched vertices. */
  for(int side=0;side<2;side++){
    int want = side? O->capB : O->capA;
    if(!want) continue;
    const float*centre = side? p1 : p0;
    float out[3]; if(side) vcp(out,dir); else vscl(out,dir,-1.0f);
    int rev = side?0:1;
    static float ring[PK_MAX_RING][3],prev[PK_MAX_RING][3],prevN[PK_MAX_RING][3];
    static float cur[PK_MAX_RING][3],curN[PK_MAX_RING][3];
    if(rev) pkRevRing((const float(*)[3])A,n,ring);
    else    for(int i=0;i<n;i++) vcp(ring[i],B[i]);
    if(roll<=0.0005f){ pkCapPoly((const float(*)[3])ring,n,co); continue; }
    for(int i=0;i<n;i++){
      vcp(prev[i],ring[i]);
      float q[3]; vsub(q,ring[i],centre); vnrm(prevN[i],q);
    }
    for(int s=1;s<=2;s++){
      float a=((float)s/2)*(float)M_PI*0.5f;
      float rr=r-roll*(1-cosf(a)), yy=roll*sinf(a);
      float c2[3]; vma(c2,centre,out,yy);
      static float tmp[PK_MAX_RING][3];
      pkRingAt(c2,U,V,rr,n,phase,tmp);
      if(rev) pkRevRing((const float(*)[3])tmp,n,cur);
      else    for(int i=0;i<n;i++) vcp(cur[i],tmp[i]);
      for(int i=0;i<n;i++){
        float rad[3],q[3],w1[3],w2[3];
        vsub(q,cur[i],c2); vnrm(rad,q);
        vscl(w1,rad,cosf(a)); vscl(w2,out,sinf(a));
        vadd(q,w1,w2); vnrm(curN[i],q);
      }
      for(int k=0;k<n;k++){
        int k1=(k+1)%n;
        float nv[4][3];
        vcp(nv[0],prevN[k]); vcp(nv[1],prevN[k1]);
        vcp(nv[2],curN[k1]); vcp(nv[3],curN[k]);
        PkOpts t2=*co; t2.nv=(const float(*)[3])nv;
        pkFace4(prev[k],prev[k1],cur[k1],cur[k],&t2);
      }
      for(int i=0;i<n;i++){ vcp(prev[i],cur[i]); vcp(prevN[i],curN[i]); }
    }
    pkCapPoly((const float(*)[3])prev,n,co);
  }

  /* collar: the universal "this was fitted" cue.  Without it a pipe is
     a cylinder stuck into a box. */
  if(O->collarA||O->collarB){
    float cr=r*(O->collarR>0?O->collarR:1.32f);
    float cl=O->collarL>0?O->collarL:(0.02f>r*0.7f?0.02f:r*0.7f);
    PkOpts c2def=pkOpts();
    c2def.kind=PK_PLAIN; c2def.seed=(fo->seed?fo->seed:9)+40; c2def.tile=1;
    const PkOpts*c2=O->collarOpts?O->collarOpts:&c2def;
    static float A2[PK_MAX_RING][3],B2[PK_MAX_RING][3],rv[PK_MAX_RING][3];
    if(O->collarA){
      float e[3]; vma(e,p0,dir,cl);
      pkRingAt(p0,U,V,cr,n,phase,A2);
      pkRingAt(e ,U,V,cr,n,phase,B2);
      pkHullRing((const float(*)[3])A2,(const float(*)[3])B2,n,c2,PK_SMOOTH);
      pkCapPoly((const float(*)[3])B2,n,c2);
    }
    if(O->collarB){
      float s[3]; vma(s,p1,dir,-cl);
      pkRingAt(s ,U,V,cr,n,phase,A2);
      pkRingAt(p1,U,V,cr,n,phase,B2);
      pkHullRing((const float(*)[3])A2,(const float(*)[3])B2,n,c2,PK_SMOOTH);
      pkRevRing((const float(*)[3])A2,n,rv);
      pkCapPoly((const float(*)[3])rv,n,c2);
    }
  }
}

/* ---- stack -------------------------------------------------------- */
PkStack pkStack0(void){ PkStack s; memset(&s,0,sizeof s); s.phase=-1; return s; }

void pkStackDraw(const float base[3],const float axis[3],
                 const PkSect*S,int nS,const PkStack*O0){
  PkStack d=pkStack0(); const PkStack*O=O0?O0:&d;
  float dir[3]; vnrm(dir,axis);
  float U[3],V[3],up[3]={0,1,0},xx[3]={1,0,0};
  vcrs(U,up,dir); if(vlen(U)<0.01f) vcrs(U,xx,dir);
  vnrm(U,U); vcrs(V,dir,U);
  float maxR=0; for(int i=0;i<nS;i++) if(S[i].r>maxR) maxR=S[i].r;
  int n=O->n?O->n:(maxR>0.16f?16:(maxR>0.06f?12:8));
  float ph=O->phase<0?(float)M_PI/n:O->phase;
  PkOpts shDef=pkOpts(); shDef.kind=PK_PLAIN; shDef.seed=8; shDef.tile=1;
  const PkOpts*sho=O->shoulder?O->shoulder:&shDef;

  static float A[PK_MAX_RING][3],B[PK_MAX_RING][3],P[PK_MAX_RING][3];
  float z=0,prevR=-1; int havePrev=0;
  for(int i=0;i<nS;i++){
    float c[3]; vma(c,base,dir,z);
    pkRingAt(c,U,V,S[i].r,n,ph,A);
    if(havePrev && fabsf(prevR-S[i].r)>1e-5f)
      pkHullRing((const float(*)[3])P,(const float(*)[3])A,n,sho,PK_FLAT);
    float c2[3]; vma(c2,base,dir,z+S[i].l);
    pkRingAt(c2,U,V,S[i].r,n,ph,B);
    PkOpts fdef=pkOpts(); fdef.kind=PK_PLAIN; fdef.seed=9;
    const PkOpts*fo=S[i].hasFace?&S[i].face:&fdef;
    pkHullRing((const float(*)[3])A,(const float(*)[3])B,n,fo,PK_SMOOTH);
    for(int k=0;k<n;k++) vcp(P[k],B[k]);
    prevR=S[i].r; havePrev=1; z+=S[i].l;
  }
  PkOpts cdef=pkOpts(); cdef.kind=PK_PLAIN; cdef.seed=10; cdef.tile=1;
  const PkOpts*co=O->capOpts?O->capOpts:&cdef;
  for(int side=0;side<2;side++){
    if(side==0 && O->noCapA) continue;
    if(side==1 && O->noCapB) continue;
    float zc = side? z : 0;
    float r  = side? S[nS-1].r : S[0].r;
    float outS = side? 1.0f : -1.0f;
    int rev = side?0:1;
    float roll=r*0.28f<0.010f?r*0.28f:0.010f;
    static float ring[PK_MAX_RING][3],prev[PK_MAX_RING][3],prevN[PK_MAX_RING][3];
    static float cur[PK_MAX_RING][3],curN[PK_MAX_RING][3],tmp[PK_MAX_RING][3];
    float c[3]; vma(c,base,dir,zc);
    pkRingAt(c,U,V,r,n,ph,tmp);
    if(rev) pkRevRing((const float(*)[3])tmp,n,ring); else for(int i=0;i<n;i++) vcp(ring[i],tmp[i]);
    if(roll<=0.0005f){ pkCapPoly((const float(*)[3])ring,n,co); continue; }
    for(int i=0;i<n;i++){
      vcp(prev[i],ring[i]);
      float q[3]; vsub(q,ring[i],c); vnrm(prevN[i],q);
    }
    for(int s=1;s<=2;s++){
      float a=((float)s/2)*(float)M_PI*0.5f;
      float rr=r-roll*(1-cosf(a));
      float cc[3]; vma(cc,base,dir,zc+outS*roll*sinf(a));
      pkRingAt(cc,U,V,rr,n,ph,tmp);
      if(rev) pkRevRing((const float(*)[3])tmp,n,cur); else for(int i=0;i<n;i++) vcp(cur[i],tmp[i]);
      for(int i=0;i<n;i++){
        float rad[3],q[3],w1[3],w2[3],ov[3];
        vsub(q,cur[i],cc); vnrm(rad,q);
        vscl(ov,dir,outS);
        vscl(w1,rad,cosf(a)); vscl(w2,ov,sinf(a));
        vadd(q,w1,w2); vnrm(curN[i],q);
      }
      for(int k=0;k<n;k++){
        int k1=(k+1)%n; float nv[4][3];
        vcp(nv[0],prevN[k]); vcp(nv[1],prevN[k1]);
        vcp(nv[2],curN[k1]); vcp(nv[3],curN[k]);
        PkOpts t2=*co; t2.nv=(const float(*)[3])nv;
        pkFace4(prev[k],prev[k1],cur[k1],cur[k],&t2);
      }
      for(int i=0;i<n;i++){ vcp(prev[i],cur[i]); vcp(prevN[i],curN[i]); }
    }
    pkCapPoly((const float(*)[3])prev,n,co);
  }
}

/* ---- capsule bar, X-brace, rounded frame, boss -------------------- */
void pkCapBar(const float a[3],const float b[3],float wdt,float t,
              const float N[3],int seg,const PkOpts*o){
  if(seg<=0) seg=4;
  float dir[3],side[3],q[3];
  vsub(q,b,a); vnrm(dir,q);
  vcrs(q,N,dir); vnrm(side,q);
  float hw=wdt/2;
  static float out[PK_MAX_RING][3],nrm[PK_MAX_RING][3],top[PK_MAX_RING][3];
  int n=0;
  for(int s=0;s<=seg;s++){
    float th=-(float)M_PI/2 + s*(float)M_PI/seg;
    float d1[3],d2[3];
    vscl(d1,dir,cosf(th)); vscl(d2,side,sinf(th));
    vadd(nrm[n],d1,d2);
    vma(out[n],b,nrm[n],hw); n++;
  }
  for(int s=0;s<=seg;s++){
    float th=(float)M_PI/2 + s*(float)M_PI/seg;
    float d1[3],d2[3];
    vscl(d1,dir,cosf(th)); vscl(d2,side,sinf(th));
    vadd(nrm[n],d1,d2);
    vma(out[n],a,nrm[n],hw); n++;
  }
  for(int i=0;i<n;i++) vma(top[i],out[i],N,t);
  PkOpts eo=pkOpts();
  eo.kind=PK_PLAIN; eo.seed=o&&o->seed?o->seed:7;
  if(o){ eo.tint=o->tint; eo.paint=o->paint; }
  for(int k=0;k<n;k++){
    int k1=(k+1)%n;
    float nv[4][3];
    vcp(nv[0],nrm[k]); vcp(nv[1],nrm[k1]); vcp(nv[2],nrm[k1]); vcp(nv[3],nrm[k]);
    PkOpts t2=eo; t2.nv=(const float(*)[3])nv;
    pkFace4(out[k],out[k1],top[k1],top[k],&t2);
  }
  float ca[3],cb[3];
  vma(ca,a,N,t); vma(cb,b,N,t);
  for(int k=0;k<n;k++){
    int k1=(k+1)%n;
    const float*c0=(k<seg+1)?cb:ca;
    const float*c1=(k1<=seg)?cb:ca;
    pkFace4(top[k],top[k1],c1,c0,o);
  }
}

void pkXBrace(const float O[3],const float U[3],const float V[3],
              const float N[3],float w,float h,float wdt,float t,const PkOpts*o){
  float m=wdt*0.6f;
  float a[3],b[3],c[3],d[3],tmp[3];
  vma(tmp,O,U,m);      vma(a,tmp,V,m);
  vma(tmp,O,U,w-m);    vma(b,tmp,V,h-m);
  vma(tmp,O,U,w-m);    vma(c,tmp,V,m);
  vma(tmp,O,U,m);      vma(d,tmp,V,h-m);
  pkCapBar(a,b,wdt,t,N,4,o);
  pkCapBar(c,d,wdt,t,N,4,o);
}

void pkRRFrame(const float O[3],const float U[3],const float V[3],
               const float N[3],float w,float h,float r,float wdt,
               float t,const PkOpts*o,int seg){
  if(seg<=0) seg=PK_ROUND_SEG;
  float q[4][3],e1[3],e2[3],cr[3],tmp[3];
  static float oP[PK_MAX_RING][3],oN[PK_MAX_RING][3];
  static float iP[PK_MAX_RING][3],iN[PK_MAX_RING][3];
  static float oT[PK_MAX_RING][3],iT[PK_MAX_RING][3];
  int card[4], n=0, m=0;
  float rin = r-wdt; if(rin<0.004f) rin=0.004f;
  for(int pass=0;pass<2;pass++){
    float x0=pass?wdt:0, y0=pass?wdt:0;
    float x1=pass?w-wdt:w, y1=pass?h-wdt:h;
    vma(tmp,O,U,x0); vma(q[0],tmp,V,y0);
    vma(tmp,O,U,x1); vma(q[1],tmp,V,y0);
    vma(tmp,O,U,x1); vma(q[2],tmp,V,y1);
    vma(tmp,O,U,x0); vma(q[3],tmp,V,y1);
    /* the caller's U,V need not be right-handed about N; if they are
       not, the extruded walls come out inside-out */
    vsub(e1,q[1],q[0]); vsub(e2,q[3],q[0]); vcrs(cr,e1,e2);
    if(vdot(cr,N)<0){
      float s[4][3];
      for(int i=0;i<4;i++) vcp(s[i],q[3-i]);
      for(int i=0;i<4;i++) vcp(q[i],s[i]);
    }
    if(!pass) n=pkRoundRing((const float(*)[3])q,r,seg,oP,oN,card);
    else      m=pkRoundRing((const float(*)[3])q,rin,seg,iP,iN,card);
  }
  for(int i=0;i<n;i++) vma(oT[i],oP[i],N,t);
  for(int i=0;i<m;i++) vma(iT[i],iP[i],N,t);
  PkOpts eo=pkOpts();
  eo.kind=PK_PLAIN; eo.seed=(o&&o->seed)?o->seed:7;
  if(o){ eo.tint=o->tint; eo.paint=o->paint; }
  for(int k=0;k<n;k++){
    int k1=(k+1)%n;
    float nv[4][3];
    vcp(nv[0],oN[k]); vcp(nv[1],oN[k1]); vcp(nv[2],oN[k1]); vcp(nv[3],oN[k]);
    PkOpts t2=eo; t2.nv=(const float(*)[3])nv;
    pkFace4(oP[k],oP[k1],oT[k1],oT[k],&t2);      /* outer wall */
    pkFace4(oT[k],oT[k1],iT[k1],iT[k],o);        /* top face   */
    float mv[4][3];
    vscl(mv[0],iN[k],-1); vscl(mv[1],iN[k1],-1);
    vscl(mv[2],iN[k1],-1); vscl(mv[3],iN[k],-1);
    PkOpts t3=eo; t3.nv=(const float(*)[3])mv;
    pkFace4(iT[k],iT[k1],iP[k1],iP[k],&t3);      /* bore, faces inward */
  }
}

PkBoss pkBoss0(void){ PkBoss b; memset(&b,0,sizeof b); b.fillet=-1; return b; }

void pkBossDraw(const float c[3],const float axis[3],float r,float h,const PkBoss*O0){
  PkBoss d=pkBoss0(); const PkBoss*O=O0?O0:&d;
  int n=O->n?O->n:12;
  float fil=O->fillet<0?r*0.30f:O->fillet;
  int steps=O->filletSeg?O->filletSeg:3;
  float dir[3]; vnrm(dir,axis);
  float U[3],V[3],up[3]={0,1,0},xx[3]={1,0,0};
  vcrs(U,up,dir); if(vlen(U)<0.01f) vcrs(U,xx,dir);
  vnrm(U,U); vcrs(V,dir,U);
  float ph=(float)M_PI/n;
  /* JS: Object.assign({kind:K_PLAIN, seed: opts.seed || 9}, opts.face)
     -- the BOSS's own seed is the default and the face object's fields
     override it. Dropping O->seed here mis-seeded every boss in the kit. */
  PkOpts fdef=pkOpts(); fdef.kind=PK_PLAIN; fdef.seed=O->seed?O->seed:9;
  if(O->face){
    if(O->face->kind>=0) fdef.kind=O->face->kind;
    if(O->face->seed)    fdef.seed=O->face->seed;
    fdef.tint=O->face->tint; fdef.paint=O->face->paint;
    fdef.accent=O->face->accent;
    for(int i=0;i<4;i++) fdef.params[i]=O->face->params[i];
  }
  static float prev[PK_MAX_RING][3],prevN[PK_MAX_RING][3];
  static float ring[PK_MAX_RING][3],nn[PK_MAX_RING][3];
  int have=0;
  for(int s=0;s<=steps;s++){
    float a=((float)s/steps)*(float)M_PI*0.5f;
    float rr=r+fil*(1-sinf(a)), yy=fil*(1-cosf(a));
    float cc[3]; vma(cc,c,dir,yy);
    pkRingAt(cc,U,V,rr,n,ph,ring);
    for(int k=0;k<n;k++){
      float q[3],rad[3],w1[3],w2[3];
      vma(q,ring[k],dir,-yy); vsub(q,q,c); vnrm(rad,q);
      vscl(w1,rad,sinf(a)); vscl(w2,dir,cosf(a));
      vadd(q,w1,w2); vnrm(nn[k],q);
    }
    if(have) for(int k=0;k<n;k++){
      int k1=(k+1)%n; float nv[4][3];
      vcp(nv[0],prevN[k]); vcp(nv[1],prevN[k1]);
      vcp(nv[2],nn[k1]);   vcp(nv[3],nn[k]);
      PkOpts t2=fdef; t2.nv=(const float(*)[3])nv;
      pkFace4(prev[k],prev[k1],ring[k1],ring[k],&t2);
    }
    for(int k=0;k<n;k++){ vcp(prev[k],ring[k]); vcp(prevN[k],nn[k]); }
    have=1;
  }
  float tc[3]; vma(tc,c,dir,fil+h);
  static float top[PK_MAX_RING][3];
  pkRingAt(tc,U,V,r,n,ph,top);
  pkHullRing((const float(*)[3])prev,(const float(*)[3])top,n,&fdef,PK_SMOOTH);
  pkCapPoly((const float(*)[3])top,n,O->cap?O->cap:&fdef);
}

void pkWheelZ(float cx,float cy,float cz,float R,float t,float depth,
              float seed,const float*tint,int paint,float hub,float spoke){
  const int n=12; float ph=(float)M_PI/n;
  float zB=cz-depth/2, zF=cz+depth/2;
  float cB[3]={cx,cy,zB}, cF[3]={cx,cy,zF};
  static float oB[16][3],oF[16][3],iB[16][3],iF[16][3],rb[16][3],rf[16][3];
  pkRingAt(cB,PK_AX,PK_AY,R,n,ph,oB);
  pkRingAt(cF,PK_AX,PK_AY,R,n,ph,oF);
  pkRingAt(cB,PK_AX,PK_AY,R-t,n,ph,iB);
  pkRingAt(cF,PK_AX,PK_AY,R-t,n,ph,iF);
  PkOpts mo=pkOpts(); mo.kind=PK_PLAIN; mo.seed=seed?seed:11; mo.tint=tint; mo.paint=paint;
  pkHullRing((const float(*)[3])oB,(const float(*)[3])oF,n,&mo,PK_SMOOTH);
  pkRevRing((const float(*)[3])iB,n,rb); pkRevRing((const float(*)[3])iF,n,rf);
  pkHullRing((const float(*)[3])rb,(const float(*)[3])rf,n,&mo,PK_SMOOTH);
  float rl=t*0.30f<0.008f?t*0.30f:0.008f;
  for(int side=0;side<2;side++){
    const float (*ring)[3] = side? (const float(*)[3])oB : (const float(*)[3])oF;
    const float (*inner)[3]= side? (const float(*)[3])iB : (const float(*)[3])iF;
    float zc = side? zB : zF;
    int flip = side;
    /* out is 0 in both JS calls, so the mid ring stays in the face
       plane -- only its radius steps in */
    float c1[3]={cx,cy,zc};
    static float mid[16][3];
    pkRingAt(c1,PK_AX,PK_AY,R-rl*0.293f,n,ph,mid);
    for(int k=0;k<n;k++){
      int k1=(k+1)%n;
      if(flip){ pkFace4(ring[k1],ring[k],mid[k],mid[k1],&mo);
                pkFace4(mid[k1],mid[k],inner[k],inner[k1],&mo); }
      else    { pkFace4(ring[k],ring[k1],mid[k1],mid[k],&mo);
                pkFace4(mid[k],mid[k1],inner[k1],inner[k],&mo); }
    }
  }
  float hr=hub>0?hub:R*0.22f;
  float hb[3]={cx,cy,zB-0.012f}, hf[3]={cx,cy,zF+0.012f};
  static float hB[16][3],hF[16][3];
  pkRingAt(hb,PK_AX,PK_AY,hr,n,ph,hB);
  pkRingAt(hf,PK_AX,PK_AY,hr,n,ph,hF);
  pkHullRing((const float(*)[3])hB,(const float(*)[3])hF,n,&mo,PK_SMOOTH);
  PkOpts so=pkOpts(); so.kind=PK_SEAM; so.seed=(seed?seed:11)+1; so.params[0]=1;
  pkCapPoly((const float(*)[3])hF,n,&so);
  float sw=spoke>0?spoke:t*0.9f;
  pkPlainBox(cx,cy,cz,(R-t*0.5f)*2,sw,depth*0.8f,seed,tint,paint);
  pkPlainBox(cx,cy,cz,sw,(R-t*0.5f)*2,depth*0.8f,seed,tint,paint);
}
