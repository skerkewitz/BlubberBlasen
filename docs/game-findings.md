# Findings for Blubber Blasen

Things noticed in Blubber Blasen's code while building dampf, the C compiler for its SNES port,
written for whoever works on the game. Each finding says where, why, whether it changes the game's
behaviour (and with it the recorded replays), and what it costs. "dampf handles it" means the
compiler will deal with it on its own once the optimization exists; such findings still help with
Calypsi today. All numbers are as of 2026-11-18 unless dated otherwise.

The findings are numbered by priority. Their numbers before 2026-11-18 are given as "(was N)" in
each heading. When dampf is finished, they feed a guide on writing game code for dampf
(`docs/game-guide.md`, not written yet).

| # | Finding | Changes behaviour | Cost |
| --- | --- | --- | --- |
| 1 | Less work per tick: the CPU partner and the bubble separation first | yes, replays re-recorded | large, game design work |
| 2 | Uploads in `video_vblank` can wait forever; one budget check covers two copies | no | small, in `video.c` |
| 3 | `separate()` compares every pair of bubbles | yes, slightly; replays re-recorded | about 30 lines |
| 4 | `game.h` says `game_vblank` runs from the vertical blank interrupt | no | one comment |
| 5 | More recordings would test dampf on more of the game | no | a few minutes of play each |
| 6 | Shifts by a variable count in drawing code | no | 8 places, a line or two each |
| 7 | Profile shares on functions with 0 calls | no | a fix in `profile.py` |
| 8 | The dev kit's console API has new names: `sdevkit_` | no | one script run, then a refresh of the snapshot |
| 9 | Two guides for writing the game's code | no | one line in the game's `CLAUDE.md` |
| 10 | A comparison with a sum of a variable is a 32-bit comparison | no | a cast in four places |

## 1. The game has to do less work per tick (was 7)

dampf runs the whole benchmark (`tools/snesbench/bench.c`) with the same checksums as the PC on
every tick. Frames per tick, first with dampf's unoptimized code generator (when this was decided,
2026-11-17), then with dampf's code as of 2026-11-18 (FastROM):

| | Unoptimized: average | Unoptimized: worst tick | Now: average | Now: worst tick |
| --- | --- | --- | --- | --- |
| Play | 5.17 | 8.58 | 0.91 | 1.60 |
| Play with the CPU partner | 6.55 | 12.23 | 1.48 | 3.18 |

The goal is 0.6 frames per tick for the game logic. dampf's remaining steps are expected to bring
play to about 0.85–1.0 frames per tick (an estimate, not a measurement), so the compiler alone will
not close the gap, and the worst ticks are far from it. It was decided on 2026-11-17 that dampf and
the game work on it in parallel; the game's ROADMAP, step 2b, already plans this kind of change.

Where the work is:

1. The CPU partner adds 0.57 frames per tick on average and 1.58 on the worst tick (1.48 against
   0.91, 3.18 against 1.60), more than the whole budget on the worst tick. The cost comes from
   what it simulates (half a second of moves for several options, every tick), so no compiler will
   remove it. Ideas from the ROADMAP: plan only every few ticks and follow the plan in between,
   fewer candidate moves, a shorter look-ahead, one planning step spread over several ticks.
2. The bubble separation (`separate()` in `bubble.c`, finding 3): its pair loop is 16 % of the play
   ticks with dampf's code.

Targets, measured with the game's own benchmark (any compiler; the ratios matter):

- the CPU partner's extra cost per tick ("play +cpu" against "play" in `report.py`) at least
  halved, on average and on the worst tick;
- the play ticks as a whole about half as expensive. The worst tick matters more than the average,
  because a tick that does not fit drops a frame.

The targets are coarse and will be adjusted as dampf's optimizations are measured over the next
weeks. Running the game logic at 30 Hz on the heaviest ticks stays as a fallback, decided only on
measured numbers. The snapshot of 2026-11-17 has no such change yet (`bubble.c` line 389 still
compares all pairs).

Behaviour: changes. Replays recorded before the change drift and must be re-recorded; please keep
`tests/replays` up to date, since dampf checks every recording there on the SNES.

## 2. Uploads in `video_vblank` can wait forever, and one budget check covers two copies (was 8)

Found while building a playable ROM with dampf's platform layer (`snesDmaFits` reads the line
counter).

Nothing guarantees that an upload ever runs. With the screen on, every upload in `video.c` waits
until it fits into what is left of the vertical blank, and the code before it (scroll, colour
math, the HDMA tables, the OAM copy) takes time too: about 25 of the 37 lines with dampf's code
when the ROM was first built, all of them with slower code. dampf has since made each register
write cheaper (33 to 15 cycles); the share has not been measured again. A room tileset (608 bytes)
then never fits and the room stays drawn with the title logo's tiles, which is what dampf's
unoptimized build showed. A faster compiler hides this, but more work in the vertical blank or one
long tick brings it back. Suggestion: switch the screen off for one frame when a tileset or a sheet
row has waited more than a few frames (as the title logo already does), or move the register
writes after the uploads.

The tileset block calls `fits(used, GFX_TILESET_TILES * 32 + 32)` once for the tiles and the
palette, then makes two copies. Each copy needs setup (measured in Mesen with dampf, before the
register writes got cheaper: 3.5 lines from the check to the tileset copy, 1.5 more for the
palette), and `snesDmaFits` can allow for only one, so dampf keeps a margin of 8 lines. A `fits()`
before each copy, or the palette copied in the next vertical blank, would let that margin shrink.

Behaviour: unchanged. It affects when things reach the screen, not the game state.

## 3. `separate()` compares every pair of bubbles (was 2)

`bubble.c`, `separate()`: for 16 bubbles up to 120 pairs per frame, each with four `PX()`
extractions, two absolute values and four comparisons in 16-bit arithmetic. In dampf's profile it
was 8.3 % of the play ticks with dampf's unoptimized code; its pair loop is 16 % with dampf's code
now. (In the Calypsi profile its caller `bubbles_update` is 14 % of all instructions, about 7,300 per
call; Calypsi may have inlined `separate()`.)

Most pairs are far apart. A sweep skips them: keep the floating bubbles in a small index list
sorted by x (an insertion sort is nearly free, because the order hardly changes between frames) and
stop the inner loop once the x distance reaches `STICK_REACH`.

Behaviour: changes slightly. Each pair moves its bubbles in place, so a different pair order can
change positions by a few sub-pixels; replays must be re-recorded. Cost: about 30 lines and a sorted
list to keep up to date.

Er-Tüp's `collide` (`examples/ertuep/src/objects.c`, 2026-11-19) does it without sorting: the live
shots go into buckets of 32 pixels by x, a bit per shot in a 16-bit mask, and an enemy looks only
at the shots of the one to three buckets its box reaches, in the table's order, so the result is the
same as with all pairs. In its densest wave that took the pair loop from about 190,000 master cycles
a tick to about 75,000. For `separate()`, where both sides of a pair are bubbles, the same masks by
x (or by x and y) would keep the pair order too.

## 4. `game.h` says `game_vblank` runs from the vertical blank interrupt (was 9)

dampf's platform layer calls `game_vblank()` from the main loop, right after waiting for the
vertical blank. dampf keeps function frames at fixed places in RAM, so C code called from an NMI
that interrupts a tick (which happens whenever a tick takes more than a frame) would overwrite the
frames of the interrupted code. Calypsi has the same kind of problem with its static or soft-stack
frames unless the NMI handler saves them.

Suggestion: write "in the vertical blank after each tick, before the next tick starts" in
`game.h`, and keep `game_vblank` free of anything that assumes it can interrupt `game_tick`.

Behaviour: unchanged.

## 5. More recordings would test dampf on more of the game (was 6)

dampf plays the game's recordings (`tests/replays/*.bbr`) on the SNES and checks every recorded
checksum against the PC. There is one so far, `plus_bot_seed42.bbr`: 3,000 ticks of round 1 in Plus
mode, one player. Everything else is covered only by the benchmark, which plays round 1 by a fixed
script.

Most useful: the boss fight, a secret room, the special bubbles (water, fire, lightning, the
letters), two players with the CPU partner, losing a life and game over, the high score entry, and
a late round. A few minutes each is enough; dampf picks up whatever is in `tests/replays/` when the
snapshot is refreshed.

Behaviour: unchanged.

## 6. Shifts by a variable count in drawing code (was 3)

`enemy.c`, `enemies_draw()`: `g_frame >> (fast - 1)`, `g_frame >> fast` and `g_frame >> (fast + 1)`
with `fast` 3 or 4, once per enemy and frame. The 65816 shifts one bit per instruction, so a
variable count becomes a loop or a library call. The same pattern is in `draw_hud_values()`,
`join_by_start()` and `build_windows()` (8 places in all). `enemies_draw` is the third most
expensive function in the Calypsi profile (9 %, about 4,800 instructions per call, about 600 per
enemy slot for one sprite each).

Two behaviour-identical forms: constant shifts,

```c
uint8_t step = (uint8_t)(e->angry ? (g_frame >> 3) : (g_frame >> 4));
```

or the animation bits computed once per frame (`anim2 = (g_frame >> 2) & 1`, ...) and read by all
drawing code. In the same function, `attr` and `fast` are computed before
`if (!e->active ...) continue;`; moving them below the check saves that work for every empty slot.

dampf handles both, but not yet (a variable shift is still a loop, and it does not move work behind
a `continue`). Worth doing in the game only if Calypsi stays in use for a while.

Behaviour: unchanged.

## 7. Profile shares on functions with 0 calls (was 4)

`tools/snesbench/profile.py` on the Calypsi profile `calypsi_fix` (ticks 900–1700) gives large
shares to functions with 0 counted calls: `vacuum_start` 4.2 %, `enemies_touch` 3.9 %, `sfx` 2.1 %,
`enemies_defeat_at` 2.0 %, `_Mul16` 1.8 %. These instructions most likely belong to code Calypsi
placed after those functions (split-off blocks, or `static` functions without a symbol in the map).
Until that is fixed, the ranking is not reliable below the top three. Running `profile.py` with
`ASM_DIR` set may already fix part of it.

Behaviour: unchanged; this is about the measuring tool.

## 8. The dev kit's console API has new names: `sdevkit_` (2026-11-19)

The dev kit renamed its console API so that every name a game can use starts with `sdevkit_`
(functions, variables) or `SDEVKIT_` (macros), in snake case: `setMode` is `sdevkit_set_mode`,
`oamSetXY` is `sdevkit_oam_set_xy`, `oamMemory` is `sdevkit_oam_memory`, `KEY_A` is
`SDEVKIT_KEY_A`, `BG_MODE1` is `SDEVKIT_BG_MODE1`. The header is `"sdevkit/snes.h"` (and
`"sdevkit/loop.h"` for the frame rhythm), and it uses the `<stdint.h>` types; `u8`, `u16`, `s16`
and `u32` are gone from it. `snes_vram()`, which `video.c` calls for its debug check of the maps
(line 593), is `sdevkit_dbg_vram()`; it exists on the SNES too (a single zero word there). The
reason is dampf's ADR record 34: unprefixed names clashed (raylib's `KEY_UP`), and on the desktop
the build now checks that a game calls nothing but the kit.

What the game does: run the dev kit's `tools/sdevkit_rename.py src/game` once. It changes
identifiers only, not strings or comments (`--comments` changes those too, `--dry-run` only
lists the files), and rewrites `#include "snes/snes.h"` to `"sdevkit/snes.h"`. On the dev kit's
copy of the game (the snapshot) it changes 82 names, all in `video.c`, and the result builds the
same ROM, byte for byte. The game's own `src/snes/snes.h` is then no longer included by the game
code; its PC side belongs to the game's own pseudo SNES until the game uses the kit's desktop
build (dampf's roadmap M7). The kit's copy of the pseudo SNES is called Rachael now
(`lib/rachael`, dampf's ADR record 35); nothing the game calls changes with that.

Until then nothing breaks: the kit keeps the old names in a deprecated compatibility header
(`lib/include/snes/`), and the game builds with the kit unchanged. Once the game uses the new
names and dampf's snapshot is refreshed, the kit removes that header.

Behaviour: unchanged; the ROM is the same, so the replays stay.

## 9. Two guides for writing the game's code (2026-11-19)

The dev kit has two guides for whoever writes a game's code: `lib/C_STYLE.md` (what dampf rejects
and allows, what is slow and fast with dampf today and what makes no difference; each speed rule
is a measurement that dampf's `StyleGuideTest` checks, so the guide changes when dampf does) and
`lib/SNES_HARDWARE.md` (what the console allows and punishes, which features the kit has on the
SNES and on the desktop, and how each rule is checked).

Suggested: one line in the game's `CLAUDE.md` (or `AGENTS.md`) that points to both, in the dev
kit's copy the game builds with, and to read them before writing code for the SNES build. Some
rules go against general 65816 habit and are worth knowing before the game's hot loops are
touched: a pointer walk costs more than indexing (`slow-pointer-walk`), a multiplication of two
bytes as much as of two words (`same-multiply-width`), and a 32-bit value more than twice a
16-bit one (`slow-32-bit`). The width of a loop counter is not a general rule: over a byte array
a 16-bit counter is cheaper, over a word array an 8-bit one (`slow-u8-counter`,
`slow-u16-counter-words`); when the dev kit's examples were rewritten with 16-bit counters
throughout, most functions got slower. Apply a speed rule only where the timing report or the
profile shows the code is hot, and measure it.

Behaviour: unchanged; this is about the game's instructions for its AI.

## 10. A comparison with a sum of a variable is a 32-bit comparison (2026-11-19)

C adds an `int16_t` and a constant in `int`, which has 32 bits with gcc and so with dampf; a
comparison with such a sum is a 32-bit comparison, about 160 cycles where a 16-bit one is about
95 (`lib/C_STYLE.md`, `slow-compare-sum`, measured). Where the sum fits 16 bits, a cast says so:
`if (px > (int16_t)(target + 1))`. Er-Tüp's enemies, which steer toward the ship this way every
frame, got their casts on 2026-11-19. In the game: `bubble.c` lines 351 and 517
(`TOP_LIMIT + PX(...) + 1`), `enemy.c` line 685 (`target + 1`), `game.c` line 838
(`limit + CHASER_DELAY`). None of them is in a pair loop, so the gain is small; worth it where
the profile shows the line.

Behaviour: unchanged, as long as the sums fit 16 bits (they do: they are positions and frame
counts). Cost: a cast each.

## Done

- The compiler benchmark never ran the CPU partner (was 1). Fixed in the snapshot of 2026-11-17:
  `bench.c` plays `CPU_TICKS` (1,800) more ticks with player 2 as the CPU partner, and `report.py`
  lists them as "+cpu".
- `(unsigned)` casts in `video.c` broke the fixed-width rule (was 5). Fixed in the snapshot of
  2026-11-17: both casts (lines 339 and 340) are `(uint8_t)`, and no `(unsigned)` is left in
  `src/game`. `tools/int16check` still does not catch casts.
