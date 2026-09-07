/* ============================================================================
 *  FRAGMENTS -- DC32 Illuminati Party(R) badge
 *  Custom animation firmware   |   fork: neednotapply/DC32_Fragments_NNA
 *
 *  Original conference sketch by Kredence (kept alongside this file as
 *  ConferenceCode_v1), itself a hacked-up copy of Adafruit's NeoPixel demo.
 *  This is a full rewrite around a non-blocking frame engine and an RGB
 *  framebuffer, with 17 animations.
 *
 *  ---------------------------------------------------------------------------
 *  HARDWARE
 *  ---------------------------------------------------------------------------
 *  ESP32 Dev Module, 65 WS2812-family LEDs on GPIO13, button to GND on GPIO12.
 *
 *  The strand is NOT homogeneous:
 *      LEDs  0..59   board perimeter,     RGB byte order
 *      LEDs 60..64   eye + top-of-board,  GRB byte order (different vendor)
 *
 *  The original sketch coped with that by hand-swapping R and G in every
 *  color constant bound for 60..64 (hence the rightAngleGreen/rightAngleRed
 *  naming). Here it is handled in exactly one place -- pushFrame() -- so every
 *  animation below can just think in plain RGB. If your board's split sits
 *  somewhere other than 60, move GRB_FIRST and everything follows.
 *
 *  ---------------------------------------------------------------------------
 *  GEOMETRY
 *  ---------------------------------------------------------------------------
 *  60 perimeter LEDs, corners exactly 20 apart at strand indices 3, 23, 43:
 *
 *                          43  apex
 *                          /\
 *            side 2       /  \      side 1
 *        (apex -> BR)    /    \    (BL -> apex)
 *         r = 40..59    /      \    r = 20..39
 *                      /        \
 *                  23 ------------ 3
 *                (BL)    side 0    (BR)
 *                     (BR -> BL)
 *                      r = 0..19
 *
 *  "r" is the ring position: r = (i - CORNER_BR + 60) % 60, so r counts 0..59
 *  in strand order starting from the bottom-right corner. Increasing strand
 *  index runs BR -> BL -> apex -> BR: clockwise, apex up, viewed from the
 *  front. (Corner labels are taken from the original sketch's comments.)
 *
 *  buildGeometry() turns that into ringX / ringY lookup tables, so
 *  animations can be written in space -- "sweep upward", "ripple out from the
 *  eye" -- instead of in raw strand indices.
 *
 *  ---------------------------------------------------------------------------
 *  CONTROLS
 *  ---------------------------------------------------------------------------
 *      short press ............ next animation
 *      hold (0.7s, repeating) . cycle brightness, brightest wraps to dimmest
 *
 *  Both give a brief on-badge readout: a bar around the perimeter showing the
 *  brightness level, or a count of lit pixels showing the animation number.
 * ============================================================================
 */

#include <Adafruit_NeoPixel.h>
#include <esp_system.h>

// ---------------------------------------------------------------------------
// Configuration -- the knobs worth touching
// ---------------------------------------------------------------------------
#define BUTTON_PIN        12
#define PIXEL_PIN         13
#define PIXEL_COUNT       65

#define GRB_FIRST         60    // first LED of the GRB-ordered run
#define RING_COUNT        60    // perimeter LEDs, strand indices 0..59
#define FRAG_COUNT         3    // the badge is three chained boards...
#define FRAG_LEN          20    // ...of twenty LEDs each; see fragOf() below

#define CORNER_BR          3    // bottom-right corner  (ring r = 0)
#define CORNER_BL         23    // bottom-left corner   (ring r = 20)
#define CORNER_TOP        43    // apex                 (ring r = 40)

#define EYE_C             60    // eye, center
#define EYE_L             61    // eye, left
#define EYE_R             62    // eye, right
#define TOP_L             63    // above the eye, aimed DOWN into the white
#define TOP_R             64    // above the eye, aimed DOWN into the white

// 63 and 64 are not point accents. They are aimed down at the sclera, so they
// wash the whole white of the eye and whatever colour they carry becomes the
// colour of the eye. Give them the eye's colour, never the board's: a teal pair
// over a warm eye turns the sclera teal and the eye stops reading as an eye.
// They are also the highest-leverage LEDs on the badge for "the eye is awake".

#define START_MODE         0    // animation to boot into
#define START_BRIGHT      45    // where brightness sits at power-on
#define AUTO_CYCLE_MS      0    // >0 advances animations by itself, e.g. 30000

// Brightness is continuous rather than a handful of steps: holding the button
// ramps it, and the ramp turns round at each end so one button covers both
// directions. Hold, watch the badge, let go when it looks right.
//
// The ceiling is low on purpose. 65 WS2812s at full tilt is ~3.9A, which no coin
// cell or USB port is going to enjoy, and 160 is already eye-searing indoors.
#define BRIGHT_MIN         4
#define BRIGHT_MAX       160
#define BRIGHT_RAMP_MS    40    // how often the ramp takes a step while held

// Belt-and-braces current cap applied after brightness, so cranking the badge
// up on a white-heavy animation dims gracefully instead of browning out the
// ESP32. Set to 0 to disable. ~60mA per LED at full white == 765 channel units.
#define POWER_LIMIT_MA   700

// ---------------------------------------------------------------------------
// The badge's house green
//
// Greenback green -- the shade the Great Seal and the back of the dollar are
// printed in -- rather than the neon a WS2812 hands you if you simply ask for
// green. It wants some red for warmth and a little blue to keep it off acid.
//
// The numbers below look far too pale for that, and they have to be. Gamma runs
// before the brightness scale and it is a power law, so it crushes the minor
// channels much harder than the dominant one: authoring 55 red against 255 green
// does not give 22% red at the LED, it gives about 3%. To land on a drive ratio
// near 0.35 : 1.00 : 0.20 the source has to sit up at 159 : 255 : 124.
//
// Scaling the whole colour preserves the ratio -- a power law scales all three
// channels alike -- so these survive being dimmed by a breath or a fade.
#define GREEN_R      159        // the ink
#define GREEN_G      255
#define GREEN_B      124

#define GREEN_PALE_R 216        // the same ink thinned, for the eye it lights
#define GREEN_PALE_G 255
#define GREEN_PALE_B 199

#define USE_GAMMA          1    // gamma-correct output; fades look far better
#define GAMMA_EXP       2.2f    // see GAMMA[] below

// This badge idles GPIO12 LOW and the button pulls it HIGH.
//
// GPIO12 is MTDI, an ESP32 strapping pin that has to be low at boot or the chip
// sets VDD_SDIO to 1.8V and will not start from 3.3V flash. So the board carries
// an external pull-down, and that pull-down beats the chip's ~45k internal
// pull-up: with INPUT_PULLUP the pin reads LOW forever whether or not anyone is
// touching the button.
//
// The original sketch assumed active-low, and its own comment records the
// symptom -- "the code starts at 1" -- because a permanently-low pin fires
// exactly one HIGH->LOW edge at startup and then never another. Set this to 0
// if you have a board that really is wired active-low.
#define BUTTON_ACTIVE_HIGH 1

#define BTN_DEBOUNCE_MS   25
#define BTN_LONG_MS      700

Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_RGB + NEO_KHZ800);

// ---------------------------------------------------------------------------
// Framebuffer
//
// Animations write plain RGB here at the full 0..255 range and ignore both the
// master brightness and the GRB run. pushFrame() applies gamma, brightness,
// the current limiter and the byte-order fixup on the way to the strip.
// ---------------------------------------------------------------------------
uint8_t  fb[PIXEL_COUNT][3];
uint8_t  scratch[PIXEL_COUNT];      // per-animation scratch (candle, glitch, ...)

uint32_t gNow      = 0;             // millis() at the top of this frame
uint32_t gFrame    = 0;             // frames since the current animation started
uint8_t  gMode     = START_MODE;
uint8_t  gBright   = START_BRIGHT;
int8_t   gBrightDir = 1;            // which way the next ramp goes

// ---------------------------------------------------------------------------
// Small integer helpers
// ---------------------------------------------------------------------------
uint8_t SIN8[256];                  // 0..255 in, 0..255 out, centered on 128
uint8_t GAMMA[256];                 // perceptual 0..255 in, LED drive 0..255 out

// Adafruit_NeoPixel::gamma8() is a fixed 2.6 curve. That is a lot of correction
// to spend when the master brightness already caps output near 45/255: mid
// range values collapse onto 12 or 13 and most of the resolution in a fade is
// gone before it reaches the LED. 2.2 keeps fades smooth without flattening
// everything below half. Raise it if the badge looks washed out to you.

inline uint8_t sin8(uint8_t t)             { return SIN8[t]; }
inline uint8_t cos8(uint8_t t)             { return SIN8[(uint8_t)(t + 64)]; }
inline uint8_t scale8(uint8_t v, uint8_t s){ return ((uint16_t)v * ((uint16_t)s + 1)) >> 8; }
inline uint8_t qadd8(uint8_t a, uint8_t b) { uint16_t t = (uint16_t)a + b; return t > 255 ? 255 : (uint8_t)t; }
inline uint8_t qsub8(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : 0; }

// Sharpen a 0..255 curve toward its peaks -- turns a soft sine into a defined
// band. Used by anything that wants a "front" rather than a gradient.
inline uint8_t sharpen(uint8_t v)          { v = scale8(v, v); return scale8(v, v); }

inline uint8_t ringPos(uint8_t i)          { return (uint8_t)((i + RING_COUNT - CORNER_BR) % RING_COUNT); }
inline uint8_t ringIdx(uint8_t r)          { return (uint8_t)((r + CORNER_BR) % RING_COUNT); }

// The badge is three separate boards chained DOUT -> DIN, 20 LEDs each, which
// is where the 20 in the corner spacing comes from. Each board carries one
// corner: 3 LEDs running up to it, the corner LED itself at local position 3,
// then 16 more along the next edge. So a fragment is a chevron wrapping a
// corner, and the seams fall three LEDs BEFORE each corner in strand order
// (between 19|20, 39|40 and 59|0) rather than at the corners themselves.
// That is the twist that pinwheels three boards into one triangle.
inline uint8_t fragOf(uint8_t i)           { return (uint8_t)(i / FRAG_LEN); }
inline uint8_t posInFrag(uint8_t i)        { return (uint8_t)(i % FRAG_LEN); }
inline uint8_t fragCorner(uint8_t f)       { return (uint8_t)(f * FRAG_LEN + CORNER_BR); }

/* ---------------------------------------------------------------------------
 *  THREE WAYS TO WALK THE BADGE -- pick the one the effect actually means.
 *
 *  1. Strand order, 0..59. What the wire does. It runs an edge, then dives off
 *     the outline into that board's tail, then jumps back out to the next
 *     board's leg. Fragment Chain wants precisely this. Anything that is meant
 *     to read as motion ALONG AN EDGE does not: a head walking strand order
 *     veers into the middle of the badge every twenty LEDs.
 *
 *  2. Perimeter order, PERIM[0..47]. The 48 outline LEDs in the order your eye
 *     walks them, corner BR onward. This is where chases belong.
 *
 *  3. Position -- ringX / ringY, or polR / polA. No ordering at all; the badge
 *     screen and an LED is lit by where it sits. Plasma, Eye Pulse and Scanner
 *     already work this way, which is why they never had the problem.
 * ------------------------------------------------------------------------- */
#define PERIM_COUNT 48
uint8_t PERIM[PERIM_COUNT];       // outline LEDs, spatial order from corner BR
uint8_t perimPos[PIXEL_COUNT];    // inverse; 255 for the 12 tail LEDs

inline uint8_t perimGap(uint8_t a, uint8_t b) {
  uint8_t d = (uint8_t)((a + PERIM_COUNT - b) % PERIM_COUNT);
  return d > PERIM_COUNT / 2 ? (uint8_t)(PERIM_COUNT - d) : d;
}

void buildPerimeter() {
  uint8_t q = 0;
  for (uint8_t e = 0; e < 3; e++) {
    uint8_t n = (uint8_t)((e + 1) % 3);
    for (uint8_t k = 0; k <= 12; k++) PERIM[q++] = (uint8_t)(e * FRAG_LEN + CORNER_BR + k);
    for (uint8_t k = 0; k <  3; k++)  PERIM[q++] = (uint8_t)(n * FRAG_LEN + k);
  }
  memset(perimPos, 255, sizeof(perimPos));
  for (uint8_t i = 0; i < PERIM_COUNT; i++) perimPos[PERIM[i]] = i;
}

// Shortest distance between two ring positions, 0..30.
inline uint8_t ringGap(uint8_t a, uint8_t b) {
  uint8_t d = (uint8_t)((a + RING_COUNT - b) % RING_COUNT);
  return d > RING_COUNT / 2 ? (uint8_t)(RING_COUNT - d) : d;
}

// ---------------------------------------------------------------------------
// Framebuffer operations
// ---------------------------------------------------------------------------
void fbClear() { memset(fb, 0, sizeof(fb)); }

inline void fbSet(uint8_t i, uint8_t r, uint8_t g, uint8_t b) {
  fb[i][0] = r; fb[i][1] = g; fb[i][2] = b;
}

inline void fbAdd(uint8_t i, uint8_t r, uint8_t g, uint8_t b) {
  fb[i][0] = qadd8(fb[i][0], r);
  fb[i][1] = qadd8(fb[i][1], g);
  fb[i][2] = qadd8(fb[i][2], b);
}

// Mix toward (r,g,b) by alpha/255.
inline void fbBlend(uint8_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
  uint8_t inv = 255 - alpha;
  fb[i][0] = qadd8(scale8(fb[i][0], inv), scale8(r, alpha));
  fb[i][1] = qadd8(scale8(fb[i][1], inv), scale8(g, alpha));
  fb[i][2] = qadd8(scale8(fb[i][2], inv), scale8(b, alpha));
}

// keep = 255 holds the frame, keep = 0 wipes it. Anything between leaves a tail.
void fbFade(uint8_t from, uint8_t to, uint8_t keep) {
  for (uint8_t i = from; i < to; i++) {
    fb[i][0] = scale8(fb[i][0], keep);
    fb[i][1] = scale8(fb[i][1], keep);
    fb[i][2] = scale8(fb[i][2], keep);
  }
}
inline void fbFadeRing(uint8_t keep) { fbFade(0, RING_COUNT, keep); }
inline void fbFadeAll(uint8_t keep)  { fbFade(0, PIXEL_COUNT, keep); }

void fbFill(uint8_t from, uint8_t to, uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t i = from; i < to; i++) fbSet(i, r, g, b);
}

// Lay a colour down at a given level. Scaling all three channels together keeps
// the hue put, which straight fbSet with a hand-scaled channel does not.
inline void fbTint(uint8_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t v) {
  fbSet(i, scale8(r, v), scale8(g, v), scale8(b, v));
}

// hue is the full 16-bit wheel, matching Adafruit_NeoPixel::ColorHSV.
inline void fbSetHSV(uint8_t i, uint16_t hue, uint8_t sat, uint8_t val) {
  uint32_t c = strip.ColorHSV(hue, sat, val);
  fbSet(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

inline void fbAddHSV(uint8_t i, uint16_t hue, uint8_t sat, uint8_t val) {
  uint32_t c = strip.ColorHSV(hue, sat, val);
  fbAdd(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// Deposit a sub-pixel-positioned point on the ring, anti-aliased across the two
// LEDs it falls between. p is in 1/16ths of a pixel, 0 .. RING_COUNT*16 - 1.
void ringPoint(uint16_t p, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t whole = (uint8_t)((p >> 4) % RING_COUNT);
  uint8_t frac  = (uint8_t)(p & 15);
  uint8_t wB    = (uint8_t)(frac * 17);
  uint8_t wA    = (uint8_t)(255 - wB);
  fbAdd(ringIdx(whole),                            scale8(r, wA), scale8(g, wA), scale8(b, wA));
  fbAdd(ringIdx((uint8_t)((whole + 1) % RING_COUNT)), scale8(r, wB), scale8(g, wB), scale8(b, wB));
}

// As ringPoint, but walking the outline, so a moving head follows the edge
// instead of diving into a board's tail every twenty LEDs.
void perimPoint(uint16_t p, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t whole = (uint8_t)((p >> 4) % PERIM_COUNT);
  uint8_t frac  = (uint8_t)(p & 15);
  uint8_t wB    = (uint8_t)(frac * 17);
  uint8_t wA    = (uint8_t)(255 - wB);
  fbAdd(PERIM[whole], scale8(r, wA), scale8(g, wA), scale8(b, wA));
  fbAdd(PERIM[(uint8_t)((whole + 1) % PERIM_COUNT)], scale8(r, wB), scale8(g, wB), scale8(b, wB));
}

// Each board's four tail LEDs hang off perimeter position 12 of its own edge.
// Something travelling the outline can spill down them as it passes, so the
// inner LEDs join the motion instead of sitting dead through every chase.
void spillTail(uint16_t headP, uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t f = 0; f < FRAG_COUNT; f++) {
    uint16_t anchor = (uint16_t)((f * 16 + 12) * 16);
    uint16_t d = (uint16_t)((headP + PERIM_COUNT * 16 - anchor) % (PERIM_COUNT * 16));
    if (d >= 4 * 16) continue;
    uint8_t k = (uint8_t)(d >> 4);
    uint8_t w = (uint8_t)(255 - (d & 15) * 16);
    fbAdd((uint8_t)(f * FRAG_LEN + 16 + k), scale8(r, w), scale8(g, w), scale8(b, w));
  }
}

// ---------------------------------------------------------------------------
// Geometry lookup tables
// ---------------------------------------------------------------------------
int8_t  ringX[RING_COUNT];      // -100 (left) .. +100 (right)
int8_t  ringY[RING_COUNT];      //    0 (bottom edge) .. 100 (apex)
// Polar coordinates about the eye, for every LED including the five inside.
// The badge has real radial structure now -- outline at the rim, the twelve
// tail LEDs partway in, the eye at the middle -- so anything that wants to turn
// or wind can work in these instead of in position or order.
uint8_t polR[PIXEL_COUNT];      // 0 at the eye, ~251 at a corner
uint8_t polA[PIXEL_COUNT];      // angle about the eye, 0..255 is one full turn

// The five aux LEDs live inside the triangle rather than on it, so their
// positions are declared rather than derived. Same coordinate system as above.
// If the eye or top LEDs sit somewhere else on your board, edit these two rows.
// Measured off photographs of the lit badge. LEDs 61/62 sit near the outer
// ends of the eye graphic, much wider apart than the pupil; 63/64 are not up
// by the apex at all despite the "top of board" label in the original sketch --
// they flank the eye from above, inside the mandala.
const int8_t auxX[5] = {   0, -32,  32, -29,  28 };   // EYE_C, EYE_L, EYE_R, TOP_L, TOP_R
const int8_t auxY[5] = {  41,  41,  41,  50,  51 };

#define EYE_CX  0
#define EYE_CY 41

// Each board's twenty LEDs, walked in strand order:
//     3 up the previous edge, the corner LED, 12 down the next edge,
//     then a turn and two LEDs, then another turn and two more.
// So along any edge the straight run covers positions 0..12 (this board) and
// 17..20 (the next board's leg plus its corner), and positions 13..16 -- the
// tail of the board that owns the edge -- step off the line.
//
// TAIL_A1 and TAIL_A2 are the two turn angles in degrees, relative to the edge
// direction, positive swinging away from the middle of the badge. Measured off
// a photograph of a single board: both turns come out at very close to 60 deg
// in the same direction, so together they turn the run through 120 deg -- the
// exterior angle of an equilateral corner. The tail rounds the corner on a
// two-LED chamfer instead of meeting it as a point, which is what lets the next
// board's own point sit at that same corner and overlap it.
// Set both to zero to go back to treating the perimeter as three straight runs.
#define TAIL_A1  -60.0f
#define TAIL_A2  -60.0f

// LED spacings from one corner to the next. Not 20: the four tail LEDs leave
// the perimeter, so an edge carries its corner, this board's twelve, and the
// next board's three -- sixteen steps. Three edges of sixteen is 48 LEDs on the
// outline, plus the 12 tail LEDs set inside, which is the 60. Assuming 20 here
// is what left a four-LED hole in the middle of every edge.
#define EDGE_STEPS 16.0f

// Triangle height divided by half-width. 1.732 is equilateral; measure your
// board and change it if the outline is taller or squatter than that.
#define TRI_ASPECT 1.732f

inline int8_t pxX(uint8_t i) { return i < RING_COUNT ? ringX[i] : auxX[i - GRB_FIRST]; }
inline int8_t pxY(uint8_t i) { return i < RING_COUNT ? ringY[i] : auxY[i - GRB_FIRST]; }

void buildGeometry() {
  buildPerimeter();

  // Corners in half-width units, y measured up from the bottom edge. Unlike
  // the ringX/ringY tables this is a true-shape space, so the turns below come
  // out at the right angles.
  const float cx[3] = { 1.0f, -1.0f, 0.0f       };
  const float cy[3] = { 0.0f,  0.0f, TRI_ASPECT };     // BR, BL, apex
  float px[RING_COUNT], py[RING_COUNT];

  for (uint8_t e = 0; e < 3; e++) {
    uint8_t n  = (uint8_t)((e + 1) % 3);               // the next corner round
    float   ax = cx[e], ay = cy[e];
    float   dx = (cx[n] - ax) / EDGE_STEPS;            // one LED of edge
    float   dy = (cy[n] - ay) / EDGE_STEPS;

    // Edge positions 0..12: this edge's corner plus the twelve that follow it.
    for (uint8_t k = 0; k <= 12; k++) {
      uint8_t i = (uint8_t)(e * FRAG_LEN + CORNER_BR + k);
      px[i] = ax + dx * k;  py[i] = ay + dy * k;
    }
    // Edge positions 13..15: the next board's three-LED leg, carrying straight
    // on from this board's run so the perimeter stays evenly spaced.
    for (uint8_t k = 13; k < 16; k++) {
      uint8_t i = (uint8_t)(n * FRAG_LEN + (k - 13));
      px[i] = ax + dx * k;  py[i] = ay + dy * k;
    }
    // The tail. It leaves the perimeter at position 12 -- the next board's leg
    // carries the edge on from there -- so it costs the edge no LEDs of its
    // own. Two turns of two LEDs. Traversal is
    // clockwise, so rotating the edge direction counter-clockwise by a positive
    // angle swings the tail away from the middle of the badge.
    float sxp = ax + dx * 12.0f, syp = ay + dy * 12.0f;
    float a1  = TAIL_A1 * PI / 180.0f;
    float e1x = dx * cosf(a1) - dy * sinf(a1);
    float e1y = dx * sinf(a1) + dy * cosf(a1);

    uint8_t b = (uint8_t)(e * FRAG_LEN + 16);
    px[b]     = sxp + e1x;         py[b]     = syp + e1y;
    px[b + 1] = sxp + e1x * 2.0f;  py[b + 1] = syp + e1y * 2.0f;

    float qx = px[b + 1], qy = py[b + 1];
    float a2 = TAIL_A2 * PI / 180.0f;
    float e2x = e1x * cosf(a2) - e1y * sinf(a2);
    float e2y = e1x * sinf(a2) + e1y * cosf(a2);

    px[b + 2] = qx + e2x;          py[b + 2] = qy + e2y;
    px[b + 3] = qx + e2x * 2.0f;   py[b + 3] = qy + e2y * 2.0f;
  }

  const float eyeX = EYE_CX / 100.0f;
  const float eyeY = (EYE_CY / 100.0f) * TRI_ASPECT;
  for (uint8_t i = 0; i < RING_COUNT; i++) {
    ringX[i] = (int8_t)lroundf(px[i] * 100.0f);            // half-widths
    ringY[i] = (int8_t)lroundf(py[i] / TRI_ASPECT * 100.0f); // fraction of height
  }

  for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
    float x = (i < RING_COUNT) ? px[i] : (auxX[i - GRB_FIRST] / 100.0f);
    float y = (i < RING_COUNT) ? py[i] : ((auxY[i - GRB_FIRST] / 100.0f) * TRI_ASPECT);
    float dx = x - eyeX, dy = y - eyeY;
    float r  = sqrtf(dx * dx + dy * dy) * 205.0f;
    polR[i]  = (uint8_t)(r > 255.0f ? 255.0f : r);
    long  q  = lroundf((atan2f(dy, dx) + PI) * (256.0f / (2.0f * PI)));
    polA[i]  = (uint8_t)(q & 0xFF);
  }

  for (uint16_t i = 0; i < 256; i++) {
    SIN8[i]  = (uint8_t)lroundf(127.5f + 127.4f * sinf(i * (2.0f * PI / 256.0f)));
    GAMMA[i] = (uint8_t)lroundf(powf(i / 255.0f, GAMMA_EXP) * 255.0f);
  }
}

// ---------------------------------------------------------------------------
// Output: gamma -> master brightness -> current limit -> byte order -> strip
// ---------------------------------------------------------------------------
void pushFrame() {
  uint8_t  out[PIXEL_COUNT][3];
  uint32_t sum = 0;
  uint8_t  bright = gBright;

  for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
    for (uint8_t c = 0; c < 3; c++) {
      uint8_t v = fb[i][c];
#if USE_GAMMA
      v = GAMMA[v];
#endif
      v = scale8(v, bright);
      out[i][c] = v;
      sum += v;
    }
  }

#if POWER_LIMIT_MA > 0
  // 765 channel units of white ~= 60mA, so 1 unit ~= 0.0784mA.
  const uint32_t budget = (uint32_t)((POWER_LIMIT_MA * 765UL) / 60UL);
  if (sum > budget) {
    uint16_t k = (uint16_t)((budget * 256UL) / sum);       // 0..255
    for (uint8_t i = 0; i < PIXEL_COUNT; i++)
      for (uint8_t c = 0; c < 3; c++)
        out[i][c] = (uint8_t)(((uint16_t)out[i][c] * k) >> 8);
  }
#endif

  for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
    // The one place the mixed strand is reconciled: LEDs at and above
    // GRB_FIRST take green in the byte the library will send first.
    if (i >= GRB_FIRST) strip.setPixelColor(i, strip.Color(out[i][1], out[i][0], out[i][2]));
    else                strip.setPixelColor(i, strip.Color(out[i][0], out[i][1], out[i][2]));
  }
  strip.show();
}

/* ===========================================================================
 *  ANIMATIONS
 *
 *  Each renders one frame into fb[] and returns. No delay(), no while loops --
 *  the engine decides when the next frame is due, so the button stays live.
 *  gFrame == 0 marks the first frame after a mode change: seed state there.
 * ===========================================================================
 */

// --- Ambient ---------------------------------------------------------------

// The unfinished breathingBadge() from the original, finished. Deep teal board
// on a sine, eye running slightly ahead so it leads the inhale.
void animBreathe() {
  uint8_t phase = (uint8_t)(gNow / 20);              // ~5.1s per breath
  uint8_t b     = sin8(phase);

  uint8_t v = 14 + scale8(b, 120);
  for (uint8_t i = 0; i < RING_COUNT; i++) fbTint(i, GREEN_R, GREEN_G, GREEN_B, v);

  // Corners hold a little more than the edges so the triangle keeps its shape.
  uint8_t cv = 40 + scale8(b, 180);
  fbTint(CORNER_BR,  GREEN_R, GREEN_G, GREEN_B, cv);
  fbTint(CORNER_BL,  GREEN_R, GREEN_G, GREEN_B, cv);
  fbTint(CORNER_TOP, GREEN_R, GREEN_G, GREEN_B, cv);

  // The eye is the same ink thinned, so the badge reads as one colour with the
  // eye as its highlight rather than a white thing sat on a green thing.
  uint8_t e = 25 + scale8(sin8((uint8_t)(phase + 26)), 200);
  fbTint(EYE_C, GREEN_PALE_R, GREEN_PALE_G, GREEN_PALE_B, e);
  fbTint(EYE_L, GREEN_R, GREEN_G, GREEN_B, e);
  fbTint(EYE_R, GREEN_R, GREEN_G, GREEN_B, e);

  uint8_t t = scale8(e, 165);                        // wash the sclera with it
  fbTint(TOP_L, GREEN_PALE_R, GREEN_PALE_G, GREEN_PALE_B, t);
  fbTint(TOP_R, GREEN_PALE_R, GREEN_PALE_G, GREEN_PALE_B, t);
}

// Whole badge drifting through the color wheel, with a slow gradient wrapped
// around the perimeter so it never reads as one flat wash.
void animDrift() {
  uint16_t base = (uint16_t)(gNow * 3);

  // Perimeter order, and the gradient runs against the base so the band travels
  // the other way round the badge.
  for (uint8_t i = 0; i < PERIM_COUNT; i++)
    fbSetHSV(PERIM[i], (uint16_t)(base - (uint16_t)i * 300), 235, 120);
  for (uint8_t f = 0; f < FRAG_COUNT; f++)
    for (uint8_t k = 0; k < 4; k++)
      fbSetHSV((uint8_t)(f * FRAG_LEN + 16 + k),
               (uint16_t)(base - (uint16_t)(f * 16 + 12 + k) * 300), 235, 105);

  uint16_t eyeHue = base + 32768;                    // sits opposite the board
  uint8_t  puls   = 150 + scale8(sin8((uint8_t)(gNow / 22)), 105);
  fbSetHSV(EYE_C, eyeHue, 60, puls);
  fbSetHSV(EYE_L, eyeHue, 120, scale8(puls, 190));
  fbSetHSV(EYE_R, eyeHue, 120, scale8(puls, 190));
  fbSetHSV(TOP_L, eyeHue, 85, scale8(puls, 160));
  fbSetHSV(TOP_R, eyeHue, 85, scale8(puls, 160));
}

// Three sines beating against each other over the badge's real coordinates.
void animPlasma() {
  uint8_t t = (uint8_t)(gNow / 26);

  for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
    int8_t x = pxX(i), y = pxY(i);
    uint8_t a = sin8((uint8_t)(x + t * 2));
    uint8_t b = sin8((uint8_t)(y * 2 - t * 3));
    uint8_t c = sin8((uint8_t)((x + y) + t));
    uint8_t v = (uint8_t)(((uint16_t)a + b + c) / 3);
    fbSetHSV(i, (uint16_t)v * 200 + (uint16_t)(gNow), 220, 45 + scale8(v, 190));
  }
}

// --- Perimeter motion ------------------------------------------------------

// One head orbiting the triangle, tail drawn by the decay of previous frames.
void animComet() {
  static uint16_t p;
  if (gFrame == 0) { p = 0; fbClear(); }

  fbFadeRing(228);
  p = (uint16_t)((p + 9) % (PERIM_COUNT * 16));

  uint16_t hue = (uint16_t)(gNow * 6);
  uint32_t c   = strip.ColorHSV(hue, 200, 255);
  uint8_t  r   = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;

  perimPoint(p, r, g, b);
  // A dimmer leading spark reads as motion blur in the direction of travel.
  perimPoint((uint16_t)((p + 20) % (PERIM_COUNT * 16)), scale8(r, 60), scale8(g, 60), scale8(b, 60));
  spillTail(p, scale8(r, 150), scale8(g, 150), scale8(b, 150));

  uint8_t glow = 90 + scale8(sin8((uint8_t)(p >> 2)), 90);
  fbSetHSV(EYE_C, hue, 160, glow);
  fbSetHSV(EYE_L, hue, 200, scale8(glow, 220));
  fbSetHSV(EYE_R, hue, 200, scale8(glow, 220));
  fbSetHSV(TOP_L, hue, 110, scale8(glow, 205));      // eye glows with the comet
  fbSetHSV(TOP_R, hue, 110, scale8(glow, 205));
}

// Two travellers running the outline head on at different speeds, so the point
// where they meet walks round the badge instead of repeating. Passing a board's
// tail anchor one may turn off down the leg -- four LEDs in, turn round, four
// back out -- and the other goes straight past while it is down there. Every
// fresh collision hands them both new colours.
#define TRAVELERS 2

void animCollide() {
  static uint16_t pos[TRAVELERS];      // perimeter position, in 1/16ths
  static int8_t   dir[TRAVELERS];
  static uint8_t  spd[TRAVELERS];
  static uint8_t  leg[TRAVELERS];      // 255 on the outline, else which tail
  static uint8_t  legProg[TRAVELERS];  // 0..127 through that leg and back
  static uint8_t  cool[TRAVELERS];     // frames before it may turn off again
  static uint16_t hue[TRAVELERS];
  static uint8_t  flash, flashAt;
  static bool     wasHit;

  if (gFrame == 0) {
    fbClear(); flash = 0; flashAt = 0;
    for (uint8_t k = 0; k < TRAVELERS; k++) {
      pos[k]  = (uint16_t)random(PERIM_COUNT * 16);
      dir[k]  = k ? -1 : 1;                        // head on, so they actually meet
      spd[k]  = (uint8_t)(3 + k * 3 + random(0, 3));
      leg[k]  = 255;
      cool[k] = 0;
    }
    hue[0] = (uint16_t)random(65536);
    hue[1] = (uint16_t)(hue[0] + 30000);           // kept well apart from each other
    wasHit = false;
  }

  fbFadeRing(222);

  for (uint8_t k = 0; k < TRAVELERS; k++) {
    hue[k] = (uint16_t)(hue[k] + 8);             // barely drifts; the jump is the event
    uint32_t c = strip.ColorHSV(hue[k], 205, 255);
    uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    if (cool[k]) cool[k]--;

    if (leg[k] != 255) {                             // off the line, down a leg
      legProg[k] = (uint8_t)(legProg[k] + spd[k]);
      if (legProg[k] < 128) {
        uint8_t depth = legProg[k] < 64 ? (uint8_t)(legProg[k] >> 4)          // in
                                        : (uint8_t)(3 - ((legProg[k] - 64) >> 4)); // and back
        fbAdd((uint8_t)(leg[k] * FRAG_LEN + 16 + depth), r, g, b);
        continue;                                    // its position on the line is held
      }
      leg[k]  = 255;                                 // back out where it left
      cool[k] = 24;
    }

    pos[k] = (uint16_t)((pos[k] + PERIM_COUNT * 16 + dir[k] * (int16_t)spd[k])
                        % (PERIM_COUNT * 16));
    perimPoint(pos[k], r, g, b);

    if (!cool[k]) {
      uint8_t here = (uint8_t)(pos[k] >> 4);
      for (uint8_t f = 0; f < FRAG_COUNT; f++)
        if (here == (uint8_t)(f * 16 + 12) && random(100) < 25) {
          leg[k] = f; legProg[k] = 0; break;
        }
    }
  }

  bool hit = false;
  for (uint8_t k = 0; k < TRAVELERS; k++)
    for (uint8_t j = (uint8_t)(k + 1); j < TRAVELERS; j++)
      if (leg[k] == 255 && leg[j] == 255 &&
          perimGap((uint8_t)(pos[k] >> 4), (uint8_t)(pos[j] >> 4)) <= 1) {
        hit = true; flashAt = (uint8_t)(pos[k] >> 4);
      }
  if (hit && !wasHit) {                            // a fresh one, not the same one held
    flash  = 255;
    hue[0] = (uint16_t)(hue[0] + random(13000, 28000));
    hue[1] = (uint16_t)(hue[0] + 26000 + random(0, 12000));
  }
  wasHit = hit;

  if (flash) {
    for (int8_t d = -3; d <= 3; d++) {
      uint8_t w = scale8(flash, (uint8_t)(255 - abs(d) * 70));
      fbAdd(PERIM[(uint8_t)((flashAt + PERIM_COUNT + d) % PERIM_COUNT)], w, w, w);
    }
    flash = scale8(flash, 205);
  }

  // The eye carries the pair that is running: one traveller's colour on each
  // side, and the wash above each side takes the same, so the left and right
  // halves of the white glow in the two colours. The collision flash goes over
  // the top of both, which covers the swap to their new colours -- the hit
  // reads as having produced them.
  for (uint8_t k = 0; k < 2; k++) {
    uint32_t c = strip.ColorHSV(hue[k], 210, 200);
    uint32_t d = strip.ColorHSV(hue[k], 190, 120);
    uint8_t wf = scale8(flash, 205);
    fbSet(k ? EYE_R : EYE_L, qadd8((c >> 16) & 0xFF, flash),
                             qadd8((c >>  8) & 0xFF, flash),
                             qadd8( c        & 0xFF, flash));
    fbSet(k ? TOP_R : TOP_L, qadd8((d >> 16) & 0xFF, wf),
                             qadd8((d >>  8) & 0xFF, wf),
                             qadd8( d        & 0xFF, wf));
  }

  uint8_t e = qadd8(40, flash);
  fbSet(EYE_C, e, e, e);
}

// Hue mapped straight onto perimeter position, rotating. The aux LEDs pick up
// the hue of the nearest corner so the inside of the badge tracks the outside.
void animRainbow() {
  uint16_t base = (uint16_t)(gNow * 14);

  for (uint8_t i = 0; i < PERIM_COUNT; i++)
    fbSetHSV(PERIM[i], base + (uint16_t)i * (65536UL / PERIM_COUNT), 255, 200);
  // Carry the sweep on down each board's tail from where it attaches, so the
  // inner LEDs read as part of the same band rather than a separate colour.
  for (uint8_t f = 0; f < FRAG_COUNT; f++)
    for (uint8_t k = 0; k < 4; k++)
      fbSetHSV((uint8_t)(f * FRAG_LEN + 16 + k),
               base + (uint16_t)(f * 16 + 12 + k) * (65536UL / PERIM_COUNT), 255, 150);

  fbSetHSV(EYE_C, base + 32768, 40, 220);
  fbSetHSV(EYE_L, base,          255, 170);
  fbSetHSV(EYE_R, base + 21845,  255, 170);
  fbSetHSV(TOP_L, base + 32768,  105, 195);
  fbSetHSV(TOP_R, base + 32768,  105, 195);
}

// A pulse leaves each corner in both directions at once. The three pairs meet
// at the edge midpoints and annihilate in a flash.
void animCorners() {
  static uint8_t radius;
  if (gFrame == 0) { radius = 0; fbClear(); }

  fbFadeRing(200);

  uint16_t hue = (uint16_t)(gNow * 4);
  uint8_t  lead = (radius >= 8) ? 0 : (uint8_t)(255 - radius * 15);

  // Corners sit at perimeter 0, 16 and 32; each edge is sixteen steps, so the
  // two halves of a pulse meet at the midpoint eight steps out.
  for (uint8_t c = 0; c < 3; c++) {
    uint8_t base = (uint8_t)(c * 16);
    fbAddHSV(PERIM[base], hue, 200, 120);            // corner keeps a hot core

    if (radius <= 8) {
      uint8_t fwd = (uint8_t)((base + radius) % PERIM_COUNT);
      uint8_t rev = (uint8_t)((base + PERIM_COUNT - radius) % PERIM_COUNT);
      fbAddHSV(PERIM[fwd], hue, 190, qadd8(lead, 40));
      fbAddHSV(PERIM[rev], hue, 190, qadd8(lead, 40));

      if (radius == 8) {                             // the two halves meet
        uint8_t mid = (uint8_t)((base + 8) % PERIM_COUNT);
        fbAdd(PERIM[mid], 255, 255, 255);
        fbAdd(PERIM[(uint8_t)((mid + 1) % PERIM_COUNT)], 120, 120, 120);
        fbAdd(PERIM[(uint8_t)((mid + PERIM_COUNT - 1) % PERIM_COUNT)], 120, 120, 120);
      }
      // The outbound half sweeps past each board's tail on its way.
      spillTail((uint16_t)(((base + radius) % PERIM_COUNT) * 16), 90, 60, 30);
    }
  }

  if (++radius > 11) radius = 0;                     // brief dark beat, then again

  uint8_t e = (radius == 0) ? 220 : (uint8_t)(70 + scale8(sin8((uint8_t)(radius * 18)), 60));
  fbSetHSV(EYE_C, hue, 120, e);
  fbSetHSV(EYE_L, hue, 200, scale8(e, 160));
  fbSetHSV(EYE_R, hue, 200, scale8(e, 160));
  fbSetHSV(TOP_L, hue, 125, scale8(e, 195));
  fbSetHSV(TOP_R, hue, 125, scale8(e, 195));
}

// --- Eye-driven ------------------------------------------------------------

// The eye winds up, dumps into the three corners, and the discharge races the
// edges to meet at the midpoints. Then the whole thing sags and starts over.
void animCharge() {
  const uint16_t CYCLE = 3600;
  uint16_t t = (uint16_t)(gNow % CYCLE);

  fbFadeRing(178);

  if (t < 1500) {                                    // winding up
    uint8_t k = (uint8_t)((t * 255UL) / 1500);
    uint8_t e = scale8(k, k);                        // slow start, hard finish
    fbSet(EYE_C, scale8(e, 200), scale8(e, 240), 255);
    fbSet(EYE_L, scale8(e, 90),  scale8(e, 150), scale8(e, 220));
    fbSet(EYE_R, scale8(e, 90),  scale8(e, 150), scale8(e, 220));

    uint8_t amb = scale8(e, 30);
    for (uint8_t i = 0; i < RING_COUNT; i++) fbAdd(i, 0, scale8(amb, 120), amb);
    fbSet(TOP_L, scale8(e, 125), scale8(e, 185), scale8(e, 245));
    fbSet(TOP_R, scale8(e, 125), scale8(e, 185), scale8(e, 245));

  } else if (t < 1570) {                             // the beat of dark before it goes
    fbFadeAll(96);

  } else if (t < 1850) {                             // FIRE -- the whole badge at once
    uint8_t f = (uint8_t)(255 - ((uint32_t)(t - 1570) * 225) / 280);
    for (uint8_t i = 0; i < RING_COUNT; i++)
      fbSet(i, scale8(f, 190), scale8(f, 230), f);   // blue-white
    fbSet(CORNER_BR, f, f, f);
    fbSet(CORNER_BL, f, f, f);
    fbSet(CORNER_TOP, f, f, f);
    fbFill(GRB_FIRST, PIXEL_COUNT, f, f, f);

  } else if (t < 2750) {                             // the discharge runs the edges
    uint16_t p = (uint16_t)(((uint32_t)(t - 1850) * 136) / 900);
    for (uint8_t c = 0; c < 3; c++) {
      uint16_t base = (uint16_t)(c * 16 * 16);
      perimPoint((uint16_t)((base + p) % (PERIM_COUNT * 16)), 120, 220, 255);
      perimPoint((uint16_t)((base + PERIM_COUNT * 16 - p) % (PERIM_COUNT * 16)), 120, 220, 255);
      spillTail((uint16_t)((base + p) % (PERIM_COUNT * 16)), 70, 130, 150);
    }
    uint8_t e = (uint8_t)(255 - ((uint32_t)(t - 1850) * 215) / 900);
    fbSet(EYE_C, scale8(e, 200), scale8(e, 240), e);
    fbSet(EYE_L, scale8(e, 80), scale8(e, 120), scale8(e, 200));
    fbSet(EYE_R, scale8(e, 80), scale8(e, 120), scale8(e, 200));
    fbSet(TOP_L, 0, scale8(e, 145), scale8(e, 205));
    fbSet(TOP_R, 0, scale8(e, 145), scale8(e, 205));

  } else {                                           // all the way down, so the loop closes
    fbFadeAll(196);
  }
}

// The eye sweeps left and right while a horizontal line scans the triangle.
// Deliberately sparse -- this one is meant to look like it is watching you.
void animScanner() {
  uint8_t look = sin8((uint8_t)(gNow / 14));         // 0 = hard left, 255 = hard right
  uint8_t line = sin8((uint8_t)(gNow / 9));          // scan height, 0..255

  int16_t lineY = (int16_t)((uint16_t)line * 100 / 255);

  for (uint8_t i = 0; i < RING_COUNT; i++) {
    int16_t d = abs((int16_t)ringY[i] - lineY);
    uint8_t v = d >= 14 ? 0 : (uint8_t)(255 - d * 18);
    fbSet(i, scale8(v, 255), scale8(v, 40), scale8(v, 20));
    fbAdd(i, 6, 0, 0);                               // faint standby ember
  }

  fbAdd(CORNER_BR, 20, 0, 0);
  fbAdd(CORNER_BL, 20, 0, 0);
  fbAdd(CORNER_TOP, 20, 0, 0);

  fbSet(EYE_C, 255, 60, 30);
  fbSet(EYE_L, scale8((uint8_t)(255 - look), 255), scale8((uint8_t)(255 - look), 50), 0);
  fbSet(EYE_R, scale8(look, 255), scale8(look, 50), 0);
  // The white catches the scan as the line sweeps past the eye's own height.
  int16_t dEye = abs((int16_t)EYE_CY - lineY);
  uint8_t wash = dEye >= 22 ? 18 : (uint8_t)(18 + (22 - dEye) * 10);
  fbSet(TOP_L, wash, scale8(wash, 42), scale8(wash, 18));
  fbSet(TOP_R, wash, scale8(wash, 42), scale8(wash, 18));
}

// --- The fragments themselves ----------------------------------------------

// The three boards pinwheel around the center the way iris blades do, and the
// eye graphic is drawn as an aperture, so: a shutter. Each fragment's blade
// sweeps out from its corner until the three meet and the eye goes dark.
void animAperture() {
  const uint16_t CYCLE = 4600;
  uint16_t t = (uint16_t)(gNow % CYCLE);

  uint8_t open;                                      // 0 stopped all the way down, 255 wide
  uint8_t flash = 0;
  if (t < 600)       open = (uint8_t)(255 - (t * 255UL) / 600);          // snaps shut
  else if (t < 1800) {                                                   // shut, and the flash fires
    open = 0;
    uint16_t ph = (uint16_t)((t - 900) % 300);
    if (t >= 900) {
      if (ph < 70)       flash = 255;
      else if (ph < 165) flash = (uint8_t)(255 - ((ph - 70) * 255UL) / 95);
    }
  }
  else if (t < 3400) open = (uint8_t)(((t - 1800) * 255UL) / 1600);      // winds back open
  else               open = 255;

  uint8_t reach = (uint8_t)(2 + scale8((uint8_t)(255 - open), 18));      // 2..20 LEDs of blade
  const uint16_t hue = 7000;                                             // warm brass

  fbFill(0, RING_COUNT, 0, 0, 0);
  for (uint8_t f = 0; f < FRAG_COUNT; f++) {
    for (uint8_t k = 0; k < reach && k < FRAG_LEN; k++) {
      uint8_t d = (uint8_t)(reach - 1 - k);                              // 0 at the leading edge
      uint8_t v = d < 2 ? 255 : (uint8_t)(160 - (d > 14 ? 14 : d) * 7);
      fbSetHSV((uint8_t)(f * FRAG_LEN + k), hue, d < 2 ? 110 : 225, v);
    }
  }

  uint8_t light = scale8(open, open);                // what still gets through
  uint8_t blade = (uint8_t)(255 - open);
  fbSet(EYE_C, light, scale8(light, 235), scale8(light, 205));
  fbSetHSV(EYE_L, hue, 220, scale8(blade, 190));
  fbSetHSV(EYE_R, hue, 220, scale8(blade, 190));
  fbSetHSV(TOP_L, hue, 55, qadd8(scale8(light, 215), scale8(blade, 35)));
  fbSetHSV(TOP_R, hue, 55, qadd8(scale8(light, 215), scale8(blade, 35)));

  if (flash) {
    // Three hard white pops behind the closed shutter. 63/64 wash the sclera,
    // so driving them flat out is what actually blows the eye white.
    fbSet(EYE_C, flash, flash, flash);
    fbSet(EYE_L, flash, flash, flash);
    fbSet(EYE_R, flash, flash, flash);
    fbSet(TOP_L, flash, flash, flash);
    fbSet(TOP_R, flash, flash, flash);
    uint8_t spill = scale8(flash, 55);               // a little bounce onto the blades
    for (uint8_t i = 0; i < RING_COUNT; i++) fbAdd(i, spill, spill, qadd8(spill, 8));
  }
}

// The data path made visible: one packet walks the strand at a steady rate,
// each board lights as it comes up, and the seams flash on the handoff.
void animChain() {
  fbFadeRing(200);

  // Every board is powered and the chain is always carrying data, so there is
  // no start frame and no dead tail. Three packets in flight keep it seamless.
  for (uint8_t i = 0; i < RING_COUNT; i++) fbAdd(i, 0, 14, 6);

  const uint16_t PERIOD = 3300;
  for (uint8_t k = 0; k < 3; k++) {
    uint32_t ph   = (gNow + (uint32_t)k * (PERIOD / 3)) % PERIOD;
    uint16_t h16  = (uint16_t)((ph * (uint32_t)(RING_COUNT * 16)) / PERIOD);
    uint8_t  head = (uint8_t)((h16 >> 4) % RING_COUNT);

    for (uint8_t tail = 0; tail < 7; tail++) {       // the packet and its wake
      uint8_t i = (uint8_t)((head + RING_COUNT - tail) % RING_COUNT);
      uint8_t w = (uint8_t)(255 - tail * 36);
      fbAdd(i, 0, w, scale8(w, 45));
    }
    if (posInFrag(head) == 0) {                      // DOUT -> DIN handoff
      fbAdd((uint8_t)((head + RING_COUNT - 1) % RING_COUNT), 190, 190, 255);
      fbAdd(head, 190, 190, 255);
    }
  }

  uint8_t pulse = sin8((uint8_t)(gNow / 9));
  fbSet(EYE_C, 0, (uint8_t)(170 + scale8(pulse, 85)), 80);
  fbSet(EYE_L, 0, 120, 20);
  fbSet(EYE_R, 0, 120, 20);
  fbSet(TOP_L, 0, (uint8_t)(60 + scale8(pulse, 120)), 30);
  fbSet(TOP_R, 0, (uint8_t)(60 + scale8(pulse, 120)), 30);
}

// --- Spirals ---------------------------------------------------------------
//
// All three work purely in polar coordinates, so they sweep through the outline,
// the tails and the eye as one surface. Phase = a*angle + b*radius + c*time: a
// sets the number of arms, b how tightly they wind, and the signs of b and c
// together decide whether the pattern travels inward or outward.
//
// polA increases counter-clockwise seen from the front, so the sign of the angle
// term sets which way a spiral turns and the sign of the radius term sets
// whether it travels in or out. All three below turn clockwise; SPIN mirrors the
// angle where that is what it takes, without disturbing the radial travel.
//
// The radius coefficient has to stay near 1. The badge only has three radial
// bands (rim 164, tails 133, eye 52), so a coefficient of 2 or 3 wraps the phase
// right round between them and the winding aliases into arbitrary per-band
// offsets instead of reading as a spiral at all.
#define SPIN(a)  ((uint8_t)(255 - (a)))

// Three arms, one per board, winding inward. The rotation eases and stalls and
// briefly runs backwards, so it never settles into a rhythm you can predict.
void animVortex() {
  static uint16_t spin;
  if (gFrame == 0) spin = 0;
  uint8_t rate = sin8((uint8_t)(gNow / 110));
  spin = (uint16_t)(spin + (uint16_t)((int16_t)rate - 96));
  uint8_t t = (uint8_t)(spin >> 3);

  uint16_t hue = 41000 + (uint16_t)(gNow / 4);
  for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
    uint8_t phase = (uint8_t)(SPIN(polA[i]) * 3 - polR[i] - t);
    uint8_t v = sharpen(sin8(phase));
    fbSetHSV(i, (uint16_t)(hue + (uint16_t)polR[i] * 45), 205, 18 + scale8(v, 225));
  }
}

// One beam going round, with a wake that decays behind it. The tails and the
// eye get swept the same as the rim, so the beam crosses the whole badge.
void animRadar() {
  if (gFrame == 0) fbClear();
  fbFadeAll(206);

  uint8_t sweep = (uint8_t)(gNow / 8);
  for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
    uint8_t d = (uint8_t)(sweep - SPIN(polA[i]));    // 0 at the beam, growing behind
    if (d > 30) continue;
    uint8_t v = (uint8_t)(255 - d * 8);
    fbAddHSV(i, 21000, d < 3 ? 80 : 235, v);         // scope green, white at the head
  }
  for (uint8_t i = 0; i < PIXEL_COUNT; i++) fbAdd(i, 0, 7, 2);   // faint standing ground
}

// --- Glitch / hacker -------------------------------------------------------

// Droplets spawn near the apex and run down the two slanted edges, splashing
// when they reach a bottom corner. scratch[] doubles as the trail buffer.
#define DROPS      20   // drops in flight
#define DROP_COL    5   // half-width of a drop's column, in x units. Keep this
                        // narrow: at 5 a drop picks out one column cleanly, and
                        // widening it starts catching the neighbouring edge LEDs
                        // and smearing the fall.
#define DROP_HEAD  48   // head tolerance, in 1/16 height units
#define DROP_TAIL 620   // how far the trail reaches above the head
void animMatrix() {
  // The badge is a wall and the drops fall through it. A drop has a column and a
  // height, moves at a constant speed through SPACE rather than from LED to LED,
  // and lights whatever it passes over -- outline and tails alike. So the time
  // between two lit LEDs is however long the empty board between them takes to
  // cross: LED 49 and LED 59 share the column at x=38 with fifty height-units of
  // nothing between them, and the drop takes that long to get from one to the
  // other. The eye is not part of the wall.
  static int16_t dropX[DROPS], dropY[DROPS];
  static uint8_t dropSpd[DROPS];
  static bool    dropLive[DROPS];

  if (gFrame == 0) { fbClear(); for (uint8_t d = 0; d < DROPS; d++) dropLive[d] = false; }

  // A faint field of glyphs behind the rain. Real matrix rain is mostly dim
  // characters with a few bright streams through it, and it keeps the wall from
  // reading as a void wherever a drop happens not to be.
  fbFill(0, RING_COUNT, 0, 0, 0);
  for (uint8_t i = 0; i < RING_COUNT; i++) {
    if (random(100) < 9) scratch[i] = (uint8_t)random(140, 256);
    else                 scratch[i] = scale8(scratch[i], 230);
    // The floor is the whole trick. Gamma 2.2 against a 45/255 brightness cap
    // crushes anything below about 80 to zero at the LED, so a glyph field
    // authored at 45..130 looks busy in the framebuffer and is invisible on the
    // badge. 70 is roughly the lowest value that still arrives, and holding
    // every wall LED at least there means none of them ever read as dead.
    uint8_t g = (uint8_t)(70 + scale8(scratch[i], 80));
    fbAdd(i, 0, g, scale8(g, 20));
  }

  for (uint8_t d = 0; d < DROPS; d++) {
    if (!dropLive[d]) {
      if (random(100) < 32) {
        int16_t x  = (int16_t)random(-99, 100);
        dropX[d]   = x;
        // Start at the board's own ceiling for that column: the slanted sides
        // put the top of the triangle at 100 - |x|.
        dropY[d]   = (int16_t)((100 - (x < 0 ? -x : x)) * 16 + 48);
        dropSpd[d] = (uint8_t)random(13, 30);
        dropLive[d] = true;
      }
      continue;
    }

    dropY[d] = (int16_t)(dropY[d] - (int16_t)dropSpd[d]);
    if (dropY[d] < -DROP_TAIL) { dropLive[d] = false; continue; }

    for (uint8_t i = 0; i < RING_COUNT; i++) {
      int16_t dx = (int16_t)pxX(i) - dropX[d];
      if (dx < 0) dx = (int16_t)(-dx);
      if (dx > DROP_COL) continue;

      int16_t dy = (int16_t)((int16_t)pxY(i) * 16 - dropY[d]);   // >0 = above the head
      uint8_t v;
      if (dy < -DROP_HEAD)      continue;                        // not reached yet
      else if (dy <= DROP_HEAD) v = 255;                         // the head is on it
      else if (dy <= DROP_TAIL) v = (uint8_t)(230 - ((int32_t)(dy - DROP_HEAD) * 215)
                                                    / (DROP_TAIL - DROP_HEAD));
      else                      continue;

      uint8_t lat = (uint8_t)(255 - ((int32_t)dx * 255) / DROP_COL);
      uint8_t w   = scale8(v, lat);
      if (v == 255) fbAdd(i, scale8(w, 70), w, scale8(w, 105));  // green-white head
      else          fbAdd(i, 0, w, scale8(w, 28));               // green trail
    }
  }

  uint8_t cursor = 32 + scale8(sin8((uint8_t)(gNow / 8)), 200);   // breathes, not blinks
  fbSet(EYE_C, 0, cursor, scale8(cursor, 28));
  fbSet(EYE_L, 0, scale8(cursor, 90), 0);
  fbSet(EYE_R, 0, scale8(cursor, 90), 0);
  fbSet(TOP_L, 0, scale8(cursor, 155), scale8(cursor, 22));
  fbSet(TOP_R, 0, scale8(cursor, 155), scale8(cursor, 22));
}

// Mostly-composed badge with the signal breaking up: dropouts, channel tears,
// and the occasional full-frame corruption.
void animGlitch() {
  uint16_t hue = 34000;

  for (uint8_t i = 0; i < RING_COUNT; i++) {
    uint8_t base = 30 + scale8(sin8((uint8_t)(ringPos(i) * 6 + gNow / 18)), 60);
    fbSetHSV(i, hue, 230, base);
  }

  // Torn segment: a contiguous run jumps to the wrong color for a frame or two.
  if (random(100) < 30) {
    uint8_t start = (uint8_t)random(RING_COUNT);
    uint8_t len   = (uint8_t)random(3, 14);
    uint16_t bad  = random(2) ? 0 : 43000;
    for (uint8_t k = 0; k < len; k++)
      fbSetHSV(ringIdx((uint8_t)((start + k) % RING_COUNT)), bad, 255, (uint8_t)random(120, 256));
  }

  // Dropout: individual LEDs blink out entirely.
  for (uint8_t i = 0; i < RING_COUNT; i++) if (random(100) < 8) fbSet(i, 0, 0, 0);

  // Full-frame corruption, rare and short.
  if (random(1000) < 25) {
    for (uint8_t i = 0; i < RING_COUNT; i++) {
      uint8_t v = (uint8_t)random(60, 256);
      fbSet(i, v, scale8(v, 30), scale8(v, 200));
    }
  }

  bool stutter = random(100) < 18;
  uint8_t e = stutter ? (uint8_t)random(0, 90) : 200;
  fbSetHSV(EYE_C, stutter ? 0 : hue, stutter ? 255 : 40, e);
  fbSetHSV(EYE_L, hue, 220, stutter ? 20 : 130);
  fbSetHSV(EYE_R, hue, 220, stutter ? 130 : 20);
  fbSetHSV(TOP_L, hue, 240, random(100) < 15 ? 0 : 80);
  fbSetHSV(TOP_R, hue, 240, random(100) < 15 ? 0 : 80);
}

// Power-on self test, on a loop: trace the outline, lock the corners, open the
// eye, then three confirmation flashes and a short hold.
void animBoot() {
  const uint16_t CYCLE = 5600;
  uint16_t t = (uint16_t)(gNow % CYCLE);

  fbClear();

  if (t < 1600) {                                    // trace the perimeter
    uint8_t lit = (uint8_t)((t * (uint32_t)PERIM_COUNT) / 1600);
    for (uint8_t r = 0; r <= lit && r < PERIM_COUNT; r++) {
      uint8_t age = (uint8_t)(lit - r);
      uint8_t v   = age > 8 ? 45 : (uint8_t)(255 - age * 26);
      fbSet(PERIM[r], scale8(v, 40), scale8(v, 190), v);
    }
    // Each board's tail fills in behind the trace as it passes the attachment.
    for (uint8_t f = 0; f < FRAG_COUNT; f++) {
      uint8_t anchor = (uint8_t)(f * 16 + 12);
      for (uint8_t k = 0; k < 4; k++) {
        if (lit <= anchor + k) continue;
        uint8_t age = (uint8_t)(lit - anchor - k);
        uint8_t v   = age > 8 ? 45 : (uint8_t)(255 - age * 26);
        fbSet((uint8_t)(f * FRAG_LEN + 16 + k), scale8(v, 40), scale8(v, 190), v);
      }
    }

  } else if (t < 2600) {                             // corners lock in, one by one
    fbFill(0, RING_COUNT, 18, 60, 70);
    uint16_t s = t - 1600;
    const uint8_t corner[3] = { CORNER_BR, CORNER_BL, CORNER_TOP };
    for (uint8_t c = 0; c < 3; c++) {
      if (s > (uint16_t)c * 280) {
        uint16_t age = s - c * 280;
        uint8_t  v   = age > 300 ? 200 : (uint8_t)(255 - (age * 55) / 300);
        fbSet(corner[c], v, v, v);
      }
    }

  } else if (t < 3800) {                             // the eye opens
    fbFill(0, RING_COUNT, 18, 60, 70);
    fbSet(CORNER_BR, 200, 200, 200);
    fbSet(CORNER_BL, 200, 200, 200);
    fbSet(CORNER_TOP, 200, 200, 200);

    uint16_t s = t - 2600;
    uint8_t  side = s > 400 ? 255 : (uint8_t)((s * 255UL) / 400);
    fbSet(EYE_L, scale8(side, 120), scale8(side, 200), scale8(side, 255));
    fbSet(EYE_R, scale8(side, 120), scale8(side, 200), scale8(side, 255));
    if (s > 400) {
      uint8_t c = (uint8_t)(((s - 400) * 255UL) / 800);
      fbSet(EYE_C, c, c, c);
      fbSet(TOP_L, scale8(c, 120), scale8(c, 200), c);
      fbSet(TOP_R, scale8(c, 120), scale8(c, 200), c);
    }

  } else if (t < 4700) {                             // three confirmation flashes
    bool on = ((t - 3800) / 150) % 2 == 0;
    uint8_t v = on ? 220 : 20;
    fbFill(0, RING_COUNT, scale8(v, 40), scale8(v, 200), v);
    fbFill(GRB_FIRST, PIXEL_COUNT, v, v, v);

  } else {                                           // ready, holding
    uint8_t b = 60 + scale8(sin8((uint8_t)((t - 4700) / 3)), 40);
    fbFill(0, RING_COUNT, 0, scale8(b, 210), b);
    fbSet(CORNER_BR, 0, scale8(b, 200), qadd8(b, 40));
    fbSet(CORNER_BL, 0, scale8(b, 200), qadd8(b, 40));
    fbSet(CORNER_TOP, 0, scale8(b, 200), qadd8(b, 40));
    fbFill(GRB_FIRST, PIXEL_COUNT, 150, 200, 230);
  }
}


/* ===========================================================================
 *  ANIMATION TABLE
 *
 *  frameMs sets the pace per animation rather than globally, so the slow
 *  ambient ones are not burning CPU and radio time at 60fps. To reorder the
 *  badge, reorder these rows; to drop one, delete its row.
 * ===========================================================================
 */
struct Anim {
  void (*render)();
  uint16_t frameMs;
  const char *name;
};

const Anim ANIMS[] = {
  { animBreathe,      25, "Breathe"       },   // ambient
  { animDrift,        30, "Drift"         },
  { animPlasma,       28, "Plasma"        },
  { animComet,        18, "Comet"         },   // perimeter motion
  { animCollide,      18, "Collide"       },
  { animRainbow,      22, "Rainbow"       },
  { animCorners,      45, "Corner Pulse"  },
  { animCharge,       18, "Charge & Fire" },   // eye-driven
  { animScanner,      22, "Scanner"       },
  { animAperture,     22, "Aperture"      },   // the three-board construction
  { animChain,        25, "Fragment Chain"},
  { animVortex,       22, "Vortex"        },   // spirals, in polar coordinates
  { animRadar,        20, "Radar"         },
  { animMatrix,       45, "Matrix Rain"   },   // glitch / hacker
  { animGlitch,       70, "Glitch"        },
  { animBoot,         25, "Boot Sequence" },
};
#define ANIM_COUNT (sizeof(ANIMS) / sizeof(ANIMS[0]))

/* ===========================================================================
 *  ON-BADGE READOUT
 *
 *  Briefly replaces the animation after a button action so you can see what
 *  changed without plugging into a serial monitor.
 * ===========================================================================
 */
enum Feedback { FB_NONE, FB_MODE };
Feedback gFeedback   = FB_NONE;
uint32_t gFeedbackTo = 0;

// Only the animation number gets a readout. Brightness deliberately does not:
// while you are ramping you want to see the animation AT that brightness, and a
// bar drawn over the top of it tells you less than the badge itself does.
void showFeedback() {
  fbClear();
  // One lit pixel per animation number, counted up from the bottom-right
  // corner, with the corners kept visible for orientation. Perimeter order, so
  // the count runs down the edge instead of turning off into a board's leg.
  for (uint8_t r = 0; r <= gMode && r < PERIM_COUNT; r++) fbSet(PERIM[r], 255, 140, 0);
  fbAdd(CORNER_BR, 0, 0, 60);
  fbAdd(CORNER_BL, 0, 0, 60);
  fbAdd(CORNER_TOP, 0, 0, 60);
  fbFill(GRB_FIRST, PIXEL_COUNT, 90, 60, 0);
}

/* ===========================================================================
 *  MODE AND BRIGHTNESS
 * ===========================================================================
 */
void setMode(uint8_t m) {
  gMode  = (uint8_t)(m % ANIM_COUNT);
  gFrame = 0;
  fbClear();
  memset(scratch, 0, sizeof(scratch));
  gFeedback   = FB_MODE;
  gFeedbackTo = gNow + 400;
  Serial.printf("[%u/%u] %s\n", gMode, (unsigned)ANIM_COUNT - 1, ANIMS[gMode].name);
}

// One step of the ramp. The step is proportional to where it already is -- about
// 6% -- because the eye reads brightness as a ratio, not a difference: a jump of
// 4 is enormous down at 8 and invisible up at 140. That keeps the ramp feeling
// even the whole way along instead of crawling at the bottom and lurching at the
// top. Roughly 60 steps end to end, so about two and a half seconds.
void rampBrightness() {
  uint8_t step = (uint8_t)(gBright >> 4);
  if (step < 1) step = 1;

  int16_t v = (int16_t)gBright + (int16_t)gBrightDir * (int16_t)step;
  if (v >= BRIGHT_MAX)      { v = BRIGHT_MAX; gBrightDir = -1; }   // turn round
  else if (v <= BRIGHT_MIN) { v = BRIGHT_MIN; gBrightDir =  1; }
  gBright = (uint8_t)v;
}

/* ===========================================================================
 *  BUTTON
 *
 *  Debounced edge detection plus a hold-to-repeat, all non-blocking. The
 *  original delay(20) debounce inside a delay(90) animation meant presses
 *  landed only if you happened to be holding during the read; this samples
 *  every pass of loop().
 * ===========================================================================
 */
bool     btnRaw       = false;      // true means pressed, whatever the pin level
bool     btnStable    = false;
uint32_t btnEdgeAt    = 0;
uint32_t btnDownAt    = 0;
uint32_t btnNextRep   = 0;
bool     btnLongFired = false;

void serviceButton() {
  // Normalised to pressed / not pressed at the top, so nothing below has to
  // care which way round the hardware is.
  bool raw = (digitalRead(BUTTON_PIN) == (BUTTON_ACTIVE_HIGH ? HIGH : LOW));
  if (raw != btnRaw) { btnRaw = raw; btnEdgeAt = gNow; }

  if (raw != btnStable && (gNow - btnEdgeAt) >= BTN_DEBOUNCE_MS) {
    btnStable = raw;
    if (btnStable) {
      btnDownAt    = gNow;
      btnLongFired = false;
    } else if (!btnLongFired) {
      setMode((uint8_t)(gMode + 1));                 // short press
    } else {
      // Report once on release rather than 25 times a second during the ramp.
      Serial.printf("brightness %u/%u\n", gBright, BRIGHT_MAX);
    }
  }

  if (btnStable) {
    if (!btnLongFired && (gNow - btnDownAt) >= BTN_LONG_MS) {
      btnLongFired = true;
      rampBrightness();
      btnNextRep = gNow + BRIGHT_RAMP_MS;
    } else if (btnLongFired && (int32_t)(gNow - btnNextRep) >= 0) {
      rampBrightness();                              // keep holding to keep going
      btnNextRep = gNow + BRIGHT_RAMP_MS;
    }
  }
}

/* ===========================================================================
 *  SETUP / LOOP
 * ===========================================================================
 */
uint32_t gLastFrame = 0;
uint32_t gLastAuto  = 0;

void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(BUTTON_PIN, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  randomSeed(esp_random());

  buildGeometry();

  strip.begin();
  strip.setBrightness(255);        // master brightness is handled in pushFrame
  fbClear();
  strip.clear();
  strip.show();

  gNow       = millis();
  gLastAuto  = gNow;
  setMode(START_MODE);

  Serial.printf("\nFragments: %u animations, brightness %u..%u.\n"
                "  short press = next animation\n"
                "  hold        = ramp brightness, turns round at each end\n",
                (unsigned)ANIM_COUNT, BRIGHT_MIN, BRIGHT_MAX);
}

void loop() {
  gNow = millis();

  serviceButton();

#if AUTO_CYCLE_MS > 0
  if (gNow - gLastAuto >= AUTO_CYCLE_MS) { gLastAuto = gNow; setMode((uint8_t)(gMode + 1)); }
#else
  gLastAuto = gNow;
#endif

  const Anim &a = ANIMS[gMode];
  if (gNow - gLastFrame >= a.frameMs) {
    gLastFrame = gNow;

    if (gFeedback != FB_NONE) {
      if ((int32_t)(gNow - gFeedbackTo) >= 0) {
        gFeedback = FB_NONE;
        fbClear();
        gFrame = 0;                                  // let the animation re-seed
      } else {
        showFeedback();
      }
    }

    if (gFeedback == FB_NONE) {
      a.render();
      gFrame++;
    }

    pushFrame();
  }
}
