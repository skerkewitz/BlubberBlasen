# Brainstorm: EXTEND, multikills and sticky bubbles (2026-11-15)

## Problem

It started with a recording (`20261115-215634-marked.bbr`, tick 44794): the player popped a letter,
three enemies vanished and the next round began. It looked like a bug. It was the sixth EXTEND
letter, and a completed EXTEND gives an extra life and ends the round at once
(`extend_collect()` in `src/game/effect.c`). Nothing on screen says so; there is only a sound and
the lives counter going up.

Looking closer, the game rewards popping several enemies at once (bigger food, EXTEND letters) but
never shows the moment it happens or what it earned. The letters arrive seconds later, one at a
time, through the room's opening, so players cannot tell why they sometimes appear. The word EXTEND
is arcade slang for an extra life and has nothing to do with a soap factory.

Underneath sat a bigger question: can a player choose to play for chains of three or more at all?
Test games (`20261115-225358-marked.bbr`) answered it:

- Round 1 spreads the enemies over the room; round 2 brings them to the player. Whether a chain is
  possible depends mostly on the room.
- Tick 2344: three trapped enemies lined up next to a column of empty bubbles, apparently touching,
  but they did not chain. Two bubbles chain only when their centres are less than 15 px apart on
  both axes (`CHAIN_REACH` in `src/game/bubble.c`), while a bubble is drawn 16 px wide. Bubbles that
  look touching side by side do not chain; diagonal ones with a visible gap do.
- Tick 4811: a loose group of five bubbles in a corner, gaps of a few pixels. It looks like a group
  and does not chain.
- Out of three attempts in four rounds, one worked. Neither failure was the player's fault.

Time is against waiting: a trapped enemy escapes after 5 s, Hurry Up comes after 40 s (round 1) down
to 17 s (round 99), and the Soot Cloud 10 s later. Walkers don't follow the player sideways and
flyers ignore them, so enemies only meet where the room brings them together.

Constraints: the fix should not make the game noticeably harder; EXTEND is one of only two sources of
extra lives (the other is score: 30,000 and 100,000 points).

## Who is affected

- A beginner likes the round skip as a way out of a hard room, and needs the extra lives.
- Someone who wants to see all 100 rooms loses rooms to EXTEND and the umbrella.
- In co-op, letters are per player but the room is shared. One player's EXTEND ends the round under
  the other one's feet, possibly mid-combo. The CPU partner pops letters too.
- A score chaser loses the food still lying in a room that EXTEND ends early.
- A parent playing with a child doesn't care about chains and must never be punished for popping
  early.

## Before the ideas: what the chain reward is for

The session listed four jobs a chain reward does in this kind of game: it turns "trap and pop at
once" into a decision (pop now or wait for more), it makes the rooms' air matter, it gives skilled
players something extra without making the game harder for others, and a cascade feels good.

The tests showed the first three don't hold in this game. The air is invisible and slow, nobody
learns 100 rooms of it, and the clock punishes waiting. In Plus the tools can gather a nearby group
(vacuum: 40 px for 1.5 s; dash: everything in a 54 px line counts as one chain; push bubble: one
bubble at a time), but nothing gathers enemies spread over the room. The conclusion: **chains are a
bonus that happens when the room allows it, not a core mechanic.** The design guide should say so.

## Ideas

Ideas about chains:

1. **Multikill sound.** A sound the moment the third enemy of a chain pops, so the player hears that
   it counted. It is built (`multikill.wav`, `SFX_MULTIKILL`).
2. **Coyote margin for the chain distance.** Like coyote time in jumping: the rule is a little more
   generous than the picture, so bubbles that look touching always chain. Raise the reach from 15 to
   about 18 px and use a near-circle check (`dx < R && dy < R && dx + dy < R * 1.4`, no
   multiplication, fine for the SNES) so diagonals are not more generous than side by side.
3. **Sticky bubbles.** Soap bubbles cling: floating bubbles closer than 24 px pull towards each other
   until they touch. A loose group closes into a cluster by itself, so the picture matches the rule.
   It is the counterpart of `separate()`, which already pushes overlapping bubbles apart.
4. **Show the connection.** A flash between bubbles that will chain.

Ideas about EXTEND:

1. **Drop EXTEND.** No letters at all; lives from score only, maybe with a third threshold. The
   simplest game.
2. **Keep EXTEND without the round skip.** Six letters give a life and a message; the round goes on.
3. **A word that fits the world.** Same mechanic, but the letters spell B-L-A-S-E-N, the game's own
   name, also six letters.
4. **Letters for the real skill.** One letter for clearing a room before Hurry Up, or without losing
   a life. Rewards something the player controls in every room.
5. **Letters hidden in the rooms.** One per floor, in a spot only the Plus tools reach. Rewards
   exploring instead of luck.
6. **A multikill buys time.** No letters; a chain pushes Hurry Up back a few seconds. It pays in the
   currency that made chains a gamble.
7. **Wild: the letters are the off-switch code** from the story's secret rooms, and collecting them
   changes the ending.
8. **Wild: shared letters in co-op.** Both players fill one word, and both get a life.

## Ratings

Only the chosen combination was rated in detail; the player decided between the ideas from the test
games.

| Idea | Realistic? | Cost | Fragility | Solves it? | Works for others? | New problems | Reversible? | Fits plans? |
|---|---|---|---|---|---|---|---|---|
| Multikill sound | Yes, built | An hour | None | The missing moment of feedback, for chains | Everyone hears it | None seen | Easy | Yes |
| Coyote margin | Yes | An hour or two, plus re-recording the test replay | Low; the value needs tuning by play | The "looks touching but doesn't chain" case | Everyone | Too large a value makes visibly separate bubbles chain | Easy | Yes |
| Sticky bubbles | Yes, built as a test | Hours | Medium: changes how every bubble moves; push and vacuum interact with it | The loose-group case, if the group forms in time | Everyone; untested for players who jump on bubbles | Clusters of empty bubbles; the push bubble may shove a bubble out and see it pulled back | Easy (F6 switches it off) | Yes |
| BLASEN (EXTEND idea 3), with message | Yes | Hours, plus four new letter sprites (B, L, A, S) | Low | The bug-like round end and the word; not that letters depend on luck | Co-op players see who got the life and why the round ended | None new | Easy | Yes |

## Recommendation

Treat chains as a bonus and make the bonus honest: the player hears a multikill, bubbles that look
touching chain, and loose groups close up. Keep EXTEND's mechanic but give it the game's own word
and a message that explains the round end. The trade-off: letters still come mostly from rooms that
allow chains, so the extra life stays partly a matter of luck. That is acceptable for a bonus.

## Decision

1. **Multikill sound** (done). `assets/sfx/multikill.wav` plays once per multikill, when the third
   enemy is defeated, instead of the normal kill sound.
2. **A kill is a kill.** Three or more enemies defeated in one go count as a multikill, whatever did
   it: a bubble chain, a dash, a water stream, one fire bubble or one lightning bolt. Every multikill
   plays the sound and earns letters.
3. **Letters per multikill:** 3 enemies give 1 letter, 4 give 2, 5 or more give 3.
4. **The letters come for the player who caused the multikill**, always letters that player is still
   missing. If they need fewer letters than they earned, letters repeat (a 5-multikill when only B
   is missing brings three Bs).
5. **Whoever pops a letter gets it.** If that player already has it, the letter is wasted.
6. **The CPU partner only goes for letters it still needs**, so it doesn't waste them. Its dash pops
   everything on its way, so it should check the dash path for letters it doesn't need; a letter
   caught anyway is wasted, as for a human.
7. **EXTEND becomes BLASEN.** Same mechanic: six letters give an extra life and end the round. The
   side walls spell BLASEN. Claude draws B, L, A and S in the style of the existing letter bubbles.
8. **A message when BLASEN is complete**, in the darkened box of the round intro, for 2 s before the
   next room scrolls in, with the character's name from the story (Blubber is player 1, Blasi is
   player 2 and the CPU partner):

   ```
   CONGRATULATIONS!
   BLASEN COLLECTED
   BLUBBER GETS AN EXTRA LIFE
   ```

9. **The umbrella gets a message too**, in the same box: `UMBRELLA!` / `SKIP 3 ROUNDS`.
10. **Sticky bubbles stay** for now, as built in the test (`STICK_REACH` 24 px, `STICK_PULL`
    0.25 px per frame; debug key F6 switches them off).
11. **Coyote margin** for the chain distance, value and near-circle shape to be settled by play.
12. **The design guide** describes chains and BLASEN as a bonus, not a core mechanic.

## Open questions

- The exact chain reach (about 18 px?) and whether the near-circle check feels right. Settle it by
  playing rounds 1, 2 and 4.
- Whether sticky bubbles get in the way of jumping on bubbles, or of the push bubble. Watch for it in
  play; F6 compares.
- How many rooms allow a multikill at all. `mapview` could estimate it from where enemies start, if
  that number is ever wanted.
- Lightning and fire bubbles look useless in practice: aiming them takes longer than trapping the
  enemy, and few players will ever unlock the Fire Glove (4 fire kills). Parked for its own session.
  In Plus, a push bubble sets a special bubble off from a distance, which may help.
- `tests/replays/plus_bot_seed42.bbr` drifts since sticky bubbles and must be recorded again once
  the rules have settled.
