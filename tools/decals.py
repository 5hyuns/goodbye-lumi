"""Impact craters, spliced over the decal system.

Every decal in the game went through one shader that draws a blood splat,
flat and unlit, in whatever colour the caller passed -- so a bullet mark
was that splat in near-black, laid on the wall at a fixed 14 mm offset
that is inside one depth-buffer step at range, which is where the shimmer
came from.  tools/decals.js replaces it with a two-kind shader: kind 0 is
the old splat, untouched, still doing blood; kind 1 is a lit crater with a
height profile, a hot core that cools, and a range-scaled lift.

The edits:
  MAX_DECAL     the instance grows 12 floats -> 20 (light, age, kind)
  decal VAO     ... so it needs its own setup, at stride 80 and 5 attribs
  DECAL_VS/FS   replaced wholesale from decals.js
  UDe           the crater is lit, so it needs the camera, torch and fog
  decal draw    ... and those uniforms set, fog read back off pScene so it
                    is right in both the game and the map pass
  addDecal      kind, a light sample at spawn, the hole cap, door binding
  fillDecals    ... and a bound mark's position recomputed every frame
  surfaceHit    craters, at sizes that vary, bound to a leaf when it is one

Needs tools/doors.py first: a bound mark asks adBindAt() which leaf it hit.

Chain:  build_ship.py -> splice.py -> weapons.py -> doors.py -> decals.py
        -> derelict.html -> deckmap.py -> map.html

Run:  python tools/decals.py       (after every doors.py run)
"""
import io, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TARGET = os.path.join(ROOT, 'src', 'derelict', 'derelict.html')

s = io.open(TARGET, encoding='utf-8').read()
src = io.open(os.path.join(HERE, 'decals.js'), encoding='utf-8').read()

assert 'AUTOMATIC BULKHEAD DOORS' in s, 'run doors.py first: adBindAt lives there'


def block(name):
    i = src.index('/*==%s==*/' % name) + len(name) + 8
    return src[i:src.index('/*==END==*/', i)].strip() + '\n'


SHADERS, CPU = block('SHADERS'), block('CPU')

n, skipped = [0], [0]


def sub(old, new, label, mark):
    global s
    if mark in s:
        skipped[0] += 1
        return
    assert s.count(old) == 1, 'anchor missing or not unique: ' + label
    s = s.replace(old, new, 1)
    n[0] += 1


def cut(a, b, new, label, mark):
    """Replace everything from `a` up to (not including) `b`."""
    global s
    if mark in s:
        skipped[0] += 1
        return
    assert s.count(a) == 1 and s.count(b) >= 1, 'anchor missing: ' + label
    i = s.index(a)
    s = s[:i] + new + s[s.index(b, i):]
    n[0] += 1


# ---- the instance grows, and so the decal VAO stops sharing the loop ----
sub("const MAX_DECAL=220, DECAL_STRIDE=12;",
    "const MAX_DECAL=220, DECAL_STRIDE=20;", 'stride', 'DECAL_STRIDE=20;')

sub("""  for(const [vao,inst,data] of [[partVAO,partInst,partData],[decalVAO,decalInst,decalData]]){
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER,quadBuf);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0,2,gl.FLOAT,false,8,0);
    gl.bindBuffer(gl.ARRAY_BUFFER,inst);
    gl.bufferData(gl.ARRAY_BUFFER,data.byteLength,gl.DYNAMIC_DRAW);
    for(let i=0;i<3;i++){
      gl.enableVertexAttribArray(1+i);
      gl.vertexAttribPointer(1+i,4,gl.FLOAT,false,48,i*16);
      gl.vertexAttribDivisor(1+i,1);
    }
    gl.bindVertexArray(null);
  }""",
    """  // particles at 12 floats, decals at 20: the crater carries the light
  // that was on the wall, its age and its kind besides its placement
  for(const [vao,inst,data,nv] of [[partVAO,partInst,partData,3],
                                   [decalVAO,decalInst,decalData,5]]){
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER,quadBuf);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0,2,gl.FLOAT,false,8,0);
    gl.bindBuffer(gl.ARRAY_BUFFER,inst);
    gl.bufferData(gl.ARRAY_BUFFER,data.byteLength,gl.DYNAMIC_DRAW);
    for(let i=0;i<nv;i++){
      gl.enableVertexAttribArray(1+i);
      gl.vertexAttribPointer(1+i,4,gl.FLOAT,false,nv*16,i*16);
      gl.vertexAttribDivisor(1+i,1);
    }
    gl.bindVertexArray(null);
  }""", 'decal vao', 'decalData,5]]){')

# ---- the shaders themselves --------------------------------------------
cut('const DECAL_VS=`#version 300 es', 'const FS_VS=`#version 300 es',
    SHADERS + '\n', 'decal shaders', 'float crater(float a){')

sub("const UDe=uni(pDecal,['uProj','uView']);",
    "const UDe=uni(pDecal,['uProj','uView','uCamPos','uCamDir','uFlash',\n"
    "  'uFogCol','uFogDensity','uAmb']);\nlet FOG_D=-1, FOG_C=null, FOG_A=null;",
    'decal uniforms', "let FOG_D=-1, FOG_C=null, FOG_A=null;")

# the haze is a different number in the game and in the map pass, and
# deckmap.py rewrites it in place -- so read it back rather than repeat it
sub("""      gl.useProgram(pDecal);
      gl.uniformMatrix4fv(UDe.uProj,false,proj); gl.uniformMatrix4fv(UDe.uView,false,view);""",
    """      gl.useProgram(pDecal);
      if(FOG_D<0){ gl.useProgram(pScene);
        FOG_D=gl.getUniform(pScene,US.uFogDensity);
        FOG_C=gl.getUniform(pScene,US.uFogCol);
        FOG_A=gl.getUniform(pScene,US.uMinLight); gl.useProgram(pDecal); }
      gl.uniformMatrix4fv(UDe.uProj,false,proj); gl.uniformMatrix4fv(UDe.uView,false,view);
      gl.uniform3fv(UDe.uCamPos,eye); gl.uniform3fv(UDe.uCamDir,dir);
      gl.uniform1f(UDe.uFlash,flashlight?1:0);
      gl.uniform1f(UDe.uFogDensity,FOG_D);
      gl.uniform3f(UDe.uFogCol,FOG_C[0],FOG_C[1],FOG_C[2]);
      gl.uniform3f(UDe.uAmb,FOG_A[0]*0.9,FOG_A[1]*0.9,FOG_A[2]*0.9);""",
    'decal draw uniforms', 'gl.uniform3fv(UDe.uCamPos,eye);')

# ---- addDecal / fillDecals ---------------------------------------------
cut('const DECALS=[];\nfunction addDecal(',
    '\n// loose boxes with a bit of physics: gibs, shell casings, wreckage',
    CPU, 'decal cpu', 'const MAX_HOLE=56;')

# ---- and the shot that makes one ---------------------------------------
sub("""  if(mat!==T.LAMP)
    addDecal(x,y,z,n[0],n[1],n[2],0.055+Math.random()*0.045, 0.05,0.045,0.04, 0.85);""",
    """  // A HOLE IS NOT ONE SIZE.  Square on, a round leaves a neat 90 mm
  // crater; arriving at a slant it tears a gouge twice that, and the spread
  // between the two is most of what a wall full of them reads as.  Skewed
  // small, so the big ones stay events.
  if(mat!==T.LAMP)
    addDecal(x,y,z,n[0],n[1],n[2],0.045+Math.pow(Math.random(),2.3)*0.145,
             0.046,0.041,0.036, 0.94, 1, adBindAt(h));""",
    'bullet craters', 'adBindAt(h));')

io.open(TARGET, 'w', encoding='utf-8').write(s)
print('%d edits (%d already in) -> %s (%.1f MB)'
      % (n[0], skipped[0], os.path.relpath(TARGET, ROOT), len(s) / 1e6))
