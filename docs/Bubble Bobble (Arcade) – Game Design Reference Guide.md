# Bubble Bobble (Arcade) – Game Design Reference Guide

This is a description of Taito's arcade game *Bubble Bobble* (1986): its rules, enemies, items and round structure, collected from the sources listed at the end. It is reference material. The game we are building is described in [Blubber Blasen – Game Design Guide](Blubber%20Blasen%20–%20Game%20Design%20Guide.md), which also lists where it departs from the original.

Values marked "not measured" come from descriptions, not from frame counts. Where sources disagree, this guide says so.

## Overview

*Bubble Bobble* is a single-screen, two-player co-op platformer. Two dragons trap every enemy on the screen in bubbles, pop the bubbles to defeat them, collect the food the enemies turn into, and move to the next round.

What defines the original:

- Every round is one fixed screen. The challenge is routing around platforms and air currents, not exploration.
- The same bubble traps enemies, can be stood on and ridden upwards, and in special forms carries water, fire or lightning.
- Popping several trapped enemies at once multiplies the score. Waiting too long lets them escape, angrier than before.
- Two players share the screen at the same time, and the true ending is only reachable with two players.
- Many items are triggered by invisible counters (bubbles blown, jumps, distance walked).

The game has 100 rounds, 8 regular enemy types, an invincible time-out chaser, a final boss in round 100, 3 special bubble types, EXTEND letters, about 70 food items and more than 20 special items.

## Technical frame

| Element | Original arcade |
| --- | --- |
| Resolution | 256 × 224, horizontal |
| Frame rate | About 60 Hz |
| Level grid | Small square wall blocks on a fixed grid, solid side walls |
| HUD | Scores and high score on the top line, lives in the bottom corners |
| Characters | 16 × 16 px sprites |
| Rounds | 100 plus the boss, each one screen, no scrolling during play |

A round consists of:

- Solid blocks and one-way platforms. Players and enemies can jump up through platforms from below and land on top.
- Openings in the top and bottom border. Anything falling through a bottom opening reappears at the top. Rounds with openings are the "open" rounds where special bubbles enter.
- An air current map that steers floating bubbles. Currents usually converge on one or more gathering points.
- Up to 7 enemies, each with a type, a start position and direction, and its own entry delay.
- The special bubble type the round emits, if any.
- A time limit until "Hurry Up!".
- Its own palette and wall-block graphic.

## Players

Player 1 starts in the bottom-left corner, player 2 in the bottom-right corner. A single touch from an enemy, an enemy projectile or the Hurry-Up chaser kills a player.

**Controls** (2-way joystick and 2 buttons)

| Input | Action |
| --- | --- |
| Left / right | Walk. The dragon turns to face that way, which sets the bubble direction. |
| Jump | Fixed-height jump. Left/right steers in the air. |
| Bubble | Blow one bubble horizontally in the facing direction. Fire rate is limited. |
| Start | Player 2 joins at any time (coin and start). |

**Movement**

- There is no climbing and no double jump. The only ways up are jumping through one-way platforms, bouncing on bubbles, and falling through a bottom opening to wrap to the top.
- Falling is slower than in most platformers, and the player keeps air control the whole time.
- Solid side walls are never passable. Top and bottom borders are passable only where the round has openings.
- Holding Jump while landing on a bubble bounces the player upward instead of popping it. Touching a bubble without holding Jump pops it. Bubble jumping is how players reach high platforms.
- Water from a water bubble carries the player along without killing them. Fire from a fire bubble briefly stuns a player but does not kill them.

**Lives, death and respawn**

- 3 lives by default (DIP options: 1, 2, 3 or 5).
- Extra lives at 30,000 and 100,000 points by default (other DIP settings: 20k/80k, 40k/200k, 50k/250k), and by completing EXTEND. Later thresholds are not documented in the sources.
- A dead player respawns at their start corner, blinking and invulnerable for about 2–3 s (not measured).
- When any player loses a life, all angry enemies calm down, the round timer restarts, and the chaser hunting the dead player leaves.

## Bubbles

A bubble has three phases:

1. **Shot.** The bubble leaves the dragon's mouth and flies horizontally at high speed for a fixed distance. Any enemy it touches in this phase is trapped. Blowing point-blank into an adjacent enemy traps it at once. Hitting a wall ends this phase early.
2. **Float.** The bubble slows down and drifts along the air currents. Floating bubbles push each other apart, so they pile up in clusters at gathering points.
3. **Expiry.** After a fixed time an empty bubble changes colour, flashes and pops by itself.

Candy power-ups extend range, speed and fire rate (see Items).

**Trapped enemies**

- A trapped enemy rides its bubble along the air currents and cannot harm anyone.
- Its bubble changes colour in stages as a warning (normal, warmer colours, flashing). When the time runs out, the enemy breaks free angry: faster and pink/red. The escape time per enemy type and difficulty is not measured.

**Popping**

- A player pops a bubble by touching it without holding Jump.
- Popping an empty bubble scores 10 points.
- A popped bubble also pops every bubble it touches, and so on. Every trapped enemy in that chain counts towards one combo.
- A popped enemy flies off in an arc, bounces off walls, lands and turns into a food item whose value depends on its place in the combo (see Scoring).

Air currents are the level designer's main tool: they decide where trapped enemies drift, whether players can reach them, and where combos can be built.

## Enemies

Enemies are introduced over the first 60 rounds.

| Name (aliases) | First round | Movement | Attack |
| --- | --- | --- | --- |
| Zen-Chan (Bubble Buster, Benzo) | 1 | Walks along platforms, jumps up to the player's level, falls off edges | Contact only |
| Mighta (Stoner) | 6 | Walks and jumps like Zen-Chan | Stops and throws a rock horizontally |
| Monsta (Blubba) | 10 | Flies diagonally, ignores platforms, bounces off walls, floor and ceiling | Contact only |
| Pulpul (Hullaballoon) | 20 | Flies freely through platforms, changes direction often | Contact only |
| Banebou (Coiley) | 30 | Bounces continuously in arcs on a spring, rebounds off walls | Contact only |
| Hidegons (Incendo) | 40 | Fast walker, weak jumper | Spits fireballs horizontally without stopping |
| Drunk (Willy Whistle) | 50 | Walks and jumps | Throws bottles horizontally |
| Invader (Super Socket) | 60 | Only left/right, drops off platform edges | Fires lasers straight down |
| Skel-Monsta (Baron von Blubba) | Any round after time-out | Moves in steps towards a player, passes through walls, speeds up over time | Contact kills, invincible |
| Super Drunk (Grumple Gromit) | 100 | Large, floats around the screen | Throws bottles |

In Super mode the pairs Zen-Chan/Hidegons, Mighta/Drunk and Monsta/Pulpul swap places; Banebou stays.

**Shared rules**

- Enemies enter at round start and take up their start positions, each after its own delay.
- Walkers decide to jump or drop depending on where the nearest player is.
- Enemies wrap from bottom to top through openings, like players.
- An enemy turns angry (pink/red and faster) when it escapes a bubble, when "Hurry Up!" appears, or when it is the last one left. All angry enemies calm down when a player loses a life.
- Projectiles kill players on contact.

## Level flow

Every round runs through the same five stages. Only EXTEND and the umbrellas skip ahead.

1. **Intro.** The previous round scrolls up out of view and the new layout scrolls in. Players are carried to their start corners inside bubbles, enemies take their positions, and the round number is shown.
2. **Play.** The round timer starts. Each round can produce at most one point (food) item and one special item, which appear in fixed slots a few seconds after the start. Which item appears depends on the hidden counters. Special bubbles enter through the openings in open rounds.
3. **Hurry Up.** When the timer runs out, "HURRY UP!" flashes, the music speeds up and every enemy turns angry. About 10 seconds later the invincible chaser appears, one per living player. It moves in steps towards its player, ignores walls and gets faster until the round ends or the player dies. Rounds 94 and 100 have no time limit.
4. **Clear.** When the last enemy is gone, players get a few seconds to collect the remaining food. Candy cane and chest bonuses trigger here, and so does the score-digit trick: if the hundreds and tens digits of either score match, all remaining bubbles turn into fruit.
5. **Transition.** The next round scrolls in. Completing EXTEND ends the round at once; an umbrella skips 3, 5 or 7 rounds.

The time limit is set per round. It ranges from very long down to a few seconds on some rounds.

A dead player respawns in the same round; the round does not restart. When a player has no lives left, the other player continues alone. With no players left the game shows the continue screen.

## Special bubbles

Special bubbles are not blown by players. They drift in through the openings of open rounds, follow the air currents and trigger their effect when a player pops them. Players can nudge them into position with their own bubbles.

| Bubble | Effect when popped | Enemies it defeats | Notes |
| --- | --- | --- | --- |
| Water | A stream pours out, runs down and along platforms and leaves through a bottom opening | Become high-value items; all enemies swept together count as one combo | Carries players along without harming them |
| Fire | A fireball drops straight down and spreads a short-lived carpet of fire on the platform where it lands | Become items | Briefly stuns players who touch it |
| Lightning | A bolt flies horizontally across the screen, opposite to the popping player's facing direction | Become items | The only weapon against the final boss |
| EXTEND letter (E, X, T, E, N, D) | The letter is added to the popping player's EXTEND display | – | See below |
| Fireball bubble (very rare) | Fire breath instead of bubbles for 5 rounds, as long as the player doesn't die | Become 9,000-point diamonds | 1 in 4096 chance per naturally spawned bubble; 100,000 points |

**EXTEND**

- Letter bubbles only appear in open rounds.
- Each player collects their own set of six letters. Completing E-X-T-E-N-D gives that player an extra life and ends the round at once.
- How many different letters appear depends on the biggest combo popped in the preceding non-open rounds and the current round: 3 enemies at once give 1 letter type, 4 give 2, 5 give 3, 6 give 4, 7 give 5, 8 give all 6. Combos of 2 or fewer give none.
- Collecting the same letter three times spawns a candy cane, which turns into a large bonus item when the round is cleared.

## Items

Items come in two families: point items (food) that only add score, and special items that change the rules. Special items are not random. Each one is unlocked by an invisible counter, and the game places at most one per round in a fixed item slot.

**Point items**

- Food dropped by defeated enemies, worth what the combo gives (see Scoring).
- One extra food item per round in the round's item slot, worth from a few hundred to several thousand points.
- Large bonus items (big food, big diamonds) at round clear after a candy cane or chest was collected.

**Special items**

Where four values are given, they apply to the four difficulty settings, easiest first.

| Item | Effect | Unlocked by | Points |
| --- | --- | --- | --- |
| Candy (3 colours) | One each: bubbles fly farther / faster / more often. Kept until death | Blow 35 bubbles / pop 35 bubbles / jump 35 times | 100 |
| Shoes | Faster running, jumping and falling. Kept until death | Run across the full screen 15 times | 100 |
| Rings (3 types) | Bonus points per jump (500), per bubble blown (100) or per pixel walked (10) | Eat 3 candies of the matching colour | 1,000 |
| Umbrella (3 types) | Skip 3, 5 or 7 rounds | Pop 15, 20 or 25 water bubbles | 200 |
| Cross of water | Floods the screen; enemies become 6,000–7,000-point diamonds | Collect 10/11/12/13 special items | 3,000 |
| Cross of thunder | Lightning showers the screen; enemies become diamonds | Collect 10/12/14/16 point items | 3,000 |
| Cross of fire | Fire breath for the round; enemies become 9,000-point diamonds | Drown 4/5/6/7 enemies with water | 3,000 |
| Book / bomb types | Explosion kills every enemy on screen, turning them into diamonds | Pop 10/13/16/19 fire bubbles, or burn 10/12/14/16 enemies with fire | 200–3,000 |
| Heart | Freezes enemies; touching them turns them into items | Collect 50/55/60/65 enemy-drop foods | – |
| Necklace | A ball bounces around the screen, turning enemies into 6,000-point diamonds | 15/20/25/30 rounds started | 5,000 |
| Lanterns | Several candy and ring effects at once, or an explosion | Player joins 5 times, collect umbrellas or clocks | 2,000 |
| Bell | Screen flashes when a powerful special item is about to appear; lasts the whole game | "Hurry Up!" shown 8/10/12/14 times | 1,000 |
| Potion | Enemies vanish, the screen fills with 500-point items for 30 s; collecting all gives 100,000 (with 2 players: the one with more items gets 100,000, the other 50,000) | Fall from the bottom of the screen to the top 15 or more times | – |
| Candy cane / chest | At round clear, remaining bubbles turn into food or diamonds plus one large item worth 10,000–80,000 | Three of the same EXTEND letter | 500 / 1,000 |

Sources disagree on a few pairings, for example whether the clock stops the enemies or the round timer.

## Scoring

Most of the score comes from combos: the n-th enemy defeated in one pop chain is worth 1,000 × 2^(n−1) points.

| Enemy in the chain | Food value | Total for the chain |
| --- | --- | --- |
| 1st | 1,000 | 1,000 |
| 2nd | 2,000 | 3,000 |
| 3rd | 4,000 | 7,000 |
| 4th | 8,000 | 15,000 |
| 5th | 16,000 | 31,000 |
| 6th | 32,000 | 63,000 |
| 7th | 64,000 | 127,000 |

Sources describe the same doubling in two ways: as rising food values per enemy, or as one bonus for the whole chain. Whether the arcade credits the points on the pop or when the food is collected is not documented.

| Other event | Points |
| --- | --- |
| Pop an empty bubble | 10 |
| Regular round food item | About 700 |
| Bonus food items (popcorn, burger and similar) | 500–4,000 |
| Enemy killed by a special effect (cross, explosion, stars, ball) | 6,000–10,000 diamond |
| Large item at round clear after candy cane or chest | 10,000–80,000 |
| Collect every item in a potion round | 100,000 |
| Fireball bubble pickup | 100,000 |

## Secret rooms, boss, endings and two players

**Secret rooms and warps**

- Reaching rounds 20, 30 and 40 without losing a life makes a door appear in that round. It leads to a bonus room full of diamonds with a coded message on the wall.
- The three messages together hint at how to reach the happy ending and unlock Super mode.
- Reaching round 50 without losing a life opens a warp to a later round (most sources say round 70).

**Final boss (round 100)**

- Round 100 has no time limit and a single large boss (Super Drunk) that floats around the screen and throws bottles.
- From round 100 on, players blow lightning bubbles. Lightning is the only thing that damages the boss, and it takes many hits.
- When defeated, the boss is trapped in a giant bubble. Popping it gives a large bonus and starts the ending.

**Endings**

- Finishing alone gives a short bad ending that tells the player to come back with a friend.
- Finishing with two players gives the happy ending: the dragons turn back into the human brothers and are reunited with their girlfriends.
- The ending reveals a code for Super mode: a second run of 100 rounds with different colours and swapped enemy types.

**Two players**

- Both players share the round, the enemies and the bubbles. Either player can pop or bounce on any bubble.
- Players cannot hurt each other, but they compete for food and items.
- Score, lives, power-ups and EXTEND letters are tracked per player.
- Player 2 can join at any time. A player with no lives left can rejoin with a continue.
- A round ends when all enemies are gone, whichever player defeated them.

## Sources

- [StrategyWiki: Bubble Bobble special items](https://strategywiki.org/wiki/Bubble_Bobble/Special_items) (item effects and unlock conditions)
- [Bubble Bobble FAQ (lysator.liu.se)](https://www.lysator.liu.se/~/wizkid/arcade/info/bubble.faq) (EXTEND rules, doors, DIP settings, score-digit trick)
- [Arcade-History: Bubble Bobble](https://www.arcade-history.com/game/343/) (enemy names and first rounds, combo doubling)
- [Wikipedia: Bubble Bobble](<https://en.wikipedia.org/wiki/Bubble_Bobble_(video_game)>) (angry enemies, overview)
- [Hardcore Gaming 101: Bubble Bobble (Arcade)](https://www.hardcoregaming101.net/bubble-bobble-arcade/) (time limit, air currents)
- [ExoticA: Lost in Translation – Bubble Bobble](https://www.exotica.org.uk/wiki/Lost_In_Translation/Bubble_Bobble) (Super mode enemy swaps)

The last four were read via search excerpts only.
