/**
 * @file power_modes.cpp
 * @brief Deep sleep management implementation for Remote device
 */

#include "power_modes.h"
#include "logger.h"
#include <WiFi.h>
#include <esp_adc_cal.h>
#include <driver/adc.h>

namespace iwmp {

static constexpr const char* TAG = "Power";

// RTC memory variables - persist across deep sleep
RTC_DATA_ATTR uint32_t rtc_boot_count = 0;
RTC_DATA_ATTR uint32_t rtc_last_successful_send = 0;
RTC_DATA_ATTR uint8_t rtc_consecutive_failures = 0;
RTC_DATA_ATTR uint32_t rtc_total_sleep_time = 0;

void PowerModes::begin(const PowerConfig& config) {
    _config = config;
    _wake_reason = esp_sleep_get_wakeup_cause();

    // Increment boot count
    rtc_boot_count++;

    LOG_I(TAG, "Power modes initialized");
    LOG_I(TAG, "Boot count: %lu", rtc_boot_count);
    LOG_I(TAG, "Wake reason: %s", getWakeReasonString());
    LOG_I(TAG, "Total sleep time: %lu sec", rtc_total_sleep_time);

    // Configure ADC for battery reading if pin configured
    if (_config.battery_adc_pin > 0) {
        analogSetPinAttenuation(_config.battery_adc_pin, ADC_11db);
    }

    // Configure power detect pin if configured
    if (_config.power_detect_pin > 0) {
        pinMode(_config.power_detect_pin, INPUT);
    }

    _initialized = true;
}

const char* PowerModes::getWakeReasonString() const {
    switch (_wake_reason) {
        case ESP_SLEEP_WAKEUP_UNDEFINED:    return "POWER_ON";
        case ESP_SLEEP_WAKEUP_ALL:          return "ALL";
        case ESP_SLEEP_WAKEUP_EXT0:         return "EXT0";
        case ESP_SLEEP_WAKEUP_EXT1:         return "EXT1";
        case ESP_SLEEP_WAKEUP_TIMER:        return "TIMER";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:     return "TOUCHPAD";
        case ESP_SLEEP_WAKEUP_ULP:          return "ULP";
        case ESP_SLEEP_WAKEUP_GPIO:         return "GPIO";
        case ESP_SLEEP_WAKEUP_UART:         return "UART";
#ifdef ESP_SLEEP_WAKEUP_WIFI
        case ESP_SLEEP_WAKEUP_WIFI:         return "WIFI";
#endif
#ifdef ESP_SLEEP_WAKEUP_COCPU
        case ESP_SLEEP_WAKEUP_COCPU:        return "COCPU";
#endif
#ifdef ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG
        case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG: return "COCPU_TRAP";
#endif
#ifdef ESP_SLEEP_WAKEUP_BT
        case ESP_SLEEP_WAKEUP_BT:           return "BT";
#endif
        default:                            return "UNKNOWN";
    }
}

void PowerModes::enterDeepSleep(uint32_t sleep_duration_sec) {
    // Clamp sleep duration
    if (sleep_duration_sec < MIN_SLEEP_SEC) {
        sleep_duration_sec = MIN_SLEEP_SEC;
    }
    if (sleep_duration_sec > MAX_SLEEP_SEC) {
        sleep_duration_sec = MAX_SLEEP_SEC;
    }

    LOG_I(TAG, "Entering deep sleep for %lu seconds", sleep_duration_sec);

    // Update total sleep time
    rtc_total_sleep_time += sleep_duration_sec;

    // Prepare for sleep
    prepareForSleep();

    // Configure wake sources
    configureWakeSources();

    // Configure timer wake
    esp_sleep_enable_timer_wakeup(sleep_duration_sec * 1000000ULL);

    // Flush serial output
    Serial.flush();
    delay(10);

    // Enter deep sleep - this function does not return
    esp_deep_sleep_start();
}

bool PowerModes::isExternalPowerConnected() const {
    if (_config.power_detect_pin == 0) {
        return false;
    }

    // Typically USB power detect is high when connected
    return digitalRead(_config.power_detect_pin) == HIGH;
}

float PowerModes::getBatteryVoltage() const {
    if (_config.battery_adc_pin == 0) {
        return 0.0f;
    }

    // Read ADC multiple times and average
    uint32_t sum = 0;
    const int samples = 8;

    for (int i = 0; i < samples; i++) {
        sum += analogRead(_config.battery_adc_pin);
        delayMicroseconds(100);
    }

    uint16_t raw = sum / samples;

    // Convert to voltage
    // Assuming 3.3V reference and 12-bit ADC (4095 max)
    // If using a voltage divider (e.g., 2:1), multiply accordingly
    // Typical: 100K/100K divider = factor of 2
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f;

    return voltage;
}

uint8_t PowerModes::getBatteryPercent() const {
    float voltage = getBatteryVoltage();
    return voltageToPercent(voltage);
}

bool PowerModes::isBatteryLow() const {
    if (_config.battery_adc_pin == 0) {
        return false;  // No battery monitoring
    }

    float voltage = getBatteryVoltage();
    return voltage < _config.low_battery_voltage;
}

uint32_t PowerModes::calculateOptimalSleepDuration() const {
    uint32_t base_duration = _config.deep_sleep_duration_sec;

    if (base_duration == 0) {
        base_duration = 300;  // Default 5 minutes
    }

    // Apply backoff for consecutive failures
    if (rtc_consecutive_failures > 0) {
        uint8_t multiplier = rtc_consecutive_failures;
        if (multiplier > MAX_BACKOFF_MULTIPLIER) {
            multiplier = MAX_BACKOFF_MULTIPLIER;
        }
        base_duration *= multiplier;
        LOG_D(TAG, "Backoff applied: %d failures, multiplier %d",
              rtc_consecutive_failures, multiplier);
    }

    // Sleep longer when battery is low
    if (isBatteryLow()) {
        base_duration *= 2;
        LOG_D(TAG, "Low battery: doubled sleep duration");
    }

    // Sleep shorter when external power connected
    if (isExternalPowerConnected()) {
        base_duration = _config.deep_sleep_duration_sec / 2;
        if (base_duration < MIN_SLEEP_SEC) {
            base_duration = MIN_SLEEP_SEC;
        }
        LOG_D(TAG, "External power: halved sleep duration");
    }

    // Clamp to limits
    if (base_duration < MIN_SLEEP_SEC) {
        base_duration = MIN_SLEEP_SEC;
    }
    if (base_duration > MAX_SLEEP_SEC) {
        base_duration = MAX_SLEEP_SEC;
    }

    return base_duration;
}

void PowerModes::recordSuccessfulSend() {
    rtc_last_successful_send = millis() / 1000;
    rtc_consecutive_failures = 0;
    LOG_D(TAG, "Recorded successful send");
}

void PowerModes::recordFailedSend() {
    rtc_consecutive_failures++;
    if (rtc_consecutive_failures > 255) {
        rtc_consecutive_failures = 255;
    }
    LOG_W(TAG, "Recorded failed send (consecutive: %d)", rtc_consecutive_failures);
}

void PowerModes::prepareForSleep() {
    LOG_D(TAG, "Preparing for sleep");

    // Disconnect WiFi if connected
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Note: ADC power is automatically managed when entering deep sleep
    // No need to call deprecated adc_power_off()

    // Allow time for cleanup
    delay(10);
}

void PowerModes::configureWakeSources() {
    // Configure button wake if enabled
    if (_config.wake_on_button && _config.wake_button_pin > 0) {
        configureGpioWake(_config.wake_button_pin, false);  // Wake on LOW (button press)
        LOG_D(TAG, "Button wake configured on GPIO %d", _config.wake_button_pin);
    }

    // Configure power connect wake if enabled
    if (_config.wake_on_power_connect && _config.power_detect_pin > 0) {
        configureGpioWake(_config.power_detect_pin, true);  // Wake on HIGH (power connect)
        LOG_D(TAG, "Power detect wake configured on GPIO %d", _config.power_detect_pin);
    }
}

void PowerModes::configureGpioWake(uint8_t pin, bool level) {
#if CONFIG_IDF_TARGET_ESP32C3
    // ESP32-C3 only supports GPIO 0-5 for deep sleep wakeup
    if (pin > 5) {
        LOG_W(TAG, "GPIO %d not valid for C3 deep sleep wake (must be 0-5), skipping", pin);
        return;
    }
    esp_deep_sleep_enable_gpio_wakeup(1ULL << pin,
        level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#else
    // ESP32 uses ext0 for single pin wake
    esp_sleep_enable_ext0_wakeup((gpio_num_t)pin, level ? 1 : 0);
#endif
}

uint8_t PowerModes::voltageToPercent(float voltage) const {
    if (voltage >= BATTERY_MAX_V) {
        return 100;
    }
    if (voltage <= BATTERY_MIN_V) {
        return 0;
    }

    // Simple linear interpolation
    // For more accuracy, use a lookup table based on LiPo discharge curve
    float range = BATTERY_MAX_V - BATTERY_MIN_V;
    float percent = ((voltage - BATTERY_MIN_V) / range) * 100.0f;

    return (uint8_t)percent;
}

} // namespace iwmp
