# ADR-0010: The desktop from superdevkit, beside the game's own frontend

Date: 2026-11-20
Status: accepted

## Context

superdevkit 0.7 builds a game for the desktop as well as for the SNES: `superdevkit build linux`
and `superdevkit build win64` (MSVC, also from WSL) compile the game against Rachael with the kit's
frontend (`lib/desktop`): a window with the retro TV filter, sound and music, the save RAM in
`<game>.srm`, recordings in the kit's check format (they play on the ROM too), a headless
`--check`, and a debug view (F1) with pause, step, slow motion, CGRAM, VRAM tiles, the sprites,
and whatever the game registers with `sdevkit_dbg_toggle`, `_range`, `_color`, `_action` and
`sdevkit_dbg_box`. The kit's handover asks the game to drop its own desktop frontend for it.

The game's own frontend (`src/platform`, built with CMake) has more that is used daily: the
autoplay bot, start options (`--seed`, `--round`, `--plus`, `--ending`, ...), `--mute`, the ImGui
panels with text (objects, the CPU partner's goal, deaths, round status, video), `.bbr`
recordings with seed and options, stepping back and jumping in a replay. The kit has none of
that yet. The web build needs the game's frontend anyway until the kit's M8.

Both builds compiled out of the box, and the kit's desktop on Linux and on Windows played
36,000-tick recordings with every checksum the same as the PC's.

## Decision

1. **Both desktops, side by side.** `superdevkit build linux|win64` and `superdevkit run` are a way
   to play and debug the game. The CMake build with the game's own frontend stays the full
   development tool, and the web's, until the kit has what is missing (kit finding 11). Then the
   CMake desktop goes; the web follows with M8.
2. **The game registers with the kit's debug view** (`debug_register` in `game_init`): the seven
   debug switches as toggles, "spawn what" (a range) with a "spawn" button, "round" with "go to
   round", and "clear round". Buttons are flags the view sets: the next `game_tick` does the work
   at its start (`debug_actions`) and clears them. At the end of a tick `debug_boxes` draws the
   switched-on overlays: hit boxes (player green, enemy red, bubble blue, item yellow, boss
   magenta), the CPU partner's predicted dangers (red) and the air currents (a box over each run
   of cells with the same current in a row; up cyan, down orange, left white, right magenta,
   since the view draws no arrows). The game's own frontend ignores the registrations and clears
   the boxes before each tick.
3. **`BB_DEBUG` is on wherever the compiler is not dampf** (`game.h`), unless the build defines
   `BB_NO_DEBUG`. The kit's desktop build passes the game no defines, so this is how it gets the
   debug switches. CMake defines `BB_DEBUG` for development builds and `BB_NO_DEBUG` for the
   public ones (`BB_DEBUG_SWITCHES=OFF`, the web) and for `game_console`, the console's twin that
   `snescheck` uses. The ROM is unchanged, byte for byte.

## Alternatives

- **Switch the desktop to the kit now** and delete the game's desktop frontend: one desktop, but
  the bot, the start options, mute, the text panels and stepping back in replays would be gone
  until the kit adds them, while the web still needs most of the same file.
- **Wait for the kit** without registering anything: the kit's view would stay empty for the game,
  and the kit would get no feedback from use.
- **Debug switches in every build, the ROM included**, instead of keying them on the compiler:
  the SNES would pay a few bytes and compares a tick for switches it can never set.

## Consequences

- The game runs on Linux and Windows from the kit, the condition the kit set for its M7.
- Two desktop frontends for a while; the game's debug features live in both. The registrations
  carry over when the game's frontend goes.
- A change made in the kit's view is not part of the kit's recordings (the kit's window says from
  which tick a replay plays another game); the game's frontend still records its own actions.
- Automated runs of the kit's build use `--check` (no window, no sound): the window has no mute.
