// Native tests for config.cpp's JSON handling and validation:  pio test -e native
// (Runs against the ESP32 pin tables.)
#include <unity.h>
#include <string.h>
#include <string>
#include "app.h"

void setUp() {}
void tearDown() {}

static Config fresh() {
  Config c;
  configDefaults(c);
  return c;
}

static const char* apply(Config& c, const char* json) {
  JsonDocument d;
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, json), json);
  return configApply(c, d.as<JsonObjectConst>());
}

#define ASSERT_REFUSED(c, json, words)                                        \
  do {                                                                        \
    const char* e = apply(c, json);                                           \
    TEST_ASSERT_NOT_NULL_MESSAGE(e, json);                                    \
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(e, words), e);                        \
  } while (0)

static void test_defaults_are_valid() {
  Config c = fresh();
  TEST_ASSERT_NULL(apply(c, "{}"));
}

static void test_wrong_types_are_refused_and_nothing_changes() {
  Config c = fresh();
  ASSERT_REFUSED(c, R"({"name": 5})", "name must be text");
  ASSERT_REFUSED(c, R"({"read_s": "60"})", "read_s must be a whole number");
  ASSERT_REFUSED(c, R"({"sleep": 1})", "sleep must be true or false");
  ASSERT_REFUSED(c, R"({"sensors": {"en": true}})", "sensors must be a list");
  ASSERT_REFUSED(c, R"({"name": "ok", "wifi_ssid": null, "mqtt_port": 1.5})", "mqtt_port");
  TEST_ASSERT_EQUAL_STRING("", c.name);   // the refused requests changed nothing
}

static void test_text_too_long() {
  Config c = fresh();
  ASSERT_REFUSED(c, R"({"wifi_ssid": "123456789012345678901234567890123"})", "32 characters at most");
}

static void test_numbers_are_clamped() {
  Config c = fresh();
  TEST_ASSERT_NULL(apply(c, R"({"read_s": 1, "sleep_min": 99999, "relays": [{"max_on": 0}]})"));
  TEST_ASSERT_EQUAL(10, c.read_s);
  TEST_ASSERT_EQUAL(1440, c.sleep_min);
  TEST_ASSERT_EQUAL(1, c.r[0].max_on_s);
}

static void test_absent_keys_keep_their_values() {
  Config c = fresh();
  TEST_ASSERT_NULL(apply(c, R"({"wifi_ssid": "Home", "wifi_pass": "secret1234"})"));
  TEST_ASSERT_NULL(apply(c, R"({"name": "Kitchen", "sensors": [{"name": "Basil"}]})"));
  TEST_ASSERT_EQUAL_STRING("secret1234", c.wifi_pass);   // not sent: kept
  TEST_ASSERT_EQUAL_STRING("Basil", c.s[0].name);
  TEST_ASSERT_EQUAL(32, c.s[0].pin);                     // rest of the slot untouched
  TEST_ASSERT_NULL(apply(c, R"({"wifi_pass": ""})"));    // sent empty: cleared (open network)
  TEST_ASSERT_EQUAL_STRING("", c.wifi_pass);
}

static void test_sensor_needs_an_adc_pin() {
  Config c = fresh();
  ASSERT_REFUSED(c, R"({"sensors": [{"pin": 16}]})", "Sensor 1 can't use pin 16");
  TEST_ASSERT_NULL(apply(c, R"({"sensors": [{"en": false, "pin": 16}]})"));   // off: not checked
}

static void test_relay_needs_a_safe_output_pin() {
  Config c = fresh();
  ASSERT_REFUSED(c, R"({"relays": [{"en": true, "pin": 6}]})", "Relay 1 can't use pin 6");    // flash pin
  ASSERT_REFUSED(c, R"({"relays": [{"en": true, "pin": 34}]})", "Relay 1 can't use pin 34");  // input only
  TEST_ASSERT_NULL(apply(c, R"({"relays": [{"en": true, "pin": 16}]})"));
}

static void test_a_pin_can_only_be_used_once() {
  Config c = fresh();   // sensor 1 is on 32 by default
  ASSERT_REFUSED(c, R"({"relays": [{"en": true, "pin": 32}]})", "Sensor 1 and Relay 1 both use pin 32");
  ASSERT_REFUSED(c, R"({"bat_pin": 32})", "both use pin 32");
}

static void test_relay_must_follow_a_sensor_that_is_on() {
  Config c = fresh();
  ASSERT_REFUSED(c, R"({"relays": [{"en": true, "sensor": 1}]})", "follows sensor 2, which is turned off");
  TEST_ASSERT_NULL(apply(c, R"({"relays": [{"en": true, "sensor": 0}]})"));
}

static void test_battery_mode_rules() {
  Config c = fresh();
  ASSERT_REFUSED(c, R"({"sleep": true})", "Battery mode needs MQTT");
  ASSERT_REFUSED(c, R"({"sleep": true, "mqtt_host": "ha.local", "relays": [{"en": true}]})", "Relays can't be used in battery mode");
  TEST_ASSERT_NULL(apply(c, R"({"sleep": true, "mqtt_host": "ha.local"})"));
}

static void test_temperature_sensor_type() {
  Config c = fresh();
  ASSERT_REFUSED(c, R"({"sht": 5})", "SHT3x or SHT4x");
  TEST_ASSERT_NULL(apply(c, R"({"sht": 4})"));
}

static void test_passwords_are_write_only() {
  Config c = fresh();
  TEST_ASSERT_NULL(apply(c, R"({"wifi_pass": "secret1234", "admin_pass": "hunter2"})"));
  JsonDocument d;
  configToJson(c, d.to<JsonObject>(), false);
  TEST_ASSERT_TRUE(d["wifi_pass"].isNull());
  TEST_ASSERT_TRUE(d["admin_pass"].isNull());
  TEST_ASSERT_TRUE(d["wifi_pass_set"].as<bool>());
  TEST_ASSERT_TRUE(d["admin_pass_set"].as<bool>());
  TEST_ASSERT_FALSE(d["mqtt_pass_set"].as<bool>());
}

static void test_saved_json_reloads_identically() {
  Config c = fresh();
  TEST_ASSERT_NULL(apply(c, R"({"name": "Greenhouse", "wifi_pass": "pw", "bat_ratio": 2.15,
    "sensors": [{"dry": 2550, "wet": 1180}, {"en": true, "name": "Fern"}],
    "relays": [{"en": true, "sensor": 1, "below": 25, "soak": 90}]})"));
  JsonDocument saved;
  configToJson(c, saved.to<JsonObject>(), true);
  Config back = fresh();
  TEST_ASSERT_NULL(configApply(back, saved.as<JsonObjectConst>()));

  JsonDocument again;
  configToJson(back, again.to<JsonObject>(), true);
  std::string a, b;
  serializeJson(saved, a);
  serializeJson(again, b);
  TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
  TEST_ASSERT_EQUAL_FLOAT(2.15f, back.bat_ratio);
  TEST_ASSERT_EQUAL(90, back.r[0].soak_min);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_valid);
  RUN_TEST(test_wrong_types_are_refused_and_nothing_changes);
  RUN_TEST(test_text_too_long);
  RUN_TEST(test_numbers_are_clamped);
  RUN_TEST(test_absent_keys_keep_their_values);
  RUN_TEST(test_sensor_needs_an_adc_pin);
  RUN_TEST(test_relay_needs_a_safe_output_pin);
  RUN_TEST(test_a_pin_can_only_be_used_once);
  RUN_TEST(test_relay_must_follow_a_sensor_that_is_on);
  RUN_TEST(test_battery_mode_rules);
  RUN_TEST(test_temperature_sensor_type);
  RUN_TEST(test_passwords_are_write_only);
  RUN_TEST(test_saved_json_reloads_identically);
  return UNITY_END();
}
