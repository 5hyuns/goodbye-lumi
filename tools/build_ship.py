"""Turn the v6 deck plan into brush calls for the Sector C engine.

The walls are not authored.  A height map is rasterised at 10 cm from the
plan's rectangles, and every wall is a boundary of that map: an edge against
empty space becomes a hull wall, an edge between two different ceiling
heights becomes a lintel.  Doorways therefore cost nothing to cut -- a
corridor that overlaps a room simply has no boundary there.
"""
import math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckplan as D
import shipdetail
import shiplights

GX0, GX1, GY0, GY1 = 112, 542, 122, 940
GW, GH = GX1 - GX0, GY1 - GY0
S, OX, OZ = D.S, D.OX, D.OZ
WT = 0.3

hgt = [0.0] * (GW * GH)          # ceiling height, 0 = solid rock
zid = [-1] * (GW * GH)           # region index
REG = []                         # (key, floorMat, wallMat, ceilMat)


def paint(x, y, w, h, ceil, reg):
    x0, x1 = max(GX0, int(x)), min(GX1, int(x + w))
    y0, y1 = max(GY0, int(y)), min(GY1, int(y + h))
    for j in range(y0, y1):
        row = (j - GY0) * GW
        for i in range(x0, x1):
            k = row + i - GX0
            if ceil > hgt[k]:
                hgt[k] = ceil
                zid[k] = reg
            elif zid[k] < 0:
                zid[k] = reg


for r in D.ROOMS:
    REG.append((r[0], r[8], r[9], r[10]))
    paint(r[3], r[4], r[5], r[6], r[7], len(REG) - 1)

REG.append(('PASSAGE', 'PANEL', 'PANEL', 'PANEL'))
RP = len(REG) - 1
REG.append(('NODE', 'PANEL', 'PANEL', 'PANEL'))
RN = len(REG) - 1
REG.append(('BAY', 'GRATE', 'PANEL', 'PANEL'))
RB = len(REG) - 1

# corridors, stretched 6u at each end so they bite into what they serve --
# the plan draws them stopping at the wall line, and the wall has thickness.
CORR_RECTS = []
WD = D.PASSAGE_WIDEN
for (x, y, w, h) in D.CORR:
    if w >= h:                       # runs along X: lengthen it, widen in Z
        x, w = x - 6, w + 12
        y, h = y - WD, h + 2 * WD
    else:
        y, h = y - 6, h + 12
        x, w = x - WD, w + 2 * WD
    CORR_RECTS.append((x, y, w, h))
    paint(x, y, w, h, D.CEIL_CORR, RP)
for (x, y, w, h) in D.NODES:
    paint(x - 2 - WD, y - 2 - WD, w + 4 + 2 * WD, h + 4 + 2 * WD, D.CEIL_NODE, RN)
for b in D.BAYS:
    paint(b[0], b[1], b[2] + 26, b[3], 2.5, RB)      # tube reaches the gallery
paint(D.CAPSULE[0], D.CAPSULE[1], D.CAPSULE[2] + 28, D.CAPSULE[3], 2.7, RB)

# ---- greedy maximal rectangles over cells that share a height -------------
def decompose(pred):
    used = bytearray(GW * GH)
    out = []
    for j in range(GH):
        for i in range(GW):
            k = j * GW + i
            if used[k] or not pred(k):
                continue
            w = 1
            while i + w < GW and not used[k + w] and pred(k + w):
                w += 1
            h = 1
            while j + h < GH:
                kk = k + h * GW
                if any(used[kk + t] or not pred(kk + t) for t in range(w)):
                    break
                h += 1
            for jj in range(h):
                for ii in range(w):
                    used[k + jj * GW + ii] = 1
            out.append((i, j, w, h))
    return out


heights = sorted({h for h in hgt if h > 0})
J = []
def emit(s): J.append(s)

def bx(i0, j0, i1, j1, y0, y1, mat):
    """brush from grid corners (inclusive-exclusive) at world height y0..y1"""
    emit('brush(%s,%s,%s, %s,%s,%s, T.%s);' % (
        r3((i0 + GX0 - OX) * S), r3(y0), r3((j0 + GY0 - OZ) * S),
        r3((i1 + GX0 - OX) * S), r3(y1), r3((j1 + GY0 - OZ) * S), mat))

def r3(v):
    v = round(v + 0.0, 3)
    return ('%g' % v)

def onwall(axis, fixed, side, a0, a1, o0, o1, y0, y1, mat):
    """A box standing proud of a wall face.  `a` runs along the wall in grid
       units, `o` is depth into the room in METRES, `y` is height in metres."""
    o0, o1 = o0 / S, o1 / S
    if side > 0:
        g0, g1 = fixed - o1, fixed - o0
    else:
        g0, g1 = fixed + o0, fixed + o1
    if axis == 'x':
        bx(g0, a0, g1, a1, y0, y1, mat)
    else:
        bx(a0, g0, a1, g1, y0, y1, mat)


emit('/* ---- decks and overheads ---------------------------------------- */')
nfloor = 0
for hh in heights:
    for reg in range(len(REG)):
        pred = (lambda hh, reg: (lambda k: hgt[k] == hh and zid[k] == reg))(hh, reg)
        for (i, j, w, h) in decompose(pred):
            bx(i, j, i + w, j + h, -WT, 0.0, REG[reg][1])
            bx(i, j, i + w, j + h, hh, hh + WT, REG[reg][3])
            nfloor += 2

# ---- walls: every boundary of the height map -----------------------------
edges = {}
for j in range(GH):
    for i in range(GW):
        k = j * GW + i
        h = hgt[k]
        if h <= 0:
            continue
        mat = REG[zid[k]][2]
        for (di, dj, axis, fixed) in ((1, 0, 'x', i + 1), (-1, 0, 'x', i),
                                      (0, 1, 'z', j + 1), (0, -1, 'z', j)):
            ii, jj = i + di, j + dj
            h2 = 0.0 if (ii < 0 or ii >= GW or jj < 0 or jj >= GH) else hgt[jj * GW + ii]
            along = j if axis == 'x' else i
            if h2 <= 0:                       # hull: floor to ceiling, outward
                key = (axis, fixed, 0.0, round(h, 2), mat, di + dj)
            elif h2 < h - 1e-6:               # step down: a lintel over the gap
                key = (axis, fixed, round(h2, 2), round(h, 2), mat, 0)
            else:
                continue
            edges.setdefault(key, []).append(along)

emit('/* ---- hull, bulkheads and lintels -------------------------------- */')
nwall = 0
skins = []          # (axis, fixed, side, lo, hi, ceiling) -- every real wall face
for key, runs in edges.items():
    axis, fixed, y0, y1, mat, side = key
    runs.sort()
    a = 0
    while a < len(runs):
        b = a
        while b + 1 < len(runs) and runs[b + 1] == runs[b] + 1:
            b += 1
        lo, hi = runs[a], runs[b] + 1
        if side > 0:
            f0, f1 = fixed, fixed + WT / S
        elif side < 0:
            f0, f1 = fixed - WT / S, fixed
        else:
            f0, f1 = fixed - 0.1 / S, fixed + 0.1 / S
        if axis == 'x':
            bx(f0, lo, f1, hi, y0, y1, mat)
        else:
            bx(lo, f0, hi, f1, y0, y1, mat)
        if side and y0 == 0.0 and hi - lo >= 12:
            skins.append((axis, fixed, side, lo, hi, y1))
        nwall += 1
        a = b + 1

# ==========================================================================
#  THE SECTION
#  The reference corridors are not walls with a texture on them.  They are a
#  drawn section, repeated: a lit reveal at the deck, a plinth, a plate field
#  carrying two rails, a cove under the overhead, and a structural rib
#  crossing all of it at a fixed beat.  That is what the eye reads as a ship,
#  and it is why a flat plate with a good texture still read as a corridor
#  someone had painted rather than one someone had built.
#
#  Two rules out of the images, both cheap:
#
#   * THE LIGHT DRAWS THE PERSPECTIVE.  A continuous line low on the wall and
#     a second one in the cove: they converge, and that convergence is what
#     tells you how long the passage is before you have walked it.  Emissive
#     geometry, so the lighting solver is not asked for anything.
#   * WARM AND COLD IN THE SAME SHOT.  The deck line is amber (emergency,
#     machinery), the cove is cold white (systems).  Neither colour reads as
#     a colour without the other in frame, and when the sinking gauge later
#     takes the cold one away the ship goes red for free.
# ==========================================================================
emit('/* ---- the wall section: reveals, rails, ribs ---------------------- */')
RIB_PITCH = 3.9                       # metres between structural ribs
RIB_W = 0.52                          # rib width
nstrip = nrib = 0

for (axis, fixed, side, lo, hi, ceil) in skins:
    a0, a1 = lo + 1, hi - 1
    if a1 - a0 < 3:
        continue
    tall = ceil >= 3.0
    # ---- the two lines ---------------------------------------------------
    onwall(axis, fixed, side, a0, a1, 0.02, 0.14, 0.10, 0.27, 'GLOW')
    onwall(axis, fixed, side, a0, a1, 0.00, 0.15, 0.27, 0.37, 'RIB')
    nstrip += 2
    if tall:
        onwall(axis, fixed, side, a0, a1, 0.02, 0.15, ceil - 0.58, ceil - 0.43, 'COLD')
        onwall(axis, fixed, side, a0, a1, 0.00, 0.18, ceil - 0.43, ceil - 0.30, 'RIB')
        nstrip += 2
    # ---- the rails that give the plate field a horizontal grain ----------
    onwall(axis, fixed, side, a0, a1, 0.00, 0.055, 1.02, 1.14, 'TRIM')
    nstrip += 1
    if tall:
        onwall(axis, fixed, side, a0, a1, 0.00, 0.075, 2.26, 2.40, 'RIB')
        nstrip += 1
    # ---- the beat --------------------------------------------------------
    # A rib held two centimetres off the plate, with the hot line running up
    # behind it: in every reference image the orange comes out from *behind*
    # the structure, never off its face.
    if not tall:
        continue
    span = (hi - lo) * S
    nbay = max(1, int(round(span / RIB_PITCH)))
    step = (hi - lo) / float(nbay)
    hw = RIB_W * 0.5 / S
    for k in range(nbay):
        t = lo + step * (k + 0.5)
        if t - hw < a0 or t + hw > a1:
            continue
        onwall(axis, fixed, side, t - hw - 0.7, t + hw + 0.7, 0.00, 0.03, 0.37, ceil - 0.30, 'GLOW')
        onwall(axis, fixed, side, t - hw, t + hw, 0.03, 0.24, 0.0, ceil, 'RIB')
        onwall(axis, fixed, side, t - hw + 0.5, t + hw - 0.5, 0.24, 0.27, 0.40, ceil - 0.34, 'TRIM')
        nrib += 3


# ==========================================================================
#  CUT THE CORNERS
#  A brush is a box, so a hull made only of brushes has square corners
#  everywhere and reads as stacked blocks whatever is painted on it.  The
#  reference sections are not square: the deck meets the skin on a splay,
#  the skin meets the overhead on another, and the plan corners are cut.
#  All three are parallelograms, so they go through quad() and cost the bake
#  nothing it was not already doing.
# ==========================================================================
def wX(i): return (i + GX0 - OX) * S
def wZ(j): return (j + GY0 - OZ) * S


# ---- the one uv rule, mirrored from the engine ---------------------------
# T = cross(worldUp, n), B = cross(n, T).  Both unit, so u and w are arc
# length in metres along the surface whatever angle it sits at.  A chamfer
# therefore carries exactly its own width of texture, and the offset below
# slides that run so it continues out of the face it springs from instead of
# restarting -- which is the whole reason the bevels used to break.
def frame(n):
    if abs(n[1]) > 0.999:
        t = (1.0, 0.0, 0.0)
    else:
        L = math.hypot(n[2], n[0]) or 1.0
        t = (n[2] / L, 0.0, -n[0] / L)
    return t, (n[1] * t[2] - n[2] * t[1],
               n[2] * t[0] - n[0] * t[2],
               n[0] * t[1] - n[1] * t[0])


def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cont(p, n_from, n_to):
    """uv offset that makes the face `n_to` continue the face `n_from`
       across the edge they share at point p"""
    tf, bf = frame(n_from)
    tt, bt = frame(n_to)
    return _dot(p, tf) - _dot(p, tt), _dot(p, bf) - _dot(p, bt)


def strip(axis, fixed, side, lo, hi, o0, y0, o1, y1, nrm, mat):
    """One quad along a wall face, given as two profile points in the section:
       (offset into the room, height).  `nrm` is the direction it must face."""
    a0, a1 = lo + 1, hi - 1
    if a1 - a0 < 3:
        return 0
    def pt(o, y, a):
        if axis == 'x':
            return (wX(fixed) - side * o, y, wZ(a))
        return (wX(a), y, wZ(fixed) - side * o)
    c = [pt(o0, y0, a0), pt(o0, y0, a1), pt(o1, y1, a1), pt(o1, y1, a0)]
    e1 = [c[1][k] - c[0][k] for k in range(3)]
    e2 = [c[3][k] - c[0][k] for k in range(3)]
    cr = (e1[1] * e2[2] - e1[2] * e2[1],
          e1[2] * e2[0] - e1[0] * e2[2],
          e1[0] * e2[1] - e1[1] * e2[0])
    if sum(cr[k] * nrm[k] for k in range(3)) < 0:
        c[1], c[3] = c[3], c[1]
    # the splay springs off the wall plane; slide its uv so the panel run
    # walks off the wall and down the bevel without restarting
    nw = (-side, 0.0, 0.0) if axis == 'x' else (0.0, 0.0, -side)
    am = (a0 + a1) * 0.5
    if abs(o0) < 1e-6:
        uo, wo = cont(pt(o0, y0, am), nw, nrm)
    elif abs(o1) < 1e-6:
        uo, wo = cont(pt(o1, y1, am), nw, nrm)
    else:
        uo, wo = 0.0, 0.0
    emit('quad([%s,%s,%s],[%s,%s,%s],[%s,%s,%s],[%s,%s,%s], T.%s,%s,%s);' % (
        tuple(r3(v) for p in c for v in p) + (mat, r3(uo), r3(wo))))
    return 1


emit('/* ---- splayed deck and overhead corners --------------------------- */')
K = 0.7071067811865476
ncut = 0
for (axis, fixed, side, lo, hi, ceil) in skins:
    ch = 0.30 if ceil < 4.0 else 0.46          # splay, in metres
    ax = -side * K                             # into the room, along the axis
    n_lo = (ax, K, 0.0) if axis == 'x' else (0.0, K, ax)
    n_hi = (ax, -K, 0.0) if axis == 'x' else (0.0, -K, ax)
    # deck-to-skin, then skin-to-overhead
    ncut += strip(axis, fixed, side, lo, hi, ch, 0.0, 0.0, ch, n_lo, 'PANEL')
    ncut += strip(axis, fixed, side, lo, hi, 0.0, ceil - ch, ch, ceil, n_hi, 'PANEL')

MODULES = []
npil = 0

# ==========================================================================
#  GREEBLE
#  The corridors are not surfaces, they are *installations*: a cable tray on
#  the head rail, a conduit bundle under it, a junction box every few metres
#  with one small screen alive on it, a louvre vent.  None of it is
#  decoration -- it is what makes a plate read as a wall of a working
#  machine.  All of it rides the wall runs, so it lands only where a wall
#  actually is; emitted per corridor rectangle it stacked three deep at every
#  junction and flew across the open doorways.
# ==========================================================================
emit('/* ---- greeble: trays, conduit, boxes, vents ----------------------- */')
ngre = 0
for (axis, fixed, side, lo, hi, ceil) in skins:
    if hi - lo < 26 or ceil < 3.0:
        continue
    a0, a1 = lo + 2, hi - 2
    yt = min(2.44, ceil - 0.92)         # the tray sits on the head rail
    # cable tray, and the cables lying in it
    onwall(axis, fixed, side, a0, a1, 0.06, 0.38, yt, yt + 0.06, 'RIB')
    onwall(axis, fixed, side, a0, a1, 0.06, 0.13, yt + 0.06, yt + 0.30, 'RIB')
    onwall(axis, fixed, side, a0, a1, 0.32, 0.39, yt + 0.06, yt + 0.26, 'RIB')
    onwall(axis, fixed, side, a0, a1, 0.13, 0.32, yt + 0.10, yt + 0.22, 'PIPE')
    ngre += 4
    # clamps holding it
    t = lo + 8
    while t < hi - 8:
        onwall(axis, fixed, side, t, t + 2, 0.00, 0.28, yt - 0.16, yt + 0.02, 'RIB')
        ngre += 1
        t += 22
    # a junction box with one screen still alive on it
    t = lo + 16
    while t < hi - 20:
        onwall(axis, fixed, side, t, t + 6, 0.00, 0.30, 1.22, 1.86, 'CONSOLE')
        onwall(axis, fixed, side, t, t + 6, 0.30, 0.33, 1.28, 1.80, 'TRIM')
        onwall(axis, fixed, side, t + 1, t + 5, 0.33, 0.35, 1.36, 1.72, 'SCREEN')
        ngre += 3
        t += 68
    # a louvre vent, offset from the boxes so they do not queue up
    t = lo + 44
    while t < hi - 16:
        onwall(axis, fixed, side, t, t + 10, 0.00, 0.09, 1.90, 2.22, 'VENT')
        onwall(axis, fixed, side, t - 1, t + 11, 0.00, 0.13, 1.82, 1.92, 'RIB')
        onwall(axis, fixed, side, t - 1, t + 11, 0.00, 0.13, 2.20, 2.30, 'RIB')
        ngre += 3
        t += 96



emit('/* ---- module placements ------------------------------------------- */')
emit('const MODULES=[' + ','.join(
    '[%d,%s,%s,%s,%s,%s,T.%s]' % (k, a, b, c, d2, e, m)
    for (k, a, b, c, d2, e, m) in MODULES) + '];')

# ---- the plan corners are cut too ---------------------------------------
emit('/* ---- cut plan corners -------------------------------------------- */')
CUTS = []
for r in D.ROOMS:
    _, _, _, rx, ry, rw, rh, ceil = r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]
    d = 11 if rw > 60 and rh > 60 else 7       # 1.1 m / 0.7 m across the corner
    for (cx, cz, sx, sz) in ((rx, ry, 1, 1), (rx + rw, ry, -1, 1),
                             (rx, ry + rh, 1, -1), (rx + rw, ry + rh, -1, -1)):
        p0 = (wX(cx + sx * d - GX0), wZ(cz - GY0))
        p1 = (wX(cx - GX0), wZ(cz + sz * d - GY0))
        nx, nz = sx * K, sz * K
        c = [(p0[0], 0.0, p0[1]), (p1[0], 0.0, p1[1]),
             (p1[0], ceil, p1[1]), (p0[0], ceil, p0[1])]
        e1 = [c[1][k] - c[0][k] for k in range(3)]
        e2 = [c[3][k] - c[0][k] for k in range(3)]
        cr = (e1[1] * e2[2] - e1[2] * e2[1],
              e1[2] * e2[0] - e1[0] * e2[2],
              e1[0] * e2[1] - e1[1] * e2[0])
        if cr[0] * nx + cr[2] * nz < 0:
            c[1], c[3] = c[3], c[1]
        # a diagonal is longer than the corner it replaces, so it cannot
        # match both walls at once.  Continue each end and take the mean:
        # the leftover half-difference is 10 cm, which no panel run shows.
        nrm = (nx, 0.0, nz)
        uA, wA = cont((p0[0], 0.0, p0[1]), (0.0, 0.0, sz), nrm)
        uB, wB = cont((p1[0], 0.0, p1[1]), (sx, 0.0, 0.0), nrm)
        emit('quad([%s,%s,%s],[%s,%s,%s],[%s,%s,%s],[%s,%s,%s], T.PANEL,%s,%s);' % (
            tuple(r3(v) for p in c for v in p) + (r3((uA + uB) * 0.5),
                                                 r3((wA + wB) * 0.5))))
        CUTS.append(1)

shipdetail.detail(J)

K = []
nL = shiplights.lights(K)
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '_lights.js'), 'w',
     encoding='utf-8').write('\n'.join(K))

print('floor/ceil %d  walls %d  lines %d  ribs %d  splays %d  '
      'kk panels %d  greeble %d  corner cuts %d' %
      (nfloor, nwall, nstrip, nrib, ncut, npil, ngre, len(CUTS)))
print('brush calls %d  quad calls %d' % (sum(1 for l in J if l.startswith('brush(')),
                                          sum(1 for l in J if l.startswith('quad('))))
print('lights %d  zones %d' % (nL, len(shiplights.ZONES)))
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '_shell.js'), 'w',
     encoding='utf-8').write('\n'.join(J))
