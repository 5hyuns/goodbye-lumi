/* ============================================================
   THE TORCH, AS A BEAM RATHER THAN AS A CONE
   ============================================================

   What the ship had was smoothstep(0.895,0.972,dot(-L,camDir)) times a
   near-linear falloff: a circle of even paint, the same brightness in the
   middle as at the edge of its core, with no structure of any kind.  It
   read as a spotlight in a level editor and not as something being carried.

   A hand torch is a bulb sitting in a stamped reflector behind a lens, and
   three things that assembly does are what the eye actually reads:

     the reflector is pressed in facets, not spun, so the disc has petals;
     the bulb's own holder shadows the middle of it, so there is a faint
       dark ring INSIDE the hot spot rather than outside it;
     the lens is scratched and dusty, so the whole beam carries fine grain.

   All three are functions of the direction the ray leaves the TORCH, never
   of the wall it lands on -- which is the point.  The pattern slides across
   a bulkhead as the head turns, and that sliding is the single thing that
   makes a beam read as a beam instead of as a decal stuck to the wall.

   And the falloff is inverse-square now.  1-dist/24 squared is very nearly
   linear over the distances this ship has in it: it put as much light on a
   wall eight metres off as on one three metres away, which is why every
   room was evenly grey with the torch up.  1/(1+0.09 d^2) is 0.55 at three
   metres and 0.15 at eight, and that ratio is most of the Doom 3 reading.

   ONE COPY, THREE SHADERS.  render.c's world, ent.c's entities and fx.c's
   decals all light from this torch, and a crater lit by a plain cone inside
   a wall lit by a cookie is worse than either -- and three hand-kept
   copies of one lookup drift apart with no error, which the octahedral
   probe fetch already shows.  vmw.c is deliberately NOT on it: the
   viewmodel sits 400 mm from the lens, inside the near field where the
   reflector pattern has not formed yet, and its own soft cone is right.
   ============================================================ */
#ifndef TORCH_H
#define TORCH_H

/* Every line of this macro carries its continuation, comments included --
   a comment line without one ends the #define in the middle of a shader. */
#define TORCH_GLSL \
"const float TH_TAN=0.502;\n"          /* tan of the outer half-angle */ \
"const vec3  TH_WARM=vec3(1.00,0.95,0.84);\n" \
"const vec3  TH_COOL=vec3(0.80,0.87,1.04);\n" \
/* 2.25 WAS A TORCH YOU COULD SEE BY AND NOT ONE YOU COULD SEARCH WITH. */ \
/* With the inverse-square falloff below, eight metres is 0.15 of the    */ \
/* peak, and the ship's ambient floor is 0.046 -- so the far end of a    */ \
/* hold read as the same near-black lit as unlit, and the answer the     */ \
/* player arrives at is to stop pressing F.  x2.5.  The tonemap absorbs  */ \
/* it where it is already clipping -- the hot spot inside three metres   */ \
/* -- so what actually moves is the corona and the far half of the beam. */ \
"const float TH_GAIN=5.60;\n" \
"float thHash(vec2 p){ p=fract(p*vec2(127.1,311.7)); p+=dot(p,p+34.53);\n" \
"  return fract(p.x*p.y); }\n" \
"float thNoise(vec2 p){ vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);\n" \
"  return mix(mix(thHash(i),           thHash(i+vec2(1,0)),f.x),\n" \
"             mix(thHash(i+vec2(0,1)), thHash(i+vec2(1,1)),f.x),f.y); }\n" \
/* rgb: the radiance the torch lands on P, cookie and falloff already in */ \
/* it.  a: the same as a scalar, so a caller can skip the lobe at zero.  */ \
"vec4 torchBeam(vec3 P,vec3 camPos,vec3 camDir){\n" \
"  vec3 B=P-camPos;\n" \
"  float dist=length(B);\n" \
"  if(dist<1e-4) return vec4(0.0);\n" \
"  B/=dist;\n" \
"  vec3 fwd=normalize(camDir);\n" \
"  float c=dot(B,fwd);\n" \
"  if(c<=0.80) return vec4(0.0);\n"     /* outside every part of the cone */ \
/* the beam's own plane.  Taking right from world up rather than from the */ \
/* camera's roll is deliberate: a torch hangs off the hand, and a cookie  */ \
/* that stays level while the head rolls is what a carried light does.    */ \
"  vec3 rt=cross(fwd,vec3(0.0,1.0,0.0));\n" \
"  float rl=length(rt);\n" \
"  rt = rl>1e-3 ? rt/rl : vec3(1.0,0.0,0.0);\n" \
"  vec3 up=cross(rt,fwd);\n" \
"  vec2 q=vec2(dot(B,rt),dot(B,up))/c;\n" /* length(q) is tan(off-axis) */ \
"  float r=length(q)/TH_TAN;\n" \
/* A BEAM HAS AN EDGE.  smoothstep(1.04,0.34,r) ramps across the whole    */ \
/* disc, which is a glow and not a beam; a reflector throws a flat field  */ \
/* out to most of its aperture and then falls off in a hurry.            */ \
"  float body=1.0-smoothstep(0.74,1.02,r);\n" \
"  if(body<=0.0) return vec4(0.0);\n" \
"  float core=exp(-r*r*2.60);\n" \
/* the holder's shadow, sitting INSIDE the disc rather than around it.    */ \
/* NOT pow((r-0.58)*8.5,2.0): that base goes negative inside the ring and */ \
/* GLSL leaves pow() undefined there -- the same trap that put pepper on  */ \
/* every torch-lit wall through Schlick's pow(1-x,5).  A square is a      */ \
/* multiply.                                                             */ \
"  float rr=(r-0.58)*8.5;\n" \
"  float ring=1.0-0.24*exp(-rr*rr);\n" \
/* nine facets, and a finer set of striations off the same stamping.     */ \
/* Both live in the corona: a reflector's structure shows where the core */ \
/* has fallen off enough to let it, never in the hot spot.               */ \
"  float th=atan(q.y,q.x+1e-6);\n" \
"  float shp=smoothstep(0.20,0.86,r);\n" \
"  float petal=1.0+(0.13*cos(th*9.0+1.7)+0.05*cos(th*23.0-0.6))*shp;\n" \
/* the dust on the lens: THREE octaves and fine.  Two coarse ones read as */ \
/* cloud on the wall rather than as dirt on the glass -- the beam looked  */ \
/* stained.  On the BEAM's coordinates, so it travels with the beam.     */ \
"  float g=thNoise(q*118.0)*0.55+thNoise(q*257.0+7.3)*0.30\n" \
"         +thNoise(q*545.0-3.1)*0.15;\n" \
"  float cookie=(0.30+0.86*core)*body*ring*petal*(1.0+0.26*(g-0.5));\n" \
"  float att=1.0/(1.0+0.090*dist*dist);\n" \
"  float u=dist*(1.0/26.0); u*=u; u*=u;\n" /* ... and a finite reach */ \
"  att*=clamp(1.0-u,0.0,1.0);\n" \
/* warm in the middle and cool at the rim -- the reflector's own coating  */ \
/* does it, and it is the one place this ship is allowed to be blue       */ \
"  vec3 col=mix(TH_WARM,TH_COOL,smoothstep(0.30,1.0,r));\n" \
"  float k=cookie*att*TH_GAIN;\n" \
"  return vec4(col*k,k);\n" \
"}\n"

#endif
