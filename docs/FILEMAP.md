# File map

Every tracked file is listed here, grouped by what it does. Line counts are
from the current tree. For the line a function or section sits on, see
`docs/symbols.json` (rewritten by `python tools/mkindex.py`).

Files marked **generated** are written by a tool. Do not edit them by hand;
change the tool's input and run the tool again.

## How the pieces connect

```
ref/ship_deck_plan_v6_fixed.svg ─┐
src/sector-c/ (browser engine) ──┼─ tools/*.py ──► src/derelict/map.html (browser previz)
tools/kit.js, weapons.js ... ────┘                         │
                                                           ▼
story/*.json ─────── tools/storypack.py ──► native/src/story_data.h
map.html ─────────── tools/nativepack.py ─► native/src/plan_data.h
props/props_v01/ ─── tools/propglsl.py ───► native/src/props_glsl.h
                 └── tools/propexport.js ─► native/src/props_gen.c
Japanese strings ─── tools/jafont.py ─────► native/src/jafont_data.h
                                                           │
                         native/src/*.c + native/build.sh ─► playlumi.exe
```

The game itself is only `native/`. Everything above the last line exists to
design the ship and to write the five generated files. Those files are
committed, so `sh native/build.sh` works from a fresh clone without Python
or Node.

## Top level

| Path | What it is |
|---|---|
| `README.md` | What the game is, how to build it |
| `LICENSE` | MIT |
| `CLAUDE.md` | Working manual for AI-assisted development of this repo: build chain, which file owns which change, house rules |
| `docs/` | This map, the engineering notes, the original design docs |
| `native/` | **The game.** C99, Win32, OpenGL 3.3, no libraries |
| `tools/` | Generators and the checks that compare the exe against them |
| `src/` | The browser previz the ship was designed and lit in |
| `props/` | LUMI's geometry generator and the room-prop bench |
| `story/` | Dialogue, scenes and encounters as data |
| `ref/` | The deck plan the whole ship is built from |
| `archive/` | Documents from the contest period. Kept as a record and not maintained |
| `making/` | The prototypes each piece started from: deck plan revisions, LUMI's viewer v1-v5, an early prop bench, the weapon bench, two concept sheets. Not used by the build; see `making/README.md` |

## docs/

| File | What it is |
|---|---|
| `FILEMAP.md` | This file |
| `reports/PROJECT_INDEX.md` `.json` | A short summary of the layout, entry points and data flow, for a first look |
| `engineering-notes.md` | The 75 problems hit during development, each with its cause, how it was found and the fix. Reads as a list of traps in real-time rendering, porting JS to C, game AI and procedural audio |
| `symbols.json` | **generated** by `tools/mkindex.py`: the sections and exported symbols of every source file, with line numbers |
| `design/gdd.md` | The original game design document (Korean) |
| `design/story-script.md` | The original story and dialogue script (Korean). The shipped dialogue is `story/dialogue.json` |

## native/ — the game

`native/build.sh` makes two gcc calls and one windres call:
`sh native/build.sh` builds a release, and `sh native/build.sh debug` builds
with `-O0 -g`. The output is `native/build/playlumi.exe`. `vmw.c` is
compiled without `-ffast-math`, and `build.sh` explains why.

53 hand-written files (23,164 lines) and 5 generated ones (12,654 lines).

### Platform and frame

| File | Lines | What it owns |
|---|--:|---|
| `plat.c` | 332 | Win32 window, WGL context, thread pool, `main` |
| `app.h` | 32 | The interface between the platform layer and the game |
| `gl33.c` `.h` | 33 + 180 | The OpenGL 3.3 entry points, loaded by hand |
| `game.c` | 932 | Player, input, startup sequence, the order a frame is drawn in, every command-line flag |
| `menu.c` `.h` | 283 + 26 | Title card, language page, the eyelid wake-up |
| `shot.c` | 98 | `-shot`: writes a PNG of the frame (the encoder is hand-written) |
| `m.h` | 112 | Math: hash, periodic noise, Worley noise, matrices |

### Building the ship at load time

| File | Lines | What it owns |
|---|--:|---|
| `level.c` | 268 | Brushes, faces, lamps, zones, door positions, the solid grid; `deckAt`/`deckNear` |
| `ship.h` | 152 | The ship's data structures |
| `tex.c` | 532 | Material compiler: 27 operators, 40 recipes, run in parallel |
| `mesh.c` `.h` | 852 + 86 | Mesh operators: n-gons, extrude, inset, creases, Catmull-Clark |
| `kit.c` `.h` | 355 + 108 | The corridor kit's machinery |
| `kitparts.c` | 632 | Floor, ceiling, skirt, cove, band and column parts |
| `kitwalls.c` `.h` | 645 + 11 | Wall, fillet and corner parts |
| `geom.c` | 273 | Lightmap lattice and draw ranges |
| `bake.c` | 713 | Radiosity light bake, reflection probes, lamp flicker groups |

### Rendering

| File | Lines | What it owns |
|---|--:|---|
| `render.c` | 996 | Scene shader (GGX), HDR target, MSAA, bloom, tone map, colour grade, the 16 nearest lamps |
| `torch.h` | 113 | The flashlight beam as GLSL, shared by three shaders |
| `atmos.c` | 261 | Dust and light coronas |
| `ent.c` `.h` | 687 + 72 | Instanced entities: creatures, props, doors, LUMI |
| `prim.c` `.h` | 346 + 37 | The 13 instanced primitive solids |
| `fx.c` `.h` | 684 + 53 | Dynamic lights, particles, bullet decals, debris |
| `hud.c` `.h` | 256 + 34 | HUD quad batcher, 5×7 Latin font |
| `kfont.c` `.h` | 381 + 26 | Hangul built from 119 strokes, and the Japanese glyph lookup |

### Things in the world

| File | Lines | What it owns |
|---|--:|---|
| `vmw.c` `.h` | 2111 + 42 | The three procedural weapons: meshes, textures, animation, firing |
| `zom.c` `.h` | 1630 + 66 | The six-legged creature: body graph, gait, AI, navigation lattice |
| `lumi.c` `.h` | 688 + 38 | LUMI's behaviour: following, leading, flying, her lamp |
| `lumimesh.c` `.h` | 749 + 46 | LUMI's shape, a 1:1 port of `props/lumi_v02/lumi_geometry.js` |
| `face.c` `.h` | 347 + 40 | LUMI's display face, drawn as a shader |
| `doors.c` | 422 | Automatic bulkhead doors |
| `props.c` `.h` | 813 + 47 | Room props: which room asks for what, and where each one is placed |
| `propkit.c` `.h` | 885 + 210 | Prop builders, ported from `props/props_v01/propkit.js` |
| `audio.c` `.h` | 1201 + 48 | Procedural audio: voices, filters, reverb, every sound effect, `waveOut` |

### Story

| File | Lines | What it owns |
|---|--:|---|
| `story.c` `.h` | 3082 + 40 | The director: scenes, dialogue, waves, checkpoints, objectives, the ending |

### Generated

| File | Lines | Written by | From |
|---|--:|---|---|
| `plan_data.h` | 6754 | `tools/nativepack.py` | `src/derelict/map.html` |
| `story_data.h` | 1076 | `tools/storypack.py` | `story/*.json` |
| `props_gen.c` | 3207 | `tools/propexport.js` | `props/props_v01/propkit.js` |
| `props_glsl.h` | 260 | `tools/propglsl.py` | `props/props_v01/propview.html` |
| `jafont_data.h` | 1357 | `tools/jafont.py` | every Japanese string (needs Windows fonts) |

`native/build/icon.ico` is drawn by `tools/mkicon.py` during the build and is
not committed.

## tools/

Python uses the standard library only, and Node uses built-in modules only.
Run every tool from the repository root.

### The previz chain, in order

```sh
python tools/build_ship.py && python tools/splice.py && python tools/weapons.py \
  && python tools/doors.py && python tools/decals.py && python tools/deckmap.py
```

| File | Lines | What it does |
|---|--:|---|
| `deckplan.py` | 138 | The SVG deck plan, transcribed into rooms and passages (imported, not run) |
| `deckgrid.py` | 140 | The deck plan, placed on the kit's 1.2 m module grid (imported) |
| `shipmaterials.py` | 1891 | Every material recipe (imported) |
| `shipdetail.py` | 252 | Ribs, pipes, grating, door frames (imported) |
| `shiplights.py` | 175 | Lamps and reflection zones (imported) |
| `build_ship.py` | 432 | Deck plan to brushes → `_shell.js`, `_lights.js` |
| `splice.py` | 794 | Grafts the ship onto `src/sector-c/` → `src/derelict/derelict.html` |
| `weapons.py` + `weapons.js` | 78 + 1328 | Splices in the three guns |
| `doors.py` + `doors.js` | 211 + 309 | Splices in the doors |
| `decals.py` + `decals.js` | 152 + 227 | Splices in bullet decals |
| `deckmap.py` | 1248 | Lays the kit over the plan → `src/derelict/map.html`, `_map_modules.js` |
| `kit.js` | 2326 | The 21 corridor kit parts |
| `kitview.py` | 723 | The part bench → `src/derelict/kit.html` |
| `_shell.js` `_lights.js` `_map_modules.js` | | **generated** splice payloads |

### Writing the exe's data

| File | Lines | What it does |
|---|--:|---|
| `nativepack.py` | 230 | `map.html` → `native/src/plan_data.h` |
| `storypack.py` | 485 | `story/*.json` → `native/src/story_data.h` |
| `propexport.js` | 251 | `propkit.js` → `native/src/props_gen.c` |
| `propglsl.py` | 117 | `propview.html` → `native/src/props_glsl.h` |
| `jafont.py` | 315 | Japanese glyphs → `native/src/jafont_data.h` (Windows only) |
| `mkicon.py` | 238 | Draws LUMI as the exe icon (`build.sh` runs it) |
| `mkindex.py` | 133 | → `docs/symbols.json` |

### Checks

Each check prints numbers that should match what the exe prints.

| File | Lines | Compare against |
|---|--:|---|
| `vmwcheck.js` | 116 | `playlumi.exe -vmw` (weapon meshes) |
| `lumicheck.js` | 48 | `playlumi.exe -lumimesh` (LUMI's 35 parts) |
| `matview.js` | 91 | `playlumi.exe -mats` (materials, as PNG) |
| `wavan.py` | 120 | `playlumi.exe -wav` (loudness and noisiness of each sound) |
| `kitcost.js` | 49 | Face count of a module list, before building it |
| `shot.js` | 137 | Screenshots a previz page in Chrome |

## src/ — the browser previz

All four pages open directly in Chrome. `?fast` shortens the light bake.

| File | Lines | What it is |
|---|--:|---|
| `sector-c/sector-c-containment.html` | 7205 | The WebGL engine the previz is built on (hand-written) |
| `derelict/derelict.html` | 16105 | **generated**, intermediate build step |
| `derelict/map.html` | 16293 | **generated**, the whole ship, playable |
| `derelict/kit.html` | 12328 | **generated**, the kit part bench |

## props/

| File | Lines | What it is |
|---|--:|---|
| `lumi_v02/LUMI-SPEC.md` | 190 | LUMI's design spec. Her shape is frozen |
| `lumi_v02/lumi_geometry.js` | 600 | The generator for her mesh (35 parts, 14,772 triangles) |
| `lumi_v02/lumi_v6.html` | 793 | Bench page: LUMI and her display states in the browser |
| `props_v01/propkit.js` | 2991 | The 37 room props as builder calls |
| `props_v01/propview.html` | 1137 | Bench page for props, including the surface shader library |

## story/

| File | Lines | What it is |
|---|--:|---|
| `dialogue.json` | 547 | Every line, in Korean, English and Japanese |
| `scenes.json` | 324 | Scenes, story flags, sink stages, checkpoints |
| `spawns.json` | 876 | Encounters and creature waves |

## ref/

| File | What it is |
|---|---|
| `ship_deck_plan_v6_fixed.svg` | The deck plan. `tools/deckplan.py` reads it, and every coordinate in `story/` is a point on it |

## archive/

See `archive/README.md`. These documents describe the project during the
contest, and much of what they say about file names, sizes and progress is
out of date.
