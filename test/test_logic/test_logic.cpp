// Native tests for logic.h:  pio test -e native
#include <unity.h>
#include "logic.h"

void setUp() {}
void tearDown() {}

static void test_moisture_normal_sensor() {        // reads high when dry
  TEST_ASSERT_EQUAL(0, moisturePct(2400, 2400, 1100));
  TEST_ASSERT_EQUAL(100, moisturePct(1100, 2400, 1100));
  TEST_ASSERT_EQUAL(50, moisturePct(1750, 2400, 1100));
  TEST_ASSERT_EQUAL(0, moisturePct(3000, 2400, 1100));    // clamped
  TEST_ASSERT_EQUAL(100, moisturePct(500, 2400, 1100));   // clamped
}

static void test_moisture_inverted_sensor() {      // reads low when dry (v1 got this wrong)
  TEST_ASSERT_EQUAL(0, moisturePct(1100, 1100, 2400));
  TEST_ASSERT_EQUAL(100, moisturePct(2400, 1100, 2400));
  TEST_ASSERT_EQUAL(50, moisturePct(1750, 1100, 2400));
}

static void test_moisture_rejects_junk_calibration() {
  TEST_ASSERT_EQUAL(-1, moisturePct(1500, 2000, 1900));
  TEST_ASSERT_EQUAL(-1, moisturePct(1500, 2000, 2000));
}

static void test_sensor_fault() {                  // range 1100..2400, margin 325
  TEST_ASSERT_FALSE(sensorFault(1750, 2400, 1100));
  TEST_ASSERT_FALSE(sensorFault(2500, 2400, 1100));
  TEST_ASSERT_TRUE(sensorFault(700, 2400, 1100));
  TEST_ASSERT_TRUE(sensorFault(2800, 2400, 1100));
  TEST_ASSERT_TRUE(sensorFault(30, 2400, 1100));
}

static void test_battery_pct() {
  TEST_ASSERT_EQUAL(100, batteryPct(4200));
  TEST_ASSERT_EQUAL(100, batteryPct(4350));
  TEST_ASSERT_EQUAL(0, batteryPct(3300));
  TEST_ASSERT_EQUAL(0, batteryPct(2900));
  TEST_ASSERT_EQUAL(50, batteryPct(3870));
  TEST_ASSERT_EQUAL(75, batteryPct(4025));
}

static void test_median() {
  int v[] = {5, 1, 9000, 3, 7};
  TEST_ASSERT_EQUAL(5, median(v, 5));
}

// ---- watering rule ----
static RelayCfg zoneCfg() {
  RelayCfg c{};
  c.en = true; c.max_on_s = 300; c.manual_s = 60; c.sensor = 0;
  c.below_pct = 30; c.run_s = 20; c.soak_min = 30; c.max_per_day = 3;
  return c;
}
constexpr uint32_t MIN = 60000;

static void test_zone_waits_out_soak_after_boot() {
  Zone z; RelayCfg c = zoneCfg();
  TEST_ASSERT_FALSE(zoneTick(z, c, 1000, 10));             // dry, but just booted
  TEST_ASSERT_FALSE(zoneTick(z, c, 30 * MIN - 1, 10));
  TEST_ASSERT_TRUE(zoneTick(z, c, 30 * MIN, 10));
}

static void test_zone_runs_then_soaks() {
  Zone z; RelayCfg c = zoneCfg();
  uint32_t t = 30 * MIN;
  TEST_ASSERT_TRUE(zoneTick(z, c, t, 10));
  TEST_ASSERT_TRUE(zoneTick(z, c, t + 19999, 10));
  TEST_ASSERT_FALSE(zoneTick(z, c, t + 20000, 10));        // run_s reached
  TEST_ASSERT_FALSE(zoneTick(z, c, t + 20000 + 30 * MIN - 1, 10));
  TEST_ASSERT_TRUE(zoneTick(z, c, t + 20000 + 30 * MIN, 10));
}

static void test_zone_only_waters_when_dry() {
  Zone z; RelayCfg c = zoneCfg();
  TEST_ASSERT_FALSE(zoneTick(z, c, 60 * MIN, 30));         // not below 30%
  TEST_ASSERT_FALSE(zoneTick(z, c, 60 * MIN, -1));         // no trustworthy reading
  c.sensor = -1;
  TEST_ASSERT_FALSE(zoneTick(z, c, 60 * MIN, 10));         // manual-only zone
}

static void test_zone_daily_cap() {
  Zone z; RelayCfg c = zoneCfg();
  uint32_t t = 30 * MIN;
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(zoneTick(z, c, t, 10));
    t += 20000;
    TEST_ASSERT_FALSE(zoneTick(z, c, t, 10));
    t += 30 * MIN;
  }
  TEST_ASSERT_FALSE(zoneTick(z, c, t, 10));                // 4th run refused
  TEST_ASSERT_TRUE(zoneTick(z, c, DAY_MS, 10));            // new 24 h window
}

static void test_zone_manual_run_is_capped_and_free() {
  Zone z; RelayCfg c = zoneCfg();
  zoneRun(z, 1000, 10 * MIN);                              // asks for 10 min, cap is 5
  TEST_ASSERT_TRUE(zoneTick(z, c, 1000 + 300000 - 1, 50));
  TEST_ASSERT_FALSE(zoneTick(z, c, 1000 + 300000, 50));
  TEST_ASSERT_EQUAL(0, z.runs);                            // manual runs don't use the cap
}

static void test_zone_stop_starts_soak() {
  Zone z; RelayCfg c = zoneCfg();
  zoneRun(z, 40 * MIN, 60000);
  zoneStop(z, 40 * MIN + 5000);
  TEST_ASSERT_FALSE(z.on);
  TEST_ASSERT_FALSE(zoneTick(z, c, 40 * MIN + 5000 + 29 * MIN, 10));
}

static void test_zone_survives_millis_wrap() {
  Zone z; RelayCfg c = zoneCfg();
  uint32_t start = 0xFFFFF000u;                            // 4 s before the 32-bit wrap
  z.dayStart = start;
  zoneRun(z, start, 20000);
  TEST_ASSERT_TRUE(zoneTick(z, c, start + 19999, 50));     // wraps past zero here
  TEST_ASSERT_FALSE(zoneTick(z, c, start + 20000, 50));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_moisture_normal_sensor);
  RUN_TEST(test_moisture_inverted_sensor);
  RUN_TEST(test_moisture_rejects_junk_calibration);
  RUN_TEST(test_sensor_fault);
  RUN_TEST(test_battery_pct);
  RUN_TEST(test_median);
  RUN_TEST(test_zone_waits_out_soak_after_boot);
  RUN_TEST(test_zone_runs_then_soaks);
  RUN_TEST(test_zone_only_waters_when_dry);
  RUN_TEST(test_zone_daily_cap);
  RUN_TEST(test_zone_manual_run_is_capped_and_free);
  RUN_TEST(test_zone_stop_starts_soak);
  RUN_TEST(test_zone_survives_millis_wrap);
  return UNITY_END();
}
