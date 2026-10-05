// logic.h - pure functions, no Arduino. Everything here runs in `pio test -e native`.
#pragma once
#include <stdint.h>
#include <algorithm>
#include "app.h"

// Moisture percent from millivolts. Works whether a sensor reads high or low when dry.
// Returns -1 when the two calibration points are too close to mean anything.
inline int moisturePct(int mv, int dry, int wet) {
  int span = dry - wet;
  if (span > -200 && span < 200) return -1;
  return std::clamp((dry - mv) * 100 / span, 0, 100);
}

// A reading far outside the calibrated range means a shorted, broken or unplugged sensor.
// Such readings never start watering. (A sensor pulled out of the pot still reads "dry";
// the daily run cap is what protects against that.)
inline bool sensorFault(int mv, int dry, int wet) {
  int lo = std::min(dry, wet), hi = std::max(dry, wet);
  int margin = std::max(150, (hi - lo) / 4);
  return mv < 50 || mv < lo - margin || mv > hi + margin;
}

// Single-cell LiPo charge from its resting voltage. ponytail: generic 10-point curve;
// read before WiFi starts, since the radio sags the cell by 50-100 mV.
inline int batteryPct(int mv) {
  static const int16_t curve[] = {3300, 3680, 3740, 3790, 3820, 3870, 3920, 3990, 4060, 4130, 4200};
  if (mv <= curve[0]) return 0;
  if (mv >= curve[10]) return 100;
  int i = 0;
  while (mv > curve[i + 1]) i++;
  return i * 10 + (mv - curve[i]) * 10 / (curve[i + 1] - curve[i]);
}

// Median of n samples (reorders the array). Throws away the ADC's spikes.
inline int median(int* v, int n) {
  std::nth_element(v, v + n / 2, v + n);
  return v[n / 2];
}

// ---- Watering rule for one relay -----------------------------------------------------------
// All times are millis() values compared by subtraction, so the 49-day wrap is harmless.
struct Zone {
  bool on = false;
  uint32_t since = 0;     // last switch on or off; soak counts from switch-off, and from boot
  uint32_t runMs = 0;     // length of the current run
  uint32_t dayStart = 0;  // start of the current 24 h window
  uint8_t runs = 0;       // automatic runs in this window
};

constexpr uint32_t DAY_MS = 86400000UL;

// One tick. pct < 0 means there's no trustworthy reading. Returns whether the relay should be on.
// ponytail: fixed 24 h windows counted from boot, so a window edge can allow up to 2x
// max_per_day within one day. Use a ring of start times if that ever matters.
inline bool zoneTick(Zone& z, const RelayCfg& c, uint32_t now, int pct) {
  if (now - z.dayStart >= DAY_MS) { z.dayStart = now; z.runs = 0; }
  if (z.on) {
    if (now - z.since >= std::min<uint32_t>(z.runMs, c.max_on_s * 1000UL)) { z.on = false; z.since = now; }
    return z.on;
  }
  if (c.sensor >= 0 && pct >= 0 && pct < c.below_pct && z.runs < c.max_per_day &&
      now - z.since >= c.soak_min * 60000UL) {
    z.on = true; z.since = now; z.runMs = c.run_s * 1000UL; z.runs++;
  }
  return z.on;
}

// Manual or Home Assistant run. Still cut at max_on_s by zoneTick; doesn't use up the daily cap.
inline void zoneRun(Zone& z, uint32_t now, uint32_t ms) { z.on = true; z.since = now; z.runMs = ms; }
inline void zoneStop(Zone& z, uint32_t now) { if (z.on) { z.on = false; z.since = now; } }
