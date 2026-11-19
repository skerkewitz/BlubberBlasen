#!/bin/sh
# Builds the SNES ROM (ROADMAP.md, stage 3) with PVSnesLib in Linux or WSL:
#
#   PVSNESLIB_HOME=~/devkit/pvsneslib-4.6.0 src/platform/snes/build.sh
#
# Output: build/snes/blubberblasen.sfc (and .sym for debugging emulators).
#
# The level and graphics data come from the host tools map2c and gfx2c with
# --snes: assembly for the large arrays, one ROM section each (816-tcc
# would otherwise copy them into RAM). By default the tools from the Windows
# build (build/tools/Debug) are used; HOSTTOOLS=<dir> picks others.
#
# Steps per C file, as in PVSnesLib's snes_rules: 816-tcc (C to assembly),
# 816-opt (optimises the assembly), wla-65816 (assembles); then wlalink.
set -e
: "${PVSNESLIB_HOME:?set PVSNESLIB_HOME to the PVSnesLib folder}"
HOSTTOOLS=${HOSTTOOLS:-build/tools/Debug}
OUT=build/snes
GEN=$OUT/gen
OBJ=$OUT/obj
TCC=$PVSNESLIB_HOME/devkitsnes/bin/816-tcc
OPT=$PVSNESLIB_HOME/devkitsnes/tools/816-opt
AS=$PVSNESLIB_HOME/devkitsnes/bin/wla-65816
LD=$PVSNESLIB_HOME/devkitsnes/bin/wlalink
LIB=$PVSNESLIB_HOME/pvsneslib/lib/LoROM_SlowROM
EXE=
[ -x "$HOSTTOOLS/map2c.exe" ] && EXE=.exe

mkdir -p "$GEN" "$OBJ"
LEVELS=$(ls assets/levels/*.map | sort)

echo "snes: data"
"$HOSTTOOLS/map2c$EXE" -o "$GEN/level_data_snes.c" --snes "$GEN/level_data.asm" $LEVELS > /dev/null
"$HOSTTOOLS/gfx2c$EXE" -o "$GEN/gfx_data_snes.c" -H "$GEN/gfx_data.h" --snes "$GEN/gfx_data.asm" \
    assets/gfx/gfx.txt $LEVELS > /dev/null

# every assembly file includes hdr.asm from the folder it is assembled in
cp src/platform/snes/hdr.asm "$OBJ/hdr.asm"

compile() {   # <c file> <include flags...>
    src=$1
    shift
    name=$(basename "$src" .c)
    "$TCC" -I"$PVSNESLIB_HOME/devkitsnes/include" "$@" -Wall -c "$src" -o "$OBJ/$name.ps"
    "$OPT" -i "$OBJ/$name.ps" -o "$OBJ/$name.asm" > /dev/null
}

echo "snes: compiling"
for f in src/game/*.c "$GEN/level_data_snes.c" "$GEN/gfx_data_snes.c"; do
    compile "$f" -Isrc -I"$GEN"
done
compile src/platform/snes/main.c -I"$PVSNESLIB_HOME/pvsneslib/include" -Isrc
cp "$GEN/level_data.asm" "$GEN/gfx_data.asm" src/platform/snes/glue.asm "$OBJ/"

echo "snes: assembling"
(
    cd "$OBJ"
    rm -f ./*.obj
    for f in *.asm; do
        [ "$f" = hdr.asm ] && continue
        "$AS" -d -s -x -o "${f%.asm}.obj" "$f" > "${f%.asm}.log" 2>&1 || { cat "${f%.asm}.log"; exit 1; }
    done
    echo "[objects]" > linkfile
    for f in *.obj; do
        echo "$f" >> linkfile
    done
    for f in "$LIB"/*.obj; do
        echo "$f" >> linkfile
    done
    echo "snes: linking"
    # -c: 816-tcc's files all define the same section labels (SECTIONSTART_.bss ...)
    "$LD" -d -s -v -A -c -L "$LIB" linkfile ../blubberblasen.sfc > ../link.log 2>&1 || { tail -20 ../link.log; exit 1; }
)
grep -E "^ROM:|^RAM:" "$OUT/link.log" | sed 's/^/snes: /'
echo "snes: $OUT/blubberblasen.sfc"
