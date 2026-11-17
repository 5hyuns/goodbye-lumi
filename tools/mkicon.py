"""mkicon.py -- LUMI, as the executable's icon.

    python tools/mkicon.py            -> native/build/icon.ico + icon.rc
    python tools/mkicon.py preview.png   ... and a 256 px PNG to look at

THE ICON IS DRAWN, NOT PHOTOGRAPHED, for the reason nothing else in this
tree is on disk either: an .ico committed beside the source is bytes off
the 1,474,560 and a second thing to keep in step with her design.  So it
is generated the way plan_data.h is -- native/build/ is a build product,
and build.sh regenerates this whenever the script is newer than the file.

WHAT IS BEING DRAWN is her front three-quarter-less view: the egg of white
clearcoat paint (LUMI-SPEC.md's `paint`, base #dedace), the dark visor
band across the top third, and the two cyan eyes the display's idle state
puts there (face.c's `eyes()`, 64 px boxes at CX +- 82 on a 512 x 290
canvas -- the same ratio here).  Then the lower dark band with its two
lamp dots, and the status LED on the crown.

AND IT IS DRAWN ONCE PER SIZE rather than once and downsampled, because
16 px is not a small picture of a 256 px one: at that size the visor is
three rows and the eyes are one, so the details have to be laid out in
the pixel grid they will be read in.  What scales with size is which
details exist at all -- DETAIL below is that ladder.

The sizes are the ones Explorer actually asks for (16 list, 32 desktop,
48 medium icons, 256 extra large); 24 and 64 are there because a DPI
between two of them scales the wrong neighbour otherwise.  The four
small ones are 32-bit BGRA DIBs, which every shell since 95 can
read; 64 and 256 are PNG, which is how a large icon has been stored since
Vista and which is worth having here -- a 64 DIB is 16,936 bytes and the
PNG of the same picture is 4,221, and the whole .ico is 46,883 of the
1,474,560 rather than 59,598.
"""
import math, os, struct, sys, zlib

# ---- the palette, out of LUMI-SPEC.md section 4 -----------------------
PAINT  = (0xde, 0xda, 0xce)      # 흰 도장, clearcoat
DARKP  = (0x1a, 0x20, 0x26)      # panel/dark -- the visor and the band
CYAN   = (0x62, 0xd8, 0xff)      # emissive_cyan -- eyes, lamps, LED

SIZES  = [16, 24, 32, 48, 64, 256]
SS     = 4                        # supersamples a side; 256 halves it

# which details a size can hold.  A dot that lands on a third of a pixel
# is not a dot, it is a smudge on the paint.
def detail(px):
    return {
        'band':  px >= 24,        # the lower dark band
        'lamps': px >= 32,        # ... and the two lamps in it
        'led':   px >= 48,        # the crown LED
        'glow':  px >= 32,        # the cyan wash under her
    }


# ---- shapes, in a unit square with y down -----------------------------
def egg(x, y):
    """signed-ish: <1 inside.  An ellipse whose width tapers toward the
    crown, because she is an egg and a circle reads as a ball."""
    dy = (y - 0.505) / 0.415
    taper = 1.0 - 0.13 * max(0.0, -dy) ** 1.6
    dx = (x - 0.5) / (0.400 * taper)
    return math.sqrt(dx * dx + dy * dy)


def rrect(x, y, cx, cy, hx, hy, r):
    """rounded box, distance in unit-square units (<=0 inside)"""
    qx = abs(x - cx) - (hx - r)
    qy = abs(y - cy) - (hy - r)
    ax, ay = max(qx, 0.0), max(qy, 0.0)
    return math.sqrt(ax * ax + ay * ay) + min(max(qx, qy), 0.0) - r


def mix(a, b, t):
    t = 0.0 if t < 0 else (1.0 if t > 1 else t)
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def shade(c, k):
    return tuple(v * k for v in c)


def sample(x, y, D):
    """one supersample -> (r,g,b,a) floats 0..255 / 0..1"""
    r = g = b = 0.0
    a = 0.0

    # ---- the emitter, which is the one thing about her that is light
    if D['glow']:
        gx, gy = (x - 0.5) / 0.30, (y - 0.99) / 0.30
        gl = 1.0 - math.sqrt(gx * gx + gy * gy)
        if gl > 0:
            gl = gl * gl * 0.55
            r, g, b = shade(CYAN, gl)
            a = gl

    d = egg(x, y)
    if d < 1.0:
        # THE PAINT IS LIT FROM ABOVE AND IN FRONT, and the rim is what
        # makes it a sphere rather than a disc: k falls off with the
        # radius, not with y, or the silhouette goes flat at the sides.
        k = 1.05 - 0.20 * d * d
        k += 0.22 * max(0.0, 0.74 - math.hypot((x - 0.35) / 0.32,
                                               (y - 0.25) / 0.28))
        k -= 0.26 * max(0.0, (y - 0.60) / 0.40) ** 1.7
        # and the bounce off the deck, which is the second thing a
        # clearcoat does: a bright edge where the shell turns away
        k += 0.20 * max(0.0, (d - 0.84) / 0.16) * max(0.0, (y - 0.48) / 0.52)
        col = shade(PAINT, max(k, 0.30))

        # ---- the lower band: a dark ring, the two lamps in it
        if D['band'] and 0.735 < y < 0.830:
            col = shade(DARKP, 1.0 + 0.5 * (1.0 - d))
            if D['lamps']:
                for sx in (-0.205, 0.205):
                    lp = math.hypot((x - (0.5 + sx)) / 0.052,
                                    (y - 0.783) / 0.030)
                    if lp < 1.0:
                        col = mix(col, CYAN, 1.0 - lp * lp * 0.55)

        # ---- the visor.  Wide, heavily rounded, and it sits high: the
        # face is in the top third of the shell and the name is under it.
        v = rrect(x, y, 0.5, 0.375, 0.310, 0.140, 0.115)
        if v < 0.0:
            # a little sky in the glass, so it reads as glass
            col = shade(DARKP, 1.0 + 0.9 * max(0.0, 0.30 - (y - 0.245)) / 0.30)
            # ---- the eyes.  face.c draws 64 px boxes at CX +- 82 on a
            # 512-wide canvas: 0.16 of the width apart, 0.125 wide.
            for sx in (-0.160, 0.160):
                e = rrect(x, y, 0.5 + sx, 0.383, 0.062, 0.052, 0.040)
                if e < 0.0:
                    col = CYAN
                elif e < 0.075:                    # the bloom on the ink
                    col = mix(col, CYAN, (1.0 - e / 0.075) ** 2 * 0.75)
        elif D['led'] and math.hypot((x - 0.5) / 0.030,
                                     (y - 0.135) / 0.022) < 1.0:
            col = CYAN                             # the status LED

        r, g, b = col
        a = 1.0
    return r, g, b, a


def render(px):
    """one size, supersampled, straight (not premultiplied) BGRA"""
    ss = SS if px < 64 else 2
    D = detail(px)
    out = bytearray(px * px * 4)
    for iy in range(px):
        for ix in range(px):
            r = g = b = a = 0.0
            for sy in range(ss):
                y = (iy + (sy + 0.5) / ss) / px
                for sx in range(ss):
                    x = (ix + (sx + 0.5) / ss) / px
                    sr, sg, sb, sa = sample(x, y, D)
                    r += sr * sa; g += sg * sa; b += sb * sa; a += sa
            n = ss * ss
            a /= n
            if a > 0.0001:
                r, g, b = r / n / a, g / n / a, b / n / a
            o = (iy * px + ix) * 4
            out[o + 0] = min(255, int(b + 0.5))
            out[o + 1] = min(255, int(g + 0.5))
            out[o + 2] = min(255, int(r + 0.5))
            out[o + 3] = min(255, int(a * 255 + 0.5))
    return out


# ---- the two containers ------------------------------------------------
def dib(px, bgra):
    """BITMAPINFOHEADER + bottom-up XOR + the 1bpp AND mask nobody reads
    any more and every loader still measures."""
    hdr = struct.pack('<IiiHHIIiiII', 40, px, px * 2, 1, 32, 0,
                      px * px * 4, 0, 0, 0, 0)
    rows = [bytes(bgra[y * px * 4:(y + 1) * px * 4]) for y in range(px)]
    xor = b''.join(reversed(rows))
    stride = ((px + 31) // 32) * 4
    mask = bytearray(stride * px)          # all zero = "opaque", per spec
    return hdr + xor + bytes(mask)


def png(px, bgra):
    raw = bytearray()
    for y in range(px):
        raw.append(0)
        for x in range(px):
            o = (y * px + x) * 4
            raw += bytes((bgra[o + 2], bgra[o + 1], bgra[o], bgra[o + 3]))

    def chunk(t, d):
        return (struct.pack('>I', len(d)) + t + d +
                struct.pack('>I', zlib.crc32(t + d) & 0xffffffff))
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', px, px, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 9)) +
            chunk(b'IEND', b''))


def ico(images):
    n = len(images)
    head = struct.pack('<HHH', 0, 1, n)
    off = 6 + 16 * n
    ent, blob = b'', b''
    for px, data in images:
        ent += struct.pack('<BBBBHHII', px & 0xff, px & 0xff, 0, 0, 1, 32,
                           len(data), off)
        off += len(data)
        blob += data
    return head + ent + blob


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, '..', 'native', 'build')
    os.makedirs(out, exist_ok=True)

    images, preview = [], None
    for px in SIZES:
        bgra = render(px)
        images.append((px, png(px, bgra) if px >= 64 else dib(px, bgra)))
        if px == 256:
            preview = png(px, bgra)

    blob = ico(images)
    with open(os.path.join(out, 'icon.ico'), 'wb') as f:
        f.write(blob)
    with open(os.path.join(out, 'icon.rc'), 'w') as f:
        f.write('/* generated by tools/mkicon.py -- do not edit */\n'
                '1 ICON "icon.ico"\n')
    if len(sys.argv) > 1 and preview:
        with open(sys.argv[1], 'wb') as f:
            f.write(preview)
    print('icon.ico  %d bytes  (%s)' %
          (len(blob), ' '.join(str(s) for s in SIZES)))


if __name__ == '__main__':
    main()
