# TODO: from playable prototype to full game

Stage 1 of the roadmap (README) is a playable prototype on PC, and the game is that already:
100 rounds, the boss, both endings, the secret rooms, high scores and a web build. This list
holds what is still missing before we call stage 1 a full game. Each item says what is wrong now
and when it counts as done.

Music is left out on purpose: it waits for stage 2 (see "Sound and music").

## Game structure

Decided in conversation on 2026-11-16. The items below build on each other; the round groups
come first.

- **Six round groups.**
  Now: the 100 rounds are ten floors of ten rounds (`docs/story.md`), and the only thing a floor
  changes is its name. Nothing in play tells the player that rounds belong together.
  Done when the rounds are split into six groups of 5, 10, 15, 20, 20 and 30 rounds, the last
  round of each group being its boss (so 94 normal rounds and 6 boss rounds). Each group has one
  theme that the player can feel: a mechanic of its own (for example conveyors or vents) and a
  way its enemies behave. The look may vary inside a group; the feel may not. `docs/story.md`
  has six areas of the factory in place of the ten floors, the design guide describes each
  group's theme, and the rooms are reordered or rebuilt to match.

- **A boss for every group.**
  Now: there is one boss, Duke Grumble in round 100.
  Done when each group ends in a boss round, and each boss is beaten with the mechanic its group
  taught (as the Duke already needs lightning bubbles). Duke Grumble stays the last one.
  Each boss has a test that beats it with scripted input, like the existing boss test.

- **Restart after a beaten boss.**
  Now: every run starts at round 1 (except with the developer option `--round`).
  Done when beating a boss is saved in the save RAM and a new run can start at the round after
  the furthest boss beaten (boss in round 5 beaten, game over in round 7: the next run can start
  at round 6). The start screen offers this choice. A checkpoint run starts with fresh lives and
  the full continues of the difficulty level, and no power-ups. Its score goes into the normal
  high score table with no special rule: the highest scores still need all 100 rounds in one run.

- **Limited continues.**
  Now: when the last player is out, a countdown offers a continue, as often as you like.
  Done when the number of continues per run depends on the difficulty: Easy 5, Normal 3, Hard 0.
  A continue puts the player back into the current round. The continues are shared between the
  two players. With none left, losing the last life ends the run. The number left is shown on the
  continue screen. For testing, the developer switch for endless lives stays as it is.
  To revisit later: whether shared continues are right, and whether the CPU partner should
  re-enter differently from a human player.

- **Cut scenes between groups.**
  Now: a new floor only shows its name when its first round starts.
  Done when a short, skippable cut scene (about 15–20 s) plays after each boss and explains what
  happens next and what the next group is about. They use the same scene code as the new intro
  (see "Story and presentation"), and `docs/story.md` has their script.

- **Secret rooms and the warp in the new structure.**
  Now: secret doors open in rounds 20, 40 and 60 without a lost life, and round 50 warps to 70.
  All of these numbers come from the ten floors.
  Done when the doors and the warp have places that fit the six groups, the three clues still
  tell the story of the two levers, and the design guide and README describe the new places.

- **Backgrounds per group.**
  Now: there is no background; rooms are drawn on black. The SNES has a third layer we do not
  use.
  Done when each group has a background on its own layer that sets the mood and stays out of the
  way: darker and less saturated than anything the player can touch, no detail at the size of a
  bubble or an enemy, little or no movement. Each background has been checked against the
  busiest room of its group (many bubbles, water, Hurry Up) and taken back where it made the room
  harder to read.

## Graphics

- **Indexed graphics and an art checker.**
  Now: graphics are 24-bit PNGs and a "palette" is a tint, so nothing stops art from breaking the
  SNES limits (ADR-0004).
  Done when the asset pipeline loads palettes as data and tiles that refer to them, and a checker
  in the build fails when a background tile uses more than one palette or more than 15 colours, a
  room needs more than 6 background palettes, a colour is off the 5-bit grid, or sprites exceed
  their budget. It reports the unique tiles per room. Needed before final art is drawn.

- **Art style sheet.**
  Now: the target look, the SNES limits, the palette budget and the readability rules are fixed
  (ADR-0004, design guide "Readability and art limits"). The drawing style is not: outlines,
  shading and proportions of the characters at 16 × 16 px.
  Done before any final art is drawn: a short document fixes the outline and shading style of the
  characters and one palette set per round group for rooms and background. It is tried out by drawing one group completely (a room, the
  background, one enemy and the boss) before the other groups are started.

- **Player and enemy sprites.**
  Now: Mint, Neela, all eight enemies, Lord Smog and the boss are placeholder art drawn
  by a script (`assets/README.md`, "Where the files come from").
  Done when every character has its own drawn walk, jump, shoot, trapped and defeat frames in
  `sprites.png` within the style sheet, the provenance table lists them as our own work, and
  nothing in the sheet is marked as a placeholder any more.

- **Items, food, bubbles and effects.**
  Now: food, gems, power-ups, letters, water, fire and lightning are script placeholders as well.
  Only the bubble cells 32–34 come from the old Java version. The B, L, A and S letters were
  drawn quickly to match the placeholder E.
  Done when every item and effect has final art within the style sheet, a player can tell each
  one apart at 1× scale on a TV-sized screen, and the provenance table is updated.

- **Font.**
  Now: `font.png` is a script placeholder.
  Done when there is a drawn 8 × 8 font that covers every character the game prints (letters,
  digits and punctuation) and fits the look of the logo.

- **Title screen.**
  Now: the Blubber Blasen logo from the Java version, scaled to half size, above plain text
  ("SELECT: OPTIONS" and so on) on an empty background. It does not show the heroes, the town
  or the factory, and it does not move.
  Done when the title screen has a composed picture (logo, Mint and Neela, a hint of the
  factory), some movement (rising bubbles, blinking "PRESS START"), and a layout we both like.
  It has to fit the SNES limits: 256 × 224, 8 × 8 tiles, the BG layers and sprites.

- **Room tilesets for all six groups.**
  Now: there are 8 tilesets (`tileset01..08.png`, from the Java version), chosen for the old ten
  floors.
  Done when each round group has the tilesets it needs, drawn within the style sheet, matching
  its theme, with a room of its own for each boss.

## Sound and music

- **Music (stage 2).**
  Now: two temporary royalty-free tracks, one for the title and one for the rounds. There is no
  music for the bosses, the ending, game over or the secret rooms, and Hurry Up does not change
  the music.
  Not part of stage 1. When it is done in stage 2: our own (or properly licensed) music for
  title, rounds, bosses, ending and high scores, a faster or tenser version for Hurry Up, written
  so that it can be played by the SNES sound chip (8 channels of short samples in 64 KB), and the
  music table in `assets/README.md` lists only final tracks with their licence.

- **Sound effects.**
  Now: all sounds except `UhHelpMe`, `vacuum` and `multikill` are placeholders synthesised by a
  script.
  Done when every effect in the list in `assets/README.md` is replaced by a final sound, the
  sounds are balanced in volume against each other, and the provenance table says where each
  comes from.

## Story and presentation

- **A better intro.**
  Now: the intro is three pages of text (`docs/story.md`, "Intro text") with a few sprites, up to
  6 s each. It tells the story but shows none of it.
  Done when the intro shows Blasenhausen as a bright town that runs on bubbles, then
  Duke Grumble taking over the factory and the town going grey, then Mint and Neela escaping. It
  needs the town and factory art for that, stays skippable with Start, and `docs/story.md`
  describes the new scenes. The scene code is shared with the cut scenes between groups.

- **Use the final character names everywhere.**
  Decided 2026-11-16 (see `docs/story.md`): heroes **Mint** (player 1, green) and **Neela** (player 2,
  blue, Mint's girlfriend); enemies Grumber, Riveter, Clinker, Pinger, Soot Moth, Boing, Cinder,
  Hoist; Hurry Up chaser **Lord Smog**; final boss **Duke Grumble**. Player 2 stays blue; angry enemies
  stay red. The Cinder is now red-purple, the Pinger yellow and the Boing orange (done in
  `sprites.png`), so no enemy is red when calm and none is green or blue like the heroes.
  Now: only `docs/story.md` uses them. The code, the BLASEN message ("BLUBBER GETS AN EXTRA LIFE"),
  README, design guide and ADRs still use Blubber, Blasi, the old enemy names and the Grumble Boiler.
  Done when the new names are used everywhere, including the game code's text strings.

- **How-to-play screen.**
  Now: nothing in the game explains the mechanics. New players have to find out by themselves
  that you trap an enemy and then touch the bubble, that you can bounce on bubbles, what a chain
  does and what the Plus moves (push bubble, vacuum, dash) are for.
  Done when there is a screen, reachable from the title, that shows each mechanic as a short
  scene played by the game itself (scripted input in a small room) with one line of text: trap
  and pop, bubble bounce, chain and multikill, BLASEN letters, and in Plus mode dash, push bubble
  and vacuum. It uses the same game code as real play, so it cannot drift from the rules.

- **Attract mode.**
  Now: the title screen waits forever. There is no gameplay demo.
  Done when, after some time on the title screen without input, the game plays a short demo
  (a few rounds, recorded replays or the CPU partner on both sides), then shows the high scores,
  then returns to the title, in a loop. Any button returns to the title. The demo plays without
  sound, cannot change the high scores and is deterministic, so a test can check it runs.

## Rounds and balance

- **Hand-designed rounds.**
  Now: rounds 31 to 99 were generated from room patterns by the asset script. They pass
  `mapview --check`, but nobody has designed them by hand.
  Done when every round has been looked at and reworked in the editor where needed, fits the
  theme of its group, and new enemy types and group mechanics get a room that introduces them.

- **Full playthrough and difficulty curve.**
  Now: nobody has played all 100 rounds in one go. The room test only proves that every room
  can be cleared, not that the difficulty rises evenly.
  Done when at least one full game in Plus and one in Classic has been played by a human on
  Normal, rooms that are far too hard or too easy for their place have been fixed, and the
  findings are noted in the design guide.

- **Difficulty settings.**
  Now: there is one difficulty, and continues are unlimited. The design guide lists difficulty
  settings under "Not built".
  Done when the options offer three levels (working names Easy, Normal, Hard), saved with the
  other options. The continues per run are already decided: Easy 5, Normal 3, Hard 0 (see
  "Limited continues"). Which other values each level changes is decided and written in the
  design guide; likely candidates are lives at the start, how long a trapped enemy stays in its
  bubble, enemy speed and the time until Hurry Up. Apart from the continues, Normal plays as the
  game does today, so the existing tests and replays stay valid. A test plays a room on each
  level and checks that the chosen values take effect.

- **Open questions in the design guide.**
  Now: the "Open questions" section of the design guide has seven items: vacuum strength,
  enemies in rounds 34/39/69/99 that only a bubble bounce reaches, the dash jump, walls and
  bubbles inside the vacuum, the CPU partner and the Plus tools, push bubble hit zones, and the
  CPU navigation.
  Done when each one is decided (built, dropped or accepted as it is), with an ADR where the
  decision changes the rules, and the section is empty or removed.

- **Still open from the BLASEN change.**
  Now: ADR-0003 calls the chain reach (18 / 25 px) and the sticky bubbles first values. It is
  also unknown whether sticky bubbles get in the way of bouncing or the push bubble, and how many
  rooms allow a multikill at all.
  Done when the values have been tuned by playing rounds 1, 2 and 4, the replay
  `tests/replays/plus_bot_seed42.bbr` is recorded again afterwards, and the results are in the
  ADR or a new one.

## CPU partner

- **Navigation that knows walls and dashes.**
  Now: the CPU's navigation data uses the lenient movement model from `tools/common/reach.c`, in
  which a sideways jump may pass through a wall. In Arcade the CPU cannot leave the start pocket
  of rounds 16 and 34, so the room test (`test_cpu_rooms`) skips them. It does not plan dashes
  into pockets and does not know that a Riveter's front blocks bubbles.
  Done when map2c builds the navigation with the strict wall model (`REACH_WALLS`) and dash edges
  for Plus (`REACH_DASH`), the CPU uses them, and the room test passes for every round in both
  modes with the exceptions in `cpu_cannot_leave_start()` removed.

## Options and controls

- **More options.**
  Now: the options screen has MODE, CO-OP RESCUE and PLAYER 2. There is no difficulty, no volume
  setting and no way to change the controls.
  Done when there are separate music and sound volumes, saved in the save RAM next to the other
  options, and the README describes them.

- **Key and button remapping.**
  Now: the keys are fixed in `src/platform/platform_raylib.c`. On a German keyboard player 2's
  dash right is the `-` key, because the code asks for `/`.
  Done when both players can change their keys and gamepad buttons in the options, the choice
  is saved, and the README controls table describes the defaults and how to change them. This
  lives in the platform layer; the game code still only sees the SNES button word.

## Documentation and housekeeping

- **ADR for the game structure.**
  Now: the decisions under "Game structure" exist only in this file.
  Done when an ADR in `docs/adr/` records the six groups, the bosses, the restart after a boss
  and the continues with their reasons, before the first of them is built.

- **ADR-0001 and ADR-0002.**
  Now: the README lists both as "not written yet" (C99 with raylib behind a platform layer; the
  old-school game loop and data layout).
  Done when both exist in `docs/adr/` and the README links them.

- **README is out of date in places.**
  Now: "How to play" says there are seven enemy types; the design guide says eight (the Riveter
  is missing from the README's list).
  Done when the README and the design guide agree, and a search for enemy counts and names finds
  no other mismatch.

- **Licence.**
  Now: the README says "To be decided".
  Done when a licence is chosen for the code, the assets have their own licence note where it
  differs, and both are in the repository.
