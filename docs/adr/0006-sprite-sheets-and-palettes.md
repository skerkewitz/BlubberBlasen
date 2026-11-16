# ADR-0006: One sprite sheet per room group, and how the 8 sprite palettes are used

Date: 2026-11-16
Status: accepted

## Context

ADR-0004 sets the SNES limits for all art, and ADR-0005 decides that the graphics are compiled into
the program and that each room group gets its own sprite sheet. Measuring the placeholder art
against those limits showed two problems.

**Cells.** The SNES holds at most 16 KB of sprite tiles, which is 128 cells of 16 x 16. About 103
cells are needed in every round (players, bubbles, effects, food, items, score pop-ups, the
defeated-enemy spin, Lord Smog), 3 to 6 per enemy type and 12 for the boss. With the enemy types
of each planned room group (`docs/story.md`), three groups needed 131 cells and the last one 144.

**Colours.** A sprite uses one of 8 palettes of 15 colours. The placeholder art used 9 colours for
the players, 35 for bubbles and effects, 74 for food and items and 25 to 56 for the enemies of a
group. The game also tints sprites: angry enemies red, frozen enemies and dash afterimages grey,
an enemy about to shoot yellow, a player's state flash orange. The PC multiplied colours for that;
on the SNES each tint seen at the same time needs a palette of its own.

## Decision

1. **One sprite sheet per room group,** as planned: rounds 1-5, 6-15, 16-30, 31-50, 51-70, 71-99.
   **Round 100 has its own sheet**, with the boss instead of enemies (it has no enemies), and the
   title, story and ending screens have one with seven enemy types. A sheet's enemy types are
   read from the rounds' level files, so they cannot get out of date.
2. **Exact copies are stored once.** Cells with the same pixels share one slot (score pop-up
   halves, the boss's top half, a dash glow outline). That brings the fullest sheets to 124 of 128.
   Colour variants of one shape (diamonds, candies, the green, orange and red bubbles and rings)
   stay separate cells: each colour on screen at the same time would cost a palette.
3. **The shared cells are in the same place in every sheet**, so the game can address them with
   fixed numbers; the generated table `gfx_sheet_slot` gives the slot of any other cell.
4. **The 8 sprite palettes:**

   | Palette | Use |
   | --- | --- |
   | 0 | Player 1 |
   | 1 | Player 2: player 1 with the green ramp swapped for blue |
   | 2 | Bubbles and effects (and the dash glow, drawn yellow) |
   | 3 | Food, items and score pop-ups |
   | 4 | The sheet's enemies, Lord Smog and the defeated-enemy spin (in round 100: the boss) |
   | 5 | Red copy of 4: angry enemies, the hurt boss |
   | 6 | Grey or yellow copy of 4, switched per frame: frozen enemies, the flash before a shot |
   | 7 | Grey copy of the player: dash afterimages |

   Every tinted copy is computed by `gfx2c` from the palette it copies, with the tint colours the
   PC used, so the game only switches palettes. A player's own flash (yellow or orange) rewrites
   palette 0 or 1 for those frames, as ADR-0004 foresaw.
5. **The placeholder art was reduced once to these limits** (2026-11-16): all colours moved to the
   nearest SNES colour, enemies, food and items and bubbles and effects merged to 15 colours each,
   the title logo to 6 palettes of 15 colours. New art is drawn to the limits instead: `gfx2c`
   fails the build when art breaks one, it never reduces colours itself.

## Consequences

- Enemies, food and items lost some shading in the placeholder art; the angry walker, the orange
  diamond and the cake show it most. The final art is drawn for 15 colours per palette (ADR-0004:
  a shared soot ramp and a few accent colours per enemy).
- All enemy types together have to share 15 colours, because an enemy looks the same in every
  group. The boss shares its palette with Lord Smog and the defeated-enemy spin.
- The sheets are full: 124 of 128 cells in groups 16-30, 51-70 and 71-99. More animation frames
  need room first. The next reserves: score pop-ups from 8 x 8 digit sprites (about 19 cells), then
  the bubble and ring colour variants as palettes (12 cells, 2 palettes).
- Loading a sheet means copying up to 5 rows of 1 KB when a group starts; the shared rows stay.
