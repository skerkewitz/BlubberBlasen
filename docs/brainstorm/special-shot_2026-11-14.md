# Brainstorm: the Plus-mode special shot (2026-11-14)

## Problem

Plus mode had a special shot: with a full energy bar, holding Y for half a second fired a large, slow
bubble that kept flying after its first catch and could hold two enemies. Nobody used it, the
developer included.

- It has no job. Two normal shots trap two enemies faster. Its only unique property is range
  (about 90 px against 32 px), and the range candy already gives more range.
- It is hard to reach. A full bar (96) takes about six kills, the dash spends the same energy
  (16 per dash), and the bar drains about 10 s after the last kill.
- Nobody watches the bar. The game is too fast to look at the bottom wall. The dash solved this with
  a yellow outline on the player; the special shot has nothing like it.
- It is buggy on the way. Next to a wall the charged shot came out as a plain bubble and still
  emptied the bar (the new bubble was looked up by its x position after that position had been
  clamped). With a full bar, normal shots fired on release instead of on press, so the reward made
  the basic action feel worse.

Constraints: the dash stays for now (still undecided); nothing may make normal shooting worse;
feedback has to show on the player, not only in the bar; the code has to stay cheap enough for the
planned SNES port (no multiplication-heavy physics).

## Who is affected

- Players chasing big chains: bubbles often drift to places nobody can reach, and there is no way
  to bring them together.
- Couch co-op players: they never look at the energy bar either.
- Touch players on the web build: holding a virtual button to charge is clumsy, and it delayed
  normal shots.
- The CPU partner: it never used the special shot and only spends energy on dashes.

## Background: how bubbles move

- A fired bubble has a flight phase (`BS_SHOOT`): fixed speed for a fixed number of frames, then it
  switches to floating. A bubble in flight traps any enemy it touches.
- Floating bubbles (`BS_FLOAT` empty, `BS_TRAPPED` with an enemy inside) have no speed of their own.
  Each frame they move one step with the air current under them.
- `separate()` keeps floating bubbles from overlapping: every pair closer than 13 px is pushed
  apart along the axis where they are further apart, harder the deeper the overlap. It remembers
  nothing between frames. Clusters form because the air drives bubbles to the same spot and this
  rule keeps them touching. There is no cluster concept in the data.
- Buttons A and X are mapped on keyboard and gamepad but were unused by the game.

## Ideas

1. **Push bubble (cue ball).** A second button fires a small bubble that does not trap. When it hits
   a bubble it shoves that bubble and bursts. It lets players move bubbles they can't reach, set up
   chains, and knock special bubbles into place.
2. **Push bubble as a co-op tool.** The same bubble also frees or nudges a partner's rescue bubble
   from a distance. It gives the couch partner a reason to use it.
3. **Gust.** A short puff of air in front of the player pushes every bubble in a small area. No
   projectile, easy on touch screens.
4. **Remote pop.** A button pops your own bubbles wherever they are. The cheapest answer to
   "the bubble is out of reach".
5. **Drop it, nothing replaces it.** Energy is only for the dash. Less code and less input.
6. **A full bar as a passive bonus.** While the bar is full, normal shots get stronger by
   themselves. Works for players who never learn an extra button.
7. **Vacuum and blow (wild).** Hold a button to pull nearby bubbles in, release to blow them out
   together like a chain bomb.
8. **Push as an energy spender.** Ideas 1 or 3, paid with energy, so the bar has a second job.

## Ratings

The session skipped the full rating table. The points that decided it:

- Momentum on the SNES is cheap: two 16-bit speeds per bubble (64 bytes for 16 bubbles) and a
  shift to slow them down, well under 1,000 cycles per frame. Real billiard physics (angled hits)
  would need multiplications and is not wanted.
- The existing `separate()` rule already spreads a shove through a cluster, so no cascade code is
  needed.
- Momentum must be put on a floating bubble, not by sending it back into the flight phase: a bubble
  in flight traps enemies, and a bubble that already holds one would lose it.

## Recommendation

Remove the special shot and try idea 1 as a small first version. The vacuum (idea 7) was the
developer's favourite among the rest and is parked for later, together with the others.

## Decision

Build the push bubble MVP and remove the special shot:

- Button A fires it, in the facing direction. Plus mode only. One push bubble per player at a time.
  Free (no energy).
- Smaller and faster than a normal shot: about 6 px per frame for 12 frames. If it hits nothing, or
  a wall, it vanishes with a small burst.
- It bursts with no effect on enemies, on players (the partner too, also in a rescue bubble) and on
  the boss. It destroys enemy projectiles.
- On a floating bubble, empty or with an enemy, it bursts and gives that bubble momentum. The
  direction depends on where it hits: upper third pushes down, middle third pushes along the push
  bubble's flight direction, lower third pushes up. The bubble ignores the air while it slides,
  slows down over about half a second, and stops at the walls.
- On a special bubble (water, fire, lightning, EXTEND letter) it acts like the player touching it.
- It does not collect food, gems or other items.
- The special shot goes completely: charging, the big bubble, and the release-to-fire behaviour at
  a full bar.

## Result after playing the MVP (2026-11-14)

The developer played the MVP and found it much better than expected. One change came out of the
first round of play:

- The push bubble moved from SNES A (the right face button) to SNES X, the top face button (Y on an
  Xbox pad, △ on PlayStation). Modern pads use the right button for dodge or secondary actions and
  the top one for special actions.
- A shoved empty bubble now traps the first enemy it slides into, like a fired bubble, and then
  stops. Before, only a bubble in its flight phase could trap, so a shoved bubble slid straight
  through enemies. A bubble that already holds an enemy still slides past others.

Three uses nobody planned emerged in play. They are the reason the feature is worth keeping, so
any later tuning has to keep them working:

1. **Combo shot.** Fire a normal bubble, then the push bubble shortly after. The faster push bubble
   reaches the normal one just as its flight ends and shoves it on, so the bubble travels much
   further. It takes timing: pressing the push button only 1–2 frames after Y fails, because the push bubble
   overtakes the bubble while it is still flying and can't be pushed. From 3 frames on it works.
   The window depends on `SHOOT_SPEED`/`SHOOT_FRAMES` against `PUSH_SPEED`/`PUSH_FRAMES` in
   `src/game/bubble.c`, and `test_push_bubble` pins it down.
2. **Shot defence with a cost.** Destroying enemy shots gives a second answer besides jumping, but
   it is no get-out-of-jail card: with only one push bubble at a time, a near enemy still gets you
   while you deal with a far one's shot.
3. **Bubbles through walls.** A shoved bubble passes through platforms (floating bubbles always
   could), so a bubble can be sent past a wall without jumping into it.

## Open questions

- Most shots will hit bubbles at the same height, so most pushes will be sideways. Is that fine, or
  should the hit zones be different?
- What should a full energy bar mean now that the special shot is gone (the colours and the chime)?
- Is the push bubble Plus-only for good, or for everyone?
- The parked ideas: vacuum and blow, freeing a partner from range, energy cost.
