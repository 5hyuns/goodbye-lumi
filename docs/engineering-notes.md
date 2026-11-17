# Engineering notes -- GOODBYE LUMI

Problems that came up while building this game, one entry each: what went
wrong, how it was found, and what fixed it. Most of them are traps that look
right in the code and only show up on screen, in a measurement, or in a long
play session. Entry names say the lesson; `code identifiers` point at where
the fix lives (see docs/FILEMAP.md).

The notes were written during development, for the people (and the AI
assistant) working on the code at the time, so some describe the state of a
system before it was finished. Line numbers and sizes inside an entry are
as measured when it was written.

contest-budget: The whole deliverable must fit 1,474,560 bytes -- a 1.44 MB
  floppy -- measured after decompression, with the executable and its runtime
  counted in. Internal compression is allowed; the unpacked total is what is
  measured. Every size decision answers to this and nothing else.

browser-build-is-previz: The contest prohibits browser games in as many words
  ("웹브라우저 게임은 금지됩니다"), and the GDD agrees: native executable, no
  commercial engine. src/derelict/map.html is therefore a previz, not the
  submission. Judging how the ship looks and plays in Chrome is correct;
  calling it the deliverable is not. native/build/playlumi.exe is.

the-native-build-is-the-deliverable: native/ is a C port of map.html -- Win32
  and OpenGL 3.3 core, no libraries, two gcc calls and a windres for the
  icon (native/build.sh). The compiler is w64devkit at C:\w64devkit; it was
  downloaded, not installed, and it is not on PATH. Everything that shells
  out to gcc has to add /c/w64devkit/bin itself or it fails with "cannot
  execute as". The exe is build/playlumi.exe.

c-does-not-order-its-arguments: JavaScript evaluates call arguments left to
  right and C does not, so a port that folds several rng() calls into one
  argument list -- chamferBox(B, -0.045+rng()*0.09, 0.085+rng()*0.20, ...) --
  silently consumes the stream backwards under gcc and builds a different
  mesh. One call per statement. This is invisible in a screenshot; it was
  caught by playlumi.exe -vmw against node tools/vmwcheck.js.

ffast-math-breaks-a-cancellation: native/src/vmw.c is the one file compiled
  without -ffast-math, and build.sh says why: finalize() decides a welded
  corner has no tangent of its own by whether the tangents of the faces
  meeting there sum to zero, and a reassociated sum does not cancel exactly.
  Fast-mathed, 44 of the double-barrel's 5,808 corners took a noise
  direction instead of a clean axis. Any new code that tests a float for
  having cancelled needs the same exemption.

one-lookup-three-copies: The octahedral probe fetch is written out three
  times -- render.c's scene shader, vmw.c's viewmodel and ent.c's entities --
  because none of the three shares a source file. octEnc must stay the exact
  inverse of bake.c's octDecode, and the layer arithmetic must stay
  zi*PROBE_LEV in all three. If they drift, nothing errors: the gun simply
  stops belonging to the corridor it is being carried down. Change one, grep
  for octEnc and change all three.

the-analytic-envbrdf-fit-is-not-good-enough: The four-constant polynomial for
  the split sum's DFG term (c0=vec4(-1,-0.0275,-0.572,0.022) and friends) is
  off by 0.18 on the scale term against the real integral -- a sixth of the
  reflection, missing where a polished panel is brightest. render.c
  Monte-Carlos a 32x32 table at load instead and hands it to all three
  shaders. It costs 16 KB of VRAM and no bytes on disk, so do not "optimise"
  it back into a fit.

the-split-sum-drops-the-second-bounce: Single-scatter GGX loses the light
  that hit a second microfacet -- negligible on a dielectric, a third of the
  reflection on rough metal, where steel at roughness 0.8 with F0=0.6 comes
  back at 0.33 of what it should and reads as soot. All three shaders carry
  Fernandez-Aguera's compensation, and it rides on the ambient cube rather
  than the mirror direction because that is where the energy actually went.

a-punctual-light-has-no-size: GGX asked for a mirror answers 1/(PI*alpha^2),
  which at the ship's 0.04 roughness floor is 78,000 and which the bloom
  threshold turns into a white hole. Every analytic light in the world shader
  is floored at alpha 0.05 -- a 0.3 m lamp panel at 3 m -- and that is also
  the alpha at which D*V peaks at 25.8, the exact peak the Blinn-Phong it
  replaced had at shin=640. Any new light source needs the same floor.

the-ray-result-is-three-globals: rayTraceOne reports through rayHitDist,
  rayHitAx and rayHitSg, and every trace overwrites all three -- including
  the shadow rays sampleLight fires. fxSurfaceHit samples the lamps for the
  crater it lays, so a caller that reads the hit distance after calling it
  reads the last shadow ray instead. Copy all three into locals on the line
  after the trace.

the-viewmodel-clears-the-depth-buffer: vmwDraw() owns its own depth range so
  a bulkhead at the muzzle cannot cut the barrel in half, which means it has
  to be the LAST thing drawn into the scene target. render.c therefore
  offers renderGlass() separately from renderEndScene(), and game.c orders
  the frame world -> entities -> glass -> atmosphere -> brass -> viewmodel.
  Anything added after vmwDraw() is depth-tested against nothing.

runtime-size-is-answered: GDD risk 2 is closed. The runtime is Win32: the exe
  imports GDI32, KERNEL32, msvcrt, OPENGL32, USER32 and WINMM, all of which
  ship with Windows, so nothing is counted beside the file. The whole ship
  plus its three procedural weapons, 184 room props, the title card, LUMI's
  own 14,772-triangle mesh and the story in three languages is 1,065,984
  bytes, leaving 408,576 of the 1,474,560 cap. Content is still not the
  constraint.

lumi-is-frozen-do-not-redesign-her: props/lumi_v02/ is a FROZEN design --
  LUMI-SPEC.md plus lumi_geometry.js, a dependency-free generator whose
  buildLumi() returns 35 parts / 9,627 vertices / 14,772 triangles with
  every transform already baked into the points. §0 says in as many words
  not to rebuild her out of spheres and cylinders and not to round its
  numbers, and the first native pass did exactly that: thirty primitives
  where a finished mesh existed. native/src/lumimesh.c is that generator
  ported statement for statement, grouped by material into thirteen
  instanced ranges. Change lumi.c's materials and animation freely; change
  the shape only on the bench, and then re-port. A shape change is proved
  by `playlumi.exe -lumimesh` against `node tools/lumicheck.js` -- all 35
  parts, counts AND bounding box, because the spec's own checksum is three
  totals and three totals cannot see a transposed rotation.

the-decal-is-ink-not-a-placard: LUMI's four decal patches carry alpha
  textures on the bench, so what is not ink is the white shell. Given them
  a dark colour in the exe -- the obvious reading of "material: decal" --
  and she wears four black blotches. The patch takes the PAINT colour and
  the lettering is separate geometry: "LUMI" and "MAINTENANCE UNIT" cut out
  of hud.c's own 5x7 stencil, one tile per lit cell, projected onto the
  decal's sphere. LUMI-SPEC.md §8 asks for exactly that substitution. It is
  a 36th part and is deliberately NOT in the §6 checksum.

the-entity-pass-does-not-blend: render.c enables GL_BLEND for renderGlass()
  and disables it again, so entDraw() runs opaque and EntFx.alpha only
  darkens -- there is no additive layer for an entity. LUMI's emitter is
  two diffusion cones the bench draws additively at 0.16 and 0.11 opacity
  with a vertex-colour fade; ported literally they are an opaque lampshade
  under a solid white plate. Carry that kind of thing as a ratio between
  emissives instead, and let the dynamic light do the work.

a-prop-casts-no-shadow-because-the-bake-ran-first: the lightmap is solved
  in buildMesh() and the props are placed 60 lines later, because the placer
  asks where the doors and the items ended up -- so every prop in the ship
  stood on a floor lit as though it were not there, which is the single
  thing that reads as "no shadows".  Re-running radiosity with 185 more
  occluders is 4.6 s the boot cannot spend twice.  props.c's
  propShadowBake() instead makes one pass over the scene vertices AFTER
  placement and scales MESHV[9..21] -- the AO term, the three radiosity
  directions and the switchable bank -- by how much of the sky each vertex
  loses to the boxes near it.  20 ms, 86,304 of 2,704,248 vertices.  Two
  things it must have: the boxes in a grid (2.7 M x 185 is 500 M tests),
  and a dot(toBox, normal) gate, or a container darkens the far side of the
  bulkhead it is standing against -- a shadow through a wall is worse than
  no shadow.  renderUpload() moved BELOW propsInit rather than running
  twice; nothing between the two draws.

one-flicker-bank-is-a-renderer-fault: bake.c splits the 257 lamps into
  switchable groups and STY_N was 4, so every broken fixture in the ship
  struck and failed on the same frame forty metres apart -- which no two
  tubes have ever done, and which reads as the renderer stuttering rather
  than as the ship having broken tubes.  Six banks: 0 and 1 go out with the
  sink, 2 is the alarm, 3-5 are the broken ones on three salts and three
  rates.  render.c already packed the group index in three bits (`sgi & 7`)
  so seven was always the ceiling and four was a choice; what has to move
  with STY_N is the `uniform vec3 uStyleC[N]` in render.c AND atmos.c and
  both glUniform3fv counts.  `playlumi.exe -auto N -log` prints a `style`
  line every two seconds: if banks 3-5 ever agree for a whole run they are
  one bank wearing three names.

the-ambient-floor-decides-the-palette: shiplights.py hangs amber
  (1.00,0.52,0.14) over every piece of machinery in the ship and cold white
  where the systems still hold, and F.minLight was 0.046 NEUTRAL grey -- so
  the ship read as a grey room with orange dots in it rather than as a
  sodium-lit hold.  Same luminance pushed to sodium (0.058,0.042,0.029) is
  the whole change, and the fog colour has to follow it off blue, because
  past a few metres the fog IS the ambient.  Do not read a room's palette
  from the crew quarters: that one is cold white on purpose.

the-prop-surface-is-evaluated-not-baked: props_v02 ported the bench's
  GEOMETRY 1:1 -- 19,094 faces, the same number both sides -- and stopped
  there, collapsing each region to (albedo, roughness, metalness,
  emissive).  So every prop was a correctly-shaped flat box while the
  walls beside it carried the full material compiler, and it read as a
  different model rather than as the same model unlit.  The bench bakes
  eleven height functions into a per-prop atlas and this build cannot:
  37 props at the density a bolt needs is 1.6 GB of texture.  So the exe
  evaluates the SAME functions per fragment -- props.c carries u,v,w,h,
  kind, seed and two params on every prop vertex (prim.c's PRIM_R, a
  parallel buffer so no existing solid had to widen), tools/propglsl.py
  extracts LIB_GLSL verbatim into props_glsl.h, and ent.c splices it in.
  Five H() evaluations a fragment, 723 -> 659 fps.  Extracted rather than
  transcribed for nativepack.py's reason: a hand copy is a second thing
  to keep in step.  Check it with playlumi.exe -prop N against
  propview.html; the geometry check cannot see a missing surface.

a-drone-pinned-against-a-wall-climbs-forever: lumi.c's slide-off-a-
  bulkhead had a last resort -- lift 20 cm to clear a ceiling rib --
  written as ny=L.y+0.25f*dt*4.f, which is one metre a SECOND with no
  ceiling of its own and nothing anywhere that brings her back down
  (descent needs the target below her AND a clear path, and a pinned
  drone has neither).  Measured over 300 s she climbed to 3.21 m -- over
  the 2.62 m door heads, where the ship has no walls -- and sat there
  while the player walked 68 m away.  Capped at the target's own height
  plus 0.90 m: max altitude 3.21 -> 2.75, and 119 samples of 149 above a
  door head -> 2.  Anything that lifts a flying thing needs a ceiling
  AND a way down.

leading-is-a-decision-not-a-distance: Lumi's lead point was a fixed
  distance ahead and BOTH settings of it were wrong.  5.5 m ran her
  through the far door before the player cleared the near one; 3.5 m,
  the fix for that, is still the middle of the room in the lobby or the
  gallery, so she led and pointed at nothing and the player could not
  tell which of five doors to take.  A number cannot answer it.  She now
  walks the route forward and stops at the first DECISION -- a door leaf
  within 1.4 m of the route, or a turn of more than ~36 degrees off the
  first leg -- capped at 9 m, and rides the cap down a corridor where
  there is nothing to decide.  And she never leads from further than she
  can be followed: past 14 m she drops to the shoulder, past 24 m for 3 s
  she is simply put there, because losing her costs the compass, the
  health readout and most of the light at once.

plan-is-data-everything-else-is-code: tools/nativepack.py writes
  native/src/plan_data.h by handing map.html's own sections to node with a
  dumper appended -- never by parsing them, because a parser is a second thing
  to keep in step. Re-run it after any change to the Python chain. The bake,
  the material operators, the mesh machine, the kit and the shaders are ports.

the-exe-photographs-itself: playlumi.exe -shot out.png -cam x,y,z,yaw,pitch is
  the native tools/shot.js, and -cam's y is the player's FEET (the eye is
  1.66 m above it). -bench turns vsync off, -wav renders the audio mixer with
  no device, -mats prints the per-material averages to compare against
  node tools/matview.js. A claim still comes with evidence.

no-dependencies-by-design: Zero external packages. The Python tools import
  stdlib plus their own siblings; the node tools require only child_process,
  fs, os, path and zlib. tools/shot.js drives Chrome through child_process
  rather than puppeteer. There is no package.json, no pyproject.toml and no
  install step -- do not add one to solve a problem a stdlib call solves.

no-assets-on-disk: Every texture, mesh, material and sound is generated in
  code at load time. An image or model file added to the tree is bytes off the
  1.44 MB budget, so the answer to "we need a texture" is a recipe in
  shipmaterials.py, not a PNG.

edits-go-in-the-tool-never-the-output: map.html, derelict.html, kit.html and
  tools/_*.js are generated. An edit made in one survives until the next
  build and then vanishes without an error. See CLAUDE.md section 4.

splices-are-anchored-and-idempotent: weapons.py, doors.py and decals.py patch
  derelict.html in place through sub()/cut(), which assert their anchor text
  exists. Re-running is safe by design. When adding a splice, anchor it on text
  that only occurs in the source, and give it a label so a missing anchor names
  itself.

visual-claims-need-a-screenshot: The radiosity bake runs in the browser, so
  nothing about how a surface reads can be settled by reading the code.
  node tools/shot.js photographs a real Chrome. "It should look right now" is
  not a result.

schlick-needs-clamp-not-max: The torch puts the light AT the eye, so L and V
  are the same vector, H is V, and dot(V,H) sits exactly on 1.0 -- where
  rounding tips it to 1.0000001. pow(1.0-max(dot,0.0),5.0) then raises a
  NEGATIVE base, which GLSL leaves undefined; this GPU answers NaN and
  clamp(NaN,0,1) is 0. That was ~2% of every torch-lit fragment turning black
  (pepper on the walls), and the half-res bloom then blurred those NaNs into
  8-pixel black blocks over the viewmodel -- the "mosaic gun". Clamp the
  Schlick base to [0,1]; do not merely floor the dot. Same trap on any
  pow(1.0-ndv,k) where ndv=max(dot(N,V),0.0). Fixed in render.c (fres5),
  vmw.c (F_S) and ent.c (ndv); playlumi.exe -flash photographs it.

the-deck-plan-is-not-the-ship: Every coordinate in story/ is a point on
  ref/ship_deck_plan_v6_fixed.svg, and build_ship.py widens passages and
  snaps what it builds to the 0.6 m half-module grid. A door leaf can stand
  five metres from the coordinate that names it, a spawn point can land
  inside a bulkhead, and a path goal inside a bulkhead solves a field that
  reaches nothing -- which reads exactly like a level that is not connected.
  Bind doors by one-to-one nearest assignment, snap spawns with freeSpot()
  and goals with navNear(), and settle any question about the deck with
  playlumi.exe -probe x0,y0,x1,y1[,gx,gy] rather than by reading deckplan.py.

hangul-is-24-letters-not-11172-pictures: The story is Korean and the overlay
  had a 48-glyph Latin stencil. A bitmap font is not affordable at this
  budget, so native/src/kfont.c stores fourteen consonant shapes and
  fourteen vowel parts as strokes in unit boxes and applies the composition
  rule: initial, medial, final, each scaled into the box the medial's
  orientation asks for. 119 strokes, five bytes each, drawn with the same
  quad batcher as the rest of the HUD. playlumi.exe -font puts it on a card.

a-filter-takes-energy-away-and-peak-stops-meaning-anything: audio.c builds
  every strike, scrape, ring and body from `peak`, and three of those four go
  through a biquad while the fourth does not.  A band-pass at Q 1.1 on 880 Hz
  passes ~800 Hz of a 22 kHz noise floor -- a fifth of the amplitude that went
  in -- so a strike written at 0.12 over a sine body written at 0.05 came out
  with the BODY three times the louder, and every layered sound collapsed onto
  its lowest partial: an 80% energy bandwidth of 22 Hz on something meant to be
  broadband, which is a NOTE.  fltComp() puts the loss back inside auHit and
  auScrape so one number means one thing in all four helpers.  Anything new
  that band-passes noise and then balances it by ear against an oscillator has
  the same trap.

a-sound-has-to-clear-the-room-tone: the ambient bed measures -43 dB RMS and
  nothing in the file was ever checked against it.  Measured on the -wav bench
  at the loudest 30 ms (an average over a 350 ms slot reads a 10 ms clack as
  silence): four mechanism sounds, all three console sounds and every one of
  LUMI's syllables sat within 1 dB of the hull hiss -- the drone who carries
  the compass, the health readout and the whole story was inaudible under the
  ship.  A new sound is not finished when it is correct, it is finished when
  `python tools/wavan.py` puts it 8 dB or more over the bed.  The two that stay
  under it are deliberate and say so in their own comments: the animal's
  footfall and the player's walk.

a-naked-triangle-is-a-square-wave-wearing-a-hat: the whole point of section 10c
  is that a bare oscillator at a named frequency reads as UI, and the first
  draft of auUI and MCH_EJECT rebuilt exactly that out of auRing -- flatness
  0.0005 and 0.005, indistinguishable from the auClick they replaced.  A
  console blip and a tumbling case are noise through a band at Q 5-7: a pitch
  with a spectrum still in it.  Reach for auRing only when something is
  genuinely ringing, and never alone.

the-hud-batch-ends-at-hudEnd: hudEnd() uploads the queued quads and draws
  them, and then the queue is gone. Anything queued after it -- storyHUD(),
  a debug card -- is drawn by nobody and there is no error. Queue first,
  flush once.

a-condition-must-not-spend-a-one-shot-trigger: three of the fourteen
  encounters carry a CONDITION as well as a trigger, and two of those are
  triggered by something that happens exactly once -- E-01 by a latch in
  the C-03 spine, E-02 by `enter_R-LBY first`.  Both want has_pistol, and
  picking the pistol up is an E press the player can decline: walk past
  W1, cross the zone, and the first two fights of the game are gone for
  the whole run with nothing logged.  (E-09, the third, hangs off the
  REPEATABLE `enter_C-04` and retries by itself -- the answer was already
  in the same table, on one row of three.)  dispatch() now ARMS an
  encounter whose trigger arrived before its condition and re-checks the
  armed ones on every event; the flag always turns during an event
  (vmwGive runs before takeItem fires pickup_W1) so the re-check lands on
  the same dispatch.  Proved by temporarily regating E-01 on has_shotgun:
  the zone fired at line 200, the shotgun at 439, "E-01 armed earlier,
  starting now" at 440, units on the deck at 445.

the-last-act-hangs-off-a-drone-arriving: lumi_dock_arrive is the only
  producer of sink 3, the ninety seconds, E-12 and the capsule, and it
  fired only when Lumi was physically within 0.6 m of the dock -- issued
  ONCE, with a latched `moving`, and no way back.  Two things take her
  off it, both at sink 2 where the trip runs: doorStep(), which only
  disables itself at sink 3, diverts her to any unopened panel door
  within DOOR_ASK of the PLAYER and hands her back in LM_LEAD; and plain
  geometry, which catches her.  Either one stops the game at S9 for ever.
  The errand is now re-issued every frame she is not on it, doorStep
  returns early while she is, and past DOCK_GIVEUP_S (25 s) she is simply
  placed on the dock -- the same admission restore() and the door's own
  DOOR_GIVEUP_S already make.  A companion's flight must never be the
  thing that ends a run.

-scene-has-to-move-lumi-too: -scene N teleports the PLAYER and left her
  wherever boot put her, which for -scene 9 is the crew quarters: forty
  metres of ship between her and the dock, so the one flag that names the
  last act could not test it -- a 200 s run of it showed no transition and
  the reading was the instrument's, not the game's.  storyScene() now ends
  in sceneMoveLumi(), which puts her at the player's eye unless the scene
  itself chose DOCKED or OFF.  An instrument that does not stand the whole
  scene up will be believed anyway.

the-story-holds-the-trigger: playlumi.exe -auto plays the game to verify a
  route, and it found three real bugs by failing at them. Two of its own
  early failures are worth remembering: it walked along its camera yaw
  rather than the route (the head turns at 3 rad/s and the feet do not have
  to wait), and it held the horizon while shooting at an animal 0.8 m tall
  whose head is out in FRONT rather than up top -- a hundred seconds of fire
  and nothing killed. Aim down.

the-emergency-lighting-is-a-grade: Spec 7 turns the whole ship red from sink
  stage one. The ship's 257 lamps are baked, so a real recolour is a re-bake
  -- 4.6 s in the middle of a chase. renderSetEmergency() grades the frame
  instead: luminance kept, hue driven to red, three instructions in the
  composite, and it can cross-fade, which a bake cannot.

the-first-surface-is-not-the-deck: A downward trace from 2.9 m finds
  whatever is overhead, and in this ship that is often not the floor:
  shipdetail.py hangs rib rings and pipe runs at 2.4-2.8 m. story.c's
  floorAt() took the first hit, and four systems settled on that answer --
  an item stands on it, the goal ring lies on it, Lumi flies to it +1.45 m,
  a prop stands on it. One trace, four bugs: the rotary cannon sat 2.7 m up
  outside its own 2.4 m pickup sphere and could not be picked up by any
  player at all, and the drone rose OVER the 2.62 m door head and crossed
  the ship through the ceiling. Use level.c's deckNear()/deckAt(), which
  walk the whole column and keep only surfaces with 1.55 m of clear space
  above, nearest the deck you name.

  AND THE COLUMN HAS TO START ABOVE THE DECK BEING ASKED ABOUT. deckNear
  began its trace at 2.90 m and walked DOWN, which is over every main-deck
  surface in the ship and under the one deck anything ever names: the cargo
  platform's grating, laid at 3.15 by shipdetail.py with its top face at
  3.37. Pack 3 -- the only item with a deck written in storypack.py's row --
  reported "NOTHING within 1.2 m of its written deck 3.15" for as long as
  that branch existed, which reads as a platform that was never built rather
  than as a trace that began underneath one. The start is now want+0.95 when
  that is higher, and at want 0 it is the old 2.90 m and 4.2 m exactly.

a-written-deck-needs-a-route-up: The above woke a branch that had never run,
  and a live one has to answer the reachability question the dead one never
  did -- pack 3 is the THIRD energy pack, so a platform with no way onto it
  is not a lost beat, it is a run that cannot be finished. Ask it with
  routeTo() (start -> target), never with settleItem's reachable(), which
  solves the field FROM the start and therefore measures target -> start;
  see the-nav-lattice-is-directed. A platform is exactly the shape where the
  two answers differ. Measured: the cargo platform is real at 3.66 m and has
  NO ROUTE UP -- the three containers meant to be its staircase have 1.05 m
  risers against a 0.46 m STEPUP and a 0.99 m jump apex (6.6^2/2*22), so
  "3단으로 쌓아 계단" is not a staircase. Left as-is by decision 2026-09-03;
  the pack falls back to the floor and the game stays winnable, and if the
  climb is ever built the pack goes up on its own.

the-visible-floor-is-not-the-traced-floor: rayTraceOne hits BRUSHES, and
  the plate the player sees is a kit part (FLOOR 1..4) standing on that
  brush. So a marker laid at the traced height is inside the floor and
  draws nothing at all, with no error -- the weapon plinth and the goal
  ring both did. Lay anything flat at deck + 0.085 m, not deck + 0.03.

the-nav-lattice-is-directed: zom.c stores links as INCOMING edges, so
  solveField(n) answers "what does it cost to reach n from everywhere
  else". That is the flow field the animals want and the REVERSE of what a
  quest target has to know. The escape gallery has a drop that can be
  walked down and not up, so a point the whole ship can reach can itself
  reach nothing -- -auto ground against the gallery's south wall for four
  minutes walking at a shotgun nine metres away. Test a destination with
  zomRouteExists(from,to), never with zomGoalDist alone.

surviving-is-not-the-same-as-winning: The final defence ran 90 seconds and
  then failed you unless you had pressed E inside 3.5 m of the capsule in
  the last 25 -- with nothing on the overlay saying so. Holding the gallery
  for the whole ninety and standing in the lit ring at zero read as "THE
  CAPSULE LEFT WITHOUT YOU". The defence is the difficulty; the door of the
  thing you are defending is not. There is a prompt now, the hatch outranks
  an item at your feet, and standing in it at T-0 boards you.

props-are-placed-by-solving-not-by-typing: native/src/props.c asks each
  room for so many of a kind and walks a grid to find spots that pass every
  test -- deck, footprint, a wall behind it, 1.25 m of room in front, three
  metres off every door leaf, clear of every item and landmark. A kind that
  cannot find a legal spot places fewer. Then storyClearRoutes() walks the
  nav route from the start to every objective and deletes anything standing
  on it, because every test in the placer is local and a room is not a
  corridor. "동선에 방해되지 않게" is that last pass, and
  playlumi.exe -probe prints prop footprints as 'P' so it can be checked.

the-title-card-owns-the-keyboard: menu.c holds the director (no storyStep,
  no simulate) until the player presses start, then runs a 3.2 s four-blink
  eyelid open over S0's own pose. Every instrument flag -- -shot, -auto,
  -bench, -scene, -probe, -cam ... -- skips the card, or they would sit on
  a title screen waiting for a keypress. -title photographs it and
  -wake N holds the lids N tenths of a second in.

english-is-a-table-beside-the-korean: story/dialogue.json carries `en` per
  line and storypack.py emits SL_TEXT_EN / SB_TEXT_EN / SQ_TEXT_EN beside
  the originals. The whole of the localisation is menuPick() and story.c's
  lineTxt/barkTxtOf/questTxt. A missing translation falls back to the
  Korean: untranslated is a small problem, blank is not. SL_CPS still times
  the subtitle off the Korean cell count, so English lines are written to
  about the same length.

a-straight-line-is-not-a-route: lumi.c flew the straight line to her target
  and, when the next position was solid, dropped whichever axis was
  blocked and kept the other. That slide gets round a FLAT WALL and
  deadlocks on every CORNER, where both axes are blocked at once and there
  is nothing left to drop. Measured with the lumiTrace counters over a
  300 s -auto run: 64% of her frames pinned, 51.6 s of it in one stretch
  sixteen metres behind a player who had walked on, and no door opened in
  the whole run -- every door beat in the game waits on lumiReadyAt(), so a
  drone that cannot round a corner is a story that stops at one. She now
  asks zom.c's lattice, on a field of her own (zomFlyGoal/zomFlyStep --
  separate from distGoal, which leadStep aims at the QUEST goal), whenever
  the line ahead is solid.

  AND THE ROUTE ALONE FIXED NOTHING, because two other things were also
  true and each one hid the others. (1) The look-ahead was ONE POINT
  1.30 m down the heading, which steps over a thin bulkhead and reports
  clear air on the far side: she sat flush against something four
  centimetres in front of her for twenty-two seconds with the route never
  once asked for. A look ahead is a SEGMENT. (2) The only thing the pin
  branch tried was a LIFT, which is the wrong instinct under a ceiling
  rib -- it carried her off her 1.40 m cruise to 2.06 m and left her
  there, since coming down needs the target below her AND a clear path
  and a pinned drone has neither. Down before up. (3) And a ghost is not
  allowed to be stuck at all: after PIN_GIVE (1.2 s) of getting nowhere
  she passes through and stays a ghost long enough to come out the far
  side, because spec 3.1 made her a ghost for exactly this and she is the
  light, the health readout and the only thing that opens a door.

  `playlumi.exe -auto N -log` prints a `lumiv` line every two seconds:
  pin / inside / stall / far / want / got as percentages of frames, plus
  the worst run of each since boot. Measured over 300 s, before -> after:
  mean pin 63.9% -> 2.0%, worst continuous pin 51.6 s -> 1.2 s, mean
  stall 59.8% -> 16.3%, doors opened 0 -> 4, emergency recalls 2 -> 0.
  Any of them creeping back up is the bug returning.

knowing-the-way-is-not-knowing-where-in-the-corridor: the lattice answers
  WHICH WAY and says nothing about WHERE, and the shortest way through a
  doorway is along the jamb. So a correct route still scrapes the ship.
  lumi.c probes left and right of its chosen heading (clearTo, FLY_WIDE
  1.70 m) and steers toward the side with more room: in an open room both
  probes run out and cancel, in a corridor they find the two walls and she
  rides the centreline, in a doorway they find the jambs. Thirteen solidAt
  calls a frame and only while she is travelling. This is the other half
  of a-straight-line-is-not-a-route and neither half works alone.

she-does-not-get-to-shut-a-door-in-your-face: a leaf is a motion sensor
  and Lumi is the second thing that stands on it. She leads, so she
  reaches it first, it opens for her, she goes through, and AD_DWELL later
  it shuts with the player still on the near side and "follow Lumi" still
  on screen. doors.c now latches D->held (AD_HELD 30 s) on a leaf she
  triggered while she was NEARER to it than the player, and the player
  reaching the same sensor is what lets go. A forced leaf (adForce, the
  locked doors she opens for the story) was already sticky; this is the
  unlocked ones.

two-flags-are-spelled-probe: `-probe atlas.png` writes the reflection probe
  atlas and `-probe 145,420,205,650` prints the deck as an ASCII map. They
  were merged in from two branches and collide on the first six characters,
  so asking for the deck map silently wrote a 1.6 MB PNG named
  "145,420,205,650" and printed nothing. game.c now decides on the .png
  suffix. Any new flag sharing a prefix needs the same.

a-fixture-belongs-on-a-plate-and-the-marker-in-front-of-it: the gallery
  console and the capsule hatch stood in the MIDDLE of a nine-metre room,
  the six-slot interface with its back to open floor and its face at the
  far bulkhead, and the objective ring eight metres north of either. Three
  things in an empty hall, none of them obviously the one the objective
  means. story.c's wallNear() mounts both on the nearest bulkhead, backs
  to it, facing in, separated along the plate by their own two widths --
  and then the POINT each marks is moved to FIX_STAND (1.35 m) in front of
  its own face, with questAim() re-aiming every objective that named it
  THROUGH onRoute, because a ring the lattice cannot reach is a run that
  stops. props.c fixes the convention: -Z in a prop's frame is its back,
  so the front is (sin yaw, cos yaw).

a-beat-fires-where-you-stand-not-where-you-cross: the gallery briefing
  (L-016..L-020, and gallery_briefed with it, which unlocks D-22) hung off
  `enter_R-GAL first` -- which is the VOLUME's own event, so five lines
  played the moment the player crossed the threshold, forty metres from
  the capsule they are about, and the marker they were walking to had
  nothing left to give. A movement objective's arrive event must be one
  only the goal ring can fire: `reach_capsule` now, aimed at the front of
  the hatch. Check before reusing a volume event as SQ_ARRIVE -- and check
  the quest CHAIN first, because quest N's arrive is usually quest N+1's
  start (the lobby's `enter_R-LBY first` is exactly that and must stay).

three-shaders-and-only-two-were-pbr: render.c and vmw.c have shaded with
  Cook-Torrance F*D*V since the GGX pass, and ent.c did not -- its lspec()
  returned pow(N.H,shin)*(shin+8)/25.13 for everything that was not a
  creature, which is every prop, every door leaf, the escape capsule and
  LUMI.  No Fresnel, so a panel was as bright face-on as edge-on; no
  shadowing term, so a rough face kept a highlight it should have lost;
  and that normalisation is for a hemisphere of Blinn lobes rather than a
  BRDF -- at shin=150 it peaks at 6.29 where a dielectric's real answer is
  0.16, forty times too bright, which the tonemap then flattened.  That is
  what "the props look like painted plastic next to the walls" was.  When
  a lighting model changes, grep for EVERY shader: there are four in the
  tree (render.c, ent.c, vmw.c, fx.c) and they share no source file.

a-baked-lamp-casts-no-highlight: the bake solves the 257 lamps' DIFFUSE
  contribution and stores it in the lightmap, correctly and with shadows.
  Specular is a function of where the EYE is, so no bake can hold it, and
  the only thing standing in for it was one blurred lobe fired down the
  lightmap's dominant direction.  A corridor lined with amber strips
  therefore had amber strips in it and no amber ON anything.  render.c now
  picks the MAXSL (16) lamps whose sphere of influence is nearest the
  camera -- by dist-radius, not dist, or a big weak lamp thirty metres off
  beats the strip overhead -- multiplies each by its flicker bank, and both
  the world and the entity shader evaluate a real GGX lobe for them.  The
  dominant-direction lobe drops to half weight because its direct half is
  now carried analytically and only the bounce is left for it.

the-bake-is-the-shadow-map-but-only-if-you-ask-it-smoothly: an analytic
  light with no visibility test shines through bulkheads.  There is no room
  for shadow maps and no need: the bake already fired a ray per lamp per
  texel, so the ratio of the baked irradiance actually standing here to
  what the lamp WOULD have delivered unshadowed IS the visibility, clamped
  to one so it can only take light away.  But do NOT divide by `baked` --
  that is the three basis directions weighted by the NORMAL MAP, and across
  a plate seam the normal map swings a quarter turn between one pixel and
  the next, so the ratio flips 0/1 along the seam and the highlight becomes
  a dotted line.  Use (vLM0+vLM1+vLM2)/3, which is vertex-interpolated and
  cannot flip.  A visibility test does not need the normal map's opinion.

a-lamp-radius-is-a-falloff-rate-not-a-reach: the falloff is (1-d/r)
  squared, so r is not "how far you can see it", it is how fast it dies.
  deckmap.py's amber kick lights were r=3.0 and the shell wall runs r=4.2
  against the white ceiling fittings' 12.0, so a point two metres along a
  skirt was at 11% of its own lamp and the deck plate a stride from an
  amber strip was the same grey as plate nowhere near one -- "the orange
  lights do not affect anything".  4.6 and 6.4 put that point at 34%.  The
  peak does not move: that is the colour.  Lamp radii live in deckmap.py
  (the kit's own) and shiplights.py (the plan's), and changing either means
  the whole Python chain AND nativepack.py.

the-torch-is-one-string-in-three-shaders: the flashlight is evaluated by
  render.c's world, ent.c's entities and fx.c's decals, and a crater lit by
  a plain cone inside a wall lit by a cookie is worse than either.  It lives
  once, in native/src/torch.h, as a TORCH_GLSL macro the three concatenate
  into their own shader source -- see one-lookup-three-copies for what
  happened the last time this project kept three copies of one lookup.
  EVERY line of that macro carries its `\` continuation, comment lines
  included: a comment line without one ends the #define in the middle of a
  shader.  vmw.c is deliberately NOT on it -- the viewmodel sits 400 mm
  from the lens, inside the near field where the reflector pattern has not
  formed, and its own soft cone is the right answer there.

a-cone-is-not-a-beam: smoothstep(0.895,0.972,dot(-L,camDir)) times
  (1-d/24) squared is a circle of even paint with no structure in it, and
  it read as a spotlight in a level editor.  What the eye recognises in a
  hand torch is three things, and all three are functions of the direction
  the ray leaves the TORCH rather than of the wall -- so the pattern slides
  across a bulkhead as the head turns, and that sliding is what makes it a
  beam instead of a decal.  The reflector is pressed in facets, so the disc
  has petals and a scalloped edge; the bulb's holder shadows the middle of
  it, so there is a faint dark ring INSIDE the hot spot; and the lens is
  dusty, so the beam carries fine grain (THREE octaves and fine -- two
  coarse ones read as cloud on the wall rather than dirt on the glass).
  The falloff is 1/(1+0.09 d^2) now: the old one was very nearly linear
  over the distances this ship has, which put as much light on a wall eight
  metres off as on one three metres away.

specular-aa-is-not-optional-once-there-are-real-lights: the world and
  entity shaders had none -- only vmw.c and the previz did -- and they got
  away with it while the only specular was one blurred lobe off the
  lightmap.  Sixteen analytic lamps put a real lobe on every plate seam and
  the crawling dots came with them.  The same three lines everywhere:
  varN=0.25*(|dFdx N|^2+|dFdy N|^2), rough=sqrt(min(rough^2+min(2 varN,
  0.18),1)).  Widen `rough`, not `alpha`, so the probe and the DFG blur
  with it, and put it AFTER the last thing that moves N -- in ent.c the
  MACHINED block is that thing, so running it with the material would
  measure a normal that is not the one being shaded with.

chromatic-aberration-has-to-be-r-squared: an even red/blue split across
  the frame is not aberration, it is a misaligned sensor, and it reads as
  one.  A lens brings the wavelengths to focus at different distances from
  the AXIS, so the split is zero in the middle and grows to the corners --
  offset = (uv-0.5) * dot(uv-0.5,uv-0.5) * k, red short and blue long,
  applied to the SCENE before the tonemap because it happens in the glass
  in front of the sensor.  k=0.0032 is about 1.3 px corner-to-corner on a
  1600-wide frame.  Measure it rather than eyeballing it: the sub-pixel
  R-to-B shift, estimated by parabolic peak fit on the correlation of the
  two channels' x-gradients, is 0.00 px at the centre and +-1 px at the
  left and right edges with OPPOSITE signs.  A build-to-build diff cannot
  prove it, because the lamp flicker runs off wall-clock time and no two
  runs render the same frame.

a-full-pool-gives-every-slot-to-the-last-writer: fx.c's dynamic light pool
  allocated with `int i = nDyn<MAXDYN ? nDyn++ : 0`, which reads as a ring
  buffer and is not one: once full, EVERY caller writes index 0, so only
  the last light added in a frame survives to fxLights.  And the pool is
  full essentially always, because story.c re-adds the glow under each
  visible item once a FRAME with a 0.06 s life, so every glow sits in it as
  three or four copies of itself -- nDyn 16 by frame three of a new game.
  The muzzle flash is added EARLY (vmwStep, top of the frame) and the item
  glows LATE (storyDrawWorld), so the gunshot was overwritten before
  fxLights ran, every shot, for the whole game: the screen flash quad and
  vmw's own uMuzC were doing all the work and the room never saw the shot.
  A full pool now drops its WEAKEST entry by fxLights' own weight, and a
  light at the same place replaces the one already there.  Any fixed pool
  in this tree that falls back to slot 0 has the same bug, and frame order
  decides who loses.

an-instrument-that-photographs-nothing-still-photographs: `-gun N` armed
  the weapon inside appInit and storyInit() ran eleven lines later calling
  vmwDisarmAll(), so OWNED went back to zero and vmwTryFire returned on its
  first line.  Every `-gun N -fire` screenshot ever taken was of a gun that
  had not fired -- and it looked fine, because a gun in the frame is what
  the eye checks for.  It was caught by differencing the shot against the
  SAME shot without -fire: byte-for-byte identical apart from the grain.
  A visual instrument is only proved by its null case; and vmwForce has to
  load the magazine and spin the rotary's barrels too, or the gate that
  rejects an empty gun rejects it silently.

a-cap-of-twenty-is-a-corridor-number: directorStep throttles spawning on
  zomCount()>=20, which is right for a ship you walk through and wrong for
  the one fight the player is meant to be swamped by.  E-12 asked for 38
  bodies over ninety seconds from two entries at the FAR end of a 32 m
  hall, and the measured result was the gallery empty for eight seconds at
  a stretch with twelve alive and none within twenty metres -- which reads
  exactly as "몬스터가 안 나온다".  136 bodies, six entries including two
  inside the hall, and a cap of 44 while F.finalDefense: live count now
  ramps 5 -> 34 and the nearest sits under 5 m for the second half.  MAXZ
  is 80 and a corpse clears in eight seconds, so the pool was never the
  limit; the number was a choice.

  AND THE CAP COUNTED THE CORPSES, so forty-four was never forty-four
  alive.  zomCount() is every USED slot and a body holds its slot for the
  eight seconds it takes to clear (zom.c, deadT>8.f) -- so a player killing
  five a second spent forty of the forty-four on the dead and the director
  stopped sending any.  The fight got QUIETER THE BETTER YOU PLAYED, which
  is the wrong way round for the one fight in the game that is about being
  swamped, and it is why the same build reads as full to -auto and as empty
  to a person.  The final defence reads zomLive() now.  The corridors
  deliberately keep zomCount(): every other fight in the ship was tuned
  against the old number, and changing those is a difficulty change nobody
  asked for.  The log line prints zomLive() and the cap tested zomCount(),
  which is exactly how this hid -- an instrument and the thing it measures
  must count the same bodies.

a-spawn-that-cannot-be-placed-is-a-spawn-that-is-deleted: two paths in
  directorStep threw a body away with no log line and no retry, so the wave
  the player was waiting for was simply smaller and nothing anywhere said
  so.  (1) pushOutOfView answers spec 6.3 -- nothing appears in front of
  you inside twelve metres -- by rotating the written point sixty degrees
  around the PLAYER at the same radius, and that rotation is blind to
  geometry.  The escape gallery is a 25 m hall with a bulkhead down its
  whole east side, so what it landed on was as often wall as deck; freeSpot
  then failed and the body was dropped.  Which means WHERE YOU STAND AND
  WHICH WAY YOU FACE decided how many arrived, and the same fight twice
  running was not the same fight -- that is the "sometimes they come and
  sometimes they do not" half of it.  Every candidate is now tested for
  standable deck BEFORE it is accepted, at thirty degrees a step, with a
  wider ring behind the player as the fallback.  (2) A spawn the cap turns
  away keeps its PEND slot and retries every half second while waves keep
  firing on the clock, and pend() simply RETURNS when all of them are held:
  whole waves gone.  MAXPEND is 192 against a 136-body schedule.  Measured
  over one -scene 10 -auto 130: 4 deleted of 123 scheduled, then 0 of 145,
  and a failure now prints "spawn  NO SPOT" instead of nothing.  Anything
  in this tree that can fail to place a thing has to say that it did.

a-density-floor-needs-a-clock-not-a-headcount: E-12 is thirteen waves and
  136 bodies over eighty seconds -- 1.7 a second, which a player with the
  rotary cannon outruns, so the ending played out quiet, which is the
  opposite of what the ninety seconds are for.  A floor under it is right
  and BOTH obvious ways of writing it are wrong.  Reading the living alone
  cascades: every row spends its lead time in PEND before it is anything at
  all, so the floor sees an empty gallery, pulls the next wave in, and does
  it again on the very next frame -- all thirteen waves queued inside one
  second, nine schedule wraps, and live pinned at the cap from t=0, which
  is not pressure, it is a fight with no shape.  Adding pendIn() to the
  test fixes the cascade and hands the pacing straight back to the
  schedule, which is what was too thin in the first place: live mean 9.9,
  max 24.  What works is the LIVING PLUS A COOLDOWN --
  zomLiveIn(e)<=FINAL_FLOOR && floorCd<=0, with FINAL_FLOOR 14 and
  FINAL_FLOOR_CD 3 s -- and a schedule that WRAPS to its own second half
  while the launch clock still has more than FINAL_WRAP_AT (8 s) on it, so
  it cannot run out.  Measured before -> after: live mean 13.4 -> 17.5,
  minimum 0 -> 8, samples with fewer than eight alive 15/46 -> 0/37.
  FINAL_FLOOR is the density knob and it is close to linear in the live
  mean: 12 gave 13.1, 14 gives 17.5, 24 gave 28.8.  Tune there, and re-read
  it off `playlumi.exe -scene 10 -auto 130 -log`, not off a play session --
  -auto boards the capsule the moment CAP_AT opens, so a run ends around
  t=20 and the last twenty seconds are never in the sample.

a-line-held-in-a-fight-that-never-ends-is-a-line-fired-at-the-ending:
  DLG_HOLD_MAX exists so a wedged animal cannot stop the story, and in the
  last ninety seconds it is the wrong end of the trade -- the fight never
  stops, so every queued line arrives twelve seconds later on top of the
  five that count the launch down.  That is what "갑자기 성체입니다 대사가
  나옴" was: L-027, about a brooder, played over the countdown.  In
  F.finalDefense a non-exception line is DROPPED (said[i]=1), not deferred
  -- and the guard has to be `&& !F.boarded`, or the same branch eats the
  epilogue, and L-041 has to join the exception list, because the event
  that turns final_defense on is the event that issues it.

two-countdowns-need-two-labels: the launch clock at the top of the screen
  and the hatch clock at the capsule were both bare digits, so standing in
  the ring at T-40 the player read "40" above and "15" in the middle with
  nothing saying which was which.  Label, do not restyle: the sizes were
  already different and that told nobody anything.

octal-escapes-do-not-survive-a-python-rewrite: story.c writes Korean as
  C octal escapes ("\355\203\221" for 탑승).  A patch script that carries
  one through a non-raw Python string turns \354 into U+00EC and writes it
  back as UTF-8, so the file ends up doubly encoded and krText draws
  nothing at all -- no error, just a missing label.  Build the escape from
  the text instead: ''.join('\%03o'%b for b in s.encode('utf-8')).

a-light-pool-with-corners-in-it: the lightmap is stored PER VERTEX -- on
  AO_STEP's 1.4 m lattice for a brush face, and at the part's OWN corners
  for a kit part, which is coarser and is 83% of the ship's 1.14 M samples.
  Gouraud between two of them is a straight line and the falloff is not:
  (1-d/r) squared is a curve, so every pool of light in the ship was that
  curve replaced by a polygon -- a lozenge on the deck with the lattice's
  diagonal creased across it.  Raising the sample count is not the answer
  (the count is an AREA, and 4x the samples is 4x the bake AND 4x the
  VRAM).  render.c sums the SAME sixteen near lamps in the VERTEX shader
  (vSLD) as the fragment shader already did (slE), and the difference is
  exactly what the interpolation lost: `diff += (slE-vSLD)*visF`, with visF
  the fraction of those lamps the bake says actually reached this texel, so
  the correction is full in open light and gone in shadow.  It is ZERO AT
  EVERY VERTEX by construction, so no triangle can disagree with its
  neighbour along a shared edge -- it can only bend the inside of one.
  Measured 596 -> 603 fps, i.e. free.  `-debug 10` draws the correction
  (it is the lattice, lit up) and `-debug 11` draws the frame without it,
  which is the only honest A/B here: the lamp flicker runs off wall-clock
  and no two RUNS render the same frame.

  AND A HARD LAMP CUT POPS ONCE IT DRIVES DIFFUSE.  renderPickLamps drops
  a lamp the instant dist-radius passes SL_REACH.  That was survivable
  while the sixteen only carried a highlight; carrying the diffuse
  correction, the same step changes the shape of a pool as the player
  walks a stride.  SL_FADE spends the last 2 m of the reach on a
  smoothstep instead.  Anything else that starts reading SL_C needs to
  know it is already faded.

japanese-is-the-one-typeface-that-is-stored: Hangul is 24 letters and a
  composition rule, so kfont.c DRAWS it -- 119 strokes, five bytes each.
  Latin is a 5x7 stencil, 51 characters for 357 bytes.  Japanese has
  neither property: there is no rule that assembles a kanji out of pieces
  small enough to store, and hand-cutting strokes for three hundred of
  them is three hundred drawings, not a typeface.  So it is the exception,
  bounded the way this build bounds everything -- tools/jafont.py scans
  every Japanese string in the tree (story/*.json, storypack.py's QUESTS,
  and the octal escapes in the .c files), rasterises ONLY those glyphs
  through Windows GDI via ctypes, and writes native/src/jafont_data.h.
  435 glyphs at 24 dots is 32 KB.  The font is needed to WRITE the header
  and never to run the exe, which is the bargain nativepack.py already
  makes with node.  RE-RUN IT AFTER ANY CHANGE TO JAPANESE TEXT; a glyph
  that is not in the sheet draws as a hollow box, deliberately, and
  `playlumi.exe -font` is where that box shows up.

the-hud-vertex-carries-uv-now: hud.c had one vertex format (x,y,rgba) and
  a shader that wrote the colour straight out.  The glyph sheet needs a
  texture, and a second draw call for text would mean the overlay's z
  order stops being its queue order.  So every quad carries uv, hudRect
  points its own at an all-white row of the SAME sheet, and there is still
  one format, one batch and one draw.  hudInit lays down a 1x1 white
  placeholder because hudRect samples the sheet too and runs before
  kfont.c has built the real one.

octal-escapes-do-not-survive-a-shell-heredoc-either: the sibling of
  octal-escapes-do-not-survive-a-python-rewrite.  Writing a patch script
  in a `<<'PY'` heredoc looks safe -- the quotes stop the shell expanding
  it -- but backslashes are still collapsed on the way in, so a Python
  literal '\n' arrives as a newline and every anchor containing a C
  escape silently fails to match.  Build the escapes from chr(92), or put
  the script in a FILE and run it.

the-icon-is-read-before-the-program-runs: everything else in this build is
  drawn by code at load time, and the picture Explorer puts on the file
  cannot be -- the shell reads it out of the exe's resources without
  starting it. So it is the second exception after the Japanese sheet, and
  it is bounded the same way: tools/mkicon.py DRAWS LUMI (the egg of
  #dedace paint, the visor, face.c's two cyan eyes) at six sizes and writes
  build/icon.ico, a build product rather than an asset in the tree, and
  windres wraps it for the link. Three things that are easy to get wrong.
  (1) Each size is drawn in its OWN pixel grid, not downsampled from the
  256 -- at 16 px the visor is three rows and the lower band and the LED
  have to not exist, which is what mkicon.py's detail() ladder is. (2) A
  64 as a BGRA DIB is 16,936 bytes and as a PNG entry 4,221, and Windows
  has read PNG icon entries since Vista: 46,883 for the whole .ico rather
  than 59,598, and the exe grew by 48,640. (3) WNDCLASSA has no hIconSm
  field -- that is WNDCLASSEX -- so wc.hIcon covers alt-tab and the
  taskbar and the 16 px title-bar slot needs LoadImage at SM_CXSMICON and
  a WM_SETICON, or the title bar shrinks the 32 and loses her eyes.
  Proved by reading the PE resource directory back: six RT_ICON of exactly
  the .ico's byte counts, plus the RT_GROUP_ICON that LoadIcon(1) asks for.

heat-belongs-to-the-gun-not-to-the-hands: vmw.c's `A` is one struct for THE
  ARMS -- one recoil, one sway, one set of casings in the air -- and
  `float heat; int over;` sat in it beside them, which is right for a pose
  and wrong for a barrel.  So the rotary run up to overheat and then
  holstered handed its heat to whatever came up next: press 2 and the
  double-barrel arrives with both barrels glowing (the shader's heat branch
  is `m==1||m==12` and the shotgun's lathe stations ARE 12) and OVERHEAT on
  the HUD, so a gun that had fired twice could not be fired at all.
  `heat[3]`/`over[3]` indexed by curW now, and all three cool every frame --
  a holstered barrel is still losing heat, and only the gun in your hands
  gets the slower trigger-held rate.  Anything new in `A` that describes the
  WEAPON rather than the arms needs the same three slots; `A.spent` and
  `reloadT` are the two that still do not have them.
