/* The entity pass.  See ent.h.  The shader is map.html's ENT_FS, ported
   line for line: `#version 300 es` becomes `#version 330 core`, the
   precision qualifiers go, and the `${}` the JS template interpolated is
   spelled with snprintf.  Nothing else changed. */
#include "gl33.h"
#include "ship.h"
#include "torch.h"
#include "app.h"
#include "prim.h"
#include "props_glsl.h"
#include "ent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const EntFx ENT_PLAIN = {0, 1, 0, 0.5f, {0,0,0}, 0.85f, {0,1,0}, 0.6f, 0,1,0,0};

static float *entData;
/* A SHORT, NOT A BYTE.  props.c hands prim.c one range per material
   group per prop -- 37 props at up to eight groups each -- and with the
   thirteen base solids and LUMI's thirteen that is past 256.  A byte
   here wrapped silently and drew a locker as a bevelled cube. */
static unsigned short *entPrim;
static float *entSorted;
static int nEnt;
static int CUBE_N;

static GLuint pEnt, entVAO, entInst, entVB, entRB;
static struct {
  GLint uProbe,uDFG,uPN,uPLev;
  GLint uProj,uView,uVM,uCamPos,uCamDir,uCube,uDynP,uDynC,uDynR,uDynN,
        uFlash,uFogDensity,uVMFlash,uTime,uFogCol,uMinLight,uSLP,uSLC,uSLN;
} UE;

int entCount(void){ return nEnt; }

/* ---- transforms ----------------------------------------------------- */
void mIdent34(float *o){
  o[0]=1;o[1]=0;o[2]=0;o[3]=0;
  o[4]=0;o[5]=1;o[6]=0;o[7]=0;
  o[8]=0;o[9]=0;o[10]=1;o[11]=0;
}
void mMul34(const float *a,const float *b,float *o){
  for(int r=0;r<3;r++){
    float a0=a[r*4],a1=a[r*4+1],a2=a[r*4+2],a3=a[r*4+3];
    o[r*4  ]=a0*b[0]+a1*b[4]+a2*b[8];
    o[r*4+1]=a0*b[1]+a1*b[5]+a2*b[9];
    o[r*4+2]=a0*b[2]+a1*b[6]+a2*b[10];
    o[r*4+3]=a0*b[3]+a1*b[7]+a2*b[11]+a3;
  }
}
void mTrans34(float x,float y,float z,float *o){ mIdent34(o); o[3]=x;o[7]=y;o[11]=z; }
void mRotY34(float a,float *o){ float c=cosf(a),s=sinf(a); mIdent34(o);
  o[0]=c;o[2]=s;o[8]=-s;o[10]=c; }
void mRotX34(float a,float *o){ float c=cosf(a),s=sinf(a); mIdent34(o);
  o[5]=c;o[6]=-s;o[9]=s;o[10]=c; }
void mRotZ34(float a,float *o){ float c=cosf(a),s=sinf(a); mIdent34(o);
  o[0]=c;o[1]=-s;o[4]=s;o[5]=c; }

/* ---- the instance buffer -------------------------------------------- */
void entReset(void){ nEnt=0; }

void pushEnt(const float *M,float sx,float sy,float sz,
             float r,float g,float b,float emis,const EntFx *fx,int prim){
  if(nEnt>=MAX_ENT) return;
  if(!fx) fx=&ENT_PLAIN;
  entPrim[nEnt]=(unsigned short)(prim<0?PRIM_BEVEL:prim);
  float *d=entData+(size_t)nEnt*ENT_STRIDE;
  /* fold the box's own size into the transform's linear part */
  d[0]=M[0]*sx; d[1]=M[1]*sy; d[2]=M[2]*sz;  d[3]=M[3];
  d[4]=M[4]*sx; d[5]=M[5]*sy; d[6]=M[6]*sz;  d[7]=M[7];
  d[8]=M[8]*sx; d[9]=M[9]*sy; d[10]=M[10]*sz;d[11]=M[11];
  d[12]=r; d[13]=g; d[14]=b; d[15]=emis;
  d[16]=fx->flash; d[17]=fx->alpha; d[18]=fx->zone; d[19]=fx->rim;
  d[20]=fx->lit[0]; d[21]=fx->lit[1]; d[22]=fx->lit[2]; d[23]=fx->ao;
  d[24]=fx->ldir[0]; d[25]=fx->ldir[1]; d[26]=fx->ldir[2]; d[27]=fx->rough;
  /* surface kind, detail scale, sheen, phase.  Only the creature sets
     these; an fx without them lands on zero and the shader skips the
     whole block. */
  d[28]=fx->sk; d[29]=fx->sf; d[30]=fx->si; d[31]=fx->sp;
  nEnt++;
}
void pushBox(float x,float y,float z,float sx,float sy,float sz,
             float r,float g,float b,float emis,const EntFx *fx,int prim){
  float M[12]; mTrans34(x,y,z,M);
  pushEnt(M,sx,sy,sz,r,g,b,emis,fx,prim);
}

/* ---- light sampling for moving things ------------------------------- */
int rayTraceOne(float ox,float oy,float oz,float dx,float dy,float dz,
                float maxT,int skip);           /* bake.c */

void sampleLight(float x,float y,float z,float *out,float *outDir){
  out[0]=out[1]=out[2]=0;
  outDir[0]=0; outDir[1]=1; outDir[2]=0;
  float wx=0,wy=0,wz=0;
  for(int i=0;i<nLight;i++){
    Light *L=&LIGHTS[i];
    float dx=L->p[0]-x, dy=L->p[1]-y, dz=L->p[2]-z;
    float dist=hypot3(dx,dy,dz);
    if(dist>=L->r||dist<1e-3f) continue;
    float inv=1.f/dist; dx*=inv; dy*=inv; dz*=inv;
    float att=1-dist/L->r; att*=att;
    if(att<0.004f) continue;
    if(rayTraceOne(x,y,z,dx,dy,dz,dist-0.12f,-1)>=0) continue;
    /* AND A SWITCHED-OFF LAMP LIGHTS NOTHING THAT MOVES EITHER.  This is
       the only light an entity gets, so without the style here an animal
       walks through a dead corridor lit by the lamps that are out. */
    float sc[3]={1,1,1};
    { int g=lampStyle(i);
      if(g>=0){ sc[0]=STY_C[g*3]; sc[1]=STY_C[g*3+1]; sc[2]=STY_C[g*3+2]; } }
    out[0]+=L->c[0]*att*sc[0]; out[1]+=L->c[1]*att*sc[1];
    out[2]+=L->c[2]*att*sc[2];
    float w=att*(L->c[0]*sc[0]+L->c[1]*sc[1]+L->c[2]*sc[2]);
    wx+=dx*w; wy+=dy*w; wz+=dz*w;
  }
  float m=hypot3(wx,wy,wz);
  if(m>1e-5f){ outDir[0]=wx/m; outDir[1]=wy/m; outDir[2]=wz/m; }
}

/* ---- the shaders ---------------------------------------------------- */
static const char *ENT_VS =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec4 aM0;\n"      /* rows of a 3x4 transform */
"layout(location=3) in vec4 aM1;\n"
"layout(location=4) in vec4 aM2;\n"
"layout(location=5) in vec4 aCol;\n"     /* rgb albedo, a emissive */
"layout(location=6) in vec4 aFx;\n"      /* hit flash, alpha, zone, rim */
"layout(location=7) in vec4 aLit;\n"     /* rgb direct light, a ao */
"layout(location=8) in vec4 aLd;\n"      /* xyz dominant dir, w roughness */
"layout(location=9) in vec4 aSurf;\n"
/* AND THE BENCH'S REGION, per VERTEX rather than per instance: a prop
   is one instance per material group and a group holds many regions,
   so the kind of surface a face wears cannot ride on the instance.
   Zero, with kind -1, on everything that is not a prop -- prim.c. */
"layout(location=10) in vec4 aRUV;\n"   /* u,v region metres; w,h */
"layout(location=11) in vec4 aRK;\n"    /* kind+16*rim, seed, px, py */    /* kind, detail scale, sheen, phase */
"uniform mat4 uProj,uView;\n"
"uniform int uVM;\n"                     /* 1 = already view-space */
"out vec3 vPos,vN,vAlb,vLit,vLd,vObjW,vAxL;\n"
"out vec4 vFx,vSurf;\n"
"out vec4 vRUV,vRK;\n"
"out float vEmis,vAO,vRough,vElong,vAlong,vThin;\n"
"void main(){\n"
"  vec4 p4=vec4(aPos,1.0);\n"
"  vec3 wp=vec3(dot(aM0,p4),dot(aM1,p4),dot(aM2,p4));\n"
"  vN=normalize(vec3(dot(aM0.xyz,aNrm),dot(aM1.xyz,aNrm),dot(aM2.xyz,aNrm)));\n"
"  vPos=wp; vAlb=aCol.rgb; vEmis=aCol.a; vFx=aFx;\n"
"  vLit=aLit.rgb; vAO=aLit.a; vLd=aLd.xyz; vRough=aLd.w;\n"
/* The unit primitive's own coordinates -- what nails the surface pattern to
   the part rather than to the world, so a limb that swings keeps its
   plating -- plus the part's LONGEST axis in world space and how long it is
   beside its shortest. */
"  vSurf=aSurf;\n"
"  vRUV=aRUV; vRK=aRK;\n"
"  vec3 axX=vec3(aM0.x,aM1.x,aM2.x), axY=vec3(aM0.y,aM1.y,aM2.y),\n"
"       axZ=vec3(aM0.z,aM1.z,aM2.z);\n"
"  float lx=length(axX), ly=length(axY), lz=length(axZ);\n"
"  float lmx=max(lx,max(ly,lz)), lmn=min(lx,min(ly,lz));\n"
"  vAxL=(lmx==ly)?axY:((lmx==lz)?axZ:axX);\n"
"  vElong=lmx/max(lmn,1e-4);\n"
"  vThin=lmn;\n"
/* The vertex in METRES, in the part's own frame, with the components
   rotated so .y always runs along the part's length.  Measuring the hide in
   metres rather than in units-of-this-part is the whole reason a run of
   solids of six different sizes can read as one animal. */
"  vec3 pw=vec3(aPos.x*lx, aPos.y*ly, aPos.z*lz);\n"
"  vObjW =(lmx==ly)?pw:((lmx==lz)?pw.xzy:pw.yxz);\n"
"  vAlong=(lmx==ly)?aPos.y:((lmx==lz)?aPos.z:aPos.x);\n"
"  gl_Position = (uVM==1) ? uProj*vec4(wp,1.0) : uProj*uView*vec4(wp,1.0);\n"
"}\n";

/* %d order: CUBE_N*6, MAXD, MAXD, MAXD, MAXD, CUBE_N-1, MAXD */
static const char *ENT_FS_FMT =
"#version 330 core\n"
/* the prop bench's own height library, extracted verbatim by
   tools/propglsl.py.  It declares gMat, uTPM, uRim and uWear at
   file scope and nothing else, and it reads no varying -- so it
   goes in whole, above everything, and costs nothing on a
   fragment that is not a prop. */
"%s\n"
"in vec3 vPos,vN,vAlb,vLit,vLd,vObjW,vAxL;\n"
"in vec4 vFx,vSurf,vRUV,vRK;\n"
"in float vEmis,vAO,vRough,vElong,vAlong,vThin;\n"
/* cycles per metre.  ONE number for the whole creature -- the point of the
   exercise is that no part gets to have its own texel density. */
"const float HIDE=46.0;\n"
"uniform vec3 uCamPos,uCamDir;\n"
"uniform vec3 uCube[%d];\n"
"uniform vec3 uDynP[%d];\n"
"uniform vec3 uDynC[%d];\n"
"uniform float uDynR[%d];\n"
"uniform int uDynN;\n"
/* the ship's own lamps, the same twelve the world shader gets: a prop
   standing under an amber strip has to catch it, or it is the one thing in
   the corridor that the corridor's lights do not touch */
"uniform vec4 uSLP[%d];\n"
"uniform vec3 uSLC[%d];\n"
"uniform int uSLN;\n"
"uniform float uFlash,uFogDensity,uVMFlash,uTime;\n"
"uniform vec3 uFogCol,uMinLight;\n"
"uniform int uVM;\n"
"out vec4 outColor;\n"
"float h31(vec3 p){ return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453); }\n"
"float vn3(vec3 p){\n"
"  vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
"  float a=mix(mix(h31(i),               h31(i+vec3(1,0,0)),f.x),\n"
"              mix(h31(i+vec3(0,1,0)),   h31(i+vec3(1,1,0)),f.x),f.y);\n"
"  float b=mix(mix(h31(i+vec3(0,0,1)),   h31(i+vec3(1,0,1)),f.x),\n"
"              mix(h31(i+vec3(0,1,1)),   h31(i+vec3(1,1,1)),f.x),f.y);\n"
"  return mix(a,b,f.z);\n"
"}\n"
/* ridged: the fold makes lumps with sharp valleys between them, which is
   what a tuberculate hide looks like.  Plain noise only ever looks blurry. */
"float rdg(vec3 p){ float v=vn3(p); return 1.0-abs(v+v-1.0); }\n"
/* GGX.  Chitin is a hard dielectric, and a hard dielectric puts a small
   bright core inside a wide dim tail.  One Blinn exponent gives you the core
   or the tail, never both at once. */
"float ggxD(float ndh,float a2){\n"
"  float d=ndh*ndh*(a2-1.0)+1.0;\n"
"  return a2/(3.14159265*d*d+1e-6);\n"
"}\n"
/* AND THE OTHER TWO THIRDS OF COOK-TORRANCE, WHICH THIS FILE DID NOT HAVE.
   render.c and vmw.c have shaded with F*D*V since the GGX pass; ent.c kept
   pow(N.H,shin)*(shin+8)/25.13 for everything that was not a creature --
   which is every prop, every door leaf, the capsule and the drone.  Three
   things were wrong with it and all three are visible.  There is no
   Fresnel, so a panel is exactly as bright face-on as edge-on and never
   gets the grazing sheen that says HARD SURFACE.  There is no shadowing-
   masking term, so a rough face keeps a highlight it should have lost.  And
   the normalisation is the one for a hemisphere of Blinn lobes rather than
   a BRDF: at shin=150 it peaks at 6.29 where a dielectric's real answer is
   0.16, so the highlight arrived forty times too bright and the tonemap
   flattened it -- which is the whole reason a machined prop read as painted
   plastic standing against a wall that did not. */
"float vSmith(float nov,float nol,float a2){\n"
"  float gv=nol*sqrt(nov*nov*(1.0-a2)+a2);\n"
"  float gl=nov*sqrt(nol*nol*(1.0-a2)+a2);\n"
"  return 0.5/max(gv+gl,1e-5);\n"
"}\n"
"vec3 fSch(vec3 f0,float u){ float x=clamp(1.0-u,0.0,1.0);\n"
"  float x2=x*x; return f0+(1.0-f0)*(x2*x2*x); }\n"
/* Kajiya-Kay.  Bristles lying along T do not reflect to a point; they
   reflect into a cone about T, so the highlight on a hairy limb is a BAND
   running across it.  That band is the difference between a leg and a
   dowel. */
"float kkSpec(vec3 T,vec3 L,vec3 V,float e){\n"
"  float tl=dot(T,L), tv=dot(T,V);\n"
"  float sn=sqrt(max(0.0,1.0-tl*tl))*sqrt(max(0.0,1.0-tv*tv))-tl*tv;\n"
"  return pow(max(sn,0.0),e);\n"
"}\n"
/* ONE LOBE FOR BOTH, AND IT IS THE ONE THE WALLS ALREADY USE.  F*D*V*NoL,
   with the Fresnel taken from the surface's own F0 -- so a metal tints its
   own highlight and stops needing the specTint fudge that stood in for it,
   and a dielectric gets the grazing sheen it never had.  The D clamp that
   used to sit here is gone: it was holding back a spike that only existed
   because there was no shadowing term underneath it, and alph has a hard
   0.15 floor, so D cannot exceed 14.2 however polished a part claims to
   be.  The creature keeps its Kajiya-Kay band on top -- bristles reflect
   into a cone about the hair axis and no microfacet model says that -- but
   the band now rides on a correct base instead of standing in for one. */
"vec3 lspec(vec3 lc,vec3 L,vec3 N,vec3 V,float nl,vec3 T,float fz,float lb,\n"
"           vec3 f0,float a2,float kkE,float tk,bool sf){\n"
"  if(nl<=0.0) return vec3(0.0);\n"
"  vec3 H=normalize(L+V);\n"
"  float ndh=max(dot(N,H),0.0);\n"
"  float ndv=max(dot(N,V),1e-3);\n"
"  vec3 F=fSch(f0,max(dot(V,H),0.0));\n"
"  vec3 ct=F*(ggxD(ndh,a2)*vSmith(ndv,nl,a2)*nl);\n"
"  if(!sf) return lc*ct;\n"
"  return lc*( ct*(1.0-0.55*lb)\n"
"            + kkSpec(T,L,V,kkE)*(0.05+0.24*fz)*lb*nl )*tk;\n"
"}\n"
TORCH_GLSL
"vec3 ambCube(int zi,vec3 d){\n"
"  vec3 s=d*d; int o=zi*6;\n"
"  return s.x*(d.x>0.0?uCube[o  ]:uCube[o+1])\n"
"       + s.y*(d.y>0.0?uCube[o+2]:uCube[o+3])\n"
"       + s.z*(d.z>0.0?uCube[o+4]:uCube[o+5]);\n"
"}\n"
/* the crew reflect the room the walls reflect.  Same octahedral probe, same
   integrated DFG, same layer arithmetic as render.c and vmw.c -- three
   copies of one lookup, and if they ever drift a body stops belonging to
   the corridor it is standing in. */
"uniform sampler2DArray uProbe;\n"
"uniform sampler2D uDFG;\n"
"uniform float uPN,uPLev;\n"
"vec2 octEnc(vec3 d){\n"
"  d/=(abs(d.x)+abs(d.y)+abs(d.z));\n"
"  vec2 p=vec2(d.x,d.z);\n"
"  if(d.y<0.0) p=vec2((1.0-abs(p.y))*(p.x>=0.0?1.0:-1.0),\n"
"                     (1.0-abs(p.x))*(p.y>=0.0?1.0:-1.0));\n"
"  return p;}\n"
"vec3 probeAt(int zi,vec3 R,float rough){\n"
"  vec2 t=((octEnc(R)*0.5+0.5)*uPN+1.0)/(uPN+2.0);\n"
"  float lv=clamp(rough,0.0,1.0)*uPLev;\n"
"  float l0=floor(lv);\n"
"  float b=float(zi)*(uPLev+1.0);\n"
"  vec3 a=texture(uProbe,vec3(t,b+l0)).rgb;\n"
"  vec3 c=texture(uProbe,vec3(t,b+min(l0+1.0,uPLev))).rgb;\n"
"  return mix(a,c,lv-l0);}\n"
"void main(){\n"
"  vec3 N=normalize(vN);\n"
"  vec3 V=(uVM==1)?normalize(-vPos):normalize(uCamPos-vPos);\n"
"  float rough=clamp(vRough,0.05,1.0);\n"
"  vec3 alb=vAlb;\n"
"  float grain=0.0, vein=0.0, wet=0.0;\n"
"  bool MC=vSurf.x>2.5;\n"
"  bool SF=vSurf.x>0.5&&!MC, shell=false;\n"
"  vec3 T=vec3(0.0,1.0,0.0);\n"
"  float fuzz=0.0, trans=0.0, limb=0.0;\n"
/* 1 on a bristle, 0 on anything as thick as a leg.  Everything downstream
   that keys off the viewing angle is faded out by it, because on a part this
   thin there is no face left -- it is edge from side to side. */
"  float slim=SF?(1.0-smoothstep(0.014,0.095,vThin)):0.0;\n"
"  float thinK=1.0-0.80*slim;\n"
"  if(SF){\n"
"    shell=vSurf.x<1.5;\n"
"    limb=smoothstep(1.35,2.60,vElong);\n"
"    vec3 q=vObjW*(HIDE*vSurf.y);\n"
"    float c0=rdg(q);\n"
"    float hair=vn3(vObjW*vec3(120.0,11.0,120.0)*vSurf.y);\n"
"    float run =vn3(vObjW*vec3( 42.0, 5.5, 42.0)*vSurf.y);\n"
"    float near=(uVM==1)?1.0:1.0-smoothstep(3.0,9.0,length(uCamPos-vPos));\n"
"    grain=c0*0.44+rdg(q*2.6+7.3)*0.30+rdg(q*6.4+21.1)*0.16*near+hair*0.10;\n"
"    const float e=0.30;\n"
"    vec3 gr=vec3(rdg(q+vec3(e,0.0,0.0)),\n"
"                 rdg(q+vec3(0.0,e,0.0)),\n"
"                 rdg(q+vec3(0.0,0.0,e)))-c0;\n"
"    gr-=N*dot(gr,N);\n"
"    N=normalize(N+gr*(shell?1.00:0.58));\n"
"    vec3 ax=normalize(vAxL+vec3(1e-6,1e-5,1e-6));\n"
"    vec3 tt=ax-N*dot(N,ax);\n"
"    if(dot(tt,tt)<1e-5) tt=(abs(N.y)<0.9)?cross(N,vec3(0.0,1.0,0.0))\n"
"                                         :cross(N,vec3(1.0,0.0,0.0));\n"
"    T=normalize(tt+N*(hair-0.5)*0.55);\n"
"    fuzz=0.26+0.74*hair;\n"
"    wet=smoothstep(0.18,0.78,grain)*(0.70+0.30*run);\n"
"    float cav=mix(0.56,1.0,smoothstep(0.04,0.52,grain));\n"
"    alb*=(0.82+0.28*grain)*cav;\n"
"    alb=mix(alb,alb*0.72+vec3(0.030,0.028,0.023),\n"
"            smoothstep(0.30,0.95,N.y)*0.34*(0.4+0.6*grain));\n"
"    rough=clamp(rough*mix(1.26,0.54,wet*vSurf.z),0.05,1.0);\n"
"    if(shell){\n"
"      float band=smoothstep(0.30,0.49,abs(vAlong))*vSurf.w;\n"
"      alb*=mix(1.0,0.30,band);\n"
"      rough=mix(rough,0.88,band*0.75);\n"
"      fuzz*=mix(1.0,0.40,band);\n"
"    }else{\n"
"      float v=1.0-abs(fract(vObjW.y*9.0+grain*2.4+vSurf.w)*2.0-1.0);\n"
"      vein=pow(v,7.0)*(0.5+0.5*sin(uTime*2.5-vObjW.y*14.0+vSurf.w*6.2831853));\n"
"      trans=1.0;\n"
"    }\n"
"  }\n"
/* MACHINED.  A hard surface, and the point is that NOTHING HERE IS NOISE.
   Every feature is an edge: a 3 mm seam with a 9 mm chamfer either side of
   it, a bolt set inboard of each plate corner, and a brushed grain that runs
   one way and lives in the ROUGHNESS.  Measured in METRES, in the part's own
   frame, so a leaf that stretches as it opens does not stretch its plating.
   THEN THE CONDUCTOR TRICK: a metal has no diffuse worth the name, so the
   albedo stops being a colour and becomes the reflectance. */
/* ---- THE BENCH'S OWN SURFACE, EVALUATED RATHER THAN BAKED ---------
   props.c carries a region on every prop vertex and propglsl.py brought
   the eleven height functions across whole, so the plate edges, the
   bolts, the ribs, the countersunk perforations and the hazard stripes
   are the SAME arithmetic that draws them on the bench -- there is just
   no atlas between the function and the pixel.  The reason for
   evaluating rather than baking is one number: baking the 37
   props at the density a bolt needs is 1.6 GB of texture.
   Five evaluations a fragment: four for the gradient and one at the
   centre, which is also the one that writes gMat. */
"  if(vRK.x>=0.0){\n"
"    int rk=int(vRK.x+0.5);\n"
"    int kind=rk&15; uRim=rk>>4; uWear=1.0;\n"
"    float ww=max(vRUV.z,0.004), wh=max(vRUV.w,0.004);\n"
"    vec2 hw=vec2(ww,wh)*0.5;\n"
"    vec2 pp=mod(vRUV.xy+hw,vec2(ww,wh))-hw;\n"
"    vec3 dp1=dFdx(vPos), dp2=dFdy(vPos);\n"
"    vec2 dq1=dFdx(vRUV.xy), dq2=dFdy(vRUV.xy);\n"
"    float det=dq1.x*dq2.y-dq2.x*dq1.y;\n"
"    if(abs(det)>1e-11){\n"
"      vec3 T=normalize(( dq2.y*dp1-dq1.y*dp2)/det);\n"
"      vec3 B=normalize((-dq2.x*dp1+dq1.x*dp2)/det);\n"
"      float fp=max(length(vec2(dq1.x,dq2.x)),length(vec2(dq1.y,dq2.y)));\n"
"      float e=max(max(0.0006,min(ww,wh)*0.0022),fp*0.55);\n"
"      uTPM=1.0/max(fp,1e-5);\n"
"      vec4 pr=vec4(vRK.z,vRK.w,0.0,0.0);\n"
"      float hx1=H(pp+vec2(e,0.0),kind,vRK.y,pr,ww,wh);\n"
"      float hx0=H(pp-vec2(e,0.0),kind,vRK.y,pr,ww,wh);\n"
"      float hy1=H(pp+vec2(0.0,e),kind,vRK.y,pr,ww,wh);\n"
"      float hy0=H(pp-vec2(0.0,e),kind,vRK.y,pr,ww,wh);\n"
"      float hC=H(pp,kind,vRK.y,pr,ww,wh);\n"
"      vec3 tn=normalize(vec3(-(hx1-hx0)/(2.0*e)*0.62,\n"
"                            -(hy1-hy0)/(2.0*e)*0.62, 1.0));\n"
"      N=normalize(T*tn.x+B*tn.y+N*tn.z);\n"
"      float fast=step(0.5,gMat)*(1.0-step(1.5,gMat));\n"
"      float trim=step(3.5,gMat);\n"
"      alb=mix(alb,alb*0.60+vec3(0.062),fast);\n"
"      alb=mix(alb,alb*1.14,trim);\n"
"      rough=mix(rough,0.28,fast);\n"
"      float cav=clamp(0.34-hC,0.0,1.0);\n"
"      alb*=1.0-0.55*cav;\n"
"      rough=clamp(rough+0.30*cav,0.05,1.0);\n"
"    }\n"
"  }\n"
"  vec3 F0mc=vec3(0.045);\n"
"  if(MC){\n"
"    float pitch=max(vSurf.y,0.02);\n"
"    float near=(uVM==1)?1.0:1.0-smoothstep(7.0,17.0,length(uCamPos-vPos));\n"
"    vec3 an=abs(N);\n"
"    vec2 uv; vec3 TU,TV;\n"
"    if(an.y>an.x&&an.y>an.z){ uv=vObjW.xz; TU=vec3(1,0,0); TV=vec3(0,0,1); }\n"
"    else if(an.x>an.z){       uv=vObjW.zy; TU=vec3(0,0,1); TV=vec3(0,1,0); }\n"
"    else {                    uv=vObjW.xy; TU=vec3(1,0,0); TV=vec3(0,1,0); }\n"
"    vec2 tilt=vec2(0.0);\n"
"    float edge=0.0;\n"
"    if(vSurf.w<0.5){\n"
"      vec2 g=uv/pitch;\n"
"      vec2 c=fract(g)-0.5;\n"
"      vec2 d=(0.5-abs(c))*pitch;\n"
"      vec2 pr=1.0-smoothstep(0.003,0.012,d);\n"
"      tilt-=sign(c)*pr;\n"
"      edge=max(pr.x,pr.y);\n"
"      vec2 bd=d-vec2(0.044);\n"
"      float rr=length(bd), k=smoothstep(0.014,0.004,rr);\n"
"      tilt+=normalize(bd+vec2(1e-5))*k*(1.0-k)*3.2;\n"
"      edge=max(edge,k);\n"
"    }\n"
"    float ln=fract(sin(floor(uv.y*620.0)*12.9898)*43758.5453);\n"
"    tilt.x+=(ln-0.5)*0.11;\n"
"    N=normalize(N+(TU*tilt.x+TV*tilt.y)*0.62*near);\n"
"    rough=clamp(rough*(0.82+0.36*ln*near)+0.20*edge*near,0.05,1.0);\n"
"    alb*=1.0-0.40*edge*near;\n"
"    float met=clamp(vSurf.z,0.0,1.0);\n"
"    F0mc=mix(vec3(0.045),alb*0.90+0.06,met);\n"
"    alb*=1.0-0.88*met;\n"
"  }\n"
/* THE SAME SPECULAR AA THE WORLD SHADER AND THE VIEWMODEL USE, and it goes
   HERE rather than up with the material, because the MACHINED block above
   is the last thing that moves N -- running it earlier would measure the
   churn of a normal that is not the one being shaded with.  The prop
   surface is five height-function evaluations a fragment, so its normal
   churns harder inside a pixel than anything else in the ship. */
"  { vec3 du=dFdx(N),dv=dFdy(N);\n"
"    float varN=0.25*(dot(du,du)+dot(dv,dv));\n"
"    rough=sqrt(min(rough*rough+min(2.0*varN,0.18),1.0)); }\n"
/* the floor is what keeps the lobe a lobe.  Below about 0.15 the GGX peak is
   narrower than one facet of a twelve-sided lathe, so it stops being a
   highlight and becomes a stripe painted along an edge. */
"  float alph=clamp(rough*rough,0.150,1.0);\n"
"  float a2=alph*alph;\n"
"  float kkE=mix(150.0,18.0,rough);\n"
"  vec3 F0=F0mc;\n"
"  float ndv=clamp(dot(N,V),0.0,1.0);\n"
"  vec3 sheenL=vec3(0.0), backL=vec3(0.0);\n"
"  int zi=clamp(int(vFx.z+0.5),0,%d);\n"
"  vec3 diff=ambCube(zi,N)*vAO+uMinLight;\n"
"  vec3 spec=vec3(0.0);\n"
"  vec3 Lk=normalize(vLd+vec3(1e-5));\n"
"  float ndl=max(dot(N,Lk),0.0);\n"
"  diff+=vLit*(ndl*0.85+0.15);\n"
"  spec+=lspec(vLit,Lk,N,V,ndl,T,fuzz,limb,F0,a2,kkE,thinK,SF);\n"
"  if(SF){ sheenL+=vLit*ndl; backL+=vLit*pow(max(dot(V,-Lk),0.0),3.0); }\n"
"  for(int i=0;i<%d;i++){\n"
"    if(i>=uDynN) break;\n"
"    vec3 d=uDynP[i]-vPos;\n"
"    float dist=length(d);\n"
"    if(dist>=uDynR[i]) continue;\n"
"    vec3 L=d/max(dist,1e-4);\n"
"    float att=1.0-dist/uDynR[i]; att*=att;\n"
"    float nl=max(dot(N,L),0.0);\n"
"    diff+=uDynC[i]*(nl*0.9+0.1)*att;\n"
"    spec+=lspec(uDynC[i],L,N,V,nl,T,fuzz,limb,F0,a2,kkE,thinK,SF)*att;\n"
"    if(SF){ sheenL+=uDynC[i]*nl*att;\n"
"            backL+=uDynC[i]*att*pow(max(dot(V,-L),0.0),3.0); }\n"
"  }\n"
/* AND THE STATIC LAMPS, gated by the entity's own baked light the way the
   world shader gates them by the lightmap -- see render.c's loop for why
   the ratio is the visibility.  vLit is what the bake found standing here,
   so a prop in a doorway shadow keeps its shadow. */
"  if(uVM==0){\n"
"    for(int i=0;i<%d;i++){\n"
"      if(i>=uSLN) break;\n"
"      vec3 d=uSLP[i].xyz-vPos;\n"
"      float dist=length(d);\n"
"      if(dist>=uSLP[i].w) continue;\n"
"      vec3 L=d/max(dist,1e-4);\n"
"      float nl=dot(N,L);\n"
"      if(nl<=0.0) continue;\n"
"      float att=1.0-dist/uSLP[i].w; att*=att;\n"
"      vec3 E=uSLC[i]*(att*nl);\n"
"      float vis=clamp(dot(vLit,vec3(0.299,0.587,0.114))\n"
"                     /max(dot(E,vec3(0.299,0.587,0.114)),1e-4),0.0,1.0);\n"
"      spec+=lspec(uSLC[i]*(att*vis),L,N,V,nl,T,fuzz,limb,F0,a2,kkE,thinK,SF);\n"
"    }\n"
"  }\n"
/* the viewmodel gets a key over the left shoulder and a cool fill, both
   tinted by whatever the room is putting on the player */
"  if(uVM==1){\n"
"    vec3 key=normalize(vec3(-0.45,0.72,0.52));\n"
"    vec3 fill=normalize(vec3(0.62,0.05,0.78));\n"
"    vec3 floorLit=ambCube(zi,vec3(0.0,1.0,0.0))*0.80+vec3(0.052,0.055,0.062);\n"
"    vec3 keyCol=vLit+floorLit*0.9;\n"
"    diff=floorLit*0.55+uMinLight\n"
"        +keyCol*(max(dot(N,key),0.0)*1.00+0.30)\n"
"        +keyCol*max(dot(N,fill),0.0)*0.40;\n"
"    spec=lspec(keyCol,key,N,V,max(dot(N,key),0.0),T,fuzz,limb,F0,a2,kkE,thinK,SF);\n"
"    vec3 ML=normalize(vec3(0.23,-0.20,-1.16)-vPos);\n"
"    diff+=vec3(1.0,0.82,0.46)*uVMFlash*(max(dot(N,ML),0.0)*0.95+0.18)*2.6;\n"
"  }\n"
/* the same beam the walls get -- torch.h.  AND THE SPECULAR HALF OF IT ON
   EVERYTHING, not only on the crew: a prop standing in the beam with no
   highlight in it is the one thing that says it is not part of the room.
   That branch used to read if(SF). */
"  if(uFlash>0.5&&uVM==0){\n"
"    vec4 tb=torchBeam(vPos,uCamPos,uCamDir);\n"
"    if(tb.a>0.0){\n"
"      vec3 L=normalize(uCamPos-vPos);\n"
"      float nl=max(dot(N,L),0.0);\n"
"      diff+=tb.rgb*(nl*0.94+0.06);\n"
"      spec+=lspec(tb.rgb,L,N,V,nl,T,fuzz,limb,F0,a2,kkE,thinK,SF);\n"
"      if(SF) sheenL+=tb.rgb*nl;\n"
"    }\n"
"  }\n"
/* a rim in the room's own colour.  Gameplay needs a silhouette to read
   against a dark wall; taking the colour from the ambient cube keeps it from
   looking like a cartoon outline pasted on. */
"  float fres=pow(1.0-ndv,3.0);\n"
"  vec3 rim=ambCube(zi,-V)*fres*vFx.w*2.4*thinK;\n"
/* the split sum, as the world shader does it: prefiltered radiance times
   the integrated DFG, plus the energy a single microfacet bounce drops.
   The Fresnel-with-a-horizon-term this replaces was a stand-in for the
   table, and on the crew's wet surfaces it was the difference between a
   reflection and a sheen. */
"  vec2 dfg=texture(uDFG,vec2(ndv,rough)).rg;\n"
"  vec3 FssEss=F0*dfg.x+dfg.y;\n"
"  float Ems=1.0-(dfg.x+dfg.y);\n"
"  vec3 Favg=F0+(1.0-F0)*0.047619;\n"
"  vec3 Fms=FssEss*Favg/(1.0-Ems*Favg);\n"
"  vec3 env=probeAt(zi,reflect(-V,N),rough)*FssEss+(Fms*Ems)*ambCube(zi,N);\n"
/* spec carries its own tint now: fSch() takes it from F0, which for a metal
   IS the albedo.  specTint was the stand-in for that Fresnel, and keeping
   both would colour the highlight twice. */
"  vec3 col=alb*diff+spec+rim+env*vAO*(uVM==1?0.45:(SF?0.55*thinK:1.0));\n"
"  col+=alb*vEmis*2.4;\n"
"  if(SF){\n"
"    float fr=pow(1.0-ndv,2.7);\n"
"    vec3 sheenCol=mix(vec3(0.46,0.44,0.43),alb*2.6,0.46);\n"
"    col+=sheenCol*fr*fuzz*(sheenL*0.26+ambCube(zi,N)*0.22)\n"
"        *(0.40+0.60*limb)*(shell?0.42:0.72)*thinK;\n"
"    col+=alb*backL*trans*1.30;\n"
"    float fs=pow(1.0-ndv,3.4);\n"
"    vec3 irid=0.5+0.5*cos(6.2831853*(vec3(0.0,0.30,0.62)+fs*1.10+grain*0.50));\n"
"    vec3 film=mix(vec3(0.50,0.58,0.74),irid*vec3(0.60,0.88,1.00),0.42);\n"
"    col+=film*fs*(0.15+wet*0.60)*vSurf.z*(0.26+diff*0.72)*thinK;\n"
"    col+=vec3(1.00,0.32,0.07)*vein*0.70;\n"
"  }\n"
"  col=mix(col,vec3(1.35,0.72,0.62),clamp(vFx.x,0.0,1.0));\n"
"  if(uVM==0){\n"
"    float fog=1.0-exp(-length(uCamPos-vPos)*uFogDensity);\n"
"    col=mix(col,uFogCol,clamp(fog,0.0,1.0));\n"
"  }\n"
"  outColor=vec4(max(col,0.0)*vFx.y,vFx.y);\n"
"}\n";

GLuint glMakeProgram(const char *vs,const char *fs,const char *what); /* render.c */

int entInit(int cubeCount){
  CUBE_N=cubeCount;
  entData  =(float*)malloc(sizeof(float)*MAX_ENT*ENT_STRIDE);
  entSorted=(float*)malloc(sizeof(float)*MAX_ENT*ENT_STRIDE);
  entPrim  =(unsigned short*)malloc(MAX_ENT*sizeof(unsigned short));

  /* 13 KB of it is the bench height library, spliced in as the first
     %s -- see propglsl.py.  The old 20 KB buffer held the shader and
     nothing else. */
  char *fs=(char*)malloc(48000);
  _snprintf(fs,48000,ENT_FS_FMT, PROPS_GLSL,
            CUBE_N*6, MAXD, MAXD, MAXD, MAXSL, MAXSL,
            CUBE_N-1, MAXD, MAXSL);
  pEnt=glMakeProgram(ENT_VS,fs,"entity");
  free(fs);
  if(!pEnt) return 0;
#define U(n) UE.n=glGetUniformLocation(pEnt,#n)
  U(uProj);U(uView);U(uVM);U(uCamPos);U(uCamDir);U(uCube);U(uDynP);U(uDynC);
  U(uDynR);U(uDynN);U(uFlash);U(uFogDensity);U(uVMFlash);U(uTime);U(uFogCol);
  U(uMinLight);U(uProbe);U(uDFG);U(uPN);U(uPLev);U(uSLP);U(uSLC);U(uSLN);
#undef U

  glGenVertexArrays(1,&entVAO);
  glBindVertexArray(entVAO);
  glGenBuffers(1,&entVB);
  glBindBuffer(GL_ARRAY_BUFFER,entVB);
  glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)PRIM_VN*4,PRIM_V,GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,(const void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(const void*)12);
  /* AND THE REGION, in step with it: PRIM_R holds eight floats for
     every vertex PRIM_V holds six for, so one more buffer and two more
     attributes carry the bench's regions with no change to any solid
     already in prim.c.  See prim.c and props.c. */
  glGenBuffers(1,&entRB);
  glBindBuffer(GL_ARRAY_BUFFER,entRB);
  glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(PRIM_VN/6)*8*4,PRIM_R,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(10);
  glVertexAttribPointer(10,4,GL_FLOAT,GL_FALSE,32,(const void*)0);
  glEnableVertexAttribArray(11);
  glVertexAttribPointer(11,4,GL_FLOAT,GL_FALSE,32,(const void*)16);
  glGenBuffers(1,&entInst);
  glBindBuffer(GL_ARRAY_BUFFER,entInst);
  glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)MAX_ENT*ENT_STRIDE*4,0,GL_DYNAMIC_DRAW);
  for(int i=0;i<8;i++){
    glEnableVertexAttribArray(2+i);
    glVertexAttribPointer(2+i,4,GL_FLOAT,GL_FALSE,ENT_STRIDE*4,
                          (const void*)(size_t)(i*16));
    glVertexAttribDivisor(2+i,1);
  }
  glBindVertexArray(0);
  return 1;
}

/* Counting-sort the instances by primitive, then one instanced draw per
   primitive.  There is no base-instance in 3.3 core either, so the attribute
   pointers are re-specified with a byte offset for each group. */
void entDraw(const FrameState *F,int viewModel){
  int n=nEnt;
  if(!n) return;
  int cnt[PRIM_N_MAX]={0}, start[PRIM_N_MAX], cur[PRIM_N_MAX];
  for(int i=0;i<n;i++) cnt[entPrim[i]]++;
  int acc=0;
  for(int p=0;p<PRIM_N;p++){ start[p]=acc; cur[p]=acc; acc+=cnt[p]; }
  for(int i=0;i<n;i++){
    int d=cur[entPrim[i]]++;
    memcpy(entSorted+(size_t)d*ENT_STRIDE, entData+(size_t)i*ENT_STRIDE,
           sizeof(float)*ENT_STRIDE);
  }

  glUseProgram(pEnt);
  glUniformMatrix4fv(UE.uProj,1,GL_FALSE,F->proj);
  glUniformMatrix4fv(UE.uView,1,GL_FALSE,F->view);
  glUniform1i(UE.uVM,viewModel);
  glUniform3fv(UE.uCamPos,1,F->camPos);
  glUniform3fv(UE.uCamDir,1,F->camDir);
  /* renderBeginScene picked them; the frame order guarantees it ran */
  glUniform4fv(UE.uSLP,MAXSL,SL_P);
  glUniform3fv(UE.uSLC,MAXSL,SL_C);
  glUniform1i(UE.uSLN,SL_N);
  glUniform3fv(UE.uCube,CUBE_N*6,CUBES);
  glUniform3fv(UE.uDynP,MAXD,F->dynP);
  glUniform3fv(UE.uDynC,MAXD,F->dynC);
  glUniform1fv(UE.uDynR,MAXD,F->dynR);
  glUniform1i(UE.uDynN,F->dynN);
  glUniform1f(UE.uFlash,F->flash?1.f:0.f);
  glUniform1f(UE.uFogDensity,F->fogDensity);
  glUniform1f(UE.uVMFlash,F->vmFlash);
  glUniform1f(UE.uTime,F->time);
  glUniform3fv(UE.uFogCol,1,F->fogCol);
  glUniform3fv(UE.uMinLight,1,F->minLight);
  /* the world's probe and DFG table.  ent.c draws nothing from a texture of
     its own, so units 0 and 1 were free. */
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY,renderProbeTex());
  glUniform1i(UE.uProbe,0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D,renderDFGTex());
  glUniform1i(UE.uDFG,1);
  glActiveTexture(GL_TEXTURE0);
  glUniform1f(UE.uPN,(float)PROBE_N);
  glUniform1f(UE.uPLev,(float)(PROBE_LEV-1));

  glBindVertexArray(entVAO);
  glBindBuffer(GL_ARRAY_BUFFER,entInst);
  glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)n*ENT_STRIDE*4,entSorted);
  for(int p=0;p<PRIM_N;p++){
    if(!cnt[p]) continue;
    size_t off=(size_t)start[p]*ENT_STRIDE*4;
    for(int i=0;i<8;i++)
      glVertexAttribPointer(2+i,4,GL_FLOAT,GL_FALSE,ENT_STRIDE*4,
                            (const void*)(off+i*16));
    glDrawArraysInstanced(GL_TRIANGLES,PRIM_OFF[p],PRIM_CNT[p],cnt[p]);
  }
  glBindVertexArray(0);
}
