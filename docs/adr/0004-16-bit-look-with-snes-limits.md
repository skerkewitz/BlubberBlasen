# ADR-0004: A 16-bit look, held to SNES limits from stage 1

Date: 2026-11-16
Status: accepted

## Context

The prototype looks like an 8-bit game: rooms are drawn on black on a single layer, graphics are
24-bit PNGs, and a "palette" is only a tint the platform multiplies in. That is close to the 1986
arcade original, but it uses little of what the SNES can do (a second background layer, 16 colours
per tile, palette effects, per-line effects).

Two AI-generated mock-ups of a factory cellar room were used as mood tests. The first one looked
rich but was unplayable: platforms, background girders and barrels had the same material and
brightness, and in a greyscale test everything became one grey. The second one followed explicit
readability rules (dark blue-grey background with only vertical shapes, bright copper platforms with
a light top edge and a shadow below, saturated colours only on characters) and read at a glance.
It also looked like a SNES game rather than an arcade game from 1986. We want that look.

A richer look carries a risk: art drawn freely as 24-bit PNGs will use more colours and tiles than
the SNES can show, and the port in stage 3 would have to redraw it.

## Decision

1. **Target look:** a SNES game from the early nineties. Each room uses three background layers and
   sprites, as in SNES mode 1:

   | Layer | Content | Colours |
   | --- | --- | --- |
   | BG1 | Walls and platforms (everything solid) | 4 bpp, 15 + transparent per tile |
   | BG2 | Background: brick, pipes, chains, furnace | 4 bpp, 15 + transparent per tile |
   | BG3 | HUD and text | 2 bpp, 3 + transparent per tile |
   | Sprites | Players, enemies, bubbles, items, effects | 4 bpp, 15 + transparent per sprite |

2. **Readability comes first.** The rules in the design guide, "Readability and art limits", are
   binding for all art: brightness order, light top edge on every platform, vertical-only
   background detail, saturated colours only for things that move or can be picked up, and a
   greyscale test for every room.

3. **SNES limits apply from stage 1**, not only in the port:
   - Colours are 15-bit (5 bits per channel); art is snapped to those steps.
   - Every 8 × 8 background tile uses exactly one palette of 15 colours plus transparent.
   - BG3 shares colour entries 0–31 with background palettes 0 and 1, so BG1 and BG2 get
     **6 palettes** (2–7) per room, about 90 colours.
   - **8 sprite palettes** of 15 colours each.
   - At most 128 sprites, **32 sprites and 34 tile-widths (272 px) per scanline**; two sprite sizes
     at a time (8 × 8 and 16 × 16 in normal rounds, 16 × 16 and 32 × 32 may be used in boss rounds).
   - At most 512 sprite tiles loaded at once (16 KB); more frames are streamed in during vertical
     blank (about 5–6 KB per frame).
   - Tiles may be flipped horizontally and vertically for free; symmetric rooms should use that.

4. **Palette budget for a room** (first plan, may be changed per group as long as the totals hold):

   | Palette | Use |
   | --- | --- |
   | BG 0–1 | HUD and text (via BG3) |
   | BG 2 | Walls and platforms |
   | BG 3–5 | Background families (for example brick, metal, furnace with fire) |
   | BG 6–7 | Spare per group |
   | Sprite 0 / 1 | Player 1 / player 2 |
   | Sprite 2 | Bubbles and effects |
   | Sprite 3 | Food and items |
   | Sprite 4–5 | The enemies of the round (they share palettes: a common soot ramp plus 2–3 accent colours per type) |
   | Sprite 6 | Angry enemies (red) |
   | Sprite 7 | Spare: Hurry Up chaser, projectiles |

   State feedback on the player sprites (dash ready, full energy, dash flash) rewrites the colours
   of palette 0 or 1; it does not use extra palettes. Animated light such as fire uses colour
   cycling inside its palette.

5. **Colour math stays free.** The SNES can only do one kind of colour math at a time, per layer.
   So the background is drawn dark in its palettes instead of being darkened by hardware, and
   bubbles are drawn as a rim and a shine spot with a transparent middle instead of being
   translucent. Colour math and HDMA are kept for per-line gradients, furnace glow and flashes.

6. **An art checker in the build** enforces points 3 and 4: it fails the build when a tile uses
   more than one palette or more than 15 colours, when a room needs more than 6 background
   palettes, when a colour is not on the 5-bit grid, or when the sprite sheet needs more than the
   sprite budget. It also reports the number of unique tiles per room.

7. **One room first.** The factory cellar (rounds 1–5) is drawn completely in the new style:
   background, walls and platforms, both players, the walker, bubbles and food. The other groups
   start only after it has been played and the rules have been adjusted.

8. **AI-generated pictures are mood references only.** They do not follow the tile grid or the
   colour limits and their provenance is unclear, so nothing from them goes into `assets/`.

## Consequences

- Stage 1 needs indexed graphics: palettes as data and tiles that refer to them, instead of 24-bit
  PNGs with a tint. That is closer to the SNES and makes the port simpler, but the platform layer
  and the asset pipeline have to change before final art is drawn.
- Art takes more work than before: more colours, a background per group, and characters that must
  still read at 16 × 16 px. Details of the mock-ups (faces, tools, helmets) have to be simplified.
- Many bubbles in one horizontal line can exceed the sprite limit per scanline. The game has to cap
  bubbles where needed and rotate sprite order every frame so that flicker is spread out.
- The "Art style sheet" item in `GAME-TODO.md` now only has to settle the drawing style (outlines,
  shading, proportions); the limits are fixed here.
