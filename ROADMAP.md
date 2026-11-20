# Roadmap

The project has three stages (see the README): a playable prototype on PC, a "pseudo SNES"
version whose platform layer behaves like the real console, and a port to real SNES hardware.

Stage 1 has reached its goal: the game is playable from round 1 to 100, with the boss, both
endings, secret rooms, high scores and a web build. What a finished game still needs (art, story
scenes, round structure, difficulty, options) is collected in [GAME-TODO.md](GAME-TODO.md). That
list waits, because a bigger question comes first: **does the game run on a SNES at all?** Art
and content built before that is answered may have to be redone.

So the next steps aim at that question. Stage 2 is split in two, with a stage 3 spike between the
halves, so that real 65816 measurements steer the second half instead of arriving after it.

| Step | Goal | Status |
|---|---|---|
| 1 | Playable prototype on PC (C99 + raylib) | Done |
| 2a | Data and limits shaped like the SNES | Done (2026-11-16) |
| 3-spike | Game code compiled for the 65816 and measured | Done (2026-11-20): the whole game runs as a ROM; [report](docs/snes-spike-report.md) |
| 2b | Fix what the spike found | In progress: room change stall fixed (2026-11-20); next vacuums, then measure the kit's M9 |
| 3-POC | One complete room group running on SNES | Technically overtaken (the whole game runs); left: the art of one group, real hardware |
| Full game | The items in GAME-TODO.md | After the POC |

## Step 2a: data and limits shaped like the SNES

Why: ADR-0004 fixes the SNES limits for all art, and the spike needs game data in a form a SNES
can hold. Both need the same groundwork. How the layer is built is decided in ADR-0005.

What, in this order:

- Done: the Dear ImGui debug view (ADR-0005, point 7): one window with docked panels, desktop
  debug builds only (README, *Debug view*). It comes first because it helps in stage 1 already and makes the rest of this step
  easier to check.
- Done: the frontend with SNES-shaped memory and a PVSnesLib-like API (`src/snes`): VRAM
  (64 KB), CGRAM (256 colours), OAM (128 sprites), registers and HDMA. The game draws through it
  (`src/game/video.c`).
- Done: graphics compiled into the program. `gfx2c` turns the PNGs into 4 bpp tiles and 15-bit
  palettes, with no asset over 32 KB, and one sprite sheet per room group within 128 cells of
  16 × 16 (ADR-0006). The platform no longer loads PNGs or tints anything.
- Done: the art checker from ADR-0004, as part of `gfx2c`.
- Done in the same library, tested headless (`tests/test_snes.c`, since 2026-11-19 in superdevkit with Rachael): a software renderer that
  draws the picture line by line from that memory, with
  three background layers as in mode 1 (walls and platforms, background, HUD and text) and the
  sprite limits of 32 sprites and 272 px per line, so that overflows are visible on the PC.
- Done: VRAM, palette and OAM viewers as debug panels, with the sprite-limit counts and the DMA
  bytes per frame.
- Done: a check that the game code is correct with a 16-bit `int`, which is what 65816 compilers
  use (`tools/int16check`). It found no place that relies on a 32-bit `int`. It runs in Linux or
  WSL next to the web build, not in the Windows CTest run. Dropped on 2026-11-19 with the rule
  (ADR-0007): dampf's `int` has 32 bits, as on the PC.

Done when the PC version draws everything through the new layer and the game code no longer
knows about PNGs or tints, the art checker passes on
the current (placeholder) assets, the 16-bit `int` check is part of the test run, all tests and
the replay still pass, and ADR-0001 (C99 with a platform layer) and ADR-0002 (the game loop and
data layout) are written.

## Step 3-spike: compile and measure

Why: the largest risk is CPU time, not graphics. The 65816 runs at about 3.6 MHz, its C compilers
produce slow code, and parts of our game code were written for a fast PC: the CPU partner
simulates half a second of moves for several options every frame, and bubbles are checked against
each other in pairs. Only real 65816 code can say whether that fits into one frame.

What:

- Choose a 65816 C compiler (candidates: the one in PVSnesLib, Calypsi) and record the choice in
  an ADR. Done (2026-11-19, ADR-0007): neither. Both were measured in 2026-11 (816-tcc about 6
  frames per tick in round 1, Calypsi about 2 with several miscompiles); the game is built with
  superdevkit, whose compiler dampf was written for it, and PVSnesLib, 816-tcc and Calypsi are
  gone from the build.
- Compile `src/game/` unchanged, with a minimal SNES platform layer: input, OAM and tile maps
  copied in vertical blank, no sound, placeholder graphics. If PVSnesLib is chosen, the
  platform layer is PVSnesLib itself, as ADR-0005 intends.
- Done (2026-11-16): `src/game` compiles with PVSnesLib's 816-tcc after two small changes
  (README, *SNES build*). The data goes into ROM as assembly (`map2c` / `gfx2c --snes`).
  The game uploads to VRAM only in the vertical blank, and the pseudo SNES enforces that (the
  first ROM showed what happens otherwise). Code about 120 KB, ROM 310 KB of 512 KB, RAM
  about 25 KB.
- Done (2026-11-19): the ROM is built with superdevkit (`build_snes.sh`, README *SNES build*):
  dampf, the kit's library for the SNES with its main loop, the data as plain C. PC and web draw
  with Rachael, the kit's SNES in C, instead of `src/snes`, and give the same picture as before,
  pixel for pixel. `tools/snescheck` compares the ROM with the PC tick by tick in dampf's
  emulator: 36,000 ticks of bot play match with dampf's naive code generator; its optimizing
  one differed from tick 24,950 on, a dampf bug the kit fixed the same day. Since then the
  optimizing ROM matches in all four scenarios.
- Done (2026-11-20): frame time in three cases (a quiet room, a room full of bubbles, two players
  with the CPU partner), the costliest functions, and ROM and RAM use, measured on the ROM in
  dampf's emulator over 36,000 ticks in four scenarios: [docs/snes-spike-report.md](docs/snes-spike-report.md).

Done when there is a short report in `docs/` with the measured numbers, the functions that cost
the most, and a list of what has to change, each item with an estimate. If the game logic already
fits into one frame, the list is short and step 2b is mostly the pseudo SNES layer.

## Step 2b: fix and finish

Why: the spike's report (docs/snes-spike-report.md) decides what this step contains. In play the
game needs 0.70 to 0.85 frames per tick on average against a goal of 0.6, about a quarter of the
ticks in Plus mode take longer than a frame, and every room change stalls for about a fifth of a
second. No single function dominates, and hand-written assembly is not needed.

What, in this order:

- Done (2026-11-20): the stall at every room change, 13 frames down to 1.5 (report, item 1).
- Vacuums: keep the window's lines while a vacuum does not change (report, item 2).
- Measure again when the kit's register allocation (M9) is in, on the same four scenarios. Then
  decide the rest: a design limit on floating bubbles, fewer sprites per frame, a cheaper CPU
  partner danger check (report, items 3 to 5).
- Each change proved on the PC by the existing tests and on the emulator by a new measurement.
- Sprite tiles streamed in if the sheets per room group turn out too small. (The DMA budget per
  vertical blank is in Rachael, superdevkit's SNES in C, ADR-0007.)

Done when the PC and the SNES build run the same replay to the same result, and the busiest room
measured in the spike fits into one frame on the SNES.

## Step 3-POC: one room group on SNES

Status (2026-11-20): technically overtaken. The whole game already runs as a ROM, with sound
effects and save RAM, and matches the PC tick for tick. What this step still adds is the art of
one finished group and a run on real hardware.

Why: one complete group proves the whole chain, from art and data to a running cartridge, at a
size small enough to finish.

What: the Cellar (rounds 1–5 in the new structure of `docs/story.md`), drawn in the style of
ADR-0004: background, walls and platforms, both players, the enemies of the group, bubbles and
food. No music and no sound effects (ADR-0005); the PC version keeps its sound. Its boss may be a
placeholder.

Done when the five rounds can be played from start to finish in an emulator and, if available,
on real hardware with a flash cartridge, at full speed and without sprite flicker that hides
anything important.

## After the POC

Sound comes first: the sound effects are done (ADR-0008, the same samples on every platform);
music on the SNES waits for the kit, and has about 20 KB of audio RAM left beside the effects. With that working, the items in [GAME-TODO.md](GAME-TODO.md) become the plan: round structure,
bosses, cut scenes, art for all groups, difficulty and options. The PC version stays the place
where the game is developed and tested; the SNES build follows it.
