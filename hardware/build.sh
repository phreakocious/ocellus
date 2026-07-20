#!/usr/bin/env bash
# Build one part: export the STL, render it, check it.
# Renders the EXPORTED STL, never the OpenCSG preview -- preview z-fights on
# coincident faces and shows holes that are not there.
set -euo pipefail
cd "$(dirname "$0")"

PART="${1:-body}"
mkdir -p out
STL="out/${PART}.stl"

# Delete first. Otherwise a failed export leaves the PREVIOUS run's STL in place
# and every check below silently passes on a stale file.
rm -f "$STL"

echo "== export $PART"
openscad -o "$STL" -D "part=\"$PART\"" fob.scad 2>&1 | tee out/_build.log

# OpenSCAD only WARNS on a missing import() and still exits 0, happily emitting a
# solid puck with no board pocket at all. The cavity comes from an import(), so
# treat that warning as fatal.
if grep -q "Can't open import file" out/_build.log; then
    echo "FATAL: import() failed -- the board cavity is empty. See out/_build.log" >&2
    exit 1
fi
[ -s "$STL" ] || { echo "FATAL: $STL was not written." >&2; exit 1; }

echo "== render"
printf 'import("%s.stl");\n' "$PART" > out/_view.scad
openscad -o "out/${PART}.png" --imgsize=800,800 \
    --camera=0,0,9,60,0,205,150 --colorscheme=Tomorrow out/_view.scad 2>/dev/null
openscad -o "out/${PART}-back.png" --imgsize=800,800 \
    --camera=0,0,9,240,0,25,150 --colorscheme=Tomorrow out/_view.scad 2>/dev/null

echo "== check"
python3 expect.py "$PART"
