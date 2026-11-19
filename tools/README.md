# Tools

Small programs that run on the build machine. They turn editable source files
from `assets/` into data the game uses.

## Conventions

- Plain C99, one folder per tool (`tools/<name>/<name>.c`), no third-party libraries. Code used by several tools lives in `tools/common/`: `mapfile.c` reads `.map` files, `reach.c` is the simplified movement model (used for the playability check and the navigation data).
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

The levels are numbered in argument order. The build passes all `assets/levels/*.map` files, sorted by name, so `round001.map` becomes level 1 and `round100.map` level 100 (the secret room, `secret.map`, sorts last).

Input format (big endian):

| Offset | Size | Content |
|---|---|---|
| 0 | 832 bytes | Static layer, 32 x 26 tiles; 0 = empty, non-zero = solid |
| 832 | 832 bytes | Airflow; 0 = none, 1 = up, 2 = down, 3 = left, 4 = right |
| 1664 | int32 | Number of enemies (optional; missing = 0) |
| then | 4 x int32 each | x, y (tile of the feet), type (1 walker, 2 bouncer, 3 thrower, 4 flutter, 5 hopper, 6 spitter, 7 dropper, 8 shield), looking left (0/1) |

### Settings file

For every `<name>.map`, map2c also reads `<name>.txt` if it exists. It holds the per-round data the editor format has no room for. The `.map` file stays exactly as the editor writes it.

```
# round001.txt
hurry   40          # seconds until HURRY UP, 0 = never        (default 30)
tileset 1           # room graphics gfx/tileset01..08.png      (default: round number)
delay   0 0 30 30   # frames before each enemy enters, 0..255, in .map order (default 0)
special water 15    # special bubbles: none, water, fire or lightning, every n seconds (default none)
boss    1           # this is the boss round (round100.txt)
secret  1           # the secret room; not part of the normal round order (secret.txt)
```

Unknown keys, values out of range and a `delay` list whose length doesn't match the enemy count are errors, reported with file and line. The CTest tests `map2c_bad_*` check this.

### Navigation data for the CPU partner

map2c also stores, per round, where one can stand and how to get from there to elsewhere: the standing positions are grouped into **segments** (one row, a run of columns), and every segment lists the segments reachable from it in one move (walk and fall, wrap through a bottom gap, or jump up to 5 rows and 4 columns). The CPU partner searches this small graph at runtime. The movement model is the same one `mapview --check` uses (`tools/common/reach.c`).

### Output

One byte per cell (bit 7 = solid, bits 0..2 = airflow), an enemy table with spawn delays, and per round the tileset and the Hurry Up time in frames, and the special bubble type with its interval in frames, as declared in `src/game/level_data.h`.

## gfx2c

Converts the graphics in `assets/gfx` into SNES data (ADR-0004, ADR-0006): 4 bpp and 2 bpp planar tiles, 15-bit palettes, the title's tile map and one sprite sheet per room group, as `const` arrays in `gfx_data.c` with `gfx_data.h`.

```
gfx2c -o <gfx_data.c> -H <gfx_data.h> <gfx.txt> <level1.map> [<level2.map> ...]
```

`assets/gfx/gfx.txt` says what each PNG is (the comments there explain every line): the font and its colours, the tilesets, the title and its number of palettes, and for the sprite sheet which cells use which palette, which cells belong to which enemy type and to the boss, player 2's colour swap, the tint colours and the sheets with their rounds. The level files tell which enemy types each round sheet needs.

What it makes:

| Data | Content |
|---|---|
| `gfx_font_tiles` | 64 glyphs (ASCII 32..95), 2 bpp |
| `gfx_tileset_tiles`, `gfx_tileset_pal` | 18 tiles of 4 bpp and one palette per tileset |
| `gfx_title_tiles`, `gfx_title_map`, `gfx_title_pal` | the logo: tiles (identical ones once), a map with BG palettes 2..7, 6 palettes |
| `gfx_sprites_shared`, `gfx_sheet_tiles` | sprite tiles in VRAM order, 8 cells per row of 16 x 2 tiles: the rows all sheets share, then each sheet's rows |
| `gfx_sheet_slot` | per sheet, the slot of each cell of `sprites.png` (0xFF = not in it) |
| `gfx_cell_pal`, `gfx_level_sheet` | the sprite palette of each cell; the sheet of each round |
| `gfx_pal_player`, `gfx_pal_effects`, `gfx_pal_items`, `gfx_pal_enemies` | sprite palettes, each with its red, grey, yellow and orange copy |

It converts and checks, it does not repair: a colour off the SNES grid, a palette over 15 colours, a sprite sheet over 128 cells or an asset over 32 KB stops the build with the file and the reason (CTest `gfx2c_bad_*` checks that). `tests/test_gfx.c` loads everything it made into the pseudo SNES, renders it and compares each pixel with the PNG.

PNGs are read by `tools/common/pngread.c` (8-bit RGB, RGBA or palette, not interlaced), which has its own small inflate.

## snescheck

Plays the game on Rachael, the kit's SNES in C, as superdevkit's main loop plays the ROM (`game_init(0)`, no save RAM, no debug switches; the game is linked without `BB_DEBUG`), with a simple bot on pad 1, and writes the session as a recording in the kit's check format: the pad per tick (`press`) and the checksum of every tick (`hash`), which covers the sprite table and the two tilemaps the game names with `sdevkit_dbg_hash` in `video.c`.

```
snescheck [--ticks N] [--cpu] [--plus] [--trace FILE] -o build/snes/check.txt
<superdevkit home>/dev/replay.sh build/snes/blubberblasen.sfc build/snes/check.txt
```

The bot only presses buttons, so the ROM plays the same game from the same pads, but it looks at the game's state to choose them. On the title screen it sets the options first: `--cpu` makes player 2 the CPU partner, `--plus` picks Plus mode, in which the bot also places a vacuum every 5 seconds. Then it starts a game, continues after a game over, and in play walks, jumps and shoots without ever pressing Start (which would pause). `--trace FILE` writes one line per tick, `tick state round bubbles enemies items vacuums` (the `GameState`, the round, the bubbles, enemies and items in use, bit i set while player i's vacuum is on), to put measurements of the ROM per tick in context.

The same recording measures the ROM: the kit's `dampf.emu.ExampleBench` prints the work per tick (on average and in the busiest 60 ticks), `dampf.emu.ExampleProfile rom.sfc rom.lbl check.txt FROM TO` the functions and C lines the time of those ticks goes to (`docs/kit-findings.md`, finding 6: today `ExampleBench` needs the ROM built beside a copy of the kit's `lib/`).

`replay.sh` plays the same pads on the ROM in dampf's emulator and names the first tick whose checksum differs, so a ROM that computes something else than the PC shows at once (ADR-0007). Built by CMake with the game (not in the web build); `--ticks` defaults to 3600, one minute of play.

## mapview

Renders rooms into one PNG contact sheet, for reviewing levels without starting the game.

```
mapview [-o <sheet.png>] [-c <columns>] [-s <scale>] [--check] <level.map>...
```

`--check` tests that each room can be played, with a simplified movement model: walking, falling, wrapping through gaps in the bottom row, and jumping up to 5 rows (through one-way platforms) and 4 columns sideways. It reports enemies that walk but cannot be reached, enemies that need a floor but have none, and a missing floor at the player start. The CTest test `rooms_playable` runs it on all rooms, so a room made in the editor that cannot be played fails the tests.

Each room shows the walls (pink), the airflow (grey arrows), the enemy spawn points (walker purple, bouncer cyan, thrower orange, flutter yellow, hopper green, spitter red, dropper grey; a white bar marks the side the enemy faces), the player start points (green = player 1, white = player 2) and the round number taken from the file name. Default: 5 columns, scale 1.

All rooms at once:

```
cmake --build build --target rooms_sheet     # writes build/rooms.png
```
