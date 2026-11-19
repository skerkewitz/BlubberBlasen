# ADR-0007: The game is built with superdevkit, on the SNES and on the PC

Date: 2026-11-19
Status: accepted

## Context

ADR-0005 shaped the game's picture API after PVSnesLib so that PVSnesLib could serve it on the
console, and left the choice of a 65816 compiler to the stage 3 spike. The spike measured both
candidates in November 2026. PVSnesLib's 816-tcc needed about 6 frames per tick in round 1 and
drifted from the PC's results at tick 1133. Calypsi needed about 2 frames but miscompiled signed
shifts, comparisons of divisions and direct-page arrays, and still drifted on the CPU partner's
path. Neither could carry the game.

In the meantime a compiler written for this game, dampf, became part of superdevkit, a
development kit for SNES games in C. The kit has:

- a console API, `sdevkit/snes.h`, which is the game's PVSnesLib-shaped API renamed (every name
  starts with `sdevkit_` or `SDEVKIT_`);
- its library for the SNES, `lib/snes`, with start-up, a splash screen and a main loop that calls
  `game_init`, `game_tick` and `game_vblank`, the same frame rhythm as the game's `game.h`;
- Rachael, `lib/rachael`, which is this game's `src/snes` taken over and extended (mode 7, register
  writes she does not draw are counted, a checksum per tick);
- a desktop frontend on raylib, which has no sound, no save RAM and no debug view yet (the kit's
  milestone M7), and no web frontend yet (M8);
- a way to compare a ROM with the PC: the game names bytes with `sdevkit_dbg_hash`, a recording
  carries the checksum of every tick, and `dev/replay.sh` plays it in dampf's emulator.

## Decision

1. **The ROM is built with superdevkit.** `build_snes.sh` converts the data as plain C and calls
   dampf with `blubberblasen.args` plus the kit's headers and `lib/snes`. PVSnesLib, 816-tcc and
   Calypsi are no longer used: `src/platform/snes`, the `--snes` outputs of `map2c` and `gfx2c` and
   the compiler benchmark `tools/snesbench` are gone.
2. **The game calls only the console API** (`sdevkit/snes.h`, converted with the kit's
   `tools/sdevkit_rename.py`).
3. **The PC and web builds draw with Rachael** from the kit instead of `src/snes`, which is gone.
   The game's own raylib frontend stays (sound, save RAM, the debug view, recordings, the web
   page and its touch controls) until the kit's frontends have what it needs; then the game moves
   to them (the kit's M7 and M8). The build finds the kit through `SUPERDEVKIT_HOME`.
4. **The game's line y is row y of the picture.** The SNES draws row y from scanline y + 1 and
   never shows scanline 0, and HDMA's first table entry lands on row 0 (measured in Mesen with a
   test ROM, and Rachael does the same). So `video.c` scrolls both backgrounds up by one line,
   writes a sprite's y unchanged and builds its HDMA tables from line 0. Before, with sprites
   written at y - 1 and the backgrounds unscrolled, the console showed the picture one line
   higher than the PC, and the HDMA effects one line late (the first line of the room under the
   HUD stayed black).
5. **The ROM is checked against the PC tick by tick.** `video_frame` names both tilemaps with
   `sdevkit_dbg_hash` (the kit's main loop adds the sprite table), and `tools/snescheck` writes a
   recording of the game on Rachael for `dev/replay.sh`.
6. `BB_FAST` stays as an empty marker on the hottest globals: dampf keeps only function frames
   in the direct page and has no hint for a global yet.
7. **Coding rule 7 and `tools/int16check` are dropped.** They made the code correct with a
   16-bit `int`, which 816-tcc and Calypsi have. dampf's `int` has 32 bits, as gcc's and MSVC's,
   so the PC and the SNES compute the same without the rule. The small types of ADR-0002 stay,
   for speed and RAM.

## Alternatives

- **Keep PVSnesLib and 816-tcc for the ROM.** Too slow and wrong at tick 1133; dropped.
- **Use the kit's desktop frontend now.** It would cost sound, saved high scores, the debug view
  and the game's recording format until the kit has them.
- **Keep `src/snes` for the PC.** Two copies of the same pseudo SNES would drift apart; Rachael
  already differs from it in the line offset of point 4.

## Consequences

- Every build needs a superdevkit home. MSVC on Windows reaches one inside WSL by its UNC path.
- On the PC the picture is unchanged, pixel for pixel (compared at five moments of bot play). On
  the console the whole 224 lines the game draws are now visible, and the HDMA effects line up.
- `tests/test_snes.c` now tests Rachael. The tests stay here until the kit takes them over.
- With dampf's naive code generator the ROM matches the PC for 36,000 ticks of bot play. The
  optimizing one differs from tick 24,950 on, which is a dampf bug (`docs/kit-findings.md`).
- The ROM has no sound and no save RAM until the kit's library for the SNES has them.
- With rule 7 gone, arithmetic in `int` is no longer checked against 16 bits. It is still
  slower on the 65816 than 16-bit arithmetic (the kit's `lib/C_STYLE.md`, `slow-32-bit`).
