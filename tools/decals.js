/* ============================================================
   IMPACT CRATERS
   Spliced over the decal system by tools/decals.py.

   WHAT WAS WRONG.  Every decal in the game -- blood, scorch and bullet
   mark alike -- went through one shader that draws a blood splat: a pool,
   six satellites and a run of drips, flat, unlit, in whatever colour the
   caller passed.  A bullet mark was that splat in near-black, which is why
   it read as a sticker laid on the wall rather than a hole in it.  And it
   was lifted off the surface by a fixed 14 mm, which is under the depth
   buffer's own resolution at any distance, so the two surfaces quantised
   to the same value and fought frame to frame.

   WHAT IT IS NOW.  Kind 1 is a CRATER, and the whole of it is one height
   profile: a floor, a wall, and a lip where the metal was pushed back out.
   Its gradient is the normal, and the normal is lit -- by the light that
   was actually on that wall when the round arrived (sampled once, on the
   CPU, with the same tracer the lightmap used) and by the torch, live.
   That is what makes it a dent instead of a decal: the near side of the
   lip catches the flashlight and the floor goes dark, and both change as
   you walk past.

   The centre also arrives HOT and cools over about a second and a half,
   because that is the one thing a hole in steel does that a hole in
   anything else does not.

   Kind 0 is the old splat, untouched, still doing blood.
   ============================================================ */

/*==SHADERS==*/
const DECAL_VS=`#version 300 es
layout(location=0) in vec2 aQuad;
layout(location=1) in vec4 aP;    // xyz centre, w size
layout(location=2) in vec4 aN;    // xyz surface normal, w rotation
layout(location=3) in vec4 aC;    // rgb colour, a strength
layout(location=4) in vec4 aL;    // rgb the light that was on it, a age
layout(location=5) in vec4 aD;    // xyz that light's direction, w kind
uniform mat4 uProj,uView;
uniform vec3 uCamPos;
out vec2 vUV; out vec4 vC,vL; out vec3 vN,vTX,vTY,vP,vLD;
out float vSeed,vKind;
void main(){
  vec3 n=normalize(aN.xyz);
  vec3 t=abs(n.y)>0.9?vec3(1,0,0):vec3(0,1,0);
  vec3 tx=normalize(cross(t,n)), ty=cross(n,tx);
  float c=cos(aN.w), s=sin(aN.w);
  vec2 q=vec2(aQuad.x*c-aQuad.y*s, aQuad.x*s+aQuad.y*c);
  // TWO SEPARATE PROBLEMS, TWO SEPARATE FIXES.  The plate does want to
  // stand slightly proud of the wall -- 10 mm, so its lip has somewhere to
  // be -- but no world offset can cure the shimmer, because the fight is in
  // the DEPTH BUFFER and its resolution collapses with range: 14 mm is a
  // mile at 2 m and inside one step at 30 m.  So the raise stays constant
  // and the depth is biased in clip space instead, which is a fixed number
  // of buffer steps at every distance and moves nothing on screen.
  vec3 wp=aP.xyz+n*0.010+(tx*q.x+ty*q.y)*aP.w;
  // the tangents, turned by the same rotation, so a gradient taken in vUV
  // lands the right way round in the world
  vTX=tx*c+ty*s; vTY=ty*c-tx*s;
  vUV=aQuad; vC=aC; vL=aL; vLD=aD.xyz; vKind=aD.w; vSeed=aN.w;
  vN=n; vP=wp;
  gl_Position=uProj*uView*vec4(wp,1.0);
  gl_Position.z-=0.0022*gl_Position.w;
}`;

const DECAL_FS=`#version 300 es
precision highp float;
in vec2 vUV; in vec4 vC,vL; in vec3 vN,vTX,vTY,vP,vLD;
in float vSeed,vKind;
uniform vec3 uCamPos,uCamDir,uFogCol,uAmb;
uniform float uFlash,uFogDensity;
// how deep the crater is, as a fraction of its own radius.  0.30 puts the
// steepest part of the wall near 45 degrees, which is what a jacketed round
// leaves in plate; past about 0.5 it stops being a hole and starts being a
// funnel.
const float DENT=0.38;
out vec4 outColor;
float h21(vec2 p){ return fract(sin(dot(p,vec2(269.5,183.3)))*43758.5453); }

// THE PROFILE IS THE WHOLE PART.  Height above the plate, in quad units:
// a floor punched below it, a wall, and a lip of metal pushed back out
// around the rim.  Everything else here is read off this one function.
float crater(float a){
  float flr=1.0-smoothstep(0.04,0.40,a);
  float lip=smoothstep(0.28,0.45,a)*(1.0-smoothstep(0.45,0.68,a));
  return lip*0.42-flr;
}

void main(){
  float r=length(vUV);
  vec3 col; float a;

  if(vKind>0.5){
    // the rim is torn, not turned -- but SMOOTHLY torn.  Hashing the
    // radius per angular sector made a cog, and worse, made the hot core a
    // starburst, because everything downstream is measured in r/wob.
    float ang=atan(vUV.y,vUV.x);
    float wob=1.0+0.10*sin(ang*3.0+vSeed*6.3)+0.055*sin(ang*5.0-vSeed*3.1);
    float e=0.022;
    float h0=crater(r/wob);
    float hx=crater(length(vUV+vec2(e,0.0))/wob);
    float hy=crater(length(vUV+vec2(0.0,e))/wob);
    vec3 N=normalize(vN-(vTX*(hx-h0)+vTY*(hy-h0))*(DENT/e));

    float aa=r/wob;
    float flr=1.0-smoothstep(0.04,0.40,aa);
    float lip=smoothstep(0.28,0.45,aa)*(1.0-smoothstep(0.45,0.68,aa));

    // torn metal on the lip, scorch on the floor, soot fading out past it
    vec3 alb=mix(vC.rgb,vec3(0.150,0.143,0.134),clamp(lip*1.4,0.0,1.0));
    alb*=1.0-0.55*flr;

    vec3 V=normalize(uCamPos-vP);
    vec3 L=normalize(vLD+vec3(1e-5));
    float nl=max(dot(N,L),0.0);
    vec3 lit=vL.rgb*(nl*0.94+0.06)+uAmb;
    if(uFlash>0.5){
      vec3 d=uCamPos-vP; float dist=length(d); vec3 Lf=d/max(dist,1e-4);
      float cone=smoothstep(0.895,0.972,dot(-Lf,normalize(uCamDir)));
      float att=max(0.0,1.0-dist/24.0); att*=att*cone*1.75;
      lit+=vec3(1.0,0.95,0.84)*(max(dot(N,Lf),0.0)*0.92+0.08)*att;
      L=normalize(L+Lf*att*2.0);
    }
    float sp=pow(max(dot(N,normalize(L+V)),0.0),40.0)*lip*1.5;
    col=alb*lit+lit*sp;

    // ---- AND IT ARRIVES HOT.  White at the instant of the hit, down
    // through yellow and orange into a deep red, gone in about a second
    // and a half.  Only the floor of the crater glows; the lip is already
    // cold because it is the metal that got out of the way.
    float heat=exp(-vL.a*2.3);
    float core=(1.0-smoothstep(0.02,0.30,r))*heat;
    col+=vec3(1.30,0.30,0.045)*core*core
        +vec3(1.45,1.15,0.62)*pow(core,4.0)*1.4;

    // COVERAGE, AND THIS IS WHAT MADE IT A STICKER.  One opaque disc out
    // to 74% of the quad is a black circle painted on the wall, whatever is
    // drawn inside it.  The hole itself is opaque; everything past its lip
    // is SOOT, which is a veil that thins to nothing -- so the mark ends by
    // fading into the plate instead of by having an edge.
    float body=1.0-smoothstep(0.62,0.82,aa);
    float soot=(1.0-smoothstep(0.10,1.00,r));
    a=max(body,soot*soot*0.55)*vC.a;
  }else{
    // ---- the old splat, unchanged: a pool, six satellites and a drip ----
    float d=r*1.22;
    float m=1.0-smoothstep(0.42,0.80,d);
    for(int i=0;i<6;i++){
      float fi=float(i);
      float ang2=h21(vec2(vSeed,fi))*6.2831;
      float rr=0.30+h21(vec2(fi,vSeed))*0.62;
      float sz=0.10+h21(vec2(vSeed*1.7,fi*3.1))*0.20;
      vec2 c=vec2(cos(ang2),sin(ang2))*rr;
      m=max(m,(1.0-smoothstep(sz*0.45,sz,length(vUV-c)))*0.94);
    }
    float dr=1.0-smoothstep(0.05,0.13,abs(vUV.x-(h21(vec2(vSeed,9.0))-0.5)*0.7));
    m=max(m,dr*(1.0-smoothstep(0.1,0.95,vUV.y))*step(0.0,vUV.y)*0.55);
    a=clamp(m,0.0,1.0)*vC.a;
    col=vC.rgb*(0.62+0.38*(1.0-smoothstep(0.0,0.55,d)));
  }

  if(a<0.01) discard;
  float fog=1.0-exp(-length(uCamPos-vP)*uFogDensity);
  col=mix(col,uFogCol,clamp(fog,0.0,1.0));
  outColor=vec4(max(col,0.0)*a,a);
}`;
/*==END==*/

/*==CPU==*/
const DECALS=[];
const MAX_HOLE=56;              // holes kept before the oldest gives way
function addDecal(x,y,z,nx,ny,nz,size,r,g,b,a,kind,bind){
  if(DECALS.length>=MAX_DECAL) DECALS.shift();
  const D={x,y,z,nx,ny,nz,s:size,r,g,b,a,a0:a,t:0,life:150,fw:14,
           rot:Math.random()*6.28,kind:kind||0,
           lit:new Float32Array(3),ldir:new Float32Array([0,1,0])};
  if(D.kind===1){
    // what was actually on that wall when the round arrived, sampled once
    // with the same shadowed tracer the lightmap used.  A crater lit by a
    // guess is a crater that floats.
    sampleLight(x+nx*0.06,y+ny*0.06,z+nz*0.06,D.lit,D.ldir);
    D.life=110;
    if(bind){ D.dr=bind.d; D.side=bind.s; D.off=bind.off; D.pp=bind.pp; }
    // A CAP THAT FADES RATHER THAN POPS.  Past MAX_HOLE the oldest hole is
    // told to close over three quarters of a second; splicing it out from
    // under the player is the one thing that would be noticed.
    let holes=0;
    for(let i=0;i<DECALS.length;i++) if(DECALS[i].kind===1) holes++;
    if(holes>=MAX_HOLE){
      for(let i=0;i<DECALS.length;i++){
        const O=DECALS[i];
        if(O.kind===1&&O.life-O.t>0.75){ O.life=O.t+0.75; O.fw=0.75; break; }
      }
    }
  }
  DECALS.push(D);
}
function fillDecals(dt){
  let n=0;
  for(let i=DECALS.length-1;i>=0;i--){
    const D=DECALS[i]; D.t+=dt;
    if(D.t>D.life){ DECALS.splice(i,1); }
  }
  for(let i=0;i<DECALS.length&&n<MAX_DECAL;i++){
    const D=DECALS[i], d=decalData;
    let x=D.x, y=D.y, z=D.z;
    if(D.dr){
      // IT WAS SHOT INTO A DOOR, SO IT RIDES THE DOOR.  The mark is held at
      // a fixed distance from the leaf's leading edge -- the edge is the
      // part the eye tracks -- and is gone once the leaf has retracted past
      // it, which is the moment it would otherwise hang in mid-air.
      const R=D.dr, lw=R.hw*(1-R.e);
      if(R.e>0.985||D.off>lw-0.015) continue;
      const q=D.side*(R.hw-lw+D.off);
      if(R.ax){ x=R.x+q; z=R.z+D.pp; } else { z=R.z+q; x=R.x+D.pp; }
    }
    const o=n*DECAL_STRIDE;
    const fade=D.t>D.life-D.fw?Math.max(0,(D.life-D.t)/D.fw):1;
    d[o]=x; d[o+1]=y; d[o+2]=z; d[o+3]=D.s;
    d[o+4]=D.nx; d[o+5]=D.ny; d[o+6]=D.nz; d[o+7]=D.rot;
    d[o+8]=D.r; d[o+9]=D.g; d[o+10]=D.b; d[o+11]=D.a0*fade;
    d[o+12]=D.lit[0]; d[o+13]=D.lit[1]; d[o+14]=D.lit[2]; d[o+15]=D.t;
    d[o+16]=D.ldir[0]; d[o+17]=D.ldir[1]; d[o+18]=D.ldir[2]; d[o+19]=D.kind;
    n++;
  }
  return n;
}
/*==END==*/
