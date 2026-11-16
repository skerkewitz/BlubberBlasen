# Blubber Blasen – Game Design Guide

Blubber Blasen is a single-screen, two-player co-op platformer for a 256 × 224 screen at 60 Hz, built so it can later run on a Super Nintendo. It started as a remake of Taito's arcade *Bubble Bobble* and does not hide that: the bubble loop, the combos, EXTEND and the hundred rounds come from there. What the original does is described in [Bubble Bobble (Arcade) – Game Design Reference Guide](Bubble%20Bobble%20(Arcade)%20–%20Game%20Design%20Reference%20Guide.md).

The game has grown into something of its own. The Plus mode adds a dash, aiming upward, a push bubble and a vacuum, and these change how a round is played: players move bubbles around the room instead of waiting for the air to bring them. This guide describes the game we are building. The [story](story.md) has the world, the characters and the texts.

Numbers in this guide are the values in the code (`src/game/`) at the time of writing. If they disagree, the code is right and this guide needs fixing.

## Modes

**Plus** is the game. Everything in this guide applies to it unless a section says otherwise.

**Classic** is a switch on the options screen. It turns off the Plus tools (dash, aiming, push bubble, vacuum) and lets fired bubbles pass through inner walls, as in the arcade. The rounds, enemies, items and story stay ours. Classic is close to the original's rules, but it is not the original game: that would also need the original 100 room layouts and the full item system. If we ever want a faithful remake, it is easier to fork this project and build it there than to keep two games in one code base.

The code still calls Classic "Arcade" and starts in it until the options are changed. Both are to be switched over.

Two more options work in both modes:

- **Co-op rescue** (off by default): an enemy that touches a player traps them in a rescue bubble instead of killing them. See Two players.
- **Player 2: CPU** (off by default): a computer partner plays player 2.

## Technical frame

| Element | Value |
| --- | --- |
| Screen | 256 × 224 at 60 Hz, all timers in frames |
| Layout | 2 tile rows of HUD at the top, then the room: 32 × 26 tiles of 8 × 8 px |
| Sprites | 16 × 16 px, hitboxes smaller than the sprite |
| Object limits | 2 players, 8 enemies, 16 bubbles, 16 items |
| Rounds | 100 plus a secret room, one screen each, no scrolling during play |
| Math | 8.8 fixed point, no floats, no heap (SNES port) |

A round is a `.map` file from the level editor (solid tiles, air direction per tile, enemies with position, type and facing) plus a `.txt` settings file: time until Hurry Up, tileset, entry delay per enemy, special bubble type and interval. See [tools/README.md](../tools/README.md).

Solid tiles are one-way from below: players and enemies jump up through platforms and land on top. The side walls are always solid. Gaps in the bottom row let anything fall through and come back in at the top.

## Readability and art limits

The player must see at a glance what is solid, what is dangerous and what can be picked up. Every room, background and sprite is drawn to these rules, and the SNES limits behind them are fixed in [ADR-0004](adr/0004-16-bit-look-with-snes-limits.md).

**Brightness order**, brightest first: players and bubbles, enemies and items, walls and platforms, background. Saturated colours are only for things that move or can be picked up.

**Solid tiles**
- Walls and platforms are on BG1 and clearly brighter and warmer than the background (for example copper or brass against blue-grey).
- Every platform has a light edge of 1–2 px on its top: a light edge means "you can stand here". Shadow tiles below and to the right of solid tiles separate them from the background.
- Platforms are one tile (8 px) thick and keep to the grid. One tile style per room, so every platform in it looks the same.
- Nothing lies on a platform that looks solid unless it is solid.

**Background**
- On BG2, dark (roughly 30–40 % brightness), low contrast, desaturated.
- Large, calm shapes. Detail is vertical: chains, vertical pipes, pillars, brick. No horizontal ledges, shelves, girders, barrels or crates that could be mistaken for a platform.
- No detail the size of a bubble or an enemy, little or no movement.

**Characters and objects**
- 16 × 16 px. Player 1 and player 2 are told apart by colour; no enemy uses the players' colours, and angry enemies use red, which no player uses.
- Bubbles have a bright, thick rim and a strong shine spot with a transparent middle, so they stand out against a dark background.

**Checks for every room**
- Greyscale test: turn a screenshot to greyscale and blur it. Platforms must still be clearly brighter than the background.
- Busy test: check the room with many bubbles, water and Hurry Up on screen, and take the background back where it makes the room harder to read.

## Players

Blubber (player 1, green) starts in the bottom-left corner, Blasi (player 2, cyan) in the bottom-right.

**Controls** (SNES layout)

| Button | Action | Mode |
| --- | --- | --- |
| D-pad left / right | Walk and turn | Both |
| B | Jump | Both |
| Y | Blow a bubble; with Up held, blow it upward | Up: Plus |
| X | Push bubble; with Up held, upward | Plus |
| A | Vacuum | Plus |
| L / R | Dash left / right | Plus |
| Start | Join, continue, pause | Both |
| Select | Options (title screen) | Both |

**Movement**

- Walk speed 0.92 px per frame, 1.3 with shoes. A jump is about 42 px high (five tiles); releasing B early cuts it short. Falling is capped at 2 px per frame, and the player steers in the air the whole time.
- No climbing and no double jump. The ways up are jumping through platforms, bouncing on bubbles and falling through a bottom gap.
- Landing on a bubble while holding B bounces the player up. Touching a bubble without holding B pops it.
- Water carries a player along without harm. Fire stuns a player for one second.

**Lives**

- 3 lives, up to 9. Extra lives at 30,000 and 100,000 points and for a completed BLASEN.
- After a death the player respawns at their corner and blinks, invulnerable for 2 s. The round goes on.
- Losing a life calms every angry enemy and restarts the round's Hurry Up timer.
- When the last player is out, a 9-second countdown lets them continue with Start; the score is kept. Player 2 can join at any time.

## Bubbles

A fired bubble flies 4 px per frame for 8 frames (about 32 px), then floats. It traps any enemy it touches while flying. A floating bubble drifts with the room's air at 0.35 px per frame (0.30 with an enemy inside). Floating bubbles push apart when closer than 13 px, so they gather into touching clusters where the air meets. Floating bubbles closer than 24 px cling together like soap: each is pulled 0.25 px per frame towards the other until they touch, so a loose group closes into a cluster that chains.

An empty bubble lasts 10 s and turns orange and then red before it bursts. A trapped enemy breaks free after 5 s, with the same warning colours, and comes out angry.

Popping a bubble pops every bubble in reach, one link every 5 frames, so a chain is visible as it runs through a cluster. In reach means less than 18 px apart on both axes and at most 25 px for dx + dy together. A bubble is drawn 16 px wide, so bubbles that look touching always chain, like coyote time in jumping, and the dx + dy limit keeps diagonal neighbours from getting more reach than side-by-side ones. The values are a first guess and get tuned by play. All enemies in one chain form one combo. A popped enemy flies through the room in an arc and lands as food.

**Walls and aiming (Plus).** A fired bubble stops at walls and platforms and starts floating in front of them. Blown into a wall at point-blank range, it pops at once. Holding Up shoots a bubble or a push bubble straight up. In Classic, fired bubbles pass through inner walls and Up does not aim.

## The Plus tools

The three tools all answer the same problem: in the arcade, a bubble that drifts out of reach is lost unless the player takes a detour or a risky jump. In Plus the player moves bubbles to where they want the chain.

**Push bubble (X).** A small, fast bubble (6 px per frame, about 72 px) that traps nothing. It bursts on the first floating bubble it meets and shoves it, empty or with an enemy inside. A hit on the upper third pushes the bubble down, on the lower third up, in the middle along the push bubble's direction. The shoved bubble slides at 3 px per frame, ignoring the air, slows down and stops at walls. An empty shoved bubble traps the first enemy it slides into. A push bubble shot upward always shoves its target up, through the platform above. A special bubble or letter that gets hit is set off as if touched. The push bubble also destroys enemy shots, and bursts without effect on enemies, players and the boss. One per player at a time, free.

Play found three uses that later tuning has to keep working:

- Combo shot: a normal bubble followed by a push bubble 3 or more frames later gets shoved on as its flight ends and travels much further. At 1–2 frames the push overtakes the bubble and misses. `test_push_bubble` pins this window.
- Shot defence: a push can destroy an enemy shot, but with only one push bubble at a time a near enemy still gets you while you deal with a far one.
- Bubbles through walls: a shoved bubble crosses platforms, so a bubble can be sent past a wall without jumping into it.

**Vacuum (A).** Puts a vacuum where the player is. For 1.5 s the air inside a circle of 5 tiles (40 px) points to its centre, so floating bubbles (empty, trapped, special, letters, and a partner's rescue bubble) gather there, about 1 px per frame at the edge and 3 near the centre. Then the room's air takes over again. The vacuum stays where it was placed, also when pressed mid-jump; pressing again moves it. Bubbles pass through walls on the way in. It moves no enemies or items, pops nothing, and costs no energy. A pulsing blue glow shows exactly the area that pulls (drawn as a colour window, the way the SNES would do it). It grows from a point to full size in 8 frames, wobbles like jelly while active (round, tall, round, wide, 4 px each way, every 32 frames), and shrinks back to a point and fades over the last 12 frames. The pull always covers the shape shown, so it reaches its full 40 px only once grown, and pressing again starts a new vacuum that grows again.

**Dash (L / R).** L dashes left and R dashes right, whatever the D-pad holds, so a player can walk one way and dash the other. A dash moves the player 54 px (about 7 tiles) in 12 frames without turning them around. It pops every bubble it passes through, goes through inner walls and platforms, never ends inside a wall, and keeps the player invulnerable for the dash and half a second after it. A press up to 6 frames early still counts.

The dash is free. Two rules limit it: one dash per jump, and 1 s (60 frames) after a dash ends before the next one. The cooldown is longer than the half second of invulnerability, so hammering the button leaves gaps in which the player can be hit. When the next dash is ready, the player glows yellow for a moment.

Everything one dash pops counts as one chain, also bubbles too far apart to set each other off. Two trapped enemies in one dash give 1,000 and 2,000 points of food, as in a chain reaction. A dash also knocks the plate off a Riveter (see the enemies).

Until version 1.4 the dash cost energy from a bar that filled by defeating enemies. Players rarely had a dash ready when they needed one, so no room could count on it. The bar was removed.

## Enemies

Every enemy is a factory worker bewitched by the Grumble Boiler (see the story). There are eight types plus the Hurry Up chaser and the boss.

| Type | Name in the story | First round | Movement | Attack |
| --- | --- | --- | --- | --- |
| Walker | Stoker | 1 | Walks, jumps up towards the player, drops off edges | Contact |
| Shield | Riveter | 11 | Slow walker behind a steel plate; jumps neither up nor over gaps | Contact |
| Thrower | Coal Tosser | 4 | Walks like the walker | Stops and throws a rock |
| Bouncer | Steam Puff | 6 | Flies diagonally, ignores platforms, bounces off walls | Contact |
| Flutter | Soot Moth | 21 | Flies through platforms, changes direction often | Contact |
| Hopper | Spring Fitter | 23 | Bounces in arcs on a spring, rebounds off walls | Contact |
| Spitter | Furnace Imp | 25 | Fast walker (1.5 px per frame), weak jumper | Spits fireballs |
| Dropper | Crane Drone | 28 | Patrols left and right, drops off edges | Shoots straight down |
| Chaser | Soot Cloud | Hurry Up | Comes for its player through walls, faster over time | Contact, invincible |
| Boss | Grumble Boiler | 100 | Floats around the top of the room | Spits soot |

**Riveter.** The plate covers the side the Riveter faces. A bubble from the front, fired or shoved, bursts on it; a bubble from behind traps it as usual. A dash through a Riveter, from either side, knocks the plate off: it stands dazed for 1 s and then walks on as a plain Stoker. In Classic, without the dash, players have to get behind it. Round 11 introduces it on open platforms.

**Rooms that need the dash.** Rounds 12 and 14 have a pocket on the bottom floor: a low room closed by walls on both sides and by a platform above. A Riveter waits inside. Players get in only with a dash through a wall and get out by jumping up through the platform. The air in the pocket blows up, so a trapped enemy floats out. In Classic, fired bubbles pass through inner walls, so the pocket can be cleared from outside. `mapview --check` lists every enemy that only a dash reaches; in round 16 that is already all of them, because the next platform above the start is 6 rows up, one more than a jump.

Rules all enemies share:

- Each enemy enters after its own delay from the round file.
- An enemy turns angry (red and faster) when it escapes a bubble, when Hurry Up starts, or when it is the last one left.
- Enemies wrap through bottom gaps like players.
- Fairness: every enemy that shoots stops and flashes for 30 frames before the shot leaves, and the shot keeps the direction the enemy faced when the wind-up began. No shot comes without warning, even point-blank. Trapping the enemy during the wind-up stops the shot.

## A round

1. **Intro.** The new room scrolls in from below, 2 px per frame, and the round number and, every ten rounds, the floor name are shown. The text sits on a darkened box so it stays readable on any room (a colour window in subtract-and-halve mode, the way the SNES would do it; sprites are not darkened).
2. **Play.** The Hurry Up timer runs. It is set per round, from 40 s in round 1 down to 17 s in round 99. After 5 s a waiting power-up appears on a platform, and after 10 s the round's bonus food (500 to 2,000 points). Rounds with a special bubble type send one in through the opening at a fixed interval.
3. **Hurry Up.** "HURRY UP!" appears and every enemy turns angry. 10 s later a Soot Cloud comes for each living player. Round 100 has no time limit.
4. **Clear.** When the last enemy is gone, players have 5 s to collect the food.
5. **Next round.** The next room scrolls in. A completed BLASEN ends the round at once and an umbrella skips 3 rounds. Both first stop the room for 2 s and say why in the darkened box of the round intro: `CONGRATULATIONS! / BLASEN COLLECTED / BLUBBER GETS AN EXTRA LIFE` (Blasi for player 2 and the CPU partner), or `UMBRELLA! / SKIP 3 ROUNDS`.

## Special bubbles, multikills and BLASEN

Special bubbles come in through the room's opening, float with the air and set off their effect when a player touches them or hits them with a push bubble. They last 20 s.

| Bubble | Effect | Enemies hit |
| --- | --- | --- |
| Water | A stream runs down and along the platforms and out through a bottom gap. Carries players without harm | Swept away; all of them count as one combo |
| Fire | A fireball drops onto the platform below and lights a carpet 4 tiles to each side for 3 s. Stuns players for 1 s | Defeated |
| Lightning | A bolt flies straight across the room | Defeated; the only thing that hurts the boss |

Three or more enemies defeated in one go are a multikill, whatever did it: a bubble chain, one dash, one water stream, one fire or lightning bubble, a Soap Star or Rinse Wave, or one touch on frozen enemies during the Steam Whistle. The third enemy plays the multikill sound in place of the kill sound. A multikill earns letter bubbles for the player who caused it: 1 for 3 enemies, 2 for 4, 3 for 5 or more. Up to 6 per player wait in a queue and come in one every 4 s through the opening, the two players taking turns. Each is a letter that player still misses, chosen at random and preferring one not already floating in the room. If they miss fewer letters than they earned, letters repeat: a 5-multikill when only B is missing brings three Bs.

Whoever pops a letter gets it; a letter they already have is wasted. The letters spell B-L-A-S-E-N, the game's name, and are shown on the side walls (player 1 left, player 2 right). All six give an extra life and end the round.

Chains and BLASEN are a bonus, not a core mechanic. Test games showed that a player can rarely set up a chain of three on purpose: the room decides whether enemies meet, the air that gathers bubbles is invisible and slow, and the clock (a trapped enemy escapes after 5 s, Hurry Up comes after 40 s down to 17 s) punishes waiting. The rules therefore make the bonus honest when the room allows it (bubbles that look touching chain, loose groups cling together, the multikill sound says it counted) and never make the game harder for a player who pops early. Background: `docs/adr/0003-multikills-and-blasen.md`.

## Items

Defeated enemies land as food, worth 1,000 for the first enemy of a chain and double for each further one, up to 64,000 for the seventh. Food stays 10 s. Every round also puts out one bonus food after 10 s: cherry 500, apple 700, melon 1,000 or cake 2,000. Enemies defeated by an item turn into 8,000-point gems.

Power-ups are unlocked by hidden counters. When a counter reaches its limit, the power-up waits for the next round and appears on a platform 5 s after the start, for 10 s.

| Power-up | Effect | Unlocked by |
| --- | --- | --- |
| Pink candy | Bubbles fly farther (14 frames instead of 8) | 35 bubbles blown |
| Blue candy | Bubbles fly faster (6 px per frame instead of 4) | 35 bubbles popped |
| Yellow candy | Shoot twice as often (every 7 frames instead of 15) | 35 jumps |
| Shoes | Run faster (1.3 px per frame) | Walking 15 times across the room |
| Umbrella | Skip 3 rounds | 5 water bubbles popped |
| Soap Star | Every enemy turns into a gem | 10 food items collected |
| Rinse Wave | Three water streams flood the room; every enemy turns into a gem | 6 enemies defeated with water |
| Steam Whistle | Enemies freeze for 8 s; touching a frozen enemy defeats it | 40 enemies defeated |
| Fire Glove | Fireballs instead of bubbles for the rest of the round | 4 enemies defeated with fire |
| Bubble Ring | 100 points per bubble blown | 3 candies collected |

Candies and shoes are lost with a life.

## Two players

Both players share the room, the enemies and the bubbles, and either can pop or bounce on any bubble. Players cannot hurt each other. Score, lives, power-ups and BLASEN letters are per player. A round ends when all enemies are gone, whoever defeated them.

**Co-op rescue** (option). An enemy that touches a player traps them in a rescue bubble for 7 s. It drifts with the air (and a vacuum pulls it), and the partner frees them by touching it, with no life lost. If the time runs out, the player loses a life. Shots, the Soot Cloud and the boss still kill.

**CPU partner** (option). A computer partner plays player 2. It frees the human from rescue bubbles, pops trapped enemies (preferably several at once), hunts enemies, then fetches BLASEN letters it still needs, and stays near player 1. It leaves letters it already has, and does not dash through one; a letter it touches anyway is wasted, as for a human. It looks half a second ahead every frame, trying its own moves (stay, walk, jump, dash, turn and shoot) against where enemies, shots, Soot Clouds and the boss will be. It leaves the food to the human and earns no score. Start on pad 2 takes over from it. A game finished with the CPU partner counts as a two-player game.

## Floors, secret rooms, boss and endings

The 100 rounds are ten factory floors of ten rounds each, from the Cellar to the Chimney, with the boss on top (see the story).

If nobody has lost a life, a door appears on the floor in rounds 20, 40, 50 and 60; pressing Up in front of it goes through. Rounds 20, 40 and 60 lead to the secret room, full of gems, which shows one part of the off-switch code. Round 50 leads to round 70.

Round 100 is the Grumble Boiler, a 32 × 32 px steam kettle with 24 hit points that floats around the top of the room and spits soot. It gets faster below half strength and again when nearly beaten. Bubbles bounce off it. In this round every bubble a player blows is a lightning bubble, and only lightning hurts it. Beaten, it is trapped in a giant bubble; popping it starts the ending. The ending is happy if two players (or one player and the CPU partner) made it, and sad if one did.

## Differences from the original

| Topic | Bubble Bobble (arcade) | Blubber Blasen |
| --- | --- | --- |
| World | Two dragons rescue their girlfriends | Two soap blobs free a bubble factory from the Grumble Boiler |
| Rounds | The original 100 layouts | Our own 100 rooms |
| Bubble tools | Bubble and jump only | Plus: push bubble, vacuum, dash, upward aim |
| Bubbles and walls | Fired bubbles pass through inner walls | Plus: they stop at walls |
| Enemy types | 8, introduced every 10 rounds up to round 60 | 8 (no bottle thrower, but a shield bearer), all introduced by round 28 |
| Enemy shots | Can come without warning | 30-frame wind-up before every shot |
| Getting caught | Always costs a life | Option: rescue bubble in co-op |
| Player 2 | Human only | Human or CPU partner |
| Extra-life letters | EXTEND, only in open rounds; letter types depend on the biggest combo | BLASEN, in any round; any multikill earns them, for the player who caused it, and the round end is explained on screen |
| Power-ups | More than 20, with difficulty-dependent limits | 10 with fixed limits |
| Umbrella | Skips 3, 5 or 7 rounds | Skips 3 |
| Secret doors | Rounds 20, 30, 40; warp in 50 | Rounds 20, 40, 60; warp in 50 |
| Boss | Throws bottles; players blow lightning from round 100 on | Spits soot; lightning bubbles in round 100 |
| Not built | Candy cane, chest, score-digit trick, rings for jumps and walking, necklace, potion, bell, lanterns, clock, book and bombs, the rare fireball bubble, Super mode, difficulty settings | |

## Open questions

- Is the vacuum too strong? Cooldown or limits are still open.
- Rounds 34, 39, 69 and 99 have enemies that walking, jumping and dashing do not reach (`mapview --check` reports them). Players need a bubble bounce there, or wait for the enemy to come out. Is that intended?
- Dash jump: a jump at the end of a dash that started on the ground, for a long jump sideways. On hold until the air dash (jump, then dash) has been tried in play.
- The CPU partner neither knows that a Riveter's front blocks bubbles nor plans dashes into pockets. Its navigation data uses the lenient movement model, in which a sideways jump may pass through a wall, so it can get stuck against one. In Arcade it cannot leave the start pocket of rounds 16 and 34 (only a bubble bounce does that); the room test skips them there. In Plus it gets out of them by chance, after several tries.
- Should walls stop bubbles inside a vacuum? A developer switch (`F12`) lets both be compared in play.
- The CPU partner dashes but uses neither the push bubble nor the vacuum. Should it?
- Hit zones of the push bubble: most shots hit bubbles at the same height, so most pushes are sideways. Is that fine?
