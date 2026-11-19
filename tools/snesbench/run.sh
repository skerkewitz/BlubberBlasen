#!/bin/sh
# Runs benchmark ROMs (build.sh) headless in Mesen 2 and compares each with
# the first:
#   tools/snesbench/run.sh [rom names in build/bench, default: tcc calypsi]
# MESEN=<folder> picks Mesen (default build/mesen).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(cd "$HERE/../../build/bench" && pwd)
MESEN=${MESEN:-$HERE/../../build/mesen}
ROMS=${*:-tcc calypsi}
cd "$MESEN"
for rom in $ROMS; do
    cp "$OUT/$rom.sfc" bench_run.sfc
    cp "$HERE/bench.lua" bench_run.lua
    rm -f bench.txt
    timeout 900 ./Mesen.exe --testrunner --timeout=800 bench_run.sfc bench_run.lua > /dev/null 2>&1 || true
    cp bench.txt "$OUT/$rom.txt"
    echo "bench: $rom $(wc -l < "$OUT/$rom.txt") ticks"
done
set -- $ROMS
first=$1
shift
for rom in "$@"; do
    echo "--- $first vs $rom"
    python3 "$HERE/report.py" "$OUT/$first.txt" "$OUT/$rom.txt"
done
