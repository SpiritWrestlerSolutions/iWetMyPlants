// sensors.cpp - the moisture sensors, the SHT temperature/humidity sensor and the battery input.
#include <Arduino.h>
#include <Wire.h>
#include "app.h"
#include "logic.h"

Readings rd;

namespace {
#ifdef IWMP_C3
constexpr int CLIP_MV = 2450;   // the C3's ADC tops out around 2.5 V
#else
constexpr int CLIP_MV = 3100;
#endif
constexpr uint32_t LIVE_EVERY_MS = 1000, LIVE_FOR_MS = 10000;

bool due = true, powered;
uint32_t lastRead, poweredAt, liveUntil;
uint8_t shtAddr = 0x44;

// Median of 21 quick samples, in factory-calibrated millivolts.
int readMv(int pin) {
  int v[21];
  for (int& x : v) x = analogReadMilliVolts(pin);
  return median(v, 21);
}

void power(bool on) {   // SPWR: HIGH = sensors powered (docs/HARDWARE.md)
  if (cfg.pwr_pin < 0) return;
  if (on && !powered) poweredAt = millis();
  digitalWrite(cfg.pwr_pin, on ? HIGH : LOW);
  powered = on;
}

bool readSht(float& t, float& h) {
  bool sht4 = cfg.sht == 4;
  for (int attempt = 0; attempt < 2; attempt++) {
    Wire.beginTransmission(shtAddr);
    if (sht4) {
      Wire.write(0xFD);                   // measure, high precision
    } else {
      Wire.write(0x24);                   // single shot, high repeatability, no clock stretching
      Wire.write(0x00);
    }
    if (Wire.endTransmission() == 0) {
      delay(sht4 ? 10 : 16);
      uint8_t b[6];
      if (Wire.requestFrom((int)shtAddr, 6) == 6) {
        for (uint8_t& x : b) x = Wire.read();
        if (shtCrc(b, 2) == b[2] && shtCrc(b + 3, 2) == b[5]) {
          t = shtTempC(b[0] << 8 | b[1]);
          h = shtRh(b[3] << 8 | b[4], sht4);
          return true;
        }
      }
    }
    shtAddr = shtAddr == 0x44 ? 0x45 : 0x44;   // boards ship at either address
  }
  return false;
}

void readAll(bool log) {
  for (int i = 0; i < MAX_SENSORS; i++) {
    const SensorCfg& s = cfg.s[i];
    if (!s.en) {
      rd.mv[i] = rd.pct[i] = -1;
      rd.fault[i] = rd.clipped[i] = false;
      continue;
    }
    int mv = readMv(s.pin);
    rd.mv[i] = mv;
    rd.pct[i] = moisturePct(mv, s.dry_mv, s.wet_mv);
    rd.fault[i] = sensorFault(mv, s.dry_mv, s.wet_mv);
    rd.clipped[i] = mv >= CLIP_MV;
    if (log) Serial.printf("S%d %d mV %d%%%s  ", i + 1, mv, rd.pct[i], rd.fault[i] ? " (check sensor)" : "");
  }
  if (!cfg.sht || !readSht(rd.tempC, rd.rh)) rd.tempC = rd.rh = NAN;
  rd.batMv = cfg.bat_pin >= 0 ? int(readMv(cfg.bat_pin) * cfg.bat_ratio) : -1;
  rd.at = millis() | 1;
  if (log) {
    if (cfg.sht) Serial.printf(isnan(rd.tempC) ? "SHT: no answer  " : "%.1f C %.0f%%RH  ", rd.tempC, rd.rh);
    if (rd.batMv >= 0) Serial.printf("battery %d mV", rd.batMv);
    Serial.println();
  }
}
}  // namespace

void sensorsBegin() {
  if (cfg.pwr_pin >= 0) {
    digitalWrite(cfg.pwr_pin, LOW);   // off, set before the pin becomes an output
    pinMode(cfg.pwr_pin, OUTPUT);
  }
  if (cfg.sht) Wire.begin(SDA_PIN, SCL_PIN);
  for (int i = 0; i < MAX_SENSORS; i++) rd.mv[i] = rd.pct[i] = -1;
  rd.tempC = rd.rh = NAN;
  rd.batMv = -1;
}

// Reads every read_s seconds (every second while the calibration page is open). With a
// sensor power pin, switches power on, waits out the warm-up across loop passes, then reads.
void sensorsLoop() {
  uint32_t now = millis();
  bool live = int32_t(liveUntil - now) > 0;
  if (!due) due = now - lastRead >= (live ? LIVE_EVERY_MS : cfg.read_s * 1000UL);
  if (!due) return;
  power(true);
  if (cfg.pwr_pin >= 0 && now - poweredAt < cfg.warmup_ms) return;
  readAll(!live);
  due = false;
  lastRead = now;
  if (!live) power(false);
}

void sensorsLive() { liveUntil = millis() + LIVE_FOR_MS; }

const char* sensorsCalibrate(int slot, const char* point) {
  if (slot < 0 || slot >= MAX_SENSORS || !cfg.s[slot].en) return "That sensor isn't turned on. Save it first.";
  SensorCfg& s = cfg.s[slot];
  if (!strcmp(point, "reset")) {
    s.dry_mv = DEFAULT_DRY_MV;
    s.wet_mv = DEFAULT_WET_MV;
  } else {
    bool dry = !strcmp(point, "dry");
    if (!dry && strcmp(point, "wet")) return "point must be dry, wet or reset";
    if (cfg.pwr_pin >= 0 && !powered) {   // the page normally keeps them powered; just in case
      power(true);
      delay(cfg.warmup_ms);
    }
    int mv = readMv(s.pin);
    if (abs(mv - (dry ? s.wet_mv : s.dry_mv)) < 200)
      return dry ? "That's within 200 mV of the wet point. Is the sensor dry?"
                 : "That's within 200 mV of the dry point. Is the sensor in water?";
    (dry ? s.dry_mv : s.wet_mv) = mv;
  }
  if (!configSave(cfg)) return "Couldn't save the calibration";
  due = true;   // re-read with the new points
  return nullptr;
}

int sensorPct(int slot) { return cfg.s[slot].en && !rd.fault[slot] ? rd.pct[slot] : -1; }

void sensorsJson(JsonObject out) {
  JsonArray list = out["sensors"].to<JsonArray>();
  for (int i = 0; i < MAX_SENSORS; i++) {
    const SensorCfg& s = cfg.s[i];
    JsonObject o = list.add<JsonObject>();
    o["on"] = s.en;
    o["name"] = s.name;
    if (!s.en) continue;
    o["mv"] = rd.mv[i];
    o["pct"] = rd.pct[i];
    o["dry"] = s.dry_mv;
    o["wet"] = s.wet_mv;
    o["calibrated"] = s.dry_mv != DEFAULT_DRY_MV || s.wet_mv != DEFAULT_WET_MV;
    o["low"] = sensorPct(i) >= 0 && sensorPct(i) < s.warn_pct;
    o["fault"] = rd.fault[i];
    o["clipped"] = rd.clipped[i];
  }
  if (!isnan(rd.tempC)) {
    out["temp"] = serialized(String(rd.tempC, 1));
    out["rh"] = serialized(String(rd.rh, 0));
  }
  if (rd.batMv >= 0) {
    out["bat_mv"] = rd.batMv;
    out["bat_pct"] = batteryPct(rd.batMv);
  }
  out["read_age"] = rd.at ? int((millis() - rd.at) / 1000) : -1;
}
