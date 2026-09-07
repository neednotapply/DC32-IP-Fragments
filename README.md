# DC32_Fragments

Welcome to the Illuminati Party® badge 'Fragments' for 2024 (DC32).

This fork adds a rewritten animation firmware and a browser test bench for it.
The original conference sketch is preserved unchanged as `ConferenceCode_v1`.

| | |
|---|---|
| `Fragments_NNA/Fragments_NNA.ino` | New firmware. 13 animations, non-blocking, brightness control. |
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
Fragments: 13 animations, brightness 4..160.
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

While you are navigating — stepping animations, ramping brightness, ramping
colour — the whole badge sits lit in the house ink at the set brightness, so a
glance tells you what it is configured to. The animation number rides on top of
that in the **opposite hue**, saturated harder than the ink.

A brighter shade of the ink does not survive the diffuser. With the whole board
lit, neighbouring LEDs blend into each other and a brightness step smears across
the boundary until the count is hard to read; opposite hues stay separate however
much they bleed. Measured after dividing luminance out, the fill and marker keep
4.1–8.2 of chroma separation right round the wheel — green against violet, cyan
against red, blue against yellow. `INK_SAT` and `MARK_SAT` set the two
saturations, `NAV_FILL` and `NAV_MARK` the two levels.

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
| 0 | Boot Sequence | What the badge wakes up to — outline only, legs dark |
| 1 | Breathe | Outline and eye breathing together |
| 2 | Vortex | Three arms winding inward, one per board |
| 3 | Radar | One beam sweeping, with a decaying wake |
| 4 | Corner Pulse | Pulses out from each corner, meeting at the midpoints |
| 5 | Scanner | The eye tracks while a line sweeps |
| 6 | Aperture | The boards close like iris blades, then the flash fires |
| 7 | Fragment Chain | The `DOUT`→`DIN` data path, made visible |
| 8 | Matrix Rain | The badge as a wall, drops falling through it |
| 9 | Drift | Multicoloured |
| 10 | Plasma | Multicoloured |
| 11 | Collide | Multicoloured — two travellers head on |
| 12 | Rainbow | Multicoloured |

**0–8 follow the house colour** — press then hold to change it and they move with
it. **9–12 are multicoloured by design** and ignore the setting, so they are
grouped at the end rather than scattered through the list, where the setting
looked broken every time it landed on one that does not use it.

Everything that goes round the outline turns clockwise seen from the front —
Vortex and Radar, plus Drift and Rainbow. Reversing a gradient means negating
the *position* term rather than time: flipping time on Rainbow would run the hue
wheel backwards instead of moving the band the other way.

Both spirals turn clockwise seen from the front.

Both spirals turn clockwise seen from the front.

The three original conference flickers are not in this firmware. They are still
in `ConferenceCode_v1`, unchanged, if you want them back.

Set `AUTO_CYCLE_MS` to a number of milliseconds to have the badge advance on its
own.

## Settings

Animation, brightness and colour survive a power cycle, kept in NVS.

Writes are held back until things have been quiet for `SETTINGS_SAVE_MS`. NVS
lives in flash and flash wears out; a brightness ramp changes the value
twenty-five times a second, so committing each step would put tens of thousands
of writes through it in an evening of fiddling. Waiting for the quiet turns a
whole ramp into a single write. A record that points past the end of the
animation table — after the list shrinks, say — is discarded rather than used.

## Wireless control

The badge runs a soft access point and serves the test bench itself:

| | |
|---|---|
| Network | `Fragments` |
| Password | `allseeing` |
| Address | `http://192.168.4.1` |

Joining should open the page by itself; there is a captive-portal redirect for
clients that ask. The page pushes changes as you make them and polls for state,
so pressing the physical button shows up in the browser and the other way round.

The page is not a second implementation — `tools/make_webpage.py` takes
`sim/bench.html`, hides the parts that only make sense on a desk, appends the
layer that talks to the badge, gzips it and writes `Fragments_NNA/webpage.h`.
So the preview in your hand runs the identical integer maths the badge does.
Re-run it after editing the bench:

```bash
python3 tools/make_webpage.py
```

**The radio is not free.** A soft AP costs well over a hundred milliamps —
more than every LED at the default brightness put together. `WIFI_ENABLED 0`
compiles it out entirely, and it is overridable from the command line, which is
also how the off-target test harness builds the animation code without pulling
in the network stack.

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
fifty height-units of nothing between them, while 59 to 8 is only twelve.

That gap is also why the trail lives **on the LEDs** rather than in the air. A
column here holds about 3.3 LEDs on average with large voids between them, so
lighting whatever happens to lie just above the head lights almost nothing and
the drop reads as a lone point crossing bare board. Instead a drop *strikes* an
LED as it passes and that LED decays on its own, which makes a column of three
read as three flashes falling in sequence. The same decaying buffer carries the
dim glyph field between drops, so trail and texture are one mechanism.

Drops also take their column from a randomly chosen LED rather than a random x.
Picking x freely drops a third of them down stripes of bare board where nothing
can be struck; seeding from an LED guarantees a target and naturally favours the
busier columns. Outline and tails are both part of the wall; only the eye is out.
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

- `auxX` / `auxY` place the eye (60–62) and the pair above it (63–64), indexed
  by strand position rather than by name. These are measured off photographs of
  the lit badge — note that 63/64 are *not* near the apex despite the original's
  "top of board" comment; they flank the eye from above, inside the mandala.
- **The strand reaches the right of the eye before the left.** The original
  labelled 61 as left and that was wrong: driven that way, Radar's beam reached
  the two sides in the wrong order and the eye read mirrored against the
  simulator. `EYE_R` is 61 and `EYE_L` is 62. Both halves have to agree — the
  naming *and* `auxX`, which is what tells the geometry where each index sits.
- 63/64 are the same class of assumption and have not been confirmed. If the top
  pair ever looks mirrored, swap `auxX[3]` and `auxX[4]`. It is much less visible
  than the eye pair was, since the two sit close together and both wash the same
  sclera.

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
