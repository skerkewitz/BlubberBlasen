# Tools

Small programs that run on the build machine. They turn editable source files
from `assets/` into data the game uses.

## Conventions

- Plain C99, one folder per tool (`tools/<name>/<name>.c`), no third-party libraries. Code used by several tools lives in `tools/common/` (for example `mapfile.c`, which reads `.map` files).
- They are built by CMake as host executables and run automatically during the build when their input changes.
- The game-code rules (no `malloc`, no `float`) do not apply here.
- Shared formats come from the game headers (`src/game/*.h`), so the tool and the game cannot disagree on a constant.
- Errors go to stderr as `<tool>: <file>: <message>`. Exit code 0 = ok, 1 = bad input, 2 = bad usage.
- Generated files go into the build directory (`build/generated/`) and are never committed.

## map2c

Converts level files from the GierzahnEditor (`*.map`), plus an optional settings file per round (`*.txt`), into `level_data.c`, which holds a `const` array per level.

```
map2c -o <output.c> <level1.map> [<level2.map> ...]
```

The levels are numbered in argument order. The build passes all `assets/levels/*.map` files, sorted by name, so `round01.map` becomes level 1.

Input format (big endian):

| Offset | Size | Content |
|---|---|---|
| 0 | 832 bytes | Static layer, 32 x 26 tiles; 0 = empty, non-zero = solid |
| 832 | 832 bytes | Airflow; 0 = none, 1 = up, 2 = down, 3 = left, 4 = right |
| 1664 | int32 | Number of enemies (optional; missing = 0) |
| then | 4 x int32 each | x, y (tile of the feet), type (1 = walker, 2 = bouncer, 3 = thrower), looking left (0/1) |

### Settings file

For every `<name>.map`, map2c also reads `<name>.txt` if it exists. It holds the per-round data the editor format has no room for. The `.map` file stays exactly as the editor writes it.

```
# round01.txt
hurry   40          # seconds until HURRY UP, 0 = never        (default 30)
tileset 1           # room graphics gfx/tileset01..08.png      (default: round number)
delay   0 0 30 30   # frames before each enemy enters, 0..255, in .map order (default 0)
special water 15    # special bubbles: none, water, fire or lightning, every n seconds (default none)
```

Unknown keys, values out of range and a `delay` list whose length doesn't match the enemy count are errors, reported with file and line. The CTest tests `map2c_bad_*` check this.

### Output

One byte per cell (bit 7 = solid, bits 0..2 = airflow), an enemy table with spawn delays, and per round the tileset and the Hurry Up time in frames, and the special bubble type with its interval in frames, as declared in `src/game/level_data.h`.

## mapview

Renders rooms into one PNG contact sheet, for reviewing levels without starting the game.

```
mapview -o <sheet.png> [-c <columns>] [-s <scale>] <level.map>...
```

Each room shows the walls (pink), the airflow (grey arrows), the enemy spawn points (walker purple, bouncer cyan, thrower orange; a white bar marks the side the enemy faces), the player start points (green = player 1, white = player 2) and the round number taken from the file name. Default: 5 columns, scale 1.

All rooms at once:

```
cmake --build build --target rooms_sheet     # writes build/rooms.png
```
