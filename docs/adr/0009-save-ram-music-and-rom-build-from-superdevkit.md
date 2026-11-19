# ADR-0009: Save RAM, music and the ROM build from superdevkit

Date: 2026-11-19
Status: accepted

## Context

Until now the game kept its save RAM itself: a 128-byte array in `hiscore.c`, which the frontend
read with `game_sram()` and wrote to `blubberblasen.sav` when `game_sram_dirty()` said so. On the
SNES that array was ordinary work RAM, so the ROM forgot its high scores at power off. The music
was the frontend's too: the game said which track should play (`game_music()`, an enum in
`game.h`), and the frontend streamed the mp3 files it listed itself. The ROM was built by
`build_snes.sh` calling dampf with `blubberblasen.args`.

superdevkit 0.7 (its M7) adds what the game had built for itself:

- `sdevkit_sram()`: the cartridge's battery-backed RAM, `SDEVKIT_SRAM_SIZE` bytes, the size
  `sram` in `superdevkit.toml` gives (dampf `--sram` on the SNES, a define on the desktop).
- `sdevkit_music_play(track)` and `sdevkit_music_stop()`: the tracks are listed in
  `superdevkit.toml`, the build numbers them in `music.h` (`MUSIC_TITLE`, ..., `MUSIC_COUNT`).
  On Rachael the request is kept in `sdevkit__music`, and a frontend plays the file. On the SNES
  the calls do nothing yet.
- `superdevkit.toml` and `superdevkit build snes|linux|win64`: the project file says what a build
  compiles and converts.

The kit's own desktop frontend has no debug view, recordings or web page yet, so the game's
raylib frontend stays (ADR-0007, decision 3).

## Decision

1. **The game keeps its high scores and options in `sdevkit_sram()`** (`hiscore.c`, layout
   unchanged). `game_sram()` and `game_sram_dirty()` are gone. The frontend loads
   `blubberblasen.sav` into `sdevkit_sram()` and writes it whenever the bytes differ from what it
   last loaded or saved, as the kit's frontend does. Recordings start from `sdevkit_sram()` too.
2. **The game asks for music through the kit.** `music_update()` calls `sdevkit_music_play` or
   `sdevkit_music_stop` at the end of every tick (title track around the title, game track in
   the rounds, nothing at game over, as before). The frontend plays `sdevkit__music.track` from
   `sdevkit__music_files`. `game_music()` and the `MUSIC_*` enum in `game.h` are gone; the numbers
   come from the generated `music.h`.
3. **`superdevkit.toml` lists the project** for the kit: name, kit version, `sram = 128`, the game's
   sources (`src/game` and `build/data`), the sound effects and the two music tracks. The tracks
   are listed only there: CMake reads them from it and writes `music.h` and `music_files.c` the
   same way `superdevkit build` does.
4. **The ROM is built by `superdevkit build snes`.** `build_snes.sh` still runs the game's own
   converters (`map2c`, `gfx2c`) into `build/data` first, since the kit cannot run them, then calls
   the kit. `blubberblasen.args` is gone; the kit writes `build/snes/blubberblasen.args` for
   calling dampf by hand.
5. **The PC and web builds stay with CMake and the game's frontend** until the kit's frontends
   have a debug view, recordings and a web build (the kit's M8). CMake defines
   `SDEVKIT_SRAM_SIZE=128` for Rachael and the game.

## Alternatives

- **Keep the game's own save RAM and music** until the desktop moves to the kit as well: the ROM
  would keep losing its high scores, and the move would have to happen anyway.
- **List the tracks in CMake as well as in `superdevkit.toml`**: simpler CMake, but two lists to
  keep in step. Reading the project file with a regular expression is fragile only if the
  entries change form; CMake stops when it finds no track.
- **Convert the levels and graphics with the kit's `png` converter**: the game's `gfx2c` checks
  the game's own limits and writes the formats the game reads, so it stays.

## Consequences

- The ROM has 128 bytes of battery-backed save RAM: high scores and options survive power off.
  On a new cartridge the bytes are random; the magic words in `hiscore.c` catch that.
- The game's behaviour is unchanged: per-tick checksums and pictures are the same as before in
  four `snescheck` scenarios over 12,000 ticks, `test_game` prints the same, the stored
  recordings still play (their checksum takes the track plus one, so "none" is still 0), and the
  ROM matches the PC over 36,000 ticks in all four scenarios.
- The ROM build needs superdevkit 0.7 or newer.
- Music on the SNES comes without a change to the game once the kit plays it there.
