// =====================================================================
//  EXHIBITION VERSION - 500 cm strip with 9 light sensors (LDRs)
//  Based on the Manifold Light Simulator animation.
//
//  Nobody touching the sensors: the river animation loops
//  (RIVER, RIVER 2, FLOODING & SEWAGE, REWILDING), with Emma's points:
//    0 = where the River Lea joins the Thames
//    tidal stretch 0-110 cm, House Mill (floods) at 110 cm,
//    sewage inputs at 105, 160, 275, 300, 325, 375, 460 cm,
//    rewilding area 195-240 cm.
//
//  Cover a sensor: green pulses (the rewilding green) travel down the
//  strip from that sensor's part of the strip, fading as they go.
//  The longer it stays covered, the darker the green.
//
//  WIRING
//   3.3V ---- LDR ----+---- GPIO 1,2,3,4,5,10,7,8,9   (10k from each GPIO to GND)
//   Strip DATA IN -> GPIO 6 (through the 300 ohm resistor)
//   ESP32 GND, strip GND and the Tagra V- all joined together
// =====================================================================

#include <Adafruit_NeoPixel.h>    // sends the colours to the strips, like the test sketch

// Watchdog: restarts the board by itself if it ever freezes.
// Off (0) while testing. Change to 1 once everything works.
#define USE_WATCHDOG 0
#if USE_WATCHDOG && defined(ESP32)
#include <esp_task_wdt.h>
#endif

#ifndef PI
#define PI 3.14159265358979f
#endif
#ifndef TWO_PI
#define TWO_PI 6.28318530717959f
#endif

// ----- Hardware: one strip on GPIO 6 -----
#define DATA_PIN          6
#define NUM_LEDS          100   // <- the number of PIXELS on the 500 cm strip

// Brightness (0 to 255). Raise it carefully, checking that the power
// supply and wires stay cool.
#define BRIGHTNESS        150

// ----- Sequence -----
#define NUM_ANIMS      4

// ---------------------------------------------------------------------
//  COLOURS: a small colour type, so the sketch doesn't need FastLED
// ---------------------------------------------------------------------
struct CRGB {
  uint8_t r, g, b;
  CRGB() : r(0), g(0), b(0) {}
  constexpr CRGB(uint8_t rr, uint8_t gg, uint8_t bb) : r(rr), g(gg), b(bb) {}
  CRGB& nscale8(uint8_t s) {                       // dim the colour (255 = unchanged)
    r = ((uint16_t)r * (s + 1)) >> 8; g = ((uint16_t)g * (s + 1)) >> 8; b = ((uint16_t)b * (s + 1)) >> 8;
    return *this;
  }
  CRGB operator|(const CRGB& o) const {            // the brighter of two colours, channel by channel
    return CRGB(r > o.r ? r : o.r, g > o.g ? g : o.g, b > o.b ? b : o.b);
  }
};
// Mix from colour a to colour b (amount 0 = all a, 255 = all b)
CRGB blend(CRGB a, CRGB b, uint8_t amount) {
  return CRGB(a.r + (((int)b.r - a.r) * amount) / 255, a.g + (((int)b.g - a.g) * amount) / 255, a.b + (((int)b.b - a.b) * amount) / 255);
}

// Transition types
#define T_FADE      0
#define T_WIPE_LR   1
#define T_WIPE_RL   2
#define T_OVERLAY   3

// ===== SETTINGS FROM THE SIMULATOR =====
// Animations: 0 = RIVER, 1 = RIVER 2, 2 = FLOODING & SEWAGE, 3 = REWILDING
const float   STRIP_LENGTH_M       = 5.00f;    // real length of the strip, in metres
const float   HOUSE_MILL_CM        = 110.0f;   // House Mill (floods), from the Thames end
const float   TIDE_CHANGES_PER_MIN = 2.0f;   // left of the mill: towards it, then away
// Flood events touching the floorboards, January to December 2025: one pulse each in FLOODING & SEWAGE
const uint8_t FLOOD_EVENTS[12]     = { 5, 5, 11, 11, 6, 3, 1, 3, 9, 12, 9, 5 };
const uint8_t START_MONTH          = 0;      // 0 = January 2025; moves on one month each time round
const uint8_t PLAY_ORDER[NUM_ANIMS]    = { 0, 1, 2, 3 };
const float   LIGHT[NUM_ANIMS]         = { 1.00f, 1.00f, 1.00f, 1.00f };
const float   SPEED[NUM_ANIMS]         = { 1.00f, 1.00f, 1.00f, 1.00f };
const float   ANIM_SECONDS[NUM_ANIMS]  = { 60.0f, 60.0f, 90.0f, 60.0f };   // length of each animation
// Transition after each position (the last one loops back to the first)
const uint8_t TRANS_TYPE[NUM_ANIMS]    = { 0, 3, 0, 0 };
const float   TRANS_SECONDS[NUM_ANIMS] = { 5.0f, 8.0f, 5.0f, 2.0f };
// 1. RIVER
const CRGB   RIVER_TROUGH_COLOUR      = CRGB(3, 31, 129);
const CRGB   RIVER_CREST_COLOUR       = CRGB(0, 90, 224);
const float  RIVER_TWINKLE_AMOUNT     = 0.40f;
const float  RIVER_TWINKLE_SPEED      = 1.00f;
const CRGB   RIVER_TWINKLE_COLOUR     = CRGB(255, 255, 255);
const float  RIVER_IRREGULARITY       = 0.00f;
// 2. RIVER 2
const CRGB   R2_TROUGH_COLOUR         = CRGB(4, 62, 139);
const CRGB   R2_CREST_COLOUR          = CRGB(0, 255, 238);
const float  R2_SPACING_CM            = 200.0f;
const int    R2_LAYERS                = 4;
const float  R2_IRREGULARITY          = 0.30f;
const float  R2_SHALLOW_CM            = 375.0f;   // was 750 on the 10 m strip
const float  R2_SHALLOW_WIDTH_CM      = 80.0f;
const float  R2_SHALLOW_AMOUNT        = 0.60f;
const CRGB   R2_SHALLOW_COLOUR        = CRGB(69, 165, 82);
const float  R2_TWINKLE_AMOUNT        = 0.05f;
const float  R2_TWINKLE_SPEED         = 1.00f;
const CRGB   R2_TWINKLE_COLOUR        = CRGB(223, 242, 238);
// 3. FLOODING & SEWAGE
const float  FS_SECONDS               = 90.0f;
const float  FLOOD_START              = 0.0f;
const float  FLOOD_END                = 50.0f;
const float  SEWAGE_START             = 40.0f;
const float  SEWAGE_END               = 90.0f;
const float  FLOOD_REACH_CM           = 50.0f;
const float  FLOOD_DRAIN_SECONDS      = 2.5f;
const float  FLOOD_SPREAD_CM          = 40.0f;
const CRGB   FLOOD_COLOUR             = CRGB(255, 26, 10);
const CRGB   SEWAGE_INK_COLOUR        = CRGB(230, 99, 0);
const CRGB   SEWAGE_MUCK_COLOUR       = CRGB(138, 55, 0);
const float  SEWAGE_MUCKINESS         = 0.60f;
const float  SEWAGE_P1_CM             = 105.0f;   // Emma's sewage input
const float  SEWAGE_E1                = 6.0f;
const float  SEWAGE_P2_CM             = 160.0f;   // Emma's sewage input
const float  SEWAGE_E2                = 12.0f;
const float  SEWAGE_P3_CM             = 275.0f;   // Emma's sewage input
const float  SEWAGE_E3                = 0.0f;
const float  SEWAGE_P4_CM             = 300.0f;   // Emma's sewage input
const float  SEWAGE_E4                = 18.0f;
const float  SEWAGE_P5_CM             = 325.0f;   // Emma's sewage input
const float  SEWAGE_E5                = 9.0f;
const float  SEWAGE_P6_CM             = 375.0f;   // Emma's sewage input
const float  SEWAGE_E6                = 0.0f;
const float  SEWAGE_P7_CM             = 460.0f;   // Emma's sewage input
const float  SEWAGE_E7                = 25.0f;
// 4. REWILDING
const float  REWILD_START_POLLUTION   = 0.45f;
const int    REWILD_PLANTS            = 20;
const float  REWILD_FROM_CM           = 195.0f;   // rewilding area starts (Emma)
const float  REWILD_TO_CM             = 240.0f;   // rewilding area ends (Emma)
const float  REWILD_SIZE_CM           = 3.0f;
const CRGB   REWILD_COLOUR            = CRGB(4, 231, 57);
// =======================================

CRGB leds[NUM_LEDS];
CRGB bufA[NUM_LEDS];
CRGB bufB[NUM_LEDS];

// ---------------------------------------------------------------------
//  THE STRIP (one strip, LED 0 = the Thames end)
// ---------------------------------------------------------------------
Adafruit_NeoPixel strip(NUM_LEDS, DATA_PIN, NEO_GRB + NEO_KHZ800);

void setVirtualPixel(int index, uint32_t color) {
  if (index >= 0 && index < NUM_LEDS) strip.setPixelColor(index, color);
}

void showAllStrips() {
  strip.show();
}

void clearAllStrips() {
  for (int i = 0; i < NUM_LEDS; i++) setVirtualPixel(i, 0);
  showAllStrips();
}

// ---------------------------------------------------------------------
//  HELPERS
// ---------------------------------------------------------------------
float rand01() { return random(10000) / 10000.0f; }
float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
float smooth01(float x) { x = clamp01(x); return x * x * (3.0f - 2.0f * x); }
float fract01(float x) { return x - floorf(x); }
float wrapTo(float x, float p) { return x - p * floorf(x / p); }
float mixf(float a, float b, float k) { return a + (b - a) * k; }
uint8_t cap255(float v) { return v <= 0 ? 0 : (v >= 255.0f ? 255 : (uint8_t)v); }

// Repeatable "random" number 0-1 for a given whole number (same as the simulator)
float hash01(uint32_t k) {
  uint32_t h = k * 2654435761u;
  h ^= h >> 16; h *= 2246822519u;
  h ^= h >> 13; h *= 3266489917u;
  h ^= h >> 16;
  return (h & 0xFFFFFF) / 16777216.0f;
}

// Twinkles: one flash per LED that rises quickly and fades
float glintStep(float &g, float &tg, float crest, float density, float rate,
                float frameScale, float fade, float lively) {
  if (tg > 0) {
    g += (tg - g) * fminf(1.0f, 0.6f * frameScale * rate);
    if (g >= tg * 0.9f) tg = 0;
  } else {
    g *= fade;
  }
  if (tg == 0 && g < 0.2f && rand01() < density * 0.3f * crest * lively * frameScale * (300.0f / NUM_LEDS)) {
    tg = fminf(1.0f, 0.35f + rand01() * 0.6f * crest + (rand01() < 0.08f ? 0.4f : 0.0f));
  }
  return g;
}

// ---------------------------------------------------------------------
//  THE RIVER ITSELF (shared by every animation)
//  Right of House Mill the water always flows towards the mill (right to
//  left). Left of it the river is tidal: towards the mill, then away,
//  changing direction TIDE_CHANGES_PER_MIN times a minute. Everything here stays small, so it is accurate all year.
// ---------------------------------------------------------------------
#define FLOW        0.027f                 // river flow (fraction of the strip per second)
#define WAVE1       0.15f                  // RIVER wave lengths (fraction of the strip)
#define WAVE2       (0.15f * 0.37f)
#define MILL_BLEND  0.03f                  // how softly the two sides of the mill meet
float tidePhase = PI * 0.1f, riverClock = 0;
float offL1 = 0, offL2 = 0, offR1 = 0, offR2 = 0;

float millU() { return clamp01(HOUSE_MILL_CM / 100.0f / STRIP_LENGTH_M); }
float tideDir() { return tanhf(6.0f * sinf(tidePhase)); }          // +1 flowing right, -1 flowing left
float flowDirAt(float u) { return u < millU() ? tideDir() : -1.0f; }   // right of the mill: always towards it

void advanceRiver(float dt) {
  tidePhase = wrapTo(tidePhase + PI * TIDE_CHANGES_PER_MIN / 60.0f * dt, TWO_PI);
  float v = FLOW * SPEED[0] * dt, d = tideDir();
  riverClock = wrapTo(riverClock + dt * SPEED[0], 600.0f);
  offL1 = wrapTo(offL1 + v * d, WAVE1);  offL2 = wrapTo(offL2 + v * d * 1.3f, WAVE2);
  offR1 = wrapTo(offR1 - v, WAVE1);      offR2 = wrapTo(offR2 - v * 1.3f, WAVE2);   // towards the mill
}

float warpU(float u, float irr) {
  float T = riverClock;
  return u + irr * (0.04f * sinf(TWO_PI * (u * 1.7f + T * 19.0f / 600.0f)) + 0.025f * sinf(TWO_PI * (u * 3.9f - T * 28.0f / 600.0f) + 1.3f));
}
float swellAt(float u, float irr) { return 1.0f - irr * 0.45f * (0.5f + 0.5f * sinf(TWO_PI * (u * 2.3f + riverClock * 11.0f / 600.0f) + 0.7f)); }
float riverShape(float uw, float o1, float o2) {
  return 0.7f * (sinf(TWO_PI * (uw - o1) / WAVE1) + 1.0f) * 0.5f + 0.3f * (sinf(TWO_PI * (uw - o2) / WAVE2 + PI) + 1.0f) * 0.5f;
}
// Shape of the ideal river at position u: 0 = trough, 1 = crest
float riverWaveAt(float u) {
  float irr = RIVER_IRREGULARITY, uw = warpU(u, irr);
  float k = smooth01((u - millU()) / MILL_BLEND + 0.5f), s = 0;   // 0 = tidal side, 1 = steady side
  if (k < 1) s += (1.0f - k) * riverShape(uw, offL1, offL2);
  if (k > 0) s += k * riverShape(uw, offR1, offR2);
  return powf(s * swellAt(u, irr), 2.2f);
}
// Colour of the ideal river (no twinkles) at position u
float idealWater(float u, float &r, float &g, float &b) {
  float w = riverWaveAt(u);
  r = mixf(RIVER_TROUGH_COLOUR.r, RIVER_CREST_COLOUR.r, w);
  g = mixf(RIVER_TROUGH_COLOUR.g, RIVER_CREST_COLOUR.g, w);
  b = mixf(RIVER_TROUGH_COLOUR.b, RIVER_CREST_COLOUR.b, w);
  return w;
}

// ---------------------------------------------------------------------
//  ANIMATIONS: each fills out[] for time t (seconds, already scaled by
//  its SPEED). u = i / NUM_LEDS is the position from 0 (left) to 1 (right),
//  x is the same position in metres.
// ---------------------------------------------------------------------

// 1. RIVER: the ideal river, with sun glitter on the crests
void fxRiver(float t, float dt, CRGB* out) {
  static float lastT = -1;
  static float glint[NUM_LEDS], target[NUM_LEDS];
  if (lastT < 0 || t < lastT) { for (int i = 0; i < NUM_LEDS; i++) { glint[i] = 0; target[i] = 0; } }
  lastT = t;
  float frameScale = dt / 0.035f, fade = powf(0.78f, frameScale * RIVER_TWINKLE_SPEED);
  float t2 = 0.68f * 0.68f, mill = millU(), calm = fabsf(tideDir());
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, r, g, b;
    float wave = idealWater(u, r, g, b);
    float crest = smooth01((wave - t2) / (1.0f - t2));
    float lively = u < mill ? 0.5f + 0.5f * calm : 1.0f;      // the tidal side is calmer while it turns
    float gl = glintStep(glint[i], target[i], crest, RIVER_TWINKLE_AMOUNT, RIVER_TWINKLE_SPEED, frameScale, fade, lively);
    gl = fminf(1.0f, gl + crest * 0.1f * lively);
    out[i] = CRGB(cap255(r + RIVER_TWINKLE_COLOUR.r * gl), cap255(g + RIVER_TWINKLE_COLOUR.g * gl), cap255(b + RIVER_TWINKLE_COLOUR.b * gl));
  }
}

// 2. RIVER 2: realistic layered waves (longer waves travel faster) and a green shallows
void fxRiver2(float t, float dt, CRGB* out) {
  static float lastT = -1;
  static float glint[NUM_LEDS], target[NUM_LEDS];
  static float oL[4], oR[4];
  const float SIZES[4] = { 1.0f, 0.57f, 0.35f, 0.22f };
  const float AMPS[4] = { 1.0f, 0.55f, 0.3f, 0.18f };
  const float PHASES[4] = { 0.0f, 1.7f, 4.1f, 2.6f };
  if (lastT < 0 || t < lastT) {
    for (int i = 0; i < NUM_LEDS; i++) { glint[i] = 0; target[i] = 0; }
    for (int k = 0; k < 4; k++) { oL[k] = 0; oR[k] = 0; }
  }
  lastT = t;
  const float L = STRIP_LENGTH_M, irr = R2_IRREGULARITY;
  int layers = R2_LAYERS < 1 ? 1 : (R2_LAYERS > 4 ? 4 : R2_LAYERS);
  float base = fmaxf(0.01f, R2_SPACING_CM / 100.0f / L);       // longest wave (fraction of the strip)
  float d = tideDir(), lens[4] = { 1, 1, 1, 1 }, sumA = 0;
  for (int k = 0; k < layers; k++) {
    lens[k] = base * SIZES[k];
    float c = 0.03f * sqrtf(SIZES[k]);                         // longer waves travel faster
    oL[k] = wrapTo(oL[k] + c * d * dt, lens[k]);
    oR[k] = wrapTo(oR[k] - c * dt, lens[k]);                    // right side: towards the mill
    sumA += AMPS[k];
  }
  float sx = R2_SHALLOW_CM / 100.0f, sw = fmaxf(0.05f, R2_SHALLOW_WIDTH_CM / 100.0f);
  float frameScale = dt / 0.035f, fade = powf(0.78f, frameScale * R2_TWINKLE_SPEED);
  float mill = millU(), calm = fabsf(d);
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, x = (i + 0.5f) / NUM_LEDS * L;
    float uw = warpU(u, irr);
    float kk = smooth01((u - mill) / MILL_BLEND + 0.5f);
    float hL = 0, hR = 0;
    for (int k = 0; k < layers; k++) {
      if (kk < 1) hL += AMPS[k] * sinf(TWO_PI * (uw - oL[k]) / lens[k] + PHASES[k]);
      if (kk > 0) hR += AMPS[k] * sinf(TWO_PI * (uw - oR[k]) / lens[k] + PHASES[k]);
    }
    float h = (hL * (1.0f - kk) + hR * kk) / sumA;
    float env = 0.7f + 0.3f * sinf(TWO_PI * (u * 0.9f - riverClock * 7.0f / 600.0f) + 0.4f);   // slow sets of bigger waves
    float wave = powf(clamp01((h * env + 1.0f) / 2.0f * swellAt(u, irr)), 1.8f);                 // sharper crests, broad troughs
    float crest = smooth01((wave - 0.55f) / 0.45f);
    float lively = u < mill ? 0.5f + 0.5f * calm : 1.0f;
    float gl = glintStep(glint[i], target[i], crest, R2_TWINKLE_AMOUNT, R2_TWINKLE_SPEED, frameScale, fade, lively);
    float q = (x - sx) / sw, shallow = R2_SHALLOW_AMOUNT * expf(-q * q);
    float r  = mixf(R2_TROUGH_COLOUR.r, R2_CREST_COLOUR.r, wave);
    float g  = mixf(R2_TROUGH_COLOUR.g, R2_CREST_COLOUR.g, wave);
    float b  = mixf(R2_TROUGH_COLOUR.b, R2_CREST_COLOUR.b, wave);
    r = mixf(r, R2_SHALLOW_COLOUR.r * (0.55f + 0.45f * wave), shallow);
    g = mixf(g, R2_SHALLOW_COLOUR.g * (0.55f + 0.45f * wave), shallow);
    b = mixf(b, R2_SHALLOW_COLOUR.b * (0.55f + 0.45f * wave), shallow);
    out[i] = CRGB(cap255(r + R2_TWINKLE_COLOUR.r * gl), cap255(g + R2_TWINKLE_COLOUR.g * gl), cap255(b + R2_TWINKLE_COLOUR.b * gl));
  }
}

// Flooding part: one red surge from House Mill for each flood event in the current month.
//   Each surge rushes out to the reach on both sides of the mill, then drains away.
//   The surges are spread evenly over the whole flooding part (D seconds).
int floodMonth = START_MONTH;
void fxFlooding(float t, float dt, CRGB* out, float D) {
  const float L = STRIP_LENGTH_M, x0 = HOUSE_MILL_CM / 100.0f;
  const int n = FLOOD_EVENTS[floodMonth];
  const float reach = fmaxf(0.05f, FLOOD_REACH_CM / 100.0f), v = FLOOD_SPREAD_CM / 100.0f;
  const float every = D / (n > 0 ? n : 1);                   // one flood in the middle of each equal slice of the time
  const float rise = reach / v;                              // seconds to rush out to the reach
  const float drain = fmaxf(0.2f, FLOOD_DRAIN_SECONDS);      // seconds each surge takes to fade away
  const float life = rise + drain;
  int kMax = (int)floorf(t / every - 0.5f);
  if (kMax > n - 1) kMax = n - 1;
  int kMin = kMax - (int)ceilf(life / every) - 1;
  if (kMin < 0) kMin = 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, x = (i + 0.5f) / NUM_LEDS * L, d = fabsf(x - x0), r, g, b;
    idealWater(u, r, g, b);
    float m = n > 0 ? 0.1f : 0.0f;                           // a faint red tint around the mill in a flooding month
    for (int k = kMin; k <= kMax; k++) {
      float age = t - (k + 0.5f) * every;
      if (age < 0 || age > life) continue;
      float rr = fminf(reach, age * v);
      float q = (d - rr) / 0.08f;
      float inside = d <= rr ? 1.0f : expf(-q * q);
      float front = expf(-q * q) * (1.0f - smooth01(age / rise)) * 0.5f;
      float fade = age < rise ? 1.0f : 1.0f - smooth01((age - rise) / drain);
      m = fmaxf(m, fminf(1.0f, 0.8f * inside + front) * fade);
    }
    m *= smooth01((reach + 0.075f - d) / 0.15f);             // nothing beyond the reach
    out[i] = CRGB(cap255(mixf(r, FLOOD_COLOUR.r, m)), cap255(mixf(g, FLOOD_COLOUR.g, m)), cap255(mixf(b, FLOOD_COLOUR.b, m)));
  }
}

// Sewage part: 8 pollution points discharging like ink poured into water
//    (later, the discharges can follow real pollution data)
#define SEWAGE_POINTS  7
#define MAX_PLUMES     48
const float SEWAGE_POS_CM[SEWAGE_POINTS] = { SEWAGE_P1_CM, SEWAGE_P2_CM, SEWAGE_P3_CM, SEWAGE_P4_CM,
                                             SEWAGE_P5_CM, SEWAGE_P6_CM, SEWAGE_P7_CM };
const float SEWAGE_EVERY[SEWAGE_POINTS]  = { SEWAGE_E1, SEWAGE_E2, SEWAGE_E3, SEWAGE_E4,
                                             SEWAGE_E5, SEWAGE_E6, SEWAGE_E7 };   // 0 = only once
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
  if (lastT < 0 || t < lastT) {
    for (int p = 0; p < MAX_PLUMES; p++) plumes[p].on = false;
    muck = 0; prevT = t;
  }
  lastT = t;
  // 1. discharges: each point on its own rhythm
  int active = 0;
  for (int p = 0; p < MAX_PLUMES; p++) if (plumes[p].on) active++;
  for (int k = 0; k < SEWAGE_POINTS; k++) {
    float first = 2.0f + hash01(k + 11) * 8.0f;
    int n = dischargeCount(t, first, SEWAGE_EVERY[k]) - dischargeCount(prevT, first, SEWAGE_EVERY[k]);
    if (n > 2) n = 2;
    for (int j = 0; j < n; j++) {
      if (active < MAX_PLUMES) {
        for (int p = 0; p < MAX_PLUMES; p++) if (!plumes[p].on) {
          plumes[p].x = SEWAGE_POS_CM[k] / 100.0f; plumes[p].age = 0; plumes[p].on = true; active++; break;
        }
      }
      muck = fminf(0.75f, muck + 0.07f * SEWAGE_MUCKINESS);
    }
  }
  // 2. the ink drifts with the river, spreads and fades; the muck slowly settles
  muck *= expf(-dt / 90.0f);
  float v = FLOW * L;
  for (int p = 0; p < MAX_PLUMES; p++) {
    if (!plumes[p].on) continue;
    plumes[p].age += dt;
    plumes[p].x += v * flowDirAt(plumes[p].x / L) * dt;
    if (plumes[p].age >= 40.0f || plumes[p].x <= -1.0f || plumes[p].x >= L + 1.0f) plumes[p].on = false;
  }
  // 3. colours
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, x = (i + 0.5f) / NUM_LEDS * L, r, g, b;
    float wave = idealWater(u, r, g, b);
    float mk = muck * (0.75f + 0.25f * sinf(TWO_PI * (x / 0.9f - riverClock * 13.0f / 600.0f)));
    r = mixf(r, SEWAGE_MUCK_COLOUR.r, mk); g = mixf(g, SEWAGE_MUCK_COLOUR.g, mk); b = mixf(b, SEWAGE_MUCK_COLOUR.b, mk);
    float conc = 0, cr = 0, cg = 0, cb = 0;
    for (int p = 0; p < MAX_PLUMES; p++) {
      if (!plumes[p].on) continue;
      float w = 0.12f + 0.03f * plumes[p].age, q = (x - plumes[p].x) / w;
      if (q < -3 || q > 3) continue;
      float c = 1.8f * powf(0.12f / w, 0.7f) * expf(-plumes[p].age / 25.0f) * expf(-q * q) * (0.75f + 0.25f * wave);
      float k = smooth01(plumes[p].age / 12.0f);               // fresh ink turns murky as it spreads
      conc += c;
      cr += c * mixf(SEWAGE_INK_COLOUR.r, SEWAGE_MUCK_COLOUR.r, k);
      cg += c * mixf(SEWAGE_INK_COLOUR.g, SEWAGE_MUCK_COLOUR.g, k);
      cb += c * mixf(SEWAGE_INK_COLOUR.b, SEWAGE_MUCK_COLOUR.b, k);
    }
    if (conc > 0) {
      float a = fminf(1.0f, conc);
      r = mixf(r, cr / conc, a); g = mixf(g, cg / conc, a); b = mixf(b, cb / conc, a);
    }
    out[i] = CRGB(cap255(r), cap255(g), cap255(b));
  }
}

// 3. FLOODING & SEWAGE: each part has its own start and end time within the animation.
//    Where they overlap, the flooding fades out while the sewage builds up.
float partWindow(float t, float a, float b, float fadeIn, float fadeOut) {
  if (t < a || t > b) return 0;
  fadeIn = fminf(fadeIn, (b - a) * 0.5f); fadeOut = fminf(fadeOut, (b - a) * 0.5f);
  return (fadeIn > 0 ? smooth01((t - a) / fadeIn) : 1.0f) * (fadeOut > 0 ? 1.0f - smooth01((t - (b - fadeOut)) / fadeOut) : 1.0f);
}
CRGB fsFlood[NUM_LEDS], fsSewage[NUM_LEDS];
void fxFloodSewage(float t, float dt, CRGB* out) {
  static float lastT = -1;
  if (lastT >= 0 && t < lastT - 0.5f) floodMonth = (floodMonth + 1) % 12;   // each time round: the next month
  lastT = t;
  float len = FS_SECONDS;
  float fS = fminf(FLOOD_START, len - 1.0f), fE = fminf(fmaxf(FLOOD_END, fS + 1.0f), len);
  float sS = fminf(SEWAGE_START, len - 1.0f), sE = fminf(fmaxf(SEWAGE_END, sS + 1.0f), len);
  float ov = fmaxf(0.0f, fminf(fE, sE) - fmaxf(fS, sS));          // how long the two overlap
  float kf = partWindow(t, fS, fE, fS > 0 ? 3.0f : 0.0f, fE >= len ? 0.0f : 3.0f);   // full strength until just before it ends
  float ks = partWindow(t, sS, sE, sS > 0 ? fmaxf(3.0f, ov * 0.6f) : 0.0f, sE >= len ? 0.0f : 3.0f);
  if (kf > 0) fxFlooding(t - fS, dt, fsFlood, fE - fS);    // floods spread over the whole flooding time
  if (ks > 0) fxSewage(t - sS, dt, fsSewage);
  for (int i = 0; i < NUM_LEDS; i++) {
    float r, g, b;
    idealWater((float)i / NUM_LEDS, r, g, b);
    float R = r, G = g, B = b;
    if (kf > 0) { R += (fsFlood[i].r - r) * kf; G += (fsFlood[i].g - g) * kf; B += (fsFlood[i].b - b) * kf; }
    if (ks > 0) { R += (fsSewage[i].r - r) * ks; G += (fsSewage[i].g - g) * ks; B += (fsSewage[i].b - b) * ks; }
    out[i] = CRGB(cap255(R), cap255(G), cap255(B));
  }
}

// 4. REWILDING: green appears just after House Mill, clean waves arrive from both ends, back to RIVER
void fxRewilding(float t, float dt, CRGB* out) {
  const float L = STRIP_LENGTH_M, mill = millU() * L;
  float prog = smooth01((t - 3.0f) / 42.0f);               // the clean waves take about 42 s to reach the mill
  float fl = mill * prog, fr = L - (L - mill) * prog;       // where the two clean fronts are (m)
  float allClean = smooth01((t - 45.0f) / 8.0f);
  float glowOn = prog > 0 ? 1.0f - smooth01((t - 45.0f) / 5.0f) : 0.0f;
  float back = 1.0f - smooth01((t - 40.0f) / 15.0f);       // plants fade as the river returns to normal
  int n = REWILD_PLANTS < 60 ? REWILD_PLANTS : 60;
  float pos[60], rad[60], bright[60];
  int count = 0;
  for (int k = 0; k < n; k++) {
    float age = t - (4.0f + hash01(k + 61) * 20.0f);        // planted between 4 s and 24 s
    if (age < 0) continue;
    float grow = smooth01(age / 10.0f);
    pos[count] = (REWILD_FROM_CM + hash01(k + 81) * (REWILD_TO_CM - REWILD_FROM_CM)) / 100.0f;   // in the rewilding area
    rad[count] = 0.03f + grow * REWILD_SIZE_CM / 100.0f;
    bright[count] = (0.35f + 0.65f * grow) * (0.85f + 0.15f * sinf(t * 1.3f + k));
    count++;
  }
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, x = (i + 0.5f) / NUM_LEDS * L, r, g, b;
    idealWater(u, r, g, b);
    float clean = x < mill ? smooth01((fl - x) / 0.6f + 0.5f) : smooth01((x - fr) / 0.6f + 0.5f);
    clean = fmaxf(clean, allClean);
    float dirt = REWILD_START_POLLUTION * (1.0f - clean) * (0.8f + 0.2f * sinf(TWO_PI * (x / 0.9f - riverClock * 13.0f / 600.0f)));
    r = mixf(r, SEWAGE_MUCK_COLOUR.r, dirt); g = mixf(g, SEWAGE_MUCK_COLOUR.g, dirt); b = mixf(b, SEWAGE_MUCK_COLOUR.b, dirt);
    float qf = x < mill ? (x - fl) / 0.3f : (x - fr) / 0.3f;  // the arriving clean waves glow a little
    float glow = 0.5f * glowOn * expf(-qf * qf);
    r = mixf(r, RIVER_CREST_COLOUR.r, glow); g = mixf(g, RIVER_CREST_COLOUR.g, glow); b = mixf(b, RIVER_CREST_COLOUR.b, glow);
    float m = 0;
    for (int k = 0; k < count; k++) {
      float q = (x - pos[k]) / rad[k];
      if (q > -3 && q < 3) m = fmaxf(m, bright[k] * expf(-q * q));
    }
    m *= back;
    out[i] = CRGB(cap255(mixf(r, REWILD_COLOUR.r, m)), cap255(mixf(g, REWILD_COLOUR.g, m)), cap255(mixf(b, REWILD_COLOUR.b, m)));
  }
}

typedef void (*EffectFn)(float t, float dt, CRGB* out);
EffectFn EFFECTS[NUM_ANIMS] = { fxRiver, fxRiver2, fxFloodSewage, fxRewilding };

// ---------------------------------------------------------------------
//  TRANSITIONS
// ---------------------------------------------------------------------
#define WIPE_EDGE  0.08f     // softness of the moving edge in left/right transitions

float wipeMix(float p, float u) {
  float front = p * (1.0f + 2.0f * WIPE_EDGE) - WIPE_EDGE;
  return smooth01((front - u) / WIPE_EDGE + 1.0f);
}

// Mix one LED of the old (a) and new (b) animation at progress p (0-1)
CRGB transitionPixel(uint8_t type, float p, int i, CRGB a, CRGB b) {
  if (type == T_OVERLAY) {                        // new one on top, old one fades underneath
    a.nscale8((uint8_t)((1.0f - smooth01((p - 0.4f) / 0.6f)) * 255.0f));
    b.nscale8((uint8_t)(smooth01(p / 0.6f) * 255.0f));
    return a | b;                                 // brighter of the two, colour by colour
  }
  float u = (float)i / NUM_LEDS;
  float m = (type == T_WIPE_LR) ? wipeMix(p, u) : (type == T_WIPE_RL) ? wipeMix(p, 1.0f - u) : smooth01(p);
  return blend(a, b, (uint8_t)(m * 255.0f));
}

// Run one animation with its SPEED and LIGHT settings
void renderAnim(int id, float localT, float dt, CRGB* out) {
  EFFECTS[id](localT * SPEED[id], dt * SPEED[id], out);
  uint8_t scale = (uint8_t)(LIGHT[id] * 255.0f + 0.5f);
  for (int i = 0; i < NUM_LEDS; i++) out[i].nscale8(scale);
}

// ---------------------------------------------------------------------
//  SETUP & LOOP
// ---------------------------------------------------------------------
float seqTime = 0;
unsigned long lastMs = 0;

// ---------------------------------------------------------------------
//  THE 9 LIGHT SENSORS AND THE GREEN PULSES
// ---------------------------------------------------------------------
#define NUM_LDRS    9
const int LDR_PINS[NUM_LDRS] = { 1, 2, 3, 4, 5, 10, 7, 8, 9 };
const float COVERED_BELOW   = 0.50;   // covered below 50% of that sensor's brightest reading (its max)
const float UNCOVERED_ABOVE = 0.60;   // uncovered again above 60% of its max

const float PULSE_SPEED     = 40.0;   // LEDs per second
const float PULSE_WIDTH     = 4.0;    // LEDs each side of the pulse's centre
const float PULSE_FADE      = 60.0;   // LEDs travelled before it has faded away completely
const int   PULSE_DIRECTION = 1;      // 1 = towards the last LED, -1 = towards LED 0
const float PULSE_EVERY     = 0.5;    // while covered, a new pulse every 0.5 seconds
const float DARKEN_SECONDS  = 5.0;    // covered this long -> the darkest green
const CRGB  DARK_GREEN      = CRGB(0, 60, 8);   // the green after DARKEN_SECONDS (starts at REWILD_COLOUR)

int ldrReading[NUM_LDRS], ldrMax[NUM_LDRS];
bool ldrCovered[NUM_LDRS];
float coveredFor[NUM_LDRS], nextPulse[NUM_LDRS];

#define MAX_PULSES 30
struct Pulse { float start, travelled; CRGB colour; bool on; };
Pulse pulses[MAX_PULSES];

int readLdr(int k) {
  analogRead(LDR_PINS[k]);                 // throw-away reading: clears the last pin's value
  delayMicroseconds(200);
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(LDR_PINS[k]);
  return sum / 8;
}

void startPulse(int k, CRGB colour) {
  for (int p = 0; p < MAX_PULSES; p++) {
    if (!pulses[p].on) {
      pulses[p].start = (k + 0.5f) * NUM_LEDS / NUM_LDRS;   // the middle of this sensor's part of the strip
      pulses[p].travelled = 0;
      pulses[p].colour = colour;
      pulses[p].on = true;
      return;
    }
  }
}

// Check the sensors, send and move the pulses, and draw them over the animation in leds[]
void updateSensorsAndPulses(float dt) {
  for (int k = 0; k < NUM_LDRS; k++) {
    ldrReading[k] = readLdr(k);
    if (ldrReading[k] > ldrMax[k]) ldrMax[k] = ldrReading[k];             // brighter than ever: new max
    if (!ldrCovered[k] && ldrReading[k] < ldrMax[k] * COVERED_BELOW) {
      ldrCovered[k] = true; coveredFor[k] = 0; nextPulse[k] = 0;           // just covered: pulse straight away
    }
    if (ldrCovered[k] && ldrReading[k] > ldrMax[k] * UNCOVERED_ABOVE) ldrCovered[k] = false;
    if (ldrCovered[k]) {
      coveredFor[k] += dt;
      if (coveredFor[k] >= nextPulse[k]) {                                  // time for the next pulse
        float dark = clamp01(coveredFor[k] / DARKEN_SECONDS);               // 0 = light green, 1 = darkest
        startPulse(k, CRGB(mixf(REWILD_COLOUR.r, DARK_GREEN.r, dark),
                           mixf(REWILD_COLOUR.g, DARK_GREEN.g, dark),
                           mixf(REWILD_COLOUR.b, DARK_GREEN.b, dark)));
        nextPulse[k] += PULSE_EVERY;
      }
    }
  }
  for (int p = 0; p < MAX_PULSES; p++) {                                    // move them along
    if (!pulses[p].on) continue;
    pulses[p].travelled += PULSE_SPEED * dt;
    float pos = pulses[p].start + PULSE_DIRECTION * pulses[p].travelled;
    if (pulses[p].travelled > PULSE_FADE || pos < -PULSE_WIDTH || pos > NUM_LEDS + PULSE_WIDTH) pulses[p].on = false;
  }
  for (int i = 0; i < NUM_LEDS; i++) {                                      // draw them over the river
    float best = 0; CRGB c;
    for (int p = 0; p < MAX_PULSES; p++) {
      if (!pulses[p].on) continue;
      float pos = pulses[p].start + PULSE_DIRECTION * pulses[p].travelled;
      float d = fabsf(i - pos) / PULSE_WIDTH;                               // 0 = centre, 1 = edge
      if (d >= 1) continue;
      float k = (1.0f - d * d) * (1.0f - pulses[p].travelled / PULSE_FADE); // fades as it travels
      if (k > best) { best = k; c = pulses[p].colour; }
    }
    if (best > 0) leds[i] = CRGB(mixf(leds[i].r, c.r, best), mixf(leds[i].g, c.g, best), mixf(leds[i].b, c.b, best));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);                                    // time for the Serial Monitor to connect
  Serial.println("\n=== Manifold animation starting ===");
  Serial.printf("Strip: pin %d, %d LEDs\n", DATA_PIN, NUM_LEDS);

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  clearAllStrips();

  analogReadResolution(12);                       // sensor readings from 0 to 4095
  analogSetAttenuation(ADC_11db);                 // full 0 - 3.3V range
  delay(500);
  for (int k = 0; k < NUM_LDRS; k++) {            // keep all sensors uncovered while it starts
    ldrMax[k] = readLdr(k);
    ldrCovered[k] = false;
  }
  Serial.println("Strips started. Running the animation...");

#if USE_WATCHDOG && defined(ESP32)
  // If this part will not compile on your board package, delete it (down to #endif)
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdtConfig = { 8000, 0, true };
    esp_task_wdt_reconfigure(&wdtConfig);
  #else
    esp_task_wdt_init(8, true);
  #endif
  esp_task_wdt_add(NULL);
#endif

  lastMs = millis();
}

void loop() {
  // Time since the last frame (works across the millis() rollover)
  unsigned long now = millis();
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  if (dt > 0.5f) dt = 0.5f;                       // only cap very long pauses, so slow frames don't slow the animation

  advanceRiver(dt);                               // the shared river keeps flowing

  // Position in the looping sequence (kept small, so it is accurate all year)
  float CYCLE = 0;
  for (int k = 0; k < NUM_ANIMS; k++) CYCLE += ANIM_SECONDS[PLAY_ORDER[k]];
  seqTime += dt;
  while (seqTime >= CYCLE) seqTime -= CYCLE;
  int pos = 0;
  float start = 0;
  while (pos < NUM_ANIMS - 1 && seqTime >= start + ANIM_SECONDS[PLAY_ORDER[pos]]) { start += ANIM_SECONDS[PLAY_ORDER[pos]]; pos++; }
  float local = seqTime - start;

  int cur = PLAY_ORDER[pos];
  float dur = ANIM_SECONDS[cur];
  int nxt = PLAY_ORDER[(pos + 1) % NUM_ANIMS];
  float inLen = TRANS_SECONDS[(pos + NUM_ANIMS - 1) % NUM_ANIMS];   // the transition that brought this one in
  float len = TRANS_SECONDS[pos];                                   // the transition out of this one
  renderAnim(cur, local + inLen, dt, bufA);

  if (local >= dur - len) {                                        // transition into the next animation
    float nt = local - (dur - len);
    renderAnim(nxt, nt, dt, bufB);
    float p = nt / len;
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = transitionPixel(TRANS_TYPE[pos], p, i, bufA[i], bufB[i]);
  } else {
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = bufA[i];
  }

  updateSensorsAndPulses(dt);                     // green pulses from covered sensors, over the river

#if USE_WATCHDOG && defined(ESP32)
  esp_task_wdt_reset();                           // tell the watchdog we are still running
#endif

  // Send the frame to the strip
  for (int i = 0; i < NUM_LEDS; i++) setVirtualPixel(i, strip.Color(leds[i].r, leds[i].g, leds[i].b));
  showAllStrips();

  // Status report once a second (Tools > Serial Monitor, 115200)
  static unsigned long lastReport = 0, frames = 0;
  frames++;
  if (now - lastReport >= 1000) {
    const char* NAMES[NUM_ANIMS] = { "RIVER", "RIVER 2", "FLOODING & SEWAGE", "REWILDING" };
    Serial.printf("%-18s %5.1f s | %lu frames/s | sensors (reading/max, * = covered): ",
                  NAMES[cur], local, frames);
    for (int k = 0; k < NUM_LDRS; k++)
      Serial.printf("G%d:%d/%d%s ", LDR_PINS[k], ldrReading[k], ldrMax[k], ldrCovered[k] ? "*" : "");
    Serial.println();
    frames = 0;
    lastReport = now;
  }
  delay(5);
}
