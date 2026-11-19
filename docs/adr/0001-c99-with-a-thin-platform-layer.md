# ADR-0001: The game in C99, with raylib behind a thin platform layer

Date: 2026-11-16 (records the decision the project started with on 2026-11-13)
Status: accepted; amended by ADR-0007 (superdevkit instead of PVSnesLib, no 16-bit `int` check)

## Context

Blubber Blasen is meant to run on a Super Nintendo in the end (ROADMAP.md, stage 3). The SNES has
a 65816 CPU at about 3.6 MHz, 128 KB of work RAM, no floating point, no hardware divide in the CPU
and no operating system. Its C compilers (the one in PVSnesLib, Calypsi) handle plain C, not C++,
and produce slow code.

The game first had to be playable and easy to change on a PC, with a window, a gamepad, sound and
a web build. A game written for the PC first usually depends on its engine everywhere, and a
port then means rewriting it.

## Decision

1. **The game is written in C99** (no compiler extensions; CMake sets `C_EXTENSIONS OFF` for
   `src/game`). C99 is what the 65816 compilers come closest to, and it builds unchanged with MSVC,
   GCC, Clang and Emscripten.
2. **raylib is used only in `src/platform/`.** Window, input, sound, timing, the save file and
   putting the picture on screen live there. The game code never includes `raylib.h` or any other
   platform header; the check in the README (`Coding rules`) looks for it.
3. **The interface between the two is shaped like the console**, so that the same game code can
   later sit on SNES hardware: one 16-bit joypad word per player and frame in the SNES bit layout;
   the picture as sprites, tile maps and colour windows (since ADR-0005 written into a pseudo SNES
   through PVSnesLib-shaped functions); sounds requested by number; 128 bytes of save RAM; exactly
   one game tick per 60 Hz frame.
4. **Code that only the PC needs stays outside the game:** recordings (`src/replay`, uses stdio
   and malloc), the debug view (`src/platform/debug_ui.cpp`, the only C++), the host tools
   (`tools/`). Developer switches inside the game are compiled only with `BB_DEBUG`.

## Consequences

- The game cannot use raylib's conveniences (drawing, text, collision, vectors); it does its own,
  in integers. That is more work and the reason it can move to the SNES.
- Features the SNES cannot do have no way into the game by accident: floating point, the heap and
  platform calls fail the rule check, and the 16-bit `int` check (`tools/int16check`) catches
  arithmetic that only works with a 32-bit `int`.
- The platform layer is small and can be replaced: by PVSnesLib and a SNES main loop in stage 3,
  or by something else if raylib ever stops fitting.
