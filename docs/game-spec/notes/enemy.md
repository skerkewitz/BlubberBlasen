# Raw notes: enemy (unverified, from reading the code of version 1.4.0, 2026-11-22)

Working notes of the reader who wrote part of the spec. Line numbers fit that version only.


Conventions. Positions are sprite top-left, 8.8 fixed point; "px" = integer part (PX()). 256 units = 1 px. Speeds in px/frame (60 frames/s). All numbers below are from the code; `file:line` points at the rule. Enemy sprite 16x16. Screen 256x224; HUD 2 rows (16 px); field y 16..223 (FIELD_Y=16, FIELD_BOTTOM=224). Level 32x26 tiles.
No enemy behaviour in enemy.c reads g_opt (Classic/Plus); see B-section "Modes".

Tick order that matters (game.c:1403-1412, 811-832): pads first (cpu_pad for CPU players) -> player_update for both -> check_door -> (return if nobody alive: enemies, bubbles, chasers freeze) -> bubbles_update (bubbles move, shots catch enemies) -> enemies_update -> chaser_update -> boss_update -> items_update (enemy shots move, thrown enemies, food) -> effects_update -> hurry/clear checks (game.c:877-915).
enemies_update also runs during GS_ROUND_INTRO (game.c:1473), but then only the "entering" part does anything (g_state != GS_PLAY skips AI, enemy.c:542).

=====================================================================
PART A: RULES
=====================================================================

## A1. Enemy pool, spawn, entry

R1. Max 8 enemies at once (MAX_ENEMIES, internal.h:177). Slot = first inactive index (enemy.c:68-90). If no slot is free, the spawn silently does not happen (level spawn stops at the first failure, enemy.c:100-102; escape spawn simply loses the enemy, enemy.c:113-122).
R2. New enemy defaults (enemy.c:73-85): not angry, not entering, spawn_delay 0, no jump, moving_down=1 (down), throw_timer = 60 (THROW_COOLDOWN>>1), no pose, vy 0, stun 0.
R3. Level enemies (enemy.c:93-111): for each LevelEnemy in file order: facing left if flag bit 0; target x = tile column*8 (px); target y = 16 + tile_row*8 - 8 (the map y is the tile row of the feet, sprite top is one tile above that, as sprite is 16 tall -> see B). Start position x = target x, y = 16 (top of the field), `entering=1`, spawn_delay = the map's delay (0..255 frames).
R4. Entering (enemy.c:530-541), done before the freeze/state test, so it also runs in round intro and while frozen: first spawn_delay counts down 1/frame; the enemy is not drawn until the delay is 0 (enemy.c:576). Then it moves 1 px per frame on each axis towards the target (x first call, y second; since x already equals target, it simply drops 1 px/frame) until both equal; then `entering=0`. No collision while entering, no AI.
R5. An entering enemy (including one still waiting its delay) is: active (counts in enemies_count, so the round cannot end, enemy.c:134), not touchable by players (enemy.c:237), not trappable by bubbles (bubble.c:298), not hit by effects (effect.c:125), not hit by item gems (enemy.c:157, survives Soap Star), not a CPU target (cpu.c:277).
R6. The round intro (GS_ROUND_INTRO) ends only after >=120 frames AND no player entering AND no enemy entering (game.c:1475), so every spawn delay and the drop-in finishes before play starts. Max wait = 255 frames delay + drop.
R7. enemies_entering() = any active enemy with entering=1 (enemy.c:124). The "last enemy gets angry" rule waits while any enemy is entering (game.c:891).
R8. enemies_clear (enemy.c:59-66): all inactive, think_timer=0, chasers reset. Called at round start (game.c:511,585) and game start.

## A2. Speeds (enemy.c:26-48, 263-268)

R9. Horizontal walking speed (px/frame), normal -> angry (angry = base + 77/256 = +0.30):
 - Walker, Thrower: 1.00 -> 1.30  (SPEED, ANGRY_EXTRA; SPEED_ANGRY macro is defined (1.30) but not used)
 - Spitter: 1.50 -> 1.80   (SPEED_SPITTER FX(1,128))
 - Dropper: 0.75 -> 1.05   (FX(0,192))
 - Shield: 0.625 -> 0.926  (FX(0,160))
 - Hopper sideways: 0.797 -> 1.098 (FX(0,204)+77)
 - Bouncer, Flutter: 0.75 on both axes -> 1.00 on both axes (FLY_SPEED FX(0,192), FLY_SPEED_ANGRY FX(1,0))
R10. Angry never changes: jump height/speed (1 px/frame up), fall speed (1 px/frame), shot timing, think chances, hopper jump power. Angry changes the sprite: Walker uses its own angry sprites (SPR_E_ANGRY0), all other types are tinted red (enemy.c:606-614); walking animation flips every 8 frames instead of 16 (enemy.c:574).
R11. An enemy turns angry when: Hurry Up starts (all enemies, game.c:883), it is the last enemy alive and no trapped bubbles exist and none is entering (game.c:891, tested every frame), it escapes from a bubble (always, bubble.c:619, even if it was calm when trapped). Angry ends only when a player loses a life (game.c:727, enemies_calm_down, enemy.c:207) or the round ends (new enemies are calm). Angry persists through trapping: a trapped angry enemy shows a red inner sprite (bubble.c:308,668) and comes out angry anyway.

## A3. Walker (type 1), Thrower (3), Spitter (6), Dropper (7), Shield (8): walking logic `think_walker` (enemy.c:370-432)

All five share one routine; capabilities per type (enemy.c:378-379):
 - can climb (jump up one platform): Walker, Thrower only.
 - can gap-jump (jump across a hole): all except Dropper and Shield (so Spitter can gap-jump but not climb).
 - shooters: Thrower, Spitter, Dropper.
R12. Order of work in one tick for one such enemy: (1) if shooter and not mid-jump: try_shoot; if it returns "standing in pose", the tick ends (no move, no facing update, no gravity); (2) dropper: walk_over_edge=1; (3) mid-jump rise OR on-ground logic OR falling; (4) facing = dir; (5) clamp x to room; (6) wrap y.
R13. Player used for the decisions is the NEAREST living player by |dx|+|dy| in px (player_nearest, player.c:294); ties -> lowest index; if no player is alive -> player 0 (player.c:296).
R14. "Player above" = enemy y > player y + 16 (player at least 17 px higher). "Player below" = enemy y < player y - 32 (player more than 32 px lower). (enemy.c:375-376; px values, signed.)
R15. Walking on ground (enemy.c:394-420), every tick:
  a. gap_jump flag cleared.
  b. Climb check: only on a "think tick" (see R16), only for Walker/Thrower, only if player above AND a platform exactly 5 tiles up is reachable (R17) AND random chance(102) = 102/256 = 39.8% -> start a vertical jump of 44 px (jump_left=44). The tick that starts the jump does nothing else (no step, no rise).
  c. Else: if a gap is ahead (R18): roll to walk off the edge, else gap-jump or turn (R19). Then if a wall is ahead (in the possibly reversed direction): reverse and clear walk_over_edge. Then take one step of walk speed in `dir` unless a wall is directly ahead (step_x, enemy.c:270).
R16. Think tick: a single global counter `think_timer` counts calls of enemies_update; every 120th call (think_timer reaches 120 -> reset to 0) is a think tick for ALL enemies at once (enemy.c:520-524). It counts during round intro, freeze and all enemies_update calls; it does not count while paused, while all players are dying, or in other game states. Reset to 0 at round start. So climbing decisions happen only once every 120 frames per enemy, with 39.8% success when conditions hold.
R17. Can-jump-up (level.c:141-156): column = (x+8)/8; feet tile row ty = (y+16-16)/8; needs ty-5 >= 1; tiles ty-1..ty-4 all empty (non-solid) in that column; tile ty-5 solid and tile ty-6 not solid. So only a platform exactly 5 rows above with 2 free rows over it (i.e. head clearance 4 rows below it; 1 row of platform thick, nothing above). The jump is straight up, 44 px at 1 px/frame (44 frames), ignores ceilings/platforms above (one-way), then the enemy falls 1 px/frame until it stands (the platform is 40 px up, so it lands after ~4 more frames).
R18. Gap ahead (level.c:158-162): the pixel under the leading foot (x+1 going left, x+14 going right) at y+16 is not ground (ground = solid, except the top border row counts only at the side walls, level.c:40-47).
R19. At a gap (enemy.c:402-414), in this order:
  1. walk_over_edge=1 if (player_below AND chance(204)=79.7%) OR chance(26)=10.2% (second roll only if the first part was false/not evaluated). Without player_below: 10.2%. With player below: 1-(1-.797)(1-.102) = 81.8%.
  2. If walk_over_edge is (now) set: keep walking, falls off.
  3. Else if the type can gap-jump AND level_can_gap_jump AND chance(64)=25%: jump 24 px up (jump_left=24, gap_jump=1) while also stepping sideways. The starting tick still goes on to the wall check and the step_x (so it steps once sideways this tick without rising).
  4. Else reverse direction (dir=-dir), same tick, then wall check on the new direction, then step.
  So at a ledge with a possible target: 10% walk off, then (90%) x 25% = 22.5% jump, 67.5% turn around (base case, no player far below).
R20. level_can_gap_jump (level.c:165-180): starting at the tile column of the leading foot, looks up to 4 columns ahead: for each column, if the tile at (feet row - 1) is solid -> no; else if the tile at the feet row is solid -> yes; else next column. Never true if nothing within 4 columns.
R21. Gap-jump flight: while rising (24 frames, 1 px/frame up) it moves sideways each frame at walk speed (step_x, blocked by walls but does not turn); after the rise it falls 1 px/frame and keeps moving sideways at walk speed until it lands (gap_jump flag cleared when on ground). Total flight about 48 frames = 48 px sideways for a Walker (72 px for Spitter 1.5 px/frame).
R22. Walking off an edge: the enemy keeps walking at walk speed until neither foot (x+4 and x+11) has ground under it, then falls straight down at 1 px/frame with NO sideways motion (gap_jump=0) (enemy.c:421-427). Falling into a bottom gap wraps to the top of the screen (level_fall/level_wrap_y, level.c:80-109), then falls on.
R23. Falling: 1 px/frame constant (no acceleration), level_fall stops at the first ground (landing at integer pixel).
R24. Wall ahead (level.c:113-121): the pixel just beyond the body edge at feet height (x+2 / x+13 +/-1) is solid while the edge pixel itself is not. Reverse direction, walk_over_edge=0. Room side walls count as solid; x is also clamped so that the body centre stays within 22..234 (level.c:123-131).
R25. Dropper (enemy.c:384-386, 396-398): walk_over_edge is forced to 1 every tick, so it never turns at ledges and never jumps; it only turns at walls (the flag is re-set next tick). So it walks off every edge; at the bottom it wraps.
R26. Shield (type 8): walks at 0.625, cannot climb, cannot gap-jump; at a ledge: 10.2% (81.8% with the player far below) walks off, else turns (R19.1/4).
R27. Spitter (type 6): walks at 1.5, cannot climb, can gap-jump (R19.3), shoots (R30).
R28. Thrower (type 3): moves exactly like the Walker (climb, gap-jump) and shoots (R29).
R29. A Walker (type 1) never shoots (a de-plated Shield becomes a Walker, see R45).

## A4. Shooting (enemy.c:311-368)

R30. try_shoot is called every tick for shooter types while not mid-jump (enemy.c:381). Sequence per tick:
  1. If in the shooting pose (throw_pose>0): throw_pose--; when it reaches 8 (ENEMY_RECOVER) the shot is released that tick; return "standing". The enemy does not move, fall or turn. Pose starts at 38 (ENEMY_POSE = 30 wind-up + 8 recover). Counting the starting tick, the enemy stands 39 ticks in all: the shot leaves on the 31st of them (30 frames after the start tick), then it stands 8 more ticks.
  2. Else if throw_timer>0: throw_timer-- and no shot (timer runs down in any state: walking, falling; but not while in the pose, not while mid-jump).
  3. Else, only if standing on ground: look for a target (R31). If there is none, nothing happens (timer stays 0, checked again next tick, no random number drawn).
  4. With a target: throw_pose=38, throw_timer reloaded = Thrower 120 + rand(0..127); Spitter 90 + rand(0..63); Dropper 80 + rand(0..63) (one rng_next() draw per wind-up; the reload happens at the START of the wind-up but the timer only counts down after the pose ends). Return "standing".
R31. Target: Thrower and Spitter: any LIVING player (any, not just the nearest) with |dy| <= 7 px (dy in -7..7) and strictly in front (dx<0 when facing left, dx>0 when facing right; dx=0 does not count), at any distance, walls ignored (player_in_line, enemy.c:279-293). Dropper: any living player with y >= enemy y + 9 (player y > enemy y + 8) and |dx| <= 11 (dx in -11..11) (player_below, enemy.c:296-309), facing ignored, platforms ignored.
R32. Direction is fixed when the wind-up starts: facing_left is not changed during the pose (the function returns before the facing update), and `dir` used for the shot is the facing at release (same value).
R33. Shots (enemy.c:324-337, item.c:93-101): Thrower: rock at the enemy's x,y, flies 2.0 px/frame in facing direction. Spitter: fireball starts 6 px in front of the enemy (x -/+ 6), 2.5 px/frame. Dropper: shot starts 8 px below the enemy (y+8), same x, flies straight down 2.5 px/frame. If the 16-slot item table is full, the shot is lost (alloc_item).
R34. Shot lifetime: sideways rock/spit disappears (small burst) when body centre leaves 22..234 or the pixel at its centre (x+8,y+8) is solid (item.c:214-220), so shots stop at any wall or platform tile in their path; they fly through nothing else. Dropper shot ends at y >= 200 (FIELD_BOTTOM-8-16) (item.c:207-212) and does not stop at platforms. Any shot kills a player on touch with |dx|<8 and |dy|<8 (top-left difference) and is TOUCH_DEADLY: it kills even if co-op rescue is on (enemy.c:222-228, 214-215). Push bubbles (Plus) destroy them (bubble.c, outside this area).
R35. Warning: during the wind-up (frames pose 38..9: enemy_shot_in = pose-8 > 0) the sprite flashes yellow every 4 frames ((g_frame>>2)&1, enemy.c:610); the thrower shows its throw sprite for the whole pose incl. recovery (enemy.c:584). Grey (frozen/stunned) beats yellow, yellow beats red (enemy.c:608-614).

## A5. Bouncer (type 2) and Flutter (type 4): `think_flyer` (enemy.c:436-476)

R36. They move in straight lines through everything (platforms, walls inside the room); only the room's outer edges count. Speed 0.75 on each axis (angry 1.00). They never shoot, never fall, never use gravity. Movement per tick: x += speed in facing direction; y += speed if moving_down==1 (down), y -= speed if moving_down==0 (up), no vertical movement if 2 (level).
R37. Bouncer: starts with moving_down=1 (down) and the facing from the map: a diagonal down. It never changes direction on its own, only by bouncing.
R38. Room edges (enemy.c:461-475): left: body centre (x+8) < 22 (or x wrapped >= 240) -> facing right, x = 14. Right: centre > 234 -> facing left, x = 226. Top: y < 24 (or wrapped >= 224) -> moving_down=1, y=24. Bottom: y > 200 -> moving_down=0, y=200. The sprite therefore lives in x 14..226, y 24..200 (top-left). Corner hits do both flips in the same tick.
R39. Flutter: throw_timer is its "turn timer". It starts at 60 (R2). Each tick: if timer>0 then timer-- ; else it draws ONE random number r: facing_left = r&1 (50/50); vertical = (r>>1)&3 -> 0 up (25%), 1 down (25%), 2 or 3 -> level (50%, 3 is remapped to 2); timer = 32 + ((r>>3)&63) = 32..95. The new direction applies in the same tick. So a heading lasts timer+1 = 33..96 frames; the first change is after 61 frames. Screen-edge bounces can overwrite the chosen vertical state at once.
R40. Flutter animation 2 frames, every 4 frames, independent of angry (enemy.c:588).

## A6. Hopper (type 5): `think_hopper` (enemy.c:479-514)

R41. Per tick:
  1. If vy >= 0 and standing on ground: hop. vy = -(2.5 + rand(0..127)/256) px/frame, i.e. -2.50 .. -2.996 (one rng_next draw, `& 127`). Then chance(128) (50%, second draw): turn to face the nearest living player (facing_left = player x < enemy x; equal x -> face right).
  2. Sideways: if a wall is directly ahead -> only flips facing (no move this tick). Else move 0.797 px (angry 1.098), clamp to the room (centre 22..234); if x did not change after the clamp -> flip facing.
  3. vy += 0.109 (28/256) gravity, capped at +2.5 downward.
  4. If vy < 0: y += vy with NO collision (flies up through platforms); if y < 24 (or wrapped) -> y = 24, vy=0 (bumps the ceiling). Else (vy >= 0): level_fall by vy; landing sets vy=0.
R42. So it hops again on the very next tick after landing (no pause), and it moves sideways also while standing for that one tick. Hop apex about 2.5^2/(2*0.109) = 29 px up to 2.996^2/0.218 = 41 px. It falls through the bottom gaps and wraps like a walker. Hopper uses no think-tick decisions and does not look at gaps/edges.
R43. Hopper animation: crouched tile when vy == 0 or vy > 1.5, else stretched tile (enemy.c:591).

## A7. Shield enemy specifics (Riveter, type 8)

R44. Plate side = facing side. A bubble (shot or shoved) overlapping the enemy (12x12 top-left test) that is "in front" bursts on the plate and is gone: in front = enemy facing left: bubble x (px) < enemy x (px); facing right: bubble x (px) > enemy x (px) (strict; enemy.c:184-190). Burst: small pop effect + pop sound, no trap, no score. From behind or exactly equal px x: it traps normally (bubble.c:298-315).
R45. A dash (Plus mode, player dashing) overlapping a Shield enemy with |dx|<14 and |dy|<14, from either side, knocks the plate off (enemy.c:193-205): type becomes Walker (permanently, also if it is later trapped and escapes: it returns as a Walker), stun=60 frames, jump_left=0, pop effect + trap sound. It is checked every frame during the dash and also on the dash start frame (player.c:538-539).
R46. Stunned (enemy.c:545-552): for 60 frames it neither walks nor shoots; if not on ground it falls 1 px/frame (and wraps); it blinks grey (g_frame&4, i.e. 4 frames on/4 off). It is still dangerous (contact still kills / rescue-traps) and can still be trapped. Stun is checked before the AI (after the entering check and after the freeze check).
R47. Effects (water, fire, lightning, glove) hit a Shield enemy like any other (the plate does not matter); only bubbles care about the plate.

## A8. Trapping, escaping, popping

R48. A shot bubble (BS_SHOOT) traps the first active non-entering enemy (lowest slot) whose top-left is within |dx|<12 and |dy|<12 of the bubble's (bubble.c:294-319), unless a Shield plate blocks (R44). The enemy slot is freed immediately (active=0), the bubble becomes a trapped bubble keeping type and angry flag, age=0, floats up (air flow, 0.30 px/frame) and a trap sound plays. The first-found enemy wins, not the nearest.
R49. Escape (bubble.c:23, 617-621): when the trapped bubble's age reaches 300 frames (5 s) the enemy comes back at the bubble's position: same type, ALWAYS angry, facing left/right 50/50 random (one rng_next()&1 draw), all other state default (R2: throw_timer 60, no jump, not entering, no stun); a pop effect shows. If no enemy slot is free the enemy is lost. Warnings: orange from age 120, red from age 210 (bubble.c:24-25).
R50. Popping a trapped bubble (by a player touching it, or by chain reaction; bubble.c:244-253): the enemy is thrown out as food: tier = min(chain_kills, 6) -> value 1000<<tier = 1,000, 2,000, 4,000, 8,000, 16,000, 32,000, 64,000 for the 1st..7th enemy of one chain (chain_kills counts per chain). Direction: first of a chain = the pop direction (chain_dir), the others random (rng_next()&1). Thrown speed x = 1.5 + rand(0..127)/256 (=1.5..1.996), y = -(3.0 + rand(0..127)/256); gravity 0.094/frame, max fall 2.0; at least 40 frames in the air before it can land; then food for 600 frames (item.c:10-19,223-257). Order of random draws in a pop: direction (only if chain_kills>0), then vx, then vy. The kill counts for the Steam Whistle counter (item.c:71). 3+ kills in a chain = multikill (outside this area).
R51. Defeated by an effect (water, fire, lightning, fire glove): sweep_enemies (effect.c:121-142), the enemy is thrown as food of tier = effect combo (glove: gem tier 3).
R52. Soap Star / Rinse Wave (enemies_to_gems, enemy.c:152-164): every active, non-entering enemy turns into a thrown 8,000-point gem (tier 3); direction alternates by slot parity (odd slot -> left, even -> right). Entering enemies are not affected. Returns the number.
R53. Steam Whistle (g_freeze): while frozen the AI of all non-entering enemies does not run (enemy.c:542), they are drawn grey, and their body does not hurt (enemy.c:235). A player overlapping a frozen enemy (|dx|<12,|dy|<12) defeats it: thrown as tier 2 = 4,000 points (GEM_TIER-1, enemy.c:174), away from the player (enemy x < player x -> thrown left, otherwise right incl. equal). Shots, the chaser and the boss stay deadly and keep moving while frozen; the think counter and entering continue.

## A9. Contact with players (enemy.c:216-243)

R54. Checked in this order, first hit wins: boss (deadly), any enemy shot item (8x8, deadly), any chaser (10x10, deadly), any enemy body (|dx|<10, |dy|<12 of top-lefts; "TOUCH_ENEMY"). Frozen enemies are skipped in the body loop only.
R55. TOUCH_ENEMY with the co-op rescue option on and the other player not OFF: the player is put in a rescue bubble (7 s) instead of dying (player.c:227-231). Everything else kills. Invulnerable players (respawn, dash grace) are skipped before the call (player.c:524,550).

## A10. Hurry Up and the Chaser

R56. Hurry Up (game.c:877-885): when g_level_frames >= the round's hurry_frames (0 = never) and at least one enemy (entering ones count) is active: g_hurry=1, "HURRY UP!" text blinks for 120 frames (every 8 frames), every enemy angry, sound. The frame counter counts only GS_PLAY frames of the round, resets to 0 when a player loses a life (game.c:730), which also clears Hurry Up, calms all enemies, removes the chasers and all shots.
R57. Chaser spawn (game.c:886; enemy.c:647-657): 600 frames (CHASER_DELAY) after the Hurry Up limit, if hurry is on, no chaser is active, the room-clear timer is not running and enemies remain: for each LIVING player a chaser appears at y=16 (top of field), x = 104 for player 1, 136 for player 2, speed 0.375 px/frame. Sound again. A player who is dead/entering at that moment gets none; later no further chaser is spawned while any chaser is active.
R58. Chaser move (enemy.c:659-689): each axis independently: if the chaser's px is more than 1 px away from the player's px (|diff| >= 2) it moves a full `speed` towards it, else that axis stays (dead zone +-1 px). So diagonal movement is faster than straight (up to sqrt2 x speed). No collision with walls/platforms, ignores freeze. Speed rises by 4/256 every time (g_frame & 31) == 0 (about every 32 frames, global frame clock), capped at 2.0 px/frame (reached after 104 steps = 3,328 frames, about 55 s). If its player is dying/entering/in rescue it stays put; if its player left the game (OFF) the chaser disappears (enemy.c:676-679).
R59. The chaser is invincible (nothing traps it), deadly on contact (10x10) even with rescue on; it sprite-flips to face away when x > player x (enemy.c:696); animation 2 frames every 8 frames. Removed at round clear (game.c:914) and on a lost life.

## A11. CPU partner (cpu.c). Header comment cpu.c:1-28; the CPU is only a joypad word source.

R60. Who: any player with `cpu` set; normally player 2 when the options say P2 = CPU (game.c:662-676); it joins with player 1; it earns no high-score entry (game.c:672-674). Start on pad 2 hands player 2 to a human (game.c:1405-1411). Its pad is computed at the start of every tick (game.c:1412), before player updates, and equals a controller word: Y shoots (edge needed), B jumps, L/R dash, d-pad. It returns 0 (no input) when the game is not in GS_PLAY or the CPU's player is not PS_ALIVE (dying, in a rescue bubble, entering) (cpu.c:696-699).
R61. Decision rhythm (cpu.c:717-722): a new goal pad is chosen every 7th frame (think is set to 6 and counts down to 0, then decides again: decision frames 0, 7, 14, ...), and the chosen pad is held in between. Danger avoidance (R70+) runs EVERY frame on top of the held pad.
R62. Goal priority list in decide() (cpu.c:373-432), first match wins:
  1. Partner in rescue bubble (state PS_RESCUE): walk to the partner (move_to its x,y). It frees it by overlapping (12x12 in player.c:204).
  2. (Danger: not a goal; every frame, R70.)
  3. Pop a bubble (R63).
  4. Hunt: if any active, non-entering enemy exists, the CPU always returns the hunt result (R64) - goals 5, 6, 7 are only reached when NO such enemy exists.
  5. Take a BLASEN letter bubble it misses (R66).
  6. Collect food/bonus/power-up (R67), only if the other player is OFF or also a CPU.
  7. Follow: if the other player is alive and |dx|+|dy| > 48 px, walk to it (move_to); else do nothing (pad 0).
R63. Pop goal (bubble_to_pop, cpu.c:291-317): candidates = trapped bubbles (state TRAPPED) and, in the boss round only, floating lightning bubbles. Urgent = a trapped bubble with age > 180 (the last 120 frames before it escapes); if several are urgent the LAST one in slot order wins (not the nearest). If there is an urgent one, go there. Otherwise go to the nearest candidate (Manhattan px of top-lefts, ties -> lower slot) only if 2 or more candidates exist, or it is the boss round, or no enemies are left on the field (enemies_count()==0, entering ones count as enemies). A lone trapped enemy with other enemies still around is left alone until 180 frames old. Popping = walking into the bubble from below/side (player touch), the CPU does not use any special action; for a bubble above it presses B when within 13 px in x (R69.2).
R64. Hunt (cpu.c:388-410): target = nearest active non-entering enemy by |dx|+|dy| (ties -> lower slot). If |dy| < 12 (same level), let `left` = enemy x < CPU x (equal -> right):
  - |dx| < 18 (KEEP_DIST) and the CPU faces away: press away from the enemy (retreat, still facing away).
  - |dx| < 40 (60 with the range power-up): if facing away -> press towards the enemy (turn); if facing it -> press Y on alternating decisions (fire toggles every decision, Y only when fire=1, and then with probability 7/8 (rng_next()&7 != 0)); otherwise nothing (pad 0) = stands still.
  - else (too far): fall through to move_to(enemy).
  Different level: move_to(enemy x,y). Because the CPU faces towards the enemy and shoots from at most 40 px away, the shot (flies 8 frames x 4 px + 8) hits. Y is pressed in a 7-frame window only every second decision (every 14 frames) and the player's shoot cooldown is 15 frames, so the CPU fires at most about once per 15-21 frames.
R65. It does not check the enemy type: it shoots Shield enemies from the front (bubble bursts) and does not wait for flyers to come level.
R66. Letter goal (cpu.c:320-336): nearest floating letter bubble whose letter the player does not have. Walk to it and touch/pop it. Letters it already has are never targeted.
R67. Food goal (cpu.c:355-371, 418-424): nearest item of kind food, bonus food or power-up (not thrown enemies in flight). Only if the other player is OFF or a CPU. If the human is alive, dying, or entering, the CPU leaves all food.
R68. Navigation `move_to` (cpu.c:225-268), uses the level's segment graph (NavSeg: row, x0..x1 columns, edges):
  - Column c = (x+4)/8; row r = (y+15-16)/8 clamped >= 0.
  - In the air (not on ground) or the level has no nav data: just steer sideways towards the target x, dead zone 2 px (no jumping).
  - Current segment = first segment of the table with the same row and c in [x0-1, x1+1]. Target segment = segment minimising cost: dy=seg.row - target row; cost = dy+dx if dy>=0 (at/below the target), else 40+|dy|+dx; dx = column distance to the segment's span; ties -> first in table.
  - Same segment (or none found): steer sideways, dead zone 3 px; and if the target is more than 10 px above the CPU and |dx| < 14 px, also press B (jump into a bubble above).
  - Different segments: breadth-first search over edges for the first hop. No path: steer sideways (dead zone 3) and press B on frames where (g_frame & 63) == 0.
  - Path: want = current column clamped into the hop segment's columns. If the hop goes UP (hop row < current row): steer to column `want` (dead zone 4 px), and press B whenever |c - want| <= 2 columns. Else if want == c (the hop segment is below/beside and already under us): walk off the nearer edge: LEFT if ((hop segment midpoint column < c) OR (current segment's x1 >= 28)) AND current x0 > 2, else RIGHT. Else steer towards `want` (dead zone 2 px).
  - It never presses B for sideways gap jumps, only for up-hops, a target overhead, no-path pulses, and unstick.
R69. Sub-rules: 2. "target hangs above": `PX(ty) < PX(py) - 10 && |dx| < 14` -> add B (cpu.c:240).
R70. Danger check, every frame (dodge, cpu.c:623-661; predict_dangers, cpu.c:513-550). Dangers (max 32; extras dropped, enemies first, then shots, chasers, boss): every active non-entering enemy (none at all while frozen), the shot an enemy is winding up for (R71), every enemy shot item (IT_ROCK incl. boss soot) with its real velocity, chasers (reach +2), the boss (centre minus 8, reach +10). Enemy, chaser and boss velocity = position change since the previous frame, set to 0 if any component exceeds 3 px/frame (wrap-arounds, new slot); predicted linearly (no bounces, no gravity) to t = 4, 8, 14, 20, 26 frames (SAMPLE_0..4).
R71. A shooter in its wind-up counts also with its coming shot: before the shot leaves (t < frames-until-release) the shot is "far away" (-1000); from then it flies along its line (thrower rock 2.0, spit 2.5 px/frame from 6 px in front, dropper shot 2.5 px/frame down from 8 px below) (cpu.c:469-509).
R72. Evaluation of an action (evaluate, cpu.c:556-593): the CPU is predicted at each sample: walking = x +/- walk_px (0.918 px/frame*t: 3, 7, 12, 18, 23 px at t=4, 8, 14, 20, 26), jump = also y - rise (9, 18, 28, 35, 40 px; jump only counts if on ground), dash = invulnerable, only the LAST sample counts, at x +/- 54 px. x clamped to 14..226. Room = max(|dx|,|dy|) of top-lefts - 10 - reach + t/2 (t/2 = 2, 4, 7, 10, 13 slack for far samples). The action's score = the minimum room over samples and dangers. Safe means score >= 4 (SAFE_DIST). With no dangers everything is safe.
  For a standing enemy of reach 0 this demands Chebyshev distance >= 12, 10, 7, 4, 1 px at the five samples.
R73. dodge (cpu.c:623-661): 
  1. If p->invulnerable > 20 (just respawned/freed) -> take the goal as is.
  2. If the goal contains Y and the shoot cooldown is 0 (a shot goes off now) -> take the goal as is (no check at all).
  3. Evaluate the goal's action with all 5 samples (only the first 2 (t=4, 8) if the goal has Y but the cooldown is not 0). Safe (>= 4) or no dangers -> goal as is.
  4. Else "shoot back": if shoot cooldown is 0, Y was not in last frame's output, no freeze, and some enemy (first in slot order, not nearest) is at |dy| < 12 and |dx| < 40 (60 with range): output Y + direction towards it (turns and shoots in the same frame).
  5. Else re-evaluate the goal action over 5 samples and try all 8 actions in the order STAY, LEFT, RIGHT, JUMP, JUMP+LEFT, JUMP+RIGHT, DASH LEFT, DASH RIGHT: jumps only when on ground, dashes only if the CPU can dash now (Plus mode, alive, not carried, not dashing, none used in this jump, no cooldown) and not if the dash would sweep through a floating letter bubble the player already owns. An action replaces the best only if STRICTLY larger. Result = that action's pad | the goal's Y bit.
R74. After dodge, in cpu_pad (cpu.c:727-729): if the CPU is in the air and rising (vy<0), B is forced held (jumps are never cut short).
R75. Unstick (cpu.c:700-715): if position (x and y, full 8.8) did not change since last frame while the goal pad holds LEFT or RIGHT, a counter rises; at 90 consecutive frames the CPU does a 30-frame "get unstuck" move: it presses the opposite direction of its goal (goal had LEFT -> RIGHT; else LEFT), with B held for the first 14 of the 30 frames. During it, no goal choice and no danger check happen (returned early).
R76. What it never does: use X (push bubble), A (vacuum), aim up, press Start, collect food while a human partner is alive, take letters it already has, dash through such letters, hunt entering enemies, plan sideways gap jumps, know about shield plates, water currents/streams, fire or other special bubbles (except lightning in the boss round), or the Soap/Whistle items beyond treating power-ups as food.

=====================================================================
PART B: HIDDEN DECISIONS
=====================================================================

Walker AI
H1. think_walker order: shoot check -> (dropper flag) -> jump continues -> on ground -> else fall; the mid-jump branch comes before the ground test, so a jumping enemy never shoots and its shot timer is frozen (enemy.c:381,388).
H2. In the tick a jump starts (vertical or gap), the enemy does not rise; a vertical jump also does not step sideways (enemy.c:399-400), but a gap jump DOES step sideways in that same tick (enemy.c:407-419: falls through to the wall check and step_x).
H3. chance() = (rng_next() & 0xFF) < p; probabilities 102/256 = 39.8%, 204/256 = 79.7%, 26/256 = 10.2%, 64/256 = 25.0% (enemy.c:399,403,407; game.c:150).
H4. Short-circuit order controls how many random numbers are drawn: climb chance only if think tick AND player above AND can_jump_up (enemy.c:399); edge roll draws chance(204) only if player_below, then chance(26) only if the first was false (enemy.c:403); gap chance(64) only if can_gap_jump and can_gap_jump() (enemy.c:407). The edge roll is repeated EVERY tick while a gap is ahead, also when the flag is already set (consumes randoms for nothing) (enemy.c:402-405).
H5. walk_over_edge is only cleared by: turning at a wall (enemy.c:417), being airborne (enemy.c:422), not otherwise. So once it was set by the roll it stays set; the Dropper sets it every tick (enemy.c:385,397).
H6. When the enemy reverses because of a gap, the wall check runs in the NEW direction in the same tick (enemy.c:411,415), and the step goes in the new direction.
H7. A turn at a ledge happens when the leading foot pixel (x+1 / x+14) is over empty; but the walk-off continues until both foot pixels (x+4, x+11) are unsupported (about 10 px past the edge) before it falls (level.c:68-78, 160).
H8. A fall after a plain walk-off has NO horizontal movement; only gap-jump falls keep walking (enemy.c:422-426). An enemy released from a bubble inside a platform falls straight down 1 px/frame until it finds ground (no push-out).
H9. Fall speed and jump rise are fixed 1 px/frame and unaffected by Hurry/angry (enemy.c:390,426).
H10. A vertical jump is 44 px and straight up; the target platform is exactly 40 px above, so the enemy overshoots by 4 px and then falls back onto it (enemy.c:30, level.c:155). Gap jump height 24 px (enemy.c:31).
H11. Climb check needs `think_now`, which is global: every enemy decides on the same frame, every 120th enemies_update call (enemy.c:520-524). The counter also runs through round intro and Steam Whistle freeze; it is reset only by enemies_clear.
H12. "Player above/below" use the nearest living player by Manhattan distance; if nobody is alive player 0's position is used (player.c:296). Two different thresholds: above is +16 px, below is -32 px (asymmetric) (enemy.c:375-376).
H13. Spitter has gap-jump but no climb; Dropper and Shield have neither. Only Walker/Thrower can climb (enemy.c:378-379). The map header comment in level_data.h calls the spitter's attack "while running", but the code stops it for the wind-up.
H14. y is wrapped after walking/jumping (enemy.c:431) and facing is written from `dir` at the end (enemy.c:429): a reversal sticks.
H15. SPEED_ANGRY (enemy.c:27) is dead code; angry speed is base + ANGRY_EXTRA, which gives the same 1.30 for the Walker.

Shooting
H16. Pose is 38 ticks (30 + 8), but the stand time is 39 ticks since the starting tick also stands; the shot leaves when pose hits 8 (enemy.c:347, internal.h:183-185).
H17. The re-fire cooldown is loaded at the START of the wind-up but only counts after the pose (the pose branch returns first), so the real gap between two shots is 39 + cooldown + (>=1) frames: thrower 160..287, spitter 130..193, dropper 120..183. The first shot of an enemy can come after 60 frames of timer (R2).
H18. Cooldown timer counts down while walking AND while falling AND while standing without target, but not during the pose or a jump (enemy.c:352-355).
H19. Shooters shoot only from the ground (enemy.c:356), but once the timer ran out and the player is in line while it is airborne, it waits until it lands; no random draw while there is no target.
H20. Line of fire: any living player counts (not the nearest), +/-7 px vertical, any distance, ignoring walls; dx = 0 does not count; the enemy never turns to face a player, so a player behind it is safe (enemy.c:279-293). Dropper ignores facing, needs the player >= 9 px lower and within +/-11 px of x (enemy.c:296-309).
H21. rng use per wind-up: exactly one rng_next(); the ternary evaluates only the branch of its type (enemy.c:364-366).
H22. The Dropper shot has no `dir`; Spitter fire starts 6 px in front; the Thrower rock starts at the enemy's own position (enemy.c:327-335).
H23. Sideways shots die on any solid pixel at their centre (item.c:216-217), the Dropper shot ignores platforms and ends at y >= 200 (item.c:209).
H24. Shot hit box against a player is 8x8 on top-left difference (< 8), smaller than a body (10x12); shot hits are always lethal and ignore co-op rescue (enemy.c:222-228).
H25. Wind-up flash colour priority: grey (frozen / stunned blink) > yellow (wind-up) > red (angry) (enemy.c:608-614). The thrower's throw sprite stays on until the end of recovery, but the yellow flash stops when recovery starts (enemy_shot_in==0 for pose<=8).

Entry / spawn
H26. Map y is the tile row of the feet but target_y = 16 + row*8 - 8 (so the sprite top = field top + row*8 - 8) (enemy.c:105). Enemies start at y=16 even if their target is the first row; target above 16 is impossible.
H27. x/y entry steps are 1 px per axis per tick, x first then y, `step_towards` works on integer px and sets fractions to 0 (enemy.c:245-253). Entry ignores walls.
H28. Spawn delay is counted down in all game states that call enemies_update, including the round intro, which only ends when nobody is entering - so the delays are effectively intro staggering (enemy.c:531, game.c:1475). An enemy with delay 255 delays the start of play by up to 255 frames.
H29. Facing at spawn comes from the map flag only; Bouncer/Flutter also start with moving_down=1 (alloc default), so every bouncer starts heading down (enemy.c:81).
H30. If the level lists more than 8 enemies the rest are dropped without notice (enemy.c:100).
H31. enemies_make_angry sets angry on all 8 slots including inactive ones (enemy.c:145); harmless because alloc_enemy resets it.
H32. Escape: enemy_spawn does not reset the position into free space and uses the bubble's top-left as the enemy's top-left (enemy.c:117; bubble.c:619). Facing is random (rng_next() & 1). The escaped enemy has throw_timer 60 again (so an escaped shooter can shoot after a 60-frame delay).
H33. Always angry after escape even if the Hurry Up has not started and the enemy was calm (bubble.c:619, `angry=1`).

Flyers and hopper
H34. Bounce detection uses an unsigned 8-bit centre: `cx = PX(x)+8` can wrap for x >= 248, which is why the extra `PX(x) >= 0xF0` test exists (enemy.c:461-462). Wall bounce re-places the sprite at x=14 or 226 (not mirrored by the overshoot).
H35. Flutter turn timer shares the `throw_timer` field and starts at 60 (inherited from alloc), so the first re-roll is after 61 frames (enemy.c:82,441-451).
H36. Flutter direction roll: one random word r: bit 0 = left/right, bits 1-2 = vertical (0 up, 1 down, 2/3 level -> level has double weight), bits 3-8 + 32 = timer (enemy.c:444-450). It cannot keep going straight on purpose except by rolling the same.
H37. Screen-edge vertical bounce overrides the flutter's vertical choice immediately (sets moving_down to 1 or 0) (enemy.c:469-475). At y 24 or 200 a "level" flutter stays level (no override unless it is out of range).
H38. Hopper: when vy == 0 exactly it counts as grounded-to-hop only if level_on_ground; a hopper hopping from the apex (vy small positive) will not hop until it lands (enemy.c:483). The hop launch happens before the horizontal step and before gravity in the same tick (enemy.c:483-501).
H39. Hopper "turn to player" is a 50% coin on every hop (second rng draw only after the jump-power draw), resolves to facing right when x equal (enemy.c:484-488).
H40. Hopper hits a wall: flips facing and does NOT move that tick, and flips again if the clamp stops it (enemy.c:490-498). It has no edge/gap logic: it hops off ledges and falls through bottom gaps.
H41. A hopper rising ignores ceilings and platforms, capped only at y=24 (enemy.c:505-510).
H42. Hopper tile choice: tile 1 when vy==0 or vy > 1.5 (enemy.c:591).

Stun, shield, items
H43. The Shield block test compares the bubble's integer x with the enemy's x, not the centre; with equal px x the bubble gets through (enemy.c:189). The block only looks at x, so a bubble coming from above/below on the plate side is also blocked, and a bubble that overlaps the enemy slightly on the far side is not (the 12x12 overlap test comes first).
H44. A de-plated Shield becomes type Walker for good and keeps jump_left=0; it can then climb and gap-jump (enemy.c:198-200). Stun is checked after the entering/freeze checks, so a freeze pauses the stun counter (enemy.c:542-552).
H45. A stunned enemy is still lethal (nothing in enemies_touch checks stun).
H46. Dash hit box on shield enemy is 14x14 (top-left diffs < 14), larger than the 10x12 body hit (enemy.c:197).
H47. Whistle defeat gives tier 2 (4,000), not the 8,000 gem tier of Star/Wave; the design guide says "items = 8,000-point gems" (enemy.c:174 vs 158). Possible inconsistency.
H48. Soap Star / Rinse Wave direction alternates by slot parity (enemy.c:158), not by position.
H49. Frozen enemies' hit test for the Whistle uses 12x12 (enemy.c:173); their body hurt test is skipped entirely under freeze, so freeze protects against bodies but not against shots/chasers (enemy.c:235).
H50. Touch order: boss, shots, chasers, bodies; so a player touching a shot and an enemy at once dies (deadly) rather than being rescue-trapped (enemy.c:216-241).

Hurry / chaser
H51. Chaser spawns only once (no active chaser), at fixed x 104 / 136, y 16; players dead or entering at that moment get none, and a player who respawns later gets none until the next Hurry Up. After a lost life, hurry and chasers are cleared and the timer restarts (game.c:727-731).
H52. Chase dead zone: a chaser axis moves only if |diff| >= 2 px (px + 1 < target or px > target + 1); the target uses the player's integer px (enemy.c:659-667). The two axes move independently at full speed each.
H53. Chaser acceleration is tied to the global g_frame (every 32nd frame), not to its own age, so the first step arrives within 32 frames, and all chasers speed up together; +4/256 per step, cap 2.0 (enemy.c:685-687).
H54. Chaser keeps running while Whistle freeze; it is skipped only while its player is not PS_ALIVE (enemy.c:680).
H55. The "last enemy angry" rule counts active enemies (entering included) == 1 AND zero trapped bubbles AND none entering: it is applied every frame (enemy.c via game.c:891), so a lone enemy in a room with a trapped sibling turns angry only after the sibling is popped or escapes.

CPU
H56. THINK_FRAMES = 6 is documented as "a new decision every 6 frames" but the countdown makes it every 7 frames (cpu.c:32,717-722). (Looks like an off-by-one; the guide says 'reaction delay'.)
H57. Priority comment lists letters, food and follow after hunting, but hunting returns for ANY enemy on the field, so letters/food/follow can never happen while an enemy exists (cpu.c:389-410). In the boss round (no enemies in the table) letters, food and follow are active.
H58. Goal 3 vs 4: pop goal is above hunt, but a lone trapped enemy while others live is ignored until age > 180 (cpu.c:304,316). "Urgent" picks the last urgent bubble in slot order, not the nearest (cpu.c:304-306).
H59. enemies_count()==0 in bubble_to_pop counts entering enemies, so with enemies still dropping in the CPU does not pop a lone trapped enemy (cpu.c:316).
H60. nearest_enemy and the "shoot back" scan differ: hunt uses the nearest by Manhattan distance; shoot_back takes the FIRST in slot order inside the window (cpu.c:271-287 vs 613-618).
H61. Hunt retreat rule: facing away within 18 px = walks away (to >= 18) and only then turns; it never turns toward an enemy that is already within 18 px behind it (cpu.c:397-399). Facing it within 18 px: it shoots point-blank.
H62. Fire alternation: `fire` toggles on every decision made in range and facing; Y only when it becomes 1, with a 1/8 deliberate miss (rng_next() & 7 == 0 -> no shot). A 'deliberate miss' still consumes the toggle (cpu.c:404-406). The random number is drawn only when fire==1 (short-circuit).
H63. Y must be released between shots (edge needed): shoot_back refuses when Y was output last frame (cpu.c:610).
H64. dodge step 2 (goal has Y and cooldown 0) skips all danger checks: the CPU will shoot even while a shot is about to hit it (cpu.c:630); step 3 limits to 2 samples (t=4, 8) when it has Y but cooldown > 0 (NEAR_SAMPLES).
H65. invulnerable > 20 skips dodging; INVULN length in player.c decides how long (cpu.c:630).
H66. evaluate adds t/2 slack to later samples (2,4,7,10,13) (cpu.c:586); static enemies are only really avoided inside 12 px at sample 0 and 1 px at sample 4. Distances are Chebyshev of top-lefts minus 10 (TOUCH_DIST).
H67. Jump prediction counts the rise only (never the fall) and only if on ground; walk prediction uses 235/256 px/frame, hard-coded and independent of the shoes power-up (cpu.c:64-65,561). Dash assumed 54 px, i.e. the Plus dash length, always invulnerable for the whole look-ahead, only the end point counts (cpu.c:567-575).
H68. Strict improvement `v > best` and action order STAY, LEFT, RIGHT, JUMP, JUMP+L, JUMP+R, DASH L, DASH R decide ties: the goal action wins ties, then the earlier listed (cpu.c:648-658).
H69. Dash actions are used only if can_dash (Plus mode only) and not dash_wastes_letter(); the letter-waste test scans x +/- 14 px around the dash path and |dy| < 20 (cpu.c:340-353).
H70. Unstick: 90-frame stuck counter reads the HELD goal pad, not the dodge output, so a CPU held still by dodge STAY while the goal says walk also triggers it; the unstick move skips dodge and goal updates for 30 frames; direction = opposite of the stuck direction; B for the first 14 frames (unstick values 29..16) (cpu.c:701-715).
H71. last_x/last_y are not updated during unstick (early return) (cpu.c:701-704); stuck counter resets only on movement.
H72. The forced B while rising (cpu.c:727) also overrides a dodge action of STAY or a walk; navigation jumps are never cut short. It does not apply during unstick or when not alive.
H73. predict_dangers runs once per frame guarded by g_frame (predicted_frame) (cpu.c:515); prev_ex/prev_ey are updated for all 8 slots every frame, even inactive or frozen ones (cpu.c:526), prev_cx/cy only for active chasers (cpu.c:539), so a freshly spawned chaser has one frame of bogus velocity (zeroed if > 3 px/frame, which always is for a spawn jump).
H74. MAX_DANGERS = 32: enemies are added first, so with many shots the boss or chasers can be dropped (cpu.c:440-442).
H75. Velocity sanity: components beyond +-3 px/frame are zeroed (cpu.c:444-447), including a real 4.5 px/frame dash of an enemy? (none exist) and the chaser at full speed only if > 3 (max 2.0, never).
H76. Soot/boss shot items (IT_ROCK) are taken from item velocities; enemy rocks too, but a rock is also a danger only from the frame it exists, not before (cpu.c:529-534).
H77. move_to: row_of clamps negative to 0; col_of uses +4 (centre of the body) (cpu.c:135-144). seg_find allows +-1 column slack and picks the first match in table order, so overlapping segments resolve by table order (cpu.c:151-160). seg_for_target prefers segments at or below the target, with a flat 40 penalty for segments above (cpu.c:170).
H78. first_hop BFS uses static arrays of 255 entries, `prev` is not cleared between calls (only `seen`), and it returns the second node of the path (cpu.c:180-211). Nav tables over 255 segments would overflow (MAX_NAV).
H79. "Walk off the nearer edge" test mixes two conditions with a precedence: (mid < c || cs.x1 >= LEVEL_W-4) && cs.x0 > 2 -> LEFT; a segment that ends in the right 4 columns always prefers going left (towards the wrap), a segment that starts at column <= 2 always goes right (cpu.c:264). Applies also to same-row hops with want == c.
H80. Up-hop: B is pressed already at <= 2 columns from the spot below the platform, while still walking (cpu.c:256-258); the dead zone for that approach is 4 px (cpu.c:255) vs 2 px for sideways walks (cpu.c:266) vs 3 px when same segment (cpu.c:238).
H81. "No path" case pulses B once per 64 frames ((g_frame & 63) == 0) as a random hop (cpu.c:247); the hold of 7 frames makes this one jump press.
H82. Target overhead jump `PX(ty) < PX(p->y) - 10 && |dx| < 14` also fires when hunting/following (any goal using move_to with same segment) (cpu.c:240).
H83. Food goal includes power-up items and bonus items but excludes thrown (in-flight) food; only when the other player is OFF or a CPU (cpu.c:361,418).
H84. In rescue: goal 1 comes before everything, but dodge can still override the movement every frame.
H85. cpu_pad returns 0 without touching `think`/`stuck` when not alive or not GS_PLAY, so decisions resume where they stopped; cpu_reset clears them at round start (game.c:586,662) (cpu.c:123-132,696-699).
H86. Debug view only (BB_DEBUG): view_goal/view_action (cpu.c:100-107, 663-690) - not behaviour.

Modes (Classic/Arcade vs Plus)
H87. enemy.c has no mode checks: every enemy rule above is identical in both modes.
H88. Shield plate removal needs a dash, which exists only in Plus (player.c:90 try_dash `!g_opt.plus` -> 0), so in Classic a Shield enemy keeps its plate for ever and must be trapped from behind or hit by effects (enemy.c:193, player.c:539).
H89. The CPU's dash actions exist only in Plus (player_can_dash needs g_opt.plus, player.c:77, used at cpu.c:625); in Classic it only walks/jumps/shoots. CPU never uses push bubble, vacuum or upward aim even in Plus.
H90. Classic shots pass through inner walls and platforms (bubble.c:shot_hits_wall: only in Plus do they stop), which matters for CPU hunting: the CPU's range test ignores walls in both modes, so in Plus it can shoot at an enemy behind a wall and the bubble stops at the wall (cpu.c:400); in Classic it reaches. Both cases also apply to the CPU's "shoot back".
H91. Rescue bubble on contact (TOUCH_ENEMY) depends on the options `rescue` flag, not on the mode (player.c:229).

Counts: see end of message.
