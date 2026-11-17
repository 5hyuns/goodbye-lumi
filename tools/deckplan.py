"""Deck plan v6, transcribed from ref/ship_deck_plan_v6_fixed.svg.

All rectangles are in SVG units.  The drawing is internally consistent at
10 cm/unit -- 22u corridors, 2x nodes, the 1/5 ratio of the design -- but that is
a *survey* scale, and a corridor a real ship would build at 2.2 m plays like a
crawlspace in a first-person game.  Doom 3 runs its main corridors near 3.5 m
wide and 4 m tall and they still feel tight.  So the plan is read at 14.5 cm
per unit, and passages are inflated on top of that (PASSAGE_WIDEN) because a
corridor needs proportionally more room than a hall does.

World transform:  X = (svgX - 340)/10      (port negative, starboard positive)
                  Z = (svgY - 130)/10      (bow at Z=0, stern at Z=80)
"""

OX, OZ, S = 340.0, 130.0, 0.145


def wx(x): return round((x - OX) * S, 3)
def wz(y): return round((y - OZ) * S, 3)


# ---- rooms.  key, name, subtitle, x,y,w,h, ceiling(m), floor, wall, ceil ----
ROOMS = [
    ('QUARTERS', 'CREW QUARTERS',     'Bow \u00b7 Port',           160, 140, 100,  75, 3.6, 'TILE',  'PANEL', 'CEIL'),
    # THE GARDEN IS A STORE, AND ONLY THE NAME OF IT CHANGED.  The
    # A later design pass replaced the hydroponic garden with the
    # maintenance store, and the story followed -- storypack.py calls this
    # rectangle R-MNT and Lumi names the store in L-018.  The build chain did
    # not follow, and the zone name is drawn on the player's HUD (game.c
    # drawHud), so the room announced itself as HYDROPONIC GARDEN while she
    # named it as the store.  The fixtures stay: what stands here reads as
    # racking either way, and re-modelling it is not what the mismatch was.
    ('MAINT',    'MAINTENANCE STORE', 'Bow \u00b7 Starboard',      430, 130, 100, 110, 7.2, 'TILE',  'PANEL', 'PANEL'),
    ('MESS',     'MESS HALL',         'Midship \u00b7 Starboard',  420, 300, 110,  70, 4.0, 'TILE',  'PANEL', 'CEIL'),
    ('LOBBY',    'CENTRAL LOBBY',     'Midship \u00b7 Hub',        270, 400, 140, 130, 6.4, 'FLOOR', 'PANEL', 'PANEL'),
    ('GALLERY',  'ESCAPE GALLERY',    'Port \u00b7 Outer hull',    145, 420,  58, 220, 5.0, 'GRATE', 'PANEL', 'PANEL'),
    ('MEDICAL',  'MEDICAL BAY',       'Midship \u00b7 Starboard',  455, 430,  80,  65, 3.8, 'TILE',  'TILE',  'CEIL'),
    ('STORES',   'STORES',            'Midship \u00b7 Lower',      225, 640,  90,  65, 4.0, 'FLOOR', 'PANEL', 'CEIL'),
    ('CARGO',    'CARGO HOLD',        'Stern \u00b7 Starboard',    420, 690, 110, 140, 8.4, 'FLOOR', 'PANEL', 'PANEL'),
    ('ENGINE',   'ENGINE ROOM',       'Stern \u00b7 Full beam',    185, 840, 310,  90, 7.8, 'DARK',  'PANEL', 'PANEL'),
]

# ---- corridors.  the 22 passage rects; the three 2x-width ones are the
#      engagement nodes ------------------------------------------
CORR = [
    (260, 165, 170,  22),   # bow cross-passage  (quarters <-> store, collapsed)
    (199, 215,  22,  90),   # quarters south
    (464, 240,  22,  60),   # store -> mess
    (308, 252,  22, 148),   # tutorial passage, north spine
    (330, 252, 134,  22),   # over-mess cross-passage (opens after pack 2)
    (199, 280, 131,  22),   # quarters passage -> spine
    (370, 335,  22,  65),   # mess lower -> lobby NE
    (370, 335,  50,  22),
    (487, 370,  22,  60),   # mess -> medical
    (203, 455,  67,  22),   # gallery <-> lobby
    (487, 495,  22, 145),   # medical -> south (cargo route)
    (290, 530,  22, 110),   # lobby -> stores
    (320, 660,  90,  22),   # stores east (collapsed dead end, ammo cache)
    (460, 640,  49,  22),   # node 3 -> cargo
    (460, 640,  22,  50),
    (255, 705,  22,  65),   # stores south
    (200, 748,  77,  22),   # node 2 -> engine west
    (200, 748,  22,  92),
    (340, 750,  80,  22),   # cargo west (decompressed dead end, ammo cache)
]
NODES = [
    (296, 268, 46, 46),     # tutorial passage upper corner
    (246, 740, 52, 46),     # engine route lower corner
    (452, 632, 64, 38),     # cargo route lower corner
]

# ---- gallery: five launched bays and the one capsule left, cut into the
#      port hull so the launch tubes really do pass through it -------------
BAYS = [(121, 432, 24, 24), (121, 466, 24, 24), (121, 500, 24, 24),
        (121, 534, 24, 24), (121, 568, 24, 24)]
CAPSULE = (119, 600, 26, 30)

# ---- bulkhead doors (34).  the long side of each rect is the door's width --
DOORS = [
    (297, 161, 6, 30), (256, 166, 8, 20), (426, 166, 8, 20), (197, 211, 26, 8),
    (462, 236, 26, 8), (195, 243, 30, 6), (427, 248, 6, 30), (460, 265, 30, 6),
    (259, 276, 6, 30), (462, 296, 26, 8), (416, 335, 8, 22), (304, 347, 30, 6),
    (485, 366, 26, 8), (306, 396, 26, 8), (368, 396, 26, 8), (485, 426, 26, 8),
    (233, 451, 6, 30), (199, 455, 8, 22), (266, 455, 8, 22), (485, 491, 26, 8),
    (288, 526, 26, 8), (483, 542, 30, 6), (286, 577, 30, 6), (483, 607, 30, 6),
    (288, 636, 26, 8), (342, 656, 6, 30), (316, 662, 8, 18), (456, 672, 30, 6),
    (458, 686, 26, 8), (253, 701, 26, 8), (251, 732, 30, 6), (416, 750, 8, 22),
    (196, 802, 30, 6), (198, 836, 26, 8),
]

# ---- the map lies: three passages that end ----------------------
BLOCKED = [
    (336, 165, 12, 22, 'collapse'),    # bow cross-passage
    (402, 658,  8, 26, 'collapse'),    # stores east
    (338, 748,  8, 26, 'vacuum'),      # cargo west
]
EVENT_DOOR = (380, 250, 8, 26)         # over-mess passage, opens after pack 2

# ---- fixtures read straight off the plan ---------------------------------
BEDS = [(172, 196, 24, 10), (204, 196, 24, 10)]
# The three fixture rows in the maintenance store.  Named for what they
# were; kept for where they are -- E-08 stands two CRATE spawns on the
# first and third of them (story/spawns.json), so the rectangles are
# load-bearing for the fight even though the room has been renamed.
PLANTERS = [(440, 162, 80, 12), (440, 182, 80, 12), (440, 202, 80, 12)]
BENCHES = [(300, 495, 30, 10), (340, 495, 30, 10), (380, 495, 30, 10)]
CONTAINERS = [(478, 752, 30, 18, 1.05), (452, 776, 30, 18, 2.10), (426, 800, 30, 18, 3.15)]
PLATFORM = (470, 712, 54, 32, 3.15)    # cargo upper platform, pack 3 sits here
DUCT = (445, 822, 20, 36)              # cargo -> engine roof
REACTORS = [(385, 876, 80, 20), (385, 904, 80, 20)]
AMMO = [(312, 170, 12, 12), (382, 666, 12, 12), (352, 755, 12, 12)]
# six slots, three already lit
SLOTS = [(171, 560, 10, 10, 1), (183, 560, 10, 10, 1), (171, 576, 10, 10, 1),
         (183, 576, 10, 10, 0), (171, 592, 10, 10, 0), (183, 592, 10, 10, 0)]
CONSOLE = (168, 555, 26, 70)           # the slot console housing

CEIL_CORR = 3.9
CEIL_NODE = 4.6

# Passages are inflated perpendicular to their run, on top of the 14.5 cm
# scale: 22u reads 3.2 m, and 22+2*2 = 26u reads 3.8 m, which is about
# what a corridor you fight down needs.  Doors widen with them.
PASSAGE_WIDEN = 2

# ---- anchors, in plan units, so they follow the scale -------------------
START = (198, 158)                 # crew quarters, clear of the bunks
CORE_AT = (340, 465)               # central lobby
TURRET_AT = (300, 465)
SPAWN_AT = [
    ('bow',    'BOW PASSAGE',   319, 300, 1, 0.9, 1),
    ('mess',   'MESS HALL',     475, 335, 2, 1.1, 0),
    ('stores', 'LOWER PASSAGE', 301, 585, 3, 0.9, 0),
    ('engine', 'ENGINE ROOM',   340, 895, 4, 2.4, 1),
]
BARRELS = [(215, 858), (262, 858), (392, 858), (452, 858),
           (250, 668), (285, 692), (450, 745), (505, 745),
           (452, 812), (215, 912)]
CRATES = [(240, 676), (470, 760), (500, 790), (300, 440), (380, 440)]
