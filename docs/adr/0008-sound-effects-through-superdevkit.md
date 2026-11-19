# ADR-0008: Sound effects through superdevkit, the same on every platform

Date: 2026-11-19
Status: accepted; the music moved to the kit as well (ADR-0009)

## Context

Until now the game only asked for sounds: each tick collected them in a 32-bit mask
(`game_sfx()`), and the PC and web frontend played WAV files for the bits. The ROM had no sound,
because superdevkit had none (ADR-0007).

superdevkit 0.5.1 brings sound effects (its ADR record 44, `docs/sound.md`). A game lists its
effects in `assets/sfx/sfx.json`; `superdevkit sfx` converts the active samples into the SNES's
format (BRR) and writes `sfx.h` (`SFX_<ID>`, `SFX_COUNT`) and `sfx.c` (`sfx_bank`). The game calls
`sdevkit_sfx_init(&sfx_bank)` once and `sdevkit_sfx_play(id)` per effect. On the SNES the kit
uploads the samples to the sound CPU and drives the S-DSP; on Rachael a model of the S-DSP plays
the same samples, and the voice choice (`lib/sound/sfx_voices.h`) is the same C on both. The
converter is Java. The kit's finding for the game proposed the move.

## Decision

1. **The game plays its effects through superdevkit** on the SNES, the PC and the web. The
   effects and their numbers come from `assets/sfx/sfx.json` (edited with the kit's BRR Lab); the
   game's own `SFX_*` list in `game.h` is gone, the names come from the generated `sfx.h`
   (`SFX_HELP` became `SFX_UHHELPME`).
2. **A tick still collects its effects in the mask** (`g_sfx`) and plays each once at its end
   (`sfx_flush` in `game_tick`). The mask is part of the game's rules: an effect asked for twice
   takes one voice, and a multikill takes back the kill sound of the same tick. `game_sfx()` is
   for the tests only (development builds).
3. **The PC and web frontend plays Rachael's sound chip**: after each `game_vblank` it calls
   `sdevkit__sfx_frame()`, as the kit's main loop on the SNES does, and puts the tick's samples
   (32 kHz, stereo, 32000 / 60 per tick) into a raylib audio stream. It no longer loads the WAV
   files. The music stays the frontend's (mp3, PC and web only), since the kit has none yet.
4. **Every build converts the effects with Java** (`java -cp <home>/java/* superdevkit.Main sfx`,
   not the launcher, so that a home inside WSL works from Windows): CMake needs a JDK 25 or newer
   and stops with a message without one; `build_snes.sh` uses the home's `bin/superdevkit`.
5. **The ROM is checked against the PC for sound too**: `snescheck --sound` writes Rachael's DSP
   writes in the format dampf's emulator records for the ROM.

## Alternatives

- **Keep the PC's WAV playback** and give only the ROM sound: the platforms would sound different,
  and the PC would no longer tell how the console sounds.
- **Generated `sfx.c` in the repository**, so that no build needs Java: one more file to keep in
  step with `sfx.json`. The Windows build could also have called the converter through WSL. The
  user chose to install a JDK on Windows, which the kit's coming `superdevkit build` needs anyway.

## Consequences

- The game sounds the same on all platforms, as dull as the console's Gaussian interpolation
  makes it; the effects are tuned in the BRR Lab, not by replacing WAV files.
- The ROM uploads 43,686 bytes of samples in `game_init`: about two seconds of black screen after
  the kit's splash.
- The game's behaviour is unchanged: per-tick checksums and pictures equal the previous version's
  in four `snescheck` scenarios, and the ROM's DSP writes equal Rachael's line by line.
- Every build machine needs a JDK 25 or newer.
