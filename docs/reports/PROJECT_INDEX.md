# PROJECT_INDEX

Quick map of the repo. For detail: `docs/FILEMAP.md` (every file and its
role), `docs/symbols.json` (line of every section and export), `CLAUDE.md`
(which file owns which change).

## What it is

GOODBYE LUMI, a first-person SF horror shooter in one Windows exe under
1,474,560 bytes. C99 + Win32 + OpenGL 3.3, no libraries, no asset files.
A browser previz (HTML/WebGL) designs the ship; Python/Node tools turn it
into C data. No package manager, no install step, no test suite: checks
compare exe output against tool output.

## Entry points

| Entry | Role |
|---|---|
| `native/build.sh` | build → `native/build/playlumi.exe` (`debug` arg for -O0 -g) |
| `native/src/plat.c` `WinMain` | process entry, window, GL, thread pool |
| `native/src/game.c` | boot order, frame order, all command-line flags |
| `src/derelict/map.html` | previz, open in Chrome (`?fast`) |

## Layout

| Path | Role |
|---|---|
| `native/src/` | the game (hand-written C) + 5 generated data files |
| `tools/*.py` | previz chain + packers writing `native/src` data |
| `tools/*.js` | spliced payloads (`kit`, `weapons`, `doors`, `decals`), packers, checks |
| `tools/_*.js` | generated, never edit |
| `src/sector-c/` | hand-written WebGL engine the previz grafts onto |
| `src/derelict/` | generated previz pages, never edit |
| `props/lumi_v02/` | LUMI spec + frozen mesh generator + bench |
| `props/props_v01/` | room props (`propkit.js`) + bench with surface GLSL |
| `story/*.json` | dialogue (ko/en/ja), scenes, encounters |
| `ref/*.svg` | deck plan, input to `tools/deckplan.py` |
| `docs/` | FILEMAP, engineering notes, design docs, this index |
| `archive/` | contest-era docs, unmaintained |

## Data flow

```
deck plan svg + sector-c engine + tools/*.js
  → build_ship → splice → weapons → doors → decals → deckmap → map.html
map.html        → nativepack.py  → plan_data.h
story/*.json    → storypack.py   → story_data.h
propkit.js      → propexport.js  → props_gen.c
propview.html   → propglsl.py    → props_glsl.h
ja strings      → jafont.py      → jafont_data.h   (Windows fonts)
native/src/*    → build.sh       → playlumi.exe    (+ mkicon.py icon)
```

## Checks (exe flag ↔ tool)

`-vmw` ↔ `vmwcheck.js` · `-lumimesh` ↔ `lumicheck.js` · `-mats` ↔
`matview.js` · `-wav` → `wavan.py` · `-shot` / `shot.js` for pictures ·
`-auto N -log` plays itself · `-probe` prints the deck as ASCII.

## Toolchain

w64devkit gcc at `C:\w64devkit` (not on PATH; `build.sh` adds it),
Python 3 stdlib, Node built-ins, Chrome for previz screenshots.
