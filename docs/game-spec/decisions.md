# Decisions the code took without asking

Written 2026-11-22 while writing [game-spec.md](game-spec.md) from the code of version 1.4.0.
Four readers went through `src/game` and listed every rule that a description of the game would
not give away: 301 candidates in all, kept in full in [notes/](notes/). This file keeps the ones where you may want to rule
differently. The spec describes the code as it is; if you decide otherwise, change the spec first
and the code follows.

Each entry has a letter for what I think it is:

- **A**: looks like an accident or a bug. Suggested ruling in the last column.
- **G**: the design guide says something else.
- **D**: a real design decision nobody took on purpose. Keep or change.

Source lines are for checking, and are only valid for this version.

## Looks like an accident

| # | What the code does | Where | Suggestion |
| --- | --- | --- | --- |
| A1 | A death animation that is cut off by the round ending (clear, skip) can skip CONTINUE and GAME OVER: the state is set to CONTINUE inside the transition start, then overwritten by TRANSITION, and the player is simply out. Inferred from the code, not run. | game.c:593, player.c:253 | Check by a test. If true, fix. |
| A2 | Round 100 shows the floor name "THE CHIMNEY"; "TOP OF THE BOILER" can never appear because the floor loop stops after 10 steps. | game.c:549 | Change the loop or the table. |
| A3 | The Dash on the ground clears "one dash per jump" on its second frame (the on-ground flag is stale during a dash). Only the 60-frame cooldown limits ground dashes. | player.c:530 | Probably fine in practice. Decide whether "one dash per jump" is a rule. |
| A4 | A dash that ends in a one-tile wall can go undetected: only the four corners are tested and they are 11 px apart. | player.c:81 | Test more points, or accept. |
| A5 | CPU decides every 7 frames; the constant says 6. | cpu.c:32, 717 | Fix the constant or the comment. |
| A6 | The CPU hunts whenever any enemy is on the field, so letters, food and following never happen in normal rounds. The goal list in the code comment suggests otherwise. | cpu.c:389 | Decide what the CPU should do. |
| A7 | The CPU skips the whole danger check on a frame where it fires. | cpu.c:630 | Probably intended (shoot first), but it walks into shots. |
| A8 | The "owner" field of a bubble is never written. | bubble.c | Delete the field. |
| A9 | `SPEED_ANGRY` is unused. A dead branch in the secret-room gem scan can never be true. | enemy.c:27, game.c:413 | Delete. |
| A10 | The edge roll (walk off a ledge?) draws random numbers on every frame the enemy stands at a gap, also when already decided. | enemy.c:402 | Changes the random sequence; leave unless the sequence is being redone anyway. |
| A11 | After a death the round timer restarts at 0, so the power-up (frame 300) and bonus food (frame 600) are tested by exact equality again. If that exact frame is skipped (everybody dying or in rescue), the item never comes that round. | effect.c:507, item.c:170 | Use "has passed and not yet placed". |
| A12 | Two players in rescue bubbles cannot free each other, and a rescue can start when the partner is dying or entering. | player.c:229 | Decide. |
| A13 | A freed player keeps the dash state of when they were trapped (dash only cleared). | player.c:205 | Minor. |
| A14 | The fire carpet and effect slot search can mark the wrong bolt when all 4 effect slots are full. | effect.c:114 | Minor. |
| A15 | The BFS for CPU paths uses 255-entry arrays and does not clear `prev`. | cpu.c:180 | Fine with today's maps; keep in mind. |

## The design guide says something else

| # | Code | Guide |
| --- | --- | --- |
| G1 | A new cartridge starts with co-op rescue on. | Rescue off. (Arcade/Plus is gone; the guide now says on, to be confirmed.) |
| G2 | A player in a rescue bubble cannot be hurt by anything, shots, soot and chaser included. | Shots still kill. |
| G3 | A dash press is honoured on the press frame and 5 more (6 checks). | "6 frames" (same thing, depending on how you count). |
| G4 | A fired bubble moves 9 times, 36 px. | 32 px. |
| G5 | Steam Whistle kills give 4,000 (tier 2). | Item kills give 8,000 gems. |
| G6 | A full jump rises about 43 px. | 42 (the code comment too). |
| G7 | Boss lightning does at most 1 damage per 20 frames, however many bolts. | Not mentioned. |
| G8 | A dash can end inside a one-tile wall (see A4). | "Never ends inside a wall." |
| G9 | A fired bubble that meets the boss stops flying and floats on, and then drifts through the boss. | "Bubbles bounce off it." |

## Design decisions nobody took

| # | What the code does | Why it matters |
| --- | --- | --- |
| D1 | Chain owner, direction and kill count are single values for the whole game. A pop by the other player, or by a push bubble, in the middle of a chain steals the points and letters of every later link. Both players popping separate groups at the same time share one count and one food tier. | Co-op fairness. |
| D2 | Jump is level-triggered: holding B jumps again on landing. There is no buffer and no grace time at ledges. | Feel. |
| D3 | Walls block a body only at the feet row; head and torso pass. | Narrow gaps. |
| D4 | The first shot-sized wall test is a single point 6 px ahead of the bubble's centre. Thin platforms and corners can be slipped past. | Shots. |
| D5 | Enemies' shot lines ignore walls and distance, use any living player (not the nearest), and never turn to face a player. A player behind a shooter is safe. | Difficulty. |
| D6 | Climbing needs a thinking tick (every 120 frames, global) and a 40% chance. All enemies decide on the same frame. | Enemies move in waves. |
| D7 | A player who takes a pick-up is the one who gets the power-up, but counters are shared: player 2's bubbles unlock player 1's power-up. | Co-op. |
| D8 | Any death resets the round timer, hurry, anger and chasers for the whole room, and clears the player's power-ups. | Punishing and forgiving at once. |
| D9 | A bubble can be bounced on while it holds a special or letter, so a letter can be jumped over instead of taken. Holding B pops nothing on the way down. | Letters. |
| D10 | Floating bubbles ignore walls and pile up along y 22. Bubbles never wrap. | Visual clutter at the top. |
| D11 | The vacuum drags a trapped enemy with its bubble, through walls, and holds bubbles at its exact centre until it ends. | Vacuum balance. |
| D12 | The 3rd, 4th and 5th kill of one go each queue a letter; the 6th and later none. | Rewards. |
| D13 | Letter choice is not uniform: random start values 6 and 7 both resolve to B. | Fairness of BLASEN. |
| D14 | An enemy released from a bubble is always angry, and a trapped bubble keeps a lone enemy from turning angry. | Stalling. |
| D16 | A trapped enemy's escape needs a free enemy slot; if the table is full the enemy is lost. | Rare. |
| D17 | Round clear is tested every 8th frame and only when no enemy is active and no bubble holds one. Thrown enemies in the air do not delay it. | Pace. |
| D18 | The random generator is advanced once per frame in every state, from power-on. The title time changes everything that follows on a console that starts with the same seed. | Recordings need the seed and the frame count. |
| D19 | Joining mid-round keeps the old score and letters but gives 3 lives; CONTINUE has no limit. | Score meaning. |
| D20 | The CPU partner's score is zeroed every frame, so it can never earn an extra life and never enters a hi-score. | Fine, but state it. |
| D21 | The `no_death` flag that opens doors is cleared by any lost life, including a rescue that ran out, but not by being rescued. | Secret rooms. |

## What the trial shows

The readers found 301 rules that a plain description would not give. Many of them are
tie-breaks and update order (which of two things is tested first). They decide what the game plays
like, and most of them cannot be seen by playing. Examples are the order of checks in a player's
frame, the shared random sequence, and the 4 or 5 frame chain links.

For the spec as the source of truth, three kinds of rule need a place:

1. Numbers and boxes. A table in the spec is enough (section 14).
2. Order of updates and of random draws. The spec has the order of a frame (section 4) and says
   that reordering changes the random sequence. A rebuild from the spec will differ in the
   random choices unless the order is written down as precisely as the code has it. I did not
   try to write down every draw.
3. Tie-breaks. Some are in the spec as rules in passing. The rest (for example "the last urgent
   bubble in slot order wins") are only in the raw notes in [notes/](notes/).

The next step of the proposal is to hand the spec to an agent that sees nothing else and let it
rebuild one slice. I would start with the bubble rules (section 6): they are self-contained, have
exact numbers, and can be checked against recordings tick by tick.
