"""The material set for a ship, replacing Sector C's research-facility one.

What actually makes a surface read as Doom 3 rather than as a painted
box, in the order the eye picks it up:

1. **Two nested panel grids.**  A big plate, roughly a metre, with a deep
   recessed seam; inside it a finer grid of access panels with a shallow one.
   One grid alone reads as tiling; two read as an engineered plate.
2. **Fasteners.**  A field of bolt heads standing proud near the seams.  They
   are small, they are the first thing a normal map can show, and nothing
   says "machined" faster.
3. **Roughness driven by the panel field, never flat.**  Plate faces are
   near-glossy, seams and grime are matte.  A single roughness value is what
   makes procedural metal look like plastic.
4. **Warm, dark, desaturated.**  The ship's grey is a brown-grey around 0.20, not the
   0.50 blue-grey of an office.  The light does the colouring; the albedo
   stays out of the way.
5. **Wear where hands and boots go.**  Ochre grime in the recesses, bare metal
   on the raised edges.

Texel density is one number for the whole ship (TEX_SCALE), for the same
reason it is one number for the creature: a wall whose panels are twice the
size of the next wall's reads as two different buildings.
"""

# op indices, matching the OP table in the engine
FLAT, PERLIN, CELL, GRAD, RECTS, GLOW, SCAN, STRIPE = 0, 1, 2, 3, 4, 5, 6, 7
BLUR, DISTORT, ADD, SUB, MUL, BLEND, MASK, LEVELS = 8, 9, 10, 11, 12, 13, 14, 15
HSCB, COLORIZE, CAVITY, NORMALS, COPY, MULC, MIXC, SHARP = 16, 17, 18, 19, 20, 21, 22, 23
GLOWR, XFORM, XMUL = 24, 25, 26          # soft box, warp, repeat


# ---- three small patterns every plate is built from ------------------------
# They call k_array/k_field, which are defined further down; Python resolves
# the names when a recipe is built, not here.

def edge_frame(reg, soft=0.016, half=0.48, power=0.16):
    """The plate's own outline: one centred box that stops `1-2*half` short of
    the tile edge, so neighbouring tiles meet across a dark seam."""
    return k_field(reg, half, half, soft=soft, power=power)


def rivet_line(reg, r=0.014, alpha=0.5, power=0.32, down=4, across=1,
               inset=0.034, gap=0.058, lead=None):
    """Rivets down the left edge of the plate, just inside its seam: one dot
    per 1/2**down of the tile, centred in its slot, and 2**across rows of them
    `gap` apart.  `lead` moves the first one, so a line can sit on a rail
    instead of in the middle of its slot.  A run of fasteners is the cue that
    reads as machined."""
    if lead is None:
        lead = 0.5 / (1 << down)
    out = [[FLAT, reg, 0, 0, 0],
           [GLOWR, reg, inset, lead, r, r, 0, 0, 1, 1, 1, alpha, power]]
    out += k_array(reg, ny=down)
    if across:
        rows = (1 << across) - 1
        out.append([XMUL, reg, 0, 1, 1, 0.5 + gap * rows, 0.5, 0, across, 1])
    return out


def rib_field(reg, count=4, w=0.032, h=0.36):
    """Tread ribs: a soft ridge with a darker groove pressed into its crest,
    repeated 2**count times evenly across the tile."""
    lead = 0.5 / (1 << count)
    return ([[FLAT, reg, 0, 0, 0],
             [GLOWR, reg, lead, 0.5, w, 0.022, 0, h, .30, .30, .30, 1.0, 0.12],
             [GLOWR, reg, lead, 0.5, 0.45 * w, 0.45 * w, 0, h - 0.035, .15, .15, .15, 1.0, 0.12]]
            + k_array(reg, nx=count))


# ---- the two accents, and the one rule that governs them -----------------
# A ship reads as designed rather than as assembled when its accent colour
# appears in a SMALL NUMBER OF NAMED ROLES and nowhere else.  There are three
# here and there will never be a fourth:
#
#   OCHRE  painted.  "This comes off."  The edge sliver on a removable panel,
#          a hatch, an access lid.  It is never lit, because a dead ship's
#          paint is not lit.
#   AMBER  lit.  "This is powered."  The light channel, the deck cartridge.
#   LIVE   lit, cool.  "This system is still running."  Readout segments only.
#
# The ratio of LIVE to dark readouts is the whole mood dial: every panel lit
# is a working ship, one in eight is a derelict with something still awake.
OCHRE = (.560, .330, .062)      # the same value the stencil already uses
AMBER = (1.00, .520, .130)
LIVE_C = (.180, .580, .780)     # a cyan segment
LIVE_G = (.200, .720, .280)     # and a green one


def k_array(reg, nx=0, ny=0, mode=0):
    """Copy `reg` onto itself 2**nx across and 2**ny down, spread evenly over
    the tile.  Repeat doubles its own translation each pass, so a pitch of
    p over n passes needs a first step of p * (2**n - 1) -- get that wrong and
    the copies bunch at one edge instead of covering the tile."""
    out = []
    if nx:
        d = (1 << nx) - 1
        out.append([XMUL, reg, 0, 1, 1, 0.5 - d / float(1 << nx), 0.5, mode, nx, 1])
    if ny:
        d = (1 << ny) - 1
        out.append([XMUL, reg, 0, 1, 1, 0.5, 0.5 - d / float(1 << ny), mode, ny, 1])
    return out


def k_field(reg, sx, sy, soft=0.010, power=0.16, cx=0.5, cy=0.5):
    """A single filled rectangle: 1 inside, 0 outside.  The building block of
    every plate picture below -- a recess, a pan, a bezel, a lip."""
    return [[FLAT, reg, 0, 0, 0],
            [GLOWR, reg, cx, cy, soft, soft, sx, sy, 1, 1, 1, 1.0, power]]


def k_stars(dst, s0, s1, count, seed, keep, lo, hi, gamma=1.6):
    """A star field, and the only honest way to draw one: the CORES of a
    cellular distance field, gated and graded by the cells' own ids.

    A star has no size, only a magnitude.  Perlin cannot make one -- noise
    thresholded high enough to give points gives BLOBS, and a blob is a
    smudge on the glass rather than a light-year away.  Cellular F1 has an
    exact minimum per cell, so 1 - F1 is a spike, and clipping the very top
    of that spike leaves a core one or two texels across.

    `keep` is therefore the star's SIZE and it is measured in cell widths:
    a cell is 256/count texels, and the core's radius is (1 - keep) of one.
    At count 12 a cell is 21 texels, so keep 0.95 gives a core about a
    texel across -- which is what a star is, and 3 texels is a planet.

    THE GATE IS THE OTHER HALF, and the first cut of this did not have one:
    one star per cell means 900 stars in a tile that subtends 11 degrees,
    and it came out as falling snow.  `lo`..`hi` selects which cells get a
    star at all out of the same id channel, and grades the survivors, so the
    field is Poisson rather than a lattice and skewed to the faint the way a
    real population is.  About twenty a tile is right: a tile is 1.2 m at
    the 6 m the chamber is deep, so it is 11 degrees of sky, and 11 degrees
    of real sky holds about that many stars you would name.

    Two scratch registers, both clobbered.
    """
    return [[CELL, s0, count, 0, 1.0, 1.0, seed],
            [FLAT, dst, 1, 1, 1],
            [SUB, dst, s0, 1.0],                       # -> a spike per cell
            [LEVELS, dst, keep, 1.00, 1.0],            # -> only its core
            [CELL, s1, count, 2, 1.0, 1.0, seed],      # the same cells' ids
            [LEVELS, s1, lo, hi, gamma],               # which cells, how bright
            [MUL, dst, s1, 1]]


# The glass itself, and it is the same two registers on both view materials:
# a window is one surface, so the dirt on it has to be one recipe or the two
# panes of the same window come back from the mill with different histories.
def k_glass(dirt, sc, seed):
    """`dirt` <- the dust film and the fine scratching, ready to be mixed
    into albedo and added to roughness.  `sc` is clobbered."""
    return [[PERLIN, dirt, 5, 4, 0.5, 0, seed, 1.0, 0],
            [LEVELS, dirt, 0.64, 0.97, 1.4],           # the salt haze
            [PERLIN, sc, 44, 2, 0.5, 1, seed + 1, 1.0, 0],
            [LEVELS, sc, 0.88, 1.00, 1.0],
            # stretched 11:1 and turned a little off the vertical, because
            # a scratch is made by something being wiped, and a wipe has a
            # direction
            [XFORM, sc, 0.07, 1, 0.09, 0.5, 0.5],
            [ADD, dirt, sc, 0.80]]


NAMES = [
    'Deck plate', 'Hull plate', 'Overhead panel', 'Machined panel',
    'Hazard chevron', 'Scorched plate', 'Louvre grille', 'Composite liner',
    'Bar grating', 'Equipment rack', 'Safety ochre', 'Cargo container',
    'Display', 'Oiled deck', 'Conduit', 'Lamp diffuser',
    'Structural rib', 'Amber strip', 'Cool strip', 'Machined trim',
    # ---- the surface set: finishes, not wall designs.  See below.
    'Machined face', 'Rolled skin', 'Coated plate', 'Shadow gap',
    'Light channel', 'Jacket', 'Copper run', 'Stencil mark',
    # ---- the plate set: pictures, unwrapped on the plate.  See below.
    'Louvre bank', 'Perforated pan', 'Access panel', 'Readout',
    'Safety band',
    # ---- the bay set: assemblies, for shell surfaces with no mesh
    'Bay deck', 'Bay plate', 'Bay hull', 'Bay overhead',
    # ---- the view set: the pane, and the two layers behind it
    'Viewport starfield', 'Viewport planet', 'Viewport glass',
]

# normal strength per material.  Panel work wants a lot; screens want none.
BUMP = [5.0, 4.6, 4.2, 5.2, 2.6, 4.4, 7.0, 3.4, 9.0, 4.4, 3.4, 4.4, 0, 2.4, 5.0, 2.4,
        6.5, 0.6, 0.6, 3.6,
        # The surface set is deliberately shallow.  Its relief is tool marks
        # and grain, and the moment a finish bumps as hard as a panel it is
        # competing with the chamfer the part actually has.
        # COAT drops from 1.0 to 0.22, and that is the deck talking.  The
        # shader re-tiles this same normal map SIX TIMES over as a detail
        # layer within eight metres, so whatever a material puts in its
        # height channel arrives on a near surface at six times the
        # frequency -- on the deck plate, which is the largest single
        # surface in the corridor and the one you are always within two
        # metres of, the orange peel and the crackle beat against that
        # re-tiling and came out as a diagonal moire.  A sprayed deck is
        # SMOOTH: its relief is the chip and nothing else.
        # ...and MACH from 0.9 to 0.5, for the same reason: it is the
        # pilaster face, which is now 480 mm proud and 200 wide and passes
        # within arm's reach.  Its relief is 14 mm turning lines, and a
        # turning line you can see the bump of is a machining fault.
        0.5, 1.1, 0.22, 2.6, 0.5, 2.6, 1.0, 1.2,
        # The plate set draws real recesses, so it bumps like real recesses --
        # but it is unwrapped on ONE plate, so its features are the plate's
        # own and cannot disagree with it however hard they push.
        4.2, 3.6, 2.4, 1.4,
        1.6,
        # The bay set draws the assembly, so it bumps like one -- but the
        # shader re-tiles every normal map 6x as a detail layer inside five
        # metres, and a plate seam ghosted at 200 mm is a moire.  These sit
        # where a modelled chamfer sits and no harder: enough for the seam
        # to catch a lamp, not enough for the ghost to be found.
        2.8, 3.0, 1.6, 3.2,
        # A SKY HAS NO RELIEF.  The two layers behind the glass are unlit
        # by the shader, so a normal on them would do nothing but cost a
        # fetch -- and if it ever did land, a star with a bump on it is a
        # rivet.  The pane itself gets 0.4: the waviness the float bath
        # left in it and the dirt lying on it, and nothing else.
        0, 0, 0.40]

# how metallic each one is -- a strip of light is not metal, a rib is
METAL = [0, 0, 0, 1.0, 0, 0, 0, 0, 0.70, 0, 0, 0, 0, 0, 0.25, 0,
         1.0, 0, 0, 1.0,
         # Cut steel is a bare conductor.  ROLLED skin is not, quite: mill
         # scale is an oxide, and a conductor gets dAlb = albedo x 0.15 in
         # the shader -- so at 1.0 a bare panel dropped among painted ones
         # went black and read as a hole in the deck rather than as steel.
         # 0.55 is both truer to scale and legible.
         1.0, 0.55, 0, 0, 0, 0, 1.0, 0,
         # slats and grate ligaments are cut steel; a sprayed lid and a
         # glass readout are not conductors at all
         1.0, 0.70, 0.45, 0,
         0,
         # a deck is coated and a bulkhead is painted; the hull band above
         # the string course is bare rolled plate, and the overhead is a
         # sprayed pan.  Only the hull conducts.
         0, 0, 0.55, 0,
         # Glass is a DIELECTRIC, and that is the whole material.  A
         # conductor's F0 is its albedo and it has no diffuse lobe, so a
         # metallic pane would reflect black and show nothing.  At 0 the
         # shader gives it F0 = 0.04 with a Fresnel term that climbs to
         # ~0.95 at a grazing angle -- which is exactly what glass does,
         # and why the pane goes to a mirror as you walk past it and back
         # to a window when you stand in front of it.  The two sky layers
         # are never lit at all, so their value is moot.
         0, 0, 0]

# WHAT KIND OF SURFACE EACH MATERIAL IS, which is a thing this renderer had
# no way to say until there was a window in the ship.  Everything it drew was
# opaque and lit by the same bake, and that is the whole of what a corridor
# needs -- but a window is three surfaces at three distances, and two of them
# are not in the room at all:
#
#   0  ROOM     lit by the bake, fogged, opaque.  Everything else in the ship.
#   1  GLASS    drawn last, blended.  Its own reflection is what you see and
#               its alpha is a Fresnel term, so it is a window head on and a
#               mirror at a grazing angle -- which is what glass is.
#   2  SKY      opaque, but unlit and UNFOGGED.  A star is not 7 m away
#               through haze, it is a light-year away through nothing, and
#               the moment the corridor's fog touches it the illusion dies.
#   3  SPRITE   the same, but blended, with its coverage in the ROUGHNESS
#               channel -- an unlit surface has no use for a roughness, and
#               a sprite has nothing else to put an alpha in.
SURF = [0] * 37 + [2, 3, 1]

TEX_SCALE = 1.0 / 1.20   # uv = worldPos * this.  ONE TILE IS ONE MODULE.
# 1.20 m is the kit's placement module -- a floor tile, a wall bay -- so
# setting the texel grid to the same number means a material's tile and a
# part's footprint are the same rectangle.  A texel is 4.6875 mm, and the
# ceiling for any structural feature a material may draw (see the surface
# set below) is 80 mm, which is 0.0667 in uv.

R = [None] * len(NAMES)

# ---- 0  deck plate -------------------------------------------------------
# Ribbed anti-slip over a plate grid.  The ribs run one way on purpose: a
# diamond pattern reads as a fire escape, a single rib run reads as a ship.
R[0] = (
    edge_frame(4, soft=0.014, half=0.478, power=0.18)
    + rib_field(5, count=5, w=0.030, h=0.46)
    + rivet_line(6, r=0.010, alpha=0.5, power=0.30, down=3, across=1)
    + [
        [COPY, 1, 4],
        [ADD, 1, 5, 0.34],                 # the tread ribs
        [ADD, 1, 6, 0.26],
        [LEVELS, 1, 0.0, 1.15, 1.0],
        [COLORIZE, 0, 1, .082, .078, .070, .196, .186, .170, .318, .304, .278],
        [PERLIN, 7, 4, 4, 0.5, 0, 104, 1.0, 0],
        [LEVELS, 7, 0.50, 0.88, 1.3],
        [MIXC, 0, 7, .215, .175, .120, 0.38],
        [CAVITY, 0, 1, 3, 8.0, 0.60],
        [FLAT, 2, 0.56, 0, 0], [SUB, 2, 4, 0.20], [SUB, 2, 5, 0.12],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 1  hull plate -------------------------------------------------------
R[1] = (
    edge_frame(4, soft=0.020, half=0.455, power=0.20)
    + rivet_line(5, r=0.013, alpha=0.60, power=0.28, down=3, across=1)
    + [
        [COPY, 1, 4],
        [ADD, 1, 5, 0.46],
        [PERLIN, 7, 6, 4, 0.5, 0, 114, 0.10, 0],
        [ADD, 1, 7, 1],
        [LEVELS, 1, 0.0, 1.08, 1.0],
        [COLORIZE, 0, 1, .096, .093, .087, .228, .222, .210, .372, .363, .344],
        [PERLIN, 7, 3, 4, 0.5, 0, 115, 1.0, 0],
        [LEVELS, 7, 0.52, 0.90, 1.3],
        [MIXC, 0, 7, .200, .165, .118, 0.32],
        [CAVITY, 0, 1, 3, 7.5, 0.58],
        [FLAT, 2, 0.52, 0, 0], [SUB, 2, 4, 0.26],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 2  overhead panel ---------------------------------------------------
# Beams one way and nothing the other.  A square grid overhead fights the
# square grid on the walls and the eye reads the whole room as graph paper;
# every reference overhead is structure *spanning* the space, so the pattern
# has a direction and the walls get to keep theirs.
R[2] = [
    [SCAN, 4, 2, 0.40, 0.055, 1.0],          # the beams
    [FLAT, 5, 0, 0, 0],                      # the panel let into each bay
    [GLOWR, 5, 0.5, 0.253, 0.012, 0.012, 0.455, 0.126, 1, 1, 1, 1.0, 0.34],
    [GLOWR, 5, 0.5, 0.753, 0.012, 0.012, 0.455, 0.126, 1, 1, 1, 1.0, 0.34],
    [CELL, 6, 34, 0, 2.4, 2.6, 122],         # the perforation
    [LEVELS, 6, 0.58, 1.0, 1.0],
    [COPY, 1, 4], [MULC, 1, 0.30, 0.30, 0.30],
    [ADD, 1, 5, 0.60],
    [SUB, 1, 6, 0.30],
    [LEVELS, 1, 0.0, 1.1, 1.0],
    [COLORIZE, 0, 1, .040, .039, .038, .108, .105, .100, .190, .186, .178],
    [CAVITY, 0, 1, 3, 8.5, 0.72],
    [FLAT, 2, 0.74, 0, 0], [SUB, 2, 5, 0.22],
    [FLAT, 3, 0, 0, 0],
]

# ---- 3  machined panel  (the workhorse) ---------------------------------
# A field of equal squares is what made the ship read as tiling, and no
# amount of grime hides it: the eye finds the grid and the grid has no
# direction, so the corridor loses its perspective.  Two moves out of the
# reference kits fix it and cost nothing:
#
#   * the courses are BONDED -- offset half a plate -- so no seam runs the
#     full height of a wall and the pattern stops being a lattice;
#   * a raised rail sits in every course seam, which gives the wall a
#     horizontal grain for the corridor to converge along.
#
# The plates are drawn with the soft box, whose falloff is radial, so every
# corner comes out cut rather than square -- the chamfer the reference
# panels all have, for free.
def k_plate(reg, cu, cv, sx=0.224, sy=0.212, r=0.015, power=0.40, lum=1.0):
    return [GLOWR, reg, cu % 1.0, cv, r, r, sx, sy, lum, lum, lum, 1.0, power]


def k_course(reg, v, u0):
    return [k_plate(reg, u0, v), k_plate(reg, u0 + 0.5, v)]


R[3] = (
    [[FLAT, 4, 0, 0, 0]]
    + k_course(4, 0.253, 0.25)
    + k_course(4, 0.753, 0.00)          # bonded half a plate across
    + [
        [FLAT, 5, 0, 0, 0],             # the rail each course stands on
        [GLOWR, 5, 0.5, 0.503, 0.005, 0.005, 0.5, 0.019, 1, 1, 1, 1.0, 0.22],
        [GLOWR, 5, 0.5, 0.003, 0.005, 0.005, 0.5, 0.019, 1, 1, 1, 1.0, 0.22],
    ]
    + rivet_line(6, r=0.010, alpha=0.55, power=0.30, down=1, across=3, lead=0.003)   # on the two rails
    + [
        [COPY, 1, 4],
        [ADD, 1, 5, 0.55],                 # rail proud of the plate
        [ADD, 1, 6, 0.26],                 # rivets proud of the rail
        [LEVELS, 1, 0.0, 1.05, 1.0],
        [COLORIZE, 0, 1, .092, .087, .080, .240, .230, .212, .412, .398, .370],
        [PERLIN, 7, 3, 3, 0.5, 0, 135, 1.0, 0],
        [LEVELS, 7, 0.48, 0.86, 1.2],
        [MIXC, 0, 7, .250, .202, .140, 0.30],
        [CAVITY, 0, 1, 3, 7.0, 0.58],
        [FLAT, 2, 0.46, 0, 0], [SUB, 2, 4, 0.26], [ADD, 2, 5, 0.14],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 4  hazard chevron ---------------------------------------------------
R[4] = [
    [STRIPE, 4, 7, 0.10],
    [PERLIN, 5, 12, 4, 0.5, 0, 141, 1.0, 0],
    [LEVELS, 5, 0.46, 0.86, 1.2],
    [RECTS, 6, 1, 1, 0.020, 0.030, 0, 142],
    [FLAT, 0, .055, .052, .048],
    [MIXC, 0, 4, .78, .48, .045, 1.0],
    [MIXC, 0, 5, .16, .15, .14, 0.75],
    [COPY, 1, 4], [MULC, 1, 0.25, 0.25, 0.25],
    [ADD, 1, 6, 0.6],
    [CAVITY, 0, 1, 2, 6.0, 0.35],
    [FLAT, 2, 0.60, 0, 0], [SUB, 2, 4, 0.16],
    [FLAT, 3, 0, 0, 0],
]

# ---- 5  scorched plate ---------------------------------------------------
# For damage and rubble only.  Dark iron: the corrosion is orange *in the
# pits*, never across the face, or the whole thing reads as terracotta.
R[5] = [
    [PERLIN, 4, 7, 5, 0.55, 1, 151, 1.0, 0],
    [PERLIN, 5, 26, 4, 0.5, 0, 152, 0.30, 0],
    [ADD, 4, 5, 1],
    [RECTS, 6, 2, 2, 0.014, 0.040, 0, 153],
    [COPY, 1, 6],
    [SUB, 1, 4, 0.42],
    [LEVELS, 1, 0.05, 1.0, 1.0],
    [COLORIZE, 0, 1, .045, .038, .033, .105, .092, .080, .190, .176, .160],
    [PERLIN, 7, 5, 5, 0.5, 1, 154, 1.0, 0],
    [LEVELS, 7, 0.62, 0.95, 1.6],
    [MIXC, 0, 7, .225, .105, .038, 0.85],
    [CAVITY, 0, 1, 3, 8.0, 0.65],
    [FLAT, 2, 0.80, 0, 0], [SUB, 2, 6, 0.14],
    [FLAT, 3, 0, 0, 0],
]

# ---- 6  louvre grille ----------------------------------------------------
R[6] = [
    [SCAN, 4, 13, 0.30, 0.06, 1.0],
    [RECTS, 5, 1, 1, 0.030, 0.020, 0, 161],
    [COPY, 1, 4], [MUL, 1, 5, 1],
    [PERLIN, 7, 30, 3, 0.5, 0, 162, 0.10, 0],
    [ADD, 1, 7, 1],
    [COLORIZE, 0, 1, .028, .027, .026, .085, .082, .078, .175, .170, .162],
    [CAVITY, 0, 1, 2, 9.0, 0.75],
    [FLAT, 2, 0.62, 0, 0], [SUB, 2, 4, 0.18],
    [FLAT, 3, 0, 0, 0],
]

# ---- 7  composite liner  (medical, quarters) -----------------------------
# The one pale material on the ship, so the rooms that still work read as
# clean without going white.
R[7] = [
    [RECTS, 4, 3, 3, 0.010, 0.022, 0, 171],
    [RECTS, 5, 9, 9, 0.007, 0.010, 0.08, 172],
    [MUL, 4, 5, 1],
    [COPY, 1, 4],
    [PERLIN, 7, 40, 3, 0.5, 0, 173, 0.08, 0],
    [ADD, 1, 7, 1],
    [COLORIZE, 0, 1, .155, .162, .158, .300, .312, .305, .420, .434, .424],
    [PERLIN, 7, 4, 4, 0.5, 0, 174, 1.0, 0],
    [LEVELS, 7, 0.62, 0.95, 1.4],
    [MIXC, 0, 7, .215, .190, .150, 0.42],
    [CAVITY, 0, 1, 2, 7.0, 0.42],
    [FLAT, 2, 0.36, 0, 0], [SUB, 2, 4, 0.14],
    [FLAT, 3, 0, 0, 0],
]

# ---- 8  bar grating ------------------------------------------------------
# The bars catch light, the gaps go to black.  High bump: this is the one
# material whose whole job is done by the normal.
R[8] = [
    [SCAN, 4, 16, 0.36, 0.05, 1.0],
    [SCAN, 5, 4, 0.42, 0.05, 1.0],
    [COPY, 6, 4], [ADD, 6, 5, 1], [LEVELS, 6, 0.30, 0.90, 1.0],
    [COPY, 1, 6],
    [COLORIZE, 0, 1, .026, .025, .023, .112, .108, .101, .285, .276, .258],
    [CAVITY, 0, 1, 2, 10.0, 0.80],
    [FLAT, 2, 0.44, 0, 0], [SUB, 2, 6, 0.18],
    [FLAT, 3, 0, 0, 0],
]

# ---- 9  equipment rack ---------------------------------------------------
R[9] = [
    [RECTS, 4, 2, 7, 0.020, 0.030, 0.14, 181],
    [SCAN, 5, 44, 0.42, 0.08, 0.55],
    [CELL, 6, 20, 0, 3.0, 3.0, 182],
    [LEVELS, 6, 0.70, 1.0, 1.0],
    [COPY, 1, 4], [ADD, 1, 5, 0.20], [ADD, 1, 6, 0.22],
    [COLORIZE, 0, 1, .048, .048, .050, .120, .122, .128, .215, .220, .230],
    [CAVITY, 0, 1, 3, 8.0, 0.62],
    [GLOW, 0, 0.22, 0.16, 0.020, 0.012, 0.35, 1.0, 0.42, 0.12, 1.0],
    [GLOW, 0, 0.30, 0.16, 0.020, 0.012, 0.35, 0.22, 1.0, 0.45, 1.0],
    [GLOW, 0, 0.74, 0.62, 0.020, 0.012, 0.35, 1.0, 0.42, 0.12, 1.0],
    [FLAT, 3, 0, 0, 0],
    [GLOW, 3, 0.22, 0.16, 0.020, 0.012, 0.35, 1, 1, 1, 1.0],
    [GLOW, 3, 0.30, 0.16, 0.020, 0.012, 0.35, 1, 1, 1, 1.0],
    [GLOW, 3, 0.74, 0.62, 0.020, 0.012, 0.35, 1, 1, 1, 1.0],
    [FLAT, 2, 0.48, 0, 0], [SUB, 2, 4, 0.16],
]

# ---- 10  safety ochre ----------------------------------------------------
# Painted structural steel, chipped back to metal on the edges.
R[10] = [
    [RECTS, 4, 2, 2, 0.012, 0.040, 0, 191],
    [PERLIN, 5, 9, 5, 0.5, 1, 192, 1.0, 0],
    [LEVELS, 5, 0.55, 0.86, 1.5],
    [CELL, 6, 14, 0, 3.2, 3.2, 193],
    [LEVELS, 6, 0.70, 1.0, 1.0],
    [COPY, 1, 4], [ADD, 1, 6, 0.22],
    [PERLIN, 7, 34, 3, 0.5, 0, 194, 0.10, 0],
    [ADD, 1, 7, 1],
    [FLAT, 0, .360, .215, .062],
    [MIXC, 0, 5, .120, .112, .105, 0.85],
    [MIXC, 0, 6, .285, .272, .255, 0.55],
    [CAVITY, 0, 1, 3, 7.0, 0.55],
    [FLAT, 2, 0.58, 0, 0], [SUB, 2, 4, 0.16], [SUB, 2, 5, 0.20],
    [FLAT, 3, 0, 0, 0],
]

# ---- 11  cargo container -------------------------------------------------
R[11] = [
    [SCAN, 4, 9, 0.34, 0.05, 1.0],
    [RECTS, 5, 1, 1, 0.022, 0.030, 0, 201],
    [COPY, 1, 4], [MULC, 1, 0.55, 0.55, 0.55], [ADD, 1, 5, 0.5],
    [CELL, 6, 12, 0, 3.2, 3.2, 202],
    [LEVELS, 6, 0.74, 1.0, 1.0],
    [ADD, 1, 6, 0.20],
    [COLORIZE, 0, 1, .085, .073, .052, .195, .168, .112, .300, .262, .180],
    [PERLIN, 7, 5, 4, 0.5, 0, 203, 1.0, 0],
    [LEVELS, 7, 0.58, 0.92, 1.4],
    [MIXC, 0, 7, .145, .130, .105, 0.55],
    [CAVITY, 0, 1, 3, 7.5, 0.55],
    [FLAT, 2, 0.66, 0, 0], [SUB, 2, 4, 0.16],
    [FLAT, 3, 0, 0, 0],
]

# ---- 12  display ---------------------------------------------------------
R[12] = [
    [PERLIN, 4, 40, 2, 0.5, 0, 211, 1.0, 0],
    [LEVELS, 4, 0.42, 0.62, 1.0],
    [SCAN, 5, 84, 0.42, 0.10, 1.0],
    [MUL, 4, 5, 1],
    [RECTS, 6, 1, 1, 0.035, 0.020, 0, 212],
    [MUL, 4, 6, 1],
    [FLAT, 0, .012, .016, .014],
    [MIXC, 0, 4, .28, 1.0, .52, 1.0],
    [COPY, 3, 4], [MULC, 3, 0.80, 0.80, 0.80],
    [FLAT, 1, 0, 0, 0],
    [FLAT, 2, 0.22, 0, 0],
]

# ---- 13  oiled deck ------------------------------------------------------
R[13] = [
    [RECTS, 4, 2, 2, 0.014, 0.045, 0, 221],
    [PERLIN, 5, 7, 5, 0.55, 0, 222, 1.0, 0],
    [PERLIN, 6, 30, 3, 0.5, 0, 223, 0.24, 0],
    [ADD, 5, 6, 1],
    [COPY, 1, 4], [SUB, 1, 5, 0.30],
    [LEVELS, 1, 0.05, 1.0, 1.0],
    [COLORIZE, 0, 1, .022, .021, .020, .058, .056, .053, .105, .102, .097],
    [LEVELS, 5, 0.58, 0.92, 1.5],
    [MIXC, 0, 5, .030, .028, .033, 0.85],
    [CAVITY, 0, 1, 3, 8.0, 0.55],
    [FLAT, 2, 0.34, 0, 0], [SUB, 2, 5, 0.16],
    [FLAT, 3, 0, 0, 0],
]

# ---- 14  conduit ---------------------------------------------------------
R[14] = [
    [SCAN, 4, 6, 0.20, 0.05, 1.0],
    [PERLIN, 5, 22, 3, 0.5, 0, 231, 0.14, 0],
    [COPY, 1, 4], [MULC, 1, 0.6, 0.6, 0.6], [ADD, 1, 5, 1],
    [COLORIZE, 0, 1, .058, .055, .050, .132, .126, .116, .215, .206, .190],
    [MIXC, 0, 4, .285, .150, .045, 0.55],
    [CAVITY, 0, 1, 2, 7.0, 0.50],
    [FLAT, 2, 0.44, 0, 0], [SUB, 2, 4, 0.14],
    [FLAT, 3, 0, 0, 0],
]

# ---- 15  lamp diffuser ---------------------------------------------------
R[15] = [
    [RECTS, 4, 3, 1, 0.040, 0.035, 0, 241],
    [SCAN, 5, 26, 0.40, 0.06, 1.0],
    [PERLIN, 6, 18, 3, 0.5, 0, 242, 0.14, 0.90],
    [MUL, 4, 6, 1],
    [COPY, 3, 4],
    [MULC, 3, 1.30, 1.24, 1.10],
    [FLAT, 0, .30, .29, .27],
    [MIXC, 0, 4, 1.00, 0.94, 0.82, 1.0],
    [COPY, 1, 5], [MULC, 1, -0.35, -0.35, -0.35],
    [FLAT, 2, 0.70, 0, 0],
]


# ==========================================================================
#  THE FOUR THE SHIP WAS MISSING
#  Every reference image is built out of the same four things and the ship
#  had none of them: a structural rib that is darker than the plate it
#  crosses, a hot line and a cold line, and a bright machined edge to catch
#  the light at every opening.  The rib gives the corridor its beat, the two
#  lines give it its perspective and its colour temperature, the trim gives
#  the openings an edge.
# ==========================================================================

# ---- 16  structural rib --------------------------------------------------
# Fluted steel, and darker than the plate: a rib that is the same value as
# the wall is not a rib, it is a smudge.
R[16] = [
    [SCAN, 4, 8, 0.26, 0.055, 1.0],
    [XFORM, 4, 0.25, 1, 1, 0.5, 0.5],          # stand the flutes upright
    [FLAT, 5, 0, 0, 0],
    [GLOWR, 5, 0.5, 0.5, 0.008, 0.008, 0.47, 0.44, 1, 1, 1, 1.0, 0.24],
    [COPY, 1, 4], [MULC, 1, 0.55, 0.55, 0.55],
    [ADD, 1, 5, 0.30],
    [PERLIN, 7, 24, 3, 0.5, 0, 251, 0.09, 0],
    [ADD, 1, 7, 1],
    [LEVELS, 1, 0.0, 1.05, 1.0],
    [COLORIZE, 0, 1, .046, .046, .048, .124, .123, .126, .224, .222, .226],
    [PERLIN, 7, 4, 4, 0.5, 0, 252, 1.0, 0],
    [LEVELS, 7, 0.56, 0.92, 1.4],
    [MIXC, 0, 7, .190, .150, .098, 0.26],
    [CAVITY, 0, 1, 3, 8.0, 0.66],
    [FLAT, 2, 0.56, 0, 0], [SUB, 2, 4, 0.22],
    [FLAT, 3, 0, 0, 0],
]

# ---- 17  amber strip -----------------------------------------------------
# The hot line.  Emissive geometry costs the lighting solver nothing and it
# is the single thing every reference corridor is drawn with: the line runs
# the length of the passage and *is* the perspective.  Segmented, because a
# dead ship's strip lighting is never a clean run.
R[17] = [
    [SCAN, 4, 22, 0.36, 0.06, 1.0],
    [XFORM, 4, 0.25, 1, 1, 0.5, 0.5],          # segments along the run
    [PERLIN, 5, 9, 3, 0.5, 0, 261, 1.0, 0],
    [LEVELS, 5, 0.30, 0.80, 1.0],
    [FLAT, 0, .34, .13, .035],
    [MIXC, 0, 4, 1.00, .52, .13, 1.0],         # the lit segments
    [MIXC, 0, 5, .62, .26, .06, 0.35],         # a few that are failing
    [COPY, 3, 4],
    [MUL, 3, 5, 1],
    [LEVELS, 3, 0.10, 0.90, 0.8],
    [FLAT, 1, 0.5, 0, 0], [SUB, 1, 4, 0.30],
    [FLAT, 2, 0.34, 0, 0],
]

# ---- 18  cool strip ------------------------------------------------------
# The cold line, for the systems that still work.  Warm and cold have to be
# in the same shot or neither reads as a colour -- that contrast is what the
# sinking gauge later spends when it turns the ship red.
R[18] = [
    [SCAN, 4, 16, 0.42, 0.05, 1.0],
    [XFORM, 4, 0.25, 1, 1, 0.5, 0.5],
    [PERLIN, 5, 7, 3, 0.5, 0, 271, 1.0, 0],
    [LEVELS, 5, 0.26, 0.78, 1.0],
    [FLAT, 0, .16, .21, .26],
    [MIXC, 0, 4, .62, .82, 1.00, 1.0],
    [COPY, 3, 4],
    [MUL, 3, 5, 1],
    [LEVELS, 3, 0.12, 0.92, 0.9],
    [FLAT, 1, 0.5, 0, 0], [SUB, 1, 4, 0.26],
    [FLAT, 2, 0.30, 0, 0],
]

# ---- 19  machined trim ---------------------------------------------------
# Bare, bright, hard.  It goes on the edge of every opening and on the nose
# of every rail, and its whole job is to be the brightest thing in a dark
# frame so the eye finds the doorway.
R[19] = [
    [FLAT, 4, 0, 0, 0],
    [GLOWR, 4, 0.5, 0.5, 0.010, 0.010, 0.46, 0.30, 1, 1, 1, 1.0, 0.30],
    [SCAN, 5, 4, 0.30, 0.05, 1.0],
    [COPY, 1, 4],
    [ADD, 1, 5, 0.16],
    [PERLIN, 7, 40, 3, 0.5, 0, 281, 0.07, 0],
    [ADD, 1, 7, 1],
    [COLORIZE, 0, 1, .150, .148, .144, .360, .356, .348, .560, .556, .546],
    [PERLIN, 7, 6, 4, 0.5, 0, 282, 1.0, 0],
    [LEVELS, 7, 0.62, 0.95, 1.4],
    [MIXC, 0, 7, .225, .180, .120, 0.24],
    [CAVITY, 0, 1, 3, 6.0, 0.44],
    [FLAT, 2, 0.24, 0, 0], [SUB, 2, 4, 0.10],
    [FLAT, 3, 0, 0, 0],
]


# ==========================================================================
#  20..27  THE SURFACE SET  --  finishes, not wall designs
#
#  Everything above this line is a *picture of a wall*.  `Machined panel`
#  draws a 0.72 m plate, a rail every 0.80 m and a field of rivets, and it
#  had to: it was painted onto brushes, and a brush has no shape of its own.
#
#  A kit part does have shape.  It has a real 16 mm chamfer, a real stile, a
#  real recess and real bolts -- and the moment those exist, the texture must
#  stop drawing its own.  Two panel grids at unrelated pitches is exactly
#  what makes a wall read as wallpaper stretched over a model: the eye finds
#  eight horizontal seams where the geometry only put three, and resolves
#  neither set.
#
#  So, the rule for everything below:
#
#      THE MATERIAL'S LARGEST *STRUCTURAL* FEATURE MUST BE SMALLER THAN
#      THE GEOMETRY'S SMALLEST ONE.
#
#  One tile is 1.61 m over 256 texels, so a texel is 6.3 mm and the smallest
#  thing the kit models is that 16 mm chamfer.  The ceiling for a line, an
#  edge, a rivet or a rectangle is therefore ~80 mm, which is 0.05 in uv.
#  Nothing here draws anything larger.  Tonal variation is exempt -- grime,
#  staining and heat colour have no edge, so they can be any size at all and
#  will never be mistaken for a seam.
#
#  What is left once the pictures are gone is what the material actually IS,
#  and that is the thing the set was missing.  These eight are eight
#  finishes.  A single wall bay now wears four of them: machined where it was
#  cut, rolled where it was not, coated where it was sprayed, and black in
#  the gaps the light cannot reach.  That is where the depth comes from now,
#  and it is depth the geometry and the texture agree about.
#
#  The one exception is 27, and it proves the rule: a stencil IS a picture in
#  the real world, so it is allowed to be one here -- but it is unwrapped in
#  the part's own space (proj 3), so the picture lands on the plate instead
#  of being sliced out of a world grid.
# ==========================================================================

# ---- 20  machined face ---------------------------------------------------
# The cut face: stile fronts, rail noses, chamfer rims, plate lands.  Bright,
# because bare steel that has just been through a machine is the brightest
# thing in a dark corridor, and that is how the eye finds an edge.
#
# Its only relief is turning marks -- 112 lines across a tile, so 14 mm
# apart, five texels.  Below the size at which anything can be counted, which
# is the point: it reads as *finish*, not as pattern.
R[20] = [
    [SCAN, 4, 112, 0.36, 0.34, 1.0],            # fine turning lines
    [XFORM, 4, 0.25, 1, 1, 0.5, 0.5],           # stood upright, with the cut
    [SCAN, 5, 43, 0.30, 0.48, 1.0],             # a coarser pass, off-angle,
    [XFORM, 5, 0.268, 1, 1, 0.5, 0.5],          # so it never moires into a grid
    [CELL, 6, 74, 0, 1.9, 1.5, 611],            # micro pitting, ~22 mm cells
    # Height carries the turning lines and NOTHING else -- a machined face
    # you can feel is a machined face that was done badly.
    [FLAT, 1, 0.55, 0, 0],
    [ADD, 1, 4, 0.055],
    [ADD, 1, 5, 0.030],
    [PERLIN, 7, 5, 3, 0.5, 0, 612, 0.06, 0],
    [ADD, 1, 7, 1],
    [LEVELS, 1, 0.10, 1.00, 1.0],
    # No black point.  A machined face never goes to zero -- its shadows are
    # grey, and giving it a black one is what makes procedural steel plastic.
    [COLORIZE, 0, 1, .150, .147, .142, .310, .304, .293, .478, .470, .454],
    [PERLIN, 7, 13, 4, 0.5, 0, 613, 1.0, 0],
    [LEVELS, 7, 0.68, 0.97, 1.5],
    [MIXC, 0, 7, .208, .170, .116, 0.15],       # a little handling grime
    [CAVITY, 0, 1, 2, 5.0, 0.18],
    # ROUGHNESS is where the machining actually lives.  Glossy ground, the
    # tool lines matte on top of it, and the pitting matte on top of that:
    # a flat surface whose specular breaks up is steel; a bumpy surface
    # whose specular is uniform is stone.
    [FLAT, 2, 0.18, 0, 0],
    [ADD, 2, 4, 0.26],
    [ADD, 2, 5, 0.13],
    [ADD, 2, 6, 0.30],
    [FLAT, 3, 0, 0, 0],
]

# ---- 21  rolled skin -----------------------------------------------------
# The plate as it came off the mill and was never touched again: the large
# faces of a bay, the backs of things, the insides of ducts.  Mill scale is a
# mottle, not a pattern, so this is the one finish that is allowed a big
# feature -- it has no edges anywhere in it.
R[21] = [
    [PERLIN, 4, 9, 5, 0.55, 0, 621, 1.0, 0],    # scale mottle
    [LEVELS, 4, 0.36, 0.72, 1.1],
    [PERLIN, 5, 46, 3, 0.5, 1, 622, 0.55, 0],   # ridged: the rolling grain
    [XFORM, 5, 0.25, 1, 0.22, 0.5, 0.5],        # drawn out along the roll
    [CELL, 6, 58, 1, 2.6, 1.2, 623],            # weld spatter and pick-up
    [LEVELS, 6, 0.88, 1.0, 2.2],                # spatter is rare, not a field
    [FLAT, 1, 0.46, 0, 0],
    [ADD, 1, 4, 0.085],
    [ADD, 1, 5, 0.040],
    [ADD, 1, 6, 0.060],
    [LEVELS, 1, 0.04, 1.02, 1.0],
    [COLORIZE, 0, 1, .076, .072, .066, .180, .170, .156, .300, .286, .264],
    [PERLIN, 7, 4, 4, 0.5, 0, 624, 1.0, 0],
    [LEVELS, 7, 0.50, 0.88, 1.3],
    [MIXC, 0, 7, .224, .178, .118, 0.34],       # rust bloom in the low ground
    [CAVITY, 0, 1, 3, 6.5, 0.38],
    [FLAT, 2, 0.46, 0, 0],
    [ADD, 2, 4, 0.30],
    [SUB, 2, 6, 0.14],                          # spatter is proud and shiny
    [FLAT, 3, 0, 0, 0],
]

# ---- 22  coated plate ----------------------------------------------------
# Sprayed, not bare.  Two things make a coat read as a coat rather than as a
# colour: orange peel, which is a fine dimple the size of a texel, and
# CHIPPING -- and chipping only works because it exposes something.  The
# chip's floor is the rolled-skin brown underneath, so a coated part that
# sits next to a bare one belongs to it.
R[22] = [
    # Orange peel is a 0.1 mm dimple you can only see in a highlight.  Run
    # at full contrast -- and then re-tiled six times over by the shader's
    # detail normal -- it becomes aggregate, and a sprayed steel panel
    # reads as a cast concrete one.  Narrow band, small amplitude.
    [PERLIN, 4, 78, 2, 0.5, 0, 631, 1.0, 0],    # orange peel, ~21 mm
    [LEVELS, 4, 0.42, 0.62, 1.0],
    # Chipping is a RARE event at the edge of a crackle, not a texture.
    # Taken wide it reads as leopard spots and the panel stops being a
    # panel: only the last 18% of the crackle survives, and only inside a
    # narrow wear band, so a bay gets four or five chips and not four
    # hundred.
    [CELL, 5, 26, 1, 2.2, 4.2, 632],            # crackle, ~62 mm cells
    [LEVELS, 5, 0.94, 1.0, 1.0],                # ...only its very tips
    [PERLIN, 6, 11, 4, 0.5, 0, 633, 1.0, 0],    # where the chipping happens
    [LEVELS, 6, 0.662, 0.694, 1.0],             # a narrow band, not the wall
    [MUL, 5, 6, 1],                             # chips only in worn zones
    [FLAT, 1, 0.62, 0, 0],
    [ADD, 1, 4, 0.005],                         # peel: all but gone in relief
    [SUB, 1, 5, 0.16],                          # the chip, which IS a pit
    [LEVELS, 1, 0.06, 1.0, 1.0],
    # the ship's grey is a warm dark, not an office blue-grey: 0.16 not 0.50
    [COLORIZE, 0, 1, .070, .070, .072, .170, .170, .174, .278, .278, .284],
    [MIXC, 0, 5, .168, .134, .098, 0.42],       # bare metal in the chips
    [PERLIN, 7, 3, 4, 0.5, 0, 634, 1.0, 0],
    [LEVELS, 7, 0.56, 0.92, 1.4],
    [MIXC, 0, 7, .150, .118, .078, 0.26],
    [CAVITY, 0, 1, 3, 6.0, 0.30],
    # A DECK COAT IS NOT A WALL COAT.  0.66 was a flat wall paint and the
    # deck returned nothing under it: no lamp reflection, no sheen down the
    # length of the plate, no sense of a sealed floor.  0.44 got it to
    # epoxy.  0.22 is where it wants to be -- a poured, polished deck
    # coating, hard and slightly plastic, which is what carries the
    # overhead lamps as a soft streak the whole length of the passage.
    #
    # And the PEEL NO LONGER SCATTERS.  A gloss whose roughness is broken
    # up by a fine noise is not glossy, it is satin: the highlight breaks
    # into speckle instead of stretching.  The only thing allowed to vary
    # the roughness now is the chip, because a chip really is bare metal.
    [FLAT, 2, 0.22, 0, 0],
    [SUB, 2, 5, 0.14],                          # the bare chip, and only it
    [FLAT, 3, 0, 0, 0],
]

# ---- 23  shadow gap ------------------------------------------------------
# The floor of every recess, the back of every louvre, the inside of every
# channel.  This is the material that does the most work in the whole set and
# has the least in it: near black, wholly matte, so a modelled recess reads
# as DEPTH instead of as a differently-lit face of the same plate.
#
# The one thing it must not be is flat black -- black with no variation reads
# as a hole in the render.  It gets oil, and it gets grit, and nothing else.
R[23] = [
    [PERLIN, 4, 26, 4, 0.5, 0, 641, 1.0, 0],
    [LEVELS, 4, 0.34, 0.80, 1.2],
    [CELL, 5, 40, 0, 1.7, 1.8, 642],            # grit that has fallen in
    [FLAT, 1, 0.30, 0, 0],
    [ADD, 1, 4, 0.24],
    [ADD, 1, 5, 0.12],
    [COLORIZE, 0, 1, .016, .016, .018, .038, .038, .042, .072, .071, .074],
    [PERLIN, 7, 7, 4, 0.5, 0, 643, 1.0, 0],
    [LEVELS, 7, 0.60, 0.95, 1.6],
    [MIXC, 0, 7, .052, .040, .026, 0.50],       # oil, warm against the cold black
    [CAVITY, 0, 1, 3, 7.0, 0.40],
    [FLAT, 2, 0.94, 0, 0],                      # matte: no gap ever shines
    [FLAT, 3, 0, 0, 0],
]

# ---- 24  light channel ---------------------------------------------------
# The single strongest sci-fi cue there is, and the kit had none of it: a
# continuous lit line, run in the bottom of a modelled recess so the light
# and the shape are the same object.  A strip stuck on a flat wall is a
# sticker; a strip at the bottom of a 40 mm channel throws a hard edge up
# both of the channel's sides, and that is the whole effect.
#
# Frosted, so it is a lit surface and not a bulb, and broken into runs with a
# dark seal between them -- an unbroken glowing line for forty metres reads
# as "engine", and this is a dead ship.
R[24] = [
    [SCAN, 4, 96, 0.42, 0.10, 1.0],             # fluting, ~17 mm, across the slot
    [SCAN, 5, 7, 0.42, 0.03, 1.0],              # 1 between seals, 0 at them...
    [XFORM, 5, 0.25, 1, 1, 0.5, 0.5],           # ...turned to space them along it
    [PERLIN, 6, 5, 3, 0.5, 0, 651, 1.0, 0],
    [LEVELS, 6, 0.34, 0.70, 1.0],               # a few runs down on output
    [COPY, 7, 5],
    [MUL, 7, 6, 1],
    [LEVELS, 7, 0.10, 0.80, 1.0],               # -> the lit band, and only it
    # ALBEDO IS DIM ON PURPOSE.  The shader adds albedo x emissive x 2.1 on
    # top of the lit surface, so a diffuser whose albedo is 0.80 arrives at
    # 1.7, blows straight through the filmic shoulder, and desaturates --
    # which is how a cool white strip came out green.  A lamp's job is to be
    # bright in the FRAME, not bright in the texture.
    [FLAT, 0, .050, .060, .075],                # the housing, and it stays dark
    # A LAMP IS WHITE.  It is tempting to author a sci-fi strip as a
    # saturated cyan, and it does not survive this pipeline: hable()'s
    # shoulder compresses the gap between channels as they approach the
    # top, and the composite's split tone then multiplies highlights by
    # (1.07, 1.00, 0.89).  Between them, a cool colour whose green and
    # blue start close ends up with GREEN as its largest channel the
    # moment the strip is bright enough to matter.
    #
    # So: a blue-WHITE, red only 19% under blue.  At corridor level it
    # reads cool against the warm lamps, and when it blows out it goes to
    # white -- which is what a light that has blown out should do.
    [MIXC, 0, 7, .420, .456, .520, 1.0],        # the band
    [MIXC, 0, 4, .470, .505, .570, 0.45],       # its ribs picked out
    [COPY, 3, 7],                               # emissive IS the lit band
    [FLAT, 1, 0.5, 0, 0],
    [SUB, 1, 4, 0.30],
    [SUB, 1, 5, 0.16],                          # the seals stand proud
    [FLAT, 2, 0.36, 0, 0],
]

# ---- 25  jacket ----------------------------------------------------------
# Rubber, loom tape and composite: cable bundles, kick strips, the gasket a
# hatch closes onto.  It exists so that not every surface in the room is
# steel -- a corridor in which everything is the same substance reads as one
# extruded object however many parts went into it.
R[25] = [
    [SCAN, 4, 88, 0.30, 0.26, 1.0],             # the helical wrap of a loom
    [XFORM, 4, 0.11, 1, 1, 0.5, 0.5],           # laid on at a shallow angle
    [PERLIN, 5, 54, 3, 0.5, 0, 661, 1.0, 0],    # the rubber's own tooth
    [LEVELS, 5, 0.38, 0.74, 1.0],
    [FLAT, 1, 0.48, 0, 0],
    [ADD, 1, 4, 0.24],
    [ADD, 1, 5, 0.16],
    [COLORIZE, 0, 1, .022, .022, .024, .054, .054, .058, .098, .097, .102],
    [PERLIN, 7, 6, 3, 0.5, 0, 662, 1.0, 0],
    [LEVELS, 7, 0.64, 0.96, 1.5],
    [MIXC, 0, 7, .118, .092, .062, 0.28],       # dust settles on rubber
    [CAVITY, 0, 1, 3, 7.0, 0.44],
    [FLAT, 2, 0.86, 0, 0],                      # matte, and no spec lobe at all
    [SUB, 2, 4, 0.10],
    [FLAT, 3, 0, 0, 0],
]

# ---- 26  copper run ------------------------------------------------------
# Coolant, bus bar, hydraulic.  One warm conductor in a set of cold ones, so
# a pipe reads as a *different service* rather than as more wall.  Tarnished,
# because polished copper in a dead ship is a lie, and the tarnish is what
# gives it the green that separates it from ochre rust.
R[26] = [
    [PERLIN, 4, 34, 4, 0.5, 0, 671, 1.0, 0],    # drawn-tube grain
    [XFORM, 4, 0.25, 1, 0.30, 0.5, 0.5],
    [LEVELS, 4, 0.34, 0.76, 1.0],
    [CELL, 5, 30, 1, 2.4, 2.0, 672],            # the tarnish front
    [LEVELS, 5, 0.55, 1.0, 1.3],
    [FLAT, 1, 0.54, 0, 0],
    [ADD, 1, 4, 0.22],
    [SUB, 1, 5, 0.10],
    [COLORIZE, 0, 1, .118, .058, .030, .296, .156, .080, .482, .298, .176],
    [MIXC, 0, 5, .086, .152, .128, 0.62],       # verdigris where it has sat
    [PERLIN, 7, 8, 4, 0.5, 0, 673, 1.0, 0],
    [LEVELS, 7, 0.66, 0.96, 1.5],
    [MIXC, 0, 7, .142, .108, .070, 0.24],
    [CAVITY, 0, 1, 3, 6.0, 0.44],
    [FLAT, 2, 0.34, 0, 0],
    [ADD, 2, 5, 0.34],                          # tarnish is the matte part
    [FLAT, 3, 0, 0, 0],
]

# ---- 27  stencil mark ----------------------------------------------------
# The exception that proves the rule.  A stencil really is a picture painted
# onto a plate, so it is allowed to be one here -- and it is the only
# material in the set unwrapped in the PART's space (proj 3) rather than the
# world's, which is why the picture lands square on the plate instead of
# being a random crop out of a world grid.
#
# It is also the kit's only writing, and writing is what tells a player the
# ship was built by somebody: a bay-number band, a block of stencilled bars
# under it, and an ochre edge stripe.
R[27] = [
    [FLAT, 4, 0, 0, 0],
    # the bay-number band: one bar, then arrayed four across
    [GLOWR, 4, 0.115, 0.315, 0.010, 0.010, 0.026, 0.115, 1, 1, 1, 1.0, 0.30],
    [XMUL, 4, 0, 1, 1, 0.302, 0.5, 3, 2],       # -> four characters
    [FLAT, 5, 0, 0, 0],
    # the stencil block under it: short bars, arrayed both ways
    [GLOWR, 5, 0.100, 0.700, 0.006, 0.006, 0.040, 0.022, 1, 1, 1, 1.0, 0.26],
    [XMUL, 5, 0, 1, 1, 0.245, 0.5, 3, 2],
    [XMUL, 5, 0, 1, 1, 0.5, 0.360, 3, 1],       # -> two lines of four
    [FLAT, 6, 0, 0, 0],                         # the plate's own edge stripe
    [GLOWR, 6, 0.5, 0.045, 0.006, 0.006, 0.46, 0.026, 1, 1, 1, 1.0, 0.24],
    [GLOWR, 6, 0.5, 0.955, 0.006, 0.006, 0.46, 0.026, 1, 1, 1, 1.0, 0.24],
    [PERLIN, 7, 40, 3, 0.5, 0, 681, 1.0, 0],    # the plate under the paint
    [LEVELS, 7, 0.36, 0.74, 1.0],
    [FLAT, 1, 0.50, 0, 0],
    [ADD, 1, 7, 0.16],
    [ADD, 1, 4, 0.055],                         # paint stands a hair proud
    [ADD, 1, 5, 0.055],
    [ADD, 1, 6, 0.070],
    [COLORIZE, 0, 1, .046, .048, .052, .104, .108, .116, .160, .164, .174],
    [MIXC, 0, 4, .780, .762, .720, 0.92],       # the number, near white
    [MIXC, 0, 5, .620, .606, .572, 0.80],       # the block, a touch dirtier
    [MIXC, 0, 6, .560, .330, .062, 0.90],       # the ochre stripe
    [PERLIN, 7, 9, 4, 0.5, 0, 682, 1.0, 0],
    [LEVELS, 7, 0.58, 0.93, 1.5],
    [MIXC, 0, 7, .118, .092, .058, 0.34],       # and it is a filthy old plate
    [CAVITY, 0, 1, 3, 6.0, 0.40],
    [FLAT, 2, 0.70, 0, 0],
    [SUB, 2, 4, 0.16],
    [SUB, 2, 6, 0.14],
    [FLAT, 3, 0, 0, 0],
]


# ==========================================================================
#  28..31  THE PLATE SET  --  pictures, unwrapped on the plate
#
#  The surface set above is right about world-projected finishes and it stays
#  right.  But it left the kit with a gap: every FACE in the references --
#  every louvre bank, every perforated pan, every access lid -- is a PICTURE,
#  and the surface set forbids pictures because a picture sliced out of a
#  world grid lands with its corners cut off.
#
#  The escape is the one thing material 27 already did and nothing else did:
#  a picture unwrapped in the PART's own square (proj 3).  There is no world
#  grid left to disagree with, so the 80 mm ceiling does not apply -- the
#  feature IS the plate, at whatever size the plate happens to be, and the
#  two can never fall out of register because they are the same rectangle.
#
#  That is what buys the look for no modelling at all.  A 560 mm deck cell is
#  already a modelled recess with a chamfer round it; giving that recess a
#  perforated pan costs one material lookup, where cutting 256 holes in it
#  costs about two thousand triangles per tile and there are ninety tiles.
#  So the rule the kit runs on now is a pair:
#
#      A FINISH IS WORLD-PROJECTED AND MAY NOT DRAW ABOVE 80 mm.
#      A PICTURE IS PART-PROJECTED AND MUST FILL EXACTLY ONE PLATE.
#
#  and the second half is load-bearing.  A picture that stops short of the
#  plate's edges reads as a decal stuck onto it, which is worse than leaving
#  the plate blank.  Every recipe below runs its bezel out to 0.47 of the
#  square and its bolts in to 0.06, which is the plate's own rim wherever
#  that plate turns out to be.
# ==========================================================================

# ---- 28  louvre bank -----------------------------------------------------
# Eight slots and a bezel.  WALL 3 models its intake with real tilted slats
# and should keep doing so -- that bank is 1.2 m of wall and its parallax is
# the whole point of it.  This is for the other twenty places a ship needs
# air and cannot afford a thousand triangles each: a 560 mm deck cell, a
# cabinet door, the return above a hatch.  At two metres they are the same.
R[28] = (
    k_field(4, 0.355, 0.028, soft=0.005, power=0.13)       # one slot...
    + k_array(4, ny=3)                                     # ...eight of them
    + k_field(5, 0.472, 0.472, soft=0.006, power=0.13)     # the bezel, less
    + k_field(7, 0.404, 0.412, soft=0.006, power=0.13)     # its own inside
    + [[SUB, 5, 7, 1]]
    + [[FLAT, 6, 0, 0, 0]]
    + [[GLOWR, 6, cx, cy, 0.007, 0.007, 0.011, 0.011, 1, 1, 1, 1.0, 0.18]
       for cx in (0.062, 0.938) for cy in (0.062, 0.938)]
    + [
        # NO TOOTH.  This bank is the deck splay of every shell room -- it
        # is the diagonal you see first walking in, it is at knee height,
        # and a perlin tooth on it was reading as hammered iron rather than
        # as a pressed louvre.  A pressing is smooth between its slots.
        [FLAT, 1, 0.52, 0, 0],
        [SUB, 1, 4, 0.42],                                 # slots, the deepest
        [ADD, 1, 5, 0.13],                                 # bezel proud of it
        [ADD, 1, 6, 0.17],                                 # bolts proud of that
        [LEVELS, 1, 0.04, 1.00, 1.0],
        # ...and brighter.  It sits below the amber line with nothing above
        # it, so at 0.24 mid it went to silhouette; this is a light alloy
        # pressing, which is what a louvre actually is.
        [COLORIZE, 0, 1, .112, .114, .118, .264, .267, .274, .412, .416, .424],
        # A SLOT IS A HOLE.  The one thing that makes a drawn louvre fail is
        # a slot that is merely darker than its plate: the eye reads a
        # painted stripe.  Take it to 0.02 and it reads as a pocket.
        [MIXC, 0, 4, .020, .020, .022, 0.92],
        [PERLIN, 7, 7, 4, 0.5, 0, 692, 1.0, 0],
        [LEVELS, 7, 0.70, 0.97, 1.6],
        [MIXC, 0, 7, .186, .152, .108, 0.14],              # dust, in the slots
        [CAVITY, 0, 1, 3, 5.0, 0.30],
        # Sharp, not soft: a bright alloy face, matte only down the slots.
        [FLAT, 2, 0.18, 0, 0],
        [ADD, 2, 4, 0.62],                                 # the pocket is matte
        [SUB, 2, 5, 0.06],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 29  perforated pan --------------------------------------------------
# The deck grate, and the part of the floor reference doing all the work:
# two of the four cells in a tile are perforated and two are solid, and that
# alternation is the only reason a deck of identical panels does not read as
# lino.  256 holes, a machined rim, four bolts.
#
# No ochre anywhere on it, on purpose.  A pan is welded in.  Ochre means
# "this comes off", and putting it on something fixed is exactly how an
# accent stops meaning anything at all.
R[29] = (
    k_field(5, 0.400, 0.400, soft=0.014, power=0.16)       # the pan floor
    + [[FLAT, 4, 0, 0, 0],
       [GLOWR, 4, 0.5, 0.5, 0.006, 0.006, 0.011, 0.011, 1, 1, 1, 1.0, 0.16]]
    + k_array(4, nx=4, ny=4)                               # 16 x 16 holes
    + [[MUL, 4, 5, 1]]                                     # only in the pan
    + k_field(6, 0.470, 0.470, soft=0.006, power=0.13)     # the rim band
    + k_field(7, 0.416, 0.416, soft=0.006, power=0.13)
    + [[SUB, 6, 7, 1]]
    + [[GLOWR, 6, cx, cy, 0.007, 0.007, 0.012, 0.012, 1, 1, 1, 1.0, 0.18]
       for cx in (0.062, 0.938) for cy in (0.062, 0.938)]
    + [
        [PERLIN, 7, 52, 3, 0.5, 0, 693, 0.08, 0],
        [FLAT, 1, 0.56, 0, 0],
        [ADD, 1, 7, 1],
        [SUB, 1, 5, 0.16],                                 # the pan is sunk
        [SUB, 1, 4, 0.44],                                 # and the holes go
        [ADD, 1, 6, 0.20],                                 # rim and bolts proud
        [LEVELS, 1, 0.04, 1.00, 1.0],
        [COLORIZE, 0, 1, .082, .083, .086, .202, .204, .209, .342, .344, .352],
        [MIXC, 0, 4, .014, .014, .016, 0.94],              # through to nothing
        [PERLIN, 7, 6, 4, 0.5, 0, 694, 1.0, 0],
        [LEVELS, 7, 0.58, 0.94, 1.5],
        [MIXC, 0, 7, .172, .136, .088, 0.34],              # a grate is filthy
        [CAVITY, 0, 1, 3, 6.5, 0.46],
        [FLAT, 2, 0.38, 0, 0],
        [ADD, 2, 4, 0.50],
        [SUB, 2, 6, 0.12],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 30  access panel ----------------------------------------------------
# The reference sheet's signature, and the piece the kit was most obviously
# missing: a light plate, a recessed field inside a machined bezel, three
# raised bars standing in the recess, six bolts, and an ochre sliver down
# each side.
#
# The sliver is the accent rule in one element.  It is PAINTED, never lit,
# and it appears only on things that come off -- so a corridor tells you
# where its lids are without a placard, and the eye gets one warm note in an
# otherwise entirely cold wall.  A lit sliver would mean power behind the
# panel, and this ship has almost none: that is what 31 is for.
R[30] = (
    k_field(4, 0.372, 0.352, soft=0.018, power=0.22)       # the recessed field
    + [  # three bars left standing in it, so the recess is a shape not a hole
        [GLOWR, 4, 0.294, 0.5, 0.011, 0.015, 0.050, 0.246, 0, 0, 0, 1.0, 0.17],
        [GLOWR, 4, 0.500, 0.5, 0.011, 0.015, 0.050, 0.246, 0, 0, 0, 1.0, 0.17],
        [GLOWR, 4, 0.706, 0.5, 0.011, 0.015, 0.050, 0.246, 0, 0, 0, 1.0, 0.17],
      ]
    + k_field(5, 0.474, 0.474, soft=0.006, power=0.13)     # the bezel band
    + k_field(7, 0.408, 0.392, soft=0.006, power=0.13)
    + [[SUB, 5, 7, 1]]
    + [[FLAT, 6, 0, 0, 0]]
    + [[GLOWR, 6, cx, cy, 0.008, 0.008, 0.013, 0.013, 1, 1, 1, 1.0, 0.18]
       for cx in (0.058, 0.500, 0.942) for cy in (0.058, 0.942)]
    + [[FLAT, 7, 0, 0, 0]]
    + [[GLOWR, 7, cx, 0.5, 0.006, 0.010, 0.009, 0.170, 1, 1, 1, 1.0, 0.16]
       for cx in (0.146, 0.854)]                           # the ochre slivers
    + [
        [FLAT, 1, 0.60, 0, 0],
        [SUB, 1, 4, 0.30],                                 # the field is sunk
        [ADD, 1, 5, 0.11],
        [ADD, 1, 6, 0.18],
        [ADD, 1, 7, 0.045],                                # paint stands proud
        [LEVELS, 1, 0.06, 1.00, 1.0],
        # A LIGHT PLATE, ON THE SAME RAMP AS EVERY OTHER PLATE.  The
        # references are grey, not black, and their separation comes from
        # recesses that are genuinely dark -- not from lids that are
        # brighter than the wall they are set into.  Authored at 0.47 first
        # time round, and three of them in a bay read as cream tiles stuck
        # on: the lid outshone the machined stile that frames it, which
        # inverts the whole point of the stile.
        [COLORIZE, 0, 1, .132, .134, .138, .318, .322, .330, .570, .574, .586],
        [MIXC, 0, 4, .076, .078, .082, 0.82],              # the recess, dark
        [MIXC, 0, 7, OCHRE[0], OCHRE[1], OCHRE[2], 0.90],  # and the one accent
        [PERLIN, 7, 5, 4, 0.5, 0, 695, 1.0, 0],
        [LEVELS, 7, 0.60, 0.94, 1.4],
        [MIXC, 0, 7, .150, .118, .078, 0.24],
        [CAVITY, 0, 1, 3, 5.5, 0.34],
        # HALF A CONDUCTOR.  At metal 0 this was a painted panel and it read
        # as one -- matte, dead, plastic against the machined stiles beside
        # it.  A ship's access lid is a coated aluminium casting: the coat
        # is thin enough that the metal underneath still steers the
        # specular, which is what makes it go bright at a glancing angle
        # and dark facing you.  0.45 in METAL, and the ramp is lifted to
        # pay for the shader's conductor rule (dAlb = albedo x 0.15 at 1.0).
        [FLAT, 2, 0.40, 0, 0],
        [ADD, 2, 4, 0.22],
        [SUB, 2, 5, 0.18],                                 # the bezel is cut
        [SUB, 2, 6, 0.22],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 31  readout ---------------------------------------------------------
# The only lit thing in the kit that is not a lamp, and the reason the accent
# rule has a third role at all: sixteen cyan segments, one amber bar and a
# keypad, behind glass, on a plate the size of a placard.
#
# It is deliberately the rarest material in the set.  One bay type in three
# carries it, so a corridor of nine bays has three live boxes in it -- enough
# that the ship reads as still powered somewhere, few enough that it reads as
# a ship that has mostly stopped.  Light every bay and the corridor becomes a
# server room; light none and there is nothing left to salvage.
R[31] = (
    [[FLAT, 4, 0, 0, 0],
     [GLOWR, 4, 0.5, 0.5, 0.006, 0.006, 0.062, 0.020, 1, 1, 1, 1.0, 0.15]]
    + k_array(4, nx=2, ny=2)                               # 4 x 4 segments...
    + [[GLOWR, 4, 0.5, 0.170, 0.006, 0.006, 0.44, 0.10, 0, 0, 0, 1.0, 0.15]]
    + [  # ...with the bottom row cleared for the amber bar and the keypad
       [FLAT, 6, 0, 0, 0],
       [GLOWR, 6, 0.226, 0.168, 0.006, 0.008, 0.108, 0.026, 1, 1, 1, 1.0, 0.15]]
    + [[FLAT, 5, 0, 0, 0],
       [GLOWR, 5, 0.5, 0.5, 0.005, 0.005, 0.013, 0.013, 1, 1, 1, 1.0, 0.16]]
    + k_array(5, nx=2, ny=1)
    + [[XFORM, 5, 0, 1, 1, 0.5 - 0.220, 0.5 - 0.332]]      # over to the keypad
    + [
        [FLAT, 7, 0, 0, 0],                                # the bezel band
        [GLOWR, 7, 0.5, 0.5, 0.006, 0.006, 0.474, 0.474, 1, 1, 1, 1.0, 0.13],
        [GLOWR, 7, 0.5, 0.5, 0.006, 0.006, 0.416, 0.408, 0, 0, 0, 1.0, 0.13],
        [FLAT, 1, 0.46, 0, 0],
        [ADD, 1, 7, 0.20],                                 # bezel proud
        [ADD, 1, 5, 0.10],                                 # keys proud
        [SUB, 1, 4, 0.05],                                 # segments recessed
        [SUB, 1, 6, 0.05],
        [LEVELS, 1, 0.06, 1.00, 1.0],
        # Glass is nearly black until it is lit -- the housing must not
        # compete with the seventeen things on it that are.
        [COLORIZE, 0, 1, .022, .023, .026, .058, .060, .066, .212, .210, .206],
        [MIXC, 0, 4, LIVE_C[0], LIVE_C[1], LIVE_C[2], 1.0],
        [MIXC, 0, 6, AMBER[0], AMBER[1], AMBER[2], 1.0],
        [MIXC, 0, 5, .076, .078, .082, 0.70],              # the keys are dead
        [PERLIN, 7, 8, 4, 0.5, 0, 696, 1.0, 0],
        [LEVELS, 7, 0.70, 0.97, 1.6],
        [MIXC, 0, 7, .120, .096, .064, 0.20],              # dust on the glass
        [CAVITY, 0, 1, 3, 5.0, 0.30],
        [FLAT, 2, 0.26, 0, 0],                             # glass is glossy
        [ADD, 2, 5, 0.42],
        [COPY, 3, 4],                                      # EMISSIVE: the
        [ADD, 3, 6, 1],                                    # segments and the
        [LEVELS, 3, 0.10, 0.92, 0.9],                      # bar, and nothing
    ])



# ---- 32  safety band -----------------------------------------------------
# The fourth and last accent role, and the only one that is not the ship's
# own colour: OCHRE means "this comes off", AMBER means "this is powered",
# CYAN means "this still runs", and SAFETY YELLOW means "mind this".  It is
# a different yellow from the ochre on purpose -- greener, harder, and it
# only ever appears diagonally striped, because that is the one pattern the
# eye reads as a warning without being told.
#
# WORLD-PROJECTED, and a deliberate exception to the 80 mm ceiling: the bars
# are 100 mm.  The ceiling exists to stop a material drawing something the
# eye mistakes for a modelled seam, and a 45 degree bar cannot be mistaken
# for one -- nothing in the deck is diagonal.  It also has to be world
# projected so a band laid across four modules is ONE band and not four
# short ones that each restart their stripe.
#
# Painted over black, in that order, which is why a worn one goes black
# rather than grey where the paint has gone.
# The first version used the STRIPE operator, which is a diagonal sine
# field and looked exactly right on paper.  On the deck it came out as one
# flat yellow bar -- and whatever the reason, the answer is not to keep
# staring at it: every other picture in this set is built out of GLOWR bars
# arrayed with Repeat, that path is proven on the louvre, the perforated
# pan and the readout, and it is what is used here.  Eight bars, arrayed,
# then the whole register turned an eighth of a turn.
R[32] = (
    k_field(4, 0.058, 0.72, soft=0.012, power=0.13)   # one upright bar...
    + k_array(4, nx=2)                                # ...four of them
    + [[XFORM, 4, 0.125, 1, 1, 0.5, 0.5]]             # turned to 45 degrees
    + [
        [PERLIN, 5, 30, 4, 0.5, 0, 701, 1.0, 0],   # the paint's own tooth
        [LEVELS, 5, 0.40, 0.78, 1.0],
        [CELL, 6, 20, 1, 2.2, 3.6, 702],           # where boots took it off
        [LEVELS, 6, 0.88, 1.0, 1.0],
        [FLAT, 1, 0.52, 0, 0],
        [ADD, 1, 5, 0.13],
        [SUB, 1, 6, 0.14],
        [COLORIZE, 0, 1, .026, .025, .024, .054, .053, .050, .096, .094, .090],
        [MIXC, 0, 4, .620, .430, .046, 0.94],      # safety yellow
        [MIXC, 0, 6, .102, .096, .084, 0.82],      # worn back to the plate
        [PERLIN, 7, 7, 4, 0.5, 0, 703, 1.0, 0],
        [LEVELS, 7, 0.58, 0.93, 1.4],
        [MIXC, 0, 7, .130, .104, .068, 0.30],      # and it is a filthy deck
        [CAVITY, 0, 1, 3, 5.5, 0.34],
        [FLAT, 2, 0.72, 0, 0],
        [SUB, 2, 4, 0.10],
        [FLAT, 3, 0, 0, 0],
    ])



# ==========================================================================
#  33..36  THE BAY SET  --  assemblies, not finishes
#
#  The kit runs on a pair of rules and both of them are about a material
#  sharing a surface with a mesh:
#
#      A FINISH IS WORLD-PROJECTED AND MAY NOT DRAW ABOVE 80 mm.
#      A PICTURE IS PART-PROJECTED AND MUST FILL EXACTLY ONE PLATE.
#
#  Neither one covers the shell.  A shell wall has NO mesh on it -- it is one
#  brush twenty metres long -- so the 80 mm ceiling is protecting it from a
#  disagreement that cannot happen, and the effect of obeying it there is a
#  wall with nothing on it but grain.  That is what the whole stern of the
#  ship looks like: `Machined panel` stretched over 600 m2, whose 540 mm
#  bonded courses were designed to be seen a bay at a time and read as
#  BRICKWORK at twenty metres.
#
#  So there is a third rule, and it is the shell's:
#
#      A BAY IS WORLD-PROJECTED, TILES ON THE MODULE, AND DRAWS THE WHOLE
#      ASSEMBLY -- because on a shell surface there is no mesh to disagree
#      with, and something has to carry the structure the mesh is not there
#      to carry.
#
#  It is safe because the two grades never share a surface: FULL cells wear
#  the finish set on kit geometry, SHELL cells wear the bay set on brushes,
#  and the module grid is the same 1.200 m for both, so a bay drawn here
#  lands on the same line a bay modelled there does.  That last part is not
#  free -- see the origin note in `deckgrid.py`.  A texture tile is the
#  module only if the module grid starts on a texture tile.
#
#  Built to six rules, the
#  ones that separate "mathematically correct" from "looks like a ship":
#
#    R1  recursive nesting, amplitude decaying  1 : 0.4 : 0.25
#    R2  bevel widths QUANTIZED -- 0.015 main, 0.009 second, 0.006 micro,
#        and nothing else, ever, because "cut by the same process" is what
#        makes a set of shapes read as one part
#    R3  fasteners only where structure is: plate corners, along a seam
#    R4  functional elements in a CLUSTER, never scattered
#    R6  density contrast -- one big empty plate against a busy corner
#
#  Tiling is §5's, and it is exact rather than approximate: the soft box wraps
#  its own distance (`dx=min(dx,1-dx)`), so a shape centred on 0 is drawn
#  half on each side of the seam and four tiles share a corner bolt.
# ==========================================================================

# the three bevels, and there are no others in this set (R2)
B1, B2, B3 = 0.015, 0.009, 0.006


def k_bay(reg, sx, sy, soft, power=0.32):
    """One tier of a nested plate: clear the register, draw the land."""
    return [[FLAT, reg, 0, 0, 0],
            [GLOWR, reg, 0.5, 0.5, soft, soft, sx, sy, 1, 1, 1, 1.0, power]]


def k_seam(reg, vertical=True, horizontal=True, w=B3):
    """The groove on the tile boundary.  Centred on 0, so it is drawn half
       in this tile and half in the next and the two halves are the same
       number -- SS5 T1, and the reason this tiles by identity."""
    out = [[FLAT, reg, 0, 0, 0]]
    if vertical:
        out.append([GLOWR, reg, 0.0, 0.5, w, w, 0.0, 0.5, 1, 1, 1, 1.0, 0.20])
    if horizontal:
        out.append([GLOWR, reg, 0.5, 0.0, w, w, 0.5, 0.0, 1, 1, 1, 1.0, 0.20])
    return out


# ---- 33  bay deck --------------------------------------------------------
# The shell floor, and the single biggest surface in the ship: 1,700 cells of
# it, which is 2,450 m2 laid as ONE flat brush of `Coated plate` per room.
# Coated plate is a paint -- 0.22 roughness, 0.22 bump, no relief at all by
# design -- so an engine room reads as a poured slab, while the deck the
# route is built on is the richest thing in the corridor.
#
# FOUR PLATES TO THE MODULE, not one, and ONE TIER EACH.  Both halves of
# that matter:
#
#   * a square inside a square is a plate drawn twice.  R1's nesting is for
#     a WALL, where you stand a metre from it and the tiers are read one
#     behind another; on a deck you look across it at 15 degrees and the
#     inner square just doubles every line.  So the deck gets one land and
#     one chamfer, and the depth it loses it takes back in seam and bolt.
#   * a 600 mm deck plate against a 1,200 mm wall plate is what tells you
#     which surface you are looking at.  Equal sizes made the room read as
#     one continuous graph paper folded up at the edges.
#
# And the rim is STAINLESS -- the chamfer round every plate is bright, hard
# and near-glossy, where the land inside it is duller.  That single contrast
# is most of what separates machined steel from forged iron: the cut edge
# catches a light the face does not.
PL = 0.212                       # plate half-extent: 4 to the module, 600 mm
R[33] = (
    [[FLAT, 4, 0, 0, 0]]
    + [[GLOWR, 4, cu, cv, B1, B1, PL, PL, 1, 1, 1, 1.0, 0.34]
       for cu in (0.25, 0.75) for cv in (0.25, 0.75)]
    + [[COPY, 7, 4], [FLAT, 6, 0, 0, 0]]
    + [[GLOWR, 6, cu, cv, B2, B2, PL - 0.021, PL - 0.021, 1, 1, 1, 1.0, 0.30]
       for cu in (0.25, 0.75) for cv in (0.25, 0.75)]
    # A RIM IS A LINE.  At 46 mm it was a light band round every plate and
    # 600 mm squares with a light band round them are floor tiles -- which
    # is what the first pass came out as.  25 mm is the chamfer itself and
    # nothing more: you see it where a light rakes across it and nowhere
    # else, which is exactly how a real cut edge behaves.
    + [[SUB, 7, 6, 1]]                        # -> reg 7: the rim, and only it
    + [
        # the seam grid: the module boundary and the cross between the four
        # plates.  Centred on 0 and on 0.5, so it tiles by identity.
        [FLAT, 5, 0, 0, 0],
        [GLOWR, 5, 0.0, 0.5, B3, B3, 0.0, 0.5, 1, 1, 1, 1.0, 0.20],
        [GLOWR, 5, 0.5, 0.0, B3, B3, 0.5, 0.0, 1, 1, 1, 1.0, 0.20],
        [GLOWR, 5, 0.5, 0.5, B3, B3, 0.0, 0.5, 1, 1, 1, 1.0, 0.20],
        [GLOWR, 5, 0.5, 0.5, B3, B3, 0.5, 0.0, 1, 1, 1, 1.0, 0.20],
        # R3: a bolt at every seam crossing and nowhere else.  Four of them,
        # each shared by the four plates that meet there.
        [FLAT, 6, 0, 0, 0],
        [GLOWR, 6, 0.0, 0.0, 0.019, 0.019, 0, 0, 1, 1, 1, 1.0, 0.30],
        [GLOWR, 6, 0.5, 0.0, 0.019, 0.019, 0, 0, 1, 1, 1, 1.0, 0.30],
        [GLOWR, 6, 0.0, 0.5, 0.019, 0.019, 0, 0, 1, 1, 1, 1.0, 0.30],
        [GLOWR, 6, 0.5, 0.5, 0.019, 0.019, 0, 0, 1, 1, 1, 1.0, 0.30],
        [FLAT, 1, 0.26, 0, 0],
        [ADD, 1, 4, 0.62],                    # the land
        [SUB, 1, 5, 0.30],                    # the seams
        [ADD, 1, 6, 0.20],                    # the bolts
        # NO NOISE IN THE HEIGHT.  A perlin tooth on a machined plate is
        # what makes it look hammered -- the normal map turns it into a
        # thousand tiny facets and the specular breaks up like a forging.
        # Whatever tooth this steel has belongs in the roughness.
        [LEVELS, 1, 0.02, 1.02, 1.0],
        # THE SHIP'S GREY IS A DARK.  0.20 mid, not 0.34 -- the light does the
        # colouring, and a deck that is already pale has nothing left for a
        # lamp to do to it.  This is the value the route's deck sits at, and
        # the two grades have to meet in a doorway.
        [COLORIZE, 0, 1, .064, .063, .061, .152, .149, .145, .252, .247, .240],
        [MIXC, 0, 7, .336, .342, .352, 0.34],   # the rim, in bright stainless
        # PLATE-TO-PLATE VARIATION.  Four plates to the tile and a two-octave
        # perlin across it, so the four are never the same tone -- which is
        # the one thing that stops a field of identical plates reading as
        # squared paper.  It repeats every 1.2 m like everything else here,
        # but it repeats at PLATE scale, and that is below the size at which
        # the eye tracks a repeat.
        [PERLIN, 6, 2, 2, 0.5, 0, 712, 1.0, 0],
        [LEVELS, 6, 0.28, 0.86, 1.0],
        [MIXC, 0, 6, .096, .094, .090, 0.48],
        [PERLIN, 5, 4, 4, 0.5, 0, 711, 1.0, 0],
        [LEVELS, 5, 0.56, 0.94, 1.3],
        [MIXC, 0, 5, .184, .156, .118, 0.16],   # grime, in the seams only
        [CAVITY, 0, 1, 3, 6.0, 0.36],
        # GLOSSY, and glossiest on the cut edge.  0.20 is a sealed deck that
        # returns the overhead lamps down its whole length; the rim goes to
        # 0.12, which is stainless; the seam is the only matte thing in it.
        [FLAT, 2, 0.20, 0, 0],
        [SUB, 2, 7, 0.08],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 34  bay plate -------------------------------------------------------
# The shell wall, below the string course.  ONE TILE IS ONE PLATE and one
# plate is one module, so a dado three modules high is three courses of
# plate and the 4U rib the shell already models lands on a plate edge
# instead of in the middle of a brick.
#
# The bolts run DOWN THE VERTICAL SEAM and along nothing else (R3).  That is
# the difference between a wall of plates and a wall of tiles: a real plate
# is fastened at its edges, so the fasteners draw the structure, and a wall
# whose bolts are evenly spread has none.
R[34] = (
    k_bay(4, 0.470, 0.474, B1, 0.34)
    # KEEP THE LAND.  reg 4 is scratch and the bolts overwrite it fifteen ops
    # from here; reg 8 is the one register a recipe may leave a value in, so
    # the plate face is parked there and the rim, the border and the metal
    # mask are all cut out of it at the end.
    + [[COPY, 8, 4]]
    + [[FLAT, 1, 0.30, 0, 0],
       [ADD, 1, 4, 0.55]]
    + k_bay(5, 0.400, 0.412, B2, 0.30)
    + [[SUB, 1, 5, 0.22]]
    + k_bay(6, 0.356, 0.372, B3, 0.28)
    + [[ADD, 1, 6, 0.14]]
    + k_seam(7, w=B3)
    + [[SUB, 1, 7, 0.28]]
    + [
        # R3: a column of bolts in the vertical seam, four to a course,
        # shared with the plate on the other side of it.
        [FLAT, 4, 0, 0, 0],
        [GLOWR, 4, 0.0, 0.125, 0.017, 0.017, 0, 0, 1, 1, 1, 1.0, 0.30],
        [GLOWR, 4, 0.0, 0.125, 0.028, 0.028, 0, 0, 1, 1, 1, 0.40, 0.16],
    ]
    + k_array(4, ny=2)                            # -> four down the seam
    + [
        [ADD, 1, 4, 0.19],
        # R4 + R6: ONE cluster on the plate and the rest of it bare.  A pair
        # of lifting lugs at the top edge, which is where a plate this size
        # is actually picked up -- and which puts the busy part of the tile
        # against the course seam, where the bolts already are.
        [FLAT, 5, 0, 0, 0],
        [GLOWR, 5, 0.290, 0.845, 0.010, 0.010, 0.048, 0.020, 1, 1, 1, 1.0, 0.26],
        [GLOWR, 5, 0.710, 0.845, 0.010, 0.010, 0.048, 0.020, 1, 1, 1, 1.0, 0.26],
        [ADD, 1, 5, 0.16],
        [FLAT, 6, 0, 0, 0],
        [GLOWR, 6, 0.290, 0.845, 0.007, 0.007, 0.018, 0.005, 1, 1, 1, 1.0, 0.20],
        [GLOWR, 6, 0.710, 0.845, 0.007, 0.007, 0.018, 0.005, 1, 1, 1, 1.0, 0.20],
        [SUB, 1, 6, 0.13],
        # NO NOISE IN THE HEIGHT.  The plate had a fine perlin tooth on it
        # and that one line was most of what made this steel look forged:
        # a normal map built from noise gives every square millimetre its
        # own facet, the specular stops being a highlight and becomes a
        # sparkle, and a bulkhead reads as hammered iron.  A rolled plate
        # is FLAT.  What it has instead is a rim -- the chamfer round the
        # land, in bright stainless, glossier than the face inside it --
        # and that is the whole difference between machined and beaten.
        # (The tooth it does want lives in the roughness, at the bottom of
        # this recipe, where it cannot become geometry.)
        [LEVELS, 1, 0.03, 1.02, 1.0],
        [COLORIZE, 0, 1, .060, .059, .058, .144, .142, .140, .244, .241, .238],
        # THE RIM IS THE LAND MINUS A SMALLER LAND -- and it has to be the
        # LAND.  This read `COPY 7, 4`, and reg 4 stopped being the land the
        # moment the bolts were drawn into it, so the stainless line and the
        # gloss that goes with it have been landing on four bolt heads and
        # nothing else.  The chamfer round a 1.2 m plate is the brightest
        # thing on it; it was simply never drawn.
        [COPY, 7, 8],
        [FLAT, 6, 0, 0, 0],
        [GLOWR, 6, 0.5, 0.5, B2, B2, 0.446, 0.450, 1, 1, 1, 1.0, 0.30],
        [SUB, 7, 6, 1],                           # -> the rim of the land
        [MIXC, 0, 7, .520, .536, .560, 0.44],     # ...in stainless, as a LINE
        # THE DARK BORDER IS THE OTHER HALF OF THE MATERIAL.  What lies
        # outside the land is not plate at all: it is the structure the
        # plate is bolted to, seen down a 6 mm gap, and it has never been
        # painted.  A coated face against a BARE STEEL surround is most of
        # what tells you the face is a separate, removable part -- so the
        # border takes its own metal and its own gloss below, and the eye
        # separates the two without a triangle being spent on it.
        [FLAT, 6, 1, 1, 1],
        [SUB, 6, 8, 1],                           # -> reg 6: 1 - land
        [PERLIN, 5, 3, 4, 0.5, 0, 722, 1.0, 0],
        [LEVELS, 5, 0.62, 0.94, 1.4],
        [MIXC, 0, 5, .176, .146, .104, 0.14],     # grime, and not much of it
        [CAVITY, 0, 1, 3, 5.5, 0.34],
        # ...AND THE BORDER IS LAID ON AFTER THE CAVITY PASS, because for a
        # conductor the albedo IS the F0 -- the colour it reflects -- and
        # darkening it in the recess does not give you dark steel, it gives
        # you no steel at all.  Bare steel is a BRIGHT 0.46 here and still
        # reads black: a conductor has no diffuse lobe, so what you see in
        # it is the room, and in a dead ship there is almost no room to see.
        # Put a lamp near it and the same texel goes white.
        [MIXC, 0, 6, .450, .466, .496, 0.66],
        # ---- gloss --------------------------------------------------------
        # 0.20 -> 0.10.  A coated plate that RETURNS the lamp above it rather
        # than smearing it: the highlight is a shape with an edge, which is
        # what a sealed ship has and a workshop does not.
        [FLAT, 2, 0.10, 0, 0],
        # ...ON A PLASTIC FLOOR.  With the detail normal gone (see splice.py
        # 8e) the face has nothing left to break its highlight, and an
        # unbroken highlight is the single clearest tell of a CG surface.
        # The break-up goes in the ROUGHNESS and never in the height -- that
        # is the difference between a coating's orange peel and a hammered
        # tooth.  Fine, and +0.00 to +0.06, so the sheen crawls slightly as
        # you move and never sparkles.
        [PERLIN, 5, 34, 2, 0.5, 0, 733, 1.0, 0],
        [LEVELS, 5, 0.36, 0.68, 1.0],
        [ADD, 2, 5, 0.07],
        [SUB, 2, 4, 0.04],                        # bolt heads are bare steel
        [SUB, 2, 7, 0.05],                        # the cut rim, glossier still
        [ADD, 2, 6, 0.12],                        # the border, a shade duller
        # ---- metal --------------------------------------------------------
        # The METAL table says 0 for this material and the FACE keeps it:
        # paint is a dielectric however new it is.  The border does not.  It
        # is the one part of the tile that is bare, so it goes to 0.90 and
        # takes a conductor's tinted, diffuse-free highlight -- which is why
        # a shadow gap in a real ship is darker AND brighter than the plate
        # beside it at the same time.  Everything else in the set leaves
        # reg 8 at zero and is unchanged.
        [COPY, 8, 6],
        [MULC, 8, 0.90, 0.90, 0.90],
        # The cut rim is bare too -- more so than the border, because it is
        # where the tool went through.  0.70 rather than 0.90 so it keeps a
        # little diffuse and does not vanish when no lamp is on it.  The two
        # masks never overlap: the rim is inside the land, the border is
        # outside it, so they simply add.
        [ADD, 8, 7, 0.70],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 35  bay hull --------------------------------------------------------
# Above the string course, where the route puts plain hull and the shell put
# `Coated plate` -- which came out as a band of pale grey the height of a
# room and read as plaster.  It is meant to be the QUIET half of the wall,
# so it keeps the module seam and the rolling grain and gives up everything
# else: no bolts on the face, no lugs, no second nesting tier.  R6 works
# across surfaces too, and this is the empty one.
R[35] = (
    k_seam(4, w=B2)
    + [
        [PERLIN, 5, 7, 5, 0.55, 0, 731, 1.0, 0],   # mill scale, no edges
        [LEVELS, 5, 0.34, 0.74, 1.1],
        [PERLIN, 6, 40, 3, 0.5, 1, 732, 0.5, 0],   # the rolling grain
        [XFORM, 6, 0.0, 0.24, 1, 0.5, 0.5],        # drawn out along the roll
        [FLAT, 1, 0.44, 0, 0],
        [ADD, 1, 5, 0.085],
        [ADD, 1, 6, 0.038],
        [SUB, 1, 4, 0.24],                         # the seam, and only it
        [LEVELS, 1, 0.04, 1.0, 1.0],
        # darker than the coat it replaces: this band is BEHIND everything,
        # and a bright far wall is what flattens a room.
        [COLORIZE, 0, 1, .050, .049, .048, .124, .122, .118, .214, .210, .202],
        [PERLIN, 7, 4, 4, 0.5, 0, 733, 1.0, 0],
        [LEVELS, 7, 0.52, 0.90, 1.3],
        [MIXC, 0, 7, .186, .148, .100, 0.30],
        [CAVITY, 0, 1, 3, 6.0, 0.40],
        [FLAT, 2, 0.52, 0, 0],
        [ADD, 2, 4, 0.22],
        [FLAT, 3, 0, 0, 0],
    ])

# ---- 36  bay overhead ----------------------------------------------------
# The shell ceiling was `Oiled deck` -- a FLOOR material, face down, which is
# why every room overhead reads as a dark tiled grid with no structure in it
# at all.  This is the overhead the corridor has, drawn instead of modelled:
# a structural beam on the module line, its bolts, and a return-air band let
# into the pan between beams.
#
# R[2]'s lesson holds and is the reason there is only ONE beam direction
# here: a square grid overhead fights the square grid on the walls and the
# room reads as graph paper.  The beam runs across the tile and nothing runs
# along it, so the overhead spans and the walls keep their own grain.
R[36] = (
    k_seam(4, horizontal=False, w=B1)              # -> the beam's own edges
    + [
        [FLAT, 5, 0, 0, 0],                        # the beam web
        [GLOWR, 5, 0.0, 0.5, B1, B1, 0.052, 0.5, 1, 1, 1, 1.0, 0.34],
        [FLAT, 6, 0, 0, 0],                        # bolts along it (R3)
        [GLOWR, 6, 0.0, 0.125, 0.014, 0.014, 0, 0, 1, 1, 1, 1.0, 0.30],
    ]
    + k_array(6, ny=2)
    + [
        [FLAT, 1, 0.34, 0, 0],
        [ADD, 1, 5, 0.44],                         # the beam, proud
        [ADD, 1, 6, 0.20],
        [SUB, 1, 4, 0.10],
    ]
    # R4: the return-air louvres, in a band and not scattered.  Confined to
    # the middle of the pan so the beam's flange stays clear of them.
    + [
        [SCAN, 7, 12, 0.40, 0.12, 1.0],
        [FLAT, 4, 0, 0, 0],
        [GLOWR, 4, 0.5, 0.5, 0.012, 0.012, 0.300, 0.190, 1, 1, 1, 1.0, 0.24],
        [MUL, 7, 4, 1],
        [SUB, 1, 7, 0.26],
        [FLAT, 4, 0, 0, 0],                        # the bezel round the band
        [GLOWR, 4, 0.5, 0.5, 0.010, 0.010, 0.330, 0.220, 1, 1, 1, 1.0, 0.24],
        [FLAT, 5, 0, 0, 0],
        [GLOWR, 5, 0.5, 0.5, 0.010, 0.010, 0.300, 0.190, 1, 1, 1, 1.0, 0.24],
        [SUB, 4, 5, 1],
        [ADD, 1, 4, 0.12],
        [LEVELS, 1, 0.02, 1.04, 1.0],
        # An overhead is the darkest surface in a room and stays that way:
        # it is above every lamp in the ship, so whatever it returns is
        # bounce, and a bright ceiling is the fastest way to lose a corridor.
        [COLORIZE, 0, 1, .036, .036, .037, .098, .097, .097, .180, .178, .174],
        [PERLIN, 7, 5, 4, 0.5, 0, 741, 1.0, 0],
        [LEVELS, 7, 0.56, 0.94, 1.4],
        [MIXC, 0, 7, .128, .104, .072, 0.28],
        [CAVITY, 0, 1, 3, 8.0, 0.66],
        [FLAT, 2, 0.62, 0, 0],
        [SUB, 2, 5, 0.16],
        [FLAT, 3, 0, 0, 0],
    ])


# ==========================================================================
#  37..39  THE VIEW SET  --  a pane, and two layers a long way behind it
#
#  The first cut of this put the stars in the glass, because the renderer had
#  no transparency and a window therefore could not be a hole.  It was wrong
#  and it was obvious: a sky painted on the pane sits at 60 mm, so it does not
#  move against the frame as you walk, and a sky that does not move is a
#  poster.  Everything the window does depends on PARALLAX and parallax needs
#  distance, so the renderer grew a transparent pass (see `SURF`) and the
#  window grew a hole, a void chamber and two layers to fill it:
#
#     0.06 m   the PANE          39, real glass, blended, Fresnel alpha
#     4.8 m    the PLANET        38, one sprite, one picture, blended
#     7.2 m    the CHAMBER       37, a tiling starfield on five plates
#
#  Two layers and not one because a single backdrop has no depth in itself:
#  the sprite and the chamber move against each other at different rates, and
#  that difference is what the eye reads as "far away" rather than "flat".
#  It is also what breaks the chamber's tiling -- 37 repeats every 1.2 m and
#  would show its lattice against nothing, but it never appears against
#  nothing, only behind a nebula and forty brighter stars that repeat never.
# ==========================================================================

# ---- 37  viewport starfield ----------------------------------------------
# The back of the void chamber, world-projected like every other finish, so
# it tiles on the module and its texel density is the ship's.  That fixes the
# star SIZE, which is the whole problem with a starfield in a texture: a tile
# is 1.2 m and a texel is 4.7 mm, so at the 7.2 m the chamber is deep a star
# three texels across subtends about a twentieth of a degree -- one or two
# pixels, which is what a star is.  Unwrap the same picture over the whole
# 8.4 m plate instead and every star comes out 150 mm wide and the window
# looks out onto a dot screen.
#
# Nothing large is allowed in it, for the same reason: a nebula at this scale
# would repeat every 1.2 m.  The nebula is on the sprite, where it lands once.
R[37] = (
    # ~20 a tile you would name ...
    k_stars(4, 5, 6, 12, 861, keep=0.950, lo=0.860, hi=1.0, gamma=1.7)
    # ... and ~50 you would not, at half the size and a third the light
    + k_stars(5, 6, 7, 26, 862, keep=0.918, lo=0.928, hi=1.0, gamma=1.3)
    + [
        [ADD, 4, 5, 0.38],
        [LEVELS, 4, 0.0, 1.0, 1.0],

        # Space is not black.  0.004 is four hundred times darker than the
        # frame around it, which is all the contrast this needs, and it keeps
        # the window from reading as a hole cut in the screen.
        [FLAT, 0, .0030, .0036, .0052],
        [MIXC, 0, 4, .94, .95, 1.00, 1.0],

        # Colour, from the same cells and the same seed, so a warm star IS a
        # star and not a stain that happens to lie near one.  A fifth old and
        # amber, a seventh young and blue: any more and it reads as confetti.
        [CELL, 7, 12, 2, 1.0, 1.0, 861],
        [LEVELS, 7, 0.880, 0.905, 1.0],
        [MUL, 7, 4, 1],
        [MIXC, 0, 7, 1.00, .68, .42, 0.90],
        [CELL, 7, 12, 2, 1.0, 1.0, 861],
        [LEVELS, 7, 0.975, 0.995, 1.0],
        [MUL, 7, 4, 1],
        [MIXC, 0, 7, .62, .74, 1.00, 0.85],

        [COPY, 3, 4],                       # EMISSIVE: the stars, and nothing
        [LEVELS, 3, 0.0, 0.92, 0.9],
        [FLAT, 1, 0.5, 0, 0],               # no relief -- it is a sky
        [FLAT, 2, 1.0, 0, 0],
    ])


# ---- 38  viewport planet -------------------------------------------------
# One sprite, 4.8 m square, hanging 4.8 m outside the hull: the planet, a
# nebula, and the forty brightest stars.  It is SURF 3, so its coverage is
# read out of the roughness channel and everything the picture leaves empty
# is genuinely empty -- the chamber's starfield shows straight through it.
#
# The terminator is a CIRCLE, not a ramp.  A lit sphere's day side is the
# intersection of its own disc with a second disc offset toward the star, so
# that is exactly how it is drawn: two soft-box discs and a multiply.  A
# linear gradient gives a straight terminator, and a straight terminator
# turns a sphere back into a coin.
R[38] = ([
        # ---- the nebula ---------------------------------------------------
        # A cloud times a RIDGED noise, never a cloud on its own.  fbm alone
        # is a bruise -- soft everywhere, edged nowhere -- and a bruise in a
        # window is a dirty window.  Ridged noise has filaments in it, and the
        # product keeps the big shape while gaining the thread structure that
        # reads as gas instead of smudge.
        [PERLIN, 7, 3, 5, 0.58, 0, 873, 1.0, 0],
        [XFORM, 7, 0.08, 2.6, 0.62, 0.5, 0.5],   # stretched: a lane, not a blob
        [LEVELS, 7, 0.52, 0.88, 1.6],            # -> where there is gas at all
        [PERLIN, 6, 15, 4, 0.5, 1, 874, 1.0, 0], # ridged and fine: filaments
        [LEVELS, 6, 0.10, 0.72, 1.0],            # -> texture, not a second mask
        [MUL, 7, 6, 1],
        [BLUR, 7, 1, 1],

        # ---- the disc -----------------------------------------------------
        # Off centre and off the vertical, because a planet centred in a
        # window is a logo.  pw 0.02 makes the soft box's falloff a hard edge: a
        # planet has a LIMB, and a soft-edged disc is a lens flare.
        [FLAT, 5, 0, 0, 0],
        [GLOWR, 5, 0.44, 0.54, 0.235, 0.235, 0, 0, 1, 1, 1, 1.0, 0.020],

        # no stars and no gas in front of it
        [FLAT, 6, 1, 1, 1],
        [SUB, 6, 5, 1],
        [MUL, 7, 6, 1],                               # no gas in front of it

        # ---- the day side, and its curved terminator ----------------------
        [FLAT, 6, 0, 0, 0],
        [GLOWR, 6, 0.55, 0.64, 0.228, 0.228, 0, 0, 1, 1, 1, 1.0, 0.150],
        [MUL, 6, 5, 1],
        [LEVELS, 6, 0.02, 0.98, 1.0],

        # ---- albedo -------------------------------------------------------
        [FLAT, 0, 0, 0, 0],
        [MIXC, 0, 7, .036, .050, .098, 1.0],          # the nebula, cold
        # The night side is NOT black.  A planet's dark limb against space is
        # the one edge the eye judges its size by, and it can only do that if
        # the disc is a shade lighter than the sky behind it.
        [MIXC, 0, 5, .019, .018, .024, 1.0],
        [MIXC, 0, 6, .350, .292, .228, 1.0],          # the day side
    ]
    + [
        # ---- the banding, on the lit side ---------------------------------
        # Scanlines displaced by a noise stretched 3:1 along them.  Straight
        # bands read as a beach ball; bands that wander by a few per cent of
        # their own width read as weather.
        [SCAN, 2, 11, 0.10, 0.28, 1.0],
        # R4, NOT R5.  R5 is the disc and it is still needed thirty ops from
        # here for the coverage channel; writing the band noise over it is
        # what made the sprite opaque everywhere the first time round.
        [PERLIN, 4, 7, 4, 0.55, 0, 875, 1.0, 0],
        [XFORM, 4, 0, 3.2, 0.55, 0.5, 0.5],
        [DISTORT, 2, 4, 0.13],
        [LEVELS, 2, 0.30, 0.82, 1.0],
        [BLUR, 2, 2, 1],
        [MUL, 2, 6, 1],
        [MIXC, 0, 2, .196, .150, .108, 0.92],
    ]
    + [
        # ---- emissive -----------------------------------------------------
        # Nothing in a dead corridor lights a planet, so the sky lights
        # itself: SURF 2 and 3 skip the bake entirely and the shader returns
        # albedo * (1 + emissive * 3.4).
        [FLAT, 3, 0, 0, 0],
        [ADD, 3, 6, 0.22],
        [ADD, 3, 7, 0.22],

        # ---- the atmosphere -----------------------------------------------
        # An annulus four per cent of the disc wide, faded round toward the
        # dark limb.  Seven ops, and the difference between a sphere and a
        # hole cut in the sky.
        [FLAT, 6, 0, 0, 0],
        [GLOWR, 6, 0.44, 0.54, 0.2560, 0.2560, 0, 0, 1, 1, 1, 1.0, 0.300],
        [SUB, 6, 5, 1],
        [FLAT, 4, 0, 0, 0],
        [GLOWR, 4, 0.55, 0.64, 0.280, 0.280, 0, 0, 1, 1, 1, 1.0, 0.200],
        [MUL, 6, 4, 1],
        [MIXC, 0, 6, .420, .560, .840, 0.45],
        [ADD, 3, 6, 0.28],
        [LEVELS, 3, 0.0, 0.95, 0.9],

        # ---- COVERAGE, in the roughness channel ---------------------------
        # SURF 3 reads alpha out of here, so this register is the sprite's
        # matte and nothing else: the disc solid, the rim solid, the gas
        # partial, the stars themselves.  Everything it leaves at zero is a
        # hole the chamber's own starfield shows through -- which is the
        # entire reason the sprite can hang in mid-air without a black
        # rectangle around it.
        [COPY, 2, 5],                                 # the disc, solid
        [ADD, 2, 6, 1.0],                             # the atmosphere, solid
        [ADD, 2, 7, 0.17],                            # the gas, barely
        [LEVELS, 2, 0.0, 1.0, 1.0],
        [FLAT, 1, 0.5, 0, 0],
    ])


# ---- 39  viewport glass --------------------------------------------------
# And the pane, which is now only a pane: no sky on it, nothing behind it,
# 48 mm of soda-lime with a hard vacuum on one side.  Almost all of what
# makes it read is in two numbers the shader uses and not in the picture at
# all -- METAL 0 and roughness 0.042 -- so that the Fresnel term runs from
# 0.04 head on to about 0.95 at a grazing angle.  Stand square to it and you
# see the stars; walk past and it rolls up into a mirror of the corridor.
# That is the material.  The picture's whole job is to say the pane is not
# perfect: the salt haze a hundred airlock cycles leave, the fine scratching
# of everything that has ever been wiped across it, and the runs where
# something wet dried on the inside.
R[39] = (
    k_glass(5, 6, 881)
    + [
        # 48 mm of glass on edge is green.  Head on it is almost nothing --
        # 0.012 -- but it is not zero, and it is what stops the pane going
        # to a pure specular and reading as a hole with a highlight on it.
        [FLAT, 0, .0100, .0132, .0118],
        [MIXC, 0, 5, .118, .126, .140, 0.34],       # the dirt sits ON it

        # runs.  Stretched 6:1, so they fall.
        [PERLIN, 6, 16, 3, 0.5, 1, 883, 1.0, 0],
        [XFORM, 6, 0, 1, 0.16, 0.5, 0.5],
        [LEVELS, 6, 0.82, 1.00, 1.0],
        [MIXC, 0, 6, .096, .104, .112, 0.32],
        [ADD, 5, 6, 0.45],

        # ROUGHNESS IS THE MATERIAL, and the dirt is the only thing on the
        # pane that is not glass, so it is the only thing allowed to be
        # rough.  The shader also reads the height of this channel above
        # 0.042 as the pane's OPACITY, which is why a filthy window stops
        # being a window.
        [FLAT, 2, 0.042, 0, 0],
        [ADD, 2, 5, 0.115],

        [PERLIN, 1, 4, 3, 0.5, 0, 884, 1.0, 0],
        [LEVELS, 1, 0.40, 0.62, 1.0],
        [ADD, 1, 5, 0.16],
        [FLAT, 3, 0, 0, 0],
    ])


OPNAME = ['FLAT', 'PERLIN', 'CELL', 'GRAD', 'RECTS', 'GLOW', 'SCAN', 'STRIPE',
          'BLUR', 'DISTORT', 'ADD', 'SUB', 'MUL', 'BLEND', 'MASK', 'LEVELS',
          'HSCB', 'COLORIZE', 'CAVITY', 'NORMALS', 'COPY', 'MULC', 'MIXC', 'SHARP',
          'GLOWR', 'XFORM', 'XMUL']


def js():
    out = ["const RECIPES=[];"]
    for i, prog in enumerate(R):
        rows = ",\n  ".join(
            "[OP." + OPNAME[ins[0]] + "," + ",".join(_fmt(v) for v in ins[1:]) + "]"
            for ins in prog)
        out.append("RECIPES[%d]=[ // %s\n  %s\n];" % (i, NAMES[i], rows))
    return "\n".join(out)


def names_js():
    return "const MAT_NAME=[%s];" % ",".join("'%s'" % n for n in NAMES)


def bump_js():
    return "const BUMP_ST=[%s];" % ",".join(str(v) for v in BUMP)


def metal_js():
    return "const METAL=[%s];" % ",".join(str(v) for v in METAL)


def surf_js():
    return "const SURF=[%s];" % ",".join(str(v) for v in SURF)


def _fmt(v):
    if isinstance(v, float):
        return ('%g' % v)
    return str(v)
