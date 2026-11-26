// =====================================================================
//  MANIFOLD - EXHIBITION VERSION 21 (500 cm strip + 9 light sensors)
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
//                           Separate LEDs in the rewilding area turn dark green
//                           and pulsate.
//
//  Covering a sensor ("adding care"): each of its sections is "turned on" -
//  it fades to dark green - and scattered blocks of turquoise and light green ripple
//  out from the middle of it, both ways, fading away before they reach the
//  next section. Uncovered, the section fades back to the river. The longer it is held, the faster they go. Where
//  they pass they clean the pollution: the longer it is held, the cleaner.
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
//   Flow direction ............. TO_MILL_WAVE_SPEED, TIDAL_WAVE_SPEED, TIDE_CHANGES_PER_MIN
//   Every animation's waves .... ..._NUM_WAVES, ..._WAVE_RANDOMNESS, ..._WAVE_TRAVEL_CM
//   First waves (RIVER) ........ RIVER_WAVE_SPEED, RIVER_TROUGH_COLOUR,
//                                RIVER_CREST_COLOUR, RIVER_SPARKLES
//   Real waves (RIVER 2) ....... R2_WAVE_SPEED, R2_TROUGH_COLOUR, R2_CREST_COLOUR,
//                                R2_DEEP_COLOUR, R2_DEEP_AMOUNT
//   Floods / sewage ............ FLOOD_COLOUR, FLOOD_STRENGTH, SEWAGE_..._COLOUR, SEWAGE_POINT_GLOW
//   Rewilding .................. REWILD_PLANTS, REWILD_PULSE_SPEED, REWILD_WAVE_POINTS
//   Interaction colours ........ PULSE_COLOUR, PULSE_COLOUR_2, PULSE_BLEND, PULSE_SPECKLE
//   Sensors .................... COVERED_BELOW, UNCOVERED_ABOVE
//   Care pulses ................ PULSE_COLOUR, PULSE_INTENSITY, PULSE_SPEED,
//                                PULSE_SPEED_UP, PULSE_SPEED_UP_SECONDS, PULSE_WIDTH,
//                                PULSE_REACH, PULSE_EVERY, CLEAN_FULL_SECONDS ...
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
//  TWEAK: one line per place: { sensor number, from cm, to cm, wider }
//  cm are measured from LED 0 (the Thames end); the strip is 500 cm long.
//   - a POINT: give the same cm twice, e.g. { 1, 60, 60 }
//       the pulses travel POINT_REACH_CM each side of it (20 cm)
//   - a SECTION: give where it starts and ends, e.g. { 1, 310, 360 }
//       the pulses start in its middle and travel out to its edges
//   - wider: how many extra LEDs it stretches out EACH SIDE (0 = none, 2 = 2 LEDs
//       each side). Places are allowed to overlap.
//   - a sensor can have several lines; several sensors can share a place
// ---------------------------------------------------------------------
struct Section { int sensor; float fromCm, toCm; int wider; };
const Section SECTIONS[] = {
  // sensor, from cm, to cm, wider (extra LEDs each side)
  { 1,  60,  60, 2 },     // points
  { 2, 107, 107, 2 },
  { 3, 110, 110, 2 },
  { 4, 155, 155, 2 },
  { 5, 180, 180, 2 },
  { 6, 195, 240, 2 },     // sensor 6: the rewilding area (a section)
  { 7, 250, 250, 2 },
  { 8, 255, 255, 2 },
  { 9, 265, 265, 2 },
  { 1, 310, 360, 2 },     // sensors 1, 2 and 3 also control 310-360 cm
  { 2, 310, 360, 2 },
  { 3, 310, 360, 2 },
  { 4, 390, 450, 2 },     // sensors 4, 5 and 6 also control 390-450 cm
  { 5, 390, 450, 2 },
  { 6, 390, 450, 2 },
  { 7, 455, 500, 2 },     // sensors 7, 8 and 9 also control 455-500 cm
  { 8, 455, 500, 2 },
  { 9, 455, 500, 2 },
};
const int NUM_SECTIONS = sizeof(SECTIONS) / sizeof(SECTIONS[0]);
const float POINT_REACH_CM = 20.0f;   // TWEAK: how far the pulses travel each side of a POINT (cm)

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
const float HOUSE_MILL_CM        = 110.0f;   // TWEAK: House Mill: where the waves change direction (floods start here; left of it is the tidal stretch)
const float TO_MILL_WAVE_SPEED   = 1.0f;     // TWEAK: speed of the waves flowing up to House Mill from the far end (1 = as now, 2 = twice as fast, 0.5 = half)
const float TIDAL_WAVE_SPEED     = 1.0f;     // TWEAK: speed of the waves on the tidal stretch, left of House Mill (1 = as now)
const float TIDE_CHANGES_PER_MIN = 2.0f;     // TWEAK: tidal stretch: how often its waves change direction (times per minute)

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
const float RIVER_NUM_WAVES       = 6.7f;              // TWEAK: how many waves along the whole strip at once (6.7 = as before)
const float RIVER_WAVE_RANDOMNESS = 0.30f;             // TWEAK: 0 = every wave the same, 1 = each wave its own height and life
const float RIVER_WAVE_TRAVEL_CM  = 300.0f;            // TWEAK: how far each wave travels before it fades away (cm, 0 = never fades)

// ---------------------------------------------------------------------
//  2. RIVER 2 - the "real" waves
// ---------------------------------------------------------------------
const float R2_WAVE_SPEED         = 0.6f;              // TWEAK: wave speed (1.0 = as on the website, lower = slower)
const CRGB  R2_TROUGH_COLOUR      = CRGB(6, 12, 16);    // TWEAK: the darker part between the waves: very dark, murky grey-blue. Was (10, 20, 34)
const CRGB  R2_CREST_COLOUR       = CRGB(40, 140, 115); // TWEAK: colour at the top of each wave: murky teal, still bright for definition. Was (0, 150, 170)
const CRGB  R2_DEEP_COLOUR        = CRGB(16, 30, 16);   // TWEAK: the deepest parts: dark, murky green-grey. Was (12, 40, 28)
const float R2_DEEP_AMOUNT        = 0.70f;             // TWEAK: how much green in the deepest parts (0 = none, 1 = all)
const float R2_DEEP_LEVEL         = 0.20f;             //        how much of each trough counts as "deepest" (0-1). Was 0.30
const float R2_CONTRAST           = 1.8f;              // TWEAK: how defined the waves are (1 = soft, 2 = crisp, sharp-edged waves)
const float R2_NUM_WAVES          = 2.5f;              // TWEAK: how many of the longest waves along the strip at once (2.5 = as before; the smaller waves follow)
const float R2_IRREGULARITY       = 0.30f;             // TWEAK: how bent / uneven the wave shapes are (0-1)
const float R2_WAVE_RANDOMNESS    = 0.50f;             // TWEAK: 0 = every wave the same, 1 = each wave its own height and life
const float R2_WAVE_TRAVEL_CM     = 150.0f;            // TWEAK: how far each wave travels before it fades away (cm, 0 = never fades)
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
const CRGB  FLOOD_COLOUR         = CRGB(210, 12, 4);    // TWEAK: flood surges from House Mill: strong, deep red. Was (150, 30, 10)
const float FLOOD_STRENGTH       = 1.0f;     // TWEAK: how strongly the red covers the river (1 = fully red, 0.5 = half). Was 0.8
const CRGB  SEWAGE_INK_COLOUR    = CRGB(190, 78, 10);  // TWEAK: fresh sewage coming out of the points: burnt amber. Was (200, 90, 12)
const CRGB  SEWAGE_WAVE_COLOUR   = CRGB(85, 22, 5);    // TWEAK: what the waves fade to further away: redder brown. Was (100, 60, 15)
const CRGB  SEWAGE_MUCK_COLOUR   = CRGB(38, 14, 3);     // TWEAK: the dark, reddish, murky brown between the waves
const float SEWAGE_POINT_GLOW    = 0.8f;                // TWEAK: how brightly the sewage points themselves glow orange-yellow (0 = not at all)
const float SEWAGE_NUM_WAVES     = 4.0f;                // TWEAK: how many waves around each sewage point at once
const float SEWAGE_WAVE_RANDOMNESS = 0.4f;              // TWEAK: 0 = even rings, 1 = each wave its own brightness and spacing
const float SEWAGE_WAVE_TRAVEL_CM  = 150.0f;            // TWEAK: how far the waves go from a point before they fade away (cm)
const float SEWAGE_WAVE_SPEED_CM = 15.0f;               // TWEAK: how fast they spread out from the points (cm per second)
const float SEWAGE_YELLOW_REACH_CM = 50.0f;             // TWEAK: how far from a point the waves are still bright orange-yellow (redder brown beyond). Was 120
const float SEWAGE_MUCKINESS     = 1.0f;                // TWEAK: how quickly the whole river gets polluted. Was 0.60
const float SEWAGE_MAX_MUCK       = 0.95f;               // TWEAK: how polluted the whole river can get (1 = none of the blue left). Was 0.75
#define SEWAGE_POINTS 7
const float SEWAGE_POS_CM[SEWAGE_POINTS] = { 105, 160, 275, 300, 325, 375, 460 };   // Emma's sewage inputs
const float SEWAGE_EVERY[SEWAGE_POINTS]  = { 6, 12, 0, 18, 9, 0, 25 };             // seconds between discharges (0 = once)

// ---------------------------------------------------------------------
//  4. REWILDING
// ---------------------------------------------------------------------
const float REWILD_START_POLLUTION = 1.0f;      // TWEAK: how much of the sewage is still there when rewilding starts (1 = all of it)
// small white waves appear from several points, then turn blue, before the river is clean
const int   REWILD_WAVE_POINTS     = 4;         // TWEAK: how many points the small white waves come from
const float REWILD_NUM_WAVES       = 3.0f;      // TWEAK: how many small white waves around each point at once
const float REWILD_WAVE_RANDOMNESS = 0.5f;      // TWEAK: 0 = evenly spaced points, all starting together; 1 = scattered points, own timing, speed and brightness
const float REWILD_WAVE_TRAVEL_CM  = 40.0f;     // TWEAK: how far the small waves go from their point before they fade away (cm)
const float REWILD_RIPPLE_SPREAD   = 20.0f;     // TWEAK: speed of the small waves (cm per second)
const float REWILD_RIPPLE_STRENGTH = 0.1f;     // TWEAK: how bright the small white waves are
const float REWILD_WHITE_AT        = 5.0f;      // TWEAK: seconds into REWILDING when the white waves appear
const float REWILD_BLUE_AT         = 16.0f;     // TWEAK: seconds into REWILDING when they start turning blue
const CRGB  REWILD_WHITE_COLOUR    = CRGB(255, 255, 255);  // TWEAK: the small waves' colour at first
// the rewilding area: random LEDs in it turn dark green
const float REWILD_FROM_CM         = 195.0f;    // TWEAK: rewilding area starts (cm from the Thames end)
const float REWILD_TO_CM           = 240.0f;    // TWEAK: rewilding area ends
const int   REWILD_PLANTS          = 8;         // TWEAK: how many separate LEDs in the area turn green (spread out; up to 5 in 195-240 cm before they touch)
const CRGB  REWILD_COLOUR          = CRGB(0, 70, 15);      // TWEAK: the dark green
const float REWILD_PULSE_SPEED     = 0.8f;      // TWEAK: how fast the green LEDs pulsate (pulses per second, 0 = steady)
const float REWILD_PULSE_DEPTH     = 0.5f;      // TWEAK: how deeply they pulsate (0 = steady, 1 = fade right out each time)
const float REWILD_SPARKLE_SPEED   = 1.0f;      // TWEAK: how fast the white sparkles in REWILDING flash and fade (1 = same as RIVER, 2 = twice as fast)

// ---------------------------------------------------------------------
//  SENSORS - when each one counts as covered (the same as the red pulse test)
//  Each sensor remembers its brightest reading (its max).
// ---------------------------------------------------------------------
const float COVERED_BELOW   = 0.50f;  // TWEAK: covered when the light drops below 50% of its max
const float UNCOVERED_ABOVE = 0.60f;  // TWEAK: uncovered again above 60% of its max (stops flickering)

// ---------------------------------------------------------------------
//  CARE PULSES - what covering a sensor does
//  While covered: pulses ripple out from the middle of each of its sections,
//  both ways, fading away before the edge of the section. Held longer = faster.
// ---------------------------------------------------------------------
const CRGB  SECTION_ON_COLOUR      = CRGB(50, 90, 80);     // TWEAK: the section's colour while covered (dark turquoise-grey)
const float SECTION_FADE_IN        = 0.8f;     // TWEAK: seconds for the section to turn on
const float SECTION_FADE_OUT       = 3.0f;     // TWEAK: seconds for the section to fade back to the river when uncovered
const CRGB  PULSE_COLOUR           = CRGB(135, 225, 160);  // TWEAK: block colour 1: turquoise, slightly greener. Was (161, 219, 189)
const CRGB  PULSE_COLOUR_2         = CRGB(240, 245, 60);   // TWEAK: block colour 2: light green, slightly yellower. Was (237, 255, 98)
const float PULSE_COLOUR_MIX       = 0.5f;     // TWEAK: 0 = all colour 1, 1 = all colour 2, 0.5 = alternating, in between = mostly one
const float PULSE_BLEND            = 0.6f;     // TWEAK: how much the two colours fade into each other (0 = hard alternating, 1 = smooth shades)
const CRGB  PULSE_SPECKLE_COLOUR   = CRGB(255, 255, 210);  // TWEAK: the third colour: little speckles (pale cream)
const float PULSE_SPECKLE          = 0.0f;     // TWEAK: random speckles in the pulses (0 = none now: the travelling specks below do this)
// the white sparkles that clean the pollution: the same white sparkles as on RIVER's crests (RIVER_SPARKLE_COLOUR).
// They travel out from the centre of each place, fading as they go, bringing RIVER's blue back behind them
const float SPECK_EVERY            = 0.12f;    // TWEAK: while covered, a new speck leaves the centre every 0.12 s (lower = more specks)
const float SPECK_SPEED            = 5.0f;     // TWEAK: how fast the specks travel out (LEDs per second)
const float SPECK_RANDOMNESS       = 0.6f;     // TWEAK: 0 = all the same speed, 1 = each its own speed
const float SPECK_BRIGHTNESS       = 0.9f;     // TWEAK: how bright they are as they leave the centre (they fade as they travel)
const float CLEAN_PER_SPECK        = 0.08f;    // TWEAK: how much blue of RIVER each passing speck brings back (lower = slower cleaning)
const float PULSE_INTENSITY        = 0.8f;     // TWEAK: how strong the pulses are (0.5 = soft, 1 = full)
const float PULSE_SPEED            = 7.0f;     // TWEAK: how fast they ripple out (LEDs per second)
const float PULSE_SPEED_UP         = 0.5f;     // TWEAK: how much faster when held a long time (0.5 = 50% faster, 0 = never)
const float PULSE_SPEED_UP_SECONDS = 5.0f;     // TWEAK: hold this long to reach the fastest speed
const float PULSE_WIDTH            = 1.0f;     // TWEAK: how thick each ripple is (LEDs)
const int   PULSE_BLOCK            = 0.5;        // TWEAK: size of each block of light (LEDs)
const float PULSE_SCATTER          = 0.8f;     // TWEAK: how many blocks light up (1 = all, lower = more scattered gaps)
const float PULSE_FLICKER          = 8.0f;     // TWEAK: how often the scattered blocks change (times per second, 0 = never)
const float PULSE_REACH            = 1.0f;     // TWEAK: sections: how far they go before fading out (1 = right to the section's edge)
const float PULSE_EVERY            = 0.6f;     // TWEAK: while covered, a new pulse every 0.6 s
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
#define WAVE1       (1.0f / RIVER_NUM_WAVES)   // RIVER wave lengths (fraction of the strip)
#define WAVE2       (WAVE1 * 0.37f)
#define MILL_BLEND  0.03f                  // how softly the two sides of the mill meet
#define KEEP_WAVES  4096.0f                // (the waves are counted for this many before the count starts again)
float tidePhase = PI * 0.1f, riverClock = 0, waveClock = 0;
float offL1 = 0, offL2 = 0, offR1 = 0, offR2 = 0;   // where the waves are, left and right of the mill

float millU() { return clamp01(HOUSE_MILL_CM / 100.0f / STRIP_LENGTH_M); }
float tideDir() { return tanhf(6.0f * sinf(tidePhase)); }               // +1 flowing right, -1 flowing left
float flowDirAt(float u) { return u < millU() ? tideDir() * TIDAL_WAVE_SPEED : -TO_MILL_WAVE_SPEED; }   // right of the mill: always towards it

// How strong one wave is right now (0-1): each wave lives for lifeSecs, then fades away
// and a new one takes its place. cycles = which wave (whole number) and where in it.
float oneWave(int32_t n, float lifeSecs, float randomness, uint32_t salt) {
  float h1 = hash01((uint32_t)n * 7u + salt), h2 = hash01((uint32_t)n * 13u + salt + 5u);
  float height = 1.0f + randomness * 0.8f * (h1 - 0.5f);             // each wave its own height (some taller, some smaller)
  if (lifeSecs <= 0) return height;                                  // 0 = waves never fade
  float life = lifeSecs * (1.0f + randomness * (h2 - 0.5f));         // each wave its own life
  float s = wrapTo(waveClock / life + h1 + h2, 1.0f);                // 0 -> 1 over its life
  return height * smooth01(s / 0.15f) * (1.0f - smooth01((s - 0.8f) / 0.2f));
}
float waveLife(float cycles, float lifeSecs, float randomness, uint32_t salt) {
  int32_t n = (int32_t)floorf(cycles);                               // which wave this is
  float f = cycles - n, e = oneWave(n, lifeSecs, randomness, salt);
  if (f < 0.15f) e = mixf(oneWave(n - 1, lifeSecs, randomness, salt), e, 0.5f + 0.5f * smooth01(f / 0.15f));          // blend softly
  if (f > 0.85f) e = mixf(e, oneWave(n + 1, lifeSecs, randomness, salt), 0.5f * smooth01((f - 0.85f) / 0.15f));      // into its neighbours
  return e;
}

// Move the waves on by dt seconds (called once per frame)
void advanceRiver(float dt) {
  tidePhase = wrapTo(tidePhase + PI * TIDE_CHANGES_PER_MIN / 60.0f * dt, TWO_PI);
  float v = FLOW * RIVER_WAVE_SPEED * dt, d = tideDir();                // RIVER_WAVE_SPEED makes the first waves faster
  float vL = v * TIDAL_WAVE_SPEED, vR = v * TO_MILL_WAVE_SPEED;         // each side of the mill its own speed
  riverClock = wrapTo(riverClock + dt, 600.0f);
  waveClock = wrapTo(waveClock + dt, 86400.0f);
  offL1 = wrapTo(offL1 + vL * d, WAVE1 * KEEP_WAVES);  offL2 = wrapTo(offL2 + vL * d * 1.3f, WAVE2 * KEEP_WAVES);
  offR1 = wrapTo(offR1 - vR, WAVE1 * KEEP_WAVES);      offR2 = wrapTo(offR2 - vR * 1.3f, WAVE2 * KEEP_WAVES);
}

float warpU(float u, float irr) {
  float T = riverClock;
  return u + irr * (0.04f * sinf(TWO_PI * (u * 1.7f + T * 19.0f / 600.0f)) + 0.025f * sinf(TWO_PI * (u * 3.9f - T * 28.0f / 600.0f) + 1.3f));
}
float swellAt(float u, float irr) { return 1.0f - irr * 0.45f * (0.5f + 0.5f * sinf(TWO_PI * (u * 2.3f + riverClock * 11.0f / 600.0f) + 0.7f)); }
float riverShape(float uw, float o1, float o2, float speed, uint32_t salt) {    // two wave sizes added together
  float travel = RIVER_WAVE_TRAVEL_CM / 100.0f / STRIP_LENGTH_M;                   // how far each wave goes (fraction of the strip)
  float life = (travel > 0 && speed > 0) ? travel / speed : 0;                     // ... so how long it lives
  float p1 = (uw - o1) / WAVE1, p2 = (uw - o2) / WAVE2;
  float e1 = waveLife(p1 + 0.25f, life, RIVER_WAVE_RANDOMNESS, salt);              // each wave fades in, travels, fades away
  float e2 = waveLife(p2 + 0.75f, life / 1.3f, RIVER_WAVE_RANDOMNESS, salt + 99u);
  return 0.7f * (sinf(TWO_PI * p1) * e1 + 1.0f) * 0.5f + 0.3f * (sinf(TWO_PI * p2 + PI) * e2 + 1.0f) * 0.5f;   // a fading wave flattens into calm water
}
// How high the ideal river is at u: 0 = trough (between waves), 1 = crest (top of a wave)
float riverWaveAt(float u) {
  float irr = RIVER_IRREGULARITY, uw = warpU(u, irr);
  float k = smooth01((u - millU()) / MILL_BLEND + 0.5f), s = 0;   // 0 = tidal side, 1 = the other side
  float v = FLOW * RIVER_WAVE_SPEED;
  if (k < 1) s += (1.0f - k) * riverShape(uw, offL1, offL2, v * TIDAL_WAVE_SPEED, 11u);
  if (k > 0) s += k * riverShape(uw, offR1, offR2, v * TO_MILL_WAVE_SPEED, 23u);
  return powf(clamp01(s * swellAt(u, irr)), 2.2f);
}
// Colour of the clean river (no sparkles) at u
CRGB idealWater(float u, float* waveOut) {   // waveOut: where to put the wave height (or nullptr)
  float w = riverWaveAt(u);
  if (waveOut) *waveOut = w;
  return mixColour(RIVER_TROUGH_COLOUR, RIVER_CREST_COLOUR, w);
}

// The SAME waves as RIVER (same shape and speed) but in the colours of RIVER 2:
// the water under the floods and the pollution
CRGB dirtyWater(float u, float* waveOut) {   // waveOut: where to put the wave height (or nullptr)
  float w = riverWaveAt(u);
  if (waveOut) *waveOut = w;
  CRGB c = mixColour(R2_TROUGH_COLOUR, R2_CREST_COLOUR, w);                          // RIVER 2's trough to crest
  float deep = R2_DEEP_AMOUNT * smooth01((R2_DEEP_LEVEL - w) / R2_DEEP_LEVEL);
  return mixColour(c, R2_DEEP_COLOUR, deep);                                          // and its green in the deepest parts
}

// The ideal river WITH sparkles (used by RIVER, and by REWILDING as it turns back into RIVER)
void drawRiver(float t, float dt, CRGB* out, Sparkles& sp, float sparkleSpeed) {
  if (sp.lastT < 0 || t < sp.lastT) { for (int i = 0; i < NUM_LEDS; i++) { sp.glint[i] = 0; sp.target[i] = 0; } }
  sp.lastT = t;
  float frameScale = dt / 0.035f, fade = powf(0.78f, frameScale * sparkleSpeed);
  float t2 = 0.68f * 0.68f, mill = millU(), calm = fabsf(tideDir());
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, wave;
    CRGB c = idealWater(u, &wave);
    float crest = smooth01((wave - t2) / (1.0f - t2));            // 1 near the top of a wave
    float lively = u < mill ? 0.5f + 0.5f * calm : 1.0f;           // the tidal side is calmer while it turns
    float gl = sparkleStep(sp.glint[i], sp.target[i], crest, RIVER_SPARKLES, sparkleSpeed, frameScale, fade, lively);
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
  drawRiver(t, dt, out, riverSparkles, RIVER_SPARKLE_SPEED);
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
  float base = 1.0f / fmaxf(0.2f, R2_NUM_WAVES);              // length of the longest waves (fraction of the strip)
  float travel = R2_WAVE_TRAVEL_CM / 100.0f / L, life[4];
  float d = tideDir(), lens[4], sumA = 0;
  for (int k = 0; k < 4; k++) {                                // move each layer on
    lens[k] = base * SIZES[k];
    float c = 0.03f * sqrtf(SIZES[k]) * R2_WAVE_SPEED;         // R2_WAVE_SPEED makes these waves slower
    oL[k] = wrapTo(oL[k] + c * TIDAL_WAVE_SPEED * d * dt, lens[k] * KEEP_WAVES);
    oR[k] = wrapTo(oR[k] - c * TO_MILL_WAVE_SPEED * dt, lens[k] * KEEP_WAVES);
    life[k] = travel > 0 ? travel / c : 0;                     // how long each wave lives before fading
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
      float ph = PHASES[k] / TWO_PI + 0.25f;                   // (so each wave is counted from trough to trough)
      if (kk < 1) { float p = (uw - oL[k]) / lens[k]; hL += AMPS[k] * waveLife(p + ph, life[k] / fmaxf(0.05f, TIDAL_WAVE_SPEED), R2_WAVE_RANDOMNESS, 31u + k * 1000u) * sinf(TWO_PI * p + PHASES[k]); }
      if (kk > 0) { float p = (uw - oR[k]) / lens[k]; hR += AMPS[k] * waveLife(p + ph, life[k] / fmaxf(0.05f, TO_MILL_WAVE_SPEED), R2_WAVE_RANDOMNESS, 47u + k * 1000u) * sinf(TWO_PI * p + PHASES[k]); }
    }
    float h = (hL * (1.0f - kk) + hR * kk) / sumA;
    float env = 0.7f + 0.3f * sinf(TWO_PI * (u * 0.9f - riverClock * 7.0f / 600.0f) + 0.4f);   // slow sets of bigger waves
    float wave = powf(clamp01((h * env + 1.0f) / 2.0f * swellAt(u, irr)), 1.8f);                 // 0 = deepest, 1 = top
    wave = clamp01((wave - 0.45f) * R2_CONTRAST + 0.45f);                                          // more defined waves
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
float floodAmt[NUM_LEDS];       // how much flood is on each LED (so it can go on top of the pollution)
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
    CRGB c = dirtyWater(u, nullptr);                            // RIVER's waves in RIVER 2's colours
    float m = n > 0 ? 0.1f : 0.0f;                             // a faint red tint at the mill in a flooding month
    for (int k = kMin; k <= kMax; k++) {
      float age = t - (k + 0.5f) * every;
      if (age < 0 || age > life) continue;
      float rr = fminf(reach, age * v);                        // how far this surge has spread
      float q = (d - rr) / 0.08f;
      float inside = d <= rr ? 1.0f : expf(-q * q);
      float front = expf(-q * q) * (1.0f - smooth01(age / rise)) * 0.5f;
      float fade = age < rise ? 1.0f : 1.0f - smooth01((age - rise) / drain);
      m = fmaxf(m, fminf(1.0f, FLOOD_STRENGTH * inside + front) * fade);
    }
    m *= smooth01((reach + 0.075f - d) / 0.15f);               // nothing beyond the reach
    floodAmt[i] = m;
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
      muck = fminf(SEWAGE_MAX_MUCK, muck + 0.07f * SEWAGE_MUCKINESS);   // the whole river gets murkier
    }
  }
  // the sewage drifts with the river, spreads and fades; the muck slowly settles
  muck *= expf(-dt / 90.0f);
  for (int p = 0; p < MAX_PLUMES; p++) {
    if (!plumes[p].on) continue;
    plumes[p].age += dt;
    plumes[p].x += FLOW * L * flowDirAt(plumes[p].x / L) * dt;      // drifts with the river (at TO_MILL / TIDAL speed)
    if (plumes[p].age >= 40.0f || plumes[p].x <= -1.0f || plumes[p].x >= L + 1.0f) plumes[p].on = false;
  }
  for (int i = 0; i < NUM_LEDS; i++) {
    float u = (float)i / NUM_LEDS, x = (i + 0.5f) / NUM_LEDS * L, wave;
    CRGB c = dirtyWater(u, &wave);                              // RIVER's waves in RIVER 2's colours
    // waves spreading out from the nearest sewage point: bright orange-yellow near it,
    // redder and browner further away, until the whole river is waves of these colours
    float nearest = 1e9f; int nk = 0;
    for (int k = 0; k < SEWAGE_POINTS; k++) {
      float dk = fabsf(x * 100.0f - SEWAGE_POS_CM[k]);                                // cm
      if (dk < nearest) { nearest = dk; nk = k; }
    }
    float travelCm = fmaxf(1.0f, SEWAGE_WAVE_TRAVEL_CM);
    float spacing = travelCm / fmaxf(0.5f, SEWAGE_NUM_WAVES);                            // distance between the waves
    float dW = nearest + SEWAGE_WAVE_RANDOMNESS * spacing * 0.3f * sinf(nearest / spacing * 1.7f + nk * 2.1f + t * 0.4f);   // uneven spacing
    float ph = (dW - t * SEWAGE_WAVE_SPEED_CM) / spacing;
    float ringH = 1.0f - SEWAGE_WAVE_RANDOMNESS * 0.8f * hash01((uint32_t)(int32_t)floorf(ph + 0.25f) * 17u + nk * 7919u);   // each wave its own brightness
    float ring = (0.5f + 0.5f * sinf(TWO_PI * ph)) * ringH;
    ring *= smooth01((t * SEWAGE_WAVE_SPEED_CM - nearest) / 20.0f);                      // only as far as they have spread
    ring *= 1.0f - smooth01((nearest - travelCm * 0.7f) / (travelCm * 0.3f));            // fade away by SEWAGE_WAVE_TRAVEL_CM
    CRGB waveCol = mixColour(SEWAGE_INK_COLOUR, SEWAGE_WAVE_COLOUR, smooth01(nearest / SEWAGE_YELLOW_REACH_CM));   // orange-yellow -> redder brown
    c = mixColour(c, mixColour(SEWAGE_MUCK_COLOUR, waveCol, ring), muck);
    float conc = 0, cr = 0, cg = 0, cb = 0;
    for (int p = 0; p < MAX_PLUMES; p++) {
      if (!plumes[p].on) continue;
      float w = 0.12f + 0.03f * plumes[p].age, q = (x - plumes[p].x) / w;
      if (q < -3 || q > 3) continue;
      float a = 1.8f * powf(0.12f / w, 0.7f) * expf(-plumes[p].age / 25.0f) * expf(-q * q) * (0.75f + 0.25f * wave);
      float k = smooth01(plumes[p].age / 12.0f);               // fresh sewage turns murky as it spreads
      conc += a;
      cr += a * mixf(SEWAGE_INK_COLOUR.r, SEWAGE_WAVE_COLOUR.r, k);   // bright orange-yellow, turning redder brown
      cg += a * mixf(SEWAGE_INK_COLOUR.g, SEWAGE_WAVE_COLOUR.g, k);
      cb += a * mixf(SEWAGE_INK_COLOUR.b, SEWAGE_WAVE_COLOUR.b, k);
    }
    if (conc > 0) c = mixColour(c, CRGB(cap255(cr / conc), cap255(cg / conc), cap255(cb / conc)), fminf(0.85f, conc));   // (0.85: the waves still show through)
    float qp = nearest / 6.0f;
    c = mixColour(c, SEWAGE_INK_COLOUR, SEWAGE_POINT_GLOW * expf(-qp * qp) * smooth01(t / 3.0f) * (sewageTapsOn ? 1.0f : 0.5f));   // the points glow bright orange-yellow
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
    CRGB w = dirtyWater((float)i / NUM_LEDS, nullptr);          // RIVER's waves in RIVER 2's colours
    float R = w.r, G = w.g, B = w.b;
    if (ks > 0) { R += (fsSewage[i].r - w.r) * ks; G += (fsSewage[i].g - w.g) * ks; B += (fsSewage[i].b - w.b) * ks; }   // the pollution first
    CRGB c = CRGB(cap255(R), cap255(G), cap255(B));
    if (kf > 0) c = mixColour(c, FLOOD_COLOUR, floodAmt[i] * kf);    // then the red flood pulses ON TOP of it
    out[i] = c;
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
  drawRiver(t, dt, cleanRiver, rewildSparkles, REWILD_SPARKLE_SPEED);   // the clean water = the RIVER animation itself
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
    // spread evenly across the area with a gap between them, so they are separate pixels
    float slot = (float)(last - first + 1) / (REWILD_PLANTS > 0 ? REWILD_PLANTS : 1);
    int led = first + (int)(slot * (k + 0.25f + 0.5f * hash01(k + 81)));
    float beat = 1.0f - REWILD_PULSE_DEPTH * (0.5f + 0.5f * sinf(TWO_PI * REWILD_PULSE_SPEED * t + k * 1.9f));   // pulsating, each its own timing
    if (led >= 0 && led < NUM_LEDS) plant[led] = fmaxf(plant[led], smooth01(age / 6.0f) * back * beat);
  }
  // the small white waves: from several points, spreading out, then turning blue
  float rippleOn = smooth01((t - REWILD_WHITE_AT) / 6.0f) * (1.0f - allClean);
  float blue = smooth01((t - REWILD_BLUE_AT) / 10.0f);
  CRGB rippleColour = mixColour(REWILD_WHITE_COLOUR, RIVER_CREST_COLOUR, blue);
  int np = REWILD_WAVE_POINTS < 1 ? 1 : REWILD_WAVE_POINTS;
  float travelCm = fmaxf(1.0f, REWILD_WAVE_TRAVEL_CM), rr = REWILD_WAVE_RANDOMNESS;
  float spacingCm = travelCm / fmaxf(0.5f, REWILD_NUM_WAVES);                // distance between the small waves

  for (int i = 0; i < NUM_LEDS; i++) {
    float x = (i + 0.5f) / NUM_LEDS * L;
    CRGB c = cleanRiver[i];
    float clean = x < mill ? smooth01((fl - x) / 0.6f + 0.5f) : smooth01((x - fr) / 0.6f + 0.5f);
    clean = fmaxf(clean, allClean);
    c = mixColour(c, fsSewage[i], REWILD_START_POLLUTION * (1.0f - clean));   // still sewage where the clean waves haven't reached
    // small waves from the nearest point
    float w = 0;
    for (int j = 0; j < np; j++) {
      float posCm = ((j + 0.5f) + rr * 0.9f * (hash01(j + 201) - 0.5f)) / np * L * 100.0f;   // where this point is (scattered by the randomness)
      float tj = t - REWILD_WHITE_AT - rr * 8.0f * hash01(j + 301);                         // each point starts at its own time
      if (tj <= 0) continue;
      float sp = REWILD_RIPPLE_SPREAD * (1.0f + rr * 0.6f * (hash01(j + 401) - 0.5f));      // and its own speed
      float d = fabsf(x * 100.0f - posCm);                                  // cm from this point
      float reach = smooth01((tj * sp - d) / 20.0f);                        // only as far as they have spread
      reach *= 1.0f - smooth01((d - travelCm * 0.6f) / (travelCm * 0.4f));  // fade away by REWILD_WAVE_TRAVEL_CM
      float ph = (d - tj * sp) / spacingCm;
      float hgt = 1.0f - rr * 0.7f * hash01((uint32_t)(int32_t)floorf(ph + 0.5f) * 29u + j * 977u);   // each wave its own brightness
      float crest = powf(0.5f + 0.5f * cosf(TWO_PI * ph), 6.0f);            // thin crests
      w = fmaxf(w, reach * crest * hgt);
    }
    c = mixColour(c, REWILD_COLOUR, plant[i]);                              // the plants (underneath the waves)
    c = mixColour(c, cleanRiver[i], 0.32f * blue * rippleOn * (1.0f - clean));   // the blue waves start to clear the water (was 0.4)
    c = mixColour(c, rippleColour, REWILD_RIPPLE_STRENGTH * rippleOn * w * (1.0f - clean));   // the small waves pass over the plants
    float qf = x < mill ? (x - fl) / 0.3f : (x - fr) / 0.3f;
    c = mixColour(c, RIVER_CREST_COLOUR, 0.5f * glowOn * expf(-qf * qf));   // the arriving clean waves glow
    float crestOver = smooth01((riverWaveAt((float)i / NUM_LEDS) - 0.5f) / 0.5f) * clean;   // river crests wash over the plants too
    out[i] = mixColour(c, cleanRiver[i], plant[i] * crestOver * 0.6f);
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
//  CARE PULSES: while a sensor is covered, white pulses ripple out from the
//  middle of each of its sections, both ways, like ripples on a river, and
//  fade away before they reach the edge of the section. The longer it is held,
//  the faster new pulses go. Where they pass, they clean the pollution.
// =====================================================================
#define MAX_RIPPLES 60
struct Ripple { int section; float radius, speed, cleanPower; uint32_t seed; bool on; };
Ripple ripples[MAX_RIPPLES];
float nextPulse[NUM_LDRS];     // seconds until this sensor's next pulse
float cleanMem[NUM_LEDS];      // how clean each LED has been made (0-1), fades over CLEAN_STAYS_SECONDS
float dirtiness = 0;           // 0 while the water is clean (RIVER, RIVER 2), 1 while dirty
float pulseAmount[NUM_LEDS];   // how much pulse is on each LED this frame
float pulseMix[NUM_LEDS];      // its colour: 0 = colour 1, 1 = colour 2
float pulseSpeck[NUM_LEDS];    // how much of the speckle colour
float flickerClock = 0;        // changes which blocks are lit
float sectionOn[NUM_LDRS];     // 0 = off, 1 = fully turned on (fades in and out)
#define MAX_SPECKS 120
struct Speck { int section; float dist, dir, speed, phase; bool on; };
Speck specks[MAX_SPECKS];
float nextSpeck[NUM_LDRS];     // seconds until this sensor's next speck
float speckAmount[NUM_LEDS];   // how much speck is on each LED this frame
CRGB  river1Now[NUM_LEDS];     // RIVER (with its sparkles) as it is now: what the cleaned water becomes
Sparkles cleanSparkles;

// Where place n is on the strip (in LEDs): its middle, and how far its pulses travel each way
void placeOf(int n, float &mid, float &reach) {
  float from = fminf(SECTIONS[n].fromCm, SECTIONS[n].toCm), to = fmaxf(SECTIONS[n].fromCm, SECTIONS[n].toCm);
  float ledsPerCm = NUM_LEDS / (STRIP_LENGTH_M * 100.0f);
  mid = (from + to) * 0.5f * ledsPerCm;
  if (to - from < 1.0f) reach = POINT_REACH_CM * ledsPerCm;                 // a point: POINT_REACH_CM each side
  else                  reach = (to - from) * 0.5f * ledsPerCm * PULSE_REACH;   // a section: out to its edges
  reach += SECTIONS[n].wider;                                                // stretched by "wider" LEDs each side
  if (reach < 1.0f) reach = 1.0f;
}

void addRipple(int n, int k) {
  for (int r = 0; r < MAX_RIPPLES; r++) {
    if (ripples[r].on) continue;
    ripples[r].section = n;
    ripples[r].radius = 0;
    ripples[r].speed = PULSE_SPEED * (1.0f + PULSE_SPEED_UP * clamp01(heldFor[k] / PULSE_SPEED_UP_SECONDS));   // held longer = faster
    ripples[r].cleanPower = clamp01(heldFor[k] / CLEAN_FULL_SECONDS);                                         // held longer = cleans more
    ripples[r].seed = random(100000);                                // its own pattern of blocks
    ripples[r].on = true;
    return;
  }
}

// A new white speck leaves the centre of place n, going left or right
void addSpeck(int n, int k) {
  for (int s = 0; s < MAX_SPECKS; s++) {
    if (specks[s].on) continue;
    specks[s].section = n;
    specks[s].dist = 0;
    specks[s].dir = random(2) ? 1.0f : -1.0f;
    specks[s].speed = SPECK_SPEED * (1.0f + SPECK_RANDOMNESS * (rand01() - 0.5f))
                    * (1.0f + PULSE_SPEED_UP * clamp01(heldFor[k] / PULSE_SPEED_UP_SECONDS));   // held longer = faster
    specks[s].phase = rand01();                                    // each twinkles in its own time
    specks[s].on = true;
    return;
  }
}

// The white specks: they travel out from the centre, fading as they go,
// and where they pass the blue of RIVER slowly comes back
void updateSpecks(float dt) {
  static float clock = 0;
  clock += dt;
  drawRiver(clock, dt, river1Now, cleanSparkles, RIVER_SPARKLE_SPEED);    // RIVER now, for the cleaned water
  for (int i = 0; i < NUM_LEDS; i++) speckAmount[i] = 0;
  for (int k = 0; k < NUM_LDRS; k++) {                                   // while covered: new specks
    if (!covered[k]) { nextSpeck[k] = 0; continue; }
    nextSpeck[k] -= dt;
    if (nextSpeck[k] <= 0) {
      for (int n = 0; n < NUM_SECTIONS; n++) if (SECTIONS[n].sensor == k + 1) addSpeck(n, k);
      nextSpeck[k] = fmaxf(0.03f, SPECK_EVERY);
    }
  }
  for (int s = 0; s < MAX_SPECKS; s++) {
    Speck& p = specks[s];
    if (!p.on) continue;
    float mid, reach;
    placeOf(p.section, mid, reach);                                      // the place's centre, and how far to go
    p.dist += p.speed * dt;
    if (p.dist >= reach) { p.on = false; continue; }
    float fade = 1.0f - smooth01(p.dist / reach);                        // fades as it travels
    float twinkle = 0.55f + 0.45f * sinf(TWO_PI * (p.dist / 1.5f + p.phase));   // twinkles like a sparkle
    float pos = mid + p.dir * p.dist - 0.5f;
    int i0 = (int)floorf(pos);
    float f = pos - i0;
    for (int j = 0; j < 2; j++) {                                        // shared smoothly between 2 LEDs as it moves
      int i = i0 + j;
      if (i < 0 || i >= NUM_LEDS) continue;
      float w = fade * (j == 0 ? 1.0f - f : f);
      speckAmount[i] = fmaxf(speckAmount[i], w * twinkle);
      cleanMem[i] = fminf(1.0f, cleanMem[i] + CLEAN_PER_SPECK * w * p.speed * dt * (1.0f - cleanMem[i]));   // the blue slowly comes back
    }
  }
}

void updatePulses(float dt, int currentAnim) {
  flickerClock += dt;
  uint32_t tick = (uint32_t)(flickerClock * PULSE_FLICKER);
  // 1. while covered: a new pulse from the middle of each of the sensor's sections every PULSE_EVERY
  for (int k = 0; k < NUM_LDRS; k++) {
    if (covered[k]) sectionOn[k] = SECTION_FADE_IN  > 0 ? fminf(1.0f, sectionOn[k] + dt / SECTION_FADE_IN)  : 1.0f;   // turn on
    else            sectionOn[k] = SECTION_FADE_OUT > 0 ? fmaxf(0.0f, sectionOn[k] - dt / SECTION_FADE_OUT) : 0.0f;   // turn off
    if (!covered[k]) { nextPulse[k] = 0; continue; }               // ready to pulse straight away when covered
    nextPulse[k] -= dt;
    if (nextPulse[k] <= 0) {
      for (int n = 0; n < NUM_SECTIONS; n++) if (SECTIONS[n].sensor == k + 1) addRipple(n, k);
      nextPulse[k] = fmaxf(0.05f, PULSE_EVERY);
    }
  }
  // 2. ripples spread out; they disappear once they have faded
  for (int i = 0; i < NUM_LEDS; i++) { pulseAmount[i] = 0; pulseMix[i] = 0; pulseSpeck[i] = 0; }
  float wantDirty = (currentAnim == 2 || currentAnim == 3) ? 1.0f : 0.0f;
  dirtiness += (wantDirty - dirtiness) * fminf(1.0f, dt / 2.0f);         // changes smoothly over about 2 s
  float keep = expf(-dt / CLEAN_STAYS_SECONDS);
  for (int i = 0; i < NUM_LEDS; i++) cleanMem[i] *= keep;                 // cleaned water slowly gets dirty again
  for (int r = 0; r < MAX_RIPPLES; r++) {
    if (!ripples[r].on) continue;
    Ripple& p = ripples[r];
    float mid, reach;
    placeOf(p.section, mid, reach);                                       // the place's middle, and how far to go
    p.radius += p.speed * dt;
    if (p.radius > reach + PULSE_WIDTH) { p.on = false; continue; }
    float fade = 1.0f - smooth01(p.radius / reach);                       // fades away before the edge
    int bs = PULSE_BLOCK < 1 ? 1 : PULSE_BLOCK;
    for (int i = (int)(mid - p.radius - 2 * PULSE_WIDTH); i <= (int)(mid + p.radius + 2 * PULSE_WIDTH); i++) {
      if (i < 0 || i >= NUM_LEDS) continue;
      int side = i < mid ? -1 : 1;
      int block = (int)(fabsf(i - mid) / bs);                         // which block of LEDs, counting out from the middle
      float blockDist = (block + 0.5f) * bs;                          // the middle of that block
      if (fabsf(blockDist - p.radius) > PULSE_WIDTH * 0.5f + bs * 0.5f) continue;   // block not on the ripple
      float r01 = hash01(p.seed + (uint32_t)(block * 2 + (side > 0)) * 131u + tick * 7919u);
      if (r01 > PULSE_SCATTER) continue;                              // a gap: this block is dark
      float k = PULSE_INTENSITY * fade * (0.6f + 0.4f * r01 / fmaxf(0.01f, PULSE_SCATTER));   // each block a slightly different brightness
      if (k > pulseAmount[i]) {
        pulseAmount[i] = k;
        // alternate the colours block by block (the pattern shifts as the blocks flicker)
        float alt = ((block + tick + p.seed) % 2) ? 1.0f : 0.0f;
        float m = PULSE_COLOUR_MIX;
        float hard = m <= 0.5f ? alt * (m * 2.0f) : alt + (1.0f - alt) * (m * 2.0f - 1.0f);   // 0.5 = alternating
        float soft = 0.5f + 0.5f * sinf(TWO_PI * (i * 0.23f + flickerClock * 0.35f) + (p.seed % 100) * 0.07f);   // shades drifting into each other
        pulseMix[i] = mixf(hard, mixf(soft, m, fabsf(m - 0.5f) * 2.0f), PULSE_BLEND);
        pulseSpeck[i] = hash01(i * 37u + tick * 7919u + p.seed) < PULSE_SPECKLE ? 0.85f : 0.0f;   // speckles of the third colour
      }
    }
  }
}

// Draw over the animation in leds[]: the turned-on sections, then the cleaned
// water (the blue of RIVER coming back), then the pulses, then the white specks
void drawPulses() {
  for (int n = 0; n < NUM_SECTIONS; n++) {                             // the turned-on sections
    int k = SECTIONS[n].sensor - 1;
    if (k < 0 || k >= NUM_LDRS || sectionOn[k] <= 0) continue;
    float mid, reach;
    placeOf(n, mid, reach);                                              // a section's own length, or a point +/- POINT_REACH_CM
    int a = (int)(mid - reach + 0.5f), b = (int)(mid + reach + 0.5f);
    for (int i = (a < 0 ? 0 : a); i < b && i < NUM_LEDS; i++) leds[i] = mixColour(leds[i], SECTION_ON_COLOUR, sectionOn[k]);
  }
  for (int i = 0; i < NUM_LEDS; i++) {                                  // cleaned water: the blue of RIVER
    float k = cleanMem[i] * dirtiness;
    if (k > 0) leds[i] = mixColour(leds[i], river1Now[i], k);         // it just becomes RIVER again
  }
  for (int i = 0; i < NUM_LEDS; i++)                                    // the pulses
    if (pulseAmount[i] > 0) leds[i] = mixColour(leds[i], mixColour(mixColour(PULSE_COLOUR, PULSE_COLOUR_2, pulseMix[i]), PULSE_SPECKLE_COLOUR, pulseSpeck[i]), fminf(1.0f, pulseAmount[i]));
  for (int i = 0; i < NUM_LEDS; i++) {                                  // the white sparkles on top, added like RIVER's
    float g = fminf(1.0f, speckAmount[i] * SPECK_BRIGHTNESS);
    if (g > 0) leds[i] = CRGB(cap255(leds[i].r + RIVER_SPARKLE_COLOUR.r * g), cap255(leds[i].g + RIVER_SPARKLE_COLOUR.g * g), cap255(leds[i].b + RIVER_SPARKLE_COLOUR.b * g));
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
  updateSpecks(dt);
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
