/* ============================================================
   AUTOMATIC BULKHEAD DOORS
   Spliced into the engine by tools/doors.py, after tools/weapons.py
   and before tools/deckmap.py, so the game and the map both get them.

   THE PLAN ALREADY DREW THEM.  deckplan.py has 34 door rects and
   shipdetail.py has built their frames since the first pass -- the
   head at 2.62 m, a jamb either side, a hazard strip on the deck --
   with the comment "the leaf is dynamic and comes later".  This is
   later.  SHIP_DOORS is the list it left behind: {x,z,w,d,ax}, where
   ax=1 means the opening runs along X and ax=0 along Z, and w/d are
   the frame's own footprint.  deckmap.py emits the same list for the
   module grid, so one runtime serves both files.

   WHY A LEAF THAT SHRINKS AND A NOSE THAT SLIDES.  A bi-parting door
   1.8 m per leaf needs 1.8 m of wall pocket to vanish into, and there
   is no pocket -- the jamb is 600 mm, exactly the deck splay it stands
   on.  So the leaf is drawn the way the engine's one hand-built blast
   door already draws it: its outer edge is pinned at the jamb and its
   width goes to zero.  The cheat is only invisible if nothing on the
   leaf reveals the scaling, which is why every feature on the BODY is
   HORIZONTAL -- bands, a rail, a kick plate, a hazard stripe, all of
   which stretch along their own length and show nothing -- and every
   feature that is vertical (the nose rib, the interlocking teeth, the
   lit edge) is drawn at a FIXED size and translated.  The eye tracks
   the nose, so the door reads as sliding into the wall.

   AND IT IS A MOTION SENSOR, NOT A BUTTON.  It opens for the player
   and for anything else alive in front of it, which is what keeps the
   zombies' pathing honest: the doors are not in navBlock, so the AI
   walks its usual route and the door is open by the time it arrives.
   ============================================================ */
const ADB=[];            // closed leaves as solid boxes -- boxSolid, dynTrace
const ADBI=[];           // ... and which door and which leaf each one is
const AD=[];             // one record per doorway
let adReady=0, adDynT=0;

const AD_HEIGHT = 2.62;  // the clear height shipdetail.py leaves under the head
const AD_PERP   = 3.30;  // how far off the face the sensor reaches
const AD_PAD    = 0.80;  // ... and how far past the jamb
const AD_DWELL  = 1.10;  // it stays open this long after you step out
const AD_OPEN_S = 2.35;  // 1/s -- 0.43 s to open ...
const AD_SHUT_S = 1.45;  //        ... and 0.69 s to close, like every servo
const AD_DRAW   = 34.0;  // drawn within this
const AD_FINE   = 16.0;  // detailed within this
const AD_SOLID  = 32.0;  // and solid within this

/* Machined steel and the three accent roles, and nothing else coloured.
   THE METAL ONES ARE AUTHORED BRIGHT ON PURPOSE.  ENT_FS's MACHINED kind
   spends 88% of a conductor's albedo on a tinted specular and leaves the
   rest as diffuse, so 0.48 steel arrives on screen at about 0.14 and only
   lights up where it catches a lamp -- which is the entire look.  Read the
   second number as "how much of this is metal", not as a finish. */
const AD_STEEL=[0.545,0.531,0.500], MET_STEEL=0.90,
      AD_TRIM =[0.735,0.712,0.658], MET_TRIM =0.95,
      AD_GAP  =[0.115,0.117,0.124], MET_GAP  =0.60,
      AD_KICK =[0.360,0.352,0.334], MET_KICK =0.90,
      AD_OCHRE=[0.360,0.252,0.033], MET_OCHRE=0.10,
      AD_AMBER=[1.00,0.60,0.15],    AD_CYAN  =[0.30,0.86,1.10];

function adInit(){
  if(adReady) return;
  adReady=1;
  if(typeof SHIP_DOORS==='undefined') return;
  for(let i=0;i<SHIP_DOORS.length;i++){
    const s=SHIP_DOORS[i], ax=s.ax|0;
    const span=ax?s.w:s.d, th=ax?s.d:s.w;
    if(span<0.8) continue;
    AD.push({x:s.x, z:s.z, ax:ax, hw:span*0.5, th:th, H:s.h||AD_HEIGHT,
             t:0, e:0, hold:0, was:0, moved:0,
             zi:zoneAt(s.x,s.z), litT:Math.random()*0.31,
             lit:new Float32Array(3), ldir:new Float32Array([0,1,0])});
  }
}

/* ---- the sensor.  A rectangle that hugs the opening, not a circle:
   a 3.6 m door has to answer you at its edge as well as at its middle. */
function adSensed(D){
  let a=D.ax?P.x-D.x:P.z-D.z, p=D.ax?P.z-D.z:P.x-D.x;
  if(a<0)a=-a; if(p<0)p=-p;
  if(p<AD_PERP&&a<D.hw+AD_PAD) return 1;
  for(let i=0;i<ZOM.length;i++){
    const z=ZOM[i];
    if(z.dead) continue;
    a=D.ax?z.x-D.x:z.z-D.z; p=D.ax?z.z-D.z:z.x-D.x;
    if(a<0)a=-a; if(p<0)p=-p;
    if(p<AD_PERP*0.85&&a<D.hw+AD_PAD) return 1;
  }
  return 0;
}

function updateAutoDoors(dt){
  adInit();
  ADB.length=0; ADBI.length=0;
  if(!AD.length) return;
  let lead=null, leadD=1e9;
  for(let i=0;i<AD.length;i++){
    const D=AD[i];
    const ddx=D.x-P.x, ddz=D.z-P.z, d2=ddx*ddx+ddz*ddz;

    // ---- the servo -----------------------------------------------------
    let want=0;
    if(d2<(AD_PERP+D.hw+4)*(AD_PERP+D.hw+4)&&adSensed(D)){ want=1; D.hold=AD_DWELL; }
    else if(D.hold>0){ D.hold-=dt; want=1; }
    const prev=D.t;
    D.t+=want?dt*AD_OPEN_S:-dt*AD_SHUT_S;
    if(D.t<0)D.t=0; else if(D.t>1)D.t=1;
    D.e=D.t*D.t*(3-2*D.t);
    D.moved=(D.t!==prev)?1:0;

    // ---- the noise it makes, once per stroke ---------------------------
    if(D.moved&&!D.was&&d2<44*44) sfxSlide(D.x,1.5,D.z,want);
    if(!D.moved&&D.was&&D.t<=0&&d2<44*44) sfxLatch(D.x,1.2,D.z);
    D.was=D.moved;
    if(D.moved&&d2<leadD){ leadD=d2; lead=D; }

    // ---- what it is lit by, on its own phase ---------------------------
    if(d2<AD_DRAW*AD_DRAW){
      D.litT-=dt;
      if(D.litT<=0){ D.litT=0.31; sampleLight(D.x,1.55,D.z,D.lit,D.ldir);
        D.lit[0]*=0.72; D.lit[1]*=0.72; D.lit[2]*=0.72; }
    }

    // ---- and what it blocks --------------------------------------------
    if(D.e>0.985||d2>AD_SOLID*AD_SOLID) continue;
    const lw=D.hw*(1-D.e);
    if(lw<0.05) continue;
    const q=D.th*0.30;
    if(D.ax){
      ADB.push([D.x-D.hw,0,D.z-q, D.x-D.hw+lw,D.H,D.z+q]);
      ADB.push([D.x+D.hw-lw,0,D.z-q, D.x+D.hw,D.H,D.z+q]);
    }else{
      ADB.push([D.x-q,0,D.z-D.hw, D.x+q,D.H,D.z-D.hw+lw]);
      ADB.push([D.x-q,0,D.z+D.hw-lw, D.x+q,D.H,D.z+D.hw]);
    }
    ADBI.push({d:D,s:-1},{d:D,s:1});
  }
  // A LIT THING HAS TO BE A LAMP.  The strip on a moving leaf is baked into
  // nothing, so the nearest door that is actually running borrows one of the
  // eight unbaked slots for as long as it takes to run.
  adDynT-=dt;
  if(lead&&adDynT<=0&&leadD<20*20){
    adDynT=0.05;
    const k=0.35+0.65*lead.e;
    addDynLight(lead.x,1.62,lead.z, AD_CYAN[0]*0.62*k,AD_CYAN[1]*0.62*k,
                AD_CYAN[2]*0.62*k, 6.2, 0.10);
  }
}

/* ---- WHICH LEAF A SHOT LANDED IN, AND WHERE ON IT.
   dynTrace runs DYNB first and ADB after it, so an index past DYNB.length
   is one of these boxes and ADBI says whose.  The offset is measured from
   the leaf's LEADING EDGE, because that is the edge that visibly travels:
   hold a mark at a fixed distance from it and the damage slides into the
   wall with the door instead of hanging in the doorway. */
function adBindAt(h){
  if(h.kind!=='dyn') return null;
  const k=h.bi-DYNB.length;
  if(k<0||k>=ADBI.length) return null;
  const B=ADBI[k], D=B.d, lw=D.hw*(1-D.e);
  const a =D.ax?HITP[0]-D.x:HITP[2]-D.z;
  const pp=D.ax?HITP[2]-D.z:HITP[0]-D.x;
  return {d:D, s:B.s, off:Math.max(0.012,B.s*a-(D.hw-lw)), pp:pp};
}

/* ---- a box in the door's own frame: a along the opening, p through it -- */
function adBox(D,a,y,p,sa,sy,sp,c,em,fx,prim){
  if(D.ax) pushBox(D.x+a,y,D.z+p, sa,sy,sp, c[0],c[1],c[2],em,fx,prim);
  else     pushBox(D.x+p,y,D.z+a, sp,sy,sa, c[0],c[1],c[2],em,fx,prim);
}

function drawAutoDoors(){
  for(let i=0;i<AD.length;i++){
    const D=AD[i];
    const ddx=D.x-P.x, ddz=D.z-P.z, d2=ddx*ddx+ddz*ddz;
    if(d2>AD_DRAW*AD_DRAW) continue;
    const fine=d2<AD_FINE*AD_FINE;
    const H=D.H, th=D.th, hw=D.hw, e=D.e;
    // one fx, rewritten between groups: sk 3 is MACHINED, sf the plate
    // pitch in metres, si the metalness, sp 1 for a plain bar with no seams
    const fx={zone:D.zi, lit:D.lit, ldir:D.ldir, rim:0.26, ao:0.56,
              rough:0.38, sk:3, sf:0.30, si:MET_STEEL, sp:0};
    const M=(rough,pitch,met,plain)=>{ fx.rough=rough; fx.sf=pitch;
                                       fx.si=met; fx.sp=plain?1:0; };
    const lampFx={zone:D.zi, lit:ENT_ZERO, ldir:ENT_ZERO, rim:0, ao:1, rough:0.30};
    // amber standing by, cyan running.  The two accent roles this needs --
    // and the crossfade is snapped, because the halfway mix of the two is a
    // washed-out white that is neither and reads as a blown lamp.
    const k=e<=0.28?0:e>=0.72?1:(e-0.28)/0.44, lamp=[AD_AMBER[0]+(AD_CYAN[0]-AD_AMBER[0])*k,
                     AD_AMBER[1]+(AD_CYAN[1]-AD_AMBER[1])*k,
                     AD_AMBER[2]+(AD_CYAN[2]-AD_AMBER[2])*k];
    const run=D.moved?(0.70+0.30*Math.sin(elapsed*22)):1.0;

    // ---- the head.  The frame's own brush starts at 2.62 and runs to the
    // ceiling, so the track hangs just under it and the sensor eye goes on
    // its FACE, clear of the brush, at half the frame's depth plus a little.
    const ey=th*0.5+0.022;
    M(0.34, 0.24, MET_STEEL, 1);
    adBox(D, 0, H-0.056, 0,          hw*2-0.02, 0.112, th*0.72, AD_STEEL, 0, fx);
    M(0.26, 0.24, MET_TRIM, 1);
    adBox(D, 0, H-0.118, 0,          hw*2-0.06, 0.026, th*0.76, AD_TRIM, 0, fx);
    M(0.34, 0.16, MET_STEEL, 0);
    for(let s=-1;s<=1;s+=2){
      adBox(D, 0, H+0.155, s*ey,     0.40, 0.110, 0.044, AD_STEEL, 0, fx);
      adBox(D, 0, H+0.155, s*(ey+0.014), 0.26,0.046,0.028, lamp, 0.85*run, lampFx);
    }

    if(e>0.985) continue;
    const lw=hw*(1-e);
    if(lw<0.045) continue;

    const LH=H-0.125;                 // the leaf, hanging off that track
    const bd=th*0.44;                 // the leaf's own thickness
    const pf=th*0.22+0.013;           // and where a plate on its face sits
    for(let si=0;si<2;si++){
      const s=si?1:-1;
      const c=s*(hw-lw*0.5);          // the body's centre: it stretches
      const nz=s*(hw-lw);             // the leading edge: it translates

      // the body.  Everything on it runs ACROSS the slide, so the stretch
      // has nothing to show -- and the plating does not stretch either,
      // because MACHINED measures itself in metres.
      M(0.38, 0.30, MET_STEEL, 0);
      adBox(D, c, LH*0.5, 0, lw, LH, bd, AD_STEEL, 0, fx, PRIM.BEVEL);
      M(0.44, 0.15, MET_KICK, 0);
      adBox(D, c, 0.105, 0, lw, 0.210, bd+0.020, AD_KICK, 0, fx);
      if(fine){
        for(let f=-1;f<=1;f+=2){
          M(0.28, 0.20, MET_TRIM, 1);
          adBox(D, c, 0.910, f*pf, lw-0.06, 0.048, 0.024, AD_TRIM, 0, fx);
          M(0.62, 0.20, MET_GAP, 1);
          for(let r=0;r<3;r++)
            adBox(D, c, 0.560+r*0.700, f*pf, lw-0.10, 0.116, 0.022, AD_GAP, 0, fx);
          if(lw>0.42){
            M(0.66, 0.09, MET_OCHRE, 0);
            adBox(D, c, 0.330, f*pf, lw-0.16, 0.090, 0.022, AD_OCHRE, 0, fx);
          }
        }
      }

      // the one vertical thing a stretching body is allowed: a stiffener
      // measured from the NOSE, so it slides with it and never scales
      M(0.26, 0.20, MET_TRIM, 1);
      if(fine) for(let r=0;r<2;r++){
        const q=0.42+r*0.46;
        if(q<lw-0.10)
          adBox(D, nz+s*q, LH*0.5+0.02, 0, 0.062, LH-0.30, bd+0.026, AD_TRIM, 0, fx);
      }

      // the leading edge, at a fixed size, sliding.  The nose rib, the teeth
      // that interlock with the other leaf, and the line down the joint.
      M(0.30, 0.20, MET_STEEL, 1);
      adBox(D, nz+s*0.070, LH*0.5, 0, 0.132, LH, bd+0.055, AD_STEEL, 0, fx, PRIM.BEVEL);
      if(lw>0.34){
        const y0=si?0.905:0.520;
        for(let r=0;r<(si?2:3);r++)
          adBox(D, nz-s*0.085, y0+r*0.770, 0, 0.180, 0.300, bd+0.030, AD_TRIM, 0, fx);
      }
      for(let f=-1;f<=1;f+=2)
        adBox(D, nz+s*0.128, LH*0.50+0.06, f*(pf+0.014), 0.030, LH*0.72, 0.022,
              lamp, 0.48*run, lampFx);

      // THE ACTUATOR, AND THE ONE THING THAT IS ALLOWED TO STRETCH.  A ram
      // bolted to the jamb and a rod running to the nose: the rod is the only
      // part of the leaf whose length is supposed to change, so the same
      // scaling that had to be hidden everywhere else is, here, the point.
      if(fine) for(let f=-1;f<=1;f+=2){
        M(0.24, 0.12, MET_TRIM, 1);
        adBox(D, s*(hw-0.17), LH-0.090, f*(pf+0.030), 0.320, 0.104, 0.086,
              AD_TRIM, 0, fx);
        M(0.18, 0.12, MET_TRIM, 1);
        adBox(D, c, LH-0.090, f*(pf+0.026), lw, 0.046, 0.044, AD_TRIM, 0, fx);
      }
    }
  }
}

/* ---- the sound of a servo, and the latch at the end of it -------------- */
function sfxSlide(x,y,z,open){
  const vol=dGain(x,y,z,34); if(vol<0.03||!canVoice(1)) return;
  const t=AC.currentTime;
  const o=AC.createOscillator(); o.type='sawtooth';
  o.frequency.setValueAtTime(open?96:190,t);
  o.frequency.linearRampToValueAtTime(open?205:104,t+0.42);
  const bp=AC.createBiquadFilter(); bp.type='bandpass';
  bp.frequency.value=520; bp.Q.value=1.6;
  const g=AC.createGain(); env(g,t,vol*0.16,0.62,0.34);
  o.connect(bp).connect(g); voiceOut(g,o,t,0.66,1);
  const n=noiseSrc(0.85);
  const nf=AC.createBiquadFilter(); nf.type='bandpass';
  nf.frequency.setValueAtTime(1500,t);
  nf.frequency.linearRampToValueAtTime(700,t+0.5);
  nf.Q.value=0.7;
  const ng=AC.createGain(); env(ng,t,vol*0.11,0.58,0.26);
  n.connect(nf).connect(ng); voiceOut(ng,n,t,0.62,1);
}
function sfxLatch(x,y,z){
  const vol=dGain(x,y,z,30); if(vol<0.03||!canVoice(1)) return;
  const t=AC.currentTime;
  const n=noiseSrc(0.5);
  const lp=AC.createBiquadFilter(); lp.type='lowpass'; lp.frequency.value=460;
  const g=AC.createGain(); env(g,t,vol*0.30,0.16);
  n.connect(lp).connect(g); voiceOut(g,n,t,0.18,1);
  const o=AC.createOscillator(); o.type='triangle';
  o.frequency.setValueAtTime(148,t);
  o.frequency.exponentialRampToValueAtTime(74,t+0.12);
  const og=AC.createGain(); env(og,t,vol*0.16,0.15);
  o.connect(og); voiceOut(og,o,t,0.17,1);
}
