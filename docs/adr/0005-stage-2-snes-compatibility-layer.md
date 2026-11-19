# ADR-0005: Stage 2 as a SNES compatibility layer, with a Dear ImGui debug view

Date: 2026-11-16
Status: accepted; amended by ADR-0007 (the API is superdevkit's, Rachael replaces `src/snes`, PVSnesLib is not used)

## Context

Stage 1 is playable. Before more art and content are built (`GAME-TODO.md`), we want to know
whether the game can run on a SNES at all (`ROADMAP.md`). Stage 2 has to bring the PC version so
close to the console that the game code can move to the SNES unchanged, without building an
emulator.

Today the interface in `src/game/game.h` already looks like a console (shadow sprite table, tile
maps, colour windows), but graphics are 24-bit PNGs loaded from disk, a "palette" is a tint
multiplied in by raylib, and nothing holds the game to the sizes of SNES memory.

ADR-0004 fixed the SNES limits for all art (15-bit colours, 4 bpp tiles with one palette each,
three background layers, 8 sprite palettes, 32 sprites per scanline, 16 KB of sprite tiles).

Debugging the game is slow: developer switches sit on F-keys, and object state, the CPU partner's
decisions and, from stage 2 on, video memory can only be seen by adding prints.

## Decision

1. **The platform splits into a frontend and a backend.** The frontend is the interface the game
   code calls; it behaves like the SNES and keeps SNES-shaped memory: VRAM (64 KB), CGRAM
   (256 colours of 15 bits), OAM (128 sprites plus the high table) and the video settings
   (mode 1, scroll, layers on, windows, colour math). The backend is raylib: it reads that memory
   each frame, draws it and plays it. Only the backend knows raylib.

2. **The API is at library level and follows PVSnesLib.** The game calls functions such as "set
   sprite n", "copy these tiles to VRAM address X", "load palette 3" or "set BG2 scroll", named
   and shaped like PVSnesLib's where PVSnesLib has one. The game does not write hardware registers
   directly; emulating their meaning (write order, latches, increment modes) would be most of an
   emulator. In stage 3 the same calls can then be served by PVSnesLib itself. That pre-selects
   PVSnesLib's toolchain for the 65816 spike; the spike confirms or overturns it in its own ADR.

3. **The backend draws with a software renderer.** It renders the 224 lines of a 256 × 224
   picture from VRAM, CGRAM and OAM, line by line as the SNES does, so that the 32-sprites and
   272-pixel limits per line, layer priorities, windows, colour math and per-line changes come
   out right. The finished picture goes to raylib as one texture; the CRT shader (RETRO-EFFECT.md)
   stays on top. A shader that decodes tiles was considered and rejected: per-line rules are
   awkward on a GPU, and a software renderer can be checked pixel by pixel in tests without one.

4. **Graphics are compiled into the program.** A build tool converts the PNGs into 4 bpp planar
   tiles and 15-bit palettes as `const` arrays ("ROM"). The game copies them into VRAM by symbol.
   VRAM addresses are real SNES addresses; ROM addresses are not faked. No single asset may be
   larger than 32 KB, the size of a ROM bank; the tool fails the build otherwise.

5. **One sprite sheet per room group.** All sprites of a round must fit into the 16 KB of sprite
   VRAM (128 cells of 16 × 16). Each room group (see `docs/story.md`) gets its own sheet with the
   shared sprites and the enemies and boss of that group. A rough count with today's placeholder
   frames: shared sprites take about 103 cells, each enemy type about 3. Score pop-ups (22 cells)
   and the life icons (2 cells, HUD on BG3) are the first candidates to shrink or move. If a
   group's sheet still does not fit, the numbers come back for a decision before anything is cut.

6. **No sound in the stage 3 proof of concept.** On the PC, sound and music stay as they are
   (raylib). On the SNES build the sound requests do nothing until sound is taken up after the
   proof of concept.

7. **A debug view with Dear ImGui**, for stage 1 and stage 2 alike. It is one window with docked
   panels: the game picture is one panel, the debug panels sit around it. raylib supports only one
   window; real separate windows (ImGui multi-viewport) were considered and rejected as fragile
   with rlImGui. Rules:
   - Desktop debug builds only (`BB_DEBUG_SWITCHES`), switched on by a command-line option or a
     key. Normal play and the web build stay as they are.
   - Dear ImGui is C++ and lives only in the platform backend, in one `.cpp` file behind a C
     interface, linked through rlImGui. The game code stays C99 and never includes it.
   - First panels: object tables (players, enemies, bubbles), the developer switches as
     checkboxes, round picker, pause and single-frame step, the CPU partner's goal and tried
     moves, and in stage 2 viewers for VRAM tiles, palettes and OAM.
   - Anything a panel changes in the game state is recorded in the replay file, as the F-keys
     are now, so replays stay deterministic. Tuning values that should get sliders become
     variables in debug builds; release builds keep them as constants.

## Consequences

- The game code changes where it draws: it loads tiles and palettes into VRAM and CGRAM itself
  instead of naming PNG cells and tints. Player 2's colours become a real palette instead of a
  colour swap in the platform.
- The current sprite sheet (256 cells) has to be split per room group before stage 2 can draw
  through the new layer.
- CMake gets a C++ compiler and two fetched dependencies (Dear ImGui, rlImGui) for desktop debug
  builds.
- The layer proves that the data fits the SNES, not that the code is fast enough; that is what
  the 65816 spike in `ROADMAP.md` measures.
