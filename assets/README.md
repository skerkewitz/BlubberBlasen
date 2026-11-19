# Assets

Everything the game loads at runtime (graphics, sound) or converts at build time (levels).

Graphics are plain **24-bit PNG** files, and the colour **magenta (255, 0, 255) is transparent**. The build converts them into SNES tiles and palettes with `tools/gfx2c` (settings in `gfx/gfx.txt`, see `tools/README.md`), and the game draws from that data through the pseudo SNES (ADR-0005); the PNGs themselves are not loaded at run time and not shipped with the web build.

### SNES limits for all art (ADR-0004, ADR-0006)

`gfx2c` fails the build when a graphic breaks one of these, naming the file and the place:

- **Colours are SNES colours:** each channel is a 5-bit value v written as `v << 3 | v >> 2` (0, 8, 16, 24, 33, 41, ... 247, 255). The error message names the nearest one.
- **15 colours per palette** (plus transparent): per tileset; per sprite palette (player, bubbles and effects, food and items, and the enemies of each sprite sheet together); per 8 x 8 tile of the title, whose tiles must fit into 6 palettes.
- **128 cells of 16 x 16 per sprite sheet.** Each room group has a sheet (rounds 1-5, 6-15, 16-30, 31-50, 51-70, 71-99), round 100 and the title, story and ending screens have their own. The cells in the `palette` lines of `gfx.txt` are in every sheet; a sheet adds the cells of its rounds' enemy types. Cells with exactly the same pixels are stored once.
- **No asset over 32 KB** (one ROM bank).

The 8 sprite palettes: 0 player 1, 1 player 2 (player 1 with the green ramp swapped for blue), 2 bubbles and effects, 3 food and items, 4 the sheet's enemies, 5 to 7 tinted copies (red for angry, grey or yellow for frozen and about to shoot, grey for dash afterimages). All enemy types share one palette of 15 colours, because each enemy looks the same in every sheet.

## Graphics (`gfx/`)

| File | Size | Content |
|---|---|---|
| `sprites.png` | 256 x 256 | Sprite sheet, 16 x 16 cells, 16 per row. Cell number = row * 16 + column. |
| `font.png` | 256 x 16 | 8 x 8 font, ASCII 32..95 (space to `_`), 32 glyphs per row. White, so the game can tint it. |
| `tileset01.png` .. `tileset08.png` | 48 x 24 | Room tiles, a 6 x 3 grid of 8 x 8 tiles. Each round picks one in its settings file. |
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
| 35 | Push bubble (Plus mode), a small light-blue bubble |
| 36 | Vacuum swirl (not used any more; the vacuum is shown as a colour window) |
| 37 | Bubble with a trapped enemy (not used) |
| 38, 39 | Bubble burst |
| 40 | Bubble with a trapped angry enemy |
| 41..43, 44..46 | Empty bubble about to burst (orange, then red) |
| 48..54 | Food, tier 1..7 (1,000 up to 64,000 points) |
| 56..58, 59..61 | Trapped enemy about to escape (orange, then red) |
| 64..81 | Score pop-ups, 32 x 16 = two cells each: 10, 500, 1000, 2000, 4000, 8000, 16000, 32000, 64000 |
| 96..98, 99..101, 102..104 | Bubble ring for trapped enemies (normal, orange, red), drawn over ... |
| 105..107 | ... a small enemy: walker, bouncer, thrower |
| 112, 113 | Bouncer flying |
| 118, 119, 120 | Thrower walking, throwing |
| 121, 122 | Rock |
| 123, 124 | Hurry Up chaser (storm cloud) |
| 128, 129, 130 | Special bubbles: water, fire, lightning |
| 131..135, 143 | Letter bubbles E, B, L, N, A and S (`blasen_tile()` in `src/game/effect.c` maps B-L-A-S-E-N to them) |
| 136, 137 | Water stream |
| 138 | Fireball; 139, 140 fire carpet |
| 141, 142 | Lightning bolt |
| 144..148 | Power-ups: range candy (pink), speed candy (blue), rate candy (yellow), shoes, umbrella |
| 149..152 | Bonus food: cherry, apple, melon, cake |
| 153, 154 | Life icons (one / two heads in the bottom half) |
| 160, 161 | Flutter |
| 162, 163 | Hopper (stretched, squashed) |
| 164, 165 | Spitter |
| 166, 167 | Dropper |
| 168, 169 | Spitter fireball, dropper shot |
| 170..174 | Small flutter, hopper, spitter, dropper, shield (inside trapped bubbles) |
| 176..179, 180..183 | The Grumble Boiler (boss), 32 x 32 in four cells (top left, top right, bottom left, bottom right); mouth closed / open |
| 184..187 | Giant bubble around the beaten boss, 32 x 32 |
| 188 | Soot ball (boss shot) |
| 189 | Door to the secret room |
| 190..194 | Items: Soap Star, Rinse Wave, Steam Whistle, Fire Glove, Bubble Ring |
| 196..200 | Glow outline of player frames 0..4 (dash ready) |
| 201, 202 | Shield enemy (Riveter) walking |

Score pop-ups also include 100 and 200 (cells 82..85).

Cells 37, 40 and 56..61 (trapped walker in one picture) are no longer used. Cells 35 and 36 held two of them and are now the push bubble and the vacuum swirl.

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

Each round is two files:

- `roundNNN.map` (three digits, so that sorting by name keeps the order up to round 100) in the format of the [GierzahnEditor](https://github.com/skerkewitz/GierzahnEditor): 32 x 26 tiles, a static layer, an airflow layer and enemy spawn points.
- `roundNNN.txt` with the settings the editor can't store: Hurry Up time, tileset, a spawn delay per enemy and which special bubbles the round sends in.

The build converts both with `tools/map2c` (formats in `tools/README.md`). All `*.map` files are used, sorted by name. To look at all rooms side by side, build the `rooms_sheet` target (`build/rooms.png`, made by `tools/mapview`). To try a round directly, start the game with `--round NN`.

Things to keep in mind when designing rooms:

- The player spawns bottom-left (feet on row 24, column 3-4), so there must be floor there.
- Platforms are one-way: everything can jump up through them. A jump is just over 5 rows high, so platforms 5 rows apart can be climbed.
- A gap in the bottom row lets things fall out and come back in at the top.
- Airflow decides where bubbles drift. Cells without airflow make bubbles wander randomly.
- Special and letter bubbles come in through the first gap in the bottom row (else the top row, else near the bottom centre).
- Enemy types in the `.map`: 1 walker, 2 bouncer, 3 thrower, 4 flutter, 5 hopper, 6 spitter, 7 dropper, 8 shield (Riveter). Bouncers and flutters fly and need no floor; all others must stand on one.
- `mapview --check` (CTest `rooms_playable`) verifies that every room can be played. It also lists enemies that only a dash reaches (Plus mode) and enemies that need more than walking, jumping and dashing; neither fails the check.
- A pocket that only a dash gets into (rounds 12 and 14): walls on both sides, a platform as its ceiling, at most 3 rows high so players can jump out, air blowing up inside. Put a Riveter or another enemy that does not jump up into it, or it walks out on its own.
- `round100.map` is the boss arena (`boss 1` in its settings, no enemies). `secret.map` is the gem room behind the doors (`secret 1`); it is not part of the normal round order.
- Rounds 31 to 99 were generated from room patterns by the asset script and checked with `mapview --check`; they can be reworked in the editor like any other room.

## Sound effects (`sfx/`)

WAV files, listed with their settings in `sfx.json`, the file superdevkit's sound effects are built from (ADR-0008): one effect per entry (`id`, `priority`, and the WAV with the rate, treble boost, gain and volume it plays at on the SNES). The build converts them into the SNES's sample format (BRR) with `superdevkit sfx` and compiles them into the game; the game plays them by name, `SFX_<ID>` (`SFX_JUMP`, `SFX_UHHELPME`, ...), and the SNES and Rachael on the PC and in the browser play the same samples. Edit the file with the kit's `tools/brr_lab.html`, which opens this folder, plays each effect as the SNES will, and shows the audio RAM it takes (64,448 bytes; 43,686 now). `full` and `ready` are listed but not played by the game yet.

`UhHelpMe` plays when a player is caught in a rescue bubble (co-op rescue) and once more when two thirds of the bubble's time are over. `vacuum` plays when a player places a vacuum (Plus mode). `multikill` plays once per multikill, in place of `kill`, when the third enemy defeated in one go falls (a chain, a dash, one water, fire or lightning bubble, an item); that is the moment that earns BLASEN letters.

## Music (`music/`)

Two temporary tracks, streamed by the platform layer (`music_files` in `src/platform/platform_raylib.c` names the files). The game only says which track should play (`game_music()`): the title track on the title screen, the intro, the high scores and the ending; the game track in the rounds; nothing during game over. They are placeholders and will be replaced.

| File | Plays | Note |
|---|---|---|
| `zephiramusic-lofi-gaming-606302.mp3` | Title | Royalty free, added as a temporary track |
| `bouncyrunner-retro-games-flower-hill-stage-two-400495.mp3` | In game | Royalty free, added as a temporary track |
| `senormusica81-cinematic-dark-ambient-guarded-stillness-475740.mp3` | Not used any more | Former in-game track |

## Where the files come from

| Files | Origin |
|---|---|
| `title.png` | The Blubber Blasen logo from the Java version (`png/tile-screen.png`), scaled to half size. |
| `tileset01..08.png` | The room tiles from the Java version (`level/round01..08.png`), unchanged. |
| Bubble cells 32..34 | `spritesheet/bubble.png` from the Java version. |
| Rest of `sprites.png`, `font.png` | Placeholder art drawn by a script for this prototype. Replace freely. |
| All of `gfx/*.png` | On 2026-11-16 every colour was moved to the nearest SNES colour, and the sprites, the title logo and the tilesets were reduced once to the palette limits above (ADR-0006). The dash glow cells (196..200) were drawn white and tinted yellow by the game; they are yellow now. |
| `sfx/*.wav` | Placeholder sounds synthesised by a script for this prototype, except `UhHelpMe.wav`, `vacuum.wav` and `multikill.wav`. |
| `sfx/UhHelpMe.wav`, `sfx/vacuum.wav` | Added by the project owner (2026-11-14). |
| `sfx/multikill.wav` | Added by the project owner (2026-11-15). |
| `levels/*.map`, `levels/*.txt` | New room layouts and settings for this prototype. |

The Java version also contained a ripped NES sprite sheet, traced character sprites, original game sound effects, music and arcade round layouts. **None of these are used here, and they must not be added** (see the license note in the main README).
