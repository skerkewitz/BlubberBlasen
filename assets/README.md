# Assets

Everything the game loads at runtime (graphics, sound) or converts at build time (levels).

Stage 1 keeps it simple: graphics are plain **24-bit PNG** files, and the colour **magenta (255, 0, 255) is transparent**. The platform layer loads them at startup and builds its "VRAM" textures from them. Indexed colours and SNES palettes come later.

## Graphics (`gfx/`)

| File | Size | Content |
|---|---|---|
| `sprites.png` | 256 x 256 | Sprite sheet, 16 x 16 cells, 16 per row. Cell number = row * 16 + column. |
| `font.png` | 256 x 16 | 8 x 8 font, ASCII 32..95 (space to `_`), 32 glyphs per row. White, so the game can tint it. |
| `tileset01.png` .. `tileset08.png` | 48 x 24 | Room tiles, a 6 x 3 grid of 8 x 8 tiles. Round *n* uses tileset *n* (repeating after 8). |
| `title.png` | 256 x 176 | Title logo. |

### Sprite sheet cells

| Cells | Content |
|---|---|
| 0, 1 | Player walking |
| 2, 3, 4 | Player jumping, falling, shooting |
| 5..8 | Player dying (spin) |
| 16, 17 | Enemy walking |
| 18, 19 | Enemy angry |
| 20..23 | Defeated enemy flying (spin) |
| 32..34 | Bubble (animation) |
| 35..37 | Bubble with a trapped enemy |
| 38, 39 | Bubble burst |
| 40 | Bubble with a trapped angry enemy |
| 48, 49 | Gem |
| 50, 51, 52 | Score pop-ups 10, 500, 1000 |

All sprites face **right**; the game mirrors them for the left.

### Tileset layout

Which graphic is used where is decided by the game from the neighbouring cells (see `autotile()` in `src/game/level.c`):

```
x:   0          1          2          3     4               5
y=0  block TL   block TR   block      -     shadow start V  -
y=1  block BL   block BR   shadow     shadow shadow outer   shadow start H
                           inner      horiz.
y=2  -          -          shadow V   -     -               -
```

The 2 x 2 "block" is used for the side walls, the single block for everything else. The shadow tiles are drawn into empty cells below and to the right of solid ones.

### Background tile numbers

The platform puts all background graphics into one 256 x 256 texture of 8 x 8 tiles (32 per row), at the numbers defined in `src/game/game.h`:

| Tiles | Content |
|---|---|
| 0..63 | Font (tile = ASCII code - 32; tile 0 is the space and stays transparent) |
| 64 + 32 * n .. | Tileset n + 1 (18 tiles used) |
| 320..1023 | Title logo, 32 x 22 tiles |

## Levels (`levels/`)

`roundNN.map` files in the format of the [GierzahnEditor](https://github.com/skerkewitz/GierzahnEditor): 32 x 26 tiles, a static layer, an airflow layer and enemy spawn points. The build converts them with `tools/map2c` (format described in `tools/README.md`). All `*.map` files are used, sorted by name.

Things to keep in mind when designing rooms:

- The player spawns bottom-left (feet on row 24, column 3-4), so there must be floor there.
- Platforms are one-way: everything can jump up through them. A jump is just over 5 rows high, so platforms 5 rows apart can be climbed.
- A gap in the bottom row lets things fall out and come back in at the top.
- Airflow decides where bubbles drift. Cells without airflow make bubbles wander randomly.

## Sound effects (`sfx/`)

Mono WAV files, loaded by name: `jump`, `shoot`, `pop`, `trap`, `kill`, `gem`, `death`, `start`, `clear`, `hurry`, `gameover`, `menu`, `pause`. A missing file is skipped silently. There is no music yet.

## Where the files come from

| Files | Origin |
|---|---|
| `title.png` | The Blubber Blasen logo from the Java version (`png/tile-screen.png`), scaled to half size. |
| `tileset01..08.png` | The room tiles from the Java version (`level/round01..08.png`), unchanged. |
| Bubble cells 32..34 | `spritesheet/bubble.png` from the Java version. |
| Rest of `sprites.png`, `font.png` | Placeholder art drawn by a script for this prototype. Replace freely. |
| `sfx/*.wav` | Placeholder sounds synthesised by a script for this prototype. |
| `levels/*.map` | New room layouts for this prototype. |

The Java version also contained a ripped NES sprite sheet, traced character sprites, original game sound effects, music and arcade round layouts. **None of these are used here, and they must not be added** (see the license note in the main README).
