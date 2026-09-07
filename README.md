# DC32_Fragments

Welcome to the Illuminati Party® badge 'Fragments' for 2024 (DC32).

This fork adds a rewritten animation firmware and a browser test bench for it.
The original conference sketch is preserved unchanged as `ConferenceCode_v1`.

| | |
|---|---|
| `Fragments_NNA/Fragments_NNA.ino` | New firmware. 15 animations, non-blocking, brightness control. |
| `sim/bench.html` | Test bench. Runs every animation in a browser, same math as the badge. |
| `ConferenceCode_v1` | The original conference sketch, untouched. |
| `DC32 Stand v2.stl` | Printable stand. |

---

## Setup

Either toolchain works. The IDE is the gentler path; `arduino-cli` is what this
fork was built and flashed with.

**Arduino IDE**

1. Install the Arduino IDE — https://www.arduino.cc/en/software
2. Tools → Board → Boards Manager, search **esp32** by Espressif, install it.
3. Tools → Manage Libraries, search **Adafruit NeoPixel**, install it. Accept the
   dependency prompts.
4. Tools → Board → **ESP32 Dev Module**.
5. Tools → Port → the badge's port. Usually the highest COM number on Windows,
   typically `/dev/ttyUSB0` on Linux.

**arduino-cli**

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel"
```

## Flashing

Plug the badge in **and switch it on**. The USB-serial chip is powered from USB
and enumerates a port either way, so a port appearing tells you nothing about
whether the ESP32 has power — a badge left switched off looks exactly like a
wiring fault.

In the IDE, open `Fragments_NNA/Fragments_NNA.ino` and press Upload. Or:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 Fragments_NNA
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 Fragments_NNA
```

A serial monitor at **115200** baud reports the animation and brightness on every
change, which is the quickest way to see whether the button is behaving:

```
[0/15] Breathe
Fragments: 15 animations, brightness 4..160.
  short press        = next animation
  hold               = ramp brightness, turns round at each end
  press, then hold   = ramp colour
```

### If it will not connect

`Failed to connect … No serial data received` at every baud rate means the chip is
not answering at all. That is a power or cabling problem, not something upload
settings will fix. In order of likelihood:

1. **The badge is switched off.** By far the most common cause.
2. Flat battery — the ESP32 may not come up even with USB attached.
3. Manual bootloader entry, if your board exposes the pins: hold **BOOT** (GPIO0),
   tap **EN/RST**, then release BOOT once the upload begins.

**Do not hold the badge button while flashing.** It is on GPIO12, which is MTDI,
an ESP32 strapping pin — see [The button is active HIGH](#the-button-is-active-high).

Built clean against esp32 core 3.3.11 with Adafruit NeoPixel 1.15.5:
308 KB flash (23%), 24 KB RAM (7%).

---

## Controls

| Input | Action |
|---|---|
| Short press | Next animation |
| Hold (after 0.7s) | Ramp brightness, turning round at each end |
| Press, then press and hold | Ramp the house colour |

The colour gesture undoes its own side effect: the short press that arms it steps
the animation on, and the hold steps it back, so you land on the animation you
started from. Two ordinary short presses inside the window still advance twice,
and a hold after the window has expired falls through to brightness.

Both give a readout on the badge itself: a bar around the perimeter for
brightness, or a count of lit pixels from the bottom-right corner for the
animation number. Both count in perimeter order, so they run down the edge
rather than turning off into a board's leg partway along.

### The button is active HIGH

The original sketch used `INPUT_PULLUP` and treated LOW as pressed. On this
hardware that is backwards, and it is why the original carries the comment
*"There is an issue on how mode is being set. The code starts at 1."*

GPIO12 is MTDI, an ESP32 strapping pin that must be low at boot or the chip sets
`VDD_SDIO` to 1.8 V and will not start from 3.3 V flash. The board therefore has
an external pull-down on it, and that pull-down beats the chip's ~45 kΩ internal
pull-up: with `INPUT_PULLUP` the pin reads LOW forever, pressed or not. A GPIO
scan confirms it — every other input idles at 1, GPIO12 alone sits at 0 even
with the internal pull-up enabled.

So the original never actually read the button. It saw one HIGH→LOW edge at
startup, advanced to mode 1, and never registered another press. This firmware
uses `INPUT_PULLDOWN` and treats HIGH as pressed (`BUTTON_ACTIVE_HIGH`).

If you are adapting this for a board that really is wired active-low, set
`BUTTON_ACTIVE_HIGH` to 0 — `serviceButton()` normalises to pressed/not-pressed
at the top, so nothing else needs touching.

## Animations

| # | Name | Family |
|---|---|---|
| 0 | Boot Sequence | What the badge wakes up to |
| 1 | Breathe | Ambient — the original `breathingBadge()`, finished |
| 2 | Drift | Ambient |
| 3 | Plasma | Ambient |
| 4 | Comet | Perimeter motion |
| 5 | Collide | Perimeter motion — two travellers head on; the eye holds their two colours |
| 6 | Rainbow | Perimeter motion |
| 7 | Corner Pulse | Perimeter motion |
| 8 | Charge & Fire | Eye-driven — wind up, a beat of dark, then the whole badge |
| 9 | Scanner | Eye-driven |
| 10 | Aperture | Fragments — the boards close like iris blades, then the flash fires |
| 11 | Fragment Chain | Fragments — the `DOUT`→`DIN` data path, made visible |
| 12 | Vortex | Spiral — three arms winding inward, one per board |
| 13 | Radar | Spiral — one beam sweeping, with a decaying wake |
| 14 | Matrix Rain | Glitch — the badge as a wall, drops falling through it |

Both spirals turn clockwise seen from the front.

Both spirals turn clockwise seen from the front.

Both spirals turn clockwise seen from the front.

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

**The button is read correctly.** The original had it wired backwards for this
hardware and never actually registered a press — it saw one edge at startup and
nothing after. Its own comment records the symptom. See
[The button is active HIGH](#the-button-is-active-high).

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
scaled at output. Brightness is continuous rather than a few preset steps: hold
the button and it ramps, turning round at each end so one button covers both
directions — about 58 steps from `BRIGHT_MIN` to `BRIGHT_MAX`, 2.3 s end to end.
Each step is proportional to where it already is, roughly 6%, because the eye
reads brightness as a ratio and not a difference: a jump of 4 is enormous down
at 8 and invisible up at 140. A current
limiter estimates draw from the channel sum and scales the whole frame down if
it would exceed `POWER_LIMIT_MA` (default 700 mA) — 65 WS2812s at full white is
about 3.9 A, which nothing on this badge wants to supply.

Note that at the lower brightness levels an 8-bit PWM output has real limits.
Gamma runs before the brightness scale, so with the cap at 45/255 the chain is
`fb 45 -> gamma 6 -> output 1`, and `fb 80 -> gamma 20 -> output 3`. **Anything
authored below roughly 80 does not reach the LED at all.**

The same power law bites colour. It crushes the minor channels much harder than
the dominant one, so authoring 55 red against 255 green does not give 22% red at
the LED — it gives about 3%, and what should be a sage green comes out neon. The
house ink is generated from a selectable hue at `INK_SAT` 131, landing on
0.35 : 1.00 : 0.21 at the strip. Scaling all three channels together preserves
the ratio, so a tint survives being dimmed.

There is deliberately no paler variant of the ink. Anything desaturated far
enough to sit between the ink and white just reads as *white* on an emissive
LED — which is exactly what the eye and its wash looked like before. The eye
stands apart by being brighter, not by being washed out.

This is also the easiest way to write an animation that looks right in the
framebuffer and is invisible on the badge — a dim background field authored at
45–130 measures as "busy" and lands as 57% of the LEDs completely dark. If an
effect needs a floor that the viewer can actually see, put it at 70 or above and
measure at the strip, not in `fb[]`.

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
| **Position** | `ringX` / `ringY` | No ordering at all — the badge is a screen and an LED is lit by where it sits. |
| **Polar** | `polR` / `polA` | Position again, but about the eye — radius and angle, for anything that turns or winds. |

Anything that reads as **motion along an edge** belongs in perimeter order. A
head walking strand order veers into the middle of the badge every twenty LEDs,
does four, and pops back out. Comet, Collide, Rainbow, Corner Pulse, Matrix Rain,
Boot Sequence and the discharge half of Charge & Fire all use `perimPoint()`.

`spillTail()` lets those effects run down a board's four tail LEDs as they pass
its attachment point, so the inner LEDs join the motion instead of sitting dead
through every chase.

Plasma and Scanner are **positional** — they never referenced order in the first
place, which is why they never had this problem.
Matrix Rain is positional too, and the clearest case for it: the badge is a wall
and a drop falls straight down through it at a constant speed *in space*, not
from LED to LED. So the delay between two lit LEDs is however long the empty
board between them takes to cross. LEDs 49 and 59 share the column at x=38 with
fifty height-units of nothing between them — about 1.8 s at drop speed — while
59 to 8 is twelve units and takes 450 ms. Outline and tails are both part of the
wall; only the eye is excluded.
Fragment Chain is the one animation that genuinely wants **strand order**: it is
drawing the data path.

Both Spiral animations are **polar**. Because the badge has genuine radial
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

**There is a blue power LED behind the eye, and it is always on.** So "off" is
not a colour the badge can show there — an eye left dark does not read as dark,
it reads as blue. `floorEye()` lifts everything bound for the strip to
`EYE_FLOOR`: a tint already in place scales up with its hue intact, and an eye
left completely dark takes the house green instead of the power LED's blue.
Raise `EYE_FLOOR` if blue still shows through.

**LEDs 63 and 64 are aimed down into the white of the eye.** They are wash
lights, not point accents: whatever colour they carry becomes the colour of the
sclera, which makes them the highest-leverage pair on the badge for selling
"the eye is awake". Give them the eye's colour, never the board's — a teal pair
over a warm eye turns the whole white teal and it stops reading as an eye. Every
animation drives them that way — `Collide` goes furthest with it, putting one
traveller's colour on each side so the two halves of the white glow in the two
colours that are about to hit.
- `TRI_ASPECT` is the triangle's height over its half-width, set to 1.732 for
  equilateral (photos measure ≈1.72). It sets the vertical scale for `polR`, so
  the spirals are what notice if it is wrong.

The tail geometry is measured off a photograph of a single board and reproduces
it to within a tenth of an LED-spacing. Both consequences are confirmed against
the hardware: **17 LEDs from one corner LED to the next inclusive**, and **12
LEDs sitting inside the outline** in addition to the five at the eye.

Turn on *Trace each board's 20 LEDs* in the test bench to see the modelled path
for each board next to the real thing.

---

Original conference sketch by [Kredence](https://github.com/Kredence/DC32_Fragments),
itself adapted from Adafruit's NeoPixel example.
