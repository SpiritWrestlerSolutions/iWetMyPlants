/**
 * @file power_modes.cpp
 * @brief Deep sleep and power management implementation
 */

#include "power_modes.h"
#include <WiFi.h>

namespace iwmp {

// RTC-backed variables (survive deep sleep cycles)
RTC_DATA_ATTR uint32_t rtc_boot_count          = 0;
RTC_DATA_ATTR uint32_t rtc_last_successful_send = 0;
RTC_DATA_ATTR uint8_t  rtc_consecutive_failures = 0;
RTC_DATA_ATTR uint32_t rtc_total_sleep_time     = 0;

void PowerModes::begin(const PowerConfig& config) {
    _config      = config;
    _wake_reason = esp_sleep_get_wakeup_cause();
    rtc_boot_count++;
    _initialized = true;
}

const char* PowerModes::getWakeReasonString() const {
    switch (_wake_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:     return "timer";
        case ESP_SLEEP_WAKEUP_GPIO:      return "gpio";
        case ESP_SLEEP_WAKEUP_EXT0:      return "ext0";
        case ESP_SLEEP_WAKEUP_EXT1:      return "ext1";
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "power_on";
        default:                          return "other";
    }
}

void PowerModes::enterDeepSleep(uint32_t sleep_duration_sec) {
    uint64_t sleep_us = (uint64_t)sleep_duration_sec * 1000000ULL;
    rtc_total_sleep_time += sleep_duration_sec;

    configureWakeSources();
    esp_sleep_enable_timer_wakeup(sleep_us);
    prepareForSleep();
    esp_deep_sleep_start();
}

bool PowerModes::isExternalPowerConnected() const {
    if (_config.power_detect_pin == 0) return false;
    return digitalRead(_config.power_detect_pin) == HIGH;
}

float PowerModes::getBatteryVoltage() const {
    if (_config.battery_adc_pin == 0) return 0.0f;
    uint32_t raw = analogRead(_config.battery_adc_pin);
    // 12-bit ADC, 3.3V ref, assume 1:2 voltage divider
    return (raw / 4095.0f) * 3.3f * 2.0f;
}

uint8_t PowerModes::getBatteryPercent() const {
    return voltageToPercent(getBatteryVoltage());
}

bool PowerModes::isBatteryLow() const {
    if (_config.battery_adc_pin == 0) return false;
    return getBatteryVoltage() < _config.low_battery_voltage;
}

uint32_t PowerModes::calculateOptimalSleepDuration() const {
    uint32_t base = _config.deep_sleep_duration_sec;
    if (base == 0) base = 300; // Default 5 minutes

    // Exponential backoff on failures
    uint32_t backoff = rtc_consecutive_failures;
    if (backoff > MAX_BACKOFF_MULTIPLIER) backoff = MAX_BACKOFF_MULTIPLIER;
    uint32_t sleep_sec = base * (1u + backoff);

    // Extend when battery is low
    if (isBatteryLow()) sleep_sec *= 2;

    // Shorten when on external power
    if (isExternalPowerConnected()) {
        sleep_sec = base / 4;
        if (sleep_sec < 30) sleep_sec = 30;
    }

    if (sleep_sec < MIN_SLEEP_SEC) sleep_sec = MIN_SLEEP_SEC;
    if (sleep_sec > MAX_SLEEP_SEC) sleep_sec = MAX_SLEEP_SEC;

    return sleep_sec;
}

void PowerModes::recordSuccessfulSend() {
    rtc_consecutive_failures = 0;
    rtc_last_successful_send = (uint32_t)(millis() / 1000);
}

void PowerModes::recordFailedSend() {
    if (rtc_consecutive_failures < 255) rtc_consecutive_failures++;
}

void PowerModes::prepareForSleep() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);
}

void PowerModes::configureWakeSources() {
    if (_config.wake_button_pin > 0) {
        configureGpioWake(_config.wake_button_pin, false);
    }
}

void PowerModes::configureGpioWake(uint8_t pin, bool level) {
    uint64_t mask = 1ULL << pin;
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    esp_sleep_enable_ext1_wakeup(mask,
        level ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32H2
    esp_deep_sleep_enable_gpio_wakeup(mask,
        level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#else
    (void)mask; (void)level; // GPIO wake not supported on this target
#endif
}

uint8_t PowerModes::voltageToPercent(float voltage) const {
    if (voltage <= BATTERY_MIN_V) return 0;
    if (voltage >= BATTERY_MAX_V) return 100;
    return (uint8_t)((voltage - BATTERY_MIN_V) /
                     (BATTERY_MAX_V - BATTERY_MIN_V) * 100.0f);
}

} // namespace iwmp
