#!/bin/sh
# Profiles one benchmark ROM over ticks FROM..TO (default 900..1700, play):
#   tools/snesbench/profile.sh <rom name in build/bench> [from to]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(cd "$HERE/../../build/bench" && pwd)
MESEN=${MESEN:-$HERE/../../build/mesen}
ROM=$1
FROM=${2:-900}
TO=${3:-1700}
cd "$MESEN"
cp "$OUT/$ROM.sfc" bench_run.sfc
sed -e "s/PROFILE_FROM, PROFILE_TO/$FROM, $TO/" "$HERE/profile.lua" > bench_prof.lua
rm -f profile.txt
timeout 900 ./Mesen.exe --testrunner --timeout=800 bench_run.sfc bench_prof.lua > /dev/null 2>&1 || true
if [ -f "$OUT/$ROM.sym" ]; then SYM=$OUT/$ROM.sym; else SYM=$OUT/$ROM.map; fi
python3 "$HERE/profile.py" "$SYM" profile.txt $((TO - FROM)) ${TOP:-30}
