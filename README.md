# DC32_Fragments

Welcome to the Illuminati Party® badge 'Fragments' for 2024 (DC32).

This fork adds a rewritten animation firmware and a browser test bench for it.
The original conference sketch is preserved unchanged as `ConferenceCode_v1`.

| | |
|---|---|
| `Fragments_NNA/Fragments_NNA.ino` | New firmware. 19 animations, non-blocking, brightness control. |
| `sim/bench.html` | Test bench. Runs every animation in a browser, same math as the badge. |
| `ConferenceCode_v1` | The original conference sketch, untouched. |
| `DC32 Stand v2.stl` | Printable stand. |

---

## Setup

1. Install the Arduino IDE https://www.arduino.cc/en/software
2. Open the Arduino IDE, click on Tools at the top, then "Manage Libraries"
3. On the left side of the screen, click on Board Manager
4. Search for ESP32 by Espressif and install the package.
5. On the left side again, click on Library Manager.
6. Search for and install the Adafruit NeoPixel Library. This will likely need to install other dependencies, which you will be prompted to do.
7. Plug in your badge and make sure it is turned on.
8. Go to Tools > Board, and select ESP32 Dev Module.
9. You'll need to select the appropriate COM port. This will likely be the highest number COM available. If you're unsure, check Device Manager on your computer.

## Program

1. Open `Fragments_NNA/Fragments_NNA.ino`, or copy its contents into a new IDE window.
2. Press "Upload" in the ribbon at the top (the arrow in a circle icon).
3. For the first upload, you'll be prompted to save the code to your computer.
4. You should see the IDE compile the code and upload it to the badge.

Serial monitor at **115200** baud reports the current animation and brightness.

---

## Controls

| Input | Action |
|---|---|
| Short press | Next animation |
| Hold (0.7s, repeats every 0.45s) | Cycle brightness, brightest wraps back to dimmest |

Both give a readout on the badge itself: a bar around the perimeter for
brightness, or a count of lit pixels from the bottom-right corner for the
animation number. Both count in perimeter order, so they run down the edge
rather than turning off into a board's leg partway along.

## Animations

| # | Name | Family |
|---|---|---|
| 0 | Breathe | Ambient — the original `breathingBadge()`, finished |
| 1 | Drift | Ambient |
| 2 | Plasma | Ambient |
| 3 | Candle | Ambient |
| 4 | Comet | Perimeter motion |
| 5 | Collide | Perimeter motion |
| 6 | Rainbow | Perimeter motion |
| 7 | Corner Pulse | Perimeter motion |
| 8 | Eye Pulse | Eye-driven |
| 9 | Charge & Fire | Eye-driven |
| 10 | Scanner | Eye-driven |
| 11 | Aperture | Fragments — the boards close like iris blades, then the flash fires |
| 12 | Fragment Chain | Fragments — the `DOUT`→`DIN` data path, made visible |
| 13 | Vortex | Spiral — three arms winding inward, one per board |
| 14 | Radar | Spiral — one beam sweeping, with a decaying wake |
| 15 | Bloom | Spiral — two arms thrown outward from the eye |

All three turn clockwise seen from the front.
| 16 | Matrix Rain | Glitch — the badge as a wall, drops falling through it |
| 17 | Glitch | Glitch |
| 18 | Boot Sequence | Glitch |

The three original conference flickers are not in this firmware. They are still
in `ConferenceCode_v1`, unchanged, if you want them back.

Set `AUTO_CYCLE_MS` to a number of milliseconds to have the badge advance on its
own.

## Test bench

Open `sim/bench.html` in any browser — no build step, no server. It runs the
same sine table, the same `ColorHSV`, the same gamma curve and the same current
limiter as the firmware, so colors and timing carry over. Click an animation,
drag the speed slider, hover an LED for its strand index and live RGB, or press
and hold the on-screen badge button to feel the real control scheme.

---

## How the rewrite differs

**Non-blocking.** The original ran `delay(90)` inside each animation, so a
button press only registered if you happened to be holding it during the one
`digitalRead()` per cycle. Everything now runs off a frame timer and the button
is sampled every pass of `loop()`.

**One place for the GRB problem.** LEDs 60–64 are from a different vendor and
take their bytes in GRB order while the strand is initialized `NEO_RGB`. The
original hand-swapped red and green in every color constant destined for those
five (hence `rightAngleGreen` being made of red). That fixup now happens once,
in `pushFrame()`, so animations are written in plain RGB throughout. If your
board splits somewhere other than LED 60, move `GRB_FIRST`.

**Geometry instead of indices.** `buildGeometry()` builds lookup tables for each
perimeter LED's position on the triangle and its distance from the eye, so
animations can say "sweep upward" or "ripple out from the eye" rather than
hardcoding strand numbers.

**Brightness and power.** Animations are written at the full 0–255 range and
scaled at output, giving five brightness levels on the button. A current
limiter estimates draw from the channel sum and scales the whole frame down if
it would exceed `POWER_LIMIT_MA` (default 700 mA) — 65 WS2812s at full white is
about 3.9 A, which nothing on this badge wants to supply.

Note that at the lower brightness levels an 8-bit PWM output has real limits:
with the cap at 45/255, authored values below roughly 48 land on zero. That is
inherent to the hardware, not the code. Bump the brightness level if an
animation looks like it is missing its dim detail.

## How the badge is built

The triangle is not one board. It is **three boards of 20 LEDs each**, chained
`DOUT` → `DIN` through the four-pin headers on the back, and twisted together
so they pinwheel into a triangle — which is where the name comes from, and why
the corner LEDs land 20 apart at strand indices 3, 23 and 43.

Each board's twenty LEDs, walked in strand order: **3** up the previous edge,
**the point**, **12** down the next edge, then a **turn, 2, turn, 2**. Both
turns measure close to 60° in the same direction — 120° together, the exterior
angle of an equilateral corner — so the tail leaves the perimeter rather than
following it.

That is what makes an edge sixteen LED-spacings, not twenty: an edge carries its
corner, that board's twelve, and the next board's three. Three edges of sixteen
is 48 LEDs on the outline, and the 12 tail LEDs sit inside it. `TAIL_A1`,
`TAIL_A2` and `EDGE_STEPS` in the sketch hold those numbers.

`fragOf()`, `posInFrag()` and `fragCorner()` expose that structure, and the two
Fragments animations are built on it. The arrangement is also why the eye
graphic reads as a camera aperture, which is what `Aperture` plays with.

## Three ways to walk the badge

Getting this wrong is the easiest mistake to make here, because strand order
looks like it ought to be the outline and isn't.

| | | |
|---|---|---|
| **Strand order** | `0..59` | What the wire does. It runs an edge, dives off the outline into that board's tail, then jumps out to the next board's leg. |
| **Perimeter order** | `PERIM[0..47]` | The 48 outline LEDs in the order your eye walks them, from corner BR. |
| **Position** | `ringX` / `ringY` / `ringDist` | No ordering at all — the badge is a screen and an LED is lit by where it sits. |
| **Polar** | `polR` / `polA` | Position again, but about the eye — radius and angle, for anything that turns or winds. |

Anything that reads as **motion along an edge** belongs in perimeter order. A
head walking strand order veers into the middle of the badge every twenty LEDs,
does four, and pops back out. Comet, Collide, Rainbow, Corner Pulse, Matrix Rain,
Boot Sequence and the discharge half of Charge & Fire all use `perimPoint()`.

`spillTail()` lets those effects run down a board's four tail LEDs as they pass
its attachment point, so the inner LEDs join the motion instead of sitting dead
through every chase.

Plasma, Eye Pulse and Scanner are **positional** — they never referenced order in
the first place, which is why they were the only chases that always looked right.
Matrix Rain is positional too, and the clearest case for it: the badge is a wall
and a drop falls straight down through it at a constant speed *in space*, not
from LED to LED. So the delay between two lit LEDs is however long the empty
board between them takes to cross. LEDs 49 and 59 share the column at x=38 with
fifty height-units of nothing between them — about 1.8 s at drop speed — while
59 to 8 is twelve units and takes 450 ms. Outline and tails are both part of the
wall; only the eye is excluded.
Fragment Chain is the one animation that genuinely wants **strand order**: it is
drawing the data path.

The three Spiral animations are **polar**. Because the badge has genuine radial
structure — outline at mean radius 164, the twelve tail LEDs at 133, the eye at
52 — a spiral written against `polR` / `polA` sweeps through all three bands as
one surface, curving in off the edge, through the tails, to the eye. They are all
phase = `a*angle + b*radius + c*time`: `a` sets the arm count, `b` how tightly
they wind, the sign of `a` sets which way it turns and the sign of `b` sets
whether it travels in or out.

Keep `b` near 1. With only three radial bands, a coefficient of 2 or 3 wraps the
phase right round between them, and the winding aliases into arbitrary per-band
offsets rather than reading as a spiral — the pattern still moves, but it stops
having a coherent direction.

## Layout assumptions

Increasing strand index runs clockwise from the bottom-right corner with the
apex up. Two things remain estimates, both a few lines near the top of the
sketch:

- `auxX` / `auxY` place the eye (60–62) and the pair above it (63–64). These
  are measured off photographs of the lit badge — note that 63/64 are *not*
  near the apex despite the original's "top of board" comment; they flank the
  eye from above, inside the mandala.

**LEDs 63 and 64 are aimed down into the white of the eye.** They are wash
lights, not point accents: whatever colour they carry becomes the colour of the
sclera, which makes them the highest-leverage pair on the badge for selling
"the eye is awake". Give them the eye's colour, never the board's — a teal pair
over a warm eye turns the whole white teal and it stops reading as an eye. Every
animation except the three conference classics drives them that way.
- `TRI_ASPECT` is the triangle's height over its half-width, set to 1.732 for
  equilateral (photos measure ≈1.72). Only `ringDist` — and so `Eye Pulse` —
  depends on it.

The tail geometry is measured off a photograph of a single board and reproduces
it to within a tenth of an LED-spacing. Both consequences are confirmed against
the hardware: **17 LEDs from one corner LED to the next inclusive**, and **12
LEDs sitting inside the outline** in addition to the five at the eye.

Turn on *Trace each board's 20 LEDs* in the test bench to see the modelled path
for each board next to the real thing.

---

Original conference sketch by [Kredence](https://github.com/Kredence/DC32_Fragments),
itself adapted from Adafruit's NeoPixel example.
