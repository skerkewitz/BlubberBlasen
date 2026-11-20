#!/bin/sh
# Runs superdevkit for this game: ./superdevkitw.sh build snes, ./superdevkitw.sh run linux, ...
# (every argument goes to the superdevkit command).
#
# Where superdevkit is comes from superdevkit.env next to this script, which is not committed:
#
#   SUPERDEVKIT_HOME=/path/to/superdevkit/build/install/superdevkit
#
# Without that file, SUPERDEVKIT_HOME from the environment is used.
root=$(cd "$(dirname "$0")" && pwd)
if [ -f "$root/superdevkit.env" ]; then
    set -a
    . "$root/superdevkit.env"
    set +a
fi
if [ -z "$SUPERDEVKIT_HOME" ]; then
    echo "superdevkitw: no SUPERDEVKIT_HOME: write it into $root/superdevkit.env" >&2
    echo "  SUPERDEVKIT_HOME=/path/to/superdevkit/build/install/superdevkit" >&2
    exit 1
fi
if [ ! -x "$SUPERDEVKIT_HOME/bin/superdevkit" ]; then
    echo "superdevkitw: no bin/superdevkit in $SUPERDEVKIT_HOME (SUPERDEVKIT_HOME, superdevkit.env)" >&2
    exit 1
fi
cd "$root" || exit 1
exec "$SUPERDEVKIT_HOME/bin/superdevkit" "$@"
