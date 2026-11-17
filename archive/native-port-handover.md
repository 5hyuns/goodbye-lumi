# The native port — what is done, what is not

`native/build/playlumi.exe` is the contest deliverable. `src/derelict/map.html`
stays the previz. This file is the handover: what crossed over, what did not,
and where the next session should start.

Build: `sh native/build.sh` (add `debug` for `-O0 -g`). The compiler is
**w64devkit at `C:\w64devkit`** — downloaded, not installed, not on PATH;
`build.sh` adds it itself.

Regenerate the plan after any change to the Python chain:
`python tools/nativepack.py` → `native/src/plan_data.h`.

## Measured, not asserted

Re-measured 2026-09-03 on the working tree (the two passes below are in it
and are **not committed**). `-shot -log` for the boot block, `build.sh` for
the size, `-bench 600` for the frame. Anything not re-measured here is marked.

```
playlumi.exe          1,035,264 bytes   (439,296 left of 1,474,560)
imports                 GDI32 KERNEL32 msvcrt OPENGL32 USER32 WINMM — all OS
boot, full quality      ~5.2 s  (the previz needs 257 s at ?fast)
  materials             40 layers, 847 operators, 16 threads      0.181 s
  kit                   1,364 of 1,365 modules, 2.27 M vertices   0.100 s
  radiosity bake        1,143,395 samples x 12 rays, 16 threads    4.55 s
                        lit 546,500 / 1,143,395 (47.8%), ao mean 0.397
  reflection probes     21 zones x 6 levels at 66^2, traced + GGX  (see -probe)
  weapons               3 guns, 17,022 triangles, 9 x 512^2       0.033 s
  lumi                  35 parts, 9,627 verts, 14,772 triangles   0.003 s
                        as 13 material ranges (spec 35/9627/14772)
  navigation            25,965 nodes, 196,007 links               0.272 s
  mesh                  901,416 triangles, 237.3 MB in one VBO
  fx                    1,400 particles, 220 decals, 72 debris
  story                 31 volumes, 35 doors, 24 items,
                        51 lines x2 languages, 14 encounters
  doors                 34 leaves, 34 bound, 0 unmatched, worst 5.34 m
  props                 211 placed, 191 solid, 22 taken back off the routes
frame                   795 fps / 1.26 ms at 1600x900, 4x MSAA, HDR, bloom
                        (measured in the crew quarters, props in view;
                         844 fps / 1.18 ms before the props.  LUMI's real
                         mesh cost 2 fps, which is inside the noise)
audio                   44.1 kHz, rms 991, peak 22999, no clip, no NaN
                        [not re-measured; last taken at 965607b]
```

The exe has grown 74,752 bytes since `965607b`: the story and LUMI pass, then
the playability pass — 211 room props, the title card, the eyelid wake and an
English table beside every string — and then LUMI's own frozen geometry,
which cost 17,920 of them. Content is still not the constraint: the whole
ship is 50% of the floppy, and the 738 KB left over is left over because
there is nothing that needs it.

Two things are checked with numbers rather than with a look at a screenshot.

The C material compiler reproduces the JS one to four decimals on every
channel (`playlumi.exe -mats` against `node tools/matview.js`), which also
pins ihash, the periodic noise and the cellular basis.

The C mesh builder reproduces the JS one the same way.  `playlumi.exe -vmw
-log` and `node tools/vmwcheck.js` print one table: for each of the eighteen
viewmodel meshes, the vertex count, the summed position, normal, uv, tangent,
handedness and material of every vertex, and a count of the corners
finalize() had to invent a tangent for.  All eighteen agree on every column
to four decimals.  What is left is six corners out of 51,066 whose handedness
sign falls the other way across a dot product that is zero to float
precision, on corners whose tangent frame is arbitrary by construction.

### The specular path is measured too

Three numbers stand behind the reflection, and each came from a test rather
than from taste.

The **DFG table** — the second half of Karis's split sum — is Monte-Carlo'd
at load into a 32x32 texture, 512 samples a cell, and agrees with a
65,536-sample reference to 0.0092. The four-constant analytic fit it
replaces is off by 0.18 on the same grid, which is a sixth of the reflection
missing exactly where a polished panel is meant to be brightest.

The **probe's border ring** is traced, not mirrored. Reconstructing a
fast-varying field over 400 k directions, worst error is 0.0148 with the ring
against 0.0268 clamped to the edge, and the clamped worst case lands on
v=-1.000: on the seam itself.

The **prefilter** holds energy. On a synthetic sky with one 40x lamp over a
0.2 ground, the six levels peak at 40.2 / 1.77 / 0.573 / 0.313 / 0.257 /
0.239 with the mean flat at 0.209 to four figures.

`ENV_K` in render.c's scene shader is the one knob left in the whole path.

vmwcheck.js RUNS map.html's own VMW section rather than parsing it -- it
hands the IIFE a WebGL context that remembers nothing but its buffer uploads,
for the same reason nativepack.py hands its sections to node.

And LUMI is checked the same way, for the same reason.  `props/lumi_v02/` is
a frozen design: `LUMI-SPEC.md` plus `lumi_geometry.js`, a dependency-free
generator whose `buildLumi()` returns 35 parts with every transform already
baked into the points.  §0 of the spec is one sentence -- do not design LUMI,
she is designed -- and it is aimed squarely at what the first native pass
did, which was stand thirty spheres and drums where a finished mesh already
existed.  `lumimesh.c` is that generator ported statement for statement.

`playlumi.exe -lumimesh -log` and `node tools/lumicheck.js` print one table:
each of the 35 parts by name, its vertex and triangle counts, and its
bounding box.  All 35 rows agree, character for character, and the totals
are the spec's own §6 checksum -- 35 / 9,627 / 14,772.  Counts alone would
not have been enough: a transposed rotation or a flipped sign moves no count
at all, which is exactly the class of bug `-vmw` was built to catch.

The parts are grouped by MATERIAL into thirteen primitive ranges rather than
kept as 35 solids, because a group shares one colour and one surface and the
entity pass draws a range in a single instanced call.  Thirteen ranges,
thirteen instances: the whole drone costs thirteen draws.

Two things could not be carried across, and the spec's §8 names both.  The
decals are alpha textures over the paint, so the patches take the paint
colour and the ink -- LUMI, MAINTENANCE UNIT -- is cut out of `hud.c`'s own
5x7 stencil as a 36th part, one tile per lit cell projected onto the decal's
sphere.  And the emitter's two diffusion cones are additive layers with a
vertex-colour fade, which an opaque pass with per-instance colour cannot
say; they carry the bench's 0.16 / 0.11 opacities as a ratio between three
emissives instead, and her cyan dynamic light does the rest.

## Ported

| | file |
|---|---|
| Win32 window, WGL 3.3 core, raw-input mouse, thread pool | `plat.c` |
| GL 3.3 loader, the exact call set WebGL2 used | `gl33.[ch]` |
| MATH — hash, periodic noise, worley, matrices | `m.h` |
| LEVEL — brushes, faces, lamps, zones, doors, solid grid | `level.c` |
| MATERIAL COMPILER — 27 operators, 40 recipes, parallel | `tex.c` |
| RADIOSITY BAKE — shadow trace, one bounce, HL2 basis, ambient cubes | `bake.c` |
| REFLECTION PROBES — octahedral radiance per zone, GGX prefilter | `bake.c` |
| GEOMETRY — the lattice, the draw ranges | `geom.c` |
| GL — scene shader, HDR target, MSAA resolve, bloom, filmic composite | `render.c` |
| MESH OPERATORS — n-gons, extrude, inset, creases, Catmull-Clark, orient | `mesh.[ch]` |
| THE KIT — machine and 20 parts | `kit.[ch]`, `kitparts.c`, `kitwalls.c` |
| PRIMITIVES — 13 instanced solids | `prim.[ch]` |
| ENTITIES — instance buffer, ENT shader (creature + MACHINED) | `ent.[ch]` |
| BULKHEAD DOORS — 34 of them, sensor, servo, clip, emitter | `doors.c` |
| PROCEDURAL AUDIO — voices, biquads, envelopes, plate reverb, waveOut | `audio.c` |
| HUD — quad batcher and a 5x7 procedural font | `hud.[ch]` |
| DUST and CORONAS | `atmos.c` |
| PROCEDURAL WEAPONS — the mesh builder, three guns, the GPU textures, the PBR shader, the animation, the brass and the flash | `vmw.[ch]` |
| PARTICLES, DECALS, DEBRIS — the two instanced passes, the crater shader, the dynamic light pool | `fx.[ch]` |
| NAVIGATION, THE CREATURE GRAPH, THE STALKER, THE AI | `zom.[ch]` |
| LUMI — five position modes, eleven display faces, the lamp, the bank | `lumi.[ch]` |
| LUMI's SHAPE — `props/lumi_v02/lumi_geometry.js`, 1:1: 35 parts, 9,627 verts, 14,772 tris | `lumimesh.[ch]` |
| LUMI's DISPLAY — the bench's 512×290 canvas as a fragment shader on visor_screen; SDFs for the shapes, the 5x7 stencil for the text, the scene's own bloom for `shadowBlur` | `face.[ch]` |
| THE DIRECTOR — flags, events, volumes, subtitles, waves | `story.[ch]` |
| HANGUL — 14 consonants, 14 vowel parts, a layout rule | `kfont.[ch]` |
| PLAYER, INPUT, BUILD, FRAME | `game.c` |
| self-screenshot as PNG, written by hand | `shot.c` |

Two files are **not ports of anything**, because the previz has no
equivalent to port:

| | file |
|---|---|
| THE TITLE CARD, THE EYELID WAKE and the one flag every string answers to | `menu.[ch]` |
| ROOM PROPS — the design of `props/props_v01`, solved onto the deck | `props.[ch]` |

## The story

`story/` holds the design — 51 lines of Korean dialogue, fourteen encounters
and their waves, the item and door tables — and `tools/storypack.py` turns
it into `native/src/story_data.h` the same way `nativepack.py` turns the
deck plan into `plan_data.h`, and for the same reason. What stays as code
in `story.c` is the part that is genuinely logic: the order the opening
happens in, what a checkpoint restores, how a wave decides it is over, and
the ninety seconds at the end.

Spec 18 asks for five systems rather than a script, and that is what is
there: flags, an event bus, trigger volumes, a subtitle player and a spawn
director. The arc runs S0 to S12 — you wake at ten per cent, she opens the
bunkroom door, you fetch three energy cores from the engine room, the
maintenance store and the cargo hold, the ship goes over further with each
one, the third is cracked and stops at 51%, she docks her own core, and the
last ninety seconds are fought without her light, her readout or her doors.

## The legibility pass — in the tree, not yet committed

The arc ran end to end before this; what it did not do was **tell you what to
do next**, and a story you cannot follow is the same as a story that is not
there. Everything here answers one question — how does the player know? — and
all of it is uncommitted at the time of writing.

- **She leads.** `LM_LEAD` used to hold a point near the player. `lumiLeadTo()`
  now takes a point ON the route, sampled four metres ahead node by node with
  `zomStepTo()` — the same lattice the animals walk, run forwards instead of
  back. Off the lattice she balloons rather than lying about a direction.
  `story.c` §11b `leadStep()` decides between leading and coming to your
  shoulder with hysteresis (leave at 6 m, return at 9 m) because a companion
  that flickers between two stations reads as broken.
- **A ring on the deck.** The objective is drawn as a dashed lit ring at
  `GOAL_R` = 1.70 m, and standing in it for half a second fires the event.
  This is the backstop for `the-deck-plan-is-not-the-ship`: every other way
  the story advances is a room rectangle from the SVG, and a rectangle can
  miss the floor that actually got built. `QUESTS`' fourth column in
  `storypack.py` is that event, filled in **only for movement objectives** —
  a pack you have not picked up is still a pack you have not picked up.
- **She stands in the ring.** When the objective is a thing rather than a
  place (`goalItem`), she hovers over it inside `HOVER_R` = 11 m instead of
  returning to your shoulder. A ring is a place; a ring with the only moving
  light in the ship hanging in it is an instruction.
- **A pickup is a step of the story.** The emergency kit and the pistol were
  things lying about that she happened to mention; they are the two objects
  the first ten minutes cannot be played without. Each now has a banner, a
  compass bearing and a ring — and the pistol's step is what `E-01`'s
  `flag_has_pistol` gate always assumed, so the objectives and the encounter
  design finally agree.
- **The two things a player cannot work out alone** get named once, off the
  end of `L-008`/`L-009`: the torch is on `F` (`hint_light`, `L-048`/`L-049`)
  and the compass and ring mean what they look like (`hint_marker`,
  `L-050`/`L-051`). 47 lines became 51.
- **A held line is not held forever.** Spec 3 holds dialogue during a fight,
  which is right; holding it *until the fight ends* is a bet that the fight
  ends. Two of E-01's animals sat wedged at 10.7 m for seven hundred seconds
  and took the gallery briefing with them — and the briefing sets
  `gallery_briefed`, which unlocks D-22, so the run stopped on a line that
  was never going to play. `DLG_HOLD_MAX` = 12 s: a subtitle over a fight is
  worse than a subtitle and better than a story that has stopped.
- **A checkpoint restores what you were holding.** It did not before, so a
  death on a long carry sent you back for a pack you already had. The three
  carries each take one at the pickup.
- **A door says whether it will let you through.** `doors.c` washes the
  nearest frame green or pulsing red. House rule 7 keeps colour to ochre,
  amber and cyan — this is a sign, not an accent, and is the exception.
- **The stair offset.** `tryStep()` used to spend the whole 46 cm step-up
  budget on a 4 cm door sill and the eye jumped with the body. The body still
  teleports; `stepOff` in `gamePlayerEye()` decays the eye onto it, on the way
  up and on the way down.

## The playability pass — in the tree, not yet committed

The legibility pass answered *how does the player know what to do*. This one
answers *and can they actually do it* — five reports from a play session, and
the first three turned out to be **one bug**.

- **The first surface under the ceiling is not the deck.** `floorAt()` traced
  down from 2.9 m and took the first hit, and what it hits over a good deal
  of this ship is a rib ring or a pipe run at 2.4–2.8 m. Four systems settle
  on that answer, and three of them broke visibly: an item stood 2.7 m up
  **outside its own 2.4 m pickup sphere**, so the rotary cannon could not be
  picked up by any player at all; the goal ring hung in the air; and Lumi,
  who flies to that height plus 1.45 m, rose to four metres — **over the
  2.62 m door head** — and crossed the ship through the ceiling. `deckNear()`
  and `deckAt()` in `level.c` walk the whole column and keep only surfaces
  with 1.55 m of clear space above, nearest the deck the caller names.
- **The nav lattice is directed, and the obvious test asks the wrong way
  round.** Links are stored as incoming edges, so `solveField` answers "what
  does it cost to reach *this* node from everywhere else" — the flow field
  the animals want, and the reverse of what a destination has to know. The
  escape gallery has a step that can be walked down and not up, so a point
  the whole ship can reach can itself reach nothing. `-auto` ground against
  the gallery's south wall for four minutes walking at a shotgun nine metres
  away. `zomRouteExists(from,to)` solves it the right way round; every quest
  target and the three finale landmarks are checked with it at boot. **Two
  were wrong**: the capsule objective by 1.50 m and Lumi's dock by 1.40 m.
- **A weapon is its own silhouette.** All three guns drew as the same 26 cm
  crate as a medkit. `vmwDrawPickup()` draws the real viewmodel mesh in the
  world, turning, with the same program and the same nine textures — it is
  `vmwDrawShells`' pass with a different mesh and the frame's own projection,
  and it runs between the shells and the viewmodel because the viewmodel
  clears depth. Under it, a ten-piece lit ring at deck **+ 85 mm**, not +28:
  the trace answers with the top of the *brush* and the plate the player sees
  is a kit part standing on it, so a marker at brush height is inside the
  floor and draws nothing at all. The goal ring had the same bug.
- **A gun you walk past is a gun you do not have.** The shotgun and the rotary
  were props the route went straight by, which let a player reach the cargo
  hold's wave with a pistol and no shells. Each now gates the movement
  objective after it (`QUESTS` in `storypack.py`, 11 steps → 13), which is
  what `storyScene` always assumed — scene 4 hands out slot 2, scene 7 slot 3.
- **Surviving is not the same as winning.** The final defence ran its ninety
  seconds and then failed you unless you had pressed E inside 3.46 m of the
  capsule in the last twenty-five, with nothing on the overlay saying so.
  Holding the gallery for the whole ninety and standing in the lit ring at
  zero read as `THE CAPSULE LEFT WITHOUT YOU`. There is a prompt now, the
  hatch outranks a supply box at your feet, the radius is 4.6 m, and standing
  in it at T-0 boards you.
- **Props, and what "not in the way" has to mean.** `props/props_v01` is 37
  modelled props and a placement plan, and none of it was in the exe — nine
  rooms were empty boxes with a story walking through them. `props.c` carries
  29 kinds at the bench's own dimensions, built from the thirteen instanced
  solids with the MACHINED surface. **The placement is solved, not typed**:
  each room asks for so many of a kind and the placer walks a grid for spots
  that pass every test — deck, footprint, a wall behind it, 1.25 m of room in
  front, three metres off every leaf, clear of every item and landmark. A
  kind that cannot find a legal spot places fewer. Then `storyClearRoutes()`
  walks the nav route from the start to every objective **with the furniture
  in** and deletes whatever is standing on one, because every test in the
  placer is local and a room is not a corridor. 211 placed, 22 taken back.
  `-probe` prints prop footprints as `P`, so the claim is checkable.
- **A title, and eyes that open.** The game used to start with you already in
  it. `menu.c` holds the director — no `simulate`, no `storyStep` — behind a
  black card with `GOODBYE LUMI` on it, and start does not cut to the game:
  it runs a 3.2 s, four-blink eyelid open over S0's own pose, and the story's
  first frame is the frame the eyes finish on. The lids are 44 columns with
  the centre parting first, because a lid that comes down evenly is a
  letterbox and one that keeps its corners is an eye.
- **English is a table beside the Korean.** `story/dialogue.json` carries `en`
  per line and `storypack.py` emits `SL_TEXT_EN` / `SB_TEXT_EN` /
  `SQ_TEXT_EN`. The whole of the localisation is `menuPick()` and three
  accessors in `story.c`. A missing translation falls back to the Korean:
  untranslated is a small problem, blank is not. `SL_CPS` still times the
  subtitle off the Korean cell count, so the English is written to about the
  same length. **The 51 English lines are machine-written and want a read.**

Measured after: `-auto` reaches objective 7 of 13 before it loses a fight it
is not meant to win; `-scene 10 -auto` ends on `capsule_hatch_closed` rather
than on the failure; Lumi's flight height over a whole run peaks at 1.68 m
against a 2.62 m door head; three locked doors open by her panel routine.

## Not ported yet

In the order a next session should take them:

1. **THE CROWBAR and THE GRENADE** — the two engine weapons `VMW` never
   owned. They draw through `drawViewmodelBoxes`, which is not ported, so
   the native build carries three guns on 1/2/3 and no melee.
2. **SKYLIGHT 1** — the one kit part still unplaced (1 module, map.html
   10157..10313). `KIT[]` in `kitparts.c` has the hole waiting. It lands in
   the maintenance store, where `deckmap.py` cuts a 6x6 module opening for
   it — but only in the **decorative** overhead slab. `build_ship.py` lays
   an unbroken structural ceiling for the whole region at 7.2 m, so what is
   missing is a dressing feature, not a hole to the void.
3. **THE SPITTER'S PERCH and the M4 COCOON's wall mount** — both stand on
   the deck rather than clinging to it. `zomPerch` holds a body's height
   but nothing puts one on a wall.
4. **THE SINK IS A ROLL, NOT A ROTATION** — spec 7 tilts the world; this
   tilts the camera and scales movement uphill and down. Rolling 6,050
   brushes would cost the bake.
5. **THE CARGO PLATFORM HAS NO WAY UP.** `deckplan.py` lays it at 3.15 m
   and pack 3 is the one item with a deck written in its row, because that
   height is the whole reason the duct sequence exists. The three
   containers meant to be its staircase are 1.05, 2.10 and 3.15 m with
   0.12 m caps, so the risers are 1.17/1.05/1.05 m against a 0.46 m
   `STEPUP` and a 0.99 m jump apex — and they are staggered with 0.87 m
   gaps between them, so they are not a stair in plan either. Boot says so
   now: `written deck 3.15 is real at 3.66 and has NO ROUTE UP`. The pack
   falls back to the floor and the run is winnable; that fallback is what
   has been shipping. Left as-is by decision 2026-09-03. The fix is a
   stair run in `shipdetail.py` at 0.40 m risers — nav links it at
   `NAV_STEP` 0.60, the player walks it at `STEPUP` 0.46, and `settleItem`
   then moves the pack up on its own, so the boot log is the test.

Two smaller things `fx.c` left open, both waiting on something else:

- **A crater does not ride a door.** `addDecal` in the previz takes a bind
  from `adBindAt(h)` so a mark shot into a leaf slides with it and is gone
  once the leaf has retracted past it. Natively there is nothing to bind
  to: the leaves are entities, `rayTraceOne` only sees the static brush
  set, and a `dynTrace` is part of the hitscan above. Until it lands, a
  round fired at a door passes through it and craters the wall behind.
- **A shell casing has no landing sound.** `sfxCasing` is a voice `audio.c`
  does not carry, so a `ring` debris bounces silently; the brass the guns
  eject is `vmw.c`s own and never had one either.

And two the props opened:

- **An animal walks through the furniture.** `zomInit` builds the nav lattice
  over the finished brush set, and the props are placed after it — they have
  to be, because the placer asks where the doors and the items ended up. So
  `propBoxSolid` stops the player and nothing stops a scuttler. Re-linking
  the lattice against the prop boxes is the fix; it costs a second pass over
  196,007 links at boot and nobody has needed it yet.
- **A prop's SURFACE is now the bench's own, evaluated rather than
  baked.** The geometry always was 1:1 (19,094 faces, the same number
  both sides) but every region collapsed to four numbers -- albedo,
  roughness, metalness, emissive -- so a correctly-shaped container had
  no bolts, no ribs, no perforations and no hazard stripe, and read as a
  different model rather than as the same one in the dark. The bench
  bakes eleven height functions into an atlas per prop; at the texel
  density a bolt needs that is 1.6 GB over 37 props, so the exe
  evaluates the same functions PER FRAGMENT instead. `props.c` carries
  (u, v, w, h, kind, seed, param.x, param.y) on every prop vertex in
  `prim.c`'s `PRIM_R` -- a buffer parallel to `PRIM_V`, so none of the
  forty solids already in that file had to widen -- `tools/propglsl.py`
  extracts `LIB_GLSL` verbatim into `props_glsl.h`, and `ent.c` splices
  it above its own shader. Five `H()` evaluations a fragment: 723 -> 659
  fps. `playlumi.exe -prop N` puts one on a card, framed by its own size
  and lit with the bench's key, to be held beside `propview.html`.

- **A prop is not in the bake.** Same ordering: the lightmap was solved
  before any of them existed, so a container casts no shadow on the deck it
  is standing on. `sampleLight` at the prop's own middle gives it the room's
  key light, which is what the doors and the animals get, and it is why a
  prop reads as lit rather than as pasted on — but the floor under it does
  not know.

`placeProps()` in map.html clears PROPS, so the previz has never stood one
up. That used to make the props "not a gap in the port"; it does not any
more — see **The playability pass** above. What is still true is that the
bench's own meshes did not cross over: `props.c` rebuilds each prop from the
thirteen instanced solids at `propkit.js`'s dimensions, because the bench's
one-packed-atlas-per-prop machine is a second texture pipeline and this
build cannot afford one.

## The traps this port has fallen into so far

- **The kit must run before the bake grid is built.** A part's emitter is a
  real brush; build the ray grid first and it is not in it, so every lit
  channel in the ship glows and lights nothing. (In the browser the grid is
  built at script load and `buildShipModules()` appends afterwards — the
  previz has this bug.)
- **A kit part's variant is a float, and for a wall it is the bay's height
  in metres.** Rounding it collapses a 3.057 m bay and a 3.6 m one into one
  mesh; assuming 3.6 overruns anything that iterates the height.
- **A guest draw inherits the host's GL state.** The overlay was invisible
  for a build because the scene pass leaves back-face culling on and the HUD
  quads are wound for a y-down screen.
- **C does not order its arguments and JavaScript does.** Six `rng()` calls
  inside one `chamferBox(...)` argument list came out right to left under
  gcc, and the rotary cannon grew a different set of greebles from the one
  in the previz. One call per statement is the whole fix, and `-vmw` against
  `vmwcheck.js` is how it was caught -- it is invisible in a screenshot.
- **-ffast-math reassociates a sum that is meant to cancel.** `finalize()`
  knows a welded corner has no tangent of its own because the tangents of
  the faces meeting there add up to nothing. Fast-mathed, 44 of the
  double-barrel's 5,808 corners kept a residue just over the test and took a
  normalised noise direction instead of a clean axis. `vmw.c` is the one
  file compiled without the flag; `build.sh` carries the reason.
- **`rayHitDist` and its two friends are globals, and the next trace eats
  them.** `fxSurfaceHit` samples the lamps for the crater it is about to
  lay, which is a shadow ray, which is `rayTraceOne` again -- so the
  distance, axis and sign of the hit that called it are gone by the second
  line of it. Read all three into locals before doing anything else with
  the hit.
- **The viewmodel is drawn after the glass, because it clears depth.** It
  owns its own depth range so that a bulkhead at the muzzle cannot cut the
  barrel in half, which means anything drawn after it is tested against
  nothing. `render.c` offers `renderGlass()` on its own for that reason.
- **The deck plan is not the ship.** Every coordinate in `story/` is a point
  on `ref/ship_deck_plan_v6_fixed.svg`, and `build_ship.py` widens passages
  and snaps what it stands up to the 0.6 m half-module grid — so a door can
  end up five metres from the coordinate that names it, and a spawn point
  can land inside a bulkhead. Seventeen of the thirty-four doors missed a
  nearest-within-three-metres lookup; assigning each story door the nearest
  leaf **not already taken** binds all thirty-four, worst case 5.34 m.
  Spawn points and path goals get the same treatment (`freeSpot`,
  `navNear`). `playlumi.exe -probe x0,y0,x1,y1[,gx,gy]` prints the deck as
  an ASCII map, with reachability if a goal is given — that is the
  instrument, and it is cheaper than reading the geometry.
- **The story's overlay is part of the HUD's batch.** `hudEnd()` uploads what
  has been queued and then the queue is gone; anything queued after it is
  drawn by nobody, silently. The subtitles were invisible for a build.
- **The overlay is drawn after the composite, so the emergency grade misses
  it.** That is deliberate: spec 7's red is a colour grade on the scene
  (`renderSetEmergency`), because the ship's 257 lamps are baked and a real
  recolour is a re-bake — four and a half seconds in the middle of a chase.
- **A quest may only advance.** Half the banner's triggers are room entries
  and the route crosses the central lobby six times, so a plain `questOn=q`
  kept setting the objective back to one the player had already finished.
- **The flow field routes a point and the animal is a box.** `buildNav`
  decides a cell is standable from three point samples up its centre line,
  so it will link a gap narrower than the body that has to walk it. `zMove`
  sweeps the heading thirty degrees at a time until something opens, which
  is wall-following and is what an animal should look like anyway.
- **`floorAt` must start under the ceiling.** A trace from seven metres is
  above every corridor in this ship (they are 3.9 m) and finds the deckhead;
  the first wave of the final defence spawned standing on it. 2.9 m is under
  every ceiling here and over every floor.
- **...and starting under the ceiling is still not enough.** From 2.9 m the
  first hit is often a rib ring or a pipe run at 2.4–2.8 m. Three separate
  reports — a gun that could not be picked up, a marker in the air, a drone
  flying over a door head — were this one line. Ask for a surface a *person*
  could stand on (`deckNear`, 1.55 m of clear space above), not the first
  surface.
- **The visible floor is not the traced floor.** `rayTraceOne` hits brushes,
  and the plate the player walks on is a kit part standing on that brush. A
  marker laid flat at the traced height is inside the floor and draws
  nothing, with no error anywhere. +85 mm, not +28 mm.
- **The nav lattice is directed.** Links are incoming edges, so a field
  solved from a node answers "can everything reach *me*", not "can I reach
  everything". The two differ wherever a step can be walked down and not up,
  and the escape gallery has one. Test a destination with `zomRouteExists`.
- **A local placement test cannot see a route.** Every test in `props.c`'s
  placer is true of a spot and none of them is true of a corridor, so a crate
  can pass all of them and still sit in the one gap the route goes through.
  Walking the route afterwards and deleting what is on it
  (`storyClearRoutes`) is the only test that is actually the claim.

## The exe's own instruments

```
-shot out.png            one frame, photographed, then exit
-cam x,y,z,yaw,pitch     stand somewhere; y is FEET, the eye is 1.66 m above
-gun 1|2|3               pistol / double-barrel / rotary, already in hand
-fire                    pull the trigger once, so the flash is in the shot
-hold N                  hold the shutter N frames longer.  A muzzle flash
                         is gone in a fifteenth of a second and what it
                         leaves -- the crater, the sparks off the plate,
                         the smoke -- is what the frame after it is for
-flash                   the Schlick-clamp case, photographed
-bench N                 vsync off, first 30 frames discarded
-wav out.wav             the audio mixer with no device, plus its statistics
-mats                    per-material albedo and emissive, to diff against
                         node tools/matview.js
-vmw                     the eighteen viewmodel meshes, to diff against
                         node tools/vmwcheck.js
-prop N                  one of the 37 props on a card, framed by its own
                         size and lit with the bench's key, to be held
                         beside props/props_v01/propview.html.  The mesh
                         was already checked by number; the SURFACE can
                         only be checked by eye.
-fast                    the coarse bake -- 4 rays, not 12
-probe atlas.png         every zone as a column, every roughness as a row.
                         A column that is dark is a zone with no light in
                         it; a row no blurrier than the one above it is a
                         prefilter that did not run
-debug N                 albedo / normal / roughness / ao / emissive / metal /
                         lightmap / IBL term / raw probe
-log                     derelict.log
-dev                     the developer HUD

the route, and what is on it
-auto N                  it plays itself for N seconds and says what it
                         reached.  Unkillable unless -mortal.  Ten seconds
                         without progress prints STALL and steps onto the
                         lattice, so one bad doorway does not hide the rest
-mortal                  ... but let it die
-god                     the same immunity, by hand
-scene N                 start at the head of scene N with everything that
                         scene assumes already true.  The work card's `goto`
-probe x0,y0,x1,y1       the deck as an ASCII map, in PLAN coordinates
-probe ...,gx,gy         ... and what can reach the goal.  The instrument for
                         "the deck plan is not the ship"
-spider N                N of them, in front of you
-lumi N                  the drone on a card: eye height, 1.30 m down the
                         view vector, facing you, whatever the camera
                         does -- and the intro skipped, because the
                         opening's camera is on the deck at 0.35 height.
                         N is the display state, zero included.  Pose,
                         display AND vitals are held, so the same shot
                         twice is the same shot.  Against
                         props/lumi_v02/lumi_v6.html
-lumimesh                her 35 parts by name: counts and bounding box, to
                         diff against node tools/lumicheck.js

the card, and the language
-title                   the title card, photographed
-title -options          ... and the language page
-wake N                  the eyelids held N tenths of a second into the wake
-en                      start in English (the card's other setting).  Every
                         instrument flag above skips the card, or it would
                         sit on a title screen waiting for a keypress
-font                    the Hangul machine, on a card
```

There is **no `-console`**; CLAUDE.md used to offer one. `-dev` is the HUD and
`-log` is the transcript.

On the card: **W/S** pick, **Enter** or **click** confirm, **Esc** quits from
the title and backs out of the options page, **A/D** switches the language.

In game: **WASD** move, **mouse** look (click to capture), **Shift** run,
**C** crouch, **Space** jump, **F** torch, **T** debug channel, **N** noclip,
**M** mute, **Esc** release the mouse. **Left mouse** fires, **1/2/3** pick
the pistol, the double-barrel and the rotary cannon, **R** reloads.

`-probe` prints `P` for a cell with a prop standing on it, which is how
"the furniture is not in the way" gets checked rather than asserted.
