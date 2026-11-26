// =====================================================================
//  MANIFOLD - EXHIBITION VERSION 13 (500 cm strip + 9 light sensors)
// =====================================================================
//
//  WHAT IT DOES
//  ------------
//  Nobody touching the sensors: the river animation loops forever:
//     1. RIVER              the ideal river: blue waves with sparkles
//     2. RIVER 2            the "real" waves: darker, slower, green in the deepest parts
//     3. FLOODING & SEWAGE  red floods at House Mill, then sewage from 7 inputs:
//                           orange-yellow where it comes in, fading to brown down
//                           the river, with waves of brown and yellow spreading
//                           out from the sewage points
//     4. REWILDING          small white waves appear from several points, then
//                           turn blue; clean waves arrive from both ends and wash
//                           the sewage away, back to the ideal RIVER (stage 1).
//                           Random LEDs in the rewilding area turn dark green.
//
//  Covering a sensor ("adding care"): that sensor's section of the strip
//  turns solid colour while it is covered, and pulses of colour leave it and
//  travel down the river, fading as they go. When it is uncovered the section
//  fades back to the river, and the pulses already sent carry on and fade away.
//  Where they go they clean the pollution: the longer it is held, the cleaner.
//
//  Emma's points (0 = where the River Lea joins the Thames = LED 0):
//     tidal stretch 0-110 cm, House Mill (floods) at 110 cm,
//     sewage inputs at 105, 160, 275, 300, 325, 375, 460 cm,
//     rewilding area 195-240 cm.
//
//  WIRING
//  ------
//   3.3V ---- LDR ----+---- GPIO 1,2,3,4,5,7,8,9,10   (10k from each GPIO to GND)
//   Strip DATA IN -> GPIO 6 (through the 300 ohm resistor)
//   ESP32 GND, strip GND and the Tagra V- all joined together
//
//  SERIAL MONITOR (Tools > Serial Monitor, 115200 baud)
//  ----------------------------------------------------
//   Every 0.3 s: each sensor's reading now / its brightest (max), with a *
//   when it is covered. Every second: which animation is playing.
//
//  QUICK TWEAK GUIDE  (search for "TWEAK" to jump to each one)
//  ------------------
//   First waves (RIVER) ........ RIVER_WAVE_SPEED, RIVER_TROUGH_COLOUR,
//                                RIVER_CREST_COLOUR, RIVER_SPARKLES
//   Real waves (RIVER 2) ....... R2_WAVE_SPEED, R2_TROUGH_COLOUR, R2_CREST_COLOUR,
//                                R2_DEEP_COLOUR, R2_DEEP_AMOUNT
//   Floods / sewage ............ FLOOD_COLOUR, SEWAGE_..._COLOUR, SEWAGE_P1_CM...
//   Sensors .................... COVERED_BELOW, UNCOVERED_ABOVE
//   Care input ................. PULSE_COLOUR, PULSE_TRAIL_COLOUR, PULSE_INTENSITY,
//                                PULSE_SPEED, PULSE_WAVE_FOLLOW, PULSE_WIDTH, PULSE_SPREAD,
//                                PULSE_FADE, PULSE_FADE_IN, PULSE_FADE_OUT, CLEAN_...
// =====================================================================

#include <Adafruit_NeoPixel.h>    // sends the colours to the strip

#ifndef PI
#define PI 3.14159265358979f
#endif
#ifndef TWO_PI
#define TWO_PI 6.28318530717959f
#endif

// =====================================================================
//  HARDWARE
// =====================================================================
#define DATA_PIN          6     // the strip's data wire
#define NUM_LEDS          100   // TWEAK: the number of PIXELS on the 500 cm strip
#define BRIGHTNESS        150   // TWEAK: overall brightness 0-255 (check the power supply stays cool)

#define NUM_LDRS          9
const int LDR_PINS[NUM_LDRS] = { 1, 2, 3, 4, 5, 7, 8, 9, 10 };   // sensor 1 first ... sensor 9 last

// ---------------------------------------------------------------------
//  WHICH PARTS OF THE RIVER EACH SENSOR CONTROLS
//  TWEAK: one line per section: { sensor number, from metres, to metres }
//  Metres are measured from LED 0 (the Thames end); the strip is 5 m long.
//   - give a sensor as many sections as you like (just add more lines with its number)
//   - two sensors can share a section, sections can leave gaps or overlap
//   - a sensor with no lines does nothing
//  Covering a sensor turns ALL its sections solid colour, and pulses leave
//  the end of each of them and travel down the river.
// ---------------------------------------------------------------------
struct Section { int sensor; float fromM, toM; };

 const Section SECTIONS[] = {
  // sensor, from m, to m
  { 1, 0.00, 0.60 },
  { 2, 0.60, 1.07 },
  { 3, 1.08, 1.10 },
  { 4, 1.11, 1.55 },
  { 5, 1.56, 1.8 },
  { 6, 1.95, 2.4 },
  { 7, 2.4, 2.5 },
  { 8, 2.5, 2.6},
  { 9, 2.6, 4.00 },
  { 1, 4.00, 5.00},
  
  // e.g. to give sensor 1 a second section as well, add:  { 1, 1.95, 2.40 },
};
const int NUM_SECTIONS = sizeof(SECTIONS) / sizeof(SECTIONS[0]);

// =====================================================================
//  A SMALL COLOUR TYPE (so the sketch doesn't need FastLED)
//  CRGB(r, g, b): red, green, blue, each 0-255.
// =====================================================================
struct CRGB {
  uint8_t r, g, b;
  CRGB() : r(0), g(0), b(0) {}
  constexpr CRGB(uint8_t rr, uint8_t gg, uint8_t bb) : r(rr), g(gg), b(bb) {}
};

// Memory for a set of sparkles (one per animation that sparkles).
// It is defined up here because the Arduino software needs to know it
// before any function that uses it.
struct Sparkles { float glint[NUM_LEDS]; float target[NUM_LEDS]; float lastT = -1; };

// =====================================================================
//  THE PLACE (Emma's measurements, in cm from the Thames end)
// =====================================================================
const float STRIP_LENGTH_M       = 5.00f;    // real length of the strip, in metres
const float HOUSE_MILL_CM        = 110.0f;   // House Mill: floods start here; left of it is the tidal stretch
const float TIDE_CHANGES_PER_MIN = 2.0f;     // tidal stretch: how often the flow changes direction

// =====================================================================
//  THE SEQUENCE: 0 = RIVER, 1 = RIVER 2, 2 = FLOODING & SEWAGE, 3 = REWILDING
// =====================================================================
#define NUM_ANIMS 4
const float ANIM_SECONDS[NUM_ANIMS]  = { 60.0f, 60.0f, 90.0f, 60.0f };   // TWEAK: how long each one plays
const float LIGHT[NUM_ANIMS]         = { 1.00f, 1.00f, 1.00f, 1.00f };   // TWEAK: brightness of each one (0-1)
// Transition AFTER each animation: 0 = fade, 3 = overlay (new one on top)
const uint8_t TRANS_TYPE[NUM_ANIMS]    = { 0, 3, 0, 0 };
const float   TRANS_SECONDS[NUM_ANIMS] = { 5.0f, 8.0f, 5.0f, 3.0f };   // REWILDING -> RIVER: 3 s (they look the same by then)

// ---------------------------------------------------------------------
//  1. RIVER - the first set of waves (the ideal river)
// ---------------------------------------------------------------------
const float RIVER_WAVE_SPEED      = 1.5f;              // TWEAK: wave speed (1.0 = as on the website, higher = faster)
const CRGB  RIVER_TROUGH_COLOUR   = CRGB(10, 18, 42);  // TWEAK: the darker part BETWEEN the waves: dark, slightly grey blue (less electric). Was (0, 12, 70)
const CRGB  RIVER_CREST_COLOUR    = CRGB(0, 90, 224);  // TWEAK: blue at the top of each wave
const float RIVER_SPARKLES        = 0.40f;             // TWEAK: how many sparkles (0 = none, 1 = lots)
const float RIVER_SPARKLE_SPEED   = 1.00f;             //        how fast sparkles flash and fade
const CRGB  RIVER_SPARKLE_COLOUR  = CRGB(255, 255, 255);
const float RIVER_IRREGULARITY    = 0.00f;             //        0 = even waves, 1 = uneven

// ---------------------------------------------------------------------
//  2. RIVER 2 - the "real" waves
// ---------------------------------------------------------------------
const float R2_WAVE_SPEED         = 0.6f;              // TWEAK: wave speed (1.0 = as on the website, lower = slower)
const CRGB  R2_TROUGH_COLOUR      = CRGB(10, 20, 34);  // TWEAK: the darker part between the waves: darker, slightly grey. Was (0, 25, 70)
const CRGB  R2_CREST_COLOUR       = CRGB(0, 150, 170); // TWEAK: colour at the top of each wave. Was (0, 255, 238)
const CRGB  R2_DEEP_COLOUR        = CRGB(12, 40, 28);  // TWEAK: the green in the deepest parts: darker, slightly grey. Was (0, 60, 25)
const float R2_DEEP_AMOUNT        = 0.70f;             // TWEAK: how much green in the deepest parts (0 = none, 1 = all)
const float R2_DEEP_LEVEL         = 0.30f;             //        how much of each trough counts as "deepest" (0-1)
const float R2_SPACING_CM         = 200.0f;            //        length of the longest waves
const float R2_IRREGULARITY       = 0.30f;
const float R2_SHALLOW_CM         = 375.0f;            //        a patch of green shallows, centred here
const float R2_SHALLOW_WIDTH_CM   = 80.0f;
const float R2_SHALLOW_AMOUNT     = 0.60f;
const CRGB  R2_SHALLOW_COLOUR     = CRGB(69, 165, 82);
const float R2_SPARKLES           = 0.05f;
const float R2_SPARKLE_SPEED      = 1.00f;
const CRGB  R2_SPARKLE_COLOUR     = CRGB(223, 242, 238);

// ---------------------------------------------------------------------
//  3. FLOODING & SEWAGE (90 s: floods first, then sewage)
// ---------------------------------------------------------------------
// Flood events touching the floorboards per month, January to December 2025.
// Each time round the loop, the next month plays.
const uint8_t FLOOD_EVENTS[12] = { 5, 5, 11, 11, 6, 3, 1, 3, 9, 12, 9, 5 };
const uint8_t START_MONTH      = 0;          // 0 = January 2025
const float FLOOD_START          = 0.0f;     // seconds into this animation
const float FLOOD_END            = 50.0f;
const float SEWAGE_START         = 40.0f;    // overlaps the floods for 10 s
const float SEWAGE_END           = 90.0f;
const float FLOOD_REACH_CM       = 50.0f;    // how far each flood spreads from House Mill
const float FLOOD_SPREAD_CM      = 40.0f;    // how fast it spreads (cm per second)
const float FLOOD_DRAIN_SECONDS  = 2.5f;     // how long it takes to drain away
const CRGB  FLOOD_COLOUR         = CRGB(255, 26, 10);   // TWEAK
const CRGB  SEWAGE_INK_COLOUR    = CRGB(230, 99, 0);    // TWEAK: fresh sewage: orange-yellow where it comes in
const CRGB  SEWAGE_MUCK_COLOUR   = CRGB(95, 55, 20);    // TWEAK: the brown it fades to down the river. Was (138, 55, 0)
const CRGB  SEWAGE_WAVE_COLOUR   = CRGB(210, 160, 30);  // TWEAK: the yellow in the waves spreading from the sewage points
const float SEWAGE_WAVE_CM       = 40.0f;               // TWEAK: distance between those brown/yellow waves (cm)
const float SEWAGE_WAVE_SPEED_CM = 15.0f;               // TWEAK: how fast they spread out from the points (cm per second)
const float SEWAGE_YELLOW_REACH_CM = 120.0f;            // TWEAK: how far from a point the waves are still yellow (browner beyond)
const float SEWAGE_MUCKINESS     = 0.60f;
#define SEWAGE_POINTS 7
const float SEWAGE_POS_CM[SEWAGE_POINTS] = { 105, 160, 275, 300, 325, 375, 460 };   // Emma's sewage inputs
const float SEWAGE_EVERY[SEWAGE_POINTS]  = { 6, 12, 0, 18, 9, 0, 25 };             // seconds between discharges (0 = once)

// ---------------------------------------------------------------------
//  4. REWILDING
// ---------------------------------------------------------------------
const float REWILD_START_POLLUTION = 1.0f;      // TWEAK: how much of the sewage is still there when rewilding starts (1 = all of it)
// small white waves appear from several points, then turn blue, before the river is clean
const int   REWILD_WAVE_POINTS     = 5;         // TWEAK: how many points the small white waves come from (spread evenly)
const float REWILD_WHITE_AT        = 4.0f;      // TWEAK: seconds into REWILDING when the white waves appear
const float REWILD_BLUE_AT         = 18.0f;     // TWEAK: seconds into REWILDING when they start turning blue
const float REWILD_RIPPLE_CM       = 12.0f;     // TWEAK: distance between the small waves (cm)
const float REWILD_RIPPLE_SPREAD   = 15.0f;     // TWEAK: how fast they spread out from their points (cm per second)
const CRGB  REWILD_WHITE_COLOUR    = CRGB(255, 255, 255);  // TWEAK: the small waves' colour at first
// the rewilding area: random LEDs in it turn dark green
const float REWILD_FROM_CM         = 195.0f;    // TWEAK: rewilding area starts (cm from the Thames end)
const float REWILD_TO_CM           = 240.0f;    // TWEAK: rewilding area ends
const int   REWILD_PLANTS          = 6;         // TWEAK: how many LEDs in the area turn green
const CRGB  REWILD_COLOUR          = CRGB(0, 70, 15);      // TWEAK: the dark green

// ---------------------------------------------------------------------
//  SENSORS - when each one counts as covered (the same as the red pulse test)
//  Each sensor remembers its brightest reading (its max).
// ---------------------------------------------------------------------
const float COVERED_BELOW   = 0.50f;  // TWEAK: covered when the light drops below 50% of its max
const float UNCOVERED_ABOVE = 0.60f;  // TWEAK: uncovered again above 60% of its max (stops flickering)

// ---------------------------------------------------------------------
//  CARE INPUT - what covering a sensor does
//  Covered: the colour fades in at the sensor and is released continuously,
//  flowing down the river. Uncovered: the input fades out; what was already
//  released flows on down the river and fades away. It cleans as it goes.
// ---------------------------------------------------------------------
// --- colours ---
const CRGB  PULSE_COLOUR         = CRGB(0, 255, 30);  // TWEAK: input colour (where the colour comes in, at the sensor)
const CRGB  PULSE_TRAIL_COLOUR   = CRGB(0, 120, 90);  // TWEAK: trail / fade colour (what it turns into as it flows away)
const float PULSE_INTENSITY      = 1.2f;              // TWEAK: how strong the colour is (0.5 = soft, 1 = full, 1.5 = very solid)
// --- movement ---
const float PULSE_SPEED          = 6.0f;              // TWEAK: speed it flows down the strip (LEDs per second)
const int   PULSE_DIRECTION      = 1;                 // TWEAK: 1 = towards the last LED, -1 = towards LED 0
const float PULSE_WAVE_FOLLOW    = 0.5f;              // TWEAK: how much the river's waves move it (0 = not at all, 1 = a lot)
// --- shape and fading ---
const float PULSE_WIDTH          = 6.0f;              // TWEAK: how long each pulse is (LEDs)
const float PULSE_SPREAD         = 15.0f;              // TWEAK: how much each pulse spreads out as it flows (LEDs per second; low = crisp pulses)
const float PULSE_FADE           = 15.0f;             // TWEAK: fade length: how far the pulses go before they have faded away (LEDs)
const float PULSE_EVERY          = 0.5f;              // TWEAK: while covered, a new pulse leaves every 0.5 s
const float PULSE_FADE_IN        = 0.0f;              // TWEAK: seconds for the solid section to fade in when covered
const float PULSE_FADE_OUT       = 0.1f;              // TWEAK: seconds for the solid section to fade out when uncovered
const float CLEAN_FULL_SECONDS   = 5.0f;              // TWEAK: hold this long to clean the pollution completely
const float CLEAN_STAYS_SECONDS  = 4.0f;              // TWEAK: how long cleaned water stays clean afterwards

// =====================================================================
//  THE STRIP
// =====================================================================
Adafruit_NeoPixel strip(NUM_LEDS, DATA_PIN, NEO_GRB + NEO_KHZ800);
CRGB leds[NUM_LEDS];       // the final picture sent to the strip
CRGB bufA[NUM_LEDS];       // the animation playing now
CRGB bufB[NUM_LEDS];       // the next animation, during a transition

// =====================================================================
//  HELPERS (small maths tools used everywhere)
// =====================================================================
float rand01() { return random(10000) / 10000.0f; }                       // random 0-1
float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }            // keep between 0 and 1
float smooth01(float x) { x = clamp01(x); return x * x * (3.0f - 2.0f * x); }   // soft 0-1 curve
float wrapTo(float x, float p) { return x - p * floorf(x / p); }           // wrap round, like a clock
float mixf(float a, float b, float k) { return a + (b - a) * k; }          // k = 0 gives a, 1 gives b
uint8_t cap255(float v) { return v <= 0 ? 0 : (v >= 255.0f ? 255 : (uint8_t)v); }
CRGB mixColour(CRGB a, CRGB b, float k) {                                   // mix two colours
  return CRGB(cap255(mixf(a.r, b.r, k)), cap255(mixf(a.g, b.g, k)), cap255(mixf(a.b, b.b, k)));
}
CRGB dimColour(CRGB c, float k) { return CRGB(cap255(c.r * k), cap255(c.g * k), cap255(c.b * k)); }

// The same "random" number (0-1) every time for a given whole number
float hash01(uint32_t k) {
  uint32_t h = k * 2654435761u;
  h ^= h >> 16; h *= 2246822519u;
  h ^= h >> 13; h *= 3266489917u;
  h ^= h >> 16;
  return (h & 0xFFFFFF) / 16777216.0f;
}

// Sparkles: each LED can flash (rising quickly) and then fade.
// Each set of sparkles keeps its own memory, so two animations don't disturb each other.
// (the Sparkles type is defined near the top, after CRGB)
float sparkleStep(float &g, float &tg, float crest, float density, float rate,
                  float frameScale, float fade, float lively) {
  if (tg > 0) {                                        // rising
    g += (tg - g) * fminf(1.0f, 0.6f * frameScale * rate);
    if (g >= tg * 0.9f) tg = 0;
  } else {                                             // fading
    g *= fade;
  }
  // start a new flash now and then, mostly on wave crests
  if (tg == 0 && g < 0.2f && rand01() < density * 0.3f * crest * lively * frameScale * (300.0f / NUM_LEDS)) {
    tg = fminf(1.0f, 0.35f + rand01() * 0.6f * crest + (rand01() < 0.08f ? 0.4f : 0.0f));
  }
  return g;
}

// =====================================================================
//  THE RIVER ITSELF (the water under every animation)
//  Right of House Mill the water always flows towards the mill.
//  Left of it (the tidal stretch) it flows towards the mill, then away.
//  u = position along the strip from 0 (LED 0) to 1 (the last LED).
// =====================================================================
#define FLOW        0.027f                 // river flow (fraction of the strip per second)
#define WAVE1       0.15f                  // RIVER wave lengths (fraction of the strip)
#define WAVE2       (0.15f * 0.37f)
#define MILL_BLEND  0.03f                  // how softly the two sides of the mill meet
float tidePhase = PI * 0.1f, riverClock = 0;
float offL1 = 0, offL2 = 0, offR1 = 0, offR2 = 0;   // where the waves are, left and right of the mill

float millU() { return clamp01(HOUSE_MILL_CM / 100.0f / STRIP_LENGTH_M); }
float tideDir() { return tanhf(6.0f * sinf(tidePhase)); }               // +1 flowing right, -1 flowing left
float flowDirAt(float u) { return u < millU() ? tideDir() : -1.0f; }    // right of the mill: always towards it

// Move the waves on by dt seconds (called once per frame)
void advanceRiver(float dt) {
  tidePhase = wrapTo(tidePhase + PI * TIDE_CHANGES_PER_MIN / 60.0f * dt, TWO_PI);
  float v = FLOW * RIVER_WAVE_SPEED * dt, d = tideDir();                // RIVER_WAVE_SPEED makes the first waves faster
  riverClock = wrapTo(riverClock + dt, 600.0f);
  offL1 = wrapTo(offL1 + v * d, WAVE1);  offL2 = wrapTo(offL2 + v * d * 1.3f, WAVE2);
  offR1 = wrapTo(offR1 - v, WAVE1);      offR2 = wrapTo(offR2 - v * 1.3f, WAVE2);
}

float warpU(float u, float irr) {
  float T = riverClock;
  return u + irr * (0.04f * sinf(TWO_PI * (u * 1.7f + T * 19.0f / 600.0f)) + 0.025f * sinf(TWO_PI * (u * 3.9f - T * 28.0f / 600.0f) + 1.3f));
}
float swellAt(float u, float irr) { return 1.0f - irr * 0.45f * (0.5f + 0.5f * sinf(TWO_PI * (u * 2.3f + riverClock * 11.0f / 600.0f) + 0.7f)); }
float riverShape(float uw, float o1, float o2) {    // two wave sizes added together
  return 0.7f * (sinf(TWO_PI * (uw - o1) / WAVE1) + 1.0f) * 0.5f + 0.3f * (sinf(TWO_PI * (uw - o2) / WAVE2 + PI) + 1.0f) * 0.5f;
}
// How high the ideal river is at u: 0 = trough (between waves), 1 = crest (top of a wave)
float riverWaveAt(float u) {
  float irr = RIVER_IRREGULARITY, uw = warpU(u, irr);
  float k = smooth01((u - millU()) / MILL_BLEND + 0.5f), s = 0;   // 0 = tidal side, 1 = the other side
  if (k < 1) s += (1.0f - k) * riverShape(uw, offL1, offL2);
  if (k > 0) s += k * riverShape(uw, offR1, offR2);
  return powf(s * swellAt(u, irr), 2.2f);
}
// Colour of the clean river (no sparkles) at u
CRGB idealWater(float u, float* waveOut) {   // waveOut: where to put the wave height (or nullptr)
  float w = riverWaveAt(u);
  if (waveOut) *waveOut = w;
  return mixColour(RIVER_TROUGH_COLOUR, RIVER_CREST_COLOUR, w);
}

// The ideal river WITH sparkles (used by RIVER, and by REWILDING as it turns back into RIVER)
void drawRiver(float t, float dt, CRGB* out, Sparkles& sp) {
  if (sp.lastT < 0 || t < sp.lastT) { for (int i = 0; i < NUM_LEDS; i++) { sp.glint[i] = 0; sp.target[i] = 0; } }
  sp.lastT = t;
  float frameScale = dt / 0.035f, fade = powf(0.78f, frameScale * RIVER_SPARKLE_SPEED);
  float t2 = 0.68f * 0.68f, mill = millU(), calm = fabsf(tideDir());
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, wave;
    CRGB c = idealWater(u, &wave);
    float crest = smooth01((wave - t2) / (1.0f - t2));            // 1 near the top of a wave
    float lively = u < mill ? 0.5f + 0.5f * calm : 1.0f;           // the tidal side is calmer while it turns
    float gl = sparkleStep(sp.glint[i], sp.target[i], crest, RIVER_SPARKLES, RIVER_SPARKLE_SPEED, frameScale, fade, lively);
    gl = fminf(1.0f, gl + crest * 0.1f * lively);                  // a soft shine on every crest
    out[i] = CRGB(cap255(c.r + RIVER_SPARKLE_COLOUR.r * gl), cap255(c.g + RIVER_SPARKLE_COLOUR.g * gl), cap255(c.b + RIVER_SPARKLE_COLOUR.b * gl));
  }
}

// =====================================================================
//  THE FOUR ANIMATIONS
//  Each one fills out[] with colours for time t (seconds since it started).
// =====================================================================

// ----- 1. RIVER -----
Sparkles riverSparkles;
void fxRiver(float t, float dt, CRGB* out) {
  drawRiver(t, dt, out, riverSparkles);
}

// ----- 2. RIVER 2: layered "real" waves (longer waves travel faster) -----
void fxRiver2(float t, float dt, CRGB* out) {
  static Sparkles sp;
  static float oL[4], oR[4];                                   // where each layer of waves is
  const float SIZES[4]  = { 1.0f, 0.57f, 0.35f, 0.22f };       // 4 layers of waves, longest first
  const float AMPS[4]   = { 1.0f, 0.55f, 0.3f, 0.18f };        // how tall each layer is
  const float PHASES[4] = { 0.0f, 1.7f, 4.1f, 2.6f };
  if (sp.lastT < 0 || t < sp.lastT) {                          // starting again: reset
    for (int i = 0; i < NUM_LEDS; i++) { sp.glint[i] = 0; sp.target[i] = 0; }
    for (int k = 0; k < 4; k++) { oL[k] = 0; oR[k] = 0; }
  }
  sp.lastT = t;
  const float L = STRIP_LENGTH_M, irr = R2_IRREGULARITY;
  float base = fmaxf(0.01f, R2_SPACING_CM / 100.0f / L);
  float d = tideDir(), lens[4], sumA = 0;
  for (int k = 0; k < 4; k++) {                                // move each layer on
    lens[k] = base * SIZES[k];
    float c = 0.03f * sqrtf(SIZES[k]) * R2_WAVE_SPEED;         // R2_WAVE_SPEED makes these waves slower
    oL[k] = wrapTo(oL[k] + c * d * dt, lens[k]);
    oR[k] = wrapTo(oR[k] - c * dt, lens[k]);
    sumA += AMPS[k];
  }
  float sx = R2_SHALLOW_CM / 100.0f, sw = fmaxf(0.05f, R2_SHALLOW_WIDTH_CM / 100.0f);
  float frameScale = dt / 0.035f, fade = powf(0.78f, frameScale * R2_SPARKLE_SPEED);
  float mill = millU(), calm = fabsf(d);
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, x = (i + 0.5f) / NUM_LEDS * L;
    float uw = warpU(u, irr);
    float kk = smooth01((u - mill) / MILL_BLEND + 0.5f);
    float hL = 0, hR = 0;
    for (int k = 0; k < 4; k++) {                              // add up the layers
      if (kk < 1) hL += AMPS[k] * sinf(TWO_PI * (uw - oL[k]) / lens[k] + PHASES[k]);
      if (kk > 0) hR += AMPS[k] * sinf(TWO_PI * (uw - oR[k]) / lens[k] + PHASES[k]);
    }
    float h = (hL * (1.0f - kk) + hR * kk) / sumA;
    float env = 0.7f + 0.3f * sinf(TWO_PI * (u * 0.9f - riverClock * 7.0f / 600.0f) + 0.4f);   // slow sets of bigger waves
    float wave = powf(clamp01((h * env + 1.0f) / 2.0f * swellAt(u, irr)), 1.8f);                 // 0 = deepest, 1 = top
    float crest = smooth01((wave - 0.55f) / 0.45f);
    float lively = u < mill ? 0.5f + 0.5f * calm : 1.0f;
    float gl = sparkleStep(sp.glint[i], sp.target[i], crest, R2_SPARKLES, R2_SPARKLE_SPEED, frameScale, fade, lively);

    CRGB c = mixColour(R2_TROUGH_COLOUR, R2_CREST_COLOUR, wave);            // trough to crest
    float deep = R2_DEEP_AMOUNT * smooth01((R2_DEEP_LEVEL - wave) / R2_DEEP_LEVEL);
    c = mixColour(c, R2_DEEP_COLOUR, deep);                                  // green in the deepest parts
    float q = (x - sx) / sw, shallow = R2_SHALLOW_AMOUNT * expf(-q * q);
    c = mixColour(c, dimColour(R2_SHALLOW_COLOUR, 0.55f + 0.45f * wave), shallow);   // the green shallows
    out[i] = CRGB(cap255(c.r + R2_SPARKLE_COLOUR.r * gl), cap255(c.g + R2_SPARKLE_COLOUR.g * gl), cap255(c.b + R2_SPARKLE_COLOUR.b * gl));
  }
}

// ----- 3a. Floods: one red surge from House Mill per flood event this month -----
int floodMonth = START_MONTH;
void fxFlooding(float t, float dt, CRGB* out, float D) {
  const float L = STRIP_LENGTH_M, x0 = HOUSE_MILL_CM / 100.0f;
  const int n = FLOOD_EVENTS[floodMonth];
  const float reach = fmaxf(0.05f, FLOOD_REACH_CM / 100.0f), v = FLOOD_SPREAD_CM / 100.0f;
  const float every = D / (n > 0 ? n : 1);                     // the floods are spread evenly over the flood time
  const float rise = reach / v, drain = fmaxf(0.2f, FLOOD_DRAIN_SECONDS), life = rise + drain;
  int kMax = (int)floorf(t / every - 0.5f);
  if (kMax > n - 1) kMax = n - 1;
  int kMin = kMax - (int)ceilf(life / every) - 1;
  if (kMin < 0) kMin = 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, x = (i + 0.5f) / NUM_LEDS * L, d = fabsf(x - x0);
    CRGB c = idealWater(u, nullptr);
    float m = n > 0 ? 0.1f : 0.0f;                             // a faint red tint at the mill in a flooding month
    for (int k = kMin; k <= kMax; k++) {
      float age = t - (k + 0.5f) * every;
      if (age < 0 || age > life) continue;
      float rr = fminf(reach, age * v);                        // how far this surge has spread
      float q = (d - rr) / 0.08f;
      float inside = d <= rr ? 1.0f : expf(-q * q);
      float front = expf(-q * q) * (1.0f - smooth01(age / rise)) * 0.5f;
      float fade = age < rise ? 1.0f : 1.0f - smooth01((age - rise) / drain);
      m = fmaxf(m, fminf(1.0f, 0.8f * inside + front) * fade);
    }
    m *= smooth01((reach + 0.075f - d) / 0.15f);               // nothing beyond the reach
    out[i] = mixColour(c, FLOOD_COLOUR, m);
  }
}

// ----- 3b. Sewage: the inputs discharge like ink poured into water -----
#define MAX_PLUMES 48
struct Plume { float x, age; bool on; };
bool  sewageTapsOn = true;        // false during REWILDING: no new sewage, what is there drifts on
float sewageLastT = 0;            // where the sewage has got to (seconds)
bool  sewageDoneThisFrame = false; // so the sewage is only moved on once per frame
int dischargeCount(float T, float first, float every) {
  if (T < first) return 0;
  return every > 0 ? (int)floorf((T - first) / every) + 1 : 1;
}
void fxSewage(float t, float dt, CRGB* out) {
  static Plume plumes[MAX_PLUMES];
  static float lastT = -1, muck = 0;
  const float L = STRIP_LENGTH_M;
  float prevT = lastT;
  if (lastT < 0 || t < lastT) {                                // starting again: clear
    for (int p = 0; p < MAX_PLUMES; p++) plumes[p].on = false;
    muck = 0; prevT = t;
  }
  lastT = t;
  sewageLastT = t;
  sewageDoneThisFrame = true;
  // each input discharges on its own rhythm (not during REWILDING)
  for (int k = 0; k < SEWAGE_POINTS && sewageTapsOn; k++) {
    float first = 2.0f + hash01(k + 11) * 8.0f;
    int n = dischargeCount(t, first, SEWAGE_EVERY[k]) - dischargeCount(prevT, first, SEWAGE_EVERY[k]);
    if (n > 2) n = 2;
    for (int j = 0; j < n; j++) {
      for (int p = 0; p < MAX_PLUMES; p++) if (!plumes[p].on) {
        plumes[p].x = SEWAGE_POS_CM[k] / 100.0f; plumes[p].age = 0; plumes[p].on = true; break;
      }
      muck = fminf(0.75f, muck + 0.07f * SEWAGE_MUCKINESS);   // the whole river gets murkier
    }
  }
  // the sewage drifts with the river, spreads and fades; the muck slowly settles
  muck *= expf(-dt / 90.0f);
  for (int p = 0; p < MAX_PLUMES; p++) {
    if (!plumes[p].on) continue;
    plumes[p].age += dt;
    plumes[p].x += FLOW * L * flowDirAt(plumes[p].x / L) * dt;
    if (plumes[p].age >= 40.0f || plumes[p].x <= -1.0f || plumes[p].x >= L + 1.0f) plumes[p].on = false;
  }
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, x = (i + 0.5f) / NUM_LEDS * L, wave;
    CRGB c = idealWater(u, &wave);
    // waves of brown and yellow spreading out from the nearest sewage point
    float nearest = 1e9f;
    for (int k = 0; k < SEWAGE_POINTS; k++) nearest = fminf(nearest, fabsf(x * 100.0f - SEWAGE_POS_CM[k]));   // cm
    float ring = 0.5f + 0.5f * sinf(TWO_PI * (nearest - t * SEWAGE_WAVE_SPEED_CM) / SEWAGE_WAVE_CM);
    float yellow = ring * (1.0f - smooth01(nearest / SEWAGE_YELLOW_REACH_CM));          // yellow near the points, brown further away
    c = mixColour(c, mixColour(SEWAGE_MUCK_COLOUR, SEWAGE_WAVE_COLOUR, yellow), muck);
    float conc = 0, cr = 0, cg = 0, cb = 0;
    for (int p = 0; p < MAX_PLUMES; p++) {
      if (!plumes[p].on) continue;
      float w = 0.12f + 0.03f * plumes[p].age, q = (x - plumes[p].x) / w;
      if (q < -3 || q > 3) continue;
      float a = 1.8f * powf(0.12f / w, 0.7f) * expf(-plumes[p].age / 25.0f) * expf(-q * q) * (0.75f + 0.25f * wave);
      float k = smooth01(plumes[p].age / 12.0f);               // fresh sewage turns murky as it spreads
      conc += a;
      cr += a * mixf(SEWAGE_INK_COLOUR.r, SEWAGE_MUCK_COLOUR.r, k);
      cg += a * mixf(SEWAGE_INK_COLOUR.g, SEWAGE_MUCK_COLOUR.g, k);
      cb += a * mixf(SEWAGE_INK_COLOUR.b, SEWAGE_MUCK_COLOUR.b, k);
    }
    if (conc > 0) c = mixColour(c, CRGB(cap255(cr / conc), cap255(cg / conc), cap255(cb / conc)), fminf(1.0f, conc));
    out[i] = c;
  }
}

// ----- 3. FLOODING & SEWAGE together: floods, then sewage, overlapping a little -----
float partWindow(float t, float a, float b, float fadeIn, float fadeOut) {   // how much of a part shows at time t
  if (t < a || t > b) return 0;
  fadeIn = fminf(fadeIn, (b - a) * 0.5f); fadeOut = fminf(fadeOut, (b - a) * 0.5f);
  return (fadeIn > 0 ? smooth01((t - a) / fadeIn) : 1.0f) * (fadeOut > 0 ? 1.0f - smooth01((t - (b - fadeOut)) / fadeOut) : 1.0f);
}
CRGB fsFlood[NUM_LEDS], fsSewage[NUM_LEDS];
void fxFloodSewage(float t, float dt, CRGB* out) {
  static float lastT = -1;
  if (lastT >= 0 && t < lastT - 0.5f) floodMonth = (floodMonth + 1) % 12;   // each time round: the next month
  lastT = t;
  float len = ANIM_SECONDS[2];
  // A part that lasts to the end keeps going through the transition into REWILDING
  // (so the sewage doesn't vanish suddenly just before the end)
  float fEnd = FLOOD_END >= len ? 1e6f : FLOOD_END, sEnd = SEWAGE_END >= len ? 1e6f : SEWAGE_END;
  float ov = fmaxf(0.0f, fminf(FLOOD_END, SEWAGE_END) - fmaxf(FLOOD_START, SEWAGE_START));
  float kf = partWindow(t, FLOOD_START, fEnd, FLOOD_START > 0 ? 3.0f : 0.0f, FLOOD_END >= len ? 0.0f : 3.0f);
  float ks = partWindow(t, SEWAGE_START, sEnd, SEWAGE_START > 0 ? fmaxf(3.0f, ov * 0.6f) : 0.0f, SEWAGE_END >= len ? 0.0f : 3.0f);
  if (kf > 0) fxFlooding(t - FLOOD_START, dt, fsFlood, FLOOD_END - FLOOD_START);
  sewageTapsOn = true;
  if (ks > 0) fxSewage(t - SEWAGE_START, dt, fsSewage);
  for (int i = 0; i < NUM_LEDS; i++) {
    CRGB w = idealWater((float)i / NUM_LEDS, nullptr);
    float R = w.r, G = w.g, B = w.b;
    if (kf > 0) { R += (fsFlood[i].r - w.r) * kf;  G += (fsFlood[i].g - w.g) * kf;  B += (fsFlood[i].b - w.b) * kf; }
    if (ks > 0) { R += (fsSewage[i].r - w.r) * ks; G += (fsSewage[i].g - w.g) * ks; B += (fsSewage[i].b - w.b) * ks; }
    out[i] = CRGB(cap255(R), cap255(G), cap255(B));
  }
}

// ----- 4. REWILDING: plants grow in the rewilding area, clean waves come in from
//          both ends, and the river becomes exactly the ideal RIVER (with sparkles)
//          again, so the loop flows straight back into RIVER -----
Sparkles rewildSparkles;
CRGB cleanRiver[NUM_LEDS];
void fxRewilding(float t, float dt, CRGB* out) {
  const float L = STRIP_LENGTH_M, mill = millU() * L;
  float prog = smooth01((t - 3.0f) / 42.0f);               // the clean waves take about 42 s to reach the mill
  float fl = mill * prog, fr = L - (L - mill) * prog;       // where the two clean fronts are (metres)
  float allClean = smooth01((t - 45.0f) / 8.0f);            // after 45 s: everything clean
  float glowOn = prog > 0 ? 1.0f - smooth01((t - 45.0f) / 5.0f) : 0.0f;
  drawRiver(t, dt, cleanRiver, rewildSparkles);            // the clean water = the RIVER animation itself
  // the dirty water = the sewage from FLOODING & SEWAGE, still drifting (but no new sewage)
  if (!sewageDoneThisFrame) {
    sewageTapsOn = false;
    fxSewage(sewageLastT + dt, dt, fsSewage);
  }

  // the plants: random single LEDs in the rewilding area, slowly turning dark green
  static float plant[NUM_LEDS];
  for (int i = 0; i < NUM_LEDS; i++) plant[i] = 0;
  int first = (int)(REWILD_FROM_CM / 100.0f / L * NUM_LEDS), last = (int)(REWILD_TO_CM / 100.0f / L * NUM_LEDS);
  if (last < first) { int tmp = first; first = last; last = tmp; }
  float back = 1.0f - smooth01((t - 40.0f) / 15.0f);       // they fade as the river returns to normal
  for (int k = 0; k < REWILD_PLANTS; k++) {
    float age = t - (4.0f + hash01(k + 61) * 20.0f);        // each appears at its own moment, between 4 s and 24 s
    if (age < 0) continue;
    int led = first + (int)(hash01(k + 81) * (last - first + 1));
    if (led >= 0 && led < NUM_LEDS) plant[led] = fmaxf(plant[led], smooth01(age / 6.0f) * back);
  }
  // the small white waves: from several points, spreading out, then turning blue
  float rippleOn = smooth01((t - REWILD_WHITE_AT) / 6.0f) * (1.0f - allClean);
  float blue = smooth01((t - REWILD_BLUE_AT) / 10.0f);
  CRGB rippleColour = mixColour(REWILD_WHITE_COLOUR, RIVER_CREST_COLOUR, blue);
  float spread = fmaxf(0.0f, t - REWILD_WHITE_AT) * REWILD_RIPPLE_SPREAD;      // how far they have spread (cm)
  int np = REWILD_WAVE_POINTS < 1 ? 1 : REWILD_WAVE_POINTS;

  for (int i = 0; i < NUM_LEDS; i++) {
    float x = (i + 0.5f) / NUM_LEDS * L;
    CRGB c = cleanRiver[i];
    float clean = x < mill ? smooth01((fl - x) / 0.6f + 0.5f) : smooth01((x - fr) / 0.6f + 0.5f);
    clean = fmaxf(clean, allClean);
    c = mixColour(c, fsSewage[i], REWILD_START_POLLUTION * (1.0f - clean));   // still sewage where the clean waves haven't reached
    // small waves from the nearest point
    float w = 0;
    for (int j = 0; j < np; j++) {
      float d = fabsf(x * 100.0f - (j + 0.5f) / np * L * 100.0f);           // cm from this point
      float reach = smooth01((spread - d) / 20.0f);                         // only as far as they have spread
      float crest = powf(0.5f + 0.5f * cosf(TWO_PI * (d - t * REWILD_RIPPLE_SPREAD) / REWILD_RIPPLE_CM), 6.0f);   // thin crests
      w = fmaxf(w, reach * crest);
    }
    c = mixColour(c, cleanRiver[i], 0.4f * blue * rippleOn * (1.0f - clean));   // the blue waves start to clear the water
    c = mixColour(c, rippleColour, 0.7f * rippleOn * w * (1.0f - clean));
    float qf = x < mill ? (x - fl) / 0.3f : (x - fr) / 0.3f;
    c = mixColour(c, RIVER_CREST_COLOUR, 0.5f * glowOn * expf(-qf * qf));   // the arriving clean waves glow
    out[i] = mixColour(c, REWILD_COLOUR, plant[i]);                         // the plants
  }
}

typedef void (*EffectFn)(float t, float dt, CRGB* out);
EffectFn EFFECTS[NUM_ANIMS] = { fxRiver, fxRiver2, fxFloodSewage, fxRewilding };
const char* NAMES[NUM_ANIMS] = { "RIVER", "RIVER 2", "FLOODING & SEWAGE", "REWILDING" };

// =====================================================================
//  TRANSITIONS between animations
// =====================================================================
CRGB transitionPixel(uint8_t type, float p, CRGB a, CRGB b) {
  if (type == 3) {                                  // overlay: new one on top, old one fades underneath
    CRGB A = dimColour(a, 1.0f - smooth01((p - 0.4f) / 0.6f));
    CRGB B = dimColour(b, smooth01(p / 0.6f));
    return CRGB(A.r > B.r ? A.r : B.r, A.g > B.g ? A.g : B.g, A.b > B.b ? A.b : B.b);
  }
  return mixColour(a, b, smooth01(p));              // fade
}

// Run one animation and apply its LIGHT setting
void renderAnim(int id, float localT, float dt, CRGB* out) {
  EFFECTS[id](localT, dt, out);
  for (int i = 0; i < NUM_LEDS; i++) out[i] = dimColour(out[i], LIGHT[id]);
}

// =====================================================================
//  SENSORS
// =====================================================================
int   ldrReading[NUM_LDRS];   // the reading now (0 = dark, 4095 = bright)
int   ldrMax[NUM_LDRS];       // the brightest reading so far
bool  covered[NUM_LDRS];      // covered right now?
float heldFor[NUM_LDRS];      // seconds it has been touched for

int readLdr(int k) {
  analogRead(LDR_PINS[k]);                 // throw-away reading: clears the last pin's value
  delayMicroseconds(200);
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(LDR_PINS[k]);   // average 8 readings
  return sum / 8;
}

void readSensors(float dt) {
  for (int k = 0; k < NUM_LDRS; k++) {
    ldrReading[k] = readLdr(k);
    if (ldrReading[k] > ldrMax[k]) ldrMax[k] = ldrReading[k];             // brighter than ever: new max
    if (!covered[k] && ldrReading[k] < ldrMax[k] * COVERED_BELOW)  covered[k] = true;
    if (covered[k]  && ldrReading[k] > ldrMax[k] * UNCOVERED_ABOVE) covered[k] = false;
    if (covered[k]) heldFor[k] += dt;                                     // covered: count up
    else heldFor[k] = 0;                                                 // let go: start counting again next time
  }
}

// =====================================================================
//  CARE INPUT: while a sensor is covered its section is solid colour and
//  pulses leave it every PULSE_EVERY seconds, travelling down the river.
//   - covered:   the section fades in (PULSE_FADE_IN) to solid colour
//   - uncovered: the section fades out (PULSE_FADE_OUT); the pulses already
//                sent keep travelling down the river and fade away
//  The released colour turns from PULSE_COLOUR to PULSE_TRAIL_COLOUR as it
//  flows, spreads out, moves with the waves, and cleans the pollution where
//  it goes (held longer = cleaner).
// =====================================================================
#define MAX_DROPS        300
struct InkDrop { float pos, origin, age, strength, cleanPower; bool on; };
InkDrop drops[MAX_DROPS];
float nextPulse[NUM_LDRS];     // counts towards each sensor's next drop
float inputLevel[NUM_LDRS];    // 0 = not releasing, 1 = fully releasing (fades in and out)
float cleanMem[NUM_LEDS];      // how clean each LED has been made (0-1), fades over CLEAN_STAYS_SECONDS
float dirtiness = 0;           // 0 while the water is clean (RIVER, RIVER 2), 1 while dirty
float inkAmount[NUM_LEDS];     // how much colour is on each LED this frame
float inkR[NUM_LEDS], inkG[NUM_LEDS], inkB[NUM_LEDS];   // its colour (added up from the drops)

// Send a pulse from section n, for sensor k (0 = sensor 1)
void addDrop(int n, int k) {
  float fromM = fminf(SECTIONS[n].fromM, SECTIONS[n].toM), toM = fmaxf(SECTIONS[n].fromM, SECTIONS[n].toM);
  for (int d = 0; d < MAX_DROPS; d++) {
    if (drops[d].on) continue;
    // pulses leave from the end of the section that faces down the river
    drops[d].origin = (PULSE_DIRECTION > 0 ? toM : fromM) / STRIP_LENGTH_M * NUM_LEDS;
    drops[d].pos = drops[d].origin - PULSE_DIRECTION * PULSE_WIDTH * 0.5f;   // starts just inside the section
    drops[d].age = 0;
    drops[d].strength = 1.0f;
    drops[d].cleanPower = clamp01(heldFor[k] / CLEAN_FULL_SECONDS);   // held longer = cleans more
    drops[d].on = true;
    return;
  }
}

void updatePulses(float dt, int currentAnim) {
  float life = PULSE_FADE / fmaxf(0.1f, PULSE_SPEED);              // seconds a drop lasts (fade length / speed)
  // 1. each sensor's input fades in while covered and out when uncovered, releasing drops
  for (int k = 0; k < NUM_LDRS; k++) {
    if (covered[k]) inputLevel[k] = PULSE_FADE_IN  > 0 ? fminf(1.0f, inputLevel[k] + dt / PULSE_FADE_IN)  : 1.0f;
    else            inputLevel[k] = PULSE_FADE_OUT > 0 ? fmaxf(0.0f, inputLevel[k] - dt / PULSE_FADE_OUT) : 0.0f;
    if (!covered[k]) { nextPulse[k] = 0; continue; }               // pulses only while covered; ready to send one straight away
    nextPulse[k] -= dt;
    if (nextPulse[k] <= 0) {
      for (int n = 0; n < NUM_SECTIONS; n++) if (SECTIONS[n].sensor == k + 1) addDrop(n, k);   // a pulse from each of its sections
      nextPulse[k] = fmaxf(0.05f, PULSE_EVERY);
    }
  }
  // 2. every drop flows down the river, moved by the waves, and gets older
  for (int d = 0; d < MAX_DROPS; d++) {
    if (!drops[d].on) continue;
    InkDrop& p = drops[d];
    p.age += dt;
    float u = clamp01(p.pos / NUM_LEDS);
    float push = FLOW * RIVER_WAVE_SPEED * NUM_LEDS * flowDirAt(u);   // the river's flow (against it on the tidal stretch)
    float bob = 2.0f * sinf(riverClock * 1.7f + d * 0.37f);           // bobbing with the waves
    p.pos += (PULSE_DIRECTION * PULSE_SPEED + PULSE_WAVE_FOLLOW * (push + bob)) * dt;
    if (p.age > life || p.pos < -30 || p.pos > NUM_LEDS + 30) p.on = false;
  }
  // 3. add up how much colour is on each LED, and clean the water there
  for (int i = 0; i < NUM_LEDS; i++) { inkAmount[i] = 0; inkR[i] = 0; inkG[i] = 0; inkB[i] = 0; }
  float wantDirty = (currentAnim == 2 || currentAnim == 3) ? 1.0f : 0.0f;
  dirtiness += (wantDirty - dirtiness) * fminf(1.0f, dt / 2.0f);         // changes smoothly over about 2 s
  float keep = expf(-dt / CLEAN_STAYS_SECONDS);
  for (int i = 0; i < NUM_LEDS; i++) cleanMem[i] *= keep;                 // cleaned water slowly gets dirty again
  for (int d = 0; d < MAX_DROPS; d++) {
    if (!drops[d].on) continue;
    InkDrop& p = drops[d];
    float along = clamp01(p.age / life);                                  // 0 = just released, 1 = gone
    float w = fmaxf(0.5f, PULSE_WIDTH + PULSE_SPREAD * p.age);            // spreads out as it flows
    float amount = PULSE_INTENSITY * p.strength * (1.0f - along) * (PULSE_WIDTH / w);   // each pulse fades as it goes and as it spreads
    CRGB c = mixColour(PULSE_COLOUR, PULSE_TRAIL_COLOUR, along);           // input colour -> trail colour
    for (int i = (int)(p.pos - 2 * w); i <= (int)(p.pos + 2 * w); i++) {
      if (i < 0 || i >= NUM_LEDS) continue;
      if ((i - p.origin) * PULSE_DIRECTION < 0) continue;            // never behind where it came in
      float q = (i - p.pos) / w;
      float a = amount * expf(-q * q);
      a *= 1.0f - PULSE_WAVE_FOLLOW * 0.4f * (1.0f - riverWaveAt(clamp01((float)i / NUM_LEDS)));   // brighter on wave crests
      inkAmount[i] += a;
      inkR[i] += a * c.r; inkG[i] += a * c.g; inkB[i] += a * c.b;
      if (p.cleanPower > 0) cleanMem[i] = fmaxf(cleanMem[i], p.cleanPower * fminf(1.0f, a * 4.0f));
    }
  }
}

// Draw over the animation in leds[]: first the cleaned water, then the pulses,
// then the solid colour on the sections being covered
void drawPulses() {
  for (int i = 0; i < NUM_LEDS; i++) {
    float k = cleanMem[i] * dirtiness;
    if (k > 0) leds[i] = mixColour(leds[i], idealWater((float)i / NUM_LEDS, nullptr), k);   // back to clear river
    if (inkAmount[i] > 0) {
      CRGB c = CRGB(cap255(inkR[i] / inkAmount[i]), cap255(inkG[i] / inkAmount[i]), cap255(inkB[i] / inkAmount[i]));
      leds[i] = mixColour(leds[i], c, fminf(1.0f, inkAmount[i]));
    }
  }
  for (int n = 0; n < NUM_SECTIONS; n++) {                             // the covered sensors' sections: solid colour
    int k = SECTIONS[n].sensor - 1;
    if (k < 0 || k >= NUM_LDRS || inputLevel[k] <= 0) continue;
    int a = (int)(SECTIONS[n].fromM / STRIP_LENGTH_M * NUM_LEDS + 0.5f);   // this section, in LEDs
    int b = (int)(SECTIONS[n].toM   / STRIP_LENGTH_M * NUM_LEDS + 0.5f);
    if (b < a) { int tmp = a; a = b; b = tmp; }
    if (a < 0) a = 0;
    for (int i = a; i < b && i < NUM_LEDS; i++)
      leds[i] = mixColour(leds[i], PULSE_COLOUR, clamp01(inputLevel[k] * PULSE_INTENSITY));
  }
}

// =====================================================================
//  SETUP: runs once at the start
// =====================================================================
float seqTime = 0;             // seconds into the whole loop
unsigned long lastMs = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Manifold exhibition starting ===");

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();

  analogReadResolution(12);                 // sensor readings from 0 to 4095
  analogSetAttenuation(ADC_11db);           // full 0 - 3.3V range
  delay(500);
  for (int k = 0; k < NUM_LDRS; k++) {      // keep the sensors uncovered while it starts
    ldrMax[k] = readLdr(k);
    covered[k] = false; heldFor[k] = 0; nextPulse[k] = 0;
  }
  lastMs = millis();
}

// =====================================================================
//  LOOP: runs over and over, one frame each time
// =====================================================================
void loop() {
  // 1. time since the last frame
  unsigned long now = millis();
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  if (dt > 0.5f) dt = 0.5f;

  advanceRiver(dt);                         // the river keeps flowing under everything
  sewageDoneThisFrame = false;
  readSensors(dt);

  // 2. which animation is playing, and how far into it
  float cycle = 0;
  for (int k = 0; k < NUM_ANIMS; k++) cycle += ANIM_SECONDS[k];
  seqTime += dt;
  while (seqTime >= cycle) seqTime -= cycle;
  int cur = 0;
  float start = 0;
  while (cur < NUM_ANIMS - 1 && seqTime >= start + ANIM_SECONDS[cur]) { start += ANIM_SECONDS[cur]; cur++; }
  float local = seqTime - start;
  int nxt = (cur + 1) % NUM_ANIMS;
  float inLen = TRANS_SECONDS[(cur + NUM_ANIMS - 1) % NUM_ANIMS];   // the transition that brought this one in
  float len = TRANS_SECONDS[cur];                                   // the transition out of this one

  // 3. draw it (and, near the end, mix into the next one)
  renderAnim(cur, local + inLen, dt, bufA);
  if (local >= ANIM_SECONDS[cur] - len) {
    float nt = local - (ANIM_SECONDS[cur] - len);
    renderAnim(nxt, nt, dt, bufB);
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = transitionPixel(TRANS_TYPE[cur], nt / len, bufA[i], bufB[i]);
  } else {
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = bufA[i];
  }

  // 4. the care pulses on top
  updatePulses(dt, cur);
  drawPulses();

  // 5. send it to the strip
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(leds[i].r, leds[i].g, leds[i].b));
  strip.show();

  // 6. Serial Monitor
  static unsigned long lastSensors = 0, lastAnim = 0, frames = 0;
  frames++;
  if (now - lastSensors >= 300) {           // sensors: reading now / max, * = covered
    lastSensors = now;
    Serial.print("Sensors  ");
    for (int k = 0; k < NUM_LDRS; k++)
      Serial.printf("%d(G%d): %4d/%4d%s   ", k + 1, LDR_PINS[k], ldrReading[k], ldrMax[k], covered[k] ? "*" : " ");
    Serial.println();
  }
  if (now - lastAnim >= 1000) {             // animation
    lastAnim = now;
    Serial.printf(">> %s, %.0f s of %.0f | month %d | %lu frames/s\n", NAMES[cur], local, ANIM_SECONDS[cur], floodMonth + 1, frames);
    frames = 0;
  }
  delay(5);
}
