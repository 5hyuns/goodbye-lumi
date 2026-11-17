"""Regenerate docs/symbols.json by rescanning the tree.

CLAUDE.md is the manual -- the build chain, which tool owns which change.
The index is the level below it: which LINE a thing is on, so a 16k-line
built page or a 2.3k-line kit registry can be read by range instead of whole.

The hand-written map is docs/FILEMAP.md (what each file is for); this emits
the machine copy (which line each section and export is on), so a reader
with a line number does not have to trust a stale one.

AND THE DELIVERABLE IS INDEXED TOO.  For a long time this scanned only the
previz -- the four built pages, the kit, the props and the Python chain --
because that was where the whole ship lived.  It is not any more: native/src
is 22.8 k lines of hand-authored C and it is the thing that gets submitted,
so a reader sent here by CLAUDE.md was being handed a map of the previz and
left to grep the exe.  The C carries its own section banners already
(`/* ====` then an indented title), which is what makes it indexable at all.

Run:  python tools/mkindex.py
"""
import io, re, os, json

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def rd(p):
    return io.open(os.path.join(ROOT, p), encoding='utf-8', errors='replace').read()

def at(t, i):
    return t[:i].count('\n') + 1

PAGES = ['src/derelict/map.html', 'src/derelict/derelict.html',
         'src/derelict/kit.html', 'src/sector-c/sector-c-containment.html']

out = {'generated_by': 'tools/mkindex.py',
       'note': 'CLAUDE.md is the manual; this is the symbol map. '
               'Regenerate after moving code.'}

# ---- the 14 numbered sections, in every built page ------------------------
out['pages'] = {}
for p in PAGES:
    t = rd(p)
    out['pages'][p] = {
        'lines': t.count('\n') + 1,
        'sections': [{'n': int(m.group(1)), 'name': m.group(2).strip(),
                      'line': at(t, m.start())}
                     for m in re.finditer(r'^   (\d+)\.\s+([A-Z][A-Z ]*[A-Z])', t, re.M)]}

# ---- the corridor kit: kitDef() order IS the id mod() takes ---------------
t = rd('tools/kit.js')
kit = [{'id': i, 'name': m.group(1), 'sub': m.group(2) or '', 'line': at(t, m.start())}
       for i, m in enumerate(
           re.finditer(r"kitDef\(\{\s*name:'([^']*)'(?:,\s*sub:'([^']*)')?", t))]
out['kit'] = {'file': 'tools/kit.js', 'lines': t.count('\n') + 1, 'parts': kit}

# ---- the props ------------------------------------------------------------
# THE BENCH IS VERSIONED AND THIS WAS NOT.  propkit.js moved to
# props/props_v01/ when the prop bench got a version number, and this line
# went on naming the old path -- so mkindex.py had been dying on a
# FileNotFoundError, which is a doc tool that is silent right up until
# someone runs it.  Take the highest version directory that has one.
PROPKIT = sorted(d for d in os.listdir(os.path.join(ROOT, 'props'))
                 if os.path.isfile(os.path.join(ROOT, 'props', d, 'propkit.js')))[-1]
PROPKIT = 'props/%s/propkit.js' % PROPKIT
t = rd(PROPKIT)
out['props'] = {
    'file': PROPKIT, 'lines': t.count('\n') + 1,
    'props': [{'name': m.group(1), 'room': m.group(2), 'sub': m.group(3),
               'line': at(t, m.start())}
              for m in re.finditer(r"propDef\('([^']*)',\s*'([^']*)',\s*'([^']*)'", t)]}

# ---- tools: defs, classes and SHOUTING constants --------------------------
PY = r'^(?:def |class )(\w+)|^([A-Z][A-Z0-9_]{2,})\s*='
JS = r'^function (\w+)|^const ([A-Za-z_]\w*)\s*='
out['tools'] = {}
for f in sorted(os.listdir(HERE)):
    p = 'tools/' + f
    pat = PY if f.endswith('.py') else (JS if f.endswith('.js') and not f.startswith('_') else None)
    if not pat:
        continue                      # _*.js are generated; skip them
    t = rd(p)
    out['tools'][p] = {
        'lines': t.count('\n') + 1,
        'symbols': [{'sym': m.group(1) or m.group(2), 'line': at(t, m.start())}
                    for m in re.finditer(pat, t, re.M)]}

# ---- native/src: the deliverable ------------------------------------------
# Two things are worth a line number in a C file this size: the banner that
# opens a section, and a function another translation unit can call.  A
# `static` helper is findable from the section it lives under, and there are
# 1,100 of them; listing those would bury the 300 that are the seams.
#
# GENERATED HEADERS ARE COUNTED, NOT SCANNED.  plan_data.h and story_data.h
# are nativepack.py's and storypack.py's output -- tables, no symbols, and
# 7.5 k lines between them.  The tool that writes one is the place to look.
NAT_GEN = ('plan_data.h', 'story_data.h')
NAT_BANNER = re.compile(r'^/\* =+\s*\n\s{3,}(.+?)\s*$', re.M)
NAT_EXPORT = re.compile(
    r'^(?!static\b|typedef\b|extern\b)'          # not a local, not a decl
    r'(?:const\s+)?[A-Za-z_]\w*(?:\s*\*+)?\s+\**'   # a return type
    r'(\w+)\s*\([^;]*\)\s*\{', re.M)             # name(args) {  -- a body

NATDIR = os.path.join(ROOT, 'native', 'src')
out['native'] = {'dir': 'native/src', 'note': 'the deliverable; C, hand-authored',
                 'files': {}}
for f in sorted(os.listdir(NATDIR)):
    if not f.endswith(('.c', '.h')):
        continue
    t = rd('native/src/' + f)
    e = {'lines': t.count('\n') + 1}
    if f in NAT_GEN:
        e['generated_by'] = ('tools/nativepack.py' if f.startswith('plan')
                             else 'tools/storypack.py')
    else:
        e['sections'] = [{'title': m.group(1), 'line': at(t, m.start())}
                         for m in NAT_BANNER.finditer(t)]
        e['exports'] = [{'sym': m.group(1), 'line': at(t, m.start())}
                        for m in NAT_EXPORT.finditer(t)]
    out['native']['files']['native/src/' + f] = e

dst = os.path.join(ROOT, "docs")
os.makedirs(dst, exist_ok=True)
io.open(os.path.join(dst, "symbols.json"), 'w', encoding='utf-8').write(
    json.dumps(out, ensure_ascii=False, separators=(',', ':')))

nat = out['native']['files']
print('%d pages, %d kit parts, %d props, %d tool files, '
      '%d native files (%d lines, %d sections, %d exports) -> %d bytes'
      % (len(out['pages']), len(kit), len(out['props']['props']), len(out['tools']),
         len(nat), sum(e['lines'] for e in nat.values()),
         sum(len(e.get('sections', ())) for e in nat.values()),
         sum(len(e.get('exports', ())) for e in nat.values()),
         os.path.getsize(os.path.join(dst, 'symbols.json'))))
