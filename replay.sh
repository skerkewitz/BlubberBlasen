#!/bin/sh
# Plays a recording: ./replay.sh <file> [more options]
#   ./replay.sh 20261115-165418-marked.bbr --jump 9300
# <file> is looked up in the recordings folders next to the built game
# (build/Debug/recordings, ...); ".bbr" may be left out, and a path works too.
# Without arguments it lists the newest recordings.
root=$(cd "$(dirname "$0")" && pwd)
dirs="build/Debug/recordings build/Release/recordings build/recordings build-release/Release/recordings"

if [ $# -eq 0 ]; then
    echo "usage: $0 <recording> [options]    newest recordings:"
    for d in $dirs; do
        [ -d "$root/$d" ] && ls -t "$root/$d" | head -10 | sed "s|^|  |"
    done
    exit 1
fi

name=$1
shift
case "$name" in *.bbr) ;; *) name="$name.bbr" ;; esac

file=
if [ -f "$name" ]; then
    file=$name
else
    for d in $dirs; do
        if [ -f "$root/$d/$name" ]; then
            file="$d/$name"   # relative to the project, so a Windows exe can open it too
            cd "$root" || exit 1
            break
        fi
    done
fi
if [ -z "$file" ]; then
    echo "Recording '$name' not found in: $dirs" >&2
    exit 1
fi
exec "$root/rungame.sh" --replay "$file" "$@"
