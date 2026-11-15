#!/bin/sh
# Starts the game from the build folder; all arguments are passed on,
# for example: ./rungame.sh --replay build/Debug/recordings/<file>.bbr
# Prefers the Debug build (developer options, recordings), then Release.
# Under WSL it runs the Windows build.
root=$(cd "$(dirname "$0")" && pwd)
for exe in build/Debug/blubberblasen.exe build/Release/blubberblasen.exe build/blubberblasen \
           build/Debug/blubberblasen build-release/Release/blubberblasen.exe; do
    if [ -x "$root/$exe" ] || [ -f "$root/$exe" ]; then
        exec "$root/$exe" "$@"
    fi
done
echo "No game executable found. Build first: cmake --build build --config Debug" >&2
exit 1
