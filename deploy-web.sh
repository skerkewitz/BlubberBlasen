#!/usr/bin/env bash
# Build the web version (WebAssembly) and copy it into the GitHub Pages folder.
#
#   ./deploy-web.sh [target-dir]
#
# Runs in WSL/Linux with Emscripten in /usr/lib/emscripten (see README, "Web build").
# Only copies the files; committing and pushing the Pages repo is up to you.
set -euo pipefail

TARGET="${1:-/mnt/d/Develop/skerkewitz.github.io/blasen}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build-web"
FILES=(blubberblasen.html blubberblasen.js blubberblasen.wasm blubberblasen.data)

export PATH="/usr/lib/emscripten:$PATH"
command -v emcmake >/dev/null || { echo "emcmake not found (Emscripten missing?)" >&2; exit 1; }

# Configure once, as a release build without the debug switches.
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    emcmake cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBB_DEBUG_SWITCHES=OFF
fi
cmake --build "$BUILD" -j8

mkdir -p "$TARGET"
for f in "${FILES[@]}"; do
    cp -f "$BUILD/$f" "$TARGET/$f"
done

echo "Copied to $TARGET:"
ls -l "${FILES[@]/#/$TARGET/}"
