# The SNES spike: what the ROM measures (2026-11-20)

This is the report that closes step 3-spike of the [roadmap](../ROADMAP.md): does Blubber
Blasen run on a SNES, how fast, and what has to change. Short answer: the whole game runs as a
ROM and plays exactly like the PC version, but in play it needs 0.70 to 0.85 frames of CPU time per
tick on average, and the goal is 0.6. In Plus mode about a quarter of all ticks take longer than a
frame, so the game slows down visibly. Room changes also stall for a moment.

## How it was measured

- **The ROM:** `superdevkit build snes` with superdevkit 0.7.1 (dampf's optimizing code generator),
  512 KB LoROM. It matches the PC tick for tick: `snescheck` recordings of 36,000 ticks (10
  minutes) in all four scenarios below give the same checksum in every tick on the ROM (the kit's
  `dev/replay.sh`).
- **The scenarios:** `snescheck`'s bot plays player 1 from the title on, for 36,000 ticks each:
  the bot alone in Arcade and in Plus mode, and the bot with the CPU partner as player 2, again in
  both modes. The bots reach rounds 9 to 12. Rounds 1 to 12 are the game's busiest by design
  (several enemies, many bubbles), but the later rooms, the boss and the secret rooms are not in
  these numbers.
- **The work of a tick:** dampf's emulator runs the ROM and counts the master cycles of every tick
  without the wait for the vertical blank (a scratch tool on `dampf.emu`, like the kit's
  `ExampleBench`). One frame is 357,368 master cycles, so 1.0 means the tick takes a whole frame;
  above 1.0 the main loop misses a vertical blank and the game runs slower. `snescheck --trace`
  gives each tick's state, round and the numbers of bubbles, enemies, items and vacuums.
- **Where the time goes:** the kit's `dampf.emu.ExampleProfile`, per function and per C line,
  over ticks 1,000 to 36,000 of each run.

## Frame time in play

Work per tick while a round is played (state PLAY), in frames:

| Scenario | Mean | 90 % of ticks below | Busiest tick | Ticks over one frame |
| --- | ---: | ---: | ---: | ---: |
| Bot, Arcade | 0.70 | 0.95 | 1.97 | 7.5 % |
| Bot, Plus | 0.83 | 1.26 | 2.31 | 28.7 % |
| Bot and CPU partner, Arcade | 0.78 | 1.07 | 2.66 | 16.7 % |
| Bot and CPU partner, Plus | 0.85 | 1.28 | 2.56 | 28.4 % |

The three cases the roadmap asked for, from the same runs (mean, and 90 % of ticks below):

| Case | Arcade | Plus |
| --- | --- | --- |
| A quiet room: 0 to 3 bubbles, no vacuum | 0.59, 0.83 | 0.61, 0.81 |
| A room full of bubbles: 12 to 16, no vacuum | 0.96, 1.24 | 0.94, 1.25 |
| Two players with the CPU partner, 12 to 16 bubbles | 0.99, 1.27 | 1.04, 1.26 |
| A vacuum on the screen (Plus only) | — | 1.10, 1.48 (with the CPU partner 1.21, 1.54) |

What the numbers say:

- Even a quiet room needs about 0.6 frames: the goal is reached only when almost nothing
  happens. The base load is the sprites (writing the sprite table) and the per-object updates.
- Bubbles cost the most as they pile up: from about 0.6 with a few to about 1.0 with 12 to 16.
- Vacuums are the most expensive single feature: about 1.1 to 1.2 frames while one is on the
  screen, up to 2.3 to 2.6 at worst.
- The CPU partner adds about 0.05 to 0.1 frames on average. Its worst tick (2.66 frames, round 3,
  six enemies) is its danger check meeting many enemies at once.

## Room changes and screens

Outside of play, most ticks are cheap (the transition scroll about 0.3 frames, the round intro
about 0.5), with a few long single ticks:

| Moment | Work of the tick | What does it |
| --- | ---: | --- |
| Start of every room change | 12.7 to 13.3 frames | `level_load` (63 %): the tile shape of every cell (`autotile`) asks `level_solid_tile` up to four times per cell; `stage_rows` (27 %): the whole new page, 32 rows, turned into VRAM entries in one tick |
| Start of every round intro | about 4.9 frames | `stage_rows` again, the round intro's text (`text_put`) |
| Title and story pages | 7.9 to 8.5 frames, once | loading the title's tiles and text |
| Power on | 6.9 frames, then about 2 s of black | the kit uploads 43,686 bytes of sound samples (ADR-0008) |

The room change is the one a player notices: the scroll stops for about a fifth of a second at
its start, every round.

## Where the time goes

Share of the work (the wait for the vertical blank left out), ticks 1,000 to 36,000:

| Function | Bot | Bot, Plus | Bot and CPU | Bot and CPU, Plus |
| --- | ---: | ---: | ---: | ---: |
| `oam_add` (writing the sprite table) | 11.4 % | 10.5 % | 12.3 % | 9.7 % |
| `separate` (bubbles against each other) | 11.5 % | 8.4 % | 11.8 % | 9.7 % |
| `bubbles_update` | 9.3 % | 6.9 % | 8.5 % | 7.6 % |
| `vacuums_windows` (the vacuum's colour window) | 0.8 % | 11.2 % | 0.7 % | 9.6 % |
| `update_play` | 6.1 % | 4.3 % | 4.6 % | 5.2 % |
| `bubbles_draw` | 5.4 % | 4.3 % | 5.3 % | 4.4 % |
| `level_on_ground` | 3.8 % | 4.5 % | 4.6 % | 4.7 % |
| `build_oam` | 4.7 % | 4.1 % | 4.4 % | 4.0 % |
| `player_update` | 4.6 % | 3.6 % | 4.4 % | 4.0 % |
| `enemies_draw` | 3.9 % | 3.9 % | 4.1 % | 3.3 % |
| `items_update`, `items_draw` | 7.5 % | 6.5 % | 6.9 % | 6.4 % |
| `video_vblank`, `video_frame` | 6.6 % | 5.7 % | 6.1 % | 5.6 % |

No single function dominates. Drawing (`oam_add`, `build_oam` and the `*_draw` functions) is
about a third of the work, bubbles (`separate`, `bubbles_update`) about a fifth, and in Plus the
vacuum's window about a tenth. The cheap fixes on the game's side are done already (2026-11-19:
no divisions in the vacuum, no variable multiplications in the CPU partner, sprites written in
the SNES's format, the HUD drawn only when it changes, the vacuum's window lines written straight
into the HDMA tables). What is left is
mostly how dampf compiles ordinary code: every intermediate value goes through the direct page,
because dampf has no register allocation yet (the kit's M9). The bubble separation, measured as
a benchmark for the kit (`docs/kit-bench/`, finding 9), shows this best.

## ROM and RAM

| | Used | Of | Notes |
| --- | ---: | ---: | --- |
| ROM | 343 KB | 512 KB (67 %) | code 114 KB; levels 98 KB (100 rooms of 32 x 26 cells); graphics 72 KB; sound samples 43 KB; the kit's splash screen 14 KB |
| Work RAM | 15 KB | 128 KB | the game's variables (largest: the two tile maps and the staging buffer) |
| Audio RAM | 42.7 KB | 63 KB (68 %) | the sound effects; music is not in yet |
| Save RAM | 128 bytes | | high scores and options, battery-backed (ADR-0009) |

Memory is not the problem. ROM has room for music and more art; a 1 MB cartridge would be the
next step if the art grows a lot. The audio RAM is the tightest: music will have to share the
remaining 20 KB with the effects, or the effects get shorter.

## What has to change

Ordered by what a player notices, with an estimate of the work on the game's side:

1. **The stall at every room change** (13-frame and 5-frame ticks). `level_load` can work out
   the cells' shapes from one row of solid bits at a time instead of four bounds-checked calls per
   cell, and `stage_rows` can stage a few rows per tick during the scroll instead of the whole page
   at once (the copy to VRAM takes several vertical blanks anyway). *About half a day; measurable
   right away.*
2. **Vacuums** (about 1.1 to 1.2 frames while one is on the screen). The window's lines are
   written fresh every tick; a vacuum that does not move or change size could keep last tick's.
   *About half a day to measure whether it helps.*
3. **Many bubbles** (about 1.0 frame at 12 to 16). Two ways, not exclusive: wait for the kit's
   register allocation (M9), which the separation benchmark will measure; or a design limit on
   floating bubbles (fewer at once, or older bubbles popping sooner). The limit is a game design
   decision, not a technical one. *A limit is an afternoon; M9 is the kit's.*
4. **The base load of drawing** (about a third of the work). Mostly dampf's code; the game's
   options are fewer sprites (for example drawing items every other frame) at the cost of flicker.
   *Wait for M9 first, then measure again.*
5. **The CPU partner's worst ticks** (up to 2.7 frames with many enemies). Its danger check could
   look at fewer enemies per tick when there are many, or the enemies' thinking and the partner's
   check could be spread over two frames. Both change how the game plays in detail, so the stored
   recordings would have to be made again. *Half a day each, if it still shows after M9.*
6. **The 2 s of black at power on.** The samples could be uploaded behind the kit's splash screen
   instead of after it; that is the kit's main loop. *A kit finding.*

Not needed: hand-written assembly. There is no single hot spot where it would pay, and every
improvement to dampf helps all of the code at once.

## What it means for the roadmap

The spike's question is answered: the game runs on a SNES. The full game already runs as a ROM,
with sound effects and save RAM, so the planned proof of concept (one room group on the SNES)
has nothing left to prove technically. What is open is speed: the game logic fits into one frame
in most ticks, but not in busy rooms and not with vacuums, and the goal of 0.6 frames is not met
anywhere but in quiet rooms. Items 1 and 2 are the game's to do now; items 3 to 5 are best decided
after the kit's register allocation (M9) is measured on this same setup.
