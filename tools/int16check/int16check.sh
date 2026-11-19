#!/bin/sh
# int16check: is the game code correct with a 16-bit int, as on the 65816?
# (ROADMAP.md, step 2a.) Runs in Linux / WSL; needs Emscripten (its clang,
# emcc, node) and Python 3, like the web build. Run from the repository root
# after a web build (it uses the generated level and graphics data), with
# SUPERDEVKIT_HOME set (the console API's headers, Rachael for the tests):
#
#   SUPERDEVKIT_HOME=<home> tools/int16check/int16check.sh [build-web]
#
# 1. Compiles every file of src/game for MSP430, whose int is 16 bits, with
#    -Werror and all conversion warnings: constants, shifts and conversions
#    that only fit a 32-bit int stop here.
# 2. Finds every expression computed in int whose result is then compared,
#    shifted right, divided or widened to 32 bits: there a 16-bit int could
#    give a different result than the PC.
# 3. Runs the game tests (tests/test_game.c) with each of those expressions
#    wrapped in a probe that records its values.
# 4. Fails if any of them ever left the range of a 16-bit int, and lists the
#    ones the tests never reached, to be checked by hand.
set -e
: "${SUPERDEVKIT_HOME:?set SUPERDEVKIT_HOME to the superdevkit home}"
SDK=$SUPERDEVKIT_HOME
BUILD=${1:-build-web}
GEN=$BUILD/generated
CLANG=${CLANG:-/opt/emscripten-llvm/bin/clang}
EMCC=${EMCC:-emcc}
DIR=tools/int16check
OUT=$BUILD/int16check
[ -f "$GEN/gfx_data.h" ] || { echo "int16check: no generated data in $GEN: build $BUILD first"; exit 2; }
export CLANG

echo "int16check: compiling src/game with a 16-bit int"
for debug in "" "-DBB_DEBUG=1"; do
    for f in src/game/*.c "$GEN"/level_data.c "$GEN"/gfx_data.c; do
        "$CLANG" --target=msp430 -std=c99 -fsyntax-only -Werror -Wall -Wextra -Wpedantic -Wconversion \
            -Wsign-conversion $debug -Isrc -I"$GEN" -I"$SDK/lib/include" "$f"
    done
done

rm -rf "$OUT"
mkdir -p "$OUT"
python3 "$DIR/instrument.py" "$DIR/stub" "$OUT" "$GEN"
"$EMCC" -O2 -std=c99 -DBB_DEBUG=1 -Wno-typedef-redefinition -Isrc -I"$GEN" -I"$SDK/lib/include" -I"$SDK/lib/rachael" "$OUT"/*.c "$DIR/probe.c" \
    "$SDK/lib/rachael/ppu.c" "$SDK/lib/rachael/render.c" "$GEN"/level_data.c "$GEN"/gfx_data.c tests/test_game.c \
    -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=4MB -sEXIT_RUNTIME=1 -o "$OUT/test_game.js"
(cd "$OUT" && node test_game.js > test_game.log) || { cat "$OUT/test_game.log"; echo "int16check: the game tests failed"; exit 1; }
python3 "$DIR/report.py" "$OUT/sites.txt" "$OUT/int16_ranges.txt"
