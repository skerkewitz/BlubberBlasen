# Retro TV effect

When the 256 x 224 framebuffer is scaled up into the window, it goes through three shaders
that make it look like a SNES picture on a consumer CRT television, fed over composite
video. The game itself never sees the effect: the framebuffer stays clean, and so do
`--shot` screenshots. `--shot-crt` saves a copy through the effect (see the end of this
page).

`Tab` turns the effect on and off while the game runs. `--no-crt` starts the game with it
off. If any of the shaders fails to compile, the game logs `CRT: shader failed, drawing without it`
and draws the plain picture.

## Where the code is

Everything is in [src/platform/platform_raylib.c](src/platform/platform_raylib.c), in the
section headed `Retro TV look`. The work is split into three stages, like a real set:

1. **Signal** (`SIGNAL_SHADER_BODY`): everything that happens to the video signal before
   it reaches the tube, so it depends only on the game picture: SNES colours and the
   composite colour bleed. It is drawn into `signal`, a fixed 1024 x 224 texture. The
   width is 4 samples per game pixel, so the blur stays smooth between pixels; the height
   stays 224, since the signal never mixes lines. Values are stored gamma-encoded, so dark
   tones don't band in 8 bits per channel.
2. **Glow** (`GLOW_SHADER_BODY`): the light the glass scatters, blurred from the signal at
   256 x 224 in two passes (`glow_a` horizontal, `glow_b` vertical).
3. **Screen** (`CRT_SHADER_BODY`): everything finer than a game pixel, drawn at window
   resolution: curved glass, beam and scanlines, phosphor stripes, then the glow added on
   top and the vignette. It reads the signal at the line above, its own line and the line
   below, so each screen pixel needs only three reads plus one for the glow.

Stages 1 and 2 are fixed size and cost the same in any window. Only stage 3 grows with the
window. `crt_prepare()` runs stages 1 and 2 each frame; `crt_draw()` runs stage 3.
`CRT_MASK_CONSTS` holds the mask values that stages 2 and 3 both need. Desktop (GLSL 330)
and web (GLSL ES 1.0) share the sources; only `CRT_FS_HEADER` differs.

The draw code passes `scale`, the whole number the framebuffer is enlarged by, for example
4 in a 1024 x 896 window.

The shaders are C strings, so every change needs a rebuild. Check both builds after a change:
GLSL ES 1.0 rejects some things that desktop GLSL accepts, and the error only shows up when
the shader is compiled at runtime.

## The effects

The order follows a real set: the signal is smeared first (colour bleed), then the beam
draws its lines, then it lights the phosphor stripes, and last the glass spreads a little
light (glow). Blurring after the stripes would wash them out.

### 1. Curved glass

A CRT tube had a curved front, so the picture bulged outward: the middle of each edge
reached further out than the corners. The term is barrel distortion. (Pincushion
distortion is the opposite, with edges bent inward.)

The screen stage bends the texture coordinate before it does anything else, so the
scanlines, the colour bleed, the phosphor stripes, the glow and the vignette all follow the
curve. The middle of each picture edge stays exactly on the edge of the window, and towards
the corners the edges curve inward, with black outside. No game pixel is ever cut off. That
matters because the HUD uses the top row and the corners.

| Constant | Now | Effect |
| --- | --- | --- |
| `CURVE` | `0.12` | Amount of bulge. 0 is flat. Around 0.25 looks like a cheap 14" set. At 0.12 the corners sit about 3.5 game pixels in from the window corners. |

A real TV also had rounded corners, because the tube's opening cut off the corners of the
picture (overscan). Games of that time kept important things away from the edges for that
reason. This game puts the HUD right into the corners, so rounding them would hide it.
Rounded corners would only work with a black margin around the picture, which makes the
picture smaller.

### 2. SNES colours (15 bit)

The SNES stored colours with 5 bits per channel, which gives 32 levels each. The shader
rounds every colour to those levels (`floor(c * 31.0 + 0.5) / 31.0` in `fetch()` of the signal stage). It makes
little difference with art that already uses SNES colours, but it catches smooth gradients
that the real hardware could not show.

To turn it off, delete that line. To use 6 bits, change both `31.0` to `63.0`.

### 3. Composite colour bleed

Composite video sends brightness (luma) and colour (chroma) mixed in one signal, and the
colour part has much less bandwidth. On a TV, colours smeared sideways along the line much
more than brightness did. That is why red text on a dark background looked fringed.

The signal stage converts each pixel to YIQ, the NTSC colour space, where Y is brightness and I
and Q are colour. It then blurs horizontally over 5 pixels with two different widths, and
converts back to RGB. The blur is horizontal only because the signal is a stream along each
line; neighbouring lines are not mixed. Colours are blended in linear light (gamma 2.2), so
bright and dark pixels mix the way light does.

| Value | Now | Effect |
| --- | --- | --- |
| `LUMA_SHARP` | `1.4` | Brightness blur. Higher is sharper; 2.2 was the earlier, crisper value. |
| `CHROMA_SHARP` | `0.45` | Colour blur. Lower bleeds wider. |
| loop `-2 .. 2` | 5 pixels | How far the blur reaches. With a much lower colour value, widen the loop too, or the bleed gets cut off. |

### 4. Beam spot and scanlines

The electron beam drew the picture one line at a time. A SNES sends a "240p" signal: it
draws the same lines every frame and never fills the space between them, so thin dark gaps
can appear between the lines. TV broadcasts were interlaced and filled the gaps on
alternate frames, which is why scanlines were hard to see on normal television. Even with a
console, a small TV seen from the sofa showed them only faintly. They look stronger on a
modern monitor, where each of the 224 lines is many pixels tall.

The beam spot was round and soft, and larger when it was bright. The shader models it as a
bell curve in each of three lines: the line the pixel sits in, plus the one above and the
one below, because their soft edges reach into the gap. Each line's bell gets wider with
its brightness, so bright lines fill the gaps and dark lines leave them visible. The bell
keeps its total light (`spot()` divides by its area), so a wider beam spreads the same light
over more space instead of getting brighter. That is why there is no brightness boost.

| Constant | Now | Effect |
| --- | --- | --- |
| `BEAM_DARK` | `0.22` | Beam width for dark pixels, in game lines. Smaller means darker gaps. |
| `BEAM_BRIGHT` | `0.36` | Beam width for bright pixels. At about 0.42 bright lines merge and the scanlines vanish on light colours. |
| `SCANLINES` | `0.85` | Overall strength. 0 gives flat lines without gaps. |

This part may still change: whether we want scanlines this strong is an open question.

### 5. Phosphor stripes (aperture grille)

A colour CRT has no pixels. Behind the glass are thin vertical stripes of red, green and
blue phosphor with dark gaps between them, and each stripe glows only in its own colour.
Up close you see a fine RGB grid; from the sofa it adds up to the picture.

The shader builds that grid on the tube, not on the monitor: it uses the bent game
coordinate, so the stripes follow the curved glass and stay the same size relative to the
picture at any window size. Each triplet (red, green, blue) is `1 / TRIADS` game pixels
wide. For each screen pixel the shader works out how much of each stripe it covers, so
stripes that are 1.33 or 2.25 screen pixels wide come out evenly instead of shimmering.

| Constant | Now | Effect |
| --- | --- | --- |
| `TRIADS` | `1.0` | Triplets per game pixel. |
| `MASK_FILL` | `0.75` | Part of each stripe that glows; the rest is the dark gap. |
| `MASK_DARK` | `0.1` | How much of the other colours leaks into a stripe. 0 is pure R, G and B; 1 turns the mask off. 0.45 is a soft tint. |

A real 14-20 inch TV had about 2 triplets per game pixel. That needs about 3 screen pixels
per stripe to show cleanly, so `TRIADS = 2.0` only works from about 18x up, which is wider
than a 4K monitor. At 9x (a 4K monitor) `TRIADS = 1.0` gives 3 screen pixels per stripe.
Raising `TRIADS` on a smaller screen gives moire, not detail.

The darker the gaps, the less light the mask lets through, so the shader brightens the
stripes to keep the average brightness the same. Bright colours then hit the monitor's
maximum and get a little paler and flatter. A real CRT got around this with very bright
phosphor spots; a normal monitor can't. Part of the clipped light comes back as glow
(effect 6). If bright areas look washed out, raise `MASK_DARK`.

The glow shader needs the same mask values to work out how much light gets clipped, which
is why they sit in `CRT_MASK_CONSTS` and not in the main shader alone.

### 6. Glow (halation)

On a real tube some light scattered inside the thick front glass and came out next to where
it was made, so bright things had a faint halo and the picture looked soft rather than
clinical. The screen stage takes a blurred copy of the signal and adds it on top, after the mask,
so the halo has no stripes.

The blurred copy is made at game resolution (256 x 224) in two passes, which is cheap. The
first pass turns each pixel into glow light: a small share of its colour (`GLOW`), plus a
part (`OVERFLOW`) of the light the mask clips off bright colours. That second term puts some
of the light the monitor can't show back into the picture, as a halo around bright areas.
It fades with the mask at small scales (see below).

These constants are in `GLOW_SHADER_BODY`:

| Constant | Now | Effect |
| --- | --- | --- |
| `GLOW` | `0.03` | Halo from every pixel. |
| `OVERFLOW` | `0.1` | Share of the clipped light that becomes halo. 1.0 would return all of it and makes the picture look smeared. |
| `GLOW_SIZE` | `2.0` | Halo radius in game pixels. The loop reaches 6 pixels, so going above about 3 cuts the halo off. |

Small values matter here. Glow is added in linear light, and the gamma step at the end makes
faint light over black very visible: a linear 0.05 shows as about 25 % grey. With `GLOW`
at 0.06 and `OVERFLOW` at 0.3 the dark shading under the platforms already turned pink.

### 7. Vignette

The picture gets a little darker towards the corners, as the edges of a tube were dimmer
than its centre. `rgb *= (1.0 - 0.35 * dot(c, c))` darkens the corners by about 17 %.
Change `0.35` to make it stronger or weaker; 0 turns it off.

## Small windows

Scanlines and the phosphor stripes need several screen pixels per game pixel. At 1x or 2x
they would only produce moire patterns, so they fade with the scale. The overflow part of
the glow fades with them, since there is no clipped light without the mask:

```
fx = clamp((scale - 1.5) / 1.5, 0.0, 1.0)
```

At 1x `fx` is 0 (off), at 2x it is a third, and from 3x up it is 1 (full strength). The
curvature, colours, colour bleed, the `GLOW` part of the glow and the vignette work at
every size.

## Checking a change

Screenshots taken with `--shot` do not show the effect, because they come from the
framebuffer. Add `--shot-crt <n>` (development builds) to save each shot a second time
through the effect at n x scale, as `shot_<tick>_crt.png`:

```
blubberblasen --autoplay --seed 7 --shot 900 --shot-crt 4 --quit 900
```

The same seed and tick give the same frame, so two such shots taken before and after a
change to a constant show exactly what the change did. Use a scale of 3 or more, or the
scanlines and phosphor stripes are faded out (see "Small windows"). For a crop that shows
single phosphor stripes, a scale like 9 matches a 4K monitor.
