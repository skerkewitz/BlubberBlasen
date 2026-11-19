# Findings for superdevkit

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
