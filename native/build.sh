#!/bin/sh
# Two gcc calls and a windres, and the second gcc is the whole reason
# there is not one of it.
# There is still no makefile because there is still nothing to decide: the
# ship is a dozen translation units and a cold build is a second.
#
#   ./build.sh            release, stripped
#   ./build.sh debug      -O0 -g, console attached, asserts live
#
# THE MESH BUILDER IS COMPILED WITHOUT -ffast-math.  finalize() in vmw.c
# decides whether a welded corner has a tangent of its own by whether a sum
# of face tangents CANCELS -- around the flat disc that closes a lathe it
# does, and the nothing it leaves is the signal to hand that corner an axis
# instead.  -ffast-math reassociates the sum, the cancellation stops being
# exact, and 44 of the double-barrel's 5,808 corners take a normalised noise
# direction rather than the axis.  node tools/vmwcheck.js counts those
# corners on both sides of the port; with vmw.c built plain the two counts
# agree, and with it fast-mathed they do not.  Nothing else in the ship
# tests a float for having cancelled, so nothing else needs the exemption.
#
# AND AN ICON, WHICH IS THE ONE THING WINDOWS WILL NOT LET THE PROGRAM
# DRAW FOR ITSELF: the picture Explorer puts on the file is read out of
# the exe's resources before a line of it runs.  tools/mkicon.py draws
# LUMI and writes build/icon.ico -- a build product, not an asset in the
# tree -- and windres wraps it as a COFF object the link picks up.  It is
# regenerated only when the script is newer than the icon, because the
# drawing is a second and a rebuild is not.
set -e
cd "$(dirname "$0")"
export PATH="/c/w64devkit/bin:$PATH"

PY=$(command -v python || command -v python3)
if [ ! -f build/icon.ico ] || [ ../tools/mkicon.py -nt build/icon.ico ]; then
  mkdir -p build
  "$PY" ../tools/mkicon.py
fi
windres --include-dir build build/icon.rc -O coff -o build/icon.res

SRC="src/shot.c src/plat.c src/menu.c src/props.c src/propkit.c src/props_gen.c src/gl33.c src/game.c src/fx.c src/level.c src/tex.c src/mesh.c src/kit.c src/kitparts.c src/kitwalls.c src/prim.c src/lumimesh.c src/face.c src/ent.c src/zom.c src/lumi.c src/story.c src/doors.c src/hud.c src/kfont.c src/atmos.c src/bake.c src/geom.c src/render.c src/audio.c"
EXACT="src/vmw.c"
LIBS="-lopengl32 -lgdi32 -luser32 -lwinmm"
WARN="-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type -Wno-misleading-indentation"

mkdir -p build
if [ "$1" = "debug" ]; then
  gcc -std=c99 -O0 -g $WARN -DDEBUG -o build/playlumi.exe \
      $SRC $EXACT build/icon.res $LIBS -lm
  echo "debug build: $(stat -c%s build/playlumi.exe) bytes"
else
  gcc -std=c99 -O2 -fno-ident -fno-asynchronous-unwind-tables \
      $WARN -c $EXACT -o build/vmw.o
  gcc -std=c99 -O2 -ffast-math -fno-ident -fno-asynchronous-unwind-tables \
      $WARN -o build/playlumi.exe $SRC build/vmw.o build/icon.res \
      $LIBS -lm -mwindows
  strip -s build/playlumi.exe
  SZ=$(stat -c%s build/playlumi.exe)
  echo "playlumi.exe  $SZ bytes   ($(( 1474560 - SZ )) left of the 1,474,560 cap)"
fi
