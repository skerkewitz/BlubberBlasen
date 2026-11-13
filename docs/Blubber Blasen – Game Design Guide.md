# Blubber Blasen – Game Design Guide

Nov 13, 2026 · @Stefan

## Overview

Blubber Blasen is a single-screen, two-player co-op platformer that reproduces the gameplay of Taito's arcade *Bubble Bobble* (1986). This guide describes the arcade original's mechanics in enough detail to build a first playable version. Names of original characters and items are used only as a reference; Blubber Blasen will use its own art, names, music and levels.

The core loop fits in one sentence: trap every enemy on the screen in a bubble, pop the bubbles to defeat them, collect the food they drop, move to the next round.

**Design pillars of the original**

- **One screen, one puzzle.** Every round is a fixed 1-screen layout. The challenge is routing around platforms and air currents, not exploration.
- **Bubbles are weapon, terrain and transport.** The same object traps enemies, can be stood on and ridden upwards, and carries special effects (water, fire, lightning).
- **Risk equals reward.** Popping several trapped enemies at once multiplies the score; waiting too long lets them escape angrier.
- **Co-op first.** Two players share the screen at the same time. The true ending is only reachable in two-player mode.
- **Hidden systems.** Many items are triggered by invisible counters (bubbles blown, jumps, distance walked). They reward long-term play but are not needed for a first version.

**Scope of the original:** 100 rounds plus a boss round, 8 regular enemy types, 3 special bubble types, EXTEND letters, about 70 food items and more than 20 special items.

## Technical frame

The whole game runs on one fixed 256 × 224 screen at about 60 frames per second, which matches the SNES default mode exactly. All timings in this guide are given in seconds or frames at 60 fps.

| Element | Original arcade | Recommendation for Blubber Blasen |
| --- | --- | --- |
| Resolution | 256 × 224, horizontal | Same (SNES mode 1, 256 × 224) |
| Frame rate | \~60 Hz | Fixed 60 Hz logic step, all timers in frames |
| Level grid | Small square wall blocks on a fixed grid, solid side walls | 8 × 8 px tiles; level stored as a 32 × 26 collision grid (verify exact row count in MAME) |
| HUD | Scores and high score on the top line, lives in the bottom corners | Top tile row for scores, lives drawn as sprites at the bottom |
| Characters | 16 × 16 px sprites | 16 × 16 px sprites, hitboxes slightly smaller than the sprite |
| Rounds | 100 + boss, each one screen, no scrolling during play | One data file per round |

**Round data needed per level**

- Collision grid: solid blocks, and one-way platforms (players and enemies can jump up through platforms from below and land on top).
- Openings: holes in the top and bottom border. An object falling through a bottom hole reappears at the top (vertical wrap-around). Rounds with openings are the "open" rounds where special bubbles enter.
- Air current map: a direction per cell (up, down, left, right) that steers free-floating bubbles. Currents usually converge on one or more gathering points.
- Enemy list: type, start position, start direction and a spawn delay per enemy (the original allows up to 7 enemies per round).
- Special bubble source: which special bubble type (if any) the round emits and from which opening.
- Timer: seconds until "Hurry Up!" for this round.
- Palette and wall-block graphic for the round.

## Players

Two dragons play at the same time: player 1 starts in the bottom-left corner, player 2 in the bottom-right corner. A single touch from an enemy, an enemy projectile or the Hurry-Up chaser kills them.

**Controls** (arcade: 2-way joystick + 2 buttons; SNES mapping in brackets)

| Input | Action |
| --- | --- |
| Left / right (D-pad) | Walk. The dragon also turns to face that way, which sets the bubble direction. |
| Jump (B) | Fixed-height jump. Left/right steers in the air. |
| Bubble (Y) | Blow one bubble horizontally in the facing direction. Fire rate is limited (see candy power-ups). |
| Start | Player 2 joins at any time (arcade: coin + start). |

**Movement rules**

- No climbing, no double jump. The only ways up are jumping through one-way platforms from below, bouncing on bubbles, and falling through a bottom opening to wrap to the top.
- Jumps pass upward through platforms and land on top of them. Falling is slower than in most platformers, and the player keeps air control the whole time.
- Solid side walls are never passable. Top and bottom borders are passable only where the round has openings.
- Bubble jumping: holding Jump while landing on a bubble bounces the player upward instead of popping it. Releasing Jump and touching the bubble pops it. This is the key skill for reaching high platforms.
- Water from a water bubble carries the player along without killing them. Fire from a fire bubble briefly stuns a player who touches it but does not kill them.

**Lives, death and respawn**

- Default 3 lives (arcade DIP options: 1, 2, 3 or 5).
- Extra lives: by score at 30,000 and 100,000 (default DIP; other settings 20k/80k, 40k/200k, 50k/250k), and by completing EXTEND (see Special bubbles). Later score thresholds are an open question.
- On death the player respawns at their start corner, blinking and invulnerable for a short time (about 2–3 s, measure in MAME).
- When any player loses a life, all angry enemies calm down again and the round timer restarts; the Hurry-Up chaser that hunted the dead player leaves.

## Bubbles

A bubble is a short-lived object with three phases: shot, float and expiry. Only the shot phase traps enemies; the float phase turns the bubble into a platform and a combo tool.

1. **Shot.** The bubble leaves the dragon's mouth and flies horizontally at high speed for a fixed distance. Any enemy it touches in this phase is trapped. Blowing point-blank into an adjacent enemy traps it immediately. Hitting a wall ends this phase early.
2. **Float.** The bubble slows down and drifts along the round's air current map. Floating bubbles push each other apart rather than overlapping, so they pile up in clusters at gathering points.
3. **Expiry.** After a fixed lifetime an empty bubble changes colour, flashes and pops by itself. Candy power-ups extend range, speed and fire rate (see Items).

**Trapped enemies**

- A trapped enemy rides its bubble along the air currents and cannot harm anyone.
- Its bubble changes colour in stages as a warning (normal, then warmer colours, then flashing). When the timer runs out the enemy breaks free and is angry: faster and pink/red.
- Tune the escape time per enemy type and difficulty. Measure the original's values in MAME.

**Popping**

- A player pops a bubble by touching it without holding Jump. Holding Jump on top of a bubble bounces instead.
- Popping an empty bubble scores 10 points.
- Chain reaction: a popped bubble also pops every bubble it touches, and so on recursively, in the same frame or over a few frames for visual effect. Every trapped enemy in that chain counts as one combo.
- A popped enemy flies off in an arc, bounces off walls, lands and turns into a food item (value set by its place in the combo, see Scoring).

**Design notes**

- Air currents are the level designer's main tool. They decide where trapped enemies drift, whether players can collect them, and where combos can be built.
- Bubbles must be stable as platforms: a bubble a player is bouncing on keeps its float behaviour and is not pushed into the player.

## Enemies

The original has 7 regular enemy types, introduced every 10 rounds, plus an invincible time-out chaser and one final boss. Blubber Blasen needs its own designs, but the behaviour slots below cover the whole roster.

| Original name (aliases) | First round | Movement | Attack | Behaviour slot for Blubber Blasen |
| --- | --- | --- | --- | --- |
| Zen-Chan (Bubble Buster, Benzo) | 1 | Walks along platforms, jumps up to the player's level, falls off edges | Contact only | Basic walker |
| Mighta (Stoner) | 6 | Walks and jumps like Zen-Chan | Stops and throws a rock horizontally | Walker + stop-and-shoot |
| Monsta (Blubba) | 10 | Flies diagonally, ignores platforms, bounces off walls, floor and ceiling | Contact only | Diagonal bouncer |
| Pulpul (Hullaballoon) | 20 | Flies freely through platforms, changes direction often | Contact only | Free flyer |
| Banebou (Coiley) | 30 | Bounces continuously in arcs on a spring, rebounds off walls | Contact only | Bouncer |
| Hidegons (Incendo) | 40 | Fast walker, weak jumper | Spits fireballs horizontally without stopping | Walker + run-and-shoot |
| Drunk (Willy Whistle) | 50 | Walks and jumps | Throws bottles horizontally | Walker + thrown projectile |
| Invader (Super Socket) | 60 | Only left/right, drops off platform edges | Fires lasers straight down | Horizontal patrol + down-shooter |
| Skel-Monsta (Baron von Blubba) | Any round after time-out | Moves in steps towards a player, passes through walls, speeds up over time | Contact kills | Invincible chaser (see Level flow) |
| Super Drunk (Grumple Gromit) | 100 | Large, floats around the screen | Throws bottles | Final boss (see Secret rooms and endings) |

First-round numbers are for the normal game. In Super mode the pairs Zen-Chan/Hidegons, Mighta/Drunk and Monsta/Pulpul swap places; Banebou stays.

**Shared rules**

- Enemies enter at round start and take up their start positions. Each enemy can have its own spawn delay.
- Walkers decide to jump or drop based on where the nearest player is. They are the main source of the game's "chase" feeling.
- Enemies wrap from bottom to top through openings, just like players.
- An enemy becomes **angry** (pink/red and faster) when it escapes a bubble, when "Hurry Up!" appears, or when it is the last enemy left. All angry enemies calm down when a player loses a life.
- Projectiles kill players on contact.

## Level flow

Every round runs through the same five stages; only EXTEND and umbrella items skip ahead.

1. **Intro.** The previous round scrolls up out of view and the new layout scrolls in. Players are carried to their start corners inside bubbles, enemies take their positions, and the round number is shown.
2. **Play.** The round timer starts. Each round can produce at most one point (food) item and one special item, which appear in fixed slots a few seconds after the start (which item depends on the hidden counters, see Items). Special bubbles enter through openings in open rounds.
3. **Hurry Up.** When the timer runs out, "HURRY UP!" flashes, the music speeds up and every enemy turns angry. About 10 seconds later the invincible chaser (Skel-Monsta) appears, one per living player. It moves in steps towards its player, ignores walls and gets faster until the round ends or the player dies. Rounds 94 and 100 have no time limit.
4. **Clear.** When the last enemy is gone, players get a few seconds to collect the remaining food. Special clear effects trigger here: candy cane or chest bonuses, and the score-digit trick (if the hundreds and tens digits of either score match, all remaining bubbles turn into fruit).
5. **Transition.** The next round scrolls in. Collecting all six EXTEND letters ends the round immediately and goes to the next one; an umbrella skips 3, 5 or 7 rounds.

**Timer values**

- The "Hurry Up!" limit is set per round. In the original it ranges from very long down to only a few seconds on some rounds.
- For a first version, use one default (for example 30 s) plus a per-round override, and tune by playtesting.

**Losing and continuing**

- A dead player respawns in the same round; the round does not restart.
- When a player has no lives left, the other player continues alone. With 0 players left: game over, continue screen.

## Special bubbles

Special bubbles are not blown by players. They drift in through the openings of open rounds, follow the air currents like normal bubbles and trigger their effect when a player pops them. Players can nudge them with their own bubbles to move them into position.

| Bubble | Effect when popped | Kill reward | Notes |
| --- | --- | --- | --- |
| Water | A stream pours out, runs down and along platforms and leaves through a bottom opening | Swept enemies become high-value items; all enemies swept together count as one combo | Carries players along without harming them |
| Fire | A fireball drops straight down and spreads a short-lived carpet of fire on the platform where it lands | Burned enemies become items | Players who touch the fire are briefly stunned |
| Lightning | A bolt flies horizontally across the screen, opposite to the popping player's facing direction | Enemies hit become items | The only weapon against the final boss |
| EXTEND letter (E, X, T, E, N, D) | The letter is added to the popping player's EXTEND display | — | See below |
| Fireball bubble (very rare) | Gives the player fire breath for 5 rounds instead of bubbles, as long as they don't die | Enemies become 9,000-point diamonds | 1 in 4096 chance per naturally spawned bubble; 100,000 points |

**EXTEND rules**

- Letter bubbles only appear in open rounds.
- Each player collects their own set of six letters. Completing E-X-T-E-N-D gives that player an extra life and ends the round immediately.
- How many different letters appear depends on the biggest combo popped in the preceding non-open rounds (and in the current round). In the original: 3 enemies at once = 1 letter type, 4 = 2, 5 = 3, 6 = 4, 7 = 5, 8 = all 6. Combos of 2 or fewer produce none.
- Collecting the same letter three times spawns a candy cane, which turns into a large bonus item when the round is cleared (see Items).

**Spawn rules for a first version**

- Give each open round a list of special bubble types it emits and an emit interval.
- Queue EXTEND letters earned by combos and release them in the next open round.

## Items

Items come in two families: point items (food) that only add score, and special items that change the rules. Special items are not random: each one is unlocked by an invisible counter (bubbles blown, jumps, rounds played and so on), and the game places at most one per round in a fixed item slot.

**Point items**

- Food dropped by defeated enemies; its value comes from the combo (see Scoring).
- One extra food item per round appears in the round's item slot; values range from a few hundred to several thousand points.
- Large bonus items (big food, big diamonds) appear at round clear after a candy cane or chest was collected.

**Special items**

Thresholds below are from the original. Where four values are given, they apply to the four difficulty settings, easiest first.

| Item | Effect | Unlocked by | Points |
| --- | --- | --- | --- |
| Candy (3 colours) | One each: bubbles fly farther / bubbles fly faster / faster fire rate. Kept until death | Blow 35 bubbles / pop 35 bubbles / jump 35 times | 100 |
| Shoes | Faster running, jumping and falling. Kept until death | Run across the full screen 15 times | 100 |
| Rings (3 types) | Bonus points per jump (500), per bubble blown (100) or per pixel walked (10) | Eat 3 candies of the matching colour | 1,000 |
| Umbrella (3 types) | Skip 3, 5 or 7 rounds | Pop 15, 20 or 25 water bubbles | 200 |
| Cross of water | Floods the screen; enemies become 6,000–7,000-point diamonds | Collect 10/11/12/13 special items | 3,000 |
| Cross of thunder | Lightning showers the screen; enemies become diamonds | Collect 10/12/14/16 point items | 3,000 |
| Cross of fire | Player breathes fire for the round; enemies become 9,000-point diamonds | Drown 4/5/6/7 enemies with water | 3,000 |
| Book / bomb types | Explosion kills every enemy on screen, turning them into diamonds | Pop 10/13/16/19 fire bubbles, or burn 10/12/14/16 enemies with fire | 200–3,000 |
| Heart | Freezes enemies; touching them turns them into items | Collect 50/55/60/65 enemy-drop foods | — |
| Necklace | A ball bounces around the screen, turning enemies into 6,000-point diamonds | 15/20/25/30 rounds started | 5,000 |
| Lanterns | Grant several candy and ring effects at once, or an explosion | Player joins 5 times, collect umbrellas or clocks | 2,000 |
| Bell | Screen flashes when a powerful special item is about to appear; lasts the whole game | "Hurry Up!" shown 8/10/12/14 times | 1,000 |
| Potion | Enemies vanish, screen fills with 500-point items for 30 s; collect all for a 100,000 bonus (2P: most items gets 100,000, the other 50,000) | Fall from the bottom of the screen to the top 15+ times | — |
| Candy cane / chest | At round clear, remaining bubbles turn into food or diamonds plus one large item worth 10,000–80,000 | Three of the same EXTEND letter | 500 / 1,000 |

Sources disagree on a few pairings (for example whether the clock stops enemies or the round timer). Treat this table as a design reference, not as an exact reproduction.

**Recommendation for the first version:** implement candies, shoes, umbrellas and the three crosses with simplified counters. Leave rings, lanterns, bell, potion and the high-score name tricks for later.

## Scoring

Score comes mostly from combos: the n-th enemy defeated in one pop chain is worth 1,000 × 2^(n−1) points, so popping enemies together is worth far more than popping them one by one.

**Combo table** (one chain, value of each enemy's food item)

| Enemies in the chain | Food value of that enemy | Total for the chain |
| --- | --- | --- |
| 1st | 1,000 | 1,000 |
| 2nd | 2,000 | 3,000 |
| 3rd | 4,000 | 7,000 |
| 4th | 8,000 | 15,000 |
| 5th | 16,000 | 31,000 |
| 6th | 32,000 | 63,000 |
| 7th | 64,000 | 127,000 |

Sources describe the same doubling in two ways: as rising food values per enemy, or as one bonus for the whole chain. Check in MAME whether the arcade awards the points when the food is collected or immediately on the pop.

**Other score events**

| Event | Points |
| --- | --- |
| Pop an empty bubble | 10 |
| Regular round food item | about 700 |
| Bonus food items (popcorn, burger and similar) | 500–4,000 |
| Enemy killed by a special effect (cross, explosion, stars, ball) | 6,000–10,000 diamond |
| Large item at round clear after candy cane or chest | 10,000–80,000 |
| Collect every item in a potion round | 100,000 |
| Fireball bubble pickup | 100,000 |

**Score-based extra lives:** 30,000 and 100,000 by default.

## Secret rooms, boss, endings and two-player rules

The long-term goals are reaching round 100, beating the boss there and seeing the true ending, which needs two players.

**Secret rooms and warps**

- Reaching rounds 20, 30 and 40 without losing a life makes a door appear in that round. It leads to a bonus room full of diamonds with a coded message on the wall.
- The three messages together hint at how to reach the happy ending and unlock Super mode.
- Reaching round 50 without losing a life opens a warp to a later round (most sources say round 70).

**Final boss (round 100)**

- Round 100 has no time limit and a single large boss (Super Drunk) that floats around the screen and throws bottles.
- From round 100 on, players blow lightning bubbles. Lightning is the only thing that damages the boss; it takes many hits.
- When defeated, the boss is trapped in a giant bubble. Popping it awards a large bonus and starts the ending.

**Endings**

- Finishing alone gives a short "bad" ending that tells the player to come back with a friend.
- Finishing with two players gives the happy ending: the dragons turn back into the human brothers and are reunited with their girlfriends.
- The ending reveals a code for Super mode: a second run of 100 rounds with different colours and swapped enemy types.

**Two-player rules**

- Both players share the round, the enemies and the bubbles. Either player can pop or bounce on any bubble.
- Players cannot hurt each other, but they compete for food and items.
- Score, lives, power-ups and EXTEND letters are tracked per player.
- Player 2 can join at any time; a player with no lives left can rejoin with a continue.
- A round ends when all enemies are gone, no matter which player defeated them.

## First playable version

A first version is complete when one or two players can play 10 original-style rounds with the core bubble loop, 3 enemy types, special bubbles and scoring. Everything else builds on top.

**Scope v1**

- [ ] Fixed 60 Hz loop, 256 × 224 screen, tile collision with one-way platforms and vertical wrap-around
- [ ] Player: walk, jump, air control, bubble shot, death, respawn with invulnerability, 3 lives
- [ ] Bubble: shot / float / expiry phases, air current map, bubble-to-bubble pushing, bubble jumping
- [ ] Trap, escape with angry state, pop, chain reaction, enemy-to-food animation
- [ ] Enemies: basic walker, diagonal bouncer, walker that throws projectiles
- [ ] Round flow: intro scroll, play, Hurry Up + invincible chaser, clear, transition
- [ ] Combo scoring, food items, score-based extra life
- [ ] Water, fire and lightning bubbles; EXTEND letters with extra life and round skip
- [ ] Candies, shoes and one umbrella with simple counters
- [ ] Two players at the same time
- [ ] Level format plus 10 own rounds

**Later versions**

- Remaining enemy types, all 100 rounds and the boss round
- Full hidden-counter item system, crosses, potions, bell, rings, lanterns
- Secret rooms, warps, endings and Super mode

**Open questions to measure in MAME**

- Exact jump height and arc, walk speed, fall speed, bubble range and float speed (frames and pixels)
- Bubble lifetime and enemy escape time per type and difficulty
- Per-round Hurry Up timer values
- Exact row count of the level grid
- Score thresholds for extra lives after 100,000
- When combo points are credited: on pop or on food pickup

**Sources**

- [StrategyWiki: Bubble Bobble special items](https://strategywiki.org/wiki/Bubble_Bobble/Special_items) (item effects and unlock conditions)
- [Bubble Bobble FAQ (lysator.liu.se)](https://www.lysator.liu.se/~/wizkid/arcade/info/bubble.faq) (EXTEND rules, doors, DIP settings, score-digit trick)
- [Arcade-History: Bubble Bobble](https://www.arcade-history.com/game/343/) (enemy names and first rounds, combo doubling)
- [Wikipedia: Bubble Bobble](<https://en.wikipedia.org/wiki/Bubble_Bobble_(video_game)>) (angry enemies, overview)
- [Hardcore Gaming 101: Bubble Bobble (Arcade)](https://www.hardcoregaming101.net/bubble-bobble-arcade/) (time limit, air currents)
- [ExoticA: Lost in Translation – Bubble Bobble](https://www.exotica.org.uk/wiki/Lost_In_Translation/Bubble_Bobble) (Super mode enemy swaps)

The last four were read via search excerpts only. Original names appear here only as a reference; Blubber Blasen uses its own characters, graphics, music and level layouts.
