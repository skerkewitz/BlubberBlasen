# Blubber Blasen – Game Design Guide

Blubber Blasen is a single-screen, two-player co-op platformer for a 256 × 224 screen at 60 Hz, built so it can later run on a Super Nintendo. It started as a remake of Taito's arcade *Bubble Bobble* and does not hide that: the bubble loop, the combos, EXTEND and the hundred rounds come from there. What the original does is described in [Bubble Bobble (Arcade) – Game Design Reference Guide](Bubble%20Bobble%20(Arcade)%20–%20Game%20Design%20Reference%20Guide.md).

The game has grown into something of its own. The Plus mode adds a dash, an energy bar, aiming upward, a push bubble and a vacuum, and these change how a round is played: players move bubbles around the room instead of waiting for the air to bring them. This guide describes the game we are building. The [story](story.md) has the world, the characters and the texts.

Numbers in this guide are the values in the code (`src/game/`) at the time of writing. If they disagree, the code is right and this guide needs fixing.

## Modes

**Plus** is the game. Everything in this guide applies to it unless a section says otherwise.

**Classic** is a switch on the options screen. It turns off the Plus tools (dash, energy, aiming, push bubble, vacuum) and lets fired bubbles pass through inner walls, as in the arcade. The rounds, enemies, items and story stay ours. Classic is close to the original's rules, but it is not the original game: that would also need the original 100 room layouts and the full item system. If we ever want a faithful remake, it is easier to fork this project and build it there than to keep two games in one code base.

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

- 3 lives, up to 9. Extra lives at 30,000 and 100,000 points and for a completed EXTEND.
- After a death the player respawns at their corner and blinks, invulnerable for 2 s. The round goes on.
- Losing a life calms every angry enemy and restarts the round's Hurry Up timer.
- When the last player is out, a 9-second countdown lets them continue with Start; the score is kept. Player 2 can join at any time.

## Bubbles

A fired bubble flies 4 px per frame for 8 frames (about 32 px), then floats. It traps any enemy it touches while flying. A floating bubble drifts with the room's air at 0.35 px per frame (0.30 with an enemy inside). Floating bubbles push apart when closer than 13 px, so they gather into touching clusters where the air meets.

An empty bubble lasts 10 s and turns orange and then red before it bursts. A trapped enemy breaks free after 5 s, with the same warning colours, and comes out angry.

Popping a bubble pops every bubble within 15 px of it, one link every 5 frames, so a chain is visible as it runs through a cluster. All enemies in one chain form one combo. A popped enemy flies through the room in an arc and lands as food.

**Walls and aiming (Plus).** A fired bubble stops at walls and platforms and starts floating in front of them. Blown into a wall at point-blank range, it pops at once. Holding Up shoots a bubble or a push bubble straight up. In Classic, fired bubbles pass through inner walls and Up does not aim.

## The Plus tools

The three tools all answer the same problem: in the arcade, a bubble that drifts out of reach is lost unless the player takes a detour or a risky jump. In Plus the player moves bubbles to where they want the chain.

**Push bubble (X).** A small, fast bubble (6 px per frame, about 72 px) that traps nothing. It bursts on the first floating bubble it meets and shoves it, empty or with an enemy inside. A hit on the upper third pushes the bubble down, on the lower third up, in the middle along the push bubble's direction. The shoved bubble slides at 3 px per frame, ignoring the air, slows down and stops at walls. An empty shoved bubble traps the first enemy it slides into. A push bubble shot upward always shoves its target up, through the platform above. A special bubble or letter that gets hit is set off as if touched. The push bubble also destroys enemy shots, and bursts without effect on enemies, players and the boss. One per player at a time, free.

Play found three uses that later tuning has to keep working:

- Combo shot: a normal bubble followed by a push bubble 3 or more frames later gets shoved on as its flight ends and travels much further. At 1–2 frames the push overtakes the bubble and misses. `test_push_bubble` pins this window.
- Shot defence: a push can destroy an enemy shot, but with only one push bubble at a time a near enemy still gets you while you deal with a far one.
- Bubbles through walls: a shoved bubble crosses platforms, so a bubble can be sent past a wall without jumping into it.

**Vacuum (A).** Puts a vacuum where the player is. For 1.5 s the air inside a circle of 5 tiles (40 px) points to its centre, so floating bubbles (empty, trapped, special, letters, and a partner's rescue bubble) gather there, about 1 px per frame at the edge and 3 near the centre. Then the room's air takes over again. The vacuum stays where it was placed, also when pressed mid-jump; pressing again moves it. Bubbles pass through walls on the way in. It moves no enemies or items, pops nothing, and costs no energy. A pulsing blue glow shows exactly the area that pulls (drawn as a colour window, the way the SNES would do it).

**Dash and energy (L / R).** Each player has an energy bar of 96 on the bottom wall. Defeating an enemy gives 16, plus 8 for each further enemy in the same chain. The bar holds for 3 s after a gain and then drains, so it rewards keeping up the attack. Losing a life empties it.

A dash costs 16 energy and moves the player 54 px (about 7 tiles) in 12 frames without turning them around. It pops every bubble it passes through, goes through inner walls and platforms, never ends inside a wall, and keeps the player invulnerable for the dash and half a second after it. One dash per jump. A press up to 6 frames early still counts. The player sprite shows the bar: a yellow glow with spikes when a dash is possible, cycling colours and a chime when the bar is full.

## Enemies

Every enemy is a factory worker bewitched by the Grumble Boiler (see the story). There are seven types plus the Hurry Up chaser and the boss.

| Type | Name in the story | First round | Movement | Attack |
| --- | --- | --- | --- | --- |
| Walker | Stoker | 1 | Walks, jumps up towards the player, drops off edges | Contact |
| Thrower | Coal Tosser | 4 | Walks like the walker | Stops and throws a rock |
| Bouncer | Steam Puff | 6 | Flies diagonally, ignores platforms, bounces off walls | Contact |
| Flutter | Soot Moth | 21 | Flies through platforms, changes direction often | Contact |
| Hopper | Spring Fitter | 23 | Bounces in arcs on a spring, rebounds off walls | Contact |
| Spitter | Furnace Imp | 25 | Fast walker (1.5 px per frame), weak jumper | Spits fireballs |
| Dropper | Crane Drone | 28 | Patrols left and right, drops off edges | Shoots straight down |
| Chaser | Soot Cloud | Hurry Up | Comes for its player through walls, faster over time | Contact, invincible |
| Boss | Grumble Boiler | 100 | Floats around the top of the room | Spits soot |

Rules all enemies share:

- Each enemy enters after its own delay from the round file.
- An enemy turns angry (red and faster) when it escapes a bubble, when Hurry Up starts, or when it is the last one left.
- Enemies wrap through bottom gaps like players.
- Fairness: every enemy that shoots stops and flashes for 30 frames before the shot leaves, and the shot keeps the direction the enemy faced when the wind-up began. No shot comes without warning, even point-blank. Trapping the enemy during the wind-up stops the shot.

## A round

1. **Intro.** The new room scrolls in from below, 2 px per frame, and the round number and, every ten rounds, the floor name are shown.
2. **Play.** The Hurry Up timer runs. It is set per round, from 40 s in round 1 down to 17 s in round 99. After 5 s a waiting power-up appears on a platform, and after 10 s the round's bonus food (500 to 2,000 points). Rounds with a special bubble type send one in through the opening at a fixed interval.
3. **Hurry Up.** "HURRY UP!" appears and every enemy turns angry. 10 s later a Soot Cloud comes for each living player. Round 100 has no time limit.
4. **Clear.** When the last enemy is gone, players have 5 s to collect the food.
5. **Next round.** The next room scrolls in. A completed EXTEND ends the round at once; an umbrella skips 3 rounds.

## Special bubbles and EXTEND

Special bubbles come in through the room's opening, float with the air and set off their effect when a player touches them or hits them with a push bubble. They last 20 s.

| Bubble | Effect | Enemies hit |
| --- | --- | --- |
| Water | A stream runs down and along the platforms and out through a bottom gap. Carries players without harm | Swept away; all of them count as one combo |
| Fire | A fireball drops onto the platform below and lights a carpet 4 tiles to each side for 3 s. Stuns players for 1 s | Defeated |
| Lightning | A bolt flies straight across the room | Defeated; the only thing that hurts the boss |

A chain of three or more enemies earns EXTEND letters: 1 letter for 3 or 4 enemies, 2 for 5 or 6, 3 for 7 or 8. Up to 6 letters wait in a queue and come in one every 4 s through the opening, preferring letters a player still misses. Whoever pops a letter collects it; the letters are shown on the side walls. All six give an extra life and end the round.

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

Both players share the room, the enemies and the bubbles, and either can pop or bounce on any bubble. Players cannot hurt each other. Score, lives, power-ups, energy and EXTEND letters are per player. A round ends when all enemies are gone, whoever defeated them.

**Co-op rescue** (option). An enemy that touches a player traps them in a rescue bubble for 7 s. It drifts with the air (and a vacuum pulls it), and the partner frees them by touching it, with no life lost. If the time runs out, the player loses a life. Shots, the Soot Cloud and the boss still kill.

**CPU partner** (option). A computer partner plays player 2. It frees the human from rescue bubbles, pops trapped enemies (preferably several at once), hunts enemies and stays near player 1. It looks half a second ahead every frame, trying its own moves (stay, walk, jump, dash, turn and shoot) against where enemies, shots, Soot Clouds and the boss will be. It leaves the food to the human and earns no score. Start on pad 2 takes over from it. A game finished with the CPU partner counts as a two-player game.

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
| Energy | – | Plus: energy bar that pays for the dash |
| Enemy types | 8, introduced every 10 rounds up to round 60 | 7 (no separate bottle thrower), all introduced by round 28 |
| Enemy shots | Can come without warning | 30-frame wind-up before every shot |
| Getting caught | Always costs a life | Option: rescue bubble in co-op |
| Player 2 | Human only | Human or CPU partner |
| EXTEND letters | Only in open rounds; letter types depend on the biggest combo | In any round; the number of letters depends on each chain |
| Power-ups | More than 20, with difficulty-dependent limits | 10 with fixed limits |
| Umbrella | Skips 3, 5 or 7 rounds | Skips 3 |
| Secret doors | Rounds 20, 30, 40; warp in 50 | Rounds 20, 40, 60; warp in 50 |
| Boss | Throws bottles; players blow lightning from round 100 on | Spits soot; lightning bubbles in round 100 |
| Not built | Candy cane, chest, score-digit trick, rings for jumps and walking, necklace, potion, bell, lanterns, clock, book and bombs, the rare fireball bubble, Super mode, difficulty settings | |

## Open questions

- What should a full energy bar mean now that the special shot is gone? It still changes colour and chimes.
- Is the vacuum too strong? Cooldown, limits or an energy cost are still open.
- Should walls stop bubbles inside a vacuum? A developer switch (`F12`) lets both be compared in play.
- The CPU partner dashes but uses neither the push bubble nor the vacuum. Should it?
- Hit zones of the push bubble: most shots hit bubbles at the same height, so most pushes are sideways. Is that fine?
