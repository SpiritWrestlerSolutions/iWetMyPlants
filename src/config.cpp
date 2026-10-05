// config.cpp - defaults, JSON in and out with validation, and NVS storage.
// Everything above the #ifdef ARDUINO line also builds in the native tests.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "app.h"

void configDefaults(Config& c) {
  c = Config{};
  c.mqtt_port = 1883;
  c.ha_discovery = true;
  c.read_s = 60;
  c.pwr_pin = -1;
  c.warmup_ms = 200;
  c.sleep_min = 30;
  c.bat_pin = -1;
  c.bat_ratio = 2.0f;
  for (int i = 0; i < MAX_SENSORS; i++) {
    SensorCfg& s = c.s[i];
    s.en = i == 0;
    snprintf(s.name, sizeof s.name, "Plant %d", i + 1);
    s.pin = ADC_PINS[i];
    s.dry_mv = 2400;   // placeholder until calibrated
    s.wet_mv = 1100;
    s.warn_pct = 30;
  }
  for (int i = 0; i < MAX_RELAYS; i++) {
    RelayCfg& r = c.r[i];
    snprintf(r.name, sizeof r.name, "Zone %d", i + 1);
    r.pin = RELAY_DEFAULT_PINS[i];
    r.active_low = true;
    r.max_on_s = 300;
    r.manual_s = 60;
    r.sensor = -1;
    r.below_pct = 30;
    r.run_s = 30;
    r.soak_min = 60;
    r.max_per_day = 4;
  }
}

// ---- Reading JSON: absent keys keep their value, wrong types are refused --------------------
namespace {

char err[160];
char prefix[16];   // "Sensor 2: " while reading a slot
bool bad;

void fail(const char* fmt, ...) {
  if (bad) return;   // first problem wins
  bad = true;
  int n = snprintf(err, sizeof err, "%s", prefix);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err + n, sizeof err - n, fmt, ap);
  va_end(ap);
}

template <size_t N> void text(JsonObjectConst o, const char* key, char (&dst)[N]) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return;
  if (!v.is<const char*>()) return fail("%s must be text", key);
  const char* s = v.as<const char*>();
  if (strlen(s) >= N) return fail("%s is too long (%u characters at most)", key, unsigned(N - 1));
  strcpy(dst, s);
}

template <typename T> void whole(JsonObjectConst o, const char* key, T& dst, long lo, long hi) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return;
  if (!v.is<long>()) return fail("%s must be a whole number", key);
  dst = T(std::clamp(v.as<long>(), lo, hi));
}

void flag(JsonObjectConst o, const char* key, bool& dst) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return;
  if (!v.is<bool>()) return fail("%s must be true or false", key);
  dst = v.as<bool>();
}

void real(JsonObjectConst o, const char* key, float& dst, float lo, float hi) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return;
  if (!v.is<float>()) return fail("%s must be a number", key);
  dst = std::clamp(v.as<float>(), lo, hi);
}

// Runs fn(slot object, index) for each object in in[key], up to max.
template <typename F> void slots(JsonObjectConst in, const char* key, const char* label, int max, F fn) {
  JsonVariantConst v = in[key];
  if (v.isNull()) return;
  if (!v.is<JsonArrayConst>()) return fail("%s must be a list", key);
  int i = 0;
  for (JsonVariantConst item : v.as<JsonArrayConst>()) {
    if (i == max) break;   // extra slots from a bigger chip are ignored
    snprintf(prefix, sizeof prefix, "%s %d: ", label, i + 1);
    if (item.is<JsonObjectConst>()) fn(item.as<JsonObjectConst>(), i);
    else if (!item.isNull()) fail("must be an object");
    i++;
  }
  prefix[0] = 0;
}

void merge(Config& n, JsonObjectConst in) {
  bad = false;
  prefix[0] = 0;
  text(in, "name", n.name);
  text(in, "wifi_ssid", n.wifi_ssid);
  text(in, "wifi_pass", n.wifi_pass);
  text(in, "mqtt_host", n.mqtt_host);
  whole(in, "mqtt_port", n.mqtt_port, 1, 65535);
  text(in, "mqtt_user", n.mqtt_user);
  text(in, "mqtt_pass", n.mqtt_pass);
  flag(in, "ha_discovery", n.ha_discovery);
  text(in, "admin_pass", n.admin_pass);
  whole(in, "read_s", n.read_s, 10, 3600);
  whole(in, "sht", n.sht, 0, 255);
  whole(in, "pwr_pin", n.pwr_pin, -1, 48);
  whole(in, "warmup_ms", n.warmup_ms, 0, 5000);
  flag(in, "sleep", n.sleep);
  whole(in, "sleep_min", n.sleep_min, 1, 1440);
  whole(in, "bat_pin", n.bat_pin, -1, 48);
  real(in, "bat_ratio", n.bat_ratio, 1.0f, 10.0f);
  slots(in, "sensors", "Sensor", MAX_SENSORS, [&](JsonObjectConst o, int i) {
    SensorCfg& s = n.s[i];
    flag(o, "en", s.en);
    text(o, "name", s.name);
    whole(o, "pin", s.pin, -1, 48);
    whole(o, "dry", s.dry_mv, 0, 3300);
    whole(o, "wet", s.wet_mv, 0, 3300);
    whole(o, "warn", s.warn_pct, 0, 100);
  });
  slots(in, "relays", "Relay", MAX_RELAYS, [&](JsonObjectConst o, int i) {
    RelayCfg& r = n.r[i];
    flag(o, "en", r.en);
    text(o, "name", r.name);
    whole(o, "pin", r.pin, -1, 48);
    flag(o, "active_low", r.active_low);
    whole(o, "max_on", r.max_on_s, 1, 3600);
    whole(o, "manual", r.manual_s, 1, 3600);
    whole(o, "sensor", r.sensor, -1, MAX_SENSORS - 1);
    whole(o, "below", r.below_pct, 0, 100);
    whole(o, "run", r.run_s, 1, 3600);
    whole(o, "soak", r.soak_min, 1, 1440);
    whole(o, "per_day", r.max_per_day, 1, 48);
  });
}

template <size_t N> bool allowed(int8_t pin, const int8_t (&list)[N]) {
  return std::find(list, list + N, pin) != list + N;
}

template <size_t N> const char* pinList(const int8_t (&list)[N]) {
  static char buf[64];
  int n = 0;
  for (size_t i = 0; i < N; i++) n += snprintf(buf + n, sizeof buf - n, i ? ", %d" : "%d", list[i]);
  return buf;
}

// The rules that make a config safe to run. Returns nullptr when it is.
const char* validate(const Config& n) {
  bad = false;
  prefix[0] = 0;
  if (n.sht != 0 && n.sht != 3 && n.sht != 4) fail("Temperature sensor must be none, SHT3x or SHT4x");

  // Every pin in use, with who uses it, to catch doubles.
  struct Use { int8_t pin; char who[16]; } use[MAX_SENSORS + MAX_RELAYS + 4];
  int k = 0;
  auto take = [&](int8_t pin, const char* who) {
    for (int j = 0; j < k; j++)
      if (use[j].pin == pin) return fail("%s and %s both use pin %d", use[j].who, who, pin);
    use[k].pin = pin;
    snprintf(use[k].who, sizeof use[k].who, "%s", who);
    k++;
  };
  char who[16];

  for (int i = 0; i < MAX_SENSORS; i++) {
    if (!n.s[i].en) continue;
    snprintf(who, sizeof who, "Sensor %d", i + 1);
    if (!allowed(n.s[i].pin, ADC_PINS))
      fail("%s can't use pin %d. Moisture sensors need one of: %s", who, n.s[i].pin, pinList(ADC_PINS));
    take(n.s[i].pin, who);
  }
  bool anyRelay = false;
  for (int i = 0; i < MAX_RELAYS; i++) {
    const RelayCfg& r = n.r[i];
    if (!r.en) continue;
    anyRelay = true;
    snprintf(who, sizeof who, "Relay %d", i + 1);
    if (!allowed(r.pin, OUT_PINS)) fail("%s can't use pin %d. Relays need one of: %s", who, r.pin, pinList(OUT_PINS));
    if (r.sensor >= 0 && !n.s[r.sensor].en) fail("%s follows sensor %d, which is turned off", who, r.sensor + 1);
    take(r.pin, who);
  }
  if (n.pwr_pin >= 0) {
    if (!allowed(n.pwr_pin, OUT_PINS)) fail("Sensor power can't use pin %d. Use one of: %s", n.pwr_pin, pinList(OUT_PINS));
    take(n.pwr_pin, "Sensor power");
  }
  if (n.bat_pin >= 0) {
    if (!allowed(n.bat_pin, ADC_PINS)) fail("Battery can't use pin %d. Use one of: %s", n.bat_pin, pinList(ADC_PINS));
    take(n.bat_pin, "Battery");
  }
  if (n.sht) {
    take(SDA_PIN, "Temperature SDA");
    take(SCL_PIN, "Temperature SCL");
  }
  if (n.sleep && anyRelay) fail("Relays can't be used in battery mode, because the unit sleeps between readings");
  if (n.sleep && !n.mqtt_host[0]) fail("Battery mode needs MQTT set up, because that's the only way its readings get out");
  return bad ? err : nullptr;
}

}  // namespace

const char* configApply(Config& c, JsonObjectConst in) {
  Config n = c;
  merge(n, in);
  if (bad) return err;
  if (const char* e = validate(n)) return e;
  c = n;
  return nullptr;
}

void configToJson(const Config& c, JsonObject out, bool secrets) {
  out["name"] = c.name;
  out["wifi_ssid"] = c.wifi_ssid;
  out["mqtt_host"] = c.mqtt_host;
  out["mqtt_port"] = c.mqtt_port;
  out["mqtt_user"] = c.mqtt_user;
  out["ha_discovery"] = c.ha_discovery;
  if (secrets) {
    out["wifi_pass"] = c.wifi_pass;
    out["mqtt_pass"] = c.mqtt_pass;
    out["admin_pass"] = c.admin_pass;
  } else {   // write-only: the page only learns whether one is set
    out["wifi_pass_set"] = c.wifi_pass[0] != 0;
    out["mqtt_pass_set"] = c.mqtt_pass[0] != 0;
    out["admin_pass_set"] = c.admin_pass[0] != 0;
  }
  out["read_s"] = c.read_s;
  out["sht"] = c.sht;
  out["pwr_pin"] = c.pwr_pin;
  out["warmup_ms"] = c.warmup_ms;
  out["sleep"] = c.sleep;
  out["sleep_min"] = c.sleep_min;
  out["bat_pin"] = c.bat_pin;
  out["bat_ratio"] = c.bat_ratio;
  JsonArray sa = out["sensors"].to<JsonArray>();
  for (const SensorCfg& s : c.s) {
    JsonObject o = sa.add<JsonObject>();
    o["en"] = s.en;
    o["name"] = s.name;
    o["pin"] = s.pin;
    o["dry"] = s.dry_mv;
    o["wet"] = s.wet_mv;
    o["warn"] = s.warn_pct;
  }
  JsonArray ra = out["relays"].to<JsonArray>();
  for (const RelayCfg& r : c.r) {
    JsonObject o = ra.add<JsonObject>();
    o["en"] = r.en;
    o["name"] = r.name;
    o["pin"] = r.pin;
    o["active_low"] = r.active_low;
    o["max_on"] = r.max_on_s;
    o["manual"] = r.manual_s;
    o["sensor"] = r.sensor;
    o["below"] = r.below_pct;
    o["run"] = r.run_s;
    o["soak"] = r.soak_min;
    o["per_day"] = r.max_per_day;
  }
}

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#include <memory>

static const char* NVS_NS = "iwmp2";   // v1 used "iwmp"; its binary blob is simply ignored

// Keeps names and network settings but switches every piece of hardware off, so a saved
// config that no longer passes validation (say, after a firmware change) can't drive a pin.
static void configHardwareOff(Config& c) {
  for (SensorCfg& s : c.s) s.en = false;
  for (RelayCfg& r : c.r) r.en = false;
  c.pwr_pin = c.bat_pin = -1;
  c.sht = 0;
  c.sleep = false;
}

bool configLoad(Config& c) {
  configDefaults(c);
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;   // nothing saved yet
  size_t len = p.getBytesLength("cfg");
  std::unique_ptr<char[]> buf(new char[len + 1]);
  if (len) p.getBytes("cfg", buf.get(), len);
  p.end();
  if (!len) return false;

  JsonDocument doc;
  if (deserializeJson(doc, (const char*)buf.get(), len)) {
    Serial.println("Saved settings are unreadable; starting from defaults");
    return false;
  }
  merge(c, doc.as<JsonObjectConst>());
  if (bad || validate(c)) {
    Serial.printf("Saved settings need attention (%s); hardware is off until they're fixed\n", err);
    configHardwareOff(c);
  }
  return true;
}

bool configSave(const Config& c) {
  JsonDocument doc;
  configToJson(c, doc.to<JsonObject>(), true);
  String json;
  serializeJson(doc, json);
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  bool ok = p.putBytes("cfg", json.c_str(), json.length()) == json.length();
  p.end();
  return ok;
}

void configErase() {
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.clear();
    p.end();
  }
}
#endif
