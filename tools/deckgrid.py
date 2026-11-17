"""The deck plan, read onto the kit's module grid.

`deckplan.py` is a survey in 10 cm units.  The kit is a module grid:
U = 1.200 m, and every part in it is a whole number of that
([[the-kit-is-one-module-grid]]).  Those two facts do not meet by
themselves -- a 22u passage is 3.19 m at the old 14.5 cm scale, which is
not a whole number of anything, so no run of bays can ever close.

So the scale is chosen to make them meet.  ONE MODULE IS EIGHT PLAN
UNITS.  That fixes 15 cm per plan unit, which is 3.4% off the 0.145 the
ship was already walked at -- below the threshold at which anybody
notices a room changed size -- and it lands every rectangle in the plan
on a module boundary by rounding rather than by luck.

  passage   4 modules across   4.80 m   -- the bench corridor's own width,
                                           and the width the deck zoning
                                           needs: margin, field, field,
                                           margin
  node      6 modules          7.20 m   -- 1.5x a corridor
  ship      49 x 100 modules   58.8 x 120.0 m

Everything downstream indexes CELLS, never plan units.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckplan as D

U = 1.200                     # the module, in metres
MU = 8                        # plan units to a module
S = U / MU                    # 0.150 m per plan unit
# THE ORIGIN IS A TEXTURE DECISION, NOT A SURVEY ONE.  Brush faces are
# world-projected at TEX_SCALE = 1/1.200, so a texture tile boundary sits at
# every world coordinate that is a whole multiple of 1.200 m.  The module
# grid has to start on one of those, or "one tile is one module" is false
# and every plate a shell material draws lands half a module out of step
# with the module the mesh is built on.
#
# The plan's own origin does not: (0 - 340) * 0.15 = -51.0 m, which is 42.5
# tiles, so the grid ran half a tile off in X and a quarter in Z.  Rounding
# it to 344 / 128 lands wx(0) = -51.6 = -43 tiles exactly and wz(0) = -19.2
# = -16 exactly.  It moves the whole ship 600 mm in X and 300 mm in Z and
# changes nothing else -- the plan is snapped in plan units, above this.
OX, OZ = 344.0, 128.0         # plan origin, in plan units -- see above

W_CORR = 4                    # modules across a passage
W_NODE = 6                    # modules across an engagement node
H_CORR = 3                    # ceiling, in modules: 3U = 3.60 m
H_NODE = 4                    # 4U = 4.80 m
H_MIN = 3                     # nothing in the ship is under 3U


def wx(gx):  return round((gx * MU - OX) * S, 4)     # world X of a grid line
def wz(gz):  return round((gz * MU - OZ) * S, 4)
def cx(gx):  return round(((gx + 0.5) * MU - OX) * S, 4)     # cell centre
def cz(gz):  return round(((gz + 0.5) * MU - OZ) * S, 4)
def snap(v): return int(round(v / float(MU)))


class Grid(object):
    """A cell holds the region that owns it and the height it was painted
       at.  Higher wins: a 6.4 m lobby swallows the 3.6 m passage that
       runs into it, which is what makes a doorway free."""
    def __init__(self):
        self.reg, self.ceil, self.REG = {}, {}, []

    def region(self, key, kind, name, sub):
        self.REG.append({'key': key, 'kind': kind, 'name': name, 'sub': sub})
        return len(self.REG) - 1

    def paint(self, gx0, gz0, gx1, gz1, ch, r):
        got = []
        for gz in range(gz0, gz1):
            for gx in range(gx0, gx1):
                k = (gx, gz)
                got.append(k)
                if ch > self.ceil.get(k, 0) or k not in self.reg:
                    self.ceil[k] = max(ch, self.ceil.get(k, 0))
                    self.reg[k] = r
        return got

    def open(self, gx, gz):  return (gx, gz) in self.reg
    def h(self, gx, gz):     return self.ceil.get((gx, gz), 0)


def build():
    """-> (grid, RUNS).  RUNS is every passage and node as it landed on the
       grid: ('corr'|'node', plan index, axis, gx0, gz0, gx1, gz1, cells)."""
    G, RUNS = Grid(), []

    for r in D.ROOMS:
        key, name, sub, x, y, w, h, ceil = (r[0], r[1], r[2], r[3], r[4],
                                            r[5], r[6], r[7])
        ri = G.region(key, 'room', name, sub)
        G.paint(snap(x), snap(y), snap(x + w), snap(y + h),
                max(H_MIN, int(round(ceil / U))), ri)

    # Passages are a fixed 4 modules across, centred on the run the plan
    # draws, and one module longer at each end so they bite into what they
    # serve -- the plan stops them at the wall line, and the wall has
    # thickness.
    RP = G.region('PASSAGE', 'corr', 'PASSAGE', 'main deck')
    for i, (x, y, w, h) in enumerate(D.CORR):
        if w >= h:
            z0 = int(round((y + h / 2.0) / float(MU) - W_CORR / 2.0))
            a0, a1 = snap(x) - 1, snap(x + w) + 1
            box = (a0, z0, a1, z0 + W_CORR)
            ax = 'x'
        else:
            x0 = int(round((x + w / 2.0) / float(MU) - W_CORR / 2.0))
            a0, a1 = snap(y) - 1, snap(y + h) + 1
            box = (x0, a0, x0 + W_CORR, a1)
            ax = 'z'
        RUNS.append(('corr', i, ax, box, G.paint(box[0], box[1], box[2],
                                                 box[3], H_CORR, RP)))

    RN = G.region('NODE', 'node', 'JUNCTION', 'engagement node')
    for i, (x, y, w, h) in enumerate(D.NODES):
        gx = int(round((x + w / 2.0) / float(MU) - W_NODE / 2.0))
        gz = int(round((y + h / 2.0) / float(MU) - W_NODE / 2.0))
        box = (gx, gz, gx + W_NODE, gz + W_NODE)
        RUNS.append(('node', i, 'z', box, G.paint(box[0], box[1], box[2],
                                                  box[3], H_NODE, RN)))
    return G, RUNS


if __name__ == '__main__':
    G, RUNS = build()
    xs = [k[0] for k in G.reg]; zs = [k[1] for k in G.reg]
    print('cells %d   gx %d..%d  gz %d..%d   %.1f x %.1f m' % (
        len(G.reg), min(xs), max(xs), min(zs), max(zs),
        (max(xs) - min(xs) + 1) * U, (max(zs) - min(zs) + 1) * U))
    per = {}
    for k, r in G.reg.items():
        per[G.REG[r]['key']] = per.get(G.REG[r]['key'], 0) + 1
    walls = sum(1 for (gx, gz) in G.reg
                for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1))
                if not G.open(gx + dx, gz + dz))
    print('wall modules %d' % walls)
    for k in sorted(per, key=lambda a: -per[a]):
        print('   %-10s %5d cells  %6.0f m2' % (k, per[k], per[k] * U * U))
