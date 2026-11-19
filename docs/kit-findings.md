# Findings for superdevkit

**Status (2026-11-19): all seven findings are done in superdevkit (kit commit 02fa308): the miscompilation is fixed, and the game's ROM matches the PC for 36,000 ticks in all four `snescheck` scenarios. New findings start a new table below.**

Things noticed in Blubber Blasen while moving it to superdevkit (2026-11-19), written for whoever
works on the kit. The user forwards this file. Numbers are from the kit's home at version 0.5.0 as
installed on 2026-11-19, and from the game's repository at the commit that adds ADR-0007.

| # | Finding | Kind |
| --- | --- | --- |
| 1 | The MIR code generator miscompiles the game: the ROM differs from the PC from tick 2,623 on (CPU partner) | bug, dampf |
| 2 | Blubber Blasen uses the new names: the compatibility headers can go | handover |
| 3 | The game's tests of the pseudo SNES now run on Rachael and can move to the kit | handover |
| 4 | Rachael's HDMA timing matches Mesen, but no example checks it | test gap |
| 5 | `sdevkit_rename.py` also renames raylib's `KEY_*` in a frontend file | tool |
| 6 | `ExampleBench` finds the main loop's waiting code only in a ROM built inside the kit's repository | tool |
| 7 | An array element addressed by an index loaded from another array costs about three times a counted index | C guide, measured |

## 1. The MIR code generator miscompiles the game (tick 2,623, or 24,950)

The game's ROM, built with the defaults, gives the PC's checksums for 24,950 ticks of bot play
and then differs. With `--backend naive` the same ROM matches all 36,000 ticks. The PC side is not
the cause: gcc (WSL) and MSVC give the same 36,000 checksums. This may be the open
miscompilation of the roadmap ("Risks and open questions"), now reached with the default
inlining limit.

To reproduce, in the game's repository (`/mnt/d/Develop/AiProject/BlubberBlasen`; a Windows build
in `build/` provides `map2c`, `gfx2c` and `snescheck`):

```sh
SUPERDEVKIT_HOME=<home> ./build_snes.sh
build/Debug/snescheck.exe --ticks 36000 -o build/snes/check_long.txt   # or a Linux build's snescheck
<home>/dev/replay.sh build/snes/blubberblasen.sfc build/snes/check_long.txt
#   tick 24950 has checksum 079f8e3e on the SNES, the recording 51adb3f9
```

A shorter case, found later the same day: with the CPU partner playing player 2 (`snescheck
--cpu`, which sets the option on the title screen), the ROM differs from tick 2,623 on; alone with
the bot's improved menus (no pausing), from tick 3,340; in Plus mode with the partner, from 3,253:

```sh
build/Debug/snescheck.exe --ticks 3000 --cpu -o build/snes/check_cpu.txt
<home>/dev/replay.sh build/snes/blubberblasen.sfc build/snes/check_cpu.txt
#   tick 2623 has checksum e6cdd138 on the SNES, the recording 16c2c0fc
```

The checksum of a tick is the game's two tilemaps (`g_bg0`, `g_bg1`, named with `sdevkit_dbg_hash`
at the end of `video_frame`) followed by the sprite table. `snescheck` plays as `lib/snes/main.c`
does: `game_init(0)`, no save RAM, the game without `BB_DEBUG`, a bot on pad 1 (in
`tools/snescheck/snescheck.c`). Please do not write into the game's folders; copy what you need.

## 2. The game uses the `sdevkit_` names (finding 8 of the game's list)

`src/game` was converted with `tools/sdevkit_rename.py` and includes `sdevkit/snes.h` only. The
deprecated `lib/include/snes/` is no longer used by the game, so the kit can drop it whenever it
likes.

## 3. The tests of the pseudo SNES, ported to Rachael

The game's `src/snes` is gone; its PC and web builds link `lib/rachael`. The game's
`tests/test_snes.c` (tiles, palettes and flips, scrolling and 32 x 64 maps, layer order, sprites,
the per-line sprite limits, colour math in windows, HDMA tables, brightness, the DMA count) now
tests Rachael through `rachael.h` and passes. Two expectations changed for the line offset of
Rachael ("row y of the picture is scanline y + 1"): the background tile test uses map row 1, and
the sprite test expects a sprite at y on row y. `tests/test_gfx.c` scrolls its backgrounds by
-1 for the same reason. The roadmap's "Rachael's tests from the game's tests/test_snes.c" can be
taken from there.

## 4. Rachael's HDMA timing matches Mesen, but no example checks it

None of the examples calls `sdevkit_hdma_set`, so `dev/compare_example.sh` never compares HDMA
between the SNES and Rachael. A test ROM settled it for the game: backdrop black, colour math
adding the fixed colour to it, HDMA on `COLDATA` with the table `{ 3, 0xE0, 1, 0xFF, 0 }`. In Mesen
and on Rachael, rows 0 to 2 of the picture are black and row 3 is the first white one. So the
first entry lands on row 0 (scanline 1), and Rachael is right. An example or a `RachaelTest` with
such a table would keep it so. The test program is short:

```c
#include "sdevkit/loop.h"
#include "sdevkit/snes.h"

static const uint8_t table[] = { 3, 0xE0, 1, 0xFF, 0 };

void game_init(uint16_t seed)
{
    (void)seed;
    sdevkit_set_mode(SDEVKIT_BG_MODE1, 0);
    sdevkit_set_palette_color(0, 0);
    sdevkit_set_color_effect(0x00, 0x20);      /* math always, fixed colour, on the backdrop */
    sdevkit_hdma_set(1, SDEVKIT_PPU_COLDATA, SDEVKIT_HDMA_ONE, table);
    sdevkit_hdma_enable(SDEVKIT_HDMA_CHANNEL1);
    sdevkit_set_screen_on();
}

void game_tick(const uint16_t pad[SDEVKIT_PADS]) { (void)pad; }
void game_vblank(void) { }
```

The game had the line wrong: it wrote sprites at y - 1 and left the backgrounds unscrolled, which
fit its old pseudo SNES (row y = scanline y) but not the console. Now it scrolls both backgrounds
by -1 and writes sprites at y (ADR-0007 of the game), and its picture on Rachael is the old one,
pixel for pixel.

## 5. `sdevkit_rename.py` renames raylib's `KEY_*` too

Run over the game's raylib frontend (`src/platform/platform_raylib.c`) as well as the game code,
the script turned raylib's `KEY_A`, `KEY_UP`, ... into `SDEVKIT_KEY_*`, which did not compile. The
script is meant for game code, so this was a misuse, but a file that includes `raylib.h` could be
skipped, or warned about, cheaply.

## 6. `ExampleBench` needs a ROM built inside the kit's repository

`dampf.emu.ExampleBench` finds the main loop's waiting code by the C lines of `lib/snes/snes.c`,
under exactly that name, read from the current folder. A game outside the repository names the
file in its debug file by its path relative to the ROM's folder
(`../../../../home/.../lib/snes/snes.c`), so `ExampleBench` stops with "no code of the main loop's
waiting functions". The game measured by building a second ROM beside a copy of the kit's `lib/`
(the same bytes, other names in the debug file). Matching the file by the end of its path, or
by the debug file's file id of `snes.c`, would let a game use it as it is; a game needs it, since
`dampf bench` is for the old benchmark's ports.

## 7. An index loaded from an array costs about three times a counted one

Measured in the game's `separate()` (`bubble.c`), the pair loop over 16 bubble slots, in ticks
with 14 to 16 bubbles, as master cycles per tick from `ExampleProfile`. The original inner loop
counts `j` and takes `Bubble *b = &g_bubbles[j]`: that line cost 8,621, the loop line 6,737.
Rewritten to loop over a list of the floating bubbles' slots, `Bubble *b =
&g_bubbles[list[lj]]` cost 26,054 and its loop line 16,616, though it skipped the `floating()`
checks (14,820 -> 1,788). A list of pointers (`Bubble *list[]`), a static list instead of one in
the frame, and a mask that bounds the index (`list[lj] & 15`) all measured the same. So the
list lost wherever most slots were in use. It looks as if the counted index is stepped (the
pointer advanced by the element size), while a loaded index is multiplied by the size and
turned into a 24-bit pointer each time. If that is so, it may be worth a rule in `lib/C_STYLE.md`
("walk the array with a counter and skip, rather than through an index list"), or an
optimization. The game kept the counted loop; what saved time there was a cheaper test for the
far pairs (an unsigned range compare instead of two `abs` and two compares).

## New findings

| # | Finding | Kind |
| --- | --- | --- |
| 8 | `ExampleCheck` records the ROM's sound but does not write it | tool |
| 9 | A benchmark for dampf's loop optimizations: the game's bubble separation in four variants | benchmark |
| 10 | M7 for a game with its own converters and its own frontend | tool |
| 11 | What Blubber Blasen still needs from the kit's desktop before its own frontend can go | frontend |

## 8. `ExampleCheck` records the ROM's sound but does not write it (2026-11-19)

The game checks its ROM's sound against Rachael's: `snescheck --sound` writes Rachael's DSP writes
in sound.txt's format, and dampf's emulator records the ROM's in `ExampleCheck.Result.sound()`.
But `ExampleCheck`'s `main` only writes the checksums, so the game read `sound()` with a small
class of its own. An optional output for it (`ExampleCheck rom recording [hashes.txt [sound.txt]]`,
say) would let a game outside the kit compare the sound with the kit's tools alone. With it,
Blubber Blasen's ROM and Rachael gave the same DSP writes, line for line, in three scenarios of
12,000 ticks.

## 9. A benchmark for dampf's loop optimizations: the bubble separation (2026-11-19)

With 12 to 16 floating bubbles, `separate()` in `bubble.c` (every bubble against every other:
cling when close, push apart when overlapping) is the costliest part of the game's tick: about a
quarter of a frame, and from 8 bubbles on the game gets slow (0.74 frames per tick at 8, 1.26 at
16). The game tried to cut the pairs: keep the floating bubbles' slots sorted by x and stop at
the first one too far right, which in a clump leaves about a third of the pairs. It measured no
gain, and the reasons are dampf's, so the game kept its loop and hands the case over as a
benchmark: `docs/kit-bench/` in the game's repository.

- `separate.c`: the separation in four variants, each with the game's own pair code and
  constants, on 32 frames of real bubble positions recorded from the game (a clump of 12 to 16
  bubbles): A the game's counted pair loop over its 23-byte struct array, B a sorted list of
  slots over the struct array, C the counted loop over one array per field, D the sorted list over
  one array per field. `test_main` runs all four on every frame and keeps a checksum each; on
  this data all four agree (`a_equals_c`, `b_equals_d`, and A = B). It passes `dampf check`, and
  gcc with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` is silent. It is written like the
  kit's exec tests (globals, `test_main`).
- `bench_game.c`: the same as a superdevkit game for measuring, one case per tick (loading only,
  or loading and one variant), with `check.txt` of `ticks 160`; the work per tick of a case minus
  that of loading is the variant's.
- `pc_main.c`: runs `test_main` on the PC and prints the checksums.

Measured with dampf 0.5.2, master cycles per frame of data:

| Variant | Master cycles | Frames |
| --- | ---: | ---: |
| A counter, struct array (the game) | 93,999 | 0.263 |
| B sorted list, struct array | 93,763 | 0.262 |
| C counter, one array per field | 109,381 | 0.306 |
| D sorted list, one array per field | 94,843 | 0.265 |

What the assembly of A and the game's measurements show (in the game's busiest stretch the
separation took about 99,000 master cycles a tick):

1. `j * 23` is computed anew for every pair (six shifts and subtractions, about 22 cycles) instead
   of stepping the index by 23: "an induction variable as the index", in the roadmap's "Ideas not
   yet built".
2. Every intermediate value goes through the direct page (`sta z:dampf_frame+N` then `lda`):
   register allocation (M9).
3. `b[list[k]]` multiplies the loaded slot by 23 at every access (`slow-index-list`), and the small
   loops over bytes cost extra with 8-bit counters (`slow-u8-counter`): that is what B and D pay
   for their lists, about as much as they save on the pairs.
4. One array per field (C) is slower than the struct array in the counted loop, against
   `fast-array-per-field`: worth a look at why; `DAMPF_SPLIT` would meet the same.
5. A test ends in a branch and a jump (`bcs @L; jmp ...`) where the block layout could fall
   through.

The goal for these optimizations, as a number to watch: D clearly below A (it handles a third of
the pairs), and C no slower than A. The game would then switch to the sorted list, which changes
its behaviour (the pairs are handled in another order) and so needs its recordings recorded again.

## 10. M7 for a game with its own converters and its own frontend (2026-11-19)

Blubber Blasen moved to superdevkit 0.7.0: `sdevkit_sram()`, `sdevkit_music_play`/`stop`,
`superdevkit.toml` and `superdevkit build snes`. It works: the game behaves as before, and the ROM
matches the PC over 36,000 ticks in four scenarios. Two things came up:

1. **A game with converters of its own** (Blubber Blasen's `map2c` and `gfx2c`, which check the
   game's limits and write its formats) has to run them before `superdevkit build`, into a folder
   it lists in `sources`. The game's `build_snes.sh` does that now. A step in the project file
   that runs a command first (`[build] before = ["..."]`, say) would make `superdevkit build snes`
   the whole build. Not urgent.
2. **A CMake build next to the kit's** (the game keeps its own frontend until M8) needs
   `music.h`, and for its frontend `music_files.c`, just as `superdevkit build` writes them. The
   game's CMake now reads the tracks from `superdevkit.toml` with a regular expression and writes
   both files itself. A command that only does the conversions of the project file
   (`superdevkit gen [FOLDER]`: build/gen with graphics, sfx and music.h, and the music list)
   would let other build systems call the kit instead of copying what it writes.

## 11. What Blubber Blasen still needs from the kit's desktop before its own frontend can go (2026-11-20)

The game now builds and runs from the kit on Linux and Windows (`superdevkit build linux|win64`;
both played 36,000-tick recordings with every checksum the same) and registers its switches,
buttons and overlays with the debug view (ADR-0010 of the game). It keeps its own CMake desktop
beside it for now, because these are missing, in the order they matter to the game:

1. **Start options for the game.** The kit calls `game_init(0)` and the game gets nothing from the
   command line. The game starts with `--seed`, `--round`, `--plus`, `--ending`, `--options` and
   debug switches. One way: a `--seed` option, and `--set name=value` setting a registered toggle
   or range by its name before the first tick (the game would then register its start options
   like its switches).
2. **A bot, or a pad source.** The game's `--autoplay` plays the game for screenshots and long
   unattended runs. Since dampf has no function pointers, maybe a toggle the game registers
   ("autoplay") and reads in `game_tick` to make its own pad; then only item 1 is needed to switch
   it on from the command line.
3. **Mute** in the window (`--mute`): every automated run of the game must be silent. `--check`
   is silent already, so this is about runs with a window (screenshots, watching a bot).
4. **Text in the view.** The game's ImGui panels show the objects (type, state, position, speed,
   timer), the CPU partner's goal and chosen action, why lives were lost, the round status. A
   read-only registration would do, for example `sdevkit_dbg_text(name, buffer)` (a string the game
   fills, shown as a line) or `sdevkit_dbg_value(name, &u16)`.
5. **Stepping back in a replay** (`Shift`+`F8` in the game: the replay runs again fast to the tick
   before) and `--jump <tick>`.
6. **Per-target defines in the project file**, for example `[desktop] defines = ["BB_DEBUG"]`. The
   game now keys its debug code on `__DAMPF__` instead.
7. **Box styles**: filled boxes (the CPU view's danger dots fade with distance in the game's view)
   or a direction for an arrow (the air currents are colours now).
