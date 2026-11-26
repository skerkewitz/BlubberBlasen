# Blubber Blasen

A Bubble Bobble-like game in C99 for the SNES and the PC (README.md first, then ROADMAP.md and
docs/adr/). It is built with superdevkit (ADR-0007): the console API `sdevkit/snes.h`, Rachael
(the kit's SNES in C) for the PC and web builds, dampf and the kit's `lib/snes` for the ROM.

- The superdevkit home is `/home/tropper/develop_projects/superdevkit_main/build/install/superdevkit`
  (`SUPERDEVKIT_HOME`, in `superdevkit.env`): a separate clone of the kit's `main`, pinned to a
  release tag (now v0.10.0) and built with
  `./gradlew installDist`. Use only what a release holds (ADR 41 of the kit), as an end user would;
  what the game needs and a release lacks goes into `docs/kit-findings.md`. Never build or edit
  there; to move to a newer version: `git fetch`, check out the new release tag,
  `./gradlew installDist`, run the replay check. `/home/tropper/develop_projects/superdevkit` is
  the kit's development checkout (`develop`, another agent works there, may be broken): do not
  use it. Change the kit only when the user asks.
- Before writing game code for the SNES, read the kit's guides in that home: `lib/C_STYLE.md`
  (what dampf rejects, allows, and what is slow or fast; the speed rules are measurements) and
  `lib/SNES_HARDWARE.md` (what the console allows and which of it the kit has). Apply a speed
  rule only where the timing report or a profile shows the code is hot, and measure it.
- `src/game` calls only the console API and follows the coding rules in the README.
- The ROM: `./build_snes.sh` (README, *SNES build*); check it against the PC with
  `tools/snescheck` and the kit's `dev/replay.sh`.
- Findings for the kit go into `docs/kit-findings.md`; the user forwards them. The kit's findings
  for the game arrive in `docs/game-findings.md`.
