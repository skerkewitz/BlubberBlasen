#!/bin/sh
# Builds the SNES ROM with superdevkit (ADR-0007, ADR-0009), in Linux or WSL:
#
#   ./build_snes.sh [--verbose | --plan | ...]   the ROM; options go to superdevkit build
#   ./build_snes.sh --data                       only the level and graphics data (build/data),
#                                                for ./superdevkitw.sh build linux|win64
#
# The kit is found through superdevkitw.sh: superdevkit.env next to it (SUPERDEVKIT_HOME=...),
# or SUPERDEVKIT_HOME in the environment.
#
# Output: build/snes/blubberblasen.sfc, with blubberblasen.dbg (C source and globals for Mesen),
# the timing report blubberblasen.timing.txt and the assembly next to it.
#
# The level and graphics data come from the game's host tools map2c and gfx2c as plain C, into
# build/data: the kit cannot run them itself. They come from the CMake build in build/ (the first
# of build/tools/Debug, build/tools/Release and build/tools that has them); HOSTTOOLS=<dir> picks
# others. Then `superdevkit build snes` does the rest as superdevkit.toml says: the sound effects
# and music.h into build/gen, and dampf compiles the game, the data and the kit's library for the
# SNES (lib/snes: start-up, the splash screen, the main loop that calls game_init, game_tick and
# game_vblank) as one program.
#
# To check the ROM against the PC, tick by tick (tools/snescheck, README "SNES build"):
#   build/Debug/snescheck.exe -o build/snes/check.txt     (or the snescheck of a Linux build)
#   "$SUPERDEVKIT_HOME/dev/replay.sh" build/snes/blubberblasen.sfc build/snes/check.txt
set -e
cd "$(dirname "$0")"
DATA=build/data
if [ -z "$HOSTTOOLS" ]; then
    for dir in build/tools/Debug build/tools/Release build/tools; do   # Visual Studio, then one-config builds
        if [ -x "$dir/map2c.exe" ] || [ -x "$dir/map2c" ]; then
            HOSTTOOLS=$dir
            break
        fi
    done
fi
EXE=
[ -x "$HOSTTOOLS/map2c.exe" ] && EXE=.exe
[ -n "$HOSTTOOLS" ] && [ -x "$HOSTTOOLS/map2c$EXE" ] || {
    echo "build_snes: no map2c in ${HOSTTOOLS:-build/tools/Debug, build/tools/Release or build/tools}:" \
         "build the PC version with CMake first (README, Getting started), or set HOSTTOOLS" >&2
    exit 1
}

rm -rf build/data              # superdevkit.toml takes every .c file there: nothing stale
mkdir -p "$DATA"
LEVELS=$(ls assets/levels/*.map | sort)
# shellcheck disable=SC2086
"$HOSTTOOLS/map2c$EXE" -o "$DATA/level_data.c" $LEVELS > /dev/null
# shellcheck disable=SC2086
"$HOSTTOOLS/gfx2c$EXE" -o "$DATA/gfx_data.c" -H "$DATA/gfx_data.h" assets/gfx/gfx.txt $LEVELS > /dev/null

if [ "$1" = "--data" ]; then
    echo "build_snes: $DATA written"
    exit 0
fi
./superdevkitw.sh build snes "$@"
