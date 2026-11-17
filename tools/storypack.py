"""storypack.py -- the story, compiled for the native build.

ENGLISH IS A SECOND TABLE, NOT A SECOND STORY.  The contest is Korean and
the writing is Korean, so story/dialogue.json's `text` is the original and
its `en` is a translation beside it; SL_CPS times the subtitle off the
Korean cell count either way, which is why the English lines are written
to about the same length rather than to their own rhythm.  A line with no
`en` falls back to the Korean rather than to a blank -- a missing
translation must read as untranslated, never as a missing line.


THE PLAN IS DATA, AND SO IS THE STORY.  story/ holds the design as JSON and
Markdown: 51 lines of dialogue, 14 encounters with their waves, 24 items, 35
doors, 31 trigger volumes.  None of that is logic -- it is a table that a
director reads -- so it crosses into the exe as a table, the same way the
deck plan does, and for the same reason: a second copy typed into C is a
second thing to keep in step with the first.

What is NOT data, and stays as code in native/src/story.c: the order the
scenes fire in, what a checkpoint restores, how a wave decides it is over.

  python tools/storypack.py     ->  native/src/story_data.h

The one thing this file has to get right is the coordinate system.  The
story is written in the deck plan's SVG units, and the transform it was
first written against (0.15 m/unit, origin y=30) is NOT the one the ship was built with:
deckplan.py reads the same drawing at 0.145 m/unit from y=130, and the ship
in the exe is the one deckplan built.  So every coordinate here goes through
deckplan.wx/wz and nothing else.  Getting this wrong puts every trigger
volume in the ship about nine metres aft of the room it names.
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import deckplan as DP

STORY = os.path.join(ROOT, 'story')
OUT   = os.path.join(ROOT, 'native', 'src', 'story_data.h')

# Trigger volumes are grown by this many plan units on every side.  The ship
# widens its own passages by PASSAGE_WIDEN when it builds them, and a volume
# that stops at the drawn wall would let a player walk the edge of a corridor
# without ever entering it.
GROW = 3.0


# ---- the volumes, as (x1,y1)-(x2,y2) in plan units ----
ROOMS = [
    ('R-QTR', 160, 140, 260, 215),      # 승무원 숙소   start
    ('R-MNT', 430, 130, 530, 240),      # 정비 창고     pack 2
    ('R-MSS', 420, 300, 530, 370),      # 식당          first big one
    ('R-LBY', 270, 400, 410, 530),      # 중앙 로비     hub
    ('R-GAL', 145, 420, 203, 640),      # 탈출 갤러리   the goal
    ('R-MED', 455, 430, 535, 495),      # 의무실        no combat
    ('R-SUP', 225, 640, 315, 705),      # 보급 창고     shotgun
    ('R-CGO', 420, 690, 530, 830),      # 화물칸        pack 3
    ('R-ENG', 185, 840, 495, 930),      # 기관실        pack 1
]
CORRS = [
    ('C-01', 199, 215, 221, 305), ('C-02', 199, 280, 330, 302),
    ('C-03', 308, 252, 330, 400), ('C-04', 330, 252, 464, 274),
    ('C-05', 370, 335, 392, 400), ('C-06', 370, 335, 420, 357),
    ('C-07', 464, 240, 486, 300), ('C-08', 487, 370, 509, 430),
    ('C-09', 487, 495, 509, 640), ('C-10', 460, 640, 509, 662),
    ('C-11', 460, 640, 482, 690), ('C-12', 203, 455, 270, 477),
    ('C-13', 290, 530, 312, 640), ('C-14', 255, 705, 277, 770),
    ('C-15', 200, 748, 277, 770), ('C-16', 200, 748, 222, 840),
    ('C-17', 260, 165, 430, 187), ('C-18', 320, 660, 410, 682),
    ('C-19', 340, 750, 420, 772),
]
JUNCS = [
    ('J-1', 296, 268, 342, 314), ('J-2', 246, 740, 298, 786),
    ('J-3', 452, 632, 516, 670),
]

# ---- the doors.  The position is the drawn door's centre; the
#      exe resolves it to the nearest of the 34 real leaves at boot, so a
#      door never has to be numbered twice.  lock: 0 open, 1 needs Lumi,
#      2 needs a flag, 3 sealed (SD-01, opened by being broken). ----------
DOORS = [
    ('D-01', 197, 211, 1, ''),            ('D-02', 195, 243, 0, ''),
    ('D-03', 259, 276, 0, ''),            ('D-04', 304, 347, 0, ''),
    ('D-05', 306, 396, 0, ''),            ('D-06', 368, 396, 2, 'pack1_installed'),
    ('D-07', 416, 335, 2, 'pack1_installed'), ('D-08', 462, 296, 1, ''),
    ('D-09', 460, 265, 0, ''),            ('D-10', 462, 236, 1, ''),
    ('D-11', 427, 248, 0, ''),            ('D-12', 485, 366, 2, 'pack2_installed'),
    ('D-13', 485, 426, 0, ''),            ('D-14', 485, 491, 0, ''),
    ('D-15', 483, 542, 0, ''),            ('D-16', 483, 607, 0, ''),
    ('D-17', 456, 672, 0, ''),            ('D-18', 458, 686, 1, ''),
    ('D-19', 266, 455, 0, ''),            ('D-20', 199, 455, 1, ''),
    ('D-21', 233, 451, 0, ''),            ('D-22', 288, 526, 2, 'gallery_briefed'),
    ('D-23', 286, 577, 0, ''),            ('D-24', 288, 636, 0, ''),
    ('D-25', 253, 701, 0, ''),            ('D-26', 251, 732, 0, ''),
    ('D-27', 196, 802, 0, ''),            ('D-28', 198, 836, 1, ''),
    ('D-29', 256, 166, 0, ''),            ('D-30', 297, 161, 0, ''),
    ('D-31', 426, 166, 0, ''),            ('D-32', 416, 750, 0, ''),
    ('D-33', 316, 662, 0, ''),            ('D-34', 342, 656, 0, ''),
    ('SD-01', 380, 250, 3, 'shortcut_open'),
]

# ---- the items.  kind: 0 health, 1 weapon, 2 ammo, 3 energy pack,
#      4 respawn box.  amt is health points, weapon slot, or rounds.
#      The last column is the deck it stands on, in metres, and EVERY ITEM
#      IS NOW ON THE FLOOR.  Pack 3 used to be written at 3.15 -- up on the
#      cargo hold's platform, which is what the duct sequence was for -- and
#      the platform turned out to have no way onto it: the three containers
#      meant to be its staircase have 1.05 m risers against a 0.46 m step-up
#      and a 0.99 m jump apex, so nothing could climb them.  A pack you have
#      to find is a pack that lies where the others lie.  settleItem still
#      reads this column and still tests a written deck for a route up, so
#      the day the climb is built the number can come back. ---------------
IT_HP, IT_WEP, IT_AMMO, IT_PACK, IT_BOX = range(5)
ITEMS = [
    # id       kind     x    y   amt  spawned_by (empty = present at load)
    ('HK-00',  IT_HP,  236, 202,  70, '', 0.0),
    ('HK-01',  IT_HP,  326, 455,  25, '', 0.0),
    ('HK-02',  IT_HP,  246, 690,  25, '', 0.0),
    ('HK-03',  IT_HP,  382, 674,  25, '', 0.0),
    ('HK-04',  IT_HP,  452, 320,  60, '', 0.0),
    ('HK-05',  IT_HP,  452, 232,  25, '', 0.0),
    ('HK-06',  IT_HP,  466, 442,  60, '', 0.0),
    ('HK-07',  IT_HP,  524, 486,  25, '', 0.0),
    ('HK-08',  IT_HP,  352, 747,  60, '', 0.0),
    ('HK-09',  IT_HP,  300, 880,  60, 'clear_E-06', 0.0),
    ('HK-R1',  IT_BOX, 150, 472,  40, 'flag_final_defense', 0.0),
    ('HK-R2',  IT_BOX, 150, 612,  40, 'flag_final_defense', 0.0),
    ('W1',     IT_WEP, 318, 290,   1, '', 0.0),
    ('W2',     IT_WEP, 268, 660,   2, '', 0.0),
    # THE ROTARY WAS IN A CORNER.  (432,702) is the cargo hold's own
    # north-west corner and the hold is 110 x 140 units; a weapon the
    # player is sent the length of the ship for is not a thing to hide
    # behind the first container.  Off the aisle, in the middle third.
    ('W3',     IT_WEP, 462, 748,   3, '', 0.0),
    ('AC-1',   IT_AMMO,312, 170,   1, '', 0.0),      # amt doubles as weapon slot
    ('AC-2',   IT_AMMO,382, 666,   2, '', 0.0),
    ('AC-3',   IT_AMMO,352, 755,   3, '', 0.0),
    ('E06-BOX',IT_AMMO,300, 884,   0, 'clear_E-06', 0.0),
    ('E10-BOX-A',IT_AMMO,470,760,  0, 'clear_E-10', 0.0),
    ('E10-BOX-B',IT_AMMO,500,790,  0, 'clear_E-10', 0.0),
    ('PACK-1', IT_PACK,325, 908,   1, '', 0.0),
    ('PACK-2', IT_PACK,503, 225,   2, '', 0.0),
    ('PACK-3', IT_PACK,497, 728,   3, '', 0.0),
]

# ---- the quest banner.  event -> text, the plan point the compass needle
#      points at, and the event STANDING ON THAT POINT fires.
#
#      THE FOURTH COLUMN IS THE BACKSTOP.  Every other way this story
#      advances is a room rectangle drawn on the deck plan, and
#      the plan is not the ship: build_ship.py widens passages and snaps
#      what it builds to the half-module grid, so a rectangle can miss the
#      floor that actually got built and the run dies there with the banner
#      still asking for something the player has already done.  A lit ring
#      standing on the objective, entered for half a second, fires the same
#      event.  It is only ever filled in for a MOVEMENT objective -- a pack
#      you have not picked up is still a pack you have not picked up.
#
#      The console strip is x=160, not 181: the housing spans plan
#      (168,555)-(194,625), so 181 is INSIDE it and a marker there stands
#      on a bulkhead.  See LANDMARKS below, which had this right already.
#      A PICKUP IS A STEP OF THE STORY LIKE ANY OTHER.  The kit and the
#      pistol used to be things lying about that the drone happened to
#      mention; they are the two objects the first ten minutes cannot be
#      played without, so each gets a banner, a compass bearing and a ring
#      on the deck -- and Lumi goes and hangs in the ring (leadStep's
#      HOVER_R), which is the part that makes it an instruction.  The
#      pistol's step is what E-01/E-02's flag_has_pistol gate assumes, so
#      the objective and the encounter design now agree.
#
#      AND SO ARE THE OTHER TWO GUNS.  The shotgun and the rotary were
#      props the route went straight past, which let a player reach the
#      cargo hold's wave with a pistol and no shells -- not a difficulty
#      curve but a run that cannot be finished.  Each now gates the
#      movement objective after it, which is what storyScene has always
#      assumed (scene 4 hands out slot 2, scene 7 slot 3).
QUESTS = [
    ('intro_standup_done','응급 키트를 회수한다',             236, 202, '',
     'RECOVER THE MEDKIT',
     '応急キットを回収する'),
    ('pickup_HK-00',      '권총을 회수한다',                  318, 290, '',
     'RECOVER THE PISTOL',
     '拳銃を回収する'),
    ('pickup_W1',         '루미를 따라간다 (나침반 방향)',    175, 470,
     'enter_R-LBY first', 'FOLLOW LUMI (SEE THE COMPASS)',
     'ルミについて行く (羅針盤の方向)'),
    # THE BRIEFING IS A BEAT, AND A BEAT HAPPENS WHERE YOU STAND.  Its
    # arrive event used to be `enter_R-GAL first`, which is also the
    # VOLUME's own event -- so L-016..L-020 fired the moment the player
    # crossed the threshold, forty metres of empty hall away from the
    # capsule they are about it, and the marker they were walking to had
    # nothing left to give when they got there.  `reach_capsule` is fired
    # by the goal ring and by nothing else, and story.c aims that ring at
    # the front of the hatch once the fixture has been mounted.
    ('enter_R-LBY first', '탈출 갤러리로 이동',              181, 530,
     'reach_capsule', 'REACH THE ESCAPE GALLERY',
     '脱出ギャラリーへ移動'),
    ('gallery_briefed',   '보급 창고에서 산탄총을 회수',      268, 660, '',
     'RECOVER THE SHOTGUN FROM THE STORE',
     '補給倉庫で散弾銃を回収'),
    ('pickup_W2',         '에너지팩 회수 (0/3)',             325, 908, '',
     'RECOVER THE ENERGY CORES (0/3)',
     'エネルギーパック回収 (0/3)'),
    ('pickup_PACK-1',     '갤러리 콘솔로 귀환',              160, 590, '',
     'RETURN TO THE GALLERY CONSOLE',
     'ギャラリーのコンソールへ帰還'),
    ('install_slot4',     '정비 창고에서 에너지팩 회수',      503, 225, '',
     'RECOVER A CORE FROM THE MAINTENANCE STORE',
     '整備倉庫でエネルギーパックを回収'),
    ('pickup_PACK-2',     '갤러리 콘솔로 귀환',              160, 590, '',
     'RETURN TO THE GALLERY CONSOLE',
     'ギャラリーのコンソールへ帰還'),
    ('install_slot5',     '화물칸에서 회전식 기관포를 회수',  462, 748, '',
     'RECOVER THE ROTARY CANNON IN THE CARGO HOLD',
     '貨物室で回転式機関砲を回収'),
    ('pickup_W3',         '화물칸에서 마지막 에너지팩 회수',  497, 728, '',
     'RECOVER THE LAST CORE IN THE CARGO HOLD',
     '貨物室で最後のエネルギーパックを回収'),
    ('pickup_PACK-3',     '갤러리 콘솔로 귀환',              160, 590, '',
     'RETURN TO THE GALLERY CONSOLE',
     'ギャラリーのコンソールへ帰還'),
    ('flag_final_defense','캡슐 탑승까지 버틴다',            123, 615, '',
     'HOLD OUT UNTIL YOU BOARD',
     'カプセル搭乗まで持ちこたえる'),
]


# ---- the named points the director steers by.  A landmark is a place the
#      code has to know about by name -- where the player wakes up, where
#      Lumi docks, where the duct drops you -- so it lives here with every
#      other coordinate rather than as a literal in C. ---------------------
LANDMARKS = [
    ('start',     206, 186),    # you, on the floor of the crew quarters
    ('lumi_a',    252, 152),    # her approach, three stops
    ('lumi_b',    222, 160),
    ('lumi_c',    206, 172),
    # The gallery is 58 units wide and the console housing (168,555)-(194,625)
    # stands in the middle of it, so the walkable floor is two strips either
    # side.  Every point here has to be ON one of them: a landmark inside the
    # housing puts the player inside a brush, and then nothing can path to him.
    ('console',   160, 590),    # the strip west of the six-slot console
    ('dock',      199, 556),    # the strip east, where she docks her own core
    # THE CAPSULE IS WHERE THE CONSOLE IS.  Bay six's own mouth is at
    # (152,615), which is 3.8 m from the console -- inside CAP_R, so
    # boarding always worked, and far enough that the marker moved to a
    # place the player read as a different room appearing behind them.
    # Two metres down the
    # same strip is one place with two things in it.
    ('capsule',   156, 602),
    ('duct',      455, 840),    # cargo floor -> engine roof
    ('duct_land', 450, 866),
    ('terminal',  340, 470),    # the lobby information terminal
]

# ---- the eight story displays, plus the three the speaker uses ----
DISP = ['EYES_IDLE', 'HEARTBEAT', 'VITALS', 'EYES_TALK', 'SCAN', 'OPEN',
        'COUNT', 'WARN', 'SPEAKER', 'RADIO', 'OFF']
METHOD = ['VENT', 'CRACK', 'CRATE', 'COCOON', 'CEILING', 'WALLBREAK', 'CHASE']


def c_str(s):
    """UTF-8, octal-escaped.  The dialogue is Korean and the header has to
    survive any compiler's idea of a source encoding."""
    return '"' + ''.join(
        chr(x) if 32 <= x < 127 and chr(x) not in '"\\?' else '\\%03o' % x
        for x in s.encode('utf-8')) + '"'


def num(v):
    return '%d' % int(v) if float(v) == int(v) and abs(v) < 1e9 else repr(float(v))


def strtab(o, name, vals):
    o.append('static const char *const %s[] = {' % name)
    for v in vals:
        o.append('  %s,' % c_str(v))
    o.append('};')


def bytetab(o, name, vals):
    o.append('static const unsigned char %s[] = {' % name)
    for i in range(0, len(vals), 24):
        o.append('  ' + ','.join(str(int(v)) for v in vals[i:i + 24]) + ',')
    o.append('};')


def shorttab(o, name, vals):
    o.append('static const short %s[] = {' % name)
    for i in range(0, len(vals), 20):
        o.append('  ' + ','.join(str(int(v)) for v in vals[i:i + 20]) + ',')
    o.append('};')


def floattab(o, name, rows):
    o.append('static const float %s[] = {' % name)
    for r in rows:
        o.append('  ' + ','.join(num(v) for v in r) + ',')
    o.append('};')


def main():
    dlg = json.load(open(os.path.join(STORY, 'dialogue.json'), encoding='utf-8'))
    spw = json.load(open(os.path.join(STORY, 'spawns.json'), encoding='utf-8'))

    o = ['/* GENERATED by tools/storypack.py -- do not edit.',
         '   The story as the exe receives it.  Source: story/dialogue.json,',
         '   story/spawns.json and the tables in tools/storypack.py,',
         '   every coordinate through deckplan.wx/wz -- the transform the ship',
         '   was actually built with. */',
         '#ifndef STORY_DATA_H', '#define STORY_DATA_H', '']

    # ---- volumes --------------------------------------------------------
    vols = ([(i, 0) for i in ROOMS] + [(i, 1) for i in CORRS] +
            [(i, 2) for i in JUNCS])
    o.append('/* trigger volumes: x0 z0 x1 z1, world metres, grown %g units */'
             % GROW)
    o.append('#define SV_N %d' % len(vols))
    strtab(o, 'SV_ID', [v[0][0] for v in vols])
    floattab(o, 'SV_RECT', [[DP.wx(v[0][1] - GROW), DP.wz(v[0][2] - GROW),
                             DP.wx(v[0][3] + GROW), DP.wz(v[0][4] + GROW)]
                            for v in vols])
    bytetab(o, 'SV_KIND', [v[1] for v in vols])
    o.append('')

    # ---- doors ----------------------------------------------------------
    o.append('/* doors: the plan point, resolved to the nearest real leaf at boot */')
    o.append('#define SD_N %d' % len(DOORS))
    strtab(o, 'SD_ID', [d[0] for d in DOORS])
    floattab(o, 'SD_POS', [[DP.wx(d[1]), DP.wz(d[2])] for d in DOORS])
    bytetab(o, 'SD_LOCK', [d[3] for d in DOORS])
    strtab(o, 'SD_COND', [d[4] for d in DOORS])
    o.append('')

    # ---- items ----------------------------------------------------------
    o.append('/* items: kind 0 health 1 weapon 2 ammo 3 energy pack 4 respawn box */')
    o.append('#define SI_N %d' % len(ITEMS))
    strtab(o, 'SI_ID', [i[0] for i in ITEMS])
    bytetab(o, 'SI_KIND', [i[1] for i in ITEMS])
    floattab(o, 'SI_POS', [[DP.wx(i[2]), DP.wz(i[3]), i[6]] for i in ITEMS])
    shorttab(o, 'SI_AMT', [i[4] for i in ITEMS])
    strtab(o, 'SI_BY', [i[5] for i in ITEMS])
    o.append('')

    # ---- quests ---------------------------------------------------------
    o.append('#define SQ_N %d' % len(QUESTS))
    strtab(o, 'SQ_ON', [q[0] for q in QUESTS])
    strtab(o, 'SQ_TEXT', [q[1] for q in QUESTS])
    floattab(o, 'SQ_TARGET', [[DP.wx(q[2]), DP.wz(q[3])] for q in QUESTS])
    strtab(o, 'SQ_ARRIVE', [q[4] for q in QUESTS])
    strtab(o, 'SQ_TEXT_JA', [q[6] for q in QUESTS])
    strtab(o, 'SQ_TEXT_EN', [q[5] for q in QUESTS])
    o.append('')

    o.append('/* named points the director steers by */')
    o.append('#define SP_N %d' % len(LANDMARKS))
    strtab(o, 'SP_ID', [l[0] for l in LANDMARKS])
    floattab(o, 'SP_POS', [[DP.wx(l[1]), DP.wz(l[2])] for l in LANDMARKS])
    o.append('')

    # ---- dialogue -------------------------------------------------------
    # "A>B" in the display column means: start on A, switch to B partway.
    # Splitting it here means the player never has to parse a string.
    lines = dlg['lines']
    exc = set(dlg['rules'].get('combat_exceptions', []))
    d0, d1 = [], []
    for L in lines:
        p = (L.get('display') or 'EYES_TALK').split('>')
        d0.append(DISP.index(p[0]))
        d1.append(DISP.index(p[-1]))
    o.append('/* dialogue.  trigger "" means chain: play the moment the line')
    o.append('   above it ends.  disp0/disp1 are the two halves of "A>B". */')
    o.append('#define SL_N %d' % len(lines))
    o.append('#define SL_CPS %g' % dlg['rules']['chars_per_sec'])
    o.append('#define SL_TAIL %sf' % repr(float(dlg['rules']['tail_pad_s'])))
    o.append('#define SL_WRAP %d' % dlg['rules']['max_chars_per_line'])
    o.append('#define SL_IDLE %sf' % repr(float(dlg['rules']['idle_bark_after_silence_s'])))
    strtab(o, 'SL_ID', [L['id'] for L in lines])
    strtab(o, 'SL_TRIG', [('' if L['trigger'] == 'chain'
                           else L['trigger'].split(':', 1)[1]) for L in lines])
    strtab(o, 'SL_TEXT', [L['text'] for L in lines])
    strtab(o, 'SL_TEXT_EN', [L.get('en') or L['text'] for L in lines])
    strtab(o, 'SL_TEXT_JA', [L.get('ja') or L['text'] for L in lines])
    bytetab(o, 'SL_D0', d0)
    bytetab(o, 'SL_D1', d1)
    bytetab(o, 'SL_EXC', [1 if L['id'] in exc else 0 for L in lines])
    o.append('')
    barks = dlg['idle_barks']
    o.append('#define SB_N %d' % len(barks))
    strtab(o, 'SB_TEXT', [b['text'] for b in barks])
    strtab(o, 'SB_TEXT_EN', [b.get('en') or b['text'] for b in barks])
    strtab(o, 'SB_TEXT_JA', [b.get('ja') or b['text'] for b in barks])
    o.append('')

    # ---- encounters -----------------------------------------------------
    # Flattened three deep: encounter -> wave -> unit.  A ragged array of
    # structs in C is three arrays and two offset tables, and the offsets
    # are what make it one contiguous block of rodata.
    enc = spw['encounters']
    e_trig, e_cond, e_w0, e_wn, e_flags, e_clear = [], [], [], [], [], []
    w_t, w_orle, w_u0, w_un = [], [], [], []
    u_type, u_n, u_meth, u_pos, u_lead = [], [], [], [], []
    for E in enc:
        # "event:X && flag_y" -- the event fires it, the flag gates it
        t = E['trigger']
        # The design has no stretch where the player meets an enemy unarmed --
        # design out any stretch where the player meets an enemy unarmed.  E-01 is gated
        # on the pistol in spawns.json and E-02 is not, so a player who walks
        # past the junction locker arrives in the lobby with empty hands and
        # three scuttlers.  The gate belongs on both.
        if E['id'] == 'E-02' and 'flag_has_pistol' not in t:
            t += ' && flag_has_pistol'
        ev, cond = '', ''
        for part in [p.strip() for p in t.split('&&')]:
            if part.startswith('event:'):
                ev = part[6:].strip()
            elif part.startswith('flag_'):
                cond = part[5:].strip()
            else:
                ev = part
        e_trig.append(ev); e_cond.append(cond)
        e_flags.append((1 if E.get('blackout') else 0) |
                       (2 if E.get('foam') else 0) |
                       (4 if E.get('chase_stream') else 0) |
                       (8 if E.get('schedule') else 0))
        e_clear.append(';'.join(E.get('on_clear', [])))
        waves = E.get('waves') or []
        # E-11's chase stream and E-12's schedule are waves in a different
        # spelling; flattening them here keeps the director's loop one loop.
        if E.get('chase_stream'):
            cs = E['chase_stream']
            for k in range(cs['total']):
                waves.append({'t': k * cs['interval_s'],
                              'units': [{'type': cs['type'], 'n': 1,
                                         'method': 'CHASE', 'pos': [0, 0]}]})
        for s in E.get('schedule', []):
            ent = E.get('entries', {})
            u = []
            for un in s['units']:
                p = ent.get(un.get('entry', 'A'), [203, 466])
                u.append({'type': un['type'], 'n': un['n'],
                          'method': 'CEILING' if un.get('static') else 'VENT',
                          'pos': p})
            waves.append({'t': s['t'], 'units': u})
        e_w0.append(len(w_t)); e_wn.append(len(waves))
        for W in waves:
            w_t.append(W.get('t', 0))
            w_orle.append(W.get('or_when_remaining_le', -1))
            w_u0.append(len(u_type)); w_un.append(len(W['units']))
            for U in W['units']:
                u_type.append(int(U['type'][1]))          # "M1" -> 1
                u_n.append(U['n'])
                u_meth.append(METHOD.index(U['method']))
                u_pos.append([DP.wx(U['pos'][0]), DP.wz(U['pos'][1])])
                u_lead.append(U.get('lead_s', 0))
    o.append('/* encounters.  flags: 1 blackout, 2 foam, 4 chase stream, 8 timed */')
    o.append('#define SE_N %d' % len(enc))
    o.append('#define SW_N %d' % len(w_t))
    o.append('#define SU_CNT %d' % len(u_type))
    strtab(o, 'SE_ID', [E['id'] for E in enc])
    strtab(o, 'SE_TRIG', e_trig)
    strtab(o, 'SE_COND', e_cond)
    strtab(o, 'SE_CLEAR', e_clear)
    shorttab(o, 'SE_W0', e_w0); bytetab(o, 'SE_WN', e_wn)
    bytetab(o, 'SE_FLAGS', e_flags)
    floattab(o, 'SW_T', [[t] for t in w_t])
    shorttab(o, 'SW_ORLE', w_orle)
    shorttab(o, 'SW_U0', w_u0); bytetab(o, 'SW_UN', w_un)
    bytetab(o, 'SU_TYPE', u_type); bytetab(o, 'SU_MANY', u_n)
    bytetab(o, 'SU_METHOD', u_meth)
    floattab(o, 'SU_POS', u_pos)
    floattab(o, 'SU_LEAD', [[v] for v in u_lead])
    o.append('')
    o.append('#endif')

    open(OUT, 'w', encoding='ascii', newline='\n').write('\n'.join(o) + '\n')
    total = sum(u_n[i] for i in range(len(u_n)))
    print('story_data.h  %d volumes, %d doors, %d items, %d lines, '
          '%d encounters / %d waves / %d units (%d bodies), %d bytes'
          % (len(vols), len(DOORS), len(ITEMS), len(lines), len(enc),
             len(w_t), len(u_type), total, os.path.getsize(OUT)))


if __name__ == '__main__':
    main()
