/* ============================================================
   7.  GL  --  the scene program, ported from WebGL2 to GL 3.3
               core.  The shader is the same shader: `#version
               300 es` becomes `#version 330 core`, the precision
               qualifiers go (desktop GLSL has no lowp), and the
               `${}` the JS template interpolated is spelled with
               snprintf instead.  Nothing else changed, because
               nothing else needed to.
   ============================================================ */
#include "gl33.h"
#include "ship.h"
#include "torch.h"
#include "app.h"
#include "ent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int R_DEBUG=0;
int R_FLASH=0;

#define MAXD 8

/* THE 257 LAMPS OF THIS SHIP PUT NO HIGHLIGHT ON ANYTHING, AND THAT IS WHY
   IT READ AS PAINTED.  The bake solves their DIFFUSE contribution and
   stores it in the lightmap, where it is correct and shadowed and cannot
   move; but specular is a function of where the eye is, so no bake can
   hold it, and the only thing standing in for it was one blurred lobe
   fired down the lightmap's own dominant direction.  A corridor lined with
   amber strips therefore had amber strips in it and no amber ON anything.

   MAXSL of them are handed to the shader each frame -- the ones whose
   sphere of influence is nearest the camera, which in a corridor is the
   corridor's own -- with their bank's flicker level already multiplied in,
   so a failing tube's reflection fails with it.  ship.h holds MAXSL itself,
   because ent.c uploads the same sixteen; SL_REACH below is the other half
   of the pick and the loop that uses it says what it means. */
#define SL_REACH 10.0f
#define SL_FADE   2.0f        /* the last of the reach, spent letting go */

static GLuint pScene, vao, vbo, albTex, nrmTex, probeTex, dfgTex;
static int CUBE_N;
static float *AMBCUBE;                    /* CUBE_N*6 vec3 */
static struct {
  GLint uProj,uView,uCamPos,uCamDir,uAlb,uNrm,uFlash,uFogDensity,uFogCol,
        uMinLight,uDebug,uMetal,uSurf,uCube,uStyleC,uDynP,uDynC,uDynR,uDynN,
        uProbe,uDFG,uSLP,uSLC,uSLN;
} US;

/* ---- shader plumbing ----------------------------------------------- */
static GLuint compile(GLenum kind,const char *src,const char *what){
  GLuint s=glCreateShader(kind);
  glShaderSource(s,1,&src,0);
  glCompileShader(s);
  GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if(!ok){
    char log[4096]; glGetShaderInfoLog(s,sizeof log,0,log);
    _snprintf(platFail,sizeof platFail,"%s shader:\n%s",what,log);
    platLog("%s",platFail);
    return 0;
  }
  return s;
}
GLuint glMakeProgram(const char *vs,const char *fs,const char *what){
  GLuint v=compile(GL_VERTEX_SHADER,vs,what);
  if(!v) return 0;
  GLuint f=compile(GL_FRAGMENT_SHADER,fs,what);
  if(!f) return 0;
  GLuint p=glCreateProgram();
  glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
  GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok){
    char log[4096]; glGetProgramInfoLog(p,sizeof log,0,log);
    _snprintf(platFail,sizeof platFail,"%s link:\n%s",what,log);
    platLog("%s",platFail);
    return 0;
  }
  glDeleteShader(v); glDeleteShader(f);
  return p;
}

/* ---- the scene shader ---------------------------------------------- */
/* %d order: MAXSL, MAXSL, MAXSL */
static const char *SCENE_VS_FMT =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in float aTex;\n"
"layout(location=4) in float aAO;\n"
"layout(location=5) in vec3 aLM0;\n"
"layout(location=6) in vec3 aLM1;\n"
"layout(location=7) in vec3 aLM2;\n"
"layout(location=8) in vec3 aSty;\n"
"layout(location=9) in float aCube;\n"
"layout(location=10) in float aStyG;\n"
"uniform mat4 uProj,uView;\n"
/* THE SAME SIXTEEN LAMPS THE FRAGMENT SHADER IS GIVEN, SUMMED HERE TOO.
   What the pair of them is for is under A LIGHT POOL WITH CORNERS IN IT,
   below.  Declaring the arrays in both stages links to ONE uniform, so
   renderFrame still uploads them once. */
"uniform vec4 uSLP[%d];\n"
"uniform vec3 uSLC[%d];\n"
"uniform int uSLN;\n"
"out vec3 vPos; out vec3 vN; out vec2 vUV; out float vTex; out float vAO;\n"
"out vec3 vLM0; out vec3 vLM1; out vec3 vLM2; out vec3 vSty;\n"
"out float vCube; out float vStyG; out vec3 vSLD;\n"
"void main(){ vPos=aPos; vN=aNormal; vUV=aUV; vTex=aTex; vAO=aAO;\n"
"  vLM0=aLM0; vLM1=aLM1; vLM2=aLM2; vSty=aSty; vCube=aCube; vStyG=aStyG;\n"
/* the GEOMETRIC normal, and the fragment side uses it too: what the pair
   measures is the falloff, and the normal map must not get into it. */
"  vec3 gnv=normalize(aNormal);\n"
"  vSLD=vec3(0.0);\n"
"  for(int i=0;i<%d;i++){\n"
"    if(i>=uSLN) break;\n"
"    vec3 d=uSLP[i].xyz-aPos;\n"
"    float dist=length(d);\n"
"    if(dist>=uSLP[i].w) continue;\n"
"    float att=1.0-dist/uSLP[i].w; att*=att;\n"
"    vSLD+=uSLC[i]*(att*max(dot(gnv,d/max(dist,1e-4)),0.0));\n"
"  }\n"
"  gl_Position=uProj*uView*vec4(aPos,1.0); }\n";

/* %d order: CUBE_N*6, TL, TL, MAXD, MAXD, MAXD, MAXSL, MAXSL,
                PROBE_N, PROBE_LEV, TL-1, CUBE_N-1, TL-1, MAXSL, MAXD */
static const char *SCENE_FS_FMT =
"#version 330 core\n"
"in vec3 vPos; in vec3 vN; in vec2 vUV; in float vTex; in float vAO;\n"
"in vec3 vLM0; in vec3 vLM1; in vec3 vLM2; in vec3 vSty;\n"
"in float vCube; in float vStyG; in vec3 vSLD;\n"
"uniform sampler2DArray uAlb;\n"
"uniform sampler2DArray uNrm;\n"
"uniform sampler2DArray uProbe;\n"
"uniform sampler2D uDFG;\n"
"uniform vec3 uCamPos,uCamDir;\n"
"uniform vec3 uCube[%d];\n"
/* tint x level for each switchable lamp bank -- bake.c lampStyle decides
   which lamps are in one, story.c decides what the seven values are. */
"uniform vec3 uStyleC[7];\n"
"uniform float uFlash,uFogDensity;\n"
"uniform vec3 uFogCol,uMinLight;\n"
"uniform int uDebug;\n"
"uniform float uMetal[%d];\n"
"uniform float uSurf[%d];\n"
"uniform vec3 uDynP[%d];\n"
"uniform vec3 uDynC[%d];\n"
"uniform float uDynR[%d];\n"
"uniform int uDynN;\n"
/* the ship's own lamps, xyz and radius in one vec4 so the array costs the
   slots it actually needs; uSLC already carries the bank's flicker */
"uniform vec4 uSLP[%d];\n"
"uniform vec3 uSLC[%d];\n"
"uniform int uSLN;\n"
"out vec4 outColor;\n"
/* Valve's Half-Life 2 basis: three unit vectors 54.7 degrees off the normal */
"const vec3 B0=vec3(-0.4082483,-0.7071068,0.5773503);\n"
"const vec3 B1=vec3(-0.4082483, 0.7071068,0.5773503);\n"
"const vec3 B2=vec3( 0.8164966, 0.0,      0.5773503);\n"
"const float PI=3.14159265;\n"
/* THE PROBE IS AN OCTAHEDRAL MAP, ONE LAYER PER (ZONE, ROUGHNESS).  GL 3.3
   core has no cubemap array, and a 2D array needs no extension; PN is the
   inner edge, and the +1/+2 below steps over the traced border ring that
   lets bilinear cross the seam. */
"const float PN=%d.0;\n"
"const int   PL=%d;\n"
/* THE ONE KNOB.  Everything else in the reflection is now measured -- the
   probe is traced radiance, the DFG is an integrated table, the
   compensation is derived -- so if metal reads too hot or too dead in the
   ship, this is the number to move, and nothing else. */
"const float ENV_K=1.0;\n"
/* A PUNCTUAL LIGHT HAS NO SIZE, AND EVERY LAMP IN THIS SHIP DOES.  GGX
   asked for a mirror and a zero-area source answers 1/(PI*a^2) -- at the
   0.04 roughness floor that is 78,000, and the bloom threshold turns it
   into a white hole.  A_LIGHT is the smallest lobe a light is allowed to
   have, and 0.05 is not a taste: a 0.3 m lamp panel at 3 m subtends about
   0.05 rad, and it is also the alpha at which D*V peaks at 25.8 -- the
   exact peak the Blinn-Phong this replaces had at shin=640. */
"const float A_LIGHT=0.05;\n"
/* AND THE SHIP'S OWN FITTINGS ARE BIGGER THAN THAT.  A_LIGHT is sized for
   a 0.3 m panel at 3 m, which is the muzzle flash and the torch; the
   overhead fittings are 1.24 x 0.60 m and the skirt strips are metres
   long, so at the distance they are actually seen they subtend three or
   four times as much and 0.05 would be claiming they are mirrors.  It is
   the fitting's own size, not a taste knob.  (It is NOT what fixed the
   dotted line along the plate seams -- that turned out to be a real bolt
   row in the roughness map, finally being lit by something.) */
"const float A_LAMP=0.11;\n"
/* SCHLICK'S BASE MUST BE CLAMPED, NOT JUST FLOORED.  The torch puts the
   light AT the eye, so L and V are the same vector and dot(V,H) lands on
   1.0 -- where rounding tips it to 1.0000001, 1-u goes negative and
   pow(negative,5.0) is undefined: this GPU answers NaN, and clamp(NaN)
   is black.  That was the pepper on every torch-lit surface. */
"float fres5(float u){ float x=clamp(1.0-u,0.0,1.0);\n"
"  float x2=x*x; return x2*x2*x; }\n"
"vec3 ambCube(int zi,vec3 d){\n"
"  vec3 s=d*d; int o=zi*6;\n"
"  return s.x*(d.x>0.0?uCube[o  ]:uCube[o+1])\n"
"       + s.y*(d.y>0.0?uCube[o+2]:uCube[o+3])\n"
"       + s.z*(d.z>0.0?uCube[o+4]:uCube[o+5]);\n"
"}\n"
/* Cook-Torrance: the GGX distribution and Smith's height-correlated
   visibility, which already carries the 1/(4 NoL NoV) the BRDF needs.  What
   this buys over the Blinn-Phong it replaces is the tail -- a rough metal
   keeps a wide dim sheen instead of falling to nothing, which is most of
   what makes steel read as steel. */
"float dGGX(float noh,float a){\n"
"  float a2=a*a;\n"
"  float d=noh*noh*(a2-1.0)+1.0;\n"
"  return a2/max(PI*d*d,1e-8);\n"
"}\n"
"float vGGX(float nov,float nol,float a){\n"
"  float a2=a*a;\n"
"  float gv=nol*sqrt(nov*nov*(1.0-a2)+a2);\n"
"  float gl=nov*sqrt(nol*nol*(1.0-a2)+a2);\n"
"  return 0.5/max(gv+gl,1e-5);\n"
"}\n"
"vec3 ggx(vec3 L,vec3 N,vec3 V,vec3 F0,float a,float nov){\n"
"  float nol=dot(N,L);\n"
"  if(nol<=0.0) return vec3(0.0);\n"
"  vec3 H=normalize(L+V);\n"
"  vec3 F=F0+(1.0-F0)*fres5(dot(V,H));\n"
"  return F*(dGGX(max(dot(N,H),0.0),a)*vGGX(nov,nol,a)*nol);\n"
"}\n"
TORCH_GLSL
/* the inverse of bake.c's octDecode, and it has to stay the inverse */
"vec2 octEnc(vec3 d){\n"
"  d/=(abs(d.x)+abs(d.y)+abs(d.z));\n"
"  vec2 p=vec2(d.x,d.z);\n"
"  if(d.y<0.0) p=vec2((1.0-abs(p.y))*(p.x>=0.0?1.0:-1.0),\n"
"                     (1.0-abs(p.x))*(p.y>=0.0?1.0:-1.0));\n"
"  return p;\n"
"}\n"
"vec3 probeAt(int zi,vec3 R,float rough){\n"
"  vec2 t=((octEnc(R)*0.5+0.5)*PN+1.0)/(PN+2.0);\n"
"  float lv=clamp(rough,0.0,1.0)*float(PL-1);\n"
"  float l0=floor(lv);\n"
"  float b=float(zi*PL);\n"
"  vec3 a=texture(uProbe,vec3(t,b+l0)).rgb;\n"
"  vec3 c=texture(uProbe,vec3(t,b+min(l0+1.0,float(PL-1)))).rgb;\n"
"  return mix(a,c,lv-l0);\n"
"}\n"
"void main(){\n"
"  vec3 gn=normalize(vN);\n"
"  vec4 A=texture(uAlb,vec3(vUV,vTex));\n"
"  vec4 Nm=texture(uNrm,vec3(vUV,vTex));\n"
"  vec3 albedo=A.rgb;\n"
"  float rough=clamp(A.a,0.04,1.0);\n"
"  float emis=Nm.a;\n"
"  float surf=uSurf[clamp(int(vTex+0.5),0,%d)];\n"
/* Neither the chamber nor the sprite is in the room: no bake, no ambient
   cube, no dynamic lights and, above all, no fog. */
"  if(surf>1.5){\n"
"    vec3 sky=albedo*(1.0+emis*3.4);\n"
"    if(surf>2.5){ outColor=vec4(sky,clamp(A.a,0.0,1.0)); }\n"
"    else outColor=vec4(sky,1.0);\n"
"    return;\n"
"  }\n"
"  vec3 Tg = abs(gn.y)>0.999 ? vec3(1.0,0.0,0.0)\n"
"                            : normalize(vec3(gn.z,0.0,-gn.x));\n"
"  vec3 Bt = cross(gn,Tg);\n"
"  float camDist=length(uCamPos-vPos);\n"
/* z is not stored: the compiler wrote a unit vector, so x and y are all it
   takes to get it back.  Blue is the metal mask instead. */
"  vec3 tn;\n"
"  tn.xy=Nm.xy*2.0-1.0;\n"
"  tn.z=sqrt(max(1.0-dot(tn.xy,tn.xy),1e-4));\n"
"  tn=normalize(tn);\n"
"  vec3 N=normalize(Tg*tn.x+Bt*tn.y+gn*tn.z);\n"
/* SPECULAR AA, AND IT IS NOT OPTIONAL NOW.  Where the bump normals churn
   inside one pixel a GGX highlight becomes narrower than the pixel and
   turns into crawling white dots -- the ship's plate seams are exactly
   that shape.  The world shader got away without it while the only
   specular was one blurred lobe off the lightmap; sixteen analytic lamps
   put a real lobe on every seam and the sparkle came with them.  Widen
   the roughness by the variance of the normal, which is the same three
   lines vmw.c and the previz already use -- and widen `rough`, not
   `alpha`, so the probe and the DFG blur with it. */
"  { vec3 du=dFdx(N),dv=dFdy(N);\n"
"    float varN=0.25*(dot(du,du)+dot(dv,dv));\n"
"    rough=sqrt(min(rough*rough+min(2.0*varN,0.18),1.0)); }\n"
"  vec3 V=normalize(uCamPos-vPos);\n"
"  int zi=clamp(int(vCube+0.5),0,%d);\n"
"  float ao=vAO;\n"
/* radiosity normal mapping: the bake stored irradiance for three fixed
   directions, and the normal map decides how much of each this texel faces */
"  vec3 w=vec3(max(dot(tn,B0),0.0),max(dot(tn,B1),0.0),max(dot(tn,B2),0.0));\n"
"  w/=max(w.x+w.y+w.z,1e-4);\n"
"  vec3 baked=vLM0*w.x+vLM1*w.y+vLM2*w.z;\n"
"  float metal=mix(uMetal[clamp(int(vTex+0.5),0,%d)],1.0,Nm.b);\n"
"  vec3 F0=mix(vec3(0.04),albedo,metal);\n"
/* METAL HAS NO DIFFUSE.  It used to keep 15% of it, because the only
   reflection on offer was a six-value ambient cube and a metal with
   nothing to reflect is a black hole.  The probe is that reflection now,
   so the fudge comes out. */
"  vec3 dAlb=albedo*(1.0-metal);\n"
"  float alpha=rough*rough;\n"
"  float aL=max(alpha,A_LIGHT);\n"
"  float ndv=max(dot(N,V),1e-3);\n"
/* WHICH SWITCHABLE BANK THIS FACE BELONGS TO.  bake.c packs the group in
   the low three bits and bit 3 says the face IS the fixture; six banks,
   because one made every broken tube in the ship strike on the same
   frame.  vSty is the face's own baked contribution from that bank. */
"  int sgi=int(vStyG+0.5);\n"
"  int sgg=(sgi & 7)-1;\n"
"  vec3 sty = sgg>=0 ? uStyleC[sgg] : vec3(0.0);\n"
"  vec3 diff=baked+vSty*sty+ambCube(zi,N)*ao+uMinLight;\n"
/* specular from the lightmap's own dominant direction */
"  const vec3 LW=vec3(0.299,0.587,0.114);\n"
"  vec3 domTS=B0*dot(vLM0,LW)+B1*dot(vLM1,LW)+B2*dot(vLM2,LW);\n"
"  vec3 spec=vec3(0.0);\n"
/* HALF WEIGHT NOW, AND THE OTHER HALF IS BELOW.  The lightmap holds direct
   light and bounced light together, and the lamps in the loop under this
   put the direct half back analytically and sharply.  What is left for the
   dominant-direction lobe is the bounce -- a wide dim sheen off everything
   the room has already lit, which is exactly the shape this term has. */
"  if(dot(domTS,domTS)>1e-8){\n"
"    vec3 Ld=normalize(Tg*domTS.x+Bt*domTS.y+gn*domTS.z);\n"
"    spec+=(vLM0+vLM1+vLM2)*0.1667*ggx(Ld,N,V,F0,aL,ndv);\n"
"  }\n"
/* AND THE LAMPS THEMSELVES, WITH THE BAKE STANDING IN FOR A SHADOW MAP.
   An analytic light with no visibility test shines through bulkheads, and
   a highlight on the far side of a wall is worse than no highlight at all.
   There is no room for shadow maps here and no need for one: the bake
   already answered this question with a ray per lamp per texel.  E is what
   the lamp WOULD have contributed to this texel unshadowed, on the bake's
   own falloff -- (1-d/r) squared, matched to bake.c's directAt so the two
   are comparable -- and the baked irradiance actually standing here is
   what it contributed.  Their ratio is the visibility, clamped to one, so
   the test can only ever take light away.  A texel lit by some OTHER lamp
   passes a lamp it cannot see, which is the failure this trades for: it
   costs a highlight in a bright place rather than a glow through a wall. */
/* THE GATE HAS TO BE SMOOTH OR IT IS A DITHER PATTERN.  The obvious thing
   to divide by is `baked`, the irradiance this texel actually has -- but
   `baked` is the three basis directions weighted by the NORMAL MAP, and
   across a plate seam the normal map swings a quarter turn between one
   pixel and the next, so those weights swing with it and the ratio flips
   between 0 and 1 along the seam.  What comes out is not a highlight, it
   is a dotted line, and the chromatic aberration then paints the dots red
   and blue.  The basis sum without the weights is the same irradiance
   averaged over the hemisphere: it is vertex-interpolated, it cannot
   flip, and a visibility test does not need the normal map's opinion. */
"  vec3 bakedD=(vLM0+vLM1+vLM2)*0.3333+vSty*sty;\n"
"  vec3 slE=vec3(0.0), slV=vec3(0.0);\n"
"  for(int i=0;i<%d;i++){\n"
"    if(i>=uSLN) break;\n"
"    vec3 d=uSLP[i].xyz-vPos;\n"
"    float dist=length(d);\n"
"    if(dist>=uSLP[i].w) continue;\n"
"    vec3 L=d/max(dist,1e-4);\n"
"    float att=1.0-dist/uSLP[i].w; att*=att;\n"
"    vec3 E=uSLC[i]*(att*max(dot(gn,L),0.0));\n"
"    float vis=clamp(dot(bakedD,LW)/max(dot(E,LW),1e-4),0.0,1.0);\n"
/* the two sums the diffuse correction below is made of.  They are
   accumulated BEFORE the N.L gate, because the gate is the normal map's
   and vSLD -- the same sum at the vertex -- has never heard of it. */
"    slE+=E; slV+=E*vis;\n"
"    float ndl=dot(N,L);\n"
"    if(ndl<=0.0) continue;\n"
"    spec+=uSLC[i]*(att*vis)*ggx(L,N,V,F0,max(alpha,A_LAMP),ndv);\n"
"  }\n"
/* A LIGHT POOL WITH CORNERS IN IT.  The lightmap is stored PER VERTEX --
   on a 1.4 m lattice for a brush face and at the part's own corners for a
   kit part, which is coarser still -- and Gouraud between two vertices is
   a straight line.  The falloff is not: (1-d/r) squared is a curve, so
   every pool of light in the ship was that curve replaced by a polygon,
   with the lattice's own diagonal creased across it.  That is what reads
   as "the edge of the light has corners".

   The bake cannot be asked for more samples; the whole ship is 1.14 M of
   them already and the count is an area.  But the sixteen lamps nearest
   the camera are on hand in BOTH stages now, so the shader can measure
   what the interpolation lost and put it back: slE is their unshadowed
   irradiance HERE, vSLD is the same sum at each vertex arriving
   interpolated, and the difference is exactly the curvature Gouraud
   dropped.  It is zero at every vertex by construction, so no triangle
   can disagree with its neighbour along a shared edge, and what it can do
   is bend the inside of one.

   visF is how much of those lamps actually reached this texel, by the same
   ratio the specular loop uses -- so the correction is full in open light,
   gone in shadow, and never invents light behind a bulkhead.  Everything
   past the sixteen keeps its polygon, which in practice is a lamp too far
   away for the corners to be more than a pixel. */
"  float slL=dot(slE,LW);\n"
"  float visF = slL>1e-5 ? clamp(dot(slV,LW)/slL,0.0,1.0) : 0.0;\n"
"  vec3 slFix=(slE-vSLD)*visF;\n"
/* -debug 11 is the frame WITHOUT this, fog and all, so the before and the
   after are one build and one bake apart instead of two of each -- the
   lamp flicker runs off wall-clock and no two runs render the same frame,
   so a build-to-build diff cannot settle anything here. */
"  if(uDebug!=11) diff=max(diff+slFix,uMinLight);\n"
/* THE TORCH IS A BEAM WITH A COOKIE IN IT.  torch.h says what the three
   terms are and why every one of them is a function of the ray's direction
   rather than of the wall it lands on. */
"  if(uFlash>0.5){\n"
"    vec4 tb=torchBeam(vPos,uCamPos,uCamDir);\n"
"    if(tb.a>0.0){\n"
"      vec3 L=normalize(uCamPos-vPos);\n"
"      float ndl=max(dot(N,L),0.0);\n"
"      diff+=tb.rgb*(ndl*0.94+0.06);\n"
"      spec+=tb.rgb*ggx(L,N,V,F0,aL,ndv);\n"
"    }\n"
"  }\n"
/* a gunshot has to put light on the walls or it reads as a decal on the
   screen rather than an event in the room.  No shadowing: two or three
   frames, and the eye never gets to check. */
"  for(int i=0;i<%d;i++){\n"
"    if(i>=uDynN) break;\n"
"    vec3 d=uDynP[i]-vPos;\n"
"    float dist=length(d);\n"
"    if(dist>=uDynR[i]) continue;\n"
"    vec3 L=d/max(dist,1e-4);\n"
"    float att=1.0-dist/uDynR[i]; att*=att;\n"
"    float ndl=max(dot(N,L),0.0);\n"
"    diff+=uDynC[i]*(ndl*0.92+0.08)*att;\n"
"    spec+=uDynC[i]*att*ggx(L,N,V,F0,aL,ndv);\n"
"  }\n"
/* the split sum: the prefiltered radiance for this roughness, times the
   DFG the LUT integrated at load.  The two hand-tuned boosts that used to
   stand in for the DFG are gone -- that term is measured now. */
"  vec3 Rv=reflect(-V,N);\n"
"  vec3 pre=probeAt(zi,Rv,rough);\n"
"  vec2 dfg=texture(uDFG,vec2(ndv,rough)).rg;\n"
"  vec3 FssEss=F0*dfg.x+dfg.y;\n"
/* THE SPLIT SUM LOSES THE LIGHT THAT BOUNCED TWICE.  It carries one bounce
   off the microsurface and throws away everything that hit a second facet,
   which is a rounding error on a dielectric and a third of the reflection
   on rough metal: steel at roughness 0.8 with F0=0.6 comes back at 0.33
   instead of 0.6, and reads as soot.  Fernandez-Aguera's compensation puts
   the missing energy back where it went -- spread over the hemisphere, so
   it rides on the ambient cube rather than on the mirror direction. */
"  float Ems=1.0-(dfg.x+dfg.y);\n"
"  vec3 Favg=F0+(1.0-F0)*0.047619;\n"
"  vec3 Fms=FssEss*Favg/(1.0-Ems*Favg);\n"
"  vec3 ibl=(pre*FssEss+(Fms*Ems)*ambCube(zi,N))*ao*ENV_K;\n"
"  vec3 col=dAlb*diff+spec*mix(0.35,1.0,ao)+ibl;\n"
/* AND A LAMP THAT IS OFF HAS A DARK LENS.  Bit 3 of the style index says
   this face IS the fixture of its own bank, so its glow goes out with the
   bank -- without it a dead corridor is a corridor full of bright strips
   lighting nothing, which reads as a bug and not as a power failure. */
"  vec3 em=albedo*emis*2.1;\n"
"  if(sgi>=8 && sgg>=0) em*=uStyleC[sgg];\n"
"  col+=em;\n"
"  if(uDebug==1) col=albedo;\n"
"  else if(uDebug==2) col=N*0.5+0.5;\n"
"  else if(uDebug==3) col=vec3(rough);\n"
"  else if(uDebug==4) col=vec3(ao);\n"
"  else if(uDebug==5) col=vec3(emis);\n"
"  else if(uDebug==6) col=vec3(metal);\n"
"  else if(uDebug==7) col=baked+ambCube(zi,N)*ao;\n"
"  else if(uDebug==8) col=ibl;\n"
"  else if(uDebug==9) col=pre;\n"
/* what Gouraud lost and this frame put back, x8.  It is the artefact's own
   shape: where the channel is black the interpolation was already right,
   and what lights up is the lattice -- creases along the diagonals and a
   ring where a pool's edge fell between two samples. */
"  else if(uDebug==10) col=abs(slFix)*8.0;\n"
"  if(uDebug==0||uDebug==11){\n"
"    float fog=1.0-exp(-camDist*uFogDensity);\n"
"    col=mix(col,uFogCol,clamp(fog,0.0,1.0));\n"
"  }\n"
/* what col holds by now is only what the glass ADDS; all that is missing is
   how much of the sky behind it the pane BLOCKS, and for glass that is the
   same Fresnel term.  Grime blocks too: roughness above the pane's own
   0.042 is dirt, and dirt is opaque. */
"  if(surf>0.5){\n"
"    float fres=0.04+0.96*fres5(dot(N,V));\n"
"    float grime=clamp((A.a-0.042)*1.1,0.0,0.26);\n"
"    outColor=vec4(max(col,0.0),clamp(0.022+fres*0.94+grime,0.0,1.0));\n"
"    return;\n"
"  }\n"
"  outColor=vec4(max(col,0.0),1.0);\n"
"}\n";

/* ---- the post chain -------------------------------------------------
   THE SCENE IS NOT WRITTEN TO THE SCREEN.  It goes to an RGBA16F target so
   a lamp can be brighter than 1.0, and Hable's filmic curve shoulders it
   back down at the end instead of clipping it.  Without this the mid-tones
   read black and every highlight is a white hole -- which is exactly what
   an untonemapped frame looks like. */
static const char *FS_VS =
"#version 330 core\n"
"const vec2 P[3]=vec2[3](vec2(-1.0,-1.0),vec2(3.0,-1.0),vec2(-1.0,3.0));\n"
"out vec2 vUV;\n"
"void main(){ vUV=(P[gl_VertexID]+1.0)*0.5; gl_Position=vec4(P[gl_VertexID],0.0,1.0); }\n";

static const char *BRIGHT_FS =
"#version 330 core\n"
"in vec2 vUV; uniform sampler2D uSrc; uniform float uThresh;\n"
"out vec4 outColor;\n"
"void main(){\n"
"  vec3 c=texture(uSrc,vUV).rgb;\n"
"  float b=max(max(c.r,c.g),c.b);\n"
"  outColor=vec4(c*max(0.0,b-uThresh)/max(b,1e-4),1.0);\n"
"}\n";

static const char *BLUR_FS =
"#version 330 core\n"
"in vec2 vUV; uniform sampler2D uSrc; uniform vec2 uDir;\n"
"out vec4 outColor;\n"
"void main(){\n"
"  float w[5]=float[5](0.2270,0.1946,0.1216,0.0540,0.0162);\n"
"  vec3 c=texture(uSrc,vUV).rgb*w[0];\n"
"  for(int i=1;i<5;i++){\n"
"    c+=texture(uSrc,vUV+uDir*float(i)).rgb*w[i];\n"
"    c+=texture(uSrc,vUV-uDir*float(i)).rgb*w[i];\n"
"  }\n"
"  outColor=vec4(c,1.0);\n"
"}\n";

static const char *COMP_FS =
"#version 330 core\n"
"in vec2 vUV;\n"
"uniform sampler2D uScene,uBloom;\n"
"uniform float uBloomK,uTime,uVig,uGrain,uExposure,uCA;\n"
"uniform vec2 uRes;\n"
"out vec4 outColor;\n"
/* Hable's filmic curve: shoulders the highlights off instead of clipping
   them, which is what let Lost Coast keep a bright lamp and a dark corner
   in the same frame */
"vec3 hable(vec3 x){\n"
"  const float A=0.15,B=0.50,C=0.10,D=0.20,E=0.02,F=0.30;\n"
"  return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;\n"
"}\n"
"void main(){\n"
/* LATERAL CHROMATIC ABERRATION, AND IT HAS TO BE r^2 OR IT IS A BUG.
   A lens brings the three wavelengths to focus at slightly different
   distances from the axis, so the split is ZERO in the middle of the frame
   and grows toward the corners -- an even split across the whole image is
   not aberration, it is a misaligned sensor, and it reads as one.  Red
   short, blue long, green on the axis, because that is the order glass
   bends them.  The offset is in UV, so it scales with the frame the way a
   lens does rather than being a fixed number of pixels.
   This is done on the SCENE, before the tonemap, because the aberration
   happens in the glass in front of the sensor and not after it. */
"  vec2 ca=(vUV-0.5)*(dot(vUV-0.5,vUV-0.5)*uCA);\n"
"  vec3 c;\n"
"  c.r=texture(uScene,vUV-ca).r+texture(uBloom,vUV-ca).r*uBloomK;\n"
"  c.g=texture(uScene,vUV   ).g+texture(uBloom,vUV   ).g*uBloomK;\n"
"  c.b=texture(uScene,vUV+ca).b+texture(uBloom,vUV+ca).b*uBloomK;\n"
"  if(uVig>0.0){\n"
"    c=hable(c*uExposure)/hable(vec3(11.2));\n"
"    float l=dot(c,vec3(0.299,0.587,0.114));\n"        /* split tone */
"    c*=mix(vec3(0.86,0.97,1.07),vec3(1.07,1.00,0.89),smoothstep(0.08,0.62,l));\n"
"  }\n"
/* THE EMERGENCY LIGHTING WAS A GRADE AND IS NOW LIGHT.  Three instructions
   here drove the whole frame's hue to red from sink stage one -- cheap,
   cross-fadeable, and it read as a filter laid over a photograph rather
   than as a ship losing power, which is what it is.  What replaced it is
   in bake.c: a share of the lamps are baked into switchable banks, and
   the ship goes dark a bank at a time with two of them stuttering and one
   of them blinking red.  Nothing is graded; the lights are just off. */
"  float d=length(vUV-0.5)*2.0;\n"
"  c*=1.0-smoothstep(0.62,1.42,d)*uVig;\n"
"  float g=fract(sin(dot(vUV*uRes+uTime*13.7,vec2(12.9898,78.233)))*43758.5453);\n"
"  c+=(g-0.5)*uGrain;\n"
"  outColor=vec4(clamp(c,0.0,1.0),1.0);\n"
"}\n";

typedef struct { GLuint tex,fbo; int w,h; } Target;
static Target sceneRT, brightRT, blurRT;
static GLuint depthRB, msColorRB, msFBO;
static int rtW,rtH, msSamples;
static GLuint pBright,pBlur,pComp,emptyVAO;
static struct { GLint uSrc,uThresh; } UB;
static struct { GLint uSrc,uDir; } UL;
static struct { GLint uScene,uBloom,uBloomK,uTime,uVig,uGrain,uRes,uExposure,uCA; } UC;
int R_BLOOM=1;

static void makeTarget(Target *t,int w,int h){
  glGenTextures(1,&t->tex);
  glBindTexture(GL_TEXTURE_2D,t->tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_HALF_FLOAT,0);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1,&t->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER,t->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->tex,0);
  t->w=w; t->h=h;
}
static void killTarget(Target *t){
  if(t->tex) glDeleteTextures(1,&t->tex);
  if(t->fbo) glDeleteFramebuffers(1,&t->fbo);
  t->tex=t->fbo=0;
}

/* MSAA the browser never had: WebGL2's antialias flag only touches the
   default framebuffer, and the scene has not been drawn there since the HDR
   target went in.  A multisampled renderbuffer resolved with one blit costs
   almost nothing and takes the stair-steps off every chamfer. */
static void resizeTargets(int w,int h){
  if(w==rtW&&h==rtH) return;
  rtW=w; rtH=h;
  killTarget(&sceneRT); killTarget(&brightRT); killTarget(&blurRT);
  if(depthRB){ glDeleteRenderbuffers(1,&depthRB); depthRB=0; }
  if(msColorRB){ glDeleteRenderbuffers(1,&msColorRB); msColorRB=0; }
  if(msFBO){ glDeleteFramebuffers(1,&msFBO); msFBO=0; }

  makeTarget(&sceneRT,w,h);

  if(msSamples>1){
    glGenFramebuffers(1,&msFBO);
    glBindFramebuffer(GL_FRAMEBUFFER,msFBO);
    glGenRenderbuffers(1,&msColorRB);
    glBindRenderbuffer(GL_RENDERBUFFER,msColorRB);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,msSamples,GL_RGBA16F,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,msColorRB);
    glGenRenderbuffers(1,&depthRB);
    glBindRenderbuffer(GL_RENDERBUFFER,depthRB);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,msSamples,GL_DEPTH_COMPONENT24,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depthRB);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
      platLog("render msaa target incomplete; falling back to 1x");
      glDeleteFramebuffers(1,&msFBO); msFBO=0;
      glDeleteRenderbuffers(1,&msColorRB); msColorRB=0;
      glDeleteRenderbuffers(1,&depthRB); depthRB=0;
      msSamples=1;
    }
  }
  if(msSamples<=1){
    glGenRenderbuffers(1,&depthRB);
    glBindRenderbuffer(GL_RENDERBUFFER,depthRB);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
    glBindFramebuffer(GL_FRAMEBUFFER,sceneRT.fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depthRB);
  }
  int hw=w>>1, hh=h>>1; if(hw<1)hw=1; if(hh<1)hh=1;
  makeTarget(&brightRT,hw,hh);
  makeTarget(&blurRT,hw,hh);
  glBindFramebuffer(GL_FRAMEBUFFER,0);
}

static void fullscreen(void){
  glBindVertexArray(emptyVAO);
  glDrawArrays(GL_TRIANGLES,0,3);
}

/* ---- the split-sum DFG table ----------------------------------------
   THE ANALYTIC FIT WAS NOT GOOD ENOUGH, SO THE TABLE IS INTEGRATED INSTEAD.
   The usual dodge for the second half of Karis's split sum is a four-
   constant polynomial, and it exists to save a texture fetch on a phone.
   Checked against the real integral here it is off by 0.18 on the scale
   term at grazing angles and by 0.16 at normal incidence for a polished
   surface -- a sixth of the reflection missing exactly where a steel panel
   is supposed to be brightest.  So the ship Monte-Carlos the integral at
   load, 512 samples over a 32x32 grid, and pays a 16 KB texture for a
   number that is right.  Zero bytes on disk: it is a recipe, like
   everything else here. */
#define DFGN 32
static float radInv(unsigned bits){
  bits=(bits<<16)|(bits>>16);
  bits=((bits&0x55555555u)<<1)|((bits&0xAAAAAAAAu)>>1);
  bits=((bits&0x33333333u)<<2)|((bits&0xCCCCCCCCu)>>2);
  bits=((bits&0x0F0F0F0Fu)<<4)|((bits&0xF0F0F0F0u)>>4);
  bits=((bits&0x00FF00FFu)<<8)|((bits&0xFF00FF00u)>>8);
  return (float)(bits*2.3283064365386963e-10);
}
static GLuint makeDFG(void){
  const int NS=512;
  float *px=(float*)malloc(sizeof(float)*DFGN*DFGN*4);
  for(int ri=0;ri<DFGN;ri++){
    float rough=(ri+0.5f)/DFGN;
    float a=rough*rough, k=a*0.5f;              /* Schlick-GGX, the IBL form */
    for(int vi=0;vi<DFGN;vi++){
      float nov=(vi+0.5f)/DFGN;
      float vx=sqrtf(1.f-nov*nov), vz=nov;
      float A=0,B=0;
      for(int i=0;i<NS;i++){
        float u1=(i+0.5f)/NS, u2=radInv((unsigned)i);
        float phi=6.2831853f*u1;
        float ct=sqrtf((1.f-u2)/(1.f+(a*a-1.f)*u2));
        float st=sqrtf(1.f-ct*ct>0?1.f-ct*ct:0.f);
        float hx=st*cosf(phi), hz=ct;   /* V lies in xz: hy drops out */
        float voh=vx*hx+vz*hz;
        float lz=2.f*voh*hz-vz;
        if(lz<=0) continue;
        float G=(lz/(lz*(1.f-k)+k))*(nov/(nov*(1.f-k)+k));
        float gv=G*(voh>0?voh:0)/(hz*nov>1e-7f?hz*nov:1e-7f);
        float o=1.f-(voh>0?voh:0), fc=o*o*o*o*o;
        A+=(1.f-fc)*gv; B+=fc*gv;
      }
      float *o=px+((size_t)ri*DFGN+vi)*4;
      o[0]=A/NS; o[1]=B/NS; o[2]=0; o[3]=1;
    }
  }
  GLuint t; glGenTextures(1,&t);
  glBindTexture(GL_TEXTURE_2D,t);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,DFGN,DFGN,0,GL_RGBA,GL_FLOAT,px);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  free(px);
  return t;
}

/* the zone probes the bake traced, one array layer per (zone, roughness).
   No mips: the roughness levels ARE the chain, and letting GL build one
   would blend the traced border ring into the picture. */
static GLuint makeProbes(void){
  GLuint t; glGenTextures(1,&t);
  glBindTexture(GL_TEXTURE_2D_ARRAY,t);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGB16F,PROBE_E,PROBE_E,
               CUBE_N*PROBE_LEV,0,GL_RGB,GL_FLOAT,PROBES);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
  platLog("probe  %d layers of %dx%d uploaded",CUBE_N*PROBE_LEV,PROBE_E,PROBE_E);
  return t;
}

/* ---- textures ------------------------------------------------------- */
static GLuint makeArray(const unsigned char *data){
  GLuint t; glGenTextures(1,&t);
  glBindTexture(GL_TEXTURE_2D_ARRAY,t);
  glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,TN,TN,TL,0,
               GL_RGBA,GL_UNSIGNED_BYTE,data);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  { GLfloat mx=0; glGetFloatv(GL_MAX_TEX_ANISO,&mx);
    if(mx>1) glTexParameterf(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAX_ANISOTROPY,
                             mx>16?16:mx); }
  return t;
}

int renderInit(void){
  CUBE_N = nZone+1;
  AMBCUBE=(float*)calloc(CUBE_N*6*3,sizeof(float));

  char *fs=(char*)malloc(24000);
  _snprintf(fs,24000,SCENE_FS_FMT,
            CUBE_N*6, TL, TL, MAXD, MAXD, MAXD, MAXSL, MAXSL,
            PROBE_N, PROBE_LEV, TL-1, CUBE_N-1, TL-1, MAXSL, MAXD);
  char *vs=(char*)malloc(4000);
  _snprintf(vs,4000,SCENE_VS_FMT,MAXSL,MAXSL,MAXSL);
  pScene=glMakeProgram(vs,fs,"scene");
  free(fs); free(vs);
  if(!pScene) return 0;

#define U(n) US.n=glGetUniformLocation(pScene,#n)
  U(uProj);U(uView);U(uCamPos);U(uCamDir);U(uAlb);U(uNrm);U(uFlash);
  U(uFogDensity);U(uFogCol);U(uMinLight);U(uDebug);U(uMetal);U(uSurf);
  U(uCube);U(uStyleC);U(uDynP);U(uDynC);U(uDynR);U(uDynN);U(uProbe);U(uDFG);
  U(uSLP);U(uSLC);U(uSLN);
#undef U

  glGenVertexArrays(1,&vao);
  glGenBuffers(1,&vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);
  { const int S=VSTRIDE*4;
    const int sz[11]={3,3,2,1,1,3,3,3,3,1,1};
    int off=0;
    for(int i=0;i<11;i++){
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(i,sz[i],GL_FLOAT,GL_FALSE,S,(const void*)(size_t)off);
      off+=sz[i]*4;
    }
  }
  glBindVertexArray(0);

  pBright=glMakeProgram(FS_VS,BRIGHT_FS,"bright"); if(!pBright) return 0;
  pBlur  =glMakeProgram(FS_VS,BLUR_FS,  "blur");   if(!pBlur)   return 0;
  pComp  =glMakeProgram(FS_VS,COMP_FS,  "comp");   if(!pComp)   return 0;
  UB.uSrc=glGetUniformLocation(pBright,"uSrc");
  UB.uThresh=glGetUniformLocation(pBright,"uThresh");
  UL.uSrc=glGetUniformLocation(pBlur,"uSrc");
  UL.uDir=glGetUniformLocation(pBlur,"uDir");
#define C(n) UC.n=glGetUniformLocation(pComp,#n)
  C(uScene);C(uBloom);C(uBloomK);C(uTime);C(uVig);C(uGrain);C(uRes);C(uExposure);
  C(uCA);
#undef C
  glGenVertexArrays(1,&emptyVAO);
  { GLint s=0; glGetIntegerv(GL_MAX_SAMPLES,&s);
    msSamples = s>=4 ? 4 : (s>=2?2:1); }
  resizeTargets(platW,platH);

  albTex=makeArray(ALB_DATA);
  nrmTex=makeArray(NRM_DATA);
  probeTex=makeProbes();
  dfgTex=makeDFG();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  glEnable(GL_MULTISAMPLE);
  return 1;
}

void renderUpload(void){
  glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)nMeshVert*VSTRIDE*4,MESHV,GL_STATIC_DRAW);
  platLog("vbo    %.1f MB",nMeshVert*VSTRIDE*4/1048576.0);
}

unsigned renderProbeTex(void){ return probeTex; }
unsigned renderDFGTex(void){ return dfgTex; }

/* the ambient cube the bake solved, handed to the shader */
void renderSetCubes(const float *c){ memcpy(AMBCUBE,c,CUBE_N*6*3*sizeof(float)); }

/* WHICH TWELVE.  257 lamps will not fit in a uniform array and 250 of them
   are not in the room, so the pick is by how near the camera is to a lamp's
   sphere of influence -- dist-radius, not dist, or a big weak lamp thirty
   metres off would beat the strip two metres over your head.  A lamp whose
   bank has gone dark is skipped outright: it contributes nothing and it
   would only cost one of the twelve slots.

   Insertion sort into a twelve-slot table, once a frame, over 257 entries.
   It is 3,084 comparisons and it never showed up in -bench. */
float SL_P[MAXSL*4], SL_C[MAXSL*3];
int   SL_N;
int renderPickLamps(const float *camPos){
  float key[MAXSL]; int idx[MAXSL], n=0;
  for(int i=0;i<nLight;i++){
    const Light *L=&LIGHTS[i];
    int g=lampStyle(i);
    const float *sc = g>=0 ? STY_C+g*3 : 0;
    float lev = sc ? (sc[0]+sc[1]+sc[2])*(1.f/3.f) : 1.f;
    if(lev<0.02f) continue;                    /* the bank is out */
    float dx=L->p[0]-camPos[0], dy=L->p[1]-camPos[1], dz=L->p[2]-camPos[2];
    float k=sqrtf(dx*dx+dy*dy+dz*dz)-L->r;
/* SL_REACH is not "how far a lamp shines" -- the radius already says that.
   It is how far OUTSIDE its own sphere a lamp is still worth carrying, and
   it has to be metres rather than zero because the surface that shows the
   reflection is between the eye and the lamp: a strip fifteen metres down
   a corridor lays its highlight on deck plate the player is walking over.
   At 2 m only the fixture overhead qualified and a corridor had one glint
   in it. */
    if(k>SL_REACH) continue;
    int at=n<MAXSL?n:MAXSL;
    while(at>0 && key[at-1]>k){ if(at<MAXSL){ key[at]=key[at-1]; idx[at]=idx[at-1]; } at--; }
    if(at<MAXSL){ key[at]=k; idx[at]=i; if(n<MAXSL) n++; }
  }
  for(int s=0;s<n;s++){
    const Light *L=&LIGHTS[idx[s]];
    int g=lampStyle(idx[s]);
    const float *sc = g>=0 ? STY_C+g*3 : 0;
/* AND IT HAS TO LET GO OF ONE GRADUALLY.  The cut at SL_REACH is a step,
   and a step was survivable while these sixteen only carried a highlight:
   a glint appearing on a far plate is a glint.  They carry the diffuse
   correction below now, so the same step would be the whole light pool on
   a wall changing shape as the player walks a stride.  The last two metres
   of the reach fade the lamp out instead -- smoothstep, so the derivative
   is zero at both ends and there is no edge to see anywhere. */
    float k=key[s]-(SL_REACH-SL_FADE);
    float f=1.f;
    if(k>0.f){ f=1.f-k/SL_FADE; if(f<0.f) f=0.f; f=f*f*(3.f-2.f*f); }
    SL_P[s*4+0]=L->p[0]; SL_P[s*4+1]=L->p[1];
    SL_P[s*4+2]=L->p[2]; SL_P[s*4+3]=L->r;
    for(int c=0;c<3;c++) SL_C[s*3+c]=L->c[c]*(sc?sc[c]:1.f)*f;
  }
  for(int s=n;s<MAXSL;s++){
    SL_P[s*4+0]=SL_P[s*4+1]=SL_P[s*4+2]=0.f; SL_P[s*4+3]=1.f;
    SL_C[s*3+0]=SL_C[s*3+1]=SL_C[s*3+2]=0.f;
  }
  SL_N=n;
  return n;
}

/* THE FRAME IS FOUR CALLS, NOT ONE.  Order is the whole reason: entities
   after the opaque world and before the glass, or a body behind a pane is
   in front of it; the glass before the viewmodel, because the viewmodel
   clears depth and anything after it would be tested against nothing.  So
   the renderer offers the passes and the game says when. */
void renderBeginScene(const FrameState *F){
  resizeTargets(platW,platH);
  glBindFramebuffer(GL_FRAMEBUFFER, msFBO?msFBO:sceneRT.fbo);
  glViewport(0,0,rtW,rtH);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glClearColor(F->fogCol[0],F->fogCol[1],F->fogCol[2],1.0f);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

  glUseProgram(pScene);
  glUniformMatrix4fv(US.uProj,1,GL_FALSE,F->proj);
  glUniformMatrix4fv(US.uView,1,GL_FALSE,F->view);
  glUniform3fv(US.uCamPos,1,F->camPos);
  glUniform3fv(US.uCamDir,1,F->camDir);
  glUniform1f(US.uFlash,F->flash?1.f:0.f);
  glUniform1f(US.uFogDensity,F->fogDensity);
  glUniform1i(US.uDebug,R_DEBUG);
  glUniform1fv(US.uMetal,TL,MAT_METAL);
  { float sf[TL]; for(int i=0;i<TL;i++) sf[i]=(float)MAT_SURF[i];
    glUniform1fv(US.uSurf,TL,sf); }
  glUniform3fv(US.uCube,CUBE_N*6,AMBCUBE);
  glUniform3fv(US.uStyleC,STY_N,STY_C);
  glUniform3fv(US.uDynP,MAXD,F->dynP);
  glUniform3fv(US.uDynC,MAXD,F->dynC);
  glUniform1fv(US.uDynR,MAXD,F->dynR);
  glUniform1i(US.uDynN,F->dynN);
  { int n=renderPickLamps(F->camPos);
    glUniform4fv(US.uSLP,MAXSL,SL_P);
    glUniform3fv(US.uSLC,MAXSL,SL_C);
    glUniform1i(US.uSLN,n); }
  glUniform3fv(US.uFogCol,1,F->fogCol);
  glUniform3fv(US.uMinLight,1,F->minLight);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY,albTex);
  glUniform1i(US.uAlb,0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D_ARRAY,nrmTex);
  glUniform1i(US.uNrm,1);
  glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D_ARRAY,probeTex);
  glUniform1i(US.uProbe,2);
  glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,dfgTex);
  glUniform1i(US.uDFG,3);

  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES,0,MESH_SPLIT[0]);
  glBindVertexArray(0);
}

/* The two blended ranges, drawn after the entities so that a body standing
   behind the glass is behind the glass.  Depth test on and depth WRITE off,
   because a pane must not stop the dust and the coronas behind it from being
   drawn. */
void renderGlass(void){
  if(nMeshVert<=MESH_SPLIT[0]) return;
  glUseProgram(pScene);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY,albTex);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D_ARRAY,nrmTex);
  glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D_ARRAY,probeTex);
  glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,dfgTex);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES,MESH_SPLIT[0],MESH_SPLIT[1]-MESH_SPLIT[0]);
  glDrawArrays(GL_TRIANGLES,MESH_SPLIT[1],nMeshVert-MESH_SPLIT[1]);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

void renderEndScene(const FrameState *F){
  if(msFBO){         /* resolve the samples into the texture post reads */
    glBindFramebuffer(GL_READ_FRAMEBUFFER,msFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,sceneRT.fbo);
    glBlitFramebuffer(0,0,rtW,rtH, 0,0,rtW,rtH, GL_COLOR_BUFFER_BIT,GL_NEAREST);
  }

  glDisable(GL_DEPTH_TEST);
  int useBloom = R_BLOOM && !R_DEBUG;
  if(useBloom){
    glBindFramebuffer(GL_FRAMEBUFFER,brightRT.fbo);
    glViewport(0,0,brightRT.w,brightRT.h);
    glUseProgram(pBright);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,sceneRT.tex);
    glUniform1i(UB.uSrc,0); glUniform1f(UB.uThresh,0.80f);
    fullscreen();

    glUseProgram(pBlur);
    glBindFramebuffer(GL_FRAMEBUFFER,blurRT.fbo);
    glViewport(0,0,blurRT.w,blurRT.h);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,brightRT.tex);
    glUniform1i(UL.uSrc,0); glUniform2f(UL.uDir,1.6f/brightRT.w,0);
    fullscreen();

    glBindFramebuffer(GL_FRAMEBUFFER,brightRT.fbo);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,blurRT.tex);
    glUniform1i(UL.uSrc,0); glUniform2f(UL.uDir,0,1.6f/blurRT.h);
    fullscreen();
  }

  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glViewport(0,0,platW,platH);
  glUseProgram(pComp);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,sceneRT.tex);
  glUniform1i(UC.uScene,0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D,useBloom?brightRT.tex:blurRT.tex);
  glUniform1i(UC.uBloom,1);
  glUniform1f(UC.uBloomK,useBloom?0.85f:0.0f);
  glUniform1f(UC.uTime,F->time);
/* A THIRD OFF THE AMBIENT FLOOR TAKES THE MID-TONE WITH IT, and the answer
   to that is not to put the floor back -- it is to open the aperture, which
   lifts what the lamps are lighting and leaves the dark between them dark.
   That is the difference between a dim picture and a contrasty one. */
  glUniform1f(UC.uExposure,4.00f);
  glUniform1f(UC.uVig,R_DEBUG?0.0f:0.55f);
  glUniform1f(UC.uGrain,R_DEBUG?0.0f:0.020f);
  /* 0.0032 is about 1.3 px of red-to-blue split in the corners of a 1600
     wide frame and nothing at all in the middle.  Any more and it stops
     being a lens and starts being a fault. */
  glUniform1f(UC.uCA,R_DEBUG?0.0f:0.0032f);
  glUniform2f(UC.uRes,(float)platW,(float)platH);
  fullscreen();
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
}
