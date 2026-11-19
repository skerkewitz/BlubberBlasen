#!/bin/sh
# Builds the compiler benchmark (bench.c) twice, from the repository root:
#
#   PVSNESLIB_HOME=~/devkit/pvsneslib-4.6.0 CALYPSI_HOME=<calypsi folder> tools/snesbench/build.sh
#
# Output: build/bench/tcc.sfc (816-tcc, as src/platform/snes/build.sh builds
# the game) and build/bench/calypsi.sfc (Calypsi, CALYPSI_OPT, default
# "-O2 --speed --no-cross-call": the cross-call optimisation moves
# repeated code into small subroutines and costs about 20 % speed; -DBENCH_FASTROM there switches FastROM on). Run both with
# tools/snesbench/run.sh. ONLY=calypsi skips the 816-tcc build; BENCH_NAME=<name>
# names the Calypsi ROM (default calypsi); CALYPSI_LDOPT adds linker options
# (-g with -g in CALYPSI_OPT: line information). CALYPSI_SRC=<folder> takes
# the game sources for Calypsi from a copy of src (patched experiments);
# CALYPSI_ASM=<.s files> links hand-written functions in addition.
#
# The PVSnesLib functions the game calls are renamed to the empty stubs in
# bench.c (bb_*), so neither ROM touches the hardware: only the compiled game
# code is measured.
set -e
: "${PVSNESLIB_HOME:?set PVSNESLIB_HOME to the PVSnesLib folder}"
: "${CALYPSI_HOME:?set CALYPSI_HOME to the Calypsi folder (with bin/cc65816)}"
CALYPSI_OPT=${CALYPSI_OPT:-"-O2 --speed --no-cross-call"}
CALYPSI_MODEL=${CALYPSI_MODEL:-"--code-model large --data-model large"}
CALYPSI_LIB=${CALYPSI_LIB:-clib-lc-ld-snes.a}
HOSTTOOLS=${HOSTTOOLS:-build/tools/Debug}
HERE=tools/snesbench
OUT=build/bench
EXE=
[ -x "$HOSTTOOLS/map2c.exe" ] && EXE=.exe

RENAME=""
for f in setMode setBrightness setScreenOn setScreenOff setColorEffect setColorIntensity \
         setPaletteColor dmaCopyVram dmaCopyCGram dmaClearVram bgSetGfxPtr bgSetMapPtr \
         bgSetScroll bgSetEnable bgSetDisable bgSetEnableSub bgSetDisableSub bgInitTileSet bgInitMapSet oamInit oamInitGfxSet \
         oamInitGfxAttr oamSet oamSetXY oamSetEx oamSetVisible oamClear snesRegWrite \
         snesHdmaSet snesHdmaEnable snesDmaFits oamMemory; do
    RENAME="$RENAME -D$f=bb_$f"
done

LEVELS=$(ls assets/levels/*.map | sort)
mkdir -p "$OUT/gen"

echo "bench: data"
# 816-tcc: large arrays as assembly (as the game's SNES build); Calypsi: plain C
"$HOSTTOOLS/map2c$EXE" -o "$OUT/gen/level_data_snes.c" --snes "$OUT/gen/level_data.asm" $LEVELS > /dev/null
"$HOSTTOOLS/gfx2c$EXE" -o "$OUT/gen/gfx_data_snes.c" -H "$OUT/gen/gfx_data.h" --snes "$OUT/gen/gfx_data.asm" \
    assets/gfx/gfx.txt $LEVELS > /dev/null
"$HOSTTOOLS/map2c$EXE" -o "$OUT/gen/level_data.c" $LEVELS > /dev/null
"$HOSTTOOLS/gfx2c$EXE" -o "$OUT/gen/gfx_data.c" -H "$OUT/gen/gfx_data.h" assets/gfx/gfx.txt $LEVELS > /dev/null

NAME=${BENCH_NAME:-calypsi}

if [ "${ONLY:-}" != calypsi ]; then
# --- 816-tcc -----------------------------------------------------------------
echo "bench: 816-tcc"
O=$OUT/tcc
mkdir -p "$O"
rm -f "${O:?}"/*.obj "${O:?}"/*.asm "${O:?}"/*.ps
cp src/platform/snes/hdr.asm "$O/hdr.asm"
for f in src/game/*.c "$OUT/gen/level_data_snes.c" "$OUT/gen/gfx_data_snes.c" "$HERE/bench.c"; do
    n=$(basename "$f" .c)
    "$PVSNESLIB_HOME/devkitsnes/bin/816-tcc" -I"$PVSNESLIB_HOME/devkitsnes/include" -Isrc -I"$OUT/gen" \
        $RENAME -DBENCH_TCC -Wall -c "$f" -o "$O/$n.ps"
    "$PVSNESLIB_HOME/devkitsnes/tools/816-opt" -i "$O/$n.ps" -o "$O/$n.asm" > /dev/null
done
cp "$OUT/gen/level_data.asm" "$OUT/gen/gfx_data.asm" src/platform/snes/glue.asm "$O/"
(
    cd "$O"
    for f in *.asm; do
        [ "$f" = hdr.asm ] && continue
        "$PVSNESLIB_HOME/devkitsnes/bin/wla-65816" -d -s -x -o "${f%.asm}.obj" "$f" > "${f%.asm}.log" 2>&1 \
            || { cat "${f%.asm}.log"; exit 1; }
    done
    LIB=$PVSNESLIB_HOME/pvsneslib/lib/LoROM_SlowROM
    { echo "[objects]"; ls ./*.obj; ls "$LIB"/*.obj; } > linkfile
    "$PVSNESLIB_HOME/devkitsnes/bin/wlalink" -d -s -v -A -c -L "$LIB" linkfile ../tcc.sfc > ../tcc_link.log 2>&1 \
        || { tail -20 ../tcc_link.log; exit 1; }
)
grep -E "^ROM:|^RAM:" "$OUT/tcc_link.log" | sed 's/^/bench: tcc /'
fi

# --- Calypsi -----------------------------------------------------------------
echo "bench: calypsi ($CALYPSI_MODEL $CALYPSI_OPT)"
O=$OUT/calypsi
mkdir -p "$O"
rm -f "${O:?}"/*.o
CSRC=${CALYPSI_SRC:-src}
for f in "$CSRC"/game/*.c "$OUT/gen/level_data.c" "$OUT/gen/gfx_data.c" "$HERE/bench.c"; do
    n=$(basename "$f" .c)
    # shellcheck disable=SC2086
    "$CALYPSI_HOME/bin/cc65816" --target SNES $CALYPSI_MODEL $CALYPSI_OPT -I"$CSRC" -I"$OUT/gen" \
        $RENAME -c "$f" -o "$O/$n.o"
done
"$CALYPSI_HOME/bin/as65816" "$HERE/calypsi_header.s" -o "$O/header.o"
for f in ${CALYPSI_ASM:-}; do   # hand-written replacements (with -DBB_ASM_... in CALYPSI_OPT)
    "$CALYPSI_HOME/bin/as65816" "$f" -o "$O/$(basename "$f" .s)_asm.o"
done
"$CALYPSI_HOME/bin/ln65816" ${CALYPSI_LDOPT:-} "$HERE/calypsi_linker.scm" "$O"/*.o -o "$OUT/$NAME.elf" \
    --output-format raw "$CALYPSI_LIB" --rtattr exit=simplified \
    --list-file "$OUT/$NAME.map" --cross-reference
# the raw image starts at $C00000; pad it to the 512 KB the header says
cp "$OUT/$NAME.raw" "$OUT/$NAME.sfc"
truncate -s 524288 "$OUT/$NAME.sfc"
echo "bench: $OUT/$NAME.sfc"
