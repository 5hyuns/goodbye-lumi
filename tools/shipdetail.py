"""The dressing that turns a hull into a ship.

The look this ship is after rests on six things: a line of emissive along both skins,
a structural rib at a fixed beat, pipe runs overhead, grating underfoot, a
hazard band at knee height, and haze.  Only the first five are geometry --
the sixth is already in the renderer.
"""
import deckplan as D

S, OX, OZ = D.S, D.OX, D.OZ


def detail(J):
    def emit(s):
        J.append(s)

    def r3(v):
        return '%g' % round(v + 0.0, 3)

    def PX(x):
        return r3((x - OX) * S)

    def PZ(y):
        return r3((y - OZ) * S)

    def U(v):
        return r3(v * S)

    def raw(x0, y0, z0, x1, y1, z1, mat):
        emit('brush(%s,%s,%s, %s,%s,%s, T.%s);' % (
            PX(x0), r3(y0), PZ(z0), PX(x1), r3(y1), PZ(z1), mat))

    # ---- the corridor kit ------------------------------------------------
    def corridor_kit(x, y, w, h, ceil):
        horiz = w >= h
        a0, a1 = (x, x + w) if horiz else (y, y + h)
        c0, c1 = (y, y + h) if horiz else (x, x + w)
        span = c1 - c0

        def B(al0, al1, cr0, cr1, y0, y1, mat):
            if horiz:
                raw(al0, y0, cr0, al1, y1, cr1, mat)
            else:
                raw(cr0, y0, al0, cr1, y1, al1, mat)

        # The light line and the knee pinstripe used to be emitted here, once
        # per corridor rectangle.  At a junction three rectangles overlap, so
        # they stacked, and at an opening they flew straight across the gap.
        # They are generated from the wall faces in build_ship.py now: where
        # there is no wall there is no line.
        # pipe runs overhead
        for f in (0.30, 0.52, 0.74):
            pc = c0 + span * f
            B(a0, a1, pc - 1.0, pc + 1.0, ceil - 0.24, ceil - 0.06, 'PIPE')
        # structural ribs at a fixed beat: an ochre frame on steel posts
        t = a0 + 8
        while t < a1 - 8:
            B(t, t + 2.5, c0, c1, ceil - 0.34, ceil, 'PAINT')
            B(t, t + 2.5, c0, c0 + 1.4, 0, ceil, 'PANEL')
            B(t, t + 2.5, c1 - 1.4, c1, 0, ceil, 'PANEL')
            t += 32
        # grating underfoot
        g = a0 + 6
        while g < a1 - 10:
            B(g, g + 14, c0 + span * 0.28, c1 - span * 0.28, 0.001, 0.05, 'GRATE')
            g += 30

    WD = D.PASSAGE_WIDEN
    emit('/* ---- corridor kit ----------------------------------------------- */')
    for (x, y, w, h) in D.CORR:
        if w >= h:
            corridor_kit(x, y - WD, w, h + 2 * WD, D.CEIL_CORR)
        else:
            corridor_kit(x - WD, y, w + 2 * WD, h, D.CEIL_CORR)
    for (x, y, w, h) in D.NODES:
        corridor_kit(x - WD, y - WD, w + 2 * WD, h + 2 * WD, D.CEIL_NODE)
        for (cx, cy) in ((x + 3, y + 3), (x + w - 6, y + 3),
                         (x + 3, y + h - 6), (x + w - 6, y + h - 6)):
            raw(cx, 0, cy, cx + 3, D.CEIL_NODE, cy + 3, 'PANEL')

    # ---- bulkhead door frames.  the leaf is dynamic and comes later -------
    emit('/* ---- bulkhead door frames --------------------------------------- */')
    emit('const SHIP_DOORS=[];')
    for (x, y, w, h) in D.DOORS:
        horiz = w >= h
        if horiz:                     # the leaf spans the passage: widen with it
            x, w = x - WD, w + 2 * WD
        else:
            y, h = y - WD, h + 2 * WD
        # the head only.  Everything below 2.42 m is the opening -- the leaf
        # that fills it is dynamic and belongs to the sinking gauge, not here.
        raw(x, 2.62, y, x + w, D.CEIL_CORR, y + h, 'PANEL')
        if horiz:
            raw(x - 2.5, 0, y - 0.5, x, 2.8, y + h + 0.5, 'PAINT')
            raw(x + w, 0, y - 0.5, x + w + 2.5, 2.8, y + h + 0.5, 'PAINT')
        else:
            raw(x - 0.5, 0, y - 2.5, x + w + 0.5, 2.8, y, 'PAINT')
            raw(x - 0.5, 0, y + h, x + w + 0.5, 2.8, y + h + 2.5, 'PAINT')
        raw(x, 0.001, y, x + w, 0.05, y + h, 'HAZARD')
        emit('SHIP_DOORS.push({x:%s,z:%s,w:%s,d:%s,ax:%d});' % (
            PX(x + w / 2.0), PZ(y + h / 2.0), U(w), U(h), 1 if horiz else 0))

    # ---- the map lies: three passages that end -----------------
    emit('/* ---- collapse and decompression ---------------------------------- */')
    for (x, y, w, h, kind) in D.BLOCKED:
        mat = 'RUST' if kind == 'collapse' else 'PANEL'
        raw(x - 5, 0, y - 5, x + w + 5, 3.0, y + h + 5, mat)
        if kind == 'collapse':
            raw(x - 14, 0, y + 1, x - 5, 1.5, y + h - 1, 'RUST')
            raw(x + w + 5, 0, y + 2, x + w + 13, 1.1, y + h - 2, 'RUST')
        else:
            raw(x - 6, 0, y - 6, x - 4, 3.0, y + h + 6, 'HAZARD')
    ex, ey, ew, eh = D.EVENT_DOOR
    raw(ex, 0, ey, ex + ew, 2.8, ey + eh, 'PANEL')
    raw(ex - 0.6, 0.9, ey, ex, 1.1, ey + eh, 'HAZARD')

    # ---- quarters --------------------------------------------------------
    emit('/* ---- crew quarters ----------------------------------------------- */')
    for (x, y, w, h) in D.BEDS:
        for lvl in (0.0, 1.05):
            raw(x, lvl + 0.36, y, x + w, lvl + 0.50, y + h, 'CRATE')
            raw(x, lvl, y, x + 2.5, lvl + 0.36, y + h, 'PANEL')
            raw(x + w - 2.5, lvl, y, x + w, lvl + 0.36, y + h, 'PANEL')
        raw(x - 1, 0, y, x + w + 1, 1.9, y + 1.2, 'PANEL')
    raw(232, 0, 145, 258, 1.15, 152, 'CONSOLE')
    raw(234, 1.15, 146, 256, 1.75, 147.4, 'SCREEN')

    # ---- maintenance store -----------------------------------------------
    # The fixtures are unchanged; only what the room is called is.  See the
    # note on ROOMS in deckplan.py -- the rectangles are E-08's spawn stands.
    emit('/* ---- maintenance store -------------------------------------------- */')
    for (x, y, w, h) in D.PLANTERS:
        raw(x, 0, y, x + w, 0.55, y + h, 'PANEL')
        raw(x + 1, 0.55, y + 1, x + w - 1, 0.72, y + h - 1, 'DARK')
        raw(x + 4, 2.9, y + 3, x + w - 4, 3.05, y + h - 3, 'LAMP')
        for k in range(6):
            sx = x + 6 + k * (w - 12) / 5.0
            raw(sx, 0.72, y + h / 2.0 - 1, sx + 1.4,
                1.5 + (k % 3) * 0.25, y + h / 2.0 + 1, 'PIPE')
    raw(432, 0, 232, 528, 0.06, 238, 'HAZARD')

    # ---- mess ------------------------------------------------------------
    emit('/* ---- mess hall ---------------------------------------------------- */')
    for (tx, tz, ov) in ((432, 316, 0), (466, 316, 1), (500, 316, 0),
                         (432, 344, 1), (476, 348, 0)):
        if ov:
            raw(tx, 0, tz, tx + 22, 0.9, tz + 2.5, 'PANEL')
            raw(tx + 2, 0, tz + 2.5, tx + 5, 0.35, tz + 14, 'PAINT')
        else:
            raw(tx, 0.66, tz, tx + 22, 0.78, tz + 13, 'PANEL')
            for (cx, cz) in ((tx + 2, tz + 2), (tx + 18, tz + 2),
                             (tx + 2, tz + 9), (tx + 18, tz + 9)):
                raw(cx, 0, cz, cx + 2, 0.66, cz + 2, 'PAINT')
    raw(422, 0, 302, 428, 2.0, 336, 'CONSOLE')
    raw(428, 0.9, 306, 430, 1.7, 332, 'SCREEN')

    # ---- lobby -----------------------------------------------------------
    emit('/* ---- central lobby ------------------------------------------------ */')
    for (x, y, w, h) in D.BENCHES:
        raw(x, 0.42, y, x + w, 0.54, y + h, 'PANEL')
        raw(x + 1, 0, y + 1, x + 4, 0.42, y + h - 1, 'PAINT')
        raw(x + w - 4, 0, y + 1, x + w - 1, 0.42, y + h - 1, 'PAINT')
    raw(322, 0, 470, 358, 1.12, 480, 'CONSOLE')
    raw(326, 1.12, 472, 354, 2.05, 474, 'SCREEN')
    raw(320, 0, 468, 360, 0.06, 482, 'HAZARD')
    for (px, pz) in ((286, 416), (394, 416), (286, 514), (394, 514)):
        raw(px, 0, pz, px + 8, 5.0, pz + 8, 'PAINT')
        raw(px - 1, 4.2, pz - 1, px + 9, 4.5, pz + 9, 'RUST')

    # ---- escape gallery --------------------------------------------------
    emit('/* ---- escape gallery ----------------------------------------------- */')
    for (bx, by, bw, bh) in D.BAYS:
        raw(bx - 2, 0, by - 2, bx, 2.5, by + bh + 2, 'PANEL')
        raw(bx + bw, 0, by - 3, bx + bw + 2, 2.5, by - 1, 'HAZARD')
        raw(bx + bw, 0, by + bh + 1, bx + bw + 2, 2.5, by + bh + 3, 'HAZARD')
        raw(bx + 2, 0.001, by + 2, bx + bw + 20, 0.05, by + bh - 2, 'GRATE')
    cx, cy, cw, ch = D.CAPSULE
    raw(cx - 2, 0, cy - 2, cx, 2.7, cy + ch + 2, 'PANEL')
    raw(cx, 0, cy, cx + 4, 2.4, cy + ch, 'PANEL')
    raw(cx + 1, 1.0, cy + 4, cx + 4, 1.9, cy + ch - 4, 'SCREEN')
    raw(cx + cw, 1.9, cy - 3, cx + cw + 2, 2.2, cy + ch + 3, 'LAMP')
    kx, ky, kw, kh = D.CONSOLE
    raw(kx, 0, ky, kx + kw, 1.15, ky + kh, 'CONSOLE')
    raw(kx - 1, 1.15, ky - 1, kx + kw + 1, 1.24, ky + kh + 1, 'PANEL')
    for (sx, sy, sw, sh, lit) in D.SLOTS:
        raw(sx, 1.24, sy, sx + sw, 1.30, sy + sh, 'SCREEN' if lit else 'DARK')
        raw(sx - 1, 1.15, sy - 1, sx + sw + 1, 1.24, sy + sh + 1, 'PAINT')

    # ---- medical ---------------------------------------------------------
    emit('/* ---- medical bay -------------------------------------------------- */')
    for bz in (438, 458, 478):
        raw(459, 0.62, bz, 477, 0.74, bz + 12, 'PANEL')
        raw(460, 0, bz + 1, 462, 0.62, bz + 11, 'PAINT')
        raw(474, 0, bz + 1, 476, 0.62, bz + 11, 'PAINT')
        raw(477, 0, bz + 2, 481, 1.7, bz + 6, 'CONSOLE')
        raw(477, 1.2, bz + 2.4, 477.6, 1.65, bz + 5.6, 'SCREEN')
    raw(455, 0, 486, 535, 0.06, 490, 'HAZARD')

    # ---- stores ----------------------------------------------------------
    emit('/* ---- stores -------------------------------------------------------- */')
    for (x, y, w, h, hh, lv) in ((230, 648, 14, 12, 1.00, 0.0),
                                 (230, 648, 12, 10, 0.85, 1.0),
                                 (248, 646, 13, 13, 1.10, 0.0),
                                 (296, 650, 14, 12, 1.00, 0.0),
                                 (296, 650, 12, 10, 0.80, 1.0),
                                 (232, 686, 13, 12, 0.95, 0.0),
                                 (270, 690, 15, 11, 1.15, 0.0)):
        raw(x, lv, y, x + w, lv + hh, y + h, 'CRATE')
    for k in range(3):
        raw(226, 0.9 + k * 1.1, 660, 238, 1.02 + k * 1.1, 700, 'PANEL')

    # ---- cargo hold ------------------------------------------------------
    emit('/* ---- cargo hold ---------------------------------------------------- */')
    for (x, y, w, h, top) in D.CONTAINERS:
        raw(x, 0, y, x + w, top, y + h, 'CRATE')
        raw(x - 0.5, top, y - 0.5, x + w + 0.5, top + 0.12, y + h + 0.5, 'PANEL')
        raw(x, 0.4, y, x + w, 0.56, y + 0.6, 'HAZARD')
    px, py, pw, ph, pt = D.PLATFORM
    raw(px, pt, py, px + pw, pt + 0.22, py + ph, 'GRATE')
    raw(px, pt + 0.22, py, px + pw, pt + 1.1, py + 1.2, 'PIPE')
    raw(px, pt + 0.22, py + ph - 1.2, px + pw, pt + 1.1, py + ph, 'PIPE')
    for (sx, sz) in ((px + 1, py + 1), (px + pw - 3, py + 1), (px + 1, py + ph - 3)):
        raw(sx, 0, sz, sx + 2, pt, sz + 2, 'PAINT')
    dx, dy, dw, dh = D.DUCT
    raw(dx - 1.5, 0, dy - 1.5, dx, 2.2, dy + dh + 1.5, 'PANEL')
    raw(dx + dw, 0, dy - 1.5, dx + dw + 1.5, 2.2, dy + dh + 1.5, 'PANEL')
    raw(dx, 2.0, dy, dx + dw, 2.2, dy + dh, 'GRATE')
    for k in range(4):
        raw(424 + (k % 2) * 20, 0, 700 + (k // 2) * 22,
            440 + (k % 2) * 20, 1.4 + (k % 2) * 0.5, 716 + (k // 2) * 22, 'CRATE')

    # ---- engine room -----------------------------------------------------
    emit('/* ---- engine room --------------------------------------------------- */')
    for (x, y, w, h) in D.REACTORS:
        raw(x, 0, y, x + w, 5.2, y + h, 'PANEL')
        for ry in (1.2, 2.8, 4.4):
            raw(x - 1, ry, y - 1, x + w + 1, ry + 0.28, y + h + 1, 'HAZARD')
        raw(x + 6, 5.2, y + 3, x + w - 6, 5.9, y + h - 3, 'SCREEN')
        raw(x - 2, 0, y - 2, x + w + 2, 0.22, y + h + 2, 'GRATE')
    for k in range(6):
        raw(195 + k * 26, 0, 850, 213 + k * 26, 2.1, 866, 'CONSOLE')
        raw(197 + k * 26, 1.1, 866, 211 + k * 26, 1.9, 866.6, 'SCREEN')
    for px in (232, 300, 380, 448):
        raw(px, 0, 916, px + 7, 6.5, 923, 'PAINT')
    raw(185, 0, 926, 495, 0.06, 930, 'HAZARD')

    # ---- the three ammunition caches, at the end of the three dead ends ---
    emit('/* ---- ammunition caches ---------------------------------------------- */')
    for (x, y, w, h) in D.AMMO:
        raw(x, 0, y, x + w, 0.9, y + h, 'CRATE')
        raw(x - 0.6, 0.9, y - 0.6, x + w + 0.6, 1.0, y + h + 0.6, 'PANEL')
        raw(x, 0.34, y - 0.7, x + w, 0.5, y, 'HAZARD')
