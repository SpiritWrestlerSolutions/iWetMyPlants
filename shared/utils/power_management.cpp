/**
 * @file power_management.cpp
 * @brief Sleep mode and power management implementation
 */

#include "power_management.h"
#include "logger.h"
#include <driver/adc.h>
#include <WiFi.h>

namespace iwmp {

// Global power manager instance
PowerManager Power;

// RTC data storage
static RTC_DATA_ATTR RtcData s_rtc_data;
static constexpr uint32_t RTC_MAGIC = 0x49574D50;  // "IWMP"

static constexpr const char* TAG = "Power";

// Battery voltage thresholds (LiPo)
static constexpr float BATTERY_MAX_V = 4.2f;
static constexpr float BATTERY_MIN_V = 3.0f;
static constexpr float BATTERY_LOW_V = 3.3f;

// Sleep limits
static constexpr uint32_t MIN_SLEEP_SEC = 60;
static constexpr uint32_t MAX_SLEEP_SEC = 3600;
static constexpr uint8_t MAX_BACKOFF_MULTIPLIER = 8;

void PowerManager::begin(const PowerConfig& config) {
    _config = config;

    // Load RTC data
    loadRtcData();

    // Increment boot count
    s_rtc_data.boot_count++;
    saveRtcData();

    LOG_I(TAG, "Power manager initialized");
    LOG_I(TAG, "Boot count: %lu", s_rtc_data.boot_count);
    LOG_I(TAG, "Wake reason: %s", getWakeReasonString());

    // Configure ADC for battery reading
    if (_config.battery_adc_pin > 0) {
        analogSetPinAttenuation(_config.battery_adc_pin, ADC_11db);
    }

    // Configure power detect pin
    if (_config.power_detect_pin > 0) {
        pinMode(_config.power_detect_pin, INPUT);
    }

    _initialized = true;
}

esp_sleep_wakeup_cause_t PowerManager::getWakeReason() {
    return esp_sleep_get_wakeup_cause();
}

const char* PowerManager::getWakeReasonString() {
    esp_sleep_wakeup_cause_t reason = getWakeReason();

    switch (reason) {
        case ESP_SLEEP_WAKEUP_UNDEFINED:    return "POWER_ON";
        case ESP_SLEEP_WAKEUP_ALL:          return "ALL";
        case ESP_SLEEP_WAKEUP_EXT0:         return "EXT0";
        case ESP_SLEEP_WAKEUP_EXT1:         return "EXT1";
        case ESP_SLEEP_WAKEUP_TIMER:        return "TIMER";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:     return "TOUCHPAD";
        case ESP_SLEEP_WAKEUP_ULP:          return "ULP";
        case ESP_SLEEP_WAKEUP_GPIO:         return "GPIO";
        case ESP_SLEEP_WAKEUP_UART:         return "UART";
        default:                            return "UNKNOWN";
    }
}

void PowerManager::enterDeepSleep(uint32_t sleep_duration_sec) {
    // Clamp sleep duration
    if (sleep_duration_sec < MIN_SLEEP_SEC) {
        sleep_duration_sec = MIN_SLEEP_SEC;
    }
    if (sleep_duration_sec > MAX_SLEEP_SEC) {
        sleep_duration_sec = MAX_SLEEP_SEC;
    }

    LOG_I(TAG, "Entering deep sleep for %lu seconds", sleep_duration_sec);

    // Save RTC data
    saveRtcData();

    // Prepare for sleep
    prepareForSleep();

    // Configure wake sources
    configureWakeSources();

    // Configure timer wake
    esp_sleep_enable_timer_wakeup(sleep_duration_sec * 1000000ULL);

    // Flush serial
    Serial.flush();
    delay(10);

    // Enter deep sleep
    esp_deep_sleep_start();
}

void PowerManager::enterLightSleep(uint32_t sleep_duration_ms) {
    LOG_D(TAG, "Entering light sleep for %lu ms", sleep_duration_ms);

    // Configure timer wake
    esp_sleep_enable_timer_wakeup(sleep_duration_ms * 1000ULL);

    // Enter light sleep
    esp_light_sleep_start();

    LOG_D(TAG, "Woke from light sleep");
}

bool PowerManager::isExternalPowerConnected() {
    if (_config.power_detect_pin == 0) {
        return false;
    }
    return digitalRead(_config.power_detect_pin) == HIGH;
}

float PowerManager::getBatteryVoltage() {
    if (_config.battery_adc_pin == 0) {
        return 0.0f;
    }

    // Average multiple readings
    uint32_t sum = 0;
    const int samples = 8;

    for (int i = 0; i < samples; i++) {
        sum += analogRead(_config.battery_adc_pin);
        delayMicroseconds(100);
    }

    uint16_t raw = sum / samples;

    // Convert to voltage (assuming voltage divider 2:1)
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f;

    return voltage;
}

uint8_t PowerManager::getBatteryPercent() {
    float voltage = getBatteryVoltage();
    return voltageToPercent(voltage);
}

bool PowerManager::isBatteryLow() {
    if (_config.battery_adc_pin == 0) {
        return false;
    }

    float voltage = getBatteryVoltage();
    float threshold = _config.low_battery_voltage > 0 ?
                      _config.low_battery_voltage : BATTERY_LOW_V;

    return voltage < threshold;
}

uint32_t PowerManager::calculateOptimalSleepDuration() {
    uint32_t base_duration = _config.deep_sleep_duration_sec;

    if (base_duration == 0) {
        base_duration = 300;  // Default 5 minutes
    }

    // Apply backoff for consecutive failures
    if (s_rtc_data.consecutive_failures > 0) {
        uint8_t multiplier = s_rtc_data.consecutive_failures;
        if (multiplier > MAX_BACKOFF_MULTIPLIER) {
            multiplier = MAX_BACKOFF_MULTIPLIER;
        }
        base_duration *= multiplier;
    }

    // Sleep longer when battery is low
    if (isBatteryLow()) {
        base_duration *= 2;
    }

    // Sleep shorter when external power connected
    if (isExternalPowerConnected()) {
        base_duration = _config.deep_sleep_duration_sec / 2;
        if (base_duration < MIN_SLEEP_SEC) {
            base_duration = MIN_SLEEP_SEC;
        }
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

uint32_t PowerManager::getBootCount() const {
    return s_rtc_data.boot_count;
}

uint8_t PowerManager::getConsecutiveFailures() const {
    return s_rtc_data.consecutive_failures;
}

void PowerManager::recordSuccessfulSend() {
    s_rtc_data.last_successful_send = millis() / 1000;
    s_rtc_data.consecutive_failures = 0;
    saveRtcData();
}

void PowerManager::recordFailedSend() {
    s_rtc_data.consecutive_failures++;
    if (s_rtc_data.consecutive_failures > 255) {
        s_rtc_data.consecutive_failures = 255;
    }
    saveRtcData();
}

void PowerManager::prepareForSleep() {
    // Disconnect WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Note: ADC power is automatically managed when entering deep sleep
    // No need to call deprecated adc_power_off()

    delay(10);
}

void PowerManager::configureWakeSources() {
    // Configure button wake
    if (_config.wake_on_button && _config.wake_button_pin > 0) {
        configureGpioWake(_config.wake_button_pin, false);
    }

    // Configure power connect wake
    if (_config.wake_on_power_connect && _config.power_detect_pin > 0) {
        configureGpioWake(_config.power_detect_pin, true);
    }
}

void PowerManager::configureGpioWake(uint8_t pin, bool level) {
#if CONFIG_IDF_TARGET_ESP32C3
    esp_deep_sleep_enable_gpio_wakeup(1ULL << pin,
        level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#else
    esp_sleep_enable_ext0_wakeup((gpio_num_t)pin, level ? 1 : 0);
#endif
}

void PowerManager::loadRtcData() {
    // Check if RTC data is valid
    if (s_rtc_data.magic != RTC_MAGIC) {
        // Initialize fresh
        s_rtc_data.boot_count = 0;
        s_rtc_data.last_successful_send = 0;
        s_rtc_data.consecutive_failures = 0;
        s_rtc_data.magic = RTC_MAGIC;
        LOG_D(TAG, "RTC data initialized");
    } else {
        LOG_D(TAG, "RTC data loaded");
    }
}

void PowerManager::saveRtcData() {
    s_rtc_data.magic = RTC_MAGIC;
    // RTC_DATA_ATTR is automatically persisted, nothing to do
}

uint8_t PowerManager::voltageToPercent(float voltage) {
    if (voltage >= BATTERY_MAX_V) {
        return 100;
    }
    if (voltage <= BATTERY_MIN_V) {
        return 0;
    }

    // Simple linear interpolation
    float range = BATTERY_MAX_V - BATTERY_MIN_V;
    float percent = ((voltage - BATTERY_MIN_V) / range) * 100.0f;

    return (uint8_t)percent;
}

} // namespace iwmp
