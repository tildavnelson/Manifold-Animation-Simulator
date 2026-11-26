// =====================================================================
//  MANIFOLD - EXHIBITION VERSION 3 (500 cm strip + 9 light sensors)
// =====================================================================
//
//  WHAT IT DOES
//  ------------
//  Nobody touching the sensors: the river animation loops forever:
//     1. RIVER              the ideal river: blue waves with sparkles
//     2. RIVER 2            the "real" waves: darker, slower, green in the deepest parts
//     3. FLOODING & SEWAGE  red floods at House Mill, then sewage from 7 inputs
//     4. REWILDING          clean waves arrive from both ends and the river
//                           becomes the ideal RIVER again, so the loop flows
//                           straight back into 1.
//
//  Touching the sensors ("adding care"): straight away, a burst of strong
//  green speckles shoots down the strip from that sensor's part of it,
//  fading as they go. Holding keeps them coming. Where they pass they
//  clean the pollution (flood, sewage, muck): the longer you hold, the
//  cleaner it gets.
//
//  Emma's points (0 = where the River Lea joins the Thames = LED 0):
//     tidal stretch 0-110 cm, House Mill (floods) at 110 cm,
//     sewage inputs at 105, 160, 275, 300, 325, 375, 460 cm,
//     rewilding area 195-240 cm.
//
//  WIRING
//  ------
//   3.3V ---- LDR ----+---- GPIO 1,2,3,4,5,10,7,8,9   (10k from each GPIO to GND)
//   Strip DATA IN -> GPIO 6 (through the 300 ohm resistor)
//   ESP32 GND, strip GND and the Tagra V- all joined together
//
//  SERIAL MONITOR (Tools > Serial Monitor, 115200 baud)
//  ----------------------------------------------------
//   Every 0.3 s: each sensor's reading now / its brightest (max), and how
//   much it is covered (care %). Every second: which animation is playing.
//
//  QUICK TWEAK GUIDE  (search for "TWEAK" to jump to each one)
//  ------------------
//   First waves (RIVER) ........ RIVER_WAVE_SPEED, RIVER_TROUGH_COLOUR,
//                                RIVER_CREST_COLOUR, RIVER_SPARKLES
//   Real waves (RIVER 2) ....... R2_WAVE_SPEED, R2_TROUGH_COLOUR, R2_CREST_COLOUR,
//                                R2_DEEP_COLOUR, R2_DEEP_AMOUNT
//   Floods / sewage ............ FLOOD_COLOUR, SEWAGE_..._COLOUR, SEWAGE_P1_CM...
//   Sensors .................... CARE_STARTS_AT, CARE_FULL_AT
//   Care speckles .............. SPECKLE_COLOUR, SPECKLES_ON_TOUCH, SPECKLES_PER_SECOND,
//                                SPECKLE_SPEED, SPECKLE_FADE, CLEAN_FULL_SECONDS ...
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
const int LDR_PINS[NUM_LDRS] = { 1, 2, 3, 4, 5, 10, 7, 8, 9 };   // sensor 1 first ... sensor 9 last

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
const CRGB  RIVER_TROUGH_COLOUR   = CRGB(0, 12, 70);   // TWEAK: blue BETWEEN the waves (darker = more contrast). Was (3, 31, 129)
const CRGB  RIVER_CREST_COLOUR    = CRGB(0, 90, 224);  // TWEAK: blue at the top of each wave
const float RIVER_SPARKLES        = 0.40f;             // TWEAK: how many sparkles (0 = none, 1 = lots)
const float RIVER_SPARKLE_SPEED   = 1.00f;             //        how fast sparkles flash and fade
const CRGB  RIVER_SPARKLE_COLOUR  = CRGB(255, 255, 255);
const float RIVER_IRREGULARITY    = 0.00f;             //        0 = even waves, 1 = uneven

// ---------------------------------------------------------------------
//  2. RIVER 2 - the "real" waves
// ---------------------------------------------------------------------
const float R2_WAVE_SPEED         = 0.6f;              // TWEAK: wave speed (1.0 = as on the website, lower = slower)
const CRGB  R2_TROUGH_COLOUR      = CRGB(0, 25, 70);   // TWEAK: colour between the waves. Was (4, 62, 139)
const CRGB  R2_CREST_COLOUR       = CRGB(0, 150, 170); // TWEAK: colour at the top of each wave. Was (0, 255, 238)
const CRGB  R2_DEEP_COLOUR        = CRGB(0, 60, 25);   // TWEAK: the green in the deepest parts
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
const CRGB  SEWAGE_INK_COLOUR    = CRGB(230, 99, 0);    // TWEAK: fresh sewage
const CRGB  SEWAGE_MUCK_COLOUR   = CRGB(138, 55, 0);    // TWEAK: murky water
const float SEWAGE_MUCKINESS     = 0.60f;
#define SEWAGE_POINTS 7
const float SEWAGE_POS_CM[SEWAGE_POINTS] = { 105, 160, 275, 300, 325, 375, 460 };   // Emma's sewage inputs
const float SEWAGE_EVERY[SEWAGE_POINTS]  = { 6, 12, 0, 18, 9, 0, 25 };             // seconds between discharges (0 = once)

// ---------------------------------------------------------------------
//  4. REWILDING
// ---------------------------------------------------------------------
const float REWILD_START_POLLUTION = 0.45f;     // how dirty the water is at the start
const int   REWILD_PLANTS          = 20;        // plants growing in the rewilding area
const float REWILD_FROM_CM         = 195.0f;    // rewilding area (Emma)
const float REWILD_TO_CM           = 240.0f;
const float REWILD_SIZE_CM         = 3.0f;      // how big the plants grow
const CRGB  REWILD_COLOUR          = CRGB(4, 231, 57);

// ---------------------------------------------------------------------
//  SENSORS - how covered each one is
//  Each sensor remembers its brightest reading (its max). "Care" goes
//  from 0% (normal light) to 100% (fully covered):
// ---------------------------------------------------------------------
const float CARE_STARTS_AT = 0.85f;   // TWEAK: care starts when the light drops below 85% of the max (a light touch)
const float CARE_FULL_AT   = 0.35f;   // TWEAK: full care at 35% of the max (fully covered)

// ---------------------------------------------------------------------
//  CARE SPECKLES - what touching does
//  Touch a sensor: a burst of green speckles shoots down the strip from
//  that sensor's part of it, fading as they go (like the red pulse test).
//  Keep holding: more keep coming. Where they pass, they clean the
//  pollution: the longer you hold, the more it cleans.
// ---------------------------------------------------------------------
const CRGB  SPECKLE_COLOUR       = CRGB(0, 255, 30);  // TWEAK: the strong green of the speckles
const float GREEN_VARIATION      = 0.15f;             // TWEAK: how much each speckle varies, lighter/darker (0 = all the same)
const int   SPECKLES_ON_TOUCH    = 6;                 // TWEAK: speckles in the first burst, the moment you touch
const float SPECKLES_PER_SECOND  = 15.0f;             // TWEAK: speckles per second while it stays fully covered
const float SPECKLE_SPEED        = 40.0f;             // TWEAK: how fast they shoot down the strip (LEDs per second)
const float SPECKLE_FADE         = 60.0f;             // TWEAK: LEDs travelled before a speckle has faded away
const int   SPECKLE_DIRECTION    = 1;                 // TWEAK: 1 = towards the last LED, -1 = towards LED 0
const float SPECKLE_SIZE         = 1.0f;              //        size in LEDs
const float CLEAN_FULL_SECONDS   = 5.0f;              // TWEAK: hold this long to clean the pollution completely
const float CLEAN_RADIUS         = 3.0f;              // TWEAK: how far each speckle cleans either side (LEDs)
const float CLEAN_STAYS_SECONDS  = 8.0f;              // TWEAK: how long cleaned water stays clean afterwards

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
  // each input discharges on its own rhythm
  for (int k = 0; k < SEWAGE_POINTS; k++) {
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
    float mk = muck * (0.75f + 0.25f * sinf(TWO_PI * (x / 0.9f - riverClock * 13.0f / 600.0f)));
    c = mixColour(c, SEWAGE_MUCK_COLOUR, mk);
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
  float back = 1.0f - smooth01((t - 40.0f) / 15.0f);       // plants fade as the river returns to normal
  drawRiver(t, dt, cleanRiver, rewildSparkles);            // the clean water = the RIVER animation itself

  int n = REWILD_PLANTS < 60 ? REWILD_PLANTS : 60;
  float pos[60], rad[60], bright[60];
  int count = 0;
  for (int k = 0; k < n; k++) {
    float age = t - (4.0f + hash01(k + 61) * 20.0f);        // planted between 4 s and 24 s
    if (age < 0) continue;
    float grow = smooth01(age / 10.0f);
    pos[count] = (REWILD_FROM_CM + hash01(k + 81) * (REWILD_TO_CM - REWILD_FROM_CM)) / 100.0f;
    rad[count] = 0.03f + grow * REWILD_SIZE_CM / 100.0f;
    bright[count] = (0.35f + 0.65f * grow) * (0.85f + 0.15f * sinf(t * 1.3f + k));
    count++;
  }
  for (int i = 0; i < NUM_LEDS; i++) {
    float x = (i + 0.5f) / NUM_LEDS * L;
    CRGB c = cleanRiver[i];
    float clean = x < mill ? smooth01((fl - x) / 0.6f + 0.5f) : smooth01((x - fr) / 0.6f + 0.5f);
    clean = fmaxf(clean, allClean);
    float dirt = REWILD_START_POLLUTION * (1.0f - clean) * (0.8f + 0.2f * sinf(TWO_PI * (x / 0.9f - riverClock * 13.0f / 600.0f)));
    c = mixColour(c, SEWAGE_MUCK_COLOUR, dirt);                     // dirty where the clean waves haven't reached
    float qf = x < mill ? (x - fl) / 0.3f : (x - fr) / 0.3f;
    c = mixColour(c, RIVER_CREST_COLOUR, 0.5f * glowOn * expf(-qf * qf));   // the arriving clean waves glow
    float m = 0;
    for (int k = 0; k < count; k++) {
      float q = (x - pos[k]) / rad[k];
      if (q > -3 && q < 3) m = fmaxf(m, bright[k] * expf(-q * q));
    }
    out[i] = mixColour(c, REWILD_COLOUR, m * back);                // the plants
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
float care[NUM_LDRS];         // how covered: 0 = not at all, 1 = fully
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
    float ratio = ldrReading[k] / fmaxf(1.0f, (float)ldrMax[k]);         // 1 = normal light, 0 = dark
    care[k] = clamp01((CARE_STARTS_AT - ratio) / (CARE_STARTS_AT - CARE_FULL_AT));
    if (care[k] > 0) heldFor[k] += dt;                                    // touched: count up
    else heldFor[k] = 0;                                                 // let go: start counting again next time
  }
}

// =====================================================================
//  CARE SPECKLES: green specks that shoot down the strip from a touched
//  sensor, fade as they go, and clean the pollution where they pass
// =====================================================================
#define MAX_SPECKLES 120
struct Speckle { float x, travelled, speed, size, cleanPower, sparkle; CRGB colour; bool on; };
Speckle speckles[MAX_SPECKLES];
float spawnDebt[NUM_LDRS];     // counts towards the next speckle for each sensor
bool  wasTouched[NUM_LDRS];    // touched last frame? (to spot a new touch)
float cleanMem[NUM_LEDS];      // how clean each LED has been made (0-1), fades over CLEAN_STAYS_SECONDS
float dirtiness = 0;           // 0 while the water is clean (RIVER, RIVER 2), 1 while dirty

void addSpeckle(int k) {
  for (int s = 0; s < MAX_SPECKLES; s++) {
    if (speckles[s].on) continue;
    float zone = (float)NUM_LEDS / NUM_LDRS;                         // each sensor has a ninth of the strip
    speckles[s].x = (k + 0.3f + 0.4f * rand01()) * zone;             // around the middle of this sensor's part
    speckles[s].travelled = 0;
    speckles[s].speed = SPECKLE_SPEED * (0.7f + 0.6f * rand01());    // each a little faster or slower: a speckled spray
    speckles[s].size = SPECKLE_SIZE * (0.7f + 0.6f * rand01());
    speckles[s].sparkle = rand01() * TWO_PI;
    float v = (rand01() - 0.5f) * 2.0f * GREEN_VARIATION;            // a bit lighter or darker
    speckles[s].colour = v > 0 ? mixColour(SPECKLE_COLOUR, CRGB(180, 255, 180), v) : dimColour(SPECKLE_COLOUR, 1.0f + v);
    speckles[s].cleanPower = clamp01(heldFor[k] / CLEAN_FULL_SECONDS);   // held longer = cleans more
    speckles[s].on = true;
    return;
  }
}

void updateSpeckles(float dt, int currentAnim) {
  // 1. new speckles: a burst the moment a sensor is touched, then more while it stays covered
  for (int k = 0; k < NUM_LDRS; k++) {
    bool touched = care[k] > 0;
    if (touched && !wasTouched[k]) for (int j = 0; j < SPECKLES_ON_TOUCH; j++) addSpeckle(k);
    wasTouched[k] = touched;
    spawnDebt[k] += SPECKLES_PER_SECOND * care[k] * dt;
    while (spawnDebt[k] >= 1.0f) { addSpeckle(k); spawnDebt[k] -= 1.0f; }
    if (!touched) spawnDebt[k] = 0;
  }
  // 2. move them down the strip; they disappear once faded or off the end
  for (int s = 0; s < MAX_SPECKLES; s++) {
    if (!speckles[s].on) continue;
    Speckle& p = speckles[s];
    p.travelled += p.speed * dt;
    p.x += SPECKLE_DIRECTION * p.speed * dt;
    if (p.travelled > SPECKLE_FADE || p.x < -3 || p.x > NUM_LEDS + 3) p.on = false;
  }
  // 3. cleaning: only matters while the water is dirty (FLOODING & SEWAGE, REWILDING)
  float wantDirty = (currentAnim == 2 || currentAnim == 3) ? 1.0f : 0.0f;
  dirtiness += (wantDirty - dirtiness) * fminf(1.0f, dt / 2.0f);         // changes smoothly over about 2 s
  float keep = expf(-dt / CLEAN_STAYS_SECONDS);
  for (int i = 0; i < NUM_LEDS; i++) cleanMem[i] *= keep;                 // cleaned water slowly gets dirty again
  for (int s = 0; s < MAX_SPECKLES; s++) {
    if (!speckles[s].on || speckles[s].cleanPower <= 0) continue;
    int a = (int)(speckles[s].x - CLEAN_RADIUS), b = (int)(speckles[s].x + CLEAN_RADIUS);
    for (int i = a; i <= b; i++) {
      if (i < 0 || i >= NUM_LEDS) continue;
      float d = fabsf(i - speckles[s].x) / CLEAN_RADIUS;
      cleanMem[i] = fmaxf(cleanMem[i], speckles[s].cleanPower * (1.0f - d * d));   // held longer = cleaner
    }
  }
}

// Draw over the animation in leds[]: first the cleaned water, then the speckles
void drawSpeckles() {
  for (int i = 0; i < NUM_LEDS; i++) {
    float k = cleanMem[i] * dirtiness;
    if (k > 0) leds[i] = mixColour(leds[i], idealWater((float)i / NUM_LEDS, nullptr), k);   // back to clear river
  }
  for (int s = 0; s < MAX_SPECKLES; s++) {
    if (!speckles[s].on) continue;
    Speckle& p = speckles[s];
    float fade = 1.0f - p.travelled / SPECKLE_FADE;                       // fades as it travels
    float twinkle = 0.75f + 0.25f * sinf(p.travelled * 0.8f + p.sparkle); // each one flickers a little
    float strength = fade * twinkle;
    int a = (int)(p.x - p.size - 1), b = (int)(p.x + p.size + 1);
    for (int i = a; i <= b; i++) {
      if (i < 0 || i >= NUM_LEDS) continue;
      float q = (i - p.x) / p.size;
      float k = strength * expf(-q * q);
      if (k > 0.01f) leds[i] = mixColour(leds[i], p.colour, k);
    }
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
    care[k] = 0; heldFor[k] = 0; spawnDebt[k] = 0; wasTouched[k] = false;
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

  // 4. the care speckles on top
  updateSpeckles(dt, cur);
  drawSpeckles();

  // 5. send it to the strip
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(leds[i].r, leds[i].g, leds[i].b));
  strip.show();

  // 6. Serial Monitor
  static unsigned long lastSensors = 0, lastAnim = 0, frames = 0;
  frames++;
  if (now - lastSensors >= 300) {           // sensors: reading now / max, care %
    lastSensors = now;
    Serial.print("Sensors  ");
    for (int k = 0; k < NUM_LDRS; k++)
      Serial.printf("%d(G%d): %4d/%4d %3d%%   ", k + 1, LDR_PINS[k], ldrReading[k], ldrMax[k], (int)(care[k] * 100));
    Serial.println();
  }
  if (now - lastAnim >= 1000) {             // animation
    lastAnim = now;
    Serial.printf(">> %s, %.0f s of %.0f | month %d | %lu frames/s\n", NAMES[cur], local, ANIM_SECONDS[cur], floodMonth + 1, frames);
    frames = 0;
  }
  delay(5);
}
