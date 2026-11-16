# ADR-0003: Multikills and BLASEN letters as a bonus

Date: 2026-11-16
Status: accepted
Brainstorm: [docs/archive/brainstorm/extend-blasen_2026-11-15.md](../archive/brainstorm/extend-blasen_2026-11-15.md)

## Context

A recording showed three enemies vanishing and the next round starting after the player popped a
letter. It looked like a bug. It was the sixth EXTEND letter: a completed EXTEND gave an extra life
and ended the round at once, and nothing on screen said so.

Test games then showed that a chain of three or more enemies is rarely something a player can set
up. Whether enemies meet depends on the room, the air that gathers bubbles is invisible and slow,
and the clock punishes waiting (a trapped enemy escapes after 5 s, Hurry Up comes after 40 s down
to 17 s). Two of three attempts failed without the player doing anything wrong. In one, three
bubbles that looked touching did not chain: they chained only below 15 px on both axes, while a
bubble is drawn 16 px wide. In the other, a loose group with gaps of a few pixels did not chain.

EXTEND is one of only two sources of extra lives; the other is score (30,000 and 100,000 points).

## Decision

Chains and letters stay a bonus that happens when the room allows it, not a core mechanic, and
the rules make that bonus honest:

1. Three or more enemies defeated in one go are a multikill, whatever did it: a bubble chain, one
   dash, one water stream, one fire or lightning bubble, a Soap Star or Rinse Wave, one touch on
   frozen enemies during the Steam Whistle. The third enemy plays `multikill.wav` in place of the
   kill sound.
2. A multikill earns letters for the player who caused it: 1 for 3 enemies, 2 for 4, 3 for 5 or
   more. Each is a letter that player still misses; if they miss fewer than they earned, letters
   repeat.
3. Whoever pops a letter gets it. A letter they already have is wasted.
4. The word is BLASEN (the game's name) instead of EXTEND. Six letters give an extra life and end
   the round, as before.
5. The round end is explained: the room stands still for 2 s with
   `CONGRATULATIONS! / BLASEN COLLECTED / BLUBBER GETS AN EXTRA LIFE` (Blasi for player 2 and the
   CPU partner) in the darkened box of the round intro. The umbrella gets the same treatment with
   `UMBRELLA! / SKIP 3 ROUNDS`.
6. The CPU partner fetches letters it still needs once no enemy is left to hunt, and it does not
   dash through a letter it already has.
7. Bubbles that look touching chain: the reach is less than 18 px on both axes and at most 25 px
   for dx + dy (a near-circle without multiplication, so diagonal neighbours get no more reach
   than side-by-side ones).
8. Floating bubbles closer than 24 px cling together, 0.25 px per frame each, until they touch.
   Debug key F6 switches this off for comparison.

Items were counted as multikills on purpose, although a Soap Star then always earns letters: the
rule is "a kill is a kill", and a player who earned the item earned the letters too.

## Consequences

- The extra life from letters still depends mostly on whether a room brings enemies together. That
  is accepted for a bonus.
- The chain reach (18 / 25 px) and the sticky bubbles are first values, to be tuned by playing
  rounds 1, 2 and 4. Any change to them moves `tests/replays/plus_bot_seed42.bbr`, which has to be
  recorded again.
- The CPU soak test (30 minutes from round 1, at most 100 deaths) failed with these rules at 102
  deaths. Running it with 20 seeds showed that was chance: the old and the new rules average 79
  deaths, about plus or minus 9 per seed. It was replaced by a test that lets the CPU partner play
  every room with endless lives and fails a room only if 15 tries with different seeds all fail.
  That test found that Arcade rounds 16 and 34 start the players in a pocket that only a bubble
  bounce leaves; the CPU's navigation data uses the lenient movement model and thinks it can jump
  through the wall. Both rooms are skipped in Arcade until the navigation is fixed.
- Sprite cells 132, 133 and 135 (X, T, D) now hold B, L and A; S is in cell 143.
- Game code names follow the word: `BLASEN_ALL`, `blasen_collect()`, `multikill_kill()`. The
  score-based extra lives keep their `extend_scores` table, which never meant the letters.
