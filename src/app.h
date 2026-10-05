// app.h - everything the modules share: limits, pin tables, the config struct, entry points.
// Kept free of Arduino includes so config.cpp and logic.h also build in the native tests.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

// ---- Chip limits and pins (checked against the core 2.0.17 variant files) -----------------
#ifdef IWMP_C3
constexpr int MAX_SENSORS = 4, MAX_RELAYS = 4;
inline constexpr int8_t ADC_PINS[] = {0, 1, 3, 4};          // ADC1; GPIO2 is a strapping pin
inline constexpr int8_t OUT_PINS[] = {5, 6, 7, 10, 20};     // 21 toggles at every boot and wake
inline constexpr int8_t RELAY_DEFAULT_PINS[] = {5, 20, 10, 7};
constexpr int8_t SDA_PIN = 6, SCL_PIN = 7, BOOT_PIN = 9;    // not 8/9: LED and BOOT button
#else
constexpr int MAX_SENSORS = 6, MAX_RELAYS = 8;
inline constexpr int8_t ADC_PINS[] = {32, 33, 34, 35, 36, 39};
inline constexpr int8_t OUT_PINS[] = {4, 13, 16, 17, 18, 19, 23, 25, 26, 27, 32, 33};
inline constexpr int8_t RELAY_DEFAULT_PINS[] = {16, 17, 18, 19, 23, 25, 26, 27};  // v1 wiring
constexpr int8_t SDA_PIN = 21, SCL_PIN = 22, BOOT_PIN = 0;
#endif

// ---- Config: the whole thing. Stored as JSON, so adding a field never wipes a device. ------
struct SensorCfg {
  bool en;
  char name[24];
  int8_t pin;
  uint16_t dry_mv, wet_mv;   // calibration points
  uint8_t warn_pct;          // card turns amber below this
};

struct RelayCfg {
  bool en;
  char name[24];
  int8_t pin;
  bool active_low;           // most relay boards switch on when the input is pulled LOW
  uint16_t max_on_s;         // hard cap on any single run, manual or automatic
  uint16_t manual_s;         // length of a manual or Home Assistant "ON"
  int8_t sensor;             // sensor slot this zone follows, -1 = manual only
  uint8_t below_pct;         // start an automatic run below this moisture
  uint16_t run_s;            // automatic run length
  uint16_t soak_min;         // wait after any run before the next automatic one
  uint8_t max_per_day;       // automatic runs allowed per 24 h
};

struct Config {
  char name[32];             // friendly name; blank shows the device id
  char wifi_ssid[33], wifi_pass[65];
  char mqtt_host[64];
  uint16_t mqtt_port;
  char mqtt_user[33], mqtt_pass[65];
  bool ha_discovery;
  char admin_pass[33];       // blank = no password
  uint16_t read_s;           // read + publish interval while awake
  SensorCfg s[MAX_SENSORS];
  RelayCfg r[MAX_RELAYS];
  uint8_t sht;               // 0 none, 3 = SHT3x, 4 = SHT4x
  int8_t pwr_pin;            // pin that powers the sensors, -1 = always powered
  uint16_t warmup_ms;        // sensor settle time after power-on
  bool sleep;                // battery mode: deep sleep between readings
  uint16_t sleep_min;
  int8_t bat_pin;            // battery divider input, -1 = none
  float bat_ratio;           // divider ratio; the calibration knob for the battery reading
};

extern Config cfg;               // the live config (main.cpp)

// ---- config.cpp ----------------------------------------------------------------------------
void configDefaults(Config& c);
// Applies the keys present in `in` on top of `c`. Returns nullptr on success, otherwise a
// message for the user, and `c` is left untouched.
const char* configApply(Config& c, JsonObjectConst in);
// Passwords come out blank unless `secrets` (only for saving to flash), with *_set flags.
void configToJson(const Config& c, JsonObject out, bool secrets);
bool configLoad(Config& c);     // false = nothing saved yet (defaults in place)
bool configSave(const Config& c);
void configErase();

// ---- wifi.cpp ------------------------------------------------------------------------------
const char* deviceId();         // "iwmp-a1b2c3": hostname, setup network suffix
void wifiBegin();
void wifiLoop();
bool wifiConnected();
bool wifiSetupMode();           // the setup network is open
void wifiScanJson(JsonObject out);

// ---- web.cpp -------------------------------------------------------------------------------
void webBegin();
void webLoop();
