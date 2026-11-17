"""The kit bench -- one alcove per part, so feedback can be given per part.

Takes the built ship (src/derelict/derelict.html, which already carries
tools/kit.js) and throws the map away, leaving a gallery: an aisle with
six alcoves off it and a hall at the end.  Nothing about the renderer,
the material compiler, the radiosity bake or the parts themselves
changes -- which is the point.  A part judged in a viewer with its own
lighting is a part judged somewhere it will never be seen.

  aisle, port side       FLOOR 1     FLOOR 2     FLOOR 3
  aisle, starboard       WALL 1      WALL 2      WALL 3
  hall at the end        COLUMN 1    COLUMN 2    COLUMN 3
  far end of the hall    CORNER 1  -- as an actual 90 degree junction, with
                         a different bay type down each of its two legs

Each alcove is its own zone, so the HUD panel already names whichever
part you are standing in; keys 1-9 jump straight to one.

Every part is lit two ways on purpose: a key light overhead, which is
how it will usually be seen, and a second lamp almost in the surface's
own plane.  The raking one is the whole test -- a 16 mm chamfer is
invisible under a light coming straight down and unmistakable under one
that is not, and a part that only survives its key light is a part that
will vanish the moment it is built into a corridor.

Run:  python tools/kitview.py     ->  src/derelict/kit.html
"""
import io, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, 'src', 'derelict', 'derelict.html')
OUT = os.path.join(ROOT, 'src', 'derelict', 'kit.html')

T = 1.20                     # U -- the kit's module, and one texture tile
WT = 0.3                     # wall thickness
AHW = 3.4                    # aisle half width
BAY_X = 9.6                  # alcove back wall
BAY_HW = 2.6                 # alcove half width along Z
CEIL = 4.6                   # aisle and alcove overhead
HALL_Z0, HALL_Z1 = 23.0, 44.0
# the corner island: a solid L with a wall run down each of its two
# outward faces and the post on the arris where they meet
ISL_X0, ISL_X1 = -2.65, 1.00      # three bays of U down each leg
ISL_Z0, ISL_Z1 = 36.30, 39.90
ISL_H = 3.60                      # 3U, the corridor
HALL_HW = 8.0
HALL_CEIL = 6.2
BAY_Z = [6.0, 12.0, 18.0]
CORR_Z0 = 44.0          # the corridor runs out of the hall's back wall

J, L, Z, MODULES = [], [], [], []
def g(s): J.append(s)
def r3(v): return '%g' % round(v + 0.0, 3)

def bx(x0, y0, z0, x1, y1, z1, mat):
    g('brush(%s,%s,%s, %s,%s,%s, T.%s);' % (r3(x0), r3(y0), r3(z0),
                                            r3(x1), r3(y1), r3(z1), mat))

def mod(kit, x, y, z, yaw, arg, mat):
    MODULES.append('[%d,%s,%s,%s,%s,%s,T.%s]' % (kit, r3(x), r3(y), r3(z),
                                                 r3(yaw), r3(arg), mat))

# WALL 5's aperture, in its own space -- the only numbers kit.js and this
# file both have to agree about.  A part cannot cut the brush it is stamped
# on, so a window has to be cut here, and if these two ever disagree the
# window simply goes black: the hull's own face draws over the void chamber.
PORT_W, PORT_Y0, PORT_Y1 = 2.520, 0.708, 2.028


def ported(a0, a1, y0, y1, t0, t1, mat, ports, axis):
    """One wall, thin in `axis`, with holes in it.

    `ports` are centres along the wall's long axis; each opens PORT_W wide
    and PORT_Y0..PORT_Y1 high, right through the thickness.  Emitted as the
    slabs BETWEEN the ports plus the piece over and under each one, which is
    all a rectangular opening in a rectangular wall ever needs -- the same
    thing `wall()` in the engine does for a doorway."""
    def box(b0, b1, c0, c1):
        if b1 - b0 < 0.001 or c1 - c0 < 0.001:
            return
        if axis == 'x':
            bx(t0, c0, b0, t1, c1, b1, mat)          # thin in x, spans z
        else:
            bx(b0, c0, t0, b1, c1, t1, mat)          # thin in z, spans x
    cuts = sorted((c - PORT_W / 2, c + PORT_W / 2) for c in ports)
    at = a0
    for (p0, p1) in cuts:
        p0, p1 = max(a0, p0), min(a1, p1)
        box(at, p0, y0, y1)                          # the wall between ports
        box(p0, p1, y0, PORT_Y0)                     # under the opening
        box(p0, p1, PORT_Y1, y1)                     # and over it
        at = p1
    box(at, a1, y0, y1)


def light(x, y, z, col, r):
    L.append('  {p:[%s,%s,%s], c:[%s], r:%s, noFix:1},' % (
        r3(x), r3(y), r3(z), col, r3(r)))

def zone(name, sub, x0, x1, z0, z1, cx, cz):
    Z.append("  {n:'%s', s:'%s', x0:%s,x1:%s, z0:%s,z1:%s, c:[%s,1.7,%s]}"
             % (name, sub, r3(x0), r3(x1), r3(z0), r3(z1), r3(cx), r3(cz)))

# A bench is lit to be judged by, not to be atmospheric in.  These run
# well over 1.0 -- the pipeline is HDR and tone-maps at the end, so a
# review light can be twice a corridor light without clipping.
COOL = '1.15,1.42,1.95'
WARM = '2.00,1.32,0.56'
WHITE = '1.70,1.78,1.92'
DIM = '0.50,0.60,0.82'

# (kit index, family, name, subtitle, side, z-centre for a bay / x for a column)
EXHIBITS = [
    (2,  'floor',  'FLOOR 1',  'access deck \\u00b7 1.12 m plate',   -1, BAY_Z[0]),
    (3,  'floor',  'FLOOR 2',  'vent deck \\u00b7 4 \\u00d7 600 mm',     -1, BAY_Z[1]),
    (4,  'floor',  'FLOOR 3',  'flush hatch \\u00b7 one 600 mm cell',-1, BAY_Z[2]),
    (5,  'wall',   'WALL 1',   'service panel bay \\u00b7 1U',      +1, BAY_Z[0]),
    (6,  'wall',   'WALL 2',   'main run \\u00b7 2U, five mains',  +1, BAY_Z[1]),
    (7,  'wall',   'WALL 3',   'service & air bay \\u00b7 1U',      +1, BAY_Z[2]),
    (8,  'column', 'COLUMN 1', 'stanchion \\u00b7 300 mm section',   0, -4.4),
    (9,  'column', 'COLUMN 2', 'service stack \\u00b7 4 runs',       0,  0.0),
    (10, 'column', 'COLUMN 3', 'fluted drum \\u00b7 300 mm shaft',   0,  4.4),
    (11, 'corner', 'CORNER 1', 'outside corner post \\u00b7 600 mm', 0,  0.0),
    (12, 'ceiling', 'CEILING 1', 'overhead \\u00b7 lit run & return', 0,  0.0),
    (16, 'incorr', 'WALL 4',   'assembled panel field \\u00b7 4U',   0,  3.0),
    (13, 'incorr', 'SKIRT 1',  'deck splay \\u00b7 600 mm',          0,  9.0),
    (14, 'incorr', 'COVE 1',   'overhead splay \\u00b7 600 mm',      0, 13.8),
    (17, 'incorr', 'FLOOR 4',  'deck margin \\u00b7 drain channel',  0, 18.6),
    (15, 'incorr', 'FILLET 1', 'corner splay \\u00b7 600 mm',        0, 22.8),
    (18, 'incorr', 'BAND 1',   'safety band \\u00b7 1U \\u00d7 300 mm',   0,  4.6),
    (19, 'viewport', 'WALL 5', 'viewport bay \\u00b7 3U \\u00b7 four lights', 0, 40.2),
    (-1, 'corridor', 'CORRIDOR', 'the parts, assembled \\u00b7 4U \\u00d7 3U', 0,  0.0),
]
JUMP = []                    # where key 1..9 puts you


# ==========================================================================
#  the shell
# ==========================================================================
# The bench shell is COAT everywhere, not the old `PANEL`.  PANEL draws a
# 0.72 m plate grid and a rail every 0.80 m INTO THE TEXTURE, which is
# fine on a brush that has no shape -- but it is the exact pattern the
# parts are being judged against, and a room papered in it makes every
# part look like more of the same.  A bench should be blank.
g('/* ---- the bench: an aisle, six alcoves, a hall ------------------- */')
AZ1 = HALL_Z0
bx(-AHW, -WT, 0, AHW, 0, AZ1, 'DARK')                     # aisle deck
bx(-AHW, CEIL, 0, AHW, CEIL + WT, AZ1, 'DARK')           # aisle overhead
bx(-AHW - WT, 0, -WT, AHW + WT, CEIL, 0, 'COAT')         # the wall behind you

# aisle side walls, broken where an alcove opens
edges = [0.0]
for z in BAY_Z:
    edges += [z - BAY_HW, z + BAY_HW]
edges.append(AZ1)
for s in (-1, 1):
    for k in range(0, len(edges), 2):
        z0, z1 = edges[k], edges[k + 1]
        if z1 - z0 > 0.01:
            bx(s * AHW, 0, z0, s * (AHW + WT), CEIL, z1, 'COAT')

# the alcoves
for s in (-1, 1):
    for zc in BAY_Z:
        a, b = sorted((s * AHW, s * BAY_X))
        bx(a, CEIL, zc - BAY_HW, b, CEIL + WT, zc + BAY_HW, 'COAT')
        bx(s * BAY_X, 0, zc - BAY_HW - WT, s * (BAY_X + WT), CEIL,
           zc + BAY_HW + WT, 'COAT')                     # back wall
        for t in (-1, 1):                                 # the two returns
            c, d = sorted((zc + t * BAY_HW, zc + t * (BAY_HW + WT)))
            bx(a, 0, c, b, CEIL, d, 'COAT')

# the hall at the end
bx(-HALL_HW, -WT, HALL_Z0, HALL_HW, 0, HALL_Z1, 'DARK')
# ---- SKYLIGHT 1 preview: a hole in the hall's own overhead, so K20 can
# be judged the way WALL 5 is -- standing in the room it is built for,
# not two metres away in an alcove.  Temporary bench-only hookup; the
# real ceiling this part cuts is the maintenance store's, in deckmap.py.
SKY_HW, SKY_Z = 3.6, 33.0                          # matches kit.js SK_HW
bx(-HALL_HW, HALL_CEIL, HALL_Z0, -SKY_HW, HALL_CEIL + WT, HALL_Z1, 'COAT')
bx(SKY_HW, HALL_CEIL, HALL_Z0, HALL_HW, HALL_CEIL + WT, HALL_Z1, 'COAT')
bx(-SKY_HW, HALL_CEIL, HALL_Z0, SKY_HW, HALL_CEIL + WT, SKY_Z - SKY_HW, 'COAT')
bx(-SKY_HW, HALL_CEIL, SKY_Z + SKY_HW, SKY_HW, HALL_CEIL + WT, HALL_Z1, 'COAT')
mod(20, 0.0, HALL_CEIL, SKY_Z, 0.0, 0, 'COAT')
bx(-HALL_HW - WT, 0, HALL_Z0 - WT, -HALL_HW, HALL_CEIL, HALL_Z1 + WT, 'COAT')
ported(HALL_Z0 - WT, HALL_Z1 + WT, 0, HALL_CEIL, HALL_HW, HALL_HW + WT,
       'COAT', [40.2], 'x')
for _a, _b in ((-HALL_HW - WT, -2.4), (2.4, HALL_HW + WT)):
    bx(_a, 0, HALL_Z1, _b, HALL_CEIL, HALL_Z1 + WT, 'COAT')
bx(-2.4, 3.6, HALL_Z1, 2.4, HALL_CEIL, HALL_Z1 + WT, 'COAT')   # over the door
for s in (-1, 1):                                          # the hall's front
    c, d = sorted((s * AHW, s * HALL_HW))
    bx(c, 0, HALL_Z0 - WT, d, HALL_CEIL, HALL_Z0, 'COAT')
bx(-AHW, CEIL, HALL_Z0 - WT, AHW, HALL_CEIL, HALL_Z0, 'COAT')   # lintel

# alcove floors are laid in four pieces, so FLOOR 2 can have a pit under it
PIT_ZC = BAY_Z[1]
PIT_X0, PIT_X1 = -6.5 - 1.95, -6.5 + 1.95
PIT_Z0, PIT_Z1 = PIT_ZC - 1.95, PIT_ZC + 1.95
for s in (-1, 1):
    for zc in BAY_Z:
        a, b = sorted((s * AHW, s * BAY_X))
        if s < 0 and zc == PIT_ZC:
            bx(a, -WT, zc - BAY_HW, PIT_X0, 0, zc + BAY_HW, 'DARK')
            bx(PIT_X1, -WT, zc - BAY_HW, b, 0, zc + BAY_HW, 'DARK')
            bx(PIT_X0, -WT, zc - BAY_HW, PIT_X1, 0, PIT_Z0, 'DARK')
            bx(PIT_X0, -WT, PIT_Z1, PIT_X1, 0, zc + BAY_HW, 'DARK')
        else:
            bx(a, -WT, zc - BAY_HW, b, 0, zc + BAY_HW, 'DARK')

g('/* ---- the pit under FLOOR 2, so a grating has something to be over */')
bx(PIT_X0 - 0.4, -1.75, PIT_Z0 - 0.4, PIT_X1 + 0.4, -1.45, PIT_Z1 + 0.4, 'RUST')
for a, b, c, d in ((PIT_X0 - 0.4, PIT_X0, PIT_Z0 - 0.4, PIT_Z1 + 0.4),
                   (PIT_X1, PIT_X1 + 0.4, PIT_Z0 - 0.4, PIT_Z1 + 0.4),
                   (PIT_X0, PIT_X1, PIT_Z0 - 0.4, PIT_Z0),
                   (PIT_X0, PIT_X1, PIT_Z1, PIT_Z1 + 0.4)):
    bx(a, -1.45, c, b, 0, d, 'RUST')

# a low strip down the aisle, so it has a horizon and a length
for s in (-1, 1):
    for k in range(0, len(edges), 2):
        z0, z1 = edges[k], edges[k + 1]
        if z1 - z0 < 0.8:
            continue
        bx(s * AHW, 0.40, z0 + 0.2, s * (AHW - 0.10), 0.52, z1 - 0.2, 'GLOW')
        bx(s * AHW, 0.52, z0 + 0.2, s * (AHW - 0.14), 0.60, z1 - 0.2, 'RIB')

# ---- scale reference, one in the aisle and one in the hall --------------
g('/* ---- scale reference: 1.80 m, banded at 0.3 / 0.9 / 1.5 -------- */')
def ruler(x, z):
    bx(x - 0.15, 0.0, z - 0.15, x + 0.15, 1.80, z + 0.15, 'HAZARD')
    for h in (0.30, 0.90, 1.50):
        bx(x - 0.21, h, z - 0.21, x + 0.21, h + 0.03, z + 0.21, 'TRIM')
ruler(2.6, 2.2)
ruler(6.9, HALL_Z1 - 2.0)


# ==========================================================================
#  the exhibits
# ==========================================================================
for (k, fam, name, sub_, side, at) in EXHIBITS:
    g('/* ---- K%d  %s -------------------------------------------- */' % (k, name))
    if fam == 'floor':
        xc, zc = side * (AHW + BAY_X) / 2, at
        for i in range(3):
            for j in range(3):
                # 0..15, not 0..3: the deck variant now carries which
                # diagonal is open (bit 1) and which picture it wears
                # (bit 3) as well as the old hatch cell and lit seam
                mod(k, xc + (i - 1) * T, 0.0, zc + (j - 1) * T, 0.0,
                    (i * 7 + j * 11) & 15, 'FLOOR')
        # the box it replaces, on the deck beside it, same footprint, same material
        bx(xc + 2.55 - T / 2, 0.0, zc - T / 2, xc + 2.55 + T / 2, 0.060,
           zc + T / 2, 'FLOOR')
        light(xc, CEIL - 0.5, zc, COOL, 11.0)
        light(xc + 3.05, 0.32, zc, WARM, 7.5)              # the raking lamp
        light(xc - 2.7, 0.30, zc - 2.2, WHITE, 6.0)        # and one across it
        light(xc, 2.4, zc + 2.3, DIM, 7.0)
        zone(name, sub_, xc - 3.2, xc + 3.2, zc - BAY_HW, zc + BAY_HW, xc, zc)
        JUMP.append((xc + 2.9, zc, -1.5708, -0.42))

    elif fam == 'wall':
        xc, zc = side * (AHW + BAY_X) / 2, at
        # a run of bays, spaced by the bay's OWN width -- WALL 2 is 2U and
        # WALL 4 is 4U now, and stamping either on a 1U pitch buries it
        # inside its neighbour
        BW = {5: 1, 6: 2, 7: 1, 16: 4}.get(k, 1)
        for j in ((-1, 0, 1) if BW == 1 else (-0.5, 0.5)):
            mod(k, side * BAY_X, 0.0, zc + j * T * BW, -1.5708 * side,
                3.6, 'COAT')
        # one more on the return wall, so its depth is read in silhouette
        mod(k, side * (AHW + 1.7), 0.0, zc - BAY_HW, 0.0, 3.6, 'COAT')
        # the parts are 3.6 m (3U) because a corridor is; a soffit closes the
        # gap to this alcove's 4.6 m overhead instead of leaving bare brush
        a, b = sorted((side * AHW, side * (BAY_X + WT)))
        bx(a, 3.6, zc - BAY_HW, b, 3.68, zc + BAY_HW, 'RIB')
        bx(a, 3.68, zc - BAY_HW, b, CEIL, zc + BAY_HW, 'DARK')
        light(xc - side * 1.2, 3.30, zc - 1.9, WARM, 10.5)
        light(side * (BAY_X - 0.55), 3.60, zc + 2.35, WHITE, 8.5)   # raking, high
        light(side * (BAY_X - 0.50), 0.95, zc - 2.35, WHITE, 7.5)   # raking, low
        light(xc, 2.6, zc, DIM, 8.0)                                # fill
        zone(name, sub_, xc - 3.2, xc + 3.2, zc - BAY_HW, zc + BAY_HW, xc, zc)
        JUMP.append((side * (BAY_X - 4.0), zc, 1.5708, -0.05))

    elif fam == 'corner':
        # The island itself: solid to bay height, capped, dark above.  The
        # two faces the walls hang on are its +X face (x = ISL_X1) and its
        # +Z face (z = ISL_Z1), so the arris -- and the post's origin -- is
        # the vertical line through (ISL_X1, ISL_Z1).
        bx(ISL_X0, 0, ISL_Z0, ISL_X1, ISL_H, ISL_Z1, 'COAT')
        bx(ISL_X0 - 0.10, ISL_H, ISL_Z0 - 0.10, ISL_X1 + 0.10, ISL_H + 0.26,
           ISL_Z1 + 0.10, 'MACH')
        bx(ISL_X0, ISL_H + 0.26, ISL_Z0, ISL_X1, HALL_CEIL, ISL_Z1, 'DARK')

        # leg A, along the +Z face: WALL 1, WALL 3, WALL 1
        for j, kk in enumerate((5, 7, 5)):
            mod(kk, ISL_X1 - T / 2 - j * T, 0.0, ISL_Z1, 0.0, ISL_H, 'COAT')
        # leg B, along the +X face: WALL 2, WALL 2, WALL 1.  Different bay
        # types on the two legs on purpose -- if the grammar holds, the kick
        # line, the rail and the light line run straight through the post.
        for j, kk in enumerate((6, 6, 5)):
            mod(kk, ISL_X1, 0.0, ISL_Z1 - T / 2 - j * T, 1.5708, ISL_H, 'COAT')
        # and the post on the arris
        mod(11, ISL_X1, 0.0, ISL_Z1, 0.0, ISL_H, 'MACH')

        # Lit to be judged, not to be atmospheric: a key overhead, one lamp
        # raking along each leg almost in its own plane, and a fill that
        # only reaches the near faces.  A corner that survives a light
        # coming down the corridor it turns is a corner that works.
        light(ISL_X1 - 1.4, HALL_CEIL - 0.7, ISL_Z1 + 1.2, COOL, 12.0)
        light(ISL_X1 + 3.1, 1.05, ISL_Z1 + 0.2, WARM, 9.0)      # rakes leg A
        light(ISL_X1 + 0.3, 3.30, ISL_Z1 - 3.4, WHITE, 8.5)     # rakes leg B
        light(ISL_X1 + 2.2, 2.10, ISL_Z1 + 2.6, DIM, 7.5)
        light(ISL_X0 - 1.6, 2.60, ISL_Z1 + 1.6, DIM, 6.5)
        zone(name, sub_, ISL_X0 - 4.0, ISL_X1 + 3.4,
             ISL_Z0 - 1.0, ISL_Z1 + 4.5, ISL_X1 + 2.2, ISL_Z1 + 2.4)
        JUMP.append((ISL_X1 + 2.9, ISL_Z1 + 3.1, 3.9270, -0.10))

    elif fam == 'ceiling':
        # No alcove for this one, and there should not be: you judge an
        # overhead by walking under it, so it IS the aisle's overhead --
        # six modules across, the full length of the run, with a lit
        # channel down every third one.  Two continuous lines the length
        # of the aisle, which is what gives the passage its perspective.
        for ci in range(6):
            xm = -3.0 + ci * T
            for ri in range(19):
                zm = 0.6 + ri * T
                lit = 1 if ci in (1, 4) else 0
                mod(k, xm, CEIL, zm, 0.0,
                    lit | (((ci * 5 + ri * 3) & 3) << 1), 'COAT')
        light(0, CEIL - 1.5, 3.4, WHITE, 8.0)         # rakes along the run
        light(-2.4, 2.3, 15.0, DIM, 7.0)
        JUMP.append((0.0, 15.6, 0.0, 0.62))           # look UP at it

    elif fam == 'incorr':
        # These five are junctions and backgrounds.  A junction cannot be
        # judged in an alcove -- it only exists where two planes meet --
        # and a background part is meaningless on its own, because its
        # whole job is to be the thing the events are noticed against.
        # So they have a row and a key that walks you to them, and the
        # corridor is where they are.
        JUMP.append((0.0, CORR_Z0 + 1.4 + at, 0.0, 0.02))


    elif fam == 'viewport':
        # =============================================================
        #  A WINDOW CANNOT BE JUDGED IN AN ALCOVE.
        #
        #  Every other bay in the kit is a surface and an alcove is the
        #  right place to look at a surface.  This one is a HOLE, and the
        #  two things it has to be judged on are both about distance: does
        #  the sky sit behind the glass rather than on it, and does the
        #  frame occlude the sky as you walk past.  Neither question can be
        #  asked from two metres away in a box.
        #
        #  So it hangs on the hall's starboard wall with a 4U field, an
        #  ordinary 1U bay either side of it and the scale post at the far
        #  end -- twelve metres of run you can walk the length of, which is
        #  the only way to see a reveal work.
        # =============================================================
        WX, ZC = HALL_HW, at
        RUN = [(16, 34.8), (5, 37.8), (19, ZC), (5, 42.6)]
        for kk, zm in RUN:
            mod(kk, WX, 0.0, zm, -1.5708, ISL_H, 'COAT')
        z = 32.4
        while z < 43.2 - 0.01:                      # the octagon, every module
            mod(13, WX, 0.0, z + T / 2, -1.5708, 0, 'MACH')
            mod(14, WX, ISL_H - 0.3, z + T / 2, -1.5708, 0, 'MACH')
            z += T
        # the bays are 3U and the hall is 6.2 -- a string course caps the run
        # so the wall above it reads as the deck head and not as bare brush
        bx(WX - 0.12, ISL_H, 32.2, WX, ISL_H + 0.18, 43.4, 'RIB')

        # A key overhead, one lamp raking along the wall from each end -- the
        # whole test, because a 216 mm reveal is invisible under a light
        # coming straight at it -- and a fill that only reaches the near
        # faces.  Nothing is aimed AT the glass: what the pane is supposed to
        # do is show its own sky and reflect the room, and a lamp pointed at
        # it would hide both.
        light(WX - 1.5, HALL_CEIL - 0.8, ZC, COOL, 12.0)
        light(WX - 0.55, 3.30, ZC + 3.4, WHITE, 9.5)
        light(WX - 0.50, 0.95, ZC - 3.4, WHITE, 8.5)
        light(WX - 3.2, 2.30, ZC, DIM, 8.0)
        zone(name, sub_, 4.5, WX + 0.6, 37.4, 43.6, WX - 3.0, ZC)
        JUMP.append((4.8, ZC, 1.5708, -0.04))

    elif fam == 'corridor':
        # =============================================================
        #  THE SECTION IS AN OCTAGON
        #
        #  Deck, splay, wall, splay, overhead -- and not one right angle
        #  anywhere in it.  That single change is what separates every
        #  reference for this ship from a room with panels on it: a
        #  rectangular tube reads as architecture, an eight-sided one
        #  reads as a pressure hull that somebody had to put a flat
        #  floor into.
        #
        #  And the deck is ZONED, not speckled.  Three roles, and every
        #  tile is exactly one of them:
        #
        #    MARGIN  FLOOR 4, against each wall, channel outboard -- a
        #            dark grated line hard against the skirt
        #    FIELD   FLOOR 1, the two middle runs, plain plate, calm
        #    BAND    FLOOR 2 across the FULL WIDTH every fifth module,
        #            with a hatch now and then
        #
        #  The first pass put grates on a diagonal in every tile, which
        #  is a rule applied everywhere -- and a rule applied everywhere
        #  is not a rule, it is a texture.  It came out as a random
        #  mosaic.  Zones read because they have edges.
        # =============================================================
        CHW, CH, CZ0 = 2.4, 3.6, CORR_Z0
        NROW = 20
        CZ1 = CZ0 + NROW * T
        bx(-CHW - WT, -WT, CZ0, CHW + WT, 0, CZ1 + WT, 'DARK')      # sub-deck
        bx(-CHW - WT, CH, CZ0, CHW + WT, CH + WT, CZ1 + WT, 'DARK')
        bx(-CHW - WT, 0, CZ1, CHW + WT, CH, CZ1 + WT, 'COAT')       # far end

        XS = (-1.8, -0.6, 0.6, 1.8)          # four modules across
        for ri in range(NROW):
            zm = CZ0 + T / 2 + ri * T
            band = (ri % 5 == 4)
            for ci, xm in enumerate(XS):
                if band:
                    kk, yaw = 3, 0.0                      # FLOOR 2, full width
                elif ci in (0, 3):
                    kk = 4 if (ri % 13 == 6 and ci == 0) else 17
                    yaw = 0.0 if ci == 0 else 3.1416      # channel outboard
                else:
                    kk, yaw = 2, 0.0                      # FLOOR 1, the field
                # bit 4 is the safety band, and it goes on the FIELD tiles
                # one module before every vent band -- so the yellow reads
                # as a warning about the thing behind it and not as trim
                mod(kk, xm, 0.0, zm, yaw, (ri * 7 + ci * 11) & 15, 'FLOOR')

            # THE SAFETY BAND IS A LINE OF BLOCKS, laid straight across the
            # passage one module ahead of every vent band -- so the yellow
            # is a warning about the thing behind it and never decoration.
            # Four blocks make one 4.8 m line, wall to wall.
            if ri % 5 == 3:
                for xm in XS:
                    mod(18, xm, 0.0, zm, 0.0, 0, 'MACH')

            # the two splays go in every module without exception -- the
            # rule is only worth having if it is
            for sx in (-1, 1):
                mod(13, sx * CHW, 0.0, zm, -1.5708 * sx, 0, 'MACH')
                mod(14, sx * CHW, CH - 0.3, zm, -1.5708 * sx, 0, 'MACH')

            # THE OVERHEAD IS ZONED, LIKE THE DECK.  It used to take its
            # cell dressing from (ri*3 + ci*5) & 3, which is a hash -- and a
            # hash on a ceiling reads as somebody shuffled the panels.  The
            # outer runs are the margin and always carry the return-air
            # louvres; the inner runs are the field, never do, and are the
            # ones that carry the lit channel on alternate modules.
            for ci, xm in enumerate(XS):
                outer = ci in (0, 3)
                mod(12, xm, CH, zm, 0.0,
                    (0 if outer else (1 if ri % 2 == 0 else 0))
                    | (6 if outer else 0), 'COAT')

        # THE WALL RUN.  WALL 4 is 4U now, so it is placed on the centre of
        # four modules and four 1U events fill the next four -- and the two
        # sides start a module out of phase, so a long field always faces a
        # run of equipment rather than a mirror of itself.  A pilaster every
        # 1.2 m sliced the corridor into vertical strips; one every 4.8 m
        # gives the run a length, and the events read as events at last.
        # Every bay is sized by what is IN it now, so the run is a sequence
        # of (part, modules) walked end to end: a 4U field, a 2U main run,
        # then two 1U events.  The two sides start at different points in
        # the sequence, so a long field always faces something else.
        SEQ = ((16, 4), (6, 2), (19, 3), (5, 1), (7, 1))
        PORTS = {-1: [], 1: []}
        for sx in (-1, 1):
            wx, wy = sx * CHW, -1.5708 * sx
            order = SEQ if sx < 0 else SEQ[1:] + SEQ[:1]
            ri, q = 0, 0
            while ri < NROW:
                kk, m = order[q % len(order)]
                if ri + m > NROW:           # never overhang the far end
                    kk, m = 5, 1
                zw = CZ0 + (ri + m / 2.0) * T
                mod(kk, wx, 0.0, zw, wy, CH, 'COAT')
                # A LIT PANEL IS A LAMP, and a window is a lit panel: the two
                # jamb channels are real emitters, but the bake only bounces
                # them.  One cold fitting per viewport, at the height of the
                # glass, so the reveal and the sill are read from the far end
                # of the passage rather than only from in front of it.
                if kk == 19:
                    light(wx - sx * 0.75, 1.37, zw, '0.46,0.62,1.05', 5.6)
                    PORTS[sx].append(zw)
                ri += m
                q += 1

        # THE HULL, LAST, because it has holes in it now and the wall run is
        # what says where.  A viewport is the one part in the kit the level
        # has to cooperate with: it hangs a void chamber six metres outboard
        # and the brush would draw straight over it.
        for sx in (-1, 1):
            a, b = sorted((sx * CHW, sx * (CHW + WT)))
            ported(CZ0, CZ1 + WT, 0, CH + WT, a, b, 'DARK', PORTS[sx], 'x')

        # the last two right angles in the place, filled
        mod(15, -CHW, 0.0, CZ1, 1.5708, CH, 'MACH')
        mod(15, CHW, 0.0, CZ1, 3.1416, CH, 'MACH')

        # A LIT PANEL IS A LAMP.  The skirts and the coves emit, which
        # gives them their own glow and a soft local wash and no more, so
        # every lit overhead run carries a real lamp and every sixth
        # module carries a small warm one clear of the skirt.  Nothing is
        # lit from anywhere a fitting does not exist.
        CEIL_L = '1.06,1.07,1.06'
        KICK_L = '2.40,1.12,0.26'
        for ri in range(0, NROW, 2):
            light(0, CH - 0.34, CZ0 + T / 2 + ri * T, CEIL_L, 6.8)
        for ri in range(1, NROW, 4):
            for sx in (-1, 1):
                light(sx * 1.55, 0.26, CZ0 + T / 2 + ri * T, KICK_L, 3.0)
        zone(name, sub_, -CHW - 0.4, CHW + 0.4, CZ0 - 0.5, CZ1 + 0.5,
             0.0, CZ0 + 4.0)
        JUMP.append((0.0, CZ0 + 1.2, 0.0, 0.02))

    else:
        xc, zc = at, 29.0
        mod(k, xc, 0.0, zc, 0.0, 3.6, 'MACH')
        light(xc, HALL_CEIL - 0.55, zc - 2.4, COOL, 13.0)
        light(xc + 1.9, 0.45, zc + 2.7, WARM, 7.5)         # rakes up the shaft
        light(xc - 1.9, 1.15, zc - 2.5, WHITE, 7.0)        # and across it
        light(xc, 3.30, zc + 1.6, DIM, 6.0)
        # bounded in z, not the whole hall: with the hall extended to 44 for
        # the corner island the column zones used to swallow it, and
        # zoneAt returns the first match -- so the HUD named the junction
        # COLUMN 3.
        zone(name, sub_, xc - 2.2, xc + 2.2, HALL_Z0, 33.5, xc, zc)
        JUMP.append((xc, zc - 3.6, 0.0, -0.06))

# the beam the columns carry -- without it they are posts, not structure
g('/* ---- the beam the columns carry -------------------------------- */')
bx(-HALL_HW, 3.6, 28.72, HALL_HW, 4.08, 29.28, 'RIB')
bx(-HALL_HW, 4.08, 28.64, HALL_HW, 4.20, 29.36, 'TRIM')

light(0, CEIL - 0.5, 2.0, COOL, 9.0)
light(0, CEIL - 0.5, 21.0, COOL, 9.0)
light(0, HALL_CEIL - 0.6, HALL_Z1 - 2.5, COOL, 14.0)
light(0, HALL_CEIL - 0.6, HALL_Z0 + 2.0, COOL, 12.0)

zone('THE KIT', 'walk the aisle \\u00b7 keys 1-9 jump to a part',
     -HALL_HW - 1, HALL_HW + 1, -1, HALL_Z1 + 1, 0, 10.0)

g('/* ---- part placements -------------------------------------------- */')
g('const MODULES=[' + ','.join(MODULES) + '];')

ZONE_JS = ("const ZONES=[\n" + ",\n".join(Z) + "\n];\n"
           "const VOID_CUBE=[0,1.7,10];\n"
           "function zoneAt(x,z){\n"
           "  for(let i=0;i<ZONES.length;i++){ const Z=ZONES[i];\n"
           "    if(x>=Z.x0&&x<=Z.x1&&z>=Z.z0&&z<=Z.z1) return i; }\n"
           "  return ZONES.length;\n}")


# ==========================================================================
#  splice
# ==========================================================================
s = io.open(SRC, encoding='utf-8').read()
n = 0

def cut(a, b, new, label):
    global s, n
    assert a in s, 'anchor missing: ' + label
    i = s.index(a)
    j = s.index(b, i)
    s = s[:i] + new + s[j:]
    n += 1

def sub(old, new, label):
    global s, n
    assert old in s, 'anchor missing: ' + label
    s = s.replace(old, new, 1)
    n += 1

def resub(pat, new, label):
    global s, n
    s2, k = re.subn(pat, new, s)
    assert k, 'anchor missing: ' + label
    s = s2; n += k

cut('/* ---- decks and overheads',
    '/* ============================================================\n   3.  LIGHTS',
    '\n'.join(J) + '\n\n', 'level')
cut('const LIGHTS=[',
    '/* ============================================================\n   4.  MATERIAL COMPILER',
    'const LIGHTS=[\n' + '\n'.join(L) + '\n];\n' + ZONE_JS + '\n\n', 'lights')

# ---- you wake up at the door of the aisle -------------------------------
resub(r"const P=\{x:[-\d.]+,y:0,z:[-\d.]+, vy:0,",
      "const P={x:0,y:0,z:1.4, vy:0,", 'player init')
resub(r"P\.x=[-\d.]+; P\.y=0; P\.z=[-\d.]+; P\.vy=0; P\.yaw=0;",
      "P.x=0; P.y=0; P.z=1.4; P.vy=0; P.yaw=0;", 'player reset')
resub(r"if\(P\.y<-40\)\{ P\.x=[-\d.]+;P\.y=0;P\.z=[-\d.]+;P\.vy=0; \}",
      "if(P.y<-40){ P.x=0;P.y=0;P.z=1.4;P.vy=0; }", 'fall respawn')

# ---- nothing hunts you in here, and you carry nothing -------------------
resub(r"const CORE=\{x:[-\d.]+,z:[-\d.]+,", "const CORE={x:0,z:10,", 'core')
i = s.index('function placeProps(){')
j = s.index('\n}', i) + 2
s = s[:i] + 'function placeProps(){ PROPS.length=0; }' + s[j:]; n += 1
i = s.index('const SPAWNS=[')
j = s.index('];', i) + 2
s = s[:i] + 'const SPAWNS=[];' + s[j:]; n += 1
# The bench had emptied PROPS and SPAWNS but left drawGameWorld() running,
# and that function draws the CORE's three damage bars as emissive boxes at
# a hardcoded z of 40.45 and 47.55.  With the hall at 35.5 they were behind
# the back wall and nobody ever saw them; lengthening it to 44.0 for the
# corner island put a 3.4 m bar of (0.25, 0.95, 0.65) green straight across
# the junction, at 1.75 m, which is close enough to the light line at 2.42
# to be mistaken for it.  Nothing moves on a bench: the whole pass goes.
sub('function drawGameWorld(){',
    'function drawGameWorld(){' + chr(10) +
    '  entCount=0; msp=0; return;   // the bench: nothing moves, and no core',
    'no game world')
sub('function drawViewmodel(){',
    'function drawViewmodel(){\n  return;   // the bench: nothing in your hands',
    'no viewmodel')
sub('<div id="game">', '<div id="game" style="display:none">', 'no game hud')
# The bench used to sit at 0.070 so a part could never hide in shadow.
# That was right while parts were being judged one at a time and wrong
# the moment the corridor went in: 0.070 of free light is more than the
# strips put out, so the corridor came out evenly grey and the whole
# rhythm of lit and unlit disappeared.  0.030 still shows a part in an
# alcove -- they all carry their own lamps -- and lets the corridor be
# dark, which is the one thing the references all are.
resub(r"gl\.uniform3f\(US\.uMinLight,[\d.]+,[\d.]+,[\d.]+\);",
      "gl.uniform3f(US.uMinLight,0.046,0.045,0.044);", 'bench ambient')
resub(r"gl\.uniform1f\(US\.uFogDensity,[\d.]+\);",
      "gl.uniform1f(US.uFogDensity,0.012);", 'bench haze')

# ---- the index, and a key for every part --------------------------------
KEYS = "1234567890-=[];,./'\\"


def keylabel(i):
    return KEYS[i] if i < len(KEYS) else '?'

ROWS = '\n'.join(
    '<li><b>%s</b><span>%s</span><em>%s</em></li>'
    % (keylabel(i), e[2], e[3].replace('\\u00b7', '&middot;')
              .replace('\\u00d7', '&times;'))
    for i, e in enumerate(EXHIBITS))
JUMP_JS = '[' + ','.join('[%s,%s,%s,%s]' % (r3(a), r3(b), r3(c), r3(d))
                         for (a, b, c, d) in JUMP) + ']'
BENCH = """
<div id="kitlist">
  <h4>THE KIT<span>%d parts + the corridor &middot; press a key</span></h4>
  <ol>%s</ol>
  <p>Each part is modelled once and instanced. Say which numbers to change
     and what about them.</p>
</div>
<style>
#kitlist{position:fixed;right:14px;top:74px;width:272px;z-index:40;
  font:11px/1.45 ui-monospace,Consolas,monospace;color:#9fb4c4;
  background:rgba(8,11,14,.82);border:1px solid #1d2830;border-radius:3px;
  padding:10px 12px 8px;letter-spacing:.02em;pointer-events:none}
#kitlist h4{margin:0 0 7px;font-size:11px;letter-spacing:.14em;color:#cfe2ee}
#kitlist h4 span{display:block;font-weight:400;letter-spacing:.04em;
  color:#5e7686;font-size:10px;margin-top:2px}
#kitlist ol{margin:0;padding:0;list-style:none}
#kitlist li{display:flex;align-items:baseline;gap:7px;padding:2px 0;
  border-top:1px solid #141c22}
#kitlist li b{color:#e8a33c;min-width:11px;font-weight:600}
#kitlist li span{color:#d6e6f0;min-width:74px}
#kitlist li em{font-style:normal;color:#5e7686;font-size:10px}
#kitlist li.on{background:rgba(232,163,60,.10)}
#kitlist li.on span{color:#ffd79a}
#kitlist p{margin:8px 0 0;color:#4e6474;font-size:10px;line-height:1.5;
  border-top:1px solid #141c22;padding-top:6px}
</style>
<script>
(function(){
  const JUMPS=%s, LI=document.querySelectorAll('#kitlist li');
  addEventListener('keydown',e=>{
    const k="1234567890-=[];,./'\\\\".indexOf(e.key);
    if(e.key.length!==1||k<0||k>=JUMPS.length||!window.SHIP) return;
    const P=window.SHIP.P, j=JUMPS[k];
    P.x=j[0]; P.z=j[1]; P.y=0; P.vy=0; P.yaw=j[2]; P.pitch=j[3];
    e.stopPropagation();
  },true);
  setInterval(()=>{
    if(!window.SHIP) return;
    const P=window.SHIP.P;
    let best=-1, bd=1e9;
    for(let i=0;i<JUMPS.length;i++){
      const d=(P.x-JUMPS[i][0])**2+(P.z-JUMPS[i][1])**2;
      if(d<bd){ bd=d; best=i; }
    }
    for(let i=0;i<LI.length;i++) LI[i].classList.toggle('on', i===best&&bd<40);
  },160);
})();
</script>
""" % (len([e for e in EXHIBITS if e[0] >= 0]), ROWS, JUMP_JS)
sub('<div id="toast"></div>', BENCH + '\n<div id="toast"></div>', 'kit index')

# ---- branding ------------------------------------------------------------
sub('<title>Derelict', '<title>The kit · module bench — Derelict', 'title')
sub('<div class="sub">Derelict &middot; Main deck</div>',
    '<div class="sub">Kit &middot; module bench</div>', 'hud sub')
# The splash counted parts with a literal that said 10.  The corridor
# carries -1 because it is an assembly and not a part, which is exactly
# the thing that makes it countable here.
sub('<h1>Derelict<em>Deck plan v6 · map pass</em></h1>',
    '<h1>The kit<em>%d parts · modelled once, instanced everywhere</em></h1>'
    % len([e for e in EXHIBITS if e[0] >= 0]),
    'menu title')
sub("b.textContent='Walk the ship';", "b.textContent='Inspect the parts';", 'begin')
sub('Deep Survey Vessel &middot; Main Deck &middot; Life Support Degraded',
    'Module bench &middot; Floors port, walls starboard, columns and the corner junction aft',
    'strap')

io.open(OUT, 'w', encoding='utf-8').write(s)
print('%d edits -> %s (%d bytes)' % (n, os.path.relpath(OUT, ROOT), len(s)))
print('brushes %d  parts %d  lights %d  zones %d' % (
    sum(1 for l in J if l.startswith('brush(')), len(MODULES), len(L), len(Z)))
