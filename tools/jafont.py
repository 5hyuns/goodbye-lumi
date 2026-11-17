"""jafont.py -- the Japanese glyph sheet, and only the glyphs that are used.

    python tools/jafont.py        # story/*.json + the .c files -> native/src/jafont_data.h

WHY THIS IS THE ONE TYPEFACE IN THE TREE THAT IS STORED.

    Hangul is 24 letters and a composition rule, so kfont.c draws it: 119
    strokes, five bytes each, and the whole syllabary falls out.  Latin is a
    5x7 stencil in hud.c, 51 characters for 357 bytes.  Japanese has neither
    property.  There is no rule that builds a kanji out of parts small
    enough to store, and hand-authoring 300 of them as stroke coordinates
    produces something nobody would call Japanese.

    So this is the exception, and it is bounded the way the rest of the
    build is bounded: it stores the glyphs the GAME USES and not a font.
    Every Japanese string in the tree is scanned, the distinct codepoints
    are rasterised once at CELL dots, and what lands in native/src is that
    set and nothing else -- about 30 KB for the whole story, against the
    500-odd KB the exe has spare.  Add a line of Japanese and re-run this,
    exactly as a change to story/*.json means re-running storypack.py; a
    character with no glyph draws as a hollow box rather than as nothing,
    so a forgotten run says so on the screen.

    24 dots is not a guess: it is the size Japanese bitmap fonts were
    standardised at (JIS X 9052) because it is the smallest cell in which a
    19-stroke kanji stays a different character from its neighbours, and it
    is also the size the subtitles are drawn at -- so the common case is
    one texel to one pixel.

THE RASTERISER IS WINDOWS' OWN, THROUGH ctypes.

    No dependency is added: ctypes is stdlib, and GDI is already linked
    into the exe.  The font is only needed HERE, when the header is
    written -- playlumi.exe carries the bits and asks Windows for nothing.
    That is the same bargain nativepack.py makes with node.
"""
import ctypes, json, os, re, sys
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'native', 'src', 'jafont_data.h')

CELL = 24          # dots per side.  See the docstring: JIS X 9052's number.
COLS = 32          # glyph cells across the sheet
PAD = 10           # slack around the raster box, so a spill is measured not clipped

# MS Gothic first: it is hinted for exactly this size and its strokes stay
# one dot wide, which is what a 1-bit cell needs.  The rest are fallbacks in
# descending order of how well they survive being thresholded.
FACES = ['MS Gothic', 'MS UI Gothic', 'Yu Gothic UI', 'Meiryo', 'Yu Gothic']

# What counts as Japanese.  Hangul (AC00..D7A3) is deliberately outside every
# one of these: story.c holds the Korean as octal escapes in the same files.
RANGES = [(0x3000, 0x303F),    # 、。「」〜 and the rest of CJK punctuation
          (0x3040, 0x309F),    # hiragana
          (0x30A0, 0x30FF),    # katakana, and the long mark at 30FC
          (0x4E00, 0x9FFF),    # CJK unified ideographs
          (0xFF01, 0xFF60)]    # fullwidth forms, if any slip in


def is_ja(cp):
    return any(a <= cp <= b for a, b in RANGES)


# ---- collecting what the game actually says -------------------------------
OCTAL = re.compile(r'(?:\\[0-3][0-7]{2})+')


def cps_from_text(s, out):
    for ch in s:
        if is_ja(ord(ch)):
            out.add(ord(ch))


def cps_from_source(path, out):
    """Both halves of a .c file: the raw UTF-8 in comments, and the octal
    escapes the strings are written as -- story.c spells 탑승 "\\355\\203\\221"
    and would spell 搭乗 the same way."""
    raw = open(path, encoding='utf-8', errors='replace').read()
    cps_from_text(raw, out)
    for m in OCTAL.finditer(raw):
        b = bytes(int(t, 8) for t in m.group(0).split('\\')[1:])
        try:
            cps_from_text(b.decode('utf-8'), out)
        except UnicodeDecodeError:
            pass


def walk_json(node, out):
    if isinstance(node, dict):
        for v in node.values():
            walk_json(v, out)
    elif isinstance(node, list):
        for v in node:
            walk_json(v, out)
    elif isinstance(node, str):
        cps_from_text(node, out)


def collect():
    out = set()
    for name in ('dialogue.json', 'scenes.json', 'spawns.json'):
        p = os.path.join(ROOT, 'story', name)
        if os.path.exists(p):
            walk_json(json.load(open(p, encoding='utf-8')), out)
    # storypack.py carries the quest banners as Python literals
    cps_from_source(os.path.join(HERE, 'storypack.py'), out)
    src = os.path.join(ROOT, 'native', 'src')
    for f in sorted(os.listdir(src)):
        if f.endswith(('.c', '.h')) and f != 'jafont_data.h':
            cps_from_source(os.path.join(src, f), out)
    return sorted(out)


# ---- GDI ------------------------------------------------------------------
gdi = ctypes.WinDLL('gdi32', use_last_error=True)

DIB_RGB_COLORS = 0
OPAQUE = 2
SHIFTJIS_CHARSET = 128
NONANTIALIASED_QUALITY = 3
FW_NORMAL = 400


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wintypes.DWORD), ('biWidth', wintypes.LONG),
                ('biHeight', wintypes.LONG), ('biPlanes', wintypes.WORD),
                ('biBitCount', wintypes.WORD), ('biCompression', wintypes.DWORD),
                ('biSizeImage', wintypes.DWORD),
                ('biXPelsPerMeter', wintypes.LONG),
                ('biYPelsPerMeter', wintypes.LONG),
                ('biClrUsed', wintypes.DWORD), ('biClrImportant', wintypes.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [('bmiHeader', BITMAPINFOHEADER), ('bmiColors', wintypes.DWORD * 3)]


gdi.CreateFontW.restype = wintypes.HANDLE
gdi.CreateFontW.argtypes = [ctypes.c_int] * 5 + [wintypes.DWORD] * 8 + [wintypes.LPCWSTR]
gdi.CreateDIBSection.restype = wintypes.HANDLE
gdi.CreateDIBSection.argtypes = [wintypes.HDC, ctypes.POINTER(BITMAPINFO),
                                 wintypes.UINT, ctypes.POINTER(ctypes.c_void_p),
                                 wintypes.HANDLE, wintypes.DWORD]
gdi.CreateCompatibleDC.restype = wintypes.HDC
gdi.SelectObject.restype = wintypes.HANDLE
gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
gdi.TextOutW.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int,
                         wintypes.LPCWSTR, ctypes.c_int]
gdi.SetTextColor.argtypes = [wintypes.HDC, wintypes.DWORD]
gdi.SetBkColor.argtypes = [wintypes.HDC, wintypes.DWORD]
gdi.SetBkMode.argtypes = [wintypes.HDC, ctypes.c_int]
gdi.GetTextFaceW.argtypes = [wintypes.HDC, ctypes.c_int, wintypes.LPWSTR]


class Raster:
    """A DC with a padded top-down 32bpp DIB, and one font selected into it."""

    def __init__(self, face, height):
        self.w = self.h = CELL + PAD * 2
        self.hdc = gdi.CreateCompatibleDC(None)
        bi = BITMAPINFO()
        bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bi.bmiHeader.biWidth = self.w
        bi.bmiHeader.biHeight = -self.h          # negative: rows top-down
        bi.bmiHeader.biPlanes = 1
        bi.bmiHeader.biBitCount = 32
        self.bits = ctypes.c_void_p()
        self.bmp = gdi.CreateDIBSection(self.hdc, ctypes.byref(bi), DIB_RGB_COLORS,
                                        ctypes.byref(self.bits), None, 0)
        gdi.SelectObject(self.hdc, self.bmp)
        self.font = gdi.CreateFontW(-height, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                    SHIFTJIS_CHARSET, 0, 0,
                                    NONANTIALIASED_QUALITY, 0, face)
        gdi.SelectObject(self.hdc, self.font)
        gdi.SetTextColor(self.hdc, 0x00FFFFFF)
        gdi.SetBkColor(self.hdc, 0x00000000)
        gdi.SetBkMode(self.hdc, OPAQUE)
        buf = ctypes.create_unicode_buffer(64)
        gdi.GetTextFaceW(self.hdc, 64, buf)
        self.actual = buf.value

    def draw(self, cp):
        """Returns a w*h bytearray of 0/1 coverage, origin top-left."""
        ch = chr(cp)
        gdi.TextOutW(self.hdc, PAD, PAD, '  ', 2)      # clear the box
        gdi.TextOutW(self.hdc, PAD, PAD, ch, 1)
        raw = ctypes.string_at(self.bits, self.w * self.h * 4)
        return bytearray(1 if raw[i * 4] >= 128 else 0
                         for i in range(self.w * self.h))


def ink_box(px, w, h):
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = px[y * w:(y + 1) * w]
        if 1 not in row:
            continue
        if y < y0:
            y0 = y
        y1 = y
        a = row.index(1)
        b = w - 1 - row[::-1].index(1)
        if a < x0:
            x0 = a
        if b > x1:
            x1 = b
    return (x0, y0, x1, y1) if x1 >= 0 else None


def render_all(face, cps):
    """Rasterise at the largest height whose widest glyph still fits CELL.
    Measured rather than assumed: a full-width CJK glyph is about the em and
    the em is not the character height, so -CELL overflows in some faces and
    leaves two dots of air in others."""
    height = CELL
    for _ in range(8):
        R = Raster(face, height)
        glyphs, box = {}, None
        for cp in cps:
            px = R.draw(cp)
            glyphs[cp] = px
            b = ink_box(px, R.w, R.h)
            if b:
                box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]),
                                             max(box[2], b[2]), max(box[3], b[3]))
        if box is None:
            raise SystemExit('jafont: the font drew nothing -- wrong face?')
        bw, bh = box[2] - box[0] + 1, box[3] - box[1] + 1
        if bw <= CELL and bh <= CELL:
            return R, glyphs, box, height
        height -= 1
    raise SystemExit('jafont: could not fit the glyphs into %d dots' % CELL)


def write(o):
    open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(o) + '\n')


def empty():
    """A tree with no Japanese in it still has to compile.  kfont.c reads
    JF_N and draws nothing rather than #ifdef-ing the language away."""
    write(['/* GENERATED by tools/jafont.py -- do not edit.',
           '   No Japanese text in the tree, so no glyphs. */',
           '#ifndef JAFONT_DATA_H', '#define JAFONT_DATA_H',
           '#define JF_CELL %d' % CELL, '#define JF_COLS %d' % COLS,
           '#define JF_ROWS 0', '#define JF_N    0',
           'static const unsigned short JF_CP[1] = {0};',
           'static const unsigned char JF_BITS[1] = {0};', '#endif'])
    print('jafont: no Japanese found in the tree -- wrote an empty header.')


def main():
    cps = collect()
    if not cps:
        empty()
        return
    face = None
    for f in FACES:
        R = Raster(f, CELL)
        if R.actual.lower().startswith(f.split()[0].lower()) or f == FACES[-1]:
            face = f
            break
    R, glyphs, box, height = render_all(face, cps)
    print('jafont: %s at %d dots, %d glyphs, ink %dx%d'
          % (R.actual, height, len(cps), box[2] - box[0] + 1, box[3] - box[1] + 1))

    # centre the global ink box in the cell, so every glyph shares one origin
    ox = box[0] - (CELL - (box[2] - box[0] + 1)) // 2
    oy = box[1] - (CELL - (box[3] - box[1] + 1)) // 2

    stride = (CELL + 7) // 8
    bits = bytearray()
    for cp in cps:
        px = glyphs[cp]
        for y in range(CELL):
            acc = bytearray(stride)
            sy = oy + y
            if 0 <= sy < R.h:
                for x in range(CELL):
                    sx = ox + x
                    if 0 <= sx < R.w and px[sy * R.w + sx]:
                        acc[x >> 3] |= 0x80 >> (x & 7)
            bits += acc

    rows = (len(cps) + COLS - 1) // COLS
    o = []
    o.append('/* GENERATED by tools/jafont.py -- do not edit.')
    o.append('   %s at %d dots.  %d glyphs, %d bytes of bitmap.'
             % (R.actual, height, len(cps), len(bits)))
    o.append('   Re-run after ANY change to Japanese text: this file holds the')
    o.append('   glyphs the story uses and no others. */')
    o.append('#ifndef JAFONT_DATA_H')
    o.append('#define JAFONT_DATA_H')
    o.append('#define JF_CELL %d' % CELL)
    o.append('#define JF_COLS %d' % COLS)
    o.append('#define JF_ROWS %d' % rows)
    o.append('#define JF_N    %d' % len(cps))
    o.append('static const unsigned short JF_CP[JF_N] = {')
    for i in range(0, len(cps), 12):
        o.append('  ' + ','.join('0x%04X' % c for c in cps[i:i + 12]) + ',')
    o.append('};')
    o.append('static const unsigned char JF_BITS[JF_N*%d*JF_CELL] = {' % stride)
    for i in range(0, len(bits), 24):
        o.append('  ' + ','.join('%d' % b for b in bits[i:i + 24]) + ',')
    o.append('};')
    o.append('#endif')
    write(o)
    print('jafont: wrote %s (%.1f KB source, %d bytes in the exe)'
          % (OUT, os.path.getsize(OUT) / 1024.0, len(bits) + 2 * len(cps)))


if __name__ == '__main__':
    main()
