"""Lights and cubemap zones for the derelict ship.

Stage 0 of the sinking gauge: the ship is still running, and already dark.
Cold white where the systems still hold, amber wherever there is machinery,
one green room.  Nothing here is a fixture model -- the corridor strips are
already geometry, so every corridor light is noFix.
"""
import deckplan as D

S, OX, OZ = D.S, D.OX, D.OZ


def X(x):
    return round((x - OX) * S, 2)


def Z(y):
    return round((y - OZ) * S, 2)


# Doom 3's palette, not a hospital's.  Sodium and amber carry almost
# everything; cold white is rationed to the rooms whose systems still hold;
# monitor green and warning red are small and never light a room.
COOL = '[0.52,0.66,0.98]'      # cold white, thin and blue
WARM = '[1.00,0.72,0.34]'      # sodium
AMBER = '[1.00,0.52,0.14]'     # amber
WHITE = '[0.86,0.90,1.00]'
TEAL = '[0.26,1.00,0.52]'      # monitor green
RED = '[1.00,0.15,0.09]'       # warning

# room lights: (svgx, svgy, height, colour, range, flicker)
ROOM_LIGHTS = [
    # crew quarters -- where you wake up
    (185, 160, 2.55, WHITE, 8, 0), (235, 160, 2.55, COOL, 8, 0.35),
    (210, 200, 2.55, COOL, 8, 0),
    # maintenance store -- pack 2's room
    (480, 150, 3.9, TEAL, 12, 0), (480, 192, 3.9, TEAL, 12, 0),
    (455, 225, 3.6, COOL, 9, 0), (505, 225, 3.6, COOL, 9, 0.4),
    # mess hall -- first contact
    (445, 320, 2.9, WARM, 10, 0), (505, 320, 2.9, WARM, 10, 0.55),
    (475, 355, 2.9, COOL, 8, 0),
    # central lobby -- the hub, and the last safe room
    (300, 425, 5.6, WHITE, 14, 0), (380, 425, 5.6, WHITE, 14, 0),
    (300, 465, 5.6, COOL, 14, 0), (380, 465, 5.6, COOL, 14, 0.26),
    (300, 508, 5.6, COOL, 14, 0), (380, 508, 5.6, WHITE, 14, 0),
    (340, 465, 2.6, COOL, 8, 0),
    # escape gallery -- five bays gone, one left
    (174, 435, 4.4, COOL, 11, 0), (174, 480, 4.4, COOL, 11, 0.3),
    (174, 525, 4.4, WHITE, 11, 0), (174, 570, 4.4, COOL, 11, 0),
    (174, 615, 3.4, AMBER, 10, 0),
    # medical bay
    (475, 445, 2.8, WHITE, 9, 0), (495, 480, 2.8, COOL, 8, 0.45),
    # stores
    (250, 660, 2.9, AMBER, 9, 0), (295, 690, 2.9, WARM, 9, 0),
    # cargo hold -- 13 x 20 m and 8.4 m tall, stacked, the biggest wave
    (440, 706, 7.4, AMBER, 15, 0), (505, 706, 7.4, COOL, 15, 0),
    (440, 752, 7.4, WARM, 15, 0), (505, 752, 7.4, AMBER, 15, 0.28),
    (440, 798, 7.4, AMBER, 15, 0), (505, 798, 7.4, WARM, 15, 0),
    (472, 822, 7.4, COOL, 14, 0),
    (497, 728, 4.0, WHITE, 9, 0), (450, 780, 3.2, AMBER, 9, 0.4),
    # engine room -- 45 x 13 m, the biggest volume on the ship.  Seven lamps
    # left it black; a hall this long needs a lamp roughly every 6 m or the
    # far end simply is not there.
    (205, 856, 6.8, AMBER, 15, 0), (250, 856, 6.8, WARM, 15, 0),
    (295, 856, 6.8, AMBER, 15, 0.30), (340, 856, 6.8, AMBER, 15, 0),
    (385, 856, 6.8, WARM, 15, 0), (430, 856, 6.8, AMBER, 15, 0),
    (475, 856, 6.8, AMBER, 15, 0.34),
    (205, 916, 6.8, COOL, 14, 0), (265, 916, 6.8, AMBER, 14, 0),
    (340, 916, 6.8, COOL, 14, 0), (415, 916, 6.8, AMBER, 14, 0),
    (475, 916, 6.8, COOL, 14, 0.28),
    (340, 886, 3.0, TEAL, 11, 0), (425, 886, 3.0, TEAL, 11, 0),
]

ZONES = [
    ('CREW QUARTERS',     'Bow · Port',          160, 260, 140, 215, (210, 1.7, 177)),
    ('MAINTENANCE STORE', 'Bow · Starboard',     430, 530, 130, 240, (480, 2.2, 185)),
    ('MESS HALL',         'Midship · Starboard', 420, 530, 300, 370, (475, 1.9, 335)),
    ('CENTRAL LOBBY',     'Midship · Hub',       270, 410, 400, 530, (340, 2.6, 445)),
    ('ESCAPE GALLERY',    'Port · Outer hull',   145, 203, 420, 640, (174, 2.1, 435)),
    ('MEDICAL BAY',       'Midship · Starboard', 455, 535, 430, 495, (495, 1.8, 445)),
    ('STORES',            'Midship · Lower',     225, 315, 640, 705, (270, 1.9, 672)),
    ('CARGO HOLD',        'Stern · Starboard',   420, 530, 690, 830, (450, 3.4, 745)),
    ('ENGINE ROOM',       'Stern · Full beam',   185, 495, 840, 930, (340, 3.2, 895)),
    ('BOW PASSAGE',       'Frames 14–32',        190, 500, 155, 320, (319, 1.6, 326)),
    ('MIDSHIP PASSAGE',   'Frames 32–54',        195, 515, 320, 540, (236, 1.6, 466)),
    ('LOWER PASSAGE',     'Frames 54–79',        195, 520, 540, 790, (301, 1.6, 585)),
    ('STERN PASSAGE',     'Frames 79–94',        190, 430, 740, 850, (211, 1.6, 794)),
]

VOID_CUBE = (319, 1.6, 326)


def lights(J):
    def emit(s):
        J.append(s)

    out = []

    def L(sx, sy, h, col, r, flick=0, nofix=True):
        s = '{p:[%s,%s,%s], c:%s, r:%s' % (X(sx), h, Z(sy), col, r)
        if flick:
            s += ', flick:%s' % flick
        if nofix:
            s += ', noFix:1'
        out.append(s + '}')

    # A lamp every 6.4 m, and the pool has to end before the next one starts --
    # that gap is the whole look.  Every third fitting has failed.
    fixtures = []

    def run(x, y, w, h, ceil):
        horiz = w >= h
        a0, a1 = (x, x + w) if horiz else (y, y + h)
        cc = (y + h / 2.0) if horiz else (x + w / 2.0)
        n = max(1, int((a1 - a0) / 64.0 + 0.5))
        for k in range(n):
            t = a0 + (a1 - a0) * (k + 0.5) / n
            sx, sy = (t, cc) if horiz else (cc, t)
            col = COOL if sy < 320 else (WARM if sy < 640 else AMBER)
            dead = (k % 4 == 2)
            L(sx, sy, ceil - 0.34, col, 6.2 if dead else 9.2,
              0.62 if dead else 0)
            fixtures.append((sx, sy, ceil - 0.30, 1 if horiz else 0))

    for (x, y, w, h) in D.CORR:
        run(x, y, w, h, D.CEIL_CORR)
    for (x, y, w, h) in D.NODES:
        L(x + w / 2.0, y + h / 2.0, D.CEIL_NODE - 0.4, WARM, 10.5, 0)
        fixtures.append((x + w / 2.0, y + h / 2.0, D.CEIL_NODE - 0.36, 1))

    # rooms are lit in pools, not blanket-lit
    for (sx, sy, hh, col, r, fl) in ROOM_LIGHTS:
        L(sx, sy, hh, col, round(r * 1.05, 1), fl)
        fixtures.append((sx, sy, hh + 0.04, 1))

    # every bulkhead carries a warning light -- small, red, lights nothing
    for (x, y, w, h) in D.DOORS:
        L(x + w / 2.0, y + h / 2.0, 2.72, RED, 3.4, 0.2)

    # the dead ends announce themselves before you reach them
    for (x, y, w, h, kind) in D.BLOCKED:
        L(x + w / 2.0, y + h / 2.0, 2.2, RED, 5.0, 0.55)

    emit('const LIGHTS=[\n  ' + ',\n  '.join(out) + '\n];')

    # ---- the fitting itself.  A caged lamp: a housing, a bright face, and
    #      three bars across it.  The bars are the reason it reads as Doom 3
    #      rather than as a glowing rectangle -- they break the pool up.
    emit('''function shipLamp(x,y,z,ax){
  const L=0.74, W=0.26;                       // along the run, across it
  const ax0=ax?L:W, az0=ax?W:L;
  brush(x-ax0,y,z-az0, x+ax0,y+0.16,z+az0, T.PANEL);          // housing
  brush(x-ax0*0.86,y-0.09,z-az0*0.86, x+ax0*0.86,y,z+az0*0.86, T.LAMP);
  for(let i=-1;i<=1;i++){                                      // the cage
    const o=i*(ax?0.30:0.10), p=i*(ax?0.10:0.30);
    brush(x+o-0.035,y-0.13,z+p-0.035, x+o+0.035,y-0.07,z+p+0.035, T.PAINT);
  }
  brush(x-ax0-0.05,y+0.13,z-az0-0.05, x+ax0+0.05,y+0.22,z+az0+0.05, T.PAINT);
}''')
    for (sx, sy, hh, ax) in fixtures:
        emit('shipLamp(%s,%s,%s,%d);' % (X(sx), round(hh, 2), Z(sy), ax))

    zs = []
    for (n, sub, x0, x1, y0, y1, c) in ZONES:
        zs.append("{n:'%s', s:'%s', x0:%s,x1:%s, z0:%s,z1:%s, c:[%s,%s,%s]}" % (
            n, sub, X(x0) - 0.4, X(x1) + 0.4, Z(y0) - 0.4, Z(y1) + 0.4,
            X(c[0]), c[1], Z(c[2])))
    emit('const ZONES=[\n  ' + ',\n  '.join(zs) + '\n];')
    emit('const VOID_CUBE=[%s,%s,%s];' % (X(VOID_CUBE[0]), VOID_CUBE[1], Z(VOID_CUBE[2])))
    emit('function zoneAt(x,z){')
    emit('  for(let i=0;i<ZONES.length;i++){ const Z=ZONES[i];')
    emit('    if(x>=Z.x0&&x<=Z.x1&&z>=Z.z0&&z<=Z.z1) return i; }')
    emit('  return ZONES.length;')
    emit('}')
    return len(out)
