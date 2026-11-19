# ADR-0002: An old-school game loop and data layout, as a SNES game has

Date: 2026-11-16 (records the decision the project started with on 2026-11-13)
Status: accepted; amended by ADR-0007 (no 16-bit `int` check: dampf's `int` has 32 bits)

## Context

A 65816 at about 3.6 MHz with 128 KB of work RAM leaves no room for the patterns of modern game
code: objects allocated and freed at run time, entity component systems, floating-point physics,
timers in seconds. Games of the time were built differently: a fixed frame rhythm, fixed tables,
integer arithmetic and data in ROM. If the PC version is built the same way from the start, the
port in stage 3 changes the platform, not the game.

A second reason is testing: a game that only depends on its start and its input can be replayed
and compared frame by frame.

## Decision

1. **One main loop, ticked once per frame at 60 Hz** (`game_tick`). The top level is a state
   machine: an `enum` of states and a `switch`.
2. **All game state is static, in fixed-size tables** (`g_players[2]`, `g_enemies[8]`,
   `g_bubbles[16]`, ...). No heap, no lists that grow; an object is active or not.
3. **Integer arithmetic only:** positions and speeds in 8.8 fixed point (`fixed.h`), no `float` or
   `double`. Multiplication and division stay out of hot paths where shifts or tables do; scores
   are decimal digits, so they are shown and compared without division.
4. **Small types:** `uint8_t` and `uint16_t` where they fit, so that the code is right with the
   65816's 16-bit `int` (checked by `tools/int16check`).
5. **Time is counted in frames**, never in seconds or wall-clock time.
6. **Levels and tables are `const` data**, "ROM": rooms are converted into C arrays at build time
   (`tools/map2c`), graphics too (`tools/gfx2c`).
7. **Randomness from a 16-bit LFSR** seeded at the start, so the same seed and the same input give
   the same game.

## Consequences

- The game is deterministic. That gives recordings and replays for debugging
  (`docs/DEBUG-REPLAY.md`), the determinism test, stored replays as regression tests, and the room
  test with its own seed per try.
- Fixed tables set hard limits (8 enemies, 16 bubbles, 16 items, 128 sprites). New mechanics have
  to fit them or raise them on purpose, keeping an eye on the RAM budget.
- Code reads like a SNES game of the nineties rather than a modern engine; contributors used to
  engines need the README's coding rules.
