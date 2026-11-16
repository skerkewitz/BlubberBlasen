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
| 2a | Data and limits shaped like the SNES | In progress: the game draws through the pseudo SNES; viewers, the 16-bit `int` check and two ADRs remain |
| 3-spike | Game code compiled for the 65816 and measured | Planned |
| 2b | Fix what the spike found, finish the pseudo SNES layer | Planned |
| 3-POC | One complete room group running on SNES | Planned |
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
- Done in the same library, tested headless (`tests/test_snes.c`): a software renderer that
  draws the picture line by line from that memory, with
  three background layers as in mode 1 (walls and platforms, background, HUD and text) and the
  sprite limits of 32 sprites and 272 px per line, so that overflows are visible on the PC.
- VRAM, palette and OAM viewers as debug panels.
- A check that the game code is correct with a 16-bit `int`, which is what 65816 compilers use.
  Every place that relies on a 32-bit `int` is found and fixed.

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
  an ADR.
- Compile `src/game/` unchanged, with a minimal SNES platform layer: input, OAM and tile maps
  copied in vertical blank, no sound, placeholder graphics. If PVSnesLib is chosen, the
  platform layer is PVSnesLib itself, as ADR-0005 intends.
- Run round 1 in an emulator with a profiler (for example Mesen) and measure frame time in three
  cases: a quiet room, a room full of bubbles, and two players with the CPU partner.
- Measure ROM and RAM use (the SNES has 128 KB of work RAM).

Done when there is a short report in `docs/` with the measured numbers, the functions that cost
the most, and a list of what has to change, each item with an estimate. If the game logic already
fits into one frame, the list is short and step 2b is mostly the pseudo SNES layer.

## Step 2b: fix and finish

Why: the spike's list decides what this step contains. Likely candidates are a cheaper CPU partner,
a cap on bubbles, spreading work over two frames, or hand-written assembly for hot spots.

What:

- The changes from the spike report, each proved on the PC by the existing tests and on the
  emulator by a new measurement.
- The rest of the pseudo SNES layer: a DMA budget per vertical blank, and sprite tiles streamed in
  if the sheets per room group turn out too small.

Done when the PC and the SNES build run the same replay to the same result, and the busiest room
measured in the spike fits into one frame on the SNES.

## Step 3-POC: one room group on SNES

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

Sound comes first: sound effects and music as SPC-style data (8 channels of short samples in
64 KB), written for that chip from the start. With that working, the items in [GAME-TODO.md](GAME-TODO.md) become the plan: round structure,
bosses, cut scenes, art for all groups, difficulty and options. The PC version stays the place
where the game is developed and tested; the SNES build follows it.
