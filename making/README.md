# making

Working files from the week the game was designed, before anything was
written in C. Each piece (the ship, LUMI, the guns, the props) started as a
single HTML page or a design document made in conversation with an AI
assistant. A version was saved when it looked right and then carried into
the game. They are kept here to show how each piece got to its final form.

Nothing in this folder is used by the build. The final versions live in
`ref/`, `props/`, `tools/` and `native/`. Documents are in Korean.

Every HTML page opens directly in Chrome. The pages that use three.js load
it from cdnjs, so they need a network connection.

## env/ — the ship

| File | What it is |
|---|---|
| `derelict_ship_level_map.svg`, `compressed_3zone_ship_map.svg` | The first level maps, before the deck plan |
| `ship_deck_plan_v1.svg` … `v5_hull_gallery.svg` | The deck plan through five revisions |
| `ship_deck_plan_v6_original.svg` | v6 as first drawn. The version the build reads is `ref/ship_deck_plan_v6_fixed.svg`, edited after this one |
| `derelict-ship-beat-sheet.md` | What happens in each section of the route |
| `derelict-ship-prop-list.md` | The prop list per room, before the props were built |
| `derelict-ship-procedural-priority.md` | The props ordered from easiest to generate in code, each with a one-line recipe |
| `infestation_stages_corridor.svg`, `infestation-procedural-demo.html`, `infestation-raymarch-demo.html` | A test of the corridor being overgrown in stages. It was rolled back and never reached the game |

## lumi/ — the drone

| File | What it is |
|---|---|
| `lumi_v1.html` … `lumi_v5.html` | LUMI's viewer page, version by version. v1 is an egg with a gyro ring; v3 drops the ring and gives her a lens; v4 is the first built from the concept sheet. v6, the frozen design, is `props/lumi_v02/lumi_v6.html` |
| `lumi-concept-sheet.png` | The concept sheet made with ChatGPT: ten display states and the five steps of the ending dock |

## props/ — the room props

| File | What it is |
|---|---|
| `bench-early/propview.html` + `propkit.js` | An early version of the prop bench. The final one is `props/props_v01/`. Its textures load only over HTTP: run `python -m http.server` inside `bench-early/` and open `http://localhost:8000/propview.html` |
| `capsule-concept.png` | Concept for the escape capsule and the six-slot energy pack interface, made with ChatGPT |
| `prop-placement-plan.md` | Which of the 37 props go in which of the 10 rooms |
| `PORT_INVENTORY.md` | The inventory taken before the props were ported from JS to C |

## weapons/ — the guns

| File | What it is |
|---|---|
| `procedural-viewmodel.html` | The weapon bench: all three guns, their procedural textures with live sliders, and a first-person mode. The game's guns are ported from this page |
| `PROCEDURAL-WEAPONS.md` | The technical spec for the procedural weapon system, written from the bench so it could be carried into the game |

## Not included

Conversation transcripts with the AI assistants, screenshots of the
desktop, and duplicate downloads of the same file.
