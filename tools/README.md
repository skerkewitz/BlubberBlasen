# Tools

Small programs that run on the build machine. They turn editable source files
from `assets/` into data the game uses.

## Conventions

- Plain C99, one folder per tool (`tools/<name>/<name>.c`), no third-party libraries.
- They are built by CMake as host executables and run automatically during the build when their input changes.
- The game-code rules (no `malloc`, no `float`) do not apply here.
- Shared formats come from the game headers (`src/game/*.h`), so the tool and the game cannot disagree on a constant.
- Errors go to stderr as `<tool>: <file>: <message>`. Exit code 0 = ok, 1 = bad input, 2 = bad usage.
- Generated files go into the build directory (`build/generated/`) and are never committed.

## map2c

Converts level files from the GierzahnEditor (`*.map`) into `level_data.c`, which holds a `const` array per level.

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
| then | 4 x int32 each | x, y (tile of the feet), type (1 = walker), looking left (0/1) |

Output: one byte per cell (bit 7 = solid, bits 0..2 = airflow) and an enemy table, as declared in `src/game/level_data.h`.
