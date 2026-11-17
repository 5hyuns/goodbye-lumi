"""Automatic bulkhead doors, spliced into the game.

deckplan.py has drawn 34 doorways since v6 and shipdetail.py has built
their frames since the first pass -- the head at 2.62 m, a jamb either
side, a hazard strip on the deck -- leaving behind `const SHIP_DOORS=[]`
and the note "the leaf is dynamic and comes later".  This is later.

The payload is tools/doors.js: a proximity servo, a bi-parting leaf whose
nose slides at a fixed size while its body stretches, its own solid boxes
for the player and for bullets, and a servo sound built out of the
engine's existing noise buffer.  It is spliced INSIDE the engine's main
IIFE, like the weapons payload, so it can see SHIP_DOORS, zoneAt,
sampleLight, pushBox and the sound bus.

AND ONE SHADER EDIT, WHICH IS THE REST OF THE JOB.  Every entity in this
engine -- zombie, prop, crate, blast door -- is drawn by ENT_FS with a
flat albedo, no surface detail of any kind, and F0 nailed to 0.045: a
pure dielectric.  That is why a box entity reads as painted card next to
a wall the material compiler gave a normal map to.  So ENT_FS gets a
third surface kind, MACHINED, beside the creature's shell and membrane:
a plate field with chamfered seams and bolts, a brushed grain that lives
in the roughness where a scratch too fine to see actually does, and a
metalness that moves the whole energy budget out of diffuse and into a
tinted specular.  The creature path is untouched -- everything that
passes sk=0 still takes the cheap branch and pays nothing.

The edits that carry it:
  ENT_FS      MACHINED: hard-surface normal, brushed roughness, conductor
  boxSolid    a closed leaf is solid                (player and zombies)
  dynTrace    ... and stops a bullet                 (but is not the blast
  fireHitscan     without being mistaken for one      door, so no damage)
  gameStep    the servo runs even in the menu, so doors settle shut
  render      drawn after drawGameWorld, which the map pass stubs out

Every edit is idempotent: re-running swaps the payload and leaves the
anchors alone, so iterating on a leaf does not mean re-running the chain.

Chain:  build_ship.py -> splice.py -> weapons.py -> doors.py -> decals.py
        -> derelict.html -> deckmap.py -> map.html

Run:  python tools/doors.py       (after every weapons.py run)
"""
import io, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TARGET = os.path.join(ROOT, 'src', 'derelict', 'derelict.html')

s = io.open(TARGET, encoding='utf-8').read()
payload = io.open(os.path.join(HERE, 'doors.js'), encoding='utf-8').read()

assert 'const SHIP_DOORS=[];' in s, 'no door list: run splice.py first'

n, skipped = [0], [0]


def sub(old, new, label, mark):
    """Idempotent: `mark` is what the edit leaves behind."""
    global s
    if mark in s:
        skipped[0] += 1
        return
    assert s.count(old) == 1, 'anchor missing or not unique: ' + label
    s = s.replace(old, new, 1)
    n[0] += 1


# ==========================================================================
#  1.  THE SHADER.  A third surface kind for ENT_FS.
# ==========================================================================
# vSurf is (kind, detail scale, sheen, phase) and only the stalker has ever
# set it.  kind 3 means MACHINED and takes scale as the plate pitch in
# metres, sheen as metalness, and phase as 0 for a plate field / 1 for a
# plain bar.  SF has to stop claiming everything above 0.5 or the creature
# branch would run on a door.
sub("  bool SF=vSurf.x>0.5, shell=false;",
    "  bool MC=vSurf.x>2.5;\n"
    "  bool SF=vSurf.x>0.5&&!MC, shell=false;",
    'machined kind', 'bool MC=vSurf.x>2.5;')

sub("""  float shin=mix(4.0,150.0,pow(1.0-rough,1.6));""",
    """  /* ---- MACHINED --------------------------------------------------------
     A hard surface, and the point is that NOTHING HERE IS NOISE.  Every
     feature is an edge: a 3 mm seam with a 9 mm chamfer either side of it,
     a bolt set inboard of each plate corner, and a brushed grain that runs
     one way and lives in the ROUGHNESS, which is where a scratch too fine
     to resolve actually is.  Noise in the height is what forged iron looks
     like; milled steel is flat between its edges and that is the whole
     difference.
     It is measured in METRES, in the part's own frame, for the same reason
     the creature's hide is: a 0.16 m nose rib and a 1.8 m leaf have to get
     the same seam, the same size, or the door is an assembly of parts
     instead of one door.  A leaf that stretches as it opens therefore does
     not stretch its plating -- vObjW is already in metres.
     THEN THE CONDUCTOR TRICK.  A metal has no diffuse worth the name, so
     the albedo stops being a colour and becomes the reflectance: authored
     bright, it still reads dark, and it is the only way to get a trim that
     is nearly black until it catches a lamp and then flashes. */
  vec3 specTint=vec3(1.0), F0mc=vec3(0.045);
  if(MC){
    float pitch=max(vSurf.y,0.02);
    // a feature 12 mm across stops being a feature and starts being noise
    // somewhere around fifteen metres, so it is faded out rather than
    // left to crawl
    float near=(uVM==1)?1.0:1.0-smoothstep(7.0,17.0,length(uCamPos-vPos));
    vec3 an=abs(N);
    vec2 uv; vec3 TU,TV;
    if(an.y>an.x&&an.y>an.z){ uv=vObjW.xz; TU=vec3(1,0,0); TV=vec3(0,0,1); }
    else if(an.x>an.z){       uv=vObjW.zy; TU=vec3(0,0,1); TV=vec3(0,1,0); }
    else {                    uv=vObjW.xy; TU=vec3(1,0,0); TV=vec3(0,1,0); }
    vec2 tilt=vec2(0.0);
    float edge=0.0;
    if(vSurf.w<0.5){
      vec2 g=uv/pitch;
      vec2 c=fract(g)-0.5;
      vec2 d=(0.5-abs(c))*pitch;                 // metres to the nearest seam
      vec2 pr=1.0-smoothstep(0.003,0.012,d);     // the chamfer's own profile
      tilt-=sign(c)*pr;
      edge=max(pr.x,pr.y);
      vec2 bd=d-vec2(0.044);                     // a bolt inboard of the corner
      float rr=length(bd), k=smoothstep(0.014,0.004,rr);
      tilt+=normalize(bd+vec2(1e-5))*k*(1.0-k)*3.2;
      edge=max(edge,k);
    }
    float ln=fract(sin(floor(uv.y*620.0)*12.9898)*43758.5453);
    tilt.x+=(ln-0.5)*0.11;
    N=normalize(N+(TU*tilt.x+TV*tilt.y)*0.62*near);
    rough=clamp(rough*(0.82+0.36*ln*near)+0.20*edge*near,0.05,1.0);
    alb*=1.0-0.40*edge*near;
    float met=clamp(vSurf.z,0.0,1.0);
    specTint=mix(vec3(1.0),alb*2.4,met);
    F0mc=mix(vec3(0.045),alb*0.90+0.06,met);
    alb*=1.0-0.88*met;
  }

  float shin=mix(4.0,150.0,pow(1.0-rough,1.6));""",
    'machined surface', 'vec3 specTint=vec3(1.0), F0mc=vec3(0.045);')

sub("  vec3 F0=vec3(0.045);\n  float ndv=max(dot(N,V),0.0);",
    "  vec3 F0=F0mc;\n  float ndv=max(dot(N,V),0.0);",
    'machined F0', 'vec3 F0=F0mc;')

sub("  vec3 col=alb*diff+spec+rim+env*Fe*vAO*",
    "  vec3 col=alb*diff+spec*specTint+rim+env*Fe*vAO*",
    'machined specular tint', 'spec*specTint')

# ==========================================================================
#  2.  THE WORLD EDITS
# ==========================================================================
sub("""  for(let k=0;k<DYNB.length;k++){
    const b=DYNB[k];
    if(ax1>b[0]&&ax0<b[3]&&ay1>b[1]&&ay0<b[4]&&az1>b[2]&&az0<b[5]) return true;
  }
  return false;
}""",
    """  for(let k=0;k<DYNB.length;k++){
    const b=DYNB[k];
    if(ax1>b[0]&&ax0<b[3]&&ay1>b[1]&&ay0<b[4]&&az1>b[2]&&az0<b[5]) return true;
  }
  for(let k=0;k<ADB.length;k++){
    const b=ADB[k];
    if(ax1>b[0]&&ax0<b[3]&&ay1>b[1]&&ay0<b[4]&&az1>b[2]&&az0<b[5]) return true;
  }
  return false;
}""", 'boxSolid', 'for(let k=0;k<ADB.length;k++){')

# DYNB first, so an index below DYNB.length still means the blast door
sub("""  let best=-1, bt=maxT;
  for(let k=0;k<DYNB.length;k++){
    const b=DYNB[k];""",
    """  let best=-1, bt=maxT;
  const nDB=DYNB.length, nAll=nDB+ADB.length;
  for(let k=0;k<nAll;k++){
    const b=k<nDB?DYNB[k]:ADB[k-nDB];""", 'dynTrace', 'const nDB=DYNB.length,')

sub("    if(h.kind==='dyn') hurtDoor(w.dmg*0.5);",
    "    if(h.kind==='dyn'&&h.bi<DYNB.length) hurtDoor(w.dmg*0.5);",
    'only the blast door takes damage', "h.bi<DYNB.length) hurtDoor")

# the servo, ahead of the playing gate so the doors settle shut
sub("function gameStep(dt,rdt){\n  updateDynLights(rdt);",
    "function gameStep(dt,rdt){\n  updateDynLights(rdt);\n  updateAutoDoors(rdt);",
    'door update', 'updateAutoDoors(rdt);')

# NOT inside drawGameWorld: deckmap.py stubs that out for the map pass
sub("""  // ---- everything that moves ------------------------------------------
  drawGameWorld();""",
    """  // ---- everything that moves ------------------------------------------
  drawGameWorld();
  drawAutoDoors();""", 'door draw', 'drawAutoDoors();')

# ==========================================================================
#  3.  THE PAYLOAD.  Replaced wholesale if it is already in, because the
#      leaf is the part that gets iterated on and re-running the whole
#      chain to move a colour by 0.08 is not a thing anybody does twice.
# ==========================================================================
HEAD = '/* ============================================================\n' \
       '   AUTOMATIC BULKHEAD DOORS'
TAIL = '\nbuild();\nrequestAnimationFrame(frame);'
if HEAD in s:
    i = s.index(HEAD)
    s = s[:i] + payload + s[s.index(TAIL, i):]
else:
    assert s.count(TAIL) == 1, 'anchor missing or not unique: doors payload'
    s = s.replace(TAIL, '\n' + payload + TAIL, 1)
n[0] += 1

io.open(TARGET, 'w', encoding='utf-8').write(s)
print('%d edits (%d already in) -> %s (%.1f KB payload, %.1f MB total)'
      % (n[0], skipped[0], os.path.relpath(TARGET, ROOT), len(payload) / 1024.0,
         len(s) / 1e6))
