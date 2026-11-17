"""Procedural weapons, spliced into the game.

Ports the standalone weapon bench the guns were first built on into
the derelict engine.  The payload is tools/weapons.js: loft/lathe meshes,
GPU-generated PBR texture sets, a view-space weapon shader, and the ref's
animation grammar (slide cycle, break action, spin-up + overheat), mapped
onto the engine's existing glock / shotgun / smg slots so ammo stores,
pickups and the HUD keep working.  The crowbar and the grenade keep their
box viewmodels through drawViewmodelBoxes.

The engine's main script is one strict-mode IIFE, so the payload is spliced
INSIDE it, just before build(); its three re-declared functions
(drawViewmodel / tryFire / updateWeapons) shadow the originals -- duplicate
function declarations are legal there and the last one wins.

Chain:  build_ship.py -> splice.py -> weapons.py -> doors.py -> decals.py
        -> derelict.html -> deckmap.py -> map.html

Run:  python tools/weapons.py       (after every splice.py run)
      python tools/doors.py         (and then doors.py, then decals.py)
      python tools/decals.py
"""
import io, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TARGET = os.path.join(ROOT, 'src', 'derelict', 'derelict.html')

s = io.open(TARGET, encoding='utf-8').read()
payload = io.open(os.path.join(HERE, 'weapons.js'), encoding='utf-8').read()

assert 'PROCEDURAL WEAPONS' not in s, \
    'weapons.py already applied -- re-run splice.py first for a clean base'

n = [0]


def sub(old, new, label):
    global s
    assert s.count(old) == 1, 'anchor missing or not unique: ' + label
    s = s.replace(old, new, 1)
    n[0] += 1


# the box builder stays for the crowbar and the grenade
sub('function drawViewmodel(){',
    'function drawViewmodelBoxes(){', 'rename box viewmodel')

# HUD: the cannon has no magazine -- show the pool and the heat instead
sub("  else { hudEl.ammo.textContent=MAGS[w.id]; hudEl.res.textContent='/ '+AMMO[w.id]; }",
    "  else if(!w.mag){ hudEl.ammo.textContent=AMMO[w.id];\n"
    "    hudEl.res.textContent=VMW.A.over?'OVERHEAT':((VMW.A.heat*100|0)+'% HEAT'); }\n"
    "  else { hudEl.ammo.textContent=MAGS[w.id]; hudEl.res.textContent='/ '+AMMO[w.id]; }",
    'hud magless ammo')
sub("hudEl.ammo.parentElement.classList.toggle('crit',!w.melee&&!w.thrown&&MAGS[w.id]===0);",
    "hudEl.ammo.parentElement.classList.toggle('crit',"
    "!w.melee&&!w.thrown&&(w.mag?MAGS[w.id]===0:VMW.A.over));",
    'hud crit')
sub("hudEl.wname.textContent=reloadT>0?'RELOADING':w.name;",
    "hudEl.wname.textContent=reloadT>0?'RELOADING':(VMW.A.over?'OVERHEATED':w.name);",
    'hud overheat name')

# spent casings live in the world, so they draw in the world pass with the
# scene's projection -- before the viewmodel's depth clear
sub('    // ---- particles: blood, sparks, smoke, muzzle flame -----------------',
    '    VMW.drawShells(proj,view);\n\n'
    '    // ---- particles: blood, sparks, smoke, muzzle flame -----------------',
    'shell casings in world pass')

# the payload itself, inside the IIFE, after every definition it shadows
sub('\nbuild();\nrequestAnimationFrame(frame);',
    '\n' + payload + '\nbuild();\nrequestAnimationFrame(frame);',
    'weapons payload')

io.open(TARGET, 'w', encoding='utf-8').write(s)
print('%d edits -> %s (%.1f KB payload, %.1f MB total)'
      % (n[0], os.path.relpath(TARGET, ROOT), len(payload) / 1024.0,
         len(s) / 1e6))
