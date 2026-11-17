"""Deck plan v6, assembled out of the corridor kit.

`kitview.py` proved one 24 m corridor: an octagonal section, a zoned deck,
a wall run that is a sequence rather than a repeat, and every lit thing
also a lamp.  This takes that grammar and lays it over the whole plan.

WHAT IT CANNOT DO, AND WHY.  The kit runs about 130 faces per square metre
of surface, and this engine bakes the entire ship into one mesh with a
radiosity gather at every vertex and no visibility culling at all.  The
plan is 2,553 deck modules, 2,553 overheads and 850 wall modules; built at
the bench's density that is ~2.3 M triangles, and the bench itself is
279 k for 10.6 s of AO and 553 MB of heap.  Roughly a third of the plan
fits, and which third is a design decision, not an arithmetic one.

So the ship is built at two grades, and the line between them is the one
a previz should draw anyway:

  FULL   the route the player actually walks -- every part of the kit,
         exactly as the bench lays it, module by module.
  SHELL  everything else -- the same materials, the same octagonal
         section, the same accent rule and the same 4U beat, but drawn
         as brushes and quads per WALL RUN instead of parts per module.
         About a fortieth of the cost, and it is what makes the whole
         plan exist and be walkable rather than a third of it.

AND THE SHELL HAS TO SPEAK THE SAME LANGUAGE, not just wear the same
materials.  The first pass gave it the route's finishes and left it reading
as an empty box, because a corridor is not its surfaces -- it is a BEAT (the
4U rib), an EVENT (WALL 3's intake, an access lid, a readout), a WARNING
(the safety band ahead of the thing it warns about), a LIT LINE, and
something standing in it.  Every one of those is now drawn per run or per
room instead of per module, which is the whole bargain: a fortieth of the
cost for the same vocabulary.

As built:  377 full cells / 2,176 shell, 1,364 part instances,
           965 k triangles, 257 lamps -> 2.7 s compile + 95 s AO.
           Shell structure: 77 stanchions, 14 columns, 231 wall events,
           334 deck lids, 92 caged fittings on 33 lit runs, 37 threshold
           bands.  BUDGET below is the one knob for the FULL/SHELL line:
           lower it and runs fall off the end of ROUTE into SHELL, raise it
           and they come back, at about 2.4 triangles and 0.26 ms of AO per
           face.  It is no longer the binding constraint -- ROUTE's own
           length is (390 k of 400 k spent, all ten runs taken).

Chain:  build_ship.py -> splice.py -> weapons.py -> doors.py -> decals.py
        -> derelict.html -> deckmap.py -> map.html
Sizing: node tools/kitcost.js   -- what a MODULES list really costs

Run:  python tools/deckmap.py
"""
import io, os, re, sys, math

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import deckplan as D
import deckgrid as K

SRC = os.path.join(ROOT, 'src', 'derelict', 'derelict.html')
OUT = os.path.join(ROOT, 'src', 'derelict', 'map.html')

U = K.U
WT = 0.30                       # hull thickness
BAY_H = 3 * U                   # 3.600 -- a kit bay is a corridor's height,
                                # and above it a tall room gets a string
                                # course and plain hull, like a real one

# ---- the route ------------------------------------------------------------
# The critical path: you wake in the crew quarters, come down the
# passage, turn east into the spine, take the tutorial passage south through
# the first engagement node into the lobby, then down to the stores and out
# to the engine room.  Ordered by what the player sees first, because the
# budget is spent in this order and whatever does not fit falls to SHELL.
ROUTE = [('corr', 3),      # the tutorial passage, north spine -- seen first
         ('corr', 5),      # quarters passage into the spine
         ('corr', 1),      # out of the crew quarters: where you wake
         ('node', 0),      # engagement node 1, on the spine
         ('corr', 11),     # lobby down to the stores
         ('corr', 15),     # stores south
         ('node', 1),      # engagement node 2, the engine junction
         ('corr', 17),     # its south leg
         ('corr', 16),     # its west leg, into the engine room
         ('corr', 9)]      # the gallery spur -- a branch, so it goes last

# Faces the kit may spend.  279 k triangles cost 10.6 s of AO in the bench;
# this is set so the whole ship lands near 900 k.
BUDGET = 400000

# ---- part indices, as they are numbered in kit.js -------------------------
FLOOR1, FLOOR2, FLOOR3, FLOOR4 = 2, 3, 4, 17
WALL1, WALL2, WALL3, WALL4 = 5, 6, 7, 16
COLUMN1, COLUMN2, COLUMN3, CORNER1 = 8, 9, 10, 11
CEILING1, SKIRT1, COVE1, FILLET1, BAND1 = 12, 13, 14, 15, 18

# measured off the parts themselves (tools/kitcost.js)
COST = {FLOOR1: 199, FLOOR2: 742, FLOOR3: 386, FLOOR4: 298,
        WALL1: 702, WALL2: 1252, WALL3: 1156, WALL4: 2268,
        CORNER1: 726, CEILING1: 154, SKIRT1: 76, COVE1: 132,
        FILLET1: 126, BAND1: 54, COLUMN1: 702, COLUMN2: 890, COLUMN3: 1122}

# THE WALL RUN IS A SEQUENCE, NOT A REPEAT.  A 4U field, a 2U main run,
# then two 1U events -- and the two sides of a passage start at different
# points in it, so a long field always faces something else.
SEQ = ((WALL4, 4), (WALL2, 2), (WALL1, 1), (WALL3, 1))

# Modules between lit overhead channels, and so between overhead lamps --
# they are the same number because they are the same fitting.
LAMP_BEAT = 3

J, L, Z, MODULES = [], [], [], []
faces_spent = [0]


def r3(v):  return '%g' % round(v + 0.0, 4)
def g(s):   J.append(s)


def bx(x0, y0, z0, x1, y1, z1, mat, clip=0):
    g('brush(%s,%s,%s, %s,%s,%s, T.%s%s);' % (
        r3(x0), r3(y0), r3(z0), r3(x1), r3(y1), r3(z1), mat,
        ',1' if clip else ''))


def qd(c, mat):
    g('quad([%s,%s,%s],[%s,%s,%s],[%s,%s,%s],[%s,%s,%s], T.%s);'
      % (tuple(r3(v) for p in c for v in p) + (mat,)))


def mod(kit, x, y, z, yaw, arg, mat):
    MODULES.append('[%d,%s,%s,%s,%s,%s,T.%s]'
                   % (kit, r3(x), r3(y), r3(z), r3(yaw), r3(arg), mat))
    faces_spent[0] += COST.get(kit, 400)


def light(x, y, z, col, r, flick=0):
    L.append('  {p:[%s,%s,%s], c:[%s], r:%s%s},'
             % (r3(x), r3(y), r3(z), col, r3(r), ', flick:1' if flick else ''))


def zone(name, sub, x0, x1, z0, z1, cx, cz, cy=1.7):
    Z.append("  {n:'%s', s:'%s', x0:%s,x1:%s, z0:%s,z1:%s, c:[%s,%s,%s]}"
             % (name, sub, r3(x0), r3(x1), r3(z0), r3(z1),
                r3(cx), r3(cy), r3(cz)))


# world coordinates from fractional grid coordinates
def wxf(v):  return (v * K.MU - K.OX) * K.S
def wzf(v):  return (v * K.MU - K.OZ) * K.S


# ==========================================================================
#  1.  THE GRID, AND WHICH CELLS GET THE FULL KIT
# ==========================================================================
G, RUNS = K.build()
CELLS = set(G.reg)
RUNBY = {}                       # ('corr'|'node', index) -> run record
for r in RUNS:
    RUNBY[(r[0], r[1])] = r

grade = dict((c, 1) for c in CELLS)        # 1 = SHELL, 2 = FULL
axis_of, along_of, across_of = {}, {}, {}

# every cell gets a run axis, so the deck can be zoned along something
for (kind, idx, ax, box, cells) in RUNS:
    for (gx, gz) in cells:
        if ax == 'x':
            axis_of[(gx, gz)], along_of[(gx, gz)], across_of[(gx, gz)] = 'x', gx, gz
        else:
            axis_of[(gx, gz)], along_of[(gx, gz)], across_of[(gx, gz)] = 'z', gz, gx
for c in CELLS:
    if c not in axis_of:
        axis_of[c], along_of[c], across_of[c] = 'z', c[1], c[0]

# Spend the budget down the route.  A run is taken whole or not at all --
# half a corridor in the kit and half in brushes is worse than either.
ROUTE_TAKEN, spent = [], 0
for key in ROUTE:
    if key not in RUNBY:
        continue
    # only cells this run does not already share with one taken before it:
    # passages overlap at every junction, and counting a shared cell twice
    # spends the budget on nothing
    cells = [c for c in RUNBY[key][4]
             if c in CELLS and grade[c] == 1
             and G.REG[G.reg[c]]['kind'] in ('corr', 'node')]
    if not cells:
        continue
    # deck + overhead + roughly one wall module for every two cells
    est = len(cells) * (330 + 154) + len(cells) * 0.55 * (672 + 208)
    if spent + est > BUDGET:
        continue
    spent += est
    ROUTE_TAKEN.append((key, len(cells)))
    for c in cells:
        grade[c] = 2

FULL = set(c for c in CELLS if grade[c] == 2)


def touches_wall(gx, gz):
    for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if (gx + dx, gz + dz) not in CELLS:
            return (dx, dz)
    return None


# ==========================================================================
#  2.  WALL FACES  ->  RUNS
#      Every edge of the open set is a wall.  Grouped into runs so a 20 m
#      hull side is one brush and one splay, not seventeen of each.
# ==========================================================================
# key: (axis, grid line, inward sign)  ->  {along index: (height in modules,
# grade)}
faces = {}
for (gx, gz) in CELLS:
    h, gr = G.h(gx, gz), grade[(gx, gz)]
    for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if (gx + dx, gz + dz) in CELLS:
            continue
        if dx:
            key = ('x', gx + (1 if dx > 0 else 0), -dx)
            faces.setdefault(key, {})[gz] = (h, gr)
        else:
            key = ('z', gz + (1 if dz > 0 else 0), -dz)
            faces.setdefault(key, {})[gx] = (h, gr)

WALLRUNS = []                # (axis, line, nrm, a0, a1, h_modules, grade)
for (axis, line, nrm), col in faces.items():
    for a in sorted(col):
        h, gr = col[a]
        if (WALLRUNS and WALLRUNS[-1][0] == axis and WALLRUNS[-1][1] == line
                and WALLRUNS[-1][2] == nrm and WALLRUNS[-1][4] == a
                and WALLRUNS[-1][5] == h and WALLRUNS[-1][6] == gr):
            WALLRUNS[-1][4] = a + 1
        else:
            WALLRUNS.append([axis, line, nrm, a, a + 1, h, gr])

# yaw that turns a part's own +Z into the direction the wall faces
YAW = {('x', -1): -1.5707963, ('x', 1): 1.5707963,
       ('z', -1): 3.1415927, ('z', 1): 0.0}


def wall_pt(axis, line, nrm, along, o, y):
    """A point on a wall run: `along` in grid units, `o` metres into the
       room, `y` metres up."""
    if axis == 'x':
        return (wxf(line) + nrm * o, y, wzf(along))
    return (wxf(along), y, wzf(line) + nrm * o)


# ==========================================================================
#  3.  THE FULL KIT, ON THE ROUTE
# ==========================================================================
g('/* ================================================================')
g('   THE ROUTE -- deck plan v6 assembled module by module.')
g('   The section is an octagon: deck, splay, wall, splay, overhead, and')
g('   not one right angle in it.  The deck is zoned -- margin, field,')
g('   band -- and every lit thing in here is also a lamp.')
g('   ================================================================ */')

CHANNEL_YAW = {(-1, 0): 0.0, (1, 0): 3.1415927,
               (0, 1): 1.5707963, (0, -1): -1.5707963}

for (gx, gz) in sorted(FULL):
    hm = G.h(gx, gz)
    x, z, h = K.cx(gx), K.cz(gz), hm * U
    al, ac = along_of[(gx, gz)], across_of[(gx, gz)]
    v = (al * 7 + ac * 11) & 15
    wallside = touches_wall(gx, gz)
    band = (al % 5 == 4)

    # ---- the deck ---------------------------------------------------
    # MARGIN against each wall, channel outboard; FIELD in the middle;
    # BAND across the full width every fifth module.  Zones read because
    # they have edges -- a rule applied to every tile is a texture.
    if band:
        mod(FLOOR2, x, 0.0, z, 0.0, v, 'FLOOR')
    elif wallside:
        kk = FLOOR3 if (al % 17 == 6 and ac % 2 == 0) else FLOOR4
        mod(kk, x, 0.0, z, CHANNEL_YAW[wallside], v, 'FLOOR')
    else:
        mod(FLOOR1, x, 0.0, z, 0.0, v, 'FLOOR')

    # the safety band is a line of blocks laid one module ahead of every
    # vent band, so the yellow is a warning about the thing behind it
    if al % 5 == 3:
        mod(BAND1, x, 0.0, z, 0.0 if axis_of[(gx, gz)] == 'z' else 1.5707963,
            0, 'MACH')

    # ---- the overhead ------------------------------------------------
    # Zoned like the deck: the outer runs always carry the return-air
    # louvres, the inner runs never do and are the ones that carry the
    # lit channel on alternate modules.
    # A LIT PANEL MUST ALSO BE A LAMP.  The lit channel and the lamp that
    # stands in it share one beat -- on a different one, two panels in
    # three glow in the shader and light nothing at all.
    outer = wallside is not None
    mod(CEILING1, x, h, z, 0.0,
        (6 if outer else (1 if al % LAMP_BEAT == 0 else 0)), 'COAT')

# ---- the wall runs -------------------------------------------------------
for (axis, line, nrm, a0, a1, hm, gr) in WALLRUNS:
    if gr != 2:
        continue
    n = a1 - a0
    yaw = YAW[(axis, nrm)]
    bh = min(hm * U, BAY_H)
    # the two sides of a passage start at different points in the sequence
    q = (a0 + line + (0 if nrm > 0 else 2)) % len(SEQ)
    i = 0
    while i < n:
        kk, m = SEQ[q % len(SEQ)]
        if i + m > n:
            kk, m = WALL1, 1
        p = wall_pt(axis, line, nrm, a0 + i + m / 2.0, 0.0, 0.0)
        mod(kk, p[0], 0.0, p[2], yaw, bh, 'COAT')
        i += m
        q += 1
    # the splays go in every module without exception -- the rule is only
    # worth having if it is
    for i in range(n):
        p = wall_pt(axis, line, nrm, a0 + i + 0.5, 0.0, 0.0)
        mod(SKIRT1, p[0], 0.0, p[2], yaw, 0, 'MACH')
        mod(COVE1, p[0], bh - 0.3, p[2], yaw, 0, 'MACH')

# ---- the corners ---------------------------------------------------------
# Every grid vertex with three cells open and one solid is an OUTSIDE
# corner and gets the post; three solid and one open is an INSIDE corner
# and gets the fillet.  Both take the same yaw: the bisector into the open
# space, less the 45 degrees the part is already built on.
BIS = {(1, 1): 0.7853982, (1, -1): 2.3561945,
       (-1, -1): 3.9269908, (-1, 1): 5.4977871}
vxs = set()
for (gx, gz) in FULL:
    for v in ((gx, gz), (gx + 1, gz), (gx, gz + 1), (gx + 1, gz + 1)):
        vxs.add(v)
ncorner = nfillet = 0
for (vx, vz) in sorted(vxs):
    quad = {(1, 1): (vx, vz), (-1, 1): (vx - 1, vz),
            (1, -1): (vx, vz - 1), (-1, -1): (vx - 1, vz - 1)}
    op = [d for d in quad if quad[d] in CELLS]
    if not any(quad[d] in FULL for d in op):
        continue
    hs = [G.h(*quad[d]) for d in op]
    if len(op) == 3:                                   # outside corner
        solid = [d for d in quad if quad[d] not in CELLS][0]
        d = (-solid[0], -solid[1])
        mod(CORNER1, wxf(vx), 0.0, wzf(vz), BIS[d] - 0.7853982,
            min(min(hs) * U, BAY_H), 'MACH')
        ncorner += 1
    elif len(op) == 1:                                 # inside corner
        mod(FILLET1, wxf(vx), 0.0, wzf(vz), BIS[op[0]] - 0.7853982,
            min(hs[0] * U, BAY_H), 'MACH')
        nfillet += 1


# ==========================================================================
#  4.  THE SHELL
#      The same section, drawn per run.  A 20 m hull side costs one wall
#      brush, two splay quads and two emissive strips instead of seventeen
#      bays, seventeen skirts and seventeen coves.
# ==========================================================================
def decompose(cells):
    """greedy maximal rectangles over a set of (gx,gz)"""
    left, out = set(cells), []
    while left:
        gx, gz = min(left)
        w = 1
        while (gx + w, gz) in left:
            w += 1
        h = 1
        while all((gx + i, gz + h) in left for i in range(w)):
            h += 1
        for j in range(h):
            for i in range(w):
                left.discard((gx + i, gz + j))
        out.append((gx, gz, w, h))
    return out


g('/* ---- decks, overheads and hull --------------------------------- */')
# by (grade, height) so a rectangle never straddles a ceiling step
bucket = {}
for c in CELLS:
    bucket.setdefault((grade[c], G.h(*c)), []).append(c)

# ---- the store's own hole: SKYLIGHT 1 (K20) needs the ceiling brush
# left open where it lands, the same bargain WALL 5 makes with the wall
# it is stamped on.  6 modules across -- matches SK_W in tools/kit.js
# exactly, so the module the part is built on is the module the hole is
# cut on -- and centred on the room, in grid cells so it always lands on
# a module line no matter where the room itself falls.
STORE_RI = next(i for i, R in enumerate(G.REG) if R['key'] == 'MAINT')
_gcells = [c for c in CELLS if G.reg[c] == STORE_RI]
_gx0, _gx1 = min(c[0] for c in _gcells), max(c[0] for c in _gcells) + 1
_gz0, _gz1 = min(c[1] for c in _gcells), max(c[1] for c in _gcells) + 1
SKY_MOD = 6
SKY_GX0 = (_gx0 + _gx1 - SKY_MOD) // 2
SKY_GZ0 = (_gz0 + _gz1 - SKY_MOD) // 2
SKY_HOLE = {(gx, gz) for gx in range(SKY_GX0, SKY_GX0 + SKY_MOD)
                     for gz in range(SKY_GZ0, SKY_GZ0 + SKY_MOD)}
SKY_CX = wxf(SKY_GX0 + SKY_MOD / 2.0)
SKY_CZ = wzf(SKY_GZ0 + SKY_MOD / 2.0)
SKY_H = G.h(SKY_GX0, SKY_GZ0) * U

nslab = 0
for (gr, hm), cells in sorted(bucket.items()):
    h = hm * U
    for (gx, gz, w, d) in decompose(cells):
        x0, z0, x1, z1 = wxf(gx), wzf(gz), wxf(gx + w), wzf(gz + d)
        # Under the route the sub-deck is DARK: it is what shows in the
        # reveals between the plates, and a bright one fills them in.
        # Off it there is no plate to reveal anything, so the slab IS the
        # deck and wears the bay -- one texture tile to the module, which
        # is the same 1.200 m the kit lays its floor parts on.
        bx(x0, -WT, z0, x1, 0.0, z1, 'DARK' if gr == 2 else 'BDECK')
        nslab += 1
    # the overhead: same cells, minus that hole.  Decomposed
    # separately from the deck above because a hole in the ceiling is not
    # a hole in the floor under it.
    for (gx, gz, w, d) in decompose([c for c in cells if c not in SKY_HOLE]):
        x0, z0, x1, z1 = wxf(gx), wzf(gz), wxf(gx + w), wzf(gz + d)
        # Likewise overhead: under the route CEILING 1 hangs below this slab
        # and the slab is just the structure behind it; off the route the
        # slab is the only overhead there is.
        bx(x0, h, z0, x1, h + WT, z1, 'DARK' if gr == 2 else 'BPAN')
        nslab += 1

mod(20, SKY_CX, SKY_H, SKY_CZ, 0.0, 0, 'BPAN')

g('/* ---- the hull behind every wall face --------------------------- */')
for (axis, line, nrm, a0, a1, hm, gr) in WALLRUNS:
    h = hm * U
    # o runs OUTWARD from the wall plane, which is why both ends pass a
    # depth and not a height -- the y range is given to bx directly.
    p0 = wall_pt(axis, line, nrm, a0, -0.6, 0.0)
    p1 = wall_pt(axis, line, nrm, a1, 0.0, 0.0)
    # DARK behind the route, because what shows there is the reveal between
    # two bays and a bright one fills it in.
    if gr == 2:
        bx(p0[0], -WT, p0[2], p1[0], h + WT, p1[2], 'DARK')
        continue
    # A SHELL WALL IS A DADO AND A FIELD.  PANEL draws a plate grid into
    # the texture for nothing, which is the trade a shell wall wants -- but
    # over a 20 m run with no vertical structure it stops reading as plate
    # and starts reading as brickwork.  So it stops at the bay height the
    # kit's own wall stops at, a string course closes it, and the wall
    # above is plain: which is what the route does too, where a room is
    # taller than a bay.
    #
    # PANEL and COAT were the wrong two materials for it and that is most of
    # what a shell room looks like.  PANEL is `Machined panel`, whose plates
    # are 540 mm and BONDED -- offset half a plate a course, which is right
    # for one bay of corridor and reads as brickwork the moment the run is
    # twenty metres.  COAT is a paint with no relief in it at all, so the
    # band above the string course came out as pale plaster.  Both are
    # replaced by the bay set: one plate to the module, fastened down its
    # seams, and a quiet dark hull above.
    dado = min(h, BAY_H)
    bx(p0[0], -WT, p0[2], p1[0], dado, p1[2], 'BPLATE')
    if h > dado + 0.01:
        bx(p0[0], dado, p0[2], p1[0], dado + 0.18, p1[2], 'MACH')
        bx(p0[0], dado + 0.18, p0[2], p1[0], h + WT, p1[2], 'BHULL')
    else:
        bx(p0[0], dado, p0[2], p1[0], h + WT, p1[2], 'BHULL')

# ---- lintels: where an open cell looks into a lower one -------------------
g('/* ---- lintels over the height steps ------------------------------ */')
steps = {}
for (gx, gz) in CELLS:
    h = G.h(gx, gz)
    for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        n = (gx + dx, gz + dz)
        if n not in CELLS or G.h(*n) >= h:
            continue
        if dx:
            steps.setdefault(('x', gx + (1 if dx > 0 else 0), -dx,
                              G.h(*n), h), []).append(gz)
        else:
            steps.setdefault(('z', gz + (1 if dz > 0 else 0), -dz,
                              G.h(*n), h), []).append(gx)
nlint = 0
for (axis, line, nrm, h0, h1), runs in steps.items():
    runs.sort()
    a = 0
    while a < len(runs):
        b = a
        while b + 1 < len(runs) and runs[b + 1] == runs[b] + 1:
            b += 1
        p0 = wall_pt(axis, line, nrm, runs[a], 0.12, 0.0)
        p1 = wall_pt(axis, line, nrm, runs[b] + 1, 0.0, -0.12)
        bx(p0[0], h0 * U, p0[2], p1[0], h1 * U, p1[2], 'COAT')
        bx(p0[0], h0 * U - 0.16, p0[2], p1[0], h0 * U, p1[2], 'MACH')
        nlint += 2
        a = b + 1

# ---- the section, per shell run ------------------------------------------
# SKIRT 1's own numbers: the splay stops 144 mm above the deck and stands
# on a recessed plinth, and the amber strip lives in the slot that leaves.
# What you see is not the strip but a pool on the deck the length of the
# run -- and that is the whole lighting design of the reference in one move.
g('/* ---- the shell section: plinth, splay, the two lit lines -------- */')
LIVE_L = '0.30,0.86,1.10'        # cyan: THIS SYSTEM IS STILL RUNNING
nsplay = nevent = 0
for (axis, line, nrm, a0, a1, hm, gr) in WALLRUNS:
    if gr != 1 or a1 - a0 < 1:
        continue
    h = hm * U
    A0, A1 = a0 + 0.02, a1 - 0.02

    def face(o0, y0, o1, y1, mat, nx):
        c = [wall_pt(axis, line, nrm, A0, o0, y0),
             wall_pt(axis, line, nrm, A1, o0, y0),
             wall_pt(axis, line, nrm, A1, o1, y1),
             wall_pt(axis, line, nrm, A0, o1, y1)]
        e1 = [c[1][k] - c[0][k] for k in range(3)]
        e2 = [c[3][k] - c[0][k] for k in range(3)]
        cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
              e1[0] * e2[1] - e1[1] * e2[0])
        if sum(cr[k] * nx[k] for k in range(3)) < 0:
            c[1], c[3] = c[3], c[1]
        qd(c, mat)

    def box(o0, y0, o1, y1, mat, b0=None, b1=None):
        p0 = wall_pt(axis, line, nrm, A0 if b0 is None else b0, o0, y0)
        p1 = wall_pt(axis, line, nrm, A1 if b1 is None else b1, o1, y1)
        bx(p0[0], y0, p0[2], p1[0], y1, p1[2], mat)

    inw = (nrm, 0.0, 0.0) if axis == 'x' else (0.0, 0.0, nrm)
    up = (inw[0] * 0.7071, 0.7071, inw[2] * 0.7071)
    dn = (inw[0] * 0.7071, -0.7071, inw[2] * 0.7071)

    box(0.0, 0.0, 0.49, 0.144, 'MACH')                    # the plinth
    face(0.60, 0.144, 0.144, 0.60, 'LOUV', up)            # the deck splay
    box(0.470, 0.052, 0.546, 0.100, 'GLOW')               # amber, in the slot
    # the deck margin: FLOOR 4's channel, drawn as one strip for the run.
    # Without it a shell room is a single flat material from wall to wall
    # and the zoning the route is built on stops at the doorway.
    box(0.60, 0.000, 1.20, 0.012, 'GRATE')
    box(0.136, h - 0.272, 0.240, h - 0.224, 'MACH')       # the cove's lip
    box(0.112, h - 0.244, 0.172, h - 0.194, 'CHAN')       # cold, above
    face(0.128, h - 0.156, 0.284, h, 'COAT', dn)          # the overhead splay
    # the two rails that give a plain plate field its horizontal grain
    box(0.0, 0.880, 0.055, 0.940, 'TRIM')
    if h >= 3.0:
        box(0.0, 2.368, 0.075, 2.432, 'RIB')              # on the 2U line
    nsplay += 8
    # THE 4U BEAT, WHICH IS THE ONE THE KIT ALREADY HAS.  WALL 4 is four
    # modules wide, so the route is a wall divided every 4.8 m whether you
    # count it or not.  A shell run without that division is a texture, and
    # a texture at 20 m long reads as masonry.  One rib on the same beat
    # costs a brush and puts the two grades on one rhythm.
    for a in range(a0 + 4 - (a0 % 4), a1, 4):
        if a <= a0 or a >= a1:
            continue
        box(0.0, 0.0, 0.11, h, 'MACH', a - 0.075, a + 0.075)
        box(0.11, 0.30, 0.15, h - 0.30, 'RIB', a - 0.045, a + 0.045)
        nsplay += 2

    # A WALL RUN NEEDS EVENTS, NOT JUST A BEAT.  The route's wall is a
    # SEQUENCE -- WALL 4 for four modules, WALL 2 for two, then WALL 1 and
    # WALL 3 as one-module events -- and WALL 3 is an intake with real
    # slats.  The shell had the beat and not the sequence: one plate,
    # repeated, between identical ribs, which is the last surface in the
    # ship still reading as texture rather than as wall.
    #
    # Nothing here is recessed, and that is not a compromise: the shell
    # wall is one solid brush, so there is nothing to carve into, and a
    # bulkhead-mounted box is what a ship does anyway.  Each event is a
    # backing plate with its face proud of it -- the same two-brush trick
    # the deck lids use, and it gets a bright frame for free.
    #
    # R6 IS THE POINT.  Two bays in six get an event and four stay bare.
    # An event in every bay is a beat, and we already have a beat.
    for a in range(a0, a1, 4):
        b0e, b1e = max(a0, a), min(a1, a + 4)
        if b1e - b0e < 3:
            continue
        c = (b0e + b1e) / 2.0
        k = (int(line) * 31 + b0e * 17 + (3 if nrm > 0 else 7)) % 6
        if k == 0:
            # the intake.  LOUVRE BANK draws eight slots and a bezel, so a
            # 2.4 m plate of it is the WALL 3 the route puts here.
            box(0.0, 0.92, 0.085, 2.28, 'MACH', c - 1.06, c + 1.06)
            box(0.085, 1.00, 0.125, 2.20, 'LOUV', c - 0.98, c + 0.98)
            box(0.125, 1.02, 0.165, 1.24, 'TRIM', c - 0.94, c + 0.94)
            nevent += 3
        elif k == 1:
            # the access lid.  Ochre on its edge -- OCHRE MEANS THIS COMES
            # OFF, and it is the only role the colour has.
            box(0.0, 0.70, 0.055, 2.14, 'MACH', c - 0.86, c + 0.86)
            box(0.055, 0.76, 0.105, 2.08, 'ACC', c - 0.80, c + 0.80)
            box(0.105, 0.76, 0.128, 0.80, 'SAFE', c - 0.80, c + 0.80)
            nevent += 3
        elif k == 2:
            # the readout, in a bezel, with a conduit dropping out of it.
            # ONE IN EIGHT IS AWAKE: the ratio of lit panels to dark ones is
            # the whole mood dial, and this is a derelict.
            box(0.0, 1.28, 0.06, 2.06, 'MACH', c - 0.62, c + 0.62)
            lit = (int(line) * 13 + b0e) % 8 == 0
            box(0.06, 1.36, 0.10, 1.98, 'READ' if lit else 'GAP',
                c - 0.54, c + 0.54)
            box(0.0, 0.0, 0.075, 1.28, 'DUCT', c + 0.30, c + 0.46)
            nevent += 3
            if lit:
                # a lit panel must also be a lamp
                pl = wall_pt(axis, line, nrm, c, 0.55, 0.0)
                light(pl[0], 1.68, pl[2], LIVE_L, 3.2)


# ==========================================================================
#  4b.  THE SHELL'S STRUCTURE
#
#  Section 4 gets the walls of a shell room right and leaves the ROOM wrong,
#  because a room is not its walls.  Three things were missing and all three
#  are the same mistake -- treating "cheap" as "per module, but less", when
#  the shell's whole bargain is "per run, and different":
#
#    * no room closes its own corners.  NO RIGHT ANGLE IS LEFT OPEN is the
#      rule the section is built on, and it was being kept in elevation and
#      broken in plan: every room met its own walls at a bare 90 degrees.
#    * the engine room is 46.8 m of clear span with nothing standing in it.
#      No ship has that.  A column every six modules is what the span is
#      actually carried on, and it is the single thing that turns a box
#      into an interior.
#    * the deck is one field wall to wall.  A DECK IS ZONED, NOT SPECKLED:
#      margin, field, band.  The margin is drawn (section 4's grate strip),
#      the field is the bay material, and the band was missing entirely.
#
#  All of it is brushes, quads and -- for the columns, where the silhouette
#  is the whole point and a box would read as a box -- the kit's own part.
# ==========================================================================
g('/* ---- 4b: the shell\'s own structure ------------------------------ */')

# ---- every right angle closed, in plan as well as in section -------------
# The route posts its corners with CORNER 1 and FILLET 1, module by module.
# The shell cannot afford that and does not need to: a corner is one place,
# not a run, so it gets one stanchion -- a 360 mm post centred on the grid
# vertex, exactly where the kit part goes, with its exposed arrises cut off
# at 45 degrees so the post itself keeps the rule it is there to enforce.
CH = 0.36                        # the post, across flats
FOOT = 0.62                      # its base -- reaches past the splay


def arris(px, pz, dx, dz, y0, y1, a, cc, mat):
    """The 45 degree cut across one corner of a square block of half-extent
       `a` at (px,pz).  `d` says which corner: the quad runs from
       (px+dx*a, pz+dz*(a-cc)) to (px+dx*(a-cc), pz+dz*a), facing along d."""
    CC = cc
    c = [(px + dx * a, y0, pz + dz * (a - CC)),
         (px + dx * (a - CC), y0, pz + dz * a),
         (px + dx * (a - CC), y1, pz + dz * a),
         (px + dx * a, y1, pz + dz * (a - CC))]
    e1 = [c[1][k] - c[0][k] for k in range(3)]
    e2 = [c[3][k] - c[0][k] for k in range(3)]
    n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
         e1[0] * e2[1] - e1[1] * e2[0])
    if n[0] * dx + n[2] * dz < 0:                      # face into the room
        c[1], c[3] = c[3], c[1]
    qd(c, mat)


VXS = set()
for (gx, gz) in CELLS:
    for v in ((gx, gz), (gx + 1, gz), (gx, gz + 1), (gx + 1, gz + 1)):
        VXS.add(v)
npost = 0
for (vx, vz) in sorted(VXS):
    quad_ = {(1, 1): (vx, vz), (-1, 1): (vx - 1, vz),
             (1, -1): (vx, vz - 1), (-1, -1): (vx - 1, vz - 1)}
    op = [d for d in quad_ if quad_[d] in CELLS]
    if len(op) not in (1, 3):
        continue                          # a straight run has no corner
    if any(quad_[d] in FULL for d in op):
        continue                          # the route already posts this one
    px, pz = wxf(vx), wzf(vz)
    h = min(G.h(*quad_[d]) for d in op) * U
    # THE FOOT IS THE POINT OF IT.  The section's deck splay runs 600 mm out
    # from every wall plane, so where two walls meet, two splays meet -- and
    # they meet as two mitred slopes with a hole behind them and nothing in
    # the corner at all.  A 360 mm post reaches 180 mm and does not touch
    # it.  The foot is 620 mm out, which is 20 mm PROUD of the splay it
    # closes: standing slightly forward is how a stanchion base reads as
    # bearing on the deck rather than as let into it.
    # The foot is COATED and the shaft is CUT.  `Machined face` is bare
    # bright steel and it is the right finish for a 360 mm post -- it is
    # what makes the post the highlight in the corner.  On the 1.24 m foot
    # it was the largest and palest surface in the room, and its micro
    # pitting at that size stops reading as a machined finish and starts
    # reading as cast stone.  A stanchion base is painted; only the section
    # is bare.
    bx(px - FOOT, -WT, pz - FOOT, px + FOOT, 0.155, pz + FOOT, 'COAT')
    bx(px - 0.465, 0.155, pz - 0.465, px + 0.465, 0.62, pz + 0.465, 'COAT')
    bx(px - 0.50, 0.126, pz - 0.50, px + 0.50, 0.155, pz + 0.50, 'TRIM')
    bx(px - CH / 2, -WT, pz - CH / 2, px + CH / 2, h, pz + CH / 2, 'MACH')
    # the shaft is banded on the kit's own two lines, so a shell post and a
    # route post are read as the same fitting from across a room
    for yb in (0.880, 2.368):
        if yb + 0.09 < h:
            bx(px - CH / 2 - 0.035, yb, pz - CH / 2 - 0.035,
               px + CH / 2 + 0.035, yb + 0.062, pz + CH / 2 + 0.035, 'RIB')
    # no right angle is left open -- on the shaft, and on the foot that
    # closes the splays, and on the swell between them
    for d in op:                          # only the arrises that show
        arris(px, pz, d[0], d[1], -WT, h, CH / 2, 0.105, 'MACH')
        arris(px, pz, d[0], d[1], -WT, 0.155, FOOT, 0.150, 'COAT')
        arris(px, pz, d[0], d[1], 0.155, 0.62, 0.465, 0.130, 'COAT')
    npost += 1

# ---- what the span is carried on -----------------------------------------
# COLUMN 1 is a kit part and it stays one.  This is the one place in the
# shell where a brush will not do: a column is read by its SILHOUETTE, it
# stands in the middle of the room with light on every side of it, and a
# box with a texture on it reads as a box.  Six modules apart -- 7.2 m,
# which is the span a deck beam of this depth actually carries -- and never
# within two modules of anything solid, so no column ever lands in a
# doorway or against a wall.
COL_BEAT = 6
COL_L = '0.62,0.82,1.00'    # the flange channel is a cool strip
ncol = 0
for i, R in enumerate(G.REG):
    if R['kind'] != 'room':
        continue
    cells = [c for c in CELLS if G.reg[c] == i and c not in FULL]
    if len(cells) < 40:
        continue
    cs = set(cells)
    xs = [c[0] for c in cells]; zs = [c[1] for c in cells]
    for gx in range(min(xs), max(xs) + 1):
        for gz in range(min(zs), max(zs) + 1):
            if (gx - min(xs)) % COL_BEAT or (gz - min(zs)) % COL_BEAT:
                continue
            if (gx, gz) not in cs:
                continue
            clear = all((gx + a, gz + b) in cs
                        for a in (-2, -1, 0, 1, 2) for b in (-2, -1, 0, 1, 2))
            if not clear:
                continue
            ch = G.h(gx, gz) * U
            mod(COLUMN1, K.cx(gx), 0.0, K.cz(gz), 0.0, ch, 'MACH')
            # A LIT PANEL MUST ALSO BE A LAMP.  COLUMN 1's flanges are a lit
            # channel -- that is the part's whole idea -- and emissive
            # geometry lights nothing on its own, so the channel gets the
            # lamp that makes it true.
            light(K.cx(gx), ch * 0.52, K.cz(gz), COL_L, 5.4)
            ncol += 1

# ---- the deck's other metal ----------------------------------------------
# The route's deck is not one part repeated: FLOOR 1 is the field, FLOOR 2
# is the band, FLOOR 3 and 4 carry the margin channel, and the alternation
# between them is the only reason a corridor of identical modules does not
# read as squared paper.  The shell deck had one material and no
# alternation at all, so every room was 600 m2 of the same plate.
#
# One module in nine gets a DIFFERENT metal, let into the deck 60 mm inside
# the cell so a reveal shows round it -- a grating, an access lid, a
# perforated pan, a patch of bare rolled plate.  Chosen by a hash of the
# cell, not at random, so the same map builds the same way twice.
OTHER = ['GRATE', 'PERF', 'ACC', 'SKIN', 'PERF', 'GRATE']
nplate = 0
for (gx, gz) in sorted(CELLS):
    if (gx, gz) in FULL:
        continue
    k = (gx * 73856093) ^ (gz * 19349663)
    k = (k >> 4) & 63
    if k >= 5:
        continue
    x, z = K.cx(gx), K.cz(gz)
    m = OTHER[(gx * 5 + gz * 3) % len(OTHER)]
    # A BRIGHT FRAME, THEN THE OTHER METAL ON TOP OF IT.  Two stacked
    # plates, the upper one inset further, so what shows round the lid is a
    # 35 mm ring of the same clean steel the deck plates are rimmed in --
    # which is what keeps a patch of grating from reading as a hole.
    bx(x - U / 2 + 0.040, 0.0, z - U / 2 + 0.040,
       x + U / 2 - 0.040, 0.016, z + U / 2 - 0.040, 'TRIM')
    bx(x - U / 2 + 0.075, 0.016, z - U / 2 + 0.075,
       x + U / 2 - 0.075, 0.030, z + U / 2 - 0.075, m)
    nplate += 2

# ---- the threshold, where the two grades meet ----------------------------
# Every place a shell cell touches a route cell is a doorway in everything
# but name: the ceiling steps, the deck material changes and the wall
# sequence restarts.  A ship marks that, and marking it does the other job
# too -- it stitches the two grades together instead of letting the seam
# between them be the first thing you notice.
nthresh = 0
for (gx, gz) in sorted(CELLS):
    if (gx, gz) in FULL:
        continue
    for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if (gx + dx, gz + dz) not in FULL:
            continue
        x, z = K.cx(gx), K.cz(gz)
        if dx:
            ex = wxf(gx + (1 if dx > 0 else 0))
            bx(ex - dx * 0.44, 0.0, z - U / 2 + 0.02,
               ex - dx * 0.09, 0.014, z + U / 2 - 0.02, 'SAFE')
        else:
            ez = wzf(gz + (1 if dz > 0 else 0))
            bx(x - U / 2 + 0.02, 0.0, ez - dz * 0.44,
               x + U / 2 - 0.02, 0.014, ez - dz * 0.09, 'SAFE')
        nthresh += 1

# ---- the band the deck was missing ---------------------------------------
# One module wide, across the room's short way, every sixth module along its
# long one -- a service trench, which is what a band on a real deck is.  It
# is the FIELD that needs the band, not the band that needs the field: a
# 46 m deck of identical plates has no scale in it until something crosses.
BAND_BEAT = 6
nband = 0
for i, R in enumerate(G.REG):
    cells = [c for c in CELLS if G.reg[c] == i and c not in FULL]
    if len(cells) < 30:
        continue
    cs = set(cells)
    xs = [c[0] for c in cells]; zs = [c[1] for c in cells]
    alongx = (max(xs) - min(xs)) >= (max(zs) - min(zs))
    a0, a1 = (min(xs), max(xs)) if alongx else (min(zs), max(zs))
    b0, b1 = (min(zs), max(zs)) if alongx else (min(xs), max(xs))
    def at(a, b):
        return ((a, b) if alongx else (b, a)) in cs

    for a in range(a0 + 3, a1 - 1, BAND_BEAT):
        b = b0
        while b <= b1:
            if not at(a, b):
                b += 1
                continue
            e = b
            while e + 1 <= b1 and at(a, e + 1):
                e += 1
            if alongx:
                x0, x1 = wxf(a) + 0.30, wxf(a + 1) - 0.30
                z0, z1 = wzf(b) + 0.10, wzf(e + 1) - 0.10
            else:
                x0, x1 = wxf(b) + 0.10, wxf(e + 1) - 0.10
                z0, z1 = wzf(a) + 0.30, wzf(a + 1) - 0.30
            bx(x0, -0.03, z0, x1, 0.016, z1, 'GRATE')     # the trench itself
            bx(x0 - 0.075, 0.0, z0, x0, 0.030, z1, 'MACH')   # its two lips
            bx(x1, 0.0, z0, x1 + 0.075, 0.030, z1, 'MACH')
            # AND THE WARNING GOES IN FRONT OF THE THING IT WARNS ABOUT.
            # The route lays its safety band one module ahead of every vent
            # band for exactly this reason; here it is one step ahead of the
            # trench, so the yellow is about the hole and not decoration.
            if alongx:
                bx(wxf(a) - 0.62, 0.0, z0, wxf(a) - 0.27, 0.014, z1, 'SAFE')
            else:
                bx(x0, 0.0, wzf(a) - 0.62, x1, 0.014, wzf(a) - 0.27, 'SAFE')
            nband += 4
            b = e + 1

# ---- the string course, where a room is taller than a bay ----------------
for (axis, line, nrm, a0, a1, hm, gr) in WALLRUNS:
    if gr != 2 or hm * U <= BAY_H + 0.01:
        continue
    p0 = wall_pt(axis, line, nrm, a0, 0.34, 0.0)
    p1 = wall_pt(axis, line, nrm, a1, 0.0, 0.0)
    bx(p0[0], BAY_H, p0[2], p1[0], BAY_H + 0.22, p1[2], 'MACH')


# ==========================================================================
#  5.  LIGHT
#      A LIT PANEL IS A LAMP.  The skirts and the coves emit, which gives
#      them their own glow and a soft local wash and no more -- so every
#      lit overhead run carries a real lamp, and nothing is lit from
#      anywhere a fitting does not exist.
# ==========================================================================
CEIL_L = '1.06,1.07,1.06'
KICK_L = '2.40,1.12,0.26'
# THE TWO GRADES HAVE TO BE ONE SHIP.  This was 0.86,0.92,1.04 -- a cool
# white, brighter than anything on the route -- and the effect was that
# walking through a doorway changed the ship's colour temperature: the
# corridor is a warm dark and the room behind it was an office.  The ship's grey is
# a WARM dark, the light does the colouring, and a derelict's rooms are the
# darkest part of it because nobody is in them.
ROOM_L = '0.92,0.885,0.835'   # warm, and the same warm the route is lit in
WARM_L = '1.60,0.86,0.30'

g('/* ---- lamps ------------------------------------------------------ */')
# the route: a lamp in the lit overhead channel every second module, and a
# small warm one clear of the skirt every fourth
lit_rows = {}
for (gx, gz) in FULL:
    if touches_wall(gx, gz):
        continue
    lit_rows.setdefault((axis_of[(gx, gz)], along_of[(gx, gz)]), []).append((gx, gz))
for (ax, al), cells in sorted(lit_rows.items()):
    if al % LAMP_BEAT:
        continue
    cells.sort()
    gx, gz = cells[len(cells) // 2]
    light(K.cx(gx), G.h(gx, gz) * U - 0.34, K.cz(gz), CEIL_L, 6.8)
for (gx, gz) in sorted(FULL):
    w = touches_wall(gx, gz)
    if not w or along_of[(gx, gz)] % 6 != 1:
        continue
    # 3.0 m PUT THE AMBER NOWHERE.  The falloff is (1-d/r) squared, so the
    # radius is not "how far you can see it" but how fast it dies: at r=3.0
    # a kick light two metres along the skirt was at 11% of itself, and the
    # deck plate a stride away from an amber strip was the same grey as a
    # deck plate nowhere near one.  4.6 m puts that same point at 34%, and
    # the pool finally reaches the floor it is standing on.  It does not
    # raise the peak -- that is the colour, and the colour has not moved.
    light(K.cx(gx) - w[0] * 0.55, 0.26, K.cz(gz) - w[1] * 0.55, KICK_L, 4.6)

# the shell: one lamp per rectangle, on a coarse grid, so a big room is lit
# from several places rather than from its middle -- and each one hangs in a
# fitting, because a light with no lamp in it is the same lie as a lamp with
# no light in it
nfit = nline = 0
for (gr, hm), cells in sorted(bucket.items()):
    if gr != 1:
        continue
    h = hm * U
    for (gx, gz, w, d) in decompose(cells):
        rows = {}
        for j in range(0, d, 6):
            for i in range(0, w, 6):
                cw, cd = min(6, w - i), min(6, d - j)
                if cw * cd < 6:
                    continue
                # ...and it does not reach as far.  A 13 m radius from the
                # middle of every 6x6 block is a flood, and a flood is what
                # flattens a room -- the far wall comes up as bright as the
                # near one and the space loses its depth.  Doom 3 hides
                # distance in BLACKNESS, not in fog.
                lx = K.cx(gx + i + (cw - 1) / 2.0)
                lz = K.cz(gz + j + (cd - 1) / 2.0)
                light(lx, h - 0.42, lz, ROOM_L,
                      max(7.5, min(12.0, 2.2 * max(cw, cd))))
                # AND A LAMP MUST ALSO BE A FITTING.  The rule is normally
                # read the other way round -- a lit panel has to carry a
                # real light -- but a shell room broke it in reverse: a
                # pool of light on the deck and NOTHING overhead making it.
                # A caged fitting, which is the reference's own: a housing,
                # a bright face let into it, and three bars across that.
                # The bars breaking the pool up are what separate a lamp
                # from a glowing rectangle.
                bx(lx - 0.62, h - 0.16, lz - 0.30,
                   lx + 0.62, h, lz + 0.30, 'MACH')
                bx(lx - 0.54, h - 0.185, lz - 0.225,
                   lx + 0.54, h - 0.145, lz + 0.225, 'LAMP')
                for k in (-1, 0, 1):
                    bx(lx + k * 0.34 - 0.028, h - 0.205, lz - 0.235,
                       lx + k * 0.34 + 0.028, h - 0.155, lz + 0.235, 'MACH')
                nfit += 1
                rows.setdefault(lz if w >= d else lx, []).append(lx if w >= d else lz)
        # ---- and the line the fittings hang on --------------------------
        # A CONTINUOUS LIT LINE IS THE STRONGEST SCI-FI CUE THERE IS, and
        # the route has one down every overhead while the shell had only
        # the fittings -- lamps floating in a dark ceiling with nothing
        # connecting them.  The line runs THROUGH the fittings, on their
        # own row, so the lit thing and the thing making light are one
        # object and the beat is the same beat.
        for key in sorted(rows):
            if w >= d:
                bx(wxf(gx) + 0.42, h - 0.150, key - 0.105,
                   wxf(gx + w) - 0.42, h, key + 0.105, 'MACH')
                bx(wxf(gx) + 0.46, h - 0.178, key - 0.052,
                   wxf(gx + w) - 0.46, h - 0.140, key + 0.052, 'CHAN')
            else:
                bx(key - 0.105, h - 0.150, wzf(gz) + 0.42,
                   key + 0.105, h, wzf(gz + d) - 0.42, 'MACH')
                bx(key - 0.052, h - 0.178, wzf(gz) + 0.46,
                   key + 0.052, h - 0.140, wzf(gz + d) - 0.46, 'CHAN')
            nline += 2
# and a warm one at the foot of every shell run, because the amber line is
# emissive geometry and emissive geometry lights nothing on its own
for (axis, line, nrm, a0, a1, hm, gr) in WALLRUNS:
    if gr != 1 or a1 - a0 < 3:
        continue
    for a in range(a0 + 1, a1 - 1, 9):
        p = wall_pt(axis, line, nrm, a + 0.5, 0.9, 0.30)
        light(p[0], 0.30, p[2], WARM_L, 6.4)   # see the kick light, above


# ==========================================================================
#  5b. BULKHEAD DOORS
#      The plan draws 34 of them.  It draws them in survey units, though,
#      and against 22u passages -- so the rect only says WHERE, and the RUN
#      it lands in says how wide the opening really is.  Every one of the 34
#      lands in a run, and across it, so there is no special case.
#
#      The opening is inset 600 mm each side, which is not a taste decision:
#      600 mm is exactly the deck splay's own footprint, so the jamb stands
#      on the splay instead of a leaf trying to pass through it.  The head
#      is one module deep and runs to the ceiling, which eats a module of the
#      overhead's lit channel -- correct, since a bulkhead interrupts it.
#
#      The leaf is dynamic and belongs to tools/doors.js.  What is emitted
#      here is the frame it slides in, and SHIP_DOORS, which is the list
#      that runtime reads.  shipdetail.py emits the same list for the game.
# ==========================================================================
DOOR_H, DOOR_TH, DOOR_IN = 2.62, 0.90, 0.60
g('/* ---- bulkhead door frames.  the leaf is dynamic ----------------- */')
g('const SHIP_DOORS=[];')
ndoors, door_used = 0, set()
for (dpx, dpz, dpw, dph) in D.DOORS:
    px, pz = (dpx + dpw / 2.0) / float(K.MU), (dpz + dph / 2.0) / float(K.MU)
    run = None
    for r in RUNS:
        (gx0, gz0, gx1, gz1) = r[3]
        if gx0 <= px <= gx1 and gz0 <= pz <= gz1:
            run = r
            break
    if run is None:
        continue
    (gx0, gz0, gx1, gz1) = run[3]
    if run[2] == 'x':                    # the run goes along X: the door across Z
        a = min(max(int(math.floor(px)), gx0), gx1 - 1)
        while (run[0], run[1], a) in door_used and a < gx1 - 1:
            a += 1
        cross = range(gz0, gz1)
        c, o0, o1 = K.cx(a), wzf(gz0), wzf(gz1)
    else:
        a = min(max(int(math.floor(pz)), gz0), gz1 - 1)
        while (run[0], run[1], a) in door_used and a < gz1 - 1:
            a += 1
        cross = range(gx0, gx1)
        c, o0, o1 = K.cz(a), wxf(gx0), wxf(gx1)
    if (run[0], run[1], a) in door_used:
        continue
    door_used.add((run[0], run[1], a))
    hz = max(G.h(a, k) if run[2] == 'x' else G.h(k, a) for k in cross) * U
    h0, h1 = c - DOOR_TH / 2, c + DOOR_TH / 2      # the two faces you walk through
    k0, k1 = o0 + DOOR_IN, o1 - DOOR_IN            # and the clear opening

    def dbx(f0, y0, s0, f1, y1, s1, mat):
        """A box in the door's own frame: f across the faces, s along the
           opening.  One helper, so the two run axes read the same."""
        if run[2] == 'x':
            bx(f0, y0, s0, f1, y1, s1, mat)
        else:
            bx(s0, y0, f0, s1, y1, f1, mat)

    dbx(h0, DOOR_H, o0, h1, hz, o1, 'MACH')                     # the head
    for (j0, j1) in ((o0, k0), (k1, o1)):                       # the two jambs
        dbx(h0 - 0.05, 0.0, j0, h1 + 0.05, 2.80, j1, 'MACH')
    for (e0, e1) in ((k0 - 0.055, k0), (k1, k1 + 0.055)):       # their nosings
        dbx(h0 - 0.075, 0.0, e0, h1 + 0.075, DOOR_H, e1, 'RIB')
    for (f0, f1) in ((h0 - 0.048, h0), (h1, h1 + 0.048)):       # the head's band
        dbx(f0, DOOR_H, k0 - 0.055, f1, DOOR_H + 0.085, k1 + 0.055, 'TRIM')
    dbx(h0, 0.0, k0, h1, 0.016, k1, 'MACH')                     # the sill
    for (f0, f1) in ((h0 - 0.34, h0 - 0.06), (h1 + 0.06, h1 + 0.34)):
        dbx(f0, 0.0, k0, f1, 0.014, k1, 'SAFE')                 # and its warning
    J.append('SHIP_DOORS.push({x:%s,z:%s,w:%s,d:%s,ax:%d});' % (
        r3(c if run[2] == 'x' else (o0 + o1) / 2),
        r3((o0 + o1) / 2 if run[2] == 'x' else c),
        r3(DOOR_TH if run[2] == 'x' else k1 - k0),
        r3(k1 - k0 if run[2] == 'x' else DOOR_TH),
        0 if run[2] == 'x' else 1))
    ndoors += 1


# ==========================================================================
#  6.  ZONES  --  the HUD names the room you are in, and each is its own
#      cubemap probe
# ==========================================================================
for i, R in enumerate(G.REG):
    if R['kind'] != 'room':
        continue
    cells = [c for c in CELLS if G.reg[c] == i]
    if not cells:
        continue
    xs = [c[0] for c in cells]; zs = [c[1] for c in cells]
    hm = max(G.h(*c) for c in cells)
    zone(R['name'], R['sub'], wxf(min(xs)), wxf(max(xs) + 1),
         wzf(min(zs)), wzf(max(zs) + 1),
         K.cx((min(xs) + max(xs)) // 2), K.cz((min(zs) + max(zs)) // 2),
         min(2.4, hm * U * 0.4))
for (key, n) in ROUTE_TAKEN:
    (kind, idx) = key
    (_, _, ax, box, cells) = RUNBY[key]
    gx0, gz0, gx1, gz1 = box
    zone('PASSAGE' if kind == 'corr' else 'JUNCTION',
         'main deck \\u00b7 %s %d' % (kind, idx),
         wxf(gx0), wxf(gx1), wzf(gz0), wzf(gz1),
         K.cx((gx0 + gx1) // 2), K.cz((gz0 + gz1) // 2))
xs = [c[0] for c in CELLS]; zs = [c[1] for c in CELLS]
zone('MAIN DECK', 'deep survey vessel', wxf(min(xs)), wxf(max(xs) + 1),
     wzf(min(zs)), wzf(max(zs) + 1), 0, 0)

ZONE_JS = ("const ZONES=[\n" + ",\n".join(Z) + "\n];\n"
           "const VOID_CUBE=[0,1.7,0];\n"
           "function zoneAt(x,z){\n"
           "  for(let i=0;i<ZONES.length;i++){ const Z=ZONES[i];\n"
           "    if(x>=Z.x0&&x<=Z.x1&&z>=Z.z0&&z<=Z.z1) return i; }\n"
           "  return ZONES.length;\n}")

g('/* ---- part placements -------------------------------------------- */')
g('const MODULES=[' + ','.join(MODULES) + '];')


# ==========================================================================
#  7.  WHERE YOU WAKE UP
# ==========================================================================
def route_start():
    """You wake where the plan says you wake -- at the crew quarters end of
       the passage out of them -- looking down it.  ROUTE is in budget
       order, not walking order, so the start is named rather than taken
       from the front of it."""
    for key in [('corr', 1)] + ROUTE:
        if key not in RUNBY:
            continue
        cells = [c for c in RUNBY[key][4] if c in FULL]
        if not cells:
            continue
        (_, _, ax, box, _) = RUNBY[key]
        gx0, gz0, gx1, gz1 = box
        if ax == 'z':
            gz = min(c[1] for c in cells)
            return (K.cx((gx0 + gx1 - 1) // 2), K.cz(gz), 0.0)
        gx = min(c[0] for c in cells)
        return (K.cx(gx), K.cz((gz0 + gz1 - 1) // 2), 1.5707963)
    return (0.0, 0.0, 0.0)


PX, PZ, PYAW = route_start()

# ---- dust, over the route rather than over 7,000 square metres ----------
DUST = []
for (key, n) in ROUTE_TAKEN[:5]:
    (_, _, ax, box, _) = RUNBY[key]
    gx0, gz0, gx1, gz1 = box
    DUST.append([round(wxf(gx0), 2), round(wxf(gx1), 2), 0.2, 3.2,
                 round(wzf(gz0), 2), round(wzf(gz1), 2)])
if not DUST:
    DUST = [[PX - 3, PX + 3, 0.2, 3.2, PZ - 3, PZ + 12]]


# ==========================================================================
#  8.  SPLICE
#      Nothing about the renderer, the material compiler, the radiosity
#      bake or the parts themselves changes -- only the level and the
#      lights.  That is the whole point of building the map this way.
# ==========================================================================
s = io.open(SRC, encoding='utf-8').read()
n = [0]


def cut(a, b, new, label):
    global s
    assert a in s, 'anchor missing: ' + label
    i = s.index(a)
    j = s.index(b, i)
    s = s[:i] + new + s[j:]
    n[0] += 1


def sub(old, new, label):
    global s
    assert old in s, 'anchor missing: ' + label
    s = s.replace(old, new, 1)
    n[0] += 1


def resub(pat, new, label):
    global s
    s2, k = re.subn(pat, new, s)
    assert k, 'anchor missing: ' + label
    s = s2
    n[0] += k


cut('/* ---- decks and overheads',
    '/* ============================================================\n   3.  LIGHTS',
    '\n'.join(J) + '\n\n', 'level')
cut('const LIGHTS=[',
    '/* ============================================================\n   4.  MATERIAL COMPILER',
    'const LIGHTS=[\n' + '\n'.join(L) + '\n];\n' + ZONE_JS + '\n\n', 'lights')

resub(r"const P=\{x:[-\d.]+,y:0,z:[-\d.]+, vy:0,",
      "const P={x:%s,y:0,z:%s, vy:0," % (r3(PX), r3(PZ)), 'player init')
resub(r"P\.x=[-\d.]+; P\.y=0; P\.z=[-\d.]+; P\.vy=0; P\.yaw=[-\d.]+;",
      "P.x=%s; P.y=0; P.z=%s; P.vy=0; P.yaw=%s;" % (r3(PX), r3(PZ), r3(PYAW)),
      'player reset')
resub(r"if\(P\.y<-40\)\{ P\.x=[-\d.]+;P\.y=0;P\.z=[-\d.]+;P\.vy=0; \}",
      "if(P.y<-40){ P.x=%s;P.y=0;P.z=%s;P.vy=0; }" % (r3(PX), r3(PZ)),
      'fall respawn')
resub(r"const CORE=\{x:[-\d.]+,z:[-\d.]+,",
      "const CORE={x:%s,z:%s," % (r3(PX), r3(PZ)), 'core')

# a previz walks the map: nothing hunts you -- but since tools/weapons.py
# you carry the full procedural arsenal, to judge the guns against the ship
i = s.index('function placeProps(){')
s = s[:i] + 'function placeProps(){ PROPS.length=0; }' + s[s.index('\n}', i) + 2:]
i = s.index('const SPAWNS=[')
s = s[:i] + 'const SPAWNS=[];' + s[s.index('];', i) + 2:]
n[0] += 2
sub('function drawGameWorld(){',
    'function drawGameWorld(){\n  entCount=0; msp=0; return;   // previz: nothing moves',
    'no game world')
# the file has no closing </body>; the grant runs after the main script
s = s + '<script>window.VMW&&VMW.previz();</script>\n'
n[0] += 1
sub('<div id="game">', '<div id="game" style="display:none">', 'no game hud')

# the dust volumes were the bench's; they are the route's now
i = s.index('const DUST_VOL=[')
s = s[:i] + 'const DUST_VOL=' + repr(DUST).replace(' ', '') + ';' + \
    s[s.index('];', i) + 2:]
n[0] += 1

resub(r"gl\.uniform3f\(US\.uMinLight,[\d.]+,[\d.]+,[\d.]+\);",
      "gl.uniform3f(US.uMinLight,0.046,0.045,0.044);", 'ambient')
resub(r"gl\.uniform1f\(US\.uFogDensity,[\d.]+\);",
      "gl.uniform1f(US.uFogDensity,0.013);", 'haze')

sub('<title>Derelict', '<title>Deck plan v6 · assembled — Derelict', 'title')
sub('<div class="sub">Derelict &middot; Main deck</div>',
    '<div class="sub">Deck plan v6 &middot; assembled from the kit</div>',
    'hud sub')
sub('<h1>Derelict<em>Deck plan v6 · map pass</em></h1>',
    '<h1>Deck plan v6<em>%d modules on the route · %d m&sup2; of ship</em></h1>'
    % (len(MODULES), int(len(CELLS) * U * U)), 'menu title')
sub("b.textContent='Walk the ship';", "b.textContent='Walk the plan';", 'begin')
sub('Deep Survey Vessel &middot; Main Deck &middot; Life Support Degraded',
    'Deck plan v6 &middot; the route in the kit, the rest in its shell',
    'strap')

io.open(OUT, 'w', encoding='utf-8').write(s)

kitmods = os.path.join(HERE, '_map_modules.js')
io.open(kitmods, 'w', encoding='utf-8').write(
    'const MODULES=[' + ','.join(MODULES).replace('T.', 'TT.') +
    '];\nmodule.exports=MODULES;\n')

print('grade      FULL %d cells   SHELL %d cells   (%d total, %.0f m2)'
      % (len(FULL), len(CELLS) - len(FULL), len(CELLS), len(CELLS) * U * U))
print('route      ' + ', '.join('%s %d (%d)' % (k[0], k[1], c)
                                for (k, c) in ROUTE_TAKEN))
skipped = [k for k in ROUTE if k not in [t[0] for t in ROUTE_TAKEN]]
if skipped:
    print('over budget ' + ', '.join('%s %d' % k for k in skipped))
print('parts      %d instances, ~%d faces estimated' % (len(MODULES),
                                                        faces_spent[0]))
print('brushes    %d   quads %d   corners %d posts / %d fillets'
      % (sum(1 for l in J if l.startswith('brush(')),
         sum(1 for l in J if l.startswith('quad(')), ncorner, nfillet))
print('doors      %d bulkhead frames on the plan doorways' % ndoors)
print('shell      %d stanchions, %d columns, %d band brushes, %d deck lids'
      % (npost, ncol, nband, nplate))
print('           %d overhead fittings, %d lit-line brushes' % (nfit, nline))
print('           %d wall events, %d threshold bands' % (nevent, nthresh))
print('lights     %d   zones %d   wall runs %d' % (len(L), len(Z),
                                                   len(WALLRUNS)))
print('%d edits -> %s (%.1f MB)' % (n[0], os.path.relpath(OUT, ROOT),
                                    len(s) / 1e6))
