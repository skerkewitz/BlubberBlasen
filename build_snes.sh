#!/bin/sh
# Builds the SNES ROM with superdevkit (ADR-0007), in Linux or WSL:
#
#   SUPERDEVKIT_HOME=<superdevkit home> ./build_snes.sh [dampf options]
#
# Output: build/snes/blubberblasen.sfc, with blubberblasen.dbg (C source and globals for Mesen),
# the timing report blubberblasen.timing.txt and the assembly next to it.
#
# The level and graphics data come from the host tools map2c and gfx2c as plain C. By default the
# tools from the Windows build (build/tools/Debug) are used; HOSTTOOLS=<dir> picks others.
# dampf compiles the game, the data and the kit's library for the SNES (lib/snes: start-up, the
# splash screen, the main loop that calls game_init, game_tick and game_vblank) as one program,
# and assembles and links the ROM itself.
#
# To check the ROM against the PC, tick by tick (tools/snescheck, README "SNES build"):
#   build/Debug/snescheck.exe -o build/snes/check.txt     (or the snescheck of a Linux build)
#   "$SUPERDEVKIT_HOME/dev/replay.sh" build/snes/blubberblasen.sfc build/snes/check.txt
set -e
cd "$(dirname "$0")"
: "${SUPERDEVKIT_HOME:?set SUPERDEVKIT_HOME to the superdevkit home (the folder with bin/dampf and lib/)}"
HOSTTOOLS=${HOSTTOOLS:-build/tools/Debug}
OUT=build/snes
GEN=$OUT/gen
EXE=
[ -x "$HOSTTOOLS/map2c.exe" ] && EXE=.exe
[ -x "$HOSTTOOLS/map2c$EXE" ] || { echo "build_snes: no map2c in $HOSTTOOLS (build the PC version first, or set HOSTTOOLS)"; exit 1; }

rm -rf build/snes/gen          # the args file takes every .c file there: nothing stale
mkdir -p "$GEN"
LEVELS=$(ls assets/levels/*.map | sort)
# shellcheck disable=SC2086
"$HOSTTOOLS/map2c$EXE" -o "$GEN/level_data.c" $LEVELS > /dev/null
# shellcheck disable=SC2086
"$HOSTTOOLS/gfx2c$EXE" -o "$GEN/gfx_data.c" -H "$GEN/gfx_data.h" assets/gfx/gfx.txt $LEVELS > /dev/null

"$SUPERDEVKIT_HOME/bin/dampf" build @blubberblasen.args \
    -I "$SUPERDEVKIT_HOME/lib/include" "$SUPERDEVKIT_HOME/lib/snes/" \
    -o "$OUT/blubberblasen.sfc" "$@"
